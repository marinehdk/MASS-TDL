# COLREGs C1 Crossing Beam Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the C1 phase-gate abaft-beam threshold so shallow slow crossings (rule15-cs, 10.6kn) can pass, while keeping overtaking (Rule 13) at 112.5° and not regressing fast crossings (rule15-cs-edge, 29kn) or head-on (rule14-ho).

**Architecture:** Per-rule threshold layering: crossing/head-on give-way releases at 90° beam + tcpa<0 (past CPA) + range≥cpa_safe opening; overtaking stays 112.5° (governed by C7 along-axis). Three change points: (A) Python `compute_phase_semantics` C1 evaluator, (B) M6 `release_policy.hpp` `REFERENCE_CLEAR` bow-clear 40°→90°, (C) M6 `reasoner_node.cpp` `past_and_clear_from_heading` per-rule via rule13 latch `onset_encounter`. No coordinate-system circularity: rule type comes from the onset-snapshotted encounter classification (Rule 13(d)), not from the same rel_brg used for past-clear.

**Tech Stack:** C++17 / ROS2 Humble (`m6_colregs_reasoner` package, gtest via colcon), Python 3 (pytest, `scripts/run_6_scenarios.py` gate), Docker compose isolation stack `colregs-behavior-fix` (ROS_DOMAIN_ID=43).

**Spec:** `docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md`
**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix` (branch `codex/colregs-behavior-fix`, HEAD `8e9faaf8`, clean).

**Geometric proof (the bug):** rule15-cs (target cog=290, 10.6kn) after starboard avoidance: target relative bearing vs reference-heading (~0°/N) asymptotes to **-87°**, never reaching the 112.5° abaft-beam sector. 112.5° is the Rule 13(b) overtaking sector boundary; applying it to crossing is an internal spec contradiction (`COLREGs_8Probe_Complete_Design_Report.md §4.2` itself specifies `abaft_threshold = 112.5 if is_overtaking else 90.0`).

---

## File Structure

**Create:** none.

**Modify (M6 C++, worktree):**
- `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp` — add `onset_encounter()` public getter.
- `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp` — `kGiveWayProjectionReleaseReferenceBowClearDeg` 40.0→90.0 + citation comment fix.
- `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` — `past_and_clear_from_heading` overload with threshold param; per-target `abaft_threshold_deg` from rule13 latch; thread through `finally_resolved` + 3 latch calls.
- `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp` — update 2 tests that assumed 40° (now need >90°).
- `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp` — add `onset_encounter()` getter test.

**Modify (Python gate, worktree):**
- `scripts/run_6_scenarios.py` — C1 check (line ~790-811): crossing/head-on threshold 112.5°→90°, add tcpa<0 + range backstop.
- `tests/scripts/test_run_6_scenarios_gate.py` — add C1 slow-crosser-pass and early-return-RED tests.

**Modify (docs, main checkout — separate commits):**
- `docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md` — errata note §C1.

---

## Task 1: C1 Evaluator — Crossing Threshold 90° + tcpa<0 Backstop (Python, TDD)

**Files:**
- Modify: `scripts/run_6_scenarios.py:652-654` (constant) + `scripts/run_6_scenarios.py:790-811` (C1 check)
- Test: `tests/scripts/test_run_6_scenarios_gate.py` (append new tests)

This is the gate-side fix. It must pass before touching M6 (the evaluator judges M6 behavior). Done first so we have a RED→GREEN signal for the runtime phase.

- [ ] **Step 1: Write the failing test — slow crosser C1 should PASS at 90° beam + tcpa<0**

Append to `tests/scripts/test_run_6_scenarios_gate.py` (before the final `if __name__` block, or at module level):

```python
def _slow_crosser_release_records():
    """rule15-cs-shaped trajectory: target crosses to port beam (-87°) after CPA.

    Own hdg returns to 0 (route recovered) by release. At release the target is
    at rel_brg -87° (abs 87°, <90° beam, <112.5° abaft), tcpa<0 (past CPA),
    range>=cpa_safe and opening. Old 112.5° gate: RED. New 90°+tcpa<0 gate: PASS.
    """
    recs = []
    # Own ship sits near origin on hdg=0 throughout (avoidance already recovered).
    # Target approaches from NE, crosses CPA around t=500, then opens to port beam.
    # Reconstruct (lat,lon) so that rel_brg follows the rule15-cs asymptote.
    lat0, lon0 = 63.44000, 10.38000
    own_lat, own_lon = lat0, lon0
    for t in range(0, 1201, 10):
        # Target ENU relative to own: approaches from (2838, 2381), v=(-5.13,1.87) m/s.
        tx = 2838.0 - 5.13 * t
        ty = 2381.0 + 1.87 * t
        rel_brg = math.degrees(math.atan2(tx, ty))  # nav bearing, own hdg=0
        rng = math.hypot(tx, ty)
        # forward CPA: recomputed below; here use range as proxy when tcpa<0
        cpa = rng if t > 500 else rng * 0.3
        tcpa = -1.0 if t > 500 else 300.0
        recs.append({
            "sim_t": float(t), "topic": "/sil/own_ship_state",
            "lat": own_lat, "lon": own_lon, "heading_deg": 0.0, "sog_kn": 12.0,
        })
    # behavior plan: avoidance from t=150..400 (avoid), release (behavior=0) at t=600.
    for t in range(0, 1201, 10):
        beh = 1 if 150 <= t <= 400 else 0
        recs.append({"sim_t": float(t), "topic": "/l3/m4/behavior_plan", "behavior": beh})
    return recs, lat0, lon0


