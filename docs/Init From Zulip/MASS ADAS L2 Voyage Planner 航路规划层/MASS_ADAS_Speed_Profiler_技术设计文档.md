# MASS_ADAS Speed Profiler 速度剖面生成器技术设计文档

**文档编号：** SANGO-ADAS-SPD-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考——待海试数据标定后更新动力学参数

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的速度决策思维模型
5. 计算流程总览
6. 步骤一：航段速度约束收集
7. 步骤二：全局速度可行性校验
8. 步骤三：三段式速度剖面生成
9. 步骤四：短航段特殊处理
10. 步骤五：多航段连续性优化
11. 步骤六：燃油与时间估算
12. 减速模型详解
13. 加速模型详解
14. 浅水与受限水域的速度修正
15. 风流影响下的速度修正
16. 应急速度策略
17. 内部子模块分解
18. 数值算例
19. 参数总览表
20. 与其他模块的协作关系
21. 测试策略与验收标准
22. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Speed Profiler（速度剖面生成器）是 MASS_ADAS 五层架构中 Layer 2（Voyage Planner，航路规划层）的核心子模块。它接收 WP Generator 输出的航点序列及其附带的速度约束，为每个航段生成一条完整的速度曲线——包含加速段、巡航段和减速段。

Speed Profiler 是将船长"什么时候该加速、什么时候该减速、减到多少"这套决策经验完全算法化的模块。它解决的是那个引发本系列讨论的根本问题：**船以 15 节高速冲到 90° 弯道口才发现需要减速，为时已晚。** Speed Profiler 确保这种情况在规划阶段就被消除。

### 1.2 核心职责

Speed Profiler 的职责可以概括为：

- **生成平滑的速度曲线：** 为每个航段生成从起点速度到终点速度的连续、平滑、物理可行的速度过渡曲线。
- **保证物理可行性：** 速度曲线的每一点都必须在本船推进系统的能力包络之内——加速度不超过推力上限，减速度不超过制动能力。
- **保证安全性：** 到达每个转弯航点时的速度不超过该航点的安全转弯速度 V_safe，经过每个受限水域时的速度不超过该区域的限速值。
- **优化效率：** 在满足安全约束的前提下，尽可能保持高速巡航，减少不必要的减速和加速，节约燃油和时间。

### 1.3 设计原则

- **安全第一原则：** 速度约束的优先级始终是：安全转弯速度 > 水域限速 > 燃油优化 > 时间优化。
- **保守减速原则：** 减速剖面始终按"主动减速"（非紧急倒车）的减速度计算。紧急倒车能力作为 Tactical 层的安全裕度保留，不在规划阶段使用。
- **平滑过渡原则：** 速度变化应尽可能平滑——避免阶跃式的加减速，既保护机械设备，也提高乘员（如有）舒适性和货物安全性。
- **可追溯原则：** 每段速度剖面的生成理由（为何在此处减速、约束来源是什么）应记录在输出数据中。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 航点序列 | WP Generator | 包含每个航点的 planned_speed、speed_at_wp（到达速度约束）、turn_angle、zone_type |
| 水域限速约束 | ENC Reader | 各水域类型的限速值（从 ZoneClassification 获取） |
| 本船动力性能参数 | Parameter DB | 最大推力、最大减速度、加速度曲线（见第 3 节） |
| 停船特性参数 | Parameter DB | 各速度下的减速距离和时间 |
| 浅水效应参数 | Parameter DB | h/T 比与阻力增加系数 |
| 气象数据 | Weather Routing | 预测的风速风向、海流（影响对地速度和燃油消耗） |
| 任务优先级 | Mission Layer | "fuel_optimal" / "time_optimal" / "safety" |

### 2.2 输出

通过 ROS2 话题 `/decision/voyage/speed_profile` 发布，消息类型 `mass_adas_msgs/msg/SpeedProfile`。

**输出结构：**

```
SpeedProfile:
    Header header
    string route_id
    SpeedSegment[] segments

SpeedSegment:
    uint32 wp_from                 # 起始航点序号
    uint32 wp_to                   # 终止航点序号
    float64 distance_start         # 距 wp_from 的起始距离（米）
    float64 distance_end           # 距 wp_from 的终止距离（米）
    string phase                   # "accel" / "cruise" / "decel"
    float64 speed_start            # 段起始速度（m/s）
    float64 speed_end              # 段终止速度（m/s）
    float64 acceleration           # 加/减速度（m/s²，减速为负）
    float64 duration_sec           # 段持续时间（秒）
    string constraint_source       # 约束来源："turn_safety" / "zone_limit" / "ukc" / "fuel_opt" / "none"
    float64 fuel_estimate_kg       # 段燃油消耗估算（千克）
```

同时为 Guidance 层（L4）提供一个简化的查询接口：给定当前沿航线距离，返回该位置的目标速度 u_cmd。

---

## 3. 核心参数数据库

### 3.1 推进系统动力性能参数

**最大可用推力与船速的关系（Bollard Pull 曲线）：**

| 船速 V (m/s) | 最大前进推力 F_max_ahead (N) | 最大倒车推力 F_max_astern (N) | 水阻力 F_drag (N) |
|-------------|---------------------------|-----------------------------|--------------------|
| 0.0 | 待标定 | 待标定 | 0 |
| 2.0 | 待标定 | 待标定 | 待标定 |
| 4.0 | 待标定 | 待标定 | 待标定 |
| 6.0 | 待标定 | 待标定 | 待标定 |
| 8.0 | 待标定 | 待标定 | 待标定 |
| 10.0 | 待标定 | 待标定 | 待标定 |

**重要说明：** 推力和阻力随速度变化的关系是非线性的。水阻力大致与速度的平方成正比（低速排水状态）或更高幂次（高速滑行过渡区有驼峰阻力）。最大推力在高速时因螺旋桨效率下降而减小。因此，可用的净加速力 F_net = F_max_ahead - F_drag 在高速时可能很小，导致高速段的加速能力很弱。

**经验初值（海试前估算）：**

```
水阻力经验公式（排水状态）：
F_drag ≈ 0.5 × ρ × C_T × S × V²

其中：
  ρ   = 海水密度 ≈ 1025 kg/m³
  C_T = 总阻力系数 ≈ 0.003~0.006（取决于船型和雷诺数）
  S   = 湿面积（m²）
  V   = 船速（m/s）

最大前进推力经验估算：
F_max_ahead ≈ K_T × ρ × n² × D⁴

其中：
  K_T = 推力系数（从螺旋桨敞水特性图查取）
  n   = 螺旋桨转速（rps）
  D   = 螺旋桨直径（m）

倒车推力折减系数：
F_max_astern ≈ 0.60~0.70 × F_max_ahead（同转速下）
```

