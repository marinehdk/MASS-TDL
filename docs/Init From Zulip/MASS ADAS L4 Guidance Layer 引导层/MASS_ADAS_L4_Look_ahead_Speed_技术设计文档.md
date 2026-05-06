# MASS_ADAS Look-ahead Speed 前瞻减速模块技术设计文档

**文档编号：** SANGO-ADAS-LAS-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的前瞻减速思维模型
5. 计算流程总览
6. 步骤一：读取前方速度约束
7. 步骤二：减速距离计算
8. 步骤三：减速触发判定
9. 步骤四：实时速度指令生成
10. 步骤五：多约束点的前向扫描
11. 加速指令生成
12. 与 Speed Profiler 预计划的协调
13. 避碰临时航线的速度管理
14. 内部子模块分解
15. 数值算例
16. 参数总览表
17. 与其他模块的协作关系
18. 测试策略与验收标准
19. 参考文献

---

## 1. 概述与职责定义

### 1.1 模块定位

Look-ahead Speed（前瞻减速模块）是 Layer 4（Guidance Layer）中负责"何时开始减速"的子模块。它是引导层输出 u_cmd（目标速度指令）的主要生成者。

这个模块直接解决了引发本系列讨论的原始问题：**船以 15 节高速航行时，到达 90° 转弯航点才发现需要减速，为时已晚。** Look-ahead Speed 确保系统"看到前方的减速需求"并提前开始减速。

### 1.2 核心职责

- **前方约束扫描：** 持续扫描前方航线上的所有速度约束点（转弯、限速区、浅水区）。
- **减速触发判定：** 计算从当前速度减到每个约束点要求的速度所需的距离，判断是否应该现在开始减速。
- **实时速度指令生成：** 在减速过程中，实时计算每个时刻的目标速度 u_cmd，形成平滑的减速曲线。
- **加速指令生成：** 在转弯完成后或驶出限速区后，生成加速指令恢复巡航速度。

### 1.3 设计原则

- **提前减速原则：** 宁可提前开始减速（慢一点到达），不可来不及减速（冲过弯道）。
- **平滑指令原则：** u_cmd 的变化应平滑，避免阶跃式的速度指令导致推力系统冲击。
- **多约束兼顾原则：** 不只看下一个约束点——如果下下个约束点的速度要求更低且距离更近，应提前满足更严格的约束。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 当前船速 V | Nav Filter | 实际对水速度 |
| 沿航线当前位置 | LOS Guidance | along_track_distance |
| 距下一航点距离 | LOS Guidance | distance_to_next_wp |
| 当前活跃航段 | LOS Guidance | active_leg_index |
| 速度剖面 | Speed Profiler (L2) | SpeedProfile 消息 |
| 航线数据 | Voyage Planner (L2) | PlannedRoute（含 speed_at_wp） |
| WOP 状态 | WOP Module | WOP 区域进入通知 |
| 避碰速度约束 | Tactical Layer (L3) | AvoidanceCommand.speed_limit |

### 2.2 输出

```
LookaheadSpeedOutput:
    Header header
    float64 u_cmd                       # 目标速度指令（m/s）
    float64 u_cmd_rate                  # 速度变化率指令（m/s²，负=减速）
    string speed_phase                  # "cruising" / "decelerating" / "accelerating" / "hold"
    float64 distance_to_decel_start     # 距减速起始点的距离（> 0 = 未到）
    string constraint_source            # 当前约束来源
    float64 constraint_speed            # 当前约束速度值
    float64 constraint_distance         # 当前约束点的距离
```

---

## 3. 核心参数数据库

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 规划用减速度 a_decel | 0.10 m/s² | 保守的主动减速能力 |
| 规划用加速度 a_accel | 0.15 m/s² | |
| 减速安全余量系数 η_decel | 1.2 | 减速距离 × 1.2 |
| 出弯加速延迟距离 | max(2L, V×15s) | 转弯后不立即加速 |
| 速度指令平滑率 | 0.5 m/s² | u_cmd 最大变化率 |
| 前向扫描航点数 | 5 | 向前看 5 个航点 |

