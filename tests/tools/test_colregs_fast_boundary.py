"""FAST lifecycle boundary state machine tests.

The boundary detector replaces the legacy window-count stop. It observes the
M4 behavior_plan lifecycle: a stable TRANSIT prefix (>=60s), the first
COLREG_AVOID (behavior=1) sample, and then stops at the FIRST M4 RECOVERY
transition (behavior=7), including that boundary row.

Contract:
- Only behavior=1 (COLREG_AVOID) counts as AVOIDANCE.
- DP_HOLD (2), BERTH (3), MRC {4,5,6} are never avoidance aliases.
- Any MRC before RECOVERY terminates the run early with a specific RED.
- Recovery without prior avoidance is RED AVOIDANCE_NOT_REACHED.
- Timeout (no first recovery) is RED RECOVERY_BOUNDARY_NOT_REACHED.
- Only FIRST_M4_RECOVERY is a PASS boundary; it is immutable to later chatter.
"""

from tools.sil.colregs_fast_boundary import find_fast_boundary


def m4(t: float, behavior: int) -> dict:
    return {"topic": "/l3/m4/behavior_plan", "sim_t": t, "behavior": behavior}


def test_boundary_requires_transit_avoidance_then_first_recovery():
    rows = [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(210.0, 1), m4(220.0, 7)]
    result = find_fast_boundary(rows)
    assert result.ready
    assert result.transit_start_t == 0.0
    assert result.avoidance_start_t == 90.0
    assert result.recovery_start_t == 220.0
    assert result.stop_t == 220.0
    assert result.stop_reason == "FIRST_M4_RECOVERY"


def test_boundary_ignores_recovery_without_prior_avoidance():
    result = find_fast_boundary([m4(0.0, 0), m4(70.0, 0), m4(80.0, 7)])
    assert not result.ready
    assert result.failure_code == "AVOIDANCE_NOT_REACHED"


def test_boundary_rejects_short_transit_prefix():
    result = find_fast_boundary([m4(0.0, 0), m4(20.0, 1), m4(80.0, 7)])
    assert not result.ready
    assert result.failure_code == "TRANSIT_PREFIX_TOO_SHORT"


def test_boundary_waits_when_avoidance_has_no_recovery():
    result = find_fast_boundary([m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(500.0, 1)])
    assert not result.ready
    assert result.failure_code == "RECOVERY_BOUNDARY_NOT_REACHED"


def test_first_recovery_is_immutable_when_later_chatter_exists():
    rows = [
        m4(0.0, 0), m4(70.0, 0), m4(90.0, 1),
        m4(220.0, 7), m4(221.0, 1), m4(222.0, 7),
    ]
    assert find_fast_boundary(rows).stop_t == 220.0


def test_dp_hold_never_counts_as_colregs_avoidance():
    result = find_fast_boundary([
        m4(0.0, 0), m4(70.0, 0), m4(90.0, 2), m4(100.0, 7),
    ])
    assert not result.ready
    assert result.terminal
    assert result.failure_code == "NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE"


def test_mrc_after_avoidance_stops_early_as_red():
    result = find_fast_boundary([
        m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(120.0, 4),
    ])
    assert not result.ready
    assert result.terminal
    assert result.stop_t == 120.0
    assert result.failure_code == "MRC_BEFORE_RECOVERY"


def test_transit_return_without_recovery_is_red():
    result = find_fast_boundary([
        m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(120.0, 0),
    ])
    assert result.terminal
    assert result.failure_code == "AVOIDANCE_ABORTED_BEFORE_RECOVERY"


# --- Additional boundary-state-machine proof tests ----------------------------


def test_terminal_means_a_stop_t_was_reached():
    """Terminal boundaries (PASS or RED) carry a stop_t; timeouts do not."""
    ready = find_fast_boundary(
        [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(220.0, 7)]
    )
    assert ready.terminal is True
    assert ready.ready is True

    timeout = find_fast_boundary(
        [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(500.0, 1)]
    )
    assert timeout.terminal is False
    assert timeout.ready is False


def test_only_first_recovery_is_a_pass_boundary():
    """FIRST_M4_RECOVERY is the sole ready/PASS boundary."""
    rows = [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(220.0, 7)]
    result = find_fast_boundary(rows)
    assert result.ready
    assert result.stop_reason == "FIRST_M4_RECOVERY"
    assert result.failure_code is None
    assert result.transit_duration_s == 90.0


def test_transit_duration_is_zero_until_avoidance_observed():
    no_avoid = find_fast_boundary([m4(0.0, 0), m4(70.0, 0)])
    assert no_avoid.transit_duration_s == 0.0


def test_only_colreg_avoid_behavior_counts_as_avoidance():
    """DP_HOLD(2) and BERTH(3) before avoidance are non-COLREGs, not avoidance."""
    for non_avoid in (2, 3):
        result = find_fast_boundary(
            [m4(0.0, 0), m4(70.0, 0), m4(90.0, non_avoid)]
        )
        assert result.terminal
        assert result.failure_code == "NON_COLREGS_BEHAVIOR_BEFORE_AVOIDANCE"


