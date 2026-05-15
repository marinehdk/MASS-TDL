#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

namespace mass_l3::m4 {

using namespace std::chrono_literals;

BehaviorArbiterNode::BehaviorArbiterNode(const rclcpp::NodeOptions& options)
    : Node("behavior_arbiter", options),
      heading_domain_(declare_parameter<double>(
          "m4.arbitration.heading_domain_resolution_deg", 1.0)),
      speed_domain_(
          declare_parameter<double>(
              "m4.arbitration.speed_domain_min_kn", 0.0),
          declare_parameter<double>(
              "m4.arbitration.speed_domain_max_kn", 22.0),
          declare_parameter<double>(
              "m4.arbitration.speed_domain_resolution_kn", 0.5)) {
  const std::string config_dir =
      declare_parameter<std::string>("config_dir", "");

  const auto qos_profile = rclcpp::QoS(rclcpp::KeepLast(1))
                               .reliable()
                               .durability_volatile();

  sub_odd_ = create_subscription<ODDStateMsg>(
      "/l3/m1/odd_state", qos_profile,
      [this](const ODDStateMsg::SharedPtr msg) { on_odd_state(msg); });

  sub_world_ = create_subscription<WorldStateMsg>(
      "/l3/m2/world_state", qos_profile,
      [this](const WorldStateMsg::SharedPtr msg) { on_world_state(msg); });

  sub_mode_ = create_subscription<ModeCmdMsg>(
      "/l3/m1/mode_cmd", qos_profile,
      [this](const ModeCmdMsg::SharedPtr msg) { on_mode_cmd(msg); });

  sub_mission_ = create_subscription<MissionGoalMsg>(
      "/l3/m3/mission_goal", qos_profile,
      [this](const MissionGoalMsg::SharedPtr msg) { on_mission_goal(msg); });

  sub_colregs_ = create_subscription<COLREGsConstraintMsg>(
      "/l3/m6/colregs_constraint", qos_profile,
      [this](const COLREGsConstraintMsg::SharedPtr msg) {
        on_colregs_constraint(msg);
      });

  pub_plan_ = create_publisher<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan",
      rclcpp::QoS(rclcpp::KeepLast(1)).reliable());

  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record",
      rclcpp::QoS(rclcpp::KeepLast(10)).reliable());

  const auto interval_ms =
      declare_parameter<int>("m4.arbitration.interval_ms", 500);
  timer_ = create_wall_timer(
      std::chrono::milliseconds(interval_ms),
      [this]() { arbitration_timer_callback(); });

  if (!config_dir.empty()) {
    const std::string dict_path = config_dir + "/behavior_definitions.yaml";
    if (!dictionary_.load(dict_path)) {
      RCLCPP_WARN(get_logger(),
                  "Failed to load behavior definitions from %s",
                  dict_path.c_str());
    }
  }
}

void BehaviorArbiterNode::on_odd_state(const ODDStateMsg::SharedPtr msg) {
  latest_odd_ = msg;
  odd_received_ = true;
}

void BehaviorArbiterNode::on_world_state(const WorldStateMsg::SharedPtr msg) {
  latest_world_ = msg;
  world_received_ = true;
}

void BehaviorArbiterNode::on_mode_cmd(const ModeCmdMsg::SharedPtr msg) {
  latest_mode_ = msg;
  mode_received_ = true;
}

void BehaviorArbiterNode::on_mission_goal(const MissionGoalMsg::SharedPtr msg) {
  latest_mission_ = msg;
  mission_received_ = true;
}

void BehaviorArbiterNode::on_colregs_constraint(
    const COLREGsConstraintMsg::SharedPtr msg) {
  latest_colregs_ = msg;
  colregs_received_ = true;
}

void BehaviorArbiterNode::arbitration_timer_callback() {
  if (!odd_received_ || !world_received_) {
    return;
  }

  ArbitrationInputs inputs;
  inputs.odd_state = *latest_odd_;
  inputs.world_state = *latest_world_;
  if (mode_received_)  inputs.mode_cmd = *latest_mode_;
  if (mission_received_) inputs.mission_goal = *latest_mission_;
  if (colregs_received_) inputs.colregs_constraint = *latest_colregs_;

  const auto active_set = BehaviorActivationCondition::compute_active_set(
      inputs, dictionary_);

  if (active_set.empty()) {
    RCLCPP_WARN(get_logger(), "No active behaviours — skipping arbitration");
    return;
  }

  const auto primary = BehaviorPriority::select_primary(active_set);

  BehaviorPlanMsg plan;
  plan.schema_version = "v1.1.2";
  plan.stamp = now();
  plan.behavior = static_cast<uint8_t>(primary.type);
  plan.heading_min_deg = 0.0f;
  plan.heading_max_deg = 360.0f;
  plan.speed_min_kn = static_cast<float>(speed_domain_.min_kn());
  plan.speed_max_kn = static_cast<float>(speed_domain_.max_kn());
  plan.confidence = 1.0f;
  plan.rationale = "Arbitration: " + primary.name;

  pub_plan_->publish(plan);

  l3_msgs::msg::ASDRRecord record;
  record.schema_version = "v1.1.2";
  record.stamp = now();
  record.source_module = "M4_Behavior_Arbiter";
  record.decision_type = "behavior_plan";
  record.decision_json =
      "{\"behavior_type\":" + std::to_string(static_cast<int>(primary.type)) +
      ",\"behavior_name\":\"" + primary.name +
      "\",\"active_count\":" + std::to_string(active_set.size()) +
      ",\"has_mrc\":" + (BehaviorPriority::has_mrc(active_set) ? "true" : "false") +
      "}";
  pub_asdr_->publish(record);
}

}  // namespace mass_l3::m4
