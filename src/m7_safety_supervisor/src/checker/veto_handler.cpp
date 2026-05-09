#include "m7_safety_supervisor/checker/veto_handler.hpp"

#include <cstddef>
#include <cstdint>

#include "l3_external_msgs/msg/checker_veto_notification.hpp"

namespace mass_l3::m7::checker {

// ---------------------------------------------------------------------------
// Constructor
// window_cycle_count is accepted but silently ignored — RFC-003 LOCKED at 100.
// ---------------------------------------------------------------------------
VetoHandler::VetoHandler(std::uint32_t /*window_cycle_count*/) noexcept
{
}

// ---------------------------------------------------------------------------
// on_veto_received
// Classify by veto_reason_class enum only. MUST NOT touch veto_reason_detail.
// Out-of-range values (>= kCount) map to kOther (RFC-003 constraint).
// ---------------------------------------------------------------------------
void VetoHandler::on_veto_received(
  l3_external_msgs::msg::CheckerVetoNotification const& msg) noexcept
{
  static constexpr auto kCount = static_cast<std::uint8_t>(VetoReasonClass::kCount);
  std::uint8_t raw = msg.veto_reason_class;

  // Out-of-range → kOther
  if (raw >= kCount) {
    raw = static_cast<std::uint8_t>(VetoReasonClass::kOther);
  }

  ++histogram_[static_cast<std::size_t>(raw)];
}

// ---------------------------------------------------------------------------
// on_cycle_tick
// Advance the sliding window by one cycle.
// ---------------------------------------------------------------------------
void VetoHandler::on_cycle_tick(bool veto_in_this_cycle) noexcept
{
  window_.tick(veto_in_this_cycle);
}

// ---------------------------------------------------------------------------
// current_rate
// ---------------------------------------------------------------------------
double VetoHandler::current_rate() const noexcept
{
  return window_.rate();
}

// ---------------------------------------------------------------------------
// reset
// Clear both the sliding window and the histogram.
// ---------------------------------------------------------------------------
void VetoHandler::reset() noexcept
{
  window_.reset();
  histogram_.fill(0U);
}

}  // namespace mass_l3::m7::checker