### 3.2 加速度与减速度参数

由推力和阻力推导出各速度下的最大加减速度：

```
最大加速度：
a_max_accel(V) = (F_max_ahead(V) - F_drag(V)) / M_eff

最大主动减速度（关闭推进器，仅靠水阻力）：
a_free_decel(V) = F_drag(V) / M_eff

最大倒车减速度：
a_max_decel(V) = (F_max_astern(V) + F_drag(V)) / M_eff

其中 M_eff = 船舶有效质量 = Δ × (1 + k_added_mass)
  Δ            = 排水量（kg）
  k_added_mass = 附加质量系数 ≈ 0.05~0.15（纵向），取决于船型
```

**简化参数表（规划用常数，海试后替换为速度相关查表值）：**

| 参数 | 符号 | 经验初值范围 | 说明 |
|------|------|------------|------|
| 规划用加速度 | a_plan_accel | 0.10~0.30 m/s² | 取最大加速度的 70%（留余量） |
| 规划用主动减速度 | a_plan_decel | 0.05~0.15 m/s² | 关闭推进器 + 轻微倒车 |
| 规划用自由减速度 | a_free_decel | 0.02~0.08 m/s² | 仅关闭推进器，靠水阻力 |
| 紧急倒车减速度 | a_emergency | 0.30~1.00 m/s² | 仅用于 Tactical 层应急 |
| 主机响应延迟 | t_engine_delay | 2~10 秒 | 从下令变速到推力建立 |
| 最大航速 | V_max | 根据船型（m/s） | 推力与阻力平衡点 |
| 最低可操纵速度 | V_min_maneuver | 1.5~2.0 m/s | 舵效下限（约 3~4 节） |
| 经济航速 | V_econ | V_max × 0.65~0.75 | 燃油效率最优速度 |

### 3.3 减速度非线性说明

**关键物理事实：** 减速度不是常数——它随船速变化。

在高速时，水阻力大（与 V² 成正比），仅靠关闭推进器就能获得较大的减速度。但随着船速降低，水阻力急剧减小（比如从 8 m/s 减到 4 m/s，阻力变为原来的 1/4），自由减速变得越来越慢。最后从 2 m/s 降到 0，可能需要非常长的距离。

这意味着**减速剖面不是匀减速的直线，而是一条先陡后缓的曲线**。如果用匀减速假设（恒定 a_decel）来计算减速距离，会在低速段严重低估所需距离。

**精确减速模型（推荐实现）：** 见第 12 节。

**简化减速模型（初期可用）：** 用分段恒定减速度近似：

```
高速段 (V > 0.6 × V_max):  a_decel = a_plan_decel_high  (较大值)
中速段 (0.3 × V_max < V ≤ 0.6 × V_max): a_decel = a_plan_decel_mid  (中等值)
低速段 (V ≤ 0.3 × V_max):  a_decel = a_plan_decel_low   (较小值)
```

---

## 4. 船长的速度决策思维模型

在深入算法之前，先理解一个经验丰富的船长在航行中如何做速度决策。这是 Speed Profiler 要模拟的思维过程。

### 4.1 全程速度规划

船长在开航前审查整条航线时，会在脑子里大致勾画一条全程速度曲线。他的思维过程是这样的：

首先确定各段的巡航速度——开阔水域用多少速度，经过渔区降到多少，进港引水段降到多少。这些"段速度"主要由水域类型、限速规定和安全惯例决定。

然后在每个速度变化点标注"从这里开始减速"或"从这里开始加速"。减速点必须足够提前——船长会默默估算"以我这艘船的制动能力，从 12 节降到 6 节大概需要多远"，然后在那个距离之前就开始减速。

最后审查全程曲线的连贯性——有没有哪段距离太短来不及减速？有没有不必要的减速-加速-再减速的锯齿波动（浪费燃油）？

### 4.2 实时速度调整

航行中船长会根据实际情况微调速度：

前方能见度下降 → 降速。进入浅水区 → 降速。风浪增大 → 可能降速以减小纵摇。预计到达时间太早 → 降速节油。预计到达时间太晚 → 在安全范围内加速。

这些实时调整在 MASS_ADAS 中由 Guidance 层（L4）和 Tactical 层（L3）处理，不在 Speed Profiler 的职责范围内。Speed Profiler 只负责生成基准速度剖面——一条"在一切正常情况下"的理想速度曲线。

### 4.3 关键经验法则

船长在速度管理中有几条铁律：

- **永远不要在转弯点才开始减速。** 至少提前 2~3 分钟航行距离就开始减速。
- **减速宁早不晚。** 提前减速到位只是速度低一会儿，晚减速到位可能导致冲出航道。
- **进港速度必须留余量。** 港内突发情况多，速度必须低到可以随时停船。
- **加速不急于一时。** 出弯后确认航向稳定、前方安全后再逐步加速，不要弯还没走完就急着提速。
- **短航段不要追求高速。** 如果两个弯之间的距离不够加速到巡航速度再减速回来，就不要加速——维持低速通过两个弯。

---

## 5. 计算流程总览

```
输入：航点序列 + 各航点的速度约束
          │
          ▼
    ┌──────────────────────────┐
    │ 步骤一：航段速度约束收集    │ ← ENC Reader 水域限速
    │ 汇总每个航段的所有约束      │ ← WP Generator 转弯约束
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤二：全局速度可行性校验  │ ← 推进系统参数
    │ 检查是否有"来不及减速"的段  │
    │ 必要时向上游反馈降速请求    │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤三：三段式速度剖面生成  │ ← 加减速模型
    │ 每段：加速段+巡航段+减速段  │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤四：短航段特殊处理      │
    │ 不够加速到巡航再减速的段    │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤五：多航段连续性优化    │
    │ 消除不必要的加速-减速锯齿  │
    │ 平滑相邻航段的速度过渡      │
    └────────────┬─────────────┘
                 │
                 ▼
    ┌──────────────────────────┐
    │ 步骤六：燃油与时间估算      │ ← 阻力-功率曲线
    │ 计算全程耗时、燃油消耗      │
    └────────────┬─────────────┘
                 │
                 ▼
输出：SpeedProfile（全航线速度剖面）
```

---

## 6. 步骤一：航段速度约束收集

### 6.1 约束类型

对于每个航段（WP_i → WP_{i+1}），需要收集以下四类速度约束：

**约束 A：航段最大速度 V_max_leg**

这是该航段内允许的最高速度。取以下各值的最小值：

```
V_max_leg = min(
    V_max_ship,              # 本船最大航速
    V_limit_zone,            # 水域限速（来自 ENC Reader 的 ZoneClassification）
    V_limit_depth,           # 浅水区 Squat 限速（保证 UKC）
    V_limit_weather,         # 恶劣天气限速（如有）
    V_plan_mission           # Mission Layer 指定的计划速度
)
```

