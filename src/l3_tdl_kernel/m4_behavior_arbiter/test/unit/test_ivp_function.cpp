#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4 {

// ============================================================================
// IvPFunction Tests
// ============================================================================

class IvPFunctionTest : public ::testing::Test {
 protected:
  using Piece = IvPFunctionDefault::Piece;
};

// Test 1: DefaultConstructedHasZeroPieces
TEST_F(IvPFunctionTest, DefaultConstructedHasZeroPieces) {
  IvPFunctionDefault func;
  EXPECT_EQ(func.piece_count(), 0U);
}

// Test 2: SetPiecesStoresTotalCount
TEST_F(IvPFunctionTest, SetPiecesStoresTotalCount) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 45.0, 0.0, 15.0, 0.5},
      {45.0, 90.0, 5.0, 20.0, 0.8},
  };
  M4ErrorCode ec = func.set_pieces(pieces);
  EXPECT_EQ(ec, M4ErrorCode::kOk);
  EXPECT_EQ(func.piece_count(), 2U);
}

// Test 3: EvaluateNoPiecesReturnsZero
TEST_F(IvPFunctionTest, EvaluateNoPiecesReturnsZero) {
  IvPFunctionDefault func;
  double utility = func.evaluate(90.0, 10.0);
  EXPECT_DOUBLE_EQ(utility, 0.0);
}

// Test 4: EvaluateSinglePieceMatch
TEST_F(IvPFunctionTest, EvaluateSinglePieceMatch) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {85.0, 95.0, 8.0, 12.0, 0.8},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  double utility = func.evaluate(90.0, 10.0);
  EXPECT_DOUBLE_EQ(utility, 0.8);
}

// Test 5: EvaluateOutsideAllPiecesReturnsZero
TEST_F(IvPFunctionTest, EvaluateOutsideAllPiecesReturnsZero) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {85.0, 95.0, 8.0, 12.0, 0.8},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  double utility = func.evaluate(45.0, 10.0);
  EXPECT_DOUBLE_EQ(utility, 0.0);
}

// Test 6: HeadingWrapAround - evaluate at psi within [355,360)
TEST_F(IvPFunctionTest, HeadingWrapAround) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {355.0, 360.0, 0.0, 20.0, 0.9},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  double utility = func.evaluate(357.0, 5.0);
  EXPECT_DOUBLE_EQ(utility, 0.9);
}

// Test 7: WrapAroundPiece - heading_min > heading_max
TEST_F(IvPFunctionTest, WrapAroundPiece) {
  IvPFunctionDefault func;
  // Piece spanning 350°→10° (wrap-around)
  std::vector<Piece> pieces{
      {350.0, 10.0, 0.0, 20.0, 0.7},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  // 5° should match (inside wrap-around range)
  double utility_match = func.evaluate(5.0, 10.0);
  EXPECT_DOUBLE_EQ(utility_match, 0.7);

  // 180° should not match (outside wrap-around range)
  double utility_nomatch = func.evaluate(180.0, 10.0);
  EXPECT_DOUBLE_EQ(utility_nomatch, 0.0);

  // 355° should match (inside wrap-around range)
  double utility_match2 = func.evaluate(355.0, 10.0);
  EXPECT_DOUBLE_EQ(utility_match2, 0.7);
}

// Test 8: SetPiecesTooMany
TEST_F(IvPFunctionTest, SetPiecesTooMany) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces;
  // Create 33 pieces (exceeds default max of 32)
  for (int i = 0; i < 33; ++i) {
    pieces.push_back({0.0, 45.0, 0.0 + i, 15.0 + i, 0.5});
  }

  M4ErrorCode ec = func.set_pieces(pieces);
  EXPECT_EQ(ec, M4ErrorCode::kYamlInvalidValue);
  EXPECT_EQ(func.piece_count(), 0U);
}

// Test 9: PieceAccessValid
TEST_F(IvPFunctionTest, PieceAccessValid) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 45.0, 0.0, 15.0, 0.5},
      {45.0, 90.0, 5.0, 20.0, 0.8},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  const auto& piece = func.piece(0);
  EXPECT_DOUBLE_EQ(piece.utility, 0.5);
}

// Test 10: PieceAccessOutOfRange
TEST_F(IvPFunctionTest, PieceAccessOutOfRange) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 45.0, 0.0, 15.0, 0.5},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  EXPECT_THROW(func.piece(1), std::out_of_range);
}

