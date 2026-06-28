# M6 Rule5 Primary-Latch Follow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop Rule 5 (proper look-out) from churning in/out of the M6 `active_rules` set during an active encounter, by making its risk-gate follow the primary rule (13/14/15) latch lifecycle instead of instantaneous CPA.

**Architecture:** Add a pure `inline bool` helper `rule5_follows_primary_latch(...)` to `colregs_release_policy.hpp` (mirrors the existing `give_way_duty_from_raw_or_fsm` / `primary_rule_onset_allowed` pattern), call it from the non-primary risk-gate in `run_reasoning`, and unit-test the helper in `test_colregs_release_policy.cpp`. No change to `rule5_lookout.cpp`, the Rule 14 gate constants, oracle thresholds, or any other non-primary rule's gate.

**Tech Stack:** C++17, ROS2 (rclcpp), GTest, colcon, Docker (mass-l3-gnc stack), Python probe/oracle tooling.

**Spec:** `docs/superpowers/specs/2026-06-28-m6-rule5-primary-latch-follow-design.md`

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug` on branch `codex/colregs-12probe-debug`.

---

## Background (read before Task 1)

Rule 5's evaluator (`src/l3_tdl_kernel/m6_colregs_reasoner/src/rules/colregs/rule5_lookout.cpp`) returns `is_active=true` unconditionally. But `ColregsReasonerNode::run_reasoning()` applies a Rule 7 risk gate to all non-primary rules in the else-branch at `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp:923-942`:

```cpp
} else {  // non-primary rules (5/6/7/8/16/17/18/19)
  const bool give_way_role =
      (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
  if (!give_way_role) {
    const bool raw_risk =
        (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);
    if (!raw_risk) {
      eval.is_active = false;
    }
  }
}
```

During own ship's starboard avoidance turn the CPA projection transiently opens above `cpa_safe_m`, gating Rule 5 off; next cycle it re-activates. This high-frequency churn drives M6 RULE_INSTABILITY on `colreg-rule14-ho` and `colreg-rule14-ho-intelligent`.

The primary-rule latch iterators (`rule13_latch_it`, `rule14_latch_it`, `rule15_latch_it`) are already computed earlier in the same per-target block at `colregs_reasoner_node.cpp:681-683` and are in scope at the else-branch. `RuleLatch::latched()` (`include/m6_colregs_reasoner/rule_latch.hpp:83`) returns true from onset through release.

The existing pure-helper pattern to follow is in `include/m6_colregs_reasoner/colregs_release_policy.hpp` (e.g. `give_way_duty_from_raw_or_fsm` at line 48, `primary_rule_onset_allowed` at line 81) and tested in `test/test_colregs_release_policy.cpp`.

---

## File Structure

- **Modify** `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp` — add `rule5_follows_primary_latch` inline helper.
- **Modify** `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` — call the helper in the non-primary risk-gate else-branch (~line 936).
- **Modify** `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp` — add unit tests for the helper.

No new files. No change to `rule5_lookout.cpp`, `rule_latch.hpp`, oracle tooling, or GNC source.

---

## Task 1: Add failing unit tests for `rule5_follows_primary_latch`

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp`

- [ ] **Step 1: Write the failing tests**

Append these tests inside the existing anonymous namespace in `test_colregs_release_policy.cpp` (after the last `TEST(...)` block, before the closing `}  // namespace` at the file end):

```cpp
TEST(ColregsReleasePolicy, Rule5FollowsPrimaryLatchWhenRule14Latched) {
  // Rule 5 must stay active while a primary rule (here Rule 14) is latched for
  // this target, so the instantaneous-CPA risk gate cannot churn Rule 5 out.
  EXPECT_TRUE(rule5_follows_primary_latch(
      /*rule_id=*/5,
      /*rule13_latched=*/false,
      /*rule14_latched=*/true,
      /*rule15_latched=*/false));
}

TEST(ColregsReleasePolicy, Rule5FollowsPrimaryLatchWhenAnyPrimaryLatched) {
  EXPECT_TRUE(rule5_follows_primary_latch(
      /*rule_id=*/5,
      /*rule13_latched=*/false,
      /*rule14_latched=*/false,
      /*rule15_latched=*/true));
  EXPECT_TRUE(rule5_follows_primary_latch(
      /*rule_id=*/5,
      /*rule13_latched=*/true,
      /*rule14_latched=*/false,
      /*rule15_latched=*/false));
}

TEST(ColregsReleasePolicy, Rule5DoesNotFollowWhenNoPrimaryLatched) {
  // No primary rule latched: Rule 5 falls back to the normal risk gate.
  EXPECT_FALSE(rule5_follows_primary_latch(
      /*rule_id=*/5,
      /*rule13_latched=*/false,
      /*rule14_latched=*/false,
      /*rule15_latched=*/false));
}

TEST(ColregsReleasePolicy, Rule5BypassAppliesOnlyToRule5) {
  // Non-Rule-5 rules never take the follow path, regardless of latch state.
  EXPECT_FALSE(rule5_follows_primary_latch(
      /*rule_id=*/6,
      /*rule13_latched=*/true,
      /*rule14_latched=*/true,
      /*rule15_latched=*/true));
  EXPECT_FALSE(rule5_follows_primary_latch(
      /*rule_id=*/17,
      /*rule13_latched=*/true,
      /*rule14_latched=*/false,
      /*rule15_latched=*/false));
}
```

- [ ] **Step 2: Run tests to verify they fail (helper not defined)**

Run inside the sil-nodes container (the M6 package builds there):
```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   ./build/m6_colregs_reasoner/test_colregs_release_policy --gtest_filter='ColregsReleasePolicy.Rule5*' 2>&1 | tail -20"
```
Expected: build error `'rule5_follows_primary_latch' was not declared` (or linker/compile failure referencing the missing symbol).

---

## Task 2: Implement `rule5_follows_primary_latch` helper

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp`

- [ ] **Step 1: Add the helper after `primary_rule_onset_allowed` (line ~90)**

Insert immediately after the closing brace of `primary_rule_onset_allowed` (the function ending at line 90 with `latched_primary_rule_id == candidate_rule_id; }`):

```cpp
// Rule 5 (proper look-out) is a continuous obligation that must persist through
// an active encounter. The primary-rule latch (13/14/15) holds the encounter
// classification through own ship's avoidance maneuver (Rule 13(d)); Rule 5 must
// not be gated off by an instantaneous CPA transient while a primary rule is
// latched for this target, otherwise Rule 5 flaps in/out of the active set and
// triggers M6 RULE_INSTABILITY. Returns true only for Rule 5 itself when at
// least one primary rule latch is engaged.
inline bool rule5_follows_primary_latch(
    int rule_id,
    bool rule13_latched,
    bool rule14_latched,
    bool rule15_latched) {
  return rule_id == 5 && (rule13_latched || rule14_latched || rule15_latched);
}
```

- [ ] **Step 2: Rebuild and run the Task 1 tests to verify they pass**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   ./build/m6_colregs_reasoner/test_colregs_release_policy --gtest_filter='ColregsReleasePolicy.Rule5*' 2>&1 | tail -20"
```
Expected: `4 PASSED` (or however many Rule5* tests ran), build succeeds.