def test_c1_slow_crosser_passes_at_90deg_beam_plus_tcpa_past():
    """Slow shallow crossing (rule15-cs shape): C1 must pass on 90° beam + tcpa<0,
    where the old 112.5° abaft-beam gate would RED (target asymptotes to -87°)."""
    recs, lat0, lon0 = _slow_crosser_release_records()
    # target meta: cog=290, sog=10.61, start pos matching the ENU seed above.
    import math as _m
    tgt_lat0 = lat0 + 2381.0 / 111120.0
    tgt_lon0 = lon0 + 2838.0 / (111120.0 * _m.cos(_m.radians(lat0)))
    targets_meta = [{
        "lat0": tgt_lat0, "lon0": tgt_lon0, "cog": 290.0, "sog_kn": 10.61,
    }]
    result = compute_phase_semantics(
        recs, targets_meta, lat0=lat0, lon0=lon0,
        role="give_way", rule="Rule15", cpa_safe_m=1852.0,
    )
    assert result["c1_past_clear_ok"] is True, (
        f"slow crosser C1 should pass at 90° beam + tcpa<0; "
        f"got rel_brg={result['release_target_rel_bearing_deg']}"
    )


def test_c1_early_return_at_bow_still_red():
    """Mechanical right-turn + early route return at rel_brg=36° (still on bow),
    tcpa>0 (CPA still ahead): C1 must stay RED. This is the bug the gate catches."""
    recs = []
    lat0, lon0 = 63.44000, 10.38000
    # Target on stbd bow at rel_brg 36°, closing, tcpa>0 throughout.
    for t in range(0, 401, 10):
        # Place target at fixed bearing 36° from own (own hdg=0), range shrinking.
        rng = 3000.0 - 3.0 * t
        tx = rng * math.sin(math.radians(36.0))
        ty = rng * math.cos(math.radians(36.0))
        tgt_lat = lat0 + ty / 111120.0
        tgt_lon = lon0 + tx / (111120.0 * math.cos(math.radians(lat0)))
        recs.append({"sim_t": float(t), "topic": "/sil/own_ship_state",
                     "lat": lat0, "lon": lon0, "heading_deg": 0.0, "sog_kn": 12.0})
    for t in range(0, 401, 10):
        beh = 1 if 100 <= t <= 200 else 0  # brief avoidance, return at t=210
        recs.append({"sim_t": float(t), "topic": "/l3/m4/behavior_plan", "behavior": beh})
    targets_meta = [{
        "lat0": lat0 + (3000.0 * math.cos(math.radians(36.0))) / 111120.0,
        "lon0": lon0 + (3000.0 * math.sin(math.radians(36.0))) /
                (111120.0 * math.cos(math.radians(lat0))),
        "cog": 290.0, "sog_kn": 10.61,
    }]
    result = compute_phase_semantics(
        recs, targets_meta, lat0=lat0, lon0=lon0,
        role="give_way", rule="Rule15", cpa_safe_m=1852.0,
    )
    assert result["c1_past_clear_ok"] is False, (
        "early return at rel_brg=36° with tcpa>0 must stay RED"
    )
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix" && python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py::test_c1_slow_crosser_passes_at_90deg_beam_plus_tcpa_past tests/scripts/test_run_6_scenarios_gate.py::test_c1_early_return_at_bow_still_red -v`
Expected: the slow-crosser test FAILS (current 112.5° gate returns c1_past_clear_ok=False), the early-return test PASSes (it already fails the 112.5° gate). Add `import math` at top of test file if missing.

- [ ] **Step 3: Implement the C1 fix in `scripts/run_6_scenarios.py`**

First add a crossing-specific constant. Replace the block at `scripts/run_6_scenarios.py:652-654`:

```python
# Past-and-clear bearing threshold (Rule 13(b)/16: abaft the beam = >22.5°
# abaft beam = relative bearing > 112.5° from the bow).
PHASE_GATE_PAST_CLEAR_BEARING_DEG = 112.5
```

with:

