#!/usr/bin/env python3
"""Run single-module COLREGs oracles against an existing trace (Layer 2).

Reads a trace JSONL + scenario YAML produced by a prior run_6_scenarios run,
feeds them through colregs_oracle_adapter into the per-module oracles, and
writes a per-scenario module_oracle_results.json. Does NOT re-run the
simulator; this is offline diagnosis of an existing trace.

Usage:
    python3 scripts/run_colregs_module_oracle.py \\
        --trace runs/phase1_verify/colreg-rule14-ho.trace_current.jsonl \\
        --scenario colreg-rule14-ho \\
        --out runs/phase1_verify/colreg-rule14-ho.module_oracle.json

Design: docs/Design/SIL/COLREGs_测试体系设计_v1.md §5.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

# Allow importing tools.sil.* when run as a script.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.sil.colregs_oracle_adapter import (
    extract_compiled,
    extract_m6_output,
    extract_m4_events,
    extract_m2_truth_and_estimate,
    extract_m5_plan_output,
    extract_l4_actuation,
    extract_m7_veto,
)
from tools.sil.colregs_module_oracle import (
    evaluate_m6_oracle,
    evaluate_m4_oracle,
    evaluate_m2_oracle,
    evaluate_m5_oracle,
    evaluate_m7_oracle,
    evaluate_l4_oracle,
)


def load_trace(path: Path) -> list[dict]:
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def _scenario_yaml_path(scenario_id: str) -> Path:
    return Path(f"scenarios/COLREGs测试/{scenario_id}.yaml")


def _first_avoidance_onset(rows: list[dict]) -> float | None:
    """sim_t of the first M4 COLREG_AVOID sample (L4 command time proxy)."""
    for r in sorted(
        (r for r in rows if r.get("topic") == "/l3/m4/behavior_plan"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    ):
        if int(r.get("behavior", 0)) in (1, 2):
            return float(r.get("sim_t", 0.0))
    return None


def _first_release_after_avoidance(rows: list[dict]) -> float | None:
    """sim_t of the first TRANSIT (behavior=0) after the last avoidance sample."""
    m4 = sorted(
        (r for r in rows if r.get("topic") == "/l3/m4/behavior_plan"),
        key=lambda r: float(r.get("sim_t", 0.0)),
    )
    last_avoid_idx = -1
    for i, r in enumerate(m4):
        if int(r.get("behavior", 0)) in (1, 2, 7):
            last_avoid_idx = i
    if last_avoid_idx < 0:
        return None
    for r in m4[last_avoid_idx + 1:]:
        if int(r.get("behavior", 0)) == 0:
            return float(r.get("sim_t", 0.0))
    return None


def run_oracles(*, scenario_id: str, trace_path: Path,
                scenario_yaml: Path | None = None) -> dict:
    rows = load_trace(trace_path)
    yaml_path = scenario_yaml or _scenario_yaml_path(scenario_id)
    with open(yaml_path) as f:
        scenario_doc = yaml.safe_load(f)

    compiled = extract_compiled(scenario_doc)

    # ── M6 ────────────────────────────────────────────────────────────
    m6_output = extract_m6_output(rows)
    m6_result = evaluate_m6_oracle(compiled=compiled, m6_output=m6_output)

    # ── M4 ────────────────────────────────────────────────────────────
    m4_events, m6_cleared_t = extract_m4_events(rows, m6_rows=rows)
    m4_result = evaluate_m4_oracle(
        m4_events=m4_events, m6_conflict_cleared_t=m6_cleared_t)

    # ── M2 ────────────────────────────────────────────────────────────
    m2_rows = [r for r in rows if r.get("topic") == "/l3/m2/world_state"]
    truth, estimate = extract_m2_truth_and_estimate(compiled, m2_rows=m2_rows)
    m2_result = evaluate_m2_oracle(truth=truth, estimated=estimate)

    # ── M5 ────────────────────────────────────────────────────────────
    m5_output = extract_m5_plan_output(rows)
    m5_result = evaluate_m5_oracle(plan_output=m5_output)

    # ── M7 ────────────────────────────────────────────────────────────
    # unsafe_trajectory_present defaults False (conservative). The caller can
    # set it from the runner summary cpa_ok when a floor breach is known.
    m7_input = extract_m7_veto(rows)
    m7_result = evaluate_m7_oracle(
        unsafe_trajectory_vetoed=m7_input["unsafe_trajectory_vetoed"],
        safe_trajectory_vetoed=m7_input["safe_trajectory_vetoed"],
        unsafe_trajectory_present=m7_input["unsafe_trajectory_present"])

    # ── L4 ────────────────────────────────────────────────────────────
    cmd_t = _first_avoidance_onset(rows)
    if cmd_t is None:
        cmd_t = 0.0
    # Release time: first TRANSIT after the last avoidance sample.
    release_t = _first_release_after_avoidance(rows)
    l4_input = extract_l4_actuation(rows, command_t=cmd_t, release_t=release_t)
    l4_result = evaluate_l4_oracle(
        first_command_t=l4_input["first_command_t"],
        first_realized_t=l4_input["first_realized_t"],
        realized_heading_change_deg=l4_input["realized_heading_change_deg"])

    def _to_dict(res) -> dict:
        return {
            "module": res.module,
            "passed": res.passed,
            "failed_checks": res.failed_checks,
            "evidence": res.evidence,
        }

    results = {
        "scenario_id": scenario_id,
        "trace_path": str(trace_path),
        "compiled_rule": compiled["compiled_rule"],
        "own_role": compiled["own_role"],
        "modules": {
            "M2_WorldModel": _to_dict(m2_result),
            "M4_BehaviorArbiter": _to_dict(m4_result),
            "M5_TacticalPlanner": _to_dict(m5_result),
            "M6_COLREGsReasoner": _to_dict(m6_result),
            "M7_SafetySupervisor": _to_dict(m7_result),
            "L4_GuidanceAdapter": _to_dict(l4_result),
        },
    }
    n_pass = sum(1 for m in results["modules"].values() if m["passed"])
    results["summary"] = {
        "n_modules_passed": n_pass,
        "n_modules_total": len(results["modules"]),
        "all_passed": n_pass == len(results["modules"]),
    }
    return results


def _parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Run single-module COLREGs oracles against an existing trace.")
    p.add_argument("--trace", required=True, type=Path,
                   help="Path to the trace JSONL file.")
    p.add_argument("--scenario", required=True,
                   help="Scenario id (e.g. colreg-rule14-ho).")
    p.add_argument("--scenario-yaml", type=Path, default=None,
                   help="Override scenario YAML path (default: scenarios/COLREGs测试/<id>.yaml).")
    p.add_argument("--out", type=Path, default=None,
                   help="Output JSON path (default: <trace>.module_oracle.json).")
    return p.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    results = run_oracles(
        scenario_id=args.scenario,
        trace_path=args.trace,
        scenario_yaml=args.scenario_yaml,
    )
    out_path = args.out or args.trace.with_suffix(".module_oracle.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    s = results["summary"]
    print(f"\n{'='*60}")
    print(f"MODULE ORACLE RESULTS: {args.scenario}")
    print(f"{'='*60}")
    print(f"Compiled rule: {results['compiled_rule']} | role: {results['own_role']}")
    for name, m in results["modules"].items():
        verdict = "GREEN" if m["passed"] else "RED"
        checks = ", ".join(m["failed_checks"]) if m["failed_checks"] else "-"
        print(f"  [{verdict}] {name}: {checks}")
    print(f"\n{s['n_modules_passed']}/{s['n_modules_total']} modules GREEN")
    print(f"Results written to: {out_path}")
    return 0 if s["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
