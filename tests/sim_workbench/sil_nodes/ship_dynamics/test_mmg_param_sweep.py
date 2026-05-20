# tests/sim_workbench/sil_nodes/ship_dynamics/test_mmg_param_sweep.py
"""D1.3.1' — ±20% parameter sweep: 7 cases, 600s sim, no divergence."""
import math
import copy
import sys
import os

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
    "../../../../src/sim_workbench/sil_nodes/ship_dynamics"))

from ship_dynamics.mmg_coefficients import MMGCoefficients
from ship_dynamics.mmg_model import MMGModel, ShipState


def _scale_derivative(coeffs: MMGCoefficients, prefix: str, factor: float):
    """Scale all MMG derivatives with given prefix by factor."""
    for attr_name in dir(coeffs):
        if attr_name.startswith(prefix + '_') and not attr_name.endswith('_prime'):
            try:
                curr = getattr(coeffs, attr_name)
                if isinstance(curr, (int, float)):
                    setattr(coeffs, attr_name, curr * factor)
            except (AttributeError, TypeError):
                pass
    return coeffs


# Parameter sweep cases: (case_name, modifier_lambda)
SWEEP_CASES = [
    ("baseline", lambda c: c),
    ("surge_mass_+20%", lambda c: setattr(c, 'm_x_prime', c.m_x_prime * 1.2) or c),
    ("surge_mass_-20%", lambda c: setattr(c, 'm_x_prime', c.m_x_prime * 0.8) or c),
    ("Xu_scaled_+20%", lambda c: _scale_derivative(c, 'X', 1.2) or c),
    ("Yv_scaled_+20%", lambda c: _scale_derivative(c, 'Y', 1.2) or c),
    ("Nv_scaled_+20%", lambda c: _scale_derivative(c, 'N', 1.2) or c),
    ("Nv_scaled_-20%", lambda c: _scale_derivative(c, 'N', 0.8) or c),
]


@pytest.mark.parametrize("case_name,modifier", SWEEP_CASES)
def test_param_sweep_stability(case_name, modifier, default_coeffs):
    """T5: ±20% 参数 sweep — 每 case 600s 仿真不发散。

    物理合理上限:
      - surge 速度 u ∈ [0, 15.43] m/s (≤ 30 kn)
      - yaw rate  |r| ≤ 0.1745 rad/s (≤ 10°/s)
      - 无 NaN/Inf 状态
    """
    # Clone coefficients and apply modifier
    coeffs = copy.deepcopy(default_coeffs)
    modifier(coeffs)

    # Rebuild model with modified coefficients
    model = MMGModel(coeffs)
    state = ShipState(
        x=0.0, y=0.0, psi=coeffs.psi0, u=coeffs.u0
    )

    sim_duration = 600.0
    dt = model.c.dt
    steps = int(sim_duration / dt)

    for i in range(steps):
        t = i * dt
        state = model.rk4_step(
            state,
            delta_cmd=0.0,      # 直航
            n_rps_cmd=5.0,      # 巡航推进
        )

        # Check every 1000 steps for divergence
        if i % 1000 == 0:
            assert math.isfinite(state.u), (
                f"[{case_name}] u diverged at step {i}, t={t:.1f}s: u={state.u}"
            )
            assert math.isfinite(state.r), (
                f"[{case_name}] r diverged at step {i}, t={t:.1f}s: r={state.r}"
            )
            assert state.u >= 0.0, (
                f"[{case_name}] u negative at step {i}: u={state.u:.3f} m/s"
            )
            assert state.u <= 15.43, (  # 30 kn
                f"[{case_name}] u exceeds 30 kn at step {i}: u={state.u:.3f} m/s"
            )
            assert abs(state.r) <= 0.1745, (  # 10°/s
                f"[{case_name}] |r| exceeds 10°/s at step {i}: r={math.degrees(state.r):.2f}°/s"
            )

    print(f"\n[D1.3.1'-T5] {case_name}: PASS "
          f"(u_final={state.u:.3f} m/s, r_final={state.r:.4f} rad/s, "
          f"steps={steps})")


"""
T5 失败诊断:
① u exceeds 30 kn → 无纵荡阻尼，减小 n_rps 或标注限制
② |r| exceeds 10°/s → 检查 N_r 是否被错误缩放
③ NaN/Inf → 检查 m_x_prime 是否 < -1.0 导致奇异矩阵
④ baseline 也失败 → default_coeffs 可能有问题
"""
