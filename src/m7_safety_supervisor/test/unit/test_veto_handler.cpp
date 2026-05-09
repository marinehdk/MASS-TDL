#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "m7_safety_supervisor/checker/veto_handler.hpp"

using mass_l3::m7::checker::VetoHandler;
using mass_l3::m7::checker::VetoReasonClass;
using mass_l3::m7::checker::VetoHistogram;

namespace {

// ---------------------------------------------------------------------------
// Helper: construct a CheckerVetoNotification with given class and optional detail.
// veto_reason_detail is set only to verify M7 never inspects it.
// ---------------------------------------------------------------------------
l3_external_msgs::msg::CheckerVetoNotification build_veto_msg(
  std::uint8_t reason_class,
  std::string const& detail = "")
{
  l3_external_msgs::msg::CheckerVetoNotification msg;
  msg.veto_reason_class = reason_class;
  msg.veto_reason_detail = detail;
  msg.confidence = 0.9F;
  return msg;
}

// ---------------------------------------------------------------------------
// Helper: index cast
// ---------------------------------------------------------------------------
constexpr std::size_t idx(VetoReasonClass c)
{
  return static_cast<std::size_t>(c);
}

}  // namespace

// ============================================================================
// Tests 1–6: Each enum class increments the correct histogram bucket
// ============================================================================

// Test 1 — kColregsViolation (class 0)
TEST(VetoHandlerTest, EnumClass_ColregsViolation_IncrementsBucket0)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(0U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)], 1U);
  // All other buckets must remain zero
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             0U);
}

// Test 2 — kCpaBelowThreshold (class 1)
TEST(VetoHandlerTest, EnumClass_CpaBelowThreshold_IncrementsBucket1)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(1U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 1U);
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             0U);
}

// Test 3 — kEncConstraint (class 2)
TEST(VetoHandlerTest, EnumClass_EncConstraint_IncrementsBucket2)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(2U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     1U);
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             0U);
}

// Test 4 — kActuatorLimit (class 3)
TEST(VetoHandlerTest, EnumClass_ActuatorLimit_IncrementsBucket3)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(3U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     1U);
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             0U);
}

// Test 5 — kTimeout (class 4)
TEST(VetoHandlerTest, EnumClass_Timeout_IncrementsBucket4)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(4U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           1U);
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             0U);
}

// Test 6 — kOther (class 5)
TEST(VetoHandlerTest, EnumClass_Other_IncrementsBucket5)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(5U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)],             1U);
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
}

// ============================================================================
// Test 7: Out-of-range veto_reason_class maps to kOther
// ============================================================================
TEST(VetoHandlerTest, OutOfRange_99_MapsToKOther)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(99U));
  VetoHistogram h = handler.histogram();
  EXPECT_EQ(h[idx(VetoReasonClass::kOther)], 1U);
  // All non-kOther buckets must be zero
  EXPECT_EQ(h[idx(VetoReasonClass::kColregsViolation)],  0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kCpaBelowThreshold)], 0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kEncConstraint)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kActuatorLimit)],     0U);
  EXPECT_EQ(h[idx(VetoReasonClass::kTimeout)],           0U);
}

// ============================================================================
// Test 8: veto_reason_detail set to arbitrary string — histogram/rate identical
// to without it (RFC-003 regression guard). The definitive non-access proof is
// code review of the 5-line on_veto_received body + grep confirming zero
// references to veto_reason_detail in src/checker/. This test catches any
// future regression where parsing logic is added that affects observable state.
// ============================================================================
TEST(VetoHandlerTest, FreeTextDetail_DoesNotAffectHistogramOrRate)
{
  VetoHandler handler_no_detail;
  VetoHandler handler_with_detail;

  // Both receive class 0 (ColregsViolation); handler_with_detail has a
  // populated detail string that M7 must never read.
  auto msg_no_detail   = build_veto_msg(0U, "");
  auto msg_with_detail = build_veto_msg(0U, "COLREG Rule 15 starboard crossing — bearing 045 CPA 0.12nm");

  handler_no_detail.on_veto_received(msg_no_detail);
  handler_with_detail.on_veto_received(msg_with_detail);

  // Tick both windows identically
  handler_no_detail.on_cycle_tick(true);
  handler_with_detail.on_cycle_tick(true);

  // Histogram must be identical
  EXPECT_EQ(handler_no_detail.histogram(), handler_with_detail.histogram());

  // Rate must be identical
  EXPECT_DOUBLE_EQ(handler_no_detail.current_rate(), handler_with_detail.current_rate());
}

