# MASS_ADAS Risk Monitor 风险监控器技术设计文档

**文档编号：** SANGO-ADAS-RSK-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. 核心参数数据库
4. 船长的持续监控思维模型
5. 状态机设计
6. 计算流程总览
7. 常态监控模式（MONITORING）
8. 避碰效果评估（AVOIDING）
9. 态势改善判定逻辑
10. 态势恶化判定与升级逻辑（ESCALATING）
11. 紧急态势处理（EMERGENCY）
12. 态势解除判定（RESOLVED）
13. 恢复阶段监控（RECOVERING）
14. 多目标联合监控
15. 目标行为异常响应
16. 传感器降级下的风险监控
17. 通信中断下的自主降级
18. 岸基报告与人工介入请求
19. 历史态势学习与统计
20. 内部子模块分解
21. 数值算例
22. 参数总览表
23. 与其他模块的协作关系
24. 测试策略与验收标准
25. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Risk Monitor（风险监控器）是 Layer 3（Tactical Layer）的闭环监控模块，也是整个避碰决策链的最后一道安全阀。它在避碰行动的全生命周期中持续运行——从常态监控到避碰执行、效果评估、态势解除、回归原航线，每一个阶段都在 Risk Monitor 的监控之下。

如果说 Target Tracker 是"眼睛"，COLREGs Engine 是"大脑"，Avoidance Planner 是"双手"，那么 Risk Monitor 就是"持续盯着的第二双眼睛"——它不参与决策制定，但它持续验证决策的执行效果，在效果不佳时触发升级，在态势解除时授权恢复。

### 1.2 核心职责

- **常态风险扫描：** 在无避碰行动时，持续监控所有跟踪目标的威胁趋势，及时发现威胁升级。
- **避碰效果实时评估：** 避碰行动执行期间，持续验证 CPA 是否在改善、目标行为是否符合预期。
- **态势升级触发：** 当避碰效果不佳或目标行为突变时，触发升级机制——要求加大避让幅度或切换到紧急模式。
- **态势解除判定：** 当碰撞风险完全消除时，发出"态势解除"信号，授权 Avoidance Planner 启动回归程序。
- **恢复阶段监控：** 在回归原航线过程中，持续监控是否有新的威胁出现。
- **全程记录与报告：** 记录每次避碰事件的完整时间线，生成结构化报告供岸基和船级社审查。

### 1.3 设计原则

- **持续在线原则：** Risk Monitor 在任何状态下都不停止运行。即使在 MONITORING（常态）状态下，它仍在评估每个目标的威胁趋势。
- **保守判定原则：** 态势升级的条件宽松（宁可多升级），态势解除的条件严格（确保完全安全才解除）。
- **独立评估原则：** Risk Monitor 独立于 COLREGs Engine 进行效果评估——它不信任 COLREGs Engine 的预测，而是基于实际观测的 CPA/TCPA 变化做判断。
- **不越权原则：** Risk Monitor 不直接修改避让方案——它只发出升级/解除信号，由 COLREGs Engine 和 Avoidance Planner 执行具体的方案调整。唯一的例外是 EMERGENCY 状态下的直接紧急指令。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 话题/接口 | 频率 | 说明 |
|-------|------|----------|------|------|
| 威胁目标列表 | Target Tracker | ThreatList | 2 Hz | 含 CPA/TCPA/威胁等级 |
| 当前避碰决策 | COLREGs Engine | ColregsDecision[] | 2 Hz | 当前的会遇分类和责任判定 |
| 当前避让指令 | Avoidance Planner | AvoidanceCommand | 2 Hz | 当前生效的避让航点和速度 |
| 本船状态 | Nav Filter | /nav/odom | 50 Hz | 位置、航向、航速 |
| 环境态势 | Perception | EnvironmentState | 0.2 Hz | 能见度、水域类型 |
| 系统健康状态 | System Monitor | /system/health | 1 Hz | 传感器和执行机构状态 |

### 2.2 输出

**主输出：风险监控状态和事件通知**

```
RiskMonitorStatus:
    Header header
    string current_state                    # 状态机当前状态
    RiskEvent[] active_events               # 当前活跃的风险事件
    uint32 active_avoidance_count           # 当前进行中的避碰行动数
    string overall_risk_level               # "NOMINAL" / "ELEVATED" / "HIGH" / "CRITICAL"

RiskEvent:
    string event_id                         # 唯一事件 ID
    string event_type                       # "avoidance_started" / "effectiveness_warning" / "escalation_triggered" / "situation_resolved" / "recovery_started" / "recovery_complete" / "emergency_triggered" / "target_anomaly"
    Time timestamp                          # 事件发生时间
    uint32 target_track_id                  # 相关目标 ID
    uint32 target_mmsi                      # 相关目标 MMSI
    string details                          # 事件详情
    string state_before                     # 事件前的状态
    string state_after                      # 事件后的状态
```

**控制信号输出：**

| 信号 | 接收方 | 话题 | 触发条件 |
|------|-------|------|---------|
| 态势解除通知 | Avoidance Planner | /tactical/risk/situation_resolved | 所有解除条件满足 |
| 升级请求 | COLREGs Engine | /tactical/risk/escalation_request | 效果不佳需加大幅度 |
| 紧急指令 | Control Layer (L5) | /system/emergency_maneuver | EMERGENCY 状态（跨层） |
| 岸基报告 | Shore Link | /tactical/risk/shore_report | 任何升级或异常事件 |

---

## 3. 核心参数数据库

### 3.1 效果评估参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| CPA 改善检测周期 | T_improve_check | 30 秒 | 每 30 秒检查一次 CPA 是否在改善 |
| CPA 改善最低要求 | ΔCPA_min_improve | 50 米/30秒 | 30 秒内 CPA 至少增大 50 米 |
| CPA 停滞容忍时间 | T_stagnation_tolerance | 60 秒 | CPA 停止改善后的容忍时间 |
| CPA 恶化容忍时间 | T_deterioration_tolerance | 15 秒 | CPA 开始恶化后的容忍时间 |
| 目标预测偏差阈值 | D_prediction_error_max | 200 米 | 目标实际位置与 1 分钟前预测的偏差上限 |
| 效果评估平滑窗口 | T_smooth_window | 15 秒 | CPA 趋势评估的滑动窗口 |

### 3.2 态势升级参数

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| 升级 TCPA 阈值 T_escalate | 120s (2min) | 90s (1.5min) | 60s (1min) | 45s |
| 升级 CPA 阈值（占 CPA_safe 比例） | < 0.8 | < 0.8 | < 0.7 | < 0.6 |
| 紧急 TCPA 阈值 T_emergency | 60s (1min) | 45s | 30s | 20s |
| 紧急 CPA 阈值（占 CPA_safe 比例） | < 0.5 | < 0.4 | < 0.3 | < 0.3 |

### 3.3 态势解除参数

| 参数 | 符号 | 默认值 | 说明 |
|------|------|-------|------|
| 解除所需最小 CPA | CPA_resolved | CPA_safe × 1.0 | CPA 恢复到至少等于安全距离 |
| 解除所需 TCPA 条件 | TCPA_resolved | < 0（已过） | CPA 已过最近点 |
| 解除所需距离条件 | Range_resolved | > CPA_safe × 2.0 | 目标已在 2 倍安全距离外 |
| 解除确认持续时间 | T_resolve_confirm | 60 秒 | 以上条件需持续满足 60 秒 |
| 解除后监控窗口 | T_post_resolve_watch | 120 秒 | 解除后继续关注该目标 2 分钟 |
| 威胁评分解除阈值 | Score_resolved | < 20 | threat_score 降至 NONE |

### 3.4 恢复阶段参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 恢复阶段新威胁扫描半径 | 3 × CPA_safe | 回归路径周围的安全扫描范围 |
| 恢复阶段最大速度 | 0.9 × V_plan | 回归时速度不超过原计划的 90% |
| 恢复完成判定距离 | 50 米 | 距原航线 < 50m 视为回归完成 |
| 恢复完成航向偏差 | 5° | 航向与原航线偏差 < 5° |

---

## 4. 船长的持续监控思维模型

### 4.1 船长在避碰过程中怎么做"持续监控"

一个经验丰富的船长在下达避让指令（比如"右舵 20"）后，他不会转身离开雷达屏幕。他会盯着雷达看，持续观察以下几个关键指标：

**第一看：方位在变吗？** 避让动作生效后，目标的相对方位应该开始变化。如果打了舵 30 秒了方位还没有变化，说明避让幅度不够或者对方也在转向。

**第二看：距离在增还是在减？** 即使方位在变，如果距离还在持续缩小，说明 CPA 时刻还没到来。只有当距离开始增大（range_rate 从负变正），才意味着最近点已过。

**第三看：CPA 数值在改善吗？** ARPA 雷达会持续更新 CPA 预测值。避让后 CPA 应该在增大。如果 CPA 反而在减小，有两种可能：一是我的避让幅度不够，二是对方也在做某种动作（可能是对方也在避让，但方向不对）。

**第四看：对方在干什么？** 对方的航向、速度有没有变化？如果对方突然转向了，之前的 CPA 预测全部失效，需要重新评估。

**第五看：有没有新的目标出现？** 避让过程中我偏离了原航线，可能进入了一个之前不在航线上的区域。这个区域可能有其他船只。

### 4.2 船长的决策时间线

船长在避碰过程中有一个心理时间线：

```
0s        打舵，开始转向
~15s      船开始明显偏转（舵效建立）
~30s      第一次检查：方位开始变了吗？
~60s      第二次检查：CPA 在改善吗？距离变化率呢？
~120s     如果还没改善——开始紧张。是不是要加大力度？
~180s     如果 3 分钟了 CPA 还在恶化——必须升级行动
~300s     目标通过最近点后——开始考虑恢复
~480s     确认安全后——开始回归原航线
```

Risk Monitor 的设计就是要精确复制这条时间线上的每一个检查点和判断逻辑。

### 4.3 船长最担心的事

在整个避碰过程中，船长最担心的三件事：

1. **对方不按规矩来。** 对方应该保持航向（直航船）却突然转向，或者应该让路却不动。这时候所有的 CPA 预测都失效了。
2. **避让进入了新的危险。** 为了避开一艘船而进入了另一艘船的航路，或者进入了浅水区。
3. **目标突然消失。** 雷达信号丢失、AIS 停止报告——目标还在吗？在哪里？去哪了？

