import importlib.util
from pathlib import Path

import pytest
import yaml


def _load_runner():
    path = Path(__file__).resolve().parents[2] / "scripts" / "run_6_scenarios.py"
    spec = importlib.util.spec_from_file_location("run_6_scenarios", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_overall_gate_requires_returned_to_route():
    runner = _load_runner()
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=False,
    ) is False


def test_overall_gate_passes_when_all_required_signals_pass():
    runner = _load_runner()
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
    ) is True


def test_overall_gate_requires_route_corridor_pass():
    runner = _load_runner()
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
        route_corridor_ok=False,
    ) is False


def test_route_return_uses_m4_behavior_when_bridge_active_is_stale():
    runner = _load_runner()
    records = [
        {
            "topic": "/l3/m4/behavior_plan",
            "sim_t": 10.0,
            "behavior": 1,
            "avoidance_active": True,
        },
        {
            "topic": "/l3/m4/behavior_plan",
            "sim_t": 20.0,
            "behavior": 0,
            "avoidance_active": True,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 21.0,
            "lat": 63.4401,
            "lon": 10.38,
            "heading_deg": 0.0,
        },
    ]

    status = runner.compute_route_return_status(
        records,
        lat0=63.44,
        lon0=10.38,
        init_lat=63.44,
        init_lon=10.38,
        init_hdg=0.0,
    )

    assert status["released_after_avoidance"] is True
    assert status["returned_to_route"] is True


def test_route_status_tracks_max_xte_and_corridor_violation():
    runner = _load_runner()
    records = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 10.0, "behavior": 1},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 40.0, "behavior": 0},
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 12.0,
            "lat": 0.0,
            "lon": 1200.0 / 111120.0,
            "heading_deg": 0.0,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 41.0,
            "lat": 200.0 / 111120.0,
            "lon": 25.0 / 111120.0,
            "heading_deg": 0.0,
        },
    ]

    status = runner.compute_route_return_status(
        records,
        lat0=0.0,
        lon0=0.0,
        init_lat=0.0,
        init_lon=0.0,
        init_hdg=0.0,
        route_corridor_half_width_m=1000.0,
        route_corridor_pass_limit_m=900.0,
    )

    assert status["returned_to_route"] is True
    assert status["max_route_xte_m"] == pytest.approx(1200.0, abs=1.0)
    assert status["route_corridor_violation"] is True
    assert status["route_corridor_ok"] is False


def test_overtake_status_requires_ownship_longitudinally_ahead():
    runner = _load_runner()
    records = [
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 0.0,
            "lat": 0.0,
            "lon": 0.0,
            "heading_deg": 0.0,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 100.0,
            "lat": 1200.0 / 111120.0,
            "lon": 0.0,
            "heading_deg": 0.0,
        },
    ]
    targets = [{"lat0": 900.0 / 111120.0, "lon0": 0.0, "cog": 0.0, "sog_kn": 0.0}]

    status = runner.compute_overtake_status(
        records,
        targets,
        lat0=0.0,
        lon0=0.0,
        required=True,
        along_margin_m=0.0,
    )

    assert status["overtake_required"] is True
    assert status["overtake_completed"] is True
    assert status["overtake_first_time_s"] == pytest.approx(100.0)
    assert status["final_own_minus_target_along_m"] == pytest.approx(300.0, abs=1.0)


def test_clean_probe_yaml_requires_1200s_route_return():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    for scenario_id in runner.SCENARIOS:
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        settings = data["metadata"]["simulation_settings"]
        expected = data["metadata"]["expected_outcome"]
        assert settings["total_time"] == 1200.0, scenario_id
        assert expected["returned_to_route_required"] is True, scenario_id
        assert expected["route_return_xte_m_lt"] == 150.0, scenario_id
        assert expected["route_return_heading_deg_lt"] == 10.0, scenario_id
        assert expected["route_corridor_half_width_m"] == 1000.0, scenario_id
        assert expected["route_corridor_pass_limit_m"] == 900.0, scenario_id


def test_rule13_yaml_requires_overtake_completion():
    root = Path(__file__).resolve().parents[2]
    path = root / "scenarios" / "COLREGs测试" / "colreg-rule13-ot.yaml"
    data = yaml.safe_load(path.read_text())
    expected = data["metadata"]["expected_outcome"]

    assert expected["overtake_required"] is True
    assert expected["overtake_along_margin_m"] == 0.0


def test_safe_route_left_encounter_fixture_is_in_clean_batch():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    scenario_id = "safe_route-left-encounter"
    path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"

    assert scenario_id in runner.SCENARIOS
    data = yaml.safe_load(path.read_text())
    expected = data["metadata"]["expected_outcome"]
    assert data["ownShip"]["initial"]["sog"] == 29.16
    assert expected["route_corridor_half_width_m"] == 1000.0
    assert expected["route_corridor_pass_limit_m"] == 900.0
