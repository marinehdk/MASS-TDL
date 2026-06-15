# Plugin Runtime Console Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Screen 02 as a unified runtime console that manages TDL core containers and single-instance external plugin containers before simulation launch.

**Architecture:** Add a backend runtime domain under `src/sil_orchestrator/runtime/` with trusted manifests, compose-backed lifecycle actions, readiness probes, and evidence writing. Replace the small Screen 02 external panel with a category-driven React runtime console while preserving existing preflight GO blocking.

**Tech Stack:** FastAPI, Python dataclasses, Docker Compose CLI, ROS2 CLI probes, pytest, React, RTK Query, Vitest, Vite, existing local OrbStack/A4000 compose scripts.

---

## Source Documents

- Spec: `docs/superpowers/specs/2026-06-12-plugin-runtime-console-design.md`
- Reference UI: `docs/superpowers/specs/2026-06-12-plugin-runtime-console-reference-ui.html`
- Existing external adapter spec: `docs/Design/SIL/external-module-adapter-spec.md`
- Local-first rule: `AGENTS.md`

## File Structure

### Backend Runtime Domain

- Create `src/sil_orchestrator/runtime/__init__.py`
  - Package marker.
- Create `src/sil_orchestrator/runtime/models.py`
  - Shared dataclasses/enums for core services, plugin roles, runtime gates, actions, and evidence.
- Create `src/sil_orchestrator/runtime/manifests.py`
  - Load and validate `config/runtime_plugins/*.yaml` and `config/runtime_profiles/*.yaml`.
- Create `src/sil_orchestrator/runtime/compose.py`
  - Safe Docker Compose command adapter with allowlisted service/action operations.
- Create `src/sil_orchestrator/runtime/service.py`
  - Runtime coordinator: core status/actions, plugin role status/actions, probes, summary.
- Create `src/sil_orchestrator/runtime/evidence.py`
  - Write `runs/runtime_probe_<timestamp>.json`.
- Create `src/sil_orchestrator/runtime/routes.py`
  - FastAPI router under `/api/v1/runtime`.
- Modify `src/sil_orchestrator/main.py`
  - Include runtime router.

### Config and Compose

- Create `config/runtime_plugins/*.yaml`
  - One manifest per plugin candidate.
- Create `config/runtime_profiles/internal-local.yaml`
  - Internal mode profile with built-in/mock roles.
- Create `config/runtime_profiles/integration-local.yaml`
  - Local OrbStack integration profile.
- Create `config/runtime_profiles/integration-a4000.yaml`
  - A4000 integration profile.
- Create `docker-compose.plugins.yml`
  - Container services for local plugin control scaffolding.
- Modify `scripts/local-a4000-env.sh`
  - Include `docker-compose.plugins.yml` in `COMPOSE_FILE`.
- Modify `scripts/local-a4000-acceptance.sh`
  - Add runtime API dry-run/probe checks.

### Frontend

- Modify `web/src/api/silApi.ts`
  - Add runtime API types and endpoints.
- Replace `web/src/screens/shared/ExternalIntegrationPanel.tsx`
  - Remove after new console is wired.
- Create `web/src/screens/runtime/RuntimeModeSwitch.tsx`
- Create `web/src/screens/runtime/CheckCategoryNav.tsx`
- Create `web/src/screens/runtime/CoreServicePanel.tsx`
- Create `web/src/screens/runtime/PluginRolePanel.tsx`
- Create `web/src/screens/runtime/ReadinessGatePanel.tsx`
- Create `web/src/screens/runtime/RuntimeActionLog.tsx`
- Create `web/src/screens/runtime/EvidenceStrip.tsx`
- Modify `web/src/screens/SimulationCheck.tsx`
  - New B layout and GO-path runtime probe.

### Tests

- Create `tests/sil_orchestrator/runtime/test_manifests.py`
- Create `tests/sil_orchestrator/runtime/test_compose.py`
- Create `tests/sil_orchestrator/runtime/test_service.py`
- Create `tests/sil_orchestrator/runtime/test_routes.py`
- Create `tests/scripts/test_runtime_plugin_compose.py`
- Create `web/src/screens/runtime/__tests__/RuntimeModeSwitch.test.tsx`
- Create `web/src/screens/runtime/__tests__/CheckCategoryNav.test.tsx`
- Create `web/src/screens/runtime/__tests__/CoreServicePanel.test.tsx`
- Create `web/src/screens/runtime/__tests__/PluginRolePanel.test.tsx`
- Create `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx`

## Parallel Execution Map

- Agent A: Tasks 1-2, manifest/profile and compose adapter.
- Agent B: Tasks 3-4, runtime service/evidence/routes.
- Agent C: Task 5, plugin compose and local acceptance.
- Agent D: Tasks 6-7, frontend API and components.
- Agent E: Tasks 8-9, Screen 02 integration and final verification.

Agents B, C, and D can start after Task 1 API contracts are merged. Agent E starts after Tasks 3, 4, 6, and 7.

## Task 1: Runtime Manifest and Profile Parser

**Files:**
- Create: `src/sil_orchestrator/runtime/__init__.py`
- Create: `src/sil_orchestrator/runtime/models.py`
- Create: `src/sil_orchestrator/runtime/manifests.py`
- Create: `config/runtime_plugins/hydro-fossen.yaml`
- Create: `config/runtime_plugins/l2-planner-main.yaml`
- Create: `config/runtime_plugins/yougc-fusion.yaml`
- Create: `config/runtime_plugins/tdl-mock-route.yaml`
- Create: `config/runtime_profiles/internal-local.yaml`
- Create: `config/runtime_profiles/integration-local.yaml`
- Create: `config/runtime_profiles/integration-a4000.yaml`
- Test: `tests/sil_orchestrator/runtime/test_manifests.py`

- [ ] **Step 1: Write failing parser tests**

Create `tests/sil_orchestrator/runtime/test_manifests.py`:

```python
from pathlib import Path

import pytest

from sil_orchestrator.runtime.manifests import RuntimeManifestError, load_plugin_manifests, load_runtime_profiles
from sil_orchestrator.runtime.models import PluginRole, RuntimeMode


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def test_loads_plugin_manifests_and_profiles(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(
        plugin_dir / "hydro-fossen.yaml",
        """
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics:
    /ship/odometry: nav_msgs/msg/Odometry
  forbidden_topics:
    - /sil/actuator_cmd
freshness:
  ownship_ms: 500
health:
  required: true
evidence:
  include_logs_tail_lines: 80
""",
    )
    write(
        profile_dir / "integration-local.yaml",
        """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
""",
    )

    plugins = load_plugin_manifests(plugin_dir)
    profiles = load_runtime_profiles(profile_dir, plugins)

    assert plugins["hydro-fossen"].role is PluginRole.HYDRODYNAMICS
    assert plugins["hydro-fossen"].compose.service == "plugin-hydro-fossen"
    assert profiles["integration-local"].mode is RuntimeMode.INTEGRATION
    assert profiles["integration-local"].plugin_roles[PluginRole.HYDRODYNAMICS] == "hydro-fossen"


def test_rejects_duplicate_plugin_id(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    for name in ("a.yaml", "b.yaml"):
        write(
            plugin_dir / name,
            """
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics: {}
  forbidden_topics: []
freshness: {}
health:
  required: false
evidence:
  include_logs_tail_lines: 40
""",
        )

    with pytest.raises(RuntimeManifestError, match="duplicate plugin id"):
        load_plugin_manifests(plugin_dir)


def test_rejects_profile_referencing_unknown_plugin(tmp_path: Path):
    plugin_dir = tmp_path / "runtime_plugins"
    profile_dir = tmp_path / "runtime_profiles"
    write(
        profile_dir / "integration-local.yaml",
        """
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: missing-plugin
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
""",
    )

    with pytest.raises(RuntimeManifestError, match="unknown plugin"):
        load_runtime_profiles(profile_dir, load_plugin_manifests(plugin_dir))
```

