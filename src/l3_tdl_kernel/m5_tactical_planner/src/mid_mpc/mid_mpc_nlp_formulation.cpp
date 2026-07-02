// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <casadi/casadi.hpp>
#include <spdlog/spdlog.h>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// ===========================================================================
// Anonymous namespace: IPOPT options and helper builders.
// ===========================================================================
namespace {

// [TBD-HAZID] IPOPT max iterations per Mid-MPC cycle.
// Default 150; calibrate from FCB sea-trial timing data (HAZID RUN-001 WP-04).
constexpr int32_t kIpoptMaxIter = 500;

// [TBD-HAZID] IPOPT convergence tolerance.
// Default 1e-4; calibrate per detailed design §5.2.4 SLA budget.
constexpr double kIpoptTol = 1.0e-4;

// [TBD-HAZID] IPOPT acceptable convergence tolerance (looser than tol).
// If IPOPT finds a point within this tolerance after acceptable_iter, it returns success.
// Enables graceful degradation within the cycle SLA.
constexpr double kIpoptAcceptableTol = 1.0e-3;

// [TBD-HAZID] Minimum IPOPT iterations before acceptable_tol-based early exit.
constexpr int32_t kIpoptAcceptableIter = 5;

// [TBD-HAZID] IPOPT max CPU time [s] — 2.0 s within 1 Hz cycle (detailed design §5.2.4).
constexpr double kIpoptMaxCpuTime = 2.0;

// [TBD-HAZID] Default ROT max [rad/s] when caller does not override via p_.
// 0.2094 rad/s ≈ 12°/s; FCB nominal at 18 kn (vessel_dynamics_model default).
// Calibrate per vessel/sea-state during HAZID RUN-001 WP-02.
constexpr double kDefaultRotMaxRadS = 0.2094;

// Slice helper: extract scalar p[i] as 1×1 MX.
casadi::MX slot(const casadi::MX& p, int32_t i) {
  return p(casadi::Slice(i, i + 1));
}

}  // namespace

// ===========================================================================
// Constructor — store config (no graph build yet; explicit build call required).
// ===========================================================================
MidMpcNlpFormulation::MidMpcNlpFormulation(const Config& cfg) : cfg_(cfg) {
  if (cfg_.max_targets > kMaxTargets) {
    cfg_.max_targets = kMaxTargets;
  }
  if (cfg_.max_targets < 0) {
    cfg_.max_targets = 0;
  }
  if (cfg_.n_horizon < 2) {
    cfg_.n_horizon = 2;  // require ≥2 steps for ROT differential constraint
  }
}

// ===========================================================================
// g_dim() — general-constraint count from the currently built graph.
//
// Heading and speed box limits are simple per-variable bounds; they are passed
// to IPOPT as lbx/ubx (set per-cycle in MidMpcSolver::solve), NOT as general
// inequality rows in g. Encoding a box optimum as an active *general* constraint
// under limited-memory Hessian + adaptive mu is restoration-fragile (the
// cost optimum is pinned to the box edge when the route bearing lies outside
// the avoidance window), which produced intermittent Restoration_Failed /
// Maximum_Iterations. Variable bounds make a box-active optimum IPOPT's
// canonical robust case and auto-project the warm start into [lbx,ubx].
// ===========================================================================
int32_t MidMpcNlpFormulation::g_dim() const noexcept {
  const int32_t N = cfg_.n_horizon;
  return (g_dim_ > 0) ? g_dim_ : 2 * (N - 1);
}

// ===========================================================================
// build_distance_cost_() — sum_k (psi[k] - planned_route_bearing)^2
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_distance_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX bearing = slot(p_, kIdxRouteBearing);
  const casadi::MX bearing_rep = casadi::MX::repmat(bearing, N, 1);
  const casadi::MX err = psi_ - bearing_rep;
  return casadi::MX::dot(err, err);
}

