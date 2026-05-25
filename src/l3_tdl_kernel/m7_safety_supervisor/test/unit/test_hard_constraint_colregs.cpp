#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/hard_constraint_colregs.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(HardConstraintColregs, Rule14HeadOnMustTurnStarboard)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule14HeadOn, -15.0F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintColregs, Rule14HeadOnStarboardTurnPasses)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule14HeadOn, 20.0F);
  EXPECT_TRUE(result.consistent);
}

TEST(HardConstraintColregs, Rule15StandOnMustNotTurn)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule15StandOn, 12.0F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintColregs, Rule15GiveWayMustTurnStarboard)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule15GiveWay, -5.0F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintColregs, Rule16GiveWayLargeAlterationRequired)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule16GiveWay, 3.0F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintColregs, Rule13OvertakingTurningTowardTargetViolates)
{
  auto const result = check_colregs_geometry(ColregsRule::kRule13Overtaking, 30.0F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintColregs, UnknownRuleDefaultsToPass)
{
  auto const result = check_colregs_geometry(ColregsRule::kUnknown, 90.0F);
  EXPECT_TRUE(result.consistent);
}

}  // namespace
}  // namespace mass_l3::m7::core
