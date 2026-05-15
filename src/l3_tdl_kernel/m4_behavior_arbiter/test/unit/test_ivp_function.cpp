#include <gtest/gtest.h>

#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4::test {

// Set pieces and evaluate at a point inside the piece
TEST(IvPFunctionTest, SetAndEvaluate) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  double val = f.evaluate(90.0, 15.0);
  EXPECT_DOUBLE_EQ(val, 1.0);
}

// Evaluate at a point outside all pieces yields 0.0
TEST(IvPFunctionTest, OutOfRangeReturnsZero) {
  IvPFunctionDefault f;
  ASSERT_EQ(f.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0}}), ErrorCode::Ok);

  double val = f.evaluate(0.0, 0.0);
  EXPECT_DOUBLE_EQ(val, 0.0);
}

// Default weight and priority have expected values
TEST(IvPFunctionTest, DefaultWeightAndPriority) {
  IvPFunctionDefault f;
  EXPECT_DOUBLE_EQ(f.weight, 1.0);
  EXPECT_EQ(f.priority, "normal");
}

}  // namespace mass_l3::m4::test
