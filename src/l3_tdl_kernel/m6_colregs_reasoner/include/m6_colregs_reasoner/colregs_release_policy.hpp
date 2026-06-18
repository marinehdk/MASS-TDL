#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace mass_l3::m6_colregs {

constexpr double kGiveWayProjectionReleaseRangeMultiple = 1.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
// COLREG Rule 8(d) / 15 past-and-clear for crossing give-way: the target must
// be abaft the beam (relative bearing >= 112.5 deg along the reference
// avoidance heading) before the encounter is resolved. 112.5 deg derives from
// COLREG Rule 13(b) ("more than 22.5 degrees abaft her beam", 90 deg beam +
// 22.5 deg = 112.5 deg from bow) and Rule 21(c) (sternlight 135 deg arc =
// 67.5 deg from right aft each side). NOT Rule 3(g) -- Rule 3(g) defines
// "vessel restricted in her ability to manoeuvre" and is unrelated to the
// abaft-beam sector. A 40 deg bow-clear threshold releases while the target
// is still on the bow and the own-ship is still altering -- the "early return
// to route" the phase gate flags as a Rule 8(d) violation.
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 112.5;
// Execution margin above the COLREGs abaft-beam threshold for Rule 15 release.
// M6 publishes a clean release, then M4/M5/L4 and the phase gate observe it on
// later samples; a release exactly at 112.5 deg can be seen downstream as still
// ahead of the beam. Keep the legal threshold explicit and add margin only at
// the crossing release wrapper.
constexpr double kGiveWayCrossingReleaseExecutionMarginDeg = 5.0;
constexpr double kGiveWayCrossingReleaseExecutionAbaftDeg =
    kGiveWayProjectionReleaseReferenceBowClearDeg +
    kGiveWayCrossingReleaseExecutionMarginDeg;
// COLREG Rule 13(d) overtake past-and-clear: own-ship is "finally past" once it
// is in the target's forward hemisphere. Aspect angle convention: 0 deg =
// own-ship dead ahead of the target's bow, 90 deg = on the beam, 180 deg =
// dead astern. Forward hemisphere = aspect within 90 deg of the bow.
constexpr double kGiveWayOvertakeReleaseAspectAheadDeg = 90.0;
constexpr double kGiveWayReleaseKnToMps = 0.514444;
constexpr double kGiveWayReleasePi = 3.14159265358979323846;
constexpr double kStandOnEmergencyReleaseCpaM = 185.2;
constexpr double kStandOnEmergencyReleaseRangeMultiple = 2.0;
constexpr double kTcpaClampedPastEpsilonS = 0.5;

enum class GiveWayProjectionReleaseGate {
  REFERENCE_CLEAR,
  CURRENT_ABAFT,
};

inline bool give_way_current_projection_release_allowed(
    bool rule13_projection_latched,
    bool rule15_projection_latched,
    bool rule14_projection_latched,
    bool duty_latched) {
  return rule14_projection_latched ||
      (duty_latched && !rule13_projection_latched && !rule15_projection_latched);
}

inline bool give_way_crossing_release_safe(
    bool reference_release_ok,
    double current_relative_bearing_abs_deg) {
  return reference_release_ok &&
      std::isfinite(current_relative_bearing_abs_deg) &&
      current_relative_bearing_abs_deg > kGiveWayCrossingReleaseExecutionAbaftDeg;
}

