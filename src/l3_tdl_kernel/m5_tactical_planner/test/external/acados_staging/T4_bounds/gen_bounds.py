#!/usr/bin/env python3
"""P1b-0 T4 -- bound schedule per-stage lb/ub + OR-composition (acados spike).

QUESTION (the spike): the production NLP applies a per-cycle BOUND SCHEDULE that
SOFTENS the hard CPA constraint at the EARLY stages (k < cpa_hard_from_k -- the
vessel cannot physically clear the CPA disc in the first few steps given ROT
limits, so a hard constraint there is infeasible; J_colreg pulls it back) and
HARDENS it from a stage threshold onward (k >= cpa_hard_from_k). PLUS the
OR-composition: the prefix schedule (T1: k<K prefix equality) AND the CPA
suffix-hard schedule both apply simultaneously (prefix-soften k<3 UNION
cpa-suffix-hard k>=3). Does this per-stage schedule map cleanly to acatos?

VERDICT (resolved by runner_bounds.cpp): see task-4-report.md.

====================  acatos bound-schedule reality (the subtlety)  ====================
acatos 0.4.4 constraints are STAGE-UNIFORM:
  - ocp.constraints.lh / uh  is a SINGLE array indexed by h-ROW (not stage); it
    is applied UNIFORMLY to every stage 0..N. You CANNOT make lb/uh
    stage-dependent (no "k<3 soft, k>=3 hard" via lh/uh alone).
  - ocp.constraints.lbx / ubx (and idxsh/Zl/zl) are likewise stage-uniform
    (T1/T3 lessons).

So the per-stage hard/soft switch MUST live INSIDE the constraint expression,
driven by a per-stage parameter `p` (the ONLY per-stage lever acatos exposes),
set independently per stage via the GENERATED
`m5_staging_bounds_acados_update_params(capsule, stage, vals, np)` (T1 lesson --
NOT ocp_nlp_in_set(..,"p",..), which is unavailable in this build).

====================  Why T3 idxsh slack CANNOT do the split (documented)  ====================
Could T3's per-stage slack (idxsh=[0] + Zl/zl on the CPA row) give a
k<K-soft / k>=K-hard split? NO cleanly -- idxsh is STAGE-UNIFORM: softening
row 0 adds a per-stage slack xi_k but softens the SAME row at EVERY stage
(0..N). So it softens k>=K too, where we need HARD. Setting Zl/zl=0 at k>=K is
NOT per-stage (Zl/zl are stage-uniform). You cannot "disable the slack at
k>=K" via stage-uniform idxsh/Zl/zl. The activation-factor approach (T1-style
on the CPA row) is the RIGHT tool for a stage-dependent hard/soft split.
This is the critical T3 finding this task documents and bypasses.

====================  The activation-factor technique (T1, applied to CPA)  ====================
WINNING STRATEGY: a per-stage activation parameter on the CPA h-row, exactly
T1's `pact` mechanism applied to CPA.

  Row 0 (CPA):  h_cpa = cpa_act * g_cpa
    where g_cpa = (px-tx)^2 + (py-ty)^2 - cpa_hard^2  (the base CPA residual).
    - k >= cpa_hard_from_k : cpa_act = 1.0  -> h = g_cpa, enforce g_cpa >= 0 (HARD).
    - k <  cpa_hard_from_k : cpa_act = 0.0  -> h = 0 identically (zero value
        AND zero Jacobian/Hessian) -> the row imposes NOTHING (SOFTENED). The
        CPA cost (below), NOT the hard constraint, guides the vessel here --
        mirroring production where J_colreg pulls the softened stages back.
    Uniform bound on row 0: lh[0] = 0, uh[0] = UH_INF (one-sided >= 0). With
    cpa_act=1 it binds; with cpa_act=0 the row is identically 0 and satisfies
    0 <= 0 <= UH_INF trivially -> no constraint on the vessel.

====================  The OR-composition (the KEY test of T4)  ====================
Combine TWO per-stage schedules on TWO DIFFERENT h-rows (they compose by living
on separate rows -- INDEPENDENT per-stage switches):

  Row 0 (CPA suffix-hard):  h_cpa   = cpa_act * g_cpa
  Row 1 (prefix equality):  h_pref  = pact_pre * (psi - ppsi_pre)

  con_h_expr = vertcat(h_cpa, h_pref)   (nh = 2)
  Stage-UNIFORM bounds: lh = [0, 0], uh = [UH_INF, 0]
    (row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0 <= h <= 0).

  Per-stage p = [cpa_act, ppsi_pre, pact_pre]  (np = 3):
    k <  3 : cpa_act = 0.0 (CPA SOFT), pact_pre = 1.0, ppsi_pre = prefix_psi[k]
             -> prefix binds (psi == prefix_psi[k]), CPA row disabled.
    k >= 3 : cpa_act = 1.0 (CPA HARD), pact_pre = 0.0, ppsi_pre = 0 (don't care)
             -> CPA binds (g_cpa >= 0), prefix row disabled (psi free in box).

This is the OR-composition: prefix-soften(k<3, T1 activation) UNION
cpa-suffix-hard(k>=3, this task's activation). The two schedules are
INDEPENDENT per-stage predicates on independent rows -- they compose for free.
(cpa_hard_from_k and K_prefix coincide at 3 here; if they DIFFERED, e.g.
prefix K=3 and cpa_hard_from_k=5, the per-row activation handles any
combination -- the union of per-stage predicates. No cross-row coupling.)

====================  Stage cost (NONLINEAR_LS + smooth CPA barrier)  ====================
Mirror T1: NONLINEAR_LS tracking psi_ref=0.3 + heading-rate, so the suffix
(suffix free, psi in box) has a sensible CPA-feasible target. ALSO add a SMOOTH
CPA penalty to the cost so the SOFTENED early stages (k<3, hard CPA disabled)
are still guided AWAY from the target -- mirroring production where J_colreg
pulls the softened stages back. The penalty is the smooth one-sided barrier

    barrier(g_cpa) = w_cpa_soft * ( sqrt(g_cpa^2 + 1) - g_cpa ) / 2

which smoothly approximates w_cpa_soft * max(0, -g_cpa): ~0 when the vessel is
outside the disc (g_cpa >> 0), ~ -g_cpa (linear pull) when inside (g_cpa < 0).
It is C2-smooth (EXACT hessian compatible) and avoids the nondifferentiable
kink of a literal max(0, .). The assertion is about the SCHEDULE mapping, not
the cost quality -- the barrier is a faithful, minimal J_colreg stand-in.

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

SOLVER_NAME = "m5_staging_bounds"
N, DT = 10, 5.0
K_PREFIX = 3            # prefix equality stages: k < K_PREFIX
CPA_HARD_FROM_K = 3     # CPA hard from stage CPA_HARD_FROM_K onward
PSI_REF = 0.3           # suffix cost target
PREFIX_PSI = [0.1, 0.2, 0.3]   # committed prefix heading for k=0,1,2
# Smooth CPA barrier weight (J_colreg stand-in for the softened stages). Small
# relative to the psi-tracking weight so it guides, not dominates.
W_CPA_SOFT = 1.0e0


def main():
    ocp = build_base_ocp(N=N, DT=DT)  # dynamics + CPA-h (nh=1) + box + EXACT +
    #                                    MERIT_BACKTRACKING + warm-start opts; NO cost.
    model = ocp.model
    # Override the base model name so generated artifacts (header
    # acados_solver_<name>.h, lib libacados_ocp_solver_<name>.so) match this
    # task's solver name. build_base_ocp set model.name="m5_staging_base".
    model.name = SOLVER_NAME

    # ---- Parameters: p = [cpa_act, ppsi_pre, pact_pre] (np=3). The ONLY
    #      per-stage lever acatos gives, so BOTH stage-switches (CPA hard/soft
    #      AND prefix equality/free) are encoded here on TWO h-rows. ----
    cpa_act = ca.SX.sym("cpa_act")      # CPA activation: 1.0=hard, 0.0=soft(disabled)
    ppsi_pre = ca.SX.sym("ppsi_pre")    # prefix heading target for this stage
    pact_pre = ca.SX.sym("pact_pre")    # prefix activation: 1.0=enforce, 0.0=free
    model.p = ca.vertcat(cpa_act, ppsi_pre, pact_pre)
    ocp.parameter_values = np.array([1.0, 0.0, 1.0])  # default; overridden per
    #                                                    stage by the runner via
    #                                                    update_params(k, vals, 3).

    # ---- Rebuild con_h_expr as the 2-row OR-composition. The base set
    #      con_h_expr = vertcat(g_cpa) (nh=1). Replace it entirely with the
    #      activation-factor forms so BOTH rows are parametric. ----
    px = model.x[0]
    py = model.x[1]
    psi = model.x[2]
    # g_cpa residual (matches build_base_ocp: cpa_hard=100, target (200,0)).
    g_cpa = (px - 200.0) ** 2 + (py - 0.0) ** 2 - 100.0 ** 2
    # Row 0: CPA suffix-hard (per-stage activation). cpa_act=1 -> binds; =0 -> row
    #        identically 0 (disabled / softened).
    h_cpa = cpa_act * g_cpa
    # Row 1: prefix equality (T1 activation, reproduced). pact_pre=1 -> binds
    #        psi == ppsi_pre; =0 -> row identically 0 (psi free).
    h_prefix = pact_pre * (psi - ppsi_pre)
    model.con_h_expr = ca.vertcat(h_cpa, h_prefix)  # nh 1 -> 2

    # ---- Reset h bounds to length nh=2 (acados make_consistent requires the
    #      dimension match once con_h_expr was replaced). Stage-UNIFORM (the
    #      point): row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0<=h<=0.
    #      The stage-dependence is carried by `p`, NOT by these bounds. ----
    nh = model.con_h_expr.rows()
    ocp.constraints.lh = np.array([0.0, 0.0])
    ocp.constraints.uh = np.array([UH_INF, 0.0])  # F2: bounded uh, not np.inf
    ocp.constraints.lh0 = np.array([0.0, 0.0])
    ocp.constraints.uh0 = np.array([UH_INF, 0.0])

    # ---- Stage cost (NONLINEAR_LS + smooth CPA barrier). Track psi_ref +
    #      penalize heading-rate (T1 mirror) AND add a smooth one-sided CPA
    #      barrier so the SOFTENED stages (k<3, hard CPA off) are still guided
    #      away from the target (J_colreg stand-in). ----
    # The NONLINEAR_LS residual vector is extended with the barrier so the cost
    # carries both the tracking and the CPA pull. yref stays 0 (barrier target
    # is 0 = "no violation").
    dpsi = model.u[0]
    # Smooth approximation of max(0, -g_cpa): (sqrt(g^2+1) - g)/2. C2-smooth,
    # EXACT-hessian compatible. ~0 when outside the disc, ~-g_cpa when inside.
    cpa_barrier = (ca.sqrt(g_cpa ** 2 + 1.0) - g_cpa) / 2.0
    ocp.cost.cost_type = "NONLINEAR_LS"
    # Residuals: [psi - psi_ref, dpsi, cpa_barrier]. The CPA barrier is the
    # J_colreg stand-in for the softened stages.
    model.cost_y_expr = ca.vertcat(psi - PSI_REF, dpsi, cpa_barrier)
    ocp.cost.yref = np.zeros((3,))
    ocp.cost.W = np.diag([1.0e2, 1.0e0, W_CPA_SOFT])
    # Terminal cost: psi tracking + CPA barrier (no control at terminal).
    ocp.cost.cost_type_e = "NONLINEAR_LS"
    model.cost_y_expr_e = ca.vertcat(psi - PSI_REF, cpa_barrier)
    ocp.cost.yref_e = np.zeros((2,))
    ocp.cost.W_e = np.diag([1.0e2, W_CPA_SOFT])

    # ---- Export C code. ----
    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"BOUNDS GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"OR-COMPOSITION: con_h_expr = vertcat(cpa_act*g_cpa, pact_pre*(psi-ppsi_pre))")
    print(f"  nh={nh}, uniform lh=[0,0] uh=[UH_INF,0], per-stage p=[cpa_act,ppsi_pre,pact_pre] (np=3)")
    print(f"SCHEDULE: prefix-soften(k<{K_PREFIX}) UNION cpa-suffix-hard(k>={CPA_HARD_FROM_K})")
    print(f"  k<{K_PREFIX}: cpa_act=0 (CPA SOFT), pact_pre=1 (prefix EQ), ppsi_pre=prefix_psi[k]")
    print(f"  k>={CPA_HARD_FROM_K}: cpa_act=1 (CPA HARD), pact_pre=0 (prefix FREE), ppsi_pre=0")
    print(f"PREFIX_PSI(k<{K_PREFIX})={PREFIX_PSI}  PSI_REF(suffix)={PSI_REF}")
    print(f"COST: NONLINEAR_LS [psi-psi_ref, dpsi, cpa_barrier] W=diag(1e2,1e0,{W_CPA_SOFT:.0e})")
    print(f"  cpa_barrier = (sqrt(g_cpa^2+1)-g_cpa)/2 ~ max(0,-g_cpa) [J_colreg stand-in]")


if __name__ == "__main__":
    main()
