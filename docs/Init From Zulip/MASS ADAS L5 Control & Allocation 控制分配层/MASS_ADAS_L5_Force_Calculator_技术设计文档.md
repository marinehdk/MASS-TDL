# MASS_ADAS Force Calculator 力计算器技术设计文档

**文档编号：** SANGO-ADAS-FRC-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 计算流程总览
5. 力/力矩合成算法
6. 横向力计算
7. 优先级仲裁
8. 力需求饱和检测与预处理
9. 坐标系变换
10. 内部子模块分解
11. 数值算例
12. 参数总览表
13. 与其他模块的协作关系
14. 测试策略与验收标准
15. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Force Calculator（力计算器）是 Layer 5 中连接"控制器"和"推力分配器"的中间层。它接收 Heading Controller 输出的艏摇力矩需求 M_z 和 Speed Controller 输出的纵向推力需求 F_x，将两者合成为一个完整的三自由度力/力矩需求向量 τ_cmd = [F_x, F_y, M_z]ᵀ，传递给 Thrust Allocator。

### 1.2 为什么需要这个模块

Heading Controller 输出的是 M_z，Speed Controller 输出的是 F_x。但推力分配器需要完整的三自由度力需求——包括横向力 F_y。在大多数直线航行场景中 F_y = 0，但在以下场景中 F_y ≠ 0：

- **侧风补偿：** 横风产生侧向漂移，需要横向推力抵消。
- **侧流补偿：** 横向海流导致的横向漂移。
- **靠泊操纵：** 低速靠泊时需要直接的横向移动能力。
- **动态定位（DP）模式：** 保持固定位置时需要全三自由度控制。

此外，Force Calculator 还负责**优先级仲裁**——当 Heading Controller 和 Speed Controller 的需求之和超过推进系统总能力时，决定哪个优先满足。

### 1.3 核心职责

- **力/力矩合成：** 将 F_x、F_y、M_z 合成为三自由度需求向量 τ_cmd。
- **横向力计算：** 在需要时计算横向力需求 F_y。
- **优先级仲裁：** 当总需求超过能力时，按安全优先级分配资源。
- **饱和预处理：** 在传递给 Thrust Allocator 之前，检查总需求是否在可行域内，必要时做预缩放。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| F_x_demand | Speed Controller | 50 Hz | 纵向推力需求（N） |
| M_z_demand | Heading Controller | 50 Hz | 艏摇力矩需求（Nm） |
| 偏流修正需求 | Drift Correction | 10 Hz | 横向偏流量（用于计算 F_y） |
| 控制模式 | Mode Switcher | 10 Hz | Transit/Maneuvering/DP |
| 低速横向控制需求 | Guidance (L4) | 10 Hz | 低速操纵时的横向速度指令 |
| 本船速度 (u, v) | Nav Filter | 50 Hz | |

### 2.2 输出

```
ForceCommand:
    Header header
    float64 force_x         # 纵向推力需求（N，前进正）
    float64 force_y         # 横向推力需求（N，左舷正）
    float64 moment_z        # 艏摇力矩需求（Nm，左转正）
    float64 total_power     # 总功率需求估算（kW）
    bool demand_saturated   # 总需求是否超过能力（已被缩放）
    float64 saturation_factor  # 缩放因子 [0,1]（1=未缩放）
    string priority_mode    # "heading_priority"/"speed_priority"/"balanced"
```

---

## 3. 核心参数数据库

### 3.1 优先级参数

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 航向优先级权重 | 0.6 | 总资源不足时航向控制的份额 |
| 速度优先级权重 | 0.3 | 速度控制的份额 |
| 横向优先级权重 | 0.1 | 横向控制的份额 |
| 紧急模式航向优先级 | 0.9 | 紧急避碰时几乎全部给航向 |

### 3.2 力能力包络

| 参数 | 说明 |
|------|------|
| F_x_max | 最大纵向推力（N） |
| F_y_max | 最大横向推力（N）——通常远小于 F_x_max |
| M_z_max | 最大艏摇力矩（Nm） |
| P_total_max | 推进系统总功率上限（kW） |

---

## 4. 计算流程总览

```
F_x (Speed Ctrl) ──┐
                    ├──→ 合成 τ_cmd ──→ 饱和检测 ──→ 优先级缩放 ──→ 输出
F_y (计算/指令) ────┤
                    │
M_z (Heading Ctrl) ─┘
```

---

## 5. 力/力矩合成算法

### 5.1 标准合成

