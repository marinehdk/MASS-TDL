import pytest

from tools.sil.colregs_trace_evaluator import (
    EncounterPhase,
    ManeuverTimingConfig,
    CpaProfile,
    TraceSample,
    classify_encounter_phase,
    compute_no_action_baseline,
    compute_t_last_maneuver_s,
    derive_cpa_threshold,
    evaluate_rule15_crossing,
    evaluate_rule17_latest_action,
    evaluate_rule8_action,
    make_minimal_passing_report,
)


@pytest.mark.parametrize(
    ("profile", "expected_formula", "expected_m", "expected_multiplier"),
    [
        ("corridor_close_start_4L", "4.0L", 180.0, 4.0),
        ("standon_in_extremis_4L", "4.0L", 180.0, 4.0),
        ("corridor_follow_or_overtake_4L", "4.0L", 180.0, 4.0),
        ("corridor_boundary_6L", "6.0L", 270.0, 6.0),
        ("corridor_follow_or_overtake_6L", "6.0L", 270.0, 6.0),
        ("ideal_corridor_domain_9L", "9.0L", 405.0, 9.0),
        ("open_water_crossing_20L", "20.0L", 900.0, 20.0),
    ],
)
def test_cpa_profiles_are_loa_scaled(profile, expected_formula, expected_m, expected_multiplier):
    derived = derive_cpa_threshold(CpaProfile(profile), loa_m=45.0)

    assert derived.threshold_formula == expected_formula
    assert derived.threshold_m == pytest.approx(expected_m)
    assert derived.loa_multiplier == pytest.approx(expected_multiplier)


def test_cpa_profile_rejects_legacy_nautical_mile_formula():
    with pytest.raises(ValueError, match="unsupported CPA profile"):
        derive_cpa_threshold(CpaProfile("open_water_warning_0p5nm"), loa_m=45.0)


def test_head_on_target_astern_with_negative_tcpa_is_post_pass_not_approach_threat():
    sample = TraceSample(
        t_s=120.0,
        range_m=120.0,
        cpa_m=120.0,
        tcpa_s=-18.0,
        closing_speed_mps=-1.5,
        rel_bearing_deg=185.0,
        colreg_rule="Rule14",
        own_duty="give_way",
        past_and_clear=False,
    )

    assert classify_encounter_phase(sample) == EncounterPhase.POST_PASS_CLEARANCE


def test_rule13_does_not_release_until_past_and_clear():
    sample = TraceSample(
        t_s=300.0,
        range_m=160.0,
        cpa_m=160.0,
        tcpa_s=-5.0,
        closing_speed_mps=-0.2,
        rel_bearing_deg=170.0,
        colreg_rule="Rule13",
        own_duty="give_way",
        past_and_clear=False,
    )

    assert classify_encounter_phase(sample) == EncounterPhase.APPROACH_RISK


def _scenario_yaml(*, target_lat=0.01, target_heading=180.0, rule="Rule14"):
    return {
        "metadata": {
            "simulation_settings": {"coordinate_origin": [0.0, 0.0]},
            "encounter": {"rule": rule, "give_way_vessel": "own"},
            "expected_outcome": {"cpa_min_m_ge": 180.0},
        },
        "ownShip": {
            "initial": {
                "position": {"latitude": 0.0, "longitude": 0.0},
                "heading": 0.0,
                "sog": 10.0,
            }
        },
        "targetShips": [
            {
                "initial": {
                    "position": {"latitude": target_lat, "longitude": 0.0},
                    "cog": target_heading,
                    "sog": 10.0,
                }
            }
        ],
    }


def test_no_action_baseline_detects_head_on_conflict():
    baseline = compute_no_action_baseline(_scenario_yaml(target_lat=0.01, target_heading=180.0))

    assert baseline.scenario_conflict_valid is True
    assert baseline.no_action_tcpa_s > 0.0
    assert baseline.no_action_dcpa_m < 180.0


def test_no_action_baseline_rejects_diverging_geometry():
    baseline = compute_no_action_baseline(_scenario_yaml(target_lat=-0.01, target_heading=180.0))

    assert baseline.scenario_conflict_valid is False
    assert baseline.no_action_tcpa_s < 0.0


def test_rule8_full_and_restricted_partial_are_separate():
    partial = evaluate_rule8_action(18.0)
    full = evaluate_rule8_action(32.0)

    assert partial.partial_pass is True
    assert partial.full_pass is False
    assert full.partial_pass is True
    assert full.full_pass is True


def test_rule15_cross_ahead_fails_give_way_compliance():
    verdict = evaluate_rule15_crossing(crossed_ahead=True)

    assert verdict.pass_ is False
    assert verdict.crossed_ahead is True


def test_rule17_latest_action_uses_dynamic_maneuver_time():
    config = ManeuverTimingConfig(
        required_heading_change_deg=30.0,
        max_effective_rot_deg_s=3.0,
        system_delay_s=5.0,
        actuator_delay_s=2.0,
        hydrodynamic_response_s=8.0,
        safety_margin_s=10.0,
    )

    assert compute_t_last_maneuver_s(config) == pytest.approx(35.0)
    verdict = evaluate_rule17_latest_action(action_onset_tcpa_s=36.0, config=config)
    assert verdict.latest_action_pass is True
    assert verdict.timing_inputs["required_heading_change_deg"] == 30.0


def test_trace_evaluation_report_json_schema_contains_required_layers():
    report = make_minimal_passing_report()
    data = report.to_json_dict()

    assert data["threshold_provenance"]["threshold_formula"] == "4.0L"
    assert data["layers"]["L1_scenario_validity"]["status"] == "PASS"
    assert data["verdict"]["overall_pass"] is True
    assert data["first_failure"] is None
    assert "chain_summary" in data
