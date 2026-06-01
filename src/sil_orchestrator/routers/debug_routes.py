"""Debug REST endpoints — scenario trace for in-conversation diagnostics.

GET /api/v1/debug/trace?last_n=500   raw JSONL records
GET /api/v1/debug/snapshot           latest record per topic
GET /api/v1/debug/summary            derived: M3/M4/M5 timelines + trajectory stats
"""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from fastapi import APIRouter, Query

from sil_orchestrator.config import RUN_DIR

router = APIRouter()

# Construct the trace file path using RUN_DIR
_TRACE_FILE: Path = RUN_DIR / "trace_current.jsonl"


def _tail_jsonl(n: int) -> list[dict]:
    """Return last n parsed records from _TRACE_FILE."""
    if not _TRACE_FILE.exists():
        return []
    try:
        lines = _TRACE_FILE.read_text(errors="replace").splitlines()
    except Exception:
        return []
    tail = lines[-n:] if len(lines) > n else lines
    out: list[dict] = []
    for line in tail:
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


@router.get("/api/v1/debug/trace")
async def debug_trace(last_n: int = Query(default=500, ge=1, le=10000)):
    records = _tail_jsonl(last_n)
    return {"records": records, "count": len(records)}


@router.get("/api/v1/debug/snapshot")
async def debug_snapshot():
    records = _tail_jsonl(5000)
    latest: dict[str, dict] = {}
    for rec in reversed(records):
        t = rec.get("topic", "")
        if t and t not in latest:
            latest[t] = rec
    max_sim_t = max((r.get("sim_t", 0.0) for r in records), default=0.0)
    return {"sim_t": max_sim_t, "topics": latest}


@router.get("/api/v1/debug/summary")
async def debug_summary():
    records = _tail_jsonl(10000)
    if not records:
        return {"error": "no trace data — start and run a scenario first"}

    # ── M3 task_validity timeline ─────────────────────────────
    # timeline entries contain task_validity, from_sim_t, to_sim_t, target_wp_lat, and target_wp_lon.
    m3 = [r for r in records if r.get("topic") == "/l3/m3/mission_goal"]
    m3_timeline: list[dict] = []
    for r in m3:
        tv = r.get("task_validity", -1)
        t = r.get("sim_t", 0.0)
        if not m3_timeline or m3_timeline[-1]["task_validity"] != tv:
            if m3_timeline:
                m3_timeline[-1]["to_sim_t"] = t
            m3_timeline.append({
                "task_validity": tv,
                "from_sim_t": t,
                "to_sim_t": None,
                "target_wp_lat": r.get("target_wp_lat"),
                "target_wp_lon": r.get("target_wp_lon"),
            })
    if m3_timeline and m3:
        m3_timeline[-1]["to_sim_t"] = m3[-1].get("sim_t")

    # ── M4 behavior phase timeline ────────────────────────────
    # entries contain phase, from_sim_t, and to_sim_t.
    m4 = [r for r in records if r.get("topic") == "/l3/m4/behavior_plan"]
    m4_timeline: list[dict] = []
    for r in m4:
        behavior_id = r.get("behavior", -1)
        phase = "TRANSIT" if behavior_id == 0 else f"BEHAVIOR_{behavior_id}"
        t = r.get("sim_t", 0.0)
        if not m4_timeline or m4_timeline[-1]["phase"] != phase:
            if m4_timeline:
                m4_timeline[-1]["to_sim_t"] = t
            m4_timeline.append({
                "phase": phase,
                "from_sim_t": t,
                "to_sim_t": None,
            })
    if m4_timeline and m4:
        m4_timeline[-1]["to_sim_t"] = m4[-1].get("sim_t")

    # ── M5 solver stats ───────────────────────────────────────
    # has total count, counts for each solver status, and convergence_rate_pct.
    m5 = [r for r in records if r.get("topic") == "/l3/m5/avoidance_plan"]
    counter: Counter = Counter(r.get("solver_status", "UNKNOWN") for r in m5)
    total = sum(counter.values())
    conv_rate = round(counter.get("VALID", 0) / total * 100, 1) if total else 0.0

    # ── Own ship trajectory (≤20 samples) ────────────────────
    oss = [r for r in records if r.get("topic") == "/sil/own_ship_state"]
    if len(oss) <= 20:
        sampled_oss = oss
    else:
        indices = [int(i * (len(oss) - 1) / 19) for i in range(20)]
        sampled_oss = [oss[idx] for idx in indices]

    traj = [
        {
            "sim_t": r["sim_t"],
            "lat": r.get("lat"),
            "lon": r.get("lon"),
            "hdg_deg": r.get("heading_deg"),
            "sog_kn": r.get("sog_kn"),
        }
        for r in sampled_oss
    ]
    headings = [r.get("heading_deg", 0.0) for r in oss]
    # Compute max STARBOARD (clockwise) deviation from initial heading so that
    # a ship returning from 37° back toward 0° through 358-359° doesn't make
    # max(headings) = 359° instead of 37°.
    initial_hdg = headings[0] if headings else 0.0
    starboard_devs = [
        (h - initial_hdg + 360.0) % 360.0
        for h in headings
    ]
    # Only consider clockwise (starboard) deviations ≤ 180°; anything > 180°
    # is a port-side excursion and should not inflate the max.
    capped = [d for d in starboard_devs if d <= 180.0]
    max_dev = max(capped, default=0.0)
    max_hdg = (initial_hdg + max_dev) % 360.0
    max_hdg_t = next(
        (oss[i]["sim_t"] for i, d in enumerate(starboard_devs) if abs(d - max_dev) < 0.1),
        0.0,
    )

    # ── Veto events ───────────────────────────────────────────
    veto_events = [r for r in records if r.get("topic") == "/l3/checker/veto"]

    return {
        "sim_duration_s": max((r.get("sim_t", 0.0) for r in records), default=0.0),
        "total_records": len(records),
        "m3_task_validity_timeline": m3_timeline,
        "m4_phase_timeline": m4_timeline,
        "m5_solver_stats": {
            **dict(counter),
            "total": total,
            "convergence_rate_pct": conv_rate,
        },
        "own_ship_trajectory_sampled": traj,
        "max_heading_deg": round(max_hdg, 1),
        "max_heading_sim_t": max_hdg_t,
        "veto_events": veto_events,
    }
