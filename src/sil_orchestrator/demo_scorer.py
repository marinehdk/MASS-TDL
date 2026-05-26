from __future__ import annotations

import math
from dataclasses import dataclass, field


@dataclass
class DemoScoringResult:
    min_cpa_nm: float
    tcpa_at_min_s: float
    max_rudder_deg: float
    max_cross_track_nm: float
    avg_rot_dpm: float
    distance_nm: float
    duration_s: float
    avoidance_initiated: bool
    safety: float = 0.0
    rule_compliance: float = 0.0
    delay_penalty: float = 0.0
    action_magnitude_penalty: float = 0.0
    phase_score: float = 0.0
    plausibility: float = 0.0
    total: float = 0.0
    verdict: str = "fail"
    rule_chain: list[str] = field(default_factory=list)


_CPA_THRESHOLD_NM = 0.27
_WEIGHTS = {
    "safety": 0.30,
    "rule_compliance": 0.25,
    "delay_penalty": 0.12,
    "action_magnitude_penalty": 0.08,
    "phase_score": 0.15,
    "plausibility": 0.10,
}
_MAX_ACCEPTABLE_RUDDER_DEG = 35.0
_MAX_ACCEPTABLE_CROSS_TRACK_NM = 0.5
_DELAY_THRESHOLD_S = 30.0


def score_demo_run(
    min_cpa_nm: float,
    tcpa_at_min_s: float,
    max_rudder_deg: float,
    max_cross_track_nm: float,
    rot_samples: list[float],
    distance_nm: float,
    duration_s: float,
    avoidance_initiated: bool,
) -> DemoScoringResult:
    safety = min(1.0, min_cpa_nm / _CPA_THRESHOLD_NM) if min_cpa_nm < float("inf") else 1.0

    rule_compliance = 1.0 if min_cpa_nm >= _CPA_THRESHOLD_NM else 0.0

    if avoidance_initiated and tcpa_at_min_s > 0:
        delay_ratio = min(tcpa_at_min_s / _DELAY_THRESHOLD_S, 1.0)
        delay_penalty = delay_ratio
    else:
        delay_penalty = 0.5

    if max_rudder_deg > 0:
        action_magnitude_penalty = 1.0 - min(max_rudder_deg / _MAX_ACCEPTABLE_RUDDER_DEG, 1.0)
    else:
        action_magnitude_penalty = 1.0

    if avoidance_initiated:
        phase_score = 0.8
        if max_cross_track_nm <= _MAX_ACCEPTABLE_CROSS_TRACK_NM:
            phase_score = 1.0
    else:
        phase_score = 0.0

    if rot_samples:
        rot_variance = sum((r - sum(rot_samples) / len(rot_samples)) ** 2 for r in rot_samples) / len(rot_samples)
        plausibility = max(0.0, 1.0 - math.sqrt(rot_variance) * 10.0)
    else:
        plausibility = 1.0

    total = (
        _WEIGHTS["safety"] * safety
        + _WEIGHTS["rule_compliance"] * rule_compliance
        + _WEIGHTS["delay_penalty"] * delay_penalty
        + _WEIGHTS["action_magnitude_penalty"] * action_magnitude_penalty
        + _WEIGHTS["phase_score"] * phase_score
        + _WEIGHTS["plausibility"] * plausibility
    )

    verdict = "pass" if total >= 0.70 else "fail"

    rule_chain: list[str] = []
    if avoidance_initiated:
        rule_chain.append("Rule 14 (Head-on)")
        rule_chain.append("Rule 8 (Action to avoid collision)")

    avg_rot_dpm = 0.0
    if rot_samples:
        avg_rot_dpm = sum(rot_samples) / len(rot_samples)

    return DemoScoringResult(
        min_cpa_nm=round(min_cpa_nm, 6),
        tcpa_at_min_s=round(tcpa_at_min_s, 2),
        max_rudder_deg=round(max_rudder_deg, 2),
        max_cross_track_nm=round(max_cross_track_nm, 6),
        avg_rot_dpm=round(avg_rot_dpm, 4),
        distance_nm=round(distance_nm, 4),
        duration_s=round(duration_s, 2),
        avoidance_initiated=avoidance_initiated,
        safety=round(safety, 4),
        rule_compliance=round(rule_compliance, 4),
        delay_penalty=round(delay_penalty, 4),
        action_magnitude_penalty=round(action_magnitude_penalty, 4),
        phase_score=round(phase_score, 4),
        plausibility=round(plausibility, 4),
        total=round(total, 4),
        verdict=verdict,
        rule_chain=rule_chain,
    )
