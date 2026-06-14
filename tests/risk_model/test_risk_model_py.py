from math import pi

import pytest

from l3_risk_model import (
    ColregsDuty,
    OwnShipInput,
    RankingConfig,
    RankingState,
    RiskPhase,
    RiskVector,
    RunRiskSummary,
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


def ranked_target(
    target_id: str,
    phase: RiskPhase,
    score: float,
    tcpa_s: float,
    range_m: float,
) -> RiskVector:
    return RiskVector(
        target_id=target_id,
        risk_phase=phase,
        risk_score=score,
        tcpa_s=tcpa_s,
        range_m=range_m,
    )


def test_public_inputs_default_construct_like_cpp_structs() -> None:
    own = OwnShipInput()
    target = TargetInput()

    assert own.x_m == pytest.approx(0.0)
    assert own.y_m == pytest.approx(0.0)
    assert own.heading_rad == pytest.approx(0.0)
    assert own.sog_mps == pytest.approx(0.0)
    assert own.loa_m == pytest.approx(46.0)
    assert own.confidence == pytest.approx(1.0)
    assert own.odd_degraded is False

    assert target.id == ""
    assert target.x_m == pytest.approx(0.0)
    assert target.y_m == pytest.approx(0.0)
    assert target.cog_rad == pytest.approx(0.0)
    assert target.sog_mps == pytest.approx(0.0)
    assert target.cpa_m == pytest.approx(0.0)
    assert target.tcpa_s == pytest.approx(0.0)
    assert target.confidence == pytest.approx(1.0)


def test_run_risk_summary_defaults_and_risks_factory() -> None:
    summary = RunRiskSummary()

    assert summary.primary_threat_id == ""
    assert summary.primary_threat_switches == 0
    assert summary.max_risk_score == pytest.approx(0.0)
    assert summary.worst_warning_margin_m == pytest.approx(0.0)
    assert summary.worst_danger_margin_m == pytest.approx(0.0)
    assert summary.max_warning_ddv == pytest.approx(0.0)
    assert summary.max_danger_ddv == pytest.approx(0.0)
    assert summary.warning_domain_exposure_s == pytest.approx(0.0)
    assert summary.danger_domain_exposure_s == pytest.approx(0.0)
    assert summary.encounter_complexity_score == pytest.approx(0.0)
    assert summary.risks == []

    summary.risks.append(RiskVector(target_id="one"))

    assert RunRiskSummary().risks == []


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


def test_score_gap_hysteresis_holds_first_sample_then_switches() -> None:
    state = RankingState(previous_primary_id="previous", has_previous_primary=True)
    risks = [
        ranked_target("previous", RiskPhase.WARNING, 0.80, 60.0, 200.0),
        ranked_target("candidate", RiskPhase.WARNING, 0.91, 30.0, 150.0),
    ]

    first = select_primary(risks, state)

    assert first.target_id == "previous"
    assert state.previous_primary_id == "previous"
    assert state.has_candidate_primary is True
    assert state.candidate_primary_id == "candidate"
    assert state.candidate_count == 1

    second = select_primary(risks, state)

    assert second.target_id == "candidate"
    assert state.previous_primary_id == "candidate"
    assert state.has_previous_primary is True
    assert state.has_candidate_primary is False
    assert state.candidate_primary_id == ""
    assert state.candidate_count == 0


def test_score_gap_equal_threshold_switches_immediately() -> None:
    state = RankingState(
        previous_primary_id="previous",
        candidate_primary_id="stale",
        candidate_count=1,
        has_previous_primary=True,
        has_candidate_primary=True,
    )
    risks = [
        ranked_target("previous", RiskPhase.WARNING, 0.80, 60.0, 200.0),
        ranked_target("candidate", RiskPhase.WARNING, 0.92, 30.0, 150.0),
    ]

    selected = select_primary(risks, state, RankingConfig(switch_score_gap=0.12))

    assert selected.target_id == "candidate"
    assert state.previous_primary_id == "candidate"
    assert state.has_candidate_primary is False
    assert state.candidate_primary_id == ""
    assert state.candidate_count == 0


def test_previous_primary_absent_switches_immediately() -> None:
    state = RankingState(
        previous_primary_id="missing",
        candidate_primary_id="stale",
        candidate_count=1,
        has_previous_primary=True,
        has_candidate_primary=True,
    )
    risks = [ranked_target("candidate", RiskPhase.WARNING, 0.70, 30.0, 100.0)]

    selected = select_primary(risks, state)

    assert selected.target_id == "candidate"
    assert state.previous_primary_id == "candidate"
    assert state.has_previous_primary is True
    assert state.has_candidate_primary is False
    assert state.candidate_primary_id == ""
    assert state.candidate_count == 0


def test_empty_target_id_initializes_state_and_hysteresis_works() -> None:
    state = RankingState()
    empty_primary = ranked_target("", RiskPhase.WARNING, 0.80, 60.0, 200.0)

    initial = select_primary([empty_primary], state)

    assert initial.target_id == ""
    assert state.has_previous_primary is True
    assert state.previous_primary_id == ""

    risks = [
        empty_primary,
        ranked_target("candidate", RiskPhase.WARNING, 0.91, 30.0, 150.0),
    ]
    selected = select_primary(risks, state)

    assert selected.target_id == ""
    assert state.has_previous_primary is True
    assert state.previous_primary_id == ""
    assert state.has_candidate_primary is True
    assert state.candidate_primary_id == "candidate"
    assert state.candidate_count == 1


def test_duplicate_ids_use_best_same_id_as_hysteresis_baseline() -> None:
    low_first_state = RankingState(
        previous_primary_id="duplicate",
        has_previous_primary=True,
    )
    high_first_state = RankingState(
        previous_primary_id="duplicate",
        has_previous_primary=True,
    )
    previous_low = ranked_target("duplicate", RiskPhase.WARNING, 0.70, 60.0, 200.0)
    previous_high = ranked_target("duplicate", RiskPhase.WARNING, 0.80, 60.0, 200.0)
    candidate = ranked_target("candidate", RiskPhase.WARNING, 0.91, 30.0, 150.0)

    low_first = select_primary([previous_low, previous_high, candidate], low_first_state)
    high_first = select_primary([previous_high, previous_low, candidate], high_first_state)

    assert low_first.target_id == "duplicate"
    assert high_first.target_id == "duplicate"
    assert low_first.risk_score == pytest.approx(0.80)
    assert high_first.risk_score == pytest.approx(low_first.risk_score)
    assert low_first_state.has_candidate_primary is True
    assert low_first_state.candidate_primary_id == "candidate"
    assert high_first_state.has_candidate_primary is True
    assert high_first_state.candidate_primary_id == "candidate"


def test_risk_fields_outrank_target_id_tiebreaker() -> None:
    lower_id_lower_risk = ranked_target("alpha", RiskPhase.WARNING, 0.70, 30.0, 100.0)
    lower_id_lower_risk.dcpa_m = 80.0
    lower_id_lower_risk.warning_margin_m = -5.0
    lower_id_lower_risk.danger_margin_m = 40.0

    higher_id_higher_risk = ranked_target("zulu", RiskPhase.WARNING, 0.70, 30.0, 100.0)
    higher_id_higher_risk.dcpa_m = 40.0
    higher_id_higher_risk.warning_margin_m = -15.0
    higher_id_higher_risk.danger_margin_m = 20.0

    assert select_primary([lower_id_lower_risk, higher_id_higher_risk], None).target_id == "zulu"
    assert select_primary([higher_id_higher_risk, lower_id_lower_risk], None).target_id == "zulu"


def test_non_negative_tcpa_and_range_tiebreakers() -> None:
    state = RankingState()
    smaller_tcpa = [
        ranked_target("later", RiskPhase.WARNING, 0.70, 45.0, 100.0),
        ranked_target("sooner", RiskPhase.WARNING, 0.70, 30.0, 300.0),
    ]
    assert select_primary(smaller_tcpa, state).target_id == "sooner"

    state = RankingState()
    only_non_negative = [
        ranked_target("past", RiskPhase.WARNING, 0.70, -1.0, 50.0),
        ranked_target("future", RiskPhase.WARNING, 0.70, 60.0, 500.0),
    ]
    assert select_primary(only_non_negative, state).target_id == "future"

    state = RankingState()
    smaller_range = [
        ranked_target("far", RiskPhase.WARNING, 0.70, 30.0, 300.0),
        ranked_target("near", RiskPhase.WARNING, 0.70, 30.0, 100.0),
    ]
    assert select_primary(smaller_range, state).target_id == "near"
