// M5 Tactical Planner — Mid-MPC production acados OCP formulation (Task 15).
//
// Builds the CasADi MX symbol graph for the production acados OCP (Path B
// 5-dim state, 2-dim control). Parallel to the IPOPT MidMpcNlpFormulation
// (READ-ONLY reference — never modified here). The MX graph encodes the SAME
// math as the SX gen script (gen_mid_mpc_acados.py); both are the source of
// truth for the codegen, kept in lockstep.
//
// Layout decision (single-stage MX form, not N-stacked):
//   disc_dyn_expr_  : x[k+1] = f_disc(x[k], u[k], p)  — 5-row single-stage
//   con_h_expr_     : h(x[k], u[k], p)                — nh-row single-stage
//   cost expressions: per-stage J(x[k], u[k], p)       — single-stage
// acados stacks these N times itself (the integrator_type=DISCRETE path uses
// disc_dyn_expr directly). This mirrors the P1b-1a staging contract
// (build_base_ocp_doubleint) and the acatos model.disc_dyn_expr / con_h_expr
// API surface — NOT the IPOPT N-stacked x=[psi;u] decision vector.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md
// §10 (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"  // kIdx* parity

namespace mass_l3::m5::mid_mpc {

// ===========================================================================
// Anonymous namespace: global-param index aliases (mirror IPOPT kIdx 0-25) and
// the target-block offset inside the acados global param vector.
// ===========================================================================
namespace {

// Global head scalar indices (IDENTICAL to IPOPT kIdx 0-25). Keeping the same
// integer offsets means pack_parameters writes the same value to the same
// semantic slot in both backends — the contract the spec locks.
//
// T15 F3: slots 0-3 (own initial psi/u/x/y) are RESERVED — they seed the
// acatos initial state via ocp.constraints.x0 (px=own_x, py=own_y, psi=own_psi,
// u_surge=own_u) which the Task 16 solver writes from g[0..3]. They are NOT
// graph-referenced (the acatos graph takes x0 as a hard initial-state equality,
// not a soft param slot); keeping them reserved preserves the IPOPT kIdx parity
// and lets the solver read the seed from the same global slots the IPOPT pack
// writes. r=state[3] (yaw rate) seeds as 0 (no measured yaw rate in MidMpcInput).
constexpr int32_t kGIdxPsi0             = 0;   // RESERVED: x0 seed (psi)  — solver via ocp.constraints.x0
constexpr int32_t kGIdxU0               = 1;   // RESERVED: x0 seed (u_surge) — solver via ocp.constraints.x0
constexpr int32_t kGIdxX0               = 2;   // RESERVED: x0 seed (px)   — solver via ocp.constraints.x0
constexpr int32_t kGIdxY0               = 3;   // RESERVED: x0 seed (py)   — solver via ocp.constraints.x0
constexpr int32_t kGIdxRouteBearing     = 4;
constexpr int32_t kGIdxPlannedSpeed     = kAcadosGIdxPlannedSpeed;      // 5
constexpr int32_t kGIdxHeadingMin       = 6;
constexpr int32_t kGIdxHeadingMax       = 7;
constexpr int32_t kGIdxSpeedMin         = 8;
constexpr int32_t kGIdxSpeedMax         = 9;
constexpr int32_t kGIdxCpaSafe          = 10;
constexpr int32_t kGIdxRotMax           = 11;
constexpr int32_t kGIdxOwnPsi           = 12;
constexpr int32_t kGIdxGiveWay          = 13;  // rule14/15 give-way flag
constexpr int32_t kGIdxRouteFrameOriginX = kAcadosGIdxRouteFrameOriginX;  // 14
constexpr int32_t kGIdxRouteFrameOriginY = kAcadosGIdxRouteFrameOriginY;  // 15
constexpr int32_t kGIdxRouteFrameNormalX = kAcadosGIdxRouteFrameNormalX;  // 16
constexpr int32_t kGIdxRouteFrameNormalY = kAcadosGIdxRouteFrameNormalY;  // 17
constexpr int32_t kGIdxRouteFrameBearing = kAcadosGIdxRouteFrameBearing;  // 18
constexpr int32_t kGIdxLateralScale      = 19;
constexpr int32_t kGIdxRouteWeight       = 20;
constexpr int32_t kGIdxPrefixActiveK     = 21;
constexpr int32_t kGIdxPreferredDir      = 22;
constexpr int32_t kGIdxMinAlterationRad  = 23;
constexpr int32_t kGIdxRole              = 24;
constexpr int32_t kGIdxDecelMax          = 25;
// Target block begins right after the 26 head scalars in the GLOBAL vector
// (note: NOT 62 — the prefix sequence is in the per-stage block, so the target
// block is packed contiguously after the head scalars here).
constexpr int32_t kGIdxTargets = kAcadosNpGlobalHeadScalars;  // 26
constexpr int32_t kGTargetStride = kAcadosTargetStride;       // 5

// Smooth-sqrt guard [m^2] (same as IPOPT build_colreg_cost_).
constexpr double kSqrtGuard = 1.0;
// Exponential-barrier steepness [1/m] (IPOPT Config default).
constexpr double kZeta = 5.0e-3;
// TCPA discount time constant [s] (IPOPT Config default).
constexpr double kTDiscountS = 100.0;
// Range-ramp outer distance [m] (IPOPT Config default).
constexpr double kPwtOuterM = 11112.0;

// Terminal lateral feasibility band defaults (mirror IPOPT Config).
constexpr double kTerminalLMinFeasibleM = 30.0;
constexpr double kTerminalLMaxFeasibleM = 400.0;

// Extract a scalar global param slot as 1x1 MX.
casadi::MX gslot_at(const casadi::MX& p, int32_t i) {
  return p(casadi::Slice(i, i + 1));
}

}  // namespace

// ===========================================================================
// Constructor — clamp config (horizon >= 2 for ROT differential, max_targets
// <= kAcadosMaxTargets). Mirrors IPOPT constructor discipline.
// ===========================================================================
MidMpcAcadosFormulation::MidMpcAcadosFormulation() : MidMpcAcadosFormulation(Config{}) {}

MidMpcAcadosFormulation::MidMpcAcadosFormulation(Config cfg) : cfg_(std::move(cfg)) {
  if (cfg_.max_targets > kAcadosMaxTargets) {
    cfg_.max_targets = kAcadosMaxTargets;
  }
  if (cfg_.max_targets < 0) {
    cfg_.max_targets = 0;
  }
  if (cfg_.n_horizon < 2) {
    cfg_.n_horizon = 2;  // need >=2 steps for ROT differential
  }
}

// ---------------------------------------------------------------------------
// Accessors.
// ---------------------------------------------------------------------------
int MidMpcAcadosFormulation::nx() const noexcept { return 5; }            // Path B
int MidMpcAcadosFormulation::nu() const noexcept { return 2; }            // [delta, n]
int MidMpcAcadosFormulation::np_global() const noexcept {
  return kAcadosNpGlobal;                                                  // 106
}
int MidMpcAcadosFormulation::np_per_stage() const noexcept {
  // 3 (prefix psi/u + pact_pre) + 2*Nt (per-target drift x/y) + 2 (tb_x/tb_y
  // per-stage t_b closest-point, VR-07b T3) + 2 (psi_prev/u_prev, P5 T2)
  // + 1 (w_trans_active, P5 T2). 40 at Nt=16.
  return kAcadosPerStageTgtDriftOff + 2 * cfg_.max_targets + 2  // tb
         + 3;  // P5 T2: psi_prev, u_prev, w_trans_active
}

// nh: single-stage constraint row count (the acatos con_h_expr is a per-stage
// expression; acatos stacks it N times itself). Row classes (FIXED order,
// mirrors the gen script gen_mid_mpc_acados.py and IPOPT build_constraints_;
// ROT is lbx/ubx here so it is NOT in h):
//   [prefix_psi (1)][prefix_u (1)][CPA per-target (Nt)][direction (1)]
//   [min_alt (1)][terminal (3)]
// The prefix rows come FIRST (right after ROT-via-lbx) to mirror IPOPT's
// [ROT][prefix][CPA][direction][min_alt][terminal] class order. At default
// Nt=16: nh = 2 + 16 + 1 + 1 + 3 = 23 (matches gen script output).
int MidMpcAcadosFormulation::nh() const noexcept {
  const int32_t Nt = cfg_.max_targets;
  return static_cast<int>(2 + Nt + 1 + 1);  // prefix(2) + CPA(Nt) + dir + min_alt (P4: terminal C10/C11 abolished)
}

casadi::MX MidMpcAcadosFormulation::gslot_(int32_t i) const {
  return gslot_at(p_global_, i);
}

// Per-stage fixed-offset slot helpers (T15 F2/F4). The single-stage graph reads
// stage k's value at a FIXED offset; pack_parameters writes a different value
// to that offset in each stage's per-stage vector (set via update_params).
casadi::MX MidMpcAcadosFormulation::prefix_psi_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStagePrefixPsiOff,
                                kAcadosPerStagePrefixPsiOff + 1));
}

