# MASS_ADAS Actuator Limiter 执行机构限制器技术设计文档

**文档编号：** SANGO-ADAS-ACT-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 计算流程总览
5. 螺旋桨转速限制
6. 舵角限制
7. 侧推器限制
8. 速率限制（Rate Limiter）
9. 饱和检测与反馈
10. 倒车效率折减
11. 执行机构动态模型
12. 硬件保护——最后一道防线
13. 内部子模块分解
14. 数值算例
15. 参数总览表
16. 与其他模块的协作关系
17. 测试策略与验收标准
18. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Actuator Limiter（执行机构限制器）是 Layer 5 控制链的最后一个模块——它是 Thrust Allocator 的输出和物理硬件之间的"安全阀"。它接收 Thrust Allocator 输出的执行机构指令（螺旋桨转速、舵角、侧推力），执行最终的安全限制检查，确保所有指令在硬件物理能力范围内，然后输出最终的硬件控制信号。

Actuator Limiter 的存在是"防御性编程"的体现——即使上游所有模块都正确工作（Thrust Allocator 已经做了限幅），这个模块仍然独立地检查每个指令是否合理。如果上游软件出现 bug 发出了不合理的指令（比如转速 100 rps——远超物理极限），Actuator Limiter 会拦截并限制到安全范围。

### 1.2 核心职责

- **幅值限制：** 确保每个执行机构的指令不超过物理极限。
- **速率限制：** 确保指令的变化速率不超过执行机构的响应能力（舵机转速、主机加速率）。
- **饱和反馈：** 当指令被限幅时，通知上游的控制器（用于积分抗饱和）。
- **倒车效率建模：** 螺旋桨在倒车时推力特性不同于前进——需要修正。
- **硬件保护：** 执行最终的安全检查，防止任何可能损坏硬件的指令。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 频率 | 说明 |
|-------|------|------|------|
| n_port_cmd | Thrust Allocator | 50 Hz | 左螺旋桨转速指令（rps） |
| n_stbd_cmd | Thrust Allocator | 50 Hz | 右螺旋桨转速指令（rps） |
| δ_cmd | Thrust Allocator | 50 Hz | 舵角指令（弧度） |
| F_bow_cmd | Thrust Allocator | 50 Hz | 侧推器推力指令（N） |
| 执行机构当前状态 | 硬件传感器 | 50 Hz | 当前转速、舵角、温度等 |

### 2.2 输出

```
ActuatorCommand:
    Header header
    
    # ---- 最终硬件指令 ----
    float64 prop_port_rpm_final     # 左螺旋桨最终转速（rps）
    float64 prop_stbd_rpm_final     # 右螺旋桨最终转速（rps）
    float64 rudder_angle_final      # 舵角最终指令（弧度）
    float64 bow_thruster_final      # 侧推器最终推力（N）
    
    # ---- 饱和反馈（给上游控制器用于抗饱和）----
    bool prop_port_saturated        # 左螺旋桨是否饱和
    bool prop_stbd_saturated        # 右螺旋桨是否饱和
    bool rudder_saturated           # 舵是否饱和
    bool bow_thruster_saturated     # 侧推器是否饱和
    bool any_rate_limited           # 是否有任何执行机构被速率限制
    
    # ---- 监控 ----
    float64 prop_port_utilization   # 左螺旋桨利用率 [0,1]
    float64 prop_stbd_utilization   # 右螺旋桨利用率
    float64 rudder_utilization      # 舵利用率
    float64 total_power_kw          # 总功率消耗估算
```

---

## 3. 核心参数数据库

### 3.1 螺旋桨限制

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 最大前进转速 | n_max_ahead | 待标定 rps | 主机额定转速 |
| 最大倒车转速 | n_max_astern | 0.8 × n_max | 倒车通常限制更低 |
| 最大转速变化率 | dn_max | n_max / 5 (rps/s) | 5 秒内从零到满转 |
| 最低稳定转速 | n_min_stable | 待标定 rps | 低于此主机可能熄火 |
| 紧急停机转速变化率 | dn_emergency | n_max / 2 (rps/s) | 紧急模式加速切换 |

