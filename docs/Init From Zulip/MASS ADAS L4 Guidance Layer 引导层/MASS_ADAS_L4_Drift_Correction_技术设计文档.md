# MASS_ADAS Drift Correction 偏流修正模块技术设计文档

**文档编号：** SANGO-ADAS-DFT-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的偏流修正思维模型
5. 计算流程总览
6. 海流偏流修正计算
7. 风压偏流修正计算
8. 综合偏流修正角
9. 偏流修正的自适应估计
10. 与 ILOS 积分项的协调
11. 内部子模块分解
12. 数值算例
13. 参数总览表
14. 与其他模块的协作关系
15. 测试策略与验收标准
16. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Drift Correction（偏流修正模块）是 Layer 4（Guidance Layer）中负责补偿风和海流对航向影响的子模块。当船在横向风流作用下航行时，即使船头（Heading）精确指向航线方向，实际的对地轨迹（COG）也会偏离航线——因为风和流在横向推着船走。

偏流修正的原理很简单：**让船头偏向风流来的方向一个角度 β_drift，使得对地轨迹（COG）对准航线方向。** 这就像人在侧风中走直线时，身体会自然地侧倾迎风一样。

### 1.2 核心职责

- **海流偏流角计算：** 基于已知或估计的海流矢量，计算使 COG 对准航线所需的航向偏置角。
- **风压偏流角计算：** 基于风速风向和船舶风压特性，计算风压引起的横向漂移补偿角。
- **综合偏流角输出：** 将海流和风压偏流角合并，作为加法项叠加到 LOS 输出的期望航向上。
- **自适应估计：** 当没有准确的风流数据时，通过观测 Heading 与 COG 的差异来反推偏流角。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 当前航向 ψ | Nav Filter | 真航向（度） |
| 对地航向 COG | Nav Filter | 对地运动方向（度） |
| 对地速度 SOG | Nav Filter | 对地速度（m/s） |
| 对水速度 (u, v) | Speed Log / Nav Filter | 纵荡和横荡速度 |
| 航段方位角 α_k | LOS Guidance | 当前航段方向 |
| 海流预报 | Weather Routing / 环境模型 | 流速流向（如可用） |
| 风速风向 | Perception / 气象站 | 真风速和真风向（如可用） |
| 船舶风压系数 | Parameter DB | 正面和侧面受风面积、风压系数 |

### 2.2 输出

```
DriftCorrectionOutput:
    Header header
    float64 beta_drift_total        # 综合偏流修正角（弧度）
    float64 beta_current            # 海流偏流分量（弧度）
    float64 beta_wind               # 风压偏流分量（弧度）
    float64 beta_adaptive           # 自适应估计分量（弧度）
    string estimation_mode          # "model_based" / "adaptive" / "hybrid"
    float64 confidence              # 偏流估计置信度 [0, 1]
```

---

## 3. 核心参数数据库

### 3.1 风压系数

| 参数 | 符号 | 典型值 | 说明 |
|------|------|-------|------|
| 正面受风面积 | A_front | 待测量（m²） | 船舶正面投影面积 |
| 侧面受风面积 | A_side | 待测量（m²） | 船舶侧面投影面积 |
| 风压横向力系数 | C_Y_wind | 0.6~1.0 | 取决于上层建筑形状 |
| 风压力矩系数 | C_N_wind | 0.05~0.15 | 风压引起的偏转力矩 |
| 风压横向速度系数 | K_wind | 0.01~0.05 | V_drift_wind ≈ K_wind × V_wind |

### 3.2 自适应估计参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 自适应滤波时间常数 | 30 s | 偏流角估计的平滑时间 |
| 自适应更新最低速度 | 2.0 m/s | 低速时 COG 不可靠，停止自适应 |
| COG-Heading 差异平滑窗口 | 60 s | 用于计算稳态偏流角 |

---

## 4. 船长的偏流修正思维模型

### 4.1 船长如何修正偏流

船长在海峡或沿岸航行时经常遇到横流。他的修正方法非常直观：

**看 COG 和航线的关系。** 如果 ECDIS 上显示对地轨迹偏离航线，说明有偏流。COG 向右偏，说明有从左向右的横流或横风。

