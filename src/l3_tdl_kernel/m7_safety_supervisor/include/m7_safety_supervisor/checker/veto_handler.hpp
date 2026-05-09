#ifndef M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_
#define M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_

#include <array>
#include <cstdint>

#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "m7_safety_supervisor/checker/sliding_window_15s.hpp"

namespace mass_l3::m7::checker {

// RFC-003 enum classification (from l3_external_msgs/CheckerVetoNotification.msg constants)
// M7 classifies X-axis Checker vetoes by enum only — NEVER by veto_reason_detail free text.
// Any out-of-range veto_reason_class (>= 6) maps to kOther (RFC-003 constraint).
enum class VetoReasonClass : std::uint8_t {
  kColregsViolation    = 0,   // VETO_REASON_COLREGS_VIOLATION
  kCpaBelowThreshold   = 1,   // VETO_REASON_CPA_BELOW_THRESHOLD
  kEncConstraint       = 2,   // VETO_REASON_ENC_CONSTRAINT
  kActuatorLimit       = 3,   // VETO_REASON_ACTUATOR_LIMIT
  kTimeout             = 4,   // VETO_REASON_TIMEOUT
  kOther               = 5,   // VETO_REASON_OTHER
  kCount
};

// Classification histogram (for ASDR transparency).
// Index corresponds to static_cast<std::size_t>(VetoReasonClass).
using VetoHistogram = std::array<std::uint32_t, static_cast<std::size_t>(VetoReasonClass::kCount)>;

// VetoHandler: monitors X-axis Checker veto notifications and maintains a
// 100-cycle sliding window veto rate (RFC-003 LOCKED) plus a per-class
// histogram for ASDR transparency.
//
// Threading model:
//   on_veto_received() — called from events callback group
//   on_cycle_tick() / current_rate() / histogram() / reset() — called from main_loop callback group
//   The two callback groups are MutuallyExclusive within M7; callers are
//   responsible for ensuring no concurrent access between the two groups.
class VetoHandler {
public:
  // window_cycle_count is accepted for API compatibility but silently ignored —
  // SlidingWindow15s always uses kCapacity=100 (RFC-003 LOCKED).
  explicit VetoHandler(std::uint32_t window_cycle_count = 100) noexcept;

  // Called from events callback group: classify by veto_reason_class ONLY.
  // MUST NOT access veto_reason_detail in any way (RFC-003 constraint).
  // Out-of-range veto_reason_class values (>= kCount) map to kOther.
  void on_veto_received(l3_external_msgs::msg::CheckerVetoNotification const& msg) noexcept;

  // Called from main_loop callback group: advance window cursor by one cycle.
  // veto_in_this_cycle = true if at least one veto was received since last tick.
  void on_cycle_tick(bool veto_in_this_cycle) noexcept;

  // Current veto rate in the 100-cycle sliding window ∈ [0.0, 1.0].
  // RFC-003: rate >= 0.20 triggers MRC escalation.
  [[nodiscard]] double current_rate() const noexcept;

  // Current classification histogram (returns a copy).
  // Mutating the returned array does not affect internal state.
  [[nodiscard]] VetoHistogram histogram() const noexcept { return histogram_; }

  // Reset window and histogram (called after MRC deactivation).
  void reset() noexcept;

private:
  SlidingWindow15s window_;
  VetoHistogram histogram_{};
};

}  // namespace mass_l3::m7::checker

#endif  // M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_
