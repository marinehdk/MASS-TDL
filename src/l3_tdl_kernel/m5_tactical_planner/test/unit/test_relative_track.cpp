#include "m5_tactical_planner/shared/relative_track.hpp"
#include <gtest/gtest.h>

// The header declares project_to_segment inside mass_l3::m5::shared::relative_track.
// Alias the innermost name so the using-declaration below (and the call sites in
// every test case) match the brief's pinned text verbatim.
namespace relative_track = mass_l3::m5::shared::relative_track;
using relative_track::project_to_segment;

TEST(RelativeTrack, PointInsideSegment_FootIsClosest) {
  // Segment A=(0,0)->B=(10,0), P=(5,2): foot (5,0), lateral +2 (n_hat=(0,1))
  auto r = project_to_segment(5.0, 2.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.closest_x, 5.0, 1e-9);
  EXPECT_NEAR(r.closest_y, 0.0, 1e-9);
  EXPECT_NEAR(r.t, 0.5, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 2.0, 1e-9);
}

TEST(RelativeTrack, PointPastB_EndpointIsClosest) {
  // P=(12,1) past B=(10,0) -> t clamps to 1, closest=B
  auto r = project_to_segment(12.0, 1.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.t, 1.0, 1e-9);
  EXPECT_NEAR(r.closest_x, 10.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 1.0, 1e-9);
}

TEST(RelativeTrack, PointBeforeA_EndpointIsClosest) {
  // P=(-2,1) before A=(0,0) -> t clamps to 0
  auto r = project_to_segment(-2.0, 1.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.t, 0.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 1.0, 1e-9);
}

TEST(RelativeTrack, DegenerateZeroLength_Fallback) {
  // Zero-length segment A==B=(1,1): fallback closest=A, lateral=(P-A).n_hat
  auto r = project_to_segment(3.0, 4.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0);
  EXPECT_NEAR(r.closest_x, 1.0, 1e-9);
  EXPECT_NEAR(r.signed_lateral, 3.0, 1e-9);  // (4-1)*1
}

TEST(RelativeTrack, NegativeLateral_PortSide) {
  // n_hat=(0,1), P=(5,-2) -> lateral -2 (port side)
  auto r = project_to_segment(5.0, -2.0, 0.0, 0.0, 10.0, 0.0, 0.0, 1.0);
  EXPECT_NEAR(r.signed_lateral, -2.0, 1e-9);
}
