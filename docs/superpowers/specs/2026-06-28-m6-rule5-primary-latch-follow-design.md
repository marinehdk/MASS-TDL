# M6 Rule5 Look-out Primary-Latch Follow Design

Date: 2026-06-28

Status: design draft for COLREGs 12-probe Class A defect (Rule5 churn)

## Problem

Rule 5 (proper look-out) is declared as a continuous, unconditional obligation in
`rule5_lookout.cpp` (`result.is_active = true` unconditionally). However,
`ColregsReasonerNode::run_reasoning()` applies a Rule 7 risk-of-collision gate to
all non-primary rules (5/6/7/8/16/17/18/19) in the else-branch at
`colregs_reasoner_node.cpp:923-942`:

```cpp
} else {  // non-primary rules
  const bool give_way_role = (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
  if (!give_way_role) {
    const bool raw_risk = (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);
    if (!raw_risk) {
      eval.is_active = false;
    }
  }
}
```

The gate keys on instantaneous CPA. During an active head-on give-way encounter,
own ship's starboard avoidance turn transiently opens the CPA projection above
`cpa_safe_m`, which makes `raw_risk` false and gates Rule 5 off. On the next
reasoning cycle the geometry shifts slightly and Rule 5 re-activates. This
produces high-frequency Rule 5 in/out churn of the `active_rules` set.

Primary rules (13/14/15) do not exhibit this churn because they carry onset
latches (`rule_latches_`) and the encounter FSM that hold their classification
through own ship's maneuver (Rule 13(d)). Rule 5 has no such latch, so its
active state flaps against the held primary rules.

### Evidence (fresh WIP image, `runs/trace_eval/20260628_095633_rule14_cohort_wip`)

`colreg-rule14-ho` M6 `active_rules` flips, short intervals below 2 s:

- t=181.148 `{14,16,18}` -> t=181.385 `{5,7,8,14,16,18}` (0.24 s)
- t=1330.48 `{14,16,18}` -> t=1331.046 `{5,7,8,14,16,18}` (0.57 s)
- t=1620.144 `{14,16,18}` -> t=1621.81 `{5,14,16,18}` (1.67 s)

Layer-2 module oracle: `M6_COLREGsReasoner: RULE_INSTABILITY` (5/6 others GREEN).
Layer-3 integration: `L4_colregs_compliance FAIL`, diagnosis
`first_broken_stage=M6 failing_gate=PHASE`.

Downstream propagation: M4 `behavior_toggles=9` (baseline 2), GNC
`gnc_plan_id_changes=2993` (plan identity churn), GNC `DEFERRED avoidance_active`.

### Baseline comparison

The same Rule 5 churn exists in the pre-WIP baseline
(`runs/trace_eval/20260628_060553_clean12_gnc_after_rule15_chain_fixes/colreg-rule14-ho`):
Rule 5 present 82 % of M6 samples, 7 transitions, but with slower intervals. The
WIP Rule 14 head-on course-gate widening (6 deg -> 15 deg tolerance) holds Rule 14
active longer through own ship's turn, which amplifies the desync between Rule 5's
instantaneous CPA gate and Rule 14's latched hold. This is a long-standing design
tension that the WIP exposed, not a defect introduced solely by the WIP.

## Scope

In scope:

- `colregs_reasoner_node.cpp` else-branch risk-gate: add a Rule 5 bypass so Rule 5
  follows the primary rule (13/14/15) latch lifecycle instead of instantaneous CPA.
- Affects `colreg-rule14-ho` and `colreg-rule14-ho-intelligent` (both M6
  RULE_INSTABILITY).

Out of scope (separate specs):

- `colreg-rule15-ot-boundary` M6 WRONG_RULE_CLASSIFICATION / WRONG_ROLE_ASSIGNMENT
  (Rule 13/15 overtake-boundary classification failure, distinct root cause).
- Class B integration defect: M5 plan identity churn (ho-port, cs, cs-2,
  cs-intelligent). Tracked under the TDL-GNC avoidance interface contract.
- Class C L4/GNC execution defect: `colreg-rule15-cs-edge` CPA near-miss from
  high closing speed plus GNC speed cap and decel-distance limits.

## Design

### Rule 5 bypass in the non-primary risk-gate

In the else-branch of the per-rule evaluation loop, before applying the Rule 7
risk gate, check whether Rule 5 should follow a primary rule latch instead:

```cpp
} else {  // non-primary rules (5/6/7/8/16/17/18/19)
  const bool give_way_role =
      (eval.role == Role::GIVE_WAY || eval.role == Role::BOTH_GIVE_WAY);
  if (!give_way_role) {
    const bool raw_risk =
        (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);

    // Rule 5 (proper look-out) is a continuous obligation that must persist
    // through an active encounter. The primary-rule latch (13/14/15) holds the
    // encounter classification through own ship's avoidance maneuver (Rule
    // 13(d)); Rule 5 must not be gated off by an instantaneous CPA transient
    // while a primary rule is latched for this target, otherwise Rule 5 flaps
    // in/out of the active set and triggers M6 RULE_INSTABILITY.
    bool rule5_follows_primary_latch = false;
    if (eval.rule_id == 5) {
      rule5_follows_primary_latch =
          rule13_latch_it != rule_latches_.end() && rule13_latch_it->second.latched() ||
          rule14_latch_it != rule_latches_.end() && rule14_latch_it->second.latched() ||
          rule15_latch_it != rule_latches_.end() && rule15_latch_it->second.latched();
    }

    if (!raw_risk && !rule5_follows_primary_latch) {
      eval.is_active = false;
    }
  }
}
```

