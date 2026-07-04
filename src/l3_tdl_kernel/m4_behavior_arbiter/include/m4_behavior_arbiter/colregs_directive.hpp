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
  bool rule13_active{false};
  bool rule15_active{false};
  bool rule14_active{false};
  std::string primary_threat_id;
  double primary_risk_score{0.0};
  double primary_warning_margin_m{0.0};
  double primary_danger_margin_m{0.0};
  double primary_closing_speed_mps{0.0};
  double primary_tdv_warning_s{0.0};
  std::string primary_risk_phase;
  bool speed_reduction_preferred{false};
};

struct HeadingWindow {
  double heading_min_deg{0.0};
  double heading_max_deg{0.0};
};

// v2.2 §4.6 reachability 合约 (M4 publish, M5 consume).
// Codex β review 🔴 Blocker fix (task-mr6d2jyi-jnd08o): the
// heading_box_reachable_from_psi0_deg semantic is now DIRECTION-AWARE — it is
// the max attainable deviation in the preferred COLREGS direction within the
// box, measured from own_psi (not the nearest-edge distance). This is the value
// M5 consumes as the preferred-direction min_alt reach ceiling.
struct HeadingBoxReachability {
  double heading_box_reachable_from_psi0_deg{0.0};  // preferred-direction max deviation from own_psi
  bool   box_allows_min_alt{false};                  // directional reach ≥ min_alt?
  std::string reachability_rationale;                 // 不够时 reason
};

[[nodiscard]] HeadingBoxReachability compute_heading_box_reachability(
    double h_min_deg, double h_max_deg,
    double own_hdg_deg, double rot_step_deg,
    double min_alt_rad,
    ColregsDirection preferred_direction);

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
[[nodiscard]] bool dynamic_risk_requires_speed_cap(const ColregsDirective& directive);
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

// Fix F-1 (plan↔exec ROT alignment, 2026-07-03): clamp a finite M4 heading box
// [h_min_deg, h_max_deg] (degrees, may wrap across 0/360) so it always contains
// at least one heading reachable from own_hdg_deg within one ROT step
// (rot_step_deg = rot_max_deg_s * dt_s). Preserves the directive direction: if
// the box partially overlaps the reachable arc [own-rot_step, own+rot_step],
// the overlap is kept (directive-side edge preferred); if the box is entirely
// outside the reachable arc, it is translated along the directive direction
// (sign of box-centre minus own_hdg) until just tangent to the reachable arc.
// No-op when rot_step_deg <= 0 (clamp disabled) or the box already overlaps.
//
// Rationale: without this clamp M4 can publish a corridor (e.g. onset [60,90]
// while own_psi≈0) that is not one-step ROT-reachable. M5 NLP (Fix E) then finds
// the own_psi→psi[0] ROT row infeasible → IPOPT Infeasible → geometric fallback
// → no real avoidance. The clamp guarantees the published corridor is always
// first-step executable by GNC.
void clamp_heading_box_reachable(double& h_min_deg, double& h_max_deg,
                                 double own_hdg_deg, double rot_step_deg);

}  // namespace mass_l3::m4
