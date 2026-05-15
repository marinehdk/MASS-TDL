"""Target Vessel Node — LifecycleNode publishing TargetVesselState @ 10Hz."""
from __future__ import annotations

import json
import math
from enum import Enum

import rclpy
from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy

from sil_msgs.msg import TargetVesselState


class TargetMode(str, Enum):
    REPLAY = "replay"
    NCDM = "ncdm"
    INTELLIGENT = "intelligent"


# Mapping from TargetMode string value → uint8 per TargetVesselState.msg
_TARGET_MODE_TO_UINT8 = {
    "replay": 1,
    "ncdm": 2,
    "intelligent": 3,
}


class TargetVessel:
    """A single target vessel with simple linear kinematics.

    Parameters
    ----------
    mmsi : int
        Unique vessel identifier.
    lat, lon : float
        Initial position in decimal degrees.
    heading_deg : float
        Initial heading in degrees (0 = north, clockwise).
    sog_kn : float
        Speed over ground in knots.
    mode : TargetMode
        Behavioural mode (default REPLAY).
    """

    def __init__(
        self,
        mmsi: int,
        lat: float,
        lon: float,
        heading_deg: float,
        sog_kn: float,
        mode: TargetMode = TargetMode.REPLAY,
    ):
        self.mmsi = mmsi
        self.lat = lat
        self.lon = lon
        self.heading = math.radians(heading_deg)
        self.sog = sog_kn * 0.514444  # knots → m/s
        self.mode = mode
        self._time = 0.0

    def step(self, dt: float = 0.1) -> dict:
        """Advance simulation by *dt* seconds using simple linear motion.

        Returns a dict with current state suitable for ROS2 message
        construction or test assertions.
        """
        self._time += dt
        # Approximate meridian arc: 1 deg lat ≈ 111 120 m
        lat_rad = math.radians(self.lat)
        self.lat += self.sog * math.cos(self.heading) * dt / 111120.0
        self.lon += (
            self.sog
            * math.sin(self.heading)
            * dt
            / (111120.0 * math.cos(lat_rad))
        )
        return {
            "mmsi": self.mmsi,
            "lat": self.lat,
            "lon": self.lon,
            "heading": self.heading,
            "sog": self.sog,
            "cog": self.heading,
            "rot": 0.0,
            "mode": self.mode.value,
        }


class TargetVesselNode(LifecycleNode):
    """Lifecycle-managed ROS2 node that publishes target vessel states at 10 Hz.

    Full lifecycle:
      *configure*  → declare parameters, load default targets from JSON
      *activate*   → create publisher + timer (10 Hz)
      *deactivate* → destroy timer + publisher
      *cleanup*    → clear target list
    """

    def __init__(self) -> None:
        super().__init__("target_vessel_node")
        self._targets: list[TargetVessel] = []
        self._tv_pub = None
        self._timer = None

    # ── Public helpers (preserved from original stub) ────────────────────

    def add_target(
        self,
        mmsi: int,
        lat: float,
        lon: float,
        heading_deg: float,
        sog_kn: float,
        mode: str = "replay",
    ) -> TargetVessel:
        t = TargetVessel(mmsi, lat, lon, heading_deg, sog_kn, TargetMode(mode))
        self._targets.append(t)
        return t

    def step_all(self, dt: float = 0.1) -> list[dict]:
        return [t.step(dt) for t in self._targets]

    # ── Lifecycle callbacks ─────────────────────────────────────────────

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        self.declare_parameter("default_targets_json", "[]")
        raw = self.get_parameter("default_targets_json").value
        if raw:
            try:
                for entry in json.loads(raw):
                    self.add_target(**entry)
            except (json.JSONDecodeError, TypeError, KeyError) as exc:
                self._logger.error(f"Failed to parse default_targets_json: {exc}")
                return TransitionCallbackReturn.ERROR
        self._logger.info(f"Configured with {len(self._targets)} target(s)")
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )
        self._tv_pub = self.create_lifecycle_publisher(
            TargetVesselState, "/sil/target_vessel_state", qos
        )
        self._timer = self.create_timer(0.1, self._step_callback)
        self._logger.info("Activated — publishing TargetVesselState @ 10 Hz")
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._tv_pub is not None:
            self.destroy_publisher(self._tv_pub)
            self._tv_pub = None
        self._logger.info("Deactivated")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
        self._targets.clear()
        self._logger.info("Cleaned up — targets cleared")
        return TransitionCallbackReturn.SUCCESS

    # ── Internal ────────────────────────────────────────────────────────

    def _step_callback(self) -> None:
        now = self.get_clock().now().to_msg()
        for t in self._targets:
            state = t.step(dt=0.1)
            msg = TargetVesselState()
            msg.stamp = now
            msg.mmsi = state["mmsi"]
            msg.lat = state["lat"]
            msg.lon = state["lon"]
            msg.heading = state["heading"]
            msg.sog = state["sog"]
            msg.cog = state["cog"]
            msg.rot = state["rot"]
            msg.ship_type = 1  # CARGO
            msg.mode = _TARGET_MODE_TO_UINT8.get(state["mode"], 0)
            self._tv_pub.publish(msg)


def main(args: list[str] | None = None) -> None:
    """Entry point for console_scripts — spins the lifecycle node."""
    rclpy.init(args=args)
    node = TargetVesselNode()
    rclpy.spin(node)
    rclpy.shutdown()