```
FUNCTION compute_force_command(F_x_demand, M_z_demand, F_y_demand, mode):
    
    # 直接合成三自由度向量
    tau_cmd = [F_x_demand, F_y_demand, M_z_demand]
    
    RETURN tau_cmd
```

### 5.2 Transit 模式下的简化

在高速巡航（Transit）模式下，F_y 通常为零——横向力需求由航向修正间接实现（改变航向使横向速度分量抵消漂移）：

```
IF mode == "TRANSIT":
    F_y = 0    # 高速时不使用横向推力
    tau_cmd = [F_x_demand, 0, M_z_demand]
```

### 5.3 Maneuvering 模式下的完整三自由度

在低速操纵模式下，可能需要独立的横向推力：

```
IF mode == "MANEUVERING" OR mode == "DP":
    # F_y 由 Drift Correction 或低速横向控制器提供
    tau_cmd = [F_x_demand, F_y_demand, M_z_demand]
```

---

## 6. 横向力计算

### 6.1 偏流补偿横向力

在低速时，Drift Correction 的航向偏置方法失效（低速时改变航向不能有效产生横向速度分量）。此时需要直接的横向推力：

```
FUNCTION compute_lateral_force(v_drift, v_current, ship_params, mode):
    
    IF mode != "MANEUVERING":
        RETURN 0    # 高速时不计算横向力
    
    # 横向漂移速度
    v_lateral_error = -v_drift    # 需要消除的横向速度
    
    # 简单 P 控制
    F_y = K_LATERAL × v_lateral_error
    
    # 限幅
    F_y = clamp(F_y, -F_Y_MAX, F_Y_MAX)
    
    RETURN F_y

K_LATERAL = 2000    # N/(m/s)——横向力增益
```

### 6.2 低速靠泊横向力

在靠泊操纵中（如果系统支持），Guidance 层会直接下达横向速度指令 v_cmd：

```
IF mode == "DP" AND guidance.lateral_cmd IS NOT NULL:
    v_error = guidance.lateral_cmd - v
    F_y = K_LATERAL × v_error + K_LATERAL_I × ∫v_error dt
```

---

## 7. 优先级仲裁

### 7.1 资源超限检测

```
FUNCTION check_capacity(tau_cmd, capacity):
    
    # 简化检查：各分量是否超过单独的最大值
    overloaded = false
    
    IF abs(tau_cmd.F_x) > capacity.F_x_max: overloaded = true
    IF abs(tau_cmd.F_y) > capacity.F_y_max: overloaded = true
    IF abs(tau_cmd.M_z) > capacity.M_z_max: overloaded = true
    
    # 更精确的检查：总功率
    P_estimated = estimate_power(tau_cmd, own_speed)
    IF P_estimated > capacity.P_total_max: overloaded = true
    
    RETURN {overloaded, P_estimated}
```

### 7.2 优先级缩放

当总需求超过能力时，按优先级分配资源：

```
FUNCTION apply_priority_scaling(tau_cmd, capacity, priority_weights):
    
    IF NOT check_capacity(tau_cmd, capacity).overloaded:
        RETURN tau_cmd    # 未超限，不缩放
    
    # 计算各分量的"利用率"
    util_Fx = abs(tau_cmd.F_x) / capacity.F_x_max
    util_Fy = abs(tau_cmd.F_y) / capacity.F_y_max
    util_Mz = abs(tau_cmd.M_z) / capacity.M_z_max
    
    # 按优先级缩放——优先级低的缩放更多
    # 航向（M_z）优先级最高——最后缩放
    
    w_heading = priority_weights.heading    # 0.6
    w_speed = priority_weights.speed       # 0.3
    w_lateral = priority_weights.lateral   # 0.1
    
    # 先满足航向需求（不超过能力）
    M_z_allocated = clamp(tau_cmd.M_z, -capacity.M_z_max × w_heading / 0.6, capacity.M_z_max × w_heading / 0.6)
    
    # 剩余能力给速度
    remaining_power = capacity.P_total_max - estimate_power_single(M_z_allocated, ...)
    F_x_allocated = clamp(tau_cmd.F_x, -remaining_power / max(abs(own_speed), 1), remaining_power / max(abs(own_speed), 1))
    
    # 最后横向
    remaining_power_2 = remaining_power - abs(F_x_allocated × own_speed)
    F_y_allocated = clamp(tau_cmd.F_y, -F_Y_MAX × w_lateral, F_Y_MAX × w_lateral)
    
    saturation_factor = min(
        abs(F_x_allocated / tau_cmd.F_x) if tau_cmd.F_x != 0 else 1,
        abs(M_z_allocated / tau_cmd.M_z) if tau_cmd.M_z != 0 else 1
    )
    
    RETURN {
        force_x: F_x_allocated,
        force_y: F_y_allocated,
        moment_z: M_z_allocated,
        demand_saturated: true,
        saturation_factor: saturation_factor,
        priority_mode: "heading_priority"
    }
```

