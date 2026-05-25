#ifndef M7_SAFETY_SUPERVISOR_CORE_RESUME_HANDLER_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_RESUME_HANDLER_HPP_

#include <chrono>
#include <cstdint>

namespace mass_l3::m7::core {

enum class ResumeState : std::uint8_t {
  kIdle,
  kOverrideActive,
  kPreResumeCheck,
  kReady,
  kResumed,
  kResumeTimeout,
};

struct ResumeTimeoutResult {
  bool timeout_triggered{false};
};

class ResumeHandler {
public:
  ResumeHandler() noexcept = default;

  void on_override_inactive(std::chrono::steady_clock::time_point t0) noexcept;
  void on_m7_stable(std::chrono::steady_clock::time_point now) noexcept;
  void on_m5_first_output(std::chrono::steady_clock::time_point now) noexcept;
  void on_resume_timeout() noexcept;
  void set_override_active(bool active) noexcept;

  ResumeTimeoutResult check_timeout(std::chrono::steady_clock::time_point now) noexcept;
  ResumeState state() const noexcept { return state_; }
  bool is_timeout() const noexcept { return state_ == ResumeState::kResumeTimeout; }

private:
  ResumeState state_{ResumeState::kIdle};
  std::chrono::steady_clock::time_point t0_{};
  std::uint32_t stable_cycle_count_{0};
  static constexpr std::uint32_t kStableCyclesRequired = 5;
  static constexpr auto kM7ReadyTimeout = std::chrono::milliseconds(100);
  static constexpr auto kM5OutputTimeout = std::chrono::milliseconds(150);
};

}  // namespace mass_l3::m7::core

#endif
