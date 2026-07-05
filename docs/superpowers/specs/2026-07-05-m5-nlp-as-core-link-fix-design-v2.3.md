# M5 NLP-as-Core Link Fix Spec (v2.3)

**Revision**: v2.3
**Date**: 2026-07-05
**Author**: ZCode ( GLM-5.2 )
**Scope**: M5 Mid-MPC NLP constraint reformulation + commit-gate / counter / observability fixes
**Predecessors**:
- `2026-06-30-m5-committed-route-design-v2.md` (committed route contract)
- `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md` (CPA suffix-hard schedule)
- `2026-07-04-m5-nlp-constraint-restructure-design-v2.2.md` (α/β/γ/δ integration fixes)

## 0. References

- [R1] `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md` §4.3 — CPA suffix-hard schedule + slack rejection rationale
- [R2] `2026-06-30-m5-committed-route-design-v2.md` §3.2 / §13.4 — TailBuilder + policing-function argument
- [R3] Kerrigan 2000, *Soft Constraints and Exact Penalty Functions*, CUED/F-INFENG/TR-381 — soft-with-floor theory
- [R4] Johansen et al., *MPC for Collision Avoidance among COLREGs-compliant vessels*, ICRA 2018 — empirical slack usage in COLAV MPC
- [R5] `mid_mpc_nlp_formulation.cpp:340-389` — J_colreg exponential barrier (cost-side soft, present)
- [R6] `constraint_compiler.cpp:309-357` — CPA hard inequality `d² - cpa_hard² ≥ 0` (per-step, current)
- [R7] `committed_route.cpp:297-318` — risk_trigger_event (Phase 2.1+2.3 rewrite)
- [R8] Phase 1+2 implementation: commits `51f78e8d`..`4b120973`

## 1. Motivation — v2.1 Assumption That Failed

v2.1 §4.3 explicitly rejected slack-variable CPA relaxation in favor of suffix-hard schedule. The justification rested on three pillars:

1. **Pure-soft barrier is too weak** — `w_colreg · J_colreg` was overpowered by `w_route · J_route + w_dist · J_dist` in `test_mid_mpc_route_cost.cpp:230-333`. Soft barrier alone does not open CPA.
2. **Slack does not ensure safety nor recursive feasibility** — citing [R3] Kerrigan 2000 and [R4] Johansen ICRA'18.
3. **Suffix-hard + tail-gate + DegradedHold fallback covers INFEAS** — when CPA floor becomes physically unreachable in close-range, IPOPT reports INFEAS, the solver exits, fallback (DegradedHold / BC-MPC take-over) handles the situation. Spec §13.4 documents the policing-function argument: M7 X-axis Checker independently enforces CPA hard limits.

**Pillars 1 and 2 stand** — pure-soft CPA is genuinely too weak, and slack alone does not certify safety. **Pillar 3 has collapsed under V2.3 phase 3b probe evidence**:

- The fallback path does NOT actually publish a safe route. `publish_committed_route_` has four branches (optimized / corridor / return / emit-nothing); none of them fires during close-range NLP INFEAS. The BcMpcFollow state has no corresponding publish branch — `publish_keep_last_` emits an empty-plan heartbeat that GNC `active_route_manager` silently drops (`latitude.size() < 2`).
- The commit gate (`risk_trigger_event`, pre-Phase-2.1) rejected every optimized candidate on rule14-ho approach because `current range < cpa_hard` was the steady-state geometry — the gate blocked the very CPA-opening maneuver it was supposed to author (`optimized_committed_rejected × 790`).
- The escalation counter (pre-Phase-2.2) never crossed 3 through the dominant NLP-Infeasible path because it only incremented inside `try_revise` on `candidate.nlp_ok=false`, and NLP Infeasible drove `plan.status=DEGRADED` → corridor branch → no optimized `try_revise` call (`nlp_consecutive_failures_ge_3 × 784` only fired via tail-gate rejects).

The result: NLP solver reports Infeasible 371× during the encounter, and the ship ends up following a stale frozen corridor with heading 0° for the entire CPA window, achieving CPA 5.8 m against a 180 m floor. The fallback pillar that v2.1 relied on is, in current code, an empty promise.

