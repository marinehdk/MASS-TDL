#include <gtest/gtest.h>
#include <chrono>
#include <string>

#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/tracked_target.hpp"

using namespace mass_l3::m7::sotif;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static l3_msgs::msg::WorldState build_world(float confidence) {
  l3_msgs::msg::WorldState w;
  w.confidence = confidence;
  return w;
}

static l3_msgs::msg::WorldState build_world_with_targets(
    float confidence,
    std::size_t total,
    std::size_t unknown_count) {
  l3_msgs::msg::WorldState w;
  w.confidence = confidence;
  for (std::size_t i = 0u; i < total; ++i) {
    l3_msgs::msg::TrackedTarget t;
    if (i < unknown_count) {
      t.classification = "unknown";
      t.classification_confidence = 0.1f;
    } else {
      t.classification = "vessel";
      t.classification_confidence = 0.9f;
    }
    t.cpa_m = 500.0;
    w.targets.push_back(t);
  }
  return w;
}

static l3_msgs::msg::COLREGsConstraint build_colregs(float confidence) {
  l3_msgs::msg::COLREGsConstraint c;
  c.confidence = confidence;
  return c;
}

[[maybe_unused]] static l3_msgs::msg::ODDState build_odd() {
  l3_msgs::msg::ODDState o;
  o.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  o.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  o.confidence = 1.0f;
  return o;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class AssumptionMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    AssumptionConfig cfg;
    cfg.ais_radar_duration_threshold = std::chrono::seconds{30};
    cfg.motion_window = std::chrono::seconds{30};
    cfg.fusion_confidence_low = 0.5;
    cfg.max_blind_zone_fraction = 0.20;
    cfg.colregs_consecutive_failure_count = 3u;
    cfg.rtt_threshold_s = 2.0;
    cfg.packet_loss_pct_threshold = 20.0;
    cfg.checker_veto_rate_threshold = 0.20;
    monitor_ = std::make_unique<AssumptionMonitor>(cfg);
    t0_ = std::chrono::steady_clock::now();
  }

  AssumptionMonitor& monitor() { return *monitor_; }
  std::chrono::steady_clock::time_point t(int offset_seconds) const {
    return t0_ + std::chrono::seconds{offset_seconds};
  }

private:
  std::unique_ptr<AssumptionMonitor> monitor_;
  std::chrono::steady_clock::time_point t0_;
};

// ---------------------------------------------------------------------------
// Assumption 1: AIS/Radar consistency
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, AisRadar_HighConfidence_NoViolation) {
  // confidence >= threshold (0.5) — no violation regardless of time
  auto const world = build_world(0.8f);
  EXPECT_FALSE(monitor().check_ais_radar(world, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(world, t(60)));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_ShortDuration_NoViolation) {
  // confidence below threshold but duration < 30s
  auto const world = build_world(0.3f);
  EXPECT_FALSE(monitor().check_ais_radar(world, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(world, t(10)));
  EXPECT_FALSE(monitor().check_ais_radar(world, t(29)));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_LongDuration_Violation) {
  // confidence below threshold and duration >= 30s
  auto const world = build_world(0.3f);
  EXPECT_FALSE(monitor().check_ais_radar(world, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(world, t(15)));
  // At t=30 elapsed since first call at t(0): violation
  EXPECT_TRUE(monitor().check_ais_radar(world, t(30)));
}

TEST_F(AssumptionMonitorTest, AisRadar_RecoveryAfterViolation_StateReset) {
  // Trigger violation, then recover, then confirm state is reset
  auto const world_low = build_world(0.3f);
  auto const world_ok = build_world(0.8f);
  (void)monitor().check_ais_radar(world_low, t(0));
  EXPECT_TRUE(monitor().check_ais_radar(world_low, t(30)));
  // Recovery: good confidence resets tracking
  EXPECT_FALSE(monitor().check_ais_radar(world_ok, t(31)));
  // A new low-confidence period must accumulate again from scratch
  EXPECT_FALSE(monitor().check_ais_radar(world_low, t(32)));
  EXPECT_FALSE(monitor().check_ais_radar(world_low, t(50)));  // only 18s since t(32)
}

TEST_F(AssumptionMonitorTest, AisRadar_ExactThreshold_NoViolation) {
  // confidence == threshold (0.5): condition is `< threshold`, not `<=`
  auto const world = build_world(0.5f);
  EXPECT_FALSE(monitor().check_ais_radar(world, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(world, t(60)));
}

// ---------------------------------------------------------------------------
// Assumption 2: Motion predictability
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Motion_GoodConfidence_NoViolation) {
  // confidence > 0.4 proxy threshold — no violation
  auto const world = build_world(0.9f);
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(60)));
}

TEST_F(AssumptionMonitorTest, Motion_PoorConfidence_LongDuration_Violation) {
  // confidence < 0.4 sustained > 30s
  auto const world = build_world(0.2f);
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(15)));
  EXPECT_TRUE(monitor().check_motion_predictability(world, t(30)));
}

