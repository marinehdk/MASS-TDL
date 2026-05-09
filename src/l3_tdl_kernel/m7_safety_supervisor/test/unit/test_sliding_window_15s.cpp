#include <gtest/gtest.h>

#include "m7_safety_supervisor/checker/sliding_window_15s.hpp"

using mass_l3::m7::checker::SlidingWindow15s;

// ---------------------------------------------------------------------------
// Test 1: Empty window — rate and count are zero, not filled
// ---------------------------------------------------------------------------
TEST(SlidingWindow15sTest, EmptyWindow_RateIsZero)
{
  SlidingWindow15s w;
  EXPECT_DOUBLE_EQ(w.rate(), 0.0);
  EXPECT_EQ(w.count(), 0u);
  EXPECT_FALSE(w.is_filled());
}

// ---------------------------------------------------------------------------
// Test 2: 100 all-true ticks → rate == 1.0, filled, count == 100
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, AllEventsFull_RateIsOne)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity; ++i) {
    w.tick(true);
  }
  EXPECT_DOUBLE_EQ(w.rate(), 1.0);
  EXPECT_TRUE(w.is_filled());
  EXPECT_EQ(w.count(), SlidingWindow15s::kCapacity);
}

// ---------------------------------------------------------------------------
// Test 3: 100 all-false ticks → rate == 0.0, filled, count == 0
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, NoEventsFull_RateIsZero)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity; ++i) {
    w.tick(false);
  }
  EXPECT_DOUBLE_EQ(w.rate(), 0.0);
  EXPECT_TRUE(w.is_filled());
  EXPECT_EQ(w.count(), 0u);
}

// ---------------------------------------------------------------------------
// Test 4: 100 alternating ticks → rate ≈ 0.5
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, HalfEvents_RateIsHalf)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity; ++i) {
    w.tick((i % 2u) == 0u);
  }
  EXPECT_NEAR(w.rate(), 0.5, 1e-9);
  EXPECT_EQ(w.count(), SlidingWindow15s::kCapacity / 2u);
}

// ---------------------------------------------------------------------------
// Test 5: Circular overwrite — oldest events are correctly evicted
// Fill: 50 true, 50 false → rate == 0.5
// Tick 50 more true (evicts the 50 false) → rate == 0.5 still (50 old true + 50 new true → wait, 50 old true remain + 50 new true = 100 true... no)
// Correction per spec: after 50 true + 50 false, rate=0.5.
// Then 50 more true evicts the first 50 true → still 50 true (new) + 50 false → rate=0.5
// Then 50 more true evicts the 50 false → 100 true → rate=1.0
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, CircularOverwrite_OldestEventDropped)
{
  SlidingWindow15s w;

  // Phase 1: fill 50 true, 50 false
  for (std::uint32_t i = 0; i < 50u; ++i) { w.tick(true); }
  for (std::uint32_t i = 0; i < 50u; ++i) { w.tick(false); }
  EXPECT_NEAR(w.rate(), 0.5, 1e-9);

  // Phase 2: 50 more true — evicts the first 50 true, window now has 50 false + 50 true
  for (std::uint32_t i = 0; i < 50u; ++i) { w.tick(true); }
  EXPECT_NEAR(w.rate(), 0.5, 1e-9);

  // Phase 3: 50 more true — evicts the 50 false, window now has 100 true
  for (std::uint32_t i = 0; i < 50u; ++i) { w.tick(true); }
  EXPECT_DOUBLE_EQ(w.rate(), 1.0);
}

// ---------------------------------------------------------------------------
// Test 6: is_filled transitions correctly at exactly 100 ticks
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, IsFilledAfter100Ticks)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity - 1u; ++i) {
    w.tick(false);
  }
  EXPECT_FALSE(w.is_filled());
  w.tick(false);
  EXPECT_TRUE(w.is_filled());
}

// ---------------------------------------------------------------------------
// Test 7: count() matches manual count for known pattern
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, CountConsistency_RandomPattern)
{
  SlidingWindow15s w;
  // Pattern: true on i % 3 == 0 (indices 0, 3, 6, …, 99) → 34 events
  std::uint32_t expected = 0u;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity; ++i) {
    bool ev = ((i % 3u) == 0u);
    w.tick(ev);
    if (ev) { ++expected; }
  }
  EXPECT_EQ(w.count(), expected);
  EXPECT_NEAR(w.rate(), static_cast<double>(expected) / static_cast<double>(SlidingWindow15s::kCapacity), 1e-9);
}

// ---------------------------------------------------------------------------
// Test 8: reset() clears all state; subsequent tick starts fresh
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, ResetClearsWindow)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < SlidingWindow15s::kCapacity; ++i) {
    w.tick(true);
  }
  EXPECT_DOUBLE_EQ(w.rate(), 1.0);

  w.reset();
  EXPECT_DOUBLE_EQ(w.rate(), 0.0);
  EXPECT_EQ(w.count(), 0u);
  EXPECT_FALSE(w.is_filled());

  // One true tick after reset → count=1, rate=0.01
  w.tick(true);
  EXPECT_EQ(w.count(), 1u);
  EXPECT_NEAR(w.rate(), 0.01, 1e-9);
}

// ---------------------------------------------------------------------------
// Test 9: RFC-003 threshold boundary — exactly 20 events → rate == 0.20
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(SlidingWindow15sTest, ExactThreshold_At20Events_Rate02)
{
  SlidingWindow15s w;
  for (std::uint32_t i = 0; i < 20u; ++i) { w.tick(true); }
  for (std::uint32_t i = 0; i < 80u; ++i) { w.tick(false); }
  EXPECT_NEAR(w.rate(), 0.20, 1e-9);
  EXPECT_EQ(w.count(), 20u);
}

// ---------------------------------------------------------------------------
// Test 10: Single true tick — count==1, rate==0.01, not yet filled
// ---------------------------------------------------------------------------
TEST(SlidingWindow15sTest, SingleTick_CountOne_RateSmall)
{
  SlidingWindow15s w;
  w.tick(true);
  EXPECT_EQ(w.count(), 1u);
  EXPECT_NEAR(w.rate(), 0.01, 1e-9);
  EXPECT_FALSE(w.is_filled());
}
