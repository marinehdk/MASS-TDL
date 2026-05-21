#!/usr/bin/env python3
import sys
import os
import json
import time
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy

# ROS 2 message types
from sil_msgs.msg import OwnShipState, ModulePulse, ASDREvent
from l3_external_msgs.msg import TrackedTargetArray
from rosidl_runtime_py.convert import message_to_ordereddict

class ASDRLogger(Node):
    def __init__(self, output_path: str):
        super().__init__('asdr_logger')
        self.output_path = output_path
        self.get_logger().info(f"ASDR Logger started, output: {output_path}")

        # Ensure the output directory exists
        out_dir = os.path.dirname(output_path)
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)

        # QoS Profiles
        sq = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=5,
        )
        lq = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=50,
        )

        # Subscriptions
        self.sub_oss = self.create_subscription(
            OwnShipState, '/sil/own_ship_state',
            lambda msg: self.log_msg('/sil/own_ship_state', msg), sq
        )
        self.sub_tta = self.create_subscription(
            TrackedTargetArray, '/sil/tracked_targets',
            lambda msg: self.log_msg('/sil/tracked_targets', msg), sq
        )
        self.sub_pulse = self.create_subscription(
            ModulePulse, '/sil/module_pulse',
            lambda msg: self.log_msg('/sil/module_pulse', msg), sq
        )
        self.sub_asdr = self.create_subscription(
            ASDREvent, '/sil/asdr_event',
            lambda msg: self.log_msg('/sil/asdr_event', msg), lq
        )

    def log_msg(self, topic: str, msg):
        try:
            # Convert message to ordered dict using standard ROS 2 utility
            msg_dict = message_to_ordereddict(msg)
            
            # Construct JSONL record
            record = {
                "timestamp": time.time(),
                "topic": topic,
                "message": msg_dict
            }
            
            # Append to file
            with open(self.output_path, 'a') as f:
                f.write(json.dumps(record) + '\n')
        except Exception as e:
            self.get_logger().error(f"Error logging message on {topic}: {e}")

def main():
    if len(sys.argv) < 2:
        print("Usage: asdr_logger.py <output_path>")
        sys.exit(1)

    output_path = sys.argv[1]
    
    # Clean file if exists
    if os.path.exists(output_path):
        try:
            os.remove(output_path)
        except OSError:
            pass

    rclpy.init()
    node = ASDRLogger(output_path)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.get_logger().info("ASDR Logger shutting down")
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()
