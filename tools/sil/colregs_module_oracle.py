"""Single-module COLREGs oracles (Layer 2 of test system v1).

Each oracle takes compiled geometry/truth + module output trace, returns
a per-check pass/fail with evidence. Pure stdlib, no ROS2/container deps.

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md §5.
"""
from __future__ import annotations
from dataclasses import dataclass, field
import math


@dataclass
class OracleResult:
    """Result of one module oracle evaluation."""
    module: str
    passed: bool
    failed_checks: list[str] = field(default_factory=list)
    evidence: dict = field(default_factory=dict)

    @property
    def failures(self) -> tuple[str, ...]:
        """Tuple view of failed_checks (Task 11 report-contract alias).

        The stage-separated evidence tests assert ``result.failures == (...)``
        for single-check attribution; ``failed_checks`` remains the canonical
        list field consumed by the offline runner and JSON report.
        """
        return tuple(self.failed_checks)


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
    stand_on_in_extremis_action = (
        compiled.get("own_role") == "STAND_ON"
        and bool(m6_output.get("stand_on_in_extremis_action"))
        and pref in ("STARBOARD_TURN", "DECELERATE")
    )
    if allowed and pref and pref not in allowed and not stand_on_in_extremis_action:
        failed.append("FORBIDDEN_DIRECTION")
        evidence["m6_direction"] = pref
        evidence["allowed"] = allowed
    if stand_on_in_extremis_action:
        evidence["stand_on_in_extremis_action"] = True

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

    finally_resolved_count = int(m6_output.get("finally_resolved_count", 0))
    release_sample_count = int(m6_output.get("release_sample_count", 0))
    past_clear_sample_count = int(m6_output.get("past_clear_sample_count", 0))
    if (
        finally_resolved_count > 0
        and (release_sample_count == 0 or past_clear_sample_count == 0)
    ):
        failed.append("LIFECYCLE_RELEASE_MISSING")
        evidence["finally_resolved_count"] = finally_resolved_count
        evidence["release_sample_count"] = release_sample_count
        evidence["past_clear_sample_count"] = past_clear_sample_count

    post_release_conflict_count = int(m6_output.get("post_release_conflict_count", 0))
    if past_clear_sample_count > 0 and post_release_conflict_count > 0:
        failed.append("REARM_AFTER_RELEASE")
        evidence["post_release_conflict_count"] = post_release_conflict_count

    target_id_width_alias_count = int(m6_output.get("target_id_width_alias_count", 0))
    target_id_mismatch_count = int(m6_output.get("target_id_mismatch_count", 0))
    if target_id_width_alias_count > 0:
        failed.append("TARGET_ID_WIDTH_ALIAS")
        evidence["target_id_width_alias_count"] = target_id_width_alias_count
        evidence["m2_target_ids"] = m6_output.get("m2_target_ids", [])
        evidence["m6_target_ids"] = m6_output.get("m6_target_ids", [])
        if m6_output.get("target_id_width_aliases"):
            evidence["target_id_width_aliases"] = m6_output["target_id_width_aliases"]
    elif target_id_mismatch_count > 0:
        failed.append("TARGET_IDENTITY_MISMATCH")
        evidence["target_id_mismatch_count"] = target_id_mismatch_count
        evidence["m2_target_ids"] = m6_output.get("m2_target_ids", [])
        evidence["m6_target_ids"] = m6_output.get("m6_target_ids", [])

    onset_tcpa_raw = m6_output.get("onset_tcpa_s")
    if onset_tcpa_raw is not None:
        try:
            onset_tcpa_s = float(onset_tcpa_raw)
        except (TypeError, ValueError):
            onset_tcpa_s = math.nan
        t_plan_s = float(m6_output.get("t_plan_s", 720.0))
        t_emergency_s = float(m6_output.get("t_emergency_s", 60.0))
        if (
            math.isfinite(onset_tcpa_s)
            and (onset_tcpa_s > t_plan_s or onset_tcpa_s <= t_emergency_s)
        ):
            failed.append("ONSET_TCPA_OUT_OF_PLAN_WINDOW")
            evidence["onset_tcpa_s"] = round(onset_tcpa_s, 3)
            evidence["t_plan_s"] = round(t_plan_s, 3)
            evidence["t_emergency_s"] = round(t_emergency_s, 3)

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

    post_recovery_colreg_avoid_count = 0
    if recovery_t is not None:
        prev_behavior = None
        for e in m4_events:
            if float(e["t"]) <= recovery_t:
                prev_behavior = e["behavior"]
                continue
            behavior = e["behavior"]
            if behavior == "COLREG_AVOID" and prev_behavior != "COLREG_AVOID":
                post_recovery_colreg_avoid_count += 1
            prev_behavior = behavior
    if post_recovery_colreg_avoid_count > 0:
        failed.append("RECOVERY_CHATTER_AFTER_RELEASE")
        evidence["post_recovery_colreg_avoid_count"] = post_recovery_colreg_avoid_count

    return OracleResult(
        module="M4_BehaviorArbiter",
        passed=len(failed) == 0,
        failed_checks=failed,
        evidence=evidence,
    )


