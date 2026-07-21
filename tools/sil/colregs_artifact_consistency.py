"""G-ART artifact consistency gate (design §7).

Three checkers live here:

1. ``check_trace_artifacts_strict(rows, report, alignment=...)`` — G1 G-ART
   with mandatory canonical-time selection and typed event-time provenance.

2. ``check_trace_artifacts(rows, report)`` — the legacy NON-CIRCULAR raw-trace G-ART
   (Task 3 Step 6). It derives CPA and recovery time INDEPENDENTLY from archived
   raw trace rows and compares them against the evaluator report. Mutation tests
   prove the comparison reads the raw trace, not the report, in both directions.

3. ``check_consistency(verdict, timeline)`` — the legacy identifier/numeric/
   ordering checker kept for backward compatibility with the runner's existing
   G-ART wiring.

Design rules enforced here:
- A scoring row is required in every evaluated run, so missing scoring rows are
  an M8 evidence failure (``trace_cpa_unavailable``) → inconsistent AND
  incomplete.
- Missing recovery rows remain complete and artifact-consistent when the report
  also records no recovery. The absent required transition is an honest SUT
  failure, not missing evidence. A false report recovery claim remains invalid.
- If the report claims recovery/PASS while raw recovery is absent, emit
  ``recovery_time_mismatch`` and set ``g_art_consistent=False``.
- Never raise ``ValueError`` / ``StopIteration``: every derivation falls back to
  ``None`` when its source rows are absent.
- Available report values are compared within declared numeric tolerances.
"""
from __future__ import annotations

import math
from collections.abc import Mapping
from typing import Any

from tools.sil.trace_time import (
    MAX_EVENT_UNCERTAINTY_S,
    ClockAlignment,
    ClockTransform,
    EventTime,
    EventTimeSelectionError,
    event_time_s,
    select_event_time,
)


# Numeric tolerances for report-vs-raw comparison. These are measurement
# tolerances (not pass/fail thresholds): a report CPA is "consistent" with the
# raw trace when they agree to within this rounding/noise band.
_CPA_TOL_M = 1.0
_TIME_TOL_S = 1.0


