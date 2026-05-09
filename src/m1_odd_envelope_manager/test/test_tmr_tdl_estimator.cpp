/// Unit tests for TmrTdlEstimator (TMR/TDL estimation).
/// PATH-S constraints: noexcept, no exceptions, no dynamic allocation.
/// Required test count: 12 (spec Section 7.2).

#include <chrono>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"

namespace mass_l3::m1 {
namespace {

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers: default parameter set for testing
// ---------------------------------------------------------------------------

TmrTdlParams MakeDefaultParams() {
  TmrTdlParams p{};
  p.tmr_baseline_s = 60.0;
  p.tcpa_coefficient = 0.6;
  p.tmr_min_s = 30.0;
  p.tmr_max_s = 600.0;
  p.tdl_min_s = 0.0;
  p.tdl_max_s = 600.0;
  return p;
}

SystemHealthSnapshot MakeHealthySysHealth() {
  SystemHealthSnapshot h{};
  h.mttf_estimate_s = 10000.0;
  h.heartbeat_recency_s = 0.5;
  h.fault_count = 0;
  h.has_redundancy = true;
  return h;
}

// ===========================================================================
// Factory / creation tests
// ===========================================================================

/// 1. CreateWithValidParams — factory accepts valid params and returns ok.
TEST(TmrTdlEstimatorTest, CreateWithValidParams) {
  auto result = TmrTdlEstimator::create(MakeDefaultParams());
  EXPECT_TRUE(result.has_value());
}

// ===========================================================================
// TDL computation tests
// ===========================================================================

/// 2. TdlFromTcpa — TDL is governed by TCPA * coefficient when other
///    components provide larger windows.
TEST(TmrTdlEstimatorTest, TdlFromTcpa) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;  // 100 * 0.6 = 60
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();  // -> 300
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // TDL = min(60, 300, 300) = 60
  EXPECT_DOUBLE_EQ(result.tdl_s, 60.0);
}

/// 3. TdlFromCommOk — TDL is limited by a degraded communication window.
TEST(TmrTdlEstimatorTest, TdlFromCommOk) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 500.0;  // 500 * 0.6 = 300
  in.current_rtt_s = 0.8;  // <= 1000ms -> 60s window
  in.system_health = MakeHealthySysHealth();  // -> 300
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // TDL = min(300, 60, 300) = 60
  EXPECT_DOUBLE_EQ(result.tdl_s, 60.0);
}

/// 4. TdlFromSysHealth — TDL is limited by critically degraded system health.
TEST(TmrTdlEstimatorTest, TdlFromSysHealth) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  SystemHealthSnapshot critical_health{};
  critical_health.mttf_estimate_s = 10000.0;
  critical_health.heartbeat_recency_s = 0.5;
  critical_health.fault_count = 0;
  critical_health.has_redundancy = false;  // no redundancy -> critical (30s)

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;  // 100 * 0.6 = 60
  in.current_rtt_s = 0.05;  // -> 300s window
  in.system_health = critical_health;  // -> 30s
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // TDL = min(60, 300, 30) = 30
  EXPECT_DOUBLE_EQ(result.tdl_s, 30.0);
}

// ===========================================================================
// TMR computation tests
// ===========================================================================

/// 5. TmrBaseline — default TMR = TMR_baseline when H_score is 1.0.
TEST(TmrTdlEstimatorTest, TmrBaseline) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = true;  // factor = 1.0

  TmrTdlPair result = calc->compute(in);
  EXPECT_DOUBLE_EQ(result.tmr_s, 60.0);
}

/// 6. TmrClampedMin — TMR clamped to tmr_min_s when calculated value below
///    minimum.
TEST(TmrTdlEstimatorTest, TmrClampedMin) {
  TmrTdlParams p = MakeDefaultParams();
  p.tmr_min_s = 80.0;  // clamp floor above baseline*0.5=30
  auto calc = TmrTdlEstimator::create(p);
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = false;  // factor = 0.5 -> 60 * 0.5 = 30

  TmrTdlPair result = calc->compute(in);
  // TMR = max(30, 80) = 80
  EXPECT_DOUBLE_EQ(result.tmr_s, 80.0);
}

/// 7. TmrClampedMax — TMR clamped to tmr_max_s when calculated value exceeds
///    maximum.
TEST(TmrTdlEstimatorTest, TmrClampedMax) {
  TmrTdlParams p = MakeDefaultParams();
  p.tmr_max_s = 50.0;  // clamp ceiling below baseline=60
  auto calc = TmrTdlEstimator::create(p);
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = true;  // factor = 1.0 -> 60

  TmrTdlPair result = calc->compute(in);
  // TMR = min(60, 50) = 50
  EXPECT_DOUBLE_EQ(result.tmr_s, 50.0);
}

