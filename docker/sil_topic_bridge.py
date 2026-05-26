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
)

# ── Cross-layer L3 external message types ────────────────────
from l3_external_msgs.msg import (
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
    def __init__(self, Kp: float = 10.0, max_rate_deg_s: float = 5.0):
        self.Kp = Kp
        self.max_rate_deg_s = max_rate_deg_s
        self.last_cmd_deg = 0.0

    def step(self, error_deg: float, dt: float) -> float:
        error_deg = (error_deg + 180.0) % 360.0 - 180.0
        cmd_deg = self.Kp * error_deg
        max_delta = self.max_rate_deg_s * dt
        cmd_deg = max(self.last_cmd_deg - max_delta,
                      min(self.last_cmd_deg + max_delta, cmd_deg))
        cmd_deg = max(-35.0, min(35.0, cmd_deg))
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
        self._last_odd_state = None
        self._last_behavior_plan = None
        self._last_ownship_raw = None
        self._last_actuator_publish_time = None
        self._heading_controller = HeadingController()
        self._speed_controller = SpeedController()

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
            lambda msg: self._record_pulse(M3), sq)
        self._sub_m4 = self.create_subscription(
            BehaviorPlan, "/l3/m4/behavior_plan",
            self._on_behavior_plan, sq)
        self._sub_m6 = self.create_subscription(
            COLREGsConstraint, "/l3/m6/colregs_constraint",
            lambda msg: self._record_pulse(M6), sq)
        self._sub_m7_heartbeat = self.create_subscription(
            Header, "/l3/m7/heartbeat",
            lambda msg: self._record_pulse(M7), sq)

        # M5 and M8 pulse recorded inside _on_avoidance_plan / _on_ui_state

        self._pub_pulse = self.create_publisher(
            ModulePulse, "/sil/module_pulse", sq)
        self._pulse_timer = self.create_timer(1.0, self._publish_module_pulse)

        self._autopilot_timer = self.create_timer(0.5, self._autopilot_step)

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
        tgt.classification = "unknown"
        tgt.classification_confidence = 0.0
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

    # ── L3→SIL callbacks ────────────────────────────────────

    def _on_avoidance_plan(self, msg: AvoidancePlan) -> None:
        self._record_pulse(M5)

        has_valid_plan = (
            len(msg.waypoints) > 0 and
            abs(msg.waypoints[0].turn_radius_m) > 1e-6
        )
        if has_valid_plan:
            self._last_valid_plan_time = time.monotonic()

        out = SilOwnShipState()
        out.stamp = msg.stamp

        if self._autopilot_enabled and not has_valid_plan:
            out = self._compute_transit_autopilot(msg.stamp)
        else:
            if msg.waypoints and len(msg.waypoints) > 0:
                wp = msg.waypoints[0]
                if abs(wp.turn_radius_m) > 1e-6:
                    radius = max(abs(wp.turn_radius_m), 50.0)
                    rudder_rad = math.atan2(SHIP_LENGTH_M, radius)
                    out.rudder_angle = max(-MAX_RUDDER_RAD, min(MAX_RUDDER_RAD, rudder_rad))
                else:
                    out.rudder_angle = 0.0
                out.throttle = max(0.0, min(1.0, wp.target_speed_kn / MAX_SPEED_KN))
            else:
                out.rudder_angle = 0.0
                out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN

        out.lat = 0.0
        out.lon = 0.0
        out.heading = 0.0
        out.sog = 0.0
        out.cog = 0.0
        out.rot = 0.0
        out.u = 0.0
        out.v = 0.0
        out.r = 0.0
        self._pub_act.publish(out)
        self._last_actuator_publish_time = time.monotonic()

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
        if self._last_odd_state is None:
            return

        now = time.monotonic()
        staleness_s = (
            (now - self._last_valid_plan_time)
            if self._last_valid_plan_time else float('inf'))

        is_in_envelope = (
            self._last_odd_state.envelope_state == ODDState.ENVELOPE_IN)
        is_m5_stale = staleness_s > 10.0
        m4_in_fallback = (
            self._last_behavior_plan is not None and
            "fallback" in self._last_behavior_plan.rationale.lower())

        was_enabled = self._autopilot_enabled
        self._autopilot_enabled = is_in_envelope and (is_m5_stale or m4_in_fallback)

        if was_enabled != self._autopilot_enabled:
            status = "ENABLED" if self._autopilot_enabled else "DISABLED"
            self.get_logger().info(
                f"[autopilot] {status} (in_envelope={is_in_envelope}, "
                f"stale={staleness_s:.1f}s, m4_fallback={m4_in_fallback})")

        if self._autopilot_enabled:
            if (self._last_actuator_publish_time is None or
                    (now - self._last_actuator_publish_time) > 1.5):
                stamp = self.get_clock().now().to_msg()
                out = self._compute_transit_autopilot(stamp)
                self._pub_act.publish(out)
                self._last_actuator_publish_time = now

    def _compute_transit_autopilot(self, stamp) -> SilOwnShipState:
        out = SilOwnShipState()
        out.stamp = stamp

        if self._last_ownship_raw is not None:
            current_heading_deg = math.degrees(self._last_ownship_raw.heading)
            current_sog_kn = self._last_ownship_raw.sog * 1.94384
        else:
            current_heading_deg = self._target_heading_deg
            current_sog_kn = self._target_sog_kn

        heading_error_deg = self._target_heading_deg - current_heading_deg
        speed_error_kn = self._target_sog_kn - current_sog_kn

        dt = 0.5
        out.rudder_angle = self._heading_controller.step(heading_error_deg, dt)
        out.throttle = self._speed_controller.step(speed_error_kn, dt)

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
