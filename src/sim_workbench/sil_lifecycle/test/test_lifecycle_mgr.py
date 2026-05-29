"""Unit tests for ScenarioLifecycleMgr including dynamics_mode."""
from __future__ import annotations
import sys
import types
from pathlib import Path
from unittest.mock import Mock

# Make sure we can find sil_lifecycle package
_pkg = Path(__file__).resolve().parents[2]
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

from sil_lifecycle.lifecycle_mgr import ScenarioLifecycleMgr, LifecycleState


def test_dynamics_mode_defaults_to_internal():
    mgr = ScenarioLifecycleMgr()
    assert mgr.dynamics_mode == "internal"


def test_configure_sets_dynamics_mode():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001", dynamics_mode="fmi")
    assert mgr.dynamics_mode == "fmi"
    assert mgr.current_state == LifecycleState.INACTIVE


def test_configure_rejects_invalid_mode():
    mgr = ScenarioLifecycleMgr()
    assert not mgr.configure("scenario_001", dynamics_mode="invalid")
    assert mgr.current_state == LifecycleState.UNCONFIGURED


def test_set_dynamics_mode_only_in_inactive():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001")
    assert mgr.activate()
    assert not mgr.set_dynamics_mode("fmi")  # rejected in ACTIVE


def test_set_dynamics_mode_accepts_in_inactive():
    mgr = ScenarioLifecycleMgr()
    assert mgr.configure("scenario_001")
    assert mgr.set_dynamics_mode("fmi")  # accepted in INACTIVE
    assert mgr.dynamics_mode == "fmi"


def test_status_dict_includes_dynamics_mode():
    mgr = ScenarioLifecycleMgr()
    mgr.configure("scenario_001", dynamics_mode="fmi")
    status = mgr.get_status_dict()
    assert status["dynamics_mode"] == "fmi"


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