**调整航向。** 他会把船头向流来的方向偏一个角度——"压流角"。偏多少度完全靠经验：1 节横流大约需要偏 3°~8°（取决于船速）；20 节横风大约需要偏 2°~5°（取决于受风面积）。

**持续微调。** 偏流不是一个精确值——海流可能随潮汐变化，风可能阵发性变化。船长会持续观察 COG 与航线的关系，不断微调偏流角。

---

## 5. 计算流程总览

```
输入：航段方位角 + 风流数据 + COG/Heading 观测
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：基于模型的海流偏流计算     │ ← 海流预报数据
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：基于模型的风压偏流计算     │ ← 风速风向数据
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：自适应偏流估计（补充）     │ ← COG vs Heading 观测
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：综合偏流角合成            │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：β_drift_total（叠加到 LOS 的 ψ_d 上）
```

---

## 6. 海流偏流修正计算

### 6.1 矢量三角形法

```
FUNCTION compute_current_drift(V_ship, α_k, V_current, θ_current):
    
    # V_ship: 对水速度（m/s）
    # α_k: 期望的对地航迹方向（航段方位角）
    # V_current: 海流速度（m/s）
    # θ_current: 海流流向（度，海流来向 + 180° = 海流去向）
    
    # 海流的横向分量（相对于航线方向）
    V_cross = V_current × sin(θ_current - α_k)
    
    # 偏流修正角
    IF V_ship > 0.1:
        β_current = asin(clamp(V_cross / V_ship, -1, 1))
    ELSE:
        β_current = 0
    
    # 正值 = 需要向右偏（海流从左往右推）
    # 负值 = 需要向左偏（海流从右往左推）
    
    RETURN β_current
```

### 6.2 物理解释

偏流角 β = arcsin(V_cross / V_ship)。当横流 V_cross = 1 m/s，船速 V_ship = 8 m/s 时，β = arcsin(1/8) = 7.2°。即船头需要向海流来向偏 7.2°，才能使对地轨迹对准航线。

**注意：** 当横流速度接近或超过船速时（V_cross / V_ship ≥ 1），偏流修正角趋近 90°——这意味着船无法在该流场中保持沿航线航行。此时应降速或请求航线重规划。

```
IF abs(V_cross) > V_ship × 0.8:
    # 横流太强，偏流修正不可行
    log_event("current_too_strong", {V_cross, V_ship})
    publish_advisory("横流过强，建议降速或调整航线")
```

---

## 7. 风压偏流修正计算

### 7.1 简化模型

```
FUNCTION compute_wind_drift(V_ship, heading, V_wind, θ_wind, ship_params):
    
    # 真风的横向分量（相对于船头方向）
    V_wind_cross = V_wind × sin(θ_wind - heading)
    
    # 风压引起的横向漂移速度
    V_drift_wind = ship_params.K_wind × V_wind_cross
    
    # 偏流修正角
    IF V_ship > 0.1:
        β_wind = atan(V_drift_wind / V_ship)
    ELSE:
        β_wind = 0
    
    RETURN β_wind
```

### 7.2 精确模型（基于受风面积）

```
FUNCTION compute_wind_drift_precise(V_ship, heading, V_wind, θ_wind, ship_params):
    
    # 相对风角
    wind_angle_relative = θ_wind - heading
    
    # 风压横向力
    F_Y_wind = 0.5 × ρ_air × C_Y_wind(wind_angle_relative) × A_side × V_apparent_wind²
    
    # 水阻力平衡：F_Y_wind = F_Y_hydro = C_Y_hydro × 0.5 × ρ_water × S_lateral × V_drift²
    # 简化：V_drift ≈ F_Y_wind / (C_Y_hydro × 0.5 × ρ_water × S_lateral × V_ship)
    
    V_drift = F_Y_wind / (ship_params.lateral_drag_coefficient × V_ship)
    
    β_wind = atan(V_drift / V_ship)
    
    RETURN β_wind
```

---

## 8. 综合偏流修正角

