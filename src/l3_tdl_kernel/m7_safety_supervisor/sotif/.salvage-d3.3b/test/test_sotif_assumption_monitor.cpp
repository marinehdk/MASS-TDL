#include <gtest/gtest.h>
#include <chrono>
#include <array>

#include "m7_safety_supervisor/sotif_assumption_monitor.hpp"

namespace mass_l3::m7::sotif::test {

using namespace mass_l3::m7::sotif;

// ============================================================================
// Test Fixture
// ============================================================================

class SotifAssumptionMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_.fusion_confidence_threshold = 0.5f;
    config_.fusion_hold_duration_s = 30.0f;
    config_.motion_rmse_threshold_m = 50.0f;
    config_.blind_zone_fraction_max = 0.20f;
    config_.colregs_consecutive_fail_threshold = 3;
    config_.comm_rtt_threshold_s = 2.0f;
    config_.comm_packet_loss_threshold = 20.0f;

    monitor_ = std::make_unique<SotifAssumptionMonitor>(config_);
  }

  SotifAssumptionMonitor::AssumptionMonitorConfig config_;
  std::unique_ptr<SotifAssumptionMonitor> monitor_;
};

// ============================================================================
// Test Group 1: AIS/Radar Consistency (假设 1)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, AISRadar_HighConfidence_Pass) {
  // Arrange: fusion_confidence = 0.8（高于阈值 0.5）
  l3_msgs::msg::WorldState world;
  world.fusion_confidence = 0.8f;

  // Act
  bool result = monitor_->assume_ais_radar_consistency(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass with high fusion confidence";
}

TEST_F(SotifAssumptionMonitorTest, AISRadar_BoundaryConfidence_Pass) {
  // Arrange: fusion_confidence = 0.5（边界值，应通过）
  l3_msgs::msg::WorldState world;
  world.fusion_confidence = 0.5f;

  // Act
  bool result = monitor_->assume_ais_radar_consistency(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass at boundary threshold 0.5";
}

TEST_F(SotifAssumptionMonitorTest, AISRadar_LowConfidence_Fail) {
  // Arrange: fusion_confidence = 0.49（低于阈值）
  l3_msgs::msg::WorldState world;
  world.fusion_confidence = 0.49f;

  // Act
  bool result = monitor_->assume_ais_radar_consistency(world);

  // Assert
  EXPECT_FALSE(result) << "Should fail with low fusion confidence";
}

// ============================================================================
// Test Group 2: Motion Predictability (假设 2)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, MotionPredictability_LowError_Pass) {
  // Arrange: motion_rmse = 30 m（低于阈值 50 m）
  l3_msgs::msg::WorldState world;
  world.motion_prediction_rmse_m = 30.0f;

  // Act
  bool result = monitor_->assume_motion_predictability(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass with low prediction error";
}

TEST_F(SotifAssumptionMonitorTest, MotionPredictability_BoundaryError_Pass) {
  // Arrange: motion_rmse = 50 m（边界值，应通过）
  l3_msgs::msg::WorldState world;
  world.motion_prediction_rmse_m = 50.0f;

  // Act
  bool result = monitor_->assume_motion_predictability(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass at boundary threshold 50 m";
}

TEST_F(SotifAssumptionMonitorTest, MotionPredictability_HighError_Fail) {
  // Arrange: motion_rmse = 51 m（超过阈值）
  l3_msgs::msg::WorldState world;
  world.motion_prediction_rmse_m = 51.0f;

  // Act
  bool result = monitor_->assume_motion_predictability(world);

  // Assert
  EXPECT_FALSE(result) << "Should fail with high prediction error";
}

// ============================================================================
// Test Group 3: Perception Coverage (假设 3)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, PerceptionCoverage_GoodCoverage_Pass) {
  // Arrange: blind_zone_fraction = 0.15（低于阈值 0.20）
  l3_msgs::msg::WorldState world;
  world.blind_zone_fraction = 0.15f;

  // Act
  bool result = monitor_->assume_perception_coverage(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass with good perception coverage";
}

TEST_F(SotifAssumptionMonitorTest, PerceptionCoverage_BoundaryBlindZone_Pass) {
  // Arrange: blind_zone_fraction = 0.20（边界值，应通过）
  l3_msgs::msg::WorldState world;
  world.blind_zone_fraction = 0.20f;

  // Act
  bool result = monitor_->assume_perception_coverage(world);

  // Assert
  EXPECT_TRUE(result) << "Should pass at boundary threshold 0.20";
}

TEST_F(SotifAssumptionMonitorTest, PerceptionCoverage_PoorCoverage_Fail) {
  // Arrange: blind_zone_fraction = 0.21（超过阈值）
  l3_msgs::msg::WorldState world;
  world.blind_zone_fraction = 0.21f;

  // Act
  bool result = monitor_->assume_perception_coverage(world);

  // Assert
  EXPECT_FALSE(result) << "Should fail with poor perception coverage";
}

// ============================================================================
// Test Group 4: COLREGs Solvability (假设 4)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, COLREGsSolvable_SuccessfulProcessing_Pass) {
  // Arrange: processing_success=true
  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = true;
  colregs.confidence = 0.9f;

  // Act
  bool result = monitor_->assume_colregs_solvable(colregs);

  // Assert
  EXPECT_TRUE(result) << "Should pass with successful processing";
}

TEST_F(SotifAssumptionMonitorTest, COLREGsSolvable_OneFailure_Pass) {
  // Arrange: processing_success=false, but only first failure
  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = false;
  colregs.confidence = 0.5f;

  // Act
  bool result = monitor_->assume_colregs_solvable(colregs);

  // Assert
  EXPECT_TRUE(result) << "Should pass with single failure";
}

TEST_F(SotifAssumptionMonitorTest, COLREGsSolvable_TwoConsecutiveFailures_Pass) {
  // Arrange: processing_success=false, second failure
  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = false;
  colregs.confidence = 0.5f;

  // Act - First failure
  bool result1 = monitor_->assume_colregs_solvable(colregs);
  EXPECT_TRUE(result1) << "First failure should still pass";

  // Second failure
  bool result2 = monitor_->assume_colregs_solvable(colregs);
  EXPECT_TRUE(result2) << "Two consecutive failures should still pass";
}

TEST_F(SotifAssumptionMonitorTest, COLREGsSolvable_ThreeConsecutiveFailures_Fail) {
  // Arrange: processing_success=false, third consecutive failure
  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = false;
  colregs.confidence = 0.5f;

  // Act - Call three times
  [[maybe_unused]] bool res1 = monitor_->assume_colregs_solvable(colregs);  // First failure
  [[maybe_unused]] bool res2 = monitor_->assume_colregs_solvable(colregs);  // Second failure
  bool result = monitor_->assume_colregs_solvable(colregs);  // Third failure

  // Assert
  EXPECT_FALSE(result) << "Should fail after 3 consecutive failures";
}

TEST_F(SotifAssumptionMonitorTest, COLREGsSolvable_FailureThenRecovery_Pass) {
  // Arrange
  l3_msgs::msg::COLREGsConstraint colregs_fail;
  colregs_fail.processing_success = false;
  colregs_fail.confidence = 0.5f;

  l3_msgs::msg::COLREGsConstraint colregs_pass;
  colregs_pass.processing_success = true;
  colregs_pass.confidence = 0.9f;

  // Act
  [[maybe_unused]] bool res1 = monitor_->assume_colregs_solvable(colregs_fail);  // Failure #1
  [[maybe_unused]] bool res2 = monitor_->assume_colregs_solvable(colregs_fail);  // Failure #2
  bool result = monitor_->assume_colregs_solvable(colregs_pass);  // Recovery

  // Assert
  EXPECT_TRUE(result) << "Should pass after recovery (counter reset)";
}

// ============================================================================
// Test Group 5: Communication Link (假设 5)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, CommLink_GoodQuality_Pass) {
  // Arrange: rtt=1.0 s, packet_loss=10%
  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 1.0f;
  comm.packet_loss_pct = 10.0f;

  // Act
  bool result = monitor_->assume_communication_link_ok(comm);

  // Assert
  EXPECT_TRUE(result) << "Should pass with good communication quality";
}

