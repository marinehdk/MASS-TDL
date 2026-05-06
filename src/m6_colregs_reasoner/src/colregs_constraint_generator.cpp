#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"

#include <algorithm>

namespace mass_l3::m6_colregs {

using builtin_interfaces::msg::Time;

l3_msgs::msg::COLREGsConstraint ConstraintGenerator::generate(
    const std::vector<RuleEvaluation>& evaluations,
    const RuleParameters& params, double confidence) const {
  l3_msgs::msg::COLREGsConstraint msg;

  // Collect active rules and determine overall phase
  std::string dominant_phase = "PRESERVE_COURSE";
  std::string rationale_parts;

  for (const auto& eval : evaluations) {
    if (!eval.is_active) {
      continue;
    }

    l3_msgs::msg::RuleActive ra;
    ra.rule_id = static_cast<uint8_t>(eval.rule_id);
    ra.target_id = eval.target_id;
    ra.rule_confidence = eval.confidence;
    ra.rationale = eval.rationale;
    msg.active_rules.push_back(ra);

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
  msg.confidence = std::min(static_cast<float>(confidence), 1.0f);
  msg.rationale = rationale_parts.empty() ? "No active COLREGs rules" : rationale_parts;

  return msg;
}

}  // namespace mass_l3::m6_colregs
