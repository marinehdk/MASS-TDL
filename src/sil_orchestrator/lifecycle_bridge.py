"""Bridge between FastAPI REST and ROS2 lifecycle services.

Phase 1: mock implementation (no ROS2 node running yet).
Phase 2 (Week 3): replace with actual rclpy service clients.
"""

import asyncio
import rclpy
from rclpy.node import Node
from lifecycle_msgs.srv import ChangeState, GetState
from lifecycle_msgs.msg import Transition
from dataclasses import dataclass
from enum import Enum


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
    """Real rclpy wrapper over ROS2 lifecycle service calls."""

    def __init__(self, callback_group=None) -> None:
        super().__init__('sil_orchestrator_lifecycle_bridge')
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str | None = None
        
        # ROS 2 service clients
        self.change_state_client = self.create_client(ChangeState, '/scenario_lifecycle_mgr/change_state', callback_group=callback_group)
        self.get_state_client = self.create_client(GetState, '/scenario_lifecycle_mgr/get_state', callback_group=callback_group)

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str | None:
        return self._scenario_id

    async def _change_state(self, transition_id: int) -> LifecycleResult:
        if not self.change_state_client.wait_for_service(timeout_sec=2.0):
            return LifecycleResult(success=False, error="Lifecycle service /scenario_lifecycle_mgr/change_state not available")
        
        req = ChangeState.Request()
        req.transition.id = transition_id
        
        try:
            # We use an executor or asyncio wrapper to await the ROS 2 future
            future = self.change_state_client.call_async(req)
            while not future.done():
                await asyncio.sleep(0.1)
            response = future.result()
            if response.success:
                return LifecycleResult(success=True)
            else:
                return LifecycleResult(success=False, error="Transition rejected by node")
        except Exception as e:
            return LifecycleResult(success=False, error=str(e))

    async def configure(self, scenario_id: str) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CONFIGURE)
        if res.success:
            self._scenario_id = scenario_id
            self._state = LifecycleState.INACTIVE
        return res

    async def activate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_ACTIVATE)
        if res.success:
            self._state = LifecycleState.ACTIVE
        return res

    async def deactivate(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_DEACTIVATE)
        if res.success:
            self._state = LifecycleState.INACTIVE
        return res

    async def cleanup(self) -> LifecycleResult:
        res = await self._change_state(Transition.TRANSITION_CLEANUP)
        if res.success:
            self._state = LifecycleState.UNCONFIGURED
            self._scenario_id = None
        return res

