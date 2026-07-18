#!/usr/bin/env python3
"""P1b-1b Task 15 -- production acados OCP codegen (MidMpcAcadosFormulation).

This is the CODEGEN SOURCE OF TRUTH for the production acados backend. It
re-derives in CasADi SX the SAME symbol graph the C++ MidMpcAcadosFormulation
builds in MX (src/mid_mpc/mid_mpc_acados_formulation.cpp). SX/MX are
mathematically equivalent at the acados layer; acados_template SX support is
mature while MX has limits (locked decision, plan §Global Constraints). Both
must stay in lockstep -- the C++ unit test (test_mid_mpc_acados_formulation.cpp)
asserts the dimension/param contract; this script produces the generated C.

====================  Path B 5-dim state (spec amendment 2026-07-17)  ==========
state   x = [px, py, psi, r, u_surge]   (nx = 5)
control u = [delta, n]                  (nu = 2  -- rudder angle + rpm)

Discrete dynamics (DISCRETE integrator, explicit Euler; surge as STATE so the
rpm control has a real dynamics path, per the 2026-07-17 user ruling):
    r[k+1]       = r       + DT*c_u*delta                   (c_u VDM-direct T8)
    psi[k+1]     = psi     + DT*r                           (pre-update r)
    u_surge[k+1] = u_surge + DT*(k_prop*n^2 - k_drag*u_surge^2)/m_sge  (VDM-direct)
    px[k+1]      = px      + u_surge*DT*cos(psi)
    py[k+1]      = py      + u_surge*DT*sin(psi)

Coefficients (VDM-direct literals, NOT invented -- vessel_dynamics_model.cpp:47-48
+ P1b-1a T8 yaw-gain identification):
    c_u    = 9.825342e-3   (= k_n_rudder * u^2 / izz_e at cruise, T8)
    k_prop = 500.0         (vessel_dynamics_model.cpp:47)
    k_drag = 100.0         (vessel_dynamics_model.cpp:48)
    m_sge  = 152250.0      (mass_kg*(1+surge_added_mass_factor) = 145000*1.05;
                            vessel_dynamics_model.cpp:43;
                            capability_manifest.hpp:45,79). T15 F1: surge accel is
                            MASS-NORMALIZED exactly as VDM ground truth (the raw
                            k_prop*n^2-k_drag*u^2 is a FORCE [N], /m_sge -> m/s^2).
                            Without /m_sge the accel was ~152250x too large.

====================  6 costs (mirror IPOPT build_*_cost_)  ====================
EXTERNAL per-stage (cost_scaling = ones(N+1), T2/T9 finding -- acatos DEFAULT
cost_scaling=[DT,...,DT,1] multiplies each path stage by DT, which the
production J_colreg/J_route/J_dist/J_vel/J_asym do NOT want):
    J_colreg   smooth exp barrier per-target (averaged over Nt), disc_k folded
               numerically per stage via the per-stage param block.
    J_dist     (psi - route_bearing)^2
    J_route    route-frame dimensionless cross-track (l/l_scale)^2 * route_weight
    J_vel      (u_surge - planned_speed)^2          <-- uses u_surge STATE (P1b-1b)
    J_asym     give_way * softplus port penalty (C∞ smooth)
    J_terminal terminal wrong-side softplus + upper-band two-sided softplus
               (spec §5.4); applied at terminal stage (cost_type_e).

====================  Constraints (mirror IPOPT, lbx/ubx for ROT/box)  =========
    con_h_expr = vertcat( prefix_psi (1): pact_pre*(psi - prefix_psi_at_k),
                          prefix_u   (1): pact_pre*(u_surge - prefix_u_at_k),
                          CPA per-target (Nt rows, idxsh=[2..2+Nt-1] per-target
                          xi slack, T7/T9 mixed L1/L2 Zl/zl),
                          direction (pref_dir*l[k]),
                          min_alt (pref_dir*(psi-own_psi) - min_alt) )
                          # P4: terminal C10/C11 abolished (long horizon)
    T15 F2: prefix rows come FIRST (right after ROT-via-lbx, mirroring IPOPT's
           [ROT][prefix][CPA]... class order). lh=uh=0 (equality); pact_pre is a
           per-stage activation factor (1.0 for k<K, 0.0 for k>=K) so inactive
           stages get 0==0 (trivially satisfied). This is the committed-route
           prefix lock (the GNC guard inner waypoints L4 is tracking MUST stay
           stable across replans).
    T15 F4: CPA target position is the per-stage drifted target_x_at_k/y_at_k
           (precomputed = tx+sog*cos(cog)*k*dt), matching IPOPT's per-stage drift.
    ROT via lbx/ubx (NOT h): |r| <= rot_max (state idx 3),
                             |psi| <= heading box (state idx 2),
                             u_min <= u_surge <= u_max (state idx 4),
                             |delta| <= delta_max, |n| <= n_max (control).

====================  Param partition (T15 F2/F4 documented deviation)  =========
GLOBAL (np_global = 106, stage-uniform): 26 IPOPT head scalars (kIdx 0-25) +
    16x5 target block (kIdx 62-141, remapped to global 26-105). Slots 0-3
    (own psi/u/x/y) are RESERVED x0 seeds (F3) -- the solver writes them to
    ocp.constraints.x0; they are NOT graph-referenced.
PER-STAGE (np_per_stage = 3 + 2*Nt + 2 = 37 at Nt=16): per-stage scalars stage k
    needs -- [0]prefix_psi_at_k, [1]prefix_u_at_k, [2]pact_pre (F2),
    [3..3+Nt-1]target_x_at_k[t], [3+Nt..3+2Nt-1]target_y_at_k[t] (F4),
    [35]tb_x_at_k, [36]tb_y_at_k (VR-07b T3/T5 per-stage t_b closest-point,
    route + terminal COST origin). Set via the generated
    m5_mid_mpc_acados_acados_update_params(capsule, stage, vals, np).
Why this is no longer exactly 142: IPOPT packs a single flat 142-vector and
    computes per-stage target drift symbolically from global (cog,sog). acatos
    receives a stage-uniform graph that CANNOT index stage k, so per-stage drift
    (F4), the prefix-equality activation factor (F2), and the per-stage t_b
    closest-point (VR-07b T3) are precomputed per-stage and delivered via
    update_params. The GLOBAL block stays 106 (142-compatible for the
    stage-uniform portion); the per-stage block is the documented acatos
    expansion (3 + 2*Nt + 2 = 37 at default Nt=16; NP_TOTAL=143).

====================  Solver opts (locked, P1b-1a T9 cost-read-back)  =========
    FULL_CONDENSING_HPIPM, EXACT hessian (F3), DISCRETE integrator, SQP,
    nlp_solver_tol_*=1e-9 + max_iter=400, MERIT_BACKTRACKING (F4).
    F1 warm-start seed forward-propagated non-zero (solver side, Task 16).
    F2 uh=1e10 (bounded pseudo-infinity; t_renderer rejects JSON Infinity).

References (authoritative staging templates):
    - ../acados_staging/common.py::build_base_ocp_doubleint   dynamics/box/solver
    - ../acados_staging/T9_merge6/gen_merge6.py              6-point merged impl

NOT a unit test. Run inside the sil-nodes container:
    source scripts/a4000-env.sh
    COMPOSE_PROJECT_NAME=codex-acados-backend docker compose -f docker-compose.yml \
        -f docker-compose.a4000.yml run --rm sil-nodes \
        bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend \
                 && python3 gen_mid_mpc_acados.py && ls c_generated_code/"

Production acados backend codegen. IPOPT formulation untouched (read-only ref).
"""
import os