---

## 4. 船长的前瞻减速思维模型

### 4.1 船长如何决定"该减速了"

船长在高速航行时，他的脑子里有一个"减速倒计时"——他知道前方有一个需要减速的点（转弯、进港、浅水区），他也知道从当前速度减到目标速度大约需要多远。当"减速距离"等于"到约束点的距离"时，就是该减速的时刻。

但好船长不会等到最后一刻才减速。他会给自己留余量——**总是提前 10%~30% 开始减速**。因为实际的减速能力可能受风流影响，也可能因为主机响应比预期慢。

### 4.2 关键经验

- 从 8 m/s 减到 3 m/s，以 0.10 m/s² 减速，需要 275 米。加上 20% 余量就是 330 米。
- 如果前方还要再经过一个更慢的航段，应该按最终目标速度来计算，而不是只看下一个约束。

---

## 5. 计算流程总览

```
每 100ms 执行一次：

    ┌──────────────────────────────────┐
    │ 步骤一：读取前方速度约束           │
    │ 从 Speed Profiler + 航线数据中     │
    │ 提取前方 5 个航点的速度约束        │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：计算到每个约束的减速距离   │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：减速触发判定              │
    │ 取最严格（最早需要减速的）约束     │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：生成实时速度指令 u_cmd     │
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：u_cmd + 速度阶段状态
```

---

## 6. 步骤一：读取前方速度约束

```
FUNCTION scan_ahead_constraints(along_track_pos, route, speed_profile, avoidance_cmd):
    
    constraints = []
    
    # 从当前活跃航段开始，向前扫描 N 个航点
    FOR i = active_leg_index + 1 TO min(active_leg_index + N_SCAN, len(route.waypoints)):
        
        wp = route.waypoints[i]
        dist = compute_distance_along_route(along_track_pos, route, i)
        
        # 约束来源 1：航点转弯安全速度
        IF wp.speed_at_wp > 0 AND wp.speed_at_wp < V_current:
            constraints.append({
                source: "turn_safety",
                V_target: wp.speed_at_wp,
                distance: dist,
                wp_index: i
            })
        
        # 约束来源 2：Speed Profiler 的减速段
        decel_segments = speed_profile.get_decel_segments_in_range(along_track_pos, dist)
        FOR seg IN decel_segments:
            constraints.append({
                source: "speed_profile",
                V_target: seg.speed_end,
                distance: seg.distance_start - along_track_pos,
                wp_index: seg.wp_to
            })
        
        # 约束来源 3：水域限速变化
        IF wp.zone_type != current_zone_type:
            zone_limit = get_zone_speed_limit(wp.zone_type)
            IF zone_limit < V_current:
                constraints.append({
                    source: "zone_limit",
                    V_target: zone_limit,
                    distance: dist,
                    wp_index: i
                })
    
    # 约束来源 4：避碰临时限速
    IF avoidance_cmd.active AND avoidance_cmd.speed_limit > 0:
        constraints.append({
            source: "avoidance",
            V_target: avoidance_cmd.speed_limit,
            distance: 0,    # 立即生效
            wp_index: -1
        })
    
    RETURN constraints

N_SCAN = 5    # 前向扫描航点数
```

---

## 7. 步骤二：减速距离计算

```
FUNCTION compute_decel_distances(V_current, constraints, decel_model):
    
    FOR EACH constraint IN constraints:
        
        IF constraint.V_target >= V_current:
            constraint.decel_distance = 0    # 不需要减速
            constraint.decel_margin = 0
            CONTINUE
        
        # 计算从当前速度减到约束速度的距离
        D_decel = compute_decel_distance(V_current, constraint.V_target, decel_model)
        
        # 加安全余量
        D_decel_with_margin = D_decel × η_DECEL
        
        constraint.decel_distance = D_decel
        constraint.decel_margin_distance = D_decel_with_margin
        
        # 计算"最晚减速点"——距约束点这么远时必须开始减速
        constraint.latest_decel_start = constraint.distance - D_decel_with_margin
    
    RETURN constraints
```

