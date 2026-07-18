#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/constraint_compiler.hpp"

// ===========================================================================
// Test helpers
// ===========================================================================
namespace {

// Build a column MX symbolic variable of length N.
casadi::MX make_sym(const std::string& name, int32_t N) {
  return casadi::MX::sym(name, N, 1);
}

// Check whether a name exactly matches an element in names vector.
bool has_name(const std::vector<std::string>& names, const std::string& target) {
  return std::any_of(names.begin(), names.end(),
                     [&target](const std::string& n) { return n == target; });
}

// Check whether any name in the vector contains the given substring.
bool has_name_containing(const std::vector<std::string>& names,
                         const std::string& substr) {
  return std::any_of(names.begin(), names.end(),
                     [&substr](const std::string& n) {
                       return n.find(substr) != std::string::npos;
                     });
}

// Build a default ConstraintInputs with N=5 horizon.
mass_l3::m5::ConstraintInputs default_inputs() {
  mass_l3::m5::ConstraintInputs ci;
  ci.heading_min_rad   = -M_PI;
  ci.heading_max_rad   =  M_PI;
  ci.speed_min_mps     = 0.0;
  ci.speed_max_mps     = 15.0;
  ci.own_ship_psi_rad  = 0.0;
  return ci;
}

// Build a convex square polygon (CCW) around origin, half-size s.
mass_l3::m5::Polygon2D convex_square(double s) {
  return {
    Eigen::Vector2d{-s, -s},
    Eigen::Vector2d{ s, -s},
    Eigen::Vector2d{ s,  s},
    Eigen::Vector2d{-s,  s},
  };
}

// Build a simple non-convex polygon (L-shape, 6 vertices, CCW).
mass_l3::m5::Polygon2D nonconvex_l_shape() {
  return {
    Eigen::Vector2d{0.0, 0.0},
    Eigen::Vector2d{4.0, 0.0},
    Eigen::Vector2d{4.0, 2.0},
    Eigen::Vector2d{2.0, 2.0},
    Eigen::Vector2d{2.0, 4.0},
    Eigen::Vector2d{0.0, 4.0},
  };
}

}  // namespace

// ===========================================================================
// Test 1: Rule14_HeadOn_ConstraintPresent
// P5 T4: Rule 14 now emits only an audit marker (formulation-layer
// direction/min_alt provide the actual constraint).
// ===========================================================================
TEST(ConstraintCompilerTest, Rule14_HeadOn_ConstraintPresent) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {14u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  // P5 T4: the compiler emits only an audit marker.
  EXPECT_TRUE(has_name(result.names, "rule_14_side_via_formulation_direction"))
      << "Rule 14 must produce an audit marker named "
      << "'rule_14_side_via_formulation_direction'";
}

// ===========================================================================
// Test 1b: Rule14_HeadOn_NumericCorrectness
// P5 T4: Rule 14 audit marker is g=0 (trivially satisfied).
// ===========================================================================
TEST(ConstraintCompilerTest, Rule14_HeadOn_NumericCorrectness) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 4;
  casadi::MX psi_sym = casadi::MX::sym("psi", N, 1);
  casadi::MX u_sym   = casadi::MX::sym("u",   N, 1);

  mass_l3::m5::ConstraintInputs in = default_inputs();
  in.applicable_rules = {14u};

  const auto compiled = cc.compile(psi_sym, u_sym, in, 5.0, 0.1);

  casadi::Function f("f", std::vector<casadi::MX>{psi_sym, u_sym},
                         std::vector<casadi::MX>{compiled.g});

  // The audit marker is g=0, always satisfied.
  casadi::DM psi_val = casadi::DM::zeros(N, 1);
  casadi::DM u_val   = casadi::DM::ones(N, 1) * 5.0;

  const std::vector<casadi::DM> g_out =
      f(std::vector<casadi::DM>{psi_val, u_val});

  // Verify the marker exists and evaluates to 0.
  const auto it = std::find(compiled.names.begin(), compiled.names.end(),
                            "rule_14_side_via_formulation_direction");
  ASSERT_NE(it, compiled.names.end());
  const int64_t idx = std::distance(compiled.names.begin(), it);
  EXPECT_NEAR(static_cast<double>(g_out[0](idx)), 0.0, 1e-15)
      << "Rule 14 audit marker must evaluate to 0 (trivially satisfied). "
      << "The formulation-layer direction/min_alt provide the actual constraint.";
}

