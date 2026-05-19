# Salvage: feat/d3.3b-m7-sotif (verbatim snapshot)

Imported from deleted branch `feat/d3.3b-m7-sotif` (1 commit `4ed4bd1`, 949 LOC).

**Status: REFERENCE ONLY. NOT BUILT.** COLCON_IGNORE + AMENT_IGNORE markers
exclude this directory from the build.

## Why salvaged

M7 SotifAssumptionMonitor with 6 ISO 21448 assumption checks (fusion confidence,
motion RMSE, blind zone, COLREGs solvability, comm RTT/loss, ODD zone).

Branch built against pre-2026-05-13 baseline; would not merge cleanly because:

1. **Wrong package path** — `src/m7_safety_supervisor/` (branch) vs
   `src/l3_tdl_kernel/m7_safety_supervisor/sotif/` (main).
2. **Invented IDL fields** — branch forward-declares `l3_msgs::msg::WorldState`
   with `fusion_confidence`, `motion_prediction_rmse_m`, `blind_zone_fraction`
   fields that do **not** exist in real `l3_msgs/WorldState.msg` (real msg has
   only `targets[]`, `own_ship`, `zone`, `confidence`, `rationale`). Forward-
   declaration silently bypasses message-contract tracking.
3. **Wrong ODDState/COLREGsConstraint stub fields** — branch stubs include
   constants like `ZONE_A..D` and fields like `processing_success` that don't
   match real msg definitions.
4. **Design archived** — M7 detailed design moved to
   `docs/Design/Phase 1/Archive/Old Modules/M7-Safety-Supervisor/` pending
   Phase 2/3 re-spec.
5. Main already has parallel `sotif/assumption_monitor.{hpp,cpp}` (~300 LOC)
   plus `performance_monitor` + `triggering_condition_detector` siblings — a
   divergent implementation of the same concept.

## TODOs before any of this code can be activated

1. TODO(D3.3b-IDL-RFC): Add `fusion_confidence` / `motion_prediction_rmse_m` /
   `blind_zone_fraction` to `l3_msgs/WorldState.msg` (requires IDL RFC).
2. TODO(D3.3b-IDL-RFC): Align ODDState field names + COLREGsConstraint fields
   with real msgs.
3. TODO(D3.3b-rehome): Delete forward-declared `l3_msgs::msg` stubs; add
   `find_package(l3_msgs REQUIRED)`.
4. TODO(D3.3b-rehome): Use `l3_msgs/SafetyAlert` for output instead of custom struct.
5. TODO(D3.3b-rehome): Reconcile against main's `sotif/assumption_monitor.{hpp,cpp}`.
6. TODO(D3.3b-rehome): Re-spec D3.3b in Phase 2/3 before merging.

See `/tmp/branches-review.md` for full review.