casadi::MX MidMpcAcadosFormulation::prefix_u_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStagePrefixUOff,
                                kAcadosPerStagePrefixUOff + 1));
}

casadi::MX MidMpcAcadosFormulation::pact_pre_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStagePactPreOff,
                                kAcadosPerStagePactPreOff + 1));
}

casadi::MX MidMpcAcadosFormulation::target_x_at_k_slot_(int32_t t) const {
  const int32_t off = kAcadosPerStageTgtDriftOff + t;
  return p_stage_(casadi::Slice(off, off + 1));
}

casadi::MX MidMpcAcadosFormulation::target_y_at_k_slot_(int32_t t) const {
  const int32_t off = kAcadosPerStageTgtDriftOff + cfg_.max_targets + t;
  return p_stage_(casadi::Slice(off, off + 1));
}

// Per-stage t_b closest-point slots (VR-07b T3). The route COST reads these as
// the lateral-deviation origin; the route CONSTRAINT keeps the GLOBAL route
// origin (C10/C11 deferred to P4 — intentional asymmetry, see build_route_cost_).
casadi::MX MidMpcAcadosFormulation::tb_x_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStageTbXOff, kAcadosPerStageTbXOff + 1));
}

casadi::MX MidMpcAcadosFormulation::tb_y_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStageTbYOff, kAcadosPerStageTbYOff + 1));
}

// P5 T2: per-stage transition cost slots (last cycle psi/u for J_transition).
casadi::MX MidMpcAcadosFormulation::psi_prev_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStagePsiPrevOff,
                                kAcadosPerStagePsiPrevOff + 1));
}