**Decision (Phase 3)**: revisit the slack-variable rejection. Combine the suffix-hard schedule (kept — it is still the right structural answer when reachable) with a per-target slack variable as the **last-resort feasibility preserver**, so IPOPT never reports structural Infeasibility. Safety is then enforced independently by the existing tail-gate release check + the Phase 2.1+2.3 commit gate + M7 X-axis Checker (the policing-function argument from [R2] §13.4 — the Doer does NOT need to be feasible-only; the Checker does).

## 2. Design — Per-Target Slack Variable + Retained Suffix-Hard

### 2.1 Decision Variable Extension

The decision vector becomes:

```
x = [ psi(N) ; u(N) ; sigma(Nt) ]   ∈ R^{2N + Nt}
```

where `sigma(t) ≥ 0` is a per-target slack scalar, shared across all N horizon steps for that target. Choosing per-target (not per-step) slack:

- keeps the dimension growth linear in target count (`+Nt` instead of `+N·Nt`),
- preserves the banded structure that v2.1 §4.3 was concerned about (each `sigma(t)` couples only to target-`t` rows),
- still lets a multi-target encounter distribute slack independently across targets (multi-ship forward compatibility).

### 2.2 CPA Constraint Reformulation

`constraint_compiler.cpp:309-357` `compile_cpa_distance` becomes:

```cpp
// σ_t is the per-target slack symbol, passed in as part of the decision vector.
// Constraint form: d² - cpa_hard² + σ_t ≥ 0,   σ_t ≥ 0
for k ∈ [0, N):
  for t ∈ [0, Nt):
    g_rows[cpa_row(t,k)] = dx² + dy² - cpa_hard² + sigma(t)
```

The existing suffix-hard schedule (`mid_mpc_solver.cpp:409-434` `derive_row_bound_config`) still controls which `k` rows are hard vs soft — but with σ in the expression, the feasible region is non-empty by construction regardless of geometry. The hard/soft distinction becomes "where σ is allowed to grow" rather than "where the constraint can fail".

### 2.3 Cost Penalty

```
J_slack = w_slack · sum_t ( sigma(t)² )
```

with `w_slack` a large constant. Initial value `w_slack = 1.0e4` [TBD-HAZID-WP-04] — large enough that NLP only activates σ when the alternative is geometric infeasibility, small enough that IPOPT numerics stay well-conditioned. HAZID WP-04 will calibrate against sea-trial data.

This implements the [R3] exact-penalty form: with sufficiently large `w_slack`, any local minimizer with `σ=0` is also a minimizer of the un-slacked problem. When geometry permits CPA floor compliance, σ stays 0; σ activates only when physical infeasibility would otherwise drive IPOPT to Infeasible.

### 2.4 Why This Is Not "Tuning The Probe Green"

The user's hard rule (AGENTS.md COLREGs debugging rule) forbids threshold tuning to turn a probe green. Slack variable is NOT a threshold tuning:

- It changes the NLP **structure** (feasible-region shape), not a numeric threshold.
- σ > 0 is observable in SAT/ASDR (Phase 3.4) — every cycle reports `slack_norm`. A "tuned green" probe would have σ > 0 every cycle (NLP cheating through slack); a genuine fix has σ = 0 in normal-range encounters and σ > 0 only in true close-range hard-infeasibility windows.
- Downstream gates (tail-gate release check at `types.hpp:811-842`, Phase 2.1+2.3 commit gate at `committed_route.cpp:297`) still enforce CPA safety on the candidate's actual trajectory. A slack-softened NLP that does not actually open CPA still gets rejected at commit / tail-gate — slack only prevents IPOPT from declaring structural Infeasibility; it does not bypass safety enforcement.

### 2.5 Initial-Condition Relax (Phase 3.2)

`mid_mpc_solver.cpp:409-434` currently has a tcpa≤0 fallback that sets `cpa_hard_from_k = 0` (hard from k=0). When the target is already inside `cpa_hard` at k=0, `range(0)² - cpa_hard² ≥ 0` is a hard violation of an initial condition the NLP cannot move. Phase 3.2 forces `cpa_hard_from_k = max(k_minalt, k_tcpa, k_initial_relax=2)`, guaranteeing k=0/1 are always soft. With σ now in the constraint, this is belt-and-suspenders — but it removes a known Infeasibility trigger and makes σ behavior cleaner (σ does not need to absorb initial-condition violations).

### 2.6 CPA Schedule Cleanup (Phase 3.3)

The current schedule inverts reachability: tcpa small (target near) → k_tcpa small → many hard rows, exactly when geometry is least reachable. Phase 3.3 introduces a `geometric_reach_k` floor:

