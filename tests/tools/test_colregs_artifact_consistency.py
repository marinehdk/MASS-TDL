"""Independent G-ART (artifact consistency) tests (Task 3 Step 3).

These are mutation / non-circularity tests. The old G-ART compared
evaluator-derived values with themselves (verdict.min_cpa_m vs the same value
re-placed into a timeline). The new check_trace_artifacts derives CPA and
recovery time INDEPENDENTLY from archived raw trace rows and compares them
against the report. Each test below mutates ONE side to prove the comparison is
non-circular: a mutation on the report side must be detected against the raw
trace, and vice-versa.
"""
import json

import pytest

from tools.sil.colregs_artifact_consistency import (
    check_consistency,
    check_qualified_artifacts,
    check_trace_artifacts,
    check_trace_artifacts_strict,
)
from tools.sil.trace_time import ClockAlignment, ClockTransform, select_event_time


def _strict_alignment() -> ClockAlignment:
    return ClockAlignment(
        lifecycle_run_generation=17,
        anchors=(),
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 0, True, "simulation", 17
            )
        },
        uncertainty_s=0.1,
        source_priority=("sim_t",),
    )


def _strict_row(record_id: str, topic: str, sim_t: float, **payload):
    return {
        "record_id": record_id,
        "topic": topic,
        "sim_t": sim_t,
        "source_domain": "simulation",
        "run_generation": 17,
        **payload,
    }


# ── legacy check_consistency still works (back-compat) ────────────────────


def test_check_consistency_kept_for_back_compat_flags_identifier_mismatch():
    out = check_consistency(
        {"run_id": "a", "scenario_id": "s", "min_cpa_m": 100.0},
        {"run_id": "b", "scenario_id": "s", "overall": {"min_cpa_m": 100.0}},
    )
    assert not out["g_art_ok"]


# ── CPA mutation detection (non-circular) ─────────────────────────────────


