# 003_final_waypoint_hold 闭环验证报告

日期：2026-05-18

分支：`tiger`

## 目的

`003_final_waypoint_hold` 用于验证船舶到达最终航点后的行为：

- 是否进入最终航点到达状态。
- 是否减速到安全低速。
- 是否进入保持、待命或人工接管逻辑。
- 是否避免在终点附近反复发出较大前进推力。

该场景是 `001` 静水直线和 `002` 静水 S 转之后的第三个基础闭环场景。

## 002 的经验

`002_s_turn_calm` 的核心经验是：

1. 基础闭环必须先隔离变量。不能把航迹切换、终端减速、风流波扰动、执行器故障同时放进一个基础场景。
2. 场景参数必须是场景级 tuning，不能为了修复一个场景污染全局控制参数。
3. 验证指标要区分航迹段和终端低速段。低速终端捕获不能用巡航 COG 航向误差直接评价。
4. 每次进入更复杂场景前，都要保留前一基础场景的回归证据。
5. mock 参数全部是 C 级可信度，只能用于开发验证，后续接真实数据时要校准和重新验证。

这些经验直接影响了 `003`：最终航点保持应该先在静水下成立，再进入风、流、波和低速靠泊扰动。

## 初始 003 运行结果

初始 `003_final_waypoint_hold` 同时包含横风、横流和轻微波浪：

```yaml
wind: 4.0 m/s
current: 0.2 m/s
wave: Hs 0.3 m
```

运行目录：

`D:\02-dynamics\reports\final_waypoint_hold\003_final_waypoint_hold_20260518_010122`

结果：

| 指标 | 期望 | 实测 | 结果 |
|---|---:|---:|---|
| `max_final_position_error_m` | <= 12 m | 21.1370 m | FAIL |
| `max_final_speed_mps` | <= 0.4 m/s | 0.3866 m/s | PASS |
| `should_publish_arrival_status` | true | false | FAIL |
| `should_enter_hold_or_manual_handover` | true | true | PASS |
| `should_not_keep_reissuing_large_forward_thrust` | true | true | PASS |

关键观测：

- `mission_phases_seen` 只有 `ABORT_ESCAPE`。
- `safety_statuses_seen` 包含 `ABORT_REQUIRED`。
- `max_environment_force_n` 达到 `549580 N`。
- `max_cross_track_error_m` 达到 `143.8 m`。
- `max_yaw_rate_deg_s` 达到 `21.48 deg/s`。

结论：初始 `003` 不是纯粹的“最终航点保持”验证，而是把环境抗扰和终点保持混在一起。按照 `002` 的经验，这应拆分。

## 场景修正

`003_final_waypoint_hold` 已调整为静水基础场景：

```yaml
wind: 0.0 m/s
current: 0.0 m/s
wave: Hs 0.0 m
```

同时补充基础安全门禁：

- `navigation_fault_detected: false`
- `should_enter_degraded_mode: false`
- `should_not_command_hard_turn: true`

横风、横流、波浪和低速靠泊扰动保留到后续场景：

- `004_crosswind_tracking`
- `005_crosscurrent_tracking`
- `006_combined_environment`
- `012_low_speed_berthing_crosswind`

## 最终 003 闭环结果

运行目录：

`D:\02-dynamics\reports\final_waypoint_hold\003_final_waypoint_hold_20260518_013151`

可视化文件：

`D:\02-dynamics\reports\final_waypoint_hold\003_final_waypoint_hold_20260518_013151\scenario_visualization.html`

关键结果：

| 指标 | 期望 | 实测 | 结果 |
|---|---:|---:|---|
| `max_final_position_error_m` | <= 12 m | 1.8443 m | PASS |
| `max_final_speed_mps` | <= 0.4 m/s | 0.00057 m/s | PASS |
| `should_publish_arrival_status` | true | true | PASS |
| `should_enter_hold_or_manual_handover` | true | true | PASS |
| `should_not_keep_reissuing_large_forward_thrust` | true | true | PASS |
| `navigation_fault_detected` | false | false | PASS |
| `should_enter_degraded_mode` | false | false | PASS |
| `should_not_command_hard_turn` | true | true | PASS |

自动验收结果：`0 failed, 8 passed, 0 pending`。

补充观测：

- `arrival_time_s: 345.1443`
- `max_cross_track_error_m: 0.7585`
- `max_heading_error_deg: 0.7191`
- `max_yaw_rate_deg_s: 0.0739`
- `mission_phases_seen`: `PRECHECK`, `CRUISE`, `DECEL`, `REPORT`, `APPROACH`, `STANDBY`, `COMPLETE`
- `safety_statuses_seen`: `NOMINAL`

## 可视化方法

新增工具：

```bash
ros2 run mock_scenarios scenario_visualize -- \
  --scenario /mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/003_final_waypoint_hold.yaml \
  --run-dir /mnt/d/02-dynamics/reports/final_waypoint_hold/003_final_waypoint_hold_20260518_013151
```

Windows `cmd` 中推荐使用：

```bash
wsl -d Ubuntu-22.04 --cd /mnt/d/02-dynamics bash tools/wsl_ros2_visualize_run.sh \
  /mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/003_final_waypoint_hold.yaml \
  /mnt/d/02-dynamics/reports/final_waypoint_hold/003_final_waypoint_hold_20260518_013151
```

HTML 包含：

- 计划航线和实际航迹。
- 航速曲线。
- 横向误差曲线。
- 航向误差曲线。
- 艏摇角速度曲线。
- 终点距离曲线。
- 合并后的 metrics 表。
- acceptance 表。

## 后续进入 004 的条件

进入 `004_crosswind_tracking` 前，必须保持：

1. `001_straight_calm` 通过。
2. `002_s_turn_calm` 通过。
3. `003_final_waypoint_hold` 通过。
4. 任何横风失败都不得回头污染 `001/002/003` 的基础参数。

`004` 应只引入横风，不同时引入横流、波浪或故障。若失败，优先分析：

- 环境载荷模型量级是否合理。
- 侧推/舵/主推进器是否具备抵抗横风的分配能力。
- 安全监督器是否应进入 CAUTION 而不是 ABORT_REQUIRED。
- 控制器是否需要显式横向误差和艏向联合约束。

当前阶段仍全部基于 C 级可信度 mock data。后续替换真实数据时，应校准环境载荷、阻尼、推力器和舵效参数，而不是重写任务、制导、控制和安全监督架构。
