# tests/sim_workbench/sil_nodes/ship_dynamics/test_mmg_deterministic_replay.py
"""D1.3.1' — 20x deterministic replay: fixed seed, 1h sim, σ < 1e-9."""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__),
    "../../../../src/sim_workbench/sil_nodes/ship_dynamics"))

import numpy as np


def test_deterministic_replay(default_model, cruise_state):
    """T4: 20 次同 seed 重跑 — 位置/速度/姿态 σ < 1e-9.

    工况: n=5 rev/s, δ=0° (直航), 无风/流, 每次运行 1h 仿真时间
          (= 3600s / 0.02s = 180,000 步), 固定 seed 由 MMG 模型确定性保证。

    验证: 20 次运行的 end-state 在 x, y, psi, u, v, r 上
          max σ ≤ 1e-9（IEEE 754 双精度舍入噪声级）。
    """
    model = default_model
    n_cruise = 5.0
    n_runs = 20
    sim_duration = 3600.0  # 1 hour
    dt = model.c.dt  # 0.02s
    steps = int(sim_duration / dt)  # 180,000

    print(f"\n[D1.3.1'-T4] {n_runs} 次确定性重跑 @ {steps} 步 "
          f"(1h sim time), dt={dt}s")

    n_runs = 20
    end_states = []
    for run_id in range(n_runs):
        state = cruise_state
        for i in range(steps):
            state = model.rk4_step(state,
                                   delta_cmd=0.0,
                                   n_rps_cmd=n_cruise)
        end_states.append(state)

        if (run_id + 1) % 10 == 0:
            print(f"[D1.3.1'-T4]   run {run_id+1}/{n_runs} complete, "
                  f"u={state.u:.6f}")

    # Extract end-state fields
    x_vals = np.array([s.x for s in end_states])
    y_vals = np.array([s.y for s in end_states])
    psi_vals = np.array([s.psi for s in end_states])
    u_vals = np.array([s.u for s in end_states])
    v_vals = np.array([s.v for s in end_states])
    r_vals = np.array([s.r for s in end_states])

    # Compute standard deviations
    sigma_x = float(np.std(x_vals))
    sigma_y = float(np.std(y_vals))
    sigma_psi = float(np.std(psi_vals))
    sigma_u = float(np.std(u_vals))
    sigma_v = float(np.std(v_vals))
    sigma_r = float(np.std(r_vals))

    max_sigma = max(sigma_x, sigma_y, sigma_psi, sigma_u, sigma_v, sigma_r)

    print(f"[D1.3.1'-T4] σ_x={sigma_x:.3e}  σ_y={sigma_y:.3e}  "
          f"σ_psi={sigma_psi:.3e}")
    print(f"[D1.3.1'-T4] σ_u={sigma_u:.3e}  σ_v={sigma_v:.3e}  "
          f"σ_r={sigma_r:.3e}")
    print(f"[D1.3.1'-T4] max σ = {max_sigma:.3e}")

    assert max_sigma < 1e-9, (
        f"确定性重跑失败: max σ = {max_sigma:.3e} ≥ 1e-9。\n"
        f"  σ_x={sigma_x:.3e}, σ_y={sigma_y:.3e}, σ_psi={sigma_psi:.3e}\n"
        f"  σ_u={sigma_u:.3e}, σ_v={sigma_v:.3e}, σ_r={sigma_r:.3e}\n"
        f"  排查: MMG 模型是否包含隐式随机源（numpy.random 等）？"
    )


"""
T4 失败诊断:
    ① max_sigma 1e-6~1e-3 → macOS ARM FMA 差异，放宽至 1e-6
② max_sigma > 1e-3 → 隐式随机源，搜索代码中的 random/np.random 调用
③ NaN/Inf → 数值发散，检查 u 变化趋势
④ 耗时 > 5min → 减少至 20 次重跑或 10min sim_time（本测试已从 100→20 精简）
"""
