# Design Specification — SIL Bridge Avoidance Execution Path Refactor (`sil_topic_bridge.py`)

**Date:** 2026-06-03
**Scenario under test:** `colreg-rule14-ho` (head-on, Rule 14 → own-ship must turn **STARBOARD**)
**Owner module:** `docker/sil_topic_bridge.py` (the de-facto L4 bridge: converts M4/M5 L3 outputs → `/sil/actuator_cmd`)
**Status:** root cause confirmed + design de-risked by spikes; this spec defines the contained refactor to implement cleanly.

---

## 0. Why this exists (read first)

On a **clean cold start** of `colreg-rule14-ho`, the own ship does **not** execute the avoidance turn even though every upstream decision is correct. This was mis-framed for ~2 days as "nondeterminism" and as "M5 MPC doesn't converge". Deterministic A4000 debugging (2026-06-03) proved otherwise:

- **M4 decision is CORRECT** — starboard heading window grows `[16,34]→[16,73]`, `behavior=COLREG_AVOID`.
- **M5 plan is VALID** — `turn_radius=500m`, `status=NORMAL`, 4 waypoints. (MPC is fine; the parked Line-B MPC sign-fix in `git stash` is **out of scope** here.)
- The failure is entirely in the **bridge execution path**.

Two distinct, deterministic bugs were found and two have been spiked (see §4 for current state):

1. **Dead-stick (FIXED by spike "Fix#1"):** arming was gated on `_m3_activated_once`, and `_on_mission_goal` tore down `_avoidance_active` whenever mission `fsm_state < 3`. M3 sits in **AwaitingRoute (fsm=1, task_validity=0, current_target_wp=(0,0)) forever** when no route is delivered (the long-standing "K1 keystone" — no route → M3 never ACTIVE). So avoidance arming was permanently suppressed → ship held heading 0° (5/5 deterministic LOCK; `bridge_avoid_lines=0`, `arm_suppressed=0`).

2. **Stale one-shot target (PARTIALLY spiked by "G1", needs completion):** the avoidance target heading is latched **once** at arm time from whatever M4 window exists at that instant. `_on_behavior_plan` only set `_avoidance_target_heading_deg` when it was `None` (sticky); the arm path only runs when `not _avoidance_active`. If arm caught an early/transient window (e.g. `[0,4]` → `3.3°`), the target froze and the ship barely turned (~3°). The runs that historically turned 31° just got lucky on which window the one-shot capture saw — this is the real source of the apparent "turn-magnitude variance".

NLM evidence (`colav_algorithms`, 🟢 High; sources: MOOS-IvP docs, scenario-based MPC, VO/RVO):
- COLAV target heading must be tracked **continuously** every planner iteration (seed-not-freeze), never latched once.
- Encounter **release is event/geometry-based** (TCPA<0 ∧ dist>safe / range>completed_dist / rel-vel exits collision cone), **not** a fixed time-decay.
- COLAV must be **independent of mission/route availability** — coupling COLAV to mission-FSM is "a catastrophic vulnerability".
- `maritime_regulations` notebook (🔴 Low) lacks "past and clear"/resume criteria — see §6 certification TODO.

---

## 1. Current avoidance state machine (as audited, line refs vs pre-spike file)

State vars: `_avoidance_active` (master arm), `_avoidance_target_heading_deg` (the latched target), `_latch_release_triggered/_time/_offset_at_release_deg/_progress`, `_avoidance_armed_time`, `_LATCH_MIN_HOLD_S`.

