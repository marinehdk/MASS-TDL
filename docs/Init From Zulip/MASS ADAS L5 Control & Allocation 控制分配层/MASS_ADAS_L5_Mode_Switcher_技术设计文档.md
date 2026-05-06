# MASS_ADAS Mode Switcher 模式切换器技术设计文档

**文档编号：** SANGO-ADAS-MOD-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的模式切换思维模型
5. 模式定义
6. 状态机设计
7. 切换条件详解
8. 切换过渡管理
9. 迟滞带设计
10. 紧急模式切换
11. 故障降级模式
12. 内部子模块分解
13. 数值算例
14. 参数总览表
15. 与其他模块的协作关系
16. 测试策略与验收标准
17. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Mode Switcher（模式切换器）是 Layer 5 中负责管理控制模式转换的子模块。它根据当前航速、操纵状态和任务需求，在不同控制模式之间切换——核心切换是 **Transit 模式（高速巡航）** 和 **Maneuvering 模式（低速操纵）** 之间的过渡。

### 1.2 为什么需要模式切换

在高速巡航时，航向控制主要依靠舵——舵效与速度的平方成正比，高速时舵力很强。但在低速操纵时（如港内、靠泊），舵效几乎为零——必须依靠差动推力和侧推器来控制航向和横向运动。

两种模式下的推力分配矩阵结构完全不同。在 Transit 模式下，Thrust Allocator 的主要自由度是"螺旋桨转速 + 舵角"；在 Maneuvering 模式下，自由度变成"左/右螺旋桨差动转速 + 侧推器推力"。

如果不做模式切换，直接用 Transit 模式的参数在低速下控制，结果是舵效极弱而控制器疯狂增大舵角指令（饱和），航向完全失控。

### 1.3 核心职责

- **模式判定：** 根据当前速度和操纵状态确定应该处于什么模式。
- **切换触发：** 在切换条件满足时触发模式切换。
- **过渡管理：** 确保切换过程平滑——不能在切换瞬间出现控制力中断或跳变。
- **迟滞保护：** 防止在速度边界附近频繁来回切换（乒乓效应）。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| 纵荡速度 u | Nav Filter | 50 Hz | 对水速度（m/s） |
| 转向角速度 r | Nav Filter | 50 Hz | rad/s |
| 水域类型 | ENC Reader | 事件 | zone_type |
| 避碰状态 | Tactical Layer | 2 Hz | 是否有避碰行动进行中 |
| 紧急指令 | Risk Monitor / System | 事件 | 紧急停船/紧急转向 |
| 系统健康 | System Monitor | 1 Hz | 执行机构状态 |

### 2.2 输出

```
ModeState:
    Header header
    string current_mode            # "TRANSIT"/"MANEUVERING"/"TRANSITION"/"EMERGENCY"/"DEGRADED"
    string previous_mode           # 上一个模式
    float64 transition_progress    # 过渡进度 [0,1]（0=旧模式，1=新模式）
    float64 rudder_effectiveness   # 当前估计的舵效系数 [0,1]
    bool bow_thruster_enabled      # 侧推器是否启用
    bool differential_thrust_enabled # 差动推力是否启用
```

---

## 3. 核心参数数据库

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| Transit→Maneuver 切换速度 | V_T2M | 3.0 m/s | 低于此进入 Maneuvering |
| Maneuver→Transit 切换速度 | V_M2T | 4.0 m/s | 高于此回到 Transit |
| 迟滞带宽度 | ΔV_hysteresis | V_M2T - V_T2M = 1.0 m/s | 防止乒乓 |
| 切换过渡时间 | T_transition | 10 秒 | 线性过渡 |
| 舵效下限速度 | V_rudder_min | 2.0 m/s | 低于此舵效 ≈ 0 |
| 侧推器有效上限速度 | V_thruster_max | 4.0 m/s | 高于此侧推效率急剧下降 |
| 港内强制 Maneuvering 速度 | V_port_force | 5.0 m/s | 港内即使速度高也用 Maneuvering |

---

## 4. 船长的模式切换思维模型

船长在进港过程中的思维转变是这样的：

**开阔水域（12 节以上）：** "用自动舵，舵控，主机定速。"——Transit 模式。

