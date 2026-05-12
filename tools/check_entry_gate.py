#!/usr/bin/env python3
"""MASS-L3 TDL V&V Phase Entry-Gate Checker.

Reads CI artifact JSON files and evaluates Phase Entry/Exit Gate conditions
as defined in docs/Design/V&V_Plan/00-vv-strategy-v0.1.md §2.

Usage:
    python tools/check_entry_gate.py --phase 1 [--artifacts-dir test-results/]

Artifact files expected in --artifacts-dir:
    pytest-report.json          pytest-json-report output
    imazu22_results.json        {"cpa_ge_200m_pct": float, "colreg_classification_rate_pct": float}
    coverage_cube.json          {"cells_lit": int, "total_cells": 1100}
    d1_3a_validation.json       {"turning_circle_error_pct": float}
    sil_results.json            {"pass_rate_pct": float, "tmr_ge_60s_pct": float, "total_scenarios": int}
    latency_report.json         {"avoidance_plan_p95_ms": float, "reactive_override_p95_ms": float,
                                 "mid_mpc_p95_ms": float, "bc_mpc_p95_ms": float,
                                 "m7_p95_ms": float, "dds_fmu_p95_ms": float, "dds_fmu_p99_ms": float}
    doer_checker_metrics.json   {"m1_odd_loc_ratio": float, "m1_odd_cyc_ratio": float,
                                 "m7_checker_loc_ratio": float, "m7_checker_cyc_ratio": float,
                                 "m7_sbom_intersection_size": int}
    sotif_coverage.json         {"triggering_condition_coverage_pct": float}

Exit codes:
    0  All automated gates PASS (manual items may remain — see output)
    1  One or more automated gates FAIL
    2  Usage error
"""
import argparse
import json
import sys
from pathlib import Path

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

PASS_STR = f"{GREEN}{BOLD}✓ PASS{RESET}"
FAIL_STR = f"{RED}{BOLD}✗ FAIL{RESET}"
MANUAL_STR = f"{YELLOW}{BOLD}? MANUAL{RESET}"


def _read_json(path: Path) -> dict | None:
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError:
        return None


def _get(data: dict, key_path: str):
    val = data
    for k in key_path.split("."):
        if not isinstance(val, dict):
            return None
        val = val.get(k)
    return val


def _check(artifacts: Path, json_file: str, key: str, op: str, threshold: float) -> tuple[bool | None, str]:
    data = _read_json(artifacts / json_file)
    if data is None:
        return False, f"artifact missing: {artifacts / json_file}"
    val = _get(data, key)
    if val is None:
        return False, f"key '{key}' not found in {json_file}"
    ops = {">=": float(val) >= threshold, "<=": float(val) <= threshold,
           ">": float(val) > threshold, "<": float(val) < threshold, "==": float(val) == threshold}
    ok = ops.get(op, False)
    return ok, f"{key} = {val} (required: {op} {threshold})"


def _check_file_exists(path: Path) -> tuple[bool, str]:
    return path.exists(), str(path)


def _check_pytest(artifacts: Path, pattern: str, min_count: int) -> tuple[bool, str]:
    data = _read_json(artifacts / "pytest-report.json")
    if data is None:
        return False, f"artifact missing: {artifacts / 'pytest-report.json'}"
    tests = data.get("tests", [])
    matching = [t for t in tests if pattern in t.get("nodeid", "")]
    passed = [t for t in matching if t.get("outcome") == "passed"]
    if not matching:
        return False, f"no tests matching '{pattern}'"
    ok = len(passed) >= min_count and len(passed) == len(matching)
    return ok, f"{len(passed)}/{len(matching)} passed (required: ≥ {min_count} and all matching pass)"


def _check_pytest_summary(artifacts: Path, min_passed: int, max_failed: int) -> tuple[bool, str]:
    data = _read_json(artifacts / "pytest-report.json")
    if data is None:
        return False, f"artifact missing: {artifacts / 'pytest-report.json'}"
    summary = data.get("summary", {})
    n_passed = summary.get("passed", 0)
    n_failed = summary.get("failed", 0)
    ok = n_passed >= min_passed and n_failed <= max_failed
    return ok, f"{n_passed} passed, {n_failed} failed (required: ≥ {min_passed} passed, ≤ {max_failed} failed)"


