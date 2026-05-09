#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

class ViewHealthMonitor final {
 public:
  struct Config {
    // DV thresholds
    int32_t dv_loss_periods_to_degraded;       // [TBD-HAZID] 2
    int32_t dv_loss_periods_to_critical;       // [TBD-HAZID] 5
    double dv_degraded_to_critical_timeout_s;  // [TBD-HAZID] 30.0
    // EV thresholds
    double ev_loss_ms_to_critical;             // [TBD-HAZID] 100 ms
    // SV thresholds
    double sv_loss_s_to_degraded;              // [TBD-HAZID] 30.0
    // Confidence floors
    double confidence_floor_dv_degraded;       // [TBD-HAZID] 0.5
  };

  explicit ViewHealthMonitor(Config cfg);

  /// DV: called every aggregation cycle; received_update=false if no target update.
  void report_dv_update(bool received_update,
                         std::chrono::steady_clock::time_point now);
  /// EV: called every EV update; age_s = elapsed since last own-ship msg.
  void report_ev_age(double age_s,
                     std::chrono::steady_clock::time_point now);
  /// SV: called every aggregation cycle; age_s = elapsed since last env msg.
  void report_sv_age(double age_s,
                     std::chrono::steady_clock::time_point now);

  /// Reset health for all views (e.g., on startup or after recovery).
  void reset();

  /// Current confidence levels for each view.
  [[nodiscard]] AggregatedHealth aggregated_health() const;

 private:
  struct ViewState {
    ViewHealth health = ViewHealth::Full;
    int32_t consecutive_misses = 0;
    std::chrono::steady_clock::time_point degraded_since;
    double confidence = 1.0;
  };

  Config cfg_;
  ViewState dv_, ev_, sv_;
  mutable std::mutex mutex_;
};

}  // namespace mass_l3::m2
