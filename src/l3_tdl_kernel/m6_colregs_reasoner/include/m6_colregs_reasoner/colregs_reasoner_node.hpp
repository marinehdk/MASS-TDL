#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "std_msgs/msg/string.hpp"
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/colregs_chain_layer.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/rule_assessment.hpp"

#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"
#include "m6_colregs_reasoner/encounter_state_machine.hpp"
#include "m6_colregs_reasoner/rule_library_loader.hpp"
#include "m6_colregs_reasoner/rule_latch.hpp"
#include "m6_colregs_reasoner/target_state_cache.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class ColregsReasonerNode : public rclcpp::Node {
 public:
  ColregsReasonerNode();
  ~ColregsReasonerNode() override = default;
  ColregsReasonerNode(const ColregsReasonerNode&) = delete;
  ColregsReasonerNode& operator=(const ColregsReasonerNode&) = delete;

  /// Clear all cross-scenario encounter latches/history. Idempotent; safe at
  /// any time. Primary trigger is /sil/scenario_loaded; the retained sim-time
  /// rewind (in run_reasoning) calls the lock-held variant as a fallback.
  void reset_cross_run_state();

  static std::string odd_domain_str(OddDomain d);

  struct ColregsChainResult {
    std::vector<l3_msgs::msg::ColregsChainLayer> layers;
    std::string target_id;
  };

  static ColregsChainResult test_build_colregs_chain(
      const std::vector<RuleEvaluation>& evals, OddDomain domain,
      const RuleParameters& params,
      const std::vector<TargetGeometricState>& targets);

  static void test_populate_colregs_semantics(
      l3_msgs::msg::COLREGsConstraint& msg, EncounterState state,
      bool latch_released, bool release_predicted);
  static void test_populate_colregs_publish_semantics(
      l3_msgs::msg::COLREGsConstraint& msg, EncounterState fsm_state,
      bool actual_latch_released, bool release_predicted,
      bool resolved_bookkeeping);
  static void test_populate_release_ordering_semantics(
      l3_msgs::msg::COLREGsConstraint& msg);
  static void test_populate_projection_release_ordering_semantics(
      l3_msgs::msg::COLREGsConstraint& msg);

  // Test hooks for cross-run reset coverage. encounter_fsms_ is populated per
  // (target,rule) during run_reasoning (a timer callback not reachable from
  // unit tests); these expose size and a seeder so test_cross_run_reset can
  // verify the FSM map is cleared alongside the latch maps.
  std::size_t test_encounter_fsm_count();
  void test_seed_encounter_fsm(uint32_t mmsi, uint8_t rule_id);

  static ColregsChainResult build_colregs_chain(
      const std::vector<RuleEvaluation>& evals, OddDomain domain,
      const RuleParameters& params,
      const std::vector<TargetGeometricState>& targets);

 private:
  static void populate_colregs_semantics_(
      l3_msgs::msg::COLREGsConstraint& msg, EncounterState state,
      bool latch_released, bool release_predicted);
  static void populate_colregs_publish_semantics_(
      l3_msgs::msg::COLREGsConstraint& msg, EncounterState fsm_state,
      bool actual_latch_released, bool release_predicted,
      bool resolved_bookkeeping);

  void declare_parameters();
  void load_odd_thresholds();
  void create_components();
  void setup_publishers();
  void setup_subscribers();

  /// Lock-held cross-run reset. Assumes state_mutex_ is already held by the
  /// caller (run_reasoning's sim-time rewind path). The public
  /// reset_cross_run_state() takes the lock and delegates here.
  void reset_cross_run_state_locked_();
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
  void setup_timers();
  void setup_logger();

  // Conversion helpers
  std::vector<TargetGeometricState> convert_world_state(
      const l3_msgs::msg::WorldState& ws) const;
  RuleParameters get_current_rule_params() const;

  // Callbacks
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg);

  // Timer callbacks
  void run_reasoning();              // 2 Hz
  void check_health();               // 1 Hz
  void publish_asdr_snapshot();      // 2 Hz
  void publish_sat_data();           // 10 Hz

  // Helpers
  void publish_asdr_record(const std::string& type, const std::string& json);

  // Components
  std::unique_ptr<TargetStateCache> target_cache_;
  std::unique_ptr<PhaseClassifier> phase_classifier_;
  std::unique_ptr<ConstraintGenerator> constraint_gen_;
  rules::RuleSet rules_;

  // ODD-aware thresholds
  std::unordered_map<OddDomain, RuleParameters> odd_thresholds_;

  // Cached state
  std::optional<OddDomain> current_odd_;
  std::optional<l3_msgs::msg::WorldState> last_world_state_;
  rclcpp::Time last_world_stamp_;
  rclcpp::Time last_odd_stamp_;
  rclcpp::Time prev_world_stamp_{0, 0, RCL_ROS_TIME};

  std::unordered_map<uint32_t, double> prev_target_bearing_;
  std::unordered_map<uint32_t, double> prev_target_range_;
  // Per-(target,rule) onset-latched hysteresis. Key = mmsi<<8 | rule_id.
  std::unordered_map<uint64_t, RuleLatch> rule_latches_;
  // Per-(target,rule) encounter state machine (Spec 2026-06-17-fsm-design). The
  // FSM adds a TCPA/CPA/range gate so that a far target whose CPA projection is
  // ~0 but whose TCPA is still large cannot trigger an early onset->release
  // chatter when cpa_safe is at the design 1852 m. Key = mmsi<<8 | rule_id,
  // matching rule_latches_. The FSM's ACTIVE/MONITOR stickiness also holds the
  // encounter through a single-cycle release blip in the legacy RuleLatch.
  std::unordered_map<uint64_t, EncounterStateMachine> encounter_fsms_;
  // Per-target give-way DUTY latch (Rule 8(d)/13(d)/16). Key = mmsi. Generalizes
  // the per-rule 14/15 onset latch to whatever rule carries the give-way duty,
  // so own-ship's own avoiding action (which transiently opens CPA) cannot
  // retract the obligation mid-maneuver until the target is finally past & clear.
  std::unordered_map<uint32_t, RuleLatch> give_way_latches_;
  // Per-target stand-on IN-EXTREMIS latch (Rule 17(b)). Key = mmsi. Once own
  // ship — the stand-on vessel — is forced to take independent action against a
  // give-way vessel that failed to act, the action is held through the
  // close-quarters phase so the phase classifier cannot chatter it on/off.
  std::unordered_map<uint32_t, RuleLatch> standon_latches_;
  // Per-target encounter reference heading captured at duty onset. Release uses
  // this stable beam reference so own-ship's avoidance turn cannot make a target
  // appear "past and clear" before the vessels have actually passed.
  std::unordered_map<uint32_t, double> encounter_reference_heading_;
  std::unordered_set<uint32_t> resolved_targets_;

  // Mutex protecting shared state accessed from subscriber and timer callbacks
  mutable std::mutex state_mutex_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::COLREGsConstraint>::SharedPtr constraint_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;
  rclcpp::Publisher<l3_msgs::msg::RuleAssessment>::SharedPtr rule_assessment_pub_;

  // Subscribers
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr reasoning_timer_;
  rclcpp::TimerBase::SharedPtr health_timer_;
  rclcpp::TimerBase::SharedPtr asdr_timer_;
  rclcpp::TimerBase::SharedPtr sat_timer_;
};

}  // namespace mass_l3::m6_colregs
