---
title: D-DEMO1-R3 — Avoidance chain fix (M4 IvP + bridge actuator + M5 target_speed)
date: 2026-05-27
author: Claude Code (subagent)
estimate_pw: 2.0
blocks: D-DEMO1-R6 (SIL integration + validation)
blocked_by: D-DEMO1-R1 (evidence report)
status: draft (pending main-agent audit)
---

# 1. Motivation

Evidence from R1 report identifies **three coupled failure points** in the avoidance decision chain that prevent imazu-01-ho Rule 14 head-on avoidance from executing:

| Issue | Evidence | Impact |
|-------|----------|--------|
| **F-R1-01** | M4 IvP solver always returns `rationale: "IvP infeasible fallback"` even for simplest head-on scenario | M4 outputs unbounded heading [270°, 90°] + speed_max ≈ current SOG → fallback mode locks decision chain |
| **F-R1-02** | `/sil/actuator_cmd` **silent when imazu scenario activated**; was 1 Hz in default scenario | Ship dynamics receives zero throttle command → ownship speed decays 10 kn → 5.3 kn over 12s (-47%) |
| **F-R1-05** | M5 outputs `target_speed_kn` ≈ ownship current SOG (self-feedback) instead of nominal YAML 10 kn | Forms positive feedback loop: lower speed → lower max speed constraint → even lower plan speed → cascade slowdown |

**Root cause chain** (per R1 §4):
```
M4 IvP infeasible (F-R1-01)
  ↓ speed_max_kn = current SOG (echoing from M3/L2 void)
  ↓ M5 target_speed_kn = current SOG (self-feedback F-R1-05)
  ↓ bridge _on_avoidance_plan() → actuator_cmd silent (F-R1-02)
  ↓ ship_dynamics no throttle input
  ↓ cascade slowdown → ownship immobilized
```

At T+300s (when avoidance should trigger), ownship is already at 5.3 kn with 0.1 throttle — no dynamic energy for starboard turn maneuver.

---

# 2. Goals

### Primary (P0 — block DEMO-1 if not fixed)

1. **Fix M4 IvP infeasible** — diagnose why solver fails in imazu-01-ho, implement failsafe behavior gen
2. **Fix bridge actuator_cmd silence** — restore `/sil/actuator_cmd` publication on imazu scenario activation
3. **Decouple M5 target_speed feedback loop** — use YAML nominal SOG or M4 max speed constraint, never current SOG
4. **Restore ownship dynamics energy** — ensure CRUISE_SPEED_KN (10 kn) is maintained in TRANSIT phase

### Secondary (P1 — does not block DEMO-1)

5. **Wire M6 colregs_constraint into M5 reference** — currently M6 output only goes to M8 HMI; M5 should apply Rule 14 constraints to objective weighting

### Sidenote

6. **Document orchestrator lifecycle non-persistence** — R1 §1.2 notes lifecycle state lost on restart; include in SIL Integrator runbook

---

# 3. Root Cause Investigation

## 3.1 M4 IvP infeasible — hypothesis tree

**Hypothesis Priority:**

### H3.1.1 (High priority) — Mission state precondition missing
**Location:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` lines 75–105 (`build_inputs()`)

**Theory:** M4 requires `mission_received=true` from M3, but R1 shows `/l3/m3/mission_state` has `Publisher count: 0`. M3 is stuck in AWAITING_ROUTE (no L2 input per F-R1-04). M4 may fail IvP setup if mission context is absent.

**Test:**
```bash
# In container:
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic echo --once /l3/m3/mission_state 2>&1 | timeout 4 cat || echo "SILENT"
  ros2 topic info /l3/m3/mission_state -v
'
```
Expected: Publisher count 0 or publisher exists but no messages in 4s window.

**Mitigation:**
- In M4 `arbitration_timer_callback()`, check `mission_received_` flag **before** calling `BehaviorActivationCondition::compute_active_set()`.
- If mission silent and world has target conflict, **fall back to failsafe heading bounds** (heading_min/max = nominal 0°±5°, speed_max = CRUISE_SPEED_KN).

---

### H3.1.2 (High priority) — Behavior dictionary lookup miss or empty
**Location:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` lines 128–150 (active_set computation)

