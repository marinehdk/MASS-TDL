# COLREGs 8-Probe Trace Evaluator — Complete Design Proposal v1.0

**Status**: Design Proposal (post-review)
**Date**: 2026-06-16
**Companion**: `8-Probe-Review-Report.md` (评审报告)
**Scope**: Complete evaluator redesign addressing all P0/P1 review findings

---

## 0. Design Philosophy

### 0.1 Core Principle: "Test What a Navigator Would Judge"

The evaluator must answer the same questions a qualified navigator asks when reviewing a voyage:

1. **Was it safe?** — Did the ships pass at a safe distance? (COLREGs Rule 8(d))
2. **Was it lawful?** — Did each vessel fulfill its COLREGs obligations? (Rules 13-17)
3. **Was it efficient?** — Did the vessel return to track without excessive detour? (Good seamanship)
4. **Was it stable?** — Were the maneuvers smooth and predictable? (Rule 8(b): "readily apparent")

### 0.2 Separation of Concerns

```
┌─────────────────────────────────────────────────────────┐
│                    Scenario Runner                       │
│  (configure → activate → poll → collect trace)          │
│  run_6_scenarios.py → run_colregs_clean_8probe.py       │
└──────────────────────┬──────────────────────────────────┘
                       │ trace_current.jsonl
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  TraceEvaluator                          │
│  (pure function: trace + scenario_spec → verdict)       │
│  No ROS2, no orchestrator, no network                   │
└──────────────────────┬──────────────────────────────────┘
                       │ EvaluationReport (JSON)
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  Report Formatter                        │
│  (JSON → CLI summary / HTML / dashboard)                │
└─────────────────────────────────────────────────────────┘
```

### 0.3 Verdict Structure: Three Independent Dimensions

Replace the flat AND with three independent dimensions plus an overall:

```python
@dataclass
class Verdict:
    safety_pass: bool          # CPA floor + no collision + M7 no critical veto
    colregs_pass: bool         # Rule compliance + role lifecycle + past-and-clear
    mission_pass: bool         # Route return + corridor + no excessive detour
    stability_pass: bool       # No fishtail/flap/chatter
    
    # Quality scores (0.0-1.0, do NOT replace hard gates)
    safety_quality: float      # How much margin above the floor
    colregs_quality: float     # How well the rule was followed
    mission_quality: float     # How efficiently the route was recovered
    stability_quality: float   # How smooth the maneuvers were
    
    @property
    def overall_pass(self) -> bool:
        return self.safety_pass and self.colregs_pass and self.mission_pass and self.stability_pass
    
    # Diagnostic fields
    failed_gates: List[str]    # Which specific gates failed
    first_failure_t: float     # Timestamp of first failure
    warnings: List[str]        # Non-failing concerns
```

---

## 1. CPA Threshold Model (Revised)

### 1.1 Three-Tier Threshold System

Replace the single `cpa_min_m_ge` with a three-tier system:

| Tier | Name | Value (FCB 45m LOA) | Meaning | Verdict Impact |
|---|---|---|---|---|
| **T1: Danger Floor** | Immediate danger | **185.2m** (0.1 NM, 4.1L) | Physical collision risk — ships too close | **Hard RED** — safety_fail |
| **T2: Safety Floor** | Minimum safe passing | **max(300m, 6.7L)** = 300m | Below this = unsafe passing | **safety_pass = false** |
| **T3: Quality Domain** | Good seamanship target | **scenario-dependent** | Below this = quality deduction | **safety_quality reduction** |

### 1.2 Scenario-Specific Thresholds

| Scenario Profile | T1 Danger | T2 Safety | T3 Quality | Basis |
|---|---|---|---|---|
| `corridor_close_start` (R14 对遇) | 185.2m | 300m | 405m (9L) | Fujii 8L open water [R3]; MASS legal 9L [R11] |
| `corridor_overtaking` (R13 追越) | 185.2m | 300m | 405m (9L) | Goodwin Sector 3: 0.45 NM [R4] |
| `corridor_boundary` (边界探针) | 185.2m | 300m | 405m (9L) | Same as above |
| `open_water_crossing` (R15 穿越) | 185.2m | 500m | 926m (0.5 NM) | Wang 0.5-0.6 NM [R6]; Goodwin Sector 1: 0.85 NM [R4] |
| `standon_in_extremis` (R17 末段) | 185.2m | 250m | 405m (9L) | In-extremis accepts lower safety margin [R18] |

### 1.3 Threshold Provenance Output

Every evaluation report MUST include:

```json
{
  "cpa_thresholds": {
    "danger_floor_m": 185.2,
    "safety_floor_m": 300.0,
    "quality_domain_m": 405.0,
    "profile": "corridor_close_start",
    "basis": "Fujii 8L open water domain; MASS legal 9L last-chance turn",
    "nm_equivalent": [0.1, 0.162, 0.219],
    "loa_multiplier": [4.1, 6.7, 9.0],
    "source_confidence": ["high", "moderate", "moderate"]
  }
}
```

---

