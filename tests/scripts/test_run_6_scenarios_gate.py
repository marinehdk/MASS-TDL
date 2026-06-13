import importlib.util
from pathlib import Path

import pytest
import yaml


EXPECTED_CLEAN_8 = [
    "colreg-rule14-ho",
    "colreg-rule14-ho-port",
    "colreg-rule13-ot",
    "colreg-rule15-cs",
    "colreg-rule15-cs-2",
    "colreg-rule15-cs-edge",
    "colreg-rule15-ot-boundary",
    "colreg-rule17-cr-so",
]


def _load_runner():
    path = Path(__file__).resolve().parents[2] / "scripts" / "run_6_scenarios.py"
    spec = importlib.util.spec_from_file_location("run_6_scenarios", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_base_url_can_target_local_orchestrator(monkeypatch):
    monkeypatch.setenv("SIL_ORCH_BASE_URL", "https://127.0.0.1:8000/api/v1")
    runner = _load_runner()
    assert runner.BASE == "https://127.0.0.1:8000/api/v1"


def test_clean_probe_batch_has_expected_8_scenarios():
    runner = _load_runner()
    assert runner.SCENARIOS == EXPECTED_CLEAN_8


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


def test_route_status_ignores_ownship_records_far_from_scenario_origin():
    runner = _load_runner()
    records = [
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 1.0,
            "lat": 30.5,
            "lon": 122.0,
            "heading_deg": 0.0,
        },
        {"topic": "/l3/m4/behavior_plan", "sim_t": 10.0, "behavior": 1},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 40.0, "behavior": 0},
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 12.0,
            "lat": 0.0,
            "lon": 450.0 / 111120.0,
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
        route_corridor_pass_limit_m=500.0,
    )

    assert status["returned_to_route"] is True
    assert status["max_route_xte_m"] == pytest.approx(450.0, abs=1.0)
    assert status["route_corridor_ok"] is True


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
        assert expected["route_corridor_pass_limit_m"] == 500.0, scenario_id


def test_rule14_close_start_probe_uses_corridor_limited_cpa_floor():
    root = Path(__file__).resolve().parents[2]
    for scenario_id in ("colreg-rule14-ho", "colreg-rule14-ho-port"):
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        expected = data["metadata"]["expected_outcome"]

        assert expected["cpa_min_m_ge"] == 185.2, scenario_id
        assert expected["cpa_acceptance"]["profile"] == (
            "corridor_close_start_0p1nm")
        assert expected["cpa_acceptance"]["threshold_m"] == 185.2, scenario_id
        assert expected["cpa_acceptance"]["emergency_floor_m"] == 185.2
        assert expected["cpa_acceptance"]["ideal_domain_m"] == 405.0


def test_clean_probe_yaml_declares_cpa_acceptance_profile():
    root = Path(__file__).resolve().parents[2]
    expected_profiles = {
        "colreg-rule14-ho": "corridor_close_start_0p1nm",
        "colreg-rule14-ho-port": "corridor_close_start_0p1nm",
        "colreg-rule13-ot": "corridor_follow_or_overtake_0p1nm_to_9loa",
        "colreg-rule15-cs": "open_water_warning_0p5nm",
        "colreg-rule15-cs-2": "open_water_warning_0p5nm",
        "colreg-rule15-cs-edge": "corridor_boundary_0p1nm_to_9loa",
        "colreg-rule15-ot-boundary": "corridor_boundary_0p1nm_to_9loa",
        "colreg-rule17-cr-so": "standon_in_extremis_0p1nm",
    }
    for scenario_id, profile in expected_profiles.items():
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        expected = data["metadata"]["expected_outcome"]
        cpa_acceptance = expected["cpa_acceptance"]

        assert cpa_acceptance["profile"] == profile, scenario_id
        assert cpa_acceptance["threshold_m"] == expected["cpa_min_m_ge"]
        assert cpa_acceptance["basis"], scenario_id


def test_expected_cpa_floor_uses_profile_threshold_and_rejects_mismatch():
    runner = _load_runner()

    expected = {
        "cpa_min_m_ge": 400.0,
        "cpa_acceptance": {"threshold_m": 400.0},
    }
    assert runner.expected_cpa_floor_m(expected) == 400.0

    with pytest.raises(ValueError):
        runner.expected_cpa_floor_m({
            "cpa_min_m_ge": 500.0,
            "cpa_acceptance": {"threshold_m": 400.0},
        })


def test_runner_uses_500m_default_cpa_floor():
    runner = _load_runner()
    assert runner.DEFAULT_CPA_FLOOR_M == 500.0
    assert runner.expected_cpa_floor_m({}) == 500.0


def test_rule13_yaml_allows_corridor_limited_safe_following():
    root = Path(__file__).resolve().parents[2]
    path = root / "scenarios" / "COLREGs测试" / "colreg-rule13-ot.yaml"
    data = yaml.safe_load(path.read_text())
    expected = data["metadata"]["expected_outcome"]

    assert expected["overtake_required"] is False
    assert expected["cpa_acceptance"]["profile"] == (
        "corridor_follow_or_overtake_0p1nm_to_9loa")


def test_safe_route_left_encounter_fixture_is_not_in_clean_8_probe():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    scenario_id = "safe_route-left-encounter"
    path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"

    assert scenario_id not in runner.SCENARIOS
    data = yaml.safe_load(path.read_text())
    expected = data["metadata"]["expected_outcome"]
    assert data["ownShip"]["initial"]["sog"] == 29.16
    assert expected["route_corridor_half_width_m"] == 1000.0
    assert expected["route_corridor_pass_limit_m"] == 900.0
