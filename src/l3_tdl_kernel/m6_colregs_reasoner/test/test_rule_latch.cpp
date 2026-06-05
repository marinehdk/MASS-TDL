#include <gtest/gtest.h>

#include "m6_colregs_reasoner/rule_latch.hpp"

namespace mass_l3::m6_colregs {
namespace {

TEST(RuleLatch, LatchesOnOnsetHoldsThroughBearingSwing) {
  RuleLatch latch{/*cpa_safe_m=*/1852.0, /*release_factor=*/1.5};
  // onset: rule active, range closing, cpa unsafe
  EXPECT_TRUE(latch.update(/*rule_active=*/true, /*cpa_m=*/900.0, /*range_closing=*/true));
  // own-ship turned; rule geometry says inactive, but cpa still unsafe → STAY latched
  EXPECT_TRUE(latch.update(/*rule_active=*/false, /*cpa_m=*/900.0, /*range_closing=*/true));
}

TEST(RuleLatch, ReleasesOnlyAboveReleaseThresholdAndOpening) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true);
  // cpa above safe but below release (1852*1.5=2778) → still latched
  EXPECT_TRUE(latch.update(false, 2000.0, false));
  // cpa above release threshold and opening → released
  EXPECT_FALSE(latch.update(false, 3000.0, false));
}

TEST(RuleLatch, NeverLatchesIfNeverOnset) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_FALSE(latch.update(false, 5000.0, false));
}

}  // namespace
}  // namespace mass_l3::m6_colregs
