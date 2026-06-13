import json
from pathlib import Path

import pytest

from sil_orchestrator.runtime.compose import ComposeRuntime
from sil_orchestrator.runtime.manifests import load_plugin_manifests, load_runtime_profiles
from sil_orchestrator.runtime.service import RuntimeConsoleService


class FakeCompose(ComposeRuntime):
    def __init__(self):
        self.started = []
        self.stopped = []
        self.restarted = []

    def ps_json(self):
        return json.dumps(
            [
                {
                    "Service": "sil-orchestrator",
                    "Name": "mass-l3-sil-sil-orchestrator-1",
                    "State": "running",
                    "Health": "healthy",
                    "Image": "mass-l3-sil-sil-orchestrator",
                },
                {
                    "Service": "plugin-route-l2-main",
                    "Name": "mass-l3-plugin-route",
                    "State": "running",
                    "Health": "starting",
                    "Image": "mass-l2-planner:main",
                },
            ]
        )

    def restart_service(self, service):
        self.restarted.append(service)

    def start_service(self, service):
        self.started.append(service)

    def stop_plugin_service(self, service):
        self.stopped.append(service)

    def switch_plugin(self, old_service, new_service):
        if old_service:
            self.stopped.append(old_service)
        self.started.append(new_service)


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
