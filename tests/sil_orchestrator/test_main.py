"""Tests for SIL orchestrator FastAPI app + lifecycle bridge."""
import asyncio
import pytest
from fastapi.testclient import TestClient
from sil_orchestrator.main import app, bridge
from sil_orchestrator.lifecycle_bridge import LifecycleBridge, LifecycleState


@pytest.fixture
def client():
    return TestClient(app)


def _reset_bridge():
    """Reset the module-level bridge singleton between tests."""
    bridge._state = LifecycleState.UNCONFIGURED
    bridge._scenario_id = None


class TestHealth:
    def test_health_returns_ok(self, client):
        resp = client.get("/api/v1/health")
        assert resp.status_code == 200
        assert resp.json()["status"] == "ok"


class TestLifecycleAPI:
    @pytest.fixture(autouse=True)
    def reset(self):
        _reset_bridge()

    def test_configure_sets_inactive(self, client):
        resp = client.post(
            "/api/v1/lifecycle/configure", json={"scenario_id": "r14-head-on"}
        )
        assert resp.status_code == 200
        assert resp.json()["success"] is True

        status = client.get("/api/v1/lifecycle/status").json()
        assert status["current_state"] == "inactive"
        assert status["scenario_id"] == "r14-head-on"

    def test_cannot_activate_from_unconfigured(self, client):
        resp = client.post("/api/v1/lifecycle/activate")
        assert resp.json()["success"] is False

    def test_full_flow(self, client):
        assert client.post(
            "/api/v1/lifecycle/configure", json={"scenario_id": "test"}
        ).json()["success"]
        assert client.post("/api/v1/lifecycle/activate").json()["success"]
        assert (
            client.get("/api/v1/lifecycle/status").json()["current_state"]
            == "active"
        )
        assert client.post("/api/v1/lifecycle/deactivate").json()["success"]


class TestLifecycleBridge:
    def test_initial_state(self):
        bridge = LifecycleBridge()
        assert bridge.current_state.value == "unconfigured"
        assert bridge.scenario_id is None

    def test_valid_transitions(self):
        bridge = LifecycleBridge()
        assert asyncio.run(bridge.configure("s1")).success
        assert bridge.current_state.value == "inactive"
        assert asyncio.run(bridge.activate()).success
        assert bridge.current_state.value == "active"
        assert asyncio.run(bridge.deactivate()).success
        assert bridge.current_state.value == "inactive"
        assert asyncio.run(bridge.cleanup()).success
        assert bridge.current_state.value == "unconfigured"

    def test_configure_from_active_fails(self):
        bridge = LifecycleBridge()
        asyncio.run(bridge.configure("s1"))
        asyncio.run(bridge.activate())
        result = asyncio.run(bridge.configure("s2"))
        assert result.success is False

    def test_deactivate_from_inactive_fails(self):
        bridge = LifecycleBridge()
        result = asyncio.run(bridge.deactivate())
        assert result.success is False
