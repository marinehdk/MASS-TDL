"""ROS node wrapper for SIL L4 guidance adapter."""

from __future__ import annotations

import math
from typing import Optional

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import (
        QoSProfile,
        QoSDurabilityPolicy,
        QoSHistoryPolicy,
        QoSReliabilityPolicy,
    )
except ImportError:  # pragma: no cover - pure helper tests do not import ROS.
    rclpy = None
    Node = object
    QoSProfile = None
    QoSDurabilityPolicy = None
    QoSHistoryPolicy = None
    QoSReliabilityPolicy = None

from .guidance import (
    AVOIDANCE_CORRIDOR_HARD_XTE_M,
    CRUISE_SPEED_KN,
    ActuatorCommand,
    HeadingController,
    SpeedController,
    avoidance_waypoint_heading_deg,
    command_for_heading_speed,
    corridor_guarded_avoidance_heading_deg,
    corridor_guarded_avoidance_speed_kn,
    compute_avoidance_command,
    compute_transit_command,
    m4_colregs_window_target_deg,
    safety_gate_command,
    select_avoidance_heading,
    should_refresh_m4_colregs_target,
    signed_heading_delta_deg,
    signed_xte_m,
)


def _sensor_qos(depth: int = 5):
    return QoSProfile(
        reliability=QoSReliabilityPolicy.BEST_EFFORT,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


def _latched_qos(depth: int = 50):
    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


def _reliable_volatile_qos(depth: int = 10):
    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


class L4GuidanceAdapterNode(Node):
    """Actuator-facing SIL adapter for route following and avoidance following."""

    def __init__(self) -> None:
        try:
            from rclpy.parameter import Parameter

            super().__init__(
                "l4_guidance_adapter",
                parameter_overrides=[Parameter("use_sim_time", Parameter.Type.BOOL, True)],
                automatically_declare_parameters_from_overrides=True,
            )
        except Exception:
            super().__init__("l4_guidance_adapter")

        sq = _sensor_qos()
        lq = _latched_qos()
        rq = _reliable_volatile_qos()

        from sil_msgs.msg import LifecycleStatus, OwnShipState
        from l3_external_msgs.msg import CheckerVetoNotification, PlannedRoute
        from l3_msgs.msg import (
            AvoidancePlan,
            BehaviorPlan,
            MissionGoal,
            ODDState,
            ReactiveOverrideCmd,
            SafetyAlert,
        )

        self._own_msg_cls = OwnShipState
        self._pub_act = self.create_publisher(OwnShipState, "/sil/actuator_cmd", sq)

        self.create_subscription(OwnShipState, "/sil/own_ship_state", self._on_own_ship_state, sq)
        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status", self._on_lifecycle_status, sq)
        self.create_subscription(ODDState, "/l3/m1/odd_state", self._on_odd_state, sq)
        self.create_subscription(MissionGoal, "/l3/m3/mission_goal", self._on_mission_goal, sq)
        self.create_subscription(PlannedRoute, "/l2/planned_route", self._on_planned_route, lq)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self._on_behavior_plan, sq)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self._on_avoidance_plan, sq)
        self.create_subscription(ReactiveOverrideCmd, "/l3/m5/reactive_override_cmd", self._on_reactive_override, sq)
        self.create_subscription(ReactiveOverrideCmd, "/m5/reactive_override_cmd", self._on_reactive_override, sq)
        self.create_subscription(SafetyAlert, "/l3/m7/safety_alert", self._on_safety_alert, sq)
        self.create_subscription(CheckerVetoNotification, "/l3/checker/veto", self._on_checker_veto, rq)

        self._heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=5.0)
        self._avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=10.0)
        self._override_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=15.0)
        self._speed_controller = SpeedController()

        self._target_heading_deg = self._param_value("ownship_initial_heading_deg", 0.0)
        self._target_sog_kn = self._param_value("ownship_initial_sog_kn", CRUISE_SPEED_KN)
        self._reset_state(clear_route=True)

        self._timer = self.create_timer(0.5, self._autopilot_step)
        self.get_logger().info("[l4_guidance_adapter] active; publishing /sil/actuator_cmd")

    def _param_value(self, name: str, default):
        try:
            self.declare_parameter(name, default)
        except Exception:
            pass
        try:
            return self.get_parameter(name).value
        except Exception:
            return default

    def _sim_time(self) -> float:
        return self.get_clock().now().nanoseconds * 1e-9

    def _reset_state(self, *, clear_route: bool = False) -> None:
        self._autopilot_enabled = False
        self._avoidance_active = False
        self._avoidance_target_heading_deg: Optional[float] = None
        self._last_avoidance_waypoint = None
        self._last_avoidance_waypoints = []
        self._last_valid_plan_time: Optional[float] = None
        self._last_odd_state = None
        self._last_behavior_plan = None
        self._last_ownship_raw = None
        self._last_actuator_publish_time: Optional[float] = None
        self._last_sim_time: Optional[float] = None
        self._current_target_wp_lat = 0.0
        self._current_target_wp_lon = 0.0
        self._lifecycle_state = None
        self._avoidance_armed_time: Optional[float] = None
        self._latch_release_triggered = False
        self._latch_release_time: Optional[float] = None
        self._latch_offset_at_release_deg: Optional[float] = None
        self._latch_release_progress = 0.0
        self._transit_since_time: Optional[float] = None
        self._last_override = None
        self._last_override_time: Optional[float] = None
        self._safety_alert_active = False
        self._safety_alert_until: Optional[float] = None
        self._safety_gate_reason = ""
        self._checker_veto_until: Optional[float] = None
        self._LATCH_MIN_HOLD_S = 8.0
        self._AVOID_TRANSIT_RELEASE_S = 3.0
        self._LATCH_RELEASE_DECAY_RATE_DEG_S = 16.0
        self._SAFETY_ALERT_HOLD_S = 2.0
        if clear_route:
            self._route_wps = []
        self._heading_controller.last_cmd_deg = 0.0
        self._avoidance_heading_controller.last_cmd_deg = 0.0
        self._override_heading_controller.last_cmd_deg = 0.0
        self._speed_controller = SpeedController()

    def _make_actuator_msg(self, stamp) -> object:
        out = self._own_msg_cls()
        out.stamp = stamp
        out.lat = 0.0
        out.lon = 0.0
        out.heading = 0.0
        out.sog = 0.0
        out.cog = 0.0
        out.rot = 0.0
        out.u = 0.0
        out.v = 0.0
        out.r = 0.0
        out.rudder_angle = 0.0
        out.throttle = 0.0
        return out

    def _publish_command(self, cmd: ActuatorCommand, stamp) -> None:
        out = self._make_actuator_msg(stamp)
        out.rudder_angle = float(cmd.rudder_angle)
        out.throttle = max(0.0, min(1.0, float(cmd.throttle)))
        self._pub_act.publish(out)

    def _on_lifecycle_status(self, msg) -> None:
        prev_state = self._lifecycle_state
        self._lifecycle_state = msg.current_state
        if msg.current_state != 3:
            self._reset_state(clear_route=False)
            return
        if prev_state != 3:
            self._target_heading_deg = self._param_value("ownship_initial_heading_deg", self._target_heading_deg)
            self._target_sog_kn = self._param_value("ownship_initial_sog_kn", self._target_sog_kn)

    def _on_own_ship_state(self, msg) -> None:
        now = self._sim_time()
        if self._last_sim_time is not None and now < self._last_sim_time - 1.0:
            self.get_logger().info(
                f"[l4_guidance_adapter] clock reset {self._last_sim_time:.2f}s -> {now:.2f}s")
            self._reset_state(clear_route=False)
        self._last_sim_time = now
        self._last_ownship_raw = msg

    def _on_odd_state(self, msg) -> None:
        self._last_odd_state = msg

    def _on_behavior_plan(self, msg) -> None:
        self._last_behavior_plan = msg
        if self._avoidance_active:
            if msg.behavior == 0:
                if self._latch_release_triggered:
                    self._transit_since_time = None
                elif (self._latch_hold_elapsed() and
                      self._avoidance_target_heading_deg is not None):
                    self._trigger_latch_release()
                    self._transit_since_time = None
                else:
                    now = self._sim_time()
                    if self._transit_since_time is None:
                        self._transit_since_time = now
                    if (self._latch_hold_elapsed() and
                            (now - self._transit_since_time) >= self._AVOID_TRANSIT_RELEASE_S):
                        self._avoidance_active = False
                        self._avoidance_target_heading_deg = None
                        self._avoidance_heading_controller.last_cmd_deg = 0.0
                        self._reset_latch_release_state()
                        self._transit_since_time = None
            else:
                self._transit_since_time = None

        if (self._avoidance_active and
                msg.behavior != 0 and
                not self._latch_release_triggered):
            nominal_heading = self._target_heading_deg
            candidate = m4_colregs_window_target_deg(
                msg.heading_min_deg, msg.heading_max_deg, nominal_heading)
            should_refresh = (
                candidate is not None and
                should_refresh_m4_colregs_target(
                    self._avoidance_target_heading_deg, nominal_heading, candidate)
            )
            if should_refresh:
                self._avoidance_target_heading_deg = candidate

    def _on_mission_goal(self, msg) -> None:
        if msg.fsm_state < 3:
            self._current_target_wp_lat = 0.0
            self._current_target_wp_lon = 0.0
            return
        if (abs(msg.current_target_wp.latitude) > 1e-4 or
                abs(msg.current_target_wp.longitude) > 1e-4):
            self._current_target_wp_lat = float(msg.current_target_wp.latitude)
            self._current_target_wp_lon = float(msg.current_target_wp.longitude)

    def _on_planned_route(self, msg) -> None:
        try:
            self._route_wps = [
                (float(p.pose.position.latitude), float(p.pose.position.longitude))
                for p in msg.route.poses
            ]
        except Exception:
            pass

    def _on_avoidance_plan(self, msg) -> None:
        wp0 = msg.waypoints[0] if msg.waypoints else None
        plan_status = str(getattr(msg, "status", "NORMAL")).upper()
        has_valid_plan = bool(
            plan_status == "NORMAL" and
            wp0 is not None and
            abs(wp0.turn_radius_m) > 1e-6
        )
        if has_valid_plan:
            self._last_valid_plan_time = self._sim_time()
            self._last_avoidance_waypoint = wp0
            self._last_avoidance_waypoints = list(msg.waypoints)

        m4_allows_avoidance = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior != 0
        )
        if not has_valid_plan and m4_allows_avoidance:
            return
        if not has_valid_plan and self._avoidance_active and self._latch_release_triggered:
            return

        if not has_valid_plan:
            if self._avoidance_active:
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()
            return

        if not self._avoidance_active and not m4_allows_avoidance:
            return

        if not self._avoidance_active:
            self._avoidance_active = True
            self._avoidance_armed_time = self._sim_time()
            self._reset_latch_release_state()
            if (self._last_behavior_plan is not None and
                    self._last_behavior_plan.behavior != 0 and
                    self._last_behavior_plan.heading_max_deg > 0.0):
                candidate = m4_colregs_window_target_deg(
                    self._last_behavior_plan.heading_min_deg,
                    self._last_behavior_plan.heading_max_deg,
                    self._target_heading_deg,
                )
                if candidate is not None:
                    self._avoidance_target_heading_deg = candidate

    def _on_reactive_override(self, msg) -> None:
        self._last_override = msg
        self._last_override_time = self._sim_time()

    def _on_safety_alert(self, msg) -> None:
        severity = int(getattr(msg, "severity", 0))
        self._safety_alert_active = severity >= 2
        if self._safety_alert_active:
            self._safety_alert_until = (
                self._sim_time() + float(getattr(self, "_SAFETY_ALERT_HOLD_S", 2.0))
            )
            self._safety_gate_reason = str(getattr(msg, "description", ""))
        else:
            self._safety_alert_until = None
            self._safety_gate_reason = ""

    def _on_checker_veto(self, msg) -> None:
        del msg
        self._checker_veto_until = self._sim_time() + 2.0
        self._safety_gate_reason = "checker veto"

    def _latch_hold_elapsed(self) -> bool:
        if self._avoidance_armed_time is None:
            return True
        return (self._sim_time() - self._avoidance_armed_time) >= self._LATCH_MIN_HOLD_S

    def _trigger_latch_release(self) -> None:
        if self._avoidance_target_heading_deg is None:
            return
        self._latch_release_triggered = True
        self._latch_release_time = self._sim_time()
        diff = signed_heading_delta_deg(
            self._avoidance_target_heading_deg, self._target_heading_deg)
        self._latch_offset_at_release_deg = abs(diff)
        self._latch_release_progress = 0.0

    def _reset_latch_release_state(self) -> None:
        self._latch_release_triggered = False
        self._latch_release_time = None
        self._latch_offset_at_release_deg = None
        self._latch_release_progress = 0.0
        self._avoidance_armed_time = None

    def _compute_latch_offset(self, t_release: float, t_now: float,
                              current_offset_deg: float) -> float:
        if not self._latch_release_triggered or self._latch_offset_at_release_deg is None:
            return current_offset_deg
        release_rate_deg_s = max(
            0.1,
            float(getattr(self, "_LATCH_RELEASE_DECAY_RATE_DEG_S", 2.0)),
        )
        initial_offset_deg = max(
            abs(float(self._latch_offset_at_release_deg)),
            abs(float(current_offset_deg)),
        )
        decay_duration_s = max(5.0, initial_offset_deg / release_rate_deg_s)
        progress = max(0.0, min(1.0, (t_now - t_release) / decay_duration_s))
        self._latch_release_progress = progress
        if progress >= 1.0:
            return 0.0
        return initial_offset_deg * (1.0 - progress)

    def _active_override(self, now: float):
        if self._last_override is None or self._last_override_time is None:
            return None
        validity = float(getattr(self._last_override, "validity_s", 0.0))
        if validity <= 0.0 or now - self._last_override_time > validity:
            return None
        return self._last_override

    def _safety_gate_active(self, now: float) -> bool:
        if self._safety_alert_active:
            if self._safety_alert_until is None or now <= self._safety_alert_until:
                return True
            self._safety_alert_active = False
            self._safety_alert_until = None
            self._safety_gate_reason = ""
        return self._checker_veto_until is not None and now <= self._checker_veto_until

    def _current_ownship(self):
        if self._last_ownship_raw is None:
            return {
                "heading_deg": self._target_heading_deg,
                "sog_kn": self._target_sog_kn,
                "rot_deg_s": 0.0,
                "lat": 0.0,
                "lon": 0.0,
            }
        return {
            "heading_deg": math.degrees(self._last_ownship_raw.heading) % 360.0,
            "sog_kn": self._last_ownship_raw.sog * 1.94384,
            "rot_deg_s": math.degrees(self._last_ownship_raw.rot),
            "lat": getattr(self._last_ownship_raw, "lat", 0.0),
            "lon": getattr(self._last_ownship_raw, "lon", 0.0),
        }

    def _compute_override_command(self, override, own, dt: float = 0.5) -> ActuatorCommand:
        return command_for_heading_speed(
            target_heading_deg=float(override.heading_cmd_deg),
            target_sog_kn=float(override.speed_cmd_kn),
            current_heading_deg=own["heading_deg"],
            current_sog_kn=own["sog_kn"],
            current_rot_deg_s=own["rot_deg_s"],
            heading_controller=self._override_heading_controller,
            speed_controller=self._speed_controller,
            dt=dt,
        )

    def _compute_avoidance_command(self, own, dt: float = 0.5) -> ActuatorCommand:
        if (self._latch_release_triggered and
                self._latch_release_time is not None and
                self._avoidance_target_heading_deg is not None):
            latch_offset = self._compute_latch_offset(
                self._latch_release_time,
                self._sim_time(),
                self._latch_offset_at_release_deg or 0.0,
            )
            diff = signed_heading_delta_deg(
                self._avoidance_target_heading_deg, self._target_heading_deg)
            sign = 1.0 if diff >= 0.0 else -1.0
            if latch_offset <= 0.01:
                self._latch_release_triggered = False
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()
            else:
                self._avoidance_target_heading_deg = (
                    self._target_heading_deg + sign * latch_offset) % 360.0

        if self._latch_release_triggered:
            release_xte_m = signed_xte_m(
                self._route_wps,
                own["lat"],
                own["lon"],
                self._current_target_wp_lat,
                self._current_target_wp_lon,
                fallback_heading_deg=self._target_heading_deg,
            )
            if (release_xte_m is not None and
                    math.isfinite(release_xte_m) and
                    abs(release_xte_m) >= AVOIDANCE_CORRIDOR_HARD_XTE_M):
                return self._compute_transit_command(own, dt)
        waypoints = list(self._last_avoidance_waypoints or [])
        if not waypoints and self._last_avoidance_waypoint is not None:
            waypoints = [self._last_avoidance_waypoint]
        waypoint_heading = avoidance_waypoint_heading_deg(
            waypoints=waypoints,
            own_lat=own["lat"],
            own_lon=own["lon"],
            nominal_heading_deg=self._target_heading_deg,
            preferred_heading_deg=self._avoidance_target_heading_deg,
        )
        selected_heading = select_avoidance_heading(
            waypoint_heading_deg=waypoint_heading,
            avoidance_target_heading_deg=self._avoidance_target_heading_deg,
            nominal_heading_deg=self._target_heading_deg,
        )
        selected_heading = corridor_guarded_avoidance_heading_deg(
            selected_heading_deg=selected_heading,
            nominal_heading_deg=self._target_heading_deg,
            route_wps=self._route_wps,
            own_lat=own["lat"],
            own_lon=own["lon"],
            current_target_wp_lat=self._current_target_wp_lat,
            current_target_wp_lon=self._current_target_wp_lon,
        )
        target_speed_override = None
        if self._last_avoidance_waypoint is not None:
            target_speed_override = corridor_guarded_avoidance_speed_kn(
                target_speed_kn=float(getattr(
                    self._last_avoidance_waypoint, "target_speed_kn", CRUISE_SPEED_KN)),
                selected_heading_deg=selected_heading,
                nominal_heading_deg=self._target_heading_deg,
                route_wps=self._route_wps,
                own_lat=own["lat"],
                own_lon=own["lon"],
                current_target_wp_lat=self._current_target_wp_lat,
                current_target_wp_lon=self._current_target_wp_lon,
            )
        return compute_avoidance_command(
            current_heading_deg=own["heading_deg"],
            current_sog_kn=own["sog_kn"],
            current_rot_deg_s=own["rot_deg_s"],
            waypoint_heading_deg=selected_heading,
            avoidance_target_heading_deg=self._avoidance_target_heading_deg,
            last_avoidance_waypoint=self._last_avoidance_waypoint,
            heading_controller=self._avoidance_heading_controller,
            speed_controller=self._speed_controller,
            target_speed_override_kn=target_speed_override,
            dt=dt,
        )

    def _compute_transit_command(self, own, dt: float = 0.5) -> ActuatorCommand:
        return compute_transit_command(
            current_heading_deg=own["heading_deg"],
            current_sog_kn=own["sog_kn"],
            current_rot_deg_s=own["rot_deg_s"],
            own_lat=own["lat"],
            own_lon=own["lon"],
            target_heading_deg=self._target_heading_deg,
            target_sog_kn=self._target_sog_kn,
            current_target_wp_lat=self._current_target_wp_lat,
            current_target_wp_lon=self._current_target_wp_lon,
            route_wps=self._route_wps,
            heading_controller=self._heading_controller,
            speed_controller=self._speed_controller,
            dt=dt,
        )

    def _autopilot_step(self) -> None:
        now = self._sim_time()
        if self._last_actuator_publish_time is not None and now - self._last_actuator_publish_time <= 0.5:
            return
        control_dt = 0.5
        if self._last_actuator_publish_time is not None:
            control_dt = max(0.05, min(10.0, now - self._last_actuator_publish_time))

        stamp = self.get_clock().now().to_msg()
        safe = safety_gate_command(self._safety_gate_active(now))
        if safe is not None:
            self._publish_command(safe, stamp)
            self._last_actuator_publish_time = now
            return

        own = self._current_ownship()
        override = self._active_override(now)
        if override is not None:
            cmd = self._compute_override_command(override, own, control_dt)
            self._publish_command(cmd, stamp)
            self._last_actuator_publish_time = now
            return

        if self._avoidance_active:
            cmd = self._compute_avoidance_command(own, control_dt)
            self._publish_command(cmd, stamp)
            self._last_actuator_publish_time = now
            return

        staleness_s = (
            (now - self._last_valid_plan_time)
            if self._last_valid_plan_time else float("inf")
        )
        env_state = (
            self._last_odd_state.envelope_state
            if self._last_odd_state is not None else 0
        )
        env_allows_autopilot = env_state in (0, 1, 3)
        is_m5_stale = staleness_s > 10.0
        m4_in_fallback = (
            self._last_behavior_plan is not None and
            "fallback" in self._last_behavior_plan.rationale.lower())
        m4_in_transit = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior == 0
        )
        self._autopilot_enabled = (
            env_allows_autopilot and (is_m5_stale or m4_in_fallback or m4_in_transit)
        )
        if self._autopilot_enabled:
            cmd = self._compute_transit_command(own, control_dt)
            self._publish_command(cmd, stamp)
            self._last_actuator_publish_time = now


def main() -> None:
    if rclpy is None:
        return
    rclpy.init()
    node = L4GuidanceAdapterNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
