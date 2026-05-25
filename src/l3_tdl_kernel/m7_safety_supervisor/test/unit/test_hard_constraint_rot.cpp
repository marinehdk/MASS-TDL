#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/hard_constraint_rot.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(HardConstraintRot, RotWithinLimitPasses)
{
  auto const result = check_rot_limit(3.0F, 5.0F);
  EXPECT_TRUE(result.compliant);
  EXPECT_FALSE(result.violation);
}

TEST(HardConstraintRot, RotExceedsLimit)
{
  auto const result = check_rot_limit(6.0F, 5.0F);
  EXPECT_FALSE(result.compliant);
  EXPECT_TRUE(result.violation);
}

TEST(HardConstraintRot, NegativeRotWithinLimit)
{
  auto const result = check_rot_limit(-4.0F, 5.0F);
  EXPECT_TRUE(result.compliant);
}

TEST(HardConstraintRot, NegativeRotExceedsLimit)
{
  auto const result = check_rot_limit(-6.0F, 5.0F);
  EXPECT_FALSE(result.compliant);
  EXPECT_TRUE(result.violation);
}

TEST(HardConstraintRot, ZeroRotLimitEdge)
{
  auto const result = check_rot_limit(0.1F, 0.0F);
  EXPECT_FALSE(result.compliant);
}

}  // namespace
}  // namespace mass_l3::m7::core