### 3.2 舵限制

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 最大舵角 | δ_max | 35° (0.611 rad) | 机械极限 |
| 最大舵角变化率 | dδ_max | 5°/s (0.087 rad/s) | 舵机转速 |
| 紧急舵角变化率 | dδ_emergency | 7°/s | 紧急模式 |
| 舵角死区 | δ_deadband | 0.5° | 硬件精度 |

### 3.3 侧推器限制

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 最大推力 | F_bow_max | 待标定 N | |
| 最大推力变化率 | dF_bow_max | F_bow_max / 3 (N/s) | |
| 启动延迟 | t_bow_startup | 2.0 秒 | 从关闭到生效 |
| 连续运行最大时间 | t_bow_max_continuous | 300 秒 | 防过热 |
| 冷却等待时间 | t_bow_cooldown | 60 秒 | 过热后的冷却时间 |

---

## 4. 计算流程总览

```
输入：Thrust Allocator 指令
          │
          ▼
    ┌──────────────────────────────────┐
    │ 1. 幅值限制                      │
    │ clamp(n, -n_max_astern, n_max)   │
    │ clamp(δ, -δ_max, δ_max)         │
    │ clamp(F_bow, -F_bow_max, F_bow_max)│
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 2. 速率限制                      │
    │ |Δn/Δt| ≤ dn_max               │
    │ |Δδ/Δt| ≤ dδ_max               │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 3. 倒车效率折减                  │
    │ 倒车推力 = 前进推力 × k_astern   │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 4. 硬件保护检查                  │
    │ 温度/振动/故障码                 │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 5. 饱和反馈生成                  │
    │ 通知上游控制器                   │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：最终硬件指令 + 饱和反馈
```

---

## 5. 螺旋桨转速限制

```
FUNCTION limit_propeller(n_cmd, n_prev, dt, params, mode):
    
    # ---- 幅值限制 ----
    IF n_cmd > 0:
        n_limited = min(n_cmd, params.n_max_ahead)
    ELSE:
        n_limited = max(n_cmd, -params.n_max_astern)
    
    # ---- 最低稳定转速处理 ----
    IF abs(n_limited) > 0 AND abs(n_limited) < params.n_min_stable:
        # 在不稳定区——要么增大到最低稳定转速，要么设为零
        IF abs(n_cmd) >= params.n_min_stable × 0.8:
            n_limited = sign(n_limited) × params.n_min_stable
        ELSE:
            n_limited = 0    # 太低了，关闭
    
    # ---- 速率限制 ----
    dn = n_limited - n_prev
    IF mode == "EMERGENCY":
        dn_max = params.dn_emergency × dt
    ELSE:
        dn_max = params.dn_max × dt
    
    IF abs(dn) > dn_max:
        n_limited = n_prev + sign(dn) × dn_max
    
    # ---- 饱和检测 ----
    saturated = (abs(n_cmd) > abs(n_limited) + 0.1)
    utilization = abs(n_limited) / params.n_max_ahead
    
    RETURN {n_final: n_limited, saturated, utilization}
```

---

## 6. 舵角限制

```
FUNCTION limit_rudder(δ_cmd, δ_prev, dt, params, mode):
    
    # ---- 幅值限制 ----
    δ_limited = clamp(δ_cmd, -params.δ_max, params.δ_max)
    
    # ---- 速率限制 ----
    dδ = δ_limited - δ_prev
    IF mode == "EMERGENCY":
        dδ_max = params.dδ_emergency × dt
    ELSE:
        dδ_max = params.dδ_max × dt
    
    IF abs(dδ) > dδ_max:
        δ_limited = δ_prev + sign(dδ) × dδ_max
    
    # ---- 死区处理 ----
    IF abs(δ_limited) < params.δ_deadband:
        δ_limited = 0    # 极小舵角不执行
    
    # ---- 饱和检测 ----
    saturated = (abs(δ_cmd) > params.δ_max × 0.95)
    utilization = abs(δ_limited) / params.δ_max
    
    RETURN {δ_final: δ_limited, saturated, utilization}
```

---

## 7. 侧推器限制

