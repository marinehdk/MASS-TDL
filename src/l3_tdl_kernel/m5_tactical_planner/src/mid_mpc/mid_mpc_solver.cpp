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
  casadi::DM lbx = casadi::DM::zeros(2 * N, 1);
  casadi::DM ubx = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    lbx(k)     = cst.heading_min_rad;
    ubx(k)     = cst.heading_max_rad;
    lbx(N + k) = cst.speed_min_mps;
    ubx(N + k) = cst.speed_max_mps;
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

  // v2.1 spec §4.4 / C3: implementation-only diag dump for direction rows.
  // Activated ONLY when env var M5_DIRECTION_DIAG=1. Default off. Remove after
  // direction-soften decision (Task 10 probe evidence). Uses res.at("g")
  // (CasADi nlpsol standard output, sibling to res.at("x")). Placed before
  // unpack_solution so the dump runs even on infeasible exits (g(x*) is still
  // returned by IPOPT on Infeasible).
  if (const char* env = std::getenv("M5_DIRECTION_DIAG")) {
    if (env[0] == '1') {
      const casadi::DM& g_at_sol = res.at("g");
      const auto& reg = formulation_.row_registry();
      const int32_t N_diag = formulation_.config().n_horizon;
      for (int32_t k = 0; k < N_diag; ++k) {
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

  // v2.1 §4.2 k_minalt = ceil(min_alt/rot_step) - 1, clamped [0, N].
  // rot_step = rot_max_rad_s · dt. Only lateral give-way needs the schedule;
  // stand-on/HOLD/ReduceSpeed are direction_disabled above (apply_direction_disable_
  // double-disables min_alt rows, schedule is a no-op).
  if (!cfg.direction_disabled) {
    const double rot_step = input.rot_max_rad_s * dt_s;
    if (rot_step > 1e-9) {
      const int32_t k_minalt = static_cast<int32_t>(
          std::ceil(input.colregs_min_alteration_rad / rot_step)) - 1;
      cfg.minalt_hard_from_k = std::max(0, std::min(k_minalt, n_horizon));
    }
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
  return cfg;
}

}  // namespace mass_l3::m5::mid_mpc
