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
    _no_op_writer = SimpleNamespace(record=lambda *a, **kw: None)
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
        _trace_writer=_no_op_writer,
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9
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
    _no_op_writer = SimpleNamespace(record=lambda *a, **kw: None)
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
        _trace_writer=_no_op_writer,
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
        get_logger=lambda: SimpleNamespace(info=lambda s: None),
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9
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
    _no_op_writer = SimpleNamespace(record=lambda *a, **kw: None)
    fake_self = SimpleNamespace(
        _pub_foss=_Publisher(),
        _record_pulse=lambda module_id: None,
        _trace_writer=_no_op_writer,
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9
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


# ── Task 1 tests: Lock in Fix#1 (decouple) + G1 (continuous tracking) ────────


def test_avoidance_target_refreshes_with_m4_window(monkeypatch):
    """G1: avoidance target must refresh every M4 message while active.

    Previously the target was latched once (only set when `_avoidance_target_heading_deg
    is None`). This test proves that feeding a new behavior_plan window updates the
    target even after the first window has already been captured.
    """
    bridge = _load_bridge(monkeypatch)

    # Build a minimal fake_self that is in the avoidance-active state
    fake_self = SimpleNamespace(
        _avoidance_active=True,
        _avoidance_target_heading_deg=None,
        _latch_release_triggered=False,
        _record_pulse=lambda mid: None,
        _last_behavior_plan=None,
        _trace_writer=SimpleNamespace(record=lambda *a, **kw: None),
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9

    # First M4 window: [16, 34] → 5/6 * 18 = 15 → target = 16+15 = 31 °
    msg1 = SimpleNamespace(
        behavior=1,  # COLREG_AVOID (non-zero)
        heading_min_deg=16.0,
        heading_max_deg=34.0,
    )
    bridge.SilTopicBridge._on_behavior_plan(fake_self, msg1)
    first_target = fake_self._avoidance_target_heading_deg
    assert first_target is not None
    assert abs(first_target - (16.0 + (5.0 / 6.0) * 18.0)) < 0.5, (
        f"Expected ~31°, got {first_target:.2f}°"
    )

    # Second M4 window grows: [16, 68] → 5/6 * 52 ≈ 43.3 → target ≈ 59.3°
    msg2 = SimpleNamespace(
        behavior=1,
        heading_min_deg=16.0,
        heading_max_deg=68.0,
    )
    bridge.SilTopicBridge._on_behavior_plan(fake_self, msg2)
    second_target = fake_self._avoidance_target_heading_deg
    expected2 = (16.0 + (5.0 / 6.0) * (68.0 - 16.0)) % 360.0
    assert second_target is not None
    assert abs(second_target - expected2) < 0.5, (
        f"G1 FAIL: target did not refresh from {first_target:.1f}° to ~{expected2:.1f}°; "
        f"got {second_target:.2f}°. (One-shot latch bug still present?)"
    )
    assert second_target > first_target + 5.0, (
        "Target should have grown significantly with the wider window"
    )


def test_mission_goal_inactive_does_not_teardown_avoidance(monkeypatch):
    """Fix#1: M3 fsm_state<3 (AwaitingRoute) must NOT reset _avoidance_active.

    The pre-fix code reset _avoidance_active whenever fsm_state<3, which
    permanently suppressed avoidance when M3 never reached ACTIVE (K1 keystone).
    After Fix#1, only _current_target_wp_* is reset; avoidance survives.
    """
    bridge = _load_bridge(monkeypatch)

    saved_target = 45.0
    fake_self = SimpleNamespace(
        _avoidance_active=True,
        _avoidance_target_heading_deg=saved_target,
        _latch_release_triggered=False,
        _last_behavior_plan=None,
        _m3_activated_once=False,
        _current_target_wp_lat=10.0,
        _current_target_wp_lon=5.0,
        _record_pulse=lambda mid: None,
        _trace_writer=SimpleNamespace(record=lambda *a, **kw: None),
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=20_000_000_000, to_msg=lambda: None)
        ),
        _avoidance_armed_time=None,
        _LATCH_MIN_HOLD_S=8.0,
        _latch_hold_elapsed=lambda: False,
        _trigger_latch_release=lambda: None,
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9

    # Simulate 5 rapid M3 messages at fsm_state=1 (AwaitingRoute)
    for _ in range(5):
        msg = SimpleNamespace(
            fsm_state=1,
            task_validity=0,
            current_target_wp=SimpleNamespace(latitude=0.0, longitude=0.0),
        )
        bridge.SilTopicBridge._on_mission_goal(fake_self, msg)

    assert fake_self._avoidance_active is True, (
        "Fix#1 FAIL: _avoidance_active was reset by M3 fsm_state<3. "
        "Decouple from mission FSM not working."
    )
    assert fake_self._avoidance_target_heading_deg == saved_target, (
        "avoidance target heading was reset by mission goal callback"
    )
    # current_target_wp should be zeroed (that's the only side-effect allowed)
    assert fake_self._current_target_wp_lat == 0.0
    assert fake_self._current_target_wp_lon == 0.0


def test_arm_not_gated_on_m3(monkeypatch):
    """Fix#1: avoidance must arm on first valid M5 plan past MIN_ARM_SIM_T,
    regardless of _m3_activated_once flag.

    The pre-fix code blocked arming until M3 had reached FSM_ACTIVE at least
    once (`_m3_activated_once=True`). This test confirms the gate is removed:
    even with _m3_activated_once=False, a valid plan past 10s arms avoidance.
    """
    bridge = _load_bridge(monkeypatch)

    armed_time_capture = []

    fake_self = SimpleNamespace(
        _avoidance_active=False,
        _avoidance_target_heading_deg=None,
        _m3_activated_once=False,  # the old guard; must be irrelevant now
        _autopilot_enabled=False,
        _last_behavior_plan=SimpleNamespace(
            behavior=1,  # COLREG_AVOID
            heading_min_deg=16.0,
            heading_max_deg=34.0,
        ),
        _avoidance_armed_time=None,
        _last_valid_plan_time=None,
        _last_avoidance_waypoint=None,
        _avoidance_heading_controller=SimpleNamespace(last_cmd_deg=0.0),
        _reset_latch_release_state=lambda: None,
        _latch_release_triggered=False,
        _record_pulse=lambda mid: None,
        _trace_writer=SimpleNamespace(record=lambda *a, **kw: None),
        # sim_time = 15s → past _MIN_ARM_SIM_T=10s
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
        get_logger=lambda: SimpleNamespace(info=lambda s: None),
    )
    fake_self._get_sim_time = lambda: fake_self.get_clock().now().nanoseconds * 1e-9

    def capture_armed_time():
        armed_time_capture.append(fake_self.get_clock().now().nanoseconds * 1e-9)

    fake_self._reset_latch_release_state = capture_armed_time

    plan = SimpleNamespace(
        waypoints=[SimpleNamespace(turn_radius_m=500.0, target_speed_kn=10.0)],
    )
    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)

    assert fake_self._avoidance_active is True, (
        "Fix#1 FAIL: avoidance did not arm despite valid M5 plan past sim_t guard. "
        "_m3_activated_once is still blocking arming."
    )
    assert fake_self._avoidance_target_heading_deg is not None, (
        "avoidance_target_heading_deg should be set from M4 window at arm time"
    )

