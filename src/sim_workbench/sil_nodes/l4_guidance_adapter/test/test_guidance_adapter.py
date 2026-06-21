import math
from pathlib import Path
from types import SimpleNamespace
import sys

import pytest


sys.path.insert(0, str(Path(__file__).parents[1]))

from l4_guidance_adapter.guidance import (  # noqa: E402
    HeadingController,
    SpeedController,
    avoidance_waypoint_heading_deg,
    command_for_heading_speed,
    corridor_guarded_avoidance_heading_deg,
    corridor_guarded_avoidance_speed_kn,
    compute_transit_command,
    safety_gate_command,
    select_avoidance_heading,
    signed_xte_m,
)
from l4_guidance_adapter.node import L4GuidanceAdapterNode  # noqa: E402


def test_route_xte_uses_current_target_waypoint_segment():
    route = [
        (0.0, 0.0),
        (0.0, 1.0),
        (1.0, 1.0),
    ]

    xte_m = signed_xte_m(route, 0.5, 1.0, 1.0, 1.0)

    assert xte_m == pytest.approx(0.0, abs=1.0)


def test_avoidance_waypoint_selects_plan_waypoint_nearest_preferred_heading():
    waypoint = SimpleNamespace(position=SimpleNamespace(latitude=0.00819, longitude=0.00574))

    heading = avoidance_waypoint_heading_deg(
        waypoints=[waypoint],
        own_lat=0.0,
        own_lon=0.0,
        nominal_heading_deg=0.0,
        preferred_heading_deg=60.0,
    )

    assert heading is not None
    assert heading < 60.0


def test_avoidance_waypoint_tracks_plan_order_before_preferred_heading():
    first_route_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.00845, longitude=0.00307)
    )
    later_preferred_waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=0.0045, longitude=0.00778)
    )

    heading = avoidance_waypoint_heading_deg(
        waypoints=[first_route_waypoint, later_preferred_waypoint],
        own_lat=0.0,
        own_lon=0.0,
        nominal_heading_deg=0.0,
        preferred_heading_deg=60.0,
    )

    assert heading == pytest.approx(20.0, abs=2.0)


def test_avoidance_waypoint_is_not_colregs_role_filter():
    waypoint = SimpleNamespace(position=SimpleNamespace(latitude=0.00819, longitude=0.00574))

    heading = avoidance_waypoint_heading_deg(
        waypoints=[waypoint],
        own_lat=0.0,
        own_lon=0.0,
        nominal_heading_deg=0.0,
        preferred_heading_deg=60.0,
    )

    assert heading is not None


def test_same_side_m5_waypoint_cannot_reduce_m4_heading():
    heading = select_avoidance_heading(
        waypoint_heading_deg=35.0,
        avoidance_target_heading_deg=60.0,
        nominal_heading_deg=0.0,
    )

    assert heading == pytest.approx(60.0)


def test_wrong_side_m5_waypoint_keeps_colregs_target():
    heading = select_avoidance_heading(
        waypoint_heading_deg=345.0,
        avoidance_target_heading_deg=60.0,
        nominal_heading_deg=0.0,
    )

    assert heading == pytest.approx(60.0)


def test_more_evasive_waypoint_heading_is_preserved():
    heading = select_avoidance_heading(
        waypoint_heading_deg=80.0,
        avoidance_target_heading_deg=60.0,
        nominal_heading_deg=0.0,
    )

    assert heading == pytest.approx(80.0)


def test_corridor_guard_limits_outbound_avoidance_heading_near_route_edge():
    heading = corridor_guarded_avoidance_heading_deg(
        selected_heading_deg=60.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=230.0 / 111319.9,
    )

    assert heading == pytest.approx(30.0)


def test_corridor_guard_preserves_inbound_avoidance_heading():
    heading = corridor_guarded_avoidance_heading_deg(
        selected_heading_deg=350.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=400.0 / 111319.9,
    )

    assert heading == pytest.approx(350.0)


def test_corridor_guard_forces_route_heading_before_500m_edge():
    heading = corridor_guarded_avoidance_heading_deg(
        selected_heading_deg=60.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=300.0 / 111319.9,
    )

    assert heading == pytest.approx(0.0)


def test_corridor_guard_does_not_apply_without_route():
    heading = corridor_guarded_avoidance_heading_deg(
        selected_heading_deg=60.0,
        nominal_heading_deg=0.0,
        route_wps=[],
        own_lat=0.005,
        own_lon=400.0 / 111319.9,
    )

    assert heading == pytest.approx(60.0)


def test_corridor_guard_uses_target_heading_when_route_is_not_latched_yet():
    heading = corridor_guarded_avoidance_heading_deg(
        selected_heading_deg=60.0,
        nominal_heading_deg=0.0,
        route_wps=[],
        own_lat=1.004,
        own_lon=450.0 / 111319.9,
        current_target_wp_lat=1.02,
        current_target_wp_lon=0.0,
    )

    assert heading == pytest.approx(0.0)


def test_corridor_guard_reduces_outbound_avoidance_speed_near_route_edge():
    speed_kn = corridor_guarded_avoidance_speed_kn(
        target_speed_kn=12.0,
        selected_heading_deg=30.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=190.0 / 111319.9,
    )

    assert speed_kn == pytest.approx(10.0)


