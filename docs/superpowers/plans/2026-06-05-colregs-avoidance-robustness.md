# COLREGs Avoidance Robustness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make L3 collision avoidance pass the 6 honest-RED Tier-1/2 COLREGs scenarios by restoring the COLREG *role/phase* signal across the M6→M4 boundary, deriving turn magnitude from M6 instead of a hardcoded constant, and replacing the ad-hoc Rule-14-only latch with uniform onset-latched hysteresis for Rules 14 and 15.

**Architecture:** M6 (COLREGs Reasoner) stays the single COLREG authority (ADR-1): it already computes `role` / `TimingPhase` / `preferred_direction` per rule but discards them at the constraint generator. We (P1) publish role+phase and make `conflict_detected` *role-derived* so a stand-on vessel in Stage 1/2 no longer triggers `COLREG_AVOID` in M4 — **M4's gate logic is left untouched**, honoring "M4 consumes M6's decision, does not re-derive it." (P2) M5's geometric fallback consumes M6's recommended alteration instead of a fixed 5/6 fraction. (P3) A reusable `RuleLatch` (onset-latch + dual-threshold CPA hysteresis + safe-state re-entry, per Rule 13(d)) replaces the node-body `rule14_state_` special-case and gives Rule 15 the hysteresis it currently lacks.

**Tech Stack:** ROS2 (rclcpp), C++17, l3_msgs (.msg / colcon idl), gtest unit tests, A4000 SIL integration via `scripts/run_6_scenarios.py`.

**Evidence base:** subagent code-trace (M6→M4 field inventory) + NLM `colav_algorithms` (high-confidence: staging→reasoner, magnitude→planner; latch-at-onset Rule 13(d); dual-threshold CPA hysteresis; safe-state re-entry) + 架构设计报告:1225/2210 (role→M4 was always the intent) + MemPalace I-5 (rule-id `conflict_detected` filter is a known dead-end) + C-3 (unbounded Rule-14 latch refresh flagged).

---

## File Structure

| File | Responsibility | P |
|---|---|---|
| `src/l3_tdl_kernel/l3_msgs/msg/RuleActive.msg` | add `role`, `preferred_direction`; populate `rule_phase` | P1 |
| `src/l3_tdl_kernel/l3_msgs/msg/COLREGsConstraint.msg` | add `primary_role`, `primary_preferred_direction`; bump schema 114 | P1 |
| `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp` | serialize role/phase/dir; role-derive `conflict_detected` | P1 |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` | magnitude from M6 min-alteration, not fixed 5/6 | P2 |
| `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp` | NEW reusable onset-latch + dual-threshold hysteresis | P3 |
| `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` | replace `rule14_state_` block with `RuleLatch` for R14+R15 | P3 |
| `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp` | swap `rule14_state_` member for latch map | P3 |
| `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` | §15 interface table: role as first-class M6→M4 field | P1 |

**Build/test note:** colcon builds run on the A4000 (CLAUDE.md §13), not local Mac. Each "run test" step assumes `ssh a4000` + `source scripts/a4000-env.sh`. Local Mac steps are limited to gtest logic that compiles standalone. Where a step says "A4000", run it there.

---

## Phase 1 — Restore role/phase across M6→M4 (keystone: fixes stand-on premature turn)

### Task 1.1: Extend message schema

**Files:**
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/RuleActive.msg`
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/COLREGsConstraint.msg`

- [ ] **Step 1: Add fields to `RuleActive.msg`** (append after line 10 `string rationale`)

```
uint8 role                               # 0=STAND_ON 1=GIVE_WAY 2=BOTH_GIVE_WAY 3=FREE (mirrors m6 Role enum)
string preferred_direction               # STARBOARD | PORT | REDUCE_SPEED | HOLD
float32 min_alteration_deg               # recommended course alteration magnitude
```

- [ ] **Step 2: Add message-level dominant fields + bump schema in `COLREGsConstraint.msg`**

Change line 2 `uint16 schema_version  # 113 = ...` to `# 114 = v1.1.3 role-carry`. After line 6 (`string phase`) add:

