// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>

#include <casadi/casadi.hpp>
#include <spdlog/spdlog.h>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// [TBD-HAZID] Consecutive-failure threshold before MRM-02 escalation.
// Calibrate via HAZID RUN-001 WP-04 FM-2 sensitivity analysis (detailed design §7.1).
constexpr int64_t kConsecutiveFailureEscalation = 5;

// ===========================================================================
// Constructor — store formulation reference and opts (nlpsol already built).
// ===========================================================================
MidMpcSolver::MidMpcSolver(const MidMpcNlpFormulation& formulation,
                             const IpoptOptions& opts)
    : formulation_(formulation), opts_(opts) {}

// ===========================================================================
// g_dim_() — delegate to formulation.
// ===========================================================================
int32_t MidMpcSolver::g_dim_() const noexcept {
  return formulation_.g_dim();
}

// ===========================================================================
// pack_warm_start_() — extract previous-cycle trajectory into x0 = [psi; u].
// If warm trajectory is shorter than N (degenerate), repeats the last valid
// point rather than leaving zeros (avoids psi=0/u=0 starting outside bounds).
//
// Slice C1 (spec §6.5): warm-start is now SPLIT prefix/suffix:
//   prefix segment (k < K): x0 = prefix_psi/prefix_u (the equality-pinned values
//     from the committed-geometry reprojection). These are read from the input's
//     prefix_psi_rad/prefix_u_mps (the solver's input, not the raw previous
//     solution), so the warm start is consistent with the equality rows.
//   suffix segment (k >= K): x0 = COLD-START seed (own_psi/own_u), NOT the
//     previous solution. Spec §6.5 rationale: prefix equality already guarantees
//     prefix continuity; the suffix uses cold-start (own_psi) to avoid warm-start
//     accumulation drift (v1 root cause). Suffix stability comes from the prefix
//     anchor + J_route/J_dist pull-back, not warm-start continuation.
//
// This signature is kept (warm only) for legacy callers; the K-aware split
// happens in solve() which builds x0 directly when K>0.
// ===========================================================================
casadi::DM MidMpcSolver::pack_warm_start_(const MidMpcSolution& warm) const {
  const int32_t N     = formulation_.config().n_horizon;
  const int32_t n_warm = static_cast<int32_t>(warm.trajectory.size());
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    const int32_t src = (k < n_warm) ? k : (n_warm - 1);
    if (src >= 0) {
      const auto& pt = warm.trajectory[static_cast<std::size_t>(src)];
      x0(k)     = pt.psi_rad;
      x0(N + k) = pt.u_mps;
    }
  }
  return x0;
}

// ===========================================================================
// pack_cold_start_() — initial guess from current own-ship state.
// Uses own-ship heading as psi seed (natural initial point near optimum)
// and current speed (or planned/nominal if speed < 0.1 m/s).
// ===========================================================================
casadi::DM MidMpcSolver::pack_cold_start_(const MidMpcInput& input) const {
  const int32_t N = formulation_.config().n_horizon;
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  const double psi_seed = input.own_ship.psi_rad;
  const double u_seed = (input.own_ship.u_mps > 0.1) ? input.own_ship.u_mps
                        : (input.planned_speed_mps > 0.1 ? input.planned_speed_mps : 5.14);
  for (int32_t k = 0; k < N; ++k) {
    x0(k)     = psi_seed;
    x0(N + k) = u_seed;
  }
  return x0;
}

