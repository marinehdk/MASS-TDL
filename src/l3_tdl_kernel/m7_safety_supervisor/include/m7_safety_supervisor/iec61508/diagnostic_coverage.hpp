#ifndef M7_SAFETY_SUPERVISOR_IEC61508_DIAGNOSTIC_COVERAGE_HPP_
#define M7_SAFETY_SUPERVISOR_IEC61508_DIAGNOSTIC_COVERAGE_HPP_

#include <cstdint>
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

namespace mass_l3::m7::iec61508 {

// NOTE: This is a RUNTIME FAULT-DETECTION RATE proxy, NOT the IEC 61508 §7.4.5.1
// Diagnostic Coverage (DC = λ_DD / (λ_DD + λ_DU)) figure.
// The IEC 61508 DC value is computed offline via FMEDA and submitted as a
// certification document. The coverage_ratio here tracks "fault-containing cycles /
// total cycles" for runtime health monitoring and ASDR record-keeping only.
struct DiagnosticCoverageMetric {
  std::uint64_t total_checks{0};    // total diagnostic cycles run
  std::uint64_t detected_faults{0}; // cycles in which at least one fault was found
  float coverage_ratio{0.0f};       // runtime fault-detection rate (not IEC 61508 DC)
};

class DiagnosticCoverage {
public:
  DiagnosticCoverage() noexcept = default;

  // Update coverage metric from the results of one monitoring cycle.
  // A cycle is "faulty" if any fault_result check failed OR any watchdog is critical.
  void update(DiagnosticResult const& fault_result,
              WatchdogMonitor::WatchdogResult const& watchdog_result) noexcept;

  [[nodiscard]] DiagnosticCoverageMetric current() const noexcept;

  // True if coverage_ratio >= 0.90 (SIL 2 high DC requirement)
  [[nodiscard]] bool is_sufficient() const noexcept;

  void reset() noexcept;

private:
  DiagnosticCoverageMetric metric_;
};

}  // namespace mass_l3::m7::iec61508

#endif  // M7_SAFETY_SUPERVISOR_IEC61508_DIAGNOSTIC_COVERAGE_HPP_
