from pathlib import Path

import pytest
import yaml

from tools.sil.colregs_oracle_adapter import (
    evaluate_module_oracles,
    evaluate_module_oracles_strict,
    extract_compiled,
    extract_first_m5_executable_route_time_strict,
    extract_l4_actuation_strict,
    extract_m1_output,
    extract_m2_truth_and_estimate,
    extract_m3_output,
    extract_m4_events,
    extract_m5_plan_output,
    extract_m5_plan_output_strict,
    extract_m6_output,
    extract_m6_output_strict,
)
from tools.sil.trace_time import (
    ClockAlignment,
    ClockTransform,
    EventTime,
    EventTimeSelectionError,
    select_event_time,
)


def _strict_mixed_clock_alignment() -> ClockAlignment:
    return ClockAlignment(
        lifecycle_run_generation=17,
        anchors=((0.0, 0.0), (100.0, 5.0)),
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 0, True, "simulation", 17
            ),
            "gnc_t": ClockTransform(
                "gnc_t", 0.05, 0.0, 0.1, 2, True, "gnc", 17
            ),
        },
        uncertainty_s=0.1,
        source_priority=("sim_t", "gnc_t"),
    )


def test_rule13_probe_compiles_as_dynamic_overtaking_not_head_on():
    doc = yaml.safe_load(
        Path("scenarios/COLREGs测试/colreg-rule13-ot.yaml").read_text(
            encoding="utf-8"
        )
    )

    compiled = extract_compiled(doc)

    assert compiled["geometry"]["rel_bearing_deg"] <= 6.0
    assert compiled["compiled_rule"] == "Rule13_Overtaking"
    assert compiled["own_role"] == "GIVE_WAY"


def test_m6_extract_marks_rule17_in_extremis_stand_on_action():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 760.0,
            "conflict_detected": True,
            "primary_role": 0,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 15,
                    "role": 0,
                    "rule_phase": "T_act",
                    "preferred_direction": "HOLD",
                },
                {
                    "rule_id": 17,
                    "role": 0,
                    "rule_phase": "T_act",
                    "preferred_direction": "STARBOARD",
                },
            ],
        }
    ]

    output = extract_m6_output(rows)

    assert output["rule"] == "Rule15_Crossing"
    assert output["role"] == "STAND_ON"
    assert output["preferred_direction"] == "STARBOARD_TURN"
    assert output["stand_on_in_extremis_action"] is True


def test_m6_extract_keeps_stand_on_no_action_diagnostic_rule():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 48.0,
            "conflict_detected": False,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [
                {
                    "rule_id": 13,
                    "role": 0,
                    "rule_phase": "T_standOn",
                    "preferred_direction": "HOLD",
                },
                {
                    "rule_id": 17,
                    "role": 0,
                    "rule_phase": "T_standOn",
                    "preferred_direction": "HOLD",
                },
            ],
        }
    ]

    output = extract_m6_output(rows)

    assert output["rule"] == "Rule13_Overtaking"
    assert output["role"] == "STAND_ON"
    assert output["preferred_direction"] == "HOLD"
    assert output["no_own_action_required"] is True


def test_m6_extract_reports_finally_resolved_without_release_sample():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 10.0,
            "conflict_detected": True,
            "encounter_state": 2,
            "past_clear": False,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
        {
            "topic": "/l3/asdr/record",
            "sim_t": 20.0,
            "source_module": "M6_COLREGsReasoner",
            "decision_json": '{"finally_resolved":true}',
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 21.0,
            "conflict_detected": False,
            "encounter_state": 0,
            "past_clear": False,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [],
        },
    ]

    output = extract_m6_output(rows)

    assert output["finally_resolved_count"] == 1
    assert output["release_sample_count"] == 0
    assert output["past_clear_sample_count"] == 0


def test_m6_extract_reports_onset_tcpa_from_nearest_m2_world_state():
    rows = [
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 99.0,
            "primary_tcpa_s": 719.376,
        },
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 100.01,
            "primary_tcpa_s": 730.618,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 100.0,
            "conflict_detected": True,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
    ]

    output = extract_m6_output(rows)

    assert output["onset_tcpa_s"] == 730.618


