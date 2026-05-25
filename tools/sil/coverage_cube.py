"""1100-cell COLREG × ODD × Disturbance × Seed coverage tracker.

Cell space (V&V Plan §4.2):
  11 COLREG rules × 4 ODD zones × 5 disturbance bins × 5 seeds = 1100 cells.

CLI usage:
    python tools/sil/coverage_cube.py \\
        --csv docs/Design/SIL/traceability-matrix.csv \\
        [--filter-rule Rule14] \\
        [--filter-odd open_sea]
"""
from __future__ import annotations

import argparse
import csv as _csv
import re
import sys

COLREG_RULES = [
    "Rule5", "Rule6", "Rule7", "Rule8", "Rule9",
    "Rule13", "Rule14", "Rule15", "Rule16", "Rule17", "Rule19",
]
ODD_ZONES = [
    "open_sea",
    "coastal_traffic_separation",
    "port_approach",
    "offshore_wind_farm",
]
DISTURBANCE_BINS = ["bf_0_1", "bf_2_3", "bf_4_5", "bf_6_7", "sensor_degraded"]
SEEDS = [1, 2, 3, 4, 5]

TOTAL_CELLS = len(COLREG_RULES) * len(ODD_ZONES) * len(DISTURBANCE_BINS) * len(SEEDS)  # 1100


def wind_kn_to_bin(wind_kn: float, vis_m: float) -> str:
    """Map (wind_kn, vis_m) to Beaufort disturbance bin (V&V Plan §4.2)."""
    if vis_m < 5000.0:
        return "sensor_degraded"
    if wind_kn < 3.5:
        return "bf_0_1"
    if wind_kn < 10.7:
        return "bf_2_3"
    if wind_kn < 21.6:
        return "bf_4_5"
    return "bf_6_7"


def _normalize_rule(rule: str) -> str:
    """Map label variants (e.g. 'Rule 14 Head-on') → canonical key ('Rule14').

    Supports:
      - Numeric extraction: "Rule 14 Head-on" → "Rule14", "Rule13" → "Rule13"
      - Keyword fallback (no digit found):
          lookout / look-out           → Rule5
          safe speed / safespeed       → Rule6
          risk of collision / roc      → Rule7
          action to avoid / actionavoid → Rule8
          narrow channel / narrow      → Rule9
          restricted visibility / restrvis → Rule19
    """
    m = re.search(r"\d+", rule)
    if m:
        key = f"Rule{m.group()}"
        return key if key in COLREG_RULES else rule

    # Keyword-based lookup for rules without explicit number
    rule_lower = rule.lower().replace("-", "").replace(" ", "").replace("_", "")
    kw_map: dict[str, str] = {
        "lookout": "Rule5",
        "safespeed": "Rule6",
        "riskofcollision": "Rule7",
        "roc": "Rule7",
        "actiontoavoid": "Rule8",
        "actionavoid": "Rule8",
        "narrowchannel": "Rule9",
        "narrow": "Rule9",
        "restrictedvisibility": "Rule19",
        "restrvis": "Rule19",
    }
    # Longest-match-first to resolve ambiguity (e.g. "narrow channel action to avoid"
    # matches both "narrow"→Rule9 and "actiontoavoid"→Rule8 — pick the more specific one)
    matches = [(pattern, mapped) for pattern, mapped in kw_map.items() if pattern in rule_lower]
    if matches:
        matches.sort(key=lambda x: len(x[0]), reverse=True)
        return matches[0][1]
    return rule


def seed_index_from_filename(stem: str) -> int:
    """Extract seed from filename stem and map to index 1–5.
    Mapping: seed1→1, seed5→5, seed6→1 (wraps via (seed-1)%5 + 1).
    """
    m = re.search(r"seed(\d+)", stem)
    if not m:
        return 1
    return ((int(m.group(1)) - 1) % 5) + 1


