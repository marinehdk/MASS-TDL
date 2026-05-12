"""Target Vessel Node — 3 modes: ais_replay, ncdm, intelligent."""
from __future__ import annotations

import math
from enum import Enum


class TargetMode(str, Enum):
    REPLAY = "replay"
    NCDM = "ncdm"
    INTELLIGENT = "intelligent"


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


class TargetVesselNode:
    """Container managing a collection of target vessels.

    Intended to be wrapped by a ROS2 node (target_vessel_node entry point)
    that publishes ``TargetVesselArray`` messages at a configurable rate.
    """

    def __init__(self) -> None:
        self._targets: list[TargetVessel] = []

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


def main() -> None:
    """Entry point for console_scripts (ROS2 node placeholder)."""
    print("TargetVesselNode ready")