## 2. Seven-Layer Evaluator (Revised)

### Layer 1: Scenario Validity (Pre-Flight)

**Purpose**: Verify the scenario is a valid test before evaluating the system.

```python
class Layer1ScenarioValidity:
    def evaluate(self, scenario_spec, no_action_trace=None) -> LayerResult:
        checks = {
            "has_conflict_geometry": self._check_dcpa_no_action(scenario_spec),
            "valid_role_assignment": self._check_role(scenario_spec),
            "valid_rule_coverage": self._check_rule(scenario_spec),
            "odd_within_bounds": self._check_odd(scenario_spec),
            "no_action_baseline": self._check_no_action(no_action_trace),
        }
        return LayerResult(passed=all(checks.values()), checks=checks)
    
    def _check_dcpa_no_action(self, spec) -> bool:
        """No-action DCPA must be < 500m to prove the scenario has a real conflict."""
        # Computed from initial geometry
        dcpa = compute_no_action_dcpa(spec)
        return dcpa < 500.0
    
    def _check_no_action(self, trace) -> bool:
        """If no-action baseline trace exists, verify collision would occur."""
        if trace is None:
            return True  # Graceful degradation
        min_dist = min_distance_in_trace(trace)
        return min_dist < 100.0  # Would collide without action
```

**New**: `no_action_baseline` check — if a no-action trace exists, verify the scenario actually creates a collision risk.

### Layer 2: Safety Floor

**Purpose**: Hard safety红线.

```python
class Layer2SafetyFloor:
    def evaluate(self, trace, thresholds) -> LayerResult:
        min_separation = trace.min_separation_m()
        
        checks = {
            "above_danger_floor": min_separation >= thresholds.danger_floor_m,
            "above_safety_floor": min_separation >= thresholds.safety_floor_m,
            "no_m7_critical_veto": not trace.has_critical_veto(),
        }
        
        quality = min(1.0, (min_separation - thresholds.safety_floor_m) / 
                      (thresholds.quality_domain_m - thresholds.safety_floor_m))
        
        return LayerResult(
            passed=checks["above_danger_floor"] and checks["above_safety_floor"] and checks["no_m7_critical_veto"],
            checks=checks,
            quality=max(0.0, quality),
        )
```

**Key change**: Two-tier safety (danger + safety), with quality as a continuous score.

### Layer 3: Dynamic Risk (Revised — Approach/Post-Pass Split)

**Purpose**: Quantify risk exposure, split by encounter phase.

```python
class Layer3DynamicRisk:
    def evaluate(self, trace, thresholds) -> LayerResult:
        # Phase detection
        phases = self._detect_phases(trace)
        
        # Approach phase: from conflict onset to CPA
        approach = self._evaluate_approach(trace, phases, thresholds)
        
        # Post-pass phase: from CPA to past-and-clear
        post_pass = self._evaluate_post_pass(trace, phases, thresholds)
        
        checks = {
            "approach_danger_exposure_s": approach.danger_s <= thresholds.max_approach_danger_s,
            "approach_warning_exposure_s": approach.warning_s <= thresholds.max_approach_warning_s,
            "post_pass_close_domain_s": post_pass.close_domain_s <= thresholds.max_post_pass_close_s,
            "post_pass_distance_increasing": post_pass.distance_increasing,
            "risk_recovery_ok": approach.risk_recovery_ok,
        }
        
        return LayerResult(passed=all(checks.values()), checks=checks)
    
    def _detect_phases(self, trace) -> EncounterPhases:
        """Split trace into approach / CPA / post-pass phases."""
        cpa_idx = trace.argmin_separation()
        cpa_t = trace.t[cpa_idx]
        
        # Post-pass: TCPA < 0 AND closing_speed <= 0 AND target abaft
        post_pass_start = None
        for i in range(cpa_idx, len(trace)):
            if (trace.tcpa[i] < 0 and 
                trace.closing_speed[i] <= 0 and
                self._target_abaft(trace, i)):
                post_pass_start = trace.t[i]
                break
        
        return EncounterPhases(
            approach=(trace.t[0], cpa_t),
            post_pass=(post_pass_start or cpa_t, trace.t[-1]),
        )
    
    def _target_abaft(self, trace, idx) -> bool:
        """Is the target abaft the own ship's beam?"""
        rel_bearing = trace.rel_bearing[idx]
        return abs(rel_bearing) > 90.0
```

**Key changes**:
1. Explicit phase detection (approach vs post-pass)
2. Post-pass checks: distance increasing, target abaft
3. Rule 13 exception: overtaking duty persists regardless of TCPA sign

### Layer 4: COLREG Compliance (Revised — Per-Rule Detailed)

**Purpose**: Evaluate rule-specific compliance with full behavioral context.