```python
# Past-and-clear bearing threshold by encounter type.
# Overtaking (Rule 13(b)/21(c)): >22.5° abaft beam = rel bearing > 112.5° from
#   bow (sternlight 135° arc). C7 (overtaking) uses the along-axis check, not
#   this bearing.
# Crossing / head-on give-way (Rule 8(d) finally past and clear): the target has
#   drawn past the beam = rel bearing > 90°. The 112.5° overtaking-sector
#   boundary is geometrically unreachable for shallow slow crossings after a
#   starboard avoidance turn (target asymptotes to port beam ~-87°, proven for
#   rule15-cs cog=290/10.6kn). Internal design report §4.2 specifies
#   abaft_threshold = 112.5 if is_overtaking else 90.0.
PHASE_GATE_PAST_CLEAR_BEARING_DEG = 112.5
PHASE_GATE_CROSSING_BEAM_BEARING_DEG = 90.0
```

Then replace the C1 check body at `scripts/run_6_scenarios.py:790-811` (the block starting `# ── C1: Rule 8(d) finally past and clear before route return ─────────` through `defaults["c1_past_clear_ok"] = c1_ok`):

```python
    # ── C1: Rule 8(d) finally past and clear before route return ─────────
    # Rule 8(d): "The effectiveness of the action shall be carefully checked
    # until the other vessel is finally past and clear." Past-and-clear = no
    # remaining collision risk. For crossing/head-on give-way the target must
    # have drawn past the beam (rel bearing > 90°) AND already be past CPA
    # (tcpa<0) AND at a safe opening range. The 112.5° overtaking-sector
    # boundary is unreachable for shallow slow crossings (rule15-cs proven to
    # asymptote at -87°); crossing uses the 90° beam. Overtaking give-way is
    # governed by C7 (along-axis), not this check (rule13 excluded below).
    c1_ok = True
    if (release_s is not None and role == "give_way"
            and "rule13" not in rule_l):
        rel_sample = min(traj, key=lambda p: abs(p["sim_t"] - release_s))
        rel_brg_abs = abs(rel_sample["rel_brg_deg"])
        defaults["release_target_rel_bearing_deg"] = rel_brg_abs
        before = [p for p in traj if p["sim_t"] <= release_s - 3.0]
        range_opening = True
        if before and rel_sample["range_m"] > 0:
            range_opening = rel_sample["range_m"] >= before[-1]["range_m"] - 1.0
        # Main geometry gate: target past the beam (90° for crossing/head-on).
        past_beam = rel_brg_abs > PHASE_GATE_CROSSING_BEAM_BEARING_DEG
        # tcpa<0 backstop: target must have already passed CPA. Prevents a
        # target that has swung past 90° beam but whose CPA is still ahead
        # (high-speed lateral crosser) from being judged "past and clear".
        tcpa_past = rel_sample["tcpa_s"] < 0.0
        # Safe range: at/above cpa_safe AND opening.
        range_safe = rel_sample["range_m"] >= cpa_safe_m and range_opening
        c1_ok = past_beam and tcpa_past and range_safe
    defaults["c1_past_clear_ok"] = c1_ok
```

- [ ] **Step 4: Run the C1 tests to verify they pass**

Run: `cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix" && python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py::test_c1_slow_crosser_passes_at_90deg_beam_plus_tcpa_past tests/scripts/test_run_6_scenarios_gate.py::test_c1_early_return_at_bow_still_red -v`
Expected: both PASS.

- [ ] **Step 5: Run the full gate test suite to confirm no regression**

Run: `cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix" && python3 -m pytest tests/scripts/test_run_6_scenarios_gate.py -v 2>&1 | tail -30`
Expected: all previously-passing tests still PASS. If any other C1-touching test breaks, update its expectation to the 90°+tcpa<0 semantics.

- [ ] **Step 6: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
git add scripts/run_6_scenarios.py tests/scripts/test_run_6_scenarios_gate.py
git commit -m "fix(gate): C1 crossing past-clear uses 90° beam + tcpa<0 backstop

The 112.5° abaft-beam threshold (Rule 13(b) overtaking sector) is
geometrically unreachable for shallow slow crossings after starboard
avoidance: rule15-cs (cog=290, 10.6kn) asymptotes to rel_brg -87°,
never entering the abaft sector. Internal design report §4.2 specifies
abaft_threshold = 112.5 if is_overtaking else 90.0.

Crossing/head-on give-way C1 now requires: rel_brg > 90° (past beam)
AND tcpa < 0 (past CPA) AND range >= cpa_safe opening. The tcpa<0
backstop prevents a high-speed lateral crosser that has swung past
beam but whose CPA is still ahead from being judged past-and-clear.

