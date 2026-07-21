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
// Parameter partition (T15 F2/F4 + P2 T3 — acados per-stage expansion,
// documented deviation from the IPOPT flat kParamDim==142):
//   global     (np_global   = 106): 26 IPOPT head scalars (kIdx 0-25) +
//                                   16x5 target block (kIdx 62-141, remapped to
//                                   global 26-105). The target block keeps
//                                   (tx,ty,cog,sog,tw) so the solver/node can
//                                   recompute/reproject per-stage drift.
//   per-stage  (np_per_stage = 39): per-stage scalars that stage k needs:
//         [0]      prefix_psi_at_k  — prefix psi equality target (C1, F2)
//         [1]      prefix_u_at_k    — prefix u   equality target (C1, F2)
//         [2]      pact_pre         — prefix activation (1.0 if k<K else 0.0, F2)
//         [3..18]  target_x_at_k[t] — drifted target x per target (F4)
//         [19..34] target_y_at_k[t] — drifted target y per target (F4)
//         [35]     tb_x             — per-stage t_b closest-point x (VR-07b T3)
//         [36]     tb_y             — per-stage t_b closest-point y (VR-07b T3)
//         [37]     psi_prev         — last cycle heading at this stage (P5 T2)
//         [38]     u_prev           — last cycle surge speed at this stage (P5 T2)
//   np_per_stage = 3 + 2*Nt + 2 tb + 2 transition = 39 at default Nt=16.
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
constexpr int32_t kAcadosTargetStride        = 8;    // P7: was 5 (intent_conf, compliance, class)
constexpr int32_t kAcadosNpGlobalTargetBlock =
    kAcadosMaxTargets * kAcadosTargetStride;         // 128 = 16*8
// Step5 方案 B (VR-01 final, 2026-07-20): kGIdxCpaHard is appended at the END of
// the existing global block (right after the target block), so the 26 head
// scalars AND the 128-slot target block keep their offsets (minimal regression
// surface). kGIdxTargets stays at 26; the CPA per-target rows still pack into
// the 128-slot target block; only one NEW slot is appended for the hard floor.
// The CPA per-target constraint residual (build_con_h_) reads cpa_hard_m from
// this slot instead of the bumped cpa_safe — making the CPA row a TRUE hard
// floor (nsh=0 in gen_mid_mpc_acados.py, no slack absorbs it). The soft 2500
// aspiration is expressed ONLY by J_colreg's exp barrier (kGIdxCpaSafe).
constexpr int32_t kAcadosGIdxCpaHard =
    kAcadosNpGlobalHeadScalars + kAcadosNpGlobalTargetBlock;  // 154 (appended)
constexpr int32_t kAcadosNpGlobal =
    kAcadosNpGlobalHeadScalars + kAcadosNpGlobalTargetBlock + 1;  // 155 = 26 + 128 + 1

// Public alias for the soft-aspiration slot index (Step5 方案 B code-review M1):
// kGIdxCpaSafe lives in the anonymous namespace inside mid_mpc_acados_formulation.cpp
// (it is a legacy head-scalar slot), so it is not directly nameable from other
// translation units. This public alias mirrors its value (10) so the solver
// wrapper (mid_mpc_acados_solver.cpp constraints_satisfied_) and tests can refer
// to it by name instead of hardcoding the literal — keeping the slot arithmetic
// auditable. If the head-scalar layout ever changes, update kGIdxCpaSafe in the
// .cpp AND this alias together (they MUST agree).
constexpr int32_t kAcadosGIdxCpaSafe = 10;  // legacy head-scalar slot for cpa_safe_m