```python
class Layer4ColregsCompliance:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        rule = scenario_spec.encounter.rule
        role = scenario_spec.encounter.give_way_vessel
        
        # Common checks
        checks = {
            "role_lifecycle": self._check_role_lifecycle(trace, role),
            "past_and_clear": self._check_past_and_clear(trace),
        }
        
        # Rule-specific checks
        if rule == "Rule14":
            checks.update(self._check_rule14(trace))
        elif rule == "Rule13":
            checks.update(self._check_rule13(trace))
        elif rule in ("Rule15", "Rule15_Stbd"):
            checks.update(self._check_rule15(trace))
        elif rule == "Rule17":
            checks.update(self._check_rule17(trace))
        
        return LayerResult(passed=all(checks.values()), checks=checks)
    
    def _check_rule14(self, trace) -> dict:
        """Rule 14: Head-on — both alter starboard, port-to-port pass."""
        return {
            "turn_starboard": trace.max_starboard_dev >= trace.max_port_dev and trace.max_starboard_dev >= 10.0,
            "no_port_turn": trace.max_port_dev < 15.0,  # No significant port turn
            "port_to_port_pass": self._verify_port_pass(trace),
            "no_left_turn_crossing": not self._misclassified_as_crossing(trace),
        }
    
    def _check_rule13(self, trace) -> dict:
        """Rule 13: Overtaking — keep clear until finally past and clear."""
        return {
            "keep_clear_duty_held": self._duty_held_throughout(trace, "give_way"),
            "no_reclassification": self._no_rule_reclassification(trace),
            "safe_follow_or_pass": self._safe_follow_or_pass(trace),
            "past_and_clear_before_release": self._past_and_clear_before_release(trace),
        }
    
    def _check_rule15(self, trace) -> dict:
        """Rule 15/16: Crossing give-way — early, substantial, starboard, no cross-ahead."""
        return {
            "turn_starboard": trace.max_starboard_dev >= trace.max_port_dev and trace.max_starboard_dev >= 10.0,
            "early_action": self._action_before_tcpa(trace, threshold_s=120.0),
            "substantial_action": trace.max_heading_change >= 30.0,
            "no_cross_ahead": not self._crossed_ahead_of_target(trace),
            "passed_astern": self._passed_astern_of_target(trace),
        }
    
    def _check_rule17(self, trace) -> dict:
        """Rule 17: Stand-on — hold course initially, act only when required."""
        return {
            "hold_course_phase": self._hold_course_for_initial_phase(trace, hold_frac=0.75),
            "no_premature_action": trace.max_heading_dev_in_hold < 10.0,
            "independent_action_taken": self._independent_action_taken(trace),
            "no_port_turn_for_port_vessel": self._no_port_turn_for_port_vessel(trace),
        }
    
    def _crossed_ahead_of_target(self, trace) -> bool:
        """Did own ship cross ahead of the target vessel? (Rule 15 violation)"""
        for i in range(len(trace)):
            # Own ship's along-track position relative to target
            own_along = self._along_track(trace, i, reference="target")
            target_along = 0.0  # Target is the reference
            lateral = self._cross_track(trace, i, reference="target")
            
            if own_along > target_along and abs(lateral) < 200.0:
                # Own ship is ahead of target and within 200m laterally
                return True
        return False
    
    def _action_before_tcpa(self, trace, threshold_s) -> bool:
        """Did the give-way vessel start avoiding before TCPA reached threshold?"""
        avoidance_onset = trace.avoidance_onset_t()
        if avoidance_onset is None:
            return False
        tcpa_at_onset = trace.tcpa_at(avoidance_onset)
        return tcpa_at_onset >= threshold_s
```

**Key changes**:
1. **Cross-ahead detection** for Rule 15 (critical missing check)
2. **Reaction time** quantification (Rule 8(b)/16 "early")
3. **Substantial action** threshold (Rule 8(b) "readily apparent" → ≥30° heading change)
4. **Rule 17(c)** constraint: no port turn for port-side vessel
5. **Past-and-clear** verification before conflict release

### Layer 5: Route Recovery

**Purpose**: Evaluate route return quality.

```python
class Layer5RouteRecovery:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        expected = scenario_spec.expected_outcome
        
        checks = {
            "returned_to_route": self._returned(trace, expected),
            "final_xte_ok": abs(trace.final_xte) < expected.route_return_xte_m_lt,
            "final_heading_ok": abs(trace.final_heading_dev) < expected.route_return_heading_deg_lt,
            "corridor_ok": abs(trace.max_route_xte) < expected.route_corridor_pass_limit_m,
            "no_corridor_violation": abs(trace.max_route_xte) < expected.route_corridor_half_width_m,
        }
        
        # Quality metrics
        quality = {
            "final_xte_m": trace.final_xte,
            "final_heading_dev_deg": trace.final_heading_dev,
            "max_route_xte_m": trace.max_route_xte,
            "recovery_time_s": trace.recovery_time_s,
            "transit_after_avoidance_s": trace.transit_after_avoidance_s,
        }
        
        return LayerResult(passed=all(checks.values()), checks=checks, quality=quality)
```

### Layer 6: Seamanship / Efficiency (Revised)

**Purpose**: Evaluate maneuver quality beyond safety.

