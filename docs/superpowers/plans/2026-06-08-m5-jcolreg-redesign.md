# M5 J_colreg Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the remaining M5 Mid-MPC IPOPT `Restoration_Failed`/`Max_Iterations` (22→0) by replacing the non-smooth, steep `J_colreg` `fmax` penalty with a smooth exponential barrier + dynamic (range/TCPA) weighting + a smooth gated starboard asymmetry, while keeping avoidance soft.

**Architecture:** The Mid-MPC NLP (`MidMpcNlpFormulation`) builds a CasADi/IPOPT graph once; `MidMpcSolver` re-packs parameters each cycle. Box limits are already IPOPT `lbx`/`ubx`; ROT is two smooth linear rows. This plan changes only the **objective** `J = w_colreg·J_colreg + w_dist·J_dist + w_vel·J_vel + J_asym`. All dynamic weights that depend only on initial geometry (range ramp, TCPA discount) are computed **numerically at pack-time** to keep the symbolic graph smooth; the only decision-variable-dependent new term is the smooth exponential barrier. Grounded in spec [`M5-jcolreg-redesign-spec.md`](../../Design/TDL-Kernel/M5-Tactical-Planner/M5-jcolreg-redesign-spec.md).

**Tech Stack:** C++17, CasADi (MX symbolic + IPOPT nlpsol), ROS2 Humble, GoogleTest, colcon. **Build/test runs on A4000 only** (local Mac lacks colcon/ROS).

---

## Build/Test environment (READ FIRST)

Local Mac **cannot** build. Edit files locally on branch `fix/m5-nlp-convergence`, `scp` to A4000, build+test in-container. Helpers:

```bash
# scp a changed file (run from repo root)
SCPFILE() { scp -q "$1" "a4000:~/Code/mass-l3/$1"; }

# A4000: rebuild runtime (node) — tests OFF (dodges pre-existing-broken test_nomoto_fallback.cpp)
A4_BUILD_RT='cd ~/Code/mass-l3 && CID=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes) && docker exec $CID bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=OFF 2>&1 | tail -3"'

# A4000: build + run the two unit-test targets (BUILD_TESTING reconfigured ON by colcon below)
A4_TEST='cd ~/Code/mass-l3 && CID=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes) && docker exec $CID bash -lc "source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -3; cmake --build /opt/ws/build/m5_tactical_planner --target test_mid_mpc_nlp_formulation test_mid_mpc_solver -j4 2>&1 | tail -3 && cd /opt/ws/build/m5_tactical_planner && ./test_mid_mpc_nlp_formulation 2>&1 | tail -8 && echo ---SOLVER--- && ./test_mid_mpc_solver 2>&1 | grep -vE \"Ipopt|EPL|coin-or|\\*\\*\\*\" | tail -20"'

# A4000: restart node after a runtime rebuild
A4_RESTART='cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml restart sil-nodes'

# A4000: drive one clean encounter (settle 35s to avoid SetParameters wedge) + count IPOPT failures
A4_RUN='cd ~/Code/mass-l3 && B=https://127.0.0.1:18000/api/v1 && sleep 35 && for i in $(seq 1 30); do curl -sk $B/lifecycle/status 2>/dev/null | grep -q current_state && break; sleep 2; done && curl -sk -X POST $B/lifecycle/configure -H "Content-Type: application/json" -d "{\"scenario_id\":\"colreg-rule14-ho\"}" && sleep 3 && curl -sk -X POST $B/lifecycle/activate >/dev/null && for i in $(seq 1 15); do [ "$(curl -sk $B/lifecycle/status | grep -o active)" = active ] && break; sleep 1; done && curl -sk -X POST $B/lifecycle/rate -H "Content-Type: application/json" -d "{\"rate\":5}" >/dev/null && echo running 80s && sleep 80 && CID=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes) && docker logs --tail 7000 $CID 2>&1 | grep -oE "IPOPT status=[A-Za-z_]+" | sort | uniq -c'
```

---

## File Structure

