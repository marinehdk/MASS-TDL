from __future__ import annotations

from dataclasses import dataclass

from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import (
    CpaTcpa,
    VesselKinematics,
    aspect_from_target_deg,
    compute_cpa_tcpa,
    relative_bearing_deg,
    signed_delta_deg,
    wrap_deg,
)


@dataclass
class TargetBehaviorState:
    state: str = "NOMINAL"
    rule: str | None = None
    role: str | None = None
    reason: str = "nominal"
    encounter_start_s: float | None = None
    encounter_start_own_heading_deg: float | None = None
    encounter_start_heading_deg: float | None = None
    encounter_start_dcpa_m: float | None = None
    clear_since_s: float | None = None
    last_transition_s: float = 0.0


@dataclass(frozen=True)
class TargetAction:
    desired_heading_deg: float
    desired_sog_mps: float | None
    state: str
    rule: str | None
    role: str | None
    reason: str
    emergency: bool
    dcpa_m: float
    tcpa_s: float


class ColregsRuleFsm:
    def __init__(self, cfg: TargetBehaviorConfig) -> None:
        self.cfg = cfg
        self.state = TargetBehaviorState()

    def update(
        self,
        *,
        now_s: float,
        own: VesselKinematics,
        target: VesselKinematics,
        nominal_heading_deg: float,
    ) -> TargetAction:
        cpa = compute_cpa_tcpa(own, target)
        encounter = self._classify(own, target, cpa)
        if encounter is None:
            return self._clear_or_nominal(now_s, target, nominal_heading_deg, cpa)

        rule, role = encounter
        self._prepare_encounter_context(rule, role)
        self._ensure_encounter_started(now_s, own, target, cpa)
        if not self._reaction_delay_elapsed(now_s):
            self._set_state("MONITORING", rule, role, "reaction_delay", now_s)
            return self._action(target.heading_deg, None, False, cpa)

        if rule == "Rule 14":
            return self._give_way(now_s, target, nominal_heading_deg, cpa, rule, "head_on_starboard")
        if rule == "Rule 13" and role == "GIVE_WAY":
            return self._give_way(
                now_s,
                target,
                nominal_heading_deg,
                cpa,
                rule,
                "overtaking_give_way_starboard",
            )
        if rule == "Rule 13" and role == "STAND_ON":
            self._set_state("STAND_ON", rule, role, "overtaken_stand_on_hold", now_s)
            return self._action(nominal_heading_deg, None, False, cpa)
        if rule == "Rule 15" and role == "GIVE_WAY":
            return self._give_way(
                now_s,
                target,
                nominal_heading_deg,
                cpa,
                rule,
                "crossing_give_way_starboard",
            )
        if rule == "Rule 17" and role == "STAND_ON":
            if self._ownship_action_observed(own, cpa):
                self._set_state("STAND_ON", rule, role, "stand_on_hold_observed_ownship_action", now_s)
                return self._action(nominal_heading_deg, None, False, cpa)
            if 0.0 < cpa.tcpa_s <= self.cfg.standon_action_tcpa_s:
                return self._give_way(
                    now_s,
                    target,
                    nominal_heading_deg,
                    cpa,
                    rule,
                    "rule17_independent_action",
                )
            self._set_state("STAND_ON", rule, role, "stand_on_hold", now_s)
            return self._action(nominal_heading_deg, None, False, cpa)

        self._set_state("MONITORING", rule, role, "monitoring", now_s)
        return self._action(target.heading_deg, None, False, cpa)

    def _prepare_encounter_context(self, rule: str, role: str) -> None:
        had_clear = self.state.clear_since_s is not None
        if had_clear:
            self.state.clear_since_s = None

        active_state = self.state.state in {"MONITORING", "GIVE_WAY", "STAND_ON"}
        changed_rule = self.state.rule != rule or self.state.role != role
        if self.state.encounter_start_s is None or not active_state or (had_clear and changed_rule):
            self._reset_encounter_baselines()

    def _classify(
        self,
        own: VesselKinematics,
        target: VesselKinematics,
        cpa: CpaTcpa,
    ) -> tuple[str, str] | None:
        if cpa.tcpa_s <= 0.0 or cpa.dcpa_m >= self.cfg.target_cpa_m:
            return None
        own_rel = relative_bearing_deg(own, target)
        tgt_aspect = aspect_from_target_deg(target, own)
        heading_diff = abs(signed_delta_deg(target.heading_deg, own.heading_deg))
        if abs(own_rel) <= 10.0 and heading_diff >= 160.0:
            return "Rule 14", "BOTH_GIVE_WAY"
        # Rule 13 overtaking. The overtaken vessel sees the overtaker abaft the
        # beam (>112.5 deg aspect). Two sub-cases from the FSM target's POV:
        #   * target is overtaken (own approaches from target's abaft) → STAND_ON
        #   * target is the overtaker (own lies in target's forward hemisphere
        #     while target closes from own's abaft) → GIVE_WAY.
        if abs(tgt_aspect) > 112.5:
            return "Rule 13", "STAND_ON"
        if abs(own_rel) > 112.5 and abs(heading_diff) < 67.5:
            return "Rule 13", "GIVE_WAY"
        target_sees_own = relative_bearing_deg(target, own)
        if target_sees_own > 0.0:
            return "Rule 15", "GIVE_WAY"
        return "Rule 17", "STAND_ON"

    def _ensure_encounter_started(
        self,
        now_s: float,
        own: VesselKinematics,
        target: VesselKinematics,
        cpa: CpaTcpa,
    ) -> None:
        if self.state.encounter_start_s is not None:
            return
        self.state.encounter_start_s = now_s
        self.state.encounter_start_own_heading_deg = own.heading_deg
        self.state.encounter_start_heading_deg = target.heading_deg
        self.state.encounter_start_dcpa_m = cpa.dcpa_m

    def _reset_encounter_baselines(self) -> None:
        self.state.encounter_start_s = None
        self.state.encounter_start_own_heading_deg = None
        self.state.encounter_start_heading_deg = None
        self.state.encounter_start_dcpa_m = None

    def _reaction_delay_elapsed(self, now_s: float) -> bool:
        if self.state.encounter_start_s is None:
            return False
        return (now_s - self.state.encounter_start_s) >= self.cfg.reaction_delay_s

    def _ownship_action_observed(self, own: VesselKinematics, cpa: CpaTcpa) -> bool:
        start_heading = self.state.encounter_start_own_heading_deg
        start_dcpa = self.state.encounter_start_dcpa_m
        heading_action = (
            start_heading is not None
            and abs(signed_delta_deg(own.heading_deg, start_heading))
            >= self.cfg.observed_action_heading_delta_deg
        )
        dcpa_action = (
            start_dcpa is not None
            and cpa.dcpa_m - start_dcpa >= self.cfg.observed_action_dcpa_gain_m
        )
        passing_safe = cpa.tcpa_s < 0.0 and cpa.dcpa_m >= self.cfg.target_cpa_m
        return heading_action or dcpa_action or passing_safe

    def _give_way(
        self,
        now_s: float,
        target: VesselKinematics,
        nominal_heading_deg: float,
        cpa: CpaTcpa,
        rule: str,
        reason: str,
    ) -> TargetAction:
        self._set_state("GIVE_WAY", rule, "GIVE_WAY", reason, now_s)
        emergency = 0.0 < cpa.tcpa_s <= self.cfg.emergency_tcpa_s
        speed = target.sog_mps * 0.7 if emergency else None
        return self._action(wrap_deg(nominal_heading_deg + self.cfg.min_turn_deg), speed, emergency, cpa)

    def _clear_or_nominal(
        self,
        now_s: float,
        target: VesselKinematics,
        nominal_heading_deg: float,
        cpa: CpaTcpa,
    ) -> TargetAction:
        if self.state.state in {"GIVE_WAY", "STAND_ON", "MONITORING"}:
            if self.state.clear_since_s is None:
                self.state.clear_since_s = now_s
            if now_s - self.state.clear_since_s >= self.cfg.clear_dwell_s:
                self._set_state("RETURNING", self.state.rule, self.state.role, "conflict_clear_returning", now_s)
                return self._action(nominal_heading_deg, None, False, cpa)
            return self._action(target.heading_deg, None, False, cpa)
        if self.state.state == "RETURNING":
            if abs(signed_delta_deg(target.heading_deg, nominal_heading_deg)) <= 1.0:
                self.state = TargetBehaviorState(state="NOMINAL", reason="nominal")
                return self._action(nominal_heading_deg, None, False, cpa)
            return self._action(nominal_heading_deg, None, False, cpa)
        return self._action(nominal_heading_deg, None, False, cpa)

    def _set_state(self, state: str, rule: str | None, role: str | None, reason: str, now_s: float) -> None:
        if self.state.state != state:
            self.state.last_transition_s = now_s
        self.state.state = state
        self.state.rule = rule
        self.state.role = role
        self.state.reason = reason

    def _action(
        self,
        desired_heading_deg: float,
        desired_sog_mps: float | None,
        emergency: bool,
        cpa: CpaTcpa,
    ) -> TargetAction:
        return TargetAction(
            desired_heading_deg=wrap_deg(desired_heading_deg),
            desired_sog_mps=desired_sog_mps,
            state=self.state.state,
            rule=self.state.rule,
            role=self.state.role,
            reason=self.state.reason,
            emergency=emergency,
            dcpa_m=cpa.dcpa_m,
            tcpa_s=cpa.tcpa_s,
        )
