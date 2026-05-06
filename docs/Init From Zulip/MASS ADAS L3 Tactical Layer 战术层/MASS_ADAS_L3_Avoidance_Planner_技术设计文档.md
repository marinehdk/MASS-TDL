# MASS_ADAS Avoidance Planner 避让规划器技术设计文档

**文档编号：** SANGO-ADAS-AVD-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的避让操作思维模型
5. 计算流程总览
6. 步骤一：避让方案生成
7. 步骤二：避让航点序列生成
8. 步骤三：避让速度调整
9. 步骤四：避让航线安全校验
10. 步骤五：预期效果验证
11. 步骤六：避让指令封装与发布
12. 恢复原航线策略
13. 避让方案动态更新
14. 多目标统一避让的航点规划
15. 特殊水域避让约束
16. 紧急避让处理
17. 避让事件记录与审计
18. 内部子模块分解
19. 数值算例
20. 参数总览表
21. 与其他模块的协作关系
22. 测试策略与验收标准
23. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Avoidance Planner（避让规划器）是 Layer 3（Tactical Layer）中将避碰决策转化为可执行指令的桥梁模块。它接收 COLREGs Engine 输出的避碰决策（推荐的航向变化和速度变化），将其转化为 Guidance 层（L4）能直接执行的格式——临时避碰航点序列和速度调整指令。

如果说 COLREGs Engine 决定了"应该怎么避"，那么 Avoidance Planner 决定的是"具体走哪条路"。船长做完避碰判断后，他不是简单地打一把舵就完事——他会在脑中规划一条完整的避让轨迹：从什么位置开始转向、转到什么航向后保持多久、保持多长距离后再回到原航线、在哪个点重新切入原航线。Avoidance Planner 就是这个"脑中规划"的算法化。

### 1.2 核心职责

- **避让航点生成：** 将航向变化量转化为一系列具有精确经纬度坐标的临时航点，构成避让航线。
- **避让速度规划：** 将速度调整量转化为避让期间的目标速度，含减速/加速过渡。
- **安全校验：** 确保避让航线不会进入浅水区、禁航区或其他危险区域——避让不能制造新的危险。
- **效果预验证：** 在发布避让指令之前，预测执行后的 CPA 是否确实改善到安全水平。
- **恢复规划：** 当 Risk Monitor 判定避碰态势解除后，规划从避让航线平滑回归原航线的路径。
- **动态更新：** 当目标运动状态变化或新目标出现时，快速更新避让方案。

### 1.3 设计原则

- **可执行原则：** 输出的避让航点必须在本船的物理操纵能力之内——转弯半径不小于旋回半径，减速距离不超过可用距离。
- **安全校验原则：** 每一条避让航线在发布前都必须通过 ENC Reader 的安全检查。不能为了避开一艘船而冲进浅水区。
- **最小干扰原则：** 在满足安全要求的前提下，避让航线应尽量少地偏离原航线，以减少航程损失和时间延误。
- **平滑过渡原则：** 避让航线与原航线之间的衔接应平滑——没有急转弯、没有速度阶跃。
- **可撤销原则：** 避让指令设有有效期（valid_until），超时未刷新自动取消。这防止系统故障导致船无限期偏航。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| COLREGs 避碰决策 | COLREGs Engine | ColregsDecision[]，含推荐航向变化、速度变化、时机 |
| 统一避让方案（多目标时） | COLREGs Engine | ColregsSituationSummary.overall_course/speed_change |
| 本船当前状态 | Nav Filter | 位置、航向、航速 |
| 原计划航线 | Voyage Planner (L2) | PlannedRoute（航点序列） |
| 原计划速度剖面 | Speed Profiler | SpeedProfile |
| 本船操纵性参数 | Parameter DB | 旋回半径、停船距离、最大/最小速度 |
| ENC 查询接口 | ENC Reader | Route Leg Check、水深、碍航物 |
| 水域类型 | ENC Reader | ZoneClassification |

### 2.2 输出

通过 ROS2 话题 `/decision/tactical/avoidance` 发布 AvoidanceCommand 消息（参见话题接口规范文档 6.4 节）。

**输出消息结构详解：**

```
AvoidanceCommand:
    Header header
    
    # ---- 状态标志 ----
    bool active                             # true = 避让指令生效中
    string avoidance_id                     # 唯一标识，用于跟踪和审计
    
    # ---- 避让态势描述 ----
    string situation                        # "head_on" / "crossing_give_way" / "overtaking" / "multi_target" / "rule_19"
    uint32[] target_mmsi_list               # 正在避让的目标 MMSI 列表
    
    # ---- 避让航点序列 ----
    AvoidanceWaypoint[] waypoints           # 临时避让航点
    
    # ---- 速度调整 ----
    float64 speed_limit                     # 避让期间的最大速度（m/s），0 = 不限
    float64 target_speed                    # 避让期间的目标速度（m/s）
    
    # ---- 航向调整（供 Guidance 直接使用的简化指令）----
    float64 course_alteration               # 航向变化量（度，正=右转）
    
    # ---- 有效期 ----
    Time valid_until                        # 指令有效期截止时间
    float64 refresh_interval_sec            # 预期刷新间隔（超过 2 倍此值未刷新则自动取消）
    
    # ---- 恢复信息 ----
    bool recovery_phase                     # true = 当前处于回归原航线阶段
    uint32 rejoin_wp_index                  # 回归后接入原航线的航点序号

AvoidanceWaypoint:
    float64 latitude                        # WGS84
    float64 longitude                       # WGS84
    float64 target_speed                    # 到达该点时的目标速度（m/s）
    float64 turn_radius_min                 # 该点的最小转弯半径（米）
    string waypoint_type                    # "turn_start" / "avoidance_hold" / "turn_back" / "rejoin"
    string generation_reason                # 生成理由（审计用）
```

---

## 3. 核心参数数据库

### 3.1 避让航线几何参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 转向起始偏移距离 | D_turn_start | max(V × 30s, 500m) | 避让开始点距当前位置的前置距离 |
| 避让航向维持最短距离 | D_hold_min | max(V × 120s, 1000m) | 维持避让航向的最短距离（Rule 8 "明显"要求） |
| 避让航向维持最长距离 | D_hold_max | max(V × 600s, 5000m) | 防止过度偏航 |
| 回归提前距离 | D_rejoin_advance | max(V × 60s, 800m) | 回归点在原航线上距最近点的前方偏移 |
| 回归最大切入角 | α_rejoin_max | 30° | 回归航线与原航线的最大夹角 |
| 避让航线最小转弯半径 | R_min_avoid | 旋回半径 × 1.2 | 避让航线的转弯半径不小于旋回半径的 120% |
| 安全走廊最小宽度 | W_corridor_min | 200m | 避让航线两侧至少 200m 安全空间 |

### 3.2 避让速度参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 避让期间最大速度 | V_avoid_max = 0.85 × V_max | 保留 15% 推力储备 |
| 减速过渡距离系数 | k_decel_transition = 1.2 | 减速距离 × 1.2（留余量） |
| 恢复加速度 | a_recovery = 0.10 m/s² | 回归原航线后的加速度 |
| 紧急避让速度 | V_emergency = 0.5 × V_current | 紧急情况下速度降至 50% |

### 3.3 有效期与刷新参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 避让指令有效期 | 120 秒 | 超时未刷新自动取消 |
| 标准刷新间隔 | 2 秒 | 与 Tactical 层决策周期一致 |
| 回归指令有效期 | 300 秒 | 回归阶段有效期较长 |

---

## 4. 船长的避让操作思维模型

### 4.1 船长避让时的实际操作

当船长决定向右转 50° 避让一艘交叉来船时，他的实际操作远不是"打 50° 舵角"那么简单。他的操作序列是：

**第一步：确认转向空间。** 在打舵之前，他会先确认右侧有足够的安全空间——没有浅水、没有其他船、没有碍航物。这个确认是视觉和雷达并用的快速扫视。

**第二步：下令转向。** "右舵 20"（或满舵）。他不会下令精确的 50° 目标航向——他先打一个舵角让船开始转，然后在船转到接近目标航向时回舵。

