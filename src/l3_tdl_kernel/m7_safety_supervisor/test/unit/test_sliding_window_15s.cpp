#include <gtest/gtest.h>
#include <cstdint>

#include "m7_safety_supervisor/sotif/sliding_window_15s.hpp"

using namespace mass_l3::m7::sotif;

namespace {

TEST(SlidingWindow15sTest, EmptyWindowReturnsZeroRate) {
  SlidingWindow15s w;
  EXPECT_FLOAT_EQ(w.rate(), 0.0F);
  EXPECT_EQ(w.violation_count(), 0);
}

TEST(SlidingWindow15sTest, SingleTrueGivesRateOne) {
  SlidingWindow15s w;
  w.push(true);
  EXPECT_FLOAT_EQ(w.rate(), 1.0F);
  EXPECT_EQ(w.violation_count(), 1);
}

TEST(SlidingWindow15sTest, SingleFalseGivesRateZero) {
  SlidingWindow15s w;
  w.push(false);
  EXPECT_FLOAT_EQ(w.rate(), 0.0F);
  EXPECT_EQ(w.violation_count(), 0);
}

TEST(SlidingWindow15sTest, FiftyFiftySplit) {
  SlidingWindow15s w;
  for (int i = 0; i < 100; ++i) {
    w.push(i < 50);
  }
  EXPECT_FLOAT_EQ(w.rate(), 0.5F);
  EXPECT_EQ(w.violation_count(), 50);
}

TEST(SlidingWindow15sTest, FullWindowAllTrue) {
  SlidingWindow15s w;
  for (int i = 0; i < 100; ++i) {
    w.push(true);
  }
  EXPECT_FLOAT_EQ(w.rate(), 1.0F);
  EXPECT_EQ(w.violation_count(), 100);
}

TEST(SlidingWindow15sTest, FullWindowAllFalse) {
  SlidingWindow15s w;
  for (int i = 0; i < 100; ++i) {
    w.push(false);
  }
  EXPECT_FLOAT_EQ(w.rate(), 0.0F);
  EXPECT_EQ(w.violation_count(), 0);
}

TEST(SlidingWindow15sTest, FifoEvictsOldest) {
  SlidingWindow15s w;
  // Push 100 true
  for (int i = 0; i < 100; ++i) { w.push(true); }
  EXPECT_FLOAT_EQ(w.rate(), 1.0F);
  // Push 50 false — oldest 50 true get evicted
  for (int i = 0; i < 50; ++i) { w.push(false); }
  EXPECT_FLOAT_EQ(w.rate(), 0.5F);
  EXPECT_EQ(w.violation_count(), 50);
  // Push 50 more false — all true evicted
  for (int i = 0; i < 50; ++i) { w.push(false); }
  EXPECT_FLOAT_EQ(w.rate(), 0.0F);
  EXPECT_EQ(w.violation_count(), 0);
}

TEST(SlidingWindow15sTest, ResetClearsAllState) {
  SlidingWindow15s w;
  for (int i = 0; i < 50; ++i) { w.push(true); }
  EXPECT_GT(w.violation_count(), 0);
  w.reset();
  EXPECT_FLOAT_EQ(w.rate(), 0.0F);
  EXPECT_EQ(w.violation_count(), 0);
}

TEST(SlidingWindow15sTest, ResetThenPushWorks) {
  SlidingWindow15s w;
  for (int i = 0; i < 80; ++i) { w.push(true); }
  w.reset();
  w.push(true);
  EXPECT_FLOAT_EQ(w.rate(), 1.0F);
  EXPECT_EQ(w.violation_count(), 1);
}

TEST(SlidingWindow15sTest, WindowSizeNeverExceeds100) {
  SlidingWindow15s w;
  for (int i = 0; i < 200; ++i) { w.push(true); }
  EXPECT_EQ(w.violation_count(), 100);
}

TEST(SlidingWindow15sTest, AlternatingPattern) {
  SlidingWindow15s w;
  for (int i = 0; i < 100; ++i) { w.push(i % 2 == 0); }
  EXPECT_FLOAT_EQ(w.rate(), 0.5F);
  EXPECT_EQ(w.violation_count(), 50);
}

TEST(SlidingWindow15sTest, Exactly20PercentThreshold) {
  SlidingWindow15s w;
  for (int i = 0; i < 100; ++i) { w.push(i < 20); }
  EXPECT_FLOAT_EQ(w.rate(), 0.2F);
  EXPECT_EQ(w.violation_count(), 20);
}

}  // namespace