def test_mrc_behaviors_before_recovery_all_terminate_early():
    """Each coarse MRC behavior 4/5/6 after avoidance is an early RED."""
    for mrc_behavior in (4, 5, 6):
        result = find_fast_boundary(
            [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(120.0, mrc_behavior)]
        )
        assert result.terminal
        assert result.failure_code == "MRC_BEFORE_RECOVERY"


def test_non_colregs_during_avoidance_interrupts_before_recovery():
    result = find_fast_boundary(
        [m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(120.0, 2)]
    )
    assert result.terminal
    assert result.failure_code == "NON_COLREGS_BEHAVIOR_INTERRUPTED_AVOIDANCE"


def test_detector_ignores_non_m4_topics():
    rows = [
        {"topic": "/l3/m2/world_state", "sim_t": 0.0, "behavior": 0},
        m4(0.0, 0), m4(70.0, 0), m4(90.0, 1), m4(220.0, 7),
        {"topic": "/sil/own_ship_state", "sim_t": 95.0, "behavior": 1},
    ]
    result = find_fast_boundary(rows)
    assert result.ready
    assert result.stop_t == 220.0


def test_detector_orders_events_by_canonical_time():
    """Events are sorted by canonical event time regardless of list order."""
    rows = [
        m4(220.0, 7), m4(0.0, 0), m4(90.0, 1), m4(70.0, 0),
    ]
    result = find_fast_boundary(rows)
    assert result.ready
    assert result.avoidance_start_t == 90.0
    assert result.stop_t == 220.0


def test_min_transit_s_threshold_is_configurable():
    rows = [m4(0.0, 0), m4(20.0, 0), m4(30.0, 1), m4(80.0, 7)]
    # Default 60s: 20s transit prefix is too short.
    assert find_fast_boundary(rows).failure_code == "TRANSIT_PREFIX_TOO_SHORT"
    # Lower threshold: 20s prefix is acceptable.
    result = find_fast_boundary(rows, min_transit_s=15.0)
    assert result.ready
    assert result.avoidance_start_t == 30.0


def m6(t: float, conflict: bool) -> dict:
    return {
        "topic": "/l3/m6/colregs_constraint",
        "sim_t": t,
        "conflict_detected": conflict,
    }


def test_target_resolves_boundary_is_clear_dwell_with_own_hold():
    rows = [
        m4(0.0, 0),
        m4(70.0, 0),
        m6(90.0, True),
        m4(180.0, 0),
        m6(200.0, False),
        m6(211.0, False),
    ]
    result = find_fast_boundary(
        rows,
        terminal="ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    )
    assert result.ready
    assert result.avoidance_start_t is None
    assert result.recovery_start_t == 200.0
    assert result.stop_t == 211.0
    assert result.stop_reason == "ENCOUNTER_CLEAR_WITH_OWN_HOLD"


def test_target_resolves_rejects_own_tactical_takeover():
    rows = [m4(0.0, 0), m4(70.0, 0), m6(90.0, True), m4(100.0, 1)]
    result = find_fast_boundary(
        rows,
        terminal="ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    )
    assert result.terminal
    assert result.failure_code == "OWN_TACTICAL_TAKEOVER_FOR_TARGET_RESOLUTION"


def test_target_resolves_times_out_without_clear_dwell():
    rows = [m4(0.0, 0), m4(70.0, 0), m6(90.0, True), m6(200.0, False)]
    result = find_fast_boundary(
        rows,
        terminal="ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    )
    assert not result.terminal
    assert result.failure_code == "ENCOUNTER_CLEAR_NOT_REACHED"


def test_target_resolves_can_clear_before_m6_conflict_latch_arms():
    rows = [
        m4(0.0, 0),
        m4(70.0, 0),
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 90.0,
            "conflict_detected": False,
            "active_rules": [{"rule_id": 13, "role": 0}],
            "encounter_state": 1,
        },
        m6(200.0, False),
        m6(211.0, False),
    ]
    result = find_fast_boundary(
        rows,
        terminal="ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    )
    assert result.ready
    assert result.stop_t == 211.0


def test_target_resolves_does_not_fail_when_target_acts_during_own_hold_prefix():
    rows = [
        m4(0.0, 0),
        m6(10.0, True),
        m6(20.0, False),
        m4(60.0, 0),
        m6(61.0, False),
    ]

    result = find_fast_boundary(
        rows,
        terminal="ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    )

    assert result.ready
    assert result.stop_reason == "ENCOUNTER_CLEAR_WITH_OWN_HOLD"
    assert result.stop_t == 61.0


def test_empty_rows_is_avoidance_not_reached():
    result = find_fast_boundary([])
    assert not result.ready
    assert not result.terminal
    assert result.failure_code == "AVOIDANCE_NOT_REACHED"


def test_exactly_sixty_second_transit_prefix_is_accepted():
    rows = [m4(0.0, 0), m4(60.0, 0), m4(60.0, 1), m4(200.0, 7)]
    result = find_fast_boundary(rows)
    assert result.ready
    assert result.transit_duration_s == 60.0
