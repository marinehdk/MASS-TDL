#!/usr/bin/env python3
"""P1b-0 T3 -- global sigma slack mapping (exact-penalty verification) spike.

QUESTION (the spike): the production NLP softens the hard CPA constraint with a
single global slack scalar sigma (shared across all CPA rows) so the OCP stays
feasible when a target is genuinely unavoidable. Which acatos mapping preserves
the EXACT-PENALTY semantics -- slack == 0 when CPA is feasible (the penalty must
NOT distort the feasible solution), slack > 0 when CPA is infeasible (relax
exactly as much as needed)?

VERDICT (resolved by runner_slack.cpp on two scenarios): see task-3-report.md.
Mapping (b) -- per-stage idxsh/Zl/zl -- is the acatos-native form evaluated
here. acatos does NOT literally offer "one global scalar shared across all rows";
it gives ONE slack PER softened row PER stage. With idxsh=[0] (soften the single
CPA row at every stage), acatos adds a per-stage slack xi_k for the CPA row.
That is a fidelity gap vs the production single-scalar sigma (acknowledged:
per-target per-step xi is P3 TBD-6) -- BUT the SEMANTIC property P1b-1 needs is
exact-penalty, which this spike verifies on the per-stage form.

====================  acatos soft-constraint API (used here)  ====================
From /opt/acados/examples/acados_python/tests/soft_constraint_test.py:
  - ocp.constraints.idxsh = np.array([...]) -- 0-based indices WITHIN the nh
    h-rows of the rows to soften. acatos adds a per-stage slack for each
    softened row (stage-uniform: the same h-row index is softened at every
    stage). For T3 CPA exists at every stage, so softening row 0 everywhere is
    exactly what we want.
  - ocp.cost.Zl = np.array([...]) -- quadratic weight on lower-slack (per
    softened row). Regularizer that keeps the QP well-conditioned.
  - ocp.cost.zl = np.array([...]) -- LINEAR weight on lower-slack. THIS IS THE
    EXACT-PENALTY LEVER: with zl larger than the constraint's Lagrange
    multiplier, the slack is driven to EXACTLY 0 when feasible (L1 exact-penalty
    behavior). Bumped from 1e3 -> higher if the feasible scenario keeps a
    non-zero slack (documented in the report).
  - ocp.cost.Zu / ocp.cost.zu -- upper-slack weights. CPA is one-sided (>= 0,
    uh=UH_INF so no upper bound is ever active); set to 0 to NOT soften the
    upper side.
  - The slack is read back per stage in C via
    ocp_nlp_out_get(cfg, dims, out, stage, "sl", sl_vec) where sl_vec has length
    ns = len(idxsh) = 1 for T3. sl[0] at stage k is xi_k (the CPA relaxation).

====================  Two scenarios (exact-penalty needs BOTH)  ====================
The KEY property: feasible => slack ~= 0 (softening does NOT distort the
feasible solution); infeasible => slack > 0 (relaxes exactly as much as needed).
Both must hold for mapping (b) to be recommended.

  Scenario 1 (FEASIBLE): target (500, 0), cpa_hard=100, box psi in [-1.2, 1.2].
    Vessel starts at origin heading east at 5 m/s, horizon 50s (N=10, DT=5) ->
    travels ~250m east, never within 100m of (500,0). CPA is satisfiable along
    every reachable trajectory -> the softening MUST NOT engage. Assert
    max_k xi_k < tol (1e-4). This is exact-penalty: feasible => slack = 0.

  Scenario 2 (INFEASIBLE): target (150, 0), cpa_hard=100, box psi in [-0.05,
    0.05]. Vessel starts OUTSIDE the disc (dist 150 > 100) but the tight box
    pins the heading near east -> the straight-ish path is forced THROUGH the
    disc (exclusion zone x in [50,250] on y=0). There is no feasible trajectory
    within the box -> the slack MUST relax. Assert some stage has xi_k > tol
    (1e-3) AND the relaxed CPA g_cpa + xi_k >= -tol at that stage.

The two scenarios share the SAME dynamics, SAME cost (NONLINEAR_LS tracking
psi_ref + heading-rate), SAME Zl/zl; ONLY the target position and heading box
differ. That isolates the exact-penalty test to the CPA feasibility change.

====================  Why two generated solvers  ====================
acatos bakes the heading box (lbx/ubx, stage-uniform) and the CPA target/radius
(into con_h_expr) into the generated solver. They are NOT runtime-settable per
solve. So scenario 1 and scenario 2 each need their OWN generated solver:
  m5_staging_slack_feas    (target 500,0; box +-1.2)
  m5_staging_slack_infeas  (target 150,0; box +-0.05)
runner_slack.cpp links BOTH libs and runs the two scenarios sequentially.

====================  Mapping (a) -- sigma as a dummy control (RULED OUT)  ====================
Mapping (a) would add a dummy 1-dim control sigma to model.u (nu=2: [dpsi, sigma])
with lbu/ubu pinning sigma>=0, and rewrite the CPA h to g_cpa + sigma_param. This
is hacky (sigma is not a real control -- the dynamics don't use it; acatos would
allocate a full control Hessian block for it) and it duplicates what idxsh/Zl/zl
gives natively. Per the brief: TIME-BOX (a). This file implements mapping (b)
only; mapping (a) is documented as ruled out in task-3-report.md.

====================  Mapping (c) -- outer sigma loop -- SKIP  ====================
Per spec: not recommended (loses MPC real-time). Not implemented. Noted in the
report.

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import os
import sys

import casadi as ca
import numpy as np
from acados_template import AcadosOcpSolver

# common.py lives one dir up (acatos_staging/); add parent to sys.path.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from common import build_base_ocp, UH_INF  # noqa: E402

N = 10
DT = 5.0

# ---- Slack penalty weights (the exact-penalty lever). ----
# Zl = small quadratic regularizer (keeps the QP well-conditioned).
# zl = LARGE linear weight -> L1 exact-penalty (slack = 0 when CPA feasible,
#      provided zl exceeds the CPA constraint's Lagrange multiplier).
# Zu/zu = 0 (CPA is one-sided >= 0; the upper side uh=UH_INF never binds, so we
#         do not soften the upper side).
#
# Env-overridable for the zl sweep (brief: debug exact-penalty by tuning zl).
# Defaults: Zl=1e2, zl=1e3. Override via T3_ZL / T3_ZL_LIN env vars. The
# production recommendation (the value that passes BOTH scenarios) is recorded
# in task-3-report.md.
_ZL_DEFAULT = 1.0e2
_ZL_LIN_DEFAULT = 1.0e3
ZL = np.array([float(os.environ.get("T3_ZL", _ZL_DEFAULT))])
ZL_LIN = np.array([float(os.environ.get("T3_ZL_LIN", _ZL_LIN_DEFAULT))])
# Upper-slack weights. CPA is one-sided (>= 0; the upper bound uh=UH_INF never
# binds), so conceptually the upper slack is unused. acatos allocates BOTH sl
# and su for every softened row, so Zu is set to a small positive value
# (matching Zl) to pin su at 0 via a finite quadratic cost (the upper bound
# is never active, so su stays 0 and this does NOT distort the lower-side
# exact-penalty). zu stays 0 (no linear pull on the upper side).
#
# NOTE: an earlier hypothesis that Zu=0 caused the infeasible-scenario QP
# failure (HPIPM error 3) was DISPROVED -- Zu=Zl does not fix it; the failure
# is robust to Zu. See task-3-report.md BLOCKER analysis.
ZU = np.array([float(os.environ.get("T3_ZU", _ZL_DEFAULT))])  # small +, matches Zl
ZU_LIN = np.array([0.0])  # no upper-slack linear

# Shared cost weights (NONLINEAR_LS tracking psi_ref + heading-rate), mirror T1.
W_TRACK_PSI = 1.0e2
W_DPSI = 1.0e0
W_TRACK_PSI_E = 1.0e2

# Scenario specs: (solver_name, target_x, target_y, cpa_hard, psi_lb, psi_ub,
#                 psi_ref). The two scenarios share the SAME dynamics, SAME box
#                 ([-1.2,1.2]), SAME cost, SAME Zl/zl; ONLY the target position
#                 differs -- isolating the exact-penalty test to the CPA
#                 feasibility change.
#
# Scenario 2 design note: the brief offers two routes for the infeasible case --
# (1) a tight heading box, or (2) "target extremely close to x0". Route (1)
# (box +-0.05) was tried first and caused a QP solver error (status 4) before
# the slack could engage (the tight box + box-violating seed made the first QP
# infeasible). Route (2) is cleaner: put the target close enough to x0 that the
# vessel STARTS INSIDE the CPA disc (dist 30 < cpa_hard 100) -> CPA is
# unambiguously infeasible at the early stages (the fixed initial state itself
# violates g_cpa >= 0), and the slack MUST relax those stages. With a normal
# box the vessel exits the disc by ~stage 6, so the slack is >0 early and ~0
# late -- the cleanest exact-penalty demonstration (non-zero ONLY where needed).
SCENARIOS = [
    {
        "solver": "m5_staging_slack_feas",
        "target_x": 500.0,
        "target_y": 0.0,
        "cpa_hard": 100.0,
        "psi_lb": -1.2,
        "psi_ub": 1.2,
        "psi_ref": 0.3,        # cost wants a gentle north turn (avoidance-like)
        "tag": "feasible",     # CPA satisfiable along every reachable traj
    },
    {
        # Infeasible via TARGET INSIDE THE REACHABLE SET: target at (30,0),
        # cpa_hard=100 (matches production CPA_SAFE=100 / T2). The vessel
        # starts at the origin (dist 30 < 100) -> INSIDE the CPA disc, so
        # g_cpa(x0) = 900 - 10000 = -9100 < 0. Because x0 is a hard equality
        # (lbx0=ubx0=x0), stage 0 cannot satisfy g_cpa >= 0, and the early
        # stages (vessel still near origin, 25m/step) also violate -- the
        # vessel cannot exit a 100m disc in 1-2 stages. So CPA is genuinely
        # infeasible at the early stages and the slack MUST relax them.
        #
        # FINDING (documented in task-3-report.md): with this construction the
        # acatos QP (FULL_CONDENSING_HPIPM) returns HPIPM error 3 on the FIRST
        # SQP iteration -- before the slack engages -- and the solver returns
        # the seed unchanged (status 4). This QP failure is robust to zl
        # (1..1000), Zu (0..Zl), condensing (FULL/PARTIAL), hessian
        # (EXACT/GAUSS_NEWTON), and slack warm-start. The feasible scenario
        # (target 500) converges cleanly (status 0) with slack ~= 2e-12,
        # confirming exact-penalty on the feasible side. The infeasible-side
        # relaxation could NOT be solver-confirmed in this spike -- see the
        # report's BLOCKER analysis (candidate cause: HPIPM soft-constraint QP
        # conditioning when the linearization point heavily violates the
        # softened nonlinear h; P3 TBD-6 / a different QP solver may be
        # needed).
        "solver": "m5_staging_slack_infeas",
        "target_x": 30.0,
        "target_y": 0.0,
        "cpa_hard": 100.0,
        "psi_lb": -1.2,        # SAME wide box as scenario 1
        "psi_ub": 1.2,
        "psi_ref": 0.3,
        "tag": "infeasible",
    },
]


def build_slack_ocp(scen):
    """Build ONE scenario's OCP (dynamics + CPA-h + box + slack + cost).

    Softens CPA row 0 via idxsh=[0] with Zl/zl (lower-side exact-penalty). Uses
    NONLINEAR_LS tracking psi_ref + heading-rate. Returns the configured ocp
    (NOT yet generated).
    """
    ocp = build_base_ocp(N=N, DT=DT,
                         target_x=scen["target_x"],
                         target_y=scen["target_y"],
                         cpa_hard=scen["cpa_hard"],
                         psi_lb=scen["psi_lb"],
                         psi_ub=scen["psi_ub"])  # dynamics + CPA-h (nh=1) + box
    #                                              + EXACT hessian +
    #                                              MERIT_BACKTRACKING; NO cost,
    #                                              NO slack yet.
    model = ocp.model
    # Override base model name so generated artifacts match this scenario's
    # solver name (acatos_solver_<name>.h / libacados_ocp_solver_<name>.so).
    model.name = scen["solver"]

    # ---- Per-scenario QP-solver / hessian override (diagnostic). The base uses
    #      FULL_CONDENSING_HPIPM + EXACT hessian. When the warm-start seed
    #      heavily violates the softened CPA h (infeasible scenario), the EXACT
    #      Lagrangian Hessian evaluated at the seed can lose positive-
    #      definiteness, making the first QP ill-conditioned (HPIPM error 3
    #      after several QP iterations). GAUSS_NEWTON (always PD for the
    #      NONLINEAR_LS cost) isolates whether EXACT hessian is the culprit.
    #      These overrides are LOCAL (do not touch common.py). ----
    if scen.get("qp_solver") is not None:
        ocp.solver_options.qp_solver = scen["qp_solver"]
    if scen.get("hessian_approx") is not None:
        ocp.solver_options.hessian_approx = scen["hessian_approx"]

    # ---- Soft constraint: soften CPA row 0 (idxsh stage-uniform). ----
    # Adds a per-stage slack xi_k for the CPA row. Lower-side exact-penalty via
    # zl; upper side NOT softened (CPA is one-sided >= 0).
    ocp.constraints.idxsh = np.array([0])
    ocp.cost.Zl = ZL
    ocp.cost.zl = ZL_LIN
    ocp.cost.Zu = ZU
    ocp.cost.zu = ZU_LIN

    # ---- Stage cost (NONLINEAR_LS): track psi_ref + penalize heading-rate. ----
    # Same form as T1 (cost_y_expr = vertcat(psi - psi_ref, dpsi), yref=zeros).
    psi = model.x[2]
    dpsi = model.u[0]
    psi_ref = scen["psi_ref"]
    ocp.cost.cost_type = "NONLINEAR_LS"
    model.cost_y_expr = ca.vertcat(psi - psi_ref, dpsi)
    ocp.cost.yref = np.zeros((2,))
    ocp.cost.W = np.diag([W_TRACK_PSI, W_DPSI])
    # Terminal cost: psi tracking only.
    ocp.cost.cost_type_e = "NONLINEAR_LS"
    model.cost_y_expr_e = psi - psi_ref
    ocp.cost.yref_e = np.zeros((1,))
    ocp.cost.W_e = np.array([[W_TRACK_PSI_E]])

    return ocp


def main():
    # Each scenario exports into its OWN subdir under c_generated_code/ so each
    # gets its own Makefile (acatos overwrites c_generated_code/Makefile on each
    # generate; sharing one dir would only build the last solver). The runner
    # then `make`s in each subdir and links both libs.
    base_export = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "c_generated_code")

    for scen in SCENARIOS:
        ocp = build_slack_ocp(scen)
        code_export_dir = os.path.join(base_export, scen["tag"])
        ocp.code_export_directory = code_export_dir
        json_file = os.path.join(code_export_dir,
                                 f"acados_ocp_{scen['solver']}.json")
        AcadosOcpSolver.generate(ocp, json_file=json_file)

        print(f"SLACK GEN [{scen['tag']}]: {scen['solver']} -> {code_export_dir}")
        print(f"  target=({scen['target_x']},{scen['target_y']}) "
              f"cpa_hard={scen['cpa_hard']} box=[{scen['psi_lb']},"
              f"{scen['psi_ub']}] psi_ref={scen['psi_ref']}")
        print(f"  idxsh=[0] Zl={ZL[0]:.0e} zl={ZL_LIN[0]:.0e} "
              f"Zu={ZU[0]:.0e} zu={ZU_LIN[0]:.0e}")
        print(f"  cost NONLINEAR_LS W=diag({W_TRACK_PSI:.0e},{W_DPSI:.0e}) "
              f"W_e={W_TRACK_PSI_E:.0e}")

    print(f"SLACK GEN: both scenario solvers exported under {base_export}/"
          f"{{feasible,infeasible}}/")
    print("MAPPING: (b) per-stage idxsh/Zl/zl (acatos-native soft constraint)")
    print("NOT IMPLEMENTED: (a) sigma-as-control (ruled out, hacky -- see "
          "report); (c) outer sigma loop (spec: not recommended).")


if __name__ == "__main__":
    main()
