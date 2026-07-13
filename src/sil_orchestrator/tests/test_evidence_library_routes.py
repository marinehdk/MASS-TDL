from __future__ import annotations

import json
import shutil
import sqlite3
from pathlib import Path

import pytest
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient

from sil_orchestrator.evidence_library import routes, service
from sil_orchestrator.evidence_library.config import load_effective_config


EVIDENCE_TABLES = (
    "trajectory_downsample",
    "trajectory_samples",
    "state_segments",
    "events",
    "gate_results",
    "artifacts",
    "scenarios",
    "sessions",
)


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
    (session / "colreg-rule14-ho_trajectory_dashboard.png").write_bytes(b"png")
    (session / "colreg-rule14-ho.trace_current.jsonl").write_text(
        json.dumps({"sim_t": 0, "topic": "/sil/own_ship_state", "lat": 0, "lon": 0}) + "\n"
    )
    return session


def _unified_session(repo: Path, run_id: str = "20260709_094036") -> Path:
    run = repo / "runs" / run_id
    trace = run / "trace"
    scenario = trace / "colreg-rule15-cs"
    scenario.mkdir(parents=True)
    (run / "run_meta.json").write_text(json.dumps({
        "run_id": run_id,
        "created_at": "2026-07-09T09:40:36+00:00",
        "source": "cli",
        "mode": "fast",
        "scenario_count": 1,
        "name": "ho-cs-fast-debug",
        "scenarios": ["colreg-rule15-cs"],
        "git_head": "50a1681c1",
        "status": "completed",
        "overall_pass": False,
        "verdicts": {"colreg-rule15-cs": "fail"},
    }))
    (trace / "manifest.json").write_text(json.dumps({
        "session_name": "trace",
        "source": "cli",
        "suite": "clean8",
        "created_at": "2026-07-09T17:42:06+08:00",
        "status": "completed",
        "valid_data": True,
        "scenarios": [{
            "scenario_id": "colreg-rule15-cs",
            "trace_path": "colreg-rule15-cs/trace_current.jsonl",
            "report_path": "colreg-rule15-cs/report.json",
            "png_path": "colreg-rule15-cs/trajectory_dashboard.png",
            "run_id": run_id,
            "valid_data": True,
        }],
    }))
    (trace / "summary.json").write_text(json.dumps({
        "colreg-rule15-cs": {
            "scenario_id": "colreg-rule15-cs",
            "overall_pass": False,
            "cpa_ok": True,
            "stability_pass": True,
            "returned_to_route": False,
            "route_corridor_ok": True,
            "compliance_verdict": "full",
            "phase_semantics": {"phase_semantics_ok": True},
            "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
            "artifact_consistency": {"g_art_ok": True},
        }
    }))
    (scenario / "report.json").write_text(json.dumps({"verdict": {"overall_pass": False}, "layers": {}}))
    (scenario / "trajectory_dashboard.png").write_bytes(b"png")
    (scenario / "m5_timeline.json").write_text(json.dumps({"events": []}))
    (scenario / "trace_current.jsonl").write_text(
        json.dumps({"sim_t": 0, "topic": "/sil/own_ship_state", "lat": 0, "lon": 0}) + "\n"
    )
    return trace


def _app_for(repo: Path, tmp_path: Path, monkeypatch) -> FastAPI:
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)
    return app


def _seed_all_evidence_tables(repo: Path, evidence_id: str, session: dict) -> Path:
    database_path = load_effective_config(repo_root=repo).database_path
    session_id = session["session_id"]
    scenario_id = session["scenario_ids"][0]
    with sqlite3.connect(database_path) as conn:
        conn.execute(
            "insert into state_segments values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (evidence_id, session_id, scenario_id, "M2", "test", 0.0, 1.0, "{}", "test"),
        )
        conn.execute(
            "insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, "
            "event_type, severity, payload_json, source_topic) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (evidence_id, session_id, scenario_id, 0.0, None, "test", "test", "info", "{}", "test"),
        )
        conn.commit()
        assert all(
            conn.execute(
                f"select count(*) from {table} where evidence_id = ?", (evidence_id,)
            ).fetchone()[0]
            > 0
            for table in EVIDENCE_TABLES
        )
    return database_path


