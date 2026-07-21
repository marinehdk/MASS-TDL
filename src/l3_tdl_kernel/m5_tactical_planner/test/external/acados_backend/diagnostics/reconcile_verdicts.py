#!/usr/bin/env python3
"""Attach independent-oracle scope to acados diagnostic verdicts.

Also enriches every verdict with an X4 failure_classification field
(Category 1-4 automated root-cause classifier).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import x4_failure_classifier as x4  # noqa: E402


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def _enrich_with_failure_classification(verdict_path: pathlib.Path,
                                         verdict: dict) -> None:
    """LX-T2: Add X4 failure_classification to an existing verdict dict.

    Looks for sibling diagnostic files (solution.json, derivative_diagnostics.json,
    gnc_executability.json, qp_statistics.json) alongside the verdict to provide
    richer evidence to the classifier.
    """
    parent = verdict_path.parent

    # Build solver_output from sibling files.
    solver_output: dict = {}
    for fname, extractor in [
        ("solution.json", _load_solution_for_classifier),
        ("qp_statistics.json", _load_qp_stats_for_classifier),
        ("derivative_diagnostics.json", _load_derivative_diag_for_classifier),
        ("gnc_executability.json", _load_gnc_exec_for_classifier),
    ]:
        fpath = parent / fname
        if fpath.exists():
            extractor(fpath, solver_output)

    # Enrich (mutates verdict in-place).
    x4.enrich_verdict(verdict, solver_output=solver_output)


def _load_solution_for_classifier(path: pathlib.Path,
                                   output: dict) -> None:
    sol = json.loads(path.read_text(encoding="utf-8"))
    x4._maybe_set(output, "raw_status", sol.get("raw_status"))
    x4._maybe_set(output, "sqp_iter", sol.get("sqp_iter"))
    x4._maybe_set(output, "cost", sol.get("cost"))
    fw = sol.get("final_warm")
    if isinstance(fw, dict):
        x4._maybe_set(output, "raw_status", fw.get("raw_status"))
        x4._maybe_set(output, "sqp_iter", fw.get("sqp_iter"))
        x4._maybe_set(output, "cost", fw.get("cost"))
        x4._maybe_set(output, "nlp_residuals", fw.get("nlp_residuals"))
        x4._maybe_set(output, "qp_status_history", fw.get("qp_status_history"))
        x4._maybe_set(output, "qp_iter_history", fw.get("qp_iter_history"))
        x4._maybe_set(output, "raw_semantic", fw.get("raw_semantic"))


def _load_qp_stats_for_classifier(path: pathlib.Path,
                                   output: dict) -> None:
    qps = json.loads(path.read_text(encoding="utf-8"))
    x4._maybe_set(output, "raw_status", qps.get("raw_nlp_status"))
    x4._maybe_set(output, "sqp_iter", qps.get("sqp_iter"))
    x4._maybe_set(output, "nlp_residuals",
                  qps.get("nlp_residuals_stat_eq_ineq_comp"))
    x4._maybe_set(output, "qp_status_history", qps.get("qp_status_history"))
    x4._maybe_set(output, "qp_iter_history", qps.get("qp_iter_history"))
    x4._maybe_set(output, "raw_semantic", qps.get("raw_nlp_status_semantic"))


def _load_derivative_diag_for_classifier(path: pathlib.Path,
                                          output: dict) -> None:
    diag = json.loads(path.read_text(encoding="utf-8"))
    has_cv = x4._detect_constraint_violations_from_derivative_diag(diag)
    if has_cv:
        x4._maybe_set(output, "has_constraint_violations", True)


def _load_gnc_exec_for_classifier(path: pathlib.Path,
                                   output: dict) -> None:
    gnc = json.loads(path.read_text(encoding="utf-8"))
    x4._maybe_set(output, "gnc_executability", gnc)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence-root", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = args.evidence_root
    reference_root = root / "fresh_production_config"
    oracle_by_case = {}
    for path in reference_root.glob("*/reference_oracle.json"):
        oracle_by_case[path.parent.name] = json.loads(path.read_text(encoding="utf-8"))

    updated = []
    verdict_paths = list(reference_root.glob("*/verdict.json")) + list((root / "phase2").glob("*/*/verdict.json"))
    for path in verdict_paths:
        case_id = path.parent.name
        verdict = json.loads(path.read_text(encoding="utf-8"))
        raw_status = verdict.get("raw_status")
        oracle = oracle_by_case.get(case_id)
        if verdict.get("no_solver_run") or verdict.get("supported") is False:
            if oracle is not None:
                verdict["reference_feasibility"] = oracle["reference_status"]
            verdict["classification"] = "UNSUPPORTED_NOT_EXECUTED"
            verdict["full_mid_to_l4_classification"] = "NOT_APPLICABLE_NO_ACADOS_RUN"
            write_json(path, verdict)
            updated.append(str(path))
            continue
        if oracle is None:
            if case_id == "no_target_control":
                verdict["reference_feasibility"] = "NOT_APPLICABLE_CONTROL_CASE"
                verdict["classification"] = "CONTROL_CASE_MIXED_COLD_MAXITER_WARM_SUCCESS"
                write_json(path, verdict)
                updated.append(str(path))
            continue
        reference_status = oracle["reference_status"]
        if reference_status == "REFERENCE_FEASIBLE":
            classification = ("REFERENCE_FEASIBLE + ACADOS_SUCCESS" if raw_status == 0
                              else "REFERENCE_FEASIBLE + ACADOS_FAILURE")
        else:
            classification = "OPEN"
        verdict["reference_feasibility"] = reference_status
        verdict["classification"] = classification
        verdict["classification_scope"] = oracle["basis"]
        verdict["full_mid_to_l4_classification"] = ("REFERENCE_UNKNOWN + ACADOS_SUCCESS" if raw_status == 0
                                                      else "REFERENCE_UNKNOWN + ACADOS_FAILURE")
        verdict["gnc_executability"] = "OPEN"
        verdict["independent_gnc_review_performed"] = True
        verdict.setdefault("phase0_subgates", {})["independent_reference_oracle"] = reference_status == "REFERENCE_FEASIBLE"
        verdict["phase0_subgates"]["independent_gnc_executability"] = False
        verdict["phase0_subgates"]["independent_gnc_review_performed"] = True
        if path.parent.parent.name == "fresh_production_config":
            verdict["evidence_complete_phase0"] = True
        verdict["reference_oracle_path"] = str(reference_root / case_id / "reference_oracle.json")
        if case_id == "rule14_ho_live_dispatch_749728000002":
            verdict["live_input_provenance"] = (
                "Exact projection of real dispatcher solve-boundary capture; strict SIL G-ART manifest pending/valid_data=false.")

        # LX-T2: Enrich with X4 failure classification.
        _enrich_with_failure_classification(path, verdict)

        write_json(path, verdict)
        updated.append(str(path))
    print(json.dumps({"updated_count": len(updated), "paths": updated}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
