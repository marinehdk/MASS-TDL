"""Tests for FaultInjectionNode lifecycle callbacks and core logic.
Skip all tests if rclpy is not available (e.g., macOS without ROS2).
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# ensure the package is importable when running tests directly (without colcon)
_pkg = (
    Path(__file__).resolve().parents[4]
    / "src"
    / "sim_workbench"
    / "sil_nodes"
    / "fault_injection"
)
sys.path.insert(0, str(_pkg))

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn

    from fault_injection.node import FaultInjectionNode

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_ROS2_SKIP_REASON = "rclpy not available (ROS2 not installed on this platform)"


class TestFaultInjectionCoreLogic:
    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        if not HAS_ROS2:
            pytest.skip(_ROS2_SKIP_REASON)
        rclpy.init()
        self.node = FaultInjectionNode()
        self.node.on_configure(None)
        yield
        self.node.destroy_node()
        rclpy.shutdown()

    def test_trigger_known_type_adds_to_active(self) -> None:
        fid = self.node.trigger("ais_dropout")
        assert len(fid) == 8
        assert fid in self.node._active
        assert self.node._active[fid]["fault_type"] == "ais_dropout"

    def test_trigger_unknown_type_raises(self) -> None:
        with pytest.raises(ValueError):
            self.node.trigger("unknown_fault")

    def test_list_active_returns_all_active_faults(self) -> None:
        self.node.trigger("ais_dropout")
        self.node.trigger("radar_noise_spike")
        active = self.node.list_active()
        assert len(active) >= 2
        types = [f["fault_type"] for f in active]
        assert "ais_dropout" in types
        assert "radar_noise_spike" in types

    def test_cancel_existing_returns_true(self) -> None:
        fid = self.node.trigger("ais_dropout")
        assert self.node.cancel(fid) is True
        assert fid not in self.node._active

    def test_cancel_nonexistent_returns_false(self) -> None:
        assert self.node.cancel("nonexistent") is False

    def test_get_active_types_returns_active_fault_types(self) -> None:
        self.node.trigger("ais_dropout")
        self.node.trigger("disturbance_step")
        types = self.node.get_active_types()
        assert "ais_dropout" in types
        assert "disturbance_step" in types


@pytest.mark.skipif(not HAS_ROS2, reason=_ROS2_SKIP_REASON)
class TestFaultInjectionLifecycle:
    @pytest.fixture(autouse=True)
    def ros_context(self) -> None:
        rclpy.init()
        self.node = FaultInjectionNode()
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
        assert len(self.node._publishers) == 3
        for ft in self.node.FAULT_TYPES:
            assert ft in self.node._publishers

    def test_on_deactivate_destroys_publishers(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        ret = self.node.on_deactivate(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._publishers == {}

    def test_on_cleanup_clears_active(self) -> None:
        self.node.on_configure(None)
        self.node.on_activate(None)
        self.node._active["test"] = {"fault_id": "test"}
        self.node.on_deactivate(None)
        ret = self.node.on_cleanup(None)
        assert ret == TransitionCallbackReturn.SUCCESS
        assert self.node._active == {}
