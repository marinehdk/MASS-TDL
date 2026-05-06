# MASS_ADAS Speed Controller 速度控制器技术设计文档

**文档编号：** SANGO-ADAS-SPD-CTL-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考——待海试数据标定

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长/轮机长的速度控制思维模型
5. 计算流程总览
6. 步骤一：速度误差计算
7. 步骤二：PID 速度控制器设计
8. 步骤三：增益调度（速度域+工况域）
9. 步骤四：积分项管理
10. 步骤五：前馈补偿（阻力前馈）
11. 步骤六：输出限幅与速率限制
12. 加速控制策略
13. 减速控制策略
14. 停船控制策略
15. 浅水与受限水域的速度控制
16. 速度-航向耦合处理
17. 主机保护与操纵限制
18. 故障降级策略
19. 内部子模块分解
20. 数值算例
21. 参数总览表
22. 与其他模块的协作关系
23. 测试策略与验收标准
24. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Speed Controller（速度控制器）是 Layer 5（Control & Allocation Layer）的核心控制模块之一，与 Heading Controller 并列。它接收 Guidance 层（L4）输出的期望速度 u_cmd，结合本船当前实际纵荡速度 u，计算所需的纵向推力 F_x，使船的实际速度精确跟踪期望速度。

Speed Controller 等价于传统船舶上轮机长对主机转速/功率的控制——船长下令"前进三"（Full Ahead），轮机长控制主机转速使船加速到对应航速。在 MASS_ADAS 中，这个过程完全自动化。

### 1.2 核心职责

- **速度跟踪：** 使船的实际对水速度 u 跟踪期望速度 u_cmd，稳态误差趋近于零。
- **加减速控制：** 在速度变化时，以合适的加速度平稳过渡，不超过推进系统的能力包络。
- **阻力补偿：** 在恒定航速时，自动调整推力以抵消水阻力、风阻力和海流阻力。
- **推力需求输出：** 输出所需的纵向推力 F_x（N），由下游的 Force Calculator 和 Thrust Allocator 负责分配到具体执行机构。
- **主机保护：** 确保推力需求不超过主机的安全运行包络——不超速、不超载、不超温。

### 1.3 设计原则

- **平稳过渡原则：** 速度变化应平滑。急加速浪费燃油且应力大，急减速可能导致螺旋桨空转或发动机过载。
- **效率优先原则：** 在满足跟踪精度的前提下，推力应尽可能接近理论最优值——即恰好等于当前速度下的总阻力。
- **主机安全原则：** 推力需求不得超过主机的额定功率/转速包络，不得要求主机做超出其机械限制的动作（如极短时间内从满前进切换到满倒车）。
- **与航向控制协调原则：** 速度和航向控制共享相同的推进系统资源（螺旋桨推力被分为前进力和转向力矩）。Speed Controller 的输出 F_x 与 Heading Controller 的输出 M_z 必须在 Thrust Allocator 中联合优化分配。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 话题/接口 | 频率 | 说明 |
|-------|------|----------|------|------|
| 期望速度 u_cmd | Guidance Layer (L4) | `/decision/guidance/cmd` | 10 Hz | GuidanceCommand.speed_cmd（m/s） |
| 实际纵荡速度 u | Nav Filter | `/nav/odom` | 50 Hz | twist.linear.x（m/s） |
| 实际对地速度 SOG | Nav Filter | `/nav/geo_pose` | 10 Hz | 对地速度（m/s） |
| 实际航向 ψ | Nav Filter | `/nav/odom` | 50 Hz | 用于计算有效阻力方向 |
| 横荡速度 v | Nav Filter | `/nav/odom` | 50 Hz | 用于速度-航向耦合补偿 |
| 海流估计 | Perception / Weather | `/perception/environment` | 0.2 Hz | 海流速度和方向 |
| 风速风向 | Perception / Weather | `/perception/environment` | 0.2 Hz | 用于风阻力前馈 |
| 水深 | Perception / Nav Sensors | `/nav/depth` | 1 Hz | 用于浅水阻力修正 |
| 主机状态 | Engine Monitor | `/system/engine` | 5 Hz | 转速、功率、温度、故障码 |

### 2.2 输出

```
SpeedControlOutput:
    Header header
    
    # ---- 控制量 ----
    float64 force_x_demand          # 所需纵向推力 F_x（N）
    float64 force_x_demand_raw      # 未限幅原始值
    
    # ---- 控制器状态 ----
    float64 speed_error             # 速度误差 e_u = u_cmd - u（m/s）
    float64 pid_p_term              # PID 比例项
    float64 pid_i_term              # PID 积分项
    float64 pid_d_term              # PID 微分项
    float64 feedforward_term        # 阻力前馈项
    
    # ---- 系统状态 ----
    float64 estimated_drag          # 估计的当前总阻力（N）
    float64 power_demand_kw         # 推算的功率需求（kW）
    float64 power_utilization       # 功率利用率 [0,1]
    bool throttle_saturated         # 推力需求是否饱和
    string speed_mode               # "cruising"/"accelerating"/"decelerating"/"stopping"/"emergency_stop"
    string control_mode             # "pid_tracking"/"decel_profile"/"engine_protection"/"degraded"
```

---

## 3. 核心参数数据库

### 3.1 PID 基准参数

| 参数 | 符号 | 经验初值 | 单位 | 说明 |
|------|------|---------|------|------|
| 比例增益 | K_p_speed | 2000~8000 | N/(m/s) | 每 m/s 速度误差产生的推力 |
| 积分增益 | K_i_speed | 200~2000 | N/(m/s·s) | 消除稳态阻力偏差 |
| 微分增益 | K_d_speed | 500~5000 | N·s/(m/s) | 阻尼速度振荡 |

**初值估算方法：**

```
# 基于船舶质量和期望闭环响应时间

# 期望的速度闭环时间常数（从 0 加速到 63% 巡航速度的时间）
τ_speed = 30    # 秒（典型值 20~60 秒）

# 简化的一阶模型：M_eff × du/dt = F_x - F_drag
# 闭环传递函数的时间常数 τ_cl = M_eff / K_p_speed
# → K_p_speed = M_eff / τ_speed

K_p_speed = M_eff / τ_speed = 8800 / 30 ≈ 293 N/(m/s)
# 但这是纯 P 控制的估值，实际 PID 中 K_p 可以更大

# 考虑到水阻力的非线性（∝ V²），在高速时同样的速度误差需要更大的推力修正
# 实际 K_p_speed 取 2000~5000 比较合理

K_p_speed = 3000    # 初始估值
K_i_speed = 500     # K_i ≈ K_p / τ_i，τ_i ≈ 6 秒
K_d_speed = 2000    # 阻尼项，取 K_p 的 0.5~1 倍
```

