# L2 External Plugin Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `plugin-route-l2-main` as a real L2 backend plugin that publishes `/route_planning/route_plan` at SIL `ACTIVE`, then adapts the complete route into TDL `/l2/planned_route`.

**Architecture:** Keep the external L2 ROS2 workspace private to the plugin under `plugins/l2_external/ros2_ws/` so its `ship_interfaces` package does not collide with TDL `src/ship_interfaces`. Add TDL-owned Python adaptor code in `external_adapters`: one node writes L2 `gnc_bridge_route.json` only after `/sil/lifecycle_status` reaches `ACTIVE`; one node converts external `ship_interfaces/msg/RoutePlan` into newline JSON for `external_tdl_ingress` on `127.0.0.1:8765`.

**Tech Stack:** ROS2 Humble, rclpy, Docker Compose, pytest, PyYAML, `external_adapters` IPC JSON, local OrbStack A4000-equivalent gate.

---

## Source References

- Design spec: `docs/superpowers/specs/2026-06-15-l2-external-plugin-integration-design.md`
- TDL ingress route path: `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py:48-83`, `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py:252-264`
- Canonical route payload builder: `src/sim_workbench/external_adapters/external_adapters/converters.py:126-167`
- Lifecycle active transition: `src/sil_orchestrator/lifecycle_bridge.py:435-457`
- Current plugin manifest: `config/runtime_plugins/l2-planner-main.yaml`
- Current plugin compose stub: `docker-compose.plugins.yml`
- External L2 source snapshot: `/Users/marine/Code/_a4000_snapshots/mass-simulation/船舶动力学/gnc_ws/src/route_planning_ros2`
- External L2 message snapshot: `/Users/marine/Code/_a4000_snapshots/mass-simulation/船舶动力学/gnc_ws/src/platform/ship_interfaces/msg/RoutePlan.msg`
- Integration route source: `scenarios/集成测试/safe_route.yaml`

## File Structure

- Create: `plugins/l2_external/ros2_ws/src/route_planning_ros2/**`
  - Vendored L2 route planning backend copied from A4000 snapshot.
- Create: `plugins/l2_external/ros2_ws/src/platform/ship_interfaces/**`
  - Plugin-private external message package used only by the L2 plugin image.
- Create: `src/sim_workbench/external_adapters/external_adapters/l2_route_plan_adaptor.py`
  - Validates external `RoutePlan`, converts to `route_in`, gates forwarding on lifecycle `ACTIVE`, sends IPC to `external_tdl_ingress`.
- Create: `src/sim_workbench/external_adapters/external_adapters/l2_route_seed.py`
  - Converts scenario `ownShip.nominalRoute` into `gnc_bridge_route.json`, writes it atomically on lifecycle `ACTIVE`.
- Modify: `src/sim_workbench/external_adapters/setup.py`
  - Adds console scripts for the two L2 helper nodes.
- Modify: `src/sim_workbench/external_adapters/package.xml`
  - Adds runtime dependencies used by L2 helper nodes.
- Create: `docker/l2_external_plugin.Dockerfile`
  - Builds plugin-private ROS workspace and packages the TDL-owned helper scripts.
- Create: `plugins/l2_external/entrypoint.sh`
  - Starts route seed node, external L2 `gnc_sim_node`, and route adaptor in one plugin container.
- Modify: `docker-compose.plugins.yml`
  - Replaces idle `plugin-route-l2-main` stub with local image build.
- Modify: `config/runtime_plugins/l2-planner-main.yaml`
  - Aligns plugin `ros.domain_id` with local/A4000 integration domain 42.
- Create: `scripts/integration/probe_l2_external_plugin.sh`
  - Collects local topic evidence for `/route_planning/route_plan` and `/l2/planned_route`.
- Modify: `tests/sim_workbench/external_adapters/test_entrypoints.py`
  - Confirms new modules are importable.
- Create: `tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py`
  - Unit coverage for validation, conversion, route ID hashing, duplicate suppression.
- Create: `tests/sim_workbench/external_adapters/test_l2_route_seed.py`
  - Unit coverage for scenario-to-bridge-route conversion and atomic write.
- Modify: `tests/scripts/test_runtime_plugin_compose.py`
  - Static tests for L2 plugin compose, manifest, forbidden topics, and probe script.

---

### Task 1: Vendor External L2 Backend Into Plugin-Private Workspace

**Files:**
- Create: `plugins/l2_external/ros2_ws/src/route_planning_ros2/**`
- Create: `plugins/l2_external/ros2_ws/src/platform/ship_interfaces/**`

- [ ] **Step 1: Copy the A4000 L2 backend subset**

Run from repo root:

```bash
mkdir -p plugins/l2_external/ros2_ws/src/platform
rsync -a \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.bak*' \
  --exclude '*_bak' \
  --exclude 'build/' \
  --exclude 'install/' \
  --exclude 'log/' \
  '/Users/marine/Code/_a4000_snapshots/mass-simulation/船舶动力学/gnc_ws/src/route_planning_ros2/' \
  'plugins/l2_external/ros2_ws/src/route_planning_ros2/'
rsync -a \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.bak*' \
  --exclude '*_bak' \
  --exclude 'build/' \
  --exclude 'install/' \
  --exclude 'log/' \
  '/Users/marine/Code/_a4000_snapshots/mass-simulation/船舶动力学/gnc_ws/src/platform/ship_interfaces/' \
  'plugins/l2_external/ros2_ws/src/platform/ship_interfaces/'
```

- [ ] **Step 2: Normalize vendored package metadata**

Edit `plugins/l2_external/ros2_ws/src/platform/ship_interfaces/package.xml` so the description and license are explicit:

```xml
  <description>Plugin-private external ship interfaces for MASS L2 route planning.</description>
  <license>Proprietary</license>
```

- [ ] **Step 3: Verify the copied boundary is narrow**

Run:

