#!/bin/bash
# sil_entrypoint.sh — staged SIL + L3 node startup to eliminate startup race.
#
# Stage 1: 9 SIL simulation nodes (ship_dynamics first → wait for own_ship_state)
# Stage 2: internal topic-liveness check (/sil/own_ship_state ≥ 1 frame)
# Stage 3: 7 L3 kernel nodes in dependency order (M1→M2→M4→M6→M5→M8, M7 subprocess)
#
# NO set -e: lifecycle state-machine errors (invalid transition) must not
# terminate the container. The Python spin loop handles them gracefully.

source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash

echo "==================================================================="
echo "=== SIL Staged Entrypoint — $(date -u +'%Y-%m-%dT%H:%M:%SZ') ==="
echo "==================================================================="

python3 -c "
import os, sys, time, subprocess, threading
from datetime import datetime, timezone

import rclpy
from rclpy.executors import MultiThreadedExecutor
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy
from rclpy.lifecycle import TransitionCallbackReturn

# ── Timestamp helper ──────────────────────────────────────────
def ts() -> str:
    return datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

# ── Stage 1: SIL simulation nodes ─────────────────────────────
sys.stdout.flush()
print(f'[{ts()}] Stage 1/3: Starting SIL simulation nodes (9 total)')

rclpy.init()

# Topic liveness detector — runs inside the spin loop to check if
# /sil/own_ship_state has published at least one frame.
own_ship_frames = 0
own_ship_lock = threading.Lock()

def on_own_ship(msg):
    global own_ship_frames
    with own_ship_lock:
        own_ship_frames += 1

# Import SIL node classes
from sil_lifecycle.lifecycle_mgr import LifecycleManagerNode
from ship_dynamics.node import ShipDynamicsNode
from env_disturbance.node import EnvDisturbanceNode
from target_vessel.node import TargetVesselNode
from sensor_mock.node import SensorMockNode
from tracker_mock.node import TrackerMockNode
from fault_injection.node import FaultInjectionNode
from scoring.node import ScoringLifecycleNode as ScoringNode
from scenario_authoring.node import ScenarioAuthoringNode

executor = MultiThreadedExecutor(num_threads=6)
nodes = []

# Order: LifecycleManagerNode first (manages transitions),
# then ship_dynamics (produces /sil/own_ship_state), then others
sil_node_classes = [
    LifecycleManagerNode,
    ShipDynamicsNode,
    EnvDisturbanceNode,
    TargetVesselNode,
    SensorMockNode,
    TrackerMockNode,
    FaultInjectionNode,
    ScoringNode,
    ScenarioAuthoringNode,
]

for cls in sil_node_classes:
    node = cls()
    executor.add_node(node)
    nodes.append(node)
    node_name = node.get_name()
    print(f'  [{ts()}] Stage 1: created {node_name}')

# Create a subscriber for liveness detection — attach to a tiny node
# so we can poll /sil/own_ship_state from the executor.
from std_msgs.msg import Header
liveness_node = rclpy.create_node('_sil_liveness_probe')
sq = QoSProfile(
    reliability=QoSReliabilityPolicy.BEST_EFFORT,
    durability=QoSDurabilityPolicy.VOLATILE,
    history=QoSHistoryPolicy.KEEP_LAST,
    depth=1,
)
liveness_sub = liveness_node.create_subscription(
    Header, '/sil/own_ship_state', on_own_ship, sq)
executor.add_node(liveness_node)
nodes.append(liveness_node)

# ── Stage 1.5: Validating critical node on_configure ──────────
# Explicitly call on_configure for key SIL nodes that may detect sentinel defaults.
# If any returns FAILURE, abort immediately rather than run a 'zombie simulation'.
print(f'[{ts()}] Stage 1.5: Validating node configuration...')
cfg_failures = []
for node in nodes:
    name = node.get_name()
    if name in ('ship_dynamics_node', 'target_vessel_node'):
        try:
            cfg_result = node.on_configure(None)
            if cfg_result is not None and cfg_result != TransitionCallbackReturn.SUCCESS:
                cfg_failures.append(f'{name}: on_configure returned {cfg_result}')
                print(f'  [{ts()}] FATAL: {name} on_configure FAILED → {cfg_result}', file=sys.stderr)
            else:
                print(f'  [{ts()}] {name} on_configure OK')
        except Exception as exc:
            cfg_failures.append(f'{name}: on_configure raised {type(exc).__name__}: {exc}')
            print(f'  [{ts()}] FATAL: {name} on_configure crashed: {exc}', file=sys.stderr)

