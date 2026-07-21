#!/usr/bin/env python3
"""P1a spike — M5 formulation subset re-staging as an acados staged-OCP.

Proves the existing NLP's minimal subset maps to acados primitives:
  - dynamics  : discrete explicit, from mid_mpc_nlp_formulation.cpp:360-361
                  px[k+1] = px[k] + u[k]*dt*cos(psi[k])
                  py[k+1] = py[k] + u[k]*dt*sin(psi[k])
                  psi[k+1]= psi[k] + dpsi[k]*dt      (heading-rate control)
                  u[k+1]  = u[k]                      (constant surge)
  - CPA       : nonlinear path constraint, from constraint_compiler.cpp:353
                  g = (px-tx)^2 + (py-ty)^2 - cpa_hard^2  >= 0
  - heading   : box bound on psi  lbx <= psi <= ubx
  - cost      : NONLINEAR_LS stage cost (psi - psi_ref)^2 + dpsi^2

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import os
import numpy as np
import casadi as ca
from acados_template import AcadosOcp, AcadosModel, AcadosOcpSolver

SOLVER_NAME = "m5_subset"

# Parameters chosen so the CPA disc intersects the straight-line trajectory
# within the horizon (N=10 steps * DT=5s * u=5 m/s = 250 m reach). Target at
# (200, 0) with CPA_HARD=100 means the straight path (px growing 0->250, py=0)
# enters the CPA disc at px=100 and must divert. This forces a nonzero
# avoidance maneuver, which the assertions in subset_runner.cpp check.
N = 10
DT = 5.0
TARGET_X = 200.0
TARGET_Y = 0.0
CPA_HARD = 100.0          # CPA safe radius [m]
PSI_REF = 0.0             # desired heading [rad] (drives back to route)
PSI_LB = -1.2             # heading box [rad]  (~69 deg, allows diversion)
PSI_UB = 1.2


def export_m5_subset_model() -> AcadosModel:
    model = AcadosModel()
    model.name = SOLVER_NAME

    px = ca.SX.sym("px")
    py = ca.SX.sym("py")
    psi = ca.SX.sym("psi")
    u = ca.SX.sym("u")          # surge speed (m/s), held constant
    x = ca.vertcat(px, py, psi, u)

    dpsi = ca.SX.sym("dpsi")    # control: heading rate (rad/s)
    ctrl = ca.vertcat(dpsi)

    # Discrete explicit dynamics (formulation.cpp:360-361 + heading-rate control).
    f_expl = ca.vertcat(
        px + u * DT * ca.cos(psi),
        py + u * DT * ca.sin(psi),
        psi + dpsi * DT,
        u,
    )

    model.x = x
    model.u = ctrl
    # For integrator_type=DISCRETE acados uses model.disc_dyn_expr directly
    # (no RK integration). This carries the exact formulation.cpp:360-361 step.
    model.disc_dyn_expr = f_expl
    model.p = []

    # CPA nonlinear path constraint: g(x) = (px-tx)^2 + (py-ty)^2 - cpa_hard^2.
    # Enforced >= 0 at every stage (stay outside the CPA disc around target).
    g_cpa = (px - TARGET_X) ** 2 + (py - TARGET_Y) ** 2 - CPA_HARD ** 2
    model.con_h_expr = ca.vertcat(g_cpa)
    return model


def main():
    ocp = AcadosOcp()
    ocp.model = export_m5_subset_model()

    nx = ocp.model.x.rows()
    nu = ocp.model.u.rows()
    nh = ocp.model.con_h_expr.rows()
    Tf = N * DT

    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf

    # Stage cost: track psi_ref + penalize heading-rate actuation.
    ocp.cost.cost_type = "NONLINEAR_LS"
    psi_err = ocp.model.x[2] - PSI_REF
    ocp.model.cost_y_expr = ca.vertcat(psi_err, ocp.model.u[0])
    ocp.cost.yref = np.zeros((1 + nu,))
    ocp.cost.W = np.diag([1.0e2, 1.0e0])
    # Terminal cost: psi tracking only.
    ocp.cost.cost_type_e = "NONLINEAR_LS"
    ocp.model.cost_y_expr_e = ocp.model.x[2] - PSI_REF
    ocp.cost.yref_e = np.zeros((1,))
    ocp.cost.W_e = np.array([[1.0e2]])

    # Heading-rate control bounds.
    DPSI_MAX = 0.2  # rad/s
    ocp.constraints.lbu = np.array([-DPSI_MAX])
    ocp.constraints.ubu = np.array([+DPSI_MAX])
    ocp.constraints.idxbu = np.array([0])

    # Heading box: lbx <= psi <= ubx at every stage (idxbx index 2 in x).
    # The terminal stage bounds psi via the terminal cost, so no idxbx_e here.
    ocp.constraints.lbx = np.array([PSI_LB])
    ocp.constraints.ubx = np.array([PSI_UB])
    ocp.constraints.idxbx = np.array([2])

    # CPA nonlinear path constraint: g >= 0 (one-sided lower bound). acados
    # make_consistent requires uh to be set (dimension match), and np.inf
    # serializes to JSON "Infinity" which the Rust t_renderer (strict serde)
    # rejects — use a large finite pseudo-infinity (1e10) instead. A P1a-mapped
    # friction noted for P1b: one-sided h constraints need a finite upper bound.
    UH_INF = 1e10
    ocp.constraints.lh = np.zeros((nh,))
    ocp.constraints.uh = np.full((nh,), UH_INF)
    ocp.constraints.lh0 = np.zeros((nh,))
    ocp.constraints.uh0 = np.full((nh,), UH_INF)

    # Initial state: at origin, heading east (psi=0), surge 5 m/s — heading
    # straight at the target at (500, 0), so the solver MUST divert.
    ocp.constraints.x0 = np.array([0.0, 0.0, 0.0, 5.0])

    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    # EXACT hessian (not GAUSS_NEWTON): with the nonlinear CPA constraint the
    # GN approximation lets the QP drift into CPA violation during refinement;
    # the exact Hessian keeps the trajectory CPA-feasible across SQP steps.
    ocp.solver_options.hessian_approx = "EXACT"
    ocp.solver_options.integrator_type = "DISCRETE"
    # SQP (full) for the mapping check — plan asks for a converged OCP solve.
    ocp.solver_options.nlp_solver_type = "SQP"
    ocp.solver_options.nlp_solver_max_iter = 200
    # Globalization is REQUIRED here: the straight-line seed violates the CPA
    # constraint, so the first QP is infeasible without merit-function
    # backtracking (HPIPM returns QP error 3 / acados status 4). A P1a finding
    # for P1b: the production OCP will need globalization for CPA-active starts.
    ocp.solver_options.globalization = "MERIT_BACKTRACKING"

    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"SUBSET GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")


if __name__ == "__main__":
    main()
