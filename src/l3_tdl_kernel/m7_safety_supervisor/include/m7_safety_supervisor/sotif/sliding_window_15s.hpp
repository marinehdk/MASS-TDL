#ifndef M7_SAFETY_SUPERVISOR_SOTIF_SLIDING_WINDOW_15S_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_SLIDING_WINDOW_15S_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

namespace mass_l3::m7::sotif {

// kCapacity=100 @ M7 4Hz = 25s window (not 15s; filename is historical, RFC-003 LOCKED capacity unchanged).
// Stack-allocated — PATH-S compliant (no malloc).
class SlidingWindow15s {
public:
  static constexpr std::size_t kCapacity{100};

  SlidingWindow15s() noexcept = default;

  // Push a boolean sample (true = veto event occurred this cycle).
  void push(bool event) noexcept;

  // Current violation rate within the window [0.0, 1.0].
  [[nodiscard]] float rate() const noexcept;

  // Number of true samples in current window (for SotifMetrics.window_count).
  [[nodiscard]] std::uint16_t violation_count() const noexcept;

  // Clear all samples and reset counters.
  void reset() noexcept;

private:
  std::array<bool, kCapacity> buffer_{};
  std::size_t head_{0};
  std::size_t size_{0};
  std::uint16_t running_total_{0};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_SLIDING_WINDOW_15S_HPP_
