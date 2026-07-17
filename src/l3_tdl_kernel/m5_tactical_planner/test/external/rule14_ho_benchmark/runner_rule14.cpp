// test/external/rule14_ho_benchmark/runner_rule14.cpp
// P1b-1c Task 19 — Rule14 head-on benchmark runner (IPOPT vs acados).
//
// Single source compiled TWICE: once under M5_USE_ACADOS=OFF (IPOPT kinematics
// backend: MidMpcNlpFormulation + MidMpcSolver), once under =ON (acados Path B
// double-integrator backend: MidMpcAcadosFormulation + MidMpcAcadosSolver).
// Each build runs the SAME Rule14 head-on MidMpcInput, dumps a trajectory JSON
// to stdout. compare.py then checks the 6 behavior-equivalence gates.
//
// WHY COMPILE-TIME BACKEND SELECT (not runtime dispatch): the brief mandates
// two separate builds (M5_USE_ACADOS=OFF / =ON). The MidMpcAcadosSolver ctor
// links the generated acados solver .so which is only present under ON; the
// IPOPT CasADi path is byte-identical under both but we want the OFF build to
// exercise the IPOPT-only path (the production fallback when acados is not
// installed). #ifdef M5_USE_ACADOS picks the backend at compile time — the
// runner source is byte-identical in both builds; only the backend wrapper
// differs. This is the HONEST two-build comparison the spec §P1b-1c demands.
//
// RULE14 HEAD-ON GEOMETRY (mirrors test_mid_mpc_solver.cpp make_head_on_input +
// the HeadOnGiveWayRightTurn test):
//   - Own-ship: heading north (psi=0), 5 m/s, at origin (0,0).
//   - Target: 500 m directly north (x_m=500, y_m=0 in NED where x=north),
//     heading south (cog=pi), 5 m/s. Closing at 10 m/s → head-on (Rule14).
//   - Give-way: own turns starboard (+psi). Rule 14 hard-mandated.
//   - applicable_rules={14}, heading window [-pi/3, +pi/3] (permits port too —
//     the test asserts own picks STARBOARD within the window).
//
// route_weight=1.0 for BOTH backends (production-normal, T17/T18 finding):
//   - mid_mpc_node.cpp:746 sets route_weight = guard.crosses_corner ? 0.0 : 1.0
//     — a real ship HAS a route it is tracking when the head-on target appears.
//   - route_weight=0 is the non-physical placeholder for no-L2-route / cross-
//     corner; the Rule14 scenario is the OPPOSITE (own is on its active leg).
//   - T17 confirmed acatos cold-capsule warm-up is route_weight-INDEPENDENT;
//     T18 confirmed IPOPT handles route_weight=1.0 fine. Fair comparison.
//
// HORIZON N=18 (production): both formulations use the production horizon
// (90 s @ dt=5 s). The acados generated .so is codegen'd for N=18 (cannot be
// changed at runtime); the IPOPT Config is set to n_horizon=18 to match so
// trajectory-size parity is exact. The IPOPT test fixture uses N=8 for speed
// but that does not match the acados codegen — N=18 is the production value.
//
// COST WEIGHTS: both backends use w_colreg=30.0, w_dist=10.0, w_route=3.0,
// w_vel=1.0 (the documented defaults of BOTH formulations — verified identical
// in mid_mpc_nlp_formulation.hpp:92-107 and mid_mpc_acados_formulation.hpp:
// 163-166). The T18 parity test sets the same IPOPT weights to match the
// acados defaults. Fair cost-landscape comparison.
//
// acatos WARM-UP: the MidMpcAcadosSolver ctor runs the cold-capsule warm-up
// (T17 final-fix, 2 throwaway benign solves) so the first REAL solve() sees a
// primed capsule. The runner constructs the solver fresh (one ctor) and solves
// once — exactly the warm-up contract T17 locked. Do NOT add separate warm-up.
//
// DISCIPLINE: NO mocks, NO skips, NO forced-pass, NO threshold-tuning. The
// runner dumps whatever the solver produces; compare.py applies the gates. If
// a backend fails to converge the head-on scenario, the runner reports the
// raw status and the gate classification happens in compare.py + the report.
// Per spec §P1b-1c failure-handling: gate 3 (trajectory shape) may FAIL for
// REAL physics difference (Path B double-integrator turning diameter vs IPOPT
// kinematics) — that is an EXPECTED possible outcome, handled as analysis not
// a forced-pass. See compare.py + task-19-report.md for the classification.
//
// CasADi LGPL-3.0 / acados C lib 2-Clause BSD: internal MISRA violations
// exempted per coding-standards.md §10.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

