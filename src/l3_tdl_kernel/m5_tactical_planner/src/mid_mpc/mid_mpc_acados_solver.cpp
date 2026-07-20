// M5 Tactical Planner — Mid-MPC production acados solver wrapper (Task 17).
//
// Wraps the generated acados OCP solver lib (T15 codegen + T16 CMake) behind the
// SAME MidMpcSolution output contract as the IPOPT MidMpcSolver. The downstream
// (M4/L4/tail_gate) is agnostic to the backend switch — MidMpcSolver::solve()
// dispatches to this wrapper when M5_USE_ACADOS is defined and acados_solver_ is
// non-null, otherwise it falls through to the existing IPOPT path UNCHANGED.
//
// acados C lib 2-Clause BSD; internal MISRA violations exempted per coding-
// standards.md §10 (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

// P2 T4 (VR-07b): per-stage t_b closest-point computation. CasADi-free free
// function that calls the T1 project_to_segment on the F1 seed positions; the
// result fills the per-stage tb_x/tb_y slots the route COST (T3) and terminal
// COST (T4) read as the lateral-deviation origin.
#include "m5_tactical_planner/mid_mpc/mid_mpc_per_stage_tb.hpp"

// CasADi (MX/Function) for the C1 status-4 constraint-satisfaction re-check
// (T17 review-fix C1): the check recomputes the constraint residuals h(x,u,p)
// from the SAME MX expression the acados codegen derives from
// (MidMpcAcadosFormulation::con_h_expr), evaluated independently on the solved
// trajectory. This is constraint CLARIFICATION (independent read-back), not a
// new constraint. CasADi is already a hard dependency via the formulation.
#include <casadi/casadi.hpp>

// acados C API (generated header chain transitively pulls acados_c/ocp_nlp_interface.h
// + acados/utils/types.h; include dirs wired by T16 CMake under M5_HAS_ACADOS).
#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_mid_mpc_acados.h"

namespace mass_l3::m5::mid_mpc {

// ---------------------------------------------------------------------------
// Generated-solver dimension constants (mirror acados_solver_m5_mid_mpc_acados.h).
// Kept as local constexprs (NOT macros) so the wrapper source stays typed.
// ---------------------------------------------------------------------------
namespace {
constexpr int kAcadosNx  = M5_MID_MPC_ACADOS_NX;    // 5 ([px,py,psi,r,u_surge])
constexpr int kAcadosNu  = M5_MID_MPC_ACADOS_NU;    // 2 ([delta,n])
constexpr int kAcadosN   = M5_MID_MPC_ACADOS_N;     // 18 (production horizon)
constexpr int kAcadosNp  = M5_MID_MPC_ACADOS_NP;    // 211 (155 global + 56 per-stage,
                                                      //      56 = 3 prefix + 32 target drift
                                                      //      + 2 tb_x/tb_y + 16 σ_pos (P7)
                                                      //      + 2 psi_prev/u_prev + 1 w_trans_active (P5 T2))
// Step5 方案 B (VR-01 final): nsh=0 (no slack). The codegen script no longer
// sets ocp.constraints.idxsh, so the regenerated header will define
// M5_MID_MPC_ACADOS_NSH=0 / M5_MID_MPC_ACADOS_NS=0. The OLD generated header
// still in tree (NSH=16/NS=16) is STALE pending codegen re-run; the wrapper
// logic below is nsh-tolerant (it reads "sl" only when kAcadosNsh > 0).
//
// IMPORTANT: the row LAYOUT (nh, kRowDirection, kRowMinAlt) depends on the
// target count Nt=16, NOT on nsh. Pre-Step5 these happened to be equal (both
// 16), so the code aliased them. Step5 方案 B decouples them: kAcadosNt is the
// TARGET count (16, formulation constant); kAcadosNsh is the SLACK count
// (0 after codegen re-run). Row offsets use kAcadosNt; slack reads use
// kAcadosNsh. This makes the layout stable across the nsh=0 transition.
constexpr int kAcadosNt  = 16;  // target count (layout-determining; matches kAcadosMaxTargets in formulation.hpp)
constexpr int kAcadosNsh = M5_MID_MPC_ACADOS_NSH;   // 0 after Step5 方案 B codegen re-run (was 16)
constexpr int kAcadosNs  = M5_MID_MPC_ACADOS_NS;    // 0 after Step5 方案 B codegen re-run (was 16)
constexpr int kAcadosNh  = M5_MID_MPC_ACADOS_NH;    // 20 (P4: abolished terminal C10/C11, was 23)

// Row-class offsets in con_h (mirror MidMpcAcadosFormulation::build_con_h_ and
// gen_mid_mpc_acados.py). FIXED order; the codegen emits the same layout. These
// are the indices the solver wrapper writes to when it sets per-stage lh/uh to
// mirror IPOPT's derive_row_bound_config (deactivate direction/min_alt
// for non-lateral scenarios).
//   [0]      prefix_psi      (equality via pact_pre activation factor)
//   [1]      prefix_u        (equality via pact_pre activation factor)
//   [2..17]  CPA per-target  (one-sided >= 0; Step5 方案 B: TRUE hard, nsh=0 no slack)
//   [18]     direction       (pref_dir * l_k)
//   [19]     min_alt         (pref_dir * (psi - own_psi) - min_alt_par)
// Step5 方案 B: row offsets use kAcadosNt (16, target count), NOT kAcadosNsh
// (0 after codegen). This keeps the layout stable across the nsh=0 transition.
constexpr int kRowPrefixPsi = 0;
constexpr int kRowPrefixU   = 1;
constexpr int kRowCpaBase   = 2;                    // CPA rows [2..2+Nt-1]
constexpr int kRowDirection = 2 + kAcadosNt;        // 18 (Nt=16)
constexpr int kRowMinAlt    = 2 + kAcadosNt + 1;    // 19
static_assert(kRowMinAlt == kAcadosNh - 1,
              "row offset layout must match build_con_h_ + gen script");

// F5 solver-moved tolerance: reject a status-4 that returns the seed unchanged
// (T3 lesson — a status-4 on the first QP can leave the seed in place; that is
// NOT a converged solution). Same value as staging runner_merge6.
constexpr double kTrajDeltaTol = 1.0e-6;

// Box slack tolerances (mirrors staging; box violations are fatal evidence of a
// divergent iterate, NOT a convergence wiggle).
constexpr double kBoxTol = 1.0e-6;

// Map the acados integer solver status to MidMpcSolution::Status. The mapping is
// the F5 contract: status 0 = Converged; status 4 (QP error during refinement)
// is tolerated (caller verifies solver-moved + constraints satisfied after the
// fact); status 1 = max_iter (Timeout); status 2/3 = infeasible/numerical.
// The full mapping is documented in the header; this helper does NOT auto-pass
// status 4 — the caller applies the solver-moved gate before accepting it.
MidMpcSolution::Status map_acados_status(int status) {
  switch (status) {
    case 0:  return MidMpcSolution::Status::Converged;        // ACADOS_SUCCESS
    case 1:  return MidMpcSolution::Status::Timeout;          // max_iter hit
    case 2:  return MidMpcSolution::Status::Infeasible;       // QP infeasible
    case 3:  return MidMpcSolution::Status::NumericalFailure; // QP solver failed
    case 4:  return MidMpcSolution::Status::NumericalFailure; // QP error during
                                                              // refinement — caller
                                                              // re-maps to Converged
                                                              // iff solver-moved
    default: return MidMpcSolution::Status::NumericalFailure;
  }
}

// bounded pseudo-infinity (F2: t_renderer rejects JSON Infinity; the runtime
// solver bound API accepts a plain double, so 1e10 mirrors the codegen uh).
constexpr double kUhInf = 1.0e10;

// DP-03 / VR-03 b' conservative factor (BL-B finding: ROT-reach surrogate is
// ~5x more optimistic than MMG oracle). Dividing rot_step by this factor makes
// the effective yaw rate slower, so k_minalt requires more stages — preventing
// the solver from believing it can turn faster than physics allows.
// Default 2.0: covers a 2x surrogate gap with margin. Calibrated offline via
// MMG oracle comparison (live 4.78x, benchmark 6.01x, target2500 3.57x from
// reference_oracle.json [R22]). The factor is speed-dependent per BL-B;
// a single conservative default is a safe starting point until a speed table
// is calibrated via HAZID RUN-001.
constexpr double kSurrogateFudgeFactor = 2.0;

// Build the per-stage lh/uh vectors mirroring IPOPT's derive_row_bound_config.
// The acatos graph emits a FIXED nh=23 rows per stage (single-stage graph; IPOPT
// has a dynamic row count). Rows that IPOPT disables per-scenario (direction /
// min_alt / terminal for non-lateral scenarios) MUST be relaxed to [-inf,+inf]
// here, otherwise they make the NLP infeasible (e.g. g_term_side = pref_dir*l_k
// - l_min = -30 < 0 when pref_dir=0 / Hold — the standard straight-line case).
//
// Activation condition (mirrors mid_mpc_solver.cpp derive_row_bound_config):
//   lateral_colreg_active = role∈{1,2} (give-way) AND pref_dir∈{Starboard,Port}
//                           AND behavior not Hold/ReduceSpeed.
// When inactive (stand-on / Hold / ReduceSpeed): direction/min_alt/terminal rows
// are double-disabled [-kUhInf, +kUhInf] at EVERY stage. When active: they stay
// one-sided >= 0 (the codegen default), matching IPOPT's lateral-give-way path.
//
// Terminal rows (20,21,22): applied at the TERMINAL stage N only when active
// (matches IPOPT's terminal-only evaluation). Non-terminal stages relax them to
// [-kUhInf, +kUhInf]. This is the per-stage masking the gen script comment
// described ("non-terminal stages masked") but the codegen applied stage-uniform
// bounds — the solver wrapper enforces the per-stage mask here.
//
// CPA rows (2..17): one-sided >= 0 for the n_targets REAL targets (softened
// via idxsh). Empty target slots [n_targets..max_targets-1] are RELAXED to
// [-kUhInf,+kUhInf] — this mirrors IPOPT exactly (build_constraints_ emits CPA
// rows ONLY for constraint_inputs_.targets.size() real targets; empty slots are
// absent from the constraint vector). The acatos single-stage graph cannot omit
// rows, so the solver wrapper deactivates them via the bound. Using a huge
// placeholder target position to trivially-satisfy the row is NOT equivalent:
// the 1e14-scale residual poisons the EXACT-hessian KKT conditioning when other
// rows (prefix equality) are active (CASE B divergence, isolated 2026-07-17).
// Relaxing the bound is the faithful translation of "no row present".
struct RowBounds {
  std::vector<double> lh;  // length nh
  std::vector<double> uh;  // length nh
};

RowBounds build_stage_row_bounds(int nh, bool lateral_active,
                                 int n_targets, int stage_K, int prefix_K,
                                 const ReachabilitySchedule& sched) {
  // I-4 (P4 T7): colreg_prefix_softened — relax CPA rows for prefix stages (k<K)
  // since the prefix geometry is frozen and CPA on frozen trajectory is handled
  // by the committed prefix, not the current NLP iteration. Mirrors IPOPT's
  // colreg_prefix_softened = true behavior.
  const bool prefix_stage = (prefix_K > 0) && (stage_K < prefix_K);
  std::vector<double> lh(static_cast<std::size_t>(nh), 0.0);
  std::vector<double> uh(static_cast<std::size_t>(nh), kUhInf);
  // Helper: write a row by INTEGER offset (cast to size_type at the boundary
  // to satisfy -Werror=sign-conversion on std::vector::operator[]).
  auto set_row_value = [&](int idx, double lo, double hi) {
    const std::size_t i = static_cast<std::size_t>(idx);
    lh[i] = lo;
    uh[i] = hi;
  };
  // Prefix rows: equality [0,0] (pact_pre deactivates inside the expression).
  set_row_value(kRowPrefixPsi, 0.0, 0.0);
  set_row_value(kRowPrefixU,   0.0, 0.0);
  // CPA rows [2..17]: three-phase hardening (item ② DP-07):
  //   Phase 1 (commit, k < prefix_K):    CPA relaxed — prefix frozen geometry
  //   Phase 2 (soften, k < k_cpa_suffix): CPA relaxed — allow solver to
  //     maneuver out of prefix-exit CPA violation before hard floor
  //   Phase 3 (hard,  k >= k_cpa_suffix): [0,+inf] — CPA floor enforced
  // Empty target slots [n_targets..max_targets-1] are always relaxed
  // (mirror IPOPT, which emits CPA rows only for real targets).
  const bool cpa_soft = (prefix_stage || stage_K < sched.k_cpa_suffix);
  const int n_t = std::max(0, std::min(n_targets, kAcadosNt));
  for (int t = 0; t < kAcadosNt; ++t) {
    if (t >= n_t || cpa_soft) {
      set_row_value(kRowCpaBase + t, -kUhInf, kUhInf);   // relaxed (phase 1 or 2)
    } else {
      set_row_value(kRowCpaBase + t, 0.0, kUhInf);       // hard floor (phase 3)
    }
  }
  // Direction / min_alt: deactivate unless lateral_active. Additionally,
  // reachability schedule delays hardening: min_alt active only at
  // stage_K >= sched.k_minalt; heading-box/direction active only at
  // stage_K >= sched.k_head_earliest
  // (L1b: acados-path reachability schedule, mirroring IPOPT's
  // derive_row_bound_config §4.2/§4.6).
  const bool minalt_active  = lateral_active && (stage_K >= sched.k_minalt);
  const bool heading_active = lateral_active && (stage_K >= sched.k_head_earliest);
  auto set_row = [&](int idx, bool active) {
    if (active) {
      set_row_value(idx, 0.0, kUhInf);        // one-sided >= 0
    } else {
      set_row_value(idx, -kUhInf, kUhInf);    // double-disabled
    }
  };
  set_row(kRowDirection, heading_active);  // direction follows heading schedule
  set_row(kRowMinAlt,    minalt_active);
  return {std::move(lh), std::move(uh)};
}