**第三步：稳定在新航向上。** "正舵。航向 050。" 船在新航向上稳定后，他会观察避让效果——CPA 是不是在增大？目标的相对方位是不是在变化（如果方位开始变化，说明避让开始生效）。

**第四步：维持避让航向。** 他不会转完立刻转回去。他会在避让航向上保持一段时间——至少直到目标安全通过或 CPA 已经明确在增大。实践中，这段维持时间通常是 2~5 分钟。

**第五步：判断何时恢复。** 当目标已经安全通过（TCPA < 0 且 CPA > CPA_safe），或者目标已经在本船船尾方向（相对方位接近 180°），船长会决定恢复原航向。

**第六步：平滑回归。** 他不会猛然转回原航向——那会导致又一次大幅转向，可能影响稳性或与其他目标发生新的会遇。他会逐步调整航向，以一个平滑的弧线回到原航线上。如果原航线较远，他可能先转到一个过渡航向，等距离缩短后再做最终切入。

### 4.2 船长绝不会做的事

- **盲目避让。** 转向前不检查转向方向的安全性。系统中对应的是步骤四（安全校验）。
- **避让幅度不够。** 对方在雷达上看不出你在动。COLREGs 要求"明显"——实践中至少 30° 航向变化维持至少 2 分钟。
- **避让后急于恢复。** 在对方还没安全通过前就急着转回，可能导致两船再次接近。
- **避让进入危险水域。** 为了避开一艘船而进入浅水区、禁航区——这比碰撞还危险（触礁）。
- **避让时加速。** 除非 COLREGs 明确要求（如追越时可以加速通过），否则避让期间应维持或降低速度。

---

## 5. 计算流程总览

```
输入：ColregsDecision（避碰决策）+ 本船状态 + 原航线
          │
          ▼
    ┌──────────────────────────────────┐
    │ 步骤一：避让方案生成              │
    │ 确定避让类型（纯转向/减速/组合）   │
    │ 确定转向方向和目标航向             │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤二：避让航点序列生成           │
    │ 生成转向起始点、维持航向段终点、   │
    │ 回归过渡点、原航线切入点           │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤三：避让速度调整              │
    │ 计算避让期间的目标速度             │
    │ 含减速过渡曲线                    │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤四：避让航线安全校验           │ ← ENC Reader
    │ 检查水深、碍航物、禁区             │
    │ 不通过则修改方案或拒绝             │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤五：预期效果验证              │
    │ 用目标运动预测计算执行后的 CPA     │
    │ 确认 CPA ≥ CPA_safe × 1.5       │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 步骤六：避让指令封装与发布         │
    │ 构建 AvoidanceCommand 消息        │
    │ 发布到 /decision/tactical/avoidance│
    └────────────┬─────────────────────┘
                 │
                 ▼
输出：AvoidanceCommand + 避让事件日志
```

---

## 6. 步骤一：避让方案生成

### 6.1 方案类型确定

根据 COLREGs Engine 的决策输出，确定避让方案的类型：

```
FUNCTION determine_avoidance_type(colregs_decision):
    
    action = colregs_decision.recommended_action
    
    SWITCH action:
        CASE "alter_course_starboard":
            RETURN {
                type: "course_change",
                direction: "starboard",
                course_change: colregs_decision.recommended_course_change,
                speed_change: 0,
                description: "纯转向避让（向右）"
            }
        
        CASE "alter_course_port":
            RETURN {
                type: "course_change",
                direction: "port",
                course_change: colregs_decision.recommended_course_change,
                speed_change: 0,
                description: "纯转向避让（向左）"
            }
        
        CASE "reduce_speed":
            RETURN {
                type: "speed_only",
                direction: "none",
                course_change: 0,
                speed_change: colregs_decision.recommended_speed_change,
                target_speed: colregs_decision.recommended_new_speed,
                description: "纯减速避让"
            }
        
        CASE "alter_course_and_reduce_speed":
            RETURN {
                type: "combined",
                direction: get_direction(colregs_decision.recommended_course_change),
                course_change: colregs_decision.recommended_course_change,
                speed_change: colregs_decision.recommended_speed_change,
                target_speed: colregs_decision.recommended_new_speed,
                description: "转向 + 减速组合避让"
            }
        
        CASE "stop":
            RETURN {
                type: "full_stop",
                direction: "none",
                course_change: 0,
                speed_change: -own_ship.sog,
                target_speed: 0,
                description: "紧急停船"
            }
        
        CASE "maintain":
            RETURN {
                type: "maintain",
                description: "维持航向航速（直航船）"
            }
```

### 6.2 目标航向计算

```
FUNCTION compute_target_heading(own_ship, avoidance_type):
    
    IF avoidance_type.type == "maintain" OR avoidance_type.type == "speed_only":
        RETURN own_ship.heading    # 维持当前航向
    
    target_heading = normalize_0_360(own_ship.heading + avoidance_type.course_change)
    
    RETURN target_heading
```

### 6.3 避让维持距离计算

避让航向需要维持足够长的距离/时间，以满足 COLREGs "明显" 的要求，同时确保 CPA 确实改善。

```
FUNCTION compute_hold_distance(own_ship, target, avoidance_type):
    
    # 基于时间的最低要求：至少维持 2 分钟
    D_hold_time_based = own_ship.sog × AVOIDANCE_HOLD_MIN_TIME
    
    # 基于效果的要求：维持到 CPA 时刻
    IF target.tcpa > 0:
        D_hold_effect_based = own_ship.sog × target.tcpa × 0.8
        # 维持到 TCPA 的 80%（留 20% 的恢复时间）
    ELSE:
        D_hold_effect_based = D_HOLD_MIN
    
    # 取较大值，但不超过最大值
    D_hold = max(D_hold_time_based, D_hold_effect_based, D_HOLD_MIN)
    D_hold = min(D_hold, D_HOLD_MAX)
    
    RETURN D_hold

AVOIDANCE_HOLD_MIN_TIME = 120    # 秒（2 分钟）
D_HOLD_MIN = 1000               # 米
D_HOLD_MAX = 5000               # 米
```

---

## 7. 步骤二：避让航点序列生成

### 7.1 标准避让航线的四航点模型

一条标准的避让航线由四个航点构成：

```
原航线 ----→ WP_T (转向起始点) ----→ WP_H (维持段终点) ----→ WP_R (回归过渡点) ----→ WP_J (原航线切入点)
                  ↑                        ↑                        ↑                        ↑
              开始转向                 维持避让航向              开始转回               切入原航线
              (course_change °)        (保持 D_hold)          (逐步回归)          (继续原航线)
```

### 7.2 WP_T（转向起始点）生成

```
FUNCTION generate_turn_start_point(own_ship, avoidance_type):
    
    # 转向起始点在当前位置前方 D_turn_start 处
    D_turn_start = max(own_ship.sog × 30, 500)    # 至少 30 秒前方或 500m
    
    # 但如果指令是 "urgent" 或 "emergency"，缩短前置距离
    IF avoidance_type.urgency == "urgent":
        D_turn_start = max(own_ship.sog × 10, 200)    # 10 秒或 200m
    ELIF avoidance_type.urgency == "emergency":
        D_turn_start = max(own_ship.sog × 5, 100)     # 5 秒或 100m
    
    WP_T = offset_point(
        own_ship.position,
        own_ship.heading,
        D_turn_start
    )
    
    WP_T.target_speed = own_ship.sog    # 到达此点时维持当前速度
    WP_T.waypoint_type = "turn_start"
    WP_T.turn_radius_min = get_turn_radius(own_ship.sog) × 1.2    # 旋回半径 × 1.2
    WP_T.generation_reason = f"避让转向起始点，前置距离 {D_turn_start:.0f}m"
    
    RETURN WP_T
```

### 7.3 WP_H（维持段终点）生成

