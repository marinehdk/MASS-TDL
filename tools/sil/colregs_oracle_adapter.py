"""Trace/YAML → oracle-input adapter (Layer 2 of test system v1).

Bridges the raw trace JSONL + scenario YAML to the dict shapes the pure-stdlib
oracles in colregs_module_oracle expect. Pure stdlib, no ROS2/container deps.

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md §5.
"""
from __future__ import annotations

import re
from typing import Any

from tools.sil.colregs_scenario_audit import (
    _straight_line_cpa,
    _encounter_classification,
)


# ─── enum maps: ROS2/C++ int enums ↔ oracle string keys ──────────────────

# M6 Role enum (src/l3_tdl_kernel/m6_colregs_reasoner/include/.../types.hpp):
#   STAND_ON=0, GIVE_WAY=1, BOTH_GIVE_WAY=2, FREE=3.
# BOTH_GIVE_WAY (head-on) is a give-way obligation for both vessels, so it
# maps to the oracle's "GIVE_WAY" role.
ROLE_INT_TO_STR: dict[int, str] = {
    0: "STAND_ON",
    1: "GIVE_WAY",
    2: "GIVE_WAY",
    3: "FREE",
}

# COLREGs rule_id (int) → oracle compiled_rule key. Only the actionable
# maneuvering rules have a classification key; 5/7/8/18 are obligations, not
# sector classifications, and are filtered out by the dominant-rule selector.
RULE_ID_TO_KEY: dict[int, str] = {
    13: "Rule13_Overtaking",
    14: "Rule14_HeadOn",
    15: "Rule15_Crossing",
    17: "Rule17_StandOn",  # stand-on is a role, kept for diagnostics
}

# M4 BehaviorType enum (.../m4_behavior_arbiter/include/.../types.hpp):
#   TRANSIT=0, COLREG_AVOID=1, RECOVERY=7.
BEHAVIOR_INT_TO_STR: dict[int, str] = {
    0: "TRANSIT",
    1: "COLREG_AVOID",
    2: "COLREG_AVOID",   # legacy alternate avoid code
    7: "RECOVERY",
}

_CLOSING_MPS_RE = re.compile(r"closing_mps=([-\d.]+)")


def _parse_closing_mps(rationale: str) -> float | None:
    """Extract closing_mps from M4 behavior_plan rationale. None if absent."""
    m = _CLOSING_MPS_RE.search(rationale)
    return float(m.group(1)) if m else None

# M6 preferred_direction trace value → oracle allowed_actions key.
_DIRECTION_TO_ACTION: dict[str, str] = {
    "STARBOARD": "STARBOARD_TURN",
    "PORT": "PORT_TURN",
    "DECELERATE": "DECELERATE",
    "HOLD": "HOLD",
}


def _value(record: dict[str, Any], key: str, default=None):
    v = record.get(key, default)
    return default if v is None else v


def _dominant_rule_id(active_rule_rows: list[dict[str, Any]]) -> int | None:
    """Pick the dominant maneuvering rule from M6 active_rules entries.

    Priority order reflects COLREGs specificity: head-on (14) is the most
    restrictive and overrides crossing (15), which overrides overtaking (13).
    A head-on encounter that also triggers a crossing sub-rule is still head-on.
    Rule 5/7/8 are obligations and Rule 18 is priority — none define the sector.
    """
    priority = [14, 15, 13]  # head-on > crossing > overtaking
    present = {int(r.get("rule_id", 0)) for r in active_rule_rows
               if isinstance(r, dict)}
    for rid in priority:
        if rid in present:
            return rid
    return None


# ─── scenario YAML → compiled truth ──────────────────────────────────────