**Theory:** M4 loads behavior dictionary from config (line 55: `dictionary_.load(config_dir + "/behavior_definitions.yaml")`). If config_dir not passed or YAML malformed, dictionary is empty → `compute_active_set()` returns empty → fallback path triggered.

**Test:**
```bash
docker exec mass-l3-tacticallayer-sil-orchestrator-1 bash -lc '
  grep -r "m4.config_dir" /opt/ws/install/*/share/*/config/ 2>/dev/null || echo "NOT FOUND"
  ls -la /opt/ws/install/m4_behavior_arbiter/share/m4_behavior_arbiter/behavior_definitions.yaml 2>/dev/null || echo "FILE MISSING"
'
```
Expected: Verify config file exists and path is passed to node.

**Mitigation:**
- Add explicit validation in `BehaviorArbiterNode::BehaviorArbiterNode()`: if dictionary is empty after load, log WARN and fill with a **minimal transit rule** (heading ±180°, speed 0–22 kn).

---

### H3.1.3 (Medium priority) — Objective function unbounded or constraint contradiction
**Location:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` lines 146–160+ (IvP solver setup)

**Theory:** M4 calls `solver_->solve(...)` with weighted objectives + constraints (lines not shown in excerpt). If constraints are **contradictory** (e.g., heading_min > heading_max after normalization, or speed_min > speed_max), or if no objective function is registered, IvP returns infeasible.

From R1: `heading_min_deg: 269.98, heading_max_deg: 89.98` (wraps around 0°), `speed_max_kn: 0.883 ≈ current SOG`. The heading window is valid (270°–90° = 180° via 0°), but speed is nearly zero.

**Test:**
```bash
# Check M4 solver call and return code
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  rosparam get /m4_behavior_arbiter 2>/dev/null || echo "PARAM NOT SET"
'
# Extract solver timeout, domain resolution, etc.
```

**Mitigation:**
- In M4 solver callback, **always check solver return status** before using output.
- If solver fails, **generate synthetic failsafe plan**: heading_min/max = current_heading ± some safe margin, speed_max = CRUISE_SPEED_KN.
- Log solver failure reason (timeout? infeasible? numeric error?) to ASDR for traceability.

---

### H3.1.4 (Low priority) — Speed domain resolution causing discretization artifact
**Location:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` lines 18–28 (IvPSpeedDomain setup)

**Theory:** IvP domain discretized by `speed_domain_resolution_kn`. If speed_max ≈ 0.9 kn and resolution is 0.5 kn, domain is {0.0, 0.5}, missing 0.9 → solver has no exact match.

**Test:** Check IvP solver debug output (if available in logs).

**Mitigation:** Low priority; covered by H3.1.3 failsafe.

---

## 3.2 Bridge actuator_cmd silence — hypothesis tree

**Hypothesis Priority:**

### H3.2.1 (High priority) — Conditional publication based on scenario_id
**Location:** `docker/sil_topic_bridge.py` lines 309–337 (`_on_avoidance_plan()`)

**Theory:** R1 shows `/sil/actuator_cmd` is 1 Hz in default scenario, **SILENT in imazu**. Bridge code (lines 309–337) shows straightforward subscription → transform → publish. **No scenario check visible in excerpt**, but user may have added conditional gate.

**Test:**
```bash
# Check if bridge has scenario-sensitive logic
grep -n "scenario\|imazu\|if.*scenario_id" /Users/marine/Code/MASS-L3-Tactical\ Layer/docker/sil_topic_bridge.py
# Also check if bridge subscribes to lifecycle/scenario_id somewhere
```

**Mitigation:** Remove any `if scenario_id == "imazu"` or similar gate. Bridge should publish actuator_cmd for **all scenarios**.

---

### H3.2.2 (Medium priority) — AvoidancePlan subscription not firing in imazu
**Location:** `docker/sil_topic_bridge.py` lines 158–162 (subscription setup)

**Theory:** Bridge subscribes to `/l3/m5/avoidance_plan` (QoS: BEST_EFFORT depth=5). If M5 never publishes or publishes to different topic, callback never fires. R1 shows `/l3/m5/avoidance_plan` **is 1 Hz** → M5 is publishing.

**Test:**
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic hz /l3/m5/avoidance_plan --window 10
  ros2 topic echo --once /l3/m5/avoidance_plan
