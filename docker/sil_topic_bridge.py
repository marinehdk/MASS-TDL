#!/usr/bin/env python3
"""SIL ↔ L3 Kernel Topic Bridge — bidirectional relay.

SIL→L3:
  /sil/own_ship_state        → /fusion/own_ship_state   (sil→l3_external)
  /sil/target_vessel_state   → /fusion/tracked_targets  (single→array)
  /sil/environment           → /fusion/environment_state (field rename)

L3→SIL:
  /l3/m5/avoidance_plan      → /sil/actuator_cmd        (waypoint→rudder/throttle)
  /l3/asdr/record            → /sil/asdr_event           (decision log)
  /l3/m8/ui_state            → /sil/m8_ui_state          (passthrough)

Module pulse:
  M1-M8 heartbeat topics → /sil/module_pulse @ 1 Hz aggregate
"""

import math
import signal
import threading
import time

import rclpy
from rclpy.node import Node
from rclpy.executors import MultiThreadedExecutor
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

# ── SIL message types ────────────────────────────────────────
from sil_msgs.msg import (
    OwnShipState as SilOwnShipState,
    TargetVesselState,
    EnvironmentState as SilEnvironmentState,
    ModulePulse,
    ASDREvent,
    BridgeState,
)

# ── Cross-layer L3 external message types ────────────────────
from l3_external_msgs.msg import (
    PlannedRoute,
    FilteredOwnShipState,
    TrackedTargetArray,
    EnvironmentState as L3EnvironmentState,
)

# ── L3 internal message types ────────────────────────────────
from l3_msgs.msg import (
    AvoidancePlan,
    AvoidanceWaypoint,
    ASDRRecord,
    UIState,
    ODDState,
    WorldState,
    MissionGoal,
    BehaviorPlan,
    COLREGsConstraint,
    TrackedTarget,
    ThreatState,
)

from std_msgs.msg import Header


# Module ID constants (matching sil_msgs/ModulePulse)
M1 = 1
M2 = 2
M3 = 3
M4 = 4
M5 = 5
M6 = 6
M7 = 7
M8 = 8

# Health states
HEALTH_GREEN = 1
HEALTH_RED = 3

# Pulse timeout: if no message seen in this many seconds, module declared RED
PULSE_TIMEOUT_S = 10.0

# Max rudder angle for DEMO-1
MAX_RUDDER_DEG = 35.0
MAX_RUDDER_RAD = math.radians(MAX_RUDDER_DEG)
MAX_SPEED_KN = 25.0
# Minimum cruise speed: M5 mid_mpc output target_speed_kn can be near-zero during
# the transit phase (MPC horizon conservatism).  Clamping at CRUISE_SPEED_KN ensures
# the propeller stays on the positive-thrust side of the K_T curve.
# For Rule-14 head-on avoidance (Demo-1) COLREGs requires heading change only — no
# speed reduction.  This constant must match ownShip.initial.sog in the scenario YAML.
CRUISE_SPEED_KN = 10.0
SHIP_LENGTH_M = 46.0   # FCB approximate length for turn-radius→rudder conversion

RUDDER_SIGN = -1  # MMG convention: positive delta → PORT turn; bridge uses positive = starboard

# ── QoS profiles ─────────────────────────────────────────────

