# D1.3.1 仿真器鉴定报告 — 02 · MMG 模型保真度报告

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-02 |
| 版本 | v0.1-draft |
| 编制日期 | 2026-05-15 |
| 状态 | 🟡 Projected — 方法论文档就绪；实测数据待 D1.3a ship_dynamics MMG 模型交付（5/31） |
| 上游 | D1.3a (5/31) · MMG Yasukawa 2015 4-DOF · FCB dynamics YAML |
| 下游 | P1 证明输出 → 06-evidence-matrix.md → CCS AIP C5.1 |
| 关键依赖 | `src/fcb_simulator/config/fcb_dynamics.yaml` + `src/fcb_simulator/src/mmg_model.cpp` + `RunScenario()` API（D1.3a T14） |

---

## §1 证明目标（P1）

**P1 — MMG 模型保真度证明**：FCB 船型的 4-DOF MMG 仿真模型在三种标准机动（直线停船、35°满舵定常旋回、Zigzag 10°/10°）中，运动轨迹的 RMS 误差不超过解析/数值参考解的 **5%**。

| 指标 | 阈值 | 测试环境 |
|---|---|---|
| 直线停船距离 | \|d_sim − d_ref\| / d_ref ≤ 5% | 引擎关闭（n=0），舵归中（δ=0），初速 9.26 m/s（18 kn），无风/流 |
| 战术直径 D_T | ≤ 5% 偏差 | 35° 满舵，初速 9.26 m/s，无风/流 |
| Zigzag 一阶超调角 OSA1 | ±1° | 10°/10°，初速 9.26 m/s，无风/流 |

---

## §2 MMG 模型描述

### 2.1 数学模型

采用 **Yasukawa 2015 标准 MMG 模型**（4-DOF：surge/sway/yaw/roll）[R-NLM:30]：

```
(m + m_x) · u̇ − (m + m_y) · v · r − m · x_G · r² = X_H + X_P + X_R
(m + m_y) · v̇ + (m + m_x) · u · r + m · x_G · ṙ = Y_H + Y_R
(I_zz + J_zz + m · x_G²) · ṙ + m · x_G · (v̇ + u · r) = N_H + N_R − Y_H · x_G
(I_xx + J_xx) · φ̈ = K_H + K_R − m · g · GM · φ − B_damp · φ̇
```

其中：
- 下标 `H` = Hull 水动力（Abkowitz 多项式展开到 3 阶交叉项）
- 下标 `P` = Propeller（螺旋桨敞水曲线 + 伴流系数 w_P）
- 下标 `R` = Rudder（襟翼舵升力模型，含流入减速因子 u_R）
- 下标 `x_G` = 重心纵向位置（距 midship）

数值积分：**RK4, dt = 0.02s**（决策记录 D1.3a §0 🟢 High）

### 2.2 FCB 船型参数

| 参数 | 符号 | 值 | 单位 | 来源 |
|---|---|---|---|---|
| 垂线间长 | L_pp | 46.0 | m | FCB 设计值 |
| 型宽 | B | 8.6 | m | FCB 设计值 |
| 设计吃水 | d | 2.8 | m | FCB 设计值 |
| 排水体积 | ∇ | ~350 | m³（= 350t 标准海水 ρ=1025） | FCB 设计值 |
| 方形系数 | C_b | 0.52 | — | 计算值（∇ / (L · B · d)） |
| 船型分类 | — | **SEMI_PLANING** | — | 决策记录 D1.3a §0（Fn_max ≈ 0.63） |
| 重心纵向位置 | x_G | −0.72 | m（midship 后） | FCB YAML `fcb_dynamics.yaml` |
| 初稳性高 | GM | 1.8 | m | FCB YAML `fcb_dynamics.yaml` |
| 巡航速度 | u₀ | 9.26 | m/s（18 kn） | FCB 设计巡航 |
| 最大航速 | u_max | 11.32 | m/s（22 kn） | FCB 设计最大 |
| 螺旋桨直径 | D_p | 1.6 | m | FCB YAML `fcb_dynamics.yaml` |
| 舵面积 | A_R | 1.2 | m² | FCB YAML `fcb_dynamics.yaml` |

