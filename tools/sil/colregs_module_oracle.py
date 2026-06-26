"""Single-module COLREGs oracles (Layer 2 of test system v1).

Each oracle takes compiled geometry/truth + module output trace, returns
a per-check pass/fail with evidence. Pure stdlib, no ROS2/container deps.

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md §5.
"""
from __future__ import annotations
from dataclasses import dataclass, field


@dataclass
class OracleResult:
    """Result of one module oracle evaluation."""
    module: str
    passed: bool
    failed_checks: list[str] = field(default_factory=list)
    evidence: dict = field(default_factory=dict)


def evaluate_m6_oracle(*, compiled: dict, m6_output: dict) -> OracleResult:
    """M6 COLREGsReasoner oracle: rule/role/direction/latch correctness.

    compiled: scenario_compiled rule_classification block
        {compiled_rule, own_role, allowed_actions, classification}
    m6_output: extracted from /l3/m6/colregs_constraint trace
        {rule, role, preferred_direction, flip_count, flip_intervals_s}
    """
    failed: list[str] = []
    evidence: dict = {}

    # Rule classification
    if m6_output.get("rule") != compiled.get("compiled_rule"):
        failed.append("WRONG_RULE_CLASSIFICATION")
        evidence["m6_rule"] = m6_output.get("rule")
        evidence["compiled_rule"] = compiled.get("compiled_rule")

    # Role assignment
    if m6_output.get("role") != compiled.get("own_role"):
        failed.append("WRONG_ROLE_ASSIGNMENT")
        evidence["m6_role"] = m6_output.get("role")
        evidence["compiled_role"] = compiled.get("own_role")

    # Preferred direction must be in allowed_actions
    allowed = compiled.get("allowed_actions", [])
    pref = m6_output.get("preferred_direction")
    if allowed and pref and pref not in allowed:
        failed.append("FORBIDDEN_DIRECTION")
        evidence["m6_direction"] = pref
        evidence["allowed"] = allowed

    # Latch/hysteresis stability
    flip_count = int(m6_output.get("flip_count", 0))
    classification = compiled.get("classification", "interior")
    max_allowed_flip = 2 if classification == "boundary" else 0
    if flip_count > max_allowed_flip:
        failed.append("RULE_INSTABILITY")
        evidence["flip_count"] = flip_count
        evidence["max_allowed"] = max_allowed_flip

    # Flip interval dwell (>=10s between flips)
    intervals = m6_output.get("flip_intervals_s", [])
    HYSTERESIS_DWELL_MIN_S = 10.0
    short_intervals = [i for i in intervals if i < HYSTERESIS_DWELL_MIN_S]
    if short_intervals:
        failed.append("RULE_INSTABILITY")
        evidence["short_flip_intervals_s"] = short_intervals

    return OracleResult(
        module="M6_COLREGsReasoner",
        passed=len(failed) == 0,
        failed_checks=failed,
        evidence=evidence,
    )


def evaluate_m4_oracle(*, m4_events: list[dict], m6_conflict_cleared_t: float | None,
                       timing_tolerance_s: float = 5.0) -> OracleResult:
    """M4 BehaviorArbiter oracle: state machine + release precedence.

    Core check: M4 must not enter RECOVERY before M6 has cleared conflict
    (PREMATURE_RECOVERY_BEFORE_RULE_RELEASE). This is the integration defect
    observed in rule14-ho (M4 779.6s RECOVERY vs M6 804.5s clear).

    m4_events: [{t, behavior}] sorted by t, behavior in
        {TRANSIT, COLREG_AVOID, RECOVERY}
    m6_conflict_cleared_t: sim_t when M6 conflict_detected went false (None if N/A)
    """
    failed: list[str] = []
    evidence: dict = {}

    # Find first RECOVERY entry.
    recovery_t = None
    has_avoidance = any(e["behavior"] == "COLREG_AVOID" for e in m4_events)
    for e in m4_events:
        if e["behavior"] == "RECOVERY":
            recovery_t = float(e["t"])
            break

    # PREMATURE_RECOVERY check: only meaningful if avoidance happened.
    if has_avoidance and recovery_t is not None and m6_conflict_cleared_t is not None:
        gap = m6_conflict_cleared_t - recovery_t
        evidence["recovery_t"] = recovery_t
        evidence["conflict_cleared_t"] = m6_conflict_cleared_t
        evidence["gap_s"] = round(gap, 3)
        # D1.4a: PREMATURE_RECOVERY only flags when the target was still closing
        # at the moment M4 released. If the target was opening (closing_mps<=0),
        # M4's release is physically correct even when M6 conflict_detected lags
        # due to release-latch hysteresis.
        closing_at_release = None
        for e in m4_events:
            if abs(float(e["t"]) - recovery_t) < 0.5:
                closing_at_release = e.get("closing_mps")
                break
        evidence["closing_mps_at_release"] = closing_at_release
        target_still_closing = closing_at_release is None or closing_at_release > 0.0
        # M6 must clear conflict BEFORE (or within tolerance of) M4 recovery,
        # AND the target must still be closing (else release is correct).
        if (target_still_closing
                and recovery_t < m6_conflict_cleared_t - timing_tolerance_s):
            failed.append("PREMATURE_RECOVERY_BEFORE_RULE_RELEASE")

    return OracleResult(
        module="M4_BehaviorArbiter",
        passed=len(failed) == 0,
        failed_checks=failed,
        evidence=evidence,
    )


