#include <gtest/gtest.h>

#include <cstdint>
#include <cmath>
#include <string>

#include "l3_risk_model/risk_model.hpp"
#include "m4_behavior_arbiter/colregs_directive.hpp"

namespace mass_l3::m4 {
namespace {

constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr std::uint8_t kRoleStandOn = 0U;
constexpr std::uint8_t kRoleBothGiveWay = 2U;

l3_msgs::msg::COLREGsConstraint make_msg(
    const std::string& direction,
    double min_deg,
    std::uint8_t primary_role = kRoleGiveWay,
    const std::string& phase = "PRESERVE_COURSE") {
  l3_msgs::msg::COLREGsConstraint msg;
  msg.schema_version = 114U;
  msg.conflict_detected = true;
  msg.phase = phase;
  msg.primary_role = primary_role;
  msg.primary_preferred_direction = direction;
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = min_deg;
  msg.constraints.push_back(c);
  return msg;
}

TEST(ColregsDirective, StarboardBuildsPositiveWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 30.0);

  const auto required = required_deviation_deg(directive, 3704.0);
  EXPECT_DOUBLE_EQ(required, 30.0);

  const auto window = directive_heading_window(0.0, directive, required);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 15.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 45.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, required);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 30.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 180.0);
}

TEST(ColregsDirective, PortBuildsNegativeWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("PORT", 25.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Port);

  const auto window = directive_heading_window(0.0, directive, 25.0);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 320.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 350.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, 25.0);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 180.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 335.0);
}

TEST(ColregsDirective, StarboardSplitWrappedAllowedRangeForLinearSolver) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  const auto ranges = directive_allowed_ranges(200.0, directive, 30.0);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 230.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 360.0);
  EXPECT_DOUBLE_EQ(ranges[1].first, 0.0);
  EXPECT_DOUBLE_EQ(ranges[1].second, 20.0);
}

TEST(ColregsDirective, PortSplitWrappedAllowedRangeForLinearSolver) {
  const auto directive = extract_colregs_directive(make_msg("PORT", 25.0));

  const auto ranges = directive_allowed_ranges(75.0, directive, 25.0);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 255.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 360.0);
  EXPECT_DOUBLE_EQ(ranges[1].first, 0.0);
  EXPECT_DOUBLE_EQ(ranges[1].second, 50.0);
}

TEST(ColregsDirective, StarboardKeepsDirectRangeWhenBothEndpointsWrap) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 150.0));

  const auto ranges = directive_allowed_ranges(230.0, directive, 150.0);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 20.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 50.0);
}

TEST(ColregsDirective, NonQuarteringBowCapsDeviationAtSeventyFiveDegrees) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, false), 75.0);
}

TEST(ColregsDirective, QuarteringPreservesWideDeviationEnvelope) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, true), 150.0);
}

TEST(ColregsDirective, ExplicitHighMinimumAlterationOverridesBowCap) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 95.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, false), 95.0);
}

TEST(ColregsDirective, StandOnIndependentActionDoesNotUseQuarteringWideEnvelope) {
  const auto directive = extract_colregs_directive(
      make_msg("STARBOARD", 15.0, kRoleStandOn, "INDEPENDENT_ACTION"));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, true), 60.0);
}

TEST(ColregsDirective, StandOnCriticalActionUsesEmergencyDeviationEnvelope) {
  const auto directive = extract_colregs_directive(
      make_msg("STARBOARD", 15.0, kRoleStandOn, "CRITICAL_ACTION"));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, false), 75.0);
  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, true), 150.0);
}

TEST(ColregsDirective, StandOnCriticalDangerRiskRequiresMaximumDeviation) {
  auto directive = extract_colregs_directive(
      make_msg("STARBOARD", 15.0, kRoleStandOn, "CRITICAL_ACTION"));
  directive.primary_risk_phase = "Critical";
  directive.primary_danger_margin_m = -20.0;

  EXPECT_DOUBLE_EQ(
      required_deviation_deg(directive, 5000.0, 1500.0, 2.5, 75.0),
      75.0);
}

TEST(ColregsDirective, ReduceSpeedAndHoldDoNotCreateHeadingWindow) {
  const auto reduce = extract_colregs_directive(make_msg("REDUCE_SPEED", 15.0));
  EXPECT_EQ(reduce.direction, ColregsDirection::ReduceSpeed);
  EXPECT_FALSE(directive_heading_window(90.0, reduce, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, reduce, 15.0).empty());

  const auto hold = extract_colregs_directive(make_msg("HOLD", 15.0));
  EXPECT_EQ(hold.direction, ColregsDirection::Hold);
  EXPECT_FALSE(directive_heading_window(90.0, hold, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, hold, 15.0).empty());
}

TEST(ColregsDirective, NoConflictProducesInactiveHold) {
  auto msg = make_msg("STARBOARD", 30.0);
  msg.conflict_detected = false;

  const auto directive = extract_colregs_directive(msg);
  EXPECT_FALSE(directive.conflict_active);
  EXPECT_EQ(directive.direction, ColregsDirection::Hold);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 0.0);
}

