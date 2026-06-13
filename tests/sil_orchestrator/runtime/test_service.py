import json
from pathlib import Path

import pytest

from sil_orchestrator.runtime.compose import ComposeRuntime
from sil_orchestrator.runtime.evidence import write_runtime_evidence
from sil_orchestrator.runtime.manifests import load_plugin_manifests, load_runtime_profiles
from sil_orchestrator.runtime.service import RuntimeConsoleService


class FakeCompose(ComposeRuntime):
    def __init__(self, extra_services=None):
        self.started = []
        self.stopped = []
        self.restarted = []
        self.services = [
            {
                "Service": "sil-orchestrator",
                "Name": "mass-l3-sil-sil-orchestrator-1",
                "State": "running",
                "Health": "healthy",
                "Image": "mass-l3-sil-sil-orchestrator",
            },
            {
                "Service": "sil-nodes",
                "Name": "mass-l3-sil-sil-nodes-1",
                "State": "running",
                "Health": "healthy",
                "Image": "mass-l3-sil-nodes",
            },
            {
                "Service": "foxglove-bridge",
                "Name": "mass-l3-sil-foxglove-bridge-1",
                "State": "running",
                "Health": "healthy",
                "Image": "foxglove-bridge",
            },
            {
                "Service": "martin-tile-server",
                "Name": "mass-l3-sil-martin-tile-server-1",
                "State": "running",
                "Health": "healthy",
                "Image": "martin-tile-server",
            },
            {
                "Service": "plugin-route-l2-main",
                "Name": "mass-l3-plugin-route",
                "State": "running",
                "Health": "starting",
                "Image": "mass-l2-planner:main",
            },
            *(extra_services or []),
        ]

    def ps_json(self):
        return json.dumps(self.services)

    def restart_service(self, service):
        self.restarted.append(service)

    def start_service(self, service):
        self.started.append(service)
        self._set_state(service, "running")

    def stop_plugin_service(self, service):
        self.stopped.append(service)
        self._set_state(service, "stopped")

    def switch_plugin(self, old_service, new_service):
        if old_service:
            self.stop_plugin_service(old_service)
        self.start_service(new_service)

    def _set_state(self, service, state):
        for row in self.services:
            if row["Service"] == service:
                row["State"] = state
                return
        self.services.append(
            {
                "Service": service,
                "Name": service,
                "State": state,
                "Health": "unknown",
                "Image": "unknown",
            }
        )


def test_core_restart_uses_allowlisted_service(runtime_config_dirs, tmp_path):
    plugins = load_plugin_manifests(runtime_config_dirs["plugins"])
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    compose = FakeCompose()
    service = RuntimeConsoleService(plugins, profiles, compose, runs_dir=tmp_path)

    result = service.restart_core_service("sil-orchestrator")

    assert result["accepted"] is True
    assert compose.restarted == ["sil-orchestrator"]


def test_core_stop_requires_stack_confirmation(runtime_config_dirs, tmp_path):
    plugins = load_plugin_manifests(runtime_config_dirs["plugins"])
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    service = RuntimeConsoleService(plugins, profiles, FakeCompose(), runs_dir=tmp_path)

    result = service.stop_core_stack(confirm="wrong")

    assert result["accepted"] is False
    assert "STOP_CORE_STACK" in result["error"]


def test_switch_plugin_stops_old_service_starts_new_service(runtime_config_dirs, tmp_path):
    plugins = load_plugin_manifests(runtime_config_dirs["plugins"])
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    compose = FakeCompose()
    service = RuntimeConsoleService(plugins, profiles, compose, runs_dir=tmp_path)

    result = service.switch_plugin("route_l2", "tdl-mock-route")

    assert result["accepted"] is True
    assert compose.stopped == ["plugin-route-l2-main"]
    assert compose.started == ["plugin-route-tdl-mock"]


def test_probe_uses_switched_plugin_as_effective_active(runtime_config_dirs, tmp_path):
    plugins = load_plugin_manifests(runtime_config_dirs["plugins"])
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    compose = FakeCompose(
        extra_services=[
            {
                "Service": "plugin-route-tdl-mock",
                "Name": "mass-l3-plugin-route-mock",
                "State": "stopped",
                "Health": "unknown",
                "Image": "mass-l3-plugin-route-mock:local",
            },
        ]
    )
    service = RuntimeConsoleService(plugins, profiles, compose, runs_dir=tmp_path)

    service.switch_plugin("route_l2", "tdl-mock-route")
    report = service.probe(write_evidence=False)

    plugin_gate = next(
        gate for gate in report["gates"] if gate["name"] == "single_active_plugin_per_role"
    )
    route_role = next(
        role for role in plugin_gate["roles"] if role["role"] == "route_l2"
    )
    display_role = next(
        role for role in report["plugin_roles"] if role["role"] == "route_l2"
    )
    assert report["verdict"] == "GO"
    assert route_role["active_plugin"] == "tdl-mock-route"
    assert route_role["running_plugins"] == ["tdl-mock-route"]
    assert display_role["active_plugin"] == "tdl-mock-route"


