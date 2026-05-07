// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/shared/constraint_compiler.hpp"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <casadi/casadi.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mass_l3::m5::shared {

// ===========================================================================
// Anonymous namespace: constants + Boost type aliases
// ===========================================================================
namespace {

namespace bg = boost::geometry;
using BgPoint   = bg::model::d2::point_xy<double>;
using BgPolygon = bg::model::polygon<BgPoint>;

// [TBD-HAZID] Rule 14/15 minimum starboard turn angle [rad].
// Default 5°; calibrate from encounter simulation data (HAZID RUN-001 WP-05).
constexpr double kRule1415_min_turn_rad = 5.0 * M_PI / 180.0;

// [TBD-HAZID] Rule 16 substantial action threshold [rad] within first N/2 steps.
// Default 10°; calibrate from encounter simulation data (HAZID RUN-001 WP-05).
constexpr double kRule16_substantial_rad = 10.0 * M_PI / 180.0;

// [TBD-HAZID] Rule 17 stand-on epsilon [rad]. Default 5°.
// Calibrate per ODD domain and sea state (HAZID RUN-001 WP-05).
constexpr double kRule17_epsilon_rad = 5.0 * M_PI / 180.0;

// IPOPT convention: upper bound = +infinity for one-sided inequalities.
constexpr double kInf = std::numeric_limits<double>::infinity();

// Build a name vector by repeating a base name N times with index suffix [k].
std::vector<std::string> make_names(const std::string& base, int32_t n) {
  std::vector<std::string> v;
  v.reserve(static_cast<std::size_t>(n));
  for (int32_t k = 0; k < n; ++k) {
    v.push_back(base + "[" + std::to_string(k) + "]");
  }
  return v;
}

}  // namespace

// ===========================================================================
// stack() — vertically concatenate two CompiledConstraints
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::stack(
    CompiledConstraints a, const CompiledConstraints& b) const {
  if (b.names.empty()) { return a; }
  if (a.names.empty()) { return b; }

  a.g    = casadi::MX::vertcat({a.g, b.g});
  a.g_lb = casadi::DM::vertcat({a.g_lb, b.g_lb});
  a.g_ub = casadi::DM::vertcat({a.g_ub, b.g_ub});
  a.names.insert(a.names.end(), b.names.begin(), b.names.end());
  return a;
}

// ===========================================================================
// compile_heading_bounds() — N lower + N upper constraints
// ===========================================================================
ConstraintCompiler::CompiledConstraints
ConstraintCompiler::compile_heading_bounds(
    const casadi::MX& psi_seq,
    double heading_min_rad,
    double heading_max_rad) const {
  const int32_t N = static_cast<int32_t>(psi_seq.size1());

  // psi[k] - heading_min >= 0
  casadi::MX g_lower = psi_seq - casadi::DM(heading_min_rad);
  // heading_max - psi[k] >= 0
  casadi::MX g_upper = casadi::DM(heading_max_rad) - psi_seq;

  casadi::MX g_all = casadi::MX::vertcat({g_lower, g_upper});
  const int32_t total = 2 * N;
  casadi::DM lb = casadi::DM::zeros(total, 1);
  casadi::DM ub = casadi::DM::ones(total, 1) * kInf;

  auto names_lo = make_names("heading_lower", N);
  auto names_hi = make_names("heading_upper", N);
  names_lo.insert(names_lo.end(), names_hi.begin(), names_hi.end());

  return {g_all, lb, ub, names_lo};
}