TEST(ColregsDirective, Rule15CrossingKeepsStarboardAlterationWhenSpeedReductionHelps) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.rule15_active = true;

  const mass_l3::risk::OwnShipInput own{0.0, 0.0, 0.0, 5.0, 46.0, 0.95, false};
  const mass_l3::risk::OwnShipInput slowed{0.0, 0.0, 0.0, 2.5, 46.0, 0.95, false};
  const mass_l3::risk::TargetInput target{
      "TS001", 1200.0, 260.0, -3.14159265358979323846 / 2.0, 6.0, 600.0, 220.0, 0.9};
  const auto current_risk =
      mass_l3::risk::evaluate_target(own, target, mass_l3::risk::ColregsDuty::GiveWay);
  const auto slowed_risk =
      mass_l3::risk::evaluate_target(slowed, target, mass_l3::risk::ColregsDuty::GiveWay);

  apply_primary_risk_guidance(directive, current_risk, slowed_risk);

  EXPECT_EQ(directive.primary_threat_id, "TS001");
  EXPECT_EQ(directive.primary_risk_phase, "Monitor");
  EXPECT_FALSE(directive.speed_reduction_preferred);
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_GT(
      required_deviation_deg(directive, current_risk.range_m, 1500.0, 2.5, 75.0),
      0.0);
}

TEST(ColregsDirective, GiveWayOvertakingCanPreferSpeedReductionWhenItArrestsClosing) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "PRESERVE_COURSE";

  mass_l3::risk::RiskVector current_risk;
  current_risk.target_id = "TS001";
  current_risk.range_m = 1100.0;
  current_risk.closing_speed_mps = 2.4;
  current_risk.tcpa_s = 520.0;
  current_risk.warning_margin_m = 80.0;
  current_risk.danger_margin_m = 410.0;
  current_risk.risk_phase = mass_l3::risk::RiskPhase::Monitor;
  current_risk.risk_score = 0.24;

  mass_l3::risk::RiskVector slowed_risk = current_risk;
  slowed_risk.closing_speed_mps = 0.2;
  slowed_risk.tcpa_s = 900.0;

  apply_primary_risk_guidance(directive, current_risk, slowed_risk);

  EXPECT_TRUE(directive.speed_reduction_preferred);
  EXPECT_EQ(directive.direction, ColregsDirection::ReduceSpeed);
}

TEST(ColregsDirective, Rule13GiveWayKeepsStarboardInsteadOfPureSpeedReduction) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 65.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "SOUND_WARNING";
  directive.rule13_active = true;

  mass_l3::risk::RiskVector current_risk;
  current_risk.target_id = "TS001";
  current_risk.range_m = 1100.0;
  current_risk.closing_speed_mps = 2.4;
  current_risk.tcpa_s = 520.0;
  current_risk.warning_margin_m = -124.0;
  current_risk.danger_margin_m = 410.0;
  current_risk.risk_phase = mass_l3::risk::RiskPhase::Warning;
  current_risk.risk_score = 0.27;

  mass_l3::risk::RiskVector slowed_risk = current_risk;
  slowed_risk.closing_speed_mps = 0.2;
  slowed_risk.tcpa_s = 900.0;
  slowed_risk.warning_margin_m = -80.0;

  apply_primary_risk_guidance(directive, current_risk, slowed_risk);

  EXPECT_FALSE(directive.speed_reduction_preferred);
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
}

TEST(ColregsDirective, WarningRiskAddsAuxiliarySpeedCapWithoutChangingTurnDirection) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "SOUND_WARNING";
  directive.rule15_active = true;
  directive.primary_risk_phase = "Warning";
  directive.primary_warning_margin_m = -40.0;
  directive.primary_danger_margin_m = 140.0;

  EXPECT_TRUE(dynamic_risk_requires_speed_cap(directive));
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
}

TEST(ColregsDirective, ClearAndMonitorRiskDoNotAddAuxiliarySpeedCap) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "SOUND_WARNING";
  directive.rule15_active = true;
  directive.primary_warning_margin_m = 40.0;
  directive.primary_danger_margin_m = 140.0;

  directive.primary_risk_phase = "Monitor";
  EXPECT_FALSE(dynamic_risk_requires_speed_cap(directive));

  directive.primary_risk_phase = "Clear";
  EXPECT_FALSE(dynamic_risk_requires_speed_cap(directive));
}

