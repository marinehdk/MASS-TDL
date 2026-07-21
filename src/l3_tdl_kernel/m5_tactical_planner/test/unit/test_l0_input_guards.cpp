#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>

#include "m5_tactical_planner/common/types.hpp"

// ===========================================================================
// T-L0-1 through T-L0-4: InputDegradation struct unit tests
// ===========================================================================

// T-L0-1: own_ship.psi_rad NaN guard — verify InputDegradation flag + summary.
TEST(L0InputDegradationTest, OwnPsiDegradedFlagAndSummary) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  EXPECT_FALSE(deg.any());
  EXPECT_TRUE(deg.summary().empty());

  deg.own_psi_degraded = true;
  EXPECT_TRUE(deg.any());
  EXPECT_NE(deg.summary().find("own_psi"), std::string::npos);

  deg.reset();
  EXPECT_FALSE(deg.any());
  EXPECT_TRUE(deg.summary().empty());
}

// T-L0-2: speed_max <= 0 guard — speed_box_degraded flag.
TEST(L0InputDegradationTest, SpeedBoxDegradedFlag) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  deg.speed_box_degraded = true;
  EXPECT_TRUE(deg.any());
  EXPECT_NE(deg.summary().find("speed_box"), std::string::npos);
  EXPECT_EQ(deg.summary().find("own_psi"), std::string::npos);
}

// T-L0-3: speed_max < speed_min guard — speed_box_degraded via validate_speed_box.
TEST(L0SpeedBoxValidationTest, MaxLessThanMinDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // speed_min=10 kn, speed_max=5 kn → max < min → degraded.
  const auto box = mass_l3::m5::validate_speed_box(
      10.0, 5.0, 10.0, "", deg);
  EXPECT_TRUE(deg.speed_box_degraded);
  EXPECT_DOUBLE_EQ(box.first, 0.0);
  EXPECT_DOUBLE_EQ(box.second, mass_l3::m5::units::kMsPerKn * 10.0);
}

// T-L0-4: earliest_min_alt_k out of range — REACHABILITY_DEGRADED via helper.
TEST(L0EarliestKValidationTest, OutOfRangeDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // N=80, earliest_k=100 > 80 → out of range → degraded + sentinel 0.
  const double result = mass_l3::m5::validate_earliest_min_alt_k(100.0, 80, deg);
  EXPECT_TRUE(deg.reachability_degraded);
  EXPECT_DOUBLE_EQ(result, 0.0);
}

// T-L0-4b: earliest_min_alt_k negative → degraded.
TEST(L0EarliestKValidationTest, NegativeDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double result = mass_l3::m5::validate_earliest_min_alt_k(-5.0, 80, deg);
  EXPECT_TRUE(deg.reachability_degraded);
  EXPECT_DOUBLE_EQ(result, 0.0);
}

// T-L0-4c: earliest_min_alt_k NaN → degraded.
TEST(L0EarliestKValidationTest, NanDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double result = mass_l3::m5::validate_earliest_min_alt_k(
      std::numeric_limits<double>::quiet_NaN(), 80, deg);
  EXPECT_TRUE(deg.reachability_degraded);
  EXPECT_DOUBLE_EQ(result, 0.0);
}

// T-L0-4d: earliest_min_alt_k valid in-range value → passes through.
TEST(L0EarliestKValidationTest, ValidInRangePasses) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double result = mass_l3::m5::validate_earliest_min_alt_k(42.0, 80, deg);
  EXPECT_FALSE(deg.reachability_degraded);
  EXPECT_DOUBLE_EQ(result, 42.0);
}

// ===========================================================================
// T-L0-5: Speed box validation (related to box-reach sanity)
// Note: The L0-B box-reach sanity check (pref_dir vs box_reach > 0) is
// deeply tied to assemble_input_() private method and M6 COLREGs state.
// It requires M6 msg wiring and cannot be unit-tested without a SIL
// environment. The check is verified by the warn log in the L0-B section.
// ===========================================================================

// T-L0-5a: validate_speed_box with valid inputs — no degradation.
TEST(L0SpeedBoxValidationTest, ValidInputsNoDegradation) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // speed_min=5 kn, speed_max=15 kn, nominal=10 kn → valid.
  const auto box = mass_l3::m5::validate_speed_box(5.0, 15.0, 10.0, "", deg);
  EXPECT_FALSE(deg.speed_box_degraded);
  EXPECT_DOUBLE_EQ(box.first, 5.0 * mass_l3::m5::units::kMsPerKn);
  EXPECT_DOUBLE_EQ(box.second, 15.0 * mass_l3::m5::units::kMsPerKn);
}

// T-L0-5b: negative speed_min → degrades.
TEST(L0SpeedBoxValidationTest, NegativeMinDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const auto box = mass_l3::m5::validate_speed_box(-2.0, 15.0, 10.0, "", deg);
  EXPECT_TRUE(deg.speed_box_degraded);
  EXPECT_DOUBLE_EQ(box.first, 0.0);
  EXPECT_DOUBLE_EQ(box.second, 10.0 * mass_l3::m5::units::kMsPerKn);
}

