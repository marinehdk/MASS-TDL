#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"

namespace mass_l3::m6_colregs {

TEST(ConstraintGeneratorTest, EmptyEvaluationsReturnsNoActiveRules) {
  ConstraintGenerator gen;
  RuleParameters params{};
  auto msg = gen.generate({}, params, 0.5);
  EXPECT_TRUE(msg.active_rules.empty());
  EXPECT_EQ(msg.phase, "PRESERVE_COURSE");
  EXPECT_EQ(msg.confidence, 0.5f);
}

TEST(ConstraintGeneratorTest, ActiveRuleIncluded) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval{};
  eval.is_active = true;
  eval.rule_id = 14;
  eval.phase = TimingPhase::PRESERVE_COURSE;
  eval.confidence = 0.85f;
  eval.preferred_direction = "STARBOARD";
  eval.min_alteration_deg = 15.0;
  eval.rationale = "Head-on detected";

  auto msg = gen.generate({eval}, params, 0.7);
  ASSERT_EQ(msg.active_rules.size(), 1u);
  EXPECT_EQ(msg.active_rules[0].rule_id, 14);
}

TEST(ConstraintGeneratorTest, InactiveRulesExcluded) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval{};
  eval.is_active = false;
  eval.rule_id = 6;
  eval.phase = TimingPhase::PRESERVE_COURSE;
  eval.preferred_direction = "HOLD";

  auto msg = gen.generate({eval}, params, 0.5);
  EXPECT_TRUE(msg.active_rules.empty());
}

TEST(ConstraintGeneratorTest, MostUrgentPhaseWins) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval1{};
  eval1.is_active = true;
  eval1.rule_id = 14;
  eval1.phase = TimingPhase::PRESERVE_COURSE;
  eval1.preferred_direction = "HOLD";

  RuleEvaluation eval2{};
  eval2.is_active = true;
  eval2.rule_id = 17;
  eval2.phase = TimingPhase::INDEPENDENT_ACTION;
  eval2.preferred_direction = "STARBOARD";
  eval2.min_alteration_deg = 15.0;

  auto msg = gen.generate({eval1, eval2}, params, 0.75);
  EXPECT_EQ(msg.phase, "INDEPENDENT_ACTION");
}

TEST(ConstraintGeneratorTest, CriticalActionDominatesPhase) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval1{};
  eval1.is_active = true;
  eval1.rule_id = 17;
  eval1.phase = TimingPhase::PRESERVE_COURSE;
  eval1.preferred_direction = "HOLD";

  RuleEvaluation eval2{};
  eval2.is_active = true;
  eval2.rule_id = 19;
  eval2.phase = TimingPhase::CRITICAL_ACTION;
  eval2.preferred_direction = "STARBOARD";

  auto msg = gen.generate({eval1, eval2}, params, 0.9);
  EXPECT_EQ(msg.phase, "CRITICAL_ACTION");
}

TEST(ConstraintGeneratorTest, ConstraintGeneratedForNonHoldDirection) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval{};
  eval.is_active = true;
  eval.rule_id = 14;
  eval.phase = TimingPhase::PRESERVE_COURSE;
  eval.preferred_direction = "STARBOARD";
  eval.min_alteration_deg = 15.0;
  eval.rationale = "Head-on — alter to starboard";

  auto msg = gen.generate({eval}, params, 0.8);
  ASSERT_EQ(msg.constraints.size(), 1u);
  EXPECT_EQ(msg.constraints[0].constraint_type, "colregs");
  EXPECT_DOUBLE_EQ(msg.constraints[0].numeric_value, 15.0);
}

TEST(ConstraintGeneratorTest, HOLDirectionNoConstraint) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval{};
  eval.is_active = true;
  eval.rule_id = 17;
  eval.phase = TimingPhase::PRESERVE_COURSE;
  eval.preferred_direction = "HOLD";
  eval.rationale = "Stand-on";

  auto msg = gen.generate({eval}, params, 0.7);
  EXPECT_TRUE(msg.constraints.empty());
}

TEST(ConstraintGeneratorTest, ConfidenceCapped) {
  ConstraintGenerator gen;
  RuleParameters params{};

  RuleEvaluation eval{};
  eval.is_active = true;
  eval.rule_id = 5;
  eval.phase = TimingPhase::PRESERVE_COURSE;
  eval.preferred_direction = "HOLD";

  auto msg = gen.generate({eval}, params, 1.5);
  EXPECT_LE(msg.confidence, 1.0f);
}

// ---------------------------------------------------------------------------
// Task 1.2: role/phase/direction serialisation + role-derive conflict_detected
// ---------------------------------------------------------------------------

namespace {

RuleEvaluation mk(const int id, const Role role, const TimingPhase ph,
                   const std::string& dir) {
  RuleEvaluation e;
  e.is_active = true;
  e.rule_id = id;
  e.role = role;
  e.phase = ph;
  e.preferred_direction = dir;
  e.min_alteration_deg = 20.0;
  e.confidence = 0.8f;
  return e;
}

}  // namespace

TEST(ConstraintGen, StandOnEarlyPhaseDoesNotRaiseConflict) {
  ConstraintGenerator g;
  RuleParameters p{};
  const auto msg = g.generate(
      {mk(17, Role::STAND_ON, TimingPhase::PRESERVE_COURSE, "HOLD")}, p, 0.9);
  EXPECT_EQ(msg.schema_version, 114U);
  EXPECT_FALSE(msg.conflict_detected);
  ASSERT_EQ(msg.active_rules.size(), 1u);
  EXPECT_EQ(msg.active_rules[0].role, static_cast<uint8_t>(Role::STAND_ON));
  EXPECT_EQ(msg.active_rules[0].preferred_direction, "HOLD");
  EXPECT_EQ(msg.primary_preferred_direction, "HOLD");
}

TEST(ConstraintGen, StandOnSoundWarningDoesNotRaiseConflict) {
  ConstraintGenerator g;
  RuleParameters p{};
  const auto msg = g.generate(
      {mk(17, Role::STAND_ON, TimingPhase::SOUND_WARNING, "HOLD")}, p, 0.9);
  EXPECT_FALSE(msg.conflict_detected);
  ASSERT_EQ(msg.active_rules.size(), 1u);
  EXPECT_EQ(msg.active_rules[0].role, static_cast<uint8_t>(Role::STAND_ON));
  EXPECT_EQ(msg.active_rules[0].rule_phase, "T_warn");
  EXPECT_EQ(msg.active_rules[0].preferred_direction, "HOLD");
  EXPECT_EQ(msg.primary_preferred_direction, "HOLD");
  EXPECT_TRUE(msg.constraints.empty());
}

TEST(ConstraintGen, StandOnInExtremisRaisesConflict) {
  ConstraintGenerator g;
  RuleParameters p{};
  const auto msg = g.generate(
      {mk(17, Role::STAND_ON, TimingPhase::INDEPENDENT_ACTION, "STARBOARD")},
      p, 0.9);
  EXPECT_TRUE(msg.conflict_detected);
}

TEST(ConstraintGen, GiveWayCrossingRaisesConflict) {
  ConstraintGenerator g;
  RuleParameters p{};
  const auto msg = g.generate(
      {mk(15, Role::GIVE_WAY, TimingPhase::PRESERVE_COURSE, "STARBOARD")}, p,
      0.9);
  EXPECT_TRUE(msg.conflict_detected);
  EXPECT_EQ(msg.primary_role, static_cast<uint8_t>(Role::GIVE_WAY));
}

}  // namespace mass_l3::m6_colregs
