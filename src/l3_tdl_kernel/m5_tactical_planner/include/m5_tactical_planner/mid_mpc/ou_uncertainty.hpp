// include/m5_tactical_planner/mid_mpc/ou_uncertainty.hpp
// P7: Ornstein-Uhlenbeck lateral position uncertainty ([RMD] Ch3.7 Stochastic MPC)
//
// This is NOT an Eriksen method — it is a Rawlings-Mayne-Diehl engineering extension
// for robust MPC. See docs/superpowers/specs/2026-07-18-m5-p7-robustness-ou-intent-design.md §1.2.
//
// σ_pos²(t) = σ_0² · (1 - exp(-2t/τ_OU)), bounded by σ_0 (as t → ∞).
//
// The OU process models growing lateral uncertainty over the prediction horizon:
// - Fixed objects: near-zero sigma (known position)
// - Vessels with high intent_confidence: moderate sigma (predictable behavior)
// - Vessels with low intent_confidence: large sigma (unpredictable)
// - Unknown-classification targets: conservative default

#ifndef MASS_L3_M5_OU_UNCERTAINTY_HPP_
#define MASS_L3_M5_OU_UNCERTAINTY_HPP_

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5 {

// ---------------------------------------------------------------------------
// OuUncertainty — OU process parameters for one target
// ---------------------------------------------------------------------------
struct OuUncertainty {
  double sigma_0_m{0.0};   // initial lateral position uncertainty [m]
  double tau_OU_s{1.0};    // OU time constant [s] (must be > 0)

  // σ_pos(t): OU process lateral position standard deviation at time t
  // σ_pos²(t) = σ_0² · (1 - exp(-2t/τ_OU))
  // Bounded by σ_0 as t → ∞. When τ_OU → ∞, σ_pos(t) → σ_0 (constant).
  [[nodiscard]] double sigma_pos_m(const double t_s) const noexcept {
    if (tau_OU_s <= 0.0 || !std::isfinite(tau_OU_s)) {
      return sigma_0_m;  // degenerate: constant uncertainty
    }
    if (t_s <= 0.0) {
      return 0.0;  // at t=0, no accumulated uncertainty
    }
    // σ_pos²(t) = σ_0² · (1 - exp(-2t/τ_OU))
    const double exponent = -2.0 * t_s / tau_OU_s;
    if (exponent < -50.0) {
      return sigma_0_m * std::sqrt(2.0);  // saturated to asymptotic bound
    }
    return sigma_0_m * std::sqrt(1.0 - std::exp(exponent));
  }
};

// ---------------------------------------------------------------------------
// derive_ou_params — dynamic OU parameter derivation from target properties
// spec §4.3: classification + sog + intent_confidence → (sigma_0, tau_OU)
// ---------------------------------------------------------------------------
[[nodiscard]] inline OuUncertainty derive_ou_params(
    const TargetState::Classification classification,
    const double sog_mps,
    const double intent_confidence) noexcept {
  // FixedObject: no motion uncertainty, σ near zero, τ → ∞ (constant)
  if (classification == TargetState::Classification::FixedObject) {
    return OuUncertainty{5.0, 1.0e9};
  }

  const bool high_speed = (sog_mps > 5.0);
  const bool low_intent = (intent_confidence < 0.3);

  if (classification == TargetState::Classification::Vessel) {
    if (high_speed && low_intent) {
      // Fast + unpredictable: max uncertainty
      return OuUncertainty{100.0, 300.0};
    }
    if (high_speed) {
      // Fast + predictable: moderate-high uncertainty
      return OuUncertainty{50.0, 500.0};
    }
    if (low_intent) {
      // Slow + unpredictable: moderate uncertainty
      return OuUncertainty{60.0, 400.0};
    }
    // Slow + high intent: well-behaved vessel
    return OuUncertainty{30.0, 600.0};
  }

  // Unknown classification: conservative default
  return OuUncertainty{80.0, 400.0};
}

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_OU_UNCERTAINTY_HPP_
