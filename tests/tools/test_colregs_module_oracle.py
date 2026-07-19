from tools.sil.colregs_module_oracle import (
    evaluate_l4_gnc_oracle,
    evaluate_l4_oracle,
    evaluate_m1_mrm_authority_oracle,
    evaluate_m1_oracle,
    evaluate_m3_oracle,
    evaluate_m4_oracle,
    evaluate_m2_oracle,
    evaluate_m5_oracle,
    evaluate_m6_oracle,
    evaluate_m7_oracle,
)
from tools.sil.colregs_oracle_adapter import (
    evaluate_module_oracles,
    extract_m2_truth_and_estimate,
    extract_m1_output,
    extract_m3_output,
    extract_m6_output,
)


def test_m6_oracle_allows_rule17_in_extremis_starboard_for_stand_on():
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule15_Crossing",
            "own_role": "STAND_ON",
            "allowed_actions": ["HOLD"],
            "classification": "interior",
        },
        m6_output={
            "rule": "Rule15_Crossing",
            "role": "STAND_ON",
            "preferred_direction": "STARBOARD_TURN",
            "stand_on_in_extremis_action": True,
            "flip_count": 0,
            "flip_intervals_s": [],
        },
    )

    assert result.passed
    assert "FORBIDDEN_DIRECTION" not in result.failed_checks


def test_m6_oracle_flags_finally_resolved_without_release_sample():
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule14_HeadOn",
            "own_role": "GIVE_WAY",
            "allowed_actions": ["STARBOARD_TURN"],
            "classification": "interior",
        },
        m6_output={
            "rule": "Rule14_HeadOn",
            "role": "GIVE_WAY",
            "preferred_direction": "STARBOARD_TURN",
            "flip_count": 0,
            "flip_intervals_s": [],
            "finally_resolved_count": 1,
            "release_sample_count": 0,
            "past_clear_sample_count": 0,
        },
    )

    assert not result.passed
    assert "LIFECYCLE_RELEASE_MISSING" in result.failed_checks
    assert result.evidence["finally_resolved_count"] == 1
    assert result.evidence["release_sample_count"] == 0
    assert result.evidence["past_clear_sample_count"] == 0


def test_m6_oracle_flags_conflict_rearm_after_release():
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule14_HeadOn",
            "own_role": "GIVE_WAY",
            "allowed_actions": ["STARBOARD_TURN"],
            "classification": "interior",
        },
        m6_output={
            "rule": "Rule14_HeadOn",
            "role": "GIVE_WAY",
            "preferred_direction": "STARBOARD_TURN",
            "flip_count": 0,
            "flip_intervals_s": [],
            "finally_resolved_count": 1,
            "release_sample_count": 1,
            "past_clear_sample_count": 1,
            "post_release_conflict_count": 3,
        },
    )

    assert not result.passed
    assert "REARM_AFTER_RELEASE" in result.failed_checks
    assert result.evidence["post_release_conflict_count"] == 3


def test_m6_oracle_flags_onset_tcpa_outside_plan_window():
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule14_HeadOn",
            "own_role": "GIVE_WAY",
            "allowed_actions": ["STARBOARD_TURN"],
            "classification": "interior",
        },
        m6_output={
            "rule": "Rule14_HeadOn",
            "role": "GIVE_WAY",
            "preferred_direction": "STARBOARD_TURN",
            "flip_count": 0,
            "flip_intervals_s": [],
            "onset_tcpa_s": 725.169,
        },
    )

    assert not result.passed
    assert "ONSET_TCPA_OUT_OF_PLAN_WINDOW" in result.failed_checks
    assert result.evidence["onset_tcpa_s"] == 725.169
    assert result.evidence["t_plan_s"] == 720.0


def test_m6_oracle_flags_target_id_width_alias():
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule14_HeadOn",
            "own_role": "GIVE_WAY",
            "allowed_actions": ["STARBOARD_TURN"],
            "classification": "interior",
        },
        m6_output={
            "rule": "Rule14_HeadOn",
            "role": "GIVE_WAY",
            "preferred_direction": "STARBOARD_TURN",
            "flip_count": 0,
            "flip_intervals_s": [],
            "target_id_mismatch_count": 1,
            "target_id_width_alias_count": 1,
            "m2_target_ids": [(1 << 32) + 42],
            "m6_target_ids": [42],
        },
    )

    assert not result.passed
    assert "TARGET_ID_WIDTH_ALIAS" in result.failed_checks
    assert result.evidence["target_id_width_alias_count"] == 1


def test_m6_extract_debounces_short_conflict_false_samples():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 100.0,
            "conflict_detected": True,
            "primary_role": 1,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [{"rule_id": 14, "role": 2, "preferred_direction": "STARBOARD"}],
        },
        {"topic": "/l3/m6/colregs_constraint", "sim_t": 100.5, "conflict_detected": False},
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 101.0,
            "conflict_detected": True,
            "primary_role": 1,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [{"rule_id": 14, "role": 2, "preferred_direction": "STARBOARD"}],
        },
    ]

    output = extract_m6_output(rows)
    result = evaluate_m6_oracle(
        compiled={
            "compiled_rule": "Rule14_HeadOn",
            "own_role": "GIVE_WAY",
            "allowed_actions": ["STARBOARD_TURN"],
            "classification": "interior",
        },
        m6_output=output,
    )

    assert output["flip_intervals_s"] == []
    assert result.passed


