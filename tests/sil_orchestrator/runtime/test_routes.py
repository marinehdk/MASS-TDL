import sys
from unittest.mock import MagicMock

import pytest

sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.node"] = MagicMock()
sys.modules["rclpy.callback_groups"] = MagicMock()
sys.modules["rclpy.executors"] = MagicMock()
sys.modules["sil_orchestrator.telemetry_bridge"] = MagicMock()
sys.modules["sil_orchestrator.lifecycle_bridge"] = MagicMock()
sys.modules["polars"] = MagicMock()

try:
    from sil_orchestrator.runtime.routes import get_runtime_service
except ModuleNotFoundError as exc:
    if exc.name != "sil_orchestrator.runtime.routes":
        raise
    get_runtime_service = None

from fastapi.testclient import TestClient

from sil_orchestrator.main import app


class FakeRuntimeService:
    def summary(self):
        return {
            "active_profile": "integration-local",
            "core_services": [],
            "plugin_roles": [],
        }

    def core_services(self):
        return []

    def plugin_roles(self):
        return []

    def restart_core_service(self, service_id):
        return {
            "accepted": False,
            "error": f"unknown core service {service_id!r}",
        }

    def stop_core_stack(self, confirm):
        return {
            "accepted": False,
            "error": "confirm must equal STOP_CORE_STACK",
        }


@pytest.fixture
def client():
    if get_runtime_service is not None:
        app.dependency_overrides[get_runtime_service] = FakeRuntimeService
    try:
        yield TestClient(app)
    finally:
        if get_runtime_service is not None:
            app.dependency_overrides.pop(get_runtime_service, None)


def test_runtime_summary_returns_active_profile(client):
    response = client.get("/api/v1/runtime/summary")

    assert response.status_code == 200
    body = response.json()
    assert "active_profile" in body
    assert "core_services" in body
    assert "plugin_roles" in body


def test_core_single_service_restart_route_rejects_unknown_service(client):
    response = client.post("/api/v1/runtime/core/not-a-service/restart")

    assert response.status_code == 404


def test_stop_core_stack_requires_confirmation(client):
    response = client.post("/api/v1/runtime/core/stop", json={"confirm": "wrong"})

    assert response.status_code == 400
    assert "STOP_CORE_STACK" in response.text
