// SPDX-License-Identifier: Proprietary
// FCBSimulator: pluginlib implementation of ShipMotionSimulator.
#pragma once
#include <string>
#include "ship_sim_interfaces/ship_motion_simulator.hpp"
#include "fcb_simulator/types.hpp"

namespace fcb_sim {

class FCBSimulator : public ship_sim::ShipMotionSimulator {
public:
  FCBSimulator() = default;
  void load_params(const std::string& yaml_path) override;
  ship_sim::ShipState step(const ship_sim::ShipState& state,
                           double delta_rad, double n_rps, double dt_s) override;
  std::string vessel_class() const override { return "FCB"; }
  std::string hull_class()   const override { return "SEMI_PLANING"; }
  ship_sim::FmuInterfaceSpec export_fmu_interface() const override;
private:
  MmgParams params_;
};

}  // namespace fcb_sim