**约束 B：到达下一航点的目标速度 V_arrive**

这是到达 WP_{i+1} 时必须降至的速度。取以下各值的最小值：

```
V_arrive = min(
    WP_{i+1}.speed_at_wp,    # WP Generator 计算的安全转弯速度
    V_limit_next_zone,       # 下一航段的水域限速（提前降速至此）
    V_max_leg                # 不高于本航段最大速度
)
```

**约束 C：离开当前航点的初始速度 V_depart**

这是从 WP_i 出发时的速度，等于上一航段到达 WP_i 时的速度（由上一段的 V_arrive 确定）。对于第一个航点（起始点），V_depart 由初始状态决定（停泊出发则为 0，续航则为当前航速）。

```
V_depart = V_arrive_of_previous_leg    # 上一航段的到达速度
```

**约束 D：航段内的局部限速区间**

某些航段可能在中途经过限速区域（比如航段的前半部分在开阔水域，后半部分进入港口接近区）。这种情况下，航段内部需要有额外的减速点。

```
IF 航段内存在 zone_type 变化:
    在 zone 边界处插入一个"虚拟约束点"
    该点的速度约束 = 新 zone 的限速值
    将一个航段拆为两个子航段分别处理
```

### 6.2 约束收集算法

```
FUNCTION collect_constraints(waypoints, enc_reader):

    constraints = []
    
    FOR i = 0 TO len(waypoints) - 2:
        wp_from = waypoints[i]
        wp_to   = waypoints[i + 1]
        leg_distance = geo_distance(wp_from, wp_to)
        
        # 约束 A：航段最大速度
        zone_from = enc_reader.classify_zone(wp_from)
        zone_to   = enc_reader.classify_zone(wp_to)
        
        V_limit_zone = min(
            zone_from.speed_limit if zone_from.speed_limit else V_MAX,
            zone_to.speed_limit if zone_to.speed_limit else V_MAX
        )
        
        # 浅水 Squat 限速
        depth_profile = enc_reader.get_depth_profile(wp_from, wp_to)
        min_depth = depth_profile.min_depth
        V_limit_depth = compute_squat_speed_limit(min_depth, ship_params)
        
        V_max_leg = min(V_MAX_SHIP, V_limit_zone, V_limit_depth, wp_from.planned_speed)
        
        # 约束 B：到达速度
        V_arrive = wp_to.speed_at_wp   # WP Generator 已计算
        V_arrive = min(V_arrive, V_max_leg)
        
        # 约束 C：出发速度（由上一段传递，第一段取初始值）
        IF i == 0:
            V_depart = initial_speed   # 初始速度（停泊=0，续航=当前速度）
        ELSE:
            V_depart = constraints[i-1].V_arrive
        
        constraints.append({
            wp_from: i,
            wp_to: i + 1,
            distance: leg_distance,
            V_max_leg: V_max_leg,
            V_depart: V_depart,
            V_arrive: V_arrive,
            zone_type_from: zone_from.zone_type,
            zone_type_to: zone_to.zone_type
        })
    
    RETURN constraints
```

### 6.3 Squat 限速计算

```
FUNCTION compute_squat_speed_limit(min_depth, ship_params):
    
    T = ship_params.draft
    Cb = ship_params.block_coefficient
    UKC_min = get_ukc_requirement(zone_type)
    
    # 允许的最大 Squat 量
    S_max_allowed = min_depth - T - UKC_min
    
    IF S_max_allowed ≤ 0:
        RETURN V_MIN_MANEUVER   # 水深不足，降至最低可操纵速度
    
    # 由 Barrass 公式反算最大速度
    # S_max = Cb × V_k² / 100
    # V_k = sqrt(S_max_allowed × 100 / Cb)
    V_k_max = sqrt(S_max_allowed * 100 / Cb)
    V_ms_max = V_k_max * 0.5144    # 节 → m/s
    
    RETURN V_ms_max
```

---

## 7. 步骤二：全局速度可行性校验

### 7.1 问题描述

收集完约束后，需要做一次全局校验：**每个航段的距离是否足够从 V_depart 减速到 V_arrive？** 如果不够，说明上游（WP Generator 或 Mission Layer）给出的速度约束组合是物理不可行的。

### 7.2 减速距离校验

```
FUNCTION check_decel_feasibility(constraints, decel_model):

    FOR EACH leg IN constraints:
        
        IF leg.V_depart > leg.V_arrive:
            # 需要减速
            D_decel_required = compute_decel_distance(
                leg.V_depart, leg.V_arrive, decel_model
            )
            
            IF D_decel_required > leg.distance:
                # 不可行！本航段距离不够完成减速
                
                # 计算在本航段距离内能达到的最低速度
                V_achievable = compute_achievable_speed(
                    leg.V_depart, leg.distance, decel_model
                )
                
                # 向上游反馈：要求上一航段降低巡航速度
                # 使得到达本航段起点时的速度 ≤ V_feasible
                V_feasible = compute_feasible_entry_speed(
                    leg.V_arrive, leg.distance, decel_model
                )
                
                feedback.append({
                    leg: leg,
                    issue: "decel_distance_insufficient",
                    required_distance: D_decel_required,
                    available_distance: leg.distance,
                    recommended_V_depart: V_feasible,
                    achievable_V_arrive: V_achievable
                })
    
    IF feedback is not empty:
        # 向 WP Generator 反馈，请求调整上游航段的速度约束
        RETURN {feasible: false, feedback: feedback}
    
    RETURN {feasible: true}
```

### 7.3 反向传播约束

当某个航段的减速距离不够时，Speed Profiler 需要**反向传播**约束——要求上游航段降低巡航速度。这个反向传播可能级联多个航段。

```
FUNCTION backward_propagate(constraints, decel_model):
    
    # 从最后一个航段向前反推
    FOR i = len(constraints) - 1 DOWNTO 0:
        leg = constraints[i]
        
        # 本航段能接受的最大进入速度
        V_max_entry = compute_feasible_entry_speed(
            leg.V_arrive, leg.distance, decel_model
        )
        
        # 如果上游传来的速度太高，钳制它
        IF leg.V_depart > V_max_entry:
            leg.V_depart = V_max_entry
            
            # 同时修改上一航段的到达速度
            IF i > 0:
                constraints[i-1].V_arrive = min(constraints[i-1].V_arrive, V_max_entry)
                
                # 还需要检查上一航段的巡航速度是否也需要降低
                constraints[i-1].V_max_leg = min(constraints[i-1].V_max_leg, V_max_entry)
```