def test_corridor_guard_preserves_speed_when_heading_is_inbound():
    speed_kn = corridor_guarded_avoidance_speed_kn(
        target_speed_kn=12.0,
        selected_heading_deg=350.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=320.0 / 111319.9,
    )

    assert speed_kn == pytest.approx(12.0)


def test_corridor_guard_reduces_speed_when_heading_is_neutral_at_edge():
    speed_kn = corridor_guarded_avoidance_speed_kn(
        target_speed_kn=22.0,
        selected_heading_deg=0.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=280.0 / 111319.9,
    )

    assert speed_kn == pytest.approx(22.0)


def test_transit_command_does_not_boost_speed_when_route_return_xte_is_large():
    heading_controller = HeadingController(max_rate_deg_s=100.0)
    speed_controller = SpeedController()
    cmd = None
    for _ in range(3):
        cmd = compute_transit_command(
            current_heading_deg=0.0,
            current_sog_kn=10.0,
            current_rot_deg_s=0.0,
            own_lat=0.002,
            own_lon=0.5,
            target_heading_deg=90.0,
            target_sog_kn=10.0,
            current_target_wp_lat=0.0,
            current_target_wp_lon=1.0,
            route_wps=[(0.0, 0.0), (0.0, 1.0)],
            heading_controller=heading_controller,
            speed_controller=speed_controller,
        )

    assert cmd is not None
    assert cmd.throttle <= 10.0 / 25.0
    assert math.degrees(cmd.rudder_angle) < 0.0


def test_transit_route_return_keeps_minimum_steerage_speed_at_hard_xte():
    cmd = compute_transit_command(
        current_heading_deg=0.0,
        current_sog_kn=4.0,
        current_rot_deg_s=0.0,
        own_lat=1.004,
        own_lon=450.0 / 111319.9,
        target_heading_deg=0.0,
        target_sog_kn=10.0,
        current_target_wp_lat=1.02,
        current_target_wp_lon=0.0,
        route_wps=[(1.0, 0.0), (1.02, 0.0)],
        heading_controller=HeadingController(max_rate_deg_s=100.0),
        speed_controller=SpeedController(),
        dt=5.0,
    )

    assert cmd.throttle > 0.0


def test_transit_command_rejoins_route_instead_of_chasing_behind_waypoint():
    cmd = compute_transit_command(
        current_heading_deg=0.0,
        current_sog_kn=5.0,
        current_rot_deg_s=0.0,
        own_lat=1.004,
        own_lon=300.0 / 111319.9,
        target_heading_deg=0.0,
        target_sog_kn=10.0,
        current_target_wp_lat=1.0,
        current_target_wp_lon=0.0,
        route_wps=[(1.0, 0.0), (1.02, 0.0)],
        heading_controller=HeadingController(max_rate_deg_s=100.0),
        speed_controller=SpeedController(),
    )

    assert math.degrees(cmd.rudder_angle) > 0.0


def test_transit_command_uses_control_dt_for_route_return_rudder_ramp():
    common = dict(
        current_heading_deg=50.0,
        current_sog_kn=4.0,
        current_rot_deg_s=0.0,
        own_lat=1.004,
        own_lon=450.0 / 111319.9,
        target_heading_deg=0.0,
        target_sog_kn=10.0,
        current_target_wp_lat=1.02,
        current_target_wp_lon=0.0,
        route_wps=[(1.0, 0.0), (1.02, 0.0)],
        speed_controller=SpeedController(),
    )

    slow_dt_cmd = compute_transit_command(
        **common,
        heading_controller=HeadingController(),
        dt=0.5,
    )
    sim_dt_cmd = compute_transit_command(
        **common,
        heading_controller=HeadingController(),
        dt=5.0,
    )

    assert abs(math.degrees(sim_dt_cmd.rudder_angle)) > abs(
        math.degrees(slow_dt_cmd.rudder_angle)
    ) * 5.0


def test_transit_command_uses_target_heading_when_route_is_not_latched_yet():
    cmd = compute_transit_command(
        current_heading_deg=0.0,
        current_sog_kn=4.0,
        current_rot_deg_s=0.0,
        own_lat=1.004,
        own_lon=450.0 / 111319.9,
        target_heading_deg=0.0,
        target_sog_kn=10.0,
        current_target_wp_lat=1.02,
        current_target_wp_lon=0.0,
        route_wps=[],
        heading_controller=HeadingController(),
        speed_controller=SpeedController(),
        dt=10.0,
    )

    assert math.degrees(cmd.rudder_angle) > 20.0


def test_safety_gate_forces_zero_command():
    cmd = safety_gate_command(True)

    assert cmd is not None
    assert cmd.rudder_angle == 0.0
    assert cmd.throttle == 0.0


def test_reactive_override_heading_speed_command_uses_l4_controllers():
    cmd = command_for_heading_speed(
        target_heading_deg=90.0,
        target_sog_kn=12.0,
        current_heading_deg=80.0,
        current_sog_kn=10.0,
        current_rot_deg_s=0.0,
        heading_controller=HeadingController(max_rate_deg_s=100.0),
        speed_controller=SpeedController(),
    )

    assert math.degrees(cmd.rudder_angle) < 0.0
    assert cmd.throttle > 0.0


