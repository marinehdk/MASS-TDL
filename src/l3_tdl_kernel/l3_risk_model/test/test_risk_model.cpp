#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "l3_risk_model/risk_model.hpp"

namespace mass_l3::risk {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNumericTolerance = 1.0e-3;

#ifndef RISK_GOLDEN_FIXTURE_PATH
#define RISK_GOLDEN_FIXTURE_PATH "test/fixtures/risk_golden_cases.json"
#endif

OwnShipInput nominal_ownship() {
  return OwnShipInput{0.0, 0.0, 0.0, 5.0, 46.0, 0.95, false};
}

RiskVector ranked_target(
  std::string id,
  RiskPhase phase,
  double score,
  double tcpa_s,
  double range_m) {
  RiskVector risk;
  risk.target_id = std::move(id);
  risk.risk_phase = phase;
  risk.risk_score = score;
  risk.tcpa_s = tcpa_s;
  risk.range_m = range_m;
  return risk;
}

OwnShipInput ownship_from_json(const nlohmann::json & item) {
  return OwnShipInput{
    item.at("x_m").get<double>(),
    item.at("y_m").get<double>(),
    item.at("heading_rad").get<double>(),
    item.at("sog_mps").get<double>(),
    item.at("loa_m").get<double>(),
    item.at("confidence").get<double>(),
    item.at("odd_degraded").get<bool>()};
}

TargetInput target_from_json(const nlohmann::json & item) {
  return TargetInput{
    item.at("id").get<std::string>(),
    item.at("x_m").get<double>(),
    item.at("y_m").get<double>(),
    item.at("cog_rad").get<double>(),
    item.at("sog_mps").get<double>(),
    item.at("cpa_m").get<double>(),
    item.at("tcpa_s").get<double>(),
    item.at("confidence").get<double>()};
}

ColregsDuty duty_from_string(const std::string & value) {
  if (value == "Free") {
    return ColregsDuty::Free;
  }
  if (value == "StandOnHold") {
    return ColregsDuty::StandOnHold;
  }
  if (value == "GiveWay") {
    return ColregsDuty::GiveWay;
  }
  if (value == "BothGiveWay") {
    return ColregsDuty::BothGiveWay;
  }
  if (value == "Rule17Action") {
    return ColregsDuty::Rule17Action;
  }
  throw std::invalid_argument{"unknown ColregsDuty: " + value};
}

struct FixtureExpected {
  RiskPhase risk_phase;
  double warning_margin_m;
  double danger_margin_m;
  double warning_ddv;
  double danger_ddv;
  double risk_score;
};

FixtureExpected expected_for_case(const std::string & case_id) {
  if (case_id == "forward_danger_closing") {
    return FixtureExpected{RiskPhase::Critical, -425.600, -112.000, 0.603174603, 0.285714286, 0.668589085};
  }
  if (case_id == "starboard_warning_crossing") {
    return FixtureExpected{RiskPhase::Warning, -73.770624939, 209.487480431, 0.115749360, 0.000, 0.414458439};
  }
  if (case_id == "opening_clear") {
    return FixtureExpected{RiskPhase::Clear, 645.681649249, 778.000077674, 0.000, 0.000, 0.005};
  }
  throw std::invalid_argument{"unknown fixture case_id: " + case_id};
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

TEST(RiskModelTest, SelectPrimaryPrefersDangerPhaseOverHigherScoredWarning) {
  RankingState state;
  const std::vector<RiskVector> risks{
    ranked_target("warning", RiskPhase::Warning, 0.94, 30.0, 150.0),
    ranked_target("danger", RiskPhase::Danger, 0.88, 45.0, 300.0)};

  const auto selected = select_primary(risks, &state);

  EXPECT_EQ("danger", selected.target_id);
  EXPECT_EQ("danger", state.previous_primary_id);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimaryKeepsPreviousForFirstCloseScoreSample) {
  RankingState state;
  state.previous_primary_id = "previous";
  const std::vector<RiskVector> risks{
    ranked_target("previous", RiskPhase::Warning, 0.80, 60.0, 200.0),
    ranked_target("candidate", RiskPhase::Warning, 0.91, 30.0, 150.0)};

  const auto selected = select_primary(risks, &state);

  EXPECT_EQ("previous", selected.target_id);
  EXPECT_EQ("previous", state.previous_primary_id);
  EXPECT_EQ("candidate", state.candidate_primary_id);
  EXPECT_EQ(1U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimarySwitchesOnSecondConsecutiveCloseScoreSample) {
  RankingState state;
  state.previous_primary_id = "previous";
  state.candidate_primary_id = "candidate";
  state.candidate_count = 1U;
  const std::vector<RiskVector> risks{
    ranked_target("previous", RiskPhase::Warning, 0.80, 60.0, 200.0),
    ranked_target("candidate", RiskPhase::Warning, 0.91, 30.0, 150.0)};

  const auto selected = select_primary(risks, &state);

  EXPECT_EQ("candidate", selected.target_id);
  EXPECT_EQ("candidate", state.previous_primary_id);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimarySwitchesImmediatelyWhenScoreGapMeetsThreshold) {
  RankingState state;
  state.previous_primary_id = "previous";
  state.candidate_primary_id = "stale";
  state.candidate_count = 1U;
  const std::vector<RiskVector> risks{
    ranked_target("previous", RiskPhase::Warning, 0.80, 60.0, 200.0),
    ranked_target("candidate", RiskPhase::Warning, 0.93, 30.0, 150.0)};

  const auto selected = select_primary(risks, &state);

  EXPECT_EQ("candidate", selected.target_id);
  EXPECT_EQ("candidate", state.previous_primary_id);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimarySwitchesImmediatelyWhenScoreGapEqualsThreshold) {
  RankingState state;
  state.previous_primary_id = "previous";
  state.candidate_primary_id = "stale";
  state.candidate_count = 1U;
  RankingConfig config;
  config.switch_score_gap = 0.12;
  const std::vector<RiskVector> risks{
    ranked_target("previous", RiskPhase::Warning, 0.80, 60.0, 200.0),
    ranked_target("candidate", RiskPhase::Warning, 0.92, 30.0, 150.0)};

  const auto selected = select_primary(risks, &state, config);

  EXPECT_EQ("candidate", selected.target_id);
  EXPECT_EQ("candidate", state.previous_primary_id);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimaryBreaksTiesByNonNegativeTcpaThenRange) {
  RankingState state;
  const std::vector<RiskVector> smaller_tcpa{
    ranked_target("later", RiskPhase::Warning, 0.70, 45.0, 100.0),
    ranked_target("sooner", RiskPhase::Warning, 0.70, 30.0, 300.0)};
  EXPECT_EQ("sooner", select_primary(smaller_tcpa, &state).target_id);

  state = RankingState{};
  const std::vector<RiskVector> only_non_negative{
    ranked_target("past", RiskPhase::Warning, 0.70, -1.0, 50.0),
    ranked_target("future", RiskPhase::Warning, 0.70, 60.0, 500.0)};
  EXPECT_EQ("future", select_primary(only_non_negative, &state).target_id);

  state = RankingState{};
  const std::vector<RiskVector> smaller_range{
    ranked_target("far", RiskPhase::Warning, 0.70, 30.0, 300.0),
    ranked_target("near", RiskPhase::Warning, 0.70, 30.0, 100.0)};
  EXPECT_EQ("near", select_primary(smaller_range, &state).target_id);
}

TEST(RiskModelTest, SelectPrimaryBreaksExactTiesByTargetIdRegardlessOfInputOrder) {
  const std::vector<RiskVector> ordered{
    ranked_target("alpha", RiskPhase::Warning, 0.70, 30.0, 100.0),
    ranked_target("beta", RiskPhase::Warning, 0.70, 30.0, 100.0)};
  const std::vector<RiskVector> reversed{
    ranked_target("beta", RiskPhase::Warning, 0.70, 30.0, 100.0),
    ranked_target("alpha", RiskPhase::Warning, 0.70, 30.0, 100.0)};

  EXPECT_EQ("alpha", select_primary(ordered, nullptr).target_id);
  EXPECT_EQ("alpha", select_primary(reversed, nullptr).target_id);
}

TEST(RiskModelTest, SelectPrimaryAllowsNullStateAndSelectsBest) {
  const std::vector<RiskVector> risks{
    ranked_target("warning", RiskPhase::Warning, 0.95, 30.0, 100.0),
    ranked_target("danger", RiskPhase::Danger, 0.60, 45.0, 200.0)};

  const auto selected = select_primary(risks, nullptr);

  EXPECT_EQ("danger", selected.target_id);
}

TEST(RiskModelTest, SelectPrimarySwitchesImmediatelyWhenPreviousPrimaryAbsent) {
  RankingState state;
  state.previous_primary_id = "missing";
  state.candidate_primary_id = "stale";
  state.candidate_count = 1U;
  const std::vector<RiskVector> risks{
    ranked_target("candidate", RiskPhase::Warning, 0.70, 30.0, 100.0)};

  const auto selected = select_primary(risks, &state);

  EXPECT_EQ("candidate", selected.target_id);
  EXPECT_EQ("candidate", state.previous_primary_id);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, SelectPrimaryReturnsDefaultRiskForEmptyInput) {
  RankingState state;
  state.previous_primary_id = "previous";
  state.candidate_primary_id = "candidate";
  state.candidate_count = 1U;

  const auto selected = select_primary({}, &state);

  EXPECT_TRUE(selected.target_id.empty());
  EXPECT_EQ(RiskPhase::Clear, selected.risk_phase);
  EXPECT_EQ(0.0, selected.risk_score);
  EXPECT_TRUE(state.candidate_primary_id.empty());
  EXPECT_EQ(0U, state.candidate_count);
}

TEST(RiskModelTest, GoldenFixtureCasesMatchStableRiskOutputs) {
  std::ifstream fixture(RISK_GOLDEN_FIXTURE_PATH);
  ASSERT_TRUE(fixture.good()) << RISK_GOLDEN_FIXTURE_PATH;

  const auto cases = nlohmann::json::parse(fixture);
  ASSERT_TRUE(cases.is_array());
  ASSERT_EQ(3U, cases.size());

  for (const auto & item : cases) {
    const auto case_id = item.at("case_id").get<std::string>();
    const auto expected = expected_for_case(case_id);
    const auto risk = evaluate_target(
      ownship_from_json(item.at("own")),
      target_from_json(item.at("target")),
      duty_from_string(item.at("duty").get<std::string>()));

    EXPECT_EQ(expected.risk_phase, risk.risk_phase) << case_id;
    EXPECT_NEAR(expected.warning_margin_m, risk.warning_margin_m, kNumericTolerance) << case_id;
    EXPECT_NEAR(expected.danger_margin_m, risk.danger_margin_m, kNumericTolerance) << case_id;
    EXPECT_NEAR(expected.warning_ddv, risk.warning_ddv, kNumericTolerance) << case_id;
    EXPECT_NEAR(expected.danger_ddv, risk.danger_ddv, kNumericTolerance) << case_id;
    EXPECT_NEAR(expected.risk_score, risk.risk_score, kNumericTolerance) << case_id;
  }
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