def evaluate_m2_oracle(*, truth: dict, estimated: dict,
                       bearing_tol_deg: float = 2.0,
                       cpa_tol_pct: float = 10.0,
                       tcpa_tol_pct: float = 5.0,
                       ownship_position_tol_m: float = 500.0,
                       cpa_near_zero_floor_m: float = 50.0) -> OracleResult:
    """M2 WorldModel oracle: bearing/CPA/TCPA estimate consistency.

    ``cpa_near_zero_floor_m``: when the truth CPA is below this threshold
    (near-collision geometry), the CPA comparison uses an absolute tolerance
    (the floor itself) instead of the percentage tolerance. A head-on encounter
    has truth CPA ≈ 0; a 1m absolute difference should not be a 50% error when
    both values mean 'collision course'. Above the floor the percentage check
    applies normally.
    """
    failed: list[str] = []
    evidence: dict = {}

    # Fail closed when observed M2 trace fields are missing — no fallback to truth.
    missing_fields = estimated.get("missing_fields", [])
    if missing_fields:
        failed.append("M2_TRACE_FIELDS_MISSING")
        evidence["missing_fields"] = missing_fields

    # Numerical comparisons — skip when estimate values are None (caught above).
    e_brg = estimated.get("bearing_deg")
    if e_brg is not None:
        t_brg = float(truth["bearing_deg"])
        e_brg = float(e_brg)
        if abs(e_brg - t_brg) > bearing_tol_deg:
            failed.append("MEASUREMENT_INCONSISTENT")
            evidence["bearing_err_deg"] = round(abs(e_brg - t_brg), 3)
    e_cpa = estimated.get("cpa_m")
    if e_cpa is not None:
        t_cpa = float(truth["cpa_m"])
        e_cpa = float(e_cpa)
        abs_err = abs(e_cpa - t_cpa)
        # Near-zero truth CPA (near-collision geometry): use absolute tolerance
        # instead of percentage. A head-on truth CPA ≈ 0 makes any nonzero
        # estimate yield a huge percentage error even when both mean 'collision
        # course'. Above the floor, the percentage check applies normally.
        if 0 < t_cpa < cpa_near_zero_floor_m:
            if abs_err > cpa_near_zero_floor_m:
                failed.append("MEASUREMENT_INCONSISTENT")
                evidence["cpa_err_m"] = round(abs_err, 3)
        elif t_cpa >= cpa_near_zero_floor_m:
            if abs_err / t_cpa * 100.0 > cpa_tol_pct:
                failed.append("MEASUREMENT_INCONSISTENT")
                evidence["cpa_err_pct"] = round(abs_err / t_cpa * 100.0, 3)
    e_tcpa = estimated.get("tcpa_s")
    if e_tcpa is not None:
        t_tcpa = float(truth["tcpa_s"])
        e_tcpa = float(e_tcpa)
        if t_tcpa > 0 and abs(e_tcpa - t_tcpa) / t_tcpa * 100.0 > tcpa_tol_pct:
            failed.append("MEASUREMENT_INCONSISTENT")
            evidence["tcpa_err_pct"] = round(abs(e_tcpa - t_tcpa) / t_tcpa * 100.0, 3)
    ownship_err = estimated.get("ownship_max_position_err_m")
    if ownship_err is not None and float(ownship_err) > ownship_position_tol_m:
        failed.append("OWNSHIP_STATE_INCONSISTENT")
        evidence["ownship_max_position_err_m"] = round(float(ownship_err), 3)
        if estimated.get("ownship_position_err_t_s") is not None:
            evidence["ownship_position_err_t_s"] = round(
                float(estimated["ownship_position_err_t_s"]), 3)
    return OracleResult("M2_WorldModel", len(failed) == 0, failed, evidence)


