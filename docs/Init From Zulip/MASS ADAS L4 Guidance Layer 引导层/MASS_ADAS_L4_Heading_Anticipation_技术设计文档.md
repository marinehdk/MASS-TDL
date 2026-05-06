# MASS_ADAS Heading Anticipation 航向预偏补偿模块技术设计文档

**文档编号：** SANGO-ADAS-HDA-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的航向预判思维模型
5. 计算流程总览
6. 步骤一：航向误差计算
7. 步骤二：预偏补偿计算
8. 步骤三：接近目标航向时的衰减
9. 步骤四：最终 ψ_cmd 合成与输出
10. 过调（overshoot）抑制
11. 不同速度下的预偏参数自适应
12. 转弯过程中的预偏特殊处理
13. 内部子模块分解
14. 数值算例
15. 参数总览表
16. 与其他模块的协作关系
17. 测试策略与验收标准
18. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Heading Anticipation（航向预偏补偿模块）是 Layer 4（Guidance Layer）的最后一个子模块——它接收前序模块（LOS Guidance + Drift Correction）计算的期望航向，做最终的预偏补偿处理后，生成发布给 L5 Control 层的最终航向指令 ψ_cmd。

Heading Anticipation 解决的问题是：**当船头接近目标航向时，如果不提前减小修正量（"回舵"），船会因为转向惯性而冲过目标航向，导致反复振荡。** 这就像开车转弯时到接近直道时要提前回方向盘一样——如果等到车完全对齐直道才回盘，车一定会摆过头。

### 1.2 核心职责

- **预偏补偿：** 当航向误差（ψ_desired - ψ_current）较小时，衰减修正量，防止过调。
- **航向指令平滑：** 对最终的 ψ_cmd 做平滑处理，避免高频抖动。
- **过调抑制：** 检测到航向过调（冲过目标后反向偏）时，快速稳定。
- **最终输出：** 合成并发布 ψ_cmd 和 u_cmd 给 L5 Control 层。

### 1.3 与 L5 Control 层的关系

Heading Anticipation 输出的 ψ_cmd 是 L5 Heading Controller 的设定值（setpoint）。L5 的 PID 或 Backstepping 控制器负责实际操舵使船头跟踪 ψ_cmd。

如果 L5 的 Heading Controller 设计得足够好（含前馈和微分项），理论上可以不需要 Heading Anticipation——控制器本身能处理过调问题。但在实际工程中，将"接近目标时衰减"的逻辑放在 Guidance 层而非 Control 层有以下好处：

1. **关注点分离：** Guidance 负责"想去哪个方向"，Control 负责"怎么转过去"。衰减逻辑属于"想"的范畴。
2. **调参独立：** Guidance 层的衰减参数和 Control 层的 PID 参数可以独立调整，互不干扰。
3. **更好的跨层可见性：** ψ_cmd 的平滑性可以在 Guidance 层直接验证，不需要观察 L5 的内部状态。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 期望航向（含偏流修正） | LOS + Drift Correction | ψ_desired = ψ_los + β_drift |
| 当前航向 | Nav Filter | ψ_current（弧度） |
| 当前转向角速度 | Nav Filter | r = dψ/dt（rad/s） |
| 当前船速 | Nav Filter | V（m/s） |
| CTE 变化率 | CTE Calculator | de/dt（m/s） |
| WOP 状态 | WOP Module | 是否在转弯过程中 |
| 目标速度指令 | Look-ahead Speed | u_cmd |

### 2.2 输出

```
GuidanceCommand:
    Header header
    float64 heading_cmd         # 最终航向指令 ψ_cmd（弧度 [0, 2π)）
    float64 speed_cmd           # 目标速度指令 u_cmd（m/s）
    float64 yaw_rate_cmd        # 目标角速度指令（rad/s，可选）
    bool use_yaw_rate           # true = 角速度模式（转弯中）; false = 航向模式（直航中）
    float64 cross_track_error   # 当前 CTE（供 Control 层参考）
    uint8 guidance_mode         # 0=TRACK_FOLLOW, 1=HEADING_HOLD, 2=STATION_KEEP
```

发布到 ROS2 话题：`/decision/guidance/cmd`，频率 10 Hz。

---

