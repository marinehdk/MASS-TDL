"""Comprehensive SIL HMI tests (D1.3b T15 — ≥10 test functions).

Tests: scenario list, run, status, report, WebSocket push, schema validation.
Does NOT modify any of the 4 existing test files.
"""
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest
from fastapi.testclient import TestClient

from web_server.app import create_app
from web_server.schemas import Sat1ThreatSchema
from web_server.sil_schemas import SilDebugSchema, SilODDPanel, SilSAT1Panel


@pytest.fixture
def app(monkeypatch):
    monkeypatch.setattr("web_server.ros_bridge.RosBridge.start", lambda self: None)
    return create_app(cors_origins=["*"])


@pytest.fixture
def client(app):
    return TestClient(app)


# ── Scenario list ──────────────────────────────────────────────────────────

def test_scenario_list_endpoint_returns_list(client):
    """GET /sil/scenario/list returns a JSON list."""
    resp = client.get("/sil/scenario/list")
    assert resp.status_code == 200
    assert isinstance(resp.json(), list)


def test_scenario_list_contains_yaml_when_dir_exists(client, tmp_path, monkeypatch):
    """GET /sil/scenario/list includes YAML files from SCENARIOS_DIR."""
    (tmp_path / "test_scenario.yaml").write_text("schema_version: '1.0'\nname: test")
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", tmp_path)
    resp = client.get("/sil/scenario/list")
    assert resp.status_code == 200
    assert "test_scenario.yaml" in resp.json()


def test_scenario_list_filters_schema_yaml(client, tmp_path, monkeypatch):
    """GET /sil/scenario/list excludes schema.yaml itself."""
    (tmp_path / "schema.yaml").write_text("x: y")
    (tmp_path / "actual.yaml").write_text("name: actual")
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", tmp_path)
    resp = client.get("/sil/scenario/list")
    data = resp.json()
    assert "schema.yaml" not in data
    assert "actual.yaml" in data


# ── Scenario run ───────────────────────────────────────────────────────────

def test_scenario_run_returns_job_id(client, monkeypatch):
    """POST /sil/scenario/run returns a job_id in response."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))
    resp = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    assert resp.status_code == 200
    body = resp.json()
    assert "job_id" in body
    assert isinstance(body["job_id"], str)
    assert len(body["job_id"]) > 0


def test_scenario_run_creates_trackable_job(client, monkeypatch):
    """POST /sil/scenario/run creates a job that can be queried via status endpoint."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))
    resp = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    job_id = resp.json()["job_id"]

    status_resp = client.get(f"/sil/scenario/status/{job_id}")
    assert status_resp.status_code == 200
    assert status_resp.json()["status"] in ("running", "done", "failed")


def test_scenario_run_different_calls_get_different_ids(client, monkeypatch):
    """Multiple POST /sil/scenario/run calls return different job_ids."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))

    resp1 = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    resp2 = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})

    job_id_1 = resp1.json()["job_id"]
    job_id_2 = resp2.json()["job_id"]
    assert job_id_1 != job_id_2


# ── Job status ─────────────────────────────────────────────────────────────

def test_status_nonexistent_job_returns_not_found(client):
    """GET /sil/scenario/status/{unknown_id} returns not_found status."""
    resp = client.get("/sil/scenario/status/does-not-exist-xyz")
    assert resp.status_code == 200
    body = resp.json()
    assert body["status"] == "not_found"
    assert body["progress"] == 0


def test_status_existing_job_has_progress_field(client, monkeypatch):
    """GET /sil/scenario/status/{id} includes progress field."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))
    run_resp = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    job_id = run_resp.json()["job_id"]

    status_resp = client.get(f"/sil/scenario/status/{job_id}")
    body = status_resp.json()
    assert "progress" in body
    assert isinstance(body["progress"], int)
    assert 0 <= body["progress"] <= 100


def test_status_job_has_valid_status_value(client, monkeypatch):
    """GET /sil/scenario/status/{id} status field is one of valid values."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))
    run_resp = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    job_id = run_resp.json()["job_id"]

    status_resp = client.get(f"/sil/scenario/status/{job_id}")
    status = status_resp.json()["status"]
    assert status in ("running", "done", "failed", "not_found")


# ── Report ─────────────────────────────────────────────────────────────────

def test_report_latest_404_when_no_report(client, tmp_path, monkeypatch):
    """GET /sil/report/latest returns 404 when no coverage report exists."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "REPORTS_DIR", tmp_path)
    resp = client.get("/sil/report/latest")
    assert resp.status_code == 404