# M1 ODD envelope state enum (ODDState.msg): IN=0, EDGE=1, OUT=2, MRC_PREP=3,
# MRC_ACTIVE=4. M1 is the sole safety-context authority — its envelope state is
# the authoritative ODD verdict every downstream module must respect.
M1_ENVELOPE_IN = 0
M1_ENVELOPE_EDGE = 1
M1_ENVELOPE_OUT = 2
M1_ENVELOPE_MRC_PREP = 3
M1_ENVELOPE_MRC_ACTIVE = 4
M1_ENVELOPE_STATES = {
    M1_ENVELOPE_IN, M1_ENVELOPE_EDGE, M1_ENVELOPE_OUT,
    M1_ENVELOPE_MRC_PREP, M1_ENVELOPE_MRC_ACTIVE,
}


def evaluate_m1_oracle(*, m1_output: dict) -> OracleResult:
    """M1 ODD/Envelope Manager oracle: ODD-state authority + envelope contract.

    M1 is the ONLY safety-context authority. This oracle verifies the published
    envelope_state trace is internally consistent and does not violate the
    monotonic-recovery / override-priority / stale-degradation contracts, without
    substituting any module-local safety truth.

    m1_output keys (all derived from /l3/odd_state trace, never invented):
      envelope_seq:        list[int] of envelope_state in ascending event time
      conformance_score_seq: list[float] aligned to envelope_seq
      stale_input_count:   int, samples where M2 input was marked stale
      score_degraded_on_stale: bool, score fell when input went stale
      recovered_to_edge:   bool, OUT->EDGE transition observed after score rose
      mrc_override_active: bool, MRC_PREP/MRC_ACTIVE ever asserted
      last_transition_at_updated: bool, transition timestamp updated on change
    """
    failed: list[str] = []
    evidence: dict = {}

    envelope_seq = list(m1_output.get("envelope_seq", []))
    if not envelope_seq:
        failed.append("M1_ENVELOPE_TRACE_EMPTY")
        return OracleResult("M1_ODDEnvelopeManager", False, failed, evidence)

    # Every emitted state must be a valid envelope enum.
    invalid_states = [s for s in envelope_seq if s not in M1_ENVELOPE_STATES]
    if invalid_states:
        failed.append("M1_ENVELOPE_INVALID_STATE")
        evidence["invalid_states"] = invalid_states[:8]

    # Stale-input degradation contract: M2 input staleness MUST degrade the
    # conformance score (design §3.6). It is a defect if staleness was observed
    # but the score did not drop.
    stale_input_count = int(m1_output.get("stale_input_count", 0))
    score_degraded_on_stale = bool(m1_output.get("score_degraded_on_stale", False))
    evidence["stale_input_count"] = stale_input_count
    if stale_input_count > 0 and not score_degraded_on_stale:
        failed.append("M1_STALE_INPUT_NOT_DEGRADED")
        evidence["score_degraded_on_stale"] = False

    # Recovery monotonicity: an OUT state must recover UP to EDGE (not jump
    # straight to IN) when the score improves, and never oscillate OUT->IN->OUT
    # without hysteresis. If recovery_to_edge is explicitly reported False while
    # an OUT->IN transition exists, that is a boundary-hysteresis violation.
    if m1_output.get("recovered_to_edge") is False:
        # Caller asserts a recovery occurred but skipped EDGE -> hysteresis gap.
        failed.append("M1_RECOVERY_SKIPPED_EDGE")
        evidence["recovered_to_edge"] = False

    # Override priority: if MRC_PREP/MRC_ACTIVE was ever active, it must be the
    # terminal/maximum-priority state in the observed window (no return to a
    # lower state without a recovery edge).
    mrc_states = (M1_ENVELOPE_MRC_PREP, M1_ENVELOPE_MRC_ACTIVE)
    if m1_output.get("mrc_override_active") and envelope_seq:
        last_mrc_index = max(
            (i for i, s in enumerate(envelope_seq) if s in mrc_states),
            default=None)
        if last_mrc_index is not None:
            post_mrc = envelope_seq[last_mrc_index + 1:]
            non_recovery_after_mrc = [
                s for s in post_mrc
                if s not in (M1_ENVELOPE_IN, M1_ENVELOPE_EDGE)
            ]
            if non_recovery_after_mrc:
                failed.append("M1_MRC_OVERRIDE_REGRESSION")
                evidence["post_mrc_states"] = non_recovery_after_mrc[:8]

    return OracleResult("M1_ODDEnvelopeManager", len(failed) == 0, failed, evidence)


