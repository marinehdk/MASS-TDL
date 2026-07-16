"""P1b-0 T0 — acados staging shared base (extracted from P1a gen_m5_subset.py).

This module factors the P1a minimal M5 subset (discrete explicit dynamics +
single-target CPA nonlinear path constraint + heading box + EXACT hessian +
MERIT_BACKTRACKING globalization + warm-start seed) into a shared
`build_base_ocp()` so the P1b staging tasks T1-T5 can each add ONE complexity
point (stage cost, slack, bounds, merged, ...) on top of the same base.

It does NOT set any stage cost — each task's gen script adds its own cost
before calling `AcadosOcpSolver.generate`. It also does NOT run anything on
import.

References (authoritative P1a templates, real acados API patterns):
  - ../acados_m5_subset/gen_m5_subset.py        dynamics/CPA/box/solver-options
  - ../acados_m5_subset/subset_runner.cpp:64-78 C warm-start seed loop
                                                  (forward_seed reproduces it)

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import math

import casadi as ca
import numpy as np
from acados_template import AcadosModel, AcadosOcp

# Module-level constants.
UH_INF = 1e10        # F2: bounded pseudo-infinity (t_renderer rejects JSON Infinity)
N_DEFAULT = 10
DT_DEFAULT = 5.0


def build_base_ocp(N=10, DT=5.0, target_x=200.0, target_y=0.0,
                   cpa_hard=100.0, psi_lb=-1.2, psi_ub=1.2) -> AcadosOcp:
    """Build and return a configured AcadosOcp carrying the P1a subset.

    Parameterizes the CPA constraint by `target_x/target_y/cpa_hard` (NOT the
    P1a module constants) so each staging task can reuse the base with its own
    target/radius. Does NOT set any cost — each task adds its own cost type,
    W, yref before code-gen. Does NOT call generate.
    """
    ocp = AcadosOcp()

    # ---- Model (AcadosModel) ----
    model = AcadosModel()
    model.name = "m5_staging_base"

    px = ca.SX.sym("px")
    py = ca.SX.sym("py")
    psi = ca.SX.sym("psi")
    u = ca.SX.sym("u")          # surge speed (m/s), held constant
    x = ca.vertcat(px, py, psi, u)

    dpsi = ca.SX.sym("dpsi")    # control: heading rate (rad/s)
    u_ctrl = ca.vertcat(dpsi)

    # Discrete explicit dynamics (formulation.cpp:360-361 + heading-rate control).
    # With integrator_type=DISCRETE acados uses model.disc_dyn_expr directly
    # (no RK integration). This carries the exact formulation.cpp:360-361 step.
    f_expl = ca.vertcat(
        px + u * DT * ca.cos(psi),
        py + u * DT * ca.sin(psi),
        psi + dpsi * DT,
        u,
    )

    model.x = x
    model.u = u_ctrl
    model.disc_dyn_expr = f_expl
    model.p = []

    # CPA nonlinear path constraint: g(x) = (px-tx)^2 + (py-ty)^2 - cpa_hard^2.
    # Enforced >= 0 at every stage (stay outside the CPA disc around target).
    # Uses the FUNCTION ARGUMENTS (per the signature), not module constants.
    g_cpa = (px - target_x) ** 2 + (py - target_y) ** 2 - cpa_hard ** 2
    model.con_h_expr = ca.vertcat(g_cpa)

    ocp.model = model

    nx = model.x.rows()
    nu = model.u.rows()
    nh = model.con_h_expr.rows()
    Tf = N * DT

    # ---- Solver options (copy verbatim from P1a gen_m5_subset.py) ----
    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf
    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    # F3: EXACT hessian (not GAUSS_NEWTON) — nonlinear CPA needs it.
    ocp.solver_options.hessian_approx = "EXACT"
    ocp.solver_options.integrator_type = "DISCRETE"
    ocp.solver_options.nlp_solver_type = "SQP"
    ocp.solver_options.nlp_solver_max_iter = 200
    # F4: MERIT_BACKTRACKING globalization — CPA-active start needs it.
    ocp.solver_options.globalization = "MERIT_BACKTRACKING"

    # ---- Constraints (copy verbatim from P1a, F2 = bounded uh) ----
    # Heading-rate control bound (DPSI_MAX=0.2 rad/s).
    DPSI_MAX = 0.2
    ocp.constraints.lbu = np.array([-DPSI_MAX])
    ocp.constraints.ubu = np.array([+DPSI_MAX])
    ocp.constraints.idxbu = np.array([0])

    # Heading box on psi (index 2 in x).
    ocp.constraints.lbx = np.array([psi_lb])
    ocp.constraints.ubx = np.array([psi_ub])
    ocp.constraints.idxbx = np.array([2])

    # CPA nonlinear path constraint: g >= 0 (one-sided lower bound).
    # F2: uh uses UH_INF (1e10), NOT np.inf — t_renderer rejects JSON Infinity.
    # lh0/uh0 set identically (acados make_consistent requires dimension match).
    ocp.constraints.lh = np.zeros((nh,))
    ocp.constraints.uh = np.full((nh,), UH_INF)
    ocp.constraints.lh0 = np.zeros((nh,))
    ocp.constraints.uh0 = np.full((nh,), UH_INF)

    # Initial state: origin, heading east (psi=0), surge 5 m/s.
    ocp.constraints.x0 = np.array([0.0, 0.0, 0.0, 5.0])

    # NOTE: no cost set here — each task adds its own cost_type/W/yref before
    # calling AcadosOcpSolver.generate.

    return ocp


def forward_seed(x0, dpsi_seq, N, DT):
    """Reproduce the P1a subset_runner.cpp:64-78 warm-start seed in Python.

    Given initial state x0=[px,py,psi,u], a per-step heading-rate sequence
    dpsi_seq (length N), horizon N, step DT, returns:
      - x_seed: list of length N+1 of [px,py,psi,u] states, forward-propagated
                by the discrete dynamics using dpsi_seq[k].
      - u_seed: list of length N of [dpsi].
    Surge u is held constant. F1: non-zero warm-start seed is required (zero
    seed -> ill-conditioned first QP).
    """
    x_seed = []
    u_seed = []

    px, py, psi, u = float(x0[0]), float(x0[1]), float(x0[2]), float(x0[3])
    x_seed.append([px, py, psi, u])
    for k in range(N):
        dpsi = float(dpsi_seq[k])
        u_seed.append([dpsi])
        # Discrete dynamics step (matches gen_m5_subset.py:53-58 /
        # subset_runner.cpp:71-77): psi updates with dpsi, position with old
        # psi, surge u unchanged.
        px_new = px + u * DT * math.cos(psi)
        py_new = py + u * DT * math.sin(psi)
        psi_new = psi + dpsi * DT
        # u held constant.
        px, py, psi = px_new, py_new, psi_new
        x_seed.append([px, py, psi, u])

    return x_seed, u_seed
