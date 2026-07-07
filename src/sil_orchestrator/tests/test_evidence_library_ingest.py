from __future__ import annotations

import json
import sqlite3
from pathlib import Path

from sil_orchestrator.evidence_library.config import EvidenceRootConfig
from sil_orchestrator.evidence_library.ingest import (
    ingest_session,
    query_decision_frame,
    query_replay,
)
from sil_orchestrator.evidence_library.store import initialize_schema


def _write_fixture_session(root: Path, session_name: str = "20260707_132000_single_colreg-rule14-ho") -> Path:
    session = root / session_name
    session.mkdir(parents=True)
    manifest = {
        "session_name": session_name,
        "source": "frontend",
        "suite": "single",
        "created_at": "2026-07-07T13:20:00Z",
        "ended_at": "2026-07-07T13:25:00Z",
        "status": "completed",
        "valid_data": True,
        "scenarios": [
            {
                "scenario_id": "colreg-rule14-ho",
                "run_id": "run-test",
                "status": "completed",
                "valid_data": True,
                "trace_path": "colreg-rule14-ho.trace_current.jsonl",
                "report_path": "colreg-rule14-ho.json",
                "png_path": "colreg-rule14-ho_trajectory_dashboard.png",
            }
        ],
    }
    (session / "manifest.json").write_text(json.dumps(manifest))
    report = {
        "verdict": {"overall_pass": False},
        "layers": {
            "L4_colregs_compliance": {"passed": False, "reason": "phase semantic failed"},
            "L7_stability": {"passed": True},
        },
        "kpis": {"min_cpa_m": 450.0, "min_cpa_nm": 0.243},
    }
    (session / "colreg-rule14-ho.json").write_text(json.dumps(report))
    batch = {
        "results": [
            {
                "scenario": "colreg-rule14-ho",
                "overall_pass": False,
                "cpa_ok": False,
                "stability_pass": True,
                "returned_to_route": False,
                "route_corridor_ok": True,
                "compliance_verdict": "FAIL",
                "phase_semantics": {"phase_semantics_ok": False},
                "domain_gates": {"risk_gate_ok": False, "seamanship_gate_ok": True},
            }
        ]
    }
    (session / "batch_summary.json").write_text(json.dumps(batch))
    (session / "colreg-rule14-ho.artifact_consistency.json").write_text(json.dumps({"g_art_ok": True}))
    (session / "colreg-rule14-ho_trajectory_dashboard.png").write_bytes(b"png")
    rows = [
        {"sim_t": 0.0, "wall_t": 10.0, "topic": "/sil/own_ship_state", "lat": 0.0, "lon": 0.0, "heading_deg": 0.0, "sog_kn": 8.0},
        {"sim_t": 1.0, "wall_t": 11.0, "topic": "/l3/m2/world_state", "primary_target_id": "T01", "target_id": "T01", "target_lat": 0.01, "target_lon": 0.0, "cpa_m": 450.0, "tcpa_s": 127.0, "confidence": 0.9},
        {"sim_t": 2.0, "wall_t": 12.0, "topic": "/l3/m6/colregs", "rule": "Rule14", "role": "give_way", "preferred_direction": "starboard", "phase": "active", "release_predicted": False},
        {"sim_t": 3.0, "wall_t": 13.0, "topic": "/l3/m5/trajectory", "solver_status": "VALID", "plan_status": "NORMAL", "route_hash": "abc", "waypoint_count": 4},
        {"sim_t": 4.0, "wall_t": 14.0, "topic": "/l4/guidance", "execution_state": "DEFERRED", "reason": "avoidance_active"},
        {"sim_t": 5.0, "wall_t": 15.0, "topic": "/l3/asdr/record", "module": "M5", "event_type": "PLAN_READY", "severity": "info", "payload": {"plan_id": "abc"}},
    ]
    with (session / "colreg-rule14-ho.trace_current.jsonl").open("w") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")
    return session


def _conn() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    initialize_schema(conn)
    return conn


def test_ingest_session_builds_replay_and_gate_rows(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(
        root_id="primary",
        label="Primary",
        source="background_probe",
        path_glob=str(root_path),
        trusted=True,
    )
    conn = _conn()

    result = ingest_session(conn, root, session)

    assert result.session_id == session.name
    assert result.scenario_count == 1
    assert result.trajectory_count >= 2
    assert result.event_count >= 1
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")
    assert replay["session"]["evidence_id"] == result.evidence_id
    assert replay["scenario"]["scenario_id"] == "colreg-rule14-ho"
    assert replay["scenario"]["overall_pass"] is False
    assert replay["duration_s"] == 5.0
    assert replay["trajectory"][0]["vessel_id"] == "OWN"
    assert replay["events"][0]["event_type"] in {"PLAN_READY", "GATE_RESULT"}
    gates = {gate["gate_id"]: gate["status"] for gate in replay["gates"]}
    assert gates["G-SEM"] == "FAIL"
    assert gates["G-ART"] == "PASS"


def test_decision_frame_returns_time_aligned_module_facts(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 3.5)

    assert frame["chain"]["M2"]["facts"]["primary_target_id"] == "T01"
    assert frame["chain"]["M6"]["facts"]["rule"] == "Rule14"
    assert frame["chain"]["M5"]["facts"]["solver_status"] == "VALID"
    assert frame["chain"]["L4"]["facts"] == {}
    assert frame["gates"][0]["temporal_scope"] in {"final_run_verdict", "artifact_consistency"}
