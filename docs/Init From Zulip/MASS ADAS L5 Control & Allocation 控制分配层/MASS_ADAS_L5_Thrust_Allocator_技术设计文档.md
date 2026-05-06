# MASS_ADAS Thrust Allocator 推力分配器技术设计文档

**文档编号：** SANGO-ADAS-TAL-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 轮机长/操船者的推力分配思维模型
5. 推力分配数学框架
6. 推力矩阵 T(α) 的构建
7. 执行机构建模
8. 约束优化求解
9. Transit 模式分配策略
10. Maneuvering 模式分配策略
11. 混合模式分配（TRANSITION）
12. 禁止区域处理
13. 功率最优分配
14. 故障降级下的推力重分配
15. 内部子模块分解
16. 数值算例
17. 参数总览表
18. 与其他模块的协作关系
19. 测试策略与验收标准
20. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Thrust Allocator（推力分配器）是 Layer 5 的核心工程模块——它将 Force Calculator 输出的抽象力/力矩需求 τ_cmd = [F_x, F_y, M_z]ᵀ 分配到具体的物理执行机构（螺旋桨、舵、侧推器），输出每个执行机构的具体指令（转速、舵角、侧推功率）。

这是整个控制链中"从数学世界到物理世界"的关键转换节点。上游的控制器只关心"需要多大的力和力矩"，不关心这些力矩如何产生。Thrust Allocator 负责解决"用什么执行机构、以什么组合来产生这些力矩"的问题。

### 1.2 核心职责

- **推力分配计算：** 求解约束优化问题，将力需求分配到各执行机构。
- **执行机构建模：** 精确建模每个执行机构的推力特性、效率和限制。
- **功率优化：** 在满足力需求的前提下，最小化总功率消耗。
- **故障重分配：** 当某个执行机构故障时，自动将其份额重新分配到其他执行机构。

### 1.3 SANGO USV 的执行机构配置

根据 MASS_ADAS 的 USV 平台，假设以下典型执行机构配置：

| 执行机构 | 数量 | 位置 | 产生的力/力矩 |
|---------|------|------|-------------|
| 主螺旋桨 | 2（左/右） | 船尾，距中线 ±d_prop | F_x（前进推力）+ M_z（差动力矩） |
| 舵 | 1（或 2） | 螺旋桨后方 | F_y（侧向力）+ M_z（偏转力矩） |
| 侧推器（可选） | 1 | 船首 | F_y（侧向力）+ M_z（偏转力矩） |

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| τ_cmd = [F_x, F_y, M_z]ᵀ | Force Calculator | 50 Hz | 三自由度力/力矩需求 |
| 当前控制模式 | Mode Switcher | 10 Hz | TRANSIT/MANEUVERING/TRANSITION |
| 本船速度 u | Nav Filter | 50 Hz | 用于计算速度相关的推力特性 |
| 执行机构状态 | Actuator Limiter / 硬件 | 10 Hz | 各执行机构当前位置/转速/可用性 |

### 2.2 输出

```
ThrustAllocation:
    Header header
    
    # ---- 螺旋桨指令 ----
    float64 prop_port_rpm           # 左螺旋桨转速指令（rps，正=前进）
    float64 prop_stbd_rpm           # 右螺旋桨转速指令（rps）
    
    # ---- 舵指令 ----
    float64 rudder_angle            # 舵角指令（弧度，正=右舵）
    
    # ---- 侧推器指令 ----
    float64 bow_thruster_force      # 侧推器推力指令（N，正=向左推）
    
    # ---- 分配状态 ----
    float64 allocation_error_norm   # 分配误差范数（τ_cmd - τ_achieved 的范数）
    bool allocation_feasible        # 分配是否可行
    float64 power_total_kw          # 总功率消耗（kW）
    string allocation_method        # "qp_optimal"/"pseudo_inverse"/"fixed_structure"/"degraded"
```

---

## 3. 核心参数数据库

### 3.1 执行机构几何参数

| 参数 | 符号 | 说明 |
|------|------|------|
| 左螺旋桨横向偏移 | y_prop_port | 距中线距离（米，正=左舷） |
| 右螺旋桨横向偏移 | y_prop_stbd | 距中线距离（米，负=右舷） |
| 螺旋桨纵向位置 | x_prop | 距重心纵向距离（米，负=船尾） |
| 舵纵向位置 | x_rudder | 距重心纵向距离（米） |
| 侧推器纵向位置 | x_bow_thruster | 距重心（米，正=船首） |
| 螺旋桨直径 | D_prop | 米 |
| 舵面积 | A_rudder | m² |

