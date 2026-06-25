# 45m Crewboat 船长意图航线输入契约

日期：2026-05-19

适用范围：`mock_scenarios` 中 45m crewboat 的 C 级可信度 mock data 场景。

## 结论

辅助驾驶系统不应只证明“能跑到终点”。对于一条 45m crewboat，核心目标应升级为“按照船长意图，在安全边界内完成航行”。因此，路径规划输入必须从简单 `waypoints` 升级为结构化 `RoutePlan`：

- 航点：每个 WP 的编号、坐标、角色和坐标可信度。
- 航段：每段的计划航向、航程、速度上限、左右 XTL 和航行模式。
- 转弯：每个转向点的转弯半径、ROT、提前转向距离、目标转弯速度和方向。
- 安全走廊：XTE/XTL 超限后的减速、重回航线、保持、人工接管或撤离逻辑。
- 验收策略：必须验证是否覆盖了计划航迹形状，而不是只看终点是否到达。

## 45m Crewboat 的关键差异

不能把大型商船的航线参数直接搬到 45m crewboat。比如 `ROT <= 10 deg/min` 和 `0.5-2.0 NM` 转弯半径适合较大船舶或开阔水域航线计划，但对于当前 1600m 级 mock S 转场景会过于保守。

当前阶段采用 C 级 mock envelope：

| 模式 | 典型速度 | 转向策略 | 侧推策略 |
|---|---:|---|---|
| 静水直线巡航 | 8.0 m/s | 保持计划航向，低艏摇率 | 锁定侧推 |
| 计划转弯/机动 | 3.0-5.0 m/s | 提前降速，按半径和 ROT 平滑转向 | 通常锁定或降额，优先舵和主桨差动 |
| 终端进近/保持 | 0.0-3.0 m/s | 位置、艏向和安全余量优先 | 允许侧推，但必须限幅和可观测 |

这些值不是实船标定结果，后续必须用实船航迹、舵桨响应、风流载荷和推进器能力曲线重新校准。

## 必须进入系统的核心输入

`RoutePlan` 至少应包含以下结构：

```yaml
route_plan:
  schema_version: route_plan.v1
  vessel_profile_ref: crewboat_45m_c_mock
  coordinate_frame: local_enu
  captain_intent:
    priority: safety_over_schedule
    nominal_cruise_speed_mps: 8.0
    speed_is_hard_limit: false
  waypoint_plan:
    points:
      - {id: wp0, index: 0, local_xy_m: [0.0, 0.0], role: start}
  leg_plan:
    - {from: wp0, to: wp1, planned_cog_deg: 0.0, distance_m: 200.0, xtl_port_m: 20.0, xtl_starboard_m: 20.0}
  turn_plan:
    - {at: wp1, turn_radius_m: 160.0, max_rot_deg_s: 1.2, wheel_over_distance_m: 45.0, target_speed_mps: 4.8}
  safety_corridor:
    caution_ratio: 0.7
    hold_ratio: 1.0
    abort_ratio: 1.3
```

当前 `002_s_turn_calm.yaml` 已开始携带这套输入，但制导仍通过 `own_ship.waypoints` 和 `route_plan.waypoint_gates` 兼容层运行。下一步应增加 adapter，把 `leg_plan`、`turn_plan`、`XTL` 和速度计划转换为制导参数、状态机 gate 和验收指标。

## 与船长流程的关系

船长不是机械地追逐 waypoint，而是在每个阶段持续判断：

1. 当前航段是否仍然安全。
2. 是否到了提前转向点。
3. 转弯速度、艏向变化率和横向偏差是否合理。
4. 外部风、流、浪或交通条件是否要求减速、等待或撤离。
5. 推进系统当前是否适合执行该指令。

程序不需要复刻人工操作手感，但必须把这些判断变成可观测、可参数化、可回归测试的状态机和安全监督器。

## 关于 8m/s、门线和安全优先级

`8m/s` 应被定义为静水直线巡航目标，不应作为所有场景的硬红线。转弯、外部载荷、终端进近、传感器异常和推进器故障时，合理降速是船长式决策，不是测试退让。

门线也不是简单的“越过某条线就一定成功/失败”。在程序里，门线应表达为 gate：

- 位置是否进入计划区域。
- 速度是否低于该阶段上限。
- 艏向误差是否允许切换。
- XTE 是否仍在安全走廊内。
- 是否有环境、交通、传感器或执行器风险。

按时完成航线是优化目标；安全完成航线是约束。二者必须区分。

## 对 002 场景的启示

之前 002 的“终点到达式通过”并不能证明 S 转意图被执行。新的路线形状指标已经暴露出：船可以到达终点，但可能切掉中间转向点，没有按计划覆盖 S 形航迹。

因此 002 的闭环目标应变为：

- 先回归 `001_straight_calm`，保证 8m/s 静水直线基线不退化。
- 再验证 `002_s_turn_calm` 是否按 `RoutePlan` 覆盖两侧计划横向偏移。
- 如果失败，优先检查 route-plan adapter、转弯提前量、速度计划和 propulsion policy，而不是只放宽终点门限。

## 后续闭环开发路径

1. 固化 `RoutePlan` 输入契约和 002 场景示例。
2. 增加 route-plan adapter，把 `leg_plan` 和 `turn_plan` 转成制导/状态机 gate。
3. 增加可观测话题：`/mission/active_leg`、`/mission/active_turn`、`/navigation/route_corridor_status`。
4. 重新跑 `001` 回归，再跑 `002`，用 HTML 和 metrics 证明是否按航线意图航行。
5. 再进入风、流、浪和故障场景，所有 mock 参数保持 C 级可信度。
