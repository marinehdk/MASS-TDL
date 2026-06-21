#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <sstream>
#include <cmath>
#include <vector>
#include <spdlog/spdlog.h>

#include "m4_behavior_arbiter/colregs_directive.hpp"

namespace mass_l3::m4 {

using namespace std::chrono_literals;

namespace {
constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kKnotsToMps = 0.5144444444444445;

struct PrimaryRiskGuidance {
  mass_l3::risk::RiskVector current;
  mass_l3::risk::RiskVector reduced_speed;
};

double nav_heading_deg_to_math_rad(double heading_deg) {
  return (90.0 - heading_deg) * kDegToRad;
}

mass_l3::risk::OwnShipInput ownship_risk_input(
    const WorldStateMsg& world,
    double speed_scale = 1.0) {
  return mass_l3::risk::OwnShipInput{
      0.0,
      0.0,
      nav_heading_deg_to_math_rad(world.own_ship.heading_deg),
      std::max(0.0, world.own_ship.sog_kn * kKnotsToMps * speed_scale),
      46.0,
      world.own_ship.confidence,
      world.own_ship.nav_mode == "DEGRADED"};
}

mass_l3::risk::TargetInput target_risk_input(const l3_msgs::msg::TrackedTarget& target) {
  const double bearing_rad = target.brg_deg * kDegToRad;
  const double range_m = std::max(0.0, target.rng_m);
  return mass_l3::risk::TargetInput{
      std::to_string(target.target_id),
      std::sin(bearing_rad) * range_m,
      std::cos(bearing_rad) * range_m,
      nav_heading_deg_to_math_rad(target.cog_deg),
      std::max(0.0, target.sog_kn * kKnotsToMps),
      target.cpa_m,
      target.tcpa_s,
      target.confidence};
}

mass_l3::risk::ColregsDuty target_colregs_duty(
    const ColregsDirective& directive,
    const l3_msgs::msg::TrackedTarget& target) {
  if (!directive.conflict_active) {
    return mass_l3::risk::ColregsDuty::Free;
  }
  if (target.encounter.is_giveway) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (target.encounter.encounter_type ==
          l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSED_BY ||
      target.encounter.encounter_type ==
          l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKEN) {
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  return map_role_to_duty(
      directive.primary_role,
      directive.conflict_active,
      directive.rule15_active,
      directive.phase);
}

std::optional<PrimaryRiskGuidance> compute_primary_risk_guidance(
    const WorldStateMsg& world,
    const ColregsDirective& directive,
    mass_l3::risk::RankingState& ranking_state) {
  std::vector<mass_l3::risk::RiskVector> risks;
  risks.reserve(world.targets.size());
  const auto own = ownship_risk_input(world);
  for (const auto& target : world.targets) {
    if (target.rng_m <= 0.0 || !std::isfinite(target.rng_m)) {
      continue;
    }
    risks.push_back(mass_l3::risk::evaluate_target(
        own,
        target_risk_input(target),
        target_colregs_duty(directive, target)));
  }
  if (risks.empty()) {
    return std::nullopt;
  }

  const auto primary = mass_l3::risk::select_primary(risks, &ranking_state);
  const auto target_it = std::find_if(
      world.targets.begin(),
      world.targets.end(),
      [&primary](const auto& target) {
        return primary.target_id == std::to_string(target.target_id);
      });
  if (target_it == world.targets.end()) {
    return PrimaryRiskGuidance{primary, primary};
  }

  const auto slowed = ownship_risk_input(world, 0.6);
  const auto reduced_speed_risk = mass_l3::risk::evaluate_target(
      slowed,
      target_risk_input(*target_it),
      target_colregs_duty(directive, *target_it));
  return PrimaryRiskGuidance{primary, reduced_speed_risk};
}

// Phase 4 RECOVERY geometry helpers (architecture §8.3 behavior dictionary).
// corridor_half is the route corridor half-width; RECOVERY engages when XTE
// exceeds corridor_half*0.5 after COLREGs release and clears when XTE returns
// within that gate for release_dwell cycles.
constexpr double kEarthRadiusM = 6371008.8;
// [TBD-HAZID] RECOVERY thresholds calibrated against route_return acceptance
// (XTE<150m). corridor_half=250 → gate=125m: RECOVERY engages when XTE>125m
// after COLREGs release and clears to TRANSIT once XTE<125m for release_dwell,
// letting TRANSIT/L2 route-following finish convergence below 150m.
constexpr double kRecoveryCorridorHalfM = 250.0;
constexpr double kRecoveryXteGateFraction = 0.5;        // corridor_half*0.5 = 125m
constexpr int    kRecoveryReleaseDwellCycles = 4;       // ~1s @ 250ms cycle
// RECOVERY→TRANSIT release also requires own-heading alignment with the route
// leg. Releasing on XTE alone lets the ship hand back to TRANSIT still pointed
// off the route line (e.g. -19.6°), and TRANSIT's heading controller then has
// no obligation to converge the residual heading error — route_return's
// heading<10° acceptance fails. Mirror the 4c85cbaa reference implementation
// (kRecoveryCompleteHeadingErrorDeg = 10.0).
constexpr double kRecoveryCompleteHeadingErrorDeg = 10.0;

double signed_heading_error_deg(double heading_deg, double reference_deg) {
  return std::fmod(heading_deg - reference_deg + 540.0, 360.0) - 180.0;
}

}  // namespace

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
  sub_route_ = create_subscription<PlannedRouteMsg>(
      "/l2/planned_route", qos,
      [this](const PlannedRouteMsg::SharedPtr msg) { on_planned_route(msg); });

