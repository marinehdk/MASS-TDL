"""Dynamic Under-Keel Clearance (UKC) Calculator.

Calculates real-time Net UKC under motion, heel, squat (Barrass and Römisch),
tide adjustments (S-104), and CATZOC (S-57/S-67) confidence margins.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from enum import Enum


class CatzocLevel(Enum):
    A1 = "A1"
    A2 = "A2"
    B = "B"
    C = "C"
    D = "D"
    U = "U"


@dataclass(frozen=True, slots=True)
class CatzocMargin:
    position_error_m: float
    depth_error_fixed_m: float
    depth_error_pct: float
    margin_coefficient: float
    extra_compensation_m: float


# Official IHO S-67 Mariner's Guide mapping of CATZOC categories
CATZOC_STANDARDS = {
    CatzocLevel.A1: CatzocMargin(5.0, 0.5, 0.01, 1.0, 0.0),
    CatzocLevel.A2: CatzocMargin(20.0, 1.0, 0.02, 1.2, 0.0),
    CatzocLevel.B: CatzocMargin(50.0, 1.0, 0.05, 1.5, 0.5), # B gets 0.5m extra cover compensation
    CatzocLevel.C: CatzocMargin(500.0, 2.0, 0.05, 2.0, 1.0),
    CatzocLevel.D: CatzocMargin(999.0, 3.0, 0.10, 2.5, 1.5),
    CatzocLevel.U: CatzocMargin(999.0, 3.0, 0.10, 2.5, 1.5),
}


class UkcCalculator:
    """Computes dynamic Under-Keel Clearance for real-time safety assessments."""

    def __init__(
        self,
        *,
        cb: float = 0.65,          # Block coefficient (45m Tugboat typical)
        beam_m: float = 14.0,       # Vessel breadth
        draft_static_m: float = 3.5,# Static draught
    ) -> None:
        self._cb = cb
        self._beam = beam_m
        self._draft_static = draft_static_m

    def compute_squat_barrass(self, speed_knots: float, is_restricted_channel: bool = False) -> float:
        """Compute squat using Barrass empirical formula.

        Args:
            speed_knots: Speed through water.
            is_restricted_channel: Whether transiting a narrow channel/canal.
        """
        # Barrass: squat_m = C_b * V^2 / 100 (open) or 2.65 * C_b * V^2 / 100 (restricted)
        coef = 2.65 if is_restricted_channel else 1.0
        return coef * (self._cb * (speed_knots ** 2)) / 100.0

    def compute_squat_roemisch(self, speed_knots: float, water_depth_m: float) -> float:
        """Compute squat using Römisch model (considering depth-to-draught ratio)."""
        # Ratio of depth to static draught
        ht_ratio = water_depth_m / self._draft_static
        if ht_ratio <= 1.0:
            return 0.0

        # Römisch dynamic shallow water squat scaling factor
        c_shallow = 1.0 / math.sqrt(ht_ratio - 1.0) if ht_ratio > 1.0 else 1.0
        c_shallow = min(c_shallow, 3.0)  # Bound factor limit

        # Simple velocity-induced dynamic pressure squat
        c_dynamic = 0.015 * (speed_knots ** 1.5)
        return c_dynamic * c_shallow * self._draft_static

    def calculate_heel_allowance(self, heel_degrees: float) -> float:
        """Calculate reduction in UKC due to roll or list (heel)."""
        # Bilge lowering amount = half-breadth * sin(heel)
        half_breadth = self._beam / 2.0
        return half_breadth * math.sin(math.radians(abs(heel_degrees)))

    def get_catzoc_allowance(self, catzoc: CatzocLevel, depth_m: float) -> float:
        """Retrieve safety allowance required by the CATZOC confidence rating."""
        rule = CATZOC_STANDARDS.get(catzoc, CATZOC_STANDARDS[CatzocLevel.U])
        # Depth error allowance = depth_error_fixed + depth_error_pct * depth
        base_allowance = rule.depth_error_fixed_m + rule.depth_error_pct * depth_m
        return (base_allowance * rule.margin_coefficient) + rule.extra_compensation_m

    def compute_net_ukc(
        self,
        *,
        chart_depth_m: float,
        tide_height_m: float,
        speed_knots: float,
        heel_degrees: float,
        motion_allowance_m: float = 0.3, # Swell, heave margin
        catzoc: CatzocLevel = CatzocLevel.A2,
        is_restricted: bool = False,
    ) -> dict[str, float | str | bool]:
        """Compute Net UKC and safety status.

        Returns:
            dict containing:
                "net_ukc_m": float, the remaining clearance
                "squat_m": float, the squat allowance used
                "heel_allowance_m": float, bilge lowering due to list
                "zoc_margin_m": float, spatial safety buffer
                "safe": bool, whether Net UKC exceeds minimal requirements
        """
        # 1. Total available water depth (Chart datum + S-104 Tide)
        total_depth = chart_depth_m + tide_height_m

        # 2. Compute dynamic squat - take the conservative maximum of Barrass and Römisch
        squat_barrass = self.compute_squat_barrass(speed_knots, is_restricted)
        squat_roemisch = self.compute_squat_roemisch(speed_knots, total_depth)
        dynamic_squat = max(squat_barrass, squat_roemisch)

        # 3. List / roll allowance
        heel_allowance = self.calculate_heel_allowance(heel_degrees)

        # 4. CATZOC confidence margin
        zoc_margin = self.get_catzoc_allowance(catzoc, chart_depth_m)

        # 5. Net UKC = Total Water Depth - (Static Draft + Squat + Heel + Motion + ZOC Margin)
        vessel_total_draft = self._draft_static + dynamic_squat + heel_allowance + motion_allowance_m
        net_ukc = total_depth - vessel_total_draft

        # Minimum required safety clearance: standard is 10% of draught or 0.5m
        min_clearance = max(0.10 * self._draft_static, 0.5)

        # Apply ZOC safety envelope checks
        safe = (net_ukc - zoc_margin) >= min_clearance

        return {
            "net_ukc_m": net_ukc,
            "squat_m": dynamic_squat,
            "heel_allowance_m": heel_allowance,
            "zoc_margin_m": zoc_margin,
            "safe": safe,
            "status": "SAFE" if safe else ("WARNING" if net_ukc >= min_clearance else "NO_GO"),
        }
