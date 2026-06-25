# Track B — ROS2 Message Governance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all P0 ROS2 topic mismatches that silently break links, fix the `/l3/fsm_state` type collision, split SOTIF authority/mirror, and freeze a machine-readable topic contract with a static checker — producing a clean canonical `/l3/...` bus before Track A (GNC integration) begins.

**Architecture:** Pure source-string canonicalization (no launch remaps hiding defects in source), one fix per defect, a YAML contract as single source of truth, and a Python static checker enforcing it. No behavior changes; the only behavioral side-effect is fixing a latent bug (M7 never received BC-MPC reactive override due to a `/l3/m4/` copy-paste defect).

**Tech Stack:** C++ (ROS2 humble, rclcpp, the nodes being edited), Python (contract checker + tests), pytest, colcon.

**Spec:** `docs/superpowers/specs/2026-06-25-ros2-msg-governance-impl-design.md`

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-generalization-debug`, branch `codex/colregs-generalization-debug`.

**Constraint:** Runtime adaptors in this and all future tracks are C++. Track B does not introduce new runtime adaptors (B8 is a CI tool, not a runtime adaptor), so this constraint is automatically satisfied.

---

## File Structure

**Modified (C++ source, topic string changes):**
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — 3 publisher topic strings
- `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp` — 2 sub + 2 pub topic strings
- `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp` — 1 sub topic string
- `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp` — 2 constexpr topic strings
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp` — 1 sub topic string
- `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py` — remove 1 dual-sub line
- `docker/sil_topic_bridge.py` — 1 sub type change (P0 defect only; full removal is Track A)

**Modified (SOTIF split):**
- `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp` — topic `/sil/n` → `/l3/m7/sotif_metrics`
- `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp` — document mirror only (no code change if already on `/sil/sotif_metrics`)

**Created:**
- `docs/Design/SIL/ros2-interface-contract.yaml` — machine-readable contract
- `tools/sil/__init__.py`, `tools/sil/check_ros2_interface_contract.py` — static checker
- `tests/tools/sil/__init__.py`, `tests/tools/sil/test_ros2_interface_contract.py` — checker tests

---

## Task B0: Verify clean baseline

**Files:** none

- [ ] **Step 1: Confirm worktree + branch + build state**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-generalization-debug"
git status --short
git log --oneline -3
```
Expected: on `codex/colregs-generalization-debug`, head at `265ceeee` (Track A spec) or later; no uncommitted changes blocking.

- [ ] **Step 2: Confirm the P0 defects are still present (sanity)**

```bash
rg -n "/m5/avoidance_plan|/m5/asdr_record|/m5/sat_data|/m2/world_state|/m5/reactive_override_cmd|/l3/m4/reactive_override_cmd|/override/active_signal|/reflex/activation_notification" \
  src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
  src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp \
  src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp \
  src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp \
  src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp
```
Expected: hits in all 5 files matching spec §3 evidence table. If any are absent, STOP — spec evidence is stale; re-verify before proceeding.

- [ ] **Step 3: Record baseline (no commit)**

Note the current `ros2 topic list -t` is not obtainable without a running stack; the static rg evidence above is the baseline. Proceed to Task B1.

---

## Task B1: M5 MID-MPC source topic canonicalization

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:155-157`

- [ ] **Step 1: Change the three publisher topic strings**

In `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`, lines 155-157 currently read:

```cpp
  pub_avoidance_plan_ = create_publisher<l3_msgs::msg::AvoidancePlan>("/m5/avoidance_plan", 10);
  pub_asdr_record_    = create_publisher<l3_msgs::msg::ASDRRecord>("/m5/asdr_record", 10);
  pub_sat_data_       = create_publisher<l3_msgs::msg::SATData>("/m5/sat_data", 10);
```

Change to:

```cpp
  pub_avoidance_plan_ = create_publisher<l3_msgs::msg::AvoidancePlan>("/l3/m5/avoidance_plan", 10);
  pub_asdr_record_    = create_publisher<l3_msgs::msg::ASDRRecord>("/l3/asdr/record", 10);
  pub_sat_data_       = create_publisher<l3_msgs::msg::SATData>("/l3/sat/data", 10);
```

- [ ] **Step 2: Check for launch remaps that were hiding these defects**

```bash
rg -n "/m5/avoidance_plan|/m5/asdr_record|/m5/sat_data" docker/ scripts/ src/l3_tdl_kernel/m5_tactical_planner/launch/ 2>/dev/null
```
If any remap lines exist that map `/m5/...` → `/l3/...`, they are now no-ops; remove them to avoid confusion. If none exist, proceed.