def extract_compiled(scenario_doc: dict[str, Any]) -> dict[str, Any]:
    """Build the oracle compiled-truth dict from a scenario YAML document.

    Reuses colregs_scenario_audit._straight_line_cpa (geometric truth) and
    _encounter_classification (rule sector). own_role comes from the declared
    give_way_vessel; BOTH_GIVE_WAY head-on maps to GIVE_WAY.
    """
    geometry = _straight_line_cpa(scenario_doc)
    own_heading = float(
        scenario_doc["ownShip"]["initial"].get("heading",
        scenario_doc["ownShip"]["initial"].get("cog", 0.0)))
    cls = _encounter_classification(geometry, own_heading)
    metadata = scenario_doc.get("metadata", {})
    encounter = metadata.get("encounter", {})
    give_way_decl = str(encounter.get("give_way_vessel", "")).strip().lower()

    # Determine own role: head-on (compiled Rule14) → both give-way; otherwise
    # defer to the declared give_way_vessel field.
    compiled_rule = cls["compiled_rule"]
    if compiled_rule == "Rule14_HeadOn":
        own_role = "GIVE_WAY"
    elif give_way_decl in ("own", "give_way", "give-way"):
        own_role = "GIVE_WAY"
    elif give_way_decl in ("target", "stand_on", "stand-on"):
        own_role = "STAND_ON"
    else:
        own_role = "GIVE_WAY"  # safe default for single-target probes

    allowed = ["STARBOARD_TURN", "DECELERATE"]
    if own_role == "STAND_ON":
        allowed = ["HOLD"]
    return {
        "compiled_rule": compiled_rule,
        "own_role": own_role,
        "allowed_actions": allowed,
        "forbidden_actions": ["PORT_TURN"] if own_role == "GIVE_WAY" else [],
        "classification": cls["classification"],
        "compiled_rel_bearing_deg": cls["compiled_rel_bearing_deg"],
        "geometry": geometry,
    }


# ─── trace → M6 oracle input ──────────────────────────────────────────────

