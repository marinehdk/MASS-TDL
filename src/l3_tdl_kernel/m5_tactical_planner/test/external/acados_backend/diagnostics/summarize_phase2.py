#!/usr/bin/env python3
"""Build machine-readable Phase 2 cold/warm matrix from per-case evidence."""

from __future__ import annotations

import argparse
import csv
import json
import pathlib

REQUIRED_VARIANTS = {
    "baseline", "starboard_seed", "partial_condensing", "hpipm_robust",
    "cond_ric_alg_only", "qp_ric_alg_only", "residual_slack_init",
    "dimensionless_cpa", "deterministic_collision", "feasible_qp_funnel",
    "reachability_scheduled_bounds", "gershgorin_derived_lm",
    "project_regularization", "gauss_newton_hessian",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase2", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for path in sorted(args.phase2.glob("*/*/qp_statistics.json")):
        variant, case_id = path.parts[-3], path.parts[-2]
        stats = json.loads(path.read_text(encoding="utf-8"))
        config_path = path.parent / "solver_config.json"
        config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.exists() else {}
        cold, warm = stats.get("cold", {}), stats.get("warm", {})
        rows.append({
            "variant": variant, "case_id": case_id,
            "single_variable": config.get("single_variable"),
            "supported": config.get("supported", True),
            "cold_count": cold.get("count", 0), "cold_success": cold.get("success_count", 0),
            "cold_status_counts": cold.get("raw_status_counts", {}),
            "cold_median_sqp_iter": cold.get("median_sqp_iter"),
            "warm_count": warm.get("count", 0), "warm_success": warm.get("success_count", 0),
            "warm_status_counts": warm.get("raw_status_counts", {}),
            "warm_median_sqp_iter": warm.get("median_sqp_iter"),
            "required_counts_complete": cold.get("count", 0) == 5 and warm.get("count", 0) == 20,
            "evidence_dir": str(path.parent),
        })
    required_cases = {"target2500_exact", "rule14_ho_5000_ab_canonical",
                      "rule14_ho_live_dispatch_749728000002"}
    variants = sorted({row["variant"] for row in rows if row["case_id"] in required_cases})
    coverage = {}
    for variant in variants:
        present = {row["case_id"] for row in rows if row["variant"] == variant and row["required_counts_complete"]}
        coverage[variant] = {"complete_cases": sorted(present),
                             "three_case_complete": required_cases.issubset(present)}
    output = {"rows": rows, "coverage": coverage,
              "required_variants": sorted(REQUIRED_VARIANTS),
              "all_required_three_case_arms_complete": all(
                  coverage.get(variant, {}).get("three_case_complete", False)
                  for variant in REQUIRED_VARIANTS),
              "interaction_status": coverage.get("lm_dimensionless_interaction", {"three_case_complete": False}),
              "reference_evidence": "Read each fresh_production_config/<case>/reference_oracle.json; classification is scoped there, not inferred from ablation output."}
    (args.output / "ablation_matrix.json").write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    fieldnames = ["variant", "case_id", "single_variable", "supported", "cold_count", "cold_success",
                  "cold_status_counts", "cold_median_sqp_iter", "warm_count", "warm_success",
                  "warm_status_counts", "warm_median_sqp_iter", "required_counts_complete", "evidence_dir"]
    with (args.output / "ablation_matrix.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames); writer.writeheader()
        for row in rows:
            item = row.copy()
            item["cold_status_counts"] = json.dumps(item["cold_status_counts"], sort_keys=True)
            item["warm_status_counts"] = json.dumps(item["warm_status_counts"], sort_keys=True)
            writer.writerow(item)
    print(json.dumps({"row_count": len(rows), "coverage": coverage}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
