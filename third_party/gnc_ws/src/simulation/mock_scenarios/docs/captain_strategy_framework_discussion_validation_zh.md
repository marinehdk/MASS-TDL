# 船长策略框架讨论与验证记录

## 1. 核心结论

当前系统不应该继续把长航程船长意图航线简单放大为一串 waypoint，再交给折线 LOS 和局部 gate 参数去追踪。

建议采用的框架是：

```text
Context-Aware Captain Strategy Framework

PassagePlan / RoutePlan
  -> RoutePlan Adapter
  -> ContextClassifier
  -> CaptainPolicyEngine
  -> RouteExecutionPlan
  -> MissionSupervisor HSM
  -> Guidance / MotionPlanner
  -> Controller
  -> ThrustAllocator
  -> SafetySupervisor
  -> Observability / Regression
```

这套框架的含义是：

- `RoutePlan` 描述船长批准的航线意图，不等同于控制器每个周期追逐的点。
- `CaptainPolicyEngine` 把船长经验转成可参数化策略。
- `RouteExecutionPlan` 把航线意图转成可执行的速度、转弯、走廊和内部轨迹。
- `MissionSupervisor HSM` 决定当前任务阶段。
- `SafetySupervisor` 持续监控偏离、风险和执行器余量，并有权降级、待命或撤离。
- `Observability` 必须记录每一次策略选择、限幅、gate 切换、XTE 超限和状态转换。

## 2. 为什么不能只有一套通用固定参数

真实船长脑中有通用经验，但不是一套固定数值。

可以通用的是判断流程：

1. 先确认航次计划、船况、水域、权限和环境。
2. 再判断当前任务阶段：巡航、减速、转弯、报告、进近、待命、作业、撤离。
3. 再选择速度窗口、XTD/XTE、转弯半径、ROT、wheel-over、推进器使用策略。
4. 持续监控偏差和风险。
5. 偏离后按分级策略处理：报警、减速、重入航线、待命、人工/岸基接管、撤离。

不能通用的是固定数值：

- 8.0 m/s 可以作为 45m crewboat 在静水直航 001 中的目标航速，但不是所有场景的硬红线。
- 转弯处速度必须根据半径、ROT、横向加速度、乘客舒适性、环境载荷和水域限制调整。
- XTE/XTD 不应全航线固定。开阔水域、狭水道、港池、靠泊进近和作业区应有不同走廊。
- 侧推、主桨差动和舵的使用不能由 QP 自由优化，必须受速度、任务阶段和船长策略约束。

## 3. 标准和最新外部信息依据

### IMO 航次计划

IMO A.893(21) 将 voyage/passage planning 分为 appraisal、planning、execution、monitoring，并强调从 berth-to-berth 进行计划和持续监控。这个结构正好对应代码中的：

```text
Appraisal  -> ContextClassifier
Planning   -> RoutePlan / RouteExecutionPlan
Execution  -> MissionSupervisor + Guidance + Controller
Monitoring -> SafetySupervisor + Observability
```

参考：IMO Resolution A.893(21), Guidelines for Voyage Planning
https://wwwcdn.imo.org/localresources/en/KnowledgeCentre/IndexofIMOResolutions/AssemblyDocuments/A.893%2821%29.pdf

### IMO MASS Code

截至 2026-05-20，IMO MSC 111 正在 2026-05-13 至 2026-05-22 召开。IMO 官方预告称 MSC 111 预计完成并通过非强制性的 MASS Code。MSC 110 的路线图则给出后续方向：2026 年形成非强制 MASS Code，2030 年前后推动强制 MASS Code，目标 2032 年生效。

这说明当前工程阶段应重视：

- operational context
- risk assessment
- human element
- alert management
- safety of navigation
- remote/autonomous operation
- software principles
- connectivity

参考：IMO MSC 111 preview
https://www.imo.org/en/mediacentre/meetingsummaries/pages/preview-msc-111.aspx

参考：IMO MSC 110 summary
https://www.imo.org/en/mediacentre/meetingsummaries/pages/msc-110th-session.aspx

### IHO S-100 / S-101 / ECDIS

IHO 在 2026 年进入 S-100 Phase 1 产品规范生效阶段。S-101 ENC Edition 2.0.0、S-102 bathymetry、S-104 water level、S-111 surface currents、S-124 navigational warnings、S-129 UKC management 等均已成为辅助驾驶路径约束的重要数据源。

但需要区分：

- S-57/S-101/S-100 是海图和水文数据标准，不是航线计划本身。
- 航线交换更接近 RTZ / IEC PAS 61174-1。
- 程序应从 ECDIS/route planner/RTZ 中读取航线，从 ENC/S-100 产品中读取限制条件。

参考：IHO ENC & ECDIS
https://iho.int/en/enc-ecdis

参考：IHO S-100 framework operational
https://iho.int/en/the-s-100-framework-is-now-operational

### RTZ 航线交换

