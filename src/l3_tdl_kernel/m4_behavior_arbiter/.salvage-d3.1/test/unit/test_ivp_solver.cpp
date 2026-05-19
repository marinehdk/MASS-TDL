#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "m4_behavior_arbiter/error.hpp"
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
    c.cpa_safe_m = 1852.0;  // 1 NM // [TBD-HAZID]
    c.rot_max_deg_s = 3.0;  // [TBD-HAZID]
    return c;
  }

  std::unique_ptr<IvPSolver> solver_;
};

// U1: Single behavior, no constraint → solution converges to piece region
TEST_F(IvPSolverTest, SingleBehaviorNoConstraint) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  const auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  EXPECT_NEAR(sol->heading_min_deg, 85.0, 2.0);
  EXPECT_NEAR(sol->heading_max_deg, 95.0, 2.0);
}

// U2: Two compatible behaviors → weighted sum finds overlap region
TEST_F(IvPSolverTest, TwoCompatibleBehaviorsWeightedSum) {
  IvPFunctionDefault transit_f;
  ASSERT_EQ(transit_f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  IvPFunctionDefault colreg_f;
  ASSERT_EQ(colreg_f.set_pieces({{80.0, 100.0, 12.0, 18.0, 0.8}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({2.0, transit_f});
  fns.push_back({1.0, colreg_f});
  const auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  // Overlap [85°, 95°] × [14, 16] has highest aggregated utility (2.0 + 0.8 = 2.8)
  EXPECT_NEAR(sol->heading_min_deg, 85.0, 2.0);
  EXPECT_NEAR(sol->heading_max_deg, 95.0, 2.0);
}

// U3: Infeasible speed constraint (min > max) → nullopt
TEST_F(IvPSolverTest, InfeasibleSpeedConstraintReturnsNullopt) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  IvPHardConstraints c = make_unconstrained();
  c.speed_min_kn = 18.0;
  c.speed_max_kn = 0.0;  // min > max → infeasible

  const auto sol = solver_->solve(fns, c);
  EXPECT_FALSE(sol.has_value());
}

// U4: Empty behavior set → nullopt
TEST_F(IvPSolverTest, EmptyBehaviorSetReturnsNullopt) {
  const std::vector<WF> fns{};
  const auto sol = solver_->solve(fns, make_unconstrained());
  EXPECT_FALSE(sol.has_value());
}

// U5: Heading wrap-around (350°→10°) → solution includes wrap-around region
TEST_F(IvPSolverTest, HeadingWrapAround) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({
      {350.0, 359.0, 0.0, 20.0, 1.0},
      {0.0,   10.0,  0.0, 20.0, 1.0},
  }), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  const auto sol = solver_->solve(fns, make_unconstrained());

  ASSERT_TRUE(sol.has_value());
  EXPECT_TRUE((sol->heading_min_deg >= 350.0 && sol->heading_max_deg < 360.0)
           || (sol->heading_min_deg < 10.0));
}

// U6: Heading constraint narrows the solution to the allowed range
TEST_F(IvPSolverTest, HeadingConstraintNarrowsSolution) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{0.0, 180.0, 5.0, 15.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  IvPHardConstraints c = make_unconstrained();
  c.heading_allowed_ranges_deg = {{80.0, 100.0}};

  const auto sol = solver_->solve(fns, c);
  ASSERT_TRUE(sol.has_value());
  EXPECT_GE(sol->heading_min_deg, 79.0);
  EXPECT_LE(sol->heading_max_deg, 101.0);
}

// U7: Diagnostics are populated after a successful solve
TEST_F(IvPSolverTest, DiagnosticsPopulated) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  const auto sol = solver_->solve(fns, make_unconstrained());
  ASSERT_TRUE(sol.has_value());

  const auto& diag = solver_->last_diagnostics();
  EXPECT_GT(diag.grid_cells_evaluated, 0U);
  EXPECT_GT(diag.grid_cells_feasible, 0U);
  EXPECT_GT(diag.duration.count(), 0);
}

// U8: Null strategy throws at construction
TEST_F(IvPSolverTest, NullStrategyThrowsAtConstruction) {
  EXPECT_THROW(
      IvPSolver(IvPHeadingDomain(1.0), IvPSpeedDomain(0.0, 20.0, 0.5),
                nullptr, std::chrono::milliseconds(100)),
      std::invalid_argument);
}

// U9: Rationale string is non-empty on successful solve
TEST_F(IvPSolverTest, SolutionRationaleNonEmpty) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  const auto sol = solver_->solve(fns, make_unconstrained());
  ASSERT_TRUE(sol.has_value());
  EXPECT_FALSE(sol->rationale.empty());
}

// U10: Zero-millisecond timeout → nullopt (timeout fires immediately)
TEST_F(IvPSolverTest, TimeoutReturnsNullopt) {
  auto strategy = std::make_unique<WeightedSumCombination>();
  IvPSolver short_solver(
      IvPHeadingDomain(1.0),
      IvPSpeedDomain(0.0, 20.0, 0.5),
      std::move(strategy),
      std::chrono::milliseconds(0));

  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  std::vector<WF> fns;
  fns.push_back({1.0, f});
  const auto sol = short_solver.solve(fns, make_unconstrained());
  EXPECT_FALSE(sol.has_value());
}

}  // namespace mass_l3::m4
