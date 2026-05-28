#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

#include <chrono>
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

using namespace std::chrono_literals;

static double compute_bearing_deg(double own_lat, double own_lon,
                                  double tgt_lat, double tgt_lon) {
  const double deg2rad = M_PI / 180.0;
  const double rad2deg = 180.0 / M_PI;
  const double lat1 = own_lat * deg2rad;
  const double lat2 = tgt_lat * deg2rad;
  const double dlon = (tgt_lon - own_lon) * deg2rad;

  const double y = std::sin(dlon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  double bearing_rad = std::atan2(y, x);
  double bearing_deg = bearing_rad * rad2deg;
  if (bearing_deg < 0.0) {
    bearing_deg += 360.0;
  }
  return bearing_deg;
}

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
  sub_rule_assessment_ = create_subscription<l3_msgs::msg::RuleAssessment>(
      "/l3/m6/rule_assessment", qos,
      [this](const l3_msgs::msg::RuleAssessment::SharedPtr msg) { on_rule_assessment(msg); });

  pub_plan_ = create_publisher<BehaviorPlanMsg>("/l3/m4/behavior_plan", qos);
  pub_sat2_ = create_publisher<l3_msgs::msg::SAT2Data>("/sil/sat2_data", qos);
  pub_asdr_ = create_publisher<ASDRRecordMsg>("/l3/asdr/record", asdr_qos);
  concern_pub_ = create_publisher<l3_msgs::msg::SafetyConcernEvent>(
      "/l3/safety/concern", rclcpp::QoS(10).reliable());

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
void BehaviorArbiterNode::on_rule_assessment(const l3_msgs::msg::RuleAssessment::SharedPtr msg) {
  latest_rule_assessment_ = msg;
  if (msg->applicable_rule == "Rule 14") {
    colreg_avoidance_weight_ = 0.85f;  // Boost from default
    dictionary_.set_priority_weight(BehaviorType::COLREG_AVOID, 0.85);
    RCLCPP_WARN(get_logger(), "[M4] Rule 14 detected, boosting COLREG_AVOIDANCE weight to 0.85");
  } else {
    colreg_avoidance_weight_ = 0.7f;   // Default from YAML
    dictionary_.set_priority_weight(BehaviorType::COLREG_AVOID, 0.70);
  }
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
  bool m3_task_valid = mission_received_ && latest_mission_ &&
      (latest_mission_->fsm_state == l3_msgs::msg::MissionGoal::FSM_ACTIVE) &&
      (latest_mission_->task_validity == l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID);

  if (!m3_active_latch_ && m3_task_valid) {
    m3_active_latch_ = true;
    RCLCPP_INFO(get_logger(), "[M4] M3 first ACTIVE+VALID: enabling IvP + snapshot guard");
  }

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

    // Define a local helper lambda to wrap headings to [0, 360) range
    auto wrap_hdg = [](double hdg) {
      double w = std::fmod(hdg, 360.0);
      return w < 0.0 ? w + 360.0 : w;
    };

    // ── 1. TRANSIT BEHAVIOR IvP FUNCTION ───────────────────────
    double nominal_hdg = latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;
    if (latest_mission_ && latest_world_ &&
        (std::abs(latest_mission_->current_target_wp.latitude) > 1e-4 ||
         std::abs(latest_mission_->current_target_wp.longitude) > 1e-4)) {
      double own_lat = latest_world_->own_ship.position.latitude;
      double own_lon = latest_world_->own_ship.position.longitude;
      double tgt_lat = latest_mission_->current_target_wp.latitude;
      double tgt_lon = latest_mission_->current_target_wp.longitude;
      nominal_hdg = compute_bearing_deg(own_lat, own_lon, tgt_lat, tgt_lon);
    }
    double nominal_spd = speed_max_kn_; // Target nominal speed

    IvPFunctionDefault transit_fn;
    std::vector<IvPFunctionDefault::Piece> transit_pieces;

    // 1a. Peak Optimal Piece (1.0 utility): Heading error <= 2.5 deg, Speed error <= 0.5 kn
    // This provides a high-precision restoring force pulling the own ship back to target bearing
    IvPFunctionDefault::Piece peak_tp;
    peak_tp.heading_min_deg = wrap_hdg(nominal_hdg - 2.5);
    peak_tp.heading_max_deg = wrap_hdg(nominal_hdg + 2.5);
    peak_tp.speed_min_kn = std::max(0.0, nominal_spd - 0.5);
    peak_tp.speed_max_kn = nominal_spd;
    peak_tp.utility = 1.0;
    transit_pieces.push_back(peak_tp);

    // 1b. Near Optimal Piece (0.85 utility): Heading error <= 8.0 deg, Speed error <= 1.0 kn
    IvPFunctionDefault::Piece near_tp;
    near_tp.heading_min_deg = wrap_hdg(nominal_hdg - 8.0);
    near_tp.heading_max_deg = wrap_hdg(nominal_hdg + 8.0);
    near_tp.speed_min_kn = std::max(0.0, nominal_spd - 1.0);
    near_tp.speed_max_kn = nominal_spd;
    near_tp.utility = 0.85;
    transit_pieces.push_back(near_tp);

    // 1c. Moderate Transit Piece (0.6 utility): Heading error <= 20.0 deg
    IvPFunctionDefault::Piece mod_tp;
    mod_tp.heading_min_deg = wrap_hdg(nominal_hdg - 20.0);
    mod_tp.heading_max_deg = wrap_hdg(nominal_hdg + 20.0);
    mod_tp.speed_min_kn = 0.0;
    mod_tp.speed_max_kn = nominal_spd;
    mod_tp.utility = 0.6;
    transit_pieces.push_back(mod_tp);

    // 1d. Acceptable Piece (0.3 utility): Heading error <= 45.0 deg
    IvPFunctionDefault::Piece acc_tp;
    acc_tp.heading_min_deg = wrap_hdg(nominal_hdg - 45.0);
    acc_tp.heading_max_deg = wrap_hdg(nominal_hdg + 45.0);
    acc_tp.speed_min_kn = 0.0;
    acc_tp.speed_max_kn = nominal_spd;
    acc_tp.utility = 0.3;
    transit_pieces.push_back(acc_tp);

    // 1e. Base Failsafe Piece (0.1 utility): 全向低保底基面
    IvPFunctionDefault::Piece base_tp;
    base_tp.heading_min_deg = 0.0;
    base_tp.heading_max_deg = 359.9;
    base_tp.speed_min_kn = 0.0;
    base_tp.speed_max_kn = speed_max_kn_;
    base_tp.utility = 0.1;
    transit_pieces.push_back(base_tp);

    transit_fn.set_pieces(transit_pieces);
    weighted_fns.push_back({1.0, transit_fn}); // Weight: 1.0

    // ── 2. COLREGs AVOIDANCE BEHAVIOR IvP FUNCTION ──────────────
    if (colregs_received_ && latest_colregs_ && latest_colregs_->conflict_detected) {
      double colregs_dev = 0.0;

      for (const auto& c : latest_colregs_->constraints) {
        if (c.constraint_type == "colregs" && c.unit == "deg") {
          colregs_dev = std::max(colregs_dev, c.numeric_value);
        }
      }

      if (colregs_dev > 0.0) {
        IvPFunctionDefault avoid_fn;
        std::vector<IvPFunctionDefault::Piece> avoid_pieces;

        // 2a. Penalty Zone (0.05 utility): [nominal_hdg - 180, nominal_hdg + colregs_dev)
        // Severely penalizes port turns or insufficient starboard turns
        IvPFunctionDefault::Piece penalty_ap;
        penalty_ap.heading_min_deg = wrap_hdg(nominal_hdg - 180.0);
        penalty_ap.heading_max_deg = wrap_hdg(nominal_hdg + colregs_dev);
        penalty_ap.speed_min_kn = 0.0;
        penalty_ap.speed_max_kn = speed_max_kn_;
        penalty_ap.utility = 0.05;
        avoid_pieces.push_back(penalty_ap);

        // 2b. Comfort Avoidance Zone (1.0 utility): [nominal_hdg + colregs_dev, nominal_hdg + 60.0]
        IvPFunctionDefault::Piece optimal_ap;
        optimal_ap.heading_min_deg = wrap_hdg(nominal_hdg + colregs_dev);
        optimal_ap.heading_max_deg = wrap_hdg(nominal_hdg + 60.0);
        optimal_ap.speed_min_kn = 0.0;
        optimal_ap.speed_max_kn = speed_max_kn_;
        optimal_ap.utility = 1.0;
        avoid_pieces.push_back(optimal_ap);

        // 2c. Sub-Optimal Transition Zone (0.6 utility): [nominal_hdg + 60.0, nominal_hdg + 90.0]
        // Transition region for larger evasion maneuvers
        IvPFunctionDefault::Piece transition_ap;
        transition_ap.heading_min_deg = wrap_hdg(nominal_hdg + 60.0);
        transition_ap.heading_max_deg = wrap_hdg(nominal_hdg + 90.0);
        transition_ap.speed_min_kn = 0.0;
        transition_ap.speed_max_kn = speed_max_kn_;
        transition_ap.utility = 0.6;
        avoid_pieces.push_back(transition_ap);

        // 2d. Far Zone / Low-Utility Base (0.1 utility)
        IvPFunctionDefault::Piece base_ap;
        base_ap.heading_min_deg = 0.0;
        base_ap.heading_max_deg = 359.9;
        base_ap.speed_min_kn = 0.0;
        base_ap.speed_max_kn = speed_max_kn_;
        base_ap.utility = 0.1;
        avoid_pieces.push_back(base_ap);

        avoid_fn.set_pieces(avoid_pieces);
        weighted_fns.push_back({10.0, avoid_fn}); // Weight: 10.0
      }
    }

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
      // F2 fix: When COLREGs reports a starboard-required deviation, emit a
      //         heading window biased toward starboard (matching the M6
      //         constraint's numeric_value) instead of a symmetric ±90° window
      //         that downstream M5 collapses into a no-op straight-line plan.
      const double own_hdg =
          latest_world_ ? latest_world_->own_ship.heading_deg : 0.0;

      double starboard_dev_deg = 0.0;
      if (colregs_received_ && latest_colregs_ &&
          latest_colregs_->conflict_detected) {
        for (const auto& c : latest_colregs_->constraints) {
          if (c.constraint_type == "colregs" && c.unit == "deg" &&
              c.numeric_value > 0.0) {
            if (c.numeric_value > starboard_dev_deg) {
              starboard_dev_deg = c.numeric_value;
            }
          }
        }
      }

      if (starboard_dev_deg > 0.0) {
        if (m3_active_latch_ && !fallback_anchor_set_) {
          fallback_anchor_hdg_ = own_hdg;
          fallback_anchor_set_ = true;
          RCLCPP_WARN(get_logger(),
            "[M4] IvP infeasible — anchoring to %.1f° (absolute, will not track own_hdg)",
            fallback_anchor_hdg_);
          
          // Publish ASDR event
          publish_asdr_event("fallback_anchor_latched",
            "{\"anchor_hdg_deg\":" + std::to_string(fallback_anchor_hdg_) + "}");

          // Emit SafetyConcernEvent
          l3_msgs::msg::SafetyConcernEvent concern;
          concern.stamp = now();
          concern.concern_type = l3_msgs::msg::SafetyConcernEvent::CONCERN_IVP_INFEASIBLE;
          concern.anchor_hdg = static_cast<float>(fallback_anchor_hdg_);
          concern.suggested_action = "turn_starboard_30deg_absolute";
          concern.severity = 0.7f;
          concern_pub_->publish(concern);
        }

        const double effective_centre = fallback_anchor_set_
            ? std::fmod(fallback_anchor_hdg_ + starboard_dev_deg + 360.0, 360.0)
            : std::fmod(own_hdg + starboard_dev_deg + 360.0, 360.0);

        h_min = std::fmod(effective_centre - 15.0 + 360.0, 360.0);
        h_max = std::fmod(effective_centre + 15.0 + 360.0, 360.0);
        confidence = fallback_anchor_set_ ? 0.55 : 0.45;
        
        std::ostringstream r;
        r << (fallback_anchor_set_ ? "IvP infeasible — geometric fallback ABSOLUTE "
                                   : "IvP infeasible — geometric fallback relative ")
          << "(anchor=" << (fallback_anchor_set_ ? fallback_anchor_hdg_ : own_hdg)
          << "deg, dev=" << starboard_dev_deg
          << "deg, window=" << h_min << "→" << h_max << ")";
        rationale = r.str();
      } else {
        h_min = std::fmod(own_hdg - 90.0 + 360.0, 360.0);
        h_max = std::fmod(own_hdg + 90.0, 360.0);
        confidence = 0.30;
        rationale = "IvP infeasible fallback";
      }
      s_max = speed_max_kn_;

      // Log infeasibility (always, for ASDR audit trail).
      if (colregs_received_ && latest_colregs_ &&
          latest_colregs_->conflict_detected) {
        std::ostringstream json;
        json << "{\"constraint_count\":"
             << latest_colregs_->constraints.size()
             << ",\"fallback_used\":\""
             << (starboard_dev_deg > 0.0 ? "geometric_starboard" : "cascading")
             << "\",\"starboard_dev_deg\":" << starboard_dev_deg << "}";
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

  if (fallback_anchor_set_ && m3_task_valid) {
    fallback_anchor_set_ = false;
    RCLCPP_INFO(get_logger(), "[M4] Fallback anchor released — M3 task_validity=VALID");
    publish_asdr_event("fallback_anchor_released", "{}");
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
