#include <gtest/gtest.h>

#include <cmath>

#include "m4_behavior_arbiter/ivp_domain.hpp"

namespace mass_l3::m4::test {

// HeadingDomain with default resolution (1°)
TEST(IvPHeadingDomainTest, DefaultResolution) {
  IvPHeadingDomain dom;
  EXPECT_EQ(dom.size(), 360U);
  EXPECT_DOUBLE_EQ(dom.resolution_deg(), 1.0);
  EXPECT_DOUBLE_EQ(dom.at(0), 0.0);
  EXPECT_DOUBLE_EQ(dom.at(90), 90.0);
  EXPECT_DOUBLE_EQ(dom.at(359), 359.0);
}

// HeadingDomain with custom resolution (0.5°)
TEST(IvPHeadingDomainTest, CustomResolution) {
  IvPHeadingDomain dom(0.5);
  EXPECT_EQ(dom.size(), 720U);
  EXPECT_DOUBLE_EQ(dom.resolution_deg(), 0.5);
  EXPECT_DOUBLE_EQ(dom.at(0), 0.0);
  EXPECT_DOUBLE_EQ(dom.at(1), 0.5);
}

// HeadingDomain static wrap normalises to [0, 360)
TEST(IvPHeadingDomainTest, Wrap) {
  EXPECT_DOUBLE_EQ(IvPHeadingDomain::wrap(0.0), 0.0);
  EXPECT_DOUBLE_EQ(IvPHeadingDomain::wrap(360.0), 0.0);
  EXPECT_DOUBLE_EQ(IvPHeadingDomain::wrap(450.0), 90.0);
  EXPECT_DOUBLE_EQ(IvPHeadingDomain::wrap(-90.0), 270.0);
  EXPECT_DOUBLE_EQ(IvPHeadingDomain::wrap(-360.0), 0.0);
}

// SpeedDomain construction and accessors
TEST(IvPSpeedDomainTest, ConstructionAndAccessors) {
  IvPSpeedDomain dom(0.0, 20.0, 0.5);
  EXPECT_DOUBLE_EQ(dom.min_kn(), 0.0);
  EXPECT_DOUBLE_EQ(dom.max_kn(), 20.0);
  EXPECT_DOUBLE_EQ(dom.resolution_kn(), 0.5);
  EXPECT_EQ(dom.size(), 40U);
}

// SpeedDomain at() returns expected values
TEST(IvPSpeedDomainTest, AtReturnsExpected) {
  IvPSpeedDomain dom(0.0, 20.0, 0.5);
  EXPECT_DOUBLE_EQ(dom.at(0), 0.0);
  EXPECT_DOUBLE_EQ(dom.at(1), 0.5);
  EXPECT_DOUBLE_EQ(dom.at(39), 19.5);
}

// SpeedDomain with negative min
TEST(IvPSpeedDomainTest, NegativeMin) {
  IvPSpeedDomain dom(-5.0, 5.0, 1.0);
  EXPECT_DOUBLE_EQ(dom.min_kn(), -5.0);
  EXPECT_DOUBLE_EQ(dom.at(0), -5.0);
  EXPECT_DOUBLE_EQ(dom.at(5), 0.0);
  EXPECT_EQ(dom.size(), 10U);
}

}  // namespace mass_l3::m4::test
