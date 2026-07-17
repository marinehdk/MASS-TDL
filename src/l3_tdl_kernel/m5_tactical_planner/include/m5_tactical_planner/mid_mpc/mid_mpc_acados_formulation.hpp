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
//   u_surge[k+1]= u_surge + DT * (k_prop*n^2 - k_drag*u_surge^2) / m_sge
//   px[k+1]     = px      + u_surge * DT * cos(psi)
//   py[k+1]     = py      + u_surge * DT * sin(psi)
//
// Coefficients are VDM-direct literals (not invented):
//   c_u    = 9.825342e-3   (P1b-1a T8, = k_n_rudder * u^2 / izz_e at cruise)
//   k_prop = 500.0         (vessel_dynamics_model.cpp:47)
//   k_drag = 100.0         (vessel_dynamics_model.cpp:48)
//   m_sge  = 152250.0      (mass_kg*(1+surge_added_mass_factor) =
//                           145000*1.05; vessel_dynamics_model.cpp:43 +
//                           capability_manifest.hpp:45,79). The surge accel is
//                           MASS-NORMALIZED exactly as VDM ground truth: the
//                           raw (k_prop*n^2 - k_drag*u^2) is a FORCE [N], so it
//                           must be divided by m_sge [kg] to yield m/s^2. Omitting
//                           /m_sge makes surge accel ~152250x too large (T15 F1).
//
// Parameter partition (T15 F2/F4 — acados per-stage expansion, documented
// deviation from the IPOPT flat kParamDim==142):
//   global     (np_global   = 106): 26 IPOPT head scalars (kIdx 0-25) +
//                                   16x5 target block (kIdx 62-141, remapped to
//                                   global 26-105). The target block keeps
//                                   (tx,ty,cog,sog,tw) so the solver/node can
//                                   recompute/reproject per-stage drift.
//   per-stage  (np_per_stage = 35): per-stage scalars that stage k needs:
//         [0]      prefix_psi_at_k  — prefix psi equality target (C1, F2)
//         [1]      prefix_u_at_k    — prefix u   equality target (C1, F2)
//         [2]      pact_pre         — prefix activation (1.0 if k<K else 0.0, F2)
//         [3..18]  target_x_at_k[t] — drifted target x per target (F4)
//         [19..34] target_y_at_k[t] — drifted target y per target (F4)
//   np_per_stage = 3 + 2*Nt = 35 at default Nt=16.
//
// Why this is no longer exactly 142: IPOPT packs a single flat 142-vector and
// computes per-stage target drift symbolically inside the graph from the global
// (cog,sog). acados receives a stage-uniform graph that CANNOT index the stage
// k, so per-stage drift (F4) and the prefix-equality activation factor (F2) must
// be precomputed per-stage and delivered via update_params. The GLOBAL block
// stays 106 (IPOPT 26 head + 80 target — 142-compatible for the stage-uniform
// portion); the per-stage block is the documented acados expansion. The static
// contract below pins the per-stage count and documents the deviation rather
// than asserting a false 142.
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

// Per-stage parameter layout (T15 F2/F4). Each stage k carries only the scalars
// that stage needs (NOT the whole prefix sequence — the single-stage graph reads
// fixed offsets that hold stage k's values):
//   [0]                       prefix_psi_at_k
//   [1]                       prefix_u_at_k
//   [2]                       pact_pre (prefix activation, 1.0 if k<K else 0.0)
//   [3 .. 3+Nt-1]             target_x_at_k[t]
//   [3+Nt .. 3+2Nt-1]         target_y_at_k[t]
constexpr int32_t kAcadosNDefault = 18;               // production default N
constexpr int32_t kAcadosPerStagePrefixPsiOff = 0;
constexpr int32_t kAcadosPerStagePrefixUOff   = 1;
constexpr int32_t kAcadosPerStagePactPreOff   = 2;
constexpr int32_t kAcadosPerStageTgtDriftOff  = 3;  // target_x_at_k[0] starts here
constexpr int32_t kAcadosNpPerStageDefault =
    kAcadosPerStageTgtDriftOff + 2 * kAcadosMaxTargets;  // 3 + 32 = 35

// Static contract (T15 F2/F4): the GLOBAL block stays 142-compatible with the
// IPOPT stage-uniform portion (26 head + 80 target = 106). The per-stage block
// is the documented acados expansion (prefix scalars + activation + per-stage
// target drift); it is NOT summed to 142 because acados precomputes per-stage
// drift/activation that IPOPT folds into its flat 142-vector + per-row bounds.
// See the partition doc above. This asserts the per-stage count is stable.
static_assert(kAcadosNpPerStageDefault == 35,
              "acados np_per_stage(default) = 3 + 2*Nt = 35; update if Nt changes");
static_assert(kAcadosNpGlobal == 106,
              "acados np_global = 26 head + 80 target = 106 (IPOPT-compatible)");

