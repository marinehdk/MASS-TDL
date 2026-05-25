#include <gtest/gtest.h>
#include <chrono>
#include "m7_safety_supervisor/core/resume_handler.hpp"

namespace mass_l3::m7::core {
namespace {
using namespace std::chrono_literals;

TEST(ResumeHandler, NormalResumeSequence)
{
  ResumeHandler handler;
  auto const t0 = std::chrono::steady_clock::now();
  handler.on_override_inactive(t0);
  EXPECT_EQ(handler.state(), ResumeState::kPreResumeCheck);
  for (int i = 0; i < 5; ++i) {
    handler.on_m7_stable(t0 + 10ms + i * 250ms);
  }
  EXPECT_EQ(handler.state(), ResumeState::kReady);
  handler.on_m5_first_output(t0 + 110ms);
  EXPECT_EQ(handler.state(), ResumeState::kResumed);
}

TEST(ResumeHandler, TimeoutWhenM7NotReadyWithin100ms)
{
  ResumeHandler handler;
  auto const t0 = std::chrono::steady_clock::now();
  handler.on_override_inactive(t0);
  handler.on_m7_stable(t0 + 10ms);
  handler.on_m7_stable(t0 + 12ms);
  auto const result = handler.check_timeout(t0 + 110ms);
  EXPECT_TRUE(result.timeout_triggered);
  EXPECT_EQ(handler.state(), ResumeState::kResumeTimeout);
}

TEST(ResumeHandler, ResumeTimeoutReturnsTrue)
{
  ResumeHandler handler;
  auto const t0 = std::chrono::steady_clock::now();
  handler.on_override_inactive(t0);
  handler.on_resume_timeout();
  EXPECT_EQ(handler.state(), ResumeState::kResumeTimeout);
  EXPECT_TRUE(handler.is_timeout());
}

TEST(ResumeHandler, IdleStateIgnoresStableEvents)
{
  ResumeHandler handler;
  auto const t0 = std::chrono::steady_clock::now();
  handler.on_m7_stable(t0);
  EXPECT_EQ(handler.state(), ResumeState::kIdle);
}

TEST(ResumeHandler, OverrideActiveToInactiveTransition)
{
  ResumeHandler handler;
  auto const t0 = std::chrono::steady_clock::now();
  handler.set_override_active(true);
  EXPECT_EQ(handler.state(), ResumeState::kOverrideActive);
  handler.on_override_inactive(t0);
  EXPECT_EQ(handler.state(), ResumeState::kPreResumeCheck);
}

}  // namespace
}  // namespace mass_l3::m7::core
