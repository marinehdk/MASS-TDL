#include "m7_safety_supervisor/sotif/checker_veto_counter.hpp"

#include <algorithm>

namespace mass_l3::m7::sotif {

void CheckerVetoCounter::on_veto(
    l3_external_msgs::msg::CheckerVetoNotification const& msg) noexcept {
  if (msg.checker_layer != "L3") { return; }

  veto_occurred_this_cycle_ = true;

  auto const kClass = static_cast<std::size_t>(msg.veto_reason_class);
  if (kClass < reason_counts_.size()) {
    ++reason_counts_[kClass];
  }
}

void CheckerVetoCounter::on_cycle_tick() noexcept {
  window_.push(veto_occurred_this_cycle_);
  veto_occurred_this_cycle_ = false;
}

float CheckerVetoCounter::current_rate() const noexcept {
  return window_.rate();
}

std::uint16_t CheckerVetoCounter::window_violation_count() const noexcept {
  return window_.violation_count();
}

std::array<std::uint32_t, 6> CheckerVetoCounter::reason_counts() const noexcept {
  return reason_counts_;
}

void CheckerVetoCounter::reset() noexcept {
  window_.reset();
  reason_counts_.fill(0U);
  veto_occurred_this_cycle_ = false;
}

}  // namespace mass_l3::m7::sotif
