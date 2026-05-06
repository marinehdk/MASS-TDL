# MASS_ADAS Heading Controller 航向控制器技术设计文档

**文档编号：** SANGO-ADAS-HDG-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考——待海试数据标定 PID 参数

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长/舵工的操舵思维模型
5. 计算流程总览
6. 步骤一：航向误差计算
7. 步骤二：PID 控制器设计
8. 步骤三：PID 参数调度（Gain Scheduling）
9. 步骤四：积分项管理（抗饱和）
10. 步骤五：微分项噪声抑制
11. 步骤六：控制输出限幅与速率限制
12. 步骤七：前馈补偿
13. 高级控制算法：Backstepping 设计
14. 高级控制算法：模型预测控制（MPC）
15. 航向保持模式 vs 航向跟踪模式
16. 低速操纵模式（差动/侧推辅助）
17. 转弯控制增强
18. 外部干扰下的鲁棒性
19. 故障降级策略
20. 控制器在线自整定
21. 内部子模块分解
22. 数值算例
23. 参数总览表
24. 与其他模块的协作关系
25. 测试策略与验收标准
26. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Heading Controller（航向控制器）是 Layer 5（Control & Allocation Layer，控制分配层）的核心控制模块之一。它接收 Guidance 层（L4）输出的期望航向 ψ_cmd，结合本船当前实际航向 ψ 和转向角速度 r，计算所需的艏摇力矩 M_z，使船头精确、平稳地跟踪期望航向。

Heading Controller 等价于自动舵（Autopilot）的核心算法。在传统船舶上，自动舵是驾驶台上最常用的设备之一——船长设定一个目标航向，自动舵持续操舵使船保持在该航向上。MASS_ADAS 的 Heading Controller 是这个功能的完整实现，但比传统自动舵更强——它不仅能保持航向，还能跟踪时变的航向指令（来自 LOS 引导律的连续变化的 ψ_cmd）。

### 1.2 核心职责

- **航向跟踪：** 使船的实际航向 ψ 跟踪期望航向 ψ_cmd，稳态误差趋近于零。
- **转向控制：** 在航向变化时（转弯），以合适的角速度平稳转向，不过调、不振荡。
- **干扰抑制：** 在风、浪、流等外部干扰下保持航向稳定。
- **力矩需求输出：** 输出所需的艏摇力矩 M_z（Nm），由下游的 Force Calculator 和 Thrust Allocator 负责将其分配到具体执行机构。

### 1.3 设计原则

- **平稳性优先原则：** 宁可响应慢一点，也不要产生航向振荡。航向振荡导致船体横摇、舵机频繁摆动、燃油浪费——这在实际航海中是最令人厌恶的自动舵行为。
- **速度自适应原则：** 控制器参数必须随航速变化而调整。高速时舵效强但惯性大，低速时舵效弱但惯性小——同一套 PID 参数不可能在全速度范围内都表现良好。
- **安全限幅原则：** 控制器输出的力矩需求必须在执行机构的物理能力范围内。超出能力的需求不仅无法执行，还可能导致推力分配算法失效。
- **降级可用原则：** 当某个传感器或执行机构故障时，Heading Controller 应能切换到降级模式继续工作（精度降低但不完全失效）。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 话题/接口 | 频率 | 说明 |
|-------|------|----------|------|------|
| 期望航向 ψ_cmd | Guidance Layer (L4) | `/decision/guidance/cmd` | 10 Hz | GuidanceCommand.heading_cmd（弧度） |
| 期望角速度 r_cmd | Guidance Layer (L4) | `/decision/guidance/cmd` | 10 Hz | GuidanceCommand.yaw_rate_cmd（用于角速度模式） |
| 控制模式 | Guidance Layer (L4) | `/decision/guidance/cmd` | 10 Hz | use_yaw_rate: true=角速度模式 |
| 实际航向 ψ | Nav Filter | `/nav/odom` | 50 Hz | orientation 中提取 yaw |
| 实际转向角速度 r | Nav Filter | `/nav/odom` | 50 Hz | angular_velocity.z |
| 纵荡速度 u | Nav Filter | `/nav/odom` | 50 Hz | twist.linear.x |
| 横荡速度 v | Nav Filter | `/nav/odom` | 50 Hz | twist.linear.y |
| 系统健康状态 | System Monitor | `/system/health` | 1 Hz | 传感器和执行机构状态 |

### 2.2 输出

```
HeadingControlOutput:
    Header header
    
    # ---- 控制量 ----
    float64 moment_z_demand          # 所需艏摇力矩 M_z（Nm）
    float64 moment_z_demand_raw      # 未限幅的原始计算值（用于饱和检测）
    
    # ---- 控制器状态 ----
    float64 heading_error            # 航向误差（弧度）
    float64 heading_error_rate       # 航向误差变化率（rad/s）
    float64 pid_p_term               # PID 比例项贡献
    float64 pid_i_term               # PID 积分项贡献
    float64 pid_d_term               # PID 微分项贡献
    float64 feedforward_term         # 前馈项贡献
    
    # ---- 增益调度状态 ----
    float64 kp_current               # 当前使用的 Kp 值
    float64 ki_current               # 当前使用的 Ki 值
    float64 kd_current               # 当前使用的 Kd 值
    string gain_schedule_zone        # 当前增益区间名称
    
    # ---- 系统状态 ----
    bool saturated                   # 控制器是否饱和（输出被限幅）
    string control_mode              # "heading_track"/"heading_hold"/"yaw_rate"/"degraded"
    float64 control_effort_percent   # 控制力占最大力的百分比 [0,100]
```

---

## 3. 核心参数数据库

### 3.1 PID 基准参数

PID 参数是 Heading Controller 最核心的调参对象。以下为经验初值，必须通过仿真调试和海试标定获得最终值。

**基准参数（中速航行，V ≈ 6 m/s）：**

| 参数 | 符号 | 经验初值 | 单位 | 说明 |
|------|------|---------|------|------|
| 比例增益 | K_p | 5000~20000 | Nm/rad | 每弧度航向误差产生的力矩 |
| 积分增益 | K_i | 100~1000 | Nm/(rad·s) | 消除稳态偏差 |
| 微分增益 | K_d | 10000~50000 | Nm·s/rad | 阻尼振荡 |

**增益初值的经验估算方法：**

```
# 基于船舶惯性的粗略估算

# 船舶艏摇转动惯量
I_zz = M_eff × (L/4)²    # 粗略估计，M_eff 为有效质量，L 为船长
# 对于 M_eff=8800kg, L=12m: I_zz ≈ 8800 × 9 = 79200 kg·m²

# 期望的闭环自然频率（航向控制通常取 0.1~0.5 rad/s）
ω_n = 0.2    # rad/s

# 期望的阻尼比（航向控制通常取 0.8~1.2，偏过阻尼）
ζ = 1.0      # 临界阻尼

# PID 参数初值
K_p ≈ I_zz × ω_n² = 79200 × 0.04 = 3168 Nm/rad
K_d ≈ 2 × ζ × I_zz × ω_n = 2 × 1.0 × 79200 × 0.2 = 31680 Nm·s/rad
K_i ≈ K_p × ω_n / 10 = 3168 × 0.02 = 63 Nm/(rad·s)
```

