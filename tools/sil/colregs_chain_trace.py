"""Full-chain COLREGs trace summarizer.

The summarizer is diagnostic only. It must not alter gate verdicts.
"""

from __future__ import annotations

import json
from collections import Counter
from collections.abc import Iterable
from typing import Any


def _value(record: dict[str, Any], *keys: str, default: Any = None) -> Any:
    payload = record.get("msg")
    for key in keys:
        if key in record:
            return record[key]
        if isinstance(payload, dict) and key in payload:
            return payload[key]
    return default


def _transitions(values: list[Any]) -> list[str]:
    out: list[str] = []
    last = None
    have_last = False
    for value in values:
        if not have_last:
            last = value
            have_last = True
            continue
        if value != last:
            out.append(f"{last}->{value}")
            last = value
    return out


def _count_changes(values: list[Any]) -> int:
    return len(_transitions(values))


def _json_dict(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    if not isinstance(value, str) or not value:
        return {}
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError:
        return {}
    return parsed if isinstance(parsed, dict) else {}


def _has_valid_waypoint(record: dict[str, Any]) -> bool:
    n_waypoints = _value(record, "n_waypoints")
    if n_waypoints is not None and int(n_waypoints or 0) > 0:
        return True
    wp0_turn_radius = _value(record, "wp0_turn_radius_m")
    if wp0_turn_radius is not None and abs(float(wp0_turn_radius or 0.0)) > 1.0e-6:
        return True
    waypoints = _value(record, "waypoints", default=[])
    if not isinstance(waypoints, list) or not waypoints:
        return False
    first = waypoints[0]
    if not isinstance(first, dict):
        return True
    radius = first.get("turn_radius_m", first.get("turnRadiusM", 0.0))
    return abs(float(radius or 0.0)) > 1.0e-6


def _asdr_decision(record: dict[str, Any], source_module: str, decision_type: str) -> dict[str, Any]:
    if record.get("topic") != "/l3/asdr/record":
        return {}
    if _value(record, "source_module", default="") != source_module:
        return {}
    if _value(record, "decision_type", default="") != decision_type:
        return {}
    return _json_dict(_value(record, "decision_json"))


def _counter_dict(values: Iterable[Any]) -> dict[str, int]:
    return dict(Counter(str(v) for v in values if v not in (None, "")))


def build_chain_summary(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    rows = sorted((dict(r) for r in records), key=lambda r: float(r.get("sim_t", 0.0) or 0.0))
    route_hashes = [
        _value(r, "route_hash")
        for r in rows
        if r.get("topic") == "/l2/planned_route" and _value(r, "route_hash") is not None
    ]
    m6_rows = [r for r in rows if r.get("topic") == "/l3/m6/colregs_constraint"]
    m6_conflicts = [bool(_value(r, "conflict_detected", default=False)) for r in m6_rows]
    m6_targets = [_value(r, "colregs_chain_target_id", "target_id") for r in m6_rows]
    m4_behaviors = [_value(r, "behavior") for r in rows if r.get("topic") == "/l3/m4/behavior_plan"]
    m5_rows = [r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan"]
    m5_asdr_decisions = [
        decision for decision in (
            _asdr_decision(r, "M5_Tactical_Planner", "avoid_wp") for r in rows
        )
        if decision
    ]
    m5_statuses = [
        str(_value(r, "status", default="")) for r in m5_rows
    ]
    m5_healths = [
        str(_value(r, "planner_health", default="")) for r in m5_rows
    ] + [str(decision.get("planner_health", "")) for decision in m5_asdr_decisions]
    m5_solver_statuses = [str(_value(r, "solver_status", default="")) for r in m5_rows]
    lifecycle_rows = [r for r in rows if r.get("topic") == "/sil/lifecycle_status"]
    l4_asdr_rows = [
        r for r in rows
        if r.get("topic") == "/l3/asdr/record"
        and _value(r, "source_module", default="") == "L4_Guidance_Adapter"
        and _value(r, "decision_type", default="") == "guidance_cmd"
    ]
    l4_execution_sources = [
        str(_json_dict(_value(r, "decision_json")).get("execution_source", ""))
        for r in l4_asdr_rows
    ]
    l4_rows = [
        r for r in rows
        if r.get("topic") in ("/sil/actuator_cmd", "/l4/guidance_cmd")
    ] + l4_asdr_rows
    l4_gnc_rows = [r for r in rows if r.get("topic") == "/l3/gnc/execution_status"]
    l4_rows += l4_gnc_rows
    l4_gnc_plan_ids = [
        str(_value(r, "plan_id", default="")) for r in l4_gnc_rows
    ]
    m7_rows = [r for r in rows if r.get("topic") in ("/l3/checker/veto", "/l3/m7/safety_alert")]

    m6_active = any(m6_conflicts)
    lifecycle_release = any(
        bool(_value(r, "autopilot_enabled", default=False))
        and not bool(_value(r, "avoidance_active", default=True))
        for r in lifecycle_rows
    )

    route_changes = _count_changes(route_hashes)
    m6_toggles = _count_changes(m6_conflicts)
    m4_toggles = _count_changes(m4_behaviors)
    m5_transitions = _transitions([status for status in m5_statuses if status])

    first_stage = "OK"
    reason = "no chain fault detected"
    if route_changes:
        first_stage = "L2"
        reason = "route hash changed before downstream transition"
    elif m6_toggles > 3:
        first_stage = "M6"
        reason = "COLREGs conflict oscillated before downstream transition"
    elif m4_toggles > 3:
        first_stage = "M4"
        reason = "behavior oscillated before planner transition"
    elif lifecycle_release and m6_active:
        first_stage = "L4"
        reason = "lifecycle released while M6 conflict remained active"
    elif m5_transitions:
        first_stage = "M5"
        reason = "M5 status changed with stable upstream inputs"

    return {
        "route": {"hashes": route_hashes, "hash_changes": route_changes},
        "m2": {"present": any(r.get("topic") == "/l3/m2/world_state" for r in rows)},
        "m6": {"conflict_toggles": m6_toggles, "targets": [t for t in m6_targets if t is not None]},
        "m4": {"behavior_toggles": m4_toggles, "behaviors": m4_behaviors},
        "m5": {
            "status_transitions": m5_transitions,
            "planner_health_counts": _counter_dict(m5_healths),
            "solver_status_transitions": _transitions([s for s in m5_solver_statuses if s]),
            "valid_plan_samples": sum(1 for r in m5_rows if _has_valid_waypoint(r)),
            "samples": len(m5_rows) + len(m5_asdr_decisions),
        },
        "lifecycle": {
            "samples": len(lifecycle_rows),
            "released_while_m6_active": bool(lifecycle_release and m6_active),
        },
        "l4": {
            "samples": len(l4_rows),
            "execution_sources": [s for s in l4_execution_sources if s],
            "execution_source_transitions": _transitions([s for s in l4_execution_sources if s]),
            "gnc_execution_state_counts": _counter_dict(
                _value(r, "execution_state") for r in l4_gnc_rows
            ),
            "gnc_reason_counts": _counter_dict(
                _value(r, "reason") for r in l4_gnc_rows
            ),
            "gnc_suggested_action_counts": _counter_dict(
                _value(r, "suggested_action") for r in l4_gnc_rows
            ),
            "gnc_plan_id_changes": _count_changes([p for p in l4_gnc_plan_ids if p]),
        },
        "m7": {"samples": len(m7_rows)},
        "diagnosis": {"first_broken_stage": first_stage, "reason": reason},
    }


def _phase_semantics_ok(result: dict[str, Any]) -> bool:
    phase = result.get("phase_semantics") or {}
    return bool(phase.get("phase_semantics_ok", True))


def _has_recovery_or_transit_release(result: dict[str, Any]) -> bool:
    transitions = result.get("bp_transitions") or []
    saw_avoidance = False
    for item in transitions:
        if not isinstance(item, (list, tuple)) or len(item) < 2:
            continue
        if item[1] in (1, 2):
            saw_avoidance = True
            continue
        if not saw_avoidance:
            continue
        if item[1] in (0, 7):
            return True
    return False


def attach_gate_diagnosis(
    summary: dict[str, Any],
    result: dict[str, Any],
) -> dict[str, Any]:
    """Attach gate-level RED attribution without changing verdict math."""
    diagnosed = dict(summary)
    diagnosis = dict(diagnosed.get("diagnosis") or {})
    diagnosed["diagnosis"] = diagnosis

    if result.get("overall_pass", False):
        diagnosis.setdefault("failing_gate", "NONE")
        return diagnosed

    domain_gates = result.get("domain_gates") or {}
    stage = diagnosis.get("first_broken_stage", "OK")
    reason = diagnosis.get("reason", "no chain fault detected")
    failing_gate = "UNKNOWN"

    if not bool(result.get("cpa_ok", True)):
        failing_gate = "CPA"
        stage = "M5" if stage == "OK" else stage
        reason = (
            f"trajectory CPA floor not met: min_cpa_m={result.get('min_cpa_m')} "
            f"< cpa_floor_m={result.get('cpa_floor_m')}; inspect generalized M6/M5 CPA contract"
        )
    elif not bool(domain_gates.get("risk_gate_ok", True)):
        failing_gate = "RISK"
        stage = "M7" if stage == "OK" else stage
        reason = "risk gate rejected otherwise coherent route; inspect M7 risk-domain scoring"
    elif not _phase_semantics_ok(result):
        failing_gate = "PHASE"
        stage = "M6" if stage == "OK" else stage
        reason = "COLREGs phase semantics gate failed; inspect M6 rule/geometry lifecycle"
    elif bool(result.get("route_return_required", False)) and not bool(result.get("returned_to_route", True)):
        failing_gate = "ROUTE_RETURN"
        if not _has_recovery_or_transit_release(result):
            stage = "M4" if stage == "OK" else stage
            reason = "route return failed with no recovery/transit release from behavior arbiter"
        else:
            stage = "M5" if stage == "OK" else stage
            reason = "route return failed after release; inspect M5 trajectory recovery"
    elif bool(result.get("overtake_required", False)) and not bool(result.get("overtake_completed", True)):
        failing_gate = "OVERTAKE"
        stage = "M6" if stage == "OK" else stage
        reason = "overtake completion gate failed; inspect M6 rule13 relative-progress contract"
    elif not bool(domain_gates.get("seamanship_gate_ok", True)):
        failing_gate = "SEAMANSHIP"
        stage = "M5" if stage == "OK" else stage
        reason = "seamanship gate failed; inspect trajectory smoothness and route recovery"
    elif not bool(result.get("stability_pass", True)):
        failing_gate = "STABILITY"
        stage = "M4" if stage == "OK" else stage
        reason = "behavior/planner stability gate failed; inspect M4/M5 state transitions"

    diagnosis["first_broken_stage"] = stage
    diagnosis["failing_gate"] = failing_gate
    diagnosis["reason"] = reason
    return diagnosed
