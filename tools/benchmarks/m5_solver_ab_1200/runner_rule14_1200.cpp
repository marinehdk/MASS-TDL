#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

using mass_l3::m5::ColregsPreferredDirection;
using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::mid_mpc::MidMpcAcadosSolver;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;
using mass_l3::m5::tail_builder::EncounterState;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int32_t kN = 80;
constexpr double kDtS = 15.0;
constexpr double kHorizonS = 1200.0;
constexpr double kInitialRangeM = 5000.0;
constexpr double kLateralOffsetM = -60.0;
constexpr double kOwnSpeedMps = 3.0;
constexpr double kTargetSpeedMps = 3.0;
constexpr double kCpaM = std::fabs(kLateralOffsetM);
constexpr double kTargetNorthM =
    std::sqrt(kInitialRangeM * kInitialRangeM -
              kLateralOffsetM * kLateralOffsetM);
constexpr double kTcpaS = kTargetNorthM / (kOwnSpeedMps + kTargetSpeedMps);
constexpr double kCpaFloorM = 1852.0;
constexpr double kRotMaxRadS = 4.7 * M_PI / 180.0;
constexpr double kDecelMaxMps2 = 0.08;

double elapsed_ms(const Clock::time_point& begin, const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

const char* status_name(MidMpcSolution::Status status) {
  switch (status) {
    case MidMpcSolution::Status::Converged: return "Converged";
    case MidMpcSolution::Status::Timeout: return "Timeout";
    case MidMpcSolution::Status::Infeasible: return "Infeasible";
    case MidMpcSolution::Status::NumericalFailure: return "NumericalFailure";
    case MidMpcSolution::Status::NotInitialized: return "NotInitialized";
  }
  return "Unknown";
}

MidMpcInput make_input() {
  MidMpcInput inp;
  inp.own_ship.x_m = 0.0;
  inp.own_ship.y_m = 0.0;
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.u_mps = kOwnSpeedMps;
  inp.own_ship.r_rad_s = 0.0;

  inp.planned_route_bearing_rad = 0.0;
  inp.planned_speed_mps = kOwnSpeedMps;
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  inp.route_frame_normal_x = 0.0;
  inp.route_frame_normal_y = 1.0;
  inp.route_frame_active_leg_bearing_rad = 0.0;
  inp.route_weight = 1.0;
  inp.lateral_scale_m = 400.0;

  inp.constraints.heading_min_rad = -M_PI / 3.0;
  inp.constraints.heading_max_rad = M_PI / 3.0;
  inp.constraints.speed_min_mps = 0.0;
  inp.constraints.speed_max_mps = 15.0;
  inp.constraints.cpa_safe_m = kCpaFloorM;
  inp.constraints.cpa_hard_m = kCpaFloorM;
  inp.constraints.own_ship_psi_rad = 0.0;
  inp.constraints.applicable_rules = {14U};

  inp.colregs_conflict_active = true;
  inp.colregs_primary_role = 2U;  // BOTH_GIVE_WAY
  inp.colregs_preferred_direction = ColregsPreferredDirection::Starboard;
  inp.colregs_min_alteration_rad = 15.0 * M_PI / 180.0;
  inp.colregs_encounter_state = EncounterState::Onset;
  inp.has_m6_encounter_state = true;
  inp.colregs_phase = "T_act";
  inp.rot_max_rad_s = kRotMaxRadS;
  inp.decel_max_mps2 = kDecelMaxMps2;

  TargetState tgt;
  tgt.id = 1;
  tgt.x_m = kTargetNorthM;
  tgt.y_m = kLateralOffsetM;
  tgt.cog_rad = M_PI;
  tgt.sog_mps = kTargetSpeedMps;
  tgt.cpa_m = kCpaM;
  tgt.cpa_sigma_m = 0.0;
  tgt.tcpa_s = kTcpaS;
  tgt.confidence = 1.0;
  tgt.predicted_intent = TargetState::Intent::Maintain;
  tgt.intent_confidence = 1.0;
  tgt.target_compliance = 1.0;
  tgt.classification = TargetState::Classification::Vessel;
  inp.targets.push_back(tgt);
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
  return inp;
}

struct TrajectoryMetrics {
  double min_cpa_m{std::numeric_limits<double>::infinity()};
  double max_abs_rot_rad_s{0.0};
  double max_decel_mps2{0.0};
  double max_starboard_rad{0.0};
  double terminal_x_m{0.0};
  double terminal_y_m{0.0};
  double terminal_psi_rad{0.0};
};

TrajectoryMetrics trajectory_metrics(const MidMpcSolution& sol) {
  TrajectoryMetrics out;
  double previous_psi = 0.0;
  double previous_u = kOwnSpeedMps;
  for (std::size_t k = 0; k < sol.trajectory.size(); ++k) {
    const auto& point = sol.trajectory[k];
    const double t = static_cast<double>(k) * kDtS;
    const double target_x = kTargetNorthM - kTargetSpeedMps * t;
    const double target_y = kLateralOffsetM;
    out.min_cpa_m = std::min(
        out.min_cpa_m,
        std::hypot(point.x_m - target_x, point.y_m - target_y));
    const double dpsi = std::atan2(
        std::sin(point.psi_rad - previous_psi),
        std::cos(point.psi_rad - previous_psi));
    out.max_abs_rot_rad_s =
        std::max(out.max_abs_rot_rad_s, std::fabs(dpsi) / kDtS);
    out.max_decel_mps2 =
        std::max(out.max_decel_mps2, (previous_u - point.u_mps) / kDtS);
    out.max_starboard_rad = std::max(out.max_starboard_rad, point.psi_rad);
    previous_psi = point.psi_rad;
    previous_u = point.u_mps;
  }
  if (!sol.trajectory.empty()) {
    const auto& terminal = sol.trajectory.back();
    out.terminal_x_m = terminal.x_m;
    out.terminal_y_m = terminal.y_m;
    out.terminal_psi_rad = terminal.psi_rad;
  }
  return out;
}

void print_trajectory(const std::vector<TrajectoryPoint>& trajectory) {
  std::cout << "[";
  for (std::size_t k = 0; k < trajectory.size(); ++k) {
    const auto& p = trajectory[k];
    if (k != 0) std::cout << ",";
    std::cout << "{\"k\":" << k
              << ",\"t_s\":" << static_cast<double>(k) * kDtS
              << ",\"x_m\":" << p.x_m
              << ",\"y_m\":" << p.y_m
              << ",\"psi_rad\":" << p.psi_rad
              << ",\"u_mps\":" << p.u_mps << "}";
  }
  std::cout << "]";
}

int warm_runs_from_env() {
  const char* value = std::getenv("BENCH_WARM_RUNS");
  if (value == nullptr) return 10;
  return std::max(1, std::atoi(value));
}

}  // namespace

