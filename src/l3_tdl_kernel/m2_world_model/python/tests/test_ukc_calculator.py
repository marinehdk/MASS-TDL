"""Tests for dynamic UKC calculator (ukc_calculator.py).

Adapted to the actual API method names:
  - compute_squat_barrass / compute_squat_roemisch
  - get_catzoc_allowance / calculate_heel_allowance
  - compute_net_ukc
"""

from __future__ import annotations

import pytest

from enc_loader.ukc_calculator import CatzocLevel, UkcCalculator


@pytest.fixture
def calculator() -> UkcCalculator:
    return UkcCalculator(cb=0.65, beam_m=14.0, draft_static_m=3.5)


class TestBarrassSquat:
    """Barrass empirical squat formula."""

    def test_barrass_squat(self, calculator: UkcCalculator):
        squat = calculator.compute_squat_barrass(speed_knots=12, is_restricted_channel=False)
        # Barrass: C_b * V^2 / 100 = 0.65 * 144 / 100 = 0.936
        assert squat == pytest.approx(0.936, abs=1e-3)
        assert 0 < squat < 2


class TestRoemischSquat:
    """Roemisch dynamic squat model."""

    def test_romisch_squat(self, calculator: UkcCalculator):
        squat = calculator.compute_squat_roemisch(speed_knots=18, water_depth_m=10)
        assert squat > 0
        assert squat < 5  # sanity: shouldn't exceed vessel draft


class TestCatzocA1Confidence:
    """CATZOC A1 margin calculation."""

    def test_catzoc_a1_confidence(self, calculator: UkcCalculator):
        margin = calculator.get_catzoc_allowance(CatzocLevel.A1, depth_m=15.0)
        # A1: depth_error_fixed=0.5, depth_error_pct=0.01, margin=1.0
        # (0.5 + 0.01*15) * 1.0 + 0.0 = 0.65
        assert margin == pytest.approx(0.65, abs=1e-3)
        assert 0 < margin <= 1


class TestCatzocBConfidence:
    """CATZOC B margin calculation with extra compensation."""

    def test_catzoc_b_confidence(self, calculator: UkcCalculator):
        margin = calculator.get_catzoc_allowance(CatzocLevel.B, depth_m=15.0)
        # B: depth_error_fixed=1.0, depth_error_pct=0.05, margin=1.5, extra=0.5
        # (1.0 + 0.05*15) * 1.5 + 0.5 = 1.75*1.5 + 0.5 = 3.125
        assert margin == pytest.approx(3.125, abs=1e-3)
        assert 0 < margin < 5


class TestNetUkcPositive:
    """End-to-end compute_net_ukc with typical transit values."""

    def test_net_ukc_positive(self, calculator: UkcCalculator):
        result = calculator.compute_net_ukc(
            chart_depth_m=15.0,
            tide_height_m=1.0,
            speed_knots=8.0,
            heel_degrees=3.0,
            catzoc=CatzocLevel.A2,
        )
        assert result["net_ukc_m"] > 0
        assert "confidence" not in result  # actual API doesn't have "confidence"
        assert result["safe"] is True
        assert result["status"] == "SAFE"
