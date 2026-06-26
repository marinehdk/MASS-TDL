# 002_s_turn_calm 闭环验证报告

日期：2026-05-18

分支：`tiger`

## 结论

`002_s_turn_calm` 已完成端到端闭环验证。当前系统在 C 级可信度 mock data 下，可以完成静水 S 形多航段 A->B 航行，并满足当前场景的验收条件。

本次闭环同时回归了 `001_straight_calm` 黄金基线，确认 S 转修复没有破坏“静水、直线、单航段、无故障”的最小基线。

## 最终证据

`002_s_turn_calm` 端到端运行目录：

`D:\02-dynamics\reports\s_turn_final\002_s_turn_calm_20260518_001753`

关键结果：

| 指标 | 期望 | 实测 | 结果 |
|---|---:|---:|---|
| `max_cross_track_error_m` | <= 18 m | 7.3099 m | PASS |
| `max_heading_error_deg` | <= 8 deg | 7.4139 deg | PASS |
| `navigation_fault_detected` | false | false | PASS |
| `should_enter_degraded_mode` | false | false | PASS |
| `should_not_command_hard_turn` | true | true | PASS |
| `final_waypoint_reached` | true | true | PASS |
| `arrival_time_s` | - | 991.76594 s | INFO |

自动验收结果：`0 failed, 5 passed, 0 pending`。

`001_straight_calm` 回归目录：

`D:\02-dynamics\reports\golden_baseline\001_straight_calm_20260518_003713`

关键结果：

| 指标 | 期望 | 实测 | 结果 |
|---|---:|---:|---|
| `final_waypoint_reached` | true | true | PASS |
| `max_final_position_error_m` | <= 20 m | 10.1302 m | PASS |
| `max_final_speed_mps` | <= 0.25 m/s | 0.1541 m/s | PASS |
| `max_cross_track_error_m` | <= 8 m | 1.6576 m | PASS |
| `max_heading_error_deg` | <= 3 deg | 1.2440 deg | PASS |
| `max_yaw_rate_deg_s` | <= 1 deg/s | 0.1335 deg/s | PASS |

黄金基线验收：`0 failed, 9 passed, 0 pending`。

回归门限：`0 failed, 6 passed`。

## 根因

本次问题不是环境扰动造成的。失败首先出现在静水 S 转场景，因此根因集中在基础闭环：

1. 静态 YAML 航路启动时，航点索引容易从起点航点开始，导致制导目标推进不符合真实航段含义。
2. 中间航点捕获半径和终点到达半径被同一逻辑耦合，S 转中间航点切换不够可控。
3. 制导中存在硬编码最小舵效速度，导致场景级低速规划无法完全生效。
4. S 转需要比直线更保守的速度、预瞄和艏摇力矩策略；这些参数必须是场景级 tuning，不能污染全局基线。
5. 原始航向误差指标把低速终端阶段的 COG 横移也计入巡航航迹航向误差，导致接近终点时误判为大航向错误。

## 已完成修改

制导层：

- 静态 YAML 多航点航路从第一个目标航点开始跟踪。
- 分离 `intermediate_capture_radius` 和最终到达半径。
- 将 `minimum_steerage_speed` 参数化，消除硬编码。
- 支持 S 转场景独立配置预瞄、速度和 ILOS 参数。

控制层：

- 增加 `min_yaw_moment_cruise`，避免 S 转反向弯中艏摇力矩过早塌陷。
- 允许场景级 yaw PID 参数覆盖，`002` 使用独立的 `kp/kd`，不影响 `001`。

验证层：

- `scenario_metrics_from_csv` 只统计到达前的航迹段 CTE、航向和艏摇角速度。
- 转弯点附近使用入段/出段航向扇区，避免把合理转向过程误判为航向超差。
- 前进速度足够时使用 COG 评价航迹方向；低速终端捕获阶段按参数跳过航向统计，并由终点位置/速度指标评价。
- 新增 `heading_sample_count`、`heading_skipped_low_speed_count`、`heading_min_forward_speed_mps`、`course_over_ground_min_forward_speed_mps`，保证验证口径可观测。

场景数据：

- `002_s_turn_calm` 所有新增 tuning 和 validation 参数均属于 C 级可信度 mock 参数。
- 这些参数用于当前开发验证，不代表真实 45m crewboat 的最终标定值。

## 防止无限修复的边界

本次闭环采用“双基线”保护：

1. `002_s_turn_calm` 必须端到端通过。
2. `001_straight_calm` 黄金基线必须同时通过。

后续修改其他场景时，也应保持这个顺序：

1. 先跑 `001_straight_calm`，确认最小直线基线未退化。
2. 再跑 `002_s_turn_calm`，确认多航段 S 转未退化。
3. 最后再进入风、流、传感器故障、推进器故障等场景。

这样可以避免为了修复复杂场景而反复破坏基础闭环。

## 剩余风险

当前成功不等于已经满足实船辅助驾驶要求，原因如下：

1. 船舶质量、附加质量、阻尼、推力器能力曲线、舵效和倒车能力仍是 C 级 mock 参数。
2. 推力分配日志中仍可看到内部 Level-2/Level-3 优先级降级求解，说明分配器在某些力矩组合下牺牲 `Fy`，后续需要单独建立能力边界和饱和诊断。
3. 低速终端捕获目前只证明能进入到达半径并降速，不等于已经具备靠泊/人员转移/吊机作业级别的 DP 精度。
4. 当前没有真实风流、波浪、GNSS/IMU 误差谱、舵桨响应时滞和推进器功率限制的实测标定。

## 后续建议

下一步可以进入 `003_final_waypoint_hold` 和低速保持场景，但必须保持参数化、接口化、可观测化：

- mock 参数继续标注为 C 级可信度。
- 所有新增判断写入 metrics 或 observability events。
- 后续替换真实数据时，目标是校准和验证参数，而不是重写任务、制导、控制或分配架构。
