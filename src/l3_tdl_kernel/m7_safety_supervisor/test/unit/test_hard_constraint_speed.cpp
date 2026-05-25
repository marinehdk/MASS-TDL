#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/hard_constraint_speed.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(HardConstraintSpeed, SpeedWithinLimitPasses)
{
  auto const result = check_speed_limit(10.0F, 12.0F);
  EXPECT_TRUE(result.compliant);
  EXPECT_FALSE(result.violation);
}

TEST(HardConstraintSpeed, SpeedExceedsLimitWithTolerance)
{
  auto const result = check_speed_limit(13.0F, 12.0F);
  EXPECT_FALSE(result.compliant);
  EXPECT_TRUE(result.violation);
  EXPECT_GT(result.excess_pct, 5.0F);
}

TEST(HardConstraintSpeed, SpeedWithinToleranceMargin)
{
  auto const result = check_speed_limit(12.5F, 12.0F);
  EXPECT_TRUE(result.compliant);
}

TEST(HardConstraintSpeed, ZeroSpeedLimitEdge)
{
  auto const result = check_speed_limit(0.1F, 0.0F);
  EXPECT_FALSE(result.compliant);
}

TEST(HardConstraintSpeed, NegativeSpeedIsViolation)
{
  auto const result = check_speed_limit(-1.0F, 10.0F);
  EXPECT_FALSE(result.compliant);
}

}  // namespace
}  // namespace mass_l3::m7::core