IEC PAS 61174-1:2021 明确面向 Maritime Route Plan Exchange Format RTZ，用于提高 route plan exchange 的互操作性。我们的 `RoutePlan Adapter` 后续应把 RTZ/ECDIS/operator plan 转换为内部 `RouteExecutionPlan`。

参考：IEC PAS 61174-1:2021
https://webstore.iec.ch/en/publication/67774

## 4. 推荐运行流程

### 4.1 航前阶段

```text
外部航线/任务输入
  -> 航线合法性校验
  -> 海图/水深/禁区/航道/报告点/权限检查
  -> 船况和推进器可用性检查
  -> 环境窗口检查
  -> 人工或岸基批准
  -> 生成 RouteExecutionPlan
```

航前不能直接启动控制器。系统必须先证明当前航线在当前 ODD 中可执行。

### 4.2 执行阶段

```text
PRECHECK
  -> CRUISE
  -> DECEL
  -> PLANNED_TURN
  -> REPORT
  -> APPROACH
  -> STANDBY
  -> BERTH_OR_WORK
  -> COMPLETE
```

任何阶段都可以被安全监督器打断：

```text
ANY_PHASE -> CAUTION -> DEGRADED -> HOLD_OR_MANUAL_HANDOVER -> ABORT_ESCAPE
```

### 4.3 偏离处理

XTE/XTD 超限不应直接等于 emergency abort。建议策略为：

```text
0.7 * XTD: alert / caution
1.0 * XTD: reduce speed + rejoin corridor
1.3 * XTD: hold or manual/shore handover
hard boundary / collision / grounding risk: abort escape
```

## 5. 代码层落地方案

### 5.1 新增 CaptainPolicyEngine

建议新增 ROS 2 package 或 node：

```text
src/strategy/captain_policy/
  captain_policy_node.py
  config/captain_policy_45m_crewboat_c_mock.yaml
```

输入：

```text
/mission/phase
/route/approved_plan
/navigation/odometry
/environment/state
/traffic/risk
/actuator/health
/operator/clearance
```

输出：

```text
/policy/selected_profile
/policy/speed_envelope
/policy/turn_constraints
/policy/route_corridor
/policy/propulsion_policy
/policy/degradation_policy
```

### 5.2 新增 RoutePlan Adapter

建议新增：

```text
src/navigation/route_plan_adapter/
```

职责：

- 导入 RTZ / ECDIS / operator plan。
- 读取 ENC/S-100 派生约束。
- 输出内部 `RouteExecutionPlan`。
- 区分 `captain_visible_gates` 和 `planner_internal_trajectory`。
- 生成每段 XTD/XTE、速度窗口、WOP、turn radius、ROT、最大横向加速度。

### 5.3 改造 Guidance

当前 `ship_guidance_node` 仍主要消费 `own_ship.waypoints` 和 `route_plan.waypoint_gates`。002B 证明这种方式不适合长航程船长意图航线。

下一步应改成：

```text
Guidance consumes:
  RouteExecutionPlan
    - smooth reference path
    - station/progress coordinate
    - speed profile
    - curvature/ROT limits
    - rejoin policy
    - final approach mode
```

而不是：

```text
Guidance consumes:
  dense waypoint list as if all points are real WPs
```

### 5.4 改造 SafetySupervisor

当前安全监督器主要监控速度、yaw rate、环境载荷、执行器健康和 `/cmd_tau`，但没有把 RoutePlan 的 XTD/XTE 作为安全输入。002B 本轮验证中，XTE 已经超出走廊，但 `safety_status` 仍保持 `NOMINAL`，这说明安全监督器还没有真正接入航线走廊语义。

应新增输入：

```text
/navigation/cross_track_error
/route/corridor_margin
/route/current_leg
/policy/degradation_policy
```

并输出：

```text
/safety/status = NOMINAL | CAUTION | DEGRADED | ABORT_REQUIRED
/safety/command_limits
/safety/abort_request
/safety/reason_codes
```

### 5.5 改造可观测系统

每个周期或关键事件应记录：

- mission phase
- selected policy profile
- current leg
- speed envelope
- XTE / XTD margin
- WOP / switch decision reason
- heading command before/after rate limit
- propulsion policy
- side thruster lockout/derate reason
- safety status and reason codes

## 6. 002B 本轮动态验证结论

### 6.1 验证运行

场景：

```text
D:\02-dynamics\src\simulation\mock_scenarios\config\scenarios\002b_long_captain_intent.yaml
```

报告目录：

```text
D:\02-dynamics\reports\regression_002b_long_captain_final_line_20260520\002b_long_captain_intent_20260520_080514
```

HTML：

```text
D:\02-dynamics\reports\regression_002b_long_captain_final_line_20260520\002b_long_captain_intent_20260520_080514\scenario_visualization.html
```

结果：

```text
SUMMARY: 7 failed, 12 passed, 0 pending
```

关键指标：

```text
max_speed_mps = 7.99073
min_mean_transit_speed_mps = 4.3324
max_cross_track_error_m = 210.056
max_route_plan_xte_excess_m = 60.056
route_plan_xte_worst_segment = wp7-wp8
final_waypoint_reached = false
max_final_position_error_m = 1218.94
safety_statuses_seen = [NOMINAL]
mission_phases_seen = [PRECHECK, CRUISE]
```