> **⚠ HAZID 标注**：上表所有 MMG 系数（水动力导数 X_uu, Y_v, N_r 等 ~40 项）来自 `fcb_dynamics.yaml`，标注 `[HAZID-UNVERIFIED: RUN-001 2026-08-19]`（D1.3a T2 批量标注）。HAZID 完成后部分系数可能调整 > 10%，触发重跑（见 §7）。

### 2.3 软件实现溯源

| 工件 | 路径 | 角色 | 基线 commit |
|---|---|---|---|
| MMG 模型 | `src/fcb_simulator/src/mmg_model.cpp` | 水动力计算 | D1.3a 交付 |
| RK4 积分器 | `src/fcb_simulator/src/rk4_integrator.cpp` | 数值积分 | D1.3a 交付 |
| 船型参数 | `src/fcb_simulator/config/fcb_dynamics.yaml` | 参数配置 | D1.3a 交付 |
| 机动接口 | `src/fcb_simulator/include/fcb_simulator_node.hpp` `RunScenario()` | D1.3.1 调用接口 | D1.3a T14 交付 |
| 停船测试 | `src/fcb_simulator/test/test_stopping_error.cpp` | 自动化回归 | D1.3a T6 交付 |

---

## §3 三种解析参考解

### 3.1 直线停船（Straight Deceleration）

**物理描述**：船舶在巡航航速下推进系统关闭（n=0 rev/s），舵归中（δ=0°），无风/流，依靠船体阻力自然减速至可忽略航速。

**Nomoto 一阶纵荡近似**：

```
T_u · u̇ + u = 0
→ u(t) = u₀ · exp(−t / T_u)
→ s(t) = u₀ · T_u · (1 − exp(−t / T_u))
→ S_stop = u₀ · T_u （t → ∞ 理论停船距离）
```

**FCB 参数**：

| 参数 | 值 | 推导 |
|---|---|---|
| 初速 u₀ | 9.26 m/s | 18 kn 巡航 |
| 纵荡时间常数 T_u | **12.5s** 🟡 | 由 MMG X_u 导数估算：`T_u ≈ (m + m_x) / |X_u|`；实际值将随 D1.3a 交付后从高精度 dt=0.001s 参考解提取 |
| 总阻力系数 X_u | −2800 N/(m/s) | FCB YAML Abkowitz 展开线性项 |
| 理论停船距离 S_stop | **~115.8m** 🟡 | `u₀ × T_u = 9.26 × 12.5` |
| 99% 距离（u < 0.1 m/s） | **~57m** 🟡 | `u₀ × T_u × ln(u₀/0.1)` |

**阈值**：实测停船距离（dt=0.02s RK4）vs 数值参考解（dt=0.001s RK4，同一代码），偏差 ≤ 5%（约 **±5.8m**）。

**参考解方法**：由于 FCB YAML 中无独立直线阻力系数（Abkowitz 形式交叉项在 v=0, r=0 时退化为零，实际减速力来自螺旋桨/舵剩余阻力及高阶项），不存在干净闭合解，采用 dt=0.001s RK4 作为"等效真值" 🟡（方法来自 D1.3a §5.1 决策）。

### 3.2 35° 满舵定常旋回（Turning Circle）

**物理描述**：船舶以巡航航速直航，突操 35° 满舵并保持，进入定常旋回。测定战术直径 D_T（转首 180° 时的横向偏移）和定常漂角 β。

**Nomoto 一阶转首模型**：

```
T · ṙ + r = K · δ
→ 定常解（t → ∞）：r_ss = K · δ
→ 战术直径（估算）：D_T ≈ 2 · u₀ / (K · δ) （小漂角简化）
```

**FCB 参数**：