### 3.2 阻力模型参数

| 参数 | 符号 | 经验初值 | 单位 | 说明 |
|------|------|---------|------|------|
| 水阻力系数 | k_drag | 50 | N·s²/m² | F_drag = k_drag × u² |
| 风阻力系数 | C_D_wind | 0.8~1.2 | — | 正面受风 |
| 正面受风面积 | A_front | 待实测 | m² | |
| 浅水阻力增加系数 | k_shallow | 0.5~1.5 | — | 见 Speed Profiler 文档 |
| 附加质量系数（纵向） | k_surge_added | 0.05~0.15 | — | M_eff = Δ × (1 + k) |

### 3.3 推进系统限制参数

| 参数 | 符号 | 说明 |
|------|------|------|
| 最大前进推力 | F_max_ahead | 由螺旋桨特性和主机功率决定（N） |
| 最大倒车推力 | F_max_astern | 通常 = 0.60~0.70 × F_max_ahead |
| 最大功率 | P_max | 主机额定功率（kW） |
| 最大推力变化率 | dF_max | N/s，防止主机过载 |
| 主机最大转速 | n_max | rps，不可超过 |
| 主机最低稳定转速 | n_min_stable | rps，低于此主机可能熄火 |
| 倒车切换延迟 | t_reverse_delay | 从前进到倒车的切换时间（秒） |

### 3.4 速度误差参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 速度死区 | 0.1 m/s | 小于此不修正 |
| 最大有效误差 | 5.0 m/s | 超过此限幅 |
| 速度跟踪精度要求 | ±0.3 m/s（稳态） | 巡航时的稳态精度 |

---

## 4. 船长/轮机长的速度控制思维模型

### 4.1 速度控制的实际操作

在传统船舶上，速度控制的操作链是：

**船长下令：** "前进三"（或指定具体航速如"航速 12 节"）。

**轮机长执行：** 调整主机转速（或燃油供给量），使主机功率逐渐增大。船的速度会缓慢上升——不像汽车踩油门那么快，一艘排水型船从静止加速到巡航速度通常需要 1~5 分钟。

**自动调节：** 在巡航状态下，推力需要恰好等于水阻力。如果遇到逆风或逆流，阻力增大，船会轻微减速。轮机长（或自动功率管理系统）需要略微增加功率来补偿。

### 4.2 速度控制与航向控制的关键区别

| 特征 | 航向控制 | 速度控制 |
|------|---------|---------|
| 响应速度 | 快（舵效几秒内建立） | 慢（加速需要数十秒到数分钟） |
| 主要干扰 | 波浪（高频）+ 风流（低频） | 阻力变化（通常缓慢） |
| 执行机构 | 舵（响应快，力矩有限） | 螺旋桨（惯性大，功率强） |
| 非线性 | 中等（舵失速角） | 强（阻力 ∝ V²，推力特性非线性） |
| 积分需求 | 中等（补偿偏流） | 强（稳态必须完全补偿阻力） |
| 微分需求 | 强（阻尼振荡） | 弱（速度变化本身缓慢） |

**重要推论：** Speed Controller 的 PID 参数特性与 Heading Controller 很不同——积分增益相对更重要（必须完全补偿阻力），微分增益可以较小（速度变化慢，不像航向那样需要强阻尼），闭环带宽更低（速度响应本身就慢，不需要也不应该追求快速响应）。

### 4.3 轮机长绝不做的事

- **不会瞬间从满前进切换到满倒车。** 这会损坏传动系统。从前进到倒车需要先减速到零再切换——中间有一个停机-倒车启动的延迟。
- **不会让主机长时间过载运行。** 短时间（几分钟）超过额定功率 110% 可以接受，但不能持续。
- **不会在海况恶劣时维持最大速度。** 高浪中船头会周期性地冲出水面再砸入水中（砰击），导致瞬时阻力剧变。此时应降速以保护船体结构。

---

## 5. 计算流程总览

```
输入：u_cmd（期望速度）+ u（实际速度）+ 环境数据
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：速度误差计算              │
    │ e_u = u_cmd - u（含死区）         │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：模式判定                  │
    │ 巡航/加速/减速/停船               │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：增益调度                  │
    │ 根据速度域和工况调整 PID 增益      │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：PID 计算                  │
    │ F_pid = P + I + D                │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：阻力前馈                  │
    │ F_ff = 估计的总阻力               │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：主机保护检查              │
    │ 功率/转速/温度限制                │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤七：输出限幅与速率限制         │
    │ F_x = clamp(F_pid + F_ff)        │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：F_x_demand（纵向推力需求）
```

---

## 6. 步骤一：速度误差计算

```
FUNCTION compute_speed_error(u_cmd, u):
    
    e_u = u_cmd - u
    
    # 死区处理
    IF abs(e_u) < SPEED_DEADBAND:
        e_u = 0
    ELSE:
        # 平滑死区
        IF e_u > 0:
            e_u = e_u - SPEED_DEADBAND
        ELSE:
            e_u = e_u + SPEED_DEADBAND
    
    # 限幅
    e_u = clamp(e_u, -MAX_SPEED_ERROR, MAX_SPEED_ERROR)
    
    RETURN e_u

SPEED_DEADBAND = 0.1    # m/s
MAX_SPEED_ERROR = 5.0   # m/s
```

### 6.1 速度模式判定

```
FUNCTION determine_speed_mode(u_cmd, u, u_cmd_prev, e_u):
    
    IF u_cmd < 0.1 AND u > 0.5:
        RETURN "stopping"              # 目标零速但当前有速
    
    IF u_cmd < 0.1 AND u < 0.5:
        RETURN "station_keeping"       # 已接近零速
    
    IF u_cmd - u_cmd_prev > 0.1:
        RETURN "accelerating"          # 指令在增大
    
    IF u_cmd - u_cmd_prev < -0.1:
        RETURN "decelerating"          # 指令在减小
    
    IF abs(e_u) < 0.3:
        RETURN "cruising"              # 稳态巡航
    
    IF e_u > 0:
        RETURN "accelerating"
    ELSE:
        RETURN "decelerating"
```

