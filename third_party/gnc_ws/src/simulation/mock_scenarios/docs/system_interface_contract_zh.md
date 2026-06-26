# 系统接口契约与回归基线说明

适用范围：当前 `tiger` 分支的 45m crewboat mock 验证系统。

本文档的目的不是证明实船适航性，而是把当前 mock 阶段必须稳定的接口、单位、坐标系、符号约定和回归基线固定下来。后续接入真实传感器、真实推进器或岸基系统时，应优先替换数据适配器并重新校准参数，而不是重写任务、制导、控制、分配和动力学主逻辑。

## 1. 可信度边界

当前所有场景参数仍属于 C 级可信度：

```yaml
data_policy:
  parameter_source: mock_data
  parameter_confidence: C
```

含义：

- 参数可用于工程开发、接口验证、回归测试和能力边界探索。
- 参数不能直接作为实船控制、船级社论证或最终操纵性证明。
- 后续真实数据接入时，目标是校准和验证，不应推翻接口与状态机结构。

## 2. 核心 ROS Topic 契约

| Topic | 发布者 | 订阅者 | 单位/坐标 | 契约 |
|---|---|---|---|---|
| `/ship/odometry` | `ShipDynamicsNode` | `Guidance`、`Controller`、`MissionSupervisor`、`SafetySupervisor` | SI，位置为世界系，速度为船体系/ROS odom twist 约定 | 全闭环状态反馈。超时必须触发安全降级。 |
| `/ship/waypoints` | mock runtime 或未来 route manager | `ShipGuidanceNode` | 当前为局部 XY，未来可由 route adapter 从经纬度转换 | 只表示可执行航迹点，不等同于船长原始 WP。 |
| `/control/heading_setpoint` | `ShipGuidanceNode` | `ShipControllerNode` | rad | 巡航/进近航向目标。 |
| `/control/speed_setpoint` | `ShipGuidanceNode` | `ShipControllerNode` | m/s | 速度目标，不是绝对红线；应受航段、转弯、环境和安全监督约束。 |
| `/target_pose` | `ShipGuidanceNode` | `ShipControllerNode` | 世界系 pose | DP/终点保持或局部跟踪目标。 |
| `/cmd_tau` | `ShipControllerNode` | `ThrustAllocator`、`SafetySupervisor` | N, N, Nm，`base_link` | 控制器输出的船体力/力矩需求。分配器内部可转换为 kN/kNm，但接口必须保持 N/Nm。 |
| `/thruster/commands` | `ThrustAllocator` | `ShipDynamicsNode` | 每个执行器 `thrust_N, angle_rad` | 执行器命令，顺序必须与 `thruster_names` 一致。 |
| `/thruster/health_status` | `ShipDynamicsNode` | `ThrustAllocator`、`SafetySupervisor` | 数组 | 当前 mock 阶段仍弱，真实 FDI 需要恢复残差检测。 |
| `/env/total_load` | `ForceAggregatorNode` | `ShipDynamicsNode`、`ThrustAllocator`、`SafetySupervisor` | N, N, Nm | 环境载荷合力。是否前馈由策略参数决定。 |
| `/mission/status` | `MissionSupervisor` | `SafetySupervisor`、报告工具 | JSON | 必须包含 phase、progress、cross_track_error_m、route_xte_limit_m。 |
| `/captain/decision` | `CaptainDecisionNode` | 报告工具，未来控制权仲裁器 | JSON，`captain_decision.v1` | 船长化决策输出：wait_for_initial_observation、continue、reduce_speed、rejoin_corridor、hold_or_manual_handover、abort_escape_recommended、request_human_confirmation。当前只观察不执行。 |
| `/captain/recommended_speed` | `CaptainDecisionNode` | 报告工具，未来 guidance/speed governor | m/s | 船长决策层给出的建议速度。当前不直接覆盖 `/control/speed_setpoint`。 |
| `/propulsion/policy` | `PropulsionPolicyNode` | 报告工具，未来 `ThrustAllocator` 策略输入 | JSON，`propulsion_policy.v1` | 推进策略输出：侧推是否允许、侧推最大比例、主桨是否要求对称、是否允许主桨差动、是否优先舵效、是否允许倒车；`main_propulsion_policy` 使用 `main_propulsion_symmetry_policy.v1` 子结构描述主桨对称策略的 required/released 区域、速度门槛、yaw/sway deadband、C 级 mock 参数来源和 advisory-only 执行阶段。 |
| `/propulsion/constraints` | `PropulsionPolicyNode` | `ThrustAllocator`、报告工具 | 数组 | 数值化推进约束：`side_allowed, side_fraction, main_symmetry, differential_allowed, rudder_preferred, reverse_allowed, actuator_ratio`。当前分配器执行 `side_allowed=false` 侧推锁定、配置允许范围内的 `side_fraction` 侧推比例限额、`reverse_allowed=false` 主桨禁倒车；首次收到该 topic 前按侧推不可用、倒车不可用处理。 |
| `/propulsion/compliance` | `PropulsionPolicyComplianceNode` | 报告工具，未来策略闭环验收 | JSON，`propulsion_policy_compliance.v1` | 对比 shadow policy 和实际 `/thruster/commands`，报告策略符合性和违规类型。当前只观察不执行。 |
| `/propulsion/compliance_metrics` | `PropulsionPolicyComplianceNode` | 报告工具 | 数组 | 数值化符合性指标：违规数量、侧推锁定违规、侧推比例违规、主桨对称违规、未授权倒车、舵优先区侧推、实际侧推总量、主桨不对称量。 |
| `/safety/status` | `SafetySupervisor` | `MissionSupervisor`、报告工具 | JSON | 当前以观察/告警为主；后续应升级为强制 command gate。`DEGRADED/ABORT_REQUIRED` 状态必须可观测。 |
| `/safety/abort_request` | `SafetySupervisor` | `MissionSupervisor` | bool | 仅在 enforcement 模式下发布真实 abort；`shadow_mode=true` 时只能观测和报警，不得驱动 mission 进入 `ABORT_ESCAPE`。 |
| `/safety/command_limits` | `SafetySupervisor` | 当前主要用于观测 | SI | 后续应成为控制/分配前的硬约束输入。 |