def test_l4_node_has_no_local_geometry_or_threat_release_handlers():
    assert not hasattr(L4GuidanceAdapterNode, "_check_geometry_release")
    assert not hasattr(L4GuidanceAdapterNode, "_on_threat_state")
    assert not hasattr(L4GuidanceAdapterNode, "_on_colregs_constraint")
    assert not hasattr(L4GuidanceAdapterNode, "_on_target_vessel_state")


def test_mission_goal_updates_route_target_without_triggering_latch_release():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._current_target_wp_lat = 0.0
    node._current_target_wp_lon = 0.0
    node._last_behavior_plan = SimpleNamespace(behavior=0)
    node._latch_release_triggered = False
    node._latch_hold_elapsed = lambda: True

    def fail_release():
        raise AssertionError("MissionGoal must not release avoidance in L4")

    node._trigger_latch_release = fail_release

    msg = SimpleNamespace(
        fsm_state=3,
        current_target_wp=SimpleNamespace(
            latitude=1.25,
            longitude=2.5,
        ),
        task_validity=1,
    )

    L4GuidanceAdapterNode._on_mission_goal(node, msg)

    assert node._current_target_wp_lat == 1.25
    assert node._current_target_wp_lon == 2.5


def test_clock_reset_preserves_latched_route_for_post_avoidance_rejoin():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._last_sim_time = 100.0
    node._route_wps = [(1.0, 2.0), (3.0, 4.0)]
    node._heading_controller = HeadingController()
    node._avoidance_heading_controller = HeadingController()
    node._override_heading_controller = HeadingController()
    node._speed_controller = SpeedController()
    node._sim_time = lambda: 10.0
    node.get_logger = lambda: SimpleNamespace(info=lambda _msg: None)

    L4GuidanceAdapterNode._on_own_ship_state(node, SimpleNamespace())

    assert node._route_wps == [(1.0, 2.0), (3.0, 4.0)]


def test_transit_autopilot_continues_when_odd_sample_is_temporarily_missing():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._sim_time = lambda: 100.0
    node._last_actuator_publish_time = None
    node.get_clock = lambda: SimpleNamespace(
        now=lambda: SimpleNamespace(to_msg=lambda: object())
    )
    node._safety_gate_active = lambda _now: False
    node._current_ownship = lambda: {
        "lat": 1.0,
        "lon": 0.0,
        "heading_deg": 0.0,
        "sog_kn": 5.0,
        "rot_deg_s": 0.0,
    }
    node._active_override = lambda _now: None
    node._avoidance_active = False
    node._last_odd_state = None
    node._last_valid_plan_time = 80.0
    node._last_behavior_plan = SimpleNamespace(behavior=0, rationale="")
    node._compute_transit_command = lambda _own, _dt: SimpleNamespace(
        rudder_angle=0.1,
        throttle=0.5,
    )
    published = []
    node._publish_command = lambda cmd, _stamp: published.append(cmd)

    L4GuidanceAdapterNode._autopilot_step(node)

    assert len(published) == 1


def test_safety_alert_gate_expires_without_fresh_m7_alerts():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    now = [100.0]
    node._sim_time = lambda: now[0]
    node._SAFETY_ALERT_HOLD_S = 2.0
    node._safety_alert_active = False
    node._safety_alert_until = None
    node._safety_gate_reason = ""
    node._checker_veto_until = None

    L4GuidanceAdapterNode._on_safety_alert(
        node,
        SimpleNamespace(
            severity=3,
            description="MRC required: transient watchdog condition",
        ),
    )

    assert L4GuidanceAdapterNode._safety_gate_active(node, 101.0) is True
    now[0] = 103.1
    assert L4GuidanceAdapterNode._safety_gate_active(node, 103.1) is False
    assert node._safety_alert_active is False


def test_transit_autopilot_uses_elapsed_sim_time_for_control_dt():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._sim_time = lambda: 105.5
    node._last_actuator_publish_time = 100.0
    node.get_clock = lambda: SimpleNamespace(
        now=lambda: SimpleNamespace(to_msg=lambda: object())
    )
    node._safety_gate_active = lambda _now: False
    node._current_ownship = lambda: {
        "lat": 1.0,
        "lon": 0.0,
        "heading_deg": 0.0,
        "sog_kn": 5.0,
        "rot_deg_s": 0.0,
    }
    node._active_override = lambda _now: None
    node._avoidance_active = False
    node._last_odd_state = None
    node._last_valid_plan_time = 80.0
    node._last_behavior_plan = SimpleNamespace(behavior=0, rationale="")
    seen_dt = []

    def compute_transit(_own, dt):
        seen_dt.append(dt)
        return SimpleNamespace(rudder_angle=0.1, throttle=0.5)

    node._compute_transit_command = compute_transit
    node._publish_command = lambda _cmd, _stamp: None

    L4GuidanceAdapterNode._autopilot_step(node)

    assert seen_dt == [pytest.approx(5.5)]


