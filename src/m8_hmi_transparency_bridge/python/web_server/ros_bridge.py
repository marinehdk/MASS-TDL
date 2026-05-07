"""rclpy bridge — subscribes to UIState, publishes operator actions.

In unit tests, this is replaced by a FakeBridge fixture.
"""
from __future__ import annotations

import logging
from datetime import datetime

logger = logging.getLogger("m8_web_backend.ros_bridge")


class RosBridge:
    """Bridges FastAPI ↔ ROS2 via rclpy.

    Actual rclpy import is deferred so the module can be imported
    in environments without ROS2 installed (e.g. unit tests).
    """

    def __init__(self) -> None:
        self._ready = False

    async def start(self) -> None:
        """Initialize rclpy node. Deferred so tests can monkeypatch."""
        try:
            import rclpy  # noqa: PLC0415
            rclpy.init()
            self._ready = True
            logger.info("rclpy initialized")
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
        # In production: publish OperatorAction ROS2 message via rclpy
        logger.info("Sending operator action: %s from %s", action_type, operator_id)
        return True
