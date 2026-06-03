from __future__ import annotations
import subprocess
import sys
import threading
import time
import logging
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/export")

_arrow_status: dict[str, dict] = {}
_status_lock = threading.Lock()

_MAX_STATUS_AGE_S = 3600  # 1 hour TTL


def _cleanup_old_entries(status_dict: dict) -> None:
    """Remove entries older than _MAX_STATUS_AGE_S."""
    now = time.time()
    with _status_lock:
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
    try:
        result = subprocess.run(
            [sys.executable, str(_mcap_script),
             "--run-dir", str(run_path), "--output", str(out_path)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            with _status_lock:
                _arrow_status[run_id] = {"status": "error", "detail": result.stderr, "_created": time.time()}
        else:
            with _status_lock:
                _arrow_status[run_id] = {"status": "ready", "path": str(out_path), "_created": time.time()}
    except Exception as e:
        logging.error(f"Failed to run arrow conversion for {run_id}: {e}")
        with _status_lock:
            _arrow_status[run_id] = {"status": "error", "detail": str(e), "_created": time.time()}


@router.post("/arrow")
async def export_arrow(request: dict, background_tasks: BackgroundTasks):
    run_id = request.get("run_id", "")
    if not run_id:
        raise HTTPException(status_code=404, detail="Run not found")

    # Path traversal validation
    try:
        target_path = Path(RUN_DIR / run_id).resolve()
        target_path.relative_to(RUN_DIR.resolve())
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid run_id")

    if not target_path.exists():
        raise HTTPException(status_code=404, detail="Run not found")

    out_path = target_path / "replay.arrow"

    with _status_lock:
        status_entry = _arrow_status.get(run_id)
        if status_entry:
            if status_entry["status"] == "processing":
                return {"status": "processing", "run_id": run_id}
            if status_entry["status"] == "ready":
                return {"status": "ready", "run_id": run_id, "path": status_entry["path"]}

        # Check if file exists on disk
        if out_path.exists():
            _arrow_status[run_id] = {"status": "ready", "path": str(out_path), "_created": time.time()}
            return {"status": "ready", "run_id": run_id, "path": str(out_path)}

        # Update status to processing and trigger background build
        _arrow_status[run_id] = {"status": "processing", "_created": time.time()}

    background_tasks.add_task(_build_arrow, run_id)
    return {"status": "processing", "run_id": run_id}


@router.get("/arrow/status/{run_id}")
async def arrow_status(run_id: str):
    # Path traversal validation
    try:
        target_path = Path(RUN_DIR / run_id).resolve()
        target_path.relative_to(RUN_DIR.resolve())
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid run_id")

    with _status_lock:
        return _arrow_status.get(run_id, {"status": "unknown"})