	// Derive lateral_colreg_active from the input (mirrors derive_row_bound_config
// in mid_mpc_solver.cpp:343-354). Same condition the IPOPT path uses to decide
// whether direction/terminal rows are enabled.
bool derive_lateral_active(const MidMpcInput& input) {
  const bool pref_active =
      (input.colregs_preferred_direction == ColregsPreferredDirection::Starboard ||
       input.colregs_preferred_direction == ColregsPreferredDirection::Port);
  const bool give_way_role =
      (input.colregs_primary_role == 1U || input.colregs_primary_role == 2U);
  const bool lateral_behavior =
      (input.colregs_preferred_direction != ColregsPreferredDirection::Hold &&
       input.colregs_preferred_direction != ColregsPreferredDirection::ReduceSpeed);
  return give_way_role && pref_active && lateral_behavior;
}

// L1b reachability schedule (mirrors IPOPT derive_row_bound_config §4.2/§4.6).
// Determines per-cycle constraint-hardening windows for min_alt, heading-box/
// direction, and CPA floors, based on the vessel's yaw dynamics, M4 box-reach
// contract, and target TCPA geometry.
//
//   k_minalt:        earliest stage where min_alt constraint is active (ROT-reach)
//   k_head_earliest: earliest stage where direction constraint is active (box-reach)
//   k_head_latest:   latest stage by which heading MUST be active (CPA deadline)
//   k_cpa_suffix:    earliest stage where CPA floor is active (TCPA-based)
//   minalt_box_infeasible: true if M4 heading box is too tight for min_alt
//   direction_wrong_side:  true if own ship starts on opposite side of pref_dir
//
// Stages before k_minalt/k_head_earliest have those rows relaxed. If
// k_head_earliest > k_head_latest, the ship physically cannot turn before the
// CPA safety deadline — a diagnostic warning is emitted but the solve proceeds.
// The CPA suffix hardening (k_cpa_suffix) is computed here as a function of
// k_minalt + TCPA margin; the actual RowBoundConfig compositing with prefix_K
// and the multi-phase (commit→soften→harden) schedule is the responsibility
// of item ② (DP-07).

ReachabilitySchedule compute_reachability_schedule(
    const MidMpcInput& input, double dt_s) {
  ReachabilitySchedule sched{};
  sched.k_head_latest = kAcadosN;   // default: no CPA deadline
  sched.k_cpa_suffix  = kAcadosN;   // default: suffix always soft

  // Only lateral give-way scenarios need the schedule (stand-on/HOLD/ReduceSpeed
  // have direction_disabled, so min_alt and direction rows are already
  // double-disabled by build_stage_row_bounds — schedule is a no-op).
  const bool need_schedule = derive_lateral_active(input);
  if (!need_schedule) {
    return sched;
  }

  const double rot_max = input.rot_max_rad_s;
  const double rot_step = rot_max * dt_s;
  // DP-03 b' (VR-03): conservative effective rot_step for k_minalt.
  // Divide by kSurrogateFudgeFactor to account for surrogate-vs-MMG gap (~5x
  // per BL-B). k_head_earliest/k_head_latest use the RAW rot_step (they govern
  // box-reach/direction, not min_alt — the MMG gap finding was specific to
  // the min_alt ROT-reach formula; box-reach uses a different geometric
  // reasoning where the surrogate is more faithful).
  const double bprime_rot_step = rot_step / kSurrogateFudgeFactor;

  // ---------------------------------------------------------------------------
  // 1. k_minalt: ROT-reach for min_alt, with box-infeasible detection.
  //    Mirrors IPOPT derive_row_bound_config §4.2/§4.6 (lines 441-471).
  // ---------------------------------------------------------------------------
  if (rot_step > 1e-9) {
    // Base ROT-reach formula (DP-03 b'): ceil(min_alt / bprime_rot_step) - 1
    // using conservative rot_step to account for MMG surrogate gap.
    const double min_alt = input.colregs_min_alteration_rad;
    if (min_alt > 0.0) {
      const int k_minalt_rot = static_cast<int>(
          std::ceil(min_alt / bprime_rot_step)) - 1;
      int k_minalt_raw = std::max(0, std::min(k_minalt_rot, kAcadosN));

      // Box-reach check (v2.2 §4.6): if M4 published heading_box_reachable
      // and it is smaller than min_alt, the heading box physically limits
      // the ship from ever reaching min_alt → minalt_box_infeasible.
      const double box_reach_deg =
          input.constraints.heading_box_reachable_from_psi0_deg;
      if (box_reach_deg > 0.0) {
        const double box_reach_rad = box_reach_deg * units::kRadPerDeg;
        // Epsilon tolerance for M4/M5 float32→float64 conversion noise (~0.005 rad ≈ 0.3°)
        constexpr double kBoxReachEpsilonRad = 0.005;
        if (box_reach_rad < min_alt - kBoxReachEpsilonRad) {
          sched.minalt_box_infeasible = true;
          k_minalt_raw = kAcadosN;  // full soft — §13.1 BC-MPC dispatch signal
        }
      }
	      sched.k_minalt = k_minalt_raw;
	      // DP-03 oracle cross-check (VR-03): compare b'-corrected k_minalt against
	      // the raw (uncorrected) value. When the MMG oracle covers k_minalt_rot,
	      // this diagnostic logs the b' factor impact. In CI, this can be asserted:
	      //   k_minalt_bprime >= k_minalt_oracle / kSurrogateFudgeFactor
	      // The raw k_minalt (without b') is k_minalt_rot; the corrected is k_minalt.
	      if (k_minalt_rot != k_minalt_raw) {
	        spdlog::debug("[M5][MidMPC][DP-03] k_minalt: raw={} bprime={} "
	                      "(factor={}) box_infeasible={}",
	                      k_minalt_rot, k_minalt_raw,
	                      kSurrogateFudgeFactor, sched.minalt_box_infeasible);
	      }
	    }

    // -------------------------------------------------------------------------
    // 2. k_head_earliest: heading-box / direction reachability.
    //    Mirrors IPOPT derive_row_bound_config §4.4 (lines 487-523).
    // -------------------------------------------------------------------------
    // If M4 published heading_box_reachable, use it. Falls back to min_alt
    // when M4 sentinel = 0 (conservative: heading not proven reachable).
    const double box_reach_deg =
        input.constraints.heading_box_reachable_from_psi0_deg;
    if (box_reach_deg > 0.0) {
      const double box_reach_rad = box_reach_deg * units::kRadPerDeg;
      const int k_head_raw = static_cast<int>(
          std::ceil(box_reach_rad / rot_step)) - 1;
      sched.k_head_earliest = std::max(0, std::min(k_head_raw, kAcadosN));
    } else {
      // M4 did not publish heading_box_reachable (sentinel=0): use min_alt as
      // a conservative proxy — heading not proven reachable until the ship can
      // achieve min_alt (which is itself a necessary condition for any COLREGs
      // maneuver). Use bprime_rot_step (DP-03) for the same MMG-surrogate gap
      // safety margin as k_minalt.
      if (min_alt > 0.0) {
        const int k_head_raw = static_cast<int>(
            std::ceil(min_alt / bprime_rot_step)) - 1;
        sched.k_head_earliest = std::max(0, std::min(k_head_raw, kAcadosN));
      }
    }

    // Direction wrong-side detection (IPOPT §4.4, lines 487-523).
    // If own ship starts on the opposite side of the preferred direction,
    // g_dir[0] = pref_dir · XTE < 0 is an immovable HARD violation at stage 0.
    // We need k_dir stages of lateral closure to flip the sign.
    const double pref_dir_sign =
        (input.colregs_preferred_direction ==
            ColregsPreferredDirection::Starboard) ? +1.0
      : (input.colregs_preferred_direction ==
            ColregsPreferredDirection::Port)        ? -1.0
                                                     :  0.0;
    const double g_dir0 = pref_dir_sign * input.route_xte_m;
    // Worst-case closure rate at the first reachable heading (one ROT step).
    // Floor speed to 0.5 m/s to avoid division by ~0 for near-stationary ships.
    const double u_eff = std::max(input.own_ship.u_mps, 0.5);
    const double closure_rate = u_eff * dt_s * std::sin(rot_step);
    if (g_dir0 < -1e-6 && closure_rate > 1e-6) {
      sched.direction_wrong_side = true;
      const int k_dir = static_cast<int>(
          std::ceil(std::fabs(g_dir0) / closure_rate));
      // If wrong-side, direction hardening must wait at least k_dir stages,
      // so k_head_earliest is at least k_dir (max with existing box-based value).
      sched.k_head_earliest = std::max(sched.k_head_earliest,
          std::max(0, std::min(k_dir, kAcadosN)));
    }

    // -------------------------------------------------------------------------
    // 3. k_head_latest: t_latest_safe (CPA-based ample-time bound).
    //    Latest stage by which heading MUST be achieved for safe CPA.
    //    Mirrors IPOPT derive_row_bound_config §4.3 (lines 526-546).
    // -------------------------------------------------------------------------
    if (!input.targets.empty()) {
      double tcpa_min = std::numeric_limits<double>::infinity();
      for (const auto& t : input.targets) {
        if (t.tcpa_s > 0.0) {
          tcpa_min = std::min(tcpa_min, t.tcpa_s);
        }
      }
      if (std::isfinite(tcpa_min)) {
        // Safety margin: 2 stages for residual maneuver time after heading
        // is achieved (ship may need additional time to open CPA).
        constexpr int kMarginStages = 2;
        const double t_cap = static_cast<double>(kAcadosN) * dt_s;
        const double tcpa_eff = std::min(tcpa_min, t_cap);
        int k_tcpa = static_cast<int>(std::ceil(tcpa_eff / dt_s)) - 1;
        k_tcpa = std::max(0, std::min(k_tcpa, kAcadosN));
        sched.k_head_latest = std::max(0, k_tcpa - kMarginStages);

        // ---------------------------------------------------------------------
        // 4. k_cpa_suffix: CPA suffix-hard deadline (item ② DP-07).
        //    Three-phase: commit(k<prefix_K)→soften→hard(k>=k_cpa_suffix).
        //    CPA floor hardens at max(k_minalt, k_head_earliest, k_tcpa):
        //      - k_minalt: need min_alt rotation before CPA meaningful
        //      - k_head_earliest: heading must be on preferred direction before
        //        CPA can be enforced (BL-12 coupling)
        //      - k_tcpa: TCPA-based deadline
        // ---------------------------------------------------------------------
        sched.k_cpa_suffix = std::max(
            {sched.k_minalt, sched.k_head_earliest, k_tcpa});
      } else {
        // All tcpa_s <= 0 (targets already past) → conservative all-hard.
        sched.k_cpa_suffix = 0;
      }
    }

    // -------------------------------------------------------------------------
    // 5. Window cross-check: k_head_earliest > k_head_latest → warning.
    //    This means the ship physically cannot turn before the CPA deadline.
    //    The solver will naturally find an infeasible or suboptimal solution;
    //    this diagnostic signal is for M7/BC-MPC escalation.
    // -------------------------------------------------------------------------
    if (sched.k_head_earliest > sched.k_head_latest) {
      spdlog::warn(
          "[M5][MidMPC][L1b] heading reachability window violation: "
          "k_head_earliest={} > k_head_latest={} — ship cannot turn before "
          "CPA deadline. minalt_box_infeasible={} direction_wrong_side={}",
          sched.k_head_earliest, sched.k_head_latest,
          sched.minalt_box_infeasible, sched.direction_wrong_side);
    }
  }
  // If rot_step is near-zero (stationary ship), all schedules stay at defaults
  // (earliest=0 active from stage 0, k_head_latest=N, k_cpa_suffix=N).

  return sched;
}

// ===========================================================================
// D1 witness (DP-04, VR-04): independent geometric check — committed prefix
// CPA violation detection.
//
// Check whether the committed prefix trajectory (stages k=0..K-1, where K =
// input.prefix_active_k) violates the hard CPA floor (cpa_hard_m) for ANY
// target. This is a PURELY GEOMETRIC check, independent of the NLP solver's
// output — it uses the committed prefix psi/u values from the input and the
// target state, propagating own position via the same double-integrator
// kinematics.
//
// Rationale: the committed prefix stages are FROZEN geometry (the NLP cannot
// change them — they are equality-pinned). If the frozen geometry puts the
// ship within 1852 m of any target, the current cycle CANNOT produce a safe
// plan, regardless of what the suffix NLP computes. The solver's CPA rows for
// prefix stages are bounds-softened (phases 1-2, DP-07), so the solver may
// report Converged while the prefix geometry is unsafe — this witness catches
// that gap.
//
// Returns: true  — prefix CPA safe (no violation)
//          false — prefix CPA violated → caller should map to
//                  NumericalFailure (NO_SAFE_PLAN → M7 MRM)
//
// This function is a pure geometric oracle: no solver state, no NLP graph,
// no parameter vectors. It reads only from the input struct, making it
// inherently independent of the solver backend.
// ===========================================================================
bool d1_prefix_cpa_witness(const MidMpcInput& input, double dt_s) {
  const int32_t K = input.prefix_active_k;
  if (K <= 0) {
    return true;  // No committed prefix → trivially safe.
  }
  if (input.targets.empty()) {
    return true;  // No targets → no CPA to check.
  }

  // Ensure the prefix trajectory vectors are consistent.
  const std::size_t Ks = static_cast<std::size_t>(K);
  if (input.prefix_psi_rad.size() < Ks ||
      input.prefix_u_mps.size() < Ks) {
    spdlog::warn("[M5][MidMPC][D1] prefix_psi_rad.size()={} < K={} or "
                 "prefix_u_mps.size()={} < K={} — cannot verify; assuming safe",
                 input.prefix_psi_rad.size(), K,
                 input.prefix_u_mps.size(), K);
    return true;  // Conservative: don't trigger false NO_SAFE_PLAN on data gap.
  }

  const double cpa_hard = input.constraints.cpa_hard_m;
  if (cpa_hard <= 0.0) {
    return true;  // No meaningful hard floor configured.
  }

  // Propagate own position through the committed prefix geometry.
  double own_px = input.own_ship.x_m;
  double own_py = input.own_ship.y_m;

  for (int32_t k = 0; k < K; ++k) {
    const double psi_k = input.prefix_psi_rad[static_cast<std::size_t>(k)];
    const double u_k   = input.prefix_u_mps[static_cast<std::size_t>(k)];

    // Move own ship forward one stage (same kinematics as the NLP).
    own_px += u_k * dt_s * std::cos(psi_k);
    own_py += u_k * dt_s * std::sin(psi_k);

    // Check CPA against every target at this stage.
    const double kdt = static_cast<double>(k + 1) * dt_s;  // time elapsed
    for (const auto& tgt : input.targets) {
      // Propagate target position (constant velocity from cog/sog).
      const double tx = tgt.x_m
          + tgt.sog_mps * std::cos(tgt.cog_rad) * kdt;
      const double ty = tgt.y_m
          + tgt.sog_mps * std::sin(tgt.cog_rad) * kdt;

      const double dx = own_px - tx;
      const double dy = own_py - ty;
      const double dist_sq = dx * dx + dy * dy;
      const double cpa_hard_sq = cpa_hard * cpa_hard;

      if (dist_sq < cpa_hard_sq - 1e-6) {
        // Violation: committed prefix stage k places the ship within the
        // hard CPA floor of target tgt.id.
        spdlog::warn(
            "[M5][MidMPC][D1] PREFIX CPA VIOLATION at prefix stage k={} "
            "target_id={}: dist={:.1f} m < cpa_hard={:.1f} m → NO_SAFE_PLAN",
            k, tgt.id, std::sqrt(dist_sq), cpa_hard);
        return false;  // Single violation → entire plan unsafe.
      }
    }
  }

  return true;  // All prefix stages safe.
}

}  // namespace

// ===========================================================================
// Impl — pimpl holding the acatos C handles. Constructor creates the capsule +
// nlp; destructor frees them in the correct order (free before free_capsule).
// ===========================================================================
struct MidMpcAcadosSolver::Impl {
  m5_mid_mpc_acados_solver_capsule* capsule{nullptr};
  ocp_nlp_config* cfg{nullptr};
  ocp_nlp_dims* dims{nullptr};
  ocp_nlp_in* in{nullptr};
  ocp_nlp_out* out{nullptr};
  ocp_nlp_solver* solver{nullptr};
  bool created{false};  // true only after a successful acados_create
  // True while inside warm_up_capsule_: suppresses the non-Converged telemetry
  // warning for the throwaway warm-up solves (the cold-capsule first-solve
  // status=2 is EXPECTED and would mislead operators if logged at production
  // telemetry level).
  bool warm_up{false};
  // TEST-ONLY diagnostic mirrors of the last solve()'s raw signals (T17
  // review-fix Step 1 cold-capsule matrix). Production code does not read
  // these. Exposed to the friend test via MidMpcAcadosSolver::last_raw_*.
  int last_raw_status{-1};
  double last_traj_delta{0.0};
  int last_sqp_iter{-1};
  // Step5 方案 B (FB-2 telemetry remedy): with nsh=0 there is no slack vector
  // to read "soft aspiration (d<cpa_safe=2500) violation degree" from. The
  // constraints_satisfied_ helper recomputes d_min over the solved trajectory
  // (min over stage k and real target t of sqrt(dx^2+dy^2)); solve() lifts
  // these into MidMpcSolution for ASDR/SAT transparency (L4/M7 observability).
  // Kept independent from the cpa_slack field (which is always 0 under nsh=0
  // but retained for IPOPT path + downstream contract stability).
  double last_soft_aspiration_d_min_m{0.0};
  double last_soft_aspiration_violation_m{0.0};
	  // C1 status-4 constraint re-check (T17 review-fix C1): a CasADi Function
	  // built lazily on the FIRST status-4 outcome, wrapping the formulation's
	  // con_h_expr over (x, u, p_global, p_stage). Cached so the per-status-4
	  // re-check does not rebuild the function graph on every call. Null until
	  // first use; stays null if status 4 never occurs (the common case).
	  std::unique_ptr<casadi::Function> h_fn;