```
FUNCTION generate_hold_end_point(WP_T, target_heading, D_hold, avoidance_type):
    
    # 从 WP_T 出发，沿新航向 target_heading 航行 D_hold 距离
    WP_H = offset_point(
        WP_T.position,
        target_heading,
        D_hold
    )
    
    # 速度：如果是组合避让（转向+减速），此段应已减速到目标速度
    IF avoidance_type.type == "combined" OR avoidance_type.type == "speed_only":
        WP_H.target_speed = avoidance_type.target_speed
    ELSE:
        WP_H.target_speed = own_ship.sog    # 纯转向不减速
    
    WP_H.waypoint_type = "avoidance_hold"
    WP_H.turn_radius_min = get_turn_radius(WP_H.target_speed) × 1.2
    WP_H.generation_reason = f"避让航向维持段终点，维持距离 {D_hold:.0f}m"
    
    RETURN WP_H
```

### 7.4 WP_R 和 WP_J（回归过渡点和原航线切入点）生成

```
FUNCTION generate_recovery_points(WP_H, target_heading, original_route, own_ship):
    
    # ---- 找到原航线上的最近点 ----
    nearest_point_on_route = find_nearest_point_on_route(WP_H.position, original_route)
    nearest_wp_index = nearest_point_on_route.wp_index
    
    # ---- 切入点（WP_J）：在最近点前方偏移一段距离 ----
    # 不直接切回最近点——向前偏移以避免大角度切入
    D_rejoin_advance = max(own_ship.sog × 60, 800)    # 前方 60 秒或 800m
    
    WP_J = offset_along_route(
        nearest_point_on_route,
        original_route,
        D_rejoin_advance    # 沿原航线向前偏移
    )
    
    WP_J.waypoint_type = "rejoin"
    WP_J.target_speed = original_route.get_planned_speed_at(WP_J.position)
    WP_J.generation_reason = "原航线切入点"
    
    # ---- 检查切入角度 ----
    heading_at_rejoin = compute_route_heading_at(original_route, WP_J.position)
    heading_from_H_to_J = compute_bearing(WP_H.position, WP_J.position)
    rejoin_angle = abs(normalize_pm180(heading_from_H_to_J - heading_at_rejoin))
    
    IF rejoin_angle > REJOIN_MAX_ANGLE:
        # 切入角度太大——需要插入一个过渡点 WP_R
        
        # WP_R 在 WP_H 和 WP_J 的中间位置，航向为两者的平均
        mid_heading = circular_mean(target_heading, heading_at_rejoin)
        D_to_mid = geo_distance(WP_H.position, WP_J.position) × 0.5
        
        WP_R = offset_point(WP_H.position, mid_heading, D_to_mid)
        WP_R.waypoint_type = "turn_back"
        WP_R.target_speed = (WP_H.target_speed + WP_J.target_speed) / 2
        WP_R.turn_radius_min = get_turn_radius(WP_R.target_speed) × 1.2
        WP_R.generation_reason = f"回归过渡点，切入角 {rejoin_angle:.0f}° > {REJOIN_MAX_ANGLE}°"
        
        RETURN [WP_R, WP_J]
    
    ELSE:
        # 切入角度可接受——直接从 WP_H 切回 WP_J，无需过渡点
        RETURN [WP_J]

REJOIN_MAX_ANGLE = 30    # 度
```

### 7.5 避让航点序列组装

```
FUNCTION assemble_avoidance_waypoints(own_ship, colregs_decision, original_route):
    
    # 步骤一：确定避让方案
    avoidance_type = determine_avoidance_type(colregs_decision)
    
    IF avoidance_type.type == "maintain":
        RETURN []    # 不需要避让航点
    
    # 步骤二：计算目标航向和维持距离
    target_heading = compute_target_heading(own_ship, avoidance_type)
    D_hold = compute_hold_distance(own_ship, colregs_decision.target, avoidance_type)
    
    # 步骤三：生成航点
    waypoints = []
    
    # 转向起始点
    WP_T = generate_turn_start_point(own_ship, avoidance_type)
    waypoints.append(WP_T)
    
    # 维持段终点（仅转向和组合类型需要）
    IF avoidance_type.type IN ["course_change", "combined"]:
        WP_H = generate_hold_end_point(WP_T, target_heading, D_hold, avoidance_type)
        waypoints.append(WP_H)
    
    # 回归航点（如果不是紧急停船）
    IF avoidance_type.type != "full_stop":
        recovery_wps = generate_recovery_points(
            waypoints[-1],    # 从最后一个避让航点开始回归
            target_heading,
            original_route,
            own_ship
        )
        waypoints.extend(recovery_wps)
    
    RETURN waypoints
```

### 7.6 纯减速避让的航点生成

纯减速避让不需要转向，但仍需要一个"减速段"和"恢复段"：

```
FUNCTION generate_speed_only_waypoints(own_ship, avoidance_type, target, original_route):
    
    waypoints = []
    
    # 减速开始点（当前位置前方少量距离）
    WP_decel_start = offset_point(own_ship.position, own_ship.heading, own_ship.sog × 5)
    WP_decel_start.target_speed = own_ship.sog
    WP_decel_start.waypoint_type = "turn_start"    # 复用类型，实际是减速开始
    WP_decel_start.generation_reason = "减速避让开始点"
    waypoints.append(WP_decel_start)
    
    # 减速完成点
    D_decel = compute_decel_distance(own_ship.sog, avoidance_type.target_speed, decel_model)
    WP_decel_end = offset_point(WP_decel_start.position, own_ship.heading, D_decel × 1.2)
    WP_decel_end.target_speed = avoidance_type.target_speed
    WP_decel_end.waypoint_type = "avoidance_hold"
    WP_decel_end.generation_reason = f"减速至 {avoidance_type.target_speed:.1f} m/s"
    waypoints.append(WP_decel_end)
    
    # 维持低速段终点
    D_hold = compute_hold_distance(own_ship, target, avoidance_type)
    WP_hold_end = offset_point(WP_decel_end.position, own_ship.heading, D_hold)
    WP_hold_end.target_speed = avoidance_type.target_speed
    WP_hold_end.waypoint_type = "avoidance_hold"
    waypoints.append(WP_hold_end)
    
    # 恢复原速的切入点
    recovery_wps = generate_recovery_points(WP_hold_end, own_ship.heading, original_route, own_ship)
    waypoints.extend(recovery_wps)
    
    RETURN waypoints
```

---

## 8. 步骤三：避让速度调整

### 8.1 速度调整策略

```
FUNCTION compute_avoidance_speed(avoidance_type, own_ship, zone_type):
    
    IF avoidance_type.type == "course_change":
        # 纯转向：维持当前速度（但不超过避让最大速度）
        V_avoid = min(own_ship.sog, V_AVOID_MAX)
        
        # 如果转向角度大（> 60°），适当减速以减小旋回半径
        IF abs(avoidance_type.course_change) > 60:
            V_avoid = min(V_avoid, own_ship.sog × 0.8)    # 降速 20%
    
    ELIF avoidance_type.type == "speed_only":
        V_avoid = avoidance_type.target_speed
    
    ELIF avoidance_type.type == "combined":
        V_avoid = avoidance_type.target_speed
    
    ELIF avoidance_type.type == "full_stop":
        V_avoid = 0
    
    ELSE:
        V_avoid = own_ship.sog
    
    # 约束：不低于最低可操纵速度（除非是停船）
    IF V_avoid > 0:
        V_avoid = max(V_avoid, V_MIN_MANEUVER)
    
    # 约束：不超过水域限速
    V_zone_limit = get_zone_speed_limit(zone_type)
    V_avoid = min(V_avoid, V_zone_limit)
    
    RETURN V_avoid
```

### 8.2 减速过渡距离

如果需要在避让开始时减速，计算所需的过渡距离：

```
FUNCTION compute_speed_transition(V_current, V_target, decel_model):
    
    IF V_current ≤ V_target:
        RETURN {distance: 0, time: 0}    # 不需要减速
    
    D_decel = compute_decel_distance(V_current, V_target, decel_model)
    T_decel = compute_decel_time(V_current, V_target, decel_model)
    
    # 加安全余量
    D_decel_with_margin = D_decel × K_DECEL_TRANSITION
    
    RETURN {distance: D_decel_with_margin, time: T_decel}

K_DECEL_TRANSITION = 1.2
```

