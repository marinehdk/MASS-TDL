"""Sensor Mock Node — LifecycleNode publishing RadarMeasurement @ 5Hz + AISMessage @ 0.1Hz."""
from __future__ import annotations

import math
import random

import rclpy
from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy

from sil_msgs.msg import AISMessage, OwnShipState, RadarMeasurement, TargetVesselState


class SensorMockNode(LifecycleNode):
    """Lifecycle-managed ROS2 node publishing simulated sensor data.

    Full lifecycle:
      *configure*  → declare parameters (ais_drop_pct, radar_max_range)
      *activate*   → create publishers (RadarMeasurement @ 5Hz, AISMessage @ 0.1Hz),
                     subscribe to own-ship and target-vessel state topics
      *deactivate* → destroy timers, publishers, subscriptions
      *cleanup*    → reset stored state
    """

    def __init__(self, **kwargs) -> None:
        super().__init__("sensor_mock_node", **kwargs)
        self._own_state: OwnShipState | None = None
        self._target_states: dict[int, TargetVesselState] = {}
        self._radar_pub = None
        self._ais_pub = None
        self._radar_timer = None
        self._ais_timer = None
        self._own_state_sub = None
        self._target_state_sub = None

    # ── Preserved from original stub ────────────────────────────────────────

    def generate_ais(self, own_lat: float, own_lon: float, target: dict) -> dict | None:
        """Generate AIS message. Returns None if dropped."""
        if random.random() < self.ais_drop_pct / 100.0:
            return None
        return {
            "mmsi": target["mmsi"],
            "sog": target["sog"],
            "cog": target["cog"],
            "lat": target["lat"],
            "lon": target["lon"],
            "heading": target["heading"],
            "dropout_flag": False,
        }

    def generate_radar(self, own_lat: float, own_lon: float, targets: list[dict]) -> dict:
        """Generate radar measurement with polar targets."""
        polar = []
        for t in targets:
            dlat = (t["lat"] - own_lat) * 111120.0
            dlon = (t["lon"] - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
            rng = math.sqrt(dlat**2 + dlon**2)
            if rng <= self.radar_max_range:
                bearing = math.atan2(dlon, dlat)
                polar.append({
                    "range": rng + random.gauss(0, 3.0),
                    "bearing": bearing + random.gauss(0, 0.005),
                    "rcs": random.uniform(10, 50),
                })
        return {"polar_targets": polar, "clutter_cardinality": random.randint(0, 5)}

    # ── Lifecycle callbacks ─────────────────────────────────────────────────

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.declare_parameter("ais_drop_pct", 0.0)
        self.declare_parameter("radar_max_range", 12000.0)
        self.ais_drop_pct = self.get_parameter("ais_drop_pct").value
        self.radar_max_range = self.get_parameter("radar_max_range").value
        self._logger.info(
            "Configured — ais_drop_pct=%.2f radar_max_range=%.1f",
            self.ais_drop_pct,
            self.radar_max_range,
        )
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
        # Radar publisher: BEST_EFFORT + VOLATILE + KEEP_LAST(2)
        radar_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=2,
        )
        self._radar_pub = self.create_lifecycle_publisher(
            RadarMeasurement, "/sil/radar_meas", radar_qos
        )

        # AIS publisher: RELIABLE + VOLATILE + KEEP_LAST(10)
        ais_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        self._ais_pub = self.create_lifecycle_publisher(
            AISMessage, "/sil/ais_msg", ais_qos
        )

        # Timers: radar @ 5 Hz (0.2 s), AIS @ 0.1 Hz (10.0 s)
        self._radar_timer = self.create_timer(0.2, self._radar_callback)
        self._ais_timer = self.create_timer(10.0, self._ais_callback)

        # Subscriptions to upstream state topics
        self._own_state_sub = self.create_subscription(
            OwnShipState, "/sil/own_ship_state", self._own_state_cb, 10
        )
        self._target_state_sub = self.create_subscription(
            TargetVesselState, "/sil/target_vessel_state", self._target_state_cb, 10
        )

        self._logger.info(
            "Activated — radar @ 5 Hz (/sil/radar_meas), AIS @ 0.1 Hz (/sil/ais_msg)"
        )
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
        if self._radar_timer is not None:
            self.destroy_timer(self._radar_timer)
            self._radar_timer = None
        if self._ais_timer is not None:
            self.destroy_timer(self._ais_timer)
            self._ais_timer = None
        if self._radar_pub is not None:
            self.destroy_publisher(self._radar_pub)
            self._radar_pub = None
        if self._ais_pub is not None:
            self.destroy_publisher(self._ais_pub)
            self._ais_pub = None
        if self._own_state_sub is not None:
            self.destroy_subscription(self._own_state_sub)
            self._own_state_sub = None
        if self._target_state_sub is not None:
            self.destroy_subscription(self._target_state_sub)
            self._target_state_sub = None
        self._logger.info("Deactivated — all timers, publishers, subscriptions destroyed")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
        self._own_state = None
        self._target_states.clear()
        self._logger.info("Cleaned up — state reset")
        return TransitionCallbackReturn.SUCCESS

    # ── Subscription callbacks ──────────────────────────────────────────────

    def _own_state_cb(self, msg: OwnShipState) -> None:
        self._own_state = msg

    def _target_state_cb(self, msg: TargetVesselState) -> None:
        self._target_states[msg.mmsi] = msg

    # ── Timer callbacks ─────────────────────────────────────────────────────

    def _radar_callback(self) -> None:
        """Publish a RadarMeasurement from the latest own-ship + target states."""
        if self._own_state is None:
            return
        targets = [{"lat": ts.lat, "lon": ts.lon} for ts in self._target_states.values()]
        result = self.generate_radar(self._own_state.lat, self._own_state.lon, targets)
        now = self.get_clock().now().to_msg()
        msg = RadarMeasurement()
        msg.stamp = now
        msg.range = [p["range"] for p in result["polar_targets"]]
        msg.bearing = [p["bearing"] for p in result["polar_targets"]]
        msg.rcs = [p["rcs"] for p in result["polar_targets"]]
        msg.clutter_cardinality = result["clutter_cardinality"]
        self._radar_pub.publish(msg)

    def _ais_callback(self) -> None:
        """Publish an AISMessage per target vessel (subject to dropout)."""
        if self._own_state is None:
            return
        now = self.get_clock().now().to_msg()
        for ts in list(self._target_states.values()):
            target_dict = {
                "mmsi": ts.mmsi,
                "sog": ts.sog,
                "cog": ts.cog,
                "lat": ts.lat,
                "lon": ts.lon,
                "heading": ts.heading,
            }
            ais_data = self.generate_ais(self._own_state.lat, self._own_state.lon, target_dict)
            if ais_data is not None:
                msg = AISMessage()
                msg.stamp = now
                msg.mmsi = ais_data["mmsi"]
                msg.sog = ais_data["sog"]
                msg.cog = ais_data["cog"]
                msg.lat = ais_data["lat"]
                msg.lon = ais_data["lon"]
                msg.heading = ais_data["heading"]
                msg.dropout_flag = ais_data["dropout_flag"]
                self._ais_pub.publish(msg)


def main(args: list[str] | None = None) -> None:
    """Entry point for console_scripts — spins the lifecycle node."""
    rclpy.init(args=args)
    node = SensorMockNode()
    rclpy.spin(node)
    rclpy.shutdown()
