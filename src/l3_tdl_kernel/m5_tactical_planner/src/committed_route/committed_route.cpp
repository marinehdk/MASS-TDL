#include "m5_tactical_planner/committed_route/committed_route.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

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
  current_.state = LifecycleState::CandidateEvaluating;
  if (!candidate.nlp_ok) {
    ++consecutive_nlp_failures_;
    current_.state = LifecycleState::KeepLast;
    return false;
  }

  consecutive_nlp_failures_ = 0U;
  target_heading_trigger_ = std::fabs(candidate.target_heading_delta_deg) > 15.0;
  cpa_drift_trigger_ = std::fabs(candidate.cpa_drift_fraction) > 0.20;
  cpa_hard_trigger_ = candidate.current_cpa_m < candidate.cpa_hard_m;

  std::vector<GeoWP> merged_geometry = candidate.geometry;
  if (!current_.active_geometry.empty()) {
    merged_geometry = merged_with_frozen_prefix(candidate.geometry);
  }

  const std::uint32_t new_hash = hash_geometry(merged_geometry);
  const bool first_commit = current_.active_geometry.empty();
  const bool geometry_changed = first_commit || (new_hash != current_.route_hash);

  current_.plan_id = candidate.plan_id;
  current_.valid_until_s = candidate.valid_until_s;
  current_.stale_committed_at_s = now_s;
  current_.state = LifecycleState::Committed;
  current_.safety_concern_event.clear();

  if (geometry_changed) {
    current_.active_geometry = std::move(merged_geometry);
    current_.route_hash = new_hash;
    ++current_.revision;
    const std::size_t prefix_count = std::min(
        candidate.frozen_prefix_count,
        current_.active_geometry.size());
    current_.committed_prefix.assign(
        current_.active_geometry.begin(),
        current_.active_geometry.begin() + static_cast<std::ptrdiff_t>(prefix_count));
  }

  return true;
}

bool CommittedAvoidanceRoute::heartbeat(
    const std::string& plan_id,
    const double valid_until_s,
    const double now_s)
{
  if (current_.active_geometry.empty()) {
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
  if ((now_s - current_.stale_committed_at_s) > stale_route_max_age_s_) {
    enter_degraded_hold("committed_route_stale_gt_45s");
    return true;
  }
  if (consecutive_nlp_failures_ >= 3U) {
    enter_degraded_hold("nlp_consecutive_failures_ge_3");
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
  std::uint32_t hash = 2166136261u;
  for (const auto& waypoint : geometry) {
    fnv1a_update_double(hash, waypoint.x_m);
    fnv1a_update_double(hash, waypoint.y_m);
    fnv1a_update_double(hash, waypoint.speed_mps);
    fnv1a_update_string(hash, waypoint.nav_mode);
  }
  return hash;
}

std::vector<GeoWP> CommittedAvoidanceRoute::merged_with_frozen_prefix(
    const std::vector<GeoWP>& candidate_geometry) const
{
  std::vector<GeoWP> merged = candidate_geometry;
  const std::size_t prefix_count = std::min(current_.committed_prefix.size(), merged.size());
  for (std::size_t i = 0U; i < prefix_count; ++i) {
    merged[i] = current_.committed_prefix[i];
  }
  if (merged.size() < current_.committed_prefix.size()) {
    merged = current_.committed_prefix;
  }
  return merged;
}

void CommittedAvoidanceRoute::enter_degraded_hold(const std::string& safety_concern_event)
{
  current_.state = LifecycleState::DegradedHold;
  current_.safety_concern_event = safety_concern_event;
}

}  // namespace mass_l3::m5::committed_route
