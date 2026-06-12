import importlib.util
import math
import sys
import types
from pathlib import Path
from types import SimpleNamespace

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


class _FakeClock:
    def now(self):
        return SimpleNamespace(
            nanoseconds=0,
            to_msg=lambda: SimpleNamespace(sec=0, nanosec=0),
        )


class _FakeLogger:
    def info(self, *args, **kwargs):
        pass

    def warn(self, *args, **kwargs):
        pass

    def warning(self, *args, **kwargs):
        pass

    def error(self, *args, **kwargs):
        pass


class _FakeNodeBase:
    def __init__(self, *args, **kwargs):
        self.created_publishers = []
        self.created_subscriptions = []
        self.created_timers = []
        self._fake_parameters = {
            "ownship_initial_heading_deg": 0.0,
            "ownship_initial_sog_kn": 10.0,
        }
        self._fake_logger = _FakeLogger()
        self._fake_clock = _FakeClock()

    def get_logger(self):
        return self._fake_logger

    def get_clock(self):
        return self._fake_clock

    def declare_parameter(self, name, value):
        self._fake_parameters.setdefault(name, value)
        return SimpleNamespace(value=self._fake_parameters[name])

    def get_parameter(self, name):
        return SimpleNamespace(value=self._fake_parameters.get(name, 0.0))

    def create_publisher(self, msg_type, topic, qos_profile):
        pub = _Publisher()
        pub.msg_type = msg_type
        pub.topic = topic
        pub.qos_profile = qos_profile
        self.created_publishers.append(pub)
        return pub

    def create_subscription(self, msg_type, topic, callback, qos_profile):
        sub = SimpleNamespace(
            msg_type=msg_type,
            topic=topic,
            callback=callback,
            qos_profile=qos_profile,
        )
        self.created_subscriptions.append(sub)
        return sub

    def create_timer(self, period_sec, callback):
        timer = SimpleNamespace(
            period_sec=period_sec,
            callback=callback,
            reset=lambda: None,
        )
        self.created_timers.append(timer)
        return timer

    def destroy_node(self):
        pass


def _install_fake_ros_modules(monkeypatch):
    rclpy = types.ModuleType("rclpy")
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = _FakeNodeBase
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
    path = Path(__file__).resolve().parents[2] / "docker" / "sil_topic_bridge.py"
    spec = importlib.util.spec_from_file_location("sil_topic_bridge_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_l4_adapter_enable_disables_bridge_actuator_publisher(monkeypatch):
    monkeypatch.setenv("SIL_L4_ADAPTER_ENABLE", "1")
    bridge = _load_bridge(monkeypatch)
    bridge.DebugTraceWriter = lambda node: SimpleNamespace(
        record=lambda *a, **k: None,
        reset=lambda: None,
        close=lambda: None,
    )

    node = bridge.SilTopicBridge()

    assert node._l4_adapter_enabled is True
    assert node._pub_act is None
    assert "/sil/actuator_cmd" not in {
        pub.topic for pub in node.created_publishers
    }


def test_avoidance_plan_rudder_command_is_published_in_radians(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock
    fake_self = SimpleNamespace(
        _pub_act=_Publisher(),
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 100.0,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
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
        _reset_latch_release_state=lambda: None
    )
    plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=12),
        waypoints=[SimpleNamespace(turn_radius_m=92.0, target_speed_kn=12.5)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)
    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(fake_self, plan.stamp)

    assert cmd.rudder_angle == -math.atan2(46.0, 92.0)
    assert cmd.throttle == 0.5


def test_avoidance_autopilot_closes_speed_loop_under_rudder_drag(monkeypatch):
    """Avoidance throttle must rise above feed-forward when own-ship is slow.

    Rule13 long-run regression: M5 requested 14 kn, but open-loop 14/25 throttle
    settled near 10 kn under sustained rudder drag, so M6 did not clear until the
    1200 s route-return deadline.
    """
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        _last_ownship_raw=SimpleNamespace(
            heading=0.0,
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _avoidance_target_heading_deg=None,
        _last_avoidance_waypoint=SimpleNamespace(
            turn_radius_m=500.0,
            target_speed_kn=14.0,
        ),
        _speed_controller=bridge.SpeedController(),
    )

    cmd = None
    for _ in range(4):
        cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
            fake_self, SimpleNamespace(sec=12)
        )

    assert cmd is not None
    assert cmd.throttle > (14.0 / bridge.MAX_SPEED_KN) + 0.05