- `.../include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp` — Config (new tunables, `w_colreg` 1000→30), `kIdxGiveWay` param slot (kParamDim 93→94), `build_asym_cost_` decl.
- `.../src/mid_mpc/mid_mpc_nlp_formulation.cpp` — `pack_parameters` (give_way flag + range-ramp weight), `build_colreg_cost_` (exp barrier + TCPA discount), new `build_asym_cost_`, `build_symbolic_graph` (add J_asym).
- `.../src/mid_mpc/mid_mpc_solver.cpp` — remove temporary `[M5DIAG]` diagnostic.
- `.../test/unit/test_mid_mpc_nlp_formulation.cpp` — give_way pack test.
- `.../test/unit/test_mid_mpc_solver.cpp` — rewrite `HeadOnGiveWayRightTurn` (set rule 14, assert starboard not flee).

Path prefix `P=src/l3_tdl_kernel/m5_tactical_planner`.

---

## Task 0: Land the verified box+ROT fix (commit 1 of 2)

The box→`lbx`/`ubx` + ROT-smoothing changes are already made and verified (50→22, unit tests pass). Remove the temporary diagnostic and commit this neutral milestone before the behavior-changing J_colreg work.

**Files:**
- Modify: `$P/src/mid_mpc/mid_mpc_solver.cpp` (remove `[M5DIAG]` block + its call site + the `dbg_log_residuals` function + now-unused `<algorithm>`/`<cmath>` if only used by it — KEEP `<algorithm>`/`<cmath>` only if still referenced).

- [ ] **Step 1: Remove the `[M5DIAG]` diagnostic.** Delete the `// === [M5DIAG] ... ===` anonymous-namespace `dbg_log_residuals` block, and the two `dbg_log_residuals(...)` calls + their `Ndiag/dtdiag/c` locals inside the `else` branch of `solve()`. Leave the rest of the `else` branch (`++consecutive_failures_; ...`) intact. Remove `#include <cmath>` (only `std::abs` in diagnostic used it) but KEEP `#include <algorithm>` (used by `std::min`/`std::max`? verify — if not used elsewhere, remove too).

- [ ] **Step 2: scp + build runtime on A4000.**
Run: `SCPFILE $P/src/mid_mpc/mid_mpc_solver.cpp && ssh a4000 "$A4_BUILD_RT"`
Expected: `Finished <<< m5_tactical_planner`.

- [ ] **Step 3: scp tests + run unit tests on A4000** (formulation + solver from the box/ROT work).
Run: `SCPFILE $P/test/unit/test_mid_mpc_nlp_formulation.cpp && SCPFILE $P/test/unit/test_mid_mpc_solver.cpp && ssh a4000 "$A4_TEST"`
Expected: formulation 4/4 PASS; solver — all PASS **except** `HeadOnGiveWayRightTurn` (known, fixed in Task 4). `GDim_MatchesTwoNMinus1_RotOnly` PASS, `BearingOutsideWindow...` PASS.