**进港航道（8~10 节）：** "还是舵控，但准备好随时用车（主机）协助转弯。"——仍是 Transit 但开始关注速度。

**港内转弯（5~6 节）：** "舵效开始弱了，大转弯可能需要用车。通知轮机备车。"——接近切换点。

**靠泊接近（2~3 节）：** "舵没用了。全靠车控——左进右退（差动）。如果有侧推就用侧推。"——Maneuvering 模式。

Mode Switcher 的切换速度阈值就对应这个过渡过程中"舵效从强到弱"的物理渐变。

---

## 5. 模式定义

| 模式 | 速度范围 | 主要执行机构 | 推力分配特征 |
|------|---------|------------|------------|
| TRANSIT | V > V_M2T | 螺旋桨 + 舵 | 舵角控制航向，螺旋桨控制速度 |
| MANEUVERING | V < V_T2M | 差动推力 + 侧推器 | 差动控制航向，侧推控制横向 |
| TRANSITION | V_T2M ≤ V ≤ V_M2T | 混合 | 两套机制按速度比例混合 |
| EMERGENCY | 任意 | 全部可用 | 最大能力输出，不考虑效率 |
| DEGRADED | 任意 | 取决于故障 | 有啥用啥 |

---

## 6. 状态机设计

```
TRANSIT ←──(V > V_M2T)──→ TRANSITION ←──(V < V_T2M)──→ MANEUVERING
   ↑                          ↑                              ↑
   └──────── EMERGENCY（紧急，从任何状态进入）──────────────────┘
   └──────── DEGRADED（故障，从任何状态进入）──────────────────┘
```

### 6.1 状态转换逻辑

```
FUNCTION update_mode(current_mode, V, zone_type, emergency, health):
    
    # 紧急覆盖
    IF emergency.active:
        RETURN "EMERGENCY"
    
    # 故障覆盖
    IF NOT health.all_actuators_ok:
        RETURN "DEGRADED"
    
    # 港内强制 Maneuvering
    IF zone_type IN ["port_inner", "port_approach"] AND V < V_PORT_FORCE:
        IF current_mode == "TRANSIT":
            RETURN "TRANSITION"    # 开始过渡
        RETURN "MANEUVERING"
    
    # 正常切换逻辑（含迟滞）
    SWITCH current_mode:
        
        CASE "TRANSIT":
            IF V < V_T2M:
                RETURN "TRANSITION"
        
        CASE "MANEUVERING":
            IF V > V_M2T:
                RETURN "TRANSITION"
        
        CASE "TRANSITION":
            IF V < V_T2M - 0.5:        # 完全进入 Maneuvering 区
                RETURN "MANEUVERING"
            IF V > V_M2T + 0.5:        # 完全进入 Transit 区
                RETURN "TRANSIT"
            # 否则继续 TRANSITION
    
    RETURN current_mode
```

---

## 7. 切换条件详解

### 7.1 Transit → Maneuvering

```
触发条件：V < V_T2M (3.0 m/s) 持续 > 3 秒
  AND 非暂时减速（检查 u_cmd 也在降低）

NOT 触发：
  V 瞬间低于阈值但 u_cmd 仍高（可能是浪引起的瞬时减速）
```

### 7.2 Maneuvering → Transit

```
触发条件：V > V_M2T (4.0 m/s) 持续 > 5 秒
  AND 转向角速度 |r| < 5°/s（不在急转弯中）

NOT 触发：
  V 虽然高但正在急转弯（此时仍需差动辅助）
```

### 7.3 速度条件的持续时间要求

```
FUNCTION check_sustained_speed(V, threshold, direction, required_duration):
    
    IF direction == "below" AND V < threshold:
        sustained_time += dt
    ELIF direction == "above" AND V > threshold:
        sustained_time += dt
    ELSE:
        sustained_time = 0    # 重置
    
    RETURN sustained_time >= required_duration
```

---

## 8. 切换过渡管理

### 8.1 线性混合过渡

在 TRANSITION 状态下，两种模式的控制输出按比例混合：

