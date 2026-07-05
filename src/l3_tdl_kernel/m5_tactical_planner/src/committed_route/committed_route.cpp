#include "m5_tactical_planner/committed_route/committed_route.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace mass_l3::m5::committed_route {
namespace {

void fnv1a_update(std::uint32_t& hash, const void* data, const std::size_t size)
{
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0U; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
}

void fnv1a_update_double(std::uint32_t& hash, const double value)
{
  static_assert(sizeof(double) == sizeof(std::uint64_t));
  std::uint64_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(double));
  fnv1a_update(hash, &bits, sizeof(bits));
}

void fnv1a_update_string(std::uint32_t& hash, const std::string& value)
{
  fnv1a_update(hash, value.data(), value.size());
  const char nul = '\0';
  fnv1a_update(hash, &nul, sizeof(nul));
}

// Spec §3.7: same_waypoint uses tolerance comparison for the frozen-prefix
// freeze test. WGS84 degree values survive float reprojection with sub-1e-7
// jitter, so an exact double compare almost never matches. Tolerances:
//   lat/lon |Δ| < 1e-7 deg (≈ 1 cm), speed |Δ| < 0.01 m/s, nav_mode exact.
// Exact double comparison of lat/lon is forbidden.
bool same_waypoint(const GeoWP& lhs, const GeoWP& rhs)
{
  constexpr double kLatLonTolDeg = 1e-7;  // ≈ 1 cm
  constexpr double kSpeedTolMps = 0.01;
  return std::fabs(lhs.lat_deg - rhs.lat_deg) < kLatLonTolDeg &&
         std::fabs(lhs.lon_deg - rhs.lon_deg) < kLatLonTolDeg &&
         std::fabs(lhs.speed_mps - rhs.speed_mps) < kSpeedTolMps &&
         lhs.nav_mode == rhs.nav_mode;
}

bool valid_nav_mode(const std::string& nav_mode)
{
  return nav_mode == "MID_MPC_OPTIMIZED" || nav_mode == "MID_MPC_TERMINAL_HOLD" ||
         nav_mode == "REJOIN_TO_L2" || nav_mode == "L2_NOMINAL_SUFFIX" ||
         nav_mode == "DEGRADED_CORRIDOR";
}

}  // namespace

CommittedAvoidanceRoute::CommittedAvoidanceRoute(const double stale_route_max_age_s)
    : stale_route_max_age_s_(stale_route_max_age_s)
{
}

const CommittedAvoidanceRouteState& CommittedAvoidanceRoute::current() const
{
  return current_;
}

