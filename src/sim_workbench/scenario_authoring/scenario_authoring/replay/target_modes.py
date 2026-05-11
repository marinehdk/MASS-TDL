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
    """NCDM Ornstein-Uhlenbeck stochastic prediction. STUB — D2.4."""

    def __init__(self) -> None:
        raise NotImplementedError("NcdmVessel: D2.4")

    def get_targets_at(self, t_s: float):
        raise NotImplementedError("NcdmVessel: D2.4")


class IntelligentVessel(TargetShipReplayer):
    """VO/MPC multi-agent interactive target. STUB — D3.6."""

    def __init__(self) -> None:
        raise NotImplementedError("IntelligentVessel: D3.6")

    def get_targets_at(self, t_s: float):
        raise NotImplementedError("IntelligentVessel: D3.6")