**警告：** 以上仅为量级估算。实际值必须通过仿真中的阶跃响应测试来精确调定。初始调试时建议从低增益开始（上述估值的 50%），逐步增大直到响应速度满足要求但不振荡。

### 3.2 增益调度参数

PID 参数随航速变化。原因：高速时舵效强（相同舵角产生更大力矩），但船的转动惯性（含附加质量）也更大；低速时舵效弱但惯性小。综合效果是需要不同的增益。

| 速度区间 (m/s) | 名称 | K_p 倍率 | K_i 倍率 | K_d 倍率 | 说明 |
|---------------|------|---------|---------|---------|------|
| V < 1.5 | 极低速 | 0.3 | 0.5 | 0.3 | 舵效很弱，主要靠差动/侧推 |
| 1.5 ≤ V < 3.0 | 低速 | 0.5 | 0.7 | 0.5 | 舵效开始生效但偏弱 |
| 3.0 ≤ V < 5.0 | 中低速 | 0.8 | 0.9 | 0.8 | 舵效正常但不强 |
| 5.0 ≤ V < 7.0 | 中速 | 1.0 | 1.0 | 1.0 | 基准速度，增益最优点 |
| 7.0 ≤ V < 9.0 | 中高速 | 0.9 | 0.8 | 1.1 | 惯性增大，增大阻尼 |
| V ≥ 9.0 | 高速 | 0.7 | 0.6 | 1.3 | 高惯性高舵效，强阻尼防振荡 |

**倍率的物理解释：**

K_p 在高速时降低——因为高速时舵效强（单位舵角产生更大力矩），K_p 不降低会导致过度修正和振荡。

K_d 在高速时增加——因为高速时惯性大，需要更强的阻尼来抑制航向振荡。

K_i 在高速时降低——因为高速时偏流角较小（船速 >> 流速），稳态偏差小，不需要太强的积分。

### 3.3 输出限幅参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 最大艏摇力矩需求 | M_z_max | 待标定（Nm） | 由执行机构能力决定 |
| 最大力矩变化率 | dM_z_max | M_z_max / 2（Nm/s） | 防止力矩阶跃 |
| 航向误差死区 | ψ_deadband | 0.5°（0.0087 rad） | 小于此不输出修正（防噪声） |

### 3.4 微分项滤波参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 微分滤波时间常数 | τ_d = K_d / (N × K_p) | N 为微分滤波系数，取 5~20 |
| 微分滤波系数 N | 10 | 越大滤波越弱、噪声越大 |

### 3.5 积分抗饱和参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 积分上限 | I_max = M_z_max × 0.3 | 积分项不超过最大力矩的 30% |
| 积分下限 | -I_max | 对称 |
| 抗饱和模式 | Back-calculation | 推荐方法 |
| Back-calculation 系数 | K_bc = 1 / (τ_i) | τ_i = K_p / K_i |

---

## 4. 船长/舵工的操舵思维模型

### 4.1 手动操舵的思维过程

在手动操舵模式下，舵工的操作是 Heading Controller 要模拟的对象。一个好的舵工是这样操舵的：

**看偏差大小决定舵角大小。** 偏了 3° 打 5° 舵角，偏了 10° 打 15° 舵角。这就是比例控制（P）。但不是简单的线性比例——经验丰富的舵工在大偏差时会"打满舵"（比例饱和），小偏差时会"点一下舵"（死区内不动）。

**看偏差变化速度决定是否要反舵。** 如果偏了 5° 但正在快速回来（误差在减小），他会提前回舵甚至反舵——防止冲过头到另一侧。这就是微分控制（D）。一个好舵工最明显的特征就是"提前回舵"——在船头还没有对准目标航向之前就开始减小舵角。

**如果风或流导致航向始终偏一点点回不来，他会多打一点点舵。** 比如右舷有持续的横风让船头向左偏 2°，他会在目标航向的基础上往右多打 2° 的"压舵"。这就是积分控制（I）——但好的舵工不需要积分很久，他很快就能估算出需要压多少度舵。

**他不会在每一个小波动都做反应。** 海浪引起的航向波动（通常 ±1~3°，周期 5~10 秒）是不应该修正的——因为修正了也没用（波浪力远大于舵力），而且频繁摆舵会加速舵机磨损和浪费燃油。好的舵工会"忽略"这些高频波动，只响应低频的偏航趋势。

### 4.2 自动舵（PID）vs 好舵工

自动舵（PID 控制器）模拟了上述所有行为：P = 看偏差，D = 看趋势，I = 消除残余偏差。但 PID 做不到的是"忽略波浪"——如果不加波浪滤波，PID 会对每一个浪引起的航向波动都做反应，导致舵机疲劳。

因此，Heading Controller 的一个关键设计要求是**波浪滤波**——在航向误差进入 PID 之前，用低通滤波器滤除波浪频率成分（通常 > 0.1 Hz，即周期 < 10 秒的波动）。

### 4.3 转弯时的操舵

转弯不是简单的"设定新航向等 PID 追上去"——一个好的操舵过程是这样的：

1. 下令打舵到一个预设舵角（比如 15° 或 20°）。
2. 船开始转向，角速度逐渐建立。
3. 当航向接近目标时（还差 10~15°），开始回舵。
4. 航向到达目标时，舵已经接近零位，角速度也接近零。
5. 如果做得完美，船头在目标航向处精确停住，无过调。

这个过程的关键是步骤 3 的"提前回舵"——这正是 PID 中 D（微分）项的作用。D 项感知到航向误差在快速减小（因为船在转弯），会产生一个与 P 项方向相反的输出，相当于"回舵"。

---

## 5. 计算流程总览

```
输入：ψ_cmd（期望航向）+ ψ（实际航向）+ r（角速度）+ u（纵荡速度）
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：航向误差计算              │
    │ e_ψ = ψ_cmd - ψ（含角度归一化）  │
    │ 死区处理                          │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：波浪滤波                  │
    │ 低通滤波器消除高频航向波动         │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：增益调度                  │
    │ 根据当前航速查表获取 Kp/Ki/Kd    │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：PID 计算                  │
    │ P = Kp × e_ψ                     │
    │ I = Ki × ∫e_ψ dt（含抗饱和）     │
    │ D = Kd × de_ψ/dt（含滤波）       │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：前馈补偿                  │
    │ 基于 ψ_cmd 变化率的前馈力矩       │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：输出合成与限幅             │
    │ M_z = P + I + D + FF             │
    │ 限幅 + 速率限制                   │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：M_z_demand（艏摇力矩需求）
```

---

## 6. 步骤一：航向误差计算

### 6.1 角度归一化

航向误差必须正确处理 0°/360°（0/2π）跳变：

```
FUNCTION compute_heading_error(ψ_cmd, ψ):
    
    e_ψ = ψ_cmd - ψ
    
    # 归一化至 [-π, π)
    WHILE e_ψ > π:  e_ψ -= 2π
    WHILE e_ψ < -π: e_ψ += 2π
    
    RETURN e_ψ
    # e_ψ > 0: 需要右转
    # e_ψ < 0: 需要左转
```

### 6.2 死区处理

