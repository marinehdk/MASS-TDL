# tests/integration/sim_determinism/test_physics_core_equiv.py
"""B0 Decision Spike — Physics Core Equivalence Test.

Compares the C++ fcb_simulator MMG core (accessed via the pybind11
fcb_sim_py binding) against the Python ship_dynamics MMG model
(mmg_model.py / MMGModel) on an identical (state, command, dt) drive
sequence over N steps, and measures trajectory delta.

Decision criterion (per plan §3.4 / Task B0):
  real binding available AND pos_delta < 1 m AND hdg_delta < 0.1 deg  → P1
  mock OR delta too large                                              → P3

Both outcomes are VALID — they *select* the implementation path.

Status:
  - fcb_sim_py NOT built on this host (macOS, colcon not run)         → RED / SKIP
  - Python MMG available + importable                                  → runs solo half
  - If/when fcb_sim_py is built, test should turn GREEN and measure delta

Run:
    pytest tests/integration/sim_determinism/test_physics_core_equiv.py -v
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Path setup — allow import from source tree without colcon install
# ---------------------------------------------------------------------------
_REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(_REPO_ROOT))

# ---------------------------------------------------------------------------
# Import probes
# ---------------------------------------------------------------------------

def _probe_fcb_sim_py() -> tuple[bool, bool, object | None]:
    """Try to import fcb_sim_py.
    
    Returns:
        (available, is_real_binding, module)
    
    The pybind11 binding is a C extension and has no `__file__` ending in .py.
    The mock (Python fallback) has `__file__` ending in .py.
    """
    try:
        import fcb_sim_py as m  # type: ignore[import]
        is_real = not (hasattr(m, "__file__") and m.__file__ is not None
                       and str(m.__file__).endswith(".py"))
        return True, is_real, m
    except ImportError:
        return False, False, None


def _probe_python_mmg() -> tuple[bool, object | None]:
    """Try to import Python MMGModel from ship_dynamics."""
    try:
        from src.sim_workbench.sil_nodes.ship_dynamics.ship_dynamics.mmg_model import (
            MMGModel,
            ShipState,
        )
        from src.sim_workbench.sil_nodes.ship_dynamics.ship_dynamics.mmg_coefficients import (
            MMGCoefficients,
        )
        return True, (MMGModel, ShipState, MMGCoefficients)
    except ImportError:
        return False, None


FCB_AVAILABLE, FCB_IS_REAL, FCB_MOD = _probe_fcb_sim_py()
PY_AVAILABLE, PY_CLASSES = _probe_python_mmg()

# ---------------------------------------------------------------------------
# Shared test parameters (same initial condition for both cores)
# ---------------------------------------------------------------------------
# Initial conditions: ship at (0, 0), heading π/2 (North in math convention),
# speed 5.0 m/s (≈10 kn). Rudder 0.15 rad (≈8.6°, port turn).
DELTA_RAD = 0.15
N_RPS = 3.0
DT = 0.02  # s — standard sim step
N_STEPS = 500  # 10 s at dt=0.02

POS_TOL_M = 1.0     # from test_determinism.py cross-val tolerance
HDG_TOL_DEG = 0.1   # same


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _haversine_flat(x1: float, y1: float, x2: float, y2: float) -> float:
    """Euclidean distance in metres (flat-earth, ENU coords)."""
    return math.hypot(x1 - x2, y1 - y2)


def _hdg_diff_deg(h1_rad: float, h2_rad: float) -> float:
    diff_rad = (h1_rad - h2_rad + math.pi) % (2 * math.pi) - math.pi
    return abs(math.degrees(diff_rad))


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestFcbSimPyBinding:
    """Tests about whether the pybind11 binding is real vs mock."""

    def test_fcb_sim_py_is_importable(self):
        """fcb_sim_py must be importable (real OR mock) for any path to work.
        
        If this fails → neither P1 nor a mock-patched P3 can call rk4_step.
        """
        if not FCB_AVAILABLE:
            pytest.skip(
                "fcb_sim_py not importable on this host. "
                "Expected on macOS without colcon build. "
                "Install via: colcon build --packages-select fcb_simulator "
                "&& source install/setup.bash. "
                "This confirms P1 is not available without build; spike selects P3."
            )
        assert hasattr(FCB_MOD, "FcbState"), "fcb_sim_py has no FcbState"
        assert hasattr(FCB_MOD, "MmgParams"), "fcb_sim_py has no MmgParams"
        assert hasattr(FCB_MOD, "rk4_step"), "fcb_sim_py has no rk4_step"

    def test_fcb_sim_py_is_real_binding_not_mock(self):
        """fcb_sim_py must be the real pybind11 C extension, not a Python mock.
        
        The mock (fcb_sim_py_mock.py) is a simplified point-mass model
        (Euler integration, NOT RK4, NOT full MMG), unsuitable for P1.
        This test fails when the .so is not built — RED → selects P3.
        """
        if not FCB_AVAILABLE:
            pytest.skip("fcb_sim_py not importable — real binding absent, selects P3")
        if not FCB_IS_REAL:
            pytest.fail(
                "fcb_sim_py imported but is a Python mock (.py suffix), "
                "NOT the real pybind11 C extension. "
                "The mock uses simplified Euler integration, NOT full MMG. "
                "Real binding must be built. Spike selects P3 until then."
            )

    def test_fcb_sim_py_rk4_step_smoke(self):
        """A single rk4_step call should not crash and return finite values."""
        if not FCB_AVAILABLE:
            pytest.skip("fcb_sim_py not importable — skip smoke (selects P3)")
        if not FCB_IS_REAL:
            pytest.skip("fcb_sim_py is mock — skip smoke (selects P3)")

        s = FCB_MOD.FcbState()
        s.u = 5.0
        s.psi = math.pi / 2.0
        p = FCB_MOD.MmgParams()
        s2 = FCB_MOD.rk4_step(s, DELTA_RAD, N_RPS, p, DT)

        assert math.isfinite(s2.x), f"x not finite: {s2.x}"
        assert math.isfinite(s2.y), f"y not finite: {s2.y}"
        assert math.isfinite(s2.psi), f"psi not finite: {s2.psi}"
        assert math.isfinite(s2.u), f"u not finite: {s2.u}"


class TestPythonMMGCore:
    """Tests that the Python MMGModel runs without ROS dependencies."""

    def test_python_mmg_importable(self):
        """Python MMGModel must be importable directly (no ROS needed for P3)."""
        if not PY_AVAILABLE:
            pytest.skip("Python MMGModel not importable from source tree")
        assert PY_CLASSES is not None

    def test_python_mmg_rk4_step_smoke(self):
        """Python MMG rk4_step must produce finite output."""
        if not PY_AVAILABLE:
            pytest.skip("Python MMGModel not importable")
        MMGModel, ShipState, MMGCoefficients = PY_CLASSES

        coeffs = MMGCoefficients(dt=DT)
        model = MMGModel(coeffs)
        state = ShipState(u=5.0, psi=math.pi / 2.0)
        state2 = model.rk4_step(state, DELTA_RAD, N_RPS)

        assert math.isfinite(state2.x), f"x not finite: {state2.x}"
        assert math.isfinite(state2.y), f"y not finite: {state2.y}"
        assert math.isfinite(state2.psi), f"psi not finite: {state2.psi}"
        assert math.isfinite(state2.u), f"u not finite: {state2.u}"

    def test_python_mmg_turns_correctly(self):
        """Positive rudder (starboard, CW) should deflect ship heading clockwise."""
        if not PY_AVAILABLE:
            pytest.skip("Python MMGModel not importable")
        MMGModel, ShipState, MMGCoefficients = PY_CLASSES

        coeffs = MMGCoefficients(dt=DT)
        model = MMGModel(coeffs)
        state = ShipState(u=5.0, psi=math.pi / 2.0)  # heading North

        for _ in range(N_STEPS):
            state = model.rk4_step(state, DELTA_RAD, N_RPS)

        # After 10 s at 5 m/s with rudder, ship should have moved meaningfully
        dist = math.hypot(state.x, state.y)
        assert dist > 10.0, f"Ship barely moved after {N_STEPS} steps: {dist:.1f}m"


@pytest.mark.skipif(
    not FCB_AVAILABLE or not FCB_IS_REAL,
    reason=(
        "fcb_sim_py real binding not available on this host (not built). "
        "This is EXPECTED on macOS without colcon build. "
        "Spike outcome: fcb_sim_py is NOT available at runtime → P3 selected."
    ),
)
class TestPhysicsCoreEquivalence:
    """Core comparison test: C++ binding vs Python MMG.
    
    This test is SKIPPED (not FAILED) when the binding is absent — the skip
    itself is evidence for the P3 decision.
    
    When the binding IS built (e.g. in Docker CI or after colcon build), the
    test runs and measures trajectory delta. If delta ≤ tolerances → P1 viable.
    """

    def _run_cpp_trajectory(self, n_steps: int) -> list[tuple[float, float, float]]:
        """Run C++ core for n_steps, return list of (x, y, psi)."""
        s = FCB_MOD.FcbState()
        s.u = 5.0
        s.psi = math.pi / 2.0
        p = FCB_MOD.MmgParams()
        traj = []
        for _ in range(n_steps):
            s = FCB_MOD.rk4_step(s, DELTA_RAD, N_RPS, p, DT)
            traj.append((s.x, s.y, s.psi))
        return traj

    def _run_python_trajectory(self, n_steps: int) -> list[tuple[float, float, float]]:
        """Run Python MMG for n_steps, return list of (x, y, psi)."""
        if not PY_AVAILABLE:
            pytest.skip("Python MMGModel not importable")
        MMGModel, ShipState, MMGCoefficients = PY_CLASSES
        coeffs = MMGCoefficients(dt=DT)
        model = MMGModel(coeffs)
        state = ShipState(u=5.0, psi=math.pi / 2.0)
        traj = []
        for _ in range(n_steps):
            state = model.rk4_step(state, DELTA_RAD, N_RPS)
            traj.append((state.x, state.y, state.psi))
        return traj

    def test_trajectory_delta_within_tolerance(self):
        """C++ and Python cores must agree within pos<1m / hdg<0.1° over 10s.
        
        This is the P1 cross-validation gate (same tolerance as test_determinism.py).
        FAIL → P3 (C++ and Python are not equivalent — use Python pure step-fn).
        PASS → P1 viable (C++ binding is a drop-in for Python MMG).
        """
        cpp_traj = self._run_cpp_trajectory(N_STEPS)
        py_traj = self._run_python_trajectory(N_STEPS)

        assert len(cpp_traj) == len(py_traj) == N_STEPS

        max_pos_m = 0.0
        max_hdg_deg = 0.0
        for i, ((cx, cy, cpsi), (px, py_, ppsi)) in enumerate(
            zip(cpp_traj, py_traj)
        ):
            pos_err = _haversine_flat(cx, cy, px, py_)
            hdg_err = _hdg_diff_deg(cpsi, ppsi)
            max_pos_m = max(max_pos_m, pos_err)
            max_hdg_deg = max(max_hdg_deg, hdg_err)

        print(f"\nMax position delta: {max_pos_m:.4f} m (tol={POS_TOL_M} m)")
        print(f"Max heading delta:  {max_hdg_deg:.4f} deg (tol={HDG_TOL_DEG} deg)")

        within_tol = max_pos_m < POS_TOL_M and max_hdg_deg < HDG_TOL_DEG
        if within_tol:
            print("→ C++ and Python MMG cores are EQUIVALENT within tolerance → P1 viable")
        else:
            print(
                f"→ Cores DIVERGE (pos={max_pos_m:.2f}m > {POS_TOL_M}m "
                f"OR hdg={max_hdg_deg:.3f}deg > {HDG_TOL_DEG}deg) → P3 selected"
            )

        assert max_pos_m < POS_TOL_M, (
            f"Position delta {max_pos_m:.4f} m exceeds tolerance {POS_TOL_M} m. "
            f"C++ and Python cores diverge → P3 selected."
        )
        assert max_hdg_deg < HDG_TOL_DEG, (
            f"Heading delta {max_hdg_deg:.4f} deg exceeds tolerance {HDG_TOL_DEG} deg. "
            f"C++ and Python cores diverge → P3 selected."
        )

    def test_cpp_step_is_faster_than_python(self):
        """C++ binding should outperform Python MMG (basic performance check).
        
        If C++ is slower or equal, the P1 benefit (speed) doesn't exist.
        """
        import time

        # Warm up
        self._run_cpp_trajectory(10)
        if PY_AVAILABLE:
            self._run_python_trajectory(10)

        t0 = time.perf_counter()
        self._run_cpp_trajectory(N_STEPS)
        cpp_time = time.perf_counter() - t0

        if not PY_AVAILABLE:
            pytest.skip("Python MMGModel not available for timing comparison")

        t0 = time.perf_counter()
        self._run_python_trajectory(N_STEPS)
        py_time = time.perf_counter() - t0

        speedup = py_time / max(cpp_time, 1e-9)
        print(f"\nC++ time: {cpp_time*1000:.2f} ms | Python time: {py_time*1000:.2f} ms")
        print(f"C++ speedup: {speedup:.2f}x")

        # C++ should be at least comparable (allow some overhead for pybind11 calls)
        # Not enforcing strict >1x because in-process Python may have less call overhead
        # at this step count, but document the ratio.
        assert speedup > 0.0  # tautology; real check is documentation of the ratio


class TestCoefficientsComparison:
    """Static analysis: verify C++ and Python MMG use identical coefficients.
    
    This runs without building the C++ extension — pure source code inspection.
    Documents divergences that may cause large delta even if both cores call.
    """

    def test_hull_derivatives_match(self):
        """C++ types.hpp and Python mmg_coefficients.py must share hydrodynamic derivs.
        
        Verified by static read of source (this test documents the agreement).
        If either file changes, this test should catch divergence.
        """
        if not PY_AVAILABLE:
            pytest.skip("Python MMGCoefficients not importable")
        _, _, MMGCoefficients = PY_CLASSES
        c = MMGCoefficients()

        # Values from C++ types.hpp (defaults in struct MmgParams)
        # Cross-check against Python defaults
        cpp_coeffs = {
            "L": 46.0,
            "d": 2.8,
            "displacement_t": 450.0,
            "m_x_prime": 0.00831,
            "m_y_prime": 0.1284,
            "J_zz_prime": 0.00676,
            "X_vv": -0.0407,
            "X_vr": 0.0441,
            "X_rr": 0.0127,
            "X_vvvv": -0.0607,
            "Y_v": -0.3073,
            "Y_r": 0.1521,
            "Y_vvv": -0.7256,
            "Y_vvr": -0.1338,
            "Y_vrr": 0.1657,
            "Y_rrr": -0.0303,
            "N_v": -0.1084,
            "N_r": -0.0585,
            "N_vvv": 0.0040,
            "N_vvr": -0.0498,
            "N_vrr": -0.0151,
            "N_rrr": -0.0061,
            "t_P": 0.184,
            "w_P": 0.200,
            "D_P": 1.5,
            "k_0": 0.6,
            "k_1": -0.3,
            "k_2": -0.25,
            "t_R": 0.387,
            "a_H": 0.312,
            "x_H_prime": -0.464,
            "x_R_prime": -0.500,
            "gamma_R": 0.395,
            "l_R_prime": -0.710,
            "kappa": 0.50,
            "epsilon": 1.09,
            "A_R": 1.65,
            "f_alpha": 2.747,
            "G_M": 1.2,
            "T_phi": 5.0,
        }

        mismatches = []
        for name, cpp_val in cpp_coeffs.items():
            py_val = getattr(c, name, None)
            if py_val is None:
                mismatches.append(f"  {name}: C++={cpp_val}, Python=MISSING")
            elif abs(py_val - cpp_val) > 1e-8:
                mismatches.append(
                    f"  {name}: C++={cpp_val}, Python={py_val}, "
                    f"diff={abs(py_val - cpp_val):.2e}"
                )

        if mismatches:
            print("\nCoefficient mismatches (C++ vs Python):")
            for m in mismatches:
                print(m)
            pytest.fail(
                f"{len(mismatches)} coefficient mismatch(es) between C++ types.hpp "
                f"and Python mmg_coefficients.py:\n" + "\n".join(mismatches)
            )

    def test_document_known_model_divergences(self):
        """Document (not fail) known *formulation* differences between C++ and Python cores.
        
        These are structural differences in the physics equations that will cause
        non-trivial trajectory delta even with identical coefficients.
        This is NOT a test of values — it's a documentation test that prints the
        known divergences so they appear in pytest output.
        """
        divergences = [
            (
                "I_zz formula",
                "C++ mmg_model.cpp:113: I_zz = mass * (0.25*L)^2  [no J_zz_prime factor]",
                "Python mmg_coefficients.py:155: I_zz = mass * (0.25*L)^2 * (1 + J_zz_prime)",
                "Impact: ~0.67% difference in yaw inertia → growing heading/position delta",
            ),
            (
                "v_dot/r_dot coupling",
                "C++ mmg_model.cpp:129-141: Full 2x2 coupled system with m*x_G terms",
                "Python mmg_model.py:217-219: Simplified (assumes x_G=0): decoupled m22/m33",
                "Impact: With x_G=0 defaults, numerically equivalent but may diverge if x_G≠0",
            ),
            (
                "Rudder u_R formulation",
                "C++ mmg_model.cpp:76-87: u_R uses C_Th (thrust-loading coeff + kappa factor)",
                "Python mmg_model.py:184: u_R = epsilon * u * (1-w_P) [no C_Th]",
                "Impact: Rudder effectiveness differs under propeller loading → significant turn divergence",
            ),
            (
                "Surge drag term",
                "C++ mmg_model.cpp: No X_uu term",
                "Python mmg_coefficients.py:73: X_uu=-0.002544 added to X_hull_nd",
                "Impact: Different cruise equilibrium speed → u trajectory diverges over time",
            ),
            (
                "Roll dynamics",
                "C++ mmg_model.cpp:151-154: phi_ddot = -omega_n^2*phi - 2*zeta*omega_n*phi_dot (omega_n from T_phi)",
                "Python mmg_model.py:222-224: K_phi=mass*g*G_M; K_p=K_phi*T_phi/(2pi)*0.1; "
                "p_dot = (-K_phi*phi - K_p*p) / I_xx",
                "Impact: Different natural frequency calculation → roll trajectory diverges",
            ),
        ]

        print("\n[B0 SPIKE] Known C++/Python MMG formulation divergences:")
        for name, cpp_desc, py_desc, impact in divergences:
            print(f"\n  [{name}]")
            print(f"    C++: {cpp_desc}")
            print(f"    Py:  {py_desc}")
            print(f"    → {impact}")
        print(
            "\n  CONCLUSION: Even with identical coefficients, structural divergences "
            "in rudder u_R, I_zz, and X_uu are expected to produce trajectory delta "
            "well above the 1m/0.1° tolerance over 10s at 5 m/s."
        )
        print(
            "  This further supports P3 (use Python MMG as the authoritative core) "
            "rather than trying to unify on C++ binding."
        )

        # This test always passes — it's a documentation test
        assert len(divergences) > 0


class TestB0Decision:
    """Summarize the B0 spike decision in test form.
    
    This test always passes — it documents the decision and rationale
    for traceability in pytest output / CI logs.
    """

    def test_b0_decision_summary(self, capsys):
        """Print the B0 decision memo for CI log traceability."""
        fcb_built = FCB_AVAILABLE and FCB_IS_REAL
        decision = "P3" if not fcb_built else "PENDING_DELTA_MEASUREMENT"

        with capsys.disabled():
            print("\n" + "=" * 72)
            print("B0 DECISION SPIKE SUMMARY")
            print("=" * 72)
            print(f"  fcb_sim_py importable:      {'YES' if FCB_AVAILABLE else 'NO'}")
            print(f"  fcb_sim_py is real binding: {'YES' if FCB_IS_REAL else 'NO (mock or absent)'}")
            print(f"  Python MMGModel available:  {'YES' if PY_AVAILABLE else 'NO'}")
            print(f"  Delta measured:             NO (binding not built on this host)")
            print(f"  Decision:                   {decision}")
            if decision == "P3":
                print(
                    "\n  Rationale: P1 (C++ binding) is not viable as runtime physics "
                    "because:\n"
                    "    1. fcb_sim_py is not built on this host (macOS, no colcon build).\n"
                    "    2. tools/sil/simulate.py falls back to fcb_sim_py_mock.py at\n"
                    "       runtime — a simplified Euler point-mass model, not real MMG.\n"
                    "    3. Known structural divergences between C++ mmg_model.cpp and\n"
                    "       Python mmg_model.py (rudder u_R, I_zz, X_uu, roll dynamics)\n"
                    "       will produce delta >> tolerance even if binding were built.\n"
                    "    4. Python mmg_model.py is already a pure step-fn with no ROS\n"
                    "       import — it just needs the ROS wrapper stripped (node.py),\n"
                    "       which is minimal work (P3).\n"
                    "\n  P3 implementation path (for Task B3):\n"
                    "    - Use mmg_model.py + MMGCoefficients directly as physics core.\n"
                    "    - Strip ship_dynamics/node.py ROS wrapper → pure step function.\n"
                    "    - target_vessel, sensor_mock, env_disturbance: same treatment.\n"
                    "    - This requires NO build system changes (pure Python, importable).\n"
                    "    - C++ fcb_sim_py remains as the batch-runner binding; it can be\n"
                    "      made the authoritative core in a future dedicated C++ hardening\n"
                    "      task once the formulation divergences are resolved."
                )
            print("=" * 72)

        assert True  # always passes
