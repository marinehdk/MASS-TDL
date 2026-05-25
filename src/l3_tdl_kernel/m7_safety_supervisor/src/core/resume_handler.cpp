#include "m7_safety_supervisor/core/resume_handler.hpp"

namespace mass_l3::m7::core {

void ResumeHandler::on_override_inactive(
    std::chrono::steady_clock::time_point t0) noexcept
{
  if (state_ == ResumeState::kOverrideActive || state_ == ResumeState::kIdle) {
    state_ = ResumeState::kPreResumeCheck;
    t0_ = t0;
    stable_cycle_count_ = 0;
  }
}

void ResumeHandler::on_m7_stable(
    std::chrono::steady_clock::time_point /*now*/) noexcept
{
  if (state_ != ResumeState::kPreResumeCheck) { return; }
  ++stable_cycle_count_;
  if (stable_cycle_count_ >= kStableCyclesRequired) {
    state_ = ResumeState::kReady;
  }
}

void ResumeHandler::on_m5_first_output(
    std::chrono::steady_clock::time_point /*now*/) noexcept
{
  if (state_ == ResumeState::kReady) { state_ = ResumeState::kResumed; }
}

void ResumeHandler::on_resume_timeout() noexcept
{
  state_ = ResumeState::kResumeTimeout;
}

void ResumeHandler::set_override_active(bool active) noexcept
{
  if (active) { state_ = ResumeState::kOverrideActive; }
}

ResumeTimeoutResult ResumeHandler::check_timeout(
    std::chrono::steady_clock::time_point now) noexcept
{
  ResumeTimeoutResult result{};
  if (state_ == ResumeState::kPreResumeCheck || state_ == ResumeState::kReady) {
    auto const elapsed = now - t0_;
    if (elapsed > kM7ReadyTimeout && state_ == ResumeState::kPreResumeCheck) {
      state_ = ResumeState::kResumeTimeout;
      result.timeout_triggered = true;
    }
    if (elapsed > kM5OutputTimeout && state_ == ResumeState::kReady) {
      state_ = ResumeState::kResumeTimeout;
      result.timeout_triggered = true;
    }
  }
  return result;
}

}  // namespace mass_l3::m7::core