def test_m4_oracle_flags_recovery_chatter_back_to_colregs():
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID"},
            {"t": 200.0, "behavior": "RECOVERY"},
            {"t": 201.0, "behavior": "COLREG_AVOID"},
            {"t": 202.0, "behavior": "RECOVERY"},
        ],
        m6_conflict_cleared_t=199.0,
    )

    assert not result.passed
    assert "RECOVERY_CHATTER_AFTER_RELEASE" in result.failed_checks
    assert result.evidence["post_recovery_colreg_avoid_count"] == 1


def test_m5_oracle_allows_empty_plan_when_no_action_required():
    result = evaluate_m5_oracle(
        plan_output={"solver_status": "EMPTY", "n_waypoints": 0},
        plan_required=False,
    )

    assert result.passed
    assert "NO_FEASIBLE_PLAN" not in result.failed_checks


def test_m5_oracle_flags_recovery_without_executable_return_route():
    result = evaluate_m5_oracle(
        plan_output={
            "solver_status": "VALID",
            "n_waypoints": 3,
            "m4_recovery_seen": True,
            "corridor_in_recovery_count": 11,
            "recovery_rejected_count": 916,
            "recovery_publish_count": 0,
            "gnc_accepted_recovery_count": 0,
        },
        plan_required=True,
    )

    assert not result.passed
    assert "RECOVERY_RETURN_ROUTE_MISSING" in result.failed_checks
    assert result.evidence["corridor_in_recovery_count"] == 11
    assert result.evidence["recovery_rejected_count"] == 916


def test_m5_oracle_flags_bcmpc_follow_without_reactive_override():
    result = evaluate_m5_oracle(
        plan_output={
            "solver_status": "VALID",
            "n_waypoints": 3,
            "bcmpc_follow_count": 4,
            "reactive_override_after_bcmpc_count": 0,
        },
        plan_required=True,
    )

    assert not result.passed
    assert "BCMPC_FOLLOW_WITHOUT_REACTIVE_OVERRIDE" in result.failed_checks
    assert result.evidence["bcmpc_follow_count"] == 4
    assert result.evidence["reactive_override_after_bcmpc_count"] == 0


def test_m2_oracle_flags_ownship_state_inconsistent_with_sil_truth():
    result = evaluate_m2_oracle(
        truth={"bearing_deg": 0.0, "cpa_m": 100.0, "tcpa_s": 600.0},
        estimated={
            "bearing_deg": 0.0,
            "cpa_m": 100.0,
            "tcpa_s": 600.0,
            "ownship_max_position_err_m": 2260.0,
            "ownship_position_err_t_s": 1622.0,
        },
    )

    assert not result.passed
    assert "OWNSHIP_STATE_INCONSISTENT" in result.failed_checks
    assert result.evidence["ownship_max_position_err_m"] == 2260.0
    assert result.evidence["ownship_position_err_t_s"] == 1622.0


def test_m2_oracle_is_red_when_observed_geometry_is_missing():
    truth, estimate = extract_m2_truth_and_estimate(
        {"geometry": {"rel_bearing_deg": 1.0, "dcpa_m": 90.0, "tcpa_s": 600.0}},
        m2_rows=[{"topic": "/l3/m2/world_state"}],
    )
    result = evaluate_m2_oracle(truth=truth, estimated=estimate)
    assert not result.passed
    assert "M2_TRACE_FIELDS_MISSING" in result.failed_checks


# ── M2 CPA near-zero absolute floor (Issue #4) ─────────────────────────────
#
# When the truth CPA is near zero (near-collision geometry, e.g. head-on), the
# percentage check is pathological: a 1m absolute difference against a 2m truth
# yields 50% error even though both values mean "collision course". Add an
# absolute floor so near-zero CPAs are compared on absolute terms.

def test_m2_oracle_near_zero_cpa_uses_absolute_tolerance():
    """Truth CPA 1.7m, estimate 1.1m (0.6m diff) → both mean 'collision course'.
    The percentage check (35% > 10%) must NOT flag this when the absolute
    difference is below the near-zero absolute floor."""
    result = evaluate_m2_oracle(
        truth={"bearing_deg": 0.0, "cpa_m": 1.7, "tcpa_s": 1087.0},
        estimated={"bearing_deg": 0.0, "cpa_m": 1.1, "tcpa_s": 1089.0},
    )
    assert "MEASUREMENT_INCONSISTENT" not in result.failed_checks, (
        f"near-zero CPA (1.7 vs 1.1m, both collision course) must not fail on "
        f"percentage; failed: {result.failed_checks}"
    )


