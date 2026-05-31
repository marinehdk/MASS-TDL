"""Environment Disturbance Node — Gauss-Markov wind + current model.

Publishes sil_msgs/EnvironmentState at 1 Hz via a ROS 2 LifecycleNode.
Provides a first-order Gauss-Markov wind process and constant current offset.
Replaced by a real oceanographic model at D2.5 integration.
"""
from __future__ import annotations

import math

import numpy as np
from sil_common.det_rng import make_rng

import rclpy
from rclpy.lifecycle import LifecycleNode, LifecycleState, TransitionCallbackReturn
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from sil_msgs.msg import EnvironmentState


class EnvDisturbanceNode(LifecycleNode):
    """First-order Gauss-Markov wind + constant current model.

    ROS 2 LifecycleNode that publishes environment state at 1 Hz.

    Parameters
    ----------
    node_name : str
        ROS 2 node name (default ``"env_disturbance_node"``).

    Notes
    -----
    Wind speed follows a discrete-time Gauss-Markov process:

        w_{k+1} = exp(-dt/tau) * w_k + N(0, sigma*sqrt(1 - exp(-2*dt/tau)))

    Wind direction gets a small random walk perturbation.
    Current is constant (placeholder — replaced by tidal model in D2.5).
    """

    def __init__(self, node_name: str = "env_disturbance_node", **kwargs) -> None:
        self.root_seed = kwargs.pop("root_seed", 0)
        self.episode = kwargs.pop("episode", 0)
        self.worker = kwargs.pop("worker", 0)

        super().__init__(
            node_name,
            allow_undeclared_parameters=True,
            automatically_declare_parameters_from_overrides=True,
            **kwargs
        )

        # Initial conditions
        self._wind_dir: float = 0.0
        self._wind_speed: float = 5.0
        self._current_dir: float = 0.0
        self._current_speed: float = 0.5
        self._prev_wind_speed: float = 5.0
        self._prev_wind_dir: float = 0.0

        # Created during activation, cleaned up during deactivation
        self._env_pub = None
        self._timer = None

        self.rng = None
        self.reset()

    def reset(self, episode: int | None = None) -> None:
        """Reset/re-derive the RNG for a new episode."""
        if episode is not None:
            self.episode = episode
        self.rng = make_rng(
            root=self.root_seed,
            episode=self.episode,
            node="env_disturbance",
            worker=self.worker
        )

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Declare parameters and prepare for operation."""
        try:
            self.declare_parameter("tau_wind", 300.0)
            self.declare_parameter("sigma", 2.0)
            self.declare_parameter("visibility_nm", 2.0)
            self.declare_parameter("sea_state_beaufort", 4)
            self.declare_parameter("wind_speed_kn", 15.0)
            self.declare_parameter("wind_dir_deg", 180.0)
            self.declare_parameter("current_speed_kn", 0.5)
            self.declare_parameter("current_dir_deg", 0.0)
            self.declare_parameter("wind_speed_mps", 5.0)
            self.declare_parameter("current_speed_mps", 0.5 * 0.514444)
            self.declare_parameter("root_seed", 0)
            self.declare_parameter("episode", 0)
            self.declare_parameter("worker", 0)
        except Exception:
            pass

        self.root_seed = self.get_parameter("root_seed").value
        self.episode = self.get_parameter("episode").value
        self.worker = self.get_parameter("worker").value
        self.reset()

        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Create publisher and timer.

        Publisher is on ``/sil/environment`` with a QoS profile of:
        RELIABLE + VOLATILE + KEEP_LAST(2).
        """
        try:
            self._wind_speed = self.get_parameter("wind_speed_mps").value
            self._wind_dir = self.get_parameter("wind_dir_deg").value
            self._prev_wind_speed = self._wind_speed
            self._prev_wind_dir = self._wind_dir
            self._current_speed = self.get_parameter("current_speed_mps").value
            self._current_dir = self.get_parameter("current_dir_deg").value
        except Exception as exc:
            if hasattr(self, "get_logger"):
                self.get_logger().warn(f"Failed to read parameters in on_activate: {exc}")

        qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=2,
        )
        self._env_pub = self.create_publisher(EnvironmentState, "/sil/environment", qos)
        self._timer = self.create_timer(1.0, self._step_callback)
        return super().on_activate(state)

    def on_deactivate(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Destroy timer and publisher."""
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._env_pub is not None:
            self.destroy_publisher(self._env_pub)
            self._env_pub = None
        return super().on_deactivate(state)

    def on_cleanup(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Reset wind state to defaults."""
        self._wind_dir = 0.0
        self._wind_speed = 5.0
        self._current_dir = 0.0
        self._current_speed = 0.5
        self._prev_wind_speed = 5.0
        self._prev_wind_dir = 0.0
        self.reset(episode=0)
        return TransitionCallbackReturn.SUCCESS

    def _step_callback(self) -> None:
        """Advance the model and publish an EnvironmentState message."""
        dt = 1.0
        tau_wind = self.get_parameter("tau_wind").value

        # If wind speed parameter is 0, keep it strictly at 0 (calm)
        initial_wind_speed = self.get_parameter("wind_speed_mps").value
        if initial_wind_speed == 0.0:
            self._wind_speed = 0.0
            self._wind_dir = self.get_parameter("wind_dir_deg").value
            self._prev_wind_speed = 0.0
            noise = 0.0
            wind_walk = 0.0
            alpha = 1.0
        else:
            sigma = self.get_parameter("sigma").value
            alpha = math.exp(-dt / tau_wind)
            noise = self.rng.normal(0, sigma * math.sqrt(1.0 - alpha**2))
            wind_walk = self.rng.normal(0, 0.1)

        self._wind_speed = alpha * self._prev_wind_speed + noise
        self._wind_dir = (self._wind_dir + wind_walk) % 360.0
        self._prev_wind_speed = self._wind_speed

        visibility_nm = self.get_parameter("visibility_nm").value
        sea_state_beaufort = self.get_parameter("sea_state_beaufort").value

        msg = EnvironmentState()
        msg.stamp = self.get_clock().now().to_msg()
        msg.wind_direction = self._wind_dir
        msg.wind_speed_mps = max(0.0, self._wind_speed)
        msg.current_direction = self._current_dir
        msg.current_speed_mps = self._current_speed
        msg.visibility_nm = visibility_nm
        msg.sea_state_beaufort = sea_state_beaufort

        if self._env_pub is not None:
            self._env_pub.publish(msg)


def main() -> None:
    """CLI entry point (``ros2 run`` / console script)."""
    rclpy.init()
    node = EnvDisturbanceNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()