// ===========================================================================
// solve() — pack params, call IPOPT, record timing, update failure counter.
//
// Constraint convention (g >= 0): lbg = zeros, ubg = +inf by default.
// Slice N1 (spec §3.8): the per-class lbg/ubg are now built from the
// formulation's RowRegistry + the per-cycle RowBoundConfig, enabling:
//   - prefix-equality rows (active k<K → [0,0], inactive → [-inf,+inf] double)
//   - COLREG prefix softening (k<K → [-inf,+inf], k>=K → [0,+inf])
//   - direction_disabled (all direction/min_alt → [-inf,+inf])
// Default RowBoundConfig (K=0, no flags) reproduces the legacy zeros/inf
// everywhere except prefix-equality (which becomes [-inf,+inf] = no-op), so
// N1 first version does not change runtime behaviour.
// ===========================================================================
MidMpcSolution MidMpcSolver::solve(const MidMpcInput& input,
                                    const MidMpcSolution* warm_start,
                                    const RowBoundConfig& row_bounds) {
#ifdef M5_USE_ACADOS
  // P1b-1b Task 17: dispatch to the production acados backend when it has been
  // installed (acados_solver_ non-null). The dispatch is a pure #ifdef branch
  // at the TOP of solve(); the existing IPOPT path below is byte-for-byte
  // UNCHANGED (no reformatting, no while-I'm-here edits). When the flag is off
  // this branch compiles to nothing; when on but acados_solver_ is null (e.g.
  // the node chose IPOPT, or the acados backend failed to construct) solve()
  // falls through to IPOPT.
  //
  // NOTE: row_bounds (Slice N1 / C1 / D1) is an IPOPT-path concern (the acados
  // formulation encodes prefix activation + CPA schedule via per-stage params
  // + idxsh, not via lbg/ubg row bounds). The acados wrapper's pack_parameters
  // already derives prefix_active_k / pact_pre / per-stage drift from the input,
  // so row_bounds is intentionally NOT forwarded — it would be a no-op there.
  if (acados_solver_ != nullptr) {
    return acados_solver_->solve(input, warm_start);
  }
#endif
  const auto t_start = std::chrono::steady_clock::now();

  const casadi::DM p_val = formulation_.pack_parameters(input);

  // ── Slice C1 (spec §6.5): warm-start prefix/suffix split.
  //   prefix (k<K): x0 = prefix_psi/prefix_u (equality-pinned reprojected values).
  //   suffix (k>=K): x0 = cold-start seed (own_psi/own_u), NOT previous solution.
  // The previous-cycle warm trajectory is deliberately ignored for BOTH segments:
  // prefix is anchored by equality; suffix uses cold-start to prevent accumulation
  // drift (spec §6.5, v1 root cause). When K=0 (no prefix / first commit) this
  // reduces to pure cold-start, matching pack_cold_start_ exactly.
  const int32_t N = formulation_.config().n_horizon;
  const int32_t K = static_cast<int32_t>(static_cast<double>(p_val(
      mass_l3::m5::mid_mpc::kIdxPrefixActiveK)));
  const int32_t K_eff = (K < 0) ? 0 : ((K > N) ? N : K);
  casadi::DM x0_val = pack_cold_start_(input);
  for (int32_t k = 0; k < K_eff; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    const double psi_k = (kk < input.prefix_psi_rad.size())
        ? input.prefix_psi_rad[kk] : input.own_ship.psi_rad;
    const double u_k = (kk < input.prefix_u_mps.size())
        ? input.prefix_u_mps[kk] : input.own_ship.u_mps;
    x0_val(k)     = psi_k;
    x0_val(N + k) = u_k;
  }
  (void)warm_start;  // C1: suffix uses cold-start, not previous solution (§6.5)

  const int32_t gdim = g_dim_();

  // Heading & speed box limits as per-variable bounds (lbx/ubx). x = [psi; u].
  // psi[k] in [heading_min, heading_max], u[k] in [speed_min, speed_max].
  // IPOPT keeps every iterate strictly inside these bounds and auto-projects
  // x0, so a box-active optimum is the robust case (vs. restoration-fragile
  // general inequality rows — see MidMpcNlpFormulation::g_dim rationale).
  const auto& cst = input.constraints;
  // Phase 3.1 (spec v2.3 §2.1): extend lbx/ubx with the σ dimension when the
  // formulation built it. σ is bounded below by 0 (sign-constrained slack)
  // and above by +inf. x0_val also gets a 0 seed for σ (start with no slack).
  const bool slack_enabled = formulation_.config().cpa_slack_enabled;
  const int32_t x_dim = slack_enabled ? (2 * N + 1) : (2 * N);
  casadi::DM lbx = casadi::DM::zeros(x_dim, 1);
  casadi::DM ubx = casadi::DM::zeros(x_dim, 1);
  for (int32_t k = 0; k < N; ++k) {
    lbx(k)     = cst.heading_min_rad;
    ubx(k)     = cst.heading_max_rad;
    lbx(N + k) = cst.speed_min_mps;
    ubx(N + k) = cst.speed_max_mps;
  }
  if (slack_enabled) {
    lbx(2 * N) = 0.0;     // σ ≥ 0
    ubx(2 * N) = std::numeric_limits<double>::infinity();    // σ ≤ +inf
    // Extend x0_val to match x_dim with σ seed 0 (start with no slack).
    casadi::DM x0_slack = casadi::DM::zeros(x_dim, 1);
    for (int32_t i = 0; i < 2 * N; ++i) {
      x0_slack(i) = x0_val(i);
    }
    x0_val = x0_slack;
  }

  // Slice N1: per-class lbg/ubg from the formulation's RowRegistry.
  // RowBoundConfig (default {}) reproduces legacy zeros/inf except for the
  // prefix-equality class, which is double-disabled [-inf,+inf] when K=0 (no
  // prefix → the placeholder rows are unconstrained, a no-op).
  //
  // Slice T1: the terminal hard rows (§5.5) now hold REAL expressions (not the
  // N1 zero placeholders). g_term_side = pref_dir·l[N-1] - l_min is INFEASIBLE
  // (≡ -l_min < 0) when preferred_direction==0. spec §3.3: terminal rows share
  // the direction/min_alt activation condition (give-way + pref_dir≠0 + non-
  // HOLD/ReduceSpeed). So the solver derives terminal_disabled from the input
  // when the caller did not explicitly enable it — this keeps legacy callers
  // (which pass the default RowBoundConfig{}) no-op-safe: a Hold/stand-on input
  // disables the terminal rows automatically. W1/node may still override.
  //
  // Slice C1 (spec §6.3/§6.4): the active prefix K is derived from the input
  // (prefix_active_k, packed by assemble_input_ from the GNC guard distance)
  // and propagated into the RowBoundConfig. When K>0 the COLREG prefix rows are
  // softened (colreg_prefix_softened) so a target moving into the frozen prefix
  // geometry cannot make the NLP infeasible (§6.4). A caller-supplied K in the
  // RowBoundConfig takes precedence (e.g. a test that sets K explicitly).
  RowBoundConfig rb_eff = row_bounds;
  if (rb_eff.K == 0 && K_eff > 0) {
    rb_eff.K = K_eff;
  }
  if (K_eff > 0) {
    rb_eff.colreg_prefix_softened = true;  // §6.4: soften prefix COLREG rows
  }
  // v2.1 §4.2/§4.3: derive defaults from input, then merge caller row_bounds
  // on top. Caller K/colreg_prefix_softened precedence preserved above (K_eff
  // propagation). For direction/terminal_disabled + the v2.1 schedule fields,
  // caller wins only when explicitly set: caller true on direction/terminal
  // wins; v2.1 schedule fields honor *_override_valid.
  const RowBoundConfig derived = derive_row_bound_config(
      input, formulation_.config().n_horizon, formulation_.config().dt_s);
  last_minalt_box_infeasible_ = derived.minalt_box_infeasible;  // v2.2 §13.1 expose
  // direction_disabled / terminal_disabled: bool fields have no sentinel, so
  // caller explicit-true wins (matches the original auto-disable contract: the
  // caller could force-disable but could not force-enable). If the caller left
  // it false, derived overwrites (give-way lateral → false, else → true).
  if (!rb_eff.direction_disabled) { rb_eff.direction_disabled = derived.direction_disabled; }
  if (!rb_eff.terminal_disabled)  { rb_eff.terminal_disabled  = derived.terminal_disabled; }
  // v2.1 schedule merge: caller override_valid wins, else use derived.
  rb_eff.minalt_hard_from_k = rb_eff.minalt_override_valid
      ? rb_eff.minalt_hard_from_k : derived.minalt_hard_from_k;
  rb_eff.cpa_hard_from_k = rb_eff.cpa_override_valid
      ? rb_eff.cpa_hard_from_k : derived.cpa_hard_from_k;
  rb_eff.direction_hard_from_k = rb_eff.direction_override_valid
      ? rb_eff.direction_hard_from_k : derived.direction_hard_from_k;
  // terminal_nlp_soft: caller explicit always wins (it's a bool with no
  // "override_valid" sentinel; default true from Task 7 flip).
  // (rb_eff.terminal_nlp_soft already equals row_bounds.terminal_nlp_soft.)
  const BoundArray bounds =
      formulation_.row_registry().build_bounds(rb_eff);
  const int32_t nb = static_cast<int32_t>(bounds.lbg.size());
  // FAIL-CLOSED (spec §3.8/§10.1, review High): the registry is rebuilt per-cycle
  // in build_constraints_, so total_rows() MUST equal g_dim(). A size mismatch is
  // a row-contract bug (registry and the symbolic g built out of sync). Falling
  // back to legacy zeros/inf would silently re-harden softened COLREG rows /
  // degrade active equalities into half-constraints — so do NOT solve; return a
  // NumericalFailure instead (same path as an IPOPT throw, below).
  if (nb != gdim) {
    const auto t_mm = std::chrono::steady_clock::now();
    spdlog::error("[M5][MidMPC] row registry size mismatch: registry={} g_dim={}",
                  nb, gdim);
    ++consecutive_failures_;
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
    MidMpcSolution fail;
    fail.status = SolveStatus::NumericalFailure;
    fail.solve_duration_ms = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t_mm - t_start).count());
    return fail;
  }

  casadi::DM lbg = casadi::DM::zeros(gdim, 1);
  casadi::DM ubg = casadi::DM::inf(gdim, 1);
  for (int32_t i = 0; i < gdim; ++i) {
    lbg(i) = bounds.lbg[static_cast<std::size_t>(i)];
    ubg(i) = bounds.ubg[static_cast<std::size_t>(i)];
  }

  // Fix #7 (2026-07-07): detect NaN in solver inputs before IPOPT call.
  // Invalid_Number_Detected at iter=0 means IPOPT aborts immediately because
  // the initial function/gradient evaluation returns NaN. Log which parameter
  // slot is NaN so the root cause (M4 heading box, M2 target, route frame,
  // or prefix reprojection) is identifiable from the telemetry alone.
  {
    bool p_nan = false;
    for (casadi_int i = 0; i < p_val.size1(); ++i) {
      if (!std::isfinite(static_cast<double>(p_val(i)))) {
        spdlog::warn("[M5][MidMPC] NaN/Inf in p_val[{}] = {}", i,
                     static_cast<double>(p_val(i)));
        p_nan = true;
      }
    }
    if (p_nan) {
      spdlog::warn("[M5][MidMPC] p_val has NaN/Inf — solver will fail with Invalid_Number_Detected");
    }
    bool x0_nan = false;
    for (casadi_int i = 0; i < x0_val.size1(); ++i) {
      if (!std::isfinite(static_cast<double>(x0_val(i)))) {
        spdlog::warn("[M5][MidMPC] NaN/Inf in x0_val[{}] = {}", i,
                     static_cast<double>(x0_val(i)));
        x0_nan = true;
      }
    }
    if (x0_nan) {
      spdlog::warn("[M5][MidMPC] x0_val has NaN/Inf — solver will fail with Invalid_Number_Detected");
    }
  }

  const casadi::DMDict arg = {
      {"x0",  x0_val},
      {"p",   p_val},
      {"lbx", lbx},
      {"ubx", ubx},
      {"lbg", lbg},
      {"ubg", ubg},
  };

  casadi::DMDict res;
  try {
    res = formulation_.solver()(arg);
  } catch (const std::exception& e) {
    const auto t_ex = std::chrono::steady_clock::now();
    spdlog::error("[M5][MidMPC] IPOPT threw: {}", e.what());
    ++consecutive_failures_;
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
    MidMpcSolution fail;
    fail.status = SolveStatus::NumericalFailure;
    fail.solve_duration_ms = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t_ex - t_start).count());
    return fail;
  }

  const casadi::Dict stats = formulation_.solver().stats();
  const auto t_end = std::chrono::steady_clock::now();
  const int32_t duration_ms = static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

  MidMpcSolution sol = formulation_.unpack_solution(res.at("x"), stats);
  sol.solve_duration_ms = duration_ms;

  if (sol.status == SolveStatus::Converged) {
    consecutive_failures_ = 0;
  } else {
    ++consecutive_failures_;
    if (sol.status == SolveStatus::Infeasible) {
      // FM-2: collision unavoidable — M7 MRM-02 expected (detailed design §7.1).
      spdlog::critical("[M5][MidMPC] Infeasible: collision unavoidable; M7 MRM expected");
    }
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
  }
  return sol;
}

