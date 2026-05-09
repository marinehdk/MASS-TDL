#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <Eigen/Core>

#include "m2_world_model/coord_transform.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/// CPA / TCPA calculator with uncertainty propagation.
/// Implements §5.1.2 of the M2 detailed design (v1.1.2).
class CpaTcpaCalculator final {
 public:
  enum class UncertaintyMethod : std::uint8_t { Linear = 0, MonteCarlo = 1 };

  struct Config {
    UncertaintyMethod method;
    std::int32_t monte_carlo_samples;
    double safety_factor;
    double odd_d_multiplier;
    double max_align_dt_s;
    double static_target_speed_mps;
  };

  explicit CpaTcpaCalculator(Config cfg);

  /// Compute CPA / TCPA between own ship and a target.
  /// Returns nullopt when the time alignment window is exceeded or conversion fails.
  [[nodiscard]] std::optional<CpaResult>
  compute(const OwnShipSnapshot& own_ship,
          const TargetSnapshot& target,
          OddZone odd_zone) const;

 private:
  /// Extrapolate a target snapshot to a given time point (constant velocity model).
  [[nodiscard]] TargetSnapshot
  extrapolate_to_(const TargetSnapshot& target,
                  std::chrono::steady_clock::time_point target_t) const;

  /// Linear (analytical Jacobian) uncertainty propagation.
  [[nodiscard]] CpaUncertainty
  propagate_linear_(const Eigen::Vector2d& rel_pos,
                    const Eigen::Vector2d& rel_vel,
                    const Eigen::Matrix2d& sigma_rel_pos,
                    const Eigen::Matrix2d& sigma_rel_vel) const;

  /// Monte Carlo uncertainty propagation.
  [[nodiscard]] CpaUncertainty
  propagate_monte_carlo_(const Eigen::Vector2d& rel_pos,
                         const Eigen::Vector2d& rel_vel,
                         const Eigen::Matrix2d& sigma_rel_pos,
                         const Eigen::Matrix2d& sigma_rel_vel) const;

  Config cfg_;
};

}  // namespace mass_l3::m2
