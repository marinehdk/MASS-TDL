# Phase 1 Gap Closure — DEMO-1 & DEMO-2 Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan track-by-track. Each track is independently executable. Tracks A, D, F can start immediately in parallel. Tracks B, C, E depend on Track A.

**Goal:** Close all gaps preventing DEMO-1 (6/15 Skeleton Live) and DEMO-2 (7/31 Decision-Capable), organized into 6 parallel tracks for subagent dispatch.

**Architecture:** The project has 8 C++ ROS2 modules (M1-M8 under `src/l3_tdl_kernel/`), a SIL Python orchestrator (`src/sil_orchestrator/`), a sim_workbench with 9 ROS2 lifecycle nodes (`src/sim_workbench/`), a React Web HMI (`web/`), Docker compose for orchestration, and 31 Imazu+COLREGs YAML scenarios (`scenarios/`). The critical path to DEMO-1 is: compile all modules → integrate into SIL → run 1 scenario end-to-end → display in Web HMI.

**Tech Stack:** ROS2 Humble (Ubuntu 22.04), C++17 (MISRA), Python 3.10 (orchestrator/nodes), React/TypeScript/MapLibre GL JS (HMI), Docker compose, colcon build

**Current State (2026-05-19):**
- ✅ SIL infrastructure: orchestrator (50 .py), sim_workbench (9 nodes), Docker compose, foxglove_bridge, martin tileserver
- ✅ Scenarios: 22/22 Imazu + 9 COLREGs YAML with SHA256 manifest
- ✅ Web HMI: 4 screens, MapLibre ENC integration, 14/15 D1.3b.3 DoD items checked
- ✅ M1-M8 C++ source code exists, M4/M6/M7 imported as snapshots from Phase 3 branches
- ⚠️ colcon build only covers m7 + l3_msgs + sil packages (M1-M6, M8 not in build)
- ⚠️ M1-M8 modules not integrated into SIL lifecycle
- ❌ End-to-end scenario run not verified
- ❌ 47 modified files in working tree not committed

---

## Dependency Graph

```
Track A (Build & Compile) ──────────────┐
Track D (Documentation) ── independent   │
Track F (CI/Docker) ─────── independent  │
                                         ├──→ Track B (SIL Integration)
                                         ├──→ Track C (Module Validation)
                                         └──→ Track E (Web HMI Data)
                                                  │
                                                  └──→ DEMO-1 (6/15)
                                                       │
                                                  DEMO-2 (7/31)
```

---

## Track A: Build & Compile — colcon build All 8 Modules

**Entry:** None (can start immediately)
**Exit:** `colcon build` green for all M1-M8 + common + l3_msgs + l3_external_msgs
**Parallel with:** Tracks D, F

### A.1: Audit M1-M8 Build Infrastructure

**Files to inspect:**
- `src/l3_tdl_kernel/m1_odd_envelope_manager/CMakeLists.txt`
- `src/l3_tdl_kernel/m2_world_model/CMakeLists.txt`
- `src/l3_tdl_kernel/m3_mission_manager/CMakeLists.txt`
- `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`
- `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`
- `src/l3_tdl_kernel/m6_colregs_reasoner/CMakeLists.txt`
- `src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/CMakeLists.txt`
- `src/l3_tdl_kernel/common/CMakeLists.txt`
- `src/l3_tdl_kernel/l3_msgs/CMakeLists.txt`
- `src/l3_tdl_kernel/l3_external_msgs/CMakeLists.txt`

- [ ] **Step 1: Verify CMakeLists.txt exists for every module**

```bash
for m in m1 m2 m3 m4 m5 m6 m7 m8 common l3_msgs l3_external_msgs; do
  f="src/l3_tdl_kernel/${m}_*/CMakeLists.txt"
  if ls $f 2>/dev/null; then echo "  ✅ $m"; else echo "  ❌ $m MISSING"; fi
done
```

- [ ] **Step 2: Verify package.xml exists for every module**

```bash
for m in m1 m2 m3 m4 m5 m6 m7 m8; do
  f="src/l3_tdl_kernel/${m}_*/package.xml"
  if ls $f 2>/dev/null; then echo "  ✅ $m"; else echo "  ❌ $m MISSING"; fi
done
```

- [ ] **Step 3: Check for COLCON_IGNORE files blocking builds**

```bash
find src/l3_tdl_kernel -name COLCON_IGNORE -exec echo "  ⚠️ BLOCKED: {}" \;
```

- [ ] **Step 4: List all build dependencies across all package.xml files**

```bash
grep -h '<depend>' src/l3_tdl_kernel/*/package.xml | sort -u
```

- [ ] **Step 5: Report findings with a table: module | CMakeLists | package.xml | COLCON_IGNORE | dependency count**

Expected: All 8 modules have CMakeLists.txt and package.xml. No unexpected COLCON_IGNORE files.

- [ ] **Step 6: Commit audit results**

```bash
git add docs/superpowers/plans/
git commit -m "docs: M1-M8 build infrastructure audit results"
```

### A.2: Fix M1 Build (ODD/Envelope Manager)

**Files to create/modify:**
- `src/l3_tdl_kernel/m1_odd_envelope_manager/CMakeLists.txt` (verify/fix)
- `src/l3_tdl_kernel/m1_odd_envelope_manager/package.xml` (verify/fix dependencies)
- Source files in `src/l3_tdl_kernel/m1_odd_envelope_manager/src/`

- [ ] **Step 1: Verify CMakeLists.txt compiles m1 sources**

Read the CMakeLists.txt. It should create a library or executable from:
- `src/odd_envelope_manager_node.cpp`
- `src/odd_state_machine.cpp`
- `src/conformance_score_calculator.cpp`
- `src/tmr_tdl_estimator.cpp`
- `src/mrc_trigger_logic.cpp`
- `src/parameter_loader.cpp`
- `src/main.cpp`

Check: `ament_cmake` build type, correct `find_package` for `rclcpp`, `l3_msgs`, `l3_external_msgs`.

- [ ] **Step 2: Fix include paths**

Headers are in `include/m1_odd_envelope_manager/`. Verify source files use:
```cpp
#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"
```
NOT bare `#include "odd_envelope_manager_node.hpp"`.

