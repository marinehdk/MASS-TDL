"""Tests for external integration profile routes."""
import sys
from unittest.mock import MagicMock, patch

sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.node"] = MagicMock()
sys.modules["rclpy.callback_groups"] = MagicMock()
sys.modules["rclpy.executors"] = MagicMock()
sys.modules["sil_orchestrator.telemetry_bridge"] = MagicMock()
sys.modules["sil_orchestrator.lifecycle_bridge"] = MagicMock()
sys.modules["polars"] = MagicMock()

from fastapi.testclient import TestClient
from sil_orchestrator.main import app


def setup_function():
    TestClient(app).post("/api/v1/integration/profile", json={"name": "default"})


def test_list_profiles_returns_default_and_a4000_external_with_active_default():
    client = TestClient(app)

    resp = client.get("/api/v1/integration/profiles")

    assert resp.status_code == 200
    assert resp.json() == {
        "active_profile": "default",
        "profiles": ["a4000_external", "default"],
    }


def test_selecting_a4000_external_changes_active_profile():
    client = TestClient(app)

    resp = client.post("/api/v1/integration/profile", json={"name": "a4000_external"})

    assert resp.status_code == 200
    assert resp.json()["name"] == "a4000_external"
    assert client.get("/api/v1/integration/profile").json()["name"] == "a4000_external"


def test_probe_uses_active_profile_and_returns_gate_shape():
    client = TestClient(app)
    client.post("/api/v1/integration/profile", json={"name": "a4000_external"})
    fake_report = MagicMock()
    fake_report.to_dict.return_value = {
        "profile_name": "a4000_external",
        "all_clear": True,
        "checks": [
            {
                "gate_id": 1,
                "label": "Profile valid",
                "passed": True,
                "detail": "loaded",
            }
        ],
    }

    with patch(
        "sil_orchestrator.integration.routes.probe_active_profile",
        return_value=fake_report,
    ) as probe:
        resp = client.post("/api/v1/integration/probe")

    assert resp.status_code == 200
    assert resp.json()["checks"][0] == {
        "gate_id": 1,
        "label": "Profile valid",
        "passed": True,
        "detail": "loaded",
    }
    assert resp.json()["all_clear"] is True
    probe.assert_called_once()
    assert probe.call_args.args[0].name == "a4000_external"


def test_unknown_profile_returns_404():
    client = TestClient(app)

    resp = client.post("/api/v1/integration/profile", json={"name": "missing"})

    assert resp.status_code == 404
