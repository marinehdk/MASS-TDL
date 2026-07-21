#!/usr/bin/env python3
"""P1b-0 T5 -- merged 4-point coexistence (the FINAL P1b-0 staging gate).

QUESTION (the spike): do ALL FOUR verified complexity points from T1-T4 compose
into ONE acatos OCP and SOLVE together without infeasibility / numerical
interaction failure? This is the gate for P1b-1 (the full migration spec): if
the four points coexist, staging is scalable and P1b-1 can be written.

The four points (each VERIFIED individually in T1-T4):
  P1 (T1) prefix equality       : h_prefix = pact_pre*(psi-ppsi_pre)
  P2 (T2) J_colreg per-stage    : EXTERNAL cost, cost_scaling=ones(N+1) REQUIRED
       EXTERNAL cost
  P3 (T3) sigma slack           : idxsh=[0] soften CPA row, Zl=1e2/zl=1e3 exact
  P4 (T4) CPA bound schedule    : h_cpa = cpa_act*g_cpa (k<3 off, k>=3 hard)

====================  The combined OCP (this file)  ====================
con_h_expr = vertcat( cpa_act * g_cpa ,  pact_pre * (psi - ppsi_pre) )   (nh=2)
Stage-UNIFORM bounds: lh = [0.0, 0.0], uh = [UH_INF, 0.0]
  (row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0 <= h <= 0).

idxsh = [0]  -> soften row 0 (the CPA row), stage-uniform. acatos adds a
                per-stage lower-slack xi_k for the CPA row. Zl/zl/Zu/zu per T3.
cost_type = EXTERNAL  -> per-stage J_colreg form (T2), cost_scaling=ones(N+1).

====================  CRITICAL composition: CPA row carries BOTH slack (P3)
                       AND activation (P4)  ====================
Both P3 (idxsh slack on the CPA row) and P4 (cpa_act activation on the CPA row)
act on the SAME row (row 0). The composition is:
  h_cpa = cpa_act * g_cpa   (the row expression)
  idxsh = [0]               (soften this row -> per-stage xi_k)
  uniform lh[0]=0, uh[0]=UH_INF (one-sided >= 0, i.e. h_cpa + xi_k >= 0).
At k < cpa_hard_from_k : cpa_act = 0.0 -> h_cpa is identically 0 (zero value
  AND zero Jacobian/Hessian) -> the row is DISABLED by the schedule. The slack
  xi_k there relaxes a row that is already trivially satisfied (0 + xi_k >= 0
  with xi_k >= 0), so the exact-penalty linear weight zl drives xi_k -> 0.
  (Schedule-softened; slack trivially 0 -- the two mechanisms DO NOT conflict:
  the activation zeros the row, the slack has nothing to relax.)
At k >= cpa_hard_from_k: cpa_act = 1.0 -> h_cpa = g_cpa, enforced >= 0. If the
  HARD stage is feasible (this scenario: seed clears the disc by stage 3), the
  exact-penalty weight zl drives xi_k -> 0 (nothing to relax -> slack 0). If a
  HARD stage were infeasible, xi_k > 0 would relax it by exactly the violation
  (T3 exact-penalty). The activation and the slack are ORTHOGONAL on this row:
  the activation controls WHETHER the row binds, the slack controls HOW MUCH it
  may be relaxed when it does. The spike value is confirming they don't fight.

Why this is clean (not a conflict): the idxsh slack adds xi_k >= 0 to the LOWER
side of the row's constraint, i.e. the solver sees `h_cpa + xi_k >= 0`. With
cpa_act=0 the row is `0 + xi_k >= 0`, satisfied for any xi_k >= 0, and the
linear penalty zl*xi_k (zl>0) plus quadratic Zl*xi_k^2 push xi_k to 0 -- the
slack is a COST with no benefit, so it vanishes. With cpa_act=1 the row is
`g_cpa + xi_k >= 0`; if g_cpa >= 0 already, xi_k -> 0 (same penalty logic). The
two mechanisms are consistent (both push xi_k -> 0 when the row is satisfied),
never adversarial. This is verified by runner_merged.cpp.

====================  Per-stage parameter layout (np = 10)  ====================
The ONLY per-stage lever acatos exposes is the parameter vector p, set per stage
via the GENERATED m5_staging_merged_acados_update_params(capsule, stage, vals,
np) (T1 lesson -- NOT ocp_nlp_in_set(..,"p",..), unavailable in this build).
Combine ALL per-stage values from P1/P2/P4 into ONE p:

  p = [ cpa_act,                          # P4  CPA activation (0=off, 1=hard)
        ppsi_pre,                         # P1  prefix heading target
        pact_pre,                         # P1  prefix activation (0=free,1=eq)
        disc_k,                           # P2  TCPA discount exp(-(k*dt)/T)
        txdrift_A, tydrift_A, tw_A,       # P2  target A drift + weight
        txdrift_B, tydrift_B, tw_B ]      # P2  target B drift + weight
  np = 3 (P1+P4) + 1 + 3*NT (P2) = 3 + 1 + 6 = 10.

Per stage (k = 0..N, terminal carries p too):
  k <  3 : cpa_act=0.0, pact_pre=1.0, ppsi_pre=prefix_psi[k]   (prefix binds,
                                                           CPA schedule-softened)
  k >= 3 : cpa_act=1.0, pact_pre=0.0, ppsi_pre=0.0 (don't-care)  (CPA hard,
                                                           prefix free)
  disc_k = exp(-(k*DT)/T_DISCOUNT_S)   (per stage)
  txdrift_tk = tx + ts*cos(tc)*(k*DT); tydrift_tk = ty + ts*sin(tc)*(k*DT)

====================  Cost (P2 EXTERNAL, verbatim from T2)  ====================
cost_type = EXTERNAL, cost_type_0 = EXTERNAL, cost_type_e = EXTERNAL.
Per-stage external cost = (1/scale_denom) * sum_t tw_t * disc_k *
                          exp(-zeta*(d_t - cpa_safe))
  d_t = sqrt((px - txdrift_t)^2 + (py - tydrift_t)^2 + kSqrtGuard)
scale_denom = max(1, NT*N) = 20. cost_scaling = ones(N+1) (T2 CRITICAL -- else
acatos multiplies each staged cost by DT). Terminal expr = 0 so acatos_total ==
sum_{k=0}^{N-1} stage_k (matches the production N-term lumped sum).

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

SOLVER_NAME = "m5_staging_merged"

# ---- Constants (must match runner_merged.cpp EXACTLY) ----
N, DT = 10, 5.0
K_PREFIX = 3            # prefix equality stages: k < K_PREFIX  (P1)
CPA_HARD_FROM_K = 3     # CPA hard from this stage onward     (P4)

# P2 (T2) constants.
T_DISCOUNT_S = 100.0
ZETA = 5.0e-3
CPA_SAFE = 100.0
NT = 2                                  # two targets
SCALE_DENOM = max(1, NT * N)            # = 20
K_SQRT_GUARD = 1.0                      # m^2 guard inside the sqrt

# P1 prefix headings committed for stages k=0,1,2 (the "already sent to L4" tail).
PREFIX_PSI = [0.1, 0.2, 0.3]

# ---- P3 slack penalty weights (exact-penalty lever, T3). ----
# Zl = small quadratic regularizer; zl = LARGE linear weight -> L1 exact-penalty
# (slack = 0 when CPA feasible). Zu/zu: upper side not softened meaningfully
# (CPA one-sided >= 0; Zu=Zl keeps su pinned at 0, zu=0 no linear pull).
ZL = np.array([1.0e2])
ZL_LIN = np.array([1.0e3])
ZU = np.array([1.0e2])
ZU_LIN = np.array([0.0])

# Per-stage parameter layout (np = 10).
NP_P1P4 = 3                        # cpa_act, ppsi_pre, pact_pre
NP_PER_TARGET = 3                  # txdrift, tydrift, tw
NP_P2 = 1 + NP_PER_TARGET * NT     # disc + per-target (drift,drift,tw) = 7
NP = NP_P1P4 + NP_P2               # = 10

# Param indices (document the layout; the runner indexes identically).
I_CPA_ACT = 0
I_PPSI_PRE = 1
I_PACT_PRE = 2
I_DISC = 3
I_TGT0 = 4                         # start of target A block


def main():
    ocp = build_base_ocp(N=N, DT=DT, target_x=200.0, target_y=0.0,
                         cpa_hard=CPA_SAFE)  # dynamics + CPA-h (nh=1) + box +
    #                                              EXACT hessian + MERIT_BACKTRACKING;
    #                                              NO cost, NO slack yet.
    model = ocp.model
    # Override base model name so generated artifacts (header
    # acados_solver_<name>.h, lib libacados_ocp_solver_<name>.so) match this
    # task's solver name.
    model.name = SOLVER_NAME

    # ---- cost_scaling = ones(N+1) (CRITICAL for the EXTERNAL cost, T2 finding).
    # acatos's DEFAULT cost_scaling = [DT,...,DT,1] multiplies each path-stage
    # external cost by DT=5 (approximates a continuous-time integral). The
    # production J_colreg is a discrete UNGATED sum (no dt weight), so each stage
    # must be weighted by 1.0. Without this the acatos cost is ~5x the hand-
    # computed lumped -- the equivalence bug this line closes. ----
    ocp.solver_options.cost_scaling = np.ones(N + 1)

    # ---- Parameters: p = [cpa_act, ppsi_pre, pact_pre,
    #                      disc, txdrift_A, tydrift_A, tw_A,
    #                      txdrift_B, tydrift_B, tw_B]  (np=10).
    #      Combines P1+P4 (first 3) and P2 (last 7). The ONLY per-stage lever
    #      acatos gives; all stage-dependence (schedule switches + target drift +
    #      TCPA discount) is injected here. ----
    cpa_act = ca.SX.sym("cpa_act")       # P4: CPA activation (1=hard, 0=off)
    ppsi_pre = ca.SX.sym("ppsi_pre")     # P1: prefix heading target
    pact_pre = ca.SX.sym("pact_pre")     # P1: prefix activation (1=eq, 0=free)
    p_disc = ca.SX.sym("disc")           # P2: TCPA discount
    p_syms = [cpa_act, ppsi_pre, pact_pre, p_disc]
    for t in range(NT):
        p_syms.append(ca.SX.sym(f"txdrift_{t}"))
        p_syms.append(ca.SX.sym(f"tydrift_{t}"))
        p_syms.append(ca.SX.sym(f"tw_{t}"))
    model.p = ca.vertcat(*p_syms)
    # Default parameter_values (stage 0 of a forward-seeded trajectory): CPA
    # schedule-softened (cpa_act=0), prefix EQ binds (pact_pre=1, ppsi=0.1),
    # disc_0=1, target A at (200,0) tw=0.8, target B at (50,150) tw=0.5.
    ocp.parameter_values = np.array([
        0.0, PREFIX_PSI[0], 1.0,         # P1+P4: cpa_act, ppsi_pre, pact_pre
        1.0,                             # P2: disc_0 = exp(0) = 1
        200.0, 0.0, 0.8,                 # P2: target A drift+tW
        50.0, 150.0, 0.5,                # P2: target B drift+tW
    ])

    # ---- Rebuild con_h_expr as the 2-row composition. The base set
    #      con_h_expr = vertcat(g_cpa) (nh=1) with the target/radius baked in.
    #      Replace it entirely so BOTH rows are parametric. ----
    px = model.x[0]
    py = model.x[1]
    psi = model.x[2]
    # g_cpa residual (matches build_base_ocp: cpa_hard=100, target (200,0)).
    g_cpa = (px - 200.0) ** 2 + (py - 0.0) ** 2 - CPA_SAFE ** 2
    # Row 0: CPA suffix-hard (P4 activation). cpa_act=1 -> binds g_cpa>=0;
    #        =0 -> row identically 0 (disabled / schedule-softened).
    h_cpa = cpa_act * g_cpa
    # Row 1: prefix equality (P1 activation). pact_pre=1 -> binds psi==ppsi_pre;
    #        =0 -> row identically 0 (psi free).
    h_prefix = pact_pre * (psi - ppsi_pre)
    model.con_h_expr = ca.vertcat(h_cpa, h_prefix)  # nh 1 -> 2

    # ---- Reset h bounds to length nh=2 (acatos make_consistent requires the
    #      dimension match once con_h_expr was replaced). Stage-UNIFORM:
    #      row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0<=h<=0. The stage-
    #      dependence is carried by `p`, NOT by these bounds. ----
    nh = model.con_h_expr.rows()
    ocp.constraints.lh = np.array([0.0, 0.0])
    ocp.constraints.uh = np.array([UH_INF, 0.0])  # F2: bounded uh, not np.inf
    ocp.constraints.lh0 = np.array([0.0, 0.0])
    ocp.constraints.uh0 = np.array([UH_INF, 0.0])

    # ---- P3: soft constraint. Soften CPA row 0 (idxsh stage-uniform). Adds a
    #      per-stage lower-slack xi_k for the CPA row. Lower-side exact-penalty
    #      via zl; upper side Zu pins su at 0 (CPA is one-sided >= 0). The slack
    #      coexists with the P4 activation factor on the SAME row -- see the
    #      CRITICAL composition note in the module docstring. ----
    ocp.constraints.idxsh = np.array([0])
    ocp.cost.Zl = ZL
    ocp.cost.zl = ZL_LIN
    ocp.cost.Zu = ZU
    ocp.cost.zu = ZU_LIN

    # ---- P2: per-stage EXTERNAL cost (the J_colreg per-stage form, VERBATIM
    #      from T2). cost_expr_ext_cost is reused at every stage 1..N-1; stage 0
    #      gets cost_expr_ext_cost_0 (same form); terminal expr = 0 so acatos
    #      total == sum_{k=0}^{N-1} (matches the production lumped N-term sum). ----
    def stage_cost_expr():
        cost = 0.0
        for t in range(NT):
            base = I_TGT0 + NP_PER_TARGET * t
            txdrift = model.p[base + 0]
            tydrift = model.p[base + 1]
            tw = model.p[base + 2]
            dx = px - txdrift
            dy = py - tydrift
            d_t = ca.sqrt(dx * dx + dy * dy + K_SQRT_GUARD)   # kSqrtGuard=1.0
            barrier = ca.exp(-ZETA * (d_t - CPA_SAFE))
            cost = cost + tw * p_disc * barrier
        return cost / SCALE_DENOM

    ext_cost = stage_cost_expr()
    ocp.cost.cost_type = "EXTERNAL"
    model.cost_expr_ext_cost = ext_cost
    ocp.cost.cost_type_0 = "EXTERNAL"
    model.cost_expr_ext_cost_0 = ext_cost
    # Terminal = 0 so acatos total == sum_{k=0}^{N-1} stage (matches production).
    ocp.cost.cost_type_e = "EXTERNAL"
    model.cost_expr_ext_cost_e = ca.SX(0.0)

    # ---- Export C code. ----
    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"MERGED GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"COMPOSITION: con_h_expr = vertcat(cpa_act*g_cpa, "
          f"pact_pre*(psi-ppsi_pre))  nh={nh}")
    print(f"  uniform lh=[0,0] uh=[UH_INF,0]; idxsh=[0] (soften CPA row)")
    print(f"  Zl={ZL[0]:.0e} zl={ZL_LIN[0]:.0e} Zu={ZU[0]:.0e} zu={ZU_LIN[0]:.0e}")
    print(f"PARAM LAYOUT: p=[cpa_act, ppsi_pre, pact_pre, disc, "
          f"{{txdrift,tydrift,tw}} x {NT}]  np={NP}")
    print(f"SCHEDULE: prefix-EQ(k<{K_PREFIX}) UNION cpa-suffix-hard(k>={CPA_HARD_FROM_K})")
    print(f"  k<{K_PREFIX}: cpa_act=0 (SOFT), pact_pre=1 (EQ), "
          f"ppsi_pre=prefix_psi[k]")
    print(f"  k>={CPA_HARD_FROM_K}: cpa_act=1 (HARD), pact_pre=0 (FREE), "
          f"ppsi_pre=0")
    print(f"COST: EXTERNAL J_colreg per-stage, cost_scaling=ones({N+1}), "
          f"scale_denom={SCALE_DENOM}, terminal=0")
    print(f"PREFIX_PSI(k<{K_PREFIX})={PREFIX_PSI}")


if __name__ == "__main__":
    main()