# ── Task 2 tests: Geometry release + turn bounding (G3/G4) ───────────────────
#
# Written FAILING first (TDD red phase). Pass after implementing bridge changes.


def _make_ownship_raw(lat=63.44, lon=10.38, heading_deg=0.0, sog_kn=10.0, cog_deg=None):
    import math
    cog_deg = cog_deg if cog_deg is not None else heading_deg
    return SimpleNamespace(
        lat=lat, lon=lon,
        heading=math.radians(heading_deg),
        sog=sog_kn / 1.94384,
        cog=math.radians(cog_deg),
        rot=0.0, u=sog_kn / 1.94384, v=0.0, r=0.0,
        rudder_angle=0.0, throttle=0.0,
    )


def _make_target_raw(lat=63.46, lon=10.38, heading_deg=180.0, sog_kn=10.0, cog_deg=None):
    import math
    cog_deg = cog_deg if cog_deg is not None else heading_deg
    return SimpleNamespace(
        lat=lat, lon=lon,
        heading=math.radians(heading_deg),
        sog=sog_kn / 1.94384,
        cog=math.radians(cog_deg),
        rot=0.0,
    )


def test_bridge_computes_dcpa_tcpa_locally(monkeypatch):
    """G3: SilTopicBridge._compute_dcpa_tcpa(own, target) must return (dcpa_m, tcpa_s).

    ThreatState only has string fields — no numeric CPA/TCPA. The bridge must
    compute DCPA/TCPA locally from _last_ownship_raw + _last_target_vessel_raw.
    """
    bridge = _load_bridge(monkeypatch)

    assert hasattr(bridge.SilTopicBridge, "_compute_dcpa_tcpa"), (
        "G3 FAIL: SilTopicBridge._compute_dcpa_tcpa not found. "
        "Must add bridge-local CPA computation (ThreatState has no numeric fields)."
    )

    own = _make_ownship_raw(lat=63.440, lon=10.380, heading_deg=0.0, sog_kn=10.0)
    # Target ~2.2 nm ahead, heading S (head-on approaching)
    tgt = _make_target_raw(lat=63.476, lon=10.380, heading_deg=180.0, sog_kn=10.0)

    dcpa_m, tcpa_s = bridge.SilTopicBridge._compute_dcpa_tcpa(own, tgt)

    assert tcpa_s > 0, f"TCPA must be >0 for approaching head-on target, got {tcpa_s:.1f}s"
    assert dcpa_m >= 0, f"DCPA must be >=0, got {dcpa_m:.1f}m"
    assert dcpa_m < 500, f"Head-on DCPA should be near 0 (<500m), got {dcpa_m:.1f}m"