def test_m2_oracle_near_zero_cpa_still_fails_on_large_absolute_diff():
    """Truth CPA 1.7m (collision), estimate 600m (safe pass) → 598m absolute
    diff. Even with the near-zero floor, a genuinely inconsistent large
    estimate must still be flagged."""
    result = evaluate_m2_oracle(
        truth={"bearing_deg": 0.0, "cpa_m": 1.7, "tcpa_s": 1087.0},
        estimated={"bearing_deg": 0.0, "cpa_m": 600.0, "tcpa_s": 1089.0},
    )
    assert "MEASUREMENT_INCONSISTENT" in result.failed_checks


def test_m2_oracle_normal_cpa_uses_percentage_tolerance():
    """Truth CPA 200m, estimate 230m (15% diff > 10%) → must fail (normal range,
    percentage check applies, no near-zero floor)."""
    result = evaluate_m2_oracle(
        truth={"bearing_deg": 0.0, "cpa_m": 200.0, "tcpa_s": 600.0},
        estimated={"bearing_deg": 0.0, "cpa_m": 230.0, "tcpa_s": 600.0},
    )
    assert "MEASUREMENT_INCONSISTENT" in result.failed_checks


def test_l4_oracle_allows_no_heading_change_when_no_action_required():
    result = evaluate_l4_oracle(
        first_command_t=0.0,
        first_realized_t=0.0,
        realized_heading_change_deg=0.2,
        route_accepted=None,
        action_required=False,
    )

    assert result.passed
    assert "INSUFFICIENT_ACTION" not in result.failed_checks


# ─── M1 ODD/Envelope Manager oracle qualification (Task 4) ───────────────
# M1 is the sole safety-context authority. The oracle verifies the published
# envelope_state sequence honors stale-degradation, recovery monotonicity,
# override priority, and boundary validity — without any module-local truth.


def test_m1_oracle_passes_nominal_in_state_sequence():
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [0, 0, 0],
        "stale_input_count": 0,
        "score_degraded_on_stale": False,
        "recovered_to_edge": None,
        "mrc_override_active": False,
    })
    assert result.passed
    assert result.failed_checks == []


def test_m1_oracle_flags_empty_envelope_trace():
    result = evaluate_m1_oracle(m1_output={})
    assert not result.passed
    assert "M1_ENVELOPE_TRACE_EMPTY" in result.failed_checks


def test_m1_oracle_flags_invalid_envelope_state():
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [0, 9],
        "stale_input_count": 0,
        "mrc_override_active": False,
    })
    assert not result.passed
    assert "M1_ENVELOPE_INVALID_STATE" in result.failed_checks
    assert result.evidence["invalid_states"] == [9]


def test_m1_oracle_flags_stale_input_without_score_degradation():
    # Stale M2 input MUST degrade the conformance score (design §3.6).
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [0, 1],
        "stale_input_count": 3,
        "score_degraded_on_stale": False,
        "mrc_override_active": False,
    })
    assert not result.passed
    assert "M1_STALE_INPUT_NOT_DEGRADED" in result.failed_checks
    assert result.evidence["stale_input_count"] == 3


def test_m1_oracle_passes_when_stale_input_degrades_score():
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [0, 1],
        "stale_input_count": 2,
        "score_degraded_on_stale": True,
        "mrc_override_active": False,
    })
    assert result.passed


def test_m1_oracle_flags_recovery_skipping_edge_boundary():
    # OUT must recover UP to EDGE first (hysteresis); OUT->IN is a boundary
    # violation when recovery_to_edge is explicitly False.
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [2, 0],
        "recovered_to_edge": False,
        "stale_input_count": 0,
        "mrc_override_active": False,
    })
    assert not result.passed
    assert "M1_RECOVERY_SKIPPED_EDGE" in result.failed_checks


def test_m1_oracle_passes_out_to_edge_recovery_transition():
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [2, 1, 0],
        "recovered_to_edge": True,
        "stale_input_count": 0,
        "mrc_override_active": False,
    })
    assert result.passed


def test_m1_oracle_flags_mrc_override_regression_to_out():
    # MRC is the maximum-priority/terminal state; regressing to OUT after MRC
    # without a recovery edge is a priority violation.
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [3, 2],
        "mrc_override_active": True,
        "stale_input_count": 0,
    })
    assert not result.passed
    assert "M1_MRC_OVERRIDE_REGRESSION" in result.failed_checks
    assert result.evidence["post_mrc_states"] == [2]


def test_m1_oracle_passes_mrc_recovery_to_in():
    result = evaluate_m1_oracle(m1_output={
        "envelope_seq": [4, 1, 0],
        "mrc_override_active": True,
        "stale_input_count": 0,
    })
    assert result.passed


def test_m1_extract_reads_envelope_state_sequence_from_odd_state_trace():
    rows = [
        {"topic": "/l3/odd_state", "sim_t": 1.0, "envelope_state": 0,
         "conformance_score": 0.95},
        {"topic": "/l3/odd_state", "sim_t": 2.0, "envelope_state": 1,
         "conformance_score": 0.82},
        {"topic": "/l3/odd_state", "sim_t": 3.0, "envelope_state": 2,
         "conformance_score": 0.40},
        {"topic": "/l3/odd_state", "sim_t": 4.0, "envelope_state": 1,
         "conformance_score": 0.81},
    ]
    output = extract_m1_output(rows)
    assert output["envelope_seq"] == [0, 1, 2, 1]
    assert output["recovered_to_edge"] is True
    assert output["mrc_override_active"] is False