def extract_m6_output(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Extract M6 oracle input from /l3/m6/colregs_constraint trace rows.

    Returns {rule, role, preferred_direction, flip_count, flip_intervals_s}.
    Uses only rows where conflict_detected=true (the active-conflict window).
    """
    m6 = sorted(
        (r for r in rows if r.get("topic") == "/l3/m6/colregs_constraint"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    conflict_rows = [r for r in m6 if bool(_value(r, "conflict_detected", False))]
    if not conflict_rows:
        return {"rule": "", "role": "", "preferred_direction": "",
                "flip_count": 0, "flip_intervals_s": []}

    # Dominant rule from active_rules across the conflict window.
    all_active = [ar for r in conflict_rows
                  for ar in (r.get("active_rules") or [])
                  if isinstance(ar, dict)]
    dom_rid = _dominant_rule_id(all_active)
    rule = RULE_ID_TO_KEY.get(dom_rid, "") if dom_rid else ""

    # Role: mode of primary_role int over the conflict window.
    role_counts: dict[int, int] = {}
    for r in conflict_rows:
        role_counts[int(_value(r, "primary_role", 0))] = (
            role_counts.get(int(_value(r, "primary_role", 0)), 0) + 1)
    dom_role_int = max(role_counts, key=role_counts.get) if role_counts else 3
    role = ROLE_INT_TO_STR.get(dom_role_int, "")

    # Preferred direction: mode of primary_preferred_direction over window.
    dir_counts: dict[str, int] = {}
    for r in conflict_rows:
        d = str(_value(r, "primary_preferred_direction", ""))
        if d:
            dir_counts[d] = dir_counts.get(d, 0) + 1
    dom_dir = max(dir_counts, key=dir_counts.get) if dir_counts else ""
    preferred = _DIRECTION_TO_ACTION.get(dom_dir, dom_dir)

    # Flip count: number of times the dominant rule classification changes
    # WITHIN the conflict window (rule-latch instability). A single continuous
    # conflict episode with a stable dominant rule -> 0 flips. Counting
    # conflict on/off transitions instead would flag a normal single
    # encounter as unstable.
    rule_seq = [_dominant_rule_id(r.get("active_rules") or [])
                for r in conflict_rows]
    flips = 0
    prev_rule = rule_seq[0] if rule_seq else None
    for rid in rule_seq[1:]:
        if rid is not None and prev_rule is not None and rid != prev_rule:
            flips += 1
        if rid is not None:
            prev_rule = rid

    # Conflict-episode onsets (for interval diagnostics).
    onsets: list[float] = []
    prev_conflict = False
    for r in m6:
        c = bool(_value(r, "conflict_detected", False))
        if c and not prev_conflict:
            onsets.append(float(r.get("sim_t", 0.0)))
        prev_conflict = c
    intervals = [round(onsets[i + 1] - onsets[i], 3)
                 for i in range(len(onsets) - 1)]

    return {
        "rule": rule,
        "role": role,
        "preferred_direction": preferred,
        "flip_count": flips,
        "flip_intervals_s": intervals,
    }


# ─── trace → M4 oracle input ──────────────────────────────────────────────

def extract_m4_events(
    rows: list[dict[str, Any]],
    *,
    m6_rows: list[dict[str, Any]] | None = None,
) -> tuple[list[dict[str, Any]], float | None]:
    """Extract M4 oracle input from /l3/m4/behavior_plan trace rows.

    Returns (events, m6_conflict_cleared_t). events is a list of
    {t, behavior_str} sorted by sim_t, with behavior mapped via
    BEHAVIOR_INT_TO_STR. m6_conflict_cleared_t is extracted from M6 rows if
    provided (true→false transition), else None.
    """
    m4 = sorted(
        (r for r in rows if r.get("topic") == "/l3/m4/behavior_plan"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    events = [{
        "t": float(r.get("sim_t", 0.0)),
        "behavior": BEHAVIOR_INT_TO_STR.get(int(_value(r, "behavior", 0)), "UNKNOWN"),
        "closing_mps": _parse_closing_mps(r.get("rationale", "") or ""),
    } for r in m4]

    cleared_t: float | None = None
    if m6_rows is not None:
        m6_sorted = sorted(
            (r for r in m6_rows if r.get("topic") == "/l3/m6/colregs_constraint"),
            key=lambda r: float(r.get("sim_t", 0.0)),
        )
        saw_true = False
        for r in m6_sorted:
            c = bool(_value(r, "conflict_detected", False))
            if c:
                saw_true = True
            elif saw_true:
                cleared_t = float(r.get("sim_t", 0.0))
                break
    return events, cleared_t


# ─── trace → M2 oracle input ──────────────────────────────────────────────

def extract_m2_truth_and_estimate(
    compiled: dict[str, Any],
    *,
    m2_rows: list[dict[str, Any]],
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Build M2 truth (from compiled geometry) + estimate (from M2 trace).

    The estimate falls back to the truth when the M2 trace lacks explicit
    bearing/cpa/tcpa fields (host-side trace bridge does not always forward
    them); the oracle then trivially passes. A real divergence surfaces only
    when the M2 trace carries its own estimates.
    """
    geom = compiled.get("geometry", {})
    # truth bearing: rel_bearing is starboard-positive; oracle expects signed.
    truth = {
        "bearing_deg": geom.get("rel_bearing_deg", 0.0),
        "cpa_m": geom.get("dcpa_m", 0.0),
        "tcpa_s": geom.get("tcpa_s", 0.0),
    }
    est_bearing = _first_float(m2_rows, "target_rel_bearing_deg",
                               default=truth["bearing_deg"])
    est_cpa = _first_float(m2_rows, "target_cpa_m",
                           default=truth["cpa_m"])
    est_tcpa = _first_float(m2_rows, "target_tcpa_s",
                            default=truth["tcpa_s"])
    estimate = {"bearing_deg": est_bearing, "cpa_m": est_cpa, "tcpa_s": est_tcpa}
    return truth, estimate


def _first_float(rows: list[dict[str, Any]], key: str, *, default: float) -> float:
    for r in rows:
        v = r.get(key)
        if v is not None:
            try:
                return float(v)
            except (TypeError, ValueError):
                pass
    return default


# ─── trace → M5 oracle input ──────────────────────────────────────────────

def extract_m5_plan_output(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Extract M5 oracle input from /l3/m5/avoidance_plan trace rows."""
    m5 = [r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan"]
    if not m5:
        return {"solver_status": "EMPTY", "n_waypoints": 0, "oscillation_count": 0}
    statuses = [str(_value(r, "solver_status", _value(r, "status", "EMPTY")))
                for r in m5]
    valid_count = sum(1 for s in statuses if s == "VALID")
    # Oscillation: count of EMPTY→VALID transitions (each is a re-plan churn).
    osc = 0
    prev = None
    for s in statuses:
        if prev == "EMPTY" and s == "VALID":
            osc += 1
        if s in ("VALID", "EMPTY"):
            prev = s
    # n_waypoints: M5 does not always publish a count; approximate from valid runs.
    n_wp = 0
    for r in m5:
        nw = _value(r, "n_waypoints")
        if nw is None:
            nw = _value(r, "waypoint_count")
        if nw is not None:
            try:
                n_wp = max(n_wp, int(nw))
            except (TypeError, ValueError):
                pass
    if valid_count > 0 and n_wp == 0:
        n_wp = 1  # at least one valid plan existed
    dom_status = "VALID" if valid_count > 0 else "EMPTY"
    return {"solver_status": dom_status, "n_waypoints": n_wp,
            "oscillation_count": osc}


# ─── trace → L4 oracle input ──────────────────────────────────────────────

def extract_l4_actuation(rows: list[dict[str, Any]], *, command_t: float,
                         release_t: float | None = None) -> dict[str, Any]:
    """Extract L4 actuation timing from the trace.

    realized_heading_change_deg = max heading excursion from the pre-command
    heading within the avoidance window (command_t..release_t or end). This is
    the true maneuver magnitude, not the first 5deg jitter sample.
    first_realized_t = sim_t at which the heading first reaches >=50% of that
    max excursion (the maneuver is materially underway).
    """
    own = sorted(
        (r for r in rows if r.get("topic") == "/sil/own_ship_state"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    pre = [r for r in own if float(r.get("sim_t", 0.0)) <= command_t]
    base_hdg = float(_value(pre[-1], "heading_deg", 0.0)) if pre else 0.0

    window_end = release_t if release_t is not None else float("inf")
    window = [r for r in own
              if command_t <= float(r.get("sim_t", 0.0)) <= window_end]
    if not window:
        window = [r for r in own if float(r.get("sim_t", 0.0)) >= command_t]

    def _dev(r):
        hdg = float(_value(r, "heading_deg", base_hdg))
        return abs(((hdg - base_hdg + 180.0) % 360.0) - 180.0)

    if not window:
        return {"first_command_t": command_t, "first_realized_t": command_t,
                "realized_heading_change_deg": 0.0}

    max_dev = max(_dev(r) for r in window)
    threshold = max(5.0, 0.5 * max_dev)
    realized_t = command_t
    for r in window:
        if _dev(r) >= threshold:
            realized_t = float(r.get("sim_t", command_t))
            break
    return {
        "first_command_t": command_t,
        "first_realized_t": realized_t,
        "realized_heading_change_deg": round(max_dev, 3),
    }


# ─── trace → M7 oracle input ──────────────────────────────────────────────

def extract_m7_veto(rows: list[dict[str, Any]], *,
                    unsafe_trajectory_present: bool = False) -> dict[str, Any]:
    """Extract M7 veto oracle input.

    M7 (SafetySupervisor) veto events come on /l3/m7/veto or similar. With no
    veto events in the trace, unsafe_trajectory_vetoed is False. The caller
    must set unsafe_trajectory_present when a CPA-floor breach is known (e.g.
    from the runner summary cpa_ok); otherwise a clean run with no veto is a
    correct PASS.
    """
    veto_rows = [r for r in rows
                 if "m7" in str(r.get("topic", "")) and "veto" in str(r.get("topic", "")).lower()]
    return {
        "unsafe_trajectory_vetoed": len(veto_rows) > 0,
        "safe_trajectory_vetoed": False,
        "veto_count": len(veto_rows),
        "unsafe_trajectory_present": unsafe_trajectory_present,
    }