// ============================================================================
// Test 9: RFC-003 boundary — exactly 20 ticks-with-veto out of 100 → rate ≈ 0.20
// ============================================================================
TEST(VetoHandlerTest, RFC003Boundary_20VetoTicksOf100_RateIs020)
{
  VetoHandler handler;

  for (std::uint32_t i = 0U; i < 20U; ++i) {
    handler.on_veto_received(build_veto_msg(0U));
    handler.on_cycle_tick(true);
  }
  for (std::uint32_t i = 0U; i < 80U; ++i) {
    handler.on_cycle_tick(false);
  }

  EXPECT_NEAR(handler.current_rate(), 0.20, 1e-9);
}

// ============================================================================
// Test 10: Rate after 0 vetoes → 0.0
// ============================================================================
TEST(VetoHandlerTest, NoVetoes_RateIsZero)
{
  VetoHandler handler;
  for (std::uint32_t i = 0U; i < 100U; ++i) {
    handler.on_cycle_tick(false);
  }
  EXPECT_DOUBLE_EQ(handler.current_rate(), 0.0);
}

// ============================================================================
// Test 11: reset() clears histogram to all zeros and window to 0 rate
// ============================================================================
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(VetoHandlerTest, Reset_ClearsHistogramAndRate)
{
  VetoHandler handler;

  // Populate with vetoes of various classes
  handler.on_veto_received(build_veto_msg(0U));
  handler.on_veto_received(build_veto_msg(1U));
  handler.on_veto_received(build_veto_msg(2U));
  handler.on_cycle_tick(true);
  handler.on_cycle_tick(true);

  // Confirm non-zero state before reset
  EXPECT_GT(handler.current_rate(), 0.0);
  VetoHistogram h_before = handler.histogram();
  EXPECT_GT(h_before[0], 0U);

  handler.reset();

  // Rate must be zero
  EXPECT_DOUBLE_EQ(handler.current_rate(), 0.0);

  // All histogram buckets must be zero
  VetoHistogram h_after = handler.histogram();
  for (std::size_t i = 0U; i < h_after.size(); ++i) {
    EXPECT_EQ(h_after[i], 0U) << "bucket " << i << " not cleared";
  }
}

// ============================================================================
// Test 12: Multiple vetoes of same class accumulate in histogram
// ============================================================================
TEST(VetoHandlerTest, MultipleVetoesSameClass_Accumulate)
{
  VetoHandler handler;
  constexpr std::uint32_t kRepeat = 7U;

  for (std::uint32_t i = 0U; i < kRepeat; ++i) {
    handler.on_veto_received(build_veto_msg(
      static_cast<std::uint8_t>(VetoReasonClass::kCpaBelowThreshold)));
  }

  EXPECT_EQ(handler.histogram()[idx(VetoReasonClass::kCpaBelowThreshold)], kRepeat);
}

// ============================================================================
// Test 13: histogram() returns a copy — mutating returned array does not
// affect internal state (value semantics verification).
// ============================================================================
TEST(VetoHandlerTest, Histogram_ReturnsCopy_MutationDoesNotAffectInternal)
{
  VetoHandler handler;
  handler.on_veto_received(build_veto_msg(0U));

  VetoHistogram copy1 = handler.histogram();
  EXPECT_EQ(copy1[0], 1U);

  // Mutate the copy
  copy1[0] = 999U;

  // Internal state must remain unchanged
  VetoHistogram copy2 = handler.histogram();
  EXPECT_EQ(copy2[0], 1U);
}

// ============================================================================
// Test 14: Constructor parameter window_cycle_count silently ignored;
// window capacity is always kCapacity=100 (RFC-003 LOCKED).
// ============================================================================
TEST(VetoHandlerTest, ConstructorParam_SilentlyIgnored_CapacityIs100)
{
  // Pass an arbitrary non-default value; must NOT cause assertion or warning.
  VetoHandler handler(42U);

  // Fill 20 true ticks and 80 false ticks → rate == 0.20 proves window uses 100
  for (std::uint32_t i = 0U; i < 20U; ++i) { handler.on_cycle_tick(true); }
  for (std::uint32_t i = 0U; i < 80U; ++i) { handler.on_cycle_tick(false); }

  EXPECT_NEAR(handler.current_rate(), 0.20, 1e-9);
}

// ============================================================================
// Test 15: All 6 enum classes in one handler — histogram sums correctly
// ============================================================================
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(VetoHandlerTest, AllSixClasses_HistogramSumsCorrectly)
{
  VetoHandler handler;
  for (std::uint8_t c = 0U; c < 6U; ++c) {
    handler.on_veto_received(build_veto_msg(c));
  }
  VetoHistogram h = handler.histogram();
  std::uint32_t total = 0U;
  for (auto count : h) { total += count; }
  EXPECT_EQ(total, 6U);
  // Each bucket must have exactly 1
  for (std::size_t i = 0U; i < h.size(); ++i) {
    EXPECT_EQ(h[i], 1U) << "bucket " << i << " expected 1";
  }
}
