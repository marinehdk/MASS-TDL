from pathlib import Path

from tools.sil.colregs_scenario_audit import audit_clean12_scenarios, audit_scenario_file


def test_rule15_yaml_reports_complete_five_stage_contract():
    audit = audit_scenario_file(Path("scenarios/COLREGs测试/colreg-rule15-cs.yaml"))

    assert audit["scenario_id"] == "colreg-rule15-cs"
    assert audit["rule"] == "Rule15_Stbd"
    assert audit["stage_coverage"] == {
        "approach": True,
        "trigger": True,
        "obvious_avoidance": True,
        "pass_or_release": True,
        "return_to_route": True,
    }
    assert audit["stage_coverage_complete"] is True
    assert audit["geometry"]["tcpa_s"] > 0.0
    assert audit["geometry"]["dcpa_m"] < audit["expected"]["cpa_min_m_ge"]


def test_clean12_yaml_audit_has_repaired_metadata_geometry_labels():
    report = audit_clean12_scenarios(Path("scenarios/COLREGs测试"))

    assert report["scenario_count"] == 12
    assert report["all_stage_coverage_complete"] is True
    by_id = {item["scenario_id"]: item for item in report["scenarios"]}
    assert by_id["colreg-rule15-cs-2"]["geometry"]["range_nm"] > 1.5
    assert not any(
        finding["code"] in {
            "SHORT_TCPA_LABEL_GEOMETRY_MISMATCH",
            "INSIDE_1KM_LABEL_GEOMETRY_MISMATCH",
        }
        for finding in report["findings"]
    )


def test_rule15_yaml_reports_reviewable_pre_active_window_from_dcpa_tcpa_thresholds():
    audit = audit_scenario_file(Path("scenarios/COLREGs测试/colreg-rule15-cs.yaml"))

    windows = audit["phase_windows"]
    assert windows["initial_fsm_bucket"] == "FREE_APPROACH"
    assert windows["monitor_trigger_time_s"] >= 60.0
    assert windows["active_trigger_time_s"] >= 120.0
    assert windows["free_approach_window_s"] >= 60.0
    assert windows["warning_to_active_window_s"] >= 60.0


def test_clean12_yaml_audit_summarizes_immediate_active_and_threshold_review_items():
    report = audit_clean12_scenarios(Path("scenarios/COLREGs测试"))

    assert report["phase_window_summary"]["immediate_active_count"] == 0
    assert report["phase_window_summary"]["free_approach_count"] == 7
    assert not any(
        finding["code"] == "EXPECTED_CPA_FLOOR_BELOW_ODD_ACTIVE_CPA"
        for finding in report["findings"]
    )


def test_rule14_yaml_reports_monitor_threshold_inside_visibility_after_regular_fix():
    audit = audit_scenario_file(Path("scenarios/COLREGs测试/colreg-rule14-ho.yaml"))

    windows = audit["phase_windows"]
    assert windows["required_range_for_monitor_nm"] <= windows["visibility_nm"]
    assert windows["initial_fsm_bucket"] == "FREE_APPROACH"


def test_scenario_intent_profiles_drive_review_verdicts_and_recommendations():
    scenarios_dir = Path("scenarios/COLREGs测试")

    regular = audit_scenario_file(scenarios_dir / "colreg-rule15-cs.yaml")
    short_window = audit_scenario_file(scenarios_dir / "colreg-rule15-cs-2.yaml")
    boundary = audit_scenario_file(scenarios_dir / "colreg-rule15-ot-boundary.yaml")
    stand_on = audit_scenario_file(scenarios_dir / "colreg-rule17-cr-so.yaml")
    overtake = audit_scenario_file(scenarios_dir / "colreg-rule13-ot.yaml")

    assert regular["intent_profile"]["name"] == "reviewable_long_approach"
    assert regular["intent_profile"]["verdict"] == "reviewable"
    assert not any(item["action"] == "increase_initial_range_or_reduce_closing_speed" for item in regular["recommendations"])

    assert short_window["intent_profile"]["name"] == "short_window"
    assert short_window["intent_profile"]["verdict"] == "reviewable"
    assert not any(item["action"] == "repair_short_window_geometry" for item in short_window["recommendations"])

    assert boundary["intent_profile"]["name"] == "classification_boundary"
    assert boundary["intent_profile"]["verdict"] == "reviewable"
    assert not any(item["action"] == "review_threshold_profile_for_high_speed_boundary" for item in boundary["recommendations"])

    assert stand_on["intent_profile"]["name"] == "stand_on_late_action"
    assert stand_on["intent_profile"]["verdict"] == "reviewable"

    assert overtake["intent_profile"]["name"] == "overtake_completion"
    assert overtake["intent_profile"]["verdict"] == "reviewable"
    assert not any(item["action"] == "repair_metadata_geometry_label" for item in overtake["recommendations"])


