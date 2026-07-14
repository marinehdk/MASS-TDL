from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any

import pytest

from sil_orchestrator.evidence_library.config import EvidenceRootConfig
from sil_orchestrator.evidence_library.ingest import (
    ingest_session,
    query_decision_frame,
    query_replay,
)
from sil_orchestrator.evidence_library.service import get_overview_png_path, list_sessions, open_initialized
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
            "primary_cpa_m": 450.0,
            "primary_tcpa_s": 127.0,
            "confidence": 0.9,
        },
        {
            "sim_t": 2.0,
            "wall_t": 11.5,
            "topic": "/l3/m2/world_state",
            "primary_target_id": "T02",
            "own_lat": 0.015,
            "own_lon": 0.0,
            "own_heading_deg": 10.0,
            "own_sog_kn": 8.0,
            "primary_cpa_m": 430.0,
            "primary_tcpa_s": 117.0,
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
        {
            "sim_t": 2.5,
            "wall_t": 12.0,
            "topic": "/l3/m6/colregs_constraint",
            "active_rules": [{"rule_id": 14}],
            "primary_role": 1,
            "primary_preferred_direction": "STARBOARD",
            "phase": "ACTIVE",
            "release_predicted": False,
        },
        {
            "sim_t": 3.0,
            "wall_t": 13.0,
            "topic": "/l3/m5/avoidance_plan",
            "solver_status": "VALID",
            "plan_status": "DEGRADED",
            "plan_id": "m5-plan-abc",
            "n_waypoints": 12,
        },
        {"sim_t": 4.0, "wall_t": 14.0, "topic": "/l4/guidance", "execution_state": "DEFERRED", "reason": "avoidance_active"},
        {"sim_t": 5.0, "wall_t": 15.0, "topic": "/l3/asdr/record", "module": "M5", "event_type": "PLAN_READY", "severity": "info", "payload": {"plan_id": "abc"}},
    ]
    with (session / "colreg-rule14-ho.trace_current.jsonl").open("w") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")
    return session


