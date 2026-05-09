"""SIL REST endpoints (D1.3b): scenario management + report access.

Endpoints:
  GET  /sil/scenario/list          - list available YAML scenario files
  POST /sil/scenario/run           - trigger batch execution (async job)
  GET  /sil/scenario/status/{id}   - poll job progress
  GET  /sil/report/latest          - serve latest HTML coverage report
"""
from __future__ import annotations

import asyncio
import logging
import uuid
from pathlib import Path

from fastapi import APIRouter, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

logger = logging.getLogger("m8_web_backend.sil_router")
router = APIRouter()

SCENARIOS_DIR = Path("scenarios/colregs")
REPORTS_DIR = Path("reports")

# In-memory job registry: job_id → {"status": str, "progress": int}
_jobs: dict[str, dict] = {}


class RunRequest(BaseModel):
    scenario_ids: list[str] = ["all"]


@router.get("/scenario/list")
def list_scenarios() -> list[str]:
    if not SCENARIOS_DIR.exists():
        return []
    return sorted(
        f.name for f in SCENARIOS_DIR.glob("*.yaml") if f.name != "schema.yaml"
    )


@router.post("/scenario/run")
async def run_scenarios(req: RunRequest) -> dict:
    # scenario_ids filter deferred to D2.x — currently always runs all scenarios
    job_id = str(uuid.uuid4())[:8]
    _jobs[job_id] = {"status": "running", "progress": 0}
    asyncio.create_task(_run_batch_job(job_id))
    return {"job_id": job_id}


@router.get("/scenario/status/{job_id}")
def get_job_status(job_id: str) -> dict:
    if job_id not in _jobs:
        return {"status": "not_found", "progress": 0}
    return _jobs[job_id]


@router.get("/report/latest", response_class=HTMLResponse)
def get_latest_report() -> str:
    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    html_files = sorted(REPORTS_DIR.glob("coverage_report_*.html"), reverse=True)
    if not html_files:
        raise HTTPException(status_code=404, detail="No coverage report found. Run /sil/scenario/run first.")
    return html_files[0].read_text(encoding="utf-8")


async def _run_batch_job(job_id: str) -> None:
    """Run batch in a thread pool to avoid blocking the event loop."""
    try:
        from web_server import sil_ws
        sil_ws.set_job_status("batch", "running")

        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, _sync_batch_run, job_id)

        _jobs[job_id] = {"status": "done", "progress": 100}
        sil_ws.set_job_status("batch", "done")
        logger.info("Batch job %s completed", job_id)
    except Exception:
        logger.exception("Batch job %s failed", job_id)
        _jobs[job_id] = {"status": "failed", "progress": 0}
        sil_ws.set_job_status("batch", "idle")


def _sync_batch_run(job_id: str) -> None:
    tools_dir = Path(__file__).parent.parent.parent.parent.parent / "tools" / "sil"
    import sys
    sys.path.insert(0, str(tools_dir))

    import batch_runner
    import coverage_reporter
    from datetime import datetime, timezone

    REPORTS_DIR.mkdir(parents=True, exist_ok=True)
    batch_runner.run_batch(SCENARIOS_DIR, REPORTS_DIR)

    ts = datetime.now(tz=timezone.utc).strftime("%Y%m%d")
    html_path = REPORTS_DIR / f"coverage_report_{ts}.html"
    coverage_reporter.generate_report(REPORTS_DIR, html_path)
    logger.info("Coverage report written to %s", html_path)