# MRM type enum (MRMCommand.msg): NONE=0, SAFE_SPEED_HOLD=1, STOP=2,
# EMERGENCY_TURN=3, ANCHOR=4. Bare canonical MRM id text ↔ numeric type.
MRM_TYPE_NONE = 0
MRM_TYPE_SAFE_SPEED_HOLD = 1
MRM_TYPE_STOP = 2
MRM_TYPE_EMERGENCY_TURN = 3
MRM_TYPE_ANCHOR = 4
MRM_ID_TO_TYPE: dict[str, int] = {
    "MRM-01": MRM_TYPE_SAFE_SPEED_HOLD,
    "MRM-01-SAFE-SPEED-HOLD": MRM_TYPE_SAFE_SPEED_HOLD,
    "MRM-02": MRM_TYPE_STOP,
    "MRM-02-STOP": MRM_TYPE_STOP,
    "MRM-03": MRM_TYPE_EMERGENCY_TURN,
    "MRM-03-EMERGENCY-TURN": MRM_TYPE_EMERGENCY_TURN,
    "MRM-04": MRM_TYPE_ANCHOR,
    "MRM-04-ANCHOR": MRM_TYPE_ANCHOR,
}


def _canonical_mrm_id(text: str) -> str:
    """Normalize an MRM id string to its bare canonical form ('MRM-0N').

    Accepts bare ('MRM-02'), suffixed ('MRM-02-STOP'), and case/separator
    variants. Returns '' for anything that is not a recognized canonical MRM.
    Per Task 10 parse_mrm_recommendation: legacy conflicts and unknown ids are
    never silently mapped to SafeSpeedHold.
    """
    if not text:
        return ""
    norm = str(text).strip().upper().replace("_", "-")
    for key in MRM_ID_TO_TYPE:
        if norm == key or norm.startswith(key + "-"):
            return key.split("-")[0] + "-" + key.split("-")[1]
    # Bare 'MRM0N' without dash.
    compact = norm.replace(" ", "")
    for n in range(1, 5):
        token = f"MRM-0{n}"
        if compact == token.replace("-", "") or compact.startswith(token.replace("-", "")):
            return token
    return ""


def evaluate_m1_mrm_authority_oracle(*, m1_mrm_output: dict) -> OracleResult:
    """M1 MRM-authority oracle (Task 11 stage-separated evidence).

    M1 (ODD/Envelope Manager) is the SOLE executable-MRM publisher. This
    oracle verifies the M1 MRM *authority* role: that an M7 MRC request is
    answered by an M1 command, that the command is ODD-feasible, that command
    identity/generation is stable (no churn), that the published MRM taxonomy
    matches the canonical enum, and that the publisher is M1 — without
    substituting any module-local safety truth and without conflating this
    with the M7 checker or GNC execution roles.

    m1_mrm_output keys (derived from /l3/m1/mrm_command + /l3/m7/safety_alert
    trace; never invented):
      mrc_requested:       bool, M7 issued an MRC-required alert (a real MRM
                           recommendation was present).
      command_published:   bool, M1 published at least one MRMCommand in the
                           window following the request.
      non_m1_publisher:    bool, the command carried a publisher/source other
                           than M1 (M7-authored executable command is RED).
      command_not_odd_feasible: bool, M1 selected an MRM it could not execute
                           in the current ODD (e.g. ANCHOR without anchorage).
      command_id_churn:    bool, command_id changed without a new generation
                           bump (identity instability).
      taxonomy_mismatch:   bool, mrm_id text does not map to the published
                           mrm_type (canonical taxonomy violation).

    Healthy runs with no MRC-required alert do not require a command (PASS).
    """
    failed: list[str] = []
    evidence: dict = {}
    mrc_requested = bool(m1_mrm_output.get("mrc_requested", False))
    command_published = bool(m1_mrm_output.get("command_published", False))
    evidence["mrc_requested"] = mrc_requested
    evidence["command_published"] = command_published
    if mrc_requested and not command_published:
        failed.append("MRC_REQUEST_WITHOUT_M1_COMMAND")
    # NON_M1_MRM_PUBLISHER is provenance-based (topic of the executable command
    # row), so it is independent of whether an M1-topic command was also
    # published: a foreign-authored executable command is RED even when M1 never
    # published at all. This must NOT be gated on ``command_published``, or the
    # violation is unobservable when the foreign command is the only one.
    if bool(m1_mrm_output.get("non_m1_publisher", False)):
        failed.append("NON_M1_MRM_PUBLISHER")
        evidence["non_m1_publisher"] = True
    if command_published and bool(m1_mrm_output.get("command_not_odd_feasible", False)):
        failed.append("M1_COMMAND_NOT_ODD_FEASIBLE")
        evidence["command_not_odd_feasible"] = True
    if command_published and bool(m1_mrm_output.get("command_id_churn", False)):
        failed.append("M1_COMMAND_ID_CHURN")
        evidence["command_id_churn"] = True
    if command_published and bool(m1_mrm_output.get("taxonomy_mismatch", False)):
        failed.append("MRM_TAXONOMY_MISMATCH")
        evidence["taxonomy_mismatch"] = True
    return OracleResult(
        "M1_ODDEnvelopeManager", len(failed) == 0, failed, evidence)