import casadi as ca
import numpy as np
from acados_template import AcadosModel, AcadosOcp, AcadosOcpSolver

SOLVER_NAME = "m5_mid_mpc_acados"

# ---- Horizon / step (production default; matches .cpp kNDefault/kDt). ----
N, DT = 80, 15.0       # P4 N=80 dt=15 1200s (was 18, 5.0; RFC-001 90s→1200s)

# ---- Path B VDM-direct coefficients (mirror .cpp kC_u/kKProp/kKDrag/kMSge). ----
C_U = 9.825342e-3       # T8 VDM-direct yaw gain (rad/s^2 per rad)
K_PROP = 500.0          # vessel_dynamics_model.cpp:47
K_DRAG = 100.0          # vessel_dynamics_model.cpp:48
# T15 F1: surge effective mass m_sge = mass_kg*(1+surge_added_mass_factor)
# = 145000*1.05 = 152250 (vessel_dynamics_model.cpp:43;
# capability_manifest.hpp:45 mass_kg=145000, :79 surge_added_mass_factor=0.05).
# The surge accel is MASS-NORMALIZED exactly as VDM ground truth.
M_SGE = 145000.0 * (1.0 + 0.05)   # 152250.0
K_PROP_PER_MASS = K_PROP / M_SGE  # baked effective coeff (force[N]/mass[kg])
K_DRAG_PER_MASS = K_DRAG / M_SGE  # baked effective coeff

# ---- F2 bounded pseudo-infinity (t_renderer rejects JSON Infinity). ----
UH_INF = 1.0e10

