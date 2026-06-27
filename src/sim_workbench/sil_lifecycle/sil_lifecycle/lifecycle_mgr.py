"""Scenario Lifecycle Manager — LifecycleNode wrapping 5-state FSM.

States: UNCONFIGURED -> INACTIVE -> ACTIVE -> DEACTIVATING -> FINALIZED
Publishes /sim_clock @ 1kHz and /sil/lifecycle_status @ 1Hz.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md Sec3
"""

import time
import os
import socket
import math
import threading
from enum import IntEnum

import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
try:
    from rclpy.qos import qos_profile_clock
except ImportError:
    qos_profile_clock = QoSProfile(
        reliability=ReliabilityPolicy.BEST_EFFORT,
        durability=DurabilityPolicy.VOLATILE,
        history=HistoryPolicy.KEEP_LAST,
        depth=1,
    )

from builtin_interfaces.msg import Time as TimeMsg
from sil_msgs.msg import LifecycleStatus
try:
    from rosgraph_msgs.msg import Clock as ClockMsg
except ImportError:
    class ClockMsg:
        def __init__(self):
            self.clock = None

try:
    from l3_msgs.msg import BehaviorPlan, AvoidancePlan, ODDState, TrackedTarget
    from l3_external_msgs.msg import FilteredOwnShipState, TrackedTargetArray
    from std_msgs.msg import Header
    _HAS_L3_MSGS = True
except ImportError:
    _HAS_L3_MSGS = False


# ── QoS profiles per Doc 2 §7.3 ────────────────────────────────────────────

_SIM_CLOCK_QOS = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)

_STATUS_QOS = QoSProfile(
    depth=5,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)

_QOS_VOLATILE = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
)

_QOS_BEST_EFFORT = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.BEST_EFFORT,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
)


# ── Navigation math utilities for internal dynamics ────────────────────────

def _lat_offset(meters: float, lat_ref_rad: float) -> float:
    return meters / 111120.0

def _lon_offset(meters: float, lat_rad: float) -> float:
    cos_lat = math.cos(lat_rad)
    if abs(cos_lat) < 1e-10:
        cos_lat = 1e-10
    return meters / (111120.0 * cos_lat)

def _normalize_angle_rad(angle_rad: float) -> float:
    return angle_rad % (2.0 * math.pi)

def _math_heading_to_nav_heading(psi_rad: float) -> float:
    return _normalize_angle_rad((math.pi / 2.0) - psi_rad)

def _ground_track_to_nav_cog(psi_rad: float, u_mps: float, v_mps: float) -> float:
    east_mps = u_mps * math.cos(psi_rad) - v_mps * math.sin(psi_rad)
    north_mps = u_mps * math.sin(psi_rad) + v_mps * math.cos(psi_rad)
    if math.hypot(east_mps, north_mps) < 1e-9:
        return _math_heading_to_nav_heading(psi_rad)
    return _normalize_angle_rad(math.atan2(east_mps, north_mps))


# ── Simple Autopilot controllers for internal dynamics ──────────────────────

class HeadingController:
    def __init__(self, Kp: float = 1.0, max_rate_deg_s: float = 5.0):
        self.Kp = Kp
        self.max_rate_deg_s = max_rate_deg_s
        self.last_cmd_deg = 0.0

    def step(self, error_deg: float, dt: float, current_rot_deg_s: float = 0.0) -> float:
        error_deg = (error_deg + 180.0) % 360.0 - 180.0
        cmd_deg = self.Kp * error_deg
        cmd_deg = max(-35.0, min(35.0, cmd_deg))
        max_delta = self.max_rate_deg_s * dt
        cmd_deg = max(self.last_cmd_deg - max_delta, min(self.last_cmd_deg + max_delta, cmd_deg))
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
        cmd = max(self.last_cmd - max_delta, min(self.last_cmd + max_delta, cmd))
        cmd = max(0.0, min(1.0, cmd))
        self.last_cmd = cmd
        return cmd


# ── Enums (preserved from v0 stub) ──────────────────────────────────────────

class LifecycleState(IntEnum):
    """Primary states matching lifecycle_msgs/State constants."""
    UNCONFIGURED = 1
    INACTIVE = 2
    ACTIVE = 3
    DEACTIVATING = 4
    FINALIZED = 5


class Transition(IntEnum):
    """Transition identifiers matching lifecycle_msgs/Transition."""
    CONFIGURE = 1
    ACTIVATE = 3
    DEACTIVATE = 4
    CLEANUP = 6


# ── Pure-Python FSM (preserved from v0 stub) ────────────────────────────────