| 参数 | 值 | 推导 |
|---|---|---|
| 增益常数 K | **0.085 s⁻¹** 🟡 | 由 `K = N_δ / (N_r · m_0)` 公式估计 [R-NLM:30 表 3a]；实际值随 D1.3a 旋回模拟提取 |
| 时间常数 T | **2.8s** 🟡 | 同上 |
| 战术直径 D_T（35°） | **~835m** 🟡 | `≈ 2 · u₀ / (K · δ)` = `2 × 9.26 / (0.085 × 0.6109)`，其中 δ=35°=0.6109 rad |
| 定常漂角 β | **~8.5°** 🟡 | `≈ −atan(v_ss / u_ss)` ≈ `m' · K · δ` 小漂角估计 |
| 纵距（Advance）A_d | **~620m** 🟡 | T + t_a 相位累积估计 |

**阈值**：实测 D_T 与公式值偏差 ≤ 5%（约 **±42m**）。

### 3.3 Zigzag 10°/10°（Kempf Overshoot）

**物理描述**：船舶直航，操右舵 10°，当转首角达右 10° 时反操左舵 10°，当转首角回至左 10° 时再反操右舵 10°，记录首向角的超调量与到达时间。

**Kempf 超调模型**：

```
一阶超调角：OSA1 = ψ_max1 − 10°
一阶到达时间：t_a（首达首次执行航向角的时间）
二阶超调角：OSA2 = ψ_max2 − (−10°)
```

**Nomoto 低频近似**：

```
OSA1 ≈ 10° · (exp(−t_a / T) − K · T · [1 − exp(−t_a / T)])
t_a ≈ T · ln(1 + 1 / (K · T · δ_rel))
```

**FCB 参数**：

| 参数 | 值 | 推导 |
|---|---|---|
| 一阶超调角 OSA1 | **~3.2°** 🟡 | Nomoto 近似 `OSA1 ≈ K · δ₀ · T · (1 − e^(−t_a/T)) − δ₀` |
| 一阶到达时间 t_a | **~12s** 🟡 | 由 Nomoto 响应函数积分估计 |
| 二阶超调角 OSA2 | **~4.5°** 🟡 | 同上，经验公式 OSA2/OSA1 ≈ 1.4 |

**阈值**：`OSA1` ± 1°，`t_a` ± 0.6s（对应约 5% 相对误差）。

> **🟡 置信度声明**：以上三个参考解的所有数值（T_u, K, T, S_stop, D_T, β, OSA1, t_a）均基于 Yasukawa 2015 论文表 3a 的 H 型船（L=150m, B=22m, C_b=0.72, δ=35° K=0.082 T=65s）按 FCB 尺度缩放 [W-D1.3.1-1]。FCB 作为 **SEMI_PLANING** 船型（C_b=0.52, Fn_max=0.63），水动力特性与丰满船型有本质差异。MMG 仿真器中提取的 K/T 值可能偏离 ±30%。D1.3a 交付后的实测值为最终权威源。本节当前仅作为**方法论述**和**预期数量级指示**。

---

## §4 测试设计

### 4.1 5 次运行矩阵

| 运行 ID | 机动类型 | 初速 (m/s) | δ (°) | 终端条件 | 重放 seed | 数据导出 |
|---|---|---|---|---|---|---|
| **R-P1-01** | 直线停船 | 9.26 | 0 | u < 0.1 m/s | 42 | CSV 全轨迹 |
| **R-P1-02** | 直线停船 | 9.26 | 0 | u < 0.1 m/s | 42 | —（重放 P2 共用） |
| **R-P1-03** | 35° 满舵旋回 | 9.26 | 35 | ψ ≥ 540° | 43 | CSV 全轨迹 |
| **R-P1-04** | 35° 满舵旋回 | 9.26 | 35 | ψ ≥ 540° | 43 | —（重放 P2 共用） |
| **R-P1-05** | Zigzag 10°/10° | 9.26 | ±10 | ψ 过 ±20° 执行完成 | 44 | CSV 全轨迹 |

> R-P1-02 和 R-P1-04 同时服务 P2 确定性证明（03-determinism-replay.md），以相同 seed 验证完全重复性。

### 4.2 执行环境