def _safe_float(value: Any) -> float | None:
    """Coerce a trace/report value to float, returning None on any failure."""
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def check_qualified_artifacts(
    rows: list[dict[str, Any]],
    *,
    physical_minimum_separation_m: float | None,
    physical_samples_consistent: bool = True,
    report: Mapping[str, Any],
    distance_tolerance_m: float,
    recovery_required: bool | None = None,
) -> dict[str, Any]:
    """Cross-check physical truth, Scoring, and report without substitution."""
    findings: list[str] = []
    consistent = True
    complete = True

    if physical_samples_consistent is not True:
        findings.append("physical_scoring_sample_mismatch")
        consistent = False

    physical_value = _strict_number(physical_minimum_separation_m)
    if physical_value is None or physical_value < 0.0:
        findings.append("physical_separation_unavailable")
        physical_value = None
        consistent = False
        complete = False

    scoring_values: list[float] = []
    scoring_rows = [row for row in rows if row.get("topic") == "/sil/scoring"]
    for row in scoring_rows:
        payload = row.get("payload")
        separation_nm = (
            _strict_number(payload.get("separation_nm"))
            if isinstance(payload, Mapping)
            else None
        )
        if separation_nm is None or separation_nm < 0.0:
            findings.append("scoring_separation_invalid")
            consistent = False
            complete = False
            continue
        scoring_values.append(separation_nm * 1852.0)
    scoring_value = min(scoring_values) if scoring_values else None
    if scoring_value is None and "scoring_separation_invalid" not in findings:
        findings.append("scoring_separation_unavailable")
        complete = False

    report_metrics = report.get("evidence_metrics")
    if isinstance(report_metrics, Mapping):
        report_physical = _strict_number(
            report_metrics.get("physical_minimum_separation_m")
        )
        report_scoring = _strict_number(
            report_metrics.get("scoring_minimum_separation_m")
        )
    else:
        report_physical = None
        report_scoring = None
    if report_physical is None or report_physical < 0.0:
        findings.append("report_physical_separation_unavailable")
        report_physical = None
        complete = False
    if report_scoring is None or report_scoring < 0.0:
        findings.append("report_scoring_separation_unavailable")
        report_scoring = None
        complete = False

    comparisons = (
        (
            physical_value,
            scoring_value,
            "physical_scoring_separation_mismatch",
        ),
        (
            physical_value,
            report_physical,
            "physical_report_separation_mismatch",
        ),
        (
            scoring_value,
            report_scoring,
            "scoring_report_separation_mismatch",
        ),
    )
    for left, right, finding in comparisons:
        if (
            left is not None
            and right is not None
            and abs(left - right) > distance_tolerance_m
        ):
            findings.append(finding)
            consistent = False

    raw_first_recovery: dict[str, Any] | None = None
    report_first_recovery: dict[str, Any] | None = None
    if recovery_required is not None:
        recovery_rows = []
        for row in rows:
            payload = row.get("payload")
            if (
                row.get("topic") != "/l3/m4/behavior_plan"
                or not isinstance(payload, Mapping)
                or payload.get("behavior") != 7
            ):
                continue
            canonical_t_s = _strict_number(row.get("canonical_t_s"))
            uncertainty_s = _strict_number(row.get("time_uncertainty_s"))
            capture_seq = row.get("capture_seq")
            alignment_id = row.get("alignment_id")
            if (
                canonical_t_s is None
                or uncertainty_s is None
                or not 0.0 <= uncertainty_s <= MAX_EVENT_UNCERTAINTY_S
                or isinstance(capture_seq, bool)
                or not isinstance(capture_seq, int)
                or capture_seq < 0
                or not isinstance(alignment_id, str)
                or not alignment_id
            ):
                findings.append("trace_recovery_invalid")
                consistent = False
                complete = False
                continue
            recovery_rows.append(
                {
                    "capture_seq": capture_seq,
                    "topic": "/l3/m4/behavior_plan",
                    "canonical_t_s": canonical_t_s,
                    "alignment_id": alignment_id,
                    "time_uncertainty_s": uncertainty_s,
                }
            )
        raw_first_recovery = min(
            recovery_rows,
            key=lambda item: (item["canonical_t_s"], item["capture_seq"]),
            default=None,
        )
        report_recovery_value = report.get("first_recovery", ...)
        if report_recovery_value is ...:
            findings.append("report_recovery_unavailable")
            complete = False
        elif report_recovery_value is not None:
            if isinstance(report_recovery_value, Mapping):
                report_first_recovery = dict(report_recovery_value)
            else:
                findings.append("report_recovery_invalid")
                consistent = False
                complete = False
        if raw_first_recovery is None:
            if recovery_required:
                findings.append("trace_recovery_unavailable")
            if report_first_recovery is not None:
                findings.append("recovery_time_mismatch")
                consistent = False
                complete = False
        elif report_first_recovery is None:
            if "report_recovery_unavailable" not in findings:
                findings.append("report_recovery_unavailable")
            complete = False
        elif report_first_recovery != raw_first_recovery:
            findings.append("recovery_time_mismatch")
            consistent = False

    return {
        "g_art_consistent": consistent,
        "g_art_complete": complete,
        "g_art_ok": consistent and complete,
        "finding_codes": findings,
        "physical_minimum_separation_m": physical_value,
        "scoring_minimum_separation_m": scoring_value,
        "report_physical_minimum_separation_m": report_physical,
        "report_scoring_minimum_separation_m": report_scoring,
        "raw_first_recovery": raw_first_recovery,
        "report_first_recovery": report_first_recovery,
        "evidence_source": "qualified physical truth + normalized Scoring + report",
    }