```bash
test -f plugins/l2_external/ros2_ws/src/route_planning_ros2/route_planning_ros2/gnc_sim_node.py
test -f plugins/l2_external/ros2_ws/src/route_planning_ros2/setup.py
test -f plugins/l2_external/ros2_ws/src/platform/ship_interfaces/msg/RoutePlan.msg
test -f plugins/l2_external/ros2_ws/src/platform/ship_interfaces/msg/GeoPosition.msg
! find plugins/l2_external -type f \( -name '*.bag' -o -name '*.mcap' -o -name '*.db3' -o -path '*/UI/*' -o -path '*/route_output/*' \) | grep .
```

Expected: all `test -f` commands pass, and the final command prints nothing.

- [ ] **Step 4: Commit vendored backend subset**

```bash
git add plugins/l2_external/ros2_ws/src/route_planning_ros2 plugins/l2_external/ros2_ws/src/platform/ship_interfaces
git commit -m "chore: vendor L2 route planner backend subset"
```

---

### Task 2: Write Failing Tests For RoutePlan Adaptor Conversion

**Files:**
- Create: `tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py`

- [ ] **Step 1: Add tests for validation, mapping, route ID hashing, and duplicate suppression**

Create `tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py`:

```python
from types import SimpleNamespace

import pytest

from external_adapters.l2_route_plan_adaptor import (
    RoutePlanValidationError,
    route_plan_signature,
    route_plan_to_payload,
    should_forward_route,
    stable_route_id_from_string,
)


def _route_plan(
    *,
    lats=None,
    lons=None,
    speeds=None,
    modes=None,
    route_id="WH-SZ-001",
    route_type="transit",
    frame_id="map",
):
    stamp = SimpleNamespace(sec=40, nanosec=500)
    header = SimpleNamespace(stamp=stamp, frame_id=frame_id)
    return SimpleNamespace(
        header=header,
        latitude=list(lats if lats is not None else [31.0, 31.1, 31.2]),
        longitude=list(lons if lons is not None else [121.0, 121.1, 121.2]),
        speed_limit_mps=list(speeds if speeds is not None else [5.14444, 0.0, 2.57222]),
        navigation_mode=list(modes if modes is not None else ["cruise", "", "approach"]),
        route_id=route_id,
        route_type=route_type,
    )


@pytest.mark.parametrize(
    ("msg", "match"),
    [
        (_route_plan(lats=[], lons=[]), "at least two waypoints"),
        (_route_plan(lats=[31.0], lons=[121.0]), "at least two waypoints"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0]), "same length"),
        (_route_plan(lats=[31.0, float("nan")], lons=[121.0, 121.1]), "finite"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, float("inf")]), "finite"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], speeds=[1.0]), "speed_limit_mps"),
        (_route_plan(lats=[31.0, 31.1], lons=[121.0, 121.1], modes=["cruise"]), "navigation_mode"),
    ],
)
def test_route_plan_to_payload_rejects_invalid_route(msg, match):
    with pytest.raises(RoutePlanValidationError, match=match):
        route_plan_to_payload(msg)


def test_route_plan_to_payload_maps_external_route_to_tdl_route_in():
    msg = _route_plan()

    payload = route_plan_to_payload(msg, default_speed_kn=10.0)

    assert payload["kind"] == "route_in"
    assert payload["schema_version"] == 112
    assert payload["stamp"] == {"sec": 40, "nanosec": 500}
    assert payload["route_id"] == stable_route_id_from_string("WH-SZ-001")
    assert payload["route"]["header"] == {"stamp": {"sec": 40, "nanosec": 500}, "frame_id": "WGS84"}
    assert [pose["pose"]["position"]["latitude"] for pose in payload["route"]["poses"]] == [31.0, 31.1, 31.2]
    assert [pose["pose"]["position"]["longitude"] for pose in payload["route"]["poses"]] == [121.0, 121.1, 121.2]
    assert payload["speed_profile_kn"] == pytest.approx([10.0, 10.0])
    assert payload["confidence"] == pytest.approx(1.0)
    assert "route_id=WH-SZ-001" in payload["rationale"]
    assert "route_type=transit" in payload["rationale"]
    assert "navigation_modes=cruise,approach" in payload["rationale"]


def test_empty_route_id_uses_waypoint_signature_hash():
    msg = _route_plan(route_id="")

    payload = route_plan_to_payload(msg)

    assert payload["route_id"] > 0
    assert payload["route_id"] != stable_route_id_from_string("WH-SZ-001")


def test_route_plan_signature_and_forward_decision_are_stable():
    first = _route_plan()
    second = _route_plan()
    changed = _route_plan(lons=[121.0, 121.1, 121.25])

    first_signature = route_plan_signature(first)
    assert first_signature == route_plan_signature(second)
    assert first_signature != route_plan_signature(changed)
    assert should_forward_route(None, first_signature)
    assert not should_forward_route(first_signature, first_signature)
    assert should_forward_route(first_signature, route_plan_signature(changed))
```

- [ ] **Step 2: Run tests and verify they fail because the module does not exist**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters pytest tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py -q
```

Expected: FAIL with `ModuleNotFoundError: No module named 'external_adapters.l2_route_plan_adaptor'`.

- [ ] **Step 3: Commit failing tests**

```bash
git add tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py
git commit -m "test: define L2 route plan adaptor contract"
```

---

### Task 3: Implement RoutePlan Adaptor Conversion And Active-Gated Forwarding

**Files:**
- Create: `src/sim_workbench/external_adapters/external_adapters/l2_route_plan_adaptor.py`
- Modify: `src/sim_workbench/external_adapters/setup.py`
- Modify: `src/sim_workbench/external_adapters/package.xml`
- Modify: `tests/sim_workbench/external_adapters/test_entrypoints.py`

- [ ] **Step 1: Add the adaptor module**

Create `src/sim_workbench/external_adapters/external_adapters/l2_route_plan_adaptor.py`:

```python
from __future__ import annotations

import hashlib
import math
import os
import socket
import time
from typing import Any

from external_adapters.converters import route_points_to_planned_route_dict
from external_adapters.ipc import encode_payload
from external_adapters.neutral import NeutralRoutePoint

MPS_PER_KNOT = 0.514444
DEFAULT_SPEED_KN = 10.0
ACTIVE_STATE = 3