```
FUNCTION apply_deadband(e_ψ, deadband):
    
    IF abs(e_ψ) < deadband:
        RETURN 0    # 误差太小，不修正（防止对传感器噪声过度反应）
    ELSE:
        # 平滑死区（避免在死区边界处输出跳变）
        IF e_ψ > 0:
            RETURN e_ψ - deadband
        ELSE:
            RETURN e_ψ + deadband
```

### 6.3 大误差限制

```
FUNCTION limit_heading_error(e_ψ, max_error):
    
    # 限制误差的有效范围
    # 大于 max_error 时限幅——防止 PID 产生过大的输出
    RETURN clamp(e_ψ, -max_error, max_error)

MAX_HEADING_ERROR = 60° = π/3 rad
# 超过 60° 的误差限幅在 60°——此时 P 项已经足够大
# 实际转弯由前馈项和 WOP 机制处理，不依赖 P 项的极端值
```

---

## 7. 步骤二：PID 控制器设计

### 7.1 标准 PID 公式

```
FUNCTION compute_pid(e_ψ, e_ψ_prev, integral_e, dt, Kp, Ki, Kd, N):
    
    # ---- 比例项 ----
    P = Kp × e_ψ
    
    # ---- 积分项 ----
    integral_e += e_ψ × dt
    # 积分抗饱和处理（见步骤四）
    integral_e = apply_anti_windup(integral_e, ...)
    I = Ki × integral_e
    
    # ---- 微分项（带滤波）----
    # 使用"误差微分"而非"测量值微分"
    # 但对 ψ_cmd 的阶跃变化做特殊处理（见步骤五前馈替代）
    de = e_ψ - e_ψ_prev
    
    # 一阶低通滤波器抑制噪声
    τ_d = Kd / (N × Kp)
    α_d = dt / (τ_d + dt)
    D_raw = Kd × de / dt
    D = α_d × D_raw + (1 - α_d) × D_prev
    
    # ---- 合成 ----
    M_z_pid = P + I + D
    
    RETURN {M_z_pid, P, I, D, integral_e}
```

### 7.2 PID 参数的物理意义

| 参数 | 物理意义 | 调大的效果 | 调小的效果 |
|------|---------|----------|----------|
| K_p | "误差越大，修正越猛" | 响应快，但容易振荡 | 响应慢，稳态误差可能增大 |
| K_i | "消除持续偏差的耐心" | 稳态误差消除更快，但容易过调 | 稳态误差消除慢，可能有残余偏差 |
| K_d | "预判趋势，提前回舵" | 阻尼强，振荡小，但对噪声敏感 | 阻尼弱，容易振荡 |

### 7.3 使用角速度反馈代替微分项（推荐改进）

传统 PID 的 D 项对航向误差求微分。但航向信号含有较多噪声（波浪、传感器），微分会放大噪声。一个更好的做法是**直接使用 IMU 测量的转向角速度 r 作为微分反馈**——它比对航向做数值微分准确得多。

```
FUNCTION compute_pid_with_rate_feedback(e_ψ, r, integral_e, dt, Kp, Ki, Kd):
    
    P = Kp × e_ψ
    
    integral_e += e_ψ × dt
    integral_e = apply_anti_windup(integral_e, ...)
    I = Ki × integral_e
    
    # 使用实测角速度代替微分
    # D = -Kd × r（注意负号：r > 0 表示正在右转，应减小右转力矩）
    D = -Kd × r
    
    M_z_pid = P + I + D
    
    RETURN {M_z_pid, P, I, D, integral_e}
```

**优势：** r 来自 IMU 的陀螺仪，信号质量远优于对航向做数值微分。不需要微分滤波，噪声问题大幅缓解。

**这是推荐的默认实现方式。**

---

## 8. 步骤三：PID 参数调度（Gain Scheduling）

### 8.1 基于速度的增益调度

```
FUNCTION get_scheduled_gains(V, base_gains, schedule_table):
    
    # 在调度表中线性插值
    multipliers = interpolate_schedule(V, schedule_table)
    
    Kp = base_gains.Kp × multipliers.kp_mult
    Ki = base_gains.Ki × multipliers.ki_mult
    Kd = base_gains.Kd × multipliers.kd_mult
    
    RETURN {Kp, Ki, Kd}
```

### 8.2 基于海况的增益调整

```
FUNCTION adjust_gains_for_sea_state(Kp, Ki, Kd, sea_state):
    
    # 海况越恶劣，越需要降低增益——防止追踪波浪
    IF sea_state >= 5:    # BF ≥ 5（清劲风，中浪）
        Kp *= 0.7
        Ki *= 0.5
        Kd *= 1.2    # 增大阻尼
    ELIF sea_state >= 4:
        Kp *= 0.85
        Ki *= 0.7
        Kd *= 1.1
    
    RETURN {Kp, Ki, Kd}
```

### 8.3 基于控制模式的增益调整

```
FUNCTION adjust_gains_for_mode(Kp, Ki, Kd, mode):
    
    IF mode == "heading_hold":
        # 航向保持模式——更强的积分以消除偏差
        Ki *= 1.5
    
    ELIF mode == "heading_track":
        # 航向跟踪模式（跟踪 LOS 输出的时变航向）——更强的 P 和 D
        Kp *= 1.2
        Kd *= 1.2
        Ki *= 0.8    # 降低积分防止跟踪滞后
    
    ELIF mode == "yaw_rate":
        # 角速度控制模式（用于转弯）——另一套控制器（见第 15 节）
        pass
    
    RETURN {Kp, Ki, Kd}
```

---

## 9. 步骤四：积分项管理（抗饱和）

### 9.1 积分饱和问题

当控制器输出饱和（被限幅）时，如果积分项继续累积，会导致"integral windup（积分饱和）"——即使误差已经开始减小，积分累积的巨大值仍然驱动控制器输出在饱和区域，导致过调。

### 9.2 Back-Calculation 抗饱和法

```
FUNCTION apply_anti_windup_back_calculation(integral_e, M_z_raw, M_z_limited, K_bc, dt):
    
    # 计算饱和误差
    saturation_error = M_z_limited - M_z_raw
    
    # 反计算：减少积分项以消除饱和
    integral_e += K_bc × saturation_error × dt
    
    # 额外限幅
    integral_e = clamp(integral_e, -I_MAX / Ki, I_MAX / Ki)
    
    RETURN integral_e

K_bc = 1.0 / (Kp / Ki)    # Back-calculation 系数 = 1/τ_i
```

### 9.3 条件积分（Conditional Integration）

```
FUNCTION conditional_integration(e_ψ, integral_e, M_z_raw, M_z_max, dt):
    
    # 只在以下条件下积分：
    # 1. 控制器未饱和
    # 2. 误差不太大（大误差时不需要积分，P 项已经够强）
    
    IF abs(M_z_raw) < M_z_max × 0.9 AND abs(e_ψ) < INTEGRAL_ACTIVE_THRESHOLD:
        integral_e += e_ψ × dt
    ELSE:
        # 不积分——但也不清零（保持当前值）
        pass
    
    RETURN integral_e

INTEGRAL_ACTIVE_THRESHOLD = 15° = 0.26 rad
```

### 9.4 航向变化时的积分重置

