#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "m7_safety_supervisor/sotif/checker_veto_counter.hpp"
#include "l3_external_msgs/msg/checker_veto_notification.hpp"

using namespace mass_l3::m7::sotif;

namespace {

l3_external_msgs::msg::CheckerVetoNotification make_veto(
    std::string const& layer, std::uint8_t reason_class) {
  l3_external_msgs::msg::CheckerVetoNotification m;
  m.checker_layer = layer;
  m.veto_reason_class = reason_class;
  return m;
}

TEST(CheckerVetoCounterTest, EmptyCounterReturnsZeroRate) {
  CheckerVetoCounter c;
  EXPECT_FLOAT_EQ(c.current_rate(), 0.0F);
  EXPECT_EQ(c.window_violation_count(), 0);
}

TEST(CheckerVetoCounterTest, SingleL3VetoReturnsRateOne) {
  CheckerVetoCounter c;
  c.on_veto(make_veto("L3", 0));
  EXPECT_FLOAT_EQ(c.current_rate(), 1.0F);
  EXPECT_EQ(c.window_violation_count(), 1);
}

TEST(CheckerVetoCounterTest, IgnoresNonL3Layer) {
  CheckerVetoCounter c;
  c.on_veto(make_veto("L2", 0));
  c.on_veto(make_veto("L4", 1));
  c.on_veto(make_veto("L5", 2));
  EXPECT_FLOAT_EQ(c.current_rate(), 0.0F);
  EXPECT_EQ(c.window_violation_count(), 0);
}

TEST(CheckerVetoCounterTest, CountsByReasonClass) {
  CheckerVetoCounter c;
  c.on_veto(make_veto("L3", 0));
  c.on_veto(make_veto("L3", 0));
  c.on_veto(make_veto("L3", 1));
  c.on_veto(make_veto("L3", 3));
  auto counts = c.reason_counts();
  EXPECT_EQ(counts[0], 2);
  EXPECT_EQ(counts[1], 1);
  EXPECT_EQ(counts[2], 0);
  EXPECT_EQ(counts[3], 1);
  EXPECT_EQ(counts[4], 0);
  EXPECT_EQ(counts[5], 0);
}

TEST(CheckerVetoCounterTest, ResetClearsAll) {
  CheckerVetoCounter c;
  for (int i = 0; i < 50; ++i) { c.on_veto(make_veto("L3", 0)); }
  EXPECT_GT(c.window_violation_count(), 0);
  c.reset();
  EXPECT_FLOAT_EQ(c.current_rate(), 0.0F);
  EXPECT_EQ(c.window_violation_count(), 0);
  for (auto count : c.reason_counts()) { EXPECT_EQ(count, 0); }
}

TEST(CheckerVetoCounterTest, FullWindow100Events) {
  CheckerVetoCounter c;
  for (int i = 0; i < 100; ++i) { c.on_veto(make_veto("L3", 0)); }
  EXPECT_FLOAT_EQ(c.current_rate(), 1.0F);
  EXPECT_EQ(c.window_violation_count(), 100);
}

TEST(CheckerVetoCounterTest, MixedL3AndNonL3Events) {
  CheckerVetoCounter c;
  for (int i = 0; i < 50; ++i) { c.on_veto(make_veto("L3", 0)); }
  for (int i = 0; i < 50; ++i) { c.on_veto(make_veto("L2", 0)); }
  EXPECT_FLOAT_EQ(c.current_rate(), 0.5F);
  EXPECT_EQ(c.window_violation_count(), 50);
}

}  // namespace
