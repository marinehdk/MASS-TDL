import importlib.util
import math
import sys
import types
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import pytest


class _Publisher:
    def __init__(self):
        self.messages = []

    def publish(self, msg):
        self.messages.append(msg)


class _FakeSilOwnShipState:
    def __init__(self):
        self.stamp = None
        self.lat = 0.0
        self.lon = 0.0
        self.heading = 0.0
        self.sog = 0.0
        self.cog = 0.0
        self.rot = 0.0
        self.u = 0.0
        self.v = 0.0
        self.r = 0.0
        self.rudder_angle = 0.0
        self.throttle = 0.0


class _FakeFilteredOwnShipState:
    def __init__(self):
        self.schema_version = 0
        self.stamp = None
        self.position = SimpleNamespace(latitude=0.0, longitude=0.0, altitude=0.0)
        self.heading_deg = 0.0
        self.sog_kn = 0.0
        self.cog_deg = 0.0
        self.u_water = 0.0
        self.v_water = 0.0
        self.r_dot_deg_s = 0.0
        self.current_speed_kn = 0.0
        self.current_direction_deg = 0.0
        self.roll_deg = 0.0
        self.pitch_deg = 0.0
        self.covariance = [0.0] * 36
        self.nav_mode = ""
        self.confidence = 0.0
        self.rationale = ""


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = object
    rclpy.executors = types.ModuleType("rclpy.executors")
    rclpy.executors.MultiThreadedExecutor = object
    rclpy.qos = types.ModuleType("rclpy.qos")
    rclpy.qos.QoSProfile = lambda **kwargs: kwargs
    rclpy.qos.QoSReliabilityPolicy = SimpleNamespace(BEST_EFFORT=1, RELIABLE=2)
    rclpy.qos.QoSDurabilityPolicy = SimpleNamespace(VOLATILE=1, TRANSIENT_LOCAL=2)
    rclpy.qos.QoSHistoryPolicy = SimpleNamespace(KEEP_LAST=1)

    sil_msgs = types.ModuleType("sil_msgs")
    sil_msgs.msg = types.ModuleType("sil_msgs.msg")
    sil_msgs.msg.OwnShipState = _FakeSilOwnShipState
    sil_msgs.msg.TargetVesselState = type("TargetVesselState", (), {})
    sil_msgs.msg.EnvironmentState = type("EnvironmentState", (), {})
    sil_msgs.msg.ModulePulse = type("ModulePulse", (), {})
    sil_msgs.msg.ASDREvent = type("ASDREvent", (), {})
    sil_msgs.msg.BridgeState = type("BridgeState", (), {})
    sil_msgs.msg.LifecycleStatus = type("LifecycleStatus", (), {})
    sil_msgs.msg.ScoringRow = type("ScoringRow", (), {})

    l3_external_msgs = types.ModuleType("l3_external_msgs")
    l3_external_msgs.msg = types.ModuleType("l3_external_msgs.msg")
    l3_external_msgs.msg.FilteredOwnShipState = _FakeFilteredOwnShipState
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


def _load_bridge(monkeypatch):
    _install_fake_ros_modules(monkeypatch)
    path = Path("/opt/ws/docker/sil_topic_bridge.py")
    if not path.exists():
        path = Path(__file__).resolve().parents[2] / "docker" / "sil_topic_bridge.py"
    spec = importlib.util.spec_from_file_location("sil_topic_bridge_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_avoidance_plan_rudder_command_is_published_in_radians(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock
    fake_self = SimpleNamespace(
        _pub_act=_Publisher(),
        _record_pulse=lambda module_id: None,
        _autopilot_enabled=False,
        _avoidance_active=True,
        _last_behavior_plan=None,
        _avoidance_target_heading_deg=None,
        _avoidance_heading_controller=Mock(),
        _last_ownship_raw=SimpleNamespace(heading=0.0, sog=10.0, rot=0.0),
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _last_avoidance_waypoint=None,
        _reset_latch_release_state=lambda: None,
        _trace_writer=Mock(),
        _get_sim_time=lambda: 0.0,
        get_logger=lambda: Mock(),
    )
    plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=12),
        waypoints=[SimpleNamespace(turn_radius_m=92.0, target_speed_kn=12.5)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)
    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(fake_self, plan.stamp)

    assert cmd.rudder_angle == -math.atan2(46.0, 92.0)
    assert cmd.throttle == 0.5


