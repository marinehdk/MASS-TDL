from __future__ import annotations
import subprocess
import sys
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/export")

_arrow_status: dict[str, dict] = {}


def _build_arrow(run_id: str) -> None:
    run_path = RUN_DIR / run_id
    out_path = run_path / "replay.arrow"
    result = subprocess.run(
        [sys.executable, "tools/vv/mcap_to_arrow.py",
         "--run-dir", str(run_path), "--output", str(out_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        _arrow_status[run_id] = {"status": "error", "detail": result.stderr}
    else:
        _arrow_status[run_id] = {"status": "ready", "path": str(out_path)}


@router.post("/arrow")
async def export_arrow(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    if not run_id or not (RUN_DIR / run_id).exists():
        raise HTTPException(status_code=404, detail="Run not found")
    _arrow_status[run_id] = {"status": "processing"}
    background_tasks.add_task(_build_arrow, run_id)
    return {"status": "processing", "run_id": run_id}


@router.get("/arrow/status/{run_id}")
async def arrow_status(run_id: str):
    return _arrow_status.get(run_id, {"status": "unknown"})
