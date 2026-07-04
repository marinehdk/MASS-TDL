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
    const double now_s)
{
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
    if (consecutive_nlp_failures_ >= 3U) {
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
            "(consecutive_nlp_failures={}, bc_mpc_takeover=false)",
            consecutive_nlp_failures_);
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
  if (consecutive_nlp_failures_ >= 3U) {
    // v2.2 §13.2: same KeepLast-policy revision as try_revise — at the escalation
    // threshold, follow BC-MPC if it has taken over, otherwise DegradedHold.
    if (bc_mpc_takeover_requested_) {
      current_.state = LifecycleState::BcMpcFollow;
      current_.safety_concern_event = "bc_mpc_takeover_active";
    } else {
      enter_degraded_hold("nlp_consecutive_failures_ge_3_no_bcmpc");
    }
    return true;
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
  // Hard-block gates (Codex review 2026-07-03): only keep gates that are
  // sound as COMMIT REJECTION criteria. A candidate commit should be blocked
  // only when the CURRENT own↔target geometry is already inside the hard
  // floor (current_cpa < cpa_hard) — meaning the situation is already unsafe
  // regardless of the candidate route.
  //
  // REMOVED from this block path (WRONG ABSTRACTION):
  //   - target_heading_change_gt_15deg: a target maneuver is a reason to
  //     REPLAN / invalidate the keep-last snapshot, not to reject a FRESH
  //     candidate that may already account for the new target heading.
  //   - cpa_drift_gt_20pct: same — drift invalidates the stale keep-last, not
  //     a fresh candidate.
  // Both still have advisory slots in should_enter_degraded_hold() (via the
  // target_heading_trigger_ / cpa_drift_trigger_ members); wiring those from
  // the candidate fields is future work (they currently stay false).
  if (candidate.current_cpa_m < candidate.cpa_hard_m) {
    return "current_cpa_below_hard_floor";
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