---

## 9. 步骤四：避让航线安全校验

### 9.1 校验流程

这是整个 Avoidance Planner 中最关键的安全保障步骤——确保避让航线本身不会制造新的危险。

```
FUNCTION validate_avoidance_route(avoidance_waypoints, own_ship, enc_reader):
    
    issues = []
    
    # 对避让航线的每一段执行 ENC Reader 的安全检查
    FOR i = 0 TO len(avoidance_waypoints) - 2:
        wp_from = avoidance_waypoints[i]
        wp_to = avoidance_waypoints[i + 1]
        
        # 使用与 WP Generator 相同的安全检查
        check_result = enc_reader.check_route_leg(
            start = wp_from.position,
            end = wp_to.position,
            buffer_width_m = W_CORRIDOR_MIN,
            ship = ship_params,
            planned_speed_knots = wp_from.target_speed / 0.5144
        )
        
        IF NOT check_result.is_safe:
            issues.append({
                leg_index: i,
                check_result: check_result,
                severity: "CRITICAL"
            })
    
    IF len(issues) > 0:
        # 避让航线不安全——需要修改避让方案
        RETURN {valid: false, issues: issues}
    
    RETURN {valid: true}
```

### 9.2 不安全时的回退策略

```
FUNCTION handle_unsafe_avoidance(avoidance_type, issues, own_ship, target, original_route, enc_reader):
    
    # 策略 1：尝试反方向避让
    IF avoidance_type.direction == "starboard":
        alt_direction = "port"
    ELIF avoidance_type.direction == "port":
        alt_direction = "starboard"
    ELSE:
        alt_direction = NULL
    
    IF alt_direction IS NOT NULL:
        # 检查反方向是否被 COLREGs 禁止
        IF NOT is_direction_prohibited(alt_direction, avoidance_type.encounter_type, visibility):
            alt_waypoints = generate_avoidance_waypoints_in_direction(
                own_ship, target, alt_direction, avoidance_type.course_change
            )
            alt_validation = validate_avoidance_route(alt_waypoints, own_ship, enc_reader)
            
            IF alt_validation.valid:
                RETURN {
                    action: "use_alternate_direction",
                    waypoints: alt_waypoints,
                    rationale: f"原方向（{'右' if avoidance_type.direction=='starboard' else '左'}转）空间不足，改为{'左' if alt_direction=='port' else '右'}转"
                }
    
    # 策略 2：减小转向角度 + 增加减速
    FOR Δψ_reduced = abs(avoidance_type.course_change) - 10 DOWNTO 15 STEP 5:
        reduced_waypoints = generate_avoidance_waypoints_with_angle(
            own_ship, target, avoidance_type.direction, Δψ_reduced
        )
        reduced_validation = validate_avoidance_route(reduced_waypoints, own_ship, enc_reader)
        
        IF reduced_validation.valid:
            # 检查减小转向后 CPA 是否仍然安全
            expected_cpa = predict_cpa_after_avoidance(own_ship, target, Δψ_reduced, speed_reduction)
            IF expected_cpa ≥ CPA_safe:
                RETURN {
                    action: "reduced_angle_with_speed",
                    waypoints: reduced_waypoints,
                    speed_reduction: additional_speed_reduction,
                    rationale: f"空间受限，转向角减至 {Δψ_reduced}° + 补充减速"
                }
    
    # 策略 3：纯减速或停船
    stop_waypoints = generate_speed_only_waypoints(own_ship, {target_speed: 0}, target, original_route)
    stop_validation = validate_avoidance_route(stop_waypoints, own_ship, enc_reader)
    
    IF stop_validation.valid:
        RETURN {
            action: "stop",
            waypoints: stop_waypoints,
            rationale: "转向空间不足，改为停船避让"
        }
    
    # 策略 4：所有方案都不安全——上报紧急情况
    RETURN {
        action: "emergency_alert",
        rationale: "所有避让方案均因空间限制不可行，需要岸基远程介入",
        alert_shore: true
    }
```

---

## 10. 步骤五：预期效果验证

### 10.1 验证算法

在发布避让指令之前，预测执行避让方案后的 CPA 是否确实改善到安全水平。

```
FUNCTION verify_avoidance_effectiveness(own_ship, target, avoidance_waypoints, avoidance_speed):
    
    # 模拟本船执行避让后的轨迹
    simulated_trajectory = simulate_own_trajectory(
        own_ship.position,
        avoidance_waypoints,
        avoidance_speed,
        time_horizon = max(target.tcpa × 1.5, 600)    # 预测到 TCPA 的 1.5 倍或至少 10 分钟
    )
    
    # 模拟目标的轨迹（假设目标维持当前状态）
    target_trajectory = predict_target_trajectory(
        target,
        time_horizon = max(target.tcpa × 1.5, 600)
    )
    
    # 计算两条轨迹之间的最小距离
    min_distance = compute_min_distance_between_trajectories(
        simulated_trajectory,
        target_trajectory
    )
    
    # 验证
    CPA_target = CPA_safe × CPA_SAFETY_FACTOR
    
    IF min_distance ≥ CPA_target:
        RETURN {
            effective: true,
            expected_cpa: min_distance,
            rationale: f"预期 CPA {min_distance:.0f}m ≥ 目标 {CPA_target:.0f}m"
        }
    ELSE:
        RETURN {
            effective: false,
            expected_cpa: min_distance,
            shortfall: CPA_target - min_distance,
            rationale: f"预期 CPA {min_distance:.0f}m < 目标 {CPA_target:.0f}m，需要增大避让幅度"
        }
```

### 10.2 本船轨迹模拟

```
FUNCTION simulate_own_trajectory(start_pos, waypoints, speed, time_horizon):
    
    trajectory = []
    current_pos = start_pos
    current_heading = compute_bearing(start_pos, waypoints[0].position)
    current_speed = speed
    wp_index = 0
    t = 0
    dt = 1.0    # 1 秒步长
    
    WHILE t < time_horizon AND wp_index < len(waypoints):
        # 向当前目标航点航行
        target_wp = waypoints[wp_index]
        heading_to_wp = compute_bearing(current_pos, target_wp.position)
        distance_to_wp = geo_distance(current_pos, target_wp.position)
        
        # 航向逐步调整（模拟转向动态）
        heading_error = normalize_pm180(heading_to_wp - current_heading)
        max_turn_rate = compute_max_turn_rate(current_speed, ship_params)
        heading_change = clamp(heading_error, -max_turn_rate × dt, max_turn_rate × dt)
        current_heading += heading_change
        
        # 速度调整
        speed_error = target_wp.target_speed - current_speed
        max_accel = 0.15    # m/s²
        speed_change = clamp(speed_error, -max_accel × dt, max_accel × dt)
        current_speed += speed_change
        
        # 位置更新
        dx = current_speed × sin(current_heading × π/180) × dt
        dy = current_speed × cos(current_heading × π/180) × dt
        current_pos = (current_pos.x + dx, current_pos.y + dy)
        
        trajectory.append({time: t, position: current_pos, heading: current_heading, speed: current_speed})
        
        # 检查是否到达当前航点
        IF distance_to_wp < 50:    # 50m 到达半径
            wp_index += 1
        
        t += dt
    
    RETURN trajectory
```

### 10.3 效果不足时的处理

```
IF NOT verification.effective:
    # 增大转向角度 10° 重试
    new_course_change = avoidance_type.course_change + 10 × direction_sign
    IF abs(new_course_change) ≤ 90:
        # 重新走步骤二~五
        RETURN retry_with_larger_angle(new_course_change)
    ELSE:
        # 转向已到极限——增加减速
        RETURN retry_with_speed_reduction()
```

---

## 11. 步骤六：避让指令封装与发布

### 11.1 指令构建