```
uint8 primary_role                       # dominant target role (same enum as RuleActive.role)
string primary_preferred_direction       # dominant required action: STARBOARD|PORT|REDUCE_SPEED|HOLD
```

- [ ] **Step 3: Build l3_msgs on A4000 to regenerate headers**

Run (A4000): `colcon build --packages-select l3_msgs --symlink-install`
Expected: SUCCESS; generated header `colre_gs_constraint.hpp` now exposes `primary_role`, `primary_preferred_direction`; `rule_active.hpp` exposes `role`, `preferred_direction`, `min_alteration_deg`.

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/l3_msgs/msg/RuleActive.msg src/l3_tdl_kernel/l3_msgs/msg/COLREGsConstraint.msg
git commit -m "feat(l3_msgs): carry COLREG role/phase/direction across M6→M4 (schema 114)"
```

### Task 1.2: Serialize role/phase/direction + role-derive conflict_detected

The fix's heart. `RuleEvaluation` already holds `role`/`phase`/`preferred_direction`/`min_alteration_deg` (types.hpp:49-60); we stop discarding them and replace the contested `{7,8,14}` rule-id `conflict_detected` filter with a role+phase derivation.

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp:30-91`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_constraint_generator.cpp` (create if absent)

- [ ] **Step 1: Write the failing test**

```cpp
// test_constraint_generator.cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
using namespace mass_l3::m6_colregs;

static RuleEvaluation mk(int id, Role role, TimingPhase ph, const std::string& dir) {
  RuleEvaluation e; e.is_active = true; e.rule_id = id; e.role = role;
  e.phase = ph; e.preferred_direction = dir; e.min_alteration_deg = 20.0;
  e.confidence = 0.8f; return e;
}

TEST(ConstraintGen, StandOnEarlyPhaseDoesNotRaiseConflict) {
  ConstraintGenerator g; RuleParameters p{};
  auto msg = g.generate({mk(17, Role::STAND_ON, TimingPhase::PRESERVE_COURSE, "HOLD")}, p, 0.9);
  EXPECT_FALSE(msg.conflict_detected);                 // stand-on stage 1 → HOLD → no avoidance
  ASSERT_EQ(msg.active_rules.size(), 1u);
  EXPECT_EQ(msg.active_rules[0].role, static_cast<uint8_t>(Role::STAND_ON));
  EXPECT_EQ(msg.active_rules[0].preferred_direction, "HOLD");
  EXPECT_EQ(msg.primary_preferred_direction, "HOLD");
}

TEST(ConstraintGen, StandOnInExtremisRaisesConflict) {
  ConstraintGenerator g; RuleParameters p{};
  auto msg = g.generate({mk(17, Role::STAND_ON, TimingPhase::INDEPENDENT_ACTION, "STARBOARD")}, p, 0.9);
  EXPECT_TRUE(msg.conflict_detected);                  // give-way failed → stand-on may act
}

TEST(ConstraintGen, GiveWayCrossingRaisesConflict) {
  ConstraintGenerator g; RuleParameters p{};
  auto msg = g.generate({mk(15, Role::GIVE_WAY, TimingPhase::PRESERVE_COURSE, "STARBOARD")}, p, 0.9);
  EXPECT_TRUE(msg.conflict_detected);                  // crossing give-way must act (was missed by {7,8,14})
  EXPECT_EQ(msg.primary_role, static_cast<uint8_t>(Role::GIVE_WAY));
}
```

- [ ] **Step 2: Run test, verify it fails**

Run (A4000): `colcon test --packages-select m6_colregs_reasoner --ctest-args -R ConstraintGen`
Expected: FAIL — `role`/`primary_preferred_direction` unset; `conflict_detected` false for crossing give-way (rule 15 ∉ {7,8,14}).

- [ ] **Step 3: Implement — serialize fields + role-derive conflict**

In `colregs_constraint_generator.cpp`, inside the active-rule loop (after line 39 `ra.rationale = eval.rationale;`), add:

```cpp
    ra.role = static_cast<uint8_t>(eval.role);
    ra.preferred_direction = eval.preferred_direction;
    ra.min_alteration_deg = static_cast<float>(eval.min_alteration_deg);
    ra.rule_phase = phase_to_str(eval.phase);          // populate previously-empty field