casadi::MX MidMpcAcadosFormulation::u_prev_at_k_slot_() const {
  return p_stage_(casadi::Slice(kAcadosPerStageUPrevOff,
                                kAcadosPerStageUPrevOff + 1));
}

// Precise Huber loss on MX (VR-07b T3). Mirrors shared/huber_cost.hpp (the T2
// pure-function oracle used in the formulation test). C0 continuous, C1 smooth
// at delta_h. MX::abs is the CasADi MX absolute value (unary OP_FABS); if_else
// is MX::if_else (piecewise conditional; codegen emits a piecewise C expr).
casadi::MX MidMpcAcadosFormulation::huber_mx_(const casadi::MX& l,
                                              double delta_h) const {
  // |l|<=delta_h -> 0.5*l^2 ; else delta_h*(|l|-0.5*delta_h). C0/C1 at delta_h.
  const casadi::MX a    = casadi::MX::abs(l);
  const casadi::MX quad = 0.5 * l * l;
  const casadi::MX lin  = delta_h * (a - 0.5 * delta_h);
  return casadi::MX::if_else(a <= delta_h, quad, lin);
}

// ===========================================================================
// build_disc_dyn_() — Path B 5-dim discrete dynamics (single-stage MX form).
//
//   r[k+1]       = r       + DT * c_u * delta
//   psi[k+1]     = psi     + DT * r          (explicit Euler, pre-update r)
//   u_surge[k+1] = u_surge + DT * (k_prop*n^2 - k_drag*u_surge^2) / m_sge
//   px[k+1]      = px      + u_surge * DT * cos(psi)
//   py[k+1]      = py      + u_surge * DT * sin(psi)
//
// State order x = [px, py, psi, r, u_surge] (indices 0..4). Control
// u = [delta, n] (indices 0,1). Coefficients are constexpr literals (VDM-direct
// + T8 yaw gain) — they are NOT parameters (acatos bakes them into the
// generated dyn_disc C, same as build_base_ocp_doubleint).
//
// T15 F1: surge accel is MASS-NORMALIZED by m_sge = mass_kg*(1+surge_added_mass)
// = 152250 (VDM ground truth, vessel_dynamics_model.cpp:43,57). The raw
// (k_prop*n^2 - k_drag*u^2) is a FORCE [N]; dividing by m_sge [kg] yields m/s^2.
// The graph uses the baked effective coefficients kKPropPerMass/kKDragPerMass
// (= kKProp/kMSge, kKDrag/kMSge), mathematically identical to dividing the raw
// force expression by kMSge. Without this the surge accel was ~152250x too
// large (e.g. n=9.26,u=5 -> 40374 m/s^2 vs VDM 0.265 m/s^2). Verified: with the
// fix, (500*9.26^2 - 100*5^2)/152250 = 0.265 m/s^2.
// ===========================================================================
casadi::MX MidMpcAcadosFormulation::build_disc_dyn_() const {
  const casadi::MX px      = x_(0);
  const casadi::MX py      = x_(1);
  const casadi::MX psi     = x_(2);
  const casadi::MX r       = x_(3);
  const casadi::MX u_surge = x_(4);
  const casadi::MX delta   = u_(0);
  const casadi::MX n       = u_(1);
  const double dt = cfg_.dt_s;
  // Explicit-Euler order (matches build_base_ocp_doubleint): psi uses pre-update
  // r; position uses pre-update psi + pre-update u_surge. Surge integrates the
  // thrust(n)=k_prop*n^2 minus drag(u)=k_drag*u^2 simplified model, DIVIDED BY
  // m_sge (T15 F1: mass normalization via baked effective coeffs). No sign-guard
  // — rpm is unsigned by convention (n_min=0 box) and drag opposes motion via
  // the square (u_surge>=0 box), so n*n==n*|n| and u*u==u*|u| in the feasible
  // domain, matching the VDM sign-guarded form there.
  const casadi::MX r_next       = r       + dt * kC_u * delta;
  const casadi::MX psi_next     = psi     + dt * r;
  const casadi::MX u_surge_next = u_surge +
      dt * (kKPropPerMass * n * n - kKDragPerMass * u_surge * u_surge);
  const casadi::MX px_next      = px      + u_surge * dt * casadi::MX::cos(psi);
  const casadi::MX py_next      = py      + u_surge * dt * casadi::MX::sin(psi);
  return casadi::MX::vertcat({px_next, py_next, psi_next, r_next, u_surge_next});
}