def test_clean12_intent_summary_groups_scenarios_by_profile():
    report = audit_clean12_scenarios(Path("scenarios/COLREGs测试"))

    assert report["intent_profile_summary"]["reviewable_long_approach"] == 5
    assert report["intent_profile_summary"]["short_window"] == 1
    assert report["intent_profile_summary"]["classification_boundary"] == 2
    assert report["intent_profile_summary"]["stand_on_late_action"] == 2
    assert report["intent_profile_summary"]["overtake_completion"] == 2


def test_regular_rule14_rule15_yaml_has_reviewable_active_and_return_windows():
    scenarios_dir = Path("scenarios/COLREGs测试")
    regular_ids = [
        "colreg-rule14-ho",
        "colreg-rule14-ho-port",
        "colreg-rule15-cs",
        "colreg-rule14-ho-intelligent",
        "colreg-rule15-cs-intelligent",
    ]

    for scenario_id in regular_ids:
        audit = audit_scenario_file(scenarios_dir / f"{scenario_id}.yaml")
        windows = audit["phase_windows"]
        assert windows["initial_fsm_bucket"] != "ACTIVE", scenario_id
        assert windows["active_trigger_time_s"] >= 120.0, scenario_id
        assert windows["post_cpa_return_window_s"] >= 900.0, scenario_id


def test_regular_rule15_yaml_has_free_monitoring_lead_in():
    scenarios_dir = Path("scenarios/COLREGs测试")

    for scenario_id in ["colreg-rule15-cs", "colreg-rule15-cs-intelligent"]:
        audit = audit_scenario_file(scenarios_dir / f"{scenario_id}.yaml")
        assert audit["phase_windows"]["free_approach_window_s"] >= 60.0, scenario_id


def test_all_clean12_yaml_has_pre_active_window():
    report = audit_clean12_scenarios(Path("scenarios/COLREGs测试"))

    for scenario in report["scenarios"]:
        windows = scenario["phase_windows"]
        assert windows["initial_fsm_bucket"] != "ACTIVE", scenario["scenario_id"]
        assert windows["active_trigger_time_s"] >= 120.0, scenario["scenario_id"]


def test_rule13_yaml_has_free_approach_before_overtaking_action():
    scenarios_dir = Path("scenarios/COLREGs测试")

    for scenario_id in ["colreg-rule13-ot", "colreg-rule13-ot-target-giveway"]:
        audit = audit_scenario_file(scenarios_dir / f"{scenario_id}.yaml")
        assert audit["phase_windows"]["free_approach_window_s"] >= 60.0, scenario_id
        assert audit["geometry"]["dcpa_m"] < audit["expected"]["cpa_min_m_ge"], scenario_id


def test_boundary_and_standon_yaml_have_preplan_not_immediate_active():
    scenarios_dir = Path("scenarios/COLREGs测试")

    for scenario_id in [
        "colreg-rule15-cs-2",
        "colreg-rule15-cs-edge",
        "colreg-rule15-ot-boundary",
        "colreg-rule17-cr-so",
        "colreg-rule17-cr-so-target-giveway",
    ]:
        audit = audit_scenario_file(scenarios_dir / f"{scenario_id}.yaml")
        assert audit["phase_windows"]["initial_fsm_bucket"] == "PREPLAN", scenario_id
        assert audit["phase_windows"]["active_trigger_time_s"] >= 120.0, scenario_id


def test_clean12_yaml_declares_profile_specific_cpa_contract():
    report = audit_clean12_scenarios(Path("scenarios/COLREGs测试"))

    expected_by_profile = {
        "corridor_close_start_4L": {
            "acceptance_m": 180.0,
            "m6_onset_m": 180.0,
            "m6_release_m": 180.0,
            "m7_emergency_m": 180.0,
        },
        "corridor_follow_or_overtake_4L": {
            "acceptance_m": 180.0,
            "m6_onset_m": 180.0,
            "m6_release_m": 180.0,
            "m7_emergency_m": 180.0,
        },
        "open_water_crossing_20L": {
            "acceptance_m": 900.0,
            "m6_onset_m": 900.0,
            "m6_release_m": 900.0,
            "m7_emergency_m": 180.0,
        },
        "corridor_boundary_6L": {
            "acceptance_m": 270.0,
            "m6_onset_m": 270.0,
            "m6_release_m": 270.0,
            "m7_emergency_m": 180.0,
        },
        "standon_in_extremis_4L": {
            "acceptance_m": 180.0,
            "m6_onset_m": 180.0,
            "m6_release_m": 180.0,
            "m7_emergency_m": 180.0,
        },
        "corridor_follow_or_overtake_6L": {
            "acceptance_m": 270.0,
            "m6_onset_m": 270.0,
            "m6_release_m": 270.0,
            "m7_emergency_m": 180.0,
        },
    }

    for scenario in report["scenarios"]:
        profile = scenario["cpa_contract"]["profile"]
        assert scenario["cpa_contract"]["declared"] == expected_by_profile[profile], scenario["scenario_id"]
        assert scenario["cpa_contract"]["status"] == "ok", scenario["scenario_id"]
        assert not any(item["action"] == "review_cpa_floor_contract" for item in scenario["recommendations"])