class CoverageCube:
    def __init__(self) -> None:
        self._lit: set[tuple[str, str, str, int]] = set()

    def mark(
        self,
        rule: str,
        odd_zone: str,
        wind_kn: float,
        vis_m: float,
        seed_index: int,
    ) -> None:
        rule_key = _normalize_rule(rule)
        if rule_key not in COLREG_RULES:
            return
        if odd_zone not in ODD_ZONES:
            odd_zone = "open_sea"
        dist_bin = wind_kn_to_bin(wind_kn, vis_m)
        self._lit.add((rule_key, odd_zone, dist_bin, seed_index))

    def cells_lit(self) -> int:
        return len(self._lit)

    def to_heatmap_matrix(self) -> list[list[int]]:
        """Return 11 × 4 matrix (rule × ODD) with lit cell counts per (rule, odd)."""
        matrix: list[list[int]] = []
        for rule in COLREG_RULES:
            row: list[int] = []
            for odd in ODD_ZONES:
                count = sum(
                    1 for d in DISTURBANCE_BINS for s in SEEDS
                    if (rule, odd, d, s) in self._lit
                )
                row.append(count)
            matrix.append(row)
        return matrix

    @classmethod
    def load_from_csv(cls, csv_path: str) -> "CoverageCube":
        """Load coverage state from traceability.csv.

        Supports both column names: 'rule' (legacy) and 'rule_id' (current).
        """
        cube = cls()
        with open(csv_path, newline="") as f:
            reader = _csv.DictReader(f)
            for row in reader:
                rule_val = row.get("rule_id") or row.get("rule", "")
                cube.mark(
                    rule=rule_val,
                    odd_zone=row.get("odd_zone", "open_sea"),
                    wind_kn=float(row.get("wind_kn", 0.0)),
                    vis_m=float(row.get("vis_m", 10000.0)),
                    seed_index=int(row.get("seed", 1)),
                )
        return cube

    def to_heatmap_matrix(
        self,
        filter_rule: str | None = None,
        filter_odd: str | None = None,
    ) -> list[list[int]]:
        """Return 11 x 4 matrix (rule x ODD) with lit cell counts.

        When filter_rule is given, only rows matching that rule are counted.
        When filter_odd is given, only columns matching that ODD zone are counted.
        Both filters can be combined.
        """
        rules = COLREG_RULES if filter_rule is None else [filter_rule]
        odds = ODD_ZONES if filter_odd is None else [filter_odd]
        matrix: list[list[int]] = []
        for rule in rules:
            row: list[int] = []
            for odd in odds:
                count = sum(
                    1 for d in DISTURBANCE_BINS for s in SEEDS
                    if (rule, odd, d, s) in self._lit
                )
                row.append(count)
            matrix.append(row)
        return matrix

    def filtered_cells_lit(
        self,
        filter_rule: str | None = None,
        filter_odd: str | None = None,
    ) -> int:
        """Count lit cells matching the given filters."""
        count = 0
        for rule, odd, d, s in self._lit:
            if filter_rule is not None and rule != filter_rule:
                continue
            if filter_odd is not None and odd != filter_odd:
                continue
            count += 1
        return count

    def to_json_dict(self) -> dict:
        return {"cells_lit": len(self._lit), "total_cells": TOTAL_CELLS}


def _print_filtered_report(
    cube: CoverageCube,
    filter_rule: str | None,
    filter_odd: str | None,
) -> None:
    matrix = cube.to_heatmap_matrix(filter_rule=filter_rule, filter_odd=filter_odd)
    lit = cube.filtered_cells_lit(filter_rule=filter_rule, filter_odd=filter_odd)

    rules = COLREG_RULES if filter_rule is None else [filter_rule]
    odds = ODD_ZONES if filter_odd is None else [filter_odd]
    total = len(rules) * len(odds) * len(DISTURBANCE_BINS) * len(SEEDS)

    label_parts: list[str] = []
    if filter_rule:
        label_parts.append(f"Rule={filter_rule}")
    if filter_odd:
        label_parts.append(f"ODD={filter_odd}")
    label = " | ".join(label_parts) if label_parts else "ALL"

    print(f"\n{'='*60}")
    print(f"Coverage Cube Report  [{label}]")
    print(f"{'='*60}")
    print(f"Cells lit: {lit} / {total} ({lit/total*100:.1f}%)" if total else "N/A")
    print()

    header = "".join(f"{o:>22s}" for o in odds)
    print(f"{'Rule':8s} {header}")
    print("-" * (8 + 22 * len(odds)))
    for i, rule in enumerate(rules):
        vals = "".join(f"{matrix[i][j]:>22d}" for j in range(len(odds)))
        print(f"{rule:8s} {vals}")

    print(f"{'='*60}\n")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Coverage cube viewer with optional Rule/ODD filters"
    )
    ap.add_argument(
        "--csv", required=True, type=str,
        help="Path to traceability-matrix.csv"
    )
    ap.add_argument(
        "--filter-rule", type=str, default=None,
        help="Only count cells for this COLREGs rule (e.g. Rule14)"
    )
    ap.add_argument(
        "--filter-odd", type=str, default=None,
        help="Only count cells for this ODD zone (e.g. open_sea)"
    )
    args = ap.parse_args()

    if args.filter_rule and args.filter_rule not in COLREG_RULES:
        print(f"ERROR: unknown rule '{args.filter_rule}'. "
              f"Valid: {', '.join(COLREG_RULES)}", file=sys.stderr)
        sys.exit(1)
    if args.filter_odd and args.filter_odd not in ODD_ZONES:
        print(f"ERROR: unknown ODD zone '{args.filter_odd}'. "
              f"Valid: {', '.join(ODD_ZONES)}", file=sys.stderr)
        sys.exit(1)

    cube = CoverageCube.load_from_csv(args.csv)
    _print_filtered_report(cube, args.filter_rule, args.filter_odd)


if __name__ == "__main__":
    main()