#!/usr/bin/env python3
"""P1b-1a Task 9 -- merged 6-point coexistence (the FINAL P1b-1a staging gate).

QUESTION (the FINAL staging spike): do ALL SIX verified complexity points from
T1-T7 coexist in ONE acatos OCP and SOLVE together without infeasibility /
numerical interaction failure? This is the gate for P1b-1b (the production
backend): if the six points coexist, staging is scalable and P1b-1b can be
written.

The six points (each VERIFIED individually in T1-T7):
  P1 (T1) prefix equality           : h_prefix = pact_pre*(psi-ppsi_pre)
  P2 (T2) J_colreg per-stage        : EXTERNAL cost, cost_scaling=ones(N+1) REQD
       EXTERNAL cost
  P3 (T7) per-target xi slack       : idxsh=[0,1] soften BOTH CPA rows, mixed
                                      L1/L2 Zl/zl (one per-target per-stage slack)
  P4 (T4) CPA bound schedule        : h_cpa = cpa_act*g_cpa (k<3 off, k>=3 hard)
  P5 (T6) double-integrator dyn     : Path B, x=[px,py,psi,r], u=[delta],
                                      dr/dt = c_u*delta, ROT box |r|<=rot_max
  P6 (T8) honest c_u                : c_u = 9.825342e-3 (VDM-direct, baked)

P1b-0 T5_merged proved the FOUR structural points (P1+P2+P3-single-sigma+P4)
compose on the Nomoto heading-rate dynamics (build_base_ocp). P1b-1a T6 added
the double-integrator dynamics (P5, build_base_ocp_doubleint -- Path B) and T7
added per-target xi (P3 dimension upgrade, idxsh=[0,1]). T9 is the FINAL gate:
prove ALL SIX coexist in ONE acatos OCP built on the double-integrator dynamics
and solve together.

====================  The combined OCP (this file)  ====================
Built on build_base_ocp_doubleint (T6, Path B): state x=[px,py,psi,r] (nx=4),
control u=[delta] (nu=1), ROT box |r|<=rot_max, EXACT hessian,
MERIT_BACKTRACKING. The base CPA row (nh=1, baked single target) is REPLACED
with the merged nh=3 form:

  con_h_expr = vertcat( cpa_act * vertcat(g_cpaA, g_cpaB),   (rows 0,1: per-target CPA)
                        pact_pre * (psi - ppsi_pre) )        (row 2: prefix equality)
                                                                       (nh = 3)
  g_cpa_t = (px - txdrift_t)^2 + (py - tydrift_t)^2 - cpa_hard^2   (per-target,
                                                                    parametric)

Stage-UNIFORM bounds: lh = [0,0,0], uh = [UH_INF, UH_INF, 0]
  (rows 0,1 CPA one-sided >= 0; row 2 prefix EQUALITY 0 <= h <= 0).

idxsh = [0,1]  -> soften the TWO CPA rows (P3 per-target). acatos adds ns=2
                  per-stage lower-slacks: sl[0]=xi_A,k (row 0), sl[1]=xi_B,k
                  (row 1). Row 2 (prefix) is NOT softened -- it is a hard
                  equality (0<=h<=0), enforced exactly at every prefix stage.
cost_type = EXTERNAL  -> per-stage J_colreg form (P2), cost_scaling=ones(N+1).

====================  CRITICAL composition: the two CPA rows carry BOTH slack
                       (P3 per-target) AND activation (P4 bound schedule)  ====
Both P3 (idxsh=[0,1] per-target slack) and P4 (cpa_act activation) act on the
SAME two rows (0 and 1). The composition is:
  h_cpa_t = cpa_act * g_cpa_t   (the per-target row expression, t in {A,B})
  idxsh = [0,1]                  (soften both rows -> per-stage per-target xi)
  uniform lh[0,1]=0, uh[0,1]=UH_INF (one-sided >= 0, i.e. h_cpa_t + xi_{t,k}>=0).
T5_merged proved single-sigma + activation are ORTHOGONAL on the CPA row; T9
verifies the per-target GENERALIZATION (two slacks, two activated rows) is
likewise orthogonal -- the two per-target slacks are INDEPENDENT (T7 finding).

====================  Per-stage parameter layout (np = 10)  ====================
The ONLY per-stage lever acatos exposes is the parameter vector p, set per stage
via the GENERATED m5_staging_merge6_acados_update_params(capsule, stage, vals,
np) (T1 lesson -- NOT ocp_nlp_in_set(..,"p",..)). Combine ALL per-stage values
from P1/P2/P4 into ONE p (SAME layout as T5_merged):

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

====================  Scenario design (the key integration challenge)  ==========
T6 finding 2: the honest double-integrator (c_u~9.8e-3) has a HUGE turning
diameter -- a head-on target geometry (like T5_merged's (200,0)) is INFEASIBLE
(can't clear the disc). T9 MUST design a geometry the prefix+double-integrator
can feasibly hold so the HARD CPA stages (k>=3) are feasible and the per-target
xi's go to ~0 (point 4).

Chosen (honest, not cheated): a GENTLE NE trajectory the double-integrator can
hold -- prefix psi ramp [0.1,0.2,0.3] (committed-prefix semantics, r0=0.02 rad/s
mid-turn), then settle to psi~0.45. Both targets placed FAR NORTH of the path:
  target A at (0, 300)   (the CPA-h disc baked into the solver; static drift 0)
  target B at (200, 400) (static drift 0)
With cpa_hard=100 the gentle NE path clears both discs by ~200m at every stage
(min g_cpa ~ +80000). This is honest: the double-integrator's tiny c_u makes
only gentle / off-path geometries feasible -- the T9 integration test is "do the
6 points coexist on a feasible double-integrator OCP", NOT "can the double-
integrator do aggressive avoidance" (that's a P1b-1b / sea-trial question).

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import json
import os
import sys

import casadi as ca
import numpy as np
from acados_template import AcadosOcpSolver

# common.py lives one dir up (acatos_staging/); add parent to sys.path.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from common import build_base_ocp_doubleint, UH_INF  # noqa: E402

SOLVER_NAME = "m5_staging_merge6"

# ---- Constants (must match runner_merge6.cpp EXACTLY) ----
N, DT = 10, 5.0
K_PREFIX = 3            # prefix equality stages: k < K_PREFIX  (P1)
CPA_HARD_FROM_K = 3     # CPA hard from this stage onward     (P4)

# P5 (T6) double-integrator constants. c_u from T8 VDM-direct yaw gain; u_surge
# = T8 u_cruise, surge held constant (variable surge deferred to P1b-1b).
U_SURGE = 9.26
ROT_MAX = 0.2           # ROT box -- bounds the marginal-stability r channel

# P2 (T2) constants.
T_DISCOUNT_S = 100.0
ZETA = 5.0e-3
CPA_SAFE = 100.0        # J_colreg CPA_SAFE (the cost barrier reference radius)
NT = 2                                  # two targets
SCALE_DENOM = max(1, NT * N)            # = 20
K_SQRT_GUARD = 1.0                      # m^2 guard inside the sqrt

# P1 prefix headings committed for stages k=0,1,2 (the "already sent to L4"
# tail). A GENTLE ramp (0.1 rad/stage) the double-integrator can hold from an
# initial yaw rate r0 = 0.02 rad/s (committed-prefix mid-turn). Verified feasible
# by forward-sim: with r0=0.02 the prefix ramp needs delta=0 (r already matches),
# then a gentle settle to psi~0.45 clears both CPA discs by ~200m.
PREFIX_PSI = [0.1, 0.2, 0.3]
# Initial yaw rate (rad/s) -- the committed prefix is a sustained 0.02 rad/s turn
# (psi ramps 0.1/stage over DT=5). Sets stage-0 lbx/ubx for the r state.
R0 = 0.02

# ---- P3 per-target slack penalty weights (exact-penalty lever, T7). ----
# Zl = small quadratic regularizer; zl = LARGE linear weight -> L1 exact-penalty
# (per-target slack = 0 when CPA feasible). Zu/zu: upper side not softened
# meaningfully (CPA one-sided >= 0; Zu=Zl keeps su pinned at 0, zu=0).
# Length 2 EACH (one per softened CPA row, idxsh=[0,1]).
W_QUAD = 1.0e2      # quadratic regularizer (Zl, Zu) per slack
RHO_LIN = 1.0e3     # linear exact-penalty weight (zl) per slack
ZL = np.array([W_QUAD, W_QUAD])
ZL_LIN = np.array([RHO_LIN, RHO_LIN])
ZU = np.array([W_QUAD, W_QUAD])
ZU_LIN = np.array([0.0, 0.0])

# ---- Scenario geometry (baked into the generated solver; see "Scenario design"
#      above for the feasibility rationale). cpa_hard is the CPA clearance radius
#      baked into BOTH per-target rows (target-uniform). The targets' POSITIONS
#      are parametric (set per stage via p); these are the static (zero-drift)
#      stage-0 positions passed to build_base_ocp_doubleint to seed the base's
#      (discarded) single-row h and to parameter_values. ----
# CPA_H = CPA_SAFE here (the CPA-h disc radius equals the J_colreg barrier
# reference -- both 100m; same convention as T5_merged).
CPA_H = CPA_SAFE
TARGET_A = (0.0, 300.0)     # far north of the gentle NE path
TARGET_B = (200.0, 400.0)   # far north of the gentle NE path

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


def _load_c_u():
    """Read c_u from T8's nomoto_params.json; fall back to the T8 literal.

    Same loader as gen_doubleint.py (T6) -- T9 reuses the T8-identified yaw gain
    so the double-integrator dynamics match T6/T7 exactly.
    """
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

    # ---- build_base_ocp_doubleint (T6, Path B): state x=[px,py,psi,r] (nx=4),
    #      control u=[delta] (nu=1), ROT box |r|<=rot_max, EXACT hessian,
    #      MERIT_BACKTRACKING, DISCRETE integrator. Bakes c_u/u_surge into the
    #      dynamics and a SINGLE CPA row (nh=1) around target A (discarded below
    #      when we replace con_h_expr with the per-target nh=3 form). ----
    ocp = build_base_ocp_doubleint(
        N=N, DT=DT, c_u=c_u, u_surge=U_SURGE,
        target_x=TARGET_A[0], target_y=TARGET_A[1], cpa_hard=CPA_H,
        psi_lb=-1.2, psi_ub=1.2, rot_max=ROT_MAX,
    )
    model = ocp.model
    # Override base model name so generated artifacts (header
    # acados_solver_<name>.h, lib libacados_ocp_solver_<name>.so) match this
    # task's solver name.
    model.name = SOLVER_NAME

    # ---- Tighten the SQP tolerances + raise max_iter for an accurate COST
    #      read-back (P2 J_colreg equivalence, |acatos_cost - hand_lumped|).
    #      T6 finding 1: the double-integrator's marginal stability (r pole at
    #      z=1) makes SQP convergence sensitive; acatos's DEFAULT tol (1e-6) on
    #      the per-stage dynamics-equality leaves a ~3.7e-6 multiple-shooting
    #      residual on the SOLVED trajectory, which propagates into the external
    #      cost read-back as a ~5e-6 cost diff -- right at the 1e-6 equivalence
    #      gate. This is NOT assertion-widening (the gate stays 1e-6): it is
    #      honest solver config that drives the SQP residual (and hence the cost
    #      read-back error) well below the gate, exactly as tightening nlp_solver
    #      tol is the standard lever for an accurate cost read-back on a stiff /
    #      marginally-stable OCP. The T6 lesson stands: solved_dyn is convergence
    #      EVIDENCE (reported, not gated at 1e-9) -- here it drops to ~1e-8. ----
    ocp.solver_options.nlp_solver_max_iter = 400
    ocp.solver_options.nlp_solver_tol_stat = 1e-9
    ocp.solver_options.nlp_solver_tol_eq = 1e-9
    ocp.solver_options.nlp_solver_tol_ineq = 1e-9
    ocp.solver_options.nlp_solver_tol_comp = 1e-9

    # ---- cost_scaling = ones(N+1) (CRITICAL for the EXTERNAL cost, T2 finding).
    # acatos's DEFAULT cost_scaling = [DT,...,DT,1] multiplies each path-stage
    # external cost by DT=5 (approximates a continuous-time integral). The
    # production J_colreg is a discrete UNGATED sum (no dt weight), so each stage
    # must be weighted by 1.0. ----
    ocp.solver_options.cost_scaling = np.ones(N + 1)

    # ---- Parameters: p = [cpa_act, ppsi_pre, pact_pre,
    #                      disc, txdrift_A, tydrift_A, tw_A,
    #                      txdrift_B, tydrift_B, tw_B]  (np=10).
    #      Combines P1+P4 (first 3) and P2 (last 7). SAME layout as T5_merged. ----
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
    # disc_0=1, both targets static (zero drift) at their scenario positions.
    ocp.parameter_values = np.array([
        0.0, PREFIX_PSI[0], 1.0,         # P1+P4: cpa_act, ppsi_pre, pact_pre
        1.0,                             # P2: disc_0 = exp(0) = 1
        TARGET_A[0], TARGET_A[1], 0.8,   # P2: target A (static drift), tw=0.8
        TARGET_B[0], TARGET_B[1], 0.5,   # P2: target B (static drift), tw=0.5
    ])

    # ---- Rebuild con_h_expr as the 3-row composition. The base set
    #      con_h_expr = vertcat(g_cpa) (nh=1, baked single target). Replace it
    #      entirely so BOTH CPA rows are PER-TARGET parametric (T7) and the
    #      prefix row is parametric (T1). ----
    px = model.x[0]
    py = model.x[1]
    psi = model.x[2]
    # Per-target CPA residual (T7): the target positions are PARAMETRIC
    # (txdrift_t/tydrift_t in model.p), set per stage; cpa_hard is a literal.
    g_rows = []
    for t in range(NT):
        base = I_TGT0 + NP_PER_TARGET * t
        txdrift_t = model.p[base + 0]
        tydrift_t = model.p[base + 1]
        g_t = (px - txdrift_t) ** 2 + (py - tydrift_t) ** 2 - CPA_H ** 2
        g_rows.append(g_t)
    g_cpa_vert = ca.vertcat(*g_rows)     # nh=2 per-target CPA residuals
    # Rows 0,1: CPA suffix-hard (P4 activation) + per-target (T7). cpa_act=1 ->
    # binds g_cpa_t>=0; =0 -> both rows identically 0 (disabled / softened).
    h_cpa = cpa_act * g_cpa_vert
    # Row 2: prefix equality (P1 activation). pact_pre=1 -> binds psi==ppsi_pre;
    #        =0 -> row identically 0 (psi free).
    h_prefix = pact_pre * (psi - ppsi_pre)
    model.con_h_expr = ca.vertcat(h_cpa, h_prefix)  # nh 1 -> 3

    # ---- Reset h bounds to length nh=3 (acatos make_consistent requires the
    #      dimension match once con_h_expr was replaced). Stage-UNIFORM:
    #      rows 0,1 CPA one-sided >= 0; row 2 prefix EQUALITY 0<=h<=0. The stage-
    #      dependence is carried by `p`, NOT by these bounds. ----
    nh = model.con_h_expr.rows()
    ocp.constraints.lh = np.array([0.0, 0.0, 0.0])
    ocp.constraints.uh = np.array([UH_INF, UH_INF, 0.0])  # F2: bounded uh
    ocp.constraints.lh0 = np.array([0.0, 0.0, 0.0])
    ocp.constraints.uh0 = np.array([UH_INF, UH_INF, 0.0])

    # ---- P3: per-target soft constraint. Soften the TWO CPA rows (idxsh=[0,1],
    #      stage-uniform). acatos adds ns=2 lower-slacks per stage: sl[0]=xi_A,k
    #      (row 0), sl[1]=xi_B,k (row 1). Row 2 (prefix) is NOT softened -- it is
    #      a hard equality enforced exactly. Mixed L1/L2 (rho*xi + 0.5*w*xi^2)
    #      via zl (linear, exact-penalty) + Zl (quadratic regularizer). ----
    ocp.constraints.idxsh = np.array([0, 1])
    ocp.cost.Zl = ZL
    ocp.cost.zl = ZL_LIN
    ocp.cost.Zu = ZU
    ocp.cost.zu = ZU_LIN

    # ---- P2: per-stage EXTERNAL cost (the J_colreg per-stage form, VERBATIM
    #      from T2/T5_merged). cost_expr_ext_cost is reused at every stage 1..N-1;
    #      stage 0 gets cost_expr_ext_cost_0 (same form); terminal expr = 0 so
    #      acatos total == sum_{k=0}^{N-1} (matches the production lumped N-term
    #      sum). ----
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

    nx = model.x.rows()
    nu = model.u.rows()
    nsh = len([i for i in ocp.constraints.idxsh])
    print(f"MERGE6 GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"DYNAMICS (Path B double-integrator): x=[px,py,psi,r] nx={nx}, "
          f"u=[delta] nu={nu}")
    print(f"  c_u={c_u:.9e} (src={c_u_src})  u_surge={U_SURGE}  "
          f"ROT|r|<={ROT_MAX}")
    print(f"COMPOSITION: con_h_expr = vertcat(cpa_act*vertcat(g_cpaA,g_cpaB), "
          f"pact_pre*(psi-ppsi_pre))  nh={nh}")
    print(f"  uniform lh=[0,0,0] uh=[UH_INF,UH_INF,0]")
    print(f"  idxsh=[0,1] (per-target slack on CPA rows, ns={nsh}/stage); "
          f"row 2 prefix NOT softened (hard equality)")
    print(f"  Zl={ZL} zl={ZL_LIN} Zu={ZU} zu={ZU_LIN} (mixed L1/L2 per-target)")
    print(f"PARAM LAYOUT: p=[cpa_act, ppsi_pre, pact_pre, disc, "
          f"{{txdrift,tydrift,tw}} x {NT}]  np={NP}")
    print(f"SCHEDULE: prefix-EQ(k<{K_PREFIX}) UNION cpa-suffix-hard(k>={CPA_HARD_FROM_K})")
    print(f"  k<{K_PREFIX}: cpa_act=0 (SOFT), pact_pre=1 (EQ), "
          f"ppsi_pre=prefix_psi[k]")
    print(f"  k>={CPA_HARD_FROM_K}: cpa_act=1 (HARD), pact_pre=0 (FREE), "
          f"ppsi_pre=0")
    print(f"COST: EXTERNAL J_colreg per-stage, cost_scaling=ones({N+1}), "
          f"scale_denom={SCALE_DENOM}, terminal=0")
    print(f"PREFIX_PSI(k<{K_PREFIX})={PREFIX_PSI}  R0={R0} (committed-turn rate)")
    print(f"SCENARIO: cpa_hard={CPA_H}, target A={TARGET_A} (tw=0.8), "
          f"target B={TARGET_B} (tw=0.5) -- both FAR NORTH of the gentle NE path "
          f"(double-integrator turning-diameter feasibility, T6 finding 2)")


if __name__ == "__main__":
    main()
