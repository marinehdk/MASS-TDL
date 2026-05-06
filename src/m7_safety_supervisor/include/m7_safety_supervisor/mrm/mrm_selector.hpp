#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_

#include <chrono>
#include <string_view>
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_01_drift.hpp"
#include "m7_safety_supervisor/mrm/mrm_02_anchor.hpp"
#include "m7_safety_supervisor/mrm/mrm_03_heave_to.hpp"
#include "m7_safety_supervisor/mrm/mrm_04_mooring.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

struct ScenarioContext {
  sotif::AssumptionStatus assumption{};
  iec61508::WatchdogMonitor::WatchdogResult watchdog{};
  sotif::PerformanceStatus performance{};
};

struct MrmDecision {
  MrmId mrm_id{MrmId::kNone};
  float confidence{0.0F};
  std::string_view rationale{};  // points to static literal from to_string(mrm_id); zero allocation
};

// MrmSelector: selects MRM from ScenarioContext using an 8-entry priority table.
// Applies a 30-second anti-oscillation lockout to prevent rapid MRM switching.
// INVARIANT: Single-threaded (main_loop callback group only).
class MrmSelector {
public:
  struct Config {
    std::chrono::seconds change_lockout{30};
    float upgrade_confidence_threshold{0.9F};
  };

  MrmSelector(Config const& cfg,
              MrmCommandSet const& cmd_set,
              l3_msgs::msg::ODDState const& odd_snapshot) noexcept;

  [[nodiscard]] MrmDecision select(ScenarioContext const& ctx,
                                   l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world,
                                   std::chrono::steady_clock::time_point now) noexcept;

  void reset() noexcept;

private:
  [[nodiscard]] MrmId raw_select(ScenarioContext const& ctx,
                                  l3_msgs::msg::ODDState const& odd) const noexcept;

  Config cfg_;
  Mrm01Drift mrm01_;
  Mrm02Anchor mrm02_;
  Mrm03HeaveTo mrm03_;
  Mrm04Mooring mrm04_;
  MrmId last_id_{MrmId::kNone};
  std::chrono::steady_clock::time_point last_change_{};
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_