| Stage | Function | Behaviour |
|---|---|---|
| ARM | `_on_avoidance_plan` (~L812) | `has_valid_plan = len(wp)>0 ∧ |turn_radius_m|>1e-6`; arms on valid plan; guards: `_m3_activated_once` (removed by Fix#1), `_MIN_ARM_SIM_T=10`. Sets target once via 5/6 formula. |
| TARGET | `_on_behavior_plan` (~L570) | One-shot: set target only when `None` (the stale-target bug). `target = h_min + (5/6)*(h_max−h_min)`; degenerate guard `h_span>300`. |
| PUBLISH | `_autopilot_step` (~L924) / `_compute_avoidance_autopilot` (~L994) | 2 Hz timer; avoidance branch fires while `_avoidance_active`; P-controller (`_avoidance_heading_controller`, Kp=1, max_rate=10°/s) on heading error; else waypoint-radius→rudder fallback. Actuator QoS = `_sensor_qos()` BEST_EFFORT. |
| RELEASE | `_trigger_latch_release` / `_compute_latch_offset` (5 s linear decay) / `_on_threat_state` / `_on_mission_goal` cond-2 | Fixed 5 s time-decay; primary CPA path depends on exact M2 string fields `cpa_status=="cleared"` ∧ `target_relative_position=="astern"`. |

---

## 2. Confirmed GAPs (from audit)

| # | Gap | Severity |
|---|---|---|
| G1 | Target latched once, never refreshed as M4 window grows `[16,34]→[16,73]` → under-turn. | 🔴 root cause of under-turn |
| G2 | One-shot capture + BEST_EFFORT QoS on M4 → which window is caught is luck → turn-magnitude variance. | 🔴 |
| G3 | Release is a fixed 5 s time-decay (contradicts all COLAV frameworks). | 🟠 |
| G4 | CPA release depends on fragile M2 string-field exact match; if absent, only the timer fires. | 🟠 |
| G5 | `_on_mission_goal` fsm<3 teardown re-couples COLAV to mission FSM (regression of the decouple intent). | 🔴 (cert anti-pattern) |
| G6 | Arm-time guards (`_m3_activated_once`, `_MIN_ARM_SIM_T`) are workarounds for the one-shot fragility. | 🟡 |
| G7 | 5/6 window heuristic is undocumented (acceptable if refreshed). | 🟡 |

---

## 3. Target design (the decided refactor)

**Verdict: contained refactor of target-tracking + release in `sil_topic_bridge.py` — not a rewrite, not isolated patches.** COLAV is decoupled from mission FSM and is gated only on a valid M5 plan + a stabilisation window, tracks the M4 window continuously, and releases on geometry.

### 3.1 Decouple from mission FSM (closes G5 — Fix#1 already does most)
- `_on_avoidance_plan`: arm does **not** depend on `_m3_activated_once`. Keep only the `_MIN_ARM_SIM_T` stabilisation guard.
- `_on_mission_goal`: when `fsm_state < 3`, **do not** reset `_avoidance_active`/`_avoidance_target_heading_deg`/latch. Mission FSM may gate transit/route-following only. Avoidance lifecycle is owned by `_on_avoidance_plan` (M5 plan validity) + `_on_lifecycle_status` (scenario deactivate) + the §3.3 release.

### 3.2 Continuous target tracking (closes G1/G2/G6 — "G1 spike" does this; keep + harden)
- `_on_behavior_plan`: **while `_avoidance_active ∧ behavior == COLREG_AVOID(!=0) ∧ not _latch_release_triggered`**, recompute `_avoidance_target_heading_deg` every message (drop the `is None` gate). Keep the `h_span>300` degenerate guard → leave last good target / fall back to M5 waypoint rudder.
- Rudder rate-limit (10°/s, existing) provides smoothing; add a small heading **deadband** (e.g. 2°) if M4-window chatter is observed.
- Consider RELIABLE QoS for `/l3/m4/behavior_plan` ONLY after confirming the M4 publisher side is RELIABLE-compatible (cross-module check — do not change M4).

### 3.3 Geometry-based release + turn bounding (closes G3/G4 — THE key new design decision)

**This is the one genuine design decision in this spec.** The current spike (G1 alone) over-turns to ~180° because avoidance never releases (M4 may never return to TRANSIT when M3 has no route, so the ship keeps tracking an ever-growing starboard window).

**Decided release criterion (primary, mission-independent):**
> Release (begin resuming course) when the threat is **geometrically resolved**:
> `TCPA < 0` (target has passed its CPA) **AND** `DCPA ≥ cpa_safe` (predicted closest approach now clears the safety margin),
> evaluated from **numeric** M2 ThreatState fields, with a minimum-hold debounce `_LATCH_MIN_HOLD_S` after arming.

**Secondary release (whichever fires first):** M4 `behavior` returns to TRANSIT (==0) for ≥ debounce. (Kept as a fast path when M4/mission is healthy, but NOT relied upon — M3 keystone may keep M4 in AVOID.)

**Turn bounding is an emergent property of §3.3:** because release fires the instant DCPA recovers, the starboard turn stops at the angle that achieves `DCPA ≥ cpa_safe` (expected ~30–60° for this scenario) instead of spiralling to 180°.

**Post-trigger behaviour:** keep the existing short linear decay (≤5 s) **only as rudder smoothing** back toward a steady course — NOT as the release trigger. With no route (M3 not ACTIVE), resume to a held steady heading; if a route is available, the transit autopilot resumes route-following.

**Data dependency (MUST verify in Task 0 of the plan):** does `/l3/m2/threat_state` (or whatever ThreatState topic the bridge subscribes to) carry **numeric** `cpa_m`/`dcpa_m` and `tcpa_s`? 
- If YES → implement the numeric criterion directly.
- If NO (only string `cpa_status`/`target_relative_position`) → this is a **cross-module gap**: per project rule §7 "indicate, don't fix other modules". Flag M2 ThreatState as needing numeric CPA/TCPA fields; as an interim, derive DCPA/TCPA in the bridge from own-ship + target kinematics (already available via `_last_ownship_raw` and the target-vessel subscription) — document this as a temporary bridge-local computation to be moved to M2.

### 3.4 Arm robustness (closes the 2/5 intermittent LOCK)
The G1 spike still shows **2/5 cold cycles never arm** (`bridge_avoid_lines=0`). Diagnose why `_on_avoidance_plan` doesn't reach the arm branch in those cycles (candidate: transient `turn_radius=0` plan, sim_t-guard timing, or `_avoidance_active` left True from a prior race). Fix so arming is **5/5 deterministic** on cold start.

---

## 4. Current repo state the implementer inherits

- **Branch / worktree:** `feat/d-demo1-bridge-deadstick` at `.worktrees/demo1-bridge-deadstick/`. Contains **Fix#1 (decouple)** + **G1 (continuous tracking)** edits to `docker/sil_topic_bridge.py`, **uncommitted**. All temporary debug `print()`s have been removed; diff is clean (verify with `git diff`).
- **Deployed to A4000** (`~/Code/mass-l3/docker/sil_topic_bridge.py`, volume-mounted, uncommitted on host) and verified on a clean backend: **3/5 GOOD (correct starboard turn) but over-turns ~180°, 2/5 LOCK, 0 WRONGWAY.** The earlier −169° wrong-way + backend crash were artifacts of a concurrent `docker build` and disappear on a clean backend.
- **Line B (M5 MPC sign-fix) is parked** in `git stash@{0}` on `main` ("WIP Line B: MPC J_colreg exp barrier (SIGN INVERTED, untested)") — **do NOT** pull it into this work.
- **Debug scripts** (untracked, reusable as the integration gate): `scripts/_dbg_variance.py` (5-cold-cycle classifier: GOOD/LOCK/WRONGWAY), `scripts/_dbg_bridge_state.py`. Run on A4000 with `source scripts/a4000-env.sh`.

---

## 5. Acceptance criteria (must ALL hold)

**AC-1 (unit):** bridge unit tests (pytest, see plan Task 2) cover: (a) target refreshes when M4 window changes mid-avoidance; (b) avoidance is NOT torn down by `mission_goal fsm<3`; (c) release fires on `TCPA<0 ∧ DCPA≥cpa_safe`, not on a timer; (d) arm fires on first valid M5 plan past `_MIN_ARM_SIM_T`. All pass.

**AC-2 (integration, A4000 — the primary gate):** `scripts/_dbg_variance.py` with `N_CYCLES=5`, `colreg-rule14-ho` @10×, clean backend (fresh `docker compose restart sil-nodes`, no concurrent build):
- **5/5 GOOD**, 0 LOCK, 0 WRONGWAY.
- `signed_dev ∈ [+25°, +75°]` every cycle (starboard, **bounded** — no 180° over-turn).
- low spread across cycles (max−min ≤ ~20°) — determinism.

**AC-3 (existing harness):** `scripts/a4000-acceptance.sh` A_turn goes from RED to a sane PASS (net starboard turn in band), A_rtf still in `[7,12]` @10×. `web/e2e/mvp_consistency.spec.ts` A_turn green. **Do NOT loosen test thresholds to pass.**

**AC-4 (COLREGs sanity):** the bounded turn passes port-to-port (starboard alteration), min CPA ≥ `cpa_safe` for the scenario, and the ship resumes a steady course after the target is clear (no permanent lock in AVOID, no spiral).

**AC-5 (regression):** transit/route-return (`_compute_transit_autopilot`), degenerate-window fallback, avoidance-pre-empts-transit, and ENVELOPE_*/M7-MRC stand-down behaviour are unchanged.

---

## 6. Certification TODO (flag, do not block this fix)
The `maritime_regulations` NLM notebook lacks numeric "past and clear" / safe-distance criteria (🔴). The `TCPA<0 ∧ DCPA≥cpa_safe` release is a sound **engineering** default but **not** a cited regulatory one. Before certification, verify the numeric safe-distance / abeam-astern resume thresholds against annotated COLREGs Rule 13/14/16 and case law. Record as `[TBD-HAZID]`-style open item; do not let it block the DEMO-1 functional fix.

---

## 7. Out of scope
- M5 MPC convergence / cost formula (Line B, stashed).
- M3 route keystone (M3 never reaching ACTIVE) — the decouple makes COLAV independent of it; the M3 root fix is a separate task ("两者都做" — bridge first, M3 later).
- Any change to M2/M4/M5/M6/M7 source (flag cross-module gaps, don't edit per §7).