def test_avoidance_autopilot_prefers_m5_waypoint_over_stale_m4_heading(monkeypatch):
    """Orange M5 route is the executable avoidance path.

    Regression from frontend COLREG runs: M5 drew a correct starboard route, but
    Bridge kept following a stale M4 heading window and commanded the wrong turn.
    """
    bridge = _load_bridge(monkeypatch)
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.0, longitude=0.01),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(80.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=25.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=waypoint,
        _last_avoidance_waypoints=[waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) < 0.0


def test_avoidance_autopilot_skips_stale_passed_m5_waypoint(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    stale_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.0, longitude=0.00004),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    next_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.005, longitude=0.00866),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(80.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=60.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=stale_waypoint,
        _last_avoidance_waypoints=[stale_waypoint, next_waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) > 0.0


def test_avoidance_autopilot_prefers_m5_waypoint_closest_to_m4_target(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    lower_edge_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.00819, longitude=0.00574),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    target_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.005, longitude=0.00866),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(40.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=60.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=lower_edge_waypoint,
        _last_avoidance_waypoints=[lower_edge_waypoint, target_waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) < 0.0


def test_avoidance_autopilot_accepts_m5_route_return_waypoint(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    route_return_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.0, longitude=-0.01),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(0.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=30.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=route_return_waypoint,
        _last_avoidance_waypoints=[route_return_waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) > 0.0


def test_avoidance_autopilot_rejects_m5_waypoint_near_reversal(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=-0.01, longitude=0.0001),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(100.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=60.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=waypoint,
        _last_avoidance_waypoints=[waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) > 0.0


def test_avoidance_autopilot_rejects_under_evasive_m5_rejoin_during_stand_on_conflict(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    rejoin_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.00819, longitude=0.00574),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(40.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _m6_conflict_active=True,
        _m6_primary_role=0,
        _m6_phase="INDEPENDENT_ACTION",
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=60.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=rejoin_waypoint,
        _last_avoidance_waypoints=[rejoin_waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) < 0.0


def test_avoidance_autopilot_rejects_m5_rejoin_waypoint_for_give_way_conflict(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    rejoin_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.00819, longitude=0.00574),
        turn_radius_m=500.0,
        target_speed_kn=12.0,
    )
    fake_self = SimpleNamespace(
        _make_actuator_msg=lambda stamp: bridge.SilTopicBridge._make_actuator_msg(fake_self, stamp),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _last_ownship_raw=SimpleNamespace(
            lat=0.0,
            lon=0.0,
            heading=math.radians(40.0),
            sog=10.0 / 1.94384,
            rot=0.0,
        ),
        _latch_release_triggered=False,
        _latch_release_time=None,
        _m6_conflict_active=True,
        _m6_primary_role=1,
        _m6_phase="GIVE_WAY",
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=60.0,
        _avoidance_heading_controller=bridge.HeadingController(max_rate_deg_s=100.0),
        _last_avoidance_waypoint=rejoin_waypoint,
        _last_avoidance_waypoints=[rejoin_waypoint],
        _speed_controller=bridge.SpeedController(),
    )

    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(
        fake_self, SimpleNamespace(sec=12)
    )

    assert math.degrees(cmd.rudder_angle) < 0.0


def test_placeholder_turn_radius_does_not_command_hard_rudder(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock
    fake_self = SimpleNamespace(
        _pub_act=_Publisher(),
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 100.0,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
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
        _reset_latch_release_state=lambda: None
    )
    plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=12),
        waypoints=[SimpleNamespace(turn_radius_m=0.0, target_speed_kn=12.5)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)
    cmd = bridge.SilTopicBridge._compute_avoidance_autopilot(fake_self, plan.stamp)

    assert cmd.rudder_angle == 0.0
    assert cmd.throttle == 0.4


def _avoidance_fake_self(bridge):
    """fake_self armed in avoidance with no target heading (open-loop deadzone)."""
    from unittest.mock import Mock
    return SimpleNamespace(
        _record_pulse=lambda module_id: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        _get_sim_time=lambda: 100.0,
        _autopilot_enabled=False,
        _avoidance_active=True,
        _avoidance_target_heading_deg=None,
        _last_behavior_plan=None,
        _last_valid_plan_time=0.0,
        _last_avoidance_waypoint=None,
        _latch_release_triggered=False,
        _avoidance_armed_time=None,          # → _latch_hold_elapsed() True
        _LATCH_MIN_HOLD_S=8.0,
        _avoidance_heading_controller=Mock(),
        _latch_hold_elapsed=lambda: True,
        _reset_latch_release_state=lambda: None,
    )


def test_empty_plan_disarms_avoidance_regardless_of_autopilot(monkeypatch):
    """M5 EMPTY plan must tear down avoidance even when transit autopilot is off.

    Reproduces the spin trap: avoidance stays armed with target=None forever
    because the disarm was gated on _autopilot_enabled (False during avoidance).
    """
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    empty_plan = SimpleNamespace(stamp=SimpleNamespace(sec=1), waypoints=[])

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, empty_plan)

    assert fake_self._avoidance_active is False


def test_avoidance_tears_down_when_m4_returns_to_transit(monkeypatch):
    """M4 (COLREG authority) returning to TRANSIT ends avoidance once held for
    _AVOID_TRANSIT_RELEASE_S, even if M5 keeps emitting a stub geometric plan
    (target_heading None). A single transient TRANSIT cycle must NOT tear down."""
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._AVOID_TRANSIT_RELEASE_S = 3.0
    clock = {"t": 100.0}
    fake_self._get_sim_time = lambda: clock["t"]
    transit_plan = SimpleNamespace(
        behavior=0, heading_min_deg=0.0, heading_max_deg=360.0, rationale="transit",
    )

    # First TRANSIT cycle: starts the timer, must NOT tear down yet.
    bridge.SilTopicBridge._on_behavior_plan(fake_self, transit_plan)
    assert fake_self._avoidance_active is True

    # Sustained TRANSIT past the release window: tears down.
    clock["t"] = 104.0
    bridge.SilTopicBridge._on_behavior_plan(fake_self, transit_plan)
    assert fake_self._avoidance_active is False


def test_behavior_plan_transit_does_not_interrupt_latch_release_decay(monkeypatch):
    """M4 TRANSIT must not tear down avoidance while release smoothing is active."""
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = 96.0
    fake_self._AVOID_TRANSIT_RELEASE_S = 3.0
    fake_self._latch_release_triggered = True
    fake_self._latch_release_time = 99.0
    fake_self._latch_offset_at_release_deg = 150.0
    fake_self._avoidance_target_heading_deg = 120.0
    fake_self._get_sim_time = lambda: 100.0
    transit_plan = SimpleNamespace(
        behavior=0, heading_min_deg=355.0, heading_max_deg=359.0, rationale="transit",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, transit_plan)

    assert fake_self._avoidance_active is True
    assert fake_self._avoidance_target_heading_deg == 120.0
    assert fake_self._latch_release_triggered is True


def test_behavior_plan_tracking_locks_before_reversal_window(monkeypatch):
    """Once Bridge reaches the design alteration, it must not chase late M4
    windows toward a reversal."""
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._target_heading_deg = 0.0
    fake_self._avoidance_target_heading_deg = bridge.M4_AVOID_TARGET_LOCK_DELTA_DEG

    avoid_plan = SimpleNamespace(
        behavior=1,
        heading_min_deg=154.0,
        heading_max_deg=184.0,
        rationale="COLREG_AVOID",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, avoid_plan)

    assert fake_self._avoidance_target_heading_deg == pytest.approx(
        bridge.M4_AVOID_TARGET_LOCK_DELTA_DEG
    )


def test_behavior_plan_tracking_allows_rejoin_window_after_large_turn(monkeypatch):
    """A large locked avoidance target must still accept later M4 windows that
    reduce absolute deviation back toward the route."""
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._target_heading_deg = 0.0
    fake_self._avoidance_target_heading_deg = bridge.M4_AVOID_TARGET_LOCK_DELTA_DEG

    rejoin_plan = SimpleNamespace(
        behavior=1,
        heading_min_deg=65.0,
        heading_max_deg=95.0,
        rationale="COLREG_AVOID",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, rejoin_plan)

    assert fake_self._avoidance_target_heading_deg == pytest.approx(90.0)


def test_behavior_plan_does_not_reduce_avoidance_target_while_m6_conflict_active(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._m6_conflict_active = True
    fake_self._target_heading_deg = 0.0
    fake_self._avoidance_target_heading_deg = 60.0

    premature_rejoin_plan = SimpleNamespace(
        behavior=1,
        heading_min_deg=0.0,
        heading_max_deg=30.0,
        rationale="COLREG_AVOID",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, premature_rejoin_plan)

    assert fake_self._avoidance_target_heading_deg == pytest.approx(60.0)


def test_behavior_plan_tracking_recovers_from_crossed_rejoin_lock(monkeypatch):
    """A stale target past 180 deg must not ignore a new starboard-side M4 window.

    Regression from A4000 rule15-cs frontend run:
    M4 window=[63.4,93.4], bridge target stayed at 206.4, so rudder stayed near 0.
    """
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._target_heading_deg = 0.0
    fake_self._avoidance_target_heading_deg = 206.3682098388672

    rejoin_plan = SimpleNamespace(
        behavior=1,
        heading_min_deg=63.427249908447266,
        heading_max_deg=93.42724609375,
        rationale="COLREG_AVOID",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, rejoin_plan)

    assert fake_self._avoidance_target_heading_deg == pytest.approx(88.42724672953287)


def test_behavior_plan_tracking_does_not_chase_rolled_m4_window(monkeypatch):
    """Once a large starboard target is set, Bridge must not chase current-heading
    M4 windows into the opposite absolute heading side."""
    bridge = _load_bridge(monkeypatch)
    fake_self = _avoidance_fake_self(bridge)
    fake_self._transit_since_time = None
    fake_self._target_heading_deg = 0.0
    fake_self._avoidance_target_heading_deg = 154.0

    rolled_plan = SimpleNamespace(
        behavior=1,
        heading_min_deg=269.0,
        heading_max_deg=299.0,
        rationale="COLREG_AVOID",
    )

    bridge.SilTopicBridge._on_behavior_plan(fake_self, rolled_plan)

    assert fake_self._avoidance_target_heading_deg == pytest.approx(154.0)


def test_avoidance_plan_arm_uses_starboard_edge_before_180(monkeypatch):
    """Bridge arm path must not turn a valid M4 starboard window into a reversal."""
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock

    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        _get_sim_time=lambda: 100.0,
        _autopilot_enabled=False,
        _avoidance_active=False,
        _avoidance_armed_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=None,
        _last_behavior_plan=SimpleNamespace(
            behavior=1,
            heading_min_deg=154.0,
            heading_max_deg=184.0,
        ),
        _last_valid_plan_time=0.0,
        _last_avoidance_waypoint=None,
        _latch_release_triggered=False,
        _avoidance_heading_controller=Mock(last_cmd_deg=0.0),
        _reset_latch_release_state=lambda: None,
    )
    valid_plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=1),
        waypoints=[SimpleNamespace(turn_radius_m=500.0, target_speed_kn=10.0)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, valid_plan)

    assert fake_self._avoidance_active is True
    assert fake_self._avoidance_target_heading_deg == pytest.approx(154.0)


def test_avoidance_plan_does_not_arm_while_m4_is_transit(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock

    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        _get_sim_time=lambda: 100.0,
        _autopilot_enabled=False,
        _avoidance_active=False,
        _avoidance_armed_time=None,
        _target_heading_deg=0.0,
        _avoidance_target_heading_deg=None,
        _last_behavior_plan=SimpleNamespace(
            behavior=0,
            heading_min_deg=0.0,
            heading_max_deg=360.0,
        ),
        _last_valid_plan_time=0.0,
        _last_avoidance_waypoint=None,
        _last_avoidance_waypoints=[],
        _latch_release_triggered=False,
        _avoidance_heading_controller=Mock(last_cmd_deg=0.0),
        _reset_latch_release_state=lambda: None,
    )
    valid_plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=1),
        waypoints=[SimpleNamespace(turn_radius_m=500.0, target_speed_kn=10.0)],
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, valid_plan)

    assert fake_self._avoidance_active is False
    assert fake_self._avoidance_target_heading_deg is None


