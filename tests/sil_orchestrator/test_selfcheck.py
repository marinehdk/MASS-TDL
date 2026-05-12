"""Tests for self-check endpoints."""
from fastapi.testclient import TestClient
from sil_orchestrator.main import app


def test_probe_all_clear():
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    assert resp.status_code == 200
    data = resp.json()
    assert data["all_clear"] is True
    assert len(data["items"]) == 5


def test_status_returns_8_modules():
    client = TestClient(app)
    resp = client.get("/api/v1/selfcheck/status")
    assert resp.status_code == 200
    body = resp.json()
    assert len(body["modulePulses"]) == 8
    # camelCase + numeric state, matches Protobuf ModulePulse + TS types
    first = body["modulePulses"][0]
    assert first["moduleId"] == "M1"
    assert first["state"] == 1  # GREEN
    assert "latencyMs" in first
    assert "messageDrops" in first


def test_probe_items_have_required_fields():
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    for item in resp.json()["items"]:
        assert "name" in item
        assert "passed" in item
        assert "detail" in item
