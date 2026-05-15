"""Scenario Lifecycle Manager — 5-state FSM coordinating 9 business nodes.

States: UNCONFIGURED -> INACTIVE -> ACTIVE -> DEACTIVATING -> FINALIZED
Maps to FE screens: (1) UNCONFIGURED -> (2) INACTIVE -> (3) ACTIVE -> (4) INACTIVE

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md Sec3
"""

import time
from enum import IntEnum


class LifecycleState(IntEnum):
    """Primary states matching lifecycle_msgs/State constants."""
    UNCONFIGURED = 0
    INACTIVE = 1
    ACTIVE = 2
    DEACTIVATING = 3
    FINALIZED = 4


class Transition(IntEnum):
    """Transition identifiers matching lifecycle_msgs/Transition."""
    CONFIGURE = 1
    ACTIVATE = 3
    DEACTIVATE = 4
    CLEANUP = 6


class ScenarioLifecycleMgr:
    """Pure-Python lifecycle FSM for offline testing and Phase 1 mock.

    Phase 2: wraps this logic in an rclpy Node with actual service endpoints.
    Phase 1: used directly by orchestrator lifecycle_bridge.
    """

    def __init__(self, tick_hz: float = 50.0) -> None:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str = ""
        self._scenario_hash: str = ""
        self._tick_hz = tick_hz
        self._sim_rate: float = 1.0
        self._dynamics_mode: str = "internal"  # "internal" | "fmi"
        self._sim_time: float = 0.0
        self._wall_start: float = 0.0

    @property
    def current_state(self) -> LifecycleState:
        return self._state

    @property
    def scenario_id(self) -> str:
        return self._scenario_id

    @property
    def scenario_hash(self) -> str:
        return self._scenario_hash

    @property
    def sim_time(self) -> float:
        return self._sim_time

    @property
    def sim_rate(self) -> float:
        return self._sim_rate

    @property
    def dynamics_mode(self) -> str:
        return self._dynamics_mode

    def configure(self, scenario_id: str, scenario_hash: str = "",
                  dynamics_mode: str = "internal") -> bool:
        if self._state != LifecycleState.UNCONFIGURED:
            return False
        if dynamics_mode not in ("internal", "fmi"):
            return False
        self._scenario_id = scenario_id
        self._scenario_hash = scenario_hash
        self._dynamics_mode = dynamics_mode
        self._state = LifecycleState.INACTIVE
        return True

    def activate(self) -> bool:
        if self._state != LifecycleState.INACTIVE:
            return False
        self._state = LifecycleState.ACTIVE
        self._wall_start = time.time()
        return True

    def deactivate(self) -> bool:
        if self._state != LifecycleState.ACTIVE:
            return False
        self._state = LifecycleState.DEACTIVATING
        # Simulate deactivation work
        self._state = LifecycleState.INACTIVE
        return True

    def cleanup(self) -> bool:
        if self._state not in (LifecycleState.INACTIVE, LifecycleState.FINALIZED):
            return False
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id = ""
        self._scenario_hash = ""
        return True

    def set_sim_rate(self, rate: float) -> bool:
        if rate < 0:
            return False
        self._sim_rate = rate
        return True

    def set_dynamics_mode(self, mode: str) -> bool:
        """Switch between internal sim and FMI co-sim mode.

        Args:
            mode: "internal" (Phase 1 default) or "fmi" (Phase 2+)

        Returns:
            True if mode was accepted (only in INACTIVE or UNCONFIGURED state)
        """
        if self._state not in (LifecycleState.INACTIVE, LifecycleState.UNCONFIGURED):
            return False
        if mode not in ("internal", "fmi"):
            return False
        self._dynamics_mode = mode
        return True

    def tick(self) -> None:
        """Advance simulation time by one tick (called at tick_hz)."""
        if self._state == LifecycleState.ACTIVE:
            self._sim_time += (1.0 / self._tick_hz) * self._sim_rate

    def get_status_dict(self) -> dict:
        return {
            "current_state": self._state.name,
            "scenario_id": self._scenario_id,
            "scenario_hash": self._scenario_hash,
            "dynamics_mode": self._dynamics_mode,
            "sim_time": self._sim_time,
            "wall_time": time.time() - self._wall_start if self._wall_start > 0 else 0.0,
            "sim_rate": self._sim_rate,
        }


def main():
    """Entry point for ROS2 node. Phase 2: actual rclpy Node."""
    print("ScenarioLifecycleMgr ready (Phase 1 stub -- no ROS2 runtime)")


if __name__ == "__main__":
    main()
