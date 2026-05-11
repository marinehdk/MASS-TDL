#include <gtest/gtest.h>
#include <chrono>
#include <cstddef>

#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/tracked_target.hpp"

using namespace mass_l3::m7::sotif;
using namespace std::chrono_literals;

namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

l3_msgs::msg::WorldState build_world(float confidence) {
  l3_msgs::msg::WorldState w;
  w.confidence = confidence;
  return w;
}

struct WorldWithTargetsParams {
  float confidence{0.9F};
  std::size_t total{0U};
  std::size_t unknown_count{0U};
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
l3_msgs::msg::WorldState build_world_with_targets(WorldWithTargetsParams const& p) {
  l3_msgs::msg::WorldState w;
  w.confidence = p.confidence;
  for (std::size_t i = 0U; i < p.total; ++i) {
    l3_msgs::msg::TrackedTarget t;
    if (i < p.unknown_count) {
      t.classification = "unknown";
      t.classification_confidence = 0.1F;
    } else {
      t.classification = "vessel";
      t.classification_confidence = 0.9F;
    }
    t.cpa_m = 500.0;
    w.targets.push_back(t);
  }
  return w;
}

l3_msgs::msg::COLREGsConstraint build_colregs(float confidence) {
  l3_msgs::msg::COLREGsConstraint c;
  c.confidence = confidence;
  return c;
}

[[maybe_unused]] l3_msgs::msg::ODDState build_odd() {
  l3_msgs::msg::ODDState o;
  o.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  o.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  o.confidence = 1.0F;
  return o;
}

}  // namespace

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
    cfg.colregs_consecutive_failure_count = 3U;
    cfg.rtt_threshold_s = 2.0;
    cfg.packet_loss_pct_threshold = 20.0;
    cfg.checker_veto_rate_threshold = 0.20;
    monitor_ = std::make_unique<AssumptionMonitor>(cfg);
    t0_ = std::chrono::steady_clock::now();
  }

  AssumptionMonitor& monitor() { return *monitor_; }
  [[nodiscard]] std::chrono::steady_clock::time_point t(int offset_seconds) const {
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
  auto const kWorld = build_world(0.8F);
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(60)));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_ShortDuration_NoViolation) {
  // confidence below threshold but duration < 30s
  auto const kWorld = build_world(0.3F);
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(10)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(29)));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_LongDuration_Violation) {
  // confidence below threshold and duration >= 30s
  auto const kWorld = build_world(0.3F);
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(15)));
  // At t=30 elapsed since first call at t(0): violation
  EXPECT_TRUE(monitor().check_ais_radar(kWorld, t(30)));
}

TEST_F(AssumptionMonitorTest, AisRadar_RecoveryAfterViolation_StateReset) {
  // Trigger violation, then recover, then confirm state is reset
  auto const kWorldLow = build_world(0.3F);
  auto const kWorldOk = build_world(0.8F);
  (void)monitor().check_ais_radar(kWorldLow, t(0));
  EXPECT_TRUE(monitor().check_ais_radar(kWorldLow, t(30)));
  // Recovery: good confidence resets tracking
  EXPECT_FALSE(monitor().check_ais_radar(kWorldOk, t(31)));
  // A new low-confidence period must accumulate again from scratch
  EXPECT_FALSE(monitor().check_ais_radar(kWorldLow, t(32)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorldLow, t(50)));  // only 18s since t(32)
}

TEST_F(AssumptionMonitorTest, AisRadar_ExactThreshold_NoViolation) {
  // confidence == threshold (0.5): condition is `< threshold`, not `<=`
  auto const kWorld = build_world(0.5F);
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorld, t(60)));
}

// ---------------------------------------------------------------------------
// Assumption 2: Motion predictability
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Motion_GoodConfidence_NoViolation) {
  // confidence > 0.4 proxy threshold — no violation
  auto const kWorld = build_world(0.9F);
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(60)));
}

TEST_F(AssumptionMonitorTest, Motion_PoorConfidence_LongDuration_Violation) {
  // confidence < 0.4 sustained > 30s
  auto const kWorld = build_world(0.2F);
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(15)));
  EXPECT_TRUE(monitor().check_motion_predictability(kWorld, t(30)));
}

TEST_F(AssumptionMonitorTest, Motion_PoorConfidence_ShortDuration_NoViolation) {
  // confidence < 0.4 but duration < 30s
  auto const kWorld = build_world(0.2F);
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(0)));
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(10)));
  EXPECT_FALSE(monitor().check_motion_predictability(kWorld, t(29)));
}

// ---------------------------------------------------------------------------
// Assumption 3: Perception coverage
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Coverage_NoUnknownTargets_NoViolation) {
  auto const kWorld = build_world_with_targets({0.9F, 5U, 0U});
  EXPECT_FALSE(monitor().check_perception_coverage(kWorld));
}

TEST_F(AssumptionMonitorTest, Coverage_AllUnknownTargets_Violation) {
  auto const kWorld = build_world_with_targets({0.5F, 5U, 5U});
  EXPECT_TRUE(monitor().check_perception_coverage(kWorld));
}

TEST_F(AssumptionMonitorTest, Coverage_ExactThreshold_NoViolation) {
  // 1 unknown out of 5 = 20% = not strictly > 0.20 → no violation
  auto const kWorld = build_world_with_targets({0.7F, 5U, 1U});
  EXPECT_FALSE(monitor().check_perception_coverage(kWorld));
}

// ---------------------------------------------------------------------------
// Assumption 4: COLREGs solvability
// ---------------------------------------------------------------------------

TEST_F(AssumptionMonitorTest, Colregs_TwoConsecutiveFailures_NoViolation) {
  // Threshold is 3 consecutive failures
  auto const kBad = build_colregs(0.1F);
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
}

TEST_F(AssumptionMonitorTest, Colregs_ThreeConsecutiveFailures_Violation) {
  auto const kBad = build_colregs(0.1F);
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_TRUE(monitor().check_colregs_solvability(kBad));
}

TEST_F(AssumptionMonitorTest, Colregs_FailThenSuccess_CounterReset) {
  auto const kBad = build_colregs(0.1F);
  auto const kGood = build_colregs(0.8F);
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  // Success resets counter
  EXPECT_FALSE(monitor().check_colregs_solvability(kGood));
  // Must fail 3 more times from zero
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_FALSE(monitor().check_colregs_solvability(kBad));
  EXPECT_TRUE(monitor().check_colregs_solvability(kBad));
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
  auto const kWorldLow = build_world(0.2F);
  auto const kColregsBad = build_colregs(0.1F);

  (void)monitor().check_ais_radar(kWorldLow, t(0));
  (void)monitor().check_motion_predictability(kWorldLow, t(0));
  (void)monitor().check_colregs_solvability(kColregsBad);
  (void)monitor().check_colregs_solvability(kColregsBad);

  monitor().reset();

  // After reset: no ongoing violation from prior state
  // AIS/Radar: low confidence but fresh start — short duration, no violation
  EXPECT_FALSE(monitor().check_ais_radar(kWorldLow, t(100)));
  EXPECT_FALSE(monitor().check_ais_radar(kWorldLow, t(115)));

  // COLREGs: counter reset — need 3 new failures
  EXPECT_FALSE(monitor().check_colregs_solvability(kColregsBad));
  EXPECT_FALSE(monitor().check_colregs_solvability(kColregsBad));
  EXPECT_TRUE(monitor().check_colregs_solvability(kColregsBad));
}