```
FUNCTION limit_bow_thruster(F_cmd, F_prev, dt, params, bow_state):
    
    # ---- 幅值限制 ----
    F_limited = clamp(F_cmd, -params.F_bow_max, params.F_bow_max)
    
    # ---- 启动延迟 ----
    IF bow_state.status == "OFF" AND abs(F_cmd) > 10:
        # 侧推器需要启动
        bow_state.status = "STARTING"
        bow_state.startup_timer = params.t_bow_startup
        F_limited = 0    # 启动期间推力为零
    
    IF bow_state.status == "STARTING":
        bow_state.startup_timer -= dt
        IF bow_state.startup_timer ≤ 0:
            bow_state.status = "RUNNING"
        ELSE:
            F_limited = 0    # 仍在启动中
    
    # ---- 过热保护 ----
    IF bow_state.status == "RUNNING":
        bow_state.run_time += dt
        IF bow_state.run_time > params.t_bow_max_continuous:
            # 运行时间超限——强制关闭冷却
            bow_state.status = "COOLING"
            bow_state.cooldown_timer = params.t_bow_cooldown
            F_limited = 0
            log_event("bow_thruster_thermal_limit", {run_time: bow_state.run_time})
    
    IF bow_state.status == "COOLING":
        F_limited = 0
        bow_state.cooldown_timer -= dt
        IF bow_state.cooldown_timer ≤ 0:
            bow_state.status = "OFF"
            bow_state.run_time = 0
    
    # ---- 速率限制 ----
    dF = F_limited - F_prev
    dF_max = params.dF_bow_max × dt
    IF abs(dF) > dF_max:
        F_limited = F_prev + sign(dF) × dF_max
    
    # ---- 关闭检测 ----
    IF abs(F_cmd) < 10 AND bow_state.status == "RUNNING":
        bow_state.status = "OFF"
        bow_state.run_time = 0
    
    saturated = (abs(F_cmd) > params.F_bow_max × 0.95)
    
    RETURN {F_final: F_limited, saturated, bow_state}
```

---

## 8. 速率限制（Rate Limiter）

速率限制是 Actuator Limiter 最重要的功能之一。没有速率限制，控制器的输出可能瞬间从一个极端跳到另一个极端——这在数学上是合法的，但在物理上是不可能的（舵机不能瞬间从 -35° 跳到 +35°）。

```
FUNCTION rate_limiter(cmd, prev, dt, rate_max):
    
    delta = cmd - prev
    
    IF abs(delta) > rate_max × dt:
        RETURN prev + sign(delta) × rate_max × dt
    ELSE:
        RETURN cmd
```

**速率限制值的选择：**

| 执行机构 | 物理极限速率 | 控制用速率限制 | 理由 |
|---------|------------|-------------|------|
| 舵 | 7°/s（液压舵机） | 5°/s（正常）/ 7°/s（紧急） | 留 30% 余量 |
| 螺旋桨 | n_max/3 rps/s | n_max/5 rps/s（正常） | 保护传动系统 |
| 侧推器 | 接近瞬时 | F_max/3 N/s | 电机保护 |

---

## 9. 饱和检测与反馈

```
FUNCTION generate_saturation_feedback(prop_port, prop_stbd, rudder, bow):
    
    feedback = ActuatorSaturationFeedback()
    
    feedback.prop_port_saturated = prop_port.saturated
    feedback.prop_stbd_saturated = prop_stbd.saturated
    feedback.rudder_saturated = rudder.saturated
    feedback.bow_thruster_saturated = bow.saturated
    
    feedback.any_saturated = (
        prop_port.saturated OR prop_stbd.saturated OR
        rudder.saturated OR bow.saturated
    )
    
    # 通知上游控制器——用于积分抗饱和
    # Heading Controller 和 Speed Controller 需要知道自己的指令是否被实际执行
    
    RETURN feedback
```

饱和反馈通过 ROS2 话题 `/control/actuator_saturation` 发布，被 Heading Controller 和 Speed Controller 的抗饱和逻辑消费。

---

## 10. 倒车效率折减

螺旋桨在倒车时的推力效率低于前进时。同样的转速下，倒车推力约为前进推力的 60~70%。

```
FUNCTION apply_reverse_efficiency(n, K_N, k_astern):
    
    IF n < 0:
        # 倒车——推力折减
        F_actual = K_N × n × abs(n) × k_astern
    ELSE:
        F_actual = K_N × n × abs(n)
    
    RETURN F_actual

k_ASTERN = 0.65    # 倒车效率系数（65%）
```

