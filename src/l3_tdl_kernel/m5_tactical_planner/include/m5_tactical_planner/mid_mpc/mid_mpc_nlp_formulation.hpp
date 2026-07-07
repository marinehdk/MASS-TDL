#ifndef MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
#define MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_

// M5 Tactical Planner — Mid-MPC NLP Formulation (Task 2.1)
// Builds the parametric CasADi MX symbolic graph for the Mid-MPC NLP.
//
// Graph-build work: build_symbolic_graph() instantiates an nlpsol IPOPT
// Function. It may be called again after set_constraint_inputs() when
// numeric-baked ConstraintCompiler rows change.
//
// Decision variables: x = [psi[0..N-1]; u[0..N-1]] ∈ R^{2N}
// Parameters:         p ∈ R^93 (initial state + bounds + 16 targets)
// Objective:          J = w_colreg * J_colreg + w_dist * J_dist + w_vel * J_vel
// Constraints:        g(x, p) >= 0, dim = ROT rows + active compiler rows
//
// Phase P1 scope:
//   - Soft COLREGs cost (J_colreg) retained as guidance.
//   - ConstraintCompiler rows add hard rule / CPA / zone constraints.
//   - Heading/speed box limits remain IPOPT variable bounds.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no
// bare new/delete. CasADi LGPL-3.0: internal MISRA violations exempted per
// coding-standards.md §10 (dynamic-link boundary).
//
// Parameters marked [TBD-HAZID] must be calibrated during HAZID RUN-001
// (FCB sea trials, target completion 2026-08-19 per
// docs/Design/HAZID/RUN-001-kickoff.md).

#include <casadi/casadi.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/row_registry.hpp"
#include "m5_tactical_planner/shared/constraint_compiler.hpp"