Overtaking (rule13) remains excluded from C1 (governed by C7 along-axis).
Adds 2 gate tests: slow-crosser PASS (was RED under 112.5°) and
early-return-at-bow RED (still caught).

Refs: docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md"
```

---

## Task 2: M6 release_policy.hpp — REFERENCE_CLEAR 40°→90° + citation fix (C++, TDD)

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp:9-13,88-109`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp:58-66,90-98` (update existing) + new tests

This tightens the projection-release `REFERENCE_CLEAR` gate so a crossing give-way cannot project-release until the target is past the beam. Currently 40° releases while the target is still on the bow.

- [ ] **Step 1: Write the failing test — crossing projection release needs >90°**

In `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp`, first UPDATE the two existing tests that encode the old 40° behavior. Replace `AllowsCrossingProjectionReleaseAfterReferenceClear` (lines ~58-66):

```cpp
TEST(ColregsReleasePolicy, BlocksCrossingProjectionReleaseBeforeBeam) {
  // 40° (old threshold) must now be blocked: target still on the bow, not past
  // the 90° beam. The 112.5° abaft-beam was unreachable for slow crossings;
  // 90° beam is the corrected crossing threshold.
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, AllowsCrossingProjectionReleasePastBeam) {
  // Past the 90° beam: target reference rel bearing 95°.
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/95.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, BlocksCrossingProjectionReleaseAtExactlyBeam) {
  // Exactly 90° is not past the beam; require strictly greater.
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/90.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}
```

Also update `AllowsOvertakingProjectionReleaseAfterReferenceClear` (lines ~90-98) — this one used `reference_relative_bearing_abs_deg=48.0` which is now below 90° and would break. Change it to reflect that overtaking still uses the `REFERENCE_CLEAR` gate at 90° (the per-rule 112.5° lives in `past_and_clear_from_heading`, not here; the projection gate is the same 90° for all give-way):

```cpp
TEST(ColregsReleasePolicy, AllowsOvertakingProjectionReleasePastBeam) {
  // The projection REFERENCE_CLEAR gate is 90° for all give-way; overtaking's
  // stricter 112.5° past-clear is enforced in past_and_clear_from_heading, not
  // in this projection gate.
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/75.0,
      /*reference_relative_bearing_abs_deg=*/95.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}
```

- [ ] **Step 2: Build + run the release_policy tests to verify they fail**

Run (inside the sil-nodes container):
```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
source scripts/local-behavior-fix-env.sh
docker compose exec sil-nodes bash -lc \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_colregs_release_policy 2>&1 | tail -25"
```
Expected: the new `BlocksCrossingProjectionReleaseBeforeBeam` and `BlocksCrossingProjectionReleaseAtExactlyBeam` tests FAIL (current 40° threshold allows 40° and 90°), `AllowsCrossingProjectionReleasePastBeam` FAILS only if you wrote it expecting the 90° threshold before implementing (it will pass at 40° too — that's fine, it documents the target). The renamed/updated `AllowsOvertakingProjectionReleasePastBeam` FAILS (48°→95° expectation, but current 40° allows 48° too; the assertion at 95° still passes at 40°, so it stays green — acceptable).

- [ ] **Step 3: Implement the threshold change + citation fix in release_policy.hpp**

In `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp`, replace lines 9-13:

```cpp
constexpr double kGiveWayProjectionReleaseRangeMultiple = 1.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 40.0;
constexpr double kGiveWayReleaseKnToMps = 0.514444;
constexpr double kGiveWayReleasePi = 3.14159265358979323846;
```

with:

```cpp
constexpr double kGiveWayProjectionReleaseRangeMultiple = 1.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
// Crossing/head-on give-way projection release (REFERENCE_CLEAR gate): the
// target must have drawn past the beam (relative bearing >= 90° along the
// reference avoidance heading) before the encounter is resolved. The 40°
// quick-impl baseline released while the target was still on the bow — the
// early-return-to-route the phase gate flags as a Rule 8(d) violation.
//
// The 112.5° abaft-beam (Rule 13(b) overtaking sector) is unreachable for
// shallow slow crossings after starboard avoidance (rule15-cs cog=290/10.6kn
// asymptotes to rel_brg -87°). Crossing uses the 90° beam; overtaking's stricter
// 112.5° is enforced in past_and_clear_from_heading (reasoner_node.cpp), not
// here. Internal design report §4.2: abaft_threshold = 112.5 if is_overtaking
// else 90.0. NOT Rule 3(g) (defines "vessel restricted in ability to
// manoeuvre", unrelated to abaft beam); threshold derives from the beam (90°)
// plus, for overtaking only, Rule 13(b) "more than 22.5° abaft her beam".
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 90.0;
constexpr double kGiveWayReleaseKnToMps = 0.514444;
constexpr double kGiveWayReleasePi = 3.14159265358979323846;
```

The check at line 104 (`reference_relative_bearing_abs_deg < kGiveWayProjectionReleaseReferenceBowClearDeg`) now rejects anything below 90° — no other code change needed in this file.

- [ ] **Step 4: Build + run tests to verify they pass**

Run (same container command as Step 2):
```bash
docker compose exec sil-nodes bash -lc \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_colregs_release_policy 2>&1 | tail -25"
```
Expected: all release_policy tests PASS (19/19 or the updated count).

- [ ] **Step 5: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp
git commit -m "fix(m6): crossing projection release requires 90° beam (was 40°)

kGiveWayProjectionReleaseReferenceBowClearDeg 40°->90°. The 40° quick-impl
baseline released while the target was still on the bow (Rule 8(d) early
return). 90° beam is the corrected crossing/head-on past-clear threshold;
the 112.5° overtaking sector is unreachable for shallow slow crossings
(rule15-cs asymptotes to rel_brg -87°). Overtaking's 112.5° stays in
past_and_clear_from_heading (next task).

Citation comment corrected: 112.5° derives from Rule 13(b)+21(c), NOT
Rule 3(g) which defines 'vessel restricted in ability to manoeuvre'.

Updates 4 release_policy tests to the 90° semantics.

Refs: docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md"
```