### 3.2 螺旋桨特性参数

| 参数 | 说明 |
|------|------|
| K_T(J) | 推力系数——进速比 J 的函数（从敞水特性图） |
| K_Q(J) | 扭矩系数——进速比 J 的函数 |
| n_max | 最大转速（rps） |
| n_min | 最低稳定转速（rps） |
| η_prop(J) | 螺旋桨效率 |

**简化推力模型：**

```
# 螺旋桨推力
F_prop = ρ × n² × D⁴ × K_T(J)

# 进速比
J = V_a / (n × D)
# V_a = 前进速度（考虑伴流扣减）
# V_a ≈ u × (1 - w)，w 为伴流分数（典型 0.1~0.3）

# 简化线性模型（用于控制器设计）
F_prop ≈ K_n × n × |n|    # 二次模型：推力 ∝ n²
# K_n 为推力常数（N/rps²），从海试标定
```

### 3.3 舵力模型参数

```
# 舵侧向力
F_rudder_y = 0.5 × ρ × V² × A_rudder × C_L(δ)

# 升力系数近似（线性区）
C_L(δ) ≈ C_L_alpha × δ    # δ 为舵角（弧度），C_L_alpha ≈ 3.0~6.0 /rad

# 舵产生的偏转力矩
M_rudder = F_rudder_y × |x_rudder|

# 舵的失速角
δ_stall ≈ 15°~25°    # 超过此角度升力下降
δ_max = 35°          # 机械限位
```

### 3.4 侧推器参数

```
# 侧推器标称推力
F_bow_nominal = 待标定（N）

# 速度衰减系数
# 高速时侧推效率急剧下降（因为来流冲刷推力射流）
k_bow_speed(V) = max(0, 1 - (V / V_BOW_CUTOFF)²)
F_bow_effective = F_bow_nominal × k_bow_speed(V)

V_BOW_CUTOFF = 4.0    # m/s——高于此侧推几乎无效
```

---

## 4. 轮机长/操船者的推力分配思维模型

### 4.1 人类操船者怎么"分配推力"

一个经验丰富的操船者（如领港员）在不同场景下的"推力分配"方式完全不同：

**高速直行：** "主机定速前进，舵控航向。"——两台主机同速（F_x 均分），舵角修正航向偏差。

**高速转弯：** "舵打到 15°，内侧主机减速 10%。"——主要靠舵产生偏转力矩，但内侧减速辅助（差动辅助）。

**低速转弯：** "左进右退！"——差动推力为主，舵基本无用。

**靠泊横移：** "侧推向码头方向推，主机微进微退保持位置。"——侧推器产生横向力。

Thrust Allocator 的模式切换和分配矩阵变化精确对应了以上这些操作方式。

---

## 5. 推力分配数学框架

### 5.1 问题定义

推力分配问题的数学本质是：

给定力需求 τ_cmd ∈ ℝ³ 和执行机构向量 u ∈ ℝᵐ（m 个执行机构的指令），找到 u 使得：

```
T(u) × u = τ_cmd        # 推力矩阵方程
u_min ≤ u ≤ u_max       # 执行机构限制
du/dt ≤ du_max           # 变化率限制
```

其中 T ∈ ℝ³ˣᵐ 是推力矩阵——描述每个执行机构的力/力矩对船体的贡献。

### 5.2 推力矩阵

对于双螺旋桨 + 单舵 + 单侧推器的配置：

```
执行机构向量：
u = [n_port, n_stbd, δ, F_bow]ᵀ

推力矩阵（简化形式）：
        ┌                                               ┐
        │  K_n×n_p    K_n×n_s    -C_drag(δ)      0       │   ← F_x
T(u) =  │  0          0          C_L(δ)×V²     1.0      │   ← F_y
        │  -K_n×n_p×d  K_n×n_s×d  C_L(δ)×V²×x_r  x_b    │   ← M_z
        └                                               ┘

其中：
  K_n = 螺旋桨推力常数
  d = 螺旋桨到中线距离
  C_L(δ) = 舵升力系数
  x_r = 舵到重心距离
  x_b = 侧推器到重心距离
```

