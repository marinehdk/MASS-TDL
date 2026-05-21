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

TEST(ColregsChainTest, HeadOnEvalPopulatesLayer3) {
  auto target = make_target(42, 2.0, 500.0, 180.0);
  auto eval = make_eval(42, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.t_act_s = 240.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_NE(result.layers[2].conclusion.find("HEAD-ON"), std::string::npos);
  EXPECT_EQ(result.target_id, "42");
}

TEST(ColregsChainTest, GiveWayRolePopulatesLayer4) {
  auto target = make_target(99, 5.0, 300.0, 120.0);
  auto eval = make_eval(99, 14, EncounterType::HEAD_ON, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  params.t_act_s = 240.0;
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_B, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_EQ(result.layers[3].conclusion, "GIVE-WAY");
  EXPECT_EQ(result.layers[0].conclusion, "ODD-B");
}

TEST(ColregsChainTest, MultiTargetSelectsMinCpa) {
  // Two targets: target 10 at CPA=200m, target 20 at CPA=100m.
  // Primary should be target 20 (min CPA).
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
  EXPECT_NE(result.layers[2].conclusion.find("HEAD-ON"), std::string::npos);
}

TEST(ColregsChainTest, EscalationFlagWhenIndependentAction) {
  auto target = make_target(1, 0.0, 100.0, 30.0);
  auto eval = make_eval(1, 17, EncounterType::CROSSING, Role::STAND_ON,
                        TimingPhase::INDEPENDENT_ACTION);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_C, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  EXPECT_TRUE(result.layers[4].escalation);
  EXPECT_NE(result.layers[4].conclusion.find("STAGE_3"), std::string::npos);
}

TEST(ColregsChainTest, AllLayersHaveConfidence) {
  auto target = make_target(7, 180.0, 400.0, 200.0);
  auto eval = make_eval(7, 13, EncounterType::OVERTAKING, Role::GIVE_WAY,
                        TimingPhase::PRESERVE_COURSE);
  std::vector<RuleEvaluation> evals = {eval};
  std::vector<TargetGeometricState> targets = {target};
  RuleParameters params{};
  auto result = ColregsReasonerNode::test_build_colregs_chain(
      evals, OddDomain::ODD_A, params, targets);
  ASSERT_EQ(result.layers.size(), 5u);
  for (const auto& layer : result.layers) {
    EXPECT_GE(layer.confidence, 0.0f);
    EXPECT_LE(layer.confidence, 1.0f);
  }
  EXPECT_FLOAT_EQ(result.layers[2].confidence, 0.85f);
}
