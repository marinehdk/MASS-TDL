#include "m7_safety_supervisor/sotif/performance_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_risk_model/risk_model.hpp"

namespace mass_l3::m7::sotif {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kKnotsToMps = 0.5144444444444445;

// ---------------------------------------------------------------------------
// compute_min_cpa — extracted helper
// ---------------------------------------------------------------------------

float compute_min_cpa(l3_msgs::msg::WorldState const& world) noexcept {
  constexpr float kSentinelM = 9999.0F * 1852.0F;
  float min_cpa_m = kSentinelM;
  bool has_targets = false;
  for (auto const& target : world.targets) {
    auto const kCpaM = static_cast<float>(target.cpa_m);
    if (!has_targets || kCpaM < min_cpa_m) {
      min_cpa_m = kCpaM;
      has_targets = true;
    }
  }
  constexpr float kSentinelNm = 9999.0F;
  return has_targets ? (min_cpa_m / 1852.0F) : kSentinelNm;
}

mass_l3::risk::OwnShipInput ownship_risk_input(l3_msgs::msg::WorldState const& world) {
  return mass_l3::risk::OwnShipInput{
      0.0,
      0.0,
      world.own_ship.heading_deg * kDegToRad,
      std::max(0.0, world.own_ship.sog_kn * kKnotsToMps),
      46.0,
      static_cast<double>(world.own_ship.confidence),
      world.own_ship.nav_mode == "DEGRADED"};
}

mass_l3::risk::TargetInput target_risk_input(l3_msgs::msg::TrackedTarget const& target) {
  double const bearing_rad = target.brg_deg * kDegToRad;
  double const range_m = std::max(0.0, target.rng_m);
  return mass_l3::risk::TargetInput{
      std::to_string(target.target_id),
      std::cos(bearing_rad) * range_m,
      std::sin(bearing_rad) * range_m,
      target.cog_deg * kDegToRad,
      std::max(0.0, target.sog_kn * kKnotsToMps),
      target.cpa_m,
      target.tcpa_s,
      static_cast<double>(target.confidence)};
}

bool phase_at_least(mass_l3::risk::RiskPhase phase, mass_l3::risk::RiskPhase threshold) noexcept {
  return static_cast<std::uint8_t>(phase) >= static_cast<std::uint8_t>(threshold);
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

PerformanceMonitor::PerformanceMonitor(PerformanceConfig const& cfg) noexcept
  : cfg_{cfg}
{
  cpa_history_.fill(0.0F);
}

// ---------------------------------------------------------------------------
// compute_slope — extracted helper
// ---------------------------------------------------------------------------

double PerformanceMonitor::compute_slope() const noexcept {
  if (history_count_ < 2U) {
    return 0.0;
  }
  // Most recent: written at (history_idx_ - 1) % 30
  std::uint32_t const kNewestIdx = (history_idx_ - 1U) % 30U;
  // Oldest: if ring not full, index 0; if full, current write cursor (history_idx_ % 30)
  std::uint32_t const kOldestIdx = (history_count_ < 30U) ? 0U : (history_idx_ % 30U);
  float const kNewestVal = cpa_history_[kNewestIdx];
  float const kOldestVal = cpa_history_[kOldestIdx];
  return static_cast<double>(kNewestVal - kOldestVal) /
         static_cast<double>(history_count_ - 1U);
}

// ---------------------------------------------------------------------------
// compute_max_cpa_in_window — extracted helper
// ---------------------------------------------------------------------------

double PerformanceMonitor::compute_max_cpa_in_window() const noexcept {
  double max_cpa_nm = 0.0;
  for (std::uint32_t i = 0U; i < history_count_; ++i) {
    auto const kVal = static_cast<double>(cpa_history_[i]);
    max_cpa_nm = std::max(max_cpa_nm, kVal);
  }
  return max_cpa_nm;
}

// ---------------------------------------------------------------------------
// evaluate()
// ---------------------------------------------------------------------------

PerformanceStatus PerformanceMonitor::evaluate(
    l3_msgs::msg::WorldState const& world,
    std::chrono::steady_clock::time_point now) noexcept
{
  double dt_s = 0.0;
  if (has_last_eval_time_) {
    dt_s = std::chrono::duration<double>(now - last_eval_time_).count();
    if (dt_s < 0.0) {
      dt_s = 0.0;
    }
  }
  last_eval_time_ = now;
  has_last_eval_time_ = true;

  // Step 1-2: find min CPA over all targets (meters), convert to NM
  float const kMinCpaNm = compute_min_cpa(world);

  // Step 3: store in ring buffer
  cpa_history_[history_idx_ % 30U] = kMinCpaNm;
  ++history_idx_;
  if (history_count_ < 30U) {
    ++history_count_;
  }

  // Step 4: compute slope
  double const kSlope = compute_slope();

  // Step 5: degrading flag
  bool const kCpaTrendDegrading = (kSlope < cfg_.cpa_trend_slope_threshold_nm_s);

  // Step 6: max CPA in window
  double const kMaxCpaNm = compute_max_cpa_in_window();

  // Step 7: multiple targets nearby
  double const kCpaThresholdM = cfg_.multiple_targets_cpa_threshold_nm * 1852.0;
  std::uint32_t close_count = 0U;
  for (auto const& target : world.targets) {
    if (target.cpa_m < kCpaThresholdM) {
      ++close_count;
    }
  }
  bool const kMultipleTargetsNearby =
      (close_count >= cfg_.multiple_targets_count_threshold);

  PerformanceStatus status{};
  status.cpa_trend_degrading = kCpaTrendDegrading;
  status.cpa_trend_slope_nm_s = kSlope;
  status.max_cpa_in_window_nm = kMaxCpaNm;
  status.multiple_targets_nearby = kMultipleTargetsNearby;
  status.critical_target_count = close_count;

  std::vector<mass_l3::risk::RiskVector> risks;
  risks.reserve(world.targets.size());
  auto const own = ownship_risk_input(world);
  for (auto const& target : world.targets) {
    if (target.rng_m > 0.0 && std::isfinite(target.rng_m)) {
      risks.push_back(mass_l3::risk::evaluate_target(
          own,
          target_risk_input(target),
          mass_l3::risk::ColregsDuty::Free));
    }
  }

  if (!risks.empty()) {
    auto const primary = mass_l3::risk::select_primary(risks, nullptr);
    if (!has_domain_risk_) {
      worst_warning_margin_m_ = primary.warning_margin_m;
      worst_danger_margin_m_ = primary.danger_margin_m;
      has_domain_risk_ = true;
    }
    max_risk_score_ = std::max(max_risk_score_, primary.risk_score);
    worst_warning_margin_m_ = std::min(worst_warning_margin_m_, primary.warning_margin_m);
    worst_danger_margin_m_ = std::min(worst_danger_margin_m_, primary.danger_margin_m);
    if (phase_at_least(primary.risk_phase, mass_l3::risk::RiskPhase::Warning)) {
      warning_domain_exposure_s_ += dt_s;
    }
    if (phase_at_least(primary.risk_phase, mass_l3::risk::RiskPhase::Danger)) {
      danger_domain_exposure_s_ += dt_s;
    }

    status.primary_threat_id = primary.target_id;
    status.danger_veto_active =
        phase_at_least(primary.risk_phase, mass_l3::risk::RiskPhase::Danger) &&
        primary.closing_speed_mps > 0.0;
  }
  status.max_risk_score = max_risk_score_;
  status.worst_warning_margin_m = worst_warning_margin_m_;
  status.worst_danger_margin_m = worst_danger_margin_m_;
  status.warning_domain_exposure_s = warning_domain_exposure_s_;
  status.danger_domain_exposure_s = danger_domain_exposure_s_;
  return status;
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void PerformanceMonitor::reset() noexcept {
  cpa_history_.fill(0.0F);
  history_idx_ = 0U;
  history_count_ = 0U;
  has_last_eval_time_ = false;
  has_domain_risk_ = false;
  max_risk_score_ = 0.0;
  worst_warning_margin_m_ = 0.0;
  worst_danger_margin_m_ = 0.0;
  warning_domain_exposure_s_ = 0.0;
  danger_domain_exposure_s_ = 0.0;
}

}  // namespace mass_l3::m7::sotif
