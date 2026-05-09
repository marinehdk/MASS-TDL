"""Tests for batch_runner and coverage_reporter (D1.3b T7, T9 AC)."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))


def test_generate_report_with_fake_results(tmp_path):
    # Write a fake JSON result
    result = {
        "schema_version": "1.0",
        "scenario_id": "colreg-rule14-ho-001-v1.0",
        "scenario_yaml": "scenarios/colregs/colreg-rule14-ho-001-seed42-v1.0.yaml",
        "run_timestamp": "2026-05-09T12:00:00Z",
        "result": "PASS",
        "sub_checks": {
            "geometric_compliance": True,
            "bearing_sector": True,
            "solvability": True,
            "stability": True,
            "wall_clock_le_60s": True,
        },
        "metrics": {
            "dcpa_no_action_m": 5.0,
            "dcpa_with_action_m": 1200.0,
            "tcpa_no_action_s": 327.0,
        },
        "performance": {"wall_clock_s": 0.31, "n_steps": 30000, "sim_duration_s": 600.0},
        "disturbance_recorded": {"wind_kn": 0.0, "current_kn": 0.0, "vis_m": 10000.0},
        "trajectory_points": 300,
    }
    json_file = tmp_path / "ho-001.json"
    json_file.write_text(json.dumps(result))

    import coverage_reporter
    out_path = tmp_path / "report.html"
    coverage_reporter.generate_report(tmp_path, out_path)

    assert out_path.exists()
    html = out_path.read_text()
    assert "Rule 14" in html
    assert "HO-001" in html or "ho-001" in html.lower()
    assert "PASS" in html
