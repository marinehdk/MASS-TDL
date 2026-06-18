from __future__ import annotations

from target_vessel.colregs_behavior import ColregsRuleFsm, TargetBehaviorState
from target_vessel.config import TargetBehaviorConfig
from target_vessel.geometry import VesselKinematics


def _cfg(**extra):
    kwargs = {"policy": "colregs_rule_fsm"}
    kwargs.update(extra)
    return TargetBehaviorConfig(**kwargs)


def test_rule14_head_on_turns_starboard_after_delay():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, min_turn_deg=30.0))
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=180.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 14"
    assert action.reason == "head_on_starboard"
    assert action.desired_heading_deg == 210.0


def test_nonzero_reaction_delay_monitors_before_give_way():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=5.0, min_turn_deg=30.0))
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    first = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=180.0)
    assert fsm.state.state == "MONITORING"
    assert first.reason == "reaction_delay"
    assert first.desired_heading_deg == 180.0

    later = fsm.update(now_s=7.0, own=own, target=target, nominal_heading_deg=180.0)
    assert fsm.state.state == "GIVE_WAY"
    assert later.rule == "Rule 14"
    assert later.reason == "head_on_starboard"
    assert later.desired_heading_deg == 210.0


def test_rule15_target_give_way_when_ownship_on_target_starboard():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, min_turn_deg=30.0))
    own = VesselKinematics(lat=63.0, lon=10.01, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 15"
    assert action.desired_heading_deg == 30.0


def test_rule17_stand_on_holds_when_ownship_action_observed():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, standon_action_tcpa_s=90.0))
    own0 = VesselKinematics(lat=63.005, lon=9.998, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    fsm.update(now_s=1.0, own=own0, target=target, nominal_heading_deg=0.0)
    own_turning = VesselKinematics(lat=63.005, lon=9.998, heading_deg=258.0, sog_mps=5.0)
    action = fsm.update(now_s=2.0, own=own_turning, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "STAND_ON"
    assert action.reason == "stand_on_hold_observed_ownship_action"
    assert action.desired_heading_deg == 0.0


def test_rule17_independent_action_when_ownship_not_acting_and_tcpa_short():
    fsm = ColregsRuleFsm(
        _cfg(
            reaction_delay_s=0.0,
            standon_hold_tcpa_s=500.0,
            standon_action_tcpa_s=500.0,
            emergency_tcpa_s=30.0,
            min_turn_deg=30.0,
        )
    )
    own = VesselKinematics(lat=63.005, lon=9.998, heading_deg=270.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    action = fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=0.0)
    assert fsm.state.state == "GIVE_WAY"
    assert action.rule == "Rule 17"
    assert action.reason == "rule17_independent_action"
    assert action.desired_heading_deg == 30.0


def test_returning_after_conflict_clears():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=0.0, clear_dwell_s=0.0, return_cooldown_s=0.0))
    own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    fsm.update(now_s=1.0, own=own, target=target, nominal_heading_deg=180.0)
    cleared_own = VesselKinematics(lat=62.99, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    cleared_target = VesselKinematics(lat=63.0, lon=10.03, heading_deg=210.0, sog_mps=5.0)
    action = fsm.update(
        now_s=20.0,
        own=cleared_own,
        target=cleared_target,
        nominal_heading_deg=180.0,
    )
    assert fsm.state.state in {"RETURNING", "NOMINAL"}
    assert action.desired_heading_deg == 180.0


def test_reentering_after_clear_resets_delay_and_encounter_baselines():
    fsm = ColregsRuleFsm(_cfg(reaction_delay_s=5.0, clear_dwell_s=0.0, min_turn_deg=30.0))
    first_own = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    first_target = VesselKinematics(lat=63.01, lon=10.0, heading_deg=180.0, sog_mps=5.0)
    fsm.update(now_s=1.0, own=first_own, target=first_target, nominal_heading_deg=180.0)
    fsm.update(now_s=7.0, own=first_own, target=first_target, nominal_heading_deg=180.0)

    cleared_own = VesselKinematics(lat=62.99, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    cleared_target = VesselKinematics(lat=63.0, lon=10.03, heading_deg=210.0, sog_mps=5.0)
    clear_action = fsm.update(
        now_s=20.0,
        own=cleared_own,
        target=cleared_target,
        nominal_heading_deg=180.0,
    )
    assert fsm.state.state == "RETURNING"
    assert clear_action.reason == "conflict_clear_returning"
    assert fsm.state.clear_since_s == 20.0

    new_own = VesselKinematics(lat=63.005, lon=9.998, heading_deg=270.0, sog_mps=5.0)
    new_target = VesselKinematics(lat=63.0, lon=10.0, heading_deg=0.0, sog_mps=5.0)
    reentered = fsm.update(now_s=22.0, own=new_own, target=new_target, nominal_heading_deg=0.0)
    assert fsm.state.state == "MONITORING"
    assert reentered.reason == "reaction_delay"
    assert reentered.desired_heading_deg == 0.0
    assert fsm.state.clear_since_s is None
    assert fsm.state.encounter_start_s == 22.0
    assert fsm.state.encounter_start_own_heading_deg == 270.0
    assert fsm.state.encounter_start_heading_deg == 0.0


def test_state_is_dataclass_for_per_mmsi_storage():
    state = TargetBehaviorState()
    assert state.state == "NOMINAL"
    assert state.encounter_start_heading_deg is None