**注意：** 推力矩阵中某些元素依赖于速度 V 和舵角 δ——所以 T 是状态相关的。这使得推力分配变成一个非线性问题。

---

## 6. 推力矩阵 T(α) 的构建

### 6.1 每个执行机构的力/力矩贡献

```
FUNCTION build_thrust_matrix(V, config):
    
    # 左螺旋桨
    # 贡献：F_x = K_n × n²（前进推力），M_z = -K_n × n² × d_port（力矩，左舷为正）
    T_prop_port = {
        F_x: lambda n: K_N × n × abs(n),
        F_y: 0,
        M_z: lambda n: -K_N × n × abs(n) × config.d_prop_port
    }
    
    # 右螺旋桨（对称，M_z 方向相反）
    T_prop_stbd = {
        F_x: lambda n: K_N × n × abs(n),
        F_y: 0,
        M_z: lambda n: K_N × n × abs(n) × config.d_prop_stbd
    }
    
    # 舵
    T_rudder = {
        F_x: lambda δ: -0.5 × ρ × V² × A_rudder × C_D(δ),   # 舵阻力（小量）
        F_y: lambda δ: 0.5 × ρ × V² × A_rudder × C_L(δ),     # 舵侧向力
        M_z: lambda δ: 0.5 × ρ × V² × A_rudder × C_L(δ) × config.x_rudder
    }
    
    # 侧推器
    k_speed = max(0, 1 - (V / V_BOW_CUTOFF)²)
    T_bow = {
        F_x: 0,
        F_y: lambda f: f × k_speed,
        M_z: lambda f: f × k_speed × config.x_bow_thruster
    }
    
    RETURN {T_prop_port, T_prop_stbd, T_rudder, T_bow}
```

---

## 7. 执行机构建模

### 7.1 螺旋桨

```
struct PropellerModel {
    float64 K_N;           # 推力常数 N/rps²
    float64 K_Q;           # 扭矩常数 Nm/rps²
    float64 n_max;         # 最大转速 rps
    float64 n_min;         # 最低稳定转速 rps
    float64 d_offset;      # 横向偏移 m
    float64 x_offset;      # 纵向偏移 m
    float64 wake_fraction; # 伴流分数
    float64 efficiency(J); # 效率曲线
};
```

### 7.2 舵

```
struct RudderModel {
    float64 area;          # 舵面积 m²
    float64 x_offset;      # 纵向偏移 m
    float64 CL_alpha;      # 升力线斜率 /rad
    float64 delta_stall;   # 失速角 rad
    float64 delta_max;     # 机械极限角 rad
    float64 delta_rate_max; # 最大转速 rad/s
};
```

### 7.3 侧推器

```
struct BowThrusterModel {
    float64 F_nominal;     # 标称推力 N
    float64 x_offset;      # 纵向偏移 m
    float64 V_cutoff;      # 速度截止值 m/s
    float64 startup_time;  # 启动时间 s
    float64 power_max;     # 最大功率 kW
};
```

---

## 8. 约束优化求解

### 8.1 优化问题定义

```
minimize    J(u) = uᵀ W u                    # 最小化功率消耗（加权）
subject to  T(u) × u = τ_cmd                 # 力/力矩平衡
            u_min ≤ u ≤ u_max                 # 执行机构限制
            |du/dt| ≤ du_max                  # 变化率限制
```

其中 W 是功率权重对角矩阵——消耗功率大的执行机构（如主螺旋桨）权重高，消耗小的（如舵）权重低。

### 8.2 线性化求解（简化方法）

对于实时控制（50 Hz），完整的非线性优化可能太慢。可以将推力矩阵在当前工作点线性化：

```
FUNCTION linear_allocation(tau_cmd, T_linear, W, u_limits):
    
    # 加权伪逆法
    # u = T' × (T × T')⁻¹ × τ_cmd
    # 其中 T' = W⁻¹ × Tᵀ（加权伪逆）
    
    T_weighted = inv(W) × T_linear.T
    u_optimal = T_weighted × inv(T_linear × T_weighted) × tau_cmd
    
    # 限幅
    u_limited = clamp(u_optimal, u_limits.min, u_limits.max)
    
    # 计算限幅后的实际力（可能不等于 τ_cmd）
    tau_achieved = T_linear × u_limited
    allocation_error = norm(tau_cmd - tau_achieved)
    
    RETURN {u_limited, tau_achieved, allocation_error}
```