Risk Monitor 必须对这三种情况有明确的处理逻辑。

---

## 5. 状态机设计

### 5.1 状态定义

Risk Monitor 使用有限状态机管理避碰过程的生命周期：

```
┌──────────────┐
│  MONITORING  │ ← 常态，无避碰行动
│  常态监控     │
└──────┬───────┘
       │ Target Tracker 报告 threat_level ≥ HIGH
       │ 且 COLREGs Engine 发出 action_required = true
       ▼
┌──────────────┐
│   AVOIDING   │ ← 避碰行动执行中
│   避碰执行    │
└──┬─────┬─────┘
   │     │
   │     │ 效果不佳 + TCPA < T_escalate
   │     ▼
   │  ┌──────────────┐
   │  │  ESCALATING  │ ← 避碰效果不佳，升级行动
   │  │  升级中       │
   │  └──┬─────┬─────┘
   │     │     │
   │     │     │ TCPA < T_emergency + CPA < CPA_safe × 0.5
   │     │     ▼
   │     │  ┌──────────────┐
   │     │  │  EMERGENCY   │ ← 紧急碰撞风险
   │     │  │  紧急状态     │ → 直接指令 L5 (跨层)
   │     │  └──────────────┘
   │     │
   │     │ 态势解除条件满足
   │     ▼
   │  ┌──────────────┐
   ├─→│  RESOLVED    │ ← 态势解除，准备恢复
   │  │  态势解除     │
   │  └──────┬───────┘
   │         │ 通知 Avoidance Planner 启动回归
   │         ▼
   │  ┌──────────────┐
   │  │  RECOVERING  │ ← 回归原航线中
   │  │  恢复中       │
   │  └──────┬───────┘
   │         │ 回归完成
   │         ▼
   └────→ MONITORING（回到常态）
```

### 5.2 状态转换事件

| 当前状态 | 事件 | 下一状态 | 附带动作 |
|---------|------|---------|---------|
| MONITORING | threat_level ≥ HIGH + action_required | AVOIDING | 开始效果评估计时 |
| AVOIDING | 效果评估：CPA 持续改善 | AVOIDING | 继续监控 |
| AVOIDING | 效果评估：CPA 停滞或恶化 + TCPA < T_escalate | ESCALATING | 发送升级请求 |
| AVOIDING | 所有解除条件满足 + 持续 T_resolve_confirm | RESOLVED | 发送态势解除通知 |
| ESCALATING | 加大避让后效果改善 | AVOIDING | 降级回正常避碰 |
| ESCALATING | TCPA < T_emergency + CPA < 0.5 × CPA_safe | EMERGENCY | 发送紧急指令 |
| ESCALATING | 所有解除条件满足 | RESOLVED | 发送态势解除通知 |
| EMERGENCY | 碰撞风险解除 | RESOLVED | 发送态势解除 + 紧急报告 |
| RESOLVED | Avoidance Planner 确认启动恢复 | RECOVERING | 开始恢复阶段监控 |
| RECOVERING | 回归完成判定 | MONITORING | 记录事件完成 |
| RECOVERING | 新威胁出现 | AVOIDING | 中断恢复，启动新避碰 |
| 任意状态 | 目标 track_status 变为 LOST | 特殊处理 | 见第 16 节 |

### 5.3 每目标独立状态机

Risk Monitor 为每个被避让的目标维护一个独立的状态机实例。多目标同时避让时，各目标的状态可以不同——比如目标 A 已经 RESOLVED 但目标 B 仍在 AVOIDING。

```
struct AvoidanceTracker {
    uint32 target_track_id;
    string current_state;           # 状态机当前状态
    
    # 效果评估历史
    deque<CpaRecord> cpa_history;   # CPA 历史记录（每 2 秒一条）
    float64 cpa_at_avoidance_start; # 避碰开始时的 CPA
    float64 tcpa_at_avoidance_start; # 避碰开始时的 TCPA
    Time avoidance_start_time;      # 避碰开始时间
    
    # 解除判定计时
    float64 resolve_condition_duration; # 解除条件已持续满足的时间
    
    # 升级记录
    uint32 escalation_count;        # 已升级次数
    Time last_escalation_time;      # 上次升级时间
    
    # 异常记录
    uint32 anomaly_count;           # 目标异常行为次数
    string last_anomaly_type;       # 最近一次异常类型
};

struct CpaRecord {
    Time timestamp;
    float64 cpa;
    float64 tcpa;
    float64 range;
    float64 range_rate;
    float64 bearing_rate;
};
```

---

## 6. 计算流程总览

Risk Monitor 在每个决策周期（2 秒）执行以下流程：

```
输入：ThreatList + AvoidanceCommand + 本船状态
          │
          ▼
    ┌──────────────────────────────────┐
    │ 1. 更新所有 AvoidanceTracker      │
    │    记录最新 CPA/TCPA/距离数据     │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 2. 按当前状态执行对应逻辑         │
    │    MONITORING → 第 7 节           │
    │    AVOIDING   → 第 8 节           │
    │    ESCALATING → 第 10 节          │
    │    EMERGENCY  → 第 11 节          │
    │    RESOLVED   → 第 12 节          │
    │    RECOVERING → 第 13 节          │
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 3. 检查目标行为异常               │ → 第 15 节
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 4. 检查传感器健康状态             │ → 第 16 节
    └────────────┬─────────────────────┘
                 │
                 ▼
    ┌──────────────────────────────────┐
    │ 5. 发布 RiskMonitorStatus         │
    │    发送事件通知（如有状态变化）    │
    └──────────────────────────────────┘
```

---

## 7. 常态监控模式（MONITORING）

### 7.1 职责

在无避碰行动的常态下，Risk Monitor 持续扫描 Target Tracker 输出的威胁列表，关注以下趋势：

```
FUNCTION monitoring_cycle(threat_list, avoidance_trackers):
    
    FOR EACH target IN threat_list:
        
        # 检查是否有新的高威胁目标
        IF target.threat_level >= "HIGH" AND target.track_id NOT IN avoidance_trackers:
            # 新的高威胁——等待 COLREGs Engine 的决策
            log_event("new_high_threat", target)
        
        # 检查威胁趋势
        IF target.threat_trend == "INCREASING":
            # 威胁在持续增大——提前预警
            IF target.threat_score > EARLY_WARNING_THRESHOLD:
                publish_early_warning(target)
        
        # 检查是否有目标从低威胁突然跳升到高威胁
        IF target.threat_level >= "HIGH" AND target.previous_threat_level <= "LOW":
            log_event("threat_jump", target, {
                from: target.previous_threat_level,
                to: target.threat_level,
                reason: "目标威胁等级快速升高"
            })
    
    # 检查是否有目标从 AVOIDING/ESCALATING 等状态的目标已被 Target Tracker 丢失
    FOR EACH tracker IN avoidance_trackers:
        IF tracker.current_state != "MONITORING":
            target = find_target_by_id(threat_list, tracker.target_track_id)
            IF target IS NULL:
                handle_target_lost_during_avoidance(tracker)

EARLY_WARNING_THRESHOLD = 50    # threat_score > 50 时发出早期预警
```

### 7.2 状态进入 AVOIDING

```
FUNCTION transition_to_avoiding(target, colregs_decision):
    
    tracker = create_or_get_tracker(target.track_id)
    tracker.current_state = "AVOIDING"
    tracker.cpa_at_avoidance_start = target.cpa
    tracker.tcpa_at_avoidance_start = target.tcpa
    tracker.avoidance_start_time = now()
    tracker.escalation_count = 0
    tracker.cpa_history.clear()
    
    # 记录初始状态
    tracker.cpa_history.append(CpaRecord(
        timestamp: now(),
        cpa: target.cpa,
        tcpa: target.tcpa,
        range: target.range,
        range_rate: target.range_rate,
        bearing_rate: target.bearing_rate
    ))
    
    log_event("avoidance_started", target, {
        initial_cpa: target.cpa,
        initial_tcpa: target.tcpa,
        encounter_type: colregs_decision.encounter_type,
        my_role: colregs_decision.my_role
    })
```

---

## 8. 避碰效果评估（AVOIDING）

### 8.1 核心评估逻辑

在 AVOIDING 状态下，每 2 秒执行一次效果评估：

```
FUNCTION evaluate_avoidance_effectiveness(tracker, target):
    
    # 1. 记录最新数据点
    tracker.cpa_history.append(CpaRecord(
        timestamp: now(),
        cpa: target.cpa,
        tcpa: target.tcpa,
        range: target.range,
        range_rate: target.range_rate,
        bearing_rate: target.bearing_rate
    ))
    
    # 保持历史长度在合理范围内（最近 5 分钟）
    WHILE tracker.cpa_history.front().timestamp < now() - 300s:
        tracker.cpa_history.pop_front()
    
    # 2. 计算 CPA 趋势
    cpa_trend = compute_cpa_trend(tracker)
    
    # 3. 基于趋势做判断
    IF cpa_trend == "IMPROVING":
        # CPA 在持续改善——避碰有效
        tracker.resolve_condition_duration = 0    # 重置解除计时
        RETURN {status: "effective", details: "CPA 持续改善中"}
    
    ELIF cpa_trend == "STAGNANT":
        # CPA 停滞不前——可能需要加大力度
        time_since_start = now() - tracker.avoidance_start_time
        
        IF time_since_start > T_STAGNATION_TOLERANCE:
            RETURN {status: "stagnant", details: f"CPA 停滞 {time_since_start:.0f}s，超过容忍时间"}
        ELSE:
            RETURN {status: "monitoring", details: "CPA 暂时停滞，继续观察"}
    
    ELIF cpa_trend == "DETERIORATING":
        # CPA 在恶化——避碰效果不佳！
        deterioration_duration = compute_deterioration_duration(tracker)
        
        IF deterioration_duration > T_DETERIORATION_TOLERANCE:
            RETURN {status: "deteriorating", details: f"CPA 持续恶化 {deterioration_duration:.0f}s"}
        ELSE:
            RETURN {status: "monitoring", details: "CPA 短暂恶化，可能是数据波动"}
    
    # 4. 检查是否满足升级条件
    IF should_escalate(tracker, target):
        RETURN {status: "escalate_needed"}
    
    # 5. 检查是否满足解除条件
    IF should_resolve(tracker, target):
        RETURN {status: "resolve_ready"}
```