- [ ] **Step 4: Commit 1.**
```bash
git add "$P/src/mid_mpc/mid_mpc_solver.cpp" "$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp" "$P/test/unit/test_mid_mpc_nlp_formulation.cpp" "$P/test/unit/test_mid_mpc_solver.cpp"
git commit -m "fix(m5): heading/speed box as IPOPT lbx/ubx + smooth ROT rows

Encode heading/speed limits as variable bounds (lbx/ubx) instead of
general inequality rows in g, and split the ROT |.| constraint into two
smooth linear rows. An optimum pinned to an active *general* constraint
under limited-memory Hessian + adaptive mu was restoration-fragile;
variable bounds make it IPOPT's robust case. Restoration_Failed 50->22
on colreg-rule14-ho (A4000). g_dim now 2(N-1) (ROT only).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 1: Config tunables + give_way param slot

**Files:**
- Modify: `$P/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`
- Modify: `$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp` (`pack_parameters`)
- Test: `$P/test/unit/test_mid_mpc_nlp_formulation.cpp`

- [ ] **Step 1: Write the failing test (give_way packing).** Append to `test_mid_mpc_nlp_formulation.cpp` before the final `}` of file (after `GDim_...` test). Uses the new `kIdxGiveWay` constant.

```cpp
TEST(MidMpcNlpFormulationTest, PackGiveWayFlag_FromApplicableRules) {
  using mass_l3::m5::mid_mpc::kIdxGiveWay;
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 4; cfg.max_targets = 4;
  MidMpcNlpFormulation formulation(cfg);
  formulation.build_symbolic_graph();

  MidMpcInput inp{};
  inp.constraints.heading_min_rad = -M_PI; inp.constraints.heading_max_rad = M_PI;
  inp.constraints.speed_min_mps = 0.0; inp.constraints.speed_max_mps = 15.0;

  inp.constraints.applicable_rules = {17};            // stand-on → no give-way
  EXPECT_DOUBLE_EQ(static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 0.0);

  inp.constraints.applicable_rules = {14};            // head-on give-way
  EXPECT_DOUBLE_EQ(static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 1.0);

  inp.constraints.applicable_rules = {15};            // crossing give-way
  EXPECT_DOUBLE_EQ(static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 1.0);
}
```

- [ ] **Step 2: Add `kIdxGiveWay` + shift `kIdxTargets` + bump `kParamDim`** in the header. Replace the layout block tail:

```cpp
constexpr int32_t kIdxOwnPsi         = 12;
constexpr int32_t kIdxGiveWay        = 13;  // 1.0 if M6 rule 14/15 active, else 0.0
constexpr int32_t kIdxTargets        = 14;
constexpr int32_t kTargetStride      = 5;
constexpr int32_t kMaxTargets        = 16;
constexpr int32_t kParamDim          = kIdxTargets + kMaxTargets * kTargetStride;  // 94
static_assert(kParamDim == 94, "parameter layout mismatch — update kParamDim if constants change");
```

- [ ] **Step 3: Add Config tunables** in the header `struct Config` (replace the `w_colreg{1000.0}` line and append new fields):

```cpp
    // [TBD-HAZID] COLREGs compliance cost weight (~3x route per colav_algorithms NLM).
    double w_colreg{30.0};
    // [TBD-HAZID] Route-track deviation cost weight.
    double w_dist{10.0};
    // [TBD-HAZID] Speed efficiency cost weight.
    double w_vel{1.0};
    // [TBD-HAZID] Exponential-barrier steepness zeta [1/m] in exp(-zeta*(d-cpa_safe)).
    double zeta{1.0e-3};
    // [TBD-HAZID] Range-ramp outer distance [m] (6 nm); weight 0 beyond, 1 at cpa_safe.
    double pwt_outer_m{11112.0};
    // [TBD-HAZID] TCPA discount time constant [s] in exp(-t_k/T_d).
    double t_discount_s{100.0};
    // [TBD-HAZID] Starboard asymmetry weight (softplus port penalty).
    double k_asym{50.0};
    // [TBD-HAZID] Asymmetry smoothing scale [rad] (~5 deg).
    double asym_tau{0.0873};
```

- [ ] **Step 4: Add `build_asym_cost_` declaration** in the header private section, after `build_colreg_cost_`:

```cpp
  [[nodiscard]] casadi::MX build_asym_cost_() const;
```

- [ ] **Step 5: Pack give_way + range-ramp weight** in `pack_parameters` (`mid_mpc_nlp_formulation.cpp`). Add give_way before the target loop, and replace the per-target weight line `p(base + 4) = std::min(...)`.

Add `#include <cmath>` (for `std::hypot`) and `#include <algorithm>` (for `std::clamp`) at top if absent. Before the target loop, add:

```cpp
  // give_way flag: M6 rule 14 (head-on) or 15 (crossing give-way) ⇒ apply
  // starboard asymmetry. Other rules / no encounter ⇒ symmetric.
  bool give_way = false;
  for (const std::uint8_t rule : input.constraints.applicable_rules) {
    if (rule == 14u || rule == 15u) { give_way = true; }
  }
  p(kIdxGiveWay) = give_way ? 1.0 : 0.0;
```

Replace the per-target weight assignment (was `const double cpa=...; const double tcpa=...; const double cpa_tcpa_product=...; p(base + 4) = std::min(1.0 / cpa_tcpa_product, kMaxTargetWeight);`) with the range ramp:

```cpp
    // Range-ramp weight: 0 beyond pwt_outer, linear to 1 at pwt_inner (=cpa_safe).
    // Depends only on initial geometry → computed numerically (keeps NLP smooth).
    const double rng0 = std::hypot(tgt.x_m - input.own_ship.x_m,
                                   tgt.y_m - input.own_ship.y_m);
    const double pwt_inner = input.constraints.cpa_safe_m;
    const double span = std::max(cfg_.pwt_outer_m - pwt_inner, 1.0);
    const double w_range = std::clamp((cfg_.pwt_outer_m - rng0) / span, 0.0, 1.0);
    p(base + 4) = w_range;
```

