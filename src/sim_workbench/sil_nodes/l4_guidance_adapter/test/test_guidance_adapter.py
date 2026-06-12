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


def test_m4_preferred_heading_overrides_under_evasive_waypoint_heading():
    heading = select_avoidance_heading(
        waypoint_heading_deg=35.0,
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


def test_transit_command_boosts_speed_when_route_return_xte_is_large():
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
    assert cmd.throttle > 10.0 / 25.0


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