def test_m6_extract_flags_low32_target_id_alias_against_m2_world_state():
    wide_target_id = (1 << 32) + 42
    rows = [
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 99.5,
            "primary_target_id": wide_target_id,
            "primary_tcpa_s": 720.0,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 100.0,
            "conflict_detected": True,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "target_id": 42,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
    ]

    output = extract_m6_output(rows)

    assert output["m2_target_ids"] == [wide_target_id]
    assert output["m6_target_ids"] == [42]
    assert output["target_id_mismatch_count"] == 1
    assert output["target_id_width_alias_count"] == 1


def test_m6_extract_reports_conflict_rearm_after_past_clear_release():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 100.0,
            "conflict_detected": True,
            "encounter_state": 1,
            "past_clear": False,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 200.0,
            "conflict_detected": False,
            "encounter_state": 3,
            "past_clear": True,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [],
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 201.0,
            "conflict_detected": True,
            "encounter_state": 1,
            "past_clear": False,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
    ]

    output = extract_m6_output(rows)

    assert output["post_release_conflict_count"] == 1


def test_m6_extract_rearm_uses_trace_order_not_sim_time_sort():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 201.0,
            "conflict_detected": True,
            "encounter_state": 1,
            "past_clear": False,
            "primary_role": 2,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 14,
                    "role": 2,
                    "rule_phase": "T_warn",
                    "preferred_direction": "STARBOARD",
                },
            ],
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 200.0,
            "conflict_detected": False,
            "encounter_state": 3,
            "past_clear": True,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [],
        },
    ]

    output = extract_m6_output(rows)

    assert output["post_release_conflict_count"] == 0


def test_m4_extract_uses_stable_m6_clear_after_short_rearms():
    from tools.sil.colregs_oracle_adapter import extract_m4_events

    rows = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 100.0, "behavior": 1},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 201.0, "behavior": 7},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 100.0, "conflict_detected": True},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 200.0, "conflict_detected": False},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 200.5, "conflict_detected": True},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 201.0, "conflict_detected": False},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 205.0, "conflict_detected": True},
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 206.0, "conflict_detected": False},
    ]

    _, cleared_t = extract_m4_events(rows, m6_rows=rows)

    assert cleared_t == 206.0


def test_m2_extract_reads_trace_writer_primary_fields():
    truth = {"geometry": {"rel_bearing_deg": 1.0, "dcpa_m": 90.0, "tcpa_s": 600.0}}
    rows = [{
        "primary_relative_bearing_deg": 2.5,
        "primary_cpa_m": 101.0,
        "primary_tcpa_s": 590.0,
    }]
    _, estimate = extract_m2_truth_and_estimate(truth, m2_rows=rows)
    assert estimate["bearing_deg"] == 2.5
    assert estimate["cpa_m"] == 101.0
    assert estimate["tcpa_s"] == 590.0
    assert estimate["missing_fields"] == []


def test_m2_extract_reports_ownship_state_discontinuity_against_sil_truth():
    compiled = {
        "geometry": {
            "rel_bearing_deg": 0.0,
            "dcpa_m": 100.0,
            "tcpa_s": 600.0,
        }
    }
    m2_rows = [
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 1622.0,
            "own_lat": 63.5029,
            "own_lon": 10.3969,
            "primary_relative_bearing_deg": -157.8,
            "primary_cpa_m": 2024.0,
            "primary_tcpa_s": 0.0,
        }
    ]
    ownship_rows = [
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 1622.1,
            "lat": 63.4825,
            "lon": 10.3800,
        }
    ]

    _, estimate = extract_m2_truth_and_estimate(
        compiled, m2_rows=m2_rows, ownship_rows=ownship_rows)

    assert estimate["ownship_max_position_err_m"] > 2000.0
    assert estimate["ownship_position_err_t_s"] == 1622.0


def test_m2_extract_uses_best_ownship_match_when_duplicate_timestamps_exist():
    compiled = {
        "geometry": {
            "rel_bearing_deg": 0.0,
            "dcpa_m": 100.0,
            "tcpa_s": 600.0,
        }
    }
    m2_rows = [
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 90.0,
            "own_lat": 64.1806,
            "own_lon": 10.3745,
        }
    ]
    ownship_rows = [
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 90.4,
            "lat": 64.1806,
            "lon": 10.3745,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 90.4,
            "lat": 0.0,
            "lon": 0.0,
        },
    ]

    _, estimate = extract_m2_truth_and_estimate(
        compiled, m2_rows=m2_rows, ownship_rows=ownship_rows)

    assert estimate["ownship_max_position_err_m"] == 0.0


