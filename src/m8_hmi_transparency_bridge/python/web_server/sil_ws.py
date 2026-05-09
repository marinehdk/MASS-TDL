"""SIL debug WebSocket — /ws/sil_debug (D1.3b).

Independent of /ws/ui_state — separate client set, separate broadcast function.
Receives SAT/ODD updates from RosBridge._trigger_sil_broadcast() and forwards
as SilDebugSchema JSON to all connected SIL HMI clients.
"""
from __future__ import annotations

import asyncio
import logging
from datetime import datetime, timezone

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from web_server.sil_schemas import SilDebugSchema, SilODDPanel, SilSAT1Panel

logger = logging.getLogger("m8_web_backend.sil_ws")
router = APIRouter()

_sil_clients: set[WebSocket] = set()

_current_job_status: str = "idle"
_current_scenario_id: str = "idle"


def set_job_status(scenario_id: str, status: str) -> None:
    global _current_job_status, _current_scenario_id
    _current_scenario_id = scenario_id
    _current_job_status = status


@router.websocket("/ws/sil_debug")
async def sil_debug_stream(ws: WebSocket) -> None:
    await ws.accept()
    _sil_clients.add(ws)
    logger.info("SIL debug client connected (total=%d)", len(_sil_clients))
    try:
        while True:
            await asyncio.sleep(30)
    except WebSocketDisconnect:
        pass
    finally:
        _sil_clients.discard(ws)
        logger.info("SIL debug client disconnected (total=%d)", len(_sil_clients))


async def broadcast_sil_state(sat_msg: object | None, odd_msg: object | None) -> None:
    """Build SilDebugSchema from ROS messages and push to all /ws/sil_debug clients."""
    sat1_panel: SilSAT1Panel | None = None
    odd_panel: SilODDPanel | None = None

    if sat_msg is not None:
        # TODO(d2.x): parse actual SAT threat fields from sat_msg once M2 topics are live
        sat1_panel = SilSAT1Panel(threat_count=0, threats=[])

    if odd_msg is not None:
        zone_map = {0: "A", 1: "B", 2: "C", 3: "D"}
        env_map = {0: "IN", 1: "EDGE", 2: "OUT", 3: "MRC_PREP", 4: "MRC_ACTIVE"}
        odd_panel = SilODDPanel(
            zone=zone_map.get(getattr(odd_msg, "current_zone", 0), "A"),
            envelope_state=env_map.get(getattr(odd_msg, "envelope_state", 0), "IN"),
            conformance_score=float(getattr(odd_msg, "conformance_score", 0.9)),
            confidence=float(getattr(odd_msg, "confidence", 0.9)),
        )

    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id=_current_scenario_id,
        sat1=sat1_panel,
        odd=odd_panel,
        job_status=_current_job_status,  # type: ignore[arg-type]
    )
    payload = schema.model_dump_json()

    dead_clients: set[WebSocket] = set()
    for client in list(_sil_clients):
        try:
            await client.send_text(payload)
        except Exception:
            dead_clients.add(client)
    _sil_clients.difference_update(dead_clients)
