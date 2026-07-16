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


def build_base_ocp_doubleint(N=10, DT=5.0, c_u=0.01, u_surge=9.26,
                             target_x=200.0, target_y=0.0, cpa_hard=100.0,
                             psi_lb=-1.2, psi_ub=1.2,
                             rot_max=0.2) -> AcadosOcp:
    """Build and return a configured AcadosOcp carrying the Path B double-
    integrator heading dynamics (P1b-1a Task 6).

    Honest VDM-direct yaw channel (T8 finding): dr/dt = c(u)*delta, NO N_r*r
    damping term. state x=[px,py,psi,r] (nx=4, +ROT r), control u_ctrl=[delta]
    (nu=1, rudder; surge held constant). Variable surge optimization deferred
    to P1b-1b.

    Discrete dynamics (acatos integrator_type=DISCRETE uses disc_dyn_expr
    directly, no RK):
        r_new  = r  + DT*c_u*delta           # yaw-rate integrates rudder
        psi_new = psi + DT*r                 # heading from yaw rate (explicit Euler)
        px_new = px + u_surge*DT*cos(psi)    # kinematic pos (surge constant)
        py_new = py + u_surge*DT*sin(psi)

    c_u and u_surge are BAKED as literals into the dynamics expression (T6
    recommendation: param-free model -- no per-stage variation needed for a
    pure-dynamics staging test). model.p = [].

    Constraints (same F1-F5 idioms as build_base_ocp):
      - Rudder box: |delta| <= DPSI_MAX-equivalent (DDELTA_MAX=0.2 rad here; the
        brief's rot_max bounds r, the rudder box bounds delta).
      - Heading box on psi (index 2): [psi_lb, psi_ub].
      - ROT box on r (index 3): |r| <= rot_max. CRITICAL -- the double
        integrator's pole is at z=1 (marginally stable); this box is what keeps
        r bounded during SQP.
      - CPA nonlinear path constraint g=(px-tx)^2+(py-ty)^2-cpa_hard^2 >= 0,
        one-sided (lh=0, uh=UH_INF per F2).

    Solver opts (copy verbatim from build_base_ocp): FULL_CONDENSING_HPIPM,
    EXACT hessian (F3), DISCRETE integrator, SQP, max_iter 200,
    MERIT_BACKTRACKING (F4). Does NOT set any cost -- gen_doubleint.py adds its
    own before AcadosOcpSolver.generate.
    """
    ocp = AcadosOcp()

    # ---- Model (AcadosModel) ----
    model = AcadosModel()
    model.name = "m5_staging_doubleint_base"

    px = ca.SX.sym("px")
    py = ca.SX.sym("py")
    psi = ca.SX.sym("psi")
    r = ca.SX.sym("r")            # yaw rate (rad/s) -- Path B double-integrator
    x = ca.vertcat(px, py, psi, r)

    delta = ca.SX.sym("delta")    # control: rudder angle (rad)
    u_ctrl = ca.vertcat(delta)

    # Discrete explicit dynamics -- Path B double integrator. c_u and u_surge
    # are baked as the function-arg literals (no param vector). Explicit Euler
    # on psi uses r[k] (NOT r[k+1]).
    f_expl = ca.vertcat(
        px + u_surge * DT * ca.cos(psi),
        py + u_surge * DT * ca.sin(psi),
        psi + DT * r,
        r + DT * c_u * delta,
    )

    model.x = x
    model.u = u_ctrl
    model.disc_dyn_expr = f_expl
    model.p = []                  # param-free (c_u/u_surge baked)

    # CPA nonlinear path constraint: g(x) = (px-tx)^2 + (py-ty)^2 - cpa_hard^2.
    g_cpa = (px - target_x) ** 2 + (py - target_y) ** 2 - cpa_hard ** 2
    model.con_h_expr = ca.vertcat(g_cpa)

    ocp.model = model

    nx = model.x.rows()
    nu = model.u.rows()
    nh = model.con_h_expr.rows()
    Tf = N * DT

    # ---- Solver options (copy verbatim from build_base_ocp / P1a) ----
    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf
    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    ocp.solver_options.hessian_approx = "EXACT"          # F3
    ocp.solver_options.integrator_type = "DISCRETE"
    ocp.solver_options.nlp_solver_type = "SQP"
    ocp.solver_options.nlp_solver_max_iter = 200
    ocp.solver_options.globalization = "MERIT_BACKTRACKING"   # F4

    # ---- Constraints ----
    # Rudder-angle control bound (rad). The double-integrator maps delta -> r
    # via c_u; with DT=5, c_u~9.8e-3, a delta=0.2 rad yields
    # dr = 5*9.8e-3*0.2 ~= 9.8e-3 rad/s per stage -- well inside rot_max=0.2.
    DDELTA_MAX = 0.2
    ocp.constraints.lbu = np.array([-DDELTA_MAX])
    ocp.constraints.ubu = np.array([+DDELTA_MAX])
    ocp.constraints.idxbu = np.array([0])

    # State box on BOTH psi (index 2) AND r (index 3). The ROT box on r is
    # CRITICAL: the double-integrator pole is at z=1 (marginally stable); the
    # box is what keeps r bounded. lbx/ubx are ordered by the indices in idxbx.
    ocp.constraints.lbx = np.array([psi_lb, -rot_max])
    ocp.constraints.ubx = np.array([psi_ub, +rot_max])
    ocp.constraints.idxbx = np.array([2, 3])

    # CPA nonlinear path constraint: g >= 0 (one-sided). F2 bounded uh.
    ocp.constraints.lh = np.zeros((nh,))
    ocp.constraints.uh = np.full((nh,), UH_INF)
    ocp.constraints.lh0 = np.zeros((nh,))
    ocp.constraints.uh0 = np.full((nh,), UH_INF)

    # Initial state: origin, heading north (psi=0), zero yaw rate (r=0).
    ocp.constraints.x0 = np.array([0.0, 0.0, 0.0, 0.0])

    # NOTE: no cost set here -- gen_doubleint.py adds its own cost before
    # calling AcadosOcpSolver.generate.

    return ocp