### 7.3 紧急避碰模式下的优先级

```
IF emergency_maneuver_active:
    # 几乎全部资源给航向控制——必须能转向
    priority_weights = {heading: 0.9, speed: 0.08, lateral: 0.02}
```

---

## 8. 力需求饱和检测与预处理

```
FUNCTION preprocess_for_allocator(tau_cmd, capacity):
    
    # Thrust Allocator 期望收到的 tau_cmd 在可行域内
    # 如果超出，Allocator 的优化可能不收敛
    
    # 简单的分量限幅
    tau_cmd.F_x = clamp(tau_cmd.F_x, -capacity.F_x_max, capacity.F_x_max)
    tau_cmd.F_y = clamp(tau_cmd.F_y, -capacity.F_y_max, capacity.F_y_max)
    tau_cmd.M_z = clamp(tau_cmd.M_z, -capacity.M_z_max, capacity.M_z_max)
    
    RETURN tau_cmd
```

---

## 9. 坐标系变换

Force Calculator 的输出 τ_cmd 在船体坐标系（Body Frame）中定义：F_x 沿船首方向，F_y 沿左舷方向，M_z 绕船体垂直轴。Thrust Allocator 也在船体坐标系中工作——因此不需要坐标系变换。

---

## 10. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Force Synthesizer | 合成 τ_cmd 三自由度向量 | C++ |
| Lateral Force Calculator | 横向力计算（低速时） | C++ |
| Priority Arbiter | 优先级仲裁和资源分配 | C++ |
| Capacity Checker | 能力检测和预处理 | C++ |

---

## 11. 数值算例

### 正常巡航

```
F_x = 3200 N（维持 8 m/s）
M_z = 500 Nm（微量航向修正）
F_y = 0 N

P_est = 3200 × 8 + 500 × 0.01 = 25.6 kW（远未饱和）
→ 直接输出，不缩放
```

### 紧急避碰转弯

```
F_x = 2000 N（维持速度）
M_z = 15000 Nm（急转弯）
F_y = 0 N

P_est = 2000 × 8 + 15000 × 0.15 = 16 + 2.25 = 18.25 kW
→ 假设 P_max = 20 kW，接近但未超限
→ 如果 M_z_max = 12000 Nm，M_z 超限
→ 优先级仲裁：航向优先，M_z = 12000 Nm（限幅）
→ 剩余功率给速度：F_x = (20000 - 12000×0.15) / 8 = 2250 / 8 ≈ 280 N
→ 速度会降低（可接受——避碰优先）
```

---

## 12. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 航向优先级权重 | 0.6 | 正常模式 |
| 速度优先级权重 | 0.3 | |
| 横向优先级权重 | 0.1 | |
| 紧急航向优先级 | 0.9 | 紧急避碰 |
| 横向力增益 K_lateral | 2000 N/(m/s) | 低速模式 |
| F_y_max | 待标定 | 侧推器最大推力 |

---

## 13. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Heading Controller → Force Calc | 输入 | M_z_demand |
| Speed Controller → Force Calc | 输入 | F_x_demand |
| Drift Correction → Force Calc | 输入 | 横向偏流量（低速时） |
| Mode Switcher → Force Calc | 输入 | 当前控制模式 |
| Force Calc → Thrust Allocator | 输出 | τ_cmd = [F_x, F_y, M_z] |
| Force Calc → Speed/Heading Ctrl | 反馈 | 饱和状态（用于抗饱和） |

---

## 14. 测试场景与验收标准

| 场景 | 验收标准 |
|------|---------|
| FRC-001 正常巡航合成 | τ_cmd 正确，无缩放 |
| FRC-002 高需求饱和 | 优先级正确应用 |
| FRC-003 紧急模式 | 90% 资源给航向 |
| FRC-004 低速横向力 | F_y 正确计算 |
| FRC-005 零速 DP 模式 | 三自由度全激活 |
| 计算延迟 | < 0.1 ms |

---

## 15. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen 2021 | 力/力矩分配理论 |
| [2] | Sørensen 2011 "Marine Control Systems" | DP 控制系统设计 |

---

**文档结束**
