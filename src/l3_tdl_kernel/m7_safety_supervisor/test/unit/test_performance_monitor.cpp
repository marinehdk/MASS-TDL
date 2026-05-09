#include <gtest/gtest.h>
#include <chrono>
#include <memory>

#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/tracked_target.hpp"

using namespace mass_l3::m7::sotif;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static l3_msgs::msg::WorldState build_world_no_targets()
{
  l3_msgs::msg::WorldState w;
  w.own_ship.sog_kn = 5.0;
  w.confidence = 0.9f;
  return w;
}

static l3_msgs::msg::WorldState build_world_with_target(double cpa_m)
{
  l3_msgs::msg::WorldState w;
  w.own_ship.sog_kn = 5.0;
  w.confidence = 0.9f;
  l3_msgs::msg::TrackedTarget t;
  t.cpa_m = cpa_m;
  t.classification = "vessel";
  t.classification_confidence = 0.9f;
  w.targets.push_back(t);
  return w;
}

static l3_msgs::msg::WorldState build_world_multi_targets(
    std::size_t n_targets, double cpa_m)
{
  l3_msgs::msg::WorldState w;
  w.own_ship.sog_kn = 5.0;
  w.confidence = 0.9f;
  for (std::size_t i = 0u; i < n_targets; ++i) {
    l3_msgs::msg::TrackedTarget t;
    t.cpa_m = cpa_m;
    t.classification = "vessel";
    t.classification_confidence = 0.9f;
    w.targets.push_back(t);
  }
  return w;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class PerformanceMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    PerformanceConfig cfg;
    cfg.cpa_window = std::chrono::seconds{30};
    cfg.cpa_trend_slope_threshold_nm_s = -0.01;
    cfg.multiple_targets_cpa_threshold_nm = 1.0;
    cfg.multiple_targets_count_threshold = 2u;
    monitor_ = std::make_unique<PerformanceMonitor>(cfg);
    t0_ = std::chrono::steady_clock::now();
  }

  PerformanceMonitor& monitor() { return *monitor_; }
  std::chrono::steady_clock::time_point t(int offset_s) const {
    return t0_ + std::chrono::seconds{offset_s};
  }

private:
  std::unique_ptr<PerformanceMonitor> monitor_;
  std::chrono::steady_clock::time_point t0_;
};

// ===========================================================================
// Tests
// ===========================================================================

TEST_F(PerformanceMonitorTest, NoTargets_NoCpaDegradation)
{
  // No targets -> sentinel CPA, slope = 0, no degradation
  auto world = build_world_no_targets();
  auto status = monitor().evaluate(world, t(0));
  EXPECT_FALSE(status.cpa_trend_degrading);
  EXPECT_EQ(status.critical_target_count, 0u);
  EXPECT_FALSE(status.multiple_targets_nearby);
}

TEST_F(PerformanceMonitorTest, SingleTarget_CpaTracked)
{
  // Single target at 2 NM CPA (3704m) -> should be stored and tracked
  auto world = build_world_with_target(3704.0);  // 2 NM
  auto status = monitor().evaluate(world, t(0));
  // max_cpa_in_window_nm should be approximately 2 NM
  EXPECT_NEAR(status.max_cpa_in_window_nm, 2.0, 0.01);
  // CPA > 1 NM threshold -> not flagged as nearby
  EXPECT_EQ(status.critical_target_count, 0u);
  EXPECT_FALSE(status.multiple_targets_nearby);
}

TEST_F(PerformanceMonitorTest, MultipleTargetsClose_FlagsNearby)
{
  // 3 targets at 500m CPA (< 1852m = 1 NM threshold) -> multiple_targets_nearby
  auto world = build_world_multi_targets(3u, 500.0);
  auto status = monitor().evaluate(world, t(0));
  EXPECT_TRUE(status.multiple_targets_nearby);
  EXPECT_GE(status.critical_target_count, 2u);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PerformanceMonitorTest, Reset_ClearsHistory)
{
  // Populate with several calls, then reset
  auto world = build_world_with_target(1000.0);
  for (int i = 0; i < 5; ++i) {
    (void)monitor().evaluate(world, t(i));
  }
  monitor().reset();

  // After reset: single call should behave as fresh start (count=1, slope=0)
  auto status = monitor().evaluate(world, t(10));
  EXPECT_DOUBLE_EQ(status.cpa_trend_slope_nm_s, 0.0);
  // max_cpa_in_window should only contain this one new sample (~0.54 NM)
  EXPECT_NEAR(status.max_cpa_in_window_nm, 1000.0 / 1852.0, 0.01);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PerformanceMonitorTest, SteadilyIncreasingCpa_NotDegrading)
{
  // CPA improving (increasing) -> slope positive -> not degrading
  // Feed 5 samples with increasing CPA
  for (int i = 0; i < 5; ++i) {
    double const cpa_m = static_cast<double>(i + 1) * 1000.0;  // 1000m, 2000m, ...
    auto world = build_world_with_target(cpa_m);
    (void)monitor().evaluate(world, t(i));
  }
  // Last status should show positive slope -> not degrading
  auto world = build_world_with_target(6000.0);
  auto status = monitor().evaluate(world, t(5));
  EXPECT_FALSE(status.cpa_trend_degrading);
  EXPECT_GE(status.cpa_trend_slope_nm_s, 0.0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PerformanceMonitorTest, SteadilyDecreasingCpa_IsDegrading)
{
  // CPA worsening (decreasing sharply) -> slope very negative -> degrading
  for (int i = 0; i < 5; ++i) {
    // Large decrease: 10000m, 9000m, 8000m, ...
    double const cpa_m = static_cast<double>(10 - i) * 1000.0;
    auto world = build_world_with_target(cpa_m);
    (void)monitor().evaluate(world, t(i));
  }
  auto world = build_world_with_target(5000.0);
  auto status = monitor().evaluate(world, t(5));
  // Slope should be significantly negative (rate: -1000m per sample / 1852 per NM)
  // = about -0.54 NM/sample, which is << -0.01 threshold
  EXPECT_TRUE(status.cpa_trend_degrading);
  EXPECT_LT(status.cpa_trend_slope_nm_s, -0.01);
}
