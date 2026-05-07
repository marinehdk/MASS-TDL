"""
scenario_schema.py — Dataclass definitions for COLREGs HIL scenario data model.

All angles in degrees, speeds in knots, distances in metres.
Coordinate convention follows fcb_simulator ENU:
  - OwnShipInit.psi_deg: math convention (0=East, 90=North)
  - TargetShipInit.cog_deg: meteorological convention (0=North, 90=East)
"""

from __future__ import annotations

import dataclasses
import json
from dataclasses import dataclass, field
from typing import List


@dataclass
class OwnShipInit:
    x_m: float      # East (m), NED/ENU origin
    y_m: float      # North (m)
    psi_deg: float  # heading — math convention: 0=East, 90=North (matches fcb_simulator ENU)
    u_kn: float     # surge speed (knots)


@dataclass
class TargetShipInit:
    mmsi: int
    vessel_type: str    # "motor_vessel" | "sailing_vessel" | "fishing_vessel" | "restricted_maneuvering"
    x_m: float
    y_m: float
    cog_deg: float      # course over ground — meteorological: 0=North, 90=East
    sog_kn: float
    length_m: float


@dataclass
class ExpectedOutcome:
    colregs_rule: str       # "Rule_13" | "Rule_14" | "Rule_15" | "Rule_17" | "Rule_18" | "Rule_19"
    own_role: str           # "give_way" | "stand_on" | "both_give_way"
    m5_expected_action: str # "turn_right" | "slow_down" | "maintain_course" | "turn_right_or_slow"
    cpa_m: float            # expected CPA at current courses (m)
    tcpa_s: float           # expected TCPA (s)


@dataclass
class Scenario:
    scenario_id: str            # e.g. "SCN-R14-001"
    colregs_rule: str           # "Rule_13" | "Rule_14" | "Rule_15" | "Rule_17" | "Rule_18" | "Rule_19"
    description: str
    odd_visibility_nm: float    # 0.5 = fog (Rule 19 applies), 5.0 = clear
    odd_mode: str               # "NORMAL" | "DEGRADED" | "CRITICAL"
    own_ship: OwnShipInit
    targets: List[TargetShipInit]
    expected: ExpectedOutcome
    tags: List[str]             # e.g. ["head_on", "high_risk", "single_target"]


# ---------------------------------------------------------------------------
# JSON serialisation helpers
# ---------------------------------------------------------------------------

def _to_dict(obj) -> object:
    """Recursively convert dataclasses (and lists/dicts thereof) to plain dicts."""
    if dataclasses.is_dataclass(obj) and not isinstance(obj, type):
        return {k: _to_dict(v) for k, v in dataclasses.asdict(obj).items()}
    if isinstance(obj, list):
        return [_to_dict(i) for i in obj]
    return obj


def scenario_to_dict(s: Scenario) -> dict:
    return _to_dict(s)


def scenarios_to_json(scenarios: List[Scenario], indent: int = 2) -> str:
    return json.dumps([scenario_to_dict(s) for s in scenarios], indent=indent)
