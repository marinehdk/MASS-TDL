#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>
#include <l3_msgs/msg/constraint.hpp>
#include <l3_msgs/msg/rule_active.hpp>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

using builtin_interfaces::msg::Time;

namespace {

constexpr double kNonCompliantTargetThreshold = 0.4;  // [TBD-HAZID]
// W4-C: give-way crossing action window. COLREGs Rule 8/16 require action "in
// ample time". Give-way must escalate to INDEPENDENT_ACTION before the stand-on
// Rule 17(b) floor. [Data 2026-06-29]: cs-edge give-way stayed SOUND_WARNING
// down to 65m / TCPA 6.8s.
constexpr double kGivewayActionTcpaThresholdS = 180.0;

std::string phase_to_str(const TimingPhase p) {
  switch (p) {
    case TimingPhase::CRITICAL_ACTION:    return "T_emergency";
    case TimingPhase::INDEPENDENT_ACTION: return "T_act";
    case TimingPhase::SOUND_WARNING:      return "T_warn";
    case TimingPhase::PRESERVE_COURSE:
    default:                              return "T_standOn";
  }
}

bool requires_action(const RuleEvaluation& e) {
  const bool give_way = (e.role == Role::GIVE_WAY || e.role == Role::BOTH_GIVE_WAY);
  const bool standon_inextremis = (e.role == Role::STAND_ON &&
      (e.phase == TimingPhase::INDEPENDENT_ACTION || e.phase == TimingPhase::CRITICAL_ACTION));
  return e.is_active && (give_way || standon_inextremis);
}

bool should_escalate_noncompliant_standon(const RuleEvaluation& e) {
  return e.is_active && e.role == Role::STAND_ON &&
      e.phase == TimingPhase::SOUND_WARNING &&
      e.target_compliance < kNonCompliantTargetThreshold;
}

bool should_promote_directional_giveway_action(const RuleEvaluation& e) {
  const bool give_way = (e.role == Role::GIVE_WAY || e.role == Role::BOTH_GIVE_WAY);
  return e.is_active &&
      give_way &&
      e.phase == TimingPhase::PRESERVE_COURSE &&
      e.preferred_direction != "HOLD";
}

bool should_escalate_giveway_action(const RuleEvaluation& e) {
  const bool give_way = (e.role == Role::GIVE_WAY || e.role == Role::BOTH_GIVE_WAY);
  return e.is_active &&
      give_way &&
      e.phase == TimingPhase::SOUND_WARNING &&
      e.tcpa_s > 0.0 &&
      e.tcpa_s <= kGivewayActionTcpaThresholdS;
}

RuleEvaluation effective_evaluation(
    const RuleEvaluation& raw, const RuleParameters& params) {
  RuleEvaluation effective = raw;
  if (should_promote_directional_giveway_action(raw)) {
    effective.phase = TimingPhase::SOUND_WARNING;
    if (!effective.rationale.empty()) {
      effective.rationale += " ";
    }
    effective.rationale +=
        "[promoted: directional give-way conflict requires active M4 contract]";
  }
  if (should_escalate_giveway_action(effective)) {
    effective.phase = TimingPhase::INDEPENDENT_ACTION;
    if (!effective.rationale.empty()) {
      effective.rationale += " ";
    }
    effective.rationale +=
        "[escalated: give-way TCPA within action window (<" +
        std::to_string(static_cast<int>(kGivewayActionTcpaThresholdS)) +
        "s), Rule 8/16 ample-time]";
  }
  if (!should_escalate_noncompliant_standon(raw)) {
    return effective;
  }

  effective.phase = TimingPhase::INDEPENDENT_ACTION;
  effective.preferred_direction = "STARBOARD";
  if (effective.min_alteration_deg <= 0.0) {
    effective.min_alteration_deg = params.min_alteration_deg;
  }
  if (!effective.rationale.empty()) {
    effective.rationale += " ";
  }
  effective.rationale +=
      "[escalated: give-way target non-compliant before Rule 17(b) floor]";
  return effective;
}
}  // namespace

// NOLINTNEXTLINE(readability-convert-member-functions-to-static,readability-function-cognitive-complexity,readability-function-size)
l3_msgs::msg::COLREGsConstraint ConstraintGenerator::generate(
    const std::vector<RuleEvaluation>& evaluations,
    const RuleParameters& params, double confidence) const {
  l3_msgs::msg::COLREGsConstraint msg;
  msg.schema_version = 115U;

  std::vector<RuleEvaluation> effective_evaluations;
  effective_evaluations.reserve(evaluations.size());
  for (const auto& eval : evaluations) {
    effective_evaluations.push_back(effective_evaluation(eval, params));
  }

  // Collect active rules and determine overall phase
  std::string dominant_phase = "PRESERVE_COURSE";
  std::string rationale_parts;
  const RuleEvaluation* dominant = nullptr;

  for (const auto& eval : effective_evaluations) {
    if (!eval.is_active) {
      continue;
    }

    l3_msgs::msg::RuleActive ra;
    ra.rule_id = static_cast<uint8_t>(eval.rule_id);
    ra.target_id = eval.target_id;
    ra.rule_confidence = eval.confidence;
    ra.rationale = eval.rationale;
    ra.role = static_cast<uint8_t>(eval.role);
    ra.preferred_direction = eval.preferred_direction;
    ra.min_alteration_deg = static_cast<float>(eval.min_alteration_deg);
    ra.rule_phase = phase_to_str(eval.phase);
    msg.active_rules.push_back(ra);

    if (requires_action(eval) &&
        (dominant == nullptr || eval.phase > dominant->phase)) {
      dominant = &eval;
    }

    // Build combined rationale
    if (!rationale_parts.empty()) {
      rationale_parts += "; ";
    }
    rationale_parts += eval.rationale;

    // Determine dominant phase (most urgent wins)
    switch (eval.phase) {
      case TimingPhase::CRITICAL_ACTION:
        dominant_phase = "CRITICAL_ACTION";
        break;
      case TimingPhase::INDEPENDENT_ACTION:
        if (dominant_phase != "CRITICAL_ACTION") {
          dominant_phase = "INDEPENDENT_ACTION";
        }
        break;
      case TimingPhase::SOUND_WARNING:
        if (dominant_phase != "CRITICAL_ACTION" &&
            dominant_phase != "INDEPENDENT_ACTION") {
          dominant_phase = "SOUND_WARNING";
        }
        break;
      case TimingPhase::PRESERVE_COURSE:
      default:
        break;
    }

    // Generate constraint from preferred direction
    if (eval.preferred_direction != "HOLD") {
      l3_msgs::msg::Constraint c;
      c.constraint_type = "colregs";
      c.description = eval.rationale;
      c.unit = "deg";
      c.numeric_value = eval.min_alteration_deg;
      msg.constraints.push_back(c);
    }
  }

  msg.phase = dominant_phase;
  msg.confidence = std::min(static_cast<float>(confidence), 1.0F);
  msg.rationale = rationale_parts.empty() ? "No active COLREGs rules" : rationale_parts;
  
  bool conflict = false;
  for (const auto& eval : effective_evaluations) {
    if (requires_action(eval)) { conflict = true; break; }
  }
  msg.conflict_detected = conflict;

  if (dominant != nullptr) {
    msg.primary_role = static_cast<uint8_t>(dominant->role);
    msg.primary_preferred_direction = dominant->preferred_direction;
  } else {
    msg.primary_role = static_cast<uint8_t>(Role::FREE);
    msg.primary_preferred_direction = "HOLD";
  }

  return msg;
}

}  // namespace mass_l3::m6_colregs
