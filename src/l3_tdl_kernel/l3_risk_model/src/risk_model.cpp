#include "l3_risk_model/risk_model.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::risk {
namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
constexpr double kEpsilon = 1.0e-6;

struct SanitizedConfig {
  double superellipse_power{1.0};
  double warning_scale{1.0};
  double action_horizon_s{0.0};
  double emergency_horizon_s{0.0};
  double critical_horizon_s{0.0};
};

double clamp01(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double lower_bound(double value, double minimum) {
  return value >= minimum ? value : minimum;
}

double non_negative(double value) {
  return value > 0.0 ? value : 0.0;
}

SanitizedConfig sanitize_config(const DomainConfig & config) {
  return SanitizedConfig{
    lower_bound(config.superellipse_power, 1.0),
    lower_bound(config.warning_scale, 1.0),
    non_negative(config.action_horizon_s),
    non_negative(config.emergency_horizon_s),
    non_negative(config.critical_horizon_s)};
}

double normalize_degrees(double degrees) {
  while (degrees > 180.0) {
    degrees -= 360.0;
  }
  while (degrees < -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

double selected_longitudinal_axis(double x_body_m, const DomainAxes & axes) {
  return x_body_m >= 0.0 ? axes.forward_m : axes.astern_m;
}

double selected_lateral_axis(double y_body_m, const DomainAxes & axes) {
  return y_body_m >= 0.0 ? axes.starboard_m : axes.port_m;
}

double superellipse_norm(double x_body_m, double y_body_m, const DomainAxes & axes, double power) {
  const double a = std::max(selected_longitudinal_axis(x_body_m, axes), kEpsilon);
  const double b = std::max(selected_lateral_axis(y_body_m, axes), kEpsilon);
  const double p = lower_bound(power, 1.0);
  return std::pow(
    std::pow(std::abs(x_body_m / a), p) + std::pow(std::abs(y_body_m / b), p),
    1.0 / p);
}

double boundary_margin(
  double range_m,
  double norm,
  double x_body_m,
  double y_body_m,
  const DomainAxes & axes) {
  if (range_m <= kEpsilon && norm <= kEpsilon) {
    const double longitudinal_axis_m = std::max(selected_longitudinal_axis(x_body_m, axes), kEpsilon);
    const double lateral_axis_m = std::max(selected_lateral_axis(y_body_m, axes), kEpsilon);
    return -std::min(longitudinal_axis_m, lateral_axis_m);
  }
  const double boundary_range_m = range_m / std::max(norm, kEpsilon);
  return range_m - boundary_range_m;
}

double time_to_violation(double margin_m, double closing_speed_mps) {
  if (margin_m <= 0.0) {
    return 0.0;
  }
  if (closing_speed_mps > kEpsilon) {
    return margin_m / closing_speed_mps;
  }
  return 0.0;
}

double time_to_exit(double margin_m, double closing_speed_mps) {
  if (margin_m >= 0.0 || closing_speed_mps >= -kEpsilon) {
    return 0.0;
  }
  return -margin_m / -closing_speed_mps;
}

double colregs_score_component(ColregsDuty duty) {
  switch (duty) {
    case ColregsDuty::GiveWay:
    case ColregsDuty::BothGiveWay:
      return 1.0;
    case ColregsDuty::Rule17Action:
      return 0.6;
    case ColregsDuty::StandOnHold:
      return 0.3;
    case ColregsDuty::Free:
      return 0.0;
  }
  return 0.0;
}

}  // namespace

DomainAxes danger_axes(const OwnShipInput & own) {
  const double length_m = std::max(own.loa_m, 1.0);
  const double speed_mps = non_negative(own.sog_mps);
  return DomainAxes{
    std::max({8.0 * length_m, speed_mps * 60.0 + 2.0 * length_m, 300.0}),
    std::max(3.0 * length_m, 150.0),
    std::max({5.0 * length_m, speed_mps * 30.0 + length_m, 220.0}),
    std::max({4.0 * length_m, speed_mps * 25.0 + length_m, 185.0})};
}

DomainAxes warning_axes(const OwnShipInput & own, const DomainConfig & config) {
  const auto safe_config = sanitize_config(config);
  const auto danger = danger_axes(own);
  return DomainAxes{
    danger.forward_m * safe_config.warning_scale,
    danger.astern_m * safe_config.warning_scale,
    danger.starboard_m * safe_config.warning_scale,
    danger.port_m * safe_config.warning_scale};
}

RiskVector evaluate_target(
  const OwnShipInput & own,
  const TargetInput & target,
  ColregsDuty duty,
  const DomainConfig & config) {
  const auto safe_config = sanitize_config(config);
  const double dx_m = target.x_m - own.x_m;
  const double dy_m = target.y_m - own.y_m;
  const double cos_heading = std::cos(own.heading_rad);
  const double sin_heading = std::sin(own.heading_rad);
  const double x_body_m = cos_heading * dx_m + sin_heading * dy_m;
  const double y_body_m = -sin_heading * dx_m + cos_heading * dy_m;
  const double range_m = std::hypot(dx_m, dy_m);

  const double own_speed_mps = non_negative(own.sog_mps);
  const double target_speed_mps = non_negative(target.sog_mps);
  const double own_vx_mps = own_speed_mps * std::cos(own.heading_rad);
  const double own_vy_mps = own_speed_mps * std::sin(own.heading_rad);
  const double target_vx_mps = target_speed_mps * std::cos(target.cog_rad);
  const double target_vy_mps = target_speed_mps * std::sin(target.cog_rad);
  const double rel_vx_mps = target_vx_mps - own_vx_mps;
  const double rel_vy_mps = target_vy_mps - own_vy_mps;
  const double closing_speed_mps =
    range_m > kEpsilon ? -((rel_vx_mps * dx_m + rel_vy_mps * dy_m) / range_m) : 0.0;

  const auto danger = danger_axes(own);
  const auto warning = warning_axes(own, config);
  const double danger_norm =
    superellipse_norm(x_body_m, y_body_m, danger, safe_config.superellipse_power);
  const double warning_norm =
    superellipse_norm(x_body_m, y_body_m, warning, safe_config.superellipse_power);

  RiskVector risk;
  risk.target_id = target.id;
  risk.range_m = range_m;
  risk.relative_bearing_deg = normalize_degrees(std::atan2(y_body_m, x_body_m) * kRadToDeg);
  risk.closing_speed_mps = closing_speed_mps;
  risk.dcpa_m = target.cpa_m;
  risk.tcpa_s = target.tcpa_s;
  risk.warning_margin_m = boundary_margin(range_m, warning_norm, x_body_m, y_body_m, warning);
  risk.danger_margin_m = boundary_margin(range_m, danger_norm, x_body_m, y_body_m, danger);
  risk.warning_ddv = std::max(0.0, 1.0 - warning_norm);
  risk.danger_ddv = std::max(0.0, 1.0 - danger_norm);
  risk.tdv_warning_s = time_to_violation(risk.warning_margin_m, closing_speed_mps);
  risk.tdv_danger_s = time_to_violation(risk.danger_margin_m, closing_speed_mps);
  risk.tde_warning_s = time_to_exit(risk.warning_margin_m, closing_speed_mps);
  risk.tde_danger_s = time_to_exit(risk.danger_margin_m, closing_speed_mps);
  risk.colregs_duty = duty;

  if (risk.danger_ddv > 0.0 && target.tcpa_s <= safe_config.critical_horizon_s && target.tcpa_s >= 0.0) {
    risk.risk_phase = RiskPhase::Critical;
  } else if (risk.danger_ddv > 0.0 || risk.danger_margin_m < 0.0) {
    risk.risk_phase = RiskPhase::Danger;
  } else if (risk.warning_ddv > 0.0 || risk.warning_margin_m < 0.0) {
    risk.risk_phase = RiskPhase::Warning;
  } else if (
    target.tcpa_s >= 0.0 && target.tcpa_s <= safe_config.action_horizon_s &&
    risk.dcpa_m < warning.forward_m) {
    risk.risk_phase = RiskPhase::Monitor;
  } else {
    risk.risk_phase = RiskPhase::Clear;
  }

  const double domain_component = std::max(risk.warning_ddv * 0.7, risk.danger_ddv);
  const double urgency_component = target.tcpa_s >= 0.0 ? std::exp(-target.tcpa_s / 180.0) : 0.0;
  const double closing_component = clamp01(closing_speed_mps / 8.0);
  const double uncertainty_component = 1.0 - clamp01(target.confidence);
  risk.risk_score = clamp01(
    0.40 * domain_component + 0.25 * urgency_component + 0.15 * closing_component +
    0.15 * colregs_score_component(duty) + 0.05 * uncertainty_component);
  return risk;
}

const char * to_string(RiskPhase phase) noexcept {
  switch (phase) {
    case RiskPhase::Clear:
      return "Clear";
    case RiskPhase::Monitor:
      return "Monitor";
    case RiskPhase::Warning:
      return "Warning";
    case RiskPhase::Danger:
      return "Danger";
    case RiskPhase::Critical:
      return "Critical";
  }
  return "Unknown";
}

const char * to_string(ColregsDuty duty) noexcept {
  switch (duty) {
    case ColregsDuty::Free:
      return "Free";
    case ColregsDuty::StandOnHold:
      return "StandOnHold";
    case ColregsDuty::GiveWay:
      return "GiveWay";
    case ColregsDuty::BothGiveWay:
      return "BothGiveWay";
    case ColregsDuty::Rule17Action:
      return "Rule17Action";
  }
  return "Unknown";
}

}  // namespace mass_l3::risk