try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSDurabilityPolicy, QoSProfile, QoSReliabilityPolicy
    from ship_interfaces.msg import RoutePlan
    from sil_msgs.msg import LifecycleStatus
except ImportError:  # pragma: no cover - unit tests run without ROS2 generated messages.
    rclpy = None
    Node = object
    QoSDurabilityPolicy = None
    QoSProfile = None
    QoSReliabilityPolicy = None
    RoutePlan = None
    LifecycleStatus = None


class RoutePlanValidationError(ValueError):
    pass


def stable_route_id_from_string(route_id: str) -> int:
    digest = hashlib.sha256(route_id.encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big") or 1


def _stamp_from_msg(msg: Any) -> tuple[int, int]:
    stamp = getattr(getattr(msg, "header", None), "stamp", None)
    return int(getattr(stamp, "sec", 0)), int(getattr(stamp, "nanosec", 0))


def _finite_float(value: Any, field_name: str) -> float:
    try:
        numeric = float(value)
    except (TypeError, ValueError) as exc:
        raise RoutePlanValidationError(f"{field_name} must be numeric") from exc
    if not math.isfinite(numeric):
        raise RoutePlanValidationError(f"{field_name} must be finite")
    return numeric


def _route_arrays(msg: Any) -> tuple[list[float], list[float], list[float], list[str]]:
    latitudes = [_finite_float(value, f"latitude[{index}]") for index, value in enumerate(getattr(msg, "latitude", []))]
    longitudes = [_finite_float(value, f"longitude[{index}]") for index, value in enumerate(getattr(msg, "longitude", []))]
    speeds = [_finite_float(value, f"speed_limit_mps[{index}]") for index, value in enumerate(getattr(msg, "speed_limit_mps", []))]
    modes = [str(value) for value in getattr(msg, "navigation_mode", [])]

    if len(latitudes) != len(longitudes):
        raise RoutePlanValidationError("latitude and longitude arrays must have the same length")
    if len(latitudes) < 2:
        raise RoutePlanValidationError("RoutePlan must contain at least two waypoints")
    if speeds and len(speeds) != len(latitudes):
        raise RoutePlanValidationError("speed_limit_mps must be empty or match waypoint count")
    if modes and len(modes) != len(latitudes):
        raise RoutePlanValidationError("navigation_mode must be empty or match waypoint count")
    return latitudes, longitudes, speeds, modes


def route_plan_to_neutral_points(msg: Any, default_speed_kn: float = DEFAULT_SPEED_KN) -> list[NeutralRoutePoint]:
    default_speed = _finite_float(default_speed_kn, "default_speed_kn")
    if default_speed <= 0.0:
        raise RoutePlanValidationError("default_speed_kn must be positive")

    latitudes, longitudes, speeds, _ = _route_arrays(msg)
    points: list[NeutralRoutePoint] = []
    for index, (lat, lon) in enumerate(zip(latitudes, longitudes)):
        speed_mps = speeds[index] if speeds else 0.0
        speed_kn = speed_mps / MPS_PER_KNOT if speed_mps > 0.0 else default_speed
        points.append(NeutralRoutePoint(lat=lat, lon=lon, speed_kn=speed_kn))
    return points


def route_plan_signature(msg: Any, default_speed_kn: float = DEFAULT_SPEED_KN) -> str:
    points = route_plan_to_neutral_points(msg, default_speed_kn=default_speed_kn)
    payload = "|".join(f"{point.lat:.9f},{point.lon:.9f},{point.speed_kn:.6f}" for point in points)
    route_id = str(getattr(msg, "route_id", ""))
    route_type = str(getattr(msg, "route_type", ""))
    return hashlib.sha256(f"{route_id}|{route_type}|{payload}".encode("utf-8")).hexdigest()


def should_forward_route(last_signature: str | None, new_signature: str) -> bool:
    return last_signature != new_signature


def route_plan_to_payload(msg: Any, default_speed_kn: float = DEFAULT_SPEED_KN) -> dict[str, Any]:
    stamp_sec, stamp_nanosec = _stamp_from_msg(msg)
    points = route_plan_to_neutral_points(msg, default_speed_kn=default_speed_kn)
    payload = route_points_to_planned_route_dict(stamp_sec, stamp_nanosec, points)

    route_id = str(getattr(msg, "route_id", "")).strip()
    if route_id:
        payload["route_id"] = stable_route_id_from_string(route_id)

    _, _, _, modes = _route_arrays(msg)
    route_type = str(getattr(msg, "route_type", "")).strip() or "unknown"
    non_empty_modes = ",".join(mode for mode in modes if mode)
    mode_text = non_empty_modes if non_empty_modes else "none"
    id_text = route_id if route_id else "waypoint-signature"
    payload["rationale"] = (
        "external L2 RoutePlan converted to TDL route_in "
        f"route_id={id_text} route_type={route_type} navigation_modes={mode_text}"
    )
    return payload


def _route_qos():
    return QoSProfile(
        depth=10,
        reliability=QoSReliabilityPolicy.RELIABLE,
        durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
    )


class L2RoutePlanAdaptorNode(Node):
    def __init__(self) -> None:
        super().__init__("l2_route_plan_adaptor")
        self.declare_parameter("ingress_host", os.environ.get("TDL_INGRESS_HOST", "127.0.0.1"))
        self.declare_parameter("ingress_port", int(os.environ.get("TDL_INGRESS_PORT", "8765")))
        self.declare_parameter("default_speed_kn", float(os.environ.get("L2_DEFAULT_SPEED_KN", str(DEFAULT_SPEED_KN))))
        self.declare_parameter("strict_active", os.environ.get("L2_ROUTE_STRICT_ACTIVE", "1") == "1")
        self.declare_parameter("startup_timeout_s", float(os.environ.get("L2_ROUTE_STARTUP_TIMEOUT_S", "30.0")))

        self._ingress_host = str(self.get_parameter("ingress_host").value)
        self._ingress_port = int(self.get_parameter("ingress_port").value)
        self._default_speed_kn = float(self.get_parameter("default_speed_kn").value)
        self._strict_active = bool(self.get_parameter("strict_active").value)
        self._startup_timeout_s = float(self.get_parameter("startup_timeout_s").value)
        self._started_at = time.monotonic()
        self._active = not self._strict_active
        self._last_signature: str | None = None
        self._pending_route = None
        self._forwarded_once = False

        self.create_subscription(RoutePlan, "/route_planning/route_plan", self._on_route, _route_qos())
        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status", self._on_lifecycle, 10)
        self.create_timer(1.0, self._check_startup_timeout)
        self.get_logger().info(
            f"l2_route_plan_adaptor ready ingress={self._ingress_host}:{self._ingress_port} strict_active={self._strict_active}"
        )

    def _on_lifecycle(self, msg) -> None:
        self._active = int(msg.current_state) == ACTIVE_STATE
        if self._active and self._pending_route is not None:
            pending = self._pending_route
            self._pending_route = None
            self._forward_route(pending)

    def _on_route(self, msg) -> None:
        if not self._active:
            self._pending_route = msg
            self.get_logger().info("cached L2 RoutePlan until lifecycle ACTIVE")
            return
        self._forward_route(msg)

    def _forward_route(self, msg) -> None:
        try:
            signature = route_plan_signature(msg, default_speed_kn=self._default_speed_kn)
            if not should_forward_route(self._last_signature, signature):
                return
            payload = route_plan_to_payload(msg, default_speed_kn=self._default_speed_kn)
            with socket.create_connection((self._ingress_host, self._ingress_port), timeout=2.0) as sock:
                sock.sendall(encode_payload(payload))
            self._last_signature = signature
            self._forwarded_once = True
            self.get_logger().info(
                f"forwarded L2 RoutePlan waypoints={len(getattr(msg, 'latitude', []))} route_id={payload['route_id']}"
            )
        except Exception as exc:
            self.get_logger().warn(f"failed to forward L2 RoutePlan: {exc}")

    def _check_startup_timeout(self) -> None:
        if self._forwarded_once:
            return
        elapsed = time.monotonic() - self._started_at
        if elapsed >= self._startup_timeout_s:
            self.get_logger().error("no L2 RoutePlan forwarded before startup timeout")
            if self._strict_active:
                os._exit(2)


def main(args=None) -> None:
    if rclpy is None:
        raise RuntimeError("rclpy and generated ROS2 messages are required to run l2_route_plan_adaptor")
    rclpy.init(args=args)
    node = L2RoutePlanAdaptorNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

- [ ] **Step 2: Add console script entrypoint**

Modify `src/sim_workbench/external_adapters/setup.py`:

```python
        "console_scripts": [
            "external_tdl_ingress = external_adapters.tdl_ingress_node:main",
            "external_route_out_tdl = external_adapters.route_out_tdl_node:main",
            "external_route_out_path = external_adapters.route_out_external_path_node:main",
            "l2_route_plan_adaptor = external_adapters.l2_route_plan_adaptor:main",
        ],
```

- [ ] **Step 3: Add runtime dependencies**

Modify `src/sim_workbench/external_adapters/package.xml`:

```xml
  <exec_depend>ship_interfaces</exec_depend>
  <exec_depend>sil_msgs</exec_depend>
```

Place these two lines with the other `<exec_depend>` entries.

- [ ] **Step 4: Update entrypoint import test**

Modify `tests/sim_workbench/external_adapters/test_entrypoints.py`:

```python
    module_names = [
        "external_adapters.tdl_ingress_node",
        "external_adapters.route_out_tdl_node",
        "external_adapters.route_out_external_path_node",
        "external_adapters.l2_route_plan_adaptor",
    ]
```

- [ ] **Step 5: Run adaptor tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters pytest \
  tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py \
  tests/sim_workbench/external_adapters/test_entrypoints.py \
  -q
```

Expected: PASS.

- [ ] **Step 6: Commit adaptor implementation**

```bash
git add \
  src/sim_workbench/external_adapters/external_adapters/l2_route_plan_adaptor.py \
  src/sim_workbench/external_adapters/setup.py \
  src/sim_workbench/external_adapters/package.xml \
  tests/sim_workbench/external_adapters/test_entrypoints.py
git commit -m "feat: adapt external L2 route plan into TDL ingress"
```

---

### Task 4: Write Failing Tests For Lifecycle Route Seed

**Files:**
- Create: `tests/sim_workbench/external_adapters/test_l2_route_seed.py`

- [ ] **Step 1: Add seed conversion tests**

Create `tests/sim_workbench/external_adapters/test_l2_route_seed.py`:

```python
import json

import pytest
import yaml

from external_adapters.l2_route_seed import (
    RouteSeedError,
    scenario_to_bridge_route,
    write_bridge_route_file,
)


def _write_scenario(path, route):
    path.write_text(
        yaml.safe_dump(
            {
                "ownShip": {"nominalRoute": route},
                "metadata": {"scenario_id": "safe_route"},
            },
            allow_unicode=True,
            sort_keys=False,
        ),
        encoding="utf-8",
    )


def test_scenario_to_bridge_route_maps_nominal_route(tmp_path):
    scenario = tmp_path / "safe_route.yaml"
    _write_scenario(
        scenario,
        [
            {"latitude": -1.5, "longitude": 105.12, "target_sog_kn": 29.16},
            {"latitude": -1.491952, "longitude": 105.136095, "target_sog_kn": 26.24},
        ],
    )

    route = scenario_to_bridge_route(scenario)

    assert route["route_id"] == "safe_route-initial"
    assert route["route_type"] == "transit"
    assert route["selected_key"] == "safe_route"
    assert route["sample_points"] == [
        {"lat": -1.5, "lon": 105.12, "speed_kn": 29.16},
        {"lat": -1.491952, "lon": 105.136095, "speed_kn": 26.24},
    ]


def test_scenario_to_bridge_route_rejects_short_or_invalid_route(tmp_path):
    scenario = tmp_path / "short.yaml"
    _write_scenario(scenario, [{"latitude": -1.5, "longitude": 105.12, "target_sog_kn": 29.16}])

    with pytest.raises(RouteSeedError, match="at least two"):
        scenario_to_bridge_route(scenario)


def test_write_bridge_route_file_is_atomic_json(tmp_path):
    output = tmp_path / "gnc_bridge_route.json"
    route = {
        "route_id": "safe_route-initial",
        "route_type": "transit",
        "selected_key": "safe_route",
        "sample_points": [{"lat": -1.5, "lon": 105.12, "speed_kn": 29.16}, {"lat": -1.49, "lon": 105.13, "speed_kn": 29.16}],
    }

    write_bridge_route_file(route, output)

    assert json.loads(output.read_text(encoding="utf-8")) == route
    assert not (tmp_path / "gnc_bridge_route.json.tmp").exists()
```

- [ ] **Step 2: Run tests and verify they fail because the module does not exist**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters pytest tests/sim_workbench/external_adapters/test_l2_route_seed.py -q
```

Expected: FAIL with `ModuleNotFoundError: No module named 'external_adapters.l2_route_seed'`.

- [ ] **Step 3: Commit failing seed tests**

```bash
git add tests/sim_workbench/external_adapters/test_l2_route_seed.py
git commit -m "test: define L2 active route seed contract"
```

---

### Task 5: Implement Lifecycle Route Seed Node

**Files:**
- Create: `src/sim_workbench/external_adapters/external_adapters/l2_route_seed.py`
- Modify: `src/sim_workbench/external_adapters/setup.py`
- Modify: `tests/sim_workbench/external_adapters/test_entrypoints.py`

- [ ] **Step 1: Add seed module**

Create `src/sim_workbench/external_adapters/external_adapters/l2_route_seed.py`:

```python
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
from typing import Any

import yaml

ACTIVE_STATE = 3
DEFAULT_SCENARIO_YAML = "/var/sil/scenarios/集成测试/safe_route.yaml"
DEFAULT_OUTPUT_PATH = "/var/lib/l2_route/gnc_bridge_route.json"


try:
    import rclpy
    from rclpy.node import Node
    from sil_msgs.msg import LifecycleStatus
except ImportError:  # pragma: no cover - unit tests run without ROS2 generated messages.
    rclpy = None
    Node = object
    LifecycleStatus = None


class RouteSeedError(ValueError):
    pass


def _finite_float(value: Any, field_name: str) -> float:
    try:
        numeric = float(value)
    except (TypeError, ValueError) as exc:
        raise RouteSeedError(f"{field_name} must be numeric") from exc
    if not math.isfinite(numeric):
        raise RouteSeedError(f"{field_name} must be finite")
    return numeric


def scenario_to_bridge_route(scenario_yaml: str | Path, default_speed_kn: float = 10.0) -> dict[str, Any]:
    path = Path(scenario_yaml)
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RouteSeedError("scenario YAML must contain a mapping")

    nominal_route = data.get("ownShip", {}).get("nominalRoute", [])
    if not isinstance(nominal_route, list) or len(nominal_route) < 2:
        raise RouteSeedError("ownShip.nominalRoute must contain at least two waypoints")

    metadata = data.get("metadata", {})
    scenario_id = str(metadata.get("scenario_id") or path.stem)
    points: list[dict[str, float]] = []
    for index, waypoint in enumerate(nominal_route):
        if not isinstance(waypoint, dict):
            raise RouteSeedError(f"ownShip.nominalRoute[{index}] must be a mapping")
        lat = _finite_float(waypoint.get("latitude"), f"ownShip.nominalRoute[{index}].latitude")
        lon = _finite_float(waypoint.get("longitude"), f"ownShip.nominalRoute[{index}].longitude")
        speed_kn = _finite_float(
            waypoint.get("target_sog_kn", waypoint.get("speed_kn", default_speed_kn)),
            f"ownShip.nominalRoute[{index}].target_sog_kn",
        )
        if speed_kn <= 0.0:
            speed_kn = default_speed_kn
        points.append({"lat": lat, "lon": lon, "speed_kn": speed_kn})

    return {
        "route_id": f"{scenario_id}-initial",
        "route_type": "transit",
        "selected_key": scenario_id,
        "sample_points": points,
    }


def write_bridge_route_file(route: dict[str, Any], output_path: str | Path) -> None:
    path = Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_name(path.name + ".tmp")
    tmp_path.write_text(json.dumps(route, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")
    os.replace(tmp_path, path)


class RouteSeedOnActiveNode(Node):
    def __init__(self) -> None:
        super().__init__("l2_route_seed_on_active")
        self.declare_parameter("scenario_yaml", os.environ.get("L2_SCENARIO_YAML", DEFAULT_SCENARIO_YAML))
        self.declare_parameter("output_path", os.environ.get("L2_ROUTE_OUTPUT_PATH", DEFAULT_OUTPUT_PATH))
        self.declare_parameter("remove_on_start", os.environ.get("L2_ROUTE_REMOVE_ON_START", "1") == "1")
        self._scenario_yaml = str(self.get_parameter("scenario_yaml").value)
        self._output_path = str(self.get_parameter("output_path").value)
        self._written = False

        if bool(self.get_parameter("remove_on_start").value):
            Path(self._output_path).unlink(missing_ok=True)

        self.create_subscription(LifecycleStatus, "/sil/lifecycle_status", self._on_lifecycle, 10)
        self.get_logger().info(f"l2_route_seed_on_active waiting for ACTIVE scenario={self._scenario_yaml}")

    def _on_lifecycle(self, msg) -> None:
        if self._written or int(msg.current_state) != ACTIVE_STATE:
            return
        route = scenario_to_bridge_route(self._scenario_yaml)
        write_bridge_route_file(route, self._output_path)
        self._written = True
        self.get_logger().info(
            f"wrote L2 bridge route waypoints={len(route['sample_points'])} output={self._output_path}"
        )


def main(args=None) -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-once", action="store_true")
    parser.add_argument("--scenario-yaml", default=os.environ.get("L2_SCENARIO_YAML", DEFAULT_SCENARIO_YAML))
    parser.add_argument("--output-path", default=os.environ.get("L2_ROUTE_OUTPUT_PATH", DEFAULT_OUTPUT_PATH))
    parsed, ros_args = parser.parse_known_args(args)

    if parsed.write_once:
        write_bridge_route_file(scenario_to_bridge_route(parsed.scenario_yaml), parsed.output_path)
        return

    if rclpy is None:
        raise RuntimeError("rclpy and sil_msgs are required to run l2_route_seed_on_active")
    rclpy.init(args=ros_args)
    node = RouteSeedOnActiveNode()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
```

- [ ] **Step 2: Add console script entrypoint**

Modify `src/sim_workbench/external_adapters/setup.py`:

```python
            "l2_route_plan_adaptor = external_adapters.l2_route_plan_adaptor:main",
            "l2_route_seed_on_active = external_adapters.l2_route_seed:main",
```

- [ ] **Step 3: Update entrypoint import test**

Modify `tests/sim_workbench/external_adapters/test_entrypoints.py`:

```python
        "external_adapters.l2_route_plan_adaptor",
        "external_adapters.l2_route_seed",
```

- [ ] **Step 4: Run seed tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters pytest \
  tests/sim_workbench/external_adapters/test_l2_route_seed.py \
  tests/sim_workbench/external_adapters/test_entrypoints.py \
  -q
```

Expected: PASS.

- [ ] **Step 5: Commit seed implementation**

```bash
git add \
  src/sim_workbench/external_adapters/external_adapters/l2_route_seed.py \
  src/sim_workbench/external_adapters/setup.py \
  tests/sim_workbench/external_adapters/test_entrypoints.py
git commit -m "feat: seed L2 route on lifecycle active"
```

---

### Task 6: Write Static Tests For Compose, Manifest, And Probe Script

**Files:**
- Modify: `tests/scripts/test_runtime_plugin_compose.py`

- [ ] **Step 1: Add static tests for L2 plugin runtime contract**

Append to `tests/scripts/test_runtime_plugin_compose.py`:

```python
def test_l2_plugin_compose_builds_real_plugin_image():
    import yaml

    compose = yaml.safe_load((ROOT / "docker-compose.plugins.yml").read_text())
    service = compose["services"]["plugin-route-l2-main"]

    assert service["image"] == "mass-l2-planner:main"
    assert service["build"] == {"context": ".", "dockerfile": "docker/l2_external_plugin.Dockerfile"}
    assert service["command"] == ["/opt/l2_entrypoint.sh"]
    assert "while true" not in " ".join(service["command"])
    assert service["network_mode"] == "host"
    assert service["profiles"] == ["plugins"]


def test_l2_plugin_compose_sets_active_route_seed_environment():
    import yaml

    compose = yaml.safe_load((ROOT / "docker-compose.plugins.yml").read_text())
    environment = compose["services"]["plugin-route-l2-main"]["environment"]

    assert "ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-42}" in environment
    assert "TDL_INGRESS_HOST=127.0.0.1" in environment
    assert "TDL_INGRESS_PORT=8765" in environment
    assert "L2_ROUTE_STRICT_ACTIVE=1" in environment
    assert "L2_ROUTE_REMOVE_ON_START=1" in environment
    assert "L2_SCENARIO_YAML=/var/sil/scenarios/集成测试/safe_route.yaml" in environment


def test_l2_manifest_matches_external_route_plan_topic_and_domain():
    import yaml

    manifest = yaml.safe_load((ROOT / "config/runtime_plugins/l2-planner-main.yaml").read_text())

    assert manifest["compose"]["service"] == "plugin-route-l2-main"
    assert manifest["image"]["expected"] == "mass-l2-planner:main"
    assert manifest["ros"]["domain_id"] == 42
    assert manifest["ros"]["required_topics"] == {
        "/route_planning/route_plan": "ship_interfaces/msg/RoutePlan"
    }
    assert "/sil/actuator_cmd" in manifest["ros"]["forbidden_topics"]
    assert "/l4/control_cmd" in manifest["ros"]["forbidden_topics"]


def test_l2_external_probe_observes_source_and_tdl_route_topics():
    script = (ROOT / "scripts/integration/probe_l2_external_plugin.sh").read_text()

    assert "/route_planning/route_plan" in script
    assert "/l2/planned_route" in script
    assert "plugin-route-l2-main" in script
    assert "sil-nodes" in script
    assert "L2_EXTERNAL_PLUGIN_PROBE_PASS" in script
```

- [ ] **Step 2: Run static tests and verify they fail before compose/script edits**

Run:

```bash
pytest tests/scripts/test_runtime_plugin_compose.py -q
```

Expected: FAIL on the new L2 plugin compose/script assertions.

- [ ] **Step 3: Commit failing static tests**

```bash
git add tests/scripts/test_runtime_plugin_compose.py
git commit -m "test: define L2 plugin runtime wiring contract"
```

---

### Task 7: Add L2 Plugin Docker Image, Entrypoint, Compose Wiring, And Probe Script

**Files:**
- Create: `docker/l2_external_plugin.Dockerfile`
- Create: `plugins/l2_external/entrypoint.sh`
- Modify: `docker-compose.plugins.yml`
- Modify: `config/runtime_plugins/l2-planner-main.yaml`
- Create: `scripts/integration/probe_l2_external_plugin.sh`

- [ ] **Step 1: Add plugin Dockerfile**

Create `docker/l2_external_plugin.Dockerfile`:

```dockerfile
# syntax=docker/dockerfile:1.5
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|ports.ubuntu.com|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list && \
    if [ -f /usr/share/ros-apt-source/ros2.sources ]; then \
        sed -i 's|packages.ros.org|mirrors.tuna.tsinghua.edu.cn|g' /usr/share/ros-apt-source/ros2.sources; \
        sed -i '/^Types:/s/ deb-src//' /usr/share/ros-apt-source/ros2.sources; \
    fi && \
    if [ -f /etc/apt/sources.list.d/ros2.sources ]; then \
        sed -i 's|packages.ros.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list.d/ros2.sources; \
        sed -i '/^Types:/s/ deb-src//' /etc/apt/sources.list.d/ros2.sources; \
    fi

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-colcon-common-extensions \
        python3-yaml \
        ros-humble-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/l2_ws

COPY plugins/l2_external/ros2_ws/src/route_planning_ros2 src/route_planning_ros2
COPY plugins/l2_external/ros2_ws/src/platform/ship_interfaces src/ship_interfaces
COPY src/sim_workbench/sil_msgs src/sil_msgs

RUN --mount=type=cache,target=/root/.ccache,sharing=shared \
    . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install \
        --packages-select ship_interfaces sil_msgs route_planning_ros2 \
        --cmake-args -DBUILD_TESTING=OFF

COPY src/sim_workbench/external_adapters/external_adapters /opt/l2_adapters/external_adapters
COPY plugins/l2_external/entrypoint.sh /opt/l2_entrypoint.sh
RUN chmod +x /opt/l2_entrypoint.sh

ENV PYTHONPATH=/opt/l2_adapters
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp

CMD ["/opt/l2_entrypoint.sh"]
```

- [ ] **Step 2: Add plugin entrypoint**

Create `plugins/l2_external/entrypoint.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

source /opt/ros/humble/setup.bash
source /opt/l2_ws/install/setup.bash

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-42}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_cyclonedds_cpp}"
export GNC_ROUTE_PLANNING_DIR="${GNC_ROUTE_PLANNING_DIR:-/var/lib/l2_route}"
export SHIP_FEEDBACK_LOG_DIR="${SHIP_FEEDBACK_LOG_DIR:-/var/lib/l2_route/logs}"
export L2_ROUTE_OUTPUT_PATH="${L2_ROUTE_OUTPUT_PATH:-${GNC_ROUTE_PLANNING_DIR}/gnc_bridge_route.json}"
export L2_SCENARIO_YAML="${L2_SCENARIO_YAML:-/var/sil/scenarios/集成测试/safe_route.yaml}"
export TDL_INGRESS_HOST="${TDL_INGRESS_HOST:-127.0.0.1}"
export TDL_INGRESS_PORT="${TDL_INGRESS_PORT:-8765}"
export L2_ROUTE_STRICT_ACTIVE="${L2_ROUTE_STRICT_ACTIVE:-1}"
export L2_ROUTE_REMOVE_ON_START="${L2_ROUTE_REMOVE_ON_START:-1}"

