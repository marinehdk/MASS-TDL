// sil_pulse_adapter health logic — implementation of module_health declared in
// health.hpp. Mirrors the prior sil_topic_bridge.py _module_health: GREEN if the
// module's last-seen monotonic time is within the timeout window of now, else
// RED. A never-seen module (last_seen_s < 0) is RED.
#include "sil_pulse_adapter/health.hpp"

namespace sil_pulse_adapter {

uint8_t module_health(double now_s, double last_seen_s, double timeout_s) {
  if (last_seen_s < 0.0) {
    return kHealthRed;
  }
  if (now_s - last_seen_s > timeout_s) {
    return kHealthRed;
  }
  return kHealthGreen;
}

}  // namespace sil_pulse_adapter
