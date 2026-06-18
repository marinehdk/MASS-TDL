from __future__ import annotations

from dataclasses import dataclass
from typing import Any


class UnsupportedSourcePolicyError(ValueError):
    """Raised when a behavior policy is incompatible with a source type."""


class ReservedPolicyError(ValueError):
    """Raised when a future policy is selected before v1 supports it."""


@dataclass(frozen=True)
class TargetSourceConfig:
    type: str = "route"


@dataclass(frozen=True)
class TargetBehaviorConfig:
    policy: str = "passive"
    reaction_delay_s: float = 6.0
    min_turn_deg: float = 30.0
    rot_limit_deg_s: float = 3.0
    role_lock_s: float = 20.0
    target_cpa_m: float = 900.0
    standon_hold_tcpa_s: float = 180.0
    standon_action_tcpa_s: float = 90.0
    emergency_tcpa_s: float = 45.0
    observed_action_heading_delta_deg: float = 8.0
    observed_action_dcpa_gain_m: float = 150.0
    clear_dwell_s: float = 10.0
    return_cooldown_s: float = 20.0


@dataclass(frozen=True)
class NormalizedTargetConfig:
    source: TargetSourceConfig
    behavior: TargetBehaviorConfig
    legacy_model: str | None
    used_new_fields: bool


_LEGACY_MODEL_MAP = {
    "ais_replay_vessel": ("route", "passive"),
    "replay": ("route", "passive"),
    "ncdm_vessel": ("route", "ncdm"),
    "ncdm": ("route", "ncdm"),
    "intelligent_vessel": ("route", "colregs_rule_fsm"),
    "intelligent": ("route", "colregs_rule_fsm"),
}

_SOURCE_TYPES = {"route", "injected_geometry", "ais_replay", "ais_live"}
_POLICIES = {"passive", "ncdm", "colregs_rule_fsm", "intelligent_planner", "tdl_agent"}


def _as_float(raw: dict[str, Any], key: str, default: float, *, positive: bool = False) -> float:
    value = float(raw.get(key, default))
    if positive and value <= 0.0:
        raise ValueError(f"{key} must be > 0")
    if not positive and value < 0.0:
        raise ValueError(f"{key} must be >= 0")
    return value


def normalize_target_config(entry: dict[str, Any]) -> NormalizedTargetConfig:
    legacy_model = entry.get("model")
    if legacy_model is None:
        legacy_key = "ais_replay_vessel"
        source_type, policy = ("route", "passive")
    else:
        legacy_key = str(legacy_model)
        if legacy_key not in _LEGACY_MODEL_MAP:
            raise ValueError(f"unsupported legacy model: {legacy_key}")
        source_type, policy = _LEGACY_MODEL_MAP[legacy_key]

    source_raw = entry.get("source")
    behavior_raw = entry.get("behavior")
    used_new_fields = isinstance(source_raw, dict) or isinstance(behavior_raw, dict)

    if isinstance(source_raw, dict):
        source_type = str(source_raw.get("type", source_type))
    if isinstance(behavior_raw, dict):
        policy = str(behavior_raw.get("policy", policy))
    else:
        behavior_raw = {}

    if source_type not in _SOURCE_TYPES:
        raise ValueError(f"unsupported source.type: {source_type}")
    if policy not in _POLICIES:
        raise ValueError(f"unsupported behavior.policy: {policy}")
    if policy in {"intelligent_planner", "tdl_agent"}:
        raise ReservedPolicyError(f"behavior.policy={policy} is reserved for a later phase")
    if source_type in {"ais_replay", "ais_live"} and policy != "passive":
        raise UnsupportedSourcePolicyError(
            f"ais_replay/ais_live sources require passive behavior, got {policy}"
        )
    if policy == "colregs_rule_fsm" and source_type != "route":
        raise UnsupportedSourcePolicyError(
            f"colregs_rule_fsm requires source.type=route, got {source_type}"
        )

    behavior = TargetBehaviorConfig(
        policy=policy,
        reaction_delay_s=_as_float(behavior_raw, "reaction_delay_s", 6.0),
        min_turn_deg=_as_float(behavior_raw, "min_turn_deg", 30.0, positive=True),
        rot_limit_deg_s=_as_float(behavior_raw, "rot_limit_deg_s", 3.0, positive=True),
        role_lock_s=_as_float(behavior_raw, "role_lock_s", 20.0),
        target_cpa_m=_as_float(behavior_raw, "target_cpa_m", 900.0, positive=True),
        standon_hold_tcpa_s=_as_float(behavior_raw, "standon_hold_tcpa_s", 180.0, positive=True),
        standon_action_tcpa_s=_as_float(behavior_raw, "standon_action_tcpa_s", 90.0, positive=True),
        emergency_tcpa_s=_as_float(behavior_raw, "emergency_tcpa_s", 45.0, positive=True),
        observed_action_heading_delta_deg=_as_float(
            behavior_raw, "observed_action_heading_delta_deg", 8.0, positive=True
        ),
        observed_action_dcpa_gain_m=_as_float(
            behavior_raw, "observed_action_dcpa_gain_m", 150.0
        ),
        clear_dwell_s=_as_float(behavior_raw, "clear_dwell_s", 10.0),
        return_cooldown_s=_as_float(behavior_raw, "return_cooldown_s", 20.0),
    )
    if behavior.emergency_tcpa_s > behavior.standon_action_tcpa_s:
        raise ValueError("emergency_tcpa_s must be <= standon_action_tcpa_s")
    if behavior.standon_action_tcpa_s > behavior.standon_hold_tcpa_s:
        raise ValueError("standon_action_tcpa_s must be <= standon_hold_tcpa_s")

    return NormalizedTargetConfig(
        source=TargetSourceConfig(type=source_type),
        behavior=behavior,
        legacy_model=legacy_model,
        used_new_fields=used_new_fields,
    )


def count_colregs_rule_targets(configs: list[NormalizedTargetConfig]) -> int:
    return sum(1 for cfg in configs if cfg.behavior.policy == "colregs_rule_fsm")