## 3. 核心参数数据库

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 预偏衰减起始角度 | ψ_decay_start | 15° | 航向误差小于此值时开始衰减 |
| 预偏衰减终止角度 | ψ_decay_end | 2° | 航向误差小于此值时修正量衰减至最低 |
| 预偏衰减因子类型 | — | "cosine" | "linear" / "cosine" / "exponential" |
| 最小修正保持比例 | k_min | 0.1 | 衰减后的最低修正量比例（10%） |
| 角速度阻尼增益 | k_rate_damp | 0.5 | 基于 yaw rate 的阻尼修正 |
| ψ_cmd 最大变化率 | dψ_cmd_max | 5.0 °/s | 防止航向指令跳变 |
| ψ_cmd 平滑时间常数 | τ_cmd | 0.5 s | 低通滤波 |
| 过调检测阈值 | ψ_overshoot_threshold | 5° | 超过此值判定为过调 |
| 过调抑制增益 | k_overshoot | 1.5 | 过调时增大阻尼 |

---

## 4. 船长的航向预判思维模型

### 4.1 船长如何"回舵"

当船长下令"右舵 15"后，船开始向右转。他不会等到船头精确对准目标航向才下令"正舵"——因为船有转向惯性，即使回到零舵角，船头还会继续偏转一段。

他的操作是：**在船头还差目标航向约 5°~15° 时就开始回舵（减小舵角），到差约 2°~5° 时回到正舵。** 这个"提前回舵"的量取决于：

- **转向角速度：** 转得越快，需要越早回舵（因为惯性更大）。
- **船速：** 高速时惯性大，需要更早回舵。
- **船型：** "灵"的船（超越角小的船）需要更早回舵。

### 4.2 Heading Anticipation 如何模拟

Heading Anticipation 用一个衰减函数来实现"提前回舵"：当航向误差小于某个阈值时，逐渐减小 ψ_cmd 与 ψ_current 的差值，让 L5 的 Heading Controller 输出更小的舵角。

---

## 5. 计算流程总览

```
输入：ψ_desired（LOS + Drift 修正后）+ ψ_current + yaw_rate r + 船速 V
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：航向误差计算              │
    │ ψ_error = ψ_desired - ψ_current  │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：预偏补偿计算              │
    │ 基于误差大小计算衰减因子           │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：角速度阻尼修正            │
    │ 基于 yaw rate 做阻尼              │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：过调检测与抑制            │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：ψ_cmd 平滑与限幅          │
    │ 低通滤波 + 变化率限制              │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：最终 GuidanceCommand 合成 │
    │ 合并 ψ_cmd + u_cmd + 模式信息     │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：GuidanceCommand → L5 Control
```

---

## 6. 步骤一：航向误差计算

```
FUNCTION compute_heading_error(ψ_desired, ψ_current):
    
    ψ_error = normalize_pm_pi(ψ_desired - ψ_current)
    
    # ψ_error > 0：需要右转
    # ψ_error < 0：需要左转
    
    RETURN ψ_error
```

---

## 7. 步骤二：预偏补偿计算

### 7.1 衰减函数

当 |ψ_error| 较大时（> ψ_decay_start），修正量不衰减——全力修正。当 |ψ_error| 减小到 ψ_decay_start 以下时，开始衰减修正量。

```
FUNCTION compute_decay_factor(ψ_error, ψ_decay_start, ψ_decay_end, k_min, type):
    
    abs_error = abs(ψ_error)
    
    IF abs_error >= ψ_decay_start:
        RETURN 1.0    # 不衰减
    
    IF abs_error <= ψ_decay_end:
        RETURN k_min    # 最小修正保持
    
    # 在 decay_end ~ decay_start 之间做衰减
    ratio = (abs_error - ψ_decay_end) / (ψ_decay_start - ψ_decay_end)
    
    IF type == "linear":
        factor = k_min + (1.0 - k_min) × ratio
    
    ELIF type == "cosine":
        # 余弦衰减：更平滑的过渡
        factor = k_min + (1.0 - k_min) × 0.5 × (1 - cos(π × ratio))
    
    ELIF type == "exponential":
        factor = k_min + (1.0 - k_min) × (1 - exp(-3 × ratio))
    
    RETURN factor
```

### 7.2 应用衰减

```
FUNCTION apply_anticipation(ψ_current, ψ_desired, ψ_error, decay_factor):
    
    # 衰减后的有效误差
    ψ_error_damped = ψ_error × decay_factor
    
    # 预偏后的航向指令
    ψ_anticipated = ψ_current + ψ_error_damped
    
    RETURN normalize_0_2pi(ψ_anticipated)
```