```
FUNCTION compute_transition_blend(V, V_T2M, V_M2T, transit_output, maneuver_output):
    
    # 混合因子：V_T2M 时为 0（全 Maneuvering），V_M2T 时为 1（全 Transit）
    IF V ≤ V_T2M:
        blend = 0.0
    ELIF V ≥ V_M2T:
        blend = 1.0
    ELSE:
        blend = (V - V_T2M) / (V_M2T - V_T2M)    # 线性插值
    
    # 混合 Thrust Allocator 的分配矩阵权重
    output.rudder_weight = blend
    output.differential_weight = 1.0 - blend
    output.bow_thruster_weight = 1.0 - blend
    
    # 混合控制器增益
    output.heading_gains = blend × transit_gains + (1 - blend) × maneuver_gains
    
    RETURN output
```

### 8.2 执行机构启停时序

```
# Transit → Maneuvering 过渡时：
# 1. 先启动侧推器（需要预热/启动时间）
# 2. 差动推力开始介入
# 3. 舵逐渐退出（权重降低）

# Maneuvering → Transit 过渡时：
# 1. 舵逐渐介入（权重升高）
# 2. 差动推力退出
# 3. 侧推器关闭（V > V_thruster_max 后完全无效）
```

---

## 9. 迟滞带设计

迟滞带防止在切换速度附近频繁切换。V_T2M = 3.0, V_M2T = 4.0 构成 1 m/s 的迟滞带。

```
# 如果不用迟滞，而是用单一阈值 V = 3.5：
# 船在 3.4~3.6 m/s 之间因波浪或微小速度波动反复切换
# 每次切换都导致推力分配矩阵变化——控制力不连续
# 结果：航向出现周期性的小幅跳动，非常不舒服

# 迟滞带确保：从 Transit 进入需要 V < 3.0，从 Maneuvering 回来需要 V > 4.0
# 在 3.0~4.0 之间是 TRANSITION 的线性混合区——平滑过渡，无跳变
```

---

## 10. 紧急模式切换

```
FUNCTION enter_emergency_mode(emergency_type):
    
    current_mode = "EMERGENCY"
    
    # 紧急模式特征：
    # - 所有执行机构全激活（舵 + 差动 + 侧推）
    # - 控制器增益切换到紧急值（响应更快，允许更大舵角/推力差）
    # - 不受正常的速率限制约束
    # - Thrust Allocator 切换到最大推力模式
    
    emergency_config = {
        rudder_max: RUDDER_MAX_PHYSICAL,    # 物理极限舵角
        thrust_max: F_MAX_AHEAD,            # 满功率
        rate_limit: NONE,                   # 不限速率
        all_actuators: ENABLED
    }
    
    RETURN emergency_config
```

---

## 11. 故障降级模式

```
FUNCTION determine_degraded_mode(health):
    
    IF NOT health.rudder_ok AND health.propulsion_ok:
        # 舵故障——只能用差动推力控制航向
        RETURN "DEGRADED_NO_RUDDER"
    
    IF NOT health.bow_thruster_ok AND NOT health.rudder_ok:
        # 舵和侧推都故障——只能差动
        RETURN "DEGRADED_DIFFERENTIAL_ONLY"
    
    IF health.single_engine_only:
        # 单机——无法差动，只能用舵
        RETURN "DEGRADED_SINGLE_ENGINE"
    
    RETURN "DEGRADED_UNKNOWN"
```

---

## 12. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Speed Monitor | 速度持续时间监控（含迟滞） | C++ |
| State Machine | 模式状态机管理 | C++ |
| Blend Calculator | 过渡期混合因子计算 | C++ |
| Actuator Sequencer | 执行机构启停时序管理 | C++ |
| Emergency Handler | 紧急/降级模式处理 | C++ |

---

## 13. 数值算例

### 进港减速过渡