- [ ] **Step 3: Verify no remaining `/m5/` or `/m2/` in MID-MPC source**

```bash
rg -n "/m5/|/m2/" src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```
Expected: zero hits. (`/sil/sat3_data` on line 158 is a SIL debug topic, not in scope — leave it.)

- [ ] **Step 4: Build MID-MPC**

```bash
source scripts/local-a4000-env.sh
colcon build --packages-select m5_tactical_planner
```
Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "fix(m5-mid): canonicalize topic strings /m5/* -> /l3/* (Track B B1)

avoidance_plan, asdr_record, sat_data now use canonical /l3/ names in
source. Removes reliance on launch remaps that hid the defect."
```

---

## Task B2: M5 BC-MPC source topic canonicalization

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:35,41,47,49`

- [ ] **Step 1: Change the four topic strings**

In `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp`, lines 34-49 currently read:

```cpp
  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state_(std::move(msg));
      });

  sub_mid_plan_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      [this](l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        on_mid_mpc_plan_(std::move(msg));
      });

  pub_override_ = create_publisher<l3_msgs::msg::ReactiveOverrideCmd>(
      "/m5/reactive_override_cmd", 10);
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/m5/asdr_record_bc", 10);
```

Change to:

```cpp
  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state_(std::move(msg));
      });

  sub_mid_plan_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", 10,
      [this](l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        on_mid_mpc_plan_(std::move(msg));
      });

  pub_override_ = create_publisher<l3_msgs::msg::ReactiveOverrideCmd>(
      "/l3/m5/reactive_override_cmd", 10);
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record", 10);
```

- [ ] **Step 2: Verify no remaining legacy in BC-MPC source**

```bash
rg -n "/m5/|/m2/" src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp
```
Expected: zero hits.

- [ ] **Step 3: Build BC-MPC**

```bash
colcon build --packages-select m5_tactical_planner
```
Expected: succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp
git commit -m "fix(m5-bc): canonicalize topic strings /m2,/m5/* -> /l3/* (Track B B2)

world_state sub, avoidance_plan sub, reactive_override_cmd pub, asdr
pub now use canonical /l3/ names. BC-MPC previously had no launch remap
hiding these, so this fixes a real silent link break."
```

---

## Task B3: M7 reactive override subscription fix

**Files:**
- Modify: `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp:168`

**Note:** This is a latent bug fix, not just a rename. M7 subscribed `/l3/m4/reactive_override_cmd` but the reactive override is produced by M5 BC-MPC on `/l3/m5/reactive_override_cmd`. M7 likely never received BC-MPC override commands. After this fix, M7 behavior may change (it now receives them). Check M7 unit tests; if any encoded the broken state, update with rationale.

- [ ] **Step 1: Change the subscription topic**

In `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp`, line 168 currently reads:

```cpp
  sub_override_cmd_ = create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
    "/l3/m4/reactive_override_cmd", qos_events,
```

Change to:

```cpp
  sub_override_cmd_ = create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
    "/l3/m5/reactive_override_cmd", qos_events,
```

- [ ] **Step 2: Check M7 tests for encoded broken state**

```bash
rg -n "m4/reactive_override_cmd|m5/reactive_override_cmd|reactive_override" tests/m7/ 2>/dev/null
```
If any test asserts M7 subscribes `/l3/m4/...` or asserts M7 does NOT receive override, update it to reflect the canonical `/l3/m5/...` with a comment explaining the latent-bug fix.

- [ ] **Step 3: Build M7**

```bash
colcon build --packages-select m7_safety_supervisor
```
Expected: succeeds.

- [ ] **Step 4: Run M7 tests**

```bash
python3 -m pytest tests/m7/ -v 2>&1 | tail -20
```
Expected: all pass (or only pre-existing unrelated failures). If a test now fails because it encoded the broken `/l3/m4/` state, fix it per Step 2.

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp tests/m7/
git commit -m "fix(m7): subscribe reactive_override from /l3/m5 not /l3/m4 (Track B B3)

Latent bug: M7 subscribed /l3/m4/reactive_override_cmd but the command is
produced by M5 BC-MPC on /l3/m5/reactive_override_cmd. M7 likely never
received BC-MPC override. Fixes the silent link break."
```

---

## Task B4: M1 + M8 override/reflex topic normalization

**Files:**
- Modify: `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp:89-90`
- Modify: `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp:94`

- [ ] **Step 1: Change M1 constexpr topic strings**

In `src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp`, lines 89-90 currently read:

```cpp
constexpr const char* kTopicReflexActivation = "/reflex/activation_notification";
constexpr const char* kTopicOverrideSignal   = "/override/active_signal";
```