**这就是船长经验中"短航段不追求高速"的算法实现。** 如果两个弯之间距离太短，无法加速到巡航速度再减速回来，反向传播会自动把这些短航段的速度压低到可行水平。

---

## 8. 步骤三：三段式速度剖面生成

### 8.1 标准三段式剖面

对于每个航段，生成"加速段 + 巡航段 + 减速段"的三段式速度剖面。

```
FUNCTION generate_three_phase_profile(leg, accel_model, decel_model):

    V_start = leg.V_depart        # 段起始速度
    V_cruise = leg.V_max_leg      # 巡航速度
    V_end = leg.V_arrive          # 段终止速度
    D_total = leg.distance        # 总距离

    # 1. 计算加速段距离
    IF V_start < V_cruise:
        D_accel = compute_accel_distance(V_start, V_cruise, accel_model)
        T_accel = compute_accel_time(V_start, V_cruise, accel_model)
    ELSE:
        D_accel = 0
        T_accel = 0

    # 2. 计算减速段距离
    IF V_cruise > V_end:
        D_decel = compute_decel_distance(V_cruise, V_end, decel_model)
        T_decel = compute_decel_time(V_cruise, V_end, decel_model)
    ELSE:
        D_decel = 0
        T_decel = 0

    # 3. 计算巡航段距离
    D_cruise = D_total - D_accel - D_decel

    IF D_cruise < 0:
        # 距离不够，进入短航段处理（步骤四）
        RETURN handle_short_leg(leg, accel_model, decel_model)

    T_cruise = D_cruise / V_cruise

    # 4. 构建输出
    segments = []
    
    IF D_accel > 0:
        segments.append({
            phase: "accel",
            distance_start: 0,
            distance_end: D_accel,
            speed_start: V_start,
            speed_end: V_cruise,
            acceleration: accel_model.get_avg_accel(V_start, V_cruise),
            duration_sec: T_accel
        })
    
    IF D_cruise > 0:
        segments.append({
            phase: "cruise",
            distance_start: D_accel,
            distance_end: D_accel + D_cruise,
            speed_start: V_cruise,
            speed_end: V_cruise,
            acceleration: 0,
            duration_sec: T_cruise
        })
    
    IF D_decel > 0:
        segments.append({
            phase: "decel",
            distance_start: D_accel + D_cruise,
            distance_end: D_total,
            speed_start: V_cruise,
            speed_end: V_end,
            acceleration: -decel_model.get_avg_decel(V_cruise, V_end),
            duration_sec: T_decel
        })
    
    RETURN segments
```

### 8.2 减速段起始点确定

减速段的起始点就是"开始减速的位置"，距离航段终点（下一个航点）的距离为 D_decel。

**关键安全余量：** 船长的经验是减速不怕早，就怕晚。建议在计算出的 D_decel 基础上再增加一个安全余量：

```
D_decel_with_margin = D_decel × (1 + k_decel_margin)

k_decel_margin 取值：
  开阔水域，气象良好：0.10（10% 余量）
  沿岸/受限水域：0.20（20% 余量）
  港内：0.30（30% 余量）
  恶劣天气：0.30~0.50
```

增加的余量会挤压巡航段的距离。如果挤压后 D_cruise < 0，进入步骤四的短航段处理。

### 8.3 主机响应延迟的处理

从"下达减速指令"到"推力实际改变"有一个延迟 t_engine_delay。在这段延迟时间内，船仍然以当前速度航行。

```
D_delay = V_cruise × t_engine_delay

实际减速起始点 = 巡航段终点前 D_decel + D_delay 处
```

这个延迟已经包含在 WOP 计算的舵响应修正中（WP Generator 文档第 8.2 节），但在速度剖面中需要单独考虑——因为 WOP 是横向转舵延迟，而这里是纵向推力延迟，两者独立。

---

## 9. 步骤四：短航段特殊处理

### 9.1 问题描述

当 D_accel + D_decel > D_total 时，航段距离不够同时完成加速到巡航和减速到目标速度。这在连续转弯（S 型航线）、港内操纵等场景下很常见。

### 9.2 处理策略

**情况 A：V_start < V_end（需要加速到 V_end）**

不需要减速，只需加速。检查能否在可用距离内加速到 V_end：

```
D_accel_needed = compute_accel_distance(V_start, V_end, accel_model)

IF D_accel_needed ≤ D_total:
    # 可以加速到 V_end，剩余距离以 V_end 巡航
    segments = [accel(V_start→V_end), cruise(V_end)]
ELSE:
    # 距离不够加速到 V_end
    V_achievable = compute_achievable_speed_accel(V_start, D_total, accel_model)
    segments = [accel(V_start→V_achievable)]
    # 反馈给下一航段：进入速度只有 V_achievable
```

**情况 B：V_start > V_end（需要减速到 V_end）**

不加速，只需减速。已在步骤二中校验过可行性。

```
segments = [decel(V_start→V_end)]
# 如果有剩余距离，以 V_end 巡航
```

**情况 C：V_start < V_cruise AND V_cruise > V_end（需要先加速后减速，但距离不够）**

这是最常见的短航段情况。处理方法是计算一个"可达峰值速度" V_peak：

```
FUNCTION compute_peak_speed(V_start, V_end, D_total, accel_model, decel_model):
    
    # 二分搜索 V_peak 使得 D_accel + D_decel = D_total
    V_low = max(V_start, V_end)
    V_high = V_cruise
    
    WHILE (V_high - V_low) > 0.05:   # 精度 0.05 m/s
        V_mid = (V_low + V_high) / 2
        D_accel = compute_accel_distance(V_start, V_mid, accel_model)
        D_decel = compute_decel_distance(V_mid, V_end, decel_model)
        
        IF D_accel + D_decel ≤ D_total:
            V_low = V_mid    # 还有余量，尝试更高的峰值
        ELSE:
            V_high = V_mid   # 超出，降低峰值
    
    RETURN V_low
```

此时速度剖面为"加速到 V_peak + 立即减速到 V_end"的三角形剖面（无巡航段），或者如果 V_peak 接近 V_start/V_end，则退化为一条近似平坦的速度线。

**情况 D：航段极短，任何加减速都不合理**

当 D_total < 最小有意义航段距离（经验值：3 × L 或 50m，取较大者）时，不进行任何速度变化，维持 min(V_start, V_end) 匀速通过。

---

## 10. 步骤五：多航段连续性优化

### 10.1 问题描述

独立处理每个航段后，相邻航段的速度剖面在航点处应该精确衔接。但由于各航段的约束独立处理，可能出现以下不理想的模式。

**锯齿波动：** 航段 A 的末尾减速到 3 m/s（因为航点有 90° 转弯），航段 B 的巡航速度是 8 m/s，航段 B 的末尾又减速到 3 m/s（下一个转弯）。如果航段 B 很短，船刚加速到 8 m/s 马上就要减速，浪费大量燃油。