```
t=0:   V=6.0 m/s, mode=TRANSIT
t=30s: V=4.5 m/s, mode=TRANSIT（未触及 V_T2M=3.0）
t=60s: V=3.5 m/s, mode=TRANSIT（仍未到 3.0）
t=70s: V=2.8 m/s, mode=TRANSITION
       → blend = (2.8 - 3.0)/(4.0 - 3.0) = -0.2 → clamp to 0 → MANEUVERING
       → 但需要持续 3 秒确认...
t=73s: V=2.5 m/s 持续 3 秒, mode=MANEUVERING 确认
       → 侧推器启动
       → 差动推力接管航向控制
       → 舵权重降至 0

出港加速：
t=200s: V=3.0 m/s, mode=MANEUVERING
t=210s: V=3.5 m/s, mode=MANEUVERING（迟滞——需要 > 4.0 才回 Transit）
t=230s: V=4.2 m/s, 持续 5 秒, mode=TRANSITION
        → blend = (4.2 - 3.0)/1.0 = 1.0 → 但检查 > V_M2T+0.5 = 4.5? → No
t=240s: V=5.0 m/s > 4.5, mode=TRANSIT
```

---

## 14. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| V_T2M（Transit→Maneuver） | 3.0 m/s | 低于此开始切换 |
| V_M2T（Maneuver→Transit） | 4.0 m/s | 高于此切回 |
| 迟滞带宽度 | 1.0 m/s | |
| Transit→Maneuver 持续要求 | 3 秒 | |
| Maneuver→Transit 持续要求 | 5 秒 | |
| 过渡时间 | 10 秒 | |
| 港内强制 Maneuvering 速度 | 5.0 m/s | |
| 舵效下限速度 | 2.0 m/s | |
| 侧推器有效上限速度 | 4.0 m/s | |

---

## 15. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Nav Filter → Mode Switcher | 输入 | u, r |
| ENC Reader → Mode Switcher | 输入 | zone_type |
| Risk Monitor → Mode Switcher | 输入 | 紧急指令 |
| System Monitor → Mode Switcher | 输入 | 执行机构健康 |
| Mode Switcher → Heading Ctrl | 输出 | 增益切换信号 |
| Mode Switcher → Speed Ctrl | 输出 | 模式状态 |
| Mode Switcher → Force Calc | 输出 | current_mode |
| Mode Switcher → Thrust Allocator | 输出 | 分配矩阵配置 |

---

## 16. 测试场景与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| MOD-001 | 从 8→2 m/s 减速 | Transit→Transition→Maneuvering 正确 |
| MOD-002 | 从 2→8 m/s 加速 | Maneuvering→Transition→Transit 正确 |
| MOD-003 | 速度在 3.0~4.0 m/s 波动 | 迟滞生效，不频繁切换 |
| MOD-004 | 港内强制 Maneuvering | zone=port_inner 时正确强制 |
| MOD-005 | 紧急模式进入/退出 | 全执行机构激活 |
| MOD-006 | 舵故障降级 | 切换到差动控制 |
| MOD-007 | 单机故障降级 | 切换到舵控制 |
| MOD-008 | 过渡期混合平滑性 | 控制力无阶跃跳变 |
| MOD-009 | 过渡期航向保持 | 切换过程中航向偏差 < 5° |
| 切换响应时间 | | < 100 ms（不含执行机构启动时间） |

---

## 17. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen 2021 | 控制分配与模式切换 |
| [2] | Sørensen 2011 | DP 系统模式管理 |

---

## v3.0 架构升级：操作模式扩展

### A. 扩展后的操作模式枚举

v3.0 将操作模式从 2 种（Transit / Maneuver）扩展到 5 种：

| 模式 | 触发条件 | 控制目标 | 说明 |
|------|---------|---------|------|
| TRANSIT | 正常航行 | 航线跟踪（ψ_cmd + u_cmd） | 原有——LOS 引导 + 速度控制 |
| MANEUVER | 转弯/避碰/低速 | 航向+速度 | 原有——增益调整 |
| **DP_STATION** | 到达目的地/等待 | **位置 + 航向保持** | **v3.0 新增**——定点保持 |
| **JOYSTICK** | 岸基远程操控 | **直接力/力矩指令** | **v3.0 新增**——摇杆操作 |
| **LOW_SPEED_TRACK** | 港内/狭水道 | **低速精确航迹跟踪** | **v3.0 新增**——低速模式 |

### B. DP Station Keeping 模式