```
geometric_reach_k = ceil( max(0, cpa_hard - current_range) / (max(0.01, closing_rate) · dt) )
cpa_hard_from_k   = max(k_minalt, k_tcpa, k_initial_relax, geometric_reach_k)
```

When the target is far, `geometric_reach_k = 0` (no effect). When the target is inside the floor, `geometric_reach_k` estimates how many steps the own-ship needs to physically reach `cpa_hard` — keeping hard rows only where they are reachable. σ remains the global feasibility preserver behind this schedule.

## 3. Policing Function (Unchanged, Restated)

Per [R2] §13.4: tail-gate is the deterministic NLP publish gate, M7 X-axis is the SIL2 independent Checker. Phase 3 does not change either:

- tail-gate `accept_tail_gate` (`types.hpp:977`) still runs all six checks before an NLP candidate is published; σ only changes feasibility, not gate semantics.
- M7 X-axis Checker is unchanged; it independently verifies CPA/TTC/ship-domain and can veto any M5 output regardless of NLP convergence.

The Doer-Checker architecture (spec [R2] §13) explicitly permits non-deterministic Doer: only the Checker needs IEC 61508 SIL2 traceability. Slack makes the Doer feasible-only by construction; safety is still owned by the Checker.

## 4. Observability (Phase 3.4)

ASDR `avoid_wp` JSON gains:

- `nlp_slack_norm`: `||σ||₂` — the L2 norm of the slack vector. Zero means NLP did not need slack; non-zero means NLP softened a hard-infeasibility window.
- `nlp_slack_per_target`: `[sigma(t) for t in targets]` — per-target slack values.
- `nlp_kkt_residual`, `nlp_solve_duration_ms`, `nlp_ipopt_iter` (already in solver, expose to ASDR).

SAT2 `reasoning_chain` appends `nlp_slack_diagnostic`: "σ active for target X, magnitude Y" when any σ > 0.

These make the difference between "tuned green" (σ always active, every cycle) and "genuine fix" (σ zero except in true close-range windows) directly observable from `/l3/asdr/record`.

## 5. Counter / Commit Gate (Phase 2 — Already Implemented)

Phase 2.1+2.3 (commit `f440d5bd`) rewrote `risk_trigger_event` to mirror tail-gate floor semantics. Phase 2.2 (commit `41d822a7`) separated the escalation counter into solver-layer + commit-layer. These remain in force; Phase 3 NLP slack is independent of them.

### 5.1 Phase 3.8 Amend — TailBuilder Geometry Rejection vs NLP Solver Verdict

**Defect discovered on rule14-ho V2 probe run-19f31173732**: 135 consecutive `optimized_committed_rejected` ASDR events with safety_concern_event `nlp_consecutive_failures_ge_3_no_bcmpc`, while ASDR `avoid_wp` reported 140 cycles `status=NORMAL, planner_health=SOLVER_CONVERGED, cpa_slack≈0, solver_status=0`. The NLP solver had converged; the optimized candidate was rejected anyway.

**Root cause**: `MidMpcNode::publish_committed_route_` (mid_mpc_node.cpp:1689-1695) set `plan.nlp_tail_gate_failed = true` whenever `append_tail_waypoints_` returned a non-empty reject reason (e.g. `tail_spacing_invalid` from `tail_builder.cpp:310`). `committed_candidate_from_plan` then passed `candidate.nlp_ok = !plan.nlp_tail_gate_failed = false` (mid_mpc_node.cpp:1744). `try_revise` (committed_route.cpp:107-144) treated this as an NLP solver failure and escalated: `consecutive_nlp_failures_` incremented past 3 → `DegradedHold` → `publish_keep_last_` emitted an empty-geometry DEGRADED plan → GNC `active_route_manager_node.cpp:343` rejected with `invalid_avoidance_route`.

**Semantic confusion**: `nlp_tail_gate_failed` conflated two distinct failure modes:
1. NLP solver failure / NLP-achievable-CPA failure (legitimately an NLP verdict — populate_canonical_route_from_selected_plan line 75 sets it from `sol.status`).
2. TailBuilder geometry failure (a *post-hoc* geometric extension failure unrelated to the NLP solver).

