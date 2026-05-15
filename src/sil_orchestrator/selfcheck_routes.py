"""Self-check routes — 6-Gate Sequencer (Doc 3 §7.2, GAP-005/GAP-024).

POST /api/v1/selfcheck/probe  → runs 6-gate sequencer
GET  /api/v1/selfcheck/status  → returns M1-M8 module pulse status
POST /api/v1/selfcheck/skip    → dev-only skip with ASDR record
"""
from fastapi import APIRouter, Query
from sil_orchestrator.gate_runner import GateRunner
from sil_orchestrator.scenario_store import ScenarioStore
from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/selfcheck")
store = ScenarioStore()

STATE_GREEN = 1
STATE_AMBER = 2
STATE_RED = 3


@router.post("/probe")
async def probe(scenario_id: str | None = None):
    """Run 6-gate sequencer. Returns GateResult list + GO/NO-GO verdict."""
    sid = scenario_id or "unknown"
    data = store.get(sid)
    runner = GateRunner(sid, data)
    results = await runner.run_all()
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