// ===========================================================================
// Test 2: Rule15_Crossing_GiveWayStarboard
// P5 T4: Rule 15 emits only an audit marker (formulation-layer provides the
// actual starboard-turn constraint via direction/min_alt).
// ===========================================================================
TEST(ConstraintCompilerTest, Rule15_Crossing_GiveWayStarboard) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {15u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  // P5 T4: single audit marker, NOT N per-step constraints.
  const int32_t rule15_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("rule_15_side_via") != std::string::npos;
                    }));
  EXPECT_EQ(rule15_count, 1)
      << "Rule 15 (crossing) must produce 1 audit marker (formulation-layer "
      << "direction/min_alt provide the actual per-step constraints)";
  EXPECT_TRUE(has_name(result.names, "rule_15_side_via_formulation_direction"))
      << "Rule 15 must produce an audit marker named "
      << "'rule_15_side_via_formulation_direction'";
}

// ===========================================================================
// Test 3: Rule16_GiveWay_SubstantialAction
// P5 T4: Rule 16 emits only an audit marker.
// ===========================================================================
TEST(ConstraintCompilerTest, Rule16_GiveWay_SubstantialAction) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 6;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {16u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  EXPECT_TRUE(has_name(result.names, "rule_16_side_via_formulation_direction"))
      << "Rule 16 must produce an audit marker named "
      << "'rule_16_side_via_formulation_direction'";
}

// ===========================================================================
// Test 4: Rule17_StandOn_SmallMotionBound
// P5 T4: Rule 17 emits only an audit marker.
// ===========================================================================
TEST(ConstraintCompilerTest, Rule17_StandOn_SmallMotionBound) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 4;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {17u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  EXPECT_TRUE(has_name(result.names, "rule_17_side_via_formulation_direction"))
      << "Rule 17 must produce an audit marker named "
      << "'rule_17_side_via_formulation_direction'";
}

// ===========================================================================
// Test 5: HeadingLowerBound_ConstraintCount
// Heading bounds → exactly 2*N constraints (N lower + N upper)
// ===========================================================================
TEST(ConstraintCompilerTest, HeadingBounds_ConstraintCount) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  // No rules, no zones — only heading + speed + ROT
  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  const int32_t heading_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("heading_") != std::string::npos;
                    }));
  EXPECT_EQ(heading_count, 2 * N)
      << "Heading bounds must produce exactly 2*N constraints";
}

// ===========================================================================
// Test 6: SpeedBounds_ConstraintCount
// Speed bounds → exactly 2*N constraints (N lower + N upper)
// ===========================================================================
TEST(ConstraintCompilerTest, SpeedBounds_ConstraintCount) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  const int32_t speed_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("speed_") != std::string::npos;
                    }));
  EXPECT_EQ(speed_count, 2 * N)
      << "Speed bounds must produce exactly 2*N constraints";
}

// ===========================================================================
// Test 7: RotLimit_ConstraintCount
// ROT limit → exactly N-1 constraints
// ===========================================================================
TEST(ConstraintCompilerTest, RotLimit_ConstraintCount) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  const int32_t rot_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("rot_limit") != std::string::npos;
                    }));
  EXPECT_EQ(rot_count, N - 1)
      << "ROT limit must produce exactly N-1 constraints";
}