def test_m1_extract_reports_stale_input_degradation():
    rows = [
        {"topic": "/l3/odd_state", "sim_t": 1.0, "envelope_state": 0,
         "conformance_score": 0.95, "zone_reason": "normal"},
        {"topic": "/l3/odd_state", "sim_t": 2.0, "envelope_state": 1,
         "conformance_score": 0.70, "zone_reason": "M2 input stale"},
    ]
    output = extract_m1_output(rows)
    assert output["stale_input_count"] == 1
    assert output["score_degraded_on_stale"] is True


def test_m1_extract_returns_empty_when_no_odd_state_rows():
    assert extract_m1_output([]) == {}
    assert extract_m1_output([{"topic": "/l3/m2/world_state"}]) == {}


# ─── M3 Mission Manager oracle qualification (Task 4) ─────────────────────
# M3 is local mission tracking + replanning trigger. The oracle verifies the
# FSM lifecycle walk is valid, duplicate routes are rejected, and reset returns
# to IDLE — without judging route geometry (M2/M5 authority).


def test_m3_oracle_passes_nominal_lifecycle_walk():
    # INIT -> IDLE -> TASK_VALIDATION -> AWAITING_ROUTE -> ACTIVE
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1, 2, 3, 4],
        "task_validity_seq": [0, 0, 1, 1, 1],
    })
    assert result.passed
    assert result.failed_checks == []


def test_m3_oracle_flags_empty_fsm_trace():
    result = evaluate_m3_oracle(m3_output={})
    assert not result.passed
    assert "M3_FSM_TRACE_EMPTY" in result.failed_checks


def test_m3_oracle_flags_illegal_fsm_transition():
    # INIT -> ACTIVE is an illegal jump (must go through the lifecycle).
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 4],
    })
    assert not result.passed
    assert "M3_FSM_ILLEGAL_TRANSITION" in result.failed_checks
    assert result.evidence["illegal_jumps"] == [[0, 4]]


def test_m3_oracle_flags_invalid_fsm_state():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 99],
    })
    assert not result.passed
    assert "M3_FSM_INVALID_STATE" in result.failed_checks


def test_m3_oracle_flags_invalid_task_validity():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1],
        "task_validity_seq": [7],
    })
    assert not result.passed
    assert "M3_TASK_VALIDITY_INVALID" in result.failed_checks


def test_m3_oracle_flags_duplicate_route_not_rejected():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1],
        "duplicate_route_rejected": False,
    })
    assert not result.passed
    assert "M3_DUPLICATE_ROUTE_NOT_REJECTED" in result.failed_checks


def test_m3_oracle_passes_when_duplicate_route_rejected():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1],
        "duplicate_route_rejected": True,
    })
    assert result.passed


def test_m3_oracle_flags_reset_not_returning_to_idle():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1, 2],
        "reset_returned_to_idle": False,
    })
    assert not result.passed
    assert "M3_RESET_DID_NOT_RETURN_TO_IDLE" in result.failed_checks


def test_m3_oracle_passes_reset_returning_to_idle():
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [0, 1, 2, 1],
        "reset_returned_to_idle": True,
    })
    assert result.passed


def test_m3_oracle_allows_active_to_replan_wait_and_back():
    # ACTIVE -> REPLAN_WAIT -> ACTIVE (replan trigger cycle).
    result = evaluate_m3_oracle(m3_output={
        "fsm_seq": [4, 5, 4],
    })
    assert result.passed


def test_m3_extract_reads_fsm_sequence_from_mission_goal_trace():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 0,
         "task_validity": 0},
        {"topic": "/l3/mission_goal", "sim_t": 2.0, "fsm_state": 1,
         "task_validity": 0},
        {"topic": "/l3/mission_goal", "sim_t": 3.0, "fsm_state": 2,
         "task_validity": 1},
    ]
    output = extract_m3_output(rows)
    assert output["fsm_seq"] == [0, 1, 2]
    assert output["task_validity_seq"] == [0, 0, 1]


def test_m3_extract_reports_duplicate_route_rejection_from_asdr_record():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 1},
        {
            "topic": "/l3/asdr/record",
            "sim_t": 2.0,
            "source_module": "M3_Mission_Manager",
            "decision_type": "route_rejected",
            "decision_json": '{"reason":"duplicate_planned_route"}',
        },
    ]
    output = extract_m3_output(rows)
    assert output["duplicate_route_rejected"] is True