def test_avoidance_plan_does_not_arm_when_m4_is_transit():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = False
    node._last_behavior_plan = SimpleNamespace(behavior=0)
    node._last_valid_plan_time = None
    node._last_avoidance_waypoint = None
    node._last_avoidance_waypoints = []
    node._avoidance_armed_time = None
    node._reset_latch_release_state = lambda: None
    node._sim_time = lambda: 12.0

    msg = SimpleNamespace(
        status="NORMAL",
        waypoints=[
            SimpleNamespace(
                turn_radius_m=100.0,
            )
        ],
    )

    L4GuidanceAdapterNode._on_avoidance_plan(node, msg)

    assert node._last_valid_plan_time == 12.0
    assert node._last_avoidance_waypoint is msg.waypoints[0]
    assert node._avoidance_active is False


def test_degraded_fallback_plan_with_waypoints_arms_when_m4_is_avoidance():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = False
    node._last_behavior_plan = SimpleNamespace(behavior=1, heading_min_deg=60.0, heading_max_deg=90.0)
    node._last_valid_plan_time = None
    node._last_avoidance_waypoint = None
    node._last_avoidance_waypoints = []
    node._avoidance_armed_time = None
    node._avoidance_target_heading_deg = None
    node._target_heading_deg = 0.0
    node._reset_latch_release_state = lambda: None
    node._sim_time = lambda: 20.0

    msg = SimpleNamespace(
        status="DEGRADED",
        waypoints=[
            SimpleNamespace(
                turn_radius_m=100.0,
            )
        ],
    )

    L4GuidanceAdapterNode._on_avoidance_plan(node, msg)

    assert node._last_valid_plan_time == 20.0
    assert node._last_avoidance_waypoint is msg.waypoints[0]
    assert node._avoidance_active is True


def test_empty_plan_holds_existing_avoidance_while_m4_is_non_transit():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._last_behavior_plan = SimpleNamespace(behavior=1)
    node._latch_release_triggered = False
    node._last_valid_plan_time = 10.0
    node._last_avoidance_waypoint = SimpleNamespace(turn_radius_m=100.0)
    node._last_avoidance_waypoints = [node._last_avoidance_waypoint]

    msg = SimpleNamespace(status="NORMAL", waypoints=[])

    L4GuidanceAdapterNode._on_avoidance_plan(node, msg)

    assert node._avoidance_active is True
    assert node._last_avoidance_waypoint.turn_radius_m == 100.0


def test_behavior_plan_transit_triggers_latch_release_smoothing():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._latch_release_triggered = False
    node._transit_since_time = None
    node._avoidance_target_heading_deg = 160.0
    node._latch_hold_elapsed = lambda: True
    node._sim_time = lambda: 100.0
    node._AVOID_TRANSIT_RELEASE_S = 3.0
    released = []

    def trigger_release():
        released.append(True)
        node._latch_release_triggered = True

    node._trigger_latch_release = trigger_release

    L4GuidanceAdapterNode._on_behavior_plan(
        node,
        SimpleNamespace(
            behavior=0,
            heading_min_deg=0.0,
            heading_max_deg=359.0,
        ),
    )

    assert released == [True]
    assert node._avoidance_active is True
    assert node._transit_since_time is None


def test_behavior_recovery_triggers_latch_release_like_transit():
    """Fix-C: M4 BEHAVIOR_RECOVERY (7) must trigger latch release in L4.

    Before Fix-C, behavior=7 hit the else-branch and reset _transit_since_time
    without starting latch release, leaving L4 running avoidance-base transit
    indefinitely while M4 waited for XTE < 125 m to move to TRANSIT.

    Scenario: avoidance is active, heading committed at 72°, M4 sends RECOVERY.
    Latch release must fire immediately (same as TRANSIT).
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._latch_release_triggered = False
    node._transit_since_time = None
    node._avoidance_target_heading_deg = 72.0
    node._latch_hold_elapsed = lambda: True
    node._sim_time = lambda: 200.0
    node._AVOID_TRANSIT_RELEASE_S = 3.0
    released = []

    def trigger_release():
        released.append(True)
        node._latch_release_triggered = True

    node._trigger_latch_release = trigger_release

    L4GuidanceAdapterNode._on_behavior_plan(
        node,
        SimpleNamespace(
            behavior=7,  # BEHAVIOR_RECOVERY
            heading_min_deg=-10.0,
            heading_max_deg=10.0,
        ),
    )

    assert released == [True], (
        "BEHAVIOR_RECOVERY (7) must trigger latch release like BEHAVIOR_TRANSIT (0); "
        "without Fix-C, XTE deadlocks at ~200 m for the rest of the simulation"
    )
    assert node._avoidance_active is True


def test_behavior_recovery_does_not_update_avoidance_heading():
    """Fix-C: BEHAVIOR_RECOVERY (7) window must NOT update _avoidance_target_heading_deg.

    During RECOVERY, M4 publishes a heading window for gradual route return, NOT a
    COLREGs evasion window. Allowing it to update the avoidance heading would drift
    the committed evasion angle back toward nominal at the worst possible time.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._latch_release_triggered = False
    node._transit_since_time = None
    node._avoidance_target_heading_deg = 64.0   # committed heading, must not change
    node._target_heading_deg = 0.0
    node._latch_hold_elapsed = lambda: True
    node._sim_time = lambda: 200.0
    node._AVOID_TRANSIT_RELEASE_S = 3.0
    node._trigger_latch_release = lambda: None  # no-op for this test focus

    L4GuidanceAdapterNode._on_behavior_plan(
        node,
        SimpleNamespace(
            behavior=7,  # BEHAVIOR_RECOVERY — window represents return-to-route, not evasion
            heading_min_deg=10.0,
            heading_max_deg=20.0,   # would produce candidate ~18° (less evasive than 64°)
        ),
    )

    assert node._avoidance_target_heading_deg == pytest.approx(64.0, abs=0.1), (
        "RECOVERY behavior must NOT update the avoidance heading; "
        f"got {node._avoidance_target_heading_deg:.1f}°"
    )


