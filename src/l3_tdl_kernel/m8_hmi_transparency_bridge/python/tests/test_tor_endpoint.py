"""Tests for POST /api/tor/acknowledge."""
from __future__ import annotations

from fastapi.testclient import TestClient


def test_acknowledge_returns_accepted(client: TestClient) -> None:
    resp = client.post("/api/tor/acknowledge", json={"operator_id": "ROC-OP-001"})
    assert resp.status_code == 200
    body = resp.json()
    assert body["accepted"] is True
    assert body["operator_id"] == "ROC-OP-001"


def test_acknowledge_rejects_when_bridge_not_ready(
    client: TestClient, fake_bridge
) -> None:
    fake_bridge.ready = False
    resp = client.post("/api/tor/acknowledge", json={"operator_id": "ROC-OP-001"})
    assert resp.status_code == 503


def test_acknowledge_rejects_empty_operator_id(client: TestClient) -> None:
    resp = client.post("/api/tor/acknowledge", json={"operator_id": ""})
    assert resp.status_code == 422  # pydantic validation fails


def test_acknowledge_rejects_when_send_returns_false(
    client: TestClient, fake_bridge
) -> None:
    fake_bridge._send_returns = False
    resp = client.post("/api/tor/acknowledge", json={"operator_id": "OP-002"})
    assert resp.status_code == 409


def test_acknowledge_records_operator_id_in_bridge(
    client: TestClient, fake_bridge
) -> None:
    client.post("/api/tor/acknowledge", json={"operator_id": "OP-TEST"})
    assert fake_bridge.last_action is not None
    assert fake_bridge.last_action["operator_id"] == "OP-TEST"
    assert fake_bridge.last_action["action_type"] == "tor_acknowledged"


def test_acknowledge_rejects_missing_field(client: TestClient) -> None:
    resp = client.post("/api/tor/acknowledge", json={})
    assert resp.status_code == 422


def test_acknowledge_rejects_extra_fields(client: TestClient) -> None:
    resp = client.post(
        "/api/tor/acknowledge",
        json={"operator_id": "OP-001", "extra_field": "value"}
    )
    assert resp.status_code == 422  # extra="forbid"