def test_m3_extract_reports_reset_returned_to_idle():
    rows = [
        {"topic": "/l3/mission_goal", "sim_t": 1.0, "fsm_state": 4},
        {
            "topic": "/l3/asdr/record",
            "sim_t": 2.0,
            "source_module": "M3_Mission_Manager",
            "decision_type": "mission_reset",
        },
        {"topic": "/l3/mission_goal", "sim_t": 3.0, "fsm_state": 1},
    ]
    output = extract_m3_output(rows)
    assert output["reset_returned_to_idle"] is True


def test_m3_extract_returns_empty_when_no_mission_goal_rows():
    assert extract_m3_output([]) == {}
    assert extract_m3_output([{"topic": "/l3/m2/world_state"}]) == {}


# ─── M4 Behavior Arbiter oracle qualification (Task 4) ────────────────────
# M4 is behavior arbitration. The oracle verifies the FSM stability contract:
# no premature recovery before rule release, and no chatter back to COLREG_AVOID
# after recovery without a new conflict onset.


def test_m4_oracle_passes_clean_release_before_recovery():
    # Avoidance, then M6 clears conflict, then RECOVERY — correct ordering.
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID", "closing_mps": 2.0},
            {"t": 200.0, "behavior": "RECOVERY", "closing_mps": -1.0},
        ],
        m6_conflict_cleared_t=199.0,
    )
    assert result.passed


def test_m4_oracle_passes_when_target_opening_at_release():
    # M4 recovery is physically correct when the target is opening even if M6
    # conflict_detected lags (release-latch hysteresis).
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID", "closing_mps": 2.0},
            {"t": 780.0, "behavior": "RECOVERY", "closing_mps": -0.5},
        ],
        m6_conflict_cleared_t=804.5,
    )
    assert result.passed
    assert result.evidence["closing_mps_at_release"] == -0.5


def test_m4_oracle_flags_premature_recovery_before_rule_release():
    # Target still closing (closing_mps>0) but M4 recovered before M6 cleared.
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID", "closing_mps": 2.0},
            {"t": 779.6, "behavior": "RECOVERY", "closing_mps": 0.8},
        ],
        m6_conflict_cleared_t=804.5,
    )
    assert not result.passed
    assert "PREMATURE_RECOVERY_BEFORE_RULE_RELEASE" in result.failed_checks


def test_m4_oracle_flags_no_recovery_when_avoidance_without_release():
    # Avoidance present but no RECOVERY transition at all (only meaningful when
    # avoidance happened — the chatter check fires on post-recovery return).
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID"},
            {"t": 200.0, "behavior": "RECOVERY"},
            {"t": 201.0, "behavior": "COLREG_AVOID"},
        ],
        m6_conflict_cleared_t=199.0,
    )
    assert not result.passed
    assert "RECOVERY_CHATTER_AFTER_RELEASE" in result.failed_checks


def test_m4_oracle_passes_no_avoidance_transit_only():
    # No avoidance episode -> no recovery expectations, clean pass.
    result = evaluate_m4_oracle(
        m4_events=[{"t": 100.0, "behavior": "TRANSIT"}],
        m6_conflict_cleared_t=None,
    )
    assert result.passed


def test_m4_oracle_passes_single_avoid_to_recovery_no_chatter():
    # The qualification contract from brief Step 3: one AVOID->RECOVERY, no
    # return to AVOID. Two sustained RECOVERY samples after release.
    result = evaluate_m4_oracle(
        m4_events=[
            {"t": 100.0, "behavior": "COLREG_AVOID", "closing_mps": -1.0},
            {"t": 200.0, "behavior": "RECOVERY", "closing_mps": -1.0},
            {"t": 201.0, "behavior": "RECOVERY", "closing_mps": -1.0},
            {"t": 202.0, "behavior": "RECOVERY", "closing_mps": -1.0},
        ],
        m6_conflict_cleared_t=199.0,
    )
    assert result.passed
    assert "RECOVERY_CHATTER_AFTER_RELEASE" not in result.failed_checks


# ─── Task 11: split checker / MRM-authority / execution evidence ──────────
# The three MRM-related roles must be SEPARATE observable contracts in the
# trace/oracle so attribution never conflates them:
#   1. M7 checker evidence   — alerts / recommendations / veto
#   2. M1 MRM authority      — recommendation parsing, feasibility selection,
#                              command publication, generation/identity
#   3. L4/GNC execution      — command acceptance, EXECUTING, COMPLETED/REJECTED
# Each role is exercised end-to-end via evaluate_module_oracles(rows), which
# consumes raw trace rows and returns one OracleResult per role.
#
# Row schemas mirror the trace writer record() payloads (test_sil_trace_writer
# pins the field contracts). These are MRM-chain fixtures only; they do NOT
# exercise M2/M3/M6 geometry, so those oracles are skipped here.


_SAFETY_ALERT_TOPIC = "/l3/m7/safety_alert"
_MRM_COMMAND_TOPIC = "/l3/m1/mrm_command"
_MRM_EXEC_STATUS_TOPIC = "/l3/m1/mrm_execution_status"
_BEHAVIOR_PLAN_TOPIC = "/l3/m4/behavior_plan"
_OVERRIDE_TOPIC = "/l3/m5/reactive_override_cmd"
_CHECKER_VETO_TOPIC = "/l3/checker/veto"

