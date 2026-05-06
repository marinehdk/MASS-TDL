#include "m2_world_model/view_health_monitor.hpp"

#include <algorithm>

namespace mass_l3::m2 {

// ── Constructor ──────────────────────────────────────────────────────────────

ViewHealthMonitor::ViewHealthMonitor(Config cfg) : cfg_(cfg) {}

// ── DV health ────────────────────────────────────────────────────────────────

void ViewHealthMonitor::report_dv_update(bool received_update,
                                          std::chrono::steady_clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (received_update) {
    // Recovery: reset miss counter and return to Full.
    dv_.consecutive_misses = 0;
    dv_.health = ViewHealth::Full;
    dv_.confidence = 1.0;
    return;
  }

  // Count-based threshold progression.
  dv_.consecutive_misses++;

  if (dv_.consecutive_misses >= cfg_.dv_loss_periods_to_critical) {
    dv_.health = ViewHealth::Critical;
    dv_.confidence = 0.0;
  } else if (dv_.consecutive_misses >= cfg_.dv_loss_periods_to_degraded) {
    if (dv_.health != ViewHealth::Degraded) {
      // First entry into Degraded — start the timer.
      dv_.degraded_since = now;
    }
    dv_.health = ViewHealth::Degraded;
    dv_.confidence = cfg_.confidence_floor_dv_degraded;
  }

  // Timer-based: if already Degraded, check whether the timeout has expired.
  if (dv_.health == ViewHealth::Degraded) {
    const auto elapsed_s =
        std::chrono::duration<double>(now - dv_.degraded_since).count();
    if (elapsed_s >= cfg_.dv_degraded_to_critical_timeout_s) {
      dv_.health = ViewHealth::Critical;
      dv_.confidence = 0.0;
    }
  }
}

// ── EV health ────────────────────────────────────────────────────────────────

void ViewHealthMonitor::report_ev_age(double age_s,
                                       std::chrono::steady_clock::time_point now) {
  static_cast<void>(now);
  std::lock_guard<std::mutex> lock(mutex_);

  if (age_s * 1000.0 > cfg_.ev_loss_ms_to_critical) {
    ev_.health = ViewHealth::Critical;
    ev_.confidence = 0.0;
  } else {
    ev_.health = ViewHealth::Full;
    ev_.confidence = 1.0;
  }
}

// ── SV health ────────────────────────────────────────────────────────────────

void ViewHealthMonitor::report_sv_age(double age_s,
                                       std::chrono::steady_clock::time_point now) {
  static_cast<void>(now);
  std::lock_guard<std::mutex> lock(mutex_);

  if (age_s > cfg_.sv_loss_s_to_degraded) {
    sv_.health = ViewHealth::Degraded;
    sv_.confidence = 0.5;
  } else {
    sv_.health = ViewHealth::Full;
    sv_.confidence = 1.0;
  }
}

// ── Reset ────────────────────────────────────────────────────────────────────

void ViewHealthMonitor::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  dv_ = ViewState{};
  ev_ = ViewState{};
  sv_ = ViewState{};
}

// ── Aggregated health ────────────────────────────────────────────────────────

AggregatedHealth ViewHealthMonitor::aggregated_health() const {
  std::lock_guard<std::mutex> lock(mutex_);

  AggregatedHealth h;
  h.dv_confidence = dv_.confidence;
  h.ev_confidence = ev_.confidence;
  h.sv_confidence = sv_.confidence;
  h.dv_health = dv_.health;
  h.ev_health = ev_.health;
  h.sv_health = sv_.health;
  h.aggregated = std::min({dv_.confidence, ev_.confidence, sv_.confidence});
  h.aggregated = std::clamp(h.aggregated, 0.0, 1.0);
  return h;
}

}  // namespace mass_l3::m2