def evaluate_m2_oracle(*, truth: dict, estimated: dict,
                       bearing_tol_deg: float = 2.0,
                       cpa_tol_pct: float = 10.0,
                       tcpa_tol_pct: float = 5.0) -> OracleResult:
    """M2 WorldModel oracle: bearing/CPA/TCPA estimate consistency."""
    failed: list[str] = []
    evidence: dict = {}
    t_brg = float(truth["bearing_deg"]); e_brg = float(estimated["bearing_deg"])
    if abs(e_brg - t_brg) > bearing_tol_deg:
        failed.append("MEASUREMENT_INCONSISTENT")
        evidence["bearing_err_deg"] = round(abs(e_brg - t_brg), 3)
    t_cpa = float(truth["cpa_m"]); e_cpa = float(estimated["cpa_m"])
    if t_cpa > 0 and abs(e_cpa - t_cpa) / t_cpa * 100.0 > cpa_tol_pct:
        failed.append("MEASUREMENT_INCONSISTENT")
        evidence["cpa_err_pct"] = round(abs(e_cpa - t_cpa) / t_cpa * 100.0, 3)
    t_tcpa = float(truth["tcpa_s"]); e_tcpa = float(estimated["tcpa_s"])
    if t_tcpa > 0 and abs(e_tcpa - t_tcpa) / t_tcpa * 100.0 > tcpa_tol_pct:
        failed.append("MEASUREMENT_INCONSISTENT")
        evidence["tcpa_err_pct"] = round(abs(e_tcpa - t_tcpa) / t_tcpa * 100.0, 3)
    return OracleResult("M2_WorldModel", len(failed) == 0, failed, evidence)


def evaluate_m5_oracle(*, plan_output: dict) -> OracleResult:
    """M5 TacticalPlanner oracle: feasible plan + no oscillation (design §5.2.4)."""
    failed: list[str] = []
    evidence: dict = {}
    status = plan_output.get("solver_status", "EMPTY")
    if status != "VALID":
        failed.append("NO_FEASIBLE_PLAN")
        evidence["solver_status"] = status
    n_wp = int(plan_output.get("n_waypoints", 0))
    if status == "VALID" and n_wp == 0:
        failed.append("NO_FEASIBLE_PLAN")
        evidence["n_waypoints"] = n_wp
    osc = int(plan_output.get("oscillation_count", 0))
    if osc > 1:
        failed.append("PLAN_INSTABILITY")
        evidence["oscillation_count"] = osc
    return OracleResult("M5_TacticalPlanner", len(failed) == 0, failed, evidence)


def evaluate_m7_oracle(*, unsafe_trajectory_vetoed: bool, safe_trajectory_vetoed: bool,
                       unsafe_trajectory_present: bool = False,
                       ) -> OracleResult:
    """M7 SafetySupervisor oracle: correct veto behavior.

    MISSED_VETO only fires when an unsafe trajectory was actually present
    (e.g. CPA-floor breach) and M7 failed to veto it. A clean run with no
    unsafe trajectory and no veto is a correct PASS, not a missed veto.
    """
    failed: list[str] = []
    evidence: dict = {}
    if unsafe_trajectory_present and not unsafe_trajectory_vetoed:
        failed.append("MISSED_VETO")
        evidence["unsafe_trajectory_present"] = True
    if safe_trajectory_vetoed:
        failed.append("FALSE_VETO")
    return OracleResult("M7_SafetySupervisor", len(failed) == 0, failed, evidence)


def evaluate_l4_oracle(*, first_command_t: float, first_realized_t: float,
                       realized_heading_change_deg: float,
                       act_delay_max_s: float = 20.0,
                       min_heading_change_deg: float = 10.0) -> OracleResult:
    """L4 GuidanceAdapter oracle: actuation realized + delay (design §5.2.6).

    act_delay_max_s defaults to 20s for surface vessels: the delay spans the
    M5 solve + L4 handoff + rudder/hydrodynamic response, which for a
    large-inertia ship is 15-20s (not the 5s automotive ADAS value in the
    design draft). The threshold is a tuning parameter — flag ACTUATION_DELAY
    only when the maneuver is materially late, not when normal ship dynamics
    apply.
    """
    failed: list[str] = []
    evidence: dict = {}
    delay = first_realized_t - first_command_t
    evidence["actuation_delay_s"] = round(delay, 3)
    if delay > act_delay_max_s:
        failed.append("ACTUATION_DELAY_EXCEEDED")
    evidence["realized_heading_change_deg"] = round(realized_heading_change_deg, 3)
    if abs(realized_heading_change_deg) < min_heading_change_deg:
        failed.append("INSUFFICIENT_ACTION")
    return OracleResult("L4_GuidanceAdapter", len(failed) == 0, failed, evidence)
