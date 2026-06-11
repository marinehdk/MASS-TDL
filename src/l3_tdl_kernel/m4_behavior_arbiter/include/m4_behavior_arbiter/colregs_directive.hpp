#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
};

struct HeadingWindow {
  double heading_min_deg{0.0};
  double heading_max_deg{0.0};
};

[[nodiscard]] double wrap_heading_deg(double heading_deg);
[[nodiscard]] ColregsDirection parse_colregs_direction(const std::string& direction);
[[nodiscard]] ColregsDirective extract_colregs_directive(
    const l3_msgs::msg::COLREGsConstraint& msg);
[[nodiscard]] double required_deviation_deg(
    const ColregsDirective& directive,
    double nearest_target_range_m,
    double cpa_safe_m = 500.0,
    double boldness_factor = 2.5,
    double max_deviation_deg = 120.0);
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