# ---- Targets (production max; matches kAcadosMaxTargets/kMaxTargets). ----
NT = 16

# ---- Cost constants (mirror IPOPT Config defaults). ----
ZETA = 5.0e-3            # exp barrier steepness [1/m]
T_DISCOUNT_S = 100.0     # TCPA discount time constant [s]
PWT_OUTER_M = 11112.0    # range-ramp outer distance [m]
K_SQRT_GUARD = 1.0       # m^2 smooth-sqrt guard
W_COLREG = 30.0
W_DIST = 10.0
W_ROUTE = 3.0
W_VEL = 1.0
K_ASYM = 50.0
ASYM_TAU = 0.0873        # ~5 deg
TERMINAL_TAU = 0.5
TERMINAL_L_MIN_M = 30.0
TERMINAL_L_MAX_M = 400.0
LATERAL_SCALE_M = 400.0  # GncExecutionOdd.max_lateral_offset_m

# ---- P5 T2 transition cost (Eriksen mixed-norm, anti-chattering). ----
W_TRANS = 1.0       # w_trans: global transition weight (relative to collision ~40)
K_DCHI = 2.5        # K_Δχ: heading-change penalty (L2, Eriksen tran_χ)
K_DU = 0.3          # K_ΔU: speed-change penalty (L1, Eriksen tran_U)

# ---- P3 per-target slack penalty (T7/T9 mixed L1/L2 exact-penalty). ----
# Zl = quadratic regularizer, zl = LARGE linear -> L1 exact-penalty (xi=0 when
# CPA feasible). Length Nt (one per softened CPA row).
W_QUAD = 1.0e2           # Zl, Zu per slack
# P5 (2026-07-18 finding): the per-target xi slack is INERT under acatos
# SQP+MERIT_BACKTRACKING for the heavy-infeasibility regime tested (CPA gap
# >= 352m at N=80). Probes: linear-distance h (made xi in metres not m^2) and
# rho=1e6 (raised 1000x) BOTH leave xi ~ 1e-22 (numerical zero) and the
# convergence boundary unchanged. The SQP line search refuses the large
# single-step xi move the constraint would need. IPOPT's filter line-search
# handles this regime. zl=1e3 is retained (the linear-distance h does not
# require a higher rho; rho=1e6 was a probe, not a fix).
RHO_LIN = 1.0e3          # zl per slack
ZL = np.full(NT, W_QUAD)
ZL_LIN = np.full(NT, RHO_LIN)
ZU = np.full(NT, W_QUAD)
ZU_LIN = np.full(NT, 0.0)

# ---- VR-07b T3/T5: Huber route-cost delta_h (literal, mirrors MX .cpp). ----
# The MX side bakes cfg_.huber_delta_h=400.0 as a double into huber_mx_; for
# MX/SX parity the SX side MUST use the SAME literal here (NOT a param).
HUBER_DELTA_H = 400.0

# ---- State/control box defaults (lbx/ubx; node overrides per-cycle). ----
PSI_LB, PSI_UB = -np.pi, np.pi        # heading box (state idx 2)
ROT_MAX = 0.2094                       # |r| <= rot_max (state idx 3), ~12 deg/s
U_SURGE_MIN, U_SURGE_MAX = 0.0, 15.0   # surge box (state idx 4)
DELTA_MAX = 0.4                        # |delta| <= delta_max (control idx 0), rad
N_MIN, N_MAX = 0.0, 12.0              # rpm box (control idx 1), rps

# ---- Parameter partition layout (must match .cpp exactly). T15 F2/F4. ----
NP_GLOBAL_HEAD = 26                    # kGIdx 0-25 (IPOPT kIdx 0-25)
NP_GLOBAL_TARGETS = NT * 5             # 80 (IPOPT kIdx 62-141 remapped)
NP_GLOBAL = NP_GLOBAL_HEAD + NP_GLOBAL_TARGETS   # 106
# Per-stage: [0]prefix_psi_at_k, [1]prefix_u_at_k, [2]pact_pre,
#            [3..3+Nt-1]target_x_at_k[t], [3+Nt..3+2Nt-1]target_y_at_k[t],
#            [35]tb_x_at_k, [36]tb_y_at_k (VR-07b T3/T5 per-stage t_b closest
#            point -- route + terminal COST origin; constraint rows keep global).
PS_PREFIX_PSI_OFF = 0
PS_PREFIX_U_OFF = 1
PS_PACT_PRE_OFF = 2
PS_TGT_DRIFT_OFF = 3                   # target_x_at_k[0] starts here
# VR-07b T3/T5: per-stage t_b slots appended at the END (non-disruptive).
PS_TB_X_OFF = PS_TGT_DRIFT_OFF + 2 * NT   # 3 + 32 = 35 (mirror kAcadosPerStageTbXOff)
PS_TB_Y_OFF = PS_TB_X_OFF + 1             # 36 (mirror kAcadosPerStageTbYOff)
# P5 T2: transition cost per-stage slots (last cycle psi/u for J_transition).
PS_PSI_PREV_OFF = PS_TB_Y_OFF + 1         # 37 (mirror kAcadosPerStagePsiPrevOff)
PS_U_PREV_OFF = PS_PSI_PREV_OFF + 1       # 38 (mirror kAcadosPerStageUPrevOff)
PS_W_TRANS_ACTIVE_OFF = PS_U_PREV_OFF + 1 # 39 (mirror kAcadosPerStageWTransActiveOff)
NP_PER_STAGE = PS_W_TRANS_ACTIVE_OFF + 1  # 3 + 32 + 2 + 2 + 1 = 40 (mirror kAcadosNpPerStageDefault)

