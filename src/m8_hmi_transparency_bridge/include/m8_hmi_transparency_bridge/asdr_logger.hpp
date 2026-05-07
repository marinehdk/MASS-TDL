#ifndef MASS_L3_M8_ASDR_LOGGER_HPP_
#define MASS_L3_M8_ASDR_LOGGER_HPP_

#include <string>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

class AsdrLogger final {
 public:
  explicit AsdrLogger(const std::string& signature_alg = "SHA256");
  ~AsdrLogger() = default;
  AsdrLogger(const AsdrLogger&) = delete;
  AsdrLogger& operator=(const AsdrLogger&) = delete;

  void log_snapshot(const SatAggregator& aggregator,
                    SatAggregator::TimePoint now);

 private:
  std::string signature_alg_;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_ASDR_LOGGER_HPP_
