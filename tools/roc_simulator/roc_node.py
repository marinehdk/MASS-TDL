"""
roc_node.py — ROS2 Python node for the ROC HMI Simulator.

Subscribes to M8 transparency outputs and can respond to ToR requests,
simulating a Remote Operations Center operator during HIL tests.

Node name : roc_hmi_simulator
Topics in : /l3/m8/ui_state      (l3_msgs/UIState)
             /l3/m8/tor_request   (l3_msgs/ToRRequest)
             /l3/m8/sat_data      (l3_msgs/SATData)
Topic out  : /l3/roc/tor_response (std_msgs/String)
"""

import threading
import time
import logging

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

# l3_msgs — imported with graceful fallback so the module stays importable in
# offline unit-test environments where l3_msgs is not installed.
try:
    from l3_msgs.msg import UIState, ToRRequest, SATData
    _L3_MSGS_AVAILABLE = True
except ImportError:
    _L3_MSGS_AVAILABLE = False

logger = logging.getLogger(__name__)

# Auto-response delay in seconds
AUTO_RESPONSE_DELAY_S = 5.0


class RocHmiSimulatorNode(Node):
    """ROS2 node that acts as the ROC operator interface during HIL tests."""

    def __init__(self, auto_response_mode: bool = False):
        super().__init__("roc_hmi_simulator")

        self._lock = threading.Lock()
        self._auto_response_mode = auto_response_mode

        # Shared state consumed by the web server
        self._state: dict = {
            "ui_state": None,
            "sat_data": None,
            "tor_request": None,
            "tor_received_at": None,
            "tor_response": None,
        }

        # Publisher
        self._tor_pub = self.create_publisher(String, "/l3/roc/tor_response", 10)

        # Subscribers — skip if l3_msgs not available
        if _L3_MSGS_AVAILABLE:
            self.create_subscription(
                UIState,
                "/l3/m8/ui_state",
                self._on_ui_state,
                50,
            )
            self.create_subscription(
                ToRRequest,
                "/l3/m8/tor_request",
                self._on_tor_request,
                10,
            )
            self.create_subscription(
                SATData,
                "/l3/m8/sat_data",
                self._on_sat_data,
                10,
            )
            self.get_logger().info("roc_hmi_simulator: subscriptions active")
        else:
            self.get_logger().warn(
                "l3_msgs not available — running in stub mode, no subscriptions created"
            )

    # ------------------------------------------------------------------
    # Subscription callbacks
    # ------------------------------------------------------------------

    def _on_ui_state(self, msg: "UIState") -> None:
        ui_dict = {
            "stamp": _stamp_to_float(msg.stamp),
            "view_mode": msg.view_mode,
            "ship_latitude_deg": msg.ship_latitude_deg,
            "ship_longitude_deg": msg.ship_longitude_deg,
            "ship_heading_deg": msg.ship_heading_deg,
            "ship_sog_kn": msg.ship_sog_kn,
            "auto_level_text": msg.auto_level_text,
            "active_alert_count": msg.active_alert_count,
            "critical_alert_count": msg.critical_alert_count,
            "confidence": msg.confidence,
            "rationale": msg.rationale,
        }
        with self._lock:
            self._state["ui_state"] = ui_dict

    def _on_tor_request(self, msg: "ToRRequest") -> None:
        tor_dict = {
            "stamp": _stamp_to_float(msg.stamp),
            "reason": msg.reason,
            "reason_text": _tor_reason_text(msg.reason),
            "deadline_s": msg.deadline_s,
            "target_level": msg.target_level,
            "confidence": msg.confidence,
            "context_summary": msg.context_summary,
            "recommended_action": msg.recommended_action,
        }
        with self._lock:
            already_pending = self._state["tor_request"] is not None and self._state["tor_response"] is None
            self._state["tor_request"] = tor_dict
            self._state["tor_received_at"] = time.time()
            self._state["tor_response"] = None

        self.get_logger().info(
            f"ToR received: reason={tor_dict['reason_text']} deadline={msg.deadline_s}s"
        )

        if self._auto_response_mode and not already_pending:
            # Schedule auto-ACCEPT in a daemon thread so we don't block the spin
            t = threading.Timer(AUTO_RESPONSE_DELAY_S, self._auto_accept)
            t.daemon = True
            t.start()

    def _on_sat_data(self, msg: "SATData") -> None:
        sat_dict = {
            "stamp": _stamp_to_float(msg.stamp),
            "source_module": msg.source_module,
            "sat1": _sat1_to_dict(msg.sat1) if msg.sat1 else None,
            "sat2": _sat2_to_dict(msg.sat2) if msg.sat2 else None,
            "sat3": _sat3_to_dict(msg.sat3) if msg.sat3 else None,
        }
        with self._lock:
            self._state["sat_data"] = sat_dict

    # ------------------------------------------------------------------
    # Public API (called from web_server.py)
    # ------------------------------------------------------------------

    def respond_to_tor(self, response: str) -> bool:
        """
        Publish a ToR response and record it in state.

        Args:
            response: "ACCEPT" | "REJECT" | "DEFER"

        Returns:
            True if published, False if no pending ToR.
        """
        response = response.upper()
        if response not in ("ACCEPT", "REJECT", "DEFER"):
            logger.warning("respond_to_tor: invalid response %r", response)
            return False

        with self._lock:
            if self._state["tor_request"] is None:
                self.get_logger().warn("respond_to_tor called but no pending ToR")
                return False
            self._state["tor_response"] = response

        msg = String()
        msg.data = response
        self._tor_pub.publish(msg)
        self.get_logger().info(f"ToR response published: {response}")
        return True

    def get_state(self) -> dict:
        """Return a shallow copy of the current state (thread-safe)."""
        with self._lock:
            return dict(self._state)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _auto_accept(self) -> None:
        """Auto-ACCEPT callback triggered after AUTO_RESPONSE_DELAY_S seconds."""
        with self._lock:
            if self._state["tor_response"] is not None:
                # Already responded manually
                return
        self.get_logger().info(
            f"auto_response_mode: auto-ACCEPT after {AUTO_RESPONSE_DELAY_S}s"
        )
        self.respond_to_tor("ACCEPT")


