"""MMG 模型单元测试 — 纯 Python 数学，跨平台可用。"""

import math
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../../../src/sim_workbench/sil_nodes/ship_dynamics"))

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


def make_model():
    """创建使用默认 FCB 45m 系数的 MMGModel。"""
    return MMGModel(MMGCoefficients())


def test_ship_state_add():
    a = ShipState(x=1.0, y=2.0, psi=0.5, phi=0.1, u=9.0, v=0.5, r=0.02, p=0.0)
    b = ShipState(x=3.0, y=1.0, psi=0.2, phi=0.0, u=1.0, v=0.1, r=0.0, p=0.0)
    c = a + b
    assert c.x == 4.0
    assert c.y == 3.0
    assert c.psi == 0.7
    assert c.u == 10.0


def test_ship_state_mul():
    a = ShipState(x=2.0, y=3.0, psi=1.0, phi=0.5, u=5.0, v=1.0, r=0.1, p=0.01)
    b = a * 0.5
    assert b.x == 1.0
    assert b.u == 2.5
    assert b.r == 0.05
    # __rmul__
    c = 0.5 * a
    assert c.x == 1.0
    assert c.u == 2.5


def test_cruise_steady_state():
    """500 步 0° 舵角, 巡航转速 → 横向速度 |v| < 0.01*|u|。"""
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)

    for _ in range(500):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=model.c.n_rps_cruise)

    assert abs(state.v) < 0.01 * abs(state.u), f"v={state.v:.4f} 过大"


def test_cruise_no_roll():
    """500 步直航 → |phi| < 0.01 rad。"""
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)

    for _ in range(500):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=model.c.n_rps_cruise)

    assert abs(state.phi) < 0.01, f"phi={state.phi:.4f} 过大"


def test_rk4_position_advances():
    """500 步后位移 > 10.0 m (直航前进, psi0=90° 航向北, 主要位移在 y)。"""
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)

    for _ in range(500):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=model.c.n_rps_cruise)

    distance = math.sqrt(state.x ** 2 + state.y ** 2)
    assert distance > 10.0, f"位移={distance:.2f}m 不足"


def test_35deg_rudder_yaw_rate():
    """250 步 35° 右舵 → |r| > 0.015 rad/s。

    注: X_uu 加入后, 巡航转速下 (n_rps_cruise=3.0) 的偏航率为 ~0.022 rad/s.
    阈值设为 0.015 保留安全余量。
    """
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    delta = math.radians(35.0)

    for _ in range(250):
        state = model.rk4_step(state, delta_cmd=delta, n_rps_cmd=model.c.n_rps_cruise)

    assert abs(state.r) > 0.015, f"r={state.r:.5f} rad/s 偏航率不足"


def test_heading_changes():
    """500 步 35° 右舵 → |Δψ| > 0.15 rad。

    注: X_uu 加入后, 巡航转速下舷首变化较旧阈值小 (旧阈值基于 n_rps=10.0 这一非真实参数).
    阈值 0.15 rad (约 8.6°) 在 500 步内 —— 巡航转速下实际输出 ~0.20 rad.
    """
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    psi0 = state.psi
    delta = math.radians(35.0)

    for _ in range(500):
        state = model.rk4_step(state, delta_cmd=delta, n_rps_cmd=model.c.n_rps_cruise)

    dpsi = abs(state.psi - psi0)
    assert dpsi > 0.15, f"Δψ={dpsi:.4f} rad 艁向变化不足 (threshold 0.15)"


def test_rudder_opposite_sign():
    """左舵 (-35°) 应产生与右舵 (+35°) 方向相反的偏航率。"""
    model = make_model()

    s_right = ShipState(u=model.c.u0, psi=model.c.psi0)
    for _ in range(250):
        s_right = model.rk4_step(s_right, delta_cmd=math.radians(35.0), n_rps_cmd=model.c.n_rps_cruise)

    s_left = ShipState(u=model.c.u0, psi=model.c.psi0)
    for _ in range(250):
        s_left = model.rk4_step(s_left, delta_cmd=math.radians(-35.0), n_rps_cmd=model.c.n_rps_cruise)

    assert s_right.r > 0, f"右舵应产生正 r, got r={s_right.r:.4f}"
    assert s_left.r < 0, f"左舵应产生负 r, got r={s_left.r:.4f}"


def test_zero_rudder_heading_hold():
    """0° 舵角时艁向应保持大致不变 (直航稳定性)。"""
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    psi0 = state.psi

    for _ in range(200):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=model.c.n_rps_cruise)

    dpsi = abs(state.psi - psi0)
    assert dpsi < 0.1, f"Δψ={dpsi:.4f} rad, 0° 舵角应保持航向"


def test_propeller_zero_thrust_zero_rps():
    """Zero RPM → 船体阻力使船速慢慢减小 (物理正确: 无推进时 X_uu 产生阻力).

    与旧注释相反: 旧模型缺少 X_uu 项，导致零推力时速度不变 (物理错误).
    修正后速度应在 100 步 (2s) 内减少 0.001~0.05 m/s.
    """
    model = make_model()
    state = ShipState(u=model.c.u0, psi=model.c.psi0)
    u0 = state.u

    for _ in range(100):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=0.0)

    # Ship must decelerate (X_uu provides hull resistance without propulsion)
    assert state.u < u0, f"无推进应减速, got u={state.u:.4f} vs u0={u0:.4f}"
    # Must not decelerate catastrophically in only 2 seconds
    assert state.u > u0 * 0.95, f"减速过大: u={state.u:.4f} vs u0={u0:.4f}"


def test_initial_conditions_from_coeffs():
    """模型使用系数的初始条件创建状态。"""
    coeffs = MMGCoefficients(x0=100.0, y0=200.0, psi0=0.5, u0=8.0)
    model = MMGModel(coeffs)
    state = ShipState(x=coeffs.x0, y=coeffs.y0, psi=coeffs.psi0, u=coeffs.u0)

    for _ in range(10):
        state = model.rk4_step(state, delta_cmd=0.0, n_rps_cmd=coeffs.n_rps_cruise)

    assert state.x > coeffs.x0
