from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from types import MappingProxyType
from typing import Any

import yaml

ADAPTER_ROLES = frozenset({"target", "ownship", "environment", "route_in", "route_out"})


class IntegrationProfileError(ValueError):
    """Raised when an integration profile is missing required structure."""


class AdapterState(Enum):
    ENABLED = "enabled"
    DISABLED = "disabled"


@dataclass(frozen=True)
class ExternalDomain:
    domain_id: int
    workspace_setup: str
    required_topics: Mapping[str, str]


@dataclass(frozen=True)
class AdapterConfig:
    name: str
    state: AdapterState


@dataclass(frozen=True)
class FreshnessConfig:
    ownship_ms: int
    targets_ms: int
    environment_ms: int


@dataclass(frozen=True)
class SafetyConfig:
    route_out_requires_screen02_pass: bool
    forbid_low_level_control: bool


@dataclass(frozen=True)
class IntegrationProfile:
    name: str
    mode: str
    tdl_domain_id: int
    adapters: Mapping[str, AdapterConfig]
    external_domains: Mapping[str, ExternalDomain]
    freshness: FreshnessConfig
    safety: SafetyConfig


def load_profile(path: Path) -> IntegrationProfile:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise IntegrationProfileError(f"{path}: profile must be a mapping")

    name = raw.get("name")
    if not isinstance(name, str) or not name:
        raise IntegrationProfileError(f"{path}: missing name")

    mode = raw.get("mode")
    if mode not in {"default", "external"}:
        raise IntegrationProfileError(f"{path}: invalid mode {mode!r}")

    tdl_domain_id = raw.get("tdl_domain_id")
    if type(tdl_domain_id) is not int:
        raise IntegrationProfileError(f"{path}: missing tdl_domain_id")

    adapters = _parse_adapters(path, _section(path, raw, "adapters"))
    external_domains = _parse_external_domains(
        path, _optional_section(path, raw, "external_domains")
    )
    if mode == "external" and not external_domains:
        raise IntegrationProfileError(f"{path}: external mode requires external_domains")

    return IntegrationProfile(
        name=name,
        mode=mode,
        tdl_domain_id=tdl_domain_id,
        adapters=adapters,
        external_domains=external_domains,
        freshness=_parse_freshness(path, _section(path, raw, "freshness")),
        safety=_parse_safety(path, _section(path, raw, "safety")),
    )


def load_profiles(directory: Path) -> dict[str, IntegrationProfile]:
    profiles: dict[str, IntegrationProfile] = {}
    for path in sorted(directory.glob("*.yaml")):
        profile = load_profile(path)
        if profile.name in profiles:
            raise IntegrationProfileError(
                f"{directory}: duplicate profile name {profile.name!r}"
            )
        profiles[profile.name] = profile
    if "default" not in profiles:
        raise IntegrationProfileError(f"{directory}: missing default profile")
    return profiles


def _section(path: Path, raw: dict[str, Any], key: str) -> dict[str, Any]:
    section = raw.get(key)
    if not isinstance(section, dict):
        raise IntegrationProfileError(f"{path}: {key} must be a mapping")
    return section


def _optional_section(path: Path, raw: dict[str, Any], key: str) -> dict[str, Any]:
    section = raw.get(key, {})
    if not isinstance(section, dict):
        raise IntegrationProfileError(f"{path}: {key} must be a mapping")
    return section


def _parse_adapters(path: Path, raw: dict[str, Any]) -> Mapping[str, AdapterConfig]:
    roles = set(raw)
    if roles != ADAPTER_ROLES:
        raise IntegrationProfileError(
            f"{path}: adapters must contain exactly {sorted(ADAPTER_ROLES)}"
        )
    adapters: dict[str, AdapterConfig] = {}
    for name, value in raw.items():
        if not isinstance(name, str):
            raise IntegrationProfileError(f"{path}: adapter name must be a string")
        try:
            state = AdapterState(value)
        except ValueError as exc:
            raise IntegrationProfileError(
                f"{path}: invalid adapter state for {name}: {value!r}"
            ) from exc
        adapters[name] = AdapterConfig(name=name, state=state)
    return MappingProxyType(adapters)


def _parse_external_domains(
    path: Path, raw: dict[str, Any]
) -> Mapping[str, ExternalDomain]:
    domains: dict[str, ExternalDomain] = {}
    for name, value in raw.items():
        if not isinstance(name, str) or not name:
            raise IntegrationProfileError(f"{path}: external domain name must be a string")
        if not isinstance(value, dict):
            raise IntegrationProfileError(f"{path}: external_domains.{name} must be a mapping")
        required_topics = value.get("required_topics")
        if not isinstance(required_topics, dict):
            raise IntegrationProfileError(
                f"{path}: external_domains.{name}.required_topics must be a mapping"
            )
        domains[name] = ExternalDomain(
            domain_id=_int_field(path, value, f"external_domains.{name}.domain_id", "domain_id"),
            workspace_setup=_str_field(
                path,
                value,
                f"external_domains.{name}.workspace_setup",
                "workspace_setup",
            ),
            required_topics=MappingProxyType({
                _require_str(path, topic_key, f"external_domains.{name}.required_topics key"):
                _require_str(path, topic_type, f"external_domains.{name}.required_topics.{topic_key}")
                for topic_key, topic_type in required_topics.items()
            }),
        )
    return MappingProxyType(domains)


def _parse_freshness(path: Path, raw: dict[str, Any]) -> FreshnessConfig:
    return FreshnessConfig(
        ownship_ms=_int_field(path, raw, "freshness.ownship_ms", "ownship_ms"),
        targets_ms=_int_field(path, raw, "freshness.targets_ms", "targets_ms"),
        environment_ms=_int_field(path, raw, "freshness.environment_ms", "environment_ms"),
    )


def _parse_safety(path: Path, raw: dict[str, Any]) -> SafetyConfig:
    return SafetyConfig(
        route_out_requires_screen02_pass=_bool_field(
            path,
            raw,
            "safety.route_out_requires_screen02_pass",
            "route_out_requires_screen02_pass",
        ),
        forbid_low_level_control=_bool_field(
            path, raw, "safety.forbid_low_level_control", "forbid_low_level_control"
        ),
    )


def _int_field(path: Path, raw: dict[str, Any], label: str, key: str) -> int:
    value = raw.get(key)
    if type(value) is not int:
        raise IntegrationProfileError(f"{path}: {label} must be an integer")
    return value


def _str_field(path: Path, raw: dict[str, Any], label: str, key: str) -> str:
    return _require_str(path, raw.get(key), label)


def _bool_field(path: Path, raw: dict[str, Any], label: str, key: str) -> bool:
    value = raw.get(key)
    if not isinstance(value, bool):
        raise IntegrationProfileError(f"{path}: {label} must be a boolean")
    return value


def _require_str(path: Path, value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise IntegrationProfileError(f"{path}: {label} must be a string")
    return value
