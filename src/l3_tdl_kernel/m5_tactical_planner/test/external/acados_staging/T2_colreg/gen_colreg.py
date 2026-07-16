#!/usr/bin/env python3
"""P1b-0 T2 -- J_colreg per-stage EXTERNAL numeric equivalence (acatos spike).

QUESTION (the spike): does the production NLP's LUMPED COLREGs avoidance cost
J_colreg (one CasADi MX summing over all targets x steps) map to acatos's
PER-STAGE EXTERNAL cost with NUMERIC EQUIVALENCE (<1e-6)? This validates that
P1b-1 can faithfully migrate the full cost from a single lumped MX to per-stage
external cost expressions without numeric drift.

VERDICT (resolved by runner_colreg.cpp): the lumped -> staged mapping reproduces
the SAME number to <1e-6, via per-stage parameters that carry everything that
is k-dependent (the TCPA discount disc_k, the target drift tx+tdx*(k*dt),
ty+tdy*(k*dt), and the per-target range-ramp weight tw). The per-stage external
cost expression itself is then k-INDEPENDENT symbolically (acatos reuses the
same cost_expr_ext_cost at every stage); all stage-dependence is injected
through the parameter vector p, which acatos sets independently per stage.

====================  EXACT production J_colreg form (VERBATIM)  ====================
Reproduced verbatim from formulation.cpp:344-393. The plan's pseudocode
SIMPLIFIED the target kinematics (tx + tc*ts*k*dt) -- that is WRONG; the exact
production form is the tdx/tdy drift form below:

    J_colreg = cost / scale_denom
    where:
      scale_denom = max(1, Nt * N)            # Nt=#targets, N=horizon (scalar)
      cost = sum_{t=0..Nt-1} sum_{k=0..N-1}  tw_t * disc_k * barrier_{t,k}
      disc_k  = exp(-(k * dt_s) / t_discount_s)        # PER-STEP constant
      barrier_{t,k} = exp(-zeta * (d_{t,k} - cpa_safe))
      d_{t,k} = sqrt(dx^2 + dy^2 + 1.0)                  # kSqrtGuard = 1.0
      dx = x_own[k] - (tx + tdx * (k*dt_s))
      dy = y_own[k] - (ty + tdy * (k*dt_s))
      tdx = ts * cos(tc)                                # target velocity x-comp
      tdy = ts * sin(tc)                                # target velocity y-comp
      # own-ship cumulative position x_own[k], y_own[k] is the integrated state
      # at stage k (acatos gives the integrated state per stage as model.x).

Constants: dt_s=DT=5.0, N=10, t_discount_s=100.0, zeta=5.0e-3, cpa_safe=100.0,
Nt=2, kSqrtGuard=1.0  ->  scale_denom = max(1, 2*10) = 20.

====================  Param encoding (DOCUMENTED choice)  ====================
Per-stage parameter vector p_k (NP = 1 + 3*Nt = 7 for Nt=2):

    p_k = [ disc_k,
            txdrift_A, tydrift_A, tw_A,     # target A at stage k
            txdrift_B, tydrift_B, tw_B ]    # target B at stage k

where, for target t with raw params (tx,ty,tc,ts,tw) at stage k:
    txdrift_tk = tx + ts*cos(tc)*(k*DT)     # = tx + tdx*(k*dt_s),  tdx=ts*cos(tc)
    tydrift_tk = ty + ts*sin(tc)*(k*DT)     # = ty + tdy*(k*dt_s),  tdy=ts*sin(tc)

i.e. the k-dependent target drift is FOLDED INTO the param (precomputed in the
runner for each stage k). disc_k = exp(-(k*DT)/t_discount_s) is also a param
(it is the per-step TCPA discount). tw_t is the range-ramp weight (0..1).

The symbolic stage cost expression is then k-INDEPENDENT:
    cost_expr_ext_cost =
        (1/scale_denom) * sum_t  p_tw_t * p_disc * exp(-zeta*(d_t - cpa_safe))
    where  d_t = sqrt( (px - p_txdrift_t)^2 + (py - p_tydrift_t)^2 + 1.0 )

The same expression is reused at every stage 1..N-1, at stage 0
(cost_expr_ext_cost_0), and at the terminal N (cost_expr_ext_cost_e). acatos
total objective = sum_{k=0}^{N-1} stage_expr_k + terminal_expr  -- which is
EXACTLY the production lumped cost (the sum over k=0..N-1 of per-stage terms).
The runner sets per-stage p so the staged sum equals the hand-computed lumped
J_colreg (computed over the SAME solved trajectory with the SAME params).

NOTE on stage range: production sums k=0..N-1 (N terms). acatos EXTERNAL has N
path-stage exprs (stages 0..N-1) PLUS one terminal expr (stage N). To match the
production sum EXACTLY (N terms, k=0..N-1), the terminal cost_expr_ext_cost_e
is set to ZERO -- otherwise we'd add a spurious Nth term. This is documented
and consistent between the acatos expr and the hand-check (hand-check sums
k=0..N-1 only). terminal=0 -> acatos_total == sum_{k=0}^{N-1} stage_expr_k.

====================  CPA h-constraint (KEPT)  ====================
The base CPA nonlinear path constraint (g_cpa >= 0 around target A) is KEPT
as a feasibility aid so the solver returns a realistic avoidance trajectory.
T2's assertion is purely the COST NUMBER EQUIVALENCE on whatever trajectory
the solver returns -- the CPA h just makes the trajectory physical. (Per the
brief: "KEEP the CPA h ... the assertion is purely about the cost number
equivalence.")

NOT production NLP code. spike/external only. IPOPT path untouched.
"""
import os
import sys

