import importlib.util
import math
import subprocess
import sys
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


def _ownship_record(t_s, east_m, north_m, *, heading_deg=0.0, sog_kn=0.0):
    return {
        "topic": "/sil/own_ship_state",
        "sim_t": t_s,
        "lat": north_m / 111120.0,
        "lon": east_m / 111120.0,
        "heading_deg": heading_deg,
        "sog_kn": sog_kn,
    }


def _risk_defaults(**overrides):
    values = {
        "primary_threat_id": "",
        "primary_threat_switches": 0,
        "max_risk_score": 0.0,
        "worst_warning_margin_m": 0.0,
        "worst_danger_margin_m": 0.0,
        "max_warning_ddv": 0.0,
        "max_danger_ddv": 0.0,
        "warning_domain_exposure_s": 0.0,
        "danger_domain_exposure_s": 0.0,
        "risk_recovery_ok": True,
        "risk_trace": [],
    }
    values.update(overrides)
    return values


def _seamanship_defaults(**overrides):
    values = {
        "integrated_abs_xte_m_s": 0.0,
        "route_crossing_overshoot_count": 0,
        "path_length_m": 0.0,
        "path_length_ratio": 1.0,
    }
    values.update(overrides)
    return values


def test_base_url_can_target_local_orchestrator(monkeypatch):
    monkeypatch.setenv("SIL_ORCH_BASE_URL", "https://127.0.0.1:8000/api/v1")
    runner = _load_runner()
    assert runner.BASE == "https://127.0.0.1:8000/api/v1"


def test_configure_scenario_retries_transient_service_unavailable(monkeypatch):
    runner = _load_runner()
    calls = []
    responses = [
        {"success": False, "error": "SetParameters service not available after 3s"},
        {"success": True},
    ]

    def fake_req(method, path, body=None, timeout=30):
        calls.append((method, path, body, timeout))
        return responses.pop(0)

    monkeypatch.setattr(runner, "req", fake_req)
    monkeypatch.setattr(runner.time, "sleep", lambda _seconds: None)

    assert runner.configure_scenario("colreg-rule14-ho", attempts=2)["success"] is True
    assert [call[1] for call in calls] == ["/lifecycle/configure", "/lifecycle/configure"]


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


def test_overall_gate_requires_domain_and_seamanship_gates():
    runner = _load_runner()

    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
    ) is True
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
        risk_gate_ok=False,
    ) is False
    assert runner.compute_overall_pass(
        cpa_ok=True,
        stability_pass=True,
        returned_to_route=True,
        seamanship_gate_ok=False,
    ) is False


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


def test_route_return_requires_stable_transit_after_last_avoidance():
    runner = _load_runner()
    records = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 10.0, "behavior": 1},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 20.0, "behavior": 0},
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 21.0,
            "lat": 0.0,
            "lon": 0.0,
            "heading_deg": 0.0,
        },
    ]

    early = runner.compute_route_return_status(
        records,
        lat0=0.0,
        lon0=0.0,
        init_lat=0.0,
        init_lon=0.0,
        init_hdg=0.0,
        route_return_release_dwell_s=5.0,
    )

    assert early["released_after_avoidance"] is False
    assert early["returned_to_route"] is False

    records.append({
        "topic": "/sil/own_ship_state",
        "sim_t": 26.0,
        "lat": 0.0,
        "lon": 0.0,
        "heading_deg": 0.0,
    })

    stable = runner.compute_route_return_status(
        records,
        lat0=0.0,
        lon0=0.0,
        init_lat=0.0,
        init_lon=0.0,
        init_hdg=0.0,
        route_return_release_dwell_s=5.0,
    )

    assert stable["released_after_avoidance"] is True
    assert stable["returned_to_route"] is True


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