def test_sil_own_ship_state_is_converted_to_l3_units(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _pub_foss=_Publisher(),
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 100.0,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
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


def test_route_xte_uses_current_target_waypoint_segment(monkeypatch):
    """Transit XTE must use the current route leg, not the full-route chord.

    A dogleg route from WP0 east to WP1, then north to WP2, should have near-zero
    XTE when own-ship sits on the WP1->WP2 leg. Using the full WP0->WP2 chord
    incorrectly creates a large diagonal-track error and pulls the vessel off the
    complete nominal route.
    """
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _route_wps=[
            (0.0, 0.0),
            (0.0, 1.0),
            (1.0, 1.0),
        ],
        _current_target_wp_lat=1.0,
        _current_target_wp_lon=1.0,
    )

    xte_m = bridge.SilTopicBridge._signed_xte_m(fake_self, 0.5, 1.0)

    assert xte_m == pytest.approx(0.0, abs=1.0)


def test_bridge_uses_reliable_volatile_qos_for_l3_consumers(monkeypatch):
    bridge = _load_bridge(monkeypatch)

    qos = bridge._reliable_volatile_qos(depth=7)

    assert qos["reliability"] == bridge.QoSReliabilityPolicy.RELIABLE
    assert qos["durability"] == bridge.QoSDurabilityPolicy.VOLATILE
    assert qos["depth"] == 7


