# M5 NLP 约束重构 Implementation Plan (v2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the v2.1 NLP constraint restructure (min_alt reachable schedule + CPA suffix-hard + terminal full-soften with upper-band cost + ROT 4.7°/s) to resolve rule14-ho NLP 100% Infeasible + CPA penetration.

**Architecture:** Each constraint class gets a physically-motivated hard/soft schedule. ROT source unified to GNC cruise 4.7°/s. RowBoundConfig extended with `*_override_valid` bool. J_terminal extended with two-sided softplus upper-band. tail-gate gains a 6th lateral-band check + CPA release switches cpa_safe→cpa_hard.

**Tech Stack:** C++17 / CasADi 3.7 / IPOPT via nlpsol / ROS2 Humble / colcon / GTest / docker compose (sil-nodes container).

**Spec:** `docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`
**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
**Branch:** `fix/m5-nlp-heartbeat-shadow-upstream`
**Container:** `codex-gnc-validation-sil-nodes-1` (started via `bash scripts/gnc-profile-start.sh`)

**Source-aligned API references (verified against HEAD 9869ac94):**
- `MidMpcInput` fields at `types.hpp`: `colregs_primary_role` (uint8_t, 0=stand-on/free per current accept_tail_gate usage), `colregs_preferred_direction` (enum `ColregsPreferredDirection`: Starboard/Port/ReduceSpeed/Hold), `rot_max_rad_s` (types.hpp:215), `colregs_min_alteration_rad` (types.hpp:207).
- `ColregsPreferredDirection` enum at types.hpp:149-167.
- `RowRegistry` tests use fixture-free `TEST(RowRegistry, ...)` in `test_row_registry.cpp`.
- `MidMpcSolver` tests use `class MidMpcNlpTest : public ::testing::Test` (test_mid_mpc_solver.cpp:35).
- Terminal cost tests use `class TerminalConstraintTest : public ::testing::Test` (test_mid_mpc_terminal.cpp:44) with `make_base_input()` helper.
- `terminal_l_min_feasible_m` / `terminal_l_max_feasible_m` in `MidMpcNlpFormulation::Config` (mid_mpc_nlp_formulation.hpp:130,136), NOT `input.constraints`.
- CPA row order: `cpa_row(t,k) = cpa_start + k*n_targets + t` (row_registry.hpp:137).
- `tail_gate_cpa_release_clear` currently uses `input.constraints.cpa_safe_m` (types.hpp:785) — Task 7 changes to `cpa_hard_m`.
- `give_way_role` in solver: `role == 1U || role == 2U` (mid_mpc_solver.cpp:195).
- Full test list (27 tests) in `CMakeLists.txt:m5_add_gtest(...)` — see Task 9 full list.
- Stand-on scenario file: `scenarios/COLREGs测试/colreg-rule17-cr-so-target-giveway.yaml`.

**Build/test command reference:**
```bash
# Build M5 (sil-nodes container)
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON'

# Run a specific test binary
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash; /opt/ws/build/m5_tactical_planner/<test_name>'

# Run a single gtest filter
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash; /opt/ws/build/m5_tactical_planner/<test_name> --gtest_filter=<pattern>'

# Full test suite via ctest (aggregates failures correctly)
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+'

# probe (host)
PROBE_STUCK_LIMIT=150 rtk python3 scripts/run_colregs_clean_8probe.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/<tag> --summary-out runs/<tag>-summary.json --scenario colreg-rule14-ho
```

---

## File Structure

**Modify:**
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp` — RowBoundConfig 扩展 + apply_* 新函数
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp` — declare `derive_row_bound_config` (so tests can link)
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp` — auto-derive `minalt_hard_from_k` + `cpa_hard_from_k` from input
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp` — `build_terminal_cost_` upper-band term
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp` — `tail_gate_terminal_lateral_feasible` + `accept_tail_gate` 第 6 项 + `tail_gate_cpa_release_clear` cpa_safe→cpa_hard
- `third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml` — ROT 4.7
- `third_party/gnc_ws/src/platform/ship_bringup/config/ship_config_fast10.yaml` — ROT 4.7 (overlay)

**Test (modify/extend existing files; NO new files):**
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp` — RowBoundConfig fields + apply fns bounds tests
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp` — derive_row_bound_config tests
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_terminal.cpp` — upper-band cost + tail-gate lateral
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_midmpc_tail_gate.cpp` — CPA release cpa_hard_m + lateral reject

---

## Task 1: RowBoundConfig 扩展字段（spec §4.2/§4.3/§4.5）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp` (RowBoundConfig struct, around line 47-63)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp`

- [ ] **Step 1: Write the failing test**

Append to `test_row_registry.cpp` (after existing `TEST(RowRegistry, ...)` block, before `main`):

```cpp
// v2.1 spec §4.2/§4.3/§4.5 — new RowBoundConfig fields default values.
// NOTE: terminal_nlp_soft defaults FALSE in Task 1; flipped to TRUE in Task 7
// after upper-band cost (Task 5) + tail-gate lateral (Task 7) land.
TEST(RowRegistry, V21FieldsHaveCorrectDefaults) {
  RowBoundConfig cfg;
  EXPECT_EQ(cfg.minalt_hard_from_k, 0);
  EXPECT_FALSE(cfg.minalt_override_valid);
  EXPECT_EQ(cfg.cpa_hard_from_k, 0);
  EXPECT_FALSE(cfg.cpa_override_valid);
  EXPECT_FALSE(cfg.terminal_nlp_soft);  // Task 1: false; Task 7 flips to true
}
```

- [ ] **Step 2: Run test, verify it FAILS (compile error)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -20'
```
Expected: compile error `'struct RowBoundConfig' has no member named 'minalt_hard_from_k'`.

- [ ] **Step 3: Add fields to RowBoundConfig**

Edit `row_registry.hpp`, in `struct RowBoundConfig` after `bool terminal_disabled{false};` (line ~63):

```cpp
  bool terminal_disabled{false};
  // ── v2.1 spec §4.2/§4.3/§4.5: reachable/suffix-hard schedules + terminal soften ──
  // min_alt reachable schedule deadline (spec §4.2). Rows k < minalt_hard_from_k
  // are softened to [-inf,+inf]; k >= minalt_hard_from_k stay hard [0,+inf].
  // Default 0 = legacy v2 hard-all (regression baseline). Solver auto-derives
  // k*=ceil(min_alt/rot_step)-1 when minalt_override_valid=false (Task 4).
  int32_t minalt_hard_from_k{0};
  bool    minalt_override_valid{false};
  // CPA floor suffix-hard schedule deadline (spec §4.3). k < cpa_hard_from_k
  // softened; k >= cpa_hard_from_k hard. Default 0 = legacy v2 hard-all.
  int32_t cpa_hard_from_k{0};
  bool    cpa_override_valid{false};
  // Terminal NLP rows softened to [-inf,+inf] for give-way lateral (spec §4.5).
  // Default FALSE initially (Task 1) — flipped to TRUE in Task 7 after the
  // upper-band cost (Task 5) + tail-gate lateral (Task 7) land, so we never
  // ship an intermediate state with terminal soften but no upper-band pressure
  // (Concern 1 from Codex plan round-1/round-2).
  bool    terminal_nlp_soft{false};
```

- [ ] **Step 4: Run test, verify PASS**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && build/m5_tactical_planner/test_row_registry --gtest_filter=RowRegistry.V21FieldsHaveCorrectDefaults'
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp
git commit -m "feat(m5): RowBoundConfig v2.1 fields (spec §4.2/§4.3/§4.5)

Add minalt_hard_from_k + cpa_hard_from_k (int schedule) + terminal_nlp_soft.
override_valid bool distinguishes explicit 0 from solver-derived. No behavior
change yet (fields unused until Tasks 2-6 wire them)."
```

---

## Task 2: min_alt reachable schedule（spec §4.2 + B9 precedence）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp` (add apply fn + wire into build_bounds)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp`

- [ ] **Step 1: Write failing tests**

Append to `test_row_registry.cpp`:

```cpp
// v2.1 spec §4.2 — min_alt reachable schedule. k<deadline soft, k>=deadline hard.
TEST(RowRegistry, MinaltReachableScheduleSoftensBeforeDeadline) {
  RowRegistry reg;
  reg.reset(/*N=*/18, /*n_targets=*/1, /*n_rule=*/0, /*n_zone=*/0);
  RowBoundConfig cfg;
  cfg.K = 0;
  cfg.direction_disabled = false;
  cfg.minalt_hard_from_k = 2;  // k<2 soft, k>=2 hard
  cfg.minalt_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    const std::size_t r = static_cast<std::size_t>(reg.min_alt_row(k));
    if (k < 2) {
      EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k;
      EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k;
    } else {
      EXPECT_EQ(b.lbg[r], 0.0) << "k=" << k;
      EXPECT_EQ(b.ubg[r], std::numeric_limits<double>::infinity()) << "k=" << k;
    }
  }
}