namespace mass_l3::m5::mid_mpc {

// ---------------------------------------------------------------------------
// Parameter vector layout — public constants for caller readability.
// kParamDim is fixed at compile time (16 targets × 5 slots + 13 head slots).
// ---------------------------------------------------------------------------
constexpr int32_t kIdxPsi0           = 0;
constexpr int32_t kIdxU0             = 1;
constexpr int32_t kIdxX0             = 2;
constexpr int32_t kIdxY0             = 3;
constexpr int32_t kIdxRouteBearing   = 4;
constexpr int32_t kIdxPlannedSpeed   = 5;
constexpr int32_t kIdxHeadingMin     = 6;
constexpr int32_t kIdxHeadingMax     = 7;
constexpr int32_t kIdxSpeedMin       = 8;
constexpr int32_t kIdxSpeedMax       = 9;
constexpr int32_t kIdxCpaSafe        = 10;
constexpr int32_t kIdxRotMax         = 11;
constexpr int32_t kIdxOwnPsi         = 12;
constexpr int32_t kIdxGiveWay        = 13;  // 1.0 if M6 rule 14/15 (give-way) active, else 0.0
// Slice R1: route-frame + continuity/direction parameter slots (spec §3.4).
constexpr int32_t kIdxRouteFrameOriginX   = 14;  // route-frame origin, NED north [m]
constexpr int32_t kIdxRouteFrameOriginY   = 15;  // route-frame origin, NED east  [m]
constexpr int32_t kIdxRouteFrameNormalX   = 16;  // active-leg normal unit vector, north comp
constexpr int32_t kIdxRouteFrameNormalY   = 17;  // active-leg normal unit vector, east  comp
constexpr int32_t kIdxRouteFrameBearing   = 18;  // active-leg bearing [rad] (vs kIdxRouteBearing=4 first-leg)
constexpr int32_t kIdxLateralScale        = 19;  // l_scale = GncExecutionOdd.max_lateral_offset_m
constexpr int32_t kIdxRouteWeight         = 20;  // cross-leg guard: 1.0 normal, 0.0 across L2 corner
constexpr int32_t kIdxPrefixActiveK       = 21;  // active prefix length K (C1)
constexpr int32_t kIdxPreferredDir        = 22;  // M6 preferred_direction (D1)
constexpr int32_t kIdxMinAlterationRad    = 23;  // M6 min alteration (D1)
constexpr int32_t kIdxRole                = 24;  // M6 primary_role enum (D1)
constexpr int32_t kIdxDecelMax            = 25;  // Fix D-2: max decel [m/s²] (speed-rate hard constraint)
constexpr int32_t kIdxPrefixPsi           = 26;  // [N=18] prefix psi (C1)
constexpr int32_t kIdxPrefixU             = 44;  // [N=18] prefix u   (C1)
constexpr int32_t kIdxTargets             = 62;
constexpr int32_t kTargetStride          = 5;
constexpr int32_t kMaxTargets            = 16;
constexpr int32_t kParamDim              = kIdxTargets + kMaxTargets * kTargetStride;  // 62+80=142
static_assert(kParamDim == 142, "parameter layout mismatch — update kParamDim if constants change");

class MidMpcNlpFormulation {
 public:
  // -------------------------------------------------------------------------
  // Config — Mid-MPC NLP formulation hyperparameters.
  // All [TBD-HAZID] tunables. Defaults aligned with detailed design §5.2.3.
  // -------------------------------------------------------------------------
  struct Config {
    // [TBD-HAZID] Horizon length N per spec v2 baseline: 18 × 5 s = 90 s.
    int32_t n_horizon{18};
    // Step duration [s] (aligned with L4 LOS period, detailed design §5.2.3).
    double dt_s{5.0};
    // [TBD-HAZID] COLREGs compliance cost weight (~3x route per colav_algorithms NLM).
    double w_colreg{30.0};
    // [TBD-HAZID] Route-track deviation cost weight.
    double w_dist{10.0};
    // [TBD-HAZID] Speed efficiency cost weight.
    double w_vel{1.0};
    // [TBD-HAZID] Route-frame cross-track (J_route) weight. Dimensionless cost
    // (l/l_scale)^2 is O(1). COLREG dominance (spec §3.2): the new R1 term must
    // not suppress avoidance, i.e. w_colreg·J_colreg > w_route·J_route must hold
    // near the CPA hard floor. Spec §3.2 line 115: "若 dominance 不成立，降 w_route
    // 而非升 w_colreg". The default 3.0 is calibrated (not self-certified) from the
    // dominance fixture so w_route·J_route stays below w_colreg·J_colreg at cpa_hard.
    // The full w_colreg·J_colreg > w_route·J_route + w_dist·J_dist contract cannot
    // hold at cpa_hard because J_dist (heading deviation during avoidance) dominates
    // physically; J_dist is itself an avoidance driver, not the new R1 term under test.
    // HAZID RUN-001 to recalibrate (preserve w_colreg·J_colreg > w_route·J_route).
    double w_route{3.0};
    // Phase 3.1/3.7 (spec v2.3 §2.3): CPA slack-variable penalty weight. Large
    // constant so NLP only activates σ when the alternative is geometric
    // infeasibility (exact-penalty form, Kerrigan 2000). σ is a single
    // scalar shared across all targets and horizon steps — keeps graph
    // dimension growth at +1 regardless of target count. Per-target / per-
    // window slack is [TBD-MULTI-SHIP] (spec v2.3 §7).
    //
    // Phase 3.7 (2026-07-05): default raised 1e4 -> 1e8 after V2 probe
    // evidence (run-19f3102d92c). At 1e4 IPOPT found it cheaper to activate
    // σ=482381 m² (cost ~4.8e9) than to perform a real heading alteration
    // (J_colreg + J_route + J_dist + ROT constraint cost). At 1e8 the σ
    // penalty for σ=695 m would be ~4.8e13, dominating every other cost term
    // and forcing real avoidance. [TBD-HAZID-WP-04] recalibrate vs sea trial.
    bool   cpa_slack_enabled{true};
    double w_slack{1.0e8};
    // [TBD-HAZID] Exponential-barrier steepness zeta [1/m] in exp(-zeta*(d-cpa_safe)).
    // ~e-fold per 200 m: strong avoidance gradient inside cpa_safe, ≈0 beyond ~2·cpa.
    double zeta{5.0e-3};
    // [TBD-HAZID] Range-ramp outer distance [m] (6 nm); weight 0 beyond, 1 at cpa_safe.
    double pwt_outer_m{11112.0};
    // [TBD-HAZID] TCPA discount time constant [s] in exp(-t_k/T_d).
    double t_discount_s{100.0};
    // [TBD-HAZID] Starboard asymmetry weight (softplus port penalty, give-way only).
    double k_asym{50.0};
    // [TBD-HAZID] Asymmetry smoothing scale [rad] (~5 deg).
    double asym_tau{0.0873};
    // ── Slice T1: terminal side/bound tunables (spec §5.4 / §5.5) ───────────
    // [TBD-HAZID] J_terminal softplus smoothing scale (dimensionless, on l/l_scale).
    // Default 0.5: l_scale-normalized, so the softplus transition spans ~O(1) of
    // the normalized lateral (≈400 m at the default l_scale). Same family as
    // asym_tau (a softplus scale); chosen O(1) on the normalized lateral axis
    // rather than the rad axis. HAZID RUN-001 to recalibrate.
    double terminal_tau{0.5};
    // [TBD-HAZID] Terminal same-side minimum feasible lateral [m] (spec §5.5
    // g_term_side: pref_dir·l[N-1] ≥ l_min_feasible). COLREG apparent-action
    // minimum offset so the avoidance tail is not negligibly small. Default
    // 30 m ≈ 0.16 nm (small but non-zero apparent action).
    double terminal_l_min_feasible_m{30.0};
    // [TBD-HAZID] Terminal lateral feasibility upper bound [m] (spec §5.5
    // g_term_lo/hi: |l[N-1]| ≤ l_max_feasible via two linear rows). =
    // min(GncExecutionOdd.max_lateral_offset_m=400, TailBuilder rejoin upper
    // bound). Default 400 m (the GncExecutionOdd cap); HAZID to clamp to the
    // TailBuilder geometry once measured.
    double terminal_l_max_feasible_m{400.0};
    // Max obstacle count per cycle (parametric upper bound, must be ≤ kMaxTargets).
    int32_t max_targets{kMaxTargets};
  };

