#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

namespace {

/// Communication RTT thresholds.
constexpr double kCommOkThreshold1 = 0.1;   // 100 ms
constexpr double kCommOkThreshold2 = 0.5;   // 500 ms
constexpr double kCommOkThreshold3 = 1.0;   // 1000 ms

/// Communication window sizes (seconds) for each threshold band.
constexpr double kCommWindowBest = 300.0;   // RTT <= 100 ms
constexpr double kCommWindowGood = 120.0;   // RTT <= 500 ms
constexpr double kCommWindowFair = 60.0;    // RTT <= 1000 ms
constexpr double kCommWindowPoor = 30.0;    // RTT > 1000 ms

/// System health thresholds.
constexpr double kSysHealMttfThreshold = 3600.0;
constexpr double kSysHealMttfFactor = 0.1;
constexpr double kSysHealHealthyMax = 300.0;
constexpr double kSysHealDegraded = 60.0;
constexpr double kSysHealCritical = 30.0;
constexpr double kSysHealModerate = 120.0;

/// Forecast degradation rates (seconds lost per second of horizon).
constexpr double kTdlDegradationPerSecond = 0.2;
constexpr double kTmrDegradationPerSecond = 0.1;

}  // anonymous namespace

// ===========================================================================
// Factory
// ===========================================================================

tl::expected<TmrTdlEstimator, ErrorCode>
TmrTdlEstimator::create(const TmrTdlParams& params) noexcept {
  if (params.tmr_baseline_s <= 0.0) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  if (params.tcpa_coefficient <= 0.0) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  if (params.tmr_min_s < 0.0 || params.tmr_min_s >= params.tmr_max_s) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  if (params.tdl_min_s < 0.0 || params.tdl_min_s >= params.tdl_max_s) {
    return tl::unexpected(ErrorCode::ParameterOutOfRange);
  }
  return TmrTdlEstimator(params);
}

// ===========================================================================
// Private constructor
// ===========================================================================

TmrTdlEstimator::TmrTdlEstimator(const TmrTdlParams& params) noexcept
    : params_(params) {}

// ===========================================================================
// Communication health estimation
// ===========================================================================

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
double TmrTdlEstimator::estimate_t_comm_ok(
    const double current_rtt_s) const noexcept {  // NOLINT(readability-identifier-naming)
  if (std::isnan(current_rtt_s)) {
    return kCommWindowPoor;
  }
  if (current_rtt_s <= kCommOkThreshold1) {
    return kCommWindowBest;
  }
  if (current_rtt_s <= kCommOkThreshold2) {
    return kCommWindowGood;
  }
  if (current_rtt_s <= kCommOkThreshold3) {
    return kCommWindowFair;
  }
  return kCommWindowPoor;
}

// ===========================================================================
// System health estimation
// ===========================================================================

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
double TmrTdlEstimator::estimate_t_sys_health(
    const SystemHealthSnapshot& health) const noexcept {
  // Priority: critical > degraded > healthy > moderate fallback

  // Critical: no redundant hardware available.
  if (!health.has_redundancy) {
    return kSysHealCritical;
  }
  // Degraded: active faults in recent window.
  if (health.fault_count > 0) {
    return kSysHealDegraded;
  }
  // Healthy: MTTF above threshold with no faults.
  if (health.mttf_estimate_s > kSysHealMttfThreshold) {
    const double kT = health.mttf_estimate_s * kSysHealMttfFactor;
    return (kT > kSysHealHealthyMax) ? kSysHealHealthyMax : kT;
  }
  // Moderate: no faults, has redundancy, but MTTF below threshold.
  return kSysHealModerate;
}

// ===========================================================================
// ToR lookup
// ===========================================================================

double TmrTdlEstimator::lookup_tor_tmr(
    const OperatorState op_state,
    const ParameterSet& params) noexcept {
  const std::size_t kIdx = static_cast<std::size_t>(op_state);
  if (kIdx >= params.tor_matrix.size()) {
    return params.tmr_baseline_s;
  }
  const double kEntry = params.tor_matrix[kIdx].tmr_s;
  return (kEntry > 0.0) ? kEntry : params.tmr_baseline_s;
}

// ===========================================================================
// Compute (base)
// ===========================================================================

TmrTdlPair TmrTdlEstimator::compute(const TmrTdlInputs& inputs) const noexcept {
  // TDL = min(TCPA_component, T_comm_ok, T_sys_health, MTTF_estimate)
  double tdl = std::min({inputs.tcpa_min_s * params_.tcpa_coefficient,
                         estimate_t_comm_ok(inputs.current_rtt_s),
                         estimate_t_sys_health(inputs.system_health)});

  // Cap TDL by system MTTF estimate (safety constraint).
  const double kMttf = inputs.system_health.mttf_estimate_s;
  if (kMttf > 0.0 && !std::isnan(kMttf)) {
    tdl = std::min(tdl, kMttf);
  }

  // Clamp TDL.
  tdl = std::clamp(tdl, params_.tdl_min_s, params_.tdl_max_s);

  // TMR = baseline * H_score_factor.
  const double kHFactor = inputs.h_score_tmr_available ? 1.0 : 0.5;
  double tmr = params_.tmr_baseline_s * kHFactor;

  // Clamp TMR.
  tmr = std::clamp(tmr, params_.tmr_min_s, params_.tmr_max_s);

  return TmrTdlPair{tmr, tdl};
}

// ===========================================================================
// Compute (operator-state-aware)
// ===========================================================================

TmrTdlPair TmrTdlEstimator::compute(
    const TmrTdlInputs& inputs,
    const ParameterSet& /*params*/,
    const OperatorState op_state) const noexcept {
  TmrTdlPair result = compute(inputs);

  double tor_tmr = lookup_tor_tmr(op_state, params_);
  if (tor_tmr > 0.0) {
    result.tmr_s = tor_tmr;
  }

  return result;
}

  // Re-clamp TMR.
  result.tmr_s = std::clamp(result.tmr_s, params_.tmr_min_s, params_.tmr_max_s);

  return result;
}

// ===========================================================================
// Forecast
// ===========================================================================

TmrTdlPair TmrTdlEstimator::forecast(
    const TmrTdlInputs& current,
    const std::chrono::seconds kHorizon) const noexcept {
  const auto kH = static_cast<double>(kHorizon.count());
  TmrTdlPair base = compute(current);

  // Zero horizon returns current estimate unchanged.
  if (kH <= 0.0) {
    return base;
  }

  // Degrade both proportionally with horizon.
  double tdl = base.tdl_s - (kH * kTdlDegradationPerSecond);
  double tmr = base.tmr_s - (kH * kTmrDegradationPerSecond);

  // Clamp.
  tdl = std::max(tdl, params_.tdl_min_s);
  tmr = std::max(tmr, params_.tmr_min_s);

  return TmrTdlPair{tmr, tdl};
}

}  // namespace mass_l3::m1
