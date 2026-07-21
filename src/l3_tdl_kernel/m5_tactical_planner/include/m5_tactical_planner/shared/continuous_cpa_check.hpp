#ifndef MASS_L3_M5_SHARED_CONTINUOUS_CPA_CHECK_HPP_
#define MASS_L3_M5_SHARED_CONTINUOUS_CPA_CHECK_HPP_

// M5 Tactical Planner — LX-T3: continuous/swept CPA independent witness.
//
// Computes the minimum swept CPA over a solved trajectory by checking the
// line-segment distance between consecutive trajectory nodes. This complements
// the node-only CPA check: a trajectory that satisfies the hard CPA floor at
// every discrete stage node could still cross through the CPA cylinder between
// nodes (interval crossing). This function detects that gap.
//
// Pure C++ (no CasADi, no acados). Header-only inline, same discipline as
// soft_aspiration_telemetry.hpp and huber_cost.hpp.
//
// PATH-D (MISRA C++:2023): <60 lines, CC <= 10, no float, no bare new/delete.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::shared {

/// Return value of compute_continuous_cpa().
struct ContinuousCpaResult {
  double min_swept_cpa_m{std::numeric_limits<double>::infinity()};
  ///< minimum swept CPA over all segments [m]; infinity if no trajectory
  bool interval_crossing_detected{false};
  ///< true when swept CPA drops below cpa_hard_m for any target
  std::int32_t violating_target_id{0};
  ///< id of first target that violated; 0 when no violation
  int violating_segment_k{-1};
  ///< stage index of the segment start that violated; -1 when no violation
};

/// Compute the minimum swept (continuous) CPA from own-ship trajectory to each
/// target, checking the line-segment between consecutive trajectory nodes.
///
/// The node-only CPA check verifies that ||own_k - target_k|| >= cpa_hard_m
/// at each discrete stage k. However, both own-ship and target move linearly
/// between stages; the swept relative path traces a line segment from R_k to
/// R_{k+1} where R = own - target. If this segment passes closer to the origin
/// than cpa_hard_m, an interval crossing occurs even though both endpoint
/// distances satisfy the floor.
///
/// Target motion is accounted for: at each stage k, the target is propagated
/// forward from its initial position at constant velocity (cog, sog) by the
/// stage time t_s.
///
/// @param trajectory  solved own-ship trajectory (N points, positions in NED)
/// @param targets     target states (initial positions + cog/sog)
/// @param cpa_hard_m  hard CPA floor [m] (typically 1852)
///
/// @return  {min_swept_cpa_m, interval_crossing_detected, violating_target_id,
///           violating_segment_k}.
///          min_swept_cpa_m = infinity when trajectory has < 2 points or 0
///          targets; interval_crossing_detected is true when swept CPA drops
///          below cpa_hard_m for any target.
inline ContinuousCpaResult compute_continuous_cpa(
    const std::vector<TrajectoryPoint>& trajectory,
    const std::vector<TargetState>& targets,
    double cpa_hard_m) {
  ContinuousCpaResult result{};

  if (trajectory.size() < 2U || targets.empty() || cpa_hard_m <= 0.0) {
    // No meaningful swept check possible: return with infinity (no violation).
    return result;
  }

  const double cpa_hard_sq = cpa_hard_m * cpa_hard_m;

  for (const auto& tgt : targets) {
    // Precompute target velocity components (constant-velocity model).
    const double tvn = tgt.sog_mps * std::cos(tgt.cog_rad);
    const double tve = tgt.sog_mps * std::sin(tgt.cog_rad);

    // Compute relative position at the first trajectory node.
    const auto& p0 = trajectory[0];
    double prev_tx = tgt.x_m + tvn * p0.t_s;
    double prev_ty = tgt.y_m + tve * p0.t_s;
    double prev_rx = p0.x_m - prev_tx;
    double prev_ry = p0.y_m - prev_ty;

    for (std::size_t k = 1U; k < trajectory.size(); ++k) {
      const auto& pk = trajectory[k];

      // Target position at stage k (constant-velocity drift).
      const double cur_tx = tgt.x_m + tvn * pk.t_s;
      const double cur_ty = tgt.y_m + tve * pk.t_s;

      // Relative position at stage k.
      const double cur_rx = pk.x_m - cur_tx;
      const double cur_ry = pk.y_m - cur_ty;

      // Point-to-segment distance from origin to segment
      // A(prev_rx, prev_ry) -> B(cur_rx, cur_ry).
      const double ABx = cur_rx - prev_rx;
      const double ABy = cur_ry - prev_ry;
      const double AB2 = ABx * ABx + ABy * ABy;

      double dist_sq = prev_rx * prev_rx + prev_ry * prev_ry;  // |A|^2

      if (AB2 > 1.0e-12) {
        // Project origin onto the line through A->B:
        //   t = dot(-A, B-A) / dot(B-A, B-A)
        // Clamp to [0, 1].
        const double t = std::clamp(
            -(prev_rx * ABx + prev_ry * ABy) / AB2, 0.0, 1.0);
        const double cx = prev_rx + t * ABx;
        const double cy = prev_ry + t * ABy;
        dist_sq = cx * cx + cy * cy;
      }
      // else: AB2 ~ 0 (own and target have same velocity -> relative
      // position unchanged). The endpoint distance already covers this.

      if (dist_sq < cpa_hard_sq) {
        // Interval crossing detected: the swept CPA drops below the floor.
        result.interval_crossing_detected = true;
        if (result.violating_target_id == 0) {
          result.violating_target_id = tgt.id;
          result.violating_segment_k = static_cast<int>(k) - 1;
        }
        // Track minimum swept CPA even when violation found.
        const double d = std::sqrt(dist_sq);
        if (d < result.min_swept_cpa_m) {
          result.min_swept_cpa_m = d;
        }
      } else {
        const double d = std::sqrt(dist_sq);
        if (d < result.min_swept_cpa_m) {
          result.min_swept_cpa_m = d;
        }
      }

      // Advance to next segment.
      prev_rx = cur_rx;
      prev_ry = cur_ry;
    }
  }

  return result;
}

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_CONTINUOUS_CPA_CHECK_HPP_
