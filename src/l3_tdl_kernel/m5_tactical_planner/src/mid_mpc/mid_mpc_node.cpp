#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <geometry_msgs/msg/point.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_risk_model/risk_model.hpp"
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/avoidance_waypoint_policy.hpp"
#include "m5_tactical_planner/avoidance_route_hash.hpp"
#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/committed_route/committed_candidate_geometry.hpp"
#include "m5_tactical_planner/committed_route/committed_prefix_reproject.hpp"
#include "m5_tactical_planner/committed_route/committed_route.hpp"
#include "m5_tactical_planner/gnc_avoidance_preflight.hpp"
#include "m5_tactical_planner/mid_mpc/degraded_candidate_adapter.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_route_frame.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"
#include "m5_tactical_planner/tail_builder/tail_builder.hpp"

namespace mass_l3::m5::mid_mpc {

// File-scope alias for the TailBuilder sibling namespace (spec §5.3 wiring).
namespace tb = mass_l3::m5::tail_builder;

namespace {
// [TBD-HAZID] Safe CPA distance [m] used when ODD state is unavailable.
// Calibrate via HAZID RUN-001 WP-03 (SOTIF CPA threshold).
constexpr double kCpaSafeFallback_m = 1852.0;

// [TBD-HAZID] Default planned speed [m/s] when speed profile is absent.
// Set to nominal cruise speed from scenario YAML (10 kn for FCB imazu tests).
constexpr double kDefaultPlannedSpeed_mps = 5.14;

// GNC coordinate_transform keeps a wall-time route-update guard. Probe runs use
// accelerated sim time, so repeat the M6-owned release intent long enough for
// the GNC guard to admit one return_to_route update.
constexpr double kReturnToRouteRepublishWindow_s = 30.0;

// Slice W1 (spec §5.3): TailBuilder normal-path wiring helpers. The TailBuilder
// operates in own-relative NED metres (same frame as assemble_input_'s wp_n/wp_e
// and the NLP terminal state). These helpers build the TailInputs from the live
// M6 constraint + NLP solution + L2 route, and append the resulting NED tail
// waypoints back into the AvoidancePlan parallel arrays as lat/lon.

// Map the M6-owned encounter_state (CLEAR=0,ONSET=1,ACTIVE=2,RELEASE=3) onto the
// TailBuilder's EncounterState enum. The TailBuilder enum is the authoritative
// lifecycle contract used by its two-phase semantics; M5 must not rejudge the
// lifecycle, only translate (spec §5.2/§10.1).
//
// Review High-2: ONSET and unknown must NOT map to Active. spec §5.2 defines the
// active phase strictly as ENCOUNTER_ACTIVE && !past_clear. ONSET means M6 reports
// the encounter "just beginning" (not yet fully ACTIVE); unknown is missing data.
// Mapping either to Active was an M5 lifecycle rejudgement that violated spec
// §10.1 state authority. They now map to Onset, which build() treats as abnormal
// (neither active nor released) → reject m6_not_past_clear → honest fallback.
std::uint8_t tail_encounter_state_from_m6(std::uint8_t m6_state) noexcept
{
  namespace tb = mass_l3::m5::tail_builder;
  switch (m6_state) {
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_CLEAR:
      return static_cast<std::uint8_t>(tb::EncounterState::Clear);
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_RELEASE:
      return static_cast<std::uint8_t>(tb::EncounterState::Release);
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ACTIVE:
      return static_cast<std::uint8_t>(tb::EncounterState::Active);
    case l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ONSET:
    default:
      // ONSET / unknown → Onset: NOT active, NOT released. build() rejects it as
      // m6_not_past_clear rather than assuming ACTIVE (spec §5.2/§10.1/§14.3).
      return static_cast<std::uint8_t>(tb::EncounterState::Onset);
  }
}

mass_l3::m5::tail_builder::ColregSide tail_protected_side_from_preferred(
    mass_l3::m5::ColregsPreferredDirection pref) noexcept
{
  using mass_l3::m5::ColregsPreferredDirection;
  using mass_l3::m5::tail_builder::ColregSide;
  if (pref == ColregsPreferredDirection::Starboard) {
    return ColregSide::STBD;
  }
  if (pref == ColregsPreferredDirection::Port) {
    return ColregSide::PORT;
  }
  return ColregSide::NONE;
}

mass_l3::m5::tail_builder::ColregRole tail_role_from_m6(std::uint8_t primary_role) noexcept
{
  // M6 primary_role enum: 0=STAND_ON, 1=GIVE_WAY, 2=BOTH_GIVE_WAY, 3=FREE.
  using mass_l3::m5::tail_builder::ColregRole;
  switch (primary_role) {
    case 0U: return ColregRole::StandOn;
    case 2U: return ColregRole::BothGiveWay;
    case 1U: return ColregRole::GiveWay;
    default: return ColregRole::Free;
  }
}

// Build a tail_builder RouteFrame polyline in own-relative NED metres from the
// L2 planned route. Mirrors assemble_input_'s wp_n/wp_e projection (spec §4.1).
mass_l3::m5::tail_builder::RouteFrame tail_route_frame_from_l2(
    const l3_external_msgs::msg::PlannedRoute::SharedPtr& planned_route,
    double own_lat_deg, double own_lon_deg)
{
  namespace tb = mass_l3::m5::tail_builder;
  tb::RouteFrame frame;
  if (planned_route == nullptr || planned_route->route.poses.size() < 2u) {
    return frame;
  }
  const double cos_lat = std::cos(own_lat_deg * units::kRadPerDeg);
  const auto& poses = planned_route->route.poses;
  frame.waypoints.reserve(poses.size());
  for (const auto& pose : poses) {
    const double plat = pose.pose.position.latitude;
    const double plon = pose.pose.position.longitude;
    const double n_m = (plat - own_lat_deg) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    const double e_m = (plon - own_lon_deg) * units::kRadPerDeg * units::kEarthRadiusMean_m * cos_lat;
    frame.waypoints.push_back(tb::GeoWP{n_m, e_m, 5.0, "L2_NOMINAL"});
  }
  return frame;
}

// flat-earth NED → WGS84 (matches MidMpcWaypointGenerator::ned_to_geopoint_).
void tail_ned_to_latlon(double dx_m, double dy_m, double lat0_deg, double lon0_deg,
                        double& out_lat_deg, double& out_lon_deg) noexcept
{
  out_lat_deg = lat0_deg + (dx_m / units::kEarthRadiusMean_m) * units::kDegPerRad;
  out_lon_deg = lon0_deg + (dy_m / (units::kEarthRadiusMean_m *
      std::cos(lat0_deg * units::kRadPerDeg))) * units::kDegPerRad;
}

l3_msgs::msg::AvoidanceWaypoint waypoint_from_route_point(
    double latitude,
    double longitude,
    double speed_mps,
    float confidence,
    const std::string& rationale) {
  l3_msgs::msg::AvoidanceWaypoint wp;
  wp.schema_version = 112;
  wp.position.latitude = latitude;
  wp.position.longitude = longitude;
  wp.position.altitude = 0.0;
  wp.target_speed_kn = speed_mps / units::kMsPerKn;
  wp.confidence = confidence;
  wp.rationale = rationale;
  return wp;
}

// GNC execution-ODD minimum first-changed distance [m]. The ROS
// GncExecutionOdd msg does not yet carry this field, so the spec §6.6.2 default
// (= GncExecutionOdd.min_first_changed_distance_m, 100 m) is used. Plan
// waypoints closer than this to the own-ship are inside the GNC guard and must
// be frozen (frozen_prefix_count).
constexpr double kMinFirstChangedDistance_m = 100.0;

// Risk context sourced from M2 WorldState (spec §6.6.4 / §9.12 Keep-Last risk
// gate). Used to populate the candidate's risk fields so the manager's
// current_cpa < cpa_hard gate can trigger (the legacy default 1e9/0 left it
// inert). All fields fall back to safe "no risk" values when no target is
// present so the gate does not spuriously fire on target-free transits.
struct CommittedCandidateRiskContext {
  double own_lat_deg{0.0};
  double own_lon_deg{0.0};
  double own_psi_rad{0.0};
  double min_target_cpa_m{1.0e9};              // min predicted CPA over targets [m] (M2)
  double min_target_current_range_m{1.0e9};    // min current own↔target range [m] (hypot x_m,y_m)
  double primary_target_cog_rad{0.0};          // COG of the min-range target [rad]
  bool   has_target{false};
  // Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): the commit gate now needs the
  // candidate's achieved terminal CPA and whether the target is opening or
  // closing — same semantics tail-gate uses (types.hpp:824-842). Filled by
  // the optimized branch from sol + primary_target_risk(input) before
  // try_revise is called.
  double primary_target_closing_speed_mps{0.0};  // >0 closing, <=0 opening
  double candidate_terminal_cpa_m{1.0e9};        // achieved CPA from NLP terminal state
};

mass_l3::m5::committed_route::CommittedRouteCandidate committed_candidate_from_plan(
    const l3_msgs::msg::AvoidancePlan& plan,
    bool nlp_ok,
    double valid_until_s,
    const CommittedCandidateRiskContext& risk) {
  mass_l3::m5::committed_route::CommittedRouteCandidate candidate;
  candidate.plan_id = plan.plan_id;
  candidate.valid_until_s = valid_until_s;
  candidate.nlp_ok = nlp_ok;
  const std::size_t n = std::min(
      {plan.latitude.size(), plan.longitude.size(), plan.command_speed_mps.size(),
       plan.navigation_mode.size(), plan.segment_source.size()});
  candidate.geometry.reserve(n);
  for (std::size_t i = 0U; i < n; ++i) {
    std::string nav_mode = plan.navigation_mode[i];
    if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED) {
      nav_mode = "MID_MPC_OPTIMIZED";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD) {
      nav_mode = "MID_MPC_TERMINAL_HOLD";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2) {
      nav_mode = "REJOIN_TO_L2";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::L2_NOMINAL_SUFFIX) {
      nav_mode = "L2_NOMINAL_SUFFIX";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR) {
      nav_mode = "DEGRADED_CORRIDOR";
    }
    candidate.geometry.push_back(mass_l3::m5::committed_route::GeoWP{
        plan.latitude[i], plan.longitude[i], plan.command_speed_mps[i], nav_mode});
  }

  // Spec §6.6.2 frozen_prefix_count: leading plan waypoints within the in-guard
  // window AHEAD of the own-ship (along-track station <= own_station +
  // min_first_changed_distance_m). Those are inside the GNC guard (already
  // executing) and must be frozen so a new revision cannot alter geometry the
  // vessel is already committed to. The count is computed by the pure
  // along-track projection in committed_candidate_geometry.hpp (unit-tested in
  // test_committed_candidate_geometry), NOT the legacy Euclidean distance: the
  // Euclidean own↔waypoint distance stays < guard for a stretch even after own
  // has overrun the waypoint, so it could not bound the window correctly (spec
  // §6.6, Critical High-4 review fix). Overrun waypoints are pruned as own
  // advances via the manager's requested-count honouring (§6.6.3).
  const auto frozen = mass_l3::m5::committed_route::compute_frozen_prefix_count(
      plan.latitude, plan.longitude,
      risk.own_lat_deg, risk.own_lon_deg, kMinFirstChangedDistance_m);
  candidate.frozen_prefix_count = frozen.frozen_prefix_count;

  // Spec §6.6.4 / §9.12 Keep-Last risk fields — wired from M2 WorldState.
  // current_cpa_m semantics (Codex review 2026-07-03, WRONG ABSTRACTION fix):
  //   the manager's `current_cpa < cpa_hard_m` gate must compare the CURRENT
  //   own↔target geometric range, NOT M2's predicted CPA. A target on a
  //   collision course has tgt.cpa_m → 0 by definition (that is why avoidance
  //   is needed); using the predicted CPA here rejected every candidate whose
  //   whole point was to resolve that CPA (4670 spurious rejections in
  //   rule14-ho). The current range only drops below cpa_hard when the target
  //   is ACTUALLY inside the hard floor right now — the correct trigger.
  //   min_target_cpa_m (M2 predicted) is retained on the context for telemetry
  //   / future candidate-route CPA work, but no longer feeds the commit gate.
  candidate.current_cpa_m = risk.min_target_current_range_m;
  candidate.cpa_hard_m = kCpaSafeFallback_m;
  // Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): forward the candidate's achieved
  // terminal CPA + target open/close state so risk_trigger_event can apply
  // the tail-gate-aligned floor (skip on active approach, hard on release).
  candidate.terminal_cpa_m = risk.candidate_terminal_cpa_m;
  candidate.target_opening = risk.has_target &&
      risk.primary_target_closing_speed_mps <= 0.0;

