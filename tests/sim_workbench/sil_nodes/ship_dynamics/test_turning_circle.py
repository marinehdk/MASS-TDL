"""回转圆测试 — 精度验证 (Phase 1 容差 5%, HAZID 后收紧至 2%)。"""

import math
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../../../../src/sim_workbench/sil_nodes/ship_dynamics"))

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


def _run_turning_circle(steps: int, delta_deg: float, n_rps: float = 10.0):
    """运行指定步数的回转圆模拟。"""
    coeffs = MMGCoefficients()
    model = MMGModel(coeffs)
    state = ShipState(u=coeffs.u0, psi=coeffs.psi0)
    delta = math.radians(delta_deg)

    trajectory = [(state.x, state.y)]
    for _ in range(steps):
        state = model.rk4_step(state, delta_cmd=delta, n_rps_cmd=n_rps)
        trajectory.append((state.x, state.y))

    return trajectory, state


def _tactical_diameter(trajectory):
    """计算战术直径: 艏向旋转 180° 时的横向偏移。

    战术直径 ≈ 回转稳定后轨迹的平均直径。
    简化: 取轨迹后半部分中最大横向偏移 × 2。
    """
    half = len(trajectory) // 2
    ys = [p[1] for p in trajectory[half:]]
    ys_range = max(ys) - min(ys)
    return ys_range


def test_tactical_diameter_range():
    """2500 步 35° 右舵 → 战术直径在 50–500m 范围内。"""
    trajectory, state = _run_turning_circle(2500, 35.0)
    td = _tactical_diameter(trajectory)
    assert 50.0 <= td <= 500.0, f"战术直径 {td:.1f}m 超出 [50, 500] 范围"


def test_trajectory_rms_error():
    """轨迹 RMS 误差 < 5% — 与解析回转圆轨迹比较。

    解析参考: 稳定回转时轨迹近似圆形, 直径 ≈ 战术直径。
    取最后 500 步拟合圆, 计算 RMS 偏差。
    """
    trajectory, state = _run_turning_circle(2500, 35.0)

    # 取最后 500 步作为稳定回转阶段
    stable_traj = trajectory[-500:]
    xs = [p[0] for p in stable_traj]
    ys = [p[1] for p in stable_traj]

    # 最小二乘圆拟合
    n = len(xs)
    sum_x = sum(xs)
    sum_y = sum(ys)
    sum_x2 = sum(x * x for x in xs)
    sum_y2 = sum(y * y for y in ys)
    sum_xy = sum(x * y for x, y in stable_traj)
    sum_x3 = sum(x * x * x for x in xs)
    sum_y3 = sum(y * y * y for y in ys)
    sum_xy2 = sum(x * y * y for x, y in stable_traj)
    sum_x2y = sum(x * x * y for x, y in stable_traj)

    denom = float(n * sum_x2 - sum_x * sum_x)
    if abs(denom) < 1e-10:
        denom = 1e-10

    A = (n * sum_xy - sum_x * sum_y) / denom
    B = (sum_y * sum_x2 - sum_x * sum_xy) / denom

    denom2 = float(n * sum_y2 - sum_y * sum_y)
    if abs(denom2) < 1e-10:
        denom2 = 1e-10

    C = (n * sum_x2y - sum_x * sum_xy2) / denom2  if abs(denom2) > 1e-10 else 0
    D = (sum_y * sum_x2y - sum_xy * sum_xy2) / denom2 if abs(denom2) > 1e-10 else 0

    xc = (A * C + B * D) / 2.0
    yc = (C - A * xc) / B if abs(B) > 1e-10 else (D - A * xc) / 1e-10

    # 计算拟合圆半径
    radii = [math.sqrt((x - xc) ** 2 + (y - yc) ** 2) for x, y in stable_traj]
    R_mean = sum(radii) / len(radii)

    # RMS 误差
    errors = [abs(r - R_mean) for r in radii]
    rms_error = math.sqrt(sum(e * e for e in errors) / len(errors))

    # 相对误差 = RMS / R_mean
    rel_error = rms_error / R_mean
    assert rel_error < 0.05, f"RMS 相对误差 {rel_error:.4f} >= 5%"