Run: `grep -r '#include "' src/l3_tdl_kernel/m1_odd_envelope_manager/src/ | grep -v 'm1_odd_envelope_manager/'`

If any bare includes found, fix them to use the full path.

- [ ] **Step 3: Attempt colcon build M1 only**

```bash
colcon build --packages-select m1_odd_envelope_manager --event-handlers console_direct+
```

- [ ] **Step 4: Fix any compilation errors iteratively**

For each error:
1. Read the file at the error line
2. Identify root cause (missing include, wrong type, missing dependency)
3. Apply minimal fix
4. Rebuild

Common expected issues:
- Missing `#include "l3_msgs/msg/odd_state.hpp"` (needs l3_msgs built first)
- Missing `ament_index_cpp` dependency in package.xml
- `std::chrono` usage without `#include <chrono>`

- [ ] **Step 5: Verify M1 builds clean**

```bash
colcon build --packages-select m1_odd_envelope_manager 2>&1 | tail -5
```
Expected: "Summary: 1 package finished" with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m1_odd_envelope_manager/
git commit -m "fix(m1): resolve build errors for ODD/Envelope Manager"
```

### A.3: Fix M2 Build (World Model)

**Files:**
- `src/l3_tdl_kernel/m2_world_model/CMakeLists.txt`
- `src/l3_tdl_kernel/m2_world_model/package.xml`
- Sources: `src/world_model_node.cpp`, `src/cpa_tcpa_calculator.cpp`, `src/enc_loader.cpp`, `src/coord_transform.cpp`, `src/view_health_monitor.cpp`, `src/error.cpp`, `src/main.cpp`

- [ ] **Step 1: Fix CMakeLists.txt — ensure it links against common + l3_msgs**

```cmake
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
ament_target_dependencies(m2_world_model_node rclcpp l3_msgs l3_external_msgs)
```

- [ ] **Step 2: Fix include paths**

Run: `grep -r '#include "' src/l3_tdl_kernel/m2_world_model/src/ | grep -v m2_world_model`

- [ ] **Step 3: Build M2 only**

```bash
colcon build --packages-select m2_world_model --event-handlers console_direct+
```

Expected issue: ENC loader depends on GDAL or PROJ — check if these are available or need mock.

- [ ] **Step 4: Fix any compilation errors, rebuild until green**

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m2_world_model/
git commit -m "fix(m2): resolve build errors for World Model"
```

### A.4: Fix M3 Build (Mission Manager)

**Files:**
- `src/l3_tdl_kernel/m3_mission_manager/CMakeLists.txt`
- `src/l3_tdl_kernel/m3_mission_manager/package.xml`
- Sources: 7 .cpp files in `src/`

- [ ] **Step 1: Verify CMakeLists.txt includes all 7 source files**
- [ ] **Step 2: Fix include paths**
- [ ] **Step 3: Build M3 only**: `colcon build --packages-select m3_mission_manager`
- [ ] **Step 4: Fix errors iteratively**
- [ ] **Step 5: Commit**: `git commit -m "fix(m3): resolve build errors for Mission Manager"`

### A.5: Fix M4 Build (Behavior Arbiter)

**Files:**
- `src/l3_tdl_kernel/m4_behavior_arbiter/CMakeLists.txt`
- Sources: 8 .cpp files (ivp_solver, ivp_function, ivp_domain, ivp_combine, behavior_dictionary, behavior_activation, behavior_priority, behavior_arbiter_node, main)

- [ ] **Step 1: Verify CMakeLists.txt**
- [ ] **Step 2: Note: M4 has `.salvage-d3.1/` directory — ensure it's excluded from build**
- [ ] **Step 3: Build M4 only**: `colcon build --packages-select m4_behavior_arbiter`
- [ ] **Step 4: Fix errors**
- [ ] **Step 5: Commit**

### A.6: Fix M5 Build (Tactical Planner)

**Files:**
- `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`
- Sources: 18 .cpp files across `src/mid_mpc/`, `src/bc_mpc/`, `src/shared/`, `src/common/`

- [ ] **Step 1: M5 is the most complex module with 3 subdirectories. Verify CMakeLists.txt compiles ALL source files.**
- [ ] **Step 2: Check for CasADi/IPOPT dependencies — these may not be available. If not, check if there's a mock/stub mode.**
- [ ] **Step 3: Build M5 only**: `colcon build --packages-select m5_tactical_planner`
- [ ] **Step 4: Fix errors. Expected: CasADi headers not found → add conditional compilation or mock.**
- [ ] **Step 5: Commit**

### A.7: Fix M6 Build (COLREGs Reasoner)

**Files:**
- `src/l3_tdl_kernel/m6_colregs_reasoner/CMakeLists.txt`
- Sources: 18 .cpp files across `src/` and `src/rules/colregs/`

- [ ] **Step 1: Verify CMakeLists.txt includes all rule .cpp files**
- [ ] **Step 2: Build M6 only**: `colcon build --packages-select m6_colregs_reasoner`
- [ ] **Step 3: Fix errors**
- [ ] **Step 4: Commit**

### A.8: Fix M7 Build (Safety Supervisor)

**Files:**
- `src/l3_tdl_kernel/m7_safety_supervisor/CMakeLists.txt`
- Sources: 4 .cpp files + `src/sotif/` subdirectory

**Note:** M7 is already in the colcon build (per `build/` directory). Verify it's still building clean.

- [ ] **Step 1: Rebuild M7**: `colcon build --packages-select m7_safety_supervisor`
- [ ] **Step 2: If broken, fix and commit**

### A.9: Fix M8 Build (HMI Transparency Bridge)

