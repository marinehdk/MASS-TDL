// SPDX-License-Identifier: Proprietary
// Shared state vector for ShipMotionSimulator plugins.
// Matches fcb_sim::FcbState field names for zero-conversion use in tests.
#pragma once
namespace ship_sim {
struct ShipState {
  double x{0.0};        // East position relative to NED origin (m)
  double y{0.0};        // North position relative to NED origin (m)
  double psi{0.0};      // heading, math convention rad (CCW from East; 0=East, π/2=North)
  double u{0.0};        // surge through-water velocity (m/s)
  double v{0.0};        // sway through-water velocity (m/s)
  double r{0.0};        // yaw rate (rad/s)
  double phi{0.0};      // roll angle (rad)
  double phi_dot{0.0};  // roll rate (rad/s)
};
}  // namespace ship_sim