---

## 8. 步骤三：角速度阻尼修正

### 8.1 原理

当船正在快速转向时（yaw_rate 较大），即使航向误差还不小，也应该开始减小修正量——因为高角速度意味着"船正在快速接近目标航向"，如果不提前减量，惯性会导致过调。

```
FUNCTION apply_rate_damping(ψ_cmd, ψ_current, yaw_rate, k_rate_damp):
    
    # 角速度与目标方向一致时，做阻尼修正
    ψ_error = normalize_pm_pi(ψ_cmd - ψ_current)
    
    # 如果角速度方向与误差方向一致（正在接近目标），做阻尼
    IF sign(yaw_rate) == sign(ψ_error) AND abs(yaw_rate) > 0.5 × π/180:
        # 阻尼修正：减小 ψ_cmd 与 ψ_current 的差
        damping = k_rate_damp × yaw_rate
        ψ_cmd_damped = ψ_cmd - damping
    ELSE:
        ψ_cmd_damped = ψ_cmd
    
    RETURN normalize_0_2pi(ψ_cmd_damped)
```

### 8.2 物理含义

k_rate_damp = 0.5 意味着：如果当前角速度是 2°/s（正在以 2°/s 的速率接近目标航向），那么 ψ_cmd 会减小 0.5 × 2° = 1°。这相当于"提前 0.5 秒量的修正被减去了"——因为 0.5 秒后船会因惯性再转 1°。

---

## 9. 步骤四：接近目标航向时的衰减（详细示例）

```
假设：
  ψ_desired = 050°（LOS + Drift 计算的期望航向）
  ψ_current = 035°（当前航向）
  yaw_rate = 2°/s（正在右转中）
  ψ_decay_start = 15°, ψ_decay_end = 2°, k_min = 0.1

步骤一：ψ_error = 050° - 035° = 15°

步骤二：|ψ_error| = 15° = ψ_decay_start → 衰减因子 = 1.0（刚好在边界）
         ψ_error_damped = 15° × 1.0 = 15°
         ψ_anticipated = 035° + 15° = 050°

步骤三：角速度阻尼
         damping = 0.5 × 2° = 1°
         ψ_cmd = 050° - 1° = 049°

———— 1 秒后 ————

  ψ_current = 037°（已经转了 2°）
  ψ_error = 050° - 037° = 13°
  
  衰减因子：(13-2)/(15-2) = 11/13 = 0.846
  → ψ_error_damped = 13° × 0.846 = 11.0°
  → ψ_anticipated = 037° + 11° = 048°
  → damping = 0.5 × 2° = 1°
  → ψ_cmd = 047°

———— 3 秒后 ————

  ψ_current = 043°
  ψ_error = 050° - 043° = 7°
  
  衰减因子：(7-2)/(15-2) = 5/13 = 0.385
  → ψ_error_damped = 7° × 0.385 = 2.7°
  → ψ_anticipated = 043° + 2.7° = 045.7°
  → 角速度已减小到 1°/s
  → damping = 0.5 × 1° = 0.5°
  → ψ_cmd = 045.2°

———— 5 秒后 ————

  ψ_current = 048°
  ψ_error = 050° - 048° = 2°
  
  衰减因子：(2-2)/(15-2) = 0 → k_min = 0.1
  → ψ_error_damped = 2° × 0.1 = 0.2°
  → ψ_anticipated = 048° + 0.2° = 048.2°
  → 角速度 ≈ 0.5°/s
  → ψ_cmd ≈ 048.0°

→ 船头最终稳定在 ~050° 附近，无过调
```

---

## 10. 过调（overshoot）抑制

### 10.1 过调检测

```
FUNCTION detect_overshoot(ψ_desired, ψ_current, ψ_error_history):
    
    ψ_error = normalize_pm_pi(ψ_desired - ψ_current)
    
    # 检查误差是否发生了符号翻转（从一侧偏到另一侧）
    IF len(ψ_error_history) > 0:
        prev_error = ψ_error_history[-1]
        
        IF sign(ψ_error) != sign(prev_error) AND abs(prev_error) > ψ_OVERSHOOT_THRESHOLD:
            # 误差符号翻转且翻转前误差较大——过调！
            RETURN {overshoot: true, magnitude: abs(ψ_error)}
    
    ψ_error_history.append(ψ_error)
    
    RETURN {overshoot: false}
```

