/// Unit tests for ConformanceScoreCalculator (E/T/H three-axis scoring).
/// PATH-S constraints: noexcept, no exceptions, no dynamic allocation in compute().
/// Required test count: 15 (spec Section 7.2).

#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"

namespace mass_l3::m1 {
namespace {

// ---------------------------------------------------------------------------
// Helpers: default threshold sets for testing
// ---------------------------------------------------------------------------

EScoreThresholds MakeDefaultEThresholds() {
  EScoreThresholds t{};
  t.visibility_full_nm = 2.0;
  t.visibility_degraded_nm = 1.0;
  t.visibility_marginal_nm = 0.5;
  t.sea_state_full_hs = 2.5;
  t.sea_state_degraded_hs = 3.0;
  t.sea_state_marginal_hs = 4.0;
  return t;
}

TScoreThresholds MakeDefaultTThresholds() {
  TScoreThresholds t{};
  t.comm_delay_ok_s = 2.0;
  t.t_score_comm_ok = 0.6;
  t.t_score_comm_bad = 0.3;
  return t;
}

HScoreThresholds MakeDefaultHThresholds() {
  HScoreThresholds h{};
  h.h_score_tmr_available = 1.0;
  h.h_score_tmr_unavailable = 0.5;
  return h;
}

// ===========================================================================
// Factory / creation tests
// ===========================================================================

/// 1. CreateRejectsNonNormalizedWeights
/// w_E + w_T + w_H != 1.0 must produce ErrorCode::WeightsNotNormalized.
TEST(ConformanceScoreTest, CreateRejectsNonNormalizedWeights) {
  ScoreWeights bad{0.5, 0.2, 0.1};  // sum = 0.8
  auto result = ConformanceScoreCalculator::create(
      bad, MakeDefaultEThresholds(), MakeDefaultTThresholds(), MakeDefaultHThresholds());
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ErrorCode::WeightsNotNormalized);
}

// ===========================================================================
// Environment (E) score tests
// ===========================================================================

/// 2. EScoreFullConditions
/// visibility >= 2.0 nm AND Hs <= 2.5 m -> 1.0 (ODD-A).
TEST(ConformanceScoreTest, EScoreFullConditions) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(2.0, 2.5), 1.0);
}

/// 3. EScoreBoundary
/// Boundary values vis == 2.0, Hs == 2.5 must still return 1.0.
TEST(ConformanceScoreTest, EScoreBoundary) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(2.0, 2.5), 1.0);
  // Also just below boundary: vis=1.999, Hs=2.499 should not match ODD-A
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(1.999, 2.499), 0.7);
}

/// 4. EScoreOutOfOdd
/// visibility 0.3 nm with Hs 5.0 m -> no threshold matches -> 0.0.
TEST(ConformanceScoreTest, EScoreOutOfOdd) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(0.3, 5.0), 0.0);
}

/// 12. EScoreDegradedConditions
/// visibility >= 1.0 AND Hs <= 3.0 -> 0.7 (ODD-B).
TEST(ConformanceScoreTest, EScoreDegradedConditions) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(1.0, 3.0), 0.7);
  // Hs degraded but visibility still good: vis=1.5, Hs=2.8 -> 0.7
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(1.5, 2.8), 0.7);
}

/// 13. EScoreMarginalConditions
/// visibility >= 0.5 OR Hs <= 4.0 -> 0.4 (ODD-D warning).
TEST(ConformanceScoreTest, EScoreMarginalConditions) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  // vis >= 0.5, Hs > 4.0 -> 0.4 (vis condition satisfied)
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(0.5, 5.0), 0.4);
  // vis < 0.5, Hs <= 4.0 -> 0.4 (Hs condition satisfied)
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(0.4, 4.0), 0.4);
  // vis >= 0.5, Hs <= 4.0 -> 0.4 (both)
  EXPECT_DOUBLE_EQ(calc->evaluate_e_score(0.6, 3.5), 0.4);
}

// ===========================================================================
// Task (T) score tests
// ===========================================================================

/// 6. TScoreFullHealth
/// GNSS good, radar ok, comm ok, no critical faults -> 1.0.
TEST(ConformanceScoreTest, TScoreFullHealth) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  TScoreInputs t{};
  t.gnss_quality_good = true;
  t.radar_health_ok = true;
  t.comm_ok = true;
  t.comm_delay_s = 0.1;
  t.any_sensor_critical = false;
  EXPECT_DOUBLE_EQ(calc->evaluate_t_score(t), 1.0);
}

/// 7. TScoreDegraded
/// GNSS degraded (but comm delay within limits) -> 0.6.
TEST(ConformanceScoreTest, TScoreDegraded) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  TScoreInputs t{};
  t.gnss_quality_good = false;  // degraded
  t.radar_health_ok = true;
  t.comm_ok = true;
  t.comm_delay_s = 0.1;  // < 2.0s
  t.any_sensor_critical = false;
  EXPECT_DOUBLE_EQ(calc->evaluate_t_score(t), 0.6);
}

/// 8. TScoreCritical
/// any_sensor_critical true -> 0.3 (even if other metrics are nominal).
TEST(ConformanceScoreTest, TScoreCritical) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  TScoreInputs t{};
  t.gnss_quality_good = true;
  t.radar_health_ok = true;
  t.comm_ok = true;
  t.comm_delay_s = 3.0;  // > 2.0s (also triggers the "comm bad" path)
  t.any_sensor_critical = true;
  EXPECT_DOUBLE_EQ(calc->evaluate_t_score(t), 0.3);
}

// ===========================================================================
// Human (H) score tests
// ===========================================================================

