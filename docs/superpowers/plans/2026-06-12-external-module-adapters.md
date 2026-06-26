# External Module Adapters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a hot-pluggable integration layer so Screen 02 can select an external profile, verify A4000 external ROS2 modules, receive canonical TDL inputs, and send TDL avoidance routes to external GNC without changing default TDL behavior. Local OrbStack must run the A4000-equivalent container gate before any A4000 sync.

**Architecture:** Keep TDL default path unchanged. Add profile-driven integration APIs, cross-domain adapter packages, and a Screen 02 panel that gates external profile activation. External ROS workspaces remain isolated by `ROS_DOMAIN_ID`; sidecars translate external ROS types to neutral JSON over localhost IPC, then TDL-domain sidecars publish canonical TDL topics.

**Tech Stack:** Python 3.10+, FastAPI, pytest, ROS2 Humble `rclpy`, ament_python, React + TypeScript + RTK Query + Vitest, YAML config.

---

## Source Spec

Spec document: `docs/Design/SIL/external-module-adapter-spec.md`

Current verified code anchors:

- Screen 02 entry: `web/src/screens/SimulationCheck.tsx`
- Frontend API slice: `web/src/api/silApi.ts`
- Existing self-check router: `src/sil_orchestrator/selfcheck_routes.py`
- Existing orchestrator app router includes: `src/sil_orchestrator/main.py`
- Existing route ingest pattern: `docker/route_ingest_node.py`
- TDL canonical input messages: `src/l3_tdl_kernel/l3_external_msgs/msg/*.msg`
- TDL avoidance output message: `src/l3_tdl_kernel/l3_msgs/msg/AvoidancePlan.msg`

## Parallelization Map

Agents can run these lanes in parallel after creating isolated worktrees:

| Lane | Tasks | Dependencies |
|---|---|---|
| Backend profile/API | Task 1, Task 2 | none |
| Adapter converters/package | Task 3, Task 4 | none |
| Route output | Task 5 | Task 3 |
| Frontend Screen 02 | Task 6 | Task 2 API contract; can start with mocked data |
| Launch/profile wiring + local A4000 container gate | Task 7 | Task 1, Task 3, Task 5 |
| Integration verification and A4000 final gate | Task 8 | all previous tasks |

Merge order:

1. Task 1 and Task 3 first.
2. Task 2, Task 4, Task 5 next.
3. Task 6 after API names stabilize.
4. Task 7 and Task 8 last.

## File Structure

Create:

- `config/integration_profiles/default.yaml` — default protected profile.
- `config/integration_profiles/a4000_external.yaml` — A4000 external module profile.
- `src/sil_orchestrator/integration/__init__.py` — integration package marker.
- `src/sil_orchestrator/integration/profiles.py` — profile dataclasses, loader, validation.
- `src/sil_orchestrator/integration/probe.py` — command-backed ROS/domain/topic probe logic.
- `src/sil_orchestrator/integration/routes.py` — FastAPI routes under `/api/v1/integration`.
- `src/sim_workbench/external_adapters/package.xml` — ROS2 Python package manifest.
- `src/sim_workbench/external_adapters/setup.py` — ament_python setup.
- `src/sim_workbench/external_adapters/resource/external_adapters` — ament resource marker.
- `src/sim_workbench/external_adapters/external_adapters/__init__.py` — package marker.
- `src/sim_workbench/external_adapters/external_adapters/neutral.py` — neutral payload dataclasses.
- `src/sim_workbench/external_adapters/external_adapters/converters.py` — pure converter helpers.
- `src/sim_workbench/external_adapters/external_adapters/ipc.py` — newline JSON TCP IPC helpers.
- `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py` — TDL-domain canonical publisher.
- `src/sim_workbench/external_adapters/external_adapters/route_out_tdl_node.py` — TDL-domain AvoidancePlan capture.
- `src/sim_workbench/external_adapters/external_adapters/route_out_external_path_node.py` — external-domain `/ship/waypoints` publisher.
- `scripts/integration/start_external_adapters.sh` — profile-based local launcher.
- `scripts/local-a4000-env.sh` — local OrbStack env that reuses A4000 compose override.
- `scripts/local-a4000-acceptance.sh` — local A4000-equivalent container gate before A4000 sync.
- `tests/sil_orchestrator/test_integration_profiles.py` — profile loader tests.
- `tests/sil_orchestrator/test_integration_routes.py` — integration API tests.
- `tests/sim_workbench/external_adapters/test_converters.py` — converter unit tests.
- `tests/sim_workbench/external_adapters/test_ipc.py` — IPC unit tests.
- `tests/sim_workbench/external_adapters/test_route_out.py` — route_out converter tests.
- `web/src/screens/shared/ExternalIntegrationPanel.tsx` — Screen 02 external gate panel.
- `web/src/screens/__tests__/SimulationCheck.external.test.tsx` — Screen 02 integration panel tests.

Modify:

- `src/sil_orchestrator/main.py` — include integration router.
- `web/src/api/silApi.ts` — add integration DTOs and RTK Query endpoints.
- `web/src/screens/SimulationCheck.tsx` — mount `ExternalIntegrationPanel` in right rail and include external gate status in GO behavior.

Do not modify:

- `docker/sil_topic_bridge.py` for external module production logic.
- TDL canonical `.msg` files unless a future contract review explicitly changes IDL.
- External A4000 worktrees with git sync commands.

---

### Task 1: Backend Profile Loader

**Files:**
- Create: `config/integration_profiles/default.yaml`
- Create: `config/integration_profiles/a4000_external.yaml`
- Create: `src/sil_orchestrator/integration/__init__.py`
- Create: `src/sil_orchestrator/integration/profiles.py`
- Test: `tests/sil_orchestrator/test_integration_profiles.py`

- [ ] **Step 1: Write failing profile tests**

Create `tests/sil_orchestrator/test_integration_profiles.py`:

```python
from pathlib import Path

import pytest

from sil_orchestrator.integration.profiles import (
    AdapterState,
    IntegrationProfileError,
    load_profile,
    load_profiles,
)


def test_load_default_profile_keeps_external_disabled():
    profile = load_profile(Path("config/integration_profiles/default.yaml"))

    assert profile.name == "default"
    assert profile.mode == "default"
    assert profile.tdl_domain_id == 42
    assert profile.adapters.target is AdapterState.DISABLED
    assert profile.adapters.route_out is AdapterState.DISABLED
    assert profile.safety.forbid_low_level_control is True


def test_load_a4000_profile_enables_route_level_closed_loop():
    profile = load_profile(Path("config/integration_profiles/a4000_external.yaml"))

    assert profile.name == "a4000_external"
    assert profile.mode == "external"
    assert profile.external_domains["simulation"].domain_id == 10
    assert profile.external_domains["yougc"].domain_id == 11
    assert profile.adapters.target is AdapterState.ENABLED
    assert profile.adapters.ownship is AdapterState.ENABLED
    assert profile.adapters.route_out is AdapterState.ENABLED
    assert profile.safety.route_out_requires_screen02_pass is True


def test_load_profiles_returns_name_keyed_profiles():
    profiles = load_profiles(Path("config/integration_profiles"))

    assert set(profiles) >= {"default", "a4000_external"}
    assert profiles["default"].mode == "default"
    assert profiles["a4000_external"].mode == "external"


def test_rejects_external_profile_without_domain(tmp_path):
    profile_path = tmp_path / "bad.yaml"
    profile_path.write_text(
        """
name: bad_external
mode: external
tdl_domain_id: 42
external_domains: {}
adapters:
  target: enabled
  ownship: disabled
  environment: disabled
  route_in: disabled
  route_out: disabled
freshness:
  ownship_ms: 500
  targets_ms: 2000
  environment_ms: 10000
safety:
  route_out_requires_screen02_pass: true
  forbid_low_level_control: true
""",
        encoding="utf-8",
    )

    with pytest.raises(IntegrationProfileError, match="external profile requires"):
        load_profile(profile_path)
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_integration_profiles.py -q
```

Expected:

```text
ModuleNotFoundError: No module named 'sil_orchestrator.integration'
```

- [ ] **Step 3: Add profile YAML files**

Create `config/integration_profiles/default.yaml`:

```yaml
name: default
mode: default
tdl_domain_id: 42
external_domains: {}
adapters:
  target: disabled
  ownship: disabled
  environment: disabled
  route_in: disabled
  route_out: disabled
freshness:
  ownship_ms: 500
  targets_ms: 2000
  environment_ms: 10000
safety:
  route_out_requires_screen02_pass: true
  forbid_low_level_control: true
```

Create `config/integration_profiles/a4000_external.yaml`:

```yaml
name: a4000_external
mode: external
tdl_domain_id: 42
external_domains:
  simulation:
    domain_id: 10
    workspace_setup: /home/mass/simulation/船舶动力学/gnc_ws/install/setup.bash
    required_topics:
      /route_planning/route_plan: ship_interfaces/msg/RoutePlan
      /ship/waypoints: nav_msgs/msg/Path
      /ship/odometry: nav_msgs/msg/Odometry
  yougc:
    domain_id: 11
    workspace_setup: /home/mass/yougc/ros2_ws/install/setup.bash
    required_topics:
      /fusion/tracked_targets: nmea_interfaces/msg/TrackedTargetArray
      /gps/fix: nmea_interfaces/msg/Gps
      /heading: nmea_interfaces/msg/Heading
adapters:
  target: enabled
  ownship: enabled
  environment: enabled
  route_in: enabled
  route_out: enabled
freshness:
  ownship_ms: 500
  targets_ms: 2000
  environment_ms: 10000
safety:
  route_out_requires_screen02_pass: true
  forbid_low_level_control: true
```

- [ ] **Step 4: Add profile loader implementation**

Create `src/sil_orchestrator/integration/__init__.py`:

```python
"""External module integration support for SIL orchestrator."""
```

Create `src/sil_orchestrator/integration/profiles.py`:

