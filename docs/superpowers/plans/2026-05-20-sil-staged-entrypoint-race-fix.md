# SIL Staged Entrypoint Race Condition Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix DEMO-1 ~70% startup race condition where L3 nodes (M2 World Model) begin subscribing to `/sil/own_ship_state` before ship_dynamics publishes its first frame, causing 1-2 min of "no message received" / stale-frame failures.

**Architecture:** Staged startup in `sil_entrypoint.sh` (SIL→wait→L3), explicit lifecycle sync in `lifecycle_bridge.py`, new `wait_for_topic.sh` probe utility, and fail-loud diagnostics. Zero changes to M1-M8 internal retry logic (out of scope). M7 runs as independent subprocess for Doer-Checker isolation.

**Tech Stack:** Bash 5, Python 3.10, rclpy 5.x (Humble), ROS2 lifecycle services

**Risk Profile:** Low — entrypoint-only changes, no module internals. Rollback = `git checkout -- docker/sil_entrypoint.sh src/sil_orchestrator/lifecycle_bridge.py` + `rm scripts/wait_for_topic.sh`.

**Affected D-tasks:** D1.3.2.3 (SIL integration), D1.5 (V&V — verify_demo1_e2e.sh pass 3x)

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `scripts/wait_for_topic.sh` | **Create** | Blocking topic-liveness probe (ros2 topic hz wrapper) |
| `docker/sil_entrypoint.sh` | **Modify** | 3-stage startup: SIL nodes → wait-ready → L3 nodes (dependency order) |
| `src/sil_orchestrator/lifecycle_bridge.py` | **Modify** | `configure()` explicit wait-for-ACTIVE, remove asyncio.create_task fire-and-forget |
| `docker/sil_nodes.Dockerfile` | **Modify** | COPY stage — ensure wait_for_topic.sh is in image |

---

### Task 1: Create `scripts/wait_for_topic.sh` — Topic Readiness Probe

**Files:**
- Create: `scripts/wait_for_topic.sh`
- Modify: `docker/sil_nodes.Dockerfile:53-54` (COPY into image)

**Rollback:** `rm scripts/wait_for_topic.sh` + revert Dockerfile COPY line.

- [ ] **Step 1: Create the probe script**

```bash
#!/bin/bash
# wait_for_topic.sh — block until a ROS2 topic publishes ≥1 frame, or timeout.
# Usage: wait_for_topic.sh <topic_name> [timeout_s] [--hz-window N]
# Exit: 0 = topic alive and publishing, 1 = timeout expired

set -euo pipefail

TOPIC="${1:?Usage: wait_for_topic.sh <topic_name> [timeout_s]}"
TIMEOUT="${2:-30}"
HZ_WINDOW="${3:-3}"

# Source ROS2 environment — caller must ensure /opt/ros/humble/setup.bash
# and /opt/ws/install/setup.bash are already sourced, or pass --source.

echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') Waiting for '$TOPIC' (timeout=${TIMEOUT}s)..."

ELAPSED=0
INTERVAL=2  # seconds between probes

while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    # ros2 topic hz with --window gives average rate; if it returns non-zero
    # exit AND prints "average rate", the topic is publishing.
    HZ_OUTPUT=$(ros2 topic hz "$TOPIC" --window "$HZ_WINDOW" 2>&1) || true
    if echo "$HZ_OUTPUT" | grep -q 'average rate'; then
        AVG_RATE=$(echo "$HZ_OUTPUT" | grep 'average rate' | tail -1 | awk '{print $3}')
        echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') '$TOPIC' publishing @ ${AVG_RATE} Hz (after ${ELAPSED}s)"
        exit 0
    fi

    echo "[wait_for_topic] ... '$TOPIC' not yet publishing (elapsed=${ELAPSED}s, will retry in ${INTERVAL}s)"
    sleep "$INTERVAL"
    ELAPSED=$((ELAPSED + INTERVAL))
done

echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') TIMEOUT: '$TOPIC' not publishing after ${TIMEOUT}s" >&2

# Diagnostic dump on failure
echo "[DIAGNOSTIC] Active nodes:" >&2
ros2 node list 2>&1 >&2 || true
echo "[DIAGNOSTIC] Active topics:" >&2
ros2 topic list 2>&1 >&2 || true
exit 1
```

- [ ] **Step 2: Make executable and verify syntax**

```bash
chmod +x scripts/wait_for_topic.sh
bash -n scripts/wait_for_topic.sh
```

Expected: no syntax errors.