def test_m2_extract_ignores_startup_reset_transient_before_warmup():
    compiled = {
        "geometry": {
            "rel_bearing_deg": 0.0,
            "dcpa_m": 100.0,
            "tcpa_s": 600.0,
        }
    }
    m2_rows = [
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 52.7,
            "own_lat": 63.4550,
            "own_lon": 10.3800,
        },
        {
            "topic": "/l3/m2/world_state",
            "sim_t": 333.5,
            "own_lat": 63.4558,
            "own_lon": 10.3799,
        },
    ]
    ownship_rows = [
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 52.8,
            "lat": 63.4400,
            "lon": 10.3800,
        },
        {
            "topic": "/sil/own_ship_state",
            "sim_t": 333.6,
            "lat": 63.4558,
            "lon": 10.3799,
        },
    ]

    _, estimate = extract_m2_truth_and_estimate(
        compiled, m2_rows=m2_rows, ownship_rows=ownship_rows)

    assert estimate["ownship_max_position_err_m"] == 0.0
    assert estimate["ownship_position_err_t_s"] == 333.5


# ── M2 CPA truth recompute from observed sensor inputs (Issue #4) ──────────
#
# The M2 oracle checks M2's *geometry computation* accuracy, not controller
# speed-tracking or scenario-vs-simulator placement error. The compiled truth
# CPA uses the YAML nominal own-sog and the YAML-declared target position, but:
#   (1) the controller tracks a slightly different own-sog; and
#   (2) the simulator may place the target off from the YAML declaration.
# For near-radial encounters both are amplified 100-150x in CPA, producing a
# spurious MEASUREMENT_INCONSISTENT. The fix: recompute truth CPA from M2's OWN
# observed sensor inputs (bearing, range, both speeds/cogs) so the comparison
# isolates M2's geometry formula from sensor/placement/speed-tracking error.
#
# These tests verify the recompute logic in extract_m2_truth_and_estimate.

import math as _math


def _sensor_cpa(own_sog_kn, own_heading_deg, brg_deg, rng_m,
                tgt_sog_kn, tgt_cog_deg):
    """Standard CPA formula from sensor inputs (mirrors what M2 should compute).
    Used to build both the M2 estimate row and the expected recomputed truth."""
    te = rng_m * _math.sin(_math.radians(brg_deg))
    tn = rng_m * _math.cos(_math.radians(brg_deg))
    own_cog = own_heading_deg % 360.0
    ovx = own_sog_kn * 0.514444 * _math.sin(_math.radians(own_cog))
    ovy = own_sog_kn * 0.514444 * _math.cos(_math.radians(own_cog))
    tvx = tgt_sog_kn * 0.514444 * _math.sin(_math.radians(tgt_cog_deg))
    tvy = tgt_sog_kn * 0.514444 * _math.cos(_math.radians(tgt_cog_deg))
    rvx, rvy = tvx - ovx, tvy - ovy
    rx, ry = te, tn
    vv = rvx * rvx + rvy * rvy
    tcpa = 0.0 if vv < 1e-9 else max(0.0, -(rx * rvx + ry * rvy) / vv)
    dcpa = _math.hypot(rx + rvx * tcpa, ry + rvy * tcpa)
    return dcpa, tcpa