inline double give_way_reference_heading_cpa_m(
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(bearing_deg) ||
      !std::isfinite(target_heading_deg) ||
      !std::isfinite(target_speed_kn) ||
      !std::isfinite(own_speed_kn) ||
      !std::isfinite(reference_heading_deg) ||
      range_m < 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  const double bearing_rad = bearing_deg * kGiveWayReleasePi / 180.0;
  const double target_heading_rad = target_heading_deg * kGiveWayReleasePi / 180.0;
  const double reference_heading_rad = reference_heading_deg * kGiveWayReleasePi / 180.0;
  const double rel_x = range_m * std::sin(bearing_rad);
  const double rel_y = range_m * std::cos(bearing_rad);
  const double target_speed_mps = target_speed_kn * kGiveWayReleaseKnToMps;
  const double own_speed_mps = own_speed_kn * kGiveWayReleaseKnToMps;
  const double rel_vx = target_speed_mps * std::sin(target_heading_rad) -
      own_speed_mps * std::sin(reference_heading_rad);
  const double rel_vy = target_speed_mps * std::cos(target_heading_rad) -
      own_speed_mps * std::cos(reference_heading_rad);
  const double rel_v2 = rel_vx * rel_vx + rel_vy * rel_vy;
  double tcpa_s = 0.0;
  if (rel_v2 > 1e-9) {
    tcpa_s = -((rel_x * rel_vx) + (rel_y * rel_vy)) / rel_v2;
    if (tcpa_s < 0.0) {
      tcpa_s = 0.0;
    }
  }
  const double cpa_x = rel_x + rel_vx * tcpa_s;
  const double cpa_y = rel_y + rel_vy * tcpa_s;
  return std::hypot(cpa_x, cpa_y);
}

// Reference-heading CPA projection release for crossing give-way: resolves the
// encounter when, along the reference (onset) avoidance heading, the target is
// abaft the beam AND the CPA projection at safe distance. The abaft-beam gate
// (Rule 8(d)/13(b)+21(c), same 112.5 deg threshold as the REFERENCE_CLEAR
// projection path) prevents release while the target is still on the bow --
// the rule15-cs early-release bug fixed alongside the ea6b06e6 commit's sibling
// give_way_projection_release_safe(REFERENCE_CLEAR) path. Without this gate the
// OR branch in colregs_reasoner_node.cpp (give_way_reference_heading_release_ok)
// would release at rel_brg ~37 deg just because the CPA projection happens to
// be safe, defeating the Rule 8(d) "finally past and clear" requirement.
inline bool give_way_reference_heading_release_safe(
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg,
    double cpa_safe_m) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(bearing_deg) ||
      !std::isfinite(reference_heading_deg) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0 ||
      range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple) {
    return false;
  }
  // Abaft-beam gate: target relative bearing along the reference avoidance
  // heading must exceed 112.5 deg (Rule 13(b)+21(c)) before the encounter is
  // resolved. Normalize signed (bearing - reference_heading) to [-180,180].
  double rel_brg_deg = bearing_deg - reference_heading_deg;
  while (rel_brg_deg > 180.0) rel_brg_deg -= 360.0;
  while (rel_brg_deg < -180.0) rel_brg_deg += 360.0;
  if (std::fabs(rel_brg_deg) < kGiveWayProjectionReleaseReferenceBowClearDeg) {
    return false;
  }
  const double cpa_m = give_way_reference_heading_cpa_m(
      range_m,
      bearing_deg,
      target_heading_deg,
      target_speed_kn,
      own_speed_kn,
      reference_heading_deg);
  return std::isfinite(cpa_m) && cpa_m >= cpa_safe_m;
}

inline bool give_way_projection_release_safe(
    bool cpa_projection_past_and_safe,
    double range_m,
    double cpa_safe_m,
    double current_relative_bearing_abs_deg,
    double reference_relative_bearing_abs_deg,
    GiveWayProjectionReleaseGate gate) {
  if (!cpa_projection_past_and_safe ||
      !std::isfinite(range_m) ||
      !std::isfinite(cpa_safe_m) ||
      !std::isfinite(current_relative_bearing_abs_deg) ||
      !std::isfinite(reference_relative_bearing_abs_deg) ||
      cpa_safe_m <= 0.0) {
    return false;
  }
  if (range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple) {
    return false;
  }
  // The reference bow-clear (target abaft the beam along the reference heading)
  // is a crossing/REFERENCE_CLEAR gate criterion only; the head-on CURRENT_ABAFT
  // gate is governed by current_relative_bearing_abs_deg below and must not be
  // blocked by a reference-heading bearing.
  if (gate == GiveWayProjectionReleaseGate::REFERENCE_CLEAR &&
      reference_relative_bearing_abs_deg < kGiveWayProjectionReleaseReferenceBowClearDeg) {
    return false;
  }
  return gate == GiveWayProjectionReleaseGate::REFERENCE_CLEAR ||
      current_relative_bearing_abs_deg >= kGiveWayProjectionReleaseCurrentAbaftDeg;
}