**Fix**: Phase 3.8 removes the `plan.nlp_tail_gate_failed = true` write from the TailBuilder-reject branch in `publish_committed_route_`. The NLP solver verdict (populate_canonical_route_from_selected_plan) is authoritative for `candidate.nlp_ok`. TailBuilder rejection is honest degradation per §14.3 of [R2]: the optimized MID_MPC body still commits without a tail extension; the rejection reason is recorded in `plan.rationale` and emitted as a new `tail_builder_rejected` ASDR decision_type so future debugging does not require container logs.

**Why this is a chain fix, not a single-module patch**: the failure mode is a field-semantic confusion that crossed MidMpcNode → CommittedRouteCandidate → CommittedAvoidanceRoute → GNC active_route_manager. Each module behaved per its local contract; the chain broke at the semantic boundary between "NLP solver verdict" and "geometric tail feasibility". This is exactly the COLREGs full-chain debugging rule (AGENTS.md): the fix explains why upstream inputs (NLP solver Converged), internal state (CommittedAvoidanceRoute DegradedHold), output message (keep_last empty plan), and downstream consumer behavior (GNC invalid_avoidance_route) were mutually incoherent.

### 5.2 Phase 3.10 Amend — M5 Plan Must Publish Complete Coverage Route (L2 History Prefix + Avoidance + Tail + L2 Suffix)

**Defect discovered on rule14-ho V2 probe run-19f31e16749**: `coordinate_transform_node` `/route_planning/route_plan_status` recorded 86 route decisions, **0 ACCEPTED / 85 REJECTED** (67 × `first changed waypoint is too close to current ship position` + 18 × `dynamic route lateral offset exceeds limit`). The M5 optimized branch (commit_branch=OPTIMIZED) was publishing 12–15-waypoint plans with `route_type=emergency_avoidance`, `navigation_mode[]=emergency_avoidance`, σ=0, NLP Converged — but `/ship/waypoints` (the input `ship_guidance_node` actually follows) stayed at the L2 nominal 2-waypoint path for the entire run. Ownship trajectory: heading 0°/360° unchanged, longitude 10.38 unchanged, CPA 2.2 m (floor 180 m). GNC `active_route_manager` correctly accepted the M5 plan and republished it on `/gnc/active_route` (verified: `route_id=m5-midmpc-…`), but `coordinate_transform_node` rejected every revision so `/ship/waypoints` never advanced past L2 nominal.

**Root cause**: `coordinate_transform_node::first_geometry_change_index` (gnc_ws/ship_guidance/src/coordinate_transform_node.cpp:462-477) pairs the incoming route against `last_feedback_path_` element-by-element with a 1.0 m tolerance. Because `coordinate_transform_node` continuously rejected the M5 route, `last_feedback_path_` was always the L2 nominal 2-waypoint path (start 0,0; end 18578,0). The M5 plan anchor was the ownship current position (e.g., lat 63.46) and wp1 sat 120 m ahead with a 57 m east offset — projected onto the L2 nominal leg this is only ~120 m of along-track, while ownship had already travelled ~5000 m along L2 nominal. The paired comparison returned `first_changed_idx=1` with `first_changed_distance_ahead = 120 - 5000 ≈ -4880 m` (the "first changed" waypoint sat **behind** ownship by 4880 m). The emergency-relaxed threshold (`emergency_avoidance_min_future_update_distance_m=0.0`) still rejected it because `-4880 < 0`. The coord_transform guard's design premise — "incoming route shares origin and direction with last_feedback_path; it is an incremental revision" — does not hold for an M5 avoidance route that re-anchors at ownship. The cold-start dead-end is self-perpetuating: rejection ⇒ `last_feedback_path_` stays at L2 nominal ⇒ next M5 plan is rejected again.

**Implementation deviation from design**: spec v2 §1 (line 39-43) and §9.12 (line 209) require M5 to publish a **complete coverage route** as a versioned full snapshot (committed prefix + current segment + future Mid-MPC segment + tail/rejoin + L2 nominal suffix). The implementation (`publish_committed_route_` optimized branch) only emitted `MID_MPC_OPTIMIZED + MID_MPC_TERMINAL_HOLD + REJOIN_TO_L2 + L2_NOMINAL_SUFFIX` — it never prepended the L2 historical segment from L2 start to ownship projection. The user's architecture intent ("L3 M5 拼接已航行历史 + 避碰 + 返航 + 后续 L2") matches the design; the code diverged. On the first avoidance cycle, when no prior committed avoidance route exists, `last_feedback_path_` is the L2 nominal path, so an M5 plan that does not start at the L2 origin is geometrically incomparable.