def test_release_on_geometry_not_timer(monkeypatch):
    """G3: _check_geometry_release() must fire only when TCPA<0 AND DCPA>=cpa_safe.

    Threat still closing (TCPA>0): release must NOT fire even with >5s elapsed.
    Threat past CPA and DCPA safe: release MUST fire.
    """
    bridge = _load_bridge(monkeypatch)

    assert hasattr(bridge.SilTopicBridge, "_check_geometry_release"), (
        "G3 FAIL: _check_geometry_release() not found. Must implement geometry release."
    )

    # Scenario A: closing target at 20s — must NOT release
    release_a = []
    fs_a = SimpleNamespace(
        _avoidance_active=True,
        _avoidance_target_heading_deg=30.0,
        _target_heading_deg=0.0,
        _latch_release_triggered=False,
        _last_ownship_raw=_make_ownship_raw(lat=63.440, lon=10.381, heading_deg=30.0, sog_kn=10.0),
        _last_target_vessel_raw=_make_target_raw(lat=63.456, lon=10.384, heading_deg=210.0, sog_kn=10.0),
        get_logger=lambda: SimpleNamespace(info=lambda s: None, warning=lambda s: None),
        _trigger_latch_release=lambda: release_a.append(True),
        _latch_hold_elapsed=lambda: True,
    )
    bridge.SilTopicBridge._check_geometry_release(fs_a)
    assert len(release_a) == 0, (
        "G3 FAIL: release triggered while threat still closing (TCPA>0). "
        "Geometry gate not enforced."
    )

    # Scenario B: past CPA, DCPA safe — MUST release
    release_b = []
    # Target ~1.2 nm south of own ship, moving further south
    fs_b = SimpleNamespace(
        _avoidance_active=True,
        _avoidance_target_heading_deg=40.0,
        _target_heading_deg=0.0,
        _latch_release_triggered=False,
        _last_ownship_raw=_make_ownship_raw(lat=63.440, lon=10.381, heading_deg=40.0, sog_kn=10.0),
        _last_target_vessel_raw=_make_target_raw(lat=63.418, lon=10.380, heading_deg=180.0, sog_kn=10.0),
        get_logger=lambda: SimpleNamespace(info=lambda s: None, warning=lambda s: None),
        _trigger_latch_release=lambda: release_b.append(True),
        _latch_hold_elapsed=lambda: True,
    )
    bridge.SilTopicBridge._check_geometry_release(fs_b)
    assert len(release_b) > 0, (
        "G3 FAIL: release NOT triggered when TCPA<0 and DCPA safe. "
        "Geometry release not implemented."
    )


def test_turn_bounded(monkeypatch):
    """G4: geometry release must fire before over-turn to 180°.

    Own-ship at 40° starboard, target clearly past and diverging ~1.2 nm south.
    _check_geometry_release must trigger release, bounding the turn.
    """
    bridge = _load_bridge(monkeypatch)

    release_calls = []
    fake_self = SimpleNamespace(
        _avoidance_active=True,
        _avoidance_target_heading_deg=40.0,
        _target_heading_deg=0.0,
        _latch_release_triggered=False,
        _last_ownship_raw=_make_ownship_raw(lat=63.440, lon=10.381, heading_deg=40.0, sog_kn=10.0),
        _last_target_vessel_raw=_make_target_raw(lat=63.418, lon=10.380, heading_deg=180.0, sog_kn=10.0),
        get_logger=lambda: SimpleNamespace(info=lambda s: None, warning=lambda s: None),
        _trigger_latch_release=lambda: release_calls.append(True),
        _latch_hold_elapsed=lambda: True,
    )

    bridge.SilTopicBridge._check_geometry_release(fake_self)

    assert len(release_calls) > 0, (
        "G4 FAIL: release did not fire at ~40° bounding angle. "
        "Ship would continue to 180° (over-turn). Not bounded."
    )
    assert fake_self._avoidance_target_heading_deg <= 80.0, (
        f"G4 FAIL: avoidance target {fake_self._avoidance_target_heading_deg:.1f}° "
        "exceeds 80° after geometry resolution."
    )