### 10.2 过调抑制

```
FUNCTION suppress_overshoot(ψ_cmd, ψ_current, yaw_rate, k_overshoot):
    
    # 检测到过调时，增大阻尼增益
    # 目的是快速减小角速度，防止继续冲过
    
    enhanced_damping = k_overshoot × k_rate_damp    # 增大阻尼 50%
    ψ_cmd_suppressed = apply_rate_damping(ψ_cmd, ψ_current, yaw_rate, enhanced_damping)
    
    RETURN ψ_cmd_suppressed
```

### 10.3 反复振荡检测

```
FUNCTION detect_oscillation(ψ_error_history):
    
    # 检查最近 30 秒内误差符号翻转的次数
    sign_changes = count_sign_changes(ψ_error_history[-150:])    # 10Hz × 30s = 300 样本取后 150
    
    IF sign_changes > 4:    # 30 秒内翻转 > 4 次——振荡
        RETURN {oscillating: true, count: sign_changes}
    
    RETURN {oscillating: false}
```

当检测到振荡时的处理：

```
IF oscillation.oscillating:
    # 增大前视距离（让 LOS 更温和）
    request_lookahead_increase(factor=1.5)
    
    # 增大衰减范围
    ψ_decay_start_temp = ψ_decay_start × 1.5
    
    # 减小修正增益
    decay_factor *= 0.7
    
    log_event("heading_oscillation_detected", {count: oscillation.count})
```

---

## 11. 不同速度下的预偏参数自适应

```
FUNCTION adapt_anticipation_params(V):
    
    # 高速时需要更大的衰减范围（惯性大，需要更早回舵）
    IF V > 8.0:
        ψ_decay_start_adapted = ψ_decay_start × (V / 8.0)
        k_rate_damp_adapted = k_rate_damp × (V / 8.0)
    ELIF V < 3.0:
        # 低速时衰减可以更小（惯性小，精确跟踪）
        ψ_decay_start_adapted = ψ_decay_start × 0.7
        k_rate_damp_adapted = k_rate_damp × 0.5
    ELSE:
        ψ_decay_start_adapted = ψ_decay_start
        k_rate_damp_adapted = k_rate_damp
    
    RETURN {ψ_decay_start_adapted, k_rate_damp_adapted}
```

---

## 12. 转弯过程中的预偏特殊处理

### 12.1 WOP 区域内的预偏调整

在 WOP 混合引导区域内（步骤四的 LOS WOP Blender 正在工作），ψ_desired 本身在快速变化（从 α_k 向 α_{k+1} 过渡）。此时不应做衰减——否则会减慢转弯。

```
FUNCTION adjust_for_wop_zone(wop_status, decay_factor):
    
    IF wop_status.wop_status == "in_wop_zone":
        # WOP 区域内——禁用衰减，全力转向
        RETURN 1.0    # 不衰减
    
    RETURN decay_factor
```

### 12.2 大角度转弯的角速度模式

对于大角度转弯（> 60°），Heading Anticipation 可以切换到角速度模式——输出 yaw_rate_cmd 而非 heading_cmd，让 L5 直接控制转向速率而非航向角：

```
FUNCTION check_yaw_rate_mode(ψ_error, turn_angle, wop_status):
    
    IF wop_status.wop_status == "in_wop_zone" AND abs(turn_angle) > 60°:
        # 大角度转弯中——使用角速度模式
        # 目标角速度：基于旋回特性计算
        R = get_turning_radius(V)
        yaw_rate_cmd = V / R × sign(turn_angle)
        
        RETURN {
            use_yaw_rate: true,
            yaw_rate_cmd: yaw_rate_cmd,
            heading_cmd: ψ_desired    # 仍然传递目标航向作为参考
        }
    
    RETURN {use_yaw_rate: false}
```

---

## 13. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Error Calculator | 航向误差计算（含归一化） | C++ |
| Decay Calculator | 预偏衰减因子计算 | C++ |
| Rate Damper | 角速度阻尼修正 | C++ |
| Overshoot Detector | 过调检测和振荡检测 | C++ |
| Command Smoother | ψ_cmd 平滑和限幅 | C++ |
| Mode Selector | 航向模式 / 角速度模式切换 | C++ |
| Command Publisher | 最终 GuidanceCommand 合成和发布 | C++ |

---

## 14. 数值算例

（已在第 9 节中给出完整的逐步骤时序示例）

补充一个**过调场景**：

