from __future__ import annotations

import pytest

from target_vessel.config import (
    ReservedPolicyError,
    UnsupportedSourcePolicyError,
    count_colregs_rule_targets,
    normalize_target_config,
)


def _base_entry(**extra):
    entry = {
        "static": {"mmsi": 100000001},
        "initial": {
            "position": {"latitude": 63.4, "longitude": 10.4},
            "heading": 180.0,
            "cog": 180.0,
            "sog": 10.0,
        },
        "model": "ais_replay_vessel",
    }
    entry.update(extra)
    return entry


def test_legacy_ais_replay_maps_to_route_passive():
    cfg = normalize_target_config(_base_entry(model="ais_replay_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "passive"
    assert cfg.legacy_model == "ais_replay_vessel"


def test_legacy_ncdm_maps_to_route_ncdm():
    cfg = normalize_target_config(_base_entry(model="ncdm_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "ncdm"


def test_legacy_intelligent_maps_to_colregs_rule_fsm():
    cfg = normalize_target_config(_base_entry(model="intelligent_vessel"))
    assert cfg.source.type == "route"
    assert cfg.behavior.policy == "colregs_rule_fsm"


def test_new_fields_override_legacy_model():
    cfg = normalize_target_config(
        _base_entry(
            model="ais_replay_vessel",
            source={"type": "route"},
            behavior={"policy": "colregs_rule_fsm", "min_turn_deg": 35.0},
        )
    )
    assert cfg.behavior.policy == "colregs_rule_fsm"
    assert cfg.behavior.min_turn_deg == 35.0
    assert cfg.legacy_model == "ais_replay_vessel"
    assert cfg.used_new_fields is True


def test_colregs_rule_fsm_rejects_ais_replay_source():
    with pytest.raises(UnsupportedSourcePolicyError):
        normalize_target_config(
            _base_entry(
                source={"type": "ais_replay"},
                behavior={"policy": "colregs_rule_fsm"},
            )
        )


@pytest.mark.parametrize("source_type", ("ais_replay", "ais_live"))
@pytest.mark.parametrize("policy", ("ncdm", "colregs_rule_fsm"))
def test_ais_sources_reject_non_passive_policies(source_type: str, policy: str):
    with pytest.raises(UnsupportedSourcePolicyError):
        normalize_target_config(
            _base_entry(source={"type": source_type}, behavior={"policy": policy})
        )


def test_reserved_policies_fail_fast():
    for policy in ("intelligent_planner", "tdl_agent"):
        with pytest.raises(ReservedPolicyError):
            normalize_target_config(_base_entry(behavior={"policy": policy}))


def test_illegal_numeric_parameter_fails_fast():
    with pytest.raises(ValueError, match="rot_limit_deg_s"):
        normalize_target_config(
            _base_entry(
                source={"type": "route"},
                behavior={"policy": "colregs_rule_fsm", "rot_limit_deg_s": 0.0},
            )
        )


def test_count_colregs_rule_targets():
    configs = [
        normalize_target_config(_base_entry(model="ais_replay_vessel")),
        normalize_target_config(
            _base_entry(
                source={"type": "route"},
                behavior={"policy": "colregs_rule_fsm"},
            )
        ),
    ]
    assert count_colregs_rule_targets(configs) == 1
