# scenario_authoring/replay/target_modes.py
"""Target ship motion mode interfaces. D1.3b.2 implements AisReplayVessel only."""
from __future__ import annotations

from abc import ABC, abstractmethod

import numpy as np


class TargetShipReplayer(ABC):
    """Abstract interface for target ship trajectory provision at 50 Hz."""

    @abstractmethod
    def get_targets_at(self, t_s: float):
        """Return target state(s) at simulation time t_s."""
        ...


class AisReplayVessel(TargetShipReplayer):
    """Replay historical AIS trajectory at 50 Hz (D1.3b.2 Phase 1).

    trajectory: (N, 5) array — t, lat, lon, sog, cog at 50 Hz.
    """

    def __init__(self, trajectory: np.ndarray) -> None:
        self._traj = trajectory
        self._t_start = float(trajectory[0, 0])
        self._t_end = float(trajectory[-1, 0])

    def get_targets_at(self, t_s: float) -> dict | None:
        """Return dict with lat/lon/sog/cog at time t_s, or None if out of range."""
        if t_s < self._t_start or t_s > self._t_end:
            return None
        idx = int((t_s - self._t_start) / 0.02)
        idx = min(idx, len(self._traj) - 1)
        row = self._traj[idx]
        return {"lat": float(row[1]), "lon": float(row[2]),
                "sog_kn": float(row[3]), "cog_deg": float(row[4])}


class NcdmVessel(TargetShipReplayer):
    """NCDM Ornstein-Uhlenbeck stochastic prediction (D2.4)."""

    def __init__(self, lat0, lon0, heading0_deg, sog_kn, duration_s=600.0, dt=0.1,
                 ou_theta=0.05, ou_sigma=0.5, seed=None):
        import math
        rng = np.random.default_rng(seed)
        n = int(duration_s / dt)
        headings = np.zeros(n)
        headings[0] = math.radians(heading0_deg)
        h_ref = headings[0]
        for i in range(1, n):
            dH = (-ou_theta * (headings[i-1] - h_ref) * dt
                  + ou_sigma * math.sqrt(dt) * rng.standard_normal())
            headings[i] = headings[i-1] + dH
        sog_ms = sog_kn * 0.514444
        lats = np.zeros(n)
        lons = np.zeros(n)
        lats[0] = lat0
        lons[0] = lon0
        for i in range(1, n):
            lat_r = math.radians(lats[i-1])
            lats[i] = lats[i-1] + sog_ms * math.cos(headings[i-1]) * dt / 111120.0
            lons[i] = lons[i-1] + sog_ms * math.sin(headings[i-1]) * dt / (111120.0 * math.cos(lat_r))
        t = np.arange(n) * dt
        cogs_deg = np.degrees(headings) % 360.0
        self._traj = np.column_stack([t, lats, lons, np.full(n, sog_kn), cogs_deg])
        self._t_end = float(t[-1])
        self._dt = dt

    def get_targets_at(self, t_s: float):
        if t_s < 0.0 or t_s > self._t_end:
            return None
        idx = min(int(t_s / self._dt), len(self._traj) - 1)
        row = self._traj[idx]
        return {"lat": float(row[1]), "lon": float(row[2]),
                "sog_kn": float(row[3]), "cog_deg": float(row[4])}


class IntelligentVessel(TargetShipReplayer):
    """VO/MPC multi-agent interactive target. STUB — D3.6."""

    def __init__(self) -> None:
        raise NotImplementedError("IntelligentVessel: D3.6")

    def get_targets_at(self, t_s: float):
        raise NotImplementedError("IntelligentVessel: D3.6")