**在 Actuator Limiter 中的处理：** 当 Thrust Allocator 请求倒车推力时，Actuator Limiter 需要考虑倒车效率折减——如果 Allocator 期望产生 1000N 的倒车推力，实际需要的转速比前进时更高（因为效率只有 65%）。Actuator Limiter 可以自动补偿这个差异，或者通知 Thrust Allocator 重新计算。

```
FUNCTION compensate_reverse_efficiency(n_cmd, K_N, k_astern, F_desired):
    
    IF n_cmd < 0:
        # 倒车——检查实际推力是否满足需求
        F_actual = K_N × n_cmd × abs(n_cmd) × k_astern
        
        IF abs(F_actual) < abs(F_desired) × 0.9:
            # 推力不足——增大转速补偿
            n_compensated = -sqrt(abs(F_desired) / (K_N × k_astern))
            n_compensated = max(n_compensated, -params.n_max_astern)
            RETURN n_compensated
    
    RETURN n_cmd
```

---

## 11. 执行机构动态模型

Actuator Limiter 可以包含一个简化的执行机构动态模型，用于预测指令到实际响应之间的延迟：

```
# 舵机动态（一阶延迟）
δ_actual(t) = δ_actual(t-1) + (δ_cmd - δ_actual(t-1)) × (1 - exp(-dt/τ_rudder))
τ_rudder = 1.0    # 秒（舵机时间常数）

# 螺旋桨动态（一阶延迟 + 积分）
n_actual(t) = n_actual(t-1) + (n_cmd - n_actual(t-1)) × (1 - exp(-dt/τ_prop))
τ_prop = 3.0      # 秒（主机响应时间常数）
```

这些模型不用于控制（控制器已经设计了足够的鲁棒性），但用于**预测实际执行效果**——如果控制器假设指令被瞬间执行，但实际有 3 秒延迟，控制器可能会过度补偿。Actuator Limiter 可以输出"预测的实际状态"供控制器参考。

---

## 12. 硬件保护——最后一道防线

```
FUNCTION hardware_protection_check(commands, hardware_status):
    
    alerts = []
    
    # 检查 1：主机温度过高
    IF hardware_status.engine_temp > ENGINE_TEMP_MAX:
        commands.n_port *= 0.5
        commands.n_stbd *= 0.5
        alerts.append("ENGINE_THERMAL_LIMIT: 转速降至 50%")
    
    # 检查 2：舵机液压压力异常
    IF hardware_status.rudder_hydraulic_pressure < PRESSURE_MIN:
        commands.δ = 0    # 舵机故障——锁定中位
        alerts.append("RUDDER_HYDRAULIC_FAIL: 舵锁定中位")
    
    # 检查 3：指令合理性——防止软件 bug
    IF abs(commands.n_port) > params.n_max_ahead × 1.5:
        commands.n_port = 0    # 明显异常——紧急归零
        alerts.append("COMMAND_SANITY_FAIL: 左螺旋桨指令异常，归零")
    
    IF abs(commands.δ) > π:    # 舵角 > 180°——显然是 bug
        commands.δ = 0
        alerts.append("COMMAND_SANITY_FAIL: 舵角指令异常，归零")
    
    # 检查 4：通信超时——如果超过 1 秒没收到新指令
    IF time_since_last_command > COMMAND_TIMEOUT:
        # 执行安全默认动作：保持当前指令但逐步减小
        commands.n_port *= 0.95    # 每秒减速 5%
        commands.n_stbd *= 0.95
        commands.δ *= 0.9          # 舵角逐步回中
        alerts.append("COMMAND_TIMEOUT: 指令超时，逐步减速")
    
    RETURN {commands, alerts}

ENGINE_TEMP_MAX = 待标定（°C）
PRESSURE_MIN = 待标定（bar）
COMMAND_TIMEOUT = 1.0    # 秒
```

---