def test_m2_truth_cpa_recomputed_from_sensor_inputs():
    """Near-radial crossing: the compiled truth CPA (nominal own-sog + YAML
    target pos) ≈ 2.3m, but M2 correctly reports ~360m from its observed sensor
    inputs (own-sog 11.3kn, target at observed brg/rng). Without recompute the
    oracle fails (15636% CPA error). With recompute truth CPA is derived from
    the same sensor inputs M2 used, matching within tolerance."""
    nominal_own_sog = 10.8    # kn, from YAML
    observed_own_sog = 11.32  # kn, controller reality
    own_heading = 0.0
    brg, rng = 50.224, 9948.7  # M2 observed bearing/range at settle frame
    tgt_cog, tgt_sog = 290.0, 9.55

    # M2 estimate: CPA M2 computes from its observed sensor inputs.
    m2_cpa, m2_tcpa = _sensor_cpa(observed_own_sog, own_heading, brg, rng,
                                  tgt_sog, tgt_cog)
    # Compiled truth with a WRONG (nominal) CPA — this is what made the oracle
    # fail before the fix.
    compiled = {
        "geometry": {"dcpa_m": 2.34, "tcpa_s": 1659.0,
                     "rel_bearing_deg": brg % 360.0},
        "own_sog_kn": nominal_own_sog,
    }
    m2_rows = [{
        "topic": "/l3/m2/world_state",
        "sim_t": 48.0,
        "primary_brg_deg": brg,
        "primary_rng_m": rng,
        "own_sog_kn": observed_own_sog,
        "own_heading_deg": own_heading,
        "primary_target_sog_kn": tgt_sog,
        "primary_target_cog_deg": tgt_cog,
        "primary_cpa_m": m2_cpa,
        "primary_tcpa_s": m2_tcpa,
        "primary_relative_bearing_deg": brg % 360.0,
    }]

    truth, estimate = extract_m2_truth_and_estimate(compiled, m2_rows=m2_rows)

    # Truth CPA must be recomputed from M2's sensor inputs, NOT the nominal.
    expected_cpa, expected_tcpa = _sensor_cpa(
        observed_own_sog, own_heading, brg, rng, tgt_sog, tgt_cog)
    assert abs(truth["cpa_m"] - expected_cpa) < 1.0, (
        f"truth CPA must be recomputed from sensor inputs: "
        f"expected ~{expected_cpa:.1f}, got {truth['cpa_m']:.1f}"
    )
    assert abs(truth["tcpa_s"] - expected_tcpa) < 1.0


def test_m2_truth_cpa_falls_back_when_sensor_inputs_absent():
    """Legacy M2 trace (no bearing/range/speed sensor fields) must not crash;
    truth CPA stays at the compiled nominal value with a graceful fallback."""
    compiled = {
        "geometry": {"dcpa_m": 2.34, "tcpa_s": 1659.0, "rel_bearing_deg": 50.0},
        "own_sog_kn": 10.8,
    }
    m2_rows = [{
        "topic": "/l3/m2/world_state",
        "sim_t": 48.0,
        "primary_cpa_m": 370.0,   # estimate present
        "primary_tcpa_s": 1603.0,
        "primary_relative_bearing_deg": 50.0,
        # NO sensor inputs (primary_brg_deg, primary_rng_m, own_sog_kn, etc.)
    }]
    truth, _ = extract_m2_truth_and_estimate(compiled, m2_rows=m2_rows)
    # No sensor recompute possible → truth stays at compiled nominal (2.34).
    assert truth["cpa_m"] == 2.34


def test_m2_truth_cpa_recompute_empty_m2_rows():
    """Empty M2 rows must not crash; truth stays at compiled nominal."""
    compiled = {
        "geometry": {"dcpa_m": 100.0, "tcpa_s": 600.0, "rel_bearing_deg": 0.0},
        "own_sog_kn": 6.0,
    }
    truth, _ = extract_m2_truth_and_estimate(compiled, m2_rows=[])
    assert truth["cpa_m"] == 100.0
    assert truth["tcpa_s"] == 600.0


def test_m5_extract_counts_recovery_corridor_and_rejected_return_attempts():
    rows = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 100.0, "behavior": 1},
        {
            "topic": "/l3/m5/avoidance_plan",
            "sim_t": 130.0,
            "status": "VALID",
            "n_waypoints": 6,
            "commit_branch": 1,
            "plan_id": "m5-colregs-1",
            "latitude": [63.0, 63.001, 63.002, 63.003, 63.004, 63.005],
            "longitude": [10.0, 10.001, 10.002, 10.003, 10.004, 10.005],
            "command_speed_mps": [3.0] * 6,
            "navigation_mode": ["flyby"] * 6,
            "segment_source": list(range(6)),
            "rationale": "gnc_preflight=feasible",
        },
        {"topic": "/l3/m4/behavior_plan", "sim_t": 200.0, "behavior": 7},
        {
            "topic": "/l3/asdr/record",
            "sim_t": 205.0,
            "source_module": "M5_Tactical_Planner",
            "decision_type": "committed_route_rejected",
            "decision_json": (
                '{"reason":"recovery_preflight_failed:first_turn_radius_too_small"}'
            ),
        },
        {
            "topic": "/l3/m5/avoidance_plan",
            "sim_t": 210.0,
            "status": "VALID",
            "n_waypoints": 3,
            "commit_branch": 2,
            "plan_id": "m5-return-2",
            "latitude": [63.0, 63.001, 63.002],
            "longitude": [10.0, 10.001, 10.002],
            "command_speed_mps": [3.0] * 3,
            "navigation_mode": ["flyby"] * 3,
            "segment_source": [0, 1, 2],
            "rationale": "gnc_preflight=feasible",
        },
    ]

    output = extract_m5_plan_output(rows)

    assert output["solver_status"] == "VALID"
    assert output["m4_recovery_seen"] is True
    assert output["corridor_in_recovery_count"] == 1
    assert output["recovery_rejected_count"] == 1
    assert output["recovery_publish_count"] == 0
    assert output["gnc_accepted_recovery_count"] == 0