```python
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any

import yaml


class IntegrationProfileError(ValueError):
    """Raised when an integration profile is invalid."""


class AdapterState(str, Enum):
    ENABLED = "enabled"
    DISABLED = "disabled"


@dataclass(frozen=True)
class ExternalDomain:
    domain_id: int
    workspace_setup: str | None = None
    required_topics: dict[str, str] | None = None


@dataclass(frozen=True)
class AdapterConfig:
    target: AdapterState
    ownship: AdapterState
    environment: AdapterState
    route_in: AdapterState
    route_out: AdapterState


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
    external_domains: dict[str, ExternalDomain]
    adapters: AdapterConfig
    freshness: FreshnessConfig
    safety: SafetyConfig

    @property
    def external_enabled(self) -> bool:
        return self.mode == "external"


def _require_mapping(data: Any, label: str) -> dict[str, Any]:
    if not isinstance(data, dict):
        raise IntegrationProfileError(f"{label} must be a mapping")
    return data


def _adapter_state(value: Any, label: str) -> AdapterState:
    try:
        return AdapterState(str(value))
    except ValueError as exc:
        raise IntegrationProfileError(f"{label} must be enabled or disabled") from exc


def load_profile(path: Path) -> IntegrationProfile:
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    data = _require_mapping(raw, str(path))

    name = str(data.get("name", "")).strip()
    mode = str(data.get("mode", "")).strip()
    if not name:
        raise IntegrationProfileError("profile name is required")
    if mode not in {"default", "external", "hybrid_debug"}:
        raise IntegrationProfileError("profile mode must be default, external, or hybrid_debug")

    domains_raw = _require_mapping(data.get("external_domains", {}), "external_domains")
    domains: dict[str, ExternalDomain] = {}
    for domain_name, domain_data_raw in domains_raw.items():
        domain_data = _require_mapping(domain_data_raw, f"external_domains.{domain_name}")
        topics = domain_data.get("required_topics") or {}
        domains[str(domain_name)] = ExternalDomain(
            domain_id=int(domain_data["domain_id"]),
            workspace_setup=domain_data.get("workspace_setup"),
            required_topics={str(k): str(v) for k, v in dict(topics).items()},
        )

    if mode == "external" and not domains:
        raise IntegrationProfileError("external profile requires at least one external domain")

    adapters_raw = _require_mapping(data.get("adapters"), "adapters")
    freshness_raw = _require_mapping(data.get("freshness"), "freshness")
    safety_raw = _require_mapping(data.get("safety"), "safety")

    return IntegrationProfile(
        name=name,
        mode=mode,
        tdl_domain_id=int(data.get("tdl_domain_id", 42)),
        external_domains=domains,
        adapters=AdapterConfig(
            target=_adapter_state(adapters_raw.get("target"), "adapters.target"),
            ownship=_adapter_state(adapters_raw.get("ownship"), "adapters.ownship"),
            environment=_adapter_state(adapters_raw.get("environment"), "adapters.environment"),
            route_in=_adapter_state(adapters_raw.get("route_in"), "adapters.route_in"),
            route_out=_adapter_state(adapters_raw.get("route_out"), "adapters.route_out"),
        ),
        freshness=FreshnessConfig(
            ownship_ms=int(freshness_raw["ownship_ms"]),
            targets_ms=int(freshness_raw["targets_ms"]),
            environment_ms=int(freshness_raw["environment_ms"]),
        ),
        safety=SafetyConfig(
            route_out_requires_screen02_pass=bool(safety_raw["route_out_requires_screen02_pass"]),
            forbid_low_level_control=bool(safety_raw["forbid_low_level_control"]),
        ),
    )


def load_profiles(directory: Path) -> dict[str, IntegrationProfile]:
    profiles = {
        profile.name: profile
        for profile in (load_profile(path) for path in sorted(directory.glob("*.yaml")))
    }
    if "default" not in profiles:
        raise IntegrationProfileError("default profile is required")
    return profiles
```

- [ ] **Step 5: Run tests and verify pass**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_integration_profiles.py -q
```

Expected:

```text
4 passed
```

- [ ] **Step 6: Commit**

```bash
git add config/integration_profiles src/sil_orchestrator/integration tests/sil_orchestrator/test_integration_profiles.py
git commit -m "feat: add external integration profiles"
```

---

### Task 2: Backend Probe Service and Integration Routes

**Files:**
- Create: `src/sil_orchestrator/integration/probe.py`
- Create: `src/sil_orchestrator/integration/routes.py`
- Modify: `src/sil_orchestrator/main.py`
- Test: `tests/sil_orchestrator/test_integration_routes.py`

- [ ] **Step 1: Write failing route tests**

Create `tests/sil_orchestrator/test_integration_routes.py`:

```python
import sys
from unittest.mock import MagicMock, patch

sys.modules["rclpy"] = MagicMock()
sys.modules["rclpy.node"] = MagicMock()
sys.modules["rclpy.callback_groups"] = MagicMock()
sys.modules["rclpy.executors"] = MagicMock()
sys.modules["sil_orchestrator.telemetry_bridge"] = MagicMock()
sys.modules["sil_orchestrator.lifecycle_bridge"] = MagicMock()
sys.modules["polars"] = MagicMock()

from fastapi.testclient import TestClient

from sil_orchestrator.integration.probe import ProbeCheck, ProbeReport
from sil_orchestrator.main import app


def test_profiles_endpoint_lists_default_and_a4000():
    client = TestClient(app)

    resp = client.get("/api/v1/integration/profiles")

    assert resp.status_code == 200
    body = resp.json()
    assert [item["name"] for item in body["profiles"]] == ["a4000_external", "default"]
    assert body["active_profile"] == "default"


def test_select_profile_changes_active_profile():
    client = TestClient(app)

    resp = client.post("/api/v1/integration/profile", json={"name": "a4000_external"})

    assert resp.status_code == 200
    assert resp.json()["active_profile"] == "a4000_external"
    assert client.get("/api/v1/integration/profile").json()["profile"]["name"] == "a4000_external"


def test_probe_uses_probe_service_and_returns_gate_shape():
    report = ProbeReport(
        profile_name="a4000_external",
        all_clear=False,
        checks=[
            ProbeCheck(
                gate_id=101,
                label="Profile valid",
                passed=True,
                detail="profile a4000_external loaded",
            ),
            ProbeCheck(
                gate_id=108,
                label="Low-level control forbidden",
                passed=False,
                detail="/cmd_tau is present in forbidden output list",
            ),
        ],
    )
    client = TestClient(app)
    client.post("/api/v1/integration/profile", json={"name": "a4000_external"})

    with patch("sil_orchestrator.integration.routes.probe_active_profile", return_value=report):
        resp = client.post("/api/v1/integration/probe")

    assert resp.status_code == 200
    body = resp.json()
    assert body["profile_name"] == "a4000_external"
    assert body["all_clear"] is False
    assert body["checks"][1]["label"] == "Low-level control forbidden"
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_integration_routes.py -q
```

Expected:

```text
ModuleNotFoundError: No module named 'sil_orchestrator.integration.probe'
```

- [ ] **Step 3: Add probe dataclasses and command runner**

Create `src/sil_orchestrator/integration/probe.py`:

```python
from __future__ import annotations

from dataclasses import asdict, dataclass
import os
import subprocess
from typing import Protocol

from .profiles import AdapterState, IntegrationProfile


FORBIDDEN_OUTPUT_TOPICS = {"/cmd_tau", "/thruster/commands"}


class CommandRunner(Protocol):
    def __call__(self, command: list[str], env: dict[str, str], timeout_s: float) -> subprocess.CompletedProcess[str]:
        ...


@dataclass(frozen=True)
class ProbeCheck:
    gate_id: int
    label: str
    passed: bool
    detail: str

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True)
class ProbeReport:
    profile_name: str
    all_clear: bool
    checks: list[ProbeCheck]

    def to_dict(self) -> dict[str, object]:
        return {
            "profile_name": self.profile_name,
            "all_clear": self.all_clear,
            "checks": [check.to_dict() for check in self.checks],
        }