- [ ] **Step 2: Run parser tests and confirm failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_manifests.py -q
```

Expected: import failure for `sil_orchestrator.runtime.manifests`.

- [ ] **Step 3: Add runtime models**

Create `src/sil_orchestrator/runtime/models.py`:

```python
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


def freeze_mapping(value: dict[str, int] | dict[str, str]) -> Mapping[str, int] | Mapping[str, str]:
    return MappingProxyType(dict(value))
```

- [ ] **Step 4: Add manifest/profile loader**

Create `src/sil_orchestrator/runtime/manifests.py`:

```python
from __future__ import annotations

from collections.abc import Mapping
from pathlib import Path
from types import MappingProxyType
from typing import Any

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
)


class RuntimeManifestError(ValueError):
    """Raised when runtime plugin or profile config is invalid."""


def load_plugin_manifests(directory: Path) -> dict[str, PluginManifest]:
    manifests: dict[str, PluginManifest] = {}
    if not directory.exists():
        return manifests
    for path in sorted(directory.glob("*.yaml")):
        manifest = load_plugin_manifest(path)
        if manifest.id in manifests:
            raise RuntimeManifestError(f"{directory}: duplicate plugin id {manifest.id!r}")
        manifests[manifest.id] = manifest
    return manifests


def load_plugin_manifest(path: Path) -> PluginManifest:
    raw = _load_mapping(path)
    plugin_id = _str(path, raw, "id")
    try:
        role = PluginRole(_str(path, raw, "role"))
    except ValueError as exc:
        raise RuntimeManifestError(f"{path}: invalid role {raw.get('role')!r}") from exc
    runtime = _str(path, raw, "runtime")
    if runtime != "compose":
        raise RuntimeManifestError(f"{path}: runtime must be compose")
    compose = _mapping(path, raw, "compose")
    image = _mapping(path, raw, "image")
    ros = _mapping(path, raw, "ros")
    health = _mapping(path, raw, "health")
    evidence = _mapping(path, raw, "evidence")
    required_topics = _string_mapping(path, _mapping(path, ros, "required_topics"), "ros.required_topics")
    forbidden_topics = ros.get("forbidden_topics", [])
    if not isinstance(forbidden_topics, list) or not all(isinstance(item, str) for item in forbidden_topics):
        raise RuntimeManifestError(f"{path}: ros.forbidden_topics must be a list of strings")
    return PluginManifest(
        id=plugin_id,
        role=role,
        label=_str(path, raw, "label"),
        runtime=runtime,
        compose=ComposeServiceRef(service=_str(path, compose, "service")),
        image=ImageMetadata(
            expected=_str(path, image, "expected"),
            revision_label=_str(path, image, "revision_label"),
        ),
        ros=RosTopicContract(
            domain_id=_int(path, ros, "domain_id"),
            required_topics=MappingProxyType(required_topics),
            forbidden_topics=tuple(forbidden_topics),
        ),
        freshness=MappingProxyType(_int_mapping(path, _mapping(path, raw, "freshness"), "freshness")),
        health_required=_bool(path, health, "required"),
        include_logs_tail_lines=_int(path, evidence, "include_logs_tail_lines"),
    )


def load_runtime_profiles(directory: Path, plugins: Mapping[str, PluginManifest]) -> dict[str, RuntimeProfile]:
    profiles: dict[str, RuntimeProfile] = {}
    if not directory.exists():
        return profiles
    for path in sorted(directory.glob("*.yaml")):
        profile = load_runtime_profile(path, plugins)
        if profile.name in profiles:
            raise RuntimeManifestError(f"{directory}: duplicate profile name {profile.name!r}")
        profiles[profile.name] = profile
    return profiles


def load_runtime_profile(path: Path, plugins: Mapping[str, PluginManifest]) -> RuntimeProfile:
    raw = _load_mapping(path)
    role_raw = _mapping(path, raw, "plugin_roles")
    plugin_roles: dict[PluginRole, str] = {}
    for key, plugin_id in role_raw.items():
        if not isinstance(plugin_id, str):
            raise RuntimeManifestError(f"{path}: plugin_roles.{key} must be a string")
        try:
            role = PluginRole(key)
        except ValueError as exc:
            raise RuntimeManifestError(f"{path}: invalid plugin role {key!r}") from exc
        if plugin_id not in plugins:
            raise RuntimeManifestError(f"{path}: unknown plugin {plugin_id!r}")
        if plugins[plugin_id].role is not role:
            raise RuntimeManifestError(f"{path}: plugin {plugin_id!r} is not role {role.value}")
        plugin_roles[role] = plugin_id
    safety = _mapping(path, raw, "safety")
    return RuntimeProfile(
        name=_str(path, raw, "name"),
        mode=RuntimeMode(_str(path, raw, "mode")),
        target=RuntimeTarget(_str(path, raw, "target")),
        tdl_domain_id=_int(path, raw, "tdl_domain_id"),
        plugin_roles=MappingProxyType(plugin_roles),
        safety=RuntimeSafety(
            single_instance_per_role=_bool(path, safety, "single_instance_per_role"),
            forbid_low_level_control=_bool(path, safety, "forbid_low_level_control"),
            require_version_metadata=_bool(path, safety, "require_version_metadata"),
        ),
    )


def _load_mapping(path: Path) -> dict[str, Any]:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise RuntimeManifestError(f"{path}: expected mapping")
    return raw


def _mapping(path: Path, raw: Mapping[str, Any], key: str) -> dict[str, Any]:
    value = raw.get(key)
    if not isinstance(value, dict):
        raise RuntimeManifestError(f"{path}: {key} must be a mapping")
    return value


def _str(path: Path, raw: Mapping[str, Any], key: str) -> str:
    value = raw.get(key)
    if not isinstance(value, str) or not value:
        raise RuntimeManifestError(f"{path}: {key} must be a non-empty string")
    return value


def _int(path: Path, raw: Mapping[str, Any], key: str) -> int:
    value = raw.get(key)
    if type(value) is not int:
        raise RuntimeManifestError(f"{path}: {key} must be an integer")
    return value


def _bool(path: Path, raw: Mapping[str, Any], key: str) -> bool:
    value = raw.get(key)
    if type(value) is not bool:
        raise RuntimeManifestError(f"{path}: {key} must be a boolean")
    return value