```python
class Layer6Seamanship:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        route_distance = scenario_spec.route_distance_m
        
        checks = {
            "path_length_ratio_ok": trace.path_length_ratio <= 1.35,
            "integrated_xte_ok": trace.integrated_abs_xte_m_s <= self._xte_budget(trace, scenario_spec),
            "route_crossing_ok": trace.route_crossing_overshoot_count <= 1,
            "no_excessive_detour": trace.max_route_xte <= scenario_spec.expected_outcome.route_corridor_pass_limit_m,
        }
        
        quality = {
            "path_length_ratio": trace.path_length_ratio,
            "integrated_abs_xte_m_s": trace.integrated_abs_xte_m_s,
            "route_crossing_overshoot_count": trace.route_crossing_overshoot_count,
            "speed_variation": self._speed_variation(trace),
        }
        
        return LayerResult(passed=all(checks.values()), checks=checks, quality=quality)
    
    def _xte_budget(self, trace, spec) -> float:
        """Normalize XTE budget by scenario duration."""
        duration_s = trace.duration_s
        # Allow ~200m average XTE for the scenario duration
        return 200.0 * duration_s
```

**Key change**: XTE budget normalized by scenario duration instead of fixed 300,000 m·s.

### Layer 7: Stability / Solver Health (Revised)

**Purpose**: Detect fishtail, flap, chatter.

```python
class Layer7Stability:
    """Revised from existing stability_scorer.py — the most mature layer."""
    
    REVISED_THRESHOLDS = {
        # Tightened from current values
        "max_behavior_toggles": 2,           # Unchanged
        "max_plan_valid_segments": 2,        # Unchanged
        "max_steering_reversals_gw": 3,      # Was 4 — tightened
        "max_steering_reversals_so": 3,      # Was 5 — tightened
        "max_rot_hold_std_dps": 0.8,         # Was 1.5 — tightened
        "max_conflict_toggles": 2,           # Unchanged
        "max_role_onset_changes": 0,         # Unchanged (Rule 13(d))
        "max_premature_giveway_deg": 10.0,   # Unchanged
        "min_give_way_turn_deg": 10.0,       # Was 5.0 — tightened (Rule 8(b) "readily apparent")
        "rot_deadband_dps": 1.0,             # Unchanged
    }
```

**Key changes**:
1. `steering_reversals` tightened: give-way 4→3, stand-on 5→3
2. `rot_hold_std` tightened: 1.5→0.8 °/s
3. `min_give_way_turn` tightened: 5°→10° (Rule 8(b) "readily apparent")

---

## 3. Heading-On Post-Pass Rule (Spec §8 Implementation)

```python
class PostPassClassifier:
    """Implements Spec §8: Heading-On Post-Pass Rule."""
    
    def classify(self, trace, idx) -> str:
        """Returns 'active_threat' | 'post_pass_clearance' | 'post_pass_close'."""
        tcpa = trace.tcpa[idx]
        closing_speed = trace.closing_speed[idx]
        rel_bearing = trace.rel_bearing[idx]
        distance = trace.separation[idx]
        
        # Active collision threat: TCPA >= 0 OR closing, not past-and-clear
        if tcpa >= 0 or closing_speed > 0:
            return "active_threat"
        
        # Post-pass: TCPA < 0, closing_speed <= 0, target abaft
        target_abaft = abs(rel_bearing) > 90.0
        
        if target_abaft and distance > trace.separation[idx - 1]:
            return "post_pass_clearance"  # Distance increasing — good
        
        if target_abaft:
            return "post_pass_close"  # Target abaft but distance not increasing
        
        return "active_threat"  # Not abaft — still active
    
    def rule13_exception(self, trace, idx, rule) -> str:
        """Rule 13: overtaking duty persists regardless of TCPA sign."""
        if rule == "Rule13":
            # Overtaking vessel must keep clear until finally past and clear
            # "Finally past and clear" = overtaken vessel fully ahead and drawing away
            if not self._fully_past(trace, idx):
                return "active_threat"  # Duty persists
        return self.classify(trace, idx)
```

---

## 4. Revised Scenario Set

### 4.1 Current 8-Probe (Retained with Fixes)

| ID | Rule | Fix Required |
|---|---|---|
| `colreg-rule14-ho` | R14 | Fix CPA threshold: 185.2→300m safety floor |
| `colreg-rule14-ho-port` | R14 | Fix CPA threshold: 185.2→300m; fix README inconsistency |
| `colreg-rule13-ot` | R13 | Add wind/current variant |
| `colreg-rule15-cs` | R15 | Add cross-ahead check |
| `colreg-rule15-cs-2` | R15 | Add reaction time check |
| `colreg-rule15-cs-edge` | R15 | Mark as "geometry stress test" (target 29 kn) |
| `colreg-rule15-ot-boundary` | R15 | Mark as "geometry stress test" (target 45 kn) |
| `colreg-rule17-cr-so` | R17 | Fix CPA threshold: 185.2→250m (in-extremis) |

### 4.2 New Scenarios (Recommended Additions)

