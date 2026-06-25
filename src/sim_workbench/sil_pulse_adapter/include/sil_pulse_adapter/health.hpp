#ifndef SIL_PULSE_ADAPTER_HEALTH_HPP_
#define SIL_PULSE_ADAPTER_HEALTH_HPP_
// sil_pulse_adapter health logic — pure, unit-testable. No ROS node state.
//
// Mirrors the prior sil_topic_bridge.py pulse logic (_module_health): a module
// is GREEN if its last-seen monotonic time is within PULSE_TIMEOUT_S of now,
// else RED. The adapter subscribes the 8 M-output topics and records last-seen
// per module; a 1 Hz timer publishes a ModulePulse per module with its health.
//
// Health states (sil_msgs/ModulePulse.state): 1=GREEN 3=RED.
#include <cstdint>

namespace sil_pulse_adapter {

// sil_msgs/ModulePulse.state enum values.
constexpr uint8_t kHealthGreen = 1;
constexpr uint8_t kHealthRed = 3;

// sil_msgs/ModulePulse.module_id enum values (M1..M8).
constexpr uint8_t kM1 = 1;
constexpr uint8_t kM2 = 2;
constexpr uint8_t kM3 = 3;
constexpr uint8_t kM4 = 4;
constexpr uint8_t kM5 = 5;
constexpr uint8_t kM6 = 6;
constexpr uint8_t kM7 = 7;
constexpr uint8_t kM8 = 8;

// Pulse timeout: no message within this many seconds -> module declared RED.
constexpr double kPulseTimeoutS = 10.0;

// Decide a module's health from its last-seen monotonic time. Returns GREEN if
// the module is alive (seen within the timeout window), RED otherwise. A module
// that has never been seen (last_seen_s < 0) is RED.
uint8_t module_health(double now_s, double last_seen_s,
                      double timeout_s = kPulseTimeoutS);

}  // namespace sil_pulse_adapter

#endif  // SIL_PULSE_ADAPTER_HEALTH_HPP_