// ===========================================================================
// Test 8: NonConvexPolygon_Decomposed
// 6-vertex non-convex (L-shape) polygon → decompose_polygon returns ≥ 2 sub-polygons
// ===========================================================================
TEST(ConstraintCompilerTest, NonConvexPolygon_Decomposed) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  const auto poly = nonconvex_l_shape();
  const auto result = cc.decompose_polygon(poly);
  EXPECT_GE(static_cast<int32_t>(result.size()), 2)
      << "Non-convex polygon must be decomposed into ≥ 2 sub-polygons";
}

// ===========================================================================
// Test 9: ConvexPolygon_NotDecomposed
// Convex polygon → decompose_polygon returns exactly 1 polygon
// ===========================================================================
TEST(ConstraintCompilerTest, ConvexPolygon_NotDecomposed) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  const auto poly = convex_square(10.0);
  const auto result = cc.decompose_polygon(poly);
  EXPECT_EQ(result.size(), 1u)
      << "Convex polygon must not be split (return exactly 1 polygon)";
  EXPECT_EQ(result[0].size(), poly.size())
      << "Returned polygon must be the original (same vertex count)";
}

// ===========================================================================
// Test 10: CompoundConstraints_Stacked
// Compile with Rules 14 + 15 + heading + speed + ROT → total g rows = sum
// ===========================================================================
TEST(ConstraintCompilerTest, CompoundConstraints_Stacked) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {14u, 15u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  // P5 T4: Rules 14 and 15 each produce 1 audit marker (2 total for both rules).
  // Expected: 2N heading + 2N speed + (N-1) rot + 2 audit markers
  const int32_t expected = 2 * N + 2 * N + (N - 1) + 2;
  EXPECT_EQ(static_cast<int32_t>(result.names.size()), expected)
      << "Stacked constraints must equal the sum of individual counts";
  EXPECT_EQ(static_cast<int32_t>(result.g.size1()), expected)
      << "g vector rows must match names count";
}

// ===========================================================================
// Test 11: EmptyTargets_NoRuleConstraints
// Empty applicable_rules → no COLREGs constraint rows in output
// ===========================================================================
TEST(ConstraintCompilerTest, EmptyTargets_NoRuleConstraints) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  const bool has_any_rule = has_name_containing(result.names, "rule_");
  EXPECT_FALSE(has_any_rule)
      << "No COLREGs rules → no rule_* constraints in output";
  // Baseline: 2N heading + 2N speed + (N-1) ROT only
  const int32_t expected_base = 2 * N + 2 * N + (N - 1);
  EXPECT_EQ(static_cast<int32_t>(result.names.size()), expected_base)
      << "Without rules, only heading + speed + ROT constraints present";
}

// ===========================================================================
// Test 12: ZoneConstraint_InsideConvexPolygon_Positive
// Point known to be inside a convex polygon → point_inside_convex value > 0
// ===========================================================================
TEST(ConstraintCompilerTest, ZoneConstraint_InsideConvexPolygon_Positive) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  // Convex square [-10, 10] x [-10, 10]
  const auto poly = convex_square(10.0);

  // Point (0, 0) is inside
  casadi::MX px = casadi::DM(0.0);
  casadi::MX py = casadi::DM(0.0);
  casadi::MX result = cc.point_inside_convex(px, py, poly);

  // Evaluate with no free variables → DM result
  casadi::DM val = casadi::DM(result);
  EXPECT_GT(static_cast<double>(val), 0.0)
      << "Origin inside square must yield positive constraint value";
}

// ===========================================================================
// Test 13: ZoneConstraint_OutsideConvexPolygon_Negative
// Point known to be outside → point_inside_convex value < 0
// ===========================================================================
TEST(ConstraintCompilerTest, ZoneConstraint_OutsideConvexPolygon_Negative) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  const auto poly = convex_square(10.0);

  // Point (20, 0) is outside
  casadi::MX px = casadi::DM(20.0);
  casadi::MX py = casadi::DM(0.0);
  casadi::MX result = cc.point_inside_convex(px, py, poly);

  casadi::DM val = casadi::DM(result);
  EXPECT_LT(static_cast<double>(val), 0.0)
      << "Point outside square must yield negative constraint value";
}

