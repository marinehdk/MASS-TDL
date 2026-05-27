#!/usr/bin/env python3
"""SIL Diagnostic Mock Publisher — emits healthy /diagnostics for clean scenarios.

F1b root cause:
  M1 OddEnvelopeManagerNode.extract_diagnostics() reads /diagnostics
  (diagnostic_msgs/DiagnosticArray). When the topic has zero publishers
  (default state in SIL), the DiagExtract struct defaults to
  {radar_health_ok=false, comm_ok=false, tmr_available=false}. M1 then
  emits zone_reason="radar_degraded; comm_degraded; tmr_unavailable;"
  and slides envelope_state into MRC_PREP, which cascades to:
    M7 SafetyAlert(MRC_REQUIRED) → M3 RouteReplanRequest → REPLAN_WAIT
    → no mission_state/goal → M4 IvP fallback → no avoidance.

This mock publishes diagnostic_msgs/DiagnosticStatus entries for the
three sensor names M1 looks for ("radar", "comm", "tmr") with level=OK
at 2 Hz. Scenario YAML may override by setting
``metadata.simulation_settings.sensor_health_override`` to a dict
(future enhancement — D2.1 HAZID-aware degradation injection).

Topology:
  Publisher  /diagnostics (diagnostic_msgs/DiagnosticArray) @ 2 Hz
  No subscribers; pure source node.

Replacement path (Phase 2):
  When sensor_mock_node (src/sim_workbench/sil_nodes/sensor_mock/) gains
  real per-sensor health logic (D2.1), this script is retired. Until
  then it provides the static healthy baseline imazu / clean scenarios
  expect (radar OK, comms OK, TMR available).
"""

import signal
import sys
from typing import Optional, List, Tuple

import rclpy
from rclpy.node import Node
from rclpy.qos import (
    QoSProfile,
    QoSReliabilityPolicy,
    QoSDurabilityPolicy,
    QoSHistoryPolicy,
)

from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue

PUBLISH_HZ = 2.0


class DiagnosticMockPublisher(Node):
    def __init__(self):
        super().__init__("diagnostic_mock_publisher")

        # M1 subscribes RELIABLE; we use RELIABLE+VOLATILE+KEEP_LAST(10).
        diag_qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10,
        )
        # M1 OddEnvelopeManagerNode subscribes to /l3/diagnostics
        # (kTopicDiagnostics in odd_envelope_manager_node.cpp:98), NOT the
        # standard /diagnostics topic. Mirror M1's exact topic name.
        self._pub = self.create_publisher(
            DiagnosticArray, "/l3/diagnostics", diag_qos)

        self._timer = self.create_timer(1.0 / PUBLISH_HZ, self._on_timer)
        self._tick = 0

        self.get_logger().info(
            f"DiagnosticMockPublisher ready — emitting healthy /diagnostics "
            f"@ {PUBLISH_HZ} Hz (radar/comm/tmr all OK)"
        )

    def _make_status(self, name: str, hardware_id: str = "sil_mock",
                     extra: Optional[List[Tuple[str, str]]] = None) -> DiagnosticStatus:
        s = DiagnosticStatus()
        s.level = DiagnosticStatus.OK
        s.name = name
        s.hardware_id = hardware_id
        s.message = "operational (SIL mock)"
        if extra:
            for k, v in extra:
                kv = KeyValue()
                kv.key = k
                kv.value = v
                s.values.append(kv)
        return s

    def _on_timer(self):
        self._tick += 1
        msg = DiagnosticArray()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "sil_diagnostic_mock"

        # M1's extract_diagnostics() does substring match: "radar", "comm", "tmr".
        # comm status carries delay_s key/value (M1 reads it for comm_delay_s).
        msg.status.append(
            self._make_status("sil_mock/radar_health",
                              extra=[("range_nm_max", "12.0"),
                                     ("update_rate_hz", "5.0")]))
        msg.status.append(
            self._make_status("sil_mock/comm_link",
                              extra=[("delay_s", "0.05"),
                                     ("packet_loss_pct", "0.0")]))
        msg.status.append(
            self._make_status("sil_mock/tmr_voting",
                              extra=[("active_lanes", "3"),
                                     ("voted_health", "OK")]))

        self._pub.publish(msg)

        if self._tick == 1 or self._tick % (int(PUBLISH_HZ) * 60) == 0:
            self.get_logger().info(
                f"Published healthy diagnostics tick={self._tick} "
                f"(3 status entries: radar / comm / tmr all OK)"
            )


def main():
    rclpy.init()
    node = DiagnosticMockPublisher()

    def _shutdown(signum, _frame):
        node.get_logger().info(f"Caught signal {signum}, shutting down")
        rclpy.shutdown()
        sys.exit(0)

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            node.destroy_node()
        except Exception:
            pass
        try:
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == "__main__":
    main()