# Global head-scalar indices (IDENTICAL to IPOPT kIdx 0-25 + .cpp kGIdx*).
# Slots 0-3 (own psi/u/x/y) are RESERVED x0 seeds (F3): the solver writes them
# to ocp.constraints.x0; they are NOT graph-referenced.
G_PSI0, G_U0, G_X0, G_Y0 = 0, 1, 2, 3   # RESERVED: x0 seed (solver via x0)
G_ROUTE_BEARING, G_PLANNED_SPEED = 4, 5
G_HEADING_MIN, G_HEADING_MAX = 6, 7
G_SPEED_MIN, G_SPEED_MAX = 8, 9
G_CPA_SAFE, G_ROT_MAX, G_OWN_PSI, G_GIVE_WAY = 10, 11, 12, 13
G_RF_OX, G_RF_OY, G_RF_NX, G_RF_NY, G_RF_BRG = 14, 15, 16, 17, 18
G_LAT_SCALE, G_ROUTE_WEIGHT = 19, 20
G_PREFIX_ACTIVE_K, G_PREF_DIR, G_MIN_ALT_RAD, G_ROLE, G_DECEL_MAX = 21, 22, 23, 24, 25
G_TARGETS = NP_GLOBAL_HEAD             # 26 (target block start in global p)


def build_model() -> AcadosModel:
    """Build the CasADi SX model: 5-dim state, 2-dim control, Path B discrete
    dynamics, con_h_expr (prefix-equality + CPA per-target + direction + min_alt
    + terminal), per-stage EXTERNAL cost expressions. Parameter partition:
    global=106 (IPOPT-142-compatible stage-uniform part) + per-stage=37 (T15
    F2/F4 documented acados expansion beyond IPOPT flat 142; +2 for VR-07b T3/T5
    per-stage t_b closest-point origin used by route + terminal COST)."""
    model = AcadosModel()
    model.name = SOLVER_NAME

    # ---- State x=[px,py,psi,r,u_surge], control u=[delta,n]. ----
    px = ca.SX.sym("px")
    py = ca.SX.sym("py")
    psi = ca.SX.sym("psi")
    r = ca.SX.sym("r")
    u_surge = ca.SX.sym("u_surge")
    x = ca.vertcat(px, py, psi, r, u_surge)

    delta = ca.SX.sym("delta")
    n = ca.SX.sym("n")
    u_ctrl = ca.vertcat(delta, n)

    # ---- Parameters: global (106) + per-stage (37, T15 F2/F4 + VR-07b T3 tb). ----
    p_global = ca.SX.sym("p_global", NP_GLOBAL)
    p_stage = ca.SX.sym("p_stage", NP_PER_STAGE)
    # acatos receives a SINGLE param vector per stage (p_global concatenated with
    # that stage's p_stage). The C++ pack_parameters keeps them separate so the
    # solver can update global once + per-stage each cycle; the generated
    # <name>_acados_update_params writes the concatenated vector.
    model.p = ca.vertcat(p_global, p_stage)

    def gslot(i):
        return p_global[i]

    # Per-stage fixed-offset slot accessors (T15 F2/F4). The single-stage graph
    # reads stage k's value at a FIXED offset; pack_parameters writes a different
    # value to that offset in each stage's per-stage vector.
    def prefix_psi_at_k():
        return p_stage[PS_PREFIX_PSI_OFF]

    def prefix_u_at_k():
        return p_stage[PS_PREFIX_U_OFF]

    def pact_pre():
        return p_stage[PS_PACT_PRE_OFF]

    def target_x_at_k(t):
        return p_stage[PS_TGT_DRIFT_OFF + t]

    def target_y_at_k(t):
        return p_stage[PS_TGT_DRIFT_OFF + NT + t]

    # VR-07b T3/T5: per-stage t_b closest-point slots (route + terminal COST
    # origin). The CONSTRAINT rows keep the GLOBAL route origin (C10/C11 P4).
    def tb_x_at_k():
        return p_stage[PS_TB_X_OFF]

    def tb_y_at_k():
        return p_stage[PS_TB_Y_OFF]

    # P5 T2: transition cost per-stage slots (last cycle psi/u for J_transition).
    def psi_prev_at_k():
        return p_stage[PS_PSI_PREV_OFF]

    def u_prev_at_k():
        return p_stage[PS_U_PREV_OFF]

    # P5 T2: transition cost active flag (1.0 when prev solution exists, 0 for first cycle).
    def w_trans_active_at_k():
        return p_stage[PS_W_TRANS_ACTIVE_OFF]

    # ---- Path B discrete dynamics (explicit Euler; surge as STATE). T15 F1:
    #      surge accel MASS-NORMALIZED by m_sge via baked effective coeffs. ----
    disc_dyn = ca.vertcat(
        px + u_surge * DT * ca.cos(psi),
        py + u_surge * DT * ca.sin(psi),
        psi + DT * r,
        r + DT * C_U * delta,
        u_surge + DT * (K_PROP_PER_MASS * n * n - K_DRAG_PER_MASS * u_surge * u_surge),
    )

    # ---- con_h_expr: prefix lock (F2) + CPA per-target (F4 drift) + direction
    #      + min_alt + terminal. Row order mirrors IPOPT [ROT-via-lbx][prefix]
    #      [CPA][direction][min_alt][terminal]; prefix is the first h rows. ----
    # Prefix lock (F2): activation-factor equality. pact_pre=1 for k<K enforces
    # psi==prefix_psi, u_surge==prefix_u; pact_pre=0 for k>=K -> 0==0 (inactive).
    h_prefix_psi = pact_pre() * (psi - prefix_psi_at_k())
    h_prefix_u = pact_pre() * (u_surge - prefix_u_at_k())
    cpa_safe = gslot(G_CPA_SAFE)
    cpa_rows = []
    for t in range(NT):
        # F4: per-stage drifted target position (precomputed in pack_parameters).
        tx_at_k = target_x_at_k(t)
        ty_at_k = target_y_at_k(t)
        dx = px - tx_at_k
        dy = py - ty_at_k
        cpa_rows.append(dx * dx + dy * dy - cpa_safe * cpa_safe)
    # Direction + min_alt (single-stage form; acatos stacks per stage).
    ox = gslot(G_RF_OX)
    oy = gslot(G_RF_OY)
    nx = gslot(G_RF_NX)
    ny = gslot(G_RF_NY)
    pref_dir = gslot(G_PREF_DIR)
    own_psi = gslot(G_OWN_PSI)
    min_alt = gslot(G_MIN_ALT_RAD)
    l_k = (px - ox) * nx + (py - oy) * ny
    h_dir = pref_dir * l_k
    h_min_alt = pref_dir * (psi - own_psi) - min_alt
    con_h = ca.vertcat(h_prefix_psi, h_prefix_u, *cpa_rows, h_dir, h_min_alt)

    model.x = x
    model.u = u_ctrl
    model.disc_dyn_expr = disc_dyn
    model.con_h_expr = con_h
    model.f_expl_expr = disc_dyn   # acatos wants both for some code paths

    # ---- Per-stage EXTERNAL cost (the 6 production costs, single-stage form).
    # The lumped per-stage cost J_stage = w_colreg*J_colreg + w_dist*J_dist +
    # w_route*J_route + w_vel*J_vel + J_asym. J_terminal is cost_type_e
    # (terminal only). disc_k is folded in numerically via p_stage once the
    # solver writes per-stage discount factors (P1b-1c); the symbol form here
    # omits disc_k (constant 1) so the codegen matches the .cpp MX graph.
    # CPA range-ramp weight tw is per-target in p_global (slot base+4).
    # T15 F4: target position is the per-stage drifted position (matches CPA rows
    # and IPOPT's per-stage drift).
    cost_colreg = 0.0
    for t in range(NT):
        base = G_TARGETS + 5 * t
        tw = p_global[base + 4]
        tx_at_k = target_x_at_k(t)
        ty_at_k = target_y_at_k(t)
        dx = px - tx_at_k
        dy = py - ty_at_k
        d_t = ca.sqrt(dx * dx + dy * dy + K_SQRT_GUARD)
        cost_colreg = cost_colreg + tw * ca.exp(-ZETA * (d_t - cpa_safe))
    cost_colreg = cost_colreg / max(1, NT)

    bearing = gslot(G_ROUTE_BEARING)
    planned = gslot(G_PLANNED_SPEED)
    l_scale = gslot(G_LAT_SCALE)
    route_w = gslot(G_ROUTE_WEIGHT)
    give_way = gslot(G_GIVE_WAY)
    role = gslot(G_ROLE)

    cost_dist = (psi - bearing) ** 2
    # VR-07b T3/T5: route cost origin is the PER-STAGE t_b closest point (NOT
    # the global route origin). The route NORMAL (nx,ny) stays global. Loss is
    # Huber(l, delta_h)/l_scale^2 (mirrors MX build_route_cost_ + huber_mx_).
    # l_route uses per-stage tb_x/tb_y; the CONSTRAINT rows below still use the
    # global-origin l_k (C10/C11 deferred to P4 -- intentional asymmetry).
    l_route = (px - tb_x_at_k()) * nx + (py - tb_y_at_k()) * ny
    route_hub = ca.if_else(ca.fabs(l_route) <= HUBER_DELTA_H,
                           0.5 * l_route * l_route,
                           HUBER_DELTA_H * (ca.fabs(l_route) - 0.5 * HUBER_DELTA_H))
    cost_route = route_w * route_hub / (l_scale * l_scale)
    cost_vel = (u_surge - planned) ** 2
    z_asym = (bearing - psi) / ASYM_TAU
    cost_asym = give_way * K_ASYM * ASYM_TAU * ca.log(1.0 + ca.exp(z_asym))

    # P5 T2: transition cost (Eriksen mixed-norm, anti-chattering layer 2).
    # J_transition = w_trans_active * w_trans * (K_Δχ·(ψ-ψ_prev)² + K_ΔU·|u-u_prev|).
    # w_trans_active is 0 for first cycle (no prior trajectory), 1 for subsequent.
    dpsi_trans = psi - psi_prev_at_k()
    du_trans = u_surge - u_prev_at_k()
    cost_transition = (w_trans_active_at_k() * W_TRANS *
                       (K_DCHI * dpsi_trans * dpsi_trans +
                        K_DU * ca.fabs(du_trans)))

    j_stage = (W_COLREG * cost_colreg + W_DIST * cost_dist +
               W_ROUTE * cost_route + W_VEL * cost_vel + cost_asym +
               cost_transition)
    model.cost_expr_ext_cost = j_stage
    model.cost_expr_ext_cost_0 = j_stage   # stage 0 identical (no initial cost)

    # Terminal cost (spec §5.4): wrong-side softplus + upper-band two-sided.
    # VR-07b T4/T5: the lN ANCHOR origin is the PER-STAGE t_b closest point
    # (same expression as l_route); the route NORMAL stays global. The softplus
    # SHAPE (wrong_side / l_max band / J_lower / J_upper / lateral_active) is
    # UNCHANGED -- only the lN origin moved. Mirrors MX build_terminal_cost_.
    l_term = (px - tb_x_at_k()) * nx + (py - tb_y_at_k()) * ny
    wrong_side = -pref_dir * (l_term / l_scale)
    j_lower = TERMINAL_TAU * ca.log(1.0 + ca.exp(wrong_side / TERMINAL_TAU))
    z_pos = (l_term - TERMINAL_L_MAX_M) / l_scale
    z_neg = (-l_term - TERMINAL_L_MAX_M) / l_scale
    j_upper = TERMINAL_TAU * (ca.log(1.0 + ca.exp(z_pos / TERMINAL_TAU)) +
                              ca.log(1.0 + ca.exp(z_neg / TERMINAL_TAU)))
    lateral_active = role * pref_dir * pref_dir
    j_terminal = role * j_lower + lateral_active * j_upper
    model.cost_expr_ext_cost_e = j_terminal

    # Stash the param syms for parameter_values (default seed).
    model._p_global = p_global
    model._p_stage = p_stage
    return model