#ifdef M5_USE_ACADOS
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"
#endif

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;

#ifdef M5_USE_ACADOS
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::mid_mpc::MidMpcAcadosSolver;
constexpr const char* kBackend = "acados";
#else
constexpr const char* kBackend = "ipopt";
#endif

namespace {

// Production horizon (matches acados codegen .so, cannot change at runtime).
// 90 s @ dt=5 s. Both backends configured to this N for trajectory-size parity.
constexpr int32_t kHorizon = 18;
constexpr double kDt = 5.0;
constexpr double kShipLengthM = 45.0;   // IMO MSC.137(76) turning indices (L)

// Build the Rule14 head-on MidMpcInput — byte-identical for BOTH backends.
// Mirrors test_mid_mpc_solver.cpp make_head_on_input() + HeadOnGiveWayRightTurn
// test: target 500 m north southbound, own northbound → head-on, give-way
// starboard. route_weight=1.0 (production-normal, see file header rationale).
MidMpcInput make_head_on_rule14_input() {
  MidMpcInput inp;
  // Own-ship: heading north, 5 m/s, at origin.
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.u_mps   = 5.0;
  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  // Route: also north, planned speed matches own (J_vel cost-optimum at seed).
  inp.planned_route_bearing_rad = 0.0;
  inp.planned_speed_mps         = 5.0;
  // Constraint envelope (matches the parity test straight_line_input; the
  // Rule14 give-way WINDOW is set below as the Rule14-specific override).
  inp.constraints.heading_min_rad = -M_PI / 3.0;  // Rule14 window permits port
  inp.constraints.heading_max_rad =  M_PI / 3.0;  // too; own must pick stbd.
  inp.constraints.speed_min_mps   = 0.0;
  inp.constraints.speed_max_mps   = 15.0;
  inp.constraints.cpa_safe_m      = 1852.0;       // 1 NM ODD default
  // Phase E2: mirror own heading into the COLREGs directional reference.
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
  // Rule14 give-way applicable rule.
  inp.constraints.applicable_rules = {14};
  // Production-realistic route frame (route_weight=1.0 rationale). Own is ON
  // the active leg at its origin; eastward active-leg normal for bearing=0.
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  inp.route_frame_normal_x   = 0.0;
  inp.route_frame_normal_y   = 1.0;
  inp.route_frame_active_leg_bearing_rad = 0.0;
  inp.lateral_scale_m        = 400.0;  // GncExecutionOdd.max_lateral_offset_m
  inp.route_weight           = 1.0;    // active cross-leg guard (normal ops)
  // Head-on target: 500 m directly north, heading south, 5 m/s.
  TargetState tgt;
  tgt.x_m     =  500.0;
  tgt.y_m     =    0.0;
  tgt.cog_rad =  M_PI;     // heading south (NED: pi = south, positive clockwise)
  tgt.sog_mps =    5.0;    // closing at own+target = 10 m/s
  tgt.cpa_m   =    0.0;    // clamped to kMinCpaForWeight=1.0 → tw=1.0 (max weight)
  tgt.tcpa_s  =    0.0;    // clamped to 1.0
  inp.targets.push_back(tgt);
  // Synchronize the constraint-context targets + own-psi mirror (the node
  // does this in synchronize_mid_mpc_constraint_context; the runner must too
  // so the formulation's pack_parameters sees the same field set the node
  // delivers in production).
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
  return inp;
}

// JSON-escape a bare token (backend label / status name — no user input, so the
// only char of concern is none, but keep it defensive).
std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;
    }
  }
  return out;
}

const char* status_name(MidMpcSolution::Status s) {
  switch (s) {
    case MidMpcSolution::Status::Converged:        return "Converged";
    case MidMpcSolution::Status::Timeout:          return "Timeout";
    case MidMpcSolution::Status::Infeasible:       return "Infeasible";
    case MidMpcSolution::Status::NumericalFailure: return "NumericalFailure";
    case MidMpcSolution::Status::NotInitialized:   return "NotInitialized";
  }
  return "Unknown";
}

