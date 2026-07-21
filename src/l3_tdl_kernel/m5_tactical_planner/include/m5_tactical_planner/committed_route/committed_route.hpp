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
  HandoverNeutral = 9U,  // P6: BC handing back to Mid-MPC transition — override still active
  FinalDegrade = 10U,    // P6: BC failure dual-condition met — M5 silent, M7 MRM
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
    case LifecycleState::HandoverNeutral:     return "HandoverNeutral";
    case LifecycleState::FinalDegrade:        return "FinalDegrade";
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
  // Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): risk_trigger_event previously
  // rejected every optimized candidate whenever current range < cpa_hard,
  // which on rule14-ho approach is the steady-state geometry — the gate
  // blocked the very CPA-opening maneuver it was supposed to author. The
  // fix requires knowing (a) the candidate's achieved terminal CPA, and
  // (b) whether the target is opening or closing (active approach vs
  // release/recovery). These mirror tail-gate's trajectory_terminal_state_cpa_m
  // + target_opening logic so the two gates share the same floor semantics.
  double terminal_cpa_m{1.0e9};   // achieved CPA from the NLP terminal state
  bool target_opening{false};     // true when target closing_speed_mps <= 0
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
  explicit CommittedAvoidanceRoute(
      double stale_route_max_age_s = 45.0,
      std::uint32_t bc_final_degrade_threshold = 5U,
      std::uint32_t mid_unrecovered_threshold = 3U,
      double cpa_safe_m = 1852.0);

  [[nodiscard]] const CommittedAvoidanceRouteState& current() const;
  // Phase 2.2 (R1, spec v2.3 §13.5): solver_consecutive_failures is the
  // authoritative escalation counter — it counts Mid-MPC solver Infeasible /
  // non-Converged cycles (mid_mpc_solver.cpp), incremented every cycle the
  // solver fails and reset on Converged. The legacy in-class counter only
  // incremented inside try_revise when candidate.nlp_ok=false, so a steady
  // NLP Infeasible that drove plan.status=DEGRADED (and thus skipped the
  // optimized try_revise path entirely) never accumulated — escalation was
  // unreachable (nlp_consecutive_failures_ge_3 × 784 only fired via tail-gate
  // rejects, not via the dominant solver-Infeasible path). The new parameter
  // is fed from mid_mpc_node's solver_.consecutive_failures() each cycle.
  // Default 0 preserves legacy behavior for callers/tests that don't supply it.
  [[nodiscard]] bool try_revise(
      const CommittedRouteCandidate& candidate,
      double now_s,
      std::uint32_t solver_consecutive_failures = 0U);
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

  // P6: receive BC-MPC health metrics from mid_mpc_node (forwarded from /l3/m5/bc_mpc/health)
  void notify_bc_mpc_health(std::uint32_t override_no_improve_count,
                             double worst_case_cpa_m, bool override_active);
  // P6: receive Mid-NLP converged status + BC predicted CPA for hysteresis evaluation
  void notify_handover_inputs(bool mid_converged, double bc_predicted_cpa_m);
  // P6: evaluate dual-condition FinalDegrade trigger (Condition A && Condition B)
  [[nodiscard]] bool should_enter_final_degrade() const;
  // P6: enter FinalDegrade state — irreversible, M5 silent, M7 MRM
  void enter_final_degrade();
  // P6: accessors for FinalDegrade trigger evidence (Condition A + Condition B counters).
  // Used by mid_mpc_node to build ASDR audit records when FinalDegrade is entered.
  [[nodiscard]] std::uint32_t bc_override_no_improve_count() const noexcept {
    return bc_override_no_improve_count_;
  }
  [[nodiscard]] std::uint32_t mid_unrecovered_count() const noexcept {
    return mid_unrecovered_count_;
  }
  [[nodiscard]] double bc_worst_case_cpa_m() const noexcept {
    return bc_last_worst_case_cpa_m_;
  }
  [[nodiscard]] bool mid_last_converged() const noexcept {
    return last_mid_converged_;
  }
  // Phase 2.2 (R1, spec v2.3 §13.5): per-cycle solver-failure notification so
  // should_enter_degraded_hold can escalate on the SOLVER counter even when
  // the optimized try_revise path is never reached (plan.status=DEGRADED →
  // corridor branch). Caller (mid_mpc_node) invokes this every cycle with
  // solver_.consecutive_failures().
  void notify_solver_consecutive_failures(std::uint32_t failures) {
    last_solver_consecutive_failures_ = failures;
  }

 private:
  [[nodiscard]] std::uint32_t hash_geometry(const std::vector<GeoWP>& geometry) const;
  [[nodiscard]] bool preflight_candidate(const CommittedRouteCandidate& candidate) const;
  [[nodiscard]] bool preserves_committed_prefix(const std::vector<GeoWP>& geometry) const;
  [[nodiscard]] std::string risk_trigger_event(const CommittedRouteCandidate& candidate) const;
  void reject_keep_last(const std::string& safety_concern_event);
  void enter_degraded_hold(const std::string& safety_concern_event);

  CommittedAvoidanceRouteState current_;
  double stale_route_max_age_s_{45.0};
  // P6: tunable thresholds for dual-condition FinalDegrade (spec §7.1)
  std::uint32_t bc_final_degrade_threshold_{5U};
  std::uint32_t mid_unrecovered_threshold_{3U};
  double cpa_safe_m_{1852.0};
  std::uint32_t consecutive_nlp_failures_{0U};
  // Phase 2.2 (R1, spec v2.3 §13.5): cached from the latest try_revise call so
  // should_enter_degraded_hold (which has no caller-supplied solver counter)
  // can escalate on the same SOLVER counter used by try_revise. Without this,
  // a steady NLP Infeasible that never reached the optimized try_revise path
  // (plan.status=DEGRADED → corridor branch) could not trigger DegradedHold
  // through should_enter_degraded_hold either.
  std::uint32_t last_solver_consecutive_failures_{0U};
  bool target_heading_trigger_{false};
  bool cpa_drift_trigger_{false};
  bool cpa_hard_trigger_{false};
  bool bc_mpc_takeover_requested_{false};  // v2.2 §13.1/§13.2

  // P6 hysteresis: consecutive cycles with dual-condition met before handover
  std::uint32_t handover_hysteresis_count_{0U};
  static constexpr std::uint32_t kHandoverHysteresisThreshold = 2U;

  // P6 FinalDegrade dual-condition tracking
  std::uint32_t bc_override_no_improve_count_{0U};
  double bc_last_worst_case_cpa_m_{1.0e9};
  bool bc_health_received_{false};
  std::uint32_t mid_unrecovered_count_{0U};
  bool last_mid_converged_{true};
};

}  // namespace mass_l3::m5::committed_route

#endif  // MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_ROUTE_HPP_
