"""Per-rule sub-criteria evaluator for COLREGs rules 13/14/15/16/17.
D1.7 §5.3 rubric. Returns "full" | "partial" | "violated".
"""
from __future__ import annotations


def evaluate_rule_compliance(rule: str, encounter_state: dict) -> str:
    handlers = {
        "Rule13": _eval_rule13, "Rule14": _eval_rule14,
        "Rule15": _eval_rule15, "Rule16": _eval_rule16, "Rule17": _eval_rule17,
    }
    handler = handlers.get(rule)
    if handler is None:
        return "full"
    return handler(encounter_state)


def _eval_rule13(s: dict) -> str:
    hc = abs(s.get("heading_change_deg", 0.0))
    cpa = s.get("cpa_nm", 0.0)
    cpa_target = s.get("cpa_target_nm", 0.27)
    if hc >= 30.0 and cpa >= cpa_target:
        return "full"
    if hc >= 15.0:
        return "partial"
    return "violated"


def _eval_rule14(s: dict) -> str:
    side = s.get("rudder_side", "starboard")
    if side == "port":
        return "violated"
    hc = abs(s.get("heading_change_deg", 0.0))
    cpa = s.get("cpa_nm", 0.0)
    cpa_target = s.get("cpa_target_nm", 0.27)
    if hc >= 30.0 and cpa >= cpa_target:
        return "full"
    if hc >= 15.0:
        return "partial"
    return "violated"


def _eval_rule15(s: dict) -> str:
    role = s.get("role", "give_way")
    hc = abs(s.get("heading_change_deg", 0.0))
    cpa = s.get("cpa_nm", 0.0)
    cpa_target = s.get("cpa_target_nm", 0.27)
    if role == "give_way":
        if hc >= 30.0 and cpa >= cpa_target:
            return "full"
        if hc >= 15.0:
            return "partial"
        return "violated"
    if hc < 5.0:
        return "full"
    if hc < 15.0:
        return "partial"
    return "violated"


def _eval_rule16(s: dict) -> str:
    hc = abs(s.get("heading_change_deg", 0.0))
    cpa = s.get("cpa_nm", 0.0)
    cpa_target = s.get("cpa_target_nm", 0.27)
    early = s.get("acted_early", True)
    if hc >= 30.0 and cpa >= cpa_target and early:
        return "full"
    if hc >= 15.0:
        return "partial"
    return "violated"


def _eval_rule17(s: dict) -> str:
    hc = abs(s.get("heading_change_deg", 0.0))
    stage = s.get("timing_stage", "STAGE_1")
    if stage in ("STAGE_1", "STAGE_2"):
        if hc < 5.0:
            return "full"
        if hc < 15.0:
            return "partial"
        return "violated"
    return "partial"
