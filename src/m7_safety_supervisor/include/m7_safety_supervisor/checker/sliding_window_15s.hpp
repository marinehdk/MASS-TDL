#ifndef M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_
#define M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_

#include <array>
#include <cstdint>

namespace mass_l3::m7::checker {

// Fixed 100-cycle sliding window (RFC-003 LOCKED — do not change kCapacity).
// At M7's main_loop rate (~6.7 Hz effective check rate), 100 cycles ≈ 15 seconds.
// Implementation: fixed-size circular buffer with O(1) push/count.
// No dynamic allocation; no exceptions.
//
// INVARIANT: Single-threaded. tick(), rate(), count(), is_filled(), and reset()
// must all be invoked from a single MutuallyExclusive callback group (M7's
// main_loop). This class is NOT thread-safe; it deliberately uses no atomics
// or mutexes to keep the Checker path allocation- and lock-free.
class SlidingWindow15s {
public:
  static constexpr std::uint32_t kCapacity = 100;  // RFC-003 LOCKED

  SlidingWindow15s() noexcept = default;

  // Push a new sample into the window. Overwrites the oldest sample.
  // event = true: this cycle had an event (e.g., Checker veto received)
  // event = false: this cycle had no event
  void tick(bool event) noexcept;

  // Fraction of the fixed 100-slot window that recorded an event ∈ [0.0, 1.0].
  // Denominator is always kCapacity, so during the startup period (< kCapacity
  // ticks) the rate is biased downward. Callers that must distinguish "not yet
  // measurable" from "low rate" MUST gate on is_filled() separately.
  // NOTE: RFC-003 ≥ 0.20 threshold check uses this raw rate directly.
  [[nodiscard]] double rate() const noexcept;

  // Raw event count in the current window
  [[nodiscard]] std::uint32_t count() const noexcept { return event_count_; }

  // True after kCapacity ticks since construction or last reset() (window fully populated).
  // reset() clears this flag, restarting the startup period.
  [[nodiscard]] bool is_filled() const noexcept { return filled_; }

  // Reset window to empty state (e.g., after MRC activation)
  void reset() noexcept;

private:
  std::array<bool, kCapacity> buffer_{};
  std::uint32_t cursor_{0};         // write position (0-indexed, wraps at kCapacity)
  std::uint32_t event_count_{0};    // maintained O(1): +1 on new event, -1 on evicted event
  bool filled_{false};              // set after first full cycle (cursor wraps)
};

// ---------------------------------------------------------------------------
// Inline method definitions
// ---------------------------------------------------------------------------

inline void SlidingWindow15s::tick(bool event) noexcept
{
  bool evicted = buffer_[cursor_];
  if (evicted) { --event_count_; }
  buffer_[cursor_] = event;
  if (event) { ++event_count_; }
  cursor_ = (cursor_ + 1u) % kCapacity;
  if (cursor_ == 0u) { filled_ = true; }
}

inline double SlidingWindow15s::rate() const noexcept
{
  if (event_count_ == 0u) { return 0.0; }
  return static_cast<double>(event_count_) / static_cast<double>(kCapacity);
}

inline void SlidingWindow15s::reset() noexcept
{
  buffer_.fill(false);
  cursor_ = 0u;
  event_count_ = 0u;
  filled_ = false;
}

}  // namespace mass_l3::m7::checker

#endif  // M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_