### 6.2 根因判断

本轮失败不应首先归因于船舶动力学模型。证据是：

- 静水环境，无风流浪载荷。
- 最大速度、平均速度、yaw rate 均在可控范围。
- 失败发生在 `wp7 -> wp8` 的反向转弯切换区域。
- `wp7 -> wp8` 在还剩约 249 m 时提前切换，新的航段起点位于船后方，随后路径外抛。
- HOMING 虽然限幅到 32 度，但已经在错误的几何关系下追逐折线航段。
- 安全监督器没有因为 XTE 超限进入 CAUTION/DEGRADED，说明航线走廊没有接入安全链路。

更准确的根因是：

```text
RoutePlan 语义、wheel-over 几何、平滑轨迹生成、制导重入策略和安全监督器走廊监控之间尚未闭环。
```

### 6.3 不能采用的修复方式

不建议：

- 简单把 XTD 放宽到 250m 让场景通过。
- 继续增加密集 waypoint 伪装成船长航线。
- 单独调低速度到很低来穿过所有点。
- 让 QP 或控制器自由使用侧推补偿路径设计问题。
- 把所有超限直接 emergency abort。

### 6.4 正确修复方向

应先实现 RouteExecutionPlan：

```text
captain-visible gates
  -> smooth reference path
  -> curvature / ROT / speed profile
  -> sampled trajectory for guidance
  -> corridor and safety margins
```

对 002B，`wp7 -> wp8` 不应作为折线切换，而应成为一段连续 S-turn 曲线的一部分。wheel-over 应根据转弯半径、速度、ROT、横向加速度和船舶响应提前量计算，而不是仅用固定剩余距离触发下一段折线。

## 7. 和其他系统的对接

### 7.1 航线和海图系统

对接对象：

- ECDIS
- RTZ route exchange
- ENC S-57 / S-101
- S-102 bathymetry
- S-104 water level
- S-111 surface currents
- S-124 navigational warnings
- S-129 UKC management

接口产物：

```text
/route/approved_plan
/route/chart_constraints
/route/ukc_constraints
/route/no_go_areas
```

### 7.2 导航和感知系统

对接对象：

- GNSS / RTK
- IMU / gyro / compass
- AIS
- radar
- camera / lidar
- sensor fusion
- CPA/TCPA/COLREG risk assessment

接口产物：

```text
/navigation/ownship_state
/navigation/state_confidence
/traffic/risk
/collision/cpa_tcpa
```

### 7.3 船舶平台系统

对接对象：

- 3 个固定主桨
- 2 个贯穿侧推
- 2 个舵
- 主机和电力系统
- 推进器健康状态

接口产物：

```text
/policy/propulsion_policy
/allocator/actuator_limits
/actuator/health
/actuator/commands
```

### 7.4 任务和权限系统

对接对象：

- VTS / 港口许可
- 报告点确认
- 靠泊/作业许可
- 船长/远程操作员确认

接口产物：

```text
/operator/clearance
/mission/phase
/mission/gate_status
/mission/abort_escape_route
```

## 8. 建议的下一步闭环

### Step 1: 冻结现有成功基线

保留：

- 001: 8 m/s 静水直航 golden baseline
- 002: 短 S-turn 工程回归

这两个场景用于防止后续架构改造破坏基础能力。

### Step 2: 002B 不再继续局部调参

002B 当前应标记为：

```text
long captain-intent scenario: failed by route-execution architecture gap
```

它不是坏数据，而是正确暴露了下一阶段必须解决的问题。

### Step 3: 实现 RouteExecutionPlan 生成器

输入：

```text
captain_visible_gates
leg_plan
turn_plan
captain_policy_profile
```

输出：

```text
smooth_path_samples
station_s
curvature
speed_profile
xtd_limits
switch_events
```

验收：

- 生成的轨迹不能穿越 captain corridor。
- 曲率、ROT、横向加速度不超过 45m crewboat C 级 mock 限制。
- planner internal samples 默认不作为 captain-visible WPs 显示。

### Step 4: 改 Guidance 消费平滑参考轨迹

先保留旧 waypoint 模式用于 001/002。新增模式用于 002B：

```text
guidance_mode: route_execution_plan
```

### Step 5: 接入 SafetySupervisor 的走廊监控

验收：

- 当 XTE > 0.7 XTD，发布 CAUTION。
- 当 XTE > 1.0 XTD，发布 DEGRADED 或 REJOIN_REQUIRED。
- 当 XTE > 1.3 XTD 且持续超限，进入 HOLD_OR_MANUAL_HANDOVER。
- 硬边界风险才进入 ABORT_ESCAPE。

### Step 6: 重新跑回归

顺序：

```text
001 -> 002 -> 002B
```

只有 001/002 不退化，且 002B 在走廊和终点保持上通过，才进入风、流、浪、交通和故障场景。
