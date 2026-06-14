#include <gtest/gtest.h>

#include <cmath>

#include "l3_risk_model/risk_model.hpp"

namespace mass_l3::risk {
namespace {

constexpr double kPi = 3.14159265358979323846;

OwnShipInput nominal_ownship() {
  return OwnShipInput{0.0, 0.0, 0.0, 5.0, 46.0, 0.95, false};
}

TEST(RiskModelTest, ForwardTargetInsideDangerDomainWithShortTcpaIsCritical) {
  const auto own = nominal_ownship();
  const TargetInput target{"TS001", 280.0, 0.0, kPi, 5.0, 120.0, 45.0, 0.9};

  const auto risk = evaluate_target(own, target, ColregsDuty::GiveWay);

  EXPECT_EQ("TS001", risk.target_id);
  EXPECT_NEAR(280.0, risk.range_m, 1.0e-9);
  EXPECT_NEAR(0.0, risk.relative_bearing_deg, 1.0e-9);
  EXPECT_NEAR(120.0, risk.dcpa_m, 1.0e-9);
  EXPECT_NEAR(45.0, risk.tcpa_s, 1.0e-9);
  EXPECT_GT(risk.danger_ddv, 0.0);
  EXPECT_LT(risk.danger_margin_m, 0.0);
  EXPECT_EQ(RiskPhase::Critical, risk.risk_phase);
}

TEST(RiskModelTest, StarboardCrossingHasSmallerWarningMarginThanMirroredPortSideTarget) {
  const auto own = nominal_ownship();
  const TargetInput starboard{"TS-STBD", 0.0, 380.0, -kPi / 2.0, 5.0, 180.0, 240.0, 0.95};
  const TargetInput port{"TS-PORT", 0.0, -380.0, kPi / 2.0, 5.0, 180.0, 240.0, 0.95};

  const auto starboard_risk = evaluate_target(own, starboard, ColregsDuty::GiveWay);
  const auto port_risk = evaluate_target(own, port, ColregsDuty::StandOnHold);

  EXPECT_LT(starboard_risk.warning_margin_m, port_risk.warning_margin_m);
  EXPECT_GT(starboard_risk.warning_ddv, port_risk.warning_ddv);
}

TEST(RiskModelTest, OpeningTargetOutsideWarningReturnsClear) {
  const auto own = nominal_ownship();
  const TargetInput target{"TS-OPEN", 1200.0, 0.0, 0.0, 8.0, 1000.0, -120.0, 0.95};

  const auto risk = evaluate_target(own, target, ColregsDuty::Free);

  EXPECT_EQ(RiskPhase::Clear, risk.risk_phase);
  EXPECT_EQ(0.0, risk.warning_ddv);
  EXPECT_EQ(0.0, risk.danger_ddv);
  EXPECT_LT(risk.closing_speed_mps, 0.0);
}

TEST(RiskModelTest, ClosingSpeedSignTracksClosingAndOpeningPairs) {
  const auto own = nominal_ownship();
  const TargetInput closing{"TS-CLOSE", 500.0, 0.0, kPi, 5.0, 200.0, 120.0, 0.95};
  const TargetInput opening{"TS-AWAY", 500.0, 0.0, 0.0, 8.0, 400.0, 120.0, 0.95};

  const auto closing_risk = evaluate_target(own, closing, ColregsDuty::GiveWay);
  const auto opening_risk = evaluate_target(own, opening, ColregsDuty::Free);

  EXPECT_GT(closing_risk.closing_speed_mps, 0.0);
  EXPECT_LT(opening_risk.closing_speed_mps, 0.0);
}

TEST(RiskModelTest, NegativeSpeedsAreTreatedAsZeroForVelocityMath) {
  auto own = nominal_ownship();
  own.sog_mps = -5.0;
  const TargetInput target{"TS-NEG-SOG", 500.0, 0.0, kPi, -2.0, 500.0, 120.0, 0.95};

  const auto risk = evaluate_target(own, target, ColregsDuty::Free);

  EXPECT_NEAR(0.0, risk.closing_speed_mps, 1.0e-9);
}

TEST(RiskModelTest, ZeroRangeTargetReportsFullDomainViolationWithNegativeMargins) {
  const auto own = nominal_ownship();
  const TargetInput target{"TS-ZERO", 0.0, 0.0, 0.0, 0.0, 0.0, 30.0, 0.95};

  const auto risk = evaluate_target(own, target, ColregsDuty::GiveWay);

  EXPECT_EQ(1.0, risk.warning_ddv);
  EXPECT_EQ(1.0, risk.danger_ddv);
  EXPECT_LT(risk.warning_margin_m, 0.0);
  EXPECT_LT(risk.danger_margin_m, 0.0);
}

TEST(RiskModelTest, NegativeWarningScaleFallsBackToDangerAxesAndFiniteEvaluation) {
  const auto own = nominal_ownship();
  DomainConfig config;
  config.warning_scale = -2.0;
  config.superellipse_power = -3.0;
  const TargetInput target{"TS-BAD-CONFIG", 280.0, 0.0, kPi, 5.0, 120.0, 45.0, 0.9};

  const auto danger = danger_axes(own);
  const auto warning = warning_axes(own, config);
  const auto risk = evaluate_target(own, target, ColregsDuty::GiveWay, config);

  EXPECT_GE(warning.forward_m, danger.forward_m);
  EXPECT_GE(warning.astern_m, danger.astern_m);
  EXPECT_GE(warning.starboard_m, danger.starboard_m);
  EXPECT_GE(warning.port_m, danger.port_m);
  EXPECT_TRUE(std::isfinite(risk.warning_margin_m));
  EXPECT_TRUE(std::isfinite(risk.danger_margin_m));
  EXPECT_TRUE(std::isfinite(risk.risk_score));
}

TEST(RiskModelTest, NegativeTcpaInsideDomainIsDangerNotCritical) {
  const auto own = nominal_ownship();
  const TargetInput target{"TS-NEG-TCPA", 280.0, 0.0, kPi, 5.0, 120.0, -10.0, 0.9};

  const auto risk = evaluate_target(own, target, ColregsDuty::GiveWay);

  EXPECT_EQ(RiskPhase::Danger, risk.risk_phase);
}

TEST(RiskModelTest, RiskScoreRisesWhenDutyChangesFromStandOnHoldToGiveWay) {
  const auto own = nominal_ownship();
  const TargetInput target{"TS-DUTY", 500.0, 50.0, kPi, 4.0, 220.0, 180.0, 0.85};

  const auto stand_on = evaluate_target(own, target, ColregsDuty::StandOnHold);
  const auto give_way = evaluate_target(own, target, ColregsDuty::GiveWay);

  EXPECT_GT(give_way.risk_score, stand_on.risk_score);
}

TEST(RiskModelTest, ToStringReturnsStableNames) {
  EXPECT_STREQ("Clear", to_string(RiskPhase::Clear));
  EXPECT_STREQ("Monitor", to_string(RiskPhase::Monitor));
  EXPECT_STREQ("Warning", to_string(RiskPhase::Warning));
  EXPECT_STREQ("Danger", to_string(RiskPhase::Danger));
  EXPECT_STREQ("Critical", to_string(RiskPhase::Critical));

  EXPECT_STREQ("Free", to_string(ColregsDuty::Free));
  EXPECT_STREQ("StandOnHold", to_string(ColregsDuty::StandOnHold));
  EXPECT_STREQ("GiveWay", to_string(ColregsDuty::GiveWay));
  EXPECT_STREQ("BothGiveWay", to_string(ColregsDuty::BothGiveWay));
  EXPECT_STREQ("Rule17Action", to_string(ColregsDuty::Rule17Action));
}

}  // namespace
}  // namespace mass_l3::risk
