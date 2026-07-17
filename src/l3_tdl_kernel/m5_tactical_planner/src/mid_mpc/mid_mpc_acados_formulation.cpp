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
constexpr int32_t kGIdxPsi0             = 0;   // own initial heading [rad]
constexpr int32_t kGIdxU0               = 1;   // own initial surge   [m/s]
constexpr int32_t kGIdxX0               = 2;   // own current NED north [m]
constexpr int32_t kGIdxY0               = 3;   // own current NED east  [m]
constexpr int32_t kGIdxRouteBearing     = 4;
constexpr int32_t kGIdxPlannedSpeed     = 5;
constexpr int32_t kGIdxHeadingMin       = 6;
constexpr int32_t kGIdxHeadingMax       = 7;
constexpr int32_t kGIdxSpeedMin         = 8;
constexpr int32_t kGIdxSpeedMax         = 9;
constexpr int32_t kGIdxCpaSafe          = 10;
constexpr int32_t kGIdxRotMax           = 11;
constexpr int32_t kGIdxOwnPsi           = 12;
constexpr int32_t kGIdxGiveWay          = 13;  // rule14/15 give-way flag
constexpr int32_t kGIdxRouteFrameOriginX = 14;
constexpr int32_t kGIdxRouteFrameOriginY = 15;
constexpr int32_t kGIdxRouteFrameNormalX = 16;
constexpr int32_t kGIdxRouteFrameNormalY = 17;
constexpr int32_t kGIdxRouteFrameBearing = 18;
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
  return 2 * cfg_.n_horizon;                                               // 2*N
}

// nh: single-stage constraint row count (the acatos con_h_expr is a per-stage
// expression; acatos stacks it N times itself). Row classes (FIXED order,
// mirrors the gen script gen_mid_mpc_acados.py and IPOPT build_constraints_
// minus ROT/prefix which are lbx/ubx here):
//   [CPA per-target (Nt)][direction (1)][min_alt (1)][terminal (3)]
// At default Nt=16: nh = 16 + 1 + 1 + 3 = 21 (matches gen script output).
int MidMpcAcadosFormulation::nh() const noexcept {
  const int32_t Nt = cfg_.max_targets;
  return static_cast<int>(Nt + 1 + 1 + 3);
}

casadi::MX MidMpcAcadosFormulation::gslot_(int32_t i) const {
  return gslot_at(p_global_, i);
}

casadi::MX MidMpcAcadosFormulation::prefix_psi_slot_(int32_t k) const {
  return p_stage_(casadi::Slice(kAcadosPerStagePrefixPsiOffset + k,
                                kAcadosPerStagePrefixPsiOffset + k + 1));
}

casadi::MX MidMpcAcadosFormulation::prefix_u_slot_(int32_t k) const {
  const int32_t off = cfg_.n_horizon + k;  // prefix u follows N prefix-psi
  return p_stage_(casadi::Slice(off, off + 1));
}

// ===========================================================================
// build_disc_dyn_() — Path B 5-dim discrete dynamics (single-stage MX form).
//
//   r[k+1]       = r       + DT * c_u * delta
//   psi[k+1]     = psi     + DT * r          (explicit Euler, pre-update r)
//   u_surge[k+1] = u_surge + DT * (k_prop*n^2 - k_drag*u_surge^2)
//   px[k+1]      = px      + u_surge * DT * cos(psi)
//   py[k+1]      = py      + u_surge * DT * sin(psi)
//
// State order x = [px, py, psi, r, u_surge] (indices 0..4). Control
// u = [delta, n] (indices 0,1). Coefficients are constexpr literals (VDM-direct
// + T8 yaw gain) — they are NOT parameters (acatos bakes them into the
// generated dyn_disc C, same as build_base_ocp_doubleint).
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
  // thrust(n)=k_prop*n^2 minus drag(u)=k_drag*u^2 simplified model (spec
  // amendment 2026-07-17; VDM-direct k_prop/k_drag, no sign-guard — rpm is
  // unsigned by convention, drag opposes motion via the square).
  const casadi::MX r_next       = r       + dt * kC_u * delta;
  const casadi::MX psi_next     = psi     + dt * r;
  const casadi::MX u_surge_next = u_surge +
      dt * (kKProp * n * n - kKDrag * u_surge * u_surge);
  const casadi::MX px_next      = px      + u_surge * dt * casadi::MX::cos(psi);
  const casadi::MX py_next      = py      + u_surge * dt * casadi::MX::sin(psi);
  return casadi::MX::vertcat({px_next, py_next, psi_next, r_next, u_surge_next});
}