import casadi as ca
import numpy as np
from acados_template import AcadosOcpSolver

# common.py lives one dir up (acatos_staging/); add parent to sys.path.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from common import build_base_ocp  # noqa: E402

SOLVER_NAME = "m5_staging_colreg"

# ---- Constants (brief; match runner_colreg.cpp exactly) ----
N = 10
DT = 5.0
T_DISCOUNT_S = 100.0
ZETA = 5.0e-3
CPA_SAFE = 100.0
NT = 2                                  # two targets
SCALE_DENOM = max(1, NT * N)            # = 20
K_SQRT_GUARD = 1.0                      # m^2 guard inside the sqrt

# Per-stage parameter layout. p_k = [disc_k, then per target: txdrift,tydrift,tw].
NP_PER_TARGET = 3                        # txdrift_tk, tydrift_tk, tw_t
NP = 1 + NP_PER_TARGET * NT              # = 1 + 6 = 7


def main():
    ocp = build_base_ocp(N=N, DT=DT, target_x=200.0, target_y=0.0,
                         cpa_hard=CPA_SAFE)   # dynamics + CPA-h (nh=1) + box +
    #                                              EXACT hessian + MERIT_BACKTRACKING;
    #                                              NO cost (added below).
    model = ocp.model
    # Override base model name so generated artifacts (header
    # acatos_solver_<name>.h, lib libacatos_ocp_solver_<name>.so) match this
    # task's solver name.
    model.name = SOLVER_NAME

    # ---- cost_scaling = ones(N+1) (CRITICAL for the numeric equivalence).
    # acatos's DEFAULT cost_scaling = np.append(time_steps, 1.0) = [DT,...,DT,1]
    # = [5,...,5,1], which MULTIPLIES each path-stage external cost by DT=5.
    # That default approximates a continuous-time INTEGRAL (sum*dt). The
    # production J_colreg is a discrete UNGATED sum (no dt weight):
    #   cost = sum_t sum_k tw_t*disc_k*barrier_{t,k}
    # so each stage must be weighted by 1.0, not DT. Setting cost_scaling=ones
    # makes acatos evaluate each stage's external cost RAW, so
    #   acatos_total = sum_{k=0}^{N-1} 1*stage_k + 1*0(terminal) == hand_lumped.
    # Without this the acatos cost is 5x (DT) the hand-computed lumped -- the
    # equivalence bug this line closes. (Per the brief: be CONSISTENT between
    # the acatos expr and the hand-computed lumped; both are un-dt-weighted.)
    ocp.solver_options.cost_scaling = np.ones(N + 1)

    # ---- Parameters: p = [disc, txdrift_A, tydrift_A, tw_A,
    #                      txdrift_B, tydrift_B, tw_B]  (NP=7).
    #      disc is the per-stage TCPA discount exp(-(k*DT)/T_DISCOUNT_S); the
    #      drift params carry the k-dependent target position tx+tdx*(k*dt);
    #      tw is the per-target range-ramp weight. All k-dependence lives in p
    #      (the ONLY per-stage lever acatos exposes), so the cost expr below is
    #      symbolically identical at every stage. ----
    p_disc = ca.SX.sym("p_disc")
    p = [p_disc]
    for t in range(NT):
        p.append(ca.SX.sym(f"txdrift_{t}"))
        p.append(ca.SX.sym(f"tydrift_{t}"))
        p.append(ca.SX.sym(f"tw_{t}"))
    model.p = ca.vertcat(*p)
    # Default parameter_values (stage 0 of a forward-seeded trajectory):
    #   disc_0 = exp(0) = 1, target A at (200,0), tw=0.8, target B at (50,150), tw=0.5.
    p_default = np.array([1.0, 200.0, 0.0, 0.8, 50.0, 150.0, 0.5])
    ocp.parameter_values = p_default

    px = model.x[0]
    py = model.x[1]

    # ---- Per-stage EXTERNAL cost: the J_colreg per-stage form (VERBATIM).
    #      cost_expr_ext_cost is reused at every stage 1..N-1; stage 0 gets
    #      cost_expr_ext_cost_0; the symbolic form is identical (all
    #      stage-dependence is in p). The terminal expr is 0 so the acatos
    #      total == sum_{k=0}^{N-1} (matches the production lumped N-term sum). ----
    def stage_cost_expr():
        cost = 0.0
        for t in range(NT):
            txdrift = model.p[1 + NP_PER_TARGET * t + 0]
            tydrift = model.p[1 + NP_PER_TARGET * t + 1]
            tw = model.p[1 + NP_PER_TARGET * t + 2]
            dx = px - txdrift
            dy = py - tydrift
            d_t = ca.sqrt(dx * dx + dy * dy + K_SQRT_GUARD)   # kSqrtGuard=1.0
            barrier = ca.exp(-ZETA * (d_t - CPA_SAFE))
            cost = cost + tw * p_disc * barrier
        return cost / SCALE_DENOM

    ext_cost = stage_cost_expr()
    ocp.cost.cost_type = "EXTERNAL"
    model.cost_expr_ext_cost = ext_cost
    # Stage 0 EXTERNAL (acatos uses cost_expr_ext_cost_0 if set, else the
    # generic one). Set it to the SAME expression -- stage 0 is k=0, so its
    # p (disc=1, drift=raw target pos) makes it the k=0 term.
    ocp.cost.cost_type_0 = "EXTERNAL"
    model.cost_expr_ext_cost_0 = ext_cost

    # Terminal cost = 0 so acatos total == sum_{k=0}^{N-1} stage_expr (matches
    # the production lumped sum over k=0..N-1, N terms). A non-zero terminal
    # would add a spurious Nth term that is NOT in the production J_colreg.
    ocp.cost.cost_type_e = "EXTERNAL"
    model.cost_expr_ext_cost_e = ca.SX(0.0)

    # ---- Export C code. ----
    code_export_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    print(f"COLREG GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"PARAM ENCODING: p_k=[disc, {{txdrift,tydrift,tw}} x {NT} targets], "
          f"NP={NP}")
    print(f"CONSTANTS: N={N} DT={DT} t_discount_s={T_DISCOUNT_S} zeta={ZETA} "
          f"cpa_safe={CPA_SAFE} Nt={NT} scale_denom={SCALE_DENOM} "
          f"kSqrtGuard={K_SQRT_GUARD}")
    print(f"TERMINAL cost_expr_ext_cost_e = 0 (so acatos total == "
          f"sum_{{k=0}}^{{N-1}} stage, matching the production N-term lumped sum)")


if __name__ == "__main__":
    main()
