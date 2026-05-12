"""Environment Disturbance Node — Gauss-Markov wind + current model.

Stub for D1.3b.3 SIL foundation. Provides a first-order Gauss-Markov
wind process and constant current offset. Replaced by a real oceanographic
model at D2.5 integration.
"""
from __future__ import annotations

import math
import random


class EnvDisturbanceNode:
    """First-order Gauss-Markov wind + constant current model.

    Parameters
    ----------
    tau_wind : float
        Correlation time constant [s]. Default 300.0 (~5 min).
    sigma : float
        Steady-state wind speed standard deviation [m/s]. Default 2.0.
    """

    def __init__(self, tau_wind: float = 300.0, sigma: float = 2.0) -> None:
        self.tau_wind = tau_wind
        self.sigma = sigma

        # Initial conditions
        self._wind_dir: float = 0.0
        self._wind_speed: float = 5.0
        self._current_dir: float = 0.0
        self._current_speed: float = 0.5
        self._prev_wind_speed: float = 5.0
        self._prev_wind_dir: float = 0.0

    def step(self, dt: float = 1.0) -> dict:
        """Advance the model by *dt* seconds and return the environment state.

        Wind speed follows a discrete-time Gauss-Markov process:
            w_{k+1} = exp(-dt/tau) * w_k + N(0, sigma*sqrt(1 - exp(-2*dt/tau)))
        Wind direction gets a small random walk perturbation.
        Current is constant (placeholder — replaced by tidal model in D2.5).
        """
        alpha = math.exp(-dt / self.tau_wind)
        noise = random.gauss(0, self.sigma * math.sqrt(1.0 - alpha**2))
        self._wind_speed = alpha * self._prev_wind_speed + noise
        self._wind_dir = (self._wind_dir + random.gauss(0, 0.1)) % 360.0
        self._prev_wind_speed = self._wind_speed

        return {
            "wind_direction_deg": self._wind_dir,
            "wind_speed_mps": max(0.0, self._wind_speed),
            "current_direction_deg": self._current_dir,
            "current_speed_mps": self._current_speed,
            "visibility_nm": 10.0,
            "sea_state_beaufort": 3,
        }


def main() -> None:
    """CLI entry point (ros2 run / console script)."""
    print("EnvDisturbanceNode ready")
