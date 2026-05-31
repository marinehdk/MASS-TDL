import os
import sys
import numpy as np
import gymnasium as gym
from gymnasium.spaces import Box
from math import radians, cos, sqrt

# Inject paths to import ship_dynamics and shell_b_harness if not already importable
for p in [
    "/opt/ws/src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/install/shell_b_harness/local/lib/python3.10/dist-packages",
    "./src/sim_workbench/sil_nodes/ship_dynamics",
    "/opt/ws/src/sim_workbench/shell_b_harness",
]:
    abs_p = os.path.abspath(p)
    if abs_p not in sys.path:
        sys.path.insert(0, abs_p)

# rl-isolation-ok: env wraps simulator
from shell_b_harness.simulator import ShellBSimulator


class MASSL3Env(gym.Env):
    """
    Gymnasium environment that wraps the ShellBSimulator for reinforcement learning.
    """
    def __init__(self, port=9095, use_m7=True, verbose=False, ros_domain_id=42, headless=True):
        super().__init__()
        self.port = port
        self.use_m7 = use_m7
        self.verbose = verbose
        self.ros_domain_id = ros_domain_id
        self.headless = headless

        self.simulator = None
        self._first_reset = True

        # Action space: [target_heading_deg, target_speed_kn]
        # target_heading_deg: [-180, 180]
        # target_speed_kn: [0, 25]
        self.action_space = Box(
            low=np.array([-180.0, 0.0], dtype=np.float32),
            high=np.array([180.0, 25.0], dtype=np.float32),
            dtype=np.float32
        )

        # Observation space:
        # [own_x, own_y, own_psi, own_u, own_v, own_r, target_lat, target_lon, target_heading, target_sog, target_mmsi]
        self.observation_space = Box(
            low=-np.inf,
            high=np.inf,
            shape=(11,),
            dtype=np.float32
        )

    def _get_obs(self, state) -> np.ndarray:
        own = state["own_ship"]
        target = state["target_vessels"][0]
        return np.array([
            own["x"],
            own["y"],
            own["psi"],
            own["u"],
            own["v"],
            own["r"],
            target["lat"],
            target["lon"],
            target["heading"],
            target["sog"],
            target["mmsi"]
        ], dtype=np.float32)

    def reset(self, seed=None, options=None):
        super().reset(seed=seed)

        in_place = True
        if self.simulator is None:
            self.simulator = ShellBSimulator(
                port=self.port,
                use_m7=self.use_m7,
                verbose=self.verbose,
                ros_domain_id=self.ros_domain_id,
                headless=self.headless
            )
            in_place = False
            self._first_reset = False
        elif (self._first_reset or
              self.simulator.doer_proc is None or
              self.simulator.doer_proc.poll() is not None or
              (self.simulator.use_m7 and (self.simulator.m7_proc is None or self.simulator.m7_proc.poll() is not None))):
            in_place = False
            self._first_reset = False

        state = self.simulator.reset(seed=seed, in_place=in_place)
        obs = self._get_obs(state)
        return obs, state

    def step(self, action):
        if self.simulator is None:
            raise RuntimeError("Environment has not been reset yet. Call reset() before step().")

        # Clip action to action space limits
        action = np.clip(action, self.action_space.low, self.action_space.high)

        # Set autopilot targets
        self.simulator.target_heading_deg = float(action[0])
        self.simulator.target_sog_kn = float(action[1])

        # Step simulator
        state = self.simulator.step()

        # Extract observation
        obs = self._get_obs(state)

        # Calculate reward and terminated/truncated conditions
        own_ship = state["own_ship"]
        target = state["target_vessels"][0]
        own_x = own_ship["x"]
        own_y = own_ship["y"]
        target_lat = target["lat"]
        target_lon = target["lon"]

        # Convert target lat/lon to local meters relative to origin:
        y_t = (target_lat - 63.44) * 111120.0
        x_t = (target_lon - 10.38) * 111120.0 * cos(radians(63.44))
        d = sqrt((own_x - x_t) ** 2 + (own_y - y_t) ** 2)

        if d < 50.0:
            terminated = True
            reward = -1000.0
        else:
            terminated = False
            reward = 1.0
            if d < 200.0:
                reward -= (200.0 - d) / 200.0

        truncated = bool(self.simulator.sim_t > 150.0)

        return obs, reward, terminated, truncated, state

    def close(self):
        if self.simulator is not None:
            self.simulator.close()
            self.simulator = None
