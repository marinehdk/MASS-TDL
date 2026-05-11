#include <gtest/gtest.h>
#include <memory>

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_01_drift.hpp"
#include "m7_safety_supervisor/mrm/mrm_02_anchor.hpp"
#include "m7_safety_supervisor/mrm/mrm_03_heave_to.hpp"
#include "m7_safety_supervisor/mrm/mrm_04_mooring.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/tracked_target.hpp"

using namespace mass_l3::m7::mrm;

namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
l3_msgs::msg::ODDState build_odd(
    uint8_t zone = l3_msgs::msg::ODDState::ODD_ZONE_A,
    uint8_t health = l3_msgs::msg::ODDState::HEALTH_FULL)
{
  l3_msgs::msg::ODDState o;
  o.current_zone = zone;
  o.health = health;
  o.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  o.confidence = 1.0F;
  return o;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
l3_msgs::msg::WorldState build_world(double sog_kn = 5.0,
                                     float water_depth_m = 20.0F)
{
  l3_msgs::msg::WorldState w;
  w.own_ship.sog_kn = sog_kn;
  w.zone.min_water_depth_m = water_depth_m;
  w.confidence = 0.9F;
  return w;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
l3_msgs::msg::WorldState build_world_with_targets(
    double sog_kn,
    float depth,
    std::size_t n_targets,
    double cpa_m)
{
  auto w = build_world(sog_kn, depth);
  for (std::size_t i = 0U; i < n_targets; ++i) {
    l3_msgs::msg::TrackedTarget t;
    t.cpa_m = cpa_m;
    t.classification = "vessel";
    t.classification_confidence = 0.9F;
    w.targets.push_back(t);
  }
  return w;
}

}  // namespace

// ===========================================================================
// MRM-01 Drift tests
// ===========================================================================

TEST(Mrm01DriftTest, Applicable_WhenSpeedAboveTarget)
{
  // sog=5 kn > 4 kn target -> applicable
  Mrm01Drift mrm{Mrm01Params{}};
  auto odd = build_odd();
  auto world = build_world(5.0);
  EXPECT_TRUE(mrm.is_applicable(odd, world));
}

TEST(Mrm01DriftTest, NotApplicable_WhenAlreadyAtTargetSpeed)
{
  // sog=4.0 kn (not strictly >) -> not applicable
  Mrm01Drift mrm{Mrm01Params{}};
  auto odd = build_odd();
  auto world = build_world(4.0);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

TEST(Mrm01DriftTest, NotApplicable_WhenBelowTargetSpeed)
{
  // sog=2 kn < 4 kn target -> not applicable
  Mrm01Drift mrm{Mrm01Params{}};
  auto odd = build_odd();
  auto world = build_world(2.0);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

// ===========================================================================
// MRM-02 Anchor tests
// ===========================================================================

TEST(Mrm02AnchorTest, Applicable_WhenWaterDepthBelow30m_AndSpeedBelow4kn)
{
  // depth=20m <= 30m, sog=3 kn <= 4 kn -> applicable
  Mrm02Anchor mrm{Mrm02Params{}};
  auto odd = build_odd();
  auto world = build_world(3.0, 20.0F);
  EXPECT_TRUE(mrm.is_applicable(odd, world));
}

TEST(Mrm02AnchorTest, NotApplicable_WhenWaterDepthOver30m)
{
  // depth=35m > 30m -> not applicable
  Mrm02Anchor mrm{Mrm02Params{}};
  auto odd = build_odd();
  auto world = build_world(2.0, 35.0F);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

TEST(Mrm02AnchorTest, NotApplicable_WhenSpeedOver4kn)
{
  // depth=20m OK but sog=5 kn > 4 kn -> not applicable
  Mrm02Anchor mrm{Mrm02Params{}};
  auto odd = build_odd();
  auto world = build_world(5.0, 20.0F);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

// ===========================================================================
// MRM-03 HeaveTo tests
// ===========================================================================

TEST(Mrm03HeaveToTest, Applicable_WhenMultipleTargetsClose)
{
  // 2 targets with cpa_m=500m < 1852m -> applicable
  Mrm03HeaveTo mrm{Mrm03Params{}};
  auto world = build_world_with_targets(5.0, 20.0F, 2U, 500.0);
  EXPECT_TRUE(mrm.is_applicable(world));
}

TEST(Mrm03HeaveToTest, NotApplicable_WhenTargetsFar)
{
  // 2 targets with cpa_m=2000m > 1852m -> not applicable
  Mrm03HeaveTo mrm{Mrm03Params{}};
  auto world = build_world_with_targets(5.0, 20.0F, 2U, 2000.0);
  EXPECT_FALSE(mrm.is_applicable(world));
}

TEST(Mrm03HeaveToTest, NotApplicable_WhenOnlyOneTargetClose)
{
  // Only 1 target close, needs >= 2 -> not applicable
  Mrm03HeaveTo mrm{Mrm03Params{}};
  auto world = build_world_with_targets(5.0, 20.0F, 1U, 500.0);
  EXPECT_FALSE(mrm.is_applicable(world));
}

// ===========================================================================
// MRM-04 Mooring tests
// ===========================================================================

TEST(Mrm04MooringTest, Applicable_WhenInHarborZone)
{
  // ODD_ZONE_C + sog=1 kn <= 2 kn -> applicable
  Mrm04Mooring mrm{Mrm04Params{}};
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_C);
  auto world = build_world(1.0, 5.0F);
  EXPECT_TRUE(mrm.is_applicable(odd, world));
}

TEST(Mrm04MooringTest, NotApplicable_WhenInOpenWater)
{
  // ODD_ZONE_A (not harbor) + sog=1 kn -> not applicable
  Mrm04Mooring mrm{Mrm04Params{}};
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_A);
  auto world = build_world(1.0, 5.0F);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

TEST(Mrm04MooringTest, NotApplicable_WhenInHarborButSpeedTooHigh)
{
  // ODD_ZONE_C but sog=3.0 kn > 2.0 kn -> not applicable
  Mrm04Mooring mrm{Mrm04Params{}};
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_C);
  auto world = build_world(3.0, 5.0F);
  EXPECT_FALSE(mrm.is_applicable(odd, world));
}

// ===========================================================================
// MrmId to_string test
// ===========================================================================

TEST(MrmIdToStringTest, KnownIds_ReturnCorrectStrings)
{
  EXPECT_EQ(to_string(MrmId::kNone),          "NONE");
  EXPECT_EQ(to_string(MrmId::kMrm01_Drift),   "MRM-01-DRIFT");
  EXPECT_EQ(to_string(MrmId::kMrm02_Anchor),  "MRM-02-ANCHOR");
  EXPECT_EQ(to_string(MrmId::kMrm03_HeaveTo), "MRM-03-HEAVE-TO");
  EXPECT_EQ(to_string(MrmId::kMrm04_Mooring), "MRM-04-MOORING");
}

// ===========================================================================
// MrmCommandSetLoader::safe_default() test
// ===========================================================================

TEST(SafeDefaultTest, DefaultParams_AreInRange)
{
  auto cmd = MrmCommandSetLoader::safe_default();
  // MRM-01: target speed 4 kn, decel time 30s
  EXPECT_NEAR(cmd.mrm01.target_speed_kn, 4.0, 1e-6);
  EXPECT_NEAR(cmd.mrm01.max_decel_time_s, 30.0, 1e-6);
  // MRM-02: depth 30m, speed 4 kn
  EXPECT_NEAR(cmd.mrm02.max_water_depth_m, 30.0, 1e-6);
  EXPECT_NEAR(cmd.mrm02.max_speed_kn, 4.0, 1e-6);
  // MRM-03: turn angle 60 degrees, rot factor 0.8
  EXPECT_NEAR(cmd.mrm03.turn_angle_deg, 60.0, 1e-6);
  EXPECT_NEAR(cmd.mrm03.rot_factor, 0.8, 1e-6);
  // MRM-04: requires harbor, speed 2 kn
  EXPECT_TRUE(cmd.mrm04.requires_harbor_zone);
  EXPECT_NEAR(cmd.mrm04.max_speed_kn, 2.0, 1e-6);
}

TEST(SafeDefaultTest, LoadFromYaml_FallsBackToDefault)
{
  // Non-existent path -> falls back to safe_default()
  auto cmd = MrmCommandSetLoader::load_from_yaml("/nonexistent/path.yaml");
  auto def = MrmCommandSetLoader::safe_default();
  EXPECT_NEAR(cmd.mrm01.target_speed_kn, def.mrm01.target_speed_kn, 1e-6);
  EXPECT_NEAR(cmd.mrm02.max_water_depth_m, def.mrm02.max_water_depth_m, 1e-6);
}