# ------------------------------------------------------------------
# Helper functions
# ------------------------------------------------------------------

def _stamp_to_float(stamp) -> float:
    """Convert builtin_interfaces/Time to a Unix-like float."""
    try:
        return float(stamp.sec) + float(stamp.nanosec) * 1e-9
    except Exception:
        return 0.0


def _tor_reason_text(reason: int) -> str:
    return {0: "ODD_EXIT", 1: "MANUAL_REQUEST", 2: "SAFETY_ALERT"}.get(reason, f"UNKNOWN({reason})")


def _sat1_to_dict(sat1) -> dict:
    return {
        "current_state": getattr(sat1, "current_state", ""),
        "confidence": getattr(sat1, "confidence", 0.0),
        "rationale": getattr(sat1, "rationale", ""),
    }


def _sat2_to_dict(sat2) -> dict:
    return {
        "reasoning": getattr(sat2, "reasoning", ""),
        "triggered": getattr(sat2, "triggered", False),
        "trigger_source": getattr(sat2, "trigger_source", ""),
    }


def _sat3_to_dict(sat3) -> dict:
    return {
        "predicted_state": getattr(sat3, "predicted_state", ""),
        "horizon_s": getattr(sat3, "horizon_s", 0.0),
        "uncertainty": getattr(sat3, "uncertainty", 0.0),
        "tdl_summary": getattr(sat3, "tdl_summary", ""),
    }


# ------------------------------------------------------------------
# Standalone entry point (used by launch_roc.py)
# ------------------------------------------------------------------

_node_instance: RocHmiSimulatorNode | None = None


def start_node(auto_response_mode: bool = False) -> RocHmiSimulatorNode:
    """
    Initialise rclpy, create the node, and spin in the calling thread.

    This function blocks until rclpy is shut down.  Call it from the main
    thread (or a dedicated spin thread).
    """
    global _node_instance
    rclpy.init()
    _node_instance = RocHmiSimulatorNode(auto_response_mode=auto_response_mode)
    try:
        rclpy.spin(_node_instance)
    except KeyboardInterrupt:
        pass
    finally:
        _node_instance.destroy_node()
        rclpy.shutdown()
    return _node_instance


def get_node() -> RocHmiSimulatorNode | None:
    """Return the singleton node instance (may be None before start_node())."""
    return _node_instance


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="ROC HMI Simulator ROS2 node")
    parser.add_argument(
        "--auto-respond",
        action="store_true",
        help=f"Auto-ACCEPT any ToR after {AUTO_RESPONSE_DELAY_S}s (for automated HIL runs)",
    )
    args = parser.parse_args()
    start_node(auto_response_mode=args.auto_respond)
