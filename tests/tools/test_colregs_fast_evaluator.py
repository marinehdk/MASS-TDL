"""FAST-only verdict composition tests (Task 3) + first-broken-stage fixtures (Task 4).

The FAST evaluator slices the raw trace at the Task-1 lifecycle boundary and
applies scenario truth, module oracles, M6 dynamic action demand, M5/GNC
identity, CPA, and M7 closure — while EXCLUDING every recovery-execution metric.
No fixed 30-degree gate; required direction/alteration come from live M6 output.
Scenario CPA acceptance, M6 lifecycle thresholds, and the M7 checker floor are
three SEPARATE gates (no max()/cross-contract copying).

These tests are pure Python and do not require the live stack.
"""

from copy import deepcopy

import pytest

from tools.sil.colregs_fast_evaluator import (
    evaluate_fast,
    evaluate_fast_trace,
)
from tools.sil.colregs_module_oracle import OracleResult


# ─── shared fixtures (Task 3 brief) ─────────────────────────────────────────


def compiled_fixture(rule="Rule14_HeadOn", role="GIVE_WAY"):
    return {
        "compiled_rule": rule,
        "own_role": role,
        "allowed_actions": ["HOLD"] if role == "STAND_ON" else ["STARBOARD_TURN"],
        "classification": "interior",
        "truth_lock_ok": True,
        "cpa_floor_m": 184.0,
        "runtime_contracts": {
            "m6": {"cpa_hard_m": 1852.0, "cpa_release_m": 1000.0},
            "m7": {"cpa_hard_floor_m": 500.0},
        },
        "geometry": {"rel_bearing_deg": 0.0, "dcpa_m": 220.0, "tcpa_s": 125.0},
    }


def healthy_module_results():
    return {
        module: OracleResult(module=module, passed=True)
        for module in ("M1", "M2", "M4", "M5", "M6", "L4_GNC", "M7", "M8_EVALUATOR")
    }


def healthy_rows_to_first_recovery(required_deg=30.0, realized_deg=31.0):
    return [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 0.0, "behavior": 0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 70.0, "behavior": 0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 90.0, "behavior": 1},
        {"topic": "/sil/own_ship_state", "sim_t": 90.0, "heading_deg": 0.0},
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 100.0,
            "phase": "T_act",
            "conflict_detected": True,
            "encounter_state": 2,
            "past_clear": False,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [{
                "rule_id": 14,
                "role": 2,
                "rule_phase": "T_act",
                "preferred_direction": "STARBOARD",
                "min_alteration_deg": required_deg,
            }],
        },
        {"topic": "/sil/own_ship_state", "sim_t": 210.0, "heading_deg": realized_deg},
        {"topic": "/sil/scoring", "sim_t": 215.0, "cpa_m": 220.0},
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 219.0,
            "phase": "T_postAvoid",
            "conflict_detected": False,
            "encounter_state": 3,
            "past_clear": True,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [],
        },
        {"topic": "/l3/m4/behavior_plan", "sim_t": 220.0, "behavior": 7},
    ]


def rows_without_recovery():
    return healthy_rows_to_first_recovery()[:-1]


def m6_action_row(t_s, direction, required_deg):
    return {
        "topic": "/l3/m6/colregs_constraint",
        "sim_t": t_s,
        "phase": "T_act",
        "conflict_detected": True,
        "encounter_state": 2,
        "past_clear": False,
        "primary_role": 2,
        "primary_preferred_direction": direction,
        "active_rules": [{
            "rule_id": 14,
            "role": 2,
            "rule_phase": "T_act",
            "preferred_direction": direction,
            "min_alteration_deg": required_deg,
        }],
    }


# ─── Task 3 Step 1: scope-exclusion tests ───────────────────────────────────


def test_fast_verdict_ignores_recovery_execution_failures():
    verdict = evaluate_fast(
        rows=healthy_rows_to_first_recovery(),
        scenario=compiled_fixture(),
        module_results=healthy_module_results(),
        full_only={"returned_to_route": False, "recovery_route_published": False},
    )
    assert verdict.passed
    assert "returned_to_route" not in verdict.checks
    assert "recovery_route_published" not in verdict.checks


