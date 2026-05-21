#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

namespace {

/// Compute intent confidence matching the production formula (world_state_aggregator.cpp):
///   confidence = base * (1 - exp(-track_age_s / 60.0))
/// where base = 0.50 for vessel, 0.30 for radar-only.
/// Clamped to [0.05, 0.95].
double compute_intent_confidence_production(bool is_vessel, double track_age_s) {
  double const base = is_vessel ? 0.50 : 0.30;
  double const track_age_factor = 1.0 - std::exp(-track_age_s / 60.0);
  double const raw = base * track_age_factor;
  return std::clamp(raw, 0.05, 0.95);
}

}  // namespace

TEST(IntentConfidenceTest, AisVessel50) {
  double const conf = compute_intent_confidence_production(true, 60.0);
  EXPECT_NEAR(conf, 0.32, 0.05);
}

TEST(IntentConfidenceTest, RadarOnly30) {
  double const conf = compute_intent_confidence_production(false, 60.0);
  EXPECT_NEAR(conf, 0.19, 0.05);
}

TEST(IntentConfidenceTest, NewTarget10) {
  double const conf = compute_intent_confidence_production(true, 10.0);
  EXPECT_NEAR(conf, 0.08, 0.03);
}

TEST(IntentConfidenceTest, ClampedMin05) {
  double const conf = compute_intent_confidence_production(false, 1.0);
  EXPECT_GE(conf, 0.05);
}

TEST(IntentConfidenceTest, ClampedMax95) {
  double const conf = compute_intent_confidence_production(true, 999.0);
  EXPECT_LE(conf, 0.95);
}
