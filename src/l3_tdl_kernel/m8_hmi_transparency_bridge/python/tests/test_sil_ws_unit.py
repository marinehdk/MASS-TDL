"""Unit tests for sil_ws WebSocket module."""
from unittest.mock import AsyncMock, patch

import pytest

from web_server.sil_ws import broadcast_sil_state, router


def test_router_has_websocket_route():
    routes = [r.path for r in router.routes]
    assert "/ws/sil_debug" in routes


@pytest.mark.asyncio
async def test_broadcast_with_no_clients_does_not_raise():
    # No active clients — should complete without error
    await broadcast_sil_state(None, None)


@pytest.mark.asyncio
async def test_broadcast_sends_to_active_client():
    fake_ws = AsyncMock()
    with patch("web_server.sil_ws._sil_clients", {fake_ws}):
        await broadcast_sil_state(None, None)
    fake_ws.send_text.assert_called_once()
    payload = fake_ws.send_text.call_args[0][0]
    assert "job_status" in payload and "scenario_id" in payload