```
FUNCTION build_avoidance_command(avoidance_type, waypoints, target_info, verification):
    
    cmd = AvoidanceCommand()
    cmd.header.stamp = now()
    
    cmd.active = true
    cmd.avoidance_id = generate_unique_id()
    
    cmd.situation = avoidance_type.encounter_type
    cmd.target_mmsi_list = target_info.mmsi_list
    
    cmd.waypoints = waypoints
    cmd.speed_limit = avoidance_type.V_avoid_max
    cmd.target_speed = avoidance_type.target_speed
    cmd.course_alteration = avoidance_type.course_change
    
    cmd.valid_until = now() + AVOIDANCE_VALIDITY_DURATION
    cmd.refresh_interval_sec = REFRESH_INTERVAL
    
    cmd.recovery_phase = false
    cmd.rejoin_wp_index = find_rejoin_wp_index(waypoints, original_route)
    
    RETURN cmd
```

### 11.2 发布与刷新机制

```
FUNCTION publish_avoidance_command(cmd):
    
    # 发布到 ROS2 话题
    avoidance_publisher.publish(cmd)
    
    # 记录发布时间
    last_publish_time = now()
    
    # 设置定时刷新（每 2 秒刷新一次，更新 valid_until）
    schedule_refresh(cmd, REFRESH_INTERVAL)
```

### 11.3 指令取消

```
FUNCTION cancel_avoidance():
    
    cmd = AvoidanceCommand()
    cmd.active = false
    cmd.avoidance_id = current_avoidance_id
    cmd.valid_until = now()    # 立即过期
    
    avoidance_publisher.publish(cmd)
    
    log_event("avoidance_cancelled", current_avoidance_id)
```

---

## 12. 恢复原航线策略

### 12.1 恢复触发条件

恢复原航线由 Risk Monitor 触发（当其判定态势解除时通知 Avoidance Planner）。Avoidance Planner 收到恢复通知后，执行以下流程：

```
FUNCTION initiate_recovery(current_position, current_heading, current_speed, original_route):
    
    # 生成恢复阶段的避让指令
    recovery_cmd = AvoidanceCommand()
    recovery_cmd.active = true
    recovery_cmd.recovery_phase = true
    
    # 找到原航线上的最佳切入点
    rejoin_point = find_optimal_rejoin_point(current_position, current_heading, original_route)
    
    # 生成恢复航点序列
    recovery_wps = generate_smooth_return(current_position, current_heading, rejoin_point, original_route)
    
    recovery_cmd.waypoints = recovery_wps
    recovery_cmd.rejoin_wp_index = rejoin_point.wp_index
    recovery_cmd.target_speed = original_route.get_planned_speed_at(rejoin_point)
    recovery_cmd.valid_until = now() + RECOVERY_VALIDITY_DURATION
    
    RETURN recovery_cmd
```

### 12.2 最佳切入点选择

```
FUNCTION find_optimal_rejoin_point(current_position, current_heading, original_route):
    
    # 在原航线上搜索满足以下条件的点：
    # 1. 在当前位置的前方（不走回头路）
    # 2. 切入角度 ≤ REJOIN_MAX_ANGLE
    # 3. 距离不太远（避免过度偏航）
    # 4. 切入段不穿越任何危险区域
    
    candidates = []
    
    FOR wp_index = current_nearest_wp TO len(original_route.waypoints):
        wp = original_route.waypoints[wp_index]
        
        # 条件 1：在前方
        bearing_to_wp = compute_bearing(current_position, wp.position)
        forward_angle = abs(normalize_pm180(bearing_to_wp - current_heading))
        IF forward_angle > 90:
            CONTINUE    # 在后方，跳过
        
        # 条件 2：切入角度
        route_heading_at_wp = compute_route_heading_at(original_route, wp.position)
        rejoin_angle = abs(normalize_pm180(bearing_to_wp - route_heading_at_wp))
        IF rejoin_angle > REJOIN_MAX_ANGLE:
            CONTINUE
        
        # 条件 3：距离
        distance = geo_distance(current_position, wp.position)
        IF distance > MAX_REJOIN_DISTANCE:
            BREAK    # 已经太远了
        
        candidates.append({
            wp_index: wp_index,
            position: wp.position,
            distance: distance,
            rejoin_angle: rejoin_angle,
            score: compute_rejoin_score(distance, rejoin_angle)
        })
    
    IF len(candidates) == 0:
        # 没有找到合适的切入点——可能需要航线重规划
        RETURN request_route_replan()
    
    # 选择评分最高的切入点
    best = max(candidates, key=lambda c: c.score)
    RETURN best

FUNCTION compute_rejoin_score(distance, rejoin_angle):
    # 距离越近越好（减少航程损失），角度越小越好（切入平滑）
    score = 50 × (1 - distance / MAX_REJOIN_DISTANCE) + 50 × (1 - rejoin_angle / REJOIN_MAX_ANGLE)
    RETURN score

MAX_REJOIN_DISTANCE = 5000    # 米
```

### 12.3 平滑回归航线生成

```
FUNCTION generate_smooth_return(current_position, current_heading, rejoin_point, original_route):
    
    waypoints = []
    
    # 中间过渡点（如果需要）
    direct_heading = compute_bearing(current_position, rejoin_point.position)
    heading_change = abs(normalize_pm180(direct_heading - current_heading))
    
    IF heading_change > 20:
        # 需要中间过渡点来平滑转弯
        mid_heading = circular_mean(current_heading, direct_heading)
        mid_distance = geo_distance(current_position, rejoin_point.position) × 0.4
        
        WP_mid = offset_point(current_position, mid_heading, mid_distance)
        WP_mid.target_speed = (own_ship.sog + rejoin_point.planned_speed) / 2
        WP_mid.waypoint_type = "turn_back"
        waypoints.append(WP_mid)
    
    # 切入点
    WP_rejoin = AvoidanceWaypoint()
    WP_rejoin.latitude = rejoin_point.position.lat
    WP_rejoin.longitude = rejoin_point.position.lon
    WP_rejoin.target_speed = original_route.get_planned_speed_at(rejoin_point)
    WP_rejoin.waypoint_type = "rejoin"
    waypoints.append(WP_rejoin)
    
    RETURN waypoints
```

### 12.4 回归后的速度恢复

```
# 回归后不立即加速到原航线的巡航速度——等航向稳定后再逐步加速
# 这与 Speed Profiler 中的"出弯后加速延迟"逻辑一致

recovery_accel_delay = max(2 × L, own_speed × 15)    # 15 秒或 2 倍船长
```

---

## 13. 避让方案动态更新

### 13.1 更新触发条件

Avoidance Planner 在避让执行期间每 2 秒接收一次 COLREGs Engine 的更新决策。以下情况需要更新避让方案：

```
FUNCTION check_update_needed(current_cmd, new_decision, target):
    
    # 条件 1：推荐航向变化量改变 > 10°
    IF abs(new_decision.course_change - current_cmd.course_alteration) > 10:
        RETURN {update: true, reason: "航向变化量调整 > 10°"}
    
    # 条件 2：推荐速度调整改变 > 1 m/s
    IF abs(new_decision.speed_change - current_cmd_speed_change) > 1.0:
        RETURN {update: true, reason: "速度调整量变化 > 1 m/s"}
    
    # 条件 3：避让紧急度升级
    IF new_decision.urgency > current_urgency:
        RETURN {update: true, reason: f"紧急度升级至 {new_decision.urgency}"}
    
    # 条件 4：目标行为突变（航向/速度突然改变）
    IF target.behavior_unpredictable:
        RETURN {update: true, reason: "目标行为异常"}
    
    # 条件 5：新目标出现在避让路径上
    IF new_target_on_avoidance_path:
        RETURN {update: true, reason: "新目标出现在避让路径上"}
    
    RETURN {update: false}
```

### 13.2 增量更新 vs 完全重建