// ===========================================================================
// Test 13b: EmptyPolygon_NoConstraint
// decompose_polygon with empty polygon → returns {empty}, no crash.
// ===========================================================================
TEST(ConstraintCompilerTest, EmptyPolygon_NoConstraint) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  mass_l3::m5::Polygon2D empty;
  const auto result = cc.decompose_polygon(empty);
  EXPECT_EQ(result.size(), 1u) << "Empty polygon must return a vector of size 1";
}

// ===========================================================================
// Test 14: FullCompile_NoThrow
// compile() with all constraint types active → no exception, g is non-empty MX
// ===========================================================================
TEST(ConstraintCompilerTest, FullCompile_NoThrow) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 4;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {14u, 15u, 16u, 17u};

  // Add a zone constraint (convex square, stay inside)
  mass_l3::m5::ZoneConstraint zone;
  zone.polygon         = convex_square(5000.0);
  zone.must_stay_inside = true;
  zone.name            = "tss_lane_north";
  inputs.zone_constraints = {zone};

  EXPECT_NO_THROW({
    const auto result = cc.compile(psi, u, inputs, 5.0, 0.2);
    EXPECT_GT(static_cast<int32_t>(result.names.size()), 0)
        << "Full compile must produce at least one constraint";
    EXPECT_GT(result.g.size1(), 0)
        << "g must be non-empty after full compile";
    // Verify each COLREGs rule appeared in the active-set log.
    EXPECT_TRUE(std::any_of(result.names.begin(), result.names.end(),
        [](const std::string& n){ return n.find("rule_14") != std::string::npos; }))
        << "Rule 14 must appear in active-set log";
    EXPECT_TRUE(std::any_of(result.names.begin(), result.names.end(),
        [](const std::string& n){ return n.find("rule_15") != std::string::npos; }))
        << "Rule 15 must appear in active-set log";
  });
}

// ===========================================================================
// Test 15: HeadingExactlyAtLimitBound
// When psi = heading_max exactly, the upper bound constraint value = 0.
// Uses CasADi Function evaluation (not DM cast, which fails on Vertcat nodes).
// ===========================================================================
TEST(ConstraintCompilerTest, HeadingExactlyAtLimitBound) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 2;
  const double bound = M_PI / 4.0;  // 45°

  casadi::MX psi = make_sym("psi", N);
  casadi::MX u   = make_sym("u", N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.heading_min_rad   = -bound;
  inputs.heading_max_rad   =  bound;
  inputs.applicable_rules  = {};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.5);

  EXPECT_TRUE(has_name(result.names, "heading_upper[0]"))
      << "heading_upper[0] must be present";

  // Evaluate via CasADi Function at psi=[bound, 0], u=[5, 5]
  casadi::Function f("f", std::vector<casadi::MX>{psi, u},
                          std::vector<casadi::MX>{result.g});
  casadi::DM psi_val = casadi::DM::vertcat(
      std::vector<casadi::DM>{casadi::DM(bound), casadi::DM(0.0)});
  casadi::DM u_val   = casadi::DM::vertcat(
      std::vector<casadi::DM>{casadi::DM(5.0), casadi::DM(5.0)});
  const std::vector<casadi::DM> g_out =
      f(std::vector<casadi::DM>{psi_val, u_val});

  const auto it = std::find(result.names.begin(), result.names.end(),
                             "heading_upper[0]");
  const auto idx = static_cast<int32_t>(
      std::distance(result.names.begin(), it));

  // g_upper[0] = heading_max - psi[0] = bound - bound = 0
  EXPECT_NEAR(static_cast<double>(g_out[0](idx)), 0.0, 1.0e-9)
      << "Heading exactly at upper bound must produce g = 0";
}

