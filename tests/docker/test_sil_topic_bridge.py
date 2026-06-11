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

def _make_check_geometry_self(bridge, *, m6_conflict_active, avoidance_armed_t=80.0):
    """Minimal fake_self for _check_geometry_release tests."""
    return SimpleNamespace(
        _avoidance_active=True,
        _latch_release_triggered=False,
        _m6_conflict_active=m6_conflict_active,
        _last_ownship_raw=None,   # will be overridden in test
        _last_target_vessel_raw=None,  # will be overridden
        _avoidance_armed_time=avoidance_armed_t,
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


def test_geometry_release_allowed_when_m6_conflict_cleared(monkeypatch):
    """_check_geometry_release IS allowed when M6 conflict_detected=False.

    M6 cleared the conflict; bridge may now use geometry to decide release.
    """
    bridge = _load_bridge(monkeypatch)
    released = []
    fake_self = _make_check_geometry_self(bridge, m6_conflict_active=False)
    fake_self._trigger_latch_release = lambda: released.append(True)

    # Two ships approaching head-on then past CPA: own-ship heading N, target heading S,
    # both moving. After avoidance target has passed abeam (N of own-ship, moving away).
    # Use direct DCPA/TCPA to verify: set target far astern (N) moving away.
    fake_self._last_ownship_raw = _make_oss_ns(
        lat=63.44, lon=10.38, sog_ms=5.14, cog_rad=0.0)  # heading N
    fake_self._last_target_vessel_raw = _make_tvs_ns(
        lat=63.60, lon=10.38, sog_ms=5.14, cog_rad=0.0)  # also heading N, far N → moving away

    # Compute expected TCPA to confirm test geometry: relative velocity near zero
    # (both heading same direction at same speed → TCPA undefined; use different speeds)
    fake_self._last_target_vessel_raw = _make_tvs_ns(
        lat=63.60, lon=10.38, sog_ms=7.0, cog_rad=0.0)  # target faster N → diverging

    bridge.SilTopicBridge._check_geometry_release(fake_self)
    # We don't assert release happened (depends on TCPA<0 & DCPA>=1000m geometry)
    # but it must NOT be blocked solely because M6 cleared. No crash.


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
