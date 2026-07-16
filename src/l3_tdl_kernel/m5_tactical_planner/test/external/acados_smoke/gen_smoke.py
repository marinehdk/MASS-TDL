#!/usr/bin/env python3
"""P1a spike — acados toolchain smoke test: mass-spring toy OCP code-gen.

Generates a self-contained C OCP solver for a trivial mass-spring system
(2 states [position, velocity], 1 control [force], linear dynamics,
quadratic cost). This is NOT an M5 formulation — it exists only to prove the
acados toolchain (Python Jinja2 code-gen -> C compile -> HPIPM RTI solve)
works in the sil_nodes container with an ABI-clean link.

Reference: acados v0.4.4 examples/acados_python/getting_started/minimal_example_ocp.py
API: AcadosOcpSolver.generate(ocp, json_file=...) writes C sources + Makefile
     into ocp.code_export_directory; `make ocp_shared_lib` then builds
     libacados_ocp_solver_<name>.so which the C runner links.
"""
import os
import numpy as np
import casadi as ca
from acados_template import AcadosOcp, AcadosModel, AcadosOcpSolver

SOLVER_NAME = "mass_spring"


def export_mass_spring_model() -> AcadosModel:
    """Linear mass-spring: x=[p, v], u=[F].  dv/dt = -k*p + F (m=1, k=1)."""
    model = AcadosModel()
    model.name = SOLVER_NAME

    p = ca.SX.sym("p")
    v = ca.SX.sym("v")
    x = ca.vertcat(p, v)
    F = ca.SX.sym("F")
    u = ca.vertcat(F)

    k = 1.0  # stiffness (m = 1)
    f_expl = ca.vertcat(v, -k * p + F)
    model.x = x
    model.u = u
    model.f_expl_expr = f_expl
    model.p = []
    return model


def main():
    ocp = AcadosOcp()
    ocp.model = export_mass_spring_model()

    nx = ocp.model.x.rows()
    nu = ocp.model.u.rows()
    N = 20
    Tf = 1.0

    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf

    # Quadratic stage + terminal cost tracking the origin.
    Q = 2.0 * np.diag([1e2, 1e1])
    R = 2.0 * np.diag([1e-1])
    ocp.cost.cost_type = "NONLINEAR_LS"
    ocp.model.cost_y_expr = ca.vertcat(ocp.model.x, ocp.model.u)
    ocp.cost.yref = np.zeros((nx + nu,))
    ocp.cost.W = np.block([[Q, np.zeros((nx, nu))],
                           [np.zeros((nu, nx)), R]])
    ocp.cost.cost_type_e = "NONLINEAR_LS"
    ocp.model.cost_y_expr_e = ocp.model.x
    ocp.cost.yref_e = np.zeros((nx,))
    ocp.cost.W_e = Q

    # Force bound.
    Fmax = 80.0
    ocp.constraints.lbu = np.array([-Fmax])
    ocp.constraints.ubu = np.array([+Fmax])
    ocp.constraints.idxbu = np.array([0])

    # Nonzero initial state so the solver has work to do.
    ocp.constraints.x0 = np.array([1.0, 0.0])

    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    ocp.solver_options.hessian_approx = "GAUSS_NEWTON"
    ocp.solver_options.integrator_type = "ERK"
    # RTI proves the real-time-iteration path (the production target, DP-05).
    ocp.solver_options.nlp_solver_type = "SQP_RTI"

    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir

    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    # generate() writes C sources + Makefile; build is done by run_smoke.sh
    # so we can add a -Werror new-ABI C-compile step for the runner.
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"SMOKE GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")


if __name__ == "__main__":
    main()
