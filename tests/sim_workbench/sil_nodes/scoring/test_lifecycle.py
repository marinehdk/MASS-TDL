"""Tests for ScoringNode lifecycle + scoring logic.

Skip lifecycle tests if rclpy is not available (e.g., on macOS without ROS2).
Scoring-logic tests need rclpy for node instantiation (LifecycleNode base).
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Ensure the package is importable when running tests directly (without colcon)
_pkg = Path(__file__).resolve().parents[4] / "src" / "sim_workbench" / "sil_nodes" / "scoring"
sys.path.insert(0, str(_pkg))

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn

    from scoring.node import ScoringNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


# ── Pure-Python scoring tests ────────────────────────────────────────


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestScoringLogic:
    """Pure-Python tests for score() and get_final_verdict() — no lifecycle needed."""

    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        """Initialize rclpy before each test, shut down after."""
        rclpy.init()
        self.node = ScoringNode()
        yield
        self.node.destroy_node()
        rclpy.shutdown()

    def test_score_returns_expected_keys(self) -> None:
        row = self.node.score(timestamp=1.0)
        assert set(row.keys()) == {
            "stamp", "safety", "rule_compliance", "delay",
            "magnitude", "phase", "plausibility", "total",
        }

    def test_score_perfect_conditions(self) -> None:
        row = self.node.score(
            timestamp=1.0, cpa=2.0, rule_broken=False,
            delay_s=0.0, path_deviation=0.0,
        )
        assert row["safety"] == 1.0
        assert row["rule_compliance"] == 1.0
        assert row["delay"] == 1.0
        assert row["magnitude"] == 1.0
        assert row["total"] == 1.0

    def test_score_rule_broken_zeroes_compliance(self) -> None:
        row = self.node.score(timestamp=1.0, rule_broken=True)
        assert row["rule_compliance"] == 0.0
        assert row["total"] < 1.0

    def test_score_delay_decay(self) -> None:
        row = self.node.score(timestamp=1.0, delay_s=30.0)
        assert row["delay"] == pytest.approx(0.5)

    def test_score_path_deviation_penalty(self) -> None:
        row = self.node.score(timestamp=1.0, path_deviation=250.0)
        assert row["magnitude"] == pytest.approx(0.5)

    def test_score_cpa_clamped_below_threshold(self) -> None:
        row = self.node.score(timestamp=1.0, cpa=0.5)
        assert row["safety"] == 0.5

    def test_score_cpa_clamped_negative(self) -> None:
        row = self.node.score(timestamp=2.0, cpa=-0.5)
        assert row["safety"] == 0.0

    def test_score_cpa_clamped_above_threshold(self) -> None:
        row = self.node.score(timestamp=3.0, cpa=3.0)
        assert row["safety"] == 1.0

    def test_get_rows_returns_copy(self) -> None:
        self.node.score(timestamp=1.0)
        self.node.score(timestamp=2.0)
        rows = self.node.get_rows()
        assert len(rows) == 2
        rows.pop()
        assert len(self.node.get_rows()) == 2  # original unchanged

    def test_get_final_verdict_empty(self) -> None:
        passed, avg = self.node.get_final_verdict()
        assert passed is False
        assert avg == 0.0

    def test_get_final_verdict_above_threshold(self) -> None:
        self.node.score(
            timestamp=1.0, cpa=2.0, rule_broken=False,
            delay_s=0.0, path_deviation=0.0,
        )
        passed, avg = self.node.get_final_verdict(threshold=0.7)
        assert passed is True
        assert avg == pytest.approx(1.0)

    def test_get_final_verdict_below_threshold(self) -> None:
        self.node.score(
            timestamp=1.0, cpa=0.0, rule_broken=True,
            delay_s=60.0, path_deviation=500.0,
        )
        passed, avg = self.node.get_final_verdict(threshold=0.7)
        assert passed is False
        assert avg == pytest.approx(0.0)

    def test_custom_weights(self) -> None:
        """Only safety matters — all other weights zeroed."""
        w = {d: 0.0 for d in ScoringNode.DIMS}
        w["safety"] = 1.0
        node = ScoringNode(weights=w)
        try:
            row = node.score(timestamp=1.0, cpa=0.5, rule_broken=True)
            assert row["total"] == pytest.approx(0.5)
        finally:
            node.destroy_node()


# ── Lifecycle tests ──────────────────────────────────────────────────


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestScoringNodeLifecycle:
    """Lifecycle callbacks return correct TransitionCallbackReturn values."""

    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        """Initialize rclpy before each test, shut down after."""
        rclpy.init()
        self.node = ScoringNode()
        yield
        self.node.destroy_node()
        rclpy.shutdown()

    def test_inherits_lifecycle_node(self) -> None:
        assert isinstance(self.node, LifecycleNode)

    def test_on_configure_returns_success(self) -> None:
        ret = self.node.on_configure(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._weights is not None

    def test_on_activate_returns_success(self) -> None:
        self.node.on_configure(None)
        ret = self.node.on_activate(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._score_pub is not None
        assert self.node._timer is not None

    def test_on_deactivate_returns_success(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        ret = self.node.on_deactivate(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._timer is None
        assert self.node._score_pub is None

    def test_on_cleanup_returns_success(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        self.node.on_deactivate(None)
        self.node._rows.append({"total": 0.5})
        ret = self.node.on_cleanup(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._rows == []
