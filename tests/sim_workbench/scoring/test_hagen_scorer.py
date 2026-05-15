"""Tests for HagenScorer — 6-dimensional scoring engine per Hagen (2022) + Woerner (2019)."""

import pytest
from scoring.hagen_scorer import HagenScorer, ScoringRow


class TestHagenScorer:
    """12 tests covering all 6 dimensions + edge cases + total score."""

    def test_safety_perfect_cpa(self):
        """CPA ≫ target → safety = 1.0."""
        scorer = HagenScorer(cpa_target_nm=0.27)
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 0.0, 0.0, "transit", 0.0, 0.0
        )
        assert row.safety == 1.0

    def test_safety_zero_cpa(self):
        """Near-zero CPA → safety < 0.1."""
        scorer = HagenScorer(cpa_target_nm=0.27)
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [(63.4403, 10.38, 180.0, 10.0)],
            {}, 0.0, 0.0, 0.0, 0.0, "transit", 0.0, 0.0
        )
        assert row.safety < 0.1

    def test_rule_compliance_all_pass(self):
        """All rules 'ok' → rule_compliance = 1.0."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [],
            {"R14": "ok", "R8": "ok", "R16": "ok"},
            0.0, 0.0, 0.0, 0.0, "transit", 0.0, 0.0
        )
        assert row.rule_compliance == 1.0

    def test_rule_compliance_one_partial(self):
        """One 'partial' among three rules → 0.82 < score < 0.85."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [],
            {"R14": "ok", "R8": "partial", "R16": "ok"},
            0.0, 0.0, 0.0, 0.0, "transit", 0.0, 0.0
        )
        assert 0.82 < row.rule_compliance < 0.85

    def test_delay_penalty_zero(self):
        """Action on time → delay_penalty = 0.0."""
        scorer = HagenScorer(delay_coeff=0.01)
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 120.0, 120.0, 0.0, 0.0, "give_way", 0.0, 0.0
        )
        assert row.delay_penalty == 0.0

    def test_delay_penalty_late(self):
        """30s late with delay_coeff=0.01 → penalty ≈ 0.30."""
        scorer = HagenScorer(delay_coeff=0.01)
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 150.0, 120.0, 0.0, 0.0, "give_way", 0.0, 0.0
        )
        assert 0.29 < row.delay_penalty < 0.31

    def test_action_magnitude_good_range(self):
        """Rudder 45°, rate 3°/s — within [30°, 90°] → penalty = 0.0."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 45.0, 3.0, "give_way", 0.0, 0.0
        )
        assert row.action_magnitude_penalty == 0.0

    def test_action_magnitude_too_small(self):
        """Rudder 10° — outside optimal range → penalty > 0.1."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 10.0, 1.0, "give_way", 0.0, 0.0
        )
        assert row.action_magnitude_penalty > 0.1

    def test_phase_score_give_way(self):
        """Give-way phase → phase_score = 1.0."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 0.0, 0.0, "give_way", 0.0, 0.0
        )
        assert row.phase_score == 1.0

    def test_phase_score_in_extremis(self):
        """In-extremis phase → phase_score = 0.0."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 0.0, 0.0, "in_extremis", 0.0, 0.0
        )
        assert row.phase_score == 0.0

    def test_trajectory_implausibility_normal(self):
        """Normal curvature/accel within limits → plausibility = 1.0."""
        scorer = HagenScorer(kappa_max=0.01, accel_max_ms2=2.0)
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [], {}, 0.0, 0.0, 0.0, 0.0, "transit", 0.005, 0.5
        )
        assert row.plausibility == 1.0

    def test_total_score_perfect(self):
        """Ideal scenario → total score 0.95–1.01."""
        scorer = HagenScorer()
        row = scorer.score_frame(
            63.44, 10.38, 0.0, 10.0,
            [],
            {"R14": "ok"},
            100.0, 100.0, 45.0, 3.0, "give_way", 0.001, 0.1
        )
        assert 0.95 < row.total < 1.01
