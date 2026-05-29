"""Unit tests for lifecycle FSM logic (offline -- no ROS2 runtime needed)."""
import sys
import types
from pathlib import Path
from unittest.mock import Mock

# Make sure we can find sil_lifecycle package
_pkg = Path(__file__).resolve().parents[2] / "src" / "sim_workbench" / "sil_lifecycle"
if str(_pkg) not in sys.path:
    sys.path.insert(0, str(_pkg))

# Check if rclpy is importable (native ROS2 environment)
try:
    import rclpy
    _HAS_ROS2 = True
except ImportError:
    _HAS_ROS2 = False

if not _HAS_ROS2:
    # Set up mock rclpy modules
    mock_rclpy = types.ModuleType("rclpy")
    mock_rclpy.logging = types.ModuleType("rclpy.logging")
    
    mock_rclpy_lifecycle = types.ModuleType("rclpy.lifecycle")
    class LifecycleNode:
        def __init__(self, *args, **kwargs):
            pass
        def declare_parameter(self, *args, **kwargs):
            class Param:
                value = kwargs.get("default_value") if len(args) < 2 else args[1]
            return Param()
        def get_parameter(self, name):
            class Param:
                value = None
            return Param()
        def get_logger(self):
            return Mock()
    mock_rclpy_lifecycle.LifecycleNode = LifecycleNode
    mock_rclpy_lifecycle.TransitionCallbackReturn = Mock()
    
    mock_rclpy_qos = types.ModuleType("rclpy.qos")
    class DummyQoS:
        def __init__(self, *args, **kwargs):
            pass
    mock_rclpy_qos.QoSProfile = DummyQoS
    mock_rclpy_qos.DurabilityPolicy = Mock()
    mock_rclpy_qos.HistoryPolicy = Mock()
    mock_rclpy_qos.ReliabilityPolicy = Mock()
    
    mock_rclpy.lifecycle = mock_rclpy_lifecycle
    mock_rclpy.qos = mock_rclpy_qos
    
    sys.modules["rclpy"] = mock_rclpy
    sys.modules["rclpy.lifecycle"] = mock_rclpy_lifecycle
    sys.modules["rclpy.qos"] = mock_rclpy_qos

    # Mock sil_msgs
    mock_sil = types.ModuleType("sil_msgs")
    mock_sil_msg = types.ModuleType("sil_msgs.msg")
    class LifecycleStatus:
        pass
    mock_sil_msg.LifecycleStatus = LifecycleStatus
    mock_sil.msg = mock_sil_msg
    
    sys.modules["sil_msgs"] = mock_sil
    sys.modules["sil_msgs.msg"] = mock_sil_msg

    # Mock builtin_interfaces
    mock_builtin = types.ModuleType("builtin_interfaces")
    mock_builtin_msg = types.ModuleType("builtin_interfaces.msg")
    class Time:
        pass
    mock_builtin_msg.Time = Time
    mock_builtin.msg = mock_builtin_msg
    
    sys.modules["builtin_interfaces"] = mock_builtin
    sys.modules["builtin_interfaces.msg"] = mock_builtin_msg

    # Mock rosgraph_msgs
    mock_rosgraph = types.ModuleType("rosgraph_msgs")
    mock_rosgraph_msg = types.ModuleType("rosgraph_msgs.msg")
    class Clock:
        pass
    mock_rosgraph_msg.Clock = Clock
    mock_rosgraph.msg = mock_rosgraph_msg
    
    sys.modules["rosgraph_msgs"] = mock_rosgraph
    sys.modules["rosgraph_msgs.msg"] = mock_rosgraph_msg

import pytest
from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr, LifecycleState


@pytest.fixture
def mgr():
    return ScenarioLifecycleMgr(tick_hz=50.0)


class TestInitialState:
    def test_starts_unconfigured(self, mgr):
        assert mgr.current_state == LifecycleState.UNCONFIGURED

    def test_scenario_id_empty(self, mgr):
        assert mgr.scenario_id == ""

    def test_sim_time_zero(self, mgr):
        assert mgr.sim_time == 0.0

    def test_sim_rate_defaults_to_one(self, mgr):
        assert mgr.sim_rate == 1.0


