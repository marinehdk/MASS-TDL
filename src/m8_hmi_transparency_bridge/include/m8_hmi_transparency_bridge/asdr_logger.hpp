// include/m8_hmi_transparency_bridge/asdr_logger.hpp
#ifndef MASS_L3_M8_ASDR_LOGGER_HPP_
#define MASS_L3_M8_ASDR_LOGGER_HPP_

#include <string>
#include <string_view>

#include "l3_msgs/msg/asdr_record.hpp"
#include "m8_hmi_transparency_bridge/sha256.hpp"

namespace mass_l3::m8 {

/// ASDR event logger -- builds ASDRRecord messages with SHA-256 signature.
///
/// M8-specific source_module is always "M8_HMI_Transparency_Bridge".
/// RFC-004: decision_json is a JSON string; signature is SHA-256 of decision_json.
class AsdrLogger final {
 public:
  AsdrLogger() = default;

  /// Build an ASDRRecord for a decision event.
  [[nodiscard]] l3_msgs::msg::ASDRRecord build_record(
      const builtin_interfaces::msg::Time& stamp,
      std::string_view decision_type,
      std::string_view decision_json) const noexcept;

  /// Build ASDRRecord for ToR acknowledgment event.
  [[nodiscard]] l3_msgs::msg::ASDRRecord build_tor_acknowledgment_record(
      const builtin_interfaces::msg::Time& stamp,
      std::string_view operator_id,
      double sat1_display_duration_s,
      const std::string& odd_zone,
      float conformance_score) const noexcept;

  /// Build ASDRRecord for periodic UI state snapshot.
  [[nodiscard]] l3_msgs::msg::ASDRRecord build_ui_snapshot_record(
      const builtin_interfaces::msg::Time& stamp,
      std::string_view ui_state_summary) const noexcept;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_ASDR_LOGGER_HPP_
