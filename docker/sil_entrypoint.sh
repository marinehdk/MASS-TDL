#!/bin/bash
set -e
source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash

echo "=== Starting SIL Lifecycle Nodes ==="

python3 -c "
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
executor.spin()
" 2>&1
