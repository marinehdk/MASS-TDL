#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

#include <chrono>
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

using namespace std::chrono_literals;

BehaviorArbiterNode::BehaviorArbiterNode(const rclcpp::NodeOptions& options)
    : Node("behavior_arbiter", options) {
  const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
  const auto asdr_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

  interval_ms_ = declare_parameter<int>("m4.arbitration.interval_ms", 250);
  double h_res = declare_parameter<double>("m4.arbitration.heading_domain_resolution_deg", 1.0);
  double s_min = declare_parameter<double>("m4.arbitration.speed_domain_min_kn", 0.0);
  speed_max_kn_ = declare_parameter<double>("m4.arbitration.speed_domain_max_kn", 22.0);
  double s_res = declare_parameter<double>("m4.arbitration.speed_domain_resolution_kn", 0.5);
  int ivp_to   = declare_parameter<int>("m4.arbitration.ivp_timeout_ms", 50);

  IvPHeadingDomain hd(h_res);
  IvPSpeedDomain   sd(s_min, speed_max_kn_, s_res);
  solver_ = std::make_unique<IvPSolver>(hd, sd,
      std::make_unique<WeightedSumCombination>(),
      std::chrono::milliseconds(ivp_to));

  sub_odd_ = create_subscription<ODDStateMsg>(
      "/l3/m1/odd_state", qos,
      [this](const ODDStateMsg::SharedPtr msg) { on_odd_state(msg); });
  sub_world_ = create_subscription<WorldStateMsg>(
      "/l3/m2/world_state", qos,
      [this](const WorldStateMsg::SharedPtr msg) { on_world_state(msg); });
  sub_mode_ = create_subscription<ModeCmdMsg>(
      "/l3/m1/mode_cmd", qos,
      [this](const ModeCmdMsg::SharedPtr msg) { on_mode_cmd(msg); });
  sub_mission_ = create_subscription<MissionGoalMsg>(
      "/l3/m3/mission_goal", qos,
      [this](const MissionGoalMsg::SharedPtr msg) { on_mission_goal(msg); });
  sub_colregs_ = create_subscription<COLREGsConstraintMsg>(
      "/l3/m6/colregs_constraint", qos,
      [this](const COLREGsConstraintMsg::SharedPtr msg) { on_colregs_constraint(msg); });

  pub_plan_ = create_publisher<BehaviorPlanMsg>("/l3/m4/behavior_plan", qos);
  pub_sat2_ = create_publisher<l3_msgs::msg::SAT2Data>("/sil/sat2_data", qos);
  pub_asdr_ = create_publisher<ASDRRecordMsg>("/l3/asdr/record", asdr_qos);

  timer_ = create_wall_timer(std::chrono::milliseconds(interval_ms_),
                             [this]() { arbitration_timer_callback(); });

  std::string config_dir = declare_parameter<std::string>("m4.config_dir", "");
  if (!config_dir.empty()) {
    dictionary_.load(config_dir + "/behavior_definitions.yaml");
  }
  if (dictionary_.size() == 0) {
    RCLCPP_WARN(get_logger(), "[M4] Behavior dictionary empty; adding minimal transit rule");
    BehaviorDescriptor transit_rule;
    transit_rule.type = BehaviorType::TRANSIT;
    transit_rule.name = "TRANSIT";
    transit_rule.priority_weight = 1.0;
    transit_rule.activation_rule = "always";
    transit_rule.ivp_function_type = "uniform";
    dictionary_.add_behavior(transit_rule);
  }
}

void BehaviorArbiterNode::on_odd_state(const ODDStateMsg::SharedPtr msg) {
  latest_odd_ = msg; odd_received_ = true;
}
void BehaviorArbiterNode::on_world_state(const WorldStateMsg::SharedPtr msg) {
  latest_world_ = msg; world_received_ = true;
}
void BehaviorArbiterNode::on_mode_cmd(const ModeCmdMsg::SharedPtr msg) {
  latest_mode_ = msg; mode_received_ = true;
}
void BehaviorArbiterNode::on_mission_goal(const MissionGoalMsg::SharedPtr msg) {
  latest_mission_ = msg; mission_received_ = true;
}
void BehaviorArbiterNode::on_colregs_constraint(const COLREGsConstraintMsg::SharedPtr msg) {
  latest_colregs_ = msg; colregs_received_ = true;
}