/// 8. TmrFromHScore — TMR halved when H_score is unavailable
///    (h_score_tmr_available = false).
TEST(TmrTdlEstimatorTest, TmrFromHScore) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = false;  // factor = 0.5

  TmrTdlPair result = calc->compute(in);
  // TMR = 60 * 0.5 = 30
  EXPECT_DOUBLE_EQ(result.tmr_s, 30.0);
}

// ===========================================================================
// Forecast tests
// ===========================================================================

/// 9. ForecastZeroHorizon — forecast with horizon=0 equals compute().
TEST(TmrTdlEstimatorTest, ForecastZeroHorizon) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = true;

  TmrTdlPair current = calc->compute(in);
  TmrTdlPair fcast = calc->forecast(in, 0s);
  EXPECT_DOUBLE_EQ(fcast.tmr_s, current.tmr_s);
  EXPECT_DOUBLE_EQ(fcast.tdl_s, current.tdl_s);
}

/// 10. ForecastWithHorizon — forecast with positive horizon degrades
///     both TMR and TDL.
TEST(TmrTdlEstimatorTest, ForecastWithHorizon) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 100.0;
  in.current_rtt_s = 0.05;
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = true;

  TmrTdlPair fcast = calc->forecast(in, 10s);
  // TMR = 60 - 10*0.1 = 59, TDL = 60 - 10*0.2 = 58
  EXPECT_DOUBLE_EQ(fcast.tmr_s, 59.0);
  EXPECT_DOUBLE_EQ(fcast.tdl_s, 58.0);
}

// ===========================================================================
// Boundary / edge case tests
// ===========================================================================

/// 11. TdlClampedToZero — minimum TDL clamped to tdl_min_s (0.0).
TEST(TmrTdlEstimatorTest, TdlClampedToZero) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  TmrTdlInputs in{};
  in.tcpa_min_s = 0.0;  // TCPA component = 0 * 0.6 = 0
  in.current_rtt_s = 0.05;  // -> 300s (non-limiting)
  in.system_health = MakeHealthySysHealth();  // -> min(1000, 300) = 300
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // TDL = min(0, 300, 300) = 0, clamped to [0, 600] = 0
  EXPECT_DOUBLE_EQ(result.tdl_s, 0.0);
}

/// 12. EstimateTCommOkDegraded — degraded communication returns 30s window.
TEST(TmrTdlEstimatorTest, EstimateTCommOkDegraded) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  // RTT well above 1000ms -> 30s window
  TmrTdlInputs in{};
  in.tcpa_min_s = 500.0;
  in.current_rtt_s = 2.0;  // > 1000ms
  in.system_health = MakeHealthySysHealth();
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // TDL = min(300, 30, 300) = 30
  EXPECT_DOUBLE_EQ(result.tdl_s, 30.0);

  // Also test RTT at exact boundary: RTT = 1.0 is <= 1000ms -> 60s
  in.current_rtt_s = 1.0;
  result = calc->compute(in);
  // TDL = min(300, 60, 300) = 60
  EXPECT_DOUBLE_EQ(result.tdl_s, 60.0);
}

/// 13. EstimateTSysHealthHealthy — healthy system gives min(MTTF*0.1, 300).
TEST(TmrTdlEstimatorTest, EstimateTSysHealthHealthy) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  SystemHealthSnapshot healthy{};
  healthy.mttf_estimate_s = 5000.0;  // > 3600
  healthy.heartbeat_recency_s = 0.3;
  healthy.fault_count = 0;
  healthy.has_redundancy = true;

  TmrTdlInputs in{};
  in.tcpa_min_s = 999.0;  // large enough not to limit
  in.current_rtt_s = 0.001;  // 300s window
  in.system_health = healthy;
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // T_sys_health = min(5000 * 0.1, 300) = 300
  // TDL = min(999*0.6=599.4, 300, 300) = 300
  EXPECT_DOUBLE_EQ(result.tdl_s, 300.0);
}

/// 14. EstimateTSysHealthDegraded — system with faults returns 60s window.
TEST(TmrTdlEstimatorTest, EstimateTSysHealthDegraded) {
  auto calc = TmrTdlEstimator::create(MakeDefaultParams());
  ASSERT_TRUE(calc.has_value());

  SystemHealthSnapshot degraded{};
  degraded.mttf_estimate_s = 10000.0;
  degraded.heartbeat_recency_s = 5.0;
  degraded.fault_count = 3;  // active faults -> degraded (60s)
  degraded.has_redundancy = true;

  TmrTdlInputs in{};
  in.tcpa_min_s = 999.0;  // large enough not to limit
  in.current_rtt_s = 0.001;  // 300s window
  in.system_health = degraded;
  in.h_score_tmr_available = true;

  TmrTdlPair result = calc->compute(in);
  // T_sys_health = 60 (degraded)
  // TDL = min(599.4, 300, 60) = 60
  EXPECT_DOUBLE_EQ(result.tdl_s, 60.0);
}

}  // namespace
}  // namespace mass_l3::m1
