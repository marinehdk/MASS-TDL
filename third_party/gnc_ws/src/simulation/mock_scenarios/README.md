# mock_scenarios

`mock_scenarios` 提供面向 MASS ADAS 的场景化 mock data，用于软件架构、接口、可观测性和回归验证开发。

## 分层

- `truth`：理想本船状态，发布到 `/mock/truth/odometry`。
- `sensor`：带噪声或故障的输入，发布到 `/ship/odometry`、`/mock/gnss/odometry`、`/mock/imu` 和 `/mock/heading`。
- `expected`：每个场景的验收指标，保存在 YAML 中。
- `data_policy`：所有 mock 场景必须显式标注 `parameter_confidence: C`。

## Mock Data 规则

- mock 场景可用于软件架构、接口、可观测性和回归验证开发。
- mock 场景不可用于宣称实船能力。
- 后续接入真实数据时，应替换适配器、校准 YAML 参数并重新验证，而不是重写任务、安全、制导、控制或推力分配逻辑。

## RoutePlan 船长意图输入

从 `002_s_turn_calm` 开始，复杂航线不再只依赖最终航点到达判断。场景应携带结构化 `route_plan`，表达船长式航行意图：

- `waypoint_plan`：航点编号、坐标、角色和坐标可信度。
- `leg_plan`：计划 COG、航程、速度上限、左右 XTL 和航行模式。
- `turn_plan`：转向点、转弯半径、ROT、提前转向距离、目标转弯速度和方向。
- `safety_corridor`：XTE/XTL 超限后的减速、重回航线、保持、接管或撤离策略。
- `validation_policy`：验证是否执行了计划航迹形状，而不仅是是否到达终点。

当前实现仍通过 `own_ship.waypoints` 和 `route_plan.waypoint_gates` 兼容层驱动制导。下一阶段应增加 RoutePlan adapter，把 `leg_plan`、`turn_plan` 和 XTL 转换为制导参数、任务状态机 gate 和安全监督器输入。

45m crewboat 的 `8m/s` 是静水直线巡航目标，不是转弯、外部载荷、进近和故障场景下的硬红线。按时完成是优化目标，安全完成是约束。

工程契约见：

- `config/validation/route_plan_contract_45m_crewboat.yaml`
- `docs/route_plan_core_inputs_45m_crewboat_zh.md`

## 当前核心话题

- `/ship/odometry`：`nav_msgs/Odometry`，供现有制导/控制链路使用。
- `/env/wind_params`：`geometry_msgs/Vector3`，`x=speed_mps`，`y=direction_deg`，`z=anemometer_height_m`。
- `/env/ocean_currents`：`ship_interfaces/OceanCurrents`。
- `/thruster/health_status`：`std_msgs/Float64MultiArray`。
- `/mock/scenario_status`：`std_msgs/String`。

A->B 验证启动文件会以 shadow mode 启动 `safety_supervisor`。它只观测现有话题，并发布 `/safety/status`、`/safety/abort_request`、`/captain/alert`、`/safety/command_limits`、`/navigation/status` 和 `/actuator/capability`，不改变现有控制指令链路。

A->B 验证启动文件也会以 shadow mode 启动 `mission_supervisor`。它发布 `/mission/phase`、`/mission/active_gate` 和 `/mission/status`，把航次阶段推进、gate 判断和终点到达状态变成可观测接口。

A->B 验证启动文件同时启动 `validation_observer`，把 mission/safety/navigation/actuator 话题写入：

- `observability_events.jsonl`
- `observability_metrics.yaml`

`tools/wsl_ros2_run_ab_validation.sh` 会把 CSV 指标和 observability 指标合并为 `metrics_merged.yaml` 后再执行场景验收。

## 常用命令

启动一个 mock 场景：

```bash
ros2 launch mock_scenarios mock_scenario.launch.py scenario_name:=007_gnss_jump.yaml
```

运行动力学/分配一致性检查：

```bash
ros2 run mock_scenarios model_consistency_check -- --config /mnt/d/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml
```

运行数值动力学/推力分配探针：

```bash
ros2 run mock_scenarios dynamics_allocation_probe -- --config /mnt/d/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml
```

运行零输入动力学方程能量探针：

```bash
ros2 run mock_scenarios dynamics_equation_probe -- --config /mnt/d/02-dynamics/src/platform/ship_bringup/config/ship_config.yaml
```

生成场景验收清单：

```bash
ros2 run mock_scenarios scenario_acceptance
```

检查所有场景是否标注为 C 级可信度 mock data，并确认每个预期指标都有观测接口：

```bash
ros2 run mock_scenarios mock_contract_check
```

该检查也会验证额外的 mock 参数文件，例如 `safety_supervisor/config/safety_limits.yaml`。

从 WSL 运行 Phase 0 非 ROS 合同检查：

```bash
bash tools/wsl_ros2_phase0_check.sh
```

构建后运行已安装的 ROS2 合同检查：

```bash
bash tools/wsl_ros2_contract_check.sh
```

运行安全监督器短烟测：

```bash
bash tools/wsl_ros2_safety_smoke.sh
```

运行任务状态机短烟测：

```bash
bash tools/wsl_ros2_mission_smoke.sh
```

运行 A->B 验证启动短烟测，确认 shadow-mode 安全监督器、任务状态机和验证观测器都被接入整体 launch：

```bash
bash tools/wsl_ros2_launch_smoke.sh
```

运行全场景套件并生成能力边界报告：