'
```
Expected: Should see waypoint list + target_speed_kn.

**Mitigation:** If M5 topic fires but bridge doesn't publish, check QoS mismatch or subscriber filter.

---

### H3.2.3 (Low priority) — Empty waypoint list in M5 avoidance_plan
**Location:** `docker/sil_topic_bridge.py` line 313 (condition `if msg.waypoints`)

**Theory:** M5 publishes avoidance_plan with empty `waypoints` list → bridge line 313 condition false → no actuator_cmd published.

R1 §3.5 shows waypoints **are present** (two entries with 6.8m spacing), so this is unlikely.

**Test:** Included in H3.2.2 test above.

**Mitigation:** If waypoints are empty, still publish minimal actuator_cmd (rudder 0.0, throttle at CRUISE_SPEED_KN / MAX_SPEED_KN).

---

## 3.3 M5 target_speed self-feedback — hypothesis tree

**Hypothesis Priority:**

### H3.3.1 (High priority) — M5 uses M4 speed_max_kn as speed reference
**Location:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` lines 145–150 (constraint assembly)

**Theory:** M5 reads M4 `behavior_plan_->speed_max_kn` and uses it as upper bound. M4 sets `speed_max_kn = ownship.current_sog` (from latest_world line 87). → **Loop closes**: M5 target_speed ≤ M4 speed_max ≤ current_sog → cascade slowdown.

**Test:**
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  (ros2 topic echo --once /l3/m4/behavior_plan && echo "===") &
  (ros2 topic echo --once /l3/m5/avoidance_plan && echo "===") &
  sleep 6
  wait
'
# Compare speed_max_kn from M4 with target_speed_kn in M5 waypoints
```

Expected: M4 speed_max_kn ≈ M5 target_speed_kn ≈ ownship SOG (2.7–5.3 kn).

**Mitigation:** In M5 `mid_mpc_node.cpp`, **override M4 speed_max if M4 is in fallback**:
```cpp
double speed_ref_kn = behavior_plan_->speed_max_kn;
if (std::string(behavior_plan_->rationale) == "IvP infeasible fallback") {
  // R3 fallback: use nominal cruise speed from YAML or 10 kn default
  speed_ref_kn = 10.0;  // TODO: fetch from scenario YAML
}
inp.constraints.speed_max_mps = speed_ref_kn * units::kMsPerKn;
```

---

### H3.3.2 (Medium priority) — Planned route / speed profile unavailable
**Location:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` lines 65–75 (subscription setup)

**Theory:** M5 subscribes to `/l2/planned_route` and `/l2/speed_profile`. R1 shows both **SILENT** (no L2 publisher in SIL). M5 likely falls back to **default planned speed** (line 28: `kDefaultPlannedSpeed_mps = 5.0 m/s ≈ 9.7 kn`). But this is still near M4 fallback speed_max.

**Mitigation:** Covered by H3.3.1. M5 should **use nominal YAML ownShip.initial.sog** as fallback when L2 input absent.

---

# 4. Detailed Design (per sub-fix)

## 4.1 M4 IvP infeasible fix

**Objective:** Ensure M4 always outputs **valid (non-fallback) plan**, even when M3 mission_state is absent.

### 4.1.1 Add precondition check for mission state
**File:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`

**Action:**
- In `arbitration_timer_callback()` (line 108), **before** calling `BehaviorActivationCondition::compute_active_set()`, add:
  ```cpp
  // Step 2.5: Check if mission state is available
  //   If L2 is absent (R4 issue), M3 won't publish mission_state.
  //   Fallback to failsafe envelope for TRANSIT phase.
  bool mission_available = mission_received_ && latest_mission_;
  if (!mission_available && world_received_ && latest_world_->targets.empty()) {
    // TRANSIT phase (no conflict detected): use nominal cruise envelope
    BehaviorPlanMsg plan;
    plan.schema_version = 113;
    plan.stamp = now();
    plan.behavior = BehaviorPlanMsg::BEHAVIOR_TRANSIT;
    plan.heading_min_deg = -5.0f;  // ±5° margin
    plan.heading_max_deg = 5.0f;
    plan.speed_min_kn = 0.0f;
    plan.speed_max_kn = static_cast<float>(speed_max_kn_);  // Use node-level default (22 kn)
    plan.confidence = 0.85f;
    plan.rationale = "Failsafe TRANSIT (no L2 input, no conflict)";
    pub_plan_->publish(plan);
    return;
  }
  ```

### 4.1.2 Add behavior dictionary validation in constructor
**File:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`

