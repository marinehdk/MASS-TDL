#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <limits>

#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"
#include "m6_colregs_reasoner/types.hpp"

using namespace mass_l3::m6_colregs;

namespace {
TargetGeometricState make_target(uint64_t id, double bearing, double cpa_m, double tcpa_s) {
  TargetGeometricState t{};
  t.target_id = id;
  t.bearing_deg = bearing;
  t.aspect_deg = 180.0;
  t.cpa_m = cpa_m;
  t.tcpa_s = tcpa_s;
  t.relative_speed_kn = 10.0;
  t.ownship_heading_deg = 0.0;
  t.ownship_speed_kn = 10.0;
  return t;
}

RuleEvaluation make_eval(uint64_t target_id, int rule_id,
                          EncounterType enc, Role role, TimingPhase phase) {
  RuleEvaluation ev{};
  ev.is_active = true;
  ev.rule_id = rule_id;
  ev.target_id = target_id;
  ev.encounter_type = enc;
  ev.role = role;
  ev.phase = phase;
  ev.confidence = 0.85f;
  ev.rationale = "test rationale";
  return ev;
}
}  // anonymous namespace

TEST(ColregsChainTest, EmptyEvalsReturnsEmptyChain) {
  std::vector<RuleEvaluation> evals;
  std::vector<TargetGeometricState> targets;
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  EXPECT_TRUE(result.layers.empty());
  EXPECT_TRUE(result.target_id.empty());
}

TEST(ColregsChainTest, HeadOnRule14Identification) {
  auto target = make_target(42, 2.0, 500.0, 180.0);
  auto eval = make_eval(42, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);

  EXPECT_EQ(result.layers[0].label, "rule_identification");
  EXPECT_EQ(result.layers[0].conclusion, "Rule14");
  EXPECT_FLOAT_EQ(result.layers[0].confidence, 1.0f);
  EXPECT_NE(result.layers[0].rationale.find("Rule14"), std::string::npos);
  EXPECT_NE(result.layers[0].rationale.find("HEAD-ON"), std::string::npos);

  EXPECT_EQ(result.layers[1].label, "geometric_classification");
  EXPECT_EQ(result.layers[1].conclusion, "HEAD-ON");
  EXPECT_GE(result.layers[1].confidence, 0.5f);
  EXPECT_LE(result.layers[1].confidence, 1.0f);

  EXPECT_EQ(result.layers[2].label, "action_determination");
  EXPECT_EQ(result.layers[3].label, "priority_resolution");
  EXPECT_EQ(result.layers[4].label, "compliance_check");
}

TEST(ColregsChainTest, CrossingGiveWayRule15) {
  auto target = make_target(99, 60.0, 300.0, 120.0);
  auto eval = make_eval(99, 15, EncounterType::CROSSING, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);

  EXPECT_EQ(result.layers[0].conclusion, "Rule15");
  EXPECT_EQ(result.layers[2].conclusion, "give_way");
  EXPECT_NE(result.layers[2].rationale.find("Rule15"), std::string::npos);
  EXPECT_NE(result.layers[2].rationale.find("GIVE-WAY"), std::string::npos);
}

TEST(ColregsChainTest, OvertakingRule13) {
  auto target = make_target(7, 180.0, 400.0, 200.0);
  auto eval = make_eval(7, 13, EncounterType::OVERTAKING, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);

  EXPECT_EQ(result.layers[0].conclusion, "Rule13");
  EXPECT_EQ(result.layers[1].label, "geometric_classification");
  EXPECT_EQ(result.layers[1].conclusion, "OVERTAKING");
}

TEST(ColregsChainTest, GeometricClassificationHasRealValues) {
  auto target = make_target(42, 2.0, 500.0, 180.0);
  auto eval = make_eval(42, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  const auto& geo = result.layers[1];
  EXPECT_EQ(geo.label, "geometric_classification");
  ASSERT_EQ(geo.input_keys.size(), 4u);
  EXPECT_EQ(geo.input_keys[0], "relative_bearing_deg");
  EXPECT_EQ(geo.input_keys[1], "aspect_deg");
  EXPECT_EQ(geo.input_keys[2], "cpa_m");
  EXPECT_EQ(geo.input_keys[3], "tcpa_s");
  EXPECT_NE(geo.rationale.find("Relative bearing"), std::string::npos);
  EXPECT_NE(geo.rationale.find("CPA"), std::string::npos);
}

TEST(ColregsChainTest, PriorityResolutionSingleRule) {
  auto target = make_target(1, 0.0, 100.0, 30.0);
  auto eval = make_eval(1, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.layers[3].conclusion, "Single rule applicable");
  EXPECT_FLOAT_EQ(result.layers[3].confidence, 1.0f);
}

TEST(ColregsChainTest, ComplianceCheckGiveWayNonCompliant) {
  auto target = make_target(1, 0.0, 500.0, 30.0);
  auto eval = make_eval(1, 15, EncounterType::CROSSING, Role::GIVE_WAY,
                        TimingPhase::SOUND_WARNING);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.layers[4].conclusion, "Non-compliant");
  EXPECT_LT(result.layers[4].confidence, 1.0f);
  EXPECT_NE(result.layers[4].rationale.find("CPA"), std::string::npos);
}

TEST(ColregsChainTest, ComplianceCheckGiveWayCompliant) {
  auto target = make_target(1, 0.0, 2000.0, 30.0);
  auto eval = make_eval(1, 15, EncounterType::CROSSING, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.layers[4].conclusion, "Compliant");
  EXPECT_FLOAT_EQ(result.layers[4].confidence, 1.0f);
}

TEST(ColregsChainTest, ComplianceCheckStandOnIndependentAction) {
  auto target = make_target(1, 60.0, 500.0, 30.0);
  auto eval = make_eval(1, 17, EncounterType::CROSSING, Role::STAND_ON,
                        TimingPhase::INDEPENDENT_ACTION);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.layers[4].conclusion, "Non-compliant");
  EXPECT_FLOAT_EQ(result.layers[4].confidence, 0.5f);
  EXPECT_TRUE(result.layers[4].escalation);
}

TEST(ColregsChainTest, MultiTargetSelectsMinCpa) {
  auto t_near = make_target(20, 10.0, 100.0, 60.0);
  auto t_far  = make_target(10, 350.0, 200.0, 300.0);
  auto e_near = make_eval(20, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                          TimingPhase::PRESERVE_COURSE);
  auto e_far  = make_eval(10, 15, EncounterType::CROSSING, Role::STAND_ON,
                          TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {e_near, e_far};
  std::vector<TargetGeometricState> targets = {t_near, t_far};
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.target_id, "20");
  EXPECT_EQ(result.layers[0].conclusion, "Rule14");
}

TEST(ColregsChainTest, AllLayersHaveConfidence) {
  auto target = make_target(7, 180.0, 400.0, 200.0);
  auto eval = make_eval(7, 13, EncounterType::OVERTAKING, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.cpa_safe_m = 1852.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  for (const auto& layer : result.layers) {
    EXPECT_GE(layer.confidence, 0.0f);
    EXPECT_LE(layer.confidence, 1.0f);
  }
}
