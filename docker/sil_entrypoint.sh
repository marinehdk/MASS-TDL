#!/bin/bash
set -e
source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash

echo "=== Starting SIL Lifecycle Nodes ==="

python3 -c "
import rclpy
import inspect
from lifecycle_msgs.msg import State
from rclpy.executors import MultiThreadedExecutor

from sil_lifecycle.lifecycle_mgr import LifecycleManagerNode
from ship_dynamics.node import ShipDynamicsNode
from env_disturbance.node import EnvDisturbanceNode
from target_vessel.node import TargetVesselNode
from sensor_mock.node import SensorMockNode
from tracker_mock.node import TrackerMockNode
from fault_injection.node import FaultInjectionNode
from scoring.node import ScoringNode
from scenario_authoring.node import ScenarioAuthoringNode

rclpy.init()

nodes = []
executor = MultiThreadedExecutor(num_threads=4)

# Use each node's own default constructor — don't force node_name
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

print('=== Configuring ===')
for node in nodes:
    try:
        result = node.on_configure(State())
        print(f'  {node.get_name()}: {result}')
    except Exception as e:
        print(f'  {node.get_name()}: ERROR {type(e).__name__}: {e}')

print('=== Activating ===')
for node in nodes:
    try:
        result = node.on_activate(State())
        print(f'  {node.get_name()}: {result}')
    except Exception as e:
        print(f'  {node.get_name()}: ERROR {type(e).__name__}: {e}')

print('=== Active. Spinning MultiThreadedExecutor(4) ===')
executor.spin()
" 2>&1
