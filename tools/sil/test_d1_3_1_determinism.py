"""Tests for D1.3.1 determinism replay tool (Phase 1)."""
import math
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent))
from d1_3_1_determinism_replay import (
    DeterminismReport,
    compute_pairwise_diffs,
    compute_time_shift,
    generate_report,
)


def _make_traj(duration: float = 90.0, dt: float = 0.1) -> np.ndarray:
    t = np.arange(0.0, duration, dt)
    x = np.sin(t * 0.1) * 10.0 + t * 0.5
    y = np.cos(t * 0.1) * 10.0
    psi = np.arctan2(
        np.cos(t * 0.1) * 1.0,
        0.5 - np.sin(t * 0.1) * 1.0,
    )
    return np.column_stack([t, x, y, psi])


class TestTimeShiftComputation:
    def test_identical_zero_shift(self):
        traj = _make_traj()
        shift = compute_time_shift(traj, traj)
        assert abs(shift) < 0.001

    def test_shifted_detect(self):
        t = np.arange(0.0, 90.0, 0.1)
        x = np.sin(t * 0.1) * 10.0 + t * 0.5
        y = np.cos(t * 0.1) * 10.0
        psi = np.arctan2(np.cos(t * 0.1), 0.5 - np.sin(t * 0.1))
        traj = np.column_stack([t, x, y, psi])

        offset = 0.5
        x_s = np.interp(t - offset, t, x, left=x[0], right=x[-1])
        y_s = np.interp(t - offset, t, y, left=y[0], right=y[-1])
        psi_s = np.interp(t - offset, t, psi, left=psi[0], right=psi[-1])
        traj_shifted = np.column_stack([t, x_s, y_s, psi_s])

        shift = compute_time_shift(traj, traj_shifted)
        assert abs(shift - (-offset)) < 0.1, f"Expected shift ~{-offset}, got {shift}"


class TestPairwiseDiffs:
    def test_identical_runs_zero_diff(self):
        traj = _make_traj()
        diffs = compute_pairwise_diffs([traj, traj, traj])
        assert diffs["run_count"] == 3
        assert diffs["max_heading_diff_deg"] < 0.001
        assert diffs["max_position_diff_m"] < 0.001
        assert abs(diffs["max_time_shift_s"]) < 0.001

    def test_three_runs_sane_diffs(self):
        t = np.arange(0.0, 90.0, 0.1)

        def make_variant(amp: float) -> np.ndarray:
            x = np.sin(t * 0.1) * amp + t * 0.5
            y = np.cos(t * 0.1) * amp
            psi = np.arctan2(np.cos(t * 0.1) * amp * 0.1, 0.5 - np.sin(t * 0.1) * amp * 0.1)
            return np.column_stack([t, x, y, psi])

        trajs = [make_variant(10.0 + i * 0.2) for i in range(3)]
        diffs = compute_pairwise_diffs(trajs)

        assert diffs["run_count"] == 3
        assert diffs["max_heading_diff_deg"] > 0.001
        assert diffs["max_heading_diff_deg"] < 180.0
        assert diffs["max_position_diff_m"] > 0.001


class TestDeterminismReport:
    def test_passed_when_all_thresholds_met(self, tmp_path):
        diffs = {
            "run_count": 5,
            "max_heading_diff_deg": 0.005,
            "max_position_diff_m": 0.0001,
            "max_time_shift_s": 0.001,
        }
        report_path = generate_report(diffs, tmp_path)
        assert report_path.exists()
        content = report_path.read_text()
        assert "0.005" in content
        assert "true" in content

    def test_failed_when_heading_exceeds(self, tmp_path):
        diffs = {
            "run_count": 3,
            "max_heading_diff_deg": 5.0,
            "max_position_diff_m": 0.0001,
            "max_time_shift_s": 0.001,
        }
        report_path = generate_report(diffs, tmp_path)
        content = report_path.read_text()
        assert "false" in content

    def test_failed_when_position_exceeds(self, tmp_path):
        diffs = {
            "run_count": 3,
            "max_heading_diff_deg": 0.005,
            "max_position_diff_m": 0.5,
            "max_time_shift_s": 0.001,
        }
        report_path = generate_report(diffs, tmp_path)
        content = report_path.read_text()
        assert "false" in content