// ===========================================================================
// build_route_cost_() — Slice R1 route-frame dimensionless cross-track (§4.3).
//
// CONTRACT (spec §4.2, Critical-1 review fix): the own-ship cumulative position
// pos[k] is integrated from the own ship CURRENT position (kIdxX0/Y0), exactly
// as build_colreg_cost_ does. The route-frame origin (kIdxRouteFrameOriginX/Y)
// is the active-leg point expressed in the SAME own-relative NED frame as X0/Y0
// (node packs it as leg_point - own_position). The cross-track is then
//   l[k] = (pos[k] - route_origin) · n_hat
// so that l[0] = (own_pos - leg_point) · n_hat = the TRUE current cross-track.
//
// The previous implementation initialized the position integral AT the route
// origin (cx=ox, cy=oy), forcing l[0]=0 — the NLP was blind to the ship's real
// displacement from the route (e.g. a 50 m XTE produced zero J_route gradient).
//
// n_hat = (kIdxRouteFrameNormalX, kIdxRouteFrameNormalY) is packed as
// (-sinψ, cosψ) so starboard is positive (spec §3.1/§4.2).
//
// J_route = kIdxRouteWeight · [ Σ_{k} (l[k]/l_scale)² + λ_t·(l[N-1]/l_scale)² ]
//   - dimensionless: l_scale = GncExecutionOdd.max_lateral_offset_m (400 m) →
//     l/l_scale ∈ [-1,1], J_route O(1), same order as the averaged J_colreg.
//   - kIdxRouteWeight (parameter): 1.0 normal, 0.0 when the predicted trajectory
//     crosses an L2 leg corner (cross-leg guard, §4.3). Multiplied here so a
//     single parameter nulls the term; cfg_.w_route is applied in the J assembly.
//   - λ_t: terminal cross-track reinforcement (spec §4.3, default 1.0). The full
//     §5.4 terminal side/wrong-side softplus is Slice T1; here the §4.3 terminal
//     strengthening term keeps J_route's own terminal weight, deferred detail to T1.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_route_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX dt   = casadi::DM(cfg_.dt_s);
  // Position integral starts at the OWN SHIP current position (spec §3.1/§4.2),
  // NOT at the route origin — this is what lets l[0] see the initial XTE.
  casadi::MX cx   = slot(p_, kIdxX0);
  casadi::MX cy   = slot(p_, kIdxY0);
  const casadi::MX ox   = slot(p_, kIdxRouteFrameOriginX);
  const casadi::MX oy   = slot(p_, kIdxRouteFrameOriginY);
  const casadi::MX nx   = slot(p_, kIdxRouteFrameNormalX);
  const casadi::MX ny   = slot(p_, kIdxRouteFrameNormalY);
  const casadi::MX l_scale = slot(p_, kIdxLateralScale);
  const casadi::MX w_guard = slot(p_, kIdxRouteWeight);  // cross-leg guard

  casadi::MX lN(0.0);  // terminal cross-track l[N-1]
  casadi::MX cost(0.0);
  for (int32_t k = 0; k < N; ++k) {
    // Evaluate l[k] AT pos[k] FIRST (spec §3.1: pos[0] = own current = real XTE).
    // The previous implementation advanced the integral BEFORE evaluating, so the
    // loop body at index k actually evaluated l at pos[k+1] — the own current
    // cross-track l[0] never entered the cost (R1 review round-2 Critical 1).
    const casadi::MX l = (cx - ox) * nx + (cy - oy) * ny;  // cross-track at pos[k]
    cost = cost + casadi::MX::sq(l / l_scale);
    if (k == N - 1) { lN = l; }
    // THEN advance to pos[k+1] = pos[k] + u[k]·dt·(cos,sin)(psi[k]) (spec §3.1:
    // pos[k] = x0 + Σ_{j<k} u[j]·dt·...; the advance to pos[N] after the last
    // evaluation is harmless, it simply is not read again).
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
  }
  // Terminal cross-track reinforcement (spec §4.3: λ_terminal > 1, a STRENGTHENED
  // terminal weight relative to the running cost). λ_t = 2.0 doubles the terminal
  // l[N-1] penalty so the NLP biases the horizon tail back toward the route
  // ([TBD-HAZID] HAZID RUN-001 calibrates; T1 tunes alongside §5.4 softplus).
  const casadi::MX lambda_terminal = casadi::DM(2.0);
  cost = cost + lambda_terminal * casadi::MX::sq(lN / l_scale);
  return w_guard * cost;
}

