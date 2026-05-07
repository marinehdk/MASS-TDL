#include "m8_hmi_transparency_bridge/tor_request_generator.hpp"

namespace mass_l3::m8 {

bool TorRequestGenerator::should_issue_tor(
    const SatAggregator& /*aggregator*/,
    const TorProtocol& /*tor*/,
    SatAggregator::TimePoint /*now*/) const
{
  return false;
}

std::string TorRequestGenerator::build_reason(
    const SatAggregator& /*aggregator*/,
    SatAggregator::TimePoint /*now*/) const
{
  return {};
}

}  // namespace mass_l3::m8