def _raw_min_cpa(rows: list[dict[str, Any]]) -> float | None:
    """Derive the minimum CPA independently from /sil/scoring rows.

    Scoring is published every evaluated run; the report's min_cpa_m must be
    reproducible from these rows. Returns None when no scoring row carries a
    CPA — that is an M8 evidence failure, not a CPA value.

    NOTE: ``/l3/m2/world_state.primary_cpa_m`` is the *predicted geometric*
    CPA, not the *achieved* CPA — it must NOT be used here because it would
    always mismatch the report's achieved CPA from scoring.arrow.
    """
    cpas: list[float] = []
    for row in rows:
        if row.get("topic") != "/sil/scoring":
            continue
        cpa = _safe_float(row.get("cpa_m"))
        if cpa is not None:
            cpas.append(cpa)
    if not cpas:
        return None
    return min(cpas)


def _raw_recovery_time(rows: list[dict[str, Any]]) -> float | None:
    """Derive the first M4 RECOVERY (behavior==7) time from raw trace rows.

    Returns None when no recovery behavior is observed. This is independent of
    the report's first_recovery_t so a mutated report is caught.
    """
    recovery_rows = [
        row for row in rows
        if row.get("topic") == "/l3/m4/behavior_plan"
        and _safe_float(row.get("behavior")) == 7.0
    ]
    if not recovery_rows:
        return None
    try:
        return min(event_time_s(row) for row in recovery_rows)
    except (TypeError, ValueError):
        return None


def _raw_m6_clear_terminal(
    rows: list[dict[str, Any]],
    *,
    dwell_s: float = 10.0,
) -> float | None:
    samples = sorted(
        (
            row for row in rows
            if row.get("topic") == "/l3/m6/colregs_constraint"
        ),
        key=event_time_s,
    )
    saw_conflict = False
    clear_since: float | None = None
    for row in samples:
        time_s = event_time_s(row)
        try:
            encounter_state = int(row.get("encounter_state", 0) or 0)
        except (TypeError, ValueError):
            encounter_state = 0
        encounter_active = (
            bool(row.get("conflict_detected", False))
            or bool(row.get("active_rules") or [])
            or encounter_state in {1, 2}
        )
        if encounter_active:
            saw_conflict = True
            clear_since = None
            continue
        if not saw_conflict:
            continue
        if clear_since is None:
            clear_since = time_s
        elif time_s - clear_since >= dwell_s:
            return time_s
    return None


def _strict_time_failure(error: EventTimeSelectionError) -> dict[str, Any]:
    return {
        "g_art_consistent": False,
        "g_art_complete": False,
        "g_art_ok": False,
        "finding_codes": [
            "g1_3_time_alignment_failure",
            f"g1_3_{error.reason}",
        ],
        "raw_min_cpa_m": None,
        "raw_recovery": None,
        "raw_terminal": None,
        "report_min_cpa_m": None,
        "report_first_recovery": None,
        "report_terminal": None,
        "failure_stage": "G1.3",
        "time_alignment_failure": {
            "record_id": error.record_id,
            "attempted_source": error.attempted_source,
            "alignment_id": error.alignment_id,
            "uncertainty_s": error.uncertainty_s,
            "reason": error.reason,
        },
        "evidence_source": "strict archived raw trace rows",
    }


def _strict_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    return number if math.isfinite(number) else None


