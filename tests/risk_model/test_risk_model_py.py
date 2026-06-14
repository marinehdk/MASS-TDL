from math import pi

import pytest

from l3_risk_model import (
    ColregsDuty,
    OwnShipInput,
    RankingState,
    RiskPhase,
    TargetInput,
    danger_axes,
    evaluate_target,
    select_primary,
)


def default_ownship(sog_mps: float = 5.0) -> OwnShipInput:
    return OwnShipInput(
        x_m=0.0,
        y_m=0.0,
        heading_rad=0.0,
        sog_mps=sog_mps,
        loa_m=46.0,
        confidence=0.95,
        odd_degraded=False,
    )


def test_forward_danger_fixture_is_critical_with_danger_ddv() -> None:
    risk = evaluate_target(
        default_ownship(),
        TargetInput(
            id="TS001",
            x_m=280.0,
            y_m=0.0,
            cog_rad=pi,
            sog_mps=5.0,
            cpa_m=120.0,
            tcpa_s=45.0,
            confidence=0.9,
        ),
        ColregsDuty.GIVE_WAY,
    )

    assert risk.risk_phase == RiskPhase.CRITICAL
    assert risk.danger_ddv > 0.0


def test_starboard_warning_margin_is_tighter_than_mirrored_port_target() -> None:
    own = default_ownship()

    starboard = evaluate_target(
        own,
        TargetInput(
            id="starboard",
            x_m=500.0,
            y_m=260.0,
            cog_rad=1.5 * pi,
            sog_mps=6.0,
            cpa_m=220.0,
            tcpa_s=180.0,
            confidence=0.9,
        ),
        ColregsDuty.GIVE_WAY,
    )
    port = evaluate_target(
        own,
        TargetInput(
            id="port",
            x_m=500.0,
            y_m=-260.0,
            cog_rad=0.5 * pi,
            sog_mps=6.0,
            cpa_m=220.0,
            tcpa_s=180.0,
            confidence=0.9,
        ),
        ColregsDuty.GIVE_WAY,
    )

    assert starboard.warning_margin_m < port.warning_margin_m


def test_opening_outside_warning_returns_clear() -> None:
    risk = evaluate_target(
        default_ownship(),
        TargetInput(
            id="TS003",
            x_m=-800.0,
            y_m=-500.0,
            cog_rad=pi,
            sog_mps=3.0,
            cpa_m=900.0,
            tcpa_s=-1.0,
            confidence=0.9,
        ),
        ColregsDuty.FREE,
    )

    assert risk.risk_phase == RiskPhase.CLEAR


def test_closing_speed_sign_distinguishes_closing_and_opening() -> None:
    own = default_ownship()
    closing = evaluate_target(
        own,
        TargetInput(
            id="closing",
            x_m=600.0,
            y_m=0.0,
            cog_rad=pi,
            sog_mps=4.0,
            cpa_m=100.0,
            tcpa_s=120.0,
            confidence=1.0,
        ),
        ColregsDuty.FREE,
    )
    opening = evaluate_target(
        own,
        TargetInput(
            id="opening",
            x_m=600.0,
            y_m=0.0,
            cog_rad=0.0,
            sog_mps=8.0,
            cpa_m=100.0,
            tcpa_s=-1.0,
            confidence=1.0,
        ),
        ColregsDuty.FREE,
    )

    assert closing.closing_speed_mps > 0.0
    assert opening.closing_speed_mps < 0.0


def test_negative_speed_is_treated_as_zero() -> None:
    own_negative = default_ownship(sog_mps=-5.0)
    own_zero = default_ownship(sog_mps=0.0)
    target_negative = TargetInput(
        id="negative",
        x_m=450.0,
        y_m=90.0,
        cog_rad=pi,
        sog_mps=-3.0,
        cpa_m=200.0,
        tcpa_s=120.0,
        confidence=0.8,
    )
    target_zero = TargetInput(
        id="zero",
        x_m=450.0,
        y_m=90.0,
        cog_rad=pi,
        sog_mps=0.0,
        cpa_m=200.0,
        tcpa_s=120.0,
        confidence=0.8,
    )

    negative_risk = evaluate_target(own_negative, target_negative, ColregsDuty.FREE)
    zero_risk = evaluate_target(own_zero, target_zero, ColregsDuty.FREE)

    assert danger_axes(own_negative) == danger_axes(own_zero)
    assert negative_risk.closing_speed_mps == pytest.approx(zero_risk.closing_speed_mps)
    assert negative_risk.risk_score == pytest.approx(zero_risk.risk_score)


def test_zero_range_has_full_ddv_and_negative_margins() -> None:
    risk = evaluate_target(
        default_ownship(),
        TargetInput(
            id="zero_range",
            x_m=0.0,
            y_m=0.0,
            cog_rad=0.0,
            sog_mps=0.0,
            cpa_m=0.0,
            tcpa_s=0.0,
            confidence=1.0,
        ),
        ColregsDuty.FREE,
    )

    assert risk.warning_ddv == pytest.approx(1.0)
    assert risk.danger_ddv == pytest.approx(1.0)
    assert risk.warning_margin_m < 0.0
    assert risk.danger_margin_m < 0.0


def test_ranking_is_deterministic_for_reversed_target_order() -> None:
    own = default_ownship()
    first = evaluate_target(
        own,
        TargetInput("alpha", 420.0, 0.0, pi, 4.0, 100.0, 60.0, 1.0),
        ColregsDuty.GIVE_WAY,
    )
    second = evaluate_target(
        own,
        TargetInput("zulu", 420.0, 0.0, pi, 4.0, 100.0, 60.0, 1.0),
        ColregsDuty.GIVE_WAY,
    )

    assert select_primary([first, second], RankingState()).target_id == "alpha"
    assert select_primary([second, first], RankingState()).target_id == "alpha"