| 条件 | 值 |
|---|---|
| 环境力 | **关闭**（零风/零流/零浪）：P1 仅测裸船 MMG 水动力 |
| 推进模型 | 螺旋桨敞水曲线 + 伴流系数（Yasukawa 标准） |
| 舵模型 | 襟翼舵升力 + 流入减速因子 u_R |
| God Tracker | **启用**（真值直接注入）：消除 tracker 偏差对保真度判定的干扰 |
| 固定 seed | 42 / 43 / 44（保证可重放） |

### 4.3 CFD 备选水池试验路径（降级方案）

若 D1.3a 交付的 MMG 参数来自经验公式（无 FCB 水池试验数据），且重跑三次 RMS 仍 > 5%，则启动 CFD 备选路径：

**nikpau MMG 4 步过程**（参考 [R-NLM:30] 附录 B）：

1. **静水阻力 CFD**（RANS, k-ω SST）：提取 X_uu 等纵荡导数
2. **斜航 CFD**（Oblique Towing, β=0°/2°/4°/6°）：提取 Y_v, N_v 及交叉项
3. **旋臂 CFD**（Circular Motion, r'=0.1/0.2/0.3）：提取 Y_r, N_r
4. **纯摇艏 CFD**（Pure Yaw）：提取附加质量项 m_y, J_zz

**DoD 判据**：CFD vs 经验公式参数，若 RMS 偏差 > 15%，以 CFD 参数替换经验公式并更新 `fcb_dynamics.yaml`。

**工时估算**：~80h C FD 工程师（不在 D1.3.1 预算内；B4 contingency 触发条件）。

---

## §5 结果（🟡 Projected — 数据待 D1.3a 交付）

### 5.1 直线停船

| 指标 | 参考解（dt=0.001s） | 实测（dt=0.02s） | 偏差（%） | 通过？ |
|---|---|---|---|---|
| 停船距离 S_stop (m) | 🟡 待填入 | 🟡 待填入 | — | 🟡 待判定 |
| 停船时间 t_stop (s) | 🟡 待填入 | 🟡 待填入 | — | 🟡 待判定 |

> **🟡 Projected** — 以上数据需在 D1.3a（5/31）交付后通过 `RunScenario(STRAIGHT_DECEL)` 采集。数值参考解由同一 MMG 代码以 dt=0.001s 步长积分生成（D1.3a T6）。

### 5.2 35° 满舵旋回

| 指标 | 参考解（公式 / dt=0.001s） | 实测（dt=0.02s） | 偏差（%） | 通过？ |
|---|---|---|---|---|
| 战术直径 D_T (m) | 🟡 待填入 | 🟡 待填入 | — | 🟡 待判定 |
| 纵距 Advance (m) | 🟡 待填入 | 🟡 待填入 | — | — |
| 定常漂角 β (°) | 🟡 待填入 | 🟡 待填入 | — | — |
| 定常旋回速率 r (deg/s) | 🟡 待填入 | 🟡 待填入 | — | — |

> **🟡 Projected** — 数据通过 `RunScenario(STANDARD_TURN_35DEG)` 采集。

### 5.3 Zigzag 10°/10°

| 指标 | 参考解（Nomoto 公式） | 实测（dt=0.02s） | 偏差 | 通过？ |
|---|---|---|---|---|
| 一阶超调角 OSA1 (°) | 🟡 待填入 | 🟡 待填入 | — | 🟡 待判定 |
| 一阶到达时间 t_a (s) | 🟡 待填入 | 🟡 待填入 | — | — |
| 二阶超调角 OSA2 (°) | 🟡 待填入 | 🟡 待填入 | — | — |

> **🟡 Projected** — 数据通过 `RunScenario(ZIG_ZAG_10_10)` 采集。

### 5.4 95% 置信区间（Bootstrap N=1000）

> **🟡 Projected** — D1.3a 交付后，对每种机动以不同 seed 运行 100 次 bootstrap 采样，报告 RMS 的 95% CI。若 CI 上界 > 5%，标记为"未通过并在 §5.5 中分析根因"。

### 5.5 偏差分析（模板）

