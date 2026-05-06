#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"

#include <cmath>
#include <limits>

namespace mass_l3::m1 {

namespace {

/// Tolerance for floating-point equality in weight normalization check.
constexpr double kWeightEpsilon = 1e-6;

}  // anonymous namespace

// ===========================================================================
// Factory
// ===========================================================================

tl::expected<ConformanceScoreCalculator, ErrorCode>
ConformanceScoreCalculator::create(
    const ScoreWeights& weights,
    const EScoreThresholds& e_thresholds,
    const TScoreThresholds& t_thresholds,
    const HScoreThresholds& h_thresholds) noexcept {
  const double sum = weights.w_e + weights.w_t + weights.w_h;
  if (std::abs(sum - 1.0) > kWeightEpsilon) {
    return tl::unexpected(ErrorCode::WeightsNotNormalized);
  }
  return ConformanceScoreCalculator(weights, e_thresholds,
                                    t_thresholds, h_thresholds);
}

// ===========================================================================
// Private constructor
// ===========================================================================

ConformanceScoreCalculator::ConformanceScoreCalculator(
    const ScoreWeights& weights,
    const EScoreThresholds& e_thresholds,
    const TScoreThresholds& t_thresholds,
    const HScoreThresholds& h_thresholds) noexcept
    : weights_(weights),
      e_thresholds_(e_thresholds),
      t_thresholds_(t_thresholds),
      h_thresholds_(h_thresholds) {}

// ===========================================================================
// Environment (E) score
// ===========================================================================

double ConformanceScoreCalculator::evaluate_e_score(
    const double visibility_nm, const double sea_state_hs) const noexcept {
  // ODD-A: full conformance
  if (visibility_nm >= e_thresholds_.visibility_full_nm &&
      sea_state_hs <= e_thresholds_.sea_state_full_hs) {
    return 1.0;
  }
  // ODD-B: degraded conformance
  if (visibility_nm >= e_thresholds_.visibility_degraded_nm &&
      sea_state_hs <= e_thresholds_.sea_state_degraded_hs) {
    return 0.7;
  }
  // ODD-D warning: marginal -- uses OR (either condition triggers warning)
  if (visibility_nm >= e_thresholds_.visibility_marginal_nm ||
      sea_state_hs <= e_thresholds_.sea_state_marginal_hs) {
    return 0.4;
  }
  // OUT -- unsafe
  return 0.0;
}

// ===========================================================================
// Task (T) score
// ===========================================================================

double ConformanceScoreCalculator::evaluate_t_score(
    const TScoreInputs& t_inputs) const noexcept {
  // All nominal: every measured system is healthy.
  const bool all_nominal = t_inputs.gnss_quality_good &&
                           t_inputs.radar_health_ok &&
                           t_inputs.comm_ok &&
                           !t_inputs.any_sensor_critical &&
                           t_inputs.comm_delay_s < t_thresholds_.comm_delay_ok_s;
  if (all_nominal) {
    return 1.0;
  }

  // Sensor degradation only -- communication still within limits and no
  // critical failures.
  const bool comm_ok_delay =
      t_inputs.comm_delay_s < t_thresholds_.comm_delay_ok_s;
  if (!t_inputs.any_sensor_critical && comm_ok_delay) {
    return t_thresholds_.t_score_comm_ok;  // [TBD-HAZID] 0.6
  }

  // Comm delay exceeds threshold OR any sensor is in critical failure.
  return t_thresholds_.t_score_comm_bad;  // [TBD-HAZID] 0.3
}

// ===========================================================================
// Human (H) score
// ===========================================================================

double ConformanceScoreCalculator::evaluate_h_score(
    const HScoreInputs& h_inputs) const noexcept {
  if (h_inputs.tmr_available && h_inputs.comm_ok) {
    return h_thresholds_.h_score_tmr_available;  // [TBD-HAZID] 1.0
  }
  return h_thresholds_.h_score_tmr_unavailable;  // [TBD-HAZID] 0.5
}

// ===========================================================================
// Composite score
// ===========================================================================

ScoreTriple ConformanceScoreCalculator::compute(
    const ScoringInputs& inputs) const noexcept {
  // Evaluate each axis independently.
  double e = evaluate_e_score(inputs.visibility_nm, inputs.sea_state_hs);
  TScoreInputs t_in;
  t_in.gnss_quality_good = inputs.gnss_quality_good;
  t_in.radar_health_ok = inputs.radar_health_ok;
  t_in.comm_ok = inputs.comm_ok;
  t_in.comm_delay_s = inputs.comm_delay_s;
  t_in.any_sensor_critical = inputs.any_sensor_critical;
  double t = evaluate_t_score(t_in);

  HScoreInputs h_in;
  h_in.tmr_available = inputs.tmr_available;
  h_in.comm_ok = inputs.comm_ok;
  double h = evaluate_h_score(h_in);

  // NaN guard: per spec, any NaN sub-score is treated as 0 for that axis.
  if (std::isnan(e)) {
    e = 0.0;
  }
  if (std::isnan(t)) {
    t = 0.0;
  }
  if (std::isnan(h)) {
    h = 0.0;
  }

  // Weighted sum.
  double combined = weights_.w_e * e + weights_.w_t * t + weights_.w_h * h;

  // Clamp to [0, 1].
  if (std::isnan(combined)) {
    combined = 0.0;
  } else {
    if (combined < 0.0) {
      combined = 0.0;
    }
    if (combined > 1.0) {
      combined = 1.0;
    }
  }

  return ScoreTriple{e, t, h, combined};
}

}  // namespace mass_l3::m1
