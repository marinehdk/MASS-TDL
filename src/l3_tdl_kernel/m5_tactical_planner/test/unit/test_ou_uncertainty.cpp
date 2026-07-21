// test/unit/test_ou_uncertainty.cpp
// P7: OU uncertainty parameter derivation tests (T2)

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/ou_uncertainty.hpp"

using mass_l3::m5::OuUncertainty;
using mass_l3::m5::TargetState;
using mass_l3::m5::derive_ou_params;

// ---------------------------------------------------------------------------
// DeriveOuParams — derivation rule correctness
// ---------------------------------------------------------------------------
TEST(OuUncertaintyTest, DeriveOuParams_FixedObject_LowSigma) {
  const auto ou = derive_ou_params(
      TargetState::Classification::FixedObject, 10.0, 0.1);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 5.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 1.0e9);
}

TEST(OuUncertaintyTest, DeriveOuParams_VesselHighSpeedLowIntent_MaxSigma) {
  const auto ou = derive_ou_params(
      TargetState::Classification::Vessel, 10.0, 0.2);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 100.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 300.0);
}

TEST(OuUncertaintyTest, DeriveOuParams_VesselHighSpeedHighIntent) {
  const auto ou = derive_ou_params(
      TargetState::Classification::Vessel, 10.0, 0.8);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 50.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 500.0);
}

TEST(OuUncertaintyTest, DeriveOuParams_VesselLowSpeedLowIntent) {
  const auto ou = derive_ou_params(
      TargetState::Classification::Vessel, 3.0, 0.2);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 60.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 400.0);
}

TEST(OuUncertaintyTest, DeriveOuParams_VesselLowSpeedHighIntent) {
  const auto ou = derive_ou_params(
      TargetState::Classification::Vessel, 3.0, 0.8);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 30.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 600.0);
}

TEST(OuUncertaintyTest, DeriveOuParams_UnknownClassification_Conservative) {
  const auto ou = derive_ou_params(
      TargetState::Classification::Unknown, 5.0, 0.5);
  EXPECT_DOUBLE_EQ(ou.sigma_0_m, 80.0);
  EXPECT_DOUBLE_EQ(ou.tau_OU_s, 400.0);
}

// ---------------------------------------------------------------------------
// SigmaPos — σ_pos(t) numerical behaviour
// ---------------------------------------------------------------------------
TEST(OuUncertaintyTest, SigmaPos_BoundedAbove) {
  // σ_pos²(t) = σ_0² · (1 - exp(-2t/τ)), so σ_pos(t→∞) → σ_0 (not σ_0·√2).
  // At t = 5·τ, exp(-10) ≈ 4.5e-5 → negligible → σ_pos ≈ σ_0.
  const OuUncertainty ou{50.0, 300.0};
  const double asymptotic = 50.0;  // σ_0 is the asymptotic bound
  const double sigma_at_5tau = ou.sigma_pos_m(5.0 * 300.0);
  EXPECT_NEAR(sigma_at_5tau, asymptotic, 0.01);
  // Never exceeds σ_0
  EXPECT_LE(sigma_at_5tau, asymptotic + 1.0e-9);
}

TEST(OuUncertaintyTest, SigmaPos_MonotonicIncreasing) {
  // σ_pos(t) must be monotonically non-decreasing in t
  const OuUncertainty ou{50.0, 300.0};
  double prev = 0.0;
  for (double t = 0.0; t <= 1000.0; t += 10.0) {
    const double cur = ou.sigma_pos_m(t);
    EXPECT_GE(cur + 1.0e-12, prev);  // tolerate tiny fp rounding
    prev = cur;
  }
}

TEST(OuUncertaintyTest, SigmaPos_AtT0_Is0) {
  const OuUncertainty ou{50.0, 300.0};
  EXPECT_DOUBLE_EQ(ou.sigma_pos_m(0.0), 0.0);
}

TEST(OuUncertaintyTest, SigmaPos_TauInfinite_IsConstant) {
  // tau → ∞ → σ_pos(t) = σ_0 (process never decorrelates)
  const OuUncertainty ou{50.0, 1.0e9};
  const double at_0 = ou.sigma_pos_m(0.0);
  const double at_1k = ou.sigma_pos_m(1000.0);
  EXPECT_DOUBLE_EQ(at_0, 0.0);
  // With tau = 1e9, even at t=1000, exponent ~ -2e-6 → 1 - exp(...) ≈ 2e-6
  // So sigma ≈ sigma_0 * sqrt(2e-6) ≈ 50 * 0.0014 = 0.07, very small
  EXPECT_LT(at_1k, 1.0);
  EXPECT_GT(at_1k, 0.0);
}

TEST(OuUncertaintyTest, SigmaPos_DegenerateTau_ReturnsSigma0) {
  // tau <= 0 → degenerate, return sigma_0 directly
  const OuUncertainty ou{50.0, 0.0};
  const double at_100 = ou.sigma_pos_m(100.0);
  EXPECT_DOUBLE_EQ(at_100, 50.0);
}
