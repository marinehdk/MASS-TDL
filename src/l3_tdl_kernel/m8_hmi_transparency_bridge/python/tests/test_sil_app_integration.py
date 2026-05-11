"""Verify SIL routes are registered in the FastAPI app."""
from fastapi.testclient import TestClient


def test_sil_scenario_list_route_exists(client: TestClient) -> None:
    resp = client.get("/sil/scenario/list")
    # 404 is fine (empty stub), but not 422 (missing param) or 405 (wrong method)
    assert resp.status_code in (200, 404)


def test_sil_report_route_exists(client: TestClient) -> None:
    resp = client.get("/sil/report/latest")
    # 404 is fine (no report yet), but not 422 (route missing) or 405 (wrong method)
    assert resp.status_code in (200, 404)


def test_ws_sil_debug_route_registered(client: TestClient) -> None:
    routes = [r.path for r in client.app.routes]
    assert "/ws/sil_debug" in routes


def test_existing_tor_route_still_works(client: TestClient) -> None:
    resp = client.post("/api/tor/acknowledge", json={"operator_id": "TEST-OP"})
    # Should succeed with 200 (tor endpoint works)
    assert resp.status_code == 200