class ScenarioLifecycleMgr:
    """Pure-Python lifecycle FSM for offline testing and Phase 1 mock.

    Phase 2: wrapped by LifecycleManagerNode.
    Phase 1: used directly by orchestrator lifecycle_bridge.
    """

    def __init__(self, tick_hz: float = 50.0) -> None:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str = ""
        self._scenario_hash: str = ""
        self._tick_hz = tick_hz
        self._sim_rate: float = 1.0
        self._sim_time: float = 0.0
        self._wall_start: float = 0.0
        self._rate_anchor_wall: float = 0.0
        self._rate_anchor_sim: float = 0.0
        self._dynamics_mode: str = "internal"
        self._clock_mode: str = "realtime"

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str:
        return self._scenario_id

    @property
    def scenario_hash(self) -> str:
        return self._scenario_hash

    @property
    def sim_time(self) -> float:
        return self._sim_time

    @property
    def sim_rate(self) -> float:
        return self._sim_rate

    @property
    def run_start_wall(self) -> float:
        return self._wall_start

    @property
    def tick_hz(self) -> float:
        return self._tick_hz

    @property
    def dynamics_mode(self) -> str:
        return self._dynamics_mode

    @property
    def clock_mode(self) -> str:
        return self._clock_mode

    @property
    def wall_time(self) -> float:
        """Elapsed wall-clock time since activation (seconds)."""
        if self._wall_start > 0:
            return time.time() - self._wall_start
        return 0.0

    def configure(self, scenario_id: str, scenario_hash: str = "", dynamics_mode: str = "internal", clock_mode: str = "realtime") -> bool:
        if self._state != LifecycleState.UNCONFIGURED:
            return False
        if dynamics_mode not in ("internal", "fmi"):
            return False
        if clock_mode not in ("realtime", "free_run"):
            return False
        self._scenario_id = scenario_id
        self._scenario_hash = scenario_hash
        self._sim_time = 0.0
        # Fresh scenario starts at the 1x default — a sim_rate set dynamically
        # in a prior run must not persist across configure (else the next run
        # silently plays at the old rate, e.g. 10x).
        self._sim_rate = 1.0
        self._dynamics_mode = dynamics_mode
        self._clock_mode = clock_mode
        self._state = LifecycleState.INACTIVE
        return True

    def activate(self) -> bool:
        if self._state != LifecycleState.INACTIVE:
            return False
        self._state = LifecycleState.ACTIVE
        self._wall_start = time.time()
        self._rate_anchor_wall = self._wall_start
        self._rate_anchor_sim = self._sim_time
        return True

    def deactivate(self) -> bool:
        if self._state != LifecycleState.ACTIVE:
            return False
        self._state = LifecycleState.DEACTIVATING
        self._state = LifecycleState.INACTIVE
        return True

    def cleanup(self) -> bool:
        if self._state not in (LifecycleState.INACTIVE, LifecycleState.FINALIZED):
            return False
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id = ""
        self._scenario_hash = ""
        self._sim_time = 0.0
        return True

    def set_sim_rate(self, rate: float) -> bool:
        if rate < 0:
            return False
        # If the rate is unchanged, this is a redundant call (e.g. runner sets
        # 10x after configure already applied 10x). Do NOT re-anchor, because
        # re-anchoring locks in the sim_time advanced during the
        # configure->set_rate gap and breaks cross-run reproducibility.
        if abs(rate - self._sim_rate) < 1e-9:
            return True
        # Anchor the rate change so _clock_callback doesn't try to
        # catch up to wall_elapsed * new_rate from t=0.
        self._rate_anchor_wall = time.time()
        self._rate_anchor_sim = self._sim_time
        self._sim_rate = rate
        return True

    def set_dynamics_mode(self, mode: str) -> bool:
        if self._state != LifecycleState.INACTIVE:
            return False
        if mode not in ("internal", "fmi"):
            return False
        self._dynamics_mode = mode
        return True

    def tick(self) -> None:
        """Advance simulation time by one tick (called at tick_hz)."""
        if self._state == LifecycleState.ACTIVE:
            self._sim_time += 1.0 / self._tick_hz

    def get_status_dict(self) -> dict:
        return {
            "current_state": self._state.name,
            "scenario_id": self._scenario_id,
            "scenario_hash": self._scenario_hash,
            "sim_time": self._sim_time,
            "wall_time": self.wall_time,
            "sim_rate": self._sim_rate,
            "dynamics_mode": self._dynamics_mode,
            "clock_mode": self._clock_mode,
        }


# ── LifecycleNode (ROS2 wrapper) ────────────────────────────────────────────

