#pragma once

#include <chrono>
#include <cstdint>

#include <builtin_interfaces/msg/time.hpp>

namespace mass_l3::m2::detail {

inline builtin_interfaces::msg::Time to_msg_time() {
  using namespace std::chrono;
  auto sys_now = system_clock::now();
  auto sys_sec = duration_cast<seconds>(sys_now.time_since_epoch());
  auto sys_ns = duration_cast<nanoseconds>(sys_now.time_since_epoch()) - sys_sec;
  builtin_interfaces::msg::Time t;
  t.sec = static_cast<int32_t>(sys_sec.count());
  t.nanosec = static_cast<uint32_t>(sys_ns.count());
  return t;
}

}  // namespace mass_l3::m2::detail
