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


def test_active_avoidance_at_corridor_edge_keeps_avoidance_and_caps_speed():
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
    node._compute_transit_command = lambda _own: (_ for _ in ()).throw(
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
            "lon": 450.0 / 111319.9,
            "heading_deg": 0.0,
            "sog_kn": 5.0,
            "rot_deg_s": 0.0,
        },
    )

    assert math.degrees(cmd.rudder_angle) == pytest.approx(0.0)
    assert cmd.throttle == pytest.approx(0.0)


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
