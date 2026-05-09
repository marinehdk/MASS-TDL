#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <l3_msgs/msg/world_state.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_external_msgs/msg/filtered_own_ship_state.hpp>
#include <l3_external_msgs/msg/tracked_target_array.hpp>
#include <l3_msgs/msg/tracked_target.hpp>
#include <l3_external_msgs/msg/environment_state.hpp>

#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/enc_loader.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/types.hpp"
#include "m2_world_model/view_health_monitor.hpp"
#include "m2_world_model/world_state_aggregator.hpp"

namespace mass_l3::m2 {
namespace {

using time_point = std::chrono::steady_clock::time_point;

// ── Test helpers ─────────────────────────────────────────────────────────

auto default_wsa_config() {
  return WorldStateAggregator::Config{
      256,                          // max_targets
      10,                           // target_disappearance_periods
      60.0,                         // environment_cache_ttl_s
      50.0,                         // timing_drift_warn_ms
      0.5,                          // confidence_floor_dv_degraded
      {100.0, 200.0, 50.0, 150.0}, // cpa_safe_m indexed by OddZone
      {60.0, 30.0, 15.0, 45.0},    // tcpa_safe_s indexed by OddZone
  };
}

auto make_own_ship_msg(double lat = 35.0, double lon = 139.0) {
  l3_external_msgs::msg::FilteredOwnShipState msg;
  msg.position.latitude = lat;
  msg.position.longitude = lon;
  msg.position.altitude = 0.0;
  msg.sog_kn = 10.0;
  msg.cog_deg = 45.0;
  msg.heading_deg = 40.0;
  msg.u_water = 5.0;
  msg.v_water = 0.0;
  msg.r_dot_deg_s = 0.0;
  msg.roll_deg = 0.0;
  msg.pitch_deg = 0.0;
  // covariance: 6x6 row-major identity
  msg.covariance.fill(0.0);
  msg.covariance[0] = 1.0;   // pos_xx
  msg.covariance[7] = 1.0;   // pos_yy
  msg.covariance[14] = 1.0;  // pos_zz
  msg.nav_mode = "OPTIMAL";
  msg.confidence = 1.0f;
  return msg;
}

auto make_odd_msg(uint8_t zone = 0) {
  l3_msgs::msg::ODDState msg;
  msg.current_zone = zone;
  msg.auto_level = 3;  // D3
  msg.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  msg.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  msg.conformance_score = 1.0f;
  msg.tmr_s = 120.0f;
  msg.tdl_s = 60.0f;
  msg.confidence = 1.0f;
  return msg;
}

auto make_target_array(const std::vector<uint64_t>& ids = {1001}) {
  l3_external_msgs::msg::TrackedTargetArray msg;
  for (const auto id : ids) {
    l3_msgs::msg::TrackedTarget tgt;
    tgt.target_id = id;
    tgt.position.latitude = 35.1;
    tgt.position.longitude = 139.1;
    tgt.position.altitude = 0.0;
    tgt.sog_kn = 12.0;
    tgt.cog_deg = 90.0;
    tgt.heading_deg = 85.0;
    tgt.classification = "cargo";
    tgt.classification_confidence = 0.9F;
    msg.targets.push_back(tgt);
  }
  msg.confidence = 1.0f;
  return msg;
}

// Aggregate components into a WSA shared_ptr for the tests.
// Exposes the health monitor so tests can inject failures.
struct TestFixture {
  std::shared_ptr<WorldStateAggregator> aggregator;
  std::shared_ptr<CpaTcpaCalculator> cpa_calc;
  std::shared_ptr<EncounterClassifier> classifier;
  std::shared_ptr<TrackBuffer> track_buffer;
  std::shared_ptr<EncLoader> enc_loader;
  std::shared_ptr<ViewHealthMonitor> health;
};

auto create_aggregator(const WorldStateAggregator::Config& cfg =
                           default_wsa_config()) {
  TestFixture tf;

  tf.cpa_calc = std::make_shared<CpaTcpaCalculator>(
      CpaTcpaCalculator::Config{
          CpaTcpaCalculator::UncertaintyMethod::Linear,
          100,     // monte_carlo_samples
          1.0,     // safety_factor
          1.0,     // odd_d_multiplier
          5.0,     // max_align_dt_s
          0.0,     // static_target_speed_mps
      });

  tf.classifier = std::make_shared<EncounterClassifier>(
      EncounterClassifier::Config{
          112.5,  // overtaking_bearing_min_deg
          247.5,  // overtaking_bearing_max_deg
          6.0,    // head_on_heading_diff_tol_deg
          0.1,    // safe_pass_speed_threshold_mps
          926.0,  // safe_pass_min_cpa_m
      });

  tf.track_buffer = std::make_shared<TrackBuffer>(
      TrackBuffer::Config{256, 10, 100.0});

  tf.enc_loader = std::make_shared<EncLoader>(
      EncLoader::Config{"", ""});  // not loaded — queries return nullopt

  tf.health = std::make_shared<ViewHealthMonitor>(
      ViewHealthMonitor::Config{
          2,      // dv_loss_periods_to_degraded
          5,      // dv_loss_periods_to_critical
          30.0,   // dv_degraded_to_critical_timeout_s
          100.0,  // ev_loss_ms_to_critical
          30.0,   // sv_loss_s_to_degraded
          0.5,    // confidence_floor_dv_degraded
      });

  tf.aggregator = std::make_shared<WorldStateAggregator>(
      cfg,
      tf.cpa_calc,
      tf.classifier,
      tf.track_buffer,
      tf.enc_loader,
      tf.health);

  return tf;
}

// ── Tests ────────────────────────────────────────────────────────────────

// Test 1: ComposeWithFullHealth — all views healthy, WorldState produced
TEST(WorldStateAggregatorTest, ComposeWithFullHealth) {
  auto tf = create_aggregator();

  // Send own-ship and ODD data
  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  const auto now = time_point{} + std::chrono::seconds(1);
  auto result = tf.aggregator->compose_world_state(now);

  ASSERT_TRUE(result.has_value());
  EXPECT_GE(result->confidence, 0.99f);  // all views healthy → ~1.0
  EXPECT_FALSE(result->rationale.empty());
}

// Test 2: EvCriticalReturnsNullopt — EV critical → no output
TEST(WorldStateAggregatorTest, EvCriticalReturnsNullopt) {
  auto tf = create_aggregator();

  // Send own-ship and ODD data first
  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  // Force EV critical by reporting large age directly to the health monitor
  const auto now = time_point{} + std::chrono::seconds(10);
  tf.health->report_ev_age(1.0, now);  // 1s ≫ 100 ms threshold → Critical

  auto result = tf.aggregator->compose_world_state(now);
  EXPECT_FALSE(result.has_value());
}

// Test 3: TracksIncludedInOutput — active targets appear in WorldState
TEST(WorldStateAggregatorTest, TracksIncludedInOutput) {
  auto tf = create_aggregator();

  // Send own-ship and ODD data
  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  // Send target data
  const auto t0 = time_point{};
  tf.track_buffer->update(make_target_array({1001, 1002}), t0);

  const auto now = t0 + std::chrono::seconds(1);
  auto result = tf.aggregator->compose_world_state(now);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->targets.size(), 2u);
  // Verify target IDs
  bool has_1001 = false, has_1002 = false;
  for (const auto& t : result->targets) {
    if (t.target_id == 1001) has_1001 = true;
    if (t.target_id == 1002) has_1002 = true;
  }
  EXPECT_TRUE(has_1001);
  EXPECT_TRUE(has_1002);
}

// Test 4: CPAThresholdsByODDZone — WorldState is produced for each zone
TEST(WorldStateAggregatorTest, CPAThresholdsByODDZone) {
  auto tf = create_aggregator();
  tf.aggregator->update_own_ship(make_own_ship_msg());

  const std::vector<uint8_t> zones = {
      l3_msgs::msg::ODDState::ODD_ZONE_A,
      l3_msgs::msg::ODDState::ODD_ZONE_B,
      l3_msgs::msg::ODDState::ODD_ZONE_C,
      l3_msgs::msg::ODDState::ODD_ZONE_D,
  };

  for (size_t i = 0; i < zones.size(); ++i) {
    tf.aggregator->update_odd_state(make_odd_msg(zones[i]));

    const auto now = time_point{} + std::chrono::seconds(static_cast<long>(i + 1));
    auto result = tf.aggregator->compose_world_state(now);

    ASSERT_TRUE(result.has_value())
        << "Failed for ODD zone index " << static_cast<int>(zones[i]);

    // Each zone should produce a valid world state with non-empty rationale
    EXPECT_FALSE(result->rationale.empty());
  }
}

// Test 5: ConfidenceFloorWhenDegraded — DV degraded → confidence floor applied
TEST(WorldStateAggregatorTest, ConfidenceFloorWhenDegraded) {
  auto tf = create_aggregator();

  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  // DV misses: cause degradation via health monitor directly
  const auto t0 = time_point{};
  tf.health->report_dv_update(false, t0);
  tf.health->report_dv_update(false, t0 + std::chrono::seconds(1));

  // Verify DV is degraded
  auto health_before = tf.health->aggregated_health();
  EXPECT_EQ(health_before.dv_health, ViewHealth::Degraded);

  const auto now = t0 + std::chrono::seconds(2);
  auto result = tf.aggregator->compose_world_state(now);

  ASSERT_TRUE(result.has_value());
  // Confidence should be at floor level (0.5) not full (1.0)
  EXPECT_FLOAT_EQ(result->confidence, 0.5f);
}

// Test 6: EmptyEnvironmentCache — no environment data → SV confidence degraded
TEST(WorldStateAggregatorTest, EmptyEnvironmentCache) {
  auto tf = create_aggregator();

  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  // Do NOT call update_environment — SV has never received data
  const auto now = time_point{} + std::chrono::seconds(1);
  auto result = tf.aggregator->compose_world_state(now);

  ASSERT_TRUE(result.has_value());

  // SV aged without update (age > 30s threshold)
  // compose_world_state should report SV age — after 1s, SV is still Full
  // (threshold is 30s). Confidence should be ~1.0.
  EXPECT_GE(result->confidence, 0.99f);

  // Verify that the zone is "unknown" since environment was never set
  EXPECT_EQ(result->zone.zone_type, "unknown");
}

// Test 7: MultipleOddZoneTransitions — switching ODD zones
TEST(WorldStateAggregatorTest, MultipleOddZoneTransitions) {
  auto tf = create_aggregator();
  tf.aggregator->update_own_ship(make_own_ship_msg());

  // Cycle through zones A→B→C→D→A
  const std::vector<uint8_t> zone_seq = {
      l3_msgs::msg::ODDState::ODD_ZONE_A,
      l3_msgs::msg::ODDState::ODD_ZONE_B,
      l3_msgs::msg::ODDState::ODD_ZONE_C,
      l3_msgs::msg::ODDState::ODD_ZONE_D,
      l3_msgs::msg::ODDState::ODD_ZONE_A,
  };

  for (size_t i = 0; i < zone_seq.size(); ++i) {
    tf.aggregator->update_odd_state(make_odd_msg(zone_seq[i]));

    const auto now = time_point{} +
                     std::chrono::seconds(static_cast<long>(i * 2 + 1));
    auto result = tf.aggregator->compose_world_state(now);

    ASSERT_TRUE(result.has_value())
        << "Failed at ODD zone transition index " << i;

    // Each transition should produce a valid world state
    EXPECT_FALSE(result->rationale.empty());
    EXPECT_GE(result->confidence, 0.0f);
  }
}

// Test 8: RationaleNotEmpty — rationale string populated
TEST(WorldStateAggregatorTest, RationaleNotEmpty) {
  auto tf = create_aggregator();

  tf.aggregator->update_own_ship(make_own_ship_msg());
  tf.aggregator->update_odd_state(make_odd_msg());

  // Add a target for richer rationale
  const auto t0 = time_point{};
  tf.track_buffer->update(make_target_array({42}), t0);

  const auto now = t0 + std::chrono::seconds(1);
  auto result = tf.aggregator->compose_world_state(now);

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->rationale.empty());

