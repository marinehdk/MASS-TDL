"""Trace/YAML → oracle-input adapter (Layer 2 of test system v1).

Bridges the raw trace JSONL + scenario YAML to the dict shapes the pure-stdlib
oracles in colregs_module_oracle expect. Pure stdlib, no ROS2/container deps.

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md §5.
"""
from __future__ import annotations

import bisect
import json
import math
import re
from collections.abc import Mapping
from typing import Any

from tools.sil.colregs_module_oracle import OracleResult
from tools.sil.colregs_scenario_audit import (
    _encounter_classification,
    _straight_line_cpa,
)
from tools.sil.trace_time import (
    MAX_EVENT_UNCERTAINTY_S,
    ClockAlignment,
    ClockTransform,
    EventTime,
    EventTimeSelectionError,
    event_time_s,
    select_event_time,
)


_STRICT_EVENT_TIME_KEY = "__g1_event_time"


def _event_time(row: dict[str, Any]) -> EventTime | None:
    selected = row.get(_STRICT_EVENT_TIME_KEY)
    return selected if isinstance(selected, EventTime) else None


def _event_time_s(row: dict[str, Any]) -> float:
    selected = _event_time(row)
    return selected.canonical_s if selected is not None else event_time_s(row)


def _event_time_payload(row: dict[str, Any]) -> dict[str, float | str] | None:
    selected = _event_time(row)
    return selected.as_dict() if selected is not None else None


def _prepare_time_rows(
    rows: list[dict[str, Any]],
    alignment: ClockAlignment | None,
) -> list[dict[str, Any]]:
    if alignment is None:
        if not any(_STRICT_EVENT_TIME_KEY in row for row in rows):
            return rows
        legacy_rows = [dict(row) for row in rows]
        for row in legacy_rows:
            row.pop(_STRICT_EVENT_TIME_KEY, None)
        return legacy_rows
    _require_alignment(alignment, rows)
    prepared = [dict(row) for row in rows]
    selected_times = alignment.select_sequence(prepared)
    for source_row, row, selected in zip(
        rows, prepared, selected_times, strict=True
    ):
        if _STRICT_EVENT_TIME_KEY in source_row:
            cached = source_row[_STRICT_EVENT_TIME_KEY]
            if not isinstance(cached, EventTime) or cached != selected:
                raise EventTimeSelectionError(
                    str(source_row.get("record_id", "<unknown>")),
                    selected.source,
                    alignment.alignment_id,
                    alignment.uncertainty_s,
                    "cached_event_time_mismatch",
                )
        row[_STRICT_EVENT_TIME_KEY] = selected
    return prepared


def _require_alignment(
    alignment: ClockAlignment | None,
    rows: list[dict[str, Any]],
) -> ClockAlignment:
    if isinstance(alignment, ClockAlignment):
        return alignment
    record_id = str(rows[0].get("record_id", "<unknown>")) if rows else "<unknown>"
    raise EventTimeSelectionError(
        record_id,
        None,
        "<missing>",
        MAX_EVENT_UNCERTAINTY_S + 1.0,
        "clock_alignment_missing",
    )


def _validated_boundary_time(
    value: Any,
    alignment: ClockAlignment,
    *,
    record_id: str,
) -> EventTime:
    valid = isinstance(value, EventTime)
    if valid:
        canonical_s = _strict_time_number(value.canonical_s)
        raw_s = _strict_time_number(value.raw_s)
        uncertainty_s = _strict_time_number(value.uncertainty_s)
        transform = (
            alignment.transforms.get(value.source)
            if isinstance(value.source, str)
            and isinstance(alignment.transforms, Mapping)
            else None
        )
        scale = (
            _strict_time_number(transform.scale)
            if isinstance(transform, ClockTransform)
            else None
        )
        offset_s = (
            _strict_time_number(transform.offset_s)
            if isinstance(transform, ClockTransform)
            else None
        )
        valid = (
            canonical_s is not None
            and raw_s is not None
            and uncertainty_s is not None
            and 0.0 <= uncertainty_s <= MAX_EVENT_UNCERTAINTY_S
            and uncertainty_s == alignment.uncertainty_s
            and isinstance(value.source, str)
            and bool(value.source)
            and isinstance(alignment.source_priority, tuple)
            and value.source in alignment.source_priority
            and isinstance(transform, ClockTransform)
            and scale is not None
            and offset_s is not None
            and value.alignment_id == alignment.alignment_id
            and math.isclose(
                canonical_s,
                scale * raw_s + offset_s,
                rel_tol=0.0,
                abs_tol=1.0e-12,
            )
        )
        if valid:
            if value.source == "source_stamp":
                sec = math.floor(raw_s)
                nanosec = round((raw_s - sec) * 1.0e9)
                if nanosec == 1_000_000_000:
                    sec += 1
                    nanosec = 0
                raw_clock = {"source_stamp": {"sec": sec, "nanosec": nanosec}}
            else:
                raw_clock = {value.source: raw_s}
            try:
                recomputed = select_event_time(
                    {
                        "record_id": record_id,
                        **raw_clock,
                        "source_domain": transform.source_domain,
                        "run_generation": alignment.lifecycle_run_generation,
                    },
                    alignment,
                )
            except EventTimeSelectionError:
                valid = False
            else:
                valid = recomputed == value
    if not valid:
        raise EventTimeSelectionError(
            record_id,
            value.source if isinstance(value, EventTime) else None,
            alignment.alignment_id,
            alignment.uncertainty_s,
            "event_time_payload_invalid",
        )
    return value


def _validated_source_boundary_time(
    value: Any,
    rows: list[dict[str, Any]],
    alignment: ClockAlignment,
    *,
    record_id: str | None,
    topic: str,
    boundary_name: str,
) -> EventTime:
    boundary = _validated_boundary_time(
        value,
        alignment,
        record_id=f"<{boundary_name}>",
    )
    if not isinstance(record_id, str) or not record_id:
        raise EventTimeSelectionError(
            f"<{boundary_name}>",
            boundary.source,
            alignment.alignment_id,
            alignment.uncertainty_s,
            "event_time_source_row_identity_missing",
        )
    matches = [
        row
        for row in rows
        if row.get("topic") == topic and row.get("record_id") == record_id
    ]
    if len(matches) != 1:
        reason = (
            "event_time_source_row_missing"
            if not matches
            else "event_time_source_row_ambiguous"
        )
        raise EventTimeSelectionError(
            record_id,
            boundary.source,
            alignment.alignment_id,
            alignment.uncertainty_s,
            reason,
        )
    if _event_time(matches[0]) != boundary:
        raise EventTimeSelectionError(
            record_id,
            boundary.source,
            alignment.alignment_id,
            alignment.uncertainty_s,
            "event_time_source_row_mismatch",
        )
    return boundary


def _strict_time_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def _event_time_evidence(rows: list[dict[str, Any]]) -> list[dict[str, float | str]]:
    return [
        selected.as_dict()
        for row in sorted(rows, key=_event_time_s)
        if (selected := _event_time(row)) is not None
    ]


def _attach_event_times(
    output: dict[str, Any],
    rows: list[dict[str, Any]],
    alignment: ClockAlignment | None,
) -> dict[str, Any]:
    if alignment is not None:
        output["event_times"] = _event_time_evidence(rows)
    return output


# ─── enum maps: ROS2/C++ int enums ↔ oracle string keys ──────────────────

# M6 Role enum (src/l3_tdl_kernel/m6_colregs_reasoner/include/.../types.hpp):
#   STAND_ON=0, GIVE_WAY=1, BOTH_GIVE_WAY=2, FREE=3.
# BOTH_GIVE_WAY (head-on) is a give-way obligation for both vessels, so it
# maps to the oracle's "GIVE_WAY" role.
ROLE_INT_TO_STR: dict[int, str] = {
    0: "STAND_ON",
    1: "GIVE_WAY",
    2: "GIVE_WAY",
    3: "FREE",
}

# COLREGs rule_id (int) → oracle compiled_rule key. Only the actionable
# maneuvering rules have a classification key; 5/7/8/18 are obligations, not
# sector classifications, and are filtered out by the dominant-rule selector.
RULE_ID_TO_KEY: dict[int, str] = {
    13: "Rule13_Overtaking",
    14: "Rule14_HeadOn",
    15: "Rule15_Crossing",
    17: "Rule17_StandOn",  # stand-on is a role, kept for diagnostics
}

# M4 BehaviorType enum (.../m4_behavior_arbiter/include/.../types.hpp):
#   TRANSIT=0, COLREG_AVOID=1, RECOVERY=7.
BEHAVIOR_INT_TO_STR: dict[int, str] = {
    0: "TRANSIT",
    1: "COLREG_AVOID",
    2: "COLREG_AVOID",   # legacy alternate avoid code
    7: "RECOVERY",
}

_CLOSING_MPS_RE = re.compile(r"closing_mps=([-\d.]+)")


def _parse_closing_mps(rationale: str) -> float | None:
    """Extract closing_mps from M4 behavior_plan rationale. None if absent."""
    m = _CLOSING_MPS_RE.search(rationale)
    return float(m.group(1)) if m else None


def _debounced_conflict_onsets(
    rows: list[dict[str, Any]],
    *,
    dwell_s: float = 10.0,
) -> list[float]:
    onsets: list[float] = []
    in_conflict = False
    last_false_t: float | None = None
    for row in rows:
        t_s = _event_time_s(row)
        conflict = bool(_value(row, "conflict_detected", False))
        if conflict:
            if not in_conflict:
                if last_false_t is None or (t_s - last_false_t) >= dwell_s:
                    onsets.append(t_s)
                in_conflict = True
            continue
        if in_conflict:
            last_false_t = t_s
        in_conflict = False
    return onsets


def _stable_conflict_cleared_t(
    rows: list[dict[str, Any]],
    *,
    dwell_s: float = 10.0,
) -> float | None:
    """First conflict=false sample not followed by conflict=true within dwell."""
    m6 = sorted(rows, key=_event_time_s)
    saw_true = False
    for idx, row in enumerate(m6):
        t_s = _event_time_s(row)
        conflict = bool(_value(row, "conflict_detected", False))
        if conflict:
            saw_true = True
            continue
        if not saw_true:
            continue
        next_true_t = None
        for later in m6[idx + 1:]:
            if bool(_value(later, "conflict_detected", False)):
                next_true_t = _event_time_s(later)
                break
        if next_true_t is None or next_true_t - t_s >= dwell_s:
            return t_s
    return None

# M6 preferred_direction trace value → oracle allowed_actions key.
_DIRECTION_TO_ACTION: dict[str, str] = {
    "STARBOARD": "STARBOARD_TURN",
    "PORT": "PORT_TURN",
    "DECELERATE": "DECELERATE",
    "HOLD": "HOLD",
}


def _value(record: dict[str, Any], key: str, default=None):
    v = record.get(key, default)
    return default if v is None else v


def _decision_json(record: dict[str, Any]) -> dict[str, Any]:
    payload = record.get("decision_json")
    if isinstance(payload, dict):
        return payload
    if isinstance(payload, str) and payload:
        try:
            parsed = json.loads(payload)
        except json.JSONDecodeError:
            return {}
        return parsed if isinstance(parsed, dict) else {}
    return {}


def _commit_branch(record: dict[str, Any]) -> int | None:
    try:
        return int(_value(record, "commit_branch"))
    except (TypeError, ValueError):
        return None


