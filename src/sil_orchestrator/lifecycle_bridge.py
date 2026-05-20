"""Bridge between FastAPI REST and ROS2 lifecycle services.

Controls scenario_lifecycle_mgr as primary node, then broadcasts configure/
activate/deactivate/cleanup to all SIL lifecycle nodes (ship_dynamics, scoring,
target_vessel, etc.) on a best-effort basis so the full SIL stack comes alive
with a single orchestrator call.
"""

import asyncio
import json
import math
import shutil
import yaml
import rclpy
from pathlib import Path
from rclpy.node import Node
from lifecycle_msgs.srv import ChangeState, GetState
from lifecycle_msgs.msg import Transition
from dataclasses import dataclass
from enum import Enum
import logging

_log = logging.getLogger(__name__)

# Ordered list of SIL lifecycle nodes besides scenario_lifecycle_mgr.
# Names must match what each Node.__init__ passes as node_name.
_SIL_LIFECYCLE_NODES = [
    "ship_dynamics_node",
    "env_disturbance_node",
    "target_vessel_node",
    "sensor_mock_node",
    "tracker_mock_node",
    "fault_injection_node",
    "scoring_node",
    "scenario_authoring_node",
]

# Scenario YAML root directories, keyed by a short label.
_SCENARIO_DIRS: dict[str, Path] = {
    "imazu22": Path("scenarios/IMAZU标准测试"),
    "colregs": Path("scenarios/COLREGs测试"),
}


def _resolve_scenario_yaml(scenario_id: str) -> Path | None:
    """Search _SCENARIO_DIRS for {scenario_id}.yaml; return first match or None."""
    for label, d in _SCENARIO_DIRS.items():
        candidate = d / f"{scenario_id}.yaml"
        if candidate.exists():
            _log.info("Resolved scenario %s → %s (%s)", scenario_id, candidate, label)
            return candidate.resolve()
    _log.warning("Scenario YAML not found for id=%s (searched %d dirs)",
                 scenario_id, len(_SCENARIO_DIRS))
    return None


def _inject_scenario_params(yaml_path: Path) -> dict[str, dict[str, object]]:
    """Parse a scenario YAML and return per-node param dicts.

    Returns
    -------
    dict[str, dict[str, object]]
        Keys are ROS2 node names; values are ``{param_name: value}`` dicts.
        Always includes ``"ship_dynamics_node"``, ``"target_vessel_node"``,
        and ``"env_disturbance_node"``.
    """
    with open(yaml_path, "r", encoding="utf-8") as f:
        data = yaml.safe_load(f)

    own = data["ownShip"]
    pos = own["initial"]["position"]
    heading_deg = own["initial"]["heading"]
    sog_kn = own["initial"]["sog"]

    psi0 = math.pi / 2.0 - math.radians(heading_deg)
    u0 = sog_kn * 0.514444

    ship_dynamics_params: dict[str, object] = {
        "origin_lat": pos["latitude"],
        "origin_lon": pos["longitude"],
        "psi0": psi0,
        "u0": u0,
        "x0": 0.0,
        "y0": 0.0,
    }

    # Build target vessel JSON from targetShips[]
    targets = []
    for ts in data.get("targetShips", []):
        tp = ts["initial"]["position"]
        th = ts["initial"]["heading"]
        tk = ts["initial"]["sog"]
        targets.append({
            "mmsi": ts.get("static", {}).get("mmsi", 0),
            "latitude": tp["latitude"],
            "longitude": tp["longitude"],
            "heading_deg": th,
            "sog_kn": tk,
        })
    target_vessel_params: dict[str, object] = {
        "default_targets_json": json.dumps(targets),
    }

    # Environment parameters
    env = data.get("environment", {})
    env_disturbance_params: dict[str, object] = {
        "wind_dir_deg": env.get("wind", {}).get("dir_deg", 0.0),
        "wind_speed_mps": env.get("wind", {}).get("speed_mps", 0.0),
        "current_dir_deg": env.get("current", {}).get("dir_deg", 0.0),
        "current_speed_mps": env.get("current", {}).get("speed_mps", 0.0),
    }

    return {
        "ship_dynamics_node": ship_dynamics_params,
        "target_vessel_node": target_vessel_params,
        "env_disturbance_node": env_disturbance_params,
    }