**Files:**
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/CMakeLists.txt`
- Sources: 10 .cpp files

- [ ] **Step 1: Verify CMakeLists.txt. M8 has both C++ node AND Python web server. Ensure both build targets exist.**
- [ ] **Step 2: Build M8 only**: `colcon build --packages-select m8_hmi_transparency_bridge`
- [ ] **Step 3: Fix errors**
- [ ] **Step 4: Commit**

### A.10: Full Workspace Build Verification

- [ ] **Step 1: Clean build**

```bash
rm -rf build/ install/ log/
colcon build --event-handlers console_direct+ 2>&1 | tee build.log
```

- [ ] **Step 2: Check summary**

```bash
tail -20 build.log
```
Expected: All packages built successfully. Zero failures.

- [ ] **Step 3: Run all C++ unit tests**

```bash
colcon test --event-handlers console_direct+
colcon test-result --verbose
```

- [ ] **Step 4: Commit build log**

```bash
cp build.log docs/superpowers/plans/build-log-$(date +%Y%m%d).log
git add docs/superpowers/plans/
git commit -m "build: full colcon workspace build verified green"
```

---

## Track B: SIL End-to-End Integration

**Entry:** Track A complete (all modules build)
**Exit:** Run 1 Imazu scenario end-to-end: YAML → orchestrator → sil_nodes + M1-M8 → foxglove → Web HMI
**Parallel with:** Track C, Track E (after A)

### B.1: Wire M1-M8 Modules into SIL Lifecycle

**Files to modify:**
- `src/sil_orchestrator/lifecycle_bridge.py:24-33` (add M1-M8 node names)
- `docker/sil_entrypoint.sh:17-36` (add M1-M8 node imports and instantiation)

**Current state:** `_SIL_LIFECYCLE_NODES` list contains 8 SIL mock nodes (ship_dynamics, env_disturbance, target_vessel, sensor_mock, tracker_mock, fault_injection, scoring, scenario_authoring). M1-M8 modules are NOT in this list.

- [ ] **Step 1: Add M1-M8 node names to lifecycle bridge**

In `src/sil_orchestrator/lifecycle_bridge.py`, add to `_SIL_LIFECYCLE_NODES`:

```python
_SIL_LIFECYCLE_NODES = [
    # SIL sim nodes
    "ship_dynamics_node",
    "env_disturbance_node",
    "target_vessel_node",
    "sensor_mock_node",
    "tracker_mock_node",
    "fault_injection_node",
    "scoring_node",
    "scenario_authoring_node",
    # L3 TDL modules
    "odd_envelope_manager_node",
    "world_model_node",
    "mission_manager_node",
    "behavior_arbiter_node",
    "tactical_planner_node",
    "colregs_reasoner_node",
    "safety_supervisor_node",
    "hmi_transparency_bridge_node",
]
```

- [ ] **Step 2: Add M1-M8 node imports to sil_entrypoint.sh**

In `docker/sil_entrypoint.sh`, add imports after existing SIL node imports:

```python
from m1_odd_envelope_manager.odd_envelope_manager_node import OddEnvelopeManagerNode
from m2_world_model.world_model_node import WorldModelNode
from m3_mission_manager.mission_manager_node import MissionManagerNode
from m4_behavior_arbiter.behavior_arbiter_node import BehaviorArbiterNode
# Note: M5 has separate mid_mpc and bc_mpc nodes
from m5_tactical_planner.mid_mpc.mid_mpc_node import MidMpcNode
from m5_tactical_planner.bc_mpc.bc_mpc_node import BcMpcNode
from m6_colregs_reasoner.colregs_reasoner_node import ColregsReasonerNode
from m7_safety_supervisor.safety_supervisor_node import SafetySupervisorNode
from m8_hmi_transparency_bridge.hmi_transparency_bridge_node import HmiTransparencyBridgeNode
```

Add to `node_classes` list after existing SIL nodes.

- [ ] **Step 3: Verify import paths are correct**

For each module, check the actual Python import path:
```bash
grep -r "class.*Node" src/l3_tdl_kernel/m*_*/src/*.cpp | head -20
```
Note: These are C++ nodes. The Python entrypoint script instantiates them via `rclpy` — but C++ nodes need to be launched differently (via `ros2 run` or composed nodes). 

**Critical finding:** C++ ROS2 nodes cannot be imported in Python like Python nodes. They must be:
- Launched via `ros2 run <package> <executable>` 
- Or loaded as composed nodes via `rclcpp_components`

- [ ] **Step 4: Create a ROS2 launch file for M1-M8 nodes**

Create `launch/l3_tdl_nodes.launch.py`:

```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(package='m1_odd_envelope_manager', executable='odd_envelope_manager_node', name='odd_envelope_manager_node'),
        Node(package='m2_world_model', executable='world_model_node', name='world_model_node'),
        Node(package='m3_mission_manager', executable='mission_manager_node', name='mission_manager_node'),
        Node(package='m4_behavior_arbiter', executable='behavior_arbiter_node', name='behavior_arbiter_node'),
        Node(package='m5_tactical_planner', executable='mid_mpc_node', name='mid_mpc_node'),
        Node(package='m5_tactical_planner', executable='bc_mpc_node', name='bc_mpc_node'),
        Node(package='m6_colregs_reasoner', executable='colregs_reasoner_node', name='colregs_reasoner_node'),
        Node(package='m7_safety_supervisor', executable='safety_supervisor_node', name='safety_supervisor_node'),
        Node(package='m8_hmi_transparency_bridge', executable='hmi_transparency_bridge_node', name='hmi_transparency_bridge_node'),
    ])
```

- [ ] **Step 5: Update docker-compose.yml to include l3_tdl_nodes launch**

In `docker-compose.yml`, add a new service or modify `sil-nodes` command to also launch M1-M8:

```yaml
  l3-tdl-nodes:
    build:
      context: .
      dockerfile: docker/sil_nodes.Dockerfile
    command: >
      bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 launch l3_tdl_nodes.launch.py"
    environment:
      - ROS_DOMAIN_ID=0
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
    network_mode: host
```

- [ ] **Step 6: Commit**

```bash
git add launch/ src/sil_orchestrator/lifecycle_bridge.py docker-compose.yml
git commit -m "feat(sil): wire M1-M8 modules into SIL lifecycle and Docker compose"
```

### B.2: Verify DDS Topic Wiring Between Modules

- [ ] **Step 1: Map expected topic flow from architecture doc §4.2**

Expected topic flow (from architecture v1.1.3-pre-stub §4.2):
```
M1 → /l3/odd_state → M2, M3, M4, M5, M6, M7, M8
M2 → /l3/world_state → M3, M4, M5, M6
M4 → /l3/behavior_plan → M5
M6 → /l3/colregs_constraints → M5
M5 → /l3/avoidance_plan → L4
M7 → /l3/safety_alert → M1
M7 → /l3/checker_veto → M5
```

- [ ] **Step 2: For each module, verify it publishes/subscribes to correct topics**

```bash
# Check M1 publishes odd_state
grep -r "odd_state\|ODD_StateMsg" src/l3_tdl_kernel/m1_odd_envelope_manager/src/

# Check M2 publishes world_state
grep -r "world_state\|World_StateMsg" src/l3_tdl_kernel/m2_world_model/src/

# Check M6 subscribes to world_state
grep -r "world_state\|World_StateMsg" src/l3_tdl_kernel/m6_colregs_reasoner/src/
```

- [ ] **Step 3: Document any topic name mismatches**

Create `docs/superpowers/plans/topic-wiring-audit.md` with a table:
```
| Publisher | Topic | Subscriber | Match? |
|-----------|-------|------------|--------|
| M1 | ??? | M2 | ??? |
```

- [ ] **Step 4: Fix any mismatches** — ensure consistent topic names across all modules

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(sil): align DDS topic wiring across M1-M8 modules"
```

### B.3: Run First End-to-End Scenario

**Precondition:** Docker compose running with all services, M1-M8 nodes publishing, Web HMI connected.

- [ ] **Step 1: Start the full stack**

```bash
docker compose up -d
```

- [ ] **Step 2: Wait for all services healthy**

```bash
docker compose ps
```
Expected: 5 services healthy (sil-orchestrator, sil-nodes, foxglove-bridge, web, martin-tile-server)

- [ ] **Step 3: Run gate check**

```bash
curl -X POST http://localhost:8000/api/v1/gates/run-all?scenario_id=imazu-01-ho-v1.0
```
Expected: JSON response with 6 gate results.

- [ ] **Step 4: Load scenario via API**

```bash
curl -X POST http://localhost:8000/api/v1/scenarios/load \
  -H "Content-Type: application/json" \
  -d '{"scenario_id": "imazu-01-ho-v1.0"}'
```
Expected: 200 OK with scenario data.

- [ ] **Step 5: Activate SIL lifecycle**

```bash
curl -X POST http://localhost:8000/api/v1/lifecycle/activate
```
Expected: All nodes transition to ACTIVE state.

- [ ] **Step 6: Verify DDS data flow**

```bash
# In sil-nodes container
docker compose exec sil-nodes bash -c "source /opt/ros/humble/setup.bash && ros2 topic list"
```
Expected: Topics include `/sim_clock`, `/sil/own_ship_state`, `/sil/target_vessel_state`, `/l3/world_state`, `/l3/colregs_constraints`, etc.

- [ ] **Step 7: Open Web HMI and verify live data**

Open `http://localhost:5173` in browser.
Expected: MapLibre map loads, own-ship visible, target ship visible, CPA/TCPA updating.

- [ ] **Step 8: Run scenario for 60 seconds, verify output**

```bash
curl -X POST http://localhost:8000/api/v1/runs/start \
  -H "Content-Type: application/json" \
  -d '{"scenario_id": "imazu-01-ho-v1.0", "duration_s": 60}'
```

- [ ] **Step 9: Collect scoring output**

```bash
curl http://localhost:8000/api/v1/scoring/imazu-01-ho-v1.0/latest
```
Expected: Hagen 6-dimension scores + PASS/FAIL verdict.

- [ ] **Step 10: Commit end-to-end test evidence**

```bash
git add runs/ docs/superpowers/plans/
git commit -m "test(sil): first end-to-end Imazu-01 HO scenario verified"
```

### B.4: Fix Gate Runner Stubs

**File:** `src/sil_orchestrator/gate_runner.py`

Gates 2-6 currently have stub implementations. Need to wire real checks.

- [ ] **Step 1: Implement gate_2_module_health** (lines 279+)

Read current implementation. It needs to:
1. Check M1-M8 module pulse states from DDS topics
2. Verify M7 process independence (different PID/container)
3. Check M8 WebSocket connection

- [ ] **Step 2: Implement gate_3_scenario_integrity**

Read current implementation (line ~350+). It needs to:
1. Check SHA256 of scenario YAML against frozen manifest
2. Validate YAML schema (maritime-schema or internal)
3. Verify vessel_class matches FCB capability manifest

- [ ] **Step 3: Implement gate_4_odd_alignment**

Read current implementation. It needs to:
1. Extract ODD cell from scenario metadata
2. Verify own-ship initial state is within ODD envelope
3. Check CAPABILITY_MANIFEST allows the scenario's speed/domain

- [ ] **Step 4: Implement gate_5_time_base**

Read current implementation. It needs to:
1. Verify `/sim_clock` is publishing
2. Check clock monotonicity (no backward jumps)
3. Verify stamp alignment across topics

- [ ] **Step 5: Implement gate_6_doer_checker**

This gate references `src/sil_orchestrator/checker_verification.py`. Verify:
1. M7 imports are independent of M1-M6 (no shared headers)
2. M7 PID/container is different from M5
3. M7 VETO topic exists on DDS

- [ ] **Step 6: Run all 6 gates and verify all PASS**

```bash
curl -X POST http://localhost:8000/api/v1/gates/run-all?scenario_id=imazu-01-ho-v1.0 | jq .
```

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(sil): implement gate runner stubs for gates 2-6"
```

---

## Track C: Module Architecture Alignment

**Entry:** Track A complete
**Exit:** All 8 modules verified against v1.1.3-pre-stub architecture, interface mismatches documented and fixed
**Parallel with:** Track B, Track E

### C.1: Verify M1 (ODD Manager) Against Architecture §5

**Architecture requirements (§5):**
- 5 sub-modules: Envelope Evaluator, Mode FSM, TMR/TDL Calculator, MRC Controller, Confidence Gating
- ODD_StateMsg published at 1 Hz with fields: odd_zone, auto_level, conformance_score, tmr_s, tdl_s
- Capability Manifest loading from YAML
- Three orthogonal state dimensions: AutoLevel, ODDZone, Health

**Files:**
- `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp`
- `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_state_machine.cpp`
- `src/l3_tdl_kernel/m1_odd_envelope_manager/src/conformance_score_calculator.cpp`

- [ ] **Step 1: Read M1 source files, map each sub-module to architecture §5.3.1**
- [ ] **Step 2: Check ODD_StateMsg field alignment**

```bash
cat src/l3_tdl_kernel/l3_msgs/msg/ODD_StateMsg.msg
```

Expected fields from §5.4: `odd_zone`, `auto_level`, `conformance_score`, `tmr_s`, `tdl_s`, `stamp`, `confidence`, `schema_version`.

- [ ] **Step 3: Check Capability Manifest loading from §5.3.3**

Grep for YAML parsing in M1 source:
```bash
grep -r "capability\|manifest\|yaml\|YAML" src/l3_tdl_kernel/m1_odd_envelope_manager/src/
```

- [ ] **Step 4: Document gaps in `docs/superpowers/plans/m1-gap-report.md`**
- [ ] **Step 5: Fix critical gaps (missing fields, wrong topic names)**
- [ ] **Step 6: Commit**

### C.2: Verify M2 (World Model) Against Architecture §6

**Architecture requirements (§6):**
- 3 independent views: Static (ENC), Dynamic (targets + COLREG pre-classification), Ego (own-ship)
- World_StateMsg @ 4 Hz with confidence ∈ [0,1] per field
- CPA/TCPA calculated internally
- OVERTAKING sector [112.5°, 247.5°] (D0.1 MUST-1 fix)

**Files:**
- `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp`
- `src/l3_tdl_kernel/m2_world_model/src/cpa_tcpa_calculator.cpp`

- [ ] **Step 1: Verify 3-view architecture**

Check if world_model_node outputs separate topics or aggregated World_StateMsg.

- [ ] **Step 2: Verify OVERTAKING sector is [112.5°, 247.5°]**

```bash
grep -n "112.5\|247.5\|overtaking" src/l3_tdl_kernel/m2_world_model/src/*
```

Expected: MUST-1 fix from D0.1 should be in place. If found at old values [100°, 260°], fix immediately.

- [ ] **Step 3: Check confidence field presence**

```bash
grep -n "confidence" src/l3_tdl_kernel/m2_world_model/src/*
```

- [ ] **Step 4: Document gaps, fix, commit**

### C.3: Verify M3 (Mission Manager) Against Architecture §7

**Architecture requirements (§7):**
- Subscribes to L1 VoyageTask + L2 PlannedRoute
- Validates feasibility, does NOT do waypoint planning
- ETA projection, ODD monitoring → RouteReplanRequest to L2

- [ ] **Step 1: Verify M3 subscribes to correct external topics**

```bash
grep -r "subscription\|create_subscription\|VoyageTask\|PlannedRoute" src/l3_tdl_kernel/m3_mission_manager/src/
```

- [ ] **Step 2: Verify M3 does NOT contain waypoint planning logic**

```bash
grep -r "waypoint\|WP\|path_plan\|A\*\|RRT" src/l3_tdl_kernel/m3_mission_manager/src/
```
Expected: No waypoint planning code (that's L2's job).

- [ ] **Step 3: Document gaps, fix, commit**

### C.4: Verify M4 (Behavior Arbiter) Against Architecture §8

**Architecture requirements (§8):**
- IvP (Interval Programming) multi-objective optimization
- Behavior dictionary with 8 behaviors (Transit, COLREGs_Avoidance, Restricted_Visibility, Channel_Following, Approach, DP_Hold, Crew_Transfer_Standby, MRC_Drift)
- Each behavior has ODD applicability + priority weight
- Outputs behavior_plan to M5

**Files:**
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_dictionary.cpp`
- `src/l3_tdl_kernel/m4_behavior_arbiter/src/ivp_solver.cpp`

- [ ] **Step 1: Verify all 8 behaviors in dictionary**

```bash
grep -n "Transit\|COLREGs_Avoidance\|Restricted_Visibility\|Channel_Following\|Approach\|DP_Hold\|Crew_Transfer\|MRC_Drift" src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_dictionary.cpp
```

- [ ] **Step 2: Verify behavior weights from architecture §8.3**

Expected: Transit=0.3, COLREGs_Avoidance=0.7, Restricted_Visibility=0.6, Channel_Following=0.5, Approach=0.4, DP_Hold=0.8, Crew_Transfer_Standby=0.5, MRC_Drift=1.0

```bash
grep -n "weight\|priority" src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_dictionary.cpp
```

- [ ] **Step 3: Check for `if vessel == FCB` anti-pattern**

```bash
grep -rn "FCB\|45m\|18 kn\|22 kn" src/l3_tdl_kernel/m4_behavior_arbiter/src/
```
Expected: ZERO matches. Architecture §13: "严禁 if vessel==FCB 潜入 A 层".

- [ ] **Step 4: Document gaps, fix, commit**

### C.5: Verify M5 (Tactical Planner) Against Architecture §10

**Architecture requirements (§10):**
- Double-layer MPC: Mid-MPC (60-120s horizon, 1-2 Hz) + BC-MPC (10-30s horizon, 4-10 Hz)
- Mid-MPC: N=18 timesteps, 90s horizon (D0.1 MUST-2 fix)
- BC-MPC: k-branch candidate headings, worst-case CPA maximization
- ROT_max from Capability Manifest, not hardcoded
- FM-4: OUT_of_ODD → MRM (not hardcoded fallback) (D0.1 MUST-5 fix)

**Files:**
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_solver.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/shared/capability_manifest.cpp`

- [ ] **Step 1: Verify Mid-MPC N=18, horizon=90s**

```bash
grep -n "N\|horizon\|90\|18" src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp
```

- [ ] **Step 2: Check for hardcoded FCB constants**

```bash
grep -rn "FCB\|45m\|8.0\|ROT_max\s*=" src/l3_tdl_kernel/m5_tactical_planner/src/
```
Expected: ZERO matches (per D0.1 multi-vessel cleanup).

- [ ] **Step 3: Verify FM-4 MRM trigger (not hardcoded fallback)**

```bash
grep -n "FM-4\|fallback\|OUT_of_ODD\|MRC\|MRM" src/l3_tdl_kernel/m5_tactical_planner/src/
```

- [ ] **Step 4: Document gaps, fix, commit**

### C.6: Verify M6 (COLREGs Reasoner) Against Architecture §9

**Architecture requirements (§9):**
- 5-layer rule reasoning (applicable rules → encounter classification → responsibility → action → timing)
- Rule 13-17, 18, 19 coverage
- ODD-aware parameters (T_standOn, T_act different per ODD)
- Per-rule unit tests

**Files:**
- `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
- `src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule*.cpp`

- [ ] **Step 1: Verify all 8 rules implemented**

```bash
ls src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule*.cpp
```
Expected: rule5_lookout, rule6_safe_speed, rule7_risk_of_collision, rule8_action_to_avoid, rule13_overtaking, rule14_head_on, rule15_crossing, rule16_give_way, rule17_stand_on, rule18_responsibilities, rule19_restricted_visibility

- [ ] **Step 2: Verify ODD-aware parameters**

```bash
grep -n "odd\|ODD\|T_standOn\|T_act" src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp
```

- [ ] **Step 3: Count unit tests**

```bash
ls src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule*.cpp | wc -l
```
Expected: ≥ 10 test files.

- [ ] **Step 4: Document gaps, fix, commit**

### C.7: Verify M7 (Safety Supervisor) Against Architecture §11

**Architecture requirements (§11):**
- Doer-Checker: M7 independent path, no shared code with M1-M6
- Pre-defined MRM command set (no dynamic trajectory generation)
- SOTIF assumption monitoring + IEC 61508 compliance
- M7 end-to-end latency < 10ms
- M7 strictly ROS2 native (not through FMI)

**Files:**
- `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp`
- `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/assumption_monitor.cpp`

- [ ] **Step 1: Verify M7 does not include M1-M6 headers**

```bash
grep -r '#include.*m[1-6]_' src/l3_tdl_kernel/m7_safety_supervisor/src/
```
Expected: ZERO matches (arch-independent path requirement).

- [ ] **Step 2: Verify MRM command set is pre-defined (not dynamically computed)**

```bash
grep -n "MRM\|mrm\|generate\|plan\|compute" src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp
```

- [ ] **Step 3: Check SOTIF assumption monitor**

```bash
grep -n "assumption\|triggering_condition\|SOTIF" src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/*.cpp
```

- [ ] **Step 4: Document gaps, fix, commit**

### C.8: Verify M8 (HMI Bridge) Against Architecture §12

**Architecture requirements (§12):**
- SAT-1/2/3 aggregation (current_state, rationale, forecast)
- Adaptive triggering (SAT-3 always, SAT-2 on demand)
- ToR protocol with interactive confirmation
- ASDR logging
- M1-M8 module health monitoring

**Files:**
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/sat_aggregator.cpp`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/tor_protocol.cpp`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/asdr_logger.cpp`

- [ ] **Step 1: Verify SAT-1/2/3 fields in output**

```bash
grep -n "SAT\|sat_1\|sat_2\|sat_3\|current_state\|rationale\|forecast" src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/sat_aggregator.cpp
```

- [ ] **Step 2: Verify ToR protocol**

```bash
grep -n "tor\|TOR\|takeover\|handover" src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/tor_protocol.cpp
```

- [ ] **Step 3: Document gaps, fix, commit**

---

## Track D: Documentation & Compliance Artifacts

**Entry:** None (can start immediately)
**Exit:** All missing D1.x documents created and committed
**Parallel with:** Tracks A, B, C, E, F

### D.1: Verify Missing D1.x Documents

- [ ] **Step 1: Check all 14 document locations**

```bash
for f in \
  "docs/Design/V&V_Plan/00-vv-strategy-v0.1.md" \
  "docs/Design/SIL/v1.0-unified/01-sil-architecture.md" \
  "docs/Design/SIL/v1.0-unified/02-sil-backend-design.md" \
  "docs/Design/SIL/v1.0-unified/03-sil-frontend-design.md" \
  "docs/Design/SIL/v1.0-unified/04-sil-scenario-integration-test.md" \
  "docs/Design/SIL/01-simulator-qualification-report.md" \
  "docs/Design/SIL/02-scenario-schema.md" \
  "docs/Design/SIL/03-coverage-metrics.md" \
  "docs/Design/Cert/cert-evidence-tracking.md" \
  "docs/Design/ConOps/01-conops-v0.1.md" \
  "docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md" \
  "docs/Design/Review/2026-05-07/00-consolidated-findings.md" \
  "docs/Design/HAZID/RUN-001-kickoff.md"; do
  if [ -f "$f" ]; then
    echo "✅ $f ($(wc -l < "$f") lines)"
  else
    echo "❌ $f MISSING"
  fi
done
```

- [ ] **Step 2: Report findings. For each MISSING file, create a stub with header and TODO marker.**

### D.2: Create D1.3.1 Simulator Qualification Report

**File to create:** `docs/Design/SIL/01-simulator-qualification-report.md`

Content requirements (from v3.1 plan D1.3.1):
- 3 analytical reference solutions (straight-line deceleration, standard turning circle, zigzag)
- Error ≤ 5% vs reference
- 100-run determinism (same seed, error < 1e-9)
- MMG parameter ±20% stability check
- ISO 26262 TCL-3 tool confidence argument

- [ ] **Step 1: Write the report skeleton**

```markdown
# Simulator Qualification Report v0.1
...
```

- [ ] **Step 2: Add reference solution data (from existing MMG tests)**
- [ ] **Step 3: Add determinism test results**
- [ ] **Step 4: Add MMG parameter stability analysis**
- [ ] **Step 5: Add tool confidence argument (TCL-3 mapping)**
- [ ] **Step 6: Commit**

### D.3: Create D1.6 Scenario Schema Document

**File to create:** `docs/Design/SIL/02-scenario-schema.md`

- [ ] **Step 1: Document the current internal schema format**

Based on `scenarios/IMAZU标准测试/imazu-01-ho.yaml`, document:
- Required fields: title, description, startTime, ownShip, targetShips, environment, metadata
- ownShip fields: static, initial (position, cog, sog, heading), model, controller
- metadata fields: schema_version, scenario_id, vessel_class, odd_cell, encounter, simulation_settings

- [ ] **Step 2: Add maritime-schema mapping plan**

Document how the internal format maps to `dnv-opensource/maritime-schema` `TrafficSituation`:
- Internal `ownShip.initial` → maritime-schema `ownShip.initialState`
- Internal `targetShips[].initial` → maritime-schema `trafficSituations`
- Internal `metadata.encounter.rule` → maritime-schema `ruleApplicability`
- Internal `metadata.simulation_settings` → maritime-schema `metadata.*`

- [ ] **Step 3: Commit**

### D.4: Create D1.7 Coverage Metrics Document

**File to create:** `docs/Design/SIL/03-coverage-metrics.md`

- [ ] **Step 1: Document the 1100-cell coverage cube**

11 Rule × 4 ODD × 5 disturbance × 5 seed = 1100 concrete cases

- [ ] **Step 2: Document Hagen 6-dimension scoring rubric**

Reference Hagen 2022 / Woerner 2019:
- Safety score (CPA-based continuous)
- Rule compliance score (per-rule {full/partial/violated})
- Delay penalty
- Action magnitude penalty
- Phase score (give-way / stand-on)
- Trajectory implausibility

- [ ] **Step 3: Document Monte Carlo LHS/Sobol 10000 sample methodology**

- [ ] **Step 4: Commit**

### D.5: Create D1.8 Cert Tracking + ConOps

**Files to create:**
- `docs/Design/Cert/cert-evidence-tracking.md`
- `docs/Design/ConOps/01-conops-v0.1.md`

- [ ] **Step 1: Create cert-evidence-tracking.md**

Map CCS 9 evidence types to D-task deliverables:
| CCS Evidence Type | D-Task | Artifact |
|---|---|---|
| Software Requirements Spec | D1.1-D1.8 | Architecture doc + detailed designs |
| Software Architecture | D0-D1 | Architecture v1.1.3-pre-stub |
| Software Design | D2.1-D2.8 | M1-M8 detailed designs |
| Software Implementation | D2.x-D3.x | Source code |
| Software Verification | D1.5-D1.7 | V&V Plan + test reports |
| Software Validation | D3.6 | SIL 1000 scenario results |
| Software Configuration Mgmt | D1.2 | CI/CD pipeline |
| Software Quality Assurance | D1.4 | Coding standards |
| Functional Safety Assessment | D2.7-D3.3 | HARA + FMEDA + SOTIF |

- [ ] **Step 2: Create ConOps v0.1 stub**

Content (5-10 pages):
1. System Overview
2. Operational Scenarios (Transit, Approach, Berthing, Crew Transfer)
3. User Roles (ROC Operator, Captain, Engineer)
4. Operational Boundaries (ODD-A/B/C/D)
5. Autonomy Levels (D2/D3/D4) and Transitions
6. Emergency Procedures (MRC, ToR)
7. External Interfaces (L2 Voyage Planner, L4 Guidance, Shore Link)

- [ ] **Step 3: Commit**

---

## Track E: Web HMI Data Integration

**Entry:** Track A complete, Track B partially complete (ROS2 DDS topics flowing)
**Exit:** Web HMI displays live SIL data with MapLibre ENC, real-time CPA/TCPA, M1-M8 pulse, scoring output
**Parallel with:** Track C

### E.1: Fix foxglove_bridge Topic Whitelist

**File:** `docker-compose.yml:50`

Current whitelist includes `/sim_clock`, `/sil/own_ship_state`, `/sil/target_vessel_state`, `/sil/radar_meas`, `/sil/ais_msg`, `/sil/environment`, `/sil/tracked_targets`, `/sil/lifecycle_status`, `/sil/module_pulse`, `/sil/scoring`, `/sil/asdr_event`.

**Missing:** M1-M8 module topics (`/l3/odd_state`, `/l3/world_state`, `/l3/colregs_constraints`, `/l3/avoidance_plan`, `/l3/safety_alert`).

- [ ] **Step 1: Add M1-M8 topics to whitelist**

```yaml
- p topic_whitelist:="[/sim_clock,/sil/own_ship_state,/sil/target_vessel_state,/sil/radar_meas,/sil/ais_msg,/sil/environment,/sil/tracked_targets,/sil/lifecycle_status,/sil/module_pulse,/sil/scoring,/sil/asdr_event,/l3/odd_state,/l3/world_state,/l3/colregs_constraints,/l3/avoidance_plan,/l3/safety_alert,/l3/checker_veto]"
```

- [ ] **Step 2: Restart foxglove-bridge and verify new topics visible**

```bash
docker compose restart foxglove-bridge
docker compose exec sil-nodes bash -c "source /opt/ros/humble/setup.bash && ros2 topic list | grep l3"
```

- [ ] **Step 3: Verify Web HMI can subscribe**

Open browser DevTools → Network → WS tab. Verify foxglove WebSocket connection and data messages include `/l3/*` topics.

- [ ] **Step 4: Commit**

### E.2: Wire Scoring Output to Web HMI

**Files to check:**
- `web/src/screens/SimulationEvaluator.tsx` — verify it calls scoring API
- `src/sil_orchestrator/scoring_routes.py` — verify scoring endpoint returns Hagen 6-dimension data

- [ ] **Step 1: Read scoring API response format**

```bash
curl http://localhost:8000/api/v1/scoring/imazu-01-ho-v1.0/latest | jq .
```
Expected: JSON with `safety_score`, `rule_compliance_score`, `delay_penalty`, `magnitude_penalty`, `phase_score`, `implausibility`, `pass_fail_verdict`.

- [ ] **Step 2: Verify SimulationEvaluator.tsx renders all 6 dimensions**

Read the component and check for `ScoringRadarChart`, `ScoringGauges` usage.

- [ ] **Step 3: Test with live data**

Run 1 scenario, verify scoring appears in Evaluator screen after completion.

- [ ] **Step 4: Fix any data format mismatches**

- [ ] **Step 5: Commit**

### E.3: Verify Apache Arrow Replay (DEMO-2 feature)

**File:** `web/src/screens/shared/TrajectoryReplay.tsx`

- [ ] **Step 1: Check if Arrow IPC file is generated after scenario run**

```bash
ls runs/*/scoring.arrow 2>/dev/null
```

- [ ] **Step 2: If missing, add Arrow export to scoring node**

Check `src/sim_workbench/sil_nodes/scoring/scoring/arrow_writer.py` — verify it writes Arrow IPC files after each run.

- [ ] **Step 3: Test replay scrubber in Web HMI**

- [ ] **Step 4: Commit**

---

## Track F: CI/CD & Docker Verification

**Entry:** None (can start immediately)
**Exit:** Docker compose end-to-end verified, CI fast gate enabled, colcon test all green
**Parallel with:** Tracks A, D

### F.1: Verify Docker Compose End-to-End

- [ ] **Step 1: Prune old containers and images**

```bash
docker compose down --volumes --remove-orphans
docker system prune -f
```

- [ ] **Step 2: Build all images from scratch**

```bash
docker compose build --no-cache 2>&1 | tee docker-build.log
```

- [ ] **Step 3: Start all services**

```bash
docker compose up -d
```

- [ ] **Step 4: Wait for all healthy (up to 60s)**

```bash
for i in $(seq 1 12); do
  echo "=== Check $i/12 ==="
  docker compose ps
  sleep 5
