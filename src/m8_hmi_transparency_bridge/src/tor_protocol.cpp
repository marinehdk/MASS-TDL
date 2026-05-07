// src/tor_protocol.cpp
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

#include <algorithm>

namespace mass_l3::m8 {

bool TorProtocol::trigger(Reason reason, TimePoint now)
{
  if (state_ != State::kIdle) {
    return false;
  }
  state_ = State::kRequested;
  last_reason_ = reason;
  requested_time_ = now;
  retries_ = 0;
  return true;
}

bool TorProtocol::is_acknowledgment_button_enabled(TimePoint now) const
{
  if (state_ != State::kRequested) {
    return false;
  }
  const double elapsed =
      std::chrono::duration<double>(now - requested_time_).count();
  return elapsed >= config_.sat1_min_display_s;
}

std::optional<TorProtocol::AcknowledgmentSnapshot>
TorProtocol::on_acknowledgment_clicked(
    TimePoint now,
    const std::vector<std::string>& current_sat1_threats,
    const std::string& current_odd_zone,
    float current_conformance_score,
    const std::string& operator_id)
{
  if (!is_acknowledgment_button_enabled(now)) {
    return std::nullopt;
  }
  if (state_ != State::kRequested) {
    return std::nullopt;
  }

  AcknowledgmentSnapshot snap{};
  snap.click_time = now;
  snap.sat1_display_duration_s =
      std::chrono::duration<double>(now - requested_time_).count();
  snap.sat1_threats_visible = current_sat1_threats;
  snap.odd_zone_at_click = current_odd_zone;
  snap.conformance_score_at_click = current_conformance_score;
  snap.operator_id = operator_id;

  state_ = State::kAcknowledged;
  return snap;
}

bool TorProtocol::tick(TimePoint now)
{
  if (state_ != State::kRequested) {
    return false;
  }
  const double elapsed =
      std::chrono::duration<double>(now - requested_time_).count();
  if (elapsed >= config_.deadline_s) {
    state_ = State::kTimeoutMrc;
    return true;
  }
  return false;
}

void TorProtocol::reset() noexcept
{
  state_ = State::kIdle;
  retries_ = 0;
}

double TorProtocol::remaining_deadline_s(TimePoint now) const
{
  if (state_ == State::kIdle || state_ == State::kAcknowledged) {
    return 0.0;
  }
  const double elapsed =
      std::chrono::duration<double>(now - requested_time_).count();
  const double remaining = config_.deadline_s - elapsed;
  return remaining > 0.0 ? remaining : 0.0;
}

}  // namespace mass_l3::m8
