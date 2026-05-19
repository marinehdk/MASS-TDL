#pragma once

#include <cstdint>
#include <string>

#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/**
 * @brief Emits structured ASDR decision records via spdlog.
 *
 * In Phase E1, records are emitted as JSON-like spdlog::info lines tagged
 * [M4][ASDR]. The ASDR infrastructure scrapes these from the process log.
 * Full ASDR publisher integration is Phase E2+.
 */
class AsdrLogger {
 public:
  AsdrLogger() = default;

  /**
   * @brief Emit a behavior-change decision record.
   * @param prev Primary behavior from the previous cycle.
   * @param curr Primary behavior for the current cycle.
   * @param odd_zone Current ODD zone value (0=A…3=D).
   * @param rationale Rationale string from ArbitrationResult.
   * @post Emits one spdlog::info line iff prev != curr.
   */
  void log_behavior_change(BehaviorType prev, BehaviorType curr,
                           uint8_t odd_zone,
                           const std::string& rationale) const;

  /**
   * @brief Emit a record when IvP solve fails (nullopt returned by solver).
   * @param active_count Number of active behaviors at time of failure.
   * @param odd_zone Current ODD zone value.
   * @post Always emits one spdlog::info line.
   */
  void log_ivp_failure(size_t active_count, uint8_t odd_zone) const;
};

}  // namespace mass_l3::m4