def _sensor_qos(depth: int = 5) -> QoSProfile:
    return QoSProfile(
        reliability=QoSReliabilityPolicy.BEST_EFFORT,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


def _latched_qos(depth: int = 50) -> QoSProfile:
    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


def _reliable_volatile_qos(depth: int = 10) -> QoSProfile:
    return QoSProfile(
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.VOLATILE,
        history=QoSHistoryPolicy.KEEP_LAST,
        depth=depth,
    )


# ── Autopilot controllers ────────────────────────────────────

class HeadingController:
    def __init__(self, Kp: float = 1.0, max_rate_deg_s: float = 5.0):
        self.Kp = Kp
        self.max_rate_deg_s = max_rate_deg_s
        self.last_cmd_deg = 0.0

    def step(self, error_deg: float, dt: float,
             current_rot_deg_s: float = 0.0) -> float:
        error_deg = (error_deg + 180.0) % 360.0 - 180.0
        cmd_deg = self.Kp * error_deg
        cmd_deg = max(-35.0, min(35.0, cmd_deg))
        max_delta = self.max_rate_deg_s * dt
        cmd_deg = max(self.last_cmd_deg - max_delta,
                      min(self.last_cmd_deg + max_delta, cmd_deg))
        self.last_cmd_deg = cmd_deg
        return math.radians(cmd_deg)


class SpeedController:
    def __init__(self, Kp: float = 0.15, Ki: float = 0.02, max_rate: float = 0.5):
        self.Kp = Kp
        self.Ki = Ki
        self.max_rate = max_rate
        self.integral = 0.0
        self.last_cmd = 0.0

    def step(self, error_kn: float, dt: float) -> float:
        p_term = self.Kp * error_kn
        self.integral += error_kn * dt
        self.integral = max(-5.0, min(5.0, self.integral))
        i_term = self.Ki * self.integral
        cmd = p_term + i_term
        max_delta = self.max_rate * dt
        cmd = max(self.last_cmd - max_delta,
                  min(self.last_cmd + max_delta, cmd))
        cmd = max(0.0, min(1.0, cmd))
        self.last_cmd = cmd
        return cmd


# ── Bridge node ──────────────────────────────────────────────

class SilTopicBridge(Node):
    """Bidirectional topic bridge between SIL and L3 kernel namespaces."""

    def __init__(self) -> None:
        super().__init__("sil_topic_bridge")
        self.get_logger().info("[sil_topic_bridge] Bridge active")

        sq = _sensor_qos()
        lq = _latched_qos()
        rq = _reliable_volatile_qos()

        # ── Autopilot state ─────────────────────────────────
        self._autopilot_enabled = False
        self._last_valid_plan_time = None
        self._last_avoidance_waypoint = None
        self._last_odd_state = None
        self._last_behavior_plan = None
        self._last_ownship_raw = None
        self._last_actuator_publish_time = None
        self._heading_controller = HeadingController(
            Kp=1.0, max_rate_deg_s=5.0)
        self._avoidance_heading_controller = HeadingController(
            Kp=1.0, max_rate_deg_s=10.0)
        self._avoidance_active = False
        self._avoidance_target_heading_deg = None
        self._speed_controller = SpeedController()
        self._current_target_wp_lat = 0.0
        self._current_target_wp_lon = 0.0
        self._route_wps = []  # [(lat,lon)] cached planned route for geometric XTE

        self.declare_parameter("ownship_initial_heading_deg", 0.0)
        self.declare_parameter("ownship_initial_sog_kn", CRUISE_SPEED_KN)
        self._target_heading_deg = self.get_parameter(
            "ownship_initial_heading_deg").value
        self._target_sog_kn = self.get_parameter(
            "ownship_initial_sog_kn").value
        self.get_logger().info(
            f"[autopilot] Target heading={self._target_heading_deg}°, "
            f"sog={self._target_sog_kn} kn")

        # ── SIL→L3 bridges ──────────────────────────────────

        # 1. OwnShipState → FilteredOwnShipState
        self._sub_oss = self.create_subscription(
            SilOwnShipState, "/sil/own_ship_state",
            self._on_own_ship_state, sq)
        self._pub_foss = self.create_publisher(
            FilteredOwnShipState, "/fusion/own_ship_state", rq)

        # 2. TargetVesselState → TrackedTargetArray
        self._sub_tvs = self.create_subscription(
            TargetVesselState, "/sil/target_vessel_state",
            self._on_target_vessel_state, sq)
        self._pub_tta = self.create_publisher(
            TrackedTargetArray, "/fusion/tracked_targets", rq)

        # 3. EnvironmentState → L3 EnvironmentState
        self._sub_env = self.create_subscription(
            SilEnvironmentState, "/sil/environment",
            self._on_environment_state, sq)
        self._pub_env = self.create_publisher(
            L3EnvironmentState, "/fusion/environment_state", lq)

        # ── L3→SIL bridges ──────────────────────────────────

        # 4. AvoidancePlan → actuator command as OwnShipState (rudder_angle + throttle)
        self._sub_plan = self.create_subscription(
            AvoidancePlan, "/l3/m5/avoidance_plan",
            self._on_avoidance_plan, sq)
        self._pub_act = self.create_publisher(
            SilOwnShipState, "/sil/actuator_cmd", sq)

        # 5. ASDRRecord → ASDREvent
        self._sub_asdr = self.create_subscription(
            ASDRRecord, "/l3/asdr/record",
            self._on_asdr_record, rq)
        self._pub_asdr = self.create_publisher(
            ASDREvent, "/sil/asdr_event", lq)

        # 6. UIState → UIState (passthrough)
        self._sub_ui = self.create_subscription(
            UIState, "/l3/m8/ui_state",
            self._on_ui_state, sq)
        self._pub_ui = self.create_publisher(
            UIState, "/sil/m8_ui_state", sq)

        # ── Module pulse aggregator ─────────────────────────
        self._pulse_lock = threading.Lock()
        self._last_seen: dict[int, float] = {}  # module_id → time.monotonic()

        self._sub_m1 = self.create_subscription(
            ODDState, "/l3/m1/odd_state",
            self._on_odd_state, sq)
        self._sub_m2 = self.create_subscription(
            WorldState, "/l3/m2/world_state",
            lambda msg: self._record_pulse(M2), sq)
        self._sub_m3 = self.create_subscription(
            MissionGoal, "/l3/m3/mission_goal",
            self._on_mission_goal, sq)
        self._sub_route = self.create_subscription(
            PlannedRoute, "/l2/planned_route",
            self._on_planned_route, _latched_qos())
        self._sub_m4 = self.create_subscription(
            BehaviorPlan, "/l3/m4/behavior_plan",
            self._on_behavior_plan, sq)
        self._sub_m6 = self.create_subscription(
            COLREGsConstraint, "/l3/m6/colregs_constraint",
            lambda msg: self._record_pulse(M6), sq)
        self._sub_m7_heartbeat = self.create_subscription(
            Header, "/l3/m7/heartbeat",
            lambda msg: self._record_pulse(M7), sq)

        # ── M2 Threat State subscription ──────────────────────
        self._sub_threat = self.create_subscription(
            ThreatState, "/l3/m2/threat_state",
            self._on_threat_state, sq)

        # ── LATCH release state ───────────────────────────────
        self._latch_release_triggered = False
        self._latch_release_time = None
        self._latch_offset_at_release_deg = None
        self._latch_release_progress = 0.0
        # Sim-time at which avoidance was last armed; release is suppressed
        # for LATCH_MIN_HOLD_S after arming to prevent instant-release when
        # M4 has already reverted to TRANSIT by the time M5's plan arrives.
        self._avoidance_armed_time: float | None = None
        self._LATCH_MIN_HOLD_S: float = 8.0  # minimum sim-seconds to hold avoidance
        # Guard: do not arm avoidance before M3 has reached FSM_ACTIVE (≥3) at
        # least once in this scenario.  Prevents M5 cold-start plans from
        # arming the bridge before the scenario state is fully established.
        self._m3_activated_once: bool = False

        # ── Bridge state publisher ────────────────────────────
        self._pub_bridge_state = self.create_publisher(
            BridgeState, "/sil/bridge_state", sq)

        # M5 and M8 pulse recorded inside _on_avoidance_plan / _on_ui_state

        self._pub_pulse = self.create_publisher(
            ModulePulse, "/sil/module_pulse", sq)
        self._pulse_timer = self.create_timer(1.0, self._publish_module_pulse)

        self._autopilot_timer = self.create_timer(0.5, self._autopilot_step)

    def _get_sim_time(self) -> float:
        return self.get_clock().now().nanoseconds * 1e-9

    # ── Pulse recording helper ───────────────────────────────

    def _record_pulse(self, module_id: int) -> None:
        with self._pulse_lock:
            self._last_seen[module_id] = time.monotonic()

    def _module_health(self, module_id: int) -> int:
        with self._pulse_lock:
            t = self._last_seen.get(module_id)
        if t is None:
            return HEALTH_RED
        if time.monotonic() - t > PULSE_TIMEOUT_S:
            return HEALTH_RED
        return HEALTH_GREEN

    def _publish_module_pulse(self) -> None:
        now = self.get_clock().now().to_msg()
        for mid in (M1, M2, M3, M4, M5, M6, M7, M8):
            msg = ModulePulse()
            msg.stamp = now
            msg.module_id = mid
            msg.state = self._module_health(mid)
            msg.latency_ms = 0
            msg.message_drops = 0
            self._pub_pulse.publish(msg)

    # ── Autopilot callbacks ────────────────────────────────────

    def _on_odd_state(self, msg: ODDState) -> None:
        self._record_pulse(M1)
        self._last_odd_state = msg

    def _on_behavior_plan(self, msg: BehaviorPlan) -> None:
        self._record_pulse(M4)
        self._last_behavior_plan = msg
        if (self._avoidance_active and
                self._avoidance_target_heading_deg is None):
            h_min = float(msg.heading_min_deg)
            h_max = float(msg.heading_max_deg)
            if h_max < h_min:
                h_max += 360.0
            h_span = h_max - h_min
            if h_span > 300.0:
                # Degenerate window (≈full circle): M4 has no meaningful
                # heading constraint yet.  Leave target as None so the bridge
                # falls back to M5 waypoint-radius rudder rather than picking
                # a nonsensical port-side heading via the 5/6 formula.
                print(f"[BRIDGE] DELAYED-LATCH skipped — degenerate M4 window "
                      f"[{h_min:.1f},{h_max:.1f}] span={h_span:.1f}° "
                      f"(fallback: M5 waypoint rudder)", flush=True)
                return
            self._avoidance_target_heading_deg = (
                h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0
            print(f"[BRIDGE] DELAYED-LATCH target_heading="
                  f"{self._avoidance_target_heading_deg:.1f}deg "
                  f"from M4 window [{h_min:.1f}, {h_max:.1f}]",
                  flush=True)

    # ── SIL→L3 callbacks ────────────────────────────────────

    def _on_own_ship_state(self, msg: SilOwnShipState) -> None:
        self._record_pulse(M2)
        self._last_ownship_raw = msg
        out = FilteredOwnShipState()
        out.schema_version = 112
        out.stamp = msg.stamp
        out.position.latitude = msg.lat
        out.position.longitude = msg.lon
        out.position.altitude = 0.0
        out.heading_deg = math.degrees(msg.heading)  # rad → deg
        out.sog_kn = msg.sog * 1.94384               # m/s → kn
        out.cog_deg = math.degrees(msg.cog)          # rad → deg
        out.u_water = msg.u                      # m/s → m/s
        out.v_water = msg.v                      # m/s → m/s
        out.r_dot_deg_s = msg.r * 180.0 / math.pi  # rad/s → deg/s
        out.current_speed_kn = 0.0
        out.current_direction_deg = 0.0
        out.roll_deg = 0.0
        out.pitch_deg = 0.0
        # 6×6 identity covariance
        for i in range(6):
            out.covariance[i * 6 + i] = 1.0
        out.nav_mode = "OPTIMAL"
        out.confidence = 0.9
        out.rationale = "SIL bridge"
        self._pub_foss.publish(out)

    def _on_target_vessel_state(self, msg: TargetVesselState) -> None:
        tgt = TrackedTarget()
        tgt.schema_version = 112
        tgt.stamp = msg.stamp
        tgt.target_id = msg.mmsi
        tgt.position.latitude = msg.lat
        tgt.position.longitude = msg.lon
        tgt.position.altitude = 0.0
        tgt.heading_deg = math.degrees(msg.heading)
        tgt.sog_kn = msg.sog * 1.94384
        tgt.cog_deg = math.degrees(msg.cog)
        # 3×3 identity covariance
        for i in range(3):
            tgt.covariance[i * 3 + i] = 1.0
        tgt.classification = "vessel"
        tgt.classification_confidence = 0.85
        tgt.cpa_m = 0.0
        tgt.tcpa_s = 0.0
        tgt.confidence = 0.85
        tgt.rationale = "SIL bridge"
        tgt.source_sensor = "fused"

        out = TrackedTargetArray()
        out.schema_version = 112
        out.stamp = msg.stamp
        out.targets = [tgt]
        out.confidence = 0.85
        out.rationale = "SIL bridge"
        self._pub_tta.publish(out)

    def _on_environment_state(self, msg: SilEnvironmentState) -> None:
        out = L3EnvironmentState()
        out.schema_version = 112
        out.stamp = msg.stamp
        out.wind_speed_kn = msg.wind_speed_mps * 1.94384   # m/s → kn
        out.wind_direction_deg = msg.wind_direction          # deg → deg
        out.wave_height_m = 0.0
        out.wave_direction_deg = 0.0
        out.current_speed_kn = msg.current_speed_mps * 1.94384  # m/s → kn
        out.current_direction_deg = msg.current_direction        # deg → deg
        out.visibility_range_nm = msg.visibility_nm              # nm → nm
        out.weather_source = "sensor"
        out.confidence = 0.9
        out.rationale = "SIL bridge"
        self._pub_env.publish(out)

    def _latch_hold_elapsed(self) -> bool:
        """Return True if the minimum avoidance hold time has elapsed since arming."""
        if self._avoidance_armed_time is None:
            return True
        return (self._get_sim_time() - self._avoidance_armed_time) >= self._LATCH_MIN_HOLD_S

    def _on_threat_state(self, msg: ThreatState) -> None:
        """M2 threat state callback — check CPA-cleared release condition."""
        self._record_pulse(M2)
        # Condition 1: cpa_status == cleared && target astern
        if (hasattr(msg, 'cpa_status') and msg.cpa_status == "cleared" and
            hasattr(msg, 'target_relative_position') and msg.target_relative_position == "astern" and
            not self._latch_release_triggered and
            self._latch_hold_elapsed()):
            self.get_logger().info(
                "[BRIDGE] LATCH release condition 1 triggered: CPA cleared, target astern")
            self._trigger_latch_release()

    def _on_mission_goal(self, msg: MissionGoal) -> None:
        """M3 mission goal callback — check task_validity + behavior release condition."""
        self._record_pulse(M3)
        
        # Reset state if mission is not ACTIVE (FSM_ACTIVE = 3)
        if msg.fsm_state < 3:
            self._avoidance_active = False
            self._avoidance_target_heading_deg = None
            self._avoidance_heading_controller.last_cmd_deg = 0.0
            self._reset_latch_release_state()
            self._current_target_wp_lat = 0.0
            self._current_target_wp_lon = 0.0
            self._m3_activated_once = False  # require re-activation in new scenario
            return

        # M3 has reached ACTIVE: lift the cold-start arm guard.
        self._m3_activated_once = True

        if (abs(msg.current_target_wp.latitude) > 1e-4 or
                abs(msg.current_target_wp.longitude) > 1e-4):
            self._current_target_wp_lat = float(msg.current_target_wp.latitude)
            self._current_target_wp_lon = float(msg.current_target_wp.longitude)

        # Condition 2: task_validity == valid && behavior == TRANSIT
        # Guard: only release after LATCH_MIN_HOLD_S sim-seconds of avoidance,
        # preventing instant release when M4 has already cycled back to TRANSIT
        # by the time this callback fires (M4 races ahead of M5 plan delivery).
        task_valid = hasattr(msg, 'task_validity') and msg.task_validity in (1, "valid")
        behavior_transit = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior == 0  # BEHAVIOR_TRANSIT
        )
        if (task_valid and behavior_transit and
                not self._latch_release_triggered and
                self._latch_hold_elapsed()):
            self.get_logger().info(
                "[BRIDGE] LATCH release condition 2 triggered: task_valid + TRANSIT behavior")
            self._trigger_latch_release()

    def _trigger_latch_release(self) -> None:
        """Snapshot current LATCH offset and start 5s linear decay."""
        if self._avoidance_target_heading_deg is None:
            return
        
        self._latch_release_triggered = True
        self._latch_release_time = self._get_sim_time()
        # Account for 360-degree wrap-around when calculating target offset
        diff = (self._avoidance_target_heading_deg - self._target_heading_deg + 180.0) % 360.0 - 180.0
        self._latch_offset_at_release_deg = abs(diff)
        self._latch_release_progress = 0.0
        self.get_logger().info(
            f"[BRIDGE] LATCH release started: offset_deg={self._latch_offset_at_release_deg:.1f}")

    def _reset_latch_release_state(self) -> None:
        """Reset all latch release variables."""
        self._latch_release_triggered = False
        self._latch_release_time = None
        self._latch_offset_at_release_deg = None
        self._latch_release_progress = 0.0
        self._avoidance_armed_time = None

    def _compute_latch_offset(self, t_release: float, t_now: float,
                               current_offset_deg: float) -> float:
        """Linearly decay LATCH offset from snapshot to 0 over 5 seconds."""
        if not self._latch_release_triggered or self._latch_offset_at_release_deg is None:
            return current_offset_deg
        
        t_elapsed = t_now - t_release
        decay_duration_s = 5.0
        
        progress = max(0.0, min(1.0, t_elapsed / decay_duration_s))
        self._latch_release_progress = progress
        if progress >= 1.0:
            return 0.0
        
        return self._latch_offset_at_release_deg * (1.0 - progress)

    def _publish_bridge_state(self) -> None:
        """Publish bridge state for Foxglove visualization."""
        msg = BridgeState()
        msg.stamp = self.get_clock().now().to_msg()
        msg.latch_state = "releasing" if self._latch_release_triggered else "latched"
        msg.target_heading_deg = self._avoidance_target_heading_deg or self._target_heading_deg
        msg.release_progress = self._latch_release_progress
        msg.current_offset_deg = self._latch_offset_at_release_deg or 0.0
        self._pub_bridge_state.publish(msg)

    # ── L3→SIL callbacks ────────────────────────────────────

    def _make_actuator_msg(self, stamp) -> SilOwnShipState:
        out = SilOwnShipState()
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
        return out

    def _on_avoidance_plan(self, msg: AvoidancePlan) -> None:
        self._record_pulse(M5)

        has_valid_plan = (
            len(msg.waypoints) > 0 and
            abs(msg.waypoints[0].turn_radius_m) > 1e-6
        )
        if has_valid_plan:
            self._last_valid_plan_time = self._get_sim_time()
            self._last_avoidance_waypoint = msg.waypoints[0]

        if self._autopilot_enabled and not has_valid_plan:
            if self._avoidance_active:
                print("[BRIDGE] RESET — valid plan lost while autopilot enabled",
                      flush=True)
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()
        elif has_valid_plan:
            if not self._avoidance_active:
                # Guard: refuse to arm before M3 has reached FSM_ACTIVE at
                # least once.  M5 can deliver cold-start NLP solutions within
                # the first ~2s before the scenario state is stable; arming
                # on those leads to incorrect avoidance targets.
                if not self._m3_activated_once:
                    print("[BRIDGE] AVOIDANCE ARM suppressed — M3 not yet "
                          "ACTIVE in this scenario (cold-start guard)",
                          flush=True)
                    return
                # Arm avoidance whenever M5 delivers a valid plan with non-zero
                # turn_radius — do NOT gate on current M4 behavior, because M4
                # cycles at 1-4 Hz and may have already reverted to TRANSIT by
                # the time M5's plan (computed 1+ s later) arrives here.
                self._avoidance_active = True
                self._avoidance_armed_time = self._get_sim_time()
                self._reset_latch_release_state()
                # Use M4 heading window if it shows a non-TRANSIT (avoidance)
                # behavior; otherwise derive target from the waypoint bearing.
                if (self._last_behavior_plan is not None and
                        self._last_behavior_plan.behavior != 0 and
                        self._last_behavior_plan.heading_max_deg > 0.0):
                    beh = self._last_behavior_plan
                    h_min = float(beh.heading_min_deg)
                    h_max = float(beh.heading_max_deg)
                    if h_max < h_min:
                        h_max += 360.0
                    h_span = h_max - h_min
                    if h_span > 300.0:
                        # Degenerate window (≈full circle): leave None so the
                        # bridge uses M5 waypoint-radius rudder as fallback.
                        print(f"[BRIDGE] LATCHED (degenerate M4 window "
                              f"[{h_min:.1f},{h_max:.1f}] span={h_span:.1f}°) "
                              f"— target_heading deferred to M5 waypoint rudder",
                              flush=True)
                    else:
                        self._avoidance_target_heading_deg = (
                            h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0
                        print(f"[BRIDGE] LATCHED target_heading="
                              f"{self._avoidance_target_heading_deg:.1f}deg "
                              f"from M4 window [{h_min:.1f}, {h_max:.1f}]",
                              flush=True)
                else:
                    # M4 already reverted to TRANSIT — use the last known
                    # avoidance heading window stored from any prior M4 AVOID
                    # message, or leave None so _on_behavior_plan fills it in.
                    print("[BRIDGE] LATCHED (M4 already TRANSIT) — "
                          "target_heading deferred to next M4 AVOID window",
                          flush=True)
        else:
            if self._avoidance_active:
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()

    def _on_asdr_record(self, msg: ASDRRecord) -> None:
        out = ASDREvent()
        out.stamp = msg.stamp
        out.event_type = msg.decision_type
        out.rule_ref = msg.source_module
        out.decision_id = (msg.rationale[:64] if msg.rationale else "")
        out.verdict = 0  # PASS (default)
        out.payload_json = msg.decision_json
        self._pub_asdr.publish(out)

    def _on_ui_state(self, msg: UIState) -> None:
        self._record_pulse(M8)
        self._pub_ui.publish(msg)

    # ── Autopilot logic ────────────────────────────────────────

    def _autopilot_step(self) -> None:
        self._publish_bridge_state()
        if self._avoidance_active:
            now = self._get_sim_time()
            should_publish = (
                self._last_actuator_publish_time is None or
                (now - self._last_actuator_publish_time) > 0.5)
            if should_publish:
                stamp = self.get_clock().now().to_msg()
                out = self._compute_avoidance_autopilot(stamp)
                self._pub_act.publish(out)
                self._last_actuator_publish_time = now
            return

        if self._last_odd_state is None:
            return

        now = self._get_sim_time()
        staleness_s = (
            (now - self._last_valid_plan_time)
            if self._last_valid_plan_time else float('inf'))

        # F4-I-1 follow-up: autopilot should engage whenever M5 cannot drive
        # the ship and M7 has NOT taken full MRC control. ENVELOPE_IN (0),
        # ENVELOPE_EDGE (1) and ENVELOPE_MRC_PREP (3) are all states where
        # the Doer remains in command; only ENVELOPE_MRC_ACTIVE (4) means
        # M7 has fully seized actuation. ENVELOPE_OUT (2) is a hard fail
        # that requires operator handoff, so autopilot should also stand down.
        env_state = self._last_odd_state.envelope_state
        env_allows_autopilot = env_state in (
            ODDState.ENVELOPE_IN,
            ODDState.ENVELOPE_EDGE,
            ODDState.ENVELOPE_MRC_PREP,
        )
        is_m5_stale = staleness_s > 10.0
        m4_in_fallback = (
            self._last_behavior_plan is not None and
            "fallback" in self._last_behavior_plan.rationale.lower())
        m4_in_transit = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior == 0  # BEHAVIOR_TRANSIT
        )

        was_enabled = self._autopilot_enabled
        self._autopilot_enabled = (
            env_allows_autopilot and (is_m5_stale or m4_in_fallback or m4_in_transit))

        # F4-I-5: on rising edge, publish immediately rather than waiting up to
        # 1.5 s for the next periodic tick (otherwise there is a dead-stick
        # window the moment M5 goes stale).
        rising_edge = (not was_enabled) and self._autopilot_enabled

        if was_enabled != self._autopilot_enabled:
            status = "ENABLED" if self._autopilot_enabled else "DISABLED"
            self.get_logger().info(
                f"[autopilot] {status} (env={env_state}, "
                f"allowed={env_allows_autopilot}, "
                f"stale={staleness_s:.1f}s, m4_fallback={m4_in_fallback})")

        if self._autopilot_enabled:
            should_publish = (
                rising_edge or
                self._last_actuator_publish_time is None or
                (now - self._last_actuator_publish_time) > 0.5)
            if should_publish:
                stamp = self.get_clock().now().to_msg()
                out = self._compute_transit_autopilot(stamp)
                self._pub_act.publish(out)
                self._last_actuator_publish_time = now

    def _compute_avoidance_autopilot(self, stamp) -> SilOwnShipState:
        out = self._make_actuator_msg(stamp)

        if self._last_ownship_raw is None:
            out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN
            return out

        current_heading_deg = math.degrees(self._last_ownship_raw.heading) % 360.0
        current_rot_deg_s = math.degrees(self._last_ownship_raw.rot)

        # ── LATCH offset decay logic ─────────────────────────
        if self._latch_release_triggered and self._latch_release_time is not None and self._avoidance_target_heading_deg is not None:
            t_now = self._get_sim_time()
            latch_offset_decaying = self._compute_latch_offset(
                self._latch_release_time, t_now, 
                self._latch_offset_at_release_deg or 0.0)
            
            diff = (self._avoidance_target_heading_deg - self._target_heading_deg + 180.0) % 360.0 - 180.0
            sign = 1.0 if diff >= 0.0 else -1.0
            
            if latch_offset_decaying <= 0.01:  # fully decayed
                self._latch_release_triggered = False
                self._avoidance_target_heading_deg = self._target_heading_deg
                self.get_logger().info("[BRIDGE] LATCH decay complete, snapped to nominal route bearing")
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()
            else:
                self._avoidance_target_heading_deg = (self._target_heading_deg + sign * latch_offset_decaying) % 360.0

        if self._avoidance_target_heading_deg is not None:
            heading_error_deg = (
                self._avoidance_target_heading_deg - current_heading_deg + 180.0
            ) % 360.0 - 180.0
            dt = 0.5
            out.rudder_angle = RUDDER_SIGN * self._avoidance_heading_controller.step(
                heading_error_deg, dt, current_rot_deg_s)
            if abs(heading_error_deg) > 5.0 or abs(current_rot_deg_s) > 2.0:
                print(f"[BRIDGE-AVOID] hdg={current_heading_deg:.1f} "
                      f"tgt={self._avoidance_target_heading_deg:.1f} "
                      f"err={heading_error_deg:.1f} rot={current_rot_deg_s:.2f} "
                      f"rud={math.degrees(out.rudder_angle):.1f}", flush=True)
        elif self._last_avoidance_waypoint is not None:
            wp = self._last_avoidance_waypoint
            if abs(wp.turn_radius_m) > 1e-6:
                radius = max(abs(wp.turn_radius_m), 50.0)
                rudder_rad = math.atan2(SHIP_LENGTH_M, radius)
                out.rudder_angle = RUDDER_SIGN * max(-MAX_RUDDER_RAD,
                                       min(MAX_RUDDER_RAD, rudder_rad))
            else:
                out.rudder_angle = 0.0
        else:
            out.rudder_angle = 0.0

        if self._last_avoidance_waypoint is not None:
            out.throttle = max(0.0, min(1.0,
                self._last_avoidance_waypoint.target_speed_kn / MAX_SPEED_KN))
        else:
            out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN

        return out

    def _on_planned_route(self, msg: PlannedRoute) -> None:
        """Cache planned-route waypoints (lat, lon) for geometric cross-track error."""
        try:
            self._route_wps = [
                (float(p.pose.position.latitude), float(p.pose.position.longitude))
                for p in msg.route.poses
            ]
        except Exception:
            pass

    def _signed_xte_m(self, own_lat, own_lon):
        """Signed cross-track distance (m) of own-ship from the planned-route line.

        +ve = own-ship to port (left) of route direction (start->end); -ve = starboard.
        Returns None if no route cached. General for any route orientation
        (no scenario constants -- ADR-4).
        """
        if len(self._route_wps) < 2:
            return None
        m_per_deg_lat = 111132.9
        m_per_deg_lon = 111319.9 * math.cos(math.radians(self._route_wps[0][0]))
        ax = self._route_wps[0][1] * m_per_deg_lon
        ay = self._route_wps[0][0] * m_per_deg_lat
        bx = self._route_wps[-1][1] * m_per_deg_lon
        by = self._route_wps[-1][0] * m_per_deg_lat
        px = own_lon * m_per_deg_lon
        py = own_lat * m_per_deg_lat
        dx, dy = bx - ax, by - ay
        seg = math.hypot(dx, dy)
        if seg < 1.0:
            return None
        return (dx * (py - ay) - dy * (px - ax)) / seg

    def _compute_transit_autopilot(self, stamp) -> SilOwnShipState:
        out = self._make_actuator_msg(stamp)

        if self._last_ownship_raw is not None:
            current_heading_deg = math.degrees(self._last_ownship_raw.heading)
            current_sog_kn = self._last_ownship_raw.sog * 1.94384
            current_rot_deg_s = math.degrees(self._last_ownship_raw.rot)
            own_lat = self._last_ownship_raw.lat
            own_lon = self._last_ownship_raw.lon
        else:
            current_heading_deg = self._target_heading_deg
            current_sog_kn = self._target_sog_kn
            current_rot_deg_s = 0.0
            own_lat = 0.0
            own_lon = 0.0

        target_sog = self._target_sog_kn
        if (abs(self._current_target_wp_lat) > 1e-4 or
                abs(self._current_target_wp_lon) > 1e-4):
            effective_target_heading = self._great_circle_bearing(
                own_lat, own_lon,
                self._current_target_wp_lat, self._current_target_wp_lon)
            
            # Geometric cross-track from the actual planned route (no scenario
            # constants -- ADR-4). _signed_xte_m: +ve = port of route direction.
            xte_m = self._signed_xte_m(own_lat, own_lon)
            if xte_m is None:
                xte_m = 0.0

            # Proportional gain: 0.3 deg/meter (faster recovery)
            # Clamp to [-85.0, 85.0] degrees to allow a steeper intercept angle for rapid return
            xte_correction = max(-30.0, min(30.0, xte_m * 0.10))
            effective_target_heading = (effective_target_heading + xte_correction) % 360.0
            
            # Boost target speed when far off-track to overcome rudder drag and close XTE rapidly
            if abs(xte_m) > 150.0:
                target_sog = max(target_sog, 19.5)
            elif abs(xte_m) > 50.0:
                target_sog = max(target_sog, 18.0)
        else:
            effective_target_heading = self._target_heading_deg

        heading_error_deg = effective_target_heading - current_heading_deg
        heading_error_deg = (heading_error_deg + 180.0) % 360.0 - 180.0
        speed_error_kn = target_sog - current_sog_kn

        dt = 0.5
        out.rudder_angle = RUDDER_SIGN * self._heading_controller.step(
            heading_error_deg, dt, current_rot_deg_s)
        out.throttle = self._speed_controller.step(speed_error_kn, dt)

        return out

    @staticmethod
    def _great_circle_bearing(lat1: float, lon1: float,
                              lat2: float, lon2: float) -> float:
        """Great-circle bearing [deg] from (lat1,lon1) to (lat2,lon2)."""
        dlon = math.radians(lon2 - lon1)
        y = math.sin(dlon) * math.cos(math.radians(lat2))
        x = (math.cos(math.radians(lat1)) * math.sin(math.radians(lat2))
             - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2))
             * math.cos(dlon))
        return math.degrees(math.atan2(y, x)) % 360.0


# ── Main ─────────────────────────────────────────────────────

def main() -> None:
    rclpy.init()
    node = SilTopicBridge()
    executor = MultiThreadedExecutor(num_threads=2)
    executor.add_node(node)

    # Graceful shutdown on SIGINT
    shutdown_event = threading.Event()

    def _sigint_handler(sig, frame):
        shutdown_event.set()

    signal.signal(signal.SIGINT, _sigint_handler)

    try:
        while rclpy.ok() and not shutdown_event.is_set():
            executor.spin_once(timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.get_logger().info("[sil_topic_bridge] Shutting down")
        executor.shutdown()
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