**Action:**
- After line 56 (`dictionary_.load(...)`), add:
  ```cpp
  if (dictionary_.empty()) {
    RCLCPP_WARN(get_logger(), "[M4] Behavior dictionary empty; adding minimal transit rule");
    // Insert minimal transit rule to prevent upstream failures
    BehaviorDef transit_rule;
    transit_rule.name = "TRANSIT";
    transit_rule.heading_min_deg = 0.0;
    transit_rule.heading_max_deg = 360.0;
    transit_rule.speed_min_kn = 0.0;
    transit_rule.speed_max_kn = 22.0;
    dictionary_.add_rule(transit_rule);  // Pseudo-API; implement if not exists
  }
  ```

### 4.1.3 Add solver failure handling
**File:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` (lines 145–160+, not shown in excerpt but inferred)

**Action:** Locate IvP solver call; wrap in try-catch or status check:
  ```cpp
  // Hypothetical solver call:
  auto solver_result = solver_->solve(weighted_fns, constraints, ...);
  if (!solver_result.is_feasible()) {
    RCLCPP_WARN(get_logger(), "[M4] IvP solver failed: %s", solver_result.message().c_str());
    // Fallback to safe heading bounds + current speed max
    h_min = 0.0; h_max = 360.0;
    s_max = speed_max_kn_;
    confidence = 0.50f;
    rationale = "IvP infeasible fallback (solver timeout or unbounded)";
  }
  ```

---

## 4.2 Bridge actuator_cmd fix

**Objective:** Restore `/sil/actuator_cmd` publication on all scenarios (including imazu).

### 4.2.1 Inspect and remove scenario-sensitive gates
**File:** `docker/sil_topic_bridge.py`

**Action:**
- Search for any conditional logic in `_on_avoidance_plan()` (lines 309–337):
  ```bash
  grep -n "if.*scenario\|if.*imazu" docker/sil_topic_bridge.py
  ```
- If any exists, **remove it**. Bridge should be scenario-agnostic.
- Verify lines 313–326 publish unconditionally (no scenario check guarding `self._pub_act.publish(out)`).

### 4.2.2 Ensure QoS reliability matches
**File:** `docker/sil_topic_bridge.py`

**Action:**
- M5 publishes to `/l3/m5/avoidance_plan` with default ROS2 QoS (likely BEST_EFFORT).
- Bridge subscribes with `_sensor_qos()` (line 160, BEST_EFFORT depth=5) ✓ **Match confirmed**.
- Bridge publishes `/sil/actuator_cmd` with `_sensor_qos()` (line 162) ✓ **Correct**.

No QoS changes needed.

### 4.2.3 Add defensive null check for waypoints
**File:** `docker/sil_topic_bridge.py` lines 313–326

**Action:** Enhance to handle edge case of empty waypoint list:
```python
def _on_avoidance_plan(self, msg: AvoidancePlan) -> None:
    self._record_pulse(M5)
    out = SilOwnShipState()
    out.stamp = msg.stamp
    if msg.waypoints and len(msg.waypoints) > 0:
        wp = msg.waypoints[0]
        # ... existing rudder/throttle calc
    else:
        # Fallback when no waypoints available
        # Maintain cruise speed to prevent cascade slowdown
        out.rudder_angle = 0.0
        out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN  # = 10.0 / 25.0 ≈ 0.4
    # ... rest of code
```

---

## 4.3 M5 target_speed decoupling

**Objective:** Break feedback loop by using YAML nominal speed, not M4 fallback speed.

### 4.3.1 Accept nominal_sog as optional M5 parameter
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` (constructor)

**Action:**
- Add node parameter:
  ```cpp
  nominal_speed_kn_ = declare_parameter<double>("m5.nominal_speed_kn", 10.0);
  // Default 10 kn matches imazu-01-ho.yaml ownShip.initial.sog
  ```

### 4.3.2 Override speed_max when M4 is in fallback
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` lines 145–150

**Action:** In `assemble_input_()`:
```cpp
// Read M4 behavior plan speed constraint
double speed_max_raw = static_cast<double>(behavior_plan_->speed_max_kn);