ArbitrationInputs BehaviorArbiterNode::build_inputs() const {
  ArbitrationInputs in;
  auto now = this->now();

  if (odd_received_ && latest_odd_) {
    in.odd_zone = latest_odd_->current_zone;
    in.odd_received = true;
    in.age_odd_ms = (now - latest_odd_->stamp).nanoseconds() / 1'000'000LL;
  }
  if (world_received_ && latest_world_) {
    in.world_received = true;
    // visibility_nm removed from WorldState (v1.1.2); keep default 999.0 (good vis)
    in.own_speed_kn = latest_world_->own_ship.sog_kn;
    in.age_world_ms = (now - latest_world_->stamp).nanoseconds() / 1'000'000LL;
  }
  if (mode_received_ && latest_mode_) {
    in.mode_received = true;
    in.mode_mrc_triggered = (latest_mode_->mode == ModeCmdMsg::MODE_EMERGENCY);
  }
  if (mission_received_ && latest_mission_) {
    in.mission_received = true;
    // target_heading_deg removed from MissionGoal (v1.2.0); keep default 0.0
    in.age_mission_ms = (now - latest_mission_->stamp).nanoseconds() / 1'000'000LL;
  }
  if (colregs_received_ && latest_colregs_) {
    in.colregs_received = true;
    in.colregs_conflict_detected = latest_colregs_->conflict_detected;
    in.age_colregs_ms = (now - latest_colregs_->stamp).nanoseconds() / 1'000'000LL;
  }

  return in;
}

