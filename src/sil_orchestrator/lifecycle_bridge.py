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
import os
import shutil
import time
import rclpy
from pathlib import Path
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from lifecycle_msgs.srv import ChangeState, GetState
from lifecycle_msgs.msg import Transition
from dataclasses import dataclass
from enum import Enum
import logging
import yaml

from rcl_interfaces.srv import SetParameters
from rcl_interfaces.msg import Parameter, ParameterValue, ParameterType

from sil_msgs.msg import OwnShipState
from sil_msgs.msg import ShipReset
from sil_msgs.srv import AddTarget, RemoveTarget
from std_msgs.msg import String

_log = logging.getLogger(__name__)

# Latched QoS for the cross-run reset signal. TRANSIENT_LOCAL so that C++ L3
# nodes launched in entrypoint Stage 3 (after orchestrator configure publishes
# scenario_loaded) still receive the most recent scenario_id on subscription.
_SCENARIO_LOADED_QOS = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)

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
        self._sim_rate = 1.0
        self._simulation_duration_s = None
        self._timer_start_time = None
        self._timer_task = None
        self._backup_timer_task = None
        self._active_tasks = set()

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

        # Pre-create GetState service clients for each SIL node (best-effort)
        self._sil_get_state_clients: dict[str, object] = {}
        for node_name in _SIL_LIFECYCLE_NODES:
            svc = f"/{node_name}/get_state"
            try:
                client = self.create_client(GetState, svc,
                                            callback_group=callback_group)
                self._sil_get_state_clients[node_name] = client
            except Exception as exc:
                _log.debug("Could not create GetState client for %s: %s", svc, exc)

        # Pre-create SetParameters service clients for scenario param injection
        self._sil_set_parameters_clients: dict[str, object] = {}


        for node_name in ("ship_dynamics_node", "target_vessel_node",
                          "env_disturbance_node", "sensor_mock_node",
                          "fault_injection_node", "scenario_lifecycle_mgr"):
            svc = f"/{node_name}/set_parameters"
            try:
                client = self.create_client(SetParameters, svc,
                                            callback_group=callback_group)
                self._sil_set_parameters_clients[node_name] = client
            except Exception as exc:
                _log.debug("Could not create SetParameters client for %s: %s", svc, exc)

        # Own-ship state cache (updated via subscription, used by encounter injection)
        self._latest_own_ship = None
        self._latest_own_ship_monotonic = None
        own_ship_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )
        self.create_subscription(
            OwnShipState, "/sil/own_ship_state", self._on_own_ship_state, own_ship_qos)
        self._scenario_loaded_pub = self.create_publisher(
            String, "/sil/scenario_loaded", qos_profile=_SCENARIO_LOADED_QOS)
        self._reset_own_ship_pub = self.create_publisher(
            ShipReset, "/l3/sim/reset_own_ship", 10)

        # Service clients for runtime encounter injection (D1.8)
        self._add_target_client = self.create_client(
            AddTarget, "/target_vessel_node/add_target",
            callback_group=callback_group)
        self._remove_target_client = self.create_client(
            RemoveTarget, "/target_vessel_node/remove_target",
            callback_group=callback_group)

    def _publish_scenario_loaded(self, scenario_id: str) -> None:
        msg = String()
        msg.data = scenario_id
        self._scenario_loaded_pub.publish(msg)

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

    async def _reset_secondary_node(self, node_name: str) -> None:
        """Intelligently reset a secondary SIL node to UNCONFIGURED state based on its current state."""
        state_client = self._sil_get_state_clients.get(node_name)
        change_client = self._sil_change_state_clients.get(node_name)
        if not state_client or not change_client:
            return

        # Query state with short timeout so we don't block
        if not state_client.wait_for_service(timeout_sec=0.2):
            _log.debug("reset_secondary: /%s/get_state not available — skipping", node_name)
            return

        req = GetState.Request()
        current_state = None
        try:
            future = state_client.call_async(req)
            deadline = 10  # 1.0 s
            while not future.done() and deadline > 0:
                await asyncio.sleep(0.1)
                deadline -= 1
            if future.done():
                current_state = future.result().current_state.label
        except Exception as exc:
            _log.debug("reset_secondary: failed to get state for /%s: %s", node_name, exc)
            return

        _log.info("reset_secondary: /%s current state = %s", node_name, current_state)
        if current_state in (None, "unconfigured", "finalized"):
            return

        if current_state == "active":
            await self._broadcast_to_node(node_name, change_client, Transition.TRANSITION_DEACTIVATE)
            await asyncio.sleep(0.1)
            current_state = "inactive"

        if current_state == "inactive":
            await self._broadcast_to_node(node_name, change_client, Transition.TRANSITION_CLEANUP)

    async def _reset_to_unconfigured(self) -> LifecycleResult:
        """Drive the ROS2 node to unconfigured regardless of its current state.

        Needed after an orchestrator restart where _state is stale but the node
        may still be active/inactive from the previous session.
        """
        if self._timer_task:
            self._timer_task.cancel()
            self._timer_task = None
        if self._backup_timer_task:
            self._backup_timer_task.cancel()
            self._backup_timer_task = None

        # Reset all secondary SIL lifecycle nodes in parallel intelligently based on their state
        tasks = [self._reset_secondary_node(node_name) for node_name in _SIL_LIFECYCLE_NODES]
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

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
            for attempt in range(2):
                future = client.call_async(req)
                deadline = 150  # 150 * 0.1 s = 15 s
                while not future.done() and deadline > 0:
                    await asyncio.sleep(0.1)
                    deadline -= 1
                if not future.done():
                    if attempt == 0:
                        _log.warning(
                            "SetParameters call to '%s' timed out (15s); retrying once",
                            node_name,
                        )
                        await asyncio.sleep(0.5)
                        continue
                    raise ScenarioInjectionError(
                        f"SetParameters call to '{node_name}' timed out (15s)")
                response = future.result()
                for result in response.results:
                    if not result.successful:
                        reason = result.reason or "unknown reason"
                        raise ScenarioInjectionError(
                            f"Parameter injection to '{node_name}' failed: "
                            f"{reason}")
                return
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

    async def configure(self, scenario_id: str) -> LifecycleResult:
        # Step 1-3: Load scenario YAML, extract params, log summary
        yaml_data = _load_scenario_yaml(scenario_id)

        # Parse total_time from simulation_settings, fall back to duration_s
        duration = None
        metadata = yaml_data.get("metadata", {}) if isinstance(yaml_data, dict) else {}
        if isinstance(metadata, dict):
            sim_settings = metadata.get("simulation_settings", {})
            if isinstance(sim_settings, dict):
                duration = sim_settings.get("total_time")
        
        if duration is None:
            sim_section = yaml_data.get("simulation", {}) if isinstance(yaml_data, dict) else {}
            if isinstance(sim_section, dict):
                duration = sim_section.get("total_time") or sim_section.get("duration_s")

        self._simulation_duration_s = duration

        self._timer_start_time = None
        if self._timer_task:
            self._timer_task.cancel()
            self._timer_task = None
        if self._backup_timer_task:
            self._backup_timer_task.cancel()
            self._backup_timer_task = None

        injection_map = _filter_injection_params_for_runtime_profile(
            _extract_injection_params(yaml_data),
            os.environ.get("TDL_RUNTIME_PROFILE", "internal-local"),
        )
        _print_injection_summary(injection_map)

        # Step 5: reset to UNCONFIGURED FIRST so the node's parameter store is a
        # clean slate, THEN inject (allow_undeclared_parameters=True lets us set
        # scenario_id before on_configure declares it; on_configure then sees
        # has_parameter(...)=True and keeps the injected value instead of the
        # "" default). Injecting before the reset let the cleanup wipe the param,
        # so the LifecycleStatus broadcast carried scenario_id="" and mock_l2
        # could never load the scenario route (route-return Break #1).
        reset = await self._reset_to_unconfigured()
        if not reset.success:
            return reset

        # Step 6: Inject params — fail-loud on first error
        if injection_map:
            tasks = [
                self._inject_params_to_node(node_name, params)
                for node_name, params in injection_map.items()
            ]
            results = await asyncio.gather(*tasks, return_exceptions=True)
            for result in results:
                if isinstance(result, ScenarioInjectionError):
                    raise result



        # Step 7: configure transition + broadcast
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
            self._publish_scenario_loaded(scenario_id)
            # GNC profile: reset the GNC plant own-ship to scenario start point.
            # The plant (domain 50) is not reachable by lifecycle/cross-run-reset,
            # so emit a ShipReset that gnc_bridge forwards to ship_dynamics +
            # coordinate_transform. SIL profile skips this (handled by injection).
            runtime_profile = os.environ.get("TDL_RUNTIME_PROFILE", "internal-local")
            if runtime_profile == "gnc":
                reset_msg = _build_ship_reset(yaml_data)
                if reset_msg is not None:
                    reset_msg.header.stamp = self.get_clock().now().to_msg()
                    self._reset_own_ship_pub.publish(reset_msg)
                    _log.info(
                        "GNC reset emitted: lat=%.6f lon=%.6f heading=%.1f sog=%.1f",
                        reset_msg.latitude, reset_msg.longitude,
                        reset_msg.heading_deg, reset_msg.sog_kn)
            task = asyncio.create_task(self._broadcast_transition(Transition.TRANSITION_CONFIGURE))
            self._active_tasks.add(task)
            task.add_done_callback(self._active_tasks.discard)
        return res

    async def activate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_ACTIVATE)
        if res.success:
            self._state = LifecycleState.ACTIVE
            if self._scenario_id:
                self._publish_scenario_loaded(self._scenario_id)
            await self._broadcast_transition(Transition.TRANSITION_ACTIVATE)

            # Cancel any existing timers before scheduling new ones to prevent orphans
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None

            # Start timer and backup timer if duration is specified
            if self._simulation_duration_s is not None:
                import time
                self._timer_start_time = time.time()
                self._timer_task = asyncio.create_task(self._auto_stop_timer())
                self._backup_timer_task = asyncio.create_task(self._auto_stop_backup_timer())
        return res

    async def deactivate(self) -> LifecycleResult:
        # Idempotency check: if already INACTIVE, return success
        state_str = self._state.value if hasattr(self._state, "value") else str(self._state)
        if state_str.lower() == "inactive":
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None
            return LifecycleResult(success=True)

        res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
        if res.success:
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None
            self._state = LifecycleState.INACTIVE
            await self._broadcast_transition(Transition.TRANSITION_DEACTIVATE)
        return res

    async def _auto_stop_timer(self) -> None:
        try:
            duration = float(self._simulation_duration_s)
            _log.info(f"[LIFECYCLE] Starting auto-stop timer for {duration} seconds")
            await asyncio.sleep(duration)
            _log.info("[LIFECYCLE] Auto-stop timer expired. Triggering deactivate.")
            await self.deactivate()
        except asyncio.CancelledError:
            _log.info("[LIFECYCLE] Auto-stop timer cancelled")
        except Exception as exc:
            _log.error(f"[LIFECYCLE] Auto-stop timer error: {exc}", exc_info=True)

    async def _auto_stop_backup_timer(self) -> None:
        try:
            duration = float(self._simulation_duration_s)
            backup_duration = duration + 30.0
            _log.info(f"[LIFECYCLE] Starting backup timer for {backup_duration} seconds")
            await asyncio.sleep(backup_duration)
            _log.warning("[LIFECYCLE] Backup timer expired. Forcing INACTIVE transition.")
            self._state = LifecycleState.INACTIVE
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
        except asyncio.CancelledError:
            _log.info("[LIFECYCLE] Backup timer cancelled")
        except Exception as exc:
            _log.error(f"[LIFECYCLE] Backup timer error: {exc}", exc_info=True)

    async def cleanup(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            if self._timer_task:
                self._timer_task.cancel()
                self._timer_task = None
            if self._backup_timer_task:
                self._backup_timer_task.cancel()
                self._backup_timer_task = None
            self._state = LifecycleState.UNCONFIGURED
            self._scenario_id = None
            await self._broadcast_transition(Transition.TRANSITION_CLEANUP)
        return res

    async def set_sim_rate(self, rate: float) -> LifecycleResult:
        """Inject sim_rate parameter dynamically into scenario_lifecycle_mgr."""
        try:
            await self._inject_params_to_node(
                "scenario_lifecycle_mgr",
                {"sim_rate": (float(rate), ParameterType.PARAMETER_DOUBLE)}
            )
            return LifecycleResult(success=True)
        except Exception as exc:
            _log.error("Failed to set sim_rate dynamic parameter: %s", exc)
            return LifecycleResult(success=False, error=str(exc))

    # ------------------------------------------------------------------
    # D1.8 encounter injection helpers
    # ------------------------------------------------------------------

    def _on_own_ship_state(self, msg) -> None:
        self._latest_own_ship = msg
        self._latest_own_ship_monotonic = time.monotonic()

    def get_latest_own_ship(self):
        return self._latest_own_ship

    def get_latest_own_ship_age_s(self):
        if self._latest_own_ship_monotonic is None:
            return None
        return time.monotonic() - self._latest_own_ship_monotonic

    async def add_target(self, mmsi: int, lat: float, lon: float,
                         heading_deg: float, sog_kn: float,
                         mode: str = "replay"):
        """Call /target_vessel_node/add_target with deadline polling."""
        if not self._add_target_client.wait_for_service(timeout_sec=3.0):
            raise RuntimeError("add_target service unavailable")
        req = AddTarget.Request()
        req.mmsi = int(mmsi)
        req.lat = float(lat)
        req.lon = float(lon)
        req.heading_deg = float(heading_deg)
        req.sog_kn = float(sog_kn)
        req.mode = mode
        future = self._add_target_client.call_async(req)
        deadline = 50
        while not future.done() and deadline > 0:
            await asyncio.sleep(0.1)
            deadline -= 1
        if not future.done():
            raise RuntimeError("add_target call timed out")
        return future.result()

    async def remove_target(self, mmsi: int):
        """Call /target_vessel_node/remove_target with deadline polling."""
        if not self._remove_target_client.wait_for_service(timeout_sec=3.0):
            raise RuntimeError("remove_target service unavailable")
        req = RemoveTarget.Request()
        req.mmsi = int(mmsi)
        future = self._remove_target_client.call_async(req)
        deadline = 50
        while not future.done() and deadline > 0:
            await asyncio.sleep(0.1)
            deadline -= 1
        if not future.done():
            raise RuntimeError("remove_target call timed out")
        return future.result()


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

    import math

    own_ship = yaml_data.get("ownShip", {})
    initial = own_ship.get("initial", {}) if isinstance(own_ship, dict) else {}
    pos = initial.get("position", {}) if isinstance(initial, dict) else {}

    metadata = yaml_data.get("metadata", {})
    sim_settings = metadata.get("simulation_settings", {}) if isinstance(metadata, dict) else {}
    coordinate_origin = sim_settings.get("coordinate_origin") if isinstance(sim_settings, dict) else None

    # ownShip.initial.position.{latitude,longitude} + {heading,sog,cog}
    ship_params: dict = {}
    lat_own = pos.get("latitude")
    lon_own = pos.get("longitude")

    if lat_own is not None and lon_own is not None:
        if coordinate_origin and isinstance(coordinate_origin, list) and len(coordinate_origin) >= 2:
            lat_origin = float(coordinate_origin[0])
            lon_origin = float(coordinate_origin[1])
        else:
            lat_origin = float(lat_own)
            lon_origin = float(lon_own)

        ship_params["origin_lat"] = (lat_origin, ParameterType.PARAMETER_DOUBLE)
        ship_params["origin_lon"] = (lon_origin, ParameterType.PARAMETER_DOUBLE)

        # y_offset represents North component, x_offset represents East component
        y_offset = (float(lat_own) - lat_origin) * 111120.0
        x_offset = (float(lon_own) - lon_origin) * 111120.0 * math.cos(math.radians(lat_origin))
        ship_params["x0"] = (x_offset, ParameterType.PARAMETER_DOUBLE)
        ship_params["y0"] = (y_offset, ParameterType.PARAMETER_DOUBLE)

    if isinstance(initial, dict):
        heading = initial.get("heading")
        if heading is None:
            heading = initial.get("cog")
        if heading is not None:
            # Convert nautical heading CW from North in degrees to math CCW from East in radians
            psi0_val = math.pi / 2.0 - math.radians(float(heading))
            ship_params["psi0"] = (psi0_val, ParameterType.PARAMETER_DOUBLE)

        sog = initial.get("sog")
        if sog is not None:
            # Convert knots to m/s
            u0_val = float(sog) * 0.5144
            ship_params["u0"] = (u0_val, ParameterType.PARAMETER_DOUBLE)

        if isinstance(sim_settings, dict):
            n_rps_init = sim_settings.get("n_rps_initial")
            if n_rps_init is not None:
                ship_params["n_rps_initial"] = (float(n_rps_init), ParameterType.PARAMETER_DOUBLE)

    if ship_params:
        injection_map["ship_dynamics_node"] = ship_params

    # NOTE: the sil_topic_bridge ownship_initial_{heading,sog} injection was
    # removed in Track A A5c (sil_topic_bridge.py deleted; its ROT-cascade logic
    # is gone). The new C++ adapters (sil_fusion/trace/pulse) take no scenario
    # parameters — they are pure relays. ownShip.initial is still consumed by
    # ship_dynamics_node (above) and target_vessel_node (below).

    # targetShips[] -> target_vessel_node -> default_targets_json
    target_ships = yaml_data.get("targetShips")
    if "targetShips" in yaml_data and isinstance(target_ships, list):
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

    # Seed plumb: root_seed from metadata.simulation_settings.seed
    sim_settings = metadata.get("simulation_settings", {}) if isinstance(metadata, dict) else {}
    root_seed = None
    if isinstance(sim_settings, dict):
        root_seed = sim_settings.get("seed")

    if root_seed is not None:
        for node in ("target_vessel_node", "sensor_mock_node", "env_disturbance_node", "fault_injection_node"):
            if node not in injection_map:
                injection_map[node] = {}
            injection_map[node]["root_seed"] = (int(root_seed), ParameterType.PARAMETER_INTEGER)

    return injection_map


def _filter_injection_params_for_runtime_profile(injection_map: dict,
                                                 runtime_profile: str) -> dict:
    """Remove injections for nodes that are absent in a runtime profile."""
    filtered = {
        node_name: dict(params)
        for node_name, params in injection_map.items()
    }
    if runtime_profile == "gnc":
        filtered.pop("ship_dynamics_node", None)
    return filtered


def _build_ship_reset(yaml_data: dict):
    """Build a sil_msgs/ShipReset from scenario ownShip.initial, or None if absent.

    The GNC plant (domain 50) is not reset by lifecycle/cross-run-reset; the
    orchestrator emits this on every GNC-profile configure so the plant's
    own-ship returns to the scenario start point (coordinate_transform origin
    + ship_dynamics eta_).
    """
    own = (yaml_data.get("ownShip", {}) if isinstance(yaml_data, dict) else {}).get("initial", {})
    pos = own.get("position", {})
    lat = pos.get("latitude")
    lon = pos.get("longitude")
    if lat is None or lon is None:
        return None
    msg = ShipReset()
    msg.latitude = float(lat)
    msg.longitude = float(lon)
    msg.heading_deg = float(own.get("heading", 0.0))
    msg.sog_kn = float(own.get("sog", 0.0))
    return msg


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
