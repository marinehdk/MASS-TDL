# MASS_ADAS Safety Checker 安全校验器技术设计文档

**文档编号：** SANGO-ADAS-SAF-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  

---

## 1. 概述与职责定义

### 1.1 模块定位

Safety Checker（安全校验器）是 Layer 2（Voyage Planner）的最终把关子模块。它在 WP Generator 和 Speed Profiler 完成航线和速度剖面的生成后，对全航线执行一次端到端的综合安全复核——相当于船长在航前会议上做的最后一遍航线审查。

### 1.2 核心职责

Safety Checker 不生成航线，不修改航点，不调整速度。它只做一件事：**对已生成的完整航线逐段扫描，检查是否存在任何被前序模块遗漏的安全隐患，并输出一份结构化的安全审查报告。**

如果发现不可接受的安全问题，Safety Checker 向 WP Generator 或 Speed Profiler 发出修正请求（reject + 理由），要求重新规划。只有 Safety Checker 签发"通过"后，航线才能发布给 Guidance 层执行。

### 1.3 设计原则

- **独立审查原则：** Safety Checker 不信任前序模块的内部校验结果，独立调用 ENC Reader 重新检查每一项安全指标。
- **零容忍原则：** 对于 CRITICAL 级别的安全问题（触底风险、碰撞风险、进入禁区），不做权衡直接拒绝。
- **全量检查原则：** 检查全部航段、全部航点、全部速度剖面，不做抽样。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 说明 |
|-------|------|------|
| 完整航点序列 | WP Generator | PlannedRoute 消息 |
| 完整速度剖面 | Speed Profiler | SpeedProfile 消息 |
| 本船参数 | Parameter DB | 吃水、宽度、高度、旋回参数 |
| ENC 查询接口 | ENC Reader | 六类查询服务 |

### 2.2 输出

```
SafetyReport:
    Header header
    string route_id
    bool approved                          # 总体是否通过
    SafetyIssue[] critical_issues          # CRITICAL 级：必须修正
    SafetyIssue[] warning_issues           # WARNING 级：建议修正
    SafetyIssue[] info_issues              # INFO 级：记录但不阻止
    LegSafetyResult[] leg_results          # 逐航段的检查结果

SafetyIssue:
    string issue_id                        # 唯一编号（如 "SAF-UKC-003"）
    string category                        # "ukc" / "obstacle" / "restriction" / "turn" / "speed" / "clearance" / "coverage"
    string severity                        # "CRITICAL" / "WARNING" / "INFO"
    uint32 wp_index                        # 相关航点序号
    float64 latitude                       # 问题位置
    float64 longitude
    string description                     # 人类可读的问题描述
    string recommendation                  # 修正建议
```

---

## 3. 检查项目清单

Safety Checker 执行以下十类检查，按优先级排列：

### 3.1 检查一：陆地穿越（CRITICAL）

对每个航段调用 ENC Reader 的 Route Leg Check，确认航线不穿越任何陆地区域。

```
FOR EACH leg:
    result = enc_reader.check_route_leg(leg)
    IF result.crosses_land:
        → CRITICAL: "航段 {i} 穿越陆地"
```

### 3.2 检查二：UKC 充裕性（CRITICAL / WARNING）

对每个航段获取水深剖面，计算动态 UKC（考虑 Squat）：

```
FOR EACH leg:
    depth_profile = enc_reader.get_depth_profile(leg)
    V_plan = speed_profile.get_speed_at(leg.midpoint)
    S_squat = compute_squat(V_plan, ship.Cb)
    
    FOR EACH sample IN depth_profile:
        UKC_dynamic = sample.depth - ship.draft - S_squat
        
        IF UKC_dynamic < UKC_min_critical:
            → CRITICAL: f"航段 {i} 距起点 {sample.distance}m 处 UKC 仅 {UKC_dynamic:.1f}m"
        ELIF UKC_dynamic < UKC_min_warning:
            → WARNING: f"航段 {i} UKC 余量不足（{UKC_dynamic:.1f}m）"
```

UKC 阈值：

| 水域类型 | CRITICAL 阈值 | WARNING 阈值 |
|---------|-------------|-------------|
| 开阔水域 | < 1.0m | < 3.0m |
| 沿岸 | < 0.5m | < 2.0m |
| 港内 | < 0.3m | < 1.0m |

### 3.3 检查三：碍航物安全距离（CRITICAL / WARNING）

```
FOR EACH waypoint:
    obstructions = enc_reader.search_obstructions(waypoint, radius=1000m)
    FOR EACH obs IN obstructions:
        IF obs.danger_level == "CRITICAL" AND obs.distance < obs.safe_distance:
            → CRITICAL: f"航点 {i} 距危险碍航物仅 {obs.distance:.0f}m（要求 ≥ {obs.safe_distance:.0f}m）"
        ELIF obs.danger_level == "WARNING" AND obs.distance < obs.safe_distance:
            → WARNING
```