---

## 7. 步骤二：PID 速度控制器设计

### 7.1 PID 计算

```
FUNCTION compute_speed_pid(e_u, u, integral_e, e_u_prev, dt, Kp, Ki, Kd):
    
    # ---- 比例项 ----
    P = Kp × e_u
    
    # ---- 积分项 ----
    integral_e += e_u × dt
    integral_e = apply_anti_windup(integral_e, ...)
    I = Ki × integral_e
    
    # ---- 微分项 ----
    # 使用加速度传感器数据（如可用）代替数值微分
    # 否则用速度差分
    IF acceleration_sensor_available:
        du_dt = acceleration_measured
    ELSE:
        du_dt = (u - u_prev) / dt
        # 低通滤波
        du_dt_filtered = α_d × du_dt + (1 - α_d) × du_dt_prev
    
    # D 项作用于过程变量（速度）而非误差——防止 u_cmd 阶跃时的微分冲击
    D = -Kd × du_dt_filtered
    
    F_pid = P + I + D
    
    RETURN {F_pid, P, I, D, integral_e}
```

**关键设计决策：D 项作用于速度测量值而非误差。** 如果 D 作用于误差 e_u，当 u_cmd 突然变化时（比如 Speed Profiler 发出减速指令），de_u/dt 会产生一个巨大的脉冲——导致推力需求瞬间跳变。让 D 作用于测量值 u，则 u_cmd 的阶跃不会影响 D 项，避免了这个问题。

### 7.2 PID 参数的物理意义（速度控制特化）

| 参数 | 在速度控制中的作用 | 与航向控制的区别 |
|------|------------------|----------------|
| K_p | 速度偏差越大推力越大 | 占比较小（阻力前馈承担主要推力） |
| K_i | **最关键**——稳态时必须完全补偿阻力 | 比航向控制中更重要 |
| K_d | 阻尼加减速过程——防止速度过调 | 比航向控制中不那么关键 |

**推论：** 速度 PID 中，PID 的主要作用是修正前馈无法覆盖的阻力误差（比如海流变化、船底附着物增加阻力等不可预测的因素）。大部分推力由前馈项（步骤五）提供，PID 只做"微调"。

---

## 8. 步骤三：增益调度

### 8.1 基于速度域的调度

水阻力与速度的平方成正比——这意味着同样的速度误差在高速时需要更大的推力修正。增益调度补偿这个非线性：

```
FUNCTION schedule_gains_speed(u, Kp_base, Ki_base, Kd_base):
    
    # 在高速时增大增益，补偿非线性阻力
    # 阻力灵敏度：dF_drag/du = 2 × k_drag × u
    drag_sensitivity = 2 × K_DRAG × max(u, 1.0)    # 防止零速时除以零
    drag_sensitivity_ref = 2 × K_DRAG × V_REF       # 参考速度下的灵敏度
    
    speed_gain_factor = drag_sensitivity / drag_sensitivity_ref
    speed_gain_factor = clamp(speed_gain_factor, 0.3, 3.0)
    
    Kp = Kp_base × speed_gain_factor
    Ki = Ki_base × speed_gain_factor
    Kd = Kd_base × sqrt(speed_gain_factor)    # D 增益变化慢一些
    
    RETURN {Kp, Ki, Kd}

V_REF = 6.0    # 参考速度（m/s），增益基准点
```

### 8.2 基于工况的调度

```
FUNCTION adjust_gains_for_mode(Kp, Ki, Kd, speed_mode):
    
    IF speed_mode == "accelerating":
        # 加速时需要更大的推力——增大 P
        Kp *= 1.3
        Ki *= 0.5    # 降低积分防止过调
    
    ELIF speed_mode == "decelerating":
        # 减速主要依赖阻力——降低 PID 增益
        Kp *= 0.7
        Ki *= 0.3
        Kd *= 1.5    # 增大阻尼防止减速过头
    
    ELIF speed_mode == "stopping":
        # 停船——使用专门的停船策略（见第 14 节）
        pass
    
    ELIF speed_mode == "cruising":
        # 巡航——标准增益，积分为主
        Ki *= 1.2    # 稍增大积分以精确补偿阻力
    
    RETURN {Kp, Ki, Kd}
```

---

## 9. 步骤四：积分项管理

### 9.1 积分在速度控制中的特殊重要性

在稳态巡航时，推力必须精确等于阻力。阻力公式 F_drag = k_drag × u² 中的 k_drag 受船底状态、水温、海流、附着物等因素影响，不是一个精确已知的常数。前馈项基于理论 k_drag 计算，但实际值可能偏差 10~30%。这个偏差只能由积分项来消除。

**因此积分项在速度控制中是不可或缺的——没有积分就会有永久的速度偏差。**

### 9.2 积分抗饱和

```
FUNCTION apply_speed_anti_windup(integral_e, F_total_raw, F_limited, K_bc, Ki, dt):
    
    # Back-calculation 方法
    saturation_error = F_limited - F_total_raw
    integral_e += K_bc × saturation_error × dt / Ki
    
    # 额外硬限幅
    I_max = F_MAX_AHEAD × 0.5    # 积分项不超过最大推力的 50%
    integral_e = clamp(integral_e, -I_max / Ki, I_max / Ki)
    
    RETURN integral_e
```

### 9.3 积分在模式切换时的处理

```
FUNCTION handle_integral_on_mode_change(old_mode, new_mode, integral_e, u_cmd, u):
    
    IF old_mode == "cruising" AND new_mode == "decelerating":
        # 从巡航切到减速——积分中积累了大量的正向推力（等于巡航阻力）
        # 这会阻碍减速！必须快速释放
        integral_e *= 0.3    # 保留 30%
    
    IF old_mode == "decelerating" AND new_mode == "accelerating":
        # 从减速切到加速——积分可能是负值
        integral_e = 0    # 清零重来
    
    IF new_mode == "stopping":
        integral_e = 0    # 停船时完全清除积分
    
    RETURN integral_e
```