def test_domain_gate_fails_danger_exposure():
    runner = _load_runner()

    gates = runner.compute_domain_gate_status(
        _risk_defaults(danger_domain_exposure_s=1.0),
        _seamanship_defaults(),
    )

    assert gates["danger_domain_ok"] is False
    assert gates["risk_gate_ok"] is False
    assert gates["seamanship_gate_ok"] is True
    assert gates["domain_gate_ok"] is False


def test_domain_gate_allows_close_start_danger_ddv_only_when_flagged():
    runner = _load_runner()
    risk_metrics = _risk_defaults(max_danger_ddv=0.2)
    seamanship_metrics = _seamanship_defaults()

    blocked = runner.compute_domain_gate_status(
        risk_metrics,
        seamanship_metrics,
        close_start_emergency_allowed=False,
    )
    allowed = runner.compute_domain_gate_status(
        risk_metrics,
        seamanship_metrics,
        close_start_emergency_allowed=True,
    )

    assert blocked["danger_ddv_ok"] is False
    assert blocked["risk_gate_ok"] is False
    assert allowed["danger_ddv_ok"] is True
    assert allowed["risk_gate_ok"] is True


def test_seamanship_integrated_xte_limit_can_follow_corridor_profile():
    runner = _load_runner()
    risk_metrics = _risk_defaults()
    seamanship_metrics = _seamanship_defaults(integrated_abs_xte_m_s=329000.0)

    default_gates = runner.compute_domain_gate_status(
        risk_metrics,
        seamanship_metrics,
    )
    boundary_gates = runner.compute_domain_gate_status(
        risk_metrics,
        seamanship_metrics,
        integrated_abs_xte_limit_m_s=700.0 * 600.0,
    )

    assert default_gates["integrated_xte_ok"] is False
    assert default_gates["seamanship_gate_ok"] is False
    assert boundary_gates["integrated_abs_xte_limit_m_s"] == pytest.approx(420000.0)
    assert boundary_gates["integrated_xte_ok"] is True
    assert boundary_gates["seamanship_gate_ok"] is True


def test_seamanship_metrics_integrate_abs_xte_and_overshoot():
    runner = _load_runner()
    records = [
        _ownship_record(0.0, 100.0, 0.0),
        _ownship_record(10.0, 100.0, 100.0),
        _ownship_record(20.0, -100.0, 200.0),
        _ownship_record(30.0, -100.0, 300.0),
        _ownship_record(40.0, 100.0, 400.0),
    ]

    metrics = runner.compute_seamanship_metrics(
        records,
        lat0=0.0,
        lon0=0.0,
        init_lat=0.0,
        init_lon=0.0,
        init_hdg=0.0,
        route_distance_m=400.0,
    )

    assert metrics["integrated_abs_xte_m_s"] == pytest.approx(4000.0, abs=1.0)
    assert metrics["route_crossing_overshoot_count"] == 2
    expected_path = 200.0 + 2.0 * math.hypot(200.0, 100.0)
    assert metrics["path_length_m"] == pytest.approx(expected_path, abs=1.0)
    assert metrics["path_length_ratio"] == pytest.approx(expected_path / 400.0, abs=0.01)


def test_compute_risk_metrics_detects_primary_target_and_exposure():
    runner = _load_runner()
    records = [
        _ownship_record(0.0, 0.0, 0.0, heading_deg=0.0, sog_kn=0.0),
        _ownship_record(10.0, 0.0, 0.0, heading_deg=0.0, sog_kn=0.0),
    ]
    targets = [{
        "static": {"mmsi": 999000001},
        "lat0": 200.0 / 111120.0,
        "lon0": 0.0,
        "cog": 180.0,
        "sog_kn": 0.0,
    }]

    metrics = runner.compute_risk_metrics(
        records,
        targets,
        lat0=0.0,
        lon0=0.0,
        encounter={"rule": "Rule14", "give_way_vessel": "own"},
    )

    assert metrics["primary_threat_id"] == "999000001"
    assert metrics["max_risk_score"] > 0.0
    assert metrics["max_danger_ddv"] > 0.0
    assert metrics["warning_domain_exposure_s"] == pytest.approx(10.0)
    assert metrics["danger_domain_exposure_s"] == pytest.approx(5.0)
    assert metrics["risk_trace"][0]["risk_phase"] == "Danger"


