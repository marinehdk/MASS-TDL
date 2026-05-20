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

# === L3 Kernel Bridge & Nodes (DEMO-1 integration) ===
import os
if os.environ.get('SIL_L3_ENABLE', '1') == '1':
    print('=== Starting L3 Kernel Bridge & Nodes ===')
    sys.stdout.flush()

    # Start topic bridge in background process
    import subprocess
    bridge_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/sil_topic_bridge.py'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  Bridge PID: {bridge_proc.pid}')

    # Import and create L3 kernel nodes
    try:
        from m1_odd_envelope_manager.odd_envelope_manager_node import OddEnvelopeManagerNode
        from m2_world_model.world_model_node import WorldModelNode
        from m3_mission_manager.mission_manager_node import MissionManagerNode
        from m4_behavior_arbiter.behavior_arbiter_node import BehaviorArbiterNode
        from m6_colregs_reasoner.colregs_reasoner_node import ColregsReasonerNode
        from m5_tactical_planner.mid_mpc.mid_mpc_node import MidMpcNode
        from m8_hmi_transparency_bridge.hmi_transparency_bridge_node import HmiTransparencyBridgeNode
        from m7_safety_supervisor.safety_supervisor_node import SafetySupervisorNode

        l3_node_classes = [
            (OddEnvelopeManagerNode, 'm1_odd_manager'),
            (WorldModelNode, 'm2_world_model'),
            (MissionManagerNode, 'm3_mission_manager'),
            (BehaviorArbiterNode, 'm4_behavior_arbiter'),
            (ColregsReasonerNode, 'm6_colregs_reasoner'),
            (MidMpcNode, 'm5_tactical_planner'),
            (HmiTransparencyBridgeNode, 'm8_hmi_bridge'),
            (SafetySupervisorNode, 'm7_safety_supervisor'),
        ]

        for node_cls, name in l3_node_classes:
            try:
                node = node_cls()
                executor.add_node(node)
                nodes.append(node)
                print(f'  Created L3 {node.get_name()}')
            except Exception as exc:
                print(f'  [WARN] Failed to create {name}: {exc}', file=sys.stderr)

        print('=== L3 Kernel: nodes active ===')
    except ImportError as exc:
        print(f'  [WARN] L3 kernel import failed: {exc}', file=sys.stderr)
        print('  [WARN] L3 nodes not started — sim-only mode', file=sys.stderr)
else:
    print('=== SIL_L3_ENABLE=0 — L3 kernel bypassed ===')

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
