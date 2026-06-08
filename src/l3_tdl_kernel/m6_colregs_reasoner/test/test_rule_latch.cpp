#include <gtest/gtest.h>

#include "m6_colregs_reasoner/rule_latch.hpp"

namespace mass_l3::m6_colregs {
namespace {

TEST(RuleLatch, LatchesOnOnsetHoldsThroughBearingSwing) {
  RuleLatch latch{/*cpa_safe_m=*/1852.0, /*release_factor=*/1.5};
  // onset: rule active, range closing, cpa unsafe
  EXPECT_TRUE(latch.update(/*rule_active=*/true, /*cpa_m=*/900.0, /*range_closing=*/true,
                           /*past_and_clear=*/false));
  // own-ship turned; rule geometry says inactive, but cpa still unsafe → STAY latched
  EXPECT_TRUE(latch.update(/*rule_active=*/false, /*cpa_m=*/900.0, /*range_closing=*/true,
                           /*past_and_clear=*/false));
}

TEST(RuleLatch, ReleasesOnlyAboveReleaseThresholdAndOpening) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true, false);
  // cpa above safe but below release (1852*1.5=2778), not yet past → still latched
  EXPECT_TRUE(latch.update(false, 2000.0, false, false));
  // cpa above release threshold and opening → released (CPA fallback path)
  EXPECT_FALSE(latch.update(false, 3000.0, false, false));
}

TEST(RuleLatch, NeverLatchesIfNeverOnset) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_FALSE(latch.update(false, 5000.0, false, false));
}

// Rule 16 "finally past and clear": once the target draws abaft the beam and the
// range is opening, release immediately even though predicted CPA has not yet
// climbed past the conservative 1.5×cpa_safe fallback threshold. This is what
// hands control back to the route-return autopilot in time.
TEST(RuleLatch, ReleasesWhenPastAndClearBelowCpaThreshold) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_TRUE(latch.update(true, 900.0, true, false));   // onset
  // cpa 2000 < release 2778, but target abaft beam & opening → release
  EXPECT_FALSE(latch.update(false, 2000.0, false, true));
}

TEST(RuleLatch, PastAndClearDoesNotReleaseWhileStillClosing) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true, false);  // onset
  // past_and_clear asserted but range still closing → not finally clear, hold
  EXPECT_TRUE(latch.update(false, 2000.0, true, true));
}

}  // namespace
}  // namespace mass_l3::m6_colregs