// ===========================================================================
// build_velocity_cost_() — sum_k (u[k] - planned_speed)^2
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_velocity_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX planned = slot(p_, kIdxPlannedSpeed);
  const casadi::MX planned_rep = casadi::MX::repmat(planned, N, 1);
  const casadi::MX err = u_ - planned_rep;
  return casadi::MX::dot(err, err);
}

// ===========================================================================
// build_colreg_cost_() — smooth CPA repulsion over (N steps × max_targets).
//
// NED convention (types.hpp:29: psi=0 → north, positive clockwise):
//   dx[j] = u[j]*dt*cos(psi[j])    (north component)
//   dy[j] = u[j]*dt*sin(psi[j])    (east  component)
// Cumulative own-ship position relative to (x0, y0) is integrated step-by-step.
//
// Per (target,step): tw · disc_k · exp(-zeta·(d - cpa_safe)). A smooth
// exponential barrier (no singularity, ≈0 far, 1 at d=cpa_safe, grows when
// penetrating) replaces the non-smooth fmax(0, cpa²-d²) hinge that caused
// IPOPT Restoration_Failed/Max_Iter. Weighting is dynamic but smooth:
//   - tw (param slot, numeric) = range ramp computed at pack-time;
//   - disc_k = exp(-t_k/T_d) = constant per step (numeric coefficient).
// Only the barrier (function of d, i.e. the decision variables) is symbolic.
//
// P1: soft COLREGs cost remains as gradient guidance; hard CPA/rule rows are
// added in build_constraints_() through ConstraintCompiler.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_colreg_cost_() const {
  const int32_t N  = cfg_.n_horizon;
  const int32_t Nt = cfg_.max_targets;
  const casadi::MX dt   = casadi::DM(cfg_.dt_s);
  const casadi::MX cpa  = slot(p_, kIdxCpaSafe);   // d_safe [m]
  const casadi::MX zeta = casadi::DM(cfg_.zeta);
  constexpr double kSqrtGuard = 1.0;               // [m²] smooth-sqrt guard

  // Pre-integrate own-ship cumulative position at each step k ∈ [0, N-1].
  std::vector<casadi::MX> x_own(static_cast<std::size_t>(N));
  std::vector<casadi::MX> y_own(static_cast<std::size_t>(N));
  casadi::MX cx = slot(p_, kIdxX0);
  casadi::MX cy = slot(p_, kIdxY0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
    x_own[static_cast<std::size_t>(k)] = cx;
    y_own[static_cast<std::size_t>(k)] = cy;
  }

  // Accumulate per-target, per-step smooth barrier.
  casadi::MX cost(0.0);
  for (int32_t t = 0; t < Nt; ++t) {
    const int32_t base = kIdxTargets + t * kTargetStride;
    const casadi::MX tx = slot(p_, base + 0);
    const casadi::MX ty = slot(p_, base + 1);
    const casadi::MX tc = slot(p_, base + 2);
    const casadi::MX ts = slot(p_, base + 3);
    const casadi::MX tw = slot(p_, base + 4);   // range-ramp weight (numeric, 0..1)
    const casadi::MX tdx = ts * casadi::MX::cos(tc);
    const casadi::MX tdy = ts * casadi::MX::sin(tc);
    for (int32_t k = 0; k < N; ++k) {
      const casadi::MX kdt = casadi::DM(static_cast<double>(k) * cfg_.dt_s);
      const casadi::MX dx  = x_own[static_cast<std::size_t>(k)] - (tx + tdx * kdt);
      const casadi::MX dy  = y_own[static_cast<std::size_t>(k)] - (ty + tdy * kdt);
      const casadi::MX d   = casadi::MX::sqrt(dx * dx + dy * dy + kSqrtGuard);
      const casadi::MX barrier = casadi::MX::exp(-zeta * (d - cpa));
      // TCPA discount exp(-t_k/T_d): constant per step → numeric coefficient.
      const double disc = std::exp(-(static_cast<double>(k) * cfg_.dt_s)
                                   / cfg_.t_discount_s);
      cost = cost + tw * casadi::DM(disc) * barrier;
    }
  }
  // Per-target per-step average to normalize scale across target/horizon counts.
  const casadi::MX scale_denom = casadi::DM(
      static_cast<double>(std::max(1, Nt * N)));
  return cost / scale_denom;
}