def run_command(command: list[str], env: dict[str, str], timeout_s: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        env=env,
        timeout=timeout_s,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def _topic_info(topic: str, domain_id: int, runner: CommandRunner) -> tuple[bool, str]:
    env = os.environ.copy()
    env["ROS_DOMAIN_ID"] = str(domain_id)
    result = runner(["ros2", "topic", "info", topic], env, 3.0)
    output = "\n".join(part for part in [result.stdout, result.stderr] if part)
    return result.returncode == 0, output.strip()


def probe_active_profile(profile: IntegrationProfile, runner: CommandRunner = run_command) -> ProbeReport:
    checks: list[ProbeCheck] = [
        ProbeCheck(101, "Profile valid", True, f"profile {profile.name} loaded"),
    ]

    if profile.mode == "default":
        checks.append(ProbeCheck(102, "External adapters disabled", True, "default profile uses internal TDL/SIL path"))
        return ProbeReport(profile.name, all(check.passed for check in checks), checks)

    for domain_name, domain in profile.external_domains.items():
        if domain.workspace_setup:
            checks.append(
                ProbeCheck(
                    102,
                    f"Workspace present: {domain_name}",
                    os.path.exists(domain.workspace_setup),
                    domain.workspace_setup,
                )
            )
        for topic, expected_type in (domain.required_topics or {}).items():
            ok, detail = _topic_info(topic, domain.domain_id, runner)
            checks.append(
                ProbeCheck(
                    104,
                    f"Topic reachable: {topic}",
                    ok and expected_type in detail,
                    detail or f"expected {expected_type}",
                )
            )

    route_level_only = profile.adapters.route_out is AdapterState.ENABLED and profile.safety.forbid_low_level_control
    forbidden_detail = "route_out publishes route/waypoints only" if route_level_only else "low-level control output not forbidden"
    checks.append(ProbeCheck(108, "Low-level control forbidden", route_level_only, forbidden_detail))

    return ProbeReport(profile.name, all(check.passed for check in checks), checks)
```

- [ ] **Step 4: Add integration routes**

Create `src/sil_orchestrator/integration/routes.py`:

```python
from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from .probe import probe_active_profile
from .profiles import IntegrationProfile, IntegrationProfileError, load_profiles


router = APIRouter(prefix="/api/v1/integration")
PROFILE_DIR = Path("config/integration_profiles")
_active_profile_name = "default"


class SelectProfileRequest(BaseModel):
    name: str


def _profiles() -> dict[str, IntegrationProfile]:
    try:
        return load_profiles(PROFILE_DIR)
    except IntegrationProfileError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


def _profile_to_dict(profile: IntegrationProfile) -> dict[str, object]:
    return {
        "name": profile.name,
        "mode": profile.mode,
        "tdl_domain_id": profile.tdl_domain_id,
        "external_enabled": profile.external_enabled,
        "adapters": {
            "target": profile.adapters.target.value,
            "ownship": profile.adapters.ownship.value,
            "environment": profile.adapters.environment.value,
            "route_in": profile.adapters.route_in.value,
            "route_out": profile.adapters.route_out.value,
        },
        "freshness": {
            "ownship_ms": profile.freshness.ownship_ms,
            "targets_ms": profile.freshness.targets_ms,
            "environment_ms": profile.freshness.environment_ms,
        },
        "external_domains": {
            name: {
                "domain_id": domain.domain_id,
                "workspace_setup": domain.workspace_setup,
                "required_topics": domain.required_topics or {},
            }
            for name, domain in profile.external_domains.items()
        },
    }


@router.get("/profiles")
async def list_integration_profiles():
    profiles = _profiles()
    ordered = sorted(profiles.values(), key=lambda item: item.name)
    return {
        "active_profile": _active_profile_name,
        "profiles": [_profile_to_dict(profile) for profile in ordered],
    }


@router.get("/profile")
async def get_active_profile():
    profiles = _profiles()
    profile = profiles.get(_active_profile_name, profiles["default"])
    return {"active_profile": profile.name, "profile": _profile_to_dict(profile)}


@router.post("/profile")
async def select_profile(request: SelectProfileRequest):
    global _active_profile_name
    profiles = _profiles()
    if request.name not in profiles:
        raise HTTPException(status_code=404, detail=f"profile not found: {request.name}")
    _active_profile_name = request.name
    return {"active_profile": _active_profile_name}


@router.get("/status")
async def integration_status():
    profiles = _profiles()
    profile = profiles.get(_active_profile_name, profiles["default"])
    return {
        "active_profile": profile.name,
        "external_enabled": profile.external_enabled,
        "route_out_enabled": profile.adapters.route_out.value == "enabled",
    }


@router.post("/probe")
async def probe_integration():
    profiles = _profiles()
    profile = profiles.get(_active_profile_name, profiles["default"])
    return probe_active_profile(profile).to_dict()
```

- [ ] **Step 5: Include router in orchestrator app**

Modify `src/sil_orchestrator/main.py` imports:

```python
from sil_orchestrator.integration.routes import router as integration_router
```

Add include near other routers:

```python
app.include_router(integration_router)
```

- [ ] **Step 6: Run route tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_integration_routes.py -q
```

Expected:

```text
3 passed
```

- [ ] **Step 7: Run selfcheck regression**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_selfcheck.py -q
```

Expected:

```text
5 passed
```

- [ ] **Step 8: Commit**

```bash
git add src/sil_orchestrator/main.py src/sil_orchestrator/integration tests/sil_orchestrator/test_integration_routes.py
git commit -m "feat: expose external integration probe API"
```

---

### Task 3: External Adapter Package and Pure Converters

**Files:**
- Create: `src/sim_workbench/external_adapters/package.xml`
- Create: `src/sim_workbench/external_adapters/setup.py`
- Create: `src/sim_workbench/external_adapters/resource/external_adapters`
- Create: `src/sim_workbench/external_adapters/external_adapters/__init__.py`
- Create: `src/sim_workbench/external_adapters/external_adapters/neutral.py`
- Create: `src/sim_workbench/external_adapters/external_adapters/converters.py`
- Test: `tests/sim_workbench/external_adapters/test_converters.py`

- [ ] **Step 1: Write failing converter tests**

Create `tests/sim_workbench/external_adapters/test_converters.py`:

```python
from types import SimpleNamespace

from external_adapters.converters import (
    avoidance_plan_to_path_payload,
    neutral_environment_to_canonical_dict,
    neutral_ownship_to_canonical_dict,
    neutral_targets_to_canonical_dict,
    route_points_to_planned_route_dict,
)
from external_adapters.neutral import (
    NeutralEnvironment,
    NeutralOwnship,
    NeutralRoutePoint,
    NeutralTarget,
)


def test_neutral_target_maps_to_l3_target_fields():
    out = neutral_targets_to_canonical_dict(
        stamp_sec=10,
        stamp_nanosec=20,
        targets=[
            NeutralTarget(
                target_id=123,
                lat=1.1,
                lon=105.2,
                sog_kn=8.0,
                cog_deg=90.0,
                heading_deg=88.0,
                source_sensor="fused",
                confidence=0.7,
            )
        ],
    )

    assert out["schema_version"] == 112
    assert out["stamp"] == {"sec": 10, "nanosec": 20}
    assert out["targets"][0]["target_id"] == 123
    assert out["targets"][0]["position"]["latitude"] == 1.1
    assert out["targets"][0]["classification"] == "vessel"
    assert out["targets"][0]["source_sensor"] == "fused"


def test_neutral_ownship_maps_motion_and_current():
    out = neutral_ownship_to_canonical_dict(
        NeutralOwnship(
            stamp_sec=2,
            stamp_nanosec=3,
            lat=-1.5,
            lon=105.12,
            sog_kn=12.4,
            cog_deg=63.0,
            heading_deg=64.0,
            u_water=5.8,
            v_water=0.1,
            r_dot_deg_s=0.02,
            current_speed_kn=1.2,
            current_direction_deg=220.0,
            confidence=0.9,
            nav_mode="OPTIMAL",
        )
    )

    assert out["position"]["latitude"] == -1.5
    assert out["sog_kn"] == 12.4
    assert out["nav_mode"] == "OPTIMAL"
    assert len(out["covariance"]) == 36


def test_environment_defaults_missing_weather_fields():
    out = neutral_environment_to_canonical_dict(
        NeutralEnvironment(
            stamp_sec=5,
            stamp_nanosec=0,
            wind_speed_kn=4.0,
            wind_direction_deg=180.0,
            current_speed_kn=1.0,
            current_direction_deg=90.0,
            visibility_range_nm=6.0,
            confidence=0.8,
        )
    )

    assert out["wave_height_m"] == 0.0
    assert out["weather_source"] == "sensor"


def test_route_points_compute_total_distance_and_speed_profile():
    out = route_points_to_planned_route_dict(
        stamp_sec=7,
        stamp_nanosec=0,
        points=[
            NeutralRoutePoint(lat=-1.5, lon=105.12, speed_kn=10.0),
            NeutralRoutePoint(lat=-1.4, lon=105.22, speed_kn=11.0),
        ],
    )

    assert out["schema_version"] == 112
    assert out["route_id"] > 0
    assert out["total_distance_nm"] > 0
    assert out["speed_profile_kn"] == [10.0]


def test_avoidance_plan_to_path_payload_uses_waypoints():
    waypoint = SimpleNamespace(
        position=SimpleNamespace(latitude=-1.5, longitude=105.12, altitude=0.0),
        target_speed_kn=9.0,
    )
    plan = SimpleNamespace(
        stamp=SimpleNamespace(sec=9, nanosec=1),
        waypoints=[waypoint],
        confidence=0.85,
        rationale="test plan",
    )

    payload = avoidance_plan_to_path_payload(plan)

    assert payload["kind"] == "route_out_path"
    assert payload["stamp"] == {"sec": 9, "nanosec": 1}
    assert payload["points"] == [{"lat": -1.5, "lon": 105.12, "speed_kn": 9.0}]
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_converters.py -q
```

Expected:

```text
ModuleNotFoundError: No module named 'external_adapters'
```

- [ ] **Step 3: Add ROS package files**

Create `src/sim_workbench/external_adapters/package.xml`:

```xml
<?xml version="1.0"?>
<package format="3">
  <name>external_adapters</name>
  <version>0.1.0</version>
  <description>Cross-domain external module adapters for MASS L3 TDL integration.</description>
  <maintainer email="devnull@example.com">MASS-L3</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_python</buildtool_depend>

  <exec_depend>rclpy</exec_depend>
  <exec_depend>std_msgs</exec_depend>
  <exec_depend>nav_msgs</exec_depend>
  <exec_depend>geographic_msgs</exec_depend>
  <exec_depend>geometry_msgs</exec_depend>
  <exec_depend>l3_msgs</exec_depend>
  <exec_depend>l3_external_msgs</exec_depend>

  <test_depend>pytest</test_depend>

  <export>
    <build_type>ament_python</build_type>
  </export>
</package>
```

Create `src/sim_workbench/external_adapters/setup.py`:

```python
from setuptools import find_packages, setup

package_name = "external_adapters"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", [f"resource/{package_name}"]),
        (f"share/{package_name}", ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    entry_points={
        "console_scripts": [
            "external_tdl_ingress = external_adapters.tdl_ingress_node:main",
            "external_route_out_tdl = external_adapters.route_out_tdl_node:main",
            "external_route_out_path = external_adapters.route_out_external_path_node:main",
        ],
    },
)
```

Create empty marker file `src/sim_workbench/external_adapters/resource/external_adapters`.

Create `src/sim_workbench/external_adapters/external_adapters/__init__.py`:

```python
"""Cross-domain external adapter package."""
```

- [ ] **Step 4: Add neutral dataclasses**

Create `src/sim_workbench/external_adapters/external_adapters/neutral.py`:

```python
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class NeutralTarget:
    target_id: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    source_sensor: str
    confidence: float


@dataclass(frozen=True)
class NeutralOwnship:
    stamp_sec: int
    stamp_nanosec: int
    lat: float
    lon: float
    sog_kn: float
    cog_deg: float
    heading_deg: float
    u_water: float
    v_water: float
    r_dot_deg_s: float
    current_speed_kn: float
    current_direction_deg: float
    confidence: float
    nav_mode: str


@dataclass(frozen=True)
class NeutralEnvironment:
    stamp_sec: int
    stamp_nanosec: int
    wind_speed_kn: float
    wind_direction_deg: float
    current_speed_kn: float
    current_direction_deg: float
    visibility_range_nm: float
    confidence: float


@dataclass(frozen=True)
class NeutralRoutePoint:
    lat: float
    lon: float
    speed_kn: float
```

- [ ] **Step 5: Add converter implementation**

Create `src/sim_workbench/external_adapters/external_adapters/converters.py`:

```python
from __future__ import annotations

import hashlib
import math
from typing import Any

from .neutral import NeutralEnvironment, NeutralOwnship, NeutralRoutePoint, NeutralTarget

EARTH_RADIUS_NM = 3440.065


def _stamp(sec: int, nanosec: int) -> dict[str, int]:
    return {"sec": int(sec), "nanosec": int(nanosec)}


def _geo_point(lat: float, lon: float, altitude: float = 0.0) -> dict[str, float]:
    return {"latitude": float(lat), "longitude": float(lon), "altitude": float(altitude)}


def _haversine_nm(a: NeutralRoutePoint, b: NeutralRoutePoint) -> float:
    dlat = math.radians(b.lat - a.lat)
    dlon = math.radians(b.lon - a.lon)
    x = (
        math.sin(dlat / 2) ** 2
        + math.cos(math.radians(a.lat)) * math.cos(math.radians(b.lat)) * math.sin(dlon / 2) ** 2
    )
    return EARTH_RADIUS_NM * 2 * math.asin(math.sqrt(x))


def _route_id(points: list[NeutralRoutePoint]) -> int:
    text = ";".join(f"{p.lat:.9f},{p.lon:.9f},{p.speed_kn:.3f}" for p in points)
    return int(hashlib.sha1(text.encode("utf-8")).hexdigest()[:12], 16)


def neutral_targets_to_canonical_dict(
    stamp_sec: int,
    stamp_nanosec: int,
    targets: list[NeutralTarget],
) -> dict[str, Any]:
    return {
        "kind": "targets",
        "schema_version": 112,
        "stamp": _stamp(stamp_sec, stamp_nanosec),
        "targets": [
            {
                "schema_version": 112,
                "stamp": _stamp(stamp_sec, stamp_nanosec),
                "target_id": target.target_id,
                "position": _geo_point(target.lat, target.lon),
                "sog_kn": target.sog_kn,
                "cog_deg": target.cog_deg,
                "heading_deg": target.heading_deg,
                "covariance": [0.0] * 9,
                "classification": "vessel",
                "classification_confidence": target.confidence,
                "cpa_m": -1.0,
                "tcpa_s": -1.0,
                "encounter": {
                    "schema_version": 112,
                    "stamp": _stamp(stamp_sec, stamp_nanosec),
                    "confidence": 0.0,
                    "rationale": "external adapter leaves encounter classification to M2",
                    "encounter_type": 0,
                    "relative_bearing_deg": 0.0,
                    "aspect_angle_deg": 0.0,
                    "is_giveway": False,
                },
                "confidence": target.confidence,
                "rationale": "external_adapter target from neutral payload",
                "source_sensor": target.source_sensor,
                "cpa_covariance_m2": -1.0,
                "tcpa_covariance_s2": -1.0,
                "intent_confidence": 0.0,
                "brg_deg": 0.0,
                "rng_m": 0.0,
            }
            for target in targets
        ],
        "confidence": min([target.confidence for target in targets], default=0.0),
        "rationale": f"external_adapter converted {len(targets)} targets",
    }


def neutral_ownship_to_canonical_dict(ownship: NeutralOwnship) -> dict[str, Any]:
    return {
        "kind": "ownship",
        "schema_version": 112,
        "stamp": _stamp(ownship.stamp_sec, ownship.stamp_nanosec),
        "position": _geo_point(ownship.lat, ownship.lon),
        "sog_kn": ownship.sog_kn,
        "cog_deg": ownship.cog_deg,
        "heading_deg": ownship.heading_deg,
        "u_water": ownship.u_water,
        "v_water": ownship.v_water,
        "r_dot_deg_s": ownship.r_dot_deg_s,
        "current_speed_kn": ownship.current_speed_kn,
        "current_direction_deg": ownship.current_direction_deg,
        "roll_deg": 0.0,
        "pitch_deg": 0.0,
        "covariance": [0.0] * 36,
        "nav_mode": ownship.nav_mode,
        "confidence": ownship.confidence,
        "rationale": "external_adapter ownship from neutral payload",
    }


def neutral_environment_to_canonical_dict(environment: NeutralEnvironment) -> dict[str, Any]:
    return {
        "kind": "environment",
        "schema_version": 112,
        "stamp": _stamp(environment.stamp_sec, environment.stamp_nanosec),
        "wind_speed_kn": environment.wind_speed_kn,
        "wind_direction_deg": environment.wind_direction_deg,
        "wave_height_m": 0.0,
        "wave_direction_deg": 0.0,
        "current_speed_kn": environment.current_speed_kn,
        "current_direction_deg": environment.current_direction_deg,
        "visibility_range_nm": environment.visibility_range_nm,
        "weather_source": "sensor",
        "confidence": environment.confidence,
        "rationale": "external_adapter environment from neutral payload",
    }


def route_points_to_planned_route_dict(
    stamp_sec: int,
    stamp_nanosec: int,
    points: list[NeutralRoutePoint],
) -> dict[str, Any]:
    total_distance_nm = sum(_haversine_nm(a, b) for a, b in zip(points, points[1:]))
    speed_profile = [point.speed_kn for point in points[:-1]] or [points[0].speed_kn if points else 0.0]
    cruise_kn = max(speed_profile[0], 0.1)
    return {
        "kind": "route_in",
        "schema_version": 112,
        "stamp": _stamp(stamp_sec, stamp_nanosec),
        "route_id": _route_id(points),
        "route": {
            "header": {"stamp": _stamp(stamp_sec, stamp_nanosec), "frame_id": "WGS84"},
            "poses": [{"position": _geo_point(point.lat, point.lon)} for point in points],
        },
        "total_distance_nm": total_distance_nm,
        "estimated_duration_s": total_distance_nm / cruise_kn * 3600.0,
        "speed_profile_kn": speed_profile,
        "safety_zone": "external_adapter_corridor",
        "confidence": 1.0 if len(points) >= 2 else 0.0,
        "rationale": f"external_adapter route with {len(points)} waypoints",
    }


def avoidance_plan_to_path_payload(plan: Any) -> dict[str, Any]:
    return {
        "kind": "route_out_path",
        "stamp": _stamp(plan.stamp.sec, plan.stamp.nanosec),
        "confidence": float(plan.confidence),
        "rationale": str(plan.rationale),
        "points": [
            {
                "lat": float(waypoint.position.latitude),
                "lon": float(waypoint.position.longitude),
                "speed_kn": float(waypoint.target_speed_kn),
            }
            for waypoint in plan.waypoints
        ],
    }
```

- [ ] **Step 6: Run converter tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_converters.py -q
```

Expected:

```text
5 passed
```

- [ ] **Step 7: Commit**

```bash
git add src/sim_workbench/external_adapters tests/sim_workbench/external_adapters/test_converters.py
git commit -m "feat: add external adapter converters"
```

---

### Task 4: Neutral IPC and TDL Ingress Node

**Files:**
- Create: `src/sim_workbench/external_adapters/external_adapters/ipc.py`
- Create: `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py`
- Test: `tests/sim_workbench/external_adapters/test_ipc.py`

- [ ] **Step 1: Write failing IPC tests**

Create `tests/sim_workbench/external_adapters/test_ipc.py`:

```python
import json

from external_adapters.ipc import decode_line, encode_payload


def test_encode_payload_is_newline_json_bytes():
    encoded = encode_payload({"kind": "ownship", "value": 1})

    assert encoded.endswith(b"\n")
    assert json.loads(encoded.decode("utf-8")) == {"kind": "ownship", "value": 1}


def test_decode_line_rejects_non_mapping():
    try:
        decode_line(b"[1, 2, 3]\n")
    except ValueError as exc:
        assert "JSON object" in str(exc)
    else:
        raise AssertionError("decode_line accepted non-object payload")


def test_decode_line_accepts_known_payload_kind():
    decoded = decode_line(b'{"kind":"targets","targets":[]}\n')

    assert decoded["kind"] == "targets"
    assert decoded["targets"] == []
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_ipc.py -q
```

Expected:

```text
ModuleNotFoundError: No module named 'external_adapters.ipc'
```

- [ ] **Step 3: Add IPC implementation**

Create `src/sim_workbench/external_adapters/external_adapters/ipc.py`:

```python
from __future__ import annotations

import json
from typing import Any

KNOWN_KINDS = {"targets", "ownship", "environment", "route_in", "route_out_path"}


def encode_payload(payload: dict[str, Any]) -> bytes:
    return (json.dumps(payload, separators=(",", ":"), sort_keys=True) + "\n").encode("utf-8")


def decode_line(line: bytes) -> dict[str, Any]:
    decoded = json.loads(line.decode("utf-8"))
    if not isinstance(decoded, dict):
        raise ValueError("neutral IPC payload must be a JSON object")
    kind = decoded.get("kind")
    if kind not in KNOWN_KINDS:
        raise ValueError(f"unknown neutral IPC kind: {kind}")
    return decoded
```

- [ ] **Step 4: Add TDL ingress node**

Create `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py`:

```python
from __future__ import annotations

import socketserver
import threading
from typing import Any

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSHistoryPolicy, QoSReliabilityPolicy

from builtin_interfaces.msg import Time
from geographic_msgs.msg import GeoPath, GeoPoint, GeoPoseStamped
from l3_external_msgs.msg import EnvironmentState, FilteredOwnShipState, PlannedRoute, TrackedTargetArray
from l3_msgs.msg import EncounterClassification, TrackedTarget

from .ipc import decode_line

_LATCHED = QoSProfile(
    reliability=QoSReliabilityPolicy.RELIABLE,
    durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    history=QoSHistoryPolicy.KEEP_LAST,
    depth=5,
)


def _time(data: dict[str, Any]) -> Time:
    msg = Time()
    msg.sec = int(data.get("sec", 0))
    msg.nanosec = int(data.get("nanosec", 0))
    return msg


def _geo_point(data: dict[str, Any]) -> GeoPoint:
    msg = GeoPoint()
    msg.latitude = float(data.get("latitude", 0.0))
    msg.longitude = float(data.get("longitude", 0.0))
    msg.altitude = float(data.get("altitude", 0.0))
    return msg


def _encounter(data: dict[str, Any]) -> EncounterClassification:
    msg = EncounterClassification()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    msg.encounter_type = int(data.get("encounter_type", 0))
    msg.relative_bearing_deg = float(data.get("relative_bearing_deg", 0.0))
    msg.aspect_angle_deg = float(data.get("aspect_angle_deg", 0.0))
    msg.is_giveway = bool(data.get("is_giveway", False))
    return msg


def _tracked_target(data: dict[str, Any]) -> TrackedTarget:
    msg = TrackedTarget()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.target_id = int(data.get("target_id", 0))
    msg.position = _geo_point(data.get("position", {}))
    msg.sog_kn = float(data.get("sog_kn", 0.0))
    msg.cog_deg = float(data.get("cog_deg", 0.0))
    msg.heading_deg = float(data.get("heading_deg", 0.0))
    msg.covariance = [float(v) for v in data.get("covariance", [0.0] * 9)]
    msg.classification = str(data.get("classification", "unknown"))
    msg.classification_confidence = float(data.get("classification_confidence", 0.0))
    msg.cpa_m = float(data.get("cpa_m", -1.0))
    msg.tcpa_s = float(data.get("tcpa_s", -1.0))
    msg.encounter = _encounter(data.get("encounter", {}))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    msg.source_sensor = str(data.get("source_sensor", "fused"))
    msg.cpa_covariance_m2 = float(data.get("cpa_covariance_m2", -1.0))
    msg.tcpa_covariance_s2 = float(data.get("tcpa_covariance_s2", -1.0))
    msg.intent_confidence = float(data.get("intent_confidence", 0.0))
    msg.brg_deg = float(data.get("brg_deg", 0.0))
    msg.rng_m = float(data.get("rng_m", 0.0))
    return msg


def _tracked_target_array(data: dict[str, Any]) -> TrackedTargetArray:
    msg = TrackedTargetArray()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.targets = [_tracked_target(item) for item in data.get("targets", [])]
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    return msg


def _ownship(data: dict[str, Any]) -> FilteredOwnShipState:
    msg = FilteredOwnShipState()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.position = _geo_point(data.get("position", {}))
    msg.sog_kn = float(data.get("sog_kn", 0.0))
    msg.cog_deg = float(data.get("cog_deg", 0.0))
    msg.heading_deg = float(data.get("heading_deg", 0.0))
    msg.u_water = float(data.get("u_water", 0.0))
    msg.v_water = float(data.get("v_water", 0.0))
    msg.r_dot_deg_s = float(data.get("r_dot_deg_s", 0.0))
    msg.current_speed_kn = float(data.get("current_speed_kn", 0.0))
    msg.current_direction_deg = float(data.get("current_direction_deg", 0.0))
    msg.roll_deg = float(data.get("roll_deg", 0.0))
    msg.pitch_deg = float(data.get("pitch_deg", 0.0))
    msg.covariance = [float(v) for v in data.get("covariance", [0.0] * 36)]
    msg.nav_mode = str(data.get("nav_mode", "DEGRADED"))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    return msg


def _environment(data: dict[str, Any]) -> EnvironmentState:
    msg = EnvironmentState()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.wind_speed_kn = float(data.get("wind_speed_kn", 0.0))
    msg.wind_direction_deg = float(data.get("wind_direction_deg", 0.0))
    msg.wave_height_m = float(data.get("wave_height_m", 0.0))
    msg.wave_direction_deg = float(data.get("wave_direction_deg", 0.0))
    msg.current_speed_kn = float(data.get("current_speed_kn", 0.0))
    msg.current_direction_deg = float(data.get("current_direction_deg", 0.0))
    msg.visibility_range_nm = float(data.get("visibility_range_nm", 0.0))
    msg.weather_source = str(data.get("weather_source", "sensor"))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    return msg


def _geo_path(data: dict[str, Any]) -> GeoPath:
    msg = GeoPath()
    header = data.get("header", {})
    msg.header.stamp = _time(header.get("stamp", {}))
    msg.header.frame_id = str(header.get("frame_id", "WGS84"))
    for item in data.get("poses", []):
        pose = GeoPoseStamped()
        pose.header = msg.header
        pose.pose.position = _geo_point(item.get("position", {}))
        pose.pose.orientation.w = 1.0
        msg.poses.append(pose)
    return msg


def _planned_route(data: dict[str, Any]) -> PlannedRoute:
    msg = PlannedRoute()
    msg.schema_version = int(data.get("schema_version", 112))
    msg.stamp = _time(data.get("stamp", {}))
    msg.route_id = int(data.get("route_id", 0))
    msg.route = _geo_path(data.get("route", {}))
    msg.total_distance_nm = float(data.get("total_distance_nm", 0.0))
    msg.estimated_duration_s = float(data.get("estimated_duration_s", 0.0))
    msg.speed_profile_kn = [float(v) for v in data.get("speed_profile_kn", [])]
    msg.safety_zone = str(data.get("safety_zone", "external_adapter_corridor"))
    msg.confidence = float(data.get("confidence", 0.0))
    msg.rationale = str(data.get("rationale", "external adapter"))
    return msg


class TdlIngressNode(Node):
    def __init__(self) -> None:
        super().__init__("external_tdl_ingress")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8765)
        self._host = str(self.get_parameter("host").value)
        self._port = int(self.get_parameter("port").value)
        self._targets_pub = self.create_publisher(TrackedTargetArray, "/fusion/tracked_targets", _LATCHED)
        self._ownship_pub = self.create_publisher(FilteredOwnShipState, "/fusion/own_ship_state", _LATCHED)
        self._environment_pub = self.create_publisher(EnvironmentState, "/fusion/environment_state", _LATCHED)
        self._route_pub = self.create_publisher(PlannedRoute, "/l2/planned_route", _LATCHED)
        self._server = self._make_server()
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        self.get_logger().info(f"external_tdl_ingress listening on {self._host}:{self._port}")

    def _make_server(self) -> socketserver.ThreadingTCPServer:
        node = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self) -> None:
                for line in self.rfile:
                    payload = decode_line(line)
                    node._handle_payload(payload)

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True

        return Server((self._host, self._port), Handler)

    def _handle_payload(self, payload: dict[str, Any]) -> None:
        kind = payload["kind"]
        if kind == "targets":
            self._targets_pub.publish(_tracked_target_array(payload))
        elif kind == "ownship":
            self._ownship_pub.publish(_ownship(payload))
        elif kind == "environment":
            self._environment_pub.publish(_environment(payload))
        elif kind == "route_in":
            self._route_pub.publish(_planned_route(payload))
        else:
            self.get_logger().warn(f"unsupported inbound payload kind={kind}")
            return
        self.get_logger().info(f"published canonical payload kind={kind}")

    def destroy_node(self) -> bool:
        self._server.shutdown()
        self._server.server_close()
        return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = TdlIngressNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

- [ ] **Step 5: Run IPC tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_ipc.py -q
```

Expected:

```text
3 passed
```

- [ ] **Step 6: Build package in ROS shell**

Run:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select external_adapters
```

Expected:

```text
Summary: 1 package finished
```

- [ ] **Step 7: Commit**

```bash
git add src/sim_workbench/external_adapters/external_adapters/ipc.py src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py tests/sim_workbench/external_adapters/test_ipc.py
git commit -m "feat: add neutral IPC for external adapters"
```

---

### Task 5: Route-Out Adapter

**Files:**
- Create: `src/sim_workbench/external_adapters/external_adapters/route_out_tdl_node.py`
- Create: `src/sim_workbench/external_adapters/external_adapters/route_out_external_path_node.py`
- Test: `tests/sim_workbench/external_adapters/test_route_out.py`

- [ ] **Step 1: Write failing route-out tests**

Create `tests/sim_workbench/external_adapters/test_route_out.py`:

```python
from types import SimpleNamespace

from external_adapters.route_out_external_path_node import path_payload_to_plain_path


def test_path_payload_to_plain_path_keeps_wgs84_and_speeds():
    payload = {
        "kind": "route_out_path",
        "stamp": {"sec": 11, "nanosec": 2},
        "points": [
            {"lat": -1.5, "lon": 105.12, "speed_kn": 9.0},
            {"lat": -1.4, "lon": 105.22, "speed_kn": 10.0},
        ],
    }

    plain = path_payload_to_plain_path(payload)

    assert plain.header.stamp.sec == 11
    assert plain.header.frame_id == "WGS84"
    assert len(plain.poses) == 2
    assert plain.poses[0].pose.position.x == 105.12
    assert plain.poses[0].pose.position.y == -1.5
    assert plain.poses[0].pose.position.z == 9.0
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_route_out.py -q
```

Expected:

```text
ModuleNotFoundError: No module named 'external_adapters.route_out_external_path_node'
```

- [ ] **Step 3: Add TDL route-out capture node**

Create `src/sim_workbench/external_adapters/external_adapters/route_out_tdl_node.py`:

```python
from __future__ import annotations

import socket

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile

from l3_msgs.msg import AvoidancePlan

from .converters import avoidance_plan_to_path_payload
from .ipc import encode_payload


class RouteOutTdlNode(Node):
    def __init__(self) -> None:
        super().__init__("external_route_out_tdl")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8766)
        self._host = str(self.get_parameter("host").value)
        self._port = int(self.get_parameter("port").value)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self._on_plan, QoSProfile(depth=10))
        self.get_logger().info("external_route_out_tdl subscribed to /l3/m5/avoidance_plan")

    def _on_plan(self, msg: AvoidancePlan) -> None:
        if not msg.waypoints:
            self.get_logger().warn("ignoring empty AvoidancePlan")
            return
        payload = avoidance_plan_to_path_payload(msg)
        with socket.create_connection((self._host, self._port), timeout=1.0) as sock:
            sock.sendall(encode_payload(payload))


