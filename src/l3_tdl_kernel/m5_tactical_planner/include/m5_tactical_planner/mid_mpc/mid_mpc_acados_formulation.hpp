#ifndef MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
#define MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_

// M5 Tactical Planner — Mid-MPC production acados OCP formulation (Task 15,
// P1b-1b). Builds the CasADi MX symbolic graph for the production acados OCP,
// parallel to the existing IPOPT MidMpcNlpFormulation (which is READ-ONLY
// reference — never modified here).
//
// Path B 5-dim state (spec amendment 2026-07-17):
//   state   x = [px, py, psi, r, u_surge]   (nx = 5)
//   control u = [delta, n]                  (nu = 2  — rudder angle + rpm)
//
// Discrete dynamics (DISCRETE integrator, explicit Euler — surge as STATE so
// the rpm control has a real dynamics path, per the 2026-07-17 user ruling):
//   r[k+1]      = r       + DT * c_u * delta
//   psi[k+1]    = psi     + DT * r
//   u_surge[k+1]= u_surge + DT * (k_prop*n^2 - k_drag*u_surge^2)
//   px[k+1]     = px      + u_surge * DT * cos(psi)
//   py[k+1]     = py      + u_surge * DT * sin(psi)
//
// Coefficients are VDM-direct literals (not invented):
//   c_u    = 9.825342e-3   (P1b-1a T8, = k_n_rudder * u^2 / izz_e at cruise)
//   k_prop = 500.0         (vessel_dynamics_model.cpp:47)
//   k_drag = 100.0         (vessel_dynamics_model.cpp:48)
//
// Parameter partition (IPOPT kParamDim==142 contract preserved):
//   global     (np_global   = 106): 26 IPOPT head scalars (kIdx 0-25) +
//                                   16x5 target block (kIdx 62-141)
//   per-stage  (np_per_stage = 36): prefix psi[N] + prefix u[N]
//                                   (kIdx 26-61, N=18 default)
//   sum = 142 (== MidMpcNlpFormulation::kParamDim).
//
// MX in .cpp (symbol-graph contract + pack logic), SX in gen script (codegen).
// Both are mathematically identical (SX/MX are equivalent at the acados layer;
// acados_template SX support is mature, MX has limits — locked decision).
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no bare
// new/delete. CasADi LGPL-3.0: internal MISRA violations exempted per
// coding-standards.md §10 (dynamic-link boundary).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"  // MidMpcInput

namespace mass_l3::m5::mid_mpc {

// ===========================================================================
// Global parameter layout (stage-uniform, np_global = 106).
// Mirrors IPOPT kIdx 0-25 (26 head scalars) + kIdx 62-141 (16x5 target block).
// These are the values acados sets ONCE per cycle (stage-uniform); per-stage
// values (prefix sequence) live in the per-stage block below.
// ===========================================================================
constexpr int32_t kAcadosNpGlobalHeadScalars = 26;   // kIdx 0-25
constexpr int32_t kAcadosMaxTargets          = 16;   // == kMaxTargets (nlp_formulation.hpp)
constexpr int32_t kAcadosTargetStride        = 5;    // == kTargetStride
constexpr int32_t kAcadosNpGlobalTargetBlock =
    kAcadosMaxTargets * kAcadosTargetStride;         // 80
constexpr int32_t kAcadosNpGlobal =
    kAcadosNpGlobalHeadScalars + kAcadosNpGlobalTargetBlock;  // 106

// Per-stage parameter layout (np_per_stage = 2*N_default = 36).
// The prefix psi[N] + prefix u[N] sequence (IPOPT kIdx 26-61). These are the
// ONLY genuinely stage-indexed values in the IPOPT 142 layout; acados receives
// them via the generated <name>_acados_update_params(capsule, stage, vals, np).
constexpr int32_t kAcadosNDefault = 18;               // production default N
constexpr int32_t kAcadosNpPerStageDefault = 2 * kAcadosNDefault;  // 36

// Static contract: global + per-stage(default N) == IPOPT kParamDim (142).
static_assert(kAcadosNpGlobal + kAcadosNpPerStageDefault == 142,
              "acados global + per-stage(default) must total IPOPT kParamDim=142");

// ===========================================================================
// Per-stage prefix psi/u offsets inside the per-stage param vector.
//   per_stage[0 .. N-1]      = prefix psi (kIdxPrefixPsi relative)
//   per_stage[N .. 2N-1]     = prefix u   (kIdxPrefixU   relative)
// ===========================================================================
constexpr int32_t kAcadosPerStagePrefixPsiOffset = 0;
// prefix u follows the N prefix-psi entries; offset depends on N (computed in
// the .cpp via cfg_.n_horizon). Documented here for the gen-script parity.

// Production acados OCP symbol graph (MX). Path B 5-dim state, 2-dim control,
// 6 costs + full constraints + 142-param global/per-stage partition.
//
// This class ONLY builds the CasADi MX symbol graph and packs parameters. It
// does NOT call acatos codegen (that is gen_mid_mpc_acados.py) and does NOT
// solve (that is Task 16 MidMpcAcadosSolver). M5_USE_ACADOS=ON selects the
// acados backend; the IPOPT MidMpcNlpFormulation stays the default otherwise.
class MidMpcAcadosFormulation {
 public:
  // Parameter dimension (IPOPT kParamDim contract; static_assert 142 in
  // nlp_formulation.hpp:78). global(106) + per-stage(36) at default N=18.
  static constexpr int32_t kParamDim = 142;
  // Production default horizon N (horizon_s=90s / dt=5s -> N=18; node-config
  // overrides via resolve_mid_mpc_horizon_config, clamped to 120 max).
  static constexpr int32_t kNDefault = kAcadosNDefault;
  // Step duration [s] (IPOPT dt_s default 5.0, aligned with L4 LOS period).
  static constexpr double kDt = 5.0;
  // Path B double-integrator yaw gain (P1b-1a T8 VDM-direct, not invented).
  // = k_n_rudder * u^2 / izz_e at cruise; verified analytically in T8.
  static constexpr double kC_u = 9.825342e-3;  // rad/s^2 per rad
  // VDM-direct surge model coefficients (vessel_dynamics_model.cpp:47-48).
  static constexpr double kKProp = 500.0;
  static constexpr double kKDrag = 100.0;

