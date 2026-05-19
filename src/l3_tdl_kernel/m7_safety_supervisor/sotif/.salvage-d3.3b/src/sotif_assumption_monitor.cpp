#include "m7_safety_supervisor/sotif_assumption_monitor.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::m7::sotif {

SotifAssumptionMonitor::SotifAssumptionMonitor(
    const AssumptionMonitorConfig& cfg) noexcept
    : config_(cfg),
      colregs_fail_count_(0),
      assumption_ais_radar_ok_(true),
      assumption_motion_ok_(true),
      assumption_perception_ok_(true),
      assumption_colregs_ok_(true),
      assumption_comm_ok_(true),
      assumption_odd_ok_(true) {}

// ============================================================================
// Assumption 1: AIS/Radar Consistency
// ============================================================================

bool SotifAssumptionMonitor::assume_ais_radar_consistency(
    const l3_msgs::msg::WorldState& world) const noexcept {
  // Check if fusion_confidence meets minimum threshold
  // Threshold indicates both AIS and radar are available and in reasonable agreement
  if (world.fusion_confidence < config_.fusion_confidence_threshold) {
    assumption_ais_radar_ok_ = false;
    return false;
  }
  assumption_ais_radar_ok_ = true;
  return true;
}

// ============================================================================
// Assumption 2: Motion Predictability
// ============================================================================

bool SotifAssumptionMonitor::assume_motion_predictability(
    const l3_msgs::msg::WorldState& world) const noexcept {
  // Check if target motion prediction error exceeds threshold
  // Higher RMSE indicates unpredictable behavior (e.g., target acceleration)
  if (world.motion_prediction_rmse_m > config_.motion_rmse_threshold_m) {
    assumption_motion_ok_ = false;
    return false;
  }
  assumption_motion_ok_ = true;
  return true;
}

// ============================================================================
// Assumption 3: Perception Coverage
// ============================================================================

bool SotifAssumptionMonitor::assume_perception_coverage(
    const l3_msgs::msg::WorldState& world) const noexcept {
  // Check if blind zone fraction exceeds maximum acceptable limit
  // Blind zones reduce 360° detection capability
  if (world.blind_zone_fraction > config_.blind_zone_fraction_max) {
    assumption_perception_ok_ = false;
    return false;
  }
  assumption_perception_ok_ = true;
  return true;
}

// ============================================================================
// Assumption 4: COLREGs Solvability
// ============================================================================

bool SotifAssumptionMonitor::assume_colregs_solvable(
    const l3_msgs::msg::COLREGsConstraint& colregs) const noexcept {
  // Track consecutive failures; reset on success
  if (colregs.processing_success) {
    colregs_fail_count_ = 0;
    assumption_colregs_ok_ = true;
    return true;
  }

  // Increment failure counter
  colregs_fail_count_++;

  // Check if threshold exceeded (consecutive failures trigger violation)
  if (colregs_fail_count_ >= config_.colregs_consecutive_fail_threshold) {
    assumption_colregs_ok_ = false;
    return false;
  }

  return true;
}

// ============================================================================
// Assumption 5: Communication Link Quality
// ============================================================================

bool SotifAssumptionMonitor::assume_communication_link_ok(
    const l3_msgs::msg::CommLinkStatus& comm) const noexcept {
  // Check both RTT and packet loss constraints
  // Either exceeding threshold indicates communication degradation

  if (comm.rtt_sec > config_.comm_rtt_threshold_s) {
    assumption_comm_ok_ = false;
    return false;
  }

  if (comm.packet_loss_pct > config_.comm_packet_loss_threshold) {
    assumption_comm_ok_ = false;
    return false;
  }

  assumption_comm_ok_ = true;
  return true;
}

// ============================================================================
// Assumption 6: ODD Boundary Safety
// ============================================================================

bool SotifAssumptionMonitor::assume_ood_boundary_safe(
    const l3_msgs::msg::ODDState& odd) const noexcept {
  // Check if ODD health is critical
  // CRITICAL health indicates operating outside safe boundaries
  if (odd.health == l3_msgs::msg::ODDState::HEALTH_CRITICAL) {
    assumption_odd_ok_ = false;
    return false;
  }

  assumption_odd_ok_ = true;
  return true;
}

// ============================================================================
// State Query Interface
// ============================================================================

SotifAssumptionMonitor::AssumptionViolationSnapshot
SotifAssumptionMonitor::get_current_state() const noexcept {
  // Return a snapshot of the current state of all 6 assumptions
  // This state is maintained across all check function calls
  AssumptionViolationSnapshot snapshot;
  snapshot.ais_radar_ok = assumption_ais_radar_ok_;
  snapshot.motion_predictable = assumption_motion_ok_;
  snapshot.perception_adequate = assumption_perception_ok_;
  snapshot.colregs_resolvable = assumption_colregs_ok_;
  snapshot.comm_link_ok = assumption_comm_ok_;
  snapshot.ood_safe = assumption_odd_ok_;

  return snapshot;
}

}  // namespace mass_l3::m7::sotif
