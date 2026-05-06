#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"

#include "m2_world_model/world_state_aggregator.hpp"
#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/enc_loader.hpp"
#include "m2_world_model/view_health_monitor.hpp"
#include "m2_world_model/coord_transform.hpp"
#include "m2_world_model/parameter_loader.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

class WorldModelNode final : public rclcpp::Node {
 public:
  explicit WorldModelNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ~WorldModelNode() override = default;
  WorldModelNode(const WorldModelNode&) = delete;
  WorldModelNode& operator=(const WorldModelNode&) = delete;

 private:
  // Initialization
  void load_parameters();
  void create_components();
  void setup_subscribers();
  void setup_publishers();
  void setup_timers();
  void setup_logger();

  // Subscriber callbacks
  void on_tracked_targets(const l3_external_msgs::msg::TrackedTargetArray::SharedPtr msg);
  void on_own_ship_state(const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg);
  void on_environment_state(const l3_external_msgs::msg::EnvironmentState::SharedPtr msg);
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);

  // Timer callbacks
  void on_aggregation_timer();     // 4 Hz
  void on_sat_timer();             // 10 Hz
  void on_asdr_periodic_timer();   // 2 Hz
  void on_heartbeat_timer();       // 1 Hz

  // Internal helpers
  void publish_world_state();
  void publish_sat_data();
  void publish_asdr_record(const std::string& decision_type,
                           const std::string& rationale_json);

  // ── Parameters struct ──
  struct Parameters {
    struct {
      double aggregation_rate_hz;
      double sat_rate_hz;
      double asdr_periodic_rate_hz;
      double heartbeat_rate_hz;
    } rates;
    struct {
      int32_t max_targets;
      int32_t target_disappearance_periods;
      double environment_cache_ttl_s;
    } buffer;
    struct {
      double cpa_safe_a_m, cpa_safe_b_m, cpa_safe_c_m, cpa_safe_d_m;
      double tcpa_safe_a_s, tcpa_safe_b_s, tcpa_safe_c_s, tcpa_safe_d_s;
    } cpa;
    struct {
      double overtaking_bearing_min_deg;
      double overtaking_bearing_max_deg;
      double head_on_heading_diff_tol_deg;
      double safe_pass_speed_threshold_mps;
      double safe_pass_min_cpa_m;
    } colreg;
    struct {
      int32_t dv_loss_periods_to_degraded;
      int32_t dv_loss_periods_to_critical;
      double dv_degraded_to_critical_timeout_s;
      double ev_loss_ms_to_critical;
      double sv_loss_s_to_degraded;
      double confidence_floor_dv_degraded;
    } health;
  };

  Parameters params_;

  // Core components
  std::shared_ptr<CoordTransform> coord_transform_;
  std::shared_ptr<CpaTcpaCalculator> cpa_calc_;
  std::shared_ptr<EncounterClassifier> classifier_;
  std::shared_ptr<TrackBuffer> track_buffer_;
  std::shared_ptr<EncLoader> enc_loader_;
  std::shared_ptr<ViewHealthMonitor> health_;
  std::shared_ptr<WorldStateAggregator> aggregator_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Callback groups
  rclcpp::CallbackGroup::SharedPtr ego_cbg_;     // reentrant (50 Hz EV)
  rclcpp::CallbackGroup::SharedPtr dv_sv_cbg_;   // mutex (2 Hz DV + 0.2 Hz SV + 1 Hz ODD)

  // Subscribers
  rclcpp::Subscription<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr targets_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr own_ship_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::EnvironmentState>::SharedPtr env_sub_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_state_sub_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::WorldState>::SharedPtr world_state_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;

  // Timers
  rclcpp::TimerBase::SharedPtr aggregation_timer_;
  rclcpp::TimerBase::SharedPtr sat_timer_;
  rclcpp::TimerBase::SharedPtr asdr_periodic_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

}  // namespace mass_l3::m2