| ID | Rule | Purpose | Priority |
|---|---|---|---|
| `colreg-rule15-cs-port` | R15 | Port-side crossing (OS give-way) — tests left turn to pass astern | P1 |
| `colreg-rule17-cr-so-stbd` | R17 | Right-side stand-on — tests Rule 17(c) no-port-turn constraint | P1 |
| `colreg-rule14-ho-wind` | R14 | Head-on with moderate wind/current — tests robustness | P2 |
| `colreg-rule13-ot-slow` | R13 | Overtaking with small speed difference (2 kn) — tests patience | P2 |

### 4.3 No-Action Baseline Requirement

Each scenario MUST have a corresponding no-action baseline run:

```python
def generate_no_action_baseline(scenario_spec):
    """Run the scenario with own ship maintaining course and speed."""
    modified = deepcopy(scenario_spec)
    modified.ownShip.controller = "none"  # No avoidance
    modified.metadata.simulation_settings.total_time = 300.0  # Shorter
    return modified
```

The baseline trace proves: "Without avoidance, the ships would collide (min_distance < 100m)."

---

## 5. Report Output Format

### 5.1 Per-Scenario Report

```json
{
  "scenario_id": "colreg-rule14-ho-v2.0",
  "run_id": "run-20260616-001",
  "timestamp": "2026-06-16T10:30:00Z",
  
  "verdict": {
    "safety_pass": true,
    "colregs_pass": true,
    "mission_pass": true,
    "stability_pass": true,
    "overall_pass": true,
    "safety_quality": 0.85,
    "colregs_quality": 0.92,
    "mission_quality": 0.78,
    "stability_quality": 0.95
  },
  
  "cpa_thresholds": {
    "danger_floor_m": 185.2,
    "safety_floor_m": 300.0,
    "quality_domain_m": 405.0,
    "profile": "corridor_close_start",
    "basis": "Fujii 8L; MASS legal 9L",
    "source_confidence": ["high", "moderate", "moderate"]
  },
  
  "layer_results": {
    "L1_scenario_validity": { "passed": true, "checks": {...} },
    "L2_safety_floor": {
      "passed": true,
      "min_separation_m": 387.2,
      "danger_floor_margin_m": 202.0,
      "safety_floor_margin_m": 87.2,
      "quality": 0.85
    },
    "L3_dynamic_risk": {
      "passed": true,
      "approach": {
        "warning_exposure_s": 45.2,
        "danger_exposure_s": 0.0,
        "risk_recovery_ok": true
      },
      "post_pass": {
        "close_domain_s": 12.3,
        "distance_increasing": true,
        "min_post_pass_separation_m": 420.5
      }
    },
    "L4_colregs_compliance": {
      "passed": true,
      "rule": "Rule14",
      "checks": {
        "turn_starboard": { "value": true, "max_stbd_deg": 42.5 },
        "no_port_turn": { "value": true, "max_port_deg": 3.2 },
        "port_to_port_pass": { "value": true },
        "role_lifecycle": { "value": true, "duty_changes": 0 },
        "past_and_clear": { "value": true, "time_s": 285.3 }
      }
    },
    "L5_route_recovery": {
      "passed": true,
      "final_xte_m": 45.2,
      "final_heading_dev_deg": 2.1,
      "max_route_xte_m": 312.5,
      "recovery_time_s": 85.0
    },
    "L6_seamanship": {
      "passed": true,
      "path_length_ratio": 1.12,
      "integrated_abs_xte_m_s": 45230.0,
      "route_crossing_overshoot_count": 0
    },
    "L7_stability": {
      "passed": true,
      "behavior_toggles": 2,
      "steering_reversals": 2,
      "rot_hold_std_dps": 0.35,
      "conflict_toggles": 2,
      "role_onset_changes": 0
    }
  },
  
  "failed_gates": [],
  "warnings": [
    "Post-pass close domain exposure 12.3s — within limit but notable"
  ],
  
  "trace_artifact_path": "runs/run-20260616-001/trace_current.jsonl",
  "plot_path": "runs/run-20260616-001/trajectory.png"
}
```

### 5.2 Batch Summary

```
==================================================
COLREGs Clean 8-Probe Batch Results
==================================================
Date: 2026-06-16 | FCB LOA: 45.0m | Rate: 10x

[PASS] colreg-rule14-ho          safety=0.85 colregs=0.92 mission=0.78 stability=0.95
[PASS] colreg-rule14-ho-port     safety=0.82 colregs=0.90 mission=0.75 stability=0.93
[PASS] colreg-rule13-ot          safety=0.78 colregs=0.88 mission=0.82 stability=0.91
[PASS] colreg-rule15-cs          safety=0.91 colregs=0.95 mission=0.85 stability=0.97
[PASS] colreg-rule15-cs-2        safety=0.88 colregs=0.93 mission=0.80 stability=0.94
[PASS] colreg-rule15-cs-edge     safety=0.75 colregs=0.85 mission=0.72 stability=0.88
[PASS] colreg-rule15-ot-boundary safety=0.72 colregs=0.82 mission=0.70 stability=0.85
[PASS] colreg-rule17-cr-so       safety=0.68 colregs=0.90 mission=0.65 stability=0.92

OVERALL: 8/8 PASS
Safety: 8/8 | COLREGs: 8/8 | Mission: 8/8 | Stability: 8/8

Warnings:
  - rule15-ot-boundary: target speed 45.7 kn (4.6× OS) — geometry stress test
  - rule17-cr-so: safety_quality 0.68 — close to safety floor (in-extremis expected)
```

