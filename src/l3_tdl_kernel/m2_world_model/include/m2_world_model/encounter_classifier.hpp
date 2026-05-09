#pragma once

#include <cstdint>

#include <l3_msgs/msg/encounter_classification.hpp>

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/// Geometric COLREG encounter classifier.
/// Performs pre-classification per §5.1.3 of the M2 detailed design (v1.1.2).
/// M6 COLREGs Reasoner performs the full rule reasoning on top of this.
class EncounterClassifier final {
 public:
  struct Config {
    double overtaking_bearing_min_deg;   // 112.5
    double overtaking_bearing_max_deg;   // 247.5
    double head_on_heading_diff_tol_deg; // 6.0
    double safe_pass_speed_threshold_mps;
    double safe_pass_min_cpa_m;          // 926.0 (0.5 nm)
  };

  explicit EncounterClassifier(Config cfg);

  /// Classify encounter type based on geometric parameters.
  [[nodiscard]] l3_msgs::msg::EncounterClassification
  classify(double relative_bearing_deg,
           double own_heading_deg,
           double target_heading_deg,
           double relative_speed_mps,
           double cpa_m) const;

 private:
  /// Normalise bearing to [0, 360).
  [[nodiscard]] static double normalize_bearing_deg_(double bearing) noexcept;

  /// Smallest angular difference between two headings (result in [0, 180]).
  [[nodiscard]] static double smallest_angle_diff_deg_(double a, double b) noexcept;

  Config cfg_;
};

}  // namespace mass_l3::m2