// Manually compute the minimum own-target distance over the trajectory (CPA
// proxy). The formulation's own cpa_slack is the per-target slack at the
// optimum (0 if no slack needed); this is an INDEPENDENT trajectory-level
// CPA measurement that compare.py uses for gate 2 (CPA-feasible). Both the
// own-ship trajectory (x_m/y_m) AND a constant-velocity target projection
// are evaluated: target[k] = target_0 + sog * (cos,sin)(cog) * k * dt.
double min_trajectory_cpa_m(const MidMpcSolution& sol, const TargetState& tgt,
                            double dt) {
  double min_d2 = std::numeric_limits<double>::infinity();
  const double tx0 = tgt.x_m;
  const double ty0 = tgt.y_m;
  const double vc = std::cos(tgt.cog_rad) * tgt.sog_mps;
  const double vs = std::sin(tgt.cog_rad) * tgt.sog_mps;
  for (std::size_t k = 0; k < sol.trajectory.size(); ++k) {
    const double tk = static_cast<double>(k) * dt;
    const double tx = tx0 + vc * tk;
    const double ty = ty0 + vs * tk;
    const double dx = sol.trajectory[k].x_m - tx;
    const double dy = sol.trajectory[k].y_m - ty;
    const double d2 = dx * dx + dy * dy;
    min_d2 = std::min(min_d2, d2);
  }
  return std::isfinite(min_d2) ? std::sqrt(min_d2)
                               : std::numeric_limits<double>::infinity();
}

}  // namespace

