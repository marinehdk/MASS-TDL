#include "m2_world_model/encounter_classifier.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::m2 {

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;
constexpr double kHalfCircleDeg = 180.0;
constexpr double kFullCircleDeg = 360.0;

/// Compute the aspect angle: bearing from target to own ship, relative to target heading.
/// Returns value in [-180, 180].
double compute_aspect_deg_(double relative_bearing_deg,
                           double own_heading_deg,
                           double target_heading_deg) {
  // Absolute bearing from own ship to target
  double abs_bearing_deg = own_heading_deg + relative_bearing_deg;
  // Bearing from target to own ship = reciprocal
  double recip_bearing_deg = abs_bearing_deg + kHalfCircleDeg;
  // Aspect = (bearing from target to own ship) - target heading
  double aspect = recip_bearing_deg - target_heading_deg;
  // Normalise to [-180, 180]
  aspect = std::fmod(aspect + kHalfCircleDeg, kFullCircleDeg);
  if (aspect < 0.0) aspect += kFullCircleDeg;
  aspect -= kHalfCircleDeg;
  return aspect;
}

}  // namespace

EncounterClassifier::EncounterClassifier(Config cfg) : cfg_(std::move(cfg)) {}

l3_msgs::msg::EncounterClassification
EncounterClassifier::classify(double relative_bearing_deg,
                              double own_heading_deg,
                              double target_heading_deg,
                              double relative_speed_mps,
                              double cpa_m) const {
  l3_msgs::msg::EncounterClassification result;

  // Normalise relative bearing to [0, 360) for sector checks
  double bearing_0_360 = normalize_bearing_deg_(relative_bearing_deg);

  // Store the normalised relative bearing in [-180, 180] convention per message spec
  double bearing_signed = bearing_0_360;
  if (bearing_signed > kHalfCircleDeg) {
    bearing_signed -= kFullCircleDeg;
  }
  result.relative_bearing_deg = bearing_signed;

  // Compute aspect angle
  result.aspect_angle_deg = compute_aspect_deg_(bearing_signed, own_heading_deg,
                                                 target_heading_deg);

  // ── 1. Safe pass check ──
  if (relative_speed_mps < cfg_.safe_pass_speed_threshold_mps &&
      cpa_m > cfg_.safe_pass_min_cpa_m) {
    result.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_AMBIGUOUS;
    result.is_giveway = false;
    return result;
  }

  // ── 2. Overtaking (Rule 13) ──
  // Target in stern sector [112.5, 247.5]
  if (bearing_0_360 >= cfg_.overtaking_bearing_min_deg &&
      bearing_0_360 <= cfg_.overtaking_bearing_max_deg) {
    result.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING;
    // Overtaking vessel is give-way vessel
    result.is_giveway = true;
    return result;
  }

  // ── 3–4. Heading difference for head-on check ──
  double heading_diff = smallest_angle_diff_deg_(own_heading_deg, target_heading_deg);

  // ── 4. Head-on (Rule 14) ──
  if (heading_diff <= cfg_.head_on_heading_diff_tol_deg) {
    result.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON;
    // Both vessels are give-way in head-on situations
    result.is_giveway = true;
    return result;
  }

  // ── 5. Crossing (Rule 15) ──
  result.encounter_type =
      l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING;

  // Target on starboard side (bearing > 0) → own ship is give-way
  // Target on port side (bearing < 0) → own ship is stand-on
  result.is_giveway = (bearing_signed > 0.0);

  return result;
}

double EncounterClassifier::normalize_bearing_deg_(double bearing) noexcept {
  bearing = std::fmod(bearing, kFullCircleDeg);
  if (bearing < 0.0) {
    bearing += kFullCircleDeg;
  }
  return bearing;
}

double EncounterClassifier::smallest_angle_diff_deg_(double a, double b) noexcept {
  double diff = std::abs(a - b);
  diff = std::fmod(diff, kFullCircleDeg);
  if (diff > kHalfCircleDeg) {
    diff = kFullCircleDeg - diff;
  }
  return diff;
}

}  // namespace mass_l3::m2