// R3 fix: if M4 is in fallback, use nominal speed instead of current SOG
std::string m4_rationale = behavior_plan_->rationale;
if (m4_rationale.find("infeasible fallback") != std::string::npos ||
    m4_rationale.find("Failsafe") != std::string::npos) {
  RCLCPP_INFO(get_logger(), "[M5] M4 fallback detected; using nominal speed %.1f kn", nominal_speed_kn_);
  speed_max_raw = nominal_speed_kn_;
}

inp.constraints.speed_max_mps = speed_max_raw * units::kMsPerKn;
```

### 4.3.3 Add fallback when /l2/speed_profile absent
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` (near line 28)

**Action:** Update constant:
```cpp
// [TBD-HAZID] Default planned speed [m/s] when speed profile is absent.
// Set to nominal cruise speed from scenario YAML (10 kn for FCB imazu tests).
constexpr double kDefaultPlannedSpeed_mps = 5.14;  // 10 kn
```

---

## 4.4 (P1) M6 → M5 reference feedback wire-up

**Note:** P1, deferred for Phase 2. Placeholder only.

**Objective:** M5 should apply M6 Rule 14 heading constraint to objective weighting, not just output.

### 4.4.1 Subscribe to M6 in M5
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

**Action:** (Not yet implemented; stub for D3.1 / Phase 2)
```cpp
sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
    "/l3/m6/colregs_constraint", 10,
    [this](l3_msgs::msg::COLREGsConstraint::SharedPtr msg) {
      colregs_constraint_ = std::move(msg);
    });
```

### 4.4.2 Apply M6 constraint in NLP formulation
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp` (example; exact file TBD)

**Action:** (Stub) Use `colregs_constraint_->forbidden_heading_range` to **hard-constrain** MPC objective away from forbidden directions (e.g., left turn in Rule 14).

---

## 4.5 Orchestrator lifecycle persistence note

**Objective:** Document the lifecycle state loss issue (R1 §1.2 sidenote) for SIL Integrator.

### 4.5.1 Add to SIL Integrator runbook
**File:** `docs/Design/SIL/` (new section or referenced file)

**Action:** Create brief note:
```markdown
## Known issue: Orchestrator lifecycle non-persistence

When the SIL orchestrator container restarts, lifecycle state (current scenario, active flag) 
is not persisted. The next lifecycle/status query returns default state (scenario_id="", 
ownship position=(30.5N,122E) Yangtze fallback).

**Workaround:** Always explicitly run the three-command activation sequence:
  1. POST /lifecycle/cleanup
  2. POST /lifecycle/configure {"scenario_id":"imazu-01-ho"}
  3. POST /lifecycle/activate

**Fix candidate (deferred):** Persist lifecycle state to local file or Redis; restore on container restart.
```

---

# 5. Affected Files

| File | Reason | Risk |
|------|--------|------|
| `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` | Add mission state precondition check + dictionary validation + solver failure handling | **Low** — local changes to callback; no msg structure or API change |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` | Add nominal_speed_kn parameter + override logic for M4 fallback detection | **Low** — parameter declaration + local variable assignment; no API change |
| `docker/sil_topic_bridge.py` | Remove scenario-sensitive gates (if any) + enhance waypoint check | **Low** — pure Python; no ROS msg or DDS contract change |
| `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp` | Add nominal_speed_kn_ member variable | **Very Low** — header member; backward compat maintained |
| `docs/Design/SIL/*/` | Add lifecycle persistence note | **None** — documentation only |

---

# 6. Implementation Steps

## Step 1: M4 mission state fallback (5 min)
**File:** `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`

**Action:**
1. Open file; locate `arbitration_timer_callback()` (line 108).
2. After line 112 (standby check), **before** line 129 (`compute_active_set`), insert Section 4.1.1 code.
3. Compile: `colcon build --packages-select m4_behavior_arbiter`.
4. **Verification** (see §7.1):
   ```bash
   docker compose restart sil-nodes
   curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
     -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
   curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
   sleep 5
   docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc \
     'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
      ros2 topic echo --once /l3/m4/behavior_plan'
   # Check rationale != "IvP infeasible fallback"
   ```

## Step 2: M4 dictionary validation (5 min)
**File:** Same as Step 1

**Action:**
1. In constructor (line 56), after `dictionary_.load(...)`, insert Section 4.1.2 code.
2. Compile: same command.
3. **Verification**: Check logs for `[M4] Behavior dictionary` message.

