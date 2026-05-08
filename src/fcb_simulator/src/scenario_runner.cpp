// SPDX-License-Identifier: Proprietary
#include "fcb_simulator/scenario_runner.hpp"
#include <cmath>
#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace fcb_sim {
namespace {

constexpr double kPi       = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;

FcbState from_ship_state(const ship_sim::ShipState& s) {
  FcbState fs;
  fs.x = s.x; fs.y = s.y; fs.psi = s.psi;
  fs.u = s.u; fs.v = s.v; fs.r   = s.r;
  fs.phi = s.phi; fs.phi_dot = s.phi_dot;
  return fs;
}

double wrap_180(double deg) {
  while (deg >  180.0) deg -= 360.0;
  while (deg < -180.0) deg += 360.0;
  return deg;
}

}  // namespace

ScenarioResult run_scenario(ManeuverType type, const ship_sim::ShipState& initial, double dt) {
  ScenarioResult res;
  MmgParams p;
  FcbState s = from_ship_state(initial);

  switch (type) {
    case ManeuverType::STRAIGHT_DECEL: {
      const double x0 = s.x, y0 = s.y;
      const int max_steps = static_cast<int>(600.0 / dt);
      for (int i = 0; i < max_steps; ++i) {
        s = rk4_step(s, 0.0, 0.0, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x); res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);
        if (!std::isfinite(s.u) || s.u < 0.1) break;
      }
      res.stop_distance_m = std::hypot(s.x - x0, s.y - y0);
      break;
    }
    case ManeuverType::STANDARD_TURN_35DEG: {
      const double delta_35 = 35.0 * kPi / 180.0;
      const int steps = static_cast<int>(180.0 / dt);
      const double psi0_deg = s.psi * kRadToDeg;
      double y_min = s.y, y_max = s.y;
      double advance_candidate = 0.0;
      bool heading_90_passed = false;
      const double y0 = s.y;
      for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, delta_35, 5.0, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x); res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);
        y_min = std::min(y_min, s.y); y_max = std::max(y_max, s.y);
        double psi_change = std::abs(wrap_180(s.psi * kRadToDeg - psi0_deg));
        if (!heading_90_passed && psi_change >= 90.0) {
          advance_candidate = std::abs(s.y - y0);
          heading_90_passed = true;
        }
      }
      res.advance_m = advance_candidate;
      res.tactical_diameter_m = y_max - y_min;
      break;
    }
    case ManeuverType::ZIG_ZAG_10_10: {
      const double delta_10 = 10.0 * kPi / 180.0;
      const int steps = static_cast<int>(300.0 / dt);
      const double psi0_deg = s.psi * kRadToDeg;
      double current_delta = delta_10;
      double last_switch_psi_deg = psi0_deg;
      bool first_overshoot_captured = false;
      int switch_count = 0;
      for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, current_delta, 5.0, p, dt);
        res.time_s.push_back(i * dt);
        res.x_m.push_back(s.x); res.y_m.push_back(s.y);
        res.psi_deg.push_back(s.psi * kRadToDeg);
        double psi_change = wrap_180(s.psi * kRadToDeg - last_switch_psi_deg);
        if (std::abs(psi_change) >= 10.0) {
          current_delta = -current_delta;
          last_switch_psi_deg = s.psi * kRadToDeg;
          switch_count++;
          if (switch_count == 2 && !first_overshoot_captured) {
            double overshoot = std::abs(wrap_180(s.psi * kRadToDeg - psi0_deg)) - 10.0;
            res.first_overshoot_deg = std::max(0.0, overshoot);
            first_overshoot_captured = true;
          }
        }
        if (switch_count >= 4) break;
      }
      break;
    }
  }
  return res;
}

}  // namespace fcb_sim