# MRM severity used to mark an alert as MRC-required (SafetyAlert severity).
# Must match SafetyAlert.msg SEVERITY_MRC_REQUIRED=3 (max enum value).
_MRC_REQUIRED_SEVERITY = 3

# MRMCommand.mrm_type enum (MRMCommand.msg): NONE=0, SAFE_SPEED_HOLD=1,
# STOP=2, EMERGENCY_TURN=3, ANCHOR=4.
MRM_TYPE_BY_ID = {
    "MRM-01-SAFE-SPEED-HOLD": 1,
    "MRM-02-STOP": 2,
    "MRM-03-EMERGENCY-TURN": 3,
    "MRM-04-ANCHOR": 4,
}


def m7_mrc_required_rows(
    *,
    sim_t: float = 100.0,
    recommendation: str = "MRM-02-STOP",
    severity: int = _MRC_REQUIRED_SEVERITY,
) -> list[dict]:
    """M7 SafetyAlert requesting an MRM (MRC request) at sim_t.

    M7 only *recommends*; it never publishes an executable command. A
    non-empty canonical recommended_mrm with MRC-level severity is the signal
    the M1 authority must answer with a command.
    """
    return [{
        "topic": _SAFETY_ALERT_TOPIC,
        "sim_t": sim_t,
        "alert_type": 2,
        "severity": severity,
        "recommended_mrm": recommendation,
        "confidence": 0.9,
    }]


def m1_mrm_command(
    command_id: str = "mrm-1",
    mrm_id: str = "MRM-01-SAFE-SPEED-HOLD",
    *,
    sim_t: float = 101.0,
    generation: int = 1,
    publisher: str = "M1",
) -> dict:
    """An M1-authorized MRMCommand published on /l3/m1/mrm_command."""
    return {
        "topic": _MRM_COMMAND_TOPIC,
        "sim_t": sim_t,
        "publisher": publisher,
        "command_id": command_id,
        "command_generation": generation,
        "mrm_type": MRM_TYPE_BY_ID.get(mrm_id, 1),
        "mrm_id": mrm_id,
        "trigger_alert_key": "alert-key-1",
        "target_speed_kn": 3.0,
        "heading_delta_deg": 0.0,
        "validity_s": 30.0,
        "confidence": 0.95,
        "rationale": "M1-authorized MRM (feasible)",
    }


def m1_mrm_execution_status(
    *,
    command_id: str = "mrm-1",
    state: int = 2,
    command_source: str = "MRM",
    sim_t: float = 102.0,
    generation: int = 1,
) -> dict:
    """MRMExecutionStatus on /l3/m1/mrm_execution_status (GNC executor ack).

    state enum (MRMExecutionStatus.msg): ACCEPTED=1, EXECUTING=2, COMPLETED=3,
    REJECTED=4, STALE_HOLDING=5.
    """
    return {
        "topic": _MRM_EXEC_STATUS_TOPIC,
        "sim_t": sim_t,
        "command_id": command_id,
        "command_generation": generation,
        "state": state,
        "command_source": command_source,
        "reason": "tracking",
        "confidence": 0.9,
        "rationale": "GNC executor acknowledgement",
    }


def _m4_mrm_telemetry(
    *,
    behavior: int = 4,
    active_mrm_type: int = 1,
    active_mrm_command_id: str = "mrm-1",
    active_mrm_generation: int = 1,
    sim_t: float = 101.5,
) -> dict:
    return {
        "topic": _BEHAVIOR_PLAN_TOPIC,
        "sim_t": sim_t,
        "behavior": behavior,
        "active_mrm_type": active_mrm_type,
        "active_mrm_command_id": active_mrm_command_id,
        "active_mrm_generation": active_mrm_generation,
        "rationale": "M4 mirrors M1 MRM telemetry",
    }


def complete_mrm_rows(
    *,
    mrm_id: str = "MRM-01-SAFE-SPEED-HOLD",
    m4_behavior: int = 4,
    m4_active_mrm_type: int | None = None,
    exec_state: int = 3,
    exec_command_source: str = "MRM",
    status: str = "COMPLETED",
) -> list[dict]:
    """A complete, healthy MRM authority + execution chain end-to-end.

    M7 recommends -> M1 commands -> M4 mirrors telemetry -> GNC acknowledges.
    Used as the green baseline; individual tests corrupt one link to prove the
    attribution is stage-separated.
    """
    if m4_active_mrm_type is None:
        m4_active_mrm_type = MRM_TYPE_BY_ID.get(mrm_id, 1)
    cmd_id = "mrm-1"
    state_map = {"ACCEPTED": 1, "EXECUTING": 2, "COMPLETED": 3,
                 "REJECTED": 4, "STALE_HOLDING": 5}
    return [
        *m7_mrc_required_rows(recommendation=mrm_id),
        m1_mrm_command(cmd_id, mrm_id),
        _m4_mrm_telemetry(
            behavior=m4_behavior,
            active_mrm_type=m4_active_mrm_type,
            active_mrm_command_id=cmd_id,
        ),
        m1_mrm_execution_status(
            command_id=cmd_id,
            state=state_map.get(status, exec_state),
            command_source=exec_command_source,
        ),
    ]


