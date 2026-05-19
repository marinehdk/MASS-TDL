#ifndef M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

// Forward declarations for ROS2 message types
// (In actual implementation, include l3_msgs headers)
namespace l3_msgs {
namespace msg {
struct WorldState {
  float fusion_confidence = 0.0f;
  float motion_prediction_rmse_m = 0.0f;
  float blind_zone_fraction = 0.0f;
};

struct COLREGsConstraint {
  bool processing_success = true;
  float confidence = 0.0f;
};

struct CommLinkStatus {
  float rtt_sec = 0.0f;
  float packet_loss_pct = 0.0f;
};

struct ODDState {
  static constexpr uint8_t HEALTH_FULL = 0;
  static constexpr uint8_t HEALTH_DEGRADED = 1;
  static constexpr uint8_t HEALTH_CRITICAL = 2;

  static constexpr uint8_t ZONE_A = 0;
  static constexpr uint8_t ZONE_B = 1;
  static constexpr uint8_t ZONE_C = 2;
  static constexpr uint8_t ZONE_D = 3;

  uint8_t health = HEALTH_FULL;
  uint8_t ood_zone = ZONE_C;
};
}  // namespace msg
}  // namespace l3_msgs

namespace mass_l3::m7::sotif {

/**
 * @class SotifAssumptionMonitor
 * @brief ISO 21448 SOTIF assumption violation monitor for L3 Tactical Layer
 *
 * Implements 6 key assumptions verification per detailed design §5.3:
 *   1. AIS/Radar consistency (fusion_confidence threshold)
 *   2. Motion predictability (prediction RMSE threshold)
 *   3. Perception coverage (blind zone fraction threshold)
 *   4. COLREGs solvability (consecutive failure counter)
 *   5. Communication link quality (RTT + packet loss)
 *   6. ODD boundary safety (health + zone validation)
 *
 * All check functions are pure const noexcept with no dynamic allocation.
 * Complexity: O(1) per check; Space: O(1) fixed-size arrays only.
 */
class SotifAssumptionMonitor {
public:
  /**
   * @struct AssumptionMonitorConfig
   * @brief Configuration parameters for assumption monitoring thresholds
   *
   * Values marked [TBD-HAZID-*] are subject to HAZID calibration.
   */
  struct AssumptionMonitorConfig {
    // Assumption 1: AIS/Radar consistency
    float fusion_confidence_threshold = 0.5f;      // [TBD-HAZID-SOTIF-001]
    float fusion_hold_duration_s = 30.0f;

    // Assumption 2: Motion predictability
    float motion_rmse_threshold_m = 50.0f;         // [TBD-HAZID-SOTIF-002]

    // Assumption 3: Perception coverage
    float blind_zone_fraction_max = 0.20f;         // [TBD-HAZID-SOTIF-003]

    // Assumption 4: COLREGs solvability
    uint32_t colregs_consecutive_fail_threshold = 3;  // [TBD-HAZID-SOTIF-004]

    // Assumption 5: Communication link
    float comm_rtt_threshold_s = 2.0f;             // [TBD-HAZID-SOTIF-005]
    float comm_packet_loss_threshold = 20.0f;

    // ODD boundary
    // (TBD: additional ODD-specific thresholds if needed)
  };

  /**
   * @struct AssumptionViolationSnapshot
   * @brief Current state of all 6 assumptions (boolean snapshot)
   *
   * Used for ASDR recording and SAT data export.
   */
  struct AssumptionViolationSnapshot {
    bool ais_radar_ok;           ///< Assumption 1: AIS/Radar consistency
    bool motion_predictable;     ///< Assumption 2: Motion predictability
    bool perception_adequate;    ///< Assumption 3: Perception coverage
    bool colregs_resolvable;     ///< Assumption 4: COLREGs solvability
    bool comm_link_ok;           ///< Assumption 5: Communication link
    bool ood_safe;               ///< Assumption 6: ODD boundary safety
  };

  /**
   * @brief Constructor with configuration
   *
   * @param cfg Configuration parameters (thresholds, durations)
   */
  explicit SotifAssumptionMonitor(const AssumptionMonitorConfig& cfg) noexcept;

  // ========================================================================
  // 6 Assumption Check Functions
  // ========================================================================

