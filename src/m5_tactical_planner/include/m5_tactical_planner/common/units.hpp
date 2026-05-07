#ifndef MASS_L3_M5_COMMON_UNITS_HPP_
#define MASS_L3_M5_COMMON_UNITS_HPP_

// M5 Tactical Planner — Unit conversion utilities
// PATH-D (MISRA C++:2023): all constants are constexpr, no magic numbers
// in source files; all conversions are [[nodiscard]] pure functions.
//
// Unit conventions used throughout M5:
//   Angles  : radians internally; degrees at ROS2 message boundary
//   Speeds  : m/s internally; knots at operator / YAML boundary
//   Distances: metres internally; nautical miles at operator / spec boundary

#include <cmath>

namespace mass_l3::m5::units {

// ---------------------------------------------------------------------------
// Physical constants — defined once, referenced by all conversions
// ---------------------------------------------------------------------------
inline constexpr double kPi              = M_PI;
inline constexpr double kTwoPi           = 2.0 * M_PI;
inline constexpr double kDegPerRad       = 180.0 / M_PI;
inline constexpr double kRadPerDeg       = M_PI / 180.0;
inline constexpr double kKnPerMs         = 1.94384;   // 1 m/s = 1.94384 kn
inline constexpr double kMsPerKn         = 0.514444;  // 1 kn  = 0.514444 m/s
inline constexpr double kMetresPerNm     = 1852.0;    // 1 NM  = 1852 m (exact)
inline constexpr double kNmPerMetre      = 1.0 / 1852.0;

// ---------------------------------------------------------------------------
// Angle conversions
// ---------------------------------------------------------------------------

/// Radians → degrees
[[nodiscard]] constexpr double rad_to_deg(double rad) noexcept {
  return rad * kDegPerRad;
}

/// Degrees → radians
[[nodiscard]] constexpr double deg_to_rad(double deg) noexcept {
  return deg * kRadPerDeg;
}

/// Wrap angle to (-π, +π]
[[nodiscard]] inline double wrap_pi(double rad) noexcept {
  // fmod-based wrap; avoids branching loop for large angles
  double r = std::fmod(rad + kPi, kTwoPi);
  if (r < 0.0) { r += kTwoPi; }
  return r - kPi;
}

/// Wrap angle to [0, 2π)
[[nodiscard]] inline double wrap_two_pi(double rad) noexcept {
  double r = std::fmod(rad, kTwoPi);
  if (r < 0.0) { r += kTwoPi; }
  return r;
}

// ---------------------------------------------------------------------------
// Speed conversions
// ---------------------------------------------------------------------------

/// Knots → m/s
[[nodiscard]] constexpr double kn_to_mps(double kn) noexcept {
  return kn * kMsPerKn;
}

/// m/s → knots
[[nodiscard]] constexpr double mps_to_kn(double mps) noexcept {
  return mps * kKnPerMs;
}

// ---------------------------------------------------------------------------
// Distance conversions
// ---------------------------------------------------------------------------

/// Nautical miles → metres
[[nodiscard]] constexpr double nm_to_m(double nm) noexcept {
  return nm * kMetresPerNm;
}

/// Metres → nautical miles
[[nodiscard]] constexpr double m_to_nm(double m) noexcept {
  return m * kNmPerMetre;
}

}  // namespace mass_l3::m5::units

#endif  // MASS_L3_M5_COMMON_UNITS_HPP_
