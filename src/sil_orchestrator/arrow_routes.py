from __future__ import annotations
import subprocess
import sys
import threading
import time
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/export")

_arrow_status: dict[str, dict] = {}

_MAX_STATUS_AGE_S = 3600  # 1 hour TTL


def _cleanup_old_entries(status_dict: dict) -> None:
    """Remove entries older than _MAX_STATUS_AGE_S."""
    now = time.time()
    stale = [k for k, v in status_dict.items()
             if now - v.get("_created", 0) > _MAX_STATUS_AGE_S]
    for k in stale:
        status_dict.pop(k, None)


def _start_cleanup_thread(status_dict: dict) -> None:
    def _loop():
        while True:
            time.sleep(600)  # every 10 minutes
            _cleanup_old_entries(status_dict)
    t = threading.Thread(target=_loop, daemon=True)
    t.start()


_start_cleanup_thread(_arrow_status)


def _build_arrow(run_id: str) -> None:
    run_path = RUN_DIR / run_id
    out_path = run_path / "replay.arrow"
    _mcap_script = Path(__file__).resolve().parents[2] / "tools" / "vv" / "mcap_to_arrow.py"
    result = subprocess.run(
        [sys.executable, str(_mcap_script),
         "--run-dir", str(run_path), "--output", str(out_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        _arrow_status[run_id] = {"status": "error", "detail": result.stderr, "_created": time.time()}
    else:
        _arrow_status[run_id] = {"status": "ready", "path": str(out_path), "_created": time.time()}


@router.post("/arrow")
async def export_arrow(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    if not run_id or not (RUN_DIR / run_id).exists():
        raise HTTPException(status_code=404, detail="Run not found")
    _arrow_status[run_id] = {"status": "processing", "_created": time.time()}
    background_tasks.add_task(_build_arrow, run_id)
    return {"status": "processing", "run_id": run_id}


@router.get("/arrow/status/{run_id}")
async def arrow_status(run_id: str):
    return _arrow_status.get(run_id, {"status": "unknown"})
