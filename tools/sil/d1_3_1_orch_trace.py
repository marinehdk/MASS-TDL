#!/usr/bin/env python3
"""D1.3.1 Orchestration Trace Audit Tool — validates co-simulation trace integrity.

Phase 1: schema validation mode — actual trace data pending libcosim observer
integration. Run with --validate-schema to verify trace JSON structure.

Usage:
  python tools/sil/d1_3_1_orch_trace.py --trace trace.json
  python tools/sil/d1_3_1_orch_trace.py --trace trace.json --validate-schema
  python tools/sil/d1_3_1_orch_trace.py --trace trace.json --output-dir reports/
"""

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np

TRACE_SCHEMA = {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "type": "object",
    "required": ["steps", "expected_steps"],
    "properties": {
        "steps": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["index", "timestamp", "duration_us", "fmu_calls", "label"],
                "properties": {
                    "index": {"type": "integer"},
                    "timestamp": {"type": "number"},
                    "duration_us": {"type": "number"},
                    "fmu_calls": {"type": "array", "items": {"type": "string"}},
                    "label": {"type": "string"},
                },
            },
        },
        "expected_steps": {
            "type": "array",
            "items": {"type": "string"},
        },
    },
}


@dataclass
class OrchTraceReport:
    step_count: int
    consistency: dict[str, Any]
    completeness: dict[str, Any]
    fmu_order: dict[str, Any]
    timing: dict[str, Any]
    all_passed: bool


def parse_trace(trace_json_path: str) -> dict[str, Any]:
    path = Path(trace_json_path)
    return json.loads(path.read_text())


def audit_step_consistency(trace: dict[str, Any]) -> dict[str, Any]:
    steps = trace.get("steps", [])
    if len(steps) < 2:
        return {
            "passed": True,
            "dt_mean_s": None,
            "dt_std_s": None,
            "threshold_s": 0.001,
            "message": "insufficient steps",
        }

    timestamps = np.array([s["timestamp"] for s in steps])
    dts = np.diff(timestamps)
    dt_mean = float(np.mean(dts))
    dt_std = float(np.std(dts))
    passed = dt_std < 0.001

    return {
        "passed": passed,
        "dt_mean_s": dt_mean,
        "dt_std_s": dt_std,
        "threshold_s": 0.001,
        "message": "step timing consistent" if passed else f"dt std={dt_std:.6f}s exceeds threshold",
    }


def audit_step_completeness(trace: dict[str, Any]) -> dict[str, Any]:
    expected = set(trace.get("expected_steps", []))
    actual = {s.get("label", "") for s in trace.get("steps", [])}
    passed = expected == actual
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)

    return {
        "passed": passed,
        "expected_count": len(expected),
        "actual_count": len(actual),
        "missing": missing,
        "extra": extra,
        "message": "all expected steps present" if passed else f"missing={missing}, extra={extra}",
    }


def audit_fmu_order(trace: dict[str, Any]) -> dict[str, Any]:
    steps = trace.get("steps", [])
    violations: list[dict[str, Any]] = []
    reference: list[str] | None = None

    for step in steps:
        calls = step.get("fmu_calls", [])
        if reference is None:
            reference = calls
        elif calls != reference:
            violations.append({
                "step_index": step["index"],
                "expected_order": reference,
                "actual_order": calls,
            })

    return {
        "passed": len(violations) == 0,
        "reference_order": reference,
        "violation_count": len(violations),
        "violations": violations,
        "message": "FMU call order consistent" if not violations else f"{len(violations)} order violations",
    }


def audit_timing(trace: dict[str, Any]) -> dict[str, Any]:
    steps = trace.get("steps", [])
    if not steps:
        return {"passed": True, "p50_us": None, "p95_us": None, "p99_us": None, "overrun_count": 0}

    durations = np.array([s["duration_us"] for s in steps])
    p50 = float(np.percentile(durations, 50))
    p95 = float(np.percentile(durations, 95))
    p99 = float(np.percentile(durations, 99))
    overrun_count = int(np.sum(durations > 5000))
    passed = p99 < 5000 and overrun_count == 0

    return {
        "passed": passed,
        "p50_us": p50,
        "p95_us": p95,
        "p99_us": p99,
        "overrun_count": overrun_count,
        "threshold_us": 5000,
        "message": "all steps within 5000µs" if passed else f"P99={p99:.0f}µs, overruns={overrun_count}",
    }


def generate_audit_report(trace: dict[str, Any]) -> OrchTraceReport:
    consistency = audit_step_consistency(trace)
    completeness = audit_step_completeness(trace)
    fmu_order = audit_fmu_order(trace)
    timing = audit_timing(trace)

    all_passed = all([
        consistency["passed"],
        completeness["passed"],
        fmu_order["passed"],
        timing["passed"],
    ])

    return OrchTraceReport(
        step_count=len(trace.get("steps", [])),
        consistency=consistency,
        completeness=completeness,
        fmu_order=fmu_order,
        timing=timing,
        all_passed=all_passed,
    )


def _validate_schema(trace: dict[str, Any]) -> dict[str, Any]:
    errors: list[str] = []
    required = TRACE_SCHEMA.get("required", [])
    props = TRACE_SCHEMA.get("properties", {})

    for key in required:
        if key not in trace:
            errors.append(f"missing required key: {key}")

    for key, spec in props.items():
        if key not in trace:
            continue
        if spec.get("type") == "array" and not isinstance(trace[key], list):
            errors.append(f"{key}: expected array, got {type(trace[key]).__name__}")

    if "steps" in trace and isinstance(trace["steps"], list):
        step_schema = props["steps"].get("items", {}).get("required", [])
        step_props = props["steps"].get("items", {}).get("properties", {})
        for i, step in enumerate(trace["steps"]):
            for sk in step_schema:
                if sk not in step:
                    errors.append(f"steps[{i}]: missing required key: {sk}")
            for sk, ss in step_props.items():
                expected_type = ss.get("type", "")
                type_map = {"integer": int, "number": (int, float), "string": str, "array": list}
                py_type = type_map.get(expected_type, object)
                if sk in step and not isinstance(step[sk], py_type):
                    errors.append(
                        f"steps[{i}].{sk}: expected {expected_type}, got {type(step[sk]).__name__}"
                    )

    return {"valid": len(errors) == 0, "errors": errors}


def _print_report(report: OrchTraceReport) -> None:
    labels = ["consistency", "completeness", "fmu_order", "timing"]
    for label in labels:
        check = getattr(report, label)
        status = "PASS" if check["passed"] else "FAIL"
        print(f"  [{label:>14s}]  {status}  {check.get('message', '')}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="D1.3.1 Orchestration Trace Audit Tool"
    )
    parser.add_argument("--trace", required=True, help="Path to trace JSON file")
    parser.add_argument("--validate-schema", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=Path("reports/orch_trace"))
    args = parser.parse_args()

    print("Phase 1: schema validation mode — actual trace data pending libcosim observer integration")
    trace = parse_trace(args.trace)

    if args.validate_schema:
        schema_result = _validate_schema(trace)
        status = "VALID" if schema_result["valid"] else "INVALID"
        print(f"\n  Schema: {status}")
        for err in schema_result["errors"]:
            print(f"    - {err}")
        if not schema_result["valid"]:
            sys.exit(1)

    report = generate_audit_report(trace)
    _print_report(report)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    out = args.output_dir / "audit_report.json"
    out.write_text(json.dumps(asdict(report), indent=2, default=str))
    print(f"\nAudit report saved to {out}")

    sys.exit(0 if report.all_passed else 1)


if __name__ == "__main__":
    main()