### 8.3 二次规划求解（推荐方法）

```
FUNCTION qp_allocation(tau_cmd, T_linear, W, u_limits, u_prev, dt):
    
    # QP 问题：
    # min  uᵀ W u + s'ᵀ Q s'            # 功率 + 分配误差惩罚
    # s.t. T × u + s' = τ_cmd            # s' 为松弛变量（允许小误差）
    #      u_min ≤ u ≤ u_max
    #      |u - u_prev| ≤ du_max × dt    # 变化率约束
    
    # 使用 OSQP 或 qpOASES 求解器
    solution = solve_qp(W, T_linear, tau_cmd, u_limits, rate_limits)
    
    RETURN solution

# QP 求解时间典型值：< 0.5 ms（对于 4 个执行机构）
```

---

## 9. Transit 模式分配策略

### 9.1 简化结构

在 Transit 模式下，侧推器关闭，推力分配简化为：

```
执行机构：u = [n_port, n_stbd, δ]

F_x ≈ K_N × (n_port² + n_stbd²)
M_z ≈ K_N × (n_stbd² - n_port²) × d + F_rudder_y(δ) × x_rudder
F_y ≈ F_rudder_y(δ)
```

### 9.2 Transit 模式的典型分配

```
FUNCTION transit_allocation(F_x, M_z, V, ship_config):
    
    # 1. 舵角由 M_z 需求决定（主要力矩来源）
    F_rudder_y_needed = M_z / ship_config.x_rudder
    
    # 反推舵角
    IF V > 1.0:
        C_L_needed = F_rudder_y_needed / (0.5 × ρ × V² × ship_config.A_rudder)
        δ = C_L_needed / ship_config.CL_alpha
        δ = clamp(δ, -ship_config.delta_max, ship_config.delta_max)
    ELSE:
        δ = 0    # 低速时舵无效
    
    # 2. 螺旋桨转速由 F_x 需求决定
    # 两台主机等速（或微量差动辅助航向）
    F_x_per_prop = F_x / 2
    n_base = sign(F_x_per_prop) × sqrt(abs(F_x_per_prop) / ship_config.K_N)
    
    # 3. 差动微调辅助航向（如果舵角已接近极限）
    IF abs(δ) > ship_config.delta_max × 0.8:
        # 舵角接近极限——用差动推力补充
        M_z_remaining = M_z - compute_rudder_moment(δ, V, ship_config)
        dn = sqrt(abs(M_z_remaining / (2 × ship_config.K_N × ship_config.d_prop))) × sign(M_z_remaining)
        n_port = n_base - dn
        n_stbd = n_base + dn
    ELSE:
        n_port = n_base
        n_stbd = n_base
    
    # 限幅
    n_port = clamp(n_port, -ship_config.n_max, ship_config.n_max)
    n_stbd = clamp(n_stbd, -ship_config.n_max, ship_config.n_max)
    
    RETURN {n_port, n_stbd, δ, F_bow: 0}
```

---

## 10. Maneuvering 模式分配策略

### 10.1 简化结构

在 Maneuvering 模式下，舵基本无效，推力分配为：

```
执行机构：u = [n_port, n_stbd, F_bow]

F_x ≈ K_N × (n_port² + n_stbd²) × sign(n)
M_z ≈ K_N × (n_stbd² - n_port²) × d + F_bow × x_bow
F_y ≈ F_bow × k_speed(V)
```

### 10.2 Maneuvering 模式的典型分配

