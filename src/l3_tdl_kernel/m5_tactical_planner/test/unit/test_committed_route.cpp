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

std::vector<GeoWP> route_a()
{
  return {
      GeoWP{0.0, 0.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{100.0, 20.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{200.0, 20.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{300.0, 0.0, 5.0, "REJOIN_TO_L2"}};
}

std::vector<GeoWP> route_b_with_same_prefix()
{
  return {
      GeoWP{0.0, 0.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{100.0, 20.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{220.0, 35.0, 5.0, "MID_MPC_OPTIMIZED"},
      GeoWP{340.0, 0.0, 5.0, "REJOIN_TO_L2"}};
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
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].x_m, 0.0);
  EXPECT_DOUBLE_EQ(committed.active_geometry[0].y_m, 0.0);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].x_m, 100.0);
  EXPECT_DOUBLE_EQ(committed.active_geometry[1].y_m, 20.0);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].x_m, 220.0);
  EXPECT_DOUBLE_EQ(committed.active_geometry[2].y_m, 35.0);
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
  conflicting[0].x_m = 10.0;
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

TEST(CommittedAvoidanceRoute, frozen_prefix_cannot_shrink_and_conflicting_revision_is_rejected)
{
  CommittedAvoidanceRoute manager;
  ASSERT_TRUE(manager.try_revise(candidate("plan-a", route_a(), 2U, 20.0), 0.0));

  ASSERT_TRUE(manager.try_revise(candidate("plan-b", route_b_with_same_prefix(), 0U, 30.0), 5.0));
  const std::uint32_t accepted_revision = manager.current().revision;
  const std::uint32_t accepted_hash = manager.current().route_hash;
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  EXPECT_DOUBLE_EQ(manager.current().committed_prefix[0].x_m, 0.0);
  EXPECT_DOUBLE_EQ(manager.current().committed_prefix[1].x_m, 100.0);

  auto conflicting = route_b_with_same_prefix();
  conflicting[0].x_m = 10.0;
  EXPECT_FALSE(manager.try_revise(candidate("plan-conflict", conflicting, 0U, 40.0), 10.0));

  EXPECT_EQ(manager.current().revision, accepted_revision);
  EXPECT_EQ(manager.current().route_hash, accepted_hash);
  EXPECT_EQ(manager.current().state, LifecycleState::KeepLast);
  ASSERT_EQ(manager.current().committed_prefix.size(), 2U);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[0].x_m, 0.0);
  EXPECT_DOUBLE_EQ(manager.current().active_geometry[1].x_m, 100.0);
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