### 8.2 CPA 趋势计算

```
FUNCTION compute_cpa_trend(tracker):
    
    history = tracker.cpa_history
    
    IF len(history) < 5:
        RETURN "INSUFFICIENT_DATA"
    
    # 使用最近 T_smooth_window 内的数据
    recent = [r for r in history if r.timestamp > now() - T_SMOOTH_WINDOW]
    
    IF len(recent) < 3:
        RETURN "INSUFFICIENT_DATA"
    
    # 计算 CPA 的线性趋势（最小二乘法）
    times = [r.timestamp - recent[0].timestamp for r in recent]
    cpas = [r.cpa for r in recent]
    
    slope = linear_regression_slope(times, cpas)
    
    # slope > 0 表示 CPA 在增大（改善）
    # slope < 0 表示 CPA 在减小（恶化）
    # slope ≈ 0 表示 CPA 停滞
    
    IF slope > CPA_IMPROVE_THRESHOLD:
        RETURN "IMPROVING"
    ELIF slope < -CPA_DETERIORATE_THRESHOLD:
        RETURN "DETERIORATING"
    ELSE:
        RETURN "STAGNANT"

CPA_IMPROVE_THRESHOLD = 1.0      # CPA 每秒增大 > 1m 视为改善
CPA_DETERIORATE_THRESHOLD = 0.5  # CPA 每秒减小 > 0.5m 视为恶化
T_SMOOTH_WINDOW = 15             # 秒
```

### 8.3 辅助判定指标

除了 CPA 趋势外，以下辅助指标帮助更准确地评估效果：

```
FUNCTION compute_auxiliary_indicators(tracker, target):
    
    indicators = {}
    
    # 1. 距离变化率趋势
    # range_rate < 0 表示接近中，> 0 表示远离中
    indicators.range_rate = target.range_rate
    indicators.range_rate_improving = (target.range_rate > tracker.cpa_history[-10].range_rate)
    
    # 2. 方位变化率
    # 方位在变化说明避让在生效
    indicators.bearing_rate = target.bearing_rate
    indicators.bearing_changing = abs(target.bearing_rate) > 0.05    # > 0.05°/s
    
    # 3. 目标行为是否可预测
    predicted_pos = tracker.prediction_1min_ago     # 1 分钟前做的预测
    actual_pos = target.position
    prediction_error = geo_distance(predicted_pos, actual_pos)
    indicators.target_predictable = (prediction_error < D_PREDICTION_ERROR_MAX)
    
    # 4. CPA 比初始值改善了多少
    cpa_improvement_ratio = target.cpa / tracker.cpa_at_avoidance_start
    indicators.cpa_improvement_ratio = cpa_improvement_ratio
    # ratio > 1 表示改善，< 1 表示恶化
    
    RETURN indicators
```

---

## 9. 态势改善判定逻辑

### 9.1 改善判定

```
FUNCTION is_situation_improving(tracker, target, indicators):
    
    # 综合多个指标判定态势是否在改善
    
    improving_signals = 0
    total_signals = 4
    
    # 信号 1：CPA 趋势正向
    IF compute_cpa_trend(tracker) == "IMPROVING":
        improving_signals += 1
    
    # 信号 2：距离变化率在改善（从大幅接近变为缓慢接近或开始远离）
    IF indicators.range_rate_improving:
        improving_signals += 1
    
    # 信号 3：方位在变化（避让在生效）
    IF indicators.bearing_changing:
        improving_signals += 1
    
    # 信号 4：CPA 比初始值大
    IF indicators.cpa_improvement_ratio > 1.0:
        improving_signals += 1
    
    # 至少 2 个信号为正才判定为改善
    RETURN improving_signals >= 2
```

### 9.2 改善确认后的行为

```
IF is_situation_improving(tracker, target, indicators):
    # 态势在改善——继续监控，不做任何动作
    # 但如果之前有升级请求，可以考虑降级
    
    IF tracker.current_state == "ESCALATING":
        IF time_since_last_escalation > 60 AND cpa_trend == "IMPROVING":
            # 升级后效果改善——降级回 AVOIDING
            tracker.current_state = "AVOIDING"
            log_event("deescalation", target, "升级后效果改善，降级回正常避碰监控")
```

---

## 10. 态势恶化判定与升级逻辑（ESCALATING）

### 10.1 升级触发条件

```
FUNCTION should_escalate(tracker, target):
    
    zone_type = get_current_zone_type()
    thresholds = get_escalation_thresholds(zone_type)
    
    # 条件组合（满足任一组合即触发升级）
    
    # 组合 A：CPA 恶化 + TCPA 短
    IF compute_cpa_trend(tracker) == "DETERIORATING" AND target.tcpa < thresholds.T_escalate:
        RETURN true
    
    # 组合 B：CPA 停滞过长 + TCPA 短
    IF compute_cpa_trend(tracker) == "STAGNANT":
        stagnation_time = compute_stagnation_duration(tracker)
        IF stagnation_time > T_STAGNATION_TOLERANCE AND target.tcpa < thresholds.T_escalate × 1.5:
            RETURN true
    
    # 组合 C：CPA 低于安全距离的危险比例 + TCPA 在合理范围内
    IF target.cpa < CPA_safe × thresholds.escalation_cpa_ratio AND target.tcpa > 0 AND target.tcpa < thresholds.T_escalate × 2:
        RETURN true
    
    # 组合 D：目标行为不可预测 + CPA 未改善
    IF target.behavior_unpredictable AND NOT is_situation_improving(tracker, target, ...):
        RETURN true
    
    RETURN false
```

### 10.2 升级执行

```
FUNCTION execute_escalation(tracker, target):
    
    tracker.escalation_count += 1
    tracker.last_escalation_time = now()
    tracker.current_state = "ESCALATING"
    
    # 确定升级幅度
    IF tracker.escalation_count == 1:
        # 第一次升级：增大转向角度 15° 或减速 2 m/s
        escalation = {
            additional_course_change: 15,    # 度
            additional_speed_reduction: 2.0, # m/s
            rationale: "第一次升级：CPA 效果不佳"
        }
    ELIF tracker.escalation_count == 2:
        # 第二次升级：增大转向角度 30° 或减速 4 m/s
        escalation = {
            additional_course_change: 30,
            additional_speed_reduction: 4.0,
            rationale: "第二次升级：效果仍不佳"
        }
    ELSE:
        # 第三次及以上：准备紧急模式
        escalation = {
            action: "prepare_emergency",
            rationale: f"第 {tracker.escalation_count} 次升级：考虑切换到紧急模式"
        }
    
    # 发送升级请求给 COLREGs Engine
    publish_escalation_request(target.track_id, escalation)
    
    # 通知岸基
    report_to_shore("escalation", target, escalation)
    
    log_event("escalation_triggered", target, escalation)
```

### 10.3 升级限制

```
# 防止过度频繁升级
MIN_ESCALATION_INTERVAL = 30    # 秒，两次升级之间至少间隔 30 秒
MAX_ESCALATION_COUNT = 3        # 最多升级 3 次，之后直接进入 EMERGENCY

IF tracker.escalation_count >= MAX_ESCALATION_COUNT:
    transition_to_emergency(tracker, target)
```

---

## 11. 紧急态势处理（EMERGENCY）

### 11.1 紧急状态进入条件

```
FUNCTION should_enter_emergency(tracker, target):
    
    thresholds = get_emergency_thresholds(zone_type)
    
    # 条件 A：TCPA 极短 + CPA 极小
    IF target.tcpa < thresholds.T_emergency AND target.cpa < CPA_safe × thresholds.emergency_cpa_ratio:
        RETURN true
    
    # 条件 B：距离极近 + 仍在接近
    IF target.range < CPA_safe × 0.5 AND target.range_rate < -1.0:    # 仍在以 > 1 m/s 的速率接近
        RETURN true
    
    # 条件 C：升级次数耗尽
    IF tracker.escalation_count >= MAX_ESCALATION_COUNT:
        RETURN true
    
    RETURN false
```

### 11.2 紧急状态行动

```
FUNCTION handle_emergency(tracker, target):
    
    tracker.current_state = "EMERGENCY"
    
    # ---- 直接向 L5 发送紧急指令（唯一允许的跨层通信）----
    emergency_cmd = EmergencyManeuver()
    emergency_cmd.emergency_type = "collision_avoidance"
    emergency_cmd.override_guidance = true    # 绕过 Guidance 层
    
    # 选择紧急动作
    IF target.bearing_relative < 180:
        # 目标在右侧——紧急左满舵（Rule 2 安全覆盖，紧急情况下不受 Rule 8 方向偏好约束）
        # 但如果能见度不良，仍遵守 Rule 19 不向左转
        IF visibility < VISIBILITY_THRESHOLD AND target.bearing_relative < 90:
            emergency_cmd.action = "crash_stop"    # 无法向左转——紧急停船
        ELSE:
            emergency_cmd.action = "hard_port"
    ELSE:
        # 目标在左侧——紧急右满舵
        emergency_cmd.action = "hard_starboard"
    
    # 同时减速
    emergency_cmd.speed_action = "crash_stop"
    
    # 发布紧急指令
    emergency_publisher.publish(emergency_cmd)
    
    # 向岸基发送紧急报告
    report_to_shore("EMERGENCY", target, {
        action: emergency_cmd.action,
        cpa: target.cpa,
        tcpa: target.tcpa,
        range: target.range,
        position: own_ship.position
    })
    
    # 触发声光报警（如果有）
    trigger_alarm("collision_emergency")
    
    log_event("emergency_triggered", target, emergency_cmd)
```

### 11.3 紧急状态退出

```
FUNCTION check_emergency_exit(tracker, target):
    
    # 紧急状态退出条件非常严格
    IF target.range > CPA_safe AND target.range_rate > 0:
        # 距离在增大且已超过安全距离
        IF this_condition_sustained > 30s:
            tracker.current_state = "RESOLVED"
            log_event("emergency_resolved", target)
    
    IF target.tcpa < 0 AND target.range > CPA_safe × 0.5:
        # CPA 已过且距离恢复到安全水平
        tracker.current_state = "RESOLVED"
        log_event("emergency_resolved", target)
```