def main(args=None) -> None:
    rclpy.init(args=args)
    node = RouteOutTdlNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

- [ ] **Step 4: Add external route path publisher node**

Create `src/sim_workbench/external_adapters/external_adapters/route_out_external_path_node.py`:

```python
from __future__ import annotations

import socketserver
import threading
from types import SimpleNamespace
from typing import Any

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile

from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Path

from .ipc import decode_line


def path_payload_to_plain_path(payload: dict[str, Any]) -> Path:
    path = Path()
    path.header.frame_id = "WGS84"
    stamp = payload["stamp"]
    path.header.stamp.sec = int(stamp["sec"])
    path.header.stamp.nanosec = int(stamp["nanosec"])
    for point in payload["points"]:
        pose = PoseStamped()
        pose.header = path.header
        pose.pose.position.x = float(point["lon"])
        pose.pose.position.y = float(point["lat"])
        pose.pose.position.z = float(point.get("speed_kn", 0.0))
        pose.pose.orientation.w = 1.0
        path.poses.append(pose)
    return path


class RouteOutExternalPathNode(Node):
    def __init__(self) -> None:
        super().__init__("external_route_out_path")
        self.declare_parameter("host", "127.0.0.1")
        self.declare_parameter("port", 8766)
        self._host = str(self.get_parameter("host").value)
        self._port = int(self.get_parameter("port").value)
        self._pub = self.create_publisher(Path, "/ship/waypoints", QoSProfile(depth=5))
        self._server = self._make_server()
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        self.get_logger().info(f"external_route_out_path listening on {self._host}:{self._port}")

    def _make_server(self) -> socketserver.ThreadingTCPServer:
        node = self

        class Handler(socketserver.StreamRequestHandler):
            def handle(self) -> None:
                for line in self.rfile:
                    payload = decode_line(line)
                    if payload["kind"] == "route_out_path":
                        node._pub.publish(path_payload_to_plain_path(payload))

        class Server(socketserver.ThreadingTCPServer):
            allow_reuse_address = True

        return Server((self._host, self._port), Handler)

    def destroy_node(self) -> bool:
        self._server.shutdown()
        self._server.server_close()
        return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = RouteOutExternalPathNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

For the unit test on non-ROS hosts, add fallback simple classes only inside the test with monkeypatch if `geometry_msgs` and `nav_msgs` are missing. Use the same fake-module pattern from `tests/docker/test_route_ingest_node.py`.

- [ ] **Step 5: Run route-out unit tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters PYTHONDONTWRITEBYTECODE=1 pytest tests/sim_workbench/external_adapters/test_route_out.py -q
```

