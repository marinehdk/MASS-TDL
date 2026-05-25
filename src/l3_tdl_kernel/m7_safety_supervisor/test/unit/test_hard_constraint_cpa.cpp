#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/hard_constraint_cpa.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(HardConstraintCpa, ConsistentCpasWithinThreshold)
{
  auto const result = check_cpa_consistency(0.50F, 0.52F, 0.51F, 0.10F);
  EXPECT_TRUE(result.consistent);
  EXPECT_LE(result.deviation_pct, 10.0F);
}

TEST(HardConstraintCpa, M2InconsistentWithM7)
{
  auto const result = check_cpa_consistency(0.60F, 0.51F, 0.50F, 0.10F);
  EXPECT_FALSE(result.consistent);
  EXPECT_GT(result.deviation_pct, 10.0F);
}

TEST(HardConstraintCpa, M5DcpaInconsistentWithM7)
{
  auto const result = check_cpa_consistency(0.50F, 0.58F, 0.50F, 0.10F);
  EXPECT_FALSE(result.consistent);
  EXPECT_GT(result.deviation_pct, 10.0F);
}

TEST(HardConstraintCpa, NearZeroCpaGracefulHandling)
{
  auto const result = check_cpa_consistency(0.002F, 0.002F, 0.001F, 0.10F);
  EXPECT_FALSE(result.consistent);
}

TEST(HardConstraintCpa, BothSourcesConsistent)
{
  auto const result = check_cpa_consistency(1.0F, 1.05F, 1.02F, 0.10F);
  EXPECT_TRUE(result.consistent);
}

}  // namespace
}  // namespace mass_l3::m7::core
