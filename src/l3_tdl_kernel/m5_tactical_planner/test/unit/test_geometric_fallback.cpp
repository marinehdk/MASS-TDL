#include <gtest/gtest.h>

#include <cmath>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5 {
namespace {

// ---------------------------------------------------------------------------
// DEMO-2 P2: Geometric fallback turn magnitude from M6 min-alteration, not
// fixed 5/6 fraction. fallback_target_heading computes target_psi as
// route_bearing + min_alteration, clamped into the M4 heading window.
// ---------------------------------------------------------------------------

TEST(GeometricFallback, TargetsMinAlterationNotFixedFraction) {
  // Route bearing = 0° (north), window = [0°, 40°], min alteration = 20°.
  // Expected: 0° + 20° = 20°, inside window → no clamping.
  const double route_brg = 0.0;
  const double h_min = 0.0;
  const double h_max = 40.0 * M_PI / 180.0;
  const double min_alt = 20.0 * M_PI / 180.0;

  const double target = fallback_target_heading(route_brg, h_min, h_max, min_alt);

  EXPECT_NEAR(target, 20.0 * M_PI / 180.0, 1e-3);
}

TEST(GeometricFallback, ClampsToWindow) {
  // Min alteration larger than window → clamp at window edge.
  const double h_min = 0.0;
  const double h_max = 15.0 * M_PI / 180.0;
  const double min_alt = 30.0 * M_PI / 180.0;

  const double target = fallback_target_heading(0.0, h_min, h_max, min_alt);

  EXPECT_NEAR(target, h_max, 1e-3);
}

}  // namespace
}  // namespace mass_l3::m5
