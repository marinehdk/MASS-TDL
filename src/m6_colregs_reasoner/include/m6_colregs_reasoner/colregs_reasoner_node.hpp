#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"

#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"
#include "m6_colregs_reasoner/rule_library_loader.hpp"
#include "m6_colregs_reasoner/target_state_cache.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class ColregsReasonerNode : public rclcpp::Node {
 public:
  ColregsReasonerNode();
  ~ColregsReasonerNode() override = default;
  ColregsReasonerNode(const ColregsReasonerNode&) = delete;
  ColregsReasonerNode& operator=(const ColregsReasonerNode&) = delete;

 private:
  void declare_parameters();
  void load_odd_thresholds();
  void create_components();
  void setup_publishers();
  void setup_subscribers();
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

  // Mutex protecting shared state accessed from subscriber and timer callbacks
  mutable std::mutex state_mutex_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::COLREGsConstraint>::SharedPtr constraint_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;

  // Subscribers
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr reasoning_timer_;
  rclcpp::TimerBase::SharedPtr health_timer_;
  rclcpp::TimerBase::SharedPtr asdr_timer_;
  rclcpp::TimerBase::SharedPtr sat_timer_;
};

}  // namespace mass_l3::m6_colregs