def test_fast_verdict_is_red_without_recovery_boundary():
    verdict = evaluate_fast(
        rows=rows_without_recovery(),
        scenario=compiled_fixture(),
        module_results=healthy_module_results(),
        full_only={},
    )
    assert not verdict.passed
    assert verdict.first_failure == "RECOVERY_BOUNDARY_NOT_REACHED"


# ─── Task 3 Step 2: dynamic-action tests ────────────────────────────────────


def action_rows(required_deg, realized_starboard_deg):
    return healthy_rows_to_first_recovery(required_deg, realized_starboard_deg)


def rule17_rows(stand_on_excursion_deg, t_act_required_deg, t_act_realized_deg):
    rows = healthy_rows_to_first_recovery(t_act_required_deg, t_act_realized_deg)
    rows[4] = {
        "topic": "/l3/m6/colregs_constraint",
        "sim_t": 160.0,
        "phase": "T_act",
        "primary_role": 0,
        "primary_preferred_direction": "STARBOARD",
        "conflict_detected": True,
        "encounter_state": 2,
        "past_clear": False,
        "active_rules": [{
            "rule_id": 17,
            "role": 0,
            "rule_phase": "T_act",
            "preferred_direction": "STARBOARD",
            "min_alteration_deg": t_act_required_deg,
        }],
    }
    rows.insert(4, {
        "topic": "/sil/own_ship_state",
        "sim_t": 150.0,
        "heading_deg": stand_on_excursion_deg,
    })
    return rows


def test_rule15_uses_live_m6_50deg_not_fixed_30deg():
    rows = action_rows(required_deg=50.0, realized_starboard_deg=34.0)
    verdict = evaluate_fast(rows, compiled_fixture(rule="Rule15_Crossing"), healthy_module_results(), {})
    assert not verdict.passed
    assert verdict.required_alteration_deg == 50.0
    assert verdict.realized_alteration_deg == 34.0
    assert verdict.first_failure == "M6_ACTION_NOT_REALIZED"


def test_rule13_uses_live_m6_65deg():
    rows = action_rows(required_deg=65.0, realized_starboard_deg=66.0)
    verdict = evaluate_fast(rows, compiled_fixture(rule="Rule13_Overtaking"), healthy_module_results(), {})
    assert verdict.checks["dynamic_action_realized"] is True


def test_rule17_measures_action_only_after_t_act():
    rows = rule17_rows(stand_on_excursion_deg=4.0, t_act_required_deg=30.0, t_act_realized_deg=31.0)
    verdict = evaluate_fast(rows, compiled_fixture(role="STAND_ON"), healthy_module_results(), {})
    assert verdict.checks["stand_on_hold_before_t_act"] is True
    assert verdict.checks["dynamic_action_realized"] is True


def test_direction_change_during_active_avoidance_is_m6_red():
    rows = action_rows(required_deg=50.0, realized_starboard_deg=55.0)
    rows.append(m6_action_row(160.0, direction="PORT", required_deg=50.0))
    verdict = evaluate_fast(
        rows, compiled_fixture(rule="Rule15_Crossing"),
        healthy_module_results(), {},
    )
    assert not verdict.passed
    assert verdict.first_failure == "M6_DIRECTION_INCONSISTENT"
    assert verdict.first_broken_stage == "M6"


# ─── Task 3 Step 4: separation test (3 contracts separate) ─────────────────


def test_scenario_cpa_and_m7_checker_are_independent_required_gates():
    scenario = compiled_fixture()
    rows = healthy_rows_to_first_recovery()
    modules = healthy_module_results()
    verdict = evaluate_fast(rows, scenario, modules, full_only={})
    assert verdict.checks["scenario_cpa_acceptance_met"]  # 220 m >= 184 m
    assert verdict.checks["m7_checker_closed"]

    modules["M7"] = OracleResult(
        module="M7", passed=False, failed_checks=("COMMAND_CPA_BELOW_M7_FLOOR",)
    )
    verdict = evaluate_fast(rows, scenario, modules, full_only={})
    assert verdict.checks["scenario_cpa_acceptance_met"]
    assert not verdict.checks["m7_checker_closed"]
    assert verdict.first_broken_stage == "M7"


# ─── Task 4: first-broken-stage integration fixtures ───────────────────────