bool CommittedAvoidanceRoute::try_revise(
    const CommittedRouteCandidate& candidate,
    const double now_s,
    const std::uint32_t solver_consecutive_failures)
{
  // Phase 2.2 (R1, spec v2.3 §13.5): cache the latest solver counter so
  // should_enter_degraded_hold (no caller-supplied solver counter) can
  // escalate on the same value try_revise used. Updated unconditionally so
  // a fresh solver success clears the cache even when this candidate is
  // rejected for other reasons.
  last_solver_consecutive_failures_ = solver_consecutive_failures;
  const bool was_degraded_hold = current_.state == LifecycleState::DegradedHold;
  const std::string original_safety_concern_event = current_.safety_concern_event;
  const auto preserve_degraded_hold = [&]() {
    if (!was_degraded_hold) {
      return false;
    }
    current_.state = LifecycleState::DegradedHold;
    current_.safety_concern_event = original_safety_concern_event;
    return true;
  };

  current_.state = LifecycleState::CandidateEvaluating;

  const std::string risk_event = risk_trigger_event(candidate);
  if (!risk_event.empty()) {
    if (!candidate.nlp_ok) {
      ++consecutive_nlp_failures_;
    }
    if (!preserve_degraded_hold()) {
      enter_degraded_hold(risk_event);
    }
    return false;
  }

  if (!candidate.nlp_ok) {
    ++consecutive_nlp_failures_;
    if (preserve_degraded_hold()) {
      return false;
    }
    // Phase 2.2 (R1, spec v2.3 §13.5): escalate on the SOLVER counter (fed
    // from mid_mpc_solver.cpp via mid_mpc_node), not just the in-class commit
    // counter. The legacy gate used consecutive_nlp_failures_ alone, which
    // only incremented inside try_revise on candidate.nlp_ok=false. A steady
    // NLP Infeasible drove plan.status=DEGRADED → corridor branch → no
    // optimized try_revise call → commit counter never accumulated →
    // DegradedHold unreachable through the dominant solver-Infeasible path
    // (the V2.3 phase 3b root cause). Take the max of the two counters so
    // tail-gate-reject escalation (commit counter) still works alongside the
    // solver counter.
    const std::uint32_t escalation_failures = std::max<std::uint32_t>(
        consecutive_nlp_failures_, solver_consecutive_failures);
    if (escalation_failures >= 3U) {
      // v2.2 §13.2: KeepLast policy revision. At the escalation threshold the
      // route must NEVER hold a stale NLP corridor (SOTIF ISO 21448:2022 / IEC
      // 61508 fail-safe). If BC-MPC has taken over (§13.1), follow its maneuver;
      // otherwise enter DegradedHold + MRM-02 escalation (M7 Slice K wires the
      // real MRM; until then this is a log-critical fail-safe state).
      if (bc_mpc_takeover_requested_) {
        current_.state = LifecycleState::BcMpcFollow;
        current_.safety_concern_event = "bc_mpc_takeover_active";
      } else {
        enter_degraded_hold("nlp_consecutive_failures_ge_3_no_bcmpc");
        spdlog::critical(
            "[M5][CommittedRoute] DegradedHold + MRM-02 escalate "
            "(solver_consecutive={}, commit_consecutive={}, bc_mpc_takeover=false)",
            solver_consecutive_failures, consecutive_nlp_failures_);
      }
    } else {
      current_.state = LifecycleState::KeepLast;
    }
    return false;
  }

  if (!preflight_candidate(candidate)) {
    if (!preserve_degraded_hold()) {
      reject_keep_last("candidate_preflight_failed");
    }
    return false;
  }

  if (!preserves_committed_prefix(candidate.geometry)) {
    if (!preserve_degraded_hold()) {
      reject_keep_last("frozen_prefix_conflict");
    }
    return false;
  }

  consecutive_nlp_failures_ = 0U;
  target_heading_trigger_ = false;
  cpa_drift_trigger_ = false;
  cpa_hard_trigger_ = false;
  // v2.2 §13.2: a successful (nlp_ok) candidate clears the BC-MPC take-over —
  // the NLP solver has recovered and owns the maneuver again.
  bc_mpc_takeover_requested_ = false;

  const std::uint32_t new_hash = hash_geometry(candidate.geometry);
  const bool first_commit = current_.active_geometry.empty();
  const bool geometry_changed = first_commit || (new_hash != current_.route_hash);

  current_.plan_id = candidate.plan_id;
  current_.valid_until_s = candidate.valid_until_s;
  current_.stale_committed_at_s = now_s;
  current_.state = LifecycleState::Committed;
  current_.safety_concern_event.clear();

  if (geometry_changed) {
    current_.active_geometry = candidate.geometry;
    current_.route_hash = new_hash;
    ++current_.revision;
  }

  // Spec §6.6.3: prefix_count = requested (NOT max(existing, requested)), and
  // the prefix is recomputed on EVERY successful revise — not only when the
  // geometry changes. This is the rolling prune: when the own-ship advances and
  // the upstream candidate's frozen_prefix_count shrinks (fewer waypoints inside
  // the in-guard window), the committed_prefix must shrink too, even though the
  // geometry hash is unchanged (Critical-3 review fix). The legacy code only
  // reassigned committed_prefix inside the geometry_changed block, so a same-
  // geometry revise with a smaller frozen_prefix_count left the stale (larger)
  // prefix in place — waypoints the vessel had already overrun stayed frozen.
  // The prune count is computed upstream in committed_candidate_from_plan
  // (along-track projection, committed_candidate_geometry.hpp) and communicated
  // via frozen_prefix_count; the manager honours that count here.
  const std::size_t requested_prefix_count = std::min(
      candidate.frozen_prefix_count,
      current_.active_geometry.size());
  current_.committed_prefix.assign(
      current_.active_geometry.begin(),
      current_.active_geometry.begin() + static_cast<std::ptrdiff_t>(requested_prefix_count));

  return true;
}

