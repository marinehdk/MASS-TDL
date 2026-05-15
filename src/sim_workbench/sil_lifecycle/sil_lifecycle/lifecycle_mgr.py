"""Scenario Lifecycle Manager — LifecycleNode wrapping 5-state FSM.

States: UNCONFIGURED -> INACTIVE -> ACTIVE -> DEACTIVATING -> FINALIZED
Publishes /sim_clock @ 1kHz and /sil/lifecycle_status @ 1Hz.

Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md Sec3
"""

import time
from enum import IntEnum

import rclpy
from rclpy.lifecycle import LifecycleNode, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy

from builtin_interfaces.msg import Time as TimeMsg
from sil_msgs.msg import LifecycleStatus


# ── QoS profiles per Doc 2 §7.3 ────────────────────────────────────────────

_SIM_CLOCK_QOS = QoSProfile(
    depth=10,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)

_STATUS_QOS = QoSProfile(
    depth=5,
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.TRANSIENT_LOCAL,
    history=HistoryPolicy.KEEP_LAST,
)


# ── Enums (preserved from v0 stub) ──────────────────────────────────────────


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


# ── Pure-Python FSM (preserved from v0 stub) ────────────────────────────────


class ScenarioLifecycleMgr:
    """Pure-Python lifecycle FSM for offline testing and Phase 1 mock.

    Phase 2: wrapped by LifecycleManagerNode.
    Phase 1: used directly by orchestrator lifecycle_bridge.
    """

    def __init__(self, tick_hz: float = 50.0) -> None:
        self._state = LifecycleState.UNCONFIGURED
        self._scenario_id: str = ""
        self._scenario_hash: str = ""
        self._tick_hz = tick_hz
        self._sim_rate: float = 1.0
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
    def wall_time(self) -> float:
        """Elapsed wall-clock time since activation (seconds)."""
        if self._wall_start > 0:
            return time.time() - self._wall_start
        return 0.0

    def configure(self, scenario_id: str, scenario_hash: str = "") -> bool:
        if self._state != LifecycleState.UNCONFIGURED:
            return False
        self._scenario_id = scenario_id
        self._scenario_hash = scenario_hash
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

    def tick(self) -> None:
        """Advance simulation time by one tick (called at tick_hz)."""
        if self._state == LifecycleState.ACTIVE:
            self._sim_time += (1.0 / self._tick_hz) * self._sim_rate

    def get_status_dict(self) -> dict:
        return {
            "current_state": self._state.name,
            "scenario_id": self._scenario_id,
            "scenario_hash": self._scenario_hash,
            "sim_time": self._sim_time,
            "wall_time": self.wall_time,
            "sim_rate": self._sim_rate,
        }


# ── LifecycleNode (ROS2 wrapper) ────────────────────────────────────────────


class LifecycleManagerNode(LifecycleNode):
    """rclpy LifecycleNode wrapping ScenarioLifecycleMgr.

    Lifecycle callbacks:
      on_configure  → declare params, init FSM
      on_activate   → create publishers + timers
      on_deactivate → destroy publishers + timers
      on_cleanup    → reset FSM state
    """

    def __init__(self, node_name: str = "scenario_lifecycle_mgr") -> None:
        super().__init__(node_name)
        self._fsm = ScenarioLifecycleMgr()

        # ROS2 resources — created in on_activate, destroyed in on_deactivate
        self._sim_clock_pub = None
        self._status_pub = None
        self._sim_clock_timer = None
        self._status_timer = None

    # ── Lifecycle callbacks ──────────────────────────────────────────────

    def on_configure(self, state) -> TransitionCallbackReturn:
        """Declare ROS params and transition FSM UNCONFIGURED → INACTIVE."""
        self.declare_parameter("scenario_id", "")
        self.declare_parameter("scenario_hash", "")
        self.declare_parameter("tick_hz", 1000.0)
        self.declare_parameter("status_hz", 1.0)

        sid = self.get_parameter("scenario_id").value
        shash = self.get_parameter("scenario_hash").value
        self._fsm._tick_hz = self.get_parameter("tick_hz").value

        self._fsm.configure(str(sid), str(shash))
        self.get_logger().info(
            f"[on_configure] scenario_id={sid} scenario_hash={shash}"
        )
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state) -> TransitionCallbackReturn:
        """Create publishers + timers; transition FSM INACTIVE → ACTIVE."""
        # Publishers
        self._sim_clock_pub = self.create_publisher(
            TimeMsg, "/sim_clock", qos_profile=_SIM_CLOCK_QOS
        )
        self._status_pub = self.create_publisher(
            LifecycleStatus, "/sil/lifecycle_status", qos_profile=_STATUS_QOS
        )

        # Timers
        tick_hz = self.get_parameter("tick_hz").value
        status_hz = self.get_parameter("status_hz").value

        self._sim_clock_timer = self.create_timer(
            timer_period_sec=1.0 / float(tick_hz),
            callback=self._clock_callback,
        )
        self._status_timer = self.create_timer(
            timer_period_sec=1.0 / float(status_hz),
            callback=self._status_callback,
        )

        self._fsm.activate()
        self.get_logger().info(
            f"[on_activate] sim_clock @ {tick_hz:.0f} Hz  "
            f"status @ {status_hz:.1f} Hz"
        )
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state) -> TransitionCallbackReturn:
        """Destroy timers + publishers; transition FSM ACTIVE → INACTIVE."""
        self._fsm.deactivate()

        for timer in (self._sim_clock_timer, self._status_timer):
            if timer is not None:
                self.destroy_timer(timer)
        self._sim_clock_timer = None
        self._status_timer = None

        for pub in (self._sim_clock_pub, self._status_pub):
            if pub is not None:
                self.destroy_publisher(pub)
        self._sim_clock_pub = None
        self._status_pub = None

        self.get_logger().info("[on_deactivate] timers + publishers destroyed")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state) -> TransitionCallbackReturn:
        """Reset FSM state INACTIVE → UNCONFIGURED."""
        self._fsm.cleanup()
        self.get_logger().info("[on_cleanup] FSM reset to UNCONFIGURED")
        return TransitionCallbackReturn.SUCCESS

    # ── Timer callbacks ──────────────────────────────────────────────────

    def _clock_callback(self) -> None:
        """Publish /sim_clock as builtin_interfaces/Time @ tick_hz."""
        self._fsm.tick()
        sim_t = self._fsm.sim_time
        msg = TimeMsg()
        msg.sec = int(sim_t)
        msg.nanosec = int((sim_t - msg.sec) * 1e9)
        self._sim_clock_pub.publish(msg)

    def _status_callback(self) -> None:
        """Publish /sil/lifecycle_status @ status_hz."""
        msg = LifecycleStatus()
        msg.stamp = self.get_clock().now().to_msg()
        msg.current_state = self._fsm.current_state.value
        msg.scenario_id = self._fsm.scenario_id
        msg.scenario_hash = self._fsm.scenario_hash
        msg.sim_time = self._fsm.sim_time
        msg.wall_time = self._fsm.wall_time
        msg.sim_rate = self._fsm.sim_rate
        self._status_pub.publish(msg)


# ── Entry point ─────────────────────────────────────────────────────────────


def main():
    """ROS2 entry point: spin LifecycleManagerNode."""
    rclpy.init()
    node = LifecycleManagerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