```
# 当 ψ_cmd 发生大幅变化时（比如 LOS 切换航段导致的航向跳变），
# 积分项中累积的旧航向的修正量不应继续影响新航向。

FUNCTION check_integral_reset(ψ_cmd, ψ_cmd_prev):
    
    cmd_change = abs(normalize_pm_pi(ψ_cmd - ψ_cmd_prev))
    
    IF cmd_change > INTEGRAL_RESET_THRESHOLD:
        integral_e = 0
        log_event("integral_reset", {cmd_change_deg: cmd_change × 180/π})

INTEGRAL_RESET_THRESHOLD = 15° = 0.26 rad
```

---

## 10. 步骤五：微分项噪声抑制

### 10.1 波浪滤波器

航向信号中包含波浪引起的高频波动。这些波动的特征频率取决于波浪周期（通常 5~15 秒，对应 0.07~0.2 Hz）。控制器不应对这些波动做反应。

```
FUNCTION wave_filter(e_ψ, filter_state, wave_period):
    
    # 波浪滤波器——陷波器（Notch Filter）或低通滤波器
    
    # 方法 1：低通滤波器（简单有效）
    # 截止频率 = 1 / (2 × wave_period)
    ω_c = 1.0 / (2.0 × wave_period)    # Hz
    τ_wave = 1.0 / (2π × ω_c)          # 时间常数
    α_wave = dt / (τ_wave + dt)
    
    e_ψ_filtered = α_wave × e_ψ + (1 - α_wave) × filter_state.e_prev
    filter_state.e_prev = e_ψ_filtered
    
    RETURN e_ψ_filtered
```

**波浪周期估算：**

如果有波浪传感器或气象数据，直接使用。否则可以从航向信号的频谱分析中在线估算——寻找航向信号中 0.05~0.3 Hz 范围内的峰值频率。

### 10.2 使用角速度反馈消除微分噪声

如第 7.3 节所述，使用 IMU 角速度 r 代替航向微分，是消除微分噪声的最根本方法。IMU 陀螺仪的角速度测量噪声远小于对航向做数值微分的噪声。

---

## 11. 步骤六：控制输出限幅与速率限制

### 11.1 幅值限幅

```
FUNCTION apply_output_limits(M_z_raw, M_z_max):
    
    M_z_limited = clamp(M_z_raw, -M_z_max, M_z_max)
    
    saturated = (abs(M_z_raw) > M_z_max)
    
    # 记录饱和状态（用于抗饱和和监控）
    IF saturated:
        saturation_count += 1
        IF saturation_count > SATURATION_ALARM_THRESHOLD:
            log_event("heading_controller_saturating", {
                M_z_raw: M_z_raw,
                M_z_max: M_z_max,
                heading_error: e_ψ × 180/π
            })
    ELSE:
        saturation_count = 0
    
    RETURN {M_z_limited, saturated}

SATURATION_ALARM_THRESHOLD = 50    # 连续 50 个周期饱和则告警（= 5 秒 @10Hz）
```

### 11.2 速率限制

```
FUNCTION apply_rate_limit(M_z_new, M_z_prev, dt, dM_z_max):
    
    dM_z = M_z_new - M_z_prev
    
    IF abs(dM_z) > dM_z_max × dt:
        # 力矩变化率超限——限制变化速率
        M_z_rate_limited = M_z_prev + sign(dM_z) × dM_z_max × dt
    ELSE:
        M_z_rate_limited = M_z_new
    
    RETURN M_z_rate_limited
```

**速率限制的物理意义：** 力矩需求的急剧变化会导致舵角指令的急剧变化，而舵机的转速有限。如果需求变化太快，实际舵角跟不上，控制器的模型假设（"输出 = 实际执行"）不成立，可能导致不稳定。速率限制确保需求的变化速率在舵机能力范围内。

---

## 12. 步骤七：前馈补偿

### 12.1 航向指令变化率前馈

当 ψ_cmd 在变化时（LOS 引导律在修正航向，或 WOP 混合在进行中），PID 的 P 项是"事后纠偏"——要等误差出现了才修正。前馈项则是"提前补偿"——根据 ψ_cmd 的变化率直接施加一个预期力矩。

```
FUNCTION compute_feedforward(ψ_cmd, ψ_cmd_prev, dt, I_zz):
    
    # 期望的角加速度（近似）
    dψ_cmd = normalize_pm_pi(ψ_cmd - ψ_cmd_prev) / dt
    
    # 如果变化率太大（可能是指令跳变而非连续变化），限制前馈
    dψ_cmd = clamp(dψ_cmd, -MAX_YAW_RATE_CMD, MAX_YAW_RATE_CMD)
    
    # 前馈力矩 = 惯性 × 角加速度
    # 但 dψ_cmd/dt 已经不连续了，需要二阶微分
    # 简化：使用一阶前馈，M_ff = K_ff × dψ_cmd
    M_z_ff = K_FF × dψ_cmd
    
    RETURN M_z_ff

K_FF = I_zz × 0.5    # 前馈增益取惯性的 50%（保守，防过补偿）
MAX_YAW_RATE_CMD = 10° /s = 0.175 rad/s
```

### 12.2 偏流前馈

如果 Drift Correction 提供了偏流修正角 β_drift，Heading Controller 可以直接前馈一个对应的力矩来补偿偏流，而不是等 PID 的积分项慢慢积累：

```
FUNCTION compute_drift_feedforward(β_drift, V, ship_params):
    
    # 维持偏流修正角需要的稳态力矩（近似）
    # 在稳态下，舵产生的侧向力 × 力臂 = 风/流产生的偏转力矩
    # M_drift ≈ K_drift_ff × β_drift × V²
    
    M_z_drift_ff = K_DRIFT_FF × β_drift × V × V
    
    RETURN M_z_drift_ff

K_DRIFT_FF = 100    # 待标定（取决于船型）
```

### 12.3 最终输出合成

```
M_z_total = M_z_pid + M_z_ff + M_z_drift_ff
M_z_limited = apply_output_limits(M_z_total, M_z_max)
M_z_final = apply_rate_limit(M_z_limited, M_z_prev, dt, dM_z_max)
```

---

## 13. 高级控制算法：Backstepping 设计

### 13.1 适用场景

当 PID 的性能不能满足要求时（特别是在大角度转弯和非线性操纵区域），可以升级到 Backstepping 控制器。Backstepping 利用船舶运动方程的结构特性，设计具有全局稳定性证明的非线性控制律。

### 13.2 Backstepping 控制律推导

船舶艏摇运动方程（一阶 Nomoto 模型）：

```
T × dr/dt + r = K × δ + d(t)

其中：
  T = 追随性指数（秒）
  K = 旋回性指数（1/秒）
  δ = 舵角（弧度）
  d(t) = 外部干扰力矩
  r = dψ/dt = 转向角速度
```

**步骤 1：定义误差变量**

```
z_1 = ψ - ψ_cmd                    # 航向误差
z_2 = r - α_1                       # 角速度跟踪误差
α_1 = -k_1 × z_1 + dψ_cmd/dt      # 虚拟控制律（期望角速度）
```

**步骤 2：选择 Lyapunov 函数**

```
V = ½z_1² + ½z_2²
```

**步骤 3：推导控制律**

