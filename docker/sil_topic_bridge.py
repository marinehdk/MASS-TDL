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

import collections
import gzip
import json
import math
import os
import shutil
import signal
import threading
import time
from pathlib import Path
from typing import Optional

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
    LifecycleStatus,
    ScoringRow,
)

# ── Cross-layer L3 external message types ────────────────────
from l3_external_msgs.msg import (
    PlannedRoute,
    FilteredOwnShipState,
    TrackedTargetArray,
    EnvironmentState as L3EnvironmentState,
    CheckerVetoNotification,
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
# Transitional Bridge cap for rolling M4 windows. Boundary probes need >135 deg
# separation until L4 owns heading tracking and route-return authority.
M4_AVOID_TARGET_LOCK_DELTA_DEG = 150.0
M5_AVOID_WAYPOINT_MAX_DELTA_DEG = 170.0
M5_AVOID_WAYPOINT_MIN_LOOKAHEAD_M = 25.0
M5_AVOID_WAYPOINT_TARGET_TOLERANCE_DEG = 10.0

# ── Geometry release threshold ─────────────────────────────────────────────
# Bridge-local CPA safety margin for geometry release criterion.
# Traced from: src/l3_tdl_kernel/m6_colregs_reasoner/config/odd_aware_thresholds.yaml
# `cpa_safe_m: 1000.0  # [TBD-HAZID] calibrated baseline for integration tests`
# [TBD-HAZID][cross-module] M2 ThreatState should publish numeric cpa_m/tcpa_s
# so the release threshold is sourced from a single authoritative module, not
# duplicated here. Until M2 exports these fields, this bridge-local constant is
# the interim bridge implementation.
CPA_SAFE_M: float = 1000.0  # metres — same as M6 integration-test baseline


def _signed_heading_delta_deg(heading_deg: float, reference_deg: float) -> float:
    return (float(heading_deg) - float(reference_deg) + 180.0) % 360.0 - 180.0


def _m4_colregs_window_target_deg(
        heading_min_deg: float,
        heading_max_deg: float,
        nominal_heading_deg: float) -> Optional[float]:
    h_min = float(heading_min_deg)
    h_max = float(heading_max_deg)
    if h_max < h_min:
        h_max += 360.0
    h_span = h_max - h_min
    if h_span > 300.0:
        return None

    reversal_boundary = float(nominal_heading_deg) + 180.0
    while reversal_boundary < h_min:
        reversal_boundary += 360.0
    while reversal_boundary > h_max:
        reversal_boundary -= 360.0
    if h_min < reversal_boundary < h_max:
        return h_min % 360.0

    return (h_min + (5.0 / 6.0) * h_span) % 360.0


def _should_refresh_m4_colregs_target(
        current_target_deg: Optional[float],
        nominal_heading_deg: float,
        candidate_target_deg: Optional[float] = None) -> bool:
    if current_target_deg is None:
        return True
    current_signed_delta = _signed_heading_delta_deg(
        current_target_deg, nominal_heading_deg)
    current_delta = abs(current_signed_delta)
    if candidate_target_deg is not None:
        candidate_signed_delta = _signed_heading_delta_deg(
            candidate_target_deg, nominal_heading_deg)
        candidate_delta = abs(candidate_signed_delta)
        same_side = current_signed_delta * candidate_signed_delta >= 0.0
        if same_side and candidate_delta < current_delta:
            return True
        # Recover from a stale >180 deg lock when M4 has moved back to a
        # starboard-side rejoin window; otherwise the rudder stays near zero.
        if (current_delta >= M4_AVOID_TARGET_LOCK_DELTA_DEG and
                current_signed_delta < 0.0 and
                candidate_signed_delta > 0.0 and
                candidate_delta < M4_AVOID_TARGET_LOCK_DELTA_DEG):
            return True
    return current_delta < M4_AVOID_TARGET_LOCK_DELTA_DEG

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


# ── Debug trace writer ────────────────────────────────────

class DebugTraceWriter:
    """Ring-buffer JSONL writer for key L3 interface topics.

    Appends to /var/sil/runs/trace_current.jsonl (shared volume).
    Thread-safe; flushes every 2s. Call reset() on scenario ACTIVE to truncate.
    """

    FLUSH_INTERVAL_S = 2.0
    MAX_BUF = 2000

    def __init__(self, node: "SilTopicBridge") -> None:
        self._node = node
        self._lock = threading.Lock()
        self._buf: collections.deque = collections.deque(maxlen=self.MAX_BUF)
        self._file = None
        self._flush_timer: threading.Timer | None = None
        run_dir = Path(os.environ.get("SIL_RUN_DIR", "/var/sil/runs"))
        self._trace_path = run_dir / "trace_current.jsonl"
        self.reset()

    def reset(self) -> None:
        """Truncate trace file and restart flush timer. Call on scenario ACTIVE."""
        with self._lock:
            if self._file is not None:
                try:
                    self._file.close()
                except Exception:
                    pass
            self._buf.clear()
            try:
                self._trace_path.parent.mkdir(parents=True, exist_ok=True)
                self._file = open(self._trace_path, "w")
            except Exception as exc:
                self._node.get_logger().error(
                    f"[DebugTraceWriter] cannot open {self._trace_path}: {exc}")
                self._file = None
        self._schedule_flush()

    def record(self, topic: str, data: dict, sim_t: float) -> None:
        """Append one record to in-memory ring buffer."""
        entry = {
            "sim_t": round(sim_t, 3),
            "wall_t": round(time.time(), 3),
            "topic": topic
        }
        entry.update(data)
        with self._lock:
            self._buf.append(json.dumps(entry, default=str))

    def _schedule_flush(self) -> None:
        if self._flush_timer is not None:
            self._flush_timer.cancel()
        t = threading.Timer(self.FLUSH_INTERVAL_S, self._flush)
        t.daemon = True
        t.start()
        self._flush_timer = t

    def _flush(self) -> None:
        with self._lock:
            if self._file and self._buf:
                try:
                    lines = list(self._buf)
                    self._buf.clear()
                    self._file.write("\n".join(lines) + "\n")
                    self._file.flush()
                except Exception as exc:
                    self._node.get_logger().warning(
                        f"[DebugTraceWriter] flush error: {exc}")
            
            # File Rotation & Compression if size exceeds 50MB
            if self._file:
                try:
                    if self._trace_path.exists() and self._trace_path.stat().st_size > 50 * 1024 * 1024:
                        self._node.get_logger().info(
                            f"[DebugTraceWriter] Trace file size exceeded 50MB. Rotating...")
                        self._file.close()
                        self._file = None
                        
                        # Compress using gzip and shutil
                        rotated_path = self._trace_path.parent / f"trace_{int(time.time())}.jsonl.gz"
                        with open(self._trace_path, "rb") as f_in:
                            with gzip.open(rotated_path, "wb") as f_out:
                                shutil.copyfileobj(f_in, f_out)
                        
                        self._node.get_logger().info(
                            f"[DebugTraceWriter] Rotated trace to {rotated_path}")
                        
                        # Truncate/re-open the main trace file
                        self._file = open(self._trace_path, "w")
                except Exception as exc:
                    self._node.get_logger().error(
                        f"[DebugTraceWriter] Failed to rotate trace file: {exc}")
                    if self._file is None:
                        try:
                            self._file = open(self._trace_path, "a")
                        except Exception:
                            pass
        self._schedule_flush()

    def close(self) -> None:
        if self._flush_timer:
            self._flush_timer.cancel()
        with self._lock:
            if self._file:
                try:
                    if self._buf:
                        self._file.write("\n".join(self._buf) + "\n")
                    self._file.flush()
                    self._file.close()
                except Exception:
                    pass
                self._file = None


# ── Bridge node ──────────────────────────────────────────────

class SilTopicBridge(Node):
    """Bidirectional topic bridge between SIL and L3 kernel namespaces."""

    def __init__(self) -> None:
        super().__init__("sil_topic_bridge")
        self.get_logger().info("[sil_topic_bridge] Bridge active")
        release_fallback_raw = os.environ.get(
            "SIL_BRIDGE_RELEASE_FALLBACK", "0").strip().lower()
        self._bridge_release_fallback_enabled = (
            release_fallback_raw not in {"0", "false", "off", "no"}
        )
        l4_adapter_raw = os.environ.get(
            "SIL_L4_ADAPTER_ENABLE", "0").strip().lower()
        self._l4_adapter_enabled = (
            l4_adapter_raw in {"1", "true", "on", "yes"}
        )
        if self._bridge_release_fallback_enabled:
            self.get_logger().warn(
                "[BRIDGE] SIL_BRIDGE_RELEASE_FALLBACK=1: legacy latch-release "
                "compatibility path enabled until M4/L4 handback owns route return")
        if self._l4_adapter_enabled:
            self.get_logger().warn(
                "[BRIDGE] SIL_L4_ADAPTER_ENABLE=1: bridge actuator publisher "
                "disabled; L4 guidance adapter owns /sil/actuator_cmd")

        sq = _sensor_qos()
        lq = _latched_qos()
        rq = _reliable_volatile_qos()

        # ── Autopilot state ─────────────────────────────────
        self._autopilot_enabled = False
        self._last_valid_plan_time = None
        self._last_avoidance_waypoint = None
        self._last_avoidance_waypoints = []
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
        self._lifecycle_state = None
        self._last_sim_time = None

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
        self._pub_act = None
        if not self._l4_adapter_enabled:
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
            self._on_colregs_constraint, sq)
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
        # D-DEMO1 spin fix: sim-time at which M4 last entered (and has continuously
        # held) BEHAVIOR_TRANSIT while avoidance is armed. Once M4 holds TRANSIT for
        # _AVOID_TRANSIT_RELEASE_S, the bridge tears down avoidance (M4 is the COLREG
        # authority), independent of M5 still emitting a stub geometric plan.
        self._transit_since_time: float | None = None
        self._AVOID_TRANSIT_RELEASE_S: float = 3.0
        # Guard: do not arm avoidance before M3 has reached FSM_ACTIVE (≥3) at
        # least once in this scenario.  Prevents M5 cold-start plans from
        # arming the bridge before the scenario state is fully established.
        self._m3_activated_once: bool = False

        # ── M6 conflict authority (ADR-1) ────────────────────────────────────
        # M6 COLREGsConstraint.conflict_detected is the hard gate for avoidance
        # arm/release. The bridge must not release while M6 still reports an
        # active conflict.
        #
        # Root cause of ot toggles=126 (fbe100c4): _check_geometry_release() was
        # firing every ~6s while M6 held conflict stable, because own-ship
        # avoidance maneuver pushed the target past CPA (TCPA<0 && DCPA≥1000m).
        # Bridge was overriding M6 authority in violation of ADR-1.
        #
        # All 3 bridge release paths (geometry / threat / mission) are gated on
        # _m6_conflict_active=False. Their latch mutation is a temporary
        # compatibility fallback controlled by SIL_BRIDGE_RELEASE_FALLBACK.
        self._m6_conflict_active: bool = False
        self._m6_conflict_last_t: float | None = None
        self._m6_primary_role: int | None = None
        self._m6_phase: str = ""

        # ── Last target vessel state (for bridge-local DCPA/TCPA) ─────────────
        self._last_target_vessel_raw = None

        # ── Bridge state publisher ────────────────────────────
        self._pub_bridge_state = self.create_publisher(
            BridgeState, "/sil/bridge_state", sq)

        # M5 and M8 pulse recorded inside _on_avoidance_plan / _on_ui_state

        self._pub_pulse = self.create_publisher(
            ModulePulse, "/sil/module_pulse", sq)
        self._pulse_timer = self.create_timer(1.0, self._publish_module_pulse)

        self._autopilot_timer = self.create_timer(0.5, self._autopilot_step)

        # ── Lifecycle Status subscription for state cleanup ──
        self._sub_lifecycle = self.create_subscription(
            LifecycleStatus, "/sil/lifecycle_status",
            self._on_lifecycle_status, 10)

        # ── SIL Scoring subscription ──────────────────────────
        self._sub_scoring = self.create_subscription(
            ScoringRow, "/sil/scoring",
            self._on_scoring, sq)

        # ── L3 FSM State subscription ─────────────────────────
        self._sub_fsm_state = self.create_subscription(
            LifecycleStatus, "/l3/fsm_state",
            self._on_fsm_state, sq)

        # ── Debug trace writer ────────────────────────────────
        self._trace_writer = DebugTraceWriter(node=self)

        # ── Checker veto subscription (debug trace) ───────────
        self._sub_veto = self.create_subscription(
            CheckerVetoNotification, "/l3/checker/veto",
            self._on_checker_veto, rq)

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

    def _reset_autopilot_avoidance_state(self) -> None:
        """Reset autopilot and avoidance states to prevent leakage across cycles."""
        self._autopilot_enabled = False
        self._avoidance_active = False
        self._avoidance_target_heading_deg = None
        self._last_avoidance_waypoint = None
        self._last_avoidance_waypoints = []
        self._avoidance_heading_controller.last_cmd_deg = 0.0
        self._heading_controller.last_cmd_deg = 0.0
        self._speed_controller = SpeedController()
        self._reset_latch_release_state()
        self._route_wps = []
        self._current_target_wp_lat = 0.0
        self._current_target_wp_lon = 0.0
        self._last_ownship_raw = None
        self._last_odd_state = None
        self._last_behavior_plan = None
        self._last_valid_plan_time = None
        self._last_actuator_publish_time = None
        self._m3_activated_once = False
        self._last_sim_time = None
        self._avoidance_armed_time = None
        self._last_target_vessel_raw = None
        self._m6_conflict_active = False
        self._m6_conflict_last_t = None
        self._m6_primary_role = None
        self._m6_phase = ""

    def _on_lifecycle_status(self, msg: LifecycleStatus) -> None:
        """Reset state if simulation is not ACTIVE."""
        # 3 is ACTIVE
        prev_state = self._lifecycle_state
        self._lifecycle_state = msg.current_state

        if msg.current_state != 3:
            if prev_state == 3:
                self._trace_writer.close()
            if self._autopilot_enabled or self._avoidance_active:
                self.get_logger().info(
                    f"[BRIDGE] Simulation state is {msg.current_state} (not ACTIVE). "
                    "Resetting autopilot and avoidance states to prevent leakage."
                )
            self._reset_autopilot_avoidance_state()
        else:
            # ACTIVE state: dynamically read initial parameters to ensure we use the new scenario values
            try:
                init_heading = self.get_parameter("ownship_initial_heading_deg").value
                init_sog = self.get_parameter("ownship_initial_sog_kn").value
                self._target_heading_deg = init_heading
                self._target_sog_kn = init_sog
                if prev_state != 3:
                    self._trace_writer.reset()
                    self.get_logger().info(
                        f"[BRIDGE] Simulation active. Parameters: heading={self._target_heading_deg}°, SOG={self._target_sog_kn} kn"
                    )
            except Exception as exc:
                self.get_logger().warn(f"Failed to read updated parameters from server: {exc}")

    def _on_checker_veto(self, msg: CheckerVetoNotification) -> None:
        self._trace_writer.record("/l3/checker/veto", {
            "checker_layer": str(msg.checker_layer),
            "vetoed_module": str(msg.vetoed_module),
            "veto_reason_class": int(msg.veto_reason_class),
            "veto_reason_detail": str(msg.veto_reason_detail),
            "fallback_provided": bool(msg.fallback_provided),
            "confidence": float(msg.confidence),
        }, self._get_sim_time())

    def _on_scoring(self, msg: ScoringRow) -> None:
        self._trace_writer.record("/sil/scoring", {
            "safety": float(msg.safety),
            "rule_compliance": float(msg.rule_compliance),
            "delay": float(msg.delay),
            "magnitude": float(msg.magnitude),
            "phase": float(msg.phase),
            "plausibility": float(msg.plausibility),
            "total": float(msg.total),
        }, self._get_sim_time())

    def _on_fsm_state(self, msg: LifecycleStatus) -> None:
        self._trace_writer.record("/l3/fsm_state", {
            "state": int(msg.current_state),
        }, self._get_sim_time())

    def _on_colregs_constraint(self, msg: COLREGsConstraint) -> None:
        """Record M6 health pulse AND update conflict authority state (ADR-1).

        _m6_conflict_active mirrors M6 avoidance authority for trace and arm guard.
        The bridge defers to M6/M4 as the hard conflict gate. Legacy geometry /
        threat / mission release remains a temporary compatibility fallback while
        M4/L4 handback is not yet proven in clean 8-probe.

        primary_role enum: 0=STAND_ON 1=GIVE_WAY 2=BOTH_GIVE_WAY 3=FREE.
        """
        self._record_pulse(M6)
        t_now = self._get_sim_time()
        self._m6_conflict_active = bool(msg.conflict_detected)
        self._m6_conflict_last_t = t_now
        self._m6_primary_role = int(msg.primary_role)
        self._m6_phase = str(msg.phase)
        # [ADR-1] ARM avoidance when M6 detects conflict + M4 is in AVOID mode.
        if self._m6_conflict_active and not self._avoidance_active:
            self._arm_avoidance_from_m6()
        self._trace_writer.record("/l3/m6/colregs_constraint", {
            "conflict_detected": bool(msg.conflict_detected),
            "primary_role": int(msg.primary_role),
            "phase": str(msg.phase),
            "primary_preferred_direction": str(msg.primary_preferred_direction),
            "confidence": float(msg.confidence),
        }, t_now)

    def _on_odd_state(self, msg: ODDState) -> None:
        self._record_pulse(M1)
        self._last_odd_state = msg

    def _on_behavior_plan(self, msg: BehaviorPlan) -> None:
        self._record_pulse(M4)
        logger = getattr(self, 'get_logger', None)
        if logger is not None:
            logger().info(
                f"[BRIDGE-DIAG] Received behavior plan: behavior={msg.behavior}, "
                f"window=[{msg.heading_min_deg:.1f}, {msg.heading_max_deg:.1f}]"
            )
        self._last_behavior_plan = msg
        # Defensive avoidance teardown (D-DEMO1 spin fix): M4 is the COLREG
        # authority on whether avoidance is active. If M4 holds TRANSIT for
        # _AVOID_TRANSIT_RELEASE_S (after the minimum hold), end avoidance and hand
        # back to the transit autopilot for route-return — even if M5's stub keeps
        # emitting a geometric plan (has_valid_plan stays True forever). Without
        # this the bridge can stay armed with target_heading=None and drive the
        # open-loop turn_radius rudder indefinitely → endless circling, no return.
        if self._avoidance_active:
            if msg.behavior == 0:  # BEHAVIOR_TRANSIT
                if self._latch_release_triggered:
                    self._transit_since_time = None
                else:
                    now = self._get_sim_time()
                    if self._transit_since_time is None:
                        self._transit_since_time = now
                    if (self._latch_hold_elapsed() and
                            (now - self._transit_since_time) >= self._AVOID_TRANSIT_RELEASE_S):
                        if logger is not None:
                            logger().info(
                                "[BRIDGE] Avoidance teardown — M4 held TRANSIT for "
                                f"{now - self._transit_since_time:.1f}s; returning to route"
                            )
                        self._avoidance_active = False
                        self._avoidance_target_heading_deg = None
                        self._avoidance_heading_controller.last_cmd_deg = 0.0
                        self._reset_latch_release_state()
                        self._transit_since_time = None
            else:
                self._transit_since_time = None
        # G1 (continuous target tracking — D-DEMO1 under-turn fix): while M4
        # commands COLREG_AVOID, refresh the avoidance target every behavior_plan
        # message instead of latching it once.  The previous one-shot latch
        # (condition `_avoidance_target_heading_deg is None`) froze the target on
        # whatever transient/early M4 window arm happened to catch — e.g. a
        # near-zero [0,4] window → 3.3°, so the ship never tracked the window as it
        # grew to the correct starboard sector [16,73].  IvP/MPC COLAV track the
        # give-way heading continuously; the rudder rate limit
        # (_avoidance_heading_controller, 10°/s) provides the smoothing.  Do not
        # refresh while a release is decaying (let the decay run) or once M4 has
        # returned to TRANSIT (behavior == 0).
        if (self._avoidance_active and
                msg.behavior != 0 and
                not self._latch_release_triggered):
            nominal_heading = getattr(self, '_target_heading_deg', 0.0)
            candidate = _m4_colregs_window_target_deg(
                msg.heading_min_deg, msg.heading_max_deg, nominal_heading)
            should_refresh = (
                candidate is not None and
                _should_refresh_m4_colregs_target(
                    self._avoidance_target_heading_deg, nominal_heading, candidate)
            )
            if (should_refresh and
                    getattr(self, "_m6_conflict_active", False) and
                    self._avoidance_target_heading_deg is not None):
                current_delta = _signed_heading_delta_deg(
                    self._avoidance_target_heading_deg, nominal_heading)
                candidate_delta = _signed_heading_delta_deg(candidate, nominal_heading)
                same_side = current_delta * candidate_delta > 0.0
                if same_side and abs(candidate_delta) < abs(current_delta):
                    should_refresh = False
            if should_refresh:
                self._avoidance_target_heading_deg = candidate
            # else: degenerate (≈full circle) window — keep the last good target
            # (or None → _compute_avoidance_autopilot falls back to M5 waypoint
            # rudder); do not overwrite with a nonsensical heading.
        self._trace_writer.record("/l3/m4/behavior_plan", {
            "behavior": int(msg.behavior),
            "heading_min_deg": float(msg.heading_min_deg),
            "heading_max_deg": float(msg.heading_max_deg),
            "avoidance_active": self._avoidance_active,
            "target_heading_deg": self._avoidance_target_heading_deg,
        }, self._get_sim_time())

    # ── SIL→L3 callbacks ────────────────────────────────────

    def _on_own_ship_state(self, msg: SilOwnShipState) -> None:
        self._record_pulse(M2)

        # Reset timers if simulation clock jumps backward (scenario restart/cleanup)
        t_now = self._get_sim_time()
        last_sim_time = getattr(self, '_last_sim_time', None)
        if last_sim_time is not None and t_now < last_sim_time - 1.0:
            self.get_logger().info(
                f"[BRIDGE] Clock jump backward detected: {last_sim_time:.2f}s -> {t_now:.2f}s. Resetting autopilot, pulse timers, and state variables."
            )
            self._autopilot_timer.reset()
            self._pulse_timer.reset()
            self._reset_autopilot_avoidance_state()
        self._last_sim_time = t_now

        self._last_ownship_raw = msg
        self._trace_writer.record("/sil/own_ship_state", {
            "heading_deg": round(math.degrees(msg.heading), 2),
            "sog_kn": round(msg.sog * 1.94384, 2),
            "lat": msg.lat,
            "lon": msg.lon,
            "rot_deg_s": round(math.degrees(msg.rot), 3),
        }, self._get_sim_time())
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
        # Cache for bridge-local DCPA/TCPA computation (G3 geometry release)
        self._last_target_vessel_raw = msg
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

    @staticmethod
    def _compute_dcpa_tcpa(own, target) -> tuple[float, float]:
        """Bridge-local DCPA/TCPA from OwnShipState + TargetVesselState kinematics.

        Uses flat-earth approximation (valid for ranges ≤ ~20 nm).
        Returns (dcpa_m, tcpa_s). If vessels are stationary relative to each other,
        returns (very large, 0.0) to prevent spurious releases.

        Kinematic model:
          r = position of target relative to own (m, East-North)
          v = velocity of target relative to own (m/s, East-North)
          TCPA = -(r·v) / |v|²   [s]
          DCPA = |r + v·TCPA|    [m]   (only meaningful when TCPA ≥ 0)

        [TBD-HAZID][cross-module] M2 ThreatState should publish numeric cpa_m/tcpa_s;
        this bridge-local computation is an interim measure until M2 is updated.
        """
        import math
        M_PER_DEG_LAT = 111_132.9
        cos_lat = math.cos(math.radians(own.lat))
        m_per_deg_lon = 111_319.9 * cos_lat

        # Relative position: target minus own (East, North) in metres
        r_east = (target.lon - own.lon) * m_per_deg_lon
        r_north = (target.lat - own.lat) * M_PER_DEG_LAT

        # Absolute velocities in (East, North) m/s using COG
        own_cog = float(own.cog)
        tgt_cog = float(target.cog)
        own_sog = float(own.sog)
        tgt_sog = float(target.sog)

        own_ve = own_sog * math.sin(own_cog)
        own_vn = own_sog * math.cos(own_cog)
        tgt_ve = tgt_sog * math.sin(tgt_cog)
        tgt_vn = tgt_sog * math.cos(tgt_cog)

        # Relative velocity: target minus own
        v_east = tgt_ve - own_ve
        v_north = tgt_vn - own_vn

        v_sq = v_east ** 2 + v_north ** 2
        if v_sq < 1e-6:
            # Nearly identical velocities — no CPA computable
            return (math.hypot(r_east, r_north), 0.0)

        tcpa_s = -(r_east * v_east + r_north * v_north) / v_sq

        # DCPA: distance at TCPA moment (if TCPA < 0, use current distance as proxy)
        if tcpa_s >= 0:
            cpa_east = r_east + v_east * tcpa_s
            cpa_north = r_north + v_north * tcpa_s
        else:
            cpa_east = r_east
            cpa_north = r_north
        dcpa_m = math.hypot(cpa_east, cpa_north)

        return (dcpa_m, tcpa_s)

    def _check_geometry_release(self) -> None:
        """Detect old geometry release candidates.

        Called every autopilot step (2 Hz) while _avoidance_active and before
        any legacy release smoothing is active. The minimum-hold debounce keeps
        cold-start ambiguous geometry out of the trace. M6 remains the hard gate;
        latch mutation is a removable compatibility fallback.
        """
        if not self._avoidance_active:
            return
        if self._latch_release_triggered:
            return
        if not self._latch_hold_elapsed():
            return
        # [ADR-1] Defer to M6/M4 authority. While M6 still sees a conflict,
        # suppress even the trace candidate.
        if self._m6_conflict_active:
            return
        if self._last_ownship_raw is None or self._last_target_vessel_raw is None:
            return

        dcpa_m, tcpa_s = SilTopicBridge._compute_dcpa_tcpa(
            self._last_ownship_raw, self._last_target_vessel_raw
        )

        if tcpa_s < 0 and dcpa_m >= CPA_SAFE_M:
            if getattr(self, "_bridge_release_fallback_enabled", True):
                self.get_logger().info(
                    f"[BRIDGE] Geometry release candidate (compat fallback): "
                    f"TCPA={tcpa_s:.1f}s < 0, DCPA={dcpa_m:.0f}m >= "
                    f"{CPA_SAFE_M:.0f}m, M6 conflict cleared")
                self._trigger_latch_release()
            else:
                self.get_logger().info(
                    f"[BRIDGE] Geometry release candidate (trace-only): "
                    f"TCPA={tcpa_s:.1f}s < 0, DCPA={dcpa_m:.0f}m >= "
                    f"{CPA_SAFE_M:.0f}m, M6 conflict cleared. "
                    "Release authority stays with M6/M4.")

    def _on_threat_state(self, msg: ThreatState) -> None:
        """M2 threat state callback — handle old CPA-cleared release candidate.

        M6 remains the hard gate; latch mutation is a removable compatibility
        fallback.
        """
        self._record_pulse(M2)
        # Old condition 1 candidate: cpa_status == cleared && target astern.
        if (not self._m6_conflict_active and
            hasattr(msg, 'cpa_status') and msg.cpa_status == "cleared" and
            hasattr(msg, 'target_relative_position') and msg.target_relative_position == "astern" and
            not self._latch_release_triggered and
            self._latch_hold_elapsed()):
            if getattr(self, "_bridge_release_fallback_enabled", True):
                self.get_logger().info(
                    "[BRIDGE] LATCH release condition 1 candidate "
                    "(compat fallback): CPA cleared, target astern, "
                    "M6 conflict cleared")
                self._trigger_latch_release()
            else:
                self.get_logger().info(
                    "[BRIDGE] LATCH release condition 1 candidate (trace-only): "
                    "CPA cleared, target astern, M6 conflict cleared")

    def _on_mission_goal(self, msg: MissionGoal) -> None:
        """M3 mission goal callback — handle old task/behavior release candidate."""
        self._record_pulse(M3)
        self._trace_writer.record("/l3/m3/mission_goal", {
            "fsm_state": int(msg.fsm_state),
            "task_validity": int(msg.task_validity) if hasattr(msg, "task_validity") else -1,
            "target_wp_lat": float(msg.current_target_wp.latitude),
            "target_wp_lon": float(msg.current_target_wp.longitude),
        }, self._get_sim_time())
        
        # DECOUPLE (D-DEMO1 bridge dead-stick fix): do NOT tear down avoidance
        # when the mission FSM is not ACTIVE.  M3 publishes fsm_state=1
        # (AwaitingRoute, current_target_wp=(0,0)) at ~6 Hz whenever no route is
        # delivered (K1 keystone); resetting `_avoidance_active` on every such
        # message re-creates the dead-stick even after _on_avoidance_plan arms.
        # Avoidance lifecycle is owned by _on_avoidance_plan (M5 plan validity)
        # and _on_lifecycle_status (scenario deactivate), never by M3 mission fsm.
        if msg.fsm_state < 3:
            self._current_target_wp_lat = 0.0
            self._current_target_wp_lon = 0.0
            return

        # M3 has reached ACTIVE: lift the cold-start arm guard.
        self._m3_activated_once = True

        if (abs(msg.current_target_wp.latitude) > 1e-4 or
                abs(msg.current_target_wp.longitude) > 1e-4):
            self._current_target_wp_lat = float(msg.current_target_wp.latitude)
            self._current_target_wp_lon = float(msg.current_target_wp.longitude)

        # Old condition 2 candidate: task_validity == valid && behavior == TRANSIT.
        # Handle it only after LATCH_MIN_HOLD_S and M6 conflict cleared. Latch
        # mutation is a temporary compatibility fallback until M4/L4 handback owns
        # route return.
        task_valid = hasattr(msg, 'task_validity') and msg.task_validity in (1, "valid")
        behavior_transit = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior == 0  # BEHAVIOR_TRANSIT
        )
        if (not self._m6_conflict_active and
                task_valid and behavior_transit and
                not self._latch_release_triggered and
                self._latch_hold_elapsed()):
            if getattr(self, "_bridge_release_fallback_enabled", True):
                self.get_logger().info(
                    "[BRIDGE] LATCH release condition 2 candidate "
                    "(compat fallback): task_valid + TRANSIT + "
                    "M6 conflict cleared")
                self._trigger_latch_release()
            else:
                self.get_logger().info(
                    "[BRIDGE] LATCH release condition 2 candidate (trace-only): "
                    "task_valid + TRANSIT + M6 conflict cleared")

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

    def _arm_avoidance_from_m6(self) -> None:
        """ARM avoidance when M6 reports conflict_detected=True + M4=COLREG_AVOID.

        [ADR-1] This is the correct arm authority per design: M6 (COLREGs Reasoner)
        is the sole detector of avoidance obligation; M4 (Behavior Arbiter) confirms
        the behavior mode. M5 plan provides waypoints only (guidance data).

        Idempotent: safe to call on every M6 message when conflict_detected=True.
        Does nothing if M4 is still in TRANSIT (wait for M4 to switch to AVOID).
        """
        if self._avoidance_active:
            return  # already armed
        m4_in_avoid = (
            self._last_behavior_plan is not None and
            self._last_behavior_plan.behavior != 0  # not BEHAVIOR_TRANSIT
        )
        if not m4_in_avoid:
            # M4 not yet in avoidance mode — wait; will retry on next M6 message
            return
        sim_t = self._get_sim_time()
        self.get_logger().info(
            f"[BRIDGE] ARM avoidance via M6 conflict authority at sim_t={sim_t:.1f}s "
            f"(M4 behavior={self._last_behavior_plan.behavior})"
        )
        self._avoidance_active = True
        self._avoidance_armed_time = sim_t
        self._reset_latch_release_state()

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
        logger = getattr(self, 'get_logger', None)
        if logger is not None:
            logger().info(
                f"[BRIDGE-DIAG] Received avoidance plan: n_waypoints={len(msg.waypoints)}"
            )
        _wp0 = msg.waypoints[0] if msg.waypoints else None
        _wp1 = msg.waypoints[1] if len(msg.waypoints) > 1 else None
        _wp0_pos = getattr(_wp0, "position", None) if _wp0 else None
        _wp1_pos = getattr(_wp1, "position", None) if _wp1 else None
        self._trace_writer.record("/l3/m5/avoidance_plan", {
            "n_waypoints": len(msg.waypoints),
            "solver_status": "VALID" if (_wp0 and abs(_wp0.turn_radius_m) > 1e-6) else "EMPTY",
            "wp0_turn_radius_m": float(_wp0.turn_radius_m) if _wp0 else 0.0,
            "wp0_target_speed_kn": float(_wp0.target_speed_kn) if _wp0 else 0.0,
            "wp0_lat": float(_wp0_pos.latitude) if _wp0_pos else 0.0,
            "wp0_lon": float(_wp0_pos.longitude) if _wp0_pos else 0.0,
            "wp1_lat": float(_wp1_pos.latitude) if _wp1_pos else 0.0,
            "wp1_lon": float(_wp1_pos.longitude) if _wp1_pos else 0.0,
        }, self._get_sim_time())

        has_valid_plan = (
            len(msg.waypoints) > 0 and
            abs(msg.waypoints[0].turn_radius_m) > 1e-6
        )
        if has_valid_plan:
            self._last_valid_plan_time = self._get_sim_time()
            self._last_avoidance_waypoint = msg.waypoints[0]
            self._last_avoidance_waypoints = list(msg.waypoints)

        if not has_valid_plan and getattr(self, "_m6_conflict_active", False):
            if self._avoidance_active:
                self.get_logger().info(
                    "[BRIDGE] IGNORE empty M5 plan while M6 conflict active"
                )
            return

        if (not has_valid_plan and self._avoidance_active and
                self._latch_release_triggered):
            self.get_logger().info(
                "[BRIDGE] IGNORE empty M5 plan while LATCH release is decaying"
            )
            return

        if self._autopilot_enabled and not has_valid_plan:
            if self._avoidance_active:
                self.get_logger().info(
                    "[BRIDGE] RESET — valid plan lost while autopilot enabled; disarming"
                )
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
                self._reset_latch_release_state()
        elif has_valid_plan:
            if not self._avoidance_active:
                m4_allows_avoidance = (
                    self._last_behavior_plan is not None and
                    self._last_behavior_plan.behavior != 0)
                if not m4_allows_avoidance:
                    self.get_logger().info(
                        "[BRIDGE] Valid M5 plan cached; waiting for M4 AVOID before arming")
                    return
                # Collision avoidance is a safety reflex once M4 has selected
                # an avoidance behavior. Do not let stale M5 plans re-arm while
                # M4 has already returned to TRANSIT.
                sim_t_now = self._get_sim_time()
                if sim_t_now < 5.0:
                    # Very early arm (< 5 sim-s) — M4 may not yet have received
                    # any M2 data. Log but do NOT block; continuous tracking handles.
                    self.get_logger().info(
                        f"[BRIDGE] AVOIDANCE ARM at early sim_t={sim_t_now:.1f}s "
                        "(M4 window may not yet be stable; G1 will track continuously)"
                    )
                # Arm avoidance whenever M5 delivers a valid plan with non-zero
                # turn_radius — do NOT gate on current M4 behavior, because M4
                # cycles at 1-4 Hz and may have already reverted to TRANSIT by
                # the time M5's plan (computed 1+ s later) arrives here.
                self._avoidance_active = True
                self._avoidance_armed_time = self._get_sim_time()
                self._reset_latch_release_state()
                # Use M4 heading window if it shows a non-TRANSIT (avoidance)
                # behavior; otherwise defer to _on_behavior_plan (continuous tracking).
                if (self._last_behavior_plan is not None and
                        self._last_behavior_plan.behavior != 0 and
                        self._last_behavior_plan.heading_max_deg > 0.0):
                    beh = self._last_behavior_plan
                    h_min = float(beh.heading_min_deg)
                    h_max = float(beh.heading_max_deg)
                    candidate = _m4_colregs_window_target_deg(
                        h_min, h_max, self._target_heading_deg)
                    if candidate is None:
                        if h_max < h_min:
                            h_max += 360.0
                        h_span = h_max - h_min
                        # Degenerate window (≈full circle): defer to M5 waypoint rudder.
                        self.get_logger().info(
                            f"[BRIDGE] LATCHED (degenerate M4 window "
                            f"[{h_min:.1f},{h_max:.1f}] span={h_span:.1f}°) "
                            "— target_heading deferred to M5 waypoint rudder"
                        )
                    else:
                        self._avoidance_target_heading_deg = candidate

                        self.get_logger().info(
                            f"[BRIDGE] ARMED avoidance target_heading="
                            f"{self._avoidance_target_heading_deg:.1f}° "
                            f"from M4 window [{h_min:.1f}, {h_max:.1f}]"
                        )
                else:
                    # M4 already reverted to TRANSIT or no behavior plan seen yet —
                    # leave target as None; _on_behavior_plan will fill it in on next
                    # M4 AVOID message (G1 continuous tracking).
                    self.get_logger().info(
                        "[BRIDGE] ARMED (M4 TRANSIT or not yet seen) — "
                        "target_heading deferred to next M4 AVOID window"
                    )
        else:
            if self._avoidance_active:
                self.get_logger().info(
                    "[BRIDGE] RESET — valid plan lost; disarming avoidance"
                )
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
        if self._l4_adapter_enabled:
            return
        if self._avoidance_active:
            now = self._get_sim_time()
            # G3: check geometry-based release every step (replaces 5 s timer)
            self._check_geometry_release()
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
        current_sog_kn = self._last_ownship_raw.sog * 1.94384
        current_rot_deg_s = math.degrees(self._last_ownship_raw.rot)
        current_lat = getattr(self._last_ownship_raw, "lat", 0.0)
        current_lon = getattr(self._last_ownship_raw, "lon", 0.0)

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

        waypoint_target_heading_deg = SilTopicBridge._avoidance_waypoint_heading_deg(
            self, current_lat, current_lon)
        if waypoint_target_heading_deg is not None:
            heading_error_deg = (
                waypoint_target_heading_deg - current_heading_deg + 180.0
            ) % 360.0 - 180.0
            dt = 0.5
            out.rudder_angle = RUDDER_SIGN * self._avoidance_heading_controller.step(
                heading_error_deg, dt, current_rot_deg_s)
            self.get_logger().info(
                f"[BRIDGE-AVOID] hdg={current_heading_deg:.1f} "
                f"m5_tgt={waypoint_target_heading_deg:.1f} "
                f"err={heading_error_deg:.1f} rot={current_rot_deg_s:.2f} "
                f"rud={math.degrees(out.rudder_angle):.1f}"
            )
        elif self._avoidance_target_heading_deg is not None:
            heading_error_deg = (
                self._avoidance_target_heading_deg - current_heading_deg + 180.0
            ) % 360.0 - 180.0
            dt = 0.5
            out.rudder_angle = RUDDER_SIGN * self._avoidance_heading_controller.step(
                heading_error_deg, dt, current_rot_deg_s)
            self.get_logger().info(
                f"[BRIDGE-AVOID] hdg={current_heading_deg:.1f} "
                f"tgt={self._avoidance_target_heading_deg:.1f} "
                f"err={heading_error_deg:.1f} rot={current_rot_deg_s:.2f} "
                f"rud={math.degrees(out.rudder_angle):.1f}"
            )
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
            target_sog_kn = self._last_avoidance_waypoint.target_speed_kn
            feedforward = max(0.0, min(1.0, target_sog_kn / MAX_SPEED_KN))
            controller = getattr(self, "_speed_controller", None)
            if controller is not None:
                speed_error_kn = target_sog_kn - current_sog_kn
                closed_loop = controller.step(speed_error_kn, 0.5)
                out.throttle = max(feedforward, closed_loop)
            else:
                out.throttle = feedforward
        else:
            out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN

        return out

    def _avoidance_waypoint_heading_deg(self, own_lat: float, own_lon: float) -> Optional[float]:
        waypoints = list(getattr(self, "_last_avoidance_waypoints", []) or [])
        if not waypoints:
            wp = getattr(self, "_last_avoidance_waypoint", None)
            if wp is not None:
                waypoints = [wp]

        nominal = float(getattr(self, "_target_heading_deg", 0.0))
        preferred = getattr(self, "_avoidance_target_heading_deg", None)
        preferred_delta = None
        if preferred is not None:
            preferred_delta = _signed_heading_delta_deg(float(preferred), nominal)
        best_heading = None
        best_score = float("inf")
        for wp in waypoints:
            pos = getattr(wp, "position", None)
            if pos is None:
                continue
            try:
                lat = float(pos.latitude)
                lon = float(pos.longitude)
            except (TypeError, ValueError):
                continue
            if not (math.isfinite(lat) and math.isfinite(lon)):
                continue

            m_per_deg_lat = 111132.9
            m_per_deg_lon = 111319.9 * math.cos(math.radians(float(own_lat)))
            dist_m = math.hypot((lon - float(own_lon)) * m_per_deg_lon,
                                (lat - float(own_lat)) * m_per_deg_lat)
            if dist_m < M5_AVOID_WAYPOINT_MIN_LOOKAHEAD_M:
                continue

            bearing = SilTopicBridge._great_circle_bearing(
                float(own_lat), float(own_lon), lat, lon)
            candidate_delta = _signed_heading_delta_deg(bearing, nominal)
            if abs(candidate_delta) > M5_AVOID_WAYPOINT_MAX_DELTA_DEG:
                continue
            if preferred_delta is not None:
                if getattr(self, "_m6_conflict_active", False):
                    if (abs(preferred_delta) > 1e-3 and
                            preferred_delta * candidate_delta < 0.0):
                        continue
                    if (abs(candidate_delta) + M5_AVOID_WAYPOINT_TARGET_TOLERANCE_DEG <
                            abs(preferred_delta)):
                        continue
                score = abs(_signed_heading_delta_deg(bearing, float(preferred)))
                if score < best_score:
                    best_score = score
                    best_heading = bearing
            else:
                return bearing
        return best_heading

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
        """Signed cross-track distance (m) of own-ship from the active route leg.

        +ve = own-ship to port (left) of route direction; -ve = starboard.
        Prefer the leg ending at M3's current target waypoint. If M3 has not
        published a target yet, fall back to the nearest segment of the route.
        """
        if len(self._route_wps) < 2:
            return None

        target_lat = getattr(self, "_current_target_wp_lat", 0.0)
        target_lon = getattr(self, "_current_target_wp_lon", 0.0)
        if abs(target_lat) > 1e-4 or abs(target_lon) > 1e-4:
            target_idx = min(
                range(len(self._route_wps)),
                key=lambda i: (self._route_wps[i][0] - target_lat) ** 2
                              + (self._route_wps[i][1] - target_lon) ** 2,
            )
            if target_idx == 0:
                return SilTopicBridge._segment_signed_xte_m(
                    self._route_wps[0], self._route_wps[1], own_lat, own_lon)
            return SilTopicBridge._segment_signed_xte_m(
                self._route_wps[target_idx - 1], self._route_wps[target_idx],
                own_lat, own_lon)

        best = None
        for idx in range(len(self._route_wps) - 1):
            xte_m, distance_m = SilTopicBridge._segment_xte_and_distance_m(
                self._route_wps[idx], self._route_wps[idx + 1], own_lat, own_lon)
            if xte_m is None:
                continue
            if best is None or distance_m < best[1]:
                best = (xte_m, distance_m)
        return None if best is None else best[0]

    @staticmethod
    def _segment_signed_xte_m(start_wp, end_wp, own_lat, own_lon):
        xte_m, _ = SilTopicBridge._segment_xte_and_distance_m(
            start_wp, end_wp, own_lat, own_lon)
        return xte_m

    @staticmethod
    def _segment_xte_and_distance_m(start_wp, end_wp, own_lat, own_lon):
        m_per_deg_lat = 111132.9
        m_per_deg_lon = 111319.9 * math.cos(math.radians(start_wp[0]))
        ax = start_wp[1] * m_per_deg_lon
        ay = start_wp[0] * m_per_deg_lat
        bx = end_wp[1] * m_per_deg_lon
        by = end_wp[0] * m_per_deg_lat
        px = own_lon * m_per_deg_lon
        py = own_lat * m_per_deg_lat
        dx, dy = bx - ax, by - ay
        seg = math.hypot(dx, dy)
        if seg < 1.0:
            return None, float("inf")
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (seg * seg)))
        closest_x = ax + t * dx
        closest_y = ay + t * dy
        distance_m = math.hypot(px - closest_x, py - closest_y)
        xte_m = (dx * (py - ay) - dy * (px - ax)) / seg
        return xte_m, distance_m

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
