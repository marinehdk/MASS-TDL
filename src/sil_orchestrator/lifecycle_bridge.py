"""Bridge between FastAPI REST and ROS2 lifecycle services.

Controls scenario_lifecycle_mgr as primary node, then broadcasts configure/
activate/deactivate/cleanup to all SIL lifecycle nodes (ship_dynamics, scoring,
target_vessel, etc.) on a best-effort basis so the full SIL stack comes alive
with a single orchestrator call.

Also handles scenario YAML → ROS2 parameter injection before the CONFIGURE
transition so simulation nodes receive initial conditions from the scenario
definition instead of silently falling back to hardcoded defaults.
"""

import asyncio
import json
import shutil
import rclpy
from pathlib import Path
from rclpy.node import Node
from lifecycle_msgs.srv import ChangeState, GetState
from lifecycle_msgs.msg import Transition
from dataclasses import dataclass
from enum import Enum
import logging
import yaml

from rcl_interfaces.srv import SetParameters
from rcl_interfaces.msg import Parameter, ParameterValue, ParameterType

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


class LifecycleState(str, Enum):
    UNCONFIGURED = "unconfigured"
    INACTIVE = "inactive"
    ACTIVE = "active"
    DEACTIVATING = "deactivating"
    FINALIZED = "finalized"


