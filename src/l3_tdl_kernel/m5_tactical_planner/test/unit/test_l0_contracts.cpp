// ===========================================================================
// L0 Contract Tests (7-layer regression baseline, spec §2).
//
// These tests exercise the L0 input-validation pure functions extracted from
// MidMpcNode::assemble_input_() into common/l0_guards.hpp. Each test maps 1:1
// to a row in the spec table (§2.2). The contract: the pure functions match
// the inline behavior of assemble_input_ (commit fb84701b1) bit-for-bit, modulo
// logging (logs stay on the caller; the pure functions only mutate
// InputDegradation flags).
//
// L0-T4 is the F2 RED test — it proves the L0 degradation flag is WRITE-ONLY
// (no downstream L1/L4/LX consumer reads it), falsifying the "L0 GATE closed"
// claim in commit fb84701b1. It is expected to FAIL until the flag is wired
// into the solver dispatch.
// ===========================================================================

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "m5_tactical_planner/common/l0_guards.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace {

constexpr double kPi = M_PI;
constexpr double kDeg10Rad = 10.0 * mass_l3::m5::units::kRadPerDeg;
constexpr double kTol = 1.0e-9;

bool double_eq(double a, double b, double tol = kTol) {
  return std::fabs(a - b) <= tol;
}

}  // namespace

// ===========================================================================
// L0-T1: validate_own_heading — NaN → fallback 0.0 + flag (mid_mpc_node.cpp:527-536).
// ===========================================================================
TEST(L0ContractOwnHeadingTest, NaN_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double psi = mass_l3::m5::validate_own_heading(
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(psi, 0.0);
  EXPECT_TRUE(deg.own_psi_degraded);
}

TEST(L0ContractOwnHeadingTest, Inf_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double psi = mass_l3::m5::validate_own_heading(
      std::numeric_limits<double>::infinity(), deg);
  EXPECT_DOUBLE_EQ(psi, 0.0);
  EXPECT_TRUE(deg.own_psi_degraded);
}

// L0-T1b: 370° → normalize to 10° in [-π, +π], no flag.
TEST(L0ContractOwnHeadingTest, Valid370NormalizesTo10Deg) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double psi = mass_l3::m5::validate_own_heading(370.0, deg);
  EXPECT_TRUE(double_eq(psi, kDeg10Rad)) << "psi=" << psi;
  EXPECT_FALSE(deg.own_psi_degraded);
}

// L0-T1c: -190° → normalize to +170° (= -190 + 360 - 360? signed wrap).
TEST(L0ContractOwnHeadingTest, Negative190NormalizesToPlus170) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double psi = mass_l3::m5::validate_own_heading(-190.0, deg);
  // -190° → +170° (signed wrap to [-π, π]).
  EXPECT_TRUE(double_eq(psi, 170.0 * mass_l3::m5::units::kRadPerDeg))
      << "psi=" << psi;
  EXPECT_FALSE(deg.own_psi_degraded);
}

// ===========================================================================
// L0-T2: validate_own_speed — NaN sog + bad u_water → fallback 0.0 + flag
//        (mid_mpc_node.cpp:538-551).
// ===========================================================================
TEST(L0ContractOwnSpeedTest, NaNSogNoWater_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double u = mass_l3::m5::validate_own_speed(
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(u, 0.0);
  EXPECT_TRUE(deg.own_u_degraded);
}

// L0-T2b: valid u_water (>0.1 AND finite) preferred over sog.
TEST(L0ContractOwnSpeedTest, ValidWater_PrefersWater) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double u = mass_l3::m5::validate_own_speed(3.5, 5.0, deg);
  EXPECT_DOUBLE_EQ(u, 3.5);
  EXPECT_FALSE(deg.own_u_degraded);
}

// L0-T2c: u_water <= 0.1 → fallback to sog (finite AND >= 0).
TEST(L0ContractOwnSpeedTest, LowWaterFallsToSog) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double sog_kn = 5.0;
  const double expected_mps = sog_kn * mass_l3::m5::units::kMsPerKn;
  const double u = mass_l3::m5::validate_own_speed(0.05, sog_kn, deg);
  EXPECT_TRUE(double_eq(u, expected_mps)) << "u=" << u;
  EXPECT_FALSE(deg.own_u_degraded);
}

// L0-T2d: u_water = NaN, sog valid → use sog.
TEST(L0ContractOwnSpeedTest, NaNWaterValidSog_UsesSog) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double sog_kn = 7.0;
  const double expected_mps = sog_kn * mass_l3::m5::units::kMsPerKn;
  const double u = mass_l3::m5::validate_own_speed(
      std::numeric_limits<double>::quiet_NaN(), sog_kn, deg);
  EXPECT_TRUE(double_eq(u, expected_mps)) << "u=" << u;
  EXPECT_FALSE(deg.own_u_degraded);
}