void BehaviorArbiterNode::arbitration_timer_callback() {
  ArbitrationInputs inputs = build_inputs();
  HealthState health = BehaviorActivationCondition::compute_health_state(inputs);

  // Standby: no critical inputs received
  if (!odd_received_ || !world_received_) {
    BehaviorPlanMsg plan;
    plan.schema_version = 113;
    plan.stamp = now();
    plan.behavior = BehaviorPlanMsg::BEHAVIOR_TRANSIT;
    plan.heading_min_deg = 0.0f;
    plan.heading_max_deg = 360.0f;
    plan.speed_min_kn = 0.0f;
    plan.speed_max_kn = static_cast<float>(speed_max_kn_);
    plan.confidence = 0.0f;
    plan.rationale = "Standby: waiting for inputs";
    pub_plan_->publish(plan);
    return;
  }

  // R3 fix: mission state precondition check.
  // If M3 mission_goal is absent (L2 silent) and world has no conflict targets,
  // output failsafe TRANSIT plan instead of entering IvP with empty active_set.
  bool mission_available = mission_received_ && latest_mission_;
  bool has_conflict = (colregs_received_ && latest_colregs_
                       && latest_colregs_->conflict_detected);
  if (!mission_available && !has_conflict) {
    BehaviorPlanMsg plan;
    plan.schema_version = 113;
    plan.stamp = now();
    plan.behavior = BehaviorPlanMsg::BEHAVIOR_TRANSIT;
    plan.heading_min_deg = -5.0f;
    plan.heading_max_deg = 5.0f;
    plan.speed_min_kn = 0.0f;
    plan.speed_max_kn = static_cast<float>(speed_max_kn_);
    plan.confidence = 0.85f;
    plan.rationale = "Failsafe TRANSIT (no L2 input, no conflict)";
    pub_plan_->publish(plan);
    return;
  }

  // Step 3: Behavior activation
  auto active_set = BehaviorActivationCondition::compute_active_set(inputs, dictionary_);

  // Step 4: Check for MRC override
  bool has_mrc = BehaviorPriority::has_mrc(active_set);

  BehaviorType primary = BehaviorType::MRC_DRIFT;
  double h_min = 0.0, h_max = 360.0, s_min = 0.0, s_max = speed_max_kn_;
  double confidence = 0.95;
  std::string rationale;

  if (has_mrc) {
    primary = BehaviorType::MRC_DRIFT;
    h_min = 0.0; h_max = 360.0;
    s_min = 0.0; s_max = 0.0;
    confidence = 1.0;
    rationale = "MRC_DRIFT override: emergency drift active";
  } else if (!active_set.empty()) {
    // Build weighted functions (stub for D3.1 E1)
    std::vector<IvPCombinationStrategy::WeightedFunction> weighted_fns;

    IvPHardConstraints constraints;
    constraints.speed_min_kn = 0.0;
    constraints.speed_max_kn = speed_max_kn_;

    // Inject M6 COLREGs heading constraints.
    // Constraint.msg (v1.1.2): constraint_type=="colregs", unit=="deg",
    // numeric_value = minimum heading deviation from own heading (positive = starboard).
    if (colregs_received_ && latest_colregs_ && latest_colregs_->conflict_detected) {
      double own_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
      for (const auto& c : latest_colregs_->constraints) {
        if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
          constraints.heading_allowed_ranges_deg.push_back(
              {own_hdg + c.numeric_value, own_hdg + 180.0});
        }
      }
    }

    auto sol = solver_->solve_with_fallback(weighted_fns, constraints);

    // Select primary behavior
    BehaviorPriority priority;
    IvPSolution ivp_default{};
    ArbitrationInputs pri_inputs = inputs;
    primary = priority.select_primary(active_set, ivp_default, pri_inputs);

    if (sol.has_value()) {
      h_min = sol->heading_min_deg;
      h_max = sol->heading_max_deg;
      s_min = sol->speed_min_kn;
      s_max = sol->speed_max_kn;
      confidence = sol->relax_level > 0 ? 0.75 : 0.95;
      rationale = sol->rationale;
    } else {
      // R3 fix: Conservative fallback — use configured speed domain max
      // instead of 0.5 * current SOG to break the cascade slowdown loop.
      double own_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
      h_min = fmod(own_hdg - 90.0 + 360.0, 360.0);
      h_max = fmod(own_hdg + 90.0, 360.0);
      s_max = speed_max_kn_;
      confidence = 0.30;
      rationale = "IvP infeasible fallback";

      // Log infeasibility
      if (colregs_received_ && latest_colregs_ && latest_colregs_->conflict_detected) {
        std::ostringstream json;
        json << "{\"constraint_count\":" << latest_colregs_->constraints.size()
             << ",\"fallback_used\":\"cascading\"}";
        publish_asdr_event("ivp_infeasible", json.str());
      }
    }
  } else {
    primary = BehaviorType::TRANSIT;
    confidence = 0.60;
    rationale = "No active behaviors; default Transit";
  }

  // --- Publish Behavior_PlanMsg ---
  BehaviorPlanMsg plan;
  plan.schema_version = 113;
  plan.stamp = now();
  plan.behavior = static_cast<uint8_t>(primary);
  plan.heading_min_deg = static_cast<float>(h_min);
  plan.heading_max_deg = static_cast<float>(h_max);
  plan.speed_min_kn = static_cast<float>(s_min);
  plan.speed_max_kn = static_cast<float>(s_max);
  plan.confidence = static_cast<float>(confidence);
  plan.rationale = rationale;
  pub_plan_->publish(plan);

  // --- Publish ivp_contributions (SAT-2) ---
  l3_msgs::msg::SAT2Data sat2_msg;
  sat2_msg.schema_version = 113;
  sat2_msg.stamp = now();
  sat2_msg.confidence = static_cast<float>(confidence);
  sat2_msg.rationale = rationale;
  sat2_msg.trigger_reason = "periodic_arbitration";
  sat2_msg.reasoning_chain = "M4_Behavior_Arbiter";
  sat2_msg.system_confidence = static_cast<float>(confidence);

  if (colregs_received_ && latest_colregs_) {
    sat2_msg.colregs_chain = latest_colregs_->colregs_chain;
    sat2_msg.colregs_chain_target_id = latest_colregs_->colregs_chain_target_id;
  }

  auto contributions = compute_ivp_contributions(primary, h_min, h_max, confidence);
  for (size_t i = 0; i < 6; ++i) {
    if (i < contributions.size()) {
      sat2_msg.ivp_contributions[i] = contributions[i].utility_value;
      sat2_msg.ivp_labels[i] = std::to_string(contributions[i].direction_deg);
    } else {
      sat2_msg.ivp_contributions[i] = 0.0f;
      sat2_msg.ivp_labels[i] = "0.0";
    }
  }
  pub_sat2_->publish(sat2_msg);

  // --- ASDR Events ---
  if (primary != prev_primary_) {
    std::ostringstream json;
    json << "{\"from\":" << static_cast<int>(prev_primary_)
         << ",\"to\":" << static_cast<int>(primary)
         << ",\"active_count\":" << active_set.size()
         << ",\"odd_zone\":" << static_cast<int>(inputs.odd_zone) << "}";
    publish_asdr_event("behavior_switch", json.str());
  }

  if (prev_odd_zone_ != 99 && inputs.odd_zone != prev_odd_zone_) {
    std::ostringstream json;
    json << "{\"from_zone\":" << static_cast<int>(prev_odd_zone_)
         << ",\"to_zone\":" << static_cast<int>(inputs.odd_zone) << "}";
    publish_asdr_event("odd_transition", json.str());
  }

  // Input timeout ASDR
  auto now_ts = this->now();
  if (odd_received_ && (now_ts - latest_odd_->stamp).nanoseconds() / 1'000'000LL > 2000) {
    publish_asdr_event("input_timeout",
        "{\"source_module\":\"M1\",\"age_ms\":" +
        std::to_string((now_ts - latest_odd_->stamp).nanoseconds() / 1'000'000LL) + "}");
  }

  prev_primary_ = primary;
  prev_odd_zone_ = inputs.odd_zone;
  prev_health_ = health;
}

