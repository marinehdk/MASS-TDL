#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/bc_mpc/envelope_computer.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::TargetState;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::bc_mpc::EnvelopeComputer;
using mass_l3::m5::bc_mpc::EnvelopeRegion;

namespace {

// ===========================================================================
// Helpers
// ===========================================================================
// Default config matches the handoff §5.4.b constants.
EnvelopeComputer::Config default_config() {
  EnvelopeComputer::Config cfg;
  cfg.sigma_pos_m        = 50.0;   // [TBD-HAZID]
  cfg.rudder_slew_deg_s  = 2.5;    // [TBD-HAZID]
  cfg.takeover_latency_s = 2.0;    // [TBD-HAZID]
  return cfg;
}

// Own-ship at origin, heading north, 8 m/s, no initial turn.
TrajectoryPoint own_ship_north_8mps() {
  TrajectoryPoint p;
  p.x_m     = 100.0;   // arbitrary world position
  p.y_m     = 200.0;
  p.psi_rad = 0.0;     // heading north
  p.u_mps   = 8.0;
  p.v_mps   = 0.0;
  p.r_rad_s = 0.0;
  p.t_s     = 0.0;
  return p;
}

// Own-ship at rest (zero speed).
TrajectoryPoint own_ship_at_rest() {
  TrajectoryPoint p;
  p.x_m     = 0.0;
  p.y_m     = 0.0;
  p.psi_rad = 0.0;
  p.u_mps   = 0.0;
  p.v_mps   = 0.0;
  p.r_rad_s = 0.0;
  p.t_s     = 0.0;
  return p;
}

// Target ahead at given distance [m] (north of own ship, stationary).
TargetState target_ahead(double distance_m) {
  TargetState t;
  t.id        = 1;
  t.x_m       = 100.0 + distance_m;  // same x as own_ship_north_8mps
  t.y_m       = 200.0;               // same y as own_ship_north_8mps
  t.cog_rad   = 0.0;
  t.sog_mps   = 0.0;
  t.cpa_m     = distance_m;
  t.tcpa_s    = distance_m / 8.0;    // closing at own speed
  t.confidence = 1.0;
  return t;
}

// Target abeam at given lateral offset [m] (positive = east/starboard).
TargetState target_abeam(double lateral_m) {
  TargetState t;
  t.id        = 2;
  t.x_m       = 100.0;               // same x as own_ship_north_8mps
  t.y_m       = 200.0 + lateral_m;   // lateral offset
  t.cog_rad   = 0.0;
  t.sog_mps   = 0.0;
  t.cpa_m     = std::abs(lateral_m);
  t.tcpa_s    = 0.0;                 // already at CPA
  t.confidence = 1.0;
  return t;
}

// Typical vessel dynamics for a medium vessel.
constexpr double kRotMax_rad_s  = 0.2094;   // ~12 deg/s
constexpr double kDecelMax_mps2 = 0.08;     // [TBD-HAZID]
constexpr double kDt_s          = 0.5;
constexpr double kHorizon_s     = 60.0;     // 60 s Mid-MPC horizon

}  // namespace

// ===========================================================================
// Test 1 — At-rest envelope: zero speed produces a circle centred at origin
// ===========================================================================
// When the ship is stationary, the reachable envelope collapses to the
// position-uncertainty circle (sigma_pos_m radius).  Forward reach is zero;
// the polygon is a σ-pos circle regardless of rot_max and decel_max.
TEST(EnvelopeComputer, AtRestEnvelope_ZeroSpeed_Circle) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_at_rest();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  // Must not be empty.
  EXPECT_FALSE(env.empty);

  // Polygon must have at least 3 vertices (a circle approximation).
  EXPECT_GE(env.polygon.size(), 3u);

  // Max forward = sigma_pos_m (no forward motion).
  EXPECT_NEAR(env.max_forward_m, 50.0, 5.0);

  // Lateral extents are ±sigma_pos_m (circle).
  EXPECT_GT(env.max_lateral_stbd_m, 0.0);
  EXPECT_LT(env.max_lateral_port_m, 0.0);
  EXPECT_NEAR(std::abs(env.max_lateral_port_m),
              env.max_lateral_stbd_m, 5.0);
}

// ===========================================================================
// Test 2 — Turning envelope: nonzero speed produces a forward sector
// ===========================================================================
// At 8 m/s forward, with rot_max = 12 deg/s and a 60 s horizon, the ship
// can turn significantly.  The envelope must be a forward-reaching sector
// whose width reflects the achievable heading change.
TEST(EnvelopeComputer, TurningEnvelope_SectorShape) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  // Must not be empty.
  EXPECT_FALSE(env.empty);

  // Polygon must be non-degenerate.
  EXPECT_GE(env.polygon.size(), 3u);

  // Forward reach: with 8 m/s and 60 s, we expect a large forward extent.
  // Take-over latency (2 s) + 58 s effective horizon at 8 m/s => ~480 m.
  EXPECT_GT(env.max_forward_m, 200.0);
  EXPECT_LT(env.max_forward_m, 700.0);  // sanity upper bound

  // Lateral (starboard) reach must be nonzero — the ship CAN turn.
  EXPECT_GT(env.max_lateral_stbd_m, 1.0);

  // Port lateral reach is negative (symmetric envelope).
  EXPECT_LT(env.max_lateral_port_m, -1.0);

  // min_forward should be less than max_forward (deceleration works).
  EXPECT_LT(env.min_forward_m, env.max_forward_m);
}

