// SPDX-License-Identifier: Proprietary
// Abstract base class for pluginlib-loadable ship motion simulators.
// MV-4 fix: Capability Manifest pattern — zero vessel-type constants in node.
#pragma once
#include <string>
#include "ship_sim_interfaces/ship_state.hpp"
namespace ship_sim {
class ShipMotionSimulator {
public:
  virtual ~ShipMotionSimulator() = default;
  virtual void load_params(const std::string& yaml_path) = 0;
  virtual ShipState step(const ShipState& state, double delta_rad, double n_rps, double dt_s) = 0;
  virtual std::string vessel_class() const = 0;  // e.g. "FCB"
  virtual std::string hull_class()   const = 0;  // e.g. "SEMI_PLANING"
};
}  // namespace ship_sim