def test_report_latest_returns_html_content_type(client, tmp_path, monkeypatch):
    """GET /sil/report/latest returns response with HTML content."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "REPORTS_DIR", tmp_path)
    html_content = "<html><body>Coverage Results</body></html>"
    (tmp_path / "coverage_report_20260615.html").write_text(html_content)
    resp = client.get("/sil/report/latest")
    assert resp.status_code == 200
    assert "Coverage Results" in resp.text


def test_report_latest_returns_most_recent_file(client, tmp_path, monkeypatch):
    """GET /sil/report/latest returns the latest report when multiple exist."""
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "REPORTS_DIR", tmp_path)
    (tmp_path / "coverage_report_20260614.html").write_text("<html>Old</html>")
    (tmp_path / "coverage_report_20260615.html").write_text("<html>New</html>")
    resp = client.get("/sil/report/latest")
    assert resp.status_code == 200
    assert "New" in resp.text
    assert "Old" not in resp.text


# ── WebSocket ──────────────────────────────────────────────────────────────

def test_ws_sil_debug_accepts_connection(client):
    """WebSocket connection to /ws/sil_debug is accepted."""
    with client.websocket_connect("/ws/sil_debug"):
        pass  # Connection established without error


def test_ws_sil_debug_disconnects_gracefully(client):
    """WebSocket disconnect from /ws/sil_debug is handled gracefully."""
    with client.websocket_connect("/ws/sil_debug") as ws:
        ws.close()  # Explicit close succeeds without error


# ── Broadcast ──────────────────────────────────────────────────────────────

@pytest.mark.asyncio
async def test_broadcast_sil_state_with_none_messages():
    """broadcast_sil_state(None, None) completes without error."""
    from web_server.sil_ws import broadcast_sil_state
    await broadcast_sil_state(None, None)  # Should not raise


@pytest.mark.asyncio
async def test_broadcast_sil_state_sends_to_connected_client():
    """broadcast_sil_state sends SilDebugSchema JSON to all connected clients."""
    from web_server.sil_ws import broadcast_sil_state

    fake_ws = AsyncMock()
    with patch("web_server.sil_ws._sil_clients", {fake_ws}):
        await broadcast_sil_state(None, None)

    fake_ws.send_text.assert_called_once()
    payload_json = fake_ws.send_text.call_args[0][0]
    payload = json.loads(payload_json)
    assert "timestamp" in payload
    assert "job_status" in payload
    assert "scenario_id" in payload


# ── Schema Validation ──────────────────────────────────────────────────────

def test_sil_debug_schema_serializes_to_json():
    """SilDebugSchema can be serialized to JSON."""
    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="test_scenario_001",
        job_status="running",
    )
    json_str = schema.model_dump_json()
    assert isinstance(json_str, str)
    data = json.loads(json_str)
    assert data["scenario_id"] == "test_scenario_001"
    assert data["job_status"] == "running"


def test_sil_debug_schema_with_sat1_panel():
    """SilDebugSchema accepts SilSAT1Panel."""
    threat = Sat1ThreatSchema(
        target_id=1,
        cpa_m=50.0,
        tcpa_s=120.0,
        aspect="head_on",
        confidence=0.95,
    )
    sat_panel = SilSAT1Panel(threat_count=1, threats=[threat])
    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="test",
        sat1=sat_panel,
        job_status="done",
    )
    json_str = schema.model_dump_json()
    data = json.loads(json_str)
    assert data["sat1"]["threat_count"] == 1
    assert len(data["sat1"]["threats"]) == 1


def test_sil_debug_schema_with_odd_panel():
    """SilDebugSchema accepts SilODDPanel."""
    odd_panel = SilODDPanel(
        zone="B",
        envelope_state="EDGE",
        conformance_score=0.88,
        confidence=0.92,
    )
    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="test",
        odd=odd_panel,
        job_status="done",
    )
    json_str = schema.model_dump_json()
    data = json.loads(json_str)
    assert data["odd"]["zone"] == "B"
    assert data["odd"]["conformance_score"] == 0.88


def test_sil_debug_schema_optional_fields():
    """SilDebugSchema optional fields default correctly."""
    schema = SilDebugSchema(
        timestamp=datetime.now(tz=timezone.utc),
        scenario_id="test",
    )
    json_str = schema.model_dump_json()
    data = json.loads(json_str)
    assert data["sat1"] is None
    assert data["sat2_reasoning"] is None
    assert data["sat3_tdl_s"] == 0.0
    assert data["sat3_tmr_s"] == 0.0
    assert data["odd"] is None
    assert data["job_status"] == "idle"


# ── Integration & Regression ───────────────────────────────────────────────

def test_tor_endpoint_still_works(client, app):
    """Verify TOR endpoint regression — still registered."""
    # Verify the POST /api/tor/acknowledge endpoint is in the app routes.
    routes = [r.path for r in app.routes]
    assert "/api/tor/acknowledge" in routes


def test_ws_ui_state_still_works(client):
    """Verify WebSocket /ws/ui_state endpoint is unaffected."""
    # WebSocket endpoint exists (even if it may not return data in test)
    # Just verify it doesn't 404
    with client.websocket_connect("/ws/ui_state"):
        pass  # Should connect


def test_sil_endpoints_all_registered(client):
    """All 4 SIL endpoints are registered and respond."""
    resp_list = client.get("/sil/scenario/list")
    assert resp_list.status_code == 200

    resp_run = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    assert resp_run.status_code == 200

    job_id = resp_run.json()["job_id"]
    resp_status = client.get(f"/sil/scenario/status/{job_id}")
    assert resp_status.status_code == 200

    # Report may 404, but endpoint must respond
    resp_report = client.get("/sil/report/latest")
    assert resp_report.status_code in (200, 404)
