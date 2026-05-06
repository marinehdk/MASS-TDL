#pragma once

#include <Eigen/Core>
#include <cmath>

namespace mass_l3::m6_colregs {

/// Normalize bearing to [0, 360).
double normalize_bearing_deg(double bearing);

/// Compute smallest angle difference in degrees.
double angle_diff_deg(double a, double b);

/// Compute relative bearing of target from ownship (0-360, CW from north).
double relative_bearing_deg(double own_heading_deg, double target_bearing_deg);

/// Compute aspect angle of target.
double aspect_angle_deg(double own_heading_deg, double target_heading_deg,
                        double relative_bearing_deg);

/// Haversine distance between two lat/lon points in meters.
double haversine_m(double lat1_deg, double lon1_deg,
                   double lat2_deg, double lon2_deg);

/// Convert degrees to radians.
inline double deg2rad(double deg) { return deg * M_PI / 180.0; }
inline double rad2deg(double rad) { return rad * 180.0 / M_PI; }

}  // namespace mass_l3::m6_colregs
