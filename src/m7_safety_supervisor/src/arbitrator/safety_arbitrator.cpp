#include "m7_safety_supervisor/arbitrator/safety_arbitrator.hpp"

#include <cstdint>
#include <cstring>

#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"

namespace mass_l3::m7::arbitrator {

// ---------------------------------------------------------------------------
// push_candidate — append if pool not full (no-op when at capacity)
// ---------------------------------------------------------------------------

void SafetyArbitrator::push_candidate(AlertCandidate const& candidate) noexcept {
  if (pool_size_ < kMaxAlertCandidates) {
    pool_[pool_size_] = candidate;
    ++pool_size_;
  }
}

// ---------------------------------------------------------------------------
// collect_iec61508_candidates — IEC 61508 fault sub-section
// ---------------------------------------------------------------------------

void SafetyArbitrator::collect_iec61508_candidates(
    mrm::ScenarioContext const& ctx,
    iec61508::DiagnosticResult const& diag) noexcept
{
  // Diagnostic fault: any validity check failed in FaultMonitor
  bool const kAnyFault = !diag.conformance_score_valid
                      || !diag.cpa_internal_consistent
                      || !diag.colregs_target_id_match;
  if (kAnyFault) {
    AlertCandidate c{};
    c.alert_type  = l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT;
    c.severity    = l3_msgs::msg::SafetyAlert::SEVERITY_WARNING;
    c.recommended_mrm = mrm::MrmId::kMrm01_Drift;
    c.confidence  = 0.80F;
    c.rationale   = "IEC61508: diagnostic validity check failed";
    c.description = "Safety monitor: internal consistency check failed — verify system status";
    push_candidate(c);
  }

  // Watchdog: one or more monitored modules lost heartbeat beyond tolerance
  if (ctx.watchdog.any_critical) {
    AlertCandidate c{};
    c.alert_type = l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT;
    // Severity escalates with number of critical modules
    c.severity = (ctx.watchdog.critical_count >= 2U)
                   ? l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED
                   : l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL;
    c.recommended_mrm = mrm::MrmId::kMrm01_Drift;
    c.confidence  = 0.95F;
    c.rationale   = "IEC61508: watchdog heartbeat loss — module(s) critical";
    c.description = "CRITICAL: System module(s) unresponsive — manual intervention advised";
    push_candidate(c);
  }
}

// ---------------------------------------------------------------------------
// collect_sotif_candidates — SOTIF assumption sub-section
// ---------------------------------------------------------------------------

void SafetyArbitrator::collect_sotif_candidates(
    mrm::ScenarioContext const& ctx,
    bool extreme_scenario) noexcept
{
  if (extreme_scenario) {
    AlertCandidate c{};
    c.alert_type  = l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION;
    c.severity    = l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED;
    c.recommended_mrm = mrm::MrmId::kMrm01_Drift;
    c.confidence  = 0.95F;
    c.rationale   = "SOTIF: extreme scenario — >= 3 assumptions violated simultaneously";
    c.description = "EMERGENCY: Multiple safety assumptions violated — MRC required";
    push_candidate(c);
  } else if (ctx.assumption.total_violation_count >= 2U) {
    AlertCandidate c{};
    c.alert_type  = l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION;
    c.severity    = l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL;
    c.recommended_mrm = mrm::MrmId::kMrm01_Drift;
    c.confidence  = 0.85F;
    c.rationale   = "SOTIF: 2+ assumptions violated — critical degradation";
    c.description = "CRITICAL: Performance degraded — reduced operational capability";
    push_candidate(c);
  } else if (ctx.assumption.total_violation_count >= 1U) {
    AlertCandidate c{};
    c.alert_type  = l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION;
    c.severity    = l3_msgs::msg::SafetyAlert::SEVERITY_WARNING;
    c.recommended_mrm = mrm::MrmId::kNone;
    c.confidence  = 0.70F;
    c.rationale   = "SOTIF: single assumption violated — monitoring";
    c.description = "WARNING: Safety assumption violated — monitor and prepare";
    push_candidate(c);
  }
}

// ---------------------------------------------------------------------------
// collect_perf_candidates — performance degradation sub-section
// ---------------------------------------------------------------------------

void SafetyArbitrator::collect_perf_candidates(
    mrm::ScenarioContext const& ctx) noexcept
{
  if (ctx.performance.cpa_trend_degrading || ctx.performance.multiple_targets_nearby) {
    AlertCandidate c{};
    c.alert_type  = l3_msgs::msg::SafetyAlert::ALERT_PERFORMANCE_DEGRADED;
    c.severity    = l3_msgs::msg::SafetyAlert::SEVERITY_WARNING;
    c.recommended_mrm = mrm::MrmId::kMrm01_Drift;
    c.confidence  = 0.75F;
    c.rationale   = "SOTIF: performance degraded — CPA trend worsening or multiple targets";
    c.description = "WARNING: CPA degrading or multiple vessels nearby — heightened vigilance";
    push_candidate(c);
  }
}

// ---------------------------------------------------------------------------
// collect_candidates — populate pool_ from monitor results
// ---------------------------------------------------------------------------

void SafetyArbitrator::collect_candidates(
    mrm::ScenarioContext const& ctx,
    iec61508::DiagnosticResult const& diag,
    bool extreme_scenario) noexcept
{
  collect_iec61508_candidates(ctx, diag);
  collect_sotif_candidates(ctx, extreme_scenario);
  collect_perf_candidates(ctx);
}

// ---------------------------------------------------------------------------
// sort_candidates — insertion sort by severity descending
// ---------------------------------------------------------------------------

void SafetyArbitrator::sort_candidates() noexcept {
  // Insertion sort: O(n^2) on n <= 8 — acceptable and deterministic
  for (uint32_t i = 1U; i < pool_size_; ++i) {
    AlertCandidate const kKey = pool_[i];
    uint32_t j = i;
    while (j > 0U && pool_[j - 1U].severity < kKey.severity) {
      pool_[j] = pool_[j - 1U];
      --j;
    }
    pool_[j] = kKey;
  }
}

// ---------------------------------------------------------------------------
// arbitrate — main entry point
// ---------------------------------------------------------------------------

l3_msgs::msg::SafetyAlert SafetyArbitrator::arbitrate(
    builtin_interfaces::msg::Time const& stamp,
    mrm::ScenarioContext const& ctx,
    mrm::MrmDecision const& mrm_decision,
    iec61508::DiagnosticResult const& diag,
    bool extreme_scenario_detected) noexcept
{
  pool_size_ = 0U;  // reset pool without heap allocation

  collect_candidates(ctx, diag, extreme_scenario_detected);
  sort_candidates();

  if (pool_size_ == 0U) {
    // No active conditions: emit informational no-alert via factory
    return AlertGenerator::build_safety_alert(stamp, SafetyAlertParams{
        l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT,
        l3_msgs::msg::SafetyAlert::SEVERITY_INFO,
        mrm::to_string(mrm::MrmId::kNone),
        mrm_decision.confidence,
        "no active safety conditions",
        "M7: all monitors nominal"});
  }

  // Emit highest-priority candidate (pool_[0] after descending sort) via factory
  AlertCandidate const& top = pool_[0];
  return AlertGenerator::build_safety_alert(stamp, SafetyAlertParams{
      top.alert_type,
      top.severity,
      mrm::to_string(mrm_decision.mrm_id),
      top.confidence,
      top.rationale,
      top.description});  // now distinct from rationale
}

}  // namespace mass_l3::m7::arbitrator