def complete_fast_fixture():
    rows = healthy_rows_to_first_recovery()
    rows.extend([
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 95.0,
            "primary_relative_bearing_deg": 0.0,
            "primary_cpa_m": 220.0,
            "primary_tcpa_s": 125.0,
        },
        {
            "topic": "/l3/m5/avoidance_plan",
            "sim_t": 105.0,
            "status": "DEGRADED",
            "commit_branch": 2,
            "plan_id": "m5-r1",
            "n_waypoints": 2,
            "latitude": [63.0, 63.001],
            "longitude": [10.0, 10.001],
            "command_speed_mps": [3.0, 3.0],
            "navigation_mode": ["emergency_avoidance", "emergency_avoidance"],
            "segment_source": [1, 1],
            "rationale": "gnc_preflight=feasible",
        },
        {
            "topic": "/l3/gnc/execution_status",
            "sim_t": 106.0,
            "accepted": True,
            "execution_state": "ACCEPTED",
            "plan_id": "m5-r1",
            "active_route_id": "m5-r1",
            "command_source": "m5_committed_route",
        },
        {"topic": "/gnc/active_route", "gnc_t": 106.1, "route_id": "m5-r1", "waypoint_count": 2},
        {"topic": "/route_planning/route_plan_status", "gnc_t": 106.2, "route_id": "m5-r1", "accepted": True, "waypoint_count": 2},
        {"topic": "/ship/waypoints", "gnc_t": 106.3, "pose_count": 2},
    ])
    return rows, compiled_fixture(), {"scenario_id": "fixture", "min_cpa_m": 220.0, "first_recovery_t": 220.0}


def apply_mutation(rows, report, mutation):
    if mutation == "remove_m2_geometry":
        row = next(r for r in rows if r.get("topic") == "/l3/m2/world_state")
        for key in ("primary_relative_bearing_deg", "primary_cpa_m", "primary_tcpa_s"):
            row.pop(key)
    elif mutation == "reverse_m6_direction":
        row = next(r for r in rows if r.get("topic") == "/l3/m6/colregs_constraint" and r.get("conflict_detected"))
        row["primary_preferred_direction"] = "PORT"
        row["active_rules"][0]["preferred_direction"] = "PORT"
    elif mutation == "remove_m4_avoidance":
        rows[:] = [r for r in rows if not (r.get("topic") == "/l3/m4/behavior_plan" and r.get("behavior") == 1)]
    elif mutation == "empty_m5_route":
        row = next(r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan")
        for key in ("latitude", "longitude", "command_speed_mps", "segment_source"):
            row[key] = []
        row["n_waypoints"] = 0
    elif mutation == "mismatch_gnc_route_id":
        next(r for r in rows if r.get("topic") == "/gnc/active_route")["route_id"] = "other"
    elif mutation == "mutate_report_cpa":
        report["min_cpa_m"] = 999.0
    else:
        raise AssertionError(mutation)


@pytest.mark.parametrize(("mutation", "expected_stage"), [
    ("remove_m2_geometry", "M2"),
    ("reverse_m6_direction", "M6"),
    ("remove_m4_avoidance", "M4"),
    ("empty_m5_route", "M5"),
    ("mismatch_gnc_route_id", "L4_GNC"),
    ("mutate_report_cpa", "M8_EVALUATOR"),
])
def test_first_broken_stage_is_the_mutated_contract(mutation, expected_stage):
    rows, scenario, report = complete_fast_fixture()
    rows, scenario, report = deepcopy(rows), deepcopy(scenario), deepcopy(report)
    apply_mutation(rows, report, mutation)
    verdict = evaluate_fast_trace(rows, scenario, report)
    assert verdict.first_broken_stage == expected_stage


def test_fast_trace_passes_complete_fixture():
    rows, scenario, report = complete_fast_fixture()
    verdict = evaluate_fast_trace(rows, scenario, report)
    assert verdict.passed
    assert verdict.first_broken_stage is None


def test_target_resolves_fast_contract_passes_without_own_avoidance_or_route():
    scenario = compiled_fixture(rule="Rule13_Overtaking", role="STAND_ON")
    scenario.update({
        "fast_terminal": "ENCOUNTER_CLEAR_WITH_OWN_HOLD",
        "resolution_actor": "TARGET",
        "min_transit_s": 60.0,
        "target_action": "GIVE_WAY_STARBOARD",
        "target_initial_heading_deg": 0.0,
        "target_required_alteration_deg": 65.0,
        "target_cpa_m": 500.0,
        "target_action_deadline_s": 150.0,
    })
    rows = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 0.0, "behavior": 0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 70.0, "behavior": 0},
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 90.0,
            "primary_target_heading_deg": 0.0,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 90.0,
            "conflict_detected": True,
        },
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 120.0,
            "primary_target_heading_deg": 65.0,
            "primary_cpa_m": 600.0,
        },
        {"topic": "/l3/m4/behavior_plan", "sim_t": 180.0, "behavior": 0},
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 200.0,
            "conflict_detected": False,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 211.0,
            "conflict_detected": False,
        },
    ]
    verdict = evaluate_fast(rows, scenario, healthy_module_results(), {})
    assert verdict.passed
    assert verdict.checks["avoidance_observed"]
    assert verdict.checks["m5_executable_route_valid"]
    assert verdict.checks["gnc_handoff_identity_valid"]
    assert verdict.required_alteration_deg == 65.0
    assert verdict.realized_alteration_deg == 65.0


