#include "m8_hmi_transparency_bridge/asdr_logger.hpp"

namespace mass_l3::m8 {

AsdrLogger::AsdrLogger(const std::string& signature_alg)
    : signature_alg_{signature_alg} {}

void AsdrLogger::log_snapshot(const SatAggregator& /*aggregator*/,
                               SatAggregator::TimePoint /*now*/)
{
}

}  // namespace mass_l3::m8