---

## 8. 步骤三：减速触发判定

```
FUNCTION determine_decel_trigger(constraints):
    
    # 找到最严格的约束——即 latest_decel_start 最小（最早需要开始减速）的那个
    active_constraints = [c for c in constraints if c.decel_distance > 0]
    
    IF len(active_constraints) == 0:
        RETURN {trigger: false, phase: "cruising"}
    
    # 按 latest_decel_start 升序排序
    active_constraints.sort(by=lambda c: c.latest_decel_start)
    
    most_urgent = active_constraints[0]
    
    IF most_urgent.latest_decel_start ≤ 0:
        # 已经到了或过了减速起始点——立即减速！
        RETURN {
            trigger: true,
            phase: "decelerating",
            constraint: most_urgent,
            urgency: "immediate"
        }
    
    IF most_urgent.latest_decel_start < V_current × 10:    # < 10 秒航行距离
        # 即将到达减速起始点——准备减速
        RETURN {
            trigger: true,
            phase: "decelerating",
            constraint: most_urgent,
            urgency: "imminent"
        }
    
    RETURN {
        trigger: false,
        phase: "cruising",
        distance_to_decel_start: most_urgent.latest_decel_start
    }
```

---

## 9. 步骤四：实时速度指令生成

### 9.1 减速中的 u_cmd 计算

```
FUNCTION compute_speed_command_decel(V_current, constraint, decel_model):
    
    D_remaining = constraint.distance    # 距约束点的剩余距离
    V_target = constraint.V_target
    
    # 基于剩余距离计算当前应达到的速度
    # 从终点（V_target）反推：到达约束点时速度恰好为 V_target
    V_required = compute_speed_at_distance(V_target, D_remaining, decel_model)
    
    # 如果 V_required > V_current，说明还不需要减速（余量充裕）
    IF V_required > V_current:
        RETURN V_current    # 维持当前速度
    
    # 否则，u_cmd 取 V_required 和 V_current 的较小值
    u_cmd = min(V_current, V_required)
    
    # 但 u_cmd 不低于最终目标速度（不过度减速）
    u_cmd = max(u_cmd, V_target)
    
    RETURN u_cmd

FUNCTION compute_speed_at_distance(V_end, D, decel_model):
    # 反推：如果要在 D 距离后到达 V_end，现在应该是什么速度？
    # 匀减速模型：V_now² = V_end² + 2 × a_decel × D
    V_now = sqrt(V_end² + 2 × decel_model.a_decel × D)
    RETURN V_now
```

### 9.2 巡航中的 u_cmd

```
FUNCTION compute_speed_command_cruise(route, active_leg):
    
    V_cruise = route.waypoints[active_leg].planned_speed
    
    # 避碰限速覆盖
    IF avoidance_cmd.active AND avoidance_cmd.speed_limit > 0:
        V_cruise = min(V_cruise, avoidance_cmd.speed_limit)
    
    RETURN V_cruise
```

### 9.3 u_cmd 平滑

```
FUNCTION smooth_speed_command(u_cmd_raw, u_cmd_prev, dt, max_rate):
    
    change = u_cmd_raw - u_cmd_prev
    max_change = max_rate × dt
    
    IF abs(change) > max_change:
        change = sign(change) × max_change
    
    u_cmd_smoothed = u_cmd_prev + change
    
    RETURN u_cmd_smoothed
```

---

## 10. 步骤五：多约束点的前向扫描

### 10.1 为什么不能只看下一个约束

```
场景：
  当前速度 V = 8 m/s
  航点 A（距 1500m）：V_target = 5 m/s（90° 转弯）
  航点 B（距 1800m，即 A 后 300m）：V_target = 2 m/s（港内限速）

  如果只看 A：从 8 减到 5，需要 275m，在距 A 约 330m 处开始减速
  到达 A 时速度 = 5 m/s，然后需要从 5 减到 2，需要 105m
  但 A 到 B 只有 300m——足够

  但如果 A 到 B 只有 100m呢？从 5 减到 2 需要 105m > 100m！
  正确做法：在减速时就考虑 B 的约束
  从 8 直接减到 2，需要 300m，在距 A 约 360m + 余量处开始
```

