// M5 Tactical Planner — L0 input validation pure functions (implementation).
//
// See include/m5_tactical_planner/common/l0_guards.hpp for the design rationale
// and per-function source-line anchors (commit fb84701b1, mid_mpc_node.cpp).
//
// Each function is a 1:1 behavior-preserving extraction from
// MidMpcNode::assemble_input_(). The spdlog::warn calls were NOT moved — the
// production caller (assemble_input_) keeps them so the log format is
// unchanged. These functions return the fallback value AND set the
// InputDegradation flag, but do not log. The unit tests can therefore assert
// behavior without capturing log output.
//
// PATH-D (MISRA C++:2023): pure functions; no heap, no I/O, no logging.

#include "m5_tactical_planner/common/l0_guards.hpp"

#include <cmath>

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5 {

// Source: mid_mpc_node.cpp:527-536.
double validate_own_heading(double heading_deg,
                            MidMpcInput::InputDegradation& deg) {
  const double raw_heading_rad = heading_deg * units::kRadPerDeg;
  if (!std::isfinite(raw_heading_rad)) {
    deg.own_psi_degraded = true;
    return 0.0;
  }
  return normalize_heading_signed(raw_heading_rad);
}

// Source: mid_mpc_node.cpp:538-551.
double validate_own_speed(double u_water, double sog_kn,
                          MidMpcInput::InputDegradation& deg) {
  if (u_water > 0.1 && std::isfinite(u_water)) {
    return u_water;
  }
  const double u_sog_mps = sog_kn * units::kMsPerKn;
  if (std::isfinite(u_sog_mps) && u_sog_mps >= 0.0) {
    return u_sog_mps;
  }
  deg.own_u_degraded = true;
  return 0.0;
}

// Source: mid_mpc_node.cpp:560-564.
bool validate_target_latlon(double lat, double lon) noexcept {
  return std::isfinite(lat) && std::isfinite(lon);
}

// Source: mid_mpc_node.cpp:573-581.
double validate_target_sog(double sog_kn,
                           MidMpcInput::InputDegradation& deg) {
  const double tgt_sog_mps = sog_kn * units::kMsPerKn;
  if (!std::isfinite(tgt_sog_mps) || tgt_sog_mps < 0.0) {
    deg.target_degraded = true;
    return 0.0;
  }
  return tgt_sog_mps;
}

// Source: mid_mpc_node.cpp:614-625.
double validate_box_reach(double box_reach_deg,
                          MidMpcInput::InputDegradation& deg) {
  if (!std::isfinite(box_reach_deg) || box_reach_deg < 0.0) {
    deg.reachability_degraded = true;
    return 0.0;
  }
  return box_reach_deg;
}

// Source: mid_mpc_node.cpp:628-637.
double validate_rot_step(double rot_step_deg,
                         MidMpcInput::InputDegradation& deg) {
  if (!std::isfinite(rot_step_deg) || rot_step_deg <= 0.0) {
    deg.reachability_degraded = true;
    return 0.0;
  }
  return rot_step_deg;
}

// Source: mid_mpc_node.cpp:638-647.
double validate_min_alt(double min_alt_rad,
                        MidMpcInput::InputDegradation& deg) {
  if (!std::isfinite(min_alt_rad) || min_alt_rad < 0.0) {
    deg.reachability_degraded = true;
    return 0.0;
  }
  return min_alt_rad;
}

// Source: mid_mpc_node.cpp:787-791.
double bump_cpa_safe_for_conflict(bool conflict_active) noexcept {
  return conflict_active ? kCpaSafeConflictBump_m : kCpaSafeFallback_m;
}

// Source: mid_mpc_node.cpp:769-779.
void check_box_reach_pref_dir_consistency(
    double box_reach_deg,
    bool conflict_active,
    ColregsPreferredDirection pref_dir,
    MidMpcInput::InputDegradation& deg) {
  if (box_reach_deg > 0.0
      && conflict_active
      && pref_dir != ColregsPreferredDirection::Starboard
      && pref_dir != ColregsPreferredDirection::Port) {
    deg.reachability_degraded = true;
  }
}

}  // namespace mass_l3::m5
