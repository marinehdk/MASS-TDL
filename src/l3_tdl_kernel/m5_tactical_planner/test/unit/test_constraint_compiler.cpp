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
// Compile Rule 14 constraint → names contain "rule_14_starboard_turn"
// ===========================================================================
TEST(ConstraintCompilerTest, Rule14_HeadOn_ConstraintPresent) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {14u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  EXPECT_TRUE(has_name(result.names, "rule_14_starboard_turn"))
      << "Rule 14 must produce a constraint named 'rule_14_starboard_turn'";
}

// ===========================================================================
// Test 1b: Rule14_HeadOn_NumericCorrectness
// Evaluate Rule 14 constraint at psi[N-1] = psi_0 + 10° → constraint g >= 0.
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

  // psi[3] = psi_0 + 10deg satisfies Rule 14 (min turn is 5deg)
  casadi::DM psi_val = casadi::DM::zeros(N, 1);
  psi_val(3)         = in.own_ship_psi_rad + 10.0 * M_PI / 180.0;
  casadi::DM u_val   = casadi::DM::ones(N, 1) * 5.0;

  const std::vector<casadi::DM> g_out =
      f(std::vector<casadi::DM>{psi_val, u_val});

  const auto it = std::find(compiled.names.begin(), compiled.names.end(),
                            "rule_14_starboard_turn");
  ASSERT_NE(it, compiled.names.end());
  const int64_t idx = std::distance(compiled.names.begin(), it);
  EXPECT_GE(static_cast<double>(g_out[0](idx)), 0.0)
      << "Rule 14: psi[N-1] = psi_0 + 10deg must satisfy constraint (g >= 0)";
}

// ===========================================================================
// Test 2: Rule15_Crossing_GiveWayStarboard
// Compile Rule 15 constraint → produces N constraints (one per step), not 1.
// After fix: Rule 15 constrains psi[k] >= psi_0 + 5° for ALL k in [0..N-1].
// ===========================================================================
TEST(ConstraintCompilerTest, Rule15_Crossing_GiveWayStarboard) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 5;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {15u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  // Rule 15 must produce N per-step constraints named "rule_15_starboard_turn[k]"
  const int32_t rule15_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("rule_15_starboard_turn") != std::string::npos;
                    }));
  EXPECT_EQ(rule15_count, N)
      << "Rule 15 (crossing) must produce N per-step constraints, not 1";
  EXPECT_TRUE(has_name(result.names, "rule_15_starboard_turn[0]"))
      << "Rule 15 must produce a constraint named 'rule_15_starboard_turn[0]'";
  EXPECT_TRUE(has_name(result.names, "rule_15_starboard_turn[4]"))
      << "Rule 15 must produce a constraint named 'rule_15_starboard_turn[4]'";
}

// ===========================================================================
// Test 3: Rule16_GiveWay_SubstantialAction
// Compile Rule 16 → names contain "rule_16_substantial_action"
// ===========================================================================
TEST(ConstraintCompilerTest, Rule16_GiveWay_SubstantialAction) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 6;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {16u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  EXPECT_TRUE(has_name(result.names, "rule_16_substantial_action"))
      << "Rule 16 must produce a constraint named 'rule_16_substantial_action'";
}

// ===========================================================================
// Test 4: Rule17_StandOn_SmallMotionBound
// Compile Rule 17 → names contain "rule_17_stand_on_bound" (any step)
// ===========================================================================
TEST(ConstraintCompilerTest, Rule17_StandOn_SmallMotionBound) {
  mass_l3::m5::shared::ConstraintCompiler cc;
  constexpr int32_t N = 4;
  auto psi = make_sym("psi", N);
  auto u   = make_sym("u",   N);

  mass_l3::m5::ConstraintInputs inputs = default_inputs();
  inputs.applicable_rules = {17u};

  const auto result = cc.compile(psi, u, inputs, 1.0, 0.1);
  EXPECT_TRUE(has_name_containing(result.names, "rule_17_stand_on_bound"))
      << "Rule 17 must produce constraints containing 'rule_17_stand_on_bound'";
  // 2*N constraints (upper + lower per step)
  const int32_t rule17_count = static_cast<int32_t>(
      std::count_if(result.names.begin(), result.names.end(),
                    [](const std::string& n) {
                      return n.find("rule_17_stand_on_bound") != std::string::npos;
                    }));
  EXPECT_EQ(rule17_count, 2 * N)
      << "Rule 17 must produce 2*N stand-on constraints";
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

  // Expected: 2N heading + 2N speed + (N-1) rot + 1 rule14 + N rule15
  // (Rule 15 after fix produces N per-step constraints, Rule 14 produces 1)
  const int32_t expected = 2 * N + 2 * N + (N - 1) + 1 + N;
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