# M3 Mission FSM state enum (MissionGoal.msg):
#   INIT=0, IDLE=1, TASK_VALIDATION=2, AWAITING_ROUTE=3, ACTIVE=4, REPLAN_WAIT=5.
M3_FSM_INIT = 0
M3_FSM_IDLE = 1
M3_FSM_TASK_VALIDATION = 2
M3_FSM_AWAITING_ROUTE = 3
M3_FSM_ACTIVE = 4
M3_FSM_REPLAN_WAIT = 5
M3_FSM_STATES = {
    M3_FSM_INIT, M3_FSM_IDLE, M3_FSM_TASK_VALIDATION,
    M3_FSM_AWAITING_ROUTE, M3_FSM_ACTIVE, M3_FSM_REPLAN_WAIT,
}
# Task validity sub-state enum: PENDING=0, VALID=1, INVALID=2, REPLANNING=3.
M3_TASK_PENDING = 0
M3_TASK_VALID = 1
M3_TASK_INVALID = 2
M3_TASK_REPLANNING = 3
M3_TASK_STATES = {M3_TASK_PENDING, M3_TASK_VALID, M3_TASK_INVALID, M3_TASK_REPLANNING}

# Valid M3 FSM transitions (mission lifecycle). M3 is local mission tracking +
# replanning trigger, NOT global voyage planning, so transitions must be in this
# set; an out-of-set jump is a lifecycle instability. (MRC_TRANSIT is an M3
# *replan trigger*, not an FSM state — it routes back to IDLE.)
_M3_VALID_TRANSITIONS: set[tuple[int, int]] = {
    (M3_FSM_INIT, M3_FSM_IDLE),
    (M3_FSM_IDLE, M3_FSM_TASK_VALIDATION),
    (M3_FSM_TASK_VALIDATION, M3_FSM_AWAITING_ROUTE),
    (M3_FSM_TASK_VALIDATION, M3_FSM_IDLE),          # task rejected -> idle
    (M3_FSM_AWAITING_ROUTE, M3_FSM_ACTIVE),
    (M3_FSM_AWAITING_ROUTE, M3_FSM_REPLAN_WAIT),    # route never arrives
    (M3_FSM_ACTIVE, M3_FSM_REPLAN_WAIT),
    (M3_FSM_ACTIVE, M3_FSM_IDLE),                   # mission complete
    (M3_FSM_REPLAN_WAIT, M3_FSM_ACTIVE),
    (M3_FSM_REPLAN_WAIT, M3_FSM_IDLE),              # replan failed
}
# Self-transitions are always allowed (no change is a valid re-emission).
for _s in M3_FSM_STATES:
    _M3_VALID_TRANSITIONS.add((_s, _s))
del _s