/// 9. HScoreTmrAvailable
/// TMR available AND comm ok -> 1.0.
TEST(ConformanceScoreTest, HScoreTmrAvailable) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  HScoreInputs h{};
  h.tmr_available = true;
  h.comm_ok = true;
  EXPECT_DOUBLE_EQ(calc->evaluate_h_score(h), 1.0);
}

/// 10. HScoreTmrUnavailable
/// No TMR (regardless of comm state) -> 0.5.
TEST(ConformanceScoreTest, HScoreTmrUnavailable) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  HScoreInputs h{};
  h.tmr_available = false;
  h.comm_ok = true;
  EXPECT_DOUBLE_EQ(calc->evaluate_h_score(h), 0.5);
  // Also verify: no TMR and no comm
  h.comm_ok = false;
  EXPECT_DOUBLE_EQ(calc->evaluate_h_score(h), 0.5);
}

// ===========================================================================
// Combined score tests
// ===========================================================================

/// 11. ConformanceScoreWeightedSum
/// Verify the weighted sum calculation: E=1.0, T=0.6, H=0.5
/// with weights (0.5, 0.3, 0.2) -> 0.5*1.0 + 0.3*0.6 + 0.2*0.5 = 0.78.
TEST(ConformanceScoreTest, ConformanceScoreWeightedSum) {
  ScoreWeights w{0.5, 0.3, 0.2};
  auto calc = ConformanceScoreCalculator::create(
      w, MakeDefaultEThresholds(), MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  ScoringInputs inputs{};
  inputs.visibility_nm = 2.0;
  inputs.sea_state_hs = 2.5;   // E = 1.0
  inputs.gnss_quality_good = false;  // degraded
  inputs.radar_health_ok = true;
  inputs.comm_ok = true;
  inputs.comm_delay_s = 0.1;
  inputs.any_sensor_critical = false;  // T = 0.6
  inputs.tmr_available = false;  // H = 0.5

  ScoreTriple result = calc->compute(inputs);
  EXPECT_DOUBLE_EQ(result.e_score, 1.0);
  EXPECT_DOUBLE_EQ(result.t_score, 0.6);
  EXPECT_DOUBLE_EQ(result.h_score, 0.5);
  EXPECT_DOUBLE_EQ(result.conformance_score, 0.5 * 1.0 + 0.3 * 0.6 + 0.2 * 0.5);
}

/// 5. ScoreNeverExceedsOne
/// When all sub-scores are maximum, the combined must be clamped to <= 1.0.
TEST(ConformanceScoreTest, ScoreNeverExceedsOne) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  ScoringInputs best{};
  best.visibility_nm = 2.0;
  best.sea_state_hs = 2.5;   // E = 1.0
  best.gnss_quality_good = true;
  best.radar_health_ok = true;
  best.comm_ok = true;
  best.comm_delay_s = 0.1;
  best.any_sensor_critical = false;  // T = 1.0
  best.tmr_available = true;  // H = 1.0
  ScoreTriple result = calc->compute(best);
  EXPECT_LE(result.conformance_score, 1.0);
  EXPECT_GE(result.conformance_score, 0.0);
}

/// 14. ScoreClampedAtZero
/// With all sub-scores at their minimum, the combined score must be
/// >= 0 (clamped from below). No negative scores.
TEST(ConformanceScoreTest, ScoreClampedAtZero) {
  auto calc = ConformanceScoreCalculator::create(
      ScoreWeights{0.4, 0.3, 0.3}, MakeDefaultEThresholds(),
      MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  ScoringInputs worst{};
  worst.visibility_nm = 0.3;
  worst.sea_state_hs = 5.0;   // E = 0.0 (OUT)
  worst.gnss_quality_good = true;
  worst.radar_health_ok = true;
  worst.comm_ok = true;
  worst.comm_delay_s = 3.0;   // > 2.0s -> T = 0.3
  worst.any_sensor_critical = false;
  worst.tmr_available = false;  // H = 0.5

  ScoreTriple result = calc->compute(worst);
  EXPECT_DOUBLE_EQ(result.e_score, 0.0);
  EXPECT_GE(result.conformance_score, 0.0);
}

/// 15. ScoreEqualsWeightsAtUnity
/// All three sub-scores = 1.0 -> conformance_score must equal 1.0
/// regardless of weight distribution (as long as sum = 1.0).
TEST(ConformanceScoreTest, ScoreEqualsWeightsAtUnity) {
  ScoreWeights w{0.5, 0.3, 0.2};
  auto calc = ConformanceScoreCalculator::create(
      w, MakeDefaultEThresholds(), MakeDefaultTThresholds(), MakeDefaultHThresholds());
  ASSERT_TRUE(calc.has_value());
  ScoringInputs all_good{};
  all_good.visibility_nm = 2.0;
  all_good.sea_state_hs = 2.5;   // E = 1.0
  all_good.gnss_quality_good = true;
  all_good.radar_health_ok = true;
  all_good.comm_ok = true;
  all_good.comm_delay_s = 0.1;
  all_good.any_sensor_critical = false;  // T = 1.0
  all_good.tmr_available = true;  // H = 1.0
  ScoreTriple result = calc->compute(all_good);
  EXPECT_DOUBLE_EQ(result.conformance_score, 1.0);
  EXPECT_DOUBLE_EQ(result.e_score, 1.0);
  EXPECT_DOUBLE_EQ(result.t_score, 1.0);
  EXPECT_DOUBLE_EQ(result.h_score, 1.0);
}

}  // namespace
}  // namespace mass_l3::m1
