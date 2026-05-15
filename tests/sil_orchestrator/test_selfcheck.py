"""Tests for 6-gate self-check endpoints."""
import sys
from unittest.mock import MagicMock, patch

# Mock rclpy before importing main — sil_orchestrator.main imports rclpy
# at module level, and rclpy is not available on the dev host without ROS2.
sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.node"] = MagicMock()
sys.modules["rclpy.callback_groups"] = MagicMock()
sys.modules["rclpy.executors"] = MagicMock()

# Mock sil_orchestrator.telemetry_bridge if it exists
sys.modules["sil_orchestrator.telemetry_bridge"] = MagicMock()
sys.modules["sil_orchestrator.lifecycle_bridge"] = MagicMock()

# Mock polars — sil_orchestrator.marzip_builder imports polars at module level,
# and polars is not installed on the dev host.
sys.modules["polars"] = MagicMock()

from fastapi.testclient import TestClient
from sil_orchestrator.main import app


def test_probe_returns_gates():
    """POST /probe returns gates array with GO/NO-GO."""
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe", params={"scenario_id": "test-01"})
    assert resp.status_code == 200
    data = resp.json()
    assert "gates" in data
    assert len(data["gates"]) >= 1
    assert data["go_no_go"] in ("GO", "NO-GO")
    assert "scenario_id" in data


def test_probe_gate_has_required_fields():
    """Each gate has gate_id/label/passed/checks/duration_ms/rationale."""
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe", params={"scenario_id": "test-01"})
    for gate in resp.json()["gates"]:
        assert "gate_id" in gate
        assert "label" in gate
        assert "passed" in gate
        assert isinstance(gate["checks"], list)
        assert "duration_ms" in gate
        assert "rationale" in gate


def test_status_returns_8_modules():
    """GET /status returns 8 modulePulses."""
    client = TestClient(app)
    resp = client.get("/api/v1/selfcheck/status")
    assert resp.status_code == 200
    body = resp.json()
    assert len(body["modulePulses"]) == 8
    first = body["modulePulses"][0]
    assert first["moduleId"] == "M1"
    assert first["state"] == 1


def test_probe_graceful_unknown_scenario():
    """Probe with unknown scenario_id should not crash."""
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe", params={"scenario_id": "nonexistent_xyz"})
    assert resp.status_code == 200
    data = resp.json()
    assert data["scenario_id"] == "nonexistent_xyz"


def test_skip_preflight_writes_record():
    """POST /skip writes ASDR record with warning_unverified_run verdict."""
    client = TestClient(app)
    resp = client.post(
        "/api/v1/selfcheck/skip",
        params={"scenario_id": "test-01", "reason": "dev testing"},
    )
    assert resp.status_code == 200
    data = resp.json()
    assert data["skipped"] is True
    assert data["verdict"] == "warning_unverified_run"