Expected:

```text
1 passed
```

- [ ] **Step 6: ROS build check**

Run:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select external_adapters
```

Expected:

```text
Summary: 1 package finished
```

- [ ] **Step 7: Commit**

```bash
git add src/sim_workbench/external_adapters/external_adapters/route_out_tdl_node.py src/sim_workbench/external_adapters/external_adapters/route_out_external_path_node.py tests/sim_workbench/external_adapters/test_route_out.py
git commit -m "feat: add route output adapter"
```

---

### Task 6: Screen 02 External Integration Panel

**Files:**
- Modify: `web/src/api/silApi.ts`
- Create: `web/src/screens/shared/ExternalIntegrationPanel.tsx`
- Modify: `web/src/screens/SimulationCheck.tsx`
- Test: `web/src/screens/__tests__/SimulationCheck.external.test.tsx`

- [ ] **Step 1: Write failing Screen 02 tests**

Create `web/src/screens/__tests__/SimulationCheck.external.test.tsx`:

```tsx
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';

const mocks = vi.hoisted(() => ({
  gates: [] as any[],
  verdict: null as any,
  streaming: false,
  error: '',
  start: vi.fn(),
  abort: vi.fn(),
  configureLifecycle: vi.fn(),
  activateLifecycle: vi.fn(),
  cleanupLifecycle: vi.fn(),
  profiles: {
    active_profile: 'default',
    profiles: [
      { name: 'default', mode: 'default', external_enabled: false },
      { name: 'a4000_external', mode: 'external', external_enabled: true },
    ],
  },
  status: {
    active_profile: 'default',
    external_enabled: false,
    route_out_enabled: false,
  },
  probe: vi.fn(),
  selectProfile: vi.fn(),
}));