def test_risk_recovery_fails_when_score_does_not_decrease_after_avoidance():
    runner = _load_runner()
    records = [
        _ownship_record(0.0, 0.0, 0.0, heading_deg=0.0, sog_kn=0.0),
        {"topic": "/l3/m4/behavior_plan", "sim_t": 10.0, "behavior": 1},
        _ownship_record(10.0, 0.0, 0.0, heading_deg=0.0, sog_kn=0.0),
        _ownship_record(70.0, 0.0, 0.0, heading_deg=0.0, sog_kn=0.0),
    ]
    targets = [{
        "id": "ts-danger",
        "lat0": 200.0 / 111120.0,
        "lon0": 0.0,
        "cog": 180.0,
        "sog_kn": 0.0,
    }]

    metrics = runner.compute_risk_metrics(
        records,
        targets,
        lat0=0.0,
        lon0=0.0,
        encounter={"rule": "Rule14", "give_way_vessel": "own"},
    )

    assert metrics["risk_recovery_ok"] is False


def test_risk_recovery_passes_when_avoidance_stays_outside_warning_domain():
    runner = _load_runner()
    risk_trace = [
        {
            "t_s": 10.0,
            "risk_phase": "Monitor",
            "risk_score": 0.30,
            "warning_ddv": 0.0,
            "danger_ddv": 0.0,
            "closing_speed_mps": 6.0,
        },
        {
            "t_s": 70.0,
            "risk_phase": "Clear",
            "risk_score": 0.34,
            "warning_ddv": 0.0,
            "danger_ddv": 0.0,
            "closing_speed_mps": 5.0,
        },
    ]

    assert runner._risk_recovery_ok(risk_trace, 10.0, 0.34) is True


def test_risk_recovery_passes_when_warning_peak_recovers_within_window():
    runner = _load_runner()
    risk_trace = [
        {
            "t_s": 10.0,
            "risk_phase": "Monitor",
            "risk_score": 0.30,
            "warning_ddv": 0.0,
            "danger_ddv": 0.0,
            "closing_speed_mps": 6.0,
        },
        {
            "t_s": 120.0,
            "risk_phase": "Warning",
            "risk_score": 0.52,
            "warning_ddv": 0.10,
            "danger_ddv": 0.0,
            "closing_speed_mps": 4.0,
        },
        {
            "t_s": 180.0,
            "risk_phase": "Clear",
            "risk_score": 0.20,
            "warning_ddv": 0.0,
            "danger_ddv": 0.0,
            "closing_speed_mps": -2.0,
        },
    ]

    assert runner._risk_recovery_ok(risk_trace, 10.0, 0.52) is True


def test_risk_recovery_passes_without_avoidance_when_outside_warning():
    runner = _load_runner()
    records = [
        _ownship_record(0.0, 0.0, 0.0, heading_deg=0.0, sog_kn=5.0),
        _ownship_record(60.0, 0.0, 300.0, heading_deg=0.0, sog_kn=5.0),
    ]
    targets = [{
        "id": "ts-opening-clear",
        "lat0": -1200.0 / 111120.0,
        "lon0": 0.0,
        "cog": 180.0,
        "sog_kn": 5.0,
    }]

    metrics = runner.compute_risk_metrics(
        records,
        targets,
        lat0=0.0,
        lon0=0.0,
        encounter={"rule": "Rule15", "give_way_vessel": "own"},
    )

    assert metrics["max_risk_score"] > 0.0
    assert metrics["warning_domain_exposure_s"] == 0.0
    assert metrics["risk_recovery_ok"] is True


