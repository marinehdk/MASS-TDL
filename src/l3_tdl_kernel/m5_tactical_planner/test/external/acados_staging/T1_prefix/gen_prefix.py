#!/usr/bin/env python3
"""P1b-0 T1 -- prefix equality staging (acados staged-OCP spike).

QUESTION (the spike): can "first K stages locked to a fixed heading prefix,
stages k>=K free" map cleanly to acados per MPC cycle?

VERDICT (resolved here, proven by runner_prefix.cpp): YES, via a PARAMETRIC
ACTIVATION factor on a second h-row. See STRATEGY note below.

acados 0.4.4 staging reality (the subtlety this spike resolves):
  - ocp.constraints.lh / uh  is a SINGLE array indexed by h-ROW (not stage);
    it is applied UNIFORMLY to every stage 0..N. You CANNOT make lb/uh
    stage-dependent.
  - ocp.constraints.lbx / ubx (and Zl/Zu, idxsh) are likewise stage-uniform.
  - The ONLY per-stage lever acados exposes is the parameter vector `p`,
    set independently per stage (Python: solver.set(k,"p",...);
    C: ocp_nlp_in_set(cfg,dims,in,k,"p",vals)).

Therefore the stage-switch (equality at k<K, freedom at k>=K) MUST live INSIDE
the constraint expression, driven by a per-stage parameter. Two naive options
are ruled out by the uniform-bounds reality:
  (LOSER 1) "uniform equality lh[1]=uh[1]=0 + per-stage ppsi": pins psi==ppsi
            at EVERY stage including k>=K -> suffix is NOT free. Ruled out:
            uniform bounds cannot disable at k>=K.
  (LOSER 2) slack-softening (idxsh) the prefix row: idxsh is ALSO a single
            uniform array -> softens at every stage, so k<K equality is no
            longer hard. Ruled out: same uniform-bounds blocker.

WINNING STRATEGY (a): parametric activation.
  Add a second h-row  h_prefix = pact * (psi - ppsi)   (nh becomes 2).
  Uniform bounds on that row:  lh[1] = 0,  uh[1] = 0   (i.e. equality 0<=h<=0).
  Per-stage parameters p = [ppsi, pact]:
     k <  K : pact = 1.0, ppsi = prefix_psi[k]  -> h = psi - prefix  == 0 binds.
     k >= K : pact = 0.0, ppsi = <don't care>   -> h = 0           == 0 holds
                                                       for ANY psi -> suffix FREE.
  The uniform equality bound is satisfied at every stage, but at k>=K the
  expression is identically 0 (zero value AND zero Jacobian/Hessian), so it
  imposes nothing on psi. This is the clean staging and it uses the one
  per-stage lever acados actually provides.

The CPA row (index 0, g_cpa = (px-tx)^2+(py-ty)^2 - cpa_hard^2) keeps its
one-sided bound lh[0]=0, uh[0]=UH_INF at all stages.

Cost: NONLINEAR_LS tracking psi_ref=0.3 (suffix target) + heading-rate penalty,
mirroring P1a. The prefix overrides cost at k<K (hard equality > soft cost);
the cost lets the free suffix choose a sensible CPA-feasible heading.

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import os
import sys

import casadi as ca
import numpy as np
from acados_template import AcadosOcpSolver

# common.py lives one dir up (acados_staging/); add parent to sys.path.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from common import build_base_ocp, UH_INF  # noqa: E402

SOLVER_NAME = "m5_staging_prefix"
N, DT, K = 10, 5.0, 3
PSI_REF = 0.3
PREFIX_PSI = [0.1, 0.2, 0.3]  # committed prefix heading for stages k=0,1,2


def main():
    ocp = build_base_ocp(N=N, DT=DT)  # dynamics + CPA-h (nh=1) + box + EXACT +
    #                                    MERIT_BACKTRACKING + warm-start opts; NO cost.
    model = ocp.model
    # Override the base model name so generated artifacts (header
    # acados_solver_<name>.h, lib libacados_ocp_solver_<name>.so) match this
    # task's solver name. build_base_ocp set model.name="m5_staging_base".
    model.name = SOLVER_NAME

    # ---- Parameters: p = [ppsi, pact] (np=2). The ONLY per-stage lever acados
    #      gives, so the staging switch is encoded here. ----
    ppsi = ca.SX.sym("ppsi")   # prefix heading target for this stage
    pact = ca.SX.sym("pact")   # activation: 1.0 = enforce equality, 0.0 = free
    model.p = ca.vertcat(ppsi, pact)
    ocp.parameter_values = np.array([0.0, 1.0])  # default; overridden per stage
    #                                                 by the runner via set(k,"p").

    # ---- Second h-row: parametric prefix equality. ----
    psi = model.x[2]
    h_prefix = pact * (psi - ppsi)
    model.con_h_expr = ca.vertcat(model.con_h_expr, h_prefix)  # nh 1 -> 2

    # ---- Reset h bounds to length nh=2 (acados make_consistent requires the
    #      dimension match once con_h_expr grew). Stage-UNIFORM (the point):
    #      row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0 <= h <= 0. The
    #      stage-dependence is carried by `p`, not by these bounds. ----
    nh = model.con_h_expr.rows()
    ocp.constraints.lh = np.array([0.0, 0.0])
    ocp.constraints.uh = np.array([UH_INF, 0.0])  # F2: bounded uh, not np.inf
    ocp.constraints.lh0 = np.array([0.0, 0.0])
    ocp.constraints.uh0 = np.array([UH_INF, 0.0])

    # ---- Stage cost (NONLINEAR_LS): track psi_ref + penalize heading-rate. ----
    dpsi = model.u[0]
    ocp.cost.cost_type = "NONLINEAR_LS"
    model.cost_y_expr = ca.vertcat(psi - PSI_REF, dpsi)
    ocp.cost.yref = np.zeros((2,))
    ocp.cost.W = np.diag([1.0e2, 1.0e0])
    # Terminal cost: psi tracking only.
    ocp.cost.cost_type_e = "NONLINEAR_LS"
    model.cost_y_expr_e = psi - PSI_REF
    ocp.cost.yref_e = np.zeros((1,))
    ocp.cost.W_e = np.array([[1.0e2]])

    # ---- Export C code. ----
    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"PREFIX GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"STRATEGY: parametric activation h_prefix = pact*(psi-ppsi), "
          f"nh={nh}, uniform lh/uh=[0,0]/[UH_INF,0], per-stage p=[ppsi,pact]")
    print(f"PREFIX_PSI(k<3)={PREFIX_PSI}  PSI_REF(suffix)={PSI_REF}")


if __name__ == "__main__":
    main()