# ── P1: ADR-1 M6 conflict authority tests ─────────────────────────────────

@pytest.mark.parametrize("autopilot_enabled", [False, True])
def test_m5_empty_plan_does_not_release_while_m6_conflict_active(
    monkeypatch, autopilot_enabled
):
    """Empty M5 plan must not clear avoidance while M6 still owns conflict."""
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock

    sentinel = object()
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 100.0,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _autopilot_enabled=autopilot_enabled,
        _avoidance_active=True,
        _m6_conflict_active=True,
        _avoidance_target_heading_deg=20.0,
        _last_behavior_plan=None,
        _last_valid_plan_time=90.0,
        _last_avoidance_waypoint=sentinel,
        _latch_release_triggered=False,
        _avoidance_armed_time=80.0,
        _avoidance_heading_controller=Mock(last_cmd_deg=0.0),
        _reset_latch_release_state=lambda: None,
    )
    empty_plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=1),
        waypoints=[],
        speed_adjustments=[],
        confidence=1.0,
        status="NORMAL",
        rationale="M4 TRANSIT - no avoidance required",
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, empty_plan)

    assert fake_self._avoidance_active is True
    assert fake_self._avoidance_target_heading_deg == 20.0
    assert fake_self._last_avoidance_waypoint is sentinel


