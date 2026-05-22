"""Arrow IPC export routes for SIL replay data."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/export")

_PROJECT_ROOT = Path(__file__).resolve().parents[2]
_CONVERTER = _PROJECT_ROOT / "tools" / "vv" / "mcap_to_arrow.py"
_arrow_status: dict[str, dict[str, str]] = {}


def _build_arrow(run_id: str) -> None:
    run_path = RUN_DIR / run_id
    output_path = run_path / "replay.arrow"
    _arrow_status[run_id] = {"status": "processing", "run_id": run_id}

    command = [
        sys.executable,
        str(_CONVERTER),
        "--run-dir",
        str(run_path),
        "--output",
        str(output_path),
    ]
    result = subprocess.run(
        command,
        cwd=str(_PROJECT_ROOT),
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        _arrow_status[run_id] = {
            "status": "error",
            "run_id": run_id,
            "error": result.stderr.strip() or result.stdout.strip(),
        }
        return

    _arrow_status[run_id] = {
        "status": "ready",
        "run_id": run_id,
        "download_url": f"/api/v1/runs/{run_id}/replay.arrow",
    }


@router.post("/arrow")
async def export_arrow(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    run_path = RUN_DIR / run_id
    if not run_id or not run_path.exists():
        raise HTTPException(status_code=404, detail="Run not found")

    _arrow_status[run_id] = {"status": "processing", "run_id": run_id}
    background_tasks.add_task(_build_arrow, run_id)
    return {"status": "processing", "run_id": run_id}


@router.get("/arrow/status/{run_id}")
async def arrow_status(run_id: str):
    return _arrow_status.get(run_id, {"status": "unknown", "run_id": run_id})