- [ ] **Step 3: Run the full release-policy test file to confirm no regression**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && ./build/m6_colregs_reasoner/test_colregs_release_policy 2>&1 | tail -5"
```
Expected: all tests PASS (pre-existing tests unchanged).

---

## Task 3: Wire the helper into `run_reasoning`

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` (else-branch ~line 935-940)

- [ ] **Step 1: Replace the non-primary risk-gate block**

Find this exact block in `colregs_reasoner_node.cpp` (inside `run_reasoning`, the `else` clause for non-primary rules):

```cpp
      } else {
        // COLREG Rule 7 (risk of collision) gate for NON-give-way obligations
        // (stand-on Rule 17, Rule 18 stand-on, Rule 5/6/19...): a rule must not
        // fire for a target posing no risk — already passed (tcpa < 0) or
        // clearing (cpa ≥ cpa_safe). Give-way roles are deliberately LEFT
        // un-gated here; the post-loop give-way duty-latch gate decides whether
        // they carry conflict (Rule 8(d) hysteresis). That both holds the
        // give-way carriers through own-ship's maneuver (CPA transiently opens)
        // and suppresses an unconfirmed blanket-CPA give_way (Rule 16) before
        // the encounter is classified / on a stand-on vessel (Rule 17).
        const bool give_way_role =
            (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
        if (!give_way_role) {
          const bool raw_risk =
              (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);
          if (!raw_risk) {
            eval.is_active = false;
          }
        }
      }
```