def build_ocp() -> AcadosOcp:
    """Assemble the AcadosOcp: model + dims + solver opts + bounds."""
    ocp = AcadosOcp()
    model = build_model()
    ocp.model = model

    nx = model.x.rows()
    nu = model.u.rows()
    nh = model.con_h_expr.rows()
    Tf = N * DT

    # ---- Solver opts (P1b-1a T9 verbatim: FULL_CONDENSING_HPIPM, EXACT,
    #      DISCRETE, SQP, tol 1e-9, max_iter 400, MERIT_BACKTRACKING). ----
    ocp.solver_options.N_horizon = N
    ocp.solver_options.tf = Tf
    ocp.solver_options.qp_solver = "FULL_CONDENSING_HPIPM"
    ocp.solver_options.hessian_approx = "EXACT"                  # F3
    ocp.solver_options.integrator_type = "DISCRETE"
    ocp.solver_options.nlp_solver_type = "SQP"
    ocp.solver_options.nlp_solver_max_iter = 400
    ocp.solver_options.nlp_solver_tol_stat = 1e-9
    ocp.solver_options.nlp_solver_tol_eq = 1e-9
    ocp.solver_options.nlp_solver_tol_ineq = 1e-9
    ocp.solver_options.nlp_solver_tol_comp = 1e-9
    ocp.solver_options.globalization = "MERIT_BACKTRACKING"      # F4
    # cost_scaling = ones(N+1): CRITICAL for EXTERNAL cost (T2/T9 finding).
    ocp.solver_options.cost_scaling = np.ones(N + 1)

    # ---- Cost type: EXTERNAL per-stage + EXTERNAL terminal. ----
    ocp.cost.cost_type = "EXTERNAL"
    ocp.cost.cost_type_0 = "EXTERNAL"
    ocp.cost.cost_type_e = "EXTERNAL"

    # ---- Control box (lbu/ubu): |delta|<=DELTA_MAX, |n|<=N_MAX. ----
    ocp.constraints.lbu = np.array([-DELTA_MAX, N_MIN])
    ocp.constraints.ubu = np.array([+DELTA_MAX, N_MAX])
    ocp.constraints.idxbu = np.array([0, 1])

    # ---- State box (lbx/ubx): psi (idx 2), r (idx 3), u_surge (idx 4).
    #      ROT box on r is CRITICAL: the double-integrator pole at z=1 is
    #      marginally stable; |r|<=rot_max is what keeps r bounded (T6 lesson).
    #      px/py (idx 0,1) unbounded. ----
    ocp.constraints.lbx = np.array([PSI_LB, -ROT_MAX, U_SURGE_MIN])
    ocp.constraints.ubx = np.array([PSI_UB, +ROT_MAX, U_SURGE_MAX])
    ocp.constraints.idxbx = np.array([2, 3, 4])
    ocp.constraints.lbx_0 = np.array([PSI_LB, -ROT_MAX, U_SURGE_MIN])
    ocp.constraints.ubx_0 = np.array([PSI_UB, +ROT_MAX, U_SURGE_MAX])
    ocp.constraints.idxbx_0 = np.array([2, 3, 4])

        # ---- h bounds (T15 F2 row order: [prefix_psi(0), prefix_u(1),
        #      CPA(2..2+NT-1), direction(2+NT), min_alt(2+NT+1)]).
        #      P4: terminal C10/C11 abolished.
        #      Prefix rows 0,1: EQUALITY lh=uh=0 (the activation factor pact_pre
        #      deactivates them at stages k>=K by making the expression 0).
    #      CPA rows: one-sided >= 0 (lh=0, uh=+inf), softened via idxsh below.
    #      direction/min_alt: one-sided >= 0.
    lh = np.zeros((nh,))
    uh = np.full((nh,), UH_INF)
    uh[0] = 0.0   # prefix_psi: equality (lh=0 already)
    uh[1] = 0.0   # prefix_u:   equality (lh=0 already)
    # P4: terminal C10/C11 abolished (long horizon 1200s ensures convergence
    # without terminal set). Terminal COST J_terminal remains as cost_type_e.
    ocp.constraints.lh = lh
    ocp.constraints.uh = uh
    ocp.constraints.lh0 = lh
    ocp.constraints.uh0 = uh

    # ---- P3 per-target soft constraint: soften the NT CPA rows (idxsh).
    #      CPA rows now start at row 2 (after the 2 prefix rows); idxsh shifts
    #      from [0..NT-1] to [2..2+NT-1]. acatos adds ns=NT lower-slacks per
    #      stage. Mixed L1/L2 (zl*xi + 0.5*Zl*xi^2) -> exact-penalty. ----
    ocp.constraints.idxsh = np.arange(NT) + 2
    ocp.cost.Zl = ZL
    ocp.cost.zl = ZL_LIN
    ocp.cost.Zu = ZU
    ocp.cost.zu = ZU_LIN

    # ---- Initial state (origin, north heading, zero yaw rate, 5 m/s surge). ----
    ocp.constraints.x0 = np.array([0.0, 0.0, 0.0, 0.0, 5.0])

    # ---- Default parameter_values (seed; solver updates per-cycle). ----
    p_global_seed = np.zeros(NP_GLOBAL)
    p_global_seed[G_PSI0] = 0.0
    p_global_seed[G_U0] = 5.0
    p_global_seed[G_X0] = 0.0
    p_global_seed[G_Y0] = 0.0
    p_global_seed[G_ROUTE_BEARING] = 0.0
    p_global_seed[G_PLANNED_SPEED] = 5.0
    p_global_seed[G_HEADING_MIN] = PSI_LB
    p_global_seed[G_HEADING_MAX] = PSI_UB
    p_global_seed[G_SPEED_MIN] = U_SURGE_MIN
    p_global_seed[G_SPEED_MAX] = U_SURGE_MAX
    p_global_seed[G_CPA_SAFE] = 1852.0     # 1 NM default (ConstraintInputs)
    p_global_seed[G_ROT_MAX] = ROT_MAX
    p_global_seed[G_OWN_PSI] = 0.0
    p_global_seed[G_GIVE_WAY] = 0.0
    p_global_seed[G_RF_NY] = 1.0           # route-frame normal default (types.hpp)
    p_global_seed[G_LAT_SCALE] = LATERAL_SCALE_M
    p_global_seed[G_ROUTE_WEIGHT] = 0.0    # default inert (High-4 review fix)
    p_global_seed[G_DECEL_MAX] = 0.08
    # Per-stage seed (NP_PER_STAGE=37): all-zero -> pact_pre=0 (no prefix),
    # target drift = 0 (targets static at their global tx/ty), tb_x/tb_y = 0
    # (route origin coincident with global route origin -- benign). The solver
    # updates these per-cycle via update_params (F2 prefix + F4 drift + T3 tb).
    p_stage_seed = np.zeros(NP_PER_STAGE)
    ocp.parameter_values = np.concatenate([p_global_seed, p_stage_seed])

    return ocp


