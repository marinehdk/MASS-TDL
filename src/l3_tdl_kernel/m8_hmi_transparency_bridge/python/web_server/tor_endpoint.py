"""POST /api/tor/acknowledge — "Acknowledge SAT-1" button click endpoint.

Design (v1.1.2 §12.4.1 C interaction verification):
  - C++ TorProtocol already validates button-enabled (SAT-1 ≥ 5s displayed).
  - This endpoint forwards the click event via rclpy to C++ node.
"""
from __future__ import annotations

import logging
from datetime import datetime, timezone

from fastapi import APIRouter, HTTPException, Request

from web_server.schemas import TorAcknowledgeRequest, TorAcknowledgeResponse

router = APIRouter()
logger = logging.getLogger("m8_web_backend.tor")


@router.post("/tor/acknowledge", response_model=TorAcknowledgeResponse)
async def acknowledge_tor(
    payload: TorAcknowledgeRequest, request: Request
) -> TorAcknowledgeResponse:
    bridge = request.app.state.ros_bridge
    if bridge is None or not bridge.is_ready():
        raise HTTPException(status_code=503, detail="ROS bridge not ready")
    click_time = datetime.now(tz=timezone.utc)
    accepted = bridge.send_operator_action(
        action_type="tor_acknowledged",
        operator_id=payload.operator_id,
        click_time=click_time,
    )
    if not accepted:
        raise HTTPException(
            status_code=409,
            detail="ToR not in REQUESTED state or button not yet enabled",
        )
    logger.info("ToR acknowledged by %s @ %s", payload.operator_id, click_time.isoformat())
    return TorAcknowledgeResponse(
        accepted=True,
        click_time=click_time,
        operator_id=payload.operator_id,
    )