```
FUNCTION compute_total_drift(β_current, β_wind, β_adaptive, mode):
    
    IF mode == "model_based":
        # 有可靠的风流模型数据
        β_total = β_current + β_wind
        confidence = 0.8
    
    ELIF mode == "adaptive":
        # 无模型数据，完全依赖自适应估计
        β_total = β_adaptive
        confidence = 0.5
    
    ELIF mode == "hybrid":
        # 模型数据 + 自适应修正（推荐）
        β_model = β_current + β_wind
        β_total = 0.7 × β_model + 0.3 × β_adaptive    # 加权融合
        confidence = 0.7
    
    # 限幅：偏流角不应超过 30°（超过说明风流太强或模型有误）
    β_total = clamp(β_total, -30° × π/180, 30° × π/180)
    
    RETURN {β_total, confidence}
```

---

## 9. 偏流修正的自适应估计

### 9.1 原理

当没有准确的风流预报数据时（或数据不可靠），可以通过观测 Heading（船头指向）和 COG（对地运动方向）的差异来反推偏流角：

```
β_observed = COG - Heading
```

这个差异包含了所有外部干扰（海流 + 风压 + 波浪漂移）的综合效果。

### 9.2 自适应估计算法

```
FUNCTION adaptive_drift_estimation(heading, cog, speed, dt):
    
    IF speed < ADAPTIVE_MIN_SPEED:
        # 低速时 COG 受 GPS 噪声影响大，不可靠
        RETURN β_adaptive_prev    # 保持上次估计值
    
    # 观测偏流角
    β_observed = normalize_pm_pi(cog - heading)
    
    # 一阶低通滤波（时间常数 30 秒）
    α = dt / (ADAPTIVE_TAU + dt)
    β_adaptive = α × β_observed + (1 - α) × β_adaptive_prev
    
    β_adaptive_prev = β_adaptive
    
    RETURN β_adaptive

ADAPTIVE_MIN_SPEED = 2.0    # m/s
ADAPTIVE_TAU = 30.0         # 秒
```

### 9.3 自适应的局限性

自适应估计有一个固有缺陷：**它基于当前状态估计，有滞后性。** 当海流突然变化（比如进入一个新的潮流区域）时，自适应需要约 30~60 秒才能收敛到新的偏流角。在这段时间内，CTE 会暂时增大。

ILOS 的积分项可以部分弥补这个延迟，但最终的解决方案是获取更准确的海流预报数据。

---

## 10. 与 ILOS 积分项的协调

### 10.1 避免重复补偿

Drift Correction 和 ILOS 积分项都能补偿恒定侧向干扰。如果两者同时工作，可能导致过度补偿。

```
FUNCTION coordinate_with_ilos(β_drift, ilos_integral, e_current):
    
    # 如果 Drift Correction 已经在工作（|β_drift| > 1°），
    # 则减小 ILOS 积分增益，让 Drift Correction 主导
    IF abs(β_drift) > 1° × π/180:
        ilos_gain_reduction = 0.3    # ILOS 增益降至 30%
    ELSE:
        ilos_gain_reduction = 1.0    # ILOS 正常工作
    
    RETURN ilos_gain_reduction
```

### 10.2 分工

```
Drift Correction: 快速前馈补偿（基于模型或自适应观测）
ILOS 积分项:      慢速反馈补偿（消除 Drift Correction 的残余偏差）

两者互补：Drift Correction 快但可能不精确，ILOS 慢但最终精确。
```

---

## 11. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Current Drift Calculator | 海流偏流角计算 | C++ |
| Wind Drift Calculator | 风压偏流角计算 | C++ |
| Adaptive Estimator | 自适应偏流估计 | C++ |
| Drift Combiner | 综合偏流角合成 + 限幅 | C++ |
| ILOS Coordinator | 与 ILOS 积分项的协调 | C++ |

---

## 12. 数值算例