TEST_F(SotifAssumptionMonitorTest, CommLink_BoundaryRTT_Pass) {
  // Arrange: rtt=2.0 s（边界值，应通过）
  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 2.0f;
  comm.packet_loss_pct = 10.0f;

  // Act
  bool result = monitor_->assume_communication_link_ok(comm);

  // Assert
  EXPECT_TRUE(result) << "Should pass at boundary RTT threshold 2.0 s";
}

TEST_F(SotifAssumptionMonitorTest, CommLink_HighRTT_Fail) {
  // Arrange: rtt=2.1 s（超过阈值）
  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 2.1f;
  comm.packet_loss_pct = 10.0f;

  // Act
  bool result = monitor_->assume_communication_link_ok(comm);

  // Assert
  EXPECT_FALSE(result) << "Should fail with high RTT";
}

TEST_F(SotifAssumptionMonitorTest, CommLink_HighPacketLoss_Fail) {
  // Arrange: packet_loss=21%（超过阈值 20%）
  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 1.0f;
  comm.packet_loss_pct = 21.0f;

  // Act
  bool result = monitor_->assume_communication_link_ok(comm);

  // Assert
  EXPECT_FALSE(result) << "Should fail with high packet loss";
}

// ============================================================================
// Test Group 6: ODD Boundary Safety (假设 6)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, OODBoundary_FullHealth_Pass) {
  // Arrange: health=FULL, zone=C
  l3_msgs::msg::ODDState odd;
  odd.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  odd.ood_zone = l3_msgs::msg::ODDState::ZONE_C;

  // Act
  bool result = monitor_->assume_ood_boundary_safe(odd);

  // Assert
  EXPECT_TRUE(result) << "Should pass with FULL health";
}

