#ifndef MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
#define MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_

#include <cstdint>
#include <string>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

enum class SatTriggerDecision : uint8_t {
  kNoTrigger = 0,
  kTriggerSat1 = 1,
  kTriggerSat2 = 2,
  kTriggerSat3 = 3,
};

struct AdaptiveSatTriggerConfig {
  double threat_confidence_threshold{0.7};
  double rule_confidence_threshold{0.8};
  double priority_high_tdl_s{30.0};
  double system_confidence_drop_threshold{0.6};
};

class AdaptiveSatTrigger final {
 public:
  explicit AdaptiveSatTrigger(const AdaptiveSatTriggerConfig& config = {});
  ~AdaptiveSatTrigger() = default;
  AdaptiveSatTrigger(const AdaptiveSatTrigger&) = delete;
  AdaptiveSatTrigger& operator=(const AdaptiveSatTrigger&) = delete;

  SatTriggerDecision evaluate(const SatAggregator& aggregator,
                              SatAggregator::TimePoint now) const;

 private:
  AdaptiveSatTriggerConfig config_;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
