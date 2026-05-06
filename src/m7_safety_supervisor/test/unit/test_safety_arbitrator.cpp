#include <gtest/gtest.h>

#include "m7_safety_supervisor/arbitrator/safety_arbitrator.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "l3_msgs/msg/safety_alert.hpp"

using namespace mass_l3::m7;
using namespace mass_l3::m7::arbitrator;
using namespace mass_l3::m7::mrm;
using namespace mass_l3::m7::iec61508;
using namespace mass_l3::m7::sotif;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ScenarioContext make_nominal_ctx()
{
  ScenarioContext ctx{};
  ctx.assumption.total_violation_count = 0u;
  ctx.watchdog.any_critical = false;
  ctx.watchdog.critical_count = 0u;
  ctx.performance.cpa_trend_degrading = false;
  ctx.performance.multiple_targets_nearby = false;
  return ctx;
}

static DiagnosticResult make_diag_ok()
{
  DiagnosticResult d{};
  d.conformance_score_valid = true;
  d.cpa_internal_consistent = true;
  d.colregs_target_id_match = true;
  d.fault_count = 0u;
  return d;
}

static DiagnosticResult make_diag_fault()
{
  DiagnosticResult d{};
  d.conformance_score_valid = false;  // one check failed
  d.cpa_internal_consistent = true;
  d.colregs_target_id_match = true;
  d.fault_count = 1u;
  return d;
}

static MrmDecision make_mrm_none()
{
  MrmDecision m{};
  m.mrm_id = MrmId::kNone;
  m.confidence = 0.5F;
  m.rationale = "none";
  return m;
}

static MrmDecision make_mrm_drift()
{
  MrmDecision m{};
  m.mrm_id = MrmId::kMrm01_Drift;
  m.confidence = 0.90F;
  m.rationale = "MRM-01-DRIFT";
  return m;
}

static auto now() { return std::chrono::steady_clock::now(); }

// ---------------------------------------------------------------------------
// Test 1: No faults, no violations, no perf issues → SEVERITY_INFO
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, NominalConditions_SeverityInfo)
{
  SafetyArbitrator arb;
  auto ctx  = make_nominal_ctx();
  auto diag = make_diag_ok();
  auto mrm  = make_mrm_none();

  auto const alert = arb.arbitrate(ctx, mrm, diag, false, now());

  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_INFO);
}

// ---------------------------------------------------------------------------
// Test 2: IEC 61508 fault active (diagnostic check failed) → ALERT_IEC61508_FAULT
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, DiagnosticFaultActive_Iec61508Alert)
{
  SafetyArbitrator arb;
  auto ctx  = make_nominal_ctx();
  auto diag = make_diag_fault();
  auto mrm  = make_mrm_drift();

  auto const alert = arb.arbitrate(ctx, mrm, diag, false, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT);
  EXPECT_GE(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_WARNING);
}

// ---------------------------------------------------------------------------
// Test 3: Watchdog critical → ALERT_IEC61508_FAULT with SEVERITY_CRITICAL
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, WatchdogCritical_Iec61508AlertCritical)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.watchdog.any_critical = true;
  ctx.watchdog.critical_count = 1u;

  auto const alert = arb.arbitrate(ctx, make_mrm_drift(), make_diag_ok(), false, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT);
  EXPECT_GE(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
}

// ---------------------------------------------------------------------------
// Test 4: Single SOTIF violation → ALERT_SOTIF_ASSUMPTION WARNING
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, OneSotifViolation_AssumptionWarning)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.assumption.total_violation_count = 1u;

  auto const alert = arb.arbitrate(ctx, make_mrm_none(), make_diag_ok(), false, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_WARNING);
}

// ---------------------------------------------------------------------------
// Test 5: Two SOTIF violations → ALERT_SOTIF_ASSUMPTION CRITICAL
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, TwoSotifViolations_AssumptionCritical)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.assumption.total_violation_count = 2u;

  auto const alert = arb.arbitrate(ctx, make_mrm_drift(), make_diag_ok(), false, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
}

// ---------------------------------------------------------------------------
// Test 6: Extreme scenario (3+ violations) → SEVERITY_MRC_REQUIRED
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, ExtremeScenario_MrcRequired)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.assumption.total_violation_count = 3u;

  auto const alert = arb.arbitrate(ctx, make_mrm_drift(), make_diag_ok(), true, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

// ---------------------------------------------------------------------------
// Test 7: Performance degrading → ALERT_PERFORMANCE_DEGRADED
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, PerformanceDegrading_PerfAlert)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.performance.cpa_trend_degrading = true;

  auto const alert = arb.arbitrate(ctx, make_mrm_drift(), make_diag_ok(), false, now());

  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_PERFORMANCE_DEGRADED);
}

// ---------------------------------------------------------------------------
// Test 8: Priority — watchdog critical takes precedence over perf degrading
// ALERT_IEC61508_FAULT must be highest severity in the pool
// ---------------------------------------------------------------------------
TEST(SafetyArbitratorTest, WatchdogAndPerf_WatchdogPriority)
{
  SafetyArbitrator arb;
  auto ctx = make_nominal_ctx();
  ctx.watchdog.any_critical = true;
  ctx.watchdog.critical_count = 1u;
  ctx.performance.cpa_trend_degrading = true;

  auto const alert = arb.arbitrate(ctx, make_mrm_drift(), make_diag_ok(), false, now());

  // Watchdog critical has SEVERITY_CRITICAL; perf has SEVERITY_WARNING → watchdog wins
  EXPECT_EQ(alert.alert_type, l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT);
  EXPECT_GE(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
}