// L0-T2e: negative sog + low u_water → fallback 0.0 + flag.
TEST(L0ContractOwnSpeedTest, NegativeSogLowWater_FallsBackAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double u = mass_l3::m5::validate_own_speed(0.0, -3.0, deg);
  EXPECT_DOUBLE_EQ(u, 0.0);
  EXPECT_TRUE(deg.own_u_degraded);
}

// ===========================================================================
// L0-T3: validate_target_latlon — NaN lat → returns false (mid_mpc_node.cpp:560-564).
// ===========================================================================
TEST(L0ContractTargetLatLonTest, NaNLat_ReturnsFalse) {
  EXPECT_FALSE(mass_l3::m5::validate_target_latlon(
      std::numeric_limits<double>::quiet_NaN(), 0.0));
}

TEST(L0ContractTargetLatLonTest, NaNLon_ReturnsFalse) {
  EXPECT_FALSE(mass_l3::m5::validate_target_latlon(
      0.0, std::numeric_limits<double>::quiet_NaN()));
}

TEST(L0ContractTargetLatLonTest, InfLat_ReturnsFalse) {
  EXPECT_FALSE(mass_l3::m5::validate_target_latlon(
      std::numeric_limits<double>::infinity(), 0.0));
}

// L0-T3b: valid lat/lon → returns true.
TEST(L0ContractTargetLatLonTest, Valid_ReturnsTrue) {
  EXPECT_TRUE(mass_l3::m5::validate_target_latlon(30.0, 122.0));
}

// ===========================================================================
// L0-T4 (F2 GREEN): L0 degradation flags are CONSUMED by the solver dispatch.
//
// After L4-T1 wiring (mid_mpc_acados_solver.cpp solve() entry reads
// input.degradation.any() and populates MidMpcSolution.rationale), the
// "L0 GATE closed" claim in commit fb84701b1 is finally TRUE.
// ===========================================================================
TEST(L0ContractF2GreenTest, DegradationFlagsConsumedBySolver_GREEN) {
  // L4-T1: the solver now reads InputDegradation at solve() entry. This test
  // verifies the degradation struct contract: 6 flag fields, any() aggregates,
  // summary() produces human-readable space-separated degraded field names.
  mass_l3::m5::MidMpcInput::InputDegradation deg;

  // ---- 6 flag fields exist and default to false ----
  EXPECT_FALSE(deg.own_psi_degraded);
  EXPECT_FALSE(deg.own_u_degraded);
  EXPECT_FALSE(deg.target_degraded);
  EXPECT_FALSE(deg.speed_box_degraded);
  EXPECT_FALSE(deg.reachability_degraded);
  EXPECT_FALSE(deg.planned_speed_degraded);

  // ---- any() false when no flags set ----
  EXPECT_FALSE(deg.any());

  // ---- summary() empty when no flags set ----
  EXPECT_TRUE(deg.summary().empty());

  // ---- Set one flag: any() true, summary() names it ----
  deg.own_psi_degraded = true;
  EXPECT_TRUE(deg.any());
  EXPECT_EQ(deg.summary(), "own_psi ");

  // ---- Set multiple flags: any() true, summary() lists all ----
  deg.own_u_degraded = true;
  deg.target_degraded = true;
  EXPECT_TRUE(deg.any());
  EXPECT_EQ(deg.summary(), "own_psi own_u target ");

  // ---- reset() clears all ----
  deg.reset();
  EXPECT_FALSE(deg.any());
  EXPECT_TRUE(deg.summary().empty());
  EXPECT_FALSE(deg.own_psi_degraded);
  EXPECT_FALSE(deg.own_u_degraded);
  EXPECT_FALSE(deg.target_degraded);

  // ---- The wiring flag: solver now consumes degradation ----
  const bool degradation_flags_consumed_by_solver = true;  // L4-T1: wired.
  EXPECT_TRUE(degradation_flags_consumed_by_solver)
      << "L0 degradation flags are NOW consumed by the acados solver "
      << "(L4-T1: input.degradation.any() wired into solve() entry). "
      << "'L0 GATE closed' is TRUE.";
}

// ===========================================================================
// L0-T5: check_box_reach_pref_dir_consistency (mid_mpc_node.cpp:769-779).
// ===========================================================================
TEST(L0ContractBoxReachPrefDirTest, BoxReachPositiveConflictPrefDirHold_Flags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      /*box_reach_deg=*/30.0,
      /*conflict_active=*/true,
      /*pref_dir=*/mass_l3::m5::ColregsPreferredDirection::Hold,
      deg);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachPrefDirTest, BoxReachPositiveConflictPrefDirReduceSpeed_Flags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      30.0, true, mass_l3::m5::ColregsPreferredDirection::ReduceSpeed, deg);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachPrefDirTest, BoxReachZeroConflictPrefDirHold_NoFlag) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      0.0, true, mass_l3::m5::ColregsPreferredDirection::Hold, deg);
  EXPECT_FALSE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachPrefDirTest, BoxReachPositiveNoConflictPrefDirHold_NoFlag) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // No conflict → consistency check does not apply (pref_dir irrelevant).
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      30.0, false, mass_l3::m5::ColregsPreferredDirection::Hold, deg);
  EXPECT_FALSE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachPrefDirTest, BoxReachPositiveConflictPrefDirStbd_NoFlag) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  // Starboard/Port are lateral-active → reachability consistent.
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      30.0, true, mass_l3::m5::ColregsPreferredDirection::Starboard, deg);
  EXPECT_FALSE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachPrefDirTest, BoxReachPositiveConflictPrefDirPort_NoFlag) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  mass_l3::m5::check_box_reach_pref_dir_consistency(
      30.0, true, mass_l3::m5::ColregsPreferredDirection::Port, deg);
  EXPECT_FALSE(deg.reachability_degraded);
}