// ===========================================================================
// build_con_h_() — nonlinear path constraints (single-stage h(x,u,p), nh rows).
//
// Row classes (FIXED order, mirrors IPOPT build_constraints_; ROT is lbx/ubx
// here so it is NOT in h — prefix comes right after ROT-via-lbx, first h rows):
//   [prefix_psi (1)][prefix_u (1)][CPA per-target (Nt)][direction (1)]
//   [min_alt (1)][terminal (3)]
//
// prefix_psi / prefix_u (T15 F2): committed-route prefix lock. The expression
//   pact_pre * (psi - prefix_psi_at_k)
//   pact_pre * (u_surge - prefix_u_at_k)
// enforces psi[k]==prefix_psi[k], u_surge[k]==prefix_u[k] for the active prefix
// stages (k<K, pact_pre=1.0) and is trivially 0==0 (satisfied) for k>=K
// (pact_pre=0.0). acatos lh=uh=0 (equality). This is the activation-factor
// approach (P1b-1a staging finding): acatos lbx/ubx/lh/uh are stage-uniform, so
// per-stage activation is encoded by multiplying the row by a per-stage param
// factor rather than toggling per-stage bounds. Mirrors IPOPT's prefix equality
// rows (mid_mpc_nlp_formulation.cpp:504-513) which activate the first K rows via
// RowBoundConfig equality [0,0] and leave k>=K double-disabled [-inf,+inf]; here
// the [0,0] equality is stage-uniform and pact_pre selects active vs inactive.
//
// CPA (per-target, Nt rows): smooth squared-distance residual (one-sided >= 0
// after acatos lh=0). T15 F4: target position is the PER-STAGE drifted position
// target_x_at_k[t]/target_y_at_k[t] (precomputed in pack_parameters as
// tx + sog*cos(cog)*k*dt, ty + sog*sin(cog)*k*dt — matches IPOPT
// mid_mpc_nlp_formulation.cpp:375-380). The single-stage graph cannot index k,
// so drift is delivered per-stage via update_params. P1b-1a T7 per-target xi
// slack softens these rows (idxsh=[2..2+Nt-1]) in the codegen script.
//
// direction (1 row): preferred_direction · l[k] >= 0. l[k] is the route-frame
// cross-track at the current stage (own pos - route origin)·n_hat.
//
// min_alt (1 row): preferred_direction · (psi - own_psi) >= min_alt.
//
// terminal (3 rows): g_term_side / g_term_lo / g_term_hi (spec §5.5). Built
// here unconditionally (acatos evaluates them every stage; the codegen script
// deactivates non-terminal stages via lh/uh = [-inf,+inf] or stage-bound masks).
//
// NOTE: this is a structural symbol-graph for the contract test. The codegen
// script (gen_mid_mpc_acados.py) is the codegen source of truth and rewrites
// the SAME row classes in SX; the .cpp MX form must stay mathematically
// identical (locked decision). Target count Nt = cfg_.max_targets (16 default).
// ===========================================================================
casadi::MX MidMpcAcadosFormulation::build_con_h_() const {
  const int32_t Nt = cfg_.max_targets;
  const casadi::MX px      = x_(0);
  const casadi::MX py      = x_(1);
  const casadi::MX psi     = x_(2);
  const casadi::MX u_surge = x_(4);
  std::vector<casadi::MX> rows;
  rows.reserve(static_cast<std::size_t>(2 + Nt + 1 + 1));  // P4: C10/C11 abolished (was +3)
  // ---- Prefix lock (F2): activation-factor equality rows. ----
  const casadi::MX pact_pre = pact_pre_slot_();
  rows.push_back(pact_pre * (psi     - prefix_psi_at_k_slot_()));  // prefix_psi
  rows.push_back(pact_pre * (u_surge - prefix_u_at_k_slot_()));    // prefix_u
  // ---- CPA per-target residual (F4: per-stage drifted target position). ----
  const casadi::MX cpa_safe = gslot_(kGIdxCpaSafe);
  for (int32_t t = 0; t < Nt; ++t) {
    const casadi::MX tx_at_k = target_x_at_k_slot_(t);
    const casadi::MX ty_at_k = target_y_at_k_slot_(t);
    const casadi::MX dx = px - tx_at_k;
    const casadi::MX dy = py - ty_at_k;
    // Squared-distance residual (one-sided >= cpa_safe^2 after acatos lh).
    rows.push_back(dx * dx + dy * dy - cpa_safe * cpa_safe);
  }
  // Direction + min_alt rows: l[k] = (own_pos - route_origin) · n_hat.
  const casadi::MX ox = gslot_(kGIdxRouteFrameOriginX);
  const casadi::MX oy = gslot_(kGIdxRouteFrameOriginY);
  const casadi::MX nx = gslot_(kGIdxRouteFrameNormalX);
  const casadi::MX ny = gslot_(kGIdxRouteFrameNormalY);
  const casadi::MX pref_dir    = gslot_(kGIdxPreferredDir);
  const casadi::MX own_psi     = gslot_(kGIdxOwnPsi);
  const casadi::MX min_alt_par = gslot_(kGIdxMinAlterationRad);
  const casadi::MX l_k = (px - ox) * nx + (py - oy) * ny;
  rows.push_back(pref_dir * l_k);                         // direction
  rows.push_back(pref_dir * (psi - own_psi) - min_alt_par);  // min_alt
  return casadi::MX::vertcat(rows);
}

// ===========================================================================
// Cost expressions (per-stage single-stage MX form, mirror IPOPT build_*_cost_).
// The acatos codegen uses EXTERNAL cost with cost_scaling=ones(N+1) (T2/T9).
// ===========================================================================

