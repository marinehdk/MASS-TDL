from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter

from .service import get_config_payload, get_decision_frame, get_replay, list_sessions, rescan_all

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
    return {"sessions": list_sessions(limit=limit)}


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/replay")
async def replay(evidence_id: str, scenario_id: str):
    return get_replay(evidence_id, scenario_id)


@router.get("/sessions/{evidence_id}/scenarios/{scenario_id}/decision-frame")
async def decision_frame(evidence_id: str, scenario_id: str, sim_t: float):
    return get_decision_frame(evidence_id, scenario_id, sim_t)
