"""Shared fixtures for M8 Python tests."""
from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

import sys
import os
# Ensure web_server package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))


class FakeBridge:
    """RosBridge stub for unit tests — no rclpy dependency."""

    def __init__(self) -> None:
        self.ready = True
        self.last_action: dict | None = None
        self._send_returns = True
        self.latest_sat = None   # SIL extension
        self.latest_odd = None   # SIL extension

    async def start(self) -> None:
        pass

    async def stop(self) -> None:
        pass

    def is_ready(self) -> bool:
        return self.ready

    def send_operator_action(
        self, action_type: str, operator_id: str, click_time
    ) -> bool:
        self.last_action = {
            "action_type": action_type,
            "operator_id": operator_id,
            "click_time": click_time,
        }
        return self._send_returns


@pytest.fixture()
def fake_bridge() -> FakeBridge:
    return FakeBridge()


@pytest.fixture()
def app(monkeypatch: pytest.MonkeyPatch, fake_bridge: FakeBridge):
    import web_server.app as app_mod
    monkeypatch.setattr(app_mod, "RosBridge", lambda: fake_bridge)
    from web_server.app import create_app
    return create_app(cors_origins=["http://localhost:3000"])


@pytest.fixture()
def client(app) -> TestClient:
    with TestClient(app) as c:
        yield c