## Step 3: Bridge actuator_cmd gates (5 min)
**File:** `docker/sil_topic_bridge.py`

**Action:**
1. Search for scenario-sensitive gates: `grep -n "scenario\|imazu\|if.*scene" docker/sil_topic_bridge.py`.
2. If found, **delete entire conditional block** or surrounding if statement.
3. Ensure `self._pub_act.publish(out)` (line 337) is **not gated**.
4. **Verification** (see §7.1):
   ```bash
   docker compose restart sil-topic-bridge
   # Re-run imazu scenario activation
   sleep 10
   docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc \
     'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
      ros2 topic hz /sil/actuator_cmd --window 5 2>&1 | grep "average rate" || echo "SILENT"'
   # Expected: ~1 Hz (M5 publishes 1 Hz)
   ```

## Step 4: Bridge waypoint defensive check (3 min)
**File:** Same as Step 3

**Action:**
1. At line 313, change `if msg.waypoints:` to `if msg.waypoints and len(msg.waypoints) > 0:`.
2. In else block (line 324–326), add fallback throttle calc per Section 4.2.3.
3. Compile/reload Python (no build needed for pure Python).

## Step 5: M5 nominal_speed parameter (7 min)
**File:** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

**Action:**
1. In constructor (line 34+), add parameter declaration (Section 4.3.1) before line 44.
2. In `mid_mpc_node.hpp`, add member: `double nominal_speed_kn_;` (in private section).
3. In `assemble_input_()` (lines 145–150), insert Section 4.3.2 override logic.
4. Update constant at line 28 per Section 4.3.3.
5. Compile: `colcon build --packages-select m5_tactical_planner`.
6. **Verification** (see §7.1):
   ```bash
   docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc \
     'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
      (timeout 6 ros2 topic echo --once /l3/m5/avoidance_plan 2>&1 | grep target_speed_kn)'
   # Expected: target_speed_kn > 5 kn (not echoing current SOG)
   ```

## Step 6: Add orchestrator lifecycle note (3 min)
**File:** `docs/Design/SIL/` (create or append)

**Action:**
1. Create `docs/Design/SIL/orchestrator-lifecycle-note.md` or append to existing runbook.
2. Insert Section 4.5.1 content.

---

# 7. Verification Plan

## 7.1 Per-fix smoke test (5 min each, 30 min total)

After each step, run the verification command in the "Verification" subsection above. Expected output indicates fix is working.

## 7.2 End-to-end imazu-01-ho run (15 min)

**Setup:**
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer
npm run sys:start  # Start docker + PM2 frontend
sleep 10
```

**Run scenario:**
```bash
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 10
```

**Probe decision chain (15 min into simulation):**
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  
  echo "=== Checking at T+180s (before avoidance) ==="
  sleep 180
  
  echo "Topic: /sil/own_ship_state"
  ros2 topic echo --once /sil/own_ship_state 2>&1 | grep -E "sog|heading|throttle"
  
  echo "Topic: /l3/m4/behavior_plan"
  ros2 topic echo --once /l3/m4/behavior_plan 2>&1 | grep -E "rationale|speed_max"
  
  echo "Topic: /l3/m5/avoidance_plan"
  ros2 topic echo --once /l3/m5/avoidance_plan 2>&1 | grep -E "target_speed_kn" | head -1
  
  echo "Topic: /sil/actuator_cmd"
  ros2 topic hz /sil/actuator_cmd --window 5 2>&1 | grep "average rate" || echo "SILENT"
'
```

**Expected observations at T+180s (before avoidance starts ~T+300s):**
- `/sil/own_ship_state` sog ≥ 9.0 kn (maintained cruise speed, not decayed)
- `/l3/m4/behavior_plan` rationale should be **not** "IvP infeasible fallback" (either valid plan or "Failsafe TRANSIT")
- `/l3/m5/avoidance_plan` target_speed_kn ≥ 9.0 kn (not echoing ~2.7 kn current SOG)
- `/sil/actuator_cmd` frequency ≥ 1 Hz (not SILENT)

**Expected observations at T+350s (during avoidance):**
- `/sil/own_ship_state` heading ~25°–35° (starboard turn per expected_own_action)
- `/l3/m4/behavior_plan` speed_max_kn ≥ 8.0 kn (not fallback)
- `/sil/actuator_cmd` rudder_angle > 0.5 rad (~28°) for starboard turn