# ─── M5 strict validity (Task 3 Step 1): fail closed, no fabrication ──────


def _m5_plan(sim_t, *, n_waypoints=2, commit_branch=1, plan_id="m5-colregs-1"):
    """A row that fully satisfies a valid, executable M5 route contract.

    Used as a known-good baseline; individual tests corrupt one field to prove
    the validity check is non-trivial and fails closed.
    """
    return {
        "topic": "/l3/m5/avoidance_plan",
        "sim_t": sim_t,
        "status": "VALID",
        "commit_branch": commit_branch,
        "n_waypoints": n_waypoints,
        "plan_id": plan_id,
        "latitude": [63.0, 63.001],
        "longitude": [10.0, 10.002],
        "command_speed_mps": [3.0, 3.0],
        "navigation_mode": ["flyby", "flyby"],
        "segment_source": [0, 1],
        "rationale": "gnc_preflight=feasible",
    }


def test_m5_commit_branch_without_route_is_not_valid():
    output = extract_m5_plan_output([{
        "topic": "/l3/m5/avoidance_plan",
        "sim_t": 10.0,
        "status": "DEGRADED",
        "commit_branch": 2,
        "n_waypoints": 0,
        "latitude": [],
        "longitude": [],
        "command_speed_mps": [],
        "segment_source": [],
    }])
    assert output["solver_status"] == "EMPTY"
    assert output["n_waypoints"] == 0
    assert output["valid_route_count"] == 0


def test_m5_route_requires_complete_arrays_and_preflight():
    row = _m5_plan(10.0, n_waypoints=2)
    row["command_speed_mps"] = []
    output = extract_m5_plan_output([row])
    assert output["valid_route_count"] == 0


def test_m5_route_requires_commit_branch_in_avoidance_set():
    row = _m5_plan(10.0, commit_branch=5)  # branch 5 is not a committed exec branch
    output = extract_m5_plan_output([row])
    assert output["valid_route_count"] == 0


def test_m5_route_requires_nonempty_plan_id():
    row = _m5_plan(10.0, plan_id="")
    output = extract_m5_plan_output([row])
    assert output["valid_route_count"] == 0


def test_m5_valid_route_count_counts_complete_proven_routes():
    good = _m5_plan(10.0)
    output = extract_m5_plan_output([good])
    assert output["solver_status"] == "VALID"
    assert output["valid_route_count"] == 1
    assert "m5-colregs-1" in output["valid_plan_ids"]


def test_m5_n_waypoints_never_synthesized_to_one_for_empty_plan():
    output = extract_m5_plan_output([{
        "topic": "/l3/m5/avoidance_plan",
        "sim_t": 10.0,
        "status": "DEGRADED",
        "commit_branch": 2,
    }])
    assert output["n_waypoints"] == 0
    assert output["valid_route_count"] == 0


# ─── M1 / M3 trace extraction (Task 4 qualification) ─────────────────────


def test_m1_extract_reports_mrc_override_active_from_envelope_seq():
    rows = [
        {"topic": "/l3/odd_state", "sim_t": 1.0, "envelope_state": 0,
         "conformance_score": 0.9},
        {"topic": "/l3/odd_state", "sim_t": 2.0, "envelope_state": 3,
         "conformance_score": 0.2},
    ]
    output = extract_m1_output(rows)
    assert output["envelope_seq"] == [0, 3]
    assert output["mrc_override_active"] is True


