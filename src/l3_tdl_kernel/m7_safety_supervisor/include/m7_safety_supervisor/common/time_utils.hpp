#ifndef M7_SAFETY_SUPERVISOR_COMMON_TIME_UTILS_HPP_
#define M7_SAFETY_SUPERVISOR_COMMON_TIME_UTILS_HPP_

#include <chrono>
#include <cstdint>
#include "builtin_interfaces/msg/time.hpp"

namespace mass_l3::m7::common {

// Convert ROS2 builtin_interfaces::Time to nanoseconds (wall-clock composite value).
// NOTE: this value is NOT comparable to steady_clock::now().time_since_epoch() directly,
// as ROS2 stamps use system clock. Use only for zero-detection and relative comparisons
// within the same clock domain.
[[nodiscard]] std::int64_t to_ns(builtin_interfaces::msg::Time const& t) noexcept;

// Check if a ROS2 stamp is uninitialized (sec==0, nanosec==0).
// Full staleness detection (wall vs. steady clock mismatch) is handled at node level
// via watchdog timers. This function only guards against uninitialized stamps.
// [LIMITATION]: Cannot reliably compare ROS2 wall-clock stamps against steady_clock::now().
[[nodiscard]] bool is_stale(builtin_interfaces::msg::Time const& msg_stamp,
                             std::chrono::steady_clock::time_point reference_now,
                             std::chrono::milliseconds max_age) noexcept;

// Duration from `since` to `now`, clamped to [0, seconds::max].
// Returns 0 seconds if `now` is before `since` (clock monotonicity guard).
[[nodiscard]] std::chrono::seconds elapsed_seconds(
    std::chrono::steady_clock::time_point since,
    std::chrono::steady_clock::time_point now) noexcept;

}  // namespace mass_l3::m7::common

#endif  // M7_SAFETY_SUPERVISOR_COMMON_TIME_UTILS_HPP_