  // Config — acados OCP formulation hyperparameters. Defaults mirror the IPOPT
  // MidMpcNlpFormulation::Config so the two backends are interchangeable.
  struct Config {
    int32_t n_horizon{kNDefault};   // horizon N (production default 18)
    double dt_s{kDt};               // step duration [s]
    // Cost weights (mirror IPOPT Config; [TBD-HAZID] recalibrate at RUN-001).
    double w_colreg{30.0};
    double w_dist{10.0};
    double w_route{3.0};
    double w_vel{1.0};
    double k_asym{50.0};
    double terminal_tau{0.5};
    // CPA per-target slack penalty (exact-penalty; IPOPT w_slack analog).
    // P1b-1a T7/T9: per-target xi slack on CPA rows, mixed L1/L2.
    double w_slack{1.0e8};
    bool   cpa_slack_enabled{true};
    int32_t max_targets{kAcadosMaxTargets};
  };

  // Default-construct with production defaults, or pass an explicit Config.
  // (Two ctors instead of `= Config{}` default arg: NSDMI on the nested Config
  // struct is not usable in a default argument while the enclosing class is
  // still incomplete — GCC 11 rejects it.)
  MidMpcAcadosFormulation();
  explicit MidMpcAcadosFormulation(Config cfg);

  // Build the MX symbol graph: state/control/param symbols, disc_dyn_expr_,
  // con_h_expr_, 6 cost expressions, box bounds. Idempotent. Does NOT codegen.
  void build_symbolic_graph();

  // Pack MidMpcInput into the acados parameter partition.
  //   first  = global params (np_global=106), stage-uniform.
  //   second = per-stage params (N+1 rows of np_per_stage=2*N), one row per
  //            acados stage 0..N (terminal stage carries a per-stage block too).
  // Mirrors IPOPT pack_parameters semantics (formulation.cpp:670-787) but
  // splits global vs per-stage so acados can update them independently.
  [[nodiscard]] std::pair<std::vector<double>, std::vector<std::vector<double>>>
  pack_parameters(const MidMpcInput& input) const;

  // ---- Accessors (for codegen parity + Task 16 solver) ----
  const std::string& solver_name() const noexcept { return solver_name_; }
  int nx() const noexcept;            // 5 (Path B)
  int nu() const noexcept;            // 2 (delta, n)
  int nh() const noexcept;            // constraint h rows (per-target CPA + dir + ...)
  int np_global() const noexcept;     // 106
  int np_per_stage() const noexcept;  // 2*N (36 at default N)
  int n_horizon() const noexcept { return cfg_.n_horizon; }

  // CasADi MX graph handles (for the codegen script / solver to consume).
  // Empty until build_symbolic_graph() is called.
  const casadi::MX& disc_dyn_expr() const noexcept { return disc_dyn_expr_; }
  const casadi::MX& con_h_expr() const noexcept { return con_h_expr_; }
  const casadi::MX& x_sym() const noexcept { return x_; }
  const casadi::MX& u_sym() const noexcept { return u_; }
  const casadi::MX& p_global_sym() const noexcept { return p_global_; }
  const casadi::MX& p_stage_sym() const noexcept { return p_stage_; }
  bool graph_valid() const noexcept { return !disc_dyn_expr_.is_null(); }

  const Config& config() const noexcept { return cfg_; }

 private:
  Config cfg_;
  std::string solver_name_{"m5_mid_mpc_acados"};

  // CasADi MX symbol-graph members. State x=[px,py,psi,r,u_surge] (5),
  // control u=[delta,n] (2). p_global (106) is stage-uniform; p_stage (2*N)
  // is per-stage (prefix psi[N] + prefix u[N]).
  casadi::MX x_, u_, p_global_, p_stage_;
  casadi::MX disc_dyn_expr_;  // x[k+1] = f_disc(x[k], u[k], p)  (5 rows)
  casadi::MX con_h_expr_;     // nonlinear path constraint h(x,u,p) (nh rows)

  // Cost expressions (one per IPOPT build_*_cost_; evaluated at the symbol
  // level so the codegen script can mirror them in SX).
  casadi::MX J_colreg_, J_dist_, J_route_, J_vel_, J_asym_, J_terminal_;

  // Helpers (defined in the .cpp; mirror IPOPT build_*_cost_ / constraints).
  [[nodiscard]] casadi::MX build_disc_dyn_() const;
  [[nodiscard]] casadi::MX build_con_h_() const;
  [[nodiscard]] casadi::MX build_colreg_cost_() const;
  [[nodiscard]] casadi::MX build_dist_cost_() const;
  [[nodiscard]] casadi::MX build_route_cost_() const;
  [[nodiscard]] casadi::MX build_vel_cost_() const;
  [[nodiscard]] casadi::MX build_asym_cost_() const;
  [[nodiscard]] casadi::MX build_terminal_cost_() const;

  // Per-stage param slot helper (prefix psi at k, prefix u at k).
  [[nodiscard]] casadi::MX prefix_psi_slot_(int32_t k) const;
  [[nodiscard]] casadi::MX prefix_u_slot_(int32_t k) const;
  // Global param slot helper (scalar p_global[i]).
  [[nodiscard]] casadi::MX gslot_(int32_t i) const;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
