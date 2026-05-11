#include <gtest/gtest.h>
#include <chrono>
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m2_world_model/world_state_aggregator.hpp"
#include "m2_world_model/view_health_monitor.hpp"
#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/enc_loader.hpp"

using namespace mass_l3::m2;
using namespace std::chrono_literals;

// ── Test fixtures ────────────────────────────────────────────────────────────

static ViewHealthMonitor::Config default_health_config() {
  ViewHealthMonitor::Config cfg{};
  cfg.dv_loss_periods_to_degraded = 2;
  cfg.dv_loss_periods_to_critical = 5;
  cfg.dv_degraded_to_critical_timeout_s = 30.0;
  cfg.ev_loss_ms_to_critical = 100.0;
  cfg.sv_loss_s_to_degraded = 30.0;
  cfg.confidence_floor_dv_degraded = 0.5;
  return cfg;
}

static l3_external_msgs::msg::FilteredOwnShipState make_own_ship_msg() {
  l3_external_msgs::msg::FilteredOwnShipState msg{};
  msg.position.latitude = 30.0;
  msg.position.longitude = 121.0;
  msg.sog_kn = 10.0;
  msg.cog_deg = 0.0;
  msg.heading_deg = 0.0;
  msg.u_water = 5.14;
  msg.v_water = 0.0;
  return msg;
}

static l3_external_msgs::msg::EnvironmentState make_env_state(
    const std::string& zone_type = "open_water") {
  l3_external_msgs::msg::EnvironmentState msg{};
  msg.current_speed_kn = 0.5;
  msg.current_direction_deg = 45.0;
  (void)zone_type;  // zone comes from ENC; EnvironmentState carries current data
  return msg;
}

static std::shared_ptr<WorldStateAggregator> make_test_aggregator(
    std::shared_ptr<ViewHealthMonitor> health = nullptr) {
  if (!health) {
    health = std::make_shared<ViewHealthMonitor>(default_health_config());
  }

  CpaTcpaCalculator::Config cpa_cfg{};
  cpa_cfg.method = CpaTcpaCalculator::UncertaintyMethod::Linear;
  cpa_cfg.monte_carlo_samples = 100;
  cpa_cfg.safety_factor = 1.0;
  cpa_cfg.odd_d_multiplier = 1.5;
  cpa_cfg.max_align_dt_s = 1.0;
  cpa_cfg.static_target_speed_mps = 0.0;
  auto cpa_calc = std::make_shared<CpaTcpaCalculator>(cpa_cfg);

  EncounterClassifier::Config enc_cfg{};
  enc_cfg.overtaking_bearing_min_deg = 112.5;
  enc_cfg.overtaking_bearing_max_deg = 247.5;
  enc_cfg.head_on_heading_diff_tol_deg = 6.0;
  enc_cfg.safe_pass_speed_threshold_mps = 0.5;
  enc_cfg.safe_pass_min_cpa_m = 926.0;
  auto classifier = std::make_shared<EncounterClassifier>(enc_cfg);

  TrackBuffer::Config tb_cfg{};
  tb_cfg.max_targets = 256;
  tb_cfg.disappearance_periods = 10;
  tb_cfg.max_target_age_s = 5.0;
  tb_cfg.position_default_sigma_m = 50.0;
  auto track_buffer = std::make_shared<TrackBuffer>(tb_cfg);

  EncLoader::Config el_cfg{};
  el_cfg.enc_data_root = "";
  el_cfg.enc_metadata_file = "";
  auto enc_loader = std::make_shared<EncLoader>(el_cfg);

  WorldStateAggregator::Config agg_cfg{};
  agg_cfg.max_targets = 256;
  agg_cfg.target_disappearance_periods = 10;
  agg_cfg.environment_cache_ttl_s = 60.0;
  agg_cfg.timing_drift_warn_ms = 50.0;
  agg_cfg.confidence_floor_dv_degraded = 0.5;
  agg_cfg.cpa_safe_m = {1852.0, 555.6, 277.8, 2778.0};
  agg_cfg.tcpa_safe_s = {720.0, 240.0, 180.0, 600.0};

  return std::make_shared<WorldStateAggregator>(
      agg_cfg, cpa_calc, classifier, track_buffer, enc_loader, health);
}

// ── Tests ────────────────────────────────────────────────────────────────────

class EnvironmentDegradedTest : public ::testing::Test {
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }
};

// Per detailed design §7.1.3 (SV degradation): no env update for >30 s → SV DEGRADED.
TEST_F(EnvironmentDegradedTest, SvDegradesAfter30sOfNoEnvUpdate) {
  auto health = std::make_shared<ViewHealthMonitor>(default_health_config());
  const auto t0 = std::chrono::steady_clock::now();

  // Report a fresh SV age at t0.
  health->report_sv_age(0.0, t0);
  EXPECT_EQ(health->aggregated_health().sv_health, ViewHealth::Full);

  // 31 s later with no further update → report age=31 s → SV DEGRADED.
  health->report_sv_age(31.0, t0 + 31s);
  EXPECT_EQ(health->aggregated_health().sv_health, ViewHealth::Degraded);
}

// Aggregator should still produce world state using cached zone after SV loss.
TEST_F(EnvironmentDegradedTest, AggregatorUsesCachedZoneAfterEnvLoss) {
  auto health = std::make_shared<ViewHealthMonitor>(default_health_config());
  auto agg = make_test_aggregator(health);
  const auto t0 = std::chrono::steady_clock::now();

  // Prime the own-ship cache (required for compose to succeed).
  agg->update_own_ship(make_own_ship_msg());
  health->report_ev_age(0.0, t0);

  // Prime the environment cache once.
  agg->update_environment(make_env_state("open_water"));
  health->report_sv_age(0.0, t0);

  // 45 s later, SV is degraded but cache still valid within ttl (60 s).
  health->report_sv_age(45.0, t0 + 45s);
  const auto state = agg->compose_world_state(t0 + 45s);

  // World state should still be produced (EV is healthy).
  ASSERT_TRUE(state.has_value());
  // Aggregated confidence pulled down because SV is degraded.
  EXPECT_LT(state->confidence, 0.8f);
}

// Per detailed design §7.1.2 (EV CRITICAL): no own-ship update → nullopt.
TEST_F(EnvironmentDegradedTest, EvCriticalForcesNullopt) {
  auto health = std::make_shared<ViewHealthMonitor>(default_health_config());
  auto agg = make_test_aggregator(health);
  const auto t0 = std::chrono::steady_clock::now();

  // No own_ship update at all, EV not reported → stays at initial health.
  // Force EV CRITICAL by reporting a large age gap.
  health->report_ev_age(1000.0, t0);

  const auto state = agg->compose_world_state(t0);
  EXPECT_FALSE(state.has_value());
}