// ===========================================================================
// Test 16: ZoneAvoid_OutsideConstraintPositive
// must_stay_inside=false: point outside polygon → constraint value > 0.
// Uses CasADi Function evaluation.
// ===========================================================================
TEST(ConstraintCompilerTest, ZoneAvoid_OutsideConstraintPositive) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 1;

  // Polygon around (100, 100) — ship at origin stays far outside
  mass_l3::m5::ZoneConstraint zone;
  zone.polygon = {
    Eigen::Vector2d{95.0, 95.0},
    Eigen::Vector2d{105.0, 95.0},
    Eigen::Vector2d{105.0, 105.0},
    Eigen::Vector2d{95.0, 105.0},
  };
  zone.must_stay_inside = false;  // avoid zone
  zone.name             = "avoid_zone";

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {};
  inputs.zone_constraints = {zone};

  // Symbolic variables so we can use CasADi Function evaluation
  casadi::MX psi = make_sym("psi", N);
  casadi::MX u   = make_sym("u",   N);

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.5);

  EXPECT_TRUE(has_name_containing(result.names, "avoid_zone"))
      << "Avoid zone must produce constraints named with 'avoid_zone'";

  // Evaluate at psi=0 (north), u=1 m/s, dt=1s → position (sin(0)*1, cos(0)*1) = (0,1)
  // (0,1) is OUTSIDE the (95..105, 95..105) polygon
  // g_k = -point_inside_convex (avoid zone) → -negative = positive
  casadi::Function f("f_avoid", std::vector<casadi::MX>{psi, u},
                                std::vector<casadi::MX>{result.g});
  casadi::DM psi_val = casadi::DM(0.0);
  casadi::DM u_val   = casadi::DM(1.0);
  const std::vector<casadi::DM> g_out =
      f(std::vector<casadi::DM>{psi_val, u_val});

  const auto it = std::find_if(result.names.begin(), result.names.end(),
                               [](const std::string& n) {
                                 return n.find("avoid_zone") != std::string::npos;
                               });
  ASSERT_NE(it, result.names.end());
  const auto idx = static_cast<int32_t>(
      std::distance(result.names.begin(), it));

  // Point (0,1) is outside (95-105) polygon → constraint > 0
  EXPECT_GT(static_cast<double>(g_out[0](idx)), 0.0)
      << "Avoid zone: point outside polygon → constraint value > 0";
}

// ===========================================================================
// Test 17: CompileCpaDistanceProducesBarrierConstraint
// CPA hard constraint g = d^2 - cpa_safe^2 uses one-sided [0, +inf] bounds.
// ===========================================================================
TEST(ConstraintCompilerTest, CompileCpaDistanceProducesBarrierConstraint) {
  mass_l3::m5::shared::ConstraintCompiler compiler;
  constexpr int32_t N = 4;
  casadi::MX psi = casadi::MX::sym("psi", N);
  casadi::MX u   = casadi::MX::sym("u", N);

  mass_l3::m5::ConstraintInputs inputs;
  inputs.cpa_safe_m = 1852.0;
  mass_l3::m5::TargetState tgt;
  tgt.x_m = 5000.0;
  tgt.y_m = 0.0;
  tgt.cog_rad = 0.0;
  tgt.sog_mps = 0.0;
  inputs.targets.push_back(tgt);
  inputs.own_ship_psi_rad = 0.0;

  const auto cc = compiler.compile_cpa_distance(psi, u, inputs, 5.0);

  EXPECT_GT(cc.g.size1(), 0) << "CPA distance constraint must be non-empty";
  EXPECT_EQ(cc.g.size1(), cc.g_lb.size1());
  EXPECT_EQ(cc.g.size1(), cc.g_ub.size1());
  for (int i = 0; i < cc.g_lb.size1(); ++i) {
    EXPECT_DOUBLE_EQ(static_cast<double>(cc.g_lb(i)), 0.0);
  }
  EXPECT_FALSE(cc.names.empty());
  EXPECT_EQ(cc.names.front().find("cpa_distance"), 0u);
}

