#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"

namespace mass_l3::m8 {

UiState UiStateBuilder::build(const SatAggregator& /*aggregator*/,
                               const TorProtocol& tor,
                               SatAggregator::TimePoint /*now*/) const
{
  UiState state{};
  state.tor_state = tor.state();
  return state;
}

}  // namespace mass_l3::m8
