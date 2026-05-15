"""Environment Disturbance Node — Gauss-Markov wind + current model.

Publishes sil_msgs/EnvironmentState at 1 Hz via a ROS 2 LifecycleNode.
Provides a first-order Gauss-Markov wind process and constant current offset.
Replaced by a real oceanographic model at D2.5 integration.
"""
from __future__ import annotations

import math
import random

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

    def __init__(self, node_name: str = "env_disturbance_node") -> None:
        super().__init__(node_name)

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

    def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Declare parameters and prepare for operation."""
        self.declare_parameter("tau_wind", 300.0)
        self.declare_parameter("sigma", 2.0)
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: LifecycleState) -> TransitionCallbackReturn:
        """Create publisher and timer.

        Publisher is on ``/sil/environment`` with a QoS profile of:
        RELIABLE + VOLATILE + KEEP_LAST(2).
        """
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
        return TransitionCallbackReturn.SUCCESS

    def _step_callback(self) -> None:
        """Advance the model and publish an EnvironmentState message."""
        dt = 1.0
        tau_wind = self.get_parameter("tau_wind").value
        sigma = self.get_parameter("sigma").value
        alpha = math.exp(-dt / tau_wind)
        noise = random.gauss(0, sigma * math.sqrt(1.0 - alpha**2))

        self._wind_speed = alpha * self._prev_wind_speed + noise
        self._wind_dir = (self._wind_dir + random.gauss(0, 0.1)) % 360.0
        self._prev_wind_speed = self._wind_speed

        msg = EnvironmentState()
        msg.stamp = self.get_clock().now().to_msg()
        msg.wind_direction = self._wind_dir
        msg.wind_speed_mps = max(0.0, self._wind_speed)
        msg.current_direction = self._current_dir
        msg.current_speed_mps = self._current_speed
        msg.visibility_nm = 10.0
        msg.sea_state_beaufort = 3

        if self._env_pub is not None:
            self._env_pub.publish(msg)


def main() -> None:
    """CLI entry point (``ros2 run`` / console script)."""
    rclpy.init()
    node = EnvDisturbanceNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()
