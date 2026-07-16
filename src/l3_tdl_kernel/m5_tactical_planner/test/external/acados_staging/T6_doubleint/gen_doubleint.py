#!/usr/bin/env python3
"""P1b-1a Task 6 -- double-integrator heading dynamics staging (Path B).

QUESTION (the spike): does the honest double-integrator yaw dynamics
(dr/dt = c(u)*delta, NO N_r*r damping -- VDM-direct read, T8 Path B finding)
map cleanly to acatos `disc_dyn_expr` and SOLVE, despite the marginal-stability
pole at z=1 (r is a pure integrator of delta) -- with the ROT box |r|<=rot_max
keeping r bounded?

state x = [px, py, psi, r] (nx=4, +ROT r).
control u_ctrl = [delta]    (nu=1, rudder; surge held constant as a baked
                             literal u_surge -- variable surge deferred to
                             P1b-1b).
c_u and u_surge are BAKED as literals (no model.p -- pure-dynamics staging
test needs no per-stage variation).

Discrete dynamics (acatos integrator_type=DISCRETE uses disc_dyn_expr directly,
no RK):
    r_new  = r  + DT*c_u*delta           # yaw-rate integrates rudder
    psi_new = psi + DT*r                 # heading from yaw rate (explicit Euler)
    px_new = px + u_surge*DT*cos(psi)    # kinematic pos (surge constant)
    py_new = py + u_surge*DT*sin(psi)

Cost: NONLINEAR_LS on (psi - psi_ref) + small delta penalty. psi_ref is a
NON-ZERO soft attractor so the SOLVED trajectory commands real rudder and
exercises the r-integrator (the marginal-stability channel); the ROT box does
the hard bounding. This is a staging cost -- not a production objective.

====================  c_u value (from T8 VDM identification)  ====================
c_u = 9.825342e-3 rad/s^2/rad  (T8's VDM-direct yaw gain k_n_rudder*u^2/izz_e,
matched to 8 sig figs; the VDM yaw channel is a pure double-integrator, no
N_r*r damping -- Path B decision). Read from
../T8_ident/nomoto_params.json key `c_u`; if the json is absent (gitignored) we
fall back to the hard-coded literal cited to T8.

====================  self-contained builder (inlined)  ========================
This script INLINES build_base_ocp_doubleint rather than importing it from
../common.py. The inlined body is identical to common.py's function; the
duplication is deliberate so the gen step is robust to the bind-mount dirent
flakiness seen for the staging subtree in the SIL container (a parent-dir
import intermittently fails to resolve). common.py still carries the canonical
copy for T7/T9 consumption.

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import json
import os

import casadi as ca
import numpy as np
from acados_template import AcadosModel, AcadosOcp, AcadosOcpSolver

SOLVER_NAME = "m5_staging_doubleint"

# F2: bounded pseudo-infinity (t_renderer rejects JSON Infinity).
UH_INF = 1e10

# ---- Constants (must match runner_doubleint.cpp EXACTLY) ----
N, DT = 10, 5.0
U_SURGE = 9.26              # T8 u_cruise (m/s), surge held constant
ROT_MAX = 0.2               # rad/s -- ROT box, bounds marginal-stability r
# PSI_REF is a NON-ZERO heading attractor so the SOLVED trajectory must command
# real rudder to turn the vessel -- this exercises the r-integrator (the
# marginal-stability channel) so the load-bearing dynamics-match check #2
# (forward-sim of the SOLVED u-sequence) genuinely validates acatos's
# disc_dyn_expr rollout over non-trivial controls. With psi_ref=0 the solver
# trivially drives delta->0 and the r-channel is barely tested.
PSI_REF = 0.1               # staging heading attractor (rad) -- non-zero to exercise r
# Target placed OFF the vessel's direct eastbound path (due north, far enough
# that the gentle rudder seed clears the CPA disc at every stage). T6 is a
# DYNAMICS + marginal-stability SQP test, NOT an avoidance-feasibility test;
# the CPA constraint stays in the OCP for compositional consistency with T9,
# but must not make the first QP infeasible. With the honest double-integrator
# yaw gain c_u~9.8e-3 (T8), the vessel cannot turn fast enough to clear a
# head-on (200,0) disc -- a real physical consequence (reported as a finding),
# so the target is placed off-path to isolate the dynamics question.
TARGET_X, TARGET_Y = 0.0, 300.0
CPA_HARD = 100.0


def build_base_ocp_doubleint(N=10, DT=5.0, c_u=0.01, u_surge=9.26,
                             target_x=200.0, target_y=0.0, cpa_hard=100.0,
                             psi_lb=-1.2, psi_ub=1.2, rot_max=0.2):
    """INLINED copy of common.build_base_ocp_doubleint (see module docstring).

    Builds the Path B double-integrator OCP: dynamics + CPA-h + psi/r boxes +
    EXACT hessian + MERIT_BACKTRACKING. Does NOT set any cost.
    """
    ocp = AcadosOcp()

    model = AcadosModel()
    model.name = "m5_staging_doubleint_base"

    px = ca.SX.sym("px")
    py = ca.SX.sym("py")
    psi = ca.SX.sym("psi")
    r = ca.SX.sym("r")            # yaw rate (rad/s) -- double-integrator
    x = ca.vertcat(px, py, psi, r)

    delta = ca.SX.sym("delta")    # control: rudder angle (rad)
    u_ctrl = ca.vertcat(delta)

    # Discrete explicit dynamics -- Path B double integrator. c_u and u_surge
    # baked as literals (no param vector). Explicit Euler: psi uses r[k].
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

    g_cpa = (px - target_x) ** 2 + (py - target_y) ** 2 - cpa_hard ** 2
    model.con_h_expr = ca.vertcat(g_cpa)

    ocp.model = model

    nh = model.con_h_expr.rows()
    Tf = N * DT

    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf
    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    ocp.solver_options.hessian_approx = "EXACT"          # F3
    ocp.solver_options.integrator_type = "DISCRETE"
    ocp.solver_options.nlp_solver_type = "SQP"
    ocp.solver_options.nlp_solver_max_iter = 200
    ocp.solver_options.globalization = "MERIT_BACKTRACKING"   # F4

    DDELTA_MAX = 0.2
    ocp.constraints.lbu = np.array([-DDELTA_MAX])
    ocp.constraints.ubu = np.array([+DDELTA_MAX])
    ocp.constraints.idxbu = np.array([0])

    # State box on BOTH psi (idx 2) AND r (idx 3). ROT box on r is CRITICAL:
    # the double-integrator pole is at z=1 (marginally stable); the box keeps
    # r bounded during SQP.
    ocp.constraints.lbx = np.array([psi_lb, -rot_max])
    ocp.constraints.ubx = np.array([psi_ub, +rot_max])
    ocp.constraints.idxbx = np.array([2, 3])

    ocp.constraints.lh = np.zeros((nh,))
    ocp.constraints.uh = np.full((nh,), UH_INF)
    ocp.constraints.lh0 = np.zeros((nh,))
    ocp.constraints.uh0 = np.full((nh,), UH_INF)

    ocp.constraints.x0 = np.array([0.0, 0.0, 0.0, 0.0])

    return ocp


def _load_c_u():
    """Read c_u from T8's nomoto_params.json; fall back to the T8 literal."""
    json_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..", "T8_ident", "nomoto_params.json")
    try:
        with open(json_path) as f:
            val = float(json.load(f)["c_u"])
        return val, "T8_ident/nomoto_params.json"
    except (OSError, KeyError, ValueError):
        # Gitignored json absent (fresh clone) -- T8 VDM-direct yaw gain
        # (k_n_rudder*u^2/izz_e), matches json to 8 sig figs.
        return 9.825342e-3, "hardcoded (T8_ident/nomoto_params.json gitignored)"