```
ψ_desired = 000°（正北）
初始 ψ_current = 330°（需要右转 30°）
船速 8 m/s，角速度 3°/s

无 Heading Anticipation 时：
  t=0s:  ψ=330°, error=30°, 满舵右转
  t=5s:  ψ=345°, error=15°, 仍然满舵
  t=10s: ψ=000°, error=0°, 回正舵...但角速度还有 2°/s
  t=11s: ψ=002°, error=-2°, 过调！开始左修
  t=12s: ψ=003°, error=-3°, 继续左修
  t=15s: ψ=000°, 回到目标...但可能再次过调
  → 振荡

有 Heading Anticipation 时：
  t=0s:  ψ=330°, error=30°, decay=1.0, ψ_cmd=000°
  t=5s:  ψ=345°, error=15°, decay=1.0, damping=0.5×3°=1.5°, ψ_cmd=358.5°
  t=7s:  ψ=351°, error=9°, decay=0.54, error_damped=4.9°, ψ_cmd=355.9°, damping=1°
         → L5 收到更温和的指令，开始回舵
  t=9s:  ψ=357°, error=3°, decay=0.15, error_damped=0.45°, ψ_cmd=357.5°
         → 几乎不再修正，角速度自然衰减
  t=12s: ψ=359.5°, error=0.5°, 微调
  t=15s: ψ=000°, 稳定，无过调
```

---

## 15. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 衰减起始角 | 15° | |
| 衰减终止角 | 2° | |
| 衰减类型 | "cosine" | 余弦衰减最平滑 |
| 最小修正保持 | 0.1（10%） | |
| 角速度阻尼增益 | 0.5 | |
| ψ_cmd 最大变化率 | 5 °/s | |
| ψ_cmd 平滑时间常数 | 0.5 s | |
| 过调检测阈值 | 5° | |
| 过调抑制增益 | 1.5 × k_rate_damp | |
| 振荡检测翻转次数 | > 4 次/30s | |
| WOP 区域内衰减 | 禁用（= 1.0） | |
| 角速度模式转角阈值 | > 60° | |

---

## 16. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| LOS + Drift → Heading Anticip. | 输入 | ψ_desired（期望航向） |
| Nav Filter → Heading Anticip. | 输入 | ψ_current, yaw_rate, V |
| CTE Calculator → Heading Anticip. | 输入 | cte_rate（辅助判断） |
| WOP Module → Heading Anticip. | 输入 | wop_status（转弯区域判定） |
| Look-ahead Speed → Heading Anticip. | 输入 | u_cmd（速度指令） |
| **Heading Anticip. → L5 Control** | **输出** | **GuidanceCommand（最终 ψ_cmd + u_cmd）** |

**这是整个 Guidance 层的最终输出口**——GuidanceCommand 从这里发布到 `/decision/guidance/cmd`，是 L4 到 L5 的唯一数据通道。

---

## 17. 测试策略与验收标准

| 场景 | 验证目标 | 验收标准 |
|------|---------|---------|
| HDA-001: 30° 航向修正 | 无过调收敛 | 过调 < 2° |
| HDA-002: 60° 航向修正 | 大角度无振荡 | 过调 < 5°, 无振荡 |
| HDA-003: 90° 转弯（WOP 区域） | WOP 区域内不衰减 | 转弯速度正常 |
| HDA-004: 高速 15 节 | 自适应衰减增大 | 过调 < 3° |
| HDA-005: 低速 3 节 | 精确跟踪 | 稳态误差 < 1° |
| HDA-006: 恒定偏差（流干扰） | 不因衰减导致偏差不修正 | 稳态 CTE < 5m |
| HDA-007: 故意制造过调 | 过调检测触发 + 抑制 | 2 次振荡内收敛 |
| HDA-008: 连续 S 型航线 | 反复转弯无累积振荡 | 无振荡 |
| HDA-009: ψ_cmd 平滑性 | 输出无阶跃 | 变化率 < 5°/s |
| HDA-010: 角速度模式（大转弯） | 平滑转弯 | 角速度跟踪误差 < 10% |

---

## 18. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" 2021 | 航向控制理论 |
| [2] | Åström, K.J. & Hägglund, T. "Advanced PID Control" 2006 | PID 抗过调技术 |
| [3] | Skjetne, R. et al. "Nonlinear Ship Manoeuvring" 2004 | 船舶航向控制 |

---

**文档结束**