@pytest.mark.parametrize("autopilot_enabled", [False, True])
def test_m5_empty_plan_does_not_interrupt_latch_release_decay(
    monkeypatch, autopilot_enabled
):
    """Once release smoothing is active, empty M5 plans must not force an abrupt
    transition to transit steering."""
    bridge = _load_bridge(monkeypatch)
    from unittest.mock import Mock

    sentinel = object()
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 190.0,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
        _autopilot_enabled=autopilot_enabled,
        _avoidance_active=True,
        _m6_conflict_active=False,
        _avoidance_target_heading_deg=120.0,
        _last_behavior_plan=SimpleNamespace(behavior=0),
        _last_valid_plan_time=188.0,
        _last_avoidance_waypoint=sentinel,
        _latch_release_triggered=True,
        _latch_release_time=189.0,
        _latch_offset_at_release_deg=150.0,
        _avoidance_armed_time=30.0,
        _avoidance_heading_controller=Mock(last_cmd_deg=0.0),
        _reset_latch_release_state=lambda: None,
    )
    empty_plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=1),
        waypoints=[],
        speed_adjustments=[],
        confidence=1.0,
        status="NORMAL",
        rationale="M4 TRANSIT - release smoothing",
    )

    bridge.SilTopicBridge._on_avoidance_plan(fake_self, empty_plan)

    assert fake_self._avoidance_active is True
    assert fake_self._avoidance_target_heading_deg == 120.0
    assert fake_self._last_avoidance_waypoint is sentinel