def _dominant_rule_id(active_rule_rows: list[dict[str, Any]]) -> int | None:
    """Pick the dominant maneuvering rule from M6 active_rules entries.

    Priority order reflects COLREGs specificity: head-on (14) is the most
    restrictive and overrides crossing (15), which overrides overtaking (13).
    A head-on encounter that also triggers a crossing sub-rule is still head-on.
    Rule 5/7/8 are obligations and Rule 18 is priority — none define the sector.
    """
    priority = [14, 15, 13]  # head-on > crossing > overtaking
    present = {int(r.get("rule_id", 0)) for r in active_rule_rows
               if isinstance(r, dict)}
    for rid in priority:
        if rid in present:
            return rid
    return None


_UINT32_MASK = (1 << 32) - 1


def _coerce_target_id(value: Any) -> int | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _nested_value(record: Any, key: str) -> Any:
    if isinstance(record, dict):
        return record.get(key)
    return getattr(record, key, None)


def _collect_m2_target_ids(rows: list[dict[str, Any]]) -> list[int]:
    ids: set[int] = set()
    for row in rows:
        if row.get("topic") != "/l3/m2/world_state":
            continue
        for key in ("primary_target_id", "target_id"):
            tid = _coerce_target_id(row.get(key))
            if tid is not None:
                ids.add(tid)
        target_ids = row.get("target_ids")
        if isinstance(target_ids, list):
            for value in target_ids:
                tid = _coerce_target_id(value)
                if tid is not None:
                    ids.add(tid)
        targets = row.get("targets")
        if isinstance(targets, list):
            for target in targets:
                for key in ("target_id", "id", "mmsi"):
                    tid = _coerce_target_id(_nested_value(target, key))
                    if tid is not None:
                        ids.add(tid)
                        break
    return sorted(ids)


def _collect_m6_target_ids(rows: list[dict[str, Any]]) -> list[int]:
    ids: set[int] = set()
    for row in rows:
        for key in ("colregs_chain_target_id", "target_id"):
            tid = _coerce_target_id(_value(row, key, None))
            if tid is not None:
                ids.add(tid)
        for active_rule in (row.get("active_rules") or []):
            if not isinstance(active_rule, dict):
                continue
            tid = _coerce_target_id(active_rule.get("target_id"))
            if tid is not None:
                ids.add(tid)
    return sorted(ids)


def _m6_target_identity_evidence(
    *,
    all_rows: list[dict[str, Any]],
    m6_rows: list[dict[str, Any]],
) -> dict[str, Any]:
    m2_ids = _collect_m2_target_ids(all_rows)
    m6_ids = _collect_m6_target_ids(m6_rows)
    evidence: dict[str, Any] = {}
    if m2_ids:
        evidence["m2_target_ids"] = m2_ids
    if m6_ids:
        evidence["m6_target_ids"] = m6_ids
    if not m2_ids or not m6_ids:
        return evidence

    mismatches = [tid for tid in m6_ids if tid not in m2_ids]
    aliases = []
    for tid in mismatches:
        m2_aliases = [
            m2_id for m2_id in m2_ids
            if m2_id > _UINT32_MASK and (m2_id & _UINT32_MASK) == tid
        ]
        if m2_aliases:
            aliases.append({"m6_target_id": tid, "m2_target_ids": m2_aliases})

    evidence["target_id_mismatch_count"] = len(mismatches)
    evidence["target_id_width_alias_count"] = len(aliases)
    if aliases:
        evidence["target_id_width_aliases"] = aliases
    return evidence


# ─── scenario YAML → compiled truth ──────────────────────────────────────

def extract_compiled(scenario_doc: dict[str, Any]) -> dict[str, Any]:
    """Build the oracle compiled-truth dict from a scenario YAML document.

    Uses ``colregs_scenario_audit._straight_line_cpa`` (geometric truth) and
    ``_encounter_classification`` (rule sector + boundary). own_role comes from
    the declared give_way_vessel; BOTH_GIVE_WAY head-on maps to GIVE_WAY.

    NOTE (m5-design-grounding graft, 2026-07-19): the upstream colregs branch
    rewrote this to use ``colregs_scenario_truth.compile_encounter`` (an
    independent EncounterTruth compiler with its own qualification_contract
    dependency chain). That module is not present on this branch and pulling
    it in would drag qualification_contract → suite_manifest → acceptance.
    This branch keeps the scenario_audit-based derivation (same geometric
    truth source the rest of m5 uses) and emits a superset of the keys both
    branches' consumers read. ``target_view_bearing_deg`` is emitted as None
    (no consumer on this branch reads it; fast_evaluator does not use it).
    """
    geometry = _straight_line_cpa(scenario_doc)
    own_initial = scenario_doc["ownShip"]["initial"]
    target_initial = scenario_doc["targetShips"][0]["initial"]
    own_heading = float(own_initial.get("heading", own_initial.get("cog", 0.0)))
    own_course = float(own_initial.get("cog", own_heading))
    target_course = float(target_initial.get(
        "cog", target_initial.get("heading", 0.0)))
    cls = _encounter_classification(
        geometry,
        own_heading,
        own_speed=float(own_initial["sog"]) if "sog" in own_initial else None,
        own_course=own_course,
        target_speed=float(target_initial["sog"]) if "sog" in target_initial else None,
        target_course=target_course,
    )
    metadata = scenario_doc.get("metadata", {})
    encounter = metadata.get("encounter", {})
    give_way_decl = str(encounter.get("give_way_vessel", "")).strip().lower()

    compiled_rule = cls["compiled_rule"]
    if compiled_rule == "Rule14_HeadOn":
        own_role = "GIVE_WAY"
    elif give_way_decl in ("own", "give_way", "give-way"):
        own_role = "GIVE_WAY"
    elif give_way_decl in ("target", "stand_on", "stand-on"):
        own_role = "STAND_ON"
    else:
        own_role = "GIVE_WAY"  # safe default for single-target probes

    allowed = ["STARBOARD_TURN", "DECELERATE"]
    if own_role == "STAND_ON":
        allowed = ["HOLD"]
    own_sog_kn = 0.0
    try:
        own_sog_kn = float(scenario_doc.get("ownShip", {}).get("initial", {}).get("sog", 0.0))
    except (TypeError, ValueError):
        pass
    return {
        "compiled_rule": compiled_rule,
        "own_role": own_role,
        "allowed_actions": allowed,
        "forbidden_actions": ["PORT_TURN"] if own_role == "GIVE_WAY" else [],
        "classification": cls["classification"],
        "compiled_rel_bearing_deg": cls["compiled_rel_bearing_deg"],
        "target_view_bearing_deg": None,
        "boundary_distance_deg": cls["boundary_distance_deg"],
        "overtake_dynamic": cls.get("overtake_dynamic", False),
        "geometry": geometry,
        "own_sog_kn": own_sog_kn,
    }


# ─── trace → M6 oracle input ──────────────────────────────────────────────

