// Unit tests for sil_pulse_adapter health logic (Track A A5b).
// Pure inference verification — no ROS spinning.
#include <gtest/gtest.h>

#include "sil_pulse_adapter/health.hpp"

using sil_pulse_adapter::kHealthGreen;
using sil_pulse_adapter::kHealthRed;
using sil_pulse_adapter::kPulseTimeoutS;
using sil_pulse_adapter::module_health;

TEST(ModuleHealth, NeverSeenIsRed) {
  // last_seen_s = -1 sentinel means "never seen".
  EXPECT_EQ(module_health(/*now_s=*/100.0, /*last_seen_s=*/-1.0), kHealthRed);
}

TEST(ModuleHealth, SeenJustNowIsGreen) {
  EXPECT_EQ(module_health(100.0, 100.0), kHealthGreen);
}

TEST(ModuleHealth, SeenWithinTimeoutIsGreen) {
  // 5s ago, within the 10s default window.
  EXPECT_EQ(module_health(100.0, 95.0), kHealthGreen);
}

TEST(ModuleHealth, SeenExactlyAtTimeoutBoundaryIsGreen) {
  // age == timeout -> still GREEN (strictly-greater-than is the red condition).
  EXPECT_EQ(module_health(100.0, 100.0 - kPulseTimeoutS), kHealthGreen);
}

TEST(ModuleHealth, SeenBeyondTimeoutIsRed) {
  // 11s ago, beyond the 10s window.
  EXPECT_EQ(module_health(100.0, 89.0), kHealthRed);
}

TEST(ModuleHealth, CustomTimeoutRespected) {
  // 3s ago is RED under a 2s custom timeout.
  EXPECT_EQ(module_health(100.0, 97.0, /*timeout_s=*/2.0), kHealthRed);
  // 3s ago is GREEN under a 5s custom timeout.
  EXPECT_EQ(module_health(100.0, 97.0, /*timeout_s=*/5.0), kHealthGreen);
}

TEST(ModuleHealth, MonotonicIncreasingClockHandled) {
  // Clock advances; a module seen at t=50 stays GREEN through t=59 then RED at t=61.
  EXPECT_EQ(module_health(59.0, 50.0), kHealthGreen);
  EXPECT_EQ(module_health(61.0, 50.0), kHealthRed);
}