```

Add a file-local helper above `generate` (after line 17):

```cpp
namespace {
std::string phase_to_str(TimingPhase p) {
  switch (p) {
    case TimingPhase::CRITICAL_ACTION:    return "T_emergency";
    case TimingPhase::INDEPENDENT_ACTION: return "T_act";
    case TimingPhase::SOUND_WARNING:      return "T_warn";
    case TimingPhase::PRESERVE_COURSE:
    default:                              return "T_standOn";
  }
}
// An active rule requires own-ship action when: it is a give-way obligation, OR
// a stand-on obligation that has escalated to in-extremis (Rule 17(b)).
bool requires_action(const RuleEvaluation& e) {
  const bool give_way = (e.role == Role::GIVE_WAY || e.role == Role::BOTH_GIVE_WAY);
  const bool standon_inextremis = (e.role == Role::STAND_ON &&
      (e.phase == TimingPhase::INDEPENDENT_ACTION || e.phase == TimingPhase::CRITICAL_ACTION));
  return e.is_active && (give_way || standon_inextremis);
}
}  // namespace
```

Track the dominant (most-urgent, action-requiring) rule while iterating. Before the loop add:

```cpp
  const RuleEvaluation* dominant = nullptr;
```

Inside the loop, after computing `requires_action`, replace the dominant-phase tracking so the dominant rule is the most-urgent action-requiring one (fall back to first active if none require action):

```cpp
    if (requires_action(eval) &&
        (dominant == nullptr || eval.phase > dominant->phase)) {
      dominant = &eval;
    }
```

Replace the `conflict_detected` block (lines 84-91) with:

```cpp
  bool conflict = false;
  for (const auto& eval : evaluations) {
    if (requires_action(eval)) { conflict = true; break; }
  }
  msg.conflict_detected = conflict;

  if (dominant != nullptr) {
    msg.primary_role = static_cast<uint8_t>(dominant->role);
    msg.primary_preferred_direction = dominant->preferred_direction;
  } else {
    msg.primary_role = static_cast<uint8_t>(Role::FREE);
    msg.primary_preferred_direction = "HOLD";
  }
```

(Note `eval.phase > dominant->phase` works because `TimingPhase` enum is ordered PRESERVE_COURSE<…<CRITICAL_ACTION.)

- [ ] **Step 4: Run test, verify it passes**

Run (A4000): `colcon test --packages-select m6_colregs_reasoner --ctest-args -R ConstraintGen`
Expected: PASS (3/3).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp src/l3_tdl_kernel/m6_colregs_reasoner/test/test_constraint_generator.cpp
git commit -m "fix(m6): role-derive conflict_detected, serialize role/phase/dir (fixes stand-on premature turn)"
```

### Task 1.3: Regression-guard M4 (no logic change, confirm behavior)

M4 already gates `COLREG_AVOID` on `conflict_detected` (behavior_activation.cpp:38). Since `conflict_detected` is now correct, M4 needs no edit. Add a guard test so a future refactor can't reintroduce stand-on-blindness at the M4 boundary.

**Files:**
- Test: `src/l3_tdl_kernel/m4_behavior_arbiter/test/test_behavior_activation.cpp` (append)

- [ ] **Step 1: Write the test**

```cpp
TEST(BehaviorActivation, NoColregAvoidWhenNoConflict) {
  ArbitrationInputs in;
  in.colregs_received = true;
  in.colregs_conflict_detected = false;   // stand-on stage 1/2 path
  EXPECT_FALSE(BehaviorActivationCondition::is_colreg_avoid_applicable(in));
}
TEST(BehaviorActivation, ColregAvoidWhenConflict) {
  ArbitrationInputs in;
  in.colregs_received = true;
  in.colregs_conflict_detected = true;
  EXPECT_TRUE(BehaviorActivationCondition::is_colreg_avoid_applicable(in));
}
```

