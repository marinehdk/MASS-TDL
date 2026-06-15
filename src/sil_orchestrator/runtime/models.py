from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType


class RuntimeMode(Enum):
    INTERNAL = "internal"
    INTEGRATION = "integration"


class RuntimeTarget(Enum):
    LOCAL = "local"
    A4000 = "a4000"


class PluginRole(Enum):
    HYDRODYNAMICS = "hydrodynamics"
    ROUTE_L2 = "route_l2"
    FUSION = "fusion"


class ServiceClass(Enum):
    CORE = "core_service"
    PLUGIN = "plugin_service"


class ServiceStatus(Enum):
    RUNNING = "running"
    STOPPED = "stopped"
    UNKNOWN = "unknown"


class HealthStatus(Enum):
    HEALTHY = "healthy"
    STARTING = "starting"
    DEGRADED = "degraded"
    UNHEALTHY = "unhealthy"
    UNKNOWN = "unknown"


@dataclass(frozen=True)
class ComposeServiceRef:
    service: str


@dataclass(frozen=True)
class ImageMetadata:
    expected: str
    revision_label: str


@dataclass(frozen=True)
class RosTopicContract:
    domain_id: int
    required_topics: Mapping[str, str]
    forbidden_topics: tuple[str, ...]


@dataclass(frozen=True)
class PluginManifest:
    id: str
    role: PluginRole
    label: str
    runtime: str
    compose: ComposeServiceRef
    image: ImageMetadata
    ros: RosTopicContract
    freshness: Mapping[str, int]
    health_required: bool
    include_logs_tail_lines: int


@dataclass(frozen=True)
class RuntimeSafety:
    single_instance_per_role: bool
    forbid_low_level_control: bool
    require_version_metadata: bool


@dataclass(frozen=True)
class RuntimeProfile:
    name: str
    mode: RuntimeMode
    target: RuntimeTarget
    tdl_domain_id: int
    plugin_roles: Mapping[PluginRole, str]
    safety: RuntimeSafety


def freeze_mapping(
    value: dict[str, int] | dict[str, str],
) -> Mapping[str, int] | Mapping[str, str]:
    return MappingProxyType(dict(value))
