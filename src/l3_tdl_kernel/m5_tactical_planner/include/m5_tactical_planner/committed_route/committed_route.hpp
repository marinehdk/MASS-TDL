#ifndef MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_ROUTE_HPP_
#define MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_ROUTE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mass_l3::m5::committed_route {

enum class LifecycleState : std::uint8_t {
  Idle = 0U,
  CandidateEvaluating = 1U,
  Committed = 2U,
  HeartbeatOnly = 3U,
  KeepLast = 4U,
  Stale = 5U,
  DegradedHold = 6U,
  Released = 7U,
  BcMpcFollow = 8U,  // v2.2 §13.2: BC-MPC take-over, committed_route 跟随 BC-MPC
};

// Phase 1.4 (spec v2.3 §15): stable name for ASDR decision_json. Pinned so
// audit consumers can group by lifecycle state without depending on the
// numeric enum value (which changes when states are inserted).
inline const char* lifecycle_state_name(LifecycleState s) noexcept {
  switch (s) {
    case LifecycleState::Idle:               return "Idle";
    case LifecycleState::CandidateEvaluating:return "CandidateEvaluating";
    case LifecycleState::Committed:          return "Committed";
    case LifecycleState::HeartbeatOnly:      return "HeartbeatOnly";
    case LifecycleState::KeepLast:           return "KeepLast";
    case LifecycleState::Stale:              return "Stale";
    case LifecycleState::DegradedHold:       return "DegradedHold";
    case LifecycleState::Released:           return "Released";
    case LifecycleState::BcMpcFollow:        return "BcMpcFollow";
  }
  return "Unknown";
}

// GeoWP holds WGS84 waypoint coordinates in DEGREES (spec §3.7 coordinate
// contract). lat_deg/lon_deg replace the legacy x_m/y_m NED-metre fields,
// which were a semantic mismatch — committed_candidate_from_plan always filled
// them with plan.latitude/longitude (degrees). NED-metre projections are
// performed in conversion helpers at point of use and are NOT persisted here.
struct GeoWP {
  double lat_deg{0.0};   // WGS84 latitude  [deg]
  double lon_deg{0.0};   // WGS84 longitude [deg]
  double speed_mps{0.0};
  std::string nav_mode;
};

struct CommittedRouteCandidate {
  std::string plan_id;
  std::vector<GeoWP> geometry;
  std::size_t frozen_prefix_count{0U};
  double valid_until_s{0.0};
  bool nlp_ok{true};
  double target_heading_delta_deg{0.0};
  double cpa_drift_fraction{0.0};
  double current_cpa_m{1.0e9};
  double cpa_hard_m{0.0};
};

struct CommittedAvoidanceRouteState {
  std::string plan_id;
  std::uint32_t revision{0U};
  std::uint32_t route_hash{0U};
  double stale_committed_at_s{0.0};
  double valid_until_s{0.0};
  LifecycleState state{LifecycleState::Idle};
  std::vector<GeoWP> committed_prefix;
  std::vector<GeoWP> active_geometry;
  std::string safety_concern_event;
};

class CommittedAvoidanceRoute {
 public:
  explicit CommittedAvoidanceRoute(double stale_route_max_age_s = 45.0);

  [[nodiscard]] const CommittedAvoidanceRouteState& current() const;
  [[nodiscard]] bool try_revise(const CommittedRouteCandidate& candidate, double now_s);
  [[nodiscard]] bool heartbeat(const std::string& plan_id, double valid_until_s, double now_s);
  [[nodiscard]] bool should_enter_degraded_hold(double now_s);
  [[nodiscard]] std::uint32_t consecutive_nlp_failures() const;

  // v2.2 §13.1/§13.2: signal that BC-MPC has taken over (MidMpcNode dispatch
  // calls this when solver consecutive_failures >= kThreshold=3). While set, the
  // escalation branch (consecutive_nlp_failures_ >= 3) routes the route into
  // LifecycleState::BcMpcFollow instead of DegradedHold — BC-MPC owns the
  // maneuver, committed_route follows its ReactiveOverrideCmd and does NOT keep a
  // stale NLP corridor alive. Cleared on a successful revise (nlp_ok candidate).
  void mark_bc_mpc_takeover() { bc_mpc_takeover_requested_ = true; }
  [[nodiscard]] bool bc_mpc_takeover_requested() const { return bc_mpc_takeover_requested_; }

 private:
  [[nodiscard]] std::uint32_t hash_geometry(const std::vector<GeoWP>& geometry) const;
  [[nodiscard]] bool preflight_candidate(const CommittedRouteCandidate& candidate) const;
  [[nodiscard]] bool preserves_committed_prefix(const std::vector<GeoWP>& geometry) const;
  [[nodiscard]] std::string risk_trigger_event(const CommittedRouteCandidate& candidate) const;
  void reject_keep_last(const std::string& safety_concern_event);
  void enter_degraded_hold(const std::string& safety_concern_event);

  CommittedAvoidanceRouteState current_;
  double stale_route_max_age_s_{45.0};
  std::uint32_t consecutive_nlp_failures_{0U};
  bool target_heading_trigger_{false};
  bool cpa_drift_trigger_{false};
  bool cpa_hard_trigger_{false};
  bool bc_mpc_takeover_requested_{false};  // v2.2 §13.1/§13.2
};

}  // namespace mass_l3::m5::committed_route

#endif  // MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_ROUTE_HPP_