// Per-stage parameter layout (T15 F2/F4 + P2 T3 + P5 T2 + P7 σ_pos). Each stage
// k carries only the scalars that stage needs:
//   [0]                       prefix_psi_at_k
//   [1]                       prefix_u_at_k
//   [2]                       pact_pre (prefix activation, 1.0 if k<K else 0.0)
//   [3 .. 3+Nt-1]             target_x_at_k[t]
//   [3+Nt .. 3+2Nt-1]         target_y_at_k[t]
//   [3+2Nt]                   tb_x (per-stage t_b closest-point x, VR-07b T3)
//   [3+2Nt+1]                 tb_y (per-stage t_b closest-point y, VR-07b T3)
//   [3+2Nt+2 .. 3+2Nt+1+Nt]  sigma_pos_at_k[t] (P7: per-target σ_pos, Nt=16)
//   [3+2Nt+2+Nt]              psi_prev_at_k (last cycle ψ at this stage, P5 T2)
//   [3+2Nt+3+Nt]              u_prev_at_k   (last cycle u at this stage, P5 T2)
//   [3+2Nt+4+Nt]              w_trans_active_at_k (1.0 if prev solution exists,
//                              0.0 for first cycle/no cache) (P5 T2)
constexpr int32_t kAcadosNDefault = 80;               // P4 N=80 dt=15 1200s (was 18)
constexpr int32_t kAcadosPerStagePrefixPsiOff = 0;
constexpr int32_t kAcadosPerStagePrefixUOff   = 1;
constexpr int32_t kAcadosPerStagePactPreOff   = 2;
constexpr int32_t kAcadosPerStageTgtDriftOff  = 3;  // target_x_at_k[0] starts here
constexpr int32_t kAcadosPerStageTbXOff =
    kAcadosPerStageTgtDriftOff + 2 * kAcadosMaxTargets;  // 3 + 32 = 35 (VR-07b T3)
constexpr int32_t kAcadosPerStageTbYOff = kAcadosPerStageTbXOff + 1;  // 36
// P7: per-target σ_pos (OU uncertainty) slots, Nt=16
constexpr int32_t kAcadosPerStageSigmaPosOff = kAcadosPerStageTbYOff + 1;  // 37
// P5 T2: transition cost per-stage parameters (shifted by P7 σ_pos block).
constexpr int32_t kAcadosPerStagePsiPrevOff =
    kAcadosPerStageSigmaPosOff + kAcadosMaxTargets;  // 37 + 16 = 53
constexpr int32_t kAcadosPerStageUPrevOff = kAcadosPerStagePsiPrevOff + 1;  // 54
constexpr int32_t kAcadosPerStageWTransActiveOff = kAcadosPerStageUPrevOff + 1;  // 55
constexpr int32_t kAcadosNpPerStageDefault =
    kAcadosPerStageWTransActiveOff + 1;  // 56

// PUBLIC global-slot indices for the route-frame scalars the solver pack needs
// to read by offset (P2 T4): the per-stage t_b computation reads the active-leg
// origin / bearing / normal + planned speed from the GLOBAL param vector the
// solver already packs via pack_parameters. These alias the SAME values as the
// anonymous-namespace kGIdx* constants in the .cpp (single source of truth —
// the values are pinned by IPOPT kIdx parity and asserted via the
// static_asserts below). Exposed publicly so the solver .cpp (a separate TU)
// can index g[] without magic numbers; mirrors how the per-stage offsets above
// are already public.
constexpr int32_t kAcadosGIdxRouteFrameOriginX = 14;
constexpr int32_t kAcadosGIdxRouteFrameOriginY = 15;
constexpr int32_t kAcadosGIdxRouteFrameNormalX = 16;
constexpr int32_t kAcadosGIdxRouteFrameNormalY = 17;
constexpr int32_t kAcadosGIdxRouteFrameBearing = 18;
constexpr int32_t kAcadosGIdxPlannedSpeed      = 5;

// Static contract (T15 F2/F4 + P2 T3): the GLOBAL block stays 142-compatible
// with the IPOPT stage-uniform portion (26 head + 80 target = 106). The
// per-stage block is the documented acatos expansion (prefix scalars +
// activation + per-stage target drift + per-stage t_b closest-point); it is
// NOT summed to 142 because acatos precomputes per-stage drift/activation and
// the per-stage t_b (VR-07b) that IPOPT folds into its flat 142-vector +
// per-row bounds. See the partition doc above. This asserts the per-stage
// count is stable.
static_assert(kAcadosNpPerStageDefault == 56,
	              "acados np_per_stage(P7) = 3 + 2*Nt + 2 tb + Nt sigma + 2 transition + "
	              "1 active = 56 at Nt=16; update if params change");
static_assert(kAcadosNpGlobal == 155,
		              "acados np_global(Step5 方案 B) = 26 head + 128 target + 1 cpa_hard = 155; "
                  "the appended kGIdxCpaHard slot makes the CPA row a true hard floor "
                  "(see review/2026-07-20-step5-plan-b-nh20-agent_8ae45f72.md)");

