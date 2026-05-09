"""Tests for simulate() function (D1.3b T5, T6 acceptance criteria)."""
import math
import sys
from pathlib import Path

import pytest
import yaml

sys.path.insert(0, str(Path(__file__).parent))
from scenario_spec import ScenarioSpec
from simulate import SimResult, simulate


def load_spec(filename: str) -> ScenarioSpec:
    p = Path(__file__).parent.parent.parent / "scenarios" / "colregs" / filename
    return ScenarioSpec.model_validate(yaml.safe_load(p.read_text()))


def test_ho001_no_action_dcpa_small():
    spec = load_spec("colreg-rule14-ho-001-seed42-v1.0.yaml")
    result = simulate(spec, apply_avoidance=False)
    assert result.stable, "simulation should be stable"
    assert result.dcpa_m < spec.pass_criteria.max_dcpa_no_action_m, \
        f"DCPA without action should be < {spec.pass_criteria.max_dcpa_no_action_m} m, got {result.dcpa_m}"


def test_ho001_with_action_dcpa_large():
    spec = load_spec("colreg-rule14-ho-001-seed42-v1.0.yaml")
    result = simulate(spec, apply_avoidance=True)
    assert result.stable
    assert result.dcpa_m >= spec.pass_criteria.min_dcpa_with_action_m, \
        f"DCPA with action should be >= {spec.pass_criteria.min_dcpa_with_action_m} m, got {result.dcpa_m}"


def test_wall_clock_under_60s():
    spec = load_spec("colreg-rule14-ho-001-seed42-v1.0.yaml")
    result = simulate(spec, apply_avoidance=True)
    assert result.wall_clock_s < 60.0, f"wall_clock_s={result.wall_clock_s} exceeds 60 s limit"


def test_ot001_no_action_dcpa_small():
    spec = load_spec("colreg-rule13-ot-001-seed20-v1.0.yaml")
    result = simulate(spec, apply_avoidance=False)
    assert result.stable
    assert result.dcpa_m < spec.pass_criteria.max_dcpa_no_action_m, \
        f"DCPA without action should be < {spec.pass_criteria.max_dcpa_no_action_m} m for OT-001, got {result.dcpa_m}"


def test_no_nan_in_trajectory():
    spec = load_spec("colreg-rule14-ho-001-seed42-v1.0.yaml")
    result = simulate(spec, apply_avoidance=True)
    assert result.stable
    for pt in result.own_trajectory_sampled:
        assert math.isfinite(pt[0]) and math.isfinite(pt[1]), f"NaN in trajectory: {pt}"


def test_json_output_has_required_fields():
    spec = load_spec("colreg-rule14-ho-001-seed42-v1.0.yaml")
    result = simulate(spec, apply_avoidance=True)
    d = result.to_json_dict(spec, no_action_dcpa_m=5.0, yaml_path="test.yaml")
    assert "result" in d
    assert "sub_checks" in d
    assert "metrics" in d
    assert "performance" in d
    # wall_clock_s is rounded to 4 decimals in JSON, so compare rounded values
    assert abs(d["performance"]["wall_clock_s"] - result.wall_clock_s) < 0.001
