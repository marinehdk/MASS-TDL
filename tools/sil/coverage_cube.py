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
    """Map label variants (e.g. 'Rule 14 Head-on') → canonical key ('Rule14')."""
    m = re.search(r"\d+", rule)
    if not m:
        return rule
    key = f"Rule{m.group()}"
    return key if key in COLREG_RULES else rule


def seed_index_from_filename(stem: str) -> int:
    """Extract seed from filename stem and map to index 1–5 via (seed % 5) + 1."""
    m = re.search(r"seed(\d+)", stem)
    if not m:
        return 1
    return (int(m.group(1)) % 5) + 1


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

    def to_json_dict(self) -> dict:
        return {"cells_lit": len(self._lit), "total_cells": TOTAL_CELLS}