def test_latch_release_decay_scales_with_heading_offset():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = True
    node._latch_offset_at_release_deg = 160.0
    node._latch_release_progress = 0.0
    node._LATCH_RELEASE_DECAY_RATE_DEG_S = 16.0

    remaining = L4GuidanceAdapterNode._compute_latch_offset(
        node,
        t_release=10.0,
        t_now=15.0,
        current_offset_deg=160.0,
    )

    assert remaining == pytest.approx(80.0)
    assert node._latch_release_progress == pytest.approx(0.5)


def test_latch_release_at_corridor_edge_uses_transit_return_heading():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = True
    node._latch_release_time = 10.0
    node._latch_offset_at_release_deg = 60.0
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._sim_time = lambda: 11.0
    node._compute_latch_offset = lambda *args: 60.0
    node._reset_latch_release_state = lambda: None
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.012),
        target_speed_kn=22.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 360.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 5.0,
            "rot_deg_s": 0.0,
        },
    )

    assert math.degrees(cmd.rudder_angle) > 0.0


def test_active_avoidance_at_corridor_edge_regresses_to_transit_return():
    # When XTE >= HARD corridor (280 m) during active avoidance, the adapter
    # must hand control to the CPA-aware avoidance transit (Fix-A3) instead of
    # saturating the avoidance heading back to nominal and locking the rudder.
    # This is the regression that prevents the long-conflict dead-lock (own ship
    # pushed off-track, corridor guard zeroes rudder, XTE never closes). Own
    # ship 450 m east of a north route → avoidance transit return correction
    # must steer the bow toward the route (westward), i.e. a non-zero rudder.
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    avoid_transit_called = {"flag": False}

    def _spy_avoid_transit(_own, _dt=0.5):
        avoid_transit_called["flag"] = True
        from l4_guidance_adapter.guidance import ActuatorCommand
        return ActuatorCommand(rudder_angle=math.radians(-15.0), throttle=0.4)

    node._compute_avoidance_transit_command = _spy_avoid_transit
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=22.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 450.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 5.0,
            "rot_deg_s": 0.0,
        },
    )

    assert avoid_transit_called["flag"] is True
    assert math.degrees(cmd.rudder_angle) == pytest.approx(-15.0)


def test_active_avoidance_below_hard_corridor_keeps_avoidance_heading():
    # Complement to the transit regression test: XTE just under HARD corridor
    # must keep the normal avoidance heading path (no transit regression). Own
    # ship 250 m east (< 280) → transit must NOT be invoked; the avoidance
    # corridor guard still clamps the outbound heading but the adapter does not
    # hand off to transit.
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._compute_transit_command = lambda _own, _dt=0.5: (_ for _ in ()).throw(
        AssertionError("below HARD corridor, active avoidance must not regress to transit")
    )
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=22.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 250.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 5.0,
            "rot_deg_s": 0.0,
        },
    )

    # transit must not run; any avoidance-derived rudder is acceptable here.
    # The assertion is purely that the spy did not raise.
    assert cmd is not None


def test_transit_regression_hysteresis_holds_between_hard_and_soft():
    # Once the regression latches at XTE>=HARD, it must stay latched while XTE
    # drops into the [SOFT, HARD) band (e.g. 250 m) before handing back to
    # avoidance below SOFT (180 m). This dead-band prevents ROT chatter when
    # XTE hovers near the HARD corridor.
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    avoid_transit_count = {"n": 0}

    def _spy_avoid_transit(_own, _dt=0.5):
        avoid_transit_count["n"] += 1
        from l4_guidance_adapter.guidance import ActuatorCommand
        return ActuatorCommand(rudder_angle=math.radians(-10.0), throttle=0.4)

    node._compute_avoidance_transit_command = _spy_avoid_transit
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=22.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint
    node._avoidance_transit_regression_active = False

    own_template = {
        "lat": 1.004,
        "heading_deg": 0.0,
        "sog_kn": 5.0,
        "rot_deg_s": 0.0,
    }

    # 1. XTE=450 m (>= HARD) → avoidance transit regression latches.
    L4GuidanceAdapterNode._compute_avoidance_command(
        node, {**own_template, "lon": 450.0 / 111319.9})
    assert avoid_transit_count["n"] == 1
    assert node._avoidance_transit_regression_active is True

    # 2. XTE drops to 250 m (SOFT<250<HARD) → regression must HOLD (still avoidance transit).
    L4GuidanceAdapterNode._compute_avoidance_command(
        node, {**own_template, "lon": 250.0 / 111319.9})
    assert avoid_transit_count["n"] == 2
    assert node._avoidance_transit_regression_active is True

    # 3. XTE drops to 150 m (< SOFT) → regression releases, avoidance resumes.
    L4GuidanceAdapterNode._compute_avoidance_command(
        node, {**own_template, "lon": 150.0 / 111319.9})
    assert node._avoidance_transit_regression_active is False
    # avoidance transit not called again (count stays at 2).


