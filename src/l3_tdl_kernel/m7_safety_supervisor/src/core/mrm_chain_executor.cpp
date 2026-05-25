#include "m7_safety_supervisor/core/mrm_chain_executor.hpp"
#include <string>

namespace mass_l3::m7::core {

using AlertType = std::uint8_t;
using Severity  = std::uint8_t;

namespace {
inline constexpr AlertType kIec61508Fault   = l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT;
inline constexpr AlertType kSotifAssumption = l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION;
inline constexpr AlertType kPerfDegraded    = l3_msgs::msg::SafetyAlert::ALERT_PERFORMANCE_DEGRADED;
inline constexpr Severity  kCritical        = l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL;
inline constexpr Severity  kMrcRequired     = l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED;
inline constexpr Severity  kInfo            = l3_msgs::msg::SafetyAlert::SEVERITY_INFO;
}  // namespace

l3_msgs::msg::SafetyAlert build_safety_alert_from_hard_constraints(
    CpaConsistencyResult const& cpa,
    ColregsGeometryResult const& colregs,
    SpeedLimitResult const& speed,
    RotLimitResult const& rot,
    WatchdogConstraintResult const& wd,
    DcConstraintResult const& dc) noexcept
{
  l3_msgs::msg::SafetyAlert alert{};
  alert.severity = kInfo;

  if (wd.multi_critical) {
    alert.alert_type         = kIec61508Fault;
    alert.severity           = kMrcRequired;
    alert.recommended_mrm    = "MRM-02";
    alert.confidence         = 1.0F;
    alert.description        = "HC-3: multi-module critical (" + std::to_string(wd.critical_count) + " modules)";
    return alert;
  }

  if (dc.violation) {
    alert.alert_type         = kIec61508Fault;
    alert.severity           = kMrcRequired;
    alert.recommended_mrm    = "MRM-01";
    alert.confidence         = dc.effective_dc_pct / 90.0F;
    alert.description        = "HC-4: DC=" + std::to_string(static_cast<int>(dc.effective_dc_pct)) + "% < 90%";
    return alert;
  }

  if (!cpa.consistent) {
    alert.alert_type         = kPerfDegraded;
    alert.severity           = kCritical;
    alert.recommended_mrm    = "MRM-03";
    alert.confidence         = 1.0F - (cpa.deviation_pct / 10.0F);
    alert.description        = "HC-1: CPA inconsistency (dev=" + std::to_string(static_cast<int>(cpa.deviation_pct)) + "%)";
    return alert;
  }

  if (!colregs.consistent) {
    alert.alert_type         = kSotifAssumption;
    alert.severity           = kCritical;
    alert.recommended_mrm    = "MRM-01";
    alert.confidence         = 0.95F;
    alert.description        = "HC-2: COLREGs rule-geometry mismatch";
    return alert;
  }

  if (!speed.compliant) {
    alert.alert_type         = kPerfDegraded;
    alert.severity           = kMrcRequired;
    alert.recommended_mrm    = "MRM-01";
    alert.confidence         = 1.0F - (speed.excess_pct / 100.0F);
    alert.description        = "HC-5: speed limit exceeded (" + std::to_string(static_cast<int>(speed.excess_pct)) + "%)";
    return alert;
  }

  if (!rot.compliant) {
    alert.alert_type         = kPerfDegraded;
    alert.severity           = kMrcRequired;
    alert.recommended_mrm    = "MRM-03";
    alert.confidence         = 1.0F - (rot.excess_pct / 100.0F);
    alert.description        = "HC-6: ROT limit exceeded (" + std::to_string(static_cast<int>(rot.excess_pct)) + "%)";
    return alert;
  }

  if (wd.any_critical && !wd.multi_critical) {
    alert.alert_type         = kIec61508Fault;
    alert.severity           = kMrcRequired;
    alert.recommended_mrm    = "MRM-01";
    alert.confidence         = 1.0F;
    alert.description        = "HC-3: single module critical (" + std::to_string(wd.critical_count) + " module)";
    return alert;
  }

  return alert;
}

}  // namespace mass_l3::m7::core