---

## 6. Implementation Plan

### Phase 1: Threshold Fix (P0 — 1 day)

1. Update all scenario YAML files:
   - Add `cpa_acceptance.safety_floor_m` field
   - Keep `cpa_acceptance.threshold_m` as danger floor (185.2m)
   - Add `cpa_acceptance.quality_domain_m` field
2. Fix README CPA inconsistencies
3. Update `expected_cpa_floor_m()` in `run_6_scenarios.py` to use safety floor

### Phase 2: Cross-Ahead + Reaction Time (P0 — 2 days)

1. Add `_crossed_ahead_of_target()` to `run_6_scenarios.py`
2. Add `_action_before_tcpa()` reaction time check
3. Wire both into `compute_overall_pass()`

### Phase 3: Approach/Post-Pass Split (P0 — 2 days)

1. Implement `PostPassClassifier` (Spec §8)
2. Split `compute_risk_metrics()` into approach and post-pass phases
3. Add post-pass quality metrics to report

### Phase 4: Stability Threshold Tightening (P1 — 0.5 day)

1. Update `DEFAULT_THRESHOLDS` in `stability_scorer.py`
2. Update test expectations in `test_stability_scorer.py`

### Phase 5: Evaluator Decoupling (P1 — 3 days)

1. Create `TraceEvaluator` class with clean interface
2. Extract evaluation logic from `run_6_scenarios.py`
3. Create `EvaluationReport` dataclass
4. Rename runner to `run_colregs_clean_8probe.py`

### Phase 6: New Scenarios (P2 — 2 days)

1. Add `colreg-rule15-cs-port` to `gen_colreg_tier12.py`
2. Add `colreg-rule17-cr-so-stbd` to `gen_colreg_tier12.py`
3. Generate and validate new scenarios

### Phase 7: No-Action Baseline (P2 — 1 day)

1. Add no-action baseline generation to scenario runner
2. Add baseline validation to Layer 1

---

## 7. Trace Input Contract (Formalized)

```python
@dataclass
class TraceContract:
    """Required trace fields for evaluation."""
    
    # Own ship (from /sil/own_ship_state)
    ownship_fields = [
        "t_s", "lat", "lon", "heading_deg", "sog_kn", "rot_deg_s",
        # Optional: "rudder_angle_deg", "cog_deg"
    ]
    
    # Target (from /sil/target_vessel_state or analytical)
    target_fields = [
        "id", "lat", "lon", "cog", "sog_kn",
    ]
    
    # Geometry (computed by evaluator)
    geometry_fields = [
        "range_m", "rel_bearing_deg", "dcpa_m", "tcpa_s", "closing_speed_mps",
    ]
    
    # Route (computed by evaluator)
    route_fields = [
        "cross_track_error_m", "route_progress",
    ]
    
    # M2/M6 (from /l3/m6/colregs_constraint)
    m6_fields = [
        "rule", "role", "phase", "conflict_detected", "confidence", "rationale",
    ]
    
    # M4 (from /l3/m4/behavior_plan)
    m4_fields = [
        "behavior", "avoidance_active", "heading_min", "heading_max",
    ]
    
    # M5 (from /l3/m5/avoidance_plan)
    m5_fields = [
        "solver_status", "route_points",
    ]
    
    # M7 (from /l3/checker/veto)
    m7_fields = [
        "veto_state", "safety_margin",
    ]
    
    # L4 output (from /l4/guidance_cmd)
    l4_fields = [
        "psi_cmd", "u_cmd", "rot_cmd", "mode",
    ]
```

---

## 8. Summary of Changes from Current System

| Aspect | Current | Proposed | Impact |
|---|---|---|---|
| CPA pass threshold | 185.2m (0.1 NM) for all close-start | 300m safety floor + 185.2m danger floor | Scenarios that barely pass at 200m will now correctly fail |
| Cross-ahead check | Missing | Added for Rule 15 | Will catch "passed in front of target" violations |
| Reaction time | Missing | Added (TCPA at avoidance onset ≥ 120s) | Will catch "acted too late" violations |
| Approach/post-pass split | Not implemented | Full implementation per Spec §8 | Post-pass close domain no longer counts as approach danger |
| Steering reversals | ≤4 (give-way), ≤5 (stand-on) | ≤3 (both) | Tighter fishtail detection |
| ROT hold std | <1.5 °/s | <0.8 °/s | Tighter smoothness requirement |
| Min give-way turn | ≥5° | ≥10° | Ensures "readily apparent" action |
| Integrated XTE | ≤300,000 m·s | ≤200m × duration_s | Normalized by scenario length |
| Verdict structure | Single overall_pass | 4-dimension verdict + quality scores | More diagnostic information |
| Evaluator coupling | Embedded in runner | Independent TraceEvaluator class | Cleaner architecture |