  explicit MidMpcNlpFormulation(const Config& cfg);

  // Build or rebuild symbolic NLP graph. Caches nlpsol Function.
  void build_symbolic_graph();

  // Set current runtime constraints before rebuilding the symbolic graph.
  void set_constraint_inputs(const ConstraintInputs& inputs) {
    constraint_inputs_ = inputs;
  }

  // Cached nlpsol Function (called by MidMpcSolver per cycle).
  [[nodiscard]] const casadi::Function& solver() const noexcept { return solver_; }
  // Fix #8: true after at least one successful build_symbolic_graph() call.
  [[nodiscard]] bool solver_valid() const noexcept { return !solver_.is_null(); }

  // Pack MidMpcInput into IPOPT parameter vector p ∈ R^kParamDim.
  [[nodiscard]] casadi::DM pack_parameters(const MidMpcInput& input) const;

  // Unpack IPOPT solution x* + solver stats into MidMpcSolution.
  [[nodiscard]] MidMpcSolution unpack_solution(
      const casadi::DM& x_opt,
      const casadi::Dict& stats) const;

  // Constraint dimension (used by MidMpcSolver for lbg/ubg sizing).
  [[nodiscard]] int32_t g_dim() const noexcept;

  // Public access to active config (read-only).
  [[nodiscard]] const Config& config() const noexcept { return cfg_; }

  // ── Slice N1: row registry ───────────────────────────────────────────────
  // Per-cycle fixed-class g row layout (spec §3.8). Built during
  // build_symbolic_graph() from the compiled ConstraintInputs (n_targets /
  // rule_rows / zone_rows). The MidMpcSolver uses it to build per-class
  // lbg/ubg via RowRegistry::build_bounds(RowBoundConfig).
  // @pre build_symbolic_graph() has been called.
  [[nodiscard]] const RowRegistry& row_registry() const noexcept {
    return row_registry_;
  }

  // ── Slice R1: cost-component evaluators (spec §3.2 / §10.1) ───────────────
  // Evaluate an individual (unweighted) cost sub-term at a given (x, p). Used by
  // unit tests to assert the COLREG-dominance contract without relying on
  // unpack_solution (Phase E1 does not split cost components). @pre graph built.
  [[nodiscard]] double eval_colreg_cost(const casadi::DM& x, const casadi::DM& p) const;
  [[nodiscard]] double eval_dist_cost(const casadi::DM& x, const casadi::DM& p) const;
  [[nodiscard]] double eval_route_cost(const casadi::DM& x, const casadi::DM& p) const;
  // Slice T1: terminal wrong-side softplus cost evaluator (spec §5.4 / §10.1).
  [[nodiscard]] double eval_terminal_cost(const casadi::DM& x, const casadi::DM& p) const;

