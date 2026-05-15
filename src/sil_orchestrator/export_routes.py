import json
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR
from sil_orchestrator.marzip_builder import assemble_marzip

router = APIRouter(prefix="/api/v1/export")

# In-memory status store (Phase 1; Phase 2 use Redis/DB)
_export_status: dict[str, dict] = {}


def _build_marzip(run_id: str) -> str:
    """Background task: assemble complete 7-piece Marzip evidence pack."""
    run_path = RUN_DIR / run_id
    if not run_path.exists():
        raise FileNotFoundError(f"Run directory not found: {run_path}")

    export_path = assemble_marzip(run_id, run_dir_parent=RUN_DIR, export_dir=EXPORT_DIR)

    _export_status[run_id] = {
        "status": "complete",
        "download_url": f"/exports/{run_id}_evidence.marzip",
    }
    return str(export_path)


@router.post("/marzip")
async def export_marzip(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    run_path = RUN_DIR / run_id
    if not run_path.exists():
        raise HTTPException(status_code=404, detail="Run not found")

    _export_status[run_id] = {"status": "processing"}
    background_tasks.add_task(_build_marzip, run_id)
    return {"status": "processing"}


@router.get("/status/{run_id}")
async def export_status(run_id: str):
    if run_id not in _export_status:
        raise HTTPException(status_code=404, detail="No export for this run")
    return _export_status[run_id]