TEST_F(AssumptionMonitorTest, Motion_PoorConfidence_ShortDuration_NoViolation) {
  // confidence < 0.4 but duration < 30s
  auto const world = build_world(0.2f);
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(10)));
  EXPECT_FALSE(monitor().check_motion_predictability(world, t(29)));
}

// ---------------------------------------------------------------------------
// Assumption 3: Perception coverage
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Coverage_NoUnknownTargets_NoViolation) {
  auto const world = build_world_with_targets(0.9f, 5u, 0u);
  EXPECT_FALSE(monitor().check_perception_coverage(world));
}

TEST_F(AssumptionMonitorTest, Coverage_AllUnknownTargets_Violation) {
  auto const world = build_world_with_targets(0.5f, 5u, 5u);
  EXPECT_TRUE(monitor().check_perception_coverage(world));
}

TEST_F(AssumptionMonitorTest, Coverage_ExactThreshold_NoViolation) {
  // 1 unknown out of 5 = 20% = not strictly > 0.20 → no violation
  auto const world = build_world_with_targets(0.7f, 5u, 1u);
  EXPECT_FALSE(monitor().check_perception_coverage(world));
}

// ---------------------------------------------------------------------------
// Assumption 4: COLREGs solvability
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Colregs_TwoConsecutiveFailures_NoViolation) {
  // Threshold is 3 consecutive failures
  auto const bad = build_colregs(0.1f);
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
}

TEST_F(AssumptionMonitorTest, Colregs_ThreeConsecutiveFailures_Violation) {
  auto const bad = build_colregs(0.1f);
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_TRUE(monitor().check_colregs_solvability(bad));
}

TEST_F(AssumptionMonitorTest, Colregs_FailThenSuccess_CounterReset) {
  auto const bad = build_colregs(0.1f);
  auto const good = build_colregs(0.8f);
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  // Success resets counter
  EXPECT_FALSE(monitor().check_colregs_solvability(good));
  // Must fail 3 more times from zero
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_FALSE(monitor().check_colregs_solvability(bad));
  EXPECT_TRUE(monitor().check_colregs_solvability(bad));
}

// ---------------------------------------------------------------------------
// Assumption 5: Communication link
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Comm_GoodRttGoodLoss_NoViolation) {
  EXPECT_FALSE(monitor().check_comm_link(mass_l3::m7::sotif::CommLinkState{0.5, 5.0}));
}

TEST_F(AssumptionMonitorTest, Comm_HighRtt_Violation) {
  // rtt_s > 2.0 threshold
  EXPECT_TRUE(monitor().check_comm_link(mass_l3::m7::sotif::CommLinkState{2.1, 5.0}));
}

TEST_F(AssumptionMonitorTest, Comm_HighPacketLoss_Violation) {
  // packet_loss_pct > 20.0 threshold
  EXPECT_TRUE(monitor().check_comm_link(mass_l3::m7::sotif::CommLinkState{1.0, 25.0}));
}

// ---------------------------------------------------------------------------
// Assumption 6: Checker veto rate
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, CheckerVeto_Below20Pct_NoViolation) {
  EXPECT_FALSE(monitor().check_checker_veto_rate(0.19));
}

TEST_F(AssumptionMonitorTest, CheckerVeto_At20Pct_Violation) {
  // RFC-003: >= 0.20 is a violation
  EXPECT_TRUE(monitor().check_checker_veto_rate(0.20));
}

TEST_F(AssumptionMonitorTest, CheckerVeto_Above20Pct_Violation) {
  EXPECT_TRUE(monitor().check_checker_veto_rate(0.50));
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, ResetClearsAllState) {
  // Accumulate state in all time-based and counter-based assumptions
  auto const world_low = build_world(0.2f);
  auto const colregs_bad = build_colregs(0.1f);

  (void)monitor().check_ais_radar(world_low, t(0));
  (void)monitor().check_motion_predictability(world_low, t(0));
  (void)monitor().check_colregs_solvability(colregs_bad);
  (void)monitor().check_colregs_solvability(colregs_bad);

  monitor().reset();

  // After reset: no ongoing violation from prior state
  // AIS/Radar: low confidence but fresh start — short duration, no violation
  EXPECT_FALSE(monitor().check_ais_radar(world_low, t(100)));
  EXPECT_FALSE(monitor().check_ais_radar(world_low, t(115)));

  // COLREGs: counter reset — need 3 new failures
  EXPECT_FALSE(monitor().check_colregs_solvability(colregs_bad));
  EXPECT_FALSE(monitor().check_colregs_solvability(colregs_bad));
  EXPECT_TRUE(monitor().check_colregs_solvability(colregs_bad));
}