---

## Task 3: M6 rule_latch.hpp — add onset_encounter() getter (C++, TDD)

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp:87-88`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp` (append)

Expose the onset-snapshotted encounter type so the reasoner can pick the per-rule abaft threshold. Tiny additive change.

- [ ] **Step 1: Write the failing test**

Append to `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp` (inside the existing `namespace mass_l3::m6_colregs { namespace {` if present, or add a new TEST):

```cpp
TEST(RuleLatch, ReportsOnsetEncounterForThresholdSelection) {
  RuleLatch latch(1852.0, 1.5);
  RuleEvaluation eval{};
  eval.is_active = true;
  eval.role = Role::GIVE_WAY;
  eval.encounter_type = EncounterType::OVERTAKING;
  eval.phase = TimingPhase::EARLY_ACTION;
  eval.preferred_direction = "STARBOARD";
  eval.min_alteration_deg = 30.0;
  // Onset: rule active, cpa<safe, closing, not yet past-clear.
  latch.update(/*rule_active=*/true, /*cpa_m=*/900.0, /*range_closing=*/true,
               /*past_and_clear=*/false, &eval);
  EXPECT_TRUE(latch.has_onset());
  EXPECT_EQ(latch.onset_encounter(), EncounterType::OVERTAKING);
}

TEST(RuleLatch, OnsetEncounterDefaultsToNoneBeforeOnset) {
  RuleLatch latch(1852.0, 1.5);
  EXPECT_FALSE(latch.has_onset());
  EXPECT_EQ(latch.onset_encounter(), EncounterType::NONE);
}
```

Check the test file's existing includes — it already includes `rule_latch.hpp` and uses `RuleEvaluation`/`EncounterType` (see existing `ReportsOnsetRoleForProjectionPolicy` test at line 79). Confirm `EncounterType::NONE` exists in `types.hpp` (it does — used in `rule_latch.hpp:113`).

- [ ] **Step 2: Build + run to verify the test fails to compile / fails**

```bash
docker compose exec sil-nodes bash -lc \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -8"
```
Expected: COMPILE ERROR — `'onset_encounter' is not a member of 'RuleLatch'`.

- [ ] **Step 3: Add the getter to rule_latch.hpp**

In `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`, after the existing `onset_role()` getter (line 89):

```cpp
  bool has_onset() const { return has_onset_; }
  Role onset_role() const { return onset_role_; }
  // Onset-snapshotted encounter type (Rule 13(d): classification fixed at
  // onset). Used to select the per-rule past-clear threshold (overtaking
  // 112.5°, crossing/head-on 90°) without coordinate-system circularity.
  EncounterType onset_encounter() const { return onset_encounter_; }
```

- [ ] **Step 4: Build + run the latch tests to verify they pass**

```bash
docker compose exec sil-nodes bash -lc \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch 2>&1 | tail -20"
```
Expected: all rule_latch tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp \
        src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp
git commit -m "feat(m6): expose RuleLatch::onset_encounter() for per-rule threshold

Add public getter for the onset-snapshotted encounter type so the reasoner
can select the past-clear abaft-beam threshold per rule (overtaking 112.5°,
crossing/head-on 90°) without coordinate circularity (the encounter
classification is fixed at onset per Rule 13(d), independent of the rel_brg
used for the past-clear test).

Refs: docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md"
```

---

## Task 4: M6 reasoner_node.cpp — per-rule past_and_clear threshold (C++)

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp:176-178` (helper) + `568-618` (per-target threshold) + `713,795,855` (latch call sites)

