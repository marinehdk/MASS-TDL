"""rclpy bridge — subscribes to UIState, publishes operator actions.

In unit tests, this is replaced by a FakeBridge fixture.
D1.3b extension: also subscribes to /l3/sat/data and /l3/m1/odd_state for SIL HMI.
"""
from __future__ import annotations

import asyncio
import logging
from datetime import datetime

logger = logging.getLogger("m8_web_backend.ros_bridge")

# Module-level asyncio loop reference — set by sil_ws on startup
_sil_broadcast_loop: asyncio.AbstractEventLoop | None = None


def set_sil_broadcast_loop(loop: asyncio.AbstractEventLoop) -> None:
    """Register the event loop for SIL HMI broadcasts (called by sil_ws.py)."""
    global _sil_broadcast_loop
    _sil_broadcast_loop = loop


class RosBridge:
    """Bridges FastAPI ↔ ROS2 via rclpy.

    Actual rclpy import is deferred so the module can be imported
    in environments without ROS2 installed (e.g. unit tests).
    """

    def __init__(self) -> None:
        self._ready = False
        self.latest_sat = None   # l3_msgs/SATData | None
        self.latest_odd = None   # l3_msgs/ODDState | None

    async def start(self) -> None:
        """Initialize rclpy node. Deferred so tests can monkeypatch."""
        try:
            import rclpy  # noqa: PLC0415
            from l3_msgs.msg import ODDState, SATData  # noqa: PLC0415
            rclpy.init()
            self._node = rclpy.create_node("m8_ros_bridge")
            self._sub_sat = self._node.create_subscription(
                SATData, "/l3/sat/data", self._on_sat_data, 10
            )
            self._sub_odd = self._node.create_subscription(
                ODDState, "/l3/m1/odd_state", self._on_odd_state, 10
            )
            self._ready = True
            logger.info("rclpy initialized with SIL subscriptions")
        except ImportError:
            logger.warning("rclpy not available — running in bridge-less mode")

    async def stop(self) -> None:
        try:
            import rclpy  # noqa: PLC0415
            rclpy.shutdown()
        except (ImportError, RuntimeError):
            pass
        self._ready = False

    def is_ready(self) -> bool:
        return self._ready

    def _on_sat_data(self, msg: object) -> None:
        """Handle SAT data subscription."""
        self.latest_sat = msg
        self._trigger_sil_broadcast()

    def _on_odd_state(self, msg: object) -> None:
        """Handle ODD state subscription."""
        self.latest_odd = msg
        self._trigger_sil_broadcast()

    def _trigger_sil_broadcast(self) -> None:
        """Asynchronously broadcast SIL state to HMI via websocket."""
        if _sil_broadcast_loop is None:
            return
        try:
            from web_server import sil_ws  # noqa: PLC0415
            asyncio.run_coroutine_threadsafe(
                sil_ws.broadcast_sil_state(self.latest_sat, self.latest_odd),
                _sil_broadcast_loop,
            )
        except Exception as exc:
            logger.debug("SIL broadcast error (non-critical): %s", exc)

    def send_operator_action(
        self,
        action_type: str,
        operator_id: str,
        click_time: datetime,
    ) -> bool:
        """Publish operator action to /l3/m8/_internal/operator_action.

        Returns False if not ready or action rejected by C++ node.
        """
        if not self._ready:
            return False
        # TODO(Phase-E2): publish OperatorAction ROS2 message via rclpy and return False
        #   if the C++ TorProtocol rejects the click (duplicate or wrong state).
        logger.info("Sending operator action: %s from %s", action_type, operator_id)
        return True