def test_target_resolution_rejects_action_after_own_t_act_deadline():
    scenario = compiled_fixture(rule="Rule13_Overtaking", role="STAND_ON")
    scenario.update({
        "fast_terminal": "ENCOUNTER_CLEAR_WITH_OWN_HOLD",
        "min_transit_s": 60.0,
        "target_action": "GIVE_WAY_STARBOARD",
        "target_initial_heading_deg": 0.0,
        "target_required_alteration_deg": 65.0,
        "target_cpa_m": 500.0,
        "target_action_deadline_s": 100.0,
    })
    rows = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 0.0, "behavior": 0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 70.0, "behavior": 0},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 90.0, "conflict_detected": True},
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 120.0,
            "primary_target_heading_deg": 65.0,
            "primary_cpa_m": 600.0,
        },
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 200.0, "conflict_detected": False},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 211.0, "conflict_detected": False},
    ]
    verdict = evaluate_fast(rows, scenario, healthy_module_results(), {})
    assert not verdict.passed
    assert verdict.first_failure == "TARGET_ACTION_NOT_REALIZED"


def test_both_actor_contract_requires_target_starboard_resolution():
    scenario = compiled_fixture(rule="Rule14_HeadOn", role="GIVE_WAY")
    scenario.update({
        "target_action": "GIVE_WAY_STARBOARD",
        "target_initial_heading_deg": 180.0,
        "target_required_alteration_deg": 30.0,
        "target_cpa_m": 500.0,
        "target_action_deadline_s": 200.0,
    })
    rows = healthy_rows_to_first_recovery()
    rows.extend([
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 95.0,
            "primary_target_heading_deg": 180.0,
            "primary_cpa_m": 0.0,
        },
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 150.0,
            "primary_target_heading_deg": 210.0,
            "primary_cpa_m": 600.0,
        },
    ])
    assert evaluate_fast(rows, scenario, healthy_module_results(), {}).passed

    rows = [
        row for row in rows
        if not (
            row.get("topic") == "/l3/m2/world_state"
            and row.get("sim_t") == 150.0
        )
    ]
    verdict = evaluate_fast(rows, scenario, healthy_module_results(), {})
    assert not verdict.passed
    assert verdict.first_failure == "TARGET_ACTION_NOT_REALIZED"
    assert verdict.first_broken_stage == "INTEGRATION_HANDOFF"


def test_intelligent_stand_on_target_must_hold_during_own_resolution():
    scenario = compiled_fixture(rule="Rule15_Crossing", role="GIVE_WAY")
    scenario.update({
        "target_action": "HOLD",
        "target_initial_heading_deg": 290.0,
    })
    rows = healthy_rows_to_first_recovery()
    rows.append({
        "topic": "/l3/m2/world_state",
        "sim_t": 150.0,
        "primary_target_heading_deg": 295.0,
    })
    verdict = evaluate_fast(rows, scenario, healthy_module_results(), {})
    assert not verdict.passed
    assert verdict.first_failure == "TARGET_HOLD_VIOLATED"
    assert verdict.first_broken_stage == "INTEGRATION_HANDOFF"
