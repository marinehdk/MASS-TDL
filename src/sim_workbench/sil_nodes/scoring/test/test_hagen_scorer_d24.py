"""D2.4 Task 1 — hagen_scorer.py regression/test suite (23 tests)."""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2]))

import pytest
from scoring.hagen_scorer import DEFAULT_WEIGHTS, HagenScorer, ScoringRow


# ---------------------------------------------------------------------------
# score_map key fix
# ---------------------------------------------------------------------------

def test_score_map_full_maps_to_1():
    scorer = HagenScorer()
    assert scorer._score_rule_compliance({"Rule14": "full"}) == 1.0


def test_score_map_partial_maps_to_half():
    scorer = HagenScorer()
    assert scorer._score_rule_compliance({"Rule14": "partial"}) == 0.5


def test_score_map_violated_maps_to_0():
    scorer = HagenScorer()
    assert scorer._score_rule_compliance({"Rule14": "violated"}) == 0.0


def test_score_map_ok_is_removed():
    """'ok' is no longer in score_map so .get defaults to 0.0."""
    scorer = HagenScorer()
    assert scorer._score_rule_compliance({"Rule14": "ok"}) == 0.0


# ---------------------------------------------------------------------------
# action magnitude penalty
# ---------------------------------------------------------------------------

def test_action_magnitude_penalty_at_center():
    scorer = HagenScorer()
    assert scorer._score_action_magnitude_penalty(60.0, 0.0) == 0.0


def test_action_magnitude_penalty_at_lower_boundary():
    scorer = HagenScorer()
    assert scorer._score_action_magnitude_penalty(30.0, 0.0) == 0.0


def test_action_magnitude_penalty_at_upper_boundary():
    scorer = HagenScorer()
    assert scorer._score_action_magnitude_penalty(90.0, 0.0) == 0.0


def test_action_magnitude_penalty_below_range():
    """rudder=20° → | |20|-60 | = 40 → dev = 40-30 = 10 → (10/30)^2 = 1/9."""
    scorer = HagenScorer()
    expected = (10.0 / 30.0) ** 2
    assert math.isclose(
        scorer._score_action_magnitude_penalty(20.0, 0.0), expected
    )


# ---------------------------------------------------------------------------
# compute_verdict
# ---------------------------------------------------------------------------

def test_compute_verdict_pass_all_full():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.5, {"Rule14": "full"}) is True


def test_compute_verdict_pass_partial_allowed():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.5, {"Rule14": "partial"}) is True


def test_compute_verdict_fail_cpa_too_small():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.10, {"Rule14": "full"}) is False


def test_compute_verdict_fail_violated_rule():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.5, {"Rule14": "violated"}) is False


def test_compute_verdict_fail_grounding():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.5, {"Rule14": "full"}, grounding=True) is False


def test_compute_verdict_empty_rules_and_safe_cpa():
    scorer = HagenScorer(cpa_target_nm=0.27)
    assert scorer.compute_verdict(0.5, {}) is True


# ---------------------------------------------------------------------------
# get_quality_score (renamed from get_final_verdict)
# ---------------------------------------------------------------------------

def test_get_quality_score_empty_returns_false():
    scorer = HagenScorer()
    assert scorer.get_quality_score() == (False, 0.0)


def test_get_quality_score_above_threshold():
    scorer = HagenScorer()
    scorer._rows = [ScoringRow(stamp=0, safety=0.9, rule_compliance=0.9,
                                delay_penalty=0.0, action_magnitude_penalty=0.0,
                                phase_score=0.9, plausibility=0.9, total=0.85)]
    ok, score = scorer.get_quality_score(threshold=0.70)
    assert ok is True
    assert math.isclose(score, 0.85)


def test_get_quality_score_below_threshold():
    scorer = HagenScorer()
    scorer._rows = [ScoringRow(stamp=0, safety=0.3, rule_compliance=0.3,
                                delay_penalty=0.0, action_magnitude_penalty=0.0,
                                phase_score=0.3, plausibility=0.3, total=0.30)]
    ok, score = scorer.get_quality_score(threshold=0.70)
    assert ok is False
    assert math.isclose(score, 0.30)


def test_get_final_verdict_removed():
    """get_final_verdict must NOT exist on the scorer class."""
    scorer = HagenScorer()
    assert not hasattr(scorer, "get_final_verdict")


# ---------------------------------------------------------------------------
# ScoringRow extension
# ---------------------------------------------------------------------------

def test_scoring_row_has_pass_fail_field():
    row = ScoringRow(stamp=0, safety=0.5, rule_compliance=0.5,
                     delay_penalty=0.0, action_magnitude_penalty=0.0,
                     phase_score=0.5, plausibility=0.5, total=0.5)
    assert hasattr(row, "pass_fail")
    assert isinstance(row.pass_fail, bool)


def test_scoring_row_has_applicable_rule_field():
    row = ScoringRow(stamp=0, safety=0.5, rule_compliance=0.5,
                     delay_penalty=0.0, action_magnitude_penalty=0.0,
                     phase_score=0.5, plausibility=0.5, total=0.5)
    assert hasattr(row, "applicable_rule")
    assert isinstance(row.applicable_rule, str)
    assert row.applicable_rule == ""


# ---------------------------------------------------------------------------
# score_frame propagates new fields
# ---------------------------------------------------------------------------

def test_score_frame_pass_fail_and_applicable_rule():
    scorer = HagenScorer(cpa_target_nm=0.27)
    row = scorer.score_frame(
        own_lat=0.0, own_lon=0.0, own_heading=0.0, own_sog=0.0,
        targets=[], rule_states={"Rule14": "full"},
        t_action_s=0.0, t_target_action_s=0.0,
        rudder_deg=0.0, turning_rate_dps=0.0,
        behavior_phase="transit",
        trajectory_curvature=0.0, trajectory_accel_ms2=0.0,
        applicable_rule="Rule14",
    )
    assert row.applicable_rule == "Rule14"
    assert isinstance(row.pass_fail, bool)
