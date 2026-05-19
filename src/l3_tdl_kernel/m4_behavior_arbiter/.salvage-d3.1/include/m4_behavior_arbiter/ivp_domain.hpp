#pragma once

#include <cstddef>

#include "m4_behavior_arbiter/error.hpp"

namespace mass_l3::m4 {

/**
 * @brief Discretized search domain for heading dimension.
 *
 * Per detailed design §5.5: resolution = 1°, range = [0°, 360°).
 * Total points = 360 / resolution.
 */
class IvPHeadingDomain {
 public:
  /**
   * @brief Construct heading domain.
   * @param resolution_deg Step size in degrees; must be > 0.0.
   */
  explicit IvPHeadingDomain(double resolution_deg = 1.0);

  /**
   * @brief Number of discrete heading samples.
   * @return 360 / resolution_deg (truncated).
   */
  size_t size() const { return size_; }

  /**
   * @brief Heading value at index i (degrees).
   * @param i Index in [0, size()).
   * @return i * resolution_deg.
   * @throws std::out_of_range if i >= size().
   */
  double at(size_t i) const;

  /**
   * @brief Resolution in degrees.
   * @return Resolution set at construction.
   */
  double resolution() const { return resolution_deg_; }

  /**
   * @brief Wrap psi_deg to [0, 360).
   * @param psi_deg Input heading in degrees.
   * @return Normalized heading.
   */
  double wrap(double psi_deg) const;

 private:
  double resolution_deg_;
  size_t size_;
};

/**
 * @brief Discretized search domain for speed dimension.
 *
 * Range [min_kn, max_kn] with given resolution. ODD-bounded.
 */
class IvPSpeedDomain {
 public:
  /**
   * @brief Construct speed domain.
   * @param min_kn Minimum speed (≥ 0).
   * @param max_kn Maximum speed (> min_kn).
   * @param resolution_kn Step size in knots; must be > 0.0.
   */
  IvPSpeedDomain(double min_kn, double max_kn, double resolution_kn = 0.5);

  /**
   * @brief Number of discrete speed samples.
   * @return (max_kn - min_kn) / resolution_kn + 1 (inclusive both ends).
   */
  size_t size() const { return size_; }

  /**
   * @brief Speed value at index i (knots).
   * @param i Index in [0, size()).
   * @return min_kn + i * resolution_kn.
   * @throws std::out_of_range if i >= size().
   */
  double at(size_t i) const;

  /**
   * @brief Resolution in knots.
   * @return Resolution set at construction.
   */
  double resolution() const { return resolution_kn_; }

  /**
   * @brief Minimum speed bound.
   * @return Minimum speed set at construction.
   */
  double min() const { return min_kn_; }

  /**
   * @brief Maximum speed bound.
   * @return Maximum speed set at construction.
   */
  double max() const { return max_kn_; }

 private:
  double min_kn_;
  double max_kn_;
  double resolution_kn_;
  size_t size_;
};

}  // namespace mass_l3::m4
