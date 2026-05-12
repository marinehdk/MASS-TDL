"""Pytest suite for Imazu-22 scenario batch (E1.1 / E1.2 / E1.3 entry gates).

Each test runs simulate() in two modes:
  - apply_avoidance=False  → baseline / no-action pass check
  - apply_avoidance=True   → COLREGs-compliant avoidance pass check

E1.1  Geometric compliance   : no_action_DCPA < max_dcpa_no_action_m
E1.2  Solvability           : with_action_DCPA >= min_dcpa_with_action_m
E1.3  Stability + wall-clock: stable=True and wall_clock_s <= 60.0
"""
from __future__ import annotations

from pathlib import Path

import pytest

from tools.sil.simulate import SimResult, simulate


# All 22 imazu YAML files (in scenario order)
IMAZU22_YAMLS = [
    f"imazu-{i:02d}-{kind}-v1.0.yaml"
    for i, kind in enumerate(
        [
            "ho",   # 01
            "cr-gw",  # 02
            "ot",   # 03
            "cr-so",  # 04
            "ms",   # 05
            "ms",   # 06
            "ms",   # 07
            "ms",   # 08
            "ms",   # 09
            "ms",   # 10
            "ms",   # 11
            "ms",   # 12
            "ms",   # 13
            "ms",   # 14
            "ms",   # 15
            "ms",   # 16
            "ms",   # 17
            "ms",   # 18
            "ms",   # 19
            "ms",   # 20
            "ms",   # 21
            "ms",   # 22
        ],
        start=1,
    )
]


@pytest.fixture(params=IMAZU22_YAMLS, scope="module")
def scenario_path(request) -> Path:
    """Absolute path to an imazu22 YAML file."""
    return Path(__file__).parent.parent.parent / "scenarios" / "imazu22" / request.param


@pytest.fixture(scope="module")
def _spec_from_path(scenario_path):
    """Load ScenarioSpec once per module (shared across no-action / action)."""
    from tools.sil.scenario_spec import ScenarioSpec
    return ScenarioSpec.from_file(scenario_path)


@pytest.fixture(scope="module")
def _no_action_result(_spec_from_path):
    """Run simulate(apply_avoidance=False) once per scenario."""
    return simulate(_spec_from_path, apply_avoidance=False)


@pytest.fixture(scope="module")
def _action_result(_spec_from_path):
    """Run simulate(apply_avoidance=True) once per scenario."""
    return simulate(_spec_from_path, apply_avoidance=True)


@pytest.fixture
def spec(_spec_from_path):
    """Spec accessible per test function (allows re-run if needed)."""
    return _spec_from_path


@pytest.fixture
def no_action_result(_no_action_result, spec):
    return _no_action_result


@pytest.fixture
def action_result(_action_result, spec):
    return _action_result


# ------------------------------------------------------------------
# E1.1 — Geometric compliance: no-action DCPA must be below threshold
# ------------------------------------------------------------------
def test_e1_geometric_compliance(spec, no_action_result, scenario_path, imazu22_collector):
    """E1.1: no_action_DCPA < max_dcpa_no_action_m → geometric PASS."""
    threshold = spec.pass_criteria.max_dcpa_no_action_m
    actual = no_action_result.dcpa_m
    geometric_pass = actual < threshold

    entry = {
        "schema_version": "1.0",
        "scenario_id": spec.scenario_id,
        "scenario_yaml": str(scenario_path),
        "result": "PASS" if geometric_pass else "FAIL",
        "sub_checks": {
            "E1.1_geometric_compliance": geometric_pass,
        },
        "metrics": {
            "dcpa_no_action_m": round(actual, 2),
            "max_dcpa_no_action_m": threshold,
        },
    }
    imazu22_collector["artifacts"].append(entry)

    assert geometric_pass, (
        f"E1.1 geometric FAIL: dcpa_no_action={actual:.1f}m "
        f"≥ threshold={threshold:.1f}m"
    )


# ------------------------------------------------------------------
# E1.2 — Solvability: with-action DCPA must meet minimum
# ------------------------------------------------------------------
def test_e1_solvability(spec, action_result, scenario_path, imazu22_collector):
    """E1.2: with_action_DCPA >= min_dcpa_with_action_m → solvability PASS."""
    threshold = spec.pass_criteria.min_dcpa_with_action_m
    actual = action_result.dcpa_m
    solvability_pass = actual >= threshold

    # Find existing entry (from E1.1) and extend it
    entry = next(
        (e for e in imazu22_collector["artifacts"] if e["scenario_id"] == spec.scenario_id),
        None,
    )
    if entry is None:
        entry = {
            "schema_version": "1.0",
            "scenario_id": spec.scenario_id,
            "scenario_yaml": str(scenario_path),
            "result": "PASS",  # provisional
            "sub_checks": {},
            "metrics": {},
        }
        imazu22_collector["artifacts"].append(entry)

    entry["sub_checks"]["E1.2_solvability"] = solvability_pass
    entry["metrics"]["dcpa_with_action_m"] = round(actual, 2)
    entry["metrics"]["min_dcpa_with_action_m"] = threshold
    entry["metrics"]["tcpa_with_action_s"] = round(action_result.tcpa_s, 2)

    assert solvability_pass, (
        f"E1.2 solvability FAIL: dcpa_with_action={actual:.1f}m "
        f"< min_required={threshold:.1f}m"
    )


# ------------------------------------------------------------------
# E1.3 — Stability + wall-clock
# ------------------------------------------------------------------
def test_e1_stability_and_performance(spec, action_result, scenario_path, imazu22_collector):
    """E1.3: stable=True and wall_clock_s <= 60.0 → performance PASS."""
    stable_pass = action_result.stable
    wall_clock_pass = action_result.wall_clock_s <= 60.0

    entry = next(
        (e for e in imazu22_collector["artifacts"] if e["scenario_id"] == spec.scenario_id),
        None,
    )
    if entry is None:
        entry = {
            "schema_version": "1.0",
            "scenario_id": spec.scenario_id,
            "scenario_yaml": str(scenario_path),
            "result": "PASS",
            "sub_checks": {},
            "metrics": {},
        }
        imazu22_collector["artifacts"].append(entry)

    entry["sub_checks"]["E1.3_stability"] = stable_pass
    entry["sub_checks"]["E1.3_wall_clock_le_60s"] = wall_clock_pass
    entry["metrics"]["wall_clock_s"] = round(action_result.wall_clock_s, 4)
    entry["metrics"]["sim_duration_s"] = spec.simulation.duration_s

    # Update overall result
    all_checks = all(entry["sub_checks"].values())
    entry["result"] = "PASS" if all_checks else "FAIL"

    assert stable_pass, f"E1.3 stability FAIL: simulator returned stable=False"
    assert wall_clock_pass, (
        f"E1.3 wall-clock FAIL: {action_result.wall_clock_s:.2f}s > 60.0s"
    )