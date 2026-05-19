#include "m4_behavior_arbiter/behavior_arbiter.hpp"

#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

BehaviorArbiter::BehaviorArbiter(const BehaviorDictionary& dict,
                                 IvPHeadingDomain h_domain,
                                 IvPSpeedDomain s_domain,
                                 std::chrono::milliseconds ivp_timeout)
    : activation_(dict),
      solver_(h_domain, s_domain, std::make_unique<WeightedSumCombination>(),
              ivp_timeout),
      priority_() {}

ArbitrationResult BehaviorArbiter::run(const ArbitrationInputs& inputs) const {
  // Step 1: Compute active behavior set
  const auto active = activation_.compute_active_set(inputs);

  // Step 2: Build IvP constraints from inputs
  const IvPHardConstraints constraints = build_constraints(inputs);

  // Step 3: Phase E1 — IvP functions are empty (utility construction is E2+)
  const std::vector<IvPCombinationStrategy::WeightedFunction> weighted_fns;

  // Step 4: Attempt IvP solve (returns nullopt for empty weighted_fns)
  const auto sol = solver_.solve(weighted_fns, constraints);

  // Step 5: Select primary behavior using priority rules
  const BehaviorType primary =
      priority_.select_primary(active, sol.value_or(IvPSolution{}), inputs);

  // Step 6: Build result
  ArbitrationResult result;
  result.primary    = primary;
  result.confidence = sol.has_value() ? 0.95 : 0.75;  // [TBD-HAZID] stub utility

  if (sol.has_value()) {
    result.heading_min_deg = sol->heading_min_deg;
    result.heading_max_deg = sol->heading_max_deg;
    result.speed_min_kn    = sol->speed_min_kn;
    result.speed_max_kn    = sol->speed_max_kn;
    result.rationale       = sol->rationale;
  } else {
    result.heading_min_deg = 0.0;
    result.heading_max_deg = 359.0;  // [TBD-HAZID] default full-range
    result.speed_min_kn    = constraints.speed_min_kn;
    result.speed_max_kn    = constraints.speed_max_kn;
    result.rationale       = build_rationale(active);
  }

  asdr_logger_.log_behavior_change(
      prev_primary_, result.primary,
      inputs.odd_state.current_zone, result.rationale);
  prev_primary_ = result.primary;

  if (!sol.has_value()) {
    asdr_logger_.log_ivp_failure(active.size(),
                                 static_cast<uint8_t>(inputs.odd_state.current_zone));
  }

  spdlog::debug("[M4] BehaviorArbiter::run primary={} confidence={:.2f}",
                static_cast<int>(result.primary), result.confidence);

  return result;
}

IvPHardConstraints BehaviorArbiter::build_constraints(
    const ArbitrationInputs& inputs) const {
  IvPHardConstraints constraints;
  constraints.speed_min_kn = 0.0;
  constraints.speed_max_kn = 15.0;  // [TBD-HAZID] from ODD envelope
  constraints.cpa_safe_m   = inputs.cpa_safe_m;
  constraints.targets      = inputs.world_state.targets;
  // Phase E1: heading_allowed_ranges_deg left empty (unconstrained)
  return constraints;
}

std::string BehaviorArbiter::build_rationale(
    const std::vector<BehaviorType>& active) const {
  std::string rationale = "active:{";
  for (size_t i = 0U; i < active.size(); ++i) {
    if (i > 0U) {
      rationale += ",";
    }
    switch (active[i]) {
      case BehaviorType::Transit:    rationale += "Transit";    break;
      case BehaviorType::ColregAvoid:rationale += "ColregAvoid";break;
      case BehaviorType::DpHold:     rationale += "DpHold";     break;
      case BehaviorType::Berth:      rationale += "Berth";      break;
      case BehaviorType::MrcDrift:   rationale += "MrcDrift";   break;
      case BehaviorType::MrcAnchor:  rationale += "MrcAnchor";  break;
      case BehaviorType::MrcHeaveTo: rationale += "MrcHeaveTo"; break;
      default:
        // Defensive fallback: covers future BehaviorType additions before switch is updated.
        spdlog::warn("[M4] BehaviorArbiter: unknown BehaviorType={}",
                     static_cast<int>(active[i]));
        rationale += "Unknown";
        break;
    }
  }
  rationale += "} IvP:stub(E1)";
  return rationale;
}

}  // namespace mass_l3::m4