// v2.1 spec §4.2/§4.3: derive k_minalt + k_cpa from input. Returns a cfg with
// override_valid=false for the v2.1 fields; solve() merges caller row_bounds
// on top (caller fields with override_valid=true win; K/colreg_prefix_softened
// keep their existing caller-precedence from the original solve() block).
RowBoundConfig derive_row_bound_config(
    const MidMpcInput& input,
    int32_t n_horizon,
    double dt_s) {
  RowBoundConfig cfg;
  // Replicate the EXISTING direction/terminal derivation (mid_mpc_solver.cpp
  // pre-Task-8 lines 190-211): give-way lateral active ↔ role∈{1,2} AND pref_dir
  // ∈ {Starboard,Port} AND not Hold/ReduceSpeed.
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

  // v2.2 §4.2/§4.6: k_minalt = max(ROT-reach deadline, box-reach 检查).
  // ROT-reach: k_minalt_rot = ceil(min_alt / rot_step) - 1 (v2.1 公式).
  // Box-reach: 若 M4 publish 合约 (heading_box_reachable_from_psi0_deg > 0):
  //   - box_reach ≥ min_alt → box 不限制, k_minalt = k_minalt_rot
  //   - box_reach < min_alt → box upper 不可达 min_alt, minalt_box_infeasible=true,
  //     k_minalt = N (全 soft, 让 NLP 尝试但预期 infeasible, §13.1 dispatch)
  // M4 未升级 (sentinel=0) → 退化 v2.1 ROT-only 公式.
  // rot_step = rot_max_rad_s · dt. Only lateral give-way needs the schedule;
  // stand-on/HOLD/ReduceSpeed are direction_disabled above (apply_direction_disable_
  // double-disables min_alt rows, schedule is a no-op).
  if (!cfg.direction_disabled) {
    const double rot_step = input.rot_max_rad_s * dt_s;
    if (rot_step > 1e-9) {
      const int32_t k_minalt_rot = static_cast<int32_t>(
          std::ceil(input.colregs_min_alteration_rad / rot_step)) - 1;
      const int32_t k_minalt_rot_clamped = std::max(0, std::min(k_minalt_rot, n_horizon));

      const double box_reach_deg =
          input.constraints.heading_box_reachable_from_psi0_deg;
      if (box_reach_deg > 0.0) {  // M4 publish 合约
        const double box_reach_rad = box_reach_deg * units::kRadPerDeg;
        // Codex β review 🟡4: epsilon tolerance for M4/M5 float32(deg)→float64(rad)
        // conversion noise. M4 publishes the directional reach as a float32 deg
        // field; M5 converts to rad (float64). The independent deg↔rad paths
        // accumulate ~0.02° error at the boundary, and a strict < test would
        // flag a box that is genuinely reachable as infeasible (false INFEAS →
        // §13.1 BC-MPC dispatch). ~0.005 rad ≈ 0.3° absorbs the noise while
        // staying well above ROT precision and below any real min_alt margin.
        constexpr double kBoxReachEpsilonRad = 0.005;
        if (box_reach_rad < input.colregs_min_alteration_rad - kBoxReachEpsilonRad) {
          cfg.minalt_box_infeasible = true;
          cfg.minalt_hard_from_k = n_horizon;  // 全 soft
        } else {
          cfg.minalt_hard_from_k = k_minalt_rot_clamped;
        }
      } else {
        // M4 未升级 sentinel: 退化 v2.1 ROT-only
        cfg.minalt_hard_from_k = k_minalt_rot_clamped;
      }
    }
  }

  // v2.1 §4.4 reachable schedule (Phase 1 root-cause fix): direction row at k=0
  // is g_dir[0] = pref_dir · l[0], where l[0] = (own_pos - route_origin) · n_hat
  // is the OWN SHIP current cross-track. pos[0] is the NLP initial condition
  // (parameter), so the NLP cannot change l[0] — if the ship starts on the
  // "wrong" side relative to M6's chosen give-way side (a perfectly valid
  // scenario, e.g. rule14-ho starts ~1 m left of route, M6 picks Starboard),
  // g_dir[0] < 0 is an immovable HARD VIOLATION.
  //
  // spec §4.4 explicitly authorizes this soften: "若有非 0 残差且活跃 → 下个会话
  // 软化（同 min_alt reachable schedule 模式）". Same geometric reasoning as
  // min_alt: the ship needs k_dir steps of lateral closure (rate ≈ u·sin(rot_step)
  // · dt) before the cross-track sign can flip. Soften the early rows so J_colreg
  // drives the maneuver; hard floor returns once the ship has had physical time to
  // reach the preferred side.
  if (!cfg.direction_disabled) {
    // l[0] = (own_pos - route_origin) · n_hat. MidMpcInput already exposes this
    // as route_xte_m (the node computes it from the same route frame); sign
    // convention: positive = starboard of route (n_hat packed as (-sin,cos),
    // see formulation.cpp:113). pref_dir=+1=Starboard, -1=Port.
    //
    // g_dir[0] = pref_dir · l[0]. The NLP cannot move pos[0] (own current pos),
    // so g_dir[0] < 0 is an immovable HARD VIOLATION only when l[0] is on the
    // OPPOSITE side of pref_dir (wrong side: pref_dir·l[0] < 0). If the ship is
    // already on the correct side (pref_dir·l[0] > 0), direction is satisfied
    // and no softening is needed — applying it anyway would over-soften hard
    // rows beyond what spec §4.4 authorizes ("non-zero residual" = wrong side).
    const double pref_dir_sign =
        (input.colregs_preferred_direction ==
            mass_l3::m5::ColregsPreferredDirection::Starboard) ? +1.0
      : (input.colregs_preferred_direction ==
            mass_l3::m5::ColregsPreferredDirection::Port)        ? -1.0
                                                                  :  0.0;
    const double g_dir0 = pref_dir_sign * input.route_xte_m;
    const double rot_step = input.rot_max_rad_s * dt_s;
    // Worst-case closure rate at the first reachable heading (one ROT step from
    // own_psi). Floor u to 0.5 so a near-stationary ship does not divide by ~0.
    const double u_eff = std::max(input.own_ship.u_mps, 0.5);
    const double closure_rate = u_eff * dt_s * std::sin(rot_step);
    if (g_dir0 < -1e-6 && closure_rate > 1e-6) {
      // Wrong-side violation: soften the first k_dir rows until closure can
      // flip the cross-track sign into the preferred side.
      const int32_t k_dir = static_cast<int32_t>(
          std::ceil(std::fabs(g_dir0) / closure_rate));
      cfg.direction_hard_from_k = std::max(0, std::min(k_dir, n_horizon));
    } else {
      // Already on the correct side (or on the route line): all-hard.
      cfg.direction_hard_from_k = 0;
    }
    // If closure_rate <= eps (zero ROT / zero speed): leave default 0 (all-hard);
    // solver will report Infeasible correctly (true geometric impossibility).
  }

  // v2.1 §4.3 k_cpa = max(k_minalt, k_tcpa_margin), where
  //   k_tcpa_margin = ceil(min(tcpa_primary, t_cap)/dt) - 1
  // Spec §4.3 B8-r2: if all targets have tcpa_s <= 0 (already past) or no
  // targets, derive fails -> conservative default cpa_hard_from_k = k_minalt
  // (mirrors min_alt reachability floor; avoids v2 legacy 0 which would over-
  // harden when min_alt itself is reachable). When targets exist but all tcpa
  // <= 0, fall back to cpa_hard_from_k = 0 (v2 legacy all-hard) per spec §4.3.
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
    // Lateral give-way but no targets: CPA floor moot, mirror min_alt deadline.
    cfg.cpa_hard_from_k = cfg.minalt_hard_from_k;
  }

  // Phase 3.2 (spec v2.3 §2.5): initial-condition relax. k=0/1 are always
  // soft. range(0) is an NLP-immovable initial condition; making it hard
  // forces Infeasible whenever the target is already inside cpa_hard at k=0
  // (a common rule14-ho close-range geometry). With σ now in the constraint
  // this is belt-and-suspenders, but it keeps σ clean (σ does not need to
  // absorb initial-condition violations).
  constexpr int32_t k_initial_relax = 2;
  if (!cfg.direction_disabled) {
    cfg.cpa_hard_from_k = std::max(cfg.cpa_hard_from_k, k_initial_relax);
  }

  // Phase 3.3 (spec v2.3 §2.6): geometric reach floor. When the target is
  // inside cpa_hard, estimate how many steps the own-ship needs to physically
  // reach cpa_hard at the current closing rate. Hard rows beyond this floor
  // are reachable; hard rows before it are not. σ remains the global
  // feasibility preserver behind this schedule.
  if (!cfg.direction_disabled && !input.targets.empty()) {
    const double cpa_hard_m = input.constraints.cpa_hard_m;
    double min_inside_deficit_m = 0.0;  // 0 when target already outside floor
    double max_closing_rate = 0.0;
    for (const auto& t : input.targets) {
      const double range_m = std::hypot(t.x_m, t.y_m);
      if (range_m < cpa_hard_m) {
        const double deficit = cpa_hard_m - range_m;
        if (deficit > min_inside_deficit_m) {
          min_inside_deficit_m = deficit;
        }
      }
      const double sog_mps = std::fabs(t.sog_mps);
      if (sog_mps > max_closing_rate) {
        max_closing_rate = sog_mps;
      }
    }
    if (min_inside_deficit_m > 0.0 && max_closing_rate > 0.01) {
      const int32_t geom_reach_k = static_cast<int32_t>(
          std::ceil(min_inside_deficit_m / (max_closing_rate * dt_s)));
      cfg.cpa_hard_from_k = std::max(
          cfg.cpa_hard_from_k, std::min(geom_reach_k, n_horizon));
    }
  }
  return cfg;
}

}  // namespace mass_l3::m5::mid_mpc