TEST(ColregsDirective, ClearRiskDoesNotOverrideTurnWithSpeedReduction) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "SOUND_WARNING";

  mass_l3::risk::RiskVector current_risk;
  current_risk.target_id = "TS001";
  current_risk.range_m = 1200.0;
  current_risk.closing_speed_mps = 2.4;
  current_risk.tcpa_s = 520.0;
  current_risk.warning_margin_m = 500.0;
  current_risk.danger_margin_m = 900.0;
  current_risk.risk_phase = mass_l3::risk::RiskPhase::Clear;
  current_risk.risk_score = 0.20;

  mass_l3::risk::RiskVector slowed_risk = current_risk;
  slowed_risk.closing_speed_mps = 0.2;
  slowed_risk.tcpa_s = 900.0;
  slowed_risk.warning_margin_m = 540.0;

  apply_primary_risk_guidance(directive, current_risk, slowed_risk);

  EXPECT_FALSE(directive.speed_reduction_preferred);
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
}

TEST(ColregsDirective, Rule13GiveWayWarningRiskDoesNotCapOvertakingSpeed) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 65.0;
  directive.primary_role = kRoleGiveWay;
  directive.phase = "SOUND_WARNING";
  directive.rule13_active = true;
  directive.primary_risk_phase = "Warning";
  directive.primary_warning_margin_m = -20.0;
  directive.primary_danger_margin_m = 250.0;
  directive.primary_closing_speed_mps = 1.7;
  directive.primary_tdv_warning_s = 120.0;

  EXPECT_FALSE(dynamic_risk_requires_speed_cap(directive));
}

// T9 / D-5: Rule 14 head-on (BOTH_GIVE_WAY) must NOT have its turn direction
// overridden to REDUCE_SPEED. COLREG Rule 14(a) requires both vessels to alter
// to starboard; speed reduction may serve as an auxiliary speed_max constraint
// but must not replace the turn direction. Give-way on Rule 13/15/16 may still
// prefer speed reduction (see GiveWayOvertakingCanPreferSpeedReduction above),
// but Rule 14's port-to-port pass geometry demands a real starboard turn.
TEST(ColregsDirective, T9_Rule14BothGiveWayForbidsSpeedReductionDirectionOverride) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleBothGiveWay;
  directive.rule14_active = true;

  mass_l3::risk::RiskVector current_risk;
  current_risk.target_id = "TS001";
  current_risk.range_m = 1100.0;
  current_risk.closing_speed_mps = 2.4;
  current_risk.tcpa_s = 520.0;  // > 180 ample
  current_risk.warning_margin_m = 80.0;
  current_risk.danger_margin_m = 410.0;  // not danger
  current_risk.risk_phase = mass_l3::risk::RiskPhase::Monitor;
  current_risk.risk_score = 0.24;

  mass_l3::risk::RiskVector slowed_risk = current_risk;
  slowed_risk.closing_speed_mps = 0.2;   // arrests closing
  slowed_risk.tcpa_s = 900.0;
  slowed_risk.warning_margin_m = 150.0;  // improves margin

  apply_primary_risk_guidance(directive, current_risk, slowed_risk);

  // Speed reduction still allowed as auxiliary (speed_reduction_preferred may
  // be true), but the turn DIRECTION must stay STARBOARD for Rule 14.
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard)
      << "Rule 14 BOTH_GIVE_WAY direction must stay STARBOARD (COLREG 14(a))";
}

TEST(ColregsDirective, DangerCrossingKeepsStarboardAlteration) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.rule15_active = true;

  const mass_l3::risk::OwnShipInput own{0.0, 0.0, 0.0, 5.0, 46.0, 0.95, false};
  const mass_l3::risk::TargetInput target{
      "TS001", 250.0, 40.0, -3.14159265358979323846 / 2.0, 6.0, 120.0, 45.0, 0.9};
  const auto risk =
      mass_l3::risk::evaluate_target(own, target, mass_l3::risk::ColregsDuty::GiveWay);

  apply_primary_risk_guidance(directive, risk, risk);

  EXPECT_FALSE(directive.speed_reduction_preferred);
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_GE(required_deviation_deg(directive, risk.range_m, 1500.0, 2.5, 75.0), 45.0);
}

TEST(ColregsDirective, MonitorWarningEntryInsideTmrAddsAuxiliarySpeedCap) {
  ColregsDirective directive;
  directive.conflict_active = true;
  directive.direction = ColregsDirection::Starboard;
  directive.min_alteration_deg = 30.0;
  directive.primary_role = kRoleGiveWay;
  directive.rule15_active = true;

  mass_l3::risk::RiskVector current_risk;
  current_risk.target_id = "TS001";
  current_risk.range_m = 1500.0;
  current_risk.closing_speed_mps = 20.0;
  current_risk.tcpa_s = 120.0;
  current_risk.warning_margin_m = 900.0;
  current_risk.danger_margin_m = 1200.0;
  current_risk.tdv_warning_s = 45.0;
  current_risk.risk_phase = mass_l3::risk::RiskPhase::Monitor;

  apply_primary_risk_guidance(directive, current_risk, current_risk);

  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_FALSE(directive.speed_reduction_preferred);
  EXPECT_TRUE(dynamic_risk_requires_speed_cap(directive));
}

}  // namespace
}  // namespace mass_l3::m4