def evaluate_m3_oracle(*, m3_output: dict) -> OracleResult:
    """M3 Mission Manager oracle: lifecycle activation + replanning contract.

    M3 is local mission tracking and a replanning trigger, NOT global voyage
    planning. This oracle verifies the published FSM state sequence is a valid
    lifecycle walk (no illegal jumps), that duplicate-planned-route handling and
    mission reset are honored, and that a reset returns the FSM to a known
    state. It does NOT judge route geometry (that is M2/M5 authority).

    m3_output keys (all derived from /l3/mission_goal trace, never invented):
      fsm_seq:               list[int] of fsm_state in ascending event time
      task_validity_seq:     list[int] aligned to fsm_seq
      duplicate_route_rejected: bool, a duplicate planned route was rejected
      reset_returned_to_idle:   bool, a reset returned the FSM to IDLE
    """
    failed: list[str] = []
    evidence: dict = {}

    fsm_seq = list(m3_output.get("fsm_seq", []))
    if not fsm_seq:
        failed.append("M3_FSM_TRACE_EMPTY")
        return OracleResult("M3_MissionManager", False, failed, evidence)

    invalid_states = [s for s in fsm_seq if s not in M3_FSM_STATES]
    if invalid_states:
        failed.append("M3_FSM_INVALID_STATE")
        evidence["invalid_fsm_states"] = invalid_states[:8]

    # Lifecycle walk: every adjacent pair must be a valid transition.
    illegal_jumps: list[list[int]] = []
    for prev, curr in zip(fsm_seq, fsm_seq[1:]):
        if (prev, curr) not in _M3_VALID_TRANSITIONS:
            illegal_jumps.append([prev, curr])
    if illegal_jumps:
        failed.append("M3_FSM_ILLEGAL_TRANSITION")
        evidence["illegal_jumps"] = illegal_jumps[:8]

    # Task validity sub-state sanity.
    task_seq = list(m3_output.get("task_validity_seq", []))
    invalid_task = [s for s in task_seq if s not in M3_TASK_STATES]
    if invalid_task:
        failed.append("M3_TASK_VALIDITY_INVALID")
        evidence["invalid_task_validity"] = invalid_task[:8]

    # Duplicate planned route must be rejected (M3 owns de-duplication of the
    # planned-route stream; a second identical route must not re-trigger
    # AWAITING_ROUTE->ACTIVE churn).
    if m3_output.get("duplicate_route_rejected") is False:
        failed.append("M3_DUPLICATE_ROUTE_NOT_REJECTED")
        evidence["duplicate_route_rejected"] = False

    # Mission reset must return to IDLE (or INIT) — a reset that leaves the FSM
    # in ACTIVE/REPLAN_WAIT is a lifecycle-corruption defect.
    if m3_output.get("reset_returned_to_idle") is False:
        failed.append("M3_RESET_DID_NOT_RETURN_TO_IDLE")
        evidence["reset_returned_to_idle"] = False

    return OracleResult("M3_MissionManager", len(failed) == 0, failed, evidence)


def evaluate_m5_oracle(*, plan_output: dict, plan_required: bool = True) -> OracleResult:
    """M5 TacticalPlanner oracle: feasible plan + recovery-return contract."""
    failed: list[str] = []
    evidence: dict = {"plan_required": plan_required}
    status = plan_output.get("solver_status", "EMPTY")
    if plan_required and status != "VALID":
        failed.append("NO_FEASIBLE_PLAN")
        evidence["solver_status"] = status
    n_wp = int(plan_output.get("n_waypoints", 0))
    if plan_required and status == "VALID" and n_wp == 0:
        failed.append("NO_FEASIBLE_PLAN")
        evidence["n_waypoints"] = n_wp
    osc = int(plan_output.get("oscillation_count", 0))
    if osc > 1:
        failed.append("PLAN_INSTABILITY")
        evidence["oscillation_count"] = osc
    m4_recovery_seen = bool(plan_output.get("m4_recovery_seen", False))
    corridor_in_recovery_count = int(plan_output.get("corridor_in_recovery_count", 0))
    recovery_rejected_count = int(plan_output.get("recovery_rejected_count", 0))
    recovery_publish_count = int(plan_output.get("recovery_publish_count", 0))
    if (
        plan_required
        and m4_recovery_seen
        and (corridor_in_recovery_count > 0 or recovery_rejected_count > 0)
        and recovery_publish_count == 0
    ):
        failed.append("RECOVERY_RETURN_ROUTE_MISSING")
        evidence["corridor_in_recovery_count"] = corridor_in_recovery_count
        evidence["recovery_rejected_count"] = recovery_rejected_count
        evidence["recovery_publish_count"] = recovery_publish_count
        evidence["gnc_accepted_recovery_count"] = int(
            plan_output.get("gnc_accepted_recovery_count", 0))
    bcmpc_follow_count = int(plan_output.get("bcmpc_follow_count", 0))
    reactive_override_count = int(plan_output.get("reactive_override_after_bcmpc_count", 0))
    if bcmpc_follow_count > 0 and reactive_override_count == 0:
        failed.append("BCMPC_FOLLOW_WITHOUT_REACTIVE_OVERRIDE")
        evidence["bcmpc_follow_count"] = bcmpc_follow_count
        evidence["reactive_override_after_bcmpc_count"] = reactive_override_count
    return OracleResult("M5_TacticalPlanner", len(failed) == 0, failed, evidence)