  /**
   * @brief Check Assumption 1: AIS/Radar fusion consistency
   *
   * Verifies that fusion_confidence (from M2 World Model) is above the
   * configured threshold, indicating reliable sensor fusion.
   *
   * @param world M2 WorldState message containing fusion_confidence
   * @return true if assumption holds (fusion_confidence >= threshold)
   * @return false if assumption violated (fusion_confidence < threshold)
   */
  [[nodiscard]] bool assume_ais_radar_consistency(
      const l3_msgs::msg::WorldState& world) const noexcept;

  /**
   * @brief Check Assumption 2: Target motion predictability
   *
   * Verifies that target motion prediction error (RMSE) remains below
   * the configured threshold, enabling reliable trajectory forecasting.
   *
   * @param world M2 WorldState containing motion_prediction_rmse_m
   * @return true if assumption holds (RMSE <= threshold)
   * @return false if assumption violated (RMSE > threshold)
   */
  [[nodiscard]] bool assume_motion_predictability(
      const l3_msgs::msg::WorldState& world) const noexcept;

  /**
   * @brief Check Assumption 3: Perception coverage adequacy
   *
   * Verifies that sensor blind zones do not exceed the configured fraction,
   * ensuring 360° detection capability is maintained.
   *
   * @param world M2 WorldState containing blind_zone_fraction
   * @return true if assumption holds (blind_zone_fraction <= threshold)
   * @return false if assumption violated (blind_zone_fraction > threshold)
   */
  [[nodiscard]] bool assume_perception_coverage(
      const l3_msgs::msg::WorldState& world) const noexcept;

  /**
   * @brief Check Assumption 4: COLREGs rule solvability
   *
   * Monitors M6 COLREGs Reasoner success rate. If processing fails
   * consecutively (>= threshold), COLREGs rules are considered unsolvable.
   *
   * Internal counter is updated on each call:
   *  - processing_success=true  → counter reset to 0
   *  - processing_success=false → counter incremented
   *
   * @param colregs M6 COLREGsConstraint with processing_success flag
   * @return true if solvable (counter < threshold)
   * @return false if unsolvable (counter >= threshold)
   */
  [[nodiscard]] bool assume_colregs_solvable(
      const l3_msgs::msg::COLREGsConstraint& colregs) const noexcept;

  /**
   * @brief Check Assumption 5: Communication link quality
   *
   * Verifies that ROC communication link latency (RTT) and packet loss
   * are within acceptable bounds for D3/D4 operation.
   *
   * @param comm Communication link status (RTT, packet loss percentage)
   * @return true if link OK (RTT <= threshold AND loss <= threshold)
   * @return false if degraded (RTT > threshold OR loss > threshold)
   */
  [[nodiscard]] bool assume_communication_link_ok(
      const l3_msgs::msg::CommLinkStatus& comm) const noexcept;

  /**
   * @brief Check Assumption 6: ODD boundary safety
   *
   * Verifies that the operational design domain (ODD) health status and
   * zone information indicate safe operation within configured limits.
   *
   * @param odd M1 ODDState with health status and zone classification
   * @return true if ODD boundary is safe (health != CRITICAL)
   * @return false if ODD boundary violated (health == CRITICAL)
   */
  [[nodiscard]] bool assume_ood_boundary_safe(
      const l3_msgs::msg::ODDState& odd) const noexcept;

  // ========================================================================
  // State Query Interface
  // ========================================================================

  /**
   * @brief Get snapshot of current assumption states
   *
   * Returns boolean state of all 6 assumptions for ASDR recording and
   * transparency display (SAT-1/2).
   *
   * Initial state (on construction): all true (no violation history)
   *
   * @return AssumptionViolationSnapshot with 6 boolean flags
   */
  [[nodiscard]] AssumptionViolationSnapshot get_current_state() const noexcept;

private:
  // Configuration (const after construction)
  AssumptionMonitorConfig config_;

  // Internal state variables (mutable for const method updates)
  mutable uint32_t colregs_fail_count_{0};

  // Assumption state tracking (updated by check functions)
  mutable bool assumption_ais_radar_ok_{true};
  mutable bool assumption_motion_ok_{true};
  mutable bool assumption_perception_ok_{true};
  mutable bool assumption_colregs_ok_{true};
  mutable bool assumption_comm_ok_{true};
  mutable bool assumption_odd_ok_{true};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