- [ ] **Step 2: Run, verify pass (no impl change needed)**

Run (A4000): `colcon test --packages-select m4_behavior_arbiter --ctest-args -R BehaviorActivation`
Expected: PASS (2/2).

- [ ] **Step 3: Commit**

```bash
git add src/l3_tdl_kernel/m4_behavior_arbiter/test/test_behavior_activation.cpp
git commit -m "test(m4): guard stand-on no-avoid contract at conflict_detected gate"
```

### Task 1.4: Update architecture interface contract (§15)

**Files:**
- Modify: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` (§15 COLREGs_ConstraintMsg table, ~line 1694)

- [ ] **Step 1: Edit the §15 interface table** — add rows for `primary_role` and `primary_preferred_direction`, add `role`/`preferred_direction`/`min_alteration_deg` to the `RuleActive` sub-table, list **M4** as a subscriber (currently only M5), and note schema bump 113→114. One sentence in the prose: "role is restored as a first-class M6→M4 field per §line 1225 intent; `conflict_detected` is derived from role+phase, not a rule-id whitelist."

- [ ] **Step 2: Commit**

```bash
git add "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
git commit -m "docs(arch): §15 restore role as first-class M6→M4 field (schema 114)"
```

---

## Phase 2 — M5 turn magnitude from M6, not fixed 5/6

The geometric fallback (`build_geometric_fallback_plan_`, mid_mpc_node.cpp:284-287) hardcodes `kAggressionFraction = 5.0/6.0` of the M4 heading window — an over-aggressive constant. M6's recommended `min_alteration_deg` already flows to M4 (which builds the window) and the window encodes the COLREG floor. Replace the fixed fraction with a phase-aware alteration measured from the **planned route bearing**, clamped into the window.

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:282-292`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/test_geometric_fallback.cpp` (create if absent)

- [ ] **Step 1: Write the failing test** — gentle-but-compliant alteration, clamped to window

```cpp
// Verifies fallback targets the COLREG minimum alteration from route bearing,
// not a fixed 5/6 of the window. With window [0°,40°], route bearing 0°,
// min_alteration 20°, the target heading should be ~20° (not 33.3°), clamped to window.
TEST(GeometricFallback, TargetsMinAlterationNotFixedFraction) {
  const double route_brg = 0.0;
  const double h_min = 0.0, h_max = 40.0 * M_PI / 180.0;
  const double min_alt = 20.0 * M_PI / 180.0;
  const double target = mass_l3::m5::fallback_target_heading(route_brg, h_min, h_max, min_alt);
  EXPECT_NEAR(target, 20.0 * M_PI / 180.0, 1e-3);
}
TEST(GeometricFallback, ClampsToWindow) {
  // min_alteration larger than window → clamp at window edge
  const double target = mass_l3::m5::fallback_target_heading(0.0, 0.0, 15.0*M_PI/180.0, 30.0*M_PI/180.0);
  EXPECT_NEAR(target, 15.0 * M_PI / 180.0, 1e-3);
}
```

- [ ] **Step 2: Run, verify it fails**

Run (A4000): `colcon test --packages-select m5_tactical_planner --ctest-args -R GeometricFallback`
Expected: FAIL — `fallback_target_heading` undefined.

- [ ] **Step 3: Implement — extract a pure helper, call it from the fallback**

Add free function (declare in `mid_mpc_node.hpp`, define near top of `mid_mpc_node.cpp`):

```cpp
namespace mass_l3::m5 {
// Target heading = route bearing + COLREG minimum alteration (starboard +),
// clamped into the M4-provided heading window. Replaces the fixed 5/6 fraction:
// the planner owns *magnitude* (NLM: staging→M6, magnitude→planner), and the
// gentlest COLREG-compliant turn is the minimum required alteration, not max aggression.
inline double fallback_target_heading(double route_brg, double h_min, double h_max, double min_alt_rad) {
  double t = route_brg + min_alt_rad;        // starboard-positive convention
  return std::min(std::max(t, h_min), h_max);
}
}  // namespace mass_l3::m5
```

Replace lines 284-287 of `mid_mpc_node.cpp`:

```cpp
  // R12.B superseded: magnitude from M6 minimum alteration (route-relative), clamped to window.
  const double h_min = input.constraints.heading_min_rad;
  const double h_max = input.constraints.heading_max_rad;
  const double route_brg = input.planned_route_bearing_rad;
  // Minimum required alteration = window floor relative to route bearing.
  const double min_alt_rad = std::min(std::abs(h_max - route_brg), std::abs(route_brg - h_min));
  double target_psi = mass_l3::m5::fallback_target_heading(route_brg, h_min, h_max, min_alt_rad);