mkdir -p "${GNC_ROUTE_PLANNING_DIR}" "${SHIP_FEEDBACK_LOG_DIR}"
if [ "${L2_ROUTE_REMOVE_ON_START}" = "1" ]; then
  rm -f "${L2_ROUTE_OUTPUT_PATH}" "${L2_ROUTE_OUTPUT_PATH}.tmp"
fi

python3 -m external_adapters.l2_route_seed &
seed_pid=$!

ros2 run route_planning_ros2 gnc_sim_node &
l2_pid=$!

python3 -m external_adapters.l2_route_plan_adaptor &
adaptor_pid=$!

trap 'kill "${seed_pid}" "${l2_pid}" "${adaptor_pid}" 2>/dev/null || true' INT TERM EXIT
wait -n "${seed_pid}" "${l2_pid}" "${adaptor_pid}"
exit_code=$?
kill "${seed_pid}" "${l2_pid}" "${adaptor_pid}" 2>/dev/null || true
exit "${exit_code}"
```

- [ ] **Step 3: Replace idle L2 compose service**

Modify only `plugin-route-l2-main` in `docker-compose.plugins.yml`:

```yaml
  plugin-route-l2-main:
    build:
      context: .
      dockerfile: docker/l2_external_plugin.Dockerfile
    image: mass-l2-planner:main
    command: ["/opt/l2_entrypoint.sh"]
    labels:
      org.opencontainers.image.revision: local-l2-main
      mass_l3.plugin.role: route_l2
      mass_l3.plugin.id: l2-planner-main
    profiles: ["plugins"]
    network_mode: host
    volumes:
      - ./scenarios:/var/sil/scenarios:ro
      - ./runs/l2_external:/var/lib/l2_route
    environment:
      - ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-42}
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
      - GNC_ROUTE_PLANNING_DIR=/var/lib/l2_route
      - SHIP_FEEDBACK_LOG_DIR=/var/lib/l2_route/logs
      - L2_ROUTE_OUTPUT_PATH=/var/lib/l2_route/gnc_bridge_route.json
      - L2_SCENARIO_YAML=/var/sil/scenarios/集成测试/safe_route.yaml
      - TDL_INGRESS_HOST=127.0.0.1
      - TDL_INGRESS_PORT=8765
      - L2_ROUTE_STRICT_ACTIVE=1
      - L2_ROUTE_REMOVE_ON_START=1
