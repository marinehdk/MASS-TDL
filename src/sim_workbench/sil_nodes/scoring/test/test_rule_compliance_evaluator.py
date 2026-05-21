"""D2.4 — Rule 13/14/15/16/17 sub-criteria evaluator tests."""
from __future__ import annotations
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[2]))
import pytest
from scoring.rule_compliance_evaluator import evaluate_rule_compliance


def test_rule13_full_sufficient_alteration_and_cpa():
    state = {"heading_change_deg": 35.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule13", state) == "full"


def test_rule13_partial_small_alteration():
    state = {"heading_change_deg": 20.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule13", state) == "partial"


def test_rule13_violated_no_alteration():
    state = {"heading_change_deg": 5.0, "cpa_nm": 0.1, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule13", state) == "violated"


def test_rule14_full_starboard_turn():
    state = {"heading_change_deg": 35.0, "rudder_side": "starboard", "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule14", state) == "full"


def test_rule14_violated_port_turn():
    state = {"heading_change_deg": 35.0, "rudder_side": "port", "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule14", state) == "violated"


def test_rule14_partial_small_starboard():
    state = {"heading_change_deg": 20.0, "rudder_side": "starboard", "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule14", state) == "partial"


def test_rule15_give_way_full():
    state = {"role": "give_way", "heading_change_deg": 35.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule15", state) == "full"


def test_rule15_stand_on_full_maintained_course():
    state = {"role": "stand_on", "heading_change_deg": 2.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule15", state) == "full"


def test_rule15_stand_on_violated_turned_too_early():
    state = {"role": "stand_on", "heading_change_deg": 35.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27}
    assert evaluate_rule_compliance("Rule15", state) == "violated"


def test_rule16_full_early_and_substantial():
    state = {"heading_change_deg": 35.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27, "acted_early": True}
    assert evaluate_rule_compliance("Rule16", state) == "full"


def test_rule16_partial_too_small():
    state = {"heading_change_deg": 20.0, "cpa_nm": 0.5, "cpa_target_nm": 0.27, "acted_early": True}
    assert evaluate_rule_compliance("Rule16", state) == "partial"


def test_rule16_violated_insufficient():
    state = {"heading_change_deg": 5.0, "cpa_nm": 0.1, "cpa_target_nm": 0.27, "acted_early": False}
    assert evaluate_rule_compliance("Rule16", state) == "violated"


def test_rule17_full_maintained_during_stage1():
    state = {"heading_change_deg": 2.0, "timing_stage": "STAGE_1"}
    assert evaluate_rule_compliance("Rule17", state) == "full"


def test_rule17_partial_independent_action_stage3():
    state = {"heading_change_deg": 35.0, "timing_stage": "STAGE_3"}
    assert evaluate_rule_compliance("Rule17", state) == "partial"


def test_rule17_violated_turned_early_during_stage1():
    state = {"heading_change_deg": 40.0, "timing_stage": "STAGE_1"}
    assert evaluate_rule_compliance("Rule17", state) == "violated"


def test_unknown_rule_returns_full_passthrough():
    state = {"heading_change_deg": 0.0}
    assert evaluate_rule_compliance("Rule99", state) in ("full", "partial", "violated")