---

## 10. 步骤五：前馈补偿（阻力前馈）

### 10.1 基于速度的水阻力前馈

前馈项提供了"维持当前速度所需的推力"的估计——这是 PID 输出的基线，PID 只做微调。

```
FUNCTION compute_drag_feedforward(u, u_cmd, wind_speed, wind_dir, heading, depth, ship_params):
    
    # ---- 水阻力 ----
    # 使用目标速度 u_cmd 而非当前速度 u——提前提供目标速度所需的推力
    V_target = u_cmd
    F_drag_water = ship_params.k_drag × V_target × abs(V_target)
    # 注意：V_target 可能为负（倒车），用 V × |V| 保持正确的方向
    
    # ---- 风阻力 ----
    IF wind_speed > 0:
        # 纵向风分量
        relative_wind_angle = wind_dir - heading
        V_wind_longitudinal = wind_speed × cos(relative_wind_angle)
        
        # 顺风减阻，逆风增阻
        F_drag_wind = 0.5 × ρ_AIR × ship_params.C_D_wind × ship_params.A_front × V_wind_longitudinal × abs(V_wind_longitudinal)
        # 注意：逆风时 V_wind_longitudinal > 0，F_drag_wind > 0（增加阻力）
    ELSE:
        F_drag_wind = 0
    
    # ---- 浅水附加阻力 ----
    IF depth > 0:
        h_over_T = depth / ship_params.draft
        IF h_over_T < 3.0:
            C_shallow = 1.0 + ship_params.k_shallow × (ship_params.draft / depth)²
            F_drag_water *= C_shallow
    
    # ---- 海流修正 ----
    # 如果有海流信息，用对水速度（而非对地速度）计算阻力
    # u_through_water = u_cmd - current_along_track
    # 但前馈已经基于 u_cmd 计算，海流影响由积分项自动补偿
    
    # ---- 总前馈推力 ----
    F_feedforward = F_drag_water + F_drag_wind
    
    RETURN {F_feedforward, F_drag_water, F_drag_wind}
```

### 10.2 加速度前馈

当 u_cmd 在变化时（加速或减速），除了阻力补偿外，还需要额外的推力来克服惯性：

```
FUNCTION compute_acceleration_feedforward(u_cmd, u_cmd_prev, dt, M_eff):
    
    du_cmd = (u_cmd - u_cmd_prev) / dt
    
    # 限制前馈加速度（防止指令跳变导致的脉冲）
    du_cmd = clamp(du_cmd, -A_FF_MAX, A_FF_MAX)
    
    F_accel_ff = M_eff × du_cmd
    
    RETURN F_accel_ff

A_FF_MAX = 0.3    # m/s²——前馈加速度上限
```

### 10.3 最终前馈合成

```
F_ff_total = F_drag_feedforward + F_accel_feedforward
```

---

## 11. 步骤六：输出限幅与速率限制

### 11.1 推力限幅

```
FUNCTION apply_force_limits(F_x_raw, u, engine_limits):
    
    IF F_x_raw > 0:
        # 前进推力
        F_max = get_max_thrust_ahead(u, engine_limits)
        F_x_limited = min(F_x_raw, F_max)
    ELSE:
        # 倒车推力
        F_max_astern = get_max_thrust_astern(u, engine_limits)
        F_x_limited = max(F_x_raw, -F_max_astern)
    
    RETURN F_x_limited
```

### 11.2 功率限制

```
FUNCTION apply_power_limit(F_x, u, P_max):
    
    # 功率 = 推力 × 速度
    P_demand = abs(F_x × u)
    
    IF P_demand > P_max:
        # 功率超限——按功率限制反推最大推力
        F_x_power_limited = sign(F_x) × P_max / max(abs(u), 0.5)
        RETURN F_x_power_limited
    
    RETURN F_x
```

### 11.3 推力变化率限制

```
FUNCTION apply_force_rate_limit(F_x_new, F_x_prev, dt, dF_max):
    
    dF = F_x_new - F_x_prev
    
    IF abs(dF) > dF_max × dt:
        F_x_rate_limited = F_x_prev + sign(dF) × dF_max × dt
    ELSE:
        F_x_rate_limited = F_x_new
    
    RETURN F_x_rate_limited
```

### 11.4 前进-倒车切换保护

```
FUNCTION check_reverse_transition(F_x_new, F_x_prev, u, reverse_state):
    
    IF sign(F_x_new) != sign(F_x_prev) AND abs(F_x_prev) > 100:
        # 推力方向反转——需要经过零点过渡
        
        IF reverse_state.phase == "IDLE":
            # 开始切换：先减到零
            reverse_state.phase = "REDUCING_TO_ZERO"
            reverse_state.start_time = now()
            F_x_output = F_x_prev × 0.7    # 逐步减小
        
        ELIF reverse_state.phase == "REDUCING_TO_ZERO":
            IF abs(F_x_prev) < 50:    # 接近零
                reverse_state.phase = "WAITING"
                reverse_state.wait_start = now()
                F_x_output = 0
            ELSE:
                F_x_output = F_x_prev × 0.7
        
        ELIF reverse_state.phase == "WAITING":
            # 等待倒车切换延迟
            IF now() - reverse_state.wait_start > T_REVERSE_DELAY:
                reverse_state.phase = "BUILDING_REVERSE"
                F_x_output = sign(F_x_new) × 100    # 缓慢建立反向推力
            ELSE:
                F_x_output = 0
        
        ELIF reverse_state.phase == "BUILDING_REVERSE":
            # 逐步增大反向推力
            F_x_output = F_x_prev + sign(F_x_new) × dF_max × dt × 0.5
            IF abs(F_x_output) >= abs(F_x_new):
                reverse_state.phase = "IDLE"    # 切换完成
        
        RETURN F_x_output
    
    ELSE:
        reverse_state.phase = "IDLE"
        RETURN F_x_new

T_REVERSE_DELAY = 3.0    # 秒——前进到倒车切换延迟
```

---

## 12. 加速控制策略

### 12.1 加速度限制

