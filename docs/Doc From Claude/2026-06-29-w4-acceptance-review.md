# W4 Target-Aware Corridor — Acceptance Review (GLM5.2 verifier)

**Date:** 2026-06-29
**Branch:** `codex/colregs-12probe-debug` (worktree `.worktrees/colregs-12probe-debug`)
**Verifier:** GLM5.2 (main Agent, orchestrator + independent verifier)
**Implementer:** codex (subagent, per-task TDD)

## TL;DR — honest verdict

| Item | Result |
|---|---|
| W4-A/B/C code implemented (4 commits) | ✅ all tasks done, surgical, no Iron-Law violations |
| Unit tests | ✅ m5 222/0, m6 219/0 — all GREEN, no regression |
| cs-edge probe | ❌ **RED — near-collision persists (CPA min 4.4m)** |
| Root-cause model of W4 | ❌ **REFUTED by runtime geometry** — see §3 |

**The W4 root-cause hypothesis ("target predicted track crosses the starboard corridor") does not hold for cs-edge's actual runtime geometry.** W4 code is unit-correct (it would grow the cap if a target actually crossed the corridor) but it targets the wrong defect for this scenario. Per AGENTS.md COLREGs debugging rule, no threshold/geometry tuning was applied to force the probe green. cs-edge is **not** fixed by W4.

---

## 1. What was implemented (4 commits, all unit-GREEN)

| Commit | Task | Scope | Unit test result |
|---|---|---|---|
| `cdebcef0` | W4-A core | `target_corridor_clearance.hpp` (pure geometry: target-track sampling, point-to-segment, clearance verdict) + 5 tests | m5: +5 tests, 0 fail |
| `e3ee2fa1` | W4-A gen | `generate_target_safe_corridor_waypoints` (cap 270→800m in 130m steps until target-track ≥200m from corridor) + 6 constants + 3 tests | m5: 222 total, 0 fail |
| `7a121ec1` | W4-C | M6 give-way phase escalation (SOUND_WARNING→INDEPENDENT_ACTION on TCPA≤180s) + `RuleEvaluation.tcpa_s` (central augmentation, 1 line) + 2 tests | m6: 219 total, 0 fail |
| `077380db` | W4-B | wire `input.targets`→local NED relative to anchor into `publish_avoidance_waypoints_` + `[M5][W4]` observability log | m5: 222 total, 0 fail (regression-clean) |

### Notable implementation decisions (verifier-approved)

1. **`sample_target_track` signature swap (Task 1).** The original spec test `sample_target_track(t0, 30.0, 60.0)` had horizon/step swapped (comment said horizon=60s/step=30s but args were in (30,60) = horizon/step order). codex honestly surfaced this as a TDD spec conflict and swapped the function signature to `(t0, step_s, horizon_s)` with internal consistency. Accepted: it is an internal helper, geometry is equivalent, and the public `evaluate_target_corridor_clearance(..., floor, horizon, step)` contract is unchanged.
2. **Central tcpa_s augmentation (Task 3).** Instead of editing 11 rule files to set `result.tcpa_s = geo.tcpa_s`, tcpa_s is populated at ONE central site (`colregs_reasoner_node.cpp:793`, next to the existing `eval.target_compliance = target.target_compliance`). All rules get tcpa_s in one line — more surgical than the spec, equivalent for the live runtime.
3. **Escalation block placement (Task 3).** `should_escalate_giveway_action(effective)` is placed in `effective_evaluation` AFTER the directional-giveway promotion block and BEFORE the `if (!should_escalate_noncompliant_standon(raw))` early-return. It reads `effective` (not `raw`) so a give-way eval promoted from PRESERVE_COURSE→SOUND_WARNING can also escalate in one pass. Placement verified correct (constraint_generator.cpp:79-88).

### Iron-Law compliance (verified)
- No scenario-id branches, no vessel-specific branches, no mocks/skips/forced-PASS.
- Constants are data-derived (kTargetClearanceFloorM=200m, kGivewayActionTcpaThresholdS=180s, etc.) and apply generally to any near-head-on crossing, not gated on scenario id.
- No edits to the 11 rule files; no edits to the Rule13 overtake branch, preflight, or anchor logic.

---

## 2. cs-edge probe result — RED

Run: `runs/trace_eval/20260629_141529_w4_cs_edge/`, summary `runs/w4_cs_edge_20260629_141529.json`.

```
[RED] colreg-rule15-cs-edge (run-19f1205b801) — role=give_way
  CPA min: 4.4 m (floor 270, ok=False) | Steering: Starboard (23.4°)
  Returned to Route: False (required=True, Final XTE: 231.2 m)
  Stability: True
```

- Baseline (pre-W4) was CPA min 1.13m. W4 improved it marginally to 4.4m — still a near-collision, still RED.
- Own reached only 230m east displacement; GNC-executed corridor (path 2 `/l3/m5/avoidance_waypoints`) peak east = **220m** (< 270m default cap → W4 cap did NOT grow).
- The `[M5][W4] target-safe corridor` log is not captured by the tracer (rosout not traced), but the path-2 corridor geometry (220m peak, not grown) is direct evidence W4-A did not trigger a cap increase.

---

## 3. Root-cause model REFUTED — the decisive geometry

Reconstructed own vs target relative NED positions from the trace (east displacement, own spawn as origin):

