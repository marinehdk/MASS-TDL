#ifndef M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_
#define M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_

#include <cstdint>
#include <string_view>

#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"

namespace mass_l3::m7::arbitrator {

// Params struct for build_safety_alert — avoids adjacent same-type parameters.
struct SafetyAlertParams {
  uint8_t alert_type{0};
  uint8_t severity{0};
  std::string_view recommended_mrm{};
  float confidence{0.0F};
  std::string_view rationale{};
  std::string_view description{};
};

// Params struct for build_asdr_record — avoids adjacent same-type parameters.
struct AsdrRecordParams {
  std::string_view source_module{};
  std::string_view decision_type{};
  std::string_view decision_summary{};
};

// AlertGenerator: static factory for building ROS2 message instances.
// Note: ROS2 message types internally use std::string — that allocation is
// within the ROS2 framework boundary and outside M7's PROJ-LR-001 scope.
// All methods are noexcept; ROS2 message constructors may throw only if
// the heap is exhausted, which is a system-level failure beyond SIL scope.
class AlertGenerator {
public:
  // Build a SafetyAlert message with all mandatory fields populated.
  [[nodiscard]] static l3_msgs::msg::SafetyAlert
  build_safety_alert(builtin_interfaces::msg::Time const& stamp,
                     SafetyAlertParams const& params) noexcept;

  // Build an ASDRRecord message for decision audit trail.
  // decision_json is set to decision_summary (key=value text per spec).
  // SHA-256 digest of decision_json is written to signature (32 bytes).
  [[nodiscard]] static l3_msgs::msg::ASDRRecord
  build_asdr_record(builtin_interfaces::msg::Time const& stamp,
                    AsdrRecordParams const& params) noexcept;

  // Build a SATData message for M8 transparency pipeline.
  // sat1.state_summary and sat2.trigger_reason are set from arguments.
  // sat3 fields are left at default (no forecast in periodic health check).
  [[nodiscard]] static l3_msgs::msg::SATData
  build_sat_data(builtin_interfaces::msg::Time const& stamp,
                 std::string_view state_summary,
                 float system_confidence,
                 std::string_view trigger_reason) noexcept;
};

}  // namespace mass_l3::m7::arbitrator

#endif  // M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_