  // Spec §6.6.4 / §9.12 Keep-Last risk fields — target_heading_delta_deg and
  // cpa_drift_fraction are intentionally left at their safe "no risk" defaults
  // (0.0) so the manager's > 15 deg / > 0.20 gates do NOT spuriously fire.
  //
  // WHY (Critical 1/2 review fix): the spec §9.12 semantics are
  //   target_heading_delta_deg = the target's heading CHANGE between two
  //     consecutive snapshots (a target manoeuvre signal), NOT target-own
  //     heading difference; and
  //   cpa_drift_fraction = the CPA's deterioration RELATIVE TO ITS INITIAL /
  //     EXPECTED value (current vs initial CPA), NOT (cpa_hard - current).
  // Both correctly require PERSISTED history (previous snapshot's target
  // heading / CPA) — a manager-level state, not derivable from a single
  // snapshot's input.targets. The previous M1 code filled them with
  // history-free proxies that triggered on SAFE geometry:
  //   - target_heading_delta_deg = target_COG - own_heading → any crossing
  //     target whose course differs from own by > 15 deg fired DegradedHold;
  //   - cpa_drift_fraction = (cpa_hard - current_cpa)/cpa_hard → a SAFE CPA
  //     (current=2500 > hard=1852) gave -0.35, |−0.35| > 0.20 → fired.
  // Safe-defaulting them to 0.0 (no deterioration, no target manoeuvre
  // observed) is the honest, conservative M1 position: the primary Keep-Last
  // trigger current_cpa < cpa_hard (real, single-snapshot data source) remains
  // active. The manager's risk_trigger_event heading/drift checks
  // (committed_route.cpp) are RETAINED so a future snapshot/history wiring can
  // activate them without further surgery here (spec §6.6.4 "from snapshot").
  (void)risk.has_target;
  (void)risk.primary_target_cog_rad;
  (void)risk.own_psi_rad;
  candidate.target_heading_delta_deg = 0.0;
  candidate.cpa_drift_fraction = 0.0;
  return candidate;
}


mass_l3::risk::ColregsDuty colregs_duty_from_role(std::uint8_t primary_role) {
  if (primary_role == 1U) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (primary_role == 2U) {
    return mass_l3::risk::ColregsDuty::BothGiveWay;
  }
  if (primary_role == 0U) {
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  return mass_l3::risk::ColregsDuty::Free;
}

mass_l3::risk::OwnShipInput ownship_risk_input(const TrajectoryPoint& own_ship) {
  return mass_l3::risk::OwnShipInput{
      own_ship.x_m,
      own_ship.y_m,
      own_ship.psi_rad,
      own_ship.u_mps,
      46.0,
      1.0,
      false};
}

mass_l3::risk::TargetInput target_risk_input(const TargetState& target) {
  return mass_l3::risk::TargetInput{
      std::to_string(target.id),
      target.x_m,
      target.y_m,
      target.cog_rad,
      target.sog_mps,
      target.cpa_m,
      target.tcpa_s,
      target.confidence};
}

std::vector<TargetRiskSnapshot> build_target_risk_snapshots(
    const MidMpcInput& input,
    std::uint8_t primary_role,
    mass_l3::risk::RankingState& ranking_state) {
  std::vector<mass_l3::risk::RiskVector> risks;
  risks.reserve(input.targets.size());
  const auto own = ownship_risk_input(input.own_ship);
  const auto duty = colregs_duty_from_role(primary_role);
  for (const auto& target : input.targets) {
    risks.push_back(mass_l3::risk::evaluate_target(
        own,
        target_risk_input(target),
        duty));
  }
  const auto primary = mass_l3::risk::select_primary(risks, &ranking_state);

  std::vector<TargetRiskSnapshot> snapshots;
  snapshots.reserve(risks.size());
  for (const auto& risk : risks) {
    snapshots.push_back(TargetRiskSnapshot{
        risk.target_id,
        risk.risk_score,
        risk.warning_margin_m,
        risk.danger_margin_m,
        risk.tcpa_s,
        risk.closing_speed_mps,
        risk.target_id == primary.target_id});
  }
  return snapshots;
}

// ===========================================================================
// Slice C1: prefix reprojection + K computation (spec §6.2 / §6.3).
//
// Frozen committed-geometry prefix (WGS84 lat/lon) → NLP psi/u equality targets
// reprojected into the current cycle's ownship-relative NED frame.
//
// CONTRACT (spec §6.2 Critical): the prefix is frozen in WGS84 (the committed
// route geometry), NOT in psi/u (which are ownship-relative control quantities
// whose implied WGS84 geometry shifts each cycle as the ownship origin moves).
// Each cycle reprojects the frozen WGS84 waypoints to the CURRENT NED origin and
// back-infers the per-step psi/u, so the published geometry stays continuous.
//
// The core logic (WGS84 → NED reproject + back-infer psi/u + K) lives in the
// pure function committed_route::reproject_prefix_psi_u
// (committed_prefix_reproject.hpp), unit-tested by test_mid_mpc_continuity
// (§6.2 Critical reprojection). This node wrapper adapts the GeoWP vector to the
// pure function's lat/lon-array interface (spec §3.7 coordinate contract).
//
// K computation (spec §6.3): K = ceil(guard_distance / (own_u · dt)), clamped to
//   [0, K_max] where K_max = N - K_suffix_min (K_suffix_min = 8 → 40 s suffix,
//   ample avoidance room). own_u is floored at 0.5 m/s so a near-stationary ship
//   does not inflate K to the full horizon (spec §6.3 footnote; K_max clamp is
//   the hard backstop). K=0 on first commit (no committed prefix yet).
// ===========================================================================
struct PrefixPsiU {
  int32_t K{0};
  std::vector<double> psi_rad;
  std::vector<double> u_mps;
};

PrefixPsiU reproject_committed_prefix(
    const std::vector<mass_l3::m5::committed_route::GeoWP>& prefix_wgs84,
    double own_lat_deg, double own_lon_deg,
    double own_psi_rad, double own_u_mps,
    double dt_s, int32_t N,
    double guard_distance_m = kMinFirstChangedDistance_m) {
  // Delegate to the pure, unit-tested function (committed_prefix_reproject.hpp).
  // Convert the GeoWP vector to parallel lat/lon degree arrays (spec §3.7).
  std::vector<double> lat_deg(prefix_wgs84.size());
  std::vector<double> lon_deg(prefix_wgs84.size());
  for (std::size_t i = 0u; i < prefix_wgs84.size(); ++i) {
    lat_deg[i] = prefix_wgs84[i].lat_deg;
    lon_deg[i] = prefix_wgs84[i].lon_deg;
  }
  const auto r = mass_l3::m5::committed_route::reproject_prefix_psi_u(
      lat_deg, lon_deg, own_lat_deg, own_lon_deg,
      own_psi_rad, own_u_mps, dt_s, N, guard_distance_m);
  return PrefixPsiU{r.K, std::move(r.psi_rad), std::move(r.u_mps)};
}
}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================
MidMpcNlpFormulation::Config MidMpcNode::resolve_nlp_config_(
    const MidMpcNlpFormulation::Config& cfg)
{
  MidMpcNlpFormulation::Config resolved = cfg;
  const double horizon_s = declare_parameter<double>("mid_mpc.horizon_s",
      static_cast<double>(resolved.n_horizon) * resolved.dt_s);
  const int64_t n_steps = declare_parameter<int64_t>(
      "mid_mpc.n_steps", static_cast<int64_t>(resolved.n_horizon));
  resolved.dt_s = declare_parameter<double>("mid_mpc.dt_s", resolved.dt_s);
  return resolve_mid_mpc_horizon_config(resolved, horizon_s, n_steps, resolved.dt_s);
}

MidMpcWaypointGenerator::Config MidMpcNode::resolve_waypoint_config_(
    const MidMpcWaypointGenerator::Config& cfg,
    double dt_s,
    int32_t n_horizon)
{
  MidMpcWaypointGenerator::Config resolved = cfg;
  resolved.dt_s = dt_s;
  resolved.num_waypoints = std::max(resolved.num_waypoints, n_horizon);
  return resolved;
}

MidMpcNode::MidMpcNode(const Config& cfg)
    : rclcpp::Node("m5_mid_mpc_node"),
      manifest_(mass_l3::m5::shared::CapabilityManifest::load_from_yaml(
          ament_index_cpp::get_package_share_directory("m5_tactical_planner") +
          "/config/fcb_vessel_capability.yaml")),
      vessel_model_(manifest_),
      nomoto_fallback_(nomoto_cfg_, manifest_),
      formulation_(resolve_nlp_config_(cfg.nlp)),
      solver_(formulation_, cfg.ipopt),
      wp_gen_(resolve_waypoint_config_(
          cfg.waypoint, formulation_.config().dt_s, formulation_.config().n_horizon))
{
  formulation_.build_symbolic_graph();

  nominal_speed_kn_ = declare_parameter<double>("m5.nominal_speed_kn", 10.0);

  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        world_state_ = std::move(msg);
      });

  sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", 10,
      [this](l3_msgs::msg::BehaviorPlan::SharedPtr msg) {
        behavior_plan_ = std::move(msg);
      });

  sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", 10,
      [this](l3_msgs::msg::COLREGsConstraint::SharedPtr msg) {
        colregs_constraint_ = std::move(msg);
      });

  sub_route_ = create_subscription<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", 10,
      [this](l3_external_msgs::msg::PlannedRoute::SharedPtr msg) {
        planned_route_ = std::move(msg);
      });

  sub_speed_ = create_subscription<l3_external_msgs::msg::SpeedProfile>(
      "/l2/speed_profile", 10,
      [this](l3_external_msgs::msg::SpeedProfile::SharedPtr msg) {
        speed_profile_ = std::move(msg);
      });

  // Cross-run reset: clear MPC warm state on new scenario. TRANSIENT_LOCAL so
  // we receive the latched scenario_id even though M5 starts after configure.
  sub_scenario_loaded_ = create_subscription<std_msgs::msg::String>(
      "/sil/scenario_loaded",
      rclcpp::QoS(rclcpp::KeepLast(10)).transient_local(),
      [this](std_msgs::msg::String::SharedPtr msg) { on_scenario_loaded_(std::move(msg)); });

  // W2: subscribe the latched GNC execution-ODD contract (forwarded by gnc_bridge
  // from domain 50). transient_local so a late-starting M5 gets the last value.
  sub_gnc_odd_ = create_subscription<ship_interfaces::msg::GncExecutionOdd>(
      "/gnc/execution_odd",
      rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable(),
      [this](ship_interfaces::msg::GncExecutionOdd::SharedPtr msg) {
        std::lock_guard<std::mutex> lk(gnc_odd_mutex_);
        latest_gnc_odd_ = std::move(*msg);
      });

  pub_avoidance_plan_ = create_publisher<l3_msgs::msg::AvoidancePlan>("/l3/m5/avoidance_plan", 10);
  pub_asdr_record_    = create_publisher<l3_msgs::msg::ASDRRecord>("/l3/asdr/record", 10);
  pub_sat_data_       = create_publisher<l3_msgs::msg::SATData>("/l3/sat/data", 10);
  pub_sat3_data_      = create_publisher<l3_msgs::msg::SAT3Data>("/sil/sat3_data", 10);
  // v2.2 §13.1: BC-MPC Phase E2 wiring. Reliable QoS (Codex 🟡1): this is a
  // safety-relevant dispatch signal — a dropped consecutive_failures sample
  // could leave BC-MPC stale at 0 and delay take-over. The signal is low-
  // frequency (~1-4 Hz, one msg per Mid-MPC cycle) so reliable backpressure
  // cannot starve the solve cycle; the 10-deep buffer absorbs bursts.
  pub_consecutive_failures_ = create_publisher<std_msgs::msg::UInt64>(
      "/l3/m5/mid_mpc/consecutive_failures", rclcpp::QoS(10).reliable());

  nomoto_cfg_.n_steps = 12;
  nomoto_cfg_.dt_s    = 5.0;

  solve_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { on_solve_cycle_(); });
}

// ===========================================================================
// has_required_inputs_
// ===========================================================================
bool MidMpcNode::has_required_inputs_() const noexcept
{
  return world_state_       != nullptr
      && behavior_plan_     != nullptr
      && colregs_constraint_ != nullptr;
}

