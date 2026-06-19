#include "m2_world_model/woerner_compliance_scorer.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::m2 {
namespace {

constexpr double kNeutralCompliance = 0.5;
constexpr double kMinHistoryDtS = 1e-6;
constexpr double kTrendScaleMps = 20.0;
constexpr double kHeadingChangeFullCreditDeg = 20.0;

double clamp01(double value) {
  return std::clamp(value, 0.0, 1.0);
}

bool finite_sample(const TargetComplianceSample& sample) {
  return std::isfinite(sample.t_s) && std::isfinite(sample.cpa_m) &&
      std::isfinite(sample.tcpa_s) && std::isfinite(sample.range_m) &&
      std::isfinite(sample.heading_deg);
}

double wrapped_heading_delta_abs_deg(double from_deg, double to_deg) {
  double delta = std::fmod(to_deg - from_deg + 540.0, 360.0) - 180.0;
  if (delta < 0.0) {
    delta = -delta;
  }
  return delta;
}

}  // namespace

WoernerComplianceScorer::WoernerComplianceScorer(double history_window_s)
    : history_window_s_(history_window_s > 0.0 ? history_window_s : 30.0) {}

void WoernerComplianceScorer::add_sample(const TargetComplianceSample& sample) {
  if (!finite_sample(sample)) {
    return;
  }
  if (!history_.empty() && sample.t_s < history_.back().t_s) {
    history_.clear();
  }
  history_.push_back(sample);
  prune_();
}

double WoernerComplianceScorer::score() const {
  if (history_.size() < 2U) {
    return kNeutralCompliance;
  }

  const auto& oldest = history_.front();
  const auto& newest = history_.back();
  const double dt_s = newest.t_s - oldest.t_s;
  if (dt_s <= kMinHistoryDtS) {
    return kNeutralCompliance;
  }

  const double cpa_slope_mps = (newest.cpa_m - oldest.cpa_m) / dt_s;
  const double range_slope_mps = (newest.range_m - oldest.range_m) / dt_s;
  const double heading_delta_deg =
      wrapped_heading_delta_abs_deg(oldest.heading_deg, newest.heading_deg);

  const double cpa_component = clamp01(0.5 + cpa_slope_mps / kTrendScaleMps);
  const double range_component = clamp01(0.5 + range_slope_mps / kTrendScaleMps);
  const double heading_component =
      clamp01(heading_delta_deg / kHeadingChangeFullCreditDeg);

  return clamp01(
      (0.65 * cpa_component) + (0.20 * range_component) +
      (0.15 * heading_component));
}

std::size_t WoernerComplianceScorer::sample_count() const noexcept {
  return history_.size();
}

void WoernerComplianceScorer::prune_() {
  if (history_.empty()) {
    return;
  }
  const double cutoff_s = history_.back().t_s - history_window_s_;
  while (!history_.empty() && history_.front().t_s < cutoff_s) {
    history_.pop_front();
  }
}

}  // namespace mass_l3::m2