```
FUNCTION dp_station_keeping(target_position, target_heading, nav_state, environment):
    
    # 位置误差
    pos_error_N = target_position.N - nav_state.N
    pos_error_E = target_position.E - nav_state.E
    
    # 转到船体坐标系
    pos_error_surge = pos_error_N × cos(nav_state.psi) + pos_error_E × sin(nav_state.psi)
    pos_error_sway = -pos_error_N × sin(nav_state.psi) + pos_error_E × cos(nav_state.psi)
    
    # 航向误差
    heading_error = normalize_pm180(target_heading - nav_state.heading)
    
    # PID 控制——位置和航向同时控制
    Fx = Kp_pos × pos_error_surge + Kd_pos × (-nav_state.u) + Ki_pos × integral_surge
    Fy = Kp_pos × pos_error_sway + Kd_pos × (-nav_state.v) + Ki_pos × integral_sway
    Mz = Kp_hdg × heading_error + Kd_hdg × (-nav_state.r) + Ki_hdg × integral_heading
    
    # 风前馈补偿
    Fx += wind_feedforward_x(environment.wind)
    Fy += wind_feedforward_y(environment.wind)
    Mz += wind_feedforward_mz(environment.wind)
    
    # 输出力/力矩需求给 Thrust Allocator
    RETURN ForceCommand(Fx, Fy, Mz)
```

### C. Joystick 模式

```
FUNCTION joystick_mode(joystick_input):
    
    # 岸基远程操控——摇杆输入直接映射为力/力矩
    # joystick_input: {surge: -1~1, sway: -1~1, yaw: -1~1}
    
    Fx = joystick_input.surge × MAX_SURGE_FORCE
    Fy = joystick_input.sway × MAX_SWAY_FORCE
    Mz = joystick_input.yaw × MAX_YAW_MOMENT
    
    # 摇杆死区
    IF abs(joystick_input.surge) < DEADZONE: Fx = 0
    IF abs(joystick_input.sway) < DEADZONE: Fy = 0
    IF abs(joystick_input.yaw) < DEADZONE: Mz = 0
    
    RETURN ForceCommand(Fx, Fy, Mz)

DEADZONE = 0.05    # 5% 死区
```

### D. 模式切换安全

```
# 模式切换时必须确保控制指令的连续性
# 例如从 TRANSIT 切到 DP_STATION：
# - TRANSIT 最后的速度可能是 8 m/s
# - DP_STATION 需要船静止
# - 切换时不能突然把速度指令从 8 变到 0
# - 应该先通过 MANEUVER 模式减速到 0，再切到 DP_STATION

ALLOWED_TRANSITIONS = {
    "TRANSIT": ["MANEUVER", "LOW_SPEED_TRACK"],
    "MANEUVER": ["TRANSIT", "DP_STATION", "JOYSTICK", "LOW_SPEED_TRACK"],
    "DP_STATION": ["MANEUVER", "JOYSTICK"],
    "JOYSTICK": ["MANEUVER", "DP_STATION"],
    "LOW_SPEED_TRACK": ["MANEUVER", "TRANSIT"]
}
```

---

## v3.1 架构升级：IJS 独立摇杆紧急模式（P2，V2 版本实现）

### A. 问题——JOYSTICK 模式依赖 ROS2

v3.0 定义的 JOYSTICK 模式运行在 ROS2 框架内——摇杆输入经 ROS2 话题传递给 Thrust Allocator。如果 ROS2 中间件或双冗余计算节点全部崩溃，JOYSTICK 模式也随之失效。此时船长只剩 Hardware Override 的纯物理操舵——从 AI 全自动直接跌落到纯人工，没有中间层缓冲。

Kongsberg 的 DP-2 设计要求配备 cJoy（Independent Joystick System）——一套完全独立于主控计算机的硬件摇杆，拥有自己的单片机处理器，通过模拟量电缆直接连接推进器控制器。

### B. 模式层级扩展（6 → 7 模式）

