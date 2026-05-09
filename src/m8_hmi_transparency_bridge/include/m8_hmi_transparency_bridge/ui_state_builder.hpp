// include/m8_hmi_transparency_bridge/ui_state_builder.hpp
#ifndef MASS_L3_M8_UI_STATE_BUILDER_HPP_
#define MASS_L3_M8_UI_STATE_BUILDER_HPP_

#include <optional>
#include <string>

#include "l3_msgs/msg/ui_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"
#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

/// Builds UIState message from internal state for 50 Hz publication.
class UiStateBuilder final {
 public:
  enum class Role : uint8_t { kRocOperator = 0, kShipCaptain = 1 };
  enum class Scenario : uint8_t {
    kTransit = 0,
    kColregAvoidance = 1,
    kMrcPreparation = 2,
    kMrcActive = 3,
    kOverrideActive = 4,
    kHandbackPreparation = 5
  };

  struct BuildContext {
    SatAggregator::TimePoint now;
    Role role;
    Scenario scenario;
    SatTriggerDecision sat_decision;
    std::optional<l3_msgs::msg::ODDState> odd;
    std::optional<l3_msgs::msg::WorldState> world;
    std::optional<l3_msgs::msg::BehaviorPlan> behavior;
    std::optional<l3_msgs::msg::COLREGsConstraint> colreg;
    std::optional<l3_msgs::msg::SafetyAlert> latest_alert;
    TorProtocol::State tor_state{TorProtocol::State::kIdle};
    double tor_remaining_s{0.0};
    bool override_active{false};
    bool m7_degradation_alert_active{false};
    std::string m7_degradation_alert_text{};
  };

  [[nodiscard]] l3_msgs::msg::UIState build(
      const BuildContext& ctx,
      const SatAggregator& sat_cache) const;

 private:
  /// Role × scenario filter — v1.1.2 §12.3 + detailed design Appendix A
  void apply_role_scenario_filter(
      Role role, Scenario scenario, l3_msgs::msg::UIState& msg) const;

  [[nodiscard]] static std::string scenario_to_string(Scenario s);
  [[nodiscard]] static std::string scenario_to_view_mode(Scenario s);
  [[nodiscard]] static std::string build_rationale(const BuildContext& ctx);
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_UI_STATE_BUILDER_HPP_
