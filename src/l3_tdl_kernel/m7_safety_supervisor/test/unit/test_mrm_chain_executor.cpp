#include <gtest/gtest.h>
#include "m7_safety_supervisor/core/mrm_chain_executor.hpp"
#include "l3_msgs/msg/safety_alert.hpp"

namespace mass_l3::m7::core {
namespace {

TEST(MrmChainExecutor, Hc1ViolationMapsToMrm03)
{
  CpaConsistencyResult cpa{}; cpa.consistent = false;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.any_critical = false;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-03");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
}

TEST(MrmChainExecutor, WatchdogMultiCriticalMapsToMrm02)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.multi_critical = true;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-02");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

TEST(MrmChainExecutor, DcFailMapsToMrm01)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.any_critical = false;
  DcConstraintResult dc{}; dc.dc_met = false; dc.violation = true; dc.effective_dc_pct = 75.0F;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-01");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

TEST(MrmChainExecutor, ColregsMismatchMapsToMrm01)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = false;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.any_critical = false;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-01");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
}

TEST(MrmChainExecutor, SpeedViolationMapsToMrm01)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = false; speed.excess_pct = 25.0F;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.any_critical = false;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-01");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

TEST(MrmChainExecutor, RotViolationMapsToMrm03)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = false; rot.excess_pct = 30.0F;
  WatchdogConstraintResult wd{}; wd.any_critical = false;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-03");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

TEST(MrmChainExecutor, SingleWatchdogMapsToMrm01)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{}; wd.any_critical = true; wd.multi_critical = false; wd.critical_count = 1;
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_EQ(alert.recommended_mrm, "MRM-01");
  EXPECT_EQ(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
}

TEST(MrmChainExecutor, AllClearNoAlert)
{
  CpaConsistencyResult cpa{}; cpa.consistent = true;
  ColregsGeometryResult colregs{}; colregs.consistent = true;
  SpeedLimitResult speed{}; speed.compliant = true;
  RotLimitResult rot{}; rot.compliant = true;
  WatchdogConstraintResult wd{};
  DcConstraintResult dc{}; dc.dc_met = true;
  auto const alert = build_safety_alert_from_hard_constraints(cpa, colregs, speed, rot, wd, dc);
  EXPECT_LE(alert.severity, l3_msgs::msg::SafetyAlert::SEVERITY_INFO);
}

}  // namespace
}  // namespace mass_l3::m7::core