class LifecycleState(str, Enum):
    UNCONFIGURED = "unconfigured"
    INACTIVE = "inactive"
    ACTIVE = "active"
    DEACTIVATING = "deactivating"
    FINALIZED = "finalized"


@dataclass
class LifecycleResult:
    success: bool
    error: str = ""


class LifecycleBridge(Node):
    """rclpy wrapper over ROS2 lifecycle service calls.

    Primary target: scenario_lifecycle_mgr.
    Broadcast targets: all _SIL_LIFECYCLE_NODES — best-effort, non-blocking.
    """

    def __init__(self, callback_group=None) -> None:
        super().__init__('sil_orchestrator_lifecycle_bridge')
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str | None = None

        # ROS2 service clients — primary node
        self.change_state_client = self.create_client(
            ChangeState, '/scenario_lifecycle_mgr/change_state',
            callback_group=callback_group)
        self.get_state_client = self.create_client(
            GetState, '/scenario_lifecycle_mgr/get_state',
            callback_group=callback_group)

        # Pre-create service clients for each SIL node (best-effort)
        self._sil_change_state_clients: dict[str, object] = {}
        for node_name in _SIL_LIFECYCLE_NODES:
            svc = f"/{node_name}/change_state"
            try:
                client = self.create_client(ChangeState, svc,
                                            callback_group=callback_group)
                self._sil_change_state_clients[node_name] = client
            except Exception as exc:
                _log.debug("Could not create client for %s: %s", svc, exc)

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str | None:
        return self._scenario_id

    async def _get_ros2_state(self) -> str | None:
        """Query the actual ROS2 node state (not the Python mirror).

        Returns the state label string (e.g. 'unconfigured', 'inactive', 'active')
        or None if the service is unavailable.
        """
        if not self.get_state_client.wait_for_service(timeout_sec=2.0):
            return None
        req = GetState.Request()
        try:
            future = self.get_state_client.call_async(req)
            deadline = 20  # 2 s
            while not future.done() and deadline > 0:
                await asyncio.sleep(0.1)
                deadline -= 1
            if not future.done():
                return None
            return future.result().current_state.label
        except Exception:
            return None

    async def _reset_to_unconfigured(self) -> LifecycleResult:
        """Drive the ROS2 node to unconfigured regardless of its current state.

        Needed after an orchestrator restart where _state is stale but the node
        may still be active/inactive from the previous session.
        """
        ros2_state = await self._get_ros2_state()
        _log.info("_reset_to_unconfigured: actual ROS2 state = %s", ros2_state)
        if ros2_state in (None, "unconfigured", "finalized"):
            self._state = LifecycleState.UNCONFIGURED
            return LifecycleResult(success=True)
        if ros2_state == "active":
            res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
            if not res.success:
                return res
            self._state = LifecycleState.INACTIVE
        # Now in inactive — cleanup to unconfigured
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            self._state = LifecycleState.UNCONFIGURED
        return res

    async def _change_state(self, transition_id: int) -> LifecycleResult:
        if not self.change_state_client.wait_for_service(timeout_sec=2.0):
            return LifecycleResult(
                success=False,
                error="Lifecycle service /scenario_lifecycle_mgr/change_state not available")

        req = ChangeState.Request()
        req.transition.id = transition_id

        try:
            future = self.change_state_client.call_async(req)
            deadline = 150  # 150 × 0.1 s = 15 s timeout (generous for loaded executors)
            while not future.done() and deadline > 0:
                await asyncio.sleep(0.1)
                deadline -= 1
            if not future.done():
                return LifecycleResult(
                    success=False,
                    error="Lifecycle service call timed out (15s) — node may not be ready")
            response = future.result()
            if response.success:
                return LifecycleResult(success=True)
            return LifecycleResult(success=False, error="Transition rejected by node")
        except Exception as exc:
            return LifecycleResult(success=False, error=str(exc))

    async def _broadcast_to_node(self, node_name: str, client, transition_id: int) -> None:
        """Send one lifecycle transition to a single SIL node (best-effort, never raises)."""
        try:
            if not client.wait_for_service(timeout_sec=0.5):
                _log.debug("broadcast: /%s/change_state not available — skipping", node_name)
                return
            req = ChangeState.Request()
            req.transition.id = transition_id
            future = client.call_async(req)
            deadline = 20  # 20 × 0.1 s = 2 s
            while not future.done() and deadline > 0:
                await asyncio.sleep(0.1)
                deadline -= 1
            if future.done():
                resp = future.result()
                if resp.success:
                    _log.info("broadcast: /%s transition %d → OK", node_name, transition_id)
                else:
                    _log.warning("broadcast: /%s transition %d → rejected", node_name, transition_id)
            else:
                _log.warning("broadcast: /%s transition %d timed out", node_name, transition_id)
        except Exception as exc:
            _log.debug("broadcast: /%s error: %s", node_name, exc)

    async def _broadcast_transition(self, transition_id: int) -> None:
        """Send transition to every SIL lifecycle node in parallel; log failures, never raise."""
        tasks = [
            self._broadcast_to_node(node_name, client, transition_id)
            for node_name, client in self._sil_change_state_clients.items()
        ]
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    async def configure(self, scenario_id: str) -> LifecycleResult:
        # Always sync with the real ROS2 state first — the Python mirror can be
        # stale after an orchestrator restart (node stays active, bridge resets).
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset

        # Resolve scenario YAML and inject per-node parameters before any
        # lifecycle transition so target nodes see correct initial conditions
        # during their on_configure() callback (GAP 3 fix).
        yaml_path = _resolve_scenario_yaml(scenario_id)
        if yaml_path is not None:
            try:
                node_params = _inject_scenario_params(yaml_path)
                for node_name, params in node_params.items():
                    for k, v in params.items():
                        self.declare_parameter(f"{node_name}.{k}", value=v)
                    _log.info("Set %d params for %s from scenario %s",
                              len(params), node_name, scenario_id)
            except Exception as exc:
                _log.error("Failed to inject params from %s: %s", yaml_path, exc)
        else:
            _log.warning("Proceeding without YAML params for scenario=%s", scenario_id)

        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
            # Fire broadcast as background task so configure() returns quickly
            # and activate() can proceed without waiting for all SIL node responses.
            asyncio.create_task(self._broadcast_transition(Transition.TRANSITION_CONFIGURE))
        return res

    async def activate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_ACTIVATE)
        if res.success:
            self._state = LifecycleState.ACTIVE
            await self._broadcast_transition(Transition.TRANSITION_ACTIVATE)
        return res

    async def deactivate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
        if res.success:
            self._state = LifecycleState.INACTIVE
            await self._broadcast_transition(Transition.TRANSITION_DEACTIVATE)
        return res

    async def cleanup(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            self._state = LifecycleState.UNCONFIGURED
            self._scenario_id = None
            await self._broadcast_transition(Transition.TRANSITION_CLEANUP)
        return res


def _copy_preflight_evidence(scenario_id: str, run_id: str) -> None:
    """Archive staging evidence from .preflight/ to runs/{run_id}/preflight/"""
    from sil_orchestrator.config import SCENARIO_DIR, RUN_DIR
    src = SCENARIO_DIR / scenario_id / ".preflight"
    dst = RUN_DIR / run_id / "preflight"
    if not src.exists():
        return
    dst.mkdir(parents=True, exist_ok=True)
    for gate_file in src.glob("gate_*.json"):
        shutil.copy2(gate_file, dst / gate_file.name)