done
```

- [ ] **Step 5: Verify each service**

```bash
# sil-orchestrator
curl http://localhost:8000/api/v1/health

# foxglove_bridge
curl http://localhost:8765

# martin tile server
curl http://localhost:3000/health

# web (Vite dev server)
curl http://localhost:5173
```

- [ ] **Step 6: Run self-check**

```bash
curl http://localhost:8000/api/v1/selfcheck
```

- [ ] **Step 7: Document any failures, fix, re-verify**

- [ ] **Step 8: Commit**

```bash
git add docker-build.log docker/
git commit -m "docker: verify compose end-to-end healthy"
```

### F.2: Enable CI Fast Gate (Imazu-22 PR Trigger)

**File:** `.gitlab-ci.yml`

- [ ] **Step 1: Verify CI image build**

```bash
docker build -t mass-l3/ci:humble-ubuntu22.04 -f docker/ci.Dockerfile .
```

- [ ] **Step 2: Add sil-smoke stage to CI**

The `.gitlab-ci.yml` already has `sil-smoke` and `sil-baseline` stages defined. Verify jobs exist for these stages.

```bash
grep -A 20 "sil-smoke\|sil-baseline" .gitlab-ci.yml
```

- [ ] **Step 3: Add Imazu-22 fast gate job**

If missing, add:

```yaml
imazu22-fast-gate:
  stage: sil-smoke
  script:
    - docker compose up -d sil-orchestrator sil-nodes
    - sleep 30
    - for scenario in $(ls scenarios/IMAZU标准测试/*.yaml); do
        id=$(basename "$scenario" .yaml);
        curl -X POST "http://localhost:8000/api/v1/gates/run-all?scenario_id=$id";
      done
  rules:
    - if: $CI_PIPELINE_SOURCE == "merge_request_event"
```

- [ ] **Step 4: Commit**

### F.3: Verify Python Test Suite (non-ROS2)

**Note:** ROS2-dependent tests require Docker. This task verifies only pure-Python tests.

- [ ] **Step 1: Run sil_orchestrator tests (mocking rclpy)**

```bash
cd src/sil_orchestrator && python -m pytest tests/ -v --ignore=tests/test_selfcheck_stream.py 2>&1 | tail -30
```

- [ ] **Step 2: Run sim_workbench tests (mocking rclpy)**

```bash
cd src/sim_workbench && python -m pytest --ignore=sil_lifecycle --ignore=sil_nodes -v 2>&1 | tail -30
```

- [ ] **Step 3: Run web tests**

```bash
cd web && npm test 2>&1 | tail -20
```

- [ ] **Step 4: Document failures, fix critical ones**

- [ ] **Step 5: Commit**

### F.4: Clean Working Tree

47 modified files in working tree need to be committed or stashed.

- [ ] **Step 1: Review modified files**

```bash
git diff --stat
```

- [ ] **Step 2: Group by category**

```bash
echo "=== Scenario YAML changes ==="
git diff --name-only | grep "scenarios/"

echo "=== SIL orchestrator changes ==="
git diff --name-only | grep "sil_orchestrator"

echo "=== Web changes ==="
git diff --name-only | grep "web/"
```

- [ ] **Step 3: Commit each group separately with descriptive messages**

```bash
git add scenarios/
git commit -m "chore(scenarios): update Imazu and COLREGs scenario YAMLs"

git add src/sil_orchestrator/
git commit -m "fix(sil): update sil_orchestrator routes and gate runner"

git add web/
git commit -m "fix(web): update HMI components and layout"

git add docker/sil_entrypoint.sh
git commit -m "fix(docker): update sil_entrypoint.sh"
```

---

## Summary: Track Dependency & Parallelism Matrix

```
Week 1 (5/19-5/25):
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track A (Build & Compile M1-M8)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track D (Documentation — independent)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track F (CI/Docker Verification — independent)
                          Track F.4 (Clean working tree — must do first!)

Week 2 (5/26-6/1):
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track A (continued)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track D (continued)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track B.3 (End-to-End — after A complete)

Week 3 (6/2-6/8):
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track B (SIL Integration)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track C (Module Architecture Alignment)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track E (Web HMI Data Integration)

Week 4 (6/9-6/15):
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track B.3/B.4 (Gate Runner + Final Integration)
  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ Track E (Web HMI Polish)
                          ★ DEMO-1 (6/15)
```

---

## Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R1 | M5 CasADi/IPOPT not available in Docker | High | Medium | Use mock solver or analytical MPC for DEMO-1 |
| R2 | C++ nodes cannot be launched from Python entrypoint | High | Medium | Use ROS2 launch file (B.1 step 4) |
| R3 | M4 IvP solver depends on libIvP (GPL license concern) | Medium | High | Verify self-implementation from D0.1 RFC-009 |
| R4 | 47 uncommitted files cause merge conflicts | High | Low | Commit first (F.4), then start tracks |
| R5 | macOS cannot run Docker (ARM vs amd64) | Medium | High | Use Linux CI runner or remote Docker host |
| R6 | M1-M8 snapshot code missing key interfaces | Medium | High | Track C identifies and fixes |

---

## Execution Order Recommendation

**Immediate (this session):**
1. F.4: Commit all 47 modified files (15 min)
2. A.1: Audit M1-M8 build infrastructure (10 min)

**Parallel subagent dispatch (next):**
- Agent 1: Track A — fix M1-M8 builds (A.2 through A.10)
- Agent 2: Track D — create missing documents (D.1 through D.5)
- Agent 3: Track F — Docker verification (F.1 through F.3)

**After Track A complete:**
- Agent 1: Track B — SIL integration (B.1 through B.4)
- Agent 2: Track C — Module alignment (C.1 through C.8)
- Agent 3: Track E — Web HMI data (E.1 through E.3)