```
操作模式层级（v3.1，从高到低）：

P6: TRANSIT          全自动巡航（AI 管 L1~L5 全部）
P5: MANEUVER         全自动机动（AI 管 L3~L5，人管 L1~L2）
P4: DP_STATION       全自动定点保持（AI 管 L5 三轴 PID）
P3: JOYSTICK         ROS2 内的摇杆控制（人操控力/力矩需求 → AI Thrust Alloc 分配）
P2: AUTO_HEADING     ROS2 内的自动航向保持（人管速度，AI 管航向）
P1: IJS_EMERGENCY    ★ v3.1 新增 ★ 独立硬件摇杆（不经过 ROS2，不经过主控计算机）
P0: MANUAL_OVERRIDE  纯物理人工操舵/车钟（Hardware Override 继电器切断全部电子信号）

降级路径：
  正常运行 → P6/P5/P4
  单台计算机故障 → Redundancy Manager 自动切换 → 保持 P6/P5/P4
  双台计算机故障 / ROS2 崩溃 → ★ 降级到 P1 IJS_EMERGENCY ★
  IJS 也故障 → 降级到 P0 MANUAL_OVERRIDE
```

### C. IJS 硬件架构

```
┌────────────────────────────────┐
│  Independent Joystick System   │
│                                │
│  ┌──────────────────────────┐  │
│  │ 三轴摇杆（Surge/Sway/Yaw）│  │
│  │ 前后 = 前进/后退          │  │
│  │ 左右 = 横移               │  │
│  │ 旋转 = 艏摇              │  │
│  └────────────┬─────────────┘  │
│               │ 模拟量          │
│  ┌────────────┴─────────────┐  │
│  │  独立 MCU（STM32 级别）   │  │
│  │  简化推力分配算法          │  │
│  │  输入：3 轴力/力矩需求    │  │
│  │  输出：各推进器指令        │  │
│  │  不依赖 ROS2/DDS/以太网   │  │
│  └────────────┬─────────────┘  │
│               │ CAN Bus / 模拟量│
└───────────────┼────────────────┘
                │
                ▼
         推进器控制器（直接连接）
```

### D. IJS 简化推力分配

```
FUNCTION ijs_thrust_allocation(Fx_demand, Fy_demand, Mz_demand):
    
    # IJS 的推力分配是极简化的——不做 QP 优化
    # 只做基本的力/力矩到推进器指令的线性映射
    
    # 双喷水推进器配置（45m crew boat）：
    #   左推进器位置：(-d, -y_left)
    #   右推进器位置：(-d, +y_right)
    
    # 纵向力：两台推进器均分
    T_left_x = Fx_demand / 2
    T_right_x = Fx_demand / 2
    
    # 横向力：两台推进器反向偏转
    deflection = atan2(Fy_demand, abs(Fx_demand) + 0.1)    # 防零除
    
    # 艏摇力矩：两台推进器差动
    T_diff = Mz_demand / (2 × y_arm)
    T_left_x -= T_diff
    T_right_x += T_diff
    
    # 限幅
    T_left_x = clamp(T_left_x, T_MIN, T_MAX)
    T_right_x = clamp(T_right_x, T_MIN, T_MAX)
    deflection = clamp(deflection, -MAX_DEFLECTION, MAX_DEFLECTION)
    
    RETURN {
        left_thrust: T_left_x,
        left_deflection: deflection,
        right_thrust: T_right_x,
        right_deflection: -deflection    # 反向偏转产生横向力
    }
```

### E. IJS 激活条件

```
# IJS 不需要手动激活——当检测到主控计算机不可用时自动激活

FUNCTION ijs_activation_monitor():
    # IJS MCU 通过 CAN Bus 监控主控计算机的心跳
    # 心跳来自 Redundancy Manager
    
    IF no_heartbeat_for(3.0 seconds):    # 3 秒无心跳
        ijs_mode = "ACTIVE"
        # IJS 接管推进器控制
        # 驾驶台指示灯：IJS 模式（蓝色闪烁）
        buzzer("IJS ACTIVE: use joystick for emergency control")
    
    IF heartbeat_restored:
        # 主控恢复——但不自动回切
        # 需要船长确认才能从 IJS 回到 AI 模式
        ijs_mode = "STANDBY_READY"
```

### F. 实现计划

| 阶段 | 内容 | 版本 |
|------|------|------|
| V1 | P3 JOYSTICK（ROS2 内摇杆）+ P0 MANUAL_OVERRIDE | v3.0 已设计 |
| V2 | P1 IJS_EMERGENCY（独立硬件摇杆，不依赖 ROS2） | v3.1 定义 |
| V3 | IJS 增强——增加简化的航向保持和定点保持功能 | 未来版本 |