## 13. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Amplitude Limiter | 幅值限制（所有执行机构） | C++ |
| Rate Limiter | 速率限制（所有执行机构） | C++ |
| Reverse Compensator | 倒车效率折减和补偿 | C++ |
| Bow Thruster Manager | 侧推器启停/过热保护 | C++ |
| Saturation Detector | 饱和检测与反馈生成 | C++ |
| Hardware Protector | 最终硬件安全检查 | C++ |
| Dynamics Predictor | 执行机构动态预测（可选） | C++ |

---

## 14. 数值算例

### 14.1 正常巡航

```
输入：n_port=5.66 rps, n_stbd=5.66 rps, δ=0.002 rad, F_bow=0
n_max=10 rps, δ_max=0.611 rad

幅值检查：全部在范围内
速率检查：变化 < 限制
饱和：无

输出：原样输出
利用率：n=56.6%, δ=0.3%, bow=0%
```

### 14.2 紧急转弯——舵速率限制

```
输入：δ_cmd = 0.611 rad (35°), δ_prev = 0 rad, dt = 0.02s
dδ_max = 5°/s = 0.087 rad/s

允许变化：0.087 × 0.02 = 0.00174 rad
δ_final = 0 + 0.00174 = 0.00174 rad (0.1°)

→ 第一个周期只能走 0.1°
→ 到达 35° 需要 35/5 = 7 秒
→ 这就是舵机的物理限制——控制器必须考虑这个延迟
```

### 14.3 侧推器过热保护

```
t=0:    F_bow_cmd=500N, bow_status=OFF → 启动（2s 延迟）
t=2s:   bow_status=RUNNING, F_bow_output=500N
t=300s: run_time=298s → 接近 300s 限制
t=302s: run_time=300s → 过热保护触发！
        F_bow_output=0, bow_status=COOLING, cooldown=60s
t=362s: cooldown 完成, bow_status=OFF, 可以重新启动
```

---

## 15. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **螺旋桨** | n_max_ahead | 待标定 rps | |
| | n_max_astern | 0.8 × n_max | |
| | dn_max（正常） | n_max/5 rps/s | |
| | dn_max（紧急） | n_max/2 rps/s | |
| | n_min_stable | 待标定 rps | |
| | k_astern（倒车效率） | 0.65 | |
| **舵** | δ_max | 35° (0.611 rad) | |
| | dδ_max（正常） | 5°/s | |
| | dδ_max（紧急） | 7°/s | |
| | δ_deadband | 0.5° | |
| **侧推器** | F_bow_max | 待标定 N | |
| | dF_bow_max | F_max/3 N/s | |
| | t_startup | 2.0 s | |
| | t_max_continuous | 300 s | |
| | t_cooldown | 60 s | |
| **保护** | 指令超时 | 1.0 s | |
| | 异常指令倍率 | 1.5 × max | 超过视为 bug |

---

## 16. 与其他模块的协作

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Thrust Allocator → Actuator Limiter | 输入 | [n_p, n_s, δ, F_bow] 指令 |
| Actuator Limiter → 硬件 | 输出 | 最终控制信号 |
| Actuator Limiter → Heading Ctrl | 反馈 | 舵饱和状态（积分抗饱和） |
| Actuator Limiter → Speed Ctrl | 反馈 | 螺旋桨饱和状态 |
| Actuator Limiter → Force Calc | 反馈 | 总饱和状态 |
| System Monitor → Actuator Limiter | 输入 | 硬件温度/压力/故障码 |
| Actuator Limiter → System Monitor | 输出 | 保护告警 |

---

## 17. 测试场景与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| ACT-001 | 正常指令——全在范围内 | 原样输出 |
| ACT-002 | 螺旋桨转速超限 | 限幅到 n_max |
| ACT-003 | 舵角超限 | 限幅到 δ_max |
| ACT-004 | 舵角速率超限 | 速率限制生效 |
| ACT-005 | 螺旋桨速率超限 | 速率限制生效 |
| ACT-006 | 侧推器启动延迟 | 2 秒延迟正确 |
| ACT-007 | 侧推器过热保护 | 300 秒后强制关闭 |
| ACT-008 | 倒车效率折减 | 推力输出 = 65% |
| ACT-009 | 指令超时 | 逐步减速 |
| ACT-010 | 异常指令（bug） | 紧急归零 |
| ACT-011 | 饱和反馈正确 | 控制器收到饱和信号 |
| ACT-012 | 紧急模式速率放宽 | 紧急速率限制生效 |
| 计算延迟 | | < 0.1 ms |

