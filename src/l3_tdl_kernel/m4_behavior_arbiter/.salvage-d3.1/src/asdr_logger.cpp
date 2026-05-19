#include "m4_behavior_arbiter/asdr_logger.hpp"

#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

void AsdrLogger::log_behavior_change(BehaviorType prev, BehaviorType curr,
                                      uint8_t odd_zone,
                                      const std::string& rationale) const {
  if (prev == curr) {
    return;
  }

  spdlog::info(
      "[M4][ASDR] {{\"event\":\"behavior_change\",\"from\":{},\"to\":{},"
      "\"odd_zone\":{},\"rationale\":\"{}\"}}",
      static_cast<int>(prev), static_cast<int>(curr),
      static_cast<int>(odd_zone), rationale);
}

void AsdrLogger::log_ivp_failure(size_t active_count,
                                 uint8_t odd_zone) const {
  spdlog::info(
      "[M4][ASDR] {{\"event\":\"ivp_failure\",\"active_count\":{},"
      "\"odd_zone\":{}}}",
      active_count, static_cast<int>(odd_zone));
}

}  // namespace mass_l3::m4
