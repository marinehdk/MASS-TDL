#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>

#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "l3_msgs/msg/rule_active.hpp"

using namespace mass_l3::m7::iec61508;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

l3_msgs::msg::ODDState build_odd(float conformance_score) {
  l3_msgs::msg::ODDState o;
  o.conformance_score = conformance_score;
  o.confidence = 1.0F;
  return o;
}

l3_msgs::msg::WorldState build_world_empty() {
  return l3_msgs::msg::WorldState{};
}

l3_msgs::msg::WorldState build_world_with_cpa(double cpa_m) {
  l3_msgs::msg::WorldState w;
  l3_msgs::msg::TrackedTarget t;
  t.cpa_m = cpa_m;
  t.tcpa_s = 120.0;
  t.confidence = 0.9F;
  w.targets.push_back(t);
  return w;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
l3_msgs::msg::WorldState build_world_multiple_targets(double cpa1, double cpa2)
{
  l3_msgs::msg::WorldState w;
  {
    l3_msgs::msg::TrackedTarget t;
    t.cpa_m = cpa1;
    t.tcpa_s = 60.0;
    w.targets.push_back(t);
  }
  {
    l3_msgs::msg::TrackedTarget t;
    t.cpa_m = cpa2;
    t.tcpa_s = 90.0;
    w.targets.push_back(t);
  }
  return w;
}

l3_msgs::msg::COLREGsConstraint build_colregs_empty() {
  return l3_msgs::msg::COLREGsConstraint{};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
l3_msgs::msg::COLREGsConstraint build_colregs_with_rules(std::size_t rule_count) {
  l3_msgs::msg::COLREGsConstraint c;
  for (std::size_t i = 0U; i < rule_count; ++i) {
    l3_msgs::msg::RuleActive r;
    r.rule_id = uint8_t{15};
    c.active_rules.push_back(r);
  }
  c.confidence = 0.9F;
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: conformance_score in range — valid
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ConformanceScore_InRange_Valid) {
  FaultMonitor monitor;
  auto const kOdd = build_odd(0.75F);
  auto const kResult = monitor.validate_odd_state(kOdd);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 2: conformance_score out of range (> 1.0) — invalid
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ConformanceScore_OutOfRange_Invalid) {
  FaultMonitor monitor;
  auto const kOdd = build_odd(1.5F);
  auto const kResult = monitor.validate_odd_state(kOdd);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_FALSE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 3: conformance_score exactly 1.0 — valid (boundary)
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ConformanceScore_ExactBoundary_1_Valid) {
  FaultMonitor monitor;
  auto const kOdd = build_odd(1.0F);
  auto const kResult = monitor.validate_odd_state(kOdd);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 4: conformance_score exactly 0.0 — valid (lower boundary)
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ConformanceScore_ExactBoundary_0_Valid) {
  FaultMonitor monitor;
  auto const kOdd = build_odd(0.0F);
  auto const kResult = monitor.validate_odd_state(kOdd);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 5: CPA negative — inconsistent
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, CpaConsistency_NegativeCpa_Inconsistent) {
  FaultMonitor monitor;
  auto const kWorld = build_world_with_cpa(-5.0);
  auto const kResult = monitor.validate_cpa_consistency(kWorld);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_FALSE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 6: All CPAs non-negative — consistent
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, CpaConsistency_ValidCpas_Consistent) {
  FaultMonitor monitor;
  auto const kWorld = build_world_multiple_targets(500.0, 0.0);
  auto const kResult = monitor.validate_cpa_consistency(kWorld);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 7: No targets, no rules — consistent (empty case)
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ColregsConsistency_NoRulesNoTargets_Consistent) {
  FaultMonitor monitor;
  auto const kWorld   = build_world_empty();
  auto const kColregs = build_colregs_empty();
  auto const kResult  = monitor.validate_colregs_consistency(kWorld, kColregs);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 8: Active rules but no targets — inconsistent
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ColregsConsistency_RulesWithNoTargets_Inconsistent) {
  FaultMonitor monitor;
  auto const kWorld   = build_world_empty();
  auto const kColregs = build_colregs_with_rules(1U);
  auto const kResult  = monitor.validate_colregs_consistency(kWorld, kColregs);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_FALSE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 9: Active rules with targets present — consistent
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ColregsConsistency_RulesWithTargets_Consistent) {
  FaultMonitor monitor;
  auto const kWorld   = build_world_with_cpa(300.0);
  auto const kColregs = build_colregs_with_rules(2U);
  auto const kResult  = monitor.validate_colregs_consistency(kWorld, kColregs);
  ASSERT_TRUE(kResult.has_value());
  EXPECT_TRUE(kResult.value());
}

// ---------------------------------------------------------------------------
// Test 10: run() with multiple failures — fault_count accumulates
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, Run_MultipleFailures_FaultCountAccumulates) {
  FaultMonitor monitor;

  // ODD out of range + negative CPA
  auto const kBadOdd    = build_odd(1.5F);
  auto const kBadWorld  = build_world_with_cpa(-10.0);
  auto const kOkColregs = build_colregs_empty();

  auto const kResult = monitor.run(kBadOdd, kBadWorld, kOkColregs);

  // Two checks failed: ODD score + CPA
  EXPECT_FALSE(kResult.conformance_score_valid);
  EXPECT_FALSE(kResult.cpa_internal_consistent);
  EXPECT_TRUE(kResult.colregs_target_id_match);
  EXPECT_GE(kResult.fault_count, 2U);
  EXPECT_EQ(monitor.fault_count(), kResult.fault_count);
}

// ---------------------------------------------------------------------------
// Test 11: run() with all checks passing — fault_count unchanged
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, Run_AllPassing_FaultCountUnchanged) {
  FaultMonitor monitor;

  auto const kGoodOdd    = build_odd(0.9F);
  auto const kGoodWorld  = build_world_with_cpa(500.0);
  auto const kOkColregs  = build_colregs_empty();

  auto const kResult = monitor.run(kGoodOdd, kGoodWorld, kOkColregs);

  EXPECT_TRUE(kResult.conformance_score_valid);
  EXPECT_TRUE(kResult.cpa_internal_consistent);
  EXPECT_TRUE(kResult.colregs_target_id_match);
  EXPECT_EQ(kResult.fault_count, 0U);
}

// ---------------------------------------------------------------------------
// Test 12: reset_count — clears cumulative fault count
// ---------------------------------------------------------------------------

TEST(FaultMonitorTest, ResetCount_ClearsFaultCount) {
  FaultMonitor monitor;

  auto const kBadOdd = build_odd(-0.1F);
  (void)monitor.run(kBadOdd, build_world_empty(), build_colregs_empty());
  EXPECT_GT(monitor.fault_count(), 0U);

  monitor.reset_count();
  EXPECT_EQ(monitor.fault_count(), 0U);
}