### 3.4 检查四：限制区域穿越（CRITICAL / WARNING）

```
FOR EACH leg:
    result = enc_reader.check_route_leg(leg)
    FOR EACH violation IN result.intersected_restrictions:
        IF violation.must_avoid:
            → CRITICAL: f"航段 {i} 穿越禁航区 {violation.area_name}"
        ELIF violation.has_speed_limit:
            # 检查速度剖面是否遵守了限速
            V_at_zone = speed_profile.get_max_speed_in_range(violation.start_dist, violation.end_dist)
            IF V_at_zone > violation.speed_limit:
                → CRITICAL: f"航段 {i} 限速区内速度 {V_at_zone:.1f} 超过限速 {violation.speed_limit:.1f}"
```

### 3.5 检查五：TSS 合规性（CRITICAL / WARNING）

```
FOR EACH leg:
    result = enc_reader.check_route_leg(leg)
    IF result.crosses_tss_against_traffic:
        → CRITICAL: "航段 {i} 逆向穿越 TSS"
    
    # 检查进出 TSS 的角度
    IF leg enters or exits TSS:
        entry_angle = compute_tss_entry_angle(leg, tss)
        IF entry_angle > 15°:
            → WARNING: f"进出 TSS 角度 {entry_angle:.0f}°（建议 ≤ 15°）"
```

### 3.6 检查六：转弯安全性复核（CRITICAL / WARNING）

独立于 WP Generator 重新校验每个转弯的空间可行性：

```
FOR EACH waypoint WITH turn_angle > 5°:
    corridor = enc_reader.get_corridor_width(waypoint)
    W_usable = min(corridor.width_port, corridor.width_stbd) × η_safety
    
    V_at_wp = speed_profile.get_speed_at(waypoint)
    Y_max = compute_turn_sweep(waypoint.turn_angle, V_at_wp, ship_params)
    
    IF Y_max > W_usable:
        → CRITICAL: f"航点 {i} 转弯空间不足（需要 {Y_max:.0f}m，可用 {W_usable:.0f}m）"
```

### 3.7 检查七：减速距离充裕性（CRITICAL）

验证 Speed Profiler 给出的减速段是否在物理上可行：

```
FOR EACH decel_segment IN speed_profile:
    D_required = compute_decel_distance(segment.speed_start, segment.speed_end, decel_model)
    D_available = segment.distance_end - segment.distance_start
    
    IF D_required > D_available:
        → CRITICAL: f"航段 {segment.wp_from}-{segment.wp_to} 减速距离不足（需 {D_required:.0f}m，仅 {D_available:.0f}m）"
    ELIF D_required > D_available × 0.85:
        → WARNING: f"减速距离余量不足（{((D_available/D_required)-1)*100:.0f}%）"
```

### 3.8 检查八：垂直净空（CRITICAL）

```
FOR EACH leg:
    result = enc_reader.check_route_leg(leg)
    FOR EACH clr IN result.clearance_checks:
        IF NOT clr.passable:
            → CRITICAL: f"航段 {i} 经过桥梁/架空电缆，净空不足"
```

### 3.9 检查九：航点间距（WARNING）

```
FOR EACH consecutive pair (wp_i, wp_{i+1}):
    D = geo_distance(wp_i, wp_{i+1})
    
    IF D < D_min_for_zone(wp_i.zone_type):
        → WARNING: f"航点 {i}-{i+1} 间距过小（{D:.0f}m）"
    IF D > D_max_for_zone(wp_i.zone_type):
        → WARNING: f"航点 {i}-{i+1} 间距过大（{D:.0f}m）"
```

### 3.10 检查十：海图数据质量（WARNING / INFO）

```
FOR EACH leg:
    chart_info = enc_reader.get_chart_info(leg)
    
    IF chart_info.worst_zoc >= "C":
        → WARNING: f"航段 {i} 经过 ZOC {chart_info.worst_zoc} 区域，数据可靠性较低"
    
    IF chart_info.days_since_update > 30:
        → WARNING: f"航段 {i} 海图数据超过 30 天未更新"
    
    IF chart_info.coverage_gap:
        → WARNING: f"航段 {i} 缺少大比例尺海图覆盖"
```

---

## 4. 审查流程