---

## Appendix A: Threshold Provenance Matrix

| Threshold | Value | Source | Reference | Confidence |
|---|---|---|---|---|
| Danger floor | 185.2m (0.1 NM) | Davis (1980) lateral separation ~0.14 NM; immediate danger convention | [R5] | 🟡 Moderate |
| Safety floor (corridor) | 300m (6.7L) | Engineering compromise between 0.1 NM and 9L | — | 🔴 Low (needs project ODD justification) |
| Safety floor (open water) | 500m (11.1L) | Wang (2009) head-on 0.5-0.6 NM; common MASS practice | [R6][R10] | 🟢 High |
| Quality domain | 405m (9L) | IMO maneuverability advance < 4.5L × 2 ships | [R11] | 🟡 Moderate |
| Quality domain (open water) | 926m (0.5 NM) | Goodwin, Davis, Wang consensus; MAXCMAS | [R4][R5][R6][R10] | 🟢 High |
| Head-on sector | ±6° (354°-006°) | Half-point convention; Kiveli v Afina I [2026] EWCA | [R9] | 🟢 High |
| Overtaking boundary | 112.5° from bow | COLREGs Rule 13(b) exact text | [R1] | 🟢 High (law) |
| Substantial action | ≥30° heading change | Maritime practice; Rule 8(b) "readily apparent" | [R7] | 🟢 High |
| Early action | TCPA ≥ 120s at onset | Norwegian AIS: mean 14-40 min; conservative for close-start | [R14] | 🟡 Moderate |
| Past-and-clear | Target abaft + distance increasing | COLREGs Rule 8(d), 13(d); Cockcroft & Lameijer | [R1][R7] | 🟢 High |
| Premature give-way | <10° in hold phase | Rule 17(a)(i) "shall keep course and speed" | [R1] | 🟢 High |
| Path length ratio | ≤1.35 | η = d₁/d₂ efficiency metric | [R16] | 🟡 Moderate |

## Appendix B: Academic Evaluation Frameworks

Three major COLREGs evaluation frameworks from the literature provide theoretical anchors for this design:

### B.1 Woerner et al. (2019) — MIT "Road Test"