TEST_F(SotifAssumptionMonitorTest, OODBoundary_DegradedHealth_Pass) {
  // Arrange: health=DEGRADED, zone=B
  l3_msgs::msg::ODDState odd;
  odd.health = l3_msgs::msg::ODDState::HEALTH_DEGRADED;
  odd.ood_zone = l3_msgs::msg::ODDState::ZONE_B;

  // Act
  bool result = monitor_->assume_ood_boundary_safe(odd);

  // Assert
  EXPECT_TRUE(result) << "Should pass with DEGRADED health in zone B";
}

TEST_F(SotifAssumptionMonitorTest, OODBoundary_CriticalHealth_Fail) {
  // Arrange: health=CRITICAL
  l3_msgs::msg::ODDState odd;
  odd.health = l3_msgs::msg::ODDState::HEALTH_CRITICAL;
  odd.ood_zone = l3_msgs::msg::ODDState::ZONE_D;

  // Act
  bool result = monitor_->assume_ood_boundary_safe(odd);

  // Assert
  EXPECT_FALSE(result) << "Should fail with CRITICAL health";
}

// ============================================================================
// Test Group 7: Aggregated State (聚合状态)
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, AggregatedState_AllPass_ReturnAllTrue) {
  // Arrange: All assumptions pass
  l3_msgs::msg::WorldState world;
  world.fusion_confidence = 0.8f;
  world.motion_prediction_rmse_m = 30.0f;
  world.blind_zone_fraction = 0.15f;

  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = true;

  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 1.0f;
  comm.packet_loss_pct = 10.0f;

  l3_msgs::msg::ODDState odd;
  odd.health = l3_msgs::msg::ODDState::HEALTH_FULL;

  // Act
  [[maybe_unused]] bool res1 = monitor_->assume_ais_radar_consistency(world);
  [[maybe_unused]] bool res2 = monitor_->assume_motion_predictability(world);
  [[maybe_unused]] bool res3 = monitor_->assume_perception_coverage(world);
  [[maybe_unused]] bool res4 = monitor_->assume_colregs_solvable(colregs);
  [[maybe_unused]] bool res5 = monitor_->assume_communication_link_ok(comm);
  [[maybe_unused]] bool res6 = monitor_->assume_ood_boundary_safe(odd);

  auto snapshot = monitor_->get_current_state();

  // Assert
  EXPECT_TRUE(snapshot.ais_radar_ok);
  EXPECT_TRUE(snapshot.motion_predictable);
  EXPECT_TRUE(snapshot.perception_adequate);
  EXPECT_TRUE(snapshot.colregs_resolvable);
  EXPECT_TRUE(snapshot.comm_link_ok);
  EXPECT_TRUE(snapshot.ood_safe);
}

TEST_F(SotifAssumptionMonitorTest, AggregatedState_OneViolation_ReturnPartialTrue) {
  // Arrange: Only AIS/Radar consistency fails
  l3_msgs::msg::WorldState world;
  world.fusion_confidence = 0.3f;  // Below threshold
  world.motion_prediction_rmse_m = 30.0f;
  world.blind_zone_fraction = 0.15f;

  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.processing_success = true;

  l3_msgs::msg::CommLinkStatus comm;
  comm.rtt_sec = 1.0f;
  comm.packet_loss_pct = 10.0f;

  l3_msgs::msg::ODDState odd;
  odd.health = l3_msgs::msg::ODDState::HEALTH_FULL;

  // Act
  [[maybe_unused]] bool res1 = monitor_->assume_ais_radar_consistency(world);
  [[maybe_unused]] bool res2 = monitor_->assume_motion_predictability(world);
  [[maybe_unused]] bool res3 = monitor_->assume_perception_coverage(world);
  [[maybe_unused]] bool res4 = monitor_->assume_colregs_solvable(colregs);
  [[maybe_unused]] bool res5 = monitor_->assume_communication_link_ok(comm);
  [[maybe_unused]] bool res6 = monitor_->assume_ood_boundary_safe(odd);

  auto snapshot = monitor_->get_current_state();

  // Assert
  EXPECT_FALSE(snapshot.ais_radar_ok);
  EXPECT_TRUE(snapshot.motion_predictable);
  EXPECT_TRUE(snapshot.perception_adequate);
  EXPECT_TRUE(snapshot.colregs_resolvable);
  EXPECT_TRUE(snapshot.comm_link_ok);
  EXPECT_TRUE(snapshot.ood_safe);
}

// ============================================================================
// Test Group 8: Initialization
// ============================================================================

TEST_F(SotifAssumptionMonitorTest, Initialization_AllAssumptionsPass) {
  // Arrange: Newly constructed monitor, no history data
  // Act
  auto snapshot = monitor_->get_current_state();

  // Assert: All should be true (no data = assumption still holds)
  EXPECT_TRUE(snapshot.ais_radar_ok);
  EXPECT_TRUE(snapshot.motion_predictable);
  EXPECT_TRUE(snapshot.perception_adequate);
  EXPECT_TRUE(snapshot.colregs_resolvable);
  EXPECT_TRUE(snapshot.comm_link_ok);
  EXPECT_TRUE(snapshot.ood_safe);
}

}  // namespace mass_l3::m7::sotif::test