```

- [ ] **Step 4: Align runtime plugin manifest domain**

Modify `config/runtime_plugins/l2-planner-main.yaml`:

```yaml
ros:
  domain_id: 42
  required_topics:
    /route_planning/route_plan: ship_interfaces/msg/RoutePlan
```

Do not change `forbidden_topics`.

- [ ] **Step 5: Add local route evidence probe script**

Create `scripts/integration/probe_l2_external_plugin.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

source scripts/local-a4000-env.sh

docker compose exec -T plugin-route-l2-main bash -lc '
  set -euo pipefail
  source /opt/ros/humble/setup.bash
  source /opt/l2_ws/install/setup.bash
  timeout 30 ros2 topic echo --once /route_planning/route_plan
'

docker compose exec -T sil-nodes bash -lc '
  set -euo pipefail
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  timeout 30 ros2 topic echo --once /l2/planned_route
'

echo "L2_EXTERNAL_PLUGIN_PROBE_PASS"
```

Run:

```bash
chmod +x scripts/integration/probe_l2_external_plugin.sh plugins/l2_external/entrypoint.sh
```

- [ ] **Step 6: Run static compose tests**

Run:

```bash
pytest tests/scripts/test_runtime_plugin_compose.py -q
```

Expected: PASS.

- [ ] **Step 7: Run compose config validation**

Run:

```bash
source scripts/local-a4000-env.sh
docker compose config -q
```

Expected: exit code 0.

- [ ] **Step 8: Commit Docker and runtime wiring**

```bash
git add \
  docker/l2_external_plugin.Dockerfile \
  plugins/l2_external/entrypoint.sh \
  docker-compose.plugins.yml \
  config/runtime_plugins/l2-planner-main.yaml \
  scripts/integration/probe_l2_external_plugin.sh \
  tests/scripts/test_runtime_plugin_compose.py