---

## 12. 态势解除判定（RESOLVED）

### 12.1 解除条件

态势解除的判定条件必须严格——过早解除可能导致回归时再次接近目标。

```
FUNCTION should_resolve(tracker, target):
    
    # 所有以下条件必须同时满足：
    
    conditions_met = true
    
    # 条件 1：CPA 已达到安全水平
    IF target.cpa < CPA_RESOLVED:
        conditions_met = false
    
    # 条件 2：CPA 已过（TCPA < 0）或目标已在安全距离外远离
    IF target.tcpa > 0 AND target.range < RANGE_RESOLVED:
        conditions_met = false
    
    # 条件 3：距离在增大（目标正在远离）
    IF target.range_rate < 0:    # 仍在接近
        conditions_met = false
    
    # 条件 4：威胁评分已降至 NONE
    IF target.threat_score >= SCORE_RESOLVED:
        conditions_met = false
    
    # 条件 5：以上条件持续满足至少 T_resolve_confirm 秒
    IF conditions_met:
        tracker.resolve_condition_duration += dt
        IF tracker.resolve_condition_duration >= T_RESOLVE_CONFIRM:
            RETURN true
    ELSE:
        tracker.resolve_condition_duration = 0    # 重置计时
    
    RETURN false
```

### 12.2 解除执行

```
FUNCTION execute_resolution(tracker, target):
    
    tracker.current_state = "RESOLVED"
    
    # 通知 Avoidance Planner 启动恢复程序
    resolve_notification = SituationResolved()
    resolve_notification.target_track_id = tracker.target_track_id
    resolve_notification.final_cpa = target.cpa
    resolve_notification.avoidance_duration_sec = now() - tracker.avoidance_start_time
    resolve_notification.escalation_count = tracker.escalation_count
    
    resolve_publisher.publish(resolve_notification)
    
    log_event("situation_resolved", target, {
        duration: resolve_notification.avoidance_duration_sec,
        initial_cpa: tracker.cpa_at_avoidance_start,
        final_cpa: target.cpa,
        escalations: tracker.escalation_count
    })
    
    # 但继续监控该目标一段时间（解除后监控窗口）
    tracker.post_resolve_watch_until = now() + T_POST_RESOLVE_WATCH
```

### 12.3 解除后的持续监控

```
FUNCTION post_resolve_monitoring(tracker, target):
    
    IF now() > tracker.post_resolve_watch_until:
        # 监控窗口结束，可以安全移除
        RETURN "watch_complete"
    
    # 在监控窗口内，如果目标又开始接近
    IF target.range_rate < -0.5 AND target.cpa < CPA_safe:
        # 目标回来了！重新进入 AVOIDING
        tracker.current_state = "AVOIDING"
        log_event("post_resolve_threat_return", target)
        RETURN "threat_returned"
    
    RETURN "watching"
```

---

## 13. 恢复阶段监控（RECOVERING）

### 13.1 恢复阶段的风险扫描

在回归原航线的过程中，船可能进入之前不在航路上的水域，可能遇到新的船舶交通。Risk Monitor 在 RECOVERING 状态下执行额外的安全扫描：

```
FUNCTION recovery_monitoring(own_ship, avoidance_cmd, threat_list):
    
    # 1. 检查回归路径上是否有新的威胁
    recovery_waypoints = avoidance_cmd.waypoints
    
    FOR EACH target IN threat_list:
        IF target.threat_level >= "MEDIUM":
            # 检查该目标是否在回归路径附近
            min_dist_to_recovery = min_distance_to_path(target.position, recovery_waypoints)
            
            IF min_dist_to_recovery < CPA_safe × 3:
                # 新威胁在回归路径附近——可能需要中断恢复
                log_event("new_threat_on_recovery_path", target)
                
                IF target.threat_level >= "HIGH":
                    # 中断恢复，启动新的避碰
                    RETURN {action: "abort_recovery", reason: "回归路径上发现新的高威胁目标"}
    
    # 2. 检查是否已回归到原航线
    distance_to_route = compute_distance_to_route(own_ship.position, original_route)
    heading_error = compute_heading_error(own_ship.heading, route_heading_at_nearest_point)
    
    IF distance_to_route < RECOVERY_COMPLETE_DISTANCE AND abs(heading_error) < RECOVERY_COMPLETE_HEADING:
        # 回归完成
        RETURN {action: "recovery_complete"}
    
    # 3. 检查回归是否超时
    IF now() - recovery_start_time > RECOVERY_TIMEOUT:
        log_event("recovery_timeout", {duration: now() - recovery_start_time})
        RETURN {action: "request_replan", reason: "回归超时，可能需要航线重规划"}
    
    RETURN {action: "continue_recovery"}

RECOVERY_COMPLETE_DISTANCE = 50    # 米
RECOVERY_COMPLETE_HEADING = 5      # 度
RECOVERY_TIMEOUT = 600             # 秒（10 分钟）
```

### 13.2 恢复完成

```
FUNCTION complete_recovery(tracker):
    
    tracker.current_state = "MONITORING"
    
    # 生成完整的避碰事件报告
    event_report = generate_avoidance_event_report(tracker)
    
    # 发布事件报告
    event_report_publisher.publish(event_report)
    
    # 通知岸基
    report_to_shore("avoidance_complete", event_report)
    
    log_event("recovery_complete", {
        total_duration: now() - tracker.avoidance_start_time,
        total_deviation_nm: compute_total_deviation(tracker),
        escalation_count: tracker.escalation_count,
        was_emergency: tracker.was_emergency
    })
    
    # 清理 tracker（保留一段时间用于统计）
    tracker.cleanup_scheduled_at = now() + 3600    # 1 小时后清理
```

---

## 14. 多目标联合监控

### 14.1 多目标态势的整体评估

当同时避让多个目标时，Risk Monitor 不仅独立监控每个目标，还需要做整体评估：

```
FUNCTION assess_multi_target_situation(trackers):
    
    active_trackers = [t for t in trackers if t.current_state IN ["AVOIDING", "ESCALATING"]]
    
    IF len(active_trackers) == 0:
        RETURN "nominal"
    
    # 整体风险等级
    max_risk = max(t.current_risk_level for t in active_trackers)
    
    # 检查是否有目标间的关联风险（避让一个导致接近另一个）
    FOR EACH pair (t1, t2) IN combinations(active_trackers, 2):
        target1 = find_target(t1.target_track_id)
        target2 = find_target(t2.target_track_id)
        
        # 如果两个目标的推荐避让方向相反——冲突风险
        IF t1.avoidance_direction != t2.avoidance_direction:
            overall_risk = "ELEVATED"
            log_event("multi_target_conflict_risk", {targets: [t1.id, t2.id]})
    
    # 整体解除条件：所有目标都必须满足解除条件
    all_resolved = all(t.current_state == "RESOLVED" for t in active_trackers)
    
    RETURN {
        overall_risk: max_risk,
        active_count: len(active_trackers),
        all_resolved: all_resolved,
        conflict_detected: conflict_detected
    }
```

### 14.2 联合解除判定

```
# 多目标时的解除规则：
# 不是某个目标解除就立即恢复——等所有目标都解除后才统一恢复
# 除非某个已解除目标的恢复方向不与未解除目标的避让方向冲突

FUNCTION should_start_recovery_multi(trackers):
    
    all_resolved = all(t.current_state IN ["RESOLVED", "MONITORING"] for t in active_trackers)
    
    IF all_resolved:
        RETURN true
    
    # 部分解除的情况：检查恢复方向是否安全
    resolved = [t for t in trackers if t.current_state == "RESOLVED"]
    still_active = [t for t in trackers if t.current_state IN ["AVOIDING", "ESCALATING"]]
    
    # 如果已解除目标的恢复方向不会恶化未解除目标的态势，可以开始部分恢复
    # 但这增加了复杂性——建议 V1 版本等全部解除后再恢复
    
    RETURN false
```

---

## 15. 目标行为异常响应

### 15.1 异常类型与检测

```
FUNCTION detect_target_anomaly(tracker, target):
    
    anomalies = []
    
    # 异常 1：目标突然转向（> 30° 在 30 秒内）
    IF target.cog_change_30s > 30:
        anomalies.append({
            type: "sudden_turn",
            severity: "HIGH",
            details: f"目标航向 30s 内变化 {target.cog_change_30s:.0f}°"
        })
    
    # 异常 2：目标突然加速或减速（> 50% 变化）
    IF abs(target.sog_change_ratio_30s) > 0.5:
        anomalies.append({
            type: "sudden_speed_change",
            severity: "HIGH",
            details: f"目标速度 30s 内变化 {target.sog_change_ratio_30s*100:.0f}%"
        })
    
    # 异常 3：目标信号品质突然下降
    IF target.confidence < 0.3 AND tracker.previous_confidence > 0.7:
        anomalies.append({
            type: "confidence_drop",
            severity: "MEDIUM",
            details: f"目标置信度从 {tracker.previous_confidence:.2f} 降至 {target.confidence:.2f}"
        })
    
    # 异常 4：AIS 与雷达数据分离
    IF target.ais_radar_divergence > 500:    # 米
        anomalies.append({
            type: "sensor_divergence",
            severity: "MEDIUM",
            details: f"AIS 与雷达位置偏差 {target.ais_radar_divergence:.0f}m"
        })
    
    # 异常 5：目标从预测位置大幅偏离
    IF tracker.prediction_error > D_PREDICTION_ERROR_MAX:
        anomalies.append({
            type: "prediction_deviation",
            severity: "HIGH",
            details: f"目标偏离预测位置 {tracker.prediction_error:.0f}m"
        })
    
    RETURN anomalies
```

### 15.2 异常响应