class ScenarioInjectionError(Exception):
    """Raised when scenario parameter injection into a ROS2 node fails."""


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

        # Pre-create SetParameters service clients for scenario param injection
        self._sil_set_parameters_clients: dict[str, object] = {}
        for node_name in ("ship_dynamics_node", "target_vessel_node",
                          "env_disturbance_node", "scenario_lifecycle_mgr"):
            svc = f"/{node_name}/set_parameters"
            try:
                client = self.create_client(SetParameters, svc,
                                            callback_group=callback_group)
                self._sil_set_parameters_clients[node_name] = client
            except Exception as exc:
                _log.debug("Could not create SetParameters client for %s: %s", svc, exc)

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

    async def _inject_params_to_node(self, node_name: str,
                                     params: dict) -> None:
        """Inject ROS2 parameters into a node via SetParameters service.

        Raises ScenarioInjectionError on any failure (service unavailable,
        timeout, or node rejects a parameter).
        """
        client = self._sil_set_parameters_clients.get(node_name)
        if client is None:
            raise ScenarioInjectionError(
                f"SetParameters client for '{node_name}' not available")

        if not client.wait_for_service(timeout_sec=3.0):
            raise ScenarioInjectionError(
                f"SetParameters service /{node_name}/set_parameters "
                "not available after 3s")

        req = SetParameters.Request()
        for param_name, (value, param_type) in params.items():
            pv = ParameterValue(type=param_type)
            if param_type == ParameterType.PARAMETER_DOUBLE:
                pv.double_value = value
            elif param_type == ParameterType.PARAMETER_STRING:
                pv.string_value = value
            elif param_type == ParameterType.PARAMETER_INTEGER:
                pv.integer_value = value
            req.parameters.append(Parameter(name=param_name, value=pv))

        try:
            future = client.call_async(req)
            deadline = 30  # 30 * 0.1 s = 3 s
            while not future.done() and deadline > 0:
                await asyncio.sleep(0.1)
                deadline -= 1
            if not future.done():
                raise ScenarioInjectionError(
                    f"SetParameters call to '{node_name}' timed out (3s)")
            response = future.result()
            for result in response.results:
                if not result.successful:
                    reason = result.reason or "unknown reason"
                    raise ScenarioInjectionError(
                        f"Parameter injection to '{node_name}' failed: "
                        f"{reason}")
        except ScenarioInjectionError:
            raise
        except Exception as exc:
            raise ScenarioInjectionError(
                f"SetParameters call to '{node_name}' failed: {exc}") from exc

    async def _broadcast_transition(self, transition_id: int) -> None:
        """Send transition to every SIL lifecycle node in parallel; log failures, never raise."""
        tasks = [
            self._broadcast_to_node(node_name, client, transition_id)
            for node_name, client in self._sil_change_state_clients.items()
        ]
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    async def _broadcast_and_wait(self, transition_id: int, target_state: str, timeout_s: float = 15.0) -> LifecycleResult:
        """Broadcast transition to all SIL nodes and wait for every node to reach target_state.

        Unlike the old fire-and-forget asyncio.create_task() pattern, this blocks
        until all nodes confirm they've transitioned (or timeout). This prevents
        the race where configure() returns before ship_dynamics publishes.
        """
        _log.info("[broadcast] Sending transition %d to %d SIL nodes, waiting for '%s' (timeout=%.0fs)",
                  transition_id, len(self._sil_change_state_clients), target_state, timeout_s)

        # 1. Send transitions in parallel (fire)
        tasks = [
            self._broadcast_to_node(node_name, client, transition_id)
            for node_name, client in self._sil_change_state_clients.items()
        ]
        await asyncio.gather(*tasks, return_exceptions=True)

        # 2. Wait for all nodes to report target_state (poll)
        deadline = asyncio.get_event_loop().time() + timeout_s
        failed_nodes: list[str] = []

        for node_name, client in self._sil_change_state_clients.items():
            while asyncio.get_event_loop().time() < deadline:
                try:
                    if not client.wait_for_service(timeout_sec=0.5):
                        await asyncio.sleep(0.5)
                        continue
                    req = GetState.Request()
                    future = client.call_async(req)
                    # Resolve the future with a short deadline
                    state_deadline = 10  # 10 × 0.1s = 1s per attempt
                    while not future.done() and state_deadline > 0:
                        await asyncio.sleep(0.1)
                        state_deadline -= 1
                    if not future.done():
                        continue
                    current = future.result().current_state.label
                    if current == target_state:
                        _log.info("[broadcast] %s → %s ✓", node_name, target_state)
                        break  # this node done
                except Exception:
                    await asyncio.sleep(0.5)
            else:
                # Timeout for this node
                failed_nodes.append(node_name)
                _log.error("[broadcast] %s did NOT reach '%s' within %.0fs", node_name, target_state, timeout_s)

        if failed_nodes:
            # Fail-loud: dump diagnostic info
            _log.error("[broadcast] %d/%d nodes failed: %s",
                       len(failed_nodes), len(self._sil_change_state_clients), ", ".join(failed_nodes))
            return LifecycleResult(
                success=False,
                error=f"Broadcast timeout: {len(failed_nodes)} nodes stuck before '{target_state}': {', '.join(failed_nodes)}")
        return LifecycleResult(success=True)

    async def configure(self, scenario_id: str) -> LifecycleResult:
        # Step 1-3: Load scenario YAML, extract params, log summary
        yaml_data = _load_scenario_yaml(scenario_id)
        injection_map = _extract_injection_params(yaml_data)
        _print_injection_summary(injection_map)

        # Step 4: Inject params in parallel — fail-loud on first error
        if injection_map:
            tasks = [
                self._inject_params_to_node(node_name, params)
                for node_name, params in injection_map.items()
            ]
            results = await asyncio.gather(*tasks, return_exceptions=True)
            for result in results:
                if isinstance(result, ScenarioInjectionError):
                    raise result

        # Step 5-7: Original flow — reset, configure transition, broadcast
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
            # Explicit sync: wait for all SIL nodes to reach 'inactive' before returning.
            # This replaces the old asyncio.create_task() fire-and-forget pattern (line 291)
            # which allowed configure() to return before ship_dynamics was ready.
            broadcast_result = await self._broadcast_and_wait(
                Transition.TRANSITION_CONFIGURE, "inactive", timeout_s=15.0)
            if not broadcast_result.success:
                return broadcast_result
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


def _load_scenario_yaml(scenario_id: str, base_dir: Path | None = None) -> dict:
    """Load and parse a scenario YAML by ID.

    Uses ScenarioStore to locate and read the file, then parses with
    yaml.safe_load. Raises ScenarioInjectionError on any failure.
    """
    from sil_orchestrator.scenario_store import ScenarioStore
    store = ScenarioStore(base_dir=base_dir)
    detail = store.get(scenario_id)
    if detail is None:
        raise ScenarioInjectionError(
            f"Scenario '{scenario_id}' not found in store")
    try:
        yaml_data = yaml.safe_load(detail["yaml_content"])
    except yaml.YAMLError as exc:
        raise ScenarioInjectionError(
            f"Failed to parse scenario YAML '{scenario_id}': {exc}") from exc
    if not isinstance(yaml_data, dict):
        raise ScenarioInjectionError(
            f"Scenario YAML '{scenario_id}' did not parse to a dict")
    return yaml_data


