#include <gtest/gtest.h>

#include "m2_world_model/woerner_compliance_scorer.hpp"

namespace mass_l3::m2 {
namespace {

TargetComplianceSample sample_at(double t_s, double cpa_m, double range_m,
                                 double heading_deg) {
  TargetComplianceSample sample;
  sample.t_s = t_s;
  sample.cpa_m = cpa_m;
  sample.tcpa_s = 120.0 - t_s;
  sample.range_m = range_m;
  sample.heading_deg = heading_deg;
  return sample;
}

}  // namespace

TEST(WoernerComplianceScorerTest, InsufficientHistoryIsNeutral) {
  WoernerComplianceScorer scorer(30.0);

  scorer.add_sample(sample_at(0.0, 120.0, 900.0, 270.0));

  EXPECT_EQ(scorer.sample_count(), 1U);
  EXPECT_NEAR(scorer.score(), 0.5, 1e-9);
}

TEST(WoernerComplianceScorerTest, YieldingTargetScoresHigh) {
  WoernerComplianceScorer scorer(30.0);

  scorer.add_sample(sample_at(0.0, 120.0, 900.0, 270.0));
  scorer.add_sample(sample_at(15.0, 280.0, 980.0, 285.0));
  scorer.add_sample(sample_at(30.0, 460.0, 1100.0, 298.0));

  EXPECT_GT(scorer.score(), 0.75);
}

TEST(WoernerComplianceScorerTest, NonYieldingClosingTargetScoresLow) {
  WoernerComplianceScorer scorer(30.0);

  scorer.add_sample(sample_at(0.0, 165.0, 900.0, 315.0));
  scorer.add_sample(sample_at(15.0, 160.0, 720.0, 315.0));
  scorer.add_sample(sample_at(30.0, 155.0, 540.0, 315.0));

  EXPECT_LT(scorer.score(), 0.4);
}

TEST(WoernerComplianceScorerTest, HistoryWindowPrunesOldSamples) {
  WoernerComplianceScorer scorer(20.0);

  scorer.add_sample(sample_at(0.0, 100.0, 900.0, 270.0));
  scorer.add_sample(sample_at(25.0, 120.0, 700.0, 270.0));
  scorer.add_sample(sample_at(35.0, 130.0, 600.0, 270.0));

  EXPECT_EQ(scorer.sample_count(), 2U);
}

}  // namespace mass_l3::m2
