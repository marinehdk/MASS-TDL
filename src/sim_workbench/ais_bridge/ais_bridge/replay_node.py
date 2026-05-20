# SPDX-License-Identifier: Proprietary
"""ROS2 node: replay NOAA/DMA AIS data as TrackedTargetArray at configurable speed."""
from __future__ import annotations

import rclpy
from rclpy.node import Node

from ais_bridge.dataset_loader import load_dma_nmea, load_noaa_csv
from ais_bridge.nmea_decoder import AISRecord
from ais_bridge.target_publisher import build_tracked_target_array

# DEMO-1: dual-publish lead target to /sil/target_vessel_state
try:
    from sil_msgs.msg import TargetVesselState
except ImportError:
    TargetVesselState = None  # /sil/target_vessel_state publisher disabled


class AISReplayNode(Node):
    """Publish TrackedTargetArray from pre-recorded AIS data."""

    def __init__(self):
        super().__init__('ais_replay')

        self.declare_parameter('dataset_path',    'data/ais_datasets/AIS_synthetic_1h.csv')
        self.declare_parameter('dataset_format',  'noaa_csv')
        self.declare_parameter('replay_rate_x',   1.0)
        self.declare_parameter('publish_rate_hz', 2.0)
        self.declare_parameter('max_targets',     50)

        path     = self.get_parameter('dataset_path').get_parameter_value().string_value
        fmt      = self.get_parameter('dataset_format').get_parameter_value().string_value
        rate_x   = self.get_parameter('replay_rate_x').get_parameter_value().double_value
        pub_hz   = self.get_parameter('publish_rate_hz').get_parameter_value().double_value
        max_tgts = self.get_parameter('max_targets').get_parameter_value().integer_value

        self._records = self._load(path, fmt)
        self._max_targets = max_tgts
        self._idx = 0

        if not self._records:
            self.get_logger().warn('No AIS records loaded — check dataset_path and format.')

        self._window_size = max(1, min(max_tgts * 10, len(self._records) // 100 or 1))

        timer_period = 1.0 / (pub_hz * max(0.1, rate_x))
        from l3_external_msgs.msg import TrackedTargetArray
        self._pub = self.create_publisher(TrackedTargetArray, '/fusion/tracked_targets', 10)
        self._pub_target = (
            self.create_publisher(TargetVesselState, '/sil/target_vessel_state', 10)
            if TargetVesselState is not None else None
        )
        self._timer = self.create_timer(timer_period, self._publish_callback)

        self.get_logger().info(
            f'AIS replay: {len(self._records)} records, {rate_x}x speed, {pub_hz} Hz, fmt={fmt}')

    def _load(self, path: str, fmt: str) -> list[AISRecord]:
        try:
            if fmt == 'noaa_csv':
                return list(load_noaa_csv(path))
            return list(load_dma_nmea(path))
        except Exception as e:
            self.get_logger().error(f'Failed to load dataset {path!r}: {e}')
            return []

    def _publish_callback(self):
        if not self._records:
            return

        end = min(self._idx + self._window_size, len(self._records))
        window = self._records[self._idx:end]
        self._idx = end % len(self._records)

        by_mmsi: dict[int, AISRecord] = {}
        for rec in window:
            by_mmsi[rec.mmsi] = rec

        active = list(by_mmsi.values())[:self._max_targets]
        stamp = self.get_clock().now().to_msg()
        msg = build_tracked_target_array(active, stamp, self)
        self._pub.publish(msg)

        if self._pub_target is not None and active:
            lead = active[0]
            tgt = TargetVesselState()
            tgt.stamp = stamp
            tgt.mmsi = int(lead.mmsi)
            tgt.lat = float(lead.lat)
            tgt.lon = float(lead.lon)
            tgt.heading = float(lead.heading_deg)
            tgt.sog = float(lead.sog_kn * 0.514444)  # kn → m/s
            tgt.cog = float(lead.cog_deg)
            tgt.rot = 0.0
            tgt.ship_type = int(lead.ship_type)
            tgt.mode = 1  # REPLAY
            self._pub_target.publish(tgt)


def main(args=None):
    rclpy.init(args=args)
    node = AISReplayNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