// ===========================================================================
// build_asym_cost_() — smooth, gated Rule-14/15 starboard preference.
//
// Softplus port-penalty: tau*log(1+exp((bearing-psi_k)/tau)). ≈ (bearing-psi_k)
// when psi_k is to port (psi_k < bearing), ≈ 0 to starboard. C∞ smooth (no kink,
// unlike a raw port/stbd multiplier switch). Multiplied by give_way ∈ {0,1} so
// it vanishes for stand-on / no encounter. Biases a symmetric head-on toward
// starboard, preventing the port-turn / course-reversal degenerate optimum.
// Grounded in colav_algorithms NLM (high-conf).
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_asym_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX bearing  = slot(p_, kIdxRouteBearing);
  const casadi::MX give_way = slot(p_, kIdxGiveWay);
  const casadi::MX tau = casadi::DM(cfg_.asym_tau);
  casadi::MX cost(0.0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX z = (bearing - psi_k) / tau;          // >0 when to port
    cost = cost + tau * casadi::MX::log(1.0 + casadi::MX::exp(z));
  }
  return give_way * casadi::DM(cfg_.k_asym) * cost;
}

// ===========================================================================
// build_constraints_() — ROT differential + ConstraintCompiler hard rows.
//
// Heading/speed box limits are NOT here — they are per-variable bounds passed
// to IPOPT as lbx/ubx by MidMpcSolver::solve (see g_dim() rationale).
//
// Constraint convention: g >= 0 (lower bound = 0, upper bound = +inf).
// P1: numeric ConstraintInputs are baked into the graph, so callers rebuild
// after set_constraint_inputs() when targets/rules/zones change.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_constraints_() const {
  const int32_t N = cfg_.n_horizon;

  // ROT differential: |psi[k+1] - psi[k]| <= rot_max*dt for k ∈ [0, N-2].
  // Encoded as TWO smooth linear rows per step rather than one |.| row:
  //   rot_step - dpsi >= 0   (dpsi <=  rot_step)
  //   rot_step + dpsi >= 0   (dpsi >= -rot_step)
  // Exactly equivalent feasible set, but smooth. The abs() form has a gradient
  // kink at dpsi=0 — precisely where a near-constant-heading trajectory sits —
  // so its constraint Jacobian sign-flips every iteration even while the bound
  // is slack, which destabilises the limited-memory Hessian and contributed to
  // intermittent Restoration_Failed / Maximum_Iterations.
  const casadi::MX dpsi = psi_(casadi::Slice(1, N)) - psi_(casadi::Slice(0, N - 1));
  const casadi::MX rot_step = slot(p_, kIdxRotMax) * casadi::DM(cfg_.dt_s);
  const casadi::MX rot_step_rep = casadi::MX::repmat(rot_step, N - 1, 1);
  const casadi::MX g_rot_hi = rot_step_rep - dpsi;
  const casadi::MX g_rot_lo = rot_step_rep + dpsi;
  casadi::MX g_all = casadi::MX::vertcat({g_rot_hi, g_rot_lo});

  const auto rule_cc = compiler_.compile_colregs_rules(
      psi_, u_, constraint_inputs_);
  if (!rule_cc.names.empty()) {
    g_all = casadi::MX::vertcat({g_all, rule_cc.g});
  }
  const auto cpa_cc = compiler_.compile_cpa_distance(
      psi_, u_, constraint_inputs_, cfg_.dt_s);
  if (!cpa_cc.names.empty()) {
    g_all = casadi::MX::vertcat({g_all, cpa_cc.g});
  }
  const auto zone_cc = compiler_.compile_zone_constraints(
      psi_, u_, constraint_inputs_, cfg_.dt_s);
  if (!zone_cc.names.empty()) {
    g_all = casadi::MX::vertcat({g_all, zone_cc.g});
  }
  return g_all;
}