```
要求 dV/dt < 0：

dV/dt = z_1 × dz_1/dt + z_2 × dz_2/dt
      = z_1 × (r - dψ_cmd/dt) + z_2 × (dr/dt - dα_1/dt)

令控制律为：
M_z = I_zz × (dα_1/dt - k_2 × z_2 - z_1) + 阻尼补偿

展开：
M_z = I_zz × (-k_1 × (r - dψ_cmd/dt) + d²ψ_cmd/dt² - k_2 × (r - α_1) - z_1) + N_r × r

其中 N_r × r 是线性阻尼补偿项。
```

**简化实现：**

```
FUNCTION backstepping_controller(ψ, ψ_cmd, r, dψ_cmd_dt, ship_params, k1, k2):
    
    z1 = normalize_pm_pi(ψ - ψ_cmd)
    α1 = -k1 × z1 + dψ_cmd_dt
    z2 = r - α1
    
    dα1_dt = -k1 × (r - dψ_cmd_dt)    # 忽略 d²ψ_cmd/dt²（小量）
    
    M_z = ship_params.I_zz × (dα1_dt - k2 × z2 - z1) + ship_params.N_r × r
    
    RETURN M_z

# 推荐参数：
# k1 = 0.3~0.5（航向误差收敛速度）
# k2 = 1.0~2.0（角速度跟踪速度）
```

**优势：** 具有全局渐近稳定性证明（Lyapunov），在大角度转弯时比 PID 更稳定。

**劣势：** 需要船舶运动模型参数（I_zz, N_r）；对模型参数误差敏感。

**建议：** V1 使用 PID（调试简单），V2 升级到 Backstepping（性能更好）。

---

## 14. 高级控制算法：模型预测控制（MPC）

### 14.1 适用场景

MPC 在需要同时满足多个约束时表现最优——比如在限制最大舵角、限制转向角速度、同时跟踪航向的多约束场景下。

### 14.2 MPC 基本框架

```
FUNCTION mpc_heading_controller(ψ, r, ψ_cmd_trajectory, ship_model, constraints):
    
    # 在每个控制周期求解一个有限时域优化问题
    
    # 预测模型（离散化 Nomoto 模型）
    # x_{k+1} = A × x_k + B × u_k
    # x = [ψ, r], u = M_z
    
    # 目标函数：
    # min Σ (ψ_k - ψ_cmd_k)² × Q + Σ ΔM_z_k² × R
    #
    # Q = 航向误差权重
    # R = 力矩变化率权重（平滑性）
    
    # 约束：
    # |M_z| ≤ M_z_max
    # |dM_z/dt| ≤ dM_z_max
    # |r| ≤ r_max（最大角速度）
    
    # 求解：QP 问题（二次规划），可用 OSQP 或 qpOASES 求解器
    
    solution = solve_qp(A, B, Q, R, constraints, prediction_horizon=N)
    
    # 取第一个控制量
    M_z = solution.u[0]
    
    RETURN M_z

# MPC 参数：
# 预测步长 N = 20（2 秒，dt=0.1s）
# Q = 1000（航向误差权重）
# R = 0.01（力矩变化率权重）
```

**优势：** 最优控制，自然处理约束，预测性强。

**劣势：** 计算量大（每个周期求解 QP），需要精确的船舶模型。

**建议：** 作为 V3 的高级选项。当 PID 或 Backstepping 在某些工况下表现不佳时再考虑。

---

## 15. 航向保持模式 vs 航向跟踪模式

### 15.1 航向保持模式（Heading Hold）

船长设定一个固定航向，控制器保持。ψ_cmd 为常数。

```
IF guidance_mode == "HEADING_HOLD":
    # 更强的积分——消除风流导致的稳态偏差
    Ki_effective = Ki × 1.5
    # 更强的波浪滤波——减少舵机摆动
    wave_filter_tau *= 1.5
```

### 15.2 航向跟踪模式（Heading Track）

跟踪 LOS 引导律输出的时变 ψ_cmd。这是正常航线跟踪时的模式。

```
IF guidance_mode == "HEADING_TRACK":
    # 更强的 P 和 D——跟踪响应速度
    Kp_effective = Kp × 1.2
    Kd_effective = Kd × 1.2
    # 降低积分——防止跟踪滞后
    Ki_effective = Ki × 0.8
    # 启用前馈补偿
    feedforward_enabled = true
```

### 15.3 角速度控制模式（Yaw Rate Control）

在某些场景下（如 WOP 转弯过程中），Guidance 层可能直接下达期望角速度 r_cmd 而非期望航向。此时切换到角速度 PID：

```
IF guidance_mode == "YAW_RATE":
    e_r = r_cmd - r    # 角速度误差
    
    M_z = Kp_rate × e_r + Ki_rate × ∫e_r dt + Kd_rate × de_r/dt
    
    # 角速度 PID 的参数与航向 PID 不同
    # Kp_rate ≈ I_zz × 2.0（更快的响应）
    # Ki_rate ≈ Kp_rate × 0.5
    # Kd_rate ≈ 0（角速度的微分噪声太大，通常不用 D）
```

---

## 16. 低速操纵模式（差动/侧推辅助）

### 16.1 问题描述

当航速 < V_rudder_effective（约 2~3 m/s），舵的水动力几乎为零——传统的舵力矩无法实现航向控制。此时需要使用差动推力和/或侧推器来产生艏摇力矩。

### 16.2 低速模式切换

```
FUNCTION check_low_speed_mode(V, V_threshold):
    
    IF V < V_threshold × 0.7:
        RETURN "THRUSTER_ONLY"      # 完全依赖差动/侧推
    ELIF V < V_threshold:
        RETURN "MIXED"              # 舵 + 差动/侧推混合
    ELSE:
        RETURN "RUDDER_ONLY"        # 正常舵控制

V_RUDDER_EFFECTIVE = 2.5    # m/s（约 5 节）
```

### 16.3 控制器行为调整

```
IF low_speed_mode == "THRUSTER_ONLY":
    # 差动和侧推的响应特性与舵不同
    # 差动推力几乎无延迟，但力矩受限于推力差
    # 侧推器有启动延迟（几秒），但力矩大
    
    # 降低 PID 增益——差动推力精度有限
    Kp_effective *= 0.5
    Ki_effective *= 0.3
    Kd_effective *= 0.3
    
    # 增大死区——低速时不追求高精度
    deadband_effective = 3.0° = 0.052 rad

ELIF low_speed_mode == "MIXED":
    # 舵和差动/侧推混合使用
    # PID 输出的 M_z 由 Thrust Allocator 分配到舵和推力器
    # 控制器本身不需要知道分配细节
    
    Kp_effective *= 0.8
    deadband_effective = 1.5°
```

---

## 17. 转弯控制增强

### 17.1 大角度转弯的挑战

在大角度转弯（> 30°）中，PID 控制器面临以下挑战：

1. **P 项过大：** 30° 误差下 P 项可能使 M_z 饱和。
2. **D 项过度阻尼：** 转弯中角速度 r 很大，D = -Kd × r 会产生一个与转弯方向相反的大力矩，抵消 P 项——导致转弯变慢。
3. **积分累积：** 长时间的大误差导致积分项累积过大。

### 17.2 解决方案