def test_m1_extract_reports_out_to_in_skipped_edge_recovery():
    # OUT(2) -> IN(0) skips EDGE(1): recovered_to_edge must be False.
    rows = [
        {"topic": "/l3/odd_state", "sim_t": 1.0, "envelope_state": 2,
         "conformance_score": 0.3},
        {"topic": "/l3/odd_state", "sim_t": 2.0, "envelope_state": 0,
         "conformance_score": 0.9},
    ]
    output = extract_m1_output(rows)
    assert output["recovered_to_edge"] is False


def test_m1_extract_no_stale_marker_when_rationale_clean():
    rows = [
        {"topic": "/l3/odd_state", "sim_t": 1.0, "envelope_state": 0,
         "conformance_score": 0.95, "zone_reason": "nominal"},
    ]
    output = extract_m1_output(rows)
    assert output["stale_input_count"] == 0
    assert output["score_degraded_on_stale"] is False


def test_m3_extract_reports_no_duplicate_reject_when_absent():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 1},
    ]
    output = extract_m3_output(rows)
    assert output["duplicate_route_rejected"] is None


def test_m3_extract_reports_reset_not_returned_to_idle_when_terminal_active():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 4},
        {
            "topic": "/l3/asdr/record",
            "sim_t": 2.0,
            "source_module": "M3_Mission_Manager",
            "decision_type": "mission_reset",
        },
        {"topic": "/l3/mission_goal", "sim_t": 3.0, "fsm_state": 4},
    ]
    output = extract_m3_output(rows)
    assert output["reset_returned_to_idle"] is False


def test_m3_extract_ignores_asdr_records_from_other_modules():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 1},
        {
            "topic": "/l3/asdr/record",
            "sim_t": 2.0,
            "source_module": "M5_Tactical_Planner",
            "decision_type": "mission_reset",
        },
    ]
    output = extract_m3_output(rows)
    assert output["reset_returned_to_idle"] is None


def test_m4_strict_time_path_sorts_by_canonical_time_and_preserves_clock_evidence():
    alignment = _strict_mixed_clock_alignment()
    rows = [
        {
            "record_id": "behavior-early",
            "topic": "/l3/m4/behavior_plan",
            "gnc_t": 100.0,
            "source_domain": "gnc",
            "run_generation": 17,
            "behavior": 1,
        },
        {
            "record_id": "behavior-late",
            "topic": "/l3/m4/behavior_plan",
            "sim_t": 10.0,
            "source_domain": "simulation",
            "run_generation": 17,
            "behavior": 7,
        },
    ]

    events, _ = extract_m4_events(rows, alignment=alignment)

    assert [event["t"] for event in events] == [5.0, 10.0]
    assert events[0]["event_time"] == {
        "canonical_s": 5.0,
        "raw_s": 100.0,
        "source": "gnc_t",
        "alignment_id": alignment.alignment_id,
        "uncertainty_s": 0.1,
    }


def test_m4_strict_time_path_does_not_fabricate_or_omit_missing_time():
    rows = [
        {
            "record_id": "missing-m4-time",
            "topic": "/l3/m4/behavior_plan",
            "run_generation": 17,
            "behavior": 1,
        }
    ]

    with pytest.raises(EventTimeSelectionError, match="missing-m4-time"):
        extract_m4_events(rows, alignment=_strict_mixed_clock_alignment())


def test_strict_oracle_report_retains_selected_time_evidence():
    alignment = _strict_mixed_clock_alignment()
    row = {
        "record_id": "oracle-time",
        "topic": "/l3/m4/behavior_plan",
        "gnc_t": 100.0,
        "source_domain": "gnc",
        "run_generation": 17,
        "behavior": 0,
    }

    results = evaluate_module_oracles_strict([row], alignment=alignment)

    for result in results.values():
        assert result.evidence["event_times"] == [
            {
                "canonical_s": 5.0,
                "raw_s": 100.0,
                "source": "gnc_t",
                "alignment_id": alignment.alignment_id,
                "uncertainty_s": 0.1,
            }
        ]


def _strict_row(record_id: str, topic: str, sim_t: float, **payload):
    return {
        "record_id": record_id,
        "topic": topic,
        "sim_t": sim_t,
        "source_domain": "simulation",
        "run_generation": 17,
        **payload,
    }