```
FUNCTION maneuvering_allocation(F_x, F_y, M_z, V, ship_config):
    
    # 1. 侧推器负责 F_y
    F_bow = F_y / max(k_bow_speed(V), 0.1)
    F_bow = clamp(F_bow, -F_BOW_MAX, F_BOW_MAX)
    
    # 侧推器也产生力矩——扣除这部分
    M_z_from_bow = F_bow × k_bow_speed(V) × ship_config.x_bow_thruster
    M_z_remaining = M_z - M_z_from_bow
    
    # 2. 差动推力负责 M_z
    # M_z = K_N × (n_s² - n_p²) × d = K_N × (n_s + n_p)(n_s - n_p) × d
    # 设 n_avg = (n_p + n_s) / 2, dn = (n_s - n_p) / 2
    # M_z = 4 × K_N × n_avg × dn × d
    
    # 3. 平均转速由 F_x 决定
    F_x_per_prop = F_x / 2
    n_avg = sign(F_x_per_prop) × sqrt(abs(F_x_per_prop) / ship_config.K_N)
    
    # 差动量由 M_z 决定
    IF abs(n_avg) > 0.1:
        dn = M_z_remaining / (4 × ship_config.K_N × abs(n_avg) × ship_config.d_prop)
    ELSE:
        # 零速时纯差动转弯
        dn_squared = abs(M_z_remaining) / (2 × ship_config.K_N × ship_config.d_prop)
        dn = sign(M_z_remaining) × sqrt(dn_squared)
        n_avg = 0
    
    n_port = n_avg - dn
    n_stbd = n_avg + dn
    
    # 限幅
    n_port = clamp(n_port, -ship_config.n_max, ship_config.n_max)
    n_stbd = clamp(n_stbd, -ship_config.n_max, ship_config.n_max)
    
    # 舵设为零（低速时无效）
    δ = 0
    
    RETURN {n_port, n_stbd, δ, F_bow}
```

---

## 11. 混合模式分配（TRANSITION）

在 Mode Switcher 的 TRANSITION 状态下，按 blend_factor 混合两种分配结果：

```
FUNCTION transition_allocation(tau_cmd, V, blend_factor, ship_config):
    
    # 分别计算两种模式的分配
    alloc_transit = transit_allocation(tau_cmd.F_x, tau_cmd.M_z, V, ship_config)
    alloc_maneuver = maneuvering_allocation(tau_cmd.F_x, tau_cmd.F_y, tau_cmd.M_z, V, ship_config)
    
    # 按 blend_factor 混合（blend=1 为全 Transit，blend=0 为全 Maneuvering）
    n_port = blend_factor × alloc_transit.n_port + (1 - blend_factor) × alloc_maneuver.n_port
    n_stbd = blend_factor × alloc_transit.n_stbd + (1 - blend_factor) × alloc_maneuver.n_stbd
    δ = blend_factor × alloc_transit.δ + (1 - blend_factor) × 0
    F_bow = (1 - blend_factor) × alloc_maneuver.F_bow
    
    RETURN {n_port, n_stbd, δ, F_bow}
```

---

## 12. 禁止区域处理

某些执行机构有"禁止区域"——不允许在某些指令范围内运行（比如螺旋桨在接近零转速时可能有振动区域）。

```
FUNCTION apply_forbidden_zones(n, forbidden_zones):
    
    FOR EACH zone IN forbidden_zones:
        IF zone.n_low < n < zone.n_high:
            # 在禁止区域内——推到最近的边界
            IF abs(n - zone.n_low) < abs(n - zone.n_high):
                n = zone.n_low
            ELSE:
                n = zone.n_high
    
    RETURN n
```

---

## 13. 功率最优分配

功率消耗与转速的三次方近似成正比。最优分配应使两台螺旋桨的转速尽可能接近（相同转速下总功率最低）。

```
# 在满足 F_x 和 M_z 的前提下，最小化 n_port³ + n_stbd³
# 这正是 QP 中权重矩阵 W 的作用——W 与功率成正比
```

---

## 14. 故障降级下的推力重分配

### 14.1 单侧螺旋桨故障

```
FUNCTION single_prop_failure(tau_cmd, failed_side, ship_config):
    
    # 只有一台螺旋桨可用
    IF failed_side == "port":
        working_prop = "stbd"
    ELSE:
        working_prop = "port"
    
    # F_x 全部由工作侧承担（最大推力减半）
    n = sign(tau_cmd.F_x) × sqrt(abs(tau_cmd.F_x) / ship_config.K_N)
    n = clamp(n, -ship_config.n_max, ship_config.n_max)
    
    # M_z 由舵 + 侧推器承担
    # 注意：单侧推力本身会产生一个偏转力矩——需要舵来补偿
    M_z_prop_offset = K_N × n × abs(n) × ship_config.d_prop × direction_sign
    M_z_for_rudder = tau_cmd.M_z - M_z_prop_offset
    
    δ = compute_rudder_angle_for_moment(M_z_for_rudder, V, ship_config)
    
    RETURN {n_working: n, n_failed: 0, δ, F_bow: 0}
```

### 14.2 舵故障

```
# 舵故障——纯差动控制航向（Maneuvering 模式）
# Mode Switcher 会自动切换到 DEGRADED_NO_RUDDER
# Thrust Allocator 使用 maneuvering_allocation 但 δ = 0 固定
```

