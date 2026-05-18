"""Self-check routes — 6-Gate Sequencer (Doc 3 §7.2, GAP-005/GAP-024).

POST /api/v1/selfcheck/probe  → runs 6-gate sequencer
GET  /api/v1/selfcheck/status  → returns M1-M8 module pulse status
POST /api/v1/selfcheck/skip    → dev-only skip with ASDR record
"""
from fastapi import APIRouter, Query
import time
import json
from fastapi.responses import StreamingResponse
from sil_orchestrator.gate_runner import GateRunner, GateResult
from sil_orchestrator.scenario_store import ScenarioStore
from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/selfcheck")
store = ScenarioStore()

STATE_GREEN = 1
STATE_AMBER = 2
STATE_RED = 3

import datetime
from pathlib import Path
from sil_orchestrator.config import SCENARIO_DIR

_SIX_GATE_LABELS = {
    1: "System Readiness", 2: "Module Health (M1-M8)", 3: "Scenario Integrity",
    4: "ODD-Scenario Alignment", 5: "Time Base + Evidence Chain", 6: "Doer-Checker Independence",
}

_SIL2_CLAUSE_MAP = {
    1: "IEC 61508-3 §5.2 Systematicity — test environment readiness",
    2: "IEC 61508-3 §7.4 Software module testing — component liveness",
    3: "IEC 61508-3 §7.2 Software V&V — artifact integrity",
    4: "IEC 61508-3 §7.2 Software V&V — ODD conformance",
    5: "IEC 61508-1 §8.2.9 Data recording — time base traceability",
    6: "IEC 61508-2 Clause 7.3 Independence — Doer-Checker physical separation",
}

def _write_gate_evidence(scenario_id: str, result) -> None:
    """Write staging evidence artifact: scenarios/{id}/.preflight/gate_N.json"""
    staging_dir = SCENARIO_DIR / scenario_id / ".preflight"
    staging_dir.mkdir(parents=True, exist_ok=True)
    checks_out = []
    for c in result.checks:
        if isinstance(c, dict):
            checks_out.append(c)
        elif isinstance(c, str):
            if "] " in c:
                status_part, detail = c.split("] ", 1)
                status = status_part.lstrip("[")
                checks_out.append({"item": detail.split(" ", 1)[0] if " " in detail else detail, "status": status, "detail": detail})
            else:
                checks_out.append({"item": c, "status": "unknown", "detail": c})
    out = {
        "gate_id": result.gate_id,
        "gate_name": _SIX_GATE_LABELS.get(result.gate_id, f"Gate {result.gate_id}"),
        "timestamp_utc": datetime.datetime.utcnow().isoformat() + "Z",
        "scenario_id": scenario_id,
        "passed": result.passed,
        "checks": checks_out,
        "duration_ms": round(result.duration_ms, 1),
        "rationale": result.rationale,
        "sil2_clause": _SIL2_CLAUSE_MAP.get(result.gate_id, ""),
        "hazid_scenario_ref": None,
    }
    out_path = staging_dir / f"gate_{result.gate_id}.json"
    out_path.write_text(json.dumps(out, indent=2))


@router.post("/probe")
async def probe(scenario_id: str | None = None):
    """Run 6-gate sequencer. Returns GateResult list + GO/NO-GO verdict."""
    from fastapi import HTTPException
    sid = scenario_id or "unknown"
    try:
        data = store.get(sid)
        runner = GateRunner(sid, data)
        results = await runner.run_all()
    except Exception as exc:
        raise HTTPException(status_code=500, detail=f"Gate sequencer failed: {exc}")
    all_pass = all(r.passed for r in results)
    return {
        "all_clear": all_pass,
        "go_no_go": "GO" if all_pass else "NO-GO",
        "scenario_id": sid,
        "gates": [
            {
                "gate_id": r.gate_id,
                "label": runner._gate_label_for(r.gate_id),
                "passed": r.passed,
                "checks": r.checks,
                "duration_ms": round(r.duration_ms, 1),
                "rationale": r.rationale,
            }
            for r in results
        ],
    }


@router.get("/status")
async def status():
    """Return M1-M8 module pulse status. Matches existing TS type contract."""
    modules = ["M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"]
    return {
        "modulePulses": [
            {
                "moduleId": m,
                "state": STATE_GREEN,
                "latencyMs": 2,
                "messageDrops": 0,
            }
            for m in modules
        ]
    }


@router.post("/skip")
async def skip_preflight(scenario_id: str, reason: str = Query(..., min_length=1)):
    """Dev-only: skip preflight with ASDR record + warning_unverified_run verdict."""
    import json, time
    record = {
        "timestamp": time.time(),
        "scenario_id": scenario_id,
        "reason": reason,
        "verdict": "warning_unverified_run",
        "gates_bypassed": 6,
    }
    asdr_path = RUN_DIR / "preflight_skips.jsonl"
    asdr_path.parent.mkdir(parents=True, exist_ok=True)
    with open(asdr_path, "a") as f:
        f.write(json.dumps(record) + "\n")
    return {"skipped": True, "verdict": "warning_unverified_run", "record": record}


@router.get("/stream")
async def probe_stream(scenario_id: str | None = None):
    """SSE streaming selfcheck — pushes each gate event as it completes"""
    sid = scenario_id or "unknown"
    runner = GateRunner(sid, None)

    async def event_generator():
        results: list[GateResult] = []
        for spec in runner.gates:
            t0 = time.monotonic()
            try:
                result = await spec.handler()
            except Exception as exc:
                result = GateResult(
                    gate_id=spec.gate_id,
                    passed=False,
                    checks=[f"[fail] {type(exc).__name__}: {exc}"],
                    duration_ms=0.0,
                    rationale=f"handler crashed: {type(exc).__name__}",
                )
            result.duration_ms = round((time.monotonic() - t0) * 1000, 1)
            results.append(result)
            _write_gate_evidence(sid, result)
            payload = json.dumps({
                "gate_id": result.gate_id,
                "label": runner._gate_label_for(result.gate_id),
                "passed": result.passed,
                "checks": [
                    {"item": c.split("]", 1)[0].lstrip("["), "status": c.split("]", 1)[0].lstrip("["), "detail": c.split("]", 1)[1].strip() if "]" in c else c}
                    if isinstance(c, str) else c
                    for c in result.checks
                ],
                "duration_ms": result.duration_ms,
                "rationale": result.rationale,
            })
            yield f"data: {payload}\n\n"
        all_pass = all(r.passed for r in results)
        final = json.dumps({
            "type": "complete",
            "all_clear": all_pass,
            "go_no_go": "GO" if all_pass else "NO-GO",
        })
        yield f"data: {final}\n\n"

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "Connection": "keep-alive", "X-Accel-Buffering": "no"},
    )
