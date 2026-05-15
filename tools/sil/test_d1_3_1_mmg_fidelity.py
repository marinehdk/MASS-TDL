"""Tests for D1.3.1 MMG fidelity assessment (7 unit tests)."""
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent))

from d1_3_1_mmg_fidelity import (
    FCB_DELTA_DEG,
    FCB_DELTA_RAD,
    FCB_K,
    FCB_T_U_S,
    FCB_T_VAL_S,
    FCB_U0_MS,
    DEFAULT_DT_S,
    FidelityResult,
    FidelityReport,
    compute_rms_error,
    nomoto_deceleration,
    nomoto_turning_circle,
    nomoto_zigzag,
    overshoot_angle,
    resample_to_common_time,
    stopping_distance,
    tactical_diameter,
)


def _make_time(duration_s, dt_s=DEFAULT_DT_S):
    return np.arange(0, duration_s, dt_s, dtype=float)


class TestNomotoStraightDeceleration:
    def test_monotonic_decrease(self):
        t = _make_time(60.0)
        u = nomoto_deceleration(t, FCB_U0_MS, FCB_T_U_S)
        diff = np.diff(u)
        assert np.all(diff <= 0), "deceleration speed must decrease monotonically"
        assert u[0] > u[-1], "final speed must be lower than initial"

    def test_stopping_distance(self):
        t = _make_time(60.0)
        u = nomoto_deceleration(t, FCB_U0_MS, FCB_T_U_S)
        s = stopping_distance(t, u)
        assert 100.0 < s < 130.0, f"stopping distance {s:.2f}m out of 100-130m range"


class TestNomotoTurningCircle:
    def test_steady_yaw_positive(self):
        t = _make_time(600.0)
        u = np.full_like(t, FCB_U0_MS)
        _, _, r = nomoto_turning_circle(t, u, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)
        r_steady = r[-5000:]
        assert np.mean(r_steady) > 0, "yaw rate must be positive for positive rudder"

    def test_tactical_diameter(self):
        t = _make_time(600.0)
        u = np.full_like(t, FCB_U0_MS)
        x, y, _ = nomoto_turning_circle(t, u, FCB_DELTA_RAD, FCB_K, FCB_T_VAL_S)
        td = tactical_diameter(t, x, y)
        assert 0.0 < td < 2000.0, f"tactical diameter {td:.1f}m out of 0-2000m range"


class TestNomotoZigZag:
    def test_heading_reversals(self):
        t = _make_time(200.0)
        u = np.full_like(t, FCB_U0_MS)
        _, _, _, delta_t = nomoto_zigzag(t, u, FCB_DELTA_DEG, FCB_K, FCB_T_VAL_S)
        sign_changes = int(np.sum(np.diff(np.sign(delta_t)) != 0))
        assert sign_changes >= 2, f"expected >= 2 rudder reversals, got {sign_changes}"

    def test_overshoot_angle(self):
        t = _make_time(200.0)
        u = np.full_like(t, FCB_U0_MS)
        _, _, psi, delta_t = nomoto_zigzag(t, u, FCB_DELTA_DEG, FCB_K, FCB_T_VAL_S)
        osa = overshoot_angle(t, psi, delta_t)
        assert 0.0 <= osa <= 10.0, f"overshoot angle {osa:.3f}° out of 0-10° range"


class TestRMSErrorComputation:
    def test_identical_zero_rmse(self):
        ref = np.array([0.0, 1.0, 2.0, 3.0, 4.0])
        sim = np.array([0.0, 1.0, 2.0, 3.0, 4.0])
        rms = compute_rms_error(sim, ref)
        assert rms == 0.0, "identical signals must yield zero RMS"

    def test_offset_positive_rmse(self):
        ref = np.array([0.0, 10.0, 20.0])
        sim = np.array([0.0, 12.0, 18.0])
        rms = compute_rms_error(sim, ref)
        assert rms > 0.0, "offset signals must yield positive RMS error"


class TestFidelityResultStructure:
    def test_required_fields(self):
        result = FidelityResult(
            scenario_id="test-scenario-001",
            reference_name="turning_circle",
            rms_position_pct=2.35,
            rms_heading_deg=1.20,
            rms_speed_pct=3.10,
            passed=True,
            data_path="/tmp/test.csv",
        )
        assert result.scenario_id == "test-scenario-001"
        assert result.reference_name == "turning_circle"
        assert result.rms_position_pct == 2.35
        assert result.rms_heading_deg == 1.20
        assert result.rms_speed_pct == 3.10
        assert result.passed is True
        assert result.data_path == "/tmp/test.csv"


def test_resample_to_common_time_aligned():
    t1 = np.array([0.0, 1.0, 2.0, 3.0])
    v1 = np.array([0.0, 1.0, 2.0, 3.0])
    t2 = np.array([0.0, 1.0, 2.0, 3.0])
    v2 = np.array([3.0, 2.0, 1.0, 0.0])
    v1i, v2i = resample_to_common_time(t1, v1, t2, v2)
    np.testing.assert_array_almost_equal(v1i, v1)
    np.testing.assert_array_almost_equal(v2i, v2)