// ===========================================================================
// build_symbolic_graph() — assemble decision vars, parameters, J, g, nlpsol.
// ===========================================================================
void MidMpcNlpFormulation::build_symbolic_graph() {
  const int32_t N = cfg_.n_horizon;
  psi_ = casadi::MX::sym("psi", N, 1);
  u_   = casadi::MX::sym("u",   N, 1);
  p_   = casadi::MX::sym("p", parameter_dim_(), 1);
  const casadi::MX x = casadi::MX::vertcat({psi_, u_});

  // Objective: weighted sum of cost sub-terms (spec §3.2).
  J_ = casadi::DM(cfg_.w_colreg) * build_colreg_cost_()
     + casadi::DM(cfg_.w_dist)   * build_distance_cost_()
     + casadi::DM(cfg_.w_vel)    * build_velocity_cost_()
     + casadi::DM(cfg_.w_route)  * build_route_cost_()   // Slice R1 (§4.3)
     + build_asym_cost_();  // gated starboard preference (give-way only)

  g_ = build_constraints_();
  g_dim_ = static_cast<int32_t>(g_.size1());

  const casadi::MXDict nlp = {{"x", x}, {"p", p_}, {"f", J_}, {"g", g_}};
  casadi::Dict opts;
  opts["ipopt.max_iter"]              = kIpoptMaxIter;
  opts["ipopt.tol"]                   = kIpoptTol;
  opts["ipopt.acceptable_tol"]        = kIpoptAcceptableTol;
  opts["ipopt.acceptable_iter"]       = kIpoptAcceptableIter;
  opts["ipopt.print_level"]           = 0;
  opts["ipopt.linear_solver"]         = std::string{"mumps"};
  opts["ipopt.hessian_approximation"] = std::string{"limited-memory"};
  opts["ipopt.max_cpu_time"]          = kIpoptMaxCpuTime;
  opts["ipopt.bound_push"]            = 1.0e-4;
  opts["ipopt.bound_frac"]            = 1.0e-4;
  opts["ipopt.mu_strategy"]           = std::string{"adaptive"};
  opts["ipopt.constr_viol_tol"]       = 1.0e-3;
  opts["ipopt.acceptable_constr_viol_tol"] = 1.0e-2;
  opts["print_time"]                  = false;
  solver_ = casadi::nlpsol("mid_mpc_solver", "ipopt", nlp, opts);
}