def evaluate_m7_oracle(*, m7_output: dict) -> OracleResult:
    """M7 SafetySupervisor checker oracle (Task 11 stage-separated evidence).

    M7 is the checker layer: it surfaces hazards via SafetyAlert
    recommendations and vetoes unsafe commands. It NEVER publishes an
    executable MRM command — that is M1's authority. This oracle verifies the
    M7 checker *role* independently of the M1 authority and GNC execution
    roles, so attribution never conflates them.

    m7_output keys (all derived from /l3/m7/* and /l3/checker/veto trace, never
    invented):
      command_without_recommendation: bool, an executable M1 MRM command was
          published without a preceding/overlapping M7 SafetyAlert
          recommendation (the checker failed to flag the hazard it authorized a
          response to).
      safe_command_wrongly_vetoed: bool, M7 vetoed a command that was itself
          safe (false veto).
      m2_m7_no_action_cpa_inconsistent: bool, M2 and M7 disagree on the
          no-action CPA threshold for the same target.
      physical_separation_breach_unhandled: bool, a CPA-floor / physical
          separation breach occurred and M7 produced neither alert nor veto.
      hard_constraint_cadence_missing: bool, M7 hard-constraint rows stopped
          arriving at the expected cadence during an active hazard.

    A healthy run with no critical/MRC-required alert is a correct PASS — M7 is
    not required to manufacture a recommendation when nothing is wrong.
    """
    failed: list[str] = []
    evidence: dict = {}
    if bool(m7_output.get("command_without_recommendation", False)):
        failed.append("UNSAFE_COMMAND_WITHOUT_M7_RECOMMENDATION")
        evidence["command_without_recommendation"] = True
    if bool(m7_output.get("safe_command_wrongly_vetoed", False)):
        failed.append("SAFE_COMMAND_WRONGLY_VETOED")
        evidence["safe_command_wrongly_vetoed"] = True
    if bool(m7_output.get("m2_m7_no_action_cpa_inconsistent", False)):
        failed.append("M2_M7_NO_ACTION_CPA_INCONSISTENT")
        evidence["m2_m7_no_action_cpa_inconsistent"] = True
    if bool(m7_output.get("physical_separation_breach_unhandled", False)):
        failed.append("PHYSICAL_SEPARATION_BREACH_UNHANDLED")
        evidence["physical_separation_breach_unhandled"] = True
    if bool(m7_output.get("hard_constraint_cadence_missing", False)):
        failed.append("HARD_CONSTRAINT_CADENCE_MISSING")
        evidence["hard_constraint_cadence_missing"] = True
    return OracleResult("M7_SafetySupervisor", len(failed) == 0, failed, evidence)


def evaluate_l4_oracle(*, first_command_t: float, first_realized_t: float,
                       realized_heading_change_deg: float,
                       route_accepted: bool | None = None,
                       first_accepted_t: float | None = None,
                       route_identity_proven: bool | None = None,
                       handoff_missing_proofs: list[str] | None = None,
                       action_required: bool = True,
                       act_delay_max_s: float = 20.0,
                       handoff_delay_max_s: float = 10.0,
                       min_heading_change_deg: float = 10.0) -> OracleResult:
    """L4 GuidanceAdapter oracle: actuation realized + delay (design §5.2.6).

    GNC executes waypoint routes, not explicit COLREG heading intents. When a
    GNC execution-status trace is available, the functional L4 contract is:
    M5 avoidance waypoints are accepted promptly and the ship later realizes a
    material maneuver. Slow heading growth is plant/path geometry, not a L4
    handoff fault. Legacy traces without GNC status fall back to the historical
    first-command to first-realized heading-delay proxy.
    """
    failed: list[str] = []
    evidence: dict = {"action_required": action_required}
    if not action_required:
        evidence["realized_heading_change_deg"] = round(realized_heading_change_deg, 3)
        if route_accepted is not None:
            evidence["route_accepted"] = route_accepted
        return OracleResult("L4_GuidanceAdapter", True, failed, evidence)
    if route_accepted is not None:
        evidence["route_accepted"] = route_accepted
        if route_identity_proven is not None:
            evidence["route_identity_proven"] = route_identity_proven
        if route_identity_proven is False:
            failed.append("ROUTE_HANDOFF_NOT_PROVEN")
            evidence["handoff_missing_proofs"] = list(handoff_missing_proofs or [])
        elif not route_accepted:
            failed.append("ROUTE_NOT_ACCEPTED")
        elif first_accepted_t is not None:
            handoff_delay = first_accepted_t - first_command_t
            evidence["handoff_delay_s"] = round(handoff_delay, 3)
            if handoff_delay > handoff_delay_max_s:
                failed.append("ROUTE_HANDOFF_DELAY_EXCEEDED")
            evidence["plant_response_delay_s"] = round(first_realized_t - first_accepted_t, 3)
    else:
        delay = first_realized_t - first_command_t
        evidence["actuation_delay_s"] = round(delay, 3)
        if delay > act_delay_max_s:
            failed.append("ACTUATION_DELAY_EXCEEDED")
    evidence["realized_heading_change_deg"] = round(realized_heading_change_deg, 3)
    if abs(realized_heading_change_deg) < min_heading_change_deg:
        failed.append("INSUFFICIENT_ACTION")
    return OracleResult("L4_GuidanceAdapter", len(failed) == 0, failed, evidence)


