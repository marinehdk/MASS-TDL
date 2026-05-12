"""Tests for export endpoints."""
import tempfile
from pathlib import Path
from fastapi.testclient import TestClient
from sil_orchestrator.main import app
from sil_orchestrator import config


def test_export_nonexistent_run_404(monkeypatch):
    monkeypatch.setattr(config, "RUN_DIR", Path("/nonexistent/path"))
    client = TestClient(app)
    resp = client.post("/api/v1/export/marzip", json={"run_id": "no-such-run"})
    assert resp.status_code == 404


def test_export_status_nonexistent_404():
    client = TestClient(app)
    resp = client.get("/api/v1/export/status/no-such-run")
    assert resp.status_code == 404