**不必要的加速：** 连续多个需要低速通过的航点（比如港内连续转弯），如果每个航段都试图在直线段加速到巡航速度，会产生频繁的加速-减速振荡。

### 10.2 优化算法

**优化一：消除短暂的巡航段**

```
FUNCTION eliminate_short_cruise(segments):
    
    FOR EACH segment IN segments:
        IF segment.phase == "cruise" AND segment.duration_sec < T_min_cruise:
            # 巡航段太短，不值得加速到巡航速度
            # 将该航段改为"直接减速"或"匀速通过"
            merge_with_adjacent_phases(segment)

T_min_cruise = 30 秒（经验值，可配置）
```

**优化二：连续低速航点的速度平坦化**

```
FUNCTION flatten_consecutive_slow_points(constraints):
    
    # 识别连续的低速航点组
    slow_groups = find_consecutive_groups(
        constraints, 
        condition = lambda leg: leg.V_arrive < 0.5 * V_MAX_SHIP
    )
    
    FOR EACH group IN slow_groups:
        IF group.total_distance < D_min_for_speedup:
            # 整组距离太短，不值得中途加速
            # 将整组的速度统一为组内最低的 V_arrive
            V_group = min(leg.V_arrive for leg in group)
            FOR EACH leg IN group:
                leg.V_max_leg = V_group
                leg.V_depart = V_group
                leg.V_arrive = V_group

D_min_for_speedup 计算：
    D_accel_to_cruise = compute_accel_distance(V_group, V_cruise)
    D_decel_from_cruise = compute_decel_distance(V_cruise, V_group)
    D_min_for_speedup = (D_accel_to_cruise + D_decel_from_cruise) × 1.5
    # 要求至少有 50% 的巡航段才值得加速
```

**优化三：速度过渡平滑化**

确保加速段和巡航段之间、巡航段和减速段之间的速度变化是平滑的（无阶跃），特别是加速度/减速度的变化应该是渐变的而非突变的。

```
# 在加速段的开始和结束各增加一个"过渡区"
# 过渡区内加速度从 0 线性增加到 a_plan（开始时）
# 或从 a_plan 线性减小到 0（结束时）
# 过渡区长度 = 船长 L 或 2 秒航行距离，取较大者

transition_length = max(L, V_current * 2.0)
```

---

## 11. 步骤六：燃油与时间估算

### 11.1 航段时间计算

```
FUNCTION compute_leg_time(segments):
    total_time = 0
    FOR EACH segment IN segments:
        total_time += segment.duration_sec
    RETURN total_time
```

### 11.2 燃油消耗估算

燃油消耗与推力和时间成正比。精确计算需要发动机的 SFOC（Specific Fuel Oil Consumption）曲线。简化模型：

```
FUNCTION estimate_fuel(segments, engine_params):
    
    total_fuel = 0
    
    FOR EACH segment IN segments:
        V_avg = (segment.speed_start + segment.speed_end) / 2
        
        # 计算该速度下维持航速所需的功率
        F_drag = compute_drag(V_avg)
        P_required = F_drag * V_avg              # 有效功率（瓦）
        P_shaft = P_required / η_propeller       # 轴功率
        P_engine = P_shaft / η_transmission      # 发动机功率
        
        # 燃油消耗率
        SFOC = engine_params.get_sfoc(P_engine)  # g/kWh
        
        fuel_kg = P_engine * (segment.duration_sec / 3600) * SFOC / 1000
        
        # 加速段额外燃油（需要克服惯性）
        IF segment.phase == "accel":
            F_accel = M_eff * abs(segment.acceleration)
            P_accel_extra = F_accel * V_avg
            fuel_kg += P_accel_extra * (segment.duration_sec / 3600) * SFOC / 1000
        
        segment.fuel_estimate_kg = fuel_kg
        total_fuel += fuel_kg
    
    RETURN total_fuel
```

**简化经验公式（无 SFOC 数据时）：**

```
# 海军系数法（Admiralty Coefficient）
# 适用于排水型船舶的粗略估算
# 燃油消耗率 ∝ V³（速度的立方）

fuel_rate(V) = fuel_rate_ref × (V / V_ref)³

其中 fuel_rate_ref 为参考速度 V_ref 下的已知消耗率
```

这意味着速度增加 10%，燃油消耗增加约 33%。这是船长做燃油优化决策的核心经验依据。

### 11.3 ETA 计算

```
FUNCTION compute_eta(speed_profile, departure_time):
    total_time_sec = sum(segment.duration_sec for segment in speed_profile.segments)
    eta = departure_time + timedelta(seconds=total_time_sec)
    RETURN eta
```

如果 ETA 超出 Mission Layer 给定的 ETA 窗口，Speed Profiler 需要评估是否可以通过提高巡航速度来追赶——当然要在安全约束之内。

---

## 12. 减速模型详解

### 12.1 匀减速模型（简化版，初期使用）

假设减速度恒定 a_decel：

```
减速距离：D = (V_start² - V_end²) / (2 × a_decel)
减速时间：T = (V_start - V_end) / a_decel
任意时刻速度：V(t) = V_start - a_decel × t
任意位置速度：V(d) = sqrt(V_start² - 2 × a_decel × d)
```

### 12.2 阻力减速模型（精确版，推荐实现）

真实的减速过程中，减速度随速度变化（因为水阻力 ∝ V²）。关闭推进器后的自由减速运动方程：

```
M_eff × dV/dt = -F_drag(V) = -k × V²

其中 k = 0.5 × ρ × C_T × S

分离变量求解：
1/V² dV = -(k/M_eff) dt

积分得：
-1/V = -(k/M_eff) × t + C

代入初始条件 V(0) = V_0：
V(t) = V_0 / (1 + (k × V_0 / M_eff) × t)

速度-距离关系（对 V ds = V dt 积分）：
s(t) = (M_eff / k) × ln(1 + (k × V_0 / M_eff) × t)

反解：从 V_0 减速到 V_f 所需的距离和时间：
T_decel = (M_eff / k) × (1/V_f - 1/V_0)
D_decel = (M_eff / k) × ln(V_0 / V_f)
```

**关键特征：** 这个模型显示减速距离与速度比的对数成正比。从 8 m/s 减到 4 m/s 需要的距离，远小于从 4 m/s 减到 2 m/s 所需的距离（尽管速度变化量相同）。这就是"低速段减速很慢"的物理原因。

### 12.3 倒车辅助减速模型

如果在自由减速的基础上加入倒车推力 F_astern：