class TestValidTransitions:
    def test_configure_from_unconfigured(self, mgr):
        assert mgr.configure("r14-head-on", "abc123")
        assert mgr.current_state == LifecycleState.INACTIVE
        assert mgr.scenario_id == "r14-head-on"
        assert mgr.scenario_hash == "abc123"

    def test_full_lifecycle_flow(self, mgr):
        assert mgr.configure("test", "hash")
        assert mgr.activate()
        assert mgr.current_state == LifecycleState.ACTIVE
        assert mgr.deactivate()
        assert mgr.current_state == LifecycleState.INACTIVE
        assert mgr.cleanup()
        assert mgr.current_state == LifecycleState.UNCONFIGURED
        assert mgr.scenario_id == ""


class TestGuardConditions:
    def test_cannot_activate_from_unconfigured(self, mgr):
        assert mgr.activate() is False

    def test_cannot_deactivate_from_unconfigured(self, mgr):
        assert mgr.deactivate() is False

    def test_cannot_deactivate_from_inactive(self, mgr):
        mgr.configure("test")
        assert mgr.activate()
        assert mgr.deactivate()
        # Second deactivate should fail (now INACTIVE)
        assert mgr.deactivate() is False

    def test_cannot_configure_from_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.configure("test2") is False

    def test_cannot_activate_twice(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.activate() is False

    def test_cannot_cleanup_from_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        assert mgr.cleanup() is False


class TestTickAndTime:
    def test_tick_increments_sim_time_when_active(self, mgr):
        mgr.configure("test")
        mgr.activate()
        mgr.tick()
        expected_dt = 1.0 / 50.0
        assert mgr.sim_time == pytest.approx(expected_dt)

    def test_tick_does_not_increment_when_inactive(self, mgr):
        mgr.configure("test")
        mgr.tick()
        assert mgr.sim_time == 0.0

    def test_tick_does_not_scale_by_sim_rate(self, mgr):
        mgr.configure("test")
        mgr.activate()
        mgr.set_sim_rate(2.0)
        mgr.tick()
        # With fixed-increment clock, sim_time increases by exactly 1.0/tick_hz regardless of sim_rate
        assert mgr.sim_time == pytest.approx(1.0 / 50.0)

    def test_set_sim_rate_rejects_negative(self, mgr):
        assert mgr.set_sim_rate(-1.0) is False


class TestGetStatus:
    def test_status_after_configure(self, mgr):
        mgr.configure("r14", "sha")
        status = mgr.get_status_dict()
        assert status["current_state"] == "INACTIVE"
        assert status["scenario_id"] == "r14"
        assert status["scenario_hash"] == "sha"
        assert status["sim_time"] == 0.0


def test_tick_advances_by_dt_tick_not_sim_rate():
    """tick() must advance sim_time by dt_tick regardless of sim_rate."""
    mgr = ScenarioLifecycleMgr(tick_hz=250.0)
    assert mgr.configure("s1")
    assert mgr.activate()
    # Rate=10 must NOT multiply the increment
    mgr.set_sim_rate(10.0)
    mgr.tick()
    expected_dt = 1.0 / 250.0
    assert abs(mgr.sim_time - expected_dt) < 1e-9, (
        f"sim_time={mgr.sim_time}, expected {expected_dt}"
    )


def test_scenario_lifecycle_mgr_run_start_wall():
    """ScenarioLifecycleMgr has run_start_wall property defaulting to 0.0 and set on activate."""
    import time
    mgr = ScenarioLifecycleMgr()
    assert mgr.run_start_wall == 0.0
    assert mgr.configure("s1")
    t0 = time.time()
    assert mgr.activate()
    assert mgr.run_start_wall >= t0
    assert mgr.run_start_wall <= time.time()


def test_tick_hz_property():
    """ScenarioLifecycleMgr has tick_hz read-only property."""
    mgr = ScenarioLifecycleMgr(tick_hz=100.0)
    assert mgr.tick_hz == 100.0
    import pytest
    with pytest.raises(AttributeError):
        mgr.tick_hz = 50.0


def test_set_sim_rate_does_not_change_dt_tick():
    """set_sim_rate() must not affect the tick increment."""
    mgr = ScenarioLifecycleMgr(tick_hz=100.0)
    assert mgr.configure("s1")
    assert mgr.activate()
    mgr.set_sim_rate(50.0)
    for _ in range(10):
        mgr.tick()
    expected = 10.0 / 100.0
    assert abs(mgr.sim_time - expected) < 1e-9, (
        f"sim_time={mgr.sim_time}, expected {expected}"
    )


def test_tick_does_not_advance_when_inactive():
    """tick() must not advance sim_time when FSM is not ACTIVE."""
    mgr = ScenarioLifecycleMgr(tick_hz=250.0)
    mgr.configure("s1")
    # Not yet activated
    mgr.tick()
    assert mgr.sim_time == 0.0

