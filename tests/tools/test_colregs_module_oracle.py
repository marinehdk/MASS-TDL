from tools.sil.colregs_module_oracle import (
    evaluate_l4_oracle,
    evaluate_m5_oracle,
    evaluate_m6_oracle,
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


def test_m5_oracle_allows_empty_plan_when_no_action_required():
    result = evaluate_m5_oracle(
        plan_output={"solver_status": "EMPTY", "n_waypoints": 0},
        plan_required=False,
    )

    assert result.passed
    assert "NO_FEASIBLE_PLAN" not in result.failed_checks


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
