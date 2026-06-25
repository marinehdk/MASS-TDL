# mission_supervisor

## 目的

`mission_supervisor` 是 MASS ADAS 验证链路中的 shadow-mode 分层状态机。它把 A->B 航行拆成可观测的任务阶段和 gate 判断，但当前不直接修改制导、控制或推力分配输出。

包内同时包含 `captain_decision_node`。它是最小船长决策层骨架，把 mission、safety、environment、actuator capability 转换成可观测决策，不直接写入控制链路。

包内还包含 `propulsion_policy_node`。它是最小推进策略层骨架，把船长决策、任务阶段、航速、`/cmd_tau`、环境载荷和执行器能力转换成可观测推进使用约束。当前同样不直接修改推力分配输出。

`propulsion_policy_compliance_node` 是推进策略符合性检查骨架。它对比 `/propulsion/policy` 与实际 `/thruster/commands`，报告策略违规，但不控制执行器。

当前阶段的任务 gate 均来自 mock data，并在 `config/mission_gates.yaml` 中标注：

- `parameter_source: mock_data`
- `parameter_confidence: C`
- 后续接入真实航次计划、港口/VTS 许可、靠泊区和实船操纵数据时，目标是校准 gate 和验证门槛，而不是重写任务接口。

## 状态

- `PRECHECK`：等待最小导航观测。
- `CRUISE`：沿航线巡航。
- `DECEL`：接近终点前减速段。
- `REPORT`：报告点/许可 gate。
- `APPROACH`：进近 gate。
- `STANDBY`：待命/终点保持 gate。
- `BERTH_OR_WORK`：预留给靠泊或作业。
- `ABORT_ESCAPE`：安全监督器请求中止/撤离。
- `COMPLETE`：到达并满足终点速度 gate。

## 输入接口

- `/ship/odometry`：本船位置、速度和艏向。
- `/safety/abort_request`：安全监督器中止建议。
- `/safety/status`：安全状态 JSON。

## 输出接口

- `/mission/phase`：当前任务阶段。
- `/mission/active_gate`：当前 gate 的 JSON 说明，包括是否通过和未通过原因。
- `/mission/status`：任务阶段、航迹进度、横向偏差、终点距离、速度、安全状态和参数可信度。
- `/captain/decision`：船长化决策 JSON，包含 `wait_for_initial_observation`、`continue`、`reduce_speed`、`rejoin_corridor`、`hold_or_manual_handover`、`abort_escape_recommended`、`request_human_confirmation`。
- `/captain/recommended_speed`：船长决策层建议速度，单位 m/s，当前只用于观测。
- `/propulsion/policy`：推进策略 JSON，包含侧推是否允许、主桨是否要求对称、是否允许主桨差动、是否优先用舵、是否允许倒车等约束；其中 `main_propulsion_policy` 是独立的主桨对称策略对象，明确 required/released 区域、速度门槛、yaw/sway deadband、C 级 mock 参数来源和当前 advisory-only 执行阶段。
- `/propulsion/constraints`：推进策略的数值化约束数组。当前 `ThrustAllocator` 已接入最小硬约束：`side_allowed=false` 时侧推锁定，`reverse_allowed=false` 时主桨禁止倒车；`side_fraction` 只在配置允许的巡航效率区和速度门槛以上执行，4 m/s 以下转弯、重入、进近、DP 保持不做比例硬限。`ThrustAllocator` 在首次收到该 topic 前采用安全默认：侧推不可用、倒车不可用，避免启动瞬态先放出未授权推进指令；主桨对称/差动仍主要用于观测和后续扩展。
- `/propulsion/compliance`：推进策略符合性 JSON，报告实际推进器输出是否违反 shadow policy。
- `/propulsion/compliance_metrics`：推进策略符合性数值指标，当前供回归报告使用。

启动宽限期内输入尚未齐全时，`captain_decision_node` 发布 `wait_for_initial_observation`；超过宽限期仍缺少关键输入时，才升级为 `request_human_confirmation`。

`propulsion_policy_node` 把船长经验显式化。当前 `ThrustAllocator` 已消费三个低争议约束：高速/策略锁定侧推、未授权倒车锁定、舵优先区侧推比例限额。主桨对称策略已经抽成 `main_propulsion_policy` shadow/advisory 骨架，主桨对称、差动许可等约束仍保持观测状态，后续应先验证对称策略边界，再逐项接入分配器并回归。

倒车许可不是全局开关：`CRUISE` 阶段仍禁止倒车；`DECEL/APPROACH` 等计划制动阶段只有在 `/cmd_tau` 明确请求负向 surge 时才允许受控倒车。

`propulsion_policy_compliance_node` 的第一版用于回答“当前分配器是否违反了 shadow policy”。它检查高速侧推、主桨对称、未授权倒车和舵优先区侧推使用，为后续把策略真正接入分配器提供证据。

## 启动示例

```bash
ros2 launch mission_supervisor mission_supervisor.launch.py \
  scenario_file:=/mnt/d/02-dynamics/src/simulation/mock_scenarios/config/scenarios/001_straight_calm.yaml
```
