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

## 6. Acceptance Criteria

- NLP solver reports Infeasible 0 times during rule14-ho (excluding genuinely under-actuated edge cases that pass even with σ — those still escalate to fallback, but should be 0 on rule14-ho).
- `nlp_slack_norm` reported in ASDR for every cycle.
- σ > 0 only during close-range hard-infeasibility windows; σ = 0 once the maneuver develops CPA clearance.
- Phase 1+2 fixes still hold: `chain_summary.m6.encounter_state_last == ACTIVE` on rule14-ho; commit gate no longer rejects CPA-opening candidates on approach.
- CPA on rule14-ho ≥ 180 m (the probe floor).

If σ is large every cycle, this is a marker that the maneuver model or weights still need work — Phase 3.4 observability is what makes that visible.

## 7. Open Items

- `[TBD-HAZID-WP-04]` `w_slack = 1.0e4` initial value. HAZID RUN-001 (2026-08-19) calibrates against sea-trial data.
- `[TBD-FOLLOWUP]` Slack as exact-penalty vs augmented-Lagrangian: current design is exact-penalty (single static `w_slack`). If IPOPT convergence degrades with large `w_slack`, an outer-loop augmented-Lagrangian update is the fallback. Not needed for Phase 3 first cut.
- `[TBD-MULTI-SHIP]` Per-target slack is shared across horizon steps. Multi-ship encounter may need per-target-per-window slack (e.g., different targets peaking at different k). Defer until single-ship rule14-ho is GREEN.
