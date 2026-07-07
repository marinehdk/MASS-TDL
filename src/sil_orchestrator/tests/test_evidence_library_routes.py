from __future__ import annotations

import json
from pathlib import Path

import pytest
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient

from sil_orchestrator.evidence_library import routes


def _session(root: Path) -> Path:
    session = root / "20260707_132000_single_colreg-rule14-ho"
    session.mkdir(parents=True)
    (session / "manifest.json").write_text(json.dumps({
        "session_name": session.name,
        "source": "cli",
        "suite": "single",
        "created_at": "2026-07-07T13:20:00Z",
        "status": "completed",
        "valid_data": True,
        "scenarios": [{
            "scenario_id": "colreg-rule14-ho",
            "trace_path": "colreg-rule14-ho.trace_current.jsonl",
            "report_path": "colreg-rule14-ho.json",
            "valid_data": True,
        }],
    }))
    (session / "colreg-rule14-ho.json").write_text(json.dumps({"verdict": {"overall_pass": True}, "layers": {}}))
    (session / "colreg-rule14-ho.trace_current.jsonl").write_text(
        json.dumps({"sim_t": 0, "topic": "/sil/own_ship_state", "lat": 0, "lon": 0}) + "\n"
    )
    return session


@pytest.mark.asyncio
async def test_rescan_sessions_replay_and_decision_frame(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    _session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert rescan.status_code == 200
        assert rescan.json()["ingested"] == 1

        listed = await client.get("/api/v1/evidence-library/sessions")
        assert listed.status_code == 200
        evidence_id = listed.json()["sessions"][0]["evidence_id"]

        replay = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/replay")
        assert replay.status_code == 200
        assert replay.json()["duration_s"] == 0.0

        frame = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/decision-frame?sim_t=0")
        assert frame.status_code == 200
        assert frame.json()["evidence_id"] == evidence_id


@pytest.mark.asyncio
async def test_replay_missing_evidence_returns_404(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    repo.mkdir()
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/api/v1/evidence-library/sessions/missing-evidence/scenarios/missing-scenario/replay")
        assert resp.status_code == 404


@pytest.mark.asyncio
async def test_decision_frame_missing_scenario_returns_404(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    _session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert rescan.status_code == 200
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]

        resp = await client.get(
            f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/missing-scenario/decision-frame?sim_t=0"
        )
        assert resp.status_code == 404