// ===========================================================================
// assemble_input_ — precondition: has_required_inputs_() == true
// ===========================================================================
MidMpcInput MidMpcNode::assemble_input_()
{
  MidMpcInput inp;

  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  // Fix C-2b (Codex review 2026-07-03): normalize own_psi to [-π, +π] to match
  // the NLP psi variable box. Rule17 (|psi-own_psi|<=5°) and direction/min_alt
  // rows use raw psi - own_psi; if own_psi=2π (positive-normalized from a
  // 0..360° heading) while NLP psi ∈ [-π,π], the subtraction yields π instead
  // of 0 → constraint set empty → Infeasible. All downstream consumers
  // (kIdxOwnPsi, constraint_inputs.own_ship_psi_rad, route-frame, risk_ctx)
  // read inp.own_ship.psi_rad, so wrapping once here fixes all paths.
  inp.own_ship.psi_rad = mass_l3::m5::normalize_heading_signed(
      world_state_->own_ship.heading_deg * units::kRadPerDeg);
  
  const double u_water = world_state_->own_ship.u_water;
  inp.own_ship.u_mps   = (u_water > 0.1) ? u_water
                         : world_state_->own_ship.sog_kn * units::kMsPerKn;

  const double own_lat = world_state_->own_ship.position.latitude;
  const double own_lon = world_state_->own_ship.position.longitude;

  for (const auto& tgt : world_state_->targets) {
    TargetState ts;
    ts.id       = static_cast<int32_t>(tgt.target_id & 0x7FFFFFFFu);
    ts.x_m      = (tgt.position.latitude  - own_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    ts.y_m      = (tgt.position.longitude - own_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                  * std::cos(own_lat * units::kRadPerDeg);
    ts.sog_mps  = tgt.sog_kn * units::kMsPerKn;
    ts.cog_rad  = tgt.cog_deg * units::kRadPerDeg;
    ts.cpa_m    = tgt.cpa_m;
    ts.cpa_sigma_m = std::sqrt(std::max(tgt.cpa_covariance_m2, 0.0));
    ts.tcpa_s   = tgt.tcpa_s;
    inp.targets.push_back(ts);
    inp.tail_gate_targets.push_back(ts);
  }

  double h_min_raw = static_cast<double>(behavior_plan_->heading_min_deg) * units::kRadPerDeg;
  double h_max_raw = static_cast<double>(behavior_plan_->heading_max_deg) * units::kRadPerDeg;
  // Full-circle M4 window (transit) ⇒ unconstrained [-π, +π]; else unwrap near
  // own_ship psi. Guarantees lb <= ub (CasADi nlpsol asserts it). See
  // resolve_heading_box_bounds + test_heading_bounds.
  const std::pair<double, double> heading_bounds =
      mass_l3::m5::resolve_heading_box_bounds(h_min_raw, h_max_raw, inp.own_ship.psi_rad);
  inp.constraints.heading_min_rad = heading_bounds.first;
  inp.constraints.heading_max_rad = heading_bounds.second;

  // v2.2 §4.6: M4 reachability 合约字段（schema 113+）。0 sentinel = M4 未升级。
  inp.constraints.heading_box_reachable_from_psi0_deg =
      static_cast<double>(behavior_plan_->heading_box_reachable_from_psi0_deg);
  inp.constraints.rot_step_deg =
      static_cast<double>(behavior_plan_->rot_step_deg);
  inp.constraints.min_alt_required_rad =
      static_cast<double>(behavior_plan_->min_alt_required_rad);
  inp.constraints.earliest_min_alt_k =
      static_cast<double>(behavior_plan_->earliest_min_alt_k);

  inp.constraints.speed_min_mps   = static_cast<double>(behavior_plan_->speed_min_kn) * units::kMsPerKn;
  double speed_max_raw = static_cast<double>(behavior_plan_->speed_max_kn);

  // R3 fix: if M4 is in fallback mode, use nominal speed instead of current SOG
  // to break the cascade slowdown feedback loop.
  const std::string& m4_rationale = behavior_plan_->rationale;
  if (mass_l3::m5::is_m4_fallback_rationale(m4_rationale)) {
    spdlog::info("[M5][MidMPC] M4 fallback detected (rationale: '{}'); using nominal speed {:.1f} kn",
                 m4_rationale, nominal_speed_kn_);
    speed_max_raw = nominal_speed_kn_;
  }

  inp.constraints.speed_max_mps   = speed_max_raw * units::kMsPerKn;
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
  inp.colregs_conflict_active =
      colregs_constraint_ != nullptr && colregs_constraint_->conflict_detected;
  if (colregs_constraint_ != nullptr) {
    inp.colregs_primary_role = colregs_constraint_->primary_role;
    inp.colregs_preferred_direction = mass_l3::m5::parse_colregs_preferred_direction(
        colregs_constraint_->primary_preferred_direction);
    for (const auto& rule : colregs_constraint_->active_rules) {
      const auto rule_id = static_cast<std::uint8_t>(rule.rule_id);
      const bool planner_visible = rule_id == 13u || rule_id == 14u || rule_id == 15u
          || rule_id == 16u || rule_id == 17u;
      // Fix C-2a (Codex review 2026-07-03): Rule 17 (stand-on) is a
      // COLREGs-mandated duty to HOLD course/speed. It only applies to own
      // ship when own is the stand-on vessel (primary_role == STAND_ON=0).
      // M6 may publish Rule17 as active with role=FREE (CPA proximity
      // evaluation) when no primary give-way rule has onset (e.g. tcpa >
      // t_plan_s). Trusting that and compiling the |psi-own_psi|<=5° hard
      // constraint makes the NLP infeasible when own must actually maneuver.
      // Defensive gate: only forward Rule17 to the NLP when primary_role is
      // STAND_ON. Belt-and-suspenders with the M6 generate() suppress (C-1).
      const bool rule17_eligible = rule_id != 17u ||
          colregs_constraint_->primary_role == 0U;  // STAND_ON
      if (planner_visible && rule17_eligible
          && std::find(inp.constraints.applicable_rules.begin(),
                       inp.constraints.applicable_rules.end(),
                       rule_id) == inp.constraints.applicable_rules.end()) {
        inp.constraints.applicable_rules.push_back(rule_id);
      }
    }
    double min_alt_deg = 0.0;
    for (const auto& c : colregs_constraint_->constraints) {
      if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
        min_alt_deg = std::max(min_alt_deg, c.numeric_value);
      }
    }
    inp.colregs_min_alteration_rad = min_alt_deg * units::kRadPerDeg;
  }

  if (inp.colregs_conflict_active) {
    inp.target_risks = build_target_risk_snapshots(
        inp,
        colregs_constraint_->primary_role,
        risk_ranking_state_);
    if (behavior_plan_->rationale.find("speed_reduction_preferred=true") != std::string::npos) {
      inp.colregs_preferred_direction = ColregsPreferredDirection::ReduceSpeed;
    }
  }

  // Dynamically adjust CPA safe distance based on COLREGs constraint.
  // Only the SOFT colreg-barrier cpa_safe is bumped here; J_colreg's range-ramp
  // weight (mid_mpc_nlp_formulation build_colreg_cost_) is the correct dynamic
  // weighting mechanism. The earlier target.cpa_m/tcpa_s mutation was dead code
  // — the NLP parameter pack does not read those fields (J_colreg redesign,
  // spec §8.2). target cpa_m/tcpa_s now stay at their M2-sourced values.
  double cpa_safe = kCpaSafeFallback_m;
  if (inp.colregs_conflict_active) {
    cpa_safe = 2500.0; // increase CPA boundary during active encounter
  }
  inp.constraints.cpa_safe_m       = cpa_safe;
  // Hard CPA floor is the un-bumped ODD CPA safe (1852), NOT the cost-scaled
  // cpa_safe above. compile_cpa_distance reads cpa_hard_m; the 2500 bump is for
  // the SOFT colreg barrier only (Bug C deep, RC-C; spec §L84).
  inp.constraints.cpa_hard_m       = kCpaSafeFallback_m;
  // v2.1 §4.5: terminal lateral feasibility band packed from the formulation
  // Config so accept_tail_gate (which receives MidMpcInput only) can enforce
  // the band the NLP softened terminal rows no longer hard-enforce.
  inp.constraints.terminal_l_min_feasible_m = formulation_.config().terminal_l_min_feasible_m;
  inp.constraints.terminal_l_max_feasible_m = formulation_.config().terminal_l_max_feasible_m;

  const bool has_route = planned_route_ != nullptr
      && planned_route_->route.poses.size() >= 2u;
  if (has_route) {
    // ── Convert the full L2 polyline to own-relative NED metres (spec §4.1).
    // own_ship.x_m/y_m (kIdxX0/Y0) are ALWAYS (0,0) — the own-relative origin
    // (targets are packed as lat/lon offsets from own). To keep J_route's
    // route-frame origin in the SAME frame (Critical-2 review fix), every
    // waypoint is projected to NED metres relative to the OWN ship current
    // position. The active-leg point thus lives in the same frame as X0/Y0, and
    // l[0] = (own_pos - leg_point) · n_hat is the true current cross-track.
    const double cos_lat = std::cos(own_lat * units::kRadPerDeg);
    const auto& poses = planned_route_->route.poses;
    const std::size_t n_wp = poses.size();
    std::vector<double> wp_n(n_wp), wp_e(n_wp);
    for (std::size_t i = 0u; i < n_wp; ++i) {
      const double plat = poses[i].pose.position.latitude;
      const double plon = poses[i].pose.position.longitude;
      wp_n[i] = (plat - own_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
      wp_e[i] = (plon - own_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                * cos_lat;
    }

    // ── Nearest-leg search + end-clamped fallback + cross-leg guard
    // (Critical-3 review fix, spec §4.1/§4.3). The pure geometry lives in
    // mid_mpc_route_frame.hpp (unit-tested in test_mid_mpc_route_frame). It now
    // (a) SAVES the station s0 of own's projection on the active leg, and
    // (b) compares reach against the REMAINING distance to the corner
    //     (active_len - s0), not the full active_len — so own in the back half
    //     of a long leg is correctly guarded.
    // The fallback now uses the END-CLAMPED point distance (distance to the
    // nearest segment endpoint), not the infinite-line perpendicular distance,
    // so a leg whose extension passes near own but whose actual segment is far
    // is not falsely selected (Critical 3B).
    const ActiveLegProjection proj =
        project_own_onto_polyline(wp_n, wp_e);
    if (!proj.valid) {
      // No usable segment — disable J_route.
      inp.route_weight = 0.0;
    } else {
      const std::size_t active_leg = proj.leg_index;
      const double route_bearing = proj.route_bearing_rad;
      inp.planned_route_bearing_rad = route_bearing;
      inp.route_xte_m = proj.cross_track_l0_m;

      // ── Route-frame origin = ACTIVE-LEG START in the own-relative frame
      // (Critical-2 review fix). Since own is at (0,0), the leg start (wp_n,wp_e)
      // is the own-relative vector FROM own TO the leg point.
      inp.route_frame_origin_x_m = wp_n[active_leg];
      inp.route_frame_origin_y_m = wp_e[active_leg];
      // Active-leg normal n = (-sinψ, cosψ) → starboard is positive (spec §3.1).
      inp.route_frame_normal_x = -std::sin(route_bearing);
      inp.route_frame_normal_y =  std::cos(route_bearing);
      inp.route_frame_active_leg_bearing_rad = route_bearing;
      // l_scale = GncExecutionOdd.max_lateral_offset_m (spec §3.2/§4.3). The
      // execution-ODD ROS msg does not yet carry this field; use the spec default.
      // [TBD-HAZID] wire to the ODD msg field once published.
      inp.lateral_scale_m = 400.0;

      // ── Cross-leg guard (spec §4.3): extrapolate own_psi straight ahead ~900 m
      // (90 s horizon × ~10 m/s); if that ray reaches past the REMAINING distance
      // to the active leg end corner (active_len - s0), the NLP trajectory would
      // cross into the next L2 leg → null J_route to avoid pulling toward the
      // wrong normal. The guard uses s0 (Critical-3A): own in the back half of a
      // long leg is close to the corner and must be guarded.
      const double reach_m = std::max(
          inp.own_ship.u_mps *
              formulation_.config().n_horizon * formulation_.config().dt_s,
          900.0);
      const CrossLegGuardResult guard = evaluate_cross_leg_guard(
          proj, n_wp, inp.own_ship.psi_rad, reach_m);
      inp.route_weight = guard.crosses_corner ? 0.0 : 1.0;
    }
  } else {
    inp.planned_route_bearing_rad = 0.0;
    // No L2 route: disable J_route (no leg to return to) so it does not
    // introduce a spurious lateral setpoint. route_weight stays at its 0.0
    // default (High-4 review fix).
    inp.route_weight = 0.0;
  }

  const bool has_speed = speed_profile_ != nullptr
      && !speed_profile_->target_speeds_kn.empty();
  inp.planned_speed_mps = has_speed
      ? speed_profile_->target_speeds_kn[0] * units::kMsPerKn
      : kDefaultPlannedSpeed_mps;

  // Fix F/G (plan↔exec ROT alignment, 2026-07-03): the NLP ROT constraint must
  // respect the GNC execution yaw cap, not the vessel's physical ROT limit.
  // The vessel model (rot_max_at_18kn ≈ 12°/s) is the physical capability; GNC
  // active_route_manager enforces cruise_max_yaw_rate (1.2°/s) at execution. A
  // plan that turns at 12°/s is unexecutable — GNC rate-limits it to 1.2°/s,
  // the ship cannot follow the planned heading profile, and CPA penetrates.
  // Architecture §L4: GNC owns final (psi,u,ROT) generation; M5 plans within
  // the execution envelope. The cruise cap is the planning baseline (emergency
  // is the TailBuilder fallback floor, see gnc_odd.max_yaw_rate_rad_s).
  const double hs_m = 0.0;  // [TBD-HAZID] sea state from EnvironmentState
  (void)hs_m;  // vessel_model_.rot_max_rad_s no longer drives NLP ROT (kept for future HAZID)
  const double cruise_yaw_rad_s =
      effective_gnc_odd_().cruise_max_yaw_rate_deg_s * M_PI / 180.0;
  inp.rot_max_rad_s = std::max(cruise_yaw_rad_s, 1.0e-3);
  inp.decel_max_mps2 = std::max(effective_gnc_odd_().max_decel_mps2, 1.0e-6);

  // v2.2 §4.7 (D2): dispatch-only speed-gap flag. Computed after both
  // planned_speed_mps and decel_max_mps2 are finalized so the reachable
  // envelope reflects the GNC execution decel cap (0.20 m/s² after α1).
  // Does NOT touch NLP constraints; feeds §13.1 BC-MPC take-over condition.
  inp.speed_gap_infeasible = mass_l3::m5::compute_speed_gap_infeasible(
      inp.own_ship.u_mps, inp.planned_speed_mps, inp.decel_max_mps2,
      formulation_.config().n_horizon, formulation_.config().dt_s);
  if (inp.speed_gap_infeasible) {
    spdlog::warn("[M5][MidMPC] speed gap {:.1f} m/s exceeds N·decel_max·dt {:.1f}; flagging",
                 std::fabs(inp.own_ship.u_mps - inp.planned_speed_mps),
                 inp.decel_max_mps2 * formulation_.config().dt_s * formulation_.config().n_horizon);
  }

  // Slice C1 (spec §6): continuity H_commit prefix. Reproject the committed-route
  // prefix (frozen WGS84 geometry from the manager) to the current cycle's
  // ownship NED origin and back-infer the per-step psi/u the NLP equality rows
  // pin (spec §6.2). K is derived from the GNC guard distance (§6.3), NOT from a
  // heartbeat. K=0 on first commit (no committed prefix) or when the suffix
  // would shrink below K_suffix_min.
  inp.own_lat_deg = own_lat;
  inp.own_lon_deg = own_lon;
  const auto& committed_prefix =
      committed_route_manager_.current().committed_prefix;
  if (!committed_prefix.empty()) {
    const PrefixPsiU pp = reproject_committed_prefix(
        committed_prefix, own_lat, own_lon,
        inp.own_ship.psi_rad, inp.own_ship.u_mps,
        formulation_.config().dt_s, formulation_.config().n_horizon);
    inp.prefix_active_k = pp.K;
    inp.prefix_psi_rad = std::move(pp.psi_rad);
    inp.prefix_u_mps = std::move(pp.u_mps);
  }

  inp.stamp_ns = this->get_clock()->now().nanoseconds();
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
  return inp;
}

// ===========================================================================
// on_solve_cycle_
// ===========================================================================
void MidMpcNode::on_solve_cycle_()
{
  if (!has_required_inputs_()) {
    spdlog::warn("[M5][MidMPC] Skipping cycle: missing required inputs");
    return;
  }

  const MidMpcInput input = assemble_input_();
  publish_trajectory_candidates_(input);
  const bool is_transit =
      behavior_plan_->behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT;
  const bool is_recovery =
      behavior_plan_->behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_RECOVERY;
  const bool wrapped_heading_window = !is_transit && !is_recovery
      && mass_l3::m5::heading_window_is_wrapped(
          input.constraints.heading_min_rad, input.constraints.heading_max_rad);
  formulation_.set_constraint_inputs(input.constraints);
  formulation_.build_symbolic_graph();
  const MidMpcSolution* warm = last_solution_.has_value() ? &last_solution_.value() : nullptr;
  MidMpcSolution sol;
  if (wrapped_heading_window) {
    sol.status = MidMpcSolution::Status::Infeasible;
    sol.stamp_ns = input.stamp_ns;
  } else {
    sol = solver_.solve(input, warm);
    last_solution_ = sol;
  }

  // v2.2 §13.1: broadcast consecutive_failures so BC-MPC (Phase E2) can decide
  // take-over. Published every cycle (best-effort); BC-MPC caches atomically and
  // treats a stale/missing value as 0. The counter is maintained inside solve().
  {
    std_msgs::msg::UInt64 failures_msg;
    failures_msg.data = static_cast<std::uint64_t>(
        solver_.consecutive_failures() > 0 ? solver_.consecutive_failures() : 0);
    pub_consecutive_failures_->publish(failures_msg);
  }

  // v2.2 §13.1: BC-MPC take-over signal. kThreshold = 3 (synced with
  // committed_route's escalation threshold, committed_route.cpp). When the NLP
  // solver is stuck (consecutive_failures >= 3), signal the committed_route
  // manager to follow BC-MPC instead of holding a stale NLP corridor (§13.2).
  // The flag is cleared on the next successful (nlp_ok) revise.
  //
  // Dispatch OR condition (spec §13.1): take-over = consecutive>=3 OR
  // minalt_box_infeasible OR speed_gap_infeasible. The latter two can trigger on
  // the FIRST solve (consecutive=0): minalt_box_infeasible when the M4-published
  // heading box upper < own+min_alt (ship physically cannot reach min_alt inside
  // its directional envelope), speed_gap_infeasible when the speed gap exceeds
  // N·decel_max·dt. Both are architectural infeasibilities — holding a stale NLP
  // corridor would be wrong, so dispatch immediately.
  constexpr int64_t kBcMpcTakeoverThreshold = 3;
  const bool bc_mpc_should_take_over = mass_l3::m5::compute_bc_mpc_take_over(
      solver_.consecutive_failures(), kBcMpcTakeoverThreshold,
      solver_.last_minalt_box_infeasible(), input.speed_gap_infeasible);  // v2.2 §13.1 OR
  if (bc_mpc_should_take_over) {
    if (!committed_route_manager_.bc_mpc_takeover_requested()) {
      spdlog::warn("[M5][MidMPC] BC-MPC take-over signaled (consecutive={}, box_infeas={}, speed_infeas={})",
                   solver_.consecutive_failures(),
                   solver_.last_minalt_box_infeasible(),
                   input.speed_gap_infeasible);
    }
    committed_route_manager_.mark_bc_mpc_takeover();
  }
  // Phase 2.2 (R1, spec v2.3 §13.5): every cycle, push the SOLVER counter to
  // the committed_route manager so should_enter_degraded_hold can escalate on
  // it even when the optimized try_revise path is not reached (plan.status=
  // DEGRADED → corridor branch). Without this, persistent NLP Infeasible
  // never crossed the escalation threshold through the in-class commit
  // counter alone (it only increments inside optimized try_revise).
  committed_route_manager_.notify_solver_consecutive_failures(
      static_cast<std::uint32_t>(solver_.consecutive_failures()));


  const double lat = world_state_->own_ship.position.latitude;
  const double lon = world_state_->own_ship.position.longitude;

  // Use geometric fallback when NLP solver fails or M4 signals starboard requirement.
  // Solver is a D3.1 stub (Phase 3); geometric arc is the DEMO-1 bridge.
  const bool solver_failed = (sol.status != MidMpcSolution::Status::Converged)
      || sol.trajectory.empty();
  const bool m4_geometric =
      behavior_plan_->rationale.find("geometric starboard") != std::string::npos
      || behavior_plan_->rationale.find("fallback") != std::string::npos;
  bool nlp_misses_colregs_target = false;
  std::string nlp_reject_reason;
  if (!solver_failed && input.colregs_conflict_active) {
    const auto tail_gate = mass_l3::m5::accept_tail_gate(sol, input);
    nlp_misses_colregs_target = !tail_gate.accepted;
    nlp_reject_reason = tail_gate.reason;
    if (nlp_misses_colregs_target) {
      spdlog::warn("[M5][TailGate] reject optimized NLP candidate: {}", nlp_reject_reason);
      // Phase 1.4 (G-M5-3, spec v2.3 §15): audit the tail-gate reject so the
      // reject reason is recoverable from /l3/asdr/record alone. terminal_cpa
      // is the achieved CPA from the NLP terminal state against the primary
      // tail-gate target (the same value computed inside tail_gate_cpa_release_clear).
      const auto* primary_tgt = mass_l3::m5::primary_tail_gate_target(input);
      const double terminal_cpa_m = (primary_tgt != nullptr)
          ? mass_l3::m5::trajectory_terminal_state_cpa_m(sol, *primary_tgt)
          : 0.0;
      const std::string target_id = (primary_tgt != nullptr)
          ? std::to_string(primary_tgt->id)
          : std::string{};
      emit_tail_gate_rejected_asdr_(
          this->get_clock()->now(),
          nlp_reject_reason.empty() ? std::string{"nlp_tail_gate_failed"} : nlp_reject_reason,
          /*plan_id=*/"",  // plan_id is assigned downstream in wp_gen.generate
          terminal_cpa_m,
          target_id);
    }
  }

  l3_msgs::msg::AvoidancePlan plan;
  if (is_recovery) {
    // Phase 4: M4 RECOVERY — gradual return-to-route. Bypass NLP solver and
    // emit a direct XTE-decay trajectory toward the route projection.
    plan = build_recovery_plan_(input, lat, lon);
  } else if (is_transit) {
    // D-DEMO1 spin fix: M4 is the COLREG authority on whether avoidance is
    // active. When M4 is in TRANSIT, emit an EMPTY plan (no waypoints) so the
    // execution bridge releases avoidance and resumes route-following. Without
    // this the geometric fallback below keeps a VALID plan alive forever (the
    // NLP solver is a Phase-3 stub that never converges → solver_failed always
    // true), trapping the bridge in avoidance → endless circling, no return.
    plan.schema_version = 116;
    plan.status     = "NORMAL";
    plan.rationale  = "M4 TRANSIT — no avoidance required";
    plan.confidence = 1.0F;
    // waypoints left empty → bridge has_valid_plan == False → avoidance released
  } else if (wrapped_heading_window || solver_failed || m4_geometric ||
             nlp_misses_colregs_target) {
    std::string reason;
    if (wrapped_heading_window) {
      reason = "wrapped_heading_window";
    } else if (solver_failed) {
      reason = "solver_status=" + std::to_string(static_cast<int>(sol.status));
    } else if (nlp_misses_colregs_target) {
      reason = nlp_reject_reason.empty() ? "nlp_tail_gate_failed" : nlp_reject_reason;
    } else {
      reason = "m4_geometric";
    }
    plan = build_geometric_fallback_plan_(input, lat, lon, reason);
  } else {
    plan = wp_gen_.generate(sol, lat, lon);
  }
  publish_outputs_(sol, plan);
  // Slice A: publish the committed route on /l3/m5/avoidance_plan (the only M5
  // execution-truth topic). Release authority lives here (spec D4): while M6
  // reports conflict we keep a rolling avoidance plan; on conflict-clear we
  // keep publishing the same return intent briefly so GNC update guards cannot
  // drop the only release message.
  publish_committed_route_(this->get_clock()->now(), input, lat, lon, plan, sol);
}

// ===========================================================================
// build_geometric_fallback_plan_
// ===========================================================================
l3_msgs::msg::AvoidancePlan MidMpcNode::build_geometric_fallback_plan_(
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg,
    const std::string& reason)
{
  // Use nominal speed if current speed is too low to be meaningful
  const double target_speed_kn = mass_l3::m5::geometric_fallback_target_speed_kn(
      input.planned_speed_mps, nominal_speed_kn_, input.constraints.speed_max_mps);
  const double u_mps = (input.own_ship.u_mps > 0.5)
      ? input.own_ship.u_mps
      : (target_speed_kn * units::kMsPerKn);

  const double own_psi = input.own_ship.psi_rad;

  // R12.B superseded: magnitude from M6 minimum alteration (route-relative), clamped to window.
  const double h_min = input.constraints.heading_min_rad;
  const double h_max = input.constraints.heading_max_rad;
  const double route_brg = input.planned_route_bearing_rad;
  double min_alt_rad = input.colregs_min_alteration_rad;
  min_alt_rad = mass_l3::m5::fallback_min_alteration_rad(
      route_brg, h_min, h_max, min_alt_rad);
  const auto fallback_direction = mass_l3::m5::risk_aware_fallback_direction(input);
  double target_psi = mass_l3::m5::risk_aware_fallback_target_heading(
      input, route_brg, h_min, h_max, min_alt_rad, fallback_direction);

  const double delta_psi = mass_l3::m5::geometric_fallback_delta_heading_rad(
      own_psi, target_psi);
  const double turn_radius_m = mass_l3::m5::geometric_fallback_turn_radius_m(
      u_mps, input.rot_max_rad_s);

  constexpr int kNWp = 10;

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
  plan.stamp = this->get_clock()->now();
  plan.horizon_s = static_cast<float>(
      mass_l3::m5::geometric_fallback_waypoint_time_s(kNWp - 1));
  plan.status = "DEGRADED";
  plan.confidence = 0.6f;
  plan.rationale = "M5 geometric COLREG fallback (" + reason + ")"
      + " turn_r=" + std::to_string(static_cast<int>(turn_radius_m)) + "m"
      + " tgt=" + std::to_string(static_cast<int>(target_psi * units::kDegPerRad)) + "deg"
      + " risk_dir=" + std::to_string(static_cast<int>(fallback_direction));

  double prev_xN = 0.0;
  double prev_xE = 0.0;

  for (int i = 0; i < kNWp; ++i) {
    const double t = mass_l3::m5::geometric_fallback_waypoint_time_s(i);
    const auto point = mass_l3::m5::geometric_fallback_arc_point(
        own_psi, target_psi, u_mps, input.rot_max_rad_s, t);
    const double xN = point.x_m;
    const double xE = point.y_m;

    // Segment distance from previous waypoint
    const double ddN = xN - prev_xN;
    const double ddE = xE - prev_xE;
    const double seg_dist = std::sqrt(ddN * ddN + ddE * ddE);
    prev_xN = xN;
    prev_xE = xE;

    // Flat-earth NED → WGS84 (same approximation as WaypointGenerator::ned_to_geopoint_)
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.schema_version = 112;
    wp.position.latitude  = lat0_deg
        + (xN / units::kEarthRadiusMean_m) * units::kDegPerRad;
    wp.position.longitude = lon0_deg
        + (xE / (units::kEarthRadiusMean_m
                 * std::cos(lat0_deg * units::kRadPerDeg)))
        * units::kDegPerRad;
    wp.position.altitude  = 0.0;
    wp.wp_distance_m      = seg_dist;
    wp.safety_corridor_m  = 500.0;
    wp.target_speed_kn    = target_speed_kn;
    wp.turn_radius_m      = turn_radius_m;
    wp.confidence         = 0.6f;
    wp.rationale          = "M5 geometric COLREG fallback";

    plan.waypoints.push_back(wp);
  }

  spdlog::info("[M5][GeoFallback] {} wps: turn_r={:.0f}m tgt_psi={:.1f}deg "
               "own_psi={:.1f}deg delta={:.1f}deg aggression={:.4f} reason={}",
               kNWp, turn_radius_m,
               target_psi * units::kDegPerRad,
               own_psi * units::kDegPerRad,
               delta_psi * units::kDegPerRad,
               min_alt_rad * units::kDegPerRad,
               reason);

  return plan;
}

// ===========================================================================
// build_recovery_plan_ — Phase 4 gradual return-to-route (architecture §7.2)
// ===========================================================================
l3_msgs::msg::AvoidancePlan MidMpcNode::build_recovery_plan_(
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg)
{
  const double target_speed_kn = mass_l3::m5::geometric_fallback_target_speed_kn(
      input.planned_speed_mps, nominal_speed_kn_, input.constraints.speed_max_mps);
  const double speed_mps = target_speed_kn * units::kMsPerKn;

  constexpr int kNWp = 6;
  const double horizon_s =
      mass_l3::m5::geometric_fallback_waypoint_time_s(kNWp - 1);

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
  plan.stamp = this->get_clock()->now();
  plan.horizon_s = static_cast<float>(horizon_s);
  plan.status = "RECOVERY";
  plan.confidence = 0.8f;
  plan.rationale = "M5 RECOVERY gradual return to planned route"
      " xte=" + std::to_string(static_cast<int>(input.route_xte_m)) + "m";

  double prev_xN = 0.0;
  double prev_xE = 0.0;
  for (int i = 0; i < kNWp; ++i) {
    const double t = mass_l3::m5::geometric_fallback_waypoint_time_s(i);
    const auto point = mass_l3::m5::recovery_route_point(
        input.planned_route_bearing_rad,
        input.route_xte_m,
        speed_mps,
        t,
        horizon_s);

    const double ddN = point.x_m - prev_xN;
    const double ddE = point.y_m - prev_xE;
    const double seg_dist = std::sqrt(ddN * ddN + ddE * ddE);
    prev_xN = point.x_m;
    prev_xE = point.y_m;

    // Flat-earth NED → WGS84 (same approximation as geometric fallback).
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.schema_version = 112;
    wp.position.latitude  = lat0_deg
        + (point.x_m / units::kEarthRadiusMean_m) * units::kDegPerRad;
    wp.position.longitude = lon0_deg
        + (point.y_m / (units::kEarthRadiusMean_m
                         * std::cos(lat0_deg * units::kRadPerDeg)))
        * units::kDegPerRad;
    wp.position.altitude  = 0.0;
    wp.wp_distance_m      = seg_dist;
    wp.safety_corridor_m  = 500.0;
    wp.target_speed_kn    = target_speed_kn;
    wp.turn_radius_m      = mass_l3::m5::geometric_fallback_turn_radius_m(
        speed_mps, input.rot_max_rad_s);
    wp.confidence         = 0.8f;
    wp.rationale          = "M5 RECOVERY return-to-route";
    plan.waypoints.push_back(wp);
  }

  spdlog::info("[M5][Recovery] {} wps: xte={:.0f}m route_brg={:.1f}deg speed={:.1f}kn",
               kNWp, input.route_xte_m,
               input.planned_route_bearing_rad * units::kDegPerRad,
               target_speed_kn);

  return plan;
}

// ===========================================================================
// publish_outputs_
// ===========================================================================
void MidMpcNode::publish_outputs_(const MidMpcSolution& sol,
                                   const l3_msgs::msg::AvoidancePlan& plan)
{
  const auto now = this->get_clock()->now();

  // Slice A: /l3/m5/avoidance_plan is now the event-driven committed-route
  // execution truth. publish_outputs_ keeps ASDR/SAT audit only; route snapshots
  // are emitted by publish_avoidance_plan_ when geometry changes or heartbeat expires.

  const std::string planner_health =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.status == "DEGRADED" ? "GEOMETRIC_FALLBACK" :
       (plan.waypoints.empty() ? "EMPTY_TRANSIT" : "SOLVER_CONVERGED"));
  const std::string semantic_mode =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.waypoints.empty() ? "TRANSIT" : "AVOIDANCE");
  std::string fallback_reason = "none";
  if (plan.status == "DEGRADED") {
    const std::string marker = "fallback (";
    const auto begin = plan.rationale.find(marker);
    if (begin != std::string::npos) {
      const auto reason_begin = begin + marker.size();
      const auto reason_end = plan.rationale.find(')', reason_begin);
      fallback_reason = plan.rationale.substr(
          reason_begin,
          reason_end == std::string::npos ? std::string::npos : reason_end - reason_begin);
    } else {
      fallback_reason = "unknown";
    }
  }

  // Phase 3.4 (spec v2.3 §4): NLP diagnostic fields. cpa_slack > 0 means
  // the maneuver could not fully open CPA inside the horizon and σ softened
  // a hard-infeasibility window — the marker that distinguishes "tuned
  // green" (σ always active) from "genuine fix" (σ zero except close-range).
  char slack_buf[32];
  std::snprintf(slack_buf, sizeof(slack_buf), "%.3f", sol.cpa_slack);
  const std::string json =
      std::string("{\"status\":\"") + plan.status
      + "\",\"planner_health\":\"" + planner_health
      + "\",\"semantic_mode\":\"" + semantic_mode
      + "\",\"fallback_reason\":\"" + fallback_reason
      + "\",\"waypoints\":"  + std::to_string(plan.waypoints.size())
      + ",\"solve_ms\":"     + std::to_string(sol.solve_duration_ms)
      + ",\"ipopt_iter\":"   + std::to_string(sol.ipopt_iterations)
      + ",\"solver_status\":" + std::to_string(static_cast<int>(sol.status))
      + ",\"cpa_slack\":"    + slack_buf
      + "}";

  l3_msgs::msg::ASDRRecord record;
  record.stamp         = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "avoid_wp";
  record.decision_json = json;
  const auto digest = mass_l3::m5::common::sha256(json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);

  l3_msgs::msg::SATData sat;
  sat.stamp                   = now;
  sat.source_module           = "M5_Tactical_Planner";
  sat.sat2.trigger_reason     = "mpc_cycle";
  // Phase 3.4: append σ diagnostic to reasoning_chain so the audit trail
  // shows whether NLP softened a hard-infeasibility window this cycle.
  std::string slack_diagnostic;
  if (sol.cpa_slack > 1.0e-6) {
    slack_diagnostic = "; nlp_slack_active=" + std::string(slack_buf)
        + " (CPA floor softened — close-range hard-infeasibility window)";
  } else {
    slack_diagnostic = "; nlp_slack=0 (CPA floor compliant)";
  }
  sat.sat2.reasoning_chain    =
      plan.rationale + "; planner_health=" + planner_health
      + "; semantic_mode=" + semantic_mode
      + "; fallback_reason=" + fallback_reason + slack_diagnostic;
  sat.sat2.system_confidence  = plan.confidence;
  pub_sat_data_->publish(sat);
}

// ===========================================================================
// Phase 1.4 (G-M5-2/3, spec v2.3 §15): audit-trail emitters for the
// committed-route reject and tail-gate reject paths. Previously these
// rejections only surfaced as RCLCPP_WARN + in-memory safety_concern_event,
// so the V2.3 phase 3b probe needed container docker logs to recover the
// per-cycle reject reason (optimized_committed_rejected × 790,
// committed_route_rejected × 375, nlp_consecutive_failures_ge_3 × 784).
// Publishing each rejection as its own ASDR decision_type puts the audit
// trail on the same bus as the rest of M5's decisions so future debugging
// can be done from /l3/asdr/record alone.
// ===========================================================================
void MidMpcNode::emit_committed_route_rejected_asdr_(
    rclcpp::Time now,
    const std::string& reason,
    const std::string& safety_concern_event,
    const std::string& lifecycle_state_name,
    std::uint32_t consecutive_nlp_failures,
    const std::string& plan_id) {
  l3_msgs::msg::ASDRRecord record;
  record.stamp = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "committed_route_rejected";
  record.decision_json =
      std::string("{\"reason\":\"") + reason
      + "\",\"safety_concern_event\":\"" + safety_concern_event
      + "\",\"lifecycle_state\":\"" + lifecycle_state_name
      + "\",\"consecutive_nlp_failures\":" + std::to_string(consecutive_nlp_failures)
      + ",\"plan_id\":\"" + plan_id + "\"}";
  record.confidence = 0.0F;
  record.rationale = std::string{"committed route rejected ("} + reason + ")";
  const auto digest = mass_l3::m5::common::sha256(record.decision_json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);
}

void MidMpcNode::emit_tail_gate_rejected_asdr_(
    rclcpp::Time now,
    const std::string& reject_reason,
    const std::string& plan_id,
    double terminal_cpa_m,
    const std::string& target_id) {
  l3_msgs::msg::ASDRRecord record;
  record.stamp = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "tail_gate_rejected";
  // Fixed-point formatting avoids the locale-dependent exponent form so the
  // ASDR JSON stays grep-friendly.
  char cpa_buf[32];
  std::snprintf(cpa_buf, sizeof(cpa_buf), "%.1f", terminal_cpa_m);
  record.decision_json =
      std::string("{\"reject_reason\":\"") + reject_reason
      + "\",\"plan_id\":\"" + plan_id
      + "\",\"terminal_cpa_m\":" + cpa_buf
      + ",\"target_id\":\"" + target_id + "\"}";
  record.confidence = 0.0F;
  record.rationale = std::string{"NLP tail-gate rejected ("} + reject_reason + ")";
  const auto digest = mass_l3::m5::common::sha256(record.decision_json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);
}

void MidMpcNode::emit_tail_builder_rejected_asdr_(
    rclcpp::Time now,
    const std::string& reject_reason,
    const std::string& plan_id) {
  l3_msgs::msg::ASDRRecord record;
  record.stamp = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "tail_builder_rejected";
  record.decision_json =
      std::string("{\"reject_reason\":\"") + reject_reason
      + "\",\"plan_id\":\"" + plan_id + "\"}";
  record.confidence = 0.0F;
  // Phase 3.8: distinguish TailBuilder geometry failure from NLP tail-gate
  // failure. The NLP solver may have converged (nlp_tail_gate_failed=false);
  // only the geometric tail extension failed. Honest degradation per
  // spec §14.3 (amended): the optimized body still commits.
  record.rationale = std::string{"TailBuilder geometry rejected ("} + reject_reason
      + "); NLP solver verdict unaffected";
  const auto digest = mass_l3::m5::common::sha256(record.decision_json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);
}

void MidMpcNode::emit_empty_plan_handoff_asdr_(
    rclcpp::Time now,
    const std::string& reason,
    const std::string& plan_id,
    const std::string& plan_status) {
  l3_msgs::msg::ASDRRecord record;
  record.stamp = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "gnc_empty_plan_nack";
  record.decision_json =
      std::string("{\"reason\":\"") + reason
      + "\",\"plan_id\":\"" + plan_id
      + "\",\"plan_status\":\"" + plan_status
      + "\",\"note\":\"GNC active_route_manager requires >=2 waypoints; empty plan will be silently dropped\"}";
  record.confidence = 0.0F;
  record.rationale = std::string{"empty avoidance_plan hand-off ("} + reason + ")";
  const auto digest = mass_l3::m5::common::sha256(record.decision_json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);
}


// ===========================================================================
// publish_avoidance_plan_ — Slice A committed-route execution truth
// ===========================================================================
void MidMpcNode::publish_avoidance_plan_(
    const l3_msgs::msg::AvoidancePlan& plan,
    const std::string& reason)
{
  const auto now = this->get_clock()->now();
  l3_msgs::msg::AvoidancePlan out = plan;
  out.schema_version = 116;
  out.stamp = now;
  out.route_hash = mass_l3::m5::avoidance_route_hash(out);

  const bool route_changed = !last_published_route_hash_.has_value()
      || last_published_route_hash_.value() != out.route_hash;
  const bool heartbeat_due = !last_avoidance_plan_publish_time_.has_value()
      || ((now - last_avoidance_plan_publish_time_.value()).seconds() >= kAvoidancePlanHeartbeat_s);

  if (!route_changed && !heartbeat_due) {
    return;
  }

  // Heartbeat refreshes valid_until without forcing a new revision/hash.
  out.valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
  // Phase 1.4 (G-GNC-1, spec v2.3 §15): GNC active_route_manager silently
  // rejects plans with fewer than 2 waypoints. A NORMAL status with empty
  // waypoints is the legitimate M4-TRANSIT release signal (bridge sees
  // has_valid_plan==False → avoidance released). Any OTHER status with empty
  // waypoints (DEGRADED / BcMpcFollow / keep_last empty) is a hand-off GNC
  // will drop without acknowledgement — audit it so the silent drop is
  // visible from /l3/asdr/record.
  if (out.waypoints.empty() && out.status != "NORMAL") {
    emit_empty_plan_handoff_asdr_(now, reason, out.plan_id, out.status);
  }
  pub_avoidance_plan_->publish(out);
  last_published_route_hash_ = out.route_hash;
  last_avoidance_plan_publish_time_ = now;
  RCLCPP_INFO(get_logger(),
      "[M5][AvoidancePlan] publish reason=%s changed=%d heartbeat=%d points=%zu hash=%u",
      reason.c_str(), route_changed ? 1 : 0, heartbeat_due ? 1 : 0,
      out.latitude.size(), out.route_hash);
}

// ===========================================================================
// publish_keep_last_ — Slice B Keep-Last heartbeat fallback (spec §9.10/§9.12)
//
// Called by publish_committed_route_ on every GNC-preflight or committed_route
// rejection path. Without this, those paths silently returned and the 60s
// heartbeat went silent for the entire encounter (rule14-ho trace showed only
// 2 publishes over 1470 s). This helper always reaches publish_avoidance_plan_
// so the heartbeat refreshes valid_until and GNC keeps a route snapshot.
//
// Source: committed_route_manager_.current().active_geometry (last accepted
// committed route). If empty (no prior commit), emits a minimal DEGRADED plan
// with no waypoints so the heartbeat still fires and downstream sees the
// keep-last marker rather than a missing topic.
// ===========================================================================
void MidMpcNode::publish_keep_last_(rclcpp::Time now, const std::string& reason) {
  // v2.2 §13.2: BcMpcFollow must NOT republish a stale NLP corridor (Codex
  // integration blocker 2). When the committed route has transitioned to
  // BcMpcFollow, BC-MPC owns the maneuver via ReactiveOverrideCmd (架构 §L4)
  // and drives L4 directly. Republishing the last committed active_geometry
  // here would resurrect a stale corridor that the NLP could not make feasible
  // — exactly the failure that triggered the take-over. So emit an empty
  // BcMpcFollow-status heartbeat (bridge sees no valid NLP plan; BC-MPC
  // override is the active path) and return BEFORE touching active_geometry.
  if (committed_route_manager_.current().state ==
      mass_l3::m5::committed_route::LifecycleState::BcMpcFollow) {
    l3_msgs::msg::AvoidancePlan bc_plan;
    bc_plan.schema_version = 116;
    bc_plan.stamp = now;
    bc_plan.status = "BcMpcFollow";
    bc_plan.confidence = 0.0F;
    bc_plan.command_source = "m5_bcmpc_override";
    bc_plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_BCMPC_FOLLOW;
    bc_plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
    bc_plan.nlp_tail_gate_failed = true;
    bc_plan.rationale =
        "BC-MPC take-over active; NLP corridor suppressed (v2.2 §13.2, " +
        reason + ")";
    bc_plan.plan_id = "m5_bcmpc_follow";
    bc_plan.parent_route_id = "nominal";
    bc_plan.behavior_mode = "collision_avoidance";
    bc_plan.valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
    // waypoints left empty — release NLP corridor; BC-MPC override drives L4.
    spdlog::warn("[M5][CommittedRoute] BcMpcFollow - suppress stale corridor publish (reason={})",
                 reason);
    // Phase 1.4 (G-M5-2): audit the BcMpcFollow suppress path too — it is
    // where takeover signalled but no NLP corridor is published (the R4
    // chain-break from the V2.3 audit).
    emit_committed_route_rejected_asdr_(
        now,
        reason,
        committed_route_manager_.current().safety_concern_event,
        lifecycle_state_name(committed_route_manager_.current().state),
        committed_route_manager_.consecutive_nlp_failures(),
        bc_plan.plan_id);
    publish_avoidance_plan_(bc_plan, std::string{"bcmpc_follow:"} + reason);
    return;  // CRITICAL: skip the stale active_geometry republish below.
  }

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
  plan.stamp = now;
  plan.status = "DEGRADED";
  plan.confidence = 0.5F;
  plan.command_source = "m5_keep_last";
  plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_KEEP_LAST;
  plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
  plan.nlp_tail_gate_failed = true;
  plan.rationale = std::string{"keep_last ("} + reason + ")";
  plan.valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);

  const auto& committed = committed_route_manager_.current();
  if (!committed.active_geometry.empty()) {
    plan.plan_id = committed.plan_id.empty() ? std::string{"m5_keep_last"} : committed.plan_id;
    plan.parent_route_id = "nominal";
    plan.behavior_mode = "collision_avoidance";
    plan.latitude.reserve(committed.active_geometry.size());
    plan.longitude.reserve(committed.active_geometry.size());
    plan.command_speed_mps.reserve(committed.active_geometry.size());
    plan.navigation_mode.reserve(committed.active_geometry.size());
    for (const auto& wp : committed.active_geometry) {
      plan.latitude.push_back(wp.lat_deg);
      plan.longitude.push_back(wp.lon_deg);
      plan.command_speed_mps.push_back(wp.speed_mps);
      plan.navigation_mode.push_back(wp.nav_mode.empty() ? std::string{"collision_avoidance"} : wp.nav_mode);
    }
    plan.waypoints.clear();
    plan.waypoints.reserve(plan.latitude.size());
    for (std::size_t i = 0u; i < plan.latitude.size(); ++i) {
      const double speed = i < plan.command_speed_mps.size() ? plan.command_speed_mps[i] : 0.0;
      plan.waypoints.push_back(waypoint_from_route_point(
          plan.latitude[i], plan.longitude[i], speed, plan.confidence, plan.rationale));
    }
  } else {
    plan.plan_id = "m5_keep_last_empty";
    plan.parent_route_id = "nominal";
    plan.behavior_mode = "collision_avoidance";
    RCLCPP_WARN(get_logger(),
        "[M5][KeepLast] no prior committed route; publishing empty DEGRADED heartbeat reason=%s",
        reason.c_str());
  }
  // Phase 1.4 (G-M5-2, spec v2.3 §15): audit the keep-last path so the
  // reject reason lands on the ASDR bus alongside M5's other decisions. The
  // V2.3 phase 3b probe had to scrape container logs to recover why each
  // candidate was rejected; this puts the same evidence in /l3/asdr/record.
  emit_committed_route_rejected_asdr_(
      now,
      reason,
      committed_route_manager_.current().safety_concern_event,
      lifecycle_state_name(committed_route_manager_.current().state),
      committed_route_manager_.consecutive_nlp_failures(),
      plan.plan_id);
  publish_avoidance_plan_(plan, std::string{"keep_last:"} + reason);
}

