"""1100-cell COLREG × ODD × Disturbance × Seed coverage tracker.

Cell space (V&V Plan §4.2):
  11 COLREG rules × 4 ODD zones × 5 disturbance bins × 5 seeds = 1100 cells.
"""
from __future__ import annotations
import re

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
        """Load coverage state from traceability.csv (columns: scenario_id, rule, odd_zone, wind_kn, vis_m, seed)."""
        import csv as _csv

        cube = cls()
        with open(csv_path, newline="") as f:
            reader = _csv.DictReader(f)
            for row in reader:
                cube.mark(
                    rule=row.get("rule", ""),
                    odd_zone=row.get("odd_zone", "open_sea"),
                    wind_kn=float(row.get("wind_kn", 0.0)),
                    vis_m=float(row.get("vis_m", 10000.0)),
                    seed_index=int(row.get("seed", 1)),
                )
        return cube

    def to_json_dict(self) -> dict:
        return {"cells_lit": len(self._lit), "total_cells": TOTAL_CELLS}