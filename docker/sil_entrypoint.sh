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
echo "=== TLS/WSS certs: /certs/sil.{crt,key} (DoD #18)              ==="
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

from rclpy.executors import SingleThreadedExecutor

executor = MultiThreadedExecutor(num_threads=8)
nodes = []

# Order: ship_dynamics (produces /sil/own_ship_state) first, then others
sil_node_classes = [
    ShipDynamicsNode,
    EnvDisturbanceNode,
    TargetVesselNode,
    SensorMockNode,
    TrackerMockNode,
    FaultInjectionNode,
    ScoringNode,
    ScenarioAuthoringNode,
]

# 1. Create and spin LifecycleManagerNode in a dedicated SingleThreadedExecutor and thread
# to prevent executor-level thread starvation and circular time deadlocks.
mgr_node = LifecycleManagerNode()
nodes.append(mgr_node)
print(f'  [{ts()}] Stage 1: created scenario_lifecycle_mgr (dedicated thread)')

from rclpy.executors import SingleThreadedExecutor
mgr_executor = SingleThreadedExecutor()
mgr_executor.add_node(mgr_node)

def run_mgr():
    try:
        mgr_executor.spin()
    except Exception as exc:
        print(f'[{ts()}] [WARN] mgr_executor thread exit: {exc}', file=sys.stderr)

mgr_thread = threading.Thread(target=run_mgr, daemon=True)
mgr_thread.start()

# 2. Create and add the 8 simulation nodes to the main executor
from rclpy.parameter import Parameter as _Param
for cls in sil_node_classes:
    node = cls()
    try:
        node.set_parameters([_Param('use_sim_time', _Param.Type.BOOL, True)])
    except Exception as exc:
        print(f'  [{ts()}] [WARN] Failed to set use_sim_time on {node.get_name()}: {exc}')
    executor.add_node(node)
    nodes.append(node)
    node_name = node.get_name()
    print(f'  [{ts()}] Stage 1: created {node_name}')



# Create a subscriber for liveness detection — attach to a tiny node
# so we can poll /sil/own_ship_state from the executor.
# IMPORTANT: must use the SAME message type as the publisher (sil_msgs/OwnShipState)
from sil_msgs.msg import OwnShipState as _LivenessMsg
from rclpy.parameter import Parameter as _Param
liveness_node = rclpy.create_node(
    '_sil_liveness_probe',
    parameter_overrides=[_Param('use_sim_time', _Param.Type.BOOL, False)]
)


sq = QoSProfile(
    reliability=QoSReliabilityPolicy.BEST_EFFORT,
    durability=QoSDurabilityPolicy.VOLATILE,
    history=QoSHistoryPolicy.KEEP_LAST,
    depth=1,
)
liveness_sub = liveness_node.create_subscription(
    _LivenessMsg, '/sil/own_ship_state', on_own_ship, sq)
executor.add_node(liveness_node)
nodes.append(liveness_node)

# ── Stage 1.5: Configure + Activate LifecycleNodes ────────────
# LifecycleNodes must go: unconfigured → configure → activate
# Only activated nodes create publishers/timers and start publishing.
print(f'[{ts()}] Stage 1.5: Configuring + activating LifecycleNodes...')
cfg_failures = []
lifecycle_nodes_to_activate = ('scenario_lifecycle_mgr', 'ship_dynamics_node', 'target_vessel_node',
                                'env_disturbance_node', 'sensor_mock_node', 'tracker_mock_node',
                                'fault_injection_node', 'scoring_node', 'scenario_authoring_node')

for node in nodes:
    name = node.get_name()
    # Skip non-lifecycle nodes (liveness probe)
    if not hasattr(node, 'on_configure') or not hasattr(node, 'on_activate'):
        continue
    if name.startswith('_'):
        continue  # Skip liveness probe

    # Step 1: on_configure
    try:
        cfg_result = node.on_configure(None)
        if cfg_result is not None and cfg_result != TransitionCallbackReturn.SUCCESS:
            cfg_failures.append(f'{name}: on_configure returned {cfg_result}')
            print(f'  [{ts()}] FATAL: {name} on_configure FAILED → {cfg_result}', file=sys.stderr)
            continue
        else:
            print(f'  [{ts()}] {name} on_configure OK')
    except Exception as exc:
        cfg_failures.append(f'{name}: on_configure raised {type(exc).__name__}: {exc}')
        print(f'  [{ts()}] FATAL: {name} on_configure crashed: {exc}', file=sys.stderr)
        continue

    # Step 2: on_activate (creates publishers, timers, subscriptions)
    try:
        act_result = node.on_activate(None)
        if act_result is not None and act_result != TransitionCallbackReturn.SUCCESS:
            cfg_failures.append(f'{name}: on_activate returned {act_result}')
            print(f'  [{ts()}] WARN: {name} on_activate FAILED → {act_result}', file=sys.stderr)
        else:
            print(f'  [{ts()}] {name} on_activate OK')
    except Exception as exc:
        # Some nodes may not have on_activate — that's OK (non-lifecycle)
        print(f'  [{ts()}] [INFO] {name} on_activate skipped: {type(exc).__name__}: {exc}')