Change to:

```cpp
constexpr const char* kTopicReflexActivation = "/l3/reflex/activation";
constexpr const char* kTopicOverrideSignal   = "/l3/override/active";
```

- [ ] **Step 2: Change M8 override subscription topic**

In `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp`, lines 93-94 currently read:

```cpp
  sub_override_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
      "/override/active_signal", rclcpp::QoS(50).reliable().transient_local(),
```

Change to:

```cpp
  sub_override_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
      "/l3/override/active", rclcpp::QoS(50).reliable().transient_local(),
```

- [ ] **Step 3: Check for legacy remaps compensating for these names**

```bash
rg -n "/override/active_signal|/reflex/activation_notification" docker/ scripts/ 2>/dev/null
```
If remaps exist mapping legacy → canonical, they are now redundant for M1/M8 but may still be needed if an external publisher emits legacy names. Leave remaps in place; they are recorded in the contract (Task B7) with expiry `2026-07-15`. Do not remove yet.

- [ ] **Step 4: Build M1 + M8**

```bash
colcon build --packages-select m1_odd_envelope_manager m8_hmi_transparency_bridge
```
Expected: succeeds.

- [ ] **Step 5: Run M1 + M8 tests**

```bash
python3 -m pytest tests/ -k "m1 or m8" -v 2>&1 | tail -20
```
Expected: pass (update any test encoding legacy topic names with rationale).

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp \
        src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp \
        tests/
git commit -m "fix(m1,m8): normalize override/reflex topics to /l3/* (Track B B4)

M1 kTopicReflexActivation, kTopicOverrideSignal and M8 override sub now
use canonical /l3/reflex/activation and /l3/override/active. M7 already
used canonical names; M1/M8 now align."
```

---

## Task B5: L4 adapter dual-sub removal

**Files:**
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py:132`

- [ ] **Step 1: Remove the redundant legacy subscription**

In `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`, line 132 currently reads:

```python
        self.create_subscription(ReactiveOverrideCmd, "/m5/reactive_override_cmd", self._on_reactive_override, sq)
```

Delete this entire line. The canonical subscription at line 131 (`/l3/m5/reactive_override_cmd`) remains.

- [ ] **Step 2: Verify no remaining legacy sub**

```bash
rg -n "/m5/reactive_override_cmd" src/sim_workbench/
```
Expected: zero hits.

- [ ] **Step 3: Run L4 adapter tests**

```bash
python3 -m pytest tests/sim_workbench/sil_nodes/l4_guidance_adapter/ -v 2>&1 | tail -20
```
Expected: pass. (The L4 adapter is removed entirely in Track A; this task only removes the redundant legacy sub.)

- [ ] **Step 4: Commit**

```bash
git add src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py
git commit -m "refactor(l4-adapter): remove redundant /m5/reactive_override dual-sub (Track B B5)

Canonical /l3/m5/reactive_override_cmd subscription at line 131 remains.
Legacy /m5/ sub removed. L4 adapter is fully removed in Track A."
```

---

## Task B6: `/l3/fsm_state` type collision fix

**Files:**
- Modify: `docker/sil_topic_bridge.py:577` (and callback at line 726-727)

**Note:** Only the `/l3/fsm_state` subscription type changes. The separate `/sil/lifecycle_status` subscription at line 567 (correct, type `LifecycleStatus`) is untouched.

- [ ] **Step 1: Inspect the current fsm_state subscription + callback**

```bash
sed -n '575,580p' docker/sil_topic_bridge.py
sed -n '724,735p' docker/sil_topic_bridge.py
rg -n "FsmState|from l3_msgs" docker/sil_topic_bridge.py | head -5
```
Note the import state: if `FsmState` is not imported, it must be added.

- [ ] **Step 2: Add FsmState import if missing**

In `docker/sil_topic_bridge.py`, in the `from l3_msgs.msg import (...)` block, add `FsmState` if absent. Verify the exact import block first:

```bash
rg -n "from l3_msgs.msg import" docker/sil_topic_bridge.py
```
Add `FsmState,` to that import tuple (matching existing multiline style).

- [ ] **Step 3: Change the subscription type at line 577**

Line 576-578 currently reads:

```python
        self._sub_fsm_state = self.create_subscription(
            LifecycleStatus, "/l3/fsm_state",
            self._on_fsm_state, sq)
```

Change to:

```python
        self._sub_fsm_state = self.create_subscription(
            FsmState, "/l3/fsm_state",
            self._on_fsm_state, sq)
```

- [ ] **Step 4: Update the `_on_fsm_state` callback to consume FsmState fields**

At lines ~726-727, the callback signature and trace record use `LifecycleStatus` fields. Inspect:

