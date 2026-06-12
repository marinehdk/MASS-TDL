from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
from types import MappingProxyType
from typing import Any, TypeVar

import yaml

from sil_orchestrator.runtime.models import (
    ComposeServiceRef,
    ImageMetadata,
    PluginManifest,
    PluginRole,
    RosTopicContract,
    RuntimeMode,
    RuntimeProfile,
    RuntimeSafety,
    RuntimeTarget,
    freeze_mapping,
)


class RuntimeManifestError(ValueError):
    """Raised when a runtime plugin manifest or profile is invalid."""


EnumT = TypeVar("EnumT", RuntimeMode, RuntimeTarget, PluginRole)


PLUGIN_KEYS = frozenset(
    {
        "id",
        "role",
        "label",
        "runtime",
        "compose",
        "image",
        "ros",
        "freshness",
        "health",
        "evidence",
    }
)
PROFILE_KEYS = frozenset(
    {"name", "mode", "target", "tdl_domain_id", "plugin_roles", "safety"}
)


def load_plugin_manifests(directory: Path) -> dict[str, PluginManifest]:
    plugins: dict[str, PluginManifest] = {}
    for path in sorted(directory.glob("*.yaml")):
        manifest = load_plugin_manifest(path)
        if manifest.id in plugins:
            raise RuntimeManifestError(
                f"{directory}: duplicate plugin id {manifest.id!r}"
            )
        plugins[manifest.id] = manifest
    return plugins


def load_plugin_manifest(path: Path) -> PluginManifest:
    raw = _load_yaml_mapping(path, "plugin manifest")
    _reject_unknown_keys(path, raw, PLUGIN_KEYS, "plugin manifest")

    compose = _section(path, raw, "compose")
    _reject_unknown_keys(path, compose, frozenset({"service"}), "compose")
    image = _section(path, raw, "image")
    _reject_unknown_keys(path, image, frozenset({"expected", "revision_label"}), "image")
    ros = _section(path, raw, "ros")
    _reject_unknown_keys(
        path,
        ros,
        frozenset({"domain_id", "required_topics", "forbidden_topics"}),
        "ros",
    )
    health = _section(path, raw, "health")
    _reject_unknown_keys(path, health, frozenset({"required"}), "health")
    evidence = _section(path, raw, "evidence")
    _reject_unknown_keys(
        path, evidence, frozenset({"include_logs_tail_lines"}), "evidence"
    )

    return PluginManifest(
        id=_str_field(path, raw, "id"),
        role=_enum_field(path, raw, "role", PluginRole),
        label=_str_field(path, raw, "label"),
        runtime=_runtime_field(path, raw),
        compose=ComposeServiceRef(
            service=_str_field(path, compose, "compose.service", "service")
        ),
        image=ImageMetadata(
            expected=_str_field(path, image, "image.expected", "expected"),
            revision_label=_str_field(
                path, image, "image.revision_label", "revision_label"
            ),
        ),
        ros=RosTopicContract(
            domain_id=_int_field(path, ros, "ros.domain_id", "domain_id"),
            required_topics=freeze_mapping(
                _str_str_mapping(path, ros, "ros.required_topics", "required_topics")
            ),
            forbidden_topics=_str_tuple(
                path, ros, "ros.forbidden_topics", "forbidden_topics"
            ),
        ),
        freshness=freeze_mapping(
            _str_int_mapping(path, _section(path, raw, "freshness"), "freshness")
        ),
        health_required=_bool_field(path, health, "health.required", "required"),
        include_logs_tail_lines=_int_field(
            path,
            evidence,
            "evidence.include_logs_tail_lines",
            "include_logs_tail_lines",
        ),
    )


def load_runtime_profiles(
    directory: Path, plugins: Mapping[str, PluginManifest]
) -> dict[str, RuntimeProfile]:
    profiles: dict[str, RuntimeProfile] = {}
    for path in sorted(directory.glob("*.yaml")):
        profile = load_runtime_profile(path, plugins)
        if profile.name in profiles:
            raise RuntimeManifestError(
                f"{directory}: duplicate profile name {profile.name!r}"
            )
        profiles[profile.name] = profile
    return profiles


def load_runtime_profile(
    path: Path, plugins: Mapping[str, PluginManifest]
) -> RuntimeProfile:
    raw = _load_yaml_mapping(path, "runtime profile")
    _reject_unknown_keys(path, raw, PROFILE_KEYS, "runtime profile")

    plugin_roles = _parse_plugin_roles(
        path, _section(path, raw, "plugin_roles"), plugins
    )
    safety = _section(path, raw, "safety")
    _reject_unknown_keys(
        path,
        safety,
        frozenset(
            {
                "single_instance_per_role",
                "forbid_low_level_control",
                "require_version_metadata",
            }
        ),
        "safety",
    )

    return RuntimeProfile(
        name=_str_field(path, raw, "name"),
        mode=_enum_field(path, raw, "mode", RuntimeMode),
        target=_enum_field(path, raw, "target", RuntimeTarget),
        tdl_domain_id=_int_field(path, raw, "tdl_domain_id"),
        plugin_roles=MappingProxyType(plugin_roles),
        safety=RuntimeSafety(
            single_instance_per_role=_bool_field(
                path, safety, "safety.single_instance_per_role", "single_instance_per_role"
            ),
            forbid_low_level_control=_bool_field(
                path, safety, "safety.forbid_low_level_control", "forbid_low_level_control"
            ),
            require_version_metadata=_bool_field(
                path,
                safety,
                "safety.require_version_metadata",
                "require_version_metadata",
            ),
        ),
    )