if cfg_failures:
    print(f'[{ts()}] FATAL: {len(cfg_failures)} node(s) failed lifecycle transitions:', file=sys.stderr)
    for f in cfg_failures:
        print(f'  - {f}', file=sys.stderr)
    sys.exit(1)

sil_count = len(nodes) - 1  # exclude liveness probe
print(f'[{ts()}] Stage 1 complete: {sil_count} SIL nodes configured + activated')

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
        ['python3', '/opt/ws/docker/sil_topic_bridge.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  [{ts()}] Bridge PID: {bridge_proc.pid}')

    # 3b. Launch L3 kernel C++ nodes as subprocesses via ros2 run
    # M1-M8 are ament_cmake packages with C++ executables — they cannot be
    # imported as Python modules. Each runs in its own process.
    # Dependency order: M1 → M2 → M4 → M6 → M5 → M3 → M8 → M7 (independent)
    l3_launch_specs = [
        ('m1_odd_envelope_manager', 'm1_odd_envelope_manager',  'm1_odd_manager', ['--ros-args', '-p', 'yaml_path:=/opt/ws/install/m1_odd_envelope_manager/share/m1_odd_envelope_manager/config/m1_params.yaml']),
        ('m2_world_model',          'm2_world_model',           'm2_world_model', []),
        ('m4_behavior_arbiter',     'm4_behavior_arbiter',      'm4_behavior_arbiter', ['--ros-args', '-p', 'config_dir:=/opt/ws/install/m4_behavior_arbiter/share/m4_behavior_arbiter/config']),
        ('m6_colregs_reasoner',     'm6_colregs_reasoner',      'm6_colregs_reasoner', ['--ros-args', '-p', 'config_dir:=/opt/ws/install/m6_colregs_reasoner/share/m6_colregs_reasoner/config']),
        ('m5_tactical_planner',     'm5_mid_mpc_node',          'm5_tactical_planner', ['--ros-args', '-r', '/m5/avoidance_plan:=/l3/m5/avoidance_plan', '-r', '/m5/sat_data:=/l3/sat/data', '-r', '/m5/asdr_record:=/l3/asdr/record']),
        ('m3_mission_manager',      'm3_mission_manager',       'm3_mission_manager', []),
        ('m8_hmi_transparency_bridge', 'm8_hmi_transparency_bridge_node', 'm8_hmi_bridge', []),
    ]

    l3_procs = []
    l3_created = 0
    for pkg, exe, label, extra_args in l3_launch_specs:
        try:
            # Build full arguments list with use_sim_time:=True
            cmd = ['ros2', 'run', pkg, exe]
            has_ros_args = False
            for arg in extra_args:
                if arg == '--ros-args':
                    has_ros_args = True
            if has_ros_args:
                cmd += extra_args + ['-p', 'use_sim_time:=True']
            else:
                cmd += extra_args + ['--ros-args', '-p', 'use_sim_time:=True']

            proc = subprocess.Popen(
                cmd,
                stdout=sys.stdout, stderr=sys.stderr
            )
            l3_procs.append(proc)
            print(f'  [{ts()}] Stage 3: launched {label} (PID {proc.pid})')
            l3_created += 1
            # Small delay between launches to respect dependency order
            time.sleep(1.0)
            if proc.poll() is not None:
                print(f'  [{ts()}] [WARN] {label} exited early (code {proc.returncode})', file=sys.stderr)
                l3_created -= 1
        except Exception as exc:
            print(f'  [{ts()}] [WARN] Failed to launch {label}: {exc}', file=sys.stderr)

    # M7 Safety Supervisor — independent subprocess (Doer-Checker isolation)
    # Uses ros2 run in a separate process so it shares no executor, no GIL, no
    # shared data with the Doer modules (M1-M6).
    print(f'  [{ts()}] Stage 3: starting M7 SafetySupervisor as independent subprocess')
    m7_proc = subprocess.Popen(
        ['ros2', 'run', 'm7_safety_supervisor', 'm7_safety_supervisor', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    l3_procs.append(m7_proc)
    print(f'  [{ts()}] M7 PID: {m7_proc.pid}')
    time.sleep(2.0)
    if m7_proc.poll() is not None:
        print(f'  [{ts()}] M7 FAILED to start (exit code {m7_proc.returncode})', file=sys.stderr)
    else:
        l3_created += 1


    # Report
    all_modules = [n.get_name() for n in nodes]
    total_active = len(all_modules) + l3_created  # SIL nodes + L3 subprocesses
    print(f'[{ts()}] Stage 3 complete: {l3_created} L3 nodes launched as subprocesses')
    print(f'[{ts()}] Total active: {total_active} ({len(all_modules)} SIL + {l3_created} L3)')
    _sep = ' | '
    print(f'[{ts()}] SIL nodes: {_sep.join(all_modules)}')

else:
    print(f'[{ts()}] Stage 3/3: SIL_L3_ENABLE=0 — L3 kernel bypassed')
    bridge_proc = None
    l3_procs = []

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
    # Clean up subprocesses (bridge + all L3 processes)
    if 'bridge_proc' in dir() and bridge_proc and bridge_proc.poll() is None:
        bridge_proc.terminate()
        bridge_proc.wait(timeout=5)
    for p in (l3_procs if 'l3_procs' in dir() else []):
        if p and p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()
    sys.stdout.flush()
" 2>&1