**Fix**: Phase 3.10 introduces `prepend_l2_history_prefix_if_preflight_feasible` (mid_mpc_waypoint_generator.cpp) and wires it into `publish_committed_route_` optimized branch between `populate_canonical_route_from_selected_plan` and `append_tail_waypoints_`. The helper finds the L2 `planned_route` pose closest to ownship, takes the preceding L2 poses (from L2 start up to but excluding the closest pose) as a historical prefix, decimates them to ≥15 m spacing to satisfy `validate_canonical_route_for_gnc` `segment_too_short`, and inserts them at the front of the plan's parallel arrays with `navigation_mode="cruise"` and a new `segment_source=L2_HISTORICAL_PREFIX=5`. With the prefix in place, the incoming route's first N entries match `last_feedback_path_` element-by-element (same lat/lon as L2 nominal), so `first_geometry_change_index` returns N (the ownship anchor position) instead of 1, and `first_changed_distance_ahead` becomes positive (the anchor sits ahead of ownship in the L2 along-track sense, not behind). The optimized branch's existing `append_l2_nominal_suffix_if_preflight_feasible` continues to append the future L2 tail after the avoidance + tail segments, so the published plan is genuinely `L2_HISTORICAL_PREFIX + MID_MPC_OPTIMIZED + MID_MPC_TERMINAL_HOLD + REJOIN_TO_L2 + L2_NOMINAL_SUFFIX` — the complete coverage route the design always specified. `segment_source` enum and `schema_version` are bumped 115 → 116 in `l3_msgs/AvoidancePlan.msg` and `ship_interfaces/AvoidancePlan.msg`; the legacy `third_party/gnc_ws/.../ship_interfaces/AvoidancePlan.msg` is **not** modified (AGENTS.md forbids third_party source edits; `coordinate_transform_node` consumes `navigation_mode[]`, not `segment_source[]`, so enum-value addition is non-breaking on the GNC side).

**Why this is a chain fix, not a single-module patch**: the failure mode spans L2 nominal route latch → `gnc_bridge` L3-side forwarder → `active_route_manager` accept/defer state machine → `coordinate_transform_node` `route_update_guard` → `ship_guidance_node` LOS path tracking → ownship dynamics. Each layer behaved per its local contract (L2 legitimately broadcasts its nominal route; ARM legitimately accepts M5 plans; coord_transform legitimately guards against route jumps; ship_guidance legitimately tracks the only path it receives). The chain broke at the unstated contract between M5's route shape and coord_transform's pairing assumption — M5's "anchor at ownship" shape is geometrically valid as a COLREG avoidance intent but invalid as an incremental revision of an L2-anchored feedback path. Phase 3.10 restores the contract by making M5 publish a route that is simultaneously a valid COLREG avoidance intent (preserved MID_MPC_OPTIMIZED + tail segments) and a valid incremental revision of L2 nominal (prepended L2 historical prefix aligns the paired comparison). This is exactly the AGENTS.md COLREGs full-chain debugging rule: the fix explains why the M5 NLP converged with σ=0 and produced a correct starboard-turn geometry (upstream input), why coord_transform rejected it anyway (internal state with stale last_feedback_path_), why ship_guidance followed L2 nominal (output message never advanced), and why ownship did not maneuver (downstream consumer behavior) — and the fix makes all four coherent without tuning any guard threshold or modifying GNC source.

### 5.2.1 Phase 3.10.1 Amend — Station-Based Prefix/Suffix Builder (Codex 方案 E) + mock_l2 三修

**Defect discovered on rule14-ho V2 probe run-19f3231690a (with C2 densify + prepend v1 on branch)**: CPA 5.6 m (still RED, floor 180 m), steering 0.0° unchanged. M5 published plans reported `segment_source_count=12` all `MID_MPC_OPTIMIZED` — the historical prefix was not prepended — and the mock_l2 container log showed only `No SIL_SCENARIO_YAML env var set; using default route config` followed by an AttributeError crash.

**Three independent root causes (mock_l2 side, all on the host-published `docker/mock_l2_publisher.py`)**:

1. **`self._resample_speeds` typo (line 528 of C2 commit 60ef0cf9)**. `_resample_speeds` is a module-level helper (defined at line 162), not a `MockL2Publisher` method. The first `_load_scenario` call after `_densify_waypoints` therefore raised `AttributeError: 'MockL2Publisher' object has no attribute '_resample_speeds'` and killed the node. After death, mock_l2 could not receive the subsequent `/sil/scenario_loaded` broadcast, so M2/M4/M6 logged `scenario_loaded 'colreg-rule14-ho'` while mock_l2 stayed on the default 2-endpoint route. The C2 densify code was correct but unreachable.