def _row(gate_id: str, label: str, ok: bool | None, detail: str, source: str) -> dict:
    return {"id": gate_id, "label": label, "ok": ok, "detail": detail, "source": source}


def phase1(a: Path) -> list[dict]:
    rows = []

    ok, d = _check_file_exists(Path("docs/Design/V&V_Plan/00-vv-strategy-v0.1.md"))
    rows.append(_row("E1.7", "[V&V] 00-vv-strategy-v0.1.md exists", ok, d, "D1.5 DoD"))

    ok, d = _check_pytest_summary(a, min_passed=39, max_failed=0)
    rows.append(_row("E1.6", "[CI] ≥ 39 passed, 0 failed", ok, d, "D1.2 CLOSED"))

    ok, d = _check_pytest(a, "imazu", min_count=22)
    rows.append(_row("E1.1", "[SIL] Imazu-22 22/22 PASS", ok, d, "SIL decisions §6 [E2]"))

    ok, d = _check(a, "imazu22_results.json", "cpa_ge_200m_pct", ">=", 95.0)
    rows.append(_row("E1.2", "[SIL] CPA ≥ 200m ratio ≥ 95% (Imazu-22)", ok, d, "SIL decisions §6 [E2]"))

    ok, d = _check(a, "imazu22_results.json", "colreg_classification_rate_pct", ">=", 95.0)
    rows.append(_row("E1.3", "[SIL] COLREG classification rate ≥ 95%", ok, d, "SIL decisions §6 [E2]"))

    ok, d = _check(a, "coverage_cube.json", "cells_lit", ">=", 10.0)
    rows.append(_row("E1.4", "[SIL] Coverage cube ≥ 10 / 1100 cells lit", ok, d, "SIL decisions §6"))

    ok, d = _check(a, "d1_3a_validation.json", "turning_circle_error_pct", "<=", 5.0)
    rows.append(_row("E1.5", "[SIL] D1.3a MMG turning circle error ≤ 5%", ok, d, "D1.3a DoD"))

    ok, d = _check_pytest(a, "m7", min_count=1)
    rows.append(_row("E1.8", "[SIL2→§2.5] M7 watchdog white-box ≥ 1 PASS", ok, d, "arch §11 + §2.5"))

    return rows