// ===========================================================================
// publish_trajectory_candidates_ — DEMO-2 P0: SAT3Data with Nomoto fallback
// ===========================================================================
void MidMpcNode::publish_trajectory_candidates_(const MidMpcInput& input)
{
  auto sol = nomoto_fallback_.solve(input);
  const auto now = this->get_clock()->now();

  l3_msgs::msg::SAT3Data sat3;
  sat3.stamp = now;
  sat3.schema_version = 112;

  const std::size_t n_candidates = sol.trajectories.size();
  for (std::size_t i = 0; i < n_candidates; ++i) {
    l3_msgs::msg::TrajectoryCandidate tc;
    tc.stamp = now;
    tc.confidence = 1.0F;
    tc.branch_index = static_cast<int32_t>(i);
    tc.heading_deg = sol.headings_rad[i] * 180.0 / M_PI;
    tc.speed_kn = input.own_ship.u_mps * 1.94384;
    tc.cpa_m = sol.cpa_vals[i];
    tc.rule_compliant = false;
    tc.is_primary = (i == static_cast<std::size_t>(sol.primary_branch_idx));

    for (const auto& pt : sol.trajectories[i]) {
      geometry_msgs::msg::Point p;
      p.x = pt.x();
      p.y = pt.y();
      p.z = 0.0;
      tc.waypoints.push_back(p);
    }

    sat3.trajectory_candidates[i] = tc;
  }
  sat3.primary_trajectory_idx = static_cast<uint8_t>(sol.primary_branch_idx);

  pub_sat3_data_->publish(sat3);
}

