#!/usr/bin/env python3
"""Attach independent-oracle scope to acados diagnostic verdicts."""

from __future__ import annotations

import argparse
import json
import pathlib


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


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
        write_json(path, verdict)
        updated.append(str(path))
    print(json.dumps({"updated_count": len(updated), "paths": updated}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