def _typed_report_time(
    value: Any,
    alignment: ClockAlignment,
    rows_and_times: list[tuple[dict[str, Any], EventTime]],
) -> tuple[str, EventTime] | None | bool:
    if value is None:
        return None
    if not isinstance(value, Mapping):
        return False
    required = {
        "canonical_s",
        "raw_s",
        "source",
        "alignment_id",
        "uncertainty_s",
        "record_id",
    }
    if not required.issubset(value):
        return False
    canonical_s = _strict_number(value.get("canonical_s"))
    raw_s = _strict_number(value.get("raw_s"))
    uncertainty_s = _strict_number(value.get("uncertainty_s"))
    source = value.get("source")
    alignment_id = value.get("alignment_id")
    record_id = value.get("record_id")
    transform = (
        alignment.transforms.get(source)
        if isinstance(source, str) and isinstance(alignment.transforms, Mapping)
        else None
    )
    scale = (
        _strict_number(transform.scale)
        if isinstance(transform, ClockTransform)
        else None
    )
    offset_s = (
        _strict_number(transform.offset_s)
        if isinstance(transform, ClockTransform)
        else None
    )
    if (
        canonical_s is None
        or raw_s is None
        or uncertainty_s is None
        or not 0.0 <= uncertainty_s <= MAX_EVENT_UNCERTAINTY_S
        or not isinstance(source, str)
        or not source
        or not isinstance(alignment_id, str)
        or not alignment_id
        or not isinstance(record_id, str)
        or not record_id
        or alignment_id != alignment.alignment_id
        or not isinstance(transform, ClockTransform)
        or scale is None
        or offset_s is None
        or not math.isclose(
            canonical_s,
            scale * raw_s + offset_s,
            rel_tol=0.0,
            abs_tol=1.0e-12,
        )
    ):
        return False
    candidate = EventTime(
        canonical_s=canonical_s,
        raw_s=raw_s,
        source=source,
        alignment_id=alignment_id,
        uncertainty_s=uncertainty_s,
    )
    if source == "source_stamp":
        sec = math.floor(raw_s)
        nanosec = round((raw_s - sec) * 1.0e9)
        if nanosec == 1_000_000_000:
            sec += 1
            nanosec = 0
        raw_clock = {"source_stamp": {"sec": sec, "nanosec": nanosec}}
    else:
        raw_clock = {source: raw_s}
    try:
        recomputed = select_event_time(
            {
                "record_id": "<report-time>",
                **raw_clock,
                "source_domain": transform.source_domain,
                "run_generation": alignment.lifecycle_run_generation,
            },
            alignment,
        )
    except EventTimeSelectionError:
        return False
    if recomputed != candidate:
        return False
    members = [
        selected
        for row, selected in rows_and_times
        if row.get("record_id") == record_id
    ]
    if len(members) != 1:
        return False
    member = members[0]
    if (
        candidate.source != member.source
        or candidate.raw_s != member.raw_s
        or candidate.alignment_id != member.alignment_id
        or candidate.uncertainty_s != member.uncertainty_s
    ):
        return False
    return record_id, candidate


def _strict_raw_recovery(
    rows_and_times: list[tuple[dict[str, Any], EventTime]],
) -> tuple[str, EventTime] | None:
    candidates = [
        (row["record_id"], selected)
        for row, selected in rows_and_times
        if row.get("topic") == "/l3/m4/behavior_plan"
        and _safe_float(row.get("behavior")) == 7.0
        and isinstance(row.get("record_id"), str)
        and bool(row["record_id"])
    ]
    return min(candidates, key=lambda item: item[1].canonical_s, default=None)


def _strict_raw_m6_clear_terminal(
    rows_and_times: list[tuple[dict[str, Any], EventTime]],
    *,
    dwell_s: float,
) -> tuple[str, EventTime] | None:
    samples = sorted(
        (
            (row, selected)
            for row, selected in rows_and_times
            if row.get("topic") == "/l3/m6/colregs_constraint"
        ),
        key=lambda item: item[1].canonical_s,
    )
    saw_conflict = False
    clear_since: EventTime | None = None
    for row, selected in samples:
        try:
            encounter_state = int(row.get("encounter_state", 0) or 0)
        except (TypeError, ValueError):
            encounter_state = 0
        encounter_active = (
            bool(row.get("conflict_detected", False))
            or bool(row.get("active_rules") or [])
            or encounter_state in {1, 2}
        )
        if encounter_active:
            saw_conflict = True
            clear_since = None
            continue
        if not saw_conflict:
            continue
        if clear_since is None:
            clear_since = selected
        elif selected.canonical_s - clear_since.canonical_s >= dwell_s:
            record_id = row.get("record_id")
            return (
                (record_id, selected)
                if isinstance(record_id, str) and record_id
                else None
            )
    return None