```
FUNCTION respond_to_anomaly(tracker, target, anomalies):
    
    FOR EACH anomaly IN anomalies:
        tracker.anomaly_count += 1
        tracker.last_anomaly_type = anomaly.type
        
        log_event("target_anomaly", target, anomaly)
        
        IF anomaly.severity == "HIGH":
            # 高严重度异常——所有 CPA/TCPA 预测可能失效
            # 强制 COLREGs Engine 重新评估
            publish_reevaluation_request(target.track_id, anomaly)
            
            # 如果当前在 AVOIDING 状态，可能需要升级
            IF tracker.current_state == "AVOIDING":
                IF tracker.anomaly_count >= 2:    # 多次异常
                    execute_escalation(tracker, target)
    
    # 如果目标持续不可预测（3 分钟内 ≥ 3 次异常），切换到最保守模式
    IF tracker.anomaly_count >= 3 AND (now() - tracker.first_anomaly_time) < 180:
        publish_escalation_request(target.track_id, {
            reason: "目标行为持续不可预测",
            recommendation: "切换到最大避让幅度或停船"
        })
```

---

## 16. 传感器降级下的风险监控

### 16.1 传感器健康状态监控

```
FUNCTION check_sensor_health(system_health):
    
    degradation = {}
    
    # 检查雷达
    IF system_health.radar.status != "OK":
        degradation["radar"] = system_health.radar.status
        # 雷达故障——目标跟踪精度下降
        # 增大所有 CPA 安全距离 30%
        CPA_safe_adjusted = CPA_safe × 1.3
    
    # 检查 AIS
    IF system_health.ais.status != "OK":
        degradation["ais"] = system_health.ais.status
        # AIS 故障——丢失目标身份信息
        # 无法判定目标类型（Rule 18），按最保守处理
    
    # 检查 GPS
    IF system_health.gnss.status != "OK":
        degradation["gnss"] = system_health.gnss.status
        # GPS 故障——本船位置不确定性增大
        # 增大安全余量
    
    # 检查摄像头
    IF system_health.camera.status != "OK":
        degradation["camera"] = system_health.camera.status
        # 摄像头故障——目标分类能力下降
    
    IF len(degradation) > 0:
        log_event("sensor_degradation", degradation)
        apply_degraded_mode_parameters(degradation)
        report_to_shore("sensor_degradation", degradation)
    
    RETURN degradation
```

### 16.2 降级模式参数调整

```
FUNCTION apply_degraded_mode_parameters(degradation):
    
    # 基本原则：传感器越少，安全余量越大
    
    sensor_count = count_operational_sensors()
    
    IF sensor_count >= 4:    # 全部正常
        safety_multiplier = 1.0
    ELIF sensor_count == 3:
        safety_multiplier = 1.2    # 增大 20%
    ELIF sensor_count == 2:
        safety_multiplier = 1.5    # 增大 50%
    ELIF sensor_count == 1:
        safety_multiplier = 2.0    # 增大 100%
        # 同时降速至 50% 原航速
        publish_speed_reduction_advisory(0.5)
    ELSE:
        # 所有传感器失效——紧急停船
        trigger_emergency_stop("all_sensors_failed")
    
    # 调整全局安全参数
    CPA_safe_effective = CPA_safe × safety_multiplier
    T_give_way_effective = T_give_way × safety_multiplier
```

---

## 17. 通信中断下的自主降级

### 17.1 岸基通信中断检测

```
FUNCTION check_shore_link(system_health):
    
    IF system_health.shore_link.status == "DISCONNECTED":
        comms_lost_duration = now() - system_health.shore_link.last_contact
        
        IF comms_lost_duration > COMMS_LOSS_THRESHOLD_1:    # > 5 分钟
            # 一级降级：增大安全余量，降低航速
            apply_comms_loss_mode_1()
        
        IF comms_lost_duration > COMMS_LOSS_THRESHOLD_2:    # > 15 分钟
            # 二级降级：切换到最保守的避碰策略
            apply_comms_loss_mode_2()
        
        IF comms_lost_duration > COMMS_LOSS_THRESHOLD_3:    # > 30 分钟
            # 三级降级：考虑就近锚泊等待
            apply_comms_loss_mode_3()

COMMS_LOSS_THRESHOLD_1 = 300     # 秒
COMMS_LOSS_THRESHOLD_2 = 900     # 秒
COMMS_LOSS_THRESHOLD_3 = 1800    # 秒
```

### 17.2 降级模式定义

```
FUNCTION apply_comms_loss_mode_1():
    # 增大 CPA 安全距离 20%
    CPA_safe_effective *= 1.2
    # 降低航速至计划的 80%
    publish_speed_advisory(0.8)
    log_event("comms_loss_mode_1", {comms_lost_duration})

FUNCTION apply_comms_loss_mode_2():
    # 增大 CPA 安全距离 50%
    CPA_safe_effective *= 1.5
    # 降低航速至计划的 60%
    publish_speed_advisory(0.6)
    # 避碰策略切换为"所有目标都让路"（最保守）
    set_avoidance_policy("all_give_way")
    log_event("comms_loss_mode_2", {comms_lost_duration})

FUNCTION apply_comms_loss_mode_3():
    # 评估是否有安全的锚泊区域
    nearest_anchorage = find_nearest_safe_anchorage()
    IF nearest_anchorage:
        publish_anchorage_advisory(nearest_anchorage)
    ELSE:
        # 无法锚泊——继续以最低安全速度航行
        publish_speed_advisory(V_MIN_MANEUVER / V_MAX)
    log_event("comms_loss_mode_3", {comms_lost_duration, anchorage: nearest_anchorage})
```

---

## 18. 岸基报告与人工介入请求

### 18.1 自动报告触发条件

以下事件自动触发向岸基的报告：

| 事件类型 | 报告优先级 | 内容 |
|---------|----------|------|
| 新的 HIGH/CRITICAL 威胁 | 中 | 目标信息 + CPA/TCPA |
| 避碰行动开始 | 中 | 会遇类型 + 推荐行动 |
| 升级触发 | 高 | 升级原因 + 当前 CPA/TCPA |
| 紧急模式激活 | 最高 | 紧急行动 + 位置 + 目标信息 |
| 目标行为异常 | 高 | 异常类型 + 详情 |
| 传感器降级 | 高 | 降级的传感器 + 影响评估 |
| 态势解除 | 低 | 最终 CPA + 避让持续时间 |
| 回归完成 | 低 | 偏航距离 + 时间损失 |

### 18.2 人工介入请求

```
FUNCTION request_human_intervention(reason, urgency, details):
    
    intervention_request = HumanInterventionRequest()
    intervention_request.reason = reason
    intervention_request.urgency = urgency    # "advisory" / "recommended" / "mandatory"
    intervention_request.details = details
    intervention_request.own_position = own_ship.position
    intervention_request.situation_snapshot = capture_situation_snapshot()
    
    shore_link.publish(intervention_request)
    
    log_event("human_intervention_requested", {reason, urgency})
```

触发人工介入请求的条件：

```
# 条件 1：多次升级后仍无法解决
IF tracker.escalation_count >= 2 AND NOT is_situation_improving():
    request_human_intervention(
        reason: "多次升级后避碰效果仍不佳",
        urgency: "recommended"
    )

# 条件 2：多目标冲突无法自动消解
IF multi_target_conflict AND no_unified_solution_found:
    request_human_intervention(
        reason: "多目标冲突无法自动消解",
        urgency: "mandatory"
    )

# 条件 3：所有避让方案因空间限制不可行
IF avoidance_planner.all_plans_failed:
    request_human_intervention(
        reason: "所有避让方案因空间限制不可行",
        urgency: "mandatory"
    )
```

---

## 19. 历史态势学习与统计

### 19.1 避碰事件统计

Risk Monitor 维护一个运行时的避碰事件统计数据库，用于识别模式和优化参数：

```
struct AvoidanceStatistics {
    uint32 total_encounters;           # 总会遇次数
    uint32 avoidance_actions_taken;    # 采取避碰行动次数
    uint32 escalation_count;           # 总升级次数
    uint32 emergency_count;            # 紧急事件次数
    
    float64 avg_initial_cpa;           # 平均初始 CPA
    float64 avg_final_cpa;             # 平均最终 CPA
    float64 avg_avoidance_duration;    # 平均避碰持续时间
    float64 avg_deviation_nm;          # 平均偏航距离
    
    map<string, uint32> encounter_type_counts;  # 各会遇类型的次数分布
    map<string, uint32> action_type_counts;     # 各行动类型的次数分布
    
    float64 false_alarm_rate;          # 虚警率（避碰后 CPA 远超安全距离的比例）
};
```

### 19.2 虚警率监控

```
FUNCTION update_false_alarm_rate(statistics, event):
    
    # 如果最终 CPA > CPA_safe × 3.0，说明可能是过度反应（虚警）
    IF event.final_cpa > CPA_safe × 3.0 AND event.escalation_count == 0:
        statistics.possible_false_alarms += 1
    
    statistics.false_alarm_rate = statistics.possible_false_alarms / statistics.total_encounters
    
    # 如果虚警率过高（> 30%），建议调整威胁评估参数
    IF statistics.false_alarm_rate > 0.3 AND statistics.total_encounters > 20:
        log_event("high_false_alarm_rate", {
            rate: statistics.false_alarm_rate,
            recommendation: "建议降低 CPA 权重 w_cpa 或提高 MEDIUM 阈值"
        })
```

---

## 20. 内部子模块分解

| 子模块 | 职责 | 建议语言 | 说明 |
|-------|------|---------|------|
| State Machine Manager | 管理每目标独立状态机的状态转换 | C++ | 核心控制逻辑 |
| CPA Trend Analyzer | CPA 趋势计算（线性回归+分类） | C++ | 实时数学计算 |
| Effectiveness Evaluator | 综合多指标评估避碰效果 | C++ | 多指标融合判定 |
| Escalation Controller | 升级触发判定和执行 | C++ | 安全关键路径 |
| Resolution Checker | 态势解除条件检查（严格条件+持续确认） | C++ | 安全关键路径 |
| Emergency Handler | 紧急态势处理和跨层指令发送 | C++ | 最高优先级 |
| Recovery Monitor | 恢复阶段安全扫描 | C++ | 回归安全保障 |
| Anomaly Detector | 目标行为异常检测 | C++ | 与 Target Tracker 协作 |
| Sensor Health Monitor | 传感器降级检测和参数调整 | C++ | 系统鲁棒性 |
| Comms Loss Handler | 通信中断降级策略 | C++ | 自主降级 |
| Shore Reporter | 岸基报告生成和发送 | Python | IO 密集 |
| Statistics Tracker | 避碰事件统计和虚警率监控 | Python | 分析类 |
| Event Logger | 全程事件日志记录 | Python | 审计合规 |