def test_gart_detects_report_cpa_mutation_against_raw_trace():
    rows = [
        {"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "min_cpa_m": 999.0, "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert not result["g_art_consistent"]
    assert "min_cpa_mismatch" in result["finding_codes"]


def test_gart_detects_raw_cpa_mutation_against_report():
    """Reverse direction: raw trace has a different CPA than the report.
    Proves the derivation reads the raw trace, not the report."""
    rows = [
        {"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "min_cpa_m": 400.0, "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert not result["g_art_consistent"]
    assert "min_cpa_mismatch" in result["finding_codes"]


# ── recovery time mutation detection (non-circular) ───────────────────────


def test_gart_detects_recovery_time_mutation_against_raw_m4():
    rows = [{"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7}]
    result = check_trace_artifacts(rows, {"scenario_id": "s", "first_recovery_t": 40.0})
    assert not result["g_art_consistent"]
    assert "recovery_time_mismatch" in result["finding_codes"]


def test_gart_detects_raw_recovery_mutation_against_report():
    """Raw recovery at 30s, report claims 30s, but raw row is actually at 55s.
    Proves recovery derivation reads the raw M4 behavior rows."""
    rows = [{"topic": "/l3/m4/behavior_plan", "sim_t": 55.0, "behavior": 7}]
    result = check_trace_artifacts(rows, {"scenario_id": "s", "first_recovery_t": 30.0})
    assert not result["g_art_consistent"]
    assert "recovery_time_mismatch" in result["finding_codes"]


# ── completeness vs consistency separation ────────────────────────────────


def test_gart_reports_unavailable_when_scoring_rows_are_absent():
    """When no CPA source exists in the JSONL trace, G-ART reports a
    completeness gap (trace_cpa_unavailable). This is NOT an inconsistency —
    the achieved CPA may be correct in the scoring.arrow binary; the JSONL
    trace simply does not carry it."""
    rows = [{"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7}]
    result = check_trace_artifacts(rows, {"scenario_id": "s", "first_recovery_t": 30.0})
    assert result["g_art_consistent"]  # no mismatch, just unavailable
    assert not result["g_art_complete"]
    assert "trace_cpa_unavailable" in result["finding_codes"]


def test_gart_accepts_explicit_terminal_report_when_recovery_is_absent():
    rows = [{"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0}]
    report = {
        "scenario_id": "s",
        "min_cpa_m": 210.0,
        "first_recovery_t": None,
        "overall_pass": False,
        "stop_reason": "RECOVERY_BOUNDARY_NOT_REACHED",
    }
    result = check_trace_artifacts(rows, report)
    assert result["g_art_consistent"]
    assert not result["g_art_complete"]
    assert "trace_recovery_unavailable" in result["finding_codes"]


def test_gart_rejects_claimed_recovery_when_raw_recovery_is_absent():
    rows = [{"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0}]
    report = {"scenario_id": "s", "min_cpa_m": 210.0, "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert not result["g_art_consistent"]
    assert "recovery_time_mismatch" in result["finding_codes"]


# ── consistent + complete happy path ──────────────────────────────────────


def test_gart_passes_when_raw_matches_report_and_recovery_present():
    rows = [
        {"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "min_cpa_m": 210.0, "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["finding_codes"] == []


def test_gart_uses_m6_clear_dwell_for_target_resolution_terminal():
    rows = [
        {"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 600.0},
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 90.0,
            "conflict_detected": True,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 200.0,
            "conflict_detected": False,
        },
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 211.0,
            "conflict_detected": False,
        },
    ]
    report = {
        "scenario_id": "target-resolves",
        "min_cpa_m": 600.0,
        "first_recovery_t": None,
        "terminal_t": 211.0,
        "fast_terminal": "ENCOUNTER_CLEAR_WITH_OWN_HOLD",
        "overall_pass": True,
        "stop_reason": "ENCOUNTER_CLEAR_WITH_OWN_HOLD",
    }
    result = check_trace_artifacts(rows, report)
    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["raw_terminal_t"] == 211.0


# ── robustness: never raises, handles tolerances ──────────────────────────


def test_gart_cpa_within_tolerance_is_consistent():
    rows = [
        {"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "min_cpa_m": 210.5, "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert result["g_art_consistent"]


def test_gart_recovery_within_time_tolerance_is_consistent():
    rows = [{"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7}]
    report = {
        "scenario_id": "s",
        "min_cpa_m": None,
        "first_recovery_t": 30.5,
    }
    result = check_trace_artifacts(rows, report)
    # No scoring rows -> not complete, but recovery consistent within tol.
    assert "recovery_time_mismatch" not in result["finding_codes"]


def test_gart_never_raises_on_empty_inputs():
    result = check_trace_artifacts([], {})
    # Empty trace + empty report: no CPA (completeness gap), no recovery,
    # and the empty report has no honest terminal reason → recovery mismatch.
    assert not result["g_art_consistent"]
    assert not result["g_art_complete"]
    assert "trace_cpa_unavailable" in result["finding_codes"]


def test_gart_does_not_use_m2_predicted_cpa():
    """M2 primary_cpa_m is the *predicted geometric* CPA, not the *achieved*
    CPA from scoring.arrow. G-ART must NOT read it — doing so would always
    produce a mismatch for head-on scenarios where predicted CPA ≈ 0 but
    achieved CPA is large."""
    rows = [
        {"topic": "/l3/m2/world_state", "sim_t": 100.0, "primary_cpa_m": 0.5},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 300.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "min_cpa_m": 500.0, "first_recovery_t": 300.0}
    result = check_trace_artifacts(rows, report)
    # No scoring CPA → trace_cpa_unavailable (completeness gap only)
    assert result["raw_min_cpa_m"] is None
    assert "trace_cpa_unavailable" in result["finding_codes"]
    assert "min_cpa_mismatch" not in result["finding_codes"]
    assert result["g_art_consistent"]  # no mismatch, just unavailable


def test_gart_uses_first_recovery_when_multiple_recovery_rows():
    rows = [
        {"topic": "/l3/m4/behavior_plan", "sim_t": 15.0, "behavior": 1},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 30.0, "behavior": 7},
        {"topic": "/l3/m4/behavior_plan", "sim_t": 50.0, "behavior": 7},
    ]
    report = {"scenario_id": "s", "first_recovery_t": 30.0}
    result = check_trace_artifacts(rows, report)
    assert "recovery_time_mismatch" not in result["finding_codes"]


def test_gart_always_returns_finding_codes_list():
    rows = [{"topic": "/sil/scoring", "sim_t": 20.0, "cpa_m": 210.0}]
    result = check_trace_artifacts(rows, {"scenario_id": "s", "min_cpa_m": 210.0})
    assert isinstance(result["finding_codes"], list)


def test_strict_gart_missing_alignment_is_explicit_g1_3_failure():
    result = check_trace_artifacts_strict([], {})

    assert not result["g_art_consistent"]
    assert not result["g_art_complete"]
    assert result["failure_stage"] == "G1.3"
    assert result["time_alignment_failure"]["reason"] == "clock_alignment_missing"
    json.dumps(result, allow_nan=False)


def test_strict_gart_missing_raw_clock_can_never_match_report_zero():
    alignment = _strict_alignment()
    report_zero = {
        "scenario_id": "s",
        "min_cpa_m": None,
        "first_recovery": {
            "canonical_s": 0.0,
            "raw_s": 0.0,
            "source": "sim_t",
            "alignment_id": alignment.alignment_id,
            "uncertainty_s": 0.1,
        },
    }
    rows = [
        {
            "record_id": "missing-clock",
            "topic": "/l3/m4/behavior_plan",
            "behavior": 7,
            "run_generation": 17,
        }
    ]

    result = check_trace_artifacts_strict(rows, report_zero, alignment=alignment)

    assert not result["g_art_consistent"]
    assert result["failure_stage"] == "G1.3"
    assert result["time_alignment_failure"]["reason"] == "clock_missing"
    assert result["raw_recovery"] is None


def test_strict_gart_recomputes_typed_recovery_time_and_preserves_provenance():
    alignment = _strict_alignment()
    scoring = _strict_row("score", "/sil/scoring", 20.0, cpa_m=210.0)
    recovery = _strict_row(
        "recovery", "/l3/m4/behavior_plan", 30.0, behavior=7
    )
    selected = select_event_time(recovery, alignment)
    report = {
        "scenario_id": "s",
        "min_cpa_m": 210.0,
        "first_recovery": {"record_id": "recovery", **selected.as_dict()},
    }

    result = check_trace_artifacts_strict(
        [scoring, recovery], report, alignment=alignment
    )

    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["raw_recovery"] == selected.as_dict()
    assert result["report_first_recovery"] == selected.as_dict()


def test_strict_gart_rejects_same_canonical_time_from_different_raw_source():
    alignment = ClockAlignment(
        lifecycle_run_generation=17,
        anchors=((0.0, 0.0), (60.0, 30.0)),
        transforms={
            "sim_t": ClockTransform(
                "sim_t", 1.0, 0.0, 0.0, 0, True, "simulation", 17
            ),
            "gnc_t": ClockTransform(
                "gnc_t", 0.5, 0.0, 0.0, 2, True, "gnc", 17
            ),
        },
        uncertainty_s=0.1,
        source_priority=("sim_t", "gnc_t"),
    )
    recovery = _strict_row(
        "recovery", "/l3/m4/behavior_plan", 30.0, behavior=7
    )
    injected_report_row = {
        "record_id": "injected-report-time",
        "gnc_t": 60.0,
        "source_domain": "gnc",
        "run_generation": 17,
    }
    report = {
        "scenario_id": "s",
        "first_recovery": {
            "record_id": "recovery",
            **select_event_time(injected_report_row, alignment).as_dict(),
        },
    }

    result = check_trace_artifacts_strict(
        [recovery], report, alignment=alignment
    )

    assert not result["g_art_consistent"]
    assert "recovery_time_mismatch" in result["finding_codes"]


def test_strict_gart_validates_sequence_backjump():
    alignment = _strict_alignment()
    rows = [
        _strict_row("later", "/sil/scoring", 20.0, cpa_m=210.0),
        _strict_row("earlier", "/l3/m4/behavior_plan", 10.0, behavior=7),
    ]

    result = check_trace_artifacts_strict(rows, {}, alignment=alignment)

    assert not result["g_art_consistent"]
    assert result["time_alignment_failure"]["reason"] == "canonical_time_backjump"


def _qualified_scoring_row(separation_m: float) -> dict:
    return {
        "topic": "/sil/scoring",
        "capture_seq": 1,
        "canonical_t_s": 20.0,
        "time_uncertainty_s": 0.1,
        "alignment_id": "a" * 64,
        "payload": {"separation_nm": separation_m / 1852.0},
    }


def _qualified_recovery_row(canonical_t_s: float = 30.0) -> dict:
    return {
        "topic": "/l3/m4/behavior_plan",
        "capture_seq": 2,
        "canonical_t_s": canonical_t_s,
        "time_uncertainty_s": 0.1,
        "alignment_id": "a" * 64,
        "payload": {"behavior": 7},
    }


def _qualified_report(
    physical_separation_m: float,
    scoring_separation_m: float,
) -> dict:
    return {
        "schema_version": 1,
        "scenario_id": "s",
        "run_id": "r",
        "evidence_metrics": {
            "physical_minimum_separation_m": physical_separation_m,
            "scoring_minimum_separation_m": scoring_separation_m,
        },
    }


def test_qualified_gart_compares_physical_scoring_and_report_independently():
    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0)],
        physical_minimum_separation_m=500.0,
        report=_qualified_report(500.0, 500.0),
        distance_tolerance_m=1.0,
    )

    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["finding_codes"] == []
    assert result["physical_minimum_separation_m"] == 500.0
    assert result["scoring_minimum_separation_m"] == pytest.approx(500.0)


def test_qualified_gart_detects_scoring_and_report_collusion():
    result = check_qualified_artifacts(
        [_qualified_scoring_row(400.0)],
        physical_minimum_separation_m=500.0,
        report=_qualified_report(400.0, 400.0),
        distance_tolerance_m=1.0,
    )

    assert not result["g_art_consistent"]
    assert result["g_art_complete"]
    assert "physical_scoring_separation_mismatch" in result["finding_codes"]
    assert "physical_report_separation_mismatch" in result["finding_codes"]
    assert "scoring_report_separation_mismatch" not in result["finding_codes"]


def test_qualified_gart_rejects_per_sample_physical_scoring_mismatch():
    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0)],
        physical_minimum_separation_m=500.0,
        physical_samples_consistent=False,
        report=_qualified_report(500.0, 500.0),
        distance_tolerance_m=1.0,
    )

    assert not result["g_art_consistent"]
    assert result["g_art_complete"]
    assert "physical_scoring_sample_mismatch" in result["finding_codes"]


def test_qualified_gart_detects_report_only_mutation():
    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0)],
        physical_minimum_separation_m=500.0,
        report=_qualified_report(600.0, 600.0),
        distance_tolerance_m=1.0,
    )

    assert not result["g_art_consistent"]
    assert result["g_art_complete"]
    assert "physical_report_separation_mismatch" in result["finding_codes"]
    assert "scoring_report_separation_mismatch" in result["finding_codes"]


def test_qualified_gart_missing_scoring_is_incomplete_without_substitution():
    result = check_qualified_artifacts(
        [],
        physical_minimum_separation_m=500.0,
        report=_qualified_report(500.0, 500.0),
        distance_tolerance_m=1.0,
    )

    assert result["g_art_consistent"]
    assert not result["g_art_complete"]
    assert "scoring_separation_unavailable" in result["finding_codes"]
    assert result["scoring_minimum_separation_m"] is None


def test_qualified_gart_rejects_false_report_recovery_claim():
    report = _qualified_report(500.0, 500.0)
    report["first_recovery"] = {
        "capture_seq": 2,
        "topic": "/l3/m4/behavior_plan",
        "canonical_t_s": 30.0,
        "alignment_id": "a" * 64,
        "time_uncertainty_s": 0.1,
    }

    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0)],
        physical_minimum_separation_m=500.0,
        report=report,
        distance_tolerance_m=1.0,
        recovery_required=True,
    )

    assert not result["g_art_consistent"]
    assert not result["g_art_complete"]
    assert "recovery_time_mismatch" in result["finding_codes"]
    assert "trace_recovery_unavailable" in result["finding_codes"]


def test_qualified_gart_accepts_honest_sut_red_without_recovery():
    report = _qualified_report(500.0, 500.0)
    report["first_recovery"] = None

    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0)],
        physical_minimum_separation_m=500.0,
        report=report,
        distance_tolerance_m=1.0,
        recovery_required=True,
    )

    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["g_art_ok"]
    assert "trace_recovery_unavailable" in result["finding_codes"]


def test_qualified_gart_accepts_report_reference_to_first_recovery_row():
    recovery = _qualified_recovery_row()
    report = _qualified_report(500.0, 500.0)
    report["first_recovery"] = {
        key: recovery[key]
        for key in (
            "capture_seq",
            "topic",
            "canonical_t_s",
            "alignment_id",
            "time_uncertainty_s",
        )
    }

    result = check_qualified_artifacts(
        [_qualified_scoring_row(500.0), recovery],
        physical_minimum_separation_m=500.0,
        report=report,
        distance_tolerance_m=1.0,
        recovery_required=True,
    )

    assert result["g_art_consistent"]
    assert result["g_art_complete"]
    assert result["raw_first_recovery"] == report["first_recovery"]