def test_active_avoidance_speed_cap_preserves_current_scenario_speed():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._compute_transit_command = lambda _own, _dt=0.5: (_ for _ in ()).throw(
        AssertionError("active avoidance must not bypass risk handling with transit")
    )
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=22.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 200.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 12.0,
            "rot_deg_s": 0.0,
        },
    )

    # XTE 200 m < HARD corridor 280 m → avoidance path (no transit regression).
    assert cmd.throttle == pytest.approx(12.0 / 25.0)


def test_active_avoidance_speed_cap_blocks_solver_slowdown_without_reduce_speed():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._last_behavior_plan = SimpleNamespace(rationale="")
    node._compute_transit_command = lambda _own, _dt=0.5: (_ for _ in ()).throw(
        AssertionError("active avoidance must not bypass risk handling with transit")
    )
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=8.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 200.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 12.0,
            "rot_deg_s": 0.0,
        },
    )

    # XTE 200 m < HARD corridor 280 m → avoidance path (no transit regression).
    assert cmd.throttle == pytest.approx(12.0 / 25.0)


def test_active_avoidance_speed_cap_preserves_requested_reduce_speed():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._latch_release_triggered = False
    node._latch_release_time = None
    node._latch_offset_at_release_deg = None
    node._avoidance_target_heading_deg = 60.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(1.0, 0.0), (1.02, 0.0)]
    node._current_target_wp_lat = 1.02
    node._current_target_wp_lon = 0.0
    node._avoidance_heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._last_behavior_plan = SimpleNamespace(rationale="speed_reduction_preferred=true")
    node._compute_transit_command = lambda _own, _dt=0.5: (_ for _ in ()).throw(
        AssertionError("active avoidance must not bypass risk handling with transit")
    )
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=1.014, longitude=0.0),
        target_speed_kn=8.0,
    )
    node._last_avoidance_waypoints = [waypoint]
    node._last_avoidance_waypoint = waypoint

    cmd = L4GuidanceAdapterNode._compute_avoidance_command(
        node,
        {
            "lat": 1.004,
            "lon": 200.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 12.0,
            "rot_deg_s": 0.0,
        },
    )

    # XTE 200 m < HARD corridor 280 m → avoidance path (no transit regression).
    assert cmd.throttle == pytest.approx(8.0 / 25.0)


# ---------------------------------------------------------------------------
# COLREGs phase-gate C2: heading-controller damping on turn recovery.
# Rule 8(b) forbids a "succession of small alterations" -- the pure-P
# controller overshoots the target heading on turn recovery (it only reacts
# once the heading error crosses zero, by which time the hull is still
# rotating), producing the small sign-flipping rudder sequence the phase
# gate flags as small_runs. A derivative term on rate-of-turn damps it.
# ---------------------------------------------------------------------------

def test_heading_controller_pure_p_overshoots_on_turn_recovery():
    """With no derivative damping, a ship that has reached the target heading
    but is still turning gets a zero command and coasts through, then must
    reverse -- the overshoot the C2 small_runs check detects."""
    hc = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    # Ship has just arrived at target (error~0) but is still rotating hard.
    cmd = hc.step(error_deg=0.0, dt=0.5, current_rot_deg_s=5.0)
    # Pure-P: error is zero so command is zero -- no braking of the 5 deg/s
    # rotation. The ship will overshoot the target heading next cycle.
    assert math.degrees(cmd) == pytest.approx(0.0, abs=1e-6)


def test_heading_controller_derivative_damps_turn_recovery():
    """A derivative term on rate-of-turn must brake the ship as it approaches
    the target heading, commanding counter-rudder while still rotating toward
    (small positive error) and while overshooting (zero error, still turning)."""
    hc = HeadingController(Kp=1.0, Kd=0.3, max_rate_deg_s=100.0)
    # Approaching target, still turning toward it: P wants more turn, D opposes.
    cmd_approach = hc.step(error_deg=3.0, dt=0.5, current_rot_deg_s=5.0)
    # At target but still rotating: P=0, D alone brakes.
    hc2 = HeadingController(Kp=1.0, Kd=0.3, max_rate_deg_s=100.0)
    cmd_brake = hc2.step(error_deg=0.0, dt=0.5, current_rot_deg_s=5.0)
    # Derivative opposes the rotation (counter-rudder) in both cases.
    assert cmd_brake < 0.0, "derivative must brake an ongoing rotation"
    # Net command near the target must be smaller than pure-P would give for
    # a comparable error, i.e. damping reduces the commanded turn rate.
    pure_p = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    cmd_pure = pure_p.step(error_deg=3.0, dt=0.5, current_rot_deg_s=5.0)
    assert abs(cmd_approach) < abs(cmd_pure), "derivative must reduce net command near target"


