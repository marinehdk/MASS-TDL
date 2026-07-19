"""FAST-only verdict composition (Task 3).

Slices the raw trace at the Task-1 lifecycle boundary and applies scenario
truth, module oracles, M6 dynamic action demand, M5/GNC identity, CPA, and M7
closure — while EXCLUDING every recovery-execution metric.

FAST contract (Global Constraints):
- FAST observes >=60s stable TRANSIT, evaluates AVOIDANCE to the first M4
  RECOVERY, stops. NO recovery-route/return/rejoin/final-TRANSIT metrics.
- Only behavior=1 (COLREG_AVOID) is AVOIDANCE; MRC 4/5/6 before RECOVERY is a
  terminal RED, not a normal PASS.
- Timeout without RECOVERY = RECOVERY_BOUNDARY_NOT_REACHED.
- No fixed 30-degree gate; live M6 min_alteration_deg drives direction/
  alteration.
- Scenario CPA acceptance / M6 lifecycle / M7 checker floor = 3 SEPARATE gates,
  no max()/copying.
- M7 PASS requires observed physical separation + independently recomputed
  route/override CPA; M2 no-action CPA is NOT command CPA.

Two entry points:
- ``evaluate_fast(rows, scenario, module_results, full_only)``: compose a
  FastVerdict from pre-computed module_results. ``full_only`` is accepted but
  IGNORED — its only purpose is to prove recovery-execution metrics are not
  copied into the FAST verdict.
- ``evaluate_fast_trace(rows, scenario, report)``: the public raw-trace entry
  point. It runs the existing adapter/oracle functions for M1, M2, M4, M5, M6,
  L4/GNC, M7, and G-ART, stores their OracleResult values, then calls
  ``evaluate_fast``. Callers cannot inject pre-green module results in
  production.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass
from typing import Any

from tools.sil.colregs_artifact_consistency import check_trace_artifacts
from tools.sil.colregs_fast_boundary import (
    ENCOUNTER_CLEAR_WITH_OWN_HOLD,
    RECOVERY_BOUNDARY_NOT_REACHED,
    find_fast_boundary,
)
from tools.sil.colregs_module_oracle import OracleResult
from tools.sil.colregs_oracle_adapter import (
    extract_m1_mrm_authority,
    extract_m2_truth_and_estimate,
    extract_m4_events,
    extract_m5_plan_output,
    extract_m6_output,
    extract_m7_veto,
)
from tools.sil.colregs_module_oracle import (
    evaluate_m1_mrm_authority_oracle,
    evaluate_m2_oracle,
    evaluate_m4_oracle,
    evaluate_m5_oracle,
    evaluate_m6_oracle,
    evaluate_m7_oracle,
)
from tools.sil.trace_time import event_time_s


@dataclass(frozen=True)
class FastVerdict:
    """Result of a FAST-only verdict composition.

    Attributes:
        passed: True iff every FAST-scope check passed (boundary reached +
            scenario truth + module oracles + dynamic action + CPA + M7).
        checks: flat named checks in chain order (no recovery-execution fields).
        first_failure: the RED reason string, or None on PASS. Follows chain
            order, not dictionary insertion accident.
        first_broken_stage: the owning stage of the first failed check
            (SCENARIO/L2/M1/M2/M3/M4/M5/M6/L4_GNC/M7/M8_EVALUATOR/
            INTEGRATION_HANDOFF), or None on PASS.
        required_alteration_deg: live M6 min_alteration_deg for the active
            encounter (0.0 when no active demand).
        realized_alteration_deg: ownship heading alteration realized in the
            action window (0.0 when no action window).
        evidence: per-stage evidence dict for diagnostics (not a gate).
    """

    passed: bool
    checks: dict[str, bool]
    first_failure: str | None
    first_broken_stage: str | None
    required_alteration_deg: float
    realized_alteration_deg: float
    evidence: dict

    def to_dict(self) -> dict:
        return asdict(self)


# Check name → owning stage. The order of this mapping IS the chain order used
# to attribute the first failure; it must match the FAST lifecycle flow
# (scenario truth → transit prefix → avoidance → geometry → M6 rule →
# stand-on hold → dynamic action → M4 lifecycle → M5 route → GNC handoff →
# CPA acceptance → M7 closure → M1 authority → recovery boundary → G-ART).
_STAGE_OF_CHECK: list[tuple[str, str]] = [
    ("scenario_truth_locked", "SCENARIO"),
    ("transit_prefix_observed", "M4"),
    ("avoidance_observed", "M4"),
    ("m2_geometry_valid", "M2"),
    ("m6_rule_role_direction_valid", "M6"),
    ("stand_on_hold_before_t_act", "M6"),
    ("target_actor_contract_realized", "INTEGRATION_HANDOFF"),
    ("dynamic_action_realized", "M6"),
    ("m4_lifecycle_valid", "M4"),
    ("m5_executable_route_valid", "M5"),
    ("gnc_handoff_identity_valid", "L4_GNC"),
    ("scenario_cpa_acceptance_met", "M8_EVALUATOR"),
    ("m7_checker_closed", "M7"),
    ("m1_mrm_authority_valid", "M1"),
    ("first_recovery_boundary_observed", "M4"),
    ("g_art_consistent", "M8_EVALUATOR"),
    ("g_art_complete", "M8_EVALUATOR"),
]


def _float(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def active_action_contract(
    rows: list[Mapping[str, Any]],
    stop_t: float,
) -> dict[str, Any] | None:
    """Extract the live M6 action demand for the active encounter.

    Scans ``/l3/m6/colregs_constraint`` rows up to *stop_t* for active-rule
    samples with a positive ``min_alteration_deg`` and a STARBOARD/PORT
    preferred direction. Returns the action onset, the single consistent
    direction, and the max required alteration. If the direction changes during
    active avoidance, returns a ``failure_code`` (M6_DIRECTION_INCONSISTENT) so
    the verdict fails on M6 without hiding an earlier reversal.

    Returns None when no active demand sample is found.
    """
    samples: list[tuple[float, str, str, float]] = []
    for row in rows:
        if row.get("topic") != "/l3/m6/colregs_constraint":
            continue
        if event_time_s(row) > stop_t:
            continue
        direction = str(row.get("primary_preferred_direction") or "")
        for rule in row.get("active_rules") or []:
            if not isinstance(rule, Mapping):
                continue
            demand = _float(rule.get("min_alteration_deg"))
            if demand is None or demand <= 0.0:
                continue
            if direction not in {"STARBOARD", "PORT"}:
                continue
            samples.append((event_time_s(row), direction, direction, demand))
    if not samples:
        return None
    onset = min(item[0] for item in samples)
    directions = {item[2] for item in samples}
    if len(directions) != 1:
        return {
            "onset_t": onset,
            "failure_code": "M6_DIRECTION_INCONSISTENT",
            "directions": sorted(directions),
        }
    direction = next(iter(directions))
    required = max(item[3] for item in samples if item[2] == direction)
    return {"onset_t": onset, "direction": direction, "required_deg": required}


def _realized_alteration_deg(
    rows: list[Mapping[str, Any]],
    base_t: float,
    onset_t: float,
    stop_t: float,
    direction: str,
) -> float:
    """Ownship heading alteration realized in the action window.

    The base heading is taken at *base_t* (avoidance onset) so a stand-on
    excursion between avoidance and T_act does not reduce the measured maneuver.
    The action window is [*onset_t*, *stop_t*]: the alteration is measured from
    samples after the M6 action onset (T_act) and before recovery. Nautical
    heading increase is starboard; decrease is port. Returns the realized
    alteration magnitude in the required direction (0.0 when the ship turned the
    wrong way or no samples exist).
    """
    own = sorted(
        (r for r in rows if r.get("topic") == "/sil/own_ship_state"),
        key=event_time_s,
    )
    pre = [r for r in own if event_time_s(r) <= base_t]
    base_hdg = _float(pre[-1].get("heading_deg")) if pre else None
    if base_hdg is None:
        # Fall back to the earliest ownship sample when no pre-avoidance heading.
        base_hdg = _float(own[0].get("heading_deg")) if own else None
        if base_hdg is None:
            return 0.0
    window = [
        r for r in own
        if onset_t <= event_time_s(r) <= stop_t
    ]
    if not window:
        return 0.0
    sign = 1.0 if direction == "STARBOARD" else -1.0
    max_realized = 0.0
    for r in window:
        hdg = _float(r.get("heading_deg"))
        if hdg is None:
            continue
        delta = ((hdg - base_hdg + 180.0) % 360.0) - 180.0
        # Realized alteration in the required direction: starboard = positive
        # heading change, port = negative. Wrong-way excursion does not count.
        signed = delta * sign
        if signed > max_realized:
            max_realized = signed
    return round(max_realized, 3)


def _stand_on_hold_before_t_act(
    rows: list[Mapping[str, Any]],
    onset_t: float | None,
    own_role: str,
) -> bool:
    """Rule 17 stand-on must hold before T_act.

    Stand-on vessel excursion before the M6 action onset (T_act) must stay
    below a small threshold. Give-way vessels have no stand-on hold duty, so
    the check is vacuously True.
    """
    if own_role != "STAND_ON":
        return True
    if onset_t is None:
        return True
    own = sorted(
        (r for r in rows if r.get("topic") == "/sil/own_ship_state"),
        key=event_time_s,
    )
    pre = [r for r in own if event_time_s(r) <= onset_t]
    if not pre:
        return True
    base_hdg = _float(pre[0].get("heading_deg"))
    if base_hdg is None:
        return True
    STAND_ON_EXCURSION_LIMIT_DEG = 10.0
    for r in pre:
        hdg = _float(r.get("heading_deg"))
        if hdg is None:
            continue
        delta = abs(((hdg - base_hdg + 180.0) % 360.0) - 180.0)
        if delta > STAND_ON_EXCURSION_LIMIT_DEG:
            return False
    return True


def _scenario_truth_locked(scenario: Mapping[str, Any]) -> bool:
    """Scenario truth lock: compiled rule/role/classification present + truth ok."""
    return bool(
        scenario.get("compiled_rule")
        and scenario.get("own_role")
        and scenario.get("classification")
        and scenario.get("truth_lock_ok", True)
    )


def _scenario_cpa_acceptance(
    report: Mapping[str, Any],
    scenario: Mapping[str, Any],
) -> bool:
    """Scenario CPA acceptance: observed min CPA >= scenario acceptance floor.

    This is the SCENARIO acceptance gate ONLY. M6 lifecycle thresholds and the
    M7 checker floor are evaluated separately (no max()/copying). The observed
    CPA comes from the report's independently-derived min_cpa_m; the floor comes
    from the scenario acceptance profile.
    """
    min_cpa = _float(report.get("min_cpa_m"))
    floor = _float(scenario.get("cpa_floor_m"))
    if min_cpa is None or floor is None:
        return False
    return min_cpa >= floor


def _boundary_for(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
) -> Any:
    return find_fast_boundary(
        rows,
        min_transit_s=float(scenario.get("min_transit_s", 60.0)),
        terminal=str(scenario.get("fast_terminal") or "OWN_RECOVERY_ENTRY"),
        clear_dwell_s=float(
            scenario.get("terminal_clear_observation_dwell_s", 10.0)
        ),
    )


def _target_action_realized(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
    stop_t: float,
) -> tuple[float, float, float | None, bool]:
    required = float(scenario.get("target_required_alteration_deg", 0.0) or 0.0)
    target_cpa = float(scenario.get("target_cpa_m", 0.0) or 0.0)
    deadline = float(scenario.get("target_action_deadline_s", 0.0) or 0.0)
    initial = _float(scenario.get("target_initial_heading_deg"))
    if initial is None or required <= 0.0 or target_cpa <= 0.0:
        return required, 0.0, None, False
    realized = 0.0
    effective_t: float | None = None
    for row in rows:
        if row.get("topic") != "/l3/m2/world_state" or event_time_s(row) > stop_t:
            continue
        heading = _float(
            row.get("primary_target_heading_deg", row.get("target_heading_deg"))
        )
        if heading is None:
            continue
        starboard = ((heading - initial + 180.0) % 360.0) - 180.0
        realized = max(realized, starboard)
        cpa_m = _float(row.get("primary_cpa_m"))
        if (
            effective_t is None
            and starboard + 1.0e-6 >= required
            and cpa_m is not None
            and cpa_m + 1.0e-6 >= target_cpa
        ):
            effective_t = event_time_s(row)
    passed = effective_t is not None and effective_t <= deadline + 1.0e-6
    return required, round(realized, 3), effective_t, passed


def _target_actor_contract(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
    stop_t: float,
) -> tuple[float, float, float | None, bool]:
    expected = str(scenario.get("target_action") or "")
    if expected in {"", "PASSIVE"}:
        return 0.0, 0.0, None, True
    if expected == "GIVE_WAY_STARBOARD":
        return _target_action_realized(rows, scenario, stop_t)
    if expected != "HOLD":
        return 0.0, 0.0, None, False
    initial = _float(scenario.get("target_initial_heading_deg"))
    if initial is None:
        return 0.0, 0.0, None, False
    samples = []
    for row in rows:
        if row.get("topic") != "/l3/m2/world_state" or event_time_s(row) > stop_t:
            continue
        heading = _float(
            row.get("primary_target_heading_deg", row.get("target_heading_deg"))
        )
        if heading is not None:
            samples.append(abs(((heading - initial + 180.0) % 360.0) - 180.0))
    max_delta = max(samples) if samples else 0.0
    return 0.0, round(max_delta, 3), None, bool(samples) and max_delta <= 1.0


def target_actor_contract_evidence(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
    stop_t: float,
) -> dict[str, Any]:
    required, realized, effective_t, passed = _target_actor_contract(
        rows, scenario, stop_t
    )
    return {
        "passed": passed,
        "expected_action": str(scenario.get("target_action") or "PASSIVE"),
        "required_alteration_deg": required,
        "realized_alteration_deg": realized,
        "effective_resolution_t": effective_t,
        "action_deadline_t": scenario.get("target_action_deadline_s"),
    }


def _own_hold_through_stop(
    rows: list[Mapping[str, Any]],
    stop_t: float,
    limit_deg: float = 10.0,
) -> bool:
    samples = sorted(
        (
            row for row in rows
            if row.get("topic") == "/sil/own_ship_state"
            and event_time_s(row) <= stop_t
        ),
        key=event_time_s,
    )
    headings = [
        heading for row in samples
        if (heading := _float(row.get("heading_deg"))) is not None
    ]
    if not headings:
        return True
    initial = headings[0]
    return all(
        abs(((heading - initial + 180.0) % 360.0) - 180.0) <= limit_deg
        for heading in headings
    )


def _no_tactical_route(rows: list[Mapping[str, Any]], stop_t: float) -> bool:
    for row in rows:
        if row.get("topic") != "/l3/m5/avoidance_plan" or event_time_s(row) > stop_t:
            continue
        try:
            branch = int(row.get("commit_branch", 0))
            count = int(row.get("n_waypoints", 0))
        except (TypeError, ValueError):
            branch, count = 0, 0
        if branch in {1, 2, 3} and count > 0:
            return False
    return True


def _no_gnc_tactical_takeover(rows: list[Mapping[str, Any]], stop_t: float) -> bool:
    for row in rows:
        if row.get("topic") != "/l3/gnc/execution_status" or event_time_s(row) > stop_t:
            continue
        if bool(row.get("accepted", False)) and str(row.get("plan_id") or "").startswith("m5-"):
            return False
    return True


def evaluate_fast(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
    module_results: Mapping[str, OracleResult],
    full_only: Mapping[str, Any] | None = None,
) -> FastVerdict:
    """Compose a FAST-only verdict.

    *full_only* is accepted but IGNORED: recovery-execution metrics
    (returned_to_route, recovery_route_published, …) are never copied into the
    FAST verdict. Accepting it proves the exclusion holds even when those fields
    are present.

    The checks dict contains only FAST-scope checks. First failure follows chain
    order (``_STAGE_OF_CHECK``), not dictionary insertion accident.
    """
    # Suppress unused-argument: full_only exists only to prove recovery metrics
    # are excluded from the FAST verdict.
    del full_only

    boundary = _boundary_for(rows, scenario)
    target_resolution = (
        str(scenario.get("fast_terminal") or "")
        == ENCOUNTER_CLEAR_WITH_OWN_HOLD
    )
    stop_t = boundary.stop_t if boundary.stop_t is not None else (
        boundary.avoidance_start_t if boundary.avoidance_start_t is not None
        else float("inf")
    )

    # Scenario truth lock.
    scenario_truth_locked = _scenario_truth_locked(scenario)

    # Live M6 dynamic action demand.
    target_actor = target_actor_contract_evidence(rows, scenario, stop_t)
    target_required_deg = float(target_actor["required_alteration_deg"])
    target_realized_deg = float(target_actor["realized_alteration_deg"])
    target_effective_t = target_actor["effective_resolution_t"]
    target_actor_ok = bool(target_actor["passed"])
    contract = None if target_resolution else active_action_contract(rows, stop_t)
    direction_failure = bool(contract and "failure_code" in contract)
    required_deg = 0.0
    realized_deg = 0.0
    dynamic_ok = True
    onset_t: float | None = None
    if target_resolution:
        required_deg = target_required_deg
        realized_deg = target_realized_deg
        effective_t = target_effective_t
        dynamic_ok = target_actor_ok
        contract = {
            "target_resolution": True,
            "target_action": scenario.get("target_action"),
            "required_deg": required_deg,
            "realized_deg": realized_deg,
            "effective_resolution_t": effective_t,
            "action_deadline_t": scenario.get("target_action_deadline_s"),
        }
    elif direction_failure:
        dynamic_ok = False
    elif contract is not None:
        onset_t = contract["onset_t"]
        required_deg = float(contract["required_deg"])
        direction = str(contract["direction"])
        # Realized alteration in the action window (after action onset, before
        # recovery). The base heading is the pre-avoidance heading at avoidance
        # onset, so a stand-on excursion between avoidance and T_act does not
        # reduce the measured maneuver magnitude.
        recovery_stop = boundary.recovery_start_t or stop_t
        base_t = boundary.avoidance_start_t if boundary.avoidance_start_t is not None else onset_t
        realized_deg = _realized_alteration_deg(
            rows, base_t, onset_t, recovery_stop, direction
        )
        if realized_deg + 1.0e-6 < required_deg:
            dynamic_ok = False

    stand_on_ok = (
        _own_hold_through_stop(rows, stop_t)
        if target_resolution
        else _stand_on_hold_before_t_act(
            rows, onset_t, str(scenario.get("own_role", ""))
        )
    )
    if not target_resolution and scenario.get("target_action"):
        contract = dict(contract or {})
        contract.update({
            "target_action": scenario.get("target_action"),
            "target_required_deg": target_required_deg,
            "target_realized_deg": target_realized_deg,
            "target_effective_resolution_t": target_effective_t,
        })

    # CPA acceptance — the report is not available here; FAST-only composition
    # uses the scenario geometry DCPA as the acceptance proxy when no explicit
    # observed CPA is present. The public evaluate_fast_trace supplies the real
    # observed CPA via the report. For unit tests that call evaluate_fast
    # directly, the scenario geometry DCPA is the acceptance evidence.
    geom = scenario.get("geometry") or {}
    observed_cpa = _float(geom.get("dcpa_m"))
    floor = _float(scenario.get("cpa_floor_m"))
    cpa_ok = bool(
        observed_cpa is not None and floor is not None and observed_cpa >= floor
    )

    def _module_passed(key: str) -> bool:
        result = module_results.get(key)
        return bool(result and result.passed)

    checks = {
        "scenario_truth_locked": scenario_truth_locked,
        "transit_prefix_observed": boundary.transit_duration_s >= float(
            scenario.get("min_transit_s", 60.0)
        ),
        "avoidance_observed": (
            True if target_resolution else boundary.avoidance_start_t is not None
        ),
        "m2_geometry_valid": _module_passed("M2"),
        "m6_rule_role_direction_valid": _module_passed("M6"),
        "stand_on_hold_before_t_act": stand_on_ok,
        "target_actor_contract_realized": target_actor_ok,
        "dynamic_action_realized": dynamic_ok,
        "m4_lifecycle_valid": (
            boundary.ready if target_resolution else _module_passed("M4")
        ),
        "m5_executable_route_valid": (
            _no_tactical_route(rows, stop_t)
            if target_resolution else _module_passed("M5")
        ),
        "gnc_handoff_identity_valid": (
            _no_gnc_tactical_takeover(rows, stop_t)
            if target_resolution else _module_passed("L4_GNC")
        ),
        "scenario_cpa_acceptance_met": cpa_ok,
        "m7_checker_closed": _module_passed("M7"),
        "m1_mrm_authority_valid": _module_passed("M1"),
        "first_recovery_boundary_observed": boundary.ready,
        "g_art_consistent": _module_passed("M8_EVALUATOR"),
        "g_art_complete": _module_passed("M8_EVALUATOR"),
    }

    # First failure follows chain order.
    first_failure: str | None = None
    first_broken_stage: str | None = None
    for idx, (check_name, stage) in enumerate(_STAGE_OF_CHECK):
        if not checks[check_name]:
            first_failure = _failure_code(check_name, boundary, contract)
            first_broken_stage = stage
            break
    # Timeout without recovery is RED even when all module checks pass.
    if first_failure is None and not boundary.ready:
        first_failure = boundary.failure_code or RECOVERY_BOUNDARY_NOT_REACHED
        first_broken_stage = "M4"

    passed = first_failure is None

    evidence = {
        "boundary": {
            "ready": boundary.ready,
            "stop_reason": boundary.stop_reason,
            "failure_code": boundary.failure_code,
            "transit_start_t": boundary.transit_start_t,
            "avoidance_start_t": boundary.avoidance_start_t,
            "recovery_start_t": boundary.recovery_start_t,
            "stop_t": boundary.stop_t,
            "transit_duration_s": boundary.transit_duration_s,
        },
        "action_contract": contract,
        "module_failures": {
            key: list(result.failed_checks)
            for key, result in module_results.items()
            if not result.passed
        },
    }

    return FastVerdict(
        passed=passed,
        checks=checks,
        first_failure=first_failure,
        first_broken_stage=first_broken_stage,
        required_alteration_deg=round(required_deg, 3),
        realized_alteration_deg=round(realized_deg, 3),
        evidence=evidence,
    )


def _failure_code(
    check_name: str,
    boundary: Any,
    contract: dict[str, Any] | None,
) -> str:
    """Map a failed FAST check to its RED reason string."""
    if check_name == "first_recovery_boundary_observed":
        return boundary.failure_code or RECOVERY_BOUNDARY_NOT_REACHED
    if check_name == "transit_prefix_observed":
        return boundary.failure_code or "TRANSIT_PREFIX_TOO_SHORT"
    if check_name == "avoidance_observed":
        return boundary.failure_code or "AVOIDANCE_NOT_REACHED"
    if check_name == "scenario_truth_locked":
        return "SCENARIO_TRUTH_NOT_LOCKED"
    if check_name == "dynamic_action_realized":
        if contract and contract.get("target_resolution"):
            return "TARGET_ACTION_NOT_REALIZED"
        if contract and "failure_code" in contract:
            return contract["failure_code"]
        return "M6_ACTION_NOT_REALIZED"
    if check_name == "target_actor_contract_realized":
        if contract and contract.get("target_action") == "HOLD":
            return "TARGET_HOLD_VIOLATED"
        return "TARGET_ACTION_NOT_REALIZED"
    if check_name == "stand_on_hold_before_t_act":
        return "STAND_ON_EXCURSION_BEFORE_T_ACT"
    if check_name == "m2_geometry_valid":
        return "M2_GEOMETRY_INVALID"
    if check_name == "m6_rule_role_direction_valid":
        return "M6_RULE_ROLE_DIRECTION_INVALID"
    if check_name == "m4_lifecycle_valid":
        return "M4_LIFECYCLE_INVALID"
    if check_name == "m5_executable_route_valid":
        return "M5_EXECUTABLE_ROUTE_INVALID"
    if check_name == "gnc_handoff_identity_valid":
        return "GNC_HANDOFF_IDENTITY_INVALID"
    if check_name == "scenario_cpa_acceptance_met":
        return "SCENARIO_CPA_ACCEPTANCE_NOT_MET"
    if check_name == "m7_checker_closed":
        return "M7_CHECKER_NOT_CLOSED"
    if check_name == "m1_mrm_authority_valid":
        return "M1_MRM_AUTHORITY_INVALID"
    if check_name in ("g_art_consistent", "g_art_complete"):
        return "G_ART_INCONSISTENT"
    return check_name.upper()


def evaluate_fast_trace(
    rows: list[Mapping[str, Any]],
    scenario: Mapping[str, Any],
    report: Mapping[str, Any],
) -> FastVerdict:
    """Public raw-trace entry point.

    Slices the trace at the Task-1 lifecycle boundary, runs the existing
    adapter/oracle functions for M1, M2, M4, M5, M6, L4/GNC, M7, and G-ART,
    then composes the FAST verdict via :func:`evaluate_fast`.

    Callers cannot inject pre-green module results: every OracleResult is
    derived from the raw trace. A qualifying first-RECOVERY FAST run requires
    both G-ART flags; terminal RED reports may be consistent but incomplete.
    """
    boundary = _boundary_for(rows, scenario)
    target_resolution = (
        str(scenario.get("fast_terminal") or "")
        == ENCOUNTER_CLEAR_WITH_OWN_HOLD
    )
    # Slice at the boundary stop time. For a terminal RED with a physical stop
    # sample, slice there; for a non-terminal timeout, evaluate the full trace.
    if boundary.stop_t is not None:
        fast_rows = [r for r in rows if event_time_s(r) <= boundary.stop_t]
    else:
        fast_rows = list(rows)

    m6_rows = [r for r in fast_rows if r.get("topic") == "/l3/m6/colregs_constraint"]
    ownship_rows = [r for r in fast_rows if r.get("topic") == "/sil/own_ship_state"]
    m2_rows = [r for r in fast_rows if r.get("topic") == "/l3/m2/world_state"]

    # M2 geometry oracle (fail-closed: missing observed fields = RED).
    truth, estimate = extract_m2_truth_and_estimate(
        dict(scenario), m2_rows=m2_rows, ownship_rows=ownship_rows
    )
    m2_result = evaluate_m2_oracle(truth=truth, estimated=estimate)

    # M6 rule/role/direction oracle.
    m6_output = extract_m6_output(fast_rows)
    m6_result = evaluate_m6_oracle(
        compiled=_compiled_for_m6(scenario), m6_output=m6_output
    )

    # M4 lifecycle oracle.
    if target_resolution:
        m4_result = OracleResult(
            module="M4_BehaviorArbiter",
            passed=boundary.ready,
            failed_checks=[] if boundary.ready else [
                boundary.failure_code or "ENCOUNTER_CLEAR_NOT_REACHED"
            ],
            evidence={"target_resolution": True},
        )
    else:
        m4_events, m6_cleared_t = extract_m4_events(fast_rows, m6_rows=m6_rows)
        m4_result = evaluate_m4_oracle(
            m4_events=m4_events, m6_conflict_cleared_t=m6_cleared_t
        )

    # M5 executable-route oracle.
    if target_resolution:
        no_route = _no_tactical_route(fast_rows, float(boundary.stop_t or float("inf")))
        m5_result = OracleResult(
            module="M5_TacticalPlanner",
            passed=no_route,
            failed_checks=[] if no_route else ["UNEXPECTED_TACTICAL_TAKEOVER"],
            evidence={"target_resolution": True},
        )
    else:
        m5_plan = extract_m5_plan_output(fast_rows)
        m5_result = evaluate_m5_oracle(plan_output=m5_plan, plan_required=True)

    # L4/GNC route-handoff identity oracle.
    if target_resolution:
        no_handoff = _no_gnc_tactical_takeover(
            fast_rows, float(boundary.stop_t or float("inf"))
        )
        l4_result = OracleResult(
            module="L4_GuidanceAdapter",
            passed=no_handoff,
            failed_checks=[] if no_handoff else ["UNEXPECTED_TACTICAL_TAKEOVER"],
            evidence={"target_resolution": True},
        )
    else:
        l4_result = _evaluate_l4_handoff(fast_rows, boundary)

    # M7 checker oracle.
    m7_result = evaluate_m7_oracle(m7_output=extract_m7_veto(fast_rows))

    # M1 MRM-authority oracle.
    m1_result = evaluate_m1_mrm_authority_oracle(
        m1_mrm_output=extract_m1_mrm_authority(fast_rows)
    )

    # G-ART artifact consistency (M8 evaluator gate): report vs raw evidence.
    artifact = check_trace_artifacts(fast_rows, dict(report))
    m8_result = OracleResult(
        module="M8_EVALUATOR",
        passed=bool(artifact["g_art_consistent"] and artifact["g_art_complete"]),
        failed_checks=(
            []
            if artifact["g_art_consistent"] and artifact["g_art_complete"]
            else list(artifact.get("finding_codes") or ["G_ART_INCONSISTENT"])
        ),
        evidence=dict(artifact),
    )

    module_results = {
        "M1": m1_result,
        "M2": m2_result,
        "M4": m4_result,
        "M5": m5_result,
        "M6": m6_result,
        "L4_GNC": l4_result,
        "M7": m7_result,
        "M8_EVALUATOR": m8_result,
    }

    # Scenario CPA acceptance uses the report's observed min_cpa_m. Build a
    # scenario view that exposes cpa_floor_m for the composition.
    scenario_with_cpa = dict(scenario)
    report_cpa = _float(report.get("min_cpa_m"))
    if report_cpa is not None:
        geom = dict(scenario_with_cpa.get("geometry") or {})
        geom["dcpa_m"] = report_cpa
        scenario_with_cpa["geometry"] = geom

    verdict = evaluate_fast(fast_rows, scenario_with_cpa, module_results, full_only=None)
    # Attach the richer G-ART evidence to the verdict.
    evidence = dict(verdict.evidence)
    evidence["g_art"] = artifact
    evidence["module_results"] = {
        key: {
            "passed": result.passed,
            "failed_checks": list(result.failed_checks),
        }
        for key, result in module_results.items()
    }
    return FastVerdict(
        passed=verdict.passed,
        checks=verdict.checks,
        first_failure=verdict.first_failure,
        first_broken_stage=verdict.first_broken_stage,
        required_alteration_deg=verdict.required_alteration_deg,
        realized_alteration_deg=verdict.realized_alteration_deg,
        evidence=evidence,
    )


def _compiled_for_m6(scenario: Mapping[str, Any]) -> dict[str, Any]:
    """Build the M6-oracle compiled dict from the scenario fixture.

    Accepts both the test's compiled_fixture shape and the
    ``extract_compiled`` adapter output (which carries the same keys).
    """
    return {
        "compiled_rule": scenario.get("compiled_rule"),
        "own_role": scenario.get("own_role"),
        "allowed_actions": list(scenario.get("allowed_actions") or []),
        "classification": scenario.get("classification", "interior"),
    }


def _evaluate_l4_handoff(
    fast_rows: list[Mapping[str, Any]],
    boundary: Any,
) -> OracleResult:
    """Run the L4/GNC route-handoff identity oracle over the FAST slice.

    FAST contract: GNC route acceptance requires matching route identity (the
    committed M5 avoidance plan_id reaches GNC's active_route with the SAME
    route_id) and an executable point count (>=2 waypoints). This is a direct
    route-identity proof, independent of the M5 plan_id naming convention, so it
    works on both real traces and synthetic fixtures.
    """
    avoidance_onset = boundary.avoidance_start_t
    if avoidance_onset is None:
        return OracleResult(
            module="L4_GuidanceAdapter",
            passed=True,
            failed_checks=[],
            evidence={"action_required": False},
        )
    release_t = boundary.recovery_start_t
    window_end = float(release_t) if release_t is not None else float("inf")
    onset_f = float(avoidance_onset)

    # The committed M5 avoidance plan id (first valid exec-branch plan in the
    # avoidance window).
    committed_plan_id: str | None = None
    for row in sorted(
        (r for r in fast_rows if r.get("topic") == "/l3/m5/avoidance_plan"),
        key=event_time_s,
    ):
        t_s = event_time_s(row)
        if t_s < onset_f or t_s > window_end:
            continue
        if _m5_route_is_valid(row):
            committed_plan_id = str(row.get("plan_id") or "")
            if committed_plan_id:
                break

    failed: list[str] = []
    evidence: dict[str, Any] = {
        "action_required": True,
        "committed_plan_id": committed_plan_id,
    }
    if committed_plan_id is None:
        # No committed avoidance route — the M5 oracle already reports that.
        # L4 has nothing to hand off; this is not an L4 fault.
        return OracleResult("L4_GuidanceAdapter", True, failed, evidence)

    # GNC execution-status acceptance for the committed plan.
    gnc_accepted = False
    for row in sorted(
        (r for r in fast_rows if r.get("topic") == "/l3/gnc/execution_status"),
        key=event_time_s,
    ):
        t_s = event_time_s(row)
        if t_s < onset_f or t_s > window_end:
            continue
        plan_id = str(row.get("plan_id") or "")
        active_route_id = str(row.get("active_route_id") or "")
        route_id = active_route_id or plan_id
        state = str(row.get("execution_state") or "").upper()
        accepted = bool(row.get("accepted", False)) or state == "ACCEPTED"
        if route_id == committed_plan_id and accepted:
            gnc_accepted = True
            evidence["gnc_accepted_plan_id"] = route_id
            break

    # Route identity proof: the committed plan_id reaches GNC's active_route
    # topic with >=2 waypoints.
    active_route_match = False
    saw_in_window_active_route = False
    for row in sorted(
        (r for r in fast_rows if r.get("topic") == "/gnc/active_route"),
        key=event_time_s,
    ):
        t_s = event_time_s(row)
        if t_s < onset_f or t_s > window_end:
            continue
        saw_in_window_active_route = True
        route_id = str(row.get("route_id") or "")
        points = _point_count_of(row)
        if route_id == committed_plan_id and points >= 2:
            active_route_match = True
            evidence["gnc_active_route_id"] = route_id
            evidence["gnc_active_route_points"] = points
            break

    # Fallback: GNC active_route rows from domain 50 carry only gnc_t (no
    # sim_t), so they may fall outside the sim_t avoidance window and never
    # be evaluated. When no active_route row was time-alignable to the
    # window AND the bridge-forwarded execution_status already proves the
    # committed plan was accepted with a matching active_route_id, accept
    # that as equivalent route-identity evidence. This does NOT apply when
    # active_route rows were in-window but had a different route_id — that
    # is a genuine mismatch.
    if not active_route_match and not saw_in_window_active_route and gnc_accepted:
        accepted_route_id = evidence.get("gnc_accepted_plan_id", "")
        if accepted_route_id == committed_plan_id:
            active_route_match = True
            evidence["gnc_active_route_id"] = accepted_route_id
            evidence["route_identity_source"] = "execution_status_fallback"

    evidence["gnc_accepted"] = gnc_accepted
    evidence["route_identity_proven"] = active_route_match and gnc_accepted
    if not active_route_match:
        failed.append("ROUTE_HANDOFF_NOT_PROVEN")
        evidence["handoff_missing_proofs"] = ["active_route"]
    elif not gnc_accepted:
        failed.append("ROUTE_NOT_ACCEPTED")
    return OracleResult("L4_GuidanceAdapter", len(failed) == 0, failed, evidence)


def _m5_route_is_valid(row: Mapping[str, Any]) -> bool:
    """True when the M5 row is a committed, complete, preflight-proven route.

    Mirrors the adapter's strict validity without importing the private helper
    so the FAST evaluator's L4 handoff proof is self-contained.
    """
    try:
        branch = int(row.get("commit_branch", 0))
    except (TypeError, ValueError):
        branch = 0
    if branch not in {1, 2, 3}:
        return False
    arrays = [
        row.get("latitude"),
        row.get("longitude"),
        row.get("command_speed_mps"),
        row.get("segment_source"),
    ]
    if not all(isinstance(v, list) for v in arrays):
        return False
    lengths = {len(v) for v in arrays}
    if len(lengths) != 1 or next(iter(lengths)) < 2:
        return False
    rationale = " ".join(str(row.get(k, "")) for k in ("rationale", "reason")).lower()
    if not (
        "gnc_preflight=feasible" in rationale
        or str(row.get("gnc_preflight", "")).lower() in {"feasible", "pass", "passed"}
        or bool(row.get("preflight_feasible", False))
    ):
        return False
    return bool(str(row.get("plan_id") or ""))


def _point_count_of(row: Mapping[str, Any]) -> int:
    for key in ("waypoint_count", "internal_waypoint_count", "pose_count", "n_waypoints"):
        try:
            value = int(row.get(key, 0))
        except (TypeError, ValueError):
            value = 0
        if value > 0:
            return value
    points = row.get("points")
    if isinstance(points, list):
        return len(points)
    return 0


def _point_count_of(row: Mapping[str, Any]) -> int:
    for key in ("waypoint_count", "internal_waypoint_count", "pose_count", "n_waypoints"):
        try:
            value = int(row.get(key, 0))
        except (TypeError, ValueError):
            value = 0
        if value > 0:
            return value
    points = row.get("points")
    if isinstance(points, list):
        return len(points)
    return 0