**Reference**: Woerner, Benjamin, Novitzky, Leonard. *Quantifying protocol evaluation for autonomous collision avoidance*. Autonomous Robots 43(4):967-991. DOI: [10.1007/s10514-018-9779-y](https://doi.org/10.1007/s10514-018-9779-y)

**Dual-metric architecture** — directly maps to our safety + colregs split:
- **Safety score** = f(CPA range, CPA pose) — scalar 0-100%
- **Protocol compliance** = rule-specific score per Rules 13-17 → 0-100%
- Uses three threshold rings: collision (red), near-miss (yellow), acceptable range (green)
- COLREGs rules converted into mathematical functions using relative bearing and contact angle at CPA

**Adoption**: Our `safety_quality` and `colregs_quality` scores follow this dual-metric approach.

### B.2 Brekke, Johansen, Hagen et al. (2023) — NTNU/FFI

**Reference**: Hagen, Vassbotn, Skogvold, Johansen, Brekke. *Safety and COLREG evaluation for marine collision avoidance algorithms*. Ocean Engineering 288:115991. DOI: [10.1016/j.oceaneng.2023.115991](https://doi.org/10.1016/j.oceaneng.2023.115991)

**Penalty function approach** — parametrized weights for continuous scoring:
- Head-on potential: `K_HO = 40`, steepness `α_x = 1/500, α_y = 1/400`
- Give-way potential: `K_GW = 40`, port-side bias `y_0 = -500 m`
- Course-derivative penalty: `K_χ̇ = 2.5` — penalizes non-smooth maneuvers
- SOG-derivative penalty: `K_U̇ = 0.3` — penalizes rapid speed changes
- COLREGs collision cost: proportional to `1/t^q` where `q=4`, prediction horizon `T=300s`

**2024 extension**: Hagen, Murvold, Johansen, Brekke. *Grounding hazard considerations*. Ocean Engineering 308:118204. Adds ENC-based grounding penalty when COLREGs action is restricted by land.

**Adoption**: Our Layer 6 seamanship evaluation could adopt the course-derivative penalty `K_χ̇` as a continuous smoothness metric, replacing the discrete `steering_reversals` count.

### B.3 Gleeson, Dunbabin, Ford (2024) — QUT

**Reference**: *COLREG Scenario classification and Compliance Evaluation with temporal and multi-vessel awareness*. Ocean Engineering 313(Part 3):119552. DOI: [10.1016/j.oceaneng.2024.119552](https://doi.org/10.1016/j.oceaneng.2024.119552)

**Two contributions**:
1. **TAG-CSC**: Temporally Aware Geometric COLREG Scenario Classification — adds hysteresis + confidence estimate to maintain temporal stability
2. **CCE**: COLREG Compliance Evaluation — alternative penalty functions with vehicle reaction time approach and uncertainty bounds
- First framework to aggregate CCE scores for >2 vessels using collision-risk-based method

**Adoption**: The TAG-CSC hysteresis approach should inform our M6 classification stability requirements. The 2-5° hysteresis at sector boundaries (recommended in the paper) should be adopted in M6 to prevent Rule14↔Rule15 chattering.

### B.4 Stankiewicz & Mullins (2019) — Good Seamanship Quantification

**Reference**: *Quantifying Good Seamanship For Autonomous Surface Vessel Performance Evaluation*. OCEANS 2019. [par.nsf.gov/servlets/purl/10268283](https://par.nsf.gov/servlets/purl/10268283)

**Two dimensions**:
1. **Reduction of overall collision risk** — measured by maximum mutual ship domain violation
2. **Early, appropriate action** — whether the ASV takes timely action before risk becomes critical

**Adoption**: Our Layer 6 should add an "overall risk reduction" metric: `max_domain_violation` across all targets, not just the primary threat.

### B.5 Pedersen et al. (2023) — 55-Scenario Extension

**Reference**: Pedersen, Vasanthan, Karolius et al. *Generating Structured Set of Encounters for Verifying Automated Collision and Grounding Avoidance Systems*. ICMASS 2023. DOI: [10.1088/1742-6596/2618/1/012013](https://doi.org/10.1088/1742-6596/2618/1/012013)

Extends Imazu-22 to **55 scenarios** with systematic rule coverage. Open-source toolbox: [github.com/dnv-opensource/ship-traffic-generator](https://github.com/dnv-opensource/ship-traffic-generator)

**Adoption**: When expanding beyond 8-probe, use Pedersen's systematic generation to ensure complete rule coverage. The 55-scenario set covers Rules 2, 8, 13/16, 13/17, 14, 15/16, 15/17.

### B.6 ClassNK Evaluation Area Diagrams (2025)

**Reference**: Nakamura & Yamada. *Objective evaluation criteria for the safety certification of autonomous navigation system*. ICMASS 2025. DOI: [10.1088/1742-6596/3123/1/012032](https://doi.org/10.1088/1742-6596/3123/1/012032)

Three risk zones:
- **Safety area**: no constraint
- **Caution area**: based on relative distance + bearing change rate
- **Danger area**: imminent collision

If ANS navigates to avoid entering Caution/Danger areas, it qualifies as "reducing risk before risk exists."

**Adoption**: Our three-tier CPA model (danger/safety/quality) maps directly to this three-zone approach.

---

## Appendix C: Additional Missing Metrics from Literature

Based on the comprehensive literature survey, the following well-established metrics should be added in future iterations:

| Metric | Definition | Source | Priority |
|---|---|---|---|
| **Safe speed compliance** (Rule 6) | Ratio of actual speed to COLREGs-appropriate speed given visibility/traffic | COLREGs Rule 6 | P2 |
| **Apparent maneuver magnitude** (Rule 8) | Binary: heading_change ≥ 30° within 2 prediction cycles | Woerner 2019; Rule 8(b) | P1 (already in design) |
| **Heading oscillation frequency** | FFT on heading signal; flag if dominant frequency > 0.02 Hz (period < 50s) | Brekke/Hagen 2023 `K_χ̇` | P2 |
| **Multi-vessel risk accumulation** | `Φ_S(t) = max_i(CRI_i) + Σ(secondary_risk)` | Stankiewicz 2019 | P3 (multi-ship phase) |
| **Speed variation penalty** | `K_U̇ = 0.3` on SOG derivative | Brekke/Hagen 2023; Eriksen 2020 | P2 |
| **Grounding-aware evaluation** | ENC-based grounding penalty when COLREGs action restricted by land | Hagen 2024 | P3 |

---

## Appendix D: Reference List

Same as Review Report Appendix A, plus:

| ID | Source | Type |
|---|---|---|
| [R19] | Woerner et al. (2019) Autonomous Robots 43(4):967-991 | MASS 评估框架 |
| [R20] | Hagen, Brekke et al. (2023) Ocean Engineering 288:115991 | MASS 评估框架 |
| [R21] | Gleeson et al. (2024) Ocean Engineering 313:119552 | MASS 评估框架 |
| [R22] | Stankiewicz & Mullins (2019) OCEANS 2019 | 良好船艺量化 |
| [R23] | Pedersen et al. (2023) ICMASS 2023 | 场景生成 |
| [R24] | Nakamura & Yamada (2025) ICMASS 2025 | 认证评估 |
| [R25] | Hagen et al. (2024) Ocean Engineering 308:118204 | 搁浅感知评估 |
| [R26] | Eriksen (2020) Frontiers in Robotics and AI | Hybrid COLAV |
| [R27] | Papadimitrakis (2021) Sensors 21-06959 | MPC 风险效率 |
