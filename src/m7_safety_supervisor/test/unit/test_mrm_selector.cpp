#include <gtest/gtest.h>
#include <chrono>
#include <memory>

#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/tracked_target.hpp"

using namespace mass_l3::m7::mrm;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static l3_msgs::msg::ODDState build_odd(
    uint8_t zone = l3_msgs::msg::ODDState::ODD_ZONE_A,
    uint8_t health = l3_msgs::msg::ODDState::HEALTH_FULL)
{
  l3_msgs::msg::ODDState o;
  o.current_zone = zone;
  o.health = health;
  o.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  o.confidence = 1.0f;
  return o;
}

static l3_msgs::msg::WorldState build_world(double sog_kn = 5.0,
                                             float water_depth_m = 20.0f)
{
  l3_msgs::msg::WorldState w;
  w.own_ship.sog_kn = sog_kn;
  w.zone.min_water_depth_m = water_depth_m;
  w.confidence = 0.9f;
  return w;
}

static l3_msgs::msg::WorldState build_world_with_targets(
    double sog_kn,
    float depth,
    std::size_t n_targets,
    double cpa_m)
{
  auto w = build_world(sog_kn, depth);
  for (std::size_t i = 0u; i < n_targets; ++i) {
    l3_msgs::msg::TrackedTarget t;
    t.cpa_m = cpa_m;
    t.classification = "vessel";
    t.classification_confidence = 0.9f;
    w.targets.push_back(t);
  }
  return w;
}

