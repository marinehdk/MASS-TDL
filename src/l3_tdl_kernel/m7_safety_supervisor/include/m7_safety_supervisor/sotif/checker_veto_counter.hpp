#ifndef M7_SAFETY_SUPERVISOR_SOTIF_CHECKER_VETO_COUNTER_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_CHECKER_VETO_COUNTER_HPP_

#include <array>
#include <cstdint>

#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "m7_safety_supervisor/sotif/sliding_window_15s.hpp"

namespace mass_l3::m7::sotif {

class CheckerVetoCounter {
public:
  CheckerVetoCounter() noexcept = default;

  void on_veto(l3_external_msgs::msg::CheckerVetoNotification const& msg) noexcept;

  // MUST be called every cycle (4 Hz) to advance the sliding window.
  // Pushes true iff on_veto() was called since the last tick; false otherwise.
  void on_cycle_tick() noexcept;

  [[nodiscard]] float current_rate() const noexcept;
  [[nodiscard]] std::uint16_t window_violation_count() const noexcept;
  [[nodiscard]] std::array<std::uint32_t, 6> reason_counts() const noexcept;

  void reset() noexcept;

private:
  SlidingWindow15s window_;
  std::array<std::uint32_t, 6> reason_counts_{};
  bool veto_occurred_this_cycle_{false};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_CHECKER_VETO_COUNTER_HPP_