def unsafe_command_rows() -> list[dict]:
    """An executable MRM command published with NO M7 recommendation.

    The command itself is authoritative once M1 publishes it, but M7 (the
    checker) never flagged the situation — that is an M7 checker RED, because
    the checker must independently surface the hazard it authorized a response to.
    """
    return [m1_mrm_command("mrm-1", "MRM-01-SAFE-SPEED-HOLD")]


def m5_reactive_override(sim_t: float = 102.5) -> dict:
    """An M5 reactive override publication during an active external MRM."""
    return {
        "topic": _OVERRIDE_TOPIC,
        "sim_t": sim_t,
        "trigger_reason": "CPA_EMERGENCY",
        "heading_cmd_deg": 53.2,
        "speed_cmd_kn": 6.0,
        "rot_cmd_deg_s": 4.7,
        "validity_s": 2.0,
        "confidence": 0.9,
    }


def test_unsafe_command_without_m7_recommendation_is_m7_red():
    result = evaluate_module_oracles(unsafe_command_rows())
    assert result["M7"].failures == ("UNSAFE_COMMAND_WITHOUT_M7_RECOMMENDATION",)


def test_m7_mrc_request_without_m1_command_is_m1_red():
    rows = m7_mrc_required_rows(recommendation="MRM-04-ANCHOR")
    result = evaluate_module_oracles(rows)
    assert result["M7"].passed
    assert "MRC_REQUEST_WITHOUT_M1_COMMAND" in result["M1"].failures


def test_m1_command_without_matching_gnc_status_is_l4_red():
    rows = [
        *m7_mrc_required_rows(),
        m1_mrm_command("mrm-1", "MRM-01-SAFE-SPEED-HOLD"),
    ]
    result = evaluate_module_oracles(rows)
    assert result["M1"].passed
    assert "M1_MRM_COMMAND_WITHOUT_GNC_ACK" in result["L4_GNC"].failures


def test_rejected_status_is_not_execution_closure():
    rows = complete_mrm_rows(status="REJECTED")
    result = evaluate_module_oracles(rows)
    assert "M1_MRM_COMMAND_REJECTED" in result["L4_GNC"].failures


def test_m1_command_and_m4_mrm_telemetry_must_match():
    rows = complete_mrm_rows(
        mrm_id="MRM-02-STOP",
        m4_behavior=4,
        m4_active_mrm_type="MRM-01-SAFE-SPEED-HOLD",
    )
    result = evaluate_module_oracles(rows)
    assert "M1_MRM_TELEMETRY_MISMATCH" in result["M4"].failures


def test_m5_must_not_publish_while_external_mrm_is_active():
    rows = complete_mrm_rows(
        mrm_id="MRM-01-SAFE-SPEED-HOLD",
        m4_behavior=4,
        m4_active_mrm_type="MRM-01-SAFE-SPEED-HOLD",
    )
    rows.append(m5_reactive_override(sim_t=rows[-1]["sim_t"] + 0.1))
    result = evaluate_module_oracles(rows)
    assert "M5_COMMAND_DURING_EXTERNAL_MRM" in result["M5"].failures


def test_complete_mrm_chain_is_all_green():
    rows = complete_mrm_rows(status="COMPLETED")
    result = evaluate_module_oracles(rows)
    assert result["M7"].passed
    assert result["M1"].passed
    assert result["M4"].passed
    assert result["M5"].passed
    assert result["L4_GNC"].passed


def test_healthy_run_with_no_mrc_request_needs_no_command():
    # No critical/MRC-required alert -> M1 is not required to publish a command.
    result = evaluate_module_oracles([])
    assert result["M7"].passed
    assert result["M1"].passed
    assert result["L4_GNC"].passed


def _foreign_topic_mrm_command(*, topic: str, sim_t: float = 101.0) -> dict:
    """An executable MRMCommand row that appears on a NON-M1 topic.

    The M1-sole-publisher invariant is structural: only /l3/m1/mrm_command carries
    an M1-authored executable command. An MRMCommand-shaped payload on any other
    /l3/ topic (e.g. a fabricated /l3/m7/mrm_command, or an M7 safety_alert row
    that smuggles a full executable command payload) is an M7-authored command —
    that is RED. The command fields are MRMCommand-shaped (command_id/mrm_type/
    mrm_id/trigger_alert_key) so the row is recognizably an executable command,
    not a recommendation.
    """
    return {
        "topic": topic,
        "sim_t": sim_t,
        "command_id": "foreign-mrm-1",
        "command_generation": 1,
        "mrm_type": 2,  # STOP
        "mrm_id": "MRM-02-STOP",
        "trigger_alert_key": "alert-key-1",
        "target_speed_kn": 0.0,
        "heading_delta_deg": 0.0,
        "validity_s": 30.0,
        "confidence": 0.9,
        "rationale": "M7-authored executable command (violation)",
    }


