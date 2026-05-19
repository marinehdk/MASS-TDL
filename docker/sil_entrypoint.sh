#!/bin/bash
# sil_entrypoint.sh — start 9 ROS2 lifecycle nodes and keep them alive.
#
# NO set -e: lifecycle state-machine errors (invalid transition) must not
# terminate the container. The Python spin loop handles them gracefully.

source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash

echo "=== Starting SIL Lifecycle Nodes ==="

python3 -c "
import sys
import rclpy
from rclpy.executors import MultiThreadedExecutor

from sil_lifecycle.lifecycle_mgr import LifecycleManagerNode
from ship_dynamics.node import ShipDynamicsNode
from env_disturbance.node import EnvDisturbanceNode
from target_vessel.node import TargetVesselNode
from sensor_mock.node import SensorMockNode
from tracker_mock.node import TrackerMockNode
from fault_injection.node import FaultInjectionNode
from scoring.node import ScoringLifecycleNode as ScoringNode
from scenario_authoring.node import ScenarioAuthoringNode

rclpy.init()

nodes = []
executor = MultiThreadedExecutor(num_threads=4)

node_classes = [
    LifecycleManagerNode, ShipDynamicsNode, EnvDisturbanceNode,
    TargetVesselNode, SensorMockNode, TrackerMockNode,
    FaultInjectionNode, ScoringNode, ScenarioAuthoringNode,
]

for cls in node_classes:
    node = cls()
    executor.add_node(node)
    nodes.append(node)
    print(f'  Created {node.get_name()}')

print('=== Nodes created. Waiting for orchestrator lifecycle commands. ===')
sys.stdout.flush()

# Resilient spin loop: lifecycle state-machine errors (e.g. ACTIVATE received
# before CONFIGURE, or duplicate transitions from the broadcast) raise RCLError.
# Catch them per-cycle so the entire process never dies from a bad transition.
try:
    while rclpy.ok():
        try:
            executor.spin_once(timeout_sec=1.0)
        except Exception as exc:
            # Log and continue — do NOT re-raise.
            print(f'[WARN] executor spin_once error (lifecycle transition rejected, continuing): '
                  f'{type(exc).__name__}: {exc}', file=sys.stderr, flush=True)
finally:
    print('=== SIL nodes shutting down ===', flush=True)
    executor.shutdown()
    for node in nodes:
        try:
            node.destroy_node()
        except Exception:
            pass
    rclpy.shutdown()
" 2>&1