```
FUNCTION limit_acceleration(F_x, u, a_max_plan, M_eff):
    
    # 计算当前推力对应的加速度
    F_drag_current = K_DRAG × u × abs(u)
    F_net = F_x - F_drag_current
    a_current = F_net / M_eff
    
    IF a_current > a_max_plan:
        # 加速度超过规划用加速度——限制推力
        F_x_limited = M_eff × a_max_plan + F_drag_current
        RETURN F_x_limited
    
    RETURN F_x

A_MAX_PLAN = 0.15    # m/s²（规划用加速度）
```

### 12.2 高速区加速放缓

接近最大航速时，可用的净推力（推力 - 阻力）趋近于零。此时加速变得越来越慢，这是物理限制，不是控制器问题。Speed Controller 应识别这种情况并通知上层：

```
IF speed_mode == "accelerating" AND abs(e_u) > 0.5 AND a_current < 0.01:
    # 加速度极小但速度误差仍大——可能已接近最大航速
    log_event("approaching_speed_limit", {
        u_cmd: u_cmd,
        u: u,
        a: a_current,
        message: "接近推进系统速度上限"
    })
    
    # 通知 Guidance 层：实际可达速度可能低于指令速度
    publish_speed_capability_advisory(u_achievable = u + 0.5)
```

---

## 13. 减速控制策略

### 13.1 主动减速 vs 自由减速

Speed Controller 支持两种减速模式：

**自由减速（关闭推进，靠阻力减速）：**

```
IF speed_mode == "decelerating" AND abs(e_u) < GENTLE_DECEL_THRESHOLD:
    # 小幅减速——关闭推力，靠水阻力自然减速
    F_x = 0    # 不使用 PID，直接零推力
    # 自由减速度 ≈ F_drag / M_eff = k_drag × u² / M_eff
    # 在 u=8m/s 时：a ≈ 50 × 64 / 8800 ≈ 0.36 m/s²——比规划减速度还大
    # 实际上高速时自由减速就够了

GENTLE_DECEL_THRESHOLD = 3.0    # m/s（速度差 < 3 m/s 用自由减速）
```

**主动减速（倒车或部分倒车辅助）：**

```
IF speed_mode == "decelerating" AND abs(e_u) >= GENTLE_DECEL_THRESHOLD:
    # 大幅减速——需要倒车辅助
    # PID 自动计算负推力
    # 但需要经过前进-倒车切换保护（第 11.4 节）
```

### 13.2 减速曲线跟踪

Guidance 层的 Look-ahead Speed 模块输出的 u_cmd 本身已经是一条平滑的减速曲线。Speed Controller 只需要跟踪这条曲线——不需要自己设计减速剖面。

```
# 正常情况下，Speed Controller 忠实跟踪 u_cmd
# 只有在以下情况下 Speed Controller 会偏离 u_cmd：
# 1. 推进系统故障导致无法达到 u_cmd
# 2. 主机保护限制导致推力受限
# 3. 紧急停船指令覆盖了 u_cmd
```

---

## 14. 停船控制策略

### 14.1 正常停船

```
FUNCTION normal_stop(u, ship_params):
    
    # u_cmd = 0，PID + 前馈会自动产生减速推力
    # 但在接近零速时（u < 1 m/s），水阻力已经很小
    # 需要倒车辅助来最终停住
    
    IF u < 1.0 AND u > 0.1:
        # 低速区——PID 可能不够（积分还没累积够）
        # 直接施加一个固定的小倒车推力
        F_x = -F_STOP_ASSIST
        RETURN F_x
    
    IF u < 0.1:
        # 已经基本停住——零推力
        F_x = 0
        speed_mode = "station_keeping"
        RETURN F_x

F_STOP_ASSIST = 200    # N（小倒车推力辅助最终停船）
```

### 14.2 紧急停船（Crash Stop）

紧急停船是 Risk Monitor（L3）的 EMERGENCY 指令或手动紧急按钮触发的最高优先级动作。

```
FUNCTION crash_stop(u, ship_params):
    
    # 立即全功率倒车
    # 但需要经过前进-倒车切换保护（不能直接跳到满倒车）
    
    IF crash_stop_phase == "INITIATED":
        # 阶段 1：减小前进推力
        F_x = max(F_x_prev × 0.5, 0)
        IF F_x < 50:
            crash_stop_phase = "ZERO_CROSSING"
    
    ELIF crash_stop_phase == "ZERO_CROSSING":
        # 阶段 2：零推力等待
        F_x = 0
        IF now() - zero_start > T_REVERSE_DELAY × 0.5:    # 紧急时减半延迟
            crash_stop_phase = "FULL_ASTERN"
    
    ELIF crash_stop_phase == "FULL_ASTERN":
        # 阶段 3：逐步增大倒车推力
        F_x = max(F_x_prev - dF_EMERGENCY × dt, -F_MAX_ASTERN)
    
    RETURN F_x

dF_EMERGENCY = F_MAX_ASTERN / 3    # 3 秒内达到满倒车
```

### 14.3 停船距离估算

```
FUNCTION estimate_stopping_distance(u_current, ship_params):
    
    # 紧急停船距离（含切换延迟）
    # 阶段 1：减推力（约 2 秒，速度略降）
    D_1 = u_current × 2.0
    
    # 阶段 2：零推力等待（T_reverse_delay × 0.5 秒）
    V_after_1 = u_current × 0.9    # 粗略估计减速 10%
    D_2 = V_after_1 × (T_REVERSE_DELAY × 0.5)
    
    # 阶段 3：倒车减速
    # 使用非线性减速模型（见 Speed Profiler 文档第 12 节）
    D_3 = compute_decel_distance_with_reverse(V_after_1 × 0.95, 0, ship_params)
    
    total = D_1 + D_2 + D_3
    
    RETURN total
```

---

## 15. 浅水与受限水域的速度控制

### 15.1 浅水阻力增加补偿

```
FUNCTION apply_shallow_water_compensation(F_ff, depth, u, ship_params):
    
    h_over_T = depth / ship_params.draft
    
    IF h_over_T < 3.0:
        C_shallow = 1.0 + ship_params.k_shallow × (ship_params.draft / depth)²
        F_ff_adjusted = F_ff × C_shallow
        
        # 同时通知上层：浅水中阻力增加，最大航速降低
        V_max_shallow = V_MAX × (1 / C_shallow)^(1/3)
        IF u_cmd > V_max_shallow:
            publish_speed_limit_advisory(V_max_shallow, "浅水阻力限速")
        
        RETURN F_ff_adjusted
    
    RETURN F_ff
```