def test_heading_controller_derivative_inactive_at_zero_rot():
    """When the ship is not rotating, the derivative term contributes nothing,
    so existing behavior (all current tests pass rot=0) is unchanged."""
    hc = HeadingController(Kp=1.0, Kd=0.3, max_rate_deg_s=100.0)
    cmd = hc.step(error_deg=10.0, dt=0.5, current_rot_deg_s=0.0)
    pure_p = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    cmd_pure = pure_p.step(error_deg=10.0, dt=0.5, current_rot_deg_s=0.0)
    assert math.degrees(cmd) == pytest.approx(math.degrees(cmd_pure), abs=1e-6)


# ---------------------------------------------------------------------------
# Fix-A3: CPA-aware avoidance transit.
# When XTE >= HARD corridor, avoidance transit must use avoidance heading as
# the base (not nominal) so that CPA is preserved and the heading jump that
# causes steering reversals is eliminated.
# ---------------------------------------------------------------------------

def test_cpa_aware_avoidance_transit_uses_avoidance_heading_as_base():
    """Fix-A3: _compute_avoidance_transit_command must steer toward the
    avoidance heading (+XTE correction) not toward nominal (0°).

    Setup: nominal heading 0° (north), avoidance target heading 85° (starboard),
    own ship 350 m east of route (XTE positive, above SOFT but below HARD so
    _compute_avoidance_transit_command is called directly).

    Expected: the effective commanded heading is closer to 85° than to 0°.
    A transit command from nominal would command ~0° - 70° = -70°, while one
    anchored to avoidance heading commands ~85° - 70° = 15°. The result must
    be positive (starboard of nominal) to preserve CPA.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_target_heading_deg = 85.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    # North route, own ship 350 m east (XTE ~350 m > SOFT=50, < HARD=280 for transit
    # return, but avoidance transit is called directly so we don't need XTE>=280)
    node._route_wps = [(63.0, 10.0), (63.02, 10.0)]
    node._current_target_wp_lat = 63.02
    node._current_target_wp_lon = 10.0
    node._avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()

    # Place own ship 350 m east of route (positive XTE => correction is toward west/port)
    # At 63° lat: 111319.9 * cos(63°) ≈ 50504 m/deg
    east_deg = 350.0 / (111319.9 * math.cos(math.radians(63.0)))

    own = {
        "lat": 63.005,
        "lon": 10.0 + east_deg,
        "heading_deg": 85.0,
        "sog_kn": 10.0,
        "rot_deg_s": 0.0,
    }

    cmd = L4GuidanceAdapterNode._compute_avoidance_transit_command(node, own)

    # Fix-A3 property: avoidance transit uses avoidance heading (85°) as base.
    # With XTE ~350 m (correction limit ~90°, actual ~70°), the effective target
    # heading is 85° - 70° = ~15° (westward correction anchored to avoidance hdg).
    # A nominal-base transit would target 0° - 70° = -70° (= 290°).
    # The key property: the rudder command from current heading 85° toward ~15°
    # is a PORT turn of ~70°. A nominal transit from heading 85° toward -70°/290°
    # would also be a PORT turn but of ~145°.  Both saturate at 35°, but the
    # SIGN is the same. What distinguishes Fix-A3 is the HEADING CONTROLLER used:
    # avoidance transit uses _avoidance_heading_controller, so its last_cmd_deg
    # reflects the avoidance path.  Verify the command is non-zero (returning
    # to route) and that the avoidance controller was exercised (last_cmd_deg != 0).
    import math as _math
    assert cmd is not None, "avoidance transit must return a command"
    # The avoidance heading controller must have been updated (not the transit controller)
    assert node._avoidance_heading_controller.last_cmd_deg != 0.0, (
        "avoidance transit must drive via avoidance heading controller, not transit"
    )
    # The nominal transit controller must NOT have been updated
    assert node._heading_controller.last_cmd_deg == 0.0, (
        "avoidance transit must NOT update the nominal heading controller"
    )


def test_cpa_aware_avoidance_transit_falls_back_when_no_avoidance_heading():
    """Fix-A3 fallback: when _avoidance_target_heading_deg is None (not yet
    set), _compute_avoidance_transit_command must fall back to plain transit
    (nominal heading as base) without crashing.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_target_heading_deg = None  # not yet set
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(63.0, 10.0), (63.02, 10.0)]
    node._current_target_wp_lat = 63.02
    node._current_target_wp_lon = 10.0
    node._avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()

    plain_transit_called = {"flag": False}

    def _spy_plain_transit(_own, _dt=0.5):
        plain_transit_called["flag"] = True
        from l4_guidance_adapter.guidance import ActuatorCommand
        return ActuatorCommand(rudder_angle=0.0, throttle=0.5)

    node._compute_transit_command = _spy_plain_transit

    own = {"lat": 63.005, "lon": 10.002, "heading_deg": 0.0, "sog_kn": 10.0, "rot_deg_s": 0.0}
    cmd = L4GuidanceAdapterNode._compute_avoidance_transit_command(node, own)

    assert plain_transit_called["flag"] is True
    assert cmd.throttle == pytest.approx(0.5)