// T-L0-5c: speed_max = 0 → degrades (non-positive).
TEST(L0SpeedBoxValidationTest, ZeroMaxDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const auto box = mass_l3::m5::validate_speed_box(0.0, 0.0, 10.0, "", deg);
  EXPECT_TRUE(deg.speed_box_degraded);
  EXPECT_DOUBLE_EQ(box.first, 0.0);
  EXPECT_DOUBLE_EQ(box.second, 10.0 * mass_l3::m5::units::kMsPerKn);
}

// T-L0-5d: NaN speed_min → degrades.
TEST(L0SpeedBoxValidationTest, NanMinDegrades) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const auto box = mass_l3::m5::validate_speed_box(
      std::numeric_limits<double>::quiet_NaN(), 15.0, 10.0, "", deg);
  EXPECT_TRUE(deg.speed_box_degraded);
  EXPECT_TRUE(std::isfinite(box.first));
  EXPECT_TRUE(std::isfinite(box.second));
}

// T-L0-5e: M4 fallback rationale → uses nominal speed internally (relies on
// is_m4_fallback_rationale). Valid box values → no degraded flag, but max
// is treated as nominal.
TEST(L0SpeedBoxValidationTest, M4FallbackRationaleUsesNominal) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // With M4 fallback rationale, speed_max_raw is overridden to nominal (10 kn)
  // BEFORE validation. Since speed_max_raw becomes 10.0, the box is valid.
  const auto box = mass_l3::m5::validate_speed_box(
      5.0, 100.0, 10.0, "infeasible fallback", deg);
  // is_m4_fallback_rationale returns true for "infeasible fallback", so
  // speed_max_raw is set to nominal_speed_kn=10.0 internally. With min=5,
  // max=10, the box is valid (10 >= 5) → no degraded flag.
  EXPECT_FALSE(deg.speed_box_degraded);
  EXPECT_DOUBLE_EQ(box.first, 5.0 * mass_l3::m5::units::kMsPerKn);
  EXPECT_DOUBLE_EQ(box.second, 10.0 * mass_l3::m5::units::kMsPerKn);
}

// ===========================================================================
// T-L0-8: InputDegradation summary() — LX root-cause tracing
// ===========================================================================

// T-L0-8a: Multiple flags → summary contains all degraded field names.
TEST(L0InputDegradationTest, MultipleFlagsSummaryContainsAll) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  deg.own_psi_degraded = true;
  deg.own_u_degraded = true;
  deg.target_degraded = true;
  deg.speed_box_degraded = true;
  deg.reachability_degraded = true;
  deg.planned_speed_degraded = true;

  const auto s = deg.summary();
  EXPECT_NE(s.find("own_psi"), std::string::npos);
  EXPECT_NE(s.find("own_u"), std::string::npos);
  EXPECT_NE(s.find("target"), std::string::npos);
  EXPECT_NE(s.find("speed_box"), std::string::npos);
  EXPECT_NE(s.find("reachability"), std::string::npos);
  EXPECT_NE(s.find("planned_speed"), std::string::npos);
}

// T-L0-8b: Reset clears all flags and summary.
TEST(L0InputDegradationTest, ResetClearsAll) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  deg.own_psi_degraded = true;
  deg.speed_box_degraded = true;
  deg.reachability_degraded = true;

  EXPECT_TRUE(deg.any());
  deg.reset();
  EXPECT_FALSE(deg.any());
  EXPECT_TRUE(deg.summary().empty());
}

// T-L0-8c: Any() returns true when at least one flag is set.
TEST(L0InputDegradationTest, AnyReturnsTrueWhenAnyFlagSet) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  EXPECT_FALSE(deg.any());

  deg.own_u_degraded = true;
  EXPECT_TRUE(deg.any());

  deg.reset();
  EXPECT_FALSE(deg.any());

  deg.planned_speed_degraded = true;
  EXPECT_TRUE(deg.any());
}

// T-L0-8d: MidMpcInput starts with no degradation (default-constructed).
TEST(L0InputDegradationTest, MidMpcInputDefaultNoDegradation) {
  mass_l3::m5::MidMpcInput input;
  EXPECT_FALSE(input.degradation.any());
  EXPECT_TRUE(input.degradation.summary().empty());
}

// ===========================================================================
// Notes on tests NOT covered here (require integration/SIL environment):
//
// T-L0-5 (box-reach sanity): The L0-B sanity check
//   (heading_box_reachable_from_psi0_deg > 0 but pref_dir not Starboard/Port)
//   requires M6 COLREGsConstraint msg wiring + private assemble_input_()
//   method. Cannot unit-test without a ROS2 node/SIL environment.
//
// T-L0-6 / T-L0-7 (cpa_hard_m yaml / ROS param consistency): The
//   cpa_hard_m_ value is a MidMpcNode member initialized from
//   declare_parameter in the constructor. Verifying the param read path
//   requires a running ROS2 node (rclcpp::init). These are covered by:
//   - Task 2 implementation: constructor reads "m5.cpa_hard_m" with
//     kCpaSafeFallback_m as default, validates (finite, > 0), warns on
//     invalid and falls back to kCpaSafeFallback_m.
//   - Integration test: launch mid_mpc_node with custom config YAML and
//     verify the published AvoidancePlan CPA floor via SIL trace.
// ===========================================================================