def test_result_schema_has_new_domain_fields():
    runner = _load_runner()
    risk_metrics = runner.compute_risk_metrics(
        [_ownship_record(0.0, 0.0, 0.0)],
        [],
        lat0=0.0,
        lon0=0.0,
    )
    seamanship_metrics = runner.compute_seamanship_metrics(
        [_ownship_record(0.0, 0.0, 0.0)],
        lat0=0.0,
        lon0=0.0,
        init_lat=0.0,
        init_lon=0.0,
        init_hdg=0.0,
    )
    result = {
        **risk_metrics,
        **seamanship_metrics,
        "domain_gates": runner.compute_domain_gate_status(
            risk_metrics,
            seamanship_metrics,
        ),
    }

    assert {
        "primary_threat_id",
        "primary_threat_switches",
        "max_risk_score",
        "worst_warning_margin_m",
        "worst_danger_margin_m",
        "max_warning_ddv",
        "max_danger_ddv",
        "warning_domain_exposure_s",
        "danger_domain_exposure_s",
        "risk_recovery_ok",
        "risk_trace",
        "integrated_abs_xte_m_s",
        "route_crossing_overshoot_count",
        "path_length_ratio",
        "domain_gates",
    } <= result.keys()


def test_clean_probe_yaml_requires_1200s_route_return():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    for scenario_id in runner.SCENARIOS:
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        settings = data["metadata"]["simulation_settings"]
        expected = data["metadata"]["expected_outcome"]
        assert settings["total_time"] == 1200.0, scenario_id
        corridor_contained_recovery = {
            "colreg-rule13-ot",
        }
        assert expected["returned_to_route_required"] is (
            scenario_id not in corridor_contained_recovery
        ), scenario_id
        assert expected["route_return_xte_m_lt"] == 150.0, scenario_id
        assert expected["route_return_heading_deg_lt"] == 10.0, scenario_id
        assert expected["route_corridor_half_width_m"] == 1000.0, scenario_id
        expected_pass_limit = {
            "colreg-rule14-ho": 550.0,
            "colreg-rule14-ho-port": 550.0,
            "colreg-rule15-ot-boundary": 700.0,
        }.get(scenario_id, 500.0)
        assert expected["route_corridor_pass_limit_m"] == expected_pass_limit, scenario_id


def test_rule14_close_start_probe_uses_corridor_limited_cpa_floor():
    root = Path(__file__).resolve().parents[2]
    for scenario_id in ("colreg-rule14-ho", "colreg-rule14-ho-port"):
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        data = yaml.safe_load(path.read_text())
        expected = data["metadata"]["expected_outcome"]

        assert expected["cpa_min_m_ge"] == 180.0, scenario_id
        assert expected["cpa_acceptance"]["profile"] == (
            "corridor_close_start_4L")
        assert expected["cpa_acceptance"]["threshold_m"] == 180.0, scenario_id
        assert expected["cpa_acceptance"]["emergency_floor_m"] == 180.0
        assert expected["cpa_acceptance"]["ideal_domain_m"] == 405.0