```
航段方位角 α_k = 000°（正北）
船速 V = 8 m/s
海流：1.0 m/s 从西向东（θ_current = 270°，去向 090°）
风：15 m/s 从西（θ_wind = 270°）
K_wind = 0.02

海流偏流角：
  V_cross = 1.0 × sin(090° - 000°) = 1.0 × sin(90°) = 1.0 m/s
  β_current = asin(1.0 / 8.0) = 7.2°

风压偏流角：
  V_wind_cross = 15 × sin(270° - 000°) = 15 × (-1) = -15 m/s（从右向左）
  V_drift_wind = 0.02 × (-15) = -0.3 m/s
  β_wind = atan(-0.3 / 8.0) = -2.1°

综合偏流角：
  β_total = 7.2° + (-2.1°) = 5.1°（向右偏 5.1°）

最终航向 = 000° + 5.1° = 005.1°
→ 船头向右偏 5.1°，抵消海流和风的横向推力，使 COG 对准 000° 航线方向
```

---

## 13. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| K_wind（风压系数） | 0.02 | USV 低干舷 |
| 偏流角上限 | ±30° | 超过说明不可行 |
| 自适应时间常数 | 30 s | |
| 自适应最低速度 | 2.0 m/s | |
| 模型/自适应融合权重 | 0.7 / 0.3 | hybrid 模式 |
| 横流过强告警阈值 | V_cross > 0.8 × V_ship | |

---

## 14. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Nav Filter → Drift | 输入 | Heading, COG, SOG |
| Weather/Env → Drift | 输入 | 风速风向、流速流向 |
| LOS Guidance → Drift | 输入 | α_k（航段方向） |
| Drift → LOS 后级叠加 | 输出 | β_drift_total |
| Drift → ILOS | 协调 | ILOS 增益调整系数 |

---

## 15. 测试策略与验收标准

| 场景 | 验证目标 | 验收标准 |
|------|---------|---------|
| DFT-001: 1节横流 | 海流偏流角正确 | CTE 稳态 < 3m |
| DFT-002: 20节横风 | 风压偏流角正确 | CTE 稳态 < 5m |
| DFT-003: 流+风同时 | 综合偏流正确 | CTE 稳态 < 5m |
| DFT-004: 无风流数据 | 自适应估计收敛 | 60s 内 CTE < 10m |
| DFT-005: 流突然变化 | 自适应跟踪 | 90s 内收敛 |
| DFT-006: 强横流 0.8V | 告警触发 | 正确报警 |

---

## v3.1 补充升级：岸壁效应补偿（离靠港操作）

### A. 岸壁效应（Bank Effect / Wall Effect）

当船靠近岸壁（码头面、防波堤）航行或机动时，船体与岸壁之间的水流加速产生两个力学效应：吸引力（船被"吸向"岸壁）和偏转力矩（船首或船尾被推离/拉向岸壁）。这在最后 20m 的靠泊接近中尤其显著。

```
FUNCTION bank_effect_compensation(own_ship, quay_distances, current_speed):
    
    # 只在近岸时激活（距离 < 3 倍船宽）
    min_dist = quay_distances.min_distance
    IF min_dist > ship_params.beam × 3:
        RETURN 0, 0    # 太远，无岸壁效应
    
    # 岸壁吸引力（横向力）
    # 近似公式：F_sway ∝ V² × B² / d²（速度平方×船宽平方/距离平方）
    # 方向：指向岸壁
    proximity_ratio = (ship_params.beam / min_dist) ** 2
    F_sway_bank = BANK_FORCE_COEFF × own_ship.u ** 2 × proximity_ratio
    # 方向取决于岸壁在哪一侧
    F_sway_bank *= sign_toward_wall(quay_distances)
    
    # 岸壁偏转力矩
    # 船首离岸壁远→船首被推离（正 Mz）
    # 简化：Mz ∝ F_sway × 杠杆臂
    lever_arm = estimate_pressure_center_offset(own_ship, quay_distances)
    Mz_bank = F_sway_bank × lever_arm
    
    # 补偿指令（反向力——抵消岸壁效应）
    drift_correction_sway = -F_sway_bank    # 反向横力
    drift_correction_yaw = -Mz_bank         # 反向力矩
    
    RETURN drift_correction_sway, drift_correction_yaw

BANK_FORCE_COEFF = 0.05    # 待海试标定——取决于船体形状和水深/船宽比
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v3.1 补充：增加岸壁效应补偿（Bank Effect）——近岸时的吸引力和偏转力矩前馈补偿 |

---

**文档结束**