// ===========================================================================
// compile_speed_bounds() — N lower + N upper constraints
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_speed_bounds(
    const casadi::MX& u_seq,
    double speed_min_mps,
    double speed_max_mps) const {
  const int32_t N = static_cast<int32_t>(u_seq.size1());

  casadi::MX g_lower = u_seq - casadi::DM(speed_min_mps);
  casadi::MX g_upper = casadi::DM(speed_max_mps) - u_seq;

  casadi::MX g_all = casadi::MX::vertcat({g_lower, g_upper});
  const int32_t total = 2 * N;
  casadi::DM lb = casadi::DM::zeros(total, 1);
  casadi::DM ub = casadi::DM::ones(total, 1) * kInf;

  auto names_lo = make_names("speed_lower", N);
  auto names_hi = make_names("speed_upper", N);
  names_lo.insert(names_lo.end(), names_hi.begin(), names_hi.end());

  return {g_all, lb, ub, names_lo};
}

// ===========================================================================
// compile_rot_limit() — N-1 rate-of-turn constraints
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_rot_limit(
    const casadi::MX& psi_seq,
    double dt_s,
    double rot_max_rad_s) const {
  const int32_t N = static_cast<int32_t>(psi_seq.size1());
  if (N < 2) { return {}; }

  casadi::MX delta = psi_seq(casadi::Slice(1, N)) -
                     psi_seq(casadi::Slice(0, N - 1));

  // rot_max * dt - |delta_psi| >= 0
  casadi::MX g_rot =
      casadi::DM(rot_max_rad_s * dt_s) - casadi::MX::abs(delta);

  const int32_t n_c = N - 1;
  casadi::DM lb = casadi::DM::zeros(n_c, 1);
  casadi::DM ub = casadi::DM::ones(n_c, 1) * kInf;

  return {g_rot, lb, ub, make_names("rot_limit", n_c)};
}

// ===========================================================================
// compile_rule14() — Rule 14 (Head-on): psi[N-1] >= psi_0 + 5°
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_rule14(
    const casadi::MX& psi_seq, double psi_initial_rad) const {
  const int32_t N = static_cast<int32_t>(psi_seq.size1());
  casadi::MX psi_last = psi_seq(casadi::Slice(N - 1, N));
  casadi::MX g = psi_last - casadi::DM(psi_initial_rad + kRule1415_min_turn_rad);
  casadi::DM lb = casadi::DM::zeros(1, 1);
  casadi::DM ub = casadi::DM(kInf);
  return {g, lb, ub, {"rule_14_starboard_turn"}};
}

// ===========================================================================
// compile_rule15() — Rule 15 (Crossing): psi[N-1] >= psi_0 + 5°
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_rule15(
    const casadi::MX& psi_seq, double psi_initial_rad) const {
  const int32_t N = static_cast<int32_t>(psi_seq.size1());
  casadi::MX psi_last = psi_seq(casadi::Slice(N - 1, N));
  casadi::MX g = psi_last - casadi::DM(psi_initial_rad + kRule1415_min_turn_rad);
  casadi::DM lb = casadi::DM::zeros(1, 1);
  casadi::DM ub = casadi::DM(kInf);
  return {g, lb, ub, {"rule_15_starboard_turn"}};
}

// ===========================================================================
// compile_rule16() — Rule 16 (Give-way): psi[N/2] >= psi_0 + 10°
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_rule16(
    const casadi::MX& psi_seq, double psi_initial_rad) const {
  const int32_t N   = static_cast<int32_t>(psi_seq.size1());
  const int32_t mid = (N / 2 > 0) ? (N / 2) : 0;
  casadi::MX psi_mid = psi_seq(casadi::Slice(mid, mid + 1));
  casadi::MX g = psi_mid - casadi::DM(psi_initial_rad + kRule16_substantial_rad);
  casadi::DM lb = casadi::DM::zeros(1, 1);
  casadi::DM ub = casadi::DM(kInf);
  return {g, lb, ub, {"rule_16_substantial_action"}};
}