## 3. 单位与符号约定

强制规则：

- `/cmd_tau.wrench.force.x` 使用 N。
- `/cmd_tau.wrench.force.y` 使用 N。
- `/cmd_tau.wrench.torque.z` 使用 Nm。
- `ThrustAllocator` 内部 QP 使用 kN/kNm，因此必须显式执行 `1e-3` 转换。
- `ThrustAllocator` 输出 `/thruster/commands` 前必须转换回 N。
- `yaw_torque_sign_cruise` 与 `yaw_torque_sign_dp` 必须显式配置，不允许隐藏在代码常量中。
- `thruster_names` 的顺序在 `ship_dynamics_node` 与 `thrust_allocation_node` 中必须完全一致。

当前 45m crewboat mock 配置：

- 3 个固定主桨：`t1, t2, t3`
- 2 个贯穿侧推：`tb1, tb2`
- 2 个舵：`r1, r2`

## 4. 航线与船长意图分层

当前设计应保持三层分离：

1. 船长/任务层：少量关键 WP、gate、报告点、待命点、最终作业点。
2. 路径规划层：转弯半径、XTL、速度区间、wheel-over、内部采样点。
3. 控制器层：LOS/DP/速度恢复/转弯控制，不把内部采样点误认为船长原始 WP。

`route_execution_plan.py` 是当前兼容层。它把场景 YAML 中的 `route_plan` 转换成 `guidance_waypoints`、`guidance_gates` 和 `segment_constraints`。

`CaptainDecisionNode` 是当前新增的最小船长决策层。它不直接控制执行机构，而是把任务状态、安全状态、航线偏差、环境载荷和执行器能力转换成可观测决策：

| 决策 | 含义 |
|---|---|
| `continue` | 继续当前阶段。 |
| `reduce_speed` | 风险进入 caution 区，建议降速继续观察。 |
| `rejoin_corridor` | 航线偏差达到重回航道阈值，建议优先回归走廊。 |
| `hold_or_manual_handover` | 进入保持、待命或人工确认逻辑。 |
| `abort_escape_recommended` | 建议进入撤离路径；当前 shadow 模式不直接触发撤离。 |
| `request_human_confirmation` | 关键状态缺失或超时，需要人工/岸基确认。 |
| `wait_for_initial_observation` | 启动宽限期内输入尚未齐全，仅等待观测建立；超过宽限期仍缺失时才升级为人工/岸基确认。 |

`PropulsionPolicyNode` 是当前新增的最小推进策略层。它不修改 QP，也不直接写 `/thruster/commands`，而是把船长推进经验先变成可观测、可参数化、可回归测试的策略接口：