---

## 15. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Thrust Matrix Builder | 构建当前状态下的推力矩阵 | C++ |
| QP Solver | 二次规划求解器 | C++（OSQP/qpOASES） |
| Transit Allocator | Transit 模式的简化分配 | C++ |
| Maneuver Allocator | Maneuvering 模式的差动+侧推分配 | C++ |
| Transition Blender | 过渡模式的混合计算 | C++ |
| Forbidden Zone Handler | 禁止区域处理 | C++ |
| Failure Reallocator | 故障后的推力重分配 | C++ |

---

## 16. 数值算例

### 16.1 Transit 模式——巡航直行

```
τ_cmd = [F_x=3200N, F_y=0, M_z=500Nm]
V = 8 m/s, d_prop = 1.5m, K_N = 50 N/rps², x_rudder = 5m

舵角：
  F_rudder_y = 500 / 5 = 100 N
  C_L = 100 / (0.5 × 1025 × 64 × 0.3) = 0.010
  δ = 0.010 / 4.0 = 0.0025 rad = 0.14° → 极小舵角

螺旋桨：
  F_x/prop = 3200 / 2 = 1600 N
  n = sqrt(1600 / 50) = sqrt(32) = 5.66 rps

→ 两台主机 5.66 rps，舵角 0.14°
```

### 16.2 Maneuvering 模式——低速转弯

```
τ_cmd = [F_x=500N, F_y=200N, M_z=3000Nm]
V = 2 m/s, d_prop = 1.5m, K_N = 50, x_bow = 5m

侧推器：F_bow = 200 / k_speed(2) = 200 / 0.75 = 267 N
M_z_from_bow = 267 × 0.75 × 5 = 1001 Nm
M_z_remaining = 3000 - 1001 = 1999 Nm

差动：
  n_avg = sqrt(250/50) = 2.24 rps
  dn = 1999 / (4 × 50 × 2.24 × 1.5) = 1999 / 672 = 2.97 rps

  n_port = 2.24 - 2.97 = -0.73 rps（微倒车！）
  n_stbd = 2.24 + 2.97 = 5.21 rps

→ 右机大前进 5.21 rps，左机微倒车 -0.73 rps，侧推 267N
→ 典型的低速差动+侧推转弯
```

---

## 17. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 推力常数 K_N | 50 N/rps² | 待海试标定 |
| 螺旋桨偏移 d_prop | 1.5 m | 实测 |
| 舵面积 | 0.3 m² | 实测 |
| 舵到重心距离 | 5.0 m | 实测 |
| 舵升力线斜率 | 4.0 /rad | 待 CFD 或标定 |
| 舵失速角 | 25° | |
| 舵最大角 | 35° | 机械限制 |
| 侧推器标称推力 | 待标定 N | |
| 侧推器速度截止 | 4.0 m/s | |
| 侧推器位置 | 5.0 m（船首） | 实测 |
| 螺旋桨最大转速 | 待标定 rps | |
| QP 求解器 | OSQP | 推荐 |

---

## 18. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Force Calculator → Thrust Alloc | 输入 | τ_cmd = [F_x, F_y, M_z] |
| Mode Switcher → Thrust Alloc | 输入 | 当前模式 + blend_factor |
| Nav Filter → Thrust Alloc | 输入 | 速度 u（用于速度相关推力特性） |
| Thrust Alloc → Actuator Limiter | 输出 | [n_port, n_stbd, δ, F_bow] |
| System Monitor → Thrust Alloc | 输入 | 执行机构可用性 |

---

## 19. 测试场景与验收标准

| 场景 | 验收标准 |
|------|---------|
| TAL-001 Transit 巡航 | 分配误差 < 1% |
| TAL-002 Transit 转弯（舵 25°） | 舵角+差动协调 |
| TAL-003 Maneuvering 差动转弯 | 力矩精度 < 5% |
| TAL-004 Maneuvering 横移 | F_y 精度 < 10% |
| TAL-005 Transition 混合 | 平滑过渡无跳变 |
| TAL-006 单侧螺旋桨故障 | 降级分配可行 |
| TAL-007 舵故障 | 差动接管航向 |
| TAL-008 QP 求解时间 | < 0.5 ms |
| TAL-009 饱和时优先级 | 航向优先满足 |
| TAL-010 禁止区域 | 转速避开禁区 |