inline double stand_on_release_cpa_floor_m(double configured_cpa_safe_m) {
  if (!std::isfinite(configured_cpa_safe_m) || configured_cpa_safe_m <= 0.0) {
    return kStandOnEmergencyReleaseCpaM;
  }
  return std::min(configured_cpa_safe_m, kStandOnEmergencyReleaseCpaM);
}

inline bool stand_on_late_action_release_safe(
    bool latched,
    bool range_closing,
    double range_m,
    double cpa_m,
    double tcpa_s,
    double configured_cpa_safe_m,
    double current_relative_bearing_abs_deg) {
  const double cpa_floor_m = stand_on_release_cpa_floor_m(configured_cpa_safe_m);
  return latched &&
      !range_closing &&
      std::isfinite(range_m) &&
      std::isfinite(cpa_m) &&
      std::isfinite(tcpa_s) &&
      std::isfinite(current_relative_bearing_abs_deg) &&
      range_m >= cpa_floor_m * kStandOnEmergencyReleaseRangeMultiple &&
      cpa_m >= cpa_floor_m &&
      tcpa_s <= kTcpaClampedPastEpsilonS;
}

// COLREG Rule 13(d): an overtaking give-way vessel is finally past and clear
// only once it has crossed into the target's forward hemisphere (aspect ahead
// of the beam) at a safe range with a past/safe CPA projection. The crossing
// bow-clear gate (kGiveWayProjectionReleaseReferenceBowClearDeg) is wrong here:
// on near-parallel overtaking courses the target stays on the own-ship's bow
// throughout, so a relative-bearing gate never opens and -- when it does open
// early -- releases before the own-ship is genuinely ahead. Aspect is the
// Rule-13-correct coordinate.
//
// COLREGs compliance note [ref-engineering-approximation]:
// Rule 13(d) "finally past and clear" is a qualitative "ordinary practice of
// seamen" standard (NLM maritime_regulations notebook, high confidence, 124
// sources). Forward hemisphere alone is insufficient (Steamship Mutual LP:
// "always safer to cross astern"; Rule 13(a) advises avoid crossing ahead).
// This function implements a multi-factor engineering approximation:
//   aspect < 90 deg (forward hemisphere) AND
//   cpa_projection_past_and_safe (CPA past, at safe distance) AND
//   range >= cpa_safe (range hard floor) AND
//   !range_closing (no remaining closing risk)
// Together these approximate "safely ahead with no remaining risk of
// collision" per Rule 13(d) + Rule 8(d). COLREGs intentionally has no single
// numerical threshold for "past and clear".
inline bool give_way_overtake_release_safe(
    bool cpa_projection_past_and_safe,
    double range_m,
    double aspect_deg,
    double cpa_safe_m) {
  if (!cpa_projection_past_and_safe ||
      !std::isfinite(range_m) ||
      !std::isfinite(aspect_deg) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0) {
    return false;
  }
  if (range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple) {
    return false;
  }
  // Forward hemisphere: aspect within kGiveWayOvertakeReleaseAspectAheadDeg of
  // the bow (0 deg) or equivalently of 360 deg. Use the smaller angular
  // distance to 0 so 350 deg (just port of the bow) also counts as ahead.
  double aspect = aspect_deg;
  while (aspect < 0.0) aspect += 360.0;
  while (aspect >= 360.0) aspect -= 360.0;
  const double off_bow_deg = std::min(aspect, 360.0 - aspect);
  return off_bow_deg < kGiveWayOvertakeReleaseAspectAheadDeg;
}

}  // namespace mass_l3::m6_colregs