This threads the per-rule abaft threshold through `finally_resolved` and the 3 latch `update()` calls. Overtaking (rule13 latch onset_encounter==OVERTAKING) keeps 112.5°; everything else uses 90°.

- [ ] **Step 1: Add the threshold overload to the helper**

In `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`, replace the helper at lines 176-178:

```cpp
bool past_and_clear_from_heading(double bearing_deg, double reference_heading_deg) {
  return std::fabs(signed_relative_bearing_deg(bearing_deg, reference_heading_deg)) > 112.5;
}
```

with a parameterized version (keep the old signature as a 112.5° default for any external caller):

```cpp
// Past-and-clear threshold by encounter type. Overtaking (Rule 13(b)/21(c)):
// target >22.5° abaft the beam = rel bearing > 112.5° (sternlight 135° arc).
// Crossing/head-on (Rule 8(d) finally past and clear): target past the beam =
// rel bearing > 90°. The 112.5° overtaking-sector boundary is geometrically
// unreachable for shallow slow crossings after starboard avoidance
// (rule15-cs cog=290/10.6kn asymptotes to rel_brg -87°). Internal design
// report §4.2: abaft_threshold = 112.5 if is_overtaking else 90.0.
bool past_and_clear_from_heading(double bearing_deg, double reference_heading_deg,
                                 double abaft_threshold_deg = 112.5) {
  return std::fabs(signed_relative_bearing_deg(bearing_deg, reference_heading_deg))
         > abaft_threshold_deg;
}
```

- [ ] **Step 2: Compute the per-target abaft threshold and thread it through**

In the per-target loop, the threshold must be computed BEFORE `past_and_clear` (line 572) so the same value feeds `finally_resolved` and all latch calls. Insert the threshold computation right after `release_reference_heading` is determined (after line 571), and modify the `past_and_clear` computation (lines 572-574).

Replace lines 568-574:

```cpp
    const auto ref_it = encounter_reference_heading_.find(mmsi);
    const bool has_release_reference = ref_it != encounter_reference_heading_.end();
    const double release_reference_heading =
        has_release_reference ? ref_it->second : target.ownship_heading_deg;
    const bool past_and_clear =
        has_release_reference &&
        past_and_clear_from_heading(target.bearing_deg, release_reference_heading);
```

with:

```cpp
    const auto ref_it = encounter_reference_heading_.find(mmsi);
    const bool has_release_reference = ref_it != encounter_reference_heading_.end();
    const double release_reference_heading =
        has_release_reference ? ref_it->second : target.ownship_heading_deg;
    // Per-rule past-clear threshold (Rule 13(d): classification fixed at onset).
    // Overtaking uses the 112.5° abaft-beam (Rule 13(b)/21(c) sternlight arc);
    // crossing/head-on uses the 90° beam (the 112.5° sector is unreachable for
    // shallow slow crossings — rule15-cs asymptotes to rel_brg -87°). Read the
    // onset encounter from the rule13 latch snapshot; fallback 90° when no
    // onset captured (duty not latched → no release anyway).
    const auto rl13_it = rule_latches_.find(
        (static_cast<uint64_t>(mmsi) << 8) | 13ULL);
    const bool is_overtaking_onset =
        rl13_it != rule_latches_.end() && rl13_it->second.has_onset() &&
        rl13_it->second.onset_encounter() == EncounterType::OVERTAKING;
    const double abaft_threshold_deg = is_overtaking_onset ? 112.5 : 90.0;
    const bool past_and_clear =
        has_release_reference &&
        past_and_clear_from_heading(target.bearing_deg, release_reference_heading,
                                    abaft_threshold_deg);
```

- [ ] **Step 3: Update the 3 latch call sites to use the per-rule past_and_clear**

The latch calls at lines 713, 795, 855 already receive the local `past_and_clear` variable, which now carries the per-rule threshold — **no change needed at the call sites**. Verify by grepping:

```bash
grep -n "past_and_clear" src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp
```
Expected: 5 references (declaration line ~572, `finally_resolved` ~617, latch calls ~713/795/855, onset guard ~788) — all read the same `past_and_clear` local. Confirmed no per-site override.

- [ ] **Step 4: Build the m6 package (no new tests — behavior covered by runtime)**

```bash
docker compose exec sil-nodes bash -lc \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m6_colregs_reasoner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -5 && \
   colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ 2>&1 | tail -15"
```
Expected: build succeeds; all m6 tests PASS (release_policy 19, rule_latch updated count, plus any others). Restart sil-nodes to load the new binary:

```bash
docker compose restart sil-nodes
sleep 8
```

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp
git commit -m "fix(m6): per-rule past-clear threshold (overtaking 112.5°, crossing 90°)