static ScenarioContext build_ctx(
    uint32_t violation_count = 0u,
    uint32_t critical_module_count = 0u,
    bool cpa_degrading = false,
    bool multiple_targets = false)
{
  ScenarioContext ctx{};
  ctx.assumption.total_violation_count = violation_count;
  ctx.watchdog.critical_count = critical_module_count;
  ctx.watchdog.any_critical = (critical_module_count > 0u);
  ctx.performance.cpa_trend_degrading = cpa_degrading;
  ctx.performance.multiple_targets_nearby = multiple_targets;
  return ctx;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class MrmSelectorTest : public ::testing::Test {
protected:
  void SetUp() override {
    MrmSelector::Config cfg;
    cfg.change_lockout = std::chrono::seconds{30};
    cfg.upgrade_confidence_threshold = 0.9F;
    auto cmd = MrmCommandSetLoader::safe_default();
    auto odd0 = build_odd();
    selector_ = std::make_unique<MrmSelector>(cfg, cmd, odd0);
    t0_ = std::chrono::steady_clock::now();
  }

  auto t(int offset_s) {
    return t0_ + std::chrono::seconds{offset_s};
  }

  std::unique_ptr<MrmSelector> selector_;
  std::chrono::steady_clock::time_point t0_;
};

// ===========================================================================
// Decision table tests
// ===========================================================================

TEST_F(MrmSelectorTest, SingleSotifViolation_SelectsMrm01)
{
  auto world = build_world(5.0, 20.0f);
  auto odd = build_odd();
  auto ctx = build_ctx(1u, 0u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm01_Drift);
}

TEST_F(MrmSelectorTest, MultipleSotifViolations_SelectsMrm02)
{
  // 2 SOTIF violations (not harbor, no other priority) -> MRM-02
  auto world = build_world(3.0, 20.0f);
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_A);
  auto ctx = build_ctx(2u, 0u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, OneTwoModulesDown_SelectsMrm01)
{
  // 1-2 watchdog critical modules -> MRM-01
  auto world = build_world(5.0, 20.0f);
  auto odd = build_odd();
  auto ctx = build_ctx(0u, 2u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm01_Drift);
}

TEST_F(MrmSelectorTest, ThreeModulesDown_SelectsMrm02)
{
  // >= 3 critical modules -> MRM-02
  auto world = build_world(3.0, 20.0f);
  auto odd = build_odd();
  auto ctx = build_ctx(0u, 3u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, CpaTrendDegrading_SelectsMrm02)
{
  // CPA trend degrading -> MRM-02
  auto world = build_world(3.0, 20.0f);
  auto odd = build_odd();
  auto ctx = build_ctx(0u, 0u, true, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, MultipleTargetsClose_SelectsMrm03)
{
  // Multiple targets nearby -> MRM-03
  auto world = build_world_with_targets(5.0, 20.0f, 2u, 500.0);
  auto odd = build_odd();
  auto ctx = build_ctx(0u, 0u, false, true);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm03_HeaveTo);
}

TEST_F(MrmSelectorTest, ExtremeScenario_3SimultaneousViolations_SelectsMrm02)
{
  // 3+ violations -> MRM-02 (priority 3 triggers before priority 6)
  auto world = build_world(3.0, 20.0f);
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_A);
  auto ctx = build_ctx(3u, 0u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, HarborZoneDegraded_SelectsMrm04)
{
  // Harbor zone (ODD_ZONE_C) + >= 2 violations -> MRM-04 (highest priority)
  auto world = build_world(1.0, 5.0f);
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_C);
  auto ctx = build_ctx(2u, 0u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kMrm04_Mooring);
}

// Counterpart to HarborZoneDegraded_SelectsMrm04: same violation count but
// outside harbor zone must select MRM-02, not MRM-04.
TEST_F(MrmSelectorTest, MultipleSotifViolations_OutsideHarbor_SelectsMrm02)
{
  auto ctx = build_ctx(2u, 0u, false, false);  // 2 violations
  auto odd = build_odd(l3_msgs::msg::ODDState::ODD_ZONE_A);  // open water
  auto world = build_world_with_targets(5.0, 20.0f, 0u, 9999.0);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, mass_l3::m7::mrm::MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, ChangeWithin30s_KeepsLastMrm)
{
  auto world = build_world_with_targets(3.0, 20.0f, 0u, 9999.0);
  auto odd = build_odd();

  // First: 1 violation -> MRM-01
  auto ctx1 = build_ctx(1u, 0u, false, false);
  auto dec1 = selector_->select(ctx1, odd, world, t(0));
  EXPECT_EQ(dec1.mrm_id, MrmId::kMrm01_Drift);

  // Then within 30s: 2 violations -> should still be MRM-01 (lockout)
  auto ctx2 = build_ctx(2u, 0u, false, false);
  auto dec2 = selector_->select(ctx2, odd, world, t(15));  // only 15s later
  EXPECT_EQ(dec2.mrm_id, MrmId::kMrm01_Drift);  // lockout keeps MRM-01
  // Confidence is degraded during lockout to signal "wanted to escalate but couldn't"
  EXPECT_LT(dec2.confidence, dec1.confidence);

  // After 30s: new selection takes effect
  auto dec3 = selector_->select(ctx2, odd, world, t(31));
  EXPECT_EQ(dec3.mrm_id, MrmId::kMrm02_Anchor);
}

TEST_F(MrmSelectorTest, NoViolations_SelectsNone)
{
  // Zero violations, zero critical modules -> kNone
  auto world = build_world(5.0, 20.0f);
  auto odd = build_odd();
  auto ctx = build_ctx(0u, 0u, false, false);
  auto dec = selector_->select(ctx, odd, world, t(0));
  EXPECT_EQ(dec.mrm_id, MrmId::kNone);
}

TEST_F(MrmSelectorTest, Reset_ClearsLockoutState)
{
  auto world = build_world(3.0, 20.0f);
  auto odd = build_odd();

  // Establish MRM-01
  auto ctx1 = build_ctx(1u, 0u, false, false);
  auto dec1 = selector_->select(ctx1, odd, world, t(0));
  EXPECT_EQ(dec1.mrm_id, MrmId::kMrm01_Drift);

  // Reset clears state
  selector_->reset();

  // After reset: 2 violations should immediately select MRM-02 (no lockout)
  auto ctx2 = build_ctx(2u, 0u, false, false);
  auto dec2 = selector_->select(ctx2, odd, world, t(5));
  EXPECT_EQ(dec2.mrm_id, MrmId::kMrm02_Anchor);
}
