#ifndef MASS_L3_M5_COMMON_L0_GUARDS_HPP_
#define MASS_L3_M5_COMMON_L0_GUARDS_HPP_

// M5 Tactical Planner — L0 input validation pure functions.
//
// Origin: 7-layer regression-baseline contract test (spec
// docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md §2).
// These functions are the L0 input guards extracted BEHAVIOR-PRESERVING from
// MidMpcNode::assemble_input_() (src/mid_mpc/mid_mpc_node.cpp). The only change
// is relocation: the logic is moved verbatim, the spdlog::warn calls stay on
// the caller side (assemble_input_), and these pure functions return the
// fallback value AND set the InputDegradation flag without logging. This keeps
// them unit-testable without log capture.
//
// Design rule (ARCH-DECISION-03): NEVER silently substitute an invalid upstream
// value — always set the corresponding InputDegradation flag so downstream
// (L1/L4/LX) can distinguish "real input" from "fallback".
//
// Each function documents its mid_mpc_node.cpp source line (commit fb84701b1)
// for traceability. The extraction is a 1:1 map; any behavior delta vs the
// original inline code is a regression.
//
// PATH-D (MISRA C++:2023): pure functions, no side effects except InputDegradation
// flag mutation; no logging, no I/O, no heap allocation.

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5 {

// [TBD-HAZID] Safe CPA distance [m] used when ODD state is unavailable.
// Calibrate via HAZID RUN-001 WP-03 (SOTIF CPA threshold). Exposed publicly so
// L0-T6 can lock the default (1852.0 == 1 NM) against silent drift. Was an
// anonymous-namespace constant in mid_mpc_node.cpp:47; moved here so
// bump_cpa_safe_for_conflict and the L0-T6 test reference the SAME symbol
// (single source of truth).
inline constexpr double kCpaSafeFallback_m = 1852.0;

// Conflict CPA safe distance [m] — the SOFT colreg-barrier radius bumped during
// active COLREGs encounter (assemble_input_:787-791). The hard floor
// (cpa_hard_m) is a SEPARATE value sourced from ODD yaml; the bump must NOT
// leak into cpa_hard_m (Bug C deep, RC-C; spec committed-route-design-v2 §L84).
inline constexpr double kCpaSafeConflictBump_m = 2500.0;

// L0-A: validate own-ship heading.
// NaN/Inf → fallback 0.0 + own_psi_degraded flag; else normalize to [-π, +π]
// (the NLP psi variable box, Fix C-2b: Rule17 and direction/min_alt rows use
// raw psi - own_psi subtraction; if own_psi is at 2π while NLP psi ∈ [-π,π],
// the subtraction yields π instead of 0 → constraint set empty → Infeasible).
// Returns psi_rad.
// Source: mid_mpc_node.cpp:527-536 (assemble_input_).
double validate_own_heading(double heading_deg,
                            MidMpcInput::InputDegradation& deg);

// L0-A: validate own-ship speed.
// Prefers water-referenced u_water when valid (>0.1 m/s AND finite); else falls
// back to SOG (finite AND >=0); else 0.0 + own_u_degraded flag. Returns u_mps.
// Source: mid_mpc_node.cpp:538-551 (assemble_input_).
double validate_own_speed(double u_water, double sog_kn,
                          MidMpcInput::InputDegradation& deg);

// L0-A: validate target lat/lon.
// Returns true iff BOTH finite. The caller drops the target and sets
// target_degraded when false. Pure boolean — no flag mutation here so the
// caller can batch-set the flag once per dropped target (matches the original
// assemble_input_ loop structure).
// Source: mid_mpc_node.cpp:560-564 (assemble_input_).
bool validate_target_latlon(double lat, double lon) noexcept;

// L0-A: validate target SOG.
// finite AND >=0 → sog_mps; else 0.0 + target_degraded flag. Returns sog_mps.
// Source: mid_mpc_node.cpp:573-581 (assemble_input_).
double validate_target_sog(double sog_kn,
                           MidMpcInput::InputDegradation& deg);

// L0-A: validate M4 heading_box_reachable (direction-aware magnitude, always
// >=0 per M4 contract). finite AND >=0 → box_reach_deg; else 0.0 sentinel
// (degrade to v2.1 ROT-only schedule) + reachability_degraded flag.
// Returns box_reach_deg.
// Source: mid_mpc_node.cpp:614-625 (assemble_input_).
double validate_box_reach(double box_reach_deg,
                          MidMpcInput::InputDegradation& deg);

// L0-A: validate M4 rot_step. finite AND >0 → rot_step_deg; else 0.0 sentinel
// (downstream ROT-reach skips) + reachability_degraded flag. Returns rot_step_deg.
// Source: mid_mpc_node.cpp:628-637 (assemble_input_).
double validate_rot_step(double rot_step_deg,
                         MidMpcInput::InputDegradation& deg);

// L0-A: validate M4 min_alt_required. finite AND >=0 → min_alt_rad; else 0.0 +
// reachability_degraded flag. Returns min_alt_rad.
// Source: mid_mpc_node.cpp:638-647 (assemble_input_).
double validate_min_alt(double min_alt_rad,
                        MidMpcInput::InputDegradation& deg);

// L0: bump soft CPA safe distance during active COLREGs encounter. Pure — no
// flag mutation (the bump is a policy, not a degradation). Returns
// kCpaSafeConflictBump_m (2500.0) when conflict_active, else kCpaSafeFallback_m
// (1852.0).
// Source: mid_mpc_node.cpp:787-791 (assemble_input_).
double bump_cpa_safe_for_conflict(bool conflict_active) noexcept;

// L0-B sanity check (GNC Q3, M4-contract research agent_b2f04b59): when
// heading_box_reach > 0 AND COLREGs conflict active AND preferred_direction is
// NOT Starboard/Port, the M4 publish is internally inconsistent (nonzero
// lateral reachability for a non-lateral behavior). Sets reachability_degraded;
// the caller (assemble_input_) emits the warn log. Pure except for the flag set
// (matches the original semantics — the consistency violation IS a degradation).
// Source: mid_mpc_node.cpp:769-779 (assemble_input_).
void check_box_reach_pref_dir_consistency(
    double box_reach_deg,
    bool conflict_active,
    ColregsPreferredDirection pref_dir,
    MidMpcInput::InputDegradation& deg);

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_L0_GUARDS_HPP_