def main():
    c_u, c_u_src = _load_c_u()

    ocp = build_base_ocp_doubleint(
        N=N, DT=DT, c_u=c_u, u_surge=U_SURGE,
        target_x=TARGET_X, target_y=TARGET_Y, cpa_hard=CPA_HARD,
        psi_lb=-1.2, psi_ub=1.2, rot_max=ROT_MAX,
    )
    model = ocp.model
    model.name = SOLVER_NAME

    # ---- Cost: NONLINEAR_LS y=[psi, delta], yref=[psi_ref, 0]. ----
    psi = model.x[2]
    delta = model.u[0]
    W_PSI = 1.0           # heading attractor weight
    # W_DELTA sized to keep delta INTERIOR (away from the +-0.2 box). With a
    # too-small weight the sluggish r-integrator drives delta to the box
    # (bang-bang), which slows SQP convergence near the marginally-stable r
    # pole. An interior delta both exercises the r-channel and lets SQP
    # converge within max_iter=200.
    W_DELTA = 1.0         # rudder regularization (sized to avoid box saturation)

    ocp.cost.cost_type = "NONLINEAR_LS"
    model.cost_y_expr = ca.vertcat(psi, delta)
    ocp.cost.W = np.diag([W_PSI, W_DELTA])
    ocp.cost.yref = np.array([PSI_REF, 0.0])

    ocp.cost.cost_type_0 = "NONLINEAR_LS"
    model.cost_y_expr_0 = ca.vertcat(psi, delta)
    ocp.cost.W_0 = np.diag([W_PSI, W_DELTA])
    ocp.cost.yref_0 = np.array([PSI_REF, 0.0])

    ocp.cost.cost_type_e = "NONLINEAR_LS"
    model.cost_y_expr_e = ca.vertcat(psi)
    ocp.cost.W_e = np.array([[W_PSI]])
    ocp.cost.yref_e = np.array([PSI_REF])

    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    nx = model.x.rows()
    nu = model.u.rows()
    nh = model.con_h_expr.rows()
    print(f"DOUBLEINT GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"DYNAMICS (disc_dyn_expr, Path B double-integrator):")
    print(f"  r_new  = r  + DT*c_u*delta        (c_u={c_u:.9e}, src={c_u_src})")
    print(f"  psi_new = psi + DT*r               (explicit Euler, r[k])")
    print(f"  px_new = px + u_surge*DT*cos(psi)  (u_surge={U_SURGE})")
    print(f"  py_new = py + u_surge*DT*sin(psi)")
    print(f"DIMS: nx={nx} (px,py,psi,r)  nu={nu} (delta)  nh={nh} (g_cpa)")
    print(f"BOXES: psi in [-1.2, 1.2] (idx 2); ROT |r|<={ROT_MAX} (idx 3); "
          f"|delta|<=0.2 (idxbu 0)")
    print(f"COST: NONLINEAR_LS y=[psi,delta] yref=[{PSI_REF},0] "
          f"W=diag({W_PSI},{W_DELTA}); term y=[psi] W={W_PSI}")
    print(f"SOLVER: SQP FULL_CONDENSING_HPIPM EXACT MERIT_BACKTRACKING "
          f"max_iter=200 DISCRETE")


if __name__ == "__main__":
    main()
