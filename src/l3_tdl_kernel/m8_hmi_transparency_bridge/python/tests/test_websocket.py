"""WebSocket smoke test."""
from __future__ import annotations


def test_websocket_broadcast_smoke() -> None:
    """Smoke test: broadcast_ui_state does not raise synchronously.

    Full end-to-end WS test requires pytest-anyio + uvicorn — deferred to Phase E2.
    """
    import asyncio
    from web_server.websocket import broadcast_ui_state, _active_clients
    from web_server.schemas import UiStateSchema
    from datetime import datetime, timezone

    # With no active clients, broadcast should complete silently
    async def run() -> None:
        payload = UiStateSchema(
            timestamp=datetime.now(tz=timezone.utc),
            role="roc_operator",
            scenario="transit",
            sat1_visible=True,
            sat2_visible=False,
            sat3_visible=True,
            sat3_priority_high=False,
            sat3_alert_color="normal",
            sat1_state_summary="TRANSIT @ D3",
            sat1_threats=[],
            sat3_tdl_s=120.0,
            sat3_tmr_s=150.0,
            tor_active=False,
            tor_remaining_s=0.0,
            tor_button_enabled=False,
            override_active=False,
        )
        await broadcast_ui_state(payload)  # no clients → no-op

    asyncio.run(run())