bool CommittedAvoidanceRoute::heartbeat(
    const std::string& plan_id,
    const double valid_until_s,
    const double now_s)
{
  if (current_.active_geometry.empty() || current_.state == LifecycleState::DegradedHold) {
    return false;
  }
  current_.plan_id = plan_id;
  current_.valid_until_s = valid_until_s;
  current_.stale_committed_at_s = now_s;
  current_.state = LifecycleState::Committed;
  current_.safety_concern_event.clear();
  return true;
}

bool CommittedAvoidanceRoute::should_enter_degraded_hold(const double now_s)
{
  if (current_.active_geometry.empty()) {
    return false;
  }
  if (current_.state == LifecycleState::DegradedHold) {
    return true;
  }
  // v2.2 §13.2: if BC-MPC has taken over, the route stays in BcMpcFollow and is
  // NOT forced into DegradedHold by the stale/escalation gates below — BC-MPC
  // owns the maneuver. (BC-MPC failing is handled upstream by its own validity
  // expiry, which clears the takeover flag.)
  if (current_.state == LifecycleState::BcMpcFollow) {
    return false;
  }
  if ((now_s - current_.stale_committed_at_s) > stale_route_max_age_s_) {
    enter_degraded_hold("committed_route_stale_gt_45s");
    return true;
  }
  // Phase 2.2 (R1, spec v2.3 §13.5): escalate when EITHER counter crosses 3.
  // The commit counter catches persistent tail-gate rejects (optimized path
  // try_revise with candidate.nlp_ok=false); the SOLVER counter catches
  // persistent NLP Infeasible that drove plan.status=DEGRADED and never
  // reached the optimized try_revise path. Take the max so either trigger
  // fires escalation.
  {
    const std::uint32_t escalation_failures = std::max<std::uint32_t>(
        consecutive_nlp_failures_, last_solver_consecutive_failures_);
    if (escalation_failures >= 3U) {
      // v2.2 §13.2: same KeepLast-policy revision as try_revise — at the
      // escalation threshold, follow BC-MPC if it has taken over, otherwise
      // DegradedHold.
      if (bc_mpc_takeover_requested_) {
        current_.state = LifecycleState::BcMpcFollow;
        current_.safety_concern_event = "bc_mpc_takeover_active";
      } else {
        enter_degraded_hold("nlp_consecutive_failures_ge_3_no_bcmpc");
      }
      return true;
    }
  }
  if (target_heading_trigger_) {
    enter_degraded_hold("target_heading_change_gt_15deg");
    return true;
  }
  if (cpa_drift_trigger_) {
    enter_degraded_hold("cpa_drift_gt_20pct");
    return true;
  }
  if (cpa_hard_trigger_) {
    enter_degraded_hold("current_cpa_below_hard_floor");
    return true;
  }
  return false;
}

std::uint32_t CommittedAvoidanceRoute::consecutive_nlp_failures() const
{
  return consecutive_nlp_failures_;
}

std::uint32_t CommittedAvoidanceRoute::hash_geometry(const std::vector<GeoWP>& geometry) const
{
  // Exact numerical hash for geometry deduplication (spec §3.7): a false
  // positive (different geometry, same hash) is impossible by construction,
  // and a false negative (same geometry, different hash) only forces an
  // unnecessary revision bump — the safer failure mode.
  std::uint32_t hash = 2166136261u;
  for (const auto& waypoint : geometry) {
    fnv1a_update_double(hash, waypoint.lat_deg);
    fnv1a_update_double(hash, waypoint.lon_deg);
    fnv1a_update_double(hash, waypoint.speed_mps);
    fnv1a_update_string(hash, waypoint.nav_mode);
  }
  return hash;
}

