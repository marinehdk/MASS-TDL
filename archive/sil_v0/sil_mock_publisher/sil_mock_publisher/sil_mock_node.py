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

        # D1.3b.3: R14 head-on trajectory (50Hz nav + 2Hz tracks)
        from l3_external_msgs.msg import TrackedTargetArray, FilteredOwnShipState
        from l3_msgs.msg import TrackedTarget
        from geographic_msgs.msg import GeoPoint
        import math
        import time

        self._pub_nav = self.create_publisher(FilteredOwnShipState, "/nav/filtered_state", 10)
        self._pub_tracks = self.create_publisher(TrackedTargetArray, "/world_model/tracks", 10)
        self._nav_timer = self.create_timer(0.02, self._publish_nav)
        self._track_timer = self.create_timer(0.5, self._publish_tracks)
        self._t0 = time.time()
        self._own_lat0, self._own_lon0 = 63.435, 10.395

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

    def _publish_nav(self) -> None:
        elapsed = time.time() - self._t0
        dist_nm = 18.0 * elapsed / 3600.0
        lat = self._own_lat0 + dist_nm / 60.0
        lon = self._own_lon0

        from l3_external_msgs.msg import FilteredOwnShipState
        from geographic_msgs.msg import GeoPoint
        msg = FilteredOwnShipState()
        msg.schema_version = "v1.1.2"
        msg.stamp = self.get_clock().now().to_msg()
        pos = GeoPoint()
        pos.latitude = lat
        pos.longitude = lon
        pos.altitude = 0.0
        msg.position = pos
        msg.sog_kn = 18.0
        msg.cog_deg = 0.0
        msg.heading_deg = 5.0
        msg.confidence = 0.9
        self._pub_nav.publish(msg)

    def _publish_tracks(self) -> None:
        elapsed = time.time() - self._t0
        dist_nm_own = 18.0 * elapsed / 3600.0
        dist_nm_ts = 10.0 * elapsed / 3600.0
        own_lat = self._own_lat0 + dist_nm_own / 60.0
        ts_lat = 63.458 - dist_nm_ts / 60.0
        ts_lon = 10.395

        from l3_msgs.msg import TrackedTarget, EncounterClassification
        from l3_external_msgs.msg import TrackedTargetArray
        from geographic_msgs.msg import GeoPoint

        dlat_nm = (ts_lat - own_lat) * 60.0
        cpa_nm = abs(dlat_nm)
        closing_speed_kn = 18.0 + 10.0
        tcpa_s = max(0.0, cpa_nm / (closing_speed_kn / 3600.0))

        enc = EncounterClassification()
        enc.encounter_type = EncounterClassification.ENCOUNTER_TYPE_HEAD_ON
        enc.is_giveway = True
        enc.relative_bearing_deg = 0.0
        enc.aspect_angle_deg = 180.0

        t = TrackedTarget()
        t.schema_version = "v1.1.2"
        t.target_id = 1
        t.stamp = self.get_clock().now().to_msg()
        pos = GeoPoint()
        pos.latitude = ts_lat
        pos.longitude = ts_lon
        pos.altitude = 0.0
        t.position = pos
        t.sog_kn = 10.0
        t.cog_deg = 180.0
        t.heading_deg = 180.0
        t.cpa_m = cpa_nm * 1852.0
        t.tcpa_s = tcpa_s
        t.classification = "vessel"
        t.classification_confidence = 0.7
        t.source_sensor = "ais"
        t.confidence = 0.7
        t.encounter = enc

        msg = TrackedTargetArray()
        msg.schema_version = "v1.1.2"
        msg.targets = [t]
        msg.confidence = 0.7
        msg.rationale = "R14 head-on mock trajectory"
        self._pub_tracks.publish(msg)

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
