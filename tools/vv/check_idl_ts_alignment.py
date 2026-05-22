#!/usr/bin/env python3
"""Check ROS2 IDL field alignment against web/src/types/sat.ts (D2.5 spec §6.2).

Ground truth from §6.2 IDL-Frontend matrix: 20 fields across 4 interfaces.

Usage:
    python tools/vv/check_idl_ts_alignment.py [--sat-ts web/src/types/sat.ts]

Exit 0 if all fields are present and type-compatible; 1 on any mismatch.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# Ground truth from D2.5 spec §6.2 Table 2 — 20 fields across 4 interfaces
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

# TypeScript literal unions are subtype-compatible with their primitive base
_LITERAL_UNION_BASE: dict[str, tuple[str, ...]] = {
    "number": ("number",),
    "string": ("string",),
    "boolean": ("boolean",),
}


def _parse_ts_interface(src: str, iface_name: str) -> dict[str, str]:
    """Extract {field: type} from a TypeScript interface block.

    Uses brace-depth counting so generic types like ``Array<{...}>``
    do not truncate the body early.
    """
    m = re.search(
        rf"interface\s+{re.escape(iface_name)}\s*\{{",
        src,
    )
    if not m:
        return {}
    # Brace-depth scan to find the matching closing brace
    pos = m.end()
    depth = 1
    while pos < len(src) and depth > 0:
        ch = src[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    body = src[m.end() : pos - 1]

    fields: dict[str, str] = {}
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        fm = re.match(r"(\w+)\??\s*:\s*(.+?)\s*;?\s*(?://.*)?$", line)
        if fm:
            fields[fm.group(1)] = fm.group(2).strip().rstrip(";")
    return fields


def _is_type_compatible(ts_type: str, expected: str) -> bool:
    """Check if a TS type annotation matches an expected ground-truth type.

    Handles:
      - exact match  (``number`` == ``number``)
      - optional     (``number`` -> ``number``)
      - generic prefix  (``Array<T>`` -> ``Array``)
      - literal unions  (``'mid_mpc' | 'bc_mpc'`` -> ``string``)
    """
    if ts_type == expected:
        return True

    # Generic type: "Array<...>" is type-compatible with "Array"
    if ts_type.startswith(expected + "<"):
        return True

    # Collapse optional flags: "string | undefined" or "string | null"
    cleaned = re.sub(r"\s*\|\s*(?:undefined|null)", "", ts_type).strip()

    if cleaned == expected:
        return True

    # Literal-union check: 'foo' | 'bar' | 'baz'  ->  string
    # Detect patterns like 'value1' | 'value2' or 1 | 2 | 3
    parts = [p.strip() for p in cleaned.split("|")]
    if len(parts) > 1:
        if expected == "string":
            if all(p.startswith("'") and p.endswith("'") for p in parts):
                return True
        if expected == "number":
            if all(p.lstrip("-").isdigit() or re.match(r"^-?\d+\.?\d*$", p) for p in parts):
                return True
        if expected == "boolean":
            if all(p in ("true", "false") for p in parts):
                return True

    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="IDL-Frontend type alignment check")
    ap.add_argument(
        "--sat-ts",
        default="web/src/types/sat.ts",
        help="Path to web/src/types/sat.ts",
    )
    ap.add_argument(
        "--output",
        default="test-results/idl_alignment_report.json",
        help="JSON report output path",
    )
    args = ap.parse_args()

    src = Path(args.sat_ts).read_text()
    mismatches: list[dict] = []
    total_checked = 0

    for iface, expected_fields in GROUND_TRUTH.items():
        actual_fields = _parse_ts_interface(src, iface)
        for field, expected_type in expected_fields.items():
            total_checked += 1
            if field not in actual_fields:
                mismatches.append({
                    "interface": iface,
                    "field": field,
                    "reason": "MISSING",
                    "expected_type": expected_type,
                    "actual_type": None,
                })
            elif not _is_type_compatible(actual_fields[field], expected_type):
                mismatches.append({
                    "interface": iface,
                    "field": field,
                    "reason": "TYPE_MISMATCH",
                    "expected_type": expected_type,
                    "actual_type": actual_fields[field],
                })

    report = {
        "total_fields_checked": total_checked,
        "mismatches_count": len(mismatches),
        "mismatches": mismatches,
        "sat_ts_path": str(args.sat_ts),
    }

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))

    if mismatches:
        print(f"\n[FAIL] {len(mismatches)} mismatch(es) found.", file=sys.stderr)
        return 1
    print(f"[PASS] All {total_checked} fields aligned.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