def _evidence_row_counts(database_path: Path, evidence_id: str) -> dict[str, int]:
    with sqlite3.connect(database_path) as conn:
        return {
            table: conn.execute(
                f"select count(*) from {table} where evidence_id = ?", (evidence_id,)
            ).fetchone()[0]
            for table in EVIDENCE_TABLES
        }


def _indexed_session_count(repo: Path, evidence_id: str) -> int:
    database_path = load_effective_config(repo_root=repo).database_path
    with sqlite3.connect(database_path) as conn:
        return conn.execute(
            "select count(*) from sessions where evidence_id = ?", (evidence_id,)
        ).fetchone()[0]


@pytest.mark.asyncio
async def test_direct_session_rescan_is_stable_when_called_twice(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    _session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        first = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        second = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert first.status_code == 200
        assert second.status_code == 200
        listed = await client.get("/api/v1/evidence-library/sessions")
        assert len(listed.json()["sessions"]) == 1


@pytest.mark.asyncio
async def test_rescan_prunes_indexed_session_when_manifest_is_removed(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        (session_dir / "manifest.json").unlink()
        response = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 200
    assert response.json()["pruned"] == 1
    assert listed.status_code == 200
    assert listed.json()["sessions"] == []


@pytest.mark.asyncio
async def test_list_hides_missing_manifest_before_rescan_without_pruning_index(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        (session_dir / "manifest.json").unlink()
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert listed.status_code == 200
    assert listed.json()["sessions"] == []
    assert _indexed_session_count(repo, session["evidence_id"]) == 1


@pytest.mark.asyncio
@pytest.mark.parametrize("replacement", ["absent", "file"])
async def test_list_hides_absent_and_non_directory_sessions_before_rescan(
    tmp_path, monkeypatch, replacement
):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        session_dir.rename(tmp_path / "moved-session")
        if replacement == "file":
            session_dir.write_text("not a directory")
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert listed.status_code == 200
    assert listed.json()["sessions"] == []
    assert _indexed_session_count(repo, session["evidence_id"]) == 1


@pytest.mark.asyncio
async def test_rescan_prunes_session_with_symlinked_manifest(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        manifest_path = session_dir / "manifest.json"
        outside_manifest = tmp_path / "outside-manifest.json"
        manifest_path.rename(outside_manifest)
        manifest_path.symlink_to(outside_manifest)

        stale_list = await client.get("/api/v1/evidence-library/sessions")
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})

    assert stale_list.json()["sessions"] == []
    assert rescan.status_code == 200
    assert rescan.json()["pruned"] == 1
    assert _indexed_session_count(repo, session["evidence_id"]) == 0


@pytest.mark.asyncio
async def test_rescan_prunes_symlinked_session_directory(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        outside_session = tmp_path / "outside-session"
        session_dir.rename(outside_session)
        session_dir.symlink_to(outside_session, target_is_directory=True)

        stale_list = await client.get("/api/v1/evidence-library/sessions")
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})

    assert stale_list.json()["sessions"] == []
    assert rescan.status_code == 200
    assert rescan.json()["pruned"] == 1
    assert outside_session.is_dir()
    assert _indexed_session_count(repo, session["evidence_id"]) == 0


@pytest.mark.asyncio
async def test_rescan_prunes_session_with_symlinked_path_component(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        outside_runs = tmp_path / "outside-runs"
        (repo / "runs").rename(outside_runs)
        (repo / "runs").symlink_to(outside_runs, target_is_directory=True)

        stale_list = await client.get("/api/v1/evidence-library/sessions")
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})

    assert stale_list.json()["sessions"] == []
    assert rescan.status_code == 200
    assert rescan.json()["pruned"] == 1
    assert outside_runs.is_dir()
    assert _indexed_session_count(repo, session["evidence_id"]) == 0