// ===========================================================================
// append_tail_waypoints_ (Slice W1, spec §5.3)
// ===========================================================================
std::string MidMpcNode::append_tail_waypoints_(
    l3_msgs::msg::AvoidancePlan& plan,
    const MidMpcInput& input,
    const MidMpcSolution& sol,
    double lat0_deg,
    double lon0_deg)
{
  // The NLP terminal state (last trajectory point) is the tail's anchor. spec
  // §5.3: pN ← NLP terminal position. unpack_solution() already dead-reckoned
  // the trajectory positions (propagate_trajectory_positions, types.hpp), so
  // sol.trajectory.back() carries the true NLP terminal x/y. Use it directly —
  // do NOT re-accumulate: re-summing N intervals yields pos[N] (one step beyond
  // the terminal pos[N-1]) because the propagation sets point[k].pos BEFORE
  // advancing, so back() is the last command's position (Review High-3 off-by-one).
  if (sol.trajectory.empty()) {
    return "tail_empty_trajectory";
  }

  const auto& term = sol.trajectory.back();
  const double pN_n_m = term.x_m;
  const double pN_e_m = term.y_m;

  // Only give-way encounters produce a tail; stand-on/free have no rejoin need.
  const tb::ColregRole role = (colregs_constraint_ != nullptr)
      ? tail_role_from_m6(colregs_constraint_->primary_role)
      : tb::ColregRole::Free;
  const tb::ColregSide protected_side = tail_protected_side_from_preferred(
      input.colregs_preferred_direction);
  if (role == tb::ColregRole::StandOn || role == tb::ColregRole::Free ||
      protected_side == tb::ColregSide::NONE) {
    // No tail expected for this role/side — not a gate failure, just a no-op.
    return {};
  }

  // M6 lifecycle (spec §5.2): the only fields that decide active vs release.
  const bool m6_past_clear = (colregs_constraint_ != nullptr) && colregs_constraint_->past_clear;
  const bool m6_release_predicted =
      (colregs_constraint_ != nullptr) && colregs_constraint_->release_predicted;
  const std::uint8_t m6_encounter_state = (colregs_constraint_ != nullptr)
      ? tail_encounter_state_from_m6(colregs_constraint_->encounter_state)
      : static_cast<std::uint8_t>(tb::EncounterState::Active);

  // M2 target snapshots for CPA gating / s_clear extrapolation.
  std::vector<tb::TargetSnapshot> targets;
  targets.reserve(input.targets.size());
  for (const auto& tgt : input.targets) {
    tb::TargetSnapshot snap;
    snap.id = tgt.id;
    snap.cpa_m = tgt.cpa_m;
    snap.tcpa_s = tgt.tcpa_s;
    snap.cpa_sigma_m = tgt.cpa_sigma_m;
    snap.relative_bearing_deg = 0.0;
    targets.push_back(snap);
  }

  // GNC execution-ODD → TailBuilder kinematic limits. The ODD msg does not yet
  // carry every field; fill spec defaults and override the available ones.
  // [TBD-HAZID] wire remaining fields once the ODD msg publishes them.
  const auto odd_msg = effective_gnc_odd_();
  tb::GncExecutionOdd gnc_odd;
  gnc_odd.ship_length_m = 50.0;
  gnc_odd.max_lateral_offset_m = input.lateral_scale_m > 0.0 ? input.lateral_scale_m : 400.0;
  gnc_odd.min_segment_length_m = 50.0;
  gnc_odd.min_turn_radius_m = std::max(odd_msg.emergency_min_turn_radius_m, 1.0);
  gnc_odd.max_yaw_rate_rad_s = std::max(
      odd_msg.emergency_max_yaw_rate_deg_s * units::kRadPerDeg, 1.0e-3);
  gnc_odd.max_lateral_accel_mps2 = std::max(odd_msg.max_lateral_accel_mps2, 1.0e-3);
  gnc_odd.max_decel_mps2 = std::max(odd_msg.max_decel_mps2, 1.0e-3);

  tb::TailInputs tail_inp;
  tail_inp.role = role;
  tail_inp.pN = tb::GeoWP{pN_n_m, pN_e_m, term.u_mps, "MID_MPC"};
  tail_inp.psiN_rad = term.psi_rad;
  tail_inp.uN_mps = term.u_mps;
  tail_inp.protected_side = protected_side;
  tail_inp.m6_past_clear = m6_past_clear;
  tail_inp.m6_encounter_state = m6_encounter_state;
  tail_inp.m6_release_predicted = m6_release_predicted;
  tail_inp.route_frame = tail_route_frame_from_l2(planned_route_, lat0_deg, lon0_deg);
  tail_inp.targets = std::move(targets);
  tail_inp.cpa_release_m = input.constraints.cpa_hard_m > 0.0
      ? input.constraints.cpa_hard_m : 1852.0;
  tail_inp.cpa_safe_m = input.constraints.cpa_safe_m;
  tail_inp.gnc_odd = gnc_odd;

  const auto tail_result = tb::TailBuilder::build(tail_inp);
  if (!tail_result.hold_then_rejoin.has_value()) {
    // TailBuilder declined (terminal state not extendable, s_clear unavailable,
    // etc.). Honest degradation (spec §14.3): caller marks nlp_tail_gate_failed
    // and falls back to DegradedHold rather than publishing a broken tail.
    return tail_result.reject_reason.empty() ? std::string("nlp_tail_gate_failed")
                                             : tail_result.reject_reason;
  }

  // Append the NED tail waypoints to the AvoidancePlan parallel arrays. They
  // convert back to lat/lon (flat-earth, matching the generator) and carry the
  // TailBuilder's source labels (MID_MPC_TERMINAL_HOLD [+ REJOIN_TO_L2]).
  const auto& segment = tail_result.hold_then_rejoin.value();
  const std::string navigation_mode = plan.behavior_mode.empty()
      ? std::string("collision_avoidance") : plan.behavior_mode;
  for (std::size_t i = 0u; i < segment.waypoints.size(); ++i) {
    const auto& wp = segment.waypoints[i];
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    tail_ned_to_latlon(wp.x_m, wp.y_m, lat0_deg, lon0_deg, lat_deg, lon_deg);
    plan.latitude.push_back(lat_deg);
    plan.longitude.push_back(lon_deg);
    plan.command_speed_mps.push_back(wp.speed_mps);
    plan.navigation_mode.push_back(navigation_mode);
    plan.segment_source.push_back(segment.source_labels[i]);
  }

  return {};
}

