#ifndef MASS_L3_M8_UI_STATE_BUILDER_HPP_
#define MASS_L3_M8_UI_STATE_BUILDER_HPP_

#include <string>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

struct UiState {
  std::string sat1_summary{};
  std::string sat2_trigger_reason{};
  std::string sat3_predicted_state{};
  float sat2_system_confidence{0.0F};
  float sat3_prediction_uncertainty{0.0F};
  float sat3_tdl_s{0.0F};
  float sat3_tmr_s{0.0F};
  TorProtocol::State tor_state{TorProtocol::State::kIdle};
};

class UiStateBuilder final {
 public:
  UiStateBuilder() = default;
  ~UiStateBuilder() = default;
  UiStateBuilder(const UiStateBuilder&) = delete;
  UiStateBuilder& operator=(const UiStateBuilder&) = delete;

  [[nodiscard]] UiState build(const SatAggregator& aggregator,
                              const TorProtocol& tor,
                              SatAggregator::TimePoint now) const;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_UI_STATE_BUILDER_HPP_