// Test 11: UtilityOutOfRangeValidation
TEST_F(IvPFunctionTest, UtilityOutOfRangeValidation) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 45.0, 0.0, 15.0, 1.5},  // utility > 1.0
  };

  M4ErrorCode ec = func.set_pieces(pieces);
  EXPECT_EQ(ec, M4ErrorCode::kYamlInvalidValue);
}

// Test 12: UtilityNegativeValidation
TEST_F(IvPFunctionTest, UtilityNegativeValidation) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 45.0, 0.0, 15.0, -0.1},  // utility < 0.0
  };

  M4ErrorCode ec = func.set_pieces(pieces);
  EXPECT_EQ(ec, M4ErrorCode::kYamlInvalidValue);
}

// Test 13: MultiPieceFirstMatchWins
TEST_F(IvPFunctionTest, MultiPieceFirstMatchWins) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 180.0, 0.0, 20.0, 0.6},
      {0.0, 180.0, 0.0, 20.0, 0.9},  // Same region, different utility
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  double utility = func.evaluate(90.0, 10.0);
  EXPECT_DOUBLE_EQ(utility, 0.6);  // First piece matches
}

// Test 32: SetPiecesDegenerate
TEST_F(IvPFunctionTest, SetPiecesDegenerate) {
  IvPFunctionDefault func;
  // Full-circle span rejected
  std::vector<Piece> pieces_full{{0.0, 360.0, 0.0, 20.0, 0.5}};
  EXPECT_EQ(func.set_pieces(pieces_full), M4ErrorCode::kYamlInvalidValue);

  // Zero-width heading rejected
  std::vector<Piece> pieces_zero{{90.0, 90.0, 0.0, 20.0, 0.5}};
  EXPECT_EQ(func.set_pieces(pieces_zero), M4ErrorCode::kYamlInvalidValue);
}

// Test 14: NegativeHeadingWrap
TEST_F(IvPFunctionTest, NegativeHeadingWrap) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {350.0, 360.0, 0.0, 20.0, 0.85},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  // -10° should wrap to 350°
  double utility = func.evaluate(-10.0, 5.0);
  EXPECT_DOUBLE_EQ(utility, 0.85);
}

// Test 15: SpeedBoundaryExactMatch
TEST_F(IvPFunctionTest, SpeedBoundaryExactMatch) {
  IvPFunctionDefault func;
  std::vector<Piece> pieces{
      {0.0, 180.0, 5.0, 15.0, 0.75},
  };
  ASSERT_EQ(func.set_pieces(pieces), M4ErrorCode::kOk);

  // Exact lower bound
  double utility_min = func.evaluate(180.0, 5.0);
  EXPECT_DOUBLE_EQ(utility_min, 0.75);

  // Exact upper bound
  double utility_max = func.evaluate(180.0, 15.0);
  EXPECT_DOUBLE_EQ(utility_max, 0.75);
}

// ============================================================================
// IvPHeadingDomain Tests
// ============================================================================

class IvPHeadingDomainTest : public ::testing::Test {};

// Test 16: HeadingDomainSizeWithResolution1
TEST_F(IvPHeadingDomainTest, HeadingDomainSizeWithResolution1) {
  IvPHeadingDomain domain(1.0);
  EXPECT_EQ(domain.size(), 360U);
}

// Test 17: HeadingDomainAtIndex0Is0deg
TEST_F(IvPHeadingDomainTest, HeadingDomainAtIndex0Is0deg) {
  IvPHeadingDomain domain(1.0);
  EXPECT_DOUBLE_EQ(domain.at(0), 0.0);
}

// Test 18: HeadingDomainAtLastIndex
TEST_F(IvPHeadingDomainTest, HeadingDomainAtLastIndex) {
  IvPHeadingDomain domain(1.0);
  double expected = 359.0;
  EXPECT_DOUBLE_EQ(domain.at(domain.size() - 1), expected);
}

// Test 19: HeadingDomainWrapNegative
TEST_F(IvPHeadingDomainTest, HeadingDomainWrapNegative) {
  IvPHeadingDomain domain(1.0);
  double wrapped = domain.wrap(-10.0);
  EXPECT_DOUBLE_EQ(wrapped, 350.0);
}

// Test 20: HeadingDomainWrapPositive
TEST_F(IvPHeadingDomainTest, HeadingDomainWrapPositive) {
  IvPHeadingDomain domain(1.0);
  double wrapped = domain.wrap(450.0);
  EXPECT_DOUBLE_EQ(wrapped, 90.0);
}