int main() {
  const MidMpcInput in = make_head_on_rule14_input();

  MidMpcSolution sol;
  int raw_acados_status = -1;
  int sqp_iter = -1;
  double traj_delta = std::numeric_limits<double>::quiet_NaN();

#ifdef M5_USE_ACADOS
  // ---- acados backend ----
  // The ctor runs the cold-capsule warm-up (T17 final-fix) so the first REAL
  // solve() sees a primed capsule. build_symbolic_graph() is required pre-cond
  // (MX dimensions read for parity asserts; the actual solver graph is the
  // codegen SX in c_generated_code/).
  auto form = std::make_unique<MidMpcAcadosFormulation>();
  form->build_symbolic_graph();
  auto solver = std::make_unique<MidMpcAcadosSolver>(*form);
  // S1 safety gate: if the ctor warm-up failed the capsule is suspect — the
  // production dispatch would fall back to IPOPT. For the BENCHMARK we still
  // run the solve (we want to see what the suspect capsule produces) but we
  // record warm_up_succeeded so compare.py / the report can flag it.
  const bool warm_up_ok = solver->warm_up_succeeded();
  sol = solver->solve(in, nullptr);
  raw_acados_status = solver->last_raw_status();
  sqp_iter = solver->last_sqp_iter();
  traj_delta = solver->last_traj_delta();
#else
  // ---- IPOPT backend ----
  // Cost weights EXACTLY match the acados formulation defaults (w_colreg=30,
  // w_dist=10, w_route=3, w_vel=1) so the cost landscape is the same shape.
  MidMpcNlpFormulation::Config icfg;
  icfg.n_horizon   = kHorizon;
  icfg.dt_s        = kDt;
  icfg.w_colreg    = 30.0;
  icfg.w_dist      = 10.0;
  icfg.w_route     = 3.0;
  icfg.w_vel       = 1.0;
  icfg.max_targets = 16;
  auto form = std::make_unique<MidMpcNlpFormulation>(icfg);
  form->build_symbolic_graph();
  MidMpcSolver::IpoptOptions opts;
  opts.max_iter  = 150;
  opts.tol       = 1.0e-4;
  opts.timeout_s = 5.0;  // N=18 heavier than the N=8 fixture; relaxed for parity
  auto solver = std::make_unique<MidMpcSolver>(*form, opts);
  sol = solver->solve(in, nullptr);
  const bool warm_up_ok = true;  // IPOPT has no cold-capsule concept
#endif

  // Trajectory-level CPA (independent of the solver's own cpa_slack field).
  const double traj_cpa_m =
      in.targets.empty()
          ? std::numeric_limits<double>::infinity()
          : min_trajectory_cpa_m(sol, in.targets.front(), kDt);

  // ---- dump JSON to stdout ----
  // Schema (consumed by compare.py):
  //   backend, ship_length_m, scenario{...}, target{...},
  //   status{name, code}, cost_total, cost_colreg, cost_dist, cost_vel,
  //   cpa_slack (solver-reported), trajectory_cpa_m (independent measurement),
  //   solve_duration_ms, iterations, horizon, dt_s,
  //   acados_diag{raw_status, sqp_iter, traj_delta, warm_up_ok},
  //   trajectory[{k, t_s, x_m, y_m, psi_rad, u_mps, v_mps, r_rad_s}]
  std::printf("{\n");
  std::printf("  \"backend\": \"%s\",\n", json_escape(kBackend).c_str());
  std::printf("  \"ship_length_m\": %.3f,\n", kShipLengthM);
  std::printf("  \"scenario\": {\n");
  std::printf("    \"name\": \"rule14_head_on\",\n");
  std::printf("    \"rule\": 14,\n");
  std::printf("    \"own_psi_rad\": %.6f,\n", in.own_ship.psi_rad);
  std::printf("    \"own_u_mps\": %.6f,\n", in.own_ship.u_mps);
  std::printf("    \"own_x_m\": %.6f,\n", in.own_ship.x_m);
  std::printf("    \"own_y_m\": %.6f,\n", in.own_ship.y_m);
  std::printf("    \"planned_route_bearing_rad\": %.6f,\n",
              in.planned_route_bearing_rad);
  std::printf("    \"planned_speed_mps\": %.6f,\n", in.planned_speed_mps);
  std::printf("    \"heading_min_rad\": %.6f,\n",
              in.constraints.heading_min_rad);
  std::printf("    \"heading_max_rad\": %.6f,\n",
              in.constraints.heading_max_rad);
  std::printf("    \"cpa_safe_m\": %.6f,\n", in.constraints.cpa_safe_m);
  std::printf("    \"route_weight\": %.6f,\n", in.route_weight);
  std::printf("    \"applicable_rules\": [14]\n");
  std::printf("  },\n");
  if (!in.targets.empty()) {
    const auto& t = in.targets.front();
    std::printf("  \"target\": {\n");
    std::printf("    \"x_m\": %.6f,\n", t.x_m);
    std::printf("    \"y_m\": %.6f,\n", t.y_m);
    std::printf("    \"cog_rad\": %.6f,\n", t.cog_rad);
    std::printf("    \"sog_mps\": %.6f\n", t.sog_mps);
    std::printf("  },\n");
  }
  std::printf("  \"status\": {\"name\": \"%s\", \"code\": %d},\n",
              status_name(sol.status), static_cast<int>(sol.status));
  std::printf("  \"usable\": %s,\n",
              (sol.status != MidMpcSolution::Status::NotInitialized &&
               sol.status != MidMpcSolution::Status::NumericalFailure &&
               sol.status != MidMpcSolution::Status::Infeasible)
                  ? "true"
                  : "false");
  std::printf("  \"cost_total\": %.6f,\n", sol.cost_total);
  std::printf("  \"cost_colreg\": %.6f,\n", sol.cost_colreg);
  std::printf("  \"cost_dist\": %.6f,\n", sol.cost_dist);
  std::printf("  \"cost_vel\": %.6f,\n", sol.cost_vel);
  std::printf("  \"cpa_slack\": %.6f,\n", sol.cpa_slack);
  std::printf("  \"trajectory_cpa_m\": %.6f,\n", traj_cpa_m);
  std::printf("  \"solve_duration_ms\": %d,\n", sol.solve_duration_ms);
  std::printf("  \"iterations\": %d,\n", sol.ipopt_iterations);
  std::printf("  \"horizon\": %d,\n", kHorizon);
  std::printf("  \"dt_s\": %.3f,\n", kDt);
#ifdef M5_USE_ACADOS
  std::printf("  \"acados_diag\": {\"raw_status\": %d, \"sqp_iter\": %d, "
              "\"traj_delta\": %.6e, \"warm_up_ok\": %s},\n",
              raw_acados_status, sqp_iter, traj_delta,
              warm_up_ok ? "true" : "false");
#else
  (void)raw_acados_status;
  (void)sqp_iter;
  (void)traj_delta;
  (void)warm_up_ok;
#endif
  std::printf("  \"trajectory\": [");
  for (std::size_t k = 0; k < sol.trajectory.size(); ++k) {
    const auto& p = sol.trajectory[k];
    std::printf("%s\n    {\"k\": %zu, \"t_s\": %.3f, \"x_m\": %.6f, "
                "\"y_m\": %.6f, \"psi_rad\": %.6f, \"u_mps\": %.6f, "
                "\"v_mps\": %.6f, \"r_rad_s\": %.6f}",
                (k == 0 ? "" : ","), k, p.t_s, p.x_m, p.y_m, p.psi_rad,
                p.u_mps, p.v_mps, p.r_rad_s);
  }
  std::printf(sol.trajectory.empty() ? "]\n" : "\n  ]\n");
  std::printf("}\n");
  return 0;
}
