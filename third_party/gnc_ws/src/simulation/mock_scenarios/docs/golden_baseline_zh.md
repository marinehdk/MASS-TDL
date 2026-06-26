# 001 静水 Golden Baseline 闭环记录

## 目的

`001_straight_calm` 是后续场景开发的最小防退化基线。它只验证静水、直线、单航段、无故障条件下，当前闭环是否能够完成 A 点到 B 点航行。

该基线不证明系统已经满足真实船舶辅助驾驶要求；它只证明在最简单条件下，制导、控制、动力学、推力分配、安全监督器、任务状态机和验证观测器可以形成一条可运行、可观测、可回归的闭环。

## 固化的验收门禁

门禁配置位于 `config/validation/regression_gates.yaml`：

- `final_waypoint_reached = true`
- `max_cross_track_error_m <= 8.0`
- `max_heading_error_deg <= 3.0`
- `max_final_position_error_m <= 20.0`
- `max_final_speed_mps <= 0.25`
- `max_yaw_rate_deg_s <= 1.0`

场景文件 `config/scenarios/001_straight_calm.yaml` 也同步包含相同的核心期望，并显式标注：

- `data_policy.parameter_source: mock_data`
- `data_policy.parameter_confidence: C`
- 后续接入真实数据时，应进行参数校准和验证，而不是重写核心任务、安全、制导、控制或分配逻辑。

## 最近一次闭环证据

运行目录：

`D:\02-dynamics\reports\golden_baseline\001_straight_calm_20260517_204102`

关键输出：

- 场景验收：9 passed, 0 failed, 0 pending
- regression gate：6 passed, 0 failed
- 最终航点到达：true
- 到达时间：489.96232 s
- 到达半径：20.0 m
- 最大横向偏差：1.55674 m
- 最大艏向误差：1.8474 deg
- 最大艏摇角速度：0.1294884617 deg/s
- 终点位置误差：9.8500156795 m
- 终点速度：0.1497219289 m/s
- 导航故障检测：false
- 安全降级：false
- captain alert：0
- abort request：0
- safety 状态：NOMINAL
- mission 阶段已到达：COMPLETE

证据文件：

- `acceptance.txt`
- `acceptance.json`
- `metrics_merged.yaml`
- `regression_gates.json`
- `observability_metrics.yaml`
- `observability_events.jsonl`
- `ship_sim_20260517_204106.csv`

## 结论

本次闭环通过后，可以进入下一阶段场景开发，但必须保持以下规则：

1. 任何修复复杂场景前，先确认 `001_straight_calm` 通过。
2. 任何复杂场景修复后，再次运行 `tools/wsl_ros2_golden_baseline.sh`。
3. 如果 baseline 失败，优先修复基础闭环，不应继续调复杂环境、故障或靠泊场景。
4. 后续新增 mock 参数必须继续标注 C 级可信度，并提供真实数据替换路径。