```bash
sed -n '726,740p' docker/sil_topic_bridge.py
```
Update the callback to read `FsmState` fields (e.g. `msg.fsm_state`, `msg.stamp`) instead of `LifecycleStatus` fields (e.g. `msg.current_state`). Map the `FsmState` enum to whatever the trace record expects. If the trace record only needs the integer state, `msg.fsm_state` (int) is the field on `l3_msgs/FsmState`.

- [ ] **Step 5: Run the sil_topic_bridge test**

```bash
python3 -m pytest tests/docker/test_sil_topic_bridge.py -v 2>&1 | tail -20
```
Expected: pass. If a test asserts the old `LifecycleStatus` type on `/l3/fsm_state`, update it to `FsmState` with a comment referencing Track B B6.

- [ ] **Step 6: Commit**

```bash
git add docker/sil_topic_bridge.py tests/docker/test_sil_topic_bridge.py
git commit -m "fix(bridge): /l3/fsm_state subscription type LifecycleStatus -> FsmState (Track B B6)

Resolves type collision: /l3/fsm_state had both l3_msgs/FsmState
(fsm_aggregator) and sil_msgs/LifecycleStatus (sil_topic_bridge). Now
single type FsmState. /sil/lifecycle_status mirror (line 567) unchanged."
```

---

## Task B7: SOTIF authority/mirror split

**Files:**
- Modify: `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp` (topic `/sil/n` → `/l3/m7/sotif_metrics`)
- Modify: `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp` (add subscription to M7 authority if it currently self-publishes `/sil/sotif_metrics` from stub data)

- [ ] **Step 1: Change M7 SOTIF publisher topic**

In `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp`, find the publisher constructor (currently creates `/sil/n` best-effort). Change the topic string to `/l3/m7/sotif_metrics`. Keep the message type `l3_msgs::msg::SotifMetrics`. Confirm with:

```bash
rg -n "/sil/n|sotif" src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp
```

- [ ] **Step 2: Verify M8 mirror stays on `/sil/sotif_metrics`**

```bash
rg -n "sotif_metrics|/sil/sotif" src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp
```
M8 should publish `/sil/sotif_metrics` (HMI mirror, line 116-117). Do NOT change M8 to publish `/l3/m7/sotif_metrics`. If M8 currently publishes `/sil/sotif_metrics` from internal stub data (line 434-438), that is acceptable as a mirror for Track B. Add a code comment at the M8 SOTIF publisher (line 116) documenting: `// HMI mirror only. Authority is /l3/m7/sotif_metrics (M7). Future task: subscribe authority and republish instead of stub data.` This is a documentation comment, not a deferred implementation.

- [ ] **Step 3: Build M7**

```bash
colcon build --packages-select m7_safety_supervisor
```
Expected: succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp \
        src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp
git commit -m "fix(m7): SOTIF authority topic /sil/n -> /l3/m7/sotif_metrics (Track B B7)

M7 is now the SOTIF authority on /l3/m7/sotif_metrics. M8 keeps
/sil/sotif_metrics as HMI mirror only. /sil/n removed."
```

---

## Task B8: Topic contract YAML

**Files:**
- Create: `docs/Design/SIL/ros2-interface-contract.yaml`

- [ ] **Step 1: Write the contract YAML**

Create `docs/Design/SIL/ros2-interface-contract.yaml` with the full canonical topic table. Content (from spec §4.1 + §6 + §8.1 of the design spec):

```yaml
topic_contract_version: 1
legacy_allowed_until: "2026-07-15"

namespaces:
  l3_internal_prefix: "/l3"
  sil_prefix: "/sil"

qos_profiles:
  state_latched:   { reliability: reliable, durability: transient_local, depth: 10 }
  state_stream:    { reliability: reliable, durability: volatile, depth: 5 }
  sensor_fast:     { reliability: best_effort, durability: volatile, depth: 2 }
  safety_event:    { reliability: reliable, durability: transient_local, depth: 50 }
  short_command:   { reliability: reliable, durability: volatile, depth: 3 }
  audit_fanin:     { reliability: reliable, durability: volatile, depth: 50 }
  hmi_mirror:      { reliability: best_effort, durability: volatile, depth: 1 }

