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

if [[ "${TDL_INTEGRATION_PROFILE:-default}" != "default" ]]; then
  /opt/ws/scripts/integration/start_external_adapters.sh &
fi

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

# 1. Create and spin LifecycleManagerNode in a dedicated 2-thread executor.
# SingleThreadedExecutor caused SetParameters starvation: rclpy processes timers
# before services, so the 1000Hz clock timer perpetually blocked service callbacks.
# MultiThreadedExecutor(2): thread-1 handles clock timer, thread-2 handles services.
mgr_node = LifecycleManagerNode()
nodes.append(mgr_node)
print(f'  [{ts()}] Stage 1: created scenario_lifecycle_mgr (dedicated 2-thread executor)')

from rclpy.executors import MultiThreadedExecutor as _MgrExecutor
mgr_executor = _MgrExecutor(num_threads=2)
mgr_executor.add_node(mgr_node)

def run_mgr():
    while rclpy.ok():
        try:
            mgr_executor.spin()
        except Exception as exc:
            print(f'[{ts()}] [WARN] mgr_executor exception (restarting spin): {exc}', file=sys.stderr, flush=True)
            import time
            time.sleep(0.5)

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
external_l2_route = _os.environ.get('TDL_INTEGRATION_PROFILE', 'default') in ('a4000_external', 'hybrid_debug')
if _os.environ.get('SIL_L3_ENABLE', '1') == '1':
    print(f'[{ts()}] Stage 3/3: Starting L3 kernel bridge + nodes')
    sys.stdout.flush()

    # 3a. Start topic bridge as background process (same as before)
    bridge_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/sil_topic_bridge.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  [{ts()}] Bridge PID: {bridge_proc.pid}')

    l4_adapter_node = None
    if _os.environ.get('SIL_L4_ADAPTER_ENABLE', '0').strip().lower() in ('1', 'true', 'on', 'yes'):
        try:
            l4_source_path = '/opt/ws/src/sim_workbench/sil_nodes/l4_guidance_adapter'
            if _os.path.isdir(l4_source_path) and l4_source_path not in sys.path:
                sys.path.insert(0, l4_source_path)
            from l4_guidance_adapter.node import L4GuidanceAdapterNode
            l4_adapter_node = L4GuidanceAdapterNode()
            try:
                l4_adapter_node.set_parameters([_Param('use_sim_time', _Param.Type.BOOL, True)])
            except Exception as exc:
                print(f'  [{ts()}] [WARN] Failed to set use_sim_time on l4_guidance_adapter: {exc}')
            executor.add_node(l4_adapter_node)
            nodes.append(l4_adapter_node)
            print(f'  [{ts()}] Stage 3: created l4_guidance_adapter (owns /sil/actuator_cmd)')
        except Exception as exc:
            print(f'  [{ts()}] FATAL: failed to start l4_guidance_adapter: {type(exc).__name__}: {exc}', file=sys.stderr)
            sys.exit(1)

    # 3a-2. Start mock L2 publisher (unblocks M3 AWAITING_ROUTE)
    # Detect active scenario YAML from scenario directory
    scenario_dir = _os.environ.get('SIL_SCENARIO_DIR', '/var/sil/scenarios')
    active_scenario_yaml = ''
    if _os.path.isdir(scenario_dir):
        # Try to find imazu-01-ho.yaml in the scenario dir (DEMO-1 default)
        for scenario_candidate in ['imazu-01-ho.yaml', 'active_scenario.yaml']:
            candidate_path = _os.path.join(scenario_dir, scenario_candidate)
            if _os.path.isfile(candidate_path):
                active_scenario_yaml = candidate_path
                print(f'  [{ts()}] Detected active scenario: {active_scenario_yaml}')
                break

    mock_l2_env = {**_os.environ, 'SIL_SCENARIO_DIR': scenario_dir}
    if active_scenario_yaml:
        mock_l2_env['SIL_SCENARIO_YAML'] = active_scenario_yaml
    if external_l2_route:
        mock_l2_env['SIL_MOCK_L2_ROUTE_ENABLE'] = '0'

    mock_l2_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/mock_l2_publisher.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr,
        env=mock_l2_env
    )
    print(f'  [{ts()}] Mock L2 Publisher PID: {mock_l2_proc.pid}')

    # 3a-2b. Start GNC route mock publisher (D1.8: publishes ship_interfaces/GncRoutePlan
    # on /route_planning/gnc_route_plan from the active scenario's nominalRoute — stands
    # in for the real L2 in Phase 1).
    gnc_route_proc = None
    route_ingest_proc = None
    if external_l2_route:
        print(f'  [{ts()}] external L2 route profile: skipping internal GNC route mock and route ingest')
    else:
        gnc_route_proc = subprocess.Popen(
            ['python3', '/opt/ws/docker/gnc_route_mock_publisher.py', '--ros-args', '-p', 'use_sim_time:=True'],
            stdout=sys.stdout, stderr=sys.stderr,
            env=mock_l2_env
        )
        print(f'  [{ts()}] GNC Route Mock Publisher PID: {gnc_route_proc.pid}')

        # 3a-2c. Start route ingest node (D1.8: re-entrant L3 consumer of GncRoutePlan;
        # republishes to internal /l2/planned_route for the frontend route layer).
        route_ingest_proc = subprocess.Popen(
            ['python3', '/opt/ws/docker/route_ingest_node.py', '--ros-args', '-p', 'use_sim_time:=True'],
            stdout=sys.stdout, stderr=sys.stderr
        )
        print(f'  [{ts()}] Route Ingest Node PID: {route_ingest_proc.pid}')

    # 3a-3. Start FSM aggregator node (publishes /l3/fsm_state at 10Hz)
    fsm_agg_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/fsm_aggregator_node.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  [{ts()}] FSM Aggregator PID: {fsm_agg_proc.pid}')

    # 3a-4. Start diagnostic mock publisher (F1b: keeps M1 envelope_state=ENVELOPE_IN
    # by publishing healthy /diagnostics for radar/comm/tmr sensors that M1 looks for).
    diag_mock_proc = subprocess.Popen(
        ['python3', '/opt/ws/docker/diagnostic_mock_publisher.py', '--ros-args', '-p', 'use_sim_time:=True'],
        stdout=sys.stdout, stderr=sys.stderr
    )
    print(f'  [{ts()}] Diagnostic Mock PID: {diag_mock_proc.pid}')

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
            exc_type = type(exc).__name__
            exc_msg  = str(exc)
            # I-4 fix: only swallow InvalidStateTransition errors from the ROS2
            # lifecycle state machine (these are benign: a node received a
            # transition request it was already in, not a crash).
            # All other exceptions are node crashes — log to stderr and re-raise
            # so the container exits non-zero (fail-fast for SIL safety).
            is_lifecycle_rejection = (
                'InvalidStateTransition' in exc_type or
                'invalid transition' in exc_msg.lower() or
                'lifecycle' in exc_msg.lower() or
                # InvalidHandle fires when the liveness-probe node is destroyed
                # mid-spin; the probe is intentionally removed after Stage 2 and
                # its handle becomes dangling. This is not a node crash.
                'InvalidHandle' in exc_type or
                'cannot use' in exc_msg.lower()
            )
            if is_lifecycle_rejection:
                print(f'[{ts()}] [WARN] lifecycle transition rejected (benign): '
                      f'{exc_type}: {exc_msg}', file=sys.stderr, flush=True)
            else:
                print(f'[{ts()}] [FATAL] executor spin_once crashed — failing fast: '
                      f'{exc_type}: {exc_msg}', file=sys.stderr, flush=True)
                raise
