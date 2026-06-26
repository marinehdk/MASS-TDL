# 45m Crewboat RoutePlan 分层说明

日期：2026-05-19

## 背景

当前 `002_s_turn_calm.yaml` 已经能作为工程验证场景证明：在给定一条 C 级 mock RoutePlan 的情况下，系统可以完成 S-turn 航迹覆盖和终点保持。

但它还不是理想的船长意图航线，因为一个 YAML 同时包含了：

- 船长/任务层信息。
- 路径规划层信息。
- 控制器和推力分配调参。
- mock 环境、传感器、故障。
- 验收指标。

这种做法适合回归测试，不适合真实自主航行系统长期使用。

## 分层目标

新的配置样例把 002 拆成三层：

1. 船长/任务层：
   `config/mission_plans/002_s_turn_captain_mission.yaml`

2. 路径规划层：
   `config/route_profiles/002_s_turn_route_profile_45m_crewboat.yaml`

3. 控制执行层：
   `config/controller_profiles/002_s_turn_execution_profile.yaml`

当前通过的 `config/scenarios/002_s_turn_calm.yaml` 暂时保留为兼容执行基线，不在本阶段替换。

## 船长/任务层

船长层只表达任务意图，不应暴露密集控制采样点。

它应该包含：

- A 点、S 转入口、上侧通过区域、交叉门、下侧恢复门、进近门、待命门、作业点。
- 每个阶段的速度上限和进入/退出 gate。
- 安全优先级，例如 `safety_over_schedule`。
- 哪些阶段需要人工确认。
- 是否需要预先存在撤离路线。

船长层回答的问题是：

> 现在这条船要做什么，允许进入下一阶段的条件是什么？

## 路径规划层

路径规划层根据船长可见 gate，生成可执行轨迹。

它应该包含：

- 内部轨迹点。
- 每段 COG、距离、XTL。
- 每段速度限制。
- 转弯半径、ROT、wheel-over 距离。
- 安全走廊和 XTE 超限响应。

路径规划层回答的问题是：

> 这艘 45m crewboat 应该怎样平滑、安全地经过船长指定的关键区域？

这里可以有密集点，但这些点是规划器内部点，不应全部变成船长航次 WP。

## 控制执行层

控制执行层描述当前 GNC 和推力分配如何消费规划结果。

它应该包含：

- LOS/gate 兼容参数。
- 终端减速和 DP handoff 参数。
- yaw moment、yaw sign、速度前馈等控制参数。
- 主桨、舵、侧推的使用策略。
- 可观测话题和验收指标。

控制执行层回答的问题是：

> 给定一条 RoutePlan，控制器和推进系统如何执行，哪些输出必须可观测？

## 与真实自主航行的关系

测试阶段可以每个场景一个 YAML，因为测试需要可复现。

真实运行时不应每条航线都重启 ROS 或加载一份完整测试 YAML。正确方向是：

- 系统启动时加载船舶、控制、安全、传感器默认配置。
- 运行时由 ECDIS、任务系统或 Route Planner 发布 MissionPlan/RoutePlan。
- Mission Supervisor 根据阶段 gate 执行。
- Safety Supervisor 持续监控，可以从任意阶段中断到保持或撤离。

因此：

```text
YAML = 配置和测试夹具
RoutePlan = 运行时任务数据
```

## 当前阶段的边界

本次新增的三份 YAML 是分层样例和接口契约，不会自动改变 ROS 运行行为。

当前 ROS launch 仍然读取：

`config/scenarios/002_s_turn_calm.yaml`

下一阶段才应实现 RoutePlan adapter，把任务层和路径规划层动态转换为：

- `own_ship.waypoints`
- `wp_speed_limit_mps`
- `wp_switch_radius_m`
- `wp_wheel_over_distance_m`
- `wp_switch_max_xte_m`
- mission gate 和 safety corridor 状态

这样才能从“固定测试 YAML”逐步走向“运行时动态航线”。