// ===========================================================================
// Test O1.a: Rule13_Overtake_NotUnsupportedSentinel
// spec §7.2: Rule13 must be an explicitly-handled case, NOT the generic
// "colreg_unsupported_rule_N" default sentinel. Compile with {13u} → the
// active-set name must identify Rule13 (NOT "unsupported").
// ===========================================================================
TEST(ConstraintCompilerTest, Rule13_Overtake_NotUnsupportedSentinel) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {13u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);

  // Rule 13 must NOT be reported as an unsupported-rule placeholder.
  EXPECT_FALSE(has_name_containing(result.names, "colreg_unsupported_rule_13"))
      << "Rule 13 must be an explicit case, not the default unsupported sentinel";
  // It MUST be audit-visible (SAT-2: all requested rules appear in active-set log).
  EXPECT_TRUE(has_name_containing(result.names, "rule_13"))
      << "Rule 13 must be audit-visible in the active-set log";
}

// ===========================================================================
// Test O1.b: Rule13_Overtake_NoCompilerHeadingRow
// spec §7.2: "复用 §7.1 的 g_dir + g_minalt（Rule13 不额外加 heading-row）".
// Rule13 side + min_alt come from the FORMULATION-layer direction/min_alt rows
// (Slice D1, mid_mpc_nlp_formulation.cpp, role-gated via kIdxRole +
// kIdxPreferredDir). The constraint_compiler Rule13 case must therefore add NO
// heading-restriction row (unlike rule14/15 which emit starboard_turn rows).
//
// We verify: compile_colregs_rules for {13u} produces no "starboard_turn" /
// "substantial_action" / "stand_on" heading-type constraint, and the only
// Rule13 contribution is a trivially-satisfied audit marker.
// ===========================================================================
TEST(ConstraintCompilerTest, Rule13_Overtake_NoCompilerHeadingRow) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {13u};

  // Isolate the COLREGs rules (no heading/speed/ROT/zone) by calling the
  // dispatch directly.
  const auto result = cc.compile_colregs_rules(psi, u, inputs);

  // No compiler-level heading-restriction row of any other rule's flavour.
  EXPECT_FALSE(has_name_containing(result.names, "starboard_turn"))
      << "Rule 13 must not add a heading-restriction row (side from formulation)";
  EXPECT_FALSE(has_name_containing(result.names, "substantial_action"))
      << "Rule 13 must not add a Rule16-style heading row";
  EXPECT_FALSE(has_name_containing(result.names, "stand_on"))
      << "Rule 13 must not add a Rule17-style stand-on row";

  // It must be present (audit-visible) but trivial (g == 0 → satisfied).
  const auto it = std::find_if(
      result.names.begin(), result.names.end(),
      [](const std::string& n) { return n.find("rule_13") != std::string::npos; });
  ASSERT_NE(it, result.names.end())
      << "Rule 13 must contribute an audit-visible name";
  const auto idx = static_cast<int32_t>(std::distance(result.names.begin(), it));

  // Build a CasADi Function over the COLREGs g and evaluate at an arbitrary
  // trajectory — the Rule13 audit row must evaluate to 0 (trivially satisfied).
  casadi::Function f("f", std::vector<casadi::MX>{psi, u},
                          std::vector<casadi::MX>{result.g});
  casadi::DM psi_val = casadi::DM::zeros(N, 1);
  casadi::DM u_val   = casadi::DM::ones(N, 1) * 5.0;
  const std::vector<casadi::DM> g_out =
      f(std::vector<casadi::DM>{psi_val, u_val});

  EXPECT_NEAR(static_cast<double>(g_out[0](idx)), 0.0, 1.0e-9)
      << "Rule 13 compiler row must be trivially satisfied (g == 0); real "
      << "side/min_alt constraints live in the formulation-layer g_dir/g_minalt "
      << "rows (Slice D1, spec §7.1/§7.2).";
}