finally:
    print(f'[{ts()}] SIL nodes shutting down', flush=True)
    executor.shutdown()
    for node in nodes:
        try:
            node.destroy_node()
        except Exception:
            pass
    rclpy.shutdown()
    # Clean up subprocesses (bridge + mock L2 + all L3 processes)
    if 'bridge_proc' in dir() and bridge_proc and bridge_proc.poll() is None:
        bridge_proc.terminate()
        bridge_proc.wait(timeout=5)
    if 'mock_l2_proc' in dir() and mock_l2_proc and mock_l2_proc.poll() is None:
        mock_l2_proc.terminate()
        mock_l2_proc.wait(timeout=5)
    if 'gnc_route_proc' in dir() and gnc_route_proc and gnc_route_proc.poll() is None:
        gnc_route_proc.terminate()
        gnc_route_proc.wait(timeout=5)
    if 'route_ingest_proc' in dir() and route_ingest_proc and route_ingest_proc.poll() is None:
        route_ingest_proc.terminate()
        route_ingest_proc.wait(timeout=5)
    if 'fsm_agg_proc' in dir() and fsm_agg_proc and fsm_agg_proc.poll() is None:
        fsm_agg_proc.terminate()
        fsm_agg_proc.wait(timeout=5)
    if 'diag_mock_proc' in dir() and diag_mock_proc and diag_mock_proc.poll() is None:
        diag_mock_proc.terminate()
        diag_mock_proc.wait(timeout=5)
    for p in (l3_procs if 'l3_procs' in dir() else []):
        if p and p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()
    sys.stdout.flush()
" 2>&1
