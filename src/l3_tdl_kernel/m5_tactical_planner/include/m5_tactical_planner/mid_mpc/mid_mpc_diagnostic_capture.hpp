#ifndef MASS_L3_M5_MID_MPC_DIAGNOSTIC_CAPTURE_HPP_
#define MASS_L3_M5_MID_MPC_DIAGNOSTIC_CAPTURE_HPP_

// Diagnostic-only, best-effort, solver-behavior-neutral MidMpcInput capture.
// Unset environment variable means no filesystem query/write and no solver-side
// state mutation.

#include <cstdlib>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc::diagnostic {
namespace detail {

inline std::string json_string(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const char raw_c : value) {
    const auto c = static_cast<unsigned char>(raw_c);
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20U) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned int>(c) << std::dec;
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  out << '"';
  return out.str();
}

inline void number(std::ostream& out, double value) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("non-finite double in MidMpcInput diagnostic capture");
  }
  out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

inline void trajectory_point(std::ostream& out, const TrajectoryPoint& p) {
  out << "{\"psi_rad\":"; number(out, p.psi_rad);
  out << ",\"r_rad_s\":"; number(out, p.r_rad_s);
  out << ",\"t_s\":"; number(out, p.t_s);
  out << ",\"u_mps\":"; number(out, p.u_mps);
  out << ",\"v_mps\":"; number(out, p.v_mps);
  out << ",\"x_m\":"; number(out, p.x_m);
  out << ",\"y_m\":"; number(out, p.y_m); out << '}';
}

inline void target(std::ostream& out, const TargetState& t) {
  out << "{\"classification\":" << static_cast<unsigned int>(t.classification);
  out << ",\"cog_rad\":"; number(out, t.cog_rad);
  out << ",\"confidence\":"; number(out, t.confidence);
  out << ",\"cpa_m\":"; number(out, t.cpa_m);
  out << ",\"cpa_sigma_m\":"; number(out, t.cpa_sigma_m);
  out << ",\"id\":" << t.id;
  out << ",\"intent_confidence\":"; number(out, t.intent_confidence);
  out << ",\"predicted_intent\":" << static_cast<unsigned int>(t.predicted_intent);
  out << ",\"sog_mps\":"; number(out, t.sog_mps);
  out << ",\"target_compliance\":"; number(out, t.target_compliance);
  out << ",\"tcpa_s\":"; number(out, t.tcpa_s);
  out << ",\"x_m\":"; number(out, t.x_m);
  out << ",\"y_m\":"; number(out, t.y_m); out << '}';
}

inline void target_array(std::ostream& out, const std::vector<TargetState>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) out << ',';
    target(out, values[i]);
  }
  out << ']';
}

inline void double_array(std::ostream& out, const std::vector<double>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) out << ',';
    number(out, values[i]);
  }
  out << ']';
}

inline void constraints(std::ostream& out, const ConstraintInputs& c) {
  out << "{\"applicable_rules\":[";
  for (std::size_t i = 0; i < c.applicable_rules.size(); ++i) {
    if (i != 0U) out << ',';
    out << static_cast<unsigned int>(c.applicable_rules[i]);
  }
  out << "],\"cpa_hard_m\":"; number(out, c.cpa_hard_m);
  out << ",\"cpa_safe_m\":"; number(out, c.cpa_safe_m);
  out << ",\"earliest_min_alt_k\":"; number(out, c.earliest_min_alt_k);
  out << ",\"heading_box_reachable_from_psi0_deg\":"; number(out, c.heading_box_reachable_from_psi0_deg);
  out << ",\"heading_max_rad\":"; number(out, c.heading_max_rad);
  out << ",\"heading_min_rad\":"; number(out, c.heading_min_rad);
  out << ",\"min_alt_required_rad\":"; number(out, c.min_alt_required_rad);
  out << ",\"own_ship_psi_rad\":"; number(out, c.own_ship_psi_rad);
  out << ",\"rot_step_deg\":"; number(out, c.rot_step_deg);
  out << ",\"speed_max_mps\":"; number(out, c.speed_max_mps);
  out << ",\"speed_min_mps\":"; number(out, c.speed_min_mps);
  out << ",\"targets\":"; target_array(out, c.targets);
  out << ",\"terminal_l_max_feasible_m\":"; number(out, c.terminal_l_max_feasible_m);
  out << ",\"terminal_l_min_feasible_m\":"; number(out, c.terminal_l_min_feasible_m);
  out << ",\"zone_constraints\":[";
  for (std::size_t i = 0; i < c.zone_constraints.size(); ++i) {
    if (i != 0U) out << ',';
    const auto& z = c.zone_constraints[i];
    out << "{\"must_stay_inside\":" << (z.must_stay_inside ? "true" : "false")
        << ",\"name\":" << json_string(z.name) << ",\"polygon\":[";
    for (std::size_t j = 0; j < z.polygon.size(); ++j) {
      if (j != 0U) out << ',';
      out << '['; number(out, z.polygon[j][0]); out << ','; number(out, z.polygon[j][1]); out << ']';
    }
    out << "]}";
  }
  out << "]}";
}