if cfg_failures:
    print(f'[{ts()}] FATAL: {len(cfg_failures)} node(s) failed on_configure:', file=sys.stderr)
    for f in cfg_failures:
        print(f'  - {f}', file=sys.stderr)
    sys.exit(1)

sil_count = len(nodes) - 1  # exclude liveness probe
print(f'[{ts()}] Stage 1 complete: {sil_count} SIL nodes created')

# ── Stage 2: Wait for /sil/own_ship_state ≥ 1 frame ──────────
print(f'[{ts()}] Stage 2/3: Waiting for /sil/own_ship_state (timeout=60s)...')
sys.stdout.flush()

WAIT_START = time.monotonic()
WAIT_TIMEOUT = 60.0

while time.monotonic() - WAIT_START < WAIT_TIMEOUT:
    executor.spin_once(timeout_sec=0.5)
    with own_ship_lock:
        frames = own_ship_frames
    if frames >= 1:
        elapsed = time.monotonic() - WAIT_START
        print(f'[{ts()}] Stage 2: /sil/own_ship_state received {frames} frame(s) after {elapsed:.1f}s')
        break
    if int(time.monotonic() - WAIT_START) % 10 == 0:
        elapsed_int = int(time.monotonic() - WAIT_START)
        print(f'[{ts()}] Stage 2: still waiting ... {frames} frames, {elapsed_int}s elapsed')
    time.sleep(0.5)
else:
    print(f'[{ts()}] Stage 2 FAIL: /sil/own_ship_state NOT received after {WAIT_TIMEOUT}s', file=sys.stderr)
    print(f'[{ts()}] DIAGNOSTIC: node list:', file=sys.stderr)
    for n in nodes:
        print(f'  {n.get_name()}', file=sys.stderr)
    sys.exit(1)

# Remove liveness probe — no longer needed
executor.remove_node(liveness_node)
nodes.remove(liveness_node)
try:
    liveness_node.destroy_node()
except Exception:
    pass