```
FUNCTION adjust_for_large_turn(e_ψ, Kp, Ki, Kd):
    
    abs_error = abs(e_ψ)
    
    IF abs_error > LARGE_TURN_THRESHOLD:
        # 大转弯模式
        
        # 降低 D 增益——不要过度阻尼转弯过程
        Kd_effective = Kd × max(0.3, 1.0 - abs_error / π)
        # 误差越大，D 增益越小（最低 30%）
        
        # 限制 P 项的有效误差——防止饱和
        e_ψ_effective = sign(e_ψ) × min(abs_error, P_SATURATION_ANGLE)
        
        # 暂停积分——大转弯时不需要积分
        integrate = false
    
    ELSE:
        Kd_effective = Kd
        e_ψ_effective = e_ψ
        integrate = true
    
    RETURN {e_ψ_effective, Kd_effective, integrate}

LARGE_TURN_THRESHOLD = 15° = 0.26 rad
P_SATURATION_ANGLE = 30° = 0.52 rad
```

### 17.3 转弯角速度限制

在转弯过程中，角速度不应超过安全限值——过快的转向可能导致横倾或使乘员/货物不适。

```
# 角速度限制作为 PID 输出的额外约束
IF abs(r) > R_MAX_SAFE:
    # 角速度太快——减小力矩需求
    M_z_limited = M_z × (R_MAX_SAFE / abs(r))
    log_event("yaw_rate_limiting", {r: r, r_max: R_MAX_SAFE})

R_MAX_SAFE = 10° /s = 0.175 rad/s    # 中小型 USV 的典型限值
# 大型商船通常 < 3°/s
```

---

## 18. 外部干扰下的鲁棒性

### 18.1 风干扰

恒定横风产生的偏转力矩由 PID 的积分项或偏流前馈补偿。阵风（突发的非恒定风力）产生的航向扰动是高频的，应由 D 项阻尼。

```
# 如果风力突然增大导致航向急剧偏转
IF abs(r) > R_GUST_THRESHOLD AND abs(e_ψ) < SMALL_ERROR:
    # 可能是阵风——不增大 P 输出（误差还小），
    # 但 D 项自动产生反力矩（-Kd × r 对抗突发角速度）
    # 这正是 D 项的设计目的

R_GUST_THRESHOLD = 5° /s
SMALL_ERROR = 3°
```

### 18.2 浪引起的艏摇

波浪引起的周期性艏摇力矩是 Heading Controller 面临的最大挑战。

**不应做的事：** 让控制器追踪每一个浪引起的航向波动。这会导致舵机持续满幅摆动，加速磨损，增加阻力，浪费燃油。

**应该做的事：** 用波浪滤波器（第 10 节）过滤掉波浪频率的误差成分，只控制低频的航向偏差。

**定量要求：** 在 BF5（中浪）以下，舵活动指数（RAI, Rudder Activity Index = 舵角变化率的均方根值）应控制在 < 3°/s。

### 18.3 海流干扰

海流引起的偏转力矩通常是缓慢变化的（除了潮流换向时）。PID 的积分项可以很好地应对。

---

## 19. 故障降级策略

### 19.1 IMU 故障

```
IF imu_health.status == "FAILED":
    # 无角速度反馈——D 项无法使用
    # 切换到纯 P+I 控制（去掉 D 项）
    Kd_effective = 0
    
    # 降低 Kp 防止振荡（无阻尼）
    Kp_effective *= 0.5
    Ki_effective *= 0.3
    
    control_mode = "degraded_no_rate"
    log_event("heading_controller_degraded", "IMU failed, D term disabled")
```

### 19.2 GPS/罗经故障

```
IF heading_sensor_health.status == "FAILED":
    # 无航向反馈——Heading Controller 完全失效
    # 切换到角速度保持模式（如果 IMU 仍可用）
    # r_cmd = 0（保持直行）
    
    control_mode = "degraded_rate_hold"
    log_event("heading_controller_degraded", "Heading sensor failed, rate hold mode")
```

### 19.3 单侧推进故障

```
IF propulsion_health.port.status == "FAILED" OR propulsion_health.stbd.status == "FAILED":
    # 单侧推力缺失——可用的最大力矩减半
    M_z_max_effective = M_z_max × 0.5
    
    # 降低增益防止饱和
    Kp_effective *= 0.6
    Ki_effective *= 0.4
    Kd_effective *= 0.6
    
    control_mode = "degraded_single_engine"
```

---

## 20. 控制器在线自整定

### 20.1 基于响应的自动调参

在平稳航行时，Heading Controller 可以执行小幅阶跃测试（比如 ψ_cmd 突然变化 3°）来评估当前 PID 参数是否合适。

```
FUNCTION auto_tune_check(response_data):
    
    # 分析阶跃响应的性能指标
    overshoot = compute_overshoot(response_data)
    settling_time = compute_settling_time(response_data)
    steady_state_error = compute_sse(response_data)
    
    # 判断是否需要调参
    IF overshoot > 15%:
        # 过调——增大 Kd 或减小 Kp
        suggestion = "增大 Kd 10% 或减小 Kp 10%"
    
    ELIF settling_time > 30s:
        # 响应太慢——增大 Kp
        suggestion = "增大 Kp 15%"
    
    ELIF steady_state_error > 2°:
        # 稳态误差——增大 Ki
        suggestion = "增大 Ki 20%"
    
    RETURN suggestion
```

### 20.2 自整定的安全约束

```
# 自整定只在以下条件下执行：
# 1. 开阔水域（zone_type == "open_sea"）
# 2. 无避碰行动进行中
# 3. 海况良好（BF < 4）
# 4. 航速在基准速度 ±20% 范围内
# 5. 船长/岸基批准（或在仿真中）

# 自整定的参数调整幅度限制在 ±30% 范围内
# 超出此范围的调整需要人工确认
```

---

## 21. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 计算频率 |
|-------|------|---------|---------|
| Error Calculator | 航向误差计算（归一化+死区+限幅） | C++ | 50 Hz |
| Wave Filter | 波浪滤波器 | C++ | 50 Hz |
| Gain Scheduler | 增益调度（速度+海况+模式） | C++ | 10 Hz |
| PID Core | PID 计算（P+I+D）含角速度反馈 | C++ | 50 Hz |
| Anti-Windup | 积分抗饱和（Back-calculation） | C++ | 50 Hz |
| Feedforward | 前馈补偿（指令变化率+偏流） | C++ | 10 Hz |
| Output Limiter | 限幅 + 速率限制 | C++ | 50 Hz |
| Mode Manager | 控制模式切换（保持/跟踪/角速度/降级） | C++ | 10 Hz |
| Degradation Handler | 传感器/执行机构故障响应 | C++ | 1 Hz |
| Auto-Tuner | 在线自整定（可选） | Python | 事件 |

---

## 22. 数值算例

### 22.1 算例一：5° 航向阶跃响应

**条件：**

```
V = 8 m/s, I_zz = 79200 kg·m²
Kp = 5000 Nm/rad, Ki = 200 Nm/(rad·s), Kd = 30000 Nm·s/rad
ψ_cmd 从 0° 阶跃到 5° = 0.087 rad
初始 r = 0, 积分 = 0
```

**第一个控制周期（dt = 0.1s）：**