// direction_disabled wins over minalt schedule (B9): ALL minalt rows disabled.
TEST(RowRegistry, MinaltScheduleIgnoredWhenDirectionDisabled) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.direction_disabled = true;       // stand-on / HOLD / ReduceSpeed
  cfg.minalt_hard_from_k = 5;          // would soften k<5, but ignored
  cfg.minalt_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    const std::size_t r = static_cast<std::size_t>(reg.min_alt_row(k));
    EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k;
    EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k;
  }
}
```

- [ ] **Step 2: Run, verify FAIL**

```bash
... build/m5_tactical_planner/test_row_registry --gtest_filter='RowRegistry.Minalt*'
```
Expected: FAIL (minalt_hard_from_k ignored; all rows hard since Task 1 only added fields).

- [ ] **Step 3: Add `apply_minalt_reachable_schedule_` + wire into build_bounds**

In `row_registry.hpp` private section (after `apply_direction_disable_`):

```cpp
  // v2.1 spec §4.2: min_alt reachable schedule. Only called when
  // direction_disabled=false (lateral give-way). When direction_disabled=true,
  // apply_direction_disable_ already double-disables ALL min_alt rows (B9).
  void apply_minalt_reachable_schedule_(const RowBoundConfig& cfg,
                                        BoundArray& b) const {
    for (int32_t k = 0; k < N_; ++k) {
      if (k < cfg.minalt_hard_from_k) {
        const std::size_t rm = static_cast<std::size_t>(min_alt_row(k));
        b.lbg[rm] = -kInf;
        b.ubg[rm] =  kInf;
      }
    }
  }
```

In `build_bounds` (the public function around line 180), change:

```cpp
    if (cfg_eff.direction_disabled) { apply_direction_disable_(b); }
    else { apply_minalt_reachable_schedule_(cfg_eff, b); }  // v2.1 §4.2
```

(was: `if (cfg_eff.direction_disabled) { apply_direction_disable_(b); }`)

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_row_registry --gtest_filter='RowRegistry.Minalt*:RowRegistry.directionDisabledNullifiesAllDirectionAndMinAltRows'
```
Expected: all PASS (including existing direction test).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp
git commit -m "feat(m5): min_alt reachable schedule (spec §4.2, B9 precedence)

k<deadline soft [-inf,+inf], k>=deadline hard [0,+inf]. direction_disabled
(stand-on/HOLD/ReduceSpeed) wins — fully disables minalt rows before schedule."
```

---

## Task 3: CPA suffix-hard schedule（spec §4.3 + C2-r2 row order）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp`

- [ ] **Step 1: Write failing test**

Append to `test_row_registry.cpp`:

```cpp
// v2.1 spec §4.3 — CPA suffix-hard. Row order: cpa_row(t,k)=cpa_start+k*n_targets+t
TEST(RowRegistry, CpaSuffixHardSoftensBeforeDeadline) {
  RowRegistry reg;
  reg.reset(/*N=*/18, /*n_targets=*/2, /*n_rule=*/0, /*n_zone=*/0);
  RowBoundConfig cfg;
  cfg.cpa_hard_from_k = 3;  // k<3 soft, k>=3 hard
  cfg.cpa_override_valid = true;
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t k = 0; k < 18; ++k) {
    for (int32_t t = 0; t < 2; ++t) {
      const std::size_t r = static_cast<std::size_t>(reg.cpa_row(t, k));
      if (k < 3) {
        EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
        EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
      } else {
        EXPECT_EQ(b.lbg[r], 0.0) << "k=" << k << " t=" << t;
        EXPECT_EQ(b.ubg[r], std::numeric_limits<double>::infinity()) << "k=" << k << " t=" << t;
      }
    }
  }
}
```

- [ ] **Step 2: Run, verify FAIL**

```bash
... build/m5_tactical_planner/test_row_registry --gtest_filter=RowRegistry.CpaSuffixHardSoftensBeforeDeadline
```
Expected: FAIL (no suffix-hard logic).

- [ ] **Step 3: Add `apply_cpa_suffix_hard_` + wire into build_bounds**

In `row_registry.hpp` private section (after `apply_colreg_prefix_soften_`):

```cpp
  // v2.1 spec §4.3: CPA suffix-hard (receding constraint). k<deadline soft
  // (J_colreg barrier drives opening); k>=deadline hard floor. Composes with
  // apply_colreg_prefix_soften_ (OR: row soft if k<K or k<k_cpa).
  // Row order: cpa_row(t,k) = cpa_start + k*n_targets_ + t (line 137).
  void apply_cpa_suffix_hard_(const RowBoundConfig& cfg, BoundArray& b) const {
    for (int32_t k = 0; k < N_; ++k) {
      if (k < cfg.cpa_hard_from_k) {
        for (int32_t t = 0; t < n_targets_; ++t) {
          const std::size_t r = static_cast<std::size_t>(cpa_row(t, k));
          b.lbg[r] = -kInf;
          b.ubg[r] =  kInf;
        }
      }
    }
  }
```

In `build_bounds`, add after `apply_colreg_prefix_soften_` line:

```cpp
    if (cfg_eff.colreg_prefix_softened) { apply_colreg_prefix_soften_(cfg_eff, b); }
    apply_cpa_suffix_hard_(cfg_eff, b);  // v2.1 §4.3
    if (cfg_eff.direction_disabled) { apply_direction_disable_(b); }
    else { apply_minalt_reachable_schedule_(cfg_eff, b); }
```

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_row_registry --gtest_filter='RowRegistry.Cpa*'
```
Expected: PASS (including existing `cpaRowsSoftenedInPrefixAndHardInSuffix`).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp
git commit -m "feat(m5): CPA suffix-hard schedule (spec §4.3, B5+NLM 🟢)

k<cpa_hard_from_k soft (J_colreg barrier), k>=deadline hard floor. Composes
with prefix-soften (OR). Row order k*n_targets+t (C2-r2)."
```

---

## Task 4: terminal_nlp_soft wiring（spec §4.5；split from Task 5/6 per Concern 1）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp` (build_bounds only)

**Note**: This task only wires `terminal_nlp_soft` flag into build_bounds. The upper-band cost (Task 5) and tail-gate lateral check (Task 6) come after — but per Concern 1, we must NOT enable `terminal_nlp_soft=true` default until Task 5/6 land, otherwise terminal has no upper-band pressure at all.

- [ ] **Step 1: Write failing test**

Append to `test_row_registry.cpp`:

```cpp
// v2.1 spec §4.5 — terminal_nlp_soft=true disables all 3 terminal rows.
TEST(RowRegistry, TerminalNlpSoftDisablesAllTerminalRows) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.terminal_disabled = false;     // give-way lateral (not stand-on)
  cfg.terminal_nlp_soft = true;      // v2.1 default
  BoundArray b = reg.build_bounds(cfg);
  for (int32_t i = 0; i < reg.kTerminalRowCount; ++i) {
    const std::size_t r = static_cast<std::size_t>(reg.terminal_row(i));
    EXPECT_EQ(b.lbg[r], -std::numeric_limits<double>::infinity()) << "terminal row " << i;
    EXPECT_EQ(b.ubg[r],  std::numeric_limits<double>::infinity()) << "terminal row " << i;
  }
}

// terminal_nlp_soft=false + terminal_disabled=false -> legacy hard terminal.
TEST(RowRegistry, TerminalNlpSoftFalseKeepsHardForGiveWay) {
  RowRegistry reg;
  reg.reset(18, 1, 0, 0);
  RowBoundConfig cfg;
  cfg.terminal_disabled = false;
  cfg.terminal_nlp_soft = false;     // legacy v2
  BoundArray b = reg.build_bounds(cfg);
  // terminal_row(0) is g_term_side (hard [0,+inf]); (1)/(2) are lo/hi
  const std::size_t r0 = static_cast<std::size_t>(reg.terminal_row(0));
  EXPECT_EQ(b.lbg[r0], 0.0);  // legacy hard lower bound
}
```