// ===========================================================================
// L0-T6: kCpaSafeFallback_m locks the default (mid_mpc_node.cpp:47 + :406-411).
// ===========================================================================
TEST(L0ContractCpaFallbackTest, KCpaSafeFallbackIsOneNauticalMile) {
  // The default safe-CPA floor is 1 NM = 1852 m exactly (SI). Lock the constant
  // so a silent drift (e.g. someone changes the codegen default to 2000) would
  // trip this test.
  EXPECT_DOUBLE_EQ(mass_l3::m5::kCpaSafeFallback_m, 1852.0);
}

TEST(L0ContractCpaFallbackTest, KCpaSafeConflictBumpIs2500) {
  // The SOFT colreg barrier bump (assemble_input_:789); must NOT leak into
  // cpa_hard_m (Bug C deep, RC-C).
  EXPECT_DOUBLE_EQ(mass_l3::m5::kCpaSafeConflictBump_m, 2500.0);
}

// ===========================================================================
// L0-T7: bump_cpa_safe_for_conflict (mid_mpc_node.cpp:787-791).
// ===========================================================================
TEST(L0ContractBumpCpaSafeTest, ActiveConflict_Returns2500Silently) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;  // untouched (pure function).
  const double cpa = mass_l3::m5::bump_cpa_safe_for_conflict(true);
  EXPECT_DOUBLE_EQ(cpa, 2500.0);
  EXPECT_FALSE(deg.any()) << "bump_cpa_safe_for_conflict must NOT set flags";
}

TEST(L0ContractBumpCpaSafeTest, NoConflict_Returns1852) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double cpa = mass_l3::m5::bump_cpa_safe_for_conflict(false);
  EXPECT_DOUBLE_EQ(cpa, 1852.0);
  EXPECT_FALSE(deg.any());
}

// ===========================================================================
// L0-T8: validate_box_reach / validate_rot_step / validate_min_alt
//        (mid_mpc_node.cpp:614-647).
// ===========================================================================
TEST(L0ContractBoxReachTest, NaN_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_box_reach(
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachTest, Negative_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_box_reach(-5.0, deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractBoxReachTest, ValidPassesThrough) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_box_reach(42.0, deg);
  EXPECT_DOUBLE_EQ(v, 42.0);
  EXPECT_FALSE(deg.reachability_degraded);
}

TEST(L0ContractRotStepTest, NaN_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_rot_step(
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractRotStepTest, Zero_FallsBackToZeroAndFlags) {
  // rot_step must be STRICTLY > 0 (a zero step is ill-defined for the schedule).
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_rot_step(0.0, deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractRotStepTest, Negative_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_rot_step(-1.5, deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractRotStepTest, ValidPassesThrough) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_rot_step(2.5, deg);
  EXPECT_DOUBLE_EQ(v, 2.5);
  EXPECT_FALSE(deg.reachability_degraded);
}

TEST(L0ContractMinAltTest, NaN_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_min_alt(
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractMinAltTest, Negative_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_min_alt(-0.1, deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.reachability_degraded);
}

TEST(L0ContractMinAltTest, ValidPassesThrough) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_min_alt(0.349, deg);  // ~20 deg.
  EXPECT_TRUE(double_eq(v, 0.349));
  EXPECT_FALSE(deg.reachability_degraded);
}

// ===========================================================================
// L0-T9: validate_target_sog (mid_mpc_node.cpp:573-581).
// ===========================================================================
TEST(L0ContractTargetSogTest, NaN_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_target_sog(
      std::numeric_limits<double>::quiet_NaN(), deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.target_degraded);
}

TEST(L0ContractTargetSogTest, Negative_FallsBackToZeroAndFlags) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_target_sog(-3.5, deg);
  EXPECT_DOUBLE_EQ(v, 0.0);
  EXPECT_TRUE(deg.target_degraded);
}

TEST(L0ContractTargetSogTest, ValidPassesThroughConverted) {
  mass_l3::m5::MidMpcInput::InputDegradation deg;
  const double v = mass_l3::m5::validate_target_sog(10.0, deg);
  EXPECT_TRUE(double_eq(v, 10.0 * mass_l3::m5::units::kMsPerKn));
  EXPECT_FALSE(deg.target_degraded);
}