def _make_check_geometry_self(
        bridge, *, m6_conflict_active, avoidance_armed_t=80.0,
        release_fallback_enabled=False):
    """Minimal fake_self for _check_geometry_release tests."""
    return SimpleNamespace(
        _avoidance_active=True,
        _latch_release_triggered=False,
        _m6_conflict_active=m6_conflict_active,
        _last_ownship_raw=None,   # will be overridden in test
        _last_target_vessel_raw=None,  # will be overridden
        _avoidance_armed_time=avoidance_armed_t,
        _bridge_release_fallback_enabled=release_fallback_enabled,
        _LATCH_MIN_HOLD_S=8.0,
        _latch_hold_elapsed=lambda: True,  # hold elapsed
        _get_sim_time=lambda: 100.0,
        _trigger_latch_release=lambda: None,  # sentinel — we override in test
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
    )


def _make_oss_ns(lat=63.44, lon=10.38, heading_rad=0.0,
                 sog_ms=5.14, cog_rad=0.0, rot_rad=0.0):
    """Fake SilOwnShipState-like namespace for CPA computation."""
    return SimpleNamespace(
        lat=lat, lon=lon,
        heading=heading_rad, sog=sog_ms, cog=cog_rad, rot=rot_rad,
    )


def _make_tvs_ns(lat=63.50, lon=10.38, heading_rad=math.pi,
                 sog_ms=5.14, cog_rad=math.pi):
    """Fake TargetVesselState-like namespace for CPA computation."""
    return SimpleNamespace(
        lat=lat, lon=lon,
        heading=heading_rad, sog=sog_ms, cog=cog_rad,
    )


def test_geometry_release_blocked_while_m6_conflict_active(monkeypatch):
    """_check_geometry_release must NOT fire while M6 conflict_detected=True.

    Root cause of ot toggles=126 (fbe100c4): own-ship avoiding action pushed
    target past CPA (TCPA<0) while M6 still held conflict. Bridge was
    independently firing geometry release every ~6s, overriding M6 authority
    (ADR-1). This test is the regression lock for that fix.
    """
    bridge = _load_bridge(monkeypatch)
    released = []
    fake_self = _make_check_geometry_self(bridge, m6_conflict_active=True)
    fake_self._trigger_latch_release = lambda: released.append(True)

    # Geometry that would normally trigger release: ships moving away from each other
    # (target has passed abeam/astern of own-ship).
    fake_self._last_ownship_raw = _make_oss_ns(lat=63.44, lon=10.38,
                                                sog_ms=5.14, cog_rad=0.0)
    fake_self._last_target_vessel_raw = _make_tvs_ns(lat=63.50, lon=10.38,
                                                      sog_ms=5.14, cog_rad=math.pi)

    bridge.SilTopicBridge._check_geometry_release(fake_self)

    assert len(released) == 0, (
        "Geometry release fired while M6 conflict_detected=True — ADR-1 violation. "
        "This is the root cause of ot toggles=126."
    )


def test_geometry_release_condition_is_trace_only_when_m6_conflict_cleared(monkeypatch):
    """Old bridge geometry release is trace-only even after M6 clears."""
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    fake_self = _make_check_geometry_self(bridge, m6_conflict_active=False)
    fake_self._trigger_latch_release = lambda: released.append(True)
    fake_self._latch_release_time = None
    fake_self._latch_offset_at_release_deg = None
    fake_self._latch_release_progress = 0.0
    fake_self.get_logger = lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg))
    fake_self._last_ownship_raw = _make_oss_ns()
    fake_self._last_target_vessel_raw = _make_tvs_ns()
    monkeypatch.setattr(
        bridge.SilTopicBridge,
        "_compute_dcpa_tcpa",
        staticmethod(lambda own, target: (bridge.CPA_SAFE_M + 1.0, -1.0)),
    )

    bridge.SilTopicBridge._check_geometry_release(fake_self)

    assert released == []
    assert fake_self._latch_release_triggered is False
    assert fake_self._latch_release_time is None
    assert fake_self._latch_offset_at_release_deg is None
    assert fake_self._latch_release_progress == 0.0
    assert any("Geometry release candidate (trace-only)" in msg for msg in logs)


