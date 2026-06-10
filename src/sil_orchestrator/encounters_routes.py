"""Encounter injection REST (D1.8).

POST /api/v1/encounters/inject  -> geometry generator -> AddTarget service.
Reusable by frontend buttons and headless test-bed scripts.

OwnShipState.msg units (sil_msgs):
  msg.lat / msg.lon  — degrees
  msg.heading        — radians, nautical (0=N, CW)
  msg.sog            — m/s
"""
from __future__ import annotations

import math
from typing import Optional

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel

from sil_orchestrator.encounter_geometry import OwnState, generate_encounter

router = APIRouter(prefix="/api/v1/encounters")

_MMSI_BASE = 990_000_000
_counter = {"n": 0}
_active_mmsis: set[int] = set()


class InjectBody(BaseModel):
    rule: str
    range_nm: Optional[float] = None
    construct_cpa_m: Optional[float] = None
    approach_angle_deg: Optional[float] = None


def _own_state_from_msg(msg) -> OwnState:
    """Convert OwnShipState msg to OwnState for the geometry generator.

    msg.heading is radians (nautical, 0=N CW) published by ship_dynamics_node
    via _math_heading_to_nav_heading.  OwnState.heading_deg expects degrees in
    the same nautical convention — use heading (not cog) so the geometry is
    consistent with the ship's instantaneous bow direction.
    msg.sog is m/s; OwnState.sog_kn expects knots.
    """
    heading_deg = math.degrees(msg.heading) % 360.0
    sog_kn = msg.sog / 0.514444
    return OwnState(lat=msg.lat, lon=msg.lon,
                    heading_deg=heading_deg, sog_kn=sog_kn)


def _bridge_is_active(bridge) -> bool:
    state = getattr(bridge, "current_state", None)
    if state is None:
        return True
    value = getattr(state, "value", state)
    return str(value).lower() == "active"


@router.post("/inject")
async def inject_encounter(body: InjectBody, request: Request):
    bridge = request.app.state.bridge
    if bridge is None:
        raise HTTPException(503, "ROS bridge unavailable")
    if not _bridge_is_active(bridge):
        raise HTTPException(409, "sim not active")
    own_msg = bridge.get_latest_own_ship()
    if own_msg is None:
        raise HTTPException(409, "own-ship state unavailable")
    get_age = getattr(bridge, "get_latest_own_ship_age_s", None)
    age_s = get_age() if callable(get_age) else None
    if age_s is not None and age_s > 2.0:
        raise HTTPException(409, "own-ship state unavailable")
    try:
        own = _own_state_from_msg(own_msg)
        spawn = generate_encounter(
            body.rule, own, body.range_nm, body.construct_cpa_m,
            body.approach_angle_deg)
    except ValueError as exc:
        raise HTTPException(400, str(exc))
    _counter["n"] += 1
    mmsi = _MMSI_BASE + _counter["n"]
    try:
        res = await bridge.add_target(
            mmsi=mmsi, lat=spawn.lat, lon=spawn.lon,
            heading_deg=spawn.course_deg, sog_kn=spawn.sog_kn, mode="replay")
    except RuntimeError as exc:
        raise HTTPException(409, str(exc)) from exc
    if not getattr(res, "success", False):
        raise HTTPException(409, getattr(res, "message", "inject failed"))
    _active_mmsis.add(mmsi)
    return {"accepted": True, "mmsi": mmsi}


@router.delete("/{mmsi}")
async def remove_encounter(mmsi: int, request: Request):
    bridge = request.app.state.bridge
    if bridge is None:
        raise HTTPException(503, "ROS bridge unavailable")
    try:
        res = await bridge.remove_target(mmsi)
    except RuntimeError as exc:
        raise HTTPException(409, str(exc)) from exc
    removed = bool(getattr(res, "success", False))
    if removed:
        _active_mmsis.discard(mmsi)
    return {"removed": removed}


@router.delete("")
async def clear_encounters(request: Request):
    bridge = request.app.state.bridge
    if bridge is None:
        raise HTTPException(503, "ROS bridge unavailable")

    removed_count = 0
    failed: list[int] = []
    for mmsi in sorted(_active_mmsis):
        try:
            res = await bridge.remove_target(mmsi)
        except RuntimeError as exc:
            raise HTTPException(409, str(exc)) from exc
        if getattr(res, "success", False):
            removed_count += 1
            _active_mmsis.discard(mmsi)
        else:
            failed.append(mmsi)
    body = {"removed_count": removed_count}
    if failed:
        body["failed_mmsis"] = failed
    return body
