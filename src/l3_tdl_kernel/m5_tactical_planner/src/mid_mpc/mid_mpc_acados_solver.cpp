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
constexpr int kAcadosNp  = M5_MID_MPC_ACADOS_NP;    // 143 (106 global + 37 per-stage,
                                                    //      concatenated by codegen; 37 = 3 prefix
                                                    //      + 2*16 target drift + 2 tb_x/tb_y, P2 VR-07b)
constexpr int kAcadosNsh = M5_MID_MPC_ACADOS_NSH;   // 16 (per-target CPA slacks)
constexpr int kAcadosNs  = M5_MID_MPC_ACADOS_NS;    // 16 (slacks per path stage)
constexpr int kAcadosNh  = M5_MID_MPC_ACADOS_NH;    // 23 (path h rows)

// Row-class offsets in con_h (mirror MidMpcAcadosFormulation::build_con_h_ and
// gen_mid_mpc_acados.py). FIXED order; the codegen emits the same layout. These
// are the indices the solver wrapper writes to when it sets per-stage lh/uh to
// mirror IPOPT's derive_row_bound_config (deactivate direction/min_alt/terminal
// for non-lateral scenarios).
//   [0]      prefix_psi      (equality via pact_pre activation factor)
//   [1]      prefix_u        (equality via pact_pre activation factor)
//   [2..17]  CPA per-target  (one-sided >= 0, softened via idxsh=[2..17])
//   [18]     direction       (pref_dir * l_k)
//   [19]     min_alt         (pref_dir * (psi - own_psi) - min_alt_par)
//   [20]     g_term_side     (pref_dir * l_k - l_min)
//   [21]     g_term_lo       (l_k + l_max)
//   [22]     g_term_hi       (l_max - l_k)
constexpr int kRowPrefixPsi = 0;
constexpr int kRowPrefixU   = 1;
constexpr int kRowCpaBase   = 2;                    // CPA rows [2..2+Nt-1]
constexpr int kRowDirection = 2 + kAcadosNsh;       // 18 (Nt=16)
constexpr int kRowMinAlt    = 2 + kAcadosNsh + 1;   // 19
constexpr int kRowTermSide  = 2 + kAcadosNsh + 2;   // 20
constexpr int kRowTermLo    = 2 + kAcadosNsh + 3;   // 21
constexpr int kRowTermHi    = 2 + kAcadosNsh + 4;   // 22
static_assert(kRowTermHi == kAcadosNh - 1,
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

RowBounds build_stage_row_bounds(int stage, int nh, bool lateral_active,
                                 int n_targets) {
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
  // CPA rows [2..17]: one-sided >= 0 for the n_targets REAL targets; relaxed
  // [-inf,+inf] for empty slots [n_targets..max_targets-1] (mirror IPOPT, which
  // emits CPA rows only for real targets). Clamp n_targets to [0, kAcadosNsh].
  const int n_t = std::max(0, std::min(n_targets, kAcadosNsh));
  for (int t = 0; t < kAcadosNsh; ++t) {
    if (t < n_t) {
      set_row_value(kRowCpaBase + t, 0.0, kUhInf);       // real target: >= 0
    } else {
      set_row_value(kRowCpaBase + t, -kUhInf, kUhInf);   // empty slot: relaxed
    }
  }
  // Direction / min_alt / terminal: deactivate unless lateral_active.
  const bool dir_active = lateral_active;
  const bool terminal_active = lateral_active && (stage == kAcadosN);
  auto set_row = [&](int idx, bool active) {
    if (active) {
      set_row_value(idx, 0.0, kUhInf);        // one-sided >= 0
    } else {
      set_row_value(idx, -kUhInf, kUhInf);    // double-disabled
    }
  };
  set_row(kRowDirection, dir_active);
  set_row(kRowMinAlt,    dir_active);
  set_row(kRowTermSide,  terminal_active);
  set_row(kRowTermLo,    terminal_active);
  set_row(kRowTermHi,    terminal_active);
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
  // C1 status-4 constraint re-check (T17 review-fix C1): a CasADi Function
  // built lazily on the FIRST status-4 outcome, wrapping the formulation's
  // con_h_expr over (x, u, p_global, p_stage). Cached so the per-status-4
  // re-check does not rebuild the function graph on every call. Null until
  // first use; stays null if status 4 never occurs (the common case).
  std::unique_ptr<casadi::Function> h_fn;

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
  return constraints_satisfied_(packed.first, packed.second, lateral_active, n_targets);
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
//   CPA(2..17)         >= 0, softened via idxsh    -> h + xi >= -kBoxTol
//                      empty target slots relaxed  -> skip (bound is ±kUhInf)
//   direction(18)      >= 0 when lateral_active    -> h >= -kBoxTol
//   min_alt(19)        >= 0 when lateral_active    -> h >= -kBoxTol
//   terminal(20,21,22) >= 0 when lateral_active    -> h >= -kBoxTol (stage N only)
// Rows deactivated via [-kUhInf, +kUhInf] are always satisfied (skip).
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
    int n_targets) {
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

  const int n_t = std::max(0, std::min(n_targets, kAcadosNsh));
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

    // Read per-stage CPA slack xi (length NS=16) for k<N; for stage N there is
    // no slack vector (no control at N) so CPA rows at N use xi=0. In practice
    // CPA rows at the terminal stage are still >= 0 ones; xi=0 is the safe
    // (strict) choice.
    double sl_vec[kAcadosNs] = {0.0};
    if (k < kAcadosN) {
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

    // Per-stage bounds (mirror build_stage_row_bounds). Terminal rows active
    // ONLY at stage N (and only when lateral_active).
    const bool terminal_active = lateral_active && (k == kAcadosN);
    RowBounds rb = build_stage_row_bounds(k, kAcadosNh, lateral_active, n_targets);

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
      // Relaxed row (double-disabled) -> always satisfied.
      if (is_relaxed(lo, hi)) continue;

      // CPA softened rows [kRowCpaBase..kRowCpaBase+n_t-1]: effective lower
      // bound is lo - xi (the slack relaxes the one-sided >= 0). Only the
      // n_targets REAL CPA rows are softened-and-active; empty slots are
      // relaxed (skipped above). For real CPA rows with t < n_t, subtract xi.
      double eff_lo = lo;
      if (r >= kRowCpaBase && r < kRowCpaBase + n_t) {
        const int t = r - kRowCpaBase;
        if (t < kAcadosNsh) {
          eff_lo = lo - sl_vec[t];
        }
      }

      // The actual bound check.
      if (hv < eff_lo - kBoxTol || hv > hi + kBoxTol) {
        // Prefix equality rows (0,1) report |h| > kBoxTol (hi==lo==0).
        const char* row_kind =
            (r == kRowPrefixPsi) ? "prefix_psi" :
            (r == kRowPrefixU)   ? "prefix_u"   :
            (r >= kRowCpaBase && r < kRowCpaBase + kAcadosNsh) ? "cpa" :
            (r == kRowDirection) ? "direction"  :
            (r == kRowMinAlt)    ? "min_alt"    :
            (r == kRowTermSide || r == kRowTermLo || r == kRowTermHi) ? "terminal" :
            "?";
        spdlog::warn("[M5][MidMPC][acados] C1 status-4 REJECT: stage={} row={} "
                     "({}) h={} outside [{}, {}] (eff_lo={}, tol={})",
                     k, r, row_kind, hv, lo, hi, eff_lo, kBoxTol);
        (void)terminal_active;  // bounds already encode the terminal mask.
        return false;
      }
    }
  }
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
  (void)warm_start;  // F1 seed is forward-propagated; warm_start is for parity.
  const auto t_start = std::chrono::steady_clock::now();
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

  // ---- 1a. P2 T4 (VR-07b, review-fix I-1): compute the F1 seed trajectory ----
  // ONCE into a shared per-stage vector. This is the SINGLE forward propagation
  // of the F1 seed (delta->0 hold, n->steady-state hold, path_b_step per stage);
  // BOTH the tb-pack block (1b) and the F1 seed-write loop (step 4) read from
  // it, so the tb closest-point origins and the acatos seed are PROVABLY the
  // same trajectory by construction (no duplicate helper, no drift hazard).
  //
  // The SeedState init + propagation here are byte-for-byte identical to the
  // pre-refactor seed loop: s.px=own_x, s.py=own_y, s.psi=own_psi, s.r=0.0,
  // s.u = own_u>0 ? own_u : planned; n_hold = steady_state_n_for_u(s.u);
  // path_b_step(s, 0.0, n_hold, dt, next). The vector captures the per-stage
  // state BEFORE any ocp_nlp_out_set write, so step 4 simply replays it.
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
  {
    SeedState s = s_seed;  // working copy; advanced by path_b_step below.
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

  // ---- 2. write per-stage concatenated params (143 per stage). ----
  // Concatenate global (106) + per-stage (37) = 143 (codegen's NP). The single-
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
  const bool lateral_active = derive_lateral_active(input);
  const int n_targets = static_cast<int>(
      std::min<std::size_t>(input.targets.size(),
                            static_cast<std::size_t>(formulation_.config().max_targets)));
  for (int k = 0; k <= kAcadosN; ++k) {
    RowBounds rb = build_stage_row_bounds(k, kAcadosNh, lateral_active, n_targets);
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
  const int status = m5_mid_mpc_acados_acados_solve(impl_->capsule);
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
    // Per-target CPA slack (NSH=16 per path stage; "sl" length = NS = 16).
    // Terminal stage N has no sl (no control at N), so skip reading it there.
    if (k < kAcadosN) {
      double sl_vec[kAcadosNs] = {0.0};
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
  // status 1 -> Timeout; status 2 -> Infeasible; status 3/other -> NumFailure.
  MidMpcSolution::Status mapped = map_acados_status(status);
  if (status == 4 && solver_moved) {
    const bool csat = constraints_satisfied_(g, ps, lateral_active, n_targets);
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
  // NaN in the trajectory is ALWAYS a NumericalFailure, regardless of status.
  if (any_nan) {
    mapped = MidMpcSolution::Status::NumericalFailure;
  }

  const auto t_end = std::chrono::steady_clock::now();
  sol.status = mapped;
  sol.solve_duration_ms = static_cast<std::int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());
  sol.cpa_slack = cpa_slack_max;
  sol.cpa_slack_per_target = per_target_max;

  // SQP iter count (the field name stays ipopt_iterations per the output
  // contract — downstream reads it; do NOT rename). ocp_nlp_get "sqp_iter".
  int sqp_iter = 0;
  ocp_nlp_get(impl_->solver, "sqp_iter", &sqp_iter);
  sol.ipopt_iterations = static_cast<std::int32_t>(sqp_iter);
  // TEST-ONLY diagnostic mirror (T17 review-fix Step 1).
  impl_->last_sqp_iter = sqp_iter;

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
  return sol;
}

}  // namespace mass_l3::m5::mid_mpc
