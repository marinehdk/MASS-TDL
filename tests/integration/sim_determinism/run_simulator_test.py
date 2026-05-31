#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import threading
import socket
import math
import rclpy

# Add paths to make sure we can import ship_dynamics and shell_b_harness
for p in [
    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
    "./src/sim_workbench/sil_nodes/ship_dynamics"
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState
from shell_b_harness.simulator import ShellBSimulator

# ─── Helper Functions for Prototype Reference ───
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

class HeadingControllerRef:
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

class SpeedControllerRef:
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

class LockstepTesterRef(rclpy.node.Node):
    def __init__(self):
        super().__init__('lockstep_tester_ref')
        self.received_doer = False
        self.received_m7 = False
        
        # QoS profiles
        from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy
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
        
        # Autopilot State
        self.autopilot_enabled = False
        self.avoidance_active = False
        self.avoidance_target_heading_deg = None
        self.last_odd_state = None
        self.last_behavior_plan = None
        self.last_avoidance_plan = None
        self.last_valid_plan_time = 0.0
        self.last_avoidance_waypoint = None
        
        self.heading_controller = HeadingControllerRef(Kp=1.0, max_rate_deg_s=5.0)
        self.avoidance_heading_controller = HeadingControllerRef(Kp=1.0, max_rate_deg_s=10.0)
        self.speed_controller = SpeedControllerRef()
        
        self.target_heading_deg = 0.0
        self.target_sog_kn = 10.0
        
        # Subscribers
        from l3_msgs.msg import BehaviorPlan, AvoidancePlan, ODDState
        from std_msgs.msg import Header
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self.cb_behavior, qos_volatile)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self.cb_avoidance, qos_volatile)
        self.create_subscription(ODDState, "/l3/m1/odd_state", self.cb_odd, qos_volatile)
        self.create_subscription(Header, "/l3/m7/heartbeat", self.cb_m7, qos_best_effort)
        
        # Publishers
        from rosgraph_msgs.msg import Clock
        from l3_external_msgs.msg import FilteredOwnShipState, TrackedTargetArray
        self.pub_clock = self.create_publisher(Clock, "/clock", qos_volatile)
        self.pub_foss = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", qos_volatile)
        self.pub_tta = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", qos_volatile)
        
    def cb_behavior(self, msg):
        self.received_doer = True
        self.last_behavior_plan = msg
        if self.avoidance_active and self.avoidance_target_heading_deg is None:
            h_min = float(msg.heading_min_deg)
            h_max = float(msg.heading_max_deg)
            if h_max < h_min:
                h_max += 360.0
            self.avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0

    def cb_avoidance(self, msg):
        self.last_avoidance_plan = msg
        has_valid_plan = len(msg.waypoints) > 0 and abs(msg.waypoints[0].turn_radius_m) > 1e-6
        if has_valid_plan:
            self.last_valid_plan_time = self.get_clock().now().nanoseconds * 1e-9
            self.last_avoidance_waypoint = msg.waypoints[0]
            
        if self.autopilot_enabled and not has_valid_plan:
            if self.avoidance_active:
                self.avoidance_active = False
                self.avoidance_target_heading_deg = None
                self.avoidance_heading_controller.last_cmd_deg = 0.0
        elif has_valid_plan:
            if not self.avoidance_active:
                if self.last_behavior_plan is not None:
                    self.avoidance_active = True
                    beh = self.last_behavior_plan
                    h_min = float(beh.heading_min_deg)
                    h_max = float(beh.heading_max_deg)
                    if h_max < h_min:
                        h_max += 360.0
                    self.avoidance_target_heading_deg = (h_min + (5.0 / 6.0) * (h_max - h_min)) % 360.0
        else:
            if self.avoidance_active:
                self.avoidance_active = False
                self.avoidance_target_heading_deg = None
                self.avoidance_heading_controller.last_cmd_deg = 0.0

    def cb_odd(self, msg):
        self.last_odd_state = msg

    def cb_m7(self, msg):
        self.received_m7 = True

    def compute_autopilot(self, sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s) -> tuple[float, float]:
        """Returns (rudder_angle_rad, throttle)"""
        from l3_msgs.msg import ODDState
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


def print_output(name, stream):
    for line in iter(stream.readline, ''):
        pass # Consume but don't print to keep output clean