git commit -m "feat: wire L2 external plugin container"
```

---

### Task 8: Run Focused Local Test Suite

**Files:**
- Test-only task.

- [ ] **Step 1: Run Python unit and static tests**

Run:

```bash
PYTHONPATH=src/sim_workbench/external_adapters pytest \
  tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py \
  tests/sim_workbench/external_adapters/test_l2_route_seed.py \
  tests/sim_workbench/external_adapters/test_entrypoints.py \
  tests/scripts/test_runtime_plugin_compose.py \
  -q
```

Expected: PASS.

- [ ] **Step 2: Run manifest parser tests**

Run:

```bash
pytest tests/sil_orchestrator/runtime/test_manifests.py -q
```

Expected: PASS.

- [ ] **Step 3: Run compose config test directly**

Run:

```bash
pytest tests/scripts/test_runtime_plugin_compose.py::test_plugin_compose_file_is_valid_with_local_env -q
```

Expected: PASS.

- [ ] **Step 4: Commit any test-only adjustment**

If no files changed, skip this step. If a narrow test adjustment was needed:

```bash
git add tests/sim_workbench/external_adapters tests/scripts/test_runtime_plugin_compose.py
git commit -m "test: cover L2 plugin integration wiring"
```

---

### Task 9: Run Local OrbStack Integration Gate

**Files:**
- Test-only task.

- [ ] **Step 1: Reclaim stale compose project only if the local project belongs to another checkout**

Run:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh --dry-run
```