// ===========================================================================
// Test O1.c: Rule13_Overtake_SideFromM6PreferredDirection
// spec §7.2: "side 由 M6 preferred_direction 定（不默认 starboard）". The side
// decision is NOT baked into the constraint_compiler (which has no preferred-
// direction parameter); it is delegated to the formulation layer where
// preferred_direction (kIdxPreferredDir, ±1) drives g_dir = pref_dir·l[k].
//
// We verify the constraint_compiler delegation is side-agnostic: compiling
// Rule13 produces the SAME audit marker regardless of any give-way/starboard
// assumption — the compiler neither hardcodes starboard nor reads a side.
// (The port/stbd side itself is unit-tested in test_mid_mpc_direction.cpp,
// Slice D1.) Here we assert the compiler emits exactly ONE rule_13 marker and
// it carries a formulation-delegation name.
// ===========================================================================
TEST(ConstraintCompilerTest, Rule13_Overtake_SideFromM6PreferredDirection) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 4;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {13u};

  const auto result = cc.compile_colregs_rules(psi, u, inputs);

  // Exactly one Rule13 contribution (a single audit marker), not per-step.
  const int32_t rule13_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("rule_13") != std::string::npos;
                    }));
  EXPECT_EQ(rule13_count, 1)
      << "Rule 13 contributes a single audit marker; per-step side/min_alt rows "
      << "are emitted by the formulation layer (Slice D1), not the compiler.";

  // The marker name must declare that side is delegated to formulation
  // (preferred_direction), so a reader of the active-set log knows the overtake
  // side is NOT a hardcoded starboard assumption.
  EXPECT_TRUE(has_name_containing(result.names, "formulation"))
      << "Rule 13 audit marker must name the formulation layer as the side "
      << "source (preferred_direction), per spec §7.2.";
}