def phase2(a: Path) -> list[dict]:
    rows = []

    ok, d = _check(a, "coverage_cube.json", "cells_lit", ">=", 220.0)
    rows.append(_row("E2.1", "[SIL] Coverage cube ≥ 220 / 1100 cells", ok, d, "gantt D2.5"))

    ok, d = _check(a, "sil_results.json", "pass_rate_pct", ">=", 90.0)
    rows.append(_row("E2.2", "[SIL] 50-scenario pass rate ≥ 90% (first-run milestone)", ok, d, "gantt D2.4 DoD"))

    ok, d = _check(a, "sil_results.json", "pass_rate_pct", ">=", 95.0)
    rows.append(_row("E2.3", "[SIL] 50-scenario pass rate ≥ 95% (after fixes — Phase 2 exit)", ok, d, "gantt D2.5 DoD"))

    ok, d = _check(a, "sil_results.json", "tmr_ge_60s_pct", ">=", 100.0)
    rows.append(_row("E2.4", "[KPI] TMR ≥ 60s compliance 100%", ok, d, "arch §3.1 [R40 Veitch 2024]"))

    ok, d = _check(a, "latency_report.json", "avoidance_plan_p95_ms", "<=", 1000.0)
    rows.append(_row("E2.5", "[KPI] AvoidancePlan P95 ≤ 1000ms", ok, d, "gantt D1.5 scope"))

    ok, d = _check(a, "latency_report.json", "reactive_override_p95_ms", "<=", 200.0)
    rows.append(_row("E2.6", "[KPI] ReactiveOverrideCmd P95 ≤ 200ms", ok, d, "gantt D1.5 scope"))

    ok, d = _check(a, "latency_report.json", "mid_mpc_p95_ms", "<=", 500.0)
    rows.append(_row("E2.7", "[KPI] Mid-MPC P95 < 500ms", ok, d, "gantt D1.5 scope"))

    ok, d = _check(a, "latency_report.json", "bc_mpc_p95_ms", "<=", 150.0)
    rows.append(_row("E2.8", "[KPI] BC-MPC P95 < 150ms", ok, d, "gantt D1.5 scope"))

    ok, d = _check(a, "latency_report.json", "dds_fmu_p95_ms", "<=", 10.0)
    rows.append(_row("E2.9", "[KPI] dds-fmu bridge P95 ≤ 10ms (D1.3c)", ok, d, "SIL decisions §2.4"))

    ok, d = _check(a, "latency_report.json", "m7_p95_ms", "<=", 10.0)
    rows.append(_row("E2.11", "[SIL2→§2.5] M7 end-to-end P95 < 10ms", ok, d, "SIL decisions §2.4 + arch §11"))

    ok, d = _check(a, "doer_checker_metrics.json", "m1_odd_loc_ratio", ">=", 50.0)
    rows.append(_row("E2.12a", "[SIL2→§2.5] M1 ODD LOC ratio ≥ 50:1", ok, d, "D3.3a scope + §2.5"))

    ok, d = _check(a, "doer_checker_metrics.json", "m1_odd_cyc_ratio", ">=", 30.0)
    rows.append(_row("E2.12b", "[SIL2→§2.5] M1 ODD CYC ratio ≥ 30:1", ok, d, "D3.3a scope + §2.5"))

    ok, d = _check(a, "doer_checker_metrics.json", "m7_checker_loc_ratio", ">=", 50.0)
    rows.append(_row("E2.13a", "[SIL2→§2.5] M7 Checker LOC ratio ≥ 50:1", ok, d, "D3.3a scope + §2.5"))

    ok, d = _check(a, "doer_checker_metrics.json", "m7_checker_cyc_ratio", ">=", 30.0)
    rows.append(_row("E2.13b", "[SIL2→§2.5] M7 Checker CYC ratio ≥ 30:1", ok, d, "D3.3a scope + §2.5"))

    ok, d = _check(a, "doer_checker_metrics.json", "m7_sbom_intersection_size", "<=", 0.0)
    rows.append(_row("E2.13c", "[SIL2→§2.5] M7 SBOM ∩ Doer = ∅", ok, d, "D3.3a scope + §2.5"))

    rows.append(_row(
        "E2.10", "[CERT] HARA v1.0 available (D2.7, ≥ 30 hazards)", None,
        "MANUAL: check docs/Design/Safety/HARA/ — document exists and covers ≥ 30 hazard entries → SIF → SIL → mitigation",
        "gantt D2.7",
    ))

    return rows


def phase3(a: Path) -> list[dict]:
    rows = []

    ok, d = _check(a, "sil_results.json", "total_scenarios", ">=", 1000.0)
    rows.append(_row("E3.1", "[SIL] ≥ 1000 scenarios complete (D3.6)", ok, d, "gantt D3.6"))

    ok, d = _check(a, "coverage_cube.json", "cells_lit", ">=", 880.0)
    rows.append(_row("E3.2", "[SIL] Coverage ≥ 880 / 1100 cells (80%)", ok, d, "gantt D3.6"))

    ok, d = _check(a, "sotif_coverage.json", "triggering_condition_coverage_pct", ">=", 80.0)
    rows.append(_row("E3.3", "[SIL] SOTIF triggering condition coverage ≥ 80%", ok, d, "gantt D3.6"))

    ok, d = _check(a, "sil_results.json", "pass_rate_pct", ">=", 95.0)
    rows.append(_row("E3.4", "[SIL] All-scenario pass rate ≥ 95%", ok, d, "D1.7 rubric"))

    rows.append(_row(
        "E3.5", "[CERT] FMEDA M7 table v1.0 complete (D2.7)", None,
        "MANUAL: check docs/Design/Safety/FMEDA/ — SFF field populated for M7",
        "gantt D2.7",
    ))

    rows.append(_row(
        "E3.6", "[SIL2→§2.5] M7 SFF ≥ [TBD-HAZID-SFF-001]", None,
        "MANUAL: value locked after HAZID RUN-001 (8/19) → FMEDA M7 table D2.7 → §2.5 backfill",
        "§2.5 + IEC 61508 SIL 2",
    ))
    rows.append(_row(
        "E3.7", "[SIL2→§2.5] MRC trigger latency ≤ [TBD-HAZID-MRC-001]", None,
        "MANUAL: fault injection test report required; threshold from HAZID RUN-001 (8/19)",
        "§2.5 + arch §11",
    ))
    rows.append(_row(
        "E3.8", "[SIL2→§2.5] MRC activation rate = 100% (injection scenarios)", None,
        "MANUAL: fault injection test report required (≥ 10 M1/M2/M7 single-failure scenarios)",
        "§2.5 + arch §11",
    ))

    return rows


