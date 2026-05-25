"""Generate traceability-matrix.csv from Imazu (and other) scenario YAML files.

Scans a scenarios directory recursively for .yaml files, extracts
COLREGs rule coverage, ODD zone, target count, and own-ship SOG,
then writes a CSV suitable for the coverage_cube.py pipeline.

Usage:
    python tools/sil/generate_traceability_matrix.py \
        --scenarios-dir scenarios/ \
        --output docs/Design/SIL/traceability-matrix.csv
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path

import yaml

from coverage_cube import COLREG_RULES, _normalize_rule

MPS_TO_KN = 1.94384
NM_TO_M = 1852.0


def _extract_scenario_id(data: dict, filepath: Path) -> str:
    meta = data.get("metadata", {})
    sid = meta.get("scenario_id", "")
    if sid:
        return str(sid)
    return filepath.stem


def _extract_rules(data: dict) -> list[str]:
    meta = data.get("metadata", {})
    encounter = meta.get("encounter", {})
    primary = encounter.get("rule", "")
    colregs_list = meta.get("colregs_rules", [])

    rules: list[str] = []
    if primary:
        rules.append(_normalize_rule(str(primary)))
    for r in colregs_list:
        normalized = _normalize_rule(str(r))
        if normalized not in rules:
            rules.append(normalized)

    if not rules:
        rules.append("")
    return rules


def _extract_odd_zone(data: dict) -> str:
    meta = data.get("metadata", {})
    odd_cell = meta.get("odd_cell", {})
    return str(odd_cell.get("domain", "unknown"))


def _extract_own_sog(data: dict) -> float:
    own = data.get("ownShip", {})
    initial = own.get("initial", {})
    return float(initial.get("sog", 0.0))


def _extract_target_count(data: dict) -> int:
    targets = data.get("targetShips", [])
    if isinstance(targets, list):
        return len(targets)
    return 0


def _extract_wind_kn(data: dict) -> float:
    env = data.get("environment", {})
    wind = env.get("wind", {})
    speed_mps = float(wind.get("speed_mps", 0.0))
    return speed_mps * MPS_TO_KN


def _extract_vis_m(data: dict) -> float:
    env = data.get("environment", {})
    vis_nm = float(env.get("visibility_nm", 10.0))
    return vis_nm * NM_TO_M


def _extract_seed(data: dict) -> int:
    meta = data.get("metadata", {})
    seed = meta.get("seed", None)
    if seed is not None:
        return int(seed)
    return 1


def parse_scenario(filepath: Path) -> list[dict]:
    with open(filepath, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        return []

    scenario_id = _extract_scenario_id(data, filepath)
    rules = _extract_rules(data)
    odd_zone = _extract_odd_zone(data)
    own_sog = _extract_own_sog(data)
    target_count = _extract_target_count(data)
    wind_kn = _extract_wind_kn(data)
    vis_m = _extract_vis_m(data)
    seed = _extract_seed(data)
    source = filepath.parent.name

    rows: list[dict] = []
    for rule_id in rules:
        rows.append({
            "scenario_id": scenario_id,
            "rule_id": rule_id,
            "odd_zone": odd_zone,
            "target_count": target_count,
            "own_sog_kn": own_sog,
            "wind_kn": round(wind_kn, 2),
            "vis_m": round(vis_m, 1),
            "seed": seed,
            "source": source,
            "file": str(filepath),
        })
    return rows


def scan_scenarios(scenarios_dir: Path) -> list[dict]:
    rows: list[dict] = []
    yaml_files = sorted(scenarios_dir.rglob("*.yaml"))
    for fp in yaml_files:
        if fp.name.startswith("."):
            continue
        try:
            rows.extend(parse_scenario(fp))
        except Exception as exc:
            print(f"WARNING: failed to parse {fp}: {exc}", file=sys.stderr)
    return rows


def write_csv(rows: list[dict], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "scenario_id", "rule_id", "odd_zone", "target_count",
        "own_sog_kn", "wind_kn", "vis_m", "seed", "source", "file",
    ]
    with open(output, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def print_summary(rows: list[dict]) -> None:
    scenario_ids = sorted({r["scenario_id"] for r in rows})
    rules_found = sorted({r["rule_id"] for r in rows if r["rule_id"]})
    odd_zones = sorted({r["odd_zone"] for r in rows})

    print(f"\n{'='*60}")
    print(f"Traceability Matrix Summary")
    print(f"{'='*60}")
    print(f"Total rows (scenario x rule): {len(rows)}")
    print(f"Unique scenarios:             {len(scenario_ids)}")
    print(f"Unique ODD zones:             {len(odd_zones)}")
    print()

    print("COLREGs Rules covered:")
    canonical = set(COLREG_RULES)
    found_set = set(rules_found)
    for rule in COLREG_RULES:
        count = sum(1 for r in rows if r["rule_id"] == rule)
        marker = "OK" if count > 0 else "MISSING"
        print(f"  {rule:8s}  {count:3d} scenario-rule pairs  [{marker}]")
    extra = found_set - canonical
    if extra:
        print(f"\n  Extra rules (not in coverage_cube): {', '.join(sorted(extra))}")

    missing = canonical - found_set
    if missing:
        print(f"\n  *** MISSING rules: {', '.join(sorted(missing))} ***")

    print(f"\nODD zones found:")
    for zone in odd_zones:
        count = sum(1 for r in rows if r["odd_zone"] == zone)
        print(f"  {zone}: {count} rows")

    print(f"\nTarget count distribution:")
    tc_counts: dict[int, int] = {}
    for r in rows:
        tc = r["target_count"]
        tc_counts[tc] = tc_counts.get(tc, 0) + 1
    for tc in sorted(tc_counts):
        print(f"  {tc} target(s): {tc_counts[tc]} rows")

    print(f"{'='*60}\n")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Generate traceability-matrix.csv from scenario YAML files"
    )
    ap.add_argument(
        "--scenarios-dir", type=Path, default=Path("scenarios"),
        help="Root directory containing scenario YAML files (default: scenarios/)"
    )
    ap.add_argument(
        "--output", type=Path, default=Path("docs/Design/SIL/traceability-matrix.csv"),
        help="Output CSV path (default: docs/Design/SIL/traceability-matrix.csv)"
    )
    args = ap.parse_args()

    if not args.scenarios_dir.is_dir():
        print(f"ERROR: {args.scenarios_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    rows = scan_scenarios(args.scenarios_dir)
    if not rows:
        print("WARNING: no scenario rows generated", file=sys.stderr)
        sys.exit(1)

    write_csv(rows, args.output)
    print(f"Wrote {len(rows)} rows to {args.output}")
    print_summary(rows)


if __name__ == "__main__":
    main()