// ===========================================================================
// pack_parameters() — MidMpcInput → DM column vector in p layout.
// ===========================================================================
casadi::DM MidMpcNlpFormulation::pack_parameters(const MidMpcInput& input) const {
  casadi::DM p = casadi::DM::zeros(parameter_dim_(), 1);

  // Initial state.
  // kIdxPsi0/kIdxU0 reserved for Phase E2 warm-start initial heading/speed.
  p(kIdxPsi0) = input.own_ship.psi_rad;
  p(kIdxU0)   = input.own_ship.u_mps;
  p(kIdxX0)   = input.own_ship.x_m;
  p(kIdxY0)   = input.own_ship.y_m;

  // Route + planned speed.
  p(kIdxRouteBearing) = input.planned_route_bearing_rad;
  p(kIdxPlannedSpeed) = input.planned_speed_mps;

  // Constraint bounds + COLREGs reference state from ConstraintInputs.
  p(kIdxHeadingMin) = input.constraints.heading_min_rad;
  p(kIdxHeadingMax) = input.constraints.heading_max_rad;
  p(kIdxSpeedMin)   = input.constraints.speed_min_mps;
  p(kIdxSpeedMax)   = input.constraints.speed_max_mps;
  p(kIdxCpaSafe)    = input.constraints.cpa_safe_m;
  // kIdxOwnPsi reserved for Phase E2 hard COLREGs directional constraints.
  p(kIdxOwnPsi)     = input.constraints.own_ship_psi_rad;

  // [TBD-HAZID] ROT max: from VesselDynamicsModel via MidMpcInput (MUST-5).
  // Fallback to kDefaultRotMaxRadS if MidMpcInput default is unchanged.
  p(kIdxRotMax) = input.rot_max_rad_s;

  // give_way flag: M6 rule 14 (head-on) or 15 (crossing give-way) ⇒ apply
  // starboard asymmetry (build_asym_cost_). Other rules / no encounter ⇒ symmetric.
  bool give_way = false;
  for (const std::uint8_t rule : input.constraints.applicable_rules) {
    if (rule == 14u || rule == 15u) { give_way = true; }
  }
  p(kIdxGiveWay) = give_way ? 1.0 : 0.0;

  // Slice R1: route-frame parameters (spec §4). Computed by assemble_input_.
  // route_weight defaults to 0.0 (inert, High-4 review fix) so legacy callers
  // that do not populate the route frame do not enable J_route. assemble_input_
  // sets route_weight=1.0 only when an active leg exists and the cross-leg guard
  // passes. Origin/normal are the active-leg point + normal in the own-relative
  // NED frame (same frame as kIdxX0/Y0, Critical-2 review fix).
  p(kIdxRouteFrameOriginX) = input.route_frame_origin_x_m;
  p(kIdxRouteFrameOriginY) = input.route_frame_origin_y_m;
  p(kIdxRouteFrameNormalX) = input.route_frame_normal_x;
  p(kIdxRouteFrameNormalY) = input.route_frame_normal_y;
  p(kIdxRouteFrameBearing) = input.route_frame_active_leg_bearing_rad;
  p(kIdxLateralScale)      = (input.lateral_scale_m > 1.0e-6)
                             ? input.lateral_scale_m : 400.0;
  p(kIdxRouteWeight)       = input.route_weight;

  // Slice C1/D1 reserved slots (prefix K, preferred direction, min alteration,
  // role) default to 0 — inactive until those slices populate them.
  p(kIdxPrefixActiveK)    = 0.0;
  p(kIdxPreferredDir)     = 0.0;
  p(kIdxMinAlterationRad) = 0.0;
  p(kIdxRole)             = 0.0;
  for (int32_t k = 0; k < cfg_.n_horizon; ++k) {
    p(kIdxPrefixPsi + k) = 0.0;
    p(kIdxPrefixU   + k) = 0.0;
  }

  // Targets: zero-padded up to cfg_.max_targets.
  const int32_t n_t = std::min(
      static_cast<int32_t>(input.targets.size()), cfg_.max_targets);
  for (int32_t t = 0; t < n_t; ++t) {
    const auto& tgt = input.targets[static_cast<std::size_t>(t)];
    const int32_t base = kIdxTargets + t * kTargetStride;
    p(base + 0) = tgt.x_m;
    p(base + 1) = tgt.y_m;
    p(base + 2) = tgt.cog_rad;
    p(base + 3) = tgt.sog_mps;
    // Range-ramp weight: 0 beyond pwt_outer, linear to 1 at pwt_inner (= cpa_safe).
    // Depends only on initial geometry → computed numerically here (keeps the
    // symbolic NLP smooth; no clamp kink in the graph).
    const double rng0 = std::hypot(tgt.x_m - input.own_ship.x_m,
                                   tgt.y_m - input.own_ship.y_m);
    const double pwt_inner = input.constraints.cpa_safe_m;
    const double span = std::max(cfg_.pwt_outer_m - pwt_inner, 1.0);
    const double w_range = std::clamp((cfg_.pwt_outer_m - rng0) / span, 0.0, 1.0);
    p(base + 4) = w_range;
  }
  return p;
}

