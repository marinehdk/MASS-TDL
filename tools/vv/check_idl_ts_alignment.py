#!/usr/bin/env python3
"""Check ROS2 IDL <-> web/src/types/sat.ts type alignment (D2.5 section 6.2).

Ground truth from spec section 6.2 IDL <-> Frontend matrix.
Usage:
    python tools/vv/check_idl_ts_alignment.py [--sat-ts web/src/types/sat.ts]
Exit 0 if all spec-listed fields are present and type-compatible; 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Ground truth from spec section 6.2 and D2.5 Task 1 plan: 17 fields across 4 interfaces.
GROUND_TRUTH: dict[str, dict[str, str]] = {
    "IvpContribution": {
        "direction_deg": "number",
        "cost": "number",
        "label": "string",
    },
    "ColregsChainLayer": {
        "layer": "number",
        "label": "string",
        "conclusion": "string",
    },
    "TrajectoryCandidate": {
        "id": "number",
        "points": "Array",
        "cost": "number",
        "is_optimal": "boolean",
        "type": "string",
    },
    "SotifMetrics": {
        "ais_radar_consistency_sigma": "number",
        "target_predictability_rms_m": "number",
        "perception_coverage_pct": "number",
        "colregs_parse_failures": "number",
        "comm_link_rtt_ms": "number",
        "checker_veto_rate_pct": "number",
    },
}


def _parse_ts_interface(src: str, iface_name: str) -> dict[str, str]:
    """Extract {field: type} from a TypeScript interface block."""
    pattern = rf"interface\s+{re.escape(iface_name)}\s*\{{"
    match = re.search(pattern, src)
    if not match:
        return {}
    start = match.end()
    depth = 1
    end = start
    while end < len(src) and depth:
        if src[end] == "{":
            depth += 1
        elif src[end] == "}":
            depth -= 1
        end += 1
    if depth:
        return {}

    fields: dict[str, str] = {}
    for line in src[start : end - 1].splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        field_match = re.match(r"(\w+)\??\s*:\s*(.+?)\s*;?\s*(?://.*)?$", line)
        if field_match:
            fields[field_match.group(1)] = field_match.group(2).strip().rstrip(";")
    return fields


def _is_type_compatible(expected_type_hint: str, actual_type: str) -> bool:
    actual = actual_type.lower()
    expected = expected_type_hint.lower()
    if expected in actual:
        return True

    union_parts = [part.strip() for part in actual_type.split("|")]
    if expected == "number":
        return bool(union_parts) and all(re.fullmatch(r"\d+(?:\.\d+)?", part) for part in union_parts)
    if expected == "string":
        return bool(union_parts) and all(
            re.fullmatch(r"['\"][^'\"]+['\"]", part) for part in union_parts
        )
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="IDL <-> sat.ts alignment check")
    parser.add_argument(
        "--sat-ts",
        default="web/src/types/sat.ts",
        help="Path to web/src/types/sat.ts",
    )
    parser.add_argument(
        "--output",
        default="test-results/idl_alignment_report.json",
        help="JSON report output path",
    )
    args = parser.parse_args()

    src = Path(args.sat_ts).read_text()
    mismatches: list[dict[str, str | None]] = []
    total_checked = 0

    for iface, expected_fields in GROUND_TRUTH.items():
        actual_fields = _parse_ts_interface(src, iface)
        for field, expected_type_hint in expected_fields.items():
            total_checked += 1
            if field not in actual_fields:
                mismatches.append(
                    {
                        "interface": iface,
                        "field": field,
                        "reason": "MISSING",
                        "expected_type": expected_type_hint,
                        "actual_type": None,
                    }
                )
                continue

            actual_type = actual_fields[field]
            if not _is_type_compatible(expected_type_hint, actual_type):
                mismatches.append(
                    {
                        "interface": iface,
                        "field": field,
                        "reason": "TYPE_MISMATCH",
                        "expected_type": expected_type_hint,
                        "actual_type": actual_type,
                    }
                )

    report = {
        "total_fields_checked": total_checked,
        "mismatches_count": len(mismatches),
        "mismatches": mismatches,
        "sat_ts_path": str(args.sat_ts),
    }

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))

    if mismatches:
        print(f"\n[FAIL] {len(mismatches)} mismatch(es) found.", file=sys.stderr)
        return 1
    print(f"[PASS] All {total_checked} fields aligned.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