---

## 21. 数值算例

### 21.1 算例一：正常避碰→解除→恢复

```
t=0s:     MONITORING 状态
          Target Tracker 报告目标 A: CPA=800m, TCPA=400s, threat=HIGH

t=2s:     COLREGs Engine 发出 action_required=true (交叉, 我让路)
          → 状态转换: MONITORING → AVOIDING
          → 记录初始 CPA=800m, TCPA=400s

t=60s:    避碰行动执行中（右转 50°）
          CPA 趋势: 800 → 900 → 1050 → 1200m (IMPROVING)
          → 效果良好，继续 AVOIDING

t=180s:   CPA 趋势: 1200 → 1500 → 1800 → 2100m (IMPROVING)
          → 持续改善

t=340s:   CPA 已过最近点 (TCPA < 0)
          CPA=2100m > CPA_safe=1852m ✓
          range_rate > 0 (远离中) ✓
          threat_score=15 < 20 ✓
          → 开始解除确认计时

t=400s:   解除条件持续满足 60 秒
          → 状态转换: AVOIDING → RESOLVED
          → 通知 Avoidance Planner 启动恢复

t=402s:   → 状态转换: RESOLVED → RECOVERING
          → 监控回归路径安全性

t=550s:   距原航线 < 50m, 航向偏差 < 5°
          → 状态转换: RECOVERING → MONITORING
          → 避碰事件完成，记录报告
          → 总持续时间: 550s, 偏航约 1.2nm
```

### 21.2 算例二：避碰效果不佳→升级→解除

```
t=0s:     AVOIDING 状态（右转 40° 避让目标 B）

t=60s:    CPA 趋势: 600 → 650 → 640 → 630m (STAGNANT/DETERIORATING)
          → 效果不佳，CPA 几乎没有改善

t=90s:    停滞持续 > T_stagnation_tolerance(60s)
          TCPA=200s < T_escalate(120s) × 1.5
          → 触发升级
          → 状态转换: AVOIDING → ESCALATING
          → 发送升级请求: 增大转向 15°（从 40° 增至 55°）

t=120s:   升级后 CPA 趋势: 630 → 750 → 900 → 1100m (IMPROVING)
          → 升级有效
          → 状态转换: ESCALATING → AVOIDING

t=250s:   CPA=1900m, TCPA < 0, threat_score=12
          → 解除条件满足，开始确认计时

t=310s:   → 状态转换: AVOIDING → RESOLVED
```

### 21.3 算例三：目标突然转向→紧急

```
t=0s:     AVOIDING 状态（右转 50° 避让目标 C）
          CPA 趋势: IMPROVING (CPA 从 500m 增至 1200m)

t=120s:   目标 C 突然向左转向 60°！
          Anomaly Detector 触发: "sudden_turn"
          CPA 预测突然从 1200m 降至 400m！
          → 发送重新评估请求给 COLREGs Engine

t=130s:   COLREGs Engine 重新评估: 新 CPA=400m, TCPA=90s
          → 触发升级: 增大转向 + 减速

t=150s:   CPA 仍在恶化: 400 → 350 → 300m
          TCPA=60s < T_emergency
          CPA=300m < CPA_safe × 0.5 = 926m
          → 状态转换: ESCALATING → EMERGENCY
          → 发送紧急指令: 左满舵 + 紧急停车
          → 通知岸基: EMERGENCY
```

---

## 22. 参数总览表

| 类别 | 参数 | 默认值 | 说明 |
|------|------|-------|------|
| **效果评估** | CPA 改善检测周期 | 30s | |
| | CPA 改善最低要求 | 50m/30s | |
| | CPA 改善趋势阈值 | > 1.0 m/s | 线性回归斜率 |
| | CPA 恶化趋势阈值 | < -0.5 m/s | |
| | CPA 停滞容忍时间 | 60s | |
| | CPA 恶化容忍时间 | 15s | |
| | 效果评估平滑窗口 | 15s | |
| | 目标预测偏差上限 | 200m | |
| **升级** | 升级 TCPA 阈值（开阔） | 120s | |
| | 升级 CPA 比例（开阔） | < 0.8 × CPA_safe | |
| | 最小升级间隔 | 30s | |
| | 最大升级次数 | 3 | 之后进入 EMERGENCY |
| | 第一次升级增量 | +15° 或 -2 m/s | |
| | 第二次升级增量 | +30° 或 -4 m/s | |
| **紧急** | 紧急 TCPA 阈值（开阔） | 60s | |
| | 紧急 CPA 比例 | < 0.5 × CPA_safe | |
| | 紧急状态退出持续确认 | 30s | |
| **解除** | CPA 解除阈值 | ≥ CPA_safe | |
| | TCPA 解除条件 | < 0 或 range 远离 | |
| | 距离解除条件 | > 2 × CPA_safe | |
| | 解除确认持续时间 | 60s | |
| | 解除后监控窗口 | 120s | |
| | 威胁评分解除阈值 | < 20 | |
| **恢复** | 新威胁扫描半径 | 3 × CPA_safe | |
| | 恢复最大速度 | 0.9 × V_plan | |
| | 恢复完成距离 | 50m | |
| | 恢复完成航向偏差 | 5° | |
| | 恢复超时 | 600s | |
| **传感器降级** | 全传感器安全系数 | 1.0 | |
| | 3 传感器安全系数 | 1.2 | |
| | 2 传感器安全系数 | 1.5 | |
| | 1 传感器安全系数 | 2.0 | |
| **通信中断** | 一级降级阈值 | 300s | |
| | 二级降级阈值 | 900s | |
| | 三级降级阈值 | 1800s | |
| **统计** | 虚警率告警阈值 | > 30% | |
| | 统计有效样本量 | ≥ 20 次 | |

---

## 23. 与其他模块的协作关系

| 交互方 | 方向 | 数据内容 | 频率 |
|-------|------|---------|------|
| Target Tracker → Risk Monitor | 输入 | ThreatList（CPA/TCPA/威胁等级） | 2 Hz |
| COLREGs Engine → Risk Monitor | 输入 | ColregsDecision[]（当前决策状态） | 2 Hz |
| Avoidance Planner → Risk Monitor | 输入 | AvoidanceCommand（当前避让指令） | 2 Hz |
| System Monitor → Risk Monitor | 输入 | SystemHealth（传感器和通信状态） | 1 Hz |
| Perception → Risk Monitor | 输入 | EnvironmentState（能见度） | 0.2 Hz |
| Risk Monitor → COLREGs Engine | 输出 | 升级请求 + 重新评估请求 | 事件驱动 |
| Risk Monitor → Avoidance Planner | 输出 | 态势解除通知 | 事件驱动 |
| Risk Monitor → Control (L5) | 输出 | 紧急指令（仅 EMERGENCY，跨层） | 事件驱动 |
| Risk Monitor → Shore Link | 输出 | 风险报告 + 人工介入请求 | 事件驱动 + 1 Hz 状态 |
| Risk Monitor → Event Logger | 输出 | 全程事件日志 | 实时 |

---

## 24. 测试策略与验收标准

### 24.1 测试场景

| 场景编号 | 描述 | 验证目标 |
|---------|------|---------|
| RSK-001 | 常态无威胁 | MONITORING 状态稳定 |
| RSK-002 | 新高威胁出现 → 进入 AVOIDING | 状态转换正确 |
| RSK-003 | 避碰效果良好（CPA 持续改善） | AVOIDING 状态保持 |
| RSK-004 | CPA 停滞 > 60s + TCPA 短 → 升级 | 升级触发正确 |
| RSK-005 | 升级后效果改善 → 降级回 AVOIDING | 降级逻辑正确 |
| RSK-006 | 多次升级 → 进入 EMERGENCY | 升级次数限制生效 |
| RSK-007 | TCPA < 60s + CPA 极小 → 直接 EMERGENCY | 紧急触发正确 |
| RSK-008 | 紧急指令正确发送至 L5 | 跨层通信正确 |
| RSK-009 | 解除条件全满足 + 持续 60s → RESOLVED | 解除判定严格且正确 |
| RSK-010 | 解除后目标回来 → 重新 AVOIDING | 解除后监控窗口有效 |
| RSK-011 | 恢复阶段发现新威胁 → 中断恢复 | 恢复安全扫描有效 |
| RSK-012 | 恢复完成 → 回到 MONITORING | 回归判定正确 |
| RSK-013 | 目标突然转向 → 异常检测 + 重新评估 | 异常响应正确 |
| RSK-014 | 目标信号丢失（LOST） | 保守处理生效 |
| RSK-015 | 雷达故障 → 安全参数增大 | 传感器降级正确 |
| RSK-016 | 岸基通信中断 5 分钟 → 一级降级 | 通信降级正确 |
| RSK-017 | 岸基通信中断 15 分钟 → 二级降级 | |
| RSK-018 | 多目标同时避让 → 联合监控 | 多目标逻辑正确 |
| RSK-019 | 全部目标解除 → 统一恢复 | 联合解除正确 |
| RSK-020 | 完整生命周期（MONITORING→AVOIDING→RESOLVED→RECOVERING→MONITORING） | 端到端流程 |

### 24.2 验收标准

| 指标 | 标准 |
|------|------|
| CPA 恶化检测延迟 | ≤ 15 秒（恶化容忍时间内检测到） |
| 升级触发响应时间 | ≤ 2 秒 |
| 紧急指令发送延迟 | ≤ 500 ms |
| 态势解除误判率 | 0%（解除后 120s 内不应再次触发同一目标） |
| 解除确认可靠性 | 100% 满足全部五个条件且持续 60 秒 |
| 异常行为检测率 | ≥ 95%（航向突变 > 30° 的检出率） |
| 状态机完整性 | 无死锁、无遗漏状态、所有转换有明确条件 |
| 岸基报告及时性 | 关键事件 ≤ 5 秒内发送 |
| 事件日志完整性 | 全部事件类型有记录，无遗漏 |

---

