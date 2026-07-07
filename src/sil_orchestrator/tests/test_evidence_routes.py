from __future__ import annotations

import json
from pathlib import Path

import pytest
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient

from sil_orchestrator import evidence_routes


def _write_trace(path: Path, samples: int = 25, duration_s: float = 10.0) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        for i in range(samples):
            f.write(json.dumps({
                "sim_t": duration_s * i / max(1, samples - 1),
                "wall_t": 1000.0 + i,
                "topic": "/sil/own_ship_state",
                "lat": 63.44 + i * 0.00001,
                "lon": 10.38,
            }) + "\n")


@pytest.mark.asyncio
async def test_start_finalize_get_and_list_session(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    monkeypatch.setattr(
        evidence_routes,
        "generate_trajectory_dashboard",
        lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"],
    )
    _write_trace(tmp_path / "runs" / "trace_current.jsonl")
    app = FastAPI()
    app.include_router(evidence_routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        start = await client.post("/api/v1/evidence/session/start", json={
            "source": "frontend",
            "suite": "frontend",
            "scenario_id": "colreg-rule14-ho",
        })
        assert start.status_code == 200
        session_id = start.json()["session_id"]

        fin = await client.post(f"/api/v1/evidence/session/{session_id}/finalize", json={
            "scenario_id": "colreg-rule14-ho",
            "status": "stopped",
            "run_id": "run-test",
        })
        assert fin.status_code == 200
        assert fin.json()["valid_data"] is True

        got = await client.get(f"/api/v1/evidence/session/{session_id}")
        assert got.status_code == 200
        assert got.json()["session_name"] == session_id

        listed = await client.get("/api/v1/evidence/sessions?limit=5")
        assert listed.status_code == 200
        assert listed.json()["sessions"][0]["session_name"] == session_id


@pytest.mark.asyncio
async def test_finalize_discards_empty_frontend_session(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    _write_trace(tmp_path / "runs" / "trace_current.jsonl", samples=2, duration_s=1.0)
    app = FastAPI()
    app.include_router(evidence_routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        start = await client.post("/api/v1/evidence/session/start", json={
            "source": "frontend",
            "suite": "frontend",
            "scenario_id": "colreg-rule14-ho",
        })
        session_id = start.json()["session_id"]

        fin = await client.post(f"/api/v1/evidence/session/{session_id}/finalize", json={
            "scenario_id": "colreg-rule14-ho",
            "status": "stopped",
        })
        assert fin.status_code == 200
        assert fin.json()["discarded"] is True
        assert not (tmp_path / "runs" / "trace_eval" / session_id).exists()


@pytest.mark.asyncio
async def test_rejects_path_traversal(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    app = FastAPI()
    app.include_router(evidence_routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/api/v1/evidence/session/..%2Fescape")
        assert resp.status_code == 400


@pytest.mark.asyncio
async def test_finalize_returns_evidence_id_when_library_ingest_succeeds(tmp_path, monkeypatch):
    monkeypatch.setattr(evidence_routes, "RUN_DIR", tmp_path / "runs")
    monkeypatch.setattr(evidence_routes, "TRACE_EVAL_DIR", tmp_path / "runs" / "trace_eval")
    monkeypatch.setattr(
        evidence_routes,
        "generate_trajectory_dashboard",
        lambda **kwargs: kwargs["output_png"].write_text("png") or kwargs["output_png"],
    )
    monkeypatch.setattr(
        evidence_routes,
        "ingest_frontend_session",
        lambda session_dir: "abc123",
    )
    _write_trace(tmp_path / "runs" / "trace_current.jsonl")
    app = FastAPI()
    app.include_router(evidence_routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        start = await client.post("/api/v1/evidence/session/start", json={
            "source": "frontend",
            "suite": "frontend",
            "scenario_id": "colreg-rule14-ho",
        })
        session_id = start.json()["session_id"]
        fin = await client.post(f"/api/v1/evidence/session/{session_id}/finalize", json={
            "scenario_id": "colreg-rule14-ho",
            "status": "completed",
            "run_id": "run-test",
        })
        assert fin.status_code == 200
        assert fin.json()["evidence_id"] == "abc123"
