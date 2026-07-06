from pathlib import Path

import yaml

from tools.sil.colregs_oracle_adapter import extract_compiled, extract_m6_output


def test_rule13_probe_compiles_as_dynamic_overtaking_not_head_on():
    doc = yaml.safe_load(
        Path("scenarios/COLREGs测试/colreg-rule13-ot.yaml").read_text(
            encoding="utf-8"
        )
    )

    compiled = extract_compiled(doc)

    assert compiled["geometry"]["rel_bearing_deg"] <= 6.0
    assert compiled["compiled_rule"] == "Rule13_Overtaking"
    assert compiled["own_role"] == "GIVE_WAY"


def test_m6_extract_marks_rule17_in_extremis_stand_on_action():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 760.0,
            "conflict_detected": True,
            "primary_role": 0,
            "primary_preferred_direction": "STARBOARD",
            "active_rules": [
                {
                    "rule_id": 15,
                    "role": 0,
                    "rule_phase": "T_act",
                    "preferred_direction": "HOLD",
                },
                {
                    "rule_id": 17,
                    "role": 0,
                    "rule_phase": "T_act",
                    "preferred_direction": "STARBOARD",
                },
            ],
        }
    ]

    output = extract_m6_output(rows)

    assert output["rule"] == "Rule15_Crossing"
    assert output["role"] == "STAND_ON"
    assert output["preferred_direction"] == "STARBOARD_TURN"
    assert output["stand_on_in_extremis_action"] is True


def test_m6_extract_keeps_stand_on_no_action_diagnostic_rule():
    rows = [
        {
            "topic": "/l3/m6/colregs_constraint",
            "sim_t": 48.0,
            "conflict_detected": False,
            "primary_role": 3,
            "primary_preferred_direction": "HOLD",
            "active_rules": [
                {
                    "rule_id": 13,
                    "role": 0,
                    "rule_phase": "T_standOn",
                    "preferred_direction": "HOLD",
                },
                {
                    "rule_id": 17,
                    "role": 0,
                    "rule_phase": "T_standOn",
                    "preferred_direction": "HOLD",
                },
            ],
        }
    ]

    output = extract_m6_output(rows)

    assert output["rule"] == "Rule13_Overtaking"
    assert output["role"] == "STAND_ON"
    assert output["preferred_direction"] == "HOLD"
    assert output["no_own_action_required"] is True