inline std::string serialize(const MidMpcInput& input, const std::string& boundary) {
  std::ostringstream out;
  out << "{\"boundary\":" << json_string(boundary) << ",\"mid_mpc_input\":{";
  out << "\"colregs_conflict_active\":" << (input.colregs_conflict_active ? "true" : "false");
  out << ",\"colregs_encounter_state\":" << static_cast<unsigned int>(input.colregs_encounter_state);
  out << ",\"colregs_min_alteration_rad\":"; number(out, input.colregs_min_alteration_rad);
  out << ",\"colregs_phase\":" << json_string(input.colregs_phase);
  out << ",\"colregs_preferred_direction\":" << static_cast<unsigned int>(input.colregs_preferred_direction);
  out << ",\"colregs_primary_role\":" << static_cast<unsigned int>(input.colregs_primary_role);
  out << ",\"constraints\":"; constraints(out, input.constraints);
  out << ",\"decel_max_mps2\":"; number(out, input.decel_max_mps2);
  out << ",\"has_m6_encounter_state\":" << (input.has_m6_encounter_state ? "true" : "false");
  out << ",\"lateral_scale_m\":"; number(out, input.lateral_scale_m);
  out << ",\"own_lat_deg\":"; number(out, input.own_lat_deg);
  out << ",\"own_lon_deg\":"; number(out, input.own_lon_deg);
  out << ",\"own_ship\":"; trajectory_point(out, input.own_ship);
  out << ",\"planned_route_bearing_rad\":"; number(out, input.planned_route_bearing_rad);
  out << ",\"planned_speed_mps\":"; number(out, input.planned_speed_mps);
  out << ",\"prefix_active_k\":" << input.prefix_active_k;
  out << ",\"prefix_psi_rad\":"; double_array(out, input.prefix_psi_rad);
  out << ",\"prefix_u_mps\":"; double_array(out, input.prefix_u_mps);
  out << ",\"rot_max_rad_s\":"; number(out, input.rot_max_rad_s);
  out << ",\"route_corridor_limit_m\":"; number(out, input.route_corridor_limit_m);
  out << ",\"route_frame_active_leg_bearing_rad\":"; number(out, input.route_frame_active_leg_bearing_rad);
  out << ",\"route_frame_normal_x\":"; number(out, input.route_frame_normal_x);
  out << ",\"route_frame_normal_y\":"; number(out, input.route_frame_normal_y);
  out << ",\"route_frame_origin_x_m\":"; number(out, input.route_frame_origin_x_m);
  out << ",\"route_frame_origin_y_m\":"; number(out, input.route_frame_origin_y_m);
  out << ",\"route_weight\":"; number(out, input.route_weight);
  out << ",\"route_xte_m\":"; number(out, input.route_xte_m);
  out << ",\"speed_gap_infeasible\":" << (input.speed_gap_infeasible ? "true" : "false");
  out << ",\"stamp_ns\":" << input.stamp_ns;
  out << ",\"tail_gate_targets\":"; target_array(out, input.tail_gate_targets);
  out << ",\"target_risks\":[";
  for (std::size_t i = 0; i < input.target_risks.size(); ++i) {
    if (i != 0U) out << ',';
    const auto& r = input.target_risks[i];
    out << "{\"closing_speed_mps\":"; number(out, r.closing_speed_mps);
    out << ",\"danger_margin_m\":"; number(out, r.danger_margin_m);
    out << ",\"primary\":" << (r.primary ? "true" : "false");
    out << ",\"risk_score\":"; number(out, r.risk_score);
    out << ",\"target_id\":" << json_string(r.target_id);
    out << ",\"tcpa_s\":"; number(out, r.tcpa_s);
    out << ",\"warning_margin_m\":"; number(out, r.warning_margin_m); out << '}';
  }
  out << "],\"targets\":"; target_array(out, input.targets);
  out << "},\"schema\":\"m5_mid_mpc_input_diag_v1\"}\n";
  return out.str();
}

inline std::string hex_digest(const std::array<std::uint8_t, 32>& digest) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const std::uint8_t byte : digest) out << std::setw(2) << static_cast<unsigned int>(byte);
  return out.str();
}

}  // namespace detail

inline bool capture_mid_mpc_input_if_enabled(const MidMpcInput& input,
                                             const std::string& boundary,
                                             std::string* error) noexcept {
  const char* directory = std::getenv("M5_ACADOS_DIAG_CAPTURE_DIR");
  if (directory == nullptr || directory[0] == '\0') return false;
  try {
    const std::string json = detail::serialize(input, boundary);
    const auto digest = common::sha256(json);
    const std::string digest_hex = detail::hex_digest(digest);
    // A stamp can repeat at multiple solve attempts.  Include content identity so
    // a later capture cannot truncate a distinct input from the same boundary.
    const std::string stem = "mid_mpc_input_" + boundary + "_" +
                             std::to_string(input.stamp_ns) + "_" + digest_hex.substr(0, 16);
    const std::filesystem::path dir(directory);
    std::filesystem::create_directories(dir);
    const std::filesystem::path json_path = dir / (stem + ".json");
    const std::filesystem::path hash_path = dir / (stem + ".sha256");
    std::ofstream json_file(json_path, std::ios::binary | std::ios::trunc);
    if (!json_file) throw std::runtime_error("open failed: " + json_path.string());
    json_file << json;
    json_file.close();
    std::ofstream hash_file(hash_path, std::ios::binary | std::ios::trunc);
    if (!hash_file) throw std::runtime_error("open failed: " + hash_path.string());
    hash_file << digest_hex << "  " << json_path.filename().string() << '\n';
    return true;
  } catch (const std::exception& exc) {
    if (error != nullptr) *error = exc.what();
    return false;
  }
}

}  // namespace mass_l3::m5::mid_mpc::diagnostic

#endif  // MASS_L3_M5_MID_MPC_DIAGNOSTIC_CAPTURE_HPP_
