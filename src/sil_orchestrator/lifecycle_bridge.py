"""Bridge between FastAPI REST and ROS2 lifecycle services.

Controls scenario_lifecycle_mgr as primary node, then broadcasts configure/
activate/deactivate/cleanup to all SIL lifecycle nodes (ship_dynamics, scoring,
target_vessel, etc.) on a best-effort basis so the full SIL stack comes alive
with a single orchestrator call.
"""

import asyncio
import rclpy
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

    async def _change_state(self, transition_id: int) -> LifecycleResult:
        if not self.change_state_client.wait_for_service(timeout_sec=2.0):
            return LifecycleResult(
                success=False,
                error="Lifecycle service /scenario_lifecycle_mgr/change_state not available")

        req = ChangeState.Request()
        req.transition.id = transition_id

        try:
            future = self.change_state_client.call_async(req)
            while not future.done():
                await asyncio.sleep(0.1)
            response = future.result()
            if response.success:
                return LifecycleResult(success=True)
            return LifecycleResult(success=False, error="Transition rejected by node")
        except Exception as exc:
            return LifecycleResult(success=False, error=str(exc))

    async def _broadcast_transition(self, transition_id: int) -> None:
        """Send transition to every SIL lifecycle node; log failures, never raise."""
        for node_name, client in self._sil_change_state_clients.items():
            try:
                if not client.wait_for_service(timeout_sec=0.5):
                    _log.debug("broadcast: /%s/change_state not available — skipping", node_name)
                    continue
                req = ChangeState.Request()
                req.transition.id = transition_id
                future = client.call_async(req)
                # Wait at most 2 s per node so broadcast can't block the main call
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

    async def configure(self, scenario_id: str) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
            asyncio.ensure_future(self._broadcast_transition(Transition.TRANSITION_CONFIGURE))
        return res

    async def activate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_ACTIVATE)
        if res.success:
            self._state = LifecycleState.ACTIVE
            asyncio.ensure_future(self._broadcast_transition(Transition.TRANSITION_ACTIVATE))
        return res

    async def deactivate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
        if res.success:
            self._state = LifecycleState.INACTIVE
            asyncio.ensure_future(self._broadcast_transition(Transition.TRANSITION_DEACTIVATE))
        return res

    async def cleanup(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            self._state = LifecycleState.UNCONFIGURED
            self._scenario_id = None
            asyncio.ensure_future(self._broadcast_transition(Transition.TRANSITION_CLEANUP))
        return res