def test_geometry_release_condition_uses_compat_fallback_when_enabled(monkeypatch):
    """Legacy geometry release still exists behind an explicit fallback flag."""
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    fake_self = _make_check_geometry_self(
        bridge, m6_conflict_active=False, release_fallback_enabled=True)
    fake_self._trigger_latch_release = lambda: released.append(True)
    fake_self.get_logger = lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg))
    fake_self._last_ownship_raw = _make_oss_ns()
    fake_self._last_target_vessel_raw = _make_tvs_ns()
    monkeypatch.setattr(
        bridge.SilTopicBridge,
        "_compute_dcpa_tcpa",
        staticmethod(lambda own, target: (bridge.CPA_SAFE_M + 1.0, -1.0)),
    )

    bridge.SilTopicBridge._check_geometry_release(fake_self)

    assert released == [True]
    assert any("Geometry release candidate (compat fallback)" in msg for msg in logs)


def test_threat_state_release_condition_is_trace_only(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _m6_conflict_active=False,
        _bridge_release_fallback_enabled=False,
        _latch_release_triggered=False,
        _latch_hold_elapsed=lambda: True,
        _trigger_latch_release=lambda: released.append(True),
        _latch_release_time=None,
        _latch_offset_at_release_deg=None,
        _latch_release_progress=0.0,
        get_logger=lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg)),
    )
    msg = SimpleNamespace(cpa_status="cleared", target_relative_position="astern")

    bridge.SilTopicBridge._on_threat_state(fake_self, msg)

    assert released == []
    assert fake_self._latch_release_triggered is False
    assert fake_self._latch_release_time is None
    assert fake_self._latch_offset_at_release_deg is None
    assert fake_self._latch_release_progress == 0.0
    assert any("condition 1 candidate (trace-only)" in msg for msg in logs)


def test_threat_state_release_condition_uses_compat_fallback_when_enabled(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _m6_conflict_active=False,
        _bridge_release_fallback_enabled=True,
        _latch_release_triggered=False,
        _latch_hold_elapsed=lambda: True,
        _trigger_latch_release=lambda: released.append(True),
        get_logger=lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg)),
    )
    msg = SimpleNamespace(cpa_status="cleared", target_relative_position="astern")

    bridge.SilTopicBridge._on_threat_state(fake_self, msg)

    assert released == [True]
    assert any("condition 1 candidate (compat fallback)" in msg for msg in logs)


def test_mission_goal_release_condition_is_trace_only(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    trace_rows = []
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _trace_writer=SimpleNamespace(record=lambda *args: trace_rows.append(args)),
        _get_sim_time=lambda: 100.0,
        _m6_conflict_active=False,
        _bridge_release_fallback_enabled=False,
        _last_behavior_plan=SimpleNamespace(behavior=0),
        _latch_release_triggered=False,
        _latch_hold_elapsed=lambda: True,
        _trigger_latch_release=lambda: released.append(True),
        _latch_release_time=None,
        _latch_offset_at_release_deg=None,
        _latch_release_progress=0.0,
        _current_target_wp_lat=0.0,
        _current_target_wp_lon=0.0,
        _m3_activated_once=False,
        get_logger=lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg)),
    )
    msg = SimpleNamespace(
        fsm_state=3,
        task_validity=1,
        current_target_wp=SimpleNamespace(latitude=63.5, longitude=10.4),
    )

    bridge.SilTopicBridge._on_mission_goal(fake_self, msg)

    assert released == []
    assert fake_self._latch_release_triggered is False
    assert fake_self._latch_release_time is None
    assert fake_self._latch_offset_at_release_deg is None
    assert fake_self._latch_release_progress == 0.0
    assert trace_rows
    assert any("condition 2 candidate (trace-only)" in msg for msg in logs)


