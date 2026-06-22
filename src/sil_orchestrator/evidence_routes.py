from __future__ import annotations

import json
import threading
from pathlib import Path
from typing import Any

from fastapi import APIRouter, BackgroundTasks, HTTPException

from sil_orchestrator.config import RUN_DIR
from tools.sil.evidence_session import EvidenceSessionManager
from tools.sil.trajectory_dashboard import generate_trajectory_dashboard

TRACE_EVAL_DIR = RUN_DIR / "trace_eval"

router = APIRouter(prefix="/api/v1/evidence", tags=["evidence"])
_session_lock = threading.Lock()
_active_frontend_session: str | None = None


def _manager() -> EvidenceSessionManager:
    return EvidenceSessionManager(root=TRACE_EVAL_DIR, run_root=RUN_DIR)


def _resolve_session(session_id: str) -> Path:
    try:
        target = (TRACE_EVAL_DIR / session_id).resolve()
        target.relative_to(TRACE_EVAL_DIR.resolve())
    except ValueError as exc:
        raise HTTPException(status_code=400, detail="Invalid session_id") from exc
    if not target.exists():
        raise HTTPException(status_code=404, detail="Evidence session not found")
    return target


def _build_png(
    session_dir: Path,
    scenario_entry: dict[str, Any],
    scenario_id: str,
    session_name: str,
) -> None:
    trace_name = scenario_entry.get("trace_path")
    if not trace_name:
        return
    trace_path = session_dir / str(trace_name)
    report_name = scenario_entry.get("report_path")
    report_path = session_dir / str(report_name) if report_name else None
    output_png = session_dir / str(
        scenario_entry.get("png_path") or f"{scenario_id}_trajectory_dashboard.png"
    )
    generate_trajectory_dashboard(
        trace_jsonl=trace_path,
        report_json=report_path,
        output_png=output_png,
        scenario_id=scenario_id,
        session_name=session_name,
    )


@router.post("/session/start")
async def start_session(request: dict):
    global _active_frontend_session
    source = str(request.get("source", "frontend"))
    suite = str(request.get("suite", "frontend"))
    scenario_id = request.get("scenario_id")
    session = _manager().start(source=source, suite=suite, scenario_id=scenario_id)
    if source == "frontend":
        with _session_lock:
            _active_frontend_session = session.session_name
    manifest = json.loads((session.session_dir / "manifest.json").read_text())
    return {
        "session_id": session.session_name,
        "session_name": session.session_name,
        "path": str(session.session_dir),
        "manifest": manifest,
    }


@router.post("/session/{session_id:path}/finalize")
async def finalize_session(
    session_id: str,
    request: dict,
    background_tasks: BackgroundTasks,
):
    global _active_frontend_session
    session_dir = _resolve_session(session_id)
    mgr = _manager()
    session = mgr.from_dir(session_dir)
    scenario_id = str(request.get("scenario_id", "unknown"))
    status = str(request.get("status", "stopped"))
    run_id = request.get("run_id")
    report_path = request.get("report_path")
    scenario_entry = mgr.archive_scenario(
        session,
        scenario_id,
        trace_path=RUN_DIR / "trace_current.jsonl",
        report_path=Path(report_path) if report_path else None,
        status=status,
        run_id=str(run_id) if run_id else None,
    )
    manifest = mgr.finalize(session, status=status)
    if manifest is None:
        if _active_frontend_session == session_id:
            with _session_lock:
                _active_frontend_session = None
        return {"discarded": True, "session_id": session_id, "valid_data": False}
    if scenario_entry.get("valid_data"):
        background_tasks.add_task(
            _build_png,
            session.session_dir,
            scenario_entry,
            scenario_id,
            session.session_name,
        )
    if _active_frontend_session == session_id:
        with _session_lock:
            _active_frontend_session = None
    return {"discarded": False, **manifest}


@router.get("/session/{session_id:path}")
async def get_session(session_id: str):
    session_dir = _resolve_session(session_id)
    manifest_path = session_dir / "manifest.json"
    if not manifest_path.exists():
        raise HTTPException(status_code=404, detail="manifest.json not found")
    return json.loads(manifest_path.read_text())


@router.get("/sessions")
async def list_sessions(limit: int = 50):
    limit = max(1, min(int(limit), 200))
    if not TRACE_EVAL_DIR.exists():
        return {"sessions": []}
    manifests = []
    for manifest_path in sorted(
        TRACE_EVAL_DIR.glob("*/manifest.json"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    ):
        try:
            manifests.append(json.loads(manifest_path.read_text()))
        except Exception:
            continue
        if len(manifests) >= limit:
            break
    return {"sessions": manifests}