vi.mock('../../hooks/useGateStream', () => ({
  useGateStream: () => ({
    gates: mocks.gates,
    verdict: mocks.verdict,
    streaming: mocks.streaming,
    error: mocks.error,
    start: mocks.start,
    abort: mocks.abort,
  }),
}));

vi.mock('../../hooks/useHotkeys', () => ({ useHotkeys: vi.fn() }));

vi.mock('../../store', () => ({
  useScenarioStore: () => ({ runId: 'run-test' }),
  useTelemetryStore: { getState: () => ({ reset: vi.fn() }) },
  useControlStore: { getState: () => ({ reset: vi.fn() }) },
}));

vi.mock('../../api/silApi', () => ({
  useGetScenarioQuery: () => ({ data: { yaml_content: 'title: test' } }),
  useConfigureLifecycleMutation: () => [mocks.configureLifecycle],
  useActivateLifecycleMutation: () => [mocks.activateLifecycle],
  useCleanupLifecycleMutation: () => [mocks.cleanupLifecycle],
  useSkipPreflightMutation: () => [vi.fn()],
  useListIntegrationProfilesQuery: () => ({ data: mocks.profiles }),
  useGetIntegrationStatusQuery: () => ({ data: mocks.status }),
  useSelectIntegrationProfileMutation: () => [mocks.selectProfile],
  useProbeIntegrationMutation: () => [mocks.probe],
}));

vi.mock('../shared/GateSequencer', () => ({ GateSequencer: () => <div data-testid="gate-sequencer" /> }));
vi.mock('../shared/DiagnosticCanvas', () => ({ DiagnosticCanvas: () => <div data-testid="diagnostic-canvas" /> }));
vi.mock('../shared/ActionLogs', () => ({ ActionLogs: () => <div data-testid="action-logs" /> }));

import { SimulationCheck } from '../SimulationCheck';

describe('SimulationCheck external integration panel', () => {
  beforeEach(() => {
    window.location.hash = '#/check/safe_route';
    mocks.probe.mockReset();
    mocks.selectProfile.mockReset();
    mocks.configureLifecycle.mockReset();
    mocks.activateLifecycle.mockReset();
    mocks.cleanupLifecycle.mockReset();
    mocks.verdict = null;
    mocks.status = {
      active_profile: 'default',
      external_enabled: false,
      route_out_enabled: false,
    };
  });

  it('renders external integration panel on Screen 02', () => {
    render(<SimulationCheck />);

    expect(screen.getByTestId('external-integration-panel')).toBeInTheDocument();
    expect(screen.getByText('External Integration')).toBeInTheDocument();
    expect(screen.getByText('default')).toBeInTheDocument();
  });

  it('selects a4000 profile from Screen 02', async () => {
    mocks.selectProfile.mockReturnValue({ unwrap: () => Promise.resolve({ active_profile: 'a4000_external' }) });

    render(<SimulationCheck />);
    fireEvent.change(screen.getByTestId('integration-profile-select'), { target: { value: 'a4000_external' } });

    await waitFor(() => {
      expect(mocks.selectProfile).toHaveBeenCalledWith({ name: 'a4000_external' });
    });
  });

  it('runs integration probe from panel button', async () => {
    mocks.probe.mockReturnValue({ unwrap: () => Promise.resolve({ all_clear: true, checks: [] }) });

    render(<SimulationCheck />);
    fireEvent.click(screen.getByTestId('integration-probe-button'));

    await waitFor(() => expect(mocks.probe).toHaveBeenCalled());
  });

  it('blocks lifecycle activation when external gate probe fails', async () => {
    vi.useFakeTimers();
    mocks.verdict = 'GO';
    mocks.status = {
      active_profile: 'a4000_external',
      external_enabled: true,
      route_out_enabled: true,
    };
    mocks.probe.mockReturnValue({
      unwrap: () => Promise.resolve({
        profile_name: 'a4000_external',
        all_clear: false,
        checks: [{ gate_id: 108, label: 'Low-level control forbidden', passed: false, detail: 'failed' }],
      }),
    });

    render(<SimulationCheck />);
    vi.advanceTimersByTime(3000);

    await waitFor(() => {
      expect(mocks.probe).toHaveBeenCalled();
      expect(mocks.configureLifecycle).not.toHaveBeenCalled();
      expect(screen.getByText(/External integration gate failed/)).toBeInTheDocument();
    });
    vi.useRealTimers();
  });
});
```

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationCheck.external.test.tsx
```

Expected:

```text
No "useListIntegrationProfilesQuery" export is defined
```

- [ ] **Step 3: Add API DTOs and endpoints**

Modify `web/src/api/silApi.ts` before `export const silApi = createApi`:

```ts
export interface IntegrationProfileSummary {
  name: string;
  mode: 'default' | 'external' | 'hybrid_debug';
  tdl_domain_id?: number;
  external_enabled: boolean;
  adapters?: Record<string, 'enabled' | 'disabled'>;
  external_domains?: Record<string, {
    domain_id: number;
    workspace_setup?: string | null;
    required_topics?: Record<string, string>;
  }>;
}

export interface IntegrationProfilesResult {
  active_profile: string;
  profiles: IntegrationProfileSummary[];
}

export interface IntegrationStatus {
  active_profile: string;
  external_enabled: boolean;
  route_out_enabled: boolean;
}

export interface IntegrationProbeCheck {
  gate_id: number;
  label: string;
  passed: boolean;
  detail: string;
}

export interface IntegrationProbeResult {
  profile_name: string;
  all_clear: boolean;
  checks: IntegrationProbeCheck[];
}
```

Add endpoints inside `endpoints: (builder) => ({`:

```ts
    listIntegrationProfiles: builder.query<IntegrationProfilesResult, void>({
      query: () => '/integration/profiles',
    }),

    getIntegrationStatus: builder.query<IntegrationStatus, void>({
      query: () => '/integration/status',
    }),

    selectIntegrationProfile: builder.mutation<{ active_profile: string }, { name: string }>({
      query: (body) => ({
        url: '/integration/profile',
        method: 'POST',
        body,
      }),
    }),

    probeIntegration: builder.mutation<IntegrationProbeResult, void>({
      query: () => ({ url: '/integration/probe', method: 'POST' }),
    }),
```

Add generated hooks to export list:

```ts
  useListIntegrationProfilesQuery,
  useGetIntegrationStatusQuery,
  useSelectIntegrationProfileMutation,
  useProbeIntegrationMutation,
```

- [ ] **Step 4: Add `ExternalIntegrationPanel`**

Create `web/src/screens/shared/ExternalIntegrationPanel.tsx`:

```tsx
import { useState } from 'react';
import {
  useGetIntegrationStatusQuery,
  useListIntegrationProfilesQuery,
  useProbeIntegrationMutation,
  useSelectIntegrationProfileMutation,
  type IntegrationProbeResult,
} from '../../api/silApi';

export function ExternalIntegrationPanel() {
  const { data: profilesData } = useListIntegrationProfilesQuery();
  const { data: status } = useGetIntegrationStatusQuery();
  const [selectProfile] = useSelectIntegrationProfileMutation();
  const [probeIntegration] = useProbeIntegrationMutation();
  const [probe, setProbe] = useState<IntegrationProbeResult | null>(null);
  const active = status?.active_profile ?? profilesData?.active_profile ?? 'default';

  const runProbe = async () => {
    const result = await probeIntegration().unwrap();
    setProbe(result);
  };

  return (
    <section
      data-testid="external-integration-panel"
      style={{
        borderTop: '1px solid var(--line-1)',
        padding: '12px',
        fontFamily: 'var(--f-body)',
        color: 'var(--txt-1)',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
        <h3 style={{ margin: 0, fontSize: 13, letterSpacing: 0 }}>External Integration</h3>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: status?.external_enabled ? 'var(--c-warn)' : 'var(--txt-3)' }}>
          {active}
        </span>
      </div>

      <select
        data-testid="integration-profile-select"
        value={active}
        onChange={(event) => selectProfile({ name: event.target.value })}
        style={{ width: '100%', marginTop: 10, height: 30, background: 'var(--bg-1)', color: 'var(--txt-1)', border: '1px solid var(--line-1)', borderRadius: 4 }}
      >
        {(profilesData?.profiles ?? []).map((profile) => (
          <option key={profile.name} value={profile.name}>{profile.name}</option>
        ))}
      </select>

      <button
        data-testid="integration-probe-button"
        type="button"
        onClick={runProbe}
        style={{ width: '100%', marginTop: 10, height: 32, background: 'var(--bg-2)', color: 'var(--txt-1)', border: '1px solid var(--line-1)', borderRadius: 4 }}
      >
        Probe External Gates
      </button>

      <div style={{ marginTop: 10, display: 'grid', gap: 6 }}>
        {(probe?.checks ?? []).map((check) => (
          <div key={`${check.gate_id}-${check.label}`} style={{ display: 'grid', gridTemplateColumns: '18px 1fr', gap: 6, fontSize: 11 }}>
            <span style={{ color: check.passed ? 'var(--c-stbd)' : 'var(--c-danger)' }}>
              {check.passed ? 'OK' : 'NO'}
            </span>
            <span title={check.detail}>{check.label}</span>
          </div>
        ))}
      </div>
    </section>
  );
}
```

- [ ] **Step 5: Mount panel in Screen 02 right rail**

Modify `web/src/screens/SimulationCheck.tsx` imports. Keep the existing `../api/silApi` import and add the two hooks to that import list:

```ts
import { ExternalIntegrationPanel } from './shared/ExternalIntegrationPanel';
import {
  useGetScenarioQuery,
  useConfigureLifecycleMutation,
  useActivateLifecycleMutation,
  useCleanupLifecycleMutation,
  useSkipPreflightMutation,
  useGetIntegrationStatusQuery,
  useProbeIntegrationMutation,
} from '../api/silApi';
```

Add hooks near the other API hooks:

```tsx
  const { data: integrationStatus } = useGetIntegrationStatusQuery();
  const [probeIntegration] = useProbeIntegrationMutation();
```

Add external gate check at the start of `handleProceed`, before `setTransitioning(true)`:

```tsx
    if (integrationStatus?.external_enabled) {
      try {
        const integrationProbe = await probeIntegration().unwrap();
        if (!integrationProbe.all_clear) {
          const failed = integrationProbe.checks.find((check) => !check.passed);
          setLifecycleError(`External integration gate failed: ${failed?.label ?? 'unknown'}`);
          setTransitioning(false);
          return;
        }
      } catch (e) {
        setLifecycleError(`External integration probe failed: ${e instanceof Error ? e.message : String(e)}`);
        setTransitioning(false);
        return;
      }
    }
```

Add `integrationStatus?.external_enabled` and `probeIntegration` to the `handleProceed` dependency array:

```tsx
  }, [scenarioId, cleanupLifecycle, configureLifecycle, activateLifecycle, integrationStatus?.external_enabled, probeIntegration]);
```

Replace right rail block:

```tsx
      <ActionLogs focusedGateId={focusedGateId} gates={gates}
        scenarioId={scenarioId} runId={runId ?? 'unknown'}
        onRerun={start} onAbort={handleAbort} onFixApplied={() => {}}
        isDev={IS_DEV && verdict === 'NO-GO'}
        devSkipReason={devSkipReason}
        onDevSkipReasonChange={setDevSkipReason}
        onDevSkip={handleDevSkip} />
```

with:

```tsx
      <div style={{ minHeight: 0, overflow: 'auto', borderLeft: '1px solid var(--line-1)' }}>
        <ActionLogs focusedGateId={focusedGateId} gates={gates}
          scenarioId={scenarioId} runId={runId ?? 'unknown'}
          onRerun={start} onAbort={handleAbort} onFixApplied={() => {}}
          isDev={IS_DEV && verdict === 'NO-GO'}
          devSkipReason={devSkipReason}
          onDevSkipReasonChange={setDevSkipReason}
          onDevSkip={handleDevSkip} />
        <ExternalIntegrationPanel />
      </div>
```

- [ ] **Step 6: Run Screen 02 test**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationCheck.external.test.tsx
```

Expected:

```text
3 passed
```

- [ ] **Step 7: Run frontend regression slice**

Run:

```bash
cd web && npm test -- --run src/screens/__tests__/SimulationScenario.test.tsx src/screens/__tests__/SimulationMonitor.test.tsx
```

Expected:

```text
passed
```

- [ ] **Step 8: Commit**

```bash
git add web/src/api/silApi.ts web/src/screens/SimulationCheck.tsx web/src/screens/shared/ExternalIntegrationPanel.tsx web/src/screens/__tests__/SimulationCheck.external.test.tsx
git commit -m "feat: add Screen 02 external integration panel"
```

---

### Task 7: Profile-Based Launch Wiring and Local A4000 Container Gate

**Files:**
- Create: `scripts/integration/start_external_adapters.sh`
- Create: `scripts/local-a4000-env.sh`
- Create: `scripts/local-a4000-acceptance.sh`
- Modify: `docker/sil_entrypoint.sh`
- Test: `tests/scripts/test_start_external_adapters.py`

- [ ] **Step 1: Write failing script tests**

Create `tests/scripts/test_start_external_adapters.py`:

```python
import subprocess
from pathlib import Path


SCRIPT = Path("scripts/integration/start_external_adapters.sh")