def test_mission_goal_release_condition_uses_compat_fallback_when_enabled(monkeypatch):
    bridge = _load_bridge(monkeypatch)
    released = []
    logs = []
    trace_rows = []
    fake_self = SimpleNamespace(
        _record_pulse=lambda module_id: None,
        _trace_writer=SimpleNamespace(record=lambda *args: trace_rows.append(args)),
        _get_sim_time=lambda: 100.0,
        _m6_conflict_active=False,
        _bridge_release_fallback_enabled=True,
        _last_behavior_plan=SimpleNamespace(behavior=0),
        _latch_release_triggered=False,
        _latch_hold_elapsed=lambda: True,
        _trigger_latch_release=lambda: released.append(True),
        _current_target_wp_lat=0.0,
        _current_target_wp_lon=0.0,
        _m3_activated_once=False,
        get_logger=lambda: SimpleNamespace(info=lambda msg, *a, **k: logs.append(msg)),
    )
    msg = SimpleNamespace(
        fsm_state=3,
        task_validity=1,
        current_target_wp=SimpleNamespace(latitude=63.5, longitude=10.4),
    )

    bridge.SilTopicBridge._on_mission_goal(fake_self, msg)

    assert released == [True]
    assert trace_rows
    assert any("condition 2 candidate (compat fallback)" in msg for msg in logs)


def test_arm_avoidance_from_m6_arms_when_m4_in_avoid(monkeypatch):
    """_arm_avoidance_from_m6() arms avoidance when M4 is in COLREG_AVOID mode."""
    bridge = _load_bridge(monkeypatch)
    logged = []
    fake_self = SimpleNamespace(
        _avoidance_active=False,
        _last_behavior_plan=SimpleNamespace(behavior=1),  # COLREG_AVOID
        _get_sim_time=lambda: 100.0,
        _avoidance_armed_time=None,
        _reset_latch_release_state=lambda: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: logged.append(a)),
    )

    bridge.SilTopicBridge._arm_avoidance_from_m6(fake_self)

    assert fake_self._avoidance_active is True, \
        "_arm_avoidance_from_m6 did not arm avoidance when M4 is COLREG_AVOID"
    assert fake_self._avoidance_armed_time == 100.0


def test_arm_avoidance_from_m6_noop_when_m4_in_transit(monkeypatch):
    """_arm_avoidance_from_m6() must NOT arm when M4 is still in TRANSIT."""
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _avoidance_active=False,
        _last_behavior_plan=SimpleNamespace(behavior=0),  # TRANSIT
        _get_sim_time=lambda: 100.0,
        _avoidance_armed_time=None,
        _reset_latch_release_state=lambda: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
    )

    bridge.SilTopicBridge._arm_avoidance_from_m6(fake_self)

    assert fake_self._avoidance_active is False, \
        "_arm_avoidance_from_m6 armed avoidance while M4 is TRANSIT — must wait for M4"


def test_arm_avoidance_from_m6_noop_when_already_armed(monkeypatch):
    """_arm_avoidance_from_m6() is idempotent: noop when already armed."""
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _avoidance_active=True,  # already armed
        _last_behavior_plan=SimpleNamespace(behavior=1),
        _get_sim_time=lambda: 100.0,
        _avoidance_armed_time=90.0,
        _reset_latch_release_state=lambda: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
    )

    bridge.SilTopicBridge._arm_avoidance_from_m6(fake_self)

    # armed_time must remain at original value (not reset)
    assert fake_self._avoidance_armed_time == 90.0, \
        "_arm_avoidance_from_m6 re-armed when already active (not idempotent)"


def test_on_colregs_constraint_updates_m6_conflict_active(monkeypatch):
    """_on_colregs_constraint must update _m6_conflict_active from msg.conflict_detected."""
    bridge = _load_bridge(monkeypatch)
    fake_self = SimpleNamespace(
        _m6_conflict_active=False,
        _m6_conflict_last_t=None,
        _m6_primary_role=None,
        _m6_phase="",
        _avoidance_active=False,
        _last_behavior_plan=None,
        _record_pulse=lambda module_id: None,
        _get_sim_time=lambda: 42.5,
        _trace_writer=SimpleNamespace(record=lambda *a, **k: None),
        _arm_avoidance_from_m6=lambda: None,
        get_logger=lambda: SimpleNamespace(info=lambda *a, **k: None),
    )
    msg = SimpleNamespace(
        conflict_detected=True,
        primary_role=1,
        phase="GIVE_WAY",
        primary_preferred_direction="STARBOARD",
        confidence=0.95,
    )

    bridge.SilTopicBridge._on_colregs_constraint(fake_self, msg)

    assert fake_self._m6_conflict_active is True
    assert fake_self._m6_conflict_last_t == 42.5
    assert fake_self._m6_primary_role == 1
    assert fake_self._m6_phase == "GIVE_WAY"