Replace it with (preserving the existing comment, adding the Rule 5 bypass):

```cpp
      } else {
        // COLREG Rule 7 (risk of collision) gate for NON-give-way obligations
        // (stand-on Rule 17, Rule 18 stand-on, Rule 5/6/19...): a rule must not
        // fire for a target posing no risk — already passed (tcpa < 0) or
        // clearing (cpa ≥ cpa_safe). Give-way roles are deliberately LEFT
        // un-gated here; the post-loop give-way duty-latch gate decides whether
        // they carry conflict (Rule 8(d) hysteresis). That both holds the
        // give-way carriers through own-ship's maneuver (CPA transiently opens)
        // and suppresses an unconfirmed blanket-CPA give_way (Rule 16) before
        // the encounter is classified / on a stand-on vessel (Rule 17).
        const bool give_way_role =
            (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
        if (!give_way_role) {
          const bool raw_risk =
              (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);
          // Rule 5 (proper look-out) is a continuous obligation that must
          // persist through an active encounter. While a primary rule
          // (13/14/15) is latched for this target, an instantaneous CPA
          // transient must not gate Rule 5 off — otherwise Rule 5 flaps in/out
          // of the active set and drives M6 RULE_INSTABILITY. The latch
          // iterators are computed earlier in this per-target block.
          const bool follows_primary_latch = rule5_follows_primary_latch(
              eval.rule_id,
              rule13_latch_it != rule_latches_.end() && rule13_latch_it->second.latched(),
              rule14_latch_it != rule_latches_.end() && rule14_latch_it->second.latched(),
              rule15_latch_it != rule_latches_.end() && rule15_latch_it->second.latched());
          if (!raw_risk && !follows_primary_latch) {
            eval.is_active = false;
          }
        }
      }
```

- [ ] **Step 2: Rebuild the M6 package to verify it compiles**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -8"
```
Expected: `Finished <<< m6_colregs_reasoner` with no errors. The `rule13/14/15_latch_it` iterators are declared at `colregs_reasoner_node.cpp:681-683` in the same per-target scope, so they resolve.

- [ ] **Step 3: Run the M6 unit test suite to confirm no regression**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ 2>&1 | tail -20"
```
Expected: all M6 tests PASS, including `test_rule5_lookout`, `test_rule14_head_on`, `test_colregs_release_policy`, `test_encounter_state_machine`.

- [ ] **Step 4: Run M5 unit tests to confirm no M5 regression**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -3 && \
   ./build/m5_tactical_planner/test_avoidance_waypoint_gen --gtest_color=no 2>&1 | tail -5"
```
Expected: 43/43 PASS (M5 untouched; this is a regression guard).

- [ ] **Step 5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp
git commit -m "fix(m6): Rule5 look-out follows primary rule latch to stop active-set churn

Rule 5 evaluator returns is_active=true unconditionally, but run_reasoning's
non-primary risk gate (instantaneous tcpa>=0 AND cpa<cpa_safe) churned Rule 5
in/out during own ship's starboard turn as the CPA projection transiently
opened. This drove M6 RULE_INSTABILITY on rule14-ho / rule14-ho-intelligent
(plan_id churn, M4 behavior toggles).

Add rule5_follows_primary_latch() pure helper: while any primary rule
(13/14/15) is latched for the target, Rule 5 skips the risk gate and stays
active through the encounter (Rule 13(d) hold). Falls back to the risk gate
after release. No change to rule5_lookout.cpp, Rule 14 gate constants, oracle
thresholds, or other non-primary rules.

Spec: docs/superpowers/specs/2026-06-28-m6-rule5-primary-latch-follow-design.md"
```