// ===========================================================================
// compile_rule17() — Rule 17 (Stand-on): |psi[k] - psi_0| <= 5° for all k
// Produces 2*N constraints (upper and lower bound per step).
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_rule17(
    const casadi::MX& psi_seq, double psi_initial_rad) const {
  const int32_t N = static_cast<int32_t>(psi_seq.size1());
  const casadi::DM eps(kRule17_epsilon_rad);
  casadi::MX dpsi = psi_seq - casadi::DM(psi_initial_rad);

  // eps - dpsi >= 0  (upper bound: psi[k] - psi_0 <= eps)
  casadi::MX g_ub = eps - dpsi;
  // dpsi + eps >= 0  (lower bound: psi[k] - psi_0 >= -eps)
  casadi::MX g_lb = dpsi + eps;

  casadi::MX g_all  = casadi::MX::vertcat({g_ub, g_lb});
  const int32_t total = 2 * N;
  casadi::DM lb     = casadi::DM::zeros(total, 1);
  casadi::DM ub_dm  = casadi::DM::ones(total, 1) * kInf;

  auto names_ub = make_names("rule_17_stand_on_bound_upper", N);
  auto names_lb = make_names("rule_17_stand_on_bound_lower", N);
  names_ub.insert(names_ub.end(), names_lb.begin(), names_lb.end());
  return {g_all, lb, ub_dm, names_ub};
}

// ===========================================================================
// compile_colregs_rules() — dispatch to per-rule helpers
// ===========================================================================
ConstraintCompiler::CompiledConstraints
ConstraintCompiler::compile_colregs_rules(
    const casadi::MX& psi_seq,
    const casadi::MX& /*u_seq*/,
    const ConstraintInputs& inputs) const {
  CompiledConstraints result{};
  const double psi_0 = inputs.own_ship_psi_rad;
  for (const uint8_t rule : inputs.applicable_rules) {
    CompiledConstraints cc{};
    switch (rule) {
      case 14u: cc = compile_rule14(psi_seq, psi_0); break;
      case 15u: cc = compile_rule15(psi_seq, psi_0); break;
      case 16u: cc = compile_rule16(psi_seq, psi_0); break;
      case 17u: cc = compile_rule17(psi_seq, psi_0); break;
      default:  break;  // unknown rule: skip silently
    }
    result = stack(std::move(result), cc);
  }
  return result;
}

// ===========================================================================
// point_inside_convex() — minimum cross product over all edges (half-plane)
// ===========================================================================
casadi::MX ConstraintCompiler::point_inside_convex(
    const casadi::MX& px,
    const casadi::MX& py,
    const Polygon2D& polygon) const {
  casadi::MX result(1.0e6);
  const auto n = static_cast<int32_t>(polygon.size());
  for (int32_t i = 0; i < n; ++i) {
    const Eigen::Vector2d& v0 = polygon[static_cast<std::size_t>(i)];
    const Eigen::Vector2d& v1 = polygon[static_cast<std::size_t>((i + 1) % n)];
    const casadi::DM ex(v1.x() - v0.x());
    const casadi::DM ey(v1.y() - v0.y());
    const casadi::MX rx = px - casadi::DM(v0.x());
    const casadi::MX ry = py - casadi::DM(v0.y());
    // CCW polygon: cross = ex*ry - ey*rx >= 0 means point is to the left of edge
    casadi::MX cross_val = ex * ry - ey * rx;
    result = casadi::MX::fmin(result, cross_val);
  }
  return result;
}

// ===========================================================================
// decompose_polygon() — convexity check, then fan triangulation from centroid
// ===========================================================================
std::vector<Polygon2D> ConstraintCompiler::decompose_polygon(
    const Polygon2D& polygon) const {
  if (polygon.size() < 3u) { return {polygon}; }

  BgPolygon bg_poly;
  for (const auto& v : polygon) {
    bg::append(bg_poly.outer(), BgPoint(v.x(), v.y()));
  }
  bg::correct(bg_poly);

  if (bg::is_convex(bg_poly)) { return {polygon}; }

  // Fan triangulation from centroid
  Eigen::Vector2d centroid = Eigen::Vector2d::Zero();
  for (const auto& v : polygon) { centroid += v; }
  centroid /= static_cast<double>(polygon.size());

  const std::size_t n = polygon.size();
  std::vector<Polygon2D> result;
  result.reserve(n);
  for (std::size_t i = 0u; i < n; ++i) {
    result.push_back({centroid, polygon[i], polygon[(i + 1u) % n]});
  }
  return result;
}