@pytest.mark.parametrize(
    ("field", "forged_value"),
    [
        ("canonical_s", 0.0),
        ("raw_s", 0.0),
        ("source", "gnc_t"),
        ("alignment_id", "forged-alignment"),
        ("uncertainty_s", 0.2),
    ],
)
def test_strict_adapter_recomputes_and_rejects_every_forged_cached_field(
    field, forged_value
):
    alignment = _strict_mixed_clock_alignment()
    row = _strict_row(
        "forged-time",
        "/l3/m6/colregs_constraint",
        10.0,
        conflict_detected=False,
    )
    selected = select_event_time(row, alignment)
    fields = selected.as_dict()
    fields[field] = forged_value
    row["__g1_event_time"] = EventTime(**fields)

    with pytest.raises(EventTimeSelectionError, match="cached_event_time_mismatch"):
        extract_m6_output_strict([row], alignment=alignment)


def test_strict_adapter_rejects_cached_time_without_raw_clock():
    alignment = _strict_mixed_clock_alignment()
    row = {
        "record_id": "cached-only",
        "topic": "/l3/m6/colregs_constraint",
        "run_generation": 17,
        "__g1_event_time": EventTime(
            0.0, 0.0, "sim_t", alignment.alignment_id, 0.1
        ),
    }

    with pytest.raises(EventTimeSelectionError, match="clock_missing"):
        extract_m6_output_strict([row], alignment=alignment)


def test_strict_adapter_sequence_rejects_canonical_backjump():
    alignment = _strict_mixed_clock_alignment()
    rows = [
        _strict_row("later", "/l3/m6/colregs_constraint", 2.0),
        _strict_row("earlier", "/l3/m6/colregs_constraint", 1.0),
    ]
    for row in rows:
        row["__g1_event_time"] = select_event_time(row, alignment)

    with pytest.raises(EventTimeSelectionError, match="canonical_time_backjump"):
        extract_m6_output_strict(rows, alignment=alignment)


@pytest.mark.parametrize(
    "call",
    [
        lambda: extract_m6_output_strict([]),
        lambda: extract_m5_plan_output_strict([]),
        lambda: extract_first_m5_executable_route_time_strict(
            [],
            command_time=EventTime(0.0, 0.0, "sim_t", "missing", 0.1),
        ),
        lambda: extract_l4_actuation_strict(
            [],
            command_time=EventTime(0.0, 0.0, "sim_t", "missing", 0.1),
        ),
        lambda: evaluate_module_oracles_strict([]),
    ],
    ids=["m6", "m5", "m5-handoff", "l4", "module-oracles"],
)
def test_every_strict_adapter_api_fails_closed_without_alignment(call):
    with pytest.raises(EventTimeSelectionError, match="clock_alignment_missing"):
        call()


def test_strict_m6_and_m5_outputs_preserve_selected_event_time_provenance():
    alignment = _strict_mixed_clock_alignment()
    m6_row = _strict_row(
        "m6-time",
        "/l3/m6/colregs_constraint",
        4.0,
        conflict_detected=True,
        primary_role=2,
        primary_preferred_direction="STARBOARD",
        active_rules=[
            {
                "rule_id": 14,
                "role": 2,
                "rule_phase": "T_warn",
                "preferred_direction": "STARBOARD",
            }
        ],
    )
    recovery_row = _strict_row(
        "m4-recovery", "/l3/m4/behavior_plan", 5.0, behavior=7
    )
    m5_row = _strict_row(
        "m5-time",
        "/l3/m5/avoidance_plan",
        6.0,
        status="VALID",
        commit_branch=1,
        n_waypoints=2,
        plan_id="m5-colregs-strict",
        latitude=[63.0, 63.001],
        longitude=[10.0, 10.001],
        command_speed_mps=[3.0, 3.0],
        navigation_mode=["flyby", "flyby"],
        segment_source=[0, 1],
        rationale="gnc_preflight=feasible",
    )

    m6_output = extract_m6_output_strict([m6_row], alignment=alignment)
    m5_output = extract_m5_plan_output_strict(
        [recovery_row, m5_row], alignment=alignment
    )

    assert m6_output["event_times"] == [
        select_event_time(m6_row, alignment).as_dict()
    ]
    assert m5_output["event_times"] == [
        select_event_time(m5_row, alignment).as_dict()
    ]
    assert m5_output["first_recovery"] == select_event_time(
        recovery_row, alignment
    ).as_dict()


