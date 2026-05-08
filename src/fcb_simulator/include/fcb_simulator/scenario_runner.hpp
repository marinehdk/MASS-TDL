// SPDX-License-Identifier: Proprietary
// D1.3.1 qualification hook: run standardised ship manoeuvres and return metrics.
#pragma once
#include <vector>
#include "ship_sim_interfaces/ship_state.hpp"
#include "fcb_simulator/types.hpp"

namespace fcb_sim {

enum class ManeuverType {
  STRAIGHT_DECEL,
  STANDARD_TURN_35DEG,
  ZIG_ZAG_10_10,
};

struct ScenarioResult {
  double advance_m{0.0};
  double tactical_diameter_m{0.0};
  double stop_distance_m{0.0};
  double first_overshoot_deg{0.0};
  std::vector<double> time_s;
  std::vector<double> x_m;
  std::vector<double> y_m;
  std::vector<double> psi_deg;
};

// Uses YAML-loaded MmgParams (preferred for D1.3.1 qualification).
ScenarioResult run_scenario(ManeuverType type,
                            const ship_sim::ShipState& initial,
                            const MmgParams& params,
                            double dt = 0.02);

// Uses default MmgParams (HAZID-UNVERIFIED RUN-001 values only; for smoke tests).
ScenarioResult run_scenario(ManeuverType type,
                            const ship_sim::ShipState& initial,
                            double dt = 0.02);

}  // namespace fcb_sim