```
e_ψ = 0.087 rad（5°）
P = 5000 × 0.087 = 435 Nm
I = 200 × 0.087 × 0.1 = 1.74 Nm（积分刚开始，很小）
D = -30000 × 0 = 0 Nm（初始角速度为零）
FF = K_FF × (0.087/0.1) = 0.5 × 79200 × 0.87 = 34452 Nm（前馈很大！）

→ 前馈主导初始响应，使船迅速建立转向角速度
→ 但前馈只在指令变化时存在，稳态后 = 0

M_z_total = 435 + 1.74 + 0 + 34452 ≈ 34889 Nm
→ 如果 M_z_max = 20000 Nm，则被限幅到 20000 Nm
→ 饱和！前馈值太大——需要限制前馈增益

调整：K_FF = I_zz × 0.1（降至 10%）
FF = 0.1 × 79200 × 0.87 = 6890 Nm
M_z_total = 435 + 1.74 + 0 + 6890 = 7327 Nm（在限幅内）
```

**后续响应（简化模拟）：**

```
t=0.5s:  ψ ≈ 0.5°, r ≈ 0.5°/s, e_ψ = 4.5°
         P = 393, D = -30000×0.0087 = -261, FF ≈ 0（指令不再变化）
         M_z = 393 + (small I) - 261 ≈ 140 Nm

t=2.0s:  ψ ≈ 3.0°, r ≈ 1.5°/s, e_ψ = 2.0°
         P = 175, D = -30000×0.026 = -780
         M_z = 175 + (I) - 780 ≈ -600 Nm（D 项产生"回舵"力矩）

t=5.0s:  ψ ≈ 4.8°, r ≈ 0.2°/s, e_ψ = 0.2°
         P = 17, D = -30000×0.003 = -90, I ≈ 小值
         M_z ≈ -73 Nm（微小的稳定力矩）

t=10.0s: ψ ≈ 5.0°, r ≈ 0, e_ψ ≈ 0
         → 稳态达到，无过调
```

**结论：** 过调 ≈ 0%（临界阻尼），稳定时间 ≈ 8 秒。性能良好。

### 22.2 算例二：横风干扰

```
V = 8 m/s, 恒定横风导致偏转力矩 M_wind = 1000 Nm

没有积分项时：
  稳态误差 = M_wind / Kp = 1000 / 5000 = 0.2 rad = 11.5°（不可接受！）

有积分项时（Ki = 200）：
  积分会缓慢累积，直到 Ki × ∫e dt = M_wind
  稳态积分值 = M_wind / Ki = 1000 / 200 = 5 rad·s
  → 稳态误差 → 0（积分完全补偿了风力矩）
  → 但收敛时间可能需要 30~60 秒

有偏流前馈时（K_DRIFT_FF 已标定）：
  前馈立即补偿大部分风力矩
  → 积分只需要补偿残余
  → 收敛时间 < 10 秒
```

---

## 23. 参数总览表

| 类别 | 参数 | 符号 | 默认值 | 说明 |
|------|------|------|-------|------|
| **PID 基准** | 比例增益 | K_p | 5000 Nm/rad | 待标定 |
| | 积分增益 | K_i | 200 Nm/(rad·s) | 待标定 |
| | 微分增益（角速度反馈） | K_d | 30000 Nm·s/rad | 待标定 |
| | 微分滤波系数 | N | 10 | |
| **增益调度** | 极低速 K_p 倍率 | — | 0.3 | V < 1.5 m/s |
| | 低速 K_p 倍率 | — | 0.5 | V < 3.0 m/s |
| | 中速 K_p 倍率 | — | 1.0 | 基准 |
| | 高速 K_p 倍率 | — | 0.7 | V > 9.0 m/s |
| **限幅** | 最大力矩需求 | M_z_max | 待标定 (Nm) | 执行机构决定 |
| | 最大力矩变化率 | dM_z_max | M_z_max/2 (Nm/s) | |
| | 航向误差死区 | ψ_deadband | 0.5° | |
| | 最大有效误差 | — | 60° | |
| **积分管理** | 积分上限 | I_max | 0.3 × M_z_max | |
| | 积分激活阈值 | — | 15° | 大于此不积分 |
| | 积分重置阈值 | — | 15°（指令变化） | |
| | Back-calc 系数 | K_bc | 1/τ_i | |
| **前馈** | 前馈增益 | K_FF | 0.1 × I_zz | 待标定 |
| | 偏流前馈增益 | K_DRIFT_FF | 100 | 待标定 |
| | 最大前馈角速度 | — | 10°/s | |
| **波浪滤波** | 滤波时间常数 | τ_wave | 自适应 | |
| **转弯** | 大转弯阈值 | — | 15° | |
| | D 增益最低倍率 | — | 0.3 | |
| | 最大安全角速度 | R_MAX | 10°/s | |
| **低速** | 舵效下限速度 | V_rudder_eff | 2.5 m/s | |
| | 低速死区 | — | 3.0° | |

---

## 24. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| Guidance (L4) → Heading Ctrl | 输入 | ψ_cmd, r_cmd, guidance_mode | 10 Hz |
| Nav Filter → Heading Ctrl | 输入 | ψ, r, u, v | 50 Hz |
| Drift Correction → Heading Ctrl | 输入 | β_drift（偏流前馈） | 10 Hz |
| Heading Ctrl → Force Calculator | 输出 | M_z_demand（艏摇力矩需求） | 50 Hz |
| System Monitor → Heading Ctrl | 输入 | 传感器/执行机构健康状态 | 1 Hz |
| Heading Ctrl → System Monitor | 输出 | 饱和告警、降级状态 | 事件 |
| Perception → Heading Ctrl | 输入 | sea_state（海况，用于增益调整） | 0.2 Hz |
| Speed Controller → Heading Ctrl | 协调 | 同步速度变化时的增益调度 | 10 Hz |

---

## 25. 测试策略与验收标准

### 25.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| HDG-001 | 5° 航向阶跃响应 | 过调 < 10%，稳定时间 < 15s |
| HDG-002 | 30° 航向阶跃响应 | 过调 < 15%，稳定时间 < 30s |
| HDG-003 | 90° 大转弯响应 | 无振荡，转弯时间合理 |
| HDG-004 | 恒定 1000 Nm 偏转干扰 | 稳态误差 < 2°（积分生效） |
| HDG-005 | 阵风干扰（脉冲力矩） | 航向扰动 < 5°，3s 内恢复 |
| HDG-006 | 波浪中航向保持（BF4） | RAI < 3°/s，平均 CTE < 5m |
| HDG-007 | 波浪中航向保持（BF6） | 不振荡，舵机不过度摆动 |
| HDG-008 | 低速（2 m/s）航向控制 | 差动/侧推接管，航向误差 < 10° |
| HDG-009 | 高速（10 m/s）航向控制 | 增益调度正确，无振荡 |
| HDG-010 | 速度从 2→8 m/s 变化中 | 增益平滑过渡，不振荡 |
| HDG-011 | 连续时变 ψ_cmd（LOS 跟踪）| 跟踪误差 < 3°，前馈有效 |
| HDG-012 | IMU 故障→降级 | 切换到 P+I 模式，不振荡 |
| HDG-013 | 罗经故障→降级 | 切换到角速度保持模式 |
| HDG-014 | 单侧推进故障→降级 | M_z_max 减半，不饱和告警过多 |
| HDG-015 | 积分饱和→抗饱和 | 无过调（与无抗饱和对比） |
| HDG-016 | ψ_cmd 阶跃 30°→积分重置 | 积分正确重置，不影响新航向 |
| HDG-017 | 前馈 vs 无前馈对比 | 前馈使转弯更快更平滑 |
| HDG-018 | Backstepping vs PID 对比 | 大转弯时 Backstepping 更优 |
| HDG-019 | 控制力占比监控 | 正常航行 < 30%，转弯 < 70% |
| HDG-020 | 自整定建议生成 | 过调/慢响应正确诊断 |