  // Rationale should contain key substrings
  EXPECT_NE(result->rationale.find("DV="), std::string::npos);
  EXPECT_NE(result->rationale.find("EV="), std::string::npos);
  EXPECT_NE(result->rationale.find("SV="), std::string::npos);
  EXPECT_NE(result->rationale.find("targets="), std::string::npos);
}

// Test 9: (Bonus) Own ship and zone snapshot accessors
TEST(WorldStateAggregatorTest, SnapshotAccessors) {
  auto tf = create_aggregator();

  // Before any update, accessors should return defaults
  auto empty_os = tf.aggregator->latest_own_ship();
  EXPECT_DOUBLE_EQ(empty_os.sog_kn, 0.0);

  auto empty_zone = tf.aggregator->latest_zone();
  EXPECT_EQ(empty_zone.zone_type, "unknown");

  // After update, they should reflect the cached data
  tf.aggregator->update_own_ship(make_own_ship_msg(35.5, 139.5));

  auto os = tf.aggregator->latest_own_ship();
  EXPECT_DOUBLE_EQ(os.sog_kn, 10.0);
  EXPECT_DOUBLE_EQ(os.latitude_deg, 35.5);
  EXPECT_DOUBLE_EQ(os.longitude_deg, 139.5);
}

}  // namespace
}  // namespace mass_l3::m2