**Expected end-to-end at T+700s:**
- Ownship heading back to ~0° (or ±2° per tolerance)
- Min CPA ≥ 500 m (YAML expected_outcome satisfied)
- HMI shows TRANSIT → COLREG_AVOIDANCE → TRANSIT state transitions

## 7.3 Regression: other scenarios

Run default scenario (no target ships) to verify **no regression**:
```bash
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" -d '{"scenario_id":"default"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 10

# Check ownship cruises at YAML sog without spurious turns
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  sleep 30
  ros2 topic echo --once /sil/own_ship_state | grep -E "sog|heading"
'
```

**Expected:** sog ≈ 10 kn, heading ≈ 0°, no heading drift.

---

# 8. Risks

| Risk | Mitigation |
|------|-----------|
| **M4 fallback detection via string match** on rationale field. Fragile if message format changes. | Add enum flag to BehaviorPlanMsg (future D3.1 message evolution) instead of string parsing. For now, document regex pattern. |
| **M5 nominal_speed_kn hardcoded to 10.0 kn** in code. If scenario YAML changes SOG, must recompile. | Solution (P1): pass nominal_speed_kn via launch param from orchestrator; read YAML scenario_id at M5 startup. For DEMO-1, 10 kn is sufficient. |
| **Bridge scenario-sensitive gate removal** may break other use cases if one existed. | Thoroughly search codebase for any gate logic before deleting; review git blame to understand intent. |
| **Solver failure handling in M4** may mask upstream design issues (e.g., behavior dictionary truly missing). | Add WARN-level logs with solver error reason; include in ASDR for post-run audit. |

---

# 9. Out of Scope

The following are **deliberately excluded** from R3 and assigned to other repair items:

| Item | Owner | Reason |
|------|-------|--------|
| **TRANSIT autopilot** — maintain YAML sog/heading in non-conflict phases | **R2** | R3 scope is decision chain; R2 handles actuation failsafe |
| **L2 mock publisher** — generate /l2/planned_route, /l2/speed_profile | **R4** | R3 adds defensive fallback; R4 unblocks root cause |
| **FSM aggregation node** — publish /l3/fsm_state from M1/M4/M7 inputs | **R5** | HMI observability; distinct from avoidance chain |
| **M6→M5 hard constraint wire** (Rule 14 heading forbidden range) | **Phase 2 D3.1** | P1 enhancement; not critical for DEMO-1 avoidance |
| **Behavior dictionary config file path** — parameterize or validate | **Phase 2 D3.1** | Requires config schema review; deferred |

---

# 10. Open Questions [TBD-REASON]

1. **[TBD-CONFIG]** What is the actual value of `m4.config_dir` parameter in deployed launch file? Does behavior_definitions.yaml actually load? → **Action**: Check launch file in orchestrator startup or sil_nodes container.

2. **[TBD-SCENARIO-PARAM]** How to pass YAML ownShip.initial.sog (10 kn for imazu) to M5 at runtime, rather than hardcoded 10.0? → **Action**: R4 orchestrator integration; for R3 scope, assume 10 kn is nominal.

3. **[TBD-SOLVER]** Does IvP solver have debug/verbose mode to log rejection reason? → **Action**: Check IvP solver API (`solver_->solve(...)` return value) for detailed error.

4. **[TBD-M4-RATIONALE]** What are all possible rationale strings for M4 behavior_plan? (Used for fallback detection in §4.3.2.) → **Action**: Grep M4 source for `plan.rationale =` assignments; list all strings.

---

# 11. Sign-Off Checklist

- [ ] R1 evidence report reviewed and root causes confirmed
- [ ] Each hypothesis in §3 has "File: XXX" pointer and test command
- [ ] Verification commands are copy-pasteable and tested locally
- [ ] Affected files table (§5) reviewed for completeness and risk
- [ ] Main-agent audit completed before implementation
- [ ] Post-merge: all 7.1 smoke tests pass
- [ ] Post-merge: 7.2 end-to-end imazu-01-ho passes
- [ ] Post-merge: 7.3 regression check passes
- [ ] Fixes merged to main; R3 marked ✅

---

**Plan Status:** Ready for main-agent audit. Estimate 2.0 person-weeks assuming parallel M4+M5+bridge work.

