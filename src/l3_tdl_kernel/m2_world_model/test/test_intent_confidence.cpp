#include <algorithm>
#include <gtest/gtest.h>

namespace {

/// Compute intent confidence from target type and track age.
/// Formula: confidence = base * min(1.0, track_age_s / 50.0)
/// where base = 0.50 for vessel, 0.30 for radar-only.
/// Clamped to [0.05, 0.95].
double compute_intent_confidence(bool is_vessel, double track_age_s) {
  double const base = is_vessel ? 0.50 : 0.30;
  double const age_factor = std::min(1.0, track_age_s / 50.0);
  double const raw = base * age_factor;
  return std::clamp(raw, 0.05, 0.95);
}

}  // namespace

TEST(IntentConfidenceTest, AisVessel50) {
  double const conf = compute_intent_confidence(true, 60.0);
  EXPECT_NEAR(conf, 0.50, 0.05);
}

TEST(IntentConfidenceTest, RadarOnly30) {
  double const conf = compute_intent_confidence(false, 60.0);
  EXPECT_NEAR(conf, 0.30, 0.05);
}

TEST(IntentConfidenceTest, NewTarget10) {
  double const conf = compute_intent_confidence(true, 10.0);
  EXPECT_NEAR(conf, 0.10, 0.05);
}

TEST(IntentConfidenceTest, ClampedMin05) {
  double const conf = compute_intent_confidence(false, 1.0);
  EXPECT_GE(conf, 0.05);
}

TEST(IntentConfidenceTest, ClampedMax95) {
  double const conf = compute_intent_confidence(true, 999.0);
  EXPECT_LE(conf, 0.95);
}
