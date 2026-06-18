"""Target Vessel Node — LifecycleNode publishing TargetVesselState @ 10Hz."""
from __future__ import annotations

import json
import math
from enum import Enum

import numpy as np
from sil_common.det_rng import make_rng

import rclpy
from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy

from sil_msgs.msg import TargetVesselState
from sil_msgs.srv import AddTarget, RemoveTarget


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
        ou_theta: float = 0.05,
        ou_sigma: float = 0.5,
        rng: np.random.Generator | None = None,
    ):
        self.mmsi = mmsi
        self.lat = lat
        self.lon = lon
        self.heading = math.radians(heading_deg)
        self.sog = sog_kn * 0.514444  # knots → m/s
        self.mode = mode
        self._time = 0.0
        self._ou_theta = ou_theta
        self._ou_sigma = ou_sigma
        self._heading_ref = self.heading
        if rng is None:
            self.rng = np.random.default_rng()
        else:
            self.rng = rng

    def step(self, dt: float = 0.1) -> dict:
        """Advance simulation by *dt* seconds using simple linear motion.

        Returns a dict with current state suitable for ROS2 message
        construction or test assertions.
        """
        self._time += dt
        if self.mode == TargetMode.NCDM:
            dH = (-self._ou_theta * (self.heading - self._heading_ref) * dt
                  + self._ou_sigma * math.sqrt(dt) * self.rng.normal())
            self.heading += dH
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
        try:
            from rclpy.parameter import Parameter
            overrides = [Parameter('use_sim_time', Parameter.Type.BOOL, True)]
        except ImportError:
            overrides = None

        import inspect
        kwargs = {}
        try:
            sig = inspect.signature(super().__init__)
            if "parameter_overrides" in sig.parameters and overrides is not None:
                kwargs["parameter_overrides"] = overrides
            if "allow_undeclared_parameters" in sig.parameters:
                kwargs["allow_undeclared_parameters"] = True
            if "automatically_declare_parameters_from_overrides" in sig.parameters:
                kwargs["automatically_declare_parameters_from_overrides"] = True
        except Exception:
            pass

        super().__init__("target_vessel_node", **kwargs)
        self._targets: list[TargetVessel] = []
        self._tv_pub = None
        self._timer = None
        self._last_sim_time = None
        self._root_seed = None
        self._episode = None
        self._worker = None

        self._add_target_srv = None
        self._remove_target_srv = None

        # Last published simulation timestamp.
        self._last_pub_sim_time: float = 0.0


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
        if self._root_seed is not None:
            t_rng = make_rng(
                root=self._root_seed,
                episode=self._episode,
                node="target_vessel",
                worker=self._worker + mmsi,
            )
        else:
            t_rng = None
        t = TargetVessel(mmsi, lat, lon, heading_deg, sog_kn, TargetMode(mode), rng=t_rng)
        self._targets.append(t)
        return t

    def step_all(self, dt: float = 0.1) -> list[dict]:
        return [t.step(dt) for t in self._targets]

    # ── Lifecycle callbacks ─────────────────────────────────────────────

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        try:
            self.declare_parameter("default_targets_json", "[]")
        except Exception:
            pass
        try:
            self.declare_parameter("root_seed", 0)
        except Exception:
            pass
        try:
            self.declare_parameter("episode", 0)
        except Exception:
            pass
        try:
            self.declare_parameter("worker", 0)
        except Exception:
            pass

        self._root_seed = self.get_parameter("root_seed").value
        self._episode = self.get_parameter("episode").value
        self._worker = self.get_parameter("worker").value

        raw = self.get_parameter("default_targets_json").value
        if raw:
            try:
                for entry in json.loads(raw):
                    if isinstance(entry, dict) and "static" in entry and "initial" in entry:
                        mmsi = int(entry["static"].get("mmsi", 0))
                        initial = entry["initial"]
                        pos = initial.get("position", {})
                        lat = float(pos.get("latitude", 0.0))
                        lon = float(pos.get("longitude", 0.0))
                        heading_deg = float(initial.get("heading", initial.get("cog", 0.0)))
                        sog_kn = float(initial.get("sog", 0.0))
                        mode = entry.get("model", "replay")
                        if mode == "ais_replay_vessel" or mode not in [m.value for m in TargetMode]:
                            mode = "replay"
                        self.add_target(mmsi, lat, lon, heading_deg, sog_kn, mode)
                    else:
                        self.add_target(**entry)
            except (json.JSONDecodeError, TypeError, KeyError, ValueError) as exc:
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
        self._tv_pub = self.create_publisher(
            TargetVesselState, "/sil/target_vessel_state", qos
        )
        self._timer = self.create_timer(0.1, self._step_callback)
        self._add_target_srv = self.create_service(
            AddTarget, "/target_vessel_node/add_target", self._handle_add_target)
        self._remove_target_srv = self.create_service(
            RemoveTarget, "/target_vessel_node/remove_target", self._handle_remove_target)
        self._logger.info("AddTarget/RemoveTarget services up")
        self._logger.info("Activated — publishing TargetVesselState @ 10 Hz")
        return super().on_activate(state)

    def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._tv_pub is not None:
            self.destroy_publisher(self._tv_pub)
            self._tv_pub = None
        for attr in ("_add_target_srv", "_remove_target_srv"):
            srv = getattr(self, attr)
            if srv is not None:
                self.destroy_service(srv)
                setattr(self, attr, None)
        self._logger.info("Deactivated")
        return super().on_deactivate(state)

    def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
        self._targets.clear()
        self._last_sim_time = None
        self._logger.info("Cleaned up — targets cleared")
        return TransitionCallbackReturn.SUCCESS

    # ── Service handlers ────────────────────────────────────────────────

    def _handle_add_target(self, request, response):
        try:
            mode = request.mode if request.mode else "replay"
            self.add_target(
                mmsi=int(request.mmsi), lat=request.lat, lon=request.lon,
                heading_deg=request.heading_deg, sog_kn=request.sog_kn, mode=mode)
            response.success = True
            response.message = f"added MMSI {request.mmsi}"
            self._logger.info(f"Service add_target MMSI {request.mmsi}")
        except Exception as exc:  # noqa: BLE001
            response.success = False
            response.message = f"add_target error: {exc}"
            self._logger.error(response.message)
        return response

    def _handle_remove_target(self, request, response):
        before = len(self._targets)
        self._targets = [t for t in self._targets if t.mmsi != int(request.mmsi)]
        removed = len(self._targets) < before
        response.success = removed
        response.message = (f"removed MMSI {request.mmsi}" if removed
                            else f"MMSI {request.mmsi} not found")
        self._logger.info(f"Service remove_target: {response.message}")
        return response

    # ── Internal ────────────────────────────────────────────────────────

    def _step_callback(self) -> None:
        if self._tv_pub is None:
            return

        now_sim = self.get_clock().now()
        if self._last_sim_time is None:
            self._last_sim_time = now_sim
            return

        dt = 0.1
        elapsed = (now_sim - self._last_sim_time).nanoseconds / 1e9
        if elapsed < dt:
            return

        steps = int(elapsed / dt)

        from rclpy.duration import Duration
        for _ in range(steps):
            for t in self._targets:
                t.step(dt=dt)
            self._last_sim_time += Duration(nanoseconds=int(dt * 1e9))
            now_sim_msg = self._last_sim_time.to_msg()
            for t in self._targets:
                msg = TargetVesselState()
                msg.stamp = now_sim_msg
                msg.mmsi = t.mmsi
                msg.lat = t.lat
                msg.lon = t.lon
                msg.heading = t.heading
                msg.sog = t.sog
                msg.cog = t.heading
                msg.rot = 0.0
                msg.ship_type = 1  # CARGO
                msg.mode = _TARGET_MODE_TO_UINT8.get(t.mode.value, 0)
                self._tv_pub.publish(msg)
            self._last_pub_sim_time = self._last_sim_time.nanoseconds / 1e9



def main(args: list[str] | None = None) -> None:
    """Entry point for console_scripts — spins the lifecycle node."""
    rclpy.init(args=args)
    node = TargetVesselNode()
    rclpy.spin(node)
    rclpy.shutdown()
