import json
import shutil
import tempfile
from pathlib import Path

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR

router = APIRouter(prefix="/api/v1/export")

# In-memory status store (Phase 1; Phase 2 use Redis/DB)
_export_status: dict[str, dict] = {}


def _build_marzip(run_id: str) -> str:
    """Background task: assemble Marzip evidence pack."""
    run_path = RUN_DIR / run_id
    export_path = EXPORT_DIR / f"{run_id}_evidence.marzip"
    export_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        # Write manifest
        manifest = {
            "run_id": run_id,
            "toolchain": "sil_orchestrator v0.1.0",
            "format": "marzip-1.0",
        }
        (tmp_dir / "manifest.yaml").write_text(json.dumps(manifest, indent=2))

        # Copy scenario + scoring artefacts if they exist
        for fname in ("scenario.yaml", "scenario.sha256", "scoring.json"):
            src = run_path / fname
            if src.exists():
                shutil.copy(src, tmp_dir / fname)

        # Build zip then rename to .marzip (DNV evidence container suffix)
        zip_base = str(export_path.with_suffix(""))
        shutil.make_archive(zip_base, "zip", tmp_dir)
        zip_path = Path(zip_base + ".zip")
        if zip_path.exists():
            zip_path.rename(export_path)

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