2. **QoS mismatch on `/sil/lifecycle_status` and `/sil/scenario_loaded`**. mock_l2 subscribed with `QoS depth=10` (default VOLATILE durability). The orchestrator publishes `scenario_loaded` with `_SCENARIO_LOADED_QOS` (TRANSIENT_LOCAL, `lifecycle_bridge.py:42-47`) and lifecycle_mgr publishes `lifecycle_status` with `_STATUS_QOS` (TRANSIENT_LOCAL). DDS downgrades durability on the subscriber side, so messages still flow, but the latched value was never delivered to mock_l2 — every cold start the subscriptions came up after Stage 1 had already latched `ACTIVE` without `scenario_id`, so mock_l2 fell into `_auto_detect_scenario`.

3. **`_auto_detect_scenario` picked alphabetical-first YAML**. With 44 scenario files in `/var/sil/scenarios`, the legacy `sorted(glob('**/*.yaml'))[0]` returned `colreg-rule13-ot-target-giveway` (the alphabetically earliest `colreg-rule13-*`), not the configured scenario. Even when the latch was lost and auto-detect ran, it densified and published the *wrong* nominal route, so the M5 prefix helper aligned against a feedback path that did not match the actual encounter.

**Fix (mock_l2)**: (a) drop the `self.` prefix on `_resample_speeds`; (b) add a `latched_qos` helper (RELIABLE + TRANSIENT_LOCAL + KEEP_LAST) and use it for both `/sil/lifecycle_status` and `/sil/scenario_loaded` subscriptions (ownship BEST_EFFORT/VOLATILE sensor subscription unchanged); (c) `_auto_detect_scenario` now prefers the YAML whose `metadata.scenario_id` equals the latched `current_scenario_id`, falling back to alphabetical-first only when no metadata match exists. The ownship sensor QoS is intentionally left BEST_EFFORT/VOLATILE.

**M5-side defect (Codex 方案 E)**: even with mock_l2 fixed and the dense L2 nominal feedback path in place, the legacy `prepend_l2_history_prefix_if_preflight_feasible` used `route_point_distance_m` to find the nearest L2 pose (`closest_idx`) and treated `[0, closest_idx)` as the prefix. When ownship sat near the L2 start the nearest pose was vertex 0, so `closest_idx=0` made the helper a no-op and the M5 plan shipped without a prefix. `coordinate_transform_node::first_geometry_change_index` (`coordinate_transform_node.cpp:462-477`) then compared M5's ownship-anchored plan against the dense L2 `last_feedback_path_` pair-wise and reported the first changed point at the ownship anchor itself — `first_changed_distance_ahead ≈ 0` (or negative when ownship had already advanced along L2), triggering the `first changed waypoint is too close to current ship position` rejection.

**Fix (M5)**: replace the nearest-pose selection with a station-based builder that reuses the existing `tail_builder::RouteFrame` (`tail_builder/route_frame.hpp:11-34`) projection math — the same abstraction the TailBuilder itself uses for hold/rejoin. The new helper:

1. Builds an own-relative NED polyline from the L2 GeoPath (mirroring `tail_route_frame_from_l2`, `mid_mpc_node.cpp:117-137`, using `units::kEarthRadiusMean_m = 6371000.0` — not the legacy `kMetersPerDegLat = 111320.0` — so prefix and tail share one projection error model).
2. Projects ownship (the NED origin) onto the polyline to get along-track station `s_own`.
3. Computes `s_first_change = s_own + max(wheel_over_distance_m, kMinPrefixSpacingM)` where `wheel_over_distance_m = 120.0` matches `MidMpcWaypointGenerator::Config::wheel_over_distance_m` and `GncAvoidancePreflightConfig::emergency_wheel_over_distance_m`.
4. Takes every L2 pose whose station is strictly less than `s_first_change` as the prefix, decimating to ≥ `kMinPrefixSpacingM = 15.0 m` to satisfy `validate_canonical_route_for_gnc`'s `emergency_min_segment_length_m` floor.
5. The ownship anchor itself is excluded from the prefix — it remains the first `MID_MPC_OPTIMIZED` entry populated by `populate_canonical_route_from_selected_plan`. The check `if (std::hypot(dx_own, dy_own) < 1.0) continue;` enforces this: any L2 vertex within 1 m of ownship (which is the cold-start vertex 0 case) is dropped so the prefix never starts with a zero-length segment.

