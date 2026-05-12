"""Bridge between FastAPI REST and ROS2 lifecycle services.

Phase 1: mock implementation (no ROS2 node running yet).
Phase 2 (Week 3): replace with actual rclpy service clients.
"""

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


class LifecycleBridge:
    """Thin wrapper over rclpy lifecycle service calls."""

    def __init__(self) -> None:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str | None = None

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str | None:
        return self._scenario_id

    async def configure(self, scenario_id: str) -> LifecycleResult:
        if self._state != LifecycleState.UNCONFIGURED:
            return LifecycleResult(
                success=False,
                error=f"Cannot configure from {self._state.value}",
            )
        self._scenario_id = scenario_id
        self._state = LifecycleState.INACTIVE
        return LifecycleResult(success=True)

    async def activate(self) -> LifecycleResult:
        if self._state != LifecycleState.INACTIVE:
            return LifecycleResult(
                success=False,
                error=f"Cannot activate from {self._state.value}",
            )
        self._state = LifecycleState.ACTIVE
        return LifecycleResult(success=True)

    async def deactivate(self) -> LifecycleResult:
        if self._state != LifecycleState.ACTIVE:
            return LifecycleResult(
                success=False,
                error=f"Cannot deactivate from {self._state.value}",
            )
        self._state = LifecycleState.INACTIVE
        return LifecycleResult(success=True)

    async def cleanup(self) -> LifecycleResult:
        if self._state not in (LifecycleState.INACTIVE, LifecycleState.FINALIZED):
            return LifecycleResult(
                success=False,
                error=f"Cannot cleanup from {self._state.value}",
            )
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id = None
        return LifecycleResult(success=True)
