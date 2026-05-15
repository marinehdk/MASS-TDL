"""Tests for TargetVesselNode lifecycle callbacks.

Skip all tests if rclpy is not available (e.g., on macOS without ROS2).
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Ensure the package is importable when running tests directly (without colcon)
_pkg = Path(__file__).resolve().parents[4] / "src" / "sim_workbench" / "sil_nodes" / "target_vessel"
sys.path.insert(0, str(_pkg))

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn

    from target_vessel.node import TargetVesselNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False


_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestTargetVesselNodeLifecycle:
    """Lifecycle callbacks return correct TransitionCallbackReturn values."""

    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        """Initialize rclpy before each test, shut down after."""
        rclpy.init()
        self.node = TargetVesselNode()
        yield
        self.node.destroy_node()
        rclpy.shutdown()

    def test_inherits_lifecycle_node(self) -> None:
        assert isinstance(self.node, LifecycleNode)

    def test_on_configure_returns_success(self) -> None:
        ret = self.node.on_configure(None)
        assert ret == TransitionCallbackReturn.SUCCESS

    def test_on_activate_returns_success(self) -> None:
        self.node.on_configure(None)
        ret = self.node.on_activate(None)
        assert ret == TransitionCallbackReturn.SUCCESS

    def test_on_deactivate_returns_success(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        ret = self.node.on_deactivate(None)
        assert ret == TransitionCallbackReturn.SUCCESS

    def test_on_cleanup_returns_success(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        self.node.on_deactivate(None)
        ret = self.node.on_cleanup(None)
        assert ret == TransitionCallbackReturn.SUCCESS