// ===========================================================================
// Test 18: ZoneIntegrationDirection_matchesCPACoordinatesForSameTrajectory
// spec §8.1: zone trajectory integration must match the CPA integration's NED
// convention (psi=0 → north = +x). CPA (compile_cpa_distance, :305-306) uses
//   cx += u*dt*cos(psi), cy += u*dt*sin(psi)   ← correct NED
// but zone (build_zone_steps, :447-448) had sin/cos swapped:
//   cum_x += u*dt*sin(psi), cum_y += u*dt*cos(psi)   ← BUG
//
// With psi=0 (north), u=5 m/s, dt=5s: each step moves +25 m north. The zone
// integrator accumulates position inclusive of step k, so at step k the ship is
// at x = 25*(k+1) along the north axis:
//   step 0 → x=25,  step 1 → x=50,  step 2 → x=75.
//   correct (cos/sin): position = (25*(k+1), 0)   (north axis grows)
//   bug     (sin/cos): position = (0, 25*(k+1))   (east axis grows instead)
//
// We place a thin polygon band centred on the NORTH (x) axis spanning x∈[40,120].
// At step 2 the ship is either at (75, 0) [correct → inside → g>0] or
// at (0, 75) [bug → outside → g<0]. This unambiguously distinguishes cos vs sin
// (sin(0)=0 → cum_x stays 0 → ship never reaches the north band).
// ===========================================================================
TEST(ZoneIntegrationDirection, matchesCPACoordinatesForSameTrajectory) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 3;
  casadi::MX psi_sym = casadi::MX::sym("psi", N, 1);
  casadi::MX u_sym   = casadi::MX::sym("u",   N, 1);

  // North-axis band: x ∈ [40, 120], y ∈ [-10, 10]. CCW (same winding as the
  // existing convex_square helper, which point_inside_convex treats as inside).
  // At step 2 the corrected ship position (75, 0) is centred in this band.
  mass_l3::m5::ZoneConstraint zone;
  zone.polygon = {
    Eigen::Vector2d{40.0,  -10.0},
    Eigen::Vector2d{120.0, -10.0},
    Eigen::Vector2d{120.0,  10.0},
    Eigen::Vector2d{40.0,   10.0},
  };
  zone.must_stay_inside = true;
  zone.name             = "north_band";

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules  = {};
  inputs.zone_constraints  = {zone};

  const auto zone_cc = cc.compile_zone_constraints(psi_sym, u_sym, inputs, 5.0);

  // Build a CasADi Function over the zone constraint vector for evaluation.
  casadi::Function fz("fz", std::vector<casadi::MX>{psi_sym, u_sym},
                            std::vector<casadi::MX>{zone_cc.g});

  // psi = 0 (north), u = 5 m/s, dt = 5 s → +25 m/step along north axis.
  casadi::DM psi_val = casadi::DM::zeros(N, 1);   // all north
  casadi::DM u_val   = casadi::DM::ones(N, 1) * 5.0;
  const std::vector<casadi::DM> z_out =
      fz(std::vector<casadi::DM>{psi_val, u_val});

  // step 2 (index 2): after fix ship is at (75, 0) → inside band → g >= 0.
  const auto it2 = std::find(zone_cc.names.begin(), zone_cc.names.end(),
                             "north_band_step[2]");
  ASSERT_NE(it2, zone_cc.names.end())
      << "zone compile must emit a 'north_band_step[2]' constraint";
  const auto idx2 = static_cast<int32_t>(
      std::distance(zone_cc.names.begin(), it2));

  const double g2 = static_cast<double>(z_out[0](idx2));
  EXPECT_GT(g2, 0.0)
      << "psi=0 (north) ship must reach (75,0) after 2 steps → inside band; "
      << "g2=" << g2 << " indicates the zone integration used the wrong trig "
      << "function (sin(psi=0)=0 keeps cum_x at the origin).";

  // Same psi/u fed to CPA integration must also place own-ship at (75, 0) at
  // step 2: a static target at (75, 0) yields d→0 → CPA g = d² - cpa_hard² is
  // strongly negative, proving BOTH integrators agree on the north position.
  mass_l3::m5::ConstraintInputs cpa_inputs;
  cpa_inputs.cpa_hard_m = 1852.0;
  mass_l3::m5::TargetState tgt;
  tgt.x_m     = 75.0;   // on the north axis the ship travels along
  tgt.y_m     = 0.0;
  tgt.cog_rad = 0.0;
  tgt.sog_mps = 0.0;    // static target
  cpa_inputs.targets.push_back(tgt);

  const auto cpa_cc = cc.compile_cpa_distance(psi_sym, u_sym, cpa_inputs, 5.0);
  casadi::Function fc("fc", std::vector<casadi::MX>{psi_sym, u_sym},
                            std::vector<casadi::MX>{cpa_cc.g});
  const std::vector<casadi::DM> c_out =
      fc(std::vector<casadi::DM>{psi_val, u_val});

  const auto cit = std::find(cpa_cc.names.begin(), cpa_cc.names.end(),
                             std::string("cpa_distance_t0_k2"));
  ASSERT_NE(cit, cpa_cc.names.end());
  const auto cidx = static_cast<int32_t>(
      std::distance(cpa_cc.names.begin(), cit));
  const double cg2 = static_cast<double>(c_out[0](cidx));
  // d² ≈ 0 at step 2 → g = d² − 1852² ≈ −3.43e6 (large negative).
  EXPECT_LT(cg2, -1.0e6)
      << "CPA integrator (reference NED) must also place ship at (75,0) at "
      << "step 2; cg2=" << cg2 << " (must be strongly negative for d≈0).";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