// J_colreg: smooth exp barrier per-target at the current stage (averaged over
// Nt). disc_k is folded in numerically by the codegen script per stage; here
// the symbol form omits disc_k (it is a stage-dependent numeric coefficient,
// applied as cost_scaling in the codegen). Mirror IPOPT build_colreg_cost_.
// T15 F4: target position is the per-stage drifted position (target_x_at_k /
// target_y_at_k), matching the CPA constraint rows and IPOPT's per-stage drift.
casadi::MX MidMpcAcadosFormulation::build_colreg_cost_() const {
  const int32_t Nt = std::max(cfg_.max_targets, 1);
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  const casadi::MX cpa = gslot_(kGIdxCpaSafe);
  const casadi::MX zeta = casadi::DM(kZeta);
  casadi::MX cost(0.0);
  for (int32_t t = 0; t < cfg_.max_targets; ++t) {
    const int32_t base = kGIdxTargets + t * kGTargetStride;
    const casadi::MX tw = gslot_at(p_global_, base + 4);  // range-ramp weight
    const casadi::MX tx_at_k = target_x_at_k_slot_(t);     // F4 per-stage drift
    const casadi::MX ty_at_k = target_y_at_k_slot_(t);
    const casadi::MX dx = px - tx_at_k;
    const casadi::MX dy = py - ty_at_k;
    const casadi::MX d = casadi::MX::sqrt(dx * dx + dy * dy + kSqrtGuard);
    cost = cost + tw * casadi::MX::exp(-zeta * (d - cpa));
  }
  return cost / casadi::DM(static_cast<double>(Nt));
}

// J_dist: heading deviation from planned route bearing (single-stage form).
casadi::MX MidMpcAcadosFormulation::build_dist_cost_() const {
  const casadi::MX bearing = gslot_(kGIdxRouteBearing);
  const casadi::MX psi = x_(2);
  return (psi - bearing) * (psi - bearing);
}

// J_route: route-frame lateral-deviation cost (single-stage form). VR-07b T3:
// origin is the PER-STAGE t_b closest point (own predicted position projected
// onto the nominal route leg); solver pack (T4) computes t_b[k] via
// project_to_segment and writes it to the per-stage tb_x/tb_y slots. The route
// NORMAL (nx,ny) stays GLOBAL (the leg bearing is stage-uniform). The lateral
// deviation l[k] = (px - tb_x)*nx + (py - tb_y)*ny is measured relative to
// t_b[k]. The loss is a PRECISE Huber (quadratic near zero, linear far — no
// exponential pull-back when the solver is pushed off-route by an obstacle).
//
// INTENTIONAL asymmetry (spec deferral, NOT a bug): the route COST origin is
// per-stage t_b, but the route CONSTRAINT origin (build_con_h_ direction /
// min_alt / terminal C10/C11 rows) stays the GLOBAL route origin. Switching
// the constraint origin to per-stage t_b is deferred to P4 ("废除终端
// C10/C11 -> P4"). build_terminal_cost_ ALSO moved to per-stage t_b for its
// lN anchor (P2 T4, VR-07b); only the CONSTRAINT rows still use the global
// origin until P4.
casadi::MX MidMpcAcadosFormulation::build_route_cost_() const {
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  // VR-07b T3: per-stage t_b closest-point origin (NOT the global route origin).
  const casadi::MX ox = tb_x_at_k_slot_();
  const casadi::MX oy = tb_y_at_k_slot_();
  const casadi::MX nx = gslot_(kGIdxRouteFrameNormalX);
  const casadi::MX ny = gslot_(kGIdxRouteFrameNormalY);
  const casadi::MX l_scale = gslot_(kGIdxLateralScale);
  const casadi::MX w_guard = gslot_(kGIdxRouteWeight);
  const casadi::MX l = (px - ox) * nx + (py - oy) * ny;
  // VR-07b T3: pure quadratic (l/l_scale)^2 -> Huber(l, delta_h)/l_scale^2.
  const casadi::MX hub = huber_mx_(l, cfg_.huber_delta_h);
  return w_guard * hub / (l_scale * l_scale);
}

// J_vel: surge deviation from planned speed (single-stage form). Uses the
// u_surge STATE (Path B variable-speed), not a param — this is the production
// upgrade from P1b-1a staging (where surge was a held constant).
casadi::MX MidMpcAcadosFormulation::build_vel_cost_() const {
  const casadi::MX planned = gslot_(kGIdxPlannedSpeed);
  const casadi::MX u_surge = x_(4);
  return (u_surge - planned) * (u_surge - planned);
}

// J_asym: smooth starboard preference (give-way only). Mirror IPOPT
// build_asym_cost_ (softplus port penalty, C∞ smooth).
casadi::MX MidMpcAcadosFormulation::build_asym_cost_() const {
  const casadi::MX bearing  = gslot_(kGIdxRouteBearing);
  const casadi::MX give_way = gslot_(kGIdxGiveWay);
  const casadi::MX tau = casadi::DM(0.0873);  // IPOPT asym_tau default
  const casadi::MX psi = x_(2);
  const casadi::MX z = (bearing - psi) / tau;
  const casadi::MX softplus = tau * casadi::MX::log(1.0 + casadi::MX::exp(z));
  return give_way * casadi::DM(cfg_.k_asym) * softplus;
}

