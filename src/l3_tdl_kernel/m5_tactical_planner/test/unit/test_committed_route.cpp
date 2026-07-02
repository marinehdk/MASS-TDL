#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "m5_tactical_planner/committed_route/committed_route.hpp"

using mass_l3::m5::committed_route::CommittedAvoidanceRoute;
using mass_l3::m5::committed_route::CommittedRouteCandidate;
using mass_l3::m5::committed_route::GeoWP;
using mass_l3::m5::committed_route::LifecycleState;

namespace {

// Route fixtures use realistic WGS84 lat/lon (degrees) per spec §3.7 GeoWP
// coordinate contract — GeoWP.lat_deg/lon_deg hold WGS84 degrees, NOT NED metres.
// Base near Imazu: lat≈34.0, lon≈130.0. ~0.001 deg ≈ 111 m latitude.
std::vector<GeoWP> route_a()
{
  return {
      GeoWP{34.00000, 130.00000, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00090, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00180, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00270, 130.00000, 5.0, "REJOIN_TO_L2"}};
}

std::vector<GeoWP> route_b_with_same_prefix()
{
  return {
      GeoWP{34.00000, 130.00000, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00090, 130.00018, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00198, 130.00031, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{34.00306, 130.00000, 5.0, "REJOIN_TO_L2"}};
}

CommittedRouteCandidate candidate(
    std::string plan_id,
    std::vector<GeoWP> geometry,
    std::size_t frozen_prefix_count,
    double valid_until_s,
    bool nlp_ok = true)
{
  CommittedRouteCandidate c;
  c.plan_id = std::move(plan_id);
  c.geometry = std::move(geometry);
  c.frozen_prefix_count = frozen_prefix_count;
  c.valid_until_s = valid_until_s;
  c.nlp_ok = nlp_ok;
  return c;
}

}  // namespace

TEST(CommittedAvoidanceRoute, keeps_committed_prefix_when_suffix_revised)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;

  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 30.0), 5.0));

  const auto& committed = manager.current();
  EXPECT_EQ(committed.revision, first_revision + 1U);
  ASSERT_EQ(committed.committed_prefix.size(), 2U);
  ASSERT_EQ(committed.active_geometry.size(), 4U);
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].lat_deg, 34.00000);
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].lon_deg, 130.00000);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].lat_deg, 34.00090);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].lon_deg, 130.00018);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].lat_deg, 34.00198);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].lon_deg, 130.00031);
}

TEST(CommittedAvoidanceRoute, repeated_geometry_refreshes_without_revision_bump)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.try_revise(candidate("plan-a-repeat", route_a(), 2U, 35.0), 10.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-repeat");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 35.0);
}

TEST(CommittedAvoidanceRoute, heartbeat_refreshes_valid_until_without_revision_bump)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.heartbeat("plan-a-heartbeat", 55.0, 12.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-heartbeat");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 55.0);
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
}

TEST(CommittedAvoidanceRoute, stale_keep_last_over_45_seconds_enters_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  EXPECT_FALSE(manager.should_enter_degraded_hold(45.0));
  EXPECT_TRUE(manager.should_enter_degraded_hold(45.001));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, "committed_route_stale_gt_45s");
  EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
}

TEST(CommittedAvoidanceRoute, three_consecutive_nlp_failures_enter_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  EXPECT_FALSE(manager.try_revise(candidate("fail-1", route_b_with_same_prefix(), 2U, 30.0, false), 1.0));
  EXPECT_FALSE(manager.should_enter_degraded_hold(1.0));
  EXPECT_FALSE(manager.try_revise(candidate("fail-2", route_b_with_same_prefix(), 2U, 30.0, false), 2.0));
  EXPECT_FALSE(manager.should_enter_degraded_hold(2.0));
  EXPECT_FALSE(manager.try_revise(candidate("fail-3", route_b_with_same_prefix(), 2U, 30.0, false), 3.0));

  EXPECT_TRUE(manager.should_enter_degraded_hold(3.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, "nlp_consecutive_failures_ge_3");
  EXPECT_EQ(manager.current().revision, 1U);
  EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
}


TEST(CommittedAvoidanceRoute, heartbeat_does_not_clear_degraded_hold_after_stale_gate)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));
  const double stale_started_at = manager.current().stale_committed_at_s;

  EXPECT_FALSE(manager.heartbeat("plan-a-heartbeat", 90.0, 50.0));

  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, "committed_route_stale_gt_45s");
  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 20.0);
}


TEST(CommittedAvoidanceRoute, valid_revised_route_can_exit_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));

  EXPECT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 80.0), 50.0));

  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
  EXPECT_TRUE(manager.current().safety_concern_event.empty());
  EXPECT_EQ(manager.current().plan_id, "plan-b");
  EXPECT_DOUBLE_EQ(manager.current().valid_until_s, 80.0);
  EXPECT_EQ(manager.current().revision, 2U);
}