```
FUNCTION run_safety_check(route, speed_profile, ship_params, enc_reader):
    
    report = SafetyReport(route_id=route.route_id)
    
    # 按优先级顺序执行全部十类检查
    check_land_crossing(route, enc_reader, report)
    check_ukc(route, speed_profile, ship_params, enc_reader, report)
    check_obstructions(route, enc_reader, report)
    check_restrictions(route, speed_profile, enc_reader, report)
    check_tss_compliance(route, enc_reader, report)
    check_turn_safety(route, speed_profile, ship_params, enc_reader, report)
    check_decel_feasibility(speed_profile, ship_params, report)
    check_vertical_clearance(route, ship_params, enc_reader, report)
    check_waypoint_spacing(route, report)
    check_chart_quality(route, enc_reader, report)
    
    # 判定总体结果
    IF len(report.critical_issues) > 0:
        report.approved = false
    ELSE:
        report.approved = true
    
    RETURN report
```

---

## 5. 与其他模块的协作

| 交互方 | 方向 | 内容 |
|-------|------|------|
| WP Generator → Safety Checker | 输入 | 完整航点序列 |
| Speed Profiler → Safety Checker | 输入 | 完整速度剖面 |
| ENC Reader → Safety Checker | 查询 | 全部六类查询接口 |
| Safety Checker → WP Generator | 反馈 | CRITICAL 问题 → 要求重新生成航线 |
| Safety Checker → Speed Profiler | 反馈 | 速度相关 CRITICAL 问题 → 要求重新生成速度剖面 |
| Safety Checker → Guidance Layer (L4) | 输出 | approved = true 时航线方可发布执行 |
| Safety Checker → Shore Link | 输出 | 安全报告发送至岸基存档 |

---

## 6. 测试场景

| 场景编号 | 描述 | 预期结果 |
|---------|------|---------|
| SAF-001 | 安全航线（所有指标合格） | approved = true，无 CRITICAL |
| SAF-002 | 航线穿越陆地 | CRITICAL：陆地穿越 |
| SAF-003 | 航线经过浅水（UKC 不足） | CRITICAL：UKC |
| SAF-004 | 航线经过沉船（距离不足） | CRITICAL：碍航物 |
| SAF-005 | 航线穿越禁航区 | CRITICAL：限制区域 |
| SAF-006 | 航线逆向穿越 TSS | CRITICAL：TSS 逆行 |
| SAF-007 | 转弯空间不足 | CRITICAL：转弯 |
| SAF-008 | 减速距离不足 | CRITICAL：减速 |
| SAF-009 | 限速区超速 | CRITICAL：限速 |
| SAF-010 | ZOC D 区域 | WARNING：数据质量 |

---

## v2.0 架构升级：提升为 Deterministic Checker 角色

### 架构变更说明

在 MASS_ADAS v2.0 防御性架构中，Safety Checker 从"L2 内部子模块"提升为"独立的右轨 Deterministic Checker 的 L2 层实例"。

**变更前（v1.0）：** Safety Checker 是 Voyage Planner 的内部校验器——WP Generator 生成航线后调用 Safety Checker 校验，二者在同一个决策路径中。

**变更后（v2.0）：** Safety Checker 成为独立的 Route Safety Gate，与 WP Generator 并行运行。WP Generator（Doer）生成航线方案，Route Safety Gate（Checker）独立校验。两者的校验结果必须一致才能通过。

### 新增接口

```
# 输入：来自 WP Generator 的候选航线（而非内部调用）
RouteCheckRequest:
    PlannedRoute candidate_route        # 候选航线
    string request_source               # "wp_generator" / "avoidance_planner" / "weather_routing"

# 输出：校验结果 + 否决时的最近合规替代方案
RouteCheckResponse:
    bool approved                       # 是否通过校验
    string[] violations                 # 违反的安全规则列表
    PlannedRoute nearest_compliant      # 否决时的最近合规替代航线（NEW）
    string rationale                    # 校验理由
```

### 否决后回退策略

当 Safety Checker 否决航线时，它不仅说"不行"，还必须输出"最近的合规方案"：

```
FUNCTION compute_nearest_compliant_route(original_route, violations):
    
    FOR EACH violation IN violations:
        IF violation.type == "shallow_water":
            # 将违规航段偏移到安全水深区域
            modified = offset_leg_to_safe_depth(original_route, violation.leg_index, min_depth)
        
        IF violation.type == "exclusion_zone":
            # 绕行禁区
            modified = reroute_around_exclusion(original_route, violation.zone)
        
        IF violation.type == "insufficient_ukc":
            # 调整到更深水域
            modified = adjust_to_deeper_water(original_route, violation.leg_index, required_ukc)
    
    # 再次校验修改后的航线
    IF validate(modified).approved:
        RETURN modified
    ELSE:
        RETURN NULL    # 无法自动找到合规方案——需要人工审查
```

### 与 Deterministic Checker 总体设计的关系

