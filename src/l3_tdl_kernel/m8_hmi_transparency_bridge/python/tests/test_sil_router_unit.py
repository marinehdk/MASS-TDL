"""Unit tests for sil_router REST endpoints."""
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from web_server.app import create_app


@pytest.fixture
def client(monkeypatch):
    monkeypatch.setattr("web_server.ros_bridge.RosBridge.start", lambda self: None)
    app = create_app(cors_origins=["*"])
    return TestClient(app)


def test_scenario_list_returns_yaml_files(client, tmp_path, monkeypatch):
    from web_server import sil_router
    fake_yaml = tmp_path / "test.yaml"
    fake_yaml.write_text("schema_version: '1.0'")
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", tmp_path)
    resp = client.get("/sil/scenario/list")
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data, list)


def test_scenario_run_returns_job_id(client, monkeypatch):
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "SCENARIOS_DIR", Path("scenarios/colregs"))
    resp = client.post("/sil/scenario/run", json={"scenario_ids": ["all"]})
    assert resp.status_code == 200
    assert "job_id" in resp.json()


def test_scenario_status_unknown_job(client):
    resp = client.get("/sil/scenario/status/nonexistent-job-id")
    assert resp.status_code == 200
    assert resp.json()["status"] == "not_found"


def test_report_latest_returns_html(client, tmp_path, monkeypatch):
    from web_server import sil_router
    monkeypatch.setattr(sil_router, "REPORTS_DIR", tmp_path)
    fake_html = tmp_path / "coverage_report_20260615.html"
    fake_html.write_text("<html><body>Test Report</body></html>")
    resp = client.get("/sil/report/latest")
    assert resp.status_code == 200
    assert "Test Report" in resp.text
