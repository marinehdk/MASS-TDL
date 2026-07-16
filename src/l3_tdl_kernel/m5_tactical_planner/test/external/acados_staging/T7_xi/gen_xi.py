#!/usr/bin/env python3
"""P1b-1a T7 -- per-target per-step xi high-dim slack staging.

QUESTION (the staging gate): the production multi-target CPA soft-constraint needs
a PER-TARGET PER-STEP slack -- each target gets its OWN lower-slack per stage, so
that one target's unavoidable CPA violation is relaxed WITHOUT distorting another
target's feasible CPA row. P1b-0 T3 verified the SINGLE-scalar form (idxsh=[0],
one slack per stage softening the single CPA row). T7 verifies the DIMENSION
UPGRADE: idxsh=[0,1] (one slack per CPA row per stage), with a mixed L1/L2
penalty (rho*xi + 0.5*w*xi^2, TBD-6 VR-TBD6 option B).

The KEY property to verify (the staging gate): per-target slacks are INDEPENDENT
in the exact-penalty sense. With two CPA rows (one per target) each softened by
its own per-stage xi_{t,k}:
  - target A CPA-feasible (g_cpa_A >= 0 at every stage) -> xi_A,k ~ 0 at every
    stage (the linear+quadratic penalty drives the slack to 0 when nothing needs
    relaxing), REGARDLESS of target B.
  - target B CPA-infeasible at some stage (g_cpa_B < 0, unavoidable CPA entry)
    -> xi_B,k > 0 at the violating stage, relaxing B's row by exactly the
    violation (g_cpa_B + xi_B,k >= 0).
If acatos couples the multi-row idxsh slacks so that A's feasible slack does NOT
go to ~0 because of B's violation, that is a REAL finding (brief Step 4) -- STOP
and report honestly. Do NOT widen tolerances to force a pass.

====================  acatos soft-constraint API (T7 dimension upgrade)  ==========
From T3 (P1b-0, the single-scalar form) -- T7 generalizes to per-target:
  - ocp.constraints.idxsh = np.array([0, 1])  -- 0-based indices WITHIN the nh=2
    h-rows of the rows to soften. acatos adds a per-stage slack for EACH softened
    row (stage-uniform: rows 0 and 1 are softened at every stage). With nh=2 and
    idxsh=[0,1], acatos allocates ns=2 lower-slacks per stage: sl[0]=xi_A,k
    (relaxes row 0, target A's CPA), sl[1]=xi_B,k (relaxes row 1, target B's CPA).
  - ocp.cost.Zl = np.array([w_quad, w_quad])  -- quadratic regularizer per slack
    (the 0.5*w*xi^2 term of the mixed L1/L2). Keeps the QP well-conditioned.
  - ocp.cost.zl = np.array([rho_lin, rho_lin])  -- LINEAR exact-penalty weight per
    slack (the rho*xi term). THIS IS THE EXACT-PENALTY LEVER: with zl larger than
    each row's Lagrange multiplier, the slack is driven to EXACTLY 0 when its row
    is feasible. Two targets have different lambda*; rho must exceed max(||lambda*
    _A||inf, ||lambda*_B||inf). zl is the SAME for both slacks here (rho takes the
    max); if independence fails, bump zl (brief Step 4).
  - ocp.cost.Zu = np.array([w_quad, w_quad]), ocp.cost.zu = np.array([0, 0]) --
    upper-slack weights. CPA is one-sided (>= 0; the upper bound uh=UH_INF never
    binds), so the upper side is not softened meaningfully; Zu=Zl pins su at 0 via
    a finite quadratic cost, zu=0 (no linear pull on the upper side).
  - The per-stage slack is read back in C via
    ocp_nlp_out_get(cfg, dims, out, stage, "sl", sl_vec) where sl_vec has length
    ns = len(idxsh) = 2 (sl_vec[0]=xi_A,k, sl_vec[1]=xi_B,k).

====================  The per-target xi OCP (spec-locked)  ====================
Start from build_base_ocp (single-channel, x=[px,py,psi,u]). Then REPLACE the
base con_h_expr (nh=1, single CPA row around a baked target) with a per-target
multi-row form (nh=2):
  con_h_expr = vertcat(g_cpa_A, g_cpa_B)
  g_cpa_t = (px - txdrift_t)^2 + (py - tydrift_t)^2 - cpa_hard^2   (per target t)
The target positions are PARAMETRIC per stage (txdrift_t/tydrift_t in model.p),
set per stage via the GENERATED m5_staging_xi_*_acados_update_params (T2 pattern;
staging-finding 3: NOT ocp_nlp_in_set "p"). cpa_hard is a LITERAL baked per
solver (same for both rows -- the CPA clearance radius is target-uniform).
Stage-uniform lh=[0,0], uh=[UH_INF, UH_INF] (F2 bounded uh, one-sided >= 0).

Param vector (minimal -- only the CPA-h needs): model.p = [txdrift_A, tydrift_A,
txdrift_B, tydrift_B], NP=4. The brief's NP=7 layout ([disc_k, {txdrift,tydrift,
tw} x NT]) is the EXTERNAL-colreg-cost variant (T2/T5); T7 uses the brief's
allowed NONLINEAR_LS minimal cost (the point is the slack, not the cost), so only
the CPA-h params are needed. The per-stage update_params MECHANISM (T2 pattern)
is preserved -- the runner sets per-stage target drift (static here; production
has moving targets).

Cost: NONLINEAR_LS tracking psi_ref + heading-rate (T3 style -- T7 is T3's per-
target generalization, so the same minimal cost isolates the slack test). Same
W/yref form as T3.

Solver opts unchanged from build_base_ocp (EXACT F3, MERIT_BACKTRACKING F4,
DISCRETE, SQP).

====================  Two scenarios (two generated solvers)  ====================
acatos bakes the heading box (lbx/ubx, stage-uniform) and cpa_hard (into
con_h_expr) into the generated solver -- NOT runtime-settable per solve. The two
scenarios need DIFFERENT box + cpa_hard, so each needs its OWN generated solver
(exactly like T3_slack):
  m5_staging_xi_feas   : cpa_hard=100, box psi in [-1.2, 1.2], BOTH targets far.
                         Both CPA rows feasible at every stage -> exact-penalty
                         drives BOTH xi_A and xi_B to ~0. Asserts
                         max over (target, stage) |xi_{t,k}| < tol.
  m5_staging_xi_infeas : cpa_hard=22, box psi in [-0.02, 0.02], target A far
                         (500,0), target B close (100,20). FUTURE-VIOLATION on B
                         (T3 lesson / staging-finding 4): vessel starts OUTSIDE
                         B's disc (stage-0 dist 102 > 22, g_cpa_B(0)=+9916 > 0 --
                         FEASIBLE START), but the tight box makes B's disc
                         unavoidable around stage k>=3 (best south avoidance
                         reaches only py ~ -1.5 at stage 4 -> dist 21.5 < 22,
                         g_cpa_B=-22). Target A stays feasible throughout (dist
                         >= 250 > 22). Asserts xi_A,k ~ 0 at every stage
                         (INDEPENDENT exact-penalty -- A feasible regardless of
                         B), xi_B,k > tol at the violating stage, AND
                         g_cpa_B + xi_B,k >= -tol (B's row relaxed by its slack).
runner_xi.cpp links BOTH libs and runs the two scenarios sequentially.

CRITICAL (T3 lesson): the slack activates on FUTURE-VIOLATION, NOT start-inside-
disc. HPIPM cannot iterate from an infeasible seed. Scenario 2's target B is
feasible at the START but unavoidably enters the CPA disc at a FUTURE stage (the
vessel's own motion + the tight box drives it in around k>=3). Do NOT start the
seed inside B's disc.

NOT production NLP code. staging/external only. IPOPT path untouched.
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
NT = 2  # two CPA targets (two rows in con_h_expr)

# ---- Param vector: model.p = [txdrift_A, tydrift_A, txdrift_B, tydrift_B].
#      NP=4 (minimal -- only the CPA-h target positions; NONLINEAR_LS cost does
#      not need disc_k/tw). Set per stage via the GENERATED
#      <name>_acados_update_params(capsule, stage, vals, NP). ----
NP = 2 * NT  # = 4

# ---- Per-target mixed L1/L2 slack penalty weights (TBD-6 VR-TBD6 option B).
#      Zl = small quadratic regularizer (the 0.5*w*xi^2 term) per slack.
#      zl = LARGE linear weight (the rho*xi term) -> L1 exact-penalty (slack = 0
#           when the row is feasible, provided zl exceeds the row's Lagrange
#           multiplier). Two targets have different lambda*; rho (zl) must exceed
#           the max -> SAME zl for both slacks here.
#      Zu = Zl (pins the unused upper slack su at 0 via a finite quadratic cost;
#           CPA is one-sided >= 0, uh=UH_INF never binds). zu = 0 (no upper
#           linear pull). Length 2 each (one per softened row, idxsh=[0,1]). ----
W_QUAD = 1.0e2     # quadratic regularizer (Zl, Zu)
RHO_LIN = 1.0e3    # linear exact-penalty weight (zl); zu stays 0
ZL = np.array([W_QUAD, W_QUAD])
ZL_LIN = np.array([RHO_LIN, RHO_LIN])
ZU = np.array([W_QUAD, W_QUAD])
ZU_LIN = np.array([0.0, 0.0])

# Shared cost weights (NONLINEAR_LS tracking psi_ref + heading-rate), mirror T3.
W_TRACK_PSI = 1.0e2
W_DPSI = 1.0e0
W_TRACK_PSI_E = 1.0e2

# Scenario specs: (solver_name, cpa_hard, psi_lb, psi_ub, psi_ref, targets).
# Each scenario BAKES cpa_hard (into con_h_expr) and the heading box (lbx/ubx)
# into its OWN generated solver. The two targets' positions are PARAMETRIC
# (txdrift/tydrift per target), set per stage at runtime.
#
# Scenario 1 (BOTH FEASIBLE): cpa_hard=100, wide box +-1.2. Target A (500,0)
# and target B (500,500) are both far from every reachable trajectory (vessel
# travels ~250m east over the horizon). Both CPA rows feasible at every stage ->
# exact-penalty drives BOTH xi_A and xi_B to ~0.
#
# Scenario 2 (A feasible, B infeasible -- FUTURE-VIOLATION): cpa_hard=22, TIGHT
# box psi in [-0.02, 0.02]. Target A (500,0) far (dist >= 250 > 22 -> feasible
# throughout). Target B (100,20) close: vessel starts OUTSIDE B's disc (stage-0
# dist 102 > 22, g_cpa_B(0)=+9916 > 0 -- FEASIBLE START) but the tight box makes
# B's disc unavoidable around stage k>=3 (best south avoidance reaches only
# py~-1.5 at stage 4 -> dist 21.5 < 22 -> g_cpa_B~-22). This is the production-
# correct sigma/xi trigger (currently CPA-feasible but cannot AVOID a future CPA
# violation within maneuvering limits), AND well-conditioned (violation ~22-84,
# not thousands). Target A's row stays feasible -> xi_A ~ 0 INDEPENDENT of B's
# relaxation -> xi_B > 0. This is the per-target independence T7 verifies.
SCENARIOS = [
    {
        "solver": "m5_staging_xi_feas",
        "cpa_hard": 100.0,
        "psi_lb": -1.2,
        "psi_ub": 1.2,
        "psi_ref": 0.3,        # cost wants a gentle north turn (avoidance-like)
        "targets": [(500.0, 0.0), (500.0, 500.0)],   # A far east, B far NE
        "tag": "both_feasible",
    },
    {
        # Infeasible via FUTURE CPA VIOLATION on target B (T3's production-correct
        # trigger, per-target). cpa_hard=22, TIGHT box [-0.02, 0.02]. Target A
        # (500,0) far (dist >= 250 > 22 -> feasible throughout -> xi_A ~ 0
        # INDEPENDENT of B). Target B (100,20): vessel starts OUTSIDE B's disc
        # (stage-0 dist 102 > 22) but unavoidably enters it around stage 4 (best
        # south avoidance py~-1.5 -> dist 21.5 < 22). xi_B > 0 relaxes B's row;
        # xi_A stays ~0 -- the per-target exact-penalty independence.
        "solver": "m5_staging_xi_infeas",
        "cpa_hard": 22.0,
        "psi_lb": -0.02,       # TIGHT: best south avoidance only reaches py~-1.5
        "psi_ub": 0.02,
        "psi_ref": 0.0,        # straight-east cost (seed is least-violating)
        "targets": [(500.0, 0.0), (100.0, 20.0)],   # A far east, B in path
        "tag": "A_feasible_B_infeasible",
    },
]


def build_xi_ocp(scen):
    """Build ONE scenario's OCP (dynamics + per-target CPA-h + box + per-target
    slack + minimal NONLINEAR_LS cost).

    Replaces the base con_h_expr (nh=1) with a per-target 2-row form (nh=2),
    softens BOTH rows via idxsh=[0,1] with mixed L1/L2 Zl/zl/Zu/zu (length 2
    each). Returns the configured ocp (NOT yet generated).
    """
    # build_base_ocp with this scenario's cpa_hard + heading box baked in. The
    # base sets con_h_expr=vertcat(g_cpa) (nh=1) around a DUMMY single target --
    # we REPLACE it below with the per-target 2-row form. The target_x/y args
    # here only seed the base's (discarded) single-row h; the real targets are
    # parametric.
    ocp = build_base_ocp(N=N, DT=DT,
                         target_x=0.0, target_y=0.0,       # dummy (replaced below)
                         cpa_hard=scen["cpa_hard"],
                         psi_lb=scen["psi_lb"],
                         psi_ub=scen["psi_ub"])  # dynamics + CPA-h (nh=1, dummy)
    #                                              + box + EXACT hessian +
    #                                              MERIT_BACKTRACKING; NO cost,
    #                                              NO slack yet.
    model = ocp.model
    # Override base model name so generated artifacts match this scenario's
    # solver name (acatos_solver_<name>.h / libacados_ocp_solver_<name>.so).
    model.name = scen["solver"]

    # ---- Per-target parametric CPA h (REPLACE the base nh=1 h). ----
    # model.p = [txdrift_A, tydrift_A, txdrift_B, tydrift_B]  (NP=4). The target
    # positions are parametric (set per stage at runtime via update_params); the
    # CPA clearance radius cpa_hard is a literal baked per solver.
    px = model.x[0]
    py = model.x[1]
    p_syms = []
    for t in range(NT):
        p_syms.append(ca.SX.sym(f"txdrift_{t}"))
        p_syms.append(ca.SX.sym(f"tydrift_{t}"))
    model.p = ca.vertcat(*p_syms)
    cpa_hard = scen["cpa_hard"]
    g_rows = []
    for t in range(NT):
        txdrift = model.p[2 * t + 0]
        tydrift = model.p[2 * t + 1]
        g_t = (px - txdrift) ** 2 + (py - tydrift) ** 2 - cpa_hard ** 2
        g_rows.append(g_t)
    model.con_h_expr = ca.vertcat(*g_rows)  # nh 1 -> 2 (per-target)

    # ---- Reset h bounds to length nh=2 (acatos make_consistent requires the
    #      dimension match once con_h_expr was replaced). Stage-UNIFORM:
    #      both rows one-sided >= 0 (lh=0, uh=UH_INF per F2). The per-stage
    #      target drift is carried by `p`, NOT by these bounds. ----
    nh = model.con_h_expr.rows()
    ocp.constraints.lh = np.zeros((nh,))
    ocp.constraints.uh = np.full((nh,), UH_INF)
    ocp.constraints.lh0 = np.zeros((nh,))
    ocp.constraints.uh0 = np.full((nh,), UH_INF)

    # ---- Per-target soft constraint: soften BOTH CPA rows (idxsh=[0,1]).
    #      acatos adds ns=2 lower-slacks per stage: sl[0]=xi_A,k (row 0), sl[1]=
    #      xi_B,k (row 1). Mixed L1/L2 penalty (rho*xi + 0.5*w*xi^2) per slack
    #      via zl (linear, exact-penalty lever) + Zl (quadratic regularizer).
    #      Upper side: Zu=Zl pins su at 0 (CPA one-sided), zu=0. ----
    ocp.constraints.idxsh = np.array([0, 1])
    ocp.cost.Zl = ZL
    ocp.cost.zl = ZL_LIN
    ocp.cost.Zu = ZU
    ocp.cost.zu = ZU_LIN

    # ---- Stage cost (NONLINEAR_LS): track psi_ref + penalize heading-rate.
    #      Same minimal form as T3 (T7 is T3's per-target generalization -- the
    #      point is the slack, not the cost). The cost does NOT depend on the
    #      targets; it just drives the vessel along a reference heading so the
    #      CPA feasibility is determined by the target positions + the box. ----
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
        ocp = build_xi_ocp(scen)
        # Default parameter_values: stage-0 target positions (the runner sets
        # per-stage anyway, but acatos requires parameter_values of length NP).
        p_default = np.zeros((NP,))
        for t in range(NT):
            tx, ty = scen["targets"][t]
            p_default[2 * t + 0] = tx
            p_default[2 * t + 1] = ty
        ocp.parameter_values = p_default

        code_export_dir = os.path.join(base_export, scen["tag"])
        ocp.code_export_directory = code_export_dir
        json_file = os.path.join(code_export_dir,
                                 f"acados_ocp_{scen['solver']}.json")
        AcadosOcpSolver.generate(ocp, json_file=json_file)

        print(f"XI GEN [{scen['tag']}]: {scen['solver']} -> {code_export_dir}")
        print(f"  cpa_hard={scen['cpa_hard']} box=[{scen['psi_lb']},"
              f"{scen['psi_ub']}] psi_ref={scen['psi_ref']}")
        print(f"  targets: A={scen['targets'][0]}  B={scen['targets'][1]}")
        print(f"  con_h_expr = vertcat(g_cpa_A, g_cpa_B)  "
              f"nh={ocp.model.con_h_expr.rows()}")
        print(f"  idxsh=[0,1] (per-target slack, ns=2/stage)")
        print(f"  Zl={ZL} zl={ZL_LIN} Zu={ZU} zu={ZU_LIN} (mixed L1/L2)")
        print(f"  param p=[txdrift_A,tydrift_A,txdrift_B,tydrift_B] NP={NP}")
        print(f"  cost NONLINEAR_LS W=diag({W_TRACK_PSI:.0e},{W_DPSI:.0e}) "
              f"W_e={W_TRACK_PSI_E:.0e}")

    print(f"XI GEN: both scenario solvers exported under {base_export}/"
          f"{{both_feasible,A_feasible_B_infeasible}}/")
    print("DIMENSION UPGRADE: single-scalar sigma (idxsh=[0]) -> per-target xi "
          "(idxsh=[0,1], ns=2/stage). Mixed L1/L2 (rho*xi + 0.5*w*xi^2).")


if __name__ == "__main__":
    main()