// ===========================================================================
// publish_committed_route_ (Slice A: /l3/m5/avoidance_plan is canonical truth)
// Emits the committed avoidance route on /l3/m5/avoidance_plan — the only M5
// execution-truth topic. The gnc_bridge translates it to /colav/avoidance_plan
// for GNC active_route_manager_node. Release authority (spec D4): while M6
// reports conflict we emit one encounter-anchored avoidance corridor; on the
// conflict->clear transition we emit a stable current-anchored rejoin corridor,
// then stop emitting.
//
// Slice B (heartbeat discipline): every path MUST reach publish_avoidance_plan_
// at the end. Preflight failures or committed_route rejections no longer
// silently return — they fall through to publish a Keep-Last DEGRADED plan so
// the 60s heartbeat (spec §9.10) keeps refreshing valid_until and GNC never
// loses the only release message.
// ===========================================================================
void MidMpcNode::publish_committed_route_(
    rclcpp::Time now,
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg,
    const l3_msgs::msg::AvoidancePlan& selected_plan,
    const MidMpcSolution& sol) {
  const bool collision_avoidance_authorized =
      behavior_plan_ != nullptr &&
      mass_l3::m5::should_emit_collision_avoidance_waypoints(
          input.colregs_conflict_active, behavior_plan_->behavior);
  const bool conflict_active = collision_avoidance_authorized;

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
  plan.stamp = now;
  plan.command_source = "m5_committed_route";
  plan.confidence = 0.8F;
  // Phase 2.4 (G-M5-1): default to UNSPECIFIED; each branch below sets the
  // real value before publish_avoidance_plan_ emits.
  plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_UNSPECIFIED;
  // Slice D owns keep-last stale transitions. Slice A emits fresh route snapshots,
  // so zero is the explicit non-stale/unset value.
  plan.stale_committed_at.sec = 0;
  plan.stale_committed_at.nanosec = 0;

  const bool return_republish_active = return_to_route_emit_until_.has_value() &&
      ((*return_to_route_emit_until_ - now).seconds() > 0.0);

  if (input.colregs_conflict_active && !collision_avoidance_authorized) {
    avoidance_corridor_anchor_.reset();
    return_route_anchor_.reset();
    return_to_route_emit_until_.reset();
    last_emitted_conflict_active_ = false;
    return;
  }

  if (conflict_active && selected_plan.status == "NORMAL" && !selected_plan.waypoints.empty()) {
    return_to_route_emit_until_.reset();
    return_route_anchor_.reset();
    avoidance_corridor_anchor_.reset();
    plan = selected_plan;
    populate_canonical_route_from_selected_plan(
        plan,
        sol,
        "m5-midmpc-" + std::to_string(now.nanoseconds()),
        "nominal",
        mass_l3::m5::gnc_avoidance_navigation_mode(/*colregs_overtake_corridor=*/false));
    plan.valid_until = (now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s));
    // Slice W1 (spec §5.3): append the TailBuilder hold[+rejoin] segment between
    // the MID_MPC_OPTIMIZED waypoints and the L2 nominal suffix. The tail
    // extends the NLP terminal state to the predicted s_clear (active phase,
    // hold-only) or to a curvature-limited rejoin (release phase).
    //
    // Phase 3.8 (spec §14.3 amended): TailBuilder geometry rejection (e.g.
    // tail_spacing_invalid) is honest degradation that does NOT affect the NLP
    // solver verdict. The legacy code set plan.nlp_tail_gate_failed=true here,
    // which made committed_candidate_from_plan pass candidate.nlp_ok=false to
    // try_revise, escalating NLP-converged candidates into DegradedHold on
    // every cycle where the tail geometry failed (135 spurious escalations on
    // rule14-ho). The optimized body still commits — the NLP solver's
    // convergence verdict (populate_canonical_route_from_selected_plan sets
    // nlp_tail_gate_failed from sol.status, line 75) is authoritative.
    const std::string tail_reject = append_tail_waypoints_(plan, input, sol, lat0_deg, lon0_deg);
    if (!tail_reject.empty()) {
      spdlog::warn("[M5][TailBuilder] reject tail for plan_id={} reason={}",
                   plan.plan_id, tail_reject);
      emit_tail_builder_rejected_asdr_(
          now, tail_reject, plan.plan_id);
      plan.rationale += " tail_gate=" + tail_reject;
    }
    if (!append_l2_nominal_suffix_if_preflight_feasible(
            plan, planned_route_, {lat0_deg, lon0_deg}, input.planned_speed_mps)) {
      RCLCPP_WARN(get_logger(),
          "[M5][GNCPreflight] reject L2 nominal suffix for optimized plan_id=%s; publishing selected route without suffix",
          plan.plan_id.c_str());
    }
    // Phase 2: plan.waypoints[0] is anchor (MidMpcWaypointGenerator).
    const auto preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg}, /*wps_has_anchor=*/true);
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible optimized plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          plan.plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "optimized_preflight_failed");
      return;
    }
    // Spec §6.6.2/§6.6.4: build risk context from M2 WorldState (via the
    // assembled input targets) for the Keep-Last risk gate + frozen prefix.
    // Both M2 predicted CPA (tgt.cpa_m) and current geometric range
    // (hypot(tgt.x_m, tgt.y_m)) are collected; the commit gate uses the
    // CURRENT range (see committed_candidate_from_plan above, Codex fix).
    CommittedCandidateRiskContext risk_ctx;
    risk_ctx.own_lat_deg = lat0_deg;
    risk_ctx.own_lon_deg = lon0_deg;
    risk_ctx.own_psi_rad = input.own_ship.psi_rad;
    for (const auto& tgt : input.targets) {
      const double current_range_m = std::hypot(tgt.x_m, tgt.y_m);
      if (current_range_m < risk_ctx.min_target_current_range_m) {
        risk_ctx.min_target_current_range_m = current_range_m;
        risk_ctx.min_target_cpa_m = tgt.cpa_m;  // telemetry / future candidate-CPA
        risk_ctx.primary_target_cog_rad = tgt.cog_rad;
        risk_ctx.has_target = true;
      }
    }
    // Phase 2.1/2.3 (R2/R6): forward target closing speed + candidate
    // terminal CPA so risk_trigger_event can apply the tail-gate-aligned
    // floor. closing_speed comes from M2 TargetRiskSnapshot (primary target);
    // terminal CPA is the NLP solution's achieved CPA against that target.
    if (const auto* risk = mass_l3::m5::primary_target_risk(input)) {
      risk_ctx.primary_target_closing_speed_mps = risk->closing_speed_mps;
    }
    if (const auto* primary_tgt = mass_l3::m5::primary_tail_gate_target(input)) {
      risk_ctx.candidate_terminal_cpa_m =
          mass_l3::m5::trajectory_terminal_state_cpa_m(sol, *primary_tgt);
    }
    if (!committed_route_manager_.try_revise(
            committed_candidate_from_plan(plan, !plan.nlp_tail_gate_failed, (now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s)).seconds(), risk_ctx),
            now.seconds(),
            static_cast<std::uint32_t>(solver_.consecutive_failures()))) {
      RCLCPP_WARN(get_logger(),
          "[M5][CommittedRoute] reject optimized candidate plan_id=%s event=%s",
          plan.plan_id.c_str(), committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "optimized_committed_rejected");
      return;
    }
    // Phase 2.4 (G-M5-1): optimized branch committed successfully.
    plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_OPTIMIZED;
  } else if (conflict_active) {
    return_to_route_emit_until_.reset();
    return_route_anchor_.reset();
    const bool colregs_overtake_corridor =
        mass_l3::m5::requires_colregs_overtake_corridor(
            input.colregs_conflict_active, input.constraints.applicable_rules);
    const double heading_min_deg =
        input.constraints.heading_min_rad / units::kRadPerDeg;
    const double heading_max_deg =
        input.constraints.heading_max_rad / units::kRadPerDeg;
    const double command_speed_mps =
        mass_l3::m5::gnc_avoidance_command_speed_mps(
            input.planned_speed_mps, colregs_overtake_corridor);
    const std::string navigation_mode =
        mass_l3::m5::gnc_avoidance_navigation_mode(colregs_overtake_corridor);
    const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
        input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
    const bool need_new_anchor =
        !avoidance_corridor_anchor_.has_value()
        || avoidance_corridor_anchor_->direction != input.colregs_preferred_direction;
    if (need_new_anchor) {
      avoidance_corridor_anchor_ = AvoidanceCorridorAnchor{
          lat0_deg,
          lon0_deg,
          heading_min_deg,
          heading_max_deg,
          command_speed_mps,
          route_frame.bearing_rad,
          route_frame.reversed ? -1.0 : 1.0,
          input.colregs_preferred_direction,
          colregs_overtake_corridor,
          "m5-colregs-" + std::to_string(now.nanoseconds())};
    }

    const auto& anchor = avoidance_corridor_anchor_.value();
    // Keep one encounter-anchored avoidance corridor active until M6 clears.
    // GNC follows waypoint geometry; regenerating from current position each cycle
    // makes XTE look healthy while diluting the global COLREG maneuver.
    plan.behavior_mode = navigation_mode;
    plan.parent_route_id = "nominal";
    plan.plan_id = anchor.plan_id;
    // W4-B: convert live targets to local NED relative to the corridor anchor.
    // TargetState.x_m/y_m are NED relative to own; the corridor anchor is fixed
    // at conflict onset, so shift each target by own-relative-to-anchor.
    std::vector<mass_l3::m5::TargetTrackPoint> target_tracks;
    target_tracks.reserve(input.targets.size());
    const double m_per_deg_lon_anchor =
        mass_l3::m5::kMetersPerDegLat * std::cos(anchor.lat_deg * M_PI / 180.0);
    const double own_n_rel_anchor =
        (lat0_deg - anchor.lat_deg) * mass_l3::m5::kMetersPerDegLat;
    const double own_e_rel_anchor =
        (lon0_deg - anchor.lon_deg) * m_per_deg_lon_anchor;
    for (const auto& tgt : input.targets) {
      const double tgt_n = own_n_rel_anchor + tgt.x_m;
      const double tgt_e = own_e_rel_anchor + tgt.y_m;
      target_tracks.push_back({tgt_n, tgt_e, tgt.cog_rad, tgt.sog_mps});
    }

    constexpr double kRule13OvertakeTaperStartM = 7500.0;
    constexpr double kRule13OvertakeTaperEndM = 12000.0;
    const auto wps = colregs_overtake_corridor
        ? mass_l3::m5::generate_rule13_overtake_corridor_waypoints(
            anchor.heading_min_deg,
            anchor.heading_max_deg,
            anchor.lat_deg,
            anchor.lon_deg,
            anchor.route_bearing_rad,
            anchor.direction,
            kRule13OvertakeTaperStartM,
            kRule13OvertakeTaperEndM)
        : mass_l3::m5::generate_target_safe_corridor_waypoints(
            anchor.heading_min_deg,
            anchor.heading_max_deg,
            anchor.lat_deg,
            anchor.lon_deg,
            anchor.route_bearing_rad,
            anchor.direction,
            target_tracks,
            /*own_n=*/0.0,
            /*own_e=*/0.0);
    if (!colregs_overtake_corridor && !target_tracks.empty()) {
      double corridor_max_east = 0.0;
      const double m_per_deg_lon_wps =
          mass_l3::m5::kMetersPerDegLat * std::cos(anchor.lat_deg * M_PI / 180.0);
      for (const auto& w : wps) {
        corridor_max_east = std::max(
            corridor_max_east, (w.lon - anchor.lon_deg) * m_per_deg_lon_wps);
      }
      RCLCPP_INFO(get_logger(),
          "[M5][W4] target-safe corridor: %zu targets, corridor peak east=%.0fm (default cap 270m)",
          target_tracks.size(), corridor_max_east);
    }
    const std::vector<double> speeds(wps.size(), anchor.command_speed_mps);
    // Phase 2: corridor wps[0] is now anchor (kDistancesM[0]=0.0).
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds,
        mass_l3::m5::GncAvoidancePreflightConfig{}, /*wps_has_anchor=*/true);
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible avoidance plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          anchor.plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "degraded_preflight_failed");
      return;
    }
    DegradedCandidateRequest degraded_request;
    degraded_request.plan_id = anchor.plan_id;
    degraded_request.parent_route_id = "nominal";
    degraded_request.behavior_mode = navigation_mode;
    degraded_request.confidence = 0.8F;
    degraded_request.rationale = colregs_overtake_corridor
        ? "Rule13 overtake corridor candidate"
        : "encounter-anchored avoidance corridor candidate";
    degraded_request.nlp_unavailable = selected_plan.status == "DEGRADED" ||
        sol.status != MidMpcSolution::Status::Converged;
    degraded_request.committed_route_can_continue =
        !committed_route_manager_.current().active_geometry.empty() &&
        !committed_route_manager_.should_enter_degraded_hold(now.seconds());
    degraded_request.has_return_to_route_point = false;
    degraded_request.safety_concern_event = "m5_degraded_corridor_no_return_route";
    degraded_request.points.reserve(wps.size());
    for (std::size_t i = 0; i < wps.size(); ++i) {
      degraded_request.points.push_back(
          DegradedCandidatePoint{wps[i].lat, wps[i].lon, speeds[i], navigation_mode});
    }
    const auto valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
    const auto degraded_plan = build_committed_degraded_candidate_plan(
        degraded_request, committed_route_manager_, now.seconds(), valid_until.seconds());
    if (!degraded_plan.has_value()) {
      RCLCPP_WARN(get_logger(),
          "[M5][DegradedCandidate] drop avoidance plan_id=%s reason=committed_route_rejected event=%s",
          anchor.plan_id.c_str(), committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "degraded_committed_rejected");
      return;
    }
    plan = degraded_plan.value();
    plan.stamp = now;
    plan.valid_until = valid_until;
    // Phase 2.4 (G-M5-1): encounter-anchored corridor fallback.
    plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_CORRIDOR;
  } else if (last_emitted_conflict_active_ || return_republish_active) {
    if (last_emitted_conflict_active_) {
      return_to_route_emit_until_ =
          now + rclcpp::Duration::from_seconds(kReturnToRouteRepublishWindow_s);
      const bool colregs_overtake_rejoin =
          avoidance_corridor_anchor_.has_value() &&
          avoidance_corridor_anchor_->colregs_overtake_corridor;
      const double command_speed_mps =
          mass_l3::m5::gnc_return_command_speed_mps(
              input.planned_speed_mps, colregs_overtake_rejoin);
      const std::string navigation_mode =
          mass_l3::m5::gnc_return_navigation_mode(colregs_overtake_rejoin);
      double return_route_bearing_rad = input.planned_route_bearing_rad;
      double return_route_xte_m = input.route_xte_m;
      if (avoidance_corridor_anchor_.has_value()) {
        return_route_bearing_rad = avoidance_corridor_anchor_->route_bearing_rad;
        return_route_xte_m = avoidance_corridor_anchor_->route_xte_sign * input.route_xte_m;
      } else {
        const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
            input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
        return_route_bearing_rad = route_frame.bearing_rad;
        return_route_xte_m = route_frame.route_xte_m;
      }
      const auto return_waypoints = mass_l3::m5::generate_return_to_route_waypoints(
          lat0_deg, lon0_deg, return_route_bearing_rad, return_route_xte_m);
      return_route_anchor_ = ReturnRouteAnchor{
          return_waypoints,
          command_speed_mps,
          navigation_mode,
          "m5-return-" + std::to_string(now.nanoseconds())};
    }
    avoidance_corridor_anchor_.reset();
    if (!return_route_anchor_.has_value()) {
      const double command_speed_mps =
          mass_l3::m5::gnc_return_command_speed_mps(
              input.planned_speed_mps, /*colregs_overtake_rejoin=*/false);
      const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
          input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
      return_route_anchor_ = ReturnRouteAnchor{
          mass_l3::m5::generate_return_to_route_waypoints(
              lat0_deg, lon0_deg, route_frame.bearing_rad, route_frame.route_xte_m),
          command_speed_mps,
          mass_l3::m5::gnc_return_navigation_mode(/*colregs_overtake_rejoin=*/false),
          "m5-return-" + std::to_string(now.nanoseconds())};
    }
    // conflict -> clear transition: repeat return_to_route briefly so the GNC
    // route-update guard cannot drop the only lifecycle-release message.
    plan.behavior_mode             = "return_to_route";
    plan.parent_route_id           = "nominal";
    plan.plan_id                   = return_route_anchor_->plan_id;
    plan.has_return_to_route_point = true;
    const auto& wps = return_route_anchor_->waypoints;
    const std::vector<double> speeds(wps.size(), return_route_anchor_->command_speed_mps);
    // Phase 2 exception: return path WP[0] is a real 500m maneuver target
    // (generate_return_to_route_waypoints), NOT an anchor. Keep has_anchor=false.
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {lat0_deg, lon0_deg}, wps, speeds);
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible return plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          return_route_anchor_->plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "return_preflight_failed");
      return;
    }
    DegradedCandidateRequest degraded_return_request;
    degraded_return_request.plan_id = return_route_anchor_->plan_id;
    degraded_return_request.parent_route_id = "nominal";
    degraded_return_request.behavior_mode = "return_to_route";
    degraded_return_request.confidence = 0.8F;
    degraded_return_request.rationale =
        return_route_anchor_->navigation_mode == "colregs_overtake"
            ? "Rule13 return_to_route candidate on M6 conflict-clear"
            : "return_to_route candidate on M6 conflict-clear";
    degraded_return_request.nlp_unavailable = selected_plan.status == "DEGRADED" ||
        sol.status != MidMpcSolution::Status::Converged;
    degraded_return_request.committed_route_can_continue =
        !committed_route_manager_.current().active_geometry.empty() &&
        !committed_route_manager_.should_enter_degraded_hold(now.seconds());
    degraded_return_request.has_return_to_route_point = true;
    degraded_return_request.return_latitude = wps.back().lat;
    degraded_return_request.return_longitude = wps.back().lon;
    degraded_return_request.points.reserve(wps.size());
    for (std::size_t i = 0; i < wps.size(); ++i) {
      degraded_return_request.points.push_back(DegradedCandidatePoint{
          wps[i].lat,
          wps[i].lon,
          speeds[i],
          return_route_anchor_->navigation_mode});
    }
    const auto valid_until = now + rclcpp::Duration::from_seconds(kReturnToRouteRepublishWindow_s);
    const auto degraded_return_plan = build_committed_degraded_candidate_plan(
        degraded_return_request, committed_route_manager_, now.seconds(), valid_until.seconds());
    if (!degraded_return_plan.has_value()) {
      RCLCPP_WARN(get_logger(),
          "[M5][DegradedCandidate] drop return plan_id=%s reason=committed_route_rejected event=%s",
          return_route_anchor_->plan_id.c_str(),
          committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "return_committed_rejected");
      return;
    }
    plan = degraded_return_plan.value();
    plan.stamp = now;
    plan.valid_until = valid_until;
    // Phase 2.4 (G-M5-1): M6 conflict-clear return-to-route.
    plan.commit_branch = l3_msgs::msg::AvoidancePlan::COMMIT_BRANCH_RETURN;
  } else {
    // No conflict and already returned: emit nothing this cycle.
    last_emitted_conflict_active_ = conflict_active;
    return;
  }

  if (plan.status != "NORMAL") {
    if (plan.status != "DEGRADED" &&
        !append_l2_nominal_suffix_if_preflight_feasible(
            plan, planned_route_, {lat0_deg, lon0_deg}, input.planned_speed_mps)) {
      RCLCPP_WARN(get_logger(),
          "[M5][GNCPreflight] reject L2 nominal suffix for plan_id=%s; publishing preflighted base route without suffix",
          plan.plan_id.c_str());
    }
    // Phase 2: plan.waypoints[0] is anchor.
    const auto full_preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg}, /*wps_has_anchor=*/true);
    if (!full_preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible full route plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          plan.plan_id.c_str(), full_preflight.reason.c_str(), full_preflight.index,
          full_preflight.required_m, full_preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      publish_keep_last_(now, "full_preflight_failed");
      return;
    }
    plan.status = (plan.behavior_mode == "return_to_route") ? "RECOVERY" : "DEGRADED";
    plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
    plan.nlp_kkt_residual = 0.0F;
    plan.nlp_tail_gate_failed = (plan.behavior_mode != "return_to_route");
    mass_l3::m5::apply_tail_gate_publish_contract(input, plan);
  }
  plan.waypoints.clear();
  plan.waypoints.reserve(plan.latitude.size());
  for (std::size_t i = 0; i < plan.latitude.size(); ++i) {
    const double speed = i < plan.command_speed_mps.size() ? plan.command_speed_mps[i] : 0.0;
    plan.waypoints.push_back(waypoint_from_route_point(
        plan.latitude[i], plan.longitude[i], speed, plan.confidence, plan.rationale));
  }

  // Slice A: /l3/m5/avoidance_plan is the canonical execution truth — the only
  // route M5 publishes. The legacy /l3/m5/avoidance_waypoints shadow topic was
  // removed (no execution consumer; only sil_trace_writer subscribed).
  publish_avoidance_plan_(plan, plan.behavior_mode);
  last_emitted_conflict_active_ = conflict_active;
}

