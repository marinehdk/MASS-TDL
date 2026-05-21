#include <chrono>
#include <gtest/gtest.h>

#include "m2_world_model/env_sanity_checker.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {
namespace {

EnvSanityChecker::Config default_env_config() {
  return {0.1, 15.0, 8.0, 10.0, 60.0, 5.0};
}

ZoneSnapshot make_snapshot(std::string zone_type, double current_kn,
                           std::chrono::steady_clock::time_point stamp) {
  ZoneSnapshot s;
  s.zone_type = std::move(zone_type);
  s.in_narrow_channel = false;
  s.current_speed_kn = current_kn;
  s.stamp = stamp;
  return s;
}

}  // namespace

TEST(EnvSanityCheckerTest, StalenessOver60sFails) {
  EnvSanityChecker checker(default_env_config());
  auto const now = std::chrono::steady_clock::now();
  auto const snapshot =
      make_snapshot("open_water", 5.0, now - std::chrono::seconds(120));

  auto const result = checker.validate(snapshot, now);
  EXPECT_FALSE(result.passed);
  EXPECT_DOUBLE_EQ(result.confidence_multiplier, 0.5);
}

TEST(EnvSanityCheckerTest, CurrentSpeedTooHighFails) {
  EnvSanityChecker checker(default_env_config());
  auto const snapshot =
      make_snapshot("open_water", 15.0, std::chrono::steady_clock::now());

  auto const result = checker.validate(snapshot, std::chrono::steady_clock::now());
  EXPECT_FALSE(result.passed);
  EXPECT_DOUBLE_EQ(result.confidence_multiplier, 0.8);
  EXPECT_DOUBLE_EQ(result.corrected_snapshot.current_speed_kn, 0.0);
}

TEST(EnvSanityCheckerTest, CurrentSpeedValid) {
  EnvSanityChecker checker(default_env_config());
  auto const snapshot =
      make_snapshot("open_water", 5.0, std::chrono::steady_clock::now());

  auto const result = checker.validate(snapshot, std::chrono::steady_clock::now());
  EXPECT_TRUE(result.passed);
  EXPECT_DOUBLE_EQ(result.confidence_multiplier, 1.0);
}

TEST(EnvSanityCheckerTest, IllegalZoneTransition) {
  EnvSanityChecker checker(default_env_config());
  auto const now = std::chrono::steady_clock::now();

  auto const snap1 = make_snapshot("open_water", 5.0, now);
  checker.validate(snap1, now);

  auto const snap2 = make_snapshot("port", 5.0, now);
  auto const result = checker.validate(snap2, now);
  EXPECT_FALSE(result.passed);
  EXPECT_DOUBLE_EQ(result.confidence_multiplier, 0.7);
}

TEST(EnvSanityCheckerTest, ValidZoneTransition) {
  EnvSanityChecker checker(default_env_config());
  auto const now = std::chrono::steady_clock::now();

  auto snap1 = make_snapshot("narrow_channel", 5.0, now);
  snap1.in_narrow_channel = true;
  checker.validate(snap1, now);

  auto snap2 = make_snapshot("port", 5.0, now);
  auto const result = checker.validate(snap2, now);
  EXPECT_TRUE(result.passed);
}

TEST(EnvSanityCheckerTest, FreshSnapshotPasses) {
  EnvSanityChecker checker(default_env_config());
  auto const snapshot =
      make_snapshot("open_water", 3.0, std::chrono::steady_clock::now());

  auto const result = checker.validate(snapshot, std::chrono::steady_clock::now());
  EXPECT_TRUE(result.passed);
}

TEST(EnvSanityCheckerTest, DegradationResultHasCorrectedSnapshot) {
  EnvSanityChecker checker(default_env_config());
  auto const snapshot =
      make_snapshot("open_water", 12.0, std::chrono::steady_clock::now());

  auto const result = checker.validate(snapshot, std::chrono::steady_clock::now());
  EXPECT_FALSE(result.passed);
  EXPECT_DOUBLE_EQ(result.corrected_snapshot.current_speed_kn, 0.0);
  EXPECT_DOUBLE_EQ(result.confidence_multiplier, 0.8);
}

}  // namespace mass_l3::m2