std::vector<IvpContributionInternal> BehaviorArbiterNode::compute_ivp_contributions(
    BehaviorType primary, double h_min, double h_max, double confidence) const {
  static const double kDirections[8] = {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0};
  std::vector<IvpContributionInternal> contributions;
  contributions.reserve(8);

  for (int i = 0; i < 8; ++i) {
    double dir = kDirections[i];
    float utility = 0.3f;
    if (h_min <= h_max) {
      if (dir >= h_min && dir <= h_max) utility = 1.0f;
    } else {
      if (dir >= h_min || dir <= h_max) utility = 1.0f;
    }
    utility *= static_cast<float>(confidence);

    std::string behavior_id;
    switch (primary) {
      case BehaviorType::TRANSIT:      behavior_id = "Transit"; break;
      case BehaviorType::COLREG_AVOID: behavior_id = "ColregAvoid"; break;
      case BehaviorType::DP_HOLD:      behavior_id = "DpHold"; break;
      case BehaviorType::BERTH:        behavior_id = "Berth"; break;
      case BehaviorType::MRC_DRIFT:    behavior_id = "MrcDrift"; break;
      case BehaviorType::MRC_ANCHOR:   behavior_id = "MrcAnchor"; break;
      case BehaviorType::MRC_HEAVE_TO: behavior_id = "MrcHeaveTo"; break;
      default:                         behavior_id = "Unknown"; break;
    }

    contributions.push_back({static_cast<float>(dir), utility, behavior_id});
  }

  return contributions;
}

void BehaviorArbiterNode::publish_asdr_event(
    const std::string& decision_type, const std::string& decision_json) {
  ASDRRecordMsg record;
  record.schema_version = 113;
  record.stamp = now();
  record.source_module = "M4_Behavior_Arbiter";
  record.decision_type = decision_type;
  record.decision_json = decision_json;
  pub_asdr_->publish(record);
}

}  // namespace mass_l3::m4
