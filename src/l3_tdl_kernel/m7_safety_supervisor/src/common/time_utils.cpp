#include "m7_safety_supervisor/common/time_utils.hpp"

#include <chrono>
#include <cstdint>

#include "builtin_interfaces/msg/time.hpp"

namespace mass_l3::m7::common {

std::int64_t to_ns(builtin_interfaces::msg::Time const& t) noexcept {
  return (static_cast<std::int64_t>(t.sec) * 1'000'000'000LL)
       + static_cast<std::int64_t>(t.nanosec);
}

bool is_stale(builtin_interfaces::msg::Time const& msg_stamp,
              std::chrono::steady_clock::time_point /*reference_now*/,
              std::chrono::milliseconds /*max_age*/) noexcept {
  // LIMITATION: ROS2 stamps use system (wall) clock; steady_clock is not
  // directly comparable. Full staleness detection (topic watchdog pattern)
  // is performed at the ROS2 node layer using rclcpp::Time comparisons.
  // This function only guards against uninitialized stamps (sec==0, nanosec==0),
  // which indicate a message published before the source node set its stamp.
  return (msg_stamp.sec == 0) && (msg_stamp.nanosec == 0U);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::chrono::seconds elapsed_seconds(
    std::chrono::steady_clock::time_point since,
    std::chrono::steady_clock::time_point now) noexcept {
  if (now <= since) {
    return std::chrono::seconds{0};
  }
  return std::chrono::duration_cast<std::chrono::seconds>(now - since);
}

}  // namespace mass_l3::m7::common