	  // P5 T1 (warm-start shift-init): cache the last converged solution for use
	  // as the warm-start seed in the next cycle. Only updated when solve() returns
	  // Converged (status 0). Used by the shift-init logic (block 1a branch) and by
	  // T2 (transition cost per-stage psi_prev/u_prev packing).
	  MidMpcSolution last_converged_solution_;
	  // Number of real targets in the last converged solve. Used for sig_changed
	  // detection (constraint-structure change → fall back to F1 seed).
	  int prev_n_targets_{-1};

  Impl() = default;
  ~Impl() {
    if (created) {
      // acados_free returns a status; we do not propagate (destructor is noop-
      // fail; logging only). free_capsule releases the capsule struct itself.
      const int rc_free = m5_mid_mpc_acados_acados_free(capsule);
      if (rc_free != 0) {
        spdlog::warn("[M5][MidMPC][acados] acados_free returned {}", rc_free);
      }
    }
    if (capsule != nullptr) {
      const int rc_cap = m5_mid_mpc_acados_acados_free_capsule(capsule);
      if (rc_cap != 0) {
        spdlog::warn("[M5][MidMPC][acados] free_capsule returned {}", rc_cap);
      }
      capsule = nullptr;
    }
  }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
};

// ---------------------------------------------------------------------------
// Constructor (public, production) — delegates to the private CtorOpts ctor
// with the default opts (warm-up ENABLED). Production callers have no way to
// reach the skip_warm_up=true path: the CtorOpts ctor is private and only the
// test friend MidMpcAcadosSolverColdCapsuleTest can name it.
// ---------------------------------------------------------------------------
MidMpcAcadosSolver::MidMpcAcadosSolver(const MidMpcAcadosFormulation& formulation)
    : MidMpcAcadosSolver(formulation, CtorOpts{}) {}

// ---------------------------------------------------------------------------
// Constructor (private, CtorOpts) — create the capsule + nlp; fetch the
// per-cycle C handles. Throws on failure: the caller (MidMpcSolver ctor under
// #ifdef) treats a failed backend as a lifecycle error and must NOT install a
// non-null acados_solver_ (so the dispatch falls back to IPOPT).
//
// opts.skip_warm_up=true is TEST-ONLY (T17 review-fix Step 1 diagnostic). It
// suppresses the cold-capsule warm-up so the cold first-solve can be observed
// in isolation. Only the friend class MidMpcAcadosSolverColdCapsuleTest may
// pass it; production always goes through the public ctor above.
// ---------------------------------------------------------------------------
MidMpcAcadosSolver::MidMpcAcadosSolver(const MidMpcAcadosFormulation& formulation,
                                       const CtorOpts& opts)
    : impl_(std::make_unique<Impl>()), formulation_(formulation) {
  impl_->capsule = m5_mid_mpc_acados_acados_create_capsule();
  if (impl_->capsule == nullptr) {
    throw std::runtime_error("MidMpcAcadosSolver: create_capsule returned NULL");
  }
  const int rc = m5_mid_mpc_acados_acados_create(impl_->capsule);
  if (rc != 0) {
    // free_capsule even on a failed create (capsule was allocated).
    m5_mid_mpc_acados_acados_free_capsule(impl_->capsule);
    impl_->capsule = nullptr;
    throw std::runtime_error(
        "MidMpcAcadosSolver: acados_create failed rc=" + std::to_string(rc));
  }
  impl_->created = true;
  impl_->cfg     = m5_mid_mpc_acados_acados_get_nlp_config(impl_->capsule);
  impl_->dims    = m5_mid_mpc_acados_acados_get_nlp_dims(impl_->capsule);
  impl_->in      = m5_mid_mpc_acados_acados_get_nlp_in(impl_->capsule);
  impl_->out     = m5_mid_mpc_acados_acados_get_nlp_out(impl_->capsule);
  impl_->solver  = m5_mid_mpc_acados_acados_get_nlp_solver(impl_->capsule);

  // Parity assert: the generated solver horizon MUST match the formulation's
  // configured horizon. A mismatch means the codegen ran against a different
  // N (stale c_generated_code/); fail fast rather than solve the wrong OCP.
  const int form_n = formulation_.n_horizon();
  if (form_n != kAcadosN) {
    throw std::runtime_error(
        "MidMpcAcadosSolver: horizon mismatch formulation=" +
        std::to_string(form_n) + " generated=" + std::to_string(kAcadosN));
  }

  // ---- Cold-capsule warm-up (P1b-1b Task 17 lifecycle fix). ----
  // acados v0.4.4 SQP + FULL_CONDENSING_HPIPM + EXACT hessian: the FIRST solve
  // on a freshly-created capsule reliably returns status=2 (Infeasible,
  // sqp_iter=max_iter, traj_delta~0) EVEN when the seed is the verifiable
  // optimum. The second and all subsequent solves on the same capsule converge.
  // This is a capsule-level cold-start effect (the first QP factorization does
  // not populate caches the way subsequent ones do); it is independent of the
  // input scenario, route_weight, or seed quality. Verified empirically by a
  // route_weight sweep [1.0, 0.0, 0.5]: the FIRST solve fails regardless of
  // route_weight; subsequent solves converge regardless of route_weight.
  //
  // Production impact: the MidMpc node creates ONE capsule per solver lifetime
  // and calls solve() every cycle. Without warm-up the FIRST production cycle
  // would report Infeasible — a spurious DEGRADED/BC-MPC escalation on a
  // perfectly-feasible initial state. Warm-up in the ctor primes the capsule
  // (HPIPM factorization caches, SQP iterate buffers) so the first REAL cycle
  // converges. The warm-up solve uses the production-normal benign scenario
  // (own on route leg, route_weight=1.0, no targets) and its result is
  // DISCARDED — only the capsule state matters. The warm-up input mirrors
  // mid_mpc_node.cpp:746 (`route_weight = guard.crosses_corner ? 0.0 : 1.0`)
  // — 1.0 is the normal operational value whenever a valid active leg exists.
  //
  // S1 safety gate (T17 review-fix): the warm-up outcome is recorded in
  // warm_up_succeeded_. The MidMpcSolver dispatch reads warm_up_succeeded()
  // and, if false, refuses to dispatch to this (known-degraded) backend and
  // falls back to IPOPT — a non-converged warm-up means the first real cycle
  // would likely spuriously report Infeasible (false DEGRADED/BC-MPC).
  //
  // TEST-ONLY (T17 review-fix Step 1): when opts.skip_warm_up=true the warm-up
  // is bypassed entirely so the cold first-solve can be observed in isolation.
  // In that case warm_up_succeeded_ is left false — only the diagnostic friend
  // reaches this path, and it never dispatches through MidMpcSolver::solve.
  if (opts.skip_warm_up) {
    warm_up_succeeded_ = false;  // cold capsule; diagnostic-only.
    return;
  }
  warm_up_capsule_();
}

// Warm-up solve: run throwaway benign-scenario solves to prime the capsule's
// internal SQP/HPIPM state so the first REAL production cycle converges. The
// input is the production-normal benign scenario (own on route, route_weight=1.0,
// no targets, full heading box, default CPA floor). Results are discarded; only
// capsule state matters.
//
// Empirically (route_weight sweep on a fresh capsule): the FIRST solve always
// returns status=2 (cold-capsule effect); the SECOND and subsequent solves
// converge. So the warm-up runs TWO solves: the first (expected to fail) primes
// the HPIPM factorization cache; the second (expected to converge) confirms the
// capsule is warm. If the second still fails, log a warning — the ctor does NOT
// throw (a partially-warmed capsule degrades to cold-start behaviour, which the
// production retry/fallback path handles).
void MidMpcAcadosSolver::warm_up_capsule_() {
  MidMpcInput warm;
  warm.own_ship.psi_rad = 0.0;
  warm.own_ship.u_mps   = 5.0;
  warm.own_ship.x_m     = 0.0;
  warm.own_ship.y_m     = 0.0;
  warm.planned_route_bearing_rad = 0.0;
  warm.planned_speed_mps         = 5.0;
  warm.constraints.heading_min_rad = -M_PI;
  warm.constraints.heading_max_rad =  M_PI;
  warm.constraints.speed_min_mps   = 0.0;
  warm.constraints.speed_max_mps   = 15.0;
  warm.constraints.cpa_safe_m      = 1852.0;
  warm.constraints.own_ship_psi_rad = 0.0;
  // Production-normal active-leg scenario (mid_mpc_node.cpp:746): own on the
  // route leg at its origin, route_weight=1.0 (the active cross-leg guard value).
  warm.route_frame_origin_x_m = 0.0;
  warm.route_frame_origin_y_m = 0.0;
  warm.route_frame_normal_x   = 0.0;
  warm.route_frame_normal_y   = 1.0;
  warm.lateral_scale_m        = 400.0;
  warm.route_weight           = 1.0;
  // Two warm-up solves: the first primes the HPIPM cache (expected status=2 on
  // a cold capsule); the second confirms the capsule is warm (expected status=0).
  // Both results are discarded; only the capsule state matters. The warm_up flag
  // suppresses the non-Converged telemetry warning for these throwaway solves.
  constexpr int kWarmUpSolves = 2;
  MidMpcSolution::Status last_warm_status = MidMpcSolution::Status::NotInitialized;
  impl_->warm_up = true;
  for (int i = 0; i < kWarmUpSolves; ++i) {
    MidMpcSolution disc = solve(warm, nullptr);
    last_warm_status = disc.status;
    if (disc.status == MidMpcSolution::Status::Converged) {
      break;  // capsule is warm; no need for further warm-up solves.
    }
  }
  impl_->warm_up = false;
  // S1 safety gate (T17 review-fix): record the warm-up outcome so the
  // MidMpcSolver dispatch can refuse to use a known-degraded backend. A
  // non-converged warm-up is treated as "acados unusable": dispatch falls back
  // to IPOPT rather than risking a spurious Infeasible on the first real cycle
  // (which would escalate to DEGRADED/BC-MPC on a feasible initial state).
  warm_up_succeeded_ = (last_warm_status == MidMpcSolution::Status::Converged);
  if (!warm_up_succeeded_) {
    spdlog::warn("[M5][MidMPC][acados] cold-capsule warm-up did not converge "
                 "after {} solves (last status={}); acados backend is DISABLED "
                 "for this solver lifetime — MidMpcSolver dispatch will fall "
                 "back to IPOPT. (Known acatos v0.4.4 cold-start effect.)",
                 kWarmUpSolves, static_cast<int>(last_warm_status));
  }
}

MidMpcAcadosSolver::~MidMpcAcadosSolver() = default;

// TEST-ONLY diagnostic accessors (T17 review-fix Step 1): defined here because
// Impl is complete in this TU. Production code does not call these.
int    MidMpcAcadosSolver::last_raw_status() const noexcept { return impl_->last_raw_status; }
int    MidMpcAcadosSolver::last_sqp_iter()   const noexcept { return impl_->last_sqp_iter; }
double MidMpcAcadosSolver::last_traj_delta() const noexcept { return impl_->last_traj_delta; }

// TEST-ONLY C1 verification hook (T17 review-fix C1): re-run the constraint
// re-check on the last solved trajectory. Used by the friend test to exercise
// the status-4 recompute path on a converged solve. Production never calls this.
bool MidMpcAcadosSolver::debug_constraints_satisfied_after_solve(
    const MidMpcInput& input) {
  const auto packed = formulation_.pack_parameters(input);
  const bool lateral_active = derive_lateral_active(input);
  const int n_targets = static_cast<int>(
      std::min<std::size_t>(input.targets.size(),
                            static_cast<std::size_t>(formulation_.config().max_targets)));
  const ReachabilitySchedule debug_sched = compute_reachability_schedule(input,
      formulation_.config().dt_s);
  return constraints_satisfied_(packed.first, packed.second, lateral_active, n_targets,
                                 input.prefix_active_k, debug_sched);
}

// ===========================================================================
// F1 warm-start seed: forward-propagate the Path B 5-dim double-integrator
// dynamics from own_ship using a gentle δ/n sequence. A ZERO seed yields an
// ill-conditioned first QP (P1b-1a finding); the seed must be a trajectory the
// double-integrator can actually hold (T6 finding 2: tiny c_u -> huge turning
// diameter -> only gentle/off-path feasible).
//
// Mirror staging forward_seed_doubleint, extended to the 5-dim state:
//   r[k+1]       = r       + DT*c_u*delta
//   psi[k+1]     = psi     + DT*r           (pre-update r)
//   u_surge[k+1] = u_surge + DT*(k_prop*n^2 - k_drag*u^2)/m_sge
//   px[k+1]      = px      + u_surge*DT*cos(psi)
//   py[k+1]      = py      + u_surge*DT*sin(psi)
//
// The seed drives delta -> 0 (straight-line hold) and n -> the rpm that holds
// own_u steady-state (k_prop*n^2 = k_drag*u^2 -> n = sqrt(k_drag/k_prop)*u).
// This is the gentlest physically-consistent seed: no turn, constant speed, so
// the first QP starts from a feasible interior point. Coefficients mirror
// MidMpcAcadosFormulation (VDM-direct, baked into the generated disc_dyn_expr).
// ===========================================================================
namespace {
struct SeedState {
  double px{0.0};
  double py{0.0};
  double psi{0.0};
  double r{0.0};
  double u{0.0};
};

// One Path B discrete step (mirror gen_mid_mpc_acados.py disc_dyn + formulation
// build_disc_dyn_). Inline so the seed-readback check uses the SAME formula as
// the generated solver graph.
void path_b_step(const SeedState& s, double delta, double n, double dt,
                 SeedState& out) {
  constexpr double kCu = MidMpcAcadosFormulation::kC_u;
  constexpr double kKPropPerMass = MidMpcAcadosFormulation::kKPropPerMass;
  constexpr double kKDragPerMass = MidMpcAcadosFormulation::kKDragPerMass;
  out.px  = s.px  + s.u * dt * std::cos(s.psi);
  out.py  = s.py  + s.u * dt * std::sin(s.psi);
  out.psi = s.psi + dt * s.r;                 // pre-update r
  out.r   = s.r   + dt * kCu * delta;
  out.u   = s.u   + dt * (kKPropPerMass * n * n - kKDragPerMass * s.u * s.u);
}

// Steady-state rpm that holds surge u (k_prop*n^2 = k_drag*u^2 -> n=sqrt(drag/
// prop)*u). Clamped to the control box [N_MIN, N_MAX] = [0, 12] (gen script).
double steady_state_n_for_u(double u_mps) {
  constexpr double kNMax = 12.0;
  constexpr double kNMin = 0.0;
  if (u_mps <= 0.0) return kNMin;
  const double ratio = MidMpcAcadosFormulation::kKDrag / MidMpcAcadosFormulation::kKProp;
  const double n_raw = std::sqrt(ratio) * u_mps;
  return std::max(kNMin, std::min(kNMax, n_raw));
}

// P2 T4 (VR-07b, review-fix I-1): the per-stage F1 seed positions (px, py) are
// computed ONCE in solve() (block 1a below) into a shared std::vector<SeedState>
// that BOTH the tb-pack block (1b) and the F1 seed-write loop (step 4) read
// from. This REMOVES the prior duplicate forward-propagation helper
// (forward_propagate_seed_axis) so the tb computation and the acatos seed are
// provably the SAME trajectory by construction — no drift hazard if the F1 seed
// init/propagation changes later (there is now exactly ONE call site for the
// SeedState init + path_b_step loop).
}  // namespace

// ===========================================================================
// constraints_satisfied_ — C1 status-4 constraint re-check (T17 review-fix C1).
//
// F5 spec: status 4 (QP error during refinement) is tolerated ONLY when
// (a) solver_moved (traj_delta > tol) AND (b) constraints satisfied. The prior
// code checked (a) but NOT (b): a status-4 solve that moved but violated a
// constraint (e.g. CPA hard floor breached) was re-mapped to Converged and
// flowed to L4 unchallenged. This helper closes that gap by recomputing the
// constraint residuals h(x,u,p) from the formulation's MX con_h_expr (the SAME
// expression the acatos codegen derives from) on the SOLVED trajectory and
// verifying each ACTIVE row satisfies its lh/uh bound within kBoxTol.
//
// Row treatment (mirror build_stage_row_bounds):
//   prefix(0,1)        equality [0,0]              -> |h| <= kBoxTol
//   CPA(2..17)         >= 0 hard (Step5 方案 B:
//                      nsh=0, no slack)           -> h >= -kBoxTol
//                      empty target slots relaxed  -> skip (bound is ±kUhInf)
//   direction(18)      >= 0 when lateral_active    -> h >= -kBoxTol
//   min_alt(19)        >= 0 when lateral_active    -> h >= -kBoxTol
//   terminal(20,21,22) >= 0 when lateral_active    -> h >= -kBoxTol (stage N only)
// Rows deactivated via [-kUhInf, +kUhInf] are always satisfied (skip).
//
// Step5 方案 B (VR-01 final): nsh=0 means NO slack vector. The CPA row check is
// now a pure hard `h >= -kBoxTol` (no `h + xi >= -kBoxTol` subtraction). The
// slack read (`ocp_nlp_out_get "sl"`) is skipped when kAcadosNsh==0 — calling
// it with a zero-length buffer would be a no-op anyway, but guarding keeps the
// logic auditable and robust to the stale pre-codegen header (NSH=16) vs the
// post-codegen header (NSH=0) transition.
//
// FB-2 telemetry remedy: with no slack, L4 loses the "soft aspiration (d<2500)
// violation degree" signal. This helper additionally computes d_min over the
// horizon (min over stage k and real target t of sqrt(dx^2+dy^2)) and the soft
// aspiration violation_m = max(0, cpa_safe - d_min). The caller (solve()) lifts
// these into MidMpcSolution for ASDR/SAT transparency. The hard check itself is
// unaffected — this is pure observability, NOT a new constraint.
//
	// L2 fix (2026-07-20): d_min covers ALL stages including softened CPA rows
	// (cpa_soft=true). The d_min fold-in block runs BEFORE the is_relaxed guard
	// so it populates the minimum CPA distance over the entire horizon regardless
	// of hardening state. This is correct because d_min is a geometric measurement
	// (nearest approach distance), not a constraint check — it should reflect the
	// trajectory's actual CPA to each target irrespective of whether the CPA
	// constraint is active or softened at that stage.
	//
	// Rationale: prefix geometry is tracked separately by the committed_route
	// field (L4 consumes it independently). Including softened/prefix CPA rows
	// in d_min means the telemetry accurately reports the closest approach even
	// when the solver is still maneuvering (early stages) or the prefix is frozen.
	// L4/M7 can cross-check against the committed prefix witness (L1b DP-04 D1,
	// SC-04) for a prefix-specific audit if needed.
//
// The check is INDEPENDENT of the solver's internal residual bookkeeping: it
// rebuilds h from the published trajectory + the packed parameters and applies
// the documented bounds. A NaN residual (divergent iterate) fails the check.
// Returns false on any active-row violation; the caller keeps NumericalFailure
// instead of re-mapping status 4 to Converged.
// ===========================================================================
bool MidMpcAcadosSolver::constraints_satisfied_(
    const std::vector<double>& g,
    const std::vector<std::vector<double>>& ps,
    bool lateral_active,
    int n_targets,
    int prefix_K,
    const ReachabilitySchedule& sched) {
  // ---- Lazy-build the h function on first use (cached in Impl::h_fn). ----
  // The graph is the formulation's con_h_expr (MX); the Function wraps it over
  // (x, u, p_global, p_stage) so we can evaluate it on the solved trajectory.
  if (!impl_->h_fn) {
    // con_h_expr must reference the same symbols the formulation exposes; if
    // the graph is not built, fail CLOSED (cannot verify -> not satisfied).
    if (!formulation_.graph_valid()) {
      spdlog::warn("[M5][MidMPC][acados] C1 status-4 re-check: formulation graph "
                   "not valid; cannot verify constraints -> rejecting.");
      return false;
    }
    casadi::Function fn("m5_mid_mpc_acados_h_check",
                        {formulation_.x_sym(), formulation_.u_sym(),
                         formulation_.p_global_sym(), formulation_.p_stage_sym()},
                        {formulation_.con_h_expr()});
    impl_->h_fn = std::make_unique<casadi::Function>(std::move(fn));
  }

  // Helper: is a bound row double-disabled (±kUhInf)? Skip those.
  auto is_relaxed = [](double lo, double hi) {
    return lo <= -kUhInf * 0.5 && hi >= kUhInf * 0.5;
  };

  // Step5 方案 B: target count is kAcadosNt (16, layout constant), NOT
  // kAcadosNsh (0 after codegen re-run). CPA row activation uses n_t.
  const int n_t = std::max(0, std::min(n_targets, kAcadosNt));
  // FB-2 telemetry remedy: track d_min over the horizon for soft aspiration
  // violation_m. Initialised to +inf so the first real target sets it.
  double d_min_over_horizon = std::numeric_limits<double>::infinity();
  for (int k = 0; k <= kAcadosN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);

    // Read solved state x_k and control u_k. Terminal stage N has no control;
    // u_N is unused by the (deactivated) terminal rows when lateral_active is
    // false, and the gen script evaluates terminal rows with the last control
    // shape — feed zeros (terminal rows are one-sided on l_k which depends on
    // x only, so u_N does not affect the active terminal residual).
    double xk[kAcadosNx] = {0, 0, 0, 0, 0};
    ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "x", xk);
    double uk[kAcadosNu] = {0.0, 0.0};
    if (k < kAcadosN) {
      ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "u", uk);
    }

    // Step5 方案 B: nsh=0 -> NO slack vector. Only read "sl" when the codegen
    // actually allocated slacks (kAcadosNsh > 0). This keeps the logic robust
    // to the stale pre-codegen header (NSH=16) vs the post-codegen header
    // (NSH=0) transition. Under nsh=0 the CPA row check below is pure hard
    // `h >= -kBoxTol` (no xi subtraction). The buffer is fixed-size 16 (the max
    // slack count ever allocated; avoids a zero-length VLA when kAcadosNs==0
    // which is UB in C++ and would trip -Werror=vla).
    double sl_vec[16] = {0.0};
    static_assert(sizeof(sl_vec) / sizeof(sl_vec[0]) >= 1, "slack buffer non-empty");
    if (kAcadosNsh > 0 && kAcadosNsh <= 16 && k < kAcadosN) {
      ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "sl", sl_vec);
    }

    // Recompute h_k = con_h(x_k, u_k, g, ps_k) via the cached Function. The
    // Function takes (x, u, p_global, p_stage) as separate inputs (the
    // formulation exposes them as distinct MX symbols).
    if (g.size() + ps[kk].size() != static_cast<std::size_t>(kAcadosNp)) {
      spdlog::warn("[M5][MidMPC][acados] C1 re-check param shape mismatch at "
                   "k={}: g={} ps={} np={}", k, g.size(), ps[kk].size(), kAcadosNp);
      return false;
    }
    std::vector<double> x_vec(xk, xk + kAcadosNx);
    std::vector<double> u_vec(uk, uk + kAcadosNu);
    std::vector<double> h_val;
    try {
      // Use explicit casadi::DM inputs + std::vector<DM> call (the brace-init
      // call operator overload is ambiguous in CasADi for this arg count).
      std::vector<casadi::DM> h_in = {
          casadi::DM(casadi::Sparsity::dense(kAcadosNx, 1), x_vec),
          casadi::DM(casadi::Sparsity::dense(kAcadosNu, 1), u_vec),
          casadi::DM(casadi::Sparsity::dense(static_cast<int>(g.size()), 1), g),
          casadi::DM(casadi::Sparsity::dense(static_cast<int>(ps[kk].size()), 1), ps[kk]),
      };
      const std::vector<casadi::DM> h_out = (*impl_->h_fn)(h_in);
      // h_out[0] is a dense (nh x 1) DM; nonzeros() returns its stored values
      // (for a dense matrix this is all nh elements in column-major order).
      h_val = h_out.at(0).nonzeros();
    } catch (const std::exception& e) {
      spdlog::warn("[M5][MidMPC][acados] C1 re-check h-eval threw at k={}: {}",
                   k, e.what());
      return false;
    }
    if (static_cast<int>(h_val.size()) != kAcadosNh) {
      spdlog::warn("[M5][MidMPC][acados] C1 re-check h-size mismatch at k={}: "
                   "got {} expect {}", k, h_val.size(), kAcadosNh);
      return false;
    }

    // Per-stage bounds (mirror build_stage_row_bounds). P4: terminal C10/C11 abolished.
    RowBounds rb = build_stage_row_bounds(kAcadosNh, lateral_active, n_targets, k,
                                          std::max(0, prefix_K), sched);

    for (int r = 0; r < kAcadosNh; ++r) {
      const std::size_t rr = static_cast<std::size_t>(r);
      const double hv = h_val[rr];
      const double lo = rb.lh[rr];
      const double hi = rb.uh[rr];
      // NaN residual -> fail (divergent iterate).
      if (!std::isfinite(hv)) {
        spdlog::warn("[M5][MidMPC][acados] C1 status-4 REJECT: h[{}][{}]={} not "
                     "finite", k, r, hv);
        return false;
      }
	      // FB-2 telemetry: fold d_kt into d_min for real CPA rows (t < n_t).
	      // Runs BEFORE the is_relaxed guard so this telemetry is populated even
	      // when the CPA row is softened (cpa_soft=true or prefix_stage=true).
	      // The d_min_over_horizon is a geometric measurement (minimum distance
	      // from any solved trajectory point to any real target), not a constraint
	      // check — it should reflect the minimum CPA distance over ALL stages,
	      // regardless of hardening state. Without this fix, softened CPA rows
	      // are skipped and d_min_over_horizon sees only the terminal stage (k=N),
	      // producing artificially large values (e.g. 6849m instead of ~2100m for
	      // a target at y=2100m). The CPA residual h = dx^2+dy^2 - cpa_hard^2,
	      // so d_kt = sqrt(hv + cpa_hard^2). cpa_hard is read from the global
	      // param slot kAcadosGIdxCpaHard (1852 fixed).
	      if (r >= kRowCpaBase && r < kRowCpaBase + n_t) {
	        const std::size_t cpa_hard_idx =
	            static_cast<std::size_t>(kAcadosGIdxCpaHard);
	        if (cpa_hard_idx < g.size()) {
	          const double cpa_hard = g[cpa_hard_idx];
	          const double sum = hv + cpa_hard * cpa_hard;  // = dx^2+dy^2
	          if (sum > 0.0 && std::isfinite(sum)) {
	            const double d_kt = std::sqrt(sum);
	            if (d_kt < d_min_over_horizon) {
	              d_min_over_horizon = d_kt;
	            }
	          }
	        }
	      }

	      // Relaxed row (double-disabled) -> always satisfied.
	      if (is_relaxed(lo, hi)) continue;

	      // Step5 方案 B: CPA rows are TRUE hard (nsh=0, no slack). The effective
	      // lower bound is `lo` directly (no xi subtraction). The legacy slack
	      // subtraction (eff_lo = lo - sl_vec[t]) is retained ONLY for the stale
	      // pre-codegen header transition (kAcadosNsh > 0); once codegen re-runs
	      // with nsh=0, this branch is dead and eff_lo == lo.
	      double eff_lo = lo;
	      if (r >= kRowCpaBase && r < kRowCpaBase + n_t) {
	        const int t = r - kRowCpaBase;
	        if (kAcadosNsh > 0 && t < kAcadosNsh) {
	          eff_lo = lo - sl_vec[t];  // legacy soft (dead under nsh=0)
	        }
	      }

      // The actual bound check.
      if (hv < eff_lo - kBoxTol || hv > hi + kBoxTol) {
        // Prefix equality rows (0,1) report |h| > kBoxTol (hi==lo==0).
        const char* row_kind =
	        (r == kRowPrefixPsi) ? "prefix_psi" :
	        (r == kRowPrefixU)   ? "prefix_u"   :
	        (r >= kRowCpaBase && r < kRowCpaBase + kAcadosNt) ? "cpa" :
	        (r == kRowDirection) ? "direction"  :
	        (r == kRowMinAlt)    ? "min_alt"    :
	        "?";
        spdlog::warn("[M5][MidMPC][acados] C1 status-4 REJECT: stage={} row={} "
                     "({}) h={} outside [{}, {}] (eff_lo={}, tol={})",
                     k, r, row_kind, hv, lo, hi, eff_lo, kBoxTol);
	        return false;
      }
    }
  }
  // FB-2 telemetry remedy: lift d_min + soft aspiration violation_m into Impl
  // for solve() to read into MidMpcSolution. violation_m = max(0, cpa_safe -
  // d_min); cpa_safe is the SOFT aspiration (2500 during conflict, 1852
  // otherwise). When no real target was seen, d_min stays +inf -> violation 0.
  impl_->last_soft_aspiration_d_min_m =
      std::isfinite(d_min_over_horizon) ? d_min_over_horizon : 0.0;
  // Step5 方案 B code-review M1: use the public alias instead of the literal 10
  // so the slot arithmetic stays auditable (kAcadosGIdxCpaSafe mirrors the
  // anonymous-namespace kGIdxCpaSafe in mid_mpc_acados_formulation.cpp).
  const std::size_t cpa_safe_idx = static_cast<std::size_t>(kAcadosGIdxCpaSafe);
  const double cpa_safe_val =
      (cpa_safe_idx < g.size()) ? g[cpa_safe_idx] : 1852.0;
  const double violation =
      std::isfinite(d_min_over_horizon)
          ? std::max(0.0, cpa_safe_val - d_min_over_horizon)
          : 0.0;
  impl_->last_soft_aspiration_violation_m = violation;
  return true;
}

