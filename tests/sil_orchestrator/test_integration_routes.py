"""Tests for external integration profile routes."""
import subprocess
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
from sil_orchestrator.integration.probe import probe_active_profile
from sil_orchestrator.integration.routes import _profiles
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


def test_probe_active_profile_reports_missing_ros2_as_failed_check():
    def missing_runner(command, env, timeout_s):
        raise FileNotFoundError(command[0])

    report = probe_active_profile(_profiles["a4000_external"], runner=missing_runner)

    assert report.all_clear is False
    assert any(
        not check.passed
        and "topic" in check.label
        and ("FileNotFoundError" in check.detail or "ros2" in check.detail)
        for check in report.checks
    )


def test_probe_active_profile_reports_timeout_as_failed_check():
    def timeout_runner(command, env, timeout_s):
        raise subprocess.TimeoutExpired(command, timeout_s)

    report = probe_active_profile(_profiles["a4000_external"], runner=timeout_runner)

    assert report.all_clear is False
    assert any(
        not check.passed and "topic" in check.label and "timeout" in check.detail.lower()
        for check in report.checks
    )


def test_probe_active_profile_preserves_nonzero_stderr_detail():
    def bad_topic_runner(command, env, timeout_s):
        return subprocess.CompletedProcess(command, 1, stdout="", stderr="bad topic")

    report = probe_active_profile(_profiles["a4000_external"], runner=bad_topic_runner)

    assert report.all_clear is False
    assert any(
        not check.passed and "topic" in check.label and "bad topic" in check.detail
        for check in report.checks
    )


def test_probe_active_profile_passes_matching_topic_type_check():
    def gps_runner(command, env, timeout_s):
        return subprocess.CompletedProcess(
            command, 0, stdout="Type: nmea_interfaces/msg/Gps", stderr=""
        )

    report = probe_active_profile(_profiles["a4000_external"], runner=gps_runner)

    gps_check = next(check for check in report.checks if "/gps/fix" in check.label)
    assert gps_check.passed is True