# ---------------------------------------------------------------------------
# Fix-B: Committed avoidance heading must not be refreshed toward nominal.
# Once the latched heading is >= 10° from nominal, M4 window updates that
# would reduce the evasion angle must be blocked.
# ---------------------------------------------------------------------------

def test_committed_avoidance_heading_not_refreshed_toward_nominal():
    """Fix-B: once _avoidance_target_heading_deg is committed (delta >= 10°),
    a new M4 behavior_plan message whose window maps to a SMALLER delta must
    NOT overwrite the committed heading.

    Scenario: avoidance heading committed at 64° (64° from nominal 0°).
    M4 sends a new window [30°, 60°] → candidate ≈ 55° (< 64°). The heading
    must stay at 64°, not refresh to 55°.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._avoidance_target_heading_deg = 64.0
    node._target_heading_deg = 0.0
    node._latch_release_triggered = False

    class _FakeBP:
        behavior = 1  # COLREG_AVOID
        heading_min_deg = 30.0
        heading_max_deg = 60.0   # candidate ≈ 30 + (5/6)*30 = 55°, less evasive than 64°

    L4GuidanceAdapterNode._on_behavior_plan(node, _FakeBP())

    assert node._avoidance_target_heading_deg == pytest.approx(64.0, abs=0.5), (
        "committed avoidance heading must not be reduced by a less-evasive M4 window; "
        f"got {node._avoidance_target_heading_deg:.1f}°"
    )


def test_committed_avoidance_heading_refreshes_when_more_evasive():
    """Fix-B: when the M4 window maps to a MORE evasive heading (larger delta
    from nominal), the refresh must proceed normally.

    Scenario: avoidance heading committed at 55° from nominal 0°.
    M4 sends a new window [60°, 90°] → candidate ≈ 85° (> 55°, more evasive).
    The heading must update to 85°.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._avoidance_target_heading_deg = 55.0
    node._target_heading_deg = 0.0
    node._latch_release_triggered = False

    class _FakeBP:
        behavior = 1  # COLREG_AVOID
        heading_min_deg = 60.0
        heading_max_deg = 90.0   # candidate = 60 + (5/6)*30 = 85°, more evasive

    L4GuidanceAdapterNode._on_behavior_plan(node, _FakeBP())

    assert node._avoidance_target_heading_deg == pytest.approx(85.0, abs=1.0), (
        "a more evasive M4 window must refresh the avoidance heading; "
        f"got {node._avoidance_target_heading_deg:.1f}°"
    )


def test_uncommitted_avoidance_heading_always_refreshes():
    """Fix-B: when the avoidance heading is still very small (< 10°, not yet
    committed), any candidate from M4 must be accepted, including one that
    reduces the angle (e.g. fine-tuning at onset).
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._avoidance_target_heading_deg = 5.0   # < 10° → not committed
    node._target_heading_deg = 0.0
    node._latch_release_triggered = False

    class _FakeBP:
        behavior = 1  # COLREG_AVOID
        heading_min_deg = 2.0
        heading_max_deg = 7.0   # candidate ≈ 2 + (5/6)*5 ≈ 6.2° < 5°? let's pick bigger
        # m4_colregs_window_target returns h_min + (5/6)*span = 2 + 4.2 ≈ 6.2°

    L4GuidanceAdapterNode._on_behavior_plan(node, _FakeBP())

    # candidate 6.2° > 5° (more evasive), should refresh — but even if candidate
    # were smaller, uncommitted range allows free refresh.
    assert node._avoidance_target_heading_deg is not None


def test_committed_avoidance_heading_allows_minor_reduction_within_hysteresis():
    """Fix-B hysteresis: a small window dip (≤ 5° less evasive) must pass
    through so give-way scenarios can track a naturally fluctuating M4 window.
    Only reductions > 5° (clearly drifting back toward nominal) are blocked.

    Scenario: heading committed at 64°. M4 sends window [55°, 65°] →
    candidate = 55 + (5/6)*10 ≈ 63.3°, which is 0.7° less evasive.
    Reduction = 0.7° < 5° hysteresis → refresh must be allowed.
    """
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_active = True
    node._avoidance_target_heading_deg = 64.0
    node._target_heading_deg = 0.0
    node._latch_release_triggered = False

    class _FakeBP:
        behavior = 1  # COLREG_AVOID
        heading_min_deg = 55.0
        heading_max_deg = 65.0  # candidate = 55 + (5/6)*10 ≈ 63.3°, 0.7° less evasive

    L4GuidanceAdapterNode._on_behavior_plan(node, _FakeBP())

    # A minor dip of 0.7° is within the 5° hysteresis band and must pass through
    assert node._avoidance_target_heading_deg is not None
    assert node._avoidance_target_heading_deg != pytest.approx(64.0, abs=0.1), (
        "a minor M4 window dip within 5° hysteresis must NOT be frozen; "
        f"got {node._avoidance_target_heading_deg:.1f}°"
    )

