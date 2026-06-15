#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "l3_risk_model/risk_model.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"

namespace mass_l3::m4 {

enum class ColregsDirection : std::uint8_t {
  Hold = 0u,
  Starboard = 1u,
  Port = 2u,
  ReduceSpeed = 3u,
};

struct ColregsDirective {
  bool conflict_active{false};
  ColregsDirection direction{ColregsDirection::Hold};
  double min_alteration_deg{0.0};
  std::uint8_t primary_role{3U};
  std::string phase;
  bool rule15_active{false};
  std::string primary_threat_id;
  double primary_risk_score{0.0};
  double primary_warning_margin_m{0.0};
  double primary_danger_margin_m{0.0};
  std::string primary_risk_phase;
  bool speed_reduction_preferred{false};
};

struct HeadingWindow {
  double heading_min_deg{0.0};
  double heading_max_deg{0.0};
};

[[nodiscard]] double wrap_heading_deg(double heading_deg);
[[nodiscard]] ColregsDirection parse_colregs_direction(const std::string& direction);
[[nodiscard]] mass_l3::risk::ColregsDuty map_role_to_duty(
    std::uint8_t primary_role,
    bool conflict_active,
    bool rule15_active,
    const std::string& phase);
[[nodiscard]] ColregsDirective extract_colregs_directive(
    const l3_msgs::msg::COLREGsConstraint& msg);
void apply_primary_risk_guidance(
    ColregsDirective& directive,
    const mass_l3::risk::RiskVector& primary_risk,
    const mass_l3::risk::RiskVector& reduced_speed_risk);
[[nodiscard]] bool dynamic_risk_requires_max_deviation(const ColregsDirective& directive);
[[nodiscard]] double required_deviation_deg(
    const ColregsDirective& directive,
    double nearest_target_range_m,
    double cpa_safe_m = 500.0,
    double boldness_factor = 2.5,
    double max_deviation_deg = 120.0);
[[nodiscard]] double effective_colregs_max_deviation_deg(
    const ColregsDirective& directive,
    bool has_quartering_target,
    double bow_max_deviation_deg = 75.0,
    double quartering_max_deviation_deg = 150.0);
[[nodiscard]] std::optional<HeadingWindow> directive_heading_window(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg,
    double half_width_deg = 15.0);
[[nodiscard]] std::vector<std::pair<double, double>> directive_allowed_ranges(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg);
[[nodiscard]] double signed_deviation_deg(
    const ColregsDirective& directive,
    double required_deviation_deg);

}  // namespace mass_l3::m4
