from __future__ import annotations

import json
from collections.abc import Mapping
from pathlib import Path

from sil_orchestrator.runtime.compose import ComposeRuntime, ComposeRuntimeError
from sil_orchestrator.runtime.evidence import write_runtime_evidence
from sil_orchestrator.runtime.models import (
    PluginManifest,
    PluginRole,
    RuntimeProfile,
    ServiceClass,
)


CORE_SERVICES = (
    "sil-orchestrator",
    "sil-nodes",
    "foxglove-bridge",
    "martin-tile-server",
)


class RuntimeConsoleService:
    def __init__(
        self,
        plugins: Mapping[str, PluginManifest],
        profiles: Mapping[str, RuntimeProfile],
        compose: ComposeRuntime,
        runs_dir: Path,
        active_profile_name: str = "integration-local",
    ) -> None:
        if active_profile_name not in profiles:
            raise ValueError(f"unknown runtime profile {active_profile_name!r}")
        self.plugins = plugins
        self.profiles = profiles
        self.compose = compose
        self.runs_dir = runs_dir
        self.active_profile_name = active_profile_name
        self._selected_plugin_overrides: dict[PluginRole, str] = {}

    @property
    def active_profile(self) -> RuntimeProfile:
        return self.profiles[self.active_profile_name]

    def core_services(self) -> list[dict[str, object]]:
        services = self._compose_services_by_name()
        return [
            self._core_service_row(service, services.get(service))
            for service in CORE_SERVICES
        ]

    def plugin_roles(self) -> list[dict[str, object]]:
        services = self._compose_services_by_name()
        return self._plugin_roles(services)

    def summary(self) -> dict[str, object]:
        services = self._compose_services_by_name()
        gates = self._gates(services)
        return self._report(services, gates)

    def restart_core_service(self, service_id: str) -> dict[str, object]:
        if service_id not in CORE_SERVICES:
            return {
                "accepted": False,
                "error": f"unknown core service {service_id!r}",
            }
        self.compose.restart_service(service_id)
        return {"accepted": True, "action": "restart", "service": service_id}

    def start_core_stack(self) -> dict[str, object]:
        self.compose.start_core_stack()
        return {"accepted": True, "action": "start_core_stack"}

    def restart_core_stack(self) -> dict[str, object]:
        self.compose.restart_core_stack()
        return {"accepted": True, "action": "restart_core_stack"}

    def stop_core_stack(self, confirm: str) -> dict[str, object]:
        if confirm != "STOP_CORE_STACK":
            return {
                "accepted": False,
                "error": "confirm must equal STOP_CORE_STACK",
            }
        self.compose.stop_core_stack()
        return {"accepted": True, "action": "stop_core_stack"}

    def switch_plugin(self, role_value: str, plugin_id: str) -> dict[str, object]:
        try:
            role = PluginRole(role_value)
        except ValueError:
            return {"accepted": False, "error": f"unknown plugin role {role_value!r}"}

        plugin = self.plugins.get(plugin_id)
        if plugin is None:
            return {"accepted": False, "error": f"unknown plugin {plugin_id!r}"}
        if plugin.role is not role:
            return {
                "accepted": False,
                "error": (
                    f"plugin {plugin_id!r} has role {plugin.role.value!r}, "
                    f"not {role.value!r}"
                ),
            }

        old_plugin_id = self._effective_plugin_roles().get(role)
        old_service = None
        if old_plugin_id:
            old_service = self.plugins[old_plugin_id].compose.service
        new_service = plugin.compose.service
        self.compose.switch_plugin(old_service, new_service)
        self._selected_plugin_overrides[role] = plugin.id
        return {
            "accepted": True,
            "action": "switch_plugin",
            "role": role.value,
            "old_plugin": old_plugin_id,
            "new_plugin": plugin.id,
            "stopped_service": old_service,
            "started_service": new_service,
        }

    def probe(self, write_evidence: bool = True) -> dict[str, object]:
        services = self._compose_services_by_name()
        gates = self._gates(services)
        report = self._report(services, gates)
        if write_evidence:
            evidence_path = write_runtime_evidence(self.runs_dir, report)
            report["evidence_path"] = str(evidence_path)
        return report

    def _compose_services_by_name(self) -> dict[str, Mapping[str, object]]:
        services: dict[str, Mapping[str, object]] = {}
        for record in _compose_records(self.compose.ps_json()):
            if not isinstance(record, dict):
                continue
            service = record.get("Service")
            if isinstance(service, str) and service:
                services[service] = record
        return services

    def _report(
        self,
        services: Mapping[str, Mapping[str, object]],
        gates: list[dict[str, object]],
    ) -> dict[str, object]:
        profile = self.active_profile
        return {
            "mode": profile.mode.value,
            "target": profile.target.value,
            "active_profile": profile.name,
            "verdict": "GO" if all(gate["passed"] for gate in gates) else "NO-GO",
            "core_services": [
                self._core_service_row(service, services.get(service))
                for service in CORE_SERVICES
            ],
            "plugin_roles": self._plugin_roles(services),
            "gates": gates,
        }

    def _core_service_row(
        self, service: str, row: Mapping[str, object] | None
    ) -> dict[str, object]:
        return {
            "id": service,
            "service": service,
            "class": ServiceClass.CORE.value,
            "status": _state(row),
            "health": _health(row),
            "image": _field(row, "Image"),
            "container_name": _field(row, "Name"),
            "allowed_actions": ["restart"],
        }

    def _plugin_roles(
        self, services: Mapping[str, Mapping[str, object]]
    ) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        effective_plugin_roles = self._effective_plugin_roles()
        for role in self._reported_roles():
            active_plugin_id = effective_plugin_roles.get(role)
            plugins = sorted(
                (plugin for plugin in self.plugins.values() if plugin.role is role),
                key=lambda plugin: plugin.id,
            )
            rows.append(
                {
                    "role": role.value,
                    "active_plugin": active_plugin_id,
                    "single_instance": (
                        self.active_profile.safety.single_instance_per_role
                    ),
                    "plugins": [
                        self._plugin_row(plugin, services.get(plugin.compose.service))
                        for plugin in plugins
                    ],
                }
            )
        return rows

    def _plugin_row(
        self, plugin: PluginManifest, row: Mapping[str, object] | None
    ) -> dict[str, object]:
        return {
            "id": plugin.id,
            "label": plugin.label,
            "service": plugin.compose.service,
            "container": _field(row, "Name"),
            "status": _state(row),
            "health": _health(row),
            "image": _field(row, "Image", default=plugin.image.expected),
            "expected_image": plugin.image.expected,
            "revision": _label_value(row, plugin.image.revision_label) or "unchecked",
            "revision_label": plugin.image.revision_label,
            "required_topics": dict(plugin.ros.required_topics),
            "topic_status": "unchecked",
            "health_required": plugin.health_required,
            "ros_domain_id": plugin.ros.domain_id,
        }

    def _gates(
        self, services: Mapping[str, Mapping[str, object]]
    ) -> list[dict[str, object]]:
        core_statuses = {
            service: _state(services.get(service)) for service in CORE_SERVICES
        }
        plugin_roles = []
        effective_plugin_roles = self._effective_plugin_roles()
        for role in self._reported_roles():
            active_plugin_id = effective_plugin_roles.get(role)
            role_plugins = [
                plugin for plugin in self.plugins.values() if plugin.role is role
            ]
            running_plugins = sorted(
                plugin.id
                for plugin in role_plugins
                if _state(services.get(plugin.compose.service)) == "running"
            )
            expected_running = [active_plugin_id] if active_plugin_id else []
            plugin_roles.append(
                {
                    "role": role.value,
                    "active_plugin": active_plugin_id,
                    "running_plugins": running_plugins,
                    "passed": running_plugins == expected_running,
                }
            )

        return [
            {
                "name": "core_services_running",
                "passed": all(status == "running" for status in core_statuses.values()),
                "services": core_statuses,
            },
            {
                "name": "single_active_plugin_per_role",
                "passed": all(role["passed"] for role in plugin_roles),
                "roles": plugin_roles,
            },
        ]

    def _reported_roles(self) -> list[PluginRole]:
        return sorted(
            {plugin.role for plugin in self.plugins.values()}, key=lambda role: role.value
        )

    def _effective_plugin_roles(self) -> dict[PluginRole, str]:
        return {**self.active_profile.plugin_roles, **self._selected_plugin_overrides}


