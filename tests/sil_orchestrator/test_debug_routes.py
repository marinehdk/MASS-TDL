"""Unit tests for /api/v1/debug/* endpoints.

Uses a temp JSONL fixture — no ROS2 or running containers needed.
"""
from __future__ import annotations

import json
import importlib
from pathlib import Path

import pytest
from fastapi.testclient import TestClient


# ── Fixture helpers ─────────────────────────────────────────

SAMPLE_RECORDS = [
    {"sim_t": 10.0, "topic": "/l3/m3/mission_goal", "fsm_state": 3, "task_validity": 0,
     "target_wp_lat": 0.0, "target_wp_lon": 0.0},
    {"sim_t": 20.0, "topic": "/l3/m4/behavior_plan", "behavior": 0,
     "heading_min_deg": 350.0, "heading_max_deg": 10.0, "avoidance_active": False,
     "target_heading_deg": None},
    {"sim_t": 250.0, "topic": "/l3/m4/behavior_plan", "behavior": 1,
     "heading_min_deg": 5.0, "heading_max_deg": 45.0, "avoidance_active": True,
     "target_heading_deg": 33.0},
    {"sim_t": 250.5, "topic": "/l3/m5/avoidance_plan", "n_waypoints": 3,
     "solver_status": "VALID", "wp0_turn_radius_m": 250.0, "wp0_target_speed_kn": 8.0},
    {"sim_t": 300.0, "topic": "/sil/own_ship_state", "heading_deg": 33.2,
     "sog_kn": 8.1, "lat": 60.12, "lon": 5.01, "rot_deg_s": 0.5},
    {"sim_t": 520.0, "topic": "/l3/m4/behavior_plan", "behavior": 0,
     "heading_min_deg": 350.0, "heading_max_deg": 10.0, "avoidance_active": False,
     "target_heading_deg": None},
    {"sim_t": 600.0, "topic": "/l3/m3/mission_goal", "fsm_state": 3, "task_validity": 1,
     "target_wp_lat": 60.15, "target_wp_lon": 5.02},
]


@pytest.fixture
def trace_file(tmp_path):
    """Write SAMPLE_RECORDS to a temp JSONL."""
    p = tmp_path / "trace_current.jsonl"
    p.write_text("\n".join(json.dumps(r) for r in SAMPLE_RECORDS) + "\n")
    return p


@pytest.fixture
def client(trace_file, monkeypatch):
    from fastapi import FastAPI
    import sil_orchestrator.routers.debug_routes as dr
    importlib.reload(dr)
    monkeypatch.setattr(dr, "_TRACE_FILE", trace_file)
    monkeypatch_app = FastAPI()
    monkeypatch_app.include_router(dr.router)
    return TestClient(monkeypatch_app)


# ── Tests ───────────────────────────────────────────────────

def test_trace_returns_records(client):
    r = client.get("/api/v1/debug/trace?last_n=10")
    assert r.status_code == 200
    body = r.json()
    assert body["count"] == len(SAMPLE_RECORDS)
    assert body["records"][0]["topic"] == "/l3/m3/mission_goal"


def test_trace_last_n_limits(client):
    r = client.get("/api/v1/debug/trace?last_n=2")
    assert r.status_code == 200
    body = r.json()
    assert body["count"] == 2
    # Last 2 records should be the last items
    assert body["records"][-1]["sim_t"] == 600.0


def test_snapshot_returns_latest_per_topic(client):
    r = client.get("/api/v1/debug/snapshot")
    assert r.status_code == 200
    topics = r.json()["topics"]
    # Latest M4 behavior_plan should be the TRANSIT one at sim_t=520
    m4 = topics.get("/l3/m4/behavior_plan", {})
    assert m4.get("behavior") == 0
    assert m4.get("sim_t") == 520.0
    # Latest M3 should have task_validity=1 (sim_t=600)
    m3 = topics.get("/l3/m3/mission_goal", {})
    assert m3.get("task_validity") == 1


def test_summary_m4_phase_timeline(client):
    r = client.get("/api/v1/debug/summary")
    assert r.status_code == 200
    phases = r.json()["m4_phase_timeline"]
    phase_names = [p["phase"] for p in phases]
    assert "TRANSIT" in phase_names
    assert "BEHAVIOR_1" in phase_names
    # Should end in TRANSIT
    assert phase_names[-1] == "TRANSIT"


def test_summary_m5_solver_stats(client):
    r = client.get("/api/v1/debug/summary")
    assert r.status_code == 200
    stats = r.json()["m5_solver_stats"]
    assert stats["VALID"] == 1
    assert stats["convergence_rate_pct"] == 100.0


def test_summary_m3_task_validity_timeline(client):
    r = client.get("/api/v1/debug/summary")
    assert r.status_code == 200
    timeline = r.json()["m3_task_validity_timeline"]
    # First entry: task_validity=0 starting at sim_t=10
    assert timeline[0]["task_validity"] == 0
    assert timeline[0]["from_sim_t"] == 10.0
    # Second entry: task_validity=1 starting at sim_t=600
    assert any(e["task_validity"] == 1 for e in timeline)


def test_summary_max_heading(client):
    r = client.get("/api/v1/debug/summary")
    assert r.status_code == 200
    body = r.json()
    assert body["max_heading_deg"] == 33.2


def test_trace_empty_when_no_file(monkeypatch):
    import sil_orchestrator.routers.debug_routes as dr
    importlib.reload(dr)
    monkeypatch.setattr(dr, "_TRACE_FILE", Path("/nonexistent/file.jsonl"))
    from fastapi import FastAPI
    app = FastAPI()
    app.include_router(dr.router)
    c = TestClient(app)
    r = c.get("/api/v1/debug/trace")
    assert r.status_code == 200
    assert r.json()["count"] == 0
