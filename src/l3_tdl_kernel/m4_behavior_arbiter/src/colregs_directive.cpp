#include "m4_behavior_arbiter/colregs_directive.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace mass_l3::m4 {
namespace {

constexpr double kRadToDeg = 180.0 / 3.141592653589793238462643383279502884;

}  // namespace

double wrap_heading_deg(double heading_deg) {
  double wrapped = std::fmod(heading_deg, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped;
}

ColregsDirection parse_colregs_direction(const std::string& direction) {
  if (direction == "STARBOARD") {
    return ColregsDirection::Starboard;
  }
  if (direction == "PORT") {
    return ColregsDirection::Port;
  }
  if (direction == "REDUCE_SPEED") {
    return ColregsDirection::ReduceSpeed;
  }
  return ColregsDirection::Hold;
}

ColregsDirective extract_colregs_directive(const l3_msgs::msg::COLREGsConstraint& msg) {
  ColregsDirective out;
  out.conflict_active = msg.conflict_detected;
  if (!out.conflict_active) {
    return out;
  }

  out.direction = parse_colregs_direction(msg.primary_preferred_direction);
  for (const auto& c : msg.constraints) {
    if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
      out.min_alteration_deg = std::max(out.min_alteration_deg, c.numeric_value);
    }
  }
  return out;
}

double required_deviation_deg(
    const ColregsDirective& directive,
    double nearest_target_range_m,
    double cpa_safe_m,
    double boldness_factor,
    double max_deviation_deg) {
  if (!directive.conflict_active ||
      (directive.direction != ColregsDirection::Starboard &&
       directive.direction != ColregsDirection::Port)) {
    return 0.0;
  }

  double required = directive.min_alteration_deg;
  if (nearest_target_range_m > cpa_safe_m) {
    const double raw_deg = std::asin(cpa_safe_m / nearest_target_range_m) * kRadToDeg;
    required = std::max(required, raw_deg * boldness_factor);
  } else if (nearest_target_range_m > 0.0) {
    required = max_deviation_deg;
  }
  return std::min(max_deviation_deg, required);
}

double signed_deviation_deg(
    const ColregsDirective& directive,
    double required_deviation_deg) {
  if (directive.direction == ColregsDirection::Starboard) {
    return required_deviation_deg;
  }
  if (directive.direction == ColregsDirection::Port) {
    return -required_deviation_deg;
  }
  return 0.0;
}

std::optional<HeadingWindow> directive_heading_window(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg,
    double half_width_deg) {
  const double signed_dev = signed_deviation_deg(directive, required_deviation_deg);
  if (signed_dev == 0.0) {
    return std::nullopt;
  }

  const double centre = wrap_heading_deg(base_heading_deg + signed_dev);
  return HeadingWindow{
      wrap_heading_deg(centre - half_width_deg),
      wrap_heading_deg(centre + half_width_deg)};
}

std::vector<std::pair<double, double>> directive_allowed_ranges(
    double base_heading_deg,
    const ColregsDirective& directive,
    double required_deviation_deg) {
  if (directive.direction == ColregsDirection::Starboard && required_deviation_deg > 0.0) {
    return {{wrap_heading_deg(base_heading_deg + required_deviation_deg),
             wrap_heading_deg(base_heading_deg + 180.0)}};
  }
  if (directive.direction == ColregsDirection::Port && required_deviation_deg > 0.0) {
    return {{wrap_heading_deg(base_heading_deg - 180.0),
             wrap_heading_deg(base_heading_deg - required_deviation_deg)}};
  }
  return {};
}

}  // namespace mass_l3::m4