- [ ] **Step 3: Copy into Docker image**

Modify `docker/sil_nodes.Dockerfile`, after line 53 (`COPY docker/sil_entrypoint.sh /opt/ws/sil_entrypoint.sh`):

```dockerfile
COPY docker/sil_entrypoint.sh /opt/ws/sil_entrypoint.sh
RUN chmod +x /opt/ws/sil_entrypoint.sh
# Add topic probe script for staged startup
COPY scripts/wait_for_topic.sh /opt/ws/wait_for_topic.sh
RUN chmod +x /opt/ws/wait_for_topic.sh
```

Note: lines 53-55 in current Dockerfile are:
```
COPY docker/sil_entrypoint.sh /opt/ws/sil_entrypoint.sh
RUN chmod +x /opt/ws/sil_entrypoint.sh

# Copy L3 kernel launch files
```

- [ ] **Step 4: Commit Task 1**

```bash
git add scripts/wait_for_topic.sh docker/sil_nodes.Dockerfile
git commit -m "feat: add wait_for_topic.sh ROS2 topic readiness probe for staged SIL startup"
```

---

### Task 2: Rewrite `docker/sil_entrypoint.sh` — 3-Stage Startup

**Files:**
- Modify: `docker/sil_entrypoint.sh` (full rewrite, lines 1-120)

**Rollback:** `git checkout -- docker/sil_entrypoint.sh`