TEST(CommittedAvoidanceRoute, invalid_revisions_do_not_clear_degraded_hold)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_TRUE(manager.should_enter_degraded_hold(45.001));
  const std::uint32_t degraded_revision = manager.current().revision;
  const std::uint32_t degraded_hash = manager.current().route_hash;
  const double stale_started_at = manager.current().stale_committed_at_s;
  const std::string degraded_event = manager.current().safety_concern_event;

  EXPECT_FALSE(manager.try_revise(candidate("empty", {}, 0U, 80.0), 50.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  auto bad_label = route_b_with_same_prefix();
  bad_label[2].nav_mode = "MID_MPC";
  EXPECT_FALSE(manager.try_revise(candidate("bad-label", bad_label, 2U, 85.0), 51.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  auto conflicting = route_b_with_same_prefix();
  conflicting[0].lat_deg = 34.00009;  // > 1e-7 deg → not same waypoint → prefix conflict
  EXPECT_FALSE(manager.try_revise(candidate("prefix-conflict", conflicting, 2U, 90.0), 52.0));
  EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
  EXPECT_EQ(manager.current().safety_concern_event, degraded_event);
  EXPECT_EQ(manager.current().revision, degraded_revision);
  EXPECT_EQ(manager.current().route_hash, degraded_hash);
  EXPECT_DOUBLE_EQ(manager.current().stale_committed_at_s, stale_started_at);
  EXPECT_EQ(manager.current().plan_id, "plan-a");

  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 2U, 95.0), 53.0));
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
  EXPECT_TRUE(manager.current().safety_concern_event.empty());
  EXPECT_EQ(manager.current().plan_id, "plan-b");
  EXPECT_EQ(manager.current().revision, degraded_revision + 1U);
}

// Spec §6.6.3: prefix_count = requested (NOT max(existing, requested)).
// The committed prefix must shrink when a smaller frozen_prefix_count is
// requested (own-ship has pruned/overrun earlier waypoints). The legacy
// max-only behavior is removed.
TEST(CommittedAvoidanceRoute, committed_prefix_shrinks_when_smaller_prefix_requested)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);

  // Revise geometry (new hash → geometry_changed) requesting a SMALLER prefix.
  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 0U, 30.0), 5.0));

  // prefix_count = requested (0), not max(2, 0). committed_prefix must be empty.
  EXPECT_EQ(manager.current().committed_prefix.size(), 0U);
  EXPECT_EQ(manager.current().active_geometry.size(), 4U);
}

// Spec §6.6.3 (Critical-3 review fix): the committed prefix must shrink on a
// SAME-GEOMETRY revise when a smaller frozen_prefix_count is requested. The
// legacy code only reassigned committed_prefix inside the geometry_changed
// block, so a same-hash revise (e.g. a heartbeat-rate refresh of the identical
// route after own advanced and pruned) left the stale larger prefix in place.
// Here the route is IDENTICAL (same hash) but the candidate requests prefix=1
// after an initial prefix=2 → the prefix must shrink to 1 with NO revision bump.
TEST(CommittedAvoidanceRoute, committed_prefix_shrinks_on_same_geometry_smaller_count)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  // Identical geometry (route_a), new plan_id, SMALLER requested prefix (2 → 1).
  ASSERT_TRUE(manager.try_revise(candidate("plan-a-pruned", route_a(), 1U, 30.0), 5.0));

  // No geometry change → no revision/hash bump, but the prefix MUST shrink.
  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().plan_id, "plan-a-pruned");
  EXPECT_EQ(manager.current().committed_prefix.size(), 1U)
      << "same geometry + smaller frozen_prefix_count must prune the prefix";
  // The surviving prefix waypoint is geometry[0] (the route head is unchanged).
  EXPECT_DOUBLE_EQ(manager.current().committed_prefix[0].lat_deg, 34.00000);
}

TEST(CommittedAvoidanceRoute, prefix_conflict_on_genuinely_different_waypoint_is_rejected)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);

  auto conflicting = route_b_with_same_prefix();
  conflicting[0].lat_deg = 34.00009;  // 9e-5 deg ≈ 10 m → outside 1e-7 tolerance
  EXPECT_FALSE(manager.try_revise(candidate("plan-conflict", conflicting, 2U, 40.0), 10.0));

  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[0].lat_deg, 34.00000);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[1].lat_deg, 34.00090);
}

