"""ROS node wrapper for SIL L4 guidance adapter."""

from __future__ import annotations

import json
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

from std_msgs.msg import String

from .guidance import (
    AVOIDANCE_CORRIDOR_HARD_XTE_M,
    AVOIDANCE_CORRIDOR_SOFT_XTE_M,
    CRUISE_SPEED_KN,
    ActuatorCommand,
    HeadingController,
    SpeedController,
    avoidance_waypoint_heading_deg,
    command_for_heading_speed,
    corridor_guarded_avoidance_heading_deg,
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

        # Cross-run reset flag. Initialized BEFORE the scenario_loaded
        # subscription below: the TRANSIENT_LOCAL latched message fires the
        # callback during create_subscription, so the flag must already exist.
        # The callback only sets this flag; the actual _reset_state runs in the
        # autopilot timer (single-threaded step) to avoid racing latch fields
        # during __init__ and other callbacks (deferred-reset pattern).
        self._scenario_reset_pending = False

        sq = _sensor_qos()
        lq = _latched_qos()
        rq = _reliable_volatile_qos()

        from sil_msgs.msg import LifecycleStatus, OwnShipState
        from l3_external_msgs.msg import CheckerVetoNotification, PlannedRoute
        from l3_msgs.msg import (
            ASDRRecord,
            AvoidancePlan,
            BehaviorPlan,
            MissionGoal,
            ODDState,
            ReactiveOverrideCmd,
            SafetyAlert,
        )

        self._own_msg_cls = OwnShipState
        self._asdr_cls = ASDRRecord
        self._pub_act = self.create_publisher(OwnShipState, "/sil/actuator_cmd", sq)
        self._pub_asdr = self.create_publisher(ASDRRecord, "/l3/asdr/record", rq)

        self.create_subscription(OwnShipState, "/sil/own_ship_state", self._on_own_ship_state, sq)
        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status", self._on_lifecycle_status, sq)
        self.create_subscription(ODDState, "/l3/m1/odd_state", self._on_odd_state, sq)
        self.create_subscription(MissionGoal, "/l3/m3/mission_goal", self._on_mission_goal, sq)
        self.create_subscription(PlannedRoute, "/l2/planned_route", self._on_planned_route, lq)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self._on_behavior_plan, sq)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self._on_avoidance_plan, sq)
        self.create_subscription(ReactiveOverrideCmd, "/l3/m5/reactive_override_cmd", self._on_reactive_override, sq)
        self.create_subscription(SafetyAlert, "/l3/m7/safety_alert", self._on_safety_alert, sq)
        self.create_subscription(CheckerVetoNotification, "/l3/checker/veto", self._on_checker_veto, rq)

        # Cross-run reset: subscribe /sil/scenario_loaded (TRANSIENT_LOCAL) so a
        # new scenario clears actuator gate/latch residual. The callback only
        # arms a deferred reset executed by _autopilot_step.
        self.create_subscription(
            String, "/sil/scenario_loaded", self._on_scenario_loaded, lq)

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
        # Hysteresis latch for the active-avoidance XTE transit regression
        # (see _compute_avoidance_command). Avoids ROT chatter when XTE hovers
        # near the HARD corridor: enter the regression at XTE>=HARD, stay in it
        # until XTE<SOFT, then hand back to avoidance.
        self._avoidance_transit_regression_active = False
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

    def _publish_guidance_asdr(
        self,
        *,
        execution_source: str,
        cmd: ActuatorCommand,
        stamp,
        now: float,
    ) -> None:
        publisher = getattr(self, "_pub_asdr", None)
        asdr_cls = getattr(self, "_asdr_cls", None)
        if publisher is None or asdr_cls is None:
            return

        last_plan_time = getattr(self, "_last_valid_plan_time", None)
        m5_plan_age_s = None
        if last_plan_time is not None:
            m5_plan_age_s = max(0.0, float(now) - float(last_plan_time))
        behavior_plan = getattr(self, "_last_behavior_plan", None)
        target_heading = getattr(self, "_avoidance_target_heading_deg", None)
        payload = {
            "execution_source": str(execution_source),
            "rudder_deg": math.degrees(float(cmd.rudder_angle)),
            "throttle": max(0.0, min(1.0, float(cmd.throttle))),
            "avoidance_active": bool(getattr(self, "_avoidance_active", False)),
            "autopilot_enabled": bool(getattr(self, "_autopilot_enabled", False)),
            "m4_behavior": int(getattr(behavior_plan, "behavior", -1)) if behavior_plan is not None else -1,
            "m5_plan_age_s": m5_plan_age_s,
            "target_heading_deg": float(target_heading) if target_heading is not None else None,
        }

        record = asdr_cls()
        record.schema_version = 113
        record.stamp = stamp
        record.confidence = 1.0
        record.rationale = "L4 guidance execution source"
        record.source_module = "L4_Guidance_Adapter"
        record.decision_type = "guidance_cmd"
        record.decision_json = json.dumps(payload, sort_keys=True, separators=(",", ":"))
        publisher.publish(record)

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

    # BehaviorPlan behavior codes used for latch-release gating.
    # BEHAVIOR_TRANSIT = 0 (normal route following)
    # BEHAVIOR_RECOVERY = 7 (M4 post-avoidance return-to-route, same effect as TRANSIT
    #   for L4 latch purposes: the COLREGs turn is over and the ship must rejoin route).
    _BEHAVIOR_RECOVERY = 7

    def _on_behavior_plan(self, msg) -> None:
        self._last_behavior_plan = msg
        # Fix-C: treat BEHAVIOR_RECOVERY (7) the same as BEHAVIOR_TRANSIT (0) for
        # latch release.  M4 enters RECOVERY when the COLREGs turn is released but
        # XTE is still > 125 m; it stays there until XTE < 125 m AND heading < 10°.
        # Before this fix, behavior=7 hit the else-branch and reset _transit_since_time
        # while L4 kept running avoidance-base transit (A3), which closes XTE very
        # slowly (15° heading) — creating a deadlock that left XTE ~200 m at sim end.
        _transit_or_recovery = (msg.behavior == 0 or
                                msg.behavior == self._BEHAVIOR_RECOVERY)
        if self._avoidance_active:
            if _transit_or_recovery:
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
                msg.behavior != self._BEHAVIOR_RECOVERY and  # RECOVERY ≠ avoidance target
                not self._latch_release_triggered):
            nominal_heading = self._target_heading_deg
            candidate = m4_colregs_window_target_deg(
                msg.heading_min_deg, msg.heading_max_deg, nominal_heading)
            if candidate is not None and should_refresh_m4_colregs_target(
                    self._avoidance_target_heading_deg, nominal_heading, candidate):
                # Fix-B: once the avoidance heading is committed (delta ≥ 10° from
                # nominal), block any refresh that would move it SIGNIFICANTLY back
                # toward nominal.  The M4 heading window tracks the relative bearing
                # to the target, which changes as own-ship manoeuvres — without this
                # guard the latched heading drifts back to 0° (rule17-cr-so stand-on:
                # hdg 63° → 0° → CPA 2 m).
                # Hysteresis (_LOCK_HYSTERESIS_DEG = 5°): allow minor window
                # fluctuations (≤ 5°) through so give-way scenarios can track the
                # growing M4 window naturally; block only when the candidate would
                # reduce evasion by more than 5°.
                _COMMITTED_DELTA_DEG = 10.0
                _LOCK_HYSTERESIS_DEG = 5.0
                if self._avoidance_target_heading_deg is not None:
                    cur_delta = abs(signed_heading_delta_deg(
                        self._avoidance_target_heading_deg, nominal_heading))
                    cand_delta = abs(signed_heading_delta_deg(
                        candidate, nominal_heading))
                    if (cur_delta >= _COMMITTED_DELTA_DEG and
                            cand_delta < cur_delta - _LOCK_HYSTERESIS_DEG):
                        candidate = None  # block: significant evasion reduction
                if candidate is not None:
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
        executable_status = plan_status in ("NORMAL", "DEGRADED")
        has_valid_plan = bool(
            executable_status and
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

    def _on_scenario_loaded(self, msg) -> None:
        """Arm a deferred cross-run reset (executed by _autopilot_step).

        Only sets a flag here; never calls _reset_state directly. Both this node
        and the autopilot timer are driven by a MultiThreadedExecutor, and the
        TRANSIENT_LOCAL subscription fires during __init__ before latch fields
        are initialized — calling _reset_state here would crash. The deferred
        pattern keeps the reset on the timer thread, serialized with all other
        latch access.
        """
        del msg
        self._scenario_reset_pending = True

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

    def _risk_active_colregs_plan(self) -> bool:
        behavior_plan = getattr(self, "_last_behavior_plan", None)
        if behavior_plan is None:
            return False
        behavior = int(getattr(behavior_plan, "behavior", 0))
        if behavior == 0 or behavior == self._BEHAVIOR_RECOVERY:
            return False
        rationale = str(getattr(behavior_plan, "rationale", "")).lower()
        if "risk primary=" not in rationale:
            return False
        return (
            "phase=warning" in rationale or
            "phase=danger" in rationale or
            "phase=critical" in rationale
        )

    def _risk_clear_colregs_plan(self) -> bool:
        behavior_plan = getattr(self, "_last_behavior_plan", None)
        if behavior_plan is None:
            return False
        behavior = int(getattr(behavior_plan, "behavior", 0))
        if behavior == 0 or behavior == self._BEHAVIOR_RECOVERY:
            return False
        rationale = str(getattr(behavior_plan, "rationale", "")).lower()
        return "risk primary=" in rationale and "phase=clear" in rationale

    def _clamp_to_behavior_heading_window(self, heading_deg: float) -> float:
        behavior_plan = getattr(self, "_last_behavior_plan", None)
        if behavior_plan is None:
            return float(heading_deg) % 360.0
        try:
            h_min = float(getattr(behavior_plan, "heading_min_deg"))
            h_max = float(getattr(behavior_plan, "heading_max_deg"))
        except (AttributeError, TypeError, ValueError):
            return float(heading_deg) % 360.0
        if not (math.isfinite(h_min) and math.isfinite(h_max)):
            return float(heading_deg) % 360.0
        if h_max < h_min:
            h_max += 360.0
        if h_max - h_min > 300.0:
            return float(heading_deg) % 360.0

        candidates = [float(heading_deg) + offset for offset in (-360.0, 0.0, 360.0)]
        inside = [candidate for candidate in candidates if h_min <= candidate <= h_max]
        if inside:
            return min(
                inside,
                key=lambda candidate: abs(signed_heading_delta_deg(candidate, heading_deg)),
            ) % 360.0
        boundary = min(
            (h_min, h_max),
            key=lambda candidate: abs(signed_heading_delta_deg(candidate, heading_deg)),
        )
        return boundary % 360.0

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

        # XTE corridor regression shared by latch-release and active-avoidance
        # paths. When the own ship has been pushed so far off-track (XTE >= HARD
        # corridor) that the corridor guard would otherwise saturate the
        # avoidance heading back to nominal and lock the rudder (沿航线走不减小 XTE
        # → dead-lock), enter CPA-aware regression mode. The active-avoidance
        # gate restores the regression removed in 0a6187c0 (which dropped the
        # RETURN_XTE_M gate); it uses the HARD corridor threshold.
        #
        # Hysteresis: enter the regression at XTE>=HARD (280 m), stay in it until
        # XTE<SOFT (180 m) before handing back to avoidance. Without this dead-
        # band, XTE hovering near 280 m makes the adapter flip avoidance(±85°)
        # ↔ transit(±30° XTE-correction) every cycle and the ROT sign flips rack
        # up steering_reversals past the stability gate (observed 6-11 > 4).
        #
        # Fix-A3 (CPA-aware avoidance transit): Previously this called pure
        # _compute_transit_command which uses nominal heading (0°) + XTE
        # correction. When the avoidance heading is 85° starboard and transit
        # corrects to -56° (port of nominal), the 141° heading jump causes 8
        # steering reversals AND reduces CPA (e.g. rule14-ho-port cpa_ok=False).
        # Fix: use _compute_avoidance_transit_command which applies the XTE
        # correction to the avoidance heading as base instead of nominal, so
        # CPA is preserved and heading jump is eliminated.
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
        risk_active_plan = self._risk_active_colregs_plan()
        if risk_active_plan and waypoint_heading is not None:
            selected_heading = self._clamp_to_behavior_heading_window(waypoint_heading)
        else:
            selected_heading = select_avoidance_heading(
                waypoint_heading_deg=waypoint_heading,
                avoidance_target_heading_deg=self._avoidance_target_heading_deg,
                nominal_heading_deg=self._target_heading_deg,
            )
        risk_active_colregs_waypoint = (
            not self._latch_release_triggered and
            selected_heading is not None and
            risk_active_plan
        )
        active_xte_m = signed_xte_m(
            self._route_wps,
            own["lat"],
            own["lon"],
            self._current_target_wp_lat,
            self._current_target_wp_lon,
            fallback_heading_deg=self._target_heading_deg,
        )
        abs_active_xte_m = (
            abs(active_xte_m) if (active_xte_m is not None and
                                  math.isfinite(active_xte_m)) else 0.0)
        # getattr default keeps tests that build the node via __new__ working
        # without setting the hysteresis latch explicitly.
        regression_active = getattr(
            self, "_avoidance_transit_regression_active", False)
        if risk_active_colregs_waypoint:
            regression_active = False
        elif abs_active_xte_m >= AVOIDANCE_CORRIDOR_HARD_XTE_M:
            regression_active = True
        elif abs_active_xte_m < AVOIDANCE_CORRIDOR_SOFT_XTE_M:
            regression_active = False
        self._avoidance_transit_regression_active = regression_active
        if regression_active:
            # Fix-A3v2: use CPA-aware avoidance transit (avoidance heading as base)
            # only during active avoidance (target still a threat). During RECOVERY
            # (latch_release_triggered), the target has cleared CPA and we need fast
            # route return — use plain transit (nominal as base) to avoid blocking
            # the A1 heading gate (|hdg_error| <= 10°) needed for RECOVERY→TRANSIT.
            if self._latch_release_triggered:
                return self._compute_transit_command(own, dt)
            if self._risk_clear_colregs_plan():
                return self._compute_transit_command(own, dt)
            return self._compute_avoidance_transit_command(own, dt)
        if not risk_active_colregs_waypoint:
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
        last_waypoint = getattr(self, "_last_avoidance_waypoint", None)
        if last_waypoint is not None:
            waypoint_target_speed_kn = float(getattr(
                last_waypoint, "target_speed_kn", CRUISE_SPEED_KN))
            route_speed_cap_kn = float(self._target_sog_kn)
            if not math.isfinite(route_speed_cap_kn) or route_speed_cap_kn <= 0.0:
                route_speed_cap_kn = CRUISE_SPEED_KN
            route_speed_cap_kn = max(
                route_speed_cap_kn,
                float(own.get("sog_kn", 0.0)),
                CRUISE_SPEED_KN,
            )
            behavior_rationale = str(
                getattr(getattr(self, "_last_behavior_plan", None), "rationale", ""))
            reduce_speed_requested = "speed_reduction_preferred=true" in behavior_rationale
            target_speed_override = (
                waypoint_target_speed_kn
                if reduce_speed_requested and waypoint_target_speed_kn < route_speed_cap_kn
                else route_speed_cap_kn
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

    def _compute_avoidance_transit_command(self, own, dt: float = 0.5) -> ActuatorCommand:
        """CPA-aware regression transit: XTE correction applied to a CAPPED avoidance
        heading as base instead of nominal heading.

        When XTE >= HARD corridor the corridor guard would saturate the avoidance
        heading to nominal, causing a deadlock (ship drifts along route, XTE
        never reduces).  The plain transit command fixes the deadlock but uses
        nominal (0°) as the base heading, causing a large heading jump (85° →
        -56°) that racks up steering_reversals AND closes CPA when the target is
        near the return path.

        This variant anchors the XTE correction to the avoidance heading (capped at
        _REGRESSION_BASE_CAP_DEG from nominal) so the ship returns toward route
        while maintaining sufficient COLREGs-compliant lateral separation.

        The cap (_REGRESSION_BASE_CAP_DEG = 40°) ensures minimum XTE closure rate
        ≥ sin(40° - correction°) ≈ 2 m/s even at maximum XTE, avoiding the
        near-zero closure (0.9 m/s) that occurs with raw avoidance headings of 78-85°
        which caused rule13-ot seamanship gate failure (int_abs_xte 370k > 300k limit).
        """
        # _REGRESSION_BASE_CAP_DEG: maximum deviation from nominal used as base.
        # Smaller values → faster XTE return (less CPA protection).
        # Larger values → more CPA protection (slower XTE return).
        # Calibration from two sim data points:
        #   cap=40°: rule14-ho-port CPA=118m (FAIL <180m), rule13-ot seamanship=239k (PASS <300k)
        #   cap=72° (uncapped): rule14-ho-port CPA=290m (PASS), rule13-ot seamanship=370k (FAIL)
        # Linear interpolation: cap=55° → estimated CPA≈200m (PASS), seamanship≈290k (PASS).
        _REGRESSION_BASE_CAP_DEG = 55.0
        avoidance_base = self._avoidance_target_heading_deg
        if avoidance_base is None:
            # Avoidance heading not yet set; fall back to plain transit.
            return self._compute_transit_command(own, dt)
        # Cap the avoidance base heading magnitude at _REGRESSION_BASE_CAP_DEG
        # from nominal while preserving sign (starboard/port side).
        nominal = self._target_heading_deg
        delta = signed_heading_delta_deg(avoidance_base, nominal)
        if abs(delta) > _REGRESSION_BASE_CAP_DEG:
            capped_delta = math.copysign(_REGRESSION_BASE_CAP_DEG, delta)
            avoidance_base = (nominal + capped_delta) % 360.0
        target_sog_kn = self._target_sog_kn
        last_waypoint = getattr(self, "_last_avoidance_waypoint", None)
        if last_waypoint is not None:
            waypoint_target_speed_kn = float(getattr(
                last_waypoint, "target_speed_kn", CRUISE_SPEED_KN))
            if not math.isfinite(target_sog_kn) or target_sog_kn <= 0.0:
                target_sog_kn = CRUISE_SPEED_KN
            target_sog_kn = max(
                target_sog_kn,
                float(own.get("sog_kn", 0.0)),
                CRUISE_SPEED_KN,
            )
            behavior_rationale = str(
                getattr(getattr(self, "_last_behavior_plan", None), "rationale", ""))
            reduce_speed_requested = "speed_reduction_preferred=true" in behavior_rationale
            target_sog_kn = (
                waypoint_target_speed_kn
                if reduce_speed_requested and waypoint_target_speed_kn < target_sog_kn
                else max(target_sog_kn, waypoint_target_speed_kn)
            )
        return compute_transit_command(
            current_heading_deg=own["heading_deg"],
            current_sog_kn=own["sog_kn"],
            current_rot_deg_s=own["rot_deg_s"],
            own_lat=own["lat"],
            own_lon=own["lon"],
            target_heading_deg=avoidance_base,
            target_sog_kn=target_sog_kn,
            current_target_wp_lat=self._current_target_wp_lat,
            current_target_wp_lon=self._current_target_wp_lon,
            route_wps=self._route_wps,
            heading_controller=self._avoidance_heading_controller,
            speed_controller=self._speed_controller,
            limit_speed_for_route_return=False,
            dt=dt,
        )


    def _autopilot_step(self) -> None:
        # Deferred cross-run reset: if /sil/scenario_loaded fired since the last
        # step, clear actuator gate/latch state now (on this timer thread).
        if self._scenario_reset_pending:
            self._scenario_reset_pending = False
            self.get_logger().info(
                "[l4_guidance_adapter] scenario_loaded — resetting cross-run actuator state")
            self._reset_state(clear_route=False)
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
            self._publish_guidance_asdr(
                execution_source="safety_gate", cmd=safe, stamp=stamp, now=now)
            self._last_actuator_publish_time = now
            return

        own = self._current_ownship()
        override = self._active_override(now)
        if override is not None:
            cmd = self._compute_override_command(override, own, control_dt)
            self._publish_command(cmd, stamp)
            self._publish_guidance_asdr(
                execution_source="reactive_override", cmd=cmd, stamp=stamp, now=now)
            self._last_actuator_publish_time = now
            return

        if self._avoidance_active:
            cmd = self._compute_avoidance_command(own, control_dt)
            self._publish_command(cmd, stamp)
            self._publish_guidance_asdr(
                execution_source="avoidance", cmd=cmd, stamp=stamp, now=now)
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
            self._publish_guidance_asdr(
                execution_source="transit", cmd=cmd, stamp=stamp, now=now)
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