def _typed_times_match(
    left: tuple[str, EventTime],
    right: tuple[str, EventTime],
) -> bool:
    left_record_id, left_time = left
    right_record_id, right_time = right
    if left_record_id != right_record_id or left_time.source != right_time.source:
        return False
    return (
        left_time.raw_s == right_time.raw_s
        and left_time.alignment_id == right_time.alignment_id
        and abs(left_time.canonical_s - right_time.canonical_s)
        <= left_time.uncertainty_s + right_time.uncertainty_s
    )


def check_trace_artifacts_strict(
    rows: list[dict[str, Any]],
    report: dict[str, Any],
    *,
    alignment: ClockAlignment | None = None,
    cpa_tol_m: float = _CPA_TOL_M,
) -> dict[str, Any]:
    """Strict G-ART path using selected canonical time and typed provenance."""
    if not isinstance(alignment, ClockAlignment):
        return _strict_time_failure(
            EventTimeSelectionError(
                str(rows[0].get("record_id", "<unknown>")) if rows else "<unknown>",
                None,
                "<missing>",
                MAX_EVENT_UNCERTAINTY_S + 1.0,
                "clock_alignment_missing",
            )
        )
    try:
        selected_times = alignment.select_sequence(rows)
    except EventTimeSelectionError as error:
        return _strict_time_failure(error)

    rows_and_times = list(zip(rows, selected_times, strict=True))
    raw_min_cpa = _raw_min_cpa(rows)
    raw_recovery = _strict_raw_recovery(rows_and_times)
    report_cpa = _safe_float(report.get("min_cpa_m"))
    target_resolution = (
        str(report.get("fast_terminal") or "")
        == "ENCOUNTER_CLEAR_WITH_OWN_HOLD"
    )
    clear_dwell_s = _safe_float(report.get("terminal_clear_observation_dwell_s"))
    raw_terminal = (
        _strict_raw_m6_clear_terminal(
            rows_and_times,
            dwell_s=10.0 if clear_dwell_s is None else clear_dwell_s,
        )
        if target_resolution
        else raw_recovery
    )
    report_time_key = "terminal" if target_resolution else "first_recovery"
    report_time = (
        _typed_report_time(report[report_time_key], alignment, rows_and_times)
        if report_time_key in report
        else False
    )

    findings: list[str] = []
    failure_stage: str | None = None
    if report_time is False:
        findings.append(f"g1_6_{report_time_key}_invalid")
        failure_stage = "G1.6"

    if raw_min_cpa is None:
        findings.append("trace_cpa_unavailable")
    elif report_cpa is not None and abs(report_cpa - raw_min_cpa) > cpa_tol_m:
        findings.append("min_cpa_mismatch")

    typed_report_time = report_time if isinstance(report_time, tuple) else None
    mismatch_code = "terminal_time_mismatch" if target_resolution else "recovery_time_mismatch"
    unavailable_code = (
        "trace_terminal_unavailable"
        if target_resolution
        else "trace_recovery_unavailable"
    )
    if raw_terminal is None:
        findings.append(unavailable_code)
        honest_terminal = (
            typed_report_time is None
            and not bool(report.get("overall_pass", False))
            and bool(report.get("stop_reason"))
        )
        if typed_report_time is not None or not honest_terminal:
            findings.append(mismatch_code)
    elif typed_report_time is None or not _typed_times_match(
        raw_terminal, typed_report_time
    ):
        findings.append(mismatch_code)

    inconsistency_codes = {
        "min_cpa_mismatch",
        "recovery_time_mismatch",
        "terminal_time_mismatch",
    }
    g_art_consistent = (
        failure_stage is None
        and not any(code in inconsistency_codes for code in findings)
    )
    g_art_complete = (
        failure_stage is None
        and raw_min_cpa is not None
        and raw_terminal is not None
    )
    result = {
        "g_art_consistent": g_art_consistent,
        "g_art_complete": g_art_complete,
        "g_art_ok": g_art_consistent and g_art_complete,
        "finding_codes": findings,
        "raw_min_cpa_m": raw_min_cpa,
        "raw_recovery": raw_recovery[1].as_dict() if raw_recovery is not None else None,
        "raw_terminal": raw_terminal[1].as_dict() if raw_terminal is not None else None,
        "report_min_cpa_m": report_cpa,
        "report_first_recovery": (
            typed_report_time[1].as_dict()
            if not target_resolution and typed_report_time is not None
            else None
        ),
        "report_terminal": (
            typed_report_time[1].as_dict()
            if target_resolution and typed_report_time is not None
            else None
        ),
        "evidence_source": "strict archived raw trace rows",
    }
    if failure_stage is not None:
        result["failure_stage"] = failure_stage
    return result