class LifecycleManagerNode(LifecycleNode):
    """rclpy LifecycleNode wrapping ScenarioLifecycleMgr.

    Lifecycle callbacks:
      on_configure  → declare params, init FSM
      on_activate   → create publishers + timers (or start free-run thread)
      on_deactivate → destroy publishers + timers (or stop free-run thread)
      on_cleanup    → reset FSM state
    """

    # Maximum dt_tick steps to emit per single callback invocation.
    # Must be >= max supported sim_rate (50x) to avoid rate cap.
    _MAX_CATCHUP_TICKS = 50

    def __init__(self, node_name: str = "scenario_lifecycle_mgr") -> None:
        # Force use_sim_time to False during construction to avoid circular dependencies in clock server
        try:
            from rclpy.parameter import Parameter
            overrides = [Parameter('use_sim_time', Parameter.Type.BOOL, False)]
        except ImportError:
            overrides = None

        # Dynamically inspect parent class __init__ to support both real rclpy and simpler test mock class signatures
        import inspect
        kwargs = {}
        try:
            sig = inspect.signature(super().__init__)
            if "parameter_overrides" in sig.parameters and overrides is not None:
                kwargs["parameter_overrides"] = overrides
            if "allow_undeclared_parameters" in sig.parameters:
                kwargs["allow_undeclared_parameters"] = True
            if "automatically_declare_parameters_from_overrides" in sig.parameters:
                kwargs["automatically_declare_parameters_from_overrides"] = True
        except Exception:
            pass

        super().__init__(node_name, **kwargs)
        self._fsm = ScenarioLifecycleMgr()

        # ROS2 resources — created in on_activate, destroyed in on_deactivate
        self._sim_clock_pub = None
        self._clock_pub = None
        self._status_pub = None
        self._sim_clock_timer = None
        self._status_timer = None
        self._run_start_wall: float | None = None

        # Free-run mode background resources
        self._free_run_thread = None
        self._free_run_active = False
        self._server_sock = None
        self._clients = []

        # L3 publishers and subscribers for internal dynamics mode
        self._pub_foss = None
        self._pub_tta = None
        self._sub_behavior = None
        self._sub_avoidance = None
        self._sub_odd = None
        self._sub_m7 = None

        if hasattr(self, "add_on_set_parameters_callback"):
            self.add_on_set_parameters_callback(self._on_set_parameters)

    def _on_set_parameters(self, params):
        from rcl_interfaces.msg import SetParametersResult
        result = SetParametersResult(successful=True)
        for param in params:
            if param.name == "sim_rate":
                rate = param.value
                if rate < 0.0:
                    result.successful = False
                    result.reason = "sim_rate cannot be negative"
                else:
                    self._fsm.set_sim_rate(rate)
                    self.get_logger().info(f"sim_rate updated dynamically to {rate}")
        return result

    # ── Autopilot Subscriber callbacks for free-run mode ──────────────────

    def _cb_behavior(self, msg):
        self._received_doer = True
        self._last_behavior_plan = msg
        if self._avoidance_active and self._avoidance_target_heading_deg is None:
            h_min = float(msg.heading_min_deg)
            h_max = float(msg.heading_max_deg)
            if h_max < h_min:
                h_max += 360.0
            self._avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0

    def _cb_avoidance(self, msg):
        self._last_avoidance_plan = msg
        has_valid_plan = len(msg.waypoints) > 0 and abs(msg.waypoints[0].turn_radius_m) > 1e-6
        if has_valid_plan:
            self._last_valid_plan_time = self._fsm.sim_time
            self._last_avoidance_waypoint = msg.waypoints[0]
            
        if self._autopilot_enabled and not has_valid_plan:
            if self._avoidance_active:
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0
        elif has_valid_plan:
            if not self._avoidance_active:
                if self._last_behavior_plan is not None:
                    self._avoidance_active = True
                    beh = self._last_behavior_plan
                    h_min = float(beh.heading_min_deg)
                    h_max = float(beh.heading_max_deg)
                    if h_max < h_min:
                        h_max += 360.0
                    self._avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0
        else:
            if self._avoidance_active:
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._avoidance_heading_controller.last_cmd_deg = 0.0

    def _cb_odd(self, msg):
        self._last_odd_state = msg

    def _cb_m7(self, msg):
        self._received_m7 = True

    def _compute_autopilot(self, sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s) -> tuple[float, float]:
        """Returns (rudder_angle_rad, throttle)"""
        RUDDER_SIGN = -1
        CRUISE_SPEED_KN = 10.0
        MAX_SPEED_KN = 25.0
        
        # Decide if Autopilot is enabled
        if self._last_odd_state is not None:
            env_state = self._last_odd_state.envelope_state
            env_allows_autopilot = env_state in (ODDState.ENVELOPE_IN, ODDState.ENVELOPE_EDGE, ODDState.ENVELOPE_MRC_PREP)
        else:
            env_allows_autopilot = True
            
        is_m5_stale = (sim_t - self._last_valid_plan_time) > 10.0
        m4_in_fallback = self._last_behavior_plan is not None and "fallback" in self._last_behavior_plan.rationale.lower()
        
        self._autopilot_enabled = env_allows_autopilot and (is_m5_stale or m4_in_fallback)

        if self._avoidance_active:
            if self._avoidance_target_heading_deg is not None:
                heading_error_deg = (self._avoidance_target_heading_deg - current_heading_deg + 180.0) % 360.0 - 180.0
                dt = 1.0 / self._fsm.tick_hz
                rudder = RUDDER_SIGN * self._avoidance_heading_controller.step(heading_error_deg, dt, current_rot_deg_s)
            elif self._last_avoidance_waypoint is not None:
                wp = self._last_avoidance_waypoint
                if abs(wp.turn_radius_m) > 1e-6:
                    radius = max(abs(wp.turn_radius_m), 50.0)
                    rudder_rad = math.atan2(46.0, radius)
                    rudder = RUDDER_SIGN * max(-math.radians(35.0), min(math.radians(35.0), rudder_rad))
                else:
                    rudder = 0.0
            else:
                rudder = 0.0
                
            if self._last_avoidance_waypoint is not None:
                throttle = max(0.0, min(1.0, self._last_avoidance_waypoint.target_speed_kn / MAX_SPEED_KN))
            else:
                throttle = CRUISE_SPEED_KN / MAX_SPEED_KN
            return rudder, throttle

        if self._autopilot_enabled:
            heading_error_deg = (self._target_heading_deg - current_heading_deg + 180.0) % 360.0 - 180.0
            speed_error_kn = self._target_sog_kn - current_sog_kn
            dt = 1.0 / self._fsm.tick_hz
            rudder = RUDDER_SIGN * self._heading_controller.step(heading_error_deg, dt, current_rot_deg_s)
            throttle = self._speed_controller.step(speed_error_kn, dt)
            return rudder, throttle

        return 0.0, CRUISE_SPEED_KN / MAX_SPEED_KN

    # ── Lifecycle callbacks ──────────────────────────────────────────────

    def on_configure(self, state) -> TransitionCallbackReturn:
        """Declare ROS params and transition FSM UNCONFIGURED → INACTIVE."""

        for name, default in (
            ("scenario_id", ""),
            ("scenario_hash", ""),
            ("tick_hz", 250.0),
            ("status_hz", 1.0),
            ("sim_rate", 1.0),
            ("clock_mode", "realtime"),
            ("dynamics_mode", "internal"),
            ("lockstep_clients", 2),
        ):
            if not hasattr(self, "has_parameter") or not self.has_parameter(name):
                # With allow_undeclared_parameters=True, a parameter pre-set via
                # SetParameters injection is accessible via get_parameter() even
                # before declaration. Lifecycle CLEANUP undeclares parameters but
                # leaves the injected value in the undeclared store. Preserve it.
                try:
                    _pre = self.get_parameter(name).value
                except Exception:
                    _pre = None
                self.declare_parameter(name, _pre if _pre is not None else default)

        sid = self.get_parameter("scenario_id").value
        shash = self.get_parameter("scenario_hash").value
        self._fsm._tick_hz = self.get_parameter("tick_hz").value
        initial_rate = self.get_parameter("sim_rate").value

        clock_mode = self.get_parameter("clock_mode").value
        dynamics_mode = self.get_parameter("dynamics_mode").value

        if clock_mode not in ("realtime", "free_run"):
            self.get_logger().error(f"Invalid clock_mode: {clock_mode}")
            return TransitionCallbackReturn.FAILURE
        if dynamics_mode not in ("internal", "fmi"):
            self.get_logger().error(f"Invalid dynamics_mode: {dynamics_mode}")
            return TransitionCallbackReturn.FAILURE

        cfg_ok = self._fsm.configure(str(sid), str(shash), dynamics_mode=str(dynamics_mode), clock_mode=str(clock_mode))
        # Apply the configured sim_rate AFTER fsm.configure (which resets the
        # rate to 1.0 to prevent cross-run carry-over). Doing it before configure
        # was a no-op — configure overwrote it. With this order, the activate
        # timer advances sim_time at the target rate from t=0, so there is no
        # 1x window between configure and the later set_sim_rate(10) call whose
        # length varies per run and breaks cross-run reproducibility
        # (DIAG evidence 2026-06-27: run1 anchor_sim=31.34, run2=0.00).
        if initial_rate is not None and float(initial_rate) != 1.0:
            self._fsm.set_sim_rate(float(initial_rate))

        # Initialize internal dynamics state variables in free-run mode
        if clock_mode == "free_run" and dynamics_mode == "internal":
            try:
                import sys
                for p in [
                    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
                    "./src/sim_workbench/sil_nodes/ship_dynamics",
                ]:
                    abs_p = os.path.abspath(p)
                    if abs_p not in sys.path:
                        sys.path.insert(0, abs_p)
                        
                from ship_dynamics.mmg_coefficients import MMGCoefficients
                from ship_dynamics.mmg_model import MMGModel, ShipState
                
                self._dt = 1.0 / self._fsm.tick_hz
                self._coeffs = MMGCoefficients(dt=self._dt)
                self._model = MMGModel(self._coeffs)
                self._own_state = ShipState(
                    x=0.0, y=0.0, psi=math.pi / 2.0, phi=0.0,
                    u=5.14444, v=0.0, r=0.0, p=0.0
                )
                
                self._origin_lat_rad = math.radians(self._coeffs.origin_lat)
                self._origin_lon_rad = math.radians(self._coeffs.origin_lon)
                
                # Target vessel (TS1) state
                self._ts_mmsi = 100000001
                self._ts_lat = 63.557451
                self._ts_lon = 10.38
                self._ts_heading = math.radians(180.0) # heading South
                self._ts_sog = 10.0 * 0.514444 # 10 kn -> m/s
                
                # Autopilot state variables
                self._received_doer = False
                self._received_m7 = False
                self._autopilot_enabled = False
                self._avoidance_active = False
                self._avoidance_target_heading_deg = None
                self._last_odd_state = None
                self._last_behavior_plan = None
                self._last_avoidance_plan = None
                self._last_valid_plan_time = 0.0
                self._last_avoidance_waypoint = None
                
                self._heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=5.0)
                self._avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=10.0)
                self._speed_controller = SpeedController()
                
                self._target_heading_deg = 0.0
                self._target_sog_kn = 10.0
                
                self.get_logger().info("Free-run internal dynamics initialized successfully.")
            except Exception as e:
                self.get_logger().warn(f"Could not initialize free-run internal dynamics: {e}")

        self.get_logger().info(
            f"[on_configure] scenario_id={sid} scenario_hash={shash} sim_rate={initial_rate} clock_mode={clock_mode}"
        )
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state) -> TransitionCallbackReturn:
        """Create publishers + timers (or start free-run thread); transition FSM INACTIVE → ACTIVE."""
        # Publishers — reuse if they already exist (cross-run: keep the same DDS
        # writer instance so subscribers like trace_writer do not need to re-discover
        # a new publisher after on_deactivate. Destroying/recreating publishers
        # across runs forces CycloneDDS to rematch subscribers, which can take
        # 100+ seconds wall and backlog-deliver hundreds of sim-seconds of stale
        # messages, breaking cross-run reproducibility without container restart.
        if self._sim_clock_pub is None:
            self._sim_clock_pub = self.create_publisher(
                TimeMsg, "/sim_clock", qos_profile=_SIM_CLOCK_QOS
            )
        if self._clock_pub is None:
            self._clock_pub = self.create_publisher(
                ClockMsg, "/clock", qos_profile=qos_profile_clock
            )
        if self._status_pub is None:
            self._status_pub = self.create_publisher(
                LifecycleStatus, "/sil/lifecycle_status", qos_profile=_STATUS_QOS
            )

        tick_hz = self.get_parameter("tick_hz").value
        status_hz = self.get_parameter("status_hz").value
        clock_mode = self.get_parameter("clock_mode").value
        dynamics_mode = self.get_parameter("dynamics_mode").value

        if clock_mode == "free_run":
            if _HAS_L3_MSGS and dynamics_mode == "internal":
                self._pub_foss = self.create_publisher(
                    FilteredOwnShipState, "/fusion/own_ship_state", qos_profile=_QOS_VOLATILE
                )
                self._pub_tta = self.create_publisher(
                    TrackedTargetArray, "/fusion/tracked_targets", qos_profile=_QOS_VOLATILE
                )
                self._sub_behavior = self.create_subscription(
                    BehaviorPlan, "/l3/m4/behavior_plan", self._cb_behavior, qos_profile=_QOS_VOLATILE
                )
                self._sub_avoidance = self.create_subscription(
                    AvoidancePlan, "/l3/m5/avoidance_plan", self._cb_avoidance, qos_profile=_QOS_VOLATILE
                )
                self._sub_odd = self.create_subscription(
                    ODDState, "/l3/m1/odd_state", self._cb_odd, qos_profile=_QOS_VOLATILE
                )
                self._sub_m7 = self.create_subscription(
                    Header, "/l3/m7/heartbeat", self._cb_m7, qos_profile=_QOS_BEST_EFFORT
                )

            # Start free-run thread
            self._free_run_active = True
            self._free_run_thread = threading.Thread(target=self._free_run_loop, daemon=True)
            self._free_run_thread.start()
            
            # Keep status timer running on wall clock for 1Hz updates
            if hasattr(self, "create_wall_timer"):
                self._status_timer = self.create_wall_timer(
                    timer_period_sec=1.0 / float(status_hz),
                    callback=self._status_callback,
                )
            else:
                self._status_timer = self.create_timer(
                    1.0 / float(status_hz),
                    self._status_callback,
                )
        else:
            # Realtime mode (timers)
            if hasattr(self, "create_wall_timer"):
                self._sim_clock_timer = self.create_wall_timer(
                    timer_period_sec=1.0 / float(tick_hz),
                    callback=self._clock_callback,
                )
                self._status_timer = self.create_wall_timer(
                    timer_period_sec=1.0 / float(status_hz),
                    callback=self._status_callback,
                )
            else:
                self._sim_clock_timer = self.create_timer(
                    1.0 / float(tick_hz),
                    self._clock_callback,
                )
                self._status_timer = self.create_timer(
                    1.0 / float(status_hz),
                    self._status_callback,
                )

        self._fsm.activate()
        self._run_start_wall = time.time()
        # Publish one status immediately so the TRANSIENT_LOCAL durability cache
        # for /sil/lifecycle_status is overwritten with the fresh activate state
        # (sim_time=0 after configure). Without this, subscribers (trace_writer,
        # orchestrator) receive the stale last message from the previous run's
        # publisher instance, which carries the old sim_time/state and breaks
        # cross-run reproducibility when the container is not restarted.
        self._status_callback()
        # Likewise overwrite the TRANSIENT_LOCAL cache for /clock and /sim_clock
        # (both use TRANSIENT_LOCAL QoS). Without this, nodes with use_sim_time
        # (trace_writer, L3 modules, gnc_bridge) read the stale sim clock from the
        # prior run until the first timer tick lands, freezing their now() at the
        # old sim_time and delaying all downstream publishing by that offset.
        self._publish_clock_now()
        self.get_logger().info(
            f"[on_activate] sim_clock @ {tick_hz:.0f} Hz  "
            f"status @ {status_hz:.1f} Hz, clock_mode={clock_mode}"
        )
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state) -> TransitionCallbackReturn:
        """Destroy timers + publishers + subscriptions; transition FSM ACTIVE → INACTIVE."""
        self._fsm.deactivate()
        self._run_start_wall = None

        # Stop free-run mode background thread first
        self._free_run_active = False
        
        # Close client sockets
        if hasattr(self, "_clients") and self._clients:
            for c in self._clients:
                try:
                    c.close()
                except Exception:
                    pass
            self._clients = []
            
        # Close server socket
        if hasattr(self, "_server_sock") and self._server_sock:
            try:
                self._server_sock.close()
            except Exception:
                pass
            self._server_sock = None
            
        # Join thread
        if hasattr(self, "_free_run_thread") and self._free_run_thread is not None:
            try:
                self._free_run_thread.join(timeout=2.0)
            except Exception:
                pass
            self._free_run_thread = None

        # Clean up subscriptions and publishers
        for sub in (self._sub_behavior, self._sub_avoidance, self._sub_odd, self._sub_m7):
            if sub is not None:
                self.destroy_subscription(sub)
        self._sub_behavior = None
        self._sub_avoidance = None
        self._sub_odd = None
        self._sub_m7 = None

        for pub in (self._pub_foss, self._pub_tta):
            if pub is not None:
                self.destroy_publisher(pub)
        self._pub_foss = None
        self._pub_tta = None

        for timer in (self._sim_clock_timer, self._status_timer):
            if timer is not None:
                self.destroy_timer(timer)
        self._sim_clock_timer = None
        self._status_timer = None

        # Keep clock/status publishers alive across runs (see on_activate):
        # destroying them forces CycloneDDS to re-discover a new writer instance
        # for trace_writer and other subscribers, which backlogs stale messages
        # for up to minutes and breaks cross-run reproducibility. Only the
        # free-run internal-dynamics publishers are torn down.
        # for pub in (self._sim_clock_pub, self._clock_pub, self._status_pub):
        #     if pub is not None:
        #         self.destroy_publisher(pub)
        # self._sim_clock_pub = None
        # self._clock_pub = None
        # self._status_pub = None

        self.get_logger().info("[on_deactivate] timers destroyed (publishers kept for cross-run reuse)")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state) -> TransitionCallbackReturn:
        """Reset FSM state INACTIVE → UNCONFIGURED."""
        self._fsm.cleanup()
        self.get_logger().info("[on_cleanup] FSM reset to UNCONFIGURED")
        return TransitionCallbackReturn.SUCCESS

    # ── Free-run Thread loop ───────────────────────────────────────────────

    def _free_run_loop(self) -> None:
        """Background thread loop for free_run clock mode."""
        self.get_logger().info("Starting lockstep coordinator server accept loop...")
        
        # 1. Listen for TCP lockstep clients
        port = int(os.environ.get("SIL_LOCKSTEP_PORT", "9090"))
        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self._server_sock.bind(("0.0.0.0", port))
            self._server_sock.listen(5)
            self.get_logger().info(f"Lockstep coordinator server listening on 0.0.0.0:{port}")
        except Exception as e:
            self.get_logger().error(f"Failed to bind lockstep server to port {port}: {e}")
            return
            
        expected_clients = self.get_parameter("lockstep_clients").value
        self.get_logger().info(f"Waiting for {expected_clients} lockstep clients to connect...")
        
        self._clients = []
        while len(self._clients) < expected_clients and self._free_run_active:
            try:
                self._server_sock.settimeout(0.5)
                client_sock, client_addr = self._server_sock.accept()
                self.get_logger().info(f"Lockstep client connected from {client_addr}")
                self._clients.append(client_sock)
            except socket.timeout:
                continue
            except Exception as e:
                if self._free_run_active:
                    self.get_logger().error(f"Error accepting lockstep client: {e}")
                break
                
        if not self._free_run_active:
            self.get_logger().info("Free-run deactivated before clients connected.")
            return
            
        if len(self._clients) < expected_clients:
            self.get_logger().error(f"Only {len(self._clients)}/{expected_clients} clients connected. Aborting free-run.")
            return
            
        self.get_logger().info("All lockstep clients connected. Starting stepping loop...")
        
        dt_tick = 1.0 / self._fsm.tick_hz
        dynamics_mode = self.get_parameter("dynamics_mode").value
        
        # Stepping loop
        while self._free_run_active:
            # 1. Tick the simulation time
            self._fsm.tick()
            sim_t = self._fsm.sim_time
            
            # 2. Publish /clock and /sim_clock
            time_msg = TimeMsg()
            time_msg.sec = int(sim_t)
            time_msg.nanosec = int(round((sim_t - time_msg.sec) * 1e9))
            
            if self._sim_clock_pub is not None:
                try:
                    self._sim_clock_pub.publish(time_msg)
                except Exception as e:
                    self.get_logger().error(f"Failed to publish /sim_clock: {e}")
                    
            if self._clock_pub is not None:
                try:
                    clock_msg = ClockMsg()
                    clock_msg.clock = time_msg
                    self._clock_pub.publish(clock_msg)
                except Exception as e:
                    self.get_logger().error(f"Failed to publish /clock: {e}")
                    
            # 3. If dynamics_mode is internal, step own ship and target trajectories and publish
            if dynamics_mode == "internal" and hasattr(self, "_model") and self._model is not None:
                try:
                    # Step target vessel
                    ts_lat_rad = math.radians(self._ts_lat)
                    self._ts_lat += self._ts_sog * math.cos(self._ts_heading) * dt_tick / 111120.0
                    self._ts_lon += self._ts_sog * math.sin(self._ts_heading) * dt_tick / (111120.0 * math.cos(ts_lat_rad))
                    
                    # Publish target state
                    tgt = TrackedTarget()
                    tgt.schema_version = 112
                    tgt.stamp = time_msg
                    tgt.target_id = self._ts_mmsi
                    tgt.position.latitude = self._ts_lat
                    tgt.position.longitude = self._ts_lon
                    tgt.position.altitude = 0.0
                    tgt.heading_deg = math.degrees(self._ts_heading)
                    tgt.sog_kn = self._ts_sog * 1.94384
                    tgt.cog_deg = math.degrees(self._ts_heading)
                    for i in range(3):
                       tgt.covariance[i * 3 + i] = 1.0
                    tgt.classification = "vessel"
                    tgt.classification_confidence = 0.85
                    tgt.cpa_m = 0.0
                    tgt.tcpa_s = 0.0
                    tgt.confidence = 0.85
                    tgt.rationale = "Closed-loop lockstep coordinator"
                    tgt.source_sensor = "fused"

                    tta = TrackedTargetArray()
                    tta.schema_version = 112
                    tta.stamp = time_msg
                    tta.targets = [tgt]
                    tta.confidence = 0.85
                    tta.rationale = "Closed-loop lockstep coordinator"
                    if self._pub_tta is not None:
                        self._pub_tta.publish(tta)
                        
                    # Compute Autopilot & step own ship physics
                    current_heading_deg = _math_heading_to_nav_heading(self._own_state.psi)
                    current_sog_kn = math.sqrt(self._own_state.u ** 2 + self._own_state.v ** 2) * 1.94384
                    current_rot_deg_s = math.degrees(self._own_state.r)
                    
                    rudder_angle, throttle = self._compute_autopilot(sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s)
                    
                    u_target = throttle * (25.0 * 0.514444)
                    n_rps_cmd = u_target * (self._coeffs.n_rps_cruise / self._coeffs.u0)
                    
                    self._own_state = self._model.rk4_step(self._own_state, rudder_angle, n_rps_cmd)
                    
                    # Convert own ship state to GPS lat/lon
                    own_lat = self._origin_lat_rad + math.radians(_lat_offset(self._own_state.y, self._origin_lat_rad))
                    own_lon = self._origin_lon_rad + math.radians(_lon_offset(self._own_state.x, own_lat))
                    
                    # Publish own ship state
                    foss = FilteredOwnShipState()
                    foss.schema_version = 112
                    foss.stamp = time_msg
                    foss.position.latitude = math.degrees(own_lat)
                    foss.position.longitude = math.degrees(own_lon)
                    foss.position.altitude = 0.0
                    foss.heading_deg = _math_heading_to_nav_heading(self._own_state.psi)
                    foss.sog_kn = math.sqrt(self._own_state.u ** 2 + self._own_state.v ** 2) * 1.94384
                    foss.cog_deg = _ground_track_to_nav_cog(self._own_state.psi, self._own_state.u, self._own_state.v)
                    foss.u_water = self._own_state.u
                    foss.v_water = self._own_state.v
                    foss.r_dot_deg_s = math.degrees(self._own_state.r)
                    for i in range(6):
                        foss.covariance[i * 6 + i] = 1.0
                    foss.nav_mode = "OPTIMAL"
                    foss.confidence = 0.9
                    foss.rationale = "Closed-loop lockstep coordinator"
                    if self._pub_foss is not None:
                        self._pub_foss.publish(foss)
                except Exception as e:
                    self.get_logger().error(f"Error stepping internal physics: {e}")
                    
            # 4. Gating step: send STEP <sec> <nanosec>\n to all clients
            step_cmd = f"STEP {time_msg.sec} {time_msg.nanosec}\n"
            disconnected = False
            for c in self._clients:
                try:
                    c.sendall(step_cmd.encode())
                except Exception as e:
                    self.get_logger().error(f"Failed to send STEP to client: {e}")
                    disconnected = True
                    break
                    
            if disconnected or not self._free_run_active:
                break
                
            # Wait for ACKs
            for c in self._clients:
                ack_buf = b""
                while b"\n" not in ack_buf and self._free_run_active:
                    try:
                        c.settimeout(0.5)
                        chunk = c.recv(1)
                        if not chunk:
                            self.get_logger().error("Client disconnected before sending ACK")
                            disconnected = True
                            break
                        ack_buf += chunk
                    except socket.timeout:
                        continue
                    except Exception as e:
                        self.get_logger().error(f"Error receiving ACK from client: {e}")
                        disconnected = True
                        break
                if disconnected or not self._free_run_active:
                    break
                    
            if disconnected:
                self.get_logger().error("Stepping loop terminated due to client disconnection.")
                break
                
        # Clean up clients
        for c in self._clients:
            try:
                c.close()
            except Exception:
                pass
        self._clients = []
        self.get_logger().info("Free-run background thread loop finished.")

    # ── Timer callbacks (Realtime mode) ───────────────────────────────────

    def _clock_callback(self) -> None:
        """Wall-clock-paced /clock emitter.

        Design (spec §4.1):
          wall_elapsed = now_wall - run_start_wall
          target_sim   = wall_elapsed * sim_rate
          Emit dt_tick steps until sim_time reaches target_sim,
          capped at _MAX_CATCHUP_TICKS per callback.
          Publish one /clock + /sim_clock per dt_tick so that
          sim-time timers fire on every tick (no skipped steps).
        """
        run_start_wall = self._run_start_wall
        if run_start_wall is None:
            return

        sim_rate = self._fsm.sim_rate
        dt_tick = 1.0 / self._fsm._tick_hz
        # Use rate-change anchor so switching from 1x→10x at t=331s doesn't
        # create a 9×331=2979s catch-up deficit.
        target_sim = self._fsm._rate_anchor_sim + (time.time() - self._fsm._rate_anchor_wall) * sim_rate

        sim_clock_pub = self._sim_clock_pub
        clock_pub = self._clock_pub

        emitted = 0
        while self._fsm.sim_time < target_sim and emitted < self._MAX_CATCHUP_TICKS:
            self._fsm.tick()
            sim_t = self._fsm.sim_time

            time_msg = TimeMsg()
            time_msg.sec = int(sim_t)
            time_msg.nanosec = int((sim_t - time_msg.sec) * 1e9)
            if sim_clock_pub is not None:
                sim_clock_pub.publish(time_msg)

            if clock_pub is not None:
                clock_msg = ClockMsg()
                clock_msg.clock = time_msg
                clock_pub.publish(clock_msg)

            emitted += 1

        if emitted == self._MAX_CATCHUP_TICKS and self._fsm.sim_time < target_sim:
            self.get_logger().warn(
                f"[lifecycle_mgr] Clock catchup capped at {self._MAX_CATCHUP_TICKS} ticks; "
                f"sim_time={self._fsm.sim_time:.3f} target={target_sim:.3f}. "
                f"RTF may be < sim_rate temporarily."
            )

    def _status_callback(self) -> None:
        """Publish /sil/lifecycle_status @ status_hz."""
        msg = LifecycleStatus()
        msg.stamp = self.get_clock().now().to_msg()
        msg.current_state = self._fsm.current_state.value
        msg.scenario_id = self._fsm.scenario_id
        msg.scenario_hash = self._fsm.scenario_hash
        msg.sim_time = self._fsm.sim_time
        msg.wall_time = self._fsm.wall_time
        msg.sim_rate = self._fsm.sim_rate
        if self._status_pub is not None:
            self._status_pub.publish(msg)

    def _publish_clock_now(self) -> None:
        """Publish one /clock + /sim_clock reflecting the current _sim_time.

        Called on activate to overwrite the TRANSIENT_LOCAL cache so use_sim_time
        nodes do not read the stale clock from the previous run. Mirrors the
        publish shape used by the free-run loop and _clock_callback.
        """
        sim_t = self._fsm.sim_time
        time_msg = TimeMsg()
        time_msg.sec = int(sim_t)
        time_msg.nanosec = int(round((sim_t - time_msg.sec) * 1e9))
        if self._sim_clock_pub is not None:
            try:
                self._sim_clock_pub.publish(time_msg)
            except Exception as e:
                self.get_logger().error(f"Failed to publish /sim_clock on activate: {e}")
        if self._clock_pub is not None:
            try:
                clock_msg = ClockMsg()
                clock_msg.clock = time_msg
                self._clock_pub.publish(clock_msg)
            except Exception as e:
                self.get_logger().error(f"Failed to publish /clock on activate: {e}")


# ── Entry point ─────────────────────────────────────────────────────────────

def main():
    """ROS2 entry point: spin LifecycleManagerNode."""
    rclpy.init()
    node = LifecycleManagerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
