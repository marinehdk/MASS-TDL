#include <gtest/gtest.h>

#include <cstdint>
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
      GeoWP{0.0, 0.0, 5.0, "MID_MPC"},
      GeoWP{100.0, 20.0, 5.0, "MID_MPC"},
      GeoWP{200.0, 20.0, 5.0, "MID_MPC"},
      GeoWP{300.0, 0.0, 5.0, "REJOIN"}};
}

std::vector<GeoWP> route_b_with_same_prefix()
{
  return {
      GeoWP{0.0, 0.0, 5.0, "MID_MPC"},
      GeoWP{100.0, 20.0, 5.0, "MID_MPC"},
      GeoWP{220.0, 35.0, 5.0, "MID_MPC"},
      GeoWP{340.0, 0.0, 5.0, "REJOIN"}};
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
