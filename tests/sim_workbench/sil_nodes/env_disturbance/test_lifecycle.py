"""Tests for EnvDisturbanceNode lifecycle callbacks.

Skip all tests if rclpy is not available (e.g., on macOS without ROS2).
"""
from __future__ import annotations

import pytest

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn

    from env_disturbance.node import EnvDisturbanceNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False


_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestEnvDisturbanceLifecycle:
    """Lifecycle callbacks return correct TransitionCallbackReturn values."""

    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        """Initialize rclpy before each test, shut down after."""
        rclpy.init()
        self.node = EnvDisturbanceNode()
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