def test_clean_probe_yaml_declares_length_scaled_cpa_acceptance_profile():
    runner = _load_runner()
    root = Path(__file__).resolve().parents[2]
    expected_profiles = {
        "colreg-rule14-ho": ("corridor_close_start_4L", 180.0, "4.0L", 4.0, "project_profile_medium"),
        "colreg-rule14-ho-port": ("corridor_close_start_4L", 180.0, "4.0L", 4.0, "project_profile_medium"),
        "colreg-rule13-ot": ("corridor_follow_or_overtake_4L", 180.0, "4.0L", 4.0, "project_profile_medium_low"),
        "colreg-rule15-cs": ("open_water_crossing_20L", 900.0, "20.0L", 20.0, "project_profile_medium_high"),
        "colreg-rule15-cs-2": ("open_water_crossing_20L", 900.0, "20.0L", 20.0, "project_profile_medium_high"),
        "colreg-rule15-cs-edge": ("corridor_boundary_6L", 270.0, "6.0L", 6.0, "project_profile_medium_low"),
        "colreg-rule15-ot-boundary": ("corridor_boundary_6L", 270.0, "6.0L", 6.0, "project_profile_medium_low"),
        "colreg-rule17-cr-so": ("standon_in_extremis_4L", 180.0, "4.0L", 4.0, "project_profile_medium"),
    }
    for scenario_id in runner.SCENARIOS:
        path = root / "scenarios" / "COLREGs测试" / f"{scenario_id}.yaml"
        expected = yaml.safe_load(path.read_text())["metadata"]["expected_outcome"]
        profile, threshold_m, formula, multiplier, confidence = expected_profiles[scenario_id]
        cpa = expected["cpa_acceptance"]

        assert expected["cpa_min_m_ge"] == pytest.approx(threshold_m), scenario_id
        assert cpa["profile"] == profile, scenario_id
        assert cpa["threshold_formula"] == formula, scenario_id
        assert cpa["threshold_m"] == pytest.approx(threshold_m), scenario_id
        assert cpa["loa_m"] == pytest.approx(45.0), scenario_id
        assert cpa["loa_multiplier"] == pytest.approx(multiplier), scenario_id
        assert cpa["ideal_domain_m"] == pytest.approx(405.0), scenario_id
        assert cpa["source_confidence"] == confidence, scenario_id
        assert "NM" not in cpa["basis"], scenario_id


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


def test_expected_cpa_floor_validates_length_scaled_profile():
    runner = _load_runner()

    expected = {
        "cpa_min_m_ge": 270.0,
        "cpa_acceptance": {
            "profile": "corridor_boundary_6L",
            "threshold_formula": "6.0L",
            "threshold_m": 270.0,
            "loa_m": 45.0,
        },
    }

    assert runner.expected_cpa_floor_m(expected) == pytest.approx(270.0)

    with pytest.raises(ValueError, match="threshold_m"):
        runner.expected_cpa_floor_m({
            "cpa_min_m_ge": 270.0,
            "cpa_acceptance": {
                "profile": "corridor_boundary_6L",
                "threshold_formula": "6.0L",
                "threshold_m": 300.0,
                "loa_m": 45.0,
            },
        })

    with pytest.raises(ValueError, match="threshold_formula"):
        runner.expected_cpa_floor_m({
            "cpa_min_m_ge": 270.0,
            "cpa_acceptance": {
                "profile": "corridor_boundary_6L",
                "threshold_formula": "round_envelope(6L)",
                "threshold_m": 270.0,
                "loa_m": 45.0,
            },
        })


def test_runner_uses_500m_default_cpa_floor():
    runner = _load_runner()
    assert runner.DEFAULT_CPA_FLOOR_M == 500.0
    assert runner.expected_cpa_floor_m({}) == 500.0
    assert runner.cpa_floor_measurement_tolerance_m({}) == 0.0


def test_length_scaled_cpa_floor_has_small_measurement_tolerance():
    runner = _load_runner()
    expected = {
        "cpa_min_m_ge": 180.0,
        "cpa_acceptance": {
            "profile": "corridor_close_start_4L",
            "threshold_formula": "4.0L",
            "threshold_m": 180.0,
            "loa_m": 45.0,
        },
    }

    assert runner.cpa_floor_measurement_tolerance_m(expected) == pytest.approx(2.25)


def test_restart_between_runs_restarts_sil_nodes_and_settles(monkeypatch):
    runner = _load_runner()
    calls = []
    sleeps = []

    def fake_run(args, **kwargs):
        calls.append((args, kwargs))
        return subprocess.CompletedProcess(args, 0, stdout="container\n")

    monkeypatch.setattr(runner.subprocess, "run", fake_run)
    monkeypatch.setattr(runner.time, "sleep", lambda seconds: sleeps.append(seconds))

    runner._restart_sil_nodes("mass-l3-sil-sil-nodes-1", 24.0)

    assert calls[0][0] == ["docker", "restart", "mass-l3-sil-sil-nodes-1"]
    assert sleeps == [24.0]


