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


def test_same_side_m5_waypoint_can_reduce_m4_heading():
    heading = select_avoidance_heading(
        waypoint_heading_deg=35.0,
        avoidance_target_heading_deg=60.0,
        nominal_heading_deg=0.0,
    )

    assert heading == pytest.approx(35.0)


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


def test_corridor_guard_reduces_outbound_avoidance_speed_near_route_edge():
    speed_kn = corridor_guarded_avoidance_speed_kn(
        target_speed_kn=12.0,
        selected_heading_deg=30.0,
        nominal_heading_deg=0.0,
        route_wps=[(0.0, 0.0), (0.02, 0.0)],
        own_lat=0.005,
        own_lon=190.0 / 111319.9,
    )

    assert speed_kn == pytest.approx(6.0)


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

    assert speed_kn == pytest.approx(0.0)


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


def test_active_avoidance_at_corridor_return_edge_uses_transit_return():
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
            "lon": 450.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 5.0,
            "rot_deg_s": 0.0,
        },
    )

    assert math.degrees(cmd.rudder_angle) == pytest.approx(35.0)