def _string_mapping(path: Path, raw: Mapping[str, Any], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, value in raw.items():
        if not isinstance(key, str) or not isinstance(value, str):
            raise RuntimeManifestError(f"{path}: {label} must map string to string")
        result[key] = value
    return result


def _int_mapping(path: Path, raw: Mapping[str, Any], label: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for key, value in raw.items():
        if not isinstance(key, str) or type(value) is not int:
            raise RuntimeManifestError(f"{path}: {label} must map string to integer")
        result[key] = value
    return result
```

- [ ] **Step 5: Add initial runtime plugin/profile config**

Create these files with the exact service names shown below. If a later task changes a service name, update the matching manifest and compose service in the same commit.

`config/runtime_plugins/hydro-fossen.yaml`

```yaml
id: hydro-fossen
role: hydrodynamics
label: Hydro Fossen
runtime: compose
compose:
  service: plugin-hydro-fossen
image:
  expected: mass-hydro-fossen:0.9.3
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics:
    /ship/odometry: nav_msgs/msg/Odometry
    /ship/waypoints: nav_msgs/msg/Path
  forbidden_topics:
    - /sil/actuator_cmd
    - /l4/control_cmd
freshness:
  ownship_ms: 500
health:
  required: true
evidence:
  include_logs_tail_lines: 80
```

`config/runtime_plugins/l2-planner-main.yaml`

```yaml
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
  required_topics:
    /route_planning/route_plan: ship_interfaces/msg/RoutePlan
  forbidden_topics:
    - /sil/actuator_cmd
    - /l4/control_cmd
freshness:
  route_ms: 2000
health:
  required: true
evidence:
  include_logs_tail_lines: 80
```

`config/runtime_plugins/yougc-fusion.yaml`

```yaml
id: yougc-fusion
role: fusion
label: YouGC Fusion
runtime: compose
compose:
  service: plugin-fusion-yougc
image:
  expected: yougc-fusion:20260612
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 11
  required_topics:
    /fusion/tracked_targets: nmea_interfaces/msg/TrackedTargetArray
    /gps/fix: nmea_interfaces/msg/Gps
    /heading: nmea_interfaces/msg/Heading
  forbidden_topics:
    - /sil/actuator_cmd
    - /l4/control_cmd
freshness:
  targets_ms: 2000
  ownship_ms: 500
health:
  required: true
evidence:
  include_logs_tail_lines: 80
```

`config/runtime_plugins/tdl-mock-route.yaml`

```yaml
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
  required_topics:
    /l2/planned_route: nav_msgs/msg/Path
  forbidden_topics:
    - /sil/actuator_cmd
    - /l4/control_cmd
freshness:
  route_ms: 2000
health:
  required: false
evidence:
  include_logs_tail_lines: 40
```

`config/runtime_profiles/integration-local.yaml`

```yaml
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
  route_l2: l2-planner-main
  fusion: yougc-fusion
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
```

`config/runtime_profiles/internal-local.yaml`

```yaml
name: internal-local
mode: internal
target: local
tdl_domain_id: 42
plugin_roles:
  route_l2: tdl-mock-route
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: false
```

`config/runtime_profiles/integration-a4000.yaml`

```yaml
name: integration-a4000
mode: integration
target: a4000
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
  route_l2: l2-planner-main
  fusion: yougc-fusion
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
```

- [ ] **Step 6: Run parser tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_manifests.py -q
```

Expected: all tests pass.

- [ ] **Step 7: Commit Task 1**

```bash
git add src/sil_orchestrator/runtime config/runtime_plugins config/runtime_profiles tests/sil_orchestrator/runtime/test_manifests.py
git commit -m "feat: add runtime plugin manifests"
```

## Task 2: Compose Runtime Adapter

**Files:**
- Create: `src/sil_orchestrator/runtime/compose.py`
- Test: `tests/sil_orchestrator/runtime/test_compose.py`

- [ ] **Step 1: Write failing compose adapter tests**

Create `tests/sil_orchestrator/runtime/test_compose.py`:

```python
import subprocess

from sil_orchestrator.runtime.compose import ComposeRuntime


class FakeRunner:
    def __init__(self):
        self.calls = []

    def __call__(self, command, timeout_s):
        self.calls.append((command, timeout_s))
        if command[-1] == "ps":
            return subprocess.CompletedProcess(command, 0, stdout="[]", stderr="")
        return subprocess.CompletedProcess(command, 0, stdout="ok", stderr="")


def test_compose_uses_configured_files_and_project_name():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml", "docker-compose.plugins.yml"),
        project_name="mass-l3-sil",
        runner=runner,
    )

    runtime.restart_service("sil-orchestrator")

    command, timeout_s = runner.calls[0]
    assert command[:7] == [
        "docker",
        "compose",
        "-p",
        "mass-l3-sil",
        "-f",
        "docker-compose.yml",
        "-f",
    ]
    assert command[-3:] == ["restart", "sil-orchestrator"]
    assert timeout_s == 30.0


def test_stop_core_service_is_not_available_in_adapter():
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml",),
        project_name="mass-l3-sil",
        runner=FakeRunner(),
    )

    assert not hasattr(runtime, "stop_core_service")


def test_plugin_switch_command_sequence():
    runner = FakeRunner()
    runtime = ComposeRuntime(
        compose_files=("docker-compose.yml", "docker-compose.plugins.yml"),
        project_name="mass-l3-sil",
        runner=runner,
    )

    runtime.switch_plugin(old_service="plugin-route-l2-main", new_service="plugin-route-tdl-mock")

    commands = [call[0] for call in runner.calls]
    assert commands[0][-2:] == ["stop", "plugin-route-l2-main"]
    assert commands[1][-4:] == ["up", "-d", "plugin-route-tdl-mock"]
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_compose.py -q
```

Expected: import failure for `sil_orchestrator.runtime.compose`.

- [ ] **Step 3: Implement compose adapter**

Create `src/sil_orchestrator/runtime/compose.py`:

```python
from __future__ import annotations

import subprocess
from collections.abc import Callable


Runner = Callable[[list[str], float], subprocess.CompletedProcess[str]]


def run_command(command: list[str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, timeout=timeout_s)


class ComposeRuntimeError(RuntimeError):
    """Raised when a compose lifecycle command fails."""


class ComposeRuntime:
    def __init__(
        self,
        compose_files: tuple[str, ...],
        project_name: str,
        runner: Runner = run_command,
    ) -> None:
        self.compose_files = compose_files
        self.project_name = project_name
        self.runner = runner

    def _base(self) -> list[str]:
        command = ["docker", "compose", "-p", self.project_name]
        for compose_file in self.compose_files:
            command.extend(["-f", compose_file])
        return command

    def _run(self, args: list[str], timeout_s: float = 30.0) -> subprocess.CompletedProcess[str]:
        command = [*self._base(), *args]
        result = self.runner(command, timeout_s)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout or "").strip()
            raise ComposeRuntimeError(f"{' '.join(command)} failed: {detail}")
        return result

    def ps_json(self) -> str:
        return self._run(["ps", "--format", "json"], timeout_s=10.0).stdout

    def start_service(self, service: str) -> None:
        self._run(["up", "-d", service])

    def restart_service(self, service: str) -> None:
        self._run(["restart", service])

    def stop_plugin_service(self, service: str) -> None:
        self._run(["stop", service])

    def start_core_stack(self) -> None:
        self._run(["up", "-d", "sil-orchestrator", "sil-nodes", "foxglove-bridge", "martin-tile-server"], timeout_s=60.0)

    def restart_core_stack(self) -> None:
        self._run(["restart", "sil-orchestrator", "sil-nodes", "foxglove-bridge", "martin-tile-server"], timeout_s=60.0)

    def stop_core_stack(self) -> None:
        self._run(["stop", "sil-orchestrator", "sil-nodes", "foxglove-bridge", "martin-tile-server"], timeout_s=60.0)

    def switch_plugin(self, old_service: str | None, new_service: str) -> None:
        if old_service:
            self.stop_plugin_service(old_service)
        self.start_service(new_service)
```

- [ ] **Step 4: Run compose tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_compose.py -q
```

Expected: all tests pass.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/sil_orchestrator/runtime/compose.py tests/sil_orchestrator/runtime/test_compose.py
git commit -m "feat: add compose runtime adapter"
```

## Task 3: Runtime Service, Gates, and Evidence

**Files:**
- Create: `src/sil_orchestrator/runtime/evidence.py`
- Create: `src/sil_orchestrator/runtime/service.py`
- Test: `tests/sil_orchestrator/runtime/test_service.py`

- [ ] **Step 1: Write failing service tests**

Create `tests/sil_orchestrator/runtime/test_service.py`:

```python
import json
from pathlib import Path

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
                {"Service": "sil-orchestrator", "Name": "mass-l3-sil-sil-orchestrator-1", "State": "running", "Health": "healthy", "Image": "mass-l3-sil-sil-orchestrator"},
                {"Service": "plugin-route-l2-main", "Name": "mass-l3-plugin-route", "State": "running", "Health": "starting", "Image": "mass-l2-planner:main"},
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
```

Add a fixture in the same file:

```python
import pytest


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
```

- [ ] **Step 2: Run test to verify failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_service.py -q
```

Expected: import failure for `RuntimeConsoleService`.

- [ ] **Step 3: Implement evidence writer**

Create `src/sil_orchestrator/runtime/evidence.py`:

```python
from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo


def write_runtime_evidence(runs_dir: Path, payload: dict[str, object]) -> Path:
    runs_dir.mkdir(parents=True, exist_ok=True)
    now = datetime.now(ZoneInfo("Asia/Shanghai"))
    payload = {"timestamp": now.isoformat(), **payload}
    path = runs_dir / f"runtime_probe_{now.strftime('%Y%m%d_%H%M%S')}.json"
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path
```

- [ ] **Step 4: Implement runtime console service**

Create `src/sil_orchestrator/runtime/service.py`:

```python
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from sil_orchestrator.runtime.compose import ComposeRuntime
from sil_orchestrator.runtime.evidence import write_runtime_evidence
from sil_orchestrator.runtime.models import PluginManifest, PluginRole, RuntimeProfile


CORE_SERVICES = ("sil-orchestrator", "sil-nodes", "foxglove-bridge", "martin-tile-server")


class RuntimeConsoleService:
    def __init__(
        self,
        plugins: dict[str, PluginManifest],
        profiles: dict[str, RuntimeProfile],
        compose: ComposeRuntime,
        runs_dir: Path,
        active_profile_name: str = "integration-local",
    ) -> None:
        self.plugins = plugins
        self.profiles = profiles
        self.compose = compose
        self.runs_dir = runs_dir
        self.active_profile_name = active_profile_name

    @property
    def active_profile(self) -> RuntimeProfile:
        return self.profiles[self.active_profile_name]

    def core_services(self) -> list[dict[str, object]]:
        by_service = self._compose_services_by_name()
        rows: list[dict[str, object]] = []
        for service in CORE_SERVICES:
            item = by_service.get(service, {})
            rows.append(
                {
                    "id": service,
                    "class": "core_service",
                    "container_name": item.get("Name", ""),
                    "status": _state(item),
                    "health": _health(item),
                    "image": item.get("Image", ""),
                    "allowed_actions": ["restart"],
                }
            )
        return rows

    def plugin_roles(self) -> list[dict[str, object]]:
        by_service = self._compose_services_by_name()
        roles: list[dict[str, object]] = []
        for role in PluginRole:
            role_plugins = [plugin for plugin in self.plugins.values() if plugin.role is role]
            if not role_plugins:
                continue
            active_id = self.active_profile.plugin_roles.get(role)
            roles.append(
                {
                    "role": role.value,
                    "active_plugin": active_id,
                    "single_instance": True,
                    "plugins": [self._plugin_to_dict(plugin, by_service) for plugin in role_plugins],
                }
            )
        return roles

    def summary(self) -> dict[str, object]:
        report = self.probe(write_evidence=False)
        return {
            "mode": self.active_profile.mode.value,
            "target": self.active_profile.target.value,
            "active_profile": self.active_profile.name,
            "verdict": report["verdict"],
            "core_services": self.core_services(),
            "plugin_roles": self.plugin_roles(),
            "gates": report["gates"],
        }

    def restart_core_service(self, service_id: str) -> dict[str, object]:
        if service_id not in CORE_SERVICES:
            return {"accepted": False, "error": f"unknown core service {service_id}"}
        self.compose.restart_service(service_id)
        return {"accepted": True, "service_id": service_id, "action": "restart"}

    def start_core_stack(self) -> dict[str, object]:
        self.compose.start_core_stack()
        return {"accepted": True, "action": "start_core_stack"}

    def restart_core_stack(self) -> dict[str, object]:
        self.compose.restart_core_stack()
        return {"accepted": True, "action": "restart_core_stack"}

    def stop_core_stack(self, confirm: str) -> dict[str, object]:
        if confirm != "STOP_CORE_STACK":
            return {"accepted": False, "error": "confirm must equal STOP_CORE_STACK"}
        self.compose.stop_core_stack()
        return {"accepted": True, "action": "stop_core_stack"}

    def switch_plugin(self, role_value: str, plugin_id: str) -> dict[str, object]:
        role = PluginRole(role_value)
        new_plugin = self.plugins[plugin_id]
        if new_plugin.role is not role:
            return {"accepted": False, "error": f"{plugin_id} is not role {role.value}"}
        old_plugin_id = self.active_profile.plugin_roles.get(role)
        old_service = self.plugins[old_plugin_id].compose.service if old_plugin_id else None
        self.compose.switch_plugin(old_service, new_plugin.compose.service)
        return {"accepted": True, "role": role.value, "plugin_id": plugin_id}

    def probe(self, write_evidence: bool = True) -> dict[str, object]:
        core = self.core_services()
        plugins = self.plugin_roles()
        gates = [
            {"gate_id": 1, "label": "Core services running", "passed": all(item["status"] == "running" for item in core), "detail": f"{sum(item['status'] == 'running' for item in core)}/{len(core)}"},
            {"gate_id": 2, "label": "Single active plugin per role", "passed": True, "detail": "profile maps one plugin per role"},
        ]
        verdict = "GO" if all(gate["passed"] for gate in gates) else "NO-GO"
        payload = {
            "target": self.active_profile.target.value,
            "mode": self.active_profile.mode.value,
            "profile": self.active_profile.name,
            "core_services": core,
            "plugin_roles": plugins,
            "gates": gates,
            "verdict": verdict,
        }
        if write_evidence:
            path = write_runtime_evidence(self.runs_dir, payload)
            return {**payload, "evidence_path": str(path)}
        return payload

    def _compose_services_by_name(self) -> dict[str, dict[str, Any]]:
        raw = self.compose.ps_json().strip()
        if not raw:
            return {}
        parsed = json.loads(raw)
        if isinstance(parsed, dict):
            parsed = [parsed]
        return {str(item.get("Service", "")): item for item in parsed if isinstance(item, dict)}

    def _plugin_to_dict(self, plugin: PluginManifest, by_service: dict[str, dict[str, Any]]) -> dict[str, object]:
        item = by_service.get(plugin.compose.service, {})
        return {
            "id": plugin.id,
            "label": plugin.label,
            "service": plugin.compose.service,
            "container_name": item.get("Name", ""),
            "status": _state(item),
            "health": _health(item),
            "image": item.get("Image", plugin.image.expected),
            "revision": item.get("Labels", {}).get(plugin.image.revision_label, "unknown") if isinstance(item.get("Labels"), dict) else "unknown",
            "required_topics": [{"name": name, "type": type_name, "status": "unchecked"} for name, type_name in plugin.ros.required_topics.items()],
        }


def _state(item: dict[str, Any]) -> str:
    return str(item.get("State", "unknown")).lower()


def _health(item: dict[str, Any]) -> str:
    return str(item.get("Health", "unknown")).lower()
```

- [ ] **Step 5: Run service tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_service.py -q
```

Expected: all tests pass.

- [ ] **Step 6: Commit Task 3**

```bash
git add src/sil_orchestrator/runtime/evidence.py src/sil_orchestrator/runtime/service.py tests/sil_orchestrator/runtime/test_service.py
git commit -m "feat: add runtime console service"
```

## Task 4: Runtime FastAPI Routes

**Files:**
- Create: `src/sil_orchestrator/runtime/routes.py`
- Modify: `src/sil_orchestrator/main.py`
- Test: `tests/sil_orchestrator/runtime/test_routes.py`

- [ ] **Step 1: Write route tests**

Create `tests/sil_orchestrator/runtime/test_routes.py`:

```python
from fastapi.testclient import TestClient

from sil_orchestrator.main import app


def test_runtime_summary_returns_active_profile():
    client = TestClient(app)

    response = client.get("/api/v1/runtime/summary")

    assert response.status_code == 200
    body = response.json()
    assert "active_profile" in body
    assert "core_services" in body
    assert "plugin_roles" in body


def test_core_single_service_restart_route_rejects_unknown_service():
    client = TestClient(app)

    response = client.post("/api/v1/runtime/core/not-a-service/restart")

    assert response.status_code == 404


def test_stop_core_stack_requires_confirmation():
    client = TestClient(app)

    response = client.post("/api/v1/runtime/core/stop", json={"confirm": "wrong"})

    assert response.status_code == 400
    assert "STOP_CORE_STACK" in response.text
```

- [ ] **Step 2: Run route tests and confirm failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_routes.py -q
```

Expected: `GET /api/v1/runtime/summary` returns 404.

- [ ] **Step 3: Add runtime routes**

Create `src/sil_orchestrator/runtime/routes.py`:

```python
from __future__ import annotations

import os
from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException

from sil_orchestrator.runtime.compose import ComposeRuntime
from sil_orchestrator.runtime.manifests import load_plugin_manifests, load_runtime_profiles
from sil_orchestrator.runtime.service import RuntimeConsoleService


router = APIRouter(prefix="/api/v1/runtime")

_ROOT = Path(__file__).resolve().parents[3]
_PLUGIN_DIR = _ROOT / "config" / "runtime_plugins"
_PROFILE_DIR = _ROOT / "config" / "runtime_profiles"
_RUNS_DIR = Path(os.environ.get("SIL_RUN_DIR", str(_ROOT / "runs")))


def _runtime_service() -> RuntimeConsoleService:
    plugins = load_plugin_manifests(_PLUGIN_DIR)
    profiles = load_runtime_profiles(_PROFILE_DIR, plugins)
    compose_files = tuple(os.environ.get("COMPOSE_FILE", "docker-compose.yml").split(":"))
    compose = ComposeRuntime(compose_files=compose_files, project_name=os.environ.get("COMPOSE_PROJECT_NAME", "mass-l3-sil"))
    active_profile = os.environ.get("TDL_RUNTIME_PROFILE", "integration-local")
    if active_profile not in profiles:
        active_profile = next(iter(profiles), "")
    if not active_profile:
        raise HTTPException(status_code=500, detail="No runtime profiles loaded")
    return RuntimeConsoleService(plugins, profiles, compose, _RUNS_DIR, active_profile_name=active_profile)


@router.get("/summary")
async def runtime_summary() -> dict[str, object]:
    return _runtime_service().summary()


@router.get("/core-services")
async def runtime_core_services() -> dict[str, object]:
    return {"services": _runtime_service().core_services()}


@router.get("/plugins")
async def runtime_plugins() -> dict[str, object]:
    return {"roles": _runtime_service().plugin_roles()}


@router.post("/core/{service_id}/restart")
async def restart_core_service(service_id: str) -> dict[str, object]:
    result = _runtime_service().restart_core_service(service_id)
    if not result["accepted"]:
        raise HTTPException(status_code=404, detail=result["error"])
    return result


@router.post("/core/start")
async def start_core_stack() -> dict[str, object]:
    return _runtime_service().start_core_stack()


@router.post("/core/restart")
async def restart_core_stack() -> dict[str, object]:
    return _runtime_service().restart_core_stack()


@router.post("/core/stop")
async def stop_core_stack(request: dict[str, Any]) -> dict[str, object]:
    result = _runtime_service().stop_core_stack(confirm=str(request.get("confirm", "")))
    if not result["accepted"]:
        raise HTTPException(status_code=400, detail=result["error"])
    return result


@router.post("/plugins/{role}/switch")
async def switch_plugin(role: str, request: dict[str, str]) -> dict[str, object]:
    try:
        return _runtime_service().switch_plugin(role, request.get("plugin_id", ""))
    except (KeyError, ValueError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@router.post("/probe")
async def runtime_probe() -> dict[str, object]:
    return _runtime_service().probe()
```

- [ ] **Step 4: Register route in main app**

Modify `src/sil_orchestrator/main.py`:

```python
from sil_orchestrator.runtime.routes import router as runtime_router
```

Add near existing router includes:

```python
app.include_router(runtime_router)
```

- [ ] **Step 5: Run route tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/runtime/test_routes.py -q
```

Expected: all tests pass.

- [ ] **Step 6: Run current integration route tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_integration_profiles.py tests/sil_orchestrator/test_integration_routes.py tests/sil_orchestrator/runtime -q
```

Expected: all tests pass.

- [ ] **Step 7: Commit Task 4**

```bash
git add src/sil_orchestrator/runtime/routes.py src/sil_orchestrator/main.py tests/sil_orchestrator/runtime/test_routes.py
git commit -m "feat: expose runtime console API"
```

## Task 5: Plugin Compose and Local Acceptance

**Files:**
- Create: `docker-compose.plugins.yml`
- Modify: `scripts/local-a4000-env.sh`
- Modify: `scripts/local-a4000-acceptance.sh`
- Test: `tests/scripts/test_runtime_plugin_compose.py`

- [ ] **Step 1: Write script/config tests**

Create `tests/scripts/test_runtime_plugin_compose.py`:

```python
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_plugin_compose_file_is_valid_with_local_env():
    result = subprocess.run(
        ["bash", "-lc", "source scripts/local-a4000-env.sh && docker compose config -q"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )

    assert result.returncode == 0, result.stderr


def test_local_env_includes_plugin_compose():
    result = subprocess.run(
        ["bash", "-lc", "source scripts/local-a4000-env.sh && printf '%s' \"$COMPOSE_FILE\""],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=10,
    )

    assert "docker-compose.plugins.yml" in result.stdout


def test_acceptance_dry_run_reports_runtime_probe():
    result = subprocess.run(
        ["bash", "scripts/local-a4000-acceptance.sh", "--dry-run"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=10,
    )

    assert result.returncode == 0
    assert "runtime=/api/v1/runtime/summary" in result.stdout
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/scripts/test_runtime_plugin_compose.py -q
```

Expected: missing `docker-compose.plugins.yml` or missing dry-run line.

- [ ] **Step 3: Add plugin compose services**

Create `docker-compose.plugins.yml`:

```yaml
services:
  plugin-hydro-fossen:
    image: alpine:3.20
    command: ["sh", "-c", "while true; do sleep 3600; done"]
    labels:
      org.opencontainers.image.revision: local-hydro-fossen
      mass_l3.plugin.role: hydrodynamics
      mass_l3.plugin.id: hydro-fossen
    profiles: ["plugins"]
    network_mode: host

  plugin-route-l2-main:
    image: alpine:3.20
    command: ["sh", "-c", "while true; do sleep 3600; done"]
    labels:
      org.opencontainers.image.revision: local-l2-main
      mass_l3.plugin.role: route_l2
      mass_l3.plugin.id: l2-planner-main
    profiles: ["plugins"]
    network_mode: host

  plugin-route-tdl-mock:
    image: alpine:3.20
    command: ["sh", "-c", "while true; do sleep 3600; done"]
    labels:
      org.opencontainers.image.revision: local-tdl-mock-route
      mass_l3.plugin.role: route_l2
      mass_l3.plugin.id: tdl-mock-route
    profiles: ["plugins"]
    network_mode: host

  plugin-fusion-yougc:
    image: alpine:3.20
    command: ["sh", "-c", "while true; do sleep 3600; done"]
    labels:
      org.opencontainers.image.revision: local-yougc-fusion
      mass_l3.plugin.role: fusion
      mass_l3.plugin.id: yougc-fusion
    profiles: ["plugins"]
    network_mode: host
```

- [ ] **Step 4: Update local env compose chain**

Modify `scripts/local-a4000-env.sh`:

```bash
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml
export COMPOSE_PROFILES="${COMPOSE_PROFILES:-plugins}"
export TDL_RUNTIME_PROFILE="${TDL_RUNTIME_PROFILE:-integration-local}"
```

Keep existing exports for `ROS_DOMAIN_ID`, ports, `ORCH_URL`, and CPU caps.

- [ ] **Step 5: Extend acceptance dry-run and live probe**

Modify `scripts/local-a4000-acceptance.sh`.

In dry-run block add:

```bash
echo "runtime=/api/v1/runtime/summary"
echo "runtime_probe=/api/v1/runtime/probe"
```

After current `/api/v1/integration/profiles` check add:

```bash
curl -sk --fail "${ORCH_URL}/api/v1/runtime/summary" | grep -q '"active_profile"'
curl -sk --fail -X POST "${ORCH_URL}/api/v1/runtime/probe" \
  | tee "runs/local_runtime_probe_$(date +%Y%m%d_%H%M%S).json"
```

- [ ] **Step 6: Run compose/script tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/scripts/test_runtime_plugin_compose.py -q
```

Expected: all tests pass.

- [ ] **Step 7: Run local compose config**

Run:

```bash
source scripts/local-a4000-env.sh
docker compose config -q
```

Expected: exit code 0.

- [ ] **Step 8: Commit Task 5**

```bash
git add docker-compose.plugins.yml scripts/local-a4000-env.sh scripts/local-a4000-acceptance.sh tests/scripts/test_runtime_plugin_compose.py
git commit -m "feat: add local plugin compose gate"
```

## Task 6: Frontend Runtime API Types

**Files:**
- Modify: `web/src/api/silApi.ts`
- Test: `web/src/screens/runtime/__tests__/runtimeApiTypes.test.ts`

- [ ] **Step 1: Add a type smoke test**

Create `web/src/screens/runtime/__tests__/runtimeApiTypes.test.ts`:

```typescript
import { describe, expect, it } from 'vitest';
import type { RuntimeSummary, RuntimePluginRole } from '../../../api/silApi';

describe('runtime API types', () => {
  it('represents runtime summary and plugin roles', () => {
    const role: RuntimePluginRole = {
      role: 'route_l2',
      active_plugin: 'l2-planner-main',
      single_instance: true,
      plugins: [{
        id: 'l2-planner-main',
        label: 'L2 Planner Main',
        service: 'plugin-route-l2-main',
        container_name: 'mass-l3-plugin-route',
        status: 'running',
        health: 'degraded',
        image: 'mass-l2-planner:main',
        revision: 'unknown',
        required_topics: [{ name: '/route_planning/route_plan', type: 'ship_interfaces/msg/RoutePlan', status: 'missing' }],
      }],
    };
    const summary: RuntimeSummary = {
      mode: 'integration',
      target: 'local',
      active_profile: 'integration-local',
      verdict: 'NO-GO',
      core_services: [],
      plugin_roles: [role],
      gates: [],
    };

    expect(summary.plugin_roles[0].single_instance).toBe(true);
  });
});
```

- [ ] **Step 2: Run type smoke test and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/runtime/__tests__/runtimeApiTypes.test.ts
```

Expected: TypeScript compile failure for missing exported runtime types.

- [ ] **Step 3: Add runtime types and endpoints**

Modify `web/src/api/silApi.ts` near integration types:

```typescript
export type RuntimeMode = 'internal' | 'integration';
export type RuntimeTarget = 'local' | 'a4000';
export type RuntimeVerdict = 'GO' | 'NO-GO' | 'CHECKING' | 'IDLE';
export type RuntimeServiceStatus = 'running' | 'stopped' | 'unknown';
export type RuntimeHealthStatus = 'healthy' | 'starting' | 'degraded' | 'unhealthy' | 'unknown';

export interface RuntimeGate {
  gate_id: number;
  label: string;
  passed: boolean;
  detail: string;
}

export interface RuntimeCoreService {
  id: string;
  class: 'core_service';
  container_name: string;
  status: RuntimeServiceStatus;
  health: RuntimeHealthStatus;
  image: string;
  allowed_actions: string[];
}

export interface RuntimeTopicStatus {
  name: string;
  type: string;
  status: 'ok' | 'missing' | 'wrong_type' | 'stale' | 'unchecked';
}

export interface RuntimePlugin {
  id: string;
  label: string;
  service: string;
  container_name: string;
  status: RuntimeServiceStatus;
  health: RuntimeHealthStatus;
  image: string;
  revision: string;
  required_topics: RuntimeTopicStatus[];
}

export interface RuntimePluginRole {
  role: 'hydrodynamics' | 'route_l2' | 'fusion';
  active_plugin: string | null;
  single_instance: boolean;
  plugins: RuntimePlugin[];
}

export interface RuntimeSummary {
  mode: RuntimeMode;
  target: RuntimeTarget;
  active_profile: string;
  verdict: RuntimeVerdict;
  core_services: RuntimeCoreService[];
  plugin_roles: RuntimePluginRole[];
  gates: RuntimeGate[];
  evidence_path?: string;
}

export interface RuntimeActionResult {
  accepted: boolean;
  action?: string;
  service_id?: string;
  role?: string;
  plugin_id?: string;
  error?: string;
}
```

Add endpoints:

```typescript
getRuntimeSummary: builder.query<RuntimeSummary, void>({
  query: () => '/runtime/summary',
  providesTags: ['Integration'],
}),
restartRuntimeCoreService: builder.mutation<RuntimeActionResult, string>({
  query: (serviceId) => ({ url: `/runtime/core/${encodeURIComponent(serviceId)}/restart`, method: 'POST' }),
  invalidatesTags: ['Integration'],
}),
stopRuntimeCoreStack: builder.mutation<RuntimeActionResult, { confirm: string }>({
  query: (body) => ({ url: '/runtime/core/stop', method: 'POST', body }),
  invalidatesTags: ['Integration'],
}),
switchRuntimePlugin: builder.mutation<RuntimeActionResult, { role: string; plugin_id: string }>({
  query: ({ role, plugin_id }) => ({ url: `/runtime/plugins/${encodeURIComponent(role)}/switch`, method: 'POST', body: { plugin_id } }),
  invalidatesTags: ['Integration'],
}),
probeRuntime: builder.mutation<RuntimeSummary, void>({
  query: () => ({ url: '/runtime/probe', method: 'POST' }),
  invalidatesTags: ['Integration'],
}),
```

- [ ] **Step 4: Run type smoke test**

Run:

```bash
cd web && npm test -- --run src/screens/runtime/__tests__/runtimeApiTypes.test.ts
```

Expected: test passes.

- [ ] **Step 5: Commit Task 6**

```bash
git add web/src/api/silApi.ts web/src/screens/runtime/__tests__/runtimeApiTypes.test.ts
git commit -m "feat: add runtime API client types"
```

## Task 7: Runtime Console Frontend Components

**Files:**
- Create: `web/src/screens/runtime/RuntimeModeSwitch.tsx`
- Create: `web/src/screens/runtime/CheckCategoryNav.tsx`
- Create: `web/src/screens/runtime/CoreServicePanel.tsx`
- Create: `web/src/screens/runtime/PluginRolePanel.tsx`
- Create: `web/src/screens/runtime/RuntimeActionLog.tsx`
- Create: `web/src/screens/runtime/EvidenceStrip.tsx`
- Test: `web/src/screens/runtime/__tests__/RuntimeModeSwitch.test.tsx`
- Test: `web/src/screens/runtime/__tests__/CheckCategoryNav.test.tsx`
- Test: `web/src/screens/runtime/__tests__/CoreServicePanel.test.tsx`
- Test: `web/src/screens/runtime/__tests__/PluginRolePanel.test.tsx`

- [ ] **Step 1: Write component tests**

Create `web/src/screens/runtime/__tests__/CoreServicePanel.test.tsx`:

```tsx
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { CoreServicePanel } from '../CoreServicePanel';

describe('CoreServicePanel', () => {
  it('shows restart per core service and no single-service stop', () => {
    const onRestart = vi.fn();
    render(
      <CoreServicePanel
        services={[{
          id: 'sil-orchestrator',
          class: 'core_service',
          container_name: 'mass-l3-sil-sil-orchestrator-1',
          status: 'running',
          health: 'healthy',
          image: 'mass-l3-sil-sil-orchestrator',
          allowed_actions: ['restart'],
        }]}
        onRestart={onRestart}
        onStopCoreStack={vi.fn()}
      />,
    );

    fireEvent.click(screen.getByRole('button', { name: /Restart sil-orchestrator/i }));

    expect(onRestart).toHaveBeenCalledWith('sil-orchestrator');
    expect(screen.queryByRole('button', { name: /Stop sil-orchestrator/i })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Stop Core Stack/i })).toBeInTheDocument();
  });
});
```

Create `web/src/screens/runtime/__tests__/PluginRolePanel.test.tsx`:

```tsx
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { PluginRolePanel } from '../PluginRolePanel';

describe('PluginRolePanel', () => {
  it('switches selected plugin through role-level callback', () => {
    const onSwitch = vi.fn();
    render(
      <PluginRolePanel
        role={{
          role: 'route_l2',
          active_plugin: 'l2-planner-main',
          single_instance: true,
          plugins: [
            { id: 'l2-planner-main', label: 'L2 Planner Main', service: 'plugin-route-l2-main', container_name: 'route', status: 'running', health: 'degraded', image: 'mass-l2-planner:main', revision: 'unknown', required_topics: [] },
            { id: 'tdl-mock-route', label: 'TDL Mock Route', service: 'plugin-route-tdl-mock', container_name: '', status: 'stopped', health: 'unknown', image: 'mass-l3-plugin-route-mock:local', revision: 'local', required_topics: [] },
          ],
        }}
        onSwitch={onSwitch}
        onRestart={vi.fn()}
        onStop={vi.fn()}
      />,
    );

    fireEvent.change(screen.getByLabelText(/L2 航线规划/i), { target: { value: 'tdl-mock-route' } });
    fireEvent.click(screen.getByRole('button', { name: /Switch route_l2/i }));

    expect(onSwitch).toHaveBeenCalledWith('route_l2', 'tdl-mock-route');
  });
});
```

Create `web/src/screens/runtime/__tests__/RuntimeModeSwitch.test.tsx`:

```tsx
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { RuntimeModeSwitch } from '../RuntimeModeSwitch';

describe('RuntimeModeSwitch', () => {
  it('shows internal and integration as prominent actions', () => {
    const onChange = vi.fn();

    render(<RuntimeModeSwitch mode="internal" onChange={onChange} />);

    expect(screen.getByRole('button', { name: '内测' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '集成' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '集成' }));
    expect(onChange).toHaveBeenCalledWith('integration');
  });
});
```

Create `web/src/screens/runtime/__tests__/CheckCategoryNav.test.tsx`:

```tsx
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { CheckCategoryNav } from '../CheckCategoryNav';

describe('CheckCategoryNav', () => {
  it('shows runtime categories and selects a category', () => {
    const onSelect = vi.fn();

    render(
      <CheckCategoryNav
        selected="mode"
        onSelect={onSelect}
        status={{
          mode: 'ACTIVE',
          core: '4/4',
          plugins: '2/3',
          ros: 'OK',
          safety: 'OK',
          verdict: 'WAIT',
        }}
      />,
    );

    expect(screen.getByText('TDL 核心容器')).toBeInTheDocument();
    expect(screen.getByText('外部插件容器')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: /外部插件容器/i }));
    expect(onSelect).toHaveBeenCalledWith('plugins');
  });
});
```

- [ ] **Step 2: Run component tests and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/runtime/__tests__
```

Expected: import failures for new components.

- [ ] **Step 3: Implement RuntimeModeSwitch**

Create `web/src/screens/runtime/RuntimeModeSwitch.tsx`:

```tsx
import type { RuntimeMode } from '../../api/silApi';

export function RuntimeModeSwitch({ mode, onChange }: { mode: RuntimeMode; onChange: (mode: RuntimeMode) => void }) {
  return (
    <div style={{ display: 'inline-flex', border: '1px solid var(--line-1)', borderRadius: 6, overflow: 'hidden' }}>
      {(['internal', 'integration'] as RuntimeMode[]).map((item) => (
        <button
          key={item}
          type="button"
          onClick={() => onChange(item)}
          style={{
            height: 36,
            minWidth: 86,
            border: 0,
            borderRadius: 0,
            background: mode === item ? 'var(--c-info)' : 'var(--bg-2)',
            color: mode === item ? 'var(--bg-0)' : 'var(--txt-1)',
            fontWeight: 800,
          }}
        >
          {item === 'internal' ? '内测' : '集成'}
        </button>
      ))}
    </div>
  );
}
```

- [ ] **Step 4: Implement CheckCategoryNav**

Create `web/src/screens/runtime/CheckCategoryNav.tsx`:

```tsx
export type RuntimeCategory = 'mode' | 'core' | 'plugins' | 'ros' | 'safety' | 'verdict';

const LABELS: Record<RuntimeCategory, string> = {
  mode: '运行模式',
  core: 'TDL 核心容器',
  plugins: '外部插件容器',
  ros: 'ROS2 数据链路',
  safety: '安全边界',
  verdict: '放行结论',
};

export function CheckCategoryNav({
  selected,
  onSelect,
  status,
}: {
  selected: RuntimeCategory;
  onSelect: (category: RuntimeCategory) => void;
  status: Record<RuntimeCategory, string>;
}) {
  return (
    <nav style={{ display: 'grid', gap: 8 }}>
      {(Object.keys(LABELS) as RuntimeCategory[]).map((category, index) => (
        <button
          key={category}
          type="button"
          onClick={() => onSelect(category)}
          style={{
            textAlign: 'left',
            minHeight: 58,
            border: `1px solid ${selected === category ? 'var(--c-info)' : 'var(--line-1)'}`,
            background: selected === category ? 'var(--bg-2)' : 'var(--bg-1)',
            color: 'var(--txt-1)',
            borderRadius: 5,
            padding: '8px 10px',
          }}
        >
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10 }}>0{index + 1}</span>
          <span style={{ display: 'block', fontWeight: 800, marginTop: 4 }}>{LABELS[category]}</span>
          <span style={{ display: 'block', color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>{status[category] ?? 'IDLE'}</span>
        </button>
      ))}
    </nav>
  );
}
```

- [ ] **Step 5: Implement CoreServicePanel and PluginRolePanel**

Create focused components using the prop contracts from tests. Maintain:

- no single-service stop button for core service cards.
- `Stop Core Stack` visible once.
- plugin role dropdown with accessible label containing Chinese role label.
- `Switch {role}` button for non-active selected plugin.
- `Restart` and `Stop` buttons for active plugin.

- [ ] **Step 6: Run component tests**

Run:

```bash
cd web && npm test -- --run src/screens/runtime/__tests__
```

Expected: all runtime component tests pass.

- [ ] **Step 7: Commit Task 7**

```bash
git add web/src/screens/runtime
git commit -m "feat: add runtime console components"
```

## Task 8: Integrate Runtime Console into Screen 02

**Files:**
- Modify: `web/src/screens/SimulationCheck.tsx`
- Remove: `web/src/screens/shared/ExternalIntegrationPanel.tsx`
- Test: `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx`
- Update or remove: `web/src/screens/__tests__/SimulationCheck.external.test.tsx`

- [ ] **Step 1: Write Screen 02 integration tests**

Create `web/src/screens/__tests__/SimulationCheck.runtime.test.tsx` based on the existing `SimulationCheck.external.test.tsx` mock pattern.

Required assertions:

```tsx
expect(screen.getByText('仿真检查 · 容器运行台')).toBeInTheDocument();
expect(screen.getByRole('button', { name: '内测' })).toBeInTheDocument();
expect(screen.getByRole('button', { name: '集成' })).toBeInTheDocument();
expect(screen.getByText('TDL 核心容器')).toBeInTheDocument();
expect(screen.getByText('外部插件容器')).toBeInTheDocument();
expect(screen.queryByTestId('external-integration-panel')).not.toBeInTheDocument();
```

Add GO blocking assertion:

```tsx
expect(mocks.probeRuntime).toHaveBeenCalled();
expect(mocks.configureLifecycle).not.toHaveBeenCalled();
expect(screen.getByText(/Runtime gate failed/)).toBeInTheDocument();
```

- [ ] **Step 2: Run tests and confirm failure**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationCheck.runtime.test.tsx
```

Expected: old Screen 02 still renders `ExternalIntegrationPanel`.

- [ ] **Step 3: Replace Screen 02 layout**

Modify `web/src/screens/SimulationCheck.tsx`:

- import runtime hooks from `silApi`.
- remove `ExternalIntegrationPanel`.
- add state `selectedCategory`.
- render top mode switch.
- render category nav left.
- render summary cards and runtime panels center.
- render runtime action log right.
- keep existing lifecycle `handleProceed`, `handleAbort`, dev skip, countdown, hotkeys.

Preserve existing GO-path reset logic:

```tsx
useTelemetryStore.getState().reset();
useControlStore.getState().reset();
```

- [ ] **Step 4: Replace GO path external probe with runtime probe**

In `handleProceed`, before lifecycle cleanup/configure:

```tsx
const runtimeProbe = await probeRuntime().unwrap();
if (runtimeProbe.verdict !== 'GO') {
  const failed = runtimeProbe.gates.find((gate) => !gate.passed);
  setLifecycleError(`Runtime gate failed: ${failed?.label ?? 'unknown'}`);
  setTransitioning(false);
  proceedingRef.current = false;
  return;
}
```

Keep existing error handling form:

```tsx
setLifecycleError(`Runtime probe failed: ${e instanceof Error ? e.message : String(e)}`);
```

- [ ] **Step 5: Run Screen 02 tests**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationCheck.runtime.test.tsx
```

Expected: tests pass.

- [ ] **Step 6: Run previous shared component tests**

Run:

```bash
cd web && npm test -- --run src/screens/shared/__tests__ src/screens/runtime/__tests__
```

Expected: tests pass. If `ExternalIntegrationPanel` tests remain, delete or rewrite them to runtime console tests in this task.

- [ ] **Step 7: Commit Task 8**

```bash
git add web/src/screens/SimulationCheck.tsx web/src/screens/runtime web/src/screens/__tests__/SimulationCheck.runtime.test.tsx web/src/screens/__tests__/SimulationCheck.external.test.tsx web/src/screens/shared/ExternalIntegrationPanel.tsx
git commit -m "feat: replace screen 02 with runtime console"
```

## Task 9: Full Verification, Docs, and Local Gate

**Files:**
- Modify: `docs/Design/SIL/external-module-adapter-runbook.md`
- Modify: `docs/Design/SIL/external-module-adapter-development-ledger.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: Update runbook**

Add this exact section to `docs/Design/SIL/external-module-adapter-runbook.md`:

````markdown
## Runtime Console Local Gate

Screen 02 `仿真检查` owns runtime readiness. Before A4000 sync:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

Expected:

- `/api/v1/runtime/summary` returns `active_profile`.
- `/api/v1/runtime/probe` writes `runs/local_runtime_probe_*.json`.
- TDL core services are visible as `core_service`.
- Plugin roles are visible as `plugin_service`.
- Core services expose restart but no per-service stop.
- Plugin roles enforce one active plugin per role.
```
````

- [ ] **Step 2: Update ledger**

Append to `docs/Design/SIL/external-module-adapter-development-ledger.md`:

```markdown
| 8 | Screen 02 plugin runtime console | codex/plugin-runtime-console | DONE | local pytest/frontend/local gate evidence | commit hash after implementation |
```

- [ ] **Step 3: Run backend tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest \
  tests/sil_orchestrator/runtime \
  tests/sil_orchestrator/test_integration_profiles.py \
  tests/sil_orchestrator/test_integration_routes.py \
  tests/scripts/test_runtime_plugin_compose.py \
  -q
```

Expected: all tests pass.

- [ ] **Step 4: Run frontend tests**

Run:

```bash
cd web && npm test -- --run \
  src/screens/runtime/__tests__ \
  src/screens/__tests__/SimulationCheck.runtime.test.tsx
```

Expected: all tests pass.

- [ ] **Step 5: Run frontend build**

Run:

```bash
cd web && npm run build
```

Expected: build exits 0. Existing Foxglove dependency eval/chunk warnings may remain.

- [ ] **Step 6: Run local OrbStack gate**

Run:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

Expected:

- `LOCAL A4000 CONTAINER ACCEPTANCE PASS`
- latest `runs/local_runtime_probe_*.json` exists
- latest `runs/local_a4000_container_probe_*.json` exists

- [ ] **Step 7: Browser verification**

Start frontend if needed:

```bash
cd web
ORCH_PORT=18000 FOX_PORT=18765 npm run dev -- --host 0.0.0.0
```

Open:

```text
http://localhost:5173/#/check/colreg-rule14-ho
```

Verify:

- top `内测 / 集成` switch visible without scrolling.
- `TDL 核心容器` shows four services.
- each core service shows `Restart` and no per-service `Stop`.
- `Stop Core Stack` is group-level only.
- `外部插件容器` shows hydrodynamics, L2 route planning, fusion.
- each plugin role has only one active plugin.
- failing plugin gate blocks lifecycle activation.

- [ ] **Step 8: Commit Task 9**

```bash
git add docs/Design/SIL/external-module-adapter-runbook.md docs/Design/SIL/external-module-adapter-development-ledger.md AGENTS.md
git commit -m "docs: record runtime console verification flow"
```

## Final Verification Before A4000 Sync

Run all commands from Task 9 Steps 3-6. Do not sync to A4000 until they pass.

After local pass, sync only touched paths to A4000. Do not run `git pull`, `git reset`, `rsync --delete`, or broad checkout replacement on A4000.

## Self-Review Checklist

- Spec Goal 1, unified core/plugin runtime console: Tasks 3, 4, 7, 8.
- Spec Goal 2, `内测 / 集成` switch: Tasks 6, 7, 8.
- Spec Goal 3, one active plugin per role: Tasks 1, 3, 7, 8.
- Spec Goal 4, plugin start/stop/restart: Tasks 2, 3, 4, 7.
- Spec Goal 5, core restart and stack stop: Tasks 2, 3, 4, 7.
- Spec Goal 6, readiness gates: Tasks 3, 4, 8, 9.
- Spec Goal 7, GO blocking: Task 8.
- Spec Goal 8, evidence: Tasks 3, 4, 9.
- Spec Goal 9, local/A4000 alignment: Tasks 5, 9.

No implementation task should push to GitHub/GitLab. A4000 sync happens only after local gate passes and only by narrow touched-path sync.