int main() {
  std::cout << std::setprecision(12);
  const int warm_runs = warm_runs_from_env();
  const MidMpcInput input = make_input();
#ifdef M5_USE_ACADOS
  const auto dispatch = MidMpcSolver::compute_acatos_feasibility(input);
  const bool acados_dispatched = dispatch.acatos_dispatched;
  const std::string dispatch_reason = dispatch.reason;
  const bool dispatch_c1_pass = dispatch.c1_pass;
  const std::string dispatch_c2_state = dispatch.c2_state;
  const double dispatch_c3_gap_h_m = dispatch.c3_gap_h_m;
  const double dispatch_c4_r_reach = dispatch.c4_r_reach;
  const double dispatch_c5_align_sin = dispatch.c5_align_sin;
#else
  const bool acados_dispatched = false;
  const std::string dispatch_reason = "not_compiled_in_ipopt_binary";
  const bool dispatch_c1_pass = false;
  const std::string dispatch_c2_state = "NOT_EVALUATED";
  const double dispatch_c3_gap_h_m = 0.0;
  const double dispatch_c4_r_reach = 0.0;
  const double dispatch_c5_align_sin = 0.0;
#endif

  double graph_build_ms = 0.0;
  double solver_ctor_ms = 0.0;
  MidMpcSolution cold;
  std::vector<MidMpcSolution> warm_solutions;
  std::vector<double> warm_wall_ms;
  std::vector<int32_t> warm_iterations;
  std::vector<int> warm_statuses;
  warm_solutions.reserve(static_cast<std::size_t>(warm_runs));
  warm_wall_ms.reserve(static_cast<std::size_t>(warm_runs));

#ifdef M5_USE_ACADOS
  constexpr const char* backend = "acados";
  MidMpcAcadosFormulation::Config cfg;
  cfg.n_horizon = kN;
  cfg.dt_s = kDtS;
  cfg.w_colreg = 30.0;
  cfg.w_dist = 10.0;
  cfg.w_route = 3.0;
  cfg.w_vel = 1.0;
  cfg.max_targets = 16;
  const auto graph_start = Clock::now();
  auto form = std::make_unique<MidMpcAcadosFormulation>(cfg);
  form->build_symbolic_graph();
  const auto graph_end = Clock::now();
  const auto ctor_start = Clock::now();
  auto solver = std::make_unique<MidMpcAcadosSolver>(*form);
  const auto ctor_end = Clock::now();
  graph_build_ms = elapsed_ms(graph_start, graph_end);
  solver_ctor_ms = elapsed_ms(ctor_start, ctor_end);
  const auto cold_start = Clock::now();
  cold = solver->solve(input, nullptr);
  const auto cold_end = Clock::now();
  const double cold_wall_ms = elapsed_ms(cold_start, cold_end);
  MidMpcSolution previous = cold;
  for (int i = 0; i < warm_runs; ++i) {
    const MidMpcSolution* warm_ptr = previous.trajectory.empty() ? nullptr : &previous;
    const auto begin = Clock::now();
    auto result = solver->solve(input, warm_ptr);
    const auto end = Clock::now();
    warm_wall_ms.push_back(elapsed_ms(begin, end));
    warm_iterations.push_back(solver->last_sqp_iter());
    warm_statuses.push_back(static_cast<int>(result.status));
    previous = result;
    warm_solutions.push_back(std::move(result));
  }
  const int cold_raw_status = solver->last_raw_status();
  const int cold_iterations = cold.ipopt_iterations;
  const bool warmup_ok = solver->warm_up_succeeded();
#else
  constexpr const char* backend = "ipopt";
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = kN;
  cfg.dt_s = kDtS;
  cfg.w_colreg = 30.0;
  cfg.w_dist = 10.0;
  cfg.w_route = 3.0;
  cfg.w_vel = 1.0;
  cfg.max_targets = 16;
  const auto graph_start = Clock::now();
  auto form = std::make_unique<MidMpcNlpFormulation>(cfg);
  form->build_symbolic_graph();
  const auto graph_end = Clock::now();
  MidMpcSolver::IpoptOptions options;
  options.max_iter = 1500;
  options.tol = 1.0e-6;
  options.timeout_s = 60.0;
  const auto ctor_start = Clock::now();
  auto solver = std::make_unique<MidMpcSolver>(*form, options);
  const auto ctor_end = Clock::now();
  graph_build_ms = elapsed_ms(graph_start, graph_end);
  solver_ctor_ms = elapsed_ms(ctor_start, ctor_end);
  const auto cold_start = Clock::now();
  cold = solver->solve(input, nullptr);
  const auto cold_end = Clock::now();
  const double cold_wall_ms = elapsed_ms(cold_start, cold_end);
  MidMpcSolution previous = cold;
  for (int i = 0; i < warm_runs; ++i) {
    const MidMpcSolution* warm_ptr = previous.trajectory.empty() ? nullptr : &previous;
    const auto begin = Clock::now();
    auto result = solver->solve(input, warm_ptr);
    const auto end = Clock::now();
    warm_wall_ms.push_back(elapsed_ms(begin, end));
    warm_iterations.push_back(result.ipopt_iterations);
    warm_statuses.push_back(static_cast<int>(result.status));
    previous = result;
    warm_solutions.push_back(std::move(result));
  }
  const int cold_raw_status = -1;
  const int cold_iterations = cold.ipopt_iterations;
  const bool warmup_ok = true;
#endif

  const double warm_sum_ms =
      std::accumulate(warm_wall_ms.begin(), warm_wall_ms.end(), 0.0);
  const double warm_mean_ms = warm_sum_ms / static_cast<double>(warm_wall_ms.size());
  const TrajectoryMetrics cold_metrics = trajectory_metrics(cold);
  const auto& final_solution = warm_solutions.empty() ? cold : warm_solutions.back();
  const TrajectoryMetrics final_metrics = trajectory_metrics(final_solution);

  std::cout << "{"
            << "\"backend\":\"" << backend << "\","
            << "\"scenario\":{"
            << "\"name\":\"rule14_near_head_on_5000m\","
            << "\"range_m\":" << kInitialRangeM << ","
            << "\"lateral_offset_m\":" << kLateralOffsetM << ","
            << "\"own_speed_mps\":" << kOwnSpeedMps << ","
            << "\"target_speed_mps\":" << kTargetSpeedMps << ","
            << "\"tcpa_s\":" << kTcpaS << ","
            << "\"initial_cpa_m\":" << kCpaM << ","
            << "\"cpa_floor_m\":" << kCpaFloorM << ","
            << "\"horizon_s\":" << kHorizonS << ","
            << "\"n\":" << kN << ",\"dt_s\":" << kDtS << "},"
            << "\"dispatch\":{"
            << "\"acados_dispatched\":" << (acados_dispatched ? "true" : "false") << ","
            << "\"reason\":\"" << dispatch_reason << "\","
            << "\"c1_pass\":" << (dispatch_c1_pass ? "true" : "false") << ","
            << "\"c2_state\":\"" << dispatch_c2_state << "\","
            << "\"c3_gap_h_m\":" << dispatch_c3_gap_h_m << ","
            << "\"c4_r_reach\":" << dispatch_c4_r_reach << ","
            << "\"c5_align_sin\":" << dispatch_c5_align_sin << "},"
            << "\"timing\":{"
            << "\"graph_build_ms\":" << graph_build_ms << ","
            << "\"solver_ctor_warmup_ms\":" << solver_ctor_ms << ","
            << "\"startup_total_ms\":" << graph_build_ms + solver_ctor_ms << ","
            << "\"cold_solve_wall_ms\":" << cold_wall_ms << ","
            << "\"warm_sum_ms\":" << warm_sum_ms << ","
            << "\"warm_mean_ms\":" << warm_mean_ms << ","
            << "\"warm_mean_per_step_ms\":" << warm_mean_ms / static_cast<double>(kN) << ","
            << "\"benchmark_total_ms\":"
            << graph_build_ms + solver_ctor_ms + cold_wall_ms + warm_sum_ms << "},"
            << "\"cold\":{"
            << "\"status\":\"" << status_name(cold.status) << "\","
            << "\"status_code\":" << static_cast<int>(cold.status) << ","
            << "\"raw_status\":" << cold_raw_status << ","
            << "\"iterations\":" << cold_iterations << ","
            << "\"reported_solve_ms\":" << cold.solve_duration_ms << ","
            << "\"cost_total\":" << cold.cost_total << ","
            << "\"cpa_slack\":" << cold.cpa_slack << ","
            << "\"trajectory_size\":" << cold.trajectory.size() << ","
            << "\"min_cpa_m\":" << cold_metrics.min_cpa_m << ","
            << "\"max_abs_rot_rad_s\":" << cold_metrics.max_abs_rot_rad_s << ","
            << "\"max_decel_mps2\":" << cold_metrics.max_decel_mps2 << ","
            << "\"max_starboard_rad\":" << cold_metrics.max_starboard_rad << ","
            << "\"terminal_y_m\":" << cold_metrics.terminal_y_m << ","
            << "\"terminal_psi_rad\":" << cold_metrics.terminal_psi_rad << "},"
            << "\"warmup_ok\":" << (warmup_ok ? "true" : "false") << ","
            << "\"warm_wall_ms\":[";
  for (std::size_t i = 0; i < warm_wall_ms.size(); ++i) {
    if (i != 0) std::cout << ",";
    std::cout << warm_wall_ms[i];
  }
  std::cout << "],\"warm_statuses\":[";
  for (std::size_t i = 0; i < warm_statuses.size(); ++i) {
    if (i != 0) std::cout << ",";
    std::cout << warm_statuses[i];
  }
  std::cout << "],\"warm_iterations\":[";
  for (std::size_t i = 0; i < warm_iterations.size(); ++i) {
    if (i != 0) std::cout << ",";
    std::cout << warm_iterations[i];
  }
  std::cout << "],\"final\":{"
            << "\"status\":\"" << status_name(final_solution.status) << "\","
            << "\"status_code\":" << static_cast<int>(final_solution.status) << ","
            << "\"trajectory_size\":" << final_solution.trajectory.size() << ","
            << "\"min_cpa_m\":" << final_metrics.min_cpa_m << ","
            << "\"max_abs_rot_rad_s\":" << final_metrics.max_abs_rot_rad_s << ","
            << "\"max_decel_mps2\":" << final_metrics.max_decel_mps2 << ","
            << "\"max_starboard_rad\":" << final_metrics.max_starboard_rad << ","
            << "\"terminal_y_m\":" << final_metrics.terminal_y_m << ","
            << "\"terminal_psi_rad\":" << final_metrics.terminal_psi_rad << "},"
            << "\"cold_trajectory\":";
  print_trajectory(cold.trajectory);
  std::cout << "}\n";
  return 0;
}
