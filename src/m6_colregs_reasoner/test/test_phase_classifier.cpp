#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"

namespace mass_l3::m6_colregs {

TEST(PhaseClassifierTest, PreserveCourseAboveTStandOn) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(500.0, params);
  EXPECT_EQ(phase, TimingPhase::PRESERVE_COURSE);
}

TEST(PhaseClassifierTest, SoundWarningBetweenTStandOnAndTAct) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(200.0, params);
  EXPECT_EQ(phase, TimingPhase::SOUND_WARNING);
}

TEST(PhaseClassifierTest, IndependentActionBetweenTActAndTEmergency) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(80.0, params);
  EXPECT_EQ(phase, TimingPhase::INDEPENDENT_ACTION);
}

TEST(PhaseClassifierTest, CriticalActionBelowTEmergency) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(30.0, params);
  EXPECT_EQ(phase, TimingPhase::CRITICAL_ACTION);
}

TEST(PhaseClassifierTest, BoundaryAtTStandOn) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  // Exactly at t_standOn_s: > not satisfied, so falls through
  auto phase = classifier.classify(300.0, params);
  EXPECT_EQ(phase, TimingPhase::SOUND_WARNING);
}

TEST(PhaseClassifierTest, BoundaryAtTAct) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(120.0, params);
  EXPECT_EQ(phase, TimingPhase::INDEPENDENT_ACTION);
}

TEST(PhaseClassifierTest, BoundaryAtTEmergency) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(60.0, params);
  EXPECT_EQ(phase, TimingPhase::CRITICAL_ACTION);
}

TEST(PhaseClassifierTest, ZeroTCPA) {
  PhaseClassifier classifier;
  RuleParameters params{};
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto phase = classifier.classify(0.0, params);
  EXPECT_EQ(phase, TimingPhase::CRITICAL_ACTION);
}

}  // namespace mass_l3::m6_colregs
