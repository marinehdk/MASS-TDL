#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

constexpr double kGiveWayProjectionReleaseRangeMultiple = 1.0;
constexpr double kGiveWayOpeningReleaseRangeMultiple = 1.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
// Crossing/head-on give-way projection release (REFERENCE_CLEAR gate): the
// target must have drawn past the beam (relative bearing > 90°, strictly past
// — at the beam is not yet clear, along the reference avoidance heading)
// before the encounter is resolved. The 40° quick-impl baseline released
// while the target was still on the bow — the early-return-to-route the phase
// gate flags as a Rule 8(d) violation.
//
// The 112.5° abaft-beam (Rule 13(b) overtaking sector) is unreachable for
// shallow slow crossings after starboard avoidance (rule15-cs cog=290/10.6kn
// only crosses the 90° beam once own-ship recovers to route). Crossing uses
// the 90° beam; overtaking's stricter 112.5° is enforced in
// past_and_clear_from_heading (reasoner_node.cpp), not here. Internal design
// report §4.2: abaft_threshold = 112.5 if is_overtaking else 90.0.
// NOT Rule 3(g) (defines "vessel restricted in ability to manoeuvre",
// unrelated to abaft beam); the 112.5° derives from the beam (90°) plus, for
// overtaking only, Rule 13(b) "more than 22.5° abaft her beam".
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 90.0;
constexpr double kGiveWayReleaseKnToMps = 0.514444;
constexpr double kGiveWayReleasePi = 3.14159265358979323846;
constexpr double kFinallyPastClearEmergencyCpaM = 185.2;
constexpr double kStandOnEmergencyReleaseCpaM = kFinallyPastClearEmergencyCpaM;
constexpr double kStandOnEmergencyReleaseRangeMultiple = 2.0;
constexpr double kTcpaClampedPastEpsilonS = 0.5;

enum class GiveWayProjectionReleaseGate {
  REFERENCE_CLEAR,
  CURRENT_ABAFT,
};