def test_placeholder_turn_radius_does_not_command_hard_rudder(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock
    fake_self = SimpleNamespace(
        _pub_act=_Publisher(),
        _record_pulse=lambda module_id: None,
        _autopilot_enabled=False,
        _avoidance_active=True,
        _last_behavior_plan=None,
        _avoidance_target_heading_deg=None,
        _avoidance_heading_controller=Mock(),
        _last_ownship_raw=SimpleNamespace(heading=0.0, sog=10.0, rot=0.0),
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _last_avoidance_waypoint=None,
        _reset_latch_release_state=lambda: None,
        _trace_writer=Mock(),
        _get_sim_time=lambda: 0.0,
        get_logger=lambda: Mock(),
    )
    plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=12),
        waypoints=[SimpleNamespace(turn_radius_m=0.0, target_speed_kn=12.5)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)
    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(fake_self, plan.stamp)

    assert cmd.rudder_angle == 0.0
    assert cmd.throttle == 0.4


def test_sil_own_ship_state_is_converted_to_l3_units(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _pub_foss=_Publisher(),
        _record_pulse=lambda module_id: None,
        _trace_writer=Mock(),
        _get_sim_time=lambda: 0.0,
    )
    msg = _FakeSilOwnShipState()
    msg.lat = 63.44
    msg.lon = 10.38
    msg.heading = math.pi / 2.0
    msg.cog = math.pi
    msg.sog = 10.0 * 0.514444
    msg.u = 5.0
    msg.v = 0.25
    msg.r = 0.01

    bridge.SilTopicBridge._on_own_ship_state(fake_self, msg)

    out = fake_self._pub_foss.messages[-1]
    assert out.heading_deg == 90.0
    assert out.cog_deg == 180.0
    assert out.sog_kn == pytest.approx(10.0, abs=1e-4)
    assert out.u_water == msg.u
    assert out.r_dot_deg_s == math.degrees(msg.r)


def test_bridge_uses_reliable_volatile_qos_for_l3_consumers(monkeypatch):
    bridge = _load_bridge(monkeypatch)

    qos = bridge._reliable_volatile_qos(depth=7)

    assert qos["reliability"] == bridge.QoSReliabilityPolicy.RELIABLE
    assert qos["durability"] == bridge.QoSDurabilityPolicy.VOLATILE
    assert qos["depth"] == 7


def test_xte_intercept_gain_and_clamp(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    
    mock_heading_controller = Mock()
    mock_heading_controller.step = Mock(return_value=0.0)
    
    fake_self = SimpleNamespace(
        _pub_foss=_Publisher(),
        _record_pulse=lambda module_id: None,
        _trace_writer=Mock(),
        _get_sim_time=lambda: 0.0,
        _heading_controller=mock_heading_controller,
        _speed_controller=Mock(),
        _target_heading_deg=90.0,
        _target_sog_kn=15.0,
        _current_target_wp_lat=0.0,
        _current_target_wp_lon=1.0,
        _route_wps=[(0.0, 0.0), (0.0, 1.0)],
        _last_ownship_raw=SimpleNamespace(
            lat=0.0001,  # North of route, XTE ~11.11m (to port)
            lon=0.5,
            heading=math.radians(90.0),
            sog=15.0 / 1.94384,
            rot=0.0
        ),
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        _great_circle_bearing=bridge.SilTopicBridge._great_circle_bearing,
        _signed_xte_m=lambda lat, lon: bridge.SilTopicBridge._signed_xte_m(fake_self, lat, lon),
        get_logger=lambda: Mock(),
    )
    
    bridge.SilTopicBridge._compute_transit_autopilot(fake_self, None)
    
    assert mock_heading_controller.step.called
    args = mock_heading_controller.step.call_args[0]
    heading_error_deg = args[0]
    
    # Expected bearing ~ 90.01.
    # Expected XTE is approx 11.11 meters.
    # Gain is expected to be 0.3. Correction = 11.11 * 0.3 = 3.33 deg.
    # So heading_error_deg = effective_target_heading (90.01 + 3.33) - current_heading (90.0) = 3.34 deg.
    # Let's assert it is within 3.2 and 3.5 degrees (positive correction for port drift).
    assert 3.2 <= heading_error_deg <= 3.5
    
    # Test clamp with very large XTE (1000 meters)
    fake_self._last_ownship_raw.lat = 0.01 # ~1111 meters north
    bridge.SilTopicBridge._compute_transit_autopilot(fake_self, None)
    args = mock_heading_controller.step.call_args[0]
    heading_error_deg = args[0]
    # Expected bearing ~ 91.13.
    # Expected XTE is approx 1111 meters.
    # Correction = 1111 * 0.3 = 333 deg, which clamps to 85.0.
    # heading_error_deg = 91.13 + 85.0 - 90.0 = 86.13 deg.
    # Let's assert it is within 85.5 and 87.0 degrees.
    assert 85.5 <= heading_error_deg <= 87.0