def _state(row: Mapping[str, object] | None) -> str:
    return _lower_field(row, "State")


def _health(row: Mapping[str, object] | None) -> str:
    return _lower_field(row, "Health")


def _lower_field(row: Mapping[str, object] | None, key: str) -> str:
    value = row.get(key) if row else None
    if value is None:
        return "unknown"
    text = str(value).strip().lower()
    return text or "unknown"


def _field(
    row: Mapping[str, object] | None, key: str, default: str = "unknown"
) -> str:
    value = row.get(key) if row else None
    if value is None:
        return default
    text = str(value).strip()
    return text or default


def _label_value(row: Mapping[str, object] | None, label: str) -> str | None:
    if row is None:
        return None
    labels = row.get("Labels")
    if isinstance(labels, Mapping):
        value = labels.get(label)
        return str(value) if value else None
    if not isinstance(labels, str):
        return None
    for item in labels.split(","):
        key, separator, value = item.partition("=")
        if separator and key == label and value:
            return value
    return None


def _compose_records(raw_json: str) -> list[object]:
    text = raw_json.strip()
    if not text:
        return []
    try:
        raw = json.loads(text)
    except json.JSONDecodeError:
        records: list[object] = []
        for line in text.splitlines():
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise ComposeRuntimeError("docker compose ps returned invalid JSON") from exc
        return records
    if isinstance(raw, dict):
        return [raw]
    if isinstance(raw, list):
        return raw
    return []