---

## 20. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen 2021 Ch.12 | 推力分配理论 |
| [2] | Johansen & Fossen "Control Allocation" Automatica, 2013 | 约束优化分配 |
| [3] | Stellato et al. "OSQP" Mathematical Programming, 2020 | QP 求解器 |
| [4] | Sørensen 2011 | DP 系统推力分配 |

---

## v3.0 架构升级：全回转推进器支持与电力管理接口

### A. 全回转推进器（Azimuth Thruster）支持

全回转推进器可以 360° 旋转，推力方向不再固定——推力方向角 α 本身也成为优化变量。这从根本上改变了推力分配的数学结构。

**固定推进器的推力配置矩阵：** 每个推进器的力臂和方向是常数。

```
# 固定螺旋桨 + 舵 + 侧推器：
T(f) = B × f
B = [[1,    0,   0],       # 螺旋桨纵向
     [0, sin(δ), 1],       # 舵横向 + 侧推横向
     [0, l_r×sin(δ), l_b]] # 舵力矩 + 侧推力矩
f = [T_prop, F_rudder, F_bow]
```

**全回转推进器的推力配置矩阵：** 方向 α_i 是变量——B 矩阵本身依赖于优化变量。

```
# 两台全回转推进器（如 80m MAV 配置）：
T(f, α) = Σ_i [[cos(α_i)], [sin(α_i)], [l_xi×sin(α_i) - l_yi×cos(α_i)]] × f_i

优化问题变为：
  minimize  Σ f_i²    （最小化总推力——节能）
  subject to:
    T(f, α) = τ_cmd   （满足力/力矩需求）
    0 ≤ f_i ≤ f_max   （推力范围约束）
    |dα_i/dt| ≤ α_rate_max  （方位角变化率约束——全回转不能瞬间转 180°）
    f_i × α_i 不产生禁止区组合  （某些角度下推进器间互相干扰）
```

### B. 参数化推进器配置

通过 YAML 配置文件定义推进器布局，不硬编码到代码中：

```yaml
# thruster_config_usv_12m.yaml — 12m USV（固定桨+舵+侧推）
thrusters:
  - id: "main_propeller"
    type: "fixed_pitch"
    position: {x: -5.0, y: 0.0}    # 船尾中心
    direction: 0                     # 固定朝前
    max_thrust_N: 3000
    min_thrust_N: -500               # 倒车
    max_rate_N_per_s: 500
    
  - id: "rudder"
    type: "rudder"
    position: {x: -5.5, y: 0.0}    # 舵在螺旋桨后方
    max_angle_deg: 35
    max_rate_deg_per_s: 5
    area_m2: 0.15
    
  - id: "bow_thruster"
    type: "tunnel"
    position: {x: 4.5, y: 0.0}     # 船首
    direction: 90                    # 横向
    max_thrust_N: 500

# thruster_config_mav_80m.yaml — 80m MAV（双全回转）
thrusters:
  - id: "azimuth_port"
    type: "azimuth"
    position: {x: -35.0, y: -5.0}  # 左舷船尾
    max_thrust_N: 150000
    max_rate_N_per_s: 10000
    max_azimuth_rate_deg_per_s: 10  # 方位角变化率
    forbidden_zones: [{from: 160, to: 200}]  # 互相干扰的方位角范围
    
  - id: "azimuth_stbd"
    type: "azimuth"
    position: {x: -35.0, y: 5.0}   # 右舷船尾
    max_thrust_N: 150000
    max_rate_N_per_s: 10000
    max_azimuth_rate_deg_per_s: 10
    forbidden_zones: [{from: 160, to: 200}]
    
  - id: "bow_tunnel"
    type: "tunnel"
    position: {x: 30.0, y: 0.0}
    direction: 90
    max_thrust_N: 80000
```

### C. 全回转的 QP 求解