// Production acatos OCP symbol graph (MX). Path B 5-dim state, 2-dim control,
// 6 costs + full constraints + 106-global / 37-per-stage partition.
//
// This class ONLY builds the CasADi MX symbol graph and packs parameters. It
// does NOT call acatos codegen (that is gen_mid_mpc_acados.py) and does NOT
// solve (that is Task 16 MidMpcAcadosSolver). M5_USE_ACADOS=ON selects the
// acados backend; the IPOPT MidMpcNlpFormulation stays the default otherwise.
class MidMpcAcadosFormulation {
 public:
  // Parameter dimension accounting (T15 F2/F4 + P2 T3 + P7 + Step5 方案 B):
  //   kParamDimGlobal = 155 (IPOPT stage-uniform portion: 26 head + 128 target
  //                          with stride 8 for P7 intent/OU fields, + 1 appended
  //                          kGIdxCpaHard slot for the true hard CPA floor)
  //   kParamDimPerStage = 56 (acados per-stage expansion: prefix + act + drift
  //                          + tb_x/tb_y per-stage closest-point + per-target
  //                          σ_pos (P7) + psi_prev/u_prev/w_trans (P5 T2))
  // IPOPT's flat kParamDim==142 is preserved in the IPOPT formulation; the
  // acados backend expands per-stage (drift precomputed, activation factor,
  // per-stage t_b) because the single-stage graph cannot index stage k. See
  // partition doc.
  static constexpr int32_t kParamDimGlobal    = kAcadosNpGlobal;          // 155 (Step5 方案 B)
  static constexpr int32_t kParamDimPerStage  = kAcadosNpPerStageDefault; // 56 (P7)
  // Production default horizon N (P4: horizon_s=1200s / dt_s=15 -> N=80; was 18).
  // RFC-001: 90s locked design overturned 2026-07-16 Step2 (user-authorized).
  // node-config overrides via resolve_mid_mpc_horizon_config.
  static constexpr int32_t kNDefault = kAcadosNDefault;
  // Step duration [s] (compile-time default; runtime config dt_s=15 overrides).
  // P4: dt=15s from benchmark, acados codegen DT=15.
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
  // I-1 static_assert: lock kMSge to manifest defaults (carryover P4 T7).
  // If capability_manifest.hpp mass_kg or surge_added_mass_factor change,
  // this formula must be updated in lockstep.
  static_assert(kMSge == 152250.0,
                "kMSge must equal 145000 * 1.05 = 152250. Update if manifest changes.");
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
    // Huber radius [m] for the route cost lateral-deviation loss (VR-07b T3).
    // Default = lateral_scale (400m); quadratic near zero, linear far (no
    // exponential pull-back when the solver is pushed off-route by an obstacle).
    // Benchmark-tunable (REC) — start at lateral_scale, recalibrate at RUN-001.
    double huber_delta_h{400.0};
    // CPA per-target slack penalty (exact-penalty; IPOPT w_slack analog).
    // P1b-1a T7/T9: per-target xi slack on CPA rows, mixed L1/L2.
    double w_slack{1.0e8};
    bool   cpa_slack_enabled{true};
    int32_t max_targets{kAcadosMaxTargets};
  // P5 T2 (TBD-7): transition cost J_transition (anti-chattering layer 2).
  // Eriksen mixed-norm: w_trans * (K_Δχ·Σ(ψ-ψ_prev)² + K_ΔU·Σ|u-u_prev|).
  // Default values from Eriksen et al. (2019): K_Δχ≈2.5, K_ΔU≈0.3.
  // w_trans is a global scaling; typical range 0.2-5 vs collision cost 40.
  double w_trans{1.0};
  double k_dchi{2.5};    // K_Δχ: heading-change penalty (L2)
  double k_du{0.3};      // K_ΔU: speed-change penalty (L1)
  // P7: UT expected cost — Unscented Transform alpha (center weight).
  // α=1e-3 places negligible weight on the center point; the 4 sigma points
  // each carry (1-α)/4. [RMD] Ch3.7 Stochastic MPC.
  double ut_alpha{1.0e-3};
  // P7: intent_confidence scaling factor (T5). Default 1.0 means intent_confidence
  // at 0.0 doubles the colreg cost; at 1.0, no scaling. [E3] wi(t) heuristic.
  double k_intent_scale{1.0};
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
  //   second = per-stage params (N+1 rows of np_per_stage=39), one row per
  //            acatos stage 0..N. Each row k carries the per-stage scalars the
  //            single-stage graph reads at fixed offsets:
  //              [0] prefix_psi_at_k, [1] prefix_u_at_k, [2] pact_pre,
  //              [3..3+Nt-1] target_x_at_k[t], [3+Nt..3+2Nt-1] target_y_at_k[t],
  //              [35] tb_x, [36] tb_y (per-stage t_b closest-point, VR-07b T3),
  //              [37] psi_prev, [38] u_prev (P5 T2 transition cost).
  //            (T15 F2 prefix lock + F4 per-stage target drift + VR-07b T3 tb +
  //             P5 T2 transition prev solution.)
  //            The tb slots default to 0.0 here; T4 fills them via
  //            project_to_segment in the solver pack.
  // Mirrors IPOPT pack_parameters semantics (formulation.cpp:670-787) but
  // splits global vs per-stage so acados can update them independently.
  [[nodiscard]] std::pair<std::vector<double>, std::vector<std::vector<double>>>
  pack_parameters(const MidMpcInput& input) const;

