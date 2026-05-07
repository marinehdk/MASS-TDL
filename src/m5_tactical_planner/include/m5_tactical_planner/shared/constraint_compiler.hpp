#ifndef MASS_L3_M5_SHARED_CONSTRAINT_COMPILER_HPP_
#define MASS_L3_M5_SHARED_CONSTRAINT_COMPILER_HPP_

// M5 Tactical Planner — ConstraintCompiler
// Compiles runtime COLREGs/behavior/zone constraints into CasADi MX expressions.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no bare new/delete.
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
//
// All constraints expressed as g_i >= 0 (IPOPT convention).
// Names included for active-set logging (SAT-2 transparency per §5.2.4).
//
// Numeric values from ConstraintInputs are baked as CasADi DM constants into the
// symbolic graph. Callers re-compile each Mid-MPC cycle (1 Hz rate, acceptable).
//
// Parameters marked [TBD-HAZID] must be calibrated during HAZID RUN-001
// (FCB sea trials, target completion 2026-08-19 per docs/Design/HAZID/RUN-001-kickoff.md).

#include <casadi/casadi.hpp>
#include <string>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::shared {

class ConstraintCompiler {
 public:
  // -------------------------------------------------------------------------
  // CompiledConstraints — output bundle from compile()
  // -------------------------------------------------------------------------
  struct CompiledConstraints {
    casadi::MX g;                    // stacked inequality vector (g >= 0)
    casadi::DM g_lb;                 // lower bound (zeros, shape = g.rows())
    casadi::DM g_ub;                 // upper bound (inf, shape = g.rows())
    std::vector<std::string> names;  // per-constraint name for active-set logging
  };

  // -------------------------------------------------------------------------
  // compile() — compile all constraints for (psi_seq, u_seq) decision variables.
  // @param psi_seq       CasADi MX symbolic column vector [N×1], heading [rad].
  // @param u_seq         CasADi MX symbolic column vector [N×1], speed [m/s].
  // @param inputs        Runtime constraint parameters (baked as DM constants).
  // @param dt_s          Timestep per MPC step (needed for ROT limit).
  // @param rot_max_rad_s Maximum yaw rate (from VesselDynamicsModel, speed-avg).
  // @returns CompiledConstraints with stacked g + names.
  // -------------------------------------------------------------------------
  [[nodiscard]] CompiledConstraints compile(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ConstraintInputs& inputs,
      double dt_s,
      double rot_max_rad_s) const;

  // -------------------------------------------------------------------------
  // decompose_polygon() — decompose a (possibly non-convex) polygon into
  // convex sub-polygons via fan triangulation from centroid.
  //
  // TSS lanes (Rule 10) are often non-convex → must be decomposed before
  // point_inside_convex() can be applied.
  //
  // Phase E1: uses centroid-fan triangulation (sufficient for typical TSS
  // lane shapes). Ear-clipping upgrade is deferred to Phase E2.
  //
  // @param polygon Non-convex polygon (counter-clockwise vertices, ≥3 vertices).
  // @returns Vector of convex triangular sub-polygons (or the original polygon
  //          if already convex per Boost.Geometry is_convex check).
  // -------------------------------------------------------------------------
  [[nodiscard]] std::vector<Polygon2D> decompose_polygon(
      const Polygon2D& polygon) const;

  // -------------------------------------------------------------------------
  // point_inside_convex() — CasADi MX half-plane test for convex polygon.
  // Returns MX scalar >= 0 when inside, < 0 when outside.
  // Uses minimum cross product across all edges (tightest half-plane).
  // Pre-condition: polygon is convex (use decompose_polygon() first if not).
  // -------------------------------------------------------------------------
  [[nodiscard]] casadi::MX point_inside_convex(
      const casadi::MX& px,
      const casadi::MX& py,
      const Polygon2D& polygon) const;

 private:
  // ── Behavior bounds ──────────────────────────────────────────────────────
  [[nodiscard]] CompiledConstraints compile_heading_bounds(
      const casadi::MX& psi_seq,
      double heading_min_rad,
      double heading_max_rad) const;

  [[nodiscard]] CompiledConstraints compile_speed_bounds(
      const casadi::MX& u_seq,
      double speed_min_mps,
      double speed_max_mps) const;

  // |psi[k+1] - psi[k]| ≤ rot_max * dt for k = 0..N-2
  [[nodiscard]] CompiledConstraints compile_rot_limit(
      const casadi::MX& psi_seq,
      double dt_s,
      double rot_max_rad_s) const;

  // ── COLREGs rule constraints ──────────────────────────────────────────────
  [[nodiscard]] CompiledConstraints compile_colregs_rules(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ConstraintInputs& inputs) const;

  // Rule 14 (Head-on): give-way shall alter course to starboard.
  // [TBD-HAZID] min turn angle 5°: calibrate from encounter simulation data.
  [[nodiscard]] CompiledConstraints compile_rule14(
      const casadi::MX& psi_seq,
      double psi_initial_rad) const;

  // Rule 15 (Crossing): give-way shall pass astern; turn starboard.
  // [TBD-HAZID] min turn angle 5°: calibrate from encounter simulation data.
  [[nodiscard]] CompiledConstraints compile_rule15(
      const casadi::MX& psi_seq,
      double psi_initial_rad) const;

  // Rule 16 (Give-way action): take early substantial action.
  // [TBD-HAZID] 10° within first N/2 steps: calibrate from encounter data.
  [[nodiscard]] CompiledConstraints compile_rule16(
      const casadi::MX& psi_seq,
      double psi_initial_rad) const;

  // Rule 17 (Stand-on): maintain course and speed.
  // [TBD-HAZID] epsilon 5°: calibrate per ODD / sea state.
  [[nodiscard]] CompiledConstraints compile_rule17(
      const casadi::MX& psi_seq,
      double psi_initial_rad) const;

  // ── Zone constraints ──────────────────────────────────────────────────────
  [[nodiscard]] CompiledConstraints compile_zone_constraints(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ConstraintInputs& inputs,
      double dt_s) const;

  // Per-zone, per-step accumulation helper (extracted to honour 60-line limit).
  [[nodiscard]] CompiledConstraints build_zone_steps(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ZoneConstraint& zone,
      const std::vector<Polygon2D>& sub_polygons,
      int32_t N,
      double dt_s) const;

  // ── Helpers ──────────────────────────────────────────────────────────────
  // Stack two CompiledConstraints together (vertically concatenate g + names).
  [[nodiscard]] CompiledConstraints stack(
      CompiledConstraints a,
      const CompiledConstraints& b) const;

  // Normalize polygon to CCW orientation (required for point_inside_convex).
  // Returns input unchanged if already CCW; returns reversed copy if CW.
  [[nodiscard]] static Polygon2D normalize_ccw(const Polygon2D& polygon);
};

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_CONSTRAINT_COMPILER_HPP_
