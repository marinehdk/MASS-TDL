#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"

#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"
#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"
#include "m1_odd_envelope_manager/odd_state_machine.hpp"
#include "m1_odd_envelope_manager/parameter_loader.hpp"
#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/// M1 ODD/Envelope Manager ROS2 node.
///
/// Serves as the scheduling authority for the L3 Tactical Decision Layer.
/// Owns and drives:
///   - OddStateMachine        (6-state FSM)
///   - ConformanceScoreCalculator  (E/T/H three-axis scoring)
///   - TmrTdlEstimator        (TMR/TDL pair computation)
///   - MrcTriggerLogic        (MRC selection)
///
/// PATH-S: noexcept on all callbacks, pre-allocated state, independent logger.
///         No exceptions, no dynamic allocation on control paths.
class OddEnvelopeManagerNode final : public rclcpp::Node {
 public:
  OddEnvelopeManagerNode();
  OddEnvelopeManagerNode(const OddEnvelopeManagerNode&) = delete;
  OddEnvelopeManagerNode& operator=(const OddEnvelopeManagerNode&) = delete;
  ~OddEnvelopeManagerNode() override = default;

 private:
  // ---------------------------------------------------------------------------
  // Initialization (not on control path; noexcept not required)
  // ---------------------------------------------------------------------------
  void initialize_parameters();
  void init_state_machine(const ParameterSet& p);
  void init_conformance_calc(const ParameterSet& p);
  void init_tmr_tdl(const ParameterSet& p);
  void init_mrc(const ParameterSet& p);
  void initialize_publishers();
  void initialize_subscribers();
  void initialize_timers();
  void initialize_logger();

  // ---------------------------------------------------------------------------
  // Subscriber callbacks (all noexcept per PATH-S)
  // ---------------------------------------------------------------------------
  void on_safety_alert(const l3_msgs::msg::SafetyAlert::SharedPtr msg) noexcept;
  void on_reflex_activation(
      const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr msg)
      noexcept;
  void on_override_signal(
      const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg) noexcept;
  void on_environment_state(
      const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) noexcept;
  void on_own_ship_state(
      const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) noexcept;
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg) noexcept;

  // ---------------------------------------------------------------------------
  // Timer callbacks (all noexcept per PATH-S)
  // ---------------------------------------------------------------------------
  void on_main_loop_tick() noexcept;            // 4 Hz
  void on_odd_state_publish_tick() noexcept;    // 1 Hz
  void on_asdr_record_periodic_tick() noexcept; // 0.5 Hz
  void on_sat_data_publish_tick() noexcept;     // 10 Hz

  // ---------------------------------------------------------------------------
  // Main-loop sub-helpers (all noexcept per PATH-S)
  // ---------------------------------------------------------------------------
  [[nodiscard]] ScoringInputs build_scoring_inputs() const noexcept;
  [[nodiscard]] SystemHealthSnapshot build_system_health(bool m7_critical) const noexcept;
  [[nodiscard]] double compute_min_tcpa() const noexcept;
  [[nodiscard]] EventFlags build_event_flags(
      const rclcpp::Time& now_ros,
      bool m7_critical,
      bool m7_mrc_required) const noexcept;
  void handle_state_change(
      EnvelopeState old_state,
      EnvelopeState new_state,
      const ScoreTriple& scores,
      const TmrTdlPair& tmrtdl) noexcept;
  void check_mrc_if_needed(
      EnvelopeState new_state,
      bool m7_mrc_required,
      MrcType m7_mrm,
      const ScoringInputs& scoring) noexcept;

  // ---------------------------------------------------------------------------
  // Publish helpers (all noexcept per PATH-S)
  // ---------------------------------------------------------------------------
  void publish_odd_state_event() noexcept;
  void publish_mode_cmd(std::string_view reason) noexcept;
  void publish_asdr_record(std::string_view decision_type,
                           std::string_view rationale_json) noexcept;
  void publish_sat_data() noexcept;
  void check_input_freshness(const rclcpp::Time& now) noexcept;

  // ---------------------------------------------------------------------------
  // Owned state
  // ---------------------------------------------------------------------------
  ParameterSet params_;
  std::unique_ptr<OddStateMachine> state_machine_;
  std::unique_ptr<ConformanceScoreCalculator> score_calc_;
  std::unique_ptr<TmrTdlEstimator> tmr_tdl_;
  std::unique_ptr<MrcTriggerLogic> mrc_;

  // Independent spdlog logger (per third-party-library-policy.md Sect. 3.1)
  std::shared_ptr<spdlog::logger> logger_;

  // Event callback group (reentrant for high-priority events)
  rclcpp::CallbackGroup::SharedPtr event_cbg_;

  // ---------------------------------------------------------------------------
  // Cached input snapshots (stored as SharedPtr for zero-copy)
  // ---------------------------------------------------------------------------
  l3_external_msgs::msg::EnvironmentState::SharedPtr last_env_state_;
  l3_external_msgs::msg::FilteredOwnShipState::SharedPtr last_own_ship_;
  l3_msgs::msg::WorldState::SharedPtr last_world_state_;
  l3_msgs::msg::SafetyAlert::SharedPtr last_safety_alert_;
  rclcpp::Time last_world_state_received_;
  rclcpp::Time last_env_state_received_;
  rclcpp::Time last_own_ship_received_;
  rclcpp::Time last_safety_alert_received_;

  // Input-received flags (since default rclcpp::Time{} uses different clock
  // type than now(), preventing reliable comparison against zero).
  bool has_received_world_state_;
  bool has_received_env_state_;
  bool has_received_own_ship_;
  bool has_received_safety_alert_;

  // Override / reflex state
  bool override_active_;
  bool reflex_active_;
  rclcpp::Time override_entry_at_;

  // ---------------------------------------------------------------------------
  // Cached computed values (updated on main loop tick)
  // ---------------------------------------------------------------------------
  ScoreTriple last_score_;
  TmrTdlPair last_tmr_tdl_;
  EnvelopeState last_published_state_;

  // Current ODD zone and auto level (defaults, updated externally)
  uint8_t current_zone_;
  uint8_t current_auto_level_;

  // ---------------------------------------------------------------------------
  // Publishers
  // ---------------------------------------------------------------------------
  rclcpp::Publisher<l3_msgs::msg::ODDState>::SharedPtr odd_state_pub_;
  rclcpp::Publisher<l3_msgs::msg::ModeCmd>::SharedPtr mode_cmd_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;

  // ---------------------------------------------------------------------------
  // Subscribers
  // ---------------------------------------------------------------------------
  rclcpp::Subscription<l3_msgs::msg::SafetyAlert>::SharedPtr safety_alert_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::ReflexActivationNotification>::SharedPtr
      reflex_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr
      override_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::EnvironmentState>::SharedPtr env_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr
      own_ship_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_state_sub_;

  // ---------------------------------------------------------------------------
  // Timers
  // ---------------------------------------------------------------------------
  rclcpp::TimerBase::SharedPtr main_loop_timer_;       // 4 Hz
  rclcpp::TimerBase::SharedPtr odd_publish_timer_;     // 1 Hz
  rclcpp::TimerBase::SharedPtr asdr_periodic_timer_;   // 0.5 Hz
  rclcpp::TimerBase::SharedPtr sat_timer_;              // 10 Hz
};

}  // namespace mass_l3::m1
