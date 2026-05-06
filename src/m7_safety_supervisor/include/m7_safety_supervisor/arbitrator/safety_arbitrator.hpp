#ifndef M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_
#define M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"

namespace mass_l3::m7::arbitrator {

// AlertCandidate: zero-allocation alert entry using string_view for rationale.
// rationale MUST point to a static literal (lifetime: entire program).
struct AlertCandidate {
  uint8_t alert_type{0};
  uint8_t severity{0};
  mrm::MrmId recommended_mrm{mrm::MrmId::kNone};
  float confidence{0.0F};
  std::string_view rationale{};  // points to static literal; zero allocation
};

constexpr uint32_t kMaxAlertCandidates = 8u;

// SafetyArbitrator: collects alert candidates from all safety monitors,
// ranks by severity (highest first), and emits the highest-priority alert.
// Uses a fixed-size pool to avoid heap allocation on the 4 Hz main loop path.
//
// INVARIANT: Single-threaded (main_loop callback group only).
class SafetyArbitrator {
public:
  SafetyArbitrator() noexcept = default;

  // Collect candidates from all monitor results, sort by severity, and
  // return the highest-priority SafetyAlert ROS2 message.
  [[nodiscard]] l3_msgs::msg::SafetyAlert
  arbitrate(builtin_interfaces::msg::Time const& stamp,
            mrm::ScenarioContext const& ctx,
            mrm::MrmDecision const& mrm_decision,
            iec61508::DiagnosticResult const& diag,
            bool extreme_scenario_detected,
            std::chrono::steady_clock::time_point now) noexcept;

private:
  std::array<AlertCandidate, kMaxAlertCandidates> pool_{};
  uint32_t pool_size_{0};

  // Populate pool_ from monitor results; does not allocate.
  void collect_candidates(mrm::ScenarioContext const& ctx,
                          iec61508::DiagnosticResult const& diag,
                          bool extreme_scenario) noexcept;

  void collect_iec61508_candidates(mrm::ScenarioContext const& ctx,
                                   iec61508::DiagnosticResult const& diag) noexcept;
  void collect_sotif_candidates(mrm::ScenarioContext const& ctx,
                                 bool extreme_scenario) noexcept;
  void collect_perf_candidates(mrm::ScenarioContext const& ctx) noexcept;

  // Insertion sort by severity descending (higher severity = higher priority).
  // Avoids std::sort to prevent potential hidden allocation.
  void sort_candidates() noexcept;

  // Append a candidate to pool_ if pool is not full.
  void push_candidate(AlertCandidate const& candidate) noexcept;
};

}  // namespace mass_l3::m7::arbitrator

#endif  // M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_