def test_rule13_yaml_allows_corridor_limited_safe_following():
    root = Path(__file__).resolve().parents[2]
    path = root / "scenarios" / "COLREGs测试" / "colreg-rule13-ot.yaml"
    data = yaml.safe_load(path.read_text())
    expected = data["metadata"]["expected_outcome"]

    assert expected["overtake_required"] is False
    assert expected["returned_to_route_required"] is False
    assert expected["cpa_acceptance"]["profile"] == (
        "corridor_follow_or_overtake_4L")
    assert expected["cpa_acceptance"]["threshold_m"] == 180.0


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


def test_clean_8_runner_name_lists_eight_scenarios():
    root = Path(__file__).resolve().parents[2]
    result = subprocess.run(
        [sys.executable, "scripts/run_colregs_clean_8probe.py", "--list"],
        cwd=root,
        text=True,
        capture_output=True,
        check=True,
    )

    scenarios = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    assert scenarios == EXPECTED_CLEAN_8
    assert result.stderr == ""


def test_legacy_runner_lists_eight_scenarios_with_deprecation_warning():
    root = Path(__file__).resolve().parents[2]
    result = subprocess.run(
        [sys.executable, "scripts/run_6_scenarios.py", "--list"],
        cwd=root,
        text=True,
        capture_output=True,
        check=True,
    )

    scenarios = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    assert scenarios == EXPECTED_CLEAN_8
    assert "deprecated" in result.stderr


def test_runner_writes_trace_evaluation_report(tmp_path):
    runner = _load_runner()
    result = {
        "cpa_ok": True,
        "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
        "route_corridor_ok": True,
        "route_return_required": True,
        "returned_to_route": True,
        "compliance_verdict": "full",
        "stability_pass": True,
    }

    report_path = runner._write_trace_evaluation_report(
        "colreg-rule14-ho", result, tmp_path)

    assert report_path is not None
    data = yaml.safe_load(Path(report_path).read_text())
    assert data["scenario_id"] == "colreg-rule14-ho"
    assert data["threshold_provenance"]["threshold_formula"] == "4.0L"
    assert data["verdict"]["overall_pass"] is True
    assert "L5_route_recovery" in data["layers"]


def test_runner_archives_per_scenario_raw_trace(tmp_path):
    runner = _load_runner()
    trace_path = tmp_path / "trace_current.jsonl"
    trace_path.write_text('{"topic":"/sil/own_ship_state","sim_t":1.0}\n')

    archived = runner._archive_trace_artifact(
        "colreg-rule14-ho",
        tmp_path / "reports",
        trace_path=trace_path,
    )

    assert archived == str(tmp_path / "reports" / "colreg-rule14-ho.trace_current.jsonl")
    assert Path(archived).read_text() == trace_path.read_text()


# ── C1 crossing past-and-clear threshold (90° beam + tcpa<0 backstop) ──────
# The 112.5° abaft-beam (Rule 13(b) overtaking sector) was unreachable for
# shallow slow crossings: rule15-cs (cog=290/10.6kn) asymptotes past the beam
# only after own-ship avoidance+recovery. Crossing/head-on give-way now releases
# at 90° beam + tcpa<0 (past CPA) + range>=cpa_safe opening. See
# docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md.


def _c1_phase_semantics(*, own_records, behavior_records, targets_meta,
                        role="give_way", rule="Rule15", cpa_safe_m=1852.0):
    runner = _load_runner()
    return runner.compute_phase_semantics(
        own_records + behavior_records, targets_meta,
        lat0=0.0, lon0=0.0, role=role, rule=rule, cpa_safe_m=cpa_safe_m,
    )