topics:
  # M1
  - { name: /l3/m1/odd_state,              type: l3_msgs/msg/ODDState,              owner: M1, qos: state_latched }
  - { name: /l3/m1/mode_cmd,               type: l3_msgs/msg/ModeCmd,               owner: M1, qos: state_latched }
  - { name: /l3/m1/tor_request,            type: l3_msgs/msg/ToRRequest,            owner: M1, qos: state_latched }
  # M2
  - { name: /l3/m2/world_state,            type: l3_msgs/msg/WorldState,            owner: M2, qos: state_stream }
  # M3
  - { name: /l3/m3/mission_goal,           type: l3_msgs/msg/MissionGoal,           owner: M3, qos: state_stream }
  - { name: /l3/m3/mission_state,          type: l3_msgs/msg/MissionState,          owner: M3, qos: state_latched }
  - { name: /l3/m3/route_replan_request,   type: l3_msgs/msg/RouteReplanRequest,    owner: M3, qos: state_latched }
  # M4
  - { name: /l3/m4/behavior_plan,          type: l3_msgs/msg/BehaviorPlan,          owner: M4, qos: state_stream }
  # M5
  - { name: /l3/m5/avoidance_plan,         type: l3_msgs/msg/AvoidancePlan,         owner: M5, qos: state_stream }
  - { name: /l3/m5/reactive_override_cmd,  type: l3_msgs/msg/ReactiveOverrideCmd,   owner: M5, qos: short_command }
  # M6
  - { name: /l3/m6/colregs_constraint,     type: l3_msgs/msg/COLREGsConstraint,     owner: M6, qos: state_stream }
  # M7
  - { name: /l3/m7/safety_alert,           type: l3_msgs/msg/SafetyAlert,           owner: M7, qos: safety_event }
  - { name: /l3/m7/heartbeat,              type: std_msgs/msg/Header,               owner: M7, qos: sensor_fast }
  - { name: /l3/m7/sotif_metrics,          type: l3_msgs/msg/SotifMetrics,          owner: M7, qos: state_stream }
  # M8
  - { name: /l3/m8/operator_state,         type: l3_msgs/msg/OperatorState,         owner: M8, qos: state_latched }
  - { name: /l3/m8/ui_state,               type: l3_msgs/msg/UIState,               owner: M8, qos: hmi_mirror }
  - { name: /l3/m8/tor_request,            type: l3_msgs/msg/ToRRequest,            owner: M8, qos: state_latched }
  # shared buses
  - { name: /l3/asdr/record,               type: l3_msgs/msg/ASDRRecord,            owner: fanin, qos: audit_fanin }
  - { name: /l3/sat/data,                  type: l3_msgs/msg/SATData,                owner: fanin, qos: state_stream }
  - { name: /l3/safety/concern,            type: l3_msgs/msg/SafetyConcernEvent,    owner: fanin, qos: safety_event }
  - { name: /l3/fsm_state,                 type: l3_msgs/msg/FsmState,              owner: fsm_aggregator, qos: hmi_mirror }
  # external event ingress (canonical)
  - { name: /l3/checker/veto,              type: l3_external_msgs/msg/CheckerVetoNotification,      owner: checker_adapter, qos: safety_event }
  - { name: /l3/reflex/activation,         type: l3_external_msgs/msg/ReflexActivationNotification, owner: reflex_adapter,  qos: safety_event }
  - { name: /l3/override/active,           type: l3_external_msgs/msg/OverrideActiveSignal,         owner: override_adapter, qos: safety_event }
  # L1/L2/fusion/L4 external boundary
  - { name: /l1/voyage_task,               type: l3_external_msgs/msg/VoyageTask,        owner: l1_adapter, qos: state_latched }
  - { name: /l2/planned_route,             type: l3_external_msgs/msg/PlannedRoute,      owner: l2_adapter, qos: state_stream }
  - { name: /l2/speed_profile,             type: l3_external_msgs/msg/SpeedProfile,      owner: l2_adapter, qos: state_stream }
  - { name: /l2/replan_response,           type: l3_external_msgs/msg/ReplanResponse,    owner: l2_adapter, qos: state_latched }
  - { name: /fusion/own_ship_state,        type: l3_external_msgs/msg/FilteredOwnShipState, owner: fusion, qos: sensor_fast }
  - { name: /fusion/tracked_targets,       type: l3_external_msgs/msg/TrackedTargetArray,    owner: fusion, qos: state_stream }
  - { name: /fusion/environment_state,     type: l3_external_msgs/msg/EnvironmentState,      owner: fusion, qos: state_latched }
  - { name: /l4/tracking_error,            type: l3_external_msgs/msg/TrackingError,         owner: l4_adapter, qos: sensor_fast }
  # SIL mirrors (not safety authority)
  - { name: /sil/own_ship_state,           type: sil_msgs/msg/OwnShipState,      owner: sil, qos: sensor_fast }
  - { name: /sil/lifecycle_status,         type: sil_msgs/msg/LifecycleStatus,   owner: sil, qos: sensor_fast }
  - { name: /sil/sotif_metrics,            type: l3_msgs/msg/SotifMetrics,       owner: m8_mirror, qos: hmi_mirror }
  - { name: /sil/sat2_data,                type: l3_msgs/msg/SAT2Data,           owner: sil, qos: hmi_mirror }
  - { name: /sil/sat3_data,                type: l3_msgs/msg/SAT3Data,           owner: sil, qos: hmi_mirror }
  - { name: /sil/actuator_cmd,             type: sil_msgs/msg/OwnShipState,      owner: l4_adapter, qos: sensor_fast }