```
FUNCTION allocate_azimuth_thrusters(tau_cmd, thruster_config, prev_state):
    
    # 使用序列二次规划（SQP）求解非线性约束优化
    # 在每次迭代中将非线性约束线性化为 QP 子问题
    
    n_thrusters = len(thruster_config)
    
    # 优化变量：[f_1, α_1, f_2, α_2, ...]（推力 + 方位角）
    x0 = prev_state    # 从上一步的解开始（热启动）
    
    FOR iteration IN range(MAX_SQP_ITERATIONS):
        # 在当前 x 处线性化推力配置矩阵
        B = compute_thrust_matrix(x0, thruster_config)
        dB_dalpha = compute_thrust_matrix_jacobian(x0, thruster_config)
        
        # QP 子问题
        # minimize  (f - f_prev)^T W (f - f_prev)  + penalty × ||B×f - tau_cmd||²
        # subject to  f_min ≤ f ≤ f_max
        #             |α - α_prev| / dt ≤ rate_max
        #             α NOT IN forbidden_zones
        
        result = solve_qp(B, dB_dalpha, tau_cmd, constraints, x0)
        
        IF converged(result, x0):
            BREAK
        
        x0 = result
    
    RETURN {
        thrusts: result.f,
        azimuths: result.alpha
    }
```

### D. 电力管理接口（Power Management Interface）

```
FUNCTION apply_power_limit(thrust_allocation, available_power_kW):
    
    # 计算当前推力分配的总功率需求
    total_power = 0
    FOR EACH thruster IN thrust_allocation:
        power = estimate_thruster_power(thruster.thrust, thruster.rpm)
        total_power += power
    
    IF total_power > available_power_kW:
        # 功率超限——按优先级降低推力
        # 优先级：航向控制 > 位置保持 > 速度
        scale_factor = available_power_kW / total_power
        
        FOR EACH thruster IN thrust_allocation:
            thruster.thrust *= scale_factor
        
        log_event("power_limited", {
            demanded: total_power,
            available: available_power_kW,
            scale: scale_factor
        })
    
    RETURN thrust_allocation
```

---

## v3.1 补充升级：低速推力特性补偿 + 近场推力方向约束

### A. 螺旋桨偏转效应补偿（Propeller Walk / Transverse Thrust）

单螺旋桨或双螺旋桨在低速运转（特别是后退）时，螺旋桨的上下叶片在不同深度产生不对称推力，导致船尾向一侧偏转。右旋螺旋桨后退时船尾向左偏（即船首向右偏）。

```
FUNCTION compensate_prop_walk(rpm_cmd, thruster_config):
    
    IF abs(rpm_cmd) > LOW_SPEED_RPM_THRESHOLD:
        RETURN 0    # 高速时偏转效应可忽略
    
    IF rpm_cmd >= 0:
        # 前进——偏转效应较小
        walk_force = thruster_config.prop_walk_coeff_fwd * rpm_cmd ** 2
    ELSE:
        # 后退——偏转效应显著
        walk_force = thruster_config.prop_walk_coeff_rev * rpm_cmd ** 2
        # 方向取决于螺旋桨旋转方向
        IF thruster_config.rotation == "RIGHT_HAND":
            walk_force = -walk_force    # 船尾向左→需要向右补偿
    
    # 将偏转力作为额外的 Fy 需求加入推力分配
    RETURN walk_force

LOW_SPEED_RPM_THRESHOLD = 300    # RPM——低于此值才需要补偿
```

### B. 近场推力方向约束

当 Risk Monitor 检测到障碍物进入限制区（G3 近场安全区），Thrust Allocator 接收运动方向约束——禁止向障碍物方向分配推力。

```
FUNCTION apply_proximity_constraints(Fx, Fy, Mz, thrust_constraints):
    
    IF thrust_constraints IS NULL:
        RETURN Fx, Fy, Mz    # 无约束
    
    blocked_dir = thrust_constraints.blocked_direction    # 被阻断的方向（弧度）
    
    # 将 Fx, Fy 投影到 blocked_direction
    force_toward = Fx * cos(blocked_dir) + Fy * sin(blocked_dir)
    
    IF force_toward > 0:
        # 正在向障碍物方向推——移除该分量
        Fx -= force_toward * cos(blocked_dir)
        Fy -= force_toward * sin(blocked_dir)
        
        log_event("thrust_blocked", {
            direction: blocked_dir,
            removed_force: force_toward
        })
    
    # Mz（艏摇）不限制——允许旋转
    
    RETURN Fx, Fy, Mz
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v3.0 升级：全回转 QP + Power Mgmt |
| 0.3 | 2026-04-26 | 架构组 | v3.1 补充：增加螺旋桨偏转效应补偿（Prop Walk）；增加近场推力方向约束（配合 Risk Monitor 近场安全区） |

---

**文档结束**