```
M_eff × dV/dt = -(k × V² + F_astern)

此方程没有简单的解析解，需要数值积分。
使用 Runge-Kutta 4 阶方法，步长 0.1 秒。
```

**建议规划时使用的减速模型选择：**

| 场景 | 推荐模型 | 理由 |
|------|---------|------|
| 高速段（V > 0.6 × V_max） | 自由减速（阻力模型） | 阻力大，自由减速即可 |
| 中速段 | 主动减速（自由减速 + 轻倒车） | 阻力减小，需要辅助 |
| 低速段（V < 0.3 × V_max） | 主动减速（较大倒车） | 阻力很小，需要显著倒车 |
| 紧急情况 | 全功率倒车 | 仅 Tactical 层使用 |

---

## 13. 加速模型详解

### 13.1 匀加速模型（简化版）

```
加速距离：D = (V_end² - V_start²) / (2 × a_accel)
加速时间：T = (V_end - V_start) / a_accel
```

### 13.2 推力-阻力平衡加速模型（精确版）

```
M_eff × dV/dt = F_thrust(V) - F_drag(V) = F_net(V)

F_net 随速度增加而减小（推力下降 + 阻力增加），
最终 F_net = 0 时达到最大航速 V_max。

数值积分求解：
t = 0, V = V_start, s = 0
WHILE V < V_target:
    F_net = F_thrust(V) - F_drag(V)
    a = F_net / M_eff
    V_new = V + a × dt
    s += V × dt
    t += dt
    V = V_new

结果：D_accel = s, T_accel = t
```

**加速特征：** 低速时加速快（F_net 大），接近最大航速时加速极慢（F_net 趋近于 0）。从 0 加速到 80% V_max 可能只需要从 0 到 100% V_max 时间的一半。

### 13.3 出弯后的加速延迟

船长的经验：转弯完成后不立即加速，要等航向稳定后再提速。

```
加速延迟条件：
  航向误差 < 5°  AND  转向角速度 < 1°/s

在速度剖面中体现为：出弯后先有一段"保持当前速度"的短暂巡航段
（10~30 秒，或 1~2 个船长的航行距离），然后才进入加速段。

D_accel_delay = max(2 × L, V_depart × 15)  # 15 秒或 2 倍船长
```

---

## 14. 浅水与受限水域的速度修正

### 14.1 浅水阻力增加

浅水中船舶阻力增加，最大航速降低。阻力增加系数：

```
C_shallow = 1.0 + k_s × (T / h)²

其中：
  k_s ≈ 0.5~1.5（取决于船型，肥大型船取大值）
  T = 吃水
  h = 水深

修正后的阻力：
F_drag_shallow = F_drag_deep × C_shallow

修正后的最大航速：
V_max_shallow ≈ V_max_deep × (1 / C_shallow)^(1/3)
```

### 14.2 航道效应

在受限航道中（航道宽度 < 10 × B），阻力进一步增加：

```
C_channel = 1.0 + k_c × (B / W_channel)²

其中：
  k_c ≈ 1.0~3.0
  B = 船宽
  W_channel = 航道宽度
```

### 14.3 Speed Profiler 中的浅水处理

```
FUNCTION apply_shallow_water_correction(leg, depth_profile):
    
    min_h_over_T = min(d / ship.draft for d in depth_profile.depths)
    
    IF min_h_over_T > 3.0:
        RETURN leg  # 无需修正
    
    C_shallow = 1.0 + k_s * (ship.draft / min_depth)²
    
    # 修正该航段的最大速度
    leg.V_max_leg = min(leg.V_max_leg, V_max_deep / C_shallow^(1/3))
    
    # 修正减速模型参数（浅水中阻力更大，减速实际上更快）
    leg.decel_model.drag_multiplier = C_shallow
    
    RETURN leg
```

---

## 15. 风流影响下的速度修正

### 15.1 海流对对地速度的影响

Speed Profiler 生成的速度剖面是对水速度（Speed Through Water）。但 Guidance 层和 Mission Layer 关心的是对地速度（Speed Over Ground）和对地行程时间。

```
V_over_ground = V_through_water + V_current_along_track

其中 V_current_along_track = V_current × cos(current_dir - track_dir)
  正值 = 顺流（实际前进更快）
  负值 = 逆流（实际前进更慢）
```

**Speed Profiler 的处理：**

```
# 如果气象数据可用，计算每个航段的沿航线流速分量
FOR EACH leg:
    V_current_along = compute_current_component(leg, weather_data)
    
    # 对地巡航速度
    V_cruise_og = V_cruise_tw + V_current_along
    
    # 修正航段时间（用对地速度计算）
    leg.duration_corrected = leg.distance / V_cruise_og
    
    # 逆流时可能需要提高对水速度以满足 ETA
    IF V_cruise_og < V_required_for_eta:
        V_cruise_tw_needed = V_required_for_eta - V_current_along
        IF V_cruise_tw_needed ≤ V_max_leg:
            # 可以通过提高对水速度补偿逆流
            leg.V_max_leg_adjusted = V_cruise_tw_needed
        ELSE:
            # 即使全速也无法满足 ETA，向 Mission Layer 报告延迟
```

### 15.2 风阻对航速的影响

逆风增加空气阻力，导致实际可达航速下降：

```
F_wind_drag = 0.5 × ρ_air × C_D_air × A_front × V_apparent_wind²

其中：
  ρ_air ≈ 1.225 kg/m³
  C_D_air ≈ 0.8~1.2（取决于上层建筑形状）
  A_front = 正面受风面积（m²）
  V_apparent_wind = V_true_wind × cos(wind_dir - heading) + V_ship

# 在强逆风（> 25 节）下，对小型 USV 的航速影响可能达到 10~20%
```

---

## 16. 应急速度策略

### 16.1 Speed Profiler 与应急的关系

Speed Profiler 生成的是基准速度剖面。在实际航行中，Tactical 层或 Guidance 层可能需要偏离这个剖面——比如紧急避碰需要急减速，或者为了避让需要临时加速通过某个区域。

Speed Profiler 不处理这些运行时应急情况，但它需要在速度剖面中**为应急预留裕度**：

```
# 应急裕度：巡航速度不超过最大航速的 85%
# 这样在需要加速避让时还有 15% 的推力储备

V_cruise_with_reserve = min(V_cruise, V_max × 0.85)

# 减速裕度：规划用减速度不超过最大减速能力的 70%
# 这样紧急情况下还能多制动 30%

a_plan_decel = a_max_decel × 0.70
```

### 16.2 通信中断时的速度降级

如果岸基通信中断，MASS_ADAS 应自动进入降级模式。Speed Profiler 的降级策略：

```
IF comms_lost:
    # 将全航线巡航速度降低至 70% 原计划值
    # 增大减速裕度
    # 进入安全优先模式
```

