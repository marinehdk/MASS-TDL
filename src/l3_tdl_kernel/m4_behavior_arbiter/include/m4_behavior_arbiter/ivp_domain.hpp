#pragma once

#include <cmath>
#include <cstddef>

namespace mass_l3::m4 {

/**
 * @brief Discretised heading search space [0°, 360°) with configurable resolution.
 *
 * Wraps on 360° boundary via static wrap() utility.
 */
class IvPHeadingDomain {
public:
  explicit IvPHeadingDomain(double resolution_deg = 1.0)
      : resolution_deg_(resolution_deg),
        n_steps_(static_cast<size_t>(std::ceil(360.0 / resolution_deg_))) {}

  double at(size_t idx) const {
    return static_cast<double>(idx) * resolution_deg_;
  }

  size_t size() const { return n_steps_; }

  double resolution_deg() const { return resolution_deg_; }

  /// Normalise heading to [0°, 360°).
  static double wrap(double deg) {
    double h = std::fmod(deg, 360.0);
    if (h < 0.0) {
      h += 360.0;
    }
    return h;
  }

private:
  double resolution_deg_;
  size_t n_steps_;
};

/**
 * @brief Discretised speed search space [min_kn, max_kn] with configurable resolution.
 */
class IvPSpeedDomain {
public:
  IvPSpeedDomain(double min_kn, double max_kn, double resolution_kn)
      : min_kn_(min_kn),
        max_kn_(max_kn),
        resolution_kn_(resolution_kn),
        n_steps_(static_cast<size_t>(
            std::ceil((max_kn_ - min_kn_) / resolution_kn_))) {}

  double at(size_t idx) const {
    return min_kn_ + static_cast<double>(idx) * resolution_kn_;
  }

  size_t size() const { return n_steps_; }

  double min_kn() const { return min_kn_; }
  double max_kn() const { return max_kn_; }
  double resolution_kn() const { return resolution_kn_; }

private:
  double min_kn_;
  double max_kn_;
  double resolution_kn_;
  size_t n_steps_;
};

}  // namespace mass_l3::m4
