#include "m4_behavior_arbiter/colregs_directive.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace mass_l3::m4 {
namespace {

constexpr double kRadToDeg = 180.0 / 3.141592653589793238462643383279502884;
constexpr double kHeadingUpperBoundDeg = 360.0;
constexpr std::uint8_t kRoleStandOn = 0U;
constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr std::uint8_t kRoleBothGiveWay = 2U;
constexpr double kStandOnActionMaxDeviationDeg = 60.0;
constexpr double kRiskEpsilon = 1.0e-6;

std::vector<std::pair<double, double>> split_linear_range(double start_deg, double end_deg) {
  const double first = wrap_heading_deg(start_deg);
  const double second = wrap_heading_deg(end_deg);
  if (first <= second) {
    return {{first, second}};
  }
  return {{first, kHeadingUpperBoundDeg}, {0.0, second}};
}

bool is_stand_on_action(const ColregsDirective& directive) {
  return directive.primary_role == kRoleStandOn &&
      (directive.phase == "INDEPENDENT_ACTION" || directive.phase == "CRITICAL_ACTION");
}

bool is_stand_on_critical_action(const ColregsDirective& directive) {
  return directive.primary_role == kRoleStandOn && directive.phase == "CRITICAL_ACTION";
}

bool is_danger_or_critical(const mass_l3::risk::RiskVector& risk) {
  return risk.risk_phase == mass_l3::risk::RiskPhase::Danger ||
      risk.risk_phase == mass_l3::risk::RiskPhase::Critical ||
      risk.danger_ddv > kRiskEpsilon ||
      risk.danger_margin_m < 0.0;
}

bool speed_reduction_improves_margin(
    const mass_l3::risk::RiskVector& current,
    const mass_l3::risk::RiskVector& reduced_speed) {
  constexpr double kMinimumMarginGainM = 10.0;
  return reduced_speed.warning_margin_m >= current.warning_margin_m + kMinimumMarginGainM &&
      reduced_speed.danger_margin_m >= current.danger_margin_m - kRiskEpsilon;
}

bool speed_reduction_arrests_closing(
    const mass_l3::risk::RiskVector& current,
    const mass_l3::risk::RiskVector& reduced_speed) {
  constexpr double kMinimumClosingDropMps = 1.0;
  constexpr double kSafeFollowingClosingMps = 0.5;
  return current.closing_speed_mps > kSafeFollowingClosingMps &&
      (current.closing_speed_mps - reduced_speed.closing_speed_mps) >= kMinimumClosingDropMps &&
      reduced_speed.closing_speed_mps <= kSafeFollowingClosingMps &&
      reduced_speed.danger_margin_m >= current.danger_margin_m - kRiskEpsilon;
}

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

mass_l3::risk::ColregsDuty map_role_to_duty(
    std::uint8_t primary_role,
    bool conflict_active,
    bool /*rule15_active*/,
    const std::string& phase) {
  if (!conflict_active) {
    return mass_l3::risk::ColregsDuty::Free;
  }
  if (primary_role == kRoleGiveWay) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (primary_role == kRoleBothGiveWay) {
    return mass_l3::risk::ColregsDuty::BothGiveWay;
  }
  if (primary_role == kRoleStandOn) {
    if (phase == "INDEPENDENT_ACTION" || phase == "CRITICAL_ACTION") {
      return mass_l3::risk::ColregsDuty::Rule17Action;
    }
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  return mass_l3::risk::ColregsDuty::Free;
}

ColregsDirective extract_colregs_directive(const l3_msgs::msg::COLREGsConstraint& msg) {
  ColregsDirective out;
  out.conflict_active = msg.conflict_detected;
  out.primary_role = msg.primary_role;
  out.phase = msg.phase;
  out.rule15_active = std::any_of(
      msg.active_rules.begin(), msg.active_rules.end(),
      [](const auto& rule) { return rule.rule_id == 15U; });
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

void apply_primary_risk_guidance(
    ColregsDirective& directive,
    const mass_l3::risk::RiskVector& primary_risk,
    const mass_l3::risk::RiskVector& reduced_speed_risk) {
  directive.primary_threat_id = primary_risk.target_id;
  directive.primary_risk_score = primary_risk.risk_score;
  directive.primary_warning_margin_m = primary_risk.warning_margin_m;
  directive.primary_danger_margin_m = primary_risk.danger_margin_m;
  directive.primary_risk_phase = mass_l3::risk::to_string(primary_risk.risk_phase);
  directive.speed_reduction_preferred = false;

  if (!directive.conflict_active ||
      (directive.direction != ColregsDirection::Starboard &&
       directive.direction != ColregsDirection::Port)) {
    return;
  }

  const auto duty = map_role_to_duty(
      directive.primary_role,
      directive.conflict_active,
      directive.rule15_active,
      directive.phase);
  const bool can_reduce_speed =
      duty == mass_l3::risk::ColregsDuty::GiveWay ||
      duty == mass_l3::risk::ColregsDuty::BothGiveWay;
  const bool ample_tcpa = primary_risk.tcpa_s > 180.0;
  const bool outside_danger = !is_danger_or_critical(primary_risk);

  if (can_reduce_speed && ample_tcpa && outside_danger &&
      (speed_reduction_improves_margin(primary_risk, reduced_speed_risk) ||
       speed_reduction_arrests_closing(primary_risk, reduced_speed_risk))) {
    directive.direction = ColregsDirection::ReduceSpeed;
    directive.speed_reduction_preferred = true;
  }
}

bool dynamic_risk_requires_max_deviation(const ColregsDirective& directive) {
  if (!directive.conflict_active || !is_stand_on_critical_action(directive)) {
    return false;
  }
  return directive.primary_risk_phase == "Danger" ||
      directive.primary_risk_phase == "Critical" ||
      directive.primary_danger_margin_m < -kRiskEpsilon;
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

  if (dynamic_risk_requires_max_deviation(directive)) {
    return max_deviation_deg;
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

double effective_colregs_max_deviation_deg(
    const ColregsDirective& directive,
    bool has_quartering_target,
    double bow_max_deviation_deg,
    double quartering_max_deviation_deg) {
  if (directive.direction != ColregsDirection::Starboard &&
      directive.direction != ColregsDirection::Port) {
    return 0.0;
  }
  const double base_cap =
      has_quartering_target ? quartering_max_deviation_deg : bow_max_deviation_deg;
  if (is_stand_on_critical_action(directive)) {
    return std::max(base_cap, directive.min_alteration_deg);
  }
  if (is_stand_on_action(directive)) {
    return std::max(kStandOnActionMaxDeviationDeg, directive.min_alteration_deg);
  }
  return std::max(base_cap, directive.min_alteration_deg);
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
    return split_linear_range(
        base_heading_deg + required_deviation_deg,
        base_heading_deg + 180.0);
  }
  if (directive.direction == ColregsDirection::Port && required_deviation_deg > 0.0) {
    return split_linear_range(
        base_heading_deg - 180.0,
        base_heading_deg - required_deviation_deg);
  }
  return {};
}

}  // namespace mass_l3::m4