---

## 17. 内部子模块分解

| 子模块名称 | 职责 | 建议语言 |
|-----------|------|---------|
| Constraint Collector | 收集并汇总各航段的速度约束 | C++ 或 Python |
| Feasibility Checker | 全局可行性校验 + 反向传播 | C++ |
| Profile Generator | 三段式速度剖面生成 | C++ |
| Short Leg Handler | 短航段特殊处理（三角形剖面） | C++ |
| Continuity Optimizer | 多航段连续性优化（消除锯齿） | C++ |
| Fuel / Time Estimator | 燃油和时间估算 | Python |
| Decel Model | 减速距离/时间计算（含非线性模型） | C++（数值积分） |
| Accel Model | 加速距离/时间计算 | C++（数值积分） |
| Speed Query Service | 给定沿航线距离返回目标速度 | C++ |

---

## 18. 数值算例

### 18.1 船舶参数（示例）

| 参数 | 值 |
|------|-----|
| 船长 L | 12 m |
| 排水量 Δ | 8,000 kg |
| 附加质量系数 k_added_mass | 0.10 |
| 有效质量 M_eff | 8,800 kg |
| 最大航速 V_max | 10 m/s（约 19.4 节） |
| 经济航速 V_econ | 7 m/s（约 13.6 节） |
| 阻力系数 k（= 0.5ρC_TS） | 50 N·s²/m²（简化估值） |

### 18.2 算例一：标准航段（开阔水域，90° 转弯）

**条件：**
- 航段距离 D = 2000 m
- V_depart = 3.0 m/s（上一个弯出来）
- V_cruise = 8.0 m/s（开阔水域）
- V_arrive = 3.0 m/s（下一个弯的安全转弯速度）
- 规划加速度 a_accel = 0.20 m/s²
- 规划减速度 a_decel = 0.10 m/s²

**计算：**

```
加速距离（3.0 → 8.0）：
D_accel = (8.0² - 3.0²) / (2 × 0.20) = (64 - 9) / 0.4 = 137.5 m
T_accel = (8.0 - 3.0) / 0.20 = 25.0 s

减速距离（8.0 → 3.0）：
D_decel = (8.0² - 3.0²) / (2 × 0.10) = 55 / 0.2 = 275.0 m
T_decel = (8.0 - 3.0) / 0.10 = 50.0 s

减速安全余量（开阔水域 10%）：
D_decel_with_margin = 275.0 × 1.10 = 302.5 m

巡航距离：
D_cruise = 2000 - 137.5 - 302.5 = 1560.0 m
T_cruise = 1560.0 / 8.0 = 195.0 s

总时间：25.0 + 195.0 + 50.0 = 270.0 s（4 分 30 秒）
```

**速度剖面：**

```
[0m, 137.5m]      加速段：3.0 → 8.0 m/s,  25.0 秒
[137.5m, 1697.5m] 巡航段：8.0 m/s,         195.0 秒
[1697.5m, 2000m]  减速段：8.0 → 3.0 m/s,   50.0 秒  ← 在距终点 302.5m 处开始减速
```

### 18.3 算例二：短航段（港内连续转弯）

**条件：**
- 航段距离 D = 150 m
- V_depart = 2.0 m/s
- V_cruise = 5.0 m/s（港内限速）
- V_arrive = 2.0 m/s

**计算：**

```
D_accel = (5.0² - 2.0²) / (2 × 0.20) = 21 / 0.4 = 52.5 m
D_decel = (5.0² - 2.0²) / (2 × 0.10) = 21 / 0.2 = 105.0 m
D_decel_margin = 105.0 × 1.30 = 136.5 m（港内 30% 余量）

D_accel + D_decel_margin = 52.5 + 136.5 = 189.0 m > 150 m
→ 短航段！进入步骤四
```

**V_peak 计算（二分搜索）：**

```
尝试 V_peak = 3.5 m/s:
  D_accel = (3.5² - 2.0²) / 0.4 = 8.25 / 0.4 = 20.6 m
  D_decel = (3.5² - 2.0²) / 0.2 = 8.25 / 0.2 = 41.3 m
  D_decel_margin = 41.3 × 1.30 = 53.7 m
  总计 = 20.6 + 53.7 = 74.3 m < 150 m → 还有余量

尝试 V_peak = 4.2 m/s:
  D_accel = (4.2² - 2.0²) / 0.4 = 13.64 / 0.4 = 34.1 m
  D_decel = (4.2² - 2.0²) / 0.2 = 13.64 / 0.2 = 68.2 m
  D_decel_margin = 68.2 × 1.30 = 88.7 m
  总计 = 34.1 + 88.7 = 122.8 m < 150 m → 还有余量

尝试 V_peak = 4.6 m/s:
  D_accel = (4.6² - 2.0²) / 0.4 = 17.16 / 0.4 = 42.9 m
  D_decel = (4.6² - 2.0²) / 0.2 = 17.16 / 0.2 = 85.8 m
  D_decel_margin = 85.8 × 1.30 = 111.5 m
  总计 = 42.9 + 111.5 = 154.4 m > 150 m → 超出

→ V_peak ≈ 4.5 m/s（在 4.2 和 4.6 之间精确二分搜索）
```

**速度剖面（三角形）：**

```
[0m, ~37m]    加速段：2.0 → 4.5 m/s
[~37m, 150m]  减速段：4.5 → 2.0 m/s
无巡航段
```

### 18.4 算例三：非线性减速模型对比

**使用精确阻力减速模型（k = 50 N·s²/m²，M_eff = 8800 kg）：**

```
从 8.0 m/s 自由减速到 3.0 m/s：

T_decel = (M_eff / k) × (1/V_f - 1/V_0)
        = (8800 / 50) × (1/3.0 - 1/8.0)
        = 176 × (0.333 - 0.125)
        = 176 × 0.208
        = 36.6 秒

D_decel = (M_eff / k) × ln(V_0 / V_f)
        = 176 × ln(8.0 / 3.0)
        = 176 × 0.981
        = 172.6 米
```

**对比匀减速模型（a = 0.10 m/s²）：**

```
T_decel = (8.0 - 3.0) / 0.10 = 50.0 秒
D_decel = (8.0² - 3.0²) / (2 × 0.10) = 275.0 米
```

**差异分析：** 精确模型给出的减速距离（172.6m）远小于匀减速模型（275.0m），因为精确模型考虑了高速时水阻力大（减速快）的物理效应。匀减速模型是保守的——它高估了减速距离，导致减速开始得更早。在安全方面这是好事（提前减速总比晚减速安全），但在效率方面浪费了一段巡航距离。

建议初期使用匀减速模型（保守但安全），海试后用实测减速数据标定精确模型再切换。