def _extract_injection_params(yaml_data: dict) -> dict:
    """Build parameter injection map from parsed scenario YAML.

    Returns dict of ``node_name -> {param_name: (value, ParameterType constant)}``
    for all fields that have a defined mapping to ROS2 node parameters.
    """
    injection_map: dict = {}

    own_ship = yaml_data.get("ownShip", {})
    initial = own_ship.get("initial", {}) if isinstance(own_ship, dict) else {}
    pos = initial.get("position", {}) if isinstance(initial, dict) else {}

    # ownShip.initial.position.{latitude,longitude} + {heading,sog,cog}
    ship_params: dict = {}
    if isinstance(pos, dict):
        lat = pos.get("latitude")
        if lat is not None:
            ship_params["initial_lat"] = (float(lat), ParameterType.PARAMETER_DOUBLE)
        lon = pos.get("longitude")
        if lon is not None:
            ship_params["initial_lon"] = (float(lon), ParameterType.PARAMETER_DOUBLE)
    if isinstance(initial, dict):
        heading = initial.get("heading")
        if heading is not None:
            ship_params["initial_heading"] = (float(heading), ParameterType.PARAMETER_DOUBLE)
        sog = initial.get("sog")
        if sog is not None:
            ship_params["initial_sog"] = (float(sog), ParameterType.PARAMETER_DOUBLE)
        cog = initial.get("cog")
        if cog is not None:
            ship_params["initial_cog"] = (float(cog), ParameterType.PARAMETER_DOUBLE)
    if ship_params:
        injection_map["ship_dynamics_node"] = ship_params

    # targetShips[] -> target_vessel_node -> default_targets_json
    target_ships = yaml_data.get("targetShips", [])
    if isinstance(target_ships, list) and target_ships:
        targets_json = json.dumps(target_ships)
        injection_map["target_vessel_node"] = {
            "default_targets_json": (targets_json, ParameterType.PARAMETER_STRING)
        }

    # environment.{wind,current} -> env_disturbance_node
    env = yaml_data.get("environment", {})
    env_params: dict = {}
    if isinstance(env, dict):
        wind = env.get("wind", {})
        if isinstance(wind, dict):
            wdir = wind.get("dir_deg")
            if wdir is not None:
                env_params["wind_dir_deg"] = (float(wdir), ParameterType.PARAMETER_DOUBLE)
            wspd = wind.get("speed_mps")
            if wspd is not None:
                env_params["wind_speed_mps"] = (float(wspd), ParameterType.PARAMETER_DOUBLE)
        current = env.get("current", {})
        if isinstance(current, dict):
            cdir = current.get("dir_deg")
            if cdir is not None:
                env_params["current_dir_deg"] = (float(cdir), ParameterType.PARAMETER_DOUBLE)
            cspd = current.get("speed_mps")
            if cspd is not None:
                env_params["current_speed_mps"] = (float(cspd), ParameterType.PARAMETER_DOUBLE)
    if env_params:
        injection_map["env_disturbance_node"] = env_params

    # metadata.scenario_id -> scenario_lifecycle_mgr -> scenario_id
    metadata = yaml_data.get("metadata", {})
    if isinstance(metadata, dict):
        sid = metadata.get("scenario_id")
        if sid is not None:
            injection_map["scenario_lifecycle_mgr"] = {
                "scenario_id": (str(sid), ParameterType.PARAMETER_STRING)
            }

    return injection_map


def _print_injection_summary(injection_map: dict) -> None:
    """Log a summary of parameters to be injected per node."""
    total_params = sum(len(params) for params in injection_map.values())
    if total_params == 0:
        _log.info("Scenario parameter injection: no parameters to inject")
        return
    _log.info(
        "Scenario parameter injection: %d params across %d nodes",
        total_params, len(injection_map),
    )
    for node_name, params in injection_map.items():
        for param_name, param_value in params.items():
            _log.info("  inject /%s :: %s = %s", node_name, param_name, param_value)


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

