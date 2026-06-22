from __future__ import annotations

import json
from pathlib import Path

from tools.sil.trajectory_dashboard import generate_trajectory_dashboard


def _write_dashboard_trace(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        for i in range(40):
            sim_t = float(i)
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/own_ship_state",
                "lat": 63.44 + i * 0.00001,
                "lon": 10.38 + min(i, 20) * 0.00001,
                "heading_deg": 360.0 - min(i, 10) * 0.2,
                "sog_kn": 8.0,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m4/behavior_plan",
                "behavior": 1 if 10 <= i <= 25 else 0,
                "avoidance_active": 10 <= i <= 25,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m6/colregs_constraint",
                "conflict_detected": 10 <= i <= 25,
                "phase": "SOUND_WARNING" if 10 <= i <= 25 else "PRESERVE_COURSE",
                "primary_preferred_direction": "STARBOARD" if 10 <= i <= 25 else "HOLD",
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/l3/m5/avoidance_plan",
                "solver_status": "VALID" if 10 <= i <= 25 else "EMPTY",
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/scoring",
                "total": 0.95,
            }) + "\n")
            f.write(json.dumps({
                "sim_t": sim_t,
                "wall_t": 1000.0 + i,
                "topic": "/sil/actuator_cmd",
                "rudder_deg": 5.0,
            }) + "\n")


def test_generate_dashboard_png_from_jsonl_and_report(tmp_path: Path):
    trace = tmp_path / "colreg-rule14-ho.trace_current.jsonl"
    report = tmp_path / "colreg-rule14-ho.json"
    output = tmp_path / "colreg-rule14-ho_trajectory_dashboard.png"
    _write_dashboard_trace(trace)
    report.write_text(json.dumps({
        "verdict": {
            "overall_pass": True,
            "safety_pass": True,
            "mission_pass": True,
            "colregs_pass": True,
            "stability_pass": True,
        },
        "threshold_provenance": {
            "threshold_m": 180.0,
            "threshold_formula": "4.0L",
        },
        "layers": {
            "L1_scenario_validity": {"status": "UNKNOWN"},
            "L2_safety_floor": {"status": "PASS"},
        },
        "first_failure": None,
    }))

    result = generate_trajectory_dashboard(
        trace_jsonl=trace,
        output_png=output,
        scenario_id="colreg-rule14-ho",
        session_name="20260622_153012_single_colreg-rule14-ho",
        report_json=report,
    )

    assert result == output
    assert output.exists()
    assert output.stat().st_size > 20_000


def test_generate_dashboard_png_without_report(tmp_path: Path):
    trace = tmp_path / "colreg-rule14-ho.trace_current.jsonl"
    output = tmp_path / "colreg-rule14-ho_trajectory_dashboard.png"
    _write_dashboard_trace(trace)

    result = generate_trajectory_dashboard(
        trace_jsonl=trace,
        output_png=output,
        scenario_id="colreg-rule14-ho",
        session_name="20260622_153012_single_colreg-rule14-ho",
        report_json=None,
    )

    assert result.exists()
    assert output.stat().st_size > 20_000

