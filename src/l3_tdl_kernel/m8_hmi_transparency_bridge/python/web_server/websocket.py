"""WebSocket endpoint: push UIState at 50 Hz to frontend."""
from __future__ import annotations

import asyncio
import logging

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from web_server.schemas import UiStateSchema

router = APIRouter()
logger = logging.getLogger("m8_web_backend.websocket")

_active_clients: set[WebSocket] = set()
_clients_lock = asyncio.Lock()


@router.websocket("/ws/ui_state")
async def ui_state_stream(ws: WebSocket) -> None:
    await ws.accept()
    async with _clients_lock:
        _active_clients.add(ws)
    logger.info("WS client connected; total=%d", len(_active_clients))
    try:
        while True:
            _ = await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        async with _clients_lock:
            _active_clients.discard(ws)
        logger.info("WS client disconnected; total=%d", len(_active_clients))


async def broadcast_ui_state(payload: UiStateSchema) -> None:
    """Called by ros_bridge when UIState received: broadcast to all WS clients."""
    text = payload.model_dump_json()
    async with _clients_lock:
        clients = list(_active_clients)
    dead: list[WebSocket] = []
    for ws in clients:
        try:
            await asyncio.wait_for(ws.send_text(text), timeout=1.0)
        except (asyncio.TimeoutError, RuntimeError):
            dead.append(ws)
    if dead:
        async with _clients_lock:
            for ws in dead:
                _active_clients.discard(ws)