void MidMpcNode::on_scenario_loaded_(const std_msgs::msg::String::SharedPtr msg) {
  RCLCPP_INFO(get_logger(),
      "scenario_loaded '%s' — resetting MPC warm state", msg->data.c_str());
  reset_cross_run_state();
}

ship_interfaces::msg::GncExecutionOdd MidMpcNode::effective_gnc_odd_() const {
  std::lock_guard<std::mutex> lk(gnc_odd_mutex_);
  if (!latest_gnc_odd_.schema_version.empty()) {
    return latest_gnc_odd_;
  }
  // Fallback to hardcoded defaults matching gnc_avoidance_preflight.hpp when no
  // live ODD msg has arrived yet (e.g. unit-test build, or gnc_bridge not up).
  ship_interfaces::msg::GncExecutionOdd fallback;
  fallback.emergency_avoidance_speed_cap_mps = 3.2;
  fallback.cruise_min_speed_mps = 3.8;
  fallback.max_transit_speed_mps = 3.0;
  fallback.max_lateral_accel_mps2 = 0.25;
  fallback.max_decel_mps2 = 0.20;  // v2.2 §4.7: aligned with GNC ship_config 0.20 baseline
  fallback.emergency_min_turn_radius_m = 45.0;
  fallback.cruise_max_yaw_rate_deg_s = 1.2;
  fallback.emergency_max_yaw_rate_deg_s = 2.0;
  return fallback;
}

void MidMpcNode::reset_cross_run_state() {
  // Drop the warm-start solution so the next scenario cold-starts the solver
  // instead of inheriting the prior run's MPC trajectory. Also reset the
  // ranking history (accumulated risk-ranking state).
  last_solution_.reset();
  risk_ranking_state_ = mass_l3::risk::RankingState{};
  last_emitted_conflict_active_ = false;
  return_to_route_emit_until_.reset();
  avoidance_corridor_anchor_.reset();
  return_route_anchor_.reset();
  last_published_route_hash_.reset();
  last_avoidance_plan_publish_time_.reset();
}

}  // namespace mass_l3::m5::mid_mpc