With this geometry, the pair-wise comparison `coordinate_transform_node` performs against the dense L2 `last_feedback_path_` now finds the first changed point at the **avoidance** point (lateral offset > 1 m), not at the ownship anchor — `first_changed_distance_ahead ≈ +120 m > active_min_future_update_distance`, so the route is accepted.

**`append_l2_nominal_suffix_if_preflight_feasible` was upgraded in lock-step** (same commit, same code paths) to use the same `RouteFrame::project` station math. The legacy nearest-pose helper happened to work for suffix selection (the plan's terminal waypoint usually sits near the L2 endpoint, where the nearest-pose answer is correct), but keeping two different selection algorithms for prefix and suffix would re-introduce exactly the coordinate-frame logic split that caused the prepend bug. The suffix helper now picks every L2 pose whose station is strictly greater than the plan-end's projected station, reprojects back to lat/lon via the same own-relative NED inverse, and emits `segment_source=L2_NOMINAL_SUFFIX=3` exactly as before.

**No new `segment_source` enum value is introduced.** The existing `L2_HISTORICAL_PREFIX=5` (added in v2.3 §5.2) covers the prefix contract; `L2_NOMINAL_SUFFIX=3` covers the suffix. The earlier handoff note mentioning a `COMMITTED_OR_L2_PREFIX` enum value was withdrawn after `AvoidancePlan.msg` review showed `L2_HISTORICAL_PREFIX=5` already provides this semantic.

**Acceptance criteria increment**:
- mock_l2 startup log shows `Route from YAML: 2 nominal → N densified waypoints` (not `No SIL_SCENARIO_YAML env var set; using default route config`).
- M5 published plan's `segment_source[]` contains at least one `L2_HISTORICAL_PREFIX=5` entry at the head of the array during the M5 avoidance window (probe shows `segment_source_count > 0` for non-MID_MPC_OPTIMIZED entries).
- `coordinate_transform_node` `/route_planning/route_plan_status` records at least one `ACCEPTED` decision during the M5 avoidance window (the `first_changed_distance_ahead` for the first accepted route is positive and finite).
- `route_point_distance_m` (the file-local nearest-pose helper) is no longer referenced by prepend or suffix; it is retained as dead code with `[[maybe_unused]]` so the translation unit still links.

## 6. Acceptance Criteria

- NLP solver reports Infeasible 0 times during rule14-ho (excluding genuinely under-actuated edge cases that pass even with σ — those still escalate to fallback, but should be 0 on rule14-ho).
- `nlp_slack_norm` reported in ASDR for every cycle.
- σ > 0 only during close-range hard-infeasibility windows; σ = 0 once the maneuver develops CPA clearance.
- Phase 1+2 fixes still hold: `chain_summary.m6.encounter_state_last == ACTIVE` on rule14-ho; commit gate no longer rejects CPA-opening candidates on approach.
- CPA on rule14-ho ≥ 180 m (the probe floor).
- Phase 3.10 (cold-start route-coverage contract): on rule14-ho, `coordinate_transform_node` `/route_planning/route_plan_status` records at least one `ACCEPTED` decision during the M5 avoidance window; the M5 published plan's first waypoint matches the L2 nominal start (history prefix aligned); ownship heading is no longer pegged at 0° for the entire encounter (a real starboard turn executes).

If σ is large every cycle, this is a marker that the maneuver model or weights still need work — Phase 3.4 observability is what makes that visible.

## 7. Open Items

- `[TBD-HAZID-WP-04]` `w_slack = 1.0e4` initial value. HAZID RUN-001 (2026-08-19) calibrates against sea-trial data.
- `[TBD-FOLLOWUP]` Slack as exact-penalty vs augmented-Lagrangian: current design is exact-penalty (single static `w_slack`). If IPOPT convergence degrades with large `w_slack`, an outer-loop augmented-Lagrangian update is the fallback. Not needed for Phase 3 first cut.
- `[TBD-MULTI-SHIP]` Per-target slack is shared across horizon steps. Multi-ship encounter may need per-target-per-window slack (e.g., different targets peaking at different k). Defer until single-ship rule14-ho is GREEN.
