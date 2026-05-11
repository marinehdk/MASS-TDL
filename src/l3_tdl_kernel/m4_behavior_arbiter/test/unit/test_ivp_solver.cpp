#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"

namespace mass_l3::m4 {

class IvPSolverTest : public ::testing::Test {
 protected:
  using WF = IvPCombinationStrategy::WeightedFunction;

  void SetUp() override {
    auto strategy = std::make_unique<WeightedSumCombination>();
    solver_ = std::make_unique<IvPSolver>(
        IvPHeadingDomain(1.0),
        IvPSpeedDomain(0.0, 20.0, 0.5),
        std::move(strategy),
        std::chrono::milliseconds(100));
  }

  IvPHardConstraints make_unconstrained() const {
    IvPHardConstraints c;
    c.heading_allowed_ranges_deg = {};
    c.speed_min_kn = 0.0;
    c.speed_max_kn = 20.0;
    c.targets = {};
    c.cpa_safe_m = 1852.0;
    c.rot_max_deg_s = 3.0;
    return c;
  }

  std::unique_ptr<IvPSolver> solver_;
};

// U1: Single behavior, no constraint → solution in [85°, 95°]
TEST_F(IvPSolverTest, SingleBehaviorNoConstraint) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns{{1.0, f}};
  auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  EXPECT_NEAR(sol->heading_min_deg, 85.0, 2.0);
  EXPECT_NEAR(sol->heading_max_deg, 95.0, 2.0);
}

// U2: Two compatible behaviors → weighted sum, solution in overlap region
TEST_F(IvPSolverTest, TwoCompatibleBehaviorsWeightedSum) {
  IvPFunctionDefault transit_f;
  ASSERT_EQ(transit_f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  IvPFunctionDefault colreg_f;
  ASSERT_EQ(colreg_f.set_pieces({{80.0, 100.0, 12.0, 18.0, 0.8}}), ErrorCode::Ok);

  std::vector<WF> fns{{2.0, transit_f}, {1.0, colreg_f}};
  auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  // Overlap [85°, 95°] × [14, 16] has highest aggregated utility (2.0 + 0.8 = 2.8)
  EXPECT_NEAR(sol->heading_min_deg, 85.0, 2.0);
  EXPECT_NEAR(sol->heading_max_deg, 95.0, 2.0);
}

// U3: Infeasible speed constraint → nullopt
TEST_F(IvPSolverTest, InfeasibleSpeedConstraintReturnsNullopt) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns{{1.0, f}};
  IvPHardConstraints c = make_unconstrained();
  c.speed_min_kn = 18.0;
  c.speed_max_kn = 0.0;  // min > max → infeasible

  auto sol = solver_->solve(fns, c);
  EXPECT_FALSE(sol.has_value());
}

// U3b: Empty behavior set → nullopt
TEST_F(IvPSolverTest, EmptyBehaviorSetReturnsNullopt) {
  std::vector<WF> fns{};
  auto sol = solver_->solve(fns, make_unconstrained());
  EXPECT_FALSE(sol.has_value());
}

// U4: Heading wrap-around (350°→10°) → solution contains wrap-around region
TEST_F(IvPSolverTest, HeadingWrapAround) {
  IvPFunctionDefault f;
  // Two pieces covering the 350°→10° wrap region
  ASSERT_EQ(f.set_pieces({
      {350.0, 359.0, 0.0, 20.0, 1.0},
      {0.0,   10.0,  0.0, 20.0, 1.0},
  }), ErrorCode::Ok);

  std::vector<WF> fns{{1.0, f}};
  auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  // The solution interval spans the high-utility region around 0°/360° boundary
  EXPECT_TRUE((sol->heading_min_deg >= 350.0 && sol->heading_max_deg < 360.0)
           || (sol->heading_min_deg < 10.0));
}

// U5: Heading constraint narrows solution
TEST_F(IvPSolverTest, HeadingConstraintNarrowsSolution) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{0.0, 180.0, 5.0, 15.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns{{1.0, f}};
  IvPHardConstraints c = make_unconstrained();
  // Only allow headings in [80°, 100°] — within the piece
  c.heading_allowed_ranges_deg = {{80.0, 100.0}};

  auto sol = solver_->solve(fns, c);
  ASSERT_TRUE(sol.has_value());
  EXPECT_GE(sol->heading_min_deg, 80.0 - 1.0);
  EXPECT_LE(sol->heading_max_deg, 100.0 + 1.0);
}

// U6: Diagnostics populated after solve
TEST_F(IvPSolverTest, DiagnosticsPopulated) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns{{1.0, f}};
  auto sol = solver_->solve(fns, make_unconstrained());
  ASSERT_TRUE(sol.has_value());

  const auto& diag = solver_->last_diagnostics();
  EXPECT_GT(diag.grid_cells_evaluated, 0U);
  EXPECT_GT(diag.grid_cells_feasible, 0U);
  EXPECT_GT(diag.duration.count(), 0);
}

}  // namespace mass_l3::m4
