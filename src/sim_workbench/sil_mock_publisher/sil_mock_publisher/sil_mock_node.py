"""SIL mock publisher node — publishes SAT and ODD stub topics at 1 Hz.

Lifecycle: active from D1.3b through D2.4. At D2.5, replace with real
M1/M2 nodes in the launch file (topic names unchanged, HMI zero-diff).
"""
from __future__ import annotations

import rclpy
import rclpy.node

from l3_msgs.msg import ODDState, SAT1Data, SAT2Data, SAT3Data, SATData


class SilMockNode(rclpy.node.Node):
    def __init__(self) -> None:
        super().__init__("sil_mock_publisher")
        self._pub_sat = self.create_publisher(SATData, "/l3/sat/data", 10)
        self._pub_odd = self.create_publisher(ODDState, "/l3/m1/odd_state", 10)
        self._timer = self.create_timer(1.0, self._publish_tick)
        self.get_logger().info("sil_mock_publisher started — publishing SAT + ODD stubs at 1 Hz")

    def _publish_tick(self) -> None:
        stamp = self.get_clock().now().to_msg()
        self._pub_sat.publish(self._make_sat_stub(stamp))
        self._pub_odd.publish(self._make_odd_stub(stamp))

    def _make_sat_stub(self, stamp) -> SATData:
        msg = SATData()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.source_module = "sil_mock_publisher"

        # SAT1Data: current status
        sat1 = SAT1Data()
        sat1.state_summary = "TRANSIT @ D3, ODD-A, 8.0 kn (sil_mock stub)"
        sat1.active_alerts = []
        msg.sat1 = sat1

        # SAT2Data: reasoning (on-demand, empty for stub)
        sat2 = SAT2Data()
        sat2.trigger_reason = ""
        sat2.reasoning_chain = ""
        sat2.system_confidence = 0.9
        msg.sat2 = sat2

        # SAT3Data: forecast (TDL-driven, stub values)
        sat3 = SAT3Data()
        sat3.forecast_horizon_s = 45.0
        sat3.predicted_state = "STABLE @ D3 (sil_mock forecast)"
        sat3.prediction_uncertainty = 0.15
        sat3.tdl_s = 45.0
        sat3.tmr_s = 60.0
        msg.sat3 = sat3

        return msg

    def _make_odd_stub(self, stamp) -> ODDState:
        msg = ODDState()
        msg.schema_version = "v1.1.2"
        msg.stamp = stamp
        msg.current_zone = ODDState.ODD_ZONE_A
        msg.auto_level = ODDState.AUTO_LEVEL_D3
        msg.health = ODDState.HEALTH_FULL
        msg.envelope_state = ODDState.ENVELOPE_IN
        msg.conformance_score = 0.9
        msg.tmr_s = 60.0
        msg.tdl_s = 45.0
        msg.zone_reason = "sil_mock: ODD-A (open waters) by default, stub for D1.3b testing"
        msg.allowed_zones = [
            ODDState.ODD_ZONE_A,
            ODDState.ODD_ZONE_B,
        ]
        msg.confidence = 0.9
        msg.rationale = "sil_mock_publisher stub ODD state for D1.3b scenario management testing"

        return msg


def main(args: list[str] | None = None) -> None:
    rclpy.init(args=args)
    node = SilMockNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