 private:
  Config cfg_;
  casadi::MX psi_;     // [N×1] symbolic decision variable: heading sequence [rad]
  casadi::MX u_;       // [N×1] symbolic decision variable: speed sequence [m/s]
  // Phase 3.1 (spec v2.3 §2.1): CPA slack — single scalar shared across all
  // targets and horizon steps. Empty MX when cfg_.cpa_slack_enabled=false.
  casadi::MX sigma_;
  casadi::MX p_;       // [kParamDim×1] parameter vector
  casadi::MX J_;       // objective expression
  casadi::MX g_;       // constraint vector (g >= 0 convention)
  casadi::Function solver_;  // nlpsol-cached IPOPT Function
  int32_t g_dim_{0};
  // Slice N1: per-cycle row registry (built in build_constraints_, which is const;
  // mutable mirrors the symbolic-graph caching pattern). Spec §3.8.
  mutable RowRegistry row_registry_;
  mass_l3::m5::shared::ConstraintCompiler compiler_{};
  ConstraintInputs constraint_inputs_{};

  // Dimension helper (kept for symmetry with CasADi MX::sym signature).
  [[nodiscard]] static int32_t parameter_dim_() noexcept { return kParamDim; }

  // Objective sub-terms (called from build_symbolic_graph).
  [[nodiscard]] casadi::MX build_colreg_cost_() const;
  [[nodiscard]] casadi::MX build_asym_cost_() const;
  [[nodiscard]] casadi::MX build_distance_cost_() const;
  [[nodiscard]] casadi::MX build_velocity_cost_() const;
  // Slice R1: route-frame dimensionless cross-track cost (spec §4.3).
  [[nodiscard]] casadi::MX build_route_cost_() const;
  // Slice T1: terminal wrong-side softplus cost (spec §5.4, smooth, no max/abs).
  [[nodiscard]] casadi::MX build_terminal_cost_() const;
  // Slice T1: terminal cross-track l[N-1] = (pos[N-1] - origin)·n_hat, where
  // pos[N-1] is integrated from the own current position (same contract as
  // build_route_cost_, spec §3.1/§4.2). Shared by build_terminal_cost_ and the
  // terminal hard rows so the l[N-1] used in the cost and the constraint is
  // identical (extracted to avoid duplicating the position integral).
  [[nodiscard]] casadi::MX compute_terminal_cross_track_() const;
  // Slice D1: full cross-track sequence l[k] for k ∈ [0,N) (spec §7.1). Returns
  // one MX per step so build_constraints_ can assemble the direction rows
  // g_dir[k] = pref_dir·l[k] (§7.1). Uses the SAME position integral as
  // build_route_cost_ (pos[k] = x0 + Σ_{j<k} u[j]·dt·(cos,sin)(psi[j]), spec §3.1)
  // and the SAME own-relative origin/normal (R1 Critical-2). Kept separate from
  // compute_terminal_cross_track_ (which returns a single l[N-1]) because the
  // direction/min_alt rows need the WHOLE sequence, and extracting it lets the
  // route cost / terminal cost reuse the identical kinematics without coupling.
  [[nodiscard]] std::vector<casadi::MX> compute_cross_track_all_() const;

  // Constraint helper.
  [[nodiscard]] casadi::MX build_constraints_() const;
};

inline MidMpcNlpFormulation::Config resolve_mid_mpc_horizon_config(
    const MidMpcNlpFormulation::Config& cfg,
    double horizon_s,
    int64_t n_steps,
    double dt_s) {
  MidMpcNlpFormulation::Config resolved = cfg;
  resolved.dt_s = dt_s;
  const int64_t horizon_steps = std::max<int64_t>(
      2, static_cast<int64_t>(std::lround(horizon_s / resolved.dt_s)));
  const bool explicit_nondefault_steps = n_steps > 1 &&
      n_steps != static_cast<int64_t>(cfg.n_horizon);
  resolved.n_horizon = explicit_nondefault_steps
      ? static_cast<int32_t>(std::min<int64_t>(n_steps, 120))
      : static_cast<int32_t>(std::min<int64_t>(horizon_steps, 120));
  return resolved;
}

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
