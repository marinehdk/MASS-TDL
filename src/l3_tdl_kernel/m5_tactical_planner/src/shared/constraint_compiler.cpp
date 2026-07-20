// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/shared/constraint_compiler.hpp"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <casadi/casadi.hpp>
#include <algorithm>
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

// IPOPT convention: upper bound = +infinity for one-sided inequalities.
constexpr double kInf = std::numeric_limits<double>::infinity();

// CasADi MX fmin/fmax reduction identity values.
const casadi::DM kMxPosInf = casadi::DM(std::numeric_limits<double>::infinity());
const casadi::DM kMxNegInf = casadi::DM(-std::numeric_limits<double>::infinity());

// Build a name vector by repeating a base name N times with index suffix [k].
std::vector<std::string> make_names(const std::string& base, int32_t n) {
  std::vector<std::string> v;
  v.reserve(static_cast<std::size_t>(n));
  for (int32_t k = 0; k < n; ++k) {
    v.push_back(base + "[" + std::to_string(k) + "]");
  }
  return v;
}

bool is_convex_2d(const mass_l3::m5::Polygon2D& polygon) {
  if (polygon.size() < 4u) { return true; }
  const std::size_t n = polygon.size();
  for (std::size_t i = 0u; i < n; ++i) {
    const auto& p0 = polygon[i];
    const auto& p1 = polygon[(i + 1u) % n];
    const auto& p2 = polygon[(i + 2u) % n];
    const double dx1 = p1.x() - p0.x();
    const double dy1 = p1.y() - p0.y();
    const double dx2 = p2.x() - p1.x();
    const double dy2 = p2.y() - p1.y();
    const double cross = dx1 * dy2 - dy1 * dx2;
    if (cross < -1.0e-9) { return false; }
  }
  return true;
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
// compile_rule14/15/16/17 — REMOVED in P5 T4 (VR-04).
// These previously emitted hardcoded degree offsets (5°/5°/10°/5°).
// The M6 geometry rows (direction + min_alt) are now provided by the
// FORMULATION layer (Slice D1 in both acados and IPOPT paths):
//   g_dir[k]    = preferred_direction · l[k]                 (same-side)
//   g_minalt[k] = preferred_direction · (psi[k]-own_psi) - min_alt
// which are role-gated and M6-driven. The compiler-level hardcoded offsets
// were redundant and could conflict with the formulation-layer geometry.
// Each rule now emits a trivially-satisfied g=0 audit marker so the rule
// is SAT-2 visible in the active-set log (same pattern as Rule13).
// ===========================================================================

// ===========================================================================
// compile_colregs_rules() — dispatch to per-rule helpers
// ===========================================================================
ConstraintCompiler::CompiledConstraints
ConstraintCompiler::compile_colregs_rules(
    const casadi::MX& /*psi_seq*/,
    const casadi::MX& /*u_seq*/,
    const ConstraintInputs& inputs) const {
  // NOTE: psi_seq and u_seq are UNUSED since P5 T4 (VR-04). All COLREGs
  // constraints (direction/min_alt) are now provided by the FORMULATION
  // layer (Slice D1); the compiler emits only audit markers. Parameters
  // are retained for API compatibility with the IPOPT formulation caller.
  CompiledConstraints result{};
  (void)inputs;
  for (const uint8_t rule : inputs.applicable_rules) {
    CompiledConstraints cc{};
    switch (rule) {
      // -------------------------------------------------------------------
      // Rule 13 (Overtaking) — spec §7.2.
      // Rule13 does NOT add a compiler-level heading row. Its side + minimum-
      // alteration constraints are provided by the FORMULATION-layer direction/
      // min_alt g rows (Slice D1, §7.1):
      //   g_dir[k]    = preferred_direction · l[k]                 (same-side)
      //   g_minalt[k] = preferred_direction · (psi[k]-own_psi) - min_alt
      // which are role-gated (give-way, kIdxRole) and driven by M6
      // preferred_direction (kIdxPreferredDir, +1 stbd / -1 port). Rule13
      // give-way activates those same rows, so the overtake side follows M6's
      // preferred_direction (NOT a hardcoded starboard assumption, §7.2).
      //
      // Downgrade notice (§7.2 / §3.5): pass-astern / no-crossing-ahead /
      // side-release semantics are NOT covered by this spec (they depend on a
      // ship-domain model not yet in the kernel); only side + min_alt here.
      //
      // This case emits a single trivially-satisfied g=0 audit marker so the
      // rule is SAT-2 visible in the active-set log without duplicating the
      // formulation-layer constraints.
      // -------------------------------------------------------------------
	      case 13u:
	      case 14u:
	      case 15u:
	      case 16u:
	      case 17u: {
	        // Rules 13-17: all COLREGs side + min_alt constraints are provided
	        // by the FORMULATION layer (direction/min_alt rows, Slice D1).
	        // The compiler emits only an audit marker so the rule is SAT-2
	        // visible in the active-set log without duplicating constraints.
	        // See the Rule13 comment above for the full rationale.
	        const std::string rule_name = "rule_" + std::to_string(rule)
	            + "_side_via_formulation_direction";
	        CompiledConstraints marker;
	        marker.g     = casadi::DM(0.0);
	        marker.g_lb  = casadi::DM::zeros(1, 1);
	        marker.g_ub  = casadi::DM(kInf);
	        marker.names = {rule_name};
	        cc = marker;
	        break;
	      }
      default:
        // Unknown rule: produce a trivially satisfied g=0 sentinel so it appears
        // in the active-set log (SAT-2 audit trail requires all requested rules visible).
        {
          CompiledConstraints placeholder;
          placeholder.g     = casadi::DM(0.0);
          placeholder.g_lb  = casadi::DM::zeros(1, 1);
          placeholder.g_ub  = casadi::DM(kInf);
          placeholder.names = {"colreg_unsupported_rule_" + std::to_string(rule)};
          cc = placeholder;
        }
        break;
    }
    result = stack(std::move(result), cc);
  }
  return result;
}

// ===========================================================================
// compile_cpa_distance() — CPA constraint: d_k^2 - cpa_hard^2 + sigma >= 0
// Per (target, step). Target is constant-velocity from cog/sog.
  // Phase 3.1 (spec v2.3 §2.2): sigma (when non-empty) is added to every row,
  // making the feasible region non-empty by construction regardless of geometry.
  // Q4 (BL-15): sigma is only added to rows with k >= prefix_K. Prefix-stage
  // rows (k < prefix_K) keep the hard-only form d_k^2 - cpa_hard^2.
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_cpa_distance(
    const casadi::MX& psi_seq,
    const casadi::MX& u_seq,
    const ConstraintInputs& inputs,
    double dt_s,
    const casadi::MX& slack,
    int32_t prefix_K) const {
  const int32_t N  = static_cast<int32_t>(psi_seq.size1());
  const int32_t Nt = static_cast<int32_t>(inputs.targets.size());
  if (N < 1 || Nt < 1) { return {}; }

  const casadi::DM dt(dt_s);
  // Hard floor is cpa_hard_m (un-bumped shared floor), NOT cpa_safe_m — the node
  // bumps cpa_safe_m during conflict for SOFT cost-scaling only. Using the
  // bumped value here made the hard floor unreachable (target inside 2500 m →
  // Infeasible). Bug C deep, RC-C; spec committed-route-design-v2 §L84.
  const casadi::DM cpa_safe_sq(inputs.cpa_hard_m * inputs.cpa_hard_m);
  // Phase 3.1: slack must be either empty (legacy hard-only) or a scalar MX.
  // Per-target / per-step slack is [TBD-MULTI-SHIP]; current form is the
  // exact-penalty scalar shared across all rows.
  const bool slack_active = !slack.is_empty() && slack.size2() == 1;
  casadi::MX cx(0.0);
  casadi::MX cy(0.0);
  std::vector<casadi::MX> g_rows;
  std::vector<std::string> names;
  g_rows.reserve(static_cast<std::size_t>(N * Nt));
  names.reserve(static_cast<std::size_t>(N * Nt));

  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_seq(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_seq(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
    const double kdt = static_cast<double>(k) * dt_s;

    for (int32_t t = 0; t < Nt; ++t) {
      const auto& target = inputs.targets[static_cast<std::size_t>(t)];
      const double tx = target.x_m
          + target.sog_mps * std::cos(target.cog_rad) * kdt;
      const double ty = target.y_m
          + target.sog_mps * std::sin(target.cog_rad) * kdt;
      const casadi::MX dx = cx - casadi::DM(tx);
      const casadi::MX dy = cy - casadi::DM(ty);
      casadi::MX row = dx * dx + dy * dy - cpa_safe_sq;
      // Q4 (BL-15): σ only added to suffix rows (k >= prefix_K). Prefix-stage
      // rows keep the legacy hard-only form — they are already bounds-softened
      // by RowBoundConfig::apply_colreg_prefix_soften_, so σ in the expression
      // is both unnecessary (bounds are [-inf,+inf]) and harmful (σ is a single
      // scalar; a prefix violation absorbed by σ reduces slack headroom for
      // suffix rows, creating a false-negative fail-open).
      if (slack_active && k >= prefix_K) {
        row = row + slack;
      }
      g_rows.push_back(row);
      names.push_back("cpa_distance_t" + std::to_string(t)
                      + "_k" + std::to_string(k));
    }
  }

  const int32_t total = static_cast<int32_t>(g_rows.size());
  return {casadi::MX::vertcat(g_rows),
          casadi::DM::zeros(total, 1),
          casadi::DM::ones(total, 1) * kInf,
          names};
}

// ===========================================================================
// point_inside_convex() — minimum cross product over all edges (half-plane)
// ===========================================================================
casadi::MX ConstraintCompiler::point_inside_convex(
    const casadi::MX& px,
    const casadi::MX& py,
    const Polygon2D& polygon) const {
  if (polygon.size() < 3u) {
    // Degenerate polygon — treat as no constraint (trivially satisfied).
    return casadi::MX(0.0);
  }
  casadi::MX result(kMxPosInf);
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
// normalize_ccw() — ensure CCW orientation for point_inside_convex half-plane test.
// bg::correct may reorder; re-derive orientation from signed area.
// ===========================================================================
Polygon2D ConstraintCompiler::normalize_ccw(const Polygon2D& polygon) {
  double signed_area = 0.0;
  const std::size_t nv = polygon.size();
  for (std::size_t i = 0u; i < nv; ++i) {
    const auto& a = polygon[i];
    const auto& b = polygon[(i + 1u) % nv];
    signed_area += (a.x() * b.y() - b.x() * a.y());
  }
  // Negative signed area means CW. Provide a CCW copy for triangulation.
  if (signed_area < 0.0) {
    Polygon2D ccw = polygon;
    std::reverse(ccw.begin(), ccw.end());
    return ccw;
  }
  return polygon;
}

// ===========================================================================
// decompose_polygon() — convexity check, then fan triangulation from centroid
// ===========================================================================
std::vector<Polygon2D> ConstraintCompiler::decompose_polygon(
    const Polygon2D& polygon) const {
  if (polygon.size() < 3u) { return {polygon}; }

  // Ensure CCW orientation for point_inside_convex half-plane test.
  const Polygon2D ccw_polygon = normalize_ccw(polygon);

  if (is_convex_2d(ccw_polygon)) { return {ccw_polygon}; }

  // Fan triangulation from centroid
  Eigen::Vector2d centroid = Eigen::Vector2d::Zero();
  for (const auto& v : ccw_polygon) { centroid += v; }
  centroid /= static_cast<double>(ccw_polygon.size());

  const std::size_t nv = ccw_polygon.size();
  std::vector<Polygon2D> result;
  result.reserve(nv);
  for (std::size_t i = 0u; i < nv; ++i) {
    result.push_back({centroid, ccw_polygon[i], ccw_polygon[(i + 1u) % nv]});
  }
  return result;
}

// ===========================================================================
// compile_zone_constraints() — polygon containment over trajectory positions
//
// Phase E1 simplified trajectory (origin = own ship at k=0), NED convention
// (psi=0 → north = +x). Must match compile_cpa_distance (:305-306):
//   x[k] = sum_{j=0}^{k} u[j]*dt*cos(psi[j])   (NED: x=north)
//   y[k] = sum_{j=0}^{k} u[j]*dt*sin(psi[j])   (NED: y=east)
// spec §8.1: prior sin/cos swap placed north-heading ships on the east axis.
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
    cum_x = cum_x + u_k * casadi::DM(dt_s) * casadi::MX::cos(psi_k);
    cum_y = cum_y + u_k * casadi::DM(dt_s) * casadi::MX::sin(psi_k);

    // Union of sub-polygons: point is inside union if inside any sub-polygon
    casadi::MX best(kMxNegInf);
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