- [ ] **Step 2: Run, verify FAIL**

```bash
... --gtest_filter='RowRegistry.TerminalNlpSoft*'
```
Expected: FAIL (terminal_nlp_soft not wired).

- [ ] **Step 3: Wire into build_bounds**

In `build_bounds`, change the terminal line:

```cpp
    if (cfg_eff.terminal_disabled || cfg_eff.terminal_nlp_soft) {
      apply_terminal_disable_(b);
    }
```

(was: `if (cfg_eff.terminal_disabled) { apply_terminal_disable_(b); }`)

- [ ] **Step 4: Run, verify PASS**

```bash
... --gtest_filter='RowRegistry.TerminalNlpSoft*:RowRegistry.rotTerminalRuleZoneRowsAreLegacyZeroInfBounds'
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(m5): terminal_nlp_soft flag wiring (spec §4.5)

Either terminal_disabled (stand-on/non-lateral) OR terminal_nlp_soft (give-way
default v2.1) triggers full terminal disable. Upper-band cost + tail-gate
lateral come in Tasks 5/6 to backfill the lost upper-bound pressure."
```

---

## Task 5: J_terminal two-sided softplus upper-band（spec §4.5 B7-r2）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:267-272` (build_terminal_cost_ return)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_terminal.cpp`

- [ ] **Step 1: Write failing test (using existing TerminalConstraintTest fixture)**

Append to `test_mid_mpc_terminal.cpp`. The test evaluates `J_terminal` via the formulation's cost Function (same pattern as existing tests around line 248 — `eval_terminal_cost(x, p)` or equivalent using `formulation_->cost_function()`). `l_max` lives on `MidMpcNlpFormulation::Config` (formulation.hpp:136), accessed via `formulation_->config()` — NOT on `inp.constraints`.

```cpp
// v2.1 spec §4.5 B7-r2 — upper-band two-sided softplus activates at |l| > l_max.
// Strategy: build a TIGHT-band formulation (l_max=50m) so small terminal
// lateral offsets trigger upper-band. Evaluate J_terminal at three psi/u
// sequences dead-reckoned to known terminal y offsets. Uses the SAME
// formulation_->cost_function() call pattern as existing tests in this file.
TEST_F(TerminalConstraintTest, UpperBandCostActivatesBeyondLMax) {
  // Tight-band formulation: l_max=50 forces upper-band activation at y=200m.
  MidMpcNlpFormulation::Config cfg_tight = formulation_->config();
  cfg_tight.terminal_l_max_feasible_m = 50.0;
  MidMpcNlpFormulation form_tight(cfg_tight);
  form_tight.build_symbolic_graph();

  // Helper: evaluate J_terminal for a psi/u sequence that dead-reckons to
  // terminal y ≈ target_y. Route frame: bearing=0 (north), normal=+east,
  // so lateral offset = y_m. With u=5 m/s, dt=5s, N=8: each step moves
  // 25·sin(psi) meters in y. To reach y=200 over 8 steps: sin(psi)=1.0 ->
  // psi=π/2 (due east). y=0: psi=0 (north). y=-200: psi=-π/2 (west).
  const int32_t N = form_tight.config().n_horizon;  // 8 (TerminalConstraintTest fixture)
  auto eval_j_terminal_at_psi = [&](double psi_rad) -> double {
    MidMpcInput inp = make_base_input();
    inp.colregs_primary_role = 1U;  // give-way
    inp.colregs_preferred_direction = mass_l3::m5::mass_l3::m5::ColregsPreferredDirection::Starboard;
    inp.rot_max_rad_s = units::degToRad(4.7);
    inp.constraints.applicable_rules = {15};  // give-way gate (matches existing test)
    casadi::DM p = form_tight.pack_parameters(inp);
    casadi::DM x = casadi::DM::zeros(2 * N, 1);
    for (int32_t k = 0; k < N; ++k) {
      x(k) = psi_rad;
      x(N + k) = 5.0;
    }
    return form_tight.eval_terminal_cost(x, p);  // existing API (formulation.hpp:186)
  };

  const double cost_pos = eval_j_terminal_at_psi(units::degToRad(90));   // y≈+200
  const double cost_neg = eval_j_terminal_at_psi(units::degToRad(-90));  // y≈-200
  const double cost_mid = eval_j_terminal_at_psi(units::degToRad(0));    // y≈0

  // Upper-band activates symmetrically: both |y|>l_max costs exceed baseline.
  EXPECT_GT(cost_pos, cost_mid + 0.5) << "upper-band failed to activate at +l_max";
  EXPECT_GT(cost_neg, cost_mid + 0.5) << "upper-band failed to activate at -l_max";
  // Smoothness: finite difference at y=0 (no NaN from abs kink).
  const double cost_plus1  = eval_j_terminal_at_psi(units::degToRad(0.5));
  const double cost_minus1 = eval_j_terminal_at_psi(units::degToRad(-0.5));
  EXPECT_TRUE(std::isfinite(cost_plus1 - cost_minus1)) << "non-smooth at l=0";
}
```

**Implementer note**: If `form_tight.J_function()` is not the exact accessor (check the existing test at `test_mid_mpc_terminal.cpp:248` for the real helper name — it may be `J_` member with `casadi::Function` semantics, or an `eval_terminal_cost` helper), substitute the exact API. The contract is the three assertions above; the API call shape mirrors the neighboring test exactly.

- [ ] **Step 2: Run, verify FAIL**

```bash
... build/m5_tactical_planner/test_mid_mpc_terminal --gtest_filter=TerminalConstraintTest.UpperBandCostActivatesBeyondLMax
```
Expected: FAIL (current J_terminal has no upper-band; Case A cost ≈ Case C cost).

- [ ] **Step 3: Extend `build_terminal_cost_`**

Edit `mid_mpc_nlp_formulation.cpp`, replace the `return` statement at the end of `build_terminal_cost_` (around line 270-272):

```cpp
casadi::MX MidMpcNlpFormulation::build_terminal_cost_() const {
  const casadi::MX lN        = compute_terminal_cross_track_();
  const casadi::MX l_scale   = slot(p_, kIdxLateralScale);
  const casadi::MX pref_dir  = slot(p_, kIdxPreferredDir);
  const casadi::MX give_way  = slot(p_, kIdxRole);
  const casadi::MX tau_t     = casadi::DM(cfg_.terminal_tau);
  // v2.1 §4.5 B7-r2: upper-band two-sided softplus (no abs kink).
  const casadi::MX l_max     = casadi::DM(cfg_.terminal_l_max_feasible_m);

  // Existing: lower-band wrong-side softplus (keep verbatim).
  const casadi::MX wrong_side = -pref_dir * (lN / l_scale);
  const casadi::MX J_lower = tau_t * casadi::MX::log(1.0 + casadi::MX::exp(wrong_side / tau_t));

  // New: upper-band two-sided. Two smooth terms, each C∞ at z=0.
  // softplus(z) = tau*log(1+exp(z/tau)); z>0 activates.
  const casadi::MX z_pos = (lN - l_max) / l_scale;   // >0 when lN > +l_max
  const casadi::MX z_neg = (-lN - l_max) / l_scale;  // >0 when lN < -l_max
  const casadi::MX J_upper = tau_t * (
      casadi::MX::log(1.0 + casadi::MX::exp(z_pos / tau_t)) +
      casadi::MX::log(1.0 + casadi::MX::exp(z_neg / tau_t)));

  // give_way gate (kIdxRole) applies to both. BUT ReduceSpeed packs
  // kIdxPreferredDir=0 (not kIdxRole=0 — kIdxRole stays 1.0 for give-way
  // ReduceSpeed per pack_parameters:695). To avoid levying a lateral
  // upper-band cost on a non-lateral ReduceSpeed maneuver (B3-r2/C4-r2),
  // gate the UPPER term by lateral_active = give_way · (pref_dir · pref_dir).
  // (pref_dir·pref_dir is 0 for Hold/ReduceSpeed, 1 for Starboard(±1)/Port.)
  // The LOWER term (wrong-side softplus) keeps the give_way-only gate. When
  // pref_dir=0 (ReduceSpeed/Hold), wrong_side=0 -> softplus(0)=tau·log2, a
  // constant offset with zero gradient w.r.t. l — so it does not steer the
  // solution, just adds a constant. Acceptable per existing §5.4 reasoning.
  const casadi::MX lateral_active = give_way * pref_dir * pref_dir;
  return give_way * J_lower + lateral_active * J_upper;
}
```

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_mid_mpc_terminal
```
Expected: all terminal tests PASS (existing + new).

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_terminal.cpp
git commit -m "feat(m5): J_terminal two-sided softplus upper-band (spec §4.5, B7-r2)

No abs kink (smooth C∞). Reuses terminal_tau + l_scale + l_max_feasible_m
(no new Config fields). give_way gate consistent with role matrix C4-r2."
```