// J_terminal: terminal wrong-side softplus (spec §5.4). Single-stage form;
// the codegen script applies this only at the terminal stage (cost_type_e).
//
// VR-07b T4: the lN ANCHOR origin is the PER-STAGE t_b (tb_x_at_k_slot_ /
// tb_y_at_k_slot_), matching build_route_cost_. The terminal stage N reads
// tb[N] = tb[N-1] packed by the solver (the last real projection — see
// mid_mpc_acados_solver.cpp compute_per_stage_tb). The softplus SHAPE
// (wrong_side / l_max band / J_lower / J_upper / lateral_active / return expr)
// is UNCHANGED — only the lN origin moved from the global route origin to the
// per-stage t_b. The route NORMAL (nx,ny) stays global (the leg bearing is
// stage-uniform). P2 does NOT touch the terminal weight; P4 abolishes C10/C11.
casadi::MX MidMpcAcadosFormulation::build_terminal_cost_() const {
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  // VR-07b T4: per-stage t_b closest-point origin (NOT the global route origin).
  const casadi::MX ox = tb_x_at_k_slot_();
  const casadi::MX oy = tb_y_at_k_slot_();
  const casadi::MX nx = gslot_(kGIdxRouteFrameNormalX);
  const casadi::MX ny = gslot_(kGIdxRouteFrameNormalY);
  const casadi::MX l_scale = gslot_(kGIdxLateralScale);
  const casadi::MX pref_dir = gslot_(kGIdxPreferredDir);
  const casadi::MX give_way = gslot_(kGIdxRole);
  const casadi::MX tau_t = casadi::DM(cfg_.terminal_tau);
  const casadi::MX lN = (px - ox) * nx + (py - oy) * ny;
  const casadi::MX wrong_side = -pref_dir * (lN / l_scale);
  const casadi::MX J_lower =
      tau_t * casadi::MX::log(1.0 + casadi::MX::exp(wrong_side / tau_t));
  const casadi::MX l_max = casadi::DM(kTerminalLMaxFeasibleM);
  const casadi::MX z_pos = (lN - l_max) / l_scale;
  const casadi::MX z_neg = (-lN - l_max) / l_scale;
  const casadi::MX J_upper = tau_t * (
      casadi::MX::log(1.0 + casadi::MX::exp(z_pos / tau_t)) +
      casadi::MX::log(1.0 + casadi::MX::exp(z_neg / tau_t)));
  const casadi::MX lateral_active = give_way * pref_dir * pref_dir;
  return give_way * J_lower + lateral_active * J_upper;
}

// ===========================================================================
// build_transition_cost_() — P5 T2: J_transition = w_trans_active * w_trans *
// (K_Δχ·(ψ-ψ_prev)² + K_ΔU·|u-u_prev|). Eriksen mixed-norm anti-chattering
// (layer 2). ψ_prev and u_prev are per-stage parameters read from
// psi_prev_at_k / u_prev_at_k (set by the solver pack from
// last_converged_solution_). w_trans_active is a per-stage flag (1.0 when
// a cached solution exists, 0.0 for the first cycle) that disables the
// transition cost when there is no previous solution to compare against.
// ===========================================================================
casadi::MX MidMpcAcadosFormulation::build_transition_cost_() const {
  const casadi::MX psi = x_(2);       // current heading
  const casadi::MX u_surge = x_(4);   // current surge speed
  const casadi::MX psi_prev = psi_prev_at_k_slot_();  // last cycle psi at this stage
  const casadi::MX u_prev = u_prev_at_k_slot_();      // last cycle u at this stage
  // w_trans_active: 1.0 when previous solution exists, 0.0 for first cycle.
  // This ensures J_transition = 0 when there is no prior trajectory to compare.
  const casadi::MX active = p_stage_(casadi::Slice(
      kAcadosPerStageWTransActiveOff, kAcadosPerStageWTransActiveOff + 1));
  // Eriksen tran_χ (L2) + tran_U (L1)
  const casadi::MX dpsi = psi - psi_prev;
  const casadi::MX du = u_surge - u_prev;
  const casadi::MX tran_chi = cfg_.k_dchi * dpsi * dpsi;                 // L2 heading
  const casadi::MX tran_u   = cfg_.k_du * casadi::MX::abs(du);          // L1 speed
  return active * cfg_.w_trans * (tran_chi + tran_u);
}

// ===========================================================================
// build_symbolic_graph() — assemble the MX symbols + disc_dyn + h + 7 costs.
// Idempotent. The codegen script (gen_mid_mpc_acados.py) re-derives the SAME
// expressions in SX for acatos_template; this .cpp MX graph is the C++
// dimension/param contract + pack-logic home.
// ===========================================================================
void MidMpcAcadosFormulation::build_symbolic_graph() {
  x_ = casadi::MX::sym("x", nx(), 1);               // [px,py,psi,r,u_surge]
  u_ = casadi::MX::sym("u", nu(), 1);               // [delta,n]
  p_global_ = casadi::MX::sym("p_global", np_global(), 1);      // 106
  p_stage_  = casadi::MX::sym("p_stage", np_per_stage(), 1);    // 39 (F2/F4 + T3 tb + P5 T2)
  disc_dyn_expr_ = build_disc_dyn_();
  con_h_expr_    = build_con_h_();
  J_colreg_   = build_colreg_cost_();
  J_dist_     = build_dist_cost_();
  J_route_    = build_route_cost_();
  J_vel_      = build_vel_cost_();
  J_asym_     = build_asym_cost_();
  J_terminal_ = build_terminal_cost_();
  J_transition_ = build_transition_cost_();
}

