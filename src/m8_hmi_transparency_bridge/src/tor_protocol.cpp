#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

TorProtocol::TorProtocol(const Config& config) : config_{config} {}

void TorProtocol::issue(const std::string& /*reason*/, TimePoint now)
{
  state_ = State::kPending;
  issued_at_ = now;
  last_retry_at_ = now;
  retry_count_ = 0;
}

void TorProtocol::acknowledge(TimePoint /*now*/)
{
  state_ = State::kAcknowledged;
}

void TorProtocol::tick(TimePoint now)
{
  if (state_ != State::kPending) {
    return;
  }
  if (remaining_s(now) <= 0.0) {
    state_ = State::kExpired;
  }
}

TorProtocol::State TorProtocol::state() const
{
  return state_;
}

double TorProtocol::remaining_s(TimePoint now) const
{
  auto elapsed = std::chrono::duration<double>(now - issued_at_).count();
  return config_.deadline_s - elapsed;
}

int TorProtocol::retry_count() const
{
  return retry_count_;
}

}  // namespace mass_l3::m8