---

## 18. 参考文献

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen 2021 | 执行机构建模 |
| [2] | Sørensen 2011 | 推进系统保护 |
| [3] | IEC 61162 | 船舶控制系统通信标准 |

---

## v2.0 架构升级：提升为 Actuator Envelope Checker 角色

### A. Doer-Checker 定位

在 v2.0 防御性架构中，Actuator Limiter 的限幅和饱和保护功能被提升为 **Actuator Envelope Checker**——Deterministic Checker 的 L5 层实例，也是 Checker 链的最后一道关卡。

**变更前（v1.0）：** Actuator Limiter 是 L5 的输出处理模块——对 Thrust Allocator 输出的执行指令做限幅和速率限制后直接发送给执行机构。

**变更后（v2.0）：** Actuator Envelope Checker 独立校验所有发往执行机构的指令是否在物理安全包络之内。即使上游（Heading Controller、Speed Controller、Thrust Allocator）因软件 bug 或 AI 幻觉输出了非法指令（如舵角 80°、转速 200%、推力方向反转），Actuator Envelope Checker 会拦截并钳位到安全范围。

### B. 校验规则集

```
FUNCTION actuator_envelope_check(commands):
    
    violations = []
    clamped = copy(commands)
    
    # ---- 舵角包络 ----
    IF abs(commands.rudder_deg) > RUDDER_MAX:
        violations.append({rule: "RUDDER_MAX", value: commands.rudder_deg, limit: RUDDER_MAX})
        clamped.rudder_deg = clamp(commands.rudder_deg, -RUDDER_MAX, RUDDER_MAX)
    
    # ---- 舵角速率包络 ----
    rudder_rate = (commands.rudder_deg - prev_rudder_deg) / dt
    IF abs(rudder_rate) > RUDDER_RATE_MAX:
        violations.append({rule: "RUDDER_RATE", value: rudder_rate, limit: RUDDER_RATE_MAX})
        clamped.rudder_deg = prev_rudder_deg + sign(rudder_rate) × RUDDER_RATE_MAX × dt
    
    # ---- 主机转速包络 ----
    IF commands.rpm > RPM_MAX:
        violations.append({rule: "RPM_MAX", value: commands.rpm, limit: RPM_MAX})
        clamped.rpm = RPM_MAX
    IF commands.rpm < RPM_MIN_REVERSE:
        violations.append({rule: "RPM_MIN_REVERSE", value: commands.rpm, limit: RPM_MIN_REVERSE})
        clamped.rpm = RPM_MIN_REVERSE
    
    # ---- 转速变化率包络（防止急加减速）----
    rpm_rate = (commands.rpm - prev_rpm) / dt
    IF abs(rpm_rate) > RPM_RATE_MAX:
        violations.append({rule: "RPM_RATE", value: rpm_rate, limit: RPM_RATE_MAX})
        clamped.rpm = prev_rpm + sign(rpm_rate) × RPM_RATE_MAX × dt
    
    # ---- 侧推力包络 ----
    IF abs(commands.bow_thrust) > BOW_THRUST_MAX:
        violations.append({rule: "BOW_THRUST_MAX", value: commands.bow_thrust, limit: BOW_THRUST_MAX})
        clamped.bow_thrust = clamp(commands.bow_thrust, -BOW_THRUST_MAX, BOW_THRUST_MAX)
    
    # ---- 物理一致性校验（防止矛盾指令）----
    # 例如：同时全速前进 + 满舵 + 倒车 → 物理上矛盾
    IF commands.rpm > RPM_MAX × 0.8 AND abs(commands.rudder_deg) > RUDDER_MAX × 0.9:
        violations.append({rule: "COMBINED_STRESS", detail: "高转速+满舵同时出现，执行机构应力过大"})
        clamped.rpm = commands.rpm × 0.8    # 降低转速以保护
    
    # ---- ASDR 记录 ----
    IF len(violations) > 0:
        asdr_publish("actuator_envelope_violation", {
            violations: violations,
            original: commands,
            clamped: clamped
        })
    
    RETURN {
        approved: len(violations) == 0,
        violations: violations,
        safe_commands: clamped    # 即使有违规，也输出钳位后的安全值（而非拒绝）
    }
```