// ===========================================================================
// pack_parameters() — MidMpcInput -> {global[106], per_stage[N+1][37]}.
//
// Global block (stage-uniform, 106 = 26 head + 80 target):
//   mirrors IPOPT kIdx 0-25 (head) and kIdx 62-141 (target block, remapped to
//   26-105 since the prefix sequence moves to the per-stage block). Slots 0-3
//   (own psi/u/x/y) are RESERVED x0 seeds (F3) — the Task 16 solver writes them
//   to ocp.constraints.x0; they are NOT graph-referenced.
//
// Per-stage block (np_per_stage = 3 + 2*Nt + 2 tb = 37 at Nt=16; N+1 rows):
//   stage k carries [prefix_psi_at_k, prefix_u_at_k, pact_pre,
//                    target_x_at_k[0..Nt-1], target_y_at_k[0..Nt-1],
//                    tb_x, tb_y] (F2/F4 + VR-07b T3 per-stage t_b).
//   tb_x/tb_y DEFAULT to 0.0 here (neutral fallback); T4 fills them with real
//   project_to_segment results in the solver pack. The terminal stage N repeats
//   stage N-1 (acatos requires a per-stage param at every stage 0..N; the
//   terminal value is unused by the path constraints but must be present for
//   the update_params shape).
// ===========================================================================
std::pair<std::vector<double>, std::vector<std::vector<double>>>
MidMpcAcadosFormulation::pack_parameters(const MidMpcInput& input) const {
  std::vector<double> g(static_cast<std::size_t>(np_global()), 0.0);
  // ---- Head scalars (kGIdx 0-25) — verbatim IPOPT pack_parameters. ----
  g[kGIdxPsi0]         = input.own_ship.psi_rad;
  g[kGIdxU0]           = input.own_ship.u_mps;
  g[kGIdxX0]           = input.own_ship.x_m;
  g[kGIdxY0]           = input.own_ship.y_m;
  g[kGIdxRouteBearing] = input.planned_route_bearing_rad;
  g[kGIdxPlannedSpeed] = input.planned_speed_mps;
  g[kGIdxHeadingMin]   = input.constraints.heading_min_rad;
  g[kGIdxHeadingMax]   = input.constraints.heading_max_rad;
  g[kGIdxSpeedMin]     = input.constraints.speed_min_mps;
  g[kGIdxSpeedMax]     = input.constraints.speed_max_mps;
  g[kGIdxCpaSafe]      = input.constraints.cpa_safe_m;
  g[kGIdxOwnPsi]       = input.constraints.own_ship_psi_rad;
  g[kGIdxRotMax]       = input.rot_max_rad_s;
  bool give_way = false;
  for (const std::uint8_t rule : input.constraints.applicable_rules) {
    if (rule == 14u || rule == 15u) { give_way = true; }
  }
  g[kGIdxGiveWay] = give_way ? 1.0 : 0.0;
  g[kGIdxRouteFrameOriginX] = input.route_frame_origin_x_m;
  g[kGIdxRouteFrameOriginY] = input.route_frame_origin_y_m;
  g[kGIdxRouteFrameNormalX] = input.route_frame_normal_x;
  g[kGIdxRouteFrameNormalY] = input.route_frame_normal_y;
  g[kGIdxRouteFrameBearing] = input.route_frame_active_leg_bearing_rad;
  g[kGIdxLateralScale] = (input.lateral_scale_m > 1.0e-6) ? input.lateral_scale_m : 400.0;
  g[kGIdxRouteWeight]  = input.route_weight;
  const int32_t K_raw = input.prefix_active_k;
  const int32_t K = (K_raw < 0) ? 0 : ((K_raw > cfg_.n_horizon) ? cfg_.n_horizon : K_raw);
  g[kGIdxPrefixActiveK] = static_cast<double>(K);
  double pref_dir_val = 0.0;
  if (input.colregs_preferred_direction == ColregsPreferredDirection::Starboard) {
    pref_dir_val = 1.0;
  } else if (input.colregs_preferred_direction == ColregsPreferredDirection::Port) {
    pref_dir_val = -1.0;
  }
  g[kGIdxPreferredDir]     = pref_dir_val;
  g[kGIdxMinAlterationRad] = input.colregs_min_alteration_rad;
  const bool is_give_way =
      (input.colregs_primary_role == 1U || input.colregs_primary_role == 2U);
  g[kGIdxRole] = is_give_way ? 1.0 : 0.0;
  g[kGIdxDecelMax] = (input.decel_max_mps2 > 1.0e-6) ? input.decel_max_mps2 : 0.08;

  // ---- Target block (kGIdxTargets + t*5, t=0..15) — verbatim IPOPT, with the
  // per-target range-ramp weight computed numerically (no clamp kink in graph).
  const int32_t n_t = std::min(
      static_cast<int32_t>(input.targets.size()), cfg_.max_targets);
  for (int32_t t = 0; t < n_t; ++t) {
    const auto& tgt = input.targets[static_cast<std::size_t>(t)];
    const std::size_t base = static_cast<std::size_t>(
        kGIdxTargets + t * kGTargetStride);
    g[base + 0u] = tgt.x_m;
    g[base + 1u] = tgt.y_m;
    g[base + 2u] = tgt.cog_rad;
    g[base + 3u] = tgt.sog_mps;
    const double rng0 = std::hypot(tgt.x_m - input.own_ship.x_m,
                                   tgt.y_m - input.own_ship.y_m);
    const double pwt_inner = input.constraints.cpa_safe_m;
    const double span = std::max(kPwtOuterM - pwt_inner, 1.0);
    const double w_range = std::clamp((kPwtOuterM - rng0) / span, 0.0, 1.0);
    g[base + 4u] = w_range;
  }

  // ---- Per-stage block (np_per_stage = 3 + 2*Nt + 2 tb per stage, N+1 rows).
  // T15 F2/F4 + VR-07b T3. Each stage k carries (at fixed offsets the
  // single-stage graph reads):
  //   [0]      prefix_psi_at_k  — committed-geometry psi target (C1, F2)
  //   [1]      prefix_u_at_k    — committed-geometry u   target (C1, F2)
  //   [2]      pact_pre         — prefix activation (1.0 if k<K else 0.0, F2)
  //   [3..]    target_x_at_k[t] — drifted target x (F4)
  //   [3+Nt..] target_y_at_k[t] — drifted target y (F4)
  //   [35]     tb_x             — per-stage t_b closest-point x (VR-07b T3)
  //   [36]     tb_y             — per-stage t_b closest-point y (VR-07b T3)
  // tb_x/tb_y are left at their 0.0 default here (neutral fallback). T4 fills
  // them with real project_to_segment results in the solver pack; for T3 only
  // the SHAPE (37) matters.
  // Stages k<K carry the reprojected committed-geometry psi/u (F2 prefix lock);
  // stages k>=K carry pact_pre=0 (row deactivated) and psi/u values that are
  // unused (the activation factor zeroes the row). Target drift matches IPOPT
  // (mid_mpc_nlp_formulation.cpp:375-380): tdx=sog*cos(cog), tdy=sog*sin(cog),
  // target_x_at_k = tx + tdx*k*dt. The terminal stage N repeats stage N-1
  // (acatos requires a per-stage param at every stage 0..N; the terminal value
  // is unused by the path constraints but must be present for update_params).
  const int32_t N  = cfg_.n_horizon;
  const int32_t Nt = cfg_.max_targets;
  const double dt  = cfg_.dt_s;
  std::vector<std::vector<double>> ps(
      static_cast<std::size_t>(N + 1),
      std::vector<double>(static_cast<std::size_t>(np_per_stage()), 0.0));
  // Precompute per-target drift components (sog*cos/sin cog) once.
  // Empty target slots [n_t..Nt-1]: tx/ty/cog/sog stay 0.0 (the per-target loop
  // above only fills the tw range-ramp weight for t<n_t, so the COLREG COST is
  // neutralized for empty slots). The CPA CONSTRAINT rows for empty slots are
  // relaxed to [-inf,+inf] by the solver wrapper (build_stage_row_bounds with
  // n_targets), mirroring IPOPT — which emits CPA rows only for real targets.
  // We do NOT push empty-slot positions to a huge value here: a 1e14-scale CPA
  // residual poisons the EXACT-hessian KKT conditioning when other rows (prefix
  // equality) are active (CASE B divergence, isolated 2026-07-17). Keeping the
  // empty slot at (0,0) is harmless once its CPA row bound is relaxed.
  const std::size_t Nt_sz = static_cast<std::size_t>(Nt);
  std::vector<double> tdx(Nt_sz, 0.0);
  std::vector<double> tdy(Nt_sz, 0.0);
  std::vector<double> tx0(Nt_sz, 0.0);
  std::vector<double> ty0(Nt_sz, 0.0);
  for (int32_t t = 0; t < n_t; ++t) {
    const auto& tgt = input.targets[static_cast<std::size_t>(t)];
    tx0[static_cast<std::size_t>(t)] = tgt.x_m;
    ty0[static_cast<std::size_t>(t)] = tgt.y_m;
    tdx[static_cast<std::size_t>(t)] = tgt.sog_mps * std::cos(tgt.cog_rad);
    tdy[static_cast<std::size_t>(t)] = tgt.sog_mps * std::sin(tgt.cog_rad);
  }
  for (int32_t k = 0; k < N; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    const double kdt = static_cast<double>(k) * dt;
    // Prefix scalars + activation (F2). k<K active (pact_pre=1), else inactive.
    double psi_k = 0.0;
    double u_k   = input.own_ship.u_mps;
    double pact  = 0.0;
    if (k < K) {
      pact = 1.0;
      if (kk < input.prefix_psi_rad.size()) {
        psi_k = input.prefix_psi_rad[kk];
      }
      if (kk < input.prefix_u_mps.size()) {
        u_k = input.prefix_u_mps[kk];
      }
    }
    ps[kk][static_cast<std::size_t>(kAcadosPerStagePrefixPsiOff)] = psi_k;
    ps[kk][static_cast<std::size_t>(kAcadosPerStagePrefixUOff)]   = u_k;
    ps[kk][static_cast<std::size_t>(kAcadosPerStagePactPreOff)]   = pact;
    // Per-stage target drift (F4): target_x_at_k = tx + tdx*k*dt.
    for (int32_t t = 0; t < Nt; ++t) {
      const std::size_t tt = static_cast<std::size_t>(t);
      ps[kk][static_cast<std::size_t>(kAcadosPerStageTgtDriftOff + t)] =
          tx0[tt] + tdx[tt] * kdt;
      ps[kk][static_cast<std::size_t>(kAcadosPerStageTgtDriftOff + Nt + t)] =
          ty0[tt] + tdy[tt] * kdt;
    }
  }
  // Terminal stage N: repeat stage N-1 (unused by path constraints; fills shape).
  if (N >= 1) {
    ps[static_cast<std::size_t>(N)] = ps[static_cast<std::size_t>(N - 1)];
  }
  return {std::move(g), std::move(ps)};
}

}  // namespace mass_l3::m5::mid_mpc