// ===========================================================================
// build_con_h_() — nonlinear path constraints (single-stage h(x,u,p), nh rows).
//
// Row classes (FIXED order, mirrors IPOPT build_constraints_ minus ROT/prefix
// which are lbx/ubx here):
//   [CPA per-target][direction][min_alt][terminal]
//
// CPA (per-target, Nt rows): smooth squared-distance residual (one-sided >= 0
// after acatos lh=0). P1b-1a T7 per-target xi slack softens these rows
// (idxsh=[0..Nt-1]) in the codegen script; the EXPRESSION here is the raw
// per-target residual. Target drift is per-target-global (kGIdxTargets block);
// this single-stage form evaluates CPA at the current stage's own state x
// (acatos stacks it per stage, so each stage sees its own x[k]).
//
// direction (N rows): preferred_direction · l[k] >= 0. l[k] is the route-frame
// cross-track at the current stage (own pos - route origin)·n_hat.
//
// min_alt (N rows): preferred_direction · (psi - own_psi) >= min_alt.
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
  // CPA per-target residual (one row each). Targets use the global target block
  // (kGIdxTargets). tdx/tdy = sog*(cos cog, sin cog) drift; here we bake the
  // DRIFT-FREE position (target tx/ty are the stage-0 positions; per-stage drift
  // is a P1b-1c extension — staging T9 used per-stage drift, production keeps
  // the IPOPT global-block semantics for the contract test).
  std::vector<casadi::MX> rows;
  rows.reserve(static_cast<std::size_t>(Nt + 1 + 1 + 3));
  const casadi::MX cpa_safe = gslot_(kGIdxCpaSafe);
  for (int32_t t = 0; t < Nt; ++t) {
    const int32_t base = kGIdxTargets + t * kGTargetStride;
    const casadi::MX tx = gslot_at(p_global_, base + 0);
    const casadi::MX ty = gslot_at(p_global_, base + 1);
    const casadi::MX dx = px - tx;
    const casadi::MX dy = py - ty;
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
  // Terminal 3 rows (evaluated at current stage; codegen masks non-terminal).
  const casadi::MX l_min = casadi::DM(kTerminalLMinFeasibleM);
  const casadi::MX l_max = casadi::DM(kTerminalLMaxFeasibleM);
  rows.push_back(pref_dir * l_k - l_min);   // g_term_side
  rows.push_back(l_k + l_max);              // g_term_lo
  rows.push_back(l_max - l_k);              // g_term_hi
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
casadi::MX MidMpcAcadosFormulation::build_colreg_cost_() const {
  const int32_t Nt = std::max(cfg_.max_targets, 1);
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  const casadi::MX cpa = gslot_(kGIdxCpaSafe);
  const casadi::MX zeta = casadi::DM(kZeta);
  casadi::MX cost(0.0);
  for (int32_t t = 0; t < cfg_.max_targets; ++t) {
    const int32_t base = kGIdxTargets + t * kGTargetStride;
    const casadi::MX tx = gslot_at(p_global_, base + 0);
    const casadi::MX ty = gslot_at(p_global_, base + 1);
    const casadi::MX tw = gslot_at(p_global_, base + 4);  // range-ramp weight
    const casadi::MX dx = px - tx;
    const casadi::MX dy = py - ty;
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

// J_route: route-frame dimensionless cross-track (single-stage form). Mirror
// IPOPT build_route_cost_ (l_scale normalization + route_weight guard).
casadi::MX MidMpcAcadosFormulation::build_route_cost_() const {
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  const casadi::MX ox = gslot_(kGIdxRouteFrameOriginX);
  const casadi::MX oy = gslot_(kGIdxRouteFrameOriginY);
  const casadi::MX nx = gslot_(kGIdxRouteFrameNormalX);
  const casadi::MX ny = gslot_(kGIdxRouteFrameNormalY);
  const casadi::MX l_scale = gslot_(kGIdxLateralScale);
  const casadi::MX w_guard = gslot_(kGIdxRouteWeight);
  const casadi::MX l = (px - ox) * nx + (py - oy) * ny;
  return w_guard * (l / l_scale) * (l / l_scale);
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
casadi::MX MidMpcAcadosFormulation::build_terminal_cost_() const {
  const casadi::MX px = x_(0);
  const casadi::MX py = x_(1);
  const casadi::MX ox = gslot_(kGIdxRouteFrameOriginX);
  const casadi::MX oy = gslot_(kGIdxRouteFrameOriginY);
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
// build_symbolic_graph() — assemble the MX symbols + disc_dyn + h + 6 costs.
// Idempotent. The codegen script (gen_mid_mpc_acados.py) re-derives the SAME
// expressions in SX for acatos_template; this .cpp MX graph is the C++
// dimension/param contract + pack-logic home.
// ===========================================================================
void MidMpcAcadosFormulation::build_symbolic_graph() {
  x_ = casadi::MX::sym("x", nx(), 1);               // [px,py,psi,r,u_surge]
  u_ = casadi::MX::sym("u", nu(), 1);               // [delta,n]
  p_global_ = casadi::MX::sym("p_global", np_global(), 1);      // 106
  p_stage_  = casadi::MX::sym("p_stage", np_per_stage(), 1);    // 2*N
  disc_dyn_expr_ = build_disc_dyn_();
  con_h_expr_    = build_con_h_();
  J_colreg_   = build_colreg_cost_();
  J_dist_     = build_dist_cost_();
  J_route_    = build_route_cost_();
  J_vel_      = build_vel_cost_();
  J_asym_     = build_asym_cost_();
  J_terminal_ = build_terminal_cost_();
}

// ===========================================================================
// pack_parameters() — MidMpcInput -> {global[106], per_stage[N+1][2N]}.
//
// Global block (stage-uniform, 106 = 26 head + 80 target):
//   mirrors IPOPT kIdx 0-25 (head) and kIdx 62-141 (target block, remapped to
//   26-105 since the prefix sequence moves to the per-stage block).
//
// Per-stage block (2N = prefix psi[N] + prefix u[N], mirrors IPOPT kIdx 26-61):
//   stage k carries [prefix_psi[k], prefix_u[k]]. N+1 rows (stages 0..N);
//   the terminal stage repeats the last prefix entry (acatos requires a
//   per-stage param at every stage 0..N; the terminal value is unused by the
//   dynamics but must be present for the update_params shape).
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

  // ---- Per-stage prefix block (2N values: prefix psi[N] + prefix u[N]).
  // One row per acatos stage 0..N (N+1 rows). Stages k<K carry the reprojected
  // committed-geometry psi/u; stages k>=K carry 0 (inactive, like IPOPT).
  const int32_t N = cfg_.n_horizon;
  std::vector<std::vector<double>> ps(
      static_cast<std::size_t>(N + 1),
      std::vector<double>(static_cast<std::size_t>(2 * N), 0.0));
  for (int32_t k = 0; k < N; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    double psi_k = 0.0;
    double u_k   = input.own_ship.u_mps;
    if (k < K) {
      if (kk < input.prefix_psi_rad.size()) {
        psi_k = input.prefix_psi_rad[kk];
      }
      if (kk < input.prefix_u_mps.size()) {
        u_k = input.prefix_u_mps[kk];
      }
    }
    ps[kk][static_cast<std::size_t>(k)] = psi_k;          // prefix psi slot
    ps[kk][static_cast<std::size_t>(N + k)] = u_k;        // prefix u slot
  }
  // Terminal stage N: repeat the last prefix entry (unused by dynamics; fills
  // the required per-stage shape for update_params).
  if (N >= 1) {
    ps[static_cast<std::size_t>(N)] = ps[static_cast<std::size_t>(N - 1)];
  }
  return {std::move(g), std::move(ps)};
}

}  // namespace mass_l3::m5::mid_mpc