// ===========================================================================
// solve() — one acatos cycle. Sequence (P1b-1a T9 + staging runner pattern):
//   1. pack_parameters(input) -> {global[106], per-stage[N+1][37]}.
//   2. Per stage 0..N: concatenate global + per-stage -> 143-vector, write via
//      the GENERATED update_params (finding 3: NOT ocp_nlp_in_set "p").
//   3. Pin initial state: lbx0/ubx0 = [own_x, own_y, own_psi, 0, own_u] (r=0:
//      MidMpcInput has no measured yaw rate). idxbxe_0=[0..4] is set by codegen
//      so lbx/ubx at stage 0 act as EQUALITY.
//   4. F1 seed: forward-propagate x_seed/u_seed from own_ship (gentle straight-
//      line hold); ocp_nlp_out_set "x"/"u" per stage (non-zero seed).
//   5. Snapshot seed, solve, compute traj_delta (F5 solver-moved gate).
//   6. Reconstruct MidMpcSolution: psi/u/x/y from "x", cost from "cost_value",
//      cpa_slack = max per-target xi from "sl", iter from "sqp_iter".
// ===========================================================================
MidMpcSolution MidMpcAcadosSolver::solve(const MidMpcInput& input,
                                          const MidMpcSolution* warm_start) {
  const auto t_start = std::chrono::steady_clock::now();
#ifdef M5_ACADOS_PROFILE
  // TEMPORARY Phase-1 diagnostic: per-stage chrono breakdown. Guarded by a
  // CMake-defined macro so production builds never see it. Removed after the
  // 12.5 s/solve bottleneck is identified.
  const auto t_p0 = t_start;
  #define M5_ACADOS_PROFILE_MARK(label)                                        \
    do {                                                                       \
      const auto _t = std::chrono::steady_clock::now();                        \
      const double _ms = std::chrono::duration<double, std::milli>(_t - t_p0).count(); \
      fprintf(stderr, "[ACADOS-PROFILE] %-28s %8.2f ms (cum since last mark)\n", \
              label, _ms);                                                     \
    } while (0)
#else
  #define M5_ACADOS_PROFILE_MARK(label) do {} while (0)
#endif
  MidMpcSolution sol;  // status defaults to NotInitialized

  // ---- 1. pack parameters. ----
  // NOTE: packed.second (ps) is NON-const because P2 T4 writes the per-stage
  // tb_x/tb_y slots AFTER pack_parameters (which leaves them at 0.0) and BEFORE
  // the update_params write loop. pack_parameters fills the prefix + drift
  // slots; the tb slots are the solver's responsibility (computed from the F1
  // seed via project_to_segment, see step 1b below). g (global) stays const.
  auto packed = formulation_.pack_parameters(input);
  const std::vector<double>& g = packed.first;           // np_global = 106
  std::vector<std::vector<double>>& ps = packed.second;  // [N+1][np_per_stage]

  // Parity asserts (fail-closed: a pack/formulation mismatch is a contract bug,
  // not a tunable). Lengths MUST match the codegen-generated partition.
  if (static_cast<int>(g.size()) != formulation_.np_global() ||
      static_cast<int>(ps.size()) != kAcadosN + 1) {
    spdlog::error("[M5][MidMPC][acados] pack_parameters shape mismatch: "
                  "global={} (expect {}), stages={} (expect {})",
                  g.size(), formulation_.np_global(), ps.size(), kAcadosN + 1);
    sol.status = MidMpcSolution::Status::NumericalFailure;
    return sol;
  }

  M5_ACADOS_PROFILE_MARK("1.pack_params");

  // ---- 1a. P2 T4 (VR-07b, review-fix I-1): compute the seed trajectory ----
  // ONCE into a shared per-stage vector. This is the SINGLE propagation of the
  // seed (either warm-start shift-init or F1 forward-propagation);
  // BOTH the tb-pack block (1b) and the seed-write loop (step 4) read from
  // it, so the tb closest-point origins and the acatos seed are PROVABLY the
  // same trajectory by construction (no duplicate helper, no drift hazard).
  //
  // P5 T1: warm-start shift-init. When the previous cycle's solution is
  // available and converged, use it as the seed (shifted by one stage) instead
  // of the F1 forward-propagated seed. This is the primary anti-chattering
  // mechanism (TBD-7 option C layer 1): keeping the seed close to the previous
  // solution prevents port/starboard flips across replan cycles.
  //
  // When warm_start is null, failed, or the constraint structure changed
  // (sig_changed: different number of real targets), fall back to the F1
  // forward-propagated seed (original block 1a logic).
  //
  // The SeedState init + F1 propagation are byte-for-byte identical to the
  // pre-refactor seed loop.
  const double dt = formulation_.config().dt_s;
  SeedState s_seed;
  s_seed.px  = input.own_ship.x_m;
  s_seed.py  = input.own_ship.y_m;
  s_seed.psi = input.own_ship.psi_rad;
  s_seed.r   = 0.0;
  s_seed.u   = (input.own_ship.u_mps > 0.0) ? input.own_ship.u_mps
                                            : input.planned_speed_mps;
  const double n_hold = steady_state_n_for_u(s_seed.u);
  std::vector<SeedState> seed_traj(static_cast<std::size_t>(kAcadosN + 1));

  // P5 T1: determine if warm-start shift-init should be used.
  const int n_targets_sh = static_cast<int>(input.targets.size());
  const bool sig_changed = (impl_->prev_n_targets_ >= 0)
      && (n_targets_sh != impl_->prev_n_targets_);
  const bool use_shift_init = (warm_start != nullptr
      && static_cast<int>(warm_start->status) == 0
      && warm_start->trajectory.size() == static_cast<std::size_t>(kAcadosN)
      && !sig_changed);

  if (use_shift_init) {
    // P5 T1: shift-init from the previous converged solution.
    // Stage 0 is own_ship (pinned by lbx0/ubx0 equality constraint — must match).
    // Stages 1..N take psi/r/u from warm_start trajectory[k-1] (shifted by 1).
    // Positions are forward-propagated from the previous seed stage via the
    // double-integrator dynamics (same as path_b_step with delta=0, n=n_hold
    // for the POSITION propagation, but using warm_start's psi/r/u for heading
    // and yaw rate — the F1 hold values would give inconsistent position/heading).
    seed_traj[0] = s_seed;
    for (int k = 1; k <= kAcadosN; ++k) {
      const int ws_idx = k - 1;  // warm_start trajectory index (shift)
      SeedState sk;
      if (ws_idx < kAcadosN) {
        const auto& tp = warm_start->trajectory[static_cast<std::size_t>(ws_idx)];
        sk.psi = tp.psi_rad;
        sk.r   = tp.r_rad_s;
        sk.u   = tp.u_mps > 0.0 ? tp.u_mps : s_seed.u;
      } else {
        // Beyond warm_start horizon: hold the last warm_start value.
        const auto& tp = warm_start->trajectory[static_cast<std::size_t>(kAcadosN - 1)];
        sk.psi = tp.psi_rad;
        sk.r   = tp.r_rad_s;
        sk.u   = tp.u_mps > 0.0 ? tp.u_mps : s_seed.u;
      }
      // Forward-propagate position from the previous seed stage using the
      // warm_start psi/r/u (consistent dynamics for the tb computation).
      const SeedState& prev = seed_traj[static_cast<std::size_t>(k - 1)];
      sk.px = prev.px + prev.u * dt * std::cos(prev.psi);
      sk.py = prev.py + prev.u * dt * std::sin(prev.psi);
      seed_traj[static_cast<std::size_t>(k)] = sk;
    }
  } else {
    // F1 forward-propagated seed (original block 1a logic: gentle straight-line
    // hold, delta=0, n=n_hold). Used also for the cold-capsule warm-up (which
    // passes nullptr warm_start) and when sig_changed is true.
    SeedState s = s_seed;
    for (int k = 0; k <= kAcadosN; ++k) {
      seed_traj[static_cast<std::size_t>(k)] = s;
      if (k < kAcadosN) {
        SeedState next;
        path_b_step(s, /*delta=*/0.0, n_hold, dt, next);
        s = next;
      }
    }
  }

  // ---- 1b. P2 T4 (VR-07b): fill the per-stage t_b slots (tb_x/tb_y) via ----
  // project_to_segment on the F1 forward-propagated seed positions. The route
  // COST (build_route_cost_, T3) and terminal COST (build_terminal_cost_, T4)
  // read these slots as the lateral-deviation origin; pack_parameters leaves
  // them at 0.0 (neutral placeholder), so the solver MUST populate them here
  // BEFORE the update_params write loop concatenates them into the 143-vector
  // the acatos graph reads.
  //
  // The leg is treated as an effectively-infinite ray from the active-leg
  // origin A along the leg bearing; B = A + bearing_dir * extent with extent
  // large enough that projection onto [A, B] never clamps to B over the
  // horizon. leg_extent = planned_speed * (N+2) * dt is a safe multiple of the
  // maximum own displacement (own + planned_speed * N * dt); a degenerate seed
  // (NaN own / degenerate leg) falls back to the ABSOLUTE route origin A — the
  // honest fallback (cost well-defined relative to the leg start), never (0,0).
  //
  // Review-fix M-2: leg_extent is FLOORED at dt so a stationary / low-speed
  // ship (planned_speed == 0) still gets a finite leg to project onto (the
  // projection is well-defined for any non-zero extent). The TRUE degenerate
  // case (NaN leg origin / NaN bearing) is still handled by the
  // seed_or_leg_degenerate fallback inside compute_per_stage_tb.
  //
  // The seed positions come from the shared seed_traj (block 1a) — the SAME
  // vector the F1 seed-write loop (step 4) reads from, so the tb origins and
  // the acatos seed are identical by construction.
  {
    const double ax = g[static_cast<std::size_t>(kAcadosGIdxRouteFrameOriginX)];
    const double ay = g[static_cast<std::size_t>(kAcadosGIdxRouteFrameOriginY)];
    const double bearing = g[static_cast<std::size_t>(kAcadosGIdxRouteFrameBearing)];
    const double nx = g[static_cast<std::size_t>(kAcadosGIdxRouteFrameNormalX)];
    const double ny = g[static_cast<std::size_t>(kAcadosGIdxRouteFrameNormalY)];
    const double planned = g[static_cast<std::size_t>(kAcadosGIdxPlannedSpeed)];
    // Generous extent: (N+2) steps of planned speed. Guards against clamp-to-B
    // for any realistic own position over the horizon (own displacement is at
    // most planned_speed * N * dt; +2 steps is a safety margin). FLOORED at dt
    // (review-fix M-2) so a stationary ship still projects onto a finite leg.
    const double leg_extent = std::max(
        std::fabs(planned) * static_cast<double>(kAcadosN + 2) * dt, dt);
    // Read px/py per stage from the shared seed trajectory (block 1a).
    std::vector<double> px_seed(static_cast<std::size_t>(kAcadosN + 1), 0.0);
    std::vector<double> py_seed(static_cast<std::size_t>(kAcadosN + 1), 0.0);
    for (int k = 0; k <= kAcadosN; ++k) {
      const std::size_t kk = static_cast<std::size_t>(k);
      px_seed[kk] = seed_traj[kk].px;
      py_seed[kk] = seed_traj[kk].py;
    }
    const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                               ax, ay, bearing, nx, ny,
                                               leg_extent, kAcadosN);
    // Write tb into the per-stage slots (single source of truth: the offsets
    // are the PUBLIC constexpr kAcadosPerStageTbXOff/YOff in the formulation
    // hpp — no magic numbers here).
    const std::size_t off_tx = static_cast<std::size_t>(kAcadosPerStageTbXOff);
    const std::size_t off_ty = static_cast<std::size_t>(kAcadosPerStageTbYOff);
    for (int k = 0; k <= kAcadosN; ++k) {
      const std::size_t kk = static_cast<std::size_t>(k);
      ps[kk][off_tx] = tb.tb_x[kk];
      ps[kk][off_ty] = tb.tb_y[kk];
    }
	  }

	  // ---- 1c. P5 T2: fill per-stage psi_prev/u_prev for transition cost. ----
	  // Shift the last converged solution by one stage: stage k's psi_prev uses
	  // the converged trajectory's stage k (shifted: stage k of current cycle
	  // should compare against stage k of the PREVIOUS cycle, not k+1, because
	  // both cycles start from the same own-ship position and the shift is in
	  // the SEED, not in the transition comparison).
	  //
	  // When no cached solution exists (first cycle / cold start), psi_prev and
	  // u_prev default to 0.0 which produces a small J_transition but does not
	  // dominate the total cost. This is acceptable: the first cycle has no
	  // prior solution to compare against.
	  if (!impl_->last_converged_solution_.trajectory.empty() &&
	      impl_->last_converged_solution_.trajectory.size() ==
	          static_cast<std::size_t>(kAcadosN)) {
	    for (int k = 0; k <= kAcadosN; ++k) {
	      const std::size_t kk = static_cast<std::size_t>(k);
	      if (k < kAcadosN) {
	        // Stage k compares against converged trajectory[k] (same index).
	        const auto& tp = impl_->last_converged_solution_.trajectory[kk];
        ps[kk][kAcadosPerStagePsiPrevOff] = tp.psi_rad;
        ps[kk][kAcadosPerStageUPrevOff]   = tp.u_mps;
      } else {
        // Terminal stage N: use the last trajectory point.
        const auto& tp = impl_->last_converged_solution_.trajectory[
            static_cast<std::size_t>(kAcadosN - 1)];
        ps[kk][kAcadosPerStagePsiPrevOff] = tp.psi_rad;
        ps[kk][kAcadosPerStageUPrevOff]   = tp.u_mps;
      }
      // Transition cost active: cached solution exists.
      ps[kk][kAcadosPerStageWTransActiveOff] = 1.0;
    }
  } else {
    // No cached solution (first cycle / cold start): use the seed_traj psi/u as
    // psi_prev/u_prev so J_transition ≈ 0, with w_trans_active=0 to fully
    // disable the transition cost. This prevents penalizing the first solve
    // for deviating from the seed trajectory to avoid obstacles.
    for (int k = 0; k <= kAcadosN; ++k) {
      const std::size_t kk = static_cast<std::size_t>(k);
      ps[kk][kAcadosPerStagePsiPrevOff] = seed_traj[kk].psi;
      ps[kk][kAcadosPerStageUPrevOff]   = seed_traj[kk].u;
      ps[kk][kAcadosPerStageWTransActiveOff] = 0.0;
    }
  }

  // ---- 2. write per-stage concatenated params (146 per stage). ----
  // Concatenate global (106) + per-stage (40) = 146 (codegen's NP). The single-
  // stage graph reads fixed offsets; the global portion is stage-uniform.
  std::vector<double> p_stage_vec(static_cast<std::size_t>(kAcadosNp), 0.0);
  for (int k = 0; k <= kAcadosN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    const std::size_t nps = static_cast<std::size_t>(formulation_.np_per_stage());
    if (ps[kk].size() != nps) {
      spdlog::error("[M5][MidMPC][acados] per-stage shape mismatch at k={}: "
                    "len={} (expect {})", k, ps[kk].size(), nps);
      sol.status = MidMpcSolution::Status::NumericalFailure;
      return sol;
    }
    std::memcpy(p_stage_vec.data(), g.data(), g.size() * sizeof(double));
    std::memcpy(p_stage_vec.data() + g.size(), ps[kk].data(),
                ps[kk].size() * sizeof(double));
    const int rc = m5_mid_mpc_acados_acados_update_params(
        impl_->capsule, k, p_stage_vec.data(), kAcadosNp);
    if (rc != 0) {
      spdlog::error("[M5][MidMPC][acados] update_params failed at k={} rc={}", k, rc);
      sol.status = MidMpcSolution::Status::NumericalFailure;
      return sol;
    }
  }

  // ---- 2b. per-stage lh/uh (mirror IPOPT derive_row_bound_config). ----
  // The acados graph emits a FIXED nh=23 path-con rows per stage; the codegen
  // sets them stage-uniform. For non-lateral scenarios (stand-on / Hold /
  // ReduceSpeed) the direction/min_alt/terminal rows would make the NLP
  // infeasible (e.g. g_term_side = pref_dir*l_k - l_min = -30 < 0 with
  // pref_dir=0). IPOPT disables those rows per-scenario via
  // derive_row_bound_config; this wrapper does the same by relaxing them to
  // [-kUhInf, +kUhInf] here. This is constraint CLARIFICATION (mirror the IPOPT
  // lifecycle), NOT threshold tuning: the active-row set is determined solely
  // by the COLREGs role + preferred direction + behavior in the input.
  //
  // This is set EVERY solve (params may have changed role/direction between
  // cycles, so the active set is re-derived each call). n_targets drives CPA-row
  // activation: empty target slots [n_targets..max_targets-1] are relaxed to
  // [-inf,+inf] (mirror IPOPT, which emits CPA rows only for real targets).
