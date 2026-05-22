import json
import subprocess
import uuid
from pathlib import Path
import zipfile

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR, EXPORT_DIR

router = APIRouter(prefix="/api/v1/export")

_gif_pack_jobs: dict[str, dict] = {}

_PROJECT_ROOT = Path(__file__).resolve().parents[2]
PUPPETEER_SCRIPT = _PROJECT_ROOT / "tools" / "vv" / "puppeteer_batch.js"


def _run_gif_pack(job_id: str, run_ids: list[str]) -> None:
    """Background task: run puppeteer_batch.js for given run IDs, zip results."""
    run_ids_file = None
    try:
        _gif_pack_jobs[job_id]["status"] = "processing"

        # Write run IDs to a temp JSON file for the puppeteer script
        run_ids_file = EXPORT_DIR / f"gif_pack_{job_id}_run_ids.json"
        run_ids_file.write_text(json.dumps(run_ids))

        proc = subprocess.run(
            [
                "node",
                str(PUPPETEER_SCRIPT),
                "--run-ids-file",
                str(run_ids_file),
                "--runs-dir",
                str(RUN_DIR),
            ],
            capture_output=True,
            text=True,
            timeout=600,
            cwd=str(_PROJECT_ROOT),
        )

        if proc.returncode != 0:
            msg = proc.stderr[:500] if proc.stderr else f"exit code {proc.returncode}"
            raise RuntimeError(f"puppeteer_batch.js failed: {msg}")

        # Zip evidence/*.gif and frame_*.png into a single export archive
        evidence_dir = _PROJECT_ROOT / "evidence"
        if evidence_dir.exists():
            zip_path = EXPORT_DIR / f"gif_pack_{job_id}.zip"
            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
                for gif_file in sorted(evidence_dir.rglob("replay.gif")):
                    zf.write(gif_file, gif_file.relative_to(_PROJECT_ROOT))
                for png_file in sorted(evidence_dir.rglob("frame_*.png")):
                    zf.write(png_file, png_file.relative_to(_PROJECT_ROOT))

        _gif_pack_jobs[job_id].update({
            "status": "complete",
            "download_url": f"/exports/gif_pack_{job_id}.zip",
        })

    except Exception as exc:
        _gif_pack_jobs[job_id].update({
            "status": "failed",
            "error": str(exc),
        })
    finally:
        if run_ids_file and run_ids_file.exists():
            run_ids_file.unlink()


@router.post("/gif-pack")
async def trigger_gif_pack(request: dict, background_tasks: BackgroundTasks):
    """POST /api/v1/export/gif-pack

    Body::

        {"run_ids": ["run-abc123", "run-def456"]}

    Returns a ``job_id`` that can be polled via the status endpoint.
    """
    run_ids = request.get("run_ids")
    if not run_ids or not isinstance(run_ids, list) or len(run_ids) == 0:
        raise HTTPException(status_code=422, detail="run_ids must be a non-empty list")

    job_id = f"gif-pack-{uuid.uuid4().hex[:12]}"
    _gif_pack_jobs[job_id] = {"status": "queued", "run_ids": run_ids, "download_url": None}

    background_tasks.add_task(_run_gif_pack, job_id, run_ids)

    return {"job_id": job_id, "status": "queued"}


@router.get("/gif-pack/status/{job_id}")
async def gif_pack_status(job_id: str):
    """GET /api/v1/export/gif-pack/status/{job_id}

    Returns::

        {"status": "processing" | "complete" | "failed",
         "download_url": "…" | None,
         "error": "…" | None}
    """
    job = _gif_pack_jobs.get(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Job not found")

    return {
        "status": job["status"],
        "download_url": job.get("download_url"),
        "error": job.get("error"),
    }