---

## 19. 参数总览表

### 19.1 推进系统参数（需海试标定）

| 参数 | 符号 | 经验初值 | 标定来源 |
|------|------|---------|---------|
| 规划用加速度 | a_plan_accel | 0.15 m/s² | 系泊试验 + 海试 |
| 规划用减速度（主动） | a_plan_decel | 0.10 m/s² | 停船试验 |
| 自由减速度（仅阻力） | a_free_decel | 0.04 m/s² | 停船试验 |
| 紧急倒车减速度 | a_emergency | 0.50 m/s² | 紧急停船试验 |
| 主机响应延迟 | t_engine_delay | 3 秒 | 系泊试验 |
| 倒车推力折减系数 | k_astern | 0.65 | 系泊试验 |
| 阻力系数 | k (=0.5ρC_TS) | 待标定 | CFD 或海试 |

### 19.2 速度规划经验值

| 参数 | 经验值 | 说明 |
|------|-------|------|
| 减速安全余量 η_decel（开阔） | 1.10 | 减速距离 × 1.10 |
| 减速安全余量 η_decel（沿岸） | 1.20 | |
| 减速安全余量 η_decel（港内） | 1.30 | |
| 减速安全余量 η_decel（恶劣天气） | 1.50 | |
| 出弯加速延迟距离 | max(2L, V×15s) | 转弯完成后不立即加速 |
| 最短有意义巡航段时间 | ≥ 30 秒 | 低于此值则不值得加速 |
| 巡航速度推力储备 | V_cruise ≤ 0.85 × V_max | 保留 15% 应急加速能力 |
| 减速能力储备 | a_plan ≤ 0.70 × a_max | 保留 30% 紧急制动能力 |
| 浅水速度折减（h/T < 2.0） | 降速 30~50% | |
| 经济航速 | 0.65~0.75 × V_max | 燃油效率最优点 |

### 19.3 水域限速标准

| 水域类型 | 建议最大速度 | 依据 |
|---------|------------|------|
| 开阔水域 | V_max 或 V_econ | 无限制 |
| 沿岸航行 | ≤ 12 节 | 安全惯例 |
| 港口接近区 | ≤ 8 节 | 港口规定 |
| 港内 | ≤ 5 节 | 港口规定 |
| 狭水道 | ≤ 10 节 | VTS 规定 |
| 锚地接近 | ≤ 4 节 | 操纵需要 |
| 潜水作业区 | ≤ 5 节 | 安全惯例 |
| 浅水区（h/T < 1.5） | Squat 限速 | 由公式计算 |

---

## 20. 与其他模块的协作关系

| 模块 | 交互方向 | 数据内容 |
|------|---------|---------|
| WP Generator → Speed Profiler | 输入 | 航点序列 + 各航点的 speed_at_wp（安全转弯速度） |
| ENC Reader → Speed Profiler | 输入 | 水域限速、水深剖面（用于 Squat 限速） |
| Weather Routing → Speed Profiler | 输入 | 风流预报（对地速度修正、燃油估算） |
| Mission Layer → Speed Profiler | 输入 | ETA 窗口、燃油预算、优先级 |
| Parameter DB → Speed Profiler | 输入 | 推进系统参数、减速特性、加速特性 |
| Speed Profiler → WP Generator | 反馈 | 减速距离不足时的降速请求（反向传播） |
| Speed Profiler → Guidance Layer (L4) | 输出 | 速度剖面（u_cmd 随沿航线距离的函数） |
| Speed Profiler → Mission Layer | 输出 | 全程 ETA 估算、燃油消耗估算 |

---

## 21. 测试策略与验收标准

### 21.1 测试场景

| 场景编号 | 场景描述 | 验证目标 |
|---------|---------|---------|
| SPD-001 | 单航段，无转弯，匀速巡航 | 基本剖面生成 |
| SPD-002 | 单航段，起点停泊，终点停泊 | 完整加速+巡航+减速 |
| SPD-003 | 单航段，高速进→低速出（大转弯前减速） | 减速距离计算正确性 |
| SPD-004 | 短航段，不够加速到巡航 | 三角形剖面 / V_peak 计算 |
| SPD-005 | 极短航段（< 50m） | 匀速通过，无加减速 |
| SPD-006 | 连续多个短航段（港内 S 型） | 速度平坦化优化 |
| SPD-007 | 速度锯齿消除 | 连续性优化有效 |
| SPD-008 | 浅水区速度限制 | Squat 限速正确应用 |
| SPD-009 | 港内限速 | 水域限速正确应用 |
| SPD-010 | 逆流影响 | 对地速度修正 + ETA 修正 |
| SPD-011 | 减速距离不足（需反向传播） | 反向传播算法正确 |
| SPD-012 | 匀减速 vs 精确模型对比 | 两种模型输出差异在预期范围 |
| SPD-013 | 全航线端到端 | 速度曲线连续、平滑、物理可行 |
| SPD-014 | ETA 窗口约束 | 可通过提速满足 ETA 或报告延迟 |
| SPD-015 | 燃油估算精度 | 与经验值或仿真结果对比 |

### 21.2 验收标准

| 指标 | 标准 |
|------|------|
| 到达每个转弯航点时的速度 | ≤ 该航点的 speed_at_wp |
| 经过限速区域时的速度 | ≤ 该区域限速值 |
| 速度曲线连续性 | 相邻段衔接处速度差 < 0.01 m/s |
| 加速度连续性 | 无阶跃（过渡区平滑） |
| 减速距离充裕性 | 实际减速距离 ≥ 理论减速距离 × η_decel |
| 匀减速模型保守性 | 匀减速估距 ≥ 精确模型估距（不低估） |
| 短航段 V_peak | ≤ V_max_leg（不超速） |
| 全程 ETA | 在 Mission Layer 给定的 ETA 窗口内 |
| 燃油估算误差 | ≤ ±20%（与仿真或实测对比） |

---

## 22. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics and Motion Control" 2021 | 船舶阻力与推进理论 |
| [2] | Harvald, S.A. "Resistance and Propulsion of Ships" 1983 | 阻力估算方法 |
| [3] | Barrass, C.B. "Ship Design and Performance for Masters and Mates" 2004 | Squat 效应计算 |
| [4] | IMO MEPC.203(62) | 能效设计指数（EEDI），燃油效率相关 |
| [5] | IMO MSC.137(76) | 船舶操纵性标准（停船试验） |
| [6] | MAN Energy Solutions "Basic Principles of Ship Propulsion" | 螺旋桨特性、SFOC 曲线 |
| [7] | PIANC "Harbour Approach Channels Design Guidelines" 2014 | 航道设计速度限制 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本——动力学参数待海试标定 |

---

**文档结束**