# MRMExecutionStatus state enum (MRMExecutionStatus.msg):
#   STATUS_UNKNOWN=0, ACCEPTED=1, EXECUTING=2, COMPLETED=3, REJECTED=4,
#   STALE_HOLDING=5.
MRM_EXEC_ACCEPTED = 1
MRM_EXEC_EXECUTING = 2
MRM_EXEC_COMPLETED = 3
MRM_EXEC_REJECTED = 4
MRM_EXEC_STALE_HOLDING = 5


def evaluate_l4_gnc_oracle(*, l4_gnc_output: dict) -> OracleResult:
    """L4/GNC MRM-execution oracle (Task 11 stage-separated evidence).

    This is the GNC *execution* role for an M1-authorized MRM command,
    SEPARATE from the L4 GuidanceAdapter route-handoff oracle
    (evaluate_l4_oracle) and from the M1 authority / M7 checker roles. It
    verifies that a fresh, supported M1 MRM command is acknowledged by GNC and
    reaches execution closure — without giving the executor command-origin
    authority.

    l4_gnc_output keys (derived from /l3/m1/mrm_execution_status +
    /l3/gnc/execution_status trace; never invented):
      command_active:        bool, a fresh (non-stale), supported M1 MRM
                              command is currently active.
      gnc_acknowledged:      bool, GNC published an ACCEPTED/EXECUTING ack for
                              the active command id.
      command_id_mismatch:   bool, the GNC ack command id != the active M1
                              command id.
      generation_mismatch:   bool, the GNC ack generation != the active M1
                              command generation.
      rejected:              bool, GNC rejected the command (REJECTED is not
                              execution closure).
      stale_holding:         bool, GNC is STALE_HOLDING (proves fail-safe
                              actuation but is a liveness RED).
      non_mrm_command_source_while_mrm_active: bool, GNC command source is
                              other than MRM while a fresh supported M1 command
                              is active.

    STALE_HOLDING proves fail-safe actuation but remains a liveness RED;
    REJECTED is not execution closure. A COMPLETED or sustained EXECUTING ack
    for a fresh supported command is execution closure (PASS). When no command
    is active, there is nothing to acknowledge (PASS).
    """
    failed: list[str] = []
    evidence: dict = {}
    command_active = bool(l4_gnc_output.get("command_active", False))
    evidence["command_active"] = command_active
    if not command_active:
        return OracleResult("L4_GNC", True, failed, evidence)
    gnc_acknowledged = bool(l4_gnc_output.get("gnc_acknowledged", False))
    evidence["gnc_acknowledged"] = gnc_acknowledged
    if not gnc_acknowledged:
        failed.append("M1_MRM_COMMAND_WITHOUT_GNC_ACK")
    if bool(l4_gnc_output.get("command_id_mismatch", False)):
        failed.append("M1_MRM_COMMAND_ID_MISMATCH")
        evidence["command_id_mismatch"] = True
    if bool(l4_gnc_output.get("generation_mismatch", False)):
        failed.append("M1_MRM_GENERATION_MISMATCH")
        evidence["generation_mismatch"] = True
    if bool(l4_gnc_output.get("rejected", False)):
        failed.append("M1_MRM_COMMAND_REJECTED")
        evidence["rejected"] = True
    if bool(l4_gnc_output.get("stale_holding", False)):
        failed.append("M1_MRM_HEARTBEAT_STALE_AT_GNC")
        evidence["stale_holding"] = True
    if bool(l4_gnc_output.get("non_mrm_command_source_while_mrm_active", False)):
        failed.append("GNC_NON_MRM_SOURCE_DURING_MRM")
        evidence["non_mrm_command_source_while_mrm_active"] = True
    return OracleResult("L4_GNC", len(failed) == 0, failed, evidence)