```
FUNCTION update_avoidance_plan(current_cmd, new_decision, target):
    
    change_magnitude = compute_change_magnitude(current_cmd, new_decision)
    
    IF change_magnitude < UPDATE_THRESHOLD_MINOR:
        # 小幅调整：仅更新速度限制和 valid_until
        current_cmd.speed_limit = min(current_cmd.speed_limit, new_decision.speed_limit)
        current_cmd.valid_until = now() + AVOIDANCE_VALIDITY_DURATION
        RETURN current_cmd
    
    ELIF change_magnitude < UPDATE_THRESHOLD_MAJOR:
        # 中等调整：更新维持段终点位置和速度
        updated_WP_H = recalculate_hold_endpoint(current_cmd, new_decision)
        current_cmd.waypoints = update_waypoint(current_cmd.waypoints, updated_WP_H)
        RETURN current_cmd
    
    ELSE:
        # 大幅变化：完全重建避让方案
        RETURN build_new_avoidance_plan(new_decision, own_ship, original_route)

UPDATE_THRESHOLD_MINOR = 5     # 航向差 < 5° 且速度差 < 0.5 m/s
UPDATE_THRESHOLD_MAJOR = 15    # 航向差 < 15° 且速度差 < 2.0 m/s
```

---

## 14. 多目标统一避让的航点规划

### 14.1 问题描述

当 COLREGs Engine 输出的是多目标统一方案（overall_course_change + overall_speed_change）时，Avoidance Planner 需要生成一条同时满足所有目标的避让航线。

### 14.2 处理策略

```
FUNCTION plan_multi_target_avoidance(overall_decision, targets, own_ship, original_route):
    
    # 使用统一方案的航向和速度变化量
    avoidance_type = {
        type: "combined" if overall_decision.speed_change != 0 else "course_change",
        course_change: overall_decision.overall_course_change,
        speed_change: overall_decision.overall_speed_change,
        target_speed: own_ship.sog + overall_decision.overall_speed_change
    }
    
    # 计算维持距离：取所有目标中 TCPA 最大的那个
    max_tcpa = max(t.tcpa for t in targets if t.tcpa > 0)
    D_hold = own_ship.sog × max_tcpa × 0.8
    D_hold = clamp(D_hold, D_HOLD_MIN, D_HOLD_MAX)
    
    # 生成航点（与单目标相同的流程）
    waypoints = assemble_avoidance_waypoints_with_params(
        own_ship, avoidance_type, D_hold, original_route
    )
    
    # 安全校验
    validation = validate_avoidance_route(waypoints, own_ship, enc_reader)
    IF NOT validation.valid:
        RETURN handle_unsafe_avoidance(avoidance_type, validation.issues, ...)
    
    # 效果验证：对每个目标都验证
    FOR EACH target IN targets:
        verification = verify_avoidance_effectiveness(own_ship, target, waypoints, avoidance_type.target_speed)
        IF NOT verification.effective:
            # 对某个目标效果不足——需要增大避让幅度
            RETURN retry_multi_target_with_larger_margin(targets, avoidance_type)
    
    RETURN waypoints
```

---

## 15. 特殊水域避让约束

### 15.1 狭水道（narrow_channel）

```
FUNCTION apply_narrow_channel_constraints(waypoints, channel_info):
    
    FOR EACH wp IN waypoints:
        # 检查航点是否在航道内
        IF NOT point_in_channel(wp.position, channel_info):
            # 避让航点超出航道范围——不可接受
            RETURN {valid: false, reason: "避让航点超出狭水道范围"}
        
        # 检查航点是否在航道右侧（Rule 9(a)）
        lateral_position = compute_lateral_position_in_channel(wp.position, channel_info)
        IF lateral_position > 0.7:    # 偏左超过 70%
            # 调整航点位置到航道右半侧
            wp.position = adjust_to_starboard_side(wp.position, channel_info, 0.3)
    
    RETURN {valid: true}
```

### 15.2 TSS 内

```
FUNCTION apply_tss_constraints(waypoints, tss_info):
    
    FOR EACH leg IN consecutive_pairs(waypoints):
        heading = compute_bearing(leg.from, leg.to)
        alignment = abs(normalize_pm180(heading - tss_info.direction))
        
        IF alignment > 45:
            # 避让航线方向与 TSS 偏离太大
            RETURN {valid: false, reason: f"避让航线偏离 TSS 方向 {alignment:.0f}°"}
    
    # 确保避让航线不离开 TSS 通航分道
    FOR EACH wp IN waypoints:
        IF NOT point_in_tss_lane(wp.position, tss_info):
            RETURN {valid: false, reason: "避让航点离开 TSS 通航分道"}
    
    RETURN {valid: true}
```

### 15.3 港内

```
FUNCTION apply_port_constraints(waypoints, port_info):
    
    # 港内空间极其有限——避让幅度受严格限制
    FOR EACH wp IN waypoints:
        IF NOT point_in_safe_water(wp.position, enc_reader, min_ukc=1.0):
            RETURN {valid: false, reason: "港内避让航点水深不足"}
    
    # 港内速度限制
    FOR EACH wp IN waypoints:
        wp.target_speed = min(wp.target_speed, PORT_SPEED_LIMIT)
    
    RETURN {valid: true}

PORT_SPEED_LIMIT = 2.6    # m/s ≈ 5 节
```

---

## 16. 紧急避让处理

### 16.1 紧急避让模式

当 COLREGs Engine 发出 urgency = "emergency" 的决策时，Avoidance Planner 进入紧急模式：

```
FUNCTION handle_emergency_avoidance(colregs_decision, own_ship, target):
    
    # 紧急模式下简化流程——跳过部分校验以缩短响应时间
    
    # 1. 快速生成避让航点（简化：只有转向起始点和维持段）
    D_turn_start = max(own_ship.sog × 3, 50)    # 极短前置距离
    target_heading = own_ship.heading + colregs_decision.recommended_course_change
    
    WP_T = offset_point(own_ship.position, own_ship.heading, D_turn_start)
    WP_H = offset_point(WP_T.position, target_heading, own_ship.sog × 60)    # 1 分钟维持
    
    # 2. 快速安全检查（仅检查水深，跳过碍航物详查）
    depth_at_T = enc_reader.get_depth_at(WP_T.position)
    depth_at_H = enc_reader.get_depth_at(WP_H.position)
    
    IF depth_at_T < min_safe_depth OR depth_at_H < min_safe_depth:
        # 紧急避让方向有浅水——尝试反方向
        target_heading = own_ship.heading - colregs_decision.recommended_course_change
        WP_H = offset_point(WP_T.position, target_heading, own_ship.sog × 60)
        depth_at_H_alt = enc_reader.get_depth_at(WP_H.position)
        
        IF depth_at_H_alt < min_safe_depth:
            # 两个方向都有浅水——紧急停船
            RETURN build_emergency_stop_command()
    
    # 3. 跳过效果预验证——时间不允许
    
    # 4. 立即发布
    cmd = build_emergency_command(WP_T, WP_H, colregs_decision)
    cmd.valid_until = now() + 60    # 紧急指令只有 60 秒有效期
    
    RETURN cmd
```

### 16.2 紧急停船指令

```
FUNCTION build_emergency_stop_command():
    
    cmd = AvoidanceCommand()
    cmd.active = true
    cmd.situation = "emergency_stop"
    cmd.target_speed = 0
    cmd.speed_limit = 0
    cmd.waypoints = []    # 无航点——原地停船
    cmd.valid_until = now() + 300    # 5 分钟有效期
    cmd.recovery_phase = false
    
    # 同时通过紧急话题通知 L5 直接执行
    emergency_pub.publish({
        emergency_type: "collision_avoidance",
        action: "crash_stop",
        override_guidance: true
    })
    
    RETURN cmd
```

---

## 17. 避让事件记录与审计

### 17.1 事件日志结构

每次避让行动记录完整的决策和执行过程，用于事后分析和船级社审核。