def _write_unified_fixture_run(repo: Path) -> Path:
    run = repo / "runs" / "20260709_094036"
    trace = run / "trace"
    scenario_dir = trace / "colreg-rule15-cs"
    scenario_dir.mkdir(parents=True)
    (run / "run_meta.json").write_text(json.dumps({
        "run_id": "20260709_094036",
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
        "schema_version": 1,
        "session_name": "trace",
        "source": "cli",
        "suite": "clean8",
        "created_at": "2026-07-09T17:42:06+08:00",
        "status": "completed",
        "valid_data": True,
        "scenarios": [{
            "scenario_id": "colreg-rule15-cs",
            "status": "fail",
            "valid_data": True,
            "trace_path": "colreg-rule15-cs/trace_current.jsonl",
            "report_path": "colreg-rule15-cs/report.json",
            "png_path": "colreg-rule15-cs/trajectory_dashboard.png",
            "run_id": "20260709_094036",
        }],
    }))
    (trace / "summary.json").write_text(json.dumps({
        "colreg-rule15-cs": {
            "scenario_id": "colreg-rule15-cs",
            "run_id": "20260709_094036",
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
    (scenario_dir / "report.json").write_text(json.dumps({
        "verdict": {"overall_pass": False},
        "layers": {},
        "kpis": {"min_cpa_m": 4483.3, "min_cpa_nm": 2.42},
    }))
    (scenario_dir / "m5_timeline.json").write_text(json.dumps({"events": []}))
    (scenario_dir / "trajectory_dashboard.png").write_bytes(b"png")
    (scenario_dir / "trajectory.png").write_bytes(b"png2")
    (scenario_dir / "trace_current.jsonl").write_text(
        json.dumps({"sim_t": 0.0, "topic": "/sil/own_ship_state", "lat": 63.0, "lon": 10.0}) + "\n"
    )
    return trace


def _conn() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.row_factory = sqlite3.Row
    initialize_schema(conn)
    return conn


def test_service_lists_result_summary_and_overview_png(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    session = _write_fixture_session(root)
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    conn = open_initialized()
    ingest_session(
        conn,
        EvidenceRootConfig(root_id="primary", label="Primary", source="cli", path_glob=str(root), trusted=True),
        session,
    )
    conn.close()

    sessions = list_sessions(repo_root=repo)

    assert sessions[0]["passed_scenarios"] == 0
    assert sessions[0]["failed_scenarios"] == 1
    assert sessions[0]["overview_png"] == {
        "scenario_id": "colreg-rule14-ho",
        "relative_path": "colreg-rule14-ho_trajectory_dashboard.png",
    }
    assert sessions[0]["overview_pngs"] == [
        {
            "scenario_id": "colreg-rule14-ho",
            "relative_path": "colreg-rule14-ho_trajectory_dashboard.png",
        }
    ]
    assert get_overview_png_path(sessions[0]["evidence_id"], repo_root=repo).name == "colreg-rule14-ho_trajectory_dashboard.png"
    assert (
        get_overview_png_path(sessions[0]["evidence_id"], scenario_id="colreg-rule14-ho", repo_root=repo).name
        == "colreg-rule14-ho_trajectory_dashboard.png"
    )


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
    assert any(row["source_topic"] == "/l3/m2/world_state" and row["vessel_role"] == "ownship" for row in replay["trajectory"])
    assert replay["events"][0]["event_type"] in {"PLAN_READY", "GATE_RESULT"}
    gates = {(gate["gate_id"], gate["source"]): gate["status"] for gate in replay["gates"]}
    assert gates[("G-SEM", "batch_summary.phase_semantics.phase_semantics_ok")] == "FAIL"
    assert gates[("G-SEM", "batch_summary.compliance_verdict")] == "FAIL"
    assert gates[("G-ART", "artifact_consistency")] == "PASS"


def test_ingest_session_supports_unified_run_folder(tmp_path):
    repo = tmp_path / "repo"
    session = _write_unified_fixture_run(repo)
    root = EvidenceRootConfig(
        root_id="primary-unified",
        label="Primary unified",
        source="background_probe",
        path_glob=str(repo / "runs" / "*" / "trace"),
        trusted=True,
    )
    conn = _conn()

    result = ingest_session(conn, root, session)

    assert result.session_id == "20260709_094036"
    replay = query_replay(conn, result.evidence_id, "colreg-rule15-cs")
    assert replay["session"]["session_id"] == "20260709_094036"
    assert replay["session"]["source"] == "cli"
    assert replay["session"]["suite"] == "fast"
    assert replay["session"]["branch"] == "50a1681c1"
    assert replay["session"]["scenario_count"] == 1
    assert replay["scenario"]["overall_pass"] is False
    artifacts = {(artifact["kind"], artifact["relative_path"]) for artifact in replay["artifacts"]}
    assert ("summary", "summary.json") in artifacts
    assert ("m5_timeline", "colreg-rule15-cs/m5_timeline.json") in artifacts
    assert ("trajectory_dashboard_png", "colreg-rule15-cs/trajectory_dashboard.png") in artifacts


def test_ingest_session_derives_target_position_from_m2_relative_measurements(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(
        root_path,
        session_name="20260707_132500_single_colreg-rule15-cs-relative-target",
        world_state_rows=[
            {
                "sim_t": 10.0,
                "wall_t": 20.0,
                "topic": "/l3/m2/world_state",
                "own_lat": 63.0,
                "own_lon": 10.0,
                "own_heading_deg": 5.0,
                "own_sog_kn": 7.0,
                "primary_target_id": 100000001,
                "primary_target_heading_deg": 290.0,
                "primary_target_sog_kn": 9.5,
                "primary_brg_deg": 90.0,
                "primary_rng_m": 1852.0,
                "primary_cpa_m": 300.0,
                "primary_tcpa_s": 120.0,
                "confidence": 0.8,
            },
        ],
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")
    target = next(row for row in replay["trajectory"] if row["vessel_role"] == "target")

    assert target["vessel_id"] == "100000001"
    assert target["lat"] == pytest.approx(63.0, abs=0.001)
    assert target["lon"] == pytest.approx(10.037, abs=0.001)
    assert target["heading_deg"] == 290.0
    assert target["sog_kn"] == 9.5


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
        "preferred_direction": "STARBOARD",
        "phase": "ACTIVE",
        "release_predicted": False,
    }
    assert frame["chain"]["M5"]["facts"] == {
        "solver_status": "VALID",
        "plan_status": "DEGRADED",
        "route_hash": "m5-plan-abc",
        "waypoint_count": 12,
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
        ("TraceEvaluationReport.layers.L2_safety_floor", "UNKNOWN"),
        ("TraceEvaluationReport.layers.L3_dynamic_risk", "UNKNOWN"),
        ("batch_summary.cpa_ok", "FAIL"),
        ("batch_summary.domain_gates.risk_gate_ok", "PASS"),
    }
    assert {(row["source"], row["status"]) for row in sem_rows} == {
        ("TraceEvaluationReport.layers.L4_colregs_compliance", "FAIL"),
        ("batch_summary.phase_semantics.phase_semantics_ok", "FAIL"),
        ("batch_summary.compliance_verdict", "PASS"),
    }
    assert {(row["source"], row["status"]) for row in rel_rows} == {
        ("TraceEvaluationReport.layers.L5_route_recovery", "UNKNOWN"),
        ("TraceEvaluationReport.layers.L6_seamanship", "UNKNOWN"),
        ("batch_summary.returned_to_route", "FAIL"),
        ("batch_summary.route_corridor_ok", "PASS"),
        ("batch_summary.domain_gates.seamanship_gate_ok", "FAIL"),
    }
    assert all(row["conflict_group"] for row in sep_rows + sem_rows + rel_rows)
    conflict_events = [event for event in replay["events"] if event["event_type"] == "gate_conflict"]
    assert {event["module"] for event in conflict_events} == {"GATE"}
    assert {json.loads(event["payload_json"])["gate_id"] for event in conflict_events} == {"G-SEP", "G-SEM", "G-REL"}


def test_ingest_session_records_missing_batch_gate_sources_as_unknown(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(
        root_path,
        session_name="20260707_135000_single_colreg-rule14-ho-missing-batch-fields",
        batch_rows=[
            {
                "scenario": "colreg-rule14-ho",
                "overall_pass": False,
                "cpa_ok": False,
                "stability_pass": True,
                "returned_to_route": False,
                "compliance_verdict": "PASS",
                "phase_semantics": {"phase_semantics_ok": False},
                "domain_gates": {"seamanship_gate_ok": False},
            }
        ],
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    sep_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEP"]
    rel_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-REL"]

    sep_by_source = {row["source"]: row for row in sep_rows}
    rel_by_source = {row["source"]: row for row in rel_rows}

    assert sep_by_source["batch_summary.cpa_ok"]["status"] == "FAIL"
    assert json.loads(sep_by_source["batch_summary.cpa_ok"]["payload_json"]) == {
        "field": "batch_summary.cpa_ok",
        "value": False,
    }
    assert sep_by_source["batch_summary.domain_gates.risk_gate_ok"]["status"] == "UNKNOWN"
    assert json.loads(sep_by_source["batch_summary.domain_gates.risk_gate_ok"]["payload_json"]) == {
        "field": "batch_summary.domain_gates.risk_gate_ok",
        "value": None,
    }

    assert rel_by_source["batch_summary.returned_to_route"]["status"] == "FAIL"
    assert json.loads(rel_by_source["batch_summary.returned_to_route"]["payload_json"]) == {
        "field": "batch_summary.returned_to_route",
        "value": False,
    }
    assert rel_by_source["batch_summary.route_corridor_ok"]["status"] == "UNKNOWN"
    assert json.loads(rel_by_source["batch_summary.route_corridor_ok"]["payload_json"]) == {
        "field": "batch_summary.route_corridor_ok",
        "value": None,
    }
    assert rel_by_source["batch_summary.domain_gates.seamanship_gate_ok"]["status"] == "FAIL"
    assert json.loads(rel_by_source["batch_summary.domain_gates.seamanship_gate_ok"]["payload_json"]) == {
        "field": "batch_summary.domain_gates.seamanship_gate_ok",
        "value": False,
    }


def test_ingest_session_materializes_missing_trace_report_layer_as_unknown(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    scn_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SCN"]
    assert len(scn_rows) == 1
    assert scn_rows[0]["source"] == "TraceEvaluationReport.layers.L1_scenario_validity"
    assert scn_rows[0]["status"] == "UNKNOWN"
    assert json.loads(scn_rows[0]["payload_json"]) == {
        "layer_id": "L1_scenario_validity",
        "missing": True,
    }


def test_ingest_session_materializes_missing_batch_summary_sources_as_unknown_without_summary_file(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path)
    (session / "batch_summary.json").unlink()
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    sep_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEP"]
    sem_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEM"]
    rel_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-REL"]
    act_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-ACT"]

    assert {(row["source"], row["status"]) for row in sep_rows} == {
        ("TraceEvaluationReport.layers.L2_safety_floor", "UNKNOWN"),
        ("TraceEvaluationReport.layers.L3_dynamic_risk", "UNKNOWN"),
        ("batch_summary.cpa_ok", "UNKNOWN"),
        ("batch_summary.domain_gates.risk_gate_ok", "UNKNOWN"),
    }
    assert {(row["source"], row["status"]) for row in sem_rows} == {
        ("TraceEvaluationReport.layers.L4_colregs_compliance", "FAIL"),
        ("batch_summary.phase_semantics.phase_semantics_ok", "UNKNOWN"),
        ("batch_summary.compliance_verdict", "UNKNOWN"),
    }
    assert {(row["source"], row["status"]) for row in rel_rows} == {
        ("TraceEvaluationReport.layers.L5_route_recovery", "UNKNOWN"),
        ("TraceEvaluationReport.layers.L6_seamanship", "UNKNOWN"),
        ("batch_summary.returned_to_route", "UNKNOWN"),
        ("batch_summary.route_corridor_ok", "UNKNOWN"),
        ("batch_summary.domain_gates.seamanship_gate_ok", "UNKNOWN"),
    }
    assert {(row["source"], row["status"]) for row in act_rows} == {
        ("TraceEvaluationReport.layers.L7_stability", "PASS"),
        ("batch_summary.stability_pass", "UNKNOWN"),
    }


def test_ingest_session_does_not_flag_unknown_plus_pass_as_conflict(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path, session_name="20260707_135500_single_colreg-rule14-ho-pass-plus-unknown")
    (session / "colreg-rule14-ho.json").write_text(
        json.dumps(
            {
                "verdict": {"overall_pass": True},
                "layers": {
                    "L4_colregs_compliance": {"passed": True, "reason": "phase semantic passed"},
                    "L7_stability": {"passed": True},
                },
                "kpis": {"min_cpa_m": 450.0, "min_cpa_nm": 0.243},
            }
        )
    )
    (session / "batch_summary.json").write_text(
        json.dumps(
            {
                "results": [
                    {
                        "scenario": "colreg-rule14-ho",
                        "overall_pass": True,
                        "cpa_ok": True,
                        "stability_pass": True,
                        "returned_to_route": True,
                        "route_corridor_ok": True,
                        "compliance_verdict": "PASS",
                        "phase_semantics": {"phase_semantics_ok": True},
                        "domain_gates": {"seamanship_gate_ok": True},
                    }
                ]
            }
        )
    )
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    result = ingest_session(conn, root, session)
    replay = query_replay(conn, result.evidence_id, "colreg-rule14-ho")

    sep_rows = [gate for gate in replay["gates"] if gate["gate_id"] == "G-SEP"]
    assert {(row["source"], row["status"]) for row in sep_rows} == {
        ("TraceEvaluationReport.layers.L2_safety_floor", "UNKNOWN"),
        ("TraceEvaluationReport.layers.L3_dynamic_risk", "UNKNOWN"),
        ("batch_summary.cpa_ok", "PASS"),
        ("batch_summary.domain_gates.risk_gate_ok", "UNKNOWN"),
    }
    assert all(row["conflict_group"] is None for row in sep_rows)
    assert not [event for event in replay["events"] if event["event_type"] == "gate_conflict"]


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


def test_ingest_rejects_manifest_artifact_path_escape(tmp_path):
    root_path = tmp_path / "runs" / "trace_eval"
    session = _write_fixture_session(root_path, session_name="20260707_134000_single_path_escape")
    outside = tmp_path / "outside.json"
    outside.write_text(json.dumps({"verdict": {"overall_pass": True}}))
    manifest_path = session / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["scenarios"][0]["report_path"] = "../../outside.json"
    manifest_path.write_text(json.dumps(manifest))
    root = EvidenceRootConfig(root_id="primary", label="Primary", source="background_probe", path_glob=str(root_path), trusted=True)
    conn = _conn()

    with pytest.raises(ValueError, match="escapes session"):
        ingest_session(conn, root, session)
