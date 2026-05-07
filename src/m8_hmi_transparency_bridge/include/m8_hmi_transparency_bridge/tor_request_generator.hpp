#ifndef MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
#define MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_

#include <string>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

class TorRequestGenerator final {
 public:
  TorRequestGenerator() = default;
  ~TorRequestGenerator() = default;
  TorRequestGenerator(const TorRequestGenerator&) = delete;
  TorRequestGenerator& operator=(const TorRequestGenerator&) = delete;

  [[nodiscard]] bool should_issue_tor(const SatAggregator& aggregator,
                                     const TorProtocol& tor,
                                     SatAggregator::TimePoint now) const;

  [[nodiscard]] std::string build_reason(const SatAggregator& aggregator,
                                         SatAggregator::TimePoint now) const;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