// ---- 2. L1b reachability schedule: compute k_minalt + k_head. ----
  // This determines the earliest stage at which min_alt and heading-box
  // constraints may be hardened (mirrors IPOPT derive_row_bound_config
  // §4.2/§4.6). Stages before k_minalt/k_head have those rows double-disabled
  // (relaxed to [-inf,+inf]) so the solver does not receive an infeasible
  // problem at stage 0/1 where the ship physically cannot have turned yet.
  const bool lateral_active = derive_lateral_active(input);
  const int n_targets = static_cast<int>(
      std::min<std::size_t>(input.targets.size(),
                            static_cast<std::size_t>(formulation_.config().max_targets)));
  const int prefix_K = std::max(0, input.prefix_active_k);
  const double dt_s = formulation_.config().dt_s;
  const ReachabilitySchedule sched = compute_reachability_schedule(input, dt_s);
  for (int k = 0; k <= kAcadosN; ++k) {
    RowBounds rb = build_stage_row_bounds(kAcadosNh, lateral_active, n_targets, k,
                                          prefix_K, sched);
    ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k,
                                  "lh", rb.lh.data());
    ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k,
                                  "uh", rb.uh.data());
  }

  // ---- 3. pin initial state x0 = [px, py, psi, r, u_surge]. ----
  // idxbxe_0=[0..4] is set by codegen, so lbx0/ubx0 at stage 0 are EQUALITY.
  // r (idx 3) seeds as 0 — MidMpcInput has no measured yaw rate (the formulation
  // document confirms this in kGIdxPsi0..kGIdxY0 comments).
  double x0[kAcadosNx] = {input.own_ship.x_m, input.own_ship.y_m,
                          input.own_ship.psi_rad, 0.0, input.own_ship.u_mps};
  // Defensive: NaN/Inf in x0 aborts the solver at the first function eval.
  for (int i = 0; i < kAcadosNx; ++i) {
    if (!std::isfinite(x0[i])) {
      spdlog::warn("[M5][MidMPC][acados] non-finite x0[{}]={} (input.own_ship "
                   "x/y/psi/u)", i, x0[i]);
      x0[i] = 0.0;  // fail-safe: avoid poisoning the solver with NaN.
    }
  }
  ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, 0,
                                "lbx", x0);
  ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, 0,
                                "ubx", x0);

  // ---- 3b. DP-02 box live (Step5 方案 B, VR-02): per-stage heading, ROT, and
  //      speed bounds for stages 1..N. Stage 0 REMAINS pinned by lbx0=ubx0=x0
  //      (idxbxe_0=[0..4] makes it an equality constraint). We must NOT overwrite
  //      stage 0's bounds, otherwise the equality pinning is lost.
  //
  //      The codegen provides static defaults (PSI_LB/UB=±π, ROT_MAX=0.2094,
  //      U_SURGE_MIN/MAX=[0,15]). These are safe but suboptimal: the M4/M6 may
  //      impose a tighter heading box (e.g., 23.2°..53.2°) or speed range. By
  //      writing the LIVE input values to every stage, the solver respects the
  //      current ODD/behavior intent.
  //
  //      px [0] and py [1] stay unbounded (idxbx does not include them for path
  //      stages). The unbounded sentinel is kUhInf (1e10).
  //
  //      CAVEAT (heading wrap): if heading_min > heading_max the 0° line is
  //      inside the box (e.g. [355°, 5°] -> wrap). acados has no "short way"
  //      concept for box bounds; setting lbx > ubx would make the stage infeasible.
  //      We detect this and fall back to [-π, π] (the codegen default) for that
  //      stage. The full heading-wrap-aware schedule (k_head, t_latest_safe) is
  //      L1b scope (BL-12).
  {
    const mass_l3::m5::ConstraintInputs& cst = input.constraints;
    const double hdg_min = cst.heading_min_rad;
    const double hdg_max = cst.heading_max_rad;
    const double spd_min = cst.speed_min_mps;
    const double spd_max = cst.speed_max_mps;
    // ROT bound from MidMpcInput (not ConstraintInputs).
    const double rot_max = input.rot_max_rad_s;
    // Skip if all bounds match codegen defaults (avoids perturbing the cold
    // capsule warm-up with redundant ocp_nlp_constraints_model_set calls).
    constexpr double kDefaultHdgMin = -M_PI;
    constexpr double kDefaultHdgMax =  M_PI;
    constexpr double kDefaultSpdMin = 0.0;
    constexpr double kDefaultSpdMax = 15.0;
    constexpr double kDefaultRotMax = 0.2094;
    if (std::abs(hdg_min - kDefaultHdgMin) > 1e-9 ||
        std::abs(hdg_max - kDefaultHdgMax) > 1e-9 ||
        std::abs(spd_min - kDefaultSpdMin) > 1e-9 ||
        std::abs(spd_max - kDefaultSpdMax) > 1e-9 ||
        std::abs(rot_max - kDefaultRotMax) > 1e-9) {
      // Heading wrap guard.
      const bool hdg_valid = std::isfinite(hdg_min) && std::isfinite(hdg_max)
                          && hdg_min < hdg_max;
      const double use_hdg_min = hdg_valid ? hdg_min : kDefaultHdgMin;
      const double use_hdg_max = hdg_valid ? hdg_max : kDefaultHdgMax;
      const double use_spd_min = std::isfinite(spd_min) ? spd_min : kDefaultSpdMin;
      const double use_spd_max = std::isfinite(spd_max) ? spd_max : kDefaultSpdMax;
      const double use_rot_max = std::isfinite(rot_max) ? std::abs(rot_max) : kDefaultRotMax;
      // L2 heading/ROT schedule separation (DP-02): heading uses the reachability
      // schedule (k_head_earliest). Before k_head_earliest, heading stays at the
      // codegen default ±π because the ship has not yet turned to the M4 heading
      // box. ROT and speed are ALWAYS hard-bound (physical limits, not behavior
      // schedule) — they apply from stage 1 regardless of k_head_earliest.
      const bool hdg_differs = (std::abs(hdg_min - kDefaultHdgMin) > 1e-9 ||
                                std::abs(hdg_max - kDefaultHdgMax) > 1e-9);
      for (int k = 1; k <= kAcadosN; ++k) {
        const double stage_hdg_min = (hdg_differs && k >= sched.k_head_earliest)
                                         ? use_hdg_min : kDefaultHdgMin;
        const double stage_hdg_max = (hdg_differs && k >= sched.k_head_earliest)
                                         ? use_hdg_max : kDefaultHdgMax;
        double lbx[kAcadosNx] = {-kUhInf, -kUhInf, stage_hdg_min, -use_rot_max, use_spd_min};
        double ubx[kAcadosNx] = {kUhInf,  kUhInf,  stage_hdg_max,  use_rot_max, use_spd_max};
        ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "lbx", lbx);
        ocp_nlp_constraints_model_set(impl_->cfg, impl_->dims, impl_->in, k, "ubx", ubx);
      }
    }
  }

  // ---- 4. F1 forward-propagated NON-ZERO seed. ----
  // delta -> 0 (straight hold); n -> steady-state-hold rpm for own_u. This is
  // the gentlest physically-consistent seed: no turn, constant speed. The seed
  // is written to every stage so the first QP starts at a feasible interior
  // point (a zero seed is ill-conditioned — P1b-1a finding).
  //
  // Review-fix I-1: the per-stage SeedState is read from seed_traj (block 1a),
  // the SAME vector the tb-pack block (1b) read from — so the acatos seed and
  // the tb closest-point origins are provably the same trajectory. The values
  // written here (x_seed, u_seed) are byte-for-byte identical to the pre-refactor
  // seed loop: x_seed = {px, py, psi, r, u} per stage; u_seed = {0, n_hold} per
  // stage; the SeedState init and path_b_step propagation are unchanged (now in
  // block 1a). Only the code structure changed (shared vector vs re-propagation).
  const double delta_hold_seed = 0.0;
  for (int k = 0; k <= kAcadosN; ++k) {
    const SeedState& sk = seed_traj[static_cast<std::size_t>(k)];
    double x_seed[kAcadosNx] = {sk.px, sk.py, sk.psi, sk.r, sk.u};
    ocp_nlp_out_set(impl_->cfg, impl_->dims, impl_->out, k, "x", x_seed);
    if (k < kAcadosN) {
      double u_seed[kAcadosNu] = {delta_hold_seed, n_hold};
      ocp_nlp_out_set(impl_->cfg, impl_->dims, impl_->out, k, "u", u_seed);
    }
  }

  // ---- 5a. snapshot seed (px/py) for F5 solver-moved gate. ----
  std::vector<double> px_seed_snap(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  std::vector<double> py_seed_snap(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  for (int k = 0; k <= kAcadosN; ++k) {
    double xk[kAcadosNx] = {0, 0, 0, 0, 0};
    ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "x", xk);
    px_seed_snap[static_cast<std::size_t>(k)] = xk[0];
    py_seed_snap[static_cast<std::size_t>(k)] = xk[1];
  }

  // ---- 5b. solve. ----
  M5_ACADOS_PROFILE_MARK("5b.pre_solve");
  const int status = m5_mid_mpc_acados_acados_solve(impl_->capsule);
  M5_ACADOS_PROFILE_MARK("5b.solve_done");
  // TEST-ONLY diagnostic mirror (T17 review-fix Step 1).
  impl_->last_raw_status = status;

  // ---- 6. extract solved trajectory + per-stage per-target slack. ----
  std::vector<double> px_traj(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  std::vector<double> py_traj(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  std::vector<double> psi_traj(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  std::vector<double> u_traj(static_cast<std::size_t>(kAcadosN + 1), 0.0);
  bool any_nan = false;
  double cpa_slack_max = 0.0;  // σ = max over (target, stage) of per-target xi.
  // P3: per-target ξ max over stages (max over k per target slot t).
  std::array<double, 16> per_target_max{};  // zero-init
  for (int k = 0; k <= kAcadosN; ++k) {
    double xk[kAcadosNx] = {0, 0, 0, 0, 0};
    ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "x", xk);
    const std::size_t kk = static_cast<std::size_t>(k);
    px_traj[kk]  = xk[0];
    py_traj[kk]  = xk[1];
    psi_traj[kk] = xk[2];
    u_traj[kk]   = xk[4];  // u_surge (idx 4; idx 3 is yaw rate r)
    if (!std::isfinite(px_traj[kk]) || !std::isfinite(py_traj[kk]) ||
        !std::isfinite(psi_traj[kk]) || !std::isfinite(u_traj[kk])) {
      any_nan = true;
    }
    // Per-target CPA slack (NSH per path stage; "sl" length = NS).
    // Terminal stage N has no sl (no control at N), so skip reading it there.
    // Step5 方案 B: nsh=0 -> no slack allocated. Guard the read with
    // kAcadosNsh > 0 so the wrapper is robust to the stale pre-codegen header
    // (NSH=16) vs the post-codegen header (NSH=0). Under nsh=0 the cpa_slack
    // fields stay 0 (correct — there is no slack to report); the soft
    // aspiration signal is carried by last_soft_aspiration_violation_m instead
    // (computed in constraints_satisfied_, lifted into sol below).
    // (FB-2): the soft aspiration signal now also lives in
    // last_soft_aspiration_violation_m (computed in constraints_satisfied_),
    // which is the surviving path under nsh=0.
    if (kAcadosNsh > 0 && kAcadosNsh <= 16 && k < kAcadosN) {
      // Fixed-size buffer (max 16 slacks ever allocated) avoids a zero-length
      // VLA when kAcadosNs==0 which is UB in C++ and trips -Werror=vla.
      double sl_vec[16] = {0.0};
      ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "sl", sl_vec);
      for (int t = 0; t < kAcadosNsh; ++t) {
        const double xi = sl_vec[t];
        const double xi_abs = std::fabs(xi);
        if (std::isfinite(xi)) {
          if (xi_abs > cpa_slack_max) {
            cpa_slack_max = xi_abs;
          }
          // P3: per-target ξ max over stages.
          const std::size_t tu = static_cast<std::size_t>(t);
          if (xi_abs > per_target_max[tu]) {
            per_target_max[tu] = xi_abs;
          }
        }
      }
    }
  }

  // F5 solver-moved: traj_delta = sum |px_solved - px_seed| + |py_solved - py_seed|.
  double traj_delta = 0.0;
  for (int k = 0; k <= kAcadosN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    traj_delta += std::fabs(px_traj[kk] - px_seed_snap[kk]) +
                  std::fabs(py_traj[kk] - py_seed_snap[kk]);
  }
  const bool solver_moved = (status == 0) || (traj_delta > kTrajDeltaTol);
  // TEST-ONLY diagnostic mirror (T17 review-fix Step 1).
  impl_->last_traj_delta = traj_delta;

  // ---- Map acados status -> MidMpcSolution::Status (F5 contract). ----
  // status 0 -> Converged.
  // status 4 tolerated ONLY when (a) solver_moved AND (b) constraints satisfied
  //   (C1 T17 review-fix: the prior code checked only (a); a status-4 that
  //   moved but violated a constraint — e.g. CPA hard floor breached — was
  //   reported Converged and flowed to L4 unchallenged). Now both gates must
  //   pass; otherwise the NumericalFailure from map_acados_status stands.
  //   status 1 -> Timeout; status 2 -> Infeasible; status 3/other -> NumFailure.
  MidMpcSolution::Status mapped = map_acados_status(status);
  // Step5 方案 B (FB-2 telemetry remedy): constraints_satisfied_ computes the
  // soft-aspiration d_min + violation_m SIDE EFFECTS (lifted into Impl for
  // solve() to read into MidMpcSolution). For status=0 (Converged) the boolean
  // is unused but the d_min computation must still run so the telemetry is
  // populated every cycle. For status=4 the boolean gates the re-map. Running
  // the check on status=0 is an N-step cached MX eval (cheap, observability-
  // only). For status=1/2/3 (Timeout/Infeasible/NumFailure) the trajectory is
  // unreliable; skip the check (d_min stays at last cycle's value, which is
  // acceptable for telemetry — those statuses already flag the cycle).
  if (status == 0 || (status == 4 && solver_moved)) {
    const bool csat = constraints_satisfied_(g, ps, lateral_active, n_targets,
                                              input.prefix_active_k, sched);
    if (status == 4 && solver_moved) {
      if (csat) {
        mapped = MidMpcSolution::Status::Converged;
      } else {
        // Keep NumericalFailure: the solve moved but a constraint is violated.
        // Log which gate failed for telemetry (constraints_satisfied_ already
        // logged the offending row).
        spdlog::warn("[M5][MidMPC][acados] status=4 RE-MAP DENIED: solver moved "
                     "(traj_delta={}) but constraint re-check FAILED -> keeping "
                     "NumericalFailure (NOT Converged).", traj_delta);
      }
    }
  }
  // NaN in the trajectory is ALWAYS a NumericalFailure, regardless of status.
  if (any_nan) {
    mapped = MidMpcSolution::Status::NumericalFailure;
  }

  // D1 witness (DP-04, VR-04): independent geometric check on committed prefix
  // CPA. Overrides ALL solver-derived statuses if the frozen prefix geometry
  // violates the hard CPA floor (NO_SAFE_PLAN → M7 MRM). This check runs
  // regardless of solver status (Converged, Infeasible, Timeout, etc.) because
  // the committed prefix is frozen geometry independent of the solver output.
  //
  // Even when the solver returned Infeasible (status 2), the prefix violation
  // is the ROOT CAUSE — the solver couldn't find a feasible trajectory partly
  // because the frozen prefix makes the problem infeasible at stage 0. Mapping
  // to NumericalFailure (D1 prefix) rather than Infeasible (QP) gives M7 a
  // clearer signal: the committed prefix itself is unsafe, not just the QP.
  if (!d1_prefix_cpa_witness(input, formulation_.config().dt_s)) {
    mapped = MidMpcSolution::Status::NumericalFailure;
  }

  const auto t_end = std::chrono::steady_clock::now();
  M5_ACADOS_PROFILE_MARK("6.extract_done");
  sol.status = mapped;
  sol.solve_duration_ms = static_cast<std::int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());
  sol.cpa_slack = cpa_slack_max;
  sol.cpa_slack_per_target = per_target_max;
  // Step5 方案 B (FB-2 telemetry remedy): lift soft-aspiration d_min +
  // violation_m from Impl into MidMpcSolution so ASDR/SAT can observe how far
  // the solved trajectory is from the soft cpa_safe (2500 during conflict).
  // This is the slack-free replacement for the cpa_slack signal (which is 0
  // under nsh=0). Populated by constraints_satisfied_ (called above for
  // status=0 and status=4); stays 0 for Timeout/Infeasible/NumFailure.
  sol.soft_aspiration_d_min_m = impl_->last_soft_aspiration_d_min_m;
  sol.soft_aspiration_violation_m = impl_->last_soft_aspiration_violation_m;

  // SQP iter count (the field name stays ipopt_iterations per the output
  // contract — downstream reads it; do NOT rename). ocp_nlp_get "sqp_iter".
  int sqp_iter = 0;
  ocp_nlp_get(impl_->solver, "sqp_iter", &sqp_iter);
  sol.ipopt_iterations = static_cast<std::int32_t>(sqp_iter);
  // TEST-ONLY diagnostic mirror (T17 review-fix Step 1).
  impl_->last_sqp_iter = sqp_iter;
#ifdef M5_ACADOS_PROFILE
  // TEMPORARY Phase-1 diagnostic: acatos-internal stage breakdown (per solve).
  {
    double t_qp = 0.0, t_lin = 0.0, t_reg = 0.0, t_sim = 0.0, t_glob = 0.0, t_tot = 0.0;
    double t_qp_xcond = 0.0;
    ocp_nlp_get(impl_->solver, "time_tot", &t_tot);
    ocp_nlp_get(impl_->solver, "time_qp_solver_call", &t_qp);
    ocp_nlp_get(impl_->solver, "time_qp_xcond", &t_qp_xcond);
    ocp_nlp_get(impl_->solver, "time_lin", &t_lin);
    ocp_nlp_get(impl_->solver, "time_reg", &t_reg);
    ocp_nlp_get(impl_->solver, "time_sim", &t_sim);
    ocp_nlp_get(impl_->solver, "time_glob", &t_glob);
    const double wall_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    const double per_iter_ms = (sqp_iter > 0) ? (t_tot * 1000.0 / sqp_iter) : 0.0;
    fprintf(stderr, "[ACADOS-PROFILE] solve_detail status=%d sqp_iter=%d "
            "wall=%.1fms time_tot=%.1fms qp_call=%.1fms qp_xcond=%.1fms "
            "lin=%.1fms reg=%.1fms sim=%.1fms glob=%.1fms per_iter=%.1fms\n",
            status, sqp_iter, wall_ms, t_tot * 1000.0, t_qp * 1000.0,
            t_qp_xcond * 1000.0, t_lin * 1000.0, t_reg * 1000.0,
            t_sim * 1000.0, t_glob * 1000.0, per_iter_ms);
  }
#endif

  // Real cost value (ocp_nlp_eval_cost populates it; ocp_nlp_get reads it).
  // This is an IMPROVEMENT over IPOPT (which leaves cost_total=0 in some paths
  // — E1). The contract field is the same; only the value is now populated.
  double cost_val = 0.0;
  ocp_nlp_eval_cost(impl_->solver, impl_->in, impl_->out);
  ocp_nlp_get(impl_->solver, "cost_value", &cost_val);
  if (std::isfinite(cost_val)) {
    sol.cost_total = cost_val;
  }

  // ---- Reconstruct MidMpcSolution.trajectory (N points, NOT N+1). ----
  // The IPOPT MidMpcSolution.trajectory is N points (the MPC horizon excludes
  // the terminal state — the decision vector is [psi;u] over N). acatos returns
  // N+1 shooting-node states; we take stages 0..N-1 to match the IPOPT shape
  // (the tail-gate reads .back() as the terminal command, same as IPOPT).
  sol.trajectory.resize(static_cast<std::size_t>(kAcadosN));
  for (int k = 0; k < kAcadosN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    auto& point = sol.trajectory[kk];
    point.psi_rad = psi_traj[kk];
    point.u_mps   = u_traj[kk];
    point.x_m     = px_traj[kk];
    point.y_m     = py_traj[kk];
    point.t_s     = static_cast<double>(k) * dt;
    // r_rad_s: read from the solved state (idx 3) for diagnostic richness.
    double xk[kAcadosNx] = {0, 0, 0, 0, 0};
    ocp_nlp_out_get(impl_->cfg, impl_->dims, impl_->out, k, "x", xk);
    point.r_rad_s = xk[3];
  }

  // Stamp the cycle (parity with node-side MidMpcSolution population).
  sol.stamp_ns = input.stamp_ns;

  // Log non-Converged outcomes for telemetry (mirror IPOPT warn pattern).
  // Suppressed during cold-capsule warm-up (impl_->warm_up): the first warm-up
  // solve returns status=2 by the cold-capsule effect, which is EXPECTED and
  // would mislead operators if logged at production telemetry level.
  if (sol.status != MidMpcSolution::Status::Converged && !impl_->warm_up) {
    spdlog::warn("[M5][MidMPC][acados] status={} (acatos={}) sqp_iter={} "
                 "traj_delta={} solver_moved={} cost={} cpa_slack={}",
                 static_cast<int>(sol.status), status, sqp_iter, traj_delta,
                 solver_moved ? 1 : 0, sol.cost_total, sol.cpa_slack);
  }
	  // P5 T1: cache the converged solution for next cycle's warm-start shift-init.
	  if (static_cast<int>(sol.status) == 0) {
	    impl_->last_converged_solution_ = sol;
	    impl_->prev_n_targets_ = n_targets_sh;
	  }
	  return sol;
	}

	}  // namespace mass_l3::m5::mid_mpc
