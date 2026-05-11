#include "m7_safety_supervisor/iec61508/diagnostic_coverage.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

namespace mass_l3::m7::iec61508 {

// ---------------------------------------------------------------------------
// update — one monitoring cycle
// A cycle is counted as "faulty" if any diagnostic check or watchdog raised a fault.
// ---------------------------------------------------------------------------

void DiagnosticCoverage::update(
    DiagnosticResult const& fault_result,
    WatchdogMonitor::WatchdogResult const& watchdog_result) noexcept
{
  ++metric_.total_checks;

  bool const kFaultDetected =
      !fault_result.conformance_score_valid ||
      !fault_result.cpa_internal_consistent ||
      !fault_result.colregs_target_id_match ||
      watchdog_result.any_critical;

  if (kFaultDetected) {
    ++metric_.detected_faults;
  }

  // Recompute ratio; guard against zero-division (total_checks always > 0 here)
  metric_.coverage_ratio =
      static_cast<float>(metric_.detected_faults) /
      static_cast<float>(metric_.total_checks);
}

// ---------------------------------------------------------------------------
// current
// ---------------------------------------------------------------------------

DiagnosticCoverageMetric DiagnosticCoverage::current() const noexcept {
  return metric_;
}

// ---------------------------------------------------------------------------
// is_sufficient — SIL 2 high DC target >= 0.90
// ---------------------------------------------------------------------------

bool DiagnosticCoverage::is_sufficient() const noexcept {
  return metric_.coverage_ratio >= 0.90F;
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

void DiagnosticCoverage::reset() noexcept {
  metric_ = DiagnosticCoverageMetric{};
}

}  // namespace mass_l3::m7::iec61508