---

## Task 4: Rebuild GNC stack image and verify module oracle (Layer-2)

**Files:** none (runtime verification)

This task uses the formal fresh-image rebuild (not `docker cp`), matching the handoff gate. The source mount in the GNC stack reads from this worktree, so the Task 3 commit is picked up by `--build`.

- [ ] **Step 1: Rebuild the GNC stack from the worktree**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
Expected: both stacks up, 6 containers running (`codex-gnc-validation-sil-nodes-1`, `-sil-orchestrator-1`, `-foxglove-bridge-1`, `-martin-tile-server-1`, `-gnc-gnc-nodes-1`, `-gnc-gnc-bridge-1`). Wait ~45 s for lifecycle to settle.

- [ ] **Step 2: Health check**

```bash
curl -sk https://127.0.0.1:18000/api/v1/health
```
Expected: `{"status":"ok"}`.

- [ ] **Step 3: Run rule14-ho strict single-probe**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
source scripts/local-a4000-env.sh
TS=$(date +%Y%m%d_%H%M%S)
PROBE_STUCK_LIMIT=200 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc \
  --scenario colreg-rule14-ho \
  --restart-between-runs \
  --restart-settle 24 \
  --sim-rate 10 \
  --summary-out runs/rule14_ho_after_rule5_fix_${TS}.json \
  --trace-report-dir runs/trace_eval/${TS}_rule14_ho_after_rule5_fix