# ── Stage 3: L3 kernel nodes (dependency order) ────────────────
import os as _os
if _os.environ.get('SIL_L3_ENABLE', '1') == '1':
    print(f'[{ts()}] Stage 3/3: Starting L3 kernel bridge + nodes')
    sys.stdout.flush()

    # 3a. Start topic bridge as background process (same as before)
    bridge_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/sil_topic_bridge.py'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  [{ts()}] Bridge PID: {bridge_proc.pid}')

    # 3b. Import L3 node classes
    try:
        from m1_odd_envelope_manager.odd_envelope_manager_node import OddEnvelopeManagerNode
        from m2_world_model.world_model_node import WorldModelNode
        from m3_mission_manager.mission_manager_node import MissionManagerNode
        from m4_behavior_arbiter.behavior_arbiter_node import BehaviorArbiterNode
        from m6_colregs_reasoner.colregs_reasoner_node import ColregsReasonerNode
        from m5_tactical_planner.mid_mpc.mid_mpc_node import MidMpcNode
        from m8_hmi_transparency_bridge.hmi_transparency_bridge_node import HmiTransparencyBridgeNode
        from m7_safety_supervisor.safety_supervisor_node import SafetySupervisorNode

        # Dependency order: M1 (ODD context) → M2 (world model) → M4 (arbiter) →
        #                   M6 (COLREGs constraints) → M5 (planner) → M8 (HMI)
        # M7 (Safety Supervisor) runs as INDEPENDENT SUBPROCESS for Doer-Checker isolation.
        l3_node_classes = [
            (OddEnvelopeManagerNode,      'm1_odd_manager'),
            (WorldModelNode,              'm2_world_model'),
            (BehaviorArbiterNode,         'm4_behavior_arbiter'),
            (ColregsReasonerNode,         'm6_colregs_reasoner'),
            (MidMpcNode,                  'm5_tactical_planner'),
            (HmiTransparencyBridgeNode,   'm8_hmi_bridge'),
        ]

        l3_created = 0
        for node_cls, name in l3_node_classes:
            try:
                node = node_cls()
                executor.add_node(node)
                nodes.append(node)
                l3_created += 1
                print(f'  [{ts()}] Stage 3: created L3 node {node.get_name()}')
                # Small spin to let the node process incoming messages from
                # the now-alive SIL topics before the next node starts.
                executor.spin_once(timeout_sec=0.2)
            except Exception as exc:
                print(f'  [{ts()}] [WARN] Failed to create L3 {name}: {exc}', file=sys.stderr)

        # M7 Safety Supervisor — independent subprocess (Doer-Checker isolation)
        # Uses ros2 run in a separate process so it shares no executor, no GIL, no
        # shared data with the Doer modules (M1-M6).
        print(f'  [{ts()}] Stage 3: starting M7 SafetySupervisor as independent subprocess')
        m7_proc = subprocess.Popen(
            ['ros2', 'run', 'm7_safety_supervisor', 'safety_supervisor_node'],
            stdout=sys.stdout, stderr=sys.stderr
        )
        print(f'  [{ts()}] M7 PID: {m7_proc.pid}')
        # Wait briefly and check M7 is alive
        time.sleep(2.0)
        if m7_proc.poll() is not None:
            print(f'  [{ts()}] M7 FAILED to start (exit code {m7_proc.returncode})', file=sys.stderr)
        else:
            l3_created += 1

        # M3 MissionManager — long-horizon, starts after M1/M2/M4 are up
        try:
            node = MissionManagerNode()
            executor.add_node(node)
            nodes.append(node)
            l3_created += 1
            print(f'  [{ts()}] Stage 3: created L3 node {node.get_name()}')
            executor.spin_once(timeout_sec=0.2)
        except Exception as exc:
            print(f'  [{ts()}] [WARN] Failed to create L3 m3_mission_manager: {exc}', file=sys.stderr)

        # Report
        all_modules = [n.get_name() for n in nodes]
        total_active = len(all_modules)
        print(f'[{ts()}] Stage 3 complete: {l3_created} L3 nodes created')
        print(f'[{ts()}] Total nodes: {total_active}')
        print(f'[{ts()}] Node list: {\" | \".join(all_modules)}')

    except ImportError as exc:
        print(f'  [{ts()}] [WARN] L3 kernel import failed: {exc}', file=sys.stderr)
        print(f'  [{ts()}] [WARN] L3 nodes not started — sim-only mode', file=sys.stderr)
else:
    print(f'[{ts()}] Stage 3/3: SIL_L3_ENABLE=0 — L3 kernel bypassed')
    bridge_proc = None
    m7_proc = None

sys.stdout.flush()

# ── Resilient spin loop ────────────────────────────────────────
print(f'[{ts()}] SIL staged entrypoint — entering main spin loop')
sys.stdout.flush()

try:
    while rclpy.ok():
        try:
            executor.spin_once(timeout_sec=1.0)
        except Exception as exc:
            # Log and continue — do NOT re-raise.
            print(f'[{ts()}] [WARN] executor spin_once error (lifecycle transition rejected, continuing): '
                  f'{type(exc).__name__}: {exc}', file=sys.stderr, flush=True)
finally:
    print(f'[{ts()}] SIL nodes shutting down', flush=True)
    executor.shutdown()
    for node in nodes:
        try:
            node.destroy_node()
        except Exception:
            pass
    rclpy.shutdown()
    # Clean up subprocesses
    for proc_var in ('bridge_proc', 'm7_proc'):
        p = locals().get(proc_var)
        if p and p.poll() is None:
            p.terminate()
            p.wait(timeout=5)
    sys.stdout.flush()
" 2>&1