def phase4(_: Path) -> list[dict]:
    manual = [
        ("E4.1", "[HIL] HIL test duration ≥ 800h (D4.1/D4.2)",
         "HIL runner log + D4.1/D4.2 DoD sign-off"),
        ("E4.2", "[HIL] HIL continuous run acceptance (D4.2 DoD)",
         "HIL test report"),
        ("E4.3", "[CERT] TÜV/DNV/BV SIL 2 assessment report (D4.3)",
         "D4.3 evidence store; must be from CCS-recognised body"),
        ("E4.4", "[CERT] CCS AIP submission receipt (D4.4)",
         "D4.4 DoD; submission confirmation letter"),
        ("E4.5", "[SIL2→§2.5] Third-party assessment: M1 ODD — 'acceptable'",
         "D4.3 report §M1 section"),
        ("E4.6", "[SIL2→§2.5] Third-party assessment: M7 Checker — 'acceptable'",
         "D4.3 report §M7 section"),
        ("E4.7", "[SIL2→§2.5] Third-party assessment: MRC path — 'acceptable'",
         "D4.3 report §MRC section"),
    ]
    return [_row(gid, label, None, f"MANUAL: {detail}", "gantt Phase 4") for gid, label, detail in manual]


RUNNERS = {1: phase1, 2: phase2, 3: phase3, 4: phase4}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="MASS-L3 TDL V&V Plan v0.1 — Phase Entry-Gate Checker",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--phase", type=int, required=True, choices=[1, 2, 3, 4],
                        help="Phase number to check (1-4)")
    parser.add_argument("--artifacts-dir", default="test-results", type=Path,
                        help="Directory containing CI artifact JSON files (default: test-results/)")
    args = parser.parse_args()

    print(f"\n{BOLD}{CYAN}MASS-L3 TDL V&V Plan v0.1 — Phase {args.phase} Entry-Gate Check{RESET}")
    print(f"  Artifacts : {args.artifacts_dir.resolve()}")
    print(f"  Spec      : docs/Design/V&V_Plan/00-vv-strategy-v0.1.md §2.{args.phase}")
    print("=" * 72)

    rows = RUNNERS[args.phase](args.artifacts_dir)

    n_pass = n_fail = n_manual = 0
    for r in rows:
        ok = r["ok"]
        if ok is True:
            mark = PASS_STR
            n_pass += 1
        elif ok is False:
            mark = FAIL_STR
            n_fail += 1
        else:
            mark = MANUAL_STR
            n_manual += 1
        print(f"  {mark}  [{r['id']}] {r['label']}")
        print(f"           detail : {r['detail']}")
        print(f"           source : {r['source']}")
        print()

    print("=" * 72)
    print(f"  {GREEN}{n_pass} PASS{RESET}  {RED}{n_fail} FAIL{RESET}  {YELLOW}{n_manual} MANUAL-CHECK{RESET}\n")

    if n_fail > 0:
        print(f"{RED}Phase {args.phase} gate NOT cleared. Fix failing items above.{RESET}\n")
        return 1

    if n_manual > 0:
        print(f"{YELLOW}Phase {args.phase}: automated checks PASS. "
              f"Complete {n_manual} MANUAL item(s) above before sign-off.{RESET}\n")
        return 0

    print(f"{GREEN}{BOLD}Phase {args.phase} gate CLEARED.{RESET}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