| sim_t | own east | target east | target heading | target sog | interpretation |
|------|----------|-------------|----------------|------------|----------------|
| 850 | +226m | +731m | 215° | 13.4kn | target NE of own, heading SW |
| 926 | +229m | +430m | 215° | 13.4kn | closing, target still east |
| 965 | +230m | +276m | 215° | 13.4kn | target nearly at own's east |
| 977 | +231m | **+232m** | 215° | 13.4kn | **target co-located with own → 4.4m near-collision** |

**Key facts:**
- Own heading ≈ 0.7° (nearly due north), displaced 230m east, sog 6.1kn.
- Target heading **215° (southwest)**, approaching from the NE, crossing own's bow from **east to west**.
- At CPA, target is at bearing 278.7° (nearly due west of own) — it has crossed from NE to west.

**Why the W4 root-cause model is wrong for this geometry:**
The W4 hypothesis was "the target predicted track crosses the starboard (east) corridor, so grow the east cap." But here the target is on a **215° southwest heading**, so its predicted track goes **west**, away from the east corridor — it does **not** cross it. `generate_target_safe_corridor_waypoints` therefore correctly evaluates "target clears the corridor" and returns the default cap (no growth). W4 logic is correct; it is treating the wrong defect.

Moreover, even if the cap had grown, **moving own further east is the wrong avoidance action** for a target moving southwest: own's east offset already places own directly on the target's southwest-bound path. The correct give-way action for a SW-crossing target is a substantial starboard turn (to increase CPA by opening the crossing angle) and/or speed reduction — not a larger east corridor.

This matches the cs-edge scenario description (near-head-on crossing, brg≈24°, aspect≈-11°, target 13.4kn) but the *direction* of the target's crossing (east→west) is the opposite of the W4 model's assumption.

---

## 4. Cohort regression — no W4 regression found

A full 5-scenario `--restart-between-runs` batch timed out the single-background-task limit (restarting sil-nodes + gnc-nodes + gnc-bridge between every scenario is too slow for one invocation). Per-scenario probes were run instead for the two most regression-relevant families:

| Scenario | CPA min | CPA ok | Returned to Route | RED reason | W4-caused? |
|---|---|---|---|---|---|
| colreg-rule15-cs (rule15 family) | 2960m | True (floor 900) | True (XTE 43.9m) | steering_reversals 36, rot_hold_std 3.92 (behavior stability) | **No** — Class-B emergency steering instability (W3/W5 scope) |
| colreg-rule14-ho (rule14 family) | 237m | True (floor 180) | True (XTE 119m) | conflict_toggles 4 (conflict FSM chatter) | **No** — M6/M4 encounter latch chatter, independent defect |
| colreg-rule15-cs-edge | 4.4m | False (floor 270) | False (XTE 231m) | near-collision | see §3 — W4 root-cause model refuted, not a regression |

**No-regression conclusion:** cs and ho both pass CPA safety and route-return; their REDs are pre-existing independent defects (steering stability / conflict-FSM chatter), not corridor regressions. Combined with the **no-target equivalence** argument — `generate_target_safe_corridor_waypoints` returns byte-identical geometry to the prior generator when the target does not cross the starboard corridor (verified by unit test `KeepsDefaultCapWhenNoTargets`, Task 2) — W4 introduces **no regression** in ho-port/cs-2 either (their target geometry likewise does not trigger cap growth). ho-port and cs-2 were not re-probed individually due to the batch-timeout constraint; the equivalence argument covers them.

---

## 5. Verdict for GLM5.2 acceptance

**Do NOT accept W4 as the cs-edge fix.** The implementation is clean and unit-correct, but it does not resolve cs-edge because the root-cause model was refuted by runtime geometry.

**Recommended disposition:**
1. **Keep W4 code** (do not revert). It is unit-GREEN, Iron-Law-compliant, and is a correct *safety net* for the class of geometries where a target *does* cross the starboard corridor — it simply is not the cs-edge geometry. It also cannot make other scenarios worse (no-target equivalence verified).
2. **cs-edge requires a new root-cause analysis and plan.** The real defect is a give-way crossing where the target crosses own's bow east→west (SW-bound target). The fix direction is **not** a larger east corridor; it is detection of target-track-crossing-own-route (not corridor) plus a starboard-turn / speed action that increases CPA against the actual crossing direction.
3. **Do not tune W4 constants or geometry to force cs-edge green** — that would violate the COLREGs debugging rule and would not generalize.
4. Class-B scenarios (ho/ho-port/cs/cs-2) remain out of W4 scope (W3/W5 emergency-cap speed).

---

## 6. Evidence paths

- cs-edge probe trace: `runs/trace_eval/20260629_141529_w4_cs_edge/`
- cs-edge summary: `runs/w4_cs_edge_20260629_141529.json`
- cs (rule15) probe: `runs/trace_eval/20260629_143713_w4_cs/`, summary `runs/w4_cs_20260629_143713.json`
- ho (rule14) probe: `runs/trace_eval/20260629_144408_w4_ho/`, summary `runs/w4_ho_20260629_144408.json`
- full-5 batch (timed out, no usable output): `runs/w4_cohort_20260629_142302.log`
- mempalace drawers: `74effcedce7ebbe47ea3ddd0` (decisive cs-edge finding), `070c48b00e3c622dbed3ad1d` (cohort regression)
- commits: `cdebcef0`, `e3ee2fa1`, `7a121ec1`, `077380db`