def test_probe_writes_evidence(runtime_config_dirs, tmp_path):
    plugins = load_plugin_manifests(runtime_config_dirs["plugins"])
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    service = RuntimeConsoleService(plugins, profiles, FakeCompose(), runs_dir=tmp_path)

    report = service.probe()

    assert report["evidence_path"].startswith(str(tmp_path))
    evidence = json.loads(Path(report["evidence_path"]).read_text(encoding="utf-8"))
    assert evidence["mode"] in {"internal", "integration"}
    assert "core_services" in evidence
    assert "plugin_roles" in evidence


def test_runtime_evidence_paths_are_unique_per_call(tmp_path):
    first = write_runtime_evidence(tmp_path, {"mode": "integration"})
    second = write_runtime_evidence(tmp_path, {"mode": "integration"})

    assert first != second
    assert first.exists()
    assert second.exists()


def test_probe_rejects_running_unmapped_plugin_role(runtime_config_dirs, tmp_path):
    plugin_dir = runtime_config_dirs["plugins"]
    write_plugin_manifest(
        plugin_dir,
        plugin_id="hydro-fossen",
        role="hydrodynamics",
        label="Hydro Fossen",
        service="plugin-hydro-fossen",
        image="mass-hydro-fossen:local",
    )
    write_plugin_manifest(
        plugin_dir,
        plugin_id="yougc-fusion",
        role="fusion",
        label="YouGC Fusion",
        service="plugin-fusion-yougc",
        image="mass-yougc-fusion:local",
    )
    plugins = load_plugin_manifests(plugin_dir)
    profiles = load_runtime_profiles(runtime_config_dirs["profiles"], plugins)
    compose = FakeCompose(
        extra_services=[
            {
                "Service": "plugin-hydro-fossen",
                "Name": "mass-l3-plugin-hydro",
                "State": "running",
                "Health": "healthy",
                "Image": "mass-hydro-fossen:local",
            },
            {
                "Service": "plugin-fusion-yougc",
                "Name": "mass-l3-plugin-fusion",
                "State": "running",
                "Health": "healthy",
                "Image": "mass-yougc-fusion:local",
            },
        ]
    )
    service = RuntimeConsoleService(plugins, profiles, compose, runs_dir=tmp_path)

    report = service.probe(write_evidence=False)

    plugin_gate = next(
        gate for gate in report["gates"] if gate["name"] == "single_active_plugin_per_role"
    )
    failed_roles = {
        role["role"]: role
        for role in plugin_gate["roles"]
        if role["passed"] is False
    }
    assert report["verdict"] == "NO-GO"
    assert plugin_gate["passed"] is False
    assert failed_roles["hydrodynamics"]["active_plugin"] is None
    assert failed_roles["hydrodynamics"]["running_plugins"] == ["hydro-fossen"]
    assert failed_roles["fusion"]["active_plugin"] is None
    assert failed_roles["fusion"]["running_plugins"] == ["yougc-fusion"]


@pytest.fixture
def runtime_config_dirs(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    plugin_dir.mkdir()
    profile_dir.mkdir()
    (plugin_dir / "l2-planner-main.yaml").write_text(
        """
id: l2-planner-main
role: route_l2
label: L2 Planner Main
runtime: compose
compose:
  service: plugin-route-l2-main
image:
  expected: mass-l2-planner:main
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics: {}
  forbidden_topics:
    - /sil/actuator_cmd
freshness: {}
health:
  required: true
evidence:
  include_logs_tail_lines: 40
""",
        encoding="utf-8",
    )
    (plugin_dir / "tdl-mock-route.yaml").write_text(
        """
id: tdl-mock-route
role: route_l2
label: TDL Mock Route
runtime: compose
compose:
  service: plugin-route-tdl-mock
image:
  expected: mass-l3-plugin-route-mock:local
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 42
  required_topics: {}
  forbidden_topics:
    - /sil/actuator_cmd
freshness: {}
health:
  required: false
evidence:
  include_logs_tail_lines: 40
""",
        encoding="utf-8",
    )
    (profile_dir / "integration-local.yaml").write_text(
        """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  route_l2: l2-planner-main
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: false
""",
        encoding="utf-8",
    )
    return {"plugins": plugin_dir, "profiles": profile_dir}


def write_plugin_manifest(
    plugin_dir: Path,
    *,
    plugin_id: str,
    role: str,
    label: str,
    service: str,
    image: str,
) -> None:
    (plugin_dir / f"{plugin_id}.yaml").write_text(
        f"""
id: {plugin_id}
role: {role}
label: {label}
runtime: compose
compose:
  service: {service}
image:
  expected: {image}
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 42
  required_topics: {{}}
  forbidden_topics:
    - /sil/actuator_cmd
freshness: {{}}
health:
  required: true
evidence:
  include_logs_tail_lines: 40
""",
        encoding="utf-8",
    )