# ── Task 3 tests: Deterministic arm (fix 2/5 LOCK) ───────────────────────────
#
# Root cause: _MIN_ARM_SIM_T=10.0s guard silently drops arm if M5 delivers
# a valid plan before sim_t=10s. With G1 continuous tracking, this guard is
# vestigial — even if we arm early, _on_behavior_plan refreshes the target.
# Fix: remove the sim_t guard (or treat it as a soft warning, not a return).


def test_arm_fires_when_sim_t_below_10s(monkeypatch):
    """Task 3: arm must NOT be suppressed by sim_t < 10s guard.

    With G1 continuous tracking, early arm is safe — the target refreshes
    continuously via _on_behavior_plan. A valid M5 plan at sim_t=5s should arm.

    This test was FAILING with the old code (return when sim_t < MIN_ARM_SIM_T),
    and should PASS after removing the sim_t guard.
    """
    bridge = _load_bridge(monkeypatch)

    fake_self = SimpleNamespace(
        _avoidance_active=False,
        _avoidance_target_heading_deg=None,
        _m3_activated_once=False,
        _autopilot_enabled=False,
        _last_behavior_plan=SimpleNamespace(
            behavior=1,  # COLREG_AVOID
            heading_min_deg=16.0,
            heading_max_deg=34.0,
        ),
        _avoidance_armed_time=None,
        _last_valid_plan_time=None,
        _last_avoidance_waypoint=None,
        _avoidance_heading_controller=SimpleNamespace(last_cmd_deg=0.0),
        _latch_release_triggered=False,
        _record_pulse=lambda mid: None,
        _trace_writer=SimpleNamespace(record=lambda *a, **kw: None),
        # sim_time = 5s — BELOW the old _MIN_ARM_SIM_T=10s guard
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=5_000_000_000, to_msg=lambda: None)
        ),
        get_logger=lambda: SimpleNamespace(info=lambda s: None),
    )
    fake_self._get_sim_time = lambda: 5.0  # 5s — below old guard
    reset_calls = []
    fake_self._reset_latch_release_state = lambda: reset_calls.append(True)

    plan = SimpleNamespace(
        waypoints=[SimpleNamespace(turn_radius_m=500.0, target_speed_kn=10.0)],
    )
    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)

    assert fake_self._avoidance_active is True, (
        "Task 3 FAIL: avoidance was NOT armed at sim_t=5s. "
        "_MIN_ARM_SIM_T guard is still suppressing early arm. "
        "With G1 continuous tracking, early arm is safe — remove the guard."
    )


def test_arm_recovers_across_cold_cycles(monkeypatch):
    """Task 3: after lifecycle cleanup (not ACTIVE), a new valid plan re-arms.

    Simulates: ACTIVE → avoidance armed → not ACTIVE (cleanup) → ACTIVE again
    → new M5 plan → should arm again (no stale _avoidance_active True).
    """
    bridge = _load_bridge(monkeypatch)

    # Simulate post-lifecycle-cleanup state (all reset to False/None)
    fake_self = SimpleNamespace(
        _avoidance_active=False,  # reset by lifecycle
        _avoidance_target_heading_deg=None,
        _m3_activated_once=False,
        _autopilot_enabled=False,
        _last_behavior_plan=SimpleNamespace(
            behavior=1,
            heading_min_deg=16.0,
            heading_max_deg=34.0,
        ),
        _avoidance_armed_time=None,
        _last_valid_plan_time=None,
        _last_avoidance_waypoint=None,
        _avoidance_heading_controller=SimpleNamespace(last_cmd_deg=0.0),
        _latch_release_triggered=False,
        _record_pulse=lambda mid: None,
        _trace_writer=SimpleNamespace(record=lambda *a, **kw: None),
        get_clock=lambda: SimpleNamespace(
            now=lambda: SimpleNamespace(nanoseconds=15_000_000_000, to_msg=lambda: None)
        ),
        get_logger=lambda: SimpleNamespace(info=lambda s: None),
    )
    fake_self._get_sim_time = lambda: 15.0
    fake_self._reset_latch_release_state = lambda: None

    plan = SimpleNamespace(
        waypoints=[SimpleNamespace(turn_radius_m=500.0, target_speed_kn=10.0)],
    )
    bridge.SilTopicBridge._on_avoidance_plan(fake_self, plan)

    assert fake_self._avoidance_active is True, (
        "Task 3 FAIL: avoidance did not re-arm after lifecycle cleanup. "
        "Cold-cycle LOCK reproduced."
    )