### 25.2 验收标准

| 指标 | 标准 |
|------|------|
| 5° 阶跃过调量 | < 10% |
| 5° 阶跃稳定时间 | < 15 秒 |
| 30° 阶跃过调量 | < 15% |
| 90° 转弯无振荡 | 转弯后航向波动 < 3° |
| 稳态误差（有恒定干扰） | < 2° |
| 航向保持精度（BF4 以下） | ±3° |
| 舵活动指数 RAI（BF4） | < 3°/s |
| 控制器更新频率 | ≥ 50 Hz |
| 计算延迟 | < 0.5 ms |
| 增益调度过渡平滑性 | 增益变化率 < 10%/s |
| 故障降级切换时间 | < 100 ms |

---

## 26. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics and Motion Control" Ch.13-14, 2021 | 航向控制理论（PID/Backstepping/MPC） |
| [2] | Åström, K.J. & Hägglund, T. "Advanced PID Control" 2006 | PID 设计、抗饱和、自整定 |
| [3] | IMO MSC.64(67) Annex 3 | 自动舵性能标准 |
| [4] | Nomoto, K. et al. "On the Steering Qualities of Ships" ISP, 1957 | Nomoto 模型 |
| [5] | Fossen, T.I. & Strand, J.P. "Nonlinear Ship Heading Controller" 1999 | Backstepping 航向控制 |
| [6] | Perez, T. "Ship Motion Control" Springer, 2005 | 船舶运动控制综合 |
| [7] | DNV Rules for Classification "Ships" Pt.4 Ch.9 | 自动舵和航迹控制要求 |

---

## v2.0 架构升级：硬线仲裁器接口与反射弧输入

### A. 硬线仲裁器（Hardware Override Arbiter）接口

在 v2.0 架构中，Heading Controller 的输出（δ_cmd）不直接传递给舵机。而是经过硬线仲裁器（Hardware Override Arbiter）——一个纯硬件的信号选择器：

```
Heading Controller → δ_cmd (AI) ─┐
                                  │
                        ┌─────────┴─────────┐
                        │ Hardware Override  │
                        │    Arbiter        │
                        │                   │
                        │ SELECT =          │
                        │ IF override_relay  │
                        │    → δ_manual     │
                        │ ELSE              │
                        │    → δ_cmd (AI)   │
                        └─────────┬─────────┘
                                  │
OOW 物理舵轮 → δ_manual (人工) ─┘
                                  │
                                  ▼
                              舵机执行
```

**关键设计：** 仲裁器是继电器级硬件，不经过任何软件判断。当 OOW 操作物理舵轮时，舵轮上的压力传感器或行程开关触发继电器，物理切断 AI 的 δ_cmd 信号线，接通人工的 δ_manual 信号线。

**Heading Controller 的感知：** 仲裁器有一个状态反馈信号（override_active），通过硬件 GPIO 读入 L5 控制节点。Heading Controller 在检测到 override_active = true 时：

```
FUNCTION handle_override_active():
    
    # 1. 停止输出控制指令（即使计算仍在运行）
    controller_mode = "OVERRIDE_STANDBY"
    
    # 2. 重置积分项（防止接管结束时积分饱和导致的瞬态）
    integral_e = 0
    
    # 3. 持续跟踪实际舵角（从舵角反馈读取）
    # 当接管结束时，从当前实际舵角开始恢复，实现无缝切换
    δ_at_takeover = actual_rudder_feedback
    
    # 4. 记录事件到 ASDR
    asdr_publish("override_activated", {
        time: now(),
        ai_last_command: δ_cmd_last,
        actual_rudder: actual_rudder_feedback,
        heading: own_heading,
        speed: own_speed
    })

FUNCTION handle_override_released():
    
    # 接管结束——平滑恢复 AI 控制
    controller_mode = "RESUMING"
    
    # 从当前实际舵角开始（不是从 AI 的上一次指令开始）
    δ_cmd = actual_rudder_feedback    # 初始值 = 当前舵角
    
    # 逐步过渡到 AI 计算的航向跟踪指令
    # 过渡时间 5 秒
    resume_transition_start = now()
    
    asdr_publish("override_released", {time: now(), actual_rudder: actual_rudder_feedback})

FUNCTION compute_during_resume(δ_pid, δ_at_release, t_since_release):
    
    IF t_since_release < RESUME_TRANSITION_TIME:
        blend = t_since_release / RESUME_TRANSITION_TIME
        RETURN (1 - blend) × δ_at_release + blend × δ_pid
    ELSE:
        controller_mode = "PID"    # 完全恢复
        RETURN δ_pid

RESUME_TRANSITION_TIME = 5.0    # 秒
```

### B. 反射弧紧急输入

当 Risk Monitor 的反射弧触发 FIRE 时，紧急指令通过 `/system/emergency_maneuver` 话题直接到达 L5。Heading Controller 在收到此指令时立即切换到紧急模式：

```
FUNCTION handle_reflex_emergency(emergency_cmd):
    
    controller_mode = "EMERGENCY"
    
    IF emergency_cmd.action == "hard_starboard":
        δ_cmd = δ_MAX    # 满舵右
    ELIF emergency_cmd.action == "hard_port":
        δ_cmd = -δ_MAX   # 满舵左
    ELIF emergency_cmd.action == "crash_stop":
        δ_cmd = 0         # 正舵（停船时不转向）
    
    # 跳过所有滤波和速率限制——紧急模式直接输出
    # 但仍不超过物理机械限位
    δ_cmd = clamp(δ_cmd, -δ_MAX, δ_MAX)
    
    # 紧急模式持续时间由 Risk Monitor 控制
    # 当 EMERGENCY 解除后自动恢复到正常模式
```

### C. 所有 L5 模块的 Override 处理统一规范

Speed Controller、Force Calculator、Thrust Allocator、Actuator Limiter 都需要相同的 Override 处理逻辑。统一规范：

```
所有 L5 模块在检测到 override_active = true 时：
1. 停止输出 AI 控制指令
2. 重置所有积分项
3. 进入 OVERRIDE_STANDBY 模式
4. 记录事件到 ASDR
5. 接管结束后从当前实际状态平滑恢复
```

详细的硬线仲裁器设计见《MASS_ADAS Hardware Override Arbiter 技术设计文档》。

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本——PID 参数待海试标定 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：增加硬线仲裁器接口（Override 感知、积分重置、平滑恢复）；增加反射弧紧急输入处理；增加 ASDR 事件记录 |

---

**文档结束**