| 策略项 | 含义 |
|---|---|
| `side_thruster_allowed` | 高速/中速默认限制侧推，低速、靠泊、待命、明确应急动作才允许侧推。 |
| `side_thruster_max_fraction` | 只在配置指定的巡航效率区和速度门槛以上对侧推能力进行比例限额；4 m/s 以下转弯、重入、进近、DP 保持不做比例硬限，避免破坏船长化机动能力。 |
| `main_propulsion_symmetry_required` | 由 `main_propulsion_policy` 给出：巡航效率区、速度达到门槛、yaw/sway 需求在 deadband 内时要求主桨优先对称，避免无意义不对称推力。 |
| `differential_main_thrust_allowed` | 由 `main_propulsion_policy` 给出：转弯、回归航道、低速保持、撤离、人工确认等区域/动作释放主桨差动。 |
| `rudder_preferred` | 航速足够时优先使用舵效完成艏向控制，而不是滥用侧推。 |
| `reverse_allowed` | 巡航段禁止倒车；低速保持或 DECEL/APPROACH 计划制动阶段，且 `/cmd_tau` 明确请求负向 surge 时允许受控倒车。 |

`PropulsionPolicyComplianceNode` 是推进策略执行前的证据层。它不判断策略本身是否正确，而是回答：

```text
实际推进器输出是否符合当前 shadow policy？
```

当前检查：

| 违规项 | 含义 |
|---|---|
| `side_thruster_used_when_locked` | 策略不允许侧推时，实际侧推超过泄漏阈值。 |
| `side_thruster_fraction_exceeded` | 侧推允许但超过策略给出的比例上限。 |
| `main_propulsion_asymmetry_when_symmetry_required` | 策略要求主桨对称时，三台主桨差异过大。 |
| `reverse_thrust_without_policy_permission` | 策略不允许倒车时，主桨出现明显反推。 |
| `side_thruster_used_in_rudder_preferred_region` | 策略认为应优先舵效时，仍大量使用侧推。 |

XTE/XTL 的合同来源必须一致：

- `leg_plan` 是航线合同，验收、mission status 和 safety supervisor 应使用同一套 XTE/XTL 语义。
- `waypoint_gates` 是制导/控制器切换容差，不能替代航线合同。
- `route_plan.validation_policy.xte_limit_source` 应显式设置，避免不同模块使用不同限制来源。

## 5. Golden Regression

当前固定三条最小基线：

| 场景 | 目的 | 关键判断 |
|---|---|---|
| `001_straight_calm.yaml` | 8 m/s 静水直航 | 暴露正负号、单位、主桨分配和终点捕获问题。 |
| `002_s_turn_calm.yaml` | 工程 S-turn 回归 | 保证多航段、转弯、gate 和终点保持不退化。 |
| `002c_turn_recovery_cruise.yaml` | 显式减速与速度恢复 | 验证转弯后能在 XTE/航向/yaw rate 满足条件后恢复巡航。 |

任何修改控制、分配、动力学、route execution、安全监督或场景验收阈值的代码，都必须至少通过这三条回归。

## 6. 自动检查

本仓库新增：

```bash
python3 tools/system_contract_check.py
```

检查内容：

- `.gitignore` 是否覆盖本地构建产物和 CodeGraph 索引。
- 是否仍有已跟踪的 `src/**/build`、`.bak`、`.tmp`。
- 是否仍有 patch/fix/modify 脚本留在 `src` 生产目录。
- `ship_config.yaml` 中控制器和分配器的 yaw sign、推进器顺序、推进器几何是否一致。
- `/cmd_tau`、`/thruster/commands`、`/ship/odometry`、`/mission/status`、`/safety/status` 的代码接口是否存在。
- 所有 scenario YAML 是否保持 `parameter_confidence: C`。

WSL/ROS2 下完整闭环：

```bash
bash tools/wsl_ros2_golden_contract_baseline.sh
```

该脚本会运行接口契约检查、scenario validator、mock contract check，并执行 001/002/002C 三条 golden regression，同时生成 HTML 可视化。

## 7. 当前仍未解决的问题

- `SafetySupervisor` 尚未成为强制命令链路。
- `MissionSupervisor` 的权限、VTS、最终作业许可仍是 mock。
- `route_plan` 仍主要在启动时生成，不是运行时 plan id/version 管理。
- `ShipDynamicsNode` 中存在 yaw clamp/reset 等保护逻辑，需要参数化并报告触发次数。
- 推进器 FDI 残差检测当前被临时禁用，故障场景不能证明真实 FDI。
- `ThrustAllocator` 内仍混合 QP 求解和推进策略，后续应抽象为 `PropulsionPolicy`。