def _extract_m6_output(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M6 oracle input from /l3/m6/colregs_constraint trace rows.

    Returns {rule, role, preferred_direction, flip_count, flip_intervals_s}.
    Uses only rows where conflict_detected=true (the active-conflict window).
    """
    rows = _prepare_time_rows(rows, alignment)
    m6_trace_order = [
        r for r in rows if r.get("topic") == "/l3/m6/colregs_constraint"
    ]
    m6 = sorted(m6_trace_order, key=_event_time_s)
    conflict_rows = [r for r in m6 if bool(_value(r, "conflict_detected", False))]
    diagnostic_rows = conflict_rows
    no_own_action_required = False
    if not diagnostic_rows:
        passive_rows = [
            r for r in m6
            if any(
                isinstance(ar, dict)
                and int(ar.get("rule_id", 0)) in (13, 14, 15)
                and int(ar.get("role", -1)) == 0
                for ar in (r.get("active_rules") or [])
            )
        ]
        diagnostic_rows = passive_rows
        no_own_action_required = bool(passive_rows)
    lifecycle = _m6_lifecycle_evidence(rows=m6_trace_order, all_rows=rows)
    target_identity = _m6_target_identity_evidence(
        all_rows=rows, m6_rows=diagnostic_rows)
    if not diagnostic_rows:
        return _attach_event_times(
            {"rule": "", "role": "", "preferred_direction": "",
             "stand_on_in_extremis_action": False,
             "no_own_action_required": False,
             "flip_count": 0, "flip_intervals_s": [],
             **lifecycle, **target_identity},
            m6_trace_order,
            alignment,
        )

    # Dominant rule from active_rules across the conflict window.
    all_active = [ar for r in diagnostic_rows
                  for ar in (r.get("active_rules") or [])
                  if isinstance(ar, dict)]
    dom_rid = _dominant_rule_id(all_active)
    rule = RULE_ID_TO_KEY.get(dom_rid, "") if dom_rid else ""
    stand_on_in_extremis_action = any(
        int(ar.get("rule_id", 0)) == 17
        and int(ar.get("role", -1)) == 0
        and str(ar.get("rule_phase", ar.get("phase", ""))) in (
            "T_act",
            "T_emergency",
            "INDEPENDENT_ACTION",
            "CRITICAL_ACTION",
        )
        and str(ar.get("preferred_direction", "")) in (
            "STARBOARD",
            "DECELERATE",
        )
        for ar in all_active
    )

    # Role: mode of primary_role int over the conflict window. For stand-on
    # no-action diagnostics, conflict_detected remains false and primary_role is
    # FREE; use the diagnostic primary-rule rows instead.
    role_counts: dict[int, int] = {}
    if conflict_rows:
        for r in conflict_rows:
            role_counts[int(_value(r, "primary_role", 0))] = (
                role_counts.get(int(_value(r, "primary_role", 0)), 0) + 1)
    else:
        for ar in all_active:
            if dom_rid is None or int(ar.get("rule_id", 0)) == dom_rid:
                role_int = int(ar.get("role", 3))
                role_counts[role_int] = role_counts.get(role_int, 0) + 1
    dom_role_int = max(role_counts, key=role_counts.get) if role_counts else 3
    role = ROLE_INT_TO_STR.get(dom_role_int, "")

    # Preferred direction: mode of primary_preferred_direction over window, or
    # diagnostic primary-rule preferred_direction for stand-on no-action.
    dir_counts: dict[str, int] = {}
    if conflict_rows:
        for r in conflict_rows:
            d = str(_value(r, "primary_preferred_direction", ""))
            if d:
                dir_counts[d] = dir_counts.get(d, 0) + 1
    else:
        for ar in all_active:
            if dom_rid is None or int(ar.get("rule_id", 0)) == dom_rid:
                d = str(ar.get("preferred_direction", ""))
                if d:
                    dir_counts[d] = dir_counts.get(d, 0) + 1
    dom_dir = max(dir_counts, key=dir_counts.get) if dir_counts else ""
    preferred = _DIRECTION_TO_ACTION.get(dom_dir, dom_dir)

    # Flip count: number of times the dominant rule classification changes
    # WITHIN the conflict window (rule-latch instability). A single continuous
    # conflict episode with a stable dominant rule -> 0 flips. Counting
    # conflict on/off transitions instead would flag a normal single
    # encounter as unstable.
    rule_seq = [_dominant_rule_id(r.get("active_rules") or [])
                for r in diagnostic_rows]
    flips = 0
    prev_rule = rule_seq[0] if rule_seq else None
    for rid in rule_seq[1:]:
        if rid is not None and prev_rule is not None and rid != prev_rule:
            flips += 1
        if rid is not None:
            prev_rule = rid

    # Conflict-episode onsets (for interval diagnostics). Debounce short false
    # samples so trace jitter does not masquerade as rule lifecycle instability.
    onsets = _debounced_conflict_onsets(m6)
    intervals = [round(onsets[i + 1] - onsets[i], 3)
                 for i in range(len(onsets) - 1)]

    return _attach_event_times({
        "rule": rule,
        "role": role,
        "preferred_direction": preferred,
        "stand_on_in_extremis_action": stand_on_in_extremis_action,
        "no_own_action_required": no_own_action_required,
        "flip_count": flips,
        "flip_intervals_s": intervals,
        **target_identity,
        **lifecycle,
    }, m6_trace_order, alignment)


def extract_m6_output_strict(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """G1 M6 extraction; raw clocks and explicit alignment are mandatory."""
    return _extract_m6_output(
        rows,
        alignment=_require_alignment(alignment, rows),
    )


def extract_m6_output(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Legacy M6 extraction retained for non-G1 callers."""
    return _extract_m6_output(rows, alignment=None)


def _m6_lifecycle_evidence(*, rows: list[dict[str, Any]],
                           all_rows: list[dict[str, Any]]) -> dict[str, Any]:
    finally_resolved_count = 0
    for r in all_rows:
        if r.get("topic") != "/l3/asdr/record":
            continue
        payload = _decision_json(r)
        if bool(payload.get("finally_resolved")):
            finally_resolved_count += 1

    release_sample_count = 0
    past_clear_sample_count = 0
    first_past_clear_index: int | None = None
    for index, r in enumerate(rows):
        state = _value(r, "encounter_state")
        phase = str(_value(r, "phase", "") or "").upper()
        if state == 3 or phase == "RELEASE":
            release_sample_count += 1
        if bool(_value(r, "past_clear", False)):
            past_clear_sample_count += 1
            if first_past_clear_index is None:
                first_past_clear_index = index

    post_release_conflict_count = 0
    if first_past_clear_index is not None:
        for r in rows[first_past_clear_index + 1:]:
            if bool(_value(r, "conflict_detected", False)):
                post_release_conflict_count += 1

    evidence: dict[str, Any] = {
        "finally_resolved_count": finally_resolved_count,
        "release_sample_count": release_sample_count,
        "past_clear_sample_count": past_clear_sample_count,
        "post_release_conflict_count": post_release_conflict_count,
    }
    onset_tcpa_s = _m6_onset_tcpa_s(rows=rows, all_rows=all_rows)
    if onset_tcpa_s is not None:
        evidence["onset_tcpa_s"] = onset_tcpa_s
    return evidence


def _m6_onset_tcpa_s(*, rows: list[dict[str, Any]],
                     all_rows: list[dict[str, Any]]) -> float | None:
    first_conflict_t: float | None = None
    for r in rows:
        if bool(_value(r, "conflict_detected", False)):
            first_conflict_t = _event_time_s(r)
            break
    if first_conflict_t is None:
        return None

    m2_rows = sorted(
        (
            r for r in all_rows
            if r.get("topic") == "/l3/m2/world_state"
            and r.get("primary_tcpa_s") is not None
        ),
        key=_event_time_s,
    )
    if not m2_rows:
        return None
    times = [_event_time_s(r) for r in m2_rows]
    insert_at = bisect.bisect_left(times, first_conflict_t)
    candidates = []
    if insert_at > 0:
        candidates.append(insert_at - 1)
    if insert_at < len(times):
        candidates.append(insert_at)
    idx = min(candidates, key=lambda i: abs(times[i] - first_conflict_t))
    try:
        return round(float(m2_rows[idx]["primary_tcpa_s"]), 3)
    except (TypeError, ValueError):
        return None


# ─── trace → M4 oracle input ──────────────────────────────────────────────

def extract_m4_events(
    rows: list[dict[str, Any]],
    *,
    m6_rows: list[dict[str, Any]] | None = None,
    alignment: ClockAlignment | None = None,
) -> tuple[list[dict[str, Any]], float | None]:
    """Extract M4 oracle input from /l3/m4/behavior_plan trace rows.

    Returns (events, m6_conflict_cleared_t). events is a list of
    {t, behavior_str} sorted by sim_t, with behavior mapped via
    BEHAVIOR_INT_TO_STR. m6_conflict_cleared_t is extracted from M6 rows if
    provided (true→false transition), else None.
    """
    rows = _prepare_time_rows(rows, alignment)
    if m6_rows is not None:
        m6_rows = rows if m6_rows is rows else _prepare_time_rows(m6_rows, alignment)
    m4 = sorted(
        (r for r in rows if r.get("topic") == "/l3/m4/behavior_plan"),
        key=_event_time_s,
    )
    events = [{
        "t": _event_time_s(r),
        **(
            {"event_time": event_time}
            if (event_time := _event_time_payload(r)) is not None
            else {}
        ),
        "behavior": BEHAVIOR_INT_TO_STR.get(int(_value(r, "behavior", 0)), "UNKNOWN"),
        "closing_mps": _parse_closing_mps(r.get("rationale", "") or ""),
    } for r in m4]

    cleared_t: float | None = None
    if m6_rows is not None:
        m6_sorted = sorted(
            (r for r in m6_rows if r.get("topic") == "/l3/m6/colregs_constraint"),
            key=_event_time_s,
        )
        cleared_t = _stable_conflict_cleared_t(m6_sorted)
    return events, cleared_t


# ─── trace → M2 oracle input ──────────────────────────────────────────────

def _recompute_truth_cpa_from_sensor_inputs(
    m2_rows: list[dict[str, Any]],
) -> tuple[float, float] | None:
    """Recompute truth (dcpa_m, tcpa_s) from M2's OBSERVED sensor inputs.

    The oracle checks M2's *geometry computation* accuracy, not controller
    speed-tracking or scenario-vs-simulator placement error (Issue #4). The
    compiled truth CPA uses the YAML nominal own-sog and the YAML-declared
    target position, but:

    1. The controller tracks a slightly different own-sog (e.g. 11.3kn vs
       nominal 10.8kn); for near-radial encounters at long range a 0.5kn
       difference is amplified 100-150x in CPA.
    2. The simulator may place the target at a slightly different position
       than the YAML declares (observed ~250m discrepancy).

    Both effects make the compiled-truth CPA a poor reference. The correct
    reference is M2's OWN sensor inputs (bearing, range, both speeds/cogs) at
    the settle frame: CPA is M2's *calculation* on those inputs, so the oracle
    verifies M2's CPA formula matches the standard CPA formula on the same
    sensor inputs. This is non-circular (inputs are sensors, output is the
    computation under test) and isolates geometry-calculation errors.

    Returns (dcpa_m, tcpa_s) from the first settled M2 row, or None when the
    required sensor fields are absent (legacy trace).
    """
    if not m2_rows:
        return None
    # Use the same settle window as the estimate extraction (first row after
    # 30s) so truth and estimate reference the same encounter state.
    first_t = _event_time_s(m2_rows[0])
    settle_t = first_t + 30.0
    row = None
    for r in m2_rows:
        if _event_time_s(r) >= settle_t:
            row = r
            break
    if row is None:
        row = m2_rows[0]
    brg = row.get("primary_brg_deg")
    rng = row.get("primary_rng_m")
    own_sog = row.get("own_sog_kn")
    own_hdg = row.get("own_heading_deg")
    tgt_sog = row.get("primary_target_sog_kn")
    tgt_cog = row.get("primary_target_cog_deg")
    if None in (brg, rng, own_sog, own_hdg, tgt_sog, tgt_cog):
        return None
    try:
        te = float(rng) * math.sin(math.radians(float(brg)))
        tn = float(rng) * math.cos(math.radians(float(brg)))
        own_cog = float(own_hdg) % 360.0
        ovx = float(own_sog) * 0.514444 * math.sin(math.radians(own_cog))
        ovy = float(own_sog) * 0.514444 * math.cos(math.radians(own_cog))
        tvx = float(tgt_sog) * 0.514444 * math.sin(math.radians(float(tgt_cog)))
        tvy = float(tgt_sog) * 0.514444 * math.cos(math.radians(float(tgt_cog)))
        rvx, rvy = tvx - ovx, tvy - ovy
        rx, ry = te, tn
        vv = rvx * rvx + rvy * rvy
        tcpa = 0.0 if vv < 1e-9 else max(0.0, -((rx * rvx) + (ry * rvy)) / vv)
        dcpa = math.hypot(rx + rvx * tcpa, ry + rvy * tcpa)
        return dcpa, tcpa
    except (TypeError, ValueError):
        return None


def extract_m2_truth_and_estimate(
    compiled: dict[str, Any],
    *,
    m2_rows: list[dict[str, Any]],
    ownship_rows: list[dict[str, Any]] | None = None,
    alignment: ClockAlignment | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Build M2 truth (from compiled geometry) + estimate (from M2 trace).

    The estimate reads from observed M2 trace fields only (primary_* or
    target_* aliases).  Missing observed fields are recorded in the estimate's
    ``missing_fields`` list; the oracle MUST NOT substitute compiled truth
    for missing SUT output — missing observed fields produce RED.
    """
    m2_rows = _prepare_time_rows(m2_rows, alignment)
    if ownship_rows is not None:
        ownship_rows = _prepare_time_rows(ownship_rows, alignment)
    geom = compiled.get("geometry", {})
    # truth bearing: rel_bearing is starboard-positive; oracle expects signed.
    truth = {
        "bearing_deg": geom.get("rel_bearing_deg", 0.0),
        "cpa_m": geom.get("dcpa_m", 0.0),
        "tcpa_s": geom.get("tcpa_s", 0.0),
    }

    # Observed M2 trace fields — no fallback to compiled truth.
    M2_FIELD_ALIASES: dict[str, tuple[str, ...]] = {
        "bearing_deg": ("primary_relative_bearing_deg", "target_rel_bearing_deg"),
        "cpa_m": ("primary_cpa_m", "target_cpa_m"),
        "tcpa_s": ("primary_tcpa_s", "target_tcpa_s"),
    }

    def _first_observed_float(rows: list[dict[str, Any]], aliases: tuple[str, ...]) -> float | None:
        # Skip startup transient: the first ~30 s of M2 records reflect a
        # vessel still aligning to its route, producing inaccurate bearing
        # and TCPA. Use records at or after a 30 s settle window measured
        # from the first M2 record's event time.
        if rows:
            first_t = _event_time_s(rows[0])
            settle_t = first_t + 30.0
        else:
            settle_t = 0.0
        for row in rows:
            if _event_time_s(row) < settle_t:
                continue
            for key in aliases:
                v = row.get(key)
                if v is not None:
                    try:
                        return float(v)
                    except (TypeError, ValueError):
                        continue
        # Fallback: if no rows survived the settle window, use the first.
        for row in rows:
            for key in aliases:
                v = row.get(key)
                if v is not None:
                    try:
                        return float(v)
                    except (TypeError, ValueError):
                        continue
        return None

    def _first_observed_float_key(rows: list[dict[str, Any]], key: str) -> float | None:
        # Same settle window as _first_observed_float to avoid startup SOG
        # transient (ship still accelerating).
        if rows:
            first_t = _event_time_s(rows[0])
            settle_t = first_t + 30.0
        else:
            settle_t = 0.0
        for row in rows:
            if _event_time_s(row) < settle_t:
                continue
            v = row.get(key)
            if v is not None:
                try:
                    return float(v)
                except (TypeError, ValueError):
                    continue
        # Fallback
        for row in rows:
            v = row.get(key)
            if v is not None:
                try:
                    return float(v)
                except (TypeError, ValueError):
                    continue
        return None

    # Recompute truth CPA/TCPA from M2's OBSERVED sensor inputs (Issue #4).
    # The compiled truth uses the YAML nominal own-sog and the YAML-declared
    # target position, but the controller tracks a slightly different own-sog
    # and the simulator may place the target off from the YAML declaration.
    # For near-radial encounters these are amplified 100-150x in CPA, producing
    # a spurious MEASUREMENT_INCONSISTENT that reflects speed-tracking /
    # placement error, not M2 geometry-calculation error.
    #
    # The oracle's purpose (see comment above) is to check M2's CPA/TCPA
    # *geometry computation* accuracy. The correct reference is M2's own sensor
    # inputs (bearing, range, both speeds/cogs): CPA is M2's calculation on
    # those inputs, so recomputing from the same inputs isolates the geometry
    # formula. Falls back to the scalar TCPA scaling when sensor fields are
    # absent (legacy trace).
    sensor_cpa = _recompute_truth_cpa_from_sensor_inputs(m2_rows)
    if sensor_cpa is not None:
        truth["cpa_m"] = sensor_cpa[0]
        truth["tcpa_s"] = sensor_cpa[1]
    elif ownship_rows:
        observed_sog_mps = _first_observed_float_key(ownship_rows, "sog_mps")
        if observed_sog_mps is None:
            observed_sog_kn = _first_observed_float_key(ownship_rows, "sog_kn")
            if observed_sog_kn is not None:
                observed_sog_mps = observed_sog_kn * 0.514444
        nominal_sog_kn = compiled.get("own_sog_kn", 0.0)
        nominal_sog_mps = nominal_sog_kn * 0.514444
        if observed_sog_mps and nominal_sog_mps > 0 and truth["tcpa_s"] > 0:
            ratio = nominal_sog_mps / observed_sog_mps if observed_sog_mps > 0 else 1.0
            if 0.8 <= ratio <= 1.2:
                truth["tcpa_s"] = truth["tcpa_s"] * ratio

    estimate: dict[str, Any] = {}
    missing: list[str] = []
    for output_key, aliases in M2_FIELD_ALIASES.items():
        value = _first_observed_float(m2_rows, aliases)
        estimate[output_key] = value
        if value is None:
            missing.append(output_key)
    estimate["missing_fields"] = missing

    ownship_consistency = _m2_ownship_consistency(m2_rows, ownship_rows or [])
    if ownship_consistency:
        estimate.update(ownship_consistency)
    return truth, estimate


def _m2_ownship_consistency(
    m2_rows: list[dict[str, Any]],
    ownship_rows: list[dict[str, Any]],
    *,
    max_time_delta_s: float = 1.0,
    warmup_s: float = 60.0,
) -> dict[str, float]:
    m2_samples = sorted(
        (
            (
                _event_time_s(r),
                float(r["own_lat"]),
                float(r["own_lon"]),
            )
            for r in m2_rows
            if r.get("own_lat") is not None and r.get("own_lon") is not None
        ),
        key=lambda item: item[0],
    )
    own_samples = sorted(
        (
            (
                _event_time_s(r),
                float(r["lat"]),
                float(r["lon"]),
            )
            for r in ownship_rows
            if r.get("lat") is not None and r.get("lon") is not None
        ),
        key=lambda item: item[0],
    )
    if not m2_samples or not own_samples:
        return {}

    own_times = [sample[0] for sample in own_samples]
    max_err_m = 0.0
    max_err_t_s = None
    matched = False
    for m2_t, m2_lat, m2_lon in m2_samples:
        if m2_t < warmup_s:
            continue
        if abs(m2_lat) < 1e-9 and abs(m2_lon) < 1e-9:
            continue
        left = bisect.bisect_left(own_times, m2_t - max_time_delta_s)
        right = bisect.bisect_right(own_times, m2_t + max_time_delta_s)
        if left >= right:
            continue
        best: tuple[float, float] | None = None
        for own_t, own_lat, own_lon in own_samples[left:right]:
            if abs(own_lat) < 1e-9 and abs(own_lon) < 1e-9:
                continue
            dt = abs(own_t - m2_t)
            err = _latlon_distance_m(m2_lat, m2_lon, own_lat, own_lon)
            if best is None or (dt, err) < best:
                best = (dt, err)
        if best is None:
            continue
        matched = True
        err_m = best[1]
        if max_err_t_s is None or err_m > max_err_m:
            max_err_m = err_m
            max_err_t_s = m2_t

    if not matched or max_err_t_s is None:
        return {}
    return {
        "ownship_max_position_err_m": max_err_m,
        "ownship_position_err_t_s": max_err_t_s,
    }


def _latlon_distance_m(
    lat_a: float,
    lon_a: float,
    lat_b: float,
    lon_b: float,
) -> float:
    ref_lat = 0.5 * (lat_a + lat_b)
    north_m = (lat_a - lat_b) * 111120.0
    east_m = (lon_a - lon_b) * 111120.0 * math.cos(math.radians(ref_lat))
    return math.hypot(north_m, east_m)


# ─── trace → M1 oracle input ──────────────────────────────────────────────

# M1 envelope_state enum (ODDState.msg): IN=0, EDGE=1, OUT=2, MRC_PREP=3,
# MRC_ACTIVE=4. Mirrors the oracle constants; kept here for trace extraction.
_M1_ENVELOPE_EDGE = 1
_M1_ENVELOPE_OUT = 2
_M1_MRC_STATES = (3, 4)


def extract_m1_output(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M1 oracle input from /l3/odd_state trace rows.

    M1 is the sole safety-context authority; this derives the envelope-state
    sequence, stale-input degradation, recovery, and MRC-override evidence
    purely from observed M1 trace rows (no invented safety truth). Missing
    observed fields are left absent so the oracle reports the gap honestly.
    """
    rows = _prepare_time_rows(rows, alignment)
    m1 = sorted(
        (r for r in rows if r.get("topic") == "/l3/odd_state"),
        key=_event_time_s,
    )
    if not m1:
        return {}

    envelope_seq = [
        int(_value(r, "envelope_state", -1)) for r in m1
        if _value(r, "envelope_state") is not None
    ]
    conformance_seq = [
        float(_value(r, "conformance_score")) for r in m1
        if _value(r, "conformance_score") is not None
    ]

    # Stale-input degradation: a sample is "stale" when the rationale/zone_reason
    # flags M2 staleness (design §3.6). We look for the explicit marker; the
    # conformance score must fall across a stale->non-stale recovery boundary.
    stale_input_count = 0
    score_degraded_on_stale = False
    prev_score: float | None = None
    for r in m1:
        reason = str(_value(r, "zone_reason", "") or _value(r, "rationale", "") or "")
        score = _value(r, "conformance_score")
        if isinstance(score, (int, float)):
            score = float(score)
        else:
            score = None
        is_stale = "stale" in reason.lower()
        if is_stale:
            stale_input_count += 1
        if is_stale and prev_score is not None and score is not None and score < prev_score:
            score_degraded_on_stale = True
        if score is not None:
            prev_score = score

    # Recovery monotonicity: did the sequence contain OUT->EDGE?
    recovered_to_edge: bool | None = None
    for prev, curr in zip(envelope_seq, envelope_seq[1:]):
        if prev == _M1_ENVELOPE_OUT and curr == _M1_ENVELOPE_EDGE:
            recovered_to_edge = True
            break
        if prev == _M1_ENVELOPE_OUT and curr == 0:  # OUT->IN skipped EDGE
            recovered_to_edge = False
            break

    mrc_override_active = any(s in _M1_MRC_STATES for s in envelope_seq)

    return {
        "envelope_seq": envelope_seq,
        "conformance_score_seq": conformance_seq,
        "stale_input_count": stale_input_count,
        "score_degraded_on_stale": score_degraded_on_stale,
        "recovered_to_edge": recovered_to_edge,
        "mrc_override_active": mrc_override_active,
    }


# ─── trace → M3 oracle input ──────────────────────────────────────────────

def extract_m3_output(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M3 oracle input from /l3/mission_goal trace rows.

    M3 is local mission tracking + replanning trigger; this derives the FSM
    state sequence, task-validity sub-state, duplicate-route rejection, and
    reset-to-idle evidence purely from observed M3 trace rows.
    """
    rows = _prepare_time_rows(rows, alignment)
    m3 = sorted(
        (r for r in rows if r.get("topic") == "/l3/mission_goal"),
        key=_event_time_s,
    )
    if not m3:
        return {}

    fsm_seq = [
        int(_value(r, "fsm_state", -1)) for r in m3
        if _value(r, "fsm_state") is not None
    ]
    task_seq = [
        int(_value(r, "task_validity", -1)) for r in m3
        if _value(r, "task_validity") is not None
    ]

    # Duplicate planned-route rejection: a second identical route should not
    # re-advance the FSM. We cannot see the planned-route stream here directly,
    # so we expose whether the rationale/decision_json marks a duplicate reject.
    duplicate_route_rejected: bool | None = None
    for r in rows:
        if r.get("topic") != "/l3/asdr/record":
            continue
        if r.get("source_module") != "M3_Mission_Manager":
            continue
        payload = _decision_json(r)
        reason = str(payload.get("reason", "") or r.get("decision_type", "") or "")
        if "duplicate" in reason.lower():
            duplicate_route_rejected = True
            break
        if "rejected" in reason.lower() and "route" in reason.lower():
            duplicate_route_rejected = True
            break

    # Reset returned to IDLE: an explicit reset decision followed by an IDLE FSM.
    reset_returned_to_idle: bool | None = None
    has_reset_decision = any(
        r.get("topic") == "/l3/asdr/record"
        and r.get("source_module") == "M3_Mission_Manager"
        and "reset" in str(r.get("decision_type", "")).lower()
        for r in rows
    )
    if has_reset_decision:
        reset_returned_to_idle = bool(fsm_seq and fsm_seq[-1] == 1)

    return {
        "fsm_seq": fsm_seq,
        "task_validity_seq": task_seq,
        "duplicate_route_rejected": duplicate_route_rejected,
        "reset_returned_to_idle": reset_returned_to_idle,
    }


# ─── trace → M5 oracle input ──────────────────────────────────────────────

def _extract_m5_plan_output(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M5 oracle input from /l3/m5/avoidance_plan trace rows."""
    rows = _prepare_time_rows(rows, alignment)
    m5 = [r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan"]
    recovery_t = None
    recovery_event: EventTime | None = None
    for r in sorted(
        (r for r in rows if r.get("topic") == "/l3/m4/behavior_plan"),
        key=_event_time_s,
    ):
        if int(_value(r, "behavior", 0)) == 7:
            recovery_t = _event_time_s(r)
            recovery_event = _event_time(r)
            break
    if not m5:
        output = {
            "solver_status": "EMPTY",
            "n_waypoints": 0,
            "oscillation_count": 0,
            "valid_route_count": 0,
            "valid_plan_ids": [],
            "invalid_route_reasons": [],
        }
        if alignment is not None:
            output["first_recovery"] = (
                recovery_event.as_dict() if recovery_event is not None else None
            )
        return _attach_event_times(output, m5, alignment)
    statuses = [str(_value(r, "solver_status", _value(r, "status", "EMPTY")))
                for r in m5]
    # Strict M5 validity (Task 3 Step 4): a route counts as valid only when it
    # is committed, complete, preflight-proven, has >=2 waypoints, and carries a
    # non-empty plan_id. No n_wp=1 synthesis: an empty/degraded plan stays 0.
    valid_route_count = 0
    valid_plan_ids: list[str] = []
    invalid_route_reasons: list[str] = []
    for r, status in zip(m5, statuses):
        reasons = _m5_route_invalid_reasons(r)
        if reasons:
            invalid_route_reasons.extend(reasons)
            continue
        valid_route_count += 1
        pid = str(_value(r, "plan_id", ""))
        if pid:
            valid_plan_ids.append(pid)
    # Oscillation: count of EMPTY→VALID transitions (each is a re-plan churn).
    osc = 0
    prev = None
    for s in statuses:
        if prev == "EMPTY" and s == "VALID":
            osc += 1
        if s in ("VALID", "EMPTY"):
            prev = s
    # n_waypoints: take the max declared count among strictly-valid routes only.
    # Never synthesize n_wp=1; a plan with no waypoints is empty.
    n_wp = 0
    for r, status in zip(m5, statuses):
        if _m5_route_invalid_reasons(r):
            continue
        nw = _m5_route_waypoint_count(r)
        if nw > n_wp:
            n_wp = nw
    dom_status = "VALID" if valid_route_count > 0 else "EMPTY"
    recovery_rows = [
        r for r in m5
        if recovery_t is not None and _event_time_s(r) >= recovery_t
    ]
    corridor_in_recovery_count = sum(1 for r in recovery_rows if _commit_branch(r) == 2)
    recovery_publish_count = sum(1 for r in recovery_rows if _commit_branch(r) == 3)
    bcmpc_follow_rows = [
        r for r in m5
        if _commit_branch(r) == 5 or str(_value(r, "plan_id", "")) == "m5_bcmpc_follow"
    ]
    first_bcmpc_t = min(
        (_event_time_s(r) for r in bcmpc_follow_rows),
        default=None,
    )
    reactive_override_count = 0
    for r in rows:
        if first_bcmpc_t is not None and _event_time_s(r) < first_bcmpc_t:
            continue
        if r.get("topic") == "/l3/m5/reactive_override_cmd":
            reactive_override_count += 1
            continue
        if (
            r.get("topic") == "/l3/asdr/record"
            and r.get("source_module") == "M5_BC_MPC"
            and r.get("decision_type") == "reactive_override"
        ):
            reactive_override_count += 1
    recovery_rejected_count = 0
    for r in rows:
        if recovery_t is not None and _event_time_s(r) < recovery_t:
            continue
        if r.get("topic") != "/l3/asdr/record":
            continue
        if r.get("source_module") != "M5_Tactical_Planner":
            continue
        if r.get("decision_type") != "committed_route_rejected":
            continue
        try:
            payload = json.loads(str(r.get("decision_json", "{}")))
        except json.JSONDecodeError:
            payload = {}
        reason = str(payload.get("reason", ""))
        if reason.startswith("recovery_") or reason.startswith("return_"):
            recovery_rejected_count += 1
    gnc_accepted_recovery_count = 0
    for r in rows:
        if r.get("topic") != "/l3/gnc/execution_status":
            continue
        plan_id = str(_value(r, "plan_id", ""))
        active_route_id = str(_value(r, "active_route_id", ""))
        route_id = active_route_id or plan_id
        if (
            route_id.startswith("m5-return")
            and bool(_value(r, "accepted", False))
            and bool(_value(r, "executing", True))
        ):
            gnc_accepted_recovery_count += 1
    output = {"solver_status": dom_status, "n_waypoints": n_wp,
              "oscillation_count": osc,
              "valid_route_count": valid_route_count,
              "valid_plan_ids": valid_plan_ids,
              "invalid_route_reasons": invalid_route_reasons,
              "m4_recovery_seen": recovery_t is not None,
              "corridor_in_recovery_count": corridor_in_recovery_count,
              "recovery_rejected_count": recovery_rejected_count,
              "recovery_publish_count": recovery_publish_count,
              "gnc_accepted_recovery_count": gnc_accepted_recovery_count,
              "bcmpc_follow_count": len(bcmpc_follow_rows),
              "reactive_override_after_bcmpc_count": reactive_override_count}
    if alignment is not None:
        output["first_recovery"] = (
            recovery_event.as_dict() if recovery_event is not None else None
        )
    return _attach_event_times(output, m5, alignment)


def extract_m5_plan_output_strict(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """G1 M5 extraction with typed selected-time provenance."""
    return _extract_m5_plan_output(
        rows,
        alignment=_require_alignment(alignment, rows),
    )


def extract_m5_plan_output(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Legacy M5 extraction retained for non-G1 callers."""
    return _extract_m5_plan_output(rows, alignment=None)


def _m5_route_waypoint_count(row: dict[str, Any]) -> int:
    for key in ("n_waypoints", "waypoint_count"):
        value = row.get(key)
        if isinstance(value, int):
            return value
        if isinstance(value, float):
            return int(value)
    lat = row.get("latitude")
    if isinstance(lat, list):
        return len(lat)
    return 0


def _m5_route_arrays_complete(row: dict[str, Any]) -> bool:
    n_waypoints = _m5_route_waypoint_count(row)
    arrays = [
        row.get("latitude"),
        row.get("longitude"),
        row.get("command_speed_mps"),
        row.get("navigation_mode"),
        row.get("segment_source"),
    ]
    if all(isinstance(value, list) for value in arrays):
        lengths = {len(value) for value in arrays}
        return len(lengths) == 1 and next(iter(lengths)) >= 2
    segment_source_count = row.get("segment_source_count")
    if isinstance(segment_source_count, (int, float)):
        return n_waypoints >= 2 and int(segment_source_count) == n_waypoints
    return n_waypoints >= 2 and not any(isinstance(value, list) for value in arrays)


def _m5_route_preflight_proven(row: dict[str, Any]) -> bool:
    text = " ".join(str(row.get(key, "")) for key in ("rationale", "reason")).lower()
    return (
        "gnc_preflight=feasible" in text
        or "gnc_nominal_handoff_staged_lateral_delta<=" in text
        or str(row.get("gnc_preflight", "")).lower() in {"feasible", "pass", "passed"}
        or bool(row.get("preflight_feasible", False))
    )


def _m5_route_invalid_reasons(row: dict[str, Any]) -> list[str]:
    """Strict M5 validity check (Task 3 Step 4).

    A route is valid only when ALL hold:
      - committed exec branch (1/2/3)
      - complete route arrays
      - preflight proven
      - >=2 waypoints
      - non-empty plan_id

    Returns a list of human-readable reasons why the row is NOT a valid route
    (empty list means valid). This is the single source of truth for validity;
    ``extract_m5_plan_output`` and ``extract_first_m5_executable_route_t`` both
    build on the underlying predicates, and this helper surfaces every failing
    predicate so the oracle can report *why* a route was rejected.
    """
    reasons: list[str] = []
    branch = _commit_branch(row)
    if branch not in {1, 2, 3}:
        reasons.append(f"commit_branch_not_exec:{branch}")
    if not _m5_route_arrays_complete(row):
        reasons.append("route_arrays_incomplete")
    if not _m5_route_preflight_proven(row):
        reasons.append("preflight_not_proven")
    if _m5_route_waypoint_count(row) < 2:
        reasons.append("waypoint_count_lt_2")
    if not str(row.get("plan_id") or ""):
        reasons.append("plan_id_empty")
    return reasons


def _extract_first_m5_executable_route_t_legacy(
    rows: list[dict[str, Any]],
    *,
    command_t: float,
    release_t: float | None = None,
) -> float | None:
    """First M5 route that can be handed to GNC after M4 requests avoidance."""
    rows = _prepare_time_rows(rows, None)
    for row in sorted(
        (r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan"),
        key=_event_time_s,
    ):
        t_s = _event_time_s(row)
        if t_s < command_t:
            continue
        if release_t is not None and t_s > release_t:
            break
        if _commit_branch(row) not in {1, 2, 3}:
            continue
        if not _m5_route_arrays_complete(row):
            continue
        if not _m5_route_preflight_proven(row):
            continue
        return t_s
    return None


def extract_first_m5_executable_route_t(
    rows: list[dict[str, Any]],
    *,
    command_t: float,
    release_t: float | None = None,
) -> float | None:
    """Legacy scalar-time M5 handoff search for non-G1 callers."""
    return _extract_first_m5_executable_route_t_legacy(
        rows,
        command_t=command_t,
        release_t=release_t,
    )


def extract_first_m5_executable_route_time_strict(
    rows: list[dict[str, Any]],
    *,
    command_time: EventTime,
    command_record_id: str | None = None,
    release_time: EventTime | None = None,
    release_record_id: str | None = None,
    alignment: ClockAlignment | None = None,
) -> EventTime | None:
    """Return typed time for first executable M5 route in a G1 window."""
    strict_alignment = _require_alignment(alignment, rows)
    prepared = _prepare_time_rows(rows, strict_alignment)
    command = _validated_source_boundary_time(
        command_time,
        prepared,
        strict_alignment,
        record_id=command_record_id,
        topic="/l3/m4/behavior_plan",
        boundary_name="m4-command",
    )
    release = (
        _validated_source_boundary_time(
            release_time,
            prepared,
            strict_alignment,
            record_id=release_record_id,
            topic="/l3/m6/colregs_constraint",
            boundary_name="m6-release",
        )
        if release_time is not None
        else None
    )
    for row in sorted(
        (r for r in prepared if r.get("topic") == "/l3/m5/avoidance_plan"),
        key=_event_time_s,
    ):
        selected = _event_time(row)
        if selected is None:
            raise EventTimeSelectionError(
                str(row.get("record_id", "<unknown>")),
                None,
                strict_alignment.alignment_id,
                strict_alignment.uncertainty_s,
                "clock_missing",
            )
        if selected.canonical_s < command.canonical_s:
            continue
        if release is not None and selected.canonical_s > release.canonical_s:
            break
        if _m5_route_invalid_reasons(row):
            continue
        return selected
    return None


# ─── trace → L4 oracle input ──────────────────────────────────────────────

def _extract_l4_actuation_core(
    rows: list[dict[str, Any]],
    *,
    command_t: float,
    release_t: float | None = None,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract L4 actuation timing from the trace.

    realized_heading_change_deg = max heading excursion from the pre-command
    heading within the avoidance window (command_t..release_t or end). This is
    the true maneuver magnitude, not the first 5deg jitter sample.
    first_realized_t = sim_t at which the heading first reaches >=50% of that
    max excursion (the maneuver is materially underway).

    For GNC profile traces, also extract the L4 handoff contract from
    /l3/gnc/execution_status: GNC must accept a collision-avoidance route
    promptly. The ship may turn slowly because GNC tracks waypoints and M5
    intentionally emits a smooth corridor.
    """
    rows = _prepare_time_rows(rows, alignment)
    own = sorted(
        (r for r in rows if r.get("topic") == "/sil/own_ship_state"),
        key=_event_time_s,
    )
    pre = [r for r in own if _event_time_s(r) <= command_t]
    base_hdg = float(_value(pre[-1], "heading_deg", 0.0)) if pre else 0.0

    window_end = release_t if release_t is not None else float("inf")
    window = [r for r in own
              if command_t <= _event_time_s(r) <= window_end]
    if not window:
        window = [r for r in own if _event_time_s(r) >= command_t]

    def _dev(r):
        hdg = float(_value(r, "heading_deg", base_hdg))
        return abs(((hdg - base_hdg + 180.0) % 360.0) - 180.0)

    if not window:
        return {"first_command_t": command_t, "first_realized_t": command_t,
                "realized_heading_change_deg": 0.0}

    max_dev = max(_dev(r) for r in window)
    threshold = max(5.0, 0.5 * max_dev)
    realized_t = command_t
    for r in window:
        if _dev(r) >= threshold:
            realized_t = _event_time_s(r)
            break
    result = {
        "first_command_t": command_t,
        "first_realized_t": realized_t,
        "realized_heading_change_deg": round(max_dev, 3),
    }
    gnc_status = sorted(
        (r for r in rows if r.get("topic") == "/l3/gnc/execution_status"),
        key=_event_time_s,
    )
    if not gnc_status:
        return result

    def _is_avoidance_route(r: dict[str, Any]) -> bool:
        source = str(_value(r, "command_source", "") or "")
        active_route = str(_value(r, "active_route_id", "") or "")
        plan_id = str(_value(r, "plan_id", "") or "")
        return (
            source == "collision_avoidance"
            or active_route.startswith(("m5-colregs-", "m5-return-", "m5-midmpc-"))
            or plan_id.startswith(("m5-colregs-", "m5-return-", "m5-midmpc-"))
        )

    def _accepted(r: dict[str, Any]) -> bool:
        state = str(_value(r, "execution_state", "") or "").upper()
        return bool(_value(r, "accepted", False)) or state == "ACCEPTED"

    def _point_count(r: dict[str, Any]) -> int:
        for key in ("waypoint_count", "internal_waypoint_count", "pose_count", "n_waypoints"):
            try:
                value = int(_value(r, key, 0))
            except (TypeError, ValueError):
                value = 0
            if value > 0:
                return value
        points = r.get("points")
        if isinstance(points, list):
            return len(points)
        return 0

    def _route_status_accepted(r: dict[str, Any]) -> bool:
        status = str(_value(r, "status", "") or "").upper()
        return bool(_value(r, "accepted", False)) or status in {
            "ACCEPTED",
            "EXECUTING",
            "EXECUTING_WITH_LIMIT",
        }

    status_end = release_t if release_t is not None else float("inf")
    active_routes = sorted(
        (
            r for r in rows
            if r.get("topic") == "/gnc/active_route"
            and command_t <= _event_time_s(r) <= status_end
        ),
        key=_event_time_s,
    )
    route_status_rows = sorted(
        (
            r for r in rows
            if str(r.get("topic", "")).endswith("route_plan_status")
            and command_t <= _event_time_s(r) <= status_end
        ),
        key=_event_time_s,
    )
    ship_waypoints = sorted(
        (
            r for r in rows
            if r.get("topic") == "/ship/waypoints"
            and command_t <= _event_time_s(r) <= status_end
        ),
        key=_event_time_s,
    )

    def _handoff_proof(route_id: str) -> dict[str, Any]:
        matching_active_route = next(
            (
                r for r in active_routes
                if str(_value(r, "route_id", "") or "") == route_id
                and _point_count(r) >= 2
            ),
            None,
        )
        matching_route_status = next(
            (
                r for r in route_status_rows
                if str(_value(r, "route_id", "") or "") == route_id
                and _route_status_accepted(r)
                and _point_count(r) >= 2
            ),
            None,
        )
        matching_waypoints = next(
            (r for r in ship_waypoints if _point_count(r) >= 2),
            None,
        )
        missing: list[str] = []
        if matching_active_route is None:
            missing.append("active_route")
        if matching_route_status is None:
            missing.append("route_plan_status")
        if matching_waypoints is None:
            missing.append("ship_waypoints")
        return {
            "proven": not missing,
            "missing": missing,
            "active_route_t": (
                _event_time_s(matching_active_route)
                if matching_active_route is not None else None
            ),
            "route_status_t": (
                _event_time_s(matching_route_status)
                if matching_route_status is not None else None
            ),
            "ship_waypoints_t": (
                _event_time_s(matching_waypoints)
                if matching_waypoints is not None else None
            ),
        }

    status_end = release_t if release_t is not None else float("inf")
    accepted_rows = [
        r for r in gnc_status
        if command_t <= _event_time_s(r) <= status_end
        and _is_avoidance_route(r)
        and _accepted(r)
    ]
    result["route_accepted"] = False
    result["route_identity_proven"] = False
    result["first_accepted_t"] = None
    result["accepted_plan_id"] = ""
    result["handoff_missing_proofs"] = []
    first_status = accepted_rows[0] if accepted_rows else None
    if first_status is not None:
        route_id = str(
            _value(first_status, "active_route_id", "")
            or _value(first_status, "plan_id", "")
            or ""
        )
        result["first_accepted_t"] = _event_time_s(first_status)
        result["accepted_plan_id"] = route_id
        proof = _handoff_proof(route_id)
        result["route_identity_proven"] = proof["proven"]
        result["handoff_missing_proofs"] = proof["missing"]
        if proof["proven"]:
            result["route_accepted"] = True
            result["active_route_t"] = proof["active_route_t"]
            result["route_status_t"] = proof["route_status_t"]
            result["ship_waypoints_t"] = proof["ship_waypoints_t"]
    return result


def extract_l4_actuation(rows: list[dict[str, Any]], *, command_t: float,
                         release_t: float | None = None) -> dict[str, Any]:
    """Extract L4 actuation timing from the trace.

    realized_heading_change_deg = max heading excursion from the pre-command
    heading within the avoidance window (command_t..release_t or end). This is
    the true maneuver magnitude, not the first 5deg jitter sample.
    first_realized_t = sim_t at which the heading first reaches >=50% of that
    max excursion (the maneuver is materially underway).

    For GNC profile traces, also extract the L4 handoff contract from
    /l3/gnc/execution_status: GNC must accept a collision-avoidance route
    promptly. The ship may turn slowly because GNC tracks waypoints and M5
    intentionally emits a smooth corridor.

    NOTE (m5-design-grounding graft, 2026-07-19): the colregs branch rewrote
    this as a thin wrapper over ``_extract_l4_actuation_core`` (strict
    EventTime path + route-identity provenance). That rewrite changed the
    route_accepted semantics in a way that breaks the m5 L4 oracle contract
    test (test_colregs_l4_oracle.py). This branch keeps the m5 standalone
    scalar-time derivation for the legacy call sites; the strict path
    (``extract_l4_actuation_strict``) is preserved unchanged for G1 callers.
    fast_evaluator does not consume this function (it reads L4/GNC rows
    directly via ``_evaluate_l4_handoff``), so this divergence is invisible
    to the fast verdict path.
    """
    own = sorted(
        (r for r in rows if r.get("topic") == "/sil/own_ship_state"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    pre = [r for r in own if float(r.get("sim_t", 0.0)) <= command_t]
    base_hdg = float(_value(pre[-1], "heading_deg", 0.0)) if pre else 0.0

    window_end = release_t if release_t is not None else float("inf")
    window = [r for r in own
              if command_t <= float(r.get("sim_t", 0.0)) <= window_end]
    if not window:
        window = [r for r in own if float(r.get("sim_t", 0.0)) >= command_t]

    def _dev(r):
        hdg = float(_value(r, "heading_deg", base_hdg))
        return abs(((hdg - base_hdg + 180.0) % 360.0) - 180.0)

    if not window:
        return {"first_command_t": command_t, "first_realized_t": command_t,
                "realized_heading_change_deg": 0.0}

    max_dev = max(_dev(r) for r in window)
    threshold = max(5.0, 0.5 * max_dev)
    realized_t = command_t
    for r in window:
        if _dev(r) >= threshold:
            realized_t = float(r.get("sim_t", command_t))
            break
    result = {
        "first_command_t": command_t,
        "first_realized_t": realized_t,
        "realized_heading_change_deg": round(max_dev, 3),
    }
    gnc_status = sorted(
        (r for r in rows if r.get("topic") == "/l3/gnc/execution_status"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    if not gnc_status:
        return result

    def _is_avoidance_route(r: dict[str, Any]) -> bool:
        source = str(_value(r, "command_source", "") or "")
        active_route = str(_value(r, "active_route_id", "") or "")
        plan_id = str(_value(r, "plan_id", "") or "")
        return (
            source == "collision_avoidance"
            or active_route.startswith(("m5-colregs-", "m5-return-"))
            or plan_id.startswith(("m5-colregs-", "m5-return-"))
        )

    def _accepted(r: dict[str, Any]) -> bool:
        state = str(_value(r, "execution_state", "") or "").upper()
        return bool(_value(r, "accepted", False)) or state == "ACCEPTED"

    status_end = release_t if release_t is not None else float("inf")
    accepted_rows = [
        r for r in gnc_status
        if command_t <= float(r.get("sim_t", 0.0)) <= status_end
        and _is_avoidance_route(r)
        and _accepted(r)
    ]
    first_accepted = accepted_rows[0] if accepted_rows else None
    result["route_accepted"] = first_accepted is not None
    if first_accepted is not None:
        result["first_accepted_t"] = float(first_accepted.get("sim_t", command_t))
        result["accepted_plan_id"] = _value(first_accepted, "plan_id", "")
    return result


def extract_l4_actuation_strict(
    rows: list[dict[str, Any]],
    *,
    command_time: EventTime,
    command_record_id: str | None = None,
    release_time: EventTime | None = None,
    release_record_id: str | None = None,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """G1 L4 extraction with typed boundaries and typed time outputs."""
    strict_alignment = _require_alignment(alignment, rows)
    prepared = _prepare_time_rows(rows, strict_alignment)
    command = _validated_source_boundary_time(
        command_time,
        prepared,
        strict_alignment,
        record_id=command_record_id,
        topic="/l3/m4/behavior_plan",
        boundary_name="m4-command",
    )
    release = (
        _validated_source_boundary_time(
            release_time,
            prepared,
            strict_alignment,
            record_id=release_record_id,
            topic="/l3/m6/colregs_constraint",
            boundary_name="m6-release",
        )
        if release_time is not None
        else None
    )
    scalar = _extract_l4_actuation_core(
        prepared,
        command_t=command.canonical_s,
        release_t=release.canonical_s if release is not None else None,
        alignment=strict_alignment,
    )

    def _typed_derived_time(
        key: str,
        predicate,
        *,
        fallback: EventTime | None = None,
    ) -> dict[str, float | str] | None:
        value = scalar.get(key)
        if value is None:
            return None
        canonical_s = float(value)
        for row in prepared:
            selected = _event_time(row)
            if (
                predicate(row)
                and selected is not None
                and math.isclose(
                    selected.canonical_s,
                    canonical_s,
                    rel_tol=0.0,
                    abs_tol=1.0e-12,
                )
            ):
                return selected.as_dict()
        if fallback is not None and math.isclose(
            fallback.canonical_s,
            canonical_s,
            rel_tol=0.0,
            abs_tol=1.0e-12,
        ):
            return fallback.as_dict()
        raise EventTimeSelectionError(
            "<derived-l4-time>",
            None,
            strict_alignment.alignment_id,
            strict_alignment.uncertainty_s,
            "derived_event_time_provenance_missing",
        )

    strict_output = {
        key: value
        for key, value in scalar.items()
        if key
        not in {
            "first_command_t",
            "first_realized_t",
            "first_accepted_t",
            "active_route_t",
            "route_status_t",
            "ship_waypoints_t",
        }
    }
    strict_output["first_command"] = command.as_dict()
    ownship_observed = any(
        row.get("topic") == "/sil/own_ship_state"
        and (selected := _event_time(row)) is not None
        and selected.canonical_s >= command.canonical_s
        and (release is None or selected.canonical_s <= release.canonical_s)
        for row in prepared
    )
    strict_output["first_realized"] = (
        _typed_derived_time(
            "first_realized_t",
            lambda row: row.get("topic") == "/sil/own_ship_state",
        )
        if ownship_observed
        else None
    )
    if release is not None:
        strict_output["release"] = release.as_dict()
    if "first_accepted_t" in scalar:
        strict_output["first_accepted"] = _typed_derived_time(
            "first_accepted_t",
            lambda row: row.get("topic") == "/l3/gnc/execution_status",
        )
    if "active_route_t" in scalar:
        strict_output["active_route"] = _typed_derived_time(
            "active_route_t",
            lambda row: row.get("topic") == "/gnc/active_route",
        )
    if "route_status_t" in scalar:
        strict_output["route_status"] = _typed_derived_time(
            "route_status_t",
            lambda row: str(row.get("topic", "")).endswith("route_plan_status"),
        )
    if "ship_waypoints_t" in scalar:
        strict_output["ship_waypoints"] = _typed_derived_time(
            "ship_waypoints_t",
            lambda row: row.get("topic") == "/ship/waypoints",
        )
    strict_output["event_times"] = _event_time_evidence(prepared)
    return strict_output


# ─── trace → M7 oracle input ──────────────────────────────────────────────

# Severity threshold above which a SafetyAlert is an MRC-required request.
# SafetyAlert severity (SafetyAlert.msg): SEVERITY_INFO=0, SEVERITY_WARNING=1,
# SEVERITY_CRITICAL=2, SEVERITY_MRC_REQUIRED=3. An alert carrying a non-empty
# canonical recommended_mrm at or above this severity is the M7 MRC request
# the M1 authority must answer.
MRC_REQUIRED_SEVERITY = 3

_M7_SAFETY_ALERT_TOPIC = "/l3/m7/safety_alert"
_M1_MRM_COMMAND_TOPIC = "/l3/m1/mrm_command"
_M1_MRM_EXEC_STATUS_TOPIC = "/l3/m1/mrm_execution_status"
_M4_BEHAVIOR_PLAN_TOPIC = "/l3/m4/behavior_plan"
_M5_OVERRIDE_TOPIC = "/l3/m5/reactive_override_cmd"
_CHECKER_VETO_TOPIC = "/l3/checker/veto"


def _is_mrm_recommendation(text: Any) -> bool:
    """True when a SafetyAlert.recommended_mrm is a real canonical MRM id."""
    from tools.sil.colregs_module_oracle import _canonical_mrm_id

    return bool(_canonical_mrm_id(str(text or "")))


def extract_m7_veto(
    rows: list[dict[str, Any]],
    *,
    unsafe_trajectory_present: bool = False,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M7 checker evidence from /l3/m7/* and /l3/checker/veto trace.

    Returns the evidence dict consumed by ``evaluate_m7_oracle`` (Task 11
    stage-separated checker role). M7 is the checker layer: it surfaces
    hazards via SafetyAlert recommendations and vetoes unsafe commands. It
    never publishes an executable command.

    Keys produced:
      command_without_recommendation: an M1 MRM command was published but no
          M7 SafetyAlert recommendation (MRC request) overlapped its window.
      safe_command_wrongly_vetoed: a checker veto targeted a command that was
          itself safe (no preceding hazard alert and no breach).
      m2_m7_no_action_cpa_inconsistent / physical_separation_breach_unhandled
          / hard_constraint_cadence_missing: reserved flags (False here);
          populated by richer extractors when the trace carries the evidence.
      unsafe_trajectory_present / unsafe_trajectory_vetoed / veto_count /
          safe_trajectory_vetoed: retained for backward compatibility with the
          offline runner (run_colregs_module_oracle.py).
    """
    rows = _prepare_time_rows(rows, alignment)
    alert_rows = sorted(
        (r for r in rows if r.get("topic") == _M7_SAFETY_ALERT_TOPIC),
        key=_event_time_s,
    )
    command_rows = sorted(
        (r for r in rows if r.get("topic") == _M1_MRM_COMMAND_TOPIC),
        key=_event_time_s,
    )
    veto_rows = [
        r for r in rows
        if str(r.get("topic", "")).lower() == _CHECKER_VETO_TOPIC
        or ("m7" in str(r.get("topic", "")) and "veto" in str(r.get("topic", "")).lower())
    ]

    # An M7 MRC request is a SafetyAlert with a real canonical recommended_mrm
    # at/above the MRC severity threshold.
    request_times = [
        _event_time_s(r) for r in alert_rows
        if _is_mrm_recommendation(_value(r, "recommended_mrm", ""))
        and int(_value(r, "severity", 0)) >= MRC_REQUIRED_SEVERITY
    ]
    request_present = bool(request_times)

    # command_without_recommendation: an executable command exists with no
    # overlapping MRC request. A request at or before the first command
    # (within the command validity window) counts as coverage.
    command_without_recommendation = False
    if command_rows:
        first_cmd_t = _event_time_s(command_rows[0])
        last_cmd_t = _event_time_s(command_rows[-1])
        validity_s = float(_value(command_rows[-1], "validity_s", 0.0) or 0.0)
        window_start = first_cmd_t - max(validity_s, 0.0)
        covered = any(window_start <= rt <= last_cmd_t + 1.0 for rt in request_times)
        command_without_recommendation = not covered

    # safe_command_wrongly_vetoed: a checker veto exists but no hazard alert
    # (no MRC request and no lower-severity alert) preceded it. Without a hazard
    # to respond to, the veto is a false positive.
    safe_command_wrongly_vetoed = False
    if veto_rows and not alert_rows:
        safe_command_wrongly_vetoed = True

    return {
        "command_without_recommendation": command_without_recommendation,
        "safe_command_wrongly_vetoed": safe_command_wrongly_vetoed,
        "m2_m7_no_action_cpa_inconsistent": False,
        "physical_separation_breach_unhandled": False,
        "hard_constraint_cadence_missing": False,
        "request_present": request_present,
        # Backward-compatible veto summary for the offline runner.
        "unsafe_trajectory_vetoed": len(veto_rows) > 0,
        "safe_trajectory_vetoed": safe_command_wrongly_vetoed,
        "veto_count": len(veto_rows),
        "unsafe_trajectory_present": unsafe_trajectory_present,
    }


# ─── trace → M1 MRM-authority / L4-GNC execution evidence (Task 11) ────────


def _is_executable_mrm_command_row(row: dict[str, Any]) -> bool:
    """True when a trace row carries an executable MRMCommand payload.

    ``trigger_alert_key`` is unique to MRMCommand.msg — no other traced topic
    (SafetyAlert, MRMExecutionStatus, BehaviorPlan, …) emits it — so a row that
    carries a non-empty ``trigger_alert_key`` is recognizably an executable MRM
    command, regardless of which topic it surfaced on. This lets the
    M1-sole-publisher check use topic *provenance* rather than a message field
    MRMCommand.msg does not carry (the prior ``publisher``/``command_source``
    AND-logic was dead code).
    """
    return bool(str(_value(row, "trigger_alert_key", "")).strip())


def _latest_command(rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    cmds = sorted(
        (r for r in rows if r.get("topic") == _M1_MRM_COMMAND_TOPIC),
        key=_event_time_s,
    )
    return cmds[-1] if cmds else None


def extract_m1_mrm_authority(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M1 MRM-authority evidence from the M7/M1 trace rows.

    M1 is the SOLE executable-MRM publisher. This derives the authority-role
    evidence the ``evaluate_m1_mrm_authority_oracle`` consumes:
      mrc_requested / command_published / non_m1_publisher /
      command_not_odd_feasible / command_id_churn / taxonomy_mismatch.
    """
    rows = _prepare_time_rows(rows, alignment)
    from tools.sil.colregs_module_oracle import (
        MRM_ID_TO_TYPE,
        _canonical_mrm_id,
    )

    alert_rows = sorted(
        (r for r in rows if r.get("topic") == _M7_SAFETY_ALERT_TOPIC),
        key=_event_time_s,
    )
    mrc_requested = any(
        _is_mrm_recommendation(_value(r, "recommended_mrm", ""))
        and int(_value(r, "severity", 0)) >= MRC_REQUIRED_SEVERITY
        for r in alert_rows
    )
    command_rows = sorted(
        (r for r in rows if r.get("topic") == _M1_MRM_COMMAND_TOPIC),
        key=_event_time_s,
    )
    command_published = bool(command_rows)

    non_m1_publisher = False
    command_not_odd_feasible = False
    command_id_churn = False
    taxonomy_mismatch = False
    if command_rows:
        # M1-sole-publisher invariant: a command on /l3/m1/mrm_command is
        # M1-authored by construction (the trace writer only subscribes to that
        # topic for MRMCommand), so no in-row field check is needed for the
        # M1-topic rows. The foreign-publisher violation is detected below via
        # topic provenance, independent of whether an M1 row exists.
        # ODD feasibility: M1 must not select an MRM it cannot execute. The
        # canonical SIL-unavailable types are EMERGENCY_TURN (no executor) and
        # ANCHOR (requires anchorage/depth). A rationale explicitly marking the
        # selection infeasible also flags this.
        for r in command_rows:
            mrm_type = int(_value(r, "mrm_type", 0))
            rationale = str(_value(r, "rationale", "")).lower()
            if mrm_type in (3, 4) and "unavailable" in rationale:
                command_not_odd_feasible = True
            if "not feasible" in rationale or "infeasible" in rationale:
                command_not_odd_feasible = True
        # Command-id churn: command_id changes without a generation bump. A
        # stable MRC activation preserves command_id across heartbeats and only
        # bumps it together with command_generation on a new selection.
        prev_id: str | None = None
        prev_gen: int | None = None
        for r in command_rows:
            cid = str(_value(r, "command_id", ""))
            gen = int(_value(r, "command_generation", 0))
            if prev_id is not None and cid != prev_id and gen == prev_gen:
                command_id_churn = True
                break
            prev_id = cid
            prev_gen = gen
        # Taxonomy mismatch: published mrm_id text must map to the published
        # numeric mrm_type under the canonical taxonomy.
        for r in command_rows:
            mrm_type = int(_value(r, "mrm_type", 0))
            canonical = _canonical_mrm_id(str(_value(r, "mrm_id", "")))
            expected = MRM_ID_TO_TYPE.get(canonical)
            if expected is not None and mrm_type != expected:
                taxonomy_mismatch = True
                break

    # Non-M1 publisher (provenance-based): an executable MRMCommand-shaped row
    # appeared on a NON-M1 topic. The trace records the source topic for every
    # row, and MRMCommand's ``trigger_alert_key`` field is unique to that
    # message type (no other traced topic emits it), so a row carrying
    # ``trigger_alert_key`` on any topic other than /l3/m1/mrm_command is an
    # executable command authored outside M1 — that is RED. This replaces the
    # prior dead ``publisher``/``command_source`` AND-logic, which read fields
    # MRMCommand.msg does not carry and so could never fire from any real or
    # synthetic trace. The check runs over ALL rows (not just command_rows) so
    # a foreign command is caught even when no M1 command exists.
    non_m1_publisher = any(
        _is_executable_mrm_command_row(r)
        and str(r.get("topic", "")) != _M1_MRM_COMMAND_TOPIC
        for r in rows
    )

    return {
        "mrc_requested": mrc_requested,
        "command_published": command_published,
        "non_m1_publisher": non_m1_publisher,
        "command_not_odd_feasible": command_not_odd_feasible,
        "command_id_churn": command_id_churn,
        "taxonomy_mismatch": taxonomy_mismatch,
    }


def extract_l4_gnc_execution(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract L4/GNC MRM-execution evidence from the M1/GNC trace rows.

    Derives the GNC execution-role evidence the ``evaluate_l4_gnc_oracle``
    consumes: command_active / gnc_acknowledged / command_id_mismatch /
    generation_mismatch / rejected / stale_holding /
    non_mrm_command_source_while_mrm_active.
    """
    rows = _prepare_time_rows(rows, alignment)
    from tools.sil.colregs_module_oracle import (
        MRM_EXEC_ACCEPTED,
        MRM_EXEC_COMPLETED,
        MRM_EXEC_EXECUTING,
        MRM_EXEC_REJECTED,
        MRM_EXEC_STALE_HOLDING,
    )

    command = _latest_command(rows)
    status_rows = sorted(
        (r for r in rows if r.get("topic") == _M1_MRM_EXEC_STATUS_TOPIC),
        key=_event_time_s,
    )
    if command is None:
        return {
            "command_active": False,
            "gnc_acknowledged": False,
            "command_id_mismatch": False,
            "generation_mismatch": False,
            "rejected": False,
            "stale_holding": False,
            "non_mrm_command_source_while_mrm_active": False,
        }

    cmd_id = str(_value(command, "command_id", ""))
    cmd_gen = int(_value(command, "command_generation", 0))
    # A command is "active" while fresh: there is no later cancellation
    # (MRM_NONE / empty) and an ack window exists. We treat the latest command
    # as active when any status row references it or no status has arrived yet.
    matching = [
        r for r in status_rows
        if str(_value(r, "command_id", "")) == cmd_id
        and int(_value(r, "command_generation", 0)) == cmd_gen
    ]
    command_active = True
    rejected = any(int(_value(r, "state", 0)) == MRM_EXEC_REJECTED for r in matching)
    stale_holding = any(
        int(_value(r, "state", 0)) == MRM_EXEC_STALE_HOLDING for r in matching)
    completed = any(int(_value(r, "state", 0)) == MRM_EXEC_COMPLETED for r in matching)
    acknowledged = any(
        int(_value(r, "state", 0)) in (MRM_EXEC_ACCEPTED, MRM_EXEC_EXECUTING)
        or completed
        for r in matching
    )
    command_id_mismatch = bool(status_rows) and not matching and any(
        str(_value(r, "command_id", "")) and str(_value(r, "command_id", "")) != cmd_id
        for r in status_rows
    )
    generation_mismatch = bool(matching) and any(
        int(_value(r, "command_generation", 0)) != cmd_gen for r in matching
    )

    # GNC command source other than MRM while a fresh supported M1 command is
    # active: scan /l3/gnc/execution_status for a non-MRM source within the
    # command validity window.
    non_mrm_command_source_while_mrm_active = False
    cmd_t = _event_time_s(command)
    validity_s = float(_value(command, "validity_s", 0.0) or 0.0)
    window_end = cmd_t + validity_s if validity_s > 0 else float("inf")
    supported = int(_value(command, "mrm_type", 0)) in (1, 2)  # SAFE_SPEED_HOLD, STOP
    if command_active and supported:
        for r in rows:
            if r.get("topic") != "/l3/gnc/execution_status":
                continue
            t = _event_time_s(r)
            if t < cmd_t or t > window_end:
                continue
            src = str(_value(r, "command_source", "")).upper()
            if src and src not in ("MRM",):
                non_mrm_command_source_while_mrm_active = True
                break

    return {
        "command_active": command_active,
        "gnc_acknowledged": acknowledged,
        "command_id_mismatch": command_id_mismatch,
        "generation_mismatch": generation_mismatch,
        "rejected": rejected,
        "stale_holding": stale_holding,
        "non_mrm_command_source_while_mrm_active": non_mrm_command_source_while_mrm_active,
    }


def _resolve_mrm_type(value: Any) -> int | None:
    """Resolve an active_mrm_type value to its numeric enum, or None.

    The trace writer emits the uint8 enum (MRM_NONE=0..MRM_ANCHOR=4). Test
    fixtures / human-authored rows may carry a canonical string id
    ('MRM-01-SAFE-SPEED-HOLD'); resolve both to the numeric type so the
    mismatch check compares like-for-like under the canonical taxonomy.
    """
    from tools.sil.colregs_module_oracle import MRM_ID_TO_TYPE, _canonical_mrm_id

    if value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value)
    canonical = _canonical_mrm_id(str(value))
    if canonical:
        return MRM_ID_TO_TYPE.get(canonical)
    return None


def extract_m4_mrm_telemetry(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M4 MRM-telemetry mismatch evidence.

    M4 mirrors the exact M1-authorized MRM telemetry (active_mrm_type /
    active_mrm_command_id / active_mrm_generation). A mismatch between M4's
    telemetry and the active M1 command is an M4 attribution defect
    (M1_MRM_TELEMETRY_MISMATCH), separate from the M1 authority and GNC roles.
    """
    rows = _prepare_time_rows(rows, alignment)
    command = _latest_command(rows)
    m4_rows = sorted(
        (r for r in rows if r.get("topic") == _M4_BEHAVIOR_PLAN_TOPIC),
        key=_event_time_s,
    )
    telemetry_mismatch = False
    if command and m4_rows:
        cmd_type = int(_value(command, "mrm_type", 0))
        cmd_id = str(_value(command, "command_id", ""))
        cmd_gen = int(_value(command, "command_generation", 0))
        # Consider M4 rows at/after the command that carry MRM telemetry.
        cmd_t = _event_time_s(command)
        relevant = [
            r for r in m4_rows
            if _event_time_s(r) >= cmd_t
            and _resolve_mrm_type(_value(r, "active_mrm_type", 0)) not in (None, 0)
        ]
        for r in relevant:
            m4_type = _resolve_mrm_type(_value(r, "active_mrm_type", 0))
            m4_cid = str(_value(r, "active_mrm_command_id", ""))
            m4_gen = int(_value(r, "active_mrm_generation", 0) or 0)
            if m4_type != cmd_type or (m4_cid and m4_cid != cmd_id) or (
                m4_gen and m4_gen != cmd_gen
            ):
                telemetry_mismatch = True
                break
    return {"mrm_telemetry_mismatch": telemetry_mismatch}


def extract_m5_during_mrm(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, Any]:
    """Extract M5-command-during-external-MRM evidence.

    M5 must suppress new tactical output (avoidance plans / reactive overrides)
    while an external M1-authorized MRM executor is active. A reactive override
    or fresh committed avoidance plan inside the command window is an M5 defect
    (M5_COMMAND_DURING_EXTERNAL_MRM), separate from the M4 telemetry role.
    """
    rows = _prepare_time_rows(rows, alignment)
    command = _latest_command(rows)
    command_during_external_mrm = False
    if command:
        cmd_t = _event_time_s(command)
        validity_s = float(_value(command, "validity_s", 0.0) or 0.0)
        window_end = cmd_t + validity_s if validity_s > 0 else cmd_t + 60.0
        for r in rows:
            t = _event_time_s(r)
            if t < cmd_t or t > window_end:
                continue
            topic = r.get("topic", "")
            if topic == _M5_OVERRIDE_TOPIC:
                command_during_external_mrm = True
                break
            if topic == "/l3/m5/avoidance_plan":
                # Only a fresh committed exec branch counts as new tactical
                # output; pre-existing latched plans during MRM are suppressed.
                if _commit_branch(r) in {1, 2, 3}:
                    command_during_external_mrm = True
                    break
    return {"command_during_external_mrm": command_during_external_mrm}


def _evaluate_module_oracles(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, "OracleResult"]:  # type: ignore[name-defined]
    """Run the stage-separated MRM-chain oracles over raw trace rows.

    Returns one OracleResult per role, keyed by module:
      "M7"     — M7 checker evidence (alerts/recommendations/veto)
      "M1"     — M1 MRM authority evidence (recommendation parsing, feasibility,
                 command publication, generation/identity)
      "M4"     — M4 MRM-telemetry mirror contract (M1_MRM_TELEMETRY_MISMATCH)
      "M5"     — M5 suppression during external MRM
                 (M5_COMMAND_DURING_EXTERNAL_MRM)
      "L4_GNC" — L4/GNC execution evidence (acceptance, EXECUTING,
                 COMPLETED/REJECTED, MRM priority)

    The three MRM roles (M7 checker / M1 authority / GNC execution) are
    evaluated from INDEPENDENT evidence dicts so attribution never conflates
    them. Healthy runs with no MRC-required alert do not require a command
    (M1 and L4_GNC pass with no active command).
    """
    rows = _prepare_time_rows(rows, alignment)
    from tools.sil.colregs_module_oracle import (
        evaluate_l4_gnc_oracle,
        evaluate_m1_mrm_authority_oracle,
        evaluate_m7_oracle,
    )

    m7_result = evaluate_m7_oracle(
        m7_output=extract_m7_veto(rows, alignment=alignment)
    )
    m1_result = evaluate_m1_mrm_authority_oracle(
        m1_mrm_output=extract_m1_mrm_authority(rows, alignment=alignment))
    m4_telemetry = extract_m4_mrm_telemetry(rows, alignment=alignment)
    m4_result = OracleResult(
        module="M4_BehaviorArbiter",
        passed=not m4_telemetry["mrm_telemetry_mismatch"],
        failed_checks=(["M1_MRM_TELEMETRY_MISMATCH"]
                       if m4_telemetry["mrm_telemetry_mismatch"] else []),
        evidence=m4_telemetry,
    )
    m5_during = extract_m5_during_mrm(rows, alignment=alignment)
    m5_result = OracleResult(
        module="M5_TacticalPlanner",
        passed=not m5_during["command_during_external_mrm"],
        failed_checks=(["M5_COMMAND_DURING_EXTERNAL_MRM"]
                       if m5_during["command_during_external_mrm"] else []),
        evidence=m5_during,
    )
    l4_gnc_result = evaluate_l4_gnc_oracle(
        l4_gnc_output=extract_l4_gnc_execution(rows, alignment=alignment))
    results = {
        "M7": m7_result,
        "M1": m1_result,
        "M4": m4_result,
        "M5": m5_result,
        "L4_GNC": l4_gnc_result,
    }
    if alignment is not None:
        event_times = _event_time_evidence(rows)
        for result in results.values():
            result.evidence["event_times"] = list(event_times)
    return results


def evaluate_module_oracles_strict(
    rows: list[dict[str, Any]],
    *,
    alignment: ClockAlignment | None = None,
) -> dict[str, "OracleResult"]:  # type: ignore[name-defined]
    """G1 module-oracle entrypoint; legacy raw-time fallback is forbidden."""
    return _evaluate_module_oracles(
        rows,
        alignment=_require_alignment(alignment, rows),
    )


def evaluate_module_oracles(
    rows: list[dict[str, Any]],
) -> dict[str, "OracleResult"]:  # type: ignore[name-defined]
    """Legacy module-oracle entrypoint retained for non-G1 callers."""
    return _evaluate_module_oracles(rows, alignment=None)