```bash
bash tools/wsl_ros2_run_suite.sh
```

运行最小 golden baseline 闭环回归：

```bash
bash tools/wsl_ros2_golden_baseline.sh
```

该命令是后续开发的防退化门禁。修改其他场景、控制逻辑、推力分配、安全监督器或任务状态机前后，都应先确认 `001_straight_calm` 仍然通过：

- 场景验收必须 0 failed、0 pending。
- `regression_gates.yaml` 中的 golden baseline 门禁必须全部通过。
- 运行证据应保存在 `reports/golden_baseline/<run_id>/`。
- 若该基线失败，应先修复静水、直线、单航段、无故障的基础闭环，再推进更复杂场景。

当前 baseline 闭环记录见：

- `docs/golden_baseline_zh.md`

用 YAML 或 JSON 中的观测指标进行验收：

```bash
ros2 run mock_scenarios scenario_acceptance -- --metrics /mnt/d/02-dynamics/reports/scenario_metrics.yaml
```

从 `ship_dynamics` CSV 日志提取观测指标：

```bash
ros2 run mock_scenarios scenario_metrics_from_csv -- \
  --scenario /mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/001_straight_calm.yaml \
  --csv /mnt/d/02-dynamics/logs/ship_sim_YYYYMMDD_HHMMSS.csv \
  --output /mnt/d/02-dynamics/reports/scenario_metrics.yaml
```

合并多个指标文件：

```bash
ros2 run mock_scenarios scenario_metrics_merge -- \
  --output /mnt/d/02-dynamics/reports/metrics_merged.yaml \
  /mnt/d/02-dynamics/reports/scenario_metrics.yaml \
  /mnt/d/02-dynamics/reports/observability_metrics.yaml
```

根据套件运行结果生成 Markdown 报告：

```bash
ros2 run mock_scenarios scenario_suite_report -- \
  --suite-results /mnt/d/02-dynamics/reports/mass_adas_suite_YYYYMMDD_HHMMSS/suite_results.tsv \
  --output /mnt/d/02-dynamics/reports/mass_adas_capability_boundary.md
```

把单个场景运行结果可视化为自包含 HTML：

```bash
ros2 run mock_scenarios scenario_visualize -- \
  --scenario /mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/002_s_turn_calm.yaml \
  --run-dir /mnt/d/02-dynamics/reports/s_turn_final/002_s_turn_calm_YYYYMMDD_HHMMSS
```

输出文件默认为该运行目录下的 `scenario_visualization.html`，包含计划航线/实际航迹、速度、横向误差、航向误差、艏摇角速度、终点距离、合并 metrics 和 acceptance 表格。

也可以在 Windows `cmd` 中直接进入 WSL 脚本：

```bash
bash tools/wsl_ros2_visualize_run.sh \
  /mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/002_s_turn_calm.yaml \
  /mnt/d/02-dynamics/reports/s_turn_final/002_s_turn_calm_YYYYMMDD_HHMMSS
```

## 推荐验证顺序

1. 静水基线。
2. 增加风/流扰动。
3. 注入 GNSS 跳变和中断。
4. 注入罗经偏差。
5. 注入单个推进器故障。
6. 对照每个场景的 `expected` 块评估行为。

## 场景矩阵

- `001_straight_calm.yaml`：静水直线跟踪基线。
- `002_s_turn_calm.yaml`：静水计划转向。
- `003_final_waypoint_hold.yaml`：最终航点到达和保持。
- `004_crosswind_tracking.yaml`：稳定横风抗扰跟踪。
- `005_crosscurrent_tracking.yaml`：稳定横流抗扰跟踪。
- `006_combined_environment.yaml`：风、流和轻微波浪组合扰动。
- `007_gnss_jump.yaml`：短时 GNSS 位置跳变。
- `008_gnss_outage.yaml`：GNSS 中断和航位推算降级。
- `009_turning_circle_starboard.yaml`：右舷回转试验风格的艏摇响应筛查。
- `010_zigzag_10_10.yaml`：10/10 Z 形试验风格的超调和艏摇阻尼筛查。
- `011_crash_stop_ahead.yaml`：前进急停和倒车推力门控。
- `012_low_speed_berthing_crosswind.yaml`：横风下低速靠泊支持。
- `013_narrow_channel_crosscurrent.yaml`：受限水域横流航迹保持。
- `014_thruster_failure_crosswind.yaml`：推进器故障、重分配和船长告警。

## 证据构建顺序

1. `001`、`009`、`010`、`011`：基础操纵物理和执行器限制。
2. `004`、`005`、`006`：环境载荷抗扰。
3. `007`、`008`：导航传感器故障处理。
4. `012`、`013`：面向船长的作业场景。
5. `014`：执行器故障降级。

## 场景文件必须包含

- `scenario_class`：`baseline`、`standard_manoeuvre`、`environmental_disturbance`、`sensor_fault`、`actuator_fault` 或 `captain_operational`。
- `validation_objective`：该场景要回答的工程问题。
- `data_policy.parameter_source: mock_data`。
- `data_policy.parameter_confidence: C`。
- `data_policy.real_data_transition`：说明真实数据如何替换 mock 输入，且不重写核心逻辑。
- `expected.pass_fail_basis`：自然语言通过/失败规则。
- `expected` 下至少一个数值验收限制。

Phase-gated 回归配置位于：

- `config/validation/mock_data_contract.yaml`
- `config/validation/regression_gates.yaml`
