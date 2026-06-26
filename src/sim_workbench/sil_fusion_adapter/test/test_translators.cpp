// Unit tests for sil_fusion_adapter translators (Track A A5b).
// Pure field-map verification — no ROS spinning.
#include <gtest/gtest.h>

#include "sil_fusion_adapter/translators.hpp"

#include <cmath>

using sil_fusion_adapter::environment_sil_to_l3;
using sil_fusion_adapter::kRadPerDeg;
using sil_fusion_adapter::target_vessel_to_tracked_array;

namespace {
void fill_stamp(builtin_interfaces::msg::Time& t, uint32_t sec) {
  t.sec = sec;
  t.nanosec = 0u;
}
}  // namespace

// ── TargetVesselState -> TrackedTargetArray ─────────────────────────────────

TEST(TargetVesselToTrackedArray, SingleTargetBecomesOneElementArray) {
  sil_msgs::msg::TargetVesselState sil;
  fill_stamp(sil.stamp, 7u);
  sil.mmsi = 123456;
  auto out = target_vessel_to_tracked_array(sil);
  ASSERT_EQ(out.targets.size(), 1u);
}

TEST(TargetVesselToTrackedArray, SchemaAndStampPropagated) {
  sil_msgs::msg::TargetVesselState sil;
  fill_stamp(sil.stamp, 42u);
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_EQ(out.schema_version, sil_fusion_adapter::kSchemaV112);
  EXPECT_EQ(out.stamp.sec, 42u);
  EXPECT_EQ(out.targets[0].schema_version, sil_fusion_adapter::kSchemaV112);
  EXPECT_EQ(out.targets[0].stamp.sec, 42u);
}

TEST(TargetVesselToTrackedArray, MmsiBecomesTargetId) {
  sil_msgs::msg::TargetVesselState sil;
  sil.mmsi = 987654;
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_EQ(out.targets[0].target_id, 987654u);
}

TEST(TargetVesselToTrackedArray, PositionLatLonAltPropagated) {
  sil_msgs::msg::TargetVesselState sil;
  sil.lat = 63.0;
  sil.lon = 10.0;
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_DOUBLE_EQ(out.targets[0].position.latitude, 63.0);
  EXPECT_DOUBLE_EQ(out.targets[0].position.longitude, 10.0);
  EXPECT_DOUBLE_EQ(out.targets[0].position.altitude, 0.0);
}

TEST(TargetVesselToTrackedArray, HeadingRadiansConvertedToDegrees) {
  // SIL msg carries heading in RADIANS (target_vessel_node publishes radians).
  // L3 TrackedTarget carries heading in DEGREES.
  sil_msgs::msg::TargetVesselState sil;
  sil.heading = 180.0 * kRadPerDeg;  // 180 deg in radians
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_NEAR(out.targets[0].heading_deg, 180.0, 1e-6);
}

TEST(TargetVesselToTrackedArray, CourseRadiansConvertedToDegrees) {
  sil_msgs::msg::TargetVesselState sil;
  sil.cog = 90.0 * kRadPerDeg;  // 90 deg in radians
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_NEAR(out.targets[0].cog_deg, 90.0, 1e-6);
}

TEST(TargetVesselToTrackedArray, SogMpsConvertedToKn) {
  sil_msgs::msg::TargetVesselState sil;
  sil.sog = 5.14444;  // ~10 kn
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_NEAR(out.targets[0].sog_kn, 10.0, 1e-3);
}

TEST(TargetVesselToTrackedArray, CovarianceIsIdentity3x3) {
  sil_msgs::msg::TargetVesselState sil;
  auto out = target_vessel_to_tracked_array(sil);
  const auto& cov = out.targets[0].covariance;
  ASSERT_EQ(cov.size(), 9u);
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      EXPECT_DOUBLE_EQ(cov[i * 3 + j], (i == j) ? 1.0 : 0.0);
    }
  }
}

TEST(TargetVesselToTrackedArray, ClassificationAndConfidenceFixed) {
  sil_msgs::msg::TargetVesselState sil;
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_EQ(out.targets[0].classification, "vessel");
  EXPECT_FLOAT_EQ(out.targets[0].classification_confidence, 0.85f);
  EXPECT_FLOAT_EQ(out.targets[0].confidence, 0.85f);
  EXPECT_EQ(out.targets[0].source_sensor, "fused");
  // CPA/TCPA are zeroed (M2 owns CPA/TCPA computation downstream).
  EXPECT_DOUBLE_EQ(out.targets[0].cpa_m, 0.0);
  EXPECT_DOUBLE_EQ(out.targets[0].tcpa_s, 0.0);
}

TEST(TargetVesselToTrackedArray, ArrayConfidenceAndRationale) {
  sil_msgs::msg::TargetVesselState sil;
  auto out = target_vessel_to_tracked_array(sil);
  EXPECT_FLOAT_EQ(out.confidence, 0.85f);
}

// ── EnvironmentState (SIL) -> EnvironmentState (L3) ─────────────────────────

TEST(EnvironmentSilToL3, SchemaAndStampPropagated) {
  sil_msgs::msg::EnvironmentState sil;
  fill_stamp(sil.stamp, 99u);
  auto out = environment_sil_to_l3(sil);
  EXPECT_EQ(out.schema_version, sil_fusion_adapter::kSchemaV112);
  EXPECT_EQ(out.stamp.sec, 99u);
}

TEST(EnvironmentSilToL3, WindMpsConvertedToKn) {
  sil_msgs::msg::EnvironmentState sil;
  sil.wind_speed_mps = 5.14444;  // ~10 kn
  auto out = environment_sil_to_l3(sil);
  EXPECT_NEAR(out.wind_speed_kn, 10.0, 1e-3);
}

TEST(EnvironmentSilToL3, WindDirectionPassthrough) {
  sil_msgs::msg::EnvironmentState sil;
  sil.wind_direction = 270.0;
  auto out = environment_sil_to_l3(sil);
  EXPECT_DOUBLE_EQ(out.wind_direction_deg, 270.0);
}

TEST(EnvironmentSilToL3, CurrentMpsConvertedToKn) {
  sil_msgs::msg::EnvironmentState sil;
  sil.current_speed_mps = 2.57222;  // ~5 kn
  auto out = environment_sil_to_l3(sil);
  EXPECT_NEAR(out.current_speed_kn, 5.0, 1e-3);
}

TEST(EnvironmentSilToL3, CurrentDirectionPassthrough) {
  sil_msgs::msg::EnvironmentState sil;
  sil.current_direction = 45.0;
  auto out = environment_sil_to_l3(sil);
  EXPECT_DOUBLE_EQ(out.current_direction_deg, 45.0);
}

TEST(EnvironmentSilToL3, VisibilityPassthrough) {
  sil_msgs::msg::EnvironmentState sil;
  sil.visibility_nm = 5.0;
  auto out = environment_sil_to_l3(sil);
  EXPECT_DOUBLE_EQ(out.visibility_range_nm, 5.0);
}

TEST(EnvironmentSilToL3, WaveFieldsZeroedSILHasNoWaveData) {
  sil_msgs::msg::EnvironmentState sil;
  auto out = environment_sil_to_l3(sil);
  EXPECT_DOUBLE_EQ(out.wave_height_m, 0.0);
  EXPECT_DOUBLE_EQ(out.wave_direction_deg, 0.0);
  EXPECT_EQ(out.weather_source, "sensor");
  EXPECT_FLOAT_EQ(out.confidence, 0.9f);
}