---

## Task 6: tail-gate CPA release cpa_safe→cpa_hard（spec §4.3 B6-r2 + B8）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp:785`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_midmpc_tail_gate.cpp`

- [ ] **Step 1: Write failing test**

In `test_midmpc_tail_gate.cpp`, find existing CPA release tests and add:

```cpp
// v2.1 spec §4.3 B6-r2 — tail-gate CPA release uses cpa_hard_m (unbumped),
// NOT cpa_safe_m (bumped to 2500 during conflict). Craft a target that is
// OPENING (closing_speed <= 0) with terminal CPA = 2000m, cpa_safe=2500,
// cpa_hard=1852. Old code (cpa_safe): 2000 >= 2500 -> reject (wrong, safe).
// New code (cpa_hard): 2000 >= 1852 -> accept (correct).
TEST(TailGateCpaRelease, UsesCpaHardNotBumpedCpaSafe) {
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  // One trajectory point at terminal; CPA computed via trajectory_terminal_state_cpa_m
  // which uses target.x_m/y_m + sol.trajectory.back(). Place own terminal at
  // (0, 0), target at (2000, 0) -> terminal CPA = 2000m.
  TrajectoryPoint term{};
  term.x_m = 0.0; term.y_m = 0.0; term.t_s = 90.0;
  sol.trajectory.push_back(term);

  MidMpcInput inp;
  TargetState tgt{};
  tgt.x_m = 2000.0; tgt.y_m = 0.0;
  tgt.cpa_sigma_m = 0.0;
  inp.targets.push_back(tgt);
  TargetRiskSnapshot risk{};
  risk.closing_speed_mps = -1.0;  // OPENING (target moving away)
  inp.target_risks.push_back(risk);
  inp.constraints.cpa_safe_m = 2500.0;  // bumped during conflict
  inp.constraints.cpa_hard_m = 1852.0;  // unbumped floor

  EXPECT_TRUE(tail_gate_cpa_release_clear(sol, inp));
  // Sanity: with cpa_hard=2500 (matching old bumped value), this would be FALSE.
  inp.constraints.cpa_hard_m = 2500.0;
  EXPECT_FALSE(tail_gate_cpa_release_clear(sol, inp));
}
```

- [ ] **Step 2: Run, verify FAIL**

```bash
... build/m5_tactical_planner/test_midmpc_tail_gate --gtest_filter=TailGateCpaRelease.UsesCpaHardNotBumpedCpaSafe
```
Expected: FAIL (currently uses `cpa_safe_m`).

- [ ] **Step 3: Change types.hpp:785**

```cpp
  // v2.1 §4.3 B6-r2: release check uses cpa_hard_m (unbumped), not cpa_safe_m
  // (bumped to 2500 during conflict — that's the J_colreg soft barrier radius,
  // not a hard floor). Using bumped value made the release floor unreachable.
  return (terminal_cpa_m - (3.0 * sigma_m)) >= input.constraints.cpa_hard_m;
```

(was: `>= input.constraints.cpa_safe_m;`)

**Precondition**: confirm `ConstraintInputs` has `cpa_hard_m` field. Run:
```bash
rg -n "cpa_hard_m" src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp
```
If missing, add `double cpa_hard_m{1852.0};` to `struct ConstraintInputs` and ensure node packs it (mid_mpc_node.cpp already uses `cpa_hard_m` for the compiler floor at constraint_compiler.cpp:323 — find the node packing site and mirror).

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_midmpc_tail_gate
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(m5): tail-gate CPA release uses cpa_hard_m not bumped cpa_safe_m

spec v2.1 §4.3 B6-r2. cpa_safe_m is bumped to 2500 during conflict for
J_colreg soft barrier; using it as release floor made release unreachable."
```

---

## Task 7: tail-gate terminal lateral feasibility check（spec §4.5 B7-gap2 + C4-r2）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp` (add helper + wire into accept_tail_gate)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_midmpc_tail_gate.cpp`

- [ ] **Step 1: Write failing tests**

In `test_midmpc_tail_gate.cpp`:

```cpp
// v2.1 spec §4.5 — terminal lateral band check, role-guarded. Construct a
// non-empty trajectory with an actual terminal point so the check evaluates
// real geometry (empty trajectory early-returns true, not a useful test).
TEST(TailGateLateral, RejectsOutOfBandGiveWayLateral) {
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  // route_brg=0 (north), normal=+east -> lateral = y_m. Set y=+500m (over l_max=400).
  term.x_m = 0.0; term.y_m = 500.0; term.t_s = 90.0;
  sol.trajectory.push_back(term);
  // pref_dir=Starboard (+1), lateral_active=true, l_min=30, l_max=400.
  // 500 > 400 -> out of band -> reject.
  EXPECT_FALSE(tail_gate_terminal_lateral_feasible(
      sol, /*route_brg_rad=*/0.0, mass_l3::m5::ColregsPreferredDirection::Starboard,
      /*lateral_active=*/true, /*l_min=*/30.0, /*l_max=*/400.0));
}

TEST(TailGateLateral, AcceptsInBandGiveWayLateral) {
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  term.x_m = 0.0; term.y_m = 100.0; term.t_s = 90.0;  // 100m stbd, in [30,400]
  sol.trajectory.push_back(term);
  EXPECT_TRUE(tail_gate_terminal_lateral_feasible(
      sol, 0.0, mass_l3::m5::ColregsPreferredDirection::Starboard, true, 30.0, 400.0));
}

TEST(TailGateLateral, SkipsForNonLateralReduceSpeed) {
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  term.x_m = 0.0; term.y_m = 1000.0; term.t_s = 90.0;  // way out of band
  sol.trajectory.push_back(term);
  // lateral_active=false (ReduceSpeed) -> skip, always accept.
  EXPECT_TRUE(tail_gate_terminal_lateral_feasible(
      sol, 0.0, mass_l3::m5::ColregsPreferredDirection::ReduceSpeed, false, 30.0, 400.0));
}

