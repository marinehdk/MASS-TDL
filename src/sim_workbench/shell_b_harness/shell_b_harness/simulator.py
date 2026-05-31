from __future__ import annotations
import os
import sys
import time
import subprocess
import threading
import socket
import math
import numpy as np

# Try to insert paths to standard dependencies inside the workspace
for p in [
    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
    "./src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/src/sim_workbench/sil_common",
    "./src/sim_workbench/sil_common"
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

# Standard imports that might be environment-dependent
try:
    from ship_dynamics.mmg_coefficients import MMGCoefficients
    from ship_dynamics.mmg_model import MMGModel, ShipState
except ImportError:
    # If not found directly, try adding sibling directory structures
    root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ship_dyn_path = os.path.join(root_dir, "src/sim_workbench/sil_nodes/ship_dynamics")
    if ship_dyn_path not in sys.path:
        sys.path.insert(0, ship_dyn_path)
    from ship_dynamics.mmg_coefficients import MMGCoefficients
    from ship_dynamics.mmg_model import MMGModel, ShipState

try:
    from sil_common.det_rng import make_rng, register_node
except ImportError:
    root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    sil_common_path = os.path.join(root_dir, "src/sim_workbench/sil_common")
    if sil_common_path not in sys.path:
        sys.path.insert(0, sil_common_path)
    from sil_common.det_rng import make_rng, register_node

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy
from rosgraph_msgs.msg import Clock
from l3_external_msgs.msg import FilteredOwnShipState, TrackedTargetArray
from l3_msgs.msg import BehaviorPlan, AvoidancePlan, ODDState, TrackedTarget
from std_msgs.msg import Header

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

class LockstepNode(Node):
    def __init__(self, simulator: ShellBSimulator):
        super().__init__('lockstep_simulator')
        self.simulator = simulator
        
        # QoS profiles
        qos_volatile = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10
        )
        qos_best_effort = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10
        )
        
        # Subscribers
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self.cb_behavior, qos_volatile)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self.cb_avoidance, qos_volatile)
        self.create_subscription(ODDState, "/l3/m1/odd_state", self.cb_odd, qos_volatile)
        self.create_subscription(Header, "/l3/m7/heartbeat", self.cb_m7, qos_best_effort)
        
        # Publishers
        self.pub_clock = self.create_publisher(Clock, "/clock", qos_volatile)
        self.pub_foss = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", qos_volatile)
        self.pub_tta = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", qos_volatile)

    def cb_behavior(self, msg):
        self.simulator.received_doer = True
        self.simulator.last_behavior_plan = msg
        if self.simulator.avoidance_active and self.simulator.avoidance_target_heading_deg is None:
            h_min = float(msg.heading_min_deg)
            h_max = float(msg.heading_max_deg)
            if h_max < h_min:
                h_max += 360.0
            self.simulator.avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0

    def cb_avoidance(self, msg):
        self.simulator.last_avoidance_plan = msg
        has_valid_plan = len(msg.waypoints) > 0 and abs(msg.waypoints[0].turn_radius_m) > 1e-6
        if has_valid_plan:
            self.simulator.last_valid_plan_time = self.get_clock().now().nanoseconds * 1e-9
            self.simulator.last_avoidance_waypoint = msg.waypoints[0]
            
        if self.simulator.autopilot_enabled and not has_valid_plan:
            if self.simulator.avoidance_active:
                self.simulator.avoidance_active = False
                self.simulator.avoidance_target_heading_deg = None
                self.simulator.avoidance_heading_controller.last_cmd_deg = 0.0
        elif has_valid_plan:
            if not self.simulator.avoidance_active:
                if self.simulator.last_behavior_plan is not None:
                    self.simulator.avoidance_active = True
                    beh = self.simulator.last_behavior_plan
                    h_min = float(beh.heading_min_deg)
                    h_max = float(beh.heading_max_deg)
                    if h_max < h_min:
                        h_max += 360.0
                    self.simulator.avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0
        else:
            if self.simulator.avoidance_active:
                self.simulator.avoidance_active = False
                self.simulator.avoidance_target_heading_deg = None
                self.simulator.avoidance_heading_controller.last_cmd_deg = 0.0

    def cb_odd(self, msg):
        self.simulator.last_odd_state = msg

    def cb_m7(self, msg):
        self.simulator.received_m7 = True

