"""G-ART artifact consistency gate (design §7).

Lightweight pure-std checker that compares verdict.json against timeline
JSON on three dimensions: identifier, numeric, ordering. Any mismatch ->
G-ART RED, and the failure is attributed to the evaluation system (not the
SUT), so "evaluator inconsistency" is never mistaken for "system bug".
"""
from __future__ import annotations


def check_consistency(verdict: dict, timeline: dict,
                      trace_hash: str | None = None,
                      dashboard_hash: str | None = None,
                      *, cpa_tol_m: float = 1.0, time_tol_s: float = 1.0) -> dict:
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