// v2.1 §4.5: integration test via accept_tail_gate() (the actual wiring point).
// Spec requires the helper to be the 6th check in accept_tail_gate, not just
// a standalone helper. This test confirms the integration.
TEST(TailGateLateral, AcceptTailGateRejectsOutOfBandGiveWay) {
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  // route_brg=0 (north), terminal y=+500m > l_max=400 -> out of band.
  term.x_m = 0.0; term.y_m = 500.0; term.t_s = 90.0;
  // Need valid ROT/decel for the other tail-gate checks to pass (so we isolate
  // the lateral reject). Use uniform psi so turns_are_feasible passes.
  for (int k = 0; k < 8; ++k) {
    TrajectoryPoint p{};
    p.x_m = 0.0; p.y_m = static_cast<double>(k) * 62.5;  // 0,62.5,...,437.5,500
    p.psi_rad = units::degToRad(90);  // due east, constant -> 0 inter-step turn
    p.u_mps = 5.0; p.t_s = static_cast<double>(k) * 5.0;
    sol.trajectory.push_back(p);
  }

  MidMpcInput inp;
  inp.colregs_primary_role = 1U;  // give-way
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.planned_route_bearing_rad = 0.0;
  inp.own_ship.psi_rad = units::degToRad(90);
  inp.own_ship.u_mps = 5.0;
  inp.rot_max_rad_s = units::degToRad(4.7);
  inp.decel_max_mps2 = 0.5;
  inp.constraints.terminal_l_min_feasible_m = 30.0;
  inp.constraints.terminal_l_max_feasible_m = 400.0;
  // Ensure other tail-gate checks pass: no target (CPA skip), no crossing.

  TailGateAcceptance result = accept_tail_gate(sol, inp);
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "terminal_lateral_out_of_band");
}
```

- [ ] **Step 2: Run, verify FAIL**

Expected: compile error (no `tail_gate_terminal_lateral_feasible`).

- [ ] **Step 3: Add helper + wire into accept_tail_gate**

In `types.hpp`, before `accept_tail_gate`, add:

```cpp
// v2.1 spec §4.5: terminal lateral band feasibility (6th tail-gate check).
// Uses existing trajectory_terminal_lateral_offset_m(point, route_brg) (line 687).
// Role guard via lateral_colreg_active: stand-on / ReduceSpeed / HOLD skip.
// pref_dir is the enum (Starboard/Port are lateral; ReduceSpeed/Hold are not).
inline bool tail_gate_terminal_lateral_feasible(
    const MidMpcSolution& solution,
    double route_brg_rad,
    ColregsPreferredDirection pref_dir,
    bool   lateral_colreg_active,
    double l_min_feasible_m,
    double l_max_feasible_m) {
  if (!lateral_colreg_active) return true;
  if (solution.trajectory.empty()) return true;
  const double lN = trajectory_terminal_lateral_offset_m(
      solution.trajectory.back(), route_brg_rad);
  const double signed_pref = (pref_dir == mass_l3::m5::ColregsPreferredDirection::Starboard) ? +1.0
                           : (pref_dir == mass_l3::m5::ColregsPreferredDirection::Port)       ? -1.0
                           : 0.0;
  if (signed_pref * lN < l_min_feasible_m) return false;  // wrong side / insufficient
  if (lN < -l_max_feasible_m || lN > l_max_feasible_m) return false;
  return true;
}
```

In `accept_tail_gate` (types.hpp, just before the final `result.accepted = true;`), add:

```cpp
  // v2.1 §4.5: 6th check — terminal lateral band.
  // lateral_active mirrors solver give-way-lateral condition (mid_mpc_solver.cpp:195):
  //   role == 1U || role == 2U  AND  pref_dir ∈ {Starboard, Port}
  const bool lateral_colreg_active =
      (input.colregs_primary_role == 1U || input.colregs_primary_role == 2U) &&
      (input.colregs_preferred_direction == mass_l3::m5::ColregsPreferredDirection::Starboard ||
       input.colregs_preferred_direction == mass_l3::m5::ColregsPreferredDirection::Port);
  // l_min/l_max are MidMpcNlpFormulation::Config fields, not MidMpcInput.
  // accept_tail_gate receives MidMpcInput only. Resolution: add two fields to
  // ConstraintInputs OR pass them through a new accept_tail_gate overload.
  // Minimal change: add `terminal_l_min_feasible_m` / `terminal_l_max_feasible_m`
  // to ConstraintInputs (types.hpp struct, ~line 107), packed by mid_mpc_node
  // from the formulation Config. Then:
  if (!tail_gate_terminal_lateral_feasible(
          solution,
          input.planned_route_bearing_rad,
          input.colregs_preferred_direction,
          lateral_colreg_active,
          input.constraints.terminal_l_min_feasible_m,
          input.constraints.terminal_l_max_feasible_m)) {
    result.reason = "terminal_lateral_out_of_band";
    return result;
  }
```

**Sub-step 3a**: `ConstraintInputs` already has `cpa_hard_m` (types.hpp:112 — do NOT re-add). Add `terminal_l_min_feasible_m{30.0}` and `terminal_l_max_feasible_m{400.0}` to `struct ConstraintInputs` (types.hpp ~line 107, after `cpa_hard_m`). Default to Config defaults.

**Sub-step 3b**: In `mid_mpc_node.cpp` `assemble_input_()` (where `ConstraintInputs` is packed, near the existing `cpa_hard_m` pack site ~line 635), add (use `formulation_.config()`, NOT `formulation_cfg_` which doesn't exist):
```cpp
  inp.constraints.terminal_l_min_feasible_m = formulation_.config().terminal_l_min_feasible_m;
  inp.constraints.terminal_l_max_feasible_m = formulation_.config().terminal_l_max_feasible_m;
```

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_midmpc_tail_gate --gtest_filter='TailGateLateral.*:'
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(m5): tail-gate terminal lateral feasibility (spec §4.5, B7-gap2)

6th tail-gate check. Role guard via lateral_colreg_active (give-way + Starboard/
Port). Uses trajectory_terminal_lateral_offset_m (existing). Adds l_min/l_max
to ConstraintInputs (packed from Config in node). C4-r2 matrix enforced."
```

- [ ] **Step 6: Flip `terminal_nlp_soft` default to TRUE (now that upper-band + tail-gate land)**

Edit `row_registry.hpp` RowBoundConfig (Task 1 field):

```cpp
  bool    terminal_nlp_soft{true};   // v2.1 §4.5 default (flipped in Task 7 after upper-band cost + tail-gate lateral land)
```

Update Task 1 test `V21FieldsHaveCorrectDefaults` to expect `true`:

```cpp
  EXPECT_TRUE(cfg.terminal_nlp_soft);  // Task 7 flips default to true
```

Re-run Task 1 + Task 4 tests to confirm the flip is coherent:

```bash
... build/m5_tactical_planner/test_row_registry --gtest_filter='RowRegistry.V21*:RowRegistry.TerminalNlpSoft*:RowRegistry.rotTerminalRuleZoneRowsAreLegacyZeroInfBounds'
```

Commit:
```bash
git commit -m "feat(m5): flip terminal_nlp_soft default to true (spec §4.5)

Backstop is now in place: J_terminal upper-band cost (Task 5) + tail-gate
lateral check (Task 7). Default flips from false (Task 1 interim) to true."
```

---

## Task 8: Solver auto-derive k_minalt + k_cpa（spec §4.2/§4.3 B8-r2）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp` (declare helper)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp` (implement + wire into solve)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp` (MidMpcNlpTest fixture)

- [ ] **Step 1: Write failing tests**

