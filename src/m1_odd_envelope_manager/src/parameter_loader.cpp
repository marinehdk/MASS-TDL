/// Implementation of YAML parameter loading for M1 ODD/Envelope Manager.
///
/// PATH-S: noexcept, no exceptions (yaml-cpp throw on parse error terminates
///         the process -- acceptable for config errors in safety-critical).
///         No dynamic allocation beyond yaml-cpp internal parse structures.

#include "m1_odd_envelope_manager/parameter_loader.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/parse.h>

#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

// ---------------------------------------------------------------------------
// Default values for [TBD-HAZID] parameters (FCB baselines pending HAZID).
// ---------------------------------------------------------------------------
namespace {

/// Set all fields to conservative defaults.
ParameterSet defaults() noexcept {
  ParameterSet p{};
  p.in_to_edge = 0.8;
  p.edge_to_out = 0.5;
  p.stale_degradation_factor = 0.5;
  p.w_e = 0.4;
  p.w_t = 0.3;
  p.w_h = 0.3;
  p.visibility_full_nm = 2.0;
  p.visibility_degraded_nm = 1.0;
  p.visibility_marginal_nm = 0.5;
  p.sea_state_full_hs = 2.5;
  p.sea_state_degraded_hs = 3.0;
  p.sea_state_marginal_hs = 4.0;
  p.comm_delay_ok_s = 2.0;
  p.t_score_comm_ok = 0.6;
  p.t_score_comm_bad = 0.3;
  p.h_score_available = 1.0;
  p.h_score_unavailable = 0.5;
  p.tmr_baseline_s = 60.0;
  p.tcpa_coefficient = 0.6;
  p.tmr_min_s = 30.0;
  p.tmr_max_s = 600.0;
  p.tdl_min_s = 0.0;
  p.tdl_max_s = 600.0;
  p.max_anchor_depth_m = 50.0;
  p.max_heave_to_sea_state_hs = 4.0;
  p.max_heave_to_wind_kn = 40.0;
  return p;
}

/// Try to read a double node with a fallback default.
// NOLINTNEXTLINE(bugprone-exception-escape)
inline double read_double(const YAML::Node& node, const char* key,
                          double fallback) noexcept {
  const YAML::Node kChild = node[key];
  if (!kChild || kChild.IsNull() || !kChild.IsScalar()) {
    return fallback;
  }
  return kChild.as<double>(fallback);
}

}  // anonymous namespace

// ===========================================================================
// Public API
// ===========================================================================

// NOLINTNEXTLINE(bugprone-exception-escape,readability-function-cognitive-complexity,readability-function-size)
ParameterSet load_parameters(const std::string& yaml_path) noexcept {
  ParameterSet p = defaults();

  // Attempt to load YAML. On failure (file not found, malformed YAML) the
  // process terminates -- acceptable for non-recoverable configuration errors.
  // With -fno-exceptions, yaml-cpp's internal throw calls std::terminate.
  YAML::Node config = YAML::LoadFile(yaml_path);

  // --- state_machine section ---
  const YAML::Node kSm = config["state_machine"];
  if (kSm && !kSm.IsNull()) {
    p.in_to_edge = read_double(kSm, "in_to_edge", p.in_to_edge);
    p.edge_to_out = read_double(kSm, "edge_to_out", p.edge_to_out);
    p.stale_degradation_factor =
        read_double(kSm, "stale_degradation_factor", p.stale_degradation_factor);
  }

  // --- conformance_score section ---
  const YAML::Node kCs = config["conformance_score"];
  if (kCs && !kCs.IsNull()) {
    const YAML::Node kW = kCs["weights"];
    if (kW && !kW.IsNull()) {
      p.w_e = read_double(kW, "w_e", p.w_e);
      p.w_t = read_double(kW, "w_t", p.w_t);
      p.w_h = read_double(kW, "w_h", p.w_h);
    }
    const YAML::Node kEs = kCs["e_score"];
    if (kEs && !kEs.IsNull()) {
      p.visibility_full_nm =
          read_double(kEs, "visibility_full_nm", p.visibility_full_nm);
      p.visibility_degraded_nm =
          read_double(kEs, "visibility_degraded_nm", p.visibility_degraded_nm);
      p.visibility_marginal_nm =
          read_double(kEs, "visibility_marginal_nm", p.visibility_marginal_nm);
      p.sea_state_full_hs =
          read_double(kEs, "sea_state_full_hs", p.sea_state_full_hs);
      p.sea_state_degraded_hs =
          read_double(kEs, "sea_state_degraded_hs", p.sea_state_degraded_hs);
      p.sea_state_marginal_hs =
          read_double(kEs, "sea_state_marginal_hs", p.sea_state_marginal_hs);
    }
  }

  // --- t_score section (all [TBD-HAZID]) ---
  const YAML::Node kTs = config["t_score"];
  if (kTs && !kTs.IsNull()) {
    p.comm_delay_ok_s = read_double(kTs, "comm_delay_ok_s", p.comm_delay_ok_s);
    p.t_score_comm_ok = read_double(kTs, "comm_ok", p.t_score_comm_ok);
    p.t_score_comm_bad = read_double(kTs, "comm_bad", p.t_score_comm_bad);
  }

  // --- h_score section (all [TBD-HAZID]) ---
  const YAML::Node kHs = config["h_score"];
  if (kHs && !kHs.IsNull()) {
    p.h_score_available = read_double(kHs, "available", p.h_score_available);
    p.h_score_unavailable = read_double(kHs, "unavailable", p.h_score_unavailable);
  }

  // --- tmr_tdl section ---
  const YAML::Node kTt = config["tmr_tdl"];
  if (kTt && !kTt.IsNull()) {
    p.tmr_baseline_s = read_double(kTt, "tmr_baseline_s", p.tmr_baseline_s);
    p.tcpa_coefficient = read_double(kTt, "tcpa_coefficient", p.tcpa_coefficient);
    p.tmr_min_s = read_double(kTt, "tmr_min_s", p.tmr_min_s);
    p.tmr_max_s = read_double(kTt, "tmr_max_s", p.tmr_max_s);
    p.tdl_min_s = read_double(kTt, "tdl_min_s", p.tdl_min_s);
    p.tdl_max_s = read_double(kTt, "tdl_max_s", p.tdl_max_s);
  }

  // --- mrc section ---
  const YAML::Node kMr = config["mrc"];
  if (kMr && !kMr.IsNull()) {
    p.max_anchor_depth_m =
        read_double(kMr, "max_anchor_depth_m", p.max_anchor_depth_m);
    p.max_heave_to_sea_state_hs =
        read_double(kMr, "max_heave_to_sea_state_hs", p.max_heave_to_sea_state_hs);
    p.max_heave_to_wind_kn =
        read_double(kMr, "max_heave_to_wind_kn", p.max_heave_to_wind_kn);
  }

  return p;
}

}  // namespace mass_l3::m1