### C. Actuator Envelope Checker 的特殊性

与其他层的 Checker 不同，L5 Actuator Envelope Checker **不会完全否决指令**——而是将超出包络的指令**钳位到安全范围**。原因是：

1. **L5 是最底层**——如果 L5 否决指令，执行机构就没有任何输入，船处于失控状态。
2. **钳位比否决更安全**——把舵角从 80° 钳位到 35° 比直接拒绝舵角指令更好。
3. **上游 Checker 已经做了决策级校验**——到达 L5 的指令理论上已经过 L2/L3/L4 Checker 校验，不应出现方向性错误，只可能出现幅值超限。

### D. 与硬线仲裁器的关系

Actuator Envelope Checker（软件）和 Hardware Override Arbiter（硬件）是两层独立的保护：

```
L5 Thrust Allocator 输出
    ↓
Actuator Envelope Checker（软件层——钳位到安全范围）
    ↓
Hardware Override Arbiter（硬件层——人工接管时物理切断）
    ↓
执行机构（舵机、主机、侧推器）
```

即使 Actuator Envelope Checker 的软件有 bug（理论上不应该但工程上有可能），硬件仲裁器仍然是最后的物理屏障。

---

## v3.0 架构升级：Power Management 功率约束

### A. 功率限制接口

Actuator Envelope Checker 新增一个功率限制检查——确保所有执行机构指令的总功率不超过当前可用的电力容量。

```
FUNCTION check_power_envelope(commands, power_status):
    
    # 计算各执行机构的功率需求
    prop_power = estimate_propeller_power(commands.rpm)    # 主推进器功率
    rudder_power = estimate_rudder_servo_power(commands.rudder_rate)    # 舵机功率
    bow_power = estimate_bow_thruster_power(commands.bow_thrust)    # 侧推功率
    
    total_demanded = prop_power + rudder_power + bow_power
    available = power_status.available_power_kW
    
    IF total_demanded > available:
        # 功率超限——按优先级降低
        overload_ratio = total_demanded / available
        
        # 优先级：舵机 > 侧推 > 主推进（航向控制优先于速度）
        IF overload_ratio < 1.3:
            # 轻微超限——只降低主推进
            commands.rpm = reduce_rpm_to_power(commands.rpm, available - rudder_power - bow_power)
        ELIF overload_ratio < 2.0:
            # 中度超限——降低主推进 + 侧推
            commands.bow_thrust *= 0.5
            commands.rpm = reduce_rpm_to_power(commands.rpm, available - rudder_power - bow_power * 0.5)
        ELSE:
            # 严重超限——全部降低
            scale = available / total_demanded
            commands.rpm = int(commands.rpm * scale)
            commands.bow_thrust *= scale
        
        asdr_publish("power_limited", {
            demanded_kW: total_demanded,
            available_kW: available,
            overload_ratio: overload_ratio
        })
    
    RETURN commands
```

### B. 防黑船（Blackout Prevention）

```
# 黑船 = 全船断电。原因通常是发电机过载跳闸。
# 推进器突然加载（如紧急满舵同时全速）可能导致功率需求瞬间超过发电机容量。

FUNCTION blackout_prevention(commands, prev_commands, power_status):
    
    # 限制功率变化率——不允许瞬间加载太多
    power_now = estimate_total_power(prev_commands)
    power_next = estimate_total_power(commands)
    power_ramp = (power_next - power_now) / dt
    
    IF power_ramp > power_status.max_ramp_kW_per_s:
        # 功率增长太快——限制变化率
        max_power_this_step = power_now + power_status.max_ramp_kW_per_s × dt
        scale = max_power_this_step / power_next
        commands.rpm = int(commands.rpm × scale)
        commands.bow_thrust *= scale
        
        log_event("power_ramp_limited", {ramp: power_ramp, limit: power_status.max_ramp_kW_per_s})
    
    RETURN commands
```

### C. 三层功率保护

