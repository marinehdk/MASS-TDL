#include <gtest/gtest.h>
#include <vector>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4 {
namespace {

TEST(IvPCombineTest, WeightedSumSingleFunctionReturnsWeightedUtility) {
  IvPFunction<32> fn;
  IvPFunction<32>::Piece p{0.0, 90.0, 0.0, 10.0, 0.8};
  fn.set_pieces({p});

  WeightedSumCombination combiner;
  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {{0.5, fn}};

  double result = combiner.combine(45.0, 5.0, fns);
  EXPECT_NEAR(result, 0.4, 1e-6);
}

TEST(IvPCombineTest, WeightedSumMultipleFunctionsAddsCorrectly) {
  IvPFunction<32> fn1, fn2;
  fn1.set_pieces({{0.0, 360.0, 0.0, 22.0, 0.6}});
  fn2.set_pieces({{0.0, 360.0, 0.0, 22.0, 0.4}});

  WeightedSumCombination combiner;
  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {{0.7, fn1}, {0.3, fn2}};

  double result = combiner.combine(180.0, 10.0, fns);
  EXPECT_NEAR(result, 0.7 * 0.6 + 0.3 * 0.4, 1e-6);
}

TEST(IvPCombineTest, UtilityCanExceedOneWithMultipleBehaviors) {
  IvPFunction<32> fn1, fn2;
  fn1.set_pieces({{0.0, 360.0, 0.0, 22.0, 1.0}});
  fn2.set_pieces({{0.0, 360.0, 0.0, 22.0, 0.8}});

  WeightedSumCombination combiner;
  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {{1.0, fn1}, {0.5, fn2}};

  double result = combiner.combine(90.0, 15.0, fns);
  EXPECT_GT(result, 1.0);
  EXPECT_NEAR(result, 1.4, 1e-6);
}

TEST(IvPCombineTest, EmptyFunctionsReturnsZero) {
  WeightedSumCombination combiner;
  std::vector<IvPCombinationStrategy::WeightedFunction> fns;
  double result = combiner.combine(0.0, 0.0, fns);
  EXPECT_EQ(result, 0.0);
}

}  // namespace
}  // namespace mass_l3::m4