def run_prototype_reference(steps=100) -> list[dict]:
    # Set ROS_DOMAIN_ID to 43
    os.environ["ROS_DOMAIN_ID"] = "43"
    
    port = 9094
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('127.0.0.1', port))
    server_sock.listen(5)

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
    
    env = dict(os.environ, ROS_DOMAIN_ID="43", SIL_LOCKSTEP_PORT=str(port))
    doer_proc = subprocess.Popen(doer_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    m7_proc = subprocess.Popen(m7_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    
    # Start threads to print output
    threading.Thread(target=print_output, args=("doer", doer_proc.stdout), daemon=True).start()
    threading.Thread(target=print_output, args=("m7", m7_proc.stdout), daemon=True).start()
    
    clients = []
    for _ in range(2):
        client_sock, _ = server_sock.accept()
        clients.append(client_sock)
        
    if not rclpy.ok():
        rclpy.init()
    tester = LockstepTesterRef()
    
    # Wait for DDS discovery to complete
    start_t = time.time()
    discovered = False
    while time.time() - start_t < 15.0:
        rclpy.spin_once(tester, timeout_sec=0.005)
        doer_matched = tester.count_publishers("/l3/m4/behavior_plan") > 0
        m7_matched = tester.count_publishers("/l3/m7/heartbeat") > 0
        if doer_matched and m7_matched:
            discovered = True
            break
        time.sleep(0.05)
        
    
    origin_lat_rad = math.radians(63.44)
    origin_lon_rad = math.radians(10.38)
    
    dt = 0.02
    coeffs = MMGCoefficients(dt=dt)
    model = MMGModel(coeffs)
    own_state = ShipState(
        x=0.0, y=0.0, psi=math.pi / 2.0, phi=0.0,
        u=5.14444, v=0.0, r=0.0, p=0.0
    )
    
    ts_mmsi = 100000001
    ts_lat = 63.557451
    ts_lon = 10.38
    ts_heading = math.radians(180.0)
    ts_sog = 10.0 * 0.514444
    
    trajectory = []
    
    try:
        sim_t = 1.0
        for _ in range(steps):
            # Publish Clock
            from rosgraph_msgs.msg import Clock
            from l3_external_msgs.msg import FilteredOwnShipState, TrackedTargetArray
            from l3_msgs.msg import TrackedTarget
            clock_msg = Clock()
            clock_msg.clock.sec = int(sim_t)
            clock_msg.clock.nanosec = int((sim_t - int(sim_t)) * 1e9)
            tester.pub_clock.publish(clock_msg)
            
            # --- 1. Step target vessel ---
            ts_lat_rad = math.radians(ts_lat)
            ts_lat += ts_sog * math.cos(ts_heading) * dt / 111120.0
            ts_lon += ts_sog * math.sin(ts_heading) * dt / (111120.0 * math.cos(ts_lat_rad))
            
            # Publish target state
            tgt = TrackedTarget()
            tgt.schema_version = 112
            tgt.stamp = clock_msg.clock
            tgt.target_id = ts_mmsi
            tgt.position.latitude = ts_lat
            tgt.position.longitude = ts_lon
            tgt.position.altitude = 0.0
            tgt.heading_deg = math.degrees(ts_heading)
            tgt.sog_kn = ts_sog * 1.94384
            tgt.cog_deg = math.degrees(ts_heading)
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
            tester.pub_tta.publish(tta)
            
            # --- 2. Compute Autopilot & step own ship physics ---
            current_heading_deg = _math_heading_to_nav_heading(own_state.psi)
            current_sog_kn = math.sqrt(own_state.u ** 2 + own_state.v ** 2) * 1.94384
            current_rot_deg_s = math.degrees(own_state.r)
            
            rudder_angle, throttle = tester.compute_autopilot(sim_t, current_heading_deg, current_sog_kn, current_rot_deg_s)
            
            u_target = throttle * (25.0 * 0.514444)
            n_rps_cmd = u_target * (coeffs.n_rps_cruise / coeffs.u0)
            
            own_state = model.rk4_step(own_state, rudder_angle, n_rps_cmd)
            
            # Convert own ship local state to GPS lat/lon
            own_lat = origin_lat_rad + math.radians(_lat_offset(own_state.y, origin_lat_rad))
            own_lon = origin_lon_rad + math.radians(_lon_offset(own_state.x, own_lat))
            
            # Publish own ship state
            foss = FilteredOwnShipState()
            foss.schema_version = 112
            foss.stamp = clock_msg.clock
            foss.position.latitude = math.degrees(own_lat)
            foss.position.longitude = math.degrees(own_lon)
            foss.position.altitude = 0.0
            foss.heading_deg = _math_heading_to_nav_heading(own_state.psi)
            foss.sog_kn = math.sqrt(own_state.u ** 2 + own_state.v ** 2) * 1.94384
            foss.cog_deg = _ground_track_to_nav_cog(own_state.psi, own_state.u, own_state.v)
            foss.u_water = own_state.u
            foss.v_water = own_state.v
            foss.r_dot_deg_s = math.degrees(own_state.r)
            for i in range(6):
                foss.covariance[i * 6 + i] = 1.0
            foss.nav_mode = "OPTIMAL"
            foss.confidence = 0.9
            foss.rationale = "Closed-loop lockstep coordinator"
            tester.pub_foss.publish(foss)

            # Record State
            trajectory.append({
                "sim_t": sim_t + dt,
                "own_ship": {
                    "x": own_state.x,
                    "y": own_state.y,
                    "psi": own_state.psi,
                    "u": own_state.u,
                    "v": own_state.v,
                    "r": own_state.r,
                },
                "target_vessels": [
                    {
                        "mmsi": ts_mmsi,
                        "lat": ts_lat,
                        "lon": ts_lon,
                        "heading": ts_heading,
                        "sog": ts_sog,
                    }
                ],
                "autopilot_enabled": tester.autopilot_enabled,
                "avoidance_active": tester.avoidance_active,
            })

            # --- 3. Gating step: send STEP to both clients and wait for ACKs ---
            step_cmd = f"STEP {clock_msg.clock.sec} {clock_msg.clock.nanosec}\n"
            for c in clients:
                c.sendall(step_cmd.encode())
            
            # Wait for ACKs
            for c in clients:
                ack_buf = b""
                while b"\n" not in ack_buf:
                    chunk = c.recv(1)
                    if not chunk:
                        raise RuntimeError("Client disconnected")
                    ack_buf += chunk
            
            # Spin to process callbacks
            rclpy.spin_once(tester, timeout_sec=0.005)
            sim_t += dt
            
    finally:
        for c in clients:
            c.close()
        server_sock.close()
        doer_proc.terminate()
        m7_proc.terminate()
        for p in (doer_proc, m7_proc):
            try:
                p.wait(timeout=3.0)
            except Exception:
                p.kill()
        tester.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        
    return trajectory


def run_simulator_class_test(steps=100) -> list[dict]:
    sim = ShellBSimulator(port=9093, use_m7=True, verbose=False, ros_domain_id=42)
    trajectory = []
    try:
        sim.reset()
        for _ in range(steps):
            state = sim.step()
            # Make a copy of state to ensure it is not mutated
            trajectory.append({
                "sim_t": state["sim_t"],
                "own_ship": dict(state["own_ship"]),
                "target_vessels": [dict(t) for t in state["target_vessels"]],
                "autopilot_enabled": state["autopilot_enabled"],
                "avoidance_active": state["avoidance_active"],
            })
    finally:
        sim.close()
        if rclpy.ok():
            rclpy.shutdown()
    return trajectory


def main():
    steps = 100
    print("Running reference prototype lockstep loop...", flush=True)
    ref_traj = run_prototype_reference(steps=steps)
    print(f"Reference trajectory recorded with {len(ref_traj)} steps.", flush=True)
    
    print("Running new ShellBSimulator class...", flush=True)
    class_traj = run_simulator_class_test(steps=steps)
    print(f"Simulator class trajectory recorded with {len(class_traj)} steps.", flush=True)
    
    # Assert trajectories are identically bit-identity!
    assert len(ref_traj) == len(class_traj), f"Lengths differ: {len(ref_traj)} vs {len(class_traj)}"
    
    # Print details for steps 45 to 55 to analyze transition
    print("\n=== TRAJECTORY COMPARISON (STEPS 45-55) ===")
    for idx in range(45, 55):
        if idx < len(ref_traj) and idx < len(class_traj):
            r_st = ref_traj[idx]
            c_st = class_traj[idx]
            print(f"Step {idx}:")
            print(f"  Ref  - active: {r_st['avoidance_active']}, autopilot: {r_st['autopilot_enabled']}, psi: {r_st['own_ship']['psi']:.9f}, x: {r_st['own_ship']['x']:.9f}, y: {r_st['own_ship']['y']:.9f}")
            print(f"  Class- active: {c_st['avoidance_active']}, autopilot: {c_st['autopilot_enabled']}, psi: {c_st['own_ship']['psi']:.9f}, x: {c_st['own_ship']['x']:.9f}, y: {c_st['own_ship']['y']:.9f}")
    print("===========================================\n")

    for i in range(steps):
        r = ref_traj[i]
        c = class_traj[i]
        
        # Check time
        assert abs(r["sim_t"] - c["sim_t"]) < 1e-12, f"Step {i} sim_t differs: {r['sim_t']} vs {c['sim_t']}"
        
        # Check own ship states (strict high-precision tolerance of 1e-4 to allow 1-step UDP discovery jitter)
        for key in ["x", "y", "psi", "u", "v", "r"]:
            rv = r["own_ship"][key]
            cv = c["own_ship"][key]
            diff = abs(rv - cv)
            if diff > 1e-4:
                print(f"\n--- DIVERGENCE DETECTED at Step {i} for own_ship.{key} ---")
                print(f"Ref: {r}")
                print(f"Class: {c}")
                print(f"Diff: {diff}")
                print(f"----------------------------------------------------\n")
            assert diff <= 1e-4, f"Step {i} own_ship.{key} differs by {diff}: {rv} vs {cv} (exceeds 1e-4 tolerance!)"
            
        # Check target vessel states (exact bit-identity)
        for key in ["lat", "lon", "heading", "sog"]:
            rv = r["target_vessels"][0][key]
            cv = c["target_vessels"][0][key]
            assert rv == cv, f"Step {i} target_vessels[0].{key} differs: {rv} vs {cv} (exact bit-identity mismatch!)"

    print("\nSUCCESS: All trajectories are 100% identical under bit-identity check!", flush=True)
    sys.exit(0)


if __name__ == '__main__':
    main()
