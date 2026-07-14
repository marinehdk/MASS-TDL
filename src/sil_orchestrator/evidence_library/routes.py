from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse

from .service import (
    delete_evidence_session,
    delete_evidence_sessions,
    get_config_payload,
    get_decision_frame,
    get_overview_png_path,
    get_replay,
    list_sessions,
    rescan_all,
)

REPO_ROOT = Path(__file__).resolve().parents[3]

router = APIRouter(prefix="/api/v1/evidence-library", tags=["evidence-library"])


@router.get("/config")
async def get_config():
    return get_config_payload(repo_root=REPO_ROOT)


@router.get("/roots")
async def get_roots():
    payload = get_config_payload(repo_root=REPO_ROOT)
    return {"roots": payload["roots"]}


@router.post("/rescan")
async def rescan(request: dict):
    return rescan_all(repo_root=REPO_ROOT, force=bool(request.get("force", False)))


@router.get("/sessions")
async def sessions(limit: int = 200):
    return {"sessions": list_sessions(limit=limit, repo_root=REPO_ROOT)}


def _batch_evidence_ids(request: dict) -> list[str]:
    evidence_ids = request.get("evidence_ids")
    valid = (
        isinstance(evidence_ids, list)
        and 1 <= len(evidence_ids) <= 500
        and all(isinstance(item, str) and item.strip() == item and item for item in evidence_ids)
        and len(set(evidence_ids)) == len(evidence_ids)
    )
    if not valid:
        raise HTTPException(status_code=422, detail="evidence_ids must contain 1 to 500 unique non-empty strings")
    return evidence_ids


@router.post("/sessions/batch-delete")
async def batch_delete_sessions(request: dict):
    return delete_evidence_sessions(_batch_evidence_ids(request), repo_root=REPO_ROOT)


@router.delete("/sessions/{evidence_id}")
async def delete_session(evidence_id: str):
    try:
        return delete_evidence_session(evidence_id, repo_root=REPO_ROOT)
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except PermissionError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/replay")
async def replay(evidence_id: str, scenario_id: str):
    try:
        return get_replay(evidence_id, scenario_id, repo_root=REPO_ROOT)
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc


@router.get("/sessions/{evidence_id}/overview-png")
async def overview_png(evidence_id: str, scenario_id: str | None = None):
    try:
        path = get_overview_png_path(evidence_id, scenario_id=scenario_id, repo_root=REPO_ROOT)
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    return FileResponse(path, media_type="image/png")


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/decision-frame")
async def decision_frame(evidence_id: str, scenario_id: str, sim_t: float):
    try:
        return get_decision_frame(evidence_id, scenario_id, sim_t, repo_root=REPO_ROOT)
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