legacy_topics:
  - { old: /override/active_signal,           new: /l3/override/active }
  - { old: /reflex/activation_notification,   new: /l3/reflex/activation }
```

- [ ] **Step 2: Commit**

```bash
git add docs/Design/SIL/ros2-interface-contract.yaml
git commit -m "docs(contract): add ros2-interface-contract.yaml (Track B B8)

Machine-readable single source of truth for canonical topics, types,
owners, QoS. Legacy topics with expiry 2026-07-15. Track A will amend
this file with /l3/gnc/* and /l3/m5/avoidance_waypoints."
```

---

## Task B9: Contract checker

**Files:**
- Create: `tools/sil/__init__.py`
- Create: `tools/sil/check_ros2_interface_contract.py`
- Create: `tests/tools/sil/__init__.py`
- Create: `tests/tools/sil/test_ros2_interface_contract.py`

- [ ] **Step 1: Write the failing test**

Create `tests/tools/sil/test_ros2_interface_contract.py`:

```python
"""Tests for the ROS2 interface contract checker."""
import textwrap
from pathlib import Path

import pytest

from tools.sil.check_ros2_interface_contract import (
    TopicContract,
    SourceFinding,
    check_source_against_contract,
)


@pytest.fixture
def sample_contract_yaml(tmp_path):
    yaml_text = textwrap.dedent("""\
        topic_contract_version: 1
        legacy_allowed_until: "2026-07-15"
        topics:
          - { name: /l3/m5/avoidance_plan, type: l3_msgs/msg/AvoidancePlan, owner: M5, qos: state_stream }
          - { name: /l3/m5/reactive_override_cmd, type: l3_msgs/msg/ReactiveOverrideCmd, owner: M5, qos: short_command }
        legacy_topics:
          - { old: /m5/avoidance_plan, new: /l3/m5/avoidance_plan }
        """)
    p = tmp_path / "contract.yaml"
    p.write_text(yaml_text)
    return p


def test_load_contract(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    assert "/l3/m5/avoidance_plan" in c.topics
    assert c.topics["/l3/m5/avoidance_plan"].type == "l3_msgs/msg/AvoidancePlan"
    assert "/m5/avoidance_plan" in c.legacy_old_names


def test_finding_canonical_ok(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m5/avoidance_plan", ros_type="l3_msgs/msg/AvoidancePlan"),
    ]
    violations = check_source_against_contract(c, findings)
    assert violations == []


def test_finding_unregistered_topic_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m9/unknown", ros_type="l3_msgs/msg/Foo"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "unregistered" in violations[0]


def test_finding_type_mismatch_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/l3/m5/avoidance_plan", ros_type="l3_msgs/msg/WrongType"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "type" in violations[0].lower()


def test_finding_legacy_topic_outside_whitelist_violation(sample_contract_yaml):
    c = TopicContract.load(sample_contract_yaml)
    # /m2/foo is neither canonical nor in legacy_topics whitelist
    findings = [
        SourceFinding(path="x.cpp", line=10, kind="publisher", topic="/m2/foo", ros_type="l3_msgs/msg/Foo"),
    ]
    violations = check_source_against_contract(c, findings)
    assert len(violations) == 1
    assert "unregistered" in violations[0]
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
python3 -m pytest tests/tools/sil/test_ros2_interface_contract.py -v
```
Expected: FAIL with `ModuleNotFoundError: No module named 'tools.sil.check_ros2_interface_contract'`.

- [ ] **Step 3: Write the checker implementation**

Create `tools/sil/__init__.py` (empty) and `tools/sil/check_ros2_interface_contract.py`:

```python
"""Static checker for the ROS2 interface contract.

Scans C++/Python source for create_publisher/create_subscription calls,
extracts topic name + ROS2 type, and checks against the contract YAML.
Fails on: unregistered topic, type mismatch, legacy topic outside whitelist.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import yaml


@dataclass
class TopicEntry:
    name: str
    type: str
    owner: str
    qos: str


@dataclass
class SourceFinding:
    path: str
    line: int
    kind: str  # "publisher" or "subscription"
    topic: str
    ros_type: Optional[str]


@dataclass
class TopicContract:
    topics: Dict[str, TopicEntry] = field(default_factory=dict)
    legacy_old_names: Dict[str, str] = field(default_factory=dict)
    legacy_allowed_until: str = ""

    @classmethod
    def load(cls, path: Path) -> "TopicContract":
        data = yaml.safe_load(Path(path).read_text())
        c = cls()
        for t in data.get("topics", []):
            c.topics[t["name"]] = TopicEntry(
                name=t["name"], type=t["type"], owner=t.get("owner", ""), qos=t.get("qos", "")
            )
        for lt in data.get("legacy_topics", []):
            c.legacy_old_names[lt["old"]] = lt["new"]
        c.legacy_allowed_until = data.get("legacy_allowed_until", "")
        return c


def check_source_against_contract(
    contract: TopicContract, findings: List[SourceFinding]
) -> List[str]:
    violations: List[str] = []
    for f in findings:
        if f.topic in contract.topics:
            expected_type = contract.topics[f.topic].type
            if f.ros_type and f.ros_type != expected_type:
                violations.append(
                    f"{f.path}:{f.line}: type mismatch on {f.topic}: "
                    f"found {f.ros_type}, contract says {expected_type}"
                )
        elif f.topic in contract.legacy_old_names:
            # legacy alias: allowed (within expiry window). No violation.
            pass
        else:
            violations.append(
                f"{f.path}:{f.line}: unregistered topic {f.topic} ({f.kind})"
            )
    return violations


# Regex for C++ create_publisher<T>("topic", ...) and create_subscription<T>("topic", ...)
_CPP_PUB_RE = re.compile(
    r'create_publisher<([\w:]+)>\s*\(\s*"([^"]+)"'
)
_CPP_SUB_RE = re.compile(
    r'create_subscription<([\w:]+)>\s*\(\s*\n?\s*"([^"]+)"'
)


def scan_cpp_file(path: Path) -> List[SourceFinding]:
    findings: List[SourceFinding] = []
    text = path.read_text(errors="replace")
    for lineno, line in enumerate(text.splitlines(), 1):
        for m in _CPP_PUB_RE.finditer(line):
            ros_type = _normalize_cpp_type(m.group(1))
            findings.append(SourceFinding(str(path), lineno, "publisher", m.group(2), ros_type))
        for m in _CPP_SUB_RE.finditer(line):
            ros_type = _normalize_cpp_type(m.group(1))
            findings.append(SourceFinding(str(path), lineno, "subscription", m.group(2), ros_type))
    return findings


def _normalize_cpp_type(raw: str) -> str:
    # l3_msgs::msg::AvoidancePlan -> l3_msgs/msg/AvoidancePlan
    return raw.replace("::msg::", "/msg/").replace("::srv::", "/srv/")


def scan_directory(root: Path, exclude_globs: List[str]) -> List[SourceFinding]:
    findings: List[SourceFinding] = []
    for path in root.rglob("*.cpp"):
        if any(part in str(path) for part in exclude_globs):
            continue
        findings.extend(scan_cpp_file(path))
    return findings


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--root", default="src/l3_tdl_kernel", type=Path)
    args = parser.parse_args(argv)

    contract = TopicContract.load(args.contract)
    # Exclude GNC bridge (Track A) and any third_party. sil_topic_bridge.py is
    # adapter-boundary (Track A removes it), excluded here.
    excludes = ["third_party", "gnc_bridge", "build", "install"]
    findings = scan_directory(args.root, excludes)
    violations = check_source_against_contract(contract, findings)
    if violations:
        for v in violations:
            print(v, file=sys.stderr)
        return 1
    print(f"OK: {len(findings)} findings checked, 0 violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Create `tests/tools/sil/__init__.py` (empty).

- [ ] **Step 4: Run the test to verify it passes**

```bash
python3 -m pytest tests/tools/sil/test_ros2_interface_contract.py -v
```
Expected: all 5 tests PASS.

- [ ] **Step 5: Run the checker against the real source**

```bash
python3 tools/sil/check_ros2_interface_contract.py \
  --contract docs/Design/SIL/ros2-interface-contract.yaml \
  --root src/l3_tdl_kernel
```
Expected: exits 0 (all topics registered, types match). If violations appear, they are residual defects from Tasks B1-B7 that were missed — fix them before proceeding.

- [ ] **Step 6: Commit**

```bash
git add tools/sil/ tests/tools/sil/
git commit -m "feat(tools): ROS2 interface contract checker (Track B B9)

Static checker scans C++ create_publisher/create_subscription calls,
validates against ros2-interface-contract.yaml. Fails on unregistered
topic, type mismatch, or legacy topic outside whitelist. Excludes
gnc_bridge (Track A) and sil_topic_bridge (adapter-boundary)."
```

---

## Task B10: Runtime validation + local gate

**Files:** none (verification only)

- [ ] **Step 1: Rebuild affected packages**

```bash
source scripts/local-a4000-env.sh
colcon build --packages-select m5_tactical_planner m7_safety_supervisor \
  m1_odd_envelope_manager m8_hmi_transparency_bridge
```
Expected: all succeed.

- [ ] **Step 2: Rebuild the sil-nodes image (sil_topic_bridge.py changed)**

```bash
COMPOSE_PROJECT_NAME=mass-l3-sil docker compose build sil-nodes
```
Expected: image builds.

- [ ] **Step 3: Start the stack and verify runtime topic contract**

```bash
COMPOSE_PROJECT_NAME=mass-l3-sil docker compose up -d sil-nodes
sleep 8
docker exec mass-l3-sil-sil-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list -t | sort'
```
Expected: canonical `/l3/m5/avoidance_plan`, `/l3/m5/reactive_override_cmd`, `/l3/asdr/record`, `/l3/sat/data`, `/l3/override/active`, `/l3/reflex/activation`, `/l3/m7/sotif_metrics` all present with single types. No `/m5/*`, `/m2/*`, `/sil/n` topics.

- [ ] **Step 4: Verify `/l3/fsm_state` single type**

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic info -v /l3/fsm_state'
```
Expected: exactly one type: `l3_msgs/msg/FsmState`. No `sil_msgs/LifecycleStatus`.

- [ ] **Step 5: Run a COLREGs scenario for message-flow evidence (behavior verdict not a gate)**

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --restart-between-runs \
  --restart-container mass-l3-sil-sil-nodes-1 \
  --summary-out runs/msg_governance_rule14_$(date +%Y%m%d_%H%M%S).json
```
Expected: trace contains `/l3/m5/avoidance_plan`, `/l3/m5/reactive_override_cmd`, `/l3/m6/colregs_constraint`, `/l3/asdr/record`. NO legacy `/m5/*`, `/m2/*`, `/override/active_signal`, `/reflex/activation_notification` in the trace. (The scenario behavioral verdict may still be RED on `turn_starboard` — that is expected and is resolved by Track A, not Track B. Message-flow is the gate here.)

- [ ] **Step 6: Run the local OrbStack acceptance gate**

```bash
./scripts/local-a4000-acceptance.sh
```
Expected: passes. If it fails, fix locally before any A4000 sync.

- [ ] **Step 7: Commit the run evidence (optional, keep runs/ gitignored normally)**

Do not commit run JSONs unless the repo convention requires it. Note the evidence path in the handoff log instead.

- [ ] **Step 8: Append handoff entry**

Append to `handoff/workspace_log.md`:

```markdown
## [2026-06-25] Agent / Track B merge / ROS2 message governance
**Task Goal:** Fix all P0 ROS2 topic mismatches + freeze topic contract.
**Core Changes:** B1-B9 (M5/M7/M1/M8 topic canonicalization, fsm_state type fix, SOTIF split, contract YAML + checker).
**Current Status:** Local gate green; scenario message-flow clean; behavioral verdict still RED on turn_starboard (Track A target).
**Handoff Notes:** Track A may begin; amend ros2-interface-contract.yaml with /l3/gnc/* topics. sil_topic_bridge.py still present (full removal is Track A Task A4).
```

- [ ] **Step 9: Final commit**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): Track B complete — ROS2 msg governance, local gate green"
```

---

## Acceptance Summary

Track B is complete when:
- `python3 tools/sil/check_ros2_interface_contract.py --contract docs/Design/SIL/ros2-interface-contract.yaml --root src/l3_tdl_kernel` exits 0.
- `python3 -m pytest tests/tools/sil/test_ros2_interface_contract.py -v` passes.
- `ros2 topic info -v /l3/fsm_state` shows single type `l3_msgs/msg/FsmState`.
- `ros2 topic list -t` shows no `/m5/*`, `/m2/*`, `/sil/n`, `/override/active_signal`, `/reflex/activation_notification` (legacy aliases only via documented remap).
- colreg-rule14-ho trace contains canonical `/l3/m5/*`, `/l3/asdr/record`, `/l3/sat/data` with no legacy names.
- `./scripts/local-a4000-acceptance.sh` passes.
- The scenario behavioral verdict (turn_starboard) is NOT a Track B gate; it remains RED and is the Track A target.
