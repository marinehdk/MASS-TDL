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
constexpr double kTmrResponseHorizonS = 60.0;

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
  out.rule13_active = std::any_of(
      msg.active_rules.begin(), msg.active_rules.end(),
      [](const auto& rule) { return rule.rule_id == 13U; });
  out.rule15_active = std::any_of(
      msg.active_rules.begin(), msg.active_rules.end(),
      [](const auto& rule) { return rule.rule_id == 15U; });
  out.rule14_active = std::any_of(
      msg.active_rules.begin(), msg.active_rules.end(),
      [](const auto& rule) { return rule.rule_id == 14U; });
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
  directive.primary_closing_speed_mps = primary_risk.closing_speed_mps;
  directive.primary_tdv_warning_s = primary_risk.tdv_warning_s;
  directive.primary_risk_phase = mass_l3::risk::to_string(primary_risk.risk_phase);
  directive.speed_reduction_preferred = false;

  if (!directive.conflict_active ||
      (directive.direction != ColregsDirection::Starboard &&
       directive.direction != ColregsDirection::Port)) {
    return;
  }

  // D-5: Rule 14 head-on (BOTH_GIVE_WAY) forbids replacing the turn direction
  // with REDUCE_SPEED. COLREG Rule 14(a) requires both vessels to alter to
  // starboard so each passes on the port side of the other; speed reduction
  // alone cannot achieve port-to-port passing geometry. Speed reduction may
  // still apply downstream as an auxiliary speed_max constraint (the
  // speed_reduction_preferred flag is not set here, but the risk phase stays
  // visible to the planner). Rule 13 overtaking keeps the M6 starboard
  // alteration geometry; non-overtaking give-way may still prefer speed
  // reduction per GiveWayOvertakingCanPreferSpeedReduction.
  if (directive.rule14_active && directive.primary_role == kRoleBothGiveWay) {
    return;
  }

  const auto duty = map_role_to_duty(
      directive.primary_role,
      directive.conflict_active,
      directive.rule15_active,
      directive.phase);
  const bool can_reduce_speed =
      (!directive.rule13_active && !directive.rule15_active &&
       duty == mass_l3::risk::ColregsDuty::GiveWay) ||
      duty == mass_l3::risk::ColregsDuty::BothGiveWay;
  const bool ample_tcpa = primary_risk.tcpa_s > 180.0;
  const bool outside_danger = !is_danger_or_critical(primary_risk);
  const bool active_risk = primary_risk.risk_phase != mass_l3::risk::RiskPhase::Clear;

  if (can_reduce_speed && active_risk && ample_tcpa && outside_danger &&
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

bool dynamic_risk_requires_speed_cap(const ColregsDirective& directive) {
  if (!directive.conflict_active) {
    return false;
  }
  const bool rule13_directional_overtake =
      directive.rule13_active &&
      directive.primary_role == kRoleGiveWay &&
      (directive.direction == ColregsDirection::Starboard ||
       directive.direction == ColregsDirection::Port);
  if (rule13_directional_overtake) {
    return directive.direction == ColregsDirection::ReduceSpeed ||
        directive.speed_reduction_preferred ||
        directive.primary_risk_phase == "Danger" ||
        directive.primary_risk_phase == "Critical" ||
        directive.primary_danger_margin_m < -kRiskEpsilon;
  }
  const bool warning_entry_within_tmr =
      directive.primary_risk_phase == "Monitor" &&
      directive.primary_closing_speed_mps > kRiskEpsilon &&
      std::isfinite(directive.primary_tdv_warning_s) &&
      directive.primary_tdv_warning_s <= kTmrResponseHorizonS;
  return directive.direction == ColregsDirection::ReduceSpeed ||
      directive.speed_reduction_preferred ||
      warning_entry_within_tmr ||
      directive.primary_risk_phase == "Warning" ||
      directive.primary_risk_phase == "Danger" ||
      directive.primary_risk_phase == "Critical" ||
      directive.primary_warning_margin_m < -kRiskEpsilon ||
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

// Signed smallest angular difference (b - a), wrapped to [-180, +180].
// Used by clamp_heading_box_reachable and compute_heading_box_reachability to
// reason in own-relative coordinates. (File scope: shared by both functions.)
double signed_delta_deg(double from_deg, double to_deg) {
  double d = std::fmod(to_deg - from_deg, 360.0);
  if (d > 180.0) { d -= 360.0; }
  if (d < -180.0) { d += 360.0; }
  return d;
}

void clamp_heading_box_reachable(double& h_min_deg, double& h_max_deg,
                                 double own_hdg_deg, double rot_step_deg) {
  if (rot_step_deg <= 0.0) {
    return;  // clamp disabled
  }
  // Safety margin: shrink the effective reachable arc by a small epsilon so the
  // clamped box edges sit strictly inside the M5 NLP ROT feasibility bound.
  // Without this, float32(deg)→float64(rad) conversion + M4/M5 independent
  // deg↔rad paths accumulate ~0.02° error at the boundary, and M5's overlap
  // test (heading_min ≤ own_psi + rot_step) fails by a hair (e.g. M4 emits
  // 6.000° → M5 computes 5.998° reachable → 0.002° miss → INFEAS). A 0.3°
  // margin absorbs the conversion noise while staying well above ROT precision.
  constexpr double kClampMarginDeg = 0.3;
  const double eff_rot_step = std::max(rot_step_deg - kClampMarginDeg, 0.0);
  // Compute box centre + half-width in absolute heading, then map the CENTRE
  // into own-relative signed delta ∈ [-180, 180]. Reasoning on the centre
  // (not the two edges independently) avoids wrap artefacts where a narrow box
  // straddling the ±180° back-axis would have its edges map to opposite signs
  // and be mistaken for a full-range box.
  const double w_min = wrap_heading_deg(h_min_deg);
  const double w_max = wrap_heading_deg(h_max_deg);
  // Box width along the shorter arc (handles wrap-around boxes).
  double width = w_max - w_min;
  if (width < 0.0) { width += 360.0; }
  const double half_width = 0.5 * width;
  const double centre_abs = wrap_heading_deg(w_min + half_width);
  const double centre = signed_delta_deg(own_hdg_deg, centre_abs);
  const double d_min = centre - half_width;
  const double d_max = centre + half_width;
  const double reach_lo = -eff_rot_step;
  const double reach_hi =  eff_rot_step;
  // Already overlaps reachable arc → no-op.
  const bool overlaps = (d_min <= reach_hi) && (d_max >= reach_lo);
  if (overlaps) {
    return;
  }
  // Entirely outside. Translate along the directive direction (sign of centre)
  // until just tangent to the reachable arc.
  double new_d_min, new_d_max;
  if (centre > 0.0) {
    // Box is to starboard — pull lower edge to reach_hi.
    new_d_min = reach_hi;
    new_d_max = reach_hi + 2.0 * half_width;
  } else {
    // Box is to port — pull upper edge to reach_lo.
    new_d_max = reach_lo;
    new_d_min = reach_lo - 2.0 * half_width;
  }
  // Map back to absolute headings (preserve original min<max ordering).
  const double new_h_min = wrap_heading_deg(own_hdg_deg + new_d_min);
  const double new_h_max = wrap_heading_deg(own_hdg_deg + new_d_max);
  // Keep min/max ordered (no wrap-around box after clamp).
  h_min_deg = std::min(new_h_min, new_h_max);
  h_max_deg = std::max(new_h_min, new_h_max);
}

// v2.2 §4.6 reachability 合约 (M4 publish, M5 consume) — direction-aware.
// Codex β review 🔴 Blocker fix (task-mr6d2jyi-jnd08o):
//   heading_box_reachable_from_psi0_deg now publishes the MAX ATTAINABLE
//   DEVIATION in the preferred COLREGS direction within the box, measured from
//   own_psi — NOT the nearest-edge distance. The nearest-edge semantic was
//   consumed by M5 derive_row_bound_config as the preferred-direction min_alt
//   reach ceiling, so a starboard box [23,53] own=0 min_alt=30 published 23
//   (nearest edge) and M5 falsely flagged minalt_box_infeasible even though
//   30° is inside [23,53] and reachable via ROT. Direction-aware:
//     Starboard (+): d_max if positive (own→h_max), else 0 (box not on stbd).
//     Port      (-): |d_min| if negative (own→h_min), else 0 (box not on port).
//     Hold/ReduceSpeed: max(|d_min|,|d_max|) — no lateral direction.
//   box_allows_min_alt iff directional_reach ≥ min_alt (replaces the incorrect
//   box_width ≥ min_alt + rot_step criterion — 🟡1).
//   rot_step_deg is retained in the signature for API stability and future
//   per-step reach reasoning; the v2.2 direction-aware criterion does not
//   require it.
HeadingBoxReachability compute_heading_box_reachability(
    double h_min_deg, double h_max_deg,
    double own_hdg_deg, double rot_step_deg,
    double min_alt_rad,
    ColregsDirection preferred_direction) {
  (void)rot_step_deg;  // reserved (see rationale above); not used in v2.2 criterion
  HeadingBoxReachability r;
  const double d_min = signed_delta_deg(own_hdg_deg, h_min_deg);  // own→h_min, signed
  const double d_max = signed_delta_deg(own_hdg_deg, h_max_deg);  // own→h_max, signed

  // Direction-aware: max attainable deviation in the preferred COLREGS direction.
  // Starboard (+): positive deltas (h_max side); Port (-): negative deltas (h_min
  // side, magnitude). Hold/ReduceSpeed: no lateral direction, use max of both.
  double directional_reach_deg = 0.0;
  if (preferred_direction == ColregsDirection::Starboard) {
    directional_reach_deg = (d_max > 0.0) ? d_max : 0.0;
  } else if (preferred_direction == ColregsDirection::Port) {
    directional_reach_deg = (d_min < 0.0) ? std::fabs(d_min) : 0.0;
  } else {  // Hold / ReduceSpeed
    directional_reach_deg = std::max(std::fabs(d_min), std::fabs(d_max));
  }
  r.heading_box_reachable_from_psi0_deg = directional_reach_deg;

  // box_allows_min_alt: directional reach must be ≥ min_alt (NOT box_width).
  const double min_alt_deg = min_alt_rad * 180.0 / M_PI;
  r.box_allows_min_alt = (directional_reach_deg >= min_alt_deg);
  if (!r.box_allows_min_alt) {
    r.reachability_rationale =
        "directional_reach < min_alt (box caps preferred-direction deviation)";
  }
  return r;
}

}  // namespace mass_l3::m4