## 25. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IMO COLREGs Rule 2 | 责任条款（安全优先覆盖） |
| [2] | IMO COLREGs Rule 7 | 碰撞危险判定 |
| [3] | IMO COLREGs Rule 8 | 避碰行动要求 |
| [4] | IMO COLREGs Rule 17 | 直航船的逐级行动义务 |
| [5] | IMO MSC.1/Circ.1638 | MASS 监管框架 |
| [6] | IMO MSC.467(101) | MASS 操作指南（含远程监控要求） |
| [7] | DNV-ST-0561 "Autonomous and Remotely Operated Ships" | 船级社自主船舶标准 |
| [8] | Endsley, M.R. "Toward a Theory of Situation Awareness" Human Factors, 1995 | 态势感知理论 |
| [9] | ISO 23860 "Ships and Marine Technology - MASS" | MASS 术语和概念标准 |

---

## v2.0 架构升级：反射弧触发逻辑与 ASDR 接口

### A. Emergency Reflex Arc 触发逻辑

在 v2.0 架构中，Risk Monitor 的 EMERGENCY 通道被扩展为两级触发的反射弧。反射弧的触发源不仅限于 Risk Monitor 的状态机判定——Perception 层的 Data Preprocessing 模块可以在极近距离检测到目标时，直接触发反射弧的 Arm 级别，绕过完整的 L3 决策链。

```
FUNCTION reflex_arc_integration(tracker, target, perception_alert):
    
    # ---- 来源 1：Risk Monitor 内部的 EMERGENCY 判定（原有逻辑）----
    IF should_enter_emergency(tracker, target):
        trigger_reflex_arc_fire(target, source="risk_monitor")
    
    # ---- 来源 2：Perception 层的极近距离预警（v2.0 新增）----
    # Perception Data Preprocessing 在检测到极近距离目标时
    # 通过 /perception/reflex_alert 话题直接通知 Risk Monitor
    IF perception_alert IS NOT NULL:
        IF perception_alert.range < REFLEX_RANGE_THRESHOLD AND perception_alert.closing:
            
            # Arm 级别：预装紧急指令但不执行
            IF reflex_state == "IDLE":
                reflex_state = "ARMED"
                reflex_armed_time = now()
                preload_emergency_command(perception_alert)
                log_event("reflex_armed", perception_alert)
            
            # Fire 级别：连续 2 帧确认后执行
            IF reflex_state == "ARMED" AND perception_alert.consecutive_frames >= 2:
                reflex_state = "FIRED"
                execute_preloaded_emergency()    # 直接发布到 /system/emergency_maneuver
                log_event("reflex_fired", {
                    source: "perception_direct",
                    range: perception_alert.range,
                    tcpa: perception_alert.tcpa_estimate,
                    latency_ms: (now() - perception_alert.first_detection) × 1000
                })
    
    # Arm 超时取消（5 秒内未 Fire 则取消）
    IF reflex_state == "ARMED" AND now() - reflex_armed_time > REFLEX_ARM_TIMEOUT:
        reflex_state = "IDLE"
        log_event("reflex_arm_timeout")

REFLEX_RANGE_THRESHOLD = 200    # 米
REFLEX_ARM_TIMEOUT = 5.0        # 秒
```

### B. ASDR 事件日志接口

Risk Monitor 是 ASDR（智能黑匣子）最重要的数据源之一。所有状态转换、升级事件、紧急触发都必须实时写入 ASDR。

```
# Risk Monitor 向 ASDR 发布的数据

ASDR_RiskEvent:
    Time timestamp
    string event_type                   # 事件类型（见下表）
    string state_before, state_after    # 状态转换
    uint32 target_track_id, mmsi
    float64 cpa, tcpa, range
    string decision_rationale           # 决策理由
    float64[4] own_position_heading_speed  # 本船快照
    ColregsDecision colregs_snapshot    # 当时的 COLREGs 决策快照

# ASDR 事件类型
ASDR_EVENT_TYPES = [
    "avoidance_started",      # 避碰开始
    "effectiveness_warning",  # 效果不佳预警
    "escalation_triggered",   # 升级触发
    "emergency_triggered",    # 紧急状态激活
    "reflex_armed",           # 反射弧预备
    "reflex_fired",           # 反射弧触发
    "situation_resolved",     # 态势解除
    "recovery_complete",      # 回归完成
    "target_anomaly",         # 目标行为异常
    "sensor_degradation",     # 传感器降级
    "comms_loss"              # 通信中断
]

# 发布话题：/asdr/risk_events（QoS = RELIABLE + TRANSIENT_LOCAL）
```

### C. 与 Deterministic Checker 的协作

Risk Monitor 的升级请求（escalation_request）在发送给 COLREGs Engine 之前，会经过 L3 层的 COLREGs Compliance Checker 校验——确保升级后的避让方案仍然符合 COLREGs 规则。

```
# 升级请求路径（v2.0）：
# Risk Monitor → escalation_request → COLREGs Engine（重新计算）
#                                           ↓
#                                   COLREGs Compliance Checker（校验）
#                                           ↓ 通过
#                                   Avoidance Planner（生成新航线）
#                                           ↓
#                                   Route Safety Gate（校验航线安全性）
#                                           ↓ 通过
#                                   发布新的 AvoidanceCommand
```

---

## v3.0 架构升级：Capability Assessment 推力能力评估

### A. 功能定义

Capability Assessment（推力能力评估）是 Risk Monitor 的新增子功能，对标 Kongsberg K-Pos 的 DP Capability Analysis。它持续评估当前环境条件下本船推进系统是否有足够的推力余量来维持航线——如果余量不足，提前预警建议降速或改变航向。

### B. 推力余量计算

```
FUNCTION assess_capability(environment, thruster_status, ship_params):
    
    # 1. 估算当前环境力（来自 Navigation Filter 的海流估计 + 风速仪数据）
    F_wind = compute_wind_force(environment.wind_speed, environment.wind_dir, ship_params)
    F_current = compute_current_force(nav_state.V_c_N, nav_state.V_c_E, ship_params)
    F_wave_drift = compute_wave_drift(environment.wave_Hs, environment.wave_dir, ship_params)
    
    F_env_total = F_wind + F_current + F_wave_drift    # 三维：Fx, Fy, Mz
    
    # 2. 计算各推进器的最大可用推力
    F_available = compute_available_thrust(thruster_status, ship_params)
    # 考虑故障推进器（可用推力减少）
    # 考虑功率限制（Power Management 的约束）
    
    # 3. 计算各方向的推力余量百分比
    reserve = {}
    FOR direction IN range(0, 360, 30):    # 每 30° 一个方向
        # 该方向上的环境力分量
        F_env_dir = project_force(F_env_total, direction)
        # 该方向上的最大推力
        F_max_dir = compute_max_thrust_in_direction(F_available, direction)
        
        # 余量 = (最大推力 - 环境力) / 最大推力 × 100%
        IF F_max_dir > 0:
            reserve[direction] = max(0, (F_max_dir - abs(F_env_dir)) / F_max_dir × 100)
        ELSE:
            reserve[direction] = 0
    
    # 4. 最小余量方向
    min_reserve_dir = min(reserve, key=reserve.get)
    min_reserve_pct = reserve[min_reserve_dir]
    
    # 5. 告警判定
    IF min_reserve_pct < RESERVE_CRITICAL:
        RETURN {
            level: "CRITICAL",
            min_reserve: min_reserve_pct,
            worst_direction: min_reserve_dir,
            recommendation: "推力不足以抵抗当前环境力，建议立即降速或调整航向避开最差方向"
        }
    ELIF min_reserve_pct < RESERVE_WARNING:
        RETURN {
            level: "WARNING",
            min_reserve: min_reserve_pct,
            worst_direction: min_reserve_dir,
            recommendation: "推力余量偏低，建议降低航速或关注天气变化"
        }
    ELSE:
        RETURN {level: "OK", min_reserve: min_reserve_pct}

RESERVE_CRITICAL = 10    # %——低于 10% 为危急
RESERVE_WARNING = 30     # %——低于 30% 告警
```

### C. 推进器故障时的能力降级

```
FUNCTION assess_thruster_loss_impact(failed_thruster_id, environment, ship_params):
    
    # "如果这个推进器故障了，会怎样？"
    # Kongsberg 称之为 "Consequence Analysis"
    
    degraded_status = copy(thruster_status)
    degraded_status[failed_thruster_id].available = false
    
    degraded_capability = assess_capability(environment, degraded_status, ship_params)
    
    IF degraded_capability.min_reserve < RESERVE_CRITICAL:
        RETURN {
            impact: "SEVERE",
            message: f"如果 {failed_thruster_id} 故障，{degraded_capability.worst_direction}° 方向推力不足"
        }
    
    RETURN {impact: "MANAGEABLE"}
```

### D. 输出话题

```
# /tactical/capability_assessment（0.2 Hz，每 5 秒更新一次）
CapabilityAssessment:
    Header header
    float64[12] reserve_by_direction    # 每 30° 方向的推力余量 %
    float64 min_reserve_pct             # 最小余量百分比
    float64 worst_direction_deg         # 最差方向
    string alert_level                  # "OK"/"WARNING"/"CRITICAL"
    string recommendation               # 建议操作
```

---

## v3.1 架构升级：反射弧 PMS 预通知 + OCA 在线后果预演

### A. 反射弧触发时的电力管理预通知（P1）

反射弧从 Arm 进入 Fire 之前，Risk Monitor 向 Power Management System 发送"高负荷预警"——让 PMS 提前准备（如启动备用发电机、提高母排电压阈值），为 Crash Stop 的涌流做准备。

```
FUNCTION reflex_pms_pre_notify(reflex_state, reflex_alert):
    
    IF reflex_state == "ARMED":
        # Arm 阶段——预通知 PMS 准备高负荷
        pms_notify("HIGH_LOAD_IMMINENT", {
            estimated_power_kw: estimate_crash_stop_power(),
            time_to_fire_s: estimate_tcpa(reflex_alert),
            action: "PREPARE_SPINNING_RESERVE"
        })
    
    IF reflex_state == "FIRED":
        # Fire 阶段——确认高负荷开始
        pms_notify("HIGH_LOAD_ACTIVE", {
            actual_command: reflex_command,
            estimated_duration_s: 15    # Crash Stop 典型持续时间
        })

FUNCTION estimate_crash_stop_power():
    # 估算 Crash Stop 的峰值功率需求
    P_main_reverse = ship_params.max_engine_power × 1.2    # 反转时功率更高
    P_rudder = 5    # kW（舵机功率相对较小）
    P_bow = ship_params.bow_thruster_max_power
    RETURN P_main_reverse + P_rudder + P_bow
```