def _parse_plugin_roles(
    path: Path,
    raw: Mapping[str, Any],
    plugins: Mapping[str, PluginManifest],
) -> dict[PluginRole, str]:
    roles: dict[PluginRole, str] = {}
    for role_value, plugin_id_value in raw.items():
        role = _enum_value(path, role_value, PluginRole, "plugin_roles key")
        plugin_id = _require_str(path, plugin_id_value, f"plugin_roles.{role.value}")
        plugin = plugins.get(plugin_id)
        if plugin is None:
            raise RuntimeManifestError(
                f"{path}: unknown plugin {plugin_id!r} for role {role.value!r}"
            )
        if plugin.role is not role:
            raise RuntimeManifestError(
                f"{path}: role mismatch for {plugin_id!r}: "
                f"profile role {role.value!r}, plugin role {plugin.role.value!r}"
            )
        roles[role] = plugin_id
    return roles


def _load_yaml_mapping(path: Path, label: str) -> dict[str, Any]:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise RuntimeManifestError(f"{path}: {label} must be a mapping")
    return raw


def _section(path: Path, raw: Mapping[str, Any], key: str) -> dict[str, Any]:
    section = raw.get(key)
    if not isinstance(section, dict):
        raise RuntimeManifestError(f"{path}: {key} must be a mapping")
    return section


def _reject_unknown_keys(
    path: Path, raw: Mapping[str, Any], allowed: frozenset[str], label: str
) -> None:
    unknown = set(raw) - allowed
    if unknown:
        raise RuntimeManifestError(
            f"{path}: unknown {label} field {sorted(unknown)[0]!r}"
        )


def _enum_field(
    path: Path, raw: Mapping[str, Any], key: str, enum_type: type[EnumT]
) -> EnumT:
    return _enum_value(path, raw.get(key), enum_type, key)


def _enum_value(path: Path, value: Any, enum_type: type[EnumT], label: str) -> EnumT:
    try:
        return enum_type(_require_str(path, value, label))
    except ValueError as exc:
        raise RuntimeManifestError(f"{path}: invalid {label} {value!r}") from exc


def _str_field(
    path: Path, raw: Mapping[str, Any], label: str, key: str | None = None
) -> str:
    return _require_str(path, raw.get(key or label), label)


def _runtime_field(path: Path, raw: Mapping[str, Any]) -> str:
    runtime = _str_field(path, raw, "runtime")
    if runtime != "compose":
        raise RuntimeManifestError(f"{path}: invalid runtime {runtime!r}")
    return runtime


def _require_str(path: Path, value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeManifestError(f"{path}: {label} must be a string")
    return value


def _int_field(
    path: Path, raw: Mapping[str, Any], label: str, key: str | None = None
) -> int:
    value = raw.get(key or label)
    if type(value) is not int:
        raise RuntimeManifestError(f"{path}: {label} must be an integer")
    return value


def _bool_field(path: Path, raw: Mapping[str, Any], label: str, key: str) -> bool:
    value = raw.get(key)
    if not isinstance(value, bool):
        raise RuntimeManifestError(f"{path}: {label} must be a boolean")
    return value


def _str_str_mapping(
    path: Path, raw: Mapping[str, Any], label: str, key: str
) -> dict[str, str]:
    value = raw.get(key)
    if not isinstance(value, dict):
        raise RuntimeManifestError(f"{path}: {label} must be a mapping")
    return {
        _require_str(path, map_key, f"{label} key"): _require_str(
            path, map_value, f"{label}.{map_key}"
        )
        for map_key, map_value in value.items()
    }


def _str_int_mapping(path: Path, raw: Mapping[str, Any], label: str) -> dict[str, int]:
    parsed: dict[str, int] = {}
    for map_key, map_value in raw.items():
        key = _require_str(path, map_key, f"{label} key")
        if type(map_value) is not int:
            raise RuntimeManifestError(f"{path}: {label}.{key} must be an integer")
        parsed[key] = map_value
    return parsed


def _str_tuple(
    path: Path, raw: Mapping[str, Any], label: str, key: str
) -> tuple[str, ...]:
    value = raw.get(key)
    if not isinstance(value, list):
        raise RuntimeManifestError(f"{path}: {label} must be a list")
    return tuple(_require_str(path, item, f"{label} item") for item in value)