Safety Checker（Route Safety Gate）是四层 Checker 中的 L2 层实例。完整的 Checker 架构见《MASS_ADAS Deterministic Checker 技术设计文档》。

| Checker 层级 | 名称 | 本文档对应 |
|-------------|------|----------|
| L2 | Route Safety Gate | **本模块**（提升后） |
| L3 | COLREGs Compliance Checker | 见 COLREGs Engine 文档 |
| L4 | Corridor Guard | 见 CTE Calculator 文档 |
| L5 | Actuator Envelope Checker | 见 Actuator Limiter 文档 |

---

## v3.1 补充升级：靠泊路径规划与安全校验

### A. 靠泊路径规划器（Berth Planner，挂在 L2）

当前 L2 WP Generator 生成的航路到达"港口入口附近"就结束了。从港口入口到具体泊位的最后 500m~1km——包括进入航道、转向、减速、侧向接近——缺少路径规划。

```
FUNCTION plan_berthing_path(port_entry_wp, berth_definition, enc_data, traffic):
    
    # 输入：
    #   port_entry_wp: 最后一个海上航路点（港口入口处）
    #   berth_definition: 泊位位置、朝向、长度、进近方向
    #   enc_data: 港内海图（水深、航道、禁区）
    #   traffic: 当前港内交通（系泊船位置、航标）
    
    # 靠泊路径分三段：
    
    # 段 1：进港航道段（从港口入口到泊位前方转向点）
    channel_path = generate_channel_path(port_entry_wp, berth_definition.approach_start)
    channel_speed = min(port_speed_limit, 5.0)    # 港内限速，通常 5 节
    
    # 段 2：转向对齐段（从航道转到泊位进近航向）
    turn_point = berth_definition.approach_start
    align_path = generate_turn_path(
        from_heading = channel_path.final_heading,
        to_heading = berth_definition.approach_heading,
        turn_radius = ship_params.min_turn_radius_low_speed    # 低速旋回半径
    )
    align_speed = 2.0    # ~4 节
    
    # 段 3：最终进近段（正对泊位面，直线接近）
    approach_path = generate_straight_path(
        start = align_path.end_point,
        end = berth_definition.contact_point,
        length = berth_definition.approach_length    # 通常 100~200m
    )
    approach_speed_profile = [
        (100, 1.5),    # 100m 处 1.5 m/s
        (50, 0.8),     # 50m 处 0.8 m/s
        (20, 0.3),     # 20m 处 0.3 m/s
        (5, 0.1),      # 5m 处 0.1 m/s
        (1, 0.05)      # 1m 处 0.05 m/s
    ]
    
    # 安全校验——全路径不穿越浅水/禁区/系泊船
    safety_ok = verify_berth_path_safety(
        channel_path + align_path + approach_path, 
        enc_data, traffic
    )
    
    IF NOT safety_ok:
        # 生成替代方案（不同的进近方向或不同的转向点）
        RETURN generate_alternative_berth_path(...)
    
    RETURN BerthPlan(
        channel_path=channel_path,
        align_path=align_path,
        approach_path=approach_path,
        speed_profiles=[channel_speed, align_speed, approach_speed_profile]
    )
```

### B. 靠泊路径的安全校验

```
FUNCTION verify_berth_path_safety(full_path, enc_data, traffic):
    
    checks = []
    
    # UKC 检查（港内可能浅于航行海域）
    FOR EACH point IN full_path:
        depth = enc_data.get_depth(point)
        ukc = depth - ship_params.draft - tide_correction
        IF ukc < BERTH_UKC_MIN:
            checks.append(("UKC", False, f"UKC={ukc:.1f}m at {point}"))
    
    # 走廊宽度检查（港内航道比海上窄）
    FOR EACH segment IN full_path.segments:
        corridor_width = compute_corridor_width(segment, enc_data.channel_edges)
        IF corridor_width < ship_params.beam × 2:    # 至少 2 倍船宽
            checks.append(("corridor", False, f"width={corridor_width:.0f}m"))
    
    # 系泊船避让（检查路径不穿过已系泊船舶的安全区域）
    FOR EACH moored_vessel IN traffic.moored_vessels:
        clearance = min_clearance(full_path, moored_vessel)
        IF clearance < ship_params.beam × 1.5:
            checks.append(("moored", False, f"clearance={clearance:.0f}m"))
    
    RETURN all(ok for _, ok, _ in checks)

BERTH_UKC_MIN = 1.5    # 米——港内最小 UKC（比航行时更严格）
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：提升为 Deterministic Checker L2 实例 |
| 0.3 | 2026-04-26 | 架构组 | v3.1 补充：增加靠泊路径规划器（三段式路径 + 减速剖面 + 安全校验）；支持从港口入口到泊位的完整路径生成 |

---

**文档结束**