  // ---- Accessors (for codegen parity + Task 16 solver) ----
  const std::string& solver_name() const noexcept { return solver_name_; }
  int nx() const noexcept;            // 5 (Path B)
  int nu() const noexcept;            // 2 (delta, n)
  int nh() const noexcept;            // h rows: prefix(2) + CPA(Nt) + dir + min_alt (P4: abolished terminal C10/C11)
  int np_global() const noexcept;     // 106
  int np_per_stage() const noexcept;  // 3 + 2*Nt + 2 tb + 2 transition (39 at default Nt=16)
  int n_horizon() const noexcept { return cfg_.n_horizon; }

  // CasADi MX graph handles (for the codegen script / solver to consume).
  // Empty until build_symbolic_graph() is called.
  const casadi::MX& disc_dyn_expr() const noexcept { return disc_dyn_expr_; }
  const casadi::MX& con_h_expr() const noexcept { return con_h_expr_; }
  // Route cost expression (VR-07b T3): w_guard * Huber(l, delta_h) / l_scale^2
  // with l = (px - tb_x)*nx + (py - tb_y)*ny (per-stage t_b origin). Exposed so
  // the formulation test can build a single-output MX Function over the graph
  // and cross-check against the T2 huber_cost oracle; mirrors disc_dyn_expr().
  const casadi::MX& J_route() const noexcept { return J_route_; }
  // Terminal cost expression (VR-07b T4): wrong-side softplus + two-sided
  // l_max band, anchored at the PER-STAGE t_b (lN = (px-tb_x)*nx + (py-tb_y)*ny
  // — same per-stage origin as J_route). The softplus SHAPE is unchanged from
  // the prior global-origin form; only the lN ANCHOR origin moved to per-stage
  // t_b. Exposed so the formulation test can build a single-output MX Function
  // and cross-check the lN anchor against a discriminating input where the
  // global route origin and the per-stage t_b DIFFER. Mirrors J_route().
  const casadi::MX& J_terminal() const noexcept { return J_terminal_; }
  const casadi::MX& J_transition() const noexcept { return J_transition_; }
  const casadi::MX& x_sym() const noexcept { return x_; }
  const casadi::MX& u_sym() const noexcept { return u_; }
  const casadi::MX& p_global_sym() const noexcept { return p_global_; }
  const casadi::MX& p_stage_sym() const noexcept { return p_stage_; }
  bool graph_valid() const noexcept { return !disc_dyn_expr_.is_null(); }

  const Config& config() const noexcept { return cfg_; }

 private:
  Config cfg_;
  std::string solver_name_{"m5_mid_mpc_acados"};