inline bool evaluation_has_give_way_duty(const RuleEvaluation& eval) {
  return eval.is_active &&
      (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
}

inline bool give_way_duty_from_raw_or_fsm(
    bool raw_give_way_duty,
    bool fsm_engaged,
    const RuleEvaluation& fsm_held_eval) {
  return raw_give_way_duty ||
      (fsm_engaged && evaluation_has_give_way_duty(fsm_held_eval));
}

inline bool give_way_duty_onset_signal(
    bool raw_own_give_way,
    bool own_stand_on,
    bool past_and_clear,
    bool cpa_projection_past_and_safe,
    double tcpa_s,
    double cpa_m,
    double t_plan_s,
    double cpa_hard_m,
    bool range_closing,
    bool primary_own_give_way = true) {
  return raw_own_give_way &&
      primary_own_give_way &&
      !own_stand_on &&
      !past_and_clear &&
      !cpa_projection_past_and_safe &&
      range_closing &&
      std::isfinite(tcpa_s) &&
      std::isfinite(cpa_m) &&
      std::isfinite(t_plan_s) &&
      std::isfinite(cpa_hard_m) &&
      tcpa_s <= t_plan_s &&
      cpa_m < cpa_hard_m;
}

inline bool primary_rule_onset_allowed(
    int candidate_rule_id,
    int latched_primary_rule_id) {
  if (candidate_rule_id != 13 && candidate_rule_id != 14 &&
      candidate_rule_id != 15) {
    return true;
  }
  return latched_primary_rule_id == 0 ||
      latched_primary_rule_id == candidate_rule_id;
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

inline bool give_way_reference_heading_release_safe(
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg,
    double cpa_safe_m) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0 ||
      range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple) {
    return false;
  }
  // Past-beam guard (stage2): a safe projected CPA alone is not enough to clear
  // the give-way duty. The target must have drawn past the reference beam
  // (captured at duty onset) — i.e. its relative bearing from the reference
  // heading is at least the bow-clear angle. Without this, own-ship's own
  // avoidance turn transiently opens the projected CPA while the target is still
  // on the bow (rule15-cs released at 37.7° rel mid-avoidance), producing a
  // premature release and an impossible route return (Rule 8(d) past-and-clear).
  const double delta_rad =
      (bearing_deg - reference_heading_deg) * kGiveWayReleasePi / 180.0;
  double rel = std::fmod(delta_rad + 3.0 * kGiveWayReleasePi, 2.0 * kGiveWayReleasePi) -
      kGiveWayReleasePi;
  const double reference_rel_abs_deg = std::fabs(rel) * 180.0 / kGiveWayReleasePi;
  if (reference_rel_abs_deg < kGiveWayProjectionReleaseReferenceBowClearDeg) {
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

inline bool give_way_opening_reference_heading_release_safe(
    bool range_closing,
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg,
    double cpa_safe_m) {
  if (range_closing ||
      !std::isfinite(range_m) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0 ||
      range_m < cpa_safe_m * kGiveWayOpeningReleaseRangeMultiple) {
    return false;
  }
  const double delta_rad =
      (bearing_deg - reference_heading_deg) * kGiveWayReleasePi / 180.0;
  double rel = std::fmod(delta_rad + 3.0 * kGiveWayReleasePi, 2.0 * kGiveWayReleasePi) -
      kGiveWayReleasePi;
  const double reference_rel_abs_deg = std::fabs(rel) * 180.0 / kGiveWayReleasePi;
  if (reference_rel_abs_deg <= kGiveWayProjectionReleaseReferenceBowClearDeg) {
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

inline bool rule15_target_track_release_safe(
    bool range_closing,
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double cpa_m,
    double tcpa_s,
    double cpa_safe_m) {
  if (range_closing ||
      !std::isfinite(range_m) ||
      !std::isfinite(bearing_deg) ||
      !std::isfinite(target_heading_deg) ||
      !std::isfinite(cpa_m) ||
      !std::isfinite(tcpa_s) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0 ||
      range_m < cpa_safe_m * kGiveWayOpeningReleaseRangeMultiple ||
      cpa_m < cpa_safe_m ||
      tcpa_s > kTcpaClampedPastEpsilonS) {
    return false;
  }

  // Crossing give-way release should clear after own ship has safely passed
  // astern of the target's track, not only after the target crosses own's
  // original beam. GNC follows waypoints; holding the duty until reference beam
  // clear can force a long return-leg excursion after CPA is already safe.
  const double bearing_rad = bearing_deg * kGiveWayReleasePi / 180.0;
  const double target_heading_rad = target_heading_deg * kGiveWayReleasePi / 180.0;
  const double rel_east_m = range_m * std::sin(bearing_rad);
  const double rel_north_m = range_m * std::cos(bearing_rad);
  const double target_axis_east = std::sin(target_heading_rad);
  const double target_axis_north = std::cos(target_heading_rad);
  const double own_minus_target_along_m =
      -(rel_east_m * target_axis_east + rel_north_m * target_axis_north);
  return own_minus_target_along_m < 0.0;
}

inline bool give_way_opening_reference_release_applies_to_rule(int rule_id) {
  return rule_id == 15;
}

inline bool give_way_projection_reference_release_applies_to_rule(int rule_id) {
  return rule_id == 15;
}

inline double give_way_final_release_cpa_floor_m(
    double configured_cpa_safe_m,
    double configured_cpa_release_m,
    bool give_way_latched,
    bool rule13_latched) {
  if (rule13_latched) {
    return kFinallyPastClearEmergencyCpaM;
  }
  if (give_way_latched &&
      std::isfinite(configured_cpa_release_m) &&
      configured_cpa_release_m > 0.0) {
    return configured_cpa_release_m;
  }
  return configured_cpa_safe_m;
}

inline bool rule13_overtaking_along_axis_past_clear(
    double range_m,
    double bearing_deg,
    double target_heading_deg) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(bearing_deg) ||
      !std::isfinite(target_heading_deg) ||
      range_m <= 0.0) {
    return false;
  }
  const double bearing_rad = bearing_deg * kGiveWayReleasePi / 180.0;
  const double target_heading_rad = target_heading_deg * kGiveWayReleasePi / 180.0;
  const double rel_east_m = range_m * std::sin(bearing_rad);
  const double rel_north_m = range_m * std::cos(bearing_rad);
  const double target_axis_east = std::sin(target_heading_rad);
  const double target_axis_north = std::cos(target_heading_rad);
  const double own_minus_target_along_m =
      -(rel_east_m * target_axis_east + rel_north_m * target_axis_north);
  return own_minus_target_along_m > 0.0;
}

inline bool rule13_release_past_and_clear(
    bool rule13_release_context,
    bool bearing_past_and_clear,
    bool along_axis_past_and_clear) {
  return bearing_past_and_clear &&
      (!rule13_release_context || along_axis_past_and_clear);
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
  // REFERENCE_CLEAR gate (crossing/head-on give-way): the target must have
  // drawn past the beam along the reference avoidance heading (> 90°, strictly
  // past — at the beam is not yet clear) before the encounter is resolved. The
  // CURRENT_ABAFT gate (Rule 14 head-on) releases on the target being abaft
  // the CURRENT heading (>= 150°) and is NOT gated by the reference bearing —
  // during a head-on the target can be abaft the current beam while still
  // forward of the reference heading as own-ship rotates.
  if (gate == GiveWayProjectionReleaseGate::REFERENCE_CLEAR) {
    return reference_relative_bearing_abs_deg > kGiveWayProjectionReleaseReferenceBowClearDeg;
  }
  return current_relative_bearing_abs_deg >= kGiveWayProjectionReleaseCurrentAbaftDeg;
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

}  // namespace mass_l3::m6_colregs