def test_default_profile_prints_disabled():
    result = subprocess.run(
        ["bash", str(SCRIPT)],
        env={"TDL_INTEGRATION_PROFILE": "default"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "external adapters disabled for profile=default" in result.stdout


def test_external_profile_prints_expected_process_names():
    result = subprocess.run(
        ["bash", str(SCRIPT), "--dry-run"],
        env={"TDL_INTEGRATION_PROFILE": "a4000_external"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "external_tdl_ingress" in result.stdout
    assert "external_route_out_tdl" in result.stdout
    assert "external_route_out_path" in result.stdout


def test_local_a4000_env_reuses_a4000_compose_override():
    result = subprocess.run(
        ["bash", "-lc", "source scripts/local-a4000-env.sh && printf '%s %s %s' \"$COMPOSE_FILE\" \"$ORCH_PORT\" \"$FOX_PORT\""],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert result.stdout == "docker-compose.yml:docker-compose.a4000.yml 18000 18765"


def test_local_a4000_acceptance_dry_run_prints_gate_order():
    result = subprocess.run(
        ["bash", "scripts/local-a4000-acceptance.sh", "--dry-run"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "compose=docker-compose.yml:docker-compose.a4000.yml" in result.stdout
    assert "health=https://127.0.0.1:18000/api/v1/health" in result.stdout
    assert "integration=/api/v1/integration/profiles" in result.stdout
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/scripts/test_start_external_adapters.py -q
```

Expected:

```text
No such file or directory: 'scripts/integration/start_external_adapters.sh'
```

- [ ] **Step 3: Add launcher script**

Create `scripts/integration/start_external_adapters.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

if [[ "$PROFILE" == "default" ]]; then
  echo "external adapters disabled for profile=default"
  exit 0
fi

if [[ "$PROFILE" != "a4000_external" && "$PROFILE" != "hybrid_debug" ]]; then
  echo "unsupported integration profile=$PROFILE" >&2
  exit 2
fi

COMMANDS=(
  "ros2 run external_adapters external_tdl_ingress --ros-args -p port:=8765"
  "ros2 run external_adapters external_route_out_tdl --ros-args -p port:=8766"
  "ros2 run external_adapters external_route_out_path --ros-args -p port:=8766"
)

for cmd in "${COMMANDS[@]}"; do
  echo "$cmd"
  if [[ "$DRY_RUN" == "0" ]]; then
    bash -lc "$cmd" &
  fi
done

if [[ "$DRY_RUN" == "0" ]]; then
  wait
fi
```

Run:

```bash
chmod +x scripts/integration/start_external_adapters.sh
```

- [ ] **Step 4: Add local A4000 env script**

Create `scripts/local-a4000-env.sh`:

```bash
#!/usr/bin/env bash
# Source before local OrbStack A4000-equivalent verification.
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml
export ORCH_URL="${ORCH_URL:-https://127.0.0.1:18000}"
export ORCH_PORT="${ORCH_PORT:-18000}"
export FOX_PORT="${FOX_PORT:-18765}"
export TDL_INTEGRATION_PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
```

Run:

```bash
chmod +x scripts/local-a4000-env.sh
```

- [ ] **Step 5: Add local A4000 acceptance script**

Create `scripts/local-a4000-acceptance.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

DRY_RUN=0
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
fi

source scripts/local-a4000-env.sh

if [[ "$DRY_RUN" == "1" ]]; then
  echo "compose=$COMPOSE_FILE"
  echo "health=${ORCH_URL}/api/v1/health"
  echo "integration=/api/v1/integration/profiles"
  exit 0
fi

command -v docker >/dev/null
docker compose config -q
docker compose up -d --build sil-orchestrator sil-nodes foxglove-bridge

for _ in $(seq 1 60); do
  if curl -sk --max-time 2 "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'; then
    break
  fi
  sleep 2
done

curl -sk --fail "${ORCH_URL}/api/v1/health" | grep -q '"status":"ok"'
curl -sk --fail "${ORCH_URL}/api/v1/integration/profiles" | grep -q '"active_profile"'

mkdir -p runs
curl -sk --fail -X POST "${ORCH_URL}/api/v1/integration/probe" \
  | tee "runs/local_a4000_container_probe_$(date +%Y%m%d_%H%M%S).json"

docker compose exec -T sil-nodes bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && test "$ROS_DOMAIN_ID" = "42" && ros2 topic list >/tmp/local_a4000_topics.txt'

echo "LOCAL A4000 CONTAINER ACCEPTANCE PASS"
```

Run:

```bash
chmod +x scripts/local-a4000-acceptance.sh
```

- [ ] **Step 6: Add entrypoint hook gated by env**

Modify `docker/sil_entrypoint.sh` after ROS environment setup and before long-running foreground wait:

```bash
if [[ "${TDL_INTEGRATION_PROFILE:-default}" != "default" ]]; then
  /workspace/scripts/integration/start_external_adapters.sh &
fi
```

Keep this block gated. Do not start external adapters when `TDL_INTEGRATION_PROFILE` is unset.

- [ ] **Step 7: Run script tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/scripts/test_start_external_adapters.py -q
```

Expected:

```text
4 passed
```

- [ ] **Step 8: Commit**

```bash
git add scripts/integration/start_external_adapters.sh scripts/local-a4000-env.sh scripts/local-a4000-acceptance.sh docker/sil_entrypoint.sh tests/scripts/test_start_external_adapters.py
git commit -m "feat: add local A4000 container gate"
```

---

### Task 8: Local OrbStack and A4000 Integration Verification

**Files:**
- Create: `docs/Design/SIL/external-module-adapter-runbook.md`
- Evidence output: `runs/a4000_external_adapter_probe_<timestamp>.json`

- [ ] **Step 1: Write runbook**

Create `docs/Design/SIL/external-module-adapter-runbook.md`:

````markdown
# External Module Adapter Runbook

## Local default regression

```bash
PYTHONDONTWRITEBYTECODE=1 pytest \
  tests/sil_orchestrator/test_integration_profiles.py \
  tests/sil_orchestrator/test_integration_routes.py \
  tests/sim_workbench/external_adapters/test_converters.py \
  tests/sim_workbench/external_adapters/test_ipc.py \
  tests/sim_workbench/external_adapters/test_route_out.py \
  tests/scripts/test_start_external_adapters.py \
  -q
```

Expected: all tests pass.

## Frontend regression

```bash
cd web
npm test -- --run src/screens/__tests__/SimulationCheck.external.test.tsx
npm run build
```

Expected: tests pass and build succeeds.

## ROS build

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select external_adapters
```

Expected: `Summary: 1 package finished`.

## Local OrbStack A4000-equivalent gate

Run this before syncing to A4000:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

Expected:

- Uses `docker-compose.yml:docker-compose.a4000.yml`.
- Health endpoint responds on `https://127.0.0.1:18000`.
- `/api/v1/integration/profiles` responds.
- Probe evidence is written under `runs/local_a4000_container_probe_*.json`.
- `sil-nodes` has `ROS_DOMAIN_ID=42`.

## A4000 deploy discipline

Use patch/scp only for touched files. Do not run `git pull`, `git reset`, or broad repository sync on A4000.

A4000 sync is allowed only after the local OrbStack A4000-equivalent gate passes.

## A4000 probe

```bash
curl -s -X POST http://127.0.0.1:8000/api/v1/integration/profile \
  -H 'Content-Type: application/json' \
  -d '{"name":"a4000_external"}'

curl -s -X POST http://127.0.0.1:8000/api/v1/integration/probe \
  | tee runs/a4000_external_adapter_probe_$(date +%Y%m%d_%H%M%S).json
```

Expected:

- `profile_name` is `a4000_external`.
- Profile gate passes.
- Low-level control forbidden gate passes.
- Topic gates pass when external modules are running in configured domains.
````

- [ ] **Step 2: Run local backend/unit tests**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest \
  tests/sil_orchestrator/test_integration_profiles.py \
  tests/sil_orchestrator/test_integration_routes.py \
  tests/sim_workbench/external_adapters/test_converters.py \
  tests/sim_workbench/external_adapters/test_ipc.py \
  tests/sim_workbench/external_adapters/test_route_out.py \
  tests/scripts/test_start_external_adapters.py \
  -q
```

Expected:

```text
passed
```

- [ ] **Step 3: Run frontend tests and build**

Run:

```bash
cd web
npm test -- --run src/screens/__tests__/SimulationCheck.external.test.tsx
npm run build
```

Expected:

```text
passed
built
```

- [ ] **Step 4: Run ROS package build**

Run:

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select external_adapters
```

Expected:

```text
Summary: 1 package finished
```

- [ ] **Step 5: A4000 narrow deploy**

Only after `./scripts/local-a4000-acceptance.sh` passes, copy changed files from local repo root:

```bash
rsync -av \
  config/integration_profiles \
  src/sil_orchestrator/integration \
  src/sim_workbench/external_adapters \
  scripts/integration/start_external_adapters.sh \
  scripts/local-a4000-env.sh \
  scripts/local-a4000-acceptance.sh \
  docs/Design/SIL/external-module-adapter-spec.md \
  docs/Design/SIL/external-module-adapter-runbook.md \
  mass@192.168.121.50:/home/mass/MASS-L3-Tactical-Layer/
```

Expected:

```text
sent
```

If A4000 target repo path differs, run `ssh mass@192.168.121.50 'pwd; ls /home/mass'` and adjust destination to the existing TDL checkout. Do not create a second checkout.

- [ ] **Step 6: A4000 profile probe**

On A4000 TDL host:

```bash
cd /home/mass/MASS-L3-Tactical-Layer
curl -s -X POST http://127.0.0.1:8000/api/v1/integration/profile \
  -H 'Content-Type: application/json' \
  -d '{"name":"a4000_external"}'
curl -s -X POST http://127.0.0.1:8000/api/v1/integration/probe \
  | tee runs/a4000_external_adapter_probe_$(date +%Y%m%d_%H%M%S).json
```

Expected JSON fields:

```json
{
  "profile_name": "a4000_external",
  "all_clear": true
}
```

If `all_clear` is false, keep the JSON evidence and fix only the failing gate.

- [ ] **Step 7: Commit runbook**

```bash
git add docs/Design/SIL/external-module-adapter-runbook.md
git commit -m "docs: add external adapter integration runbook"
```

---

## Self-Review

Spec coverage:

- Default TDL behavior preserved: Task 1 default profile, Task 7 gated launcher.
- External profiles: Task 1 profile YAML and loader.
- Screen 02 integration entry: Task 6.
- Cross-domain isolation: Task 3 neutral payload, Task 4 IPC, Task 5 route-out split.
- No low-level control: Task 2 forbidden gate and Task 5 route/waypoint output only.
- Local OrbStack before A4000 sync: Task 7 local scripts and Task 8 runbook gate.
- A4000 dirty-worktree protection: Task 8 runbook and narrow deploy.
- Existing bridge protection: file structure says no `docker/sil_topic_bridge.py` production logic.

Forbidden phrase scan: clear.

Type consistency:

- Backend profile names: `default`, `a4000_external`, `hybrid_debug`.
- API hooks match RTK endpoint names.
- IPC payload kind names match `KNOWN_KINDS`.
- Route-out port defaults use `8766`; ingress port uses `8765`.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-12-external-module-adapters.md`.

Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