def check_trace_artifacts(
    rows: list[dict[str, Any]],
    report: dict[str, Any],
    *,
    cpa_tol_m: float = _CPA_TOL_M,
    time_tol_s: float = _TIME_TOL_S,
) -> dict[str, Any]:
    """Non-circular raw-trace G-ART (Task 3 Step 6).

    Derives ``min_cpa_m`` and ``first_recovery_t`` independently from the raw
    archived trace rows and compares them against the report. Returns separate
    ``g_art_consistent`` (the report matches the raw evidence) and
    ``g_art_complete`` (all required evidence rows are present) flags plus a
    list of ``finding_codes``.

    Never raises: every derivation falls back to None when its source rows are
    absent, and the absence is reported as a finding rather than an exception.
    """
    findings: list[str] = []
    raw_min_cpa = _raw_min_cpa(rows)
    raw_recovery_t = _raw_recovery_time(rows)
    report_cpa = _safe_float(report.get("min_cpa_m"))
    report_recovery_t = _safe_float(report.get("first_recovery_t"))
    target_resolution = (
        str(report.get("fast_terminal") or "")
        == "ENCOUNTER_CLEAR_WITH_OWN_HOLD"
    )
    clear_dwell_s = _safe_float(
        report.get("terminal_clear_observation_dwell_s")
    )
    raw_terminal_t = (
        _raw_m6_clear_terminal(
            rows,
            dwell_s=10.0 if clear_dwell_s is None else clear_dwell_s,
        )
        if target_resolution else raw_recovery_t
    )
    report_terminal_t = (
        _safe_float(report.get("terminal_t"))
        if target_resolution else report_recovery_t
    )
    overall_pass = bool(report.get("overall_pass", False))
    stop_reason = str(report.get("stop_reason") or "")

    # ── CPA comparison (independent of report) ────────────────────────────
    # Scoring is required in every evaluated run: absent scoring rows are an
    # M8 evidence failure (inconsistent + incomplete).
    if raw_min_cpa is None:
        findings.append("trace_cpa_unavailable")
    elif report_cpa is not None and abs(report_cpa - raw_min_cpa) > cpa_tol_m:
        findings.append("min_cpa_mismatch")

    # ── Recovery-time comparison (independent of report) ──────────────────
    if target_resolution:
        if raw_terminal_t is None:
            findings.append("trace_terminal_unavailable")
            if report_terminal_t is not None:
                findings.append("terminal_time_mismatch")
        elif report_terminal_t is None or abs(
            report_terminal_t - raw_terminal_t
        ) > time_tol_s:
            findings.append("terminal_time_mismatch")
    elif raw_recovery_t is None:
        # Missing recovery rows: incomplete. Whether this is also inconsistent
        # depends on whether the report honestly admits the absence.
        if report_recovery_t is not None:
            # Report claims a recovery time the raw trace cannot reproduce.
            findings.append("recovery_time_mismatch")
        else:
            honest_terminal = (
                not overall_pass
                and bool(stop_reason)
            )
            if honest_terminal:
                # The report admits recovery was not reached with an explicit,
                # non-PASS terminal reason. The FAST boundary evaluator owns the
                # allowed reason enum; G-ART only checks consistency + honesty.
                findings.append("trace_recovery_unavailable")
            else:
                # Report is PASS or has no terminal reason but recovery is
                # absent — that is an inconsistency, not just incompleteness.
                findings.append("trace_recovery_unavailable")
                findings.append("recovery_time_mismatch")
    elif report_recovery_t is not None:
        if abs(report_recovery_t - raw_recovery_t) > time_tol_s:
            findings.append("recovery_time_mismatch")

    # ── Resolve consistency vs completeness ───────────────────────────────
    # Consistency = the report matches the raw evidence that IS available.
    # Completeness = all required evidence (scoring + recovery) is present.
    # trace_cpa_unavailable is a completeness gap, not an inconsistency: the
    # achieved CPA is only in the binary scoring.arrow file, so when the JSONL
    # trace has no CPA source, the report may still be correct.
    inconsistency_codes = {
        "min_cpa_mismatch",
        "recovery_time_mismatch",
        "terminal_time_mismatch",
    }
    g_art_consistent = not any(code in inconsistency_codes for code in findings)
    recovery_unavailable = "trace_recovery_unavailable" in findings
    terminal_unavailable = "trace_terminal_unavailable" in findings
    g_art_complete = (
        not recovery_unavailable
        and not terminal_unavailable
        and raw_min_cpa is not None
    )

    return {
        "g_art_consistent": g_art_consistent,
        "g_art_complete": g_art_complete,
        # Back-compat alias for the runner consumer (ac.get("g_art_ok")).
        "g_art_ok": g_art_consistent and g_art_complete,
        "finding_codes": findings,
        "raw_min_cpa_m": raw_min_cpa,
        "raw_recovery_t": raw_recovery_t,
        "raw_terminal_t": raw_terminal_t,
        "report_min_cpa_m": report_cpa,
        "report_first_recovery_t": report_recovery_t,
        "evidence_source": "archived raw trace rows (independent of report)",
    }


