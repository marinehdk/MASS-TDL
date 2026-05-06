#pragma once

#include <string>

#include <yaml-cpp/yaml.h>

#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// Load and validate all M1 parameters from YAML file.
/// All [TBD-HAZID] parameters are loaded here -- no hardcoded thresholds.
/// Returns populated ParameterSet with YAML-sourced values merged over
/// conservative defaults. On YAML file errors (file missing, parse failure)
/// the process terminates -- acceptable for non-recoverable config errors
/// in a safety-critical system.
ParameterSet load_parameters(const std::string& yaml_path) noexcept;

}  // namespace mass_l3::m1