```
Expected: completes (exit 0), one trace dir with `colreg-rule14-ho.trace_current.jsonl` + `colreg-rule14-ho.json`.

- [ ] **Step 4: Run module oracle on the fresh rule14-ho trace**

```bash
TD=$(ls -dt runs/trace_eval/*rule14_ho_after_rule5_fix | head -1)
python3 scripts/run_colregs_module_oracle.py \
  --trace $TD/colreg-rule14-ho.trace_current.jsonl \
  --scenario colreg-rule14-ho \
  --out runs/module_oracle_rule14_ho_after_rule5_fix.json
```
Expected: `M6_COLREGsReasoner: [GREEN]` (was RED RULE_INSTABILITY). All 6 modules GREEN.

- [ ] **Step 5: Verify Rule 5 no longer exhibits sub-2s flip intervals**

```bash
python3 - <<'PY'
import json
path = "$(ls -dt runs/trace_eval/*rule14_ho_after_rule5_fix | head -1)/colreg-rule14-ho.trace_current.jsonl".replace("$(","")
import subprocess
td = subprocess.check_output(["ls","-dt","runs/trace_eval/*rule14_ho_after_rule5_fix"]).decode().split()[0]
path = f"{td}/colreg-rule14-ho.trace_current.jsonl"
prev=None; prev_t=None; short=[]
with open(path) as f:
    for line in f:
        e=json.loads(line)
        if e.get('topic')!='/l3/m6/colregs_constraint': continue
        msg=e.get('msg',e)
        ids=sorted({r.get('rule_id') for r in msg.get('active_rules',[])})
        t=msg.get('sim_t') or msg.get('header',{}).get('stamp',{}).get('sec')
        if ids!=prev and prev_t is not None and t is not None:
            dt=t-prev_t
            if dt<2.0: short.append((round(dt,2),t,5 in ids))
        if ids!=prev: prev=ids; prev_t=t
print(f"sub-2s Rule5 flips: {len(short)}")
assert len(short)==0, f"FAIL: still {len(short)} short flips: {short}"
print("PASS: no sub-2s Rule5 flip intervals")
PY
```
Expected: `PASS: no sub-2s Rule5 flip intervals`.

---

## Task 5: Regression — Rule 14 cohort (ho-port must stay GREEN)

**Files:** none (runtime verification)

- [ ] **Step 1: Run the Rule 14 cohort strict probe**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
source scripts/local-a4000-env.sh
TS=$(date +%Y%m%d_%H%M%S)
PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc \
  --scenario colreg-rule14-ho \
  --scenario colreg-rule14-ho-port \
  --scenario colreg-rule14-ho-intelligent \
  --restart-between-runs \
  --restart-settle 24 \
  --sim-rate 10 \
  --summary-out runs/rule14_cohort_after_rule5_fix_${TS}.json \
  --trace-report-dir runs/trace_eval/${TS}_rule14_cohort_after_rule5_fix
```
Expected: completes, three scenario trace dirs.

- [ ] **Step 2: Run module oracle on all three and check the Layer-2 verdict**

```bash
TD=$(ls -dt runs/trace_eval/*rule14_cohort_after_rule5_fix | head -1)
for sid in colreg-rule14-ho colreg-rule14-ho-port colreg-rule14-ho-intelligent; do
  python3 scripts/run_colregs_module_oracle.py \
    --trace $TD/$sid.trace_current.jsonl \
    --scenario $sid \
    --out runs/module_oracle_${sid}_after_rule5_fix.json 2>&1 | tail -10
  echo "---"
done
```
Expected:
- `colreg-rule14-ho`: M6 GREEN (was RED).
- `colreg-rule14-ho-intelligent`: M6 GREEN (was RED).
- `colreg-rule14-ho-port`: 6/6 GREEN (regression guard — must not break).

- [ ] **Step 3: Record the integration (Layer-3) verdict for each, but do NOT fix integration RED here**

```bash
TD=$(ls -dt runs/trace_eval/*rule14_cohort_after_rule5_fix | head -1)
for sid in colreg-rule14-ho colreg-rule14-ho-port colreg-rule14-ho-intelligent; do
  python3 -c "
import json
d=json.load(open('$TD/$sid.json'))
v=d['verdict']; cs=d.get('chain_summary',{}); diag=cs.get('diagnosis',{})
print('$sid: overall='+str(v['overall_pass'])+' colregs='+str(v['colregs_pass'])+' first_fail='+str(d.get('first_failure'))+' diag_stage='+str(diag.get('first_broken_stage')))
"
done
```
Expected: Layer-2 M6 GREEN for all three. Layer-3 may still be RED for ho-port/ho-intelligent from the separate Class B plan-id churn (out of scope here) — record it, do not tune thresholds or add scenario branches to force it green.

- [ ] **Step 4: Regression guard — Rule 5 gates off after encounter clears**

Confirm Rule 5 leaves `active_rules` once the primary rule releases and the target is past-and-clear (no latched primary, no collision risk). Inspect the tail of the rule14-ho trace where the encounter has resolved:

```bash
python3 - <<'PY'
import json, subprocess, glob
td = subprocess.check_output(["ls","-dt","runs/trace_eval/*rule14_cohort_after_rule5_fix"]).decode().split()[0]
path=f"{td}/colreg-rule14-ho.trace_current.jsonl"
samples=[]
with open(path) as f:
    for line in f:
        e=json.loads(line)
        if e.get('topic')!='/l3/m6/colregs_constraint': continue
        msg=e.get('msg',e)
        ids={r.get('rule_id') for r in msg.get('active_rules',[])}
        t=msg.get('sim_t')
        if t is not None: samples.append((t, 5 in ids, any(x in ids for x in (13,14,15))))
if not samples:
    print("SKIP: no m6 samples"); raise SystemExit(0)
tail = samples[-20:]
print("last 20 m6 samples (sim_t, rule5_active, any_primary_active):")
for s in tail: print(" ", s)
# After the encounter clears, neither rule5 nor primary should be active
cleared = [s for s in tail if not s[2]]
if cleared:
    assert all(not s[1] for s in cleared), f"FAIL: rule5 still active after primary cleared: {cleared}"
    print("PASS: rule5 gates off after encounter clears")
else:
    print("NOTE: primary still active in last 20 samples; manual check needed")
PY
```
Expected: `PASS: rule5 gates off after encounter clears` (Rule 5 returns to the risk gate post-release).

---

## Task 6: Record evidence and update handoff

**Files:**
- Modify: `handoff/workspace_log.md` (append entry)

- [ ] **Step 1: Append a handoff entry**

Append to `handoff/workspace_log.md`:

```markdown
## [2026-06-28] ZCode / commit <TASK3_SHA> / Class A fix: M6 Rule5 primary-latch follow

### Task Goal
Stop Rule 5 (look-out) churning in/out of M6 active_rules during an active head-on encounter, which drove M6 RULE_INSTABILITY on colreg-rule14-ho and colreg-rule14-ho-intelligent.

### Core Changes
- Added `rule5_follows_primary_latch()` inline helper in `colregs_release_policy.hpp`.
- Wired it into `run_reasoning` non-primary risk-gate: while any primary rule (13/14/15) is latched for the target, Rule 5 skips the instantaneous-CPA risk gate and stays active through the encounter (Rule 13(d) hold). Falls back to the risk gate after release.
- Added 4 unit tests in `test_colregs_release_policy.cpp`.
- No change to rule5_lookout.cpp, Rule 14 gate constants, oracle thresholds, or other non-primary rules.

### Current Status
- Layer-2 M6 oracle: rule14-ho GREEN, rule14-ho-intelligent GREEN, rule14-ho-port GREEN (regression guard).
- No sub-2s Rule 5 flip intervals on rule14-ho.
- Layer-3 integration may still be RED on ho-port/ho-intelligent from the separate Class B plan-id churn — tracked separately, not fixed here.

### Handoff Notes
- Evidence: runs/trace_eval/<TS>_rule14_cohort_after_rule5_fix/, runs/module_oracle_*_after_rule5_fix.json.
- Still open (separate specs): ot-boundary Rule13/15 classification, Class B plan-id churn, Class C cs-edge GNC speed.
```

Replace `<TASK3_SHA>` with the actual commit SHA from Task 3 Step 5.

- [ ] **Step 2: Commit the handoff**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add handoff/workspace_log.md docs/superpowers/specs/2026-06-28-m6-rule5-primary-latch-follow-design.md
git commit -m "docs(handoff): record M6 Rule5 primary-latch follow fix (Class A)"
```

---

## Self-Review

**Spec coverage:** Each spec section maps to a task:
- "Rule 5 bypass in the non-primary risk-gate" → Task 2 (helper) + Task 3 (wiring).
- "Semantics: during encounter active, after release fallback" → Task 1 tests + Task 5 Step 4 regression guard.
- "What is not changed" → Task 3 Step 3/4 regression runs on M6 + M5 suites.
- "Unit tests" → Task 1 + Task 2 Step 3.
- "Module oracle (Layer-2)" → Task 4 Step 4.
- "Integration test (Layer-3 same-rule cohort)" → Task 5.
- "Acceptance Criteria" → Task 4 Step 5 (no sub-2s flips), Task 5 Step 2 (M6 GREEN all three, ho-port regression guard), Task 3 (no rule5_lookout/oracle/gate-constant changes).

**Placeholder scan:** No TBD/TODO. `<TASK3_SHA>` in Task 6 is a fill-in instruction with explicit "replace with actual SHA" — not a plan placeholder, it is a runtime value. All code blocks contain complete code. Commands include exact flags and expected output.

**Type consistency:** Helper signature `rule5_follows_primary_latch(int, bool, bool, bool)` is identical in Task 1 (test calls), Task 2 (definition), Task 3 (call site). `rule13_latch_it`/`rule14_latch_it`/`rule15_latch_it` names match the existing declarations at `colregs_reasoner_node.cpp:681-683`. `RuleLatch::latched()` matches `rule_latch.hpp:83`.