```
Thrust Allocator:  优化层——在分配时就考虑可用功率（v3.0 新增）
    ↓
Actuator Limiter:  保护层——最终检查总功率不超限（v3.0 新增）
    ↓
发电机保护:       硬件层——过载时跳闸（最后的物理保护——不应该被触发）
```

---

## v3.1 架构升级：反射弧紧急指令的功率限幅保护

### A. 问题——反射弧绕过了 Power Management

v2.0 设计的 Emergency Reflex Arc 路径为"Sensor → bypass L3/L4 → 直插 L5 Actuator Limiter → 执行机构"。这意味着反射弧的 Crash Stop / Hard Rudder 指令不经过 Power Management I/F 的功率校验。

当反射弧触发 Crash Stop（主机满功率反转 + 满舵 + 侧推全开）时，瞬间功率需求可能达到发电机额定功率的 150~200%——超出母排保护阈值，导致主开关跳闸，全船断电（Blackout）。断电意味着失去全部控制能力——比避碰失败更危险。

### B. 解决方案——反射弧经过功率限幅但不经过决策校验

```
反射弧指令的处理路径（v3.1 修正）：

Sensor → Reflex Arc → [BYPASS L3/L4 决策校验] 
                              ↓
              L5 Actuator Limiter（正常的限幅校验）
                              ↓
              ★ 反射弧功率限幅（v3.1 新增）★
                              ↓
              Hardware Override Arbiter → 执行机构

关键区别：
  - 反射弧仍然绕过 L3 COLREGs/Avoidance 的决策校验（保持紧急响应速度）
  - 反射弧仍然绕过 L4 Guidance 的航线校验
  - 但反射弧不绕过 L5 的物理安全校验（限幅+功率）
  - 新增的功率限幅阈值比正常模式更宽松（短时过载 vs 连续额定）
```

### C. 反射弧功率限幅伪代码

```
FUNCTION reflex_power_guard(emergency_cmd, power_status):
    
    # 计算紧急指令的总功率需求
    P_rudder = estimate_rudder_power(emergency_cmd.rudder_deg)
    P_main = estimate_propulsion_power(emergency_cmd.rpm)
    P_bow = estimate_bow_thruster_power(emergency_cmd.bow_thrust)
    P_total = P_rudder + P_main + P_bow
    
    # 获取发电机的短时过载能力
    P_continuous = power_status.generator_continuous_kw    # 连续额定
    P_emergency = P_continuous × 1.15                      # 短时过载 115%（10~30 秒）
    
    IF P_total <= P_emergency:
        # 功率在短时过载范围内——全额放行
        RETURN emergency_cmd
    
    # 功率超出短时过载——必须削峰
    # 策略：优先保证转向（舵机功率小），削减主机推力
    scale_factor = P_emergency / P_total
    
    emergency_cmd.rpm = int(emergency_cmd.rpm × sqrt(scale_factor))
    # sqrt 是因为功率 ∝ RPM³，所以 RPM 缩 sqrt(scale) 使功率缩 scale^1.5
    # 近似处理，保守方向
    
    # 舵角和侧推不削减——转向避碰最优先
    
    asdr_publish("reflex_power_limited", {
        P_demanded: P_total,
        P_allowed: P_emergency,
        scale: scale_factor,
        rpm_before: original_rpm,
        rpm_after: emergency_cmd.rpm
    })
    
    log_event("CRITICAL: reflex power capped to prevent blackout")
    
    RETURN emergency_cmd
```

### D. 功率限幅参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 连续额定功率 | 从 PMS 实时读取 | 取决于在线发电机数量 |
| 短时过载比例 | 115% | 柴油发电机典型值，10~30 秒 |
| 削峰策略 | 削 RPM，保舵角和侧推 | 转向优先 |
| 反射弧指令的功率校验延迟 | < 1ms | 简单比较运算，不影响反射弧速度 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：提升为 Actuator Envelope Checker |
| 0.3 | 2026-04-26 | 架构组 | v3.0 升级：增加 Power Management 功率约束 |
| 0.4 | 2026-04-26 | 架构组 | v3.1 升级：增加反射弧紧急指令的功率限幅保护——反射弧绕过决策校验但不绕过物理安全校验；削峰策略为"削 RPM 保舵角"；防止 Crash Stop 涌流导致全船断电 |

---

**文档结束**