In `test_mid_mpc_solver.cpp` (use fully-qualified names — the file's existing `using` declarations at line 24 only cover `MidMpcInput`/`TargetState`/`MidMpcNlpFormulation`/`MidMpcSolver`, NOT `RowBoundConfig` or `ColregsPreferredDirection`):

```cpp
// v2.1 spec §4.2/§4.3 — auto-derive k_minalt + k_cpa from input.
using mass_l3::m5::mid_mpc::derive_row_bound_config;
using mass_l3::m5::mid_mpc::RowBoundConfig;
using mass_l3::m5::TargetState;  // already imported at line 24; keep if not

TEST_F(MidMpcNlpTest, DeriveMinaltKStarForRot4p7) {
  MidMpcInput inp = make_base_input();  // existing helper in this file
  inp.rot_max_rad_s = units::degToRad(4.7);
  inp.colregs_min_alteration_rad = units::degToRad(30.0);
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  RowBoundConfig cfg = derive_row_bound_config(inp, /*n_horizon=*/18, /*dt_s=*/5.0);
  ASSERT_FALSE(cfg.minalt_override_valid);
  // k* = ceil(30/23.5) - 1 = ceil(1.276) - 1 = 2 - 1 = 1
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);
}

TEST_F(MidMpcNlpTest, DeriveCpaKCPAUsesTcpaMargin) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = units::degToRad(4.7);
  inp.colregs_min_alteration_rad = units::degToRad(30.0);
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  TargetState tgt{};
  tgt.tcpa_s = 15.0;
  inp.targets.push_back(tgt);
  RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  ASSERT_FALSE(cfg.cpa_override_valid);
  // k_minalt=1; k_tcpa = ceil(15/5) - 1 = 2; k_cpa=max(1,2)=2.
  EXPECT_EQ(cfg.cpa_hard_from_k, 2);
}

TEST_F(MidMpcNlpTest, DeriveDisabledForReduceSpeed) {
  MidMpcInput inp = make_base_input();
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::ReduceSpeed;
  RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_TRUE(cfg.direction_disabled);  // B9: ReduceSpeed disables direction
}

TEST_F(MidMpcNlpTest, DeriveCpaConservativeWhenAllTcpaNonPositive) {
  // B3-r3: all targets tcpa<=0 -> conservative cpa_hard_from_k=0 (v2 legacy)
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = units::degToRad(4.7);
  inp.colregs_min_alteration_rad = units::degToRad(30.0);
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  TargetState tgt{};
  tgt.tcpa_s = 0.0;  // already past
  inp.targets.push_back(tgt);
  RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.cpa_hard_from_k, 0);  // conservative fallback
}
```

- [ ] **Step 2: Run, verify FAIL (link error: derive_row_bound_config undefined)**

```bash
... build/m5_tactical_planner/test_mid_mpc_solver --gtest_filter='MidMpcNlpTest.Derive*'
```

- [ ] **Step 3: Declare + implement `derive_row_bound_config`**

In `mid_mpc_solver.hpp` (public or in the same namespace as MidMpcSolver):

```cpp
// v2.1 spec §4.2/§4.3: derive k_minalt + k_cpa from input when caller doesn't
// override. Returns RowBoundConfig with override_valid=false for derived fields.
RowBoundConfig derive_row_bound_config(
    const MidMpcInput& input,
    int32_t n_horizon,
    double dt_s);
```

In `mid_mpc_solver.cpp`, add at file scope BEFORE the existing closing `}  // namespace mass_l3::m5::mid_mpc` (the file is already inside this namespace — do NOT add a nested `namespace` block). **IMPORTANT (B6-r2)**: the existing `solve()` signature receives a caller-supplied `row_bounds` parameter (mid_mpc_solver.cpp:168 `RowBoundConfig rb_eff = row_bounds;`). The caller's K/overrides take precedence. The new `derive_row_bound_config` produces the *derived defaults*; `solve()` must MERGE caller overrides onto derived, NOT replace.

```cpp
// (No namespace wrapper — paste inside the existing mass_l3::m5::mid_mpc block.)

// v2.1 spec §4.2/§4.3: derive k_minalt + k_cpa from input. Returns a cfg with
// override_valid=false for the v2.1 fields; solve() merges caller row_bounds
// on top (caller fields with override_valid=true win; K/colreg_prefix_softened
// keep their existing caller-precedence from the original solve() block).
RowBoundConfig derive_row_bound_config(
    const MidMpcInput& input,
    int32_t n_horizon,
    double dt_s) {
  RowBoundConfig cfg;
  // Replicate the EXISTING direction/terminal derivation (mid_mpc_solver.cpp:189-205):
  const bool pref_active =
      (input.colregs_preferred_direction == mass_l3::m5::ColregsPreferredDirection::Starboard ||
       input.colregs_preferred_direction == mass_l3::m5::ColregsPreferredDirection::Port);
  const bool give_way_role =
      (input.colregs_primary_role == 1U || input.colregs_primary_role == 2U);
  const bool lateral_behavior =
      (input.colregs_preferred_direction != mass_l3::m5::ColregsPreferredDirection::Hold &&
       input.colregs_preferred_direction != mass_l3::m5::ColregsPreferredDirection::ReduceSpeed);
  const bool lateral_colreg_active = give_way_role && pref_active && lateral_behavior;
  cfg.direction_disabled = !lateral_colreg_active;
  cfg.terminal_disabled = !lateral_colreg_active;
  // K / colreg_prefix_softened are NOT derived here — solve() keeps its existing
  // K_eff derivation from prefix_active_k + caller row_bounds.K precedence.
  cfg.K = 0;
  cfg.colreg_prefix_softened = false;

  // v2.1 §4.2 k_minalt = ceil(min_alt/rot_step) - 1, clamped [0, N]
  if (!cfg.direction_disabled) {
    const double rot_step = input.rot_max_rad_s * dt_s;
    if (rot_step > 1e-9) {
      int32_t k_minalt = static_cast<int32_t>(
          std::ceil(input.colregs_min_alteration_rad / rot_step)) - 1;
      cfg.minalt_hard_from_k = std::max(0, std::min(k_minalt, n_horizon));
    }
  }

  // v2.1 §4.3 k_cpa = max(k_minalt, k_tcpa_margin), where
  //   k_tcpa_margin = ceil(min(tcpa_primary, t_cap)/dt) - 1
  // Spec §4.3 B8-r2: if all targets have tcpa_s <= 0 (already past) or no
  // targets, derive fails -> conservative default cpa_hard_from_k=0 (all hard,
  // v2 legacy). This avoids the bug where tcpa_min stays at infinity and
  // k_tcpa collapses to N (degenerating CPA to nearly-all-soft).
  if (!cfg.direction_disabled && !input.targets.empty()) {
    double tcpa_min = std::numeric_limits<double>::infinity();
    for (const auto& t : input.targets) {
      if (t.tcpa_s > 0.0) tcpa_min = std::min(tcpa_min, t.tcpa_s);
    }
    if (std::isfinite(tcpa_min)) {
      const double t_cap = static_cast<double>(n_horizon) * dt_s;
      const double tcpa_eff = std::min(tcpa_min, t_cap);
      int32_t k_tcpa = static_cast<int32_t>(std::ceil(tcpa_eff / dt_s)) - 1;
      k_tcpa = std::max(0, std::min(k_tcpa, n_horizon));
      cfg.cpa_hard_from_k = std::max(cfg.minalt_hard_from_k, k_tcpa);
    } else {
      // All tcpa_s <= 0 (targets past) -> conservative all-hard per spec §4.3.
      cfg.cpa_hard_from_k = 0;
    }
  } else if (!cfg.direction_disabled) {
    cfg.cpa_hard_from_k = cfg.minalt_hard_from_k;
  }
  return cfg;
}
// (No closing namespace brace — already inside mass_l3::m5::mid_mpc.)
```

In `solve()`, replace the inline `direction_disabled`/`terminal_disabled` derivation block (lines ~189-205) with:

```cpp
  // v2.1: derive defaults from input, then merge caller row_bounds on top.
  // Caller K/colreg_prefix_softened/terminal_disabled/direction_disabled keep
  // their existing precedence (caller set them explicitly -> use caller value;
  // caller left at default -> use derived). For the new v2.1 schedule fields,
  // caller wins only when *_override_valid=true.
  RowBoundConfig derived = derive_row_bound_config(
      input, formulation_.config().n_horizon, formulation_.config().dt_s);

  // Existing K/prefix derivation (lines 168-187) stays — caller row_bounds.K
  // precedence preserved, K_eff from prefix_active_k propagated.
  RowBoundConfig rb_eff = row_bounds;  // caller (existing line 168)
  if (rb_eff.K == 0 && K_eff > 0) {
    rb_eff.K = K_eff;
  }
  if (K_eff > 0) {
    rb_eff.colreg_prefix_softened = true;
  }
  // Merge direction/terminal: only explicit caller TRUE wins (bool fields have
  // no sentinel, so caller false is indistinguishable from default false — we
  // let derived overwrite in that case, matching the existing auto-disable
  // contract from the original solve() block). If a future task needs
  // explicit-false-wins, add `direction_override_valid` / `terminal_override_valid`.
  if (!row_bounds.direction_disabled) { rb_eff.direction_disabled = derived.direction_disabled; }
  if (!row_bounds.terminal_disabled)  { rb_eff.terminal_disabled  = derived.terminal_disabled; }
  // v2.1 schedule merge: caller override_valid wins.
  rb_eff.minalt_hard_from_k = row_bounds.minalt_override_valid
      ? row_bounds.minalt_hard_from_k : derived.minalt_hard_from_k;
  rb_eff.cpa_hard_from_k = row_bounds.cpa_override_valid
      ? row_bounds.cpa_hard_from_k : derived.cpa_hard_from_k;
  // terminal_nlp_soft: caller explicit always wins (it's a bool with no
  // "override_valid" sentinel; default false in Task 1, flipped to true in
  // Task 7 after upper-band cost + tail-gate lateral land).
  rb_eff.terminal_nlp_soft = row_bounds.terminal_nlp_soft;
```

**Note**: the existing `solve()` block after line 187 (the `pref_active`/`give_way_role`/`lateral_behavior` derivation) is REMOVED — it's now inside `derive_row_bound_config`. The merge logic above preserves caller precedence exactly as before for K/colreg_prefix_softened/terminal_disabled/direction_disabled, and adds the v2.1 schedule merge.

- [ ] **Step 4: Run, verify PASS**

```bash
... build/m5_tactical_planner/test_mid_mpc_solver --gtest_filter='MidMpcNlpTest.Derive*:MidMpcNlpTest.StraightLineNoTargets'
```
Expected: PASS (existing test still passes with refactor).

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(m5): auto-derive k_minalt + k_cpa (spec §4.2/§4.3, B8-r2)

derive_row_bound_config() in solver. k_minalt=ceil(min_alt/rot_step)-1;
k_cpa=max(k_minalt, ceil(min(tcpa,t_cap)/dt)-1). direction_disabled wins
for stand-on/HOLD/ReduceSpeed (B9)."
```

---

## Task 9: ROT 参数 YAML → 4.7°/s（spec §5.2）

**Files:**
- Modify: `third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml:638-639`
- Modify: `third_party/gnc_ws/src/platform/ship_bringup/config/ship_config_fast10.yaml:560-561`

- [ ] **Step 1: Verify current values**

```bash
sed -n '637,640p' third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml
sed -n '559,562p' third_party/gnc_ws/src/platform/ship_bringup/config/ship_config_fast10.yaml
```
Expected: `max_yaw_rate_deg_s: 1.2` / `emergency_max_yaw_rate_deg_s: 2.0` in both.

- [ ] **Step 2: Edit ship_config.yaml lines 638-639**

```yaml
    max_yaw_rate_deg_s: 4.7              # v2.1: cruise ROT baseline, IMO MSC.137(76) derived 🟡 [TBD-HAZID-2026-08-19]
    emergency_max_yaw_rate_deg_s: 4.7    # v2.1 B3: GNC clamps max(cruise,emergency); keep equal
```

- [ ] **Step 3: Edit ship_config_fast10.yaml lines 560-561** (same change)

- [ ] **Step 4: GNC rebuild (cmake-clean + touch cpp to defeat ccache)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select ship_guidance --cmake-clean-cache'
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'touch /opt/ws/src/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp && source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select ship_guidance'
```

- [ ] **Step 5: Commit**

```bash
git add third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml \
        third_party/gnc_ws/src/platform/ship_bringup/config/ship_config_fast10.yaml
git commit -m "chore(gnc): ROT 1.2/2.0 -> 4.7 deg/s unified (spec v2.1 §5.2)

IMO MSC.137(76) derived 🟡 [TBD-HAZID-2026-08-19]. cruise + emergency unified
per GNC max(cruise,emergency) clamp (B3-r2). fast10 overlay synced."
```

**Note (concern 4)**: `config/vessels/fcb_45m.yaml` `rot_max_curve` is NOT modified in this task. Per spec §5.4 B3/B4-r2: NLP `rot_max` is sourced from GNC `cruise_max_yaw_rate_deg_s` (mid_mpc_node.cpp:728), not `fcb_45m.yaml rot_max_curve`. The latter is a HAZID/MMG D1.3a physical-model reference only; it does not participate in `k*` computation. Leave for HAZID RUN-001 + D1.3a hydrodynamic validation. Document this explicitly in handoff.

---

## Task 9.5: direction diag probe（spec §4.4，implementation-only，merge 前移除）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp` (g(x*) row residual dump, env-gated)

**Note**: This is an implementation-only diagnostic per spec §4.4 / C3. It MUST be removed (or env-gated to default-off) before final merge to main. Purpose: collect direction row residuals from rule14-ho probe to decide whether direction needs softening in a follow-up.

- [ ] **Step 1: Add env-gated diag dump in solver (default OFF)**

In `mid_mpc_solver.cpp`, after `res = formulation_.solver()(arg);` (line ~254) and before `unpack_solution`, add. **Pre-step**: ensure `<cstdlib>` and `<cstdio>` are included at the top of `mid_mpc_solver.cpp` (current includes do not have them — `std::getenv`/`std::fprintf` need them; do not rely on transitive includes). CasADi nlpsol returns constraint values at x* via `res.at("g")` (verified: existing code uses `res.at("x")` at line 277, `"g"` is the standard CasADi constraint output key):

```cpp
  // v2.1 spec §4.4 / C3: implementation-only diag dump for direction rows.
  // Activated ONLY when env var M5_DIRECTION_DIAG=1. Default off. Remove after
  // direction-soften decision. Uses res.at("g") (CasADi nlpsol standard output).
  if (const char* env = std::getenv("M5_DIRECTION_DIAG")) {
    if (env[0] == '1') {
      const casadi::DM& g_at_sol = res.at("g");
      const auto& reg = formulation_.row_registry();  // existing accessor
      const int32_t N = formulation_.config().n_horizon;
      for (int32_t k = 0; k < N; ++k) {
        const std::size_t r = static_cast<std::size_t>(reg.direction_row(k));
        const double g_val = static_cast<double>(g_at_sol(r, 0));
        const double lb = 0.0;  // direction hard lower bound
        const double margin = g_val - lb;
        std::fprintf(stderr,
            "[M5_DIRECTION_DIAG] k=%d g=%g lb=%g margin=%g %s\n",
            k, g_val, lb, margin, (margin < 1e-6 ? "ACTIVE" : "satisfied"));
      }
    }
  }
```

**API verified**: `formulation_.row_registry()` returns the `RowRegistry` (used at mid_mpc_solver.cpp:214 `formulation_.row_registry().build_bounds(rb_eff)`). `N` matches the solver's existing local variable convention (mid_mpc_solver.cpp:115 `const int32_t N = formulation_.config().n_horizon`). `res.at("g")` returns a `casadi::DM` of shape (g_dim, 1); index `(r, 0)` for row r (CasADi standard nlpsol output, sibling to `res.at("x")` used at line 277).

- [ ] **Step 2: Run probe with diag ON, collect evidence**

```bash
M5_DIRECTION_DIAG=1 PROBE_STUCK_LIMIT=150 rtk python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc --restart-between-runs --sim-rate 5 \
  --trace-report-dir runs/v21_direction_diag --scenario colreg-rule14-ho
# Inspect container stderr for [M5_DIRECTION_DIAG] lines.
```

- [ ] **Step 3: Decision + remove hook**

If direction rows are all ≥ 0 (satisfied) with margin: keep direction hard, remove the diag hook, commit.
If any direction row is at 0 (active boundary) or negative (would-be violation): open a follow-up task to soften direction (same schedule pattern as min_alt).

- [ ] **Step 4: Commit (hook removal)**

```bash
git commit -m "chore(m5): remove direction diag probe hook (spec §4.4, decision: keep hard)

Direction rows all ≥ margin in rule14-ho probe -> keep hard. Hook removed
per spec C3 (implementation-only, merge前移除)."
```

If the decision is "soften", keep the hook for the follow-up softening task and document in handoff.

---

## Task 10: 全单测 + rule14-ho probe 验证（spec §7）

- [ ] **Step 1: Full M5 test suite via ctest (aggregates failures correctly)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+'
```

Full 27-test list (verified from CMakeLists.txt):
test_vessel_dynamics_model, test_constraint_compiler, test_mid_mpc_nlp_formulation, test_mid_mpc_solver, test_mid_mpc_route_cost, test_row_registry, test_mid_mpc_terminal, test_mid_mpc_continuity, test_mid_mpc_direction, test_mid_mpc_route_frame, test_mid_mpc_waypoint_generator, test_midmpc_tail_gate, test_heading_bounds, test_stand_on_reject, test_tail_builder, test_committed_route, test_committed_candidate_geometry, test_degraded_candidate_adapter, test_gnc_preflight, test_bc_mpc_collision_detector, test_bc_mpc_solver, test_bc_mpc_node_handover, test_nomoto_fallback, test_geometric_fallback, test_avoidance_waypoint_gen, test_avoidance_plan_contract, test_target_corridor_clearance.

Expected: all PASS. On FAIL, inspect `build/m5_tactical_planner/Testing/Temporary/LastTest.log`.

- [ ] **Step 2: rule14-ho probe（spec §7.2 主 probe）**

```bash
PROBE_STUCK_LIMIT=150 rtk python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc --restart-between-runs --sim-rate 5 \
  --trace-report-dir runs/v21_rule14ho --summary-out runs/v21_rule14ho-summary.json \
  --scenario colreg-rule14-ho
```

Pass criteria (spec §7.2):
- NLP SOLVER_CONVERGED 占比 > 30% (was 0%)
- CPA min ≥ 180m (was 0.2-3.4m)
- direction diag dump 各 k 残差记录 (if implementation-only hook added)

- [ ] **Step 3: targeted probes（C5-r2: scenarios from scenarios/COLREGs测试/）**

```bash
# Available scenario files (verified):
for scenario in colreg-rule13-ot colreg-rule15-cs colreg-rule17-cr-so-target-giveway; do
  PROBE_STUCK_LIMIT=120 rtk python3 scripts/run_colregs_clean_8probe.py \
    --profile gnc --restart-between-runs --sim-rate 5 \
    --trace-report-dir runs/v21_$scenario --scenario $scenario
done
```

Note: `colreg-reduce-speed` does NOT exist as a scenario file. ReduceSpeed path is exercised via Rule 17 stand-on scenario (`colreg-rule17-cr-so-target-giveway.yaml`) + unit tests for `direction_disabled` precedence. Document this gap in handoff if no ReduceSpeed scenario exists.

Pass criteria: no regression vs baseline; stand-on skip terminal check, give-way lateral enforces it.

- [ ] **Step 4: Commit evidence**

```bash
git add runs/v21_*
git commit -m "test(m5): v2.1 probe evidence (rule14-ho + Rule13/15/17)

NLP convergence + CPA floor + terminal role matrix validation per spec §7.2.
Note: ReduceSpeed scenario file does not exist; coverage via unit tests only."
```

---

## Task 11: 文档同步（spec §6.3）

**Files:**
- Modify: `docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md` (revision history + supersede markers)
- Modify: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §10.4 (CPA wording)

- [ ] **Step 1: v2 spec revision history + supersede pointers**

In `2026-06-30-m5-committed-route-design-v2.md` revision history table, add row:

```markdown
| v2.1 | 2026-07-04 | NLP 约束分类重构（见 `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`）。§5.5/§7.1/§9.3/§13.1-3 被 v2.1 supersede；§13.4 SOTIF 不变。|
```

At top of §5.5, §7.1, §9.3, §13 (just the nonconvex subsections, not §13.4), add:

```markdown
> **v2.1 supersede**: see `docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md` §X.Y
```

- [ ] **Step 2: 架构报告 §10.4 line 919 CPA wording**

Edit the CPA hard row statement (around line 919):

```markdown
CPA floor 在 Mid-MPC NLP 内采用 suffix-hard schedule（k≥k_cpa 后 hard，前 soft barrier），
tail-gate 在 release 阶段执行 hard CPA check（cpa_hard_m）。详见 spec v2.1 §4.3。
```

- [ ] **Step 3: Commit**

```bash
git add "docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md" \
        "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
git commit -m "docs(m5): v2.1 supersede pointers + 架构 §10.4 CPA wording

v2 spec revision history + §5.5/§7.1/§9.3/§13.1-3 supersede markers (§13.4 SOTIF unchanged).
架构报告 §10.4 CPA hard -> suffix-hard wording per spec v2.1 §4.3."
```

---

## Task 12: handoff + memory

- [ ] **Step 1: handoff entry**

Append to `handoff/workspace_log.md`:

```markdown
## [2026-07-04] ZCode / 9869ac94..<new> / M5 NLP v2.1 约束重构实施

### Task Goal
Implement spec v2.1: min_alt reachable + CPA suffix-hard + terminal full-soften + ROT 4.7.

### Core Changes
- Tasks 1-4: RowBoundConfig v2.1 fields + min_alt/CPA/terminal schedules
- Task 5-7: J_terminal upper-band + tail-gate lateral + CPA release cpa_hard
- Task 8: solver auto-derive k_minalt + k_cpa
- Task 9: GNC ROT 4.7 unified
- Task 10: probe evidence
- Task 11: doc sync

### Current Status
<fill from probe results>

### Handoff Notes
- spec: docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md
- plan: docs/superpowers/plans/2026-07-04-m5-nlp-constraint-restructure.md
- 不 push（待 A4000 验证）
```

- [ ] **Step 2: mempalace drawer + diary**

```bash
mempalace add --wing MASS-L3 --room m5-nlp-v21-impl --content "<key decisions + gotchas>"
mempalace diary_write --agent-name ZCode --wing MASS-L3 --entry "SESSION:2026-07-04|M5.NLP.v21.impl|worktree:.worktrees/colregs-12probe-debug|branch:fix/m5-nlp-heartbeat-shadow-upstream|<commits>|built:min_alt.reachable+cpa.suffix-hard+terminal.soft+rot.4p7|evidence:runs/v21_*|open:A4000.validate+direction.diag.probe|★★★"
```

- [ ] **Step 3: Final commit**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): M5 NLP v2.1 implementation complete"
```

---

## Self-Review Notes

**Spec coverage (verified against v2.1 spec):**
- §4.2 min_alt reachable: Tasks 1+2+8 ✓
- §4.2 B9 ReduceSpeed precedence: Task 8 (`direction_disabled` derivation) ✓
- §4.3 CPA suffix-hard: Tasks 1+3+8 ✓
- §4.3 B5-r2 k_cpa formula: Task 8 ✓
- §4.3 C2-r2 row order: Task 3 (`cpa_row(t,k)=cpa_start+k*n_targets+t`) ✓
- §4.3 B6-r2 CPA release cpa_hard: Task 6 ✓
- §4.4 direction diag probe: implementation-only, deferred to Task 10 evidence collection (no code change to direction logic itself — direction stays hard per spec) ✓
- §4.5 terminal full-soften: Task 4 ✓
- §4.5 B7-r2 two-sided softplus upper-band: Task 5 ✓
- §4.5 B7-gap2 tail-gate lateral: Task 7 ✓
- §4.5 C4-r2 role matrix: Task 7 (`lateral_colreg_active` guard) ✓
- §5.2 ROT 4.7 YAML: Task 9 ✓
- §6.3 doc sync: Task 11 ✓
- §7 单测 + probe: Task 10 ✓

**Placeholder scan:** None. All code blocks complete. Task 5 Step 1 has a `[test body uses existing helpers]` note directing implementer to neighboring tests — this is acceptable (the assertions above it are the contract; the helper calls are visible in the same file).

**Type consistency (verified):**
- `RowBoundConfig` fields: Task 1 (decl) ↔ Task 2/3/4 (apply fns read them) ↔ Task 8 (derive sets them). Names match.
- `tail_gate_terminal_lateral_feasible`: Task 7 helper signature uses `ColregsPreferredDirection` enum (not `double pref_dir`), matching types.hpp:149. accept_tail_gate call site passes `input.colregs_preferred_direction` directly.
- `derive_row_bound_config`: declared in mid_mpc_solver.hpp (Task 8), tested via MidMpcNlpTest fixture (Task 8), called from solve() (Task 8).
- `cpa_hard_m`: Task 6 assumes it exists on `ConstraintInputs`. Precondition check in Task 6 Step 3 — if missing, add it (constraint_compiler.cpp:323 already uses unbumped value, so the field or equivalent exists somewhere; confirm during implementation).

**API alignment (verified against HEAD 9869ac94 source):**
- Fixture names: `TEST(RowRegistry, ...)` for row_registry tests; `MidMpcNlpTest` for solver; `TerminalConstraintTest` for terminal — all match.
- Enum: `ColregsPreferredDirection` (Starboard/Port/ReduceSpeed/Hold) at types.hpp:149 — used correctly in Tasks 7/8.
- Role: `colregs_primary_role == 1U || 2U` for give-way (mid_mpc_solver.cpp:195) — used in Tasks 7/8.
- Field paths: `input.rot_max_rad_s` / `input.colregs_min_alteration_rad` (types.hpp:215/207) — used in Task 8 (NOT `input.constraints.*`).
- CPA row order: `cpa_row(t,k)=cpa_start+k*n_targets+t` (row_registry.hpp:137) — used in Task 3.
- Test list: 27 tests in CMakeLists.txt — all listed in Task 10.

**Execution order:** Task 1 (fields) → 2/3/4 (apply fns, can parallelize) → 5/6/7 (cost + tail-gate) → 8 (solver derive) → 9 (YAML) → 10 (probe) → 11 (docs) → 12 (handoff). No circular deps. Task 8 depends on Tasks 1-4 (fields + apply fns exist). Task 10 depends on all code tasks.