def main():
    ocp = build_ocp()
    model = ocp.model

    code_export_dir = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "c_generated_code")
    ocp.code_export_directory = code_export_dir
    json_file = os.path.join(code_export_dir, f"acados_ocp_{SOLVER_NAME}.json")
    AcadosOcpSolver.generate(ocp, json_file=json_file)

    nx = model.x.rows()
    nu = model.u.rows()
    nh = model.con_h_expr.rows()
    nsh = len(ocp.constraints.idxsh)
    print(f"PRODUCTION GEN: C code exported to {code_export_dir}")
    print(f"SOLVER_NAME={SOLVER_NAME}")
    print(f"DYNAMICS (Path B 5-dim): x=[px,py,psi,r,u_surge] nx={nx}, "
          f"u=[delta,n] nu={nu}")
    print(f"  c_u={C_U:.9e} (VDM-direct T8)  k_prop={K_PROP}  k_drag={K_DRAG}  "
          f"m_sge={M_SGE}")
    print(f"  u_surge[k+1]=u_surge+DT*(k_prop*n^2-k_drag*u_surge^2)/m_sge  "
          f"(T15 F1 mass-normalized; rpm -> surge STATE)")
    print(f"CONSTRAINTS: con_h nh={nh} (prefix 2 + CPA per-target={NT} + "
          f"direction + min_alt + terminal 3); ROT via lbx/ubx")
    print(f"  prefix rows 0,1: equality lh=uh=0 (pact_pre activation factor, F2)")
    print(f"  idxsh={list(ocp.constraints.idxsh)} (per-target xi slack, "
          f"ns={nsh}/stage)")
    print(f"  Zl={W_QUAD} zl={RHO_LIN} (mixed L1/L2 exact-penalty)")
    print(f"COST: EXTERNAL 6-cost per-stage (colreg/dist/route/vel/asym) + "
          f"EXTERNAL terminal (§5.4 softplus)")
    print(f"  cost_scaling=ones({N+1}) (T2/T9 -- discrete ungated sum)")
    print(f"PARAM PARTITION: global np_global={NP_GLOBAL} "
          f"(26 head + {NT}x5 target) + per-stage np_per_stage={NP_PER_STAGE} "
          f"(prefix psi/u + pact_pre + per-stage target drift x/y + tb_x/tb_y) "
          f"= {NP_GLOBAL+NP_PER_STAGE} (T15 F2/F4 + VR-07b T3 tb: GLOBAL stays "
          f"142-compatible (106); per-stage expands for drift+activation+t_b)")
    print(f"SOLVER OPTS: FULL_CONDENSING_HPIPM, EXACT (F3), DISCRETE, SQP, "
          f"tol 1e-9, max_iter 400, MERIT_BACKTRACKING (F4)")


if __name__ == "__main__":
    main()