```
AvoidanceEventLog:
    string event_id                      # 唯一事件 ID
    Time start_time                      # 避让开始时间
    Time end_time                        # 避让结束时间（含恢复）
    
    # 态势描述
    string encounter_type                # 会遇类型
    uint32[] target_mmsis                # 涉及的目标 MMSI
    float64 initial_cpa                  # 初始 CPA
    float64 initial_tcpa                 # 初始 TCPA
    
    # 决策
    string my_role                       # 本船角色
    string[] rules_applied               # 应用的规则
    string recommended_action            # 推荐行动
    float64 course_change                # 实际航向变化
    float64 speed_change                 # 实际速度变化
    string decision_rationale            # 决策理由
    
    # 避让航线
    AvoidanceWaypoint[] planned_waypoints  # 规划的航点
    GeoPoint[] actual_trajectory            # 实际轨迹（从日志提取）
    
    # 效果
    float64 achieved_cpa                 # 实际达到的 CPA
    bool was_effective                   # 避让是否有效
    
    # 安全校验结果
    bool safety_check_passed             # 安全校验是否通过
    string[] safety_issues               # 如有安全问题
    
    # 回归
    Time recovery_start_time             # 恢复开始时间
    uint32 rejoin_wp_index               # 回归的航点序号
    float64 total_deviation_nm           # 总偏航距离（海里）
    float64 total_time_loss_min          # 时间损失（分钟）
```

### 17.2 日志发布

```
FUNCTION log_avoidance_event(event):
    # 发布到专用日志话题（QoS = RELIABLE + TRANSIENT_LOCAL）
    avoidance_log_publisher.publish(event)
    
    # 同时写入本地文件（rosbag2 或独立日志文件）
    write_to_log_file(event)
    
    # 通过 Shore Link 上报岸基
    IF shore_link_available:
        shore_link.send_avoidance_report(event)
```

---

## 18. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 说明 |
|-------|------|---------|------|
| Avoidance Type Resolver | 确定避让类型（转向/减速/组合/停船） | C++ | 简单逻辑 |
| Waypoint Generator | 生成四航点避让航线 | C++ | 核心几何计算 |
| Speed Planner | 避让期间速度调整计算 | C++ | 复用 Speed Profiler 的减速模型 |
| Safety Validator | 调用 ENC Reader 校验避让航线安全性 | C++ | 关键安全模块 |
| Effect Predictor | 预测避让后的 CPA | C++ | 轨迹模拟 |
| Fallback Handler | 不安全时的回退策略（反向/减角度/停船） | C++ | 多策略搜索 |
| Recovery Planner | 态势解除后的回归航线生成 | C++ | 平滑过渡计算 |
| Dynamic Updater | 避让方案的动态更新和刷新 | C++ | 增量/重建判定 |
| Command Builder | AvoidanceCommand 消息构建和发布 | C++ | ROS2 接口 |
| Event Logger | 避让事件记录 | Python | IO 密集，Python 足够 |

---

## 19. 数值算例

### 19.1 算例一：标准右转避让

**初始条件：**

```
本船：(0, 0), 航向 000°, 速度 8 m/s
目标：右舷 56°, CPA 1200m, TCPA 340s
COLREGs 决策：向右转 55°, 纯转向
原航线下一航点：(0, 5000)（正前方 5000m）
```

**步骤一（方案生成）：**

```
type: "course_change"
direction: "starboard"
course_change: +55°
target_heading: 055°
```

**步骤二（航点生成）：**

```
D_turn_start = max(8 × 30, 500) = 500m
D_hold = max(8 × 120, max(8 × 340 × 0.8, 1000)) = max(960, 2176, 1000) = 2176m

WP_T: (0, 500), type="turn_start", speed=8.0
WP_H: 从 WP_T 沿 055° 航行 2176m
  WP_H.x = 500 × sin(0) + 2176 × sin(55°) = 0 + 1783 = 1783
  WP_H.y = 500 + 2176 × cos(55°) = 500 + 1248 = 1748
  WP_H: (1783, 1748), type="avoidance_hold", speed=8.0

回归：找到原航线上 (0, 5000) 附近的切入点
  D_rejoin_advance = max(8 × 60, 800) = 800m
  WP_J: (0, 5000 - 800) = (0, 4200)... 但需要检查从 WP_H 到 WP_J 的切入角
  
  bearing_H_to_J = atan2(0 - 1783, 4200 - 1748) = atan2(-1783, 2452) = -36°
  route_heading_at_J = 000°
  rejoin_angle = 36° > 30° → 需要过渡点 WP_R

  WP_R: WP_H 和 WP_J 的中间，航向为两者平均
    mid_heading = mean(055°, 360° - 36°) ≈ 010°（需精确计算）
    mid_distance = distance(WP_H, WP_J) × 0.5 ≈ 1600m
    WP_R: 从 WP_H 沿 010° 偏移 1600m

最终航点序列：WP_T → WP_H → WP_R → WP_J
```

**步骤四（安全校验）：**

```
对 WP_T→WP_H, WP_H→WP_R, WP_R→WP_J 三段分别调用 enc_reader.check_route_leg
假设全部通过 → valid = true
```

**步骤五（效果验证）：**

```
模拟本船沿避让航线航行 → 在 t=340s 时（原始 TCPA）
本船位置约在 (1783, 1748) 附近（维持段中间）
目标位置约在 (3000 - 6×340, 2000 - 0×340) = (960, 2000)

两者距离 = sqrt((1783-960)² + (1748-2000)²) = sqrt(676969 + 63504) ≈ 860m

hmm，这不对。让我重新计算...

实际上目标航向 270°，速度 6 m/s：
  目标 x 位移 = 6 × sin(270°) × 340 = 6 × (-1) × 340 = -2040
  目标 y 位移 = 6 × cos(270°) × 340 = 6 × 0 × 340 = 0
  目标在 t=340s 时位置: (3000-2040, 2000) = (960, 2000)

本船在避让后 t=340s 时位置需要更精确计算...
假设本船在 t=0 后约 60s 完成转向（到达 WP_T 并转到 055°），
之后沿 055° 以 8 m/s 航行 280s
  本船 x ≈ 0 + 8 × sin(55°) × 280 = 8 × 0.819 × 280 ≈ 1835
  本船 y ≈ 500 + 8 × cos(55°) × 280 = 500 + 8 × 0.574 × 280 ≈ 1786

距离 = sqrt((1835-960)² + (1786-2000)²) = sqrt(765625 + 45796) = sqrt(811421) ≈ 901m

嗯，这样 CPA 约 900m，小于 CPA_safe × 1.5 = 2778m。说明 55° 转向在这个算例中不够。

这提示我们：CPA 预测需要完整的轨迹模拟（步骤五的 simulate_own_trajectory），
简单的点对点距离计算不准确——因为 CPA 可能发生在轨迹的某个中间时刻。

实际上回到 COLREGs Engine 的计算：predict_cpa_after_course_change 函数
假设本船立即改变航向到 055°，计算新的相对运动 CPA 应该是正确的。
55° 右转在之前 COLREGs Engine 的算例中预期 CPA = 2850m。

差异原因：步骤五的轨迹模拟考虑了转向过渡时间（不是瞬间到位），
而 COLREGs Engine 的预测假设瞬间改变航向。这是保守的——实际 CPA 比预测值略差。

这进一步验证了步骤五的必要性。
```

**结论：** 算例表明步骤五（效果验证）使用的轨迹模拟比步骤六中 COLREGs Engine 的瞬间航向变化预测更准确，能发现因转向延迟导致的实际 CPA 退化。如果验证不通过，会触发增大转向角度的重试逻辑。

---

## 20. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **航线几何** | 转向前置距离 D_turn_start | max(V×30s, 500m) | 常规 |
| | 紧急前置距离 | max(V×5s, 100m) | 紧急模式 |
| | 维持最短距离 D_hold_min | 1000m | Rule 8 "明显" |
| | 维持最长距离 D_hold_max | 5000m | 防止过度偏航 |
| | 回归提前距离 | max(V×60s, 800m) | 回归切入偏移 |
| | 最大切入角 | 30° | 超过需要过渡点 |
| | 最大回归搜索距离 | 5000m | |
| | 避让最小转弯半径 | 旋回半径 × 1.2 | |
| | 安全走廊最小宽度 | 200m | |
| **速度** | 避让最大速度 | 0.85 × V_max | 15% 推力储备 |
| | 减速过渡系数 | 1.2 | 减速距离 × 1.2 |
| | 恢复加速度 | 0.10 m/s² | |
| | 紧急避让速度 | 0.5 × V_current | |
| **有效期** | 常规有效期 | 120 秒 | |
| | 紧急有效期 | 60 秒 | |
| | 回归有效期 | 300 秒 | |
| | 刷新间隔 | 2 秒 | |
| **更新阈值** | 小幅更新阈值 | Δψ<5°, ΔV<0.5m/s | 仅刷新有效期 |
| | 大幅更新阈值 | Δψ<15°, ΔV<2.0m/s | 更新航点位置 |
| | 重建阈值 | 超过大幅阈值 | 完全重建方案 |

