"""Hagen (2022) + Woerner (2019) 6-dimensional scoring engine."""

from __future__ import annotations

import math
import time as _time
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

DEFAULT_WEIGHTS = {
    "safety": 0.30,
    "rule_compliance": 0.25,
    "delay_penalty": 0.12,
    "action_magnitude_penalty": 0.08,
    "phase": 0.15,
    "plausibility": 0.10,
}


@dataclass
class ScoringRow:
    """One scored simulation timestep per Hagen (2022) six-dimension method."""

    stamp: float
    safety: float
    rule_compliance: float
    delay_penalty: float
    action_magnitude_penalty: float
    phase_score: float
    plausibility: float
    total: float
    cpa_nm: float = 0.0
    cpa_target_nm: float = 0.27


class HagenScorer:
    """6-dim scoring engine: safety, rules, delay, action magnitude, phase, plausibility."""

    def __init__(
        self,
        cpa_target_nm: float = 0.27,
        delay_coeff: float = 0.01,
        kappa_max: float = 0.01,
        accel_max_ms2: float = 2.0,
        weights: Optional[Dict[str, float]] = None,
    ):
        self.cpa_target_nm = cpa_target_nm
        self.delay_coeff = delay_coeff
        self.kappa_max = kappa_max
        self.accel_max_ms2 = accel_max_ms2
        self._weights = weights or DEFAULT_WEIGHTS.copy()
        self._rows: List[ScoringRow] = []

    @staticmethod
    def _haversine_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        R_nm = 3440.065
        dlat = math.radians(lat2 - lat1)
        dlon = math.radians(lon2 - lon1)
        a = (
            math.sin(dlat / 2) ** 2
            + math.cos(math.radians(lat1))
            * math.cos(math.radians(lat2))
            * math.sin(dlon / 2) ** 2
        )
        return 2 * R_nm * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    def _compute_cpa_nm(
        self,
        own_lat: float,
        own_lon: float,
        own_sog: float,
        own_cog: float,
        tgt_lat: float,
        tgt_lon: float,
        tgt_sog: float,
        tgt_cog: float,
    ) -> float:
        return self._haversine_nm(own_lat, own_lon, tgt_lat, tgt_lon)

    def _score_safety(self, cpa_nm: float) -> float:
        if self.cpa_target_nm <= 0:
            return 1.0
        return max(0.0, min(1.0, cpa_nm / self.cpa_target_nm))

    def _score_rule_compliance(self, rule_states: Dict[str, str]) -> float:
        if not rule_states:
            return 1.0
        score_map = {"ok": 1.0, "partial": 0.5, "violated": 0.0}
        scores = [score_map.get(v, 1.0) for v in rule_states.values()]
        return sum(scores) / len(scores)

    def _score_delay_penalty(self, t_action_s: float, t_target_action_s: float) -> float:
        return max(0.0, t_action_s - t_target_action_s) * self.delay_coeff

    def _score_action_magnitude_penalty(
        self, rudder_deg: float, turning_rate_dps: float
    ) -> float:
        deviation = abs(abs(rudder_deg) - 60.0) - 30.0
        if deviation <= 0:
            return 0.0
        return (deviation / 30.0) ** 2

    def _score_phase(self, behavior_phase: str) -> float:
        return {
            "give_way": 1.0,
            "stand_on": 0.5,
            "in_extremis": 0.0,
            "transit": 1.0,
        }.get(behavior_phase, 0.5)

    def _score_plausibility(self, curvature: float, accel_ms2: float) -> float:
        ek = (
            max(0.0, abs(curvature) - self.kappa_max) / self.kappa_max
            if self.kappa_max > 0
            else 0.0
        )
        ea = (
            max(0.0, abs(accel_ms2) - self.accel_max_ms2) / self.accel_max_ms2
            if self.accel_max_ms2 > 0
            else 0.0
        )
        return 1.0 - max(ek, ea)

    def score_frame(
        self,
        own_lat: float,
        own_lon: float,
        own_heading: float,
        own_sog: float,
        targets: List[Tuple[float, float, float, float]],
        rule_states: Dict[str, str],
        t_action_s: float,
        t_target_action_s: float,
        rudder_deg: float,
        turning_rate_dps: float,
        behavior_phase: str,
        trajectory_curvature: float,
        trajectory_accel_ms2: float,
        timestamp: Optional[float] = None,
    ) -> ScoringRow:
        stamp = timestamp if timestamp is not None else _time.time()

        cpa_nm: float = float("inf")
        for tgt in targets:
            d = self._compute_cpa_nm(
                own_lat, own_lon, own_sog, own_heading,
                tgt[0], tgt[1], tgt[3], tgt[2],
            )
            cpa_nm = min(cpa_nm, d)
        if cpa_nm == float("inf"):
            cpa_nm = 10.0

        s = self._score_safety(cpa_nm)
        r = self._score_rule_compliance(rule_states)
        d_raw = self._score_delay_penalty(t_action_s, t_target_action_s)
        m_raw = self._score_action_magnitude_penalty(rudder_deg, turning_rate_dps)
        p = self._score_phase(behavior_phase)
        pl = self._score_plausibility(trajectory_curvature, trajectory_accel_ms2)

        d_score = max(0.0, 1.0 - min(1.0, d_raw))
        m_score = max(0.0, 1.0 - min(1.0, m_raw))

        total = (
            self._weights["safety"] * s
            + self._weights["rule_compliance"] * r
            + self._weights["delay_penalty"] * d_score
            + self._weights["action_magnitude_penalty"] * m_score
            + self._weights["phase"] * p
            + self._weights["plausibility"] * pl
        )

        row = ScoringRow(
            stamp=stamp,
            safety=s,
            rule_compliance=r,
            delay_penalty=d_raw,
            action_magnitude_penalty=m_raw,
            phase_score=p,
            plausibility=pl,
            total=total,
            cpa_nm=cpa_nm,
            cpa_target_nm=self.cpa_target_nm,
        )
        self._rows.append(row)
        return row

    def get_rows(self) -> List[ScoringRow]:
        return list(self._rows)

    def get_final_verdict(self, threshold: float = 0.70) -> Tuple[bool, float]:
        if not self._rows:
            return False, 0.0
        avg = sum(r.total for r in self._rows) / len(self._rows)
        return avg >= threshold, avg
