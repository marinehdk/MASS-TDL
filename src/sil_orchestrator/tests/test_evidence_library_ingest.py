from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any

from sil_orchestrator.evidence_library.config import EvidenceRootConfig
from sil_orchestrator.evidence_library.ingest import (
    ingest_session,
    query_decision_frame,
    query_replay,
)
from sil_orchestrator.evidence_library.store import initialize_schema


def _write_fixture_session(
    root: Path,
    session_name: str = "20260707_132000_single_colreg-rule14-ho",
    world_state_rows: list[dict[str, Any]] | None = None,
    batch_rows: list[dict[str, Any]] | None = None,
) -> Path:
    session = root / session_name
    session.mkdir(parents=True)
    world_state_rows = world_state_rows or [
        {
            "sim_t": 1.0,
            "wall_t": 11.0,
            "topic": "/l3/m2/world_state",
            "primary_target_id": "T01",
            "target_id": "T01",
            "target_lat": 0.01,
            "target_lon": 0.0,
            "cpa_m": 450.0,
            "tcpa_s": 127.0,
            "confidence": 0.9,
        },
        {
            "sim_t": 2.0,
            "wall_t": 11.5,
            "topic": "/l3/m2/world_state",
            "primary_target_id": "T02",
            "target_lat": 0.015,
            "target_lon": 0.0,
            "cpa_m": 430.0,
            "tcpa_s": 117.0,
            "confidence": 0.85,
        },
    ]
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
        "results": batch_rows
        or [
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
        *world_state_rows,
        {"sim_t": 2.5, "wall_t": 12.0, "topic": "/l3/m6/colregs", "rule": "Rule14", "role": "give_way", "preferred_direction": "starboard", "phase": "active", "release_predicted": False},
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
    gates = {(gate["gate_id"], gate["source"]): gate["status"] for gate in replay["gates"]}
    assert gates[("G-SEM", "batch_summary.phase_semantics.phase_semantics_ok")] == "FAIL"
    assert gates[("G-SEM", "batch_summary.compliance_verdict")] == "FAIL"
    assert gates[("G-ART", "artifact_consistency")] == "PASS"


def test_decision_frame_returns_time_aligned_module_facts(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 3.5)

    assert frame["chain"]["M2"]["facts"] == {
        "primary_target_id": "T02",
        "cpa_m": 430.0,
        "tcpa_s": 117.0,
        "confidence": 0.85,
    }
    assert frame["chain"]["M6"]["facts"] == {
        "rule": "Rule14",
        "role": "give_way",
        "preferred_direction": "starboard",
        "phase": "active",
        "release_predicted": False,
    }
    assert frame["chain"]["M5"]["facts"] == {
        "solver_status": "VALID",
        "plan_status": "NORMAL",
        "route_hash": "abc",
        "waypoint_count": 4,
    }
    assert frame["chain"]["L4"]["facts"] == {
        "execution_state": "UNKNOWN",
        "accepted": "UNKNOWN",
        "rejected": "UNKNOWN",
        "degraded": "UNKNOWN",
        "reason": "UNKNOWN",
    }
    assert frame["chain"]["M4"]["facts"] == {
        "behavior": "UNKNOWN",
        "avoidance_active": "UNKNOWN",
    }
    assert frame["chain"]["M7"]["facts"] == {
        "alert_type": "UNKNOWN",
        "severity": "UNKNOWN",
        "recommended_mrm": "UNKNOWN",
    }
    assert frame["gates"][0]["temporal_scope"] in {"final_run_verdict", "artifact_consistency"}


def test_decision_frame_clears_missing_snapshot_fields(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(
        root_path,
        session_name="20260707_133000_single_colreg-rule14-ho-missing-m2-field",
        world_state_rows=[
            {
                "sim_t": 1.0,
                "wall_t": 11.0,
                "topic": "/l3/m2/world_state",
                "primary_target_id": "T01",
                "target_id": "T01",
                "target_lat": 0.01,
                "target_lon": 0.0,
                "cpa_m": 450.0,
                "tcpa_s": 127.0,
                "confidence": 0.9,
            },
            {
                "sim_t": 2.0,
                "wall_t": 11.5,
                "topic": "/l3/m2/world_state",
                "target_lat": 0.015,
                "target_lon": 0.0,
                "cpa_m": 430.0,
                "tcpa_s": 117.0,
                "confidence": 0.85,
            },
        ],
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 2.5)

    assert frame["chain"]["M2"]["facts"] == {
        "primary_target_id": "UNKNOWN",
        "cpa_m": 430.0,
        "tcpa_s": 117.0,
        "confidence": 0.85,
    }


def test_decision_frame_prefers_latest_segment_at_transition_and_final_end(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    transition_frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 2.0)
    final_frame = query_decision_frame(conn, result.evidence_id, "colreg-rule14-ho", 5.0)

    assert transition_frame["chain"]["M2"]["facts"]["primary_target_id"] == "T02"
    assert final_frame["chain"]["M5"]["facts"]["solver_status"] == "VALID"
    assert final_frame["chain"]["M2"]["facts"]["primary_target_id"] == "T02"


def test_ingest_session_preserves_conflicting_batch_gate_sources(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(
        root_path,
        session_name="20260707_134000_single_colreg-rule14-ho-conflict",
        batch_rows=[
            {
                "scenario": "colreg-rule14-ho",
                "overall_pass": False,
                "cpa_ok": False,
                "stability_pass": True,
                "returned_to_route": False,
                "route_corridor_ok": True,
                "compliance_verdict": "PASS",
                "phase_semantics": {"phase_semantics_ok": False},
                "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": False},
            }
        ],
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    sep_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEP"]
    sem_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEM"]
    rel_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-REL"]

    assert {(row["source"], row["status"]) for row in sep_rows} == {
        ("batch_summary.cpa_ok", "FAIL"),
        ("batch_summary.domain_gates.risk_gate_ok", "PASS"),
    }
    assert {(row["source"], row["status"]) for row in sem_rows} == {
        ("TraceEvaluationReport.layers.L4_colregs_compliance", "FAIL"),
        ("batch_summary.phase_semantics.phase_semantics_ok", "FAIL"),
        ("batch_summary.compliance_verdict", "PASS"),
    }
    assert {(row["source"], row["status"]) for row in rel_rows} == {
        ("batch_summary.returned_to_route", "FAIL"),
        ("batch_summary.route_corridor_ok", "PASS"),
        ("batch_summary.domain_gates.seamanship_gate_ok", "FAIL"),
    }
    assert all(row["conflict_group"] for row in sep_rows + sem_rows + rel_rows)
    conflict_events = [event for event in replay["events"] if event["event_type"] == "gate_conflict"]
    assert {event["module"] for event in conflict_events} == {"GATE"}
    assert {json.loads(event["payload_json"])["gate_id"] for event in conflict_events} == {"G-SEP", "G-SEM", "G-REL"}


def test_trajectory_rows_use_unknown_when_target_identity_missing(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(
        root_path,
        session_name="20260707_133000_single_colreg-rule14-ho-unknown",
        world_state_rows=[
            {
                "sim_t": 1.0,
                "wall_t": 11.0,
                "topic": "/l3/m2/world_state",
                "target_lat": 0.01,
                "target_lon": 0.0,
                "cpa_m": 450.0,
                "tcpa_s": 127.0,
                "confidence": 0.9,
            }
        ],
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()
    result = ingest_session(conn, root, session)

    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    assert any(row["vessel_id"] == "UNKNOWN" and row["vessel_role"] == "target" for row in replay["trajectory"])