TEST(CommittedAvoidanceRoute, failed_nlp_risk_triggers_enter_degraded_hold_immediately)
{
  auto expect_failed_nlp_degraded_hold = [](CommittedRouteCandidate risky, const std::string& event) {
    CommittedAvoidanceRoute manager;
    ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

    EXPECT_FALSE(manager.try_revise(risky, 1.0));

    EXPECT_EQ(manager.current().state, LifecycleState::DegradedHold);
    EXPECT_EQ(manager.current().safety_concern_event, event);
    EXPECT_EQ(manager.current().revision, 1U);
    EXPECT_EQ(manager.current().active_geometry.size(), route_a().size());
  };

  auto hard_cpa = candidate("fail-hard-cpa", route_b_with_same_prefix(), 2U, 30.0, false);
  hard_cpa.current_cpa_m = 49.0;
  hard_cpa.cpa_hard_m = 50.0;
  expect_failed_nlp_degraded_hold(hard_cpa, "current_cpa_below_hard_floor");

  auto heading = candidate("fail-heading", route_b_with_same_prefix(), 2U, 30.0, false);
  heading.target_heading_delta_deg = 15.1;
  expect_failed_nlp_degraded_hold(heading, "target_heading_change_gt_15deg");

  auto drift = candidate("fail-drift", route_b_with_same_prefix(), 2U, 30.0, false);
  drift.cpa_drift_fraction = 0.201;
  expect_failed_nlp_degraded_hold(drift, "cpa_drift_gt_20pct");
}

// Spec §3.7 / §10.1 "GeoWP 坐标契约": same_waypoint uses tolerance comparison
// (|Δlat|,|Δlon| < 1e-7 deg ≈ 1cm, |Δspeed| < 0.01 m/s). Exact double
// comparison is forbidden. Verified indirectly via preserves_committed_prefix:
// a revised geometry whose prefix waypoints differ only by sub-tolerance
// WGS84 jitter must be accepted (not rejected as a prefix conflict).
TEST(CommittedAvoidanceRoute, preserves_committed_prefix_accepts_sub_tolerance_jitter)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  // Jitter well below 1e-7 deg tolerance — represents WGS84 float reprojection
  // noise, NOT a real geometry change.
  auto jittered = route_b_with_same_prefix();
  jittered[0].lat_deg += 1e-9;
  jittered[0].lon_deg += 1e-9;
  jittered[1].lat_deg += 5e-10;
  jittered[1].speed_mps += 0.001;  // < 0.01 m/s speed tolerance

  EXPECT_TRUE(manager.try_revise(candidate("plan-jitter", jittered, 2U, 30.0), 5.0));
  EXPECT_EQ(manager.current().state, LifecycleState::Committed);
}

// Spec §3.7: tolerance must REJECT points that exceed the threshold, so that
// a real ~10 m waypoint move is treated as a genuine geometry change (the
// hash changes) but a sub-1e-7 deg perturbation is a no-op.
TEST(CommittedAvoidanceRoute, preserves_committed_prefix_rejects_above_tolerance_lat_delta)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  auto drifted = route_b_with_same_prefix();
  drifted[0].lat_deg += 1e-6;  // 1e-6 deg ≈ 11 cm > 1e-7 tolerance

  // With a 2U frozen prefix, the drifted prefix[0] is NOT the same waypoint
  // → preserves_committed_prefix fails → KeepLast rejection.
  EXPECT_FALSE(manager.try_revise(candidate("plan-drift", drifted, 2U, 30.0), 5.0));
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "frozen_prefix_conflict");
}

TEST(CommittedAvoidanceRoute, successful_empty_or_invalid_candidate_is_rejected_without_revision)
{
  CommittedAvoidanceRoute manager;
  EXPECT_FALSE(manager.try_revise(candidate("empty", {}, 0U, 20.0), 0.0));
  EXPECT_EQ(manager.current().revision, 0U);
  EXPECT_TRUE(manager.current().active_geometry.empty());
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");

  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 1.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  auto invalid = route_b_with_same_prefix();
  invalid[1].speed_mps = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(manager.try_revise(candidate("invalid", invalid, 2U, 30.0), 2.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");
}

TEST(CommittedAvoidanceRoute, invented_source_label_is_rejected)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));
  const std::uint32_t first_revision = manager.current().revision;
  const std::uint32_t first_hash = manager.current().route_hash;

  auto invented_label = route_b_with_same_prefix();
  invented_label[2].nav_mode = "MID_MPC";
  EXPECT_FALSE(manager.try_revise(candidate("invented-label", invented_label, 2U, 30.0), 5.0));

  EXPECT_EQ(manager.current().revision, first_revision);
  EXPECT_EQ(manager.current().route_hash, first_hash);
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  EXPECT_EQ(manager.current().safety_concern_event, "candidate_preflight_failed");
}
