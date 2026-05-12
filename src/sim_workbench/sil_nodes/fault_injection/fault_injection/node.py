"""Fault Injection Node — ais_dropout, radar_spike, disturbance_step.

Phase 1 pure-Python stub. Phase 2 wraps this logic in an rclpy Node
publishing/subscribing on ROS2 topics.
"""
import uuid
import time


class FaultInjectionNode:
    """Manage simulated sensor/disturbance faults for SIL testing.

    Fault types
    -----------
    - ``ais_dropout``         — AIS target stream temporarily silenced
    - ``radar_noise_spike``   — Burst of high-variance noise on radar channel
    - ``disturbance_step``    — Instantaneous step disturbance on plant input
    """

    FAULT_TYPES = ("ais_dropout", "radar_noise_spike", "disturbance_step")

    def __init__(self) -> None:
        self._active: dict[str, dict] = {}

    def trigger(self, fault_type: str, payload: dict | None = None) -> str:
        """Inject a fault and return a unique 8-char fault id."""
        if fault_type not in self.FAULT_TYPES:
            raise ValueError(f"Unknown fault type: {fault_type}")
        fid = uuid.uuid4().hex[:8]
        self._active[fid] = {
            "fault_id": fid,
            "fault_type": fault_type,
            "payload": payload or {},
            "stamp": time.time(),
        }
        return fid

    def list_active(self) -> list[dict]:
        """Return all currently active faults."""
        return list(self._active.values())

    def cancel(self, fault_id: str) -> bool:
        """Remove a fault by id. Returns False if not found."""
        if fault_id not in self._active:
            return False
        del self._active[fault_id]
        return True

    def get_active_types(self) -> list[str]:
        """Return the fault_type strings of all active faults."""
        return [f["fault_type"] for f in self._active.values()]


def main() -> None:
    """Entry point for ROS2 node. Phase 2: actual rclpy Node."""
    print("FaultInjectionNode ready (Phase 1 stub -- no ROS2 runtime)")


if __name__ == "__main__":
    main()
