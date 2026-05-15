"""Fault Injection Node — ais_dropout, radar_spike, disturbance_step.

Phase 1 pure-Python stub. Phase 2 wraps this logic in an rclpy Node
publishing/subscribing on ROS2 topics.
"""

import json
import time
import uuid

import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from sil_msgs.msg import FaultEvent

# fault_type names differ from topic names (radar_noise_spike → radar_spike)
_FAULT_TOPICS = {
    "ais_dropout": "/sil/fault/ais_dropout",
    "radar_noise_spike": "/sil/fault/radar_spike",
    "disturbance_step": "/sil/fault/dist_step",
}


class FaultInjectionNode(LifecycleNode):
    """Manage simulated sensor/disturbance faults for SIL testing.

    Fault types
    -----------
    - ``ais_dropout``         -- AIS target stream temporarily silenced
    - ``radar_noise_spike``   — Burst of high-variance noise on radar channel
    - ``disturbance_step``    -- Instantaneous step disturbance on plant input
    """

    FAULT_TYPES = ("ais_dropout", "radar_noise_spike", "disturbance_step")

    def __init__(self) -> None:
        super().__init__("fault_injection_node")
        self._active: dict[str, dict] = {}
        self._publishers: dict[str, rclpy.publisher.Publisher] = {}

    def on_configure(self, state) -> TransitionCallbackReturn:
        self.declare_parameter("fault_types", list(self.FAULT_TYPES))
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state) -> TransitionCallbackReturn:
        qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            depth=10,
        )
        for ft in self.FAULT_TYPES:
            topic = _FAULT_TOPICS[ft]
            self._publishers[ft] = self.create_publisher(FaultEvent, topic, qos)
        return super().on_activate(state)

    def on_deactivate(self, state) -> TransitionCallbackReturn:
        for pub in self._publishers.values():
            self.destroy_publisher(pub)
        self._publishers.clear()
        return super().on_deactivate(state)

    def on_cleanup(self, state) -> TransitionCallbackReturn:
        self._active.clear()
        return TransitionCallbackReturn.SUCCESS

    def _publish_fault(self, fault_type: str, payload: dict) -> None:
        msg = FaultEvent()
        msg.stamp = self.get_clock().now().to_msg()
        msg.fault_type = fault_type
        msg.payload_json = json.dumps(payload)
        pub = self._publishers.get(fault_type)
        if pub is not None:
            pub.publish(msg)

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
        self._publish_fault(fault_type, payload or {})
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


def main(args=None) -> None:
    rclpy.init(args=args)
    node = FaultInjectionNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