Expected: output includes `runtime=/api/v1/runtime/summary` and `runtime_probe=/api/v1/runtime/probe`.

If the real gate later exits with stale checkout code 2, rerun that gate with:

```bash
RECLAIM_STALE_LOCAL_PROJECT=1 ./scripts/local-a4000-acceptance.sh
```

- [ ] **Step 2: Run local A4000-equivalent acceptance**

Run:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

Expected: output ends with `LOCAL A4000 CONTAINER ACCEPTANCE PASS` and writes `runs/local_a4000_container_probe_*.json` plus `runs/local_runtime_probe_*.json`.

- [ ] **Step 3: Trigger lifecycle ACTIVE through the normal Screen 02 API path**

Run:

```bash
source scripts/local-a4000-env.sh
curl -sk -X POST "${ORCH_URL}/api/v1/lifecycle/cleanup"
curl -sk -X POST "${ORCH_URL}/api/v1/lifecycle/configure" \
  -H 'Content-Type: application/json' \
  -d '{"scenario_id":"safe_route","scenario_path":"scenarios/集成测试/safe_route.yaml"}'
curl -sk -X POST "${ORCH_URL}/api/v1/lifecycle/activate"
```

Expected: activate response has `"success":true`.

- [ ] **Step 4: Probe L2 source topic and TDL route topic**