def test_c1_crossing_passes_past_beam_under_90deg_but_not_abaft_sector():
    """Crossing give-way releases with target just past the beam (rel_brg ~101°,
    between the 90° beam and the 112.5° abaft sector), tcpa<0, range>=cpa_safe
    and opening. The old 112.5° gate would RED this; the 90° beam + tcpa<0
    gate must PASS it. Own ship stationary at origin (hdg=0); target approaches,
    crosses CPA, opens past the stbd beam."""
    # Target meta: starts near origin (past-CPA region) and moves outward along a
    # bearing that places it at rel_brg ~101° (past stbd beam, not abaft) at the
    # release sample. ENU start (100,-50), velocity (sin100,cos100)*3.0 m/s.
    start_e, start_n = 100.0, -50.0
    speed_mps = 3.0
    vel_e = speed_mps * math.sin(math.radians(100.0))
    vel_n = speed_mps * math.cos(math.radians(100.0))
    targets_meta = [{
        "lat0": start_n / 111120.0,
        "lon0": start_e / (111120.0 * math.cos(math.radians(0.0))),
        "cog": 100.0,
        "sog_kn": speed_mps / 0.514444,
    }]
    # Own ship stationary at origin throughout (sog=0, hdg=0).
    own_records = [
        {"topic": "/sil/own_ship_state", "sim_t": float(t),
         "lat": 0.0, "lon": 0.0, "heading_deg": 0.0, "sog_kn": 0.0}
        for t in range(0, 1001, 50)
    ]
    # Avoidance 100..550, release (behavior=0) at t=600. At t=600 the target is
    # at rel_brg ~101°, range ~1907m (>cpa_safe), tcpa<0, opening.
    behavior_records = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": float(t),
         "behavior": 1 if 100 <= t <= 550 else 0}
        for t in range(0, 1001, 50)
    ]
    result = _c1_phase_semantics(
        own_records=own_records, behavior_records=behavior_records,
        targets_meta=targets_meta,
    )
    assert result["evaluated"] is True, "phase_semantics must have data"
    assert result["c1_past_clear_ok"] is True, (
        f"crossing C1 should pass at 90° beam + tcpa<0 (rel_brg~101, between "
        f"beam and abaft sector); got rel_brg={result['release_target_rel_bearing_deg']}"
    )


def test_c1_early_return_at_bow_with_tcpa_ahead_stays_red():
    """Mechanical right-turn + early route return while the target is still on
    the bow (rel_brg ~36°) and tcpa>0 (CPA still ahead): C1 must stay RED. This
    is the bug the gate catches — no threshold relaxation should let it through."""
    # Target on stbd bow at rel_brg 36°, range 3000m, closing (CPA ahead).
    start_range = 3000.0
    start_e = start_range * math.sin(math.radians(36.0))
    start_n = start_range * math.cos(math.radians(36.0))
    # Velocity toward origin (closing): along bearing 36+180 = 216, 2.0 m/s.
    closing_speed = 2.0
    vel_e = closing_speed * math.sin(math.radians(216.0))
    vel_n = closing_speed * math.cos(math.radians(216.0))
    targets_meta = [{
        "lat0": start_n / 111120.0,
        "lon0": start_e / (111120.0 * math.cos(math.radians(0.0))),
        "cog": 216.0,
        "sog_kn": closing_speed / 0.514444,
    }]
    own_records = [
        {"topic": "/sil/own_ship_state", "sim_t": float(t),
         "lat": 0.0, "lon": 0.0, "heading_deg": 0.0, "sog_kn": 0.0}
        for t in range(0, 401, 25)
    ]
    # Brief avoidance 100..200, release at t=225 (target still on bow, tcpa>0).
    behavior_records = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": float(t),
         "behavior": 1 if 100 <= t <= 200 else 0}
        for t in range(0, 401, 25)
    ]
    result = _c1_phase_semantics(
        own_records=own_records, behavior_records=behavior_records,
        targets_meta=targets_meta,
    )
    assert result["evaluated"] is True
    assert result["c1_past_clear_ok"] is False, (
        "early return at rel_brg~36° with tcpa>0 must stay RED"
    )