---

## v3.1 补充升级：离靠港操作支持——DP_APPROACH 模式 + 系泊状态接口

### A. 问题——当前模式无法覆盖靠泊进近

DP_STATION 是"保持在一个点不动"（调节器），但靠泊需要的是"以 0.2 m/s 的速度沿路径缓慢接近泊位，到位后保持"（跟踪器）。TRANSIT 和 MANEUVER 模式设计为航行速度（> 5 节），不适用于 < 1 节的极低速精确机动。当前模式体系在"港口入口到泊位"这段约 500m~1km 的关键距离上存在空白。

### B. 新增模式：DP_APPROACH（靠泊进近）

```
DP_APPROACH 模式定义：

  控制目标：以受控速度沿指定路径接近目标位置，到达后自动切换为 DP_STATION
  
  输入：
    - berth_target: 目标泊位位置和朝向（泊位坐标系）
    - approach_path: 进近路径（由 L2 Berth Planner 生成）
    - approach_speed_profile: 进近速度剖面（逐段递减）
    - max_contact_speed: 最大接触速度（默认 0.1 m/s）
  
  控制模式：
    - 纵向（Surge）：速度跟踪——按速度剖面减速，最后阶段速度 < 0.1 m/s
    - 横向（Sway）：位置跟踪——沿进近路径的横向偏差 < 0.3m
    - 艏摇（Yaw）：航向跟踪——保持目标靠泊航向 ±1°
    - 三轴同时控制，不是分先后
  
  激活条件：
    - 当前速度 < 3 节
    - 目标泊位已定义
    - 近场定位源（SpotTrack/RADius/LiDAR）可用
    - Navigation Filter 位置不确定性 < 1.0m
  
  退出条件：
    - 到达泊位（距离 < 0.5m）→ 自动切换 DP_STATION
    - 船长按 Override → 切换 MANUAL
    - 安全违规（超限/碰撞预警）→ 切换 DP_STATION（原地保持）
```

### C. DP_APPROACH 控制器结构

```
FUNCTION dp_approach_control(current_state, berth_target, approach_path, dt):
    
    # 坐标变换：从 WGS84/ENU 转为泊位相对坐标
    berth_relative = transform_to_berth_frame(current_state, berth_target)
    # berth_relative.d_along = 沿泊位面方向的距离（正=需要前进）
    # berth_relative.d_perp  = 垂直于泊位面的距离（正=需要接近）
    # berth_relative.heading_error = 与目标航向的偏差
    
    # ---- 纵向速度控制（沿进近路径方向）----
    distance_to_berth = berth_relative.d_perp    # 距离泊位面
    
    # 速度剖面：根据距离递减
    IF distance_to_berth > 50:
        target_speed = 1.5    # m/s（~3 节）——进近段
    ELIF distance_to_berth > 20:
        target_speed = 0.8    # m/s（~1.5 节）——减速段
    ELIF distance_to_berth > 5:
        target_speed = 0.3    # m/s（~0.6 节）——精确段
    ELIF distance_to_berth > 1:
        target_speed = 0.1    # m/s（~0.2 节）——最终接近
    ELSE:
        target_speed = 0.05   # m/s——蠕动接触
    
    # 安全限制：接触速度硬限
    IF distance_to_berth < 2.0:
        target_speed = min(target_speed, max_contact_speed)
    
    surge_error = target_speed - current_approach_speed
    Fx_cmd = surge_pid.compute(surge_error, dt)
    
    # ---- 横向位置控制（沿泊位面方向对齐）----
    sway_error = berth_relative.d_along    # 需要横移的量
    Fy_cmd = sway_pid.compute(sway_error, dt)
    
    # ---- 航向控制（对齐泊位朝向）----
    yaw_error = berth_relative.heading_error
    Mz_cmd = yaw_pid.compute(yaw_error, dt)
    
    # ---- 到达检测 ----
    IF distance_to_berth < 0.5 AND abs(sway_error) < 0.3 AND abs(yaw_error) < 1.0:
        transition_to(DP_STATION)
        log_event("berth_reached", {distance: distance_to_berth})
    
    RETURN Fx_cmd, Fy_cmd, Mz_cmd
```