Run:

```bash
./scripts/integration/probe_l2_external_plugin.sh | tee "runs/l2_external_plugin_probe_$(date +%Y%m%d_%H%M%S).log"
```

Expected: output contains one `/route_planning/route_plan` message, one `/l2/planned_route` message, and `L2_EXTERNAL_PLUGIN_PROBE_PASS`.

- [ ] **Step 5: Save final local evidence summary**

Run:

```bash
git status --short
ls -1t runs/local_a4000_container_probe_*.json runs/local_runtime_probe_*.json runs/l2_external_plugin_probe_*.log | head -n 6
```

Expected: no unexpected source changes. Evidence paths are available for final handoff.

---

### Task 10: Optional A4000 Validation After Local Gate Passes

**Files:**
- Test/deploy task only. Do not run before Task 9 passes.

- [ ] **Step 1: Sync only touched paths to the verified marine TDL checkout**

Use the verified `A4000_TDL` path under the `marine.huang` account:

```bash
rsync -avR \
  docker/l2_external_plugin.Dockerfile \
  plugins/l2_external \
  src/sim_workbench/external_adapters \
  docker-compose.plugins.yml \
  config/runtime_plugins/l2-planner-main.yaml \
  scripts/integration/probe_l2_external_plugin.sh \
  tests/sim_workbench/external_adapters/test_l2_route_plan_adaptor.py \
  tests/sim_workbench/external_adapters/test_l2_route_seed.py \
  tests/scripts/test_runtime_plugin_compose.py \
  a4000:"${A4000_TDL}/"
```

- [ ] **Step 2: Run A4000 acceptance from the marine TDL checkout**

Run on A4000:

```bash
cd "${A4000_TDL}"
source scripts/a4000-env.sh
npm run sys:start
./scripts/a4000-acceptance.sh
./scripts/integration/probe_l2_external_plugin.sh | tee "runs/a4000_l2_external_plugin_probe_$(date +%Y%m%d_%H%M%S).log"
```

Expected: acceptance passes and probe output contains `L2_EXTERNAL_PLUGIN_PROBE_PASS`.

---

## Self-Review

- Spec requirement 1 is covered by Tasks 1 and 7: `plugin-route-l2-main` becomes a built image running L2 backend code.
- Spec requirements 2 and 3 are covered by Tasks 2, 3, 6, and 7: source topic remains `/route_planning/route_plan`, TDL topic remains `/l2/planned_route`.
- Spec requirement 4 is covered by Tasks 2 and 3: adaptor converts `RoutePlan` to `route_in` IPC.
- Spec requirement 5 is covered by Tasks 4, 5, and 7: route seed file is absent until lifecycle `ACTIVE`; adaptor also gates forwarding.
- Spec requirement 6 is covered by Tasks 2, 3, and 6: main path uses `RoutePlan`; no `GncRoutePlan` dependency is introduced.
- Spec requirement 7 is covered by Task 6: manifest forbidden topics remain `/sil/actuator_cmd` and `/l4/control_cmd`.
- Spec requirement 8 is covered by Tasks 8 and 9: focused tests and local OrbStack gate precede A4000 validation.