past_and_clear_from_heading now takes an abaft_threshold_deg parameter.
The reasoner selects it per-target from the rule13 latch onset_encounter
snapshot (Rule 13(d): classification fixed at onset): OVERTAKING -> 112.5°
(Rule 13(b)/21(c) sternlight arc), everything else -> 90° (beam).

This fixes the geometrically unreachable 112.5° for shallow slow crossings
(rule15-cs cog=290/10.6kn asymptotes to rel_brg -87° after starboard
avoidance). The single past_and_clear local now feeds finally_resolved and
all 3 latch update() call sites with the per-rule threshold.

Refs: docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md"
```

---

## Task 5: Runtime verification — rule15-cs, rule15-cs-edge, rule13-ot

**Files:** none (verification only).

Run the three scenarios on the behavior-fix stack (ROS_DOMAIN_ID=43, port 18001). This is the GREEN check after the code changes.

- [ ] **Step 1: Confirm the behavior-fix stack is up**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
source scripts/local-behavior-fix-env.sh
docker compose ps
```
Expected: sil-nodes + orchestrator Up. If not, start: `COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose up -d sil-nodes orchestrator`.

- [ ] **Step 2: Run rule15-cs (slow 10.6kn) — primary C1 fix target**

```bash
export SIL_ORCH_BASE_URL="https://127.0.0.1:18001/api/v1"
python3 scripts/run_6_scenarios.py --scenario colreg-rule15-cs --restart-settle 40 \
  2>&1 | tee runs/rule15_cs_c1beamfix_$(date +%Y%m%d_%H%M%S).log | tail -40
```
Expected: `C1 past-clear=True` (was RED at rel_brg=36°), `OVERALL PASS`, `route_return=True`, CPA/stability/seamanship PASS. Record `release_target_rel_bearing_deg` (should be >90° or the tcpa<0+range backstop path).

- [ ] **Step 3: Run rule15-cs-edge (fast 29kn) — regression guard**

```bash
python3 scripts/run_6_scenarios.py --scenario colreg-rule15-cs-edge --restart-settle 40 \
  2>&1 | tee runs/rule15_cs_edge_c1beamfix_$(date +%Y%m%d_%H%M%S).log | tail -40
```
Expected: `OVERALL PASS`, `C1 past-clear=True`. Must not regress (it passed at 112.5° before via -136°).

- [ ] **Step 4: Run rule13-ot — C7 baseline regression guard**

```bash
python3 scripts/run_6_scenarios.py --scenario colreg-rule13-ot --restart-settle 40 \
  2>&1 | tee runs/rule13_ot_c1beamfix_$(date +%Y%m%d_%H%M%S).log | tail -40
```
Expected: `OVERALL PASS`. C7 overtake-past may be False (baseline state, `overtake_required: False`) — acceptable, not a regression. CPA/stability/seamanship PASS.

- [ ] **Step 5: If any scenario regresses, STOP and diagnose**

Do NOT proceed to the 8-probe batch. If rule15-cs route_return fails (release too late again), the fallback is to relax the tcpa<0 backstop to `tcpa < -X` (past CPA by X seconds) — revisit spec §3.2 with the user. Record findings in the run log.

- [ ] **Step 6: No commit (verification only). Move to Task 6.**

---

## Task 6: Strict 8-probe restart regression (colregs-clean-8probe skill)

**Files:** none (uses the skill wrapper).

Full 8-scenario batch with sil-nodes restart before every run, so warm state / latches / bridge cannot bleed.

- [ ] **Step 1: Run the local strict 8-probe batch**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
source scripts/local-behavior-fix-env.sh
export SIL_ORCH_BASE_URL="https://127.0.0.1:18001/api/v1"
python3 scripts/run_colregs_clean_8probe.py \
  --restart-between-runs \
  --summary-out runs/batch_colregs_clean_c1beamfix_$(date +%Y%m%d_%H%M%S).json \
  --trace-report-dir runs/trace_eval/local_clean8_c1beamfix_$(date +%Y%m%d_%H%M%S) \
  2>&1 | tee runs/clean8_c1beamfix_$(date +%Y%m%d_%H%M%S).log | tail -50