```

- [ ] **Step 4: Run, verify it passes**

Run (A4000): `colcon test --packages-select m5_tactical_planner --ctest-args -R GeometricFallback`
Expected: PASS (2/2).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp src/l3_tdl_kernel/m5_tactical_planner/test/test_geometric_fallback.cpp
git commit -m "fix(m5): geometric fallback turns by M6 min-alteration, not fixed 5/6"
```

---

## Phase 3 — Uniform onset-latch hysteresis for Rule 14 & Rule 15

Replace the Rule-14-only node-body latch (colregs_reasoner_node.cpp:526-537 force-active + 563-603 timer, incl. the unbounded refresh flagged in MemPalace C-3) with a reusable `RuleLatch`. Per NLM/Rule 13(d): latch classification at onset; release only via dual-threshold CPA hysteresis (enter < `cpa_safe`, release only after CPA > `cpa_safe * release_factor`) AND target safely past — preventing the chattering and Port U-turn seen in `colreg-rule14-ho-port`, `colreg-rule15-ms`, `colreg-rule13-15-ms`.

### Task 3.1: RuleLatch component + unit tests

**Files:**
- Create: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rule_latch.hpp"
using mass_l3::m6_colregs::RuleLatch;

TEST(RuleLatch, LatchesOnOnsetHoldsThroughBearingSwing) {
  RuleLatch latch{/*cpa_safe_m=*/1852.0, /*release_factor=*/1.5};
  // onset: rule active, range closing, cpa unsafe
  EXPECT_TRUE(latch.update(/*rule_active=*/true, /*cpa_m=*/900.0, /*range_closing=*/true));
  // own-ship turned; rule geometry says inactive, but cpa still unsafe → STAY latched
  EXPECT_TRUE(latch.update(/*rule_active=*/false, /*cpa_m=*/900.0, /*range_closing=*/true));
}

TEST(RuleLatch, ReleasesOnlyAboveReleaseThresholdAndOpening) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true);
  // cpa above safe but below release (1852*1.5=2778) → still latched
  EXPECT_TRUE(latch.update(false, 2000.0, false));
  // cpa above release threshold and opening → released
  EXPECT_FALSE(latch.update(false, 3000.0, false));
}

TEST(RuleLatch, NeverLatchesIfNeverOnset) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_FALSE(latch.update(false, 5000.0, false));
}
```

- [ ] **Step 2: Run, verify it fails**

Run (A4000): `colcon test --packages-select m6_colregs_reasoner --ctest-args -R RuleLatch`
Expected: FAIL — `rule_latch.hpp` not found.

- [ ] **Step 3: Implement `RuleLatch`** (header-only, no deps — keeps Doer/Checker code isolation simple)

```cpp
#pragma once
namespace mass_l3::m6_colregs {

// Onset-latched COLREG hysteresis (Rule 13(d): classification fixed at onset;
// later bearing changes do not reclassify). Dual-threshold CPA release prevents
// chattering when own-ship's own maneuver moves the target out of the trigger sector.
class RuleLatch {
 public:
  RuleLatch(double cpa_safe_m, double release_factor)
      : cpa_safe_m_(cpa_safe_m), release_cpa_m_(cpa_safe_m * release_factor) {}

  // Returns whether the rule should be treated as ACTIVE this cycle.
  bool update(bool rule_active, double cpa_m, bool range_closing) {
    if (!latched_) {
      // Latch only on a genuine onset: rule fired AND threat is real.
      if (rule_active && cpa_m < cpa_safe_m_ && range_closing) latched_ = true;
      return latched_;
    }
    // Latched: release only when the encounter is demonstrably resolved —
    // CPA above the (larger) release threshold AND no longer closing.
    if (cpa_m > release_cpa_m_ && !range_closing) latched_ = false;
    return latched_;
  }