// ===========================================================================
// compile_zone_constraints() — polygon containment over trajectory positions
//
// Phase E1 simplified trajectory (origin = own ship at k=0):
//   x[k] = sum_{j=0}^{k} u[j]*dt*sin(psi[j])   (NED: x=north)
//   y[k] = sum_{j=0}^{k} u[j]*dt*cos(psi[j])   (NED: y=east)
// ===========================================================================
ConstraintCompiler::CompiledConstraints
ConstraintCompiler::compile_zone_constraints(
    const casadi::MX& psi_seq,
    const casadi::MX& u_seq,
    const ConstraintInputs& inputs,
    double dt_s) const {
  if (inputs.zone_constraints.empty()) { return {}; }
  const int32_t N = static_cast<int32_t>(psi_seq.size1());
  CompiledConstraints result{};

  for (const ZoneConstraint& zone : inputs.zone_constraints) {
    const auto sub_polygons = decompose_polygon(zone.polygon);
    result = stack(std::move(result),
                   build_zone_steps(psi_seq, u_seq, zone, sub_polygons, N, dt_s));
  }
  return result;
}

// ===========================================================================
// build_zone_steps() — per-zone, per-step constraint accumulation
// Private helper extracted to stay within 60-line limit.
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::build_zone_steps(
    const casadi::MX& psi_seq,
    const casadi::MX& u_seq,
    const ZoneConstraint& zone,
    const std::vector<Polygon2D>& sub_polygons,
    int32_t N,
    double dt_s) const {
  CompiledConstraints result{};
  casadi::MX cum_x(0.0);
  casadi::MX cum_y(0.0);

  for (int32_t k = 0; k < N; ++k) {
    casadi::MX psi_k = psi_seq(casadi::Slice(k, k + 1));
    casadi::MX u_k   = u_seq(casadi::Slice(k, k + 1));
    cum_x = cum_x + u_k * casadi::DM(dt_s) * casadi::MX::sin(psi_k);
    cum_y = cum_y + u_k * casadi::DM(dt_s) * casadi::MX::cos(psi_k);

    // Union of sub-polygons: point is inside union if inside any sub-polygon
    casadi::MX best(-1.0e6);
    for (const Polygon2D& sub : sub_polygons) {
      casadi::MX inside = point_inside_convex(cum_x, cum_y, sub);
      best = casadi::MX::fmax(best, inside);
    }
    casadi::MX g_k = zone.must_stay_inside ? best : -best;

    CompiledConstraints cc_k;
    cc_k.g     = g_k;
    cc_k.g_lb  = casadi::DM::zeros(1, 1);
    cc_k.g_ub  = casadi::DM(kInf);
    cc_k.names = {zone.name + "_step[" + std::to_string(k) + "]"};
    result = stack(std::move(result), cc_k);
  }
  return result;
}

// ===========================================================================
// compile() — top-level dispatcher
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile(
    const casadi::MX& psi_seq,
    const casadi::MX& u_seq,
    const ConstraintInputs& inputs,
    double dt_s,
    double rot_max_rad_s) const {
  CompiledConstraints result = compile_heading_bounds(
      psi_seq, inputs.heading_min_rad, inputs.heading_max_rad);

  result = stack(std::move(result),
                 compile_speed_bounds(u_seq, inputs.speed_min_mps,
                                      inputs.speed_max_mps));
  result = stack(std::move(result),
                 compile_rot_limit(psi_seq, dt_s, rot_max_rad_s));
  result = stack(std::move(result),
                 compile_colregs_rules(psi_seq, u_seq, inputs));
  result = stack(std::move(result),
                 compile_zone_constraints(psi_seq, u_seq, inputs, dt_s));

  return result;
}

}  // namespace mass_l3::m5::shared
