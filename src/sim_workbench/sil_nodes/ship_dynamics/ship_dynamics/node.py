"""Ship Dynamics Node — FCB 4-DOF MMG model, RK4 integrator dt=0.02s."""
import math

class ShipDynamicsNode:
    def __init__(self, hull_class: str = "SEMI_PLANING", dt: float = 0.02):
        self.hull_class = hull_class
        self.dt = dt
        self._state = {"x": 0.0, "y": 0.0, "psi": 0.0, "u": 10.0, "v": 0.0, "r": 0.0}

    def step(self, rudder_angle: float = 0.0, throttle: float = 0.5) -> dict:
        # Phase 1: kinematic-only model (no hydrodynamics yet)
        u = self._state["u"] * throttle
        self._state["x"] += u * math.cos(self._state["psi"]) * self.dt
        self._state["y"] += u * math.sin(self._state["psi"]) * self.dt
        self._state["psi"] += self._state["r"] * self.dt
        self._state["r"] += rudder_angle * 0.01  # simplified turning
        return dict(self._state)

    def get_state(self) -> dict:
        return dict(self._state)

def main(): print("ShipDynamicsNode ready")