bool CommittedAvoidanceRoute::preflight_candidate(const CommittedRouteCandidate& candidate) const
{
  if (candidate.geometry.empty() || !std::isfinite(candidate.valid_until_s)) {
    return false;
  }
  for (const auto& waypoint : candidate.geometry) {
    if (!std::isfinite(waypoint.lat_deg) || !std::isfinite(waypoint.lon_deg) ||
        !std::isfinite(waypoint.speed_mps) || !valid_nav_mode(waypoint.nav_mode)) {
      return false;
    }
  }
  return true;
}

bool CommittedAvoidanceRoute::preserves_committed_prefix(
    const std::vector<GeoWP>& geometry) const
{
  if (geometry.size() < current_.committed_prefix.size()) {
    return false;
  }
  for (std::size_t i = 0U; i < current_.committed_prefix.size(); ++i) {
    if (!same_waypoint(geometry[i], current_.committed_prefix[i])) {
      return false;
    }
  }
  return true;
}

std::string CommittedAvoidanceRoute::risk_trigger_event(
    const CommittedRouteCandidate& candidate) const
{
  // Phase 2.1/2.3 (R2/R6, spec v2.3 §3.2): rewrite the CPA-floor commit gate
  // to mirror tail-gate semantics (types.hpp tail_gate_cpa_release_clear).
  //
  // The legacy gate rejected any candidate whenever the CURRENT own↔target
  // range was below cpa_hard. On a rule14-ho approach that is the steady-
  // state geometry for the entire encounter (range < 1852 m long before CPA
  // opens), so the gate rejected every optimized candidate that was actually
  // the CPA-opening maneuver (optimized_committed_rejected × 790 in V2.3
  // phase 3b probe). The candidate's terminal_cpa_m — the achieved CPA from
  // the NLP terminal state — was never consulted.
  //
  // The new gate keeps the hard floor but only on the candidate's *achieved*
  // CPA, and only when the target is OPENING (release/recovery phase). When
  // the target is still closing (active approach), the maneuver IS the CPA-
  // opening action; requiring it already safe would reject every active-
  // avoidance route — exactly the tail-gate reasoning at types.hpp:824-842.
  //
  // A true MRM-fail-safe is preserved: if even the achieved terminal CPA is
  // below the hard floor AND the target is opening, the route genuinely
  // cannot save the situation — that is the only case the gate rejects.
  const bool below_hard_floor = candidate.current_cpa_m < candidate.cpa_hard_m;
  if (!below_hard_floor) {
    return "";
  }
  // Active approach: target still closing → maneuver is the CPA-opening
  // action. Skip the floor, mirror tail_gate_cpa_release_clear.
  if (!candidate.target_opening) {
    return "";
  }
  // Release/recovery: target opening but achieved terminal CPA still below
  // the hard floor → the candidate did not open enough clearance. This is
  // the genuine fail-safe case that justifies rejecting the commit.
  if (candidate.terminal_cpa_m < candidate.cpa_hard_m) {
    return "terminal_cpa_below_hard_floor_on_release";
  }
  return "";
}

void CommittedAvoidanceRoute::reject_keep_last(const std::string& safety_concern_event)
{
  if (current_.state == LifecycleState::DegradedHold) {
    return;
  }
  current_.state = LifecycleState::KeepLast;
  current_.safety_concern_event = safety_concern_event;
}

void CommittedAvoidanceRoute::enter_degraded_hold(const std::string& safety_concern_event)
{
  current_.state = LifecycleState::DegradedHold;
  current_.safety_concern_event = safety_concern_event;
}

}  // namespace mass_l3::m5::committed_route