  pub_plan_ = create_publisher<BehaviorPlanMsg>("/l3/m4/behavior_plan", qos);
  pub_sat2_ = create_publisher<l3_msgs::msg::SAT2Data>("/sil/sat2_data", qos);
  pub_asdr_ = create_publisher<ASDRRecordMsg>("/l3/asdr/record", asdr_qos);
  concern_pub_ = create_publisher<l3_msgs::msg::SafetyConcernEvent>(
      "/l3/safety/concern", rclcpp::QoS(10).reliable());

  timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::milliseconds(interval_ms_),
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
void BehaviorArbiterNode::on_planned_route(const PlannedRouteMsg::SharedPtr msg) {
  latest_route_ = msg; route_received_ = true;
}

std::optional<BehaviorArbiterNode::RouteTracking>
BehaviorArbiterNode::current_route_tracking() const {
  if (!route_received_ || !latest_route_ || !latest_world_ ||
      latest_route_->route.poses.size() < 2u) {
    return std::nullopt;
  }
  const double p0_lat = latest_route_->route.poses[0].pose.position.latitude;
  const double p0_lon = latest_route_->route.poses[0].pose.position.longitude;
  const double p1_lat = latest_route_->route.poses[1].pose.position.latitude;
  const double p1_lon = latest_route_->route.poses[1].pose.position.longitude;
  const double own_lat = latest_world_->own_ship.position.latitude;
  const double own_lon = latest_world_->own_ship.position.longitude;
  // Flat-earth NED projection of route leg and own-ship offset from p0.
  const double ddx = (p1_lat - p0_lat) * kDegToRad * kEarthRadiusM;
  const double ddy = (p1_lon - p0_lon) * kDegToRad * kEarthRadiusM
                     * std::cos(p0_lat * kDegToRad);
  const double route_len = std::hypot(ddx, ddy);
  if (route_len <= 1.0) {
    return std::nullopt;
  }
  const double own_dx = (own_lat - p0_lat) * kDegToRad * kEarthRadiusM;
  const double own_dy = (own_lon - p0_lon) * kDegToRad * kEarthRadiusM
                        * std::cos(p0_lat * kDegToRad);
  const double route_heading_deg = compute_bearing_deg(p0_lat, p0_lon, p1_lat, p1_lon);
  // Signed cross-track: positive = own ship is to port (left) of route bearing.
  return RouteTracking{
      ((ddx * own_dy) - (ddy * own_dx)) / route_len,
      route_heading_deg,
      signed_heading_error_deg(latest_world_->own_ship.heading_deg, route_heading_deg)};
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
  // Phase 4: COLREGs turn active flag, hoisted to callback scope so the
  // RECOVERY edge-detection block at the end can observe release transitions.
  bool colregs_turn_active = false;
  bool m3_task_valid = mission_received_ && latest_mission_ &&
      (latest_mission_->fsm_state == l3_msgs::msg::MissionGoal::FSM_ACTIVE) &&
      (latest_mission_->task_validity == l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID);

  if (!m3_active_latch_ && m3_task_valid) {
    m3_active_latch_ = true;
    RCLCPP_INFO(get_logger(), "[M4] M3 first ACTIVE+VALID: enabling IvP + snapshot guard");
  }

  ArbitrationInputs inputs = build_inputs();
  constexpr int kColregsReleaseDwellCycles = 4;
  constexpr float kLowConfidenceClearThreshold = 0.5F;
  bool colregs_commit_hold = false;
  COLREGsConstraintMsg::SharedPtr colregs_for_directive = latest_colregs_;
  if (colregs_received_ && latest_colregs_) {
    if (latest_colregs_->conflict_detected) {
      last_active_colregs_ = latest_colregs_;
      colregs_inactive_cycles_ = 0;
    } else if (latest_colregs_->confidence < kLowConfidenceClearThreshold &&
               colregs_anchor_set_ && last_active_colregs_ &&
               colregs_inactive_cycles_ < kColregsReleaseDwellCycles) {
      ++colregs_inactive_cycles_;
      inputs.colregs_conflict_detected = true;
      colregs_for_directive = last_active_colregs_;
      colregs_commit_hold = true;
    } else {
      last_active_colregs_.reset();
      colregs_inactive_cycles_ = 0;
    }
  }
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
  bool has_conflict = inputs.colregs_conflict_detected;
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
  std::string risk_rationale_suffix;

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

    ColregsDirective colregs_directive;
    if (colregs_for_directive) {
      colregs_directive = extract_colregs_directive(*colregs_for_directive);
      if (latest_world_ && colregs_directive.conflict_active) {
        const auto risk_guidance = compute_primary_risk_guidance(
            *latest_world_, colregs_directive, risk_ranking_state_);
        if (risk_guidance.has_value()) {
          apply_primary_risk_guidance(
              colregs_directive,
              risk_guidance->current,
              risk_guidance->reduced_speed);
          std::ostringstream risk_text;
          risk_text << " | risk primary=" << colregs_directive.primary_threat_id
                    << " phase=" << colregs_directive.primary_risk_phase
                    << " score=" << colregs_directive.primary_risk_score
                    << " warn_margin_m=" << colregs_directive.primary_warning_margin_m
                    << " danger_margin_m=" << colregs_directive.primary_danger_margin_m;
          if (colregs_directive.speed_reduction_preferred) {
            risk_text << " speed_reduction_preferred=true";
          }
          risk_rationale_suffix = risk_text.str();
        }
      }
      if (colregs_commit_hold) {
        RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 2000,
            "[M4] Holding committed COLREG directive through release dwell (%d/%d)",
            colregs_inactive_cycles_, kColregsReleaseDwellCycles);
      }
    }

