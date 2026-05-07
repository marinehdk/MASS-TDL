#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"

namespace mass_l3::m8 {

AdaptiveSatTrigger::AdaptiveSatTrigger(const AdaptiveSatTriggerConfig& config)
    : config_{config} {}

SatTriggerDecision AdaptiveSatTrigger::evaluate(
    const SatAggregator& /*aggregator*/,
    SatAggregator::TimePoint /*now*/) const
{
  return SatTriggerDecision::kNoTrigger;
}

}  // namespace mass_l3::m8