// Production acados OCP symbol graph (MX). Path B 5-dim state, 2-dim control,
// 6 costs + full constraints + 106-global / 35-per-stage partition.
//
// This class ONLY builds the CasADi MX symbol graph and packs parameters. It
// does NOT call acatos codegen (that is gen_mid_mpc_acados.py) and does NOT
// solve (that is Task 16 MidMpcAcadosSolver). M5_USE_ACADOS=ON selects the
// acados backend; the IPOPT MidMpcNlpFormulation stays the default otherwise.
class MidMpcAcadosFormulation {
 public:
  // Parameter dimension accounting (T15 F2/F4 documented deviation):
  //   kParamDimGlobal = 106 (IPOPT stage-uniform portion: 26 head + 80 target)
  //   kParamDimPerStage = 35 (acados per-stage expansion: prefix + act + drift)
  // IPOPT's flat kParamDim==142 is preserved in the IPOPT formulation; the
  // acados backend expands per-stage (drift precomputed, activation factor)
  // because the single-stage graph cannot index stage k. See partition doc.
  static constexpr int32_t kParamDimGlobal    = kAcadosNpGlobal;          // 106
  static constexpr int32_t kParamDimPerStage  = kAcadosNpPerStageDefault; // 35
  // Production default horizon N (horizon_s=90s / dt=5s -> N=18; node-config
  // overrides via resolve_mid_mpc_horizon_config, clamped to 120 max).
  static constexpr int32_t kNDefault = kAcadosNDefault;
  // Step duration [s] (IPOPT dt_s default 5.0, aligned with L4 LOS period).
  static constexpr double kDt = 5.0;
  // Path B double-integrator yaw gain (P1b-1a T8 VDM-direct, not invented).
  // = k_n_rudder * u^2 / izz_e at cruise; verified analytically in T8.
  static constexpr double kC_u = 9.825342e-3;  // rad/s^2 per rad
  // VDM-direct surge model coefficients (vessel_dynamics_model.cpp:47-48) and
  // the surge effective mass m_sge = mass_kg*(1+surge_added_mass_factor)
  // (vessel_dynamics_model.cpp:43; capability_manifest.hpp:45 mass_kg=145000,
  // :79 surge_added_mass_factor=0.05 -> m_sge = 152250). The graph divides the
  // raw thrust/drag force by m_sge to match VDM ground truth (T15 F1).
  static constexpr double kKProp = 500.0;
  static constexpr double kKDrag = 100.0;
  static constexpr double kMSge  = 145000.0 * (1.0 + 0.05);  // 152250.0
  // Mass-normalized effective coefficients (baked into the graph so the surge
  // accel expression is (kKPropPerMass*n^2 - kKDragPerMass*u^2), i.e. already
  // divided by m_sge — mathematically identical to /m_sge on the raw force).
  static constexpr double kKPropPerMass = kKProp / kMSge;  // 0.0032840...
  static constexpr double kKDragPerMass = kKDrag / kMSge;  // 0.0006568...

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
  //   second = per-stage params (N+1 rows of np_per_stage=35), one row per
  //            acatos stage 0..N. Each row k carries the per-stage scalars the
  //            single-stage graph reads at fixed offsets:
  //              [0] prefix_psi_at_k, [1] prefix_u_at_k, [2] pact_pre,
  //              [3..3+Nt-1] target_x_at_k[t], [3+Nt..3+2Nt-1] target_y_at_k[t].
  //            (T15 F2 prefix lock + F4 per-stage target drift.)
  // Mirrors IPOPT pack_parameters semantics (formulation.cpp:670-787) but
  // splits global vs per-stage so acados can update them independently.
  [[nodiscard]] std::pair<std::vector<double>, std::vector<std::vector<double>>>
  pack_parameters(const MidMpcInput& input) const;

  // ---- Accessors (for codegen parity + Task 16 solver) ----
  const std::string& solver_name() const noexcept { return solver_name_; }
  int nx() const noexcept;            // 5 (Path B)
  int nu() const noexcept;            // 2 (delta, n)
  int nh() const noexcept;            // h rows: prefix(2) + CPA(Nt) + dir + min_alt + terminal(3)
  int np_global() const noexcept;     // 106
  int np_per_stage() const noexcept;  // 3 + 2*Nt (35 at default Nt=16)
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
  // control u=[delta,n] (2). p_global (106) is stage-uniform; p_stage (35)
  // is per-stage (prefix psi/u scalars + pact_pre + per-target drift x/y).
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

  // Per-stage param slot helpers (fixed offsets — the single-stage graph reads
  // stage k's value at a fixed offset; pack_parameters writes a different value
  // to that offset in each stage's per-stage vector). T15 F2/F4.
  [[nodiscard]] casadi::MX prefix_psi_at_k_slot_() const;  // p_stage[0]
  [[nodiscard]] casadi::MX prefix_u_at_k_slot_() const;    // p_stage[1]
  [[nodiscard]] casadi::MX pact_pre_slot_() const;         // p_stage[2]
  [[nodiscard]] casadi::MX target_x_at_k_slot_(int32_t t) const;  // p_stage[3+t]
  [[nodiscard]] casadi::MX target_y_at_k_slot_(int32_t t) const;  // p_stage[3+Nt+t]
  // Global param slot helper (scalar p_global[i]).
  [[nodiscard]] casadi::MX gslot_(int32_t i) const;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