### B. OCA 在线后果预演——最坏单点故障分析（P3，V2 版本实现）

OCA（Online Consequence Analysis）持续在后台运行"如果最重要的那台推进器此刻故障，船会被吹到哪里？"的快速仿真预演。

```
FUNCTION online_consequence_analysis(own_ship, environment, thruster_status):
    
    # 识别当前的"最坏单点故障"（WCSF）
    # 通常是输出推力最大的那台推进器
    wcsf_thruster = identify_worst_case_failure(thruster_status)
    
    # 简化仿真：假设 WCSF 推进器瞬间失效，其余推进器维持当前输出
    # 预演未来 120 秒的漂移轨迹
    drift_trajectory = fast_simulate_drift(
        own_ship = own_ship,
        failed_thruster = wcsf_thruster,
        environment = environment,    # 当前风浪流
        duration_s = 120,
        dt = 1.0    # 粗粒度积分即可
    )
    
    # 计算漂移包络线（考虑风向不确定性 ±15°）
    drift_envelope = compute_drift_envelope(drift_trajectory, wind_uncertainty=15)
    
    # 检查漂移包络线是否触及危险区域
    min_distance_to_coast = check_distance(drift_envelope, enc_coastline)
    min_distance_to_shallow = check_distance(drift_envelope, enc_shallow_water)
    min_distance_to_platform = check_distance(drift_envelope, platform_positions)
    
    # 评估结果
    IF min_distance_to_coast < OCA_CRITICAL_DISTANCE:
        oca_status = "RED"
        # 强制 L3 战术层扩大安全边距
        risk_adjustment = {
            cpa_safe_multiplier: 1.5,    # CPA 安全距离增大 50%
            speed_limit_factor: 0.8,      # 限速到 80%
            reason: f"OCA: WCSF of {wcsf_thruster.name} would cause drift to coast in {drift_time}s"
        }
        notify_tactical("OCA_RED", risk_adjustment)
    
    ELIF min_distance_to_coast < OCA_WARNING_DISTANCE:
        oca_status = "AMBER"
        notify_tactical("OCA_AMBER", {reason: "drift envelope approaching coast"})
    
    ELSE:
        oca_status = "GREEN"
    
    # 发布到 HMI（驾驶台和岸基都显示）
    publish_oca_status(oca_status, drift_envelope, wcsf_thruster)
    
    RETURN oca_status

OCA_CRITICAL_DISTANCE = 500     # 米——漂移包络线距海岸/浅水 < 500m → RED
OCA_WARNING_DISTANCE = 2000     # 米——< 2000m → AMBER
OCA_UPDATE_INTERVAL = 10        # 秒——每 10 秒更新一次预演
```

### C. 简化漂移仿真

```
FUNCTION fast_simulate_drift(own_ship, failed_thruster, environment, duration_s, dt):
    
    # 极简化的漂移模型——不需要完整的 6DOF 仿真
    # 只需要估算"无动力时船被风浪推着走多远"
    
    state = copy(own_ship)
    trajectory = [state.position]
    
    # 移除故障推进器的推力贡献
    remaining_thrust = total_thrust - failed_thruster.current_output
    
    FOR t IN range(0, duration_s, dt):
        # 环境力（风+流，简化为恒定）
        F_wind = compute_wind_force(environment.wind, state.heading)
        F_current = compute_current_force(environment.current, state.heading)
        
        # 残余推力的抵抗力（如果其余推进器仍在工作）
        F_remaining = remaining_thrust × 0.5    # 保守假设：50% 能力用于抵抗漂移
        
        # 净力
        F_net = F_wind + F_current - F_remaining
        
        # 简化动力学：F = ma → a = F/m → Δv = a×dt → Δx = v×dt
        accel = F_net / own_ship.displacement
        state.velocity += accel × dt
        state.position += state.velocity × dt
        
        trajectory.append(state.position)
    
    RETURN trajectory
```

### D. OCA 的实现阶段

| 阶段 | 内容 | 版本 |
|------|------|------|
| V1 | Capability Assessment（已有）——当前推力余量 vs 环境力 | v3.0 已实现 |
| V2 | WCSF 简化漂移预演——120 秒漂移轨迹 + 包络线 | v3.1 定义，V2 实现 |
| V3 | 完整数字孪生——嵌入简化仿真引擎实时运行 | 未来版本 |

---

## v3.1 补充升级：近场安全区监测（离靠港操作）

### A. 问题——港内静态障碍物无实时监测

L3 的 COLREGs 避碰处理的是移动目标船。港内的码头、防波堤、系泊船、桥墩是静态的，不属于 COLREGs 范围。L2 Safety Checker 检查航线不穿越禁区——但这是一次性检查。港内实时机动时（特别是 JOYSTICK 和 DP_APPROACH 模式），船可能做任意方向的移动，需要持续的近场障碍物距离监测。

### B. 近场安全区定义

```
# 近场安全区是以船体轮廓为基础向外扩展的三层保护区

FUNCTION define_proximity_zones(ship_length, ship_beam):
    
    # 第一层：警告区（提醒——可以继续但需注意）
    warning_zone = ship_outline.expand(
        forward=ship_length × 0.5,    # 船首方向 22.5m
        aft=ship_length × 0.3,        # 船尾方向 13.5m
        port=ship_beam × 1.5,         # 左舷 14.25m
        starboard=ship_beam × 1.5     # 右舷 14.25m
    )
    
    # 第二层：限制区（限制运动——禁止朝障碍物方向移动）
    restrict_zone = ship_outline.expand(
        forward=10.0,     # 船首方向 10m
        aft=5.0,          # 船尾方向 5m
        port=5.0,         # 左舷 5m
        starboard=5.0     # 右舷 5m
    )
    
    # 第三层：紧急区（紧急停车——太近了）
    emergency_zone = ship_outline.expand(
        forward=3.0,
        aft=2.0,
        port=2.0,
        starboard=2.0
    )
    
    RETURN warning_zone, restrict_zone, emergency_zone
```

### C. 障碍物距离监测逻辑

```
FUNCTION proximity_monitor(own_ship, obstacle_map, zones):
    
    # obstacle_map 来自 Perception 的近场结构物检测
    # 包含：码头面、防波堤、系泊船、桥墩、航标的位置和轮廓
    
    alerts = []
    
    FOR EACH obstacle IN obstacle_map:
        min_distance = compute_min_distance(own_ship.hull_outline, obstacle.outline)
        closest_direction = compute_closest_direction(own_ship, obstacle)
        
        IF min_distance < zones.emergency.distance(closest_direction):
            alerts.append({
                level: "EMERGENCY",
                obstacle: obstacle.name,
                distance: min_distance,
                direction: closest_direction,
                action: "EMERGENCY_STOP"
            })
            trigger_emergency_stop(closest_direction)
        
        ELIF min_distance < zones.restrict.distance(closest_direction):
            alerts.append({
                level: "RESTRICT",
                obstacle: obstacle.name,
                distance: min_distance,
                direction: closest_direction,
                action: "BLOCK_MOTION_TOWARD"
            })
            # 阻止向障碍物方向的运动——但允许其他方向
            block_motion_vector(closest_direction)
        
        ELIF min_distance < zones.warning.distance(closest_direction):
            alerts.append({
                level: "WARNING",
                obstacle: obstacle.name,
                distance: min_distance,
                direction: closest_direction,
                action: "VISUAL_AUDIO_ALERT"
            })
    
    publish_proximity_alerts(alerts)
    RETURN alerts

# 发布话题：/safety/proximity_alerts
# 频率：10 Hz（靠泊模式下）
# QoS: RELIABLE

FUNCTION block_motion_vector(blocked_direction):
    # 通知 L5 Thrust Allocator：
    # 在 blocked_direction 方向上的推力指令强制为零
    # 其他方向不受限
    # 效果：船可以沿着障碍物平行移动或远离，但不能接近
    
    thrust_constraint = ThrustConstraint()
    thrust_constraint.blocked_direction = blocked_direction
    thrust_constraint.max_thrust_toward = 0    # 不允许朝障碍物方向推进
    constraint_publisher.publish(thrust_constraint)
```

### D. 与 DP_APPROACH 的集成

```
DP_APPROACH 模式下的近场安全区逻辑：

1. 进近段（50~200m）：
   只监测 WARNING 级别——远距离不限制运动

2. 精确段（5~50m）：
   WARNING + RESTRICT 激活——如果横向偏离过大接近码头墙，
   限制该方向运动

3. 接触段（0~5m）：
   三层全部激活——但"目标泊位面"从障碍物列表中排除
   （因为我们就是要接触那个面）
   其余障碍物（相邻船舶、码头转角）正常监测

关键逻辑：
  在 DP_APPROACH 中，approach_target（目标泊位面）不触发安全区
  但 approach_target 的两侧边界（码头转角、系泊桩）仍然触发
```

### E. HMI 显示

```
近场安全区在驾驶台 MASS_ADAS 显示器上的可视化：

1. 船体轮廓周围显示三层等距线
   - 绿色虚线 = 警告区边界
   - 黄色虚线 = 限制区边界
   - 红色虚线 = 紧急区边界

2. 障碍物到最近安全区边界的距离数字实时更新

3. 当障碍物进入限制区：
   - 受限方向显示为红色箭头（直观显示"不能往那走"）
   - 蜂鸣器短鸣
   
4. 当障碍物进入紧急区：
   - 全屏闪红
   - 连续蜂鸣
   - 自动停车
```

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v2.0 升级：反射弧两级触发 + ASDR + Checker 协作 |
| 0.3 | 2026-04-26 | 架构组 | v3.0 升级：Capability Assessment |
| 0.4 | 2026-04-26 | 架构组 | v3.1 升级：PMS 预通知 + OCA/WCSF |
| 0.5 | 2026-04-26 | 架构组 | v3.1 补充：增加近场安全区监测（三层保护区 + 运动方向阻断 + DP_APPROACH 集成 + HMI 显示）；支持离靠港静态障碍物避让 |

---

**文档结束**