### 15.2 Squat 监控

在浅水中高速航行时，Squat 效应会使船体下沉，进一步减小 UKC。Speed Controller 在浅水区应监控 Squat：

```
FUNCTION monitor_squat(u, Cb, depth, draft):
    
    S_squat = Cb × (u / 0.5144)² / 100    # Barrass 公式，u 转为节
    UKC_dynamic = depth - draft - S_squat
    
    IF UKC_dynamic < UKC_MIN_CRITICAL:
        # Squat 导致 UKC 不足——必须降速！
        publish_emergency_speed_reduction({
            current_ukc: UKC_dynamic,
            squat: S_squat,
            recommendation: f"降速至 {compute_safe_speed(depth, draft, Cb):.1f} m/s"
        })
        
        # 强制覆盖 u_cmd
        u_cmd_override = compute_safe_speed(depth, draft, Cb)
```

---

## 16. 速度-航向耦合处理

### 16.1 转弯时的速度降

转弯时船会自然减速——因为部分推力被用来产生转向力矩（舵力消耗推力），同时横荡增加了阻力。

```
FUNCTION compensate_turn_speed_loss(M_z_demand, u, ship_params):
    
    # 估算转弯导致的速度降
    # 简化模型：转弯力矩消耗的功率 ≈ M_z × r
    # 等效阻力增加 ≈ M_z × r / u
    
    IF abs(heading_controller.yaw_rate) > 1.0:    # > 1°/s 表示正在转弯
        r = heading_controller.yaw_rate × π / 180
        P_turn = abs(M_z_demand × r)
        F_turn_loss = P_turn / max(u, 1.0)
        
        # 增加前馈推力补偿转弯损失
        RETURN F_turn_loss
    
    RETURN 0
```

### 16.2 推力资源共享

Speed Controller 和 Heading Controller 共享推进系统资源。当两者的需求之和超过推进系统的总能力时，必须做取舍——通常航向控制优先（安全性 > 效率）。

```
# 这个取舍在 Thrust Allocator 中实现，不在 Speed Controller 内部
# Speed Controller 只输出 F_x_demand，不需要知道推力如何分配
# 但 Speed Controller 需要知道自己的推力需求是否被满足（用于积分抗饱和）

IF thrust_allocator.speed_demand_fulfillment < 0.8:
    # 推力分配器只满足了速度需求的 80%（其余给了航向控制）
    # 通知积分项做抗饱和处理
    apply_allocation_aware_antiwindup(...)
```

---

## 17. 主机保护与操纵限制

### 17.1 主机过载保护

```
FUNCTION check_engine_protection(engine_state, F_x_demand):
    
    limits = []
    
    # 转速限制
    IF engine_state.rpm > engine_state.rpm_max × 0.95:
        limits.append({type: "rpm_limit", factor: 0.9})
    
    # 功率限制
    IF engine_state.power_kw > engine_state.power_max × 0.95:
        limits.append({type: "power_limit", factor: 0.9})
    
    # 排气温度限制
    IF engine_state.exhaust_temp > engine_state.exhaust_temp_max × 0.90:
        limits.append({type: "thermal_limit", factor: 0.8})
        log_event("engine_thermal_limit", engine_state.exhaust_temp)
    
    # 冷却水温度限制
    IF engine_state.coolant_temp > engine_state.coolant_temp_max × 0.90:
        limits.append({type: "coolant_limit", factor: 0.7})
    
    IF len(limits) > 0:
        # 取最严格的限制
        worst_factor = min(l.factor for l in limits)
        F_x_protected = F_x_demand × worst_factor
        
        control_mode = "engine_protection"
        log_event("engine_protection_active", limits)
        
        RETURN F_x_protected
    
    RETURN F_x_demand
```

### 17.2 螺旋桨空转保护

在大浪中船头冲出水面时，螺旋桨可能部分或完全出水——此时阻力瞬间消失但推力也大幅下降。如果主机仍在满功率运转，螺旋桨会空转（飞车），转速急剧上升，可能损坏传动系统。

```
FUNCTION detect_propeller_ventilation(engine_state, u, u_prev, dt):
    
    # 检测指标：转速突然上升 + 速度突然下降
    rpm_rate = (engine_state.rpm - engine_state.rpm_prev) / dt
    decel_rate = (u - u_prev) / dt
    
    IF rpm_rate > RPM_RISE_THRESHOLD AND decel_rate < -DECEL_THRESHOLD:
        # 疑似螺旋桨空转
        return {ventilation_detected: true}
    
    RETURN {ventilation_detected: false}

# 空转保护响应：立即减小推力需求
IF ventilation_detected:
    F_x_demand *= 0.3    # 降至 30%
    log_event("propeller_ventilation", {rpm_rate, decel_rate})
```

---

## 18. 故障降级策略

### 18.1 速度传感器故障

```
IF speed_sensor_health.status == "FAILED":
    # 无速度反馈——Speed Controller 无法工作
    # 切换到开环推力控制：F_x = F_ff(u_cmd)
    # 只用前馈，不用 PID
    
    F_x = compute_drag_feedforward(u_cmd, ...)
    control_mode = "degraded_open_loop"
    
    # 精度降低但仍可航行
    log_event("speed_controller_degraded", "speed sensor failed, open-loop mode")
```

### 18.2 单机故障（双机船）

```
IF engine_count == 2 AND one_engine_failed:
    # 可用推力减半
    F_max_effective = F_MAX_AHEAD × 0.5
    
    # 最大航速降低（粗略：减至 79%，因为阻力 ∝ V²，推力减半 → V_max 减至 1/√2）
    V_max_effective = V_MAX × 0.79
    
    # 如果 u_cmd > V_max_effective，通知上层降速
    IF u_cmd > V_max_effective:
        publish_speed_limit_advisory(V_max_effective, "单机运行限速")
```

---