// ===========================================================================
// Test 3 — Target ahead within envelope → inside
// ===========================================================================
// A target directly ahead at a distance inside the forward reach of the
// envelope must be reported as "inside" — the ship cannot avoid it by
// manoeuvring alone (needs BC-MPC escalation).
TEST(EnvelopeComputer, TargetInsideEnvelope_Ahead) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  ASSERT_FALSE(env.empty);

  // Target at 50 m ahead — well within the ~480 m forward reach.
  const TargetState tgt = target_ahead(50.0);

  const bool inside = comp.is_target_inside_envelope(
      env, tgt, own, 50.0);  // sigma_pos = 50 m

  EXPECT_TRUE(inside);
}

// ===========================================================================
// Test 4 — Target far away → outside envelope
// ===========================================================================
// A target far outside the reachable envelope should be reported as outside.
TEST(EnvelopeComputer, TargetOutsideEnvelope_FarAway) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  ASSERT_FALSE(env.empty);

  // Target at 2000 m ahead — well beyond the ~480 m reach.
  const TargetState tgt = target_ahead(2000.0);

  const bool inside = comp.is_target_inside_envelope(
      env, tgt, own, 50.0);

  EXPECT_FALSE(inside);
}

// ===========================================================================
// Test 5 — Target abeam (lateral) outside turning sector → outside
// ===========================================================================
// A target directly abeam at a large lateral distance should be outside
// the forward-facing sector envelope.
TEST(EnvelopeComputer, TargetOutsideEnvelope_Lateral) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  ASSERT_FALSE(env.empty);

  // Target at 600 m starboard (positive y in body frame = east for north heading).
  // With max_forward ≈ 480m and sigma=50m, the expanded lateral reach is at most
  // ~480 + 50 = 530m.  600m > 530m ensures the target is outside.
  const TargetState tgt = target_abeam(600.0);  // east of own ship

  const bool inside = comp.is_target_inside_envelope(
      env, tgt, own, 50.0);

  EXPECT_FALSE(inside);
}

// ===========================================================================
// Test 6 — High turn rate produces wider envelope (short horizon)
// ===========================================================================
// At a short horizon (10 s), rot_max differences are visible before the
// sector half-angle saturates at π/2.  Higher rot_max → wider envelope.
TEST(EnvelopeComputer, HigherTurnRate_WiderEnvelope) {
  EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  constexpr double kShortHorizon = 10.0;  // short enough that max_dpsi < π/2

  // Low turn rate: 0.05 rad/s ≈ 3 deg/s.
  const EnvelopeRegion env_low = comp.compute_envelope(
      own, 0.05, kDecelMax_mps2, kDt_s, kShortHorizon);

  // High turn rate: 0.5 rad/s ≈ 29 deg/s.
  const EnvelopeRegion env_high = comp.compute_envelope(
      own, 0.5, kDecelMax_mps2, kDt_s, kShortHorizon);

  ASSERT_FALSE(env_low.empty);
  ASSERT_FALSE(env_high.empty);

  // Higher turn rate must produce greater lateral reach.
  EXPECT_GT(env_high.max_lateral_stbd_m, env_low.max_lateral_stbd_m);
}

// ===========================================================================
// Test 7 — Higher deceleration reduces min_forward
// ===========================================================================
// A higher decel_max reduces the minimum forward reach (ship can stop
// sooner), which shrinks the rear of the envelope.
TEST(EnvelopeComputer, HigherDecel_ShorterMinForward) {
  EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  // Weak deceleration.
  const EnvelopeRegion env_weak = comp.compute_envelope(
      own, kRotMax_rad_s, 0.02, kDt_s, kHorizon_s);

  // Strong deceleration.
  const EnvelopeRegion env_strong = comp.compute_envelope(
      own, kRotMax_rad_s, 0.50, kDt_s, kHorizon_s);

  ASSERT_FALSE(env_weak.empty);
  ASSERT_FALSE(env_strong.empty);

  // Stronger deceleration reduces min_forward (ship can stop faster).
  EXPECT_LT(env_strong.min_forward_m, env_weak.min_forward_m);
}

// ===========================================================================
// Test 8 — Empty envelope on zero horizon
// ===========================================================================
TEST(EnvelopeComputer, ZeroHorizon_EmptyEnvelope) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, 0.0);

  EXPECT_TRUE(env.empty);
}