// ===========================================================================
// unpack_solution() — IPOPT x* + stats → MidMpcSolution
//
// Phase E1: cost_total / cost_colreg / cost_dist / cost_vel are not split out
// from CasADi stats (would require separate Functions). They remain zero-init.
// ===========================================================================
MidMpcSolution MidMpcNlpFormulation::unpack_solution(
    const casadi::DM& x_opt, const casadi::Dict& stats) const {
  MidMpcSolution sol;
  const int32_t N = cfg_.n_horizon;

  // Map IPOPT return_status string to Status enum.
  if (stats.count("return_status") > 0u) {
    const std::string ipopt_status =
        static_cast<std::string>(stats.at("return_status"));
    if (ipopt_status == "Solve_Succeeded" ||
        ipopt_status == "Feasible_Point_Found" ||
        ipopt_status == "Solved_To_Acceptable_Level") {
      sol.status = MidMpcSolution::Status::Converged;
    } else if (ipopt_status == "Maximum_Iterations_Exceeded" ||
               ipopt_status == "Maximum_CpuTime_Exceeded") {
      sol.status = MidMpcSolution::Status::Timeout;
    } else if (ipopt_status == "Infeasible_Problem_Detected") {
      sol.status = MidMpcSolution::Status::Infeasible;
    } else {
      sol.status = MidMpcSolution::Status::NumericalFailure;
    }

    if (sol.status != MidMpcSolution::Status::Converged) {
      int32_t iter = 0;
      if (stats.count("iter_count") > 0u) {
        iter = static_cast<int32_t>(static_cast<int>(stats.at("iter_count")));
      }
      spdlog::warn("[M5][MidMPC] IPOPT status={} iter={} x_dim={}",
                   ipopt_status, iter,
                   static_cast<int32_t>(x_opt.numel()));
    }
  }

  // Guard against degenerate x_opt (IPOPT failure may return wrong-size vector).
  const int32_t expected_dim = 2 * N;
  if (static_cast<int32_t>(x_opt.numel()) != expected_dim) {
    sol.status = MidMpcSolution::Status::NumericalFailure;
    return sol;  // trajectory stays empty
  }

  sol.trajectory.resize(static_cast<std::size_t>(N));
  for (int32_t k = 0; k < N; ++k) {
    auto& point = sol.trajectory[static_cast<std::size_t>(k)];
    point.psi_rad = static_cast<double>(x_opt(k));
    point.u_mps   = static_cast<double>(x_opt(N + k));
    point.t_s     = static_cast<double>(k) * cfg_.dt_s;
  }
  // Reconstruct position by dead-reckon — NLP optimises x=[psi;u] only, so
  // without this x_m/y_m stay 0 and the tail-gate lateral gate always fails.
  mass_l3::m5::propagate_trajectory_positions(sol.trajectory, cfg_.dt_s);
  if (stats.count("iter_count") > 0u) {
    sol.ipopt_iterations = static_cast<int32_t>(
        static_cast<int>(stats.at("iter_count")));
  }
  return sol;
}

// ===========================================================================
// eval_*_cost — Slice R1 cost-component evaluators (spec §3.2 / §10.1).
//
// Build a one-shot CasADi Function from the symbolic expressions (psi_, u_, p_
// are fixed MX symbols after build_symbolic_graph), then call it at the given
// (x=[psi;u], p). Used by unit tests to assert the COLREG-dominance contract
// without relying on unpack_solution (Phase E1 does not split costs). The
// returned value is the UNWEIGHTED sub-term; callers multiply by the w_* weight.
// @pre build_symbolic_graph() has been called.
// ===========================================================================
double MidMpcNlpFormulation::eval_colreg_cost(
    const casadi::DM& x, const casadi::DM& p) const {
  const int32_t N = cfg_.n_horizon;
  const casadi::DM psi_dm = x(casadi::Slice(0, N), casadi::Slice(0, 1));
  const casadi::DM u_dm   = x(casadi::Slice(N, 2 * N), casadi::Slice(0, 1));
  casadi::Function f("eval_colreg", {psi_, u_, p_}, {build_colreg_cost_()});
  std::vector<casadi::DM> out = f(std::vector<casadi::DM>{psi_dm, u_dm, p});
  return static_cast<double>(casadi::DM::vec(out.at(0))(0));
}

double MidMpcNlpFormulation::eval_dist_cost(
    const casadi::DM& x, const casadi::DM& p) const {
  const int32_t N = cfg_.n_horizon;
  const casadi::DM psi_dm = x(casadi::Slice(0, N), casadi::Slice(0, 1));
  const casadi::DM u_dm   = x(casadi::Slice(N, 2 * N), casadi::Slice(0, 1));
  casadi::Function f("eval_dist", {psi_, u_, p_}, {build_distance_cost_()});
  std::vector<casadi::DM> out = f(std::vector<casadi::DM>{psi_dm, u_dm, p});
  return static_cast<double>(casadi::DM::vec(out.at(0))(0));
}

double MidMpcNlpFormulation::eval_route_cost(
    const casadi::DM& x, const casadi::DM& p) const {
  const int32_t N = cfg_.n_horizon;
  const casadi::DM psi_dm = x(casadi::Slice(0, N), casadi::Slice(0, 1));
  const casadi::DM u_dm   = x(casadi::Slice(N, 2 * N), casadi::Slice(0, 1));
  casadi::Function f("eval_route", {psi_, u_, p_}, {build_route_cost_()});
  std::vector<casadi::DM> out = f(std::vector<casadi::DM>{psi_dm, u_dm, p});
  return static_cast<double>(casadi::DM::vec(out.at(0))(0));
}

}  // namespace mass_l3::m5::mid_mpc