**Design:**
- Stage 1: Create SIL 9 nodes, spin to get /sil/own_ship_state publishing ≥1 frame
- Stage 2: Probe /sil/own_ship_state internally (not via wait_for_topic.sh — we're inside Python)
- Stage 3: Create L3 nodes in dependency order (M1→M2→M4→M6→M5→M8, M7 as subprocess)
- M7 SafetySupervisorNode runs as independent subprocess for Doer-Checker isolation
- All logging: UTC timestamps + node active counts per stage

- [ ] **Step 1: Write the new entrypoint script**

```bash
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

sil_count = len(nodes) - 1  # exclude liveness probe
print(f'[{ts()}] Stage 1 complete: {sil_count} SIL nodes created')

# ── Stage 2: Wait for /sil/own_ship_state ≥ 1 frame ──────────
print(f'[{ts()}] Stage 2/3: Waiting for /sil/own_ship_state (timeout=60s)...')
sys.stdout.flush()

WAIT_START = time.monotonic()
WAIT_TIMEOUT = 60.0
WAIT_INTERVAL = 1.0

while time.monotonic() - WAIT_START < WAIT_TIMEOUT:
    executor.spin_once(timeout_sec=0.5)
    with own_ship_lock:
        frames = own_ship_frames
    if frames >= 1:
        elapsed = time.monotonic() - WAIT_START
        print(f'[{ts()}] Stage 2: /sil/own_ship_state received {frames} frame(s) after {elapsed:.1f}s')
        break
    if int(time.monotonic() - WAIT_START) % 10 == 0:
        # Print progress every 10s to avoid log spam
        print(f'[{ts()}] Stage 2: still waiting ... {frames} frames, {time.monotonic() - WAIT_START:.0f}s elapsed')
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
```

- [ ] **Step 2: Verify bash syntax**

```bash
bash -n docker/sil_entrypoint.sh
```

Expected: no syntax errors.

- [ ] **Step 3: Commit Task 2**

```bash
git add docker/sil_entrypoint.sh
git commit -m "fix: staged 3-phase startup in sil_entrypoint.sh to eliminate M2/ship_dynamics race"
```

---

### Task 3: Fix `lifecycle_bridge.py` — Explicit Sync in `configure()`

**Files:**
- Modify: `src/sil_orchestrator/lifecycle_bridge.py:191-204` (configure method)

**Rollback:** `git checkout -- src/sil_orchestrator/lifecycle_bridge.py`

**Design:** Replace `asyncio.create_task(self._broadcast_transition(...))` at line 203 with an explicit await that waits (up to 15s) for all broadcast nodes to transition to their target state. On timeout, fail-loud with a list of nodes that didn't make it.

- [ ] **Step 1: Add `_broadcast_and_wait` method and refactor `configure()`**

Replace lines 191-204 of `lifecycle_bridge.py` with:

```python
    async def _broadcast_and_wait(self, transition_id: int, target_state: str, timeout_s: float = 15.0) -> LifecycleResult:
        """Broadcast transition to all SIL nodes and wait for every node to reach target_state.

        Unlike the old fire-and-forget asyncio.create_task() pattern, this blocks
        until all nodes confirm they've transitioned (or timeout). This prevents
        the race where configure() returns before ship_dynamics publishes.
        """
        _log.info("[broadcast] Sending transition %d to %d SIL nodes, waiting for '%s' (timeout=%.0fs)",
                  transition_id, len(self._sil_change_state_clients), target_state, timeout_s)

        # 1. Send transitions in parallel (fire)
        tasks = [
            self._broadcast_to_node(node_name, client, transition_id)
            for node_name, client in self._sil_change_state_clients.items()
        ]
        await asyncio.gather(*tasks, return_exceptions=True)

        # 2. Wait for all nodes to report target_state (poll)
        deadline = asyncio.get_event_loop().time() + timeout_s
        failed_nodes: list[str] = []

        for node_name, client in self._sil_change_state_clients.items():
            while asyncio.get_event_loop().time() < deadline:
                try:
                    if not client.wait_for_service(timeout_sec=0.5):
                        await asyncio.sleep(0.5)
                        continue
                    req = GetState.Request()
                    future = client.call_async(req)
                    # Resolve the future with a short deadline
                    state_deadline = 10  # 10 × 0.1s = 1s per attempt
                    while not future.done() and state_deadline > 0:
                        await asyncio.sleep(0.1)
                        state_deadline -= 1
                    if not future.done():
                        continue
                    current = future.result().current_state.label
                    if current == target_state:
                        _log.info("[broadcast] %s → %s ✓", node_name, target_state)
                        break  # this node done
                except Exception:
                    await asyncio.sleep(0.5)
            else:
                # Timeout for this node
                failed_nodes.append(node_name)
                _log.error("[broadcast] %s did NOT reach '%s' within %.0fs", node_name, target_state, timeout_s)

        if failed_nodes:
            # Fail-loud: dump diagnostic info
            _log.error("[broadcast] %d/%d nodes failed: %s",
                       len(failed_nodes), len(self._sil_change_state_clients), ", ".join(failed_nodes))
            return LifecycleResult(
                success=False,
                error=f"Broadcast timeout: {len(failed_nodes)} nodes stuck before '{target_state}': {', '.join(failed_nodes)}")
        return LifecycleResult(success=True)

    async def configure(self, scenario_id: str) -> LifecycleResult:
        # Always sync with the real ROS2 state first — the Python mirror can be
        # stale after an orchestrator restart (node stays active, bridge resets).
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
            # Explicit sync: wait for all SIL nodes to reach 'inactive' before returning.
            # This replaces the old asyncio.create_task() fire-and-forget pattern (line 203)
            # which allowed configure() to return before ship_dynamics was ready.
            broadcast_result = await self._broadcast_and_wait(
                Transition.TRANSITION_CONFIGURE, "inactive", timeout_s=15.0)
            if not broadcast_result.success:
                return broadcast_result
        return res
```

- [ ] **Step 2: Verify Python syntax**

```bash
python3 -m py_compile src/sil_orchestrator/lifecycle_bridge.py
```

Expected: no syntax errors.

- [ ] **Step 3: Commit Task 3**

```bash
git add src/sil_orchestrator/lifecycle_bridge.py
git commit -m "fix: replace asyncio fire-and-forget with explicit broadcast-and-wait in lifecycle_bridge.configure()"
```

---

### Task 4: Logging & Diagnostics Enhancement

**Files:**
- Modify: `docker/sil_entrypoint.sh` (timestamps already added in Task 2)
- Modify: `src/sil_orchestrator/lifecycle_bridge.py` (diagnostic dump on failure — already in Task 3)
- Modify: `scripts/verify_demo1_e2e.sh` (add early-stage verification checks)

**Rollback:** `git checkout --` each file individually.

**Design:** The staged entrypoint rewrite (Task 2) already includes timestamps at each stage transition, node active counts, and diagnostic dumps on failure. The lifecycle_bridge rewrite (Task 3) already fails loud with node names. This task adds:
1. Enriched `verify_demo1_e2e.sh` with per-stage time checks (≤30s to ALL 16+ nodes active)
2. Early failure detection: if /sil/own_ship_state not publishing within 30s, exit early
3. `ros2 node info` dump for failed nodes

- [ ] **Step 1: Enhance verify_demo1_e2e.sh with staged checks**

Modify `scripts/verify_demo1_e2e.sh` (replace lines 31-45 and add new checks):

```bash
echo ""; echo "[1/3] Stage check: /sil/own_ship_state first frame (max 30s)..."
STAGE1_ELAPSED=0
STAGE1_GOT_FRAME=0
while [ $STAGE1_ELAPSED -lt 30 ]; do
    if docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic hz /sil/own_ship_state --window 3 2>&1 | grep -q 'average rate'" 2>/dev/null; then
        echo -e "  ${GREEN}/sil/own_ship_state publishing after ${STAGE1_ELAPSED}s${NC}"
        STAGE1_GOT_FRAME=1
        break
    fi
    sleep 2; STAGE1_ELAPSED=$((STAGE1_ELAPSED + 2))
done
if [ "$STAGE1_GOT_FRAME" -eq 0 ]; then
    echo -e "${RED}FAIL: /sil/own_ship_state not publishing after 30s${NC}"
    echo "=== Diagnostic: container logs (last 50 lines) ==="
    docker compose logs --tail=50 sil-nodes 2>/dev/null || true
    echo "=== Diagnostic: node list ==="
    docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    exit 1
fi

echo ""; echo "[2/3] Waiting for all 16+ nodes active (max ${TIMEOUT}s)..."
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    NODE_COUNT=$(docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list 2>/dev/null | wc -l" 2>/dev/null || echo "0")
    if [ "$NODE_COUNT" -ge 16 ]; then
        echo -e "  ${GREEN}$NODE_COUNT nodes detected — system ready${NC}"; break
    fi
    echo "  $NODE_COUNT nodes (need >=16) ... waiting 5s"
    sleep 5; ELAPSED=$((ELAPSED + 5))
done
if [ "$NODE_COUNT" -lt 16 ]; then
    echo -e "${RED}FAIL: Only $NODE_COUNT nodes after ${TIMEOUT}s${NC}"
    docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list" 2>/dev/null || true
    # Dump detailed node info for debugging
    echo ""; echo "=== Diagnostic: node info for each node ==="
    for node_name in m1_odd_manager m2_world_model m3_mission_manager m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor m8_hmi_bridge ship_dynamics_node; do
        echo "--- $node_name ---"
        docker compose exec -T "$CONTAINER" bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node info /$node_name 2>&1 || echo 'NOT FOUND'" 2>/dev/null || true
    done
    exit 1
fi

echo ""; echo "[3/3] Running verification checks..."
```

Then update the existing check numbering: `[2/2]` becomes `[3/3]`, and the check labels remain at lines 49-78.

Note: the original check `[2/2] Running verification checks...` at line 47 must change to `[3/3]`. Keep checks 1-5 unchanged (lines 49-65).

- [ ] **Step 2: Verify bash syntax**

```bash
bash -n scripts/verify_demo1_e2e.sh
```

Expected: no syntax errors.

- [ ] **Step 3: Commit Task 4**

```bash
git add scripts/verify_demo1_e2e.sh
git commit -m "feat: enrich verify_demo1_e2e.sh with staged early checks and ros2 node info diagnostics"
```

---

## Verification Checklist (Post-Implementation)

Run these after all 4 tasks are merged:

- [ ] `bash -n scripts/wait_for_topic.sh` — no syntax errors
- [ ] `bash -n docker/sil_entrypoint.sh` — no syntax errors
- [ ] `python3 -m py_compile src/sil_orchestrator/lifecycle_bridge.py` — no syntax errors
- [ ] `docker compose build sil-nodes` — builds without error
- [ ] `docker compose up sil-nodes` — watch first 60s logs:
  - [ ] Stage 1/3 timestamp printed
  - [ ] Stage 2/3 timestamp + "/sil/own_ship_state received" message
  - [ ] Stage 3/3 timestamp + L3 node creation messages
  - [ ] No "no message received" or "subscription queue empty" warnings
- [ ] `scripts/verify_demo1_e2e.sh` — 3 consecutive passes (exit code 0)
- [ ] Kill `ship_dynamics_node` mid-run: verify entrypoint exits with code ≠ 0 (fail-loud)
- [ ] Check `docker compose logs sil-nodes` for per-stage timestamps and node counts

---

## Execution

**Plan complete and saved to `docs/superpowers/plans/2026-05-20-sil-staged-entrypoint-race-fix.md`.**

Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks
2. **Inline Execution** — execute tasks in this session

**Recommendation:** Subagent-driven — Tasks 1 (probe script) and 3 (lifecycle_bridge) are fully independent. Task 2 (entrypoint rewrite) can proceed in parallel with Task 3 once the wait_for_topic.sh API is defined. Task 4 (verify script) depends on Tasks 1+2 being done.
