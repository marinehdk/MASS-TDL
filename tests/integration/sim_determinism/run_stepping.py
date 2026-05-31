#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import threading

# Set ROS_DOMAIN_ID to 42 before importing rclpy to isolate from background traffic
os.environ["ROS_DOMAIN_ID"] = "42"

import rclpy
from rclpy.node import Node
from rosgraph_msgs.msg import Clock
from l3_external_msgs.msg import FilteredOwnShipState
from l3_msgs.msg import ODDState, WorldState, MissionGoal, BehaviorPlan, AvoidancePlan, COLREGsConstraint, UIState

class SteppingTester(Node):
    def __init__(self):
        super().__init__('stepping_tester')
        self.received = {
            'm1_odd_state': False,
            'm2_world_state': False,
            'm3_mission_goal': False,
            'm4_behavior_plan': False,
            'm5_avoidance_plan': False,
            'm6_colregs_constraint': False,
            'm8_ui_state': False
        }
        
        # QoS setup
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        qos_transient_local = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
            depth=10
        )
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
        self.create_subscription(ODDState, "/l3/m1/odd_state", self.cb_m1, qos_transient_local)
        self.create_subscription(WorldState, "/l3/m2/world_state", self.cb_m2, qos_volatile)
        self.create_subscription(MissionGoal, "/l3/m3/mission_goal", self.cb_m3, qos_volatile)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self.cb_m4, qos_volatile)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self.cb_m5, qos_volatile)
        self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint", self.cb_m6, qos_volatile)
        self.create_subscription(UIState, "/l3/m8/ui_state", self.cb_m8, qos_best_effort)
        
        # Publishers
        self.pub_clock = self.create_publisher(Clock, "/clock", qos_volatile)
        self.pub_own_ship = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", qos_volatile)
        
    def cb_m1(self, msg):
        self.get_logger().info("Received M1 ODDState")
        self.received['m1_odd_state'] = True
    def cb_m2(self, msg):
        self.get_logger().info("Received M2 WorldState")
        self.received['m2_world_state'] = True
    def cb_m3(self, msg):
        self.get_logger().info("Received M3 MissionGoal")
        self.received['m3_mission_goal'] = True
    def cb_m4(self, msg):
        self.get_logger().info("Received M4 BehaviorPlan")
        self.received['m4_behavior_plan'] = True
    def cb_m5(self, msg):
        self.get_logger().info("Received M5 AvoidancePlan")
        self.received['m5_avoidance_plan'] = True
    def cb_m6(self, msg):
        self.get_logger().info("Received M6 COLREGsConstraint")
        self.received['m6_colregs_constraint'] = True
    def cb_m8(self, msg):
        self.get_logger().info("Received M8 UIState")
        self.received['m8_ui_state'] = True

def print_output(stream):
    for line in iter(stream.readline, ''):
        print(f"[composition] {line.strip()}", flush=True)

def main():
    cmd = [
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
    
    env = dict(os.environ, ROS_DOMAIN_ID="42")
    
    print("Launching doer_composition...")
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    
    # Start thread to print output
    t = threading.Thread(target=print_output, args=(proc.stdout,), daemon=True)
    t.start()
    
    # Wait for the composition binary to start and print initialization messages
    time.sleep(3.0)
    
    # Check if process is still running
    ret = proc.poll()
    if ret is not None:
        print(f"doer_composition exited immediately with code {ret}", file=sys.stderr)
        sys.exit(1)
        
    rclpy.init()
    tester = SteppingTester()
    
    try:
        # Check process lists to validate that subprocess PID is the only process running the nodes
        import subprocess as sp
        pgrep_res = sp.run(["pgrep", "-f", "doer_composition"], capture_output=True, text=True)
        pids = pgrep_res.stdout.strip().split()
        print(f"Running doer_composition PIDs: {pids}")
        assert len(pids) == 1, f"Expected exactly 1 doer_composition process, found {pids}"
        assert int(pids[0]) == proc.pid, f"Launched PID {proc.pid} does not match running PID {pids[0]}"
        
        sim_t = 1.0
        end_t = 20.0
        dt = 0.05
        
        print("Starting stepping loop...")
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
            
            # Spin to process callbacks
            rclpy.spin_once(tester, timeout_sec=0.01)
            
            # Print state of received messages
            if sim_t % 1.0 < 0.01:
                print(f"Time: {sim_t:.2f}s | Received states: {tester.received}")
                
            # If all 7 topics are received, we are done
            if all(tester.received.values()):
                print("All 7 topics received successfully!")
                break
                
            sim_t += dt
            time.sleep(0.02)
            
        print(f"Final received states: {tester.received}")
        for topic, rec in tester.received.items():
            assert rec, f"Failed to receive messages on topic {topic}"
            
        print("All assertions passed. Stepping test successful!")
        
    finally:
        print("Terminating doer_composition subprocess...")
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            print("Force-killing subprocess...")
            proc.kill()
            proc.wait()
        print("Subprocess cleaned up.")
        rclpy.shutdown()

if __name__ == '__main__':
    main()