    double nearest_target_range_m = std::numeric_limits<double>::max();
    double nearest_target_cpa_m = std::numeric_limits<double>::max();
    bool has_quartering_target = false;
    if (latest_world_) {
      for (const auto& tgt : latest_world_->targets) {
        if (tgt.rng_m > 0.0 && tgt.rng_m < nearest_target_range_m) {
          nearest_target_range_m = tgt.rng_m;
        }
        if (tgt.cpa_m > 0.0 && tgt.cpa_m < nearest_target_cpa_m) {
          nearest_target_cpa_m = tgt.cpa_m;
        }
        if (std::abs(tgt.encounter.relative_bearing_deg) >= 90.0) {
          has_quartering_target = true;
        }
      }
    }
    colregs_turn_active =
        colregs_directive.conflict_active &&
        (colregs_directive.direction == ColregsDirection::Starboard ||
         colregs_directive.direction == ColregsDirection::Port);
    if (colregs_turn_active && !colregs_anchor_set_) {
      colregs_anchor_hdg_ = latest_world_ ? latest_world_->own_ship.heading_deg : nominal_hdg;
      colregs_quartering_gate_ = has_quartering_target;
      colregs_anchor_set_ = true;
      RCLCPP_INFO(get_logger(),
          "[M4] COLREG turn anchor latched at %.1f°", colregs_anchor_hdg_);
    } else if (!colregs_turn_active && colregs_anchor_set_) {
      colregs_anchor_set_ = false;
      colregs_quartering_gate_ = false;
      colregs_rule15_commit_active_ = false;
      colregs_committed_required_dev_deg_ = 0.0;
      RCLCPP_INFO(get_logger(), "[M4] COLREG turn anchor released");
    }
    if (colregs_turn_active &&
        colregs_directive.rule15_active &&
        colregs_directive.primary_role == kRoleGiveWay) {
      colregs_rule15_commit_active_ = true;
    }
    constexpr double kColregsCriticalCpaM = 500.0;
    constexpr double kColregsTacticalCpaBufferM = 1500.0;
    constexpr double kColregsMaxDeviationDeg = 150.0;
    const double effective_max_deviation_deg = effective_colregs_max_deviation_deg(
        colregs_directive, colregs_quartering_gate_, 75.0, kColregsMaxDeviationDeg);
    double required_dev_deg = required_deviation_deg(
        colregs_directive,
        nearest_target_range_m,
        kColregsTacticalCpaBufferM,
        2.5,
        effective_max_deviation_deg);
    if (dynamic_risk_requires_max_deviation(colregs_directive)) {
      required_dev_deg = effective_max_deviation_deg;
    } else if (nearest_target_cpa_m < std::numeric_limits<double>::max() &&
        (colregs_directive.direction == ColregsDirection::Starboard ||
         colregs_directive.direction == ColregsDirection::Port)) {
      const double min_alt_deg = colregs_directive.min_alteration_deg;
      if (colregs_quartering_gate_ && nearest_target_cpa_m <= kColregsCriticalCpaM) {
        required_dev_deg = effective_max_deviation_deg;
      } else if (nearest_target_cpa_m >= kColregsTacticalCpaBufferM) {
        required_dev_deg = min_alt_deg;
      } else {
        const double cpa_ramp_m = colregs_quartering_gate_
            ? (kColregsTacticalCpaBufferM - kColregsCriticalCpaM)
            : kColregsTacticalCpaBufferM;
        const double risk = std::clamp(
            (kColregsTacticalCpaBufferM - nearest_target_cpa_m) /
                cpa_ramp_m,
            0.0,
            1.0);
        required_dev_deg = std::max(
            min_alt_deg,
            min_alt_deg + risk * (effective_max_deviation_deg - min_alt_deg));
      }
    }
    const bool hold_bow_crossing_commitment =
        colregs_turn_active &&
        colregs_rule15_commit_active_ &&
        colregs_directive.primary_role == kRoleGiveWay &&
        !colregs_quartering_gate_;
    if (hold_bow_crossing_commitment) {
      colregs_committed_required_dev_deg_ =
          std::max(colregs_committed_required_dev_deg_, required_dev_deg);
      required_dev_deg =
          std::max(required_dev_deg, colregs_committed_required_dev_deg_);
    } else {
      colregs_committed_required_dev_deg_ = 0.0;
    }
    const double colregs_signed_dev_deg = signed_deviation_deg(
        colregs_directive, required_dev_deg);
    const double colregs_base_hdg =
        colregs_anchor_set_ ? colregs_anchor_hdg_ : nominal_hdg;
    const double current_spd_kn = latest_world_ ? latest_world_->own_ship.sog_kn : speed_max_kn_;
    const double directive_speed_max_kn =
        (colregs_directive.conflict_active &&
         colregs_directive.direction == ColregsDirection::ReduceSpeed)
            ? std::min(speed_max_kn_, std::max(0.0, current_spd_kn * 0.6))
            : speed_max_kn_;

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
    if (colregs_directive.conflict_active && required_dev_deg > 0.0) {
      const double signed_dev = colregs_signed_dev_deg;
      if (std::abs(signed_dev) > 0.0) {
        IvPFunctionDefault avoid_fn;
        std::vector<IvPFunctionDefault::Piece> avoid_pieces;
        const bool turn_port = colregs_directive.direction == ColregsDirection::Port;
        const double optimal_inner_deg =
            (required_dev_deg >= 120.0) ? required_dev_deg - 1.0 : required_dev_deg;
        const double optimal_outer_deg =
            (required_dev_deg >= 120.0)
                ? required_dev_deg
                : std::min(120.0, required_dev_deg + 15.0);
        const double optimal_inner_signed_dev = turn_port ? -optimal_inner_deg : optimal_inner_deg;
        const double optimal_outer_signed_dev = turn_port ? -optimal_outer_deg : optimal_outer_deg;

        RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 2000,
            "[M4 R2] colregs_dev=%.1f° optimal_inner=%.1f° optimal_outer=%.1f° direction=%s",
            signed_dev, optimal_inner_deg, optimal_outer_deg, turn_port ? "PORT" : "STARBOARD");

        IvPFunctionDefault::Piece penalty_ap;
        penalty_ap.heading_min_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? optimal_inner_signed_dev : -180.0));
        penalty_ap.heading_max_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? 180.0 : optimal_inner_signed_dev));
        penalty_ap.speed_min_kn = 0.0;
        penalty_ap.speed_max_kn = speed_max_kn_;
        penalty_ap.utility = 0.05;
        avoid_pieces.push_back(penalty_ap);

        IvPFunctionDefault::Piece optimal_ap;
        optimal_ap.heading_min_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? optimal_outer_signed_dev : optimal_inner_signed_dev));
        optimal_ap.heading_max_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? optimal_inner_signed_dev : optimal_outer_signed_dev));
        optimal_ap.speed_min_kn = 0.0;
        optimal_ap.speed_max_kn = speed_max_kn_;
        optimal_ap.utility = 1.0;
        avoid_pieces.push_back(optimal_ap);

        // Transition region for larger evasion maneuvers in the requested direction.
        const double transition_min_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? -120.0 : optimal_outer_signed_dev));
        const double transition_max_deg = wrap_hdg(
            colregs_base_hdg + (turn_port ? optimal_outer_signed_dev : 120.0));
        if (transition_min_deg != transition_max_deg) {
          IvPFunctionDefault::Piece transition_ap;
          transition_ap.heading_min_deg = transition_min_deg;
          transition_ap.heading_max_deg = transition_max_deg;
          transition_ap.speed_min_kn = 0.0;
          transition_ap.speed_max_kn = speed_max_kn_;
          transition_ap.utility = 0.6;
          avoid_pieces.push_back(transition_ap);
        }

        // 2d. Far Zone / Low-Utility Base (0.1 utility)
        IvPFunctionDefault::Piece base_ap;
        base_ap.heading_min_deg = 0.0;
        base_ap.heading_max_deg = 359.9;
        base_ap.speed_min_kn = 0.0;
        base_ap.speed_max_kn = speed_max_kn_;
        base_ap.utility = 0.1;
        avoid_pieces.push_back(base_ap);

        const M4ErrorCode avoid_set_result = avoid_fn.set_pieces(avoid_pieces);
        if (avoid_set_result == M4ErrorCode::kOk) {
          weighted_fns.push_back({10.0, avoid_fn}); // Weight: 10.0
        } else {
          RCLCPP_WARN(get_logger(),
              "[M4] Skipping COLREG avoidance IvP function: invalid pieces (error=%d)",
              static_cast<int>(avoid_set_result));
        }
      }
    }

    IvPHardConstraints constraints;
    constraints.speed_min_kn = 0.0;
    constraints.speed_max_kn = directive_speed_max_kn;

    // Inject M6 COLREGs heading constraints.
    // Constraint.msg (v1.1.2): constraint_type=="colregs", unit=="deg",
    // numeric_value = minimum heading deviation from own heading.
    if (colregs_directive.conflict_active) {
      const auto ranges = directive_allowed_ranges(
          colregs_base_hdg, colregs_directive, required_dev_deg);
      constraints.heading_allowed_ranges_deg.insert(
          constraints.heading_allowed_ranges_deg.end(), ranges.begin(), ranges.end());
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
      if (colregs_directive.conflict_active && std::abs(colregs_signed_dev_deg) > 0.0) {
        const auto window = directive_heading_window(
            colregs_base_hdg, colregs_directive, required_dev_deg);
        if (window.has_value()) {
          h_min = window->heading_min_deg;
          h_max = window->heading_max_deg;
        }
      }
    } else {
      // R3 fix: Conservative fallback — use configured speed domain max
      // instead of 0.5 * current SOG to break the cascade slowdown loop.
      // F2 fix: When COLREGs reports a turn directive, emit a heading window
      // biased in the M6-requested direction instead of a symmetric window.
      const double own_hdg =
          colregs_turn_active ? colregs_base_hdg
                              : (latest_world_ ? latest_world_->own_ship.heading_deg : 0.0);
      const double signed_dev = colregs_signed_dev_deg;

      if (std::abs(signed_dev) > 0.0) {
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
          concern.suggested_action =
              colregs_directive.direction == ColregsDirection::Port
                  ? "turn_port_absolute"
                  : "turn_starboard_absolute";
          concern.severity = 0.7f;
          concern_pub_->publish(concern);
        }

        const double effective_centre = fallback_anchor_set_
            ? std::fmod(fallback_anchor_hdg_ + signed_dev + 360.0, 360.0)
            : std::fmod(own_hdg + signed_dev + 360.0, 360.0);

        h_min = std::fmod(effective_centre - 15.0 + 360.0, 360.0);
        h_max = std::fmod(effective_centre + 15.0 + 360.0, 360.0);
        confidence = fallback_anchor_set_ ? 0.55 : 0.45;
        
        std::ostringstream r;
        r << (fallback_anchor_set_ ? "IvP infeasible — geometric fallback ABSOLUTE "
                                   : "IvP infeasible — geometric fallback relative ")
          << "(anchor=" << (fallback_anchor_set_ ? fallback_anchor_hdg_ : own_hdg)
          << "deg, dev=" << signed_dev
          << "deg, window=" << h_min << "→" << h_max << ")";
        rationale = r.str();
      } else {
        h_min = std::fmod(own_hdg - 90.0 + 360.0, 360.0);
        h_max = std::fmod(own_hdg + 90.0, 360.0);
        confidence = 0.30;
        rationale = "IvP infeasible fallback";
      }
      s_max = directive_speed_max_kn;

      // Log infeasibility (always, for ASDR audit trail).
      if (colregs_directive.conflict_active && latest_colregs_) {
        std::ostringstream json;
        json << "{\"constraint_count\":"
             << latest_colregs_->constraints.size()
             << ",\"fallback_used\":\""
             << (std::abs(signed_dev) > 0.0 ? "geometric_colregs" : "cascading")
             << "\",\"colregs_dev_deg\":" << signed_dev << "}";
        publish_asdr_event("ivp_infeasible", json.str());
      }
    }
  } else {
    primary = BehaviorType::TRANSIT;
    confidence = 0.60;
    rationale = "No active behaviors; default Transit";
  }

  // Phase 4: AVOID → RECOVERY → TRANSIT (architecture §8.3).
  // COLREGs turn release is detected via the colregs_turn_active falling edge.
  // When release occurs with XTE beyond corridor_half*0.5, M4 holds RECOVERY
  // (gradual return-to-route) instead of hard-switching to TRANSIT. Clears to
  // TRANSIT once XTE is restored within the gate for release_dwell cycles.
  const bool colregs_turn_released = prev_colregs_turn_active_ && !colregs_turn_active;
  prev_colregs_turn_active_ = colregs_turn_active;
  if (colregs_turn_active) {
    // Active COLREGs turn cancels any in-progress recovery.
    recovery_active_ = false;
    recovery_dwell_cycles_ = 0;
  } else {
    const auto tracking = current_route_tracking();
    const double xte_gate_m = kRecoveryCorridorHalfM * kRecoveryXteGateFraction;
    const bool xte_beyond_gate = tracking.has_value() &&
        std::abs(tracking->xte_m) > xte_gate_m;
    // Release also requires own-heading alignment with the route leg, not just
    // XTE convergence. Without this, a ship can clear RECOVERY with XTE<gate
    // while still pointed off the route line and fail route_return's
    // heading<10° acceptance in TRANSIT.
    const bool heading_aligned = tracking.has_value() &&
        std::abs(tracking->heading_error_deg) <= kRecoveryCompleteHeadingErrorDeg;
    if (recovery_active_) {
      if (xte_beyond_gate || !heading_aligned) {
        recovery_dwell_cycles_ = 0;  // not yet restored, reset dwell
      } else if (++recovery_dwell_cycles_ >= kRecoveryReleaseDwellCycles) {
        recovery_active_ = false;    // XTE + heading restored + dwell → TRANSIT
      }
    } else if (colregs_turn_released && xte_beyond_gate) {
      recovery_active_ = true;       // release with XTE偏离 → enter RECOVERY
      recovery_dwell_cycles_ = 0;
    }
    if (recovery_active_) {
      primary = BehaviorType::RECOVERY;
      confidence = 0.80;
      const double xte_m = tracking.has_value() ? tracking->xte_m : 0.0;
      rationale = "RECOVERY gradual return-to-route xte=" +
          std::to_string(static_cast<int>(xte_m)) + "m";
    }
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
  if (!risk_rationale_suffix.empty()) {
    rationale += risk_rationale_suffix;
  }
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