def check_consistency(verdict: dict, timeline: dict,
                      trace_hash: str | None = None,
                      dashboard_hash: str | None = None,
                      *, cpa_tol_m: float = _CPA_TOL_M, time_tol_s: float = _TIME_TOL_S) -> dict:
    """Legacy identifier/numeric/ordering checker (design §7), kept for the
    runner's existing G-ART wiring. ``check_trace_artifacts`` supersedes the
    circular numeric comparison this function performed."""
    findings: list[tuple[str, str]] = []

    # 1. Identifier consistency
    if verdict.get("run_id") != timeline.get("run_id"):
        findings.append(("run_id_mismatch", "identifier"))
    if verdict.get("scenario_id") != timeline.get("scenario_id"):
        findings.append(("scenario_id_mismatch", "identifier"))

    # 2. Numeric consistency
    v_cpa = verdict.get("min_cpa_m")
    t_cpa = (timeline.get("overall") or {}).get("min_cpa_m")
    if v_cpa is not None and t_cpa is not None and abs(v_cpa - t_cpa) > cpa_tol_m:
        findings.append(("min_cpa_mismatch", "numeric"))
    v_rel = verdict.get("release_sim_t")
    t_rel = (timeline.get("phase_semantics") or {}).get("release_sim_t")
    if v_rel is not None and t_rel is not None and abs(v_rel - t_rel) > time_tol_s:
        findings.append(("release_time_mismatch", "numeric"))

    # 3. Ordering consistency (integration defects)
    tc = verdict.get("timing_consistency") or {}
    if tc.get("premature_recovery_before_rule_release"):
        findings.append(("premature_recovery_before_rule_release", "ordering"))

    return {
        "g_art_ok": len(findings) == 0,
        "findings": findings,
        "evidence_source": "verdict.json (source of truth)",
    }