// ===========================================================================
// Test 9 — Empty envelope triggers "everything inside"
// ===========================================================================
// When the envelope is empty, is_target_inside_envelope must return true
// (no feasible manoeuvre exists — everything is unsafe).
TEST(EnvelopeComputer, EmptyEnvelope_EverythingInside) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, 0.0);

  ASSERT_TRUE(env.empty);

  // Even a far-away target is "inside" when no manoeuvre exists.
  const TargetState tgt = target_ahead(5000.0);
  const bool inside = comp.is_target_inside_envelope(
      env, tgt, own, 50.0);

  EXPECT_TRUE(inside);
}

// ===========================================================================
// Test 10 — Rudder slew limits heading change
// ===========================================================================
// With the same rot_max but different rudder slew rates, the envelope width
// should differ (higher slew → faster turn authority → wider envelope).
TEST(EnvelopeComputer, RudderSlew_LimitsEnvelopeWidth) {
  const TrajectoryPoint own = own_ship_north_8mps();

  EnvelopeComputer::Config cfg_fast = default_config();
  cfg_fast.rudder_slew_deg_s = 5.0;   // fast rudder
  EnvelopeComputer comp_fast(cfg_fast);

  EnvelopeComputer::Config cfg_slow = default_config();
  cfg_slow.rudder_slew_deg_s = 0.5;   // slow rudder
  EnvelopeComputer comp_slow(cfg_slow);

  const EnvelopeRegion env_fast = comp_fast.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, 15.0);  // short horizon to emphasize slew

  const EnvelopeRegion env_slow = comp_slow.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, 15.0);

  ASSERT_FALSE(env_fast.empty);
  ASSERT_FALSE(env_slow.empty);

  // Fast rudder slew → more heading change → wider envelope.
  EXPECT_GT(env_fast.max_lateral_stbd_m, env_slow.max_lateral_stbd_m);
}

// ===========================================================================
// Test 11 — Take-over latency reduces lateral reach (very short horizon)
// ===========================================================================
// Uses an 8 s horizon with 4 s latency so the effective horizon (4 s) is
// short enough that the sector does NOT saturate at π/2.
TEST(EnvelopeComputer, TakeoverLatency_ReducesLateralReach) {
  const TrajectoryPoint own = own_ship_north_8mps();

  constexpr double kVeryShort = 8.0;

  EnvelopeComputer::Config cfg_no_latency = default_config();
  cfg_no_latency.takeover_latency_s = 0.0;
  EnvelopeComputer comp_no_latency(cfg_no_latency);

  EnvelopeComputer::Config cfg_latency = default_config();
  cfg_latency.takeover_latency_s = 4.0;   // 4 s latency on 8 s horizon → 4 s effective
  EnvelopeComputer comp_latency(cfg_latency);

  const EnvelopeRegion env_no_lat = comp_no_latency.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kVeryShort);

  const EnvelopeRegion env_lat = comp_latency.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kVeryShort);

  ASSERT_FALSE(env_no_lat.empty);
  ASSERT_FALSE(env_lat.empty);

  // Forward reach is the same (total horizon unchanged: 8 s).
  EXPECT_NEAR(env_lat.max_forward_m, env_no_lat.max_forward_m, 1.0);

  // Lateral reach is smaller with latency (4 s less time to turn).
  EXPECT_LT(env_lat.max_lateral_stbd_m, env_no_lat.max_lateral_stbd_m);
}

// ===========================================================================
// Test 12 — Target at envelope boundary (edge case)
// ===========================================================================
// A target at the edge of the expanded envelope (just inside after sigma_pos
// expansion) must be detected; a target just outside must not.
// The polygon does NOT include sigma expansion; is_target_inside_envelope
// adds it.  max_forward_m ≈ speed * horizon = 8 * 60 = 480 m.
TEST(EnvelopeComputer, TargetAtEnvelopeBoundary_EdgeCase) {
  const EnvelopeComputer comp(default_config());
  const TrajectoryPoint own = own_ship_north_8mps();

  const EnvelopeRegion env = comp.compute_envelope(
      own, kRotMax_rad_s, kDecelMax_mps2, kDt_s, kHorizon_s);

  ASSERT_FALSE(env.empty);

  // forward edge of polygon ≈ 480 m.  sigma_pos = 50 m expands it.
  // Target at 480 + 50 + 20 = 550 m → beyond expansion → outside.
  const double far_dist = env.max_forward_m + 50.0 + 20.0;
  const TargetState tgt_far = target_ahead(far_dist);
  EXPECT_FALSE(comp.is_target_inside_envelope(env, tgt_far, own, 50.0));

  // Target at 480 + 20 = 500 m → inside the polygon itself → inside.
  const double near_dist = env.max_forward_m + 20.0;
  const TargetState tgt_near = target_ahead(near_dist);
  EXPECT_TRUE(comp.is_target_inside_envelope(env, tgt_near, own, 50.0));
}