(The now-unused constants `kMinCpaForWeight`, `kMinTcpaForWeight`, `kMaxTargetWeight`, `kTargetWeightDenomMin` in the anon namespace may be left or removed; remove them to avoid -Wunused if the build warns.)

- [ ] **Step 6: scp + run unit tests.**
Run: `SCPFILE $P/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp && SCPFILE $P/src/mid_mpc/mid_mpc_nlp_formulation.cpp && SCPFILE $P/test/unit/test_mid_mpc_nlp_formulation.cpp && ssh a4000 "$A4_TEST"`
Expected: `PackGiveWayFlag_FromApplicableRules` PASS; `PackParameters_CorrectDim` PASS (kParamDim=94). NOTE: `build_colreg_cost_` still references removed weight semantics? No — it only reads `slot(p_, base+4)` as `tw`, still valid. Solver tests unchanged except known `HeadOnGiveWayRightTurn`.

- [ ] **Step 7: Commit** (WIP on branch, will squash-or-keep per preference).
```bash
git add "$P/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp" "$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp" "$P/test/unit/test_mid_mpc_nlp_formulation.cpp"
git commit -m "feat(m5): add give_way param slot + range-ramp target weight + J_colreg tunables

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Smooth exponential barrier + TCPA discount in J_colreg

**Files:**
- Modify: `$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp` (`build_colreg_cost_`)
- Test: `$P/test/unit/test_mid_mpc_solver.cpp` (existing `HeadOnGiveWayRightTurn`/`CrossingGiveWay` exercise it; add a cost-engagement check)

- [ ] **Step 1: Write the failing test (J_colreg engages, near>far).** Append to `test_mid_mpc_solver.cpp` before `ConsecutiveFailuresResetOnSuccess`. It asserts a near head-on target perturbs the solution away from the no-target straight line (proving J_colreg is non-zero/active), and converges.

```cpp
TEST_F(MidMpcNlpTest, ColregBarrierEngages_NearTargetDeflectsVsNoTarget) {
  // No-target baseline: tracks route (heading ~0).
  const auto base = solver_->solve(make_straight_line_input(), nullptr);
  ASSERT_EQ(base.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(std::abs(final_heading_deg(base)), 5.0);

  // Near head-on target ⇒ barrier active ⇒ heading deflects appreciably.
  const auto ho = solver_->solve(make_head_on_input(), nullptr);
  EXPECT_EQ(ho.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(std::abs(final_heading_deg(ho)), 10.0)
      << "J_colreg barrier did not deflect heading for a near head-on target";
}
```

- [ ] **Step 2: Run to verify it fails** (current `fmax` cost may not deflect ≥10° with new weights / or the test references new behavior).
Run: `SCPFILE $P/test/unit/test_mid_mpc_solver.cpp && ssh a4000 "$A4_TEST"`
Expected: `ColregBarrierEngages...` may FAIL or be marginal pre-rewrite; this anchors the new behavior.

- [ ] **Step 3: Rewrite `build_colreg_cost_`** in `mid_mpc_nlp_formulation.cpp`. Replace the whole function body. (`<cmath>` already added in Task 1.)

```cpp
casadi::MX MidMpcNlpFormulation::build_colreg_cost_() const {
  const int32_t N  = cfg_.n_horizon;
  const int32_t Nt = cfg_.max_targets;
  const casadi::MX dt   = casadi::DM(cfg_.dt_s);
  const casadi::MX cpa  = slot(p_, kIdxCpaSafe);     // d_safe [m]
  const casadi::MX zeta = casadi::DM(cfg_.zeta);
  constexpr double kSqrtGuard = 1.0;                 // [m^2] smooth-sqrt guard

  // Pre-integrate own-ship cumulative position at each step k.
  std::vector<casadi::MX> x_own(static_cast<std::size_t>(N));
  std::vector<casadi::MX> y_own(static_cast<std::size_t>(N));
  casadi::MX cx = slot(p_, kIdxX0);
  casadi::MX cy = slot(p_, kIdxY0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
    x_own[static_cast<std::size_t>(k)] = cx;
    y_own[static_cast<std::size_t>(k)] = cy;
  }

  casadi::MX cost(0.0);
  for (int32_t t = 0; t < Nt; ++t) {
    const int32_t base = kIdxTargets + t * kTargetStride;
    const casadi::MX tx = slot(p_, base + 0);
    const casadi::MX ty = slot(p_, base + 1);
    const casadi::MX tc = slot(p_, base + 2);
    const casadi::MX ts = slot(p_, base + 3);
    const casadi::MX tw = slot(p_, base + 4);   // range-ramp weight (numeric, 0..1)
    const casadi::MX tdx = ts * casadi::MX::cos(tc);
    const casadi::MX tdy = ts * casadi::MX::sin(tc);
    for (int32_t k = 0; k < N; ++k) {
      const casadi::MX kdt = casadi::DM(static_cast<double>(k) * cfg_.dt_s);
      const casadi::MX dx  = x_own[static_cast<std::size_t>(k)] - (tx + tdx * kdt);
      const casadi::MX dy  = y_own[static_cast<std::size_t>(k)] - (ty + tdy * kdt);
      const casadi::MX d   = casadi::MX::sqrt(dx * dx + dy * dy + kSqrtGuard);
      // Smooth exponential barrier: ~0 far, 1 at d=cpa_safe, grows when penetrating.
      const casadi::MX barrier = casadi::MX::exp(-zeta * (d - cpa));
      // TCPA discount exp(-t_k/T_d): constant per step → numeric coefficient.
      const double disc = std::exp(-(static_cast<double>(k) * cfg_.dt_s)
                                   / cfg_.t_discount_s);
      cost = cost + tw * casadi::DM(disc) * barrier;
    }
  }
  const casadi::MX scale_denom = casadi::DM(
      static_cast<double>(std::max(1, Nt * N)));
  return cost / scale_denom;
}
```

- [ ] **Step 4: Run tests.**
Run: `SCPFILE $P/src/mid_mpc/mid_mpc_nlp_formulation.cpp && ssh a4000 "$A4_TEST"`
Expected: `ColregBarrierEngages...` PASS; `CrossingGiveWay`/`StraightLineNoTargets`/`WarmStart...` PASS; `HeadOnGiveWayRightTurn` still the known failure (Task 4). `BearingOutsideWindow...` PASS.

- [ ] **Step 5: Commit.**
```bash
git add "$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp" "$P/test/unit/test_mid_mpc_solver.cpp"
git commit -m "feat(m5): J_colreg smooth exponential barrier + TCPA discount

Replace non-smooth fmax(0,cpa^2-d^2) with mu*exp(-zeta*(d-cpa_safe)) and
a per-step TCPA discount; per-target range-ramp weight folded numerically
at pack-time. Removes the restoration-fragile cost kink. Grounded in
colav_algorithms NLM (high-conf).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Gated smooth starboard asymmetry (J_asym)

**Files:**
- Modify: `$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp` (add `build_asym_cost_`, wire into `build_symbolic_graph`)
- Test: `$P/test/unit/test_mid_mpc_solver.cpp`

- [ ] **Step 1: Write the failing test (gated starboard preference).** Append before `ConsecutiveFailuresResetOnSuccess`. Symmetric head-on WITH give-way rule ⇒ starboard (positive heading); WITHOUT ⇒ asymmetry off (no port penalty). Uses wide box so only the cost decides side.

```cpp
TEST_F(MidMpcNlpTest, GiveWayAsymmetry_PrefersStarboardNotFlee) {
  // Head-on, give-way (Rule 14) ⇒ must pick starboard (psi>0), not port / -180 flee.
  MidMpcInput gw = make_head_on_input();
  gw.constraints.applicable_rules = {14};
  const auto sol = solver_->solve(gw, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(final_heading_deg(sol), 0.0)   // starboard
      << "give-way head-on did not prefer starboard (got "
      << final_heading_deg(sol) << " deg)";
  EXPECT_LT(final_heading_deg(sol), 90.0);  // sane magnitude

  // Without give-way rule, J_asym is gated OFF (no port penalty term active).
  MidMpcInput none = make_head_on_input();  // applicable_rules empty
  const auto sol2 = solver_->solve(none, nullptr);
  EXPECT_EQ(sol2.status, MidMpcSolver::SolveStatus::Converged);
}
```

- [ ] **Step 2: Run to verify it fails.**
Run: `SCPFILE $P/test/unit/test_mid_mpc_solver.cpp && ssh a4000 "$A4_TEST"`
Expected: `GiveWayAsymmetry...` FAILS (symmetric cost may pick port/−180 without J_asym).

- [ ] **Step 3: Add `build_asym_cost_`** in `mid_mpc_nlp_formulation.cpp` (place after `build_colreg_cost_`).

```cpp
// ===========================================================================
// build_asym_cost_() — smooth, gated Rule-14/15 starboard preference.
//
// Softplus port-penalty: tau*log(1+exp((bearing-psi_k)/tau)). ≈ (bearing-psi_k)
// when psi_k is to port (psi_k < bearing), ≈ 0 to starboard. C∞ smooth (no kink,
// unlike a raw port/stbd multiplier switch). Multiplied by give_way ∈ {0,1} so
// it vanishes for stand-on / no encounter. Biases a symmetric head-on toward
// starboard, preventing the port turn / course-reversal degenerate optimum.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_asym_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX bearing = slot(p_, kIdxRouteBearing);
  const casadi::MX give_way = slot(p_, kIdxGiveWay);
  const casadi::MX tau = casadi::DM(cfg_.asym_tau);
  casadi::MX cost(0.0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX z = (bearing - psi_k) / tau;          // >0 when to port
    cost = cost + tau * casadi::MX::log(1.0 + casadi::MX::exp(z));
  }
  return give_way * casadi::DM(cfg_.k_asym) * cost;
}
```

- [ ] **Step 4: Wire J_asym into the objective** in `build_symbolic_graph`. Replace the `J_ = ...` assignment:

```cpp
  J_ = casadi::DM(cfg_.w_colreg) * build_colreg_cost_()
     + casadi::DM(cfg_.w_dist)   * build_distance_cost_()
     + casadi::DM(cfg_.w_vel)    * build_velocity_cost_()
     + build_asym_cost_();
```

- [ ] **Step 5: Run tests.**
Run: `SCPFILE $P/src/mid_mpc/mid_mpc_nlp_formulation.cpp && ssh a4000 "$A4_TEST"`
Expected: `GiveWayAsymmetry...` PASS. Other formulation/solver tests PASS except `HeadOnGiveWayRightTurn` (Task 4).

- [ ] **Step 6: Commit.**
```bash
git add "$P/src/mid_mpc/mid_mpc_nlp_formulation.cpp" "$P/test/unit/test_mid_mpc_solver.cpp"
git commit -m "feat(m5): gated smooth starboard asymmetry (Rule-14/15 give-way)

Softplus port-penalty gated by give_way; smooth (no multiplier kink),
breaks the symmetric head-on port/-180 degenerate optimum toward starboard.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Rescope `HeadOnGiveWayRightTurn` to the M5 contract

The old test asserted M5 alone picks starboard in a wide-box symmetric head-on — testing the wrong layer. Rescope to "given a give-way rule, M5 turns starboard within a sane range" (now satisfied by J_asym).

**Files:**
- Modify: `$P/test/unit/test_mid_mpc_solver.cpp:166-173` (the `HeadOnGiveWayRightTurn` test)

- [ ] **Step 1: Rewrite the test.** Replace the `HeadOnGiveWayRightTurn` test body:

```cpp
TEST_F(MidMpcNlpTest, HeadOnGiveWayRightTurn) {
  // Rule-14 give-way head-on: M5 must execute a STARBOARD avoidance (psi>0),
  // not a port turn or course reversal. (Starboard *decision* is M4/M6's job;
  // here M5 receives the give-way rule and must comply.) Magnitude is
  // [TBD-HAZID] weight-dependent, so assert direction + sane bound only.
  MidMpcInput input = make_head_on_input();
  input.constraints.applicable_rules = {14};
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(final_heading_deg(sol), 5.0);    // starboard, non-trivial
  EXPECT_LT(final_heading_deg(sol), 90.0);   // sane
}
```

- [ ] **Step 2: Run tests — full suite green.**
Run: `SCPFILE $P/test/unit/test_mid_mpc_solver.cpp && ssh a4000 "$A4_TEST"`
Expected: **all** formulation + solver tests PASS (0 failures).

- [ ] **Step 3: Commit.**
```bash
git add "$P/test/unit/test_mid_mpc_solver.cpp"
git commit -m "test(m5): rescope HeadOnGiveWayRightTurn to M5 give-way contract

Assert M5 executes starboard when handed a Rule-14 give-way (direction +
sane bound), not the old wide-box symmetric assertion (which tested the
M4/M6 side-decision layer, not M5).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Integration verification (22→0) + 3-end sync

**Files:** none (rebuild runtime + drive encounter).

- [ ] **Step 1: Rebuild runtime + restart on A4000.**
Run: `ssh a4000 "$A4_BUILD_RT" && ssh a4000 "$A4_RESTART"`
Expected: build Finished; container Started.

- [ ] **Step 2: Drive encounter + count IPOPT failures.**
Run: `ssh a4000 "$A4_RUN"`
Expected: histogram **empty** (0 `Restoration_Failed`/`Max_Iterations`), or document residual count + reason. Compare to baseline 22.

- [ ] **Step 3: Verify cost_colreg>0 + starboard avoidance.**
Run:
```bash
ssh a4000 'cd ~/Code/mass-l3 && CID=$(docker compose -f docker-compose.yml -f docker-compose.a4000.yml ps -q sil-nodes) && docker logs --tail 4000 $CID 2>&1 | grep -E "GeoFallback|MidMPC" | tail -8'
```
Expected: M5 status NORMAL (not DEGRADED fallback); GeoFallback lines absent or rare; own heading turning starboard (positive delta) through the encounter.

- [ ] **Step 4: End-to-end avoidance regression (no circle / no U-turn).** If `_retest_spinfix.py` exists on A4000:
Run: `ssh a4000 'cd ~/Code/mass-l3 && export ORCH_URL=https://127.0.0.1:18000 && timeout 200 python3 scripts/_retest_spinfix.py 2>&1 | tail -20'` (or the trajectory dump check)
Expected: starboard avoid → past CPA → return to track; loops=0, no −180.

- [ ] **Step 5: Push branch to both remotes (3-end sync).**
```bash
git push origin fix/m5-nlp-convergence
git push gitlab fix/m5-nlp-convergence
```

- [ ] **Step 6: Align A4000 to pushed commits.**
Run: `ssh a4000 'cd ~/Code/mass-l3 && git fetch origin && git status -sb | head -3'`
(If A4000 was edited only via scp, reconcile: `git stash` local scp edits then `git reset --hard origin/fix/m5-nlp-convergence`, OR commit-on-A4000 already matches. Verify HEAD == pushed HEAD.)

- [ ] **Step 7: Update M5 progress + spec status + gap doc fix record.** Append to `docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-progress.md` (Currently Implementing → done), set spec status to ✅ implemented, and add a "修复记录" entry in the gap doc with the final 50→22→0 result. Commit:
```bash
git add docs/Design/TDL-Kernel/M5-Tactical-Planner/ "docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md"
git commit -m "docs(m5): record J_colreg redesign result (Restoration_Failed 50->0)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
git push origin fix/m5-nlp-convergence && git push gitlab fix/m5-nlp-convergence
```

---

## Self-Review

**Spec coverage:** §3.1 exp barrier → Task 2. §3.2 dynamic weighting (range numeric + TCPA constant) → Task 1 (range) + Task 2 (TCPA). §3.3 gated softplus asymmetry → Task 3. §3.4 keep soft + M4 box bounds → unchanged (already done). §4 done parts → Task 0. §5 test rescope → Task 4. §6 decisions → all reflected (μ=30, both weights, ROT kept, two-commit split: Task 0 = commit 1, Tasks 1-4 = commit 2 cluster). §8 DoD → Task 5 (22→0, cost_colreg>0, starboard, units tests, e2e, M5DIAG removed in Task 0, 3-end sync).

**Placeholder scan:** numeric defaults are concrete (`w_colreg=30, zeta=1e-3, pwt_outer=11112, t_discount=100, k_asym=50, asym_tau=0.0873`), all marked `[TBD-HAZID]` for HAZID RUN-001 — intentional honest TBD (design value present, calibration deferred), not a plan-placeholder.

**Type consistency:** `kIdxGiveWay`=13, `kIdxTargets`=14, `kParamDim`=94 used consistently (header static_assert, pack test, pack_parameters target loop via `kIdxTargets`). `build_asym_cost_` declared (Task 1 Step 4) + defined (Task 3) + called (Task 3 Step 4). Config fields `zeta/pwt_outer_m/t_discount_s/k_asym/asym_tau` defined (Task 1) + used (Task 2, 3). `final_heading_deg`/`make_head_on_input`/`make_straight_line_input` are existing helpers.
