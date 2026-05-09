#include <gtest/gtest.h>

#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

using namespace mass_l3::m7::sotif;

namespace {

// Helper: build an AssumptionStatus with N violations
AssumptionStatus make_status(uint32_t violation_count)
{
  AssumptionStatus s{};
  s.total_violation_count = violation_count;
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: Zero violations → detect returns false
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, ZeroViolations_ReturnsFalse)
{
  TriggeringConditionDetector det;
  auto const kStatus = make_status(0U);
  EXPECT_FALSE(det.detect(kStatus));
}

// ---------------------------------------------------------------------------
// Test 2: One violation → false (below threshold)
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, OneViolation_ReturnsFalse)
{
  TriggeringConditionDetector det;
  auto const kStatus = make_status(1U);
  EXPECT_FALSE(det.detect(kStatus));
}

// ---------------------------------------------------------------------------
// Test 3: Two violations → false (below threshold)
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, TwoViolations_ReturnsFalse)
{
  TriggeringConditionDetector det;
  auto const kStatus = make_status(2U);
  EXPECT_FALSE(det.detect(kStatus));
}

// ---------------------------------------------------------------------------
// Test 4: Three violations (at threshold) → true
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, ThreeViolations_ReturnsTrue)
{
  TriggeringConditionDetector det;
  auto const kStatus = make_status(3U);
  EXPECT_TRUE(det.detect(kStatus));
}

// ---------------------------------------------------------------------------
// Test 5: Four violations → true (above threshold)
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, FourViolations_ReturnsTrue)
{
  TriggeringConditionDetector det;
  auto const kStatus = make_status(4U);
  EXPECT_TRUE(det.detect(kStatus));
}

// ---------------------------------------------------------------------------
// Test 6: Max violations (all 6 assumptions violated) → true
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, MaxViolations_ReturnsTrue)
{
  TriggeringConditionDetector det;
  // kCount = 6 assumption IDs
  AssumptionStatus status{};
  for (auto& v : status.violation_active) { v = true; }
  status.total_violation_count = static_cast<uint32_t>(status.violation_active.size());
  EXPECT_TRUE(det.detect(status));
}

// ---------------------------------------------------------------------------
// Test 7: Threshold constant is 3 (compile-time check)
// ---------------------------------------------------------------------------
TEST(TriggeringConditionDetectorTest, ThresholdConstant_IsThree)
{
  EXPECT_EQ(TriggeringConditionDetector::kExtremeScenarioThreshold, 3U);
}