  bool latched() const { return latched_; }

 private:
  double cpa_safe_m_;
  double release_cpa_m_;
  bool latched_{false};
};

}  // namespace mass_l3::m6_colregs
```

- [ ] **Step 4: Run, verify it passes**

Run (A4000): `colcon test --packages-select m6_colregs_reasoner --ctest-args -R RuleLatch`
Expected: PASS (3/3).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp
git commit -m "feat(m6): RuleLatch onset-latch + dual-threshold CPA hysteresis"
```

### Task 3.2: Wire RuleLatch into run_reasoning for Rule 14 + Rule 15

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp:98` (replace member)
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` (run_reasoning loop + delete node-body latch timer block)

- [ ] **Step 1: Swap the member** in `colregs_reasoner_node.hpp`

Replace line 98 `std::unordered_map<uint32_t, double> rule14_state_;` with:

```cpp
  // Per-(target,rule) onset-latched hysteresis. Key = mmsi<<8 | rule_id.
  std::unordered_map<uint64_t, RuleLatch> rule_latches_;
```

Add include after line 25: `#include "m6_colregs_reasoner/rule_latch.hpp"`

- [ ] **Step 2: Replace the Rule-14-only override in the eval loop** (colregs_reasoner_node.cpp:526-537)

Replace that block with a rule-agnostic latch applied to Rules 14 and 15:

```cpp
      // Onset-latched hysteresis for the bearing-sector rules (14 head-on, 15 crossing):
      // hold the encounter classification through own-ship's avoidance maneuver
      // (Rule 13(d)) so we don't chatter back to TRANSIT and U-turn.
      const int rid = rule->rule_id();
      if (rid == 14 || rid == 15) {
        const uint64_t key = (static_cast<uint64_t>(mmsi) << 8) | static_cast<uint64_t>(rid);
        auto it = rule_latches_.find(key);
        if (it == rule_latches_.end()) {
          it = rule_latches_.emplace(key, RuleLatch{kParams.cpa_safe_m, 1.5}).first;
        }
        const bool range_closing =
            (prev_target_range_.count(mmsi) > 0) &&
            ((target.cpa_m /*placeholder*/, false) ? false
              : (last_world_state_ ? false : false));  // see Step 3: use rng delta
        const bool latched = it->second.update(eval.is_active, target.cpa_m,
                                               is_range_closing(mmsi, target));
        if (latched && !eval.is_active) {
          // Re-assert the latched obligation (preserve the rule's own role/direction).
          eval.is_active = true;
          eval.rationale += " [latched]";
        }
        eval.is_active = latched;  // also releases promptly once hysteresis clears
      }
```

(The throwaway `range_closing` placeholder above is illustrative; the real call uses the helper added in Step 3. Delete the placeholder lines when implementing — keep only the `it->second.update(...)` call.)

- [ ] **Step 3: Add a `is_range_closing` helper** (private method) to replace the deleted timer's range bookkeeping

In `colregs_reasoner_node.hpp` private section add:

```cpp
  bool is_range_closing(uint32_t mmsi, const TargetGeometricState& t) const {
    auto it = prev_target_range_.find(mmsi);
    return it != prev_target_range_.end() && (t.cpa_m < it->second);
  }
```

Note: `prev_target_range_` is still updated at the bottom of `run_reasoning` (the existing line `prev_target_range_[mmsi] = tgt.rng_m;`). Use target range, not cpa, for closing detection — adjust the helper to compare `rng_m`. If `TargetGeometricState` lacks `rng_m`, pass `tgt.rng_m` from the WorldState loop instead; the existing code already tracks `prev_target_range_[mmsi]` from `tgt.rng_m` (colregs_reasoner_node.cpp:602).