def test_strict_m5_l4_chain_uses_typed_boundaries_and_outputs_typed_times():
    alignment = _strict_mixed_clock_alignment()
    command_row = {
        "record_id": "m4-command",
        "topic": "/l3/m4/behavior_plan",
        "gnc_t": 100.0,
        "source_domain": "gnc",
        "run_generation": 17,
        "behavior": 1,
    }
    command_time = select_event_time(command_row, alignment)
    route_row = _strict_row(
        "m5-route",
        "/l3/m5/avoidance_plan",
        5.5,
        status="VALID",
        commit_branch=1,
        n_waypoints=2,
        plan_id="m5-colregs-typed",
        latitude=[63.0, 63.001],
        longitude=[10.0, 10.001],
        command_speed_mps=[3.0, 3.0],
        navigation_mode=["flyby", "flyby"],
        segment_source=[0, 1],
        rationale="gnc_preflight=feasible",
    )
    own_rows = [
        _strict_row(
            "own-pre", "/sil/own_ship_state", 4.0, heading_deg=0.0
        ),
        {
            "record_id": "own-realized",
            "topic": "/sil/own_ship_state",
            "gnc_t": 120.0,
            "source_domain": "gnc",
            "run_generation": 17,
            "heading_deg": 20.0,
        },
    ]

    route_time = extract_first_m5_executable_route_time_strict(
        [command_row, route_row],
        command_time=command_time,
        command_record_id="m4-command",
        alignment=alignment,
    )
    l4_output = extract_l4_actuation_strict(
        [own_rows[0], command_row, own_rows[1]],
        command_time=command_time,
        command_record_id="m4-command",
        alignment=alignment,
    )

    assert route_time == select_event_time(route_row, alignment)
    assert l4_output["first_command"] == command_time.as_dict()
    assert l4_output["first_realized"] == select_event_time(
        own_rows[1], alignment
    ).as_dict()
    assert "first_command_t" not in l4_output
    assert "first_realized_t" not in l4_output


def test_strict_l4_rejects_naked_command_time():
    with pytest.raises(EventTimeSelectionError, match="event_time_payload_invalid"):
        extract_l4_actuation_strict(
            [], command_time=5.0, alignment=_strict_mixed_clock_alignment()
        )


def test_strict_m5_rejects_unarchived_m4_command_event_time():
    alignment = _strict_mixed_clock_alignment()
    injected_command = _strict_row(
        "injected-command", "/l3/m4/behavior_plan", 5.0, behavior=1
    )
    route_row = _strict_row(
        "m5-route",
        "/l3/m5/avoidance_plan",
        5.5,
        status="VALID",
        commit_branch=1,
        n_waypoints=2,
        plan_id="m5-colregs-injected-boundary",
        latitude=[63.0, 63.001],
        longitude=[10.0, 10.001],
        command_speed_mps=[3.0, 3.0],
        navigation_mode=["flyby", "flyby"],
        segment_source=[0, 1],
        rationale="gnc_preflight=feasible",
    )

    with pytest.raises(
        EventTimeSelectionError, match="event_time_source_row_missing"
    ):
        extract_first_m5_executable_route_time_strict(
            [route_row],
            command_time=select_event_time(injected_command, alignment),
            command_record_id="injected-command",
            alignment=alignment,
        )


def test_strict_l4_rejects_unarchived_m6_release_event_time():
    alignment = _strict_mixed_clock_alignment()
    command_row = _strict_row(
        "m4-command", "/l3/m4/behavior_plan", 5.0, behavior=1
    )
    injected_release = _strict_row(
        "injected-release",
        "/l3/m6/colregs_constraint",
        8.0,
        conflict_detected=False,
    )

    with pytest.raises(
        EventTimeSelectionError, match="event_time_source_row_missing"
    ):
        extract_l4_actuation_strict(
            [command_row],
            command_time=select_event_time(command_row, alignment),
            command_record_id="m4-command",
            release_time=select_event_time(injected_release, alignment),
            release_record_id="injected-release",
            alignment=alignment,
        )


def test_strict_l4_has_no_first_realized_without_ownship_observation():
    alignment = _strict_mixed_clock_alignment()
    command_row = _strict_row(
        "m4-command", "/l3/m4/behavior_plan", 5.0, behavior=1
    )

    output = extract_l4_actuation_strict(
        [command_row],
        command_time=select_event_time(command_row, alignment),
        command_record_id="m4-command",
        alignment=alignment,
    )

    assert output["first_realized"] is None