  // CasADi MX graph members. State x=[px,py,psi,r,u_surge] (5),
  // control u=[delta,n] (2). p_global (155 Step5 方案 B) is stage-uniform; p_stage
  // (56 P7) is per-stage (prefix psi/u scalars + pact_pre + per-target drift x/y
  // + per-stage t_b closest-point tb_x/tb_y + per-target sigma_pos(P7) +
  // psi_prev/u_prev/w_trans_active(P5 T2)).
  casadi::MX x_, u_, p_global_, p_stage_;
  casadi::MX disc_dyn_expr_;  // x[k+1] = f_disc(x[k], u[k], p)  (5 rows)
  casadi::MX con_h_expr_;     // nonlinear path constraint h(x,u,p) (nh rows)

  // Cost expressions (one per IPOPT build_*_cost_; evaluated at the symbol
  // level so the codegen script can mirror them in SX).
  casadi::MX J_colreg_, J_dist_, J_route_, J_vel_, J_asym_, J_terminal_, J_transition_;

  // Helpers (defined in the .cpp; mirror IPOPT build_*_cost_ / constraints).
  [[nodiscard]] casadi::MX build_disc_dyn_() const;
  [[nodiscard]] casadi::MX build_con_h_() const;
  [[nodiscard]] casadi::MX build_colreg_cost_() const;
  [[nodiscard]] casadi::MX build_dist_cost_() const;
  [[nodiscard]] casadi::MX build_route_cost_() const;
  [[nodiscard]] casadi::MX build_vel_cost_() const;
  [[nodiscard]] casadi::MX build_asym_cost_() const;
  [[nodiscard]] casadi::MX build_terminal_cost_() const;

  // P5 T2: transition cost J_transition (anti-chattering layer 2).
  // Eriksen mixed-norm: w_trans * (K_Δχ·Σ(ψ-ψ_prev)² + K_ΔU·Σ|u-u_prev|).
  // per-stage: ψ_prev/u_prev are per-stage parameters (psi_prev_at_k, u_prev_at_k).
  [[nodiscard]] casadi::MX build_transition_cost_() const;

  // Precise Huber loss on MX (VR-07b T3): 0.5*l^2 if |l|<=delta_h, else
  // delta_h*(|l|-0.5*delta_h). C0/C1 at delta_h. CasADi MX::if_else emits the
  // piecewise expr at codegen time. Mirrors shared/huber_cost.hpp (T2 oracle).
  [[nodiscard]] casadi::MX huber_mx_(const casadi::MX& l, double delta_h) const;

  // Per-stage param slot helpers (fixed offsets — the single-stage graph reads
  // stage k's value at a fixed offset; pack_parameters writes a different value
  // to that offset in each stage's per-stage vector). T15 F2/F4 + VR-07b T3 + P5 T2 + P7.
  [[nodiscard]] casadi::MX prefix_psi_at_k_slot_() const;  // p_stage[0]
  [[nodiscard]] casadi::MX prefix_u_at_k_slot_() const;    // p_stage[1]
  [[nodiscard]] casadi::MX pact_pre_slot_() const;         // p_stage[2]
  [[nodiscard]] casadi::MX target_x_at_k_slot_(int32_t t) const;  // p_stage[3+t]
  [[nodiscard]] casadi::MX target_y_at_k_slot_(int32_t t) const;  // p_stage[3+Nt+t]
  // Per-stage t_b closest-point slots (VR-07b T3). The route COST reads these
  // as the lateral-deviation origin. P4: C10/C11 terminal constraints abolished
  // (long horizon 1200s ensures convergence without terminal set).
  [[nodiscard]] casadi::MX tb_x_at_k_slot_() const;  // p_stage[35] = tb_x
  [[nodiscard]] casadi::MX tb_y_at_k_slot_() const;  // p_stage[36] = tb_y
  // P7: per-target σ_pos at stage k slot (OU uncertainty).
  [[nodiscard]] casadi::MX sigma_pos_at_k_slot_(int32_t t) const;  // p_stage[37+t]
  // P5 T2: per-stage transition cost slots (last cycle psi/u for J_transition).
  // Shifted by P7 σ_pos block: psi_prev at 53, u_prev at 54.
  [[nodiscard]] casadi::MX psi_prev_at_k_slot_() const;  // p_stage[53] = psi_prev
  [[nodiscard]] casadi::MX u_prev_at_k_slot_() const;    // p_stage[54] = u_prev
  // Global param slot helper (scalar p_global[i]).
  [[nodiscard]] casadi::MX gslot_(int32_t i) const;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_ACADOS_FORMULATION_HPP_