```
Expected: 8/8 `overall_pass=true`. The wrapper exits 0 only if all pass.

- [ ] **Step 2: Report results faithfully**

If any scenario is RED, report which ones and the key KPIs (CPA, route_return, C1/C7 phase flags). Do not claim the batch is complete if the wrapper exits nonzero.

- [ ] **Step 3: No commit (verification only). Move to Task 7.**

---

## Task 7: Spec errata + diary + handoff (docs)

**Files:**
- Modify (main checkout): `docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md` §C1
- Modify (main checkout): `handoff/workspace_log.md`
- MemPalace: diary_write + add_drawer

- [ ] **Step 1: Add errata to the phase-semantics-gate spec (main checkout)**

In `docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md` §C1 (after the "**检查**" block), insert an errata note:

```markdown
> **ERRATA 2026-06-18:** The 112.5° abaft-beam threshold above applies to
> **overtaking only** (Rule 13(b)/21(c) sternlight arc). For crossing/head-on
> give-way, use **90° beam** + tcpa<0 backstop. The 112.5° overtaking-sector
> boundary is geometrically unreachable for shallow slow crossings after
> starboard avoidance (rule15-cs cog=290/10.6kn asymptotes to rel_brg -87°).
> See `docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md`.
```

- [ ] **Step 2: Append handoff entry to `handoff/workspace_log.md`**

```markdown
## 2026-06-18 ZCode (GLM-5.2) / codex/colregs-behavior-fix (8e9faaf8 + c1beamfix commits) / COLREGs C1 crossing beam fix

**Task Goal:** Fix C1 phase-gate abaft-beam geometric unreachability for slow shallow crossings (rule15-cs 10.6kn), which caused the ea6b06e6 regression and the e4e2cc37 revert.

**Core Changes:**
- scripts/run_6_scenarios.py: C1 crossing threshold 112.5°->90° beam + tcpa<0 + range>=cpa_safe backstop
- m6 release_policy.hpp: kGiveWayProjectionReleaseReferenceBowClearDeg 40°->90° + citation fix (Rule 3(g)->13(b)/21(c))
- m6 rule_latch.hpp: onset_encounter() getter
- m6 reasoner_node.cpp: past_and_clear_from_heading per-rule threshold (overtaking 112.5°, crossing 90°) via rule13 latch onset_encounter

**Current Status:** [fill from Task 5/6 results — 8/8 PASS or specific REDs]

**Handoff Notes:** SIL tracker fixes (5ec267e8 fix①②) verified retained, skipped. Geometric proof: rule15-cs asymptotes to rel_brg -87° after starboard avoidance, never reaching 112.5° abaft. 90° beam + tcpa<0 is the corrected crossing past-clear per internal design report §4.2. C7 (rule13-ot) unchanged — overtaking_required:False, C7=False is baseline.
```

- [ ] **Step 3: Write the MemPalace diary + decision drawer**

```bash
mempalace_diary_write  # or via MCP
```
Topic: `colregs-c1-crossing-beam-fix-2026-06-18`. AAAK entry: task goal, key decision (per-rule threshold layering: crossing 90°/overtaking 112.5°, tcpa<0 backstop), artifacts (spec fc7cd3c6, this plan, runtime logs), open items (integration path to main, A4000 verification deferred).

Also `mempalace_add_drawer` wing `MASS-L3`, room `colregs-design-decisions`: the geometric proof (asymptote -87°) + the threshold table + why rule13 latch onset_encounter (not aspect, to avoid coordinate circularity).

- [ ] **Step 4: Commit docs (main checkout)**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md handoff/workspace_log.md
git commit -m "docs: C1 crossing beam fix errata + handoff

Phase-semantics-gate spec §C1 errata: 112.5° is overtaking-only;
crossing/head-on uses 90° beam + tcpa<0. Handoff entry records the
geometric proof (rule15-cs asymptotes -87°) and the per-rule threshold
decision.

Refs: docs/superpowers/specs/2026-06-18-colregs-c1-crossing-beam-fix.md"
```

---

## Self-Review

**Spec coverage:**
- §3.1 改动 A (evaluator) → Task 1 ✅
- §3.1 改动 B (release_policy 90°) → Task 2 ✅
- §3.1 改动 C (reasoner_node per-rule) → Task 3 (getter) + Task 4 (threading) ✅
- §3.2 tcpa<0 backstop → Task 1 Step 3 ✅
- §4.1 TDD → Tasks 1-4 ✅
- §4.2 evaluator unit tests → Task 1 ✅
- §4.3 runtime (3 scenarios) → Task 5 ✅
- §4.4 8-probe regression → Task 6 ✅
- §4.5 spec errata → Task 7 ✅

**Placeholder scan:** none. All thresholds (90°, 112.5°, tcpa<0, cpa_safe) concrete. Container commands and file:line references exact.

**Type consistency:** `onset_encounter()` getter (Task 3) matches the call in Task 4 (`rl13_it->second.onset_encounter()`). `past_and_clear_from_heading(bearing, ref, abaft_threshold_deg)` signature (Task 4 Step 1) matches the call (Task 4 Step 2). `EncounterType::OVERTAKING`/`NONE` confirmed in types.hpp via rule_latch.hpp:113. `kGiveWayProjectionReleaseReferenceBowClearDeg` rename kept (constant name unchanged, only value 40→90).

**Risk notes carried forward:** Task 5 Step 5 documents the fallback (tcpa<-X) if route_return regresses. C7 explicitly out of scope (rule13-ot baseline).