@pytest.mark.asyncio
async def test_rescan_prunes_indexed_session_when_session_directory_is_renamed(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        first = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session_dir.rename(tmp_path / "renamed-session")
        response = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert first.status_code == 200
    assert first.json()["ingested"] == 1
    assert response.status_code == 200
    assert response.json()["pruned"] == 1
    assert listed.status_code == 200
    assert listed.json()["sessions"] == []


@pytest.mark.asyncio
async def test_rescan_prune_removes_evidence_from_every_table(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        first = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert first.status_code == 200
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        evidence_id = session["evidence_id"]

    database_path = load_effective_config(repo_root=repo).database_path
    with sqlite3.connect(database_path) as conn:
        session_id = session["session_id"]
        scenario_id = session["scenario_ids"][0]
        conn.execute(
            "insert into state_segments values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (evidence_id, session_id, scenario_id, "M2", "test", 0.0, 1.0, "{}", "test"),
        )
        conn.execute(
            "insert into events(evidence_id, session_id, scenario_id, sim_t, wall_t, module, event_type, severity, payload_json, source_topic) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (evidence_id, session_id, scenario_id, 0.0, None, "test", "test", "info", "{}", "test"),
        )
        conn.commit()
        tables = (
            "trajectory_downsample",
            "trajectory_samples",
            "state_segments",
            "events",
            "gate_results",
            "artifacts",
            "scenarios",
            "sessions",
        )
        assert all(
            conn.execute(f"select count(*) from {table} where evidence_id = ?", (evidence_id,)).fetchone()[0] > 0
            for table in tables
        )

    session_dir.rename(tmp_path / "renamed-session")
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        response = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 200
    assert response.json()["pruned"] == 1
    assert listed.status_code == 200
    assert listed.json()["sessions"] == []
    with sqlite3.connect(database_path) as conn:
        assert all(
            conn.execute(f"select count(*) from {table} where evidence_id = ?", (evidence_id,)).fetchone()[0] == 0
            for table in tables
        )


@pytest.mark.asyncio
async def test_rescan_retains_physical_session_under_disabled_root(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "disabled-root"
    session_dir = _session(root)
    config_home = tmp_path / "config"
    config_home.mkdir()
    config_path = config_home / "evidence_library.json"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)

    def write_root_config(enabled: bool) -> None:
        config_path.write_text(json.dumps({
            "roots": [{
                "root_id": "test-root",
                "label": "Test root",
                "source": "test",
                "path_glob": str(root),
                "enabled": enabled,
                "trusted": True,
            }],
        }))

    write_root_config(True)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        first = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        write_root_config(False)
        response = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert session_dir.is_dir()
    assert (session_dir / "manifest.json").is_file()
    assert first.status_code == 200
    assert first.json()["ingested"] == 1
    assert response.status_code == 200
    assert response.json()["pruned"] == 0
    assert listed.status_code == 200
    assert len(listed.json()["sessions"]) == 1
    assert listed.json()["sessions"][0]["session_path"] == str(session_dir.resolve())


@pytest.mark.asyncio
async def test_rescan_skips_symlink_sessions_when_root_disallows_symlinks(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    outside_root = tmp_path / "outside_trace_eval"
    outside_session = _session(outside_root)
    root.mkdir(parents=True)
    (root / outside_session.name).symlink_to(outside_session, target_is_directory=True)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert rescan.status_code == 200
        listed = await client.get("/api/v1/evidence-library/sessions")
        assert listed.json()["sessions"] == []


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
        session = listed.json()["sessions"][0]
        assert session["scenario_ids"] == ["colreg-rule14-ho"]
        assert session["passed_scenarios"] == 1
        assert session["scenario_count"] == 1
        assert session["overview_png"] == {
            "scenario_id": "colreg-rule14-ho",
            "relative_path": "colreg-rule14-ho_trajectory_dashboard.png",
        }
        assert session["overview_pngs"] == [
            {
                "scenario_id": "colreg-rule14-ho",
                "relative_path": "colreg-rule14-ho_trajectory_dashboard.png",
            }
        ]
        evidence_id = session["evidence_id"]

        overview = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/overview-png")
        assert overview.status_code == 200
        assert overview.headers["content-type"] == "image/png"
        scenario_overview = await client.get(
            f"/api/v1/evidence-library/sessions/{evidence_id}/overview-png?scenario_id=colreg-rule14-ho"
        )
        assert scenario_overview.status_code == 200
        assert scenario_overview.headers["content-type"] == "image/png"

        replay = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/replay")
        assert replay.status_code == 200
        assert replay.json()["duration_s"] == 0.0

        frame = await client.get(f"/api/v1/evidence-library/sessions/{evidence_id}/scenarios/colreg-rule14-ho/decision-frame?sim_t=0")
        assert frame.status_code == 200
        assert frame.json()["evidence_id"] == evidence_id


@pytest.mark.asyncio
async def test_rescan_supports_unified_run_trace_folder(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    _unified_session(repo)
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
        session = listed.json()["sessions"][0]
        assert session["session_id"] == "20260709_094036"
        assert session["source"] == "cli"
        assert session["suite"] == "fast"
        assert session["scenario_ids"] == ["colreg-rule15-cs"]
        assert session["passed_scenarios"] == 0
        assert session["failed_scenarios"] == 1
        assert session["overview_png"] == {
            "scenario_id": "colreg-rule15-cs",
            "relative_path": "colreg-rule15-cs/trajectory_dashboard.png",
        }


@pytest.mark.asyncio
async def test_list_exposes_server_derived_deletion_targets(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    legacy_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        sessions = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"]

    by_id = {session["session_id"]: session for session in sessions}
    unified = by_id[trace_dir.parent.name]
    legacy = by_id[legacy_dir.name]
    assert unified["deletion_allowed"] is True
    assert unified["deletion_target"] == str(trace_dir.parent.resolve())
    assert legacy["deletion_allowed"] is True
    assert legacy["deletion_target"] == str(legacy_dir.resolve())


@pytest.mark.asyncio
async def test_list_disables_and_delete_rejects_untrusted_target(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    config_home = tmp_path / "config"
    config_home.mkdir()
    (config_home / "evidence_library.json").write_text(json.dumps({
        "roots": [{
            "root_id": "untrusted-root",
            "label": "Untrusted root",
            "source": "test",
            "path_glob": str(trace_dir),
            "enabled": True,
            "trusted": False,
        }],
    }))
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        response = await client.delete("/api/v1/evidence-library/sessions/" + session["evidence_id"])

    assert session["deletion_allowed"] is False
    assert session["deletion_target"] is None
    assert "enabled and trusted" in session["deletion_error"]
    assert response.status_code == 409
    assert trace_dir.parent.is_dir()


@pytest.mark.asyncio
async def test_delete_unified_session_removes_run_dir_and_all_index_rows(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        evidence_id = session["evidence_id"]
        database_path = _seed_all_evidence_tables(repo, evidence_id, session)
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 200
    assert response.json() == {
        "evidence_id": evidence_id,
        "deleted_path": str(trace_dir.parent.resolve()),
        "filesystem_deleted": True,
    }
    assert not trace_dir.parent.exists()
    assert listed.json()["sessions"] == []
    assert _evidence_row_counts(database_path, evidence_id) == dict.fromkeys(EVIDENCE_TABLES, 0)


@pytest.mark.asyncio
async def test_delete_legacy_session_removes_only_indexed_session_directory(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)

    assert response.status_code == 200
    assert response.json()["deleted_path"] == str(session_dir.resolve())
    assert response.json()["filesystem_deleted"] is True
    assert not session_dir.exists()
    assert (repo / "runs" / "trace_eval").is_dir()


@pytest.mark.asyncio
async def test_delete_missing_session_target_removes_index_rows_only(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        shutil.rmtree(trace_dir.parent)
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 200
    assert response.json() == {
        "evidence_id": evidence_id,
        "deleted_path": str(trace_dir.parent.resolve()),
        "filesystem_deleted": False,
    }
    assert listed.json()["sessions"] == []


@pytest.mark.asyncio
async def test_delete_missing_legacy_root_removes_index_rows_only(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        evidence_id = session["evidence_id"]
        database_path = _seed_all_evidence_tables(repo, evidence_id, session)
        shutil.rmtree(repo / "runs" / "trace_eval")
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)

    assert response.status_code == 200
    assert response.json() == {
        "evidence_id": evidence_id,
        "deleted_path": str(session_dir.resolve()),
        "filesystem_deleted": False,
    }
    assert _evidence_row_counts(database_path, evidence_id) == dict.fromkeys(EVIDENCE_TABLES, 0)


@pytest.mark.asyncio
async def test_delete_untrusted_root_returns_409_without_deletion(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    config_home = tmp_path / "config"
    config_home.mkdir()
    (config_home / "evidence_library.json").write_text(json.dumps({
        "roots": [{
            "root_id": "untrusted-root",
            "label": "Untrusted root",
            "source": "test",
            "path_glob": str(trace_dir),
            "enabled": True,
            "trusted": False,
        }],
    }))
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 409
    assert trace_dir.parent.is_dir()
    assert len(listed.json()["sessions"]) == 1


@pytest.mark.asyncio
async def test_delete_missing_evidence_returns_404(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    repo.mkdir()
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        response = await client.delete("/api/v1/evidence-library/sessions/missing-evidence")

    assert response.status_code == 404


@pytest.mark.asyncio
async def test_delete_disabled_root_returns_409_without_deletion(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    config_home = tmp_path / "config"
    config_home.mkdir()
    config_path = config_home / "evidence_library.json"

    def write_config(enabled: bool) -> None:
        config_path.write_text(json.dumps({
            "roots": [{
                "root_id": "disabled-root",
                "label": "Disabled root",
                "source": "test",
                "path_glob": str(trace_dir),
                "enabled": enabled,
                "trusted": True,
            }],
        }))

    write_config(True)
    app = _app_for(repo, tmp_path, monkeypatch)
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        write_config(False)
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 409
    assert trace_dir.parent.is_dir()
    assert len(listed.json()["sessions"]) == 1


@pytest.mark.asyncio
async def test_delete_rejects_final_session_symlink(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        outside_trace = tmp_path / "outside-trace"
        trace_dir.rename(outside_trace)
        trace_dir.symlink_to(outside_trace, target_is_directory=True)
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 409
    assert outside_trace.is_dir()
    assert trace_dir.parent.is_dir()
    assert listed.json()["sessions"] == []
    assert _indexed_session_count(repo, evidence_id) == 1


@pytest.mark.asyncio
async def test_delete_rejects_symlink_in_repo_runs_ancestor(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        rescan = await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        assert rescan.json()["ingested"] == 1
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        outside_runs = tmp_path / "outside-runs"
        (repo / "runs").rename(outside_runs)
        (repo / "runs").symlink_to(outside_runs, target_is_directory=True)
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 409
    assert trace_dir.parent.is_dir()
    assert listed.json()["sessions"] == []
    assert _indexed_session_count(repo, evidence_id) == 1


@pytest.mark.asyncio
async def test_delete_rejects_session_path_escape(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    outside_session = _session(tmp_path / "outside-root")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        evidence_id = session["evidence_id"]
        database_path = load_effective_config(repo_root=repo).database_path
        with sqlite3.connect(database_path) as conn:
            conn.execute(
                "update sessions set session_path = ? where evidence_id = ?",
                (str(outside_session), evidence_id),
            )
            conn.commit()
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 409
    assert session_dir.is_dir()
    assert outside_session.is_dir()
    assert len(listed.json()["sessions"]) == 1


@pytest.mark.asyncio
async def test_batch_delete_preserves_request_order_and_removes_all_indexed_rows(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    first_trace = _unified_session(repo, "20260709_094036")
    second_trace = _unified_session(repo, "20260709_094037")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        sessions = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"]
        by_session_id = {session["session_id"]: session for session in sessions}
        first = by_session_id[first_trace.parent.name]
        second = by_session_id[second_trace.parent.name]
        first_database = _seed_all_evidence_tables(repo, first["evidence_id"], first)
        second_database = _seed_all_evidence_tables(repo, second["evidence_id"], second)
        requested_ids = [second["evidence_id"], first["evidence_id"]]

        response = await client.post(
            "/api/v1/evidence-library/sessions/batch-delete",
            json={"evidence_ids": requested_ids},
        )
        listed = await client.get("/api/v1/evidence-library/sessions")

    assert response.status_code == 200
    payload = response.json()
    assert [item["evidence_id"] for item in payload["results"]] == requested_ids
    assert payload["requested"] == 2
    assert payload["deleted"] == 2
    assert payload["failed"] == 0
    assert not first_trace.parent.exists()
    assert not second_trace.parent.exists()
    assert listed.json()["sessions"] == []
    assert _evidence_row_counts(first_database, first["evidence_id"]) == dict.fromkeys(EVIDENCE_TABLES, 0)
    assert _evidence_row_counts(second_database, second["evidence_id"]) == dict.fromkeys(EVIDENCE_TABLES, 0)


@pytest.mark.asyncio
async def test_batch_delete_returns_partial_result_without_touching_failed_sessions(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    valid_trace = _unified_session(repo, "20260709_094036")
    unsafe_trace = _unified_session(repo, "20260709_094037")
    config_home = tmp_path / "config"
    config_home.mkdir()
    (config_home / "evidence_library.json").write_text(json.dumps({
        "roots": [
            {
                "root_id": "valid-root",
                "label": "Valid root",
                "source": "test",
                "path_glob": str(valid_trace),
                "enabled": True,
                "trusted": True,
            },
            {
                "root_id": "unsafe-root",
                "label": "Unsafe root",
                "source": "test",
                "path_glob": str(unsafe_trace),
                "enabled": True,
                "trusted": False,
            },
        ],
    }))
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        sessions = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"]
        by_session_id = {session["session_id"]: session for session in sessions}
        valid = by_session_id[valid_trace.parent.name]
        unsafe = by_session_id[unsafe_trace.parent.name]
        valid_database = _seed_all_evidence_tables(repo, valid["evidence_id"], valid)
        unsafe_database = _seed_all_evidence_tables(repo, unsafe["evidence_id"], unsafe)
        requested_ids = [valid["evidence_id"], "missing-evidence", unsafe["evidence_id"]]

        response = await client.post(
            "/api/v1/evidence-library/sessions/batch-delete",
            json={"evidence_ids": requested_ids},
        )

    assert response.status_code == 200
    payload = response.json()
    assert [item["evidence_id"] for item in payload["results"]] == requested_ids
    assert [item["status"] for item in payload["results"]] == ["deleted", "failed", "failed"]
    assert payload["requested"] == 3
    assert payload["deleted"] == 1
    assert payload["failed"] == 2
    assert not valid_trace.parent.exists()
    assert unsafe_trace.parent.is_dir()
    assert _evidence_row_counts(valid_database, valid["evidence_id"]) == dict.fromkeys(EVIDENCE_TABLES, 0)
    assert all(count > 0 for count in _evidence_row_counts(unsafe_database, unsafe["evidence_id"]).values())


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "evidence_ids",
    [[], ["same", "same"], [""], [123], [f"id-{index}" for index in range(501)]],
)
async def test_batch_delete_rejects_invalid_evidence_ids_without_changing_sessions(
    tmp_path, monkeypatch, evidence_ids
):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        response = await client.post(
            "/api/v1/evidence-library/sessions/batch-delete",
            json={"evidence_ids": evidence_ids},
        )

    assert response.status_code == 422
    assert trace_dir.parent.is_dir()
    assert _indexed_session_count(repo, evidence_id) == 1


@pytest.mark.asyncio
async def test_delete_rmtree_failure_retains_all_index_rows(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        evidence_id = session["evidence_id"]
        database_path = _seed_all_evidence_tables(repo, evidence_id, session)

        def fail_rmtree(_target: Path) -> None:
            raise OSError("mocked rmtree failure")

        monkeypatch.setattr(service.shutil, "rmtree", fail_rmtree)
        with pytest.raises(OSError, match="mocked rmtree failure"):
            await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)

    assert trace_dir.parent.is_dir()
    assert all(count > 0 for count in _evidence_row_counts(database_path, evidence_id).values())


@pytest.mark.asyncio
async def test_delete_rejects_unified_run_meta_id_mismatch(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        run_meta_path = trace_dir.parent / "run_meta.json"
        run_meta = json.loads(run_meta_path.read_text())
        run_meta["run_id"] = "different-run-id"
        run_meta_path.write_text(json.dumps(run_meta))
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)

    assert response.status_code == 409
    assert trace_dir.parent.is_dir()


@pytest.mark.asyncio
async def test_delete_rejects_indexed_session_id_mismatch_with_directory_run_id(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        session = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        database_path = load_effective_config(repo_root=repo).database_path
        with sqlite3.connect(database_path) as conn:
            conn.execute(
                "update sessions set session_id = ? where evidence_id = ?",
                ("different-indexed-session-id", session["evidence_id"]),
            )
            conn.commit()

        listed = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]
        response = await client.delete("/api/v1/evidence-library/sessions/" + session["evidence_id"])

    assert listed["session_id"] == "different-indexed-session-id"
    assert listed["deletion_allowed"] is False
    assert listed["deletion_target"] is None
    assert "indexed session ID" in listed["deletion_error"]
    assert response.status_code == 409
    assert trace_dir.parent.is_dir()


@pytest.mark.asyncio
async def test_delete_rejects_nonliteral_unified_layout(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    trace_dir = _unified_session(repo)
    relocated_run = repo / "custom-runs" / trace_dir.parent.name
    relocated_run.parent.mkdir()
    trace_dir.parent.rename(relocated_run)
    trace_dir = relocated_run / "trace"
    config_home = tmp_path / "config"
    config_home.mkdir()
    (config_home / "evidence_library.json").write_text(json.dumps({
        "roots": [{
            "root_id": "nonliteral-unified",
            "label": "Nonliteral unified",
            "source": "test",
            "path_glob": str(trace_dir),
            "enabled": True,
            "trusted": True,
        }],
    }))
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        response = await client.delete("/api/v1/evidence-library/sessions/" + evidence_id)

    assert response.status_code == 409
    assert trace_dir.parent.is_dir()


@pytest.mark.asyncio
async def test_overview_png_rejects_missing_evidence(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    repo.mkdir()
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    monkeypatch.setattr(routes, "REPO_ROOT", repo)
    app = FastAPI()
    app.include_router(routes.router)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/api/v1/evidence-library/sessions/missing-evidence/overview-png")
        assert resp.status_code == 404


@pytest.mark.asyncio
async def test_overview_png_rejects_session_symlink_swap_outside_configured_root(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    session_dir = _session(repo / "runs" / "trace_eval")
    app = _app_for(repo, tmp_path, monkeypatch)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        await client.post("/api/v1/evidence-library/rescan", json={"force": True})
        evidence_id = (await client.get("/api/v1/evidence-library/sessions")).json()["sessions"][0]["evidence_id"]
        outside_session = tmp_path / "outside-session"
        session_dir.rename(outside_session)
        session_dir.symlink_to(outside_session, target_is_directory=True)
        response = await client.get(
            f"/api/v1/evidence-library/sessions/{evidence_id}/overview-png"
        )

    assert response.status_code == 404
    assert outside_session.is_dir()


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
