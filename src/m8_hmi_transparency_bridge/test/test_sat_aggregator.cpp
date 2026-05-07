#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

using mass_l3::m8::SatAggregator;
using SrcMod = SatAggregator::SourceModule;

namespace {

l3_msgs::msg::SATData make_sat_msg(const std::string& src,
                                   const std::string& summary)
{
  l3_msgs::msg::SATData m{};
  m.source_module = src;
  m.sat1.state_summary = summary;
  return m;
}

l3_msgs::msg::SATData make_full_msg(const std::string& src,
                                    const std::string& summary,
                                    const std::string& trigger_reason,
                                    float system_confidence,
                                    const std::string& predicted_state,
                                    float prediction_uncertainty)
{
  l3_msgs::msg::SATData m{};
  m.source_module = src;
  m.sat1.state_summary = summary;
  m.sat2.trigger_reason = trigger_reason;
  m.sat2.system_confidence = system_confidence;
  m.sat3.predicted_state = predicted_state;
  m.sat3.prediction_uncertainty = prediction_uncertainty;
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: ingest single M1 message — latest_sat1 returns populated value,
//         other modules return nullopt.
// ---------------------------------------------------------------------------
TEST(SatAggregator, IngestSingleM1Msg_LatestSat1Returned)
{
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M1", "TRANSIT @ D3, ODD-A, 18 kn"), now);
  auto sat1 = agg.latest_sat1(SrcMod::kM1);
  ASSERT_TRUE(sat1.has_value());
  EXPECT_EQ(sat1->state_summary, "TRANSIT @ D3, ODD-A, 18 kn");
  EXPECT_FALSE(agg.latest_sat1(SrcMod::kM7).has_value());
}

// ---------------------------------------------------------------------------
// Test 2: module with no data — age returns infinity.
// ---------------------------------------------------------------------------
TEST(SatAggregator, MissingModule_AgeReturnsInfinity)
{
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  EXPECT_GT(agg.age_seconds(SrcMod::kM7, now), 1e9);
}

// ---------------------------------------------------------------------------
// Test 3: stale detection uses threshold correctly.
// ---------------------------------------------------------------------------
TEST(SatAggregator, StaleDetectionUsesThreshold)
{
  SatAggregator agg;
  auto t0 = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M2", "x"), t0);
  auto t1 = t0 + std::chrono::milliseconds(1500);
  EXPECT_TRUE(agg.is_stale(SrcMod::kM2, t1, 1.0));
  EXPECT_FALSE(agg.is_stale(SrcMod::kM2, t1, 2.0));
}

// ---------------------------------------------------------------------------
// Test 4: to_string / from_string round-trip for all known modules.
// ---------------------------------------------------------------------------
TEST(SatAggregator, SourceModuleNameRoundTrip)
{
  EXPECT_EQ(SatAggregator::to_string(SrcMod::kM7), "M7");
  EXPECT_EQ(SatAggregator::from_string("M4").value(), SrcMod::kM4);
  EXPECT_FALSE(SatAggregator::from_string("Mx").has_value());
}

// ---------------------------------------------------------------------------
// Test 5: ingesting M7 msg updates age correctly (recent = not stale).
// ---------------------------------------------------------------------------
TEST(SatAggregator, IngestM7UpdatesAge)
{
  SatAggregator agg;
  auto t0 = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M7", "SAFE"), t0);
  auto t1 = t0 + std::chrono::milliseconds(100);
  double age = agg.age_seconds(SrcMod::kM7, t1);
  EXPECT_GT(age, 0.0);
  EXPECT_LT(age, 1.0);
  EXPECT_FALSE(agg.is_stale(SrcMod::kM7, t1, 2.0));
}

// ---------------------------------------------------------------------------
// Test 6: sat2 and sat3 fields round-trip through ingest.
// ---------------------------------------------------------------------------
TEST(SatAggregator, Sat2AndSat3FieldsRoundTrip)
{
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  agg.ingest(make_full_msg("M6", "COLREGS-OK",
                            "CPA_CLOSE", 0.85F,
                            "COMPLY_RULE8", 0.15F),
             now);

  auto sat2 = agg.latest_sat2(SrcMod::kM6);
  ASSERT_TRUE(sat2.has_value());
  EXPECT_EQ(sat2->trigger_reason, "CPA_CLOSE");
  EXPECT_FLOAT_EQ(sat2->system_confidence, 0.85F);

  auto sat3 = agg.latest_sat3(SrcMod::kM6);
  ASSERT_TRUE(sat3.has_value());
  EXPECT_EQ(sat3->predicted_state, "COMPLY_RULE8");
  EXPECT_FLOAT_EQ(sat3->prediction_uncertainty, 0.15F);
}

// ---------------------------------------------------------------------------
// Test 7: multiple modules are independent — ingest M1 does not affect M7.
// ---------------------------------------------------------------------------
TEST(SatAggregator, MultipleModulesAreIndependent)
{
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M1", "M1-state"), now);

  EXPECT_TRUE(agg.latest_sat1(SrcMod::kM1).has_value());
  EXPECT_FALSE(agg.latest_sat1(SrcMod::kM7).has_value());
  EXPECT_FALSE(agg.latest_sat2(SrcMod::kM7).has_value());
  EXPECT_FALSE(agg.latest_sat3(SrcMod::kM7).has_value());
  EXPECT_GT(agg.age_seconds(SrcMod::kM7, now), 1e9);
}

// ---------------------------------------------------------------------------
// Test 8: second ingest for the same module overwrites previous data.
// ---------------------------------------------------------------------------
TEST(SatAggregator, SecondIngestForSameModuleOverwritesData)
{
  SatAggregator agg;
  auto t0 = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M4", "first"), t0);
  auto t1 = t0 + std::chrono::milliseconds(200);
  agg.ingest(make_sat_msg("M4", "second"), t1);

  auto sat1 = agg.latest_sat1(SrcMod::kM4);
  ASSERT_TRUE(sat1.has_value());
  EXPECT_EQ(sat1->state_summary, "second");

  // age is measured from t1, not t0
  auto t2 = t1 + std::chrono::milliseconds(50);
  double age = agg.age_seconds(SrcMod::kM4, t2);
  EXPECT_LT(age, 0.1);
}

// ---------------------------------------------------------------------------
// Test 9: unknown source_module string is silently ignored.
// ---------------------------------------------------------------------------
TEST(SatAggregator, UnknownSourceModuleIsIgnored)
{
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  l3_msgs::msg::SATData msg{};
  msg.source_module = "UNKNOWN_MODULE";
  msg.sat1.state_summary = "should not appear";
  agg.ingest(msg, now);

  // None of the known modules should have data
  for (auto src : {SrcMod::kM1, SrcMod::kM2, SrcMod::kM4,
                   SrcMod::kM6, SrcMod::kM7}) {
    EXPECT_FALSE(agg.latest_sat1(src).has_value());
  }
}

// ---------------------------------------------------------------------------
// Test 10: from_string accepts all five valid module names.
// ---------------------------------------------------------------------------
TEST(SatAggregator, FromStringAcceptsAllValidModules)
{
  EXPECT_EQ(SatAggregator::from_string("M1").value(), SrcMod::kM1);
  EXPECT_EQ(SatAggregator::from_string("M2").value(), SrcMod::kM2);
  EXPECT_EQ(SatAggregator::from_string("M4").value(), SrcMod::kM4);
  EXPECT_EQ(SatAggregator::from_string("M6").value(), SrcMod::kM6);
  EXPECT_EQ(SatAggregator::from_string("M7").value(), SrcMod::kM7);
  EXPECT_FALSE(SatAggregator::from_string("M3").has_value());
  EXPECT_FALSE(SatAggregator::from_string("").has_value());
}
