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

from fastapi.testclient import TestClient

from sil_orchestrator.main import app
from sil_orchestrator.runtime import routes as runtime_routes
from sil_orchestrator.runtime.compose import ComposeRuntimeError
from sil_orchestrator.runtime.manifests import RuntimeManifestError
from sil_orchestrator.runtime.routes import get_runtime_service


class FakeRuntimeService:
    def __init__(self):
        self.active_route_plugin = "l2-planner-main"

    def summary(self):
        return {
            "active_profile": "integration-local",
            "core_services": [],
            "plugin_roles": self.plugin_roles(),
        }

    def core_services(self):
        return []

    def plugin_roles(self):
        return [
            {
                "role": "route_l2",
                "active_plugin": self.active_route_plugin,
                "plugins": [
                    {"id": "l2-planner-main"},
                    {"id": "tdl-mock-route"},
                ],
            }
        ]

    def restart_core_service(self, service_id):
        return {
            "accepted": False,
            "error": f"unknown core service {service_id!r}",
        }

    def start_core_stack(self):
        return {"accepted": True, "action": "start_core_stack"}

    def stop_core_stack(self, confirm):
        return {
            "accepted": False,
            "error": "confirm must equal STOP_CORE_STACK",
        }

    def switch_plugin(self, role, plugin_id):
        if role != "route_l2":
            return {"accepted": False, "error": f"unknown plugin role {role!r}"}
        if plugin_id not in {"l2-planner-main", "tdl-mock-route"}:
            return {"accepted": False, "error": f"unknown plugin {plugin_id!r}"}
        old_plugin = self.active_route_plugin
        self.active_route_plugin = plugin_id
        return {
            "accepted": True,
            "action": "switch_plugin",
            "role": role,
            "old_plugin": old_plugin,
            "new_plugin": plugin_id,
        }


class FailingRuntimeService(FakeRuntimeService):
    def start_core_stack(self):
        raise ComposeRuntimeError("docker compose failed")


@pytest.fixture
def client():
    service = FakeRuntimeService()
    app.dependency_overrides[get_runtime_service] = lambda: service
    try:
        yield TestClient(app, raise_server_exceptions=False)
    finally:
        app.dependency_overrides.pop(get_runtime_service, None)


@pytest.fixture(autouse=True)
def clear_runtime_service_cache():
    get_runtime_service.cache_clear()
    try:
        yield
    finally:
        app.dependency_overrides.pop(get_runtime_service, None)
        get_runtime_service.cache_clear()


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


def test_runtime_service_rejects_invalid_profile_env(monkeypatch):
    monkeypatch.setenv("TDL_RUNTIME_PROFILE", "missing-profile")
    client = TestClient(app, raise_server_exceptions=False)

    response = client.get("/api/v1/runtime/summary")

    assert response.status_code == 400
    assert response.headers["content-type"].startswith("application/json")
    assert "missing-profile" in response.json()["detail"]


def test_runtime_service_reports_manifest_load_error(monkeypatch):
    def bad_manifest_loader(_directory):
        raise RuntimeManifestError("bad runtime manifest")

    monkeypatch.setattr(runtime_routes, "load_plugin_manifests", bad_manifest_loader)
    client = TestClient(app, raise_server_exceptions=False)

    response = client.get("/api/v1/runtime/summary")

    assert response.status_code == 503
    assert response.headers["content-type"].startswith("application/json")
    assert "bad runtime manifest" in response.json()["detail"]


def test_runtime_command_error_returns_json_http_error():
    app.dependency_overrides[get_runtime_service] = lambda: FailingRuntimeService()
    client = TestClient(app, raise_server_exceptions=False)

    response = client.post("/api/v1/runtime/core/start")

    assert response.status_code == 503
    assert response.headers["content-type"].startswith("application/json")
    assert "docker compose failed" in response.json()["detail"]


def test_plugin_switch_persists_in_cached_service(client):
    response = client.post(
        "/api/v1/runtime/plugins/route_l2/switch",
        json={"plugin_id": "tdl-mock-route"},
    )

    assert response.status_code == 200
    assert response.json()["new_plugin"] == "tdl-mock-route"

    response = client.get("/api/v1/runtime/plugins")

    assert response.status_code == 200
    route_role = next(role for role in response.json()["roles"] if role["role"] == "route_l2")
    assert route_role["active_plugin"] == "tdl-mock-route"


@pytest.mark.parametrize(
    ("role", "plugin_id", "message"),
    [
        ("missing_role", "tdl-mock-route", "missing_role"),
        ("route_l2", "missing-plugin", "missing-plugin"),
    ],
)
def test_plugin_switch_validation_returns_400(client, role, plugin_id, message):
    response = client.post(
        f"/api/v1/runtime/plugins/{role}/switch",
        json={"plugin_id": plugin_id},
    )

    assert response.status_code == 400
    assert message in response.text