class ShellBSimulator:
    def __init__(self, port: int = 9091, use_m7: bool = True, verbose: bool = False, ros_domain_id: int = 42, headless: bool = True):
        self.port = port
        self.use_m7 = use_m7
        self.verbose = verbose
        self.ros_domain_id = ros_domain_id
        self.headless = headless
        
        # Subprocesses references
        self.doer_proc = None
        self.m7_proc = None
        
        # TCP socket
        self.server_sock = None
        self.clients = []
        
        # ROS 2 node
        self.node = None
        
        # Sim State variables
        self.sim_t = 0.0
        self.dt = 0.02
        self.own_state = None
        self.ts_lat = 0.0
        self.ts_lon = 0.0
        self.ts_heading = 0.0
        self.ts_sog = 0.0
        self.ts_mmsi = 100000001
        
        # Origin for GPS projection
        self.origin_lat_rad = math.radians(63.44)
        self.origin_lon_rad = math.radians(10.38)
        
        # Auto-pilot and control state variables
        self.received_doer = False
        self.received_m7 = False
        self.autopilot_enabled = False
        self.avoidance_active = False
        self.avoidance_target_heading_deg = None
        self.last_odd_state = None
        self.last_behavior_plan = None
        self.last_avoidance_plan = None
        self.last_valid_plan_time = 0.0
        self.last_avoidance_waypoint = None
        
        self.heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=5.0)
        self.avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=10.0)
        self.speed_controller = SpeedController()
        
        self.target_heading_deg = 0.0
        self.target_sog_kn = 10.0
        
        # RNG states
        self.own_ship_rng = None
        self.target_rng = None

    def _read_line(self, sock: socket.socket) -> str:
        buf = b""
        while b"\n" not in buf:
            chunk = sock.recv(1)
            if not chunk:
                raise RuntimeError("Socket disconnected while waiting for newline")
            buf += chunk
        return buf.decode()

    def _wait_for_discovery(self, timeout: float = 15.0) -> bool:
        start_t = time.time()
        discovered = False
        while time.time() - start_t < timeout:
            # Spin the node slightly to process discovery events
            rclpy.spin_once(self.node, timeout_sec=0.005)
            
            doer_matched = self.node.count_publishers("/l3/m4/behavior_plan") > 0
            m7_matched = not self.use_m7 or self.node.count_publishers("/l3/m7/heartbeat") > 0
            
            if doer_matched and m7_matched:
                discovered = True
                break
            time.sleep(0.01)
            
        if not discovered and self.verbose:
            print(f"Warning: DDS discovery timed out. doer_matched={doer_matched}, m7_matched={m7_matched}", flush=True)
        return discovered

    def _reset_local_state(self, seed: int | None = None):
        # 1. Reset local simulator states
        self.sim_t = 1.0
        self.dt = 0.02
        self.coeffs = MMGCoefficients(dt=self.dt)
        self.model = MMGModel(self.coeffs)
        self.own_state = ShipState(
            x=0.0, y=0.0, psi=math.pi / 2.0, phi=0.0,
            u=5.14444, v=0.0, r=0.0, p=0.0
        )
        
        # Target vessel (TS1)
        self.ts_mmsi = 100000001
        self.ts_lat = 63.557451
        self.ts_lon = 10.38
        self.ts_heading = math.radians(180.0)  # heading South
        self.ts_sog = 10.0 * 0.514444  # 10 kn -> m/s

        # RNG Integration
        if seed is not None:
            self.own_ship_rng = make_rng(root=seed, episode=0, node="ship_dynamics", worker=0)
            self.target_rng = make_rng(root=seed, episode=0, node="target_vessel", worker=1)
        else:
            self.own_ship_rng = None
            self.target_rng = None

        if self.own_ship_rng is not None:
            # Perturb own ship initial psi by up to +/- 5 degrees
            self.own_state.psi += self.own_ship_rng.uniform(-math.radians(5.0), math.radians(5.0))
            # Perturb own ship initial speed u by up to +/- 0.5 m/s
            self.own_state.u += self.own_ship_rng.uniform(-0.5, 0.5)

        if self.target_rng is not None:
            # Perturb target vessel starting lat/lon slightly
            self.ts_lat += self.target_rng.uniform(-0.0005, 0.0005)
            self.ts_lon += self.target_rng.uniform(-0.0005, 0.0005)
            self.ts_heading += self.target_rng.uniform(-math.radians(5.0), math.radians(5.0))
        
        # Reset autopilot and control state variables
        self.received_doer = False
        self.received_m7 = False
        self.autopilot_enabled = False
        self.avoidance_active = False
        self.avoidance_target_heading_deg = None
        self.last_odd_state = None
        self.last_behavior_plan = None
        self.last_avoidance_plan = None
        self.last_valid_plan_time = 0.0
        self.last_avoidance_waypoint = None
        
        self.heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=5.0)
        self.avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=10.0)
        self.speed_controller = SpeedController()
        
        self.target_heading_deg = 0.0
        self.target_sog_kn = 10.0

    def reset(self, seed: int | None = None, in_place: bool = True):
        """Starts processes, connects lockstep, resets physics and target states using the seeded RNG."""
        if in_place and self.doer_proc is not None and self.doer_proc.poll() is None:
            # Reset local simulator states and RNG
            self._reset_local_state(seed)
                
            # Send RESET <seed> 0\n to all sockets in self.clients
            seed_val = seed if seed is not None else 0
            reset_cmd = f"RESET {seed_val} 0\n"
            for c in self.clients:
                c.sendall(reset_cmd.encode())
                
            # Read and wait for ACK_RESET from all clients
            for c in self.clients:
                ack_buf = self._read_line(c)
                if "ACK_RESET" not in ack_buf:
                    raise RuntimeError(f"Expected ACK_RESET, got: {ack_buf}")
                
            # Wait for DDS discovery to complete with a shorter timeout (re-connecting in-place)
            self._wait_for_discovery(timeout=3.0)
            
            return self.get_state()

        # 1. Terminate any running processes/connections cleanly
        self.close()
        
        # 2. Reset simulator states and RNG
        self._reset_local_state(seed)
            
        # Setup ROS 2 node and rclpy
        # Set ROS_DOMAIN_ID to self.ros_domain_id to isolate from background traffic
        os.environ["ROS_DOMAIN_ID"] = str(self.ros_domain_id)
        if not rclpy.ok():
            rclpy.init()
        self.node = LockstepNode(self)
        
        # 4. Start TCP Server socket
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind(('127.0.0.1', self.port))
        self.server_sock.listen(5)
        
        # 5. Launch subprocesses
        doer_cmd = [
            "/opt/ws/install/shell_b_harness/lib/shell_b_harness/doer_composition", "--ros-args",
            "--params-file", "/opt/ws/src/l3_tdl_kernel/launch/l3_params.yaml",
            "-p", "m1_odd_manager:yaml_path:=/opt/ws/install/m1_odd_envelope_manager/share/m1_odd_envelope_manager/config/m1_params.yaml",
            "-p", "m4_behavior_arbiter:config_dir:=/opt/ws/install/m4_behavior_arbiter/share/m4_behavior_arbiter/config",
            "-p", "m6_colregs_reasoner:config_dir:=/opt/ws/install/m6_colregs_reasoner/share/m6_colregs_reasoner/config",
            "-r", "m1_odd_envelope_manager:__node:=m1_odd_manager",
            "-r", "world_model_node:__node:=m2_world_model",
            "-r", "behavior_arbiter:__node:=m4_behavior_arbiter",
            "-r", "m5_mid_mpc_node:__node:=m5_tactical_planner",
            "-r", "m8_hmi_transparency_bridge:__node:=m8_hmi_bridge",
            "-r", "/m5/avoidance_plan:=/l3/m5/avoidance_plan",
            "-r", "/m5/sat_data:=/l3/sat/data",
            "-r", "/m5/asdr_record:=/l3/asdr/record",
            "-p", "use_sim_time:=True"
        ]
        
        m7_cmd = [
            "/opt/ws/install/m7_safety_supervisor/lib/m7_safety_supervisor/m7_safety_supervisor",
            "--ros-args",
            "-p", "use_sim_time:=True"
        ]
        
        env = dict(os.environ, ROS_DOMAIN_ID=str(self.ros_domain_id), SIL_LOCKSTEP_PORT=str(self.port))
        if self.headless:
            env["SIL_HEADLESS"] = "1"
        
        self.doer_proc = subprocess.Popen(doer_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if self.use_m7:
            self.m7_proc = subprocess.Popen(m7_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        else:
            self.m7_proc = None
            
        # Start background threads to log outputs
        self.doer_thread = threading.Thread(target=self._log_output, args=("doer", self.doer_proc.stdout), daemon=True)
        self.doer_thread.start()
        if self.m7_proc:
            self.m7_thread = threading.Thread(target=self._log_output, args=("m7", self.m7_proc.stdout), daemon=True)
            self.m7_thread.start()
            
        # 6. Wait for clients to connect
        self.clients = []
        num_clients = 2 if self.use_m7 else 1
        for _ in range(num_clients):
            client_sock, client_addr = self.server_sock.accept()
            self.clients.append(client_sock)
            
        # 7. Check if processes are running
        if self.doer_proc.poll() is not None:
            raise RuntimeError("doer_composition exited immediately on reset")
        if self.use_m7 and self.m7_proc.poll() is not None:
            raise RuntimeError("m7_safety_supervisor exited immediately on reset")
            
        # 8. Wait for DDS discovery to complete
        self._wait_for_discovery(timeout=15.0)
        
        return self.get_state()

    def _log_output(self, name, stream):
        try:
            for line in iter(stream.readline, ''):
                if self.verbose:
                    print(f"[{name}] {line.strip()}", flush=True)
        except Exception:
            pass

    def step(self):
        """Advances simulation by one dt step (dt=0.02s)."""
        if self.node is None:
            raise RuntimeError("Simulator is not reset. Call reset() first.")
            
        # 1. Publish Clock
        clock_msg = Clock()
        clock_msg.clock.sec = int(self.sim_t)
        clock_msg.clock.nanosec = int(round((self.sim_t - int(self.sim_t)) * 1e9))
        self.node.pub_clock.publish(clock_msg)
        
        # 2. Step target vessel (using linear motion)
        ts_lat_rad = math.radians(self.ts_lat)
        self.ts_lat += self.ts_sog * math.cos(self.ts_heading) * self.dt / 111120.0
        self.ts_lon += self.ts_sog * math.sin(self.ts_heading) * self.dt / (111120.0 * math.cos(ts_lat_rad))
        
        # Publish target state
        tgt = TrackedTarget()
        tgt.schema_version = 112
        tgt.stamp = clock_msg.clock
        tgt.target_id = self.ts_mmsi
        tgt.position.latitude = self.ts_lat
        tgt.position.longitude = self.ts_lon
        tgt.position.altitude = 0.0
        tgt.heading_deg = math.degrees(self.ts_heading)
        tgt.sog_kn = self.ts_sog * 1.94384
        tgt.cog_deg = math.degrees(self.ts_heading)
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
        tta.stamp = clock_msg.clock
        tta.targets = [tgt]
        tta.confidence = 0.85
        tta.rationale = "Closed-loop lockstep coordinator"
        self.node.pub_tta.publish(tta)
        
        # 3. Compute Autopilot & step own ship physics
        current_heading_deg = _math_heading_to_nav_heading(self.own_state.psi)
        current_sog_kn = math.sqrt(self.own_state.u ** 2 + self.own_state.v ** 2) * 1.94384
        current_rot_deg_s = math.degrees(self.own_state.r)
        
        rudder_angle, throttle = self.compute_autopilot(self.sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s)
        
        u_target = throttle * (25.0 * 0.514444)
        n_rps_cmd = u_target * (self.coeffs.n_rps_cruise / self.coeffs.u0)
        
        self.own_state = self.model.rk4_step(self.own_state, rudder_angle, n_rps_cmd)
        
        # Convert own ship local state to GPS lat/lon
        own_lat = self.origin_lat_rad + math.radians(_lat_offset(self.own_state.y, self.origin_lat_rad))
        own_lon = self.origin_lon_rad + math.radians(_lon_offset(self.own_state.x, own_lat))
        
        # Publish own ship state
        foss = FilteredOwnShipState()
        foss.schema_version = 112
        foss.stamp = clock_msg.clock
        foss.position.latitude = math.degrees(own_lat)
        foss.position.longitude = math.degrees(own_lon)
        foss.position.altitude = 0.0
        foss.heading_deg = _math_heading_to_nav_heading(self.own_state.psi)
        foss.sog_kn = math.sqrt(self.own_state.u ** 2 + self.own_state.v ** 2) * 1.94384
        foss.cog_deg = _ground_track_to_nav_cog(self.own_state.psi, self.own_state.u, self.own_state.v)
        foss.u_water = self.own_state.u
        foss.v_water = self.own_state.v
        foss.r_dot_deg_s = math.degrees(self.own_state.r)
        for i in range(6):
            foss.covariance[i * 6 + i] = 1.0
        foss.nav_mode = "OPTIMAL"
        foss.confidence = 0.9
        foss.rationale = "Closed-loop lockstep coordinator"
        self.node.pub_foss.publish(foss)

        # 4. Gating step: send STEP to both clients and wait for ACKs
        step_cmd = f"STEP {clock_msg.clock.sec} {clock_msg.clock.nanosec}\n"
        for c in self.clients:
            c.sendall(step_cmd.encode())
        
        # Wait for ACKs
        for c in self.clients:
            ack_buf = self._read_line(c)
        
        # Spin to process callbacks
        rclpy.spin_once(self.node, timeout_sec=0.005)
        
        self.sim_t += self.dt
        
        return self.get_state()

    def compute_autopilot(self, sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s) -> tuple[float, float]:
        """Returns (rudder_angle_rad, throttle)"""
        RUDDER_SIGN = -1
        CRUISE_SPEED_KN = 10.0
        MAX_SPEED_KN = 25.0
        
        # Decide if Autopilot is enabled
        if self.last_odd_state is not None:
            env_state = self.last_odd_state.envelope_state
            env_allows_autopilot = env_state in (ODDState.ENVELOPE_IN, ODDState.ENVELOPE_EDGE, ODDState.ENVELOPE_MRC_PREP)
        else:
            env_allows_autopilot = True
            
        is_m5_stale = (sim_t - self.last_valid_plan_time) > 10.0
        m4_in_fallback = self.last_behavior_plan is not None and "fallback" in self.last_behavior_plan.rationale.lower()
        
        self.autopilot_enabled = env_allows_autopilot and (is_m5_stale or m4_in_fallback)

        if self.avoidance_active:
            if self.avoidance_target_heading_deg is not None:
                heading_error_deg = (self.avoidance_target_heading_deg - current_heading_deg + 180.0) % 360.0 - 180.0
                dt = 0.02
                rudder = RUDDER_SIGN * self.avoidance_heading_controller.step(heading_error_deg, dt, current_rot_deg_s)
            elif self.last_avoidance_waypoint is not None:
                wp = self.last_avoidance_waypoint
                if abs(wp.turn_radius_m) > 1e-6:
                    radius = max(abs(wp.turn_radius_m), 50.0)
                    rudder_rad = math.atan2(46.0, radius)
                    rudder = RUDDER_SIGN * max(-math.radians(35.0), min(math.radians(35.0), rudder_rad))
                else:
                    rudder = 0.0
            else:
                rudder = 0.0
                
            if self.last_avoidance_waypoint is not None:
                throttle = max(0.0, min(1.0, self.last_avoidance_waypoint.target_speed_kn / MAX_SPEED_KN))
            else:
                throttle = CRUISE_SPEED_KN / MAX_SPEED_KN
            return rudder, throttle

        if self.autopilot_enabled:
            heading_error_deg = (self.target_heading_deg - current_heading_deg + 180.0) % 360.0 - 180.0
            speed_error_kn = self.target_sog_kn - current_sog_kn
            dt = 0.02
            rudder = RUDDER_SIGN * self.heading_controller.step(heading_error_deg, dt, current_rot_deg_s)
            throttle = self.speed_controller.step(speed_error_kn, dt)
            return rudder, throttle

        return 0.0, CRUISE_SPEED_KN / MAX_SPEED_KN

    def close(self):
        """Terminates subprocesses cleanly and shuts down connections."""
        # 1. Close sockets
        if self.clients:
            for c in self.clients:
                try:
                    c.close()
                except Exception:
                    pass
            self.clients = []
            
        if self.server_sock:
            try:
                self.server_sock.close()
            except Exception:
                pass
            self.server_sock = None
            
        # 2. Terminate subprocesses
        for proc, name in [(self.doer_proc, "doer"), (self.m7_proc, "m7")]:
            if proc is not None:
                try:
                    proc.terminate()
                    try:
                        proc.wait(timeout=2.0)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
                except Exception:
                    pass
        self.doer_proc = None
        self.m7_proc = None
        
        # 3. Destroy ROS 2 node
        if self.node is not None:
            try:
                self.node.destroy_node()
            except Exception:
                pass
            self.node = None

    def get_state(self) -> dict:
        return {
            "sim_t": self.sim_t,
            "own_ship": {
                "x": self.own_state.x if self.own_state else 0.0,
                "y": self.own_state.y if self.own_state else 0.0,
                "psi": self.own_state.psi if self.own_state else 0.0,
                "u": self.own_state.u if self.own_state else 0.0,
                "v": self.own_state.v if self.own_state else 0.0,
                "r": self.own_state.r if self.own_state else 0.0,
            },
            "target_vessels": [
                {
                    "mmsi": self.ts_mmsi,
                    "lat": self.ts_lat,
                    "lon": self.ts_lon,
                    "heading": self.ts_heading,
                    "sog": self.ts_sog,
                }
            ],
            "autopilot_enabled": self.autopilot_enabled,
            "avoidance_active": self.avoidance_active,
        }
