# safety_supervisor

## 目的

`safety_supervisor` 是船舶辅助驾驶验证链路中的影子模式安全监督器。它只发布安全状态、船长告警、建议限值和降级状态，不直接修改 `/cmd_tau` 或推进器命令。

当前阶段所有安全限值均来自 mock data，并在 `config/safety_limits.yaml` 中标注：

- `parameter_source: mock_data`
- `parameter_confidence: C`
- 后续接入实船数据时，目标是校准限值和验证门槛，而不是重写任务、安全、制导、控制或分配接口。

## 输入接口

- `/ship/odometry`：船舶位置、速度和艏摇角速度。
- `/cmd_tau`：控制器输出的广义力命令。
- `/env/total_load`：环境载荷合力。
- `/thruster/health_status`：推进器健康状态。

## 输出接口

- `/safety/status`：JSON 字符串，包含安全等级、触发原因、阶段、速度、艏摇角速度、环境载荷和参数可信度。
- `/safety/abort_request`：是否建议进入中止/撤离。
- `/captain/alert`：面向船长或操作员的告警 JSON。
- `/safety/command_limits`：当前阶段的建议速度、力和力矩限制。
- `/navigation/status`：导航输入是否过期。
- `/actuator/capability`：推进器健康数量、总数量和故障标志。

## 参数化配置

主要参数：

- `config_file`：安全限值 YAML。
- `shadow_mode`：是否只观测不介入。
- `phase`：当前任务阶段，例如 `default`、`cruise`、`approach`、`berthing`。
- `publish_rate_hz`：安全状态发布频率。
- `use_sim_time`：是否使用仿真时间。

启动示例：

```bash
ros2 launch safety_supervisor safety_supervisor.launch.py phase:=cruise shadow_mode:=true
```
