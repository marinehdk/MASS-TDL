#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import threading
import socket

# Set ROS_DOMAIN_ID to 42 before importing rclpy to isolate from background traffic
os.environ["ROS_DOMAIN_ID"] = "42"

import rclpy
from rclpy.node import Node
from rosgraph_msgs.msg import Clock
from l3_external_msgs.msg import FilteredOwnShipState
from l3_msgs.msg import BehaviorPlan
from std_msgs.msg import Header

class LockstepTester(Node):
    def __init__(self):
        super().__init__('lockstep_tester')
        self.received_doer = False
        self.received_m7 = False
        
        # QoS setup
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        qos_volatile = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            depth=10
        )
        qos_best_effort = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            depth=10
        )
        
        # Subscribers
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self.cb_doer, qos_volatile)
        self.create_subscription(Header, "/l3/m7/heartbeat", self.cb_m7, qos_best_effort)
        
        # Publishers
        self.pub_clock = self.create_publisher(Clock, "/clock", qos_volatile)
        self.pub_own_ship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", qos_volatile)
        
    def cb_doer(self, msg):
        self.received_doer = True
        
    def cb_m7(self, msg):
        self.received_m7 = True

def print_output(name, stream):
    for line in iter(stream.readline, ''):
        print(f"[{name}] {line.strip()}", flush=True)

def main():
    port = 9091
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('127.0.0.1', port))
    server_sock.listen(5)
    print(f"Lockstep coordinator server listening on 127.0.0.1:{port}", flush=True)

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
    
    env = dict(os.environ, ROS_DOMAIN_ID="42", SIL_LOCKSTEP_PORT=str(port))
    
    print("Launching doer_composition and m7_safety_supervisor...", flush=True)
    doer_proc = subprocess.Popen(doer_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    m7_proc = subprocess.Popen(m7_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    
    # Start threads to print output
    threading.Thread(target=print_output, args=("doer", doer_proc.stdout), daemon=True).start()
    threading.Thread(target=print_output, args=("m7", m7_proc.stdout), daemon=True).start()
    
    # Wait for both clients to connect
    print("Waiting for lockstep clients to connect...", flush=True)
    clients = []
    for _ in range(2):
        client_sock, client_addr = server_sock.accept()
        print(f"Lockstep client connected from {client_addr}", flush=True)
        clients.append(client_sock)
        
    # Check if processes are still running
    if doer_proc.poll() is not None:
        print("doer_composition exited immediately", file=sys.stderr, flush=True)
        sys.exit(1)
    if m7_proc.poll() is not None:
        print("m7_safety_supervisor exited immediately", file=sys.stderr, flush=True)
        sys.exit(1)
        
    rclpy.init()
    tester = LockstepTester()
    
    try:
        # Check process lists to validate that subprocess PIDs are distinct
        assert doer_proc.pid != m7_proc.pid, "Expected distinct PIDs for DOER and CHECKER (M7)!"
        print(f"PIDs verified: DOER={doer_proc.pid}, M7 (Checker)={m7_proc.pid}", flush=True)
        
        sim_t = 1.0
        end_t = 20.0
        dt = 0.05
        
        print("Starting lockstep stepping loop...", flush=True)
        while sim_t <= end_t:
            # Publish Clock
            clock_msg = Clock()
            clock_msg.clock.sec = int(sim_t)
            clock_msg.clock.nanosec = int((sim_t - int(sim_t)) * 1e9)
            tester.pub_clock.publish(clock_msg)
            
            # Publish FilteredOwnShipState
            state_msg = FilteredOwnShipState()
            state_msg.schema_version = 112
            state_msg.stamp = clock_msg.clock
            state_msg.position.latitude = 30.0
            state_msg.position.longitude = 122.0
            state_msg.sog_kn = 10.0
            state_msg.cog_deg = 0.0
            state_msg.heading_deg = 0.0
            state_msg.u_water = 5.14
            state_msg.v_water = 0.0
            state_msg.nav_mode = "OPTIMAL"
            state_msg.confidence = 1.0
            tester.pub_own_ship.publish(state_msg)
            
            # Gating step: send STEP to both clients and wait for ACKs
            step_cmd = f"STEP {clock_msg.clock.sec} {clock_msg.clock.nanosec}\n"
            for c in clients:
                c.sendall(step_cmd.encode())
            
            # Wait for ACKs
            for c in clients:
                ack_buf = b""
                while b"\n" not in ack_buf:
                    chunk = c.recv(1)
                    if not chunk:
                        raise RuntimeError("Client disconnected before sending ACK")
                    ack_buf += chunk
            
            # Spin to process callbacks
            rclpy.spin_once(tester, timeout_sec=0.01)
            
            if sim_t % 1.0 < 0.01:
                print(f"Time: {sim_t:.2f}s | DOER received: {tester.received_doer} | M7 received: {tester.received_m7}", flush=True)
                
            sim_t += dt
            
        print(f"Final received states: DOER={tester.received_doer}, M7={tester.received_m7}", flush=True)
        assert tester.received_doer, "Failed to receive DOER behavior plan!"
        assert tester.received_m7, "Failed to receive M7 heartbeat!"
        
        print("All assertions passed. Lockstep barrier integration test successful!", flush=True)
        
    finally:
        for c in clients:
            c.close()
        server_sock.close()
        
        print("Terminating subprocesses...", flush=True)
        doer_proc.terminate()
        m7_proc.terminate()
        for p in (doer_proc, m7_proc):
            try:
                p.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        print("Subprocesses cleaned up.", flush=True)
        rclpy.shutdown()

if __name__ == '__main__':
    main()