## 19. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 频率 |
|-------|------|---------|------|
| Error Calculator | 速度误差计算（死区+限幅+模式判定） | C++ | 50 Hz |
| Gain Scheduler | 增益调度（速度域+工况+海况） | C++ | 10 Hz |
| PID Core | PID 计算含积分抗饱和 | C++ | 50 Hz |
| Drag Feedforward | 阻力前馈计算（水+风+浅水） | C++ | 10 Hz |
| Accel Feedforward | 加速度前馈 | C++ | 10 Hz |
| Output Limiter | 限幅+速率限制+前进倒车切换 | C++ | 50 Hz |
| Engine Protector | 主机保护（转速/功率/温度/空转） | C++ | 10 Hz |
| Stop Controller | 正常停船和紧急停船策略 | C++ | 50 Hz |
| Mode Manager | 速度模式判定和切换 | C++ | 10 Hz |
| Degradation Handler | 传感器/主机故障降级 | C++ | 1 Hz |

---

## 20. 数值算例

### 20.1 算例一：巡航阻力补偿

```
目标速度 u_cmd = 8 m/s, 实际速度 u = 8 m/s（稳态）

前馈推力：
  F_drag = k_drag × u² = 50 × 64 = 3200 N

速度误差 e_u = 0（在死区内）
PID 输出 = 0

总推力 F_x = 0 + 3200 = 3200 N
→ 推力恰好等于阻力，速度保持不变
```

### 20.2 算例二：从 4 m/s 加速到 8 m/s

```
t=0: u_cmd=8, u=4, e_u=4

前馈：F_drag(8) = 50 × 64 = 3200 N
      F_accel_ff = 8800 × (8-4)/(30) ≈ 1173 N（假设 30s 内完成）
      F_ff = 3200 + 1173 = 4373 N

PID：P = 3000 × 4 = 12000 N
     I = 0（刚开始）
     D = 0
     F_pid = 12000 N

总计：F_x_raw = 12000 + 4373 = 16373 N

功率检查：P = 16373 × 4 = 65.5 kW（假设在限制内）
推力限幅：假设 F_max = 15000 N → 限幅到 15000 N

加速度：a = (15000 - 50×16) / 8800 = (15000 - 800) / 8800 = 1.61 m/s²
→ 实际加速度远大于规划加速度 0.15 m/s²
→ 加速度限制生效：a_max = 0.15 → F_x = 8800 × 0.15 + 800 = 2120 N

最终：F_x = 2120 N，加速度约 0.15 m/s²
→ 约 27 秒后达到 8 m/s
```

### 20.3 算例三：紧急停船

```
u = 8 m/s, 紧急停船指令

阶段 1（0~2s）：F_x 从 3200N 降到 0
  平均速度 ≈ 7.5 m/s → 距离 ≈ 15m

阶段 2（2~3.5s）：零推力，自由减速
  F_drag(7.5) = 50 × 56 = 2813 N → a = 2813/8800 = 0.32 m/s²
  速度降至 ≈ 7.0 m/s → 距离 ≈ 11m

阶段 3（3.5s~）：逐步增大倒车推力至 F_max_astern = 10000N
  总制动力 = F_astern + F_drag(V)
  初始：10000 + 50×49 = 12450 N → a = 1.41 m/s² 
  用数值积分模拟...
  
  大约需要 ≈ 8s 停住（从阶段 3 开始）
  阶段 3 距离 ≈ 25m

总停船距离 ≈ 15 + 11 + 25 = 51m
总停船时间 ≈ 2 + 1.5 + 8 = 11.5s

→ 从 8 m/s 紧急停船需要约 51m / 11.5s
```

---

## 21. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **PID 基准** | K_p_speed | 3000 N/(m/s) | 待标定 |
| | K_i_speed | 500 N/(m/s·s) | 待标定 |
| | K_d_speed | 2000 N·s/(m/s) | 待标定 |
| | 参考速度 V_ref | 6.0 m/s | 增益基准 |
| **误差处理** | 死区 | 0.1 m/s | |
| | 最大有效误差 | 5.0 m/s | |
| | 稳态精度要求 | ±0.3 m/s | |
| **前馈** | 水阻力系数 k_drag | 50 N·s²/m² | 待标定 |
| | 风阻力系数 C_D_wind | 1.0 | 待标定 |
| | 前馈加速度上限 | 0.3 m/s² | |
| **限幅** | 最大前进推力 | 待标定 (N) | |
| | 最大倒车推力 | 0.65 × F_max_ahead | |
| | 最大功率 | 待标定 (kW) | |
| | 最大推力变化率 | F_max/3 (N/s) | |
| | 倒车切换延迟 | 3.0 s | |
| **加减速** | 规划用加速度上限 | 0.15 m/s² | |
| | 自由减速阈值 | 速度差 < 3 m/s | |
| | 停船辅助推力 | 200 N | |
| **保护** | 转速告警比例 | 95% n_max | |
| | 功率告警比例 | 95% P_max | |
| | 排温告警比例 | 90% T_max | |
| **积分管理** | 积分上限 | 50% F_max | |
| | 积分重置误差阈值 | 3.0 m/s（指令变化） | |
| | 减速时积分保留比 | 30% | |

---

## 22. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| Guidance (L4) → Speed Ctrl | 输入 | u_cmd 期望速度 | 10 Hz |
| Nav Filter → Speed Ctrl | 输入 | u 实际速度, SOG | 50 Hz |
| Perception → Speed Ctrl | 输入 | 风速风向, 海流, 水深 | 0.2~1 Hz |
| Engine Monitor → Speed Ctrl | 输入 | 主机转速/功率/温度 | 5 Hz |
| Speed Ctrl → Force Calculator | 输出 | F_x_demand 纵向推力 | 50 Hz |
| Heading Ctrl → Speed Ctrl | 协调 | M_z_demand（推力资源竞争感知） | 50 Hz |
| Speed Ctrl → System Monitor | 输出 | 饱和告警、保护状态 | 事件 |
| Risk Monitor (L3) → Speed Ctrl | 指令 | 紧急停船覆盖 | 事件 |

---

## 23. 测试策略与验收标准

