#include "m4_behavior_arbiter/behavior_activation.hpp"

namespace mass_l3::m4 {

bool BehaviorActivationCondition::is_transit_applicable(
    const ArbitrationInputs& inputs) {
  return inputs.odd_state.envelope_state == ODDStateMsg::ENVELOPE_IN ||
         inputs.odd_state.envelope_state == ODDStateMsg::ENVELOPE_EDGE;
}

bool BehaviorActivationCondition::is_colreg_avoid_applicable(
    const ArbitrationInputs& inputs) {
  return !inputs.colregs_constraint.active_rules.empty() &&
         inputs.colregs_constraint.conflict_detected;
}

std::vector<BehaviorDescriptor>
BehaviorActivationCondition::compute_active_set(
    const ArbitrationInputs& inputs,
    const BehaviorDictionary& dictionary) {
  auto candidates = dictionary.get_active_subset(inputs.odd_state,
                                                  inputs.mode_cmd);
  std::vector<BehaviorDescriptor> active;
  active.reserve(candidates.size());

  for (const auto& candidate : candidates) {
    bool applicable = false;

    if (candidate.activation_rule == "odd_in_or_edge") {
      applicable = is_transit_applicable(inputs);
    } else if (candidate.activation_rule == "colregs_conflict_detected") {
      applicable = is_colreg_avoid_applicable(inputs);
    } else {
      applicable = true;
    }

    if (applicable) {
      active.push_back(candidate);
    }
  }

  return active;
}

}  // namespace mass_l3::m4