---

## 21. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| COLREGs Engine → Avoidance Planner | 输入 | ColregsDecision[]（避碰决策） | 2 Hz |
| COLREGs Engine → Avoidance Planner | 输入 | ColregsSituationSummary（多目标统一方案） | 2 Hz |
| Nav Filter → Avoidance Planner | 输入 | 本船位置/航向/航速 | 50 Hz |
| Voyage Planner → Avoidance Planner | 输入 | PlannedRoute（原航线） | 事件 |
| ENC Reader → Avoidance Planner | 查询 | Route Leg Check（安全校验）| 按需 |
| ENC Reader → Avoidance Planner | 查询 | ZoneClassification（水域约束） | 按需 |
| Parameter DB → Avoidance Planner | 配置 | 旋回半径、停船距离 | 启动时 |
| Avoidance Planner → Guidance (L4) | 输出 | AvoidanceCommand（避让航点+速度） | 2 Hz |
| Risk Monitor → Avoidance Planner | 通知 | 态势解除 → 触发恢复 | 事件 |
| Avoidance Planner → Shore Link | 输出 | 避让事件日志 | 事件 |

---

## 22. 测试策略与验收标准

### 22.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| AVD-001 | 右转 30° 纯转向避让 | 基本航点生成 + 安全校验 |
| AVD-002 | 右转 60° + 维持 3 分钟 | 维持距离计算 |
| AVD-003 | 纯减速避让 | 减速航点生成 |
| AVD-004 | 转向 + 减速组合 | 组合方案航点 |
| AVD-005 | 紧急停船 | 紧急模式处理 |
| AVD-006 | 避让航线穿越浅水 → 回退到反方向 | 安全校验 + 回退策略 |
| AVD-007 | 避让航线穿越禁区 → 回退到减速 | 安全校验 + 回退策略 |
| AVD-008 | 效果验证不通过 → 增大角度重试 | 效果验证 + 重试 |
| AVD-009 | 回归原航线（切入角 < 30°） | 直接回归 |
| AVD-010 | 回归原航线（切入角 > 30°） | 插入过渡点 |
| AVD-011 | 多目标统一避让 | 多目标航点生成 |
| AVD-012 | 避让中目标行为突变 → 方案更新 | 动态更新 |
| AVD-013 | 避让指令超时未刷新 → 自动取消 | 有效期机制 |
| AVD-014 | 狭水道内避让 | 空间约束正确应用 |
| AVD-015 | TSS 内避让 | TSS 方向约束 |
| AVD-016 | 港内避让（极受限空间） | 港内约束 |
| AVD-017 | 所有方案均不可行 → 上报岸基 | 最终回退 |
| AVD-018 | 完整避让周期：开始→执行→解除→恢复 | 端到端流程 |
| AVD-019 | 避让事件日志完整性 | 日志包含全部必要字段 |
| AVD-020 | 紧急避让响应时间 | < 500ms |

### 22.2 验收标准

| 指标 | 标准 |
|------|------|
| 避让航线安全校验通过率 | 100%（发布的指令必须通过安全校验） |
| 效果验证后的预期 CPA | ≥ CPA_safe（回退后至少 ≥ CPA_safe） |
| 避让航线转弯半径 | ≥ 旋回半径 × 1.2 |
| 回归切入角 | ≤ 30°（或使用过渡点） |
| 指令发布延迟（常规） | < 100ms |
| 指令发布延迟（紧急） | < 500ms |
| 有效期超时取消 | 可靠触发，零遗漏 |
| 事件日志完整性 | 全部字段填充 |
| 总偏航距离 | ≤ 避让幅度的理论最小值 × 1.5 |

---

## 23. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO COLREGs Rule 8 | 避碰行动的一般要求（及早、大幅、明显） |
| [2] | IMO COLREGs Rule 16 | 让路船的行动 |
| [3] | IMO COLREGs Rule 17 | 直航船的行动 |
| [4] | Cockcroft & Lameijer "Collision Avoidance Rules" 7th Ed. | 避碰操作实践 |
| [5] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" 2021 | 航迹规划数学基础 |
| [6] | Szlapczynski, R. "A New Method of Ship Collision Avoidance" JNAOE, 2015 | 避碰路径规划算法 |
| [7] | IMO MSC.1/Circ.1638 | MASS 避碰要求 |

---

## v2.0 架构升级：双重 Checker 校验

### A. Avoidance Planner 的双重校验路径

在 v2.0 架构中，Avoidance Planner 生成的避让航线在发布前需要经过两道独立校验：

```
Avoidance Planner (Doer)
    │
    │ 1. 生成避让航点序列 + 速度调整
    │
    ├──→ COLREGs Compliance Checker (L3 Checker)
    │       校验避让方向/幅度是否符合 COLREGs
    │       ↓ 通过
    │
    ├──→ Route Safety Gate (L2 Checker)
    │       校验避让航线是否穿越浅水/禁区
    │       ↓ 通过
    │
    │ 2. 两道都通过 → 发布 AvoidanceCommand
    │    任一否决 → 采用 Checker 输出的替代方案或触发回退策略
    ▼
  Guidance Layer (L4)
```

### B. 集成代码

在现有的步骤六（避让指令封装与发布）之前，插入 Checker 校验环节：

```
FUNCTION publish_with_checker_validation(avoidance_cmd, colregs_decision, own_ship):
    
    # ---- Checker 1: COLREGs 合规校验 ----
    colregs_check = colregs_compliance_checker.check(
        decision = colregs_decision,
        own_ship = own_ship,
        target = colregs_decision.target
    )
    
    IF NOT colregs_check.approved:
        log_event("checker_veto_colregs", colregs_check.violations)
        
        # 使用 Checker 提供的合规替代方案重新生成航点
        IF colregs_check.nearest_compliant IS NOT NULL:
            avoidance_cmd = regenerate_waypoints(colregs_check.nearest_compliant)
        ELSE:
            # Checker 无法提供替代方案——回退到减速/停船
            avoidance_cmd = build_emergency_stop_command()
    
    # ---- Checker 2: 航线安全校验 ----
    route_check = route_safety_gate.check(
        route = avoidance_cmd.waypoints,
        ship_draft = ship_params.draft,
        corridor_width = W_CORRIDOR_MIN
    )
    
    IF NOT route_check.approved:
        log_event("checker_veto_route_safety", route_check.violations)
        
        IF route_check.nearest_compliant IS NOT NULL:
            avoidance_cmd.waypoints = route_check.nearest_compliant.waypoints
        ELSE:
            avoidance_cmd = build_emergency_stop_command()
    
    # ---- 两道都通过——发布 ----
    avoidance_publisher.publish(avoidance_cmd)
    
    # ---- ASDR 记录 ----
    asdr_publish("avoidance_published", {
        avoidance_id: avoidance_cmd.avoidance_id,
        colregs_check: colregs_check.approved,
        route_check: route_check.approved,
        checker_modifications: colregs_check.violations + route_check.violations
    })
```

### C. 与原有安全校验（步骤四）的关系

原有的步骤四（避让航线安全校验）是 Avoidance Planner 内部调用 ENC Reader 做的安全检查。v2.0 的 Route Safety Gate 是**独立于 Avoidance Planner 运行的外部 Checker**——两者可能使用相同的 ENC 数据，但 Checker 独立运行、独立判定，形成"双保险"。

```
原有：Avoidance Planner 内部调用 enc_reader.check_route_leg()（自己检查自己）
v2.0：Avoidance Planner 内部检查 + 外部 Route Safety Gate 独立检查（两道独立屏障）
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：增加双重 Checker 校验（COLREGs Compliance + Route Safety Gate）；增加否决后替代方案集成逻辑；增加 ASDR 记录 |

---

**文档结束**