def forward_seed_doubleint(x0, delta_seq, N, DT, c_u, u_surge):
    """Reproduce the double-integrator warm-start seed in Python.

    Given initial state x0=[px,py,psi,r], a per-step rudder sequence delta_seq
    (length N), horizon N, step DT, yaw gain c_u and constant surge u_surge,
    returns:
      - x_seed: list of length N+1 of [px,py,psi,r] states, forward-propagated
                by the Path B double-integrator discrete dynamics using
                delta_seq[k].
      - u_seed: list of length N of [delta].

    Discrete step (matches build_base_ocp_doubleint disc_dyn_expr, explicit
    Euler -- psi uses r[k], NOT r[k+1]):
        r_new  = r  + DT*c_u*delta[k]
        psi_new = psi + DT*r           # r is the PRE-update value
        px_new = px + u_surge*DT*cos(psi)
        py_new = py + u_surge*DT*sin(psi)

    F1: non-zero warm-start seed is required (zero seed -> ill-conditioned
    first QP).
    """
    x_seed = []
    u_seed = []

    px = float(x0[0]); py = float(x0[1]); psi = float(x0[2]); r = float(x0[3])
    x_seed.append([px, py, psi, r])
    for k in range(N):
        delta = float(delta_seq[k])
        u_seed.append([delta])
        # Explicit Euler: psi advances by DT*r (pre-update r), r advances by
        # DT*c_u*delta. Position uses pre-update psi (matches disc_dyn_expr).
        px_new = px + u_surge * DT * math.cos(psi)
        py_new = py + u_surge * DT * math.sin(psi)
        psi_new = psi + DT * r
        r_new = r + DT * c_u * delta
        px, py, psi, r = px_new, py_new, psi_new, r_new
        x_seed.append([px, py, psi, r])

    return x_seed, u_seed