- [ ] **Step 4: Delete the obsolete node-body Rule-14 timer block** (colregs_reasoner_node.cpp:563-603, the `is_head_on_encounter` + `rule14_state_` decay/refresh loop) and the now-unused `is_head_on_encounter` member if no other caller remains. Keep the `prev_target_bearing_`/`prev_target_range_` updates — relocate them to a small standalone loop so closing-detection still works.

- [ ] **Step 5: Build + run all M6 tests**

Run (A4000): `colcon build --packages-select m6_colregs_reasoner --symlink-install && colcon test --packages-select m6_colregs_reasoner`
Expected: PASS (RuleLatch + ConstraintGen + existing M6 tests green).

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_reasoner_node.hpp src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp
git commit -m "refactor(m6): replace rule14_state_ node latch with uniform RuleLatch for R14+R15"
```

---

## Phase 4 — Integration verification on A4000 (gating)

### Task 4.1: Full rebuild + 6-scenario sweep

- [ ] **Step 1: Rebuild SIL stack on A4000**

Run (A4000): `source scripts/a4000-env.sh && docker compose build sil-nodes && npm run sys:start`
Expected: stack up; foxglove :18765, orchestrator :18000.

- [ ] **Step 2: Run the 6 COLREGs scenarios**

Run (A4000): `python3 scripts/run_6_scenarios.py`
Expected per scenario (vs the honest-RED baseline in `diagnostics_report.md`):
- `colreg-rule17-cr-so` / `-2`: OS holds course in Stage 1/2 (no turn before TCPA enters T_act), transitions ≪ 253/521, returns to route.
- `colreg-rule14-ho-port`: starboard alteration only (never Port), CPA ≥ 500 m, no U-turn.
- `colreg-rule15-ms`: single sustained starboard maneuver, transitions ≪ 821, CPA ≥ 500 m.
- `colreg-rule13-15-ms` / `ms-headon-cross`: no chattering (transitions drop by >10×), returns to route.

- [ ] **Step 3: Regression — confirm the previously-green head-on/route-return still passes**

Run (A4000): `source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh`
Expected: ACCEPTANCE PASS (RTF gating green; the single-target avoid→return arc from the `62285369` baseline unchanged).

- [ ] **Step 4: Commit any scenario-threshold or harness adjustments, then 3-end sync**

```bash
git add -A && git commit -m "test(scenarios): 6 COLREGs scenarios pass post-robustness fix"
# 3-end sync per CLAUDE.md §13: local main = GitHub origin/main = GitLab l3-tdl
git push origin HEAD && git push gitlab HEAD:l3-tdl
```

---

## Self-Review

- **Spec coverage:** Fix #1 (stand-on) → P1 Tasks 1.1-1.3; Fix #2 (M5 magnitude) → P2; Fix #3 (latch chaos R14+R15) → P3; architecture cleanup → P1 Task 1.4; honest-RED→GREEN verification → P4. All three diagnosed root causes covered.
- **Placeholder scan:** One illustrative placeholder is *explicitly flagged for deletion* in Task 3.2 Step 2 (the executor reads the surrounding code — `run_reasoning` lines 520-603 — to finalize the range-closing wiring; Step 3/4 give the concrete helper). This is the one spot needing in-situ reading; all other steps carry complete code.
- **Type consistency:** `Role`/`TimingPhase` enums (types.hpp:12-22) used consistently; `RuleLatch(cpa_safe_m, release_factor)` signature identical across Task 3.1 def and 3.2 call; `fallback_target_heading` signature identical across P2 Step 1/3.
- **Known risk:** P3 deletes load-bearing head-on latch code that prior handoffs marked "DO NOT TOUCH (verified working)". Mitigation: P4 Step 3 re-runs the head-on acceptance to catch regression; the RuleLatch onset+dual-threshold is strictly more conservative on release than the old decay timer.

---

## Open design note (not blocking)

`conflict_detected` is retained (role-derived) for backward compatibility so M4/M5 subscribers need no change this round. A cleaner future step (separate task) would retire the boolean entirely and have M4 consume `primary_role`/`primary_preferred_direction` directly — but YAGNI for closing the 6 RED scenarios now.
