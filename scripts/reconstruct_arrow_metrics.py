#!/usr/bin/env python3
import json
import os
import math
from pathlib import Path
import pyarrow as pa
import pyarrow.ipc as ipc
import yaml

SCENARIOS = [
    "colreg-rule17-cr-so",
    "colreg-rule17-cr-so-2",
    "colreg-rule14-ho-port",
    "colreg-rule15-ms",
    "colreg-rule13-15-ms",
    "colreg-ms-headon-cross"
]

def main():
    runs_dir = Path("runs")
    if not runs_dir.exists():
        print("Error: runs directory not found.")
        return

    # Load the telemetry results
    results_path = Path("runs/batch_colregs_results.json")
    if not results_path.exists():
        print("Error: batch_colregs_results.json not found.")
        return

    with open(results_path) as f:
        results = json.load(f)

    # 1. Group run directories by type
    orch_runs = []
    score_runs = []
    for d in runs_dir.iterdir():
        if d.is_dir() and d.name.startswith("run-"):
            try:
                ts = int(d.name.split("-")[1], 16)
            except Exception:
                continue
            
            if (d / "scenario.yaml").exists():
                orch_runs.append((ts, d))
            elif (d / "scoring.arrow").exists():
                score_runs.append((ts, d))

    print(f"Found {len(orch_runs)} orchestrator runs and {len(score_runs)} scoring runs.")

    # 2. Match orchestrator runs to scoring runs and extract metrics
    for o_ts, o_dir in sorted(orch_runs):
        with open(o_dir / "scenario.yaml") as f:
            scen_data = yaml.safe_load(f)
        scen_id = scen_data.get("metadata", {}).get("scenario_id")
        if not scen_id:
            continue
        scen_base = scen_id.replace("-001-v1.0", "").replace("-002-v1.0", "")
        
        if scen_base not in SCENARIOS:
            continue

        # Find closest scoring run by timestamp
        best_score_dir = None
        min_diff = float("inf")
        for s_ts, s_dir in score_runs:
            diff = abs(o_ts - s_ts)
            if diff < min_diff:
                min_diff = diff
                best_score_dir = s_dir

        if best_score_dir and min_diff < 10000:
            arrow_path = best_score_dir / "scoring.arrow"
            print(f"Scenario: {scen_base} -> matched to scoring dir {best_score_dir.name} (diff {min_diff} ms)")
            
            cpa_min_nm = float("nan")
            rule_compliance_score = float("nan")
            applicable_rules = []
            
            try:
                with pa.memory_map(str(arrow_path), 'r') as source:
                    reader = ipc.open_file(source)
                    table = reader.read_all()
                arrow_data = table.to_pylist()
                if arrow_data:
                    cpas = [r["cpa_nm"] for r in arrow_data if r["cpa_nm"] is not None]
                    if cpas:
                        cpa_min_nm = min(cpas)
                    compliances = [r["rule_compliance"] for r in arrow_data if r["rule_compliance"] is not None]
                    if compliances:
                        rule_compliance_score = sum(compliances) / len(compliances)
                    rules = set(r["applicable_rule"] for r in arrow_data if r["applicable_rule"])
                    applicable_rules = list(rules)
            except Exception as e:
                print(f"  Failed to read scoring.arrow for {scen_base}: {e}")

            # Update results
            if scen_base in results:
                min_dcpa_m = cpa_min_nm * 1852.0 if not math.isnan(cpa_min_nm) else float("nan")
                results[scen_base]["min_cpa_m"] = min_dcpa_m
                results[scen_base]["min_cpa_nm"] = cpa_min_nm
                results[scen_base]["compliance_score"] = rule_compliance_score
                results[scen_base]["applicable_rules"] = applicable_rules
                
                # Determine compliance verdict
                compliance_verdict = "violated"
                if math.isnan(rule_compliance_score):
                    compliance_verdict = "unknown"
                elif rule_compliance_score >= 0.95:
                    compliance_verdict = "full"
                elif rule_compliance_score >= 0.45:
                    compliance_verdict = "partial"
                else:
                    compliance_verdict = "violated"
                results[scen_base]["compliance_verdict"] = compliance_verdict

    # Save updated results
    with open(results_path, "w") as f:
        json.dump(results, f, indent=2)

    print("\n\n==================================================")
    print("RECONSTRUCTED SUMMARY OF COLREGs INTEGRATION RESULTS:")
    print("==================================================")
    for scen, res in results.items():
        print(f"\nScenario: {scen} ({res['run_id']})")
        print(f"  CPA min: {res['min_cpa_m']:.1f} m ({res['min_cpa_nm']:.3f} NM) | Steering: {res['steer_dir']} ({res['steer_mag']:.1f}°)")
        print(f"  Compliance Verdict: {res['compliance_verdict'].upper()} (Score: {res['compliance_score']:.2f})")
        print(f"  Returned to Route: {res['returned_to_route']} (Final XTE: {res['final_xte']:.1f} m, Final Heading Dev: {res['final_heading_dev']:.1f}°)")
        print(f"  Transitions: {res['bp_transitions']}")
        print(f"  M5 Solver states: {res['solver_stats']}")

if __name__ == "__main__":
    main()