### 10.2 贯穿式约束分析

```
FUNCTION cascading_constraint_analysis(constraints, V_current):
    
    # 从最远的约束向近处反向传播
    constraints.sort(by=lambda c: -c.distance)    # 按距离降序
    
    V_required_at_each = {}
    
    FOR i = 0 TO len(constraints) - 1:
        c = constraints[i]
        
        IF i == 0:
            # 最远的约束——直接使用其速度要求
            V_required_at_each[c.wp_index] = c.V_target
        ELSE:
            # 检查从这个约束到下一个（更远的）约束之间的距离
            c_next = constraints[i - 1]    # 更远的约束
            D_between = c_next.distance - c.distance
            
            # 从 c_next 的速度要求反推：到达 c 处最多能有多少速度？
            V_max_at_c = compute_speed_at_distance(
                V_required_at_each[c_next.wp_index], D_between, decel_model
            )
            
            # 取更严格的约束
            V_required_at_each[c.wp_index] = min(c.V_target, V_max_at_c)
    
    # 更新每个约束的有效目标速度
    FOR c IN constraints:
        c.V_target_effective = V_required_at_each.get(c.wp_index, c.V_target)
    
    RETURN constraints
```

---

## 11. 加速指令生成

### 11.1 加速条件

```
FUNCTION check_acceleration_allowed(current_state, wop_status, turn_status):
    
    # 条件 1：不在减速阶段
    IF current_state.phase == "decelerating":
        RETURN false
    
    # 条件 2：不在 WOP 区域内（转弯过程中不加速）
    IF wop_status.wop_status == "in_wop_zone":
        RETURN false
    
    # 条件 3：转弯完成后的延迟
    IF turn_status == "just_completed":
        distance_since_turn = compute_distance_since_last_turn()
        IF distance_since_turn < ACCEL_DELAY_DISTANCE:
            RETURN false
    
    # 条件 4：当前速度 < 计划巡航速度
    IF V_current >= V_cruise_target:
        RETURN false
    
    # 条件 5：前方没有即将到来的减速需求
    IF constraints_ahead AND nearest_constraint.latest_decel_start < ACCEL_LOOK_AHEAD:
        RETURN false    # 马上就要减速了，不值得先加速
    
    RETURN true

ACCEL_DELAY_DISTANCE = max(2 × L, V × 15)    # 出弯后延迟距离
ACCEL_LOOK_AHEAD = V × 30                     # 30 秒内有减速需求就不加速
```

### 11.2 加速 u_cmd

```
FUNCTION compute_speed_command_accel(V_current, V_cruise_target, a_accel, dt):
    
    u_cmd = V_current + a_accel × dt
    u_cmd = min(u_cmd, V_cruise_target)
    
    RETURN u_cmd
```

---

## 12. 与 Speed Profiler 预计划的协调

```
# Speed Profiler（L2）已经为全航线预计划了速度剖面
# Look-ahead Speed（L4）的作用是实时执行这个剖面并做动态修正

FUNCTION coordinate_with_speed_profile(speed_profile, along_track_pos, V_current):
    
    # 从预计划的速度剖面中读取当前位置应达到的速度
    V_planned = speed_profile.get_speed_at(along_track_pos)
    
    # 如果实际速度与计划速度偏差较大
    IF V_current > V_planned × 1.1:
        # 速度偏高 > 10%——Speed Profiler 的减速没有被充分执行
        # Look-ahead Speed 需要更积极地减速
        u_cmd = V_planned    # 直接跟踪计划速度
    
    ELIF V_current < V_planned × 0.9:
        # 速度偏低 > 10%——可能是外部干扰（逆流）
        # 在安全范围内加速追赶
        u_cmd = min(V_planned, V_current + a_accel × dt)
    
    ELSE:
        # 速度接近计划值——正常跟踪
        u_cmd = V_planned
    
    RETURN u_cmd
```

---