| 偏差源 | 可能影响 | 排查方法 |
|---|---|---|
| RK4 截断误差（dt=0.02s） | O(dt⁴) 累积 | 与 dt=0.001s 参考解逐项对比 |
| MMG 系数不确定性 | 系统性偏差（±20%） | 单参数 ±10% sensitivity sweep |
| 数值参考解不收敛 | dt=0.001s 仍不够小 | dt=0.0005s 交叉验证 |
| 螺旋桨/舵交叉项 | 低速偏差异常 | 分速度段（<3kn / 3–10kn / >10kn）分析 |

---

## §6 验收判定（Acceptance Checkboxes）

| # | 检查项 | 状态 | 依赖 |
|---|---|---|---|
| AC-P1-1 | 直线停船 RMS ≤ 5%（vs dt=0.001s 参考解） | 🟡 待 D1.3a | `test_stopping_error.cpp` 断言 PASS |
| AC-P1-2 | 35° 旋回 D_T RMS ≤ 5% | 🟡 待 D1.3a | `RunScenario()` 输出 |
| AC-P1-3 | Zigzag OSA1 ± 1°, t_a ± 0.6s | 🟡 待 D1.3a | `RunScenario()` 输出 |
| AC-P1-4 | 1h 稳定性：180,000 步无 NaN/Inf（dt=0.02s） | 🟡 待 D1.3a | `test_stability_1h.cpp` PASS |
| AC-P1-5 | 95% CI 上界 ≤ 5%（bootstrap N=1000） | 🟡 待 D1.3a | 统计学检验 |
| AC-P1-6 | 全轨迹 CSV 归档在 annex/csv/ | 🟡 待 D1.3a | 文件存在 + 校验和 |

---

## §7 HAZID 参数敏感度说明

MMG 模型中的 40+ 水动力导数标注为 `[HAZID-UNVERIFIED: RUN-001 2026-08-19]`（见 `fcb_dynamics.yaml`）。HAZID RUN-001（8/19 完成）可能对以下参数提出修正：

| 参数组 | 敏感机动 | 修正可能性 |
|---|---|---|
| 纵荡阻力导数 (X_uu, X_vv, X_vr, X_rr) | 停船距离 | 🟡 中（若操纵性海试数据可用，可能 ±10%） |
| 横向力导数 (Y_v, Y_r, Y_vvv, ...) | 战术直径 / 漂角 | 🟡 高（半滑行船型经验公式精度有限） |
| 力矩导数 (N_v, N_r, N_vvv, ...) | 旋回速率 / Zigzag 超调 | 🟡 高（同上） |
| 舵力系数 (t_R, a_H, x_H, u_R 模型) | 所有机动 | 🟡 中（襟翼舵特殊效应） |

**触发重跑规则**：任一水动力导数修正幅度 **> 10%** 时，对应受影响的机动类型须 **全量重跑** 本节全部 5 次测试并更新结果表。若单项导数修正 ≤10% 且不影响受影响机动 RMS 至 > 5%，则仅更新 `fcb_dynamics.yaml` 版本号，不重跑。

---

## §8 参考文献

| 编号 | 来源 | 描述 |
|---|---|---|
| [R-NLM:30] | NLM Deep Research 2026-05-12 colav_algorithms | Yasukawa 2015 — MMG standard model for ship maneuvering (4-DOF derivation, Abkowitz expansion, Nomoto simplification) |
| [R-NLM:31] | 同上 | Nomoto K/T index derivation from MMG coefficients |
| [R-NLM:32] | 同上 | Kempf zigzag overshoot analytical approximation |
| [R-NLM:33] | 同上 | nikpau MMG 4-step CFD process for hull derivatives without model test |
| [W-D1.3.1-1] | 本文档 §3 | FCB Nomoto 参数缩放估算（基于 Yasukawa 2015 H 型船数据按尺度比缩放） |
| [W-D1.3.1-3] | 本文档 §2.2 | FCB 船型参数汇总表（源：fcb_dynamics.yaml + FCB 设计值） |
| [GAP-032] | 全局 gap | DNV-RP-0513 全文未采购；MMG 鉴定级别对标 CG-0264 §4 等效论证路径 |

---

*文档版本 v0.1-draft · 2026-05-15 · 🟡 Projected — 所有 §5 结果数据待 D1.3a 交付（5/31）后填充。*