### 23.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| SCTL-001 | 从 0 加速到 8 m/s | 加速曲线平滑,无过调 |
| SCTL-002 | 从 8 减速到 4 m/s | 减速平滑,无过调 |
| SCTL-003 | 恒速 8 m/s 巡航 | 稳态误差 < 0.3 m/s |
| SCTL-004 | 巡航中逆风增大 2 m/s | 积分补偿,速度恢复 |
| SCTL-005 | 巡航中逆流增大 1 节 | 积分补偿 |
| SCTL-006 | 紧急停船（8→0） | 距离 < 6L, 时间 < 15s |
| SCTL-007 | 正常停船（8→0） | 距离 < 10L, 平滑减速 |
| SCTL-008 | 前进→倒车切换 | 无推力阶跃,延迟合理 |
| SCTL-009 | 浅水中速度控制 | 阻力补偿正确 + Squat 监控 |
| SCTL-010 | 转弯中速度保持 | 速度降 < 15% |
| SCTL-011 | 主机过载保护 | 功率/转速限制生效 |
| SCTL-012 | 螺旋桨空转检测 | 推力自动减至 30% |
| SCTL-013 | 速度传感器故障 | 切换到开环模式 |
| SCTL-014 | 连续速度指令跟踪 | 跟踪 Speed Profiler 曲线 |
| SCTL-015 | 高速区加速饱和 | 正确识别速度上限 |

### 23.2 验收标准

| 指标 | 标准 |
|------|------|
| 巡航稳态误差 | < ±0.3 m/s |
| 5 m/s 阶跃过调量 | < 10% |
| 加速度限制遵守 | 100%（不超过 a_max_plan） |
| 紧急停船距离 | < 6 倍船长 |
| 前进-倒车切换无冲击 | 推力变化率 < dF_max |
| 主机保护响应时间 | < 500 ms |
| 空转检测响应时间 | < 1 s |
| 控制器更新频率 | ≥ 50 Hz |
| 计算延迟 | < 0.5 ms |

---

## 24. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" Ch.13, 2021 | 速度控制理论 |
| [2] | IMO MSC.137(76) | 操纵性标准（停船试验） |
| [3] | Harvald, S.A. "Resistance and Propulsion of Ships" 1983 | 阻力估算 |
| [4] | MAN Energy Solutions "Basic Principles of Ship Propulsion" | 螺旋桨特性和主机保护 |
| [5] | Åström & Hägglund "Advanced PID Control" 2006 | PID 设计和抗饱和 |
| [6] | Barrass, C.B. "Ship Design and Performance" 2004 | Squat 效应 |

---

## v3.1 补充升级：极低速控制策略（离靠港操作）

### A. 问题——低速时控制性能退化

当前 Speed Controller PID 设计为航行速度域（3~28 节）。在靠泊极低速（0.1~1 节 ≈ 0.05~0.5 m/s）时，三个问题导致控制失效：

| 问题 | 原因 | 影响 |
|------|------|------|
| 推进器死区 | 喷水推进器在低 RPM 时有非线性死区——指令 100 RPM 但实际推力为零 | 速度控制"踩不动油门" |
| 计程仪噪声 | 多普勒计程仪在 < 0.5 m/s 时噪声增大到 ±0.2 m/s——信噪比极差 | PID 被噪声驱动而非真实速度误差 |
| 积分饱和 | 长时间低速时积分项积累过大——当需要加速时响应滞后 | 到达泊位后"刹不住" |

### B. 极低速控制策略

```
FUNCTION low_speed_controller(target_speed, current_speed, mode, dt):
    
    IF mode != "DP_APPROACH" AND target_speed > 1.0:
        # 正常航行——使用标准 PID
        RETURN standard_pid.compute(target_speed - current_speed, dt)
    
    # ---- 极低速模式（DP_APPROACH 或 target_speed < 1 m/s）----
    
    # 1. 速度估计增强——融合多源
    speed_estimate = fuse_low_speed_sources(
        log_speed = speed_log.stw,          # 计程仪（噪声大但无累积漂移）
        gps_speed = nav_filter.sog,          # GPS SOG（低速时不可靠）
        lidar_speed = lidar_ground_speed,    # LiDAR 对地测速（如可用）
        imu_integral = imu_integrated_speed  # IMU 加速度积分（短期准确）
    )
    
    # 2. 推进器死区补偿
    raw_thrust_cmd = low_speed_pid.compute(target_speed - speed_estimate, dt)
    
    IF abs(raw_thrust_cmd) > 0 AND abs(raw_thrust_cmd) < DEAD_ZONE_THRUST:
        # 指令在死区内——强制跳到死区边界
        thrust_cmd = sign(raw_thrust_cmd) * DEAD_ZONE_THRUST
    ELSE:
        thrust_cmd = raw_thrust_cmd
    
    # 3. 积分抗饱和
    IF abs(speed_estimate) < 0.1 AND abs(target_speed) < 0.1:
        # 接近停止——清除积分项防止饱和
        low_speed_pid.reset_integral()
    
    # 4. 接触速度硬限（最后 2m）
    IF berth_distance < 2.0 AND speed_estimate > max_contact_speed:
        thrust_cmd = -EMERGENCY_BRAKE_THRUST    # 紧急制动
    
    RETURN thrust_cmd

DEAD_ZONE_THRUST = 15    # % 额定推力——低于此值推进器无输出
```

### C. 低速多源速度融合

```
FUNCTION fuse_low_speed_sources(log, gps, lidar, imu):
    # 根据当前条件动态加权
    weights = {}
    
    # 计程仪：低速时降权
    IF abs(log) < 0.5:
        weights['log'] = 0.2    # 噪声大
    ELSE:
        weights['log'] = 0.5
    
    # LiDAR 对地测速（通过连续帧的点云匹配）
    IF lidar IS NOT NULL:
        weights['lidar'] = 0.5    # 低速时最可靠
    ELSE:
        weights['lidar'] = 0
    
    # IMU 积分（短期准确但会漂移）
    weights['imu'] = 0.3
    
    # GPS SOG（低速时几乎不可用）
    IF abs(gps) < 0.5:
        weights['gps'] = 0.0
    ELSE:
        weights['gps'] = 0.2
    
    # 归一化
    total = sum(weights.values())
    estimate = (weights['log']*log + weights['lidar']*lidar 
                + weights['imu']*imu + weights['gps']*gps) / total
    
    RETURN estimate
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v3.1 补充：增加极低速控制策略（推进器死区补偿 + 低速多源速度融合 + 积分抗饱和 + 接触速度硬限）；支持 DP_APPROACH 靠泊模式 |

---

**文档结束**