## 13. 避碰临时航线的速度管理

```
FUNCTION handle_avoidance_speed(avoidance_cmd, V_current):
    
    IF NOT avoidance_cmd.active:
        RETURN NULL    # 无避碰，不干预
    
    # 避碰限速
    IF avoidance_cmd.speed_limit > 0 AND V_current > avoidance_cmd.speed_limit:
        RETURN {
            u_cmd: avoidance_cmd.speed_limit,
            phase: "decelerating",
            source: "avoidance_limit"
        }
    
    # 避碰目标速度
    IF avoidance_cmd.target_speed > 0:
        RETURN {
            u_cmd: avoidance_cmd.target_speed,
            phase: "decelerating" IF avoidance_cmd.target_speed < V_current ELSE "hold",
            source: "avoidance_target"
        }
    
    RETURN NULL
```

---

## 14. 内部子模块分解

| 子模块 | 职责 | 建议语言 |
|-------|------|---------|
| Constraint Scanner | 前方约束点扫描和收集 | C++ |
| Decel Distance Calculator | 减速距离计算 | C++ |
| Cascading Analyzer | 多约束贯穿式分析 | C++ |
| Trigger Judge | 减速/加速触发判定 | C++ |
| Speed Command Generator | u_cmd 实时生成 + 平滑 | C++ |
| Profile Tracker | 与 Speed Profiler 预计划的同步 | C++ |

---

## 15. 数值算例

```
当前速度 V = 8 m/s
前方约束：
  WP_3（距 800m）：V_target = 5 m/s（60° 转弯）
  WP_4（距 1200m）：V_target = 3 m/s（90° 转弯）

贯穿式分析：
  WP_4 要求 3 m/s
  WP_3 到 WP_4 距离 400m
  从 WP_4 反推 WP_3 处最多：V = sqrt(3² + 2×0.10×400) = sqrt(9+80) = sqrt(89) = 9.4 m/s
  → WP_3 处的有效约束 = min(5, 9.4) = 5 m/s（原约束更严格）

从 8 减到 5：D_decel = (64-25)/(2×0.10) = 195m, 加余量 = 234m
latest_decel_start_3 = 800 - 234 = 566m

→ 当 along_track_distance 距 WP_3 ≤ 566m 时开始减速
→ u_cmd 从 8.0 平滑降至 5.0，在到达 WP_3 时恰好为 5.0 m/s
→ 到达 WP_3 后继续减速至 3.0 m/s，在到达 WP_4 前完成
```

---

## 16. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 规划用减速度 | 0.10 m/s² | |
| 规划用加速度 | 0.15 m/s² | |
| 减速安全余量 | ×1.2 | |
| 前向扫描航点数 | 5 | |
| 出弯加速延迟 | max(2L, V×15s) | |
| 加速前瞻检查距离 | V×30s | |
| 速度指令平滑率 | 0.5 m/s² | |

---

## 17. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| LOS Guidance → Look-ahead | 输入 | 沿航线位置、距下一航点距离 |
| Speed Profiler → Look-ahead | 输入 | 预计划速度剖面 |
| Route → Look-ahead | 输入 | 各航点 speed_at_wp |
| WOP Module → Look-ahead | 输入 | WOP 区域状态 |
| Tactical → Look-ahead | 输入 | 避碰速度约束 |
| Look-ahead → L5 Control | 输出 | u_cmd（目标速度） |

---

## 18. 测试策略与验收标准

| 场景 | 验证目标 | 验收标准 |
|------|---------|---------|
| LAS-001: 单约束减速 | 到达航点时 V ≤ V_target | 速度偏差 < 0.3 m/s |
| LAS-002: 双约束贯穿 | 更远约束的影响正确传播 | 无"来不及减速" |
| LAS-003: 出弯后加速 | 延迟后平滑加速 | 无振荡 |
| LAS-004: 避碰限速 | 立即执行 | 响应 < 0.5s |
| LAS-005: 高速冲弯测试 | 15 节到 3 m/s 减速 | 到达航点时速度达标 |

---

**文档结束**