The primary-rule latch iterators (`rule13_latch_it`, `rule14_latch_it`,
`rule15_latch_it`) are already computed earlier in the per-target block
(`colregs_reasoner_node.cpp:681-683`) and are in scope. `RuleLatch::latched()`
returns true from onset through release, matching the "hold through maneuver"
window Rule 5 must follow.

### Semantics

- During an active encounter (any primary rule latched for this target), Rule 5
  stays active with its default `role=FREE, phase=PRESERVE_COURSE,
  preferred_direction=HOLD`. It does not change behavior; it only stops churning.
- After the primary rule releases (latch cleared, target past-and-clear), Rule 5
  falls back to the existing Rule 7 risk gate. A target with no collision risk
  (tcpa < 0 or cpa >= cpa_safe) correctly gates Rule 5 off again.
- Rule 5's `preferred_direction=HOLD` is unchanged, so it does not alter M4's
  avoidance direction. The fix removes a source of M4 instability, not a
  direction command.

### What is not changed

- `rule5_lookout.cpp` evaluator: unchanged. Rule 5 still returns
  `is_active=true, role=FREE, preferred_direction=HOLD`.
- Other non-primary rules (6/7/8/16/17/18/19): unchanged. They keep the existing
  Rule 7 risk gate. Rule 8 action-to-avoid and Rule 17 stand-on in-extremis
  semantics are untouched.
- Rule 14 head-on course-gate tolerance (WIP 15 deg): unchanged. The WIP fix for
  `colreg-rule14-ho-port` (correct Rule 14 classification, no S180 reversal)
  remains.
- Module oracle RULE_INSTABILITY thresholds: unchanged.
- Encounter FSM, give-way duty latch, stand-on latch: unchanged.

## Verification

### Unit tests (container, no full sim)

- M5 unit test `test_avoidance_waypoint_gen` must remain 43/43 (no M5 change).
- M6 unit test `test_rule5_lookout.cpp`: extend to assert Rule 5 returns
  `is_active=true, role=FREE, HOLD` regardless of input geometry (documents the
  unconditional contract the bypass relies on).

### Module oracle (Layer-2 functional test)

After rebuild, re-run module oracle on fresh traces:

- `colreg-rule14-ho`: M6 must move from RULE_INSTABILITY to GREEN.
- `colreg-rule14-ho-intelligent`: M6 must move from RULE_INSTABILITY to GREEN.
- `colreg-rule14-ho-port`: M6 must stay GREEN (regression guard).

Expected: Rule 5 present near 100 % of M6 samples during the active encounter
window for all three Rule 14 scenarios, with no sub-2 s flip intervals.

### Integration test (Layer-3 same-rule cohort)

Re-run Rule 14 cohort strict probe (restart-between-runs, three-container GNC
restart):

- `colreg-rule14-ho`: target overall GREEN (baseline was GREEN pre-WIP).
- `colreg-rule14-ho-port`: must not regress (Layer-2 stays GREEN; integration
  result may still be RED from the separate Class B plan-id churn, which is out
  of scope here and must be reported, not silently fixed).
- `colreg-rule14-ho-intelligent`: M6 oracle GREEN; integration result triaged
  separately.

### Regression guard

- Rule 5 must still gate off for a target with no collision risk and no latched
  primary rule (tcpa < 0 or cpa >= cpa_safe). Verify via a trace slice where the
  encounter has fully cleared: Rule 5 absent from `active_rules` post-release.
- M4 `behavior_toggles` should drop back toward baseline (2) for `colreg-rule14-ho`.
- GNC `gnc_plan_id_changes` should drop materially (downstream of M6 stability),
  though the separate Class B rolling-plan issue may keep it nonzero.

## Acceptance Criteria

Class A (Rule 5 churn) is resolved when:

- `colreg-rule14-ho` and `colreg-rule14-ho-intelligent` Layer-2 M6 oracle is
  GREEN (no RULE_INSTABILITY) on a fresh WIP-image rebuild.
- Rule 5 no longer exhibits sub-2 s active/inactive flip intervals during the
  active encounter window on either scenario.
- `colreg-rule14-ho-port` Layer-2 oracle stays GREEN (no regression).
- No change to `rule5_lookout.cpp`, Rule 14 gate constants, oracle thresholds,
  or any other non-primary rule's gate.
- No scenario-id conditionals, vessel-specific branches, mocks, skips, or forced
  PASS paths introduced.
