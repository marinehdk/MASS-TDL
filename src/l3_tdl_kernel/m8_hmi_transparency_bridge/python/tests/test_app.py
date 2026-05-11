"""FastAPI app CORS and lifecycle tests."""
from __future__ import annotations

from fastapi.testclient import TestClient

from web_server.app import create_app


def test_app_creation_with_cors(client: TestClient) -> None:
    resp = client.options(
        "/api/tor/acknowledge",
        headers={
            "Origin": "http://localhost:3000",
            "Access-Control-Request-Method": "POST",
        },
    )
    assert resp.status_code in {200, 204}


def test_unknown_origin_does_not_reflect(client: TestClient) -> None:
    resp = client.options(
        "/api/tor/acknowledge",
        headers={
            "Origin": "http://evil.example.com",
            "Access-Control-Request-Method": "POST",
        },
    )
    origin_header = resp.headers.get("access-control-allow-origin", "")
    assert origin_header != "http://evil.example.com"


def test_app_has_correct_title() -> None:
    app = create_app(cors_origins=["*"])
    assert app.title == "MASS L3 M8 HMI Backend"


def test_app_version() -> None:
    app = create_app(cors_origins=["*"])
    assert app.version == "1.0.0"