def test_non_m1_topic_executable_command_is_red():
    """An executable MRM command published on a non-M1 topic is RED.

    Regression test for the dead-code NON_M1_MRM_PUBLISHER check: previously the
    oracle read non-existent ``publisher``/``command_source`` fields (MRMCommand.msg
    has neither) with AND-logic, so the check could never fire from any trace.
    Topic provenance is the structural signal — only /l3/m1/mrm_command is an
    M1-authored executable command.
    """
    rows = [
        *m7_mrc_required_rows(recommendation="MRM-02-STOP"),
        _foreign_topic_mrm_command(topic="/l3/m7/mrm_command", sim_t=101.0),
    ]
    result = evaluate_module_oracles(rows)
    assert "NON_M1_MRM_PUBLISHER" in result["M1"].failures


def test_non_m1_topic_executable_command_on_safety_alert_alias_is_red():
    """Even an M7 safety_alert topic carrying a full executable command is RED.

    A fabricated M7-authored executable command appearing on /l3/m7/safety_alert
    must still be detected as a NON_M1_MRM_PUBLISHER violation, not silently
    whitelisted because the topic name contains 'm7'.
    """
    rows = [
        *m7_mrc_required_rows(recommendation="MRM-02-STOP"),
        _foreign_topic_mrm_command(topic="/l3/m7/safety_alert", sim_t=101.0),
    ]
    result = evaluate_module_oracles(rows)
    assert "NON_M1_MRM_PUBLISHER" in result["M1"].failures


def test_m1_topic_executable_command_is_not_false_positive():
    """A healthy /l3/m1/mrm_command row must NOT trigger NON_M1_MRM_PUBLISHER."""
    rows = [
        *m7_mrc_required_rows(recommendation="MRM-02-STOP"),
        m1_mrm_command("mrm-1", "MRM-02-STOP"),
    ]
    result = evaluate_module_oracles(rows)
    assert "NON_M1_MRM_PUBLISHER" not in result["M1"].failures


# ─── Task 11: oracle-level unit tests (stage separation proof) ──────────────
# Each oracle is independently drivable from its evidence dict, proving the
# three roles do not share state or conflate attribution.


def test_m7_checker_oracle_flags_command_without_recommendation():
    result = evaluate_m7_oracle(m7_output={
        "command_without_recommendation": True,
    })
    assert "UNSAFE_COMMAND_WITHOUT_M7_RECOMMENDATION" in result.failures


def test_m7_checker_oracle_flags_safe_command_wrongly_vetoed():
    result = evaluate_m7_oracle(m7_output={
        "safe_command_wrongly_vetoed": True,
    })
    assert "SAFE_COMMAND_WRONGLY_VETOED" in result.failures


def test_m7_checker_oracle_passes_healthy_run():
    result = evaluate_m7_oracle(m7_output={
        "command_without_recommendation": False,
        "safe_command_wrongly_vetoed": False,
    })
    assert result.passed


def test_m1_authority_oracle_flags_mrc_request_without_command():
    result = evaluate_m1_mrm_authority_oracle(m1_mrm_output={
        "mrc_requested": True,
        "command_published": False,
    })
    assert "MRC_REQUEST_WITHOUT_M1_COMMAND" in result.failures


def test_m1_authority_oracle_flags_non_m1_publisher():
    result = evaluate_m1_mrm_authority_oracle(m1_mrm_output={
        "mrc_requested": True,
        "command_published": True,
        "non_m1_publisher": True,
    })
    assert "NON_M1_MRM_PUBLISHER" in result.failures


def test_m1_authority_oracle_flags_command_id_churn():
    result = evaluate_m1_mrm_authority_oracle(m1_mrm_output={
        "mrc_requested": True,
        "command_published": True,
        "command_id_churn": True,
    })
    assert "M1_COMMAND_ID_CHURN" in result.failures


def test_m1_authority_oracle_flags_taxonomy_mismatch():
    result = evaluate_m1_mrm_authority_oracle(m1_mrm_output={
        "mrc_requested": True,
        "command_published": True,
        "taxonomy_mismatch": True,
    })
    assert "MRM_TAXONOMY_MISMATCH" in result.failures


def test_l4_gnc_oracle_flags_command_without_gnc_ack():
    result = evaluate_l4_gnc_oracle(l4_gnc_output={
        "command_active": True,
        "gnc_acknowledged": False,
    })
    assert "M1_MRM_COMMAND_WITHOUT_GNC_ACK" in result.failures


def test_l4_gnc_oracle_flags_rejected_status():
    result = evaluate_l4_gnc_oracle(l4_gnc_output={
        "command_active": True,
        "gnc_acknowledged": True,
        "rejected": True,
    })
    assert "M1_MRM_COMMAND_REJECTED" in result.failures


def test_l4_gnc_oracle_passes_completed_chain():
    result = evaluate_l4_gnc_oracle(l4_gnc_output={
        "command_active": True,
        "gnc_acknowledged": True,
        "completed": True,
    })
    assert result.passed