// Test 21: HeadingDomainOutOfRange
TEST_F(IvPHeadingDomainTest, HeadingDomainOutOfRange) {
  IvPHeadingDomain domain(1.0);
  EXPECT_THROW(domain.at(360), std::out_of_range);
}

// Test 22: HeadingDomainResolution2
TEST_F(IvPHeadingDomainTest, HeadingDomainResolution2) {
  IvPHeadingDomain domain(2.0);
  EXPECT_EQ(domain.size(), 180U);
  EXPECT_DOUBLE_EQ(domain.at(0), 0.0);
  EXPECT_DOUBLE_EQ(domain.at(1), 2.0);
}

// Test 30: HeadingDomainInvalidResolutionThrows
TEST_F(IvPHeadingDomainTest, HeadingDomainInvalidResolutionThrows) {
  EXPECT_THROW(IvPHeadingDomain(0.0), std::invalid_argument);
  EXPECT_THROW(IvPHeadingDomain(-1.0), std::invalid_argument);
}

// ============================================================================
// IvPSpeedDomain Tests
// ============================================================================

class IvPSpeedDomainTest : public ::testing::Test {};

// Test 23: SpeedDomainSizeInclusive
TEST_F(IvPSpeedDomainTest, SpeedDomainSizeInclusive) {
  IvPSpeedDomain domain(0.0, 20.0, 0.5);
  // (20.0 - 0.0) / 0.5 + 1 = 40 + 1 = 41
  EXPECT_EQ(domain.size(), 41U);
}

// Test 24: SpeedDomainAtIndex0IsMin
TEST_F(IvPSpeedDomainTest, SpeedDomainAtIndex0IsMin) {
  IvPSpeedDomain domain(5.0, 20.0, 1.0);
  EXPECT_DOUBLE_EQ(domain.at(0), 5.0);
}

// Test 25: SpeedDomainAtLastIndex
TEST_F(IvPSpeedDomainTest, SpeedDomainAtLastIndex) {
  IvPSpeedDomain domain(0.0, 20.0, 0.5);
  EXPECT_DOUBLE_EQ(domain.at(40), 20.0);
}

// Test 26: SpeedDomainResolution
TEST_F(IvPSpeedDomainTest, SpeedDomainResolution) {
  IvPSpeedDomain domain(0.0, 20.0, 0.5);
  EXPECT_DOUBLE_EQ(domain.resolution(), 0.5);
}

// Test 27: SpeedDomainOutOfRange
TEST_F(IvPSpeedDomainTest, SpeedDomainOutOfRange) {
  IvPSpeedDomain domain(0.0, 20.0, 0.5);
  EXPECT_THROW(domain.at(41), std::out_of_range);
}

// Test 28: SpeedDomainArbitrary
TEST_F(IvPSpeedDomainTest, SpeedDomainArbitrary) {
  IvPSpeedDomain domain(2.0, 15.0, 1.0);
  // (15.0 - 2.0) / 1.0 + 1 = 13 + 1 = 14
  EXPECT_EQ(domain.size(), 14U);
  EXPECT_DOUBLE_EQ(domain.at(0), 2.0);
  EXPECT_DOUBLE_EQ(domain.at(13), 15.0);
}

// Test 29: SpeedDomainMinMax
TEST_F(IvPSpeedDomainTest, SpeedDomainMinMax) {
  IvPSpeedDomain domain(5.0, 25.0, 2.0);
  EXPECT_DOUBLE_EQ(domain.min(), 5.0);
  EXPECT_DOUBLE_EQ(domain.max(), 25.0);
}

// Test 31: SpeedDomainInvalidArgsThrows
TEST_F(IvPSpeedDomainTest, SpeedDomainInvalidArgsThrows) {
  EXPECT_THROW(IvPSpeedDomain(0.0, 20.0, 0.0), std::invalid_argument);  // zero resolution
  EXPECT_THROW(IvPSpeedDomain(0.0, 20.0, -1.0), std::invalid_argument); // negative resolution
  EXPECT_THROW(IvPSpeedDomain(20.0, 0.0, 0.5), std::invalid_argument);  // max < min
  EXPECT_THROW(IvPSpeedDomain(5.0, 5.0, 0.5), std::invalid_argument);   // max == min
}

}  // namespace mass_l3::m4
