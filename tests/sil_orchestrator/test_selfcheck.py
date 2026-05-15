"""Tests for self-check endpoints."""
from fastapi.testclient import TestClient
from sil_orchestrator.main import app


def test_probe_all_clear():
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    assert resp.status_code == 200
    data = resp.json()
    assert isinstance(data["all_clear"], bool)
    assert len(data["items"]) >= 5


def test_status_returns_8_modules():
    client = TestClient(app)
    resp = client.get("/api/v1/selfcheck/status")
    assert resp.status_code == 200
    body = resp.json()
    assert len(body["modulePulses"]) == 8
    first = body["modulePulses"][0]
    assert first["moduleId"] == "M1"
    assert first["state"] in (0, 1, 2, 3)  # UNKNOWN/GREEN/AMBER/RED
    assert "latencyMs" in first
    assert "messageDrops" in first


def test_probe_items_have_required_fields():
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    for item in resp.json()["items"]:
        assert "name" in item
        assert "passed" in item
        assert "detail" in item
        assert "state" in item


def test_probe_items_state_is_valid_enum():
    """Each check item must return a valid sil.ModulePulse state (0-3)."""
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    for item in resp.json()["items"]:
        assert item["state"] in (0, 1, 2, 3), (
            f"{item['name']}: state={item['state']} not in [0,1,2,3]"
        )


def test_probe_items_have_timestamp():
    client = TestClient(app)
    resp = client.post("/api/v1/selfcheck/probe")
    for item in resp.json()["items"]:
        assert "timestamp" in item, f"{item['name']} missing timestamp"


def test_all_module_pulses_have_valid_state():
    """All 8 module pulses must have valid state enum values (0-3)."""
    client = TestClient(app)
    resp = client.get("/api/v1/selfcheck/status")
    for mp in resp.json()["modulePulses"]:
        assert mp["state"] in (0, 1, 2, 3), (
            f"{mp['moduleId']}: state={mp['state']} not in [0,1,2,3]"
        )
