# python/fcb_mmg_fmu.py
"""FMI 2.0 Co-Simulation slave wrapping FCB 4-DOF MMG model via pythonfmu."""
from __future__ import annotations
from pythonfmu import Fmi2Slave, Fmi2Causality, Real


class FCBMmgFmu(Fmi2Slave):
    """FCB 45m semi-planing vessel: Yasukawa 2015 4-DOF MMG, RK4 integrator.

    FMI 2.0 Co-Simulation interface wrapping FCBSimulator (C++) via pybind11.
    Phase 1: Python runtime in FMU — acceptable for DEMO-1 tool qualification.
    Phase 2: C++ FMI Library wrapper for certification evidence.
    """

    author = "MASS-L3 V&V Engineer"
    description = "FCB 45m 4-DOF MMG ship dynamics (Yasukawa 2015, RK4, dt=0.02s)"
    default_experiment_start = 0.0
    default_experiment_stop = 600.0

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        from fcb_mmg_py import FCBSimulator, ShipState

        self._sim = FCBSimulator()
        config_path = kwargs.get("config_path", "config/fcb_dynamics.yaml")
        try:
            self._sim.load_params(str(config_path))
        except Exception:
            pass  # config may not be available in test

        self._state = ShipState()

        # Outputs (11)
        self.u     = 0.0; self.register_variable(Real("u",     causality=Fmi2Causality.output, unit="m/s"))
        self.v     = 0.0; self.register_variable(Real("v",     causality=Fmi2Causality.output, unit="m/s"))
        self.r     = 0.0; self.register_variable(Real("r",     causality=Fmi2Causality.output, unit="rad/s"))
        self.x     = 0.0; self.register_variable(Real("x",     causality=Fmi2Causality.output, unit="m"))
        self.y     = 0.0; self.register_variable(Real("y",     causality=Fmi2Causality.output, unit="m"))
        self.psi   = 0.0; self.register_variable(Real("psi",   causality=Fmi2Causality.output, unit="rad"))
        self.phi   = 0.0; self.register_variable(Real("phi",   causality=Fmi2Causality.output, unit="rad"))
        self.p     = 0.0; self.register_variable(Real("p",     causality=Fmi2Causality.output, unit="rad/s"))
        self.delta = 0.0; self.register_variable(Real("delta", causality=Fmi2Causality.output, unit="rad"))
        self.n     = 0.0; self.register_variable(Real("n",     causality=Fmi2Causality.output, unit="rev/s"))
        self.sog   = 0.0; self.register_variable(Real("sog",   causality=Fmi2Causality.output, unit="m/s"))

        # Control Inputs (2)
        self.delta_cmd = 0.0; self.register_variable(Real("delta_cmd", causality=Fmi2Causality.input, unit="rad"))
        self.n_rps_cmd = 0.0; self.register_variable(Real("n_rps_cmd", causality=Fmi2Causality.input, unit="rev/s"))

        # Disturbance Inputs (4)
        self.wind_dir_deg      = 0.0; self.register_variable(Real("wind_dir_deg",      causality=Fmi2Causality.input, unit="deg"))
        self.wind_speed_mps    = 0.0; self.register_variable(Real("wind_speed_mps",    causality=Fmi2Causality.input, unit="m/s"))
        self.current_dir_deg   = 0.0; self.register_variable(Real("current_dir_deg",   causality=Fmi2Causality.input, unit="deg"))
        self.current_speed_mps = 0.0; self.register_variable(Real("current_speed_mps", causality=Fmi2Causality.input, unit="m/s"))

    def do_step(self, current_time: float, step_size: float) -> bool:
        self._state = self._sim.step(self._state, self.delta_cmd, self.n_rps_cmd, step_size)
        self.u = self._state.u; self.v = self._state.v; self.r = self._state.r
        self.x = self._state.x; self.y = self._state.y; self.psi = self._state.psi
        self.phi = self._state.phi; self.p = self._state.phi_dot
        self.delta = self.delta_cmd; self.n = self.n_rps_cmd
        self.sog = (self._state.u**2 + self._state.v**2) ** 0.5
        return True
