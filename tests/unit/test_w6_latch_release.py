"""Test W6: Bridge LATCH release logic — TDD red"""
import sys
import types
from pathlib import Path
import importlib.util
from types import SimpleNamespace
import pytest
from unittest.mock import Mock, patch

class MockNode:
    def __init__(self, name):
        self.name = name
    def get_logger(self):
        logger = Mock()
        return logger
    def create_subscription(self, msg_type, topic, callback, qos):
        return Mock()
    def create_publisher(self, msg_type, topic, qos):
        return Mock()
    def create_timer(self, period, callback):
        return Mock()
    def declare_parameter(self, name, default_value):
        class Param:
            value = default_value
        return Param()
    def get_parameter(self, name):
        class Param:
            value = 0.0
        return Param()
    def get_clock(self):
        clock = Mock()
        clock.now = Mock(return_value=SimpleNamespace(nanoseconds=0, to_msg=Mock(return_value=SimpleNamespace(sec=0, nanosec=0))))
        return clock

@pytest.fixture(autouse=True)
def setup_fake_ros(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = MockNode
    rclpy.executors = types.ModuleType("rclpy.executors")
    rclpy.executors.MultiThreadedExecutor = object
    rclpy.qos = types.ModuleType("rclpy.qos")
    rclpy.qos.QoSProfile = lambda **kwargs: kwargs
    rclpy.qos.QoSReliabilityPolicy = SimpleNamespace(BEST_EFFORT=1, RELIABLE=2)
    rclpy.qos.QoSDurabilityPolicy = SimpleNamespace(VOLATILE=1, TRANSIENT_LOCAL=2)
    rclpy.qos.QoSHistoryPolicy = SimpleNamespace(KEEP_LAST=1)

    sil_msgs = types.ModuleType("sil_msgs")
    sil_msgs.msg = types.ModuleType("sil_msgs.msg")
    sil_msgs.msg.OwnShipState = type("OwnShipState", (), {})
    sil_msgs.msg.TargetVesselState = type("TargetVesselState", (), {})
    sil_msgs.msg.EnvironmentState = type("EnvironmentState", (), {})
    sil_msgs.msg.ModulePulse = type("ModulePulse", (), {})
    sil_msgs.msg.ASDREvent = type("ASDREvent", (), {})
    sil_msgs.msg.BridgeState = type("BridgeState", (), {})
    sil_msgs.msg.LifecycleStatus = type("LifecycleStatus", (), {})
    sil_msgs.msg.ScoringRow = type("ScoringRow", (), {})

    l3_external_msgs = types.ModuleType("l3_external_msgs")
    l3_external_msgs.msg = types.ModuleType("l3_external_msgs.msg")
    l3_external_msgs.msg.FilteredOwnShipState = type("FilteredOwnShipState", (), {})
    l3_external_msgs.msg.TrackedTargetArray = type("TrackedTargetArray", (), {})
    l3_external_msgs.msg.EnvironmentState = type("L3EnvironmentState", (), {})
    l3_external_msgs.msg.PlannedRoute = type("PlannedRoute", (), {})
    l3_external_msgs.msg.CheckerVetoNotification = type("CheckerVetoNotification", (), {})

    l3_msgs = types.ModuleType("l3_msgs")
    l3_msgs.msg = types.ModuleType("l3_msgs.msg")
    for name in (
        "AvoidancePlan",
        "AvoidanceWaypoint",
        "ASDRRecord",
        "UIState",
        "ODDState",
        "WorldState",
        "MissionGoal",
        "BehaviorPlan",
        "COLREGsConstraint",
        "TrackedTarget",
        "ThreatState",
        "MissionState",
    ):
        setattr(l3_msgs.msg, name, type(name, (), {}))

    std_msgs = types.ModuleType("std_msgs")
    std_msgs.msg = types.ModuleType("std_msgs.msg")
    std_msgs.msg.Header = type("Header", (), {})

    for module in (
        rclpy,
        rclpy.node,
        rclpy.executors,
        rclpy.qos,
        sil_msgs,
        sil_msgs.msg,
        l3_external_msgs,
        l3_external_msgs.msg,
        l3_msgs,
        l3_msgs.msg,
        std_msgs,
        std_msgs.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)

def get_bridge_class():
    path = Path(__file__).resolve().parents[2] / "docker" / "sil_topic_bridge.py"
    spec = importlib.util.spec_from_file_location("sil_topic_bridge_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module.SilTopicBridge

class TestBridgeLatchRelease:
    """LATCH 释放三条件矩阵"""

    def test_latch_release_on_cpa_cleared_astern(self):
        """条件1: cpa_status==cleared 且 target astern → release"""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        threat_msg = Mock()
        threat_msg.cpa_status = "cleared"
        threat_msg.target_relative_position = "astern"
        
        bridge._avoidance_target_heading_deg = 45.0
        bridge._target_heading_deg = 10.0
        
        # Act
        bridge._on_threat_state(threat_msg)
        
        # Assert
        assert bridge._latch_release_triggered is True
        assert bridge._latch_release_time is not None

    def test_latch_release_on_task_valid_and_transit(self):
        """条件2: task_validity==valid 且 behavior==TRANSIT → release"""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        mission_msg = Mock()
        mission_msg.task_validity = 1  # VALID
        mission_msg.fsm_state = 3  # FSM_ACTIVE
        mission_msg.current_target_wp = SimpleNamespace(latitude=63.44, longitude=10.38)
        behavior_msg = Mock()
        behavior_msg.behavior = 0  # BEHAVIOR_TRANSIT
        bridge._last_behavior_plan = behavior_msg
        
        bridge._avoidance_target_heading_deg = 45.0
        bridge._target_heading_deg = 10.0
        
        # Act
        bridge._on_mission_goal(mission_msg)
        
        # Assert
        assert bridge._latch_release_triggered is True

    def test_latch_release_blocks_if_closing_or_sustained(self):
        """CPA still closing/sustained → 不释放"""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        threat_msg = Mock()
        threat_msg.cpa_status = "closing"
        
        bridge._avoidance_target_heading_deg = 45.0
        bridge._target_heading_deg = 10.0
        
        # Act
        bridge._on_threat_state(threat_msg)
        
        # Assert
        assert bridge._latch_release_triggered is False

    def test_latch_offset_decay_linear_5s(self):
        """5 秒内 LATCH offset 线性衰减到 0"""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        bridge._latch_release_triggered = True
        bridge._latch_release_time = 0.0  # release at t=0
        bridge._latch_offset_at_release_deg = 30.0
        current_offset_deg = 30.0
        
        # Act: step at t=2.5s (halfway)
        t_elapsed = 2.5
        offset_at_halfway = bridge._compute_latch_offset(
            t_release=0.0, t_now=t_elapsed, current_offset_deg=current_offset_deg
        )
        
        # Assert: should be 50% decayed
        assert offset_at_halfway == pytest.approx(15.0, abs=0.1)
        
        # Act: step at t=5.0s (end)
        offset_at_end = bridge._compute_latch_offset(
            t_release=0.0, t_now=5.0, current_offset_deg=current_offset_deg
        )
        
        # Assert: fully decayed
        assert offset_at_end == pytest.approx(0.0, abs=0.01)

    def test_bridge_state_publish_on_latch_release(self):
        """LATCH release 时 publish /sil/bridge_state"""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        bridge._pub_bridge_state = Mock()
        bridge._latch_release_triggered = True
        bridge._latch_release_time = 0.0
        
        # Act
        bridge._autopilot_step()
        
        # Assert: should have called publish
        assert bridge._pub_bridge_state.publish.called
        published_msg = bridge._pub_bridge_state.publish.call_args[0][0]
        assert hasattr(published_msg, 'latch_state')
        assert published_msg.latch_state == "releasing"

    def test_latch_release_reset_on_avoidance_active_change(self):
        """Verify that latch release state variables are reset when _avoidance_active changes state."""
        SilTopicBridge = get_bridge_class()
        bridge = SilTopicBridge()
        
        # 1. Start with latch release triggered and some state
        bridge._latch_release_triggered = True
        bridge._latch_release_time = 123.45
        bridge._latch_offset_at_release_deg = 15.0
        bridge._latch_release_progress = 0.5
        
        # We simulate _avoidance_active transitions. Let's call _on_avoidance_plan with no valid plan when active:
        # Set preconditions:
        bridge._autopilot_enabled = True
        bridge._avoidance_active = True
        bridge._avoidance_heading_controller = Mock()
        
        msg = Mock()
        msg.waypoints = [] # Invalid plan (no waypoints)
        
        bridge._on_avoidance_plan(msg)
        
        # Should have reset _avoidance_active to False and cleared latch state
        assert bridge._avoidance_active is False
        assert bridge._latch_release_triggered is False
        assert bridge._latch_release_time is None
        assert bridge._latch_offset_at_release_deg is None
        assert bridge._latch_release_progress == 0.0

