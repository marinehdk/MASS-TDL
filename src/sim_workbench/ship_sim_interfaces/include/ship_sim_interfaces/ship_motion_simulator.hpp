// SPDX-License-Identifier: Proprietary
// Abstract base class for pluginlib-loadable ship motion simulators.
// MV-4 fix: Capability Manifest pattern — zero vessel-type constants in node.
// v3.1 NEW: FMI 2.0 export contract for D1.3c dds-fmu mapping (spec §v3.1.4).
#pragma once
#include <string>
#include <vector>
#include "ship_sim_interfaces/ship_state.hpp"
namespace ship_sim {

struct FmuVariableSpec {
  std::string name;
  std::string causality;
  std::string variability;
  std::string type;
  std::string unit;
  double      start;
  std::string description;
};

struct FmuInterfaceSpec {
  std::string fmi_version;
  std::string model_name;
  std::string model_identifier;
  double      default_step_size;
  std::vector<FmuVariableSpec> variables;
};

class ShipMotionSimulator {
 public:
  virtual ~ShipMotionSimulator() = default;
  virtual void load_params(const std::string& yaml_path) = 0;
  virtual ShipState step(const ShipState& state, double delta_rad,
                         double n_rps, double dt_s) = 0;
  virtual std::string vessel_class() const = 0;
  virtual std::string hull_class()   const = 0;
  // v3.1 NEW: FMI 2.0 export contract (spec §v3.1.4).
  // Phase 1 contract-only; D1.3c implements pythonfmu/mlfmu wrap via pybind11
  // bindings into this C++ class. This method enumerates FMU variables for
  // modelDescription.xml + dds-fmu mapping XML generation.
  virtual FmuInterfaceSpec export_fmu_interface() const = 0;
};

}  // namespace ship_sim