### D. DP_APPROACH 的三阶段

```
阶段 1：进近段（50m~200m）
  速度 ~1.5 m/s，主要用 GNSS + 近场传感器混合定位
  横向修正幅度允许较大（±3m 走廊）
  此阶段与 LOW_SPEED_TRACK 相似但增加了横向控制

阶段 2：精确段（5m~50m）
  速度 0.3~0.8 m/s，主要用近场定位（SpotTrack/RADius）
  横向走廊收窄到 ±1m
  航向必须对齐泊位朝向（误差 < 2°）
  岸壁距离持续监测激活

阶段 3：接触段（0~5m）
  速度 0.05~0.1 m/s
  近场定位精度要求 < 0.1m
  LiDAR 持续测量到岸壁的精确距离
  速度硬限：接触速度 < 0.1 m/s（约 0.2 节）
  到达泊位面 → 自动切换 DP_STATION → 等待系缆完成信号
```

### E. 离泊序列

```
FUNCTION undocking_sequence():
    
    # 前提：收到"解缆完成"信号
    WAIT_FOR mooring_status == "ALL_LINES_CLEAR"
    
    # 阶段 1：缓慢横移离开泊位
    activate_mode(DP_APPROACH_REVERSE)
    target = current_position + 20m × berth_normal_direction    # 垂直于泊位面移出 20m
    approach_speed = 0.2    # m/s
    
    # 阶段 2：到达安全距离后转向
    WAIT_FOR distance_from_berth > 20m
    
    # 阶段 3：切换为正常航行模式
    transition_to(MANEUVER)    # 港内机动模式
    # 后续由 L2 的航路点引导出港
```

### F. 系泊状态接口（G9）

```
# 甲板船员通过物理按钮或对讲机通知驾驶台系/解缆状态
# MASS_ADAS 接收此状态用于模式切换决策

MooringStatus 消息：
    string status          # "ALL_FAST" / "SINGLING_UP" / "ALL_LINES_CLEAR" / "STANDBY"
    bool bow_line          # 船首缆完成
    bool stern_line        # 船尾缆完成  
    bool spring_fore       # 前倒缆完成
    bool spring_aft        # 后倒缆完成
    Time timestamp

话题：/mooring/status
频率：手动触发（非周期性）

模式切换逻辑：
  靠泊到位 + ALL_FAST → 从 DP_STATION 切换到 IDLE（停机）
  IDLE + ALL_LINES_CLEAR → 允许启动离泊序列
  任何模式 + 非 ALL_FAST → 不允许停机（缆绳未系好，需要保持 DP）
```

### G. 完整的模式层级更新（8 模式）

```
P7: TRANSIT          开阔海域全自动巡航
P6: MANEUVER         受限水域全自动机动（港内限速/窄航道）
P5: DP_STATION       定点保持（三轴 PID 调节器）
P4: DP_APPROACH      靠泊进近（三轴位置跟踪器）  ★ 新增 ★
P3: JOYSTICK         ROS2 摇杆控制
P2: AUTO_HEADING     自动航向保持
P1: IJS_EMERGENCY    独立硬件摇杆
P0: MANUAL_OVERRIDE  纯物理人工

典型靠泊操作的模式切换序列：
  TRANSIT(航行中) → MANEUVER(进港) → DP_APPROACH(靠泊) → DP_STATION(到位) → IDLE(系缆)
  
典型离泊操作的模式切换序列：
  IDLE(解缆) → DP_STATION(保持) → DP_APPROACH_REVERSE(横移离泊) → MANEUVER(出港) → TRANSIT
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v3.0 升级：DP_STATION/JOYSTICK/LOW_SPEED_TRACK |
| 0.3 | 2026-04-26 | 架构组 | v3.1 升级：IJS_EMERGENCY 独立摇杆 |
| 0.4 | 2026-04-26 | 架构组 | v3.1 补充：增加 DP_APPROACH 靠泊进近模式（三阶段进近控制 + 接触速度硬限 + 泊位相对坐标）；增加系泊状态接口（MooringStatus）；增加离泊序列；模式层级从 7 扩展到 8 |

---

**文档结束**
