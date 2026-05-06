# M3 Mission Manager

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M3-001 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 详细设计 | `docs/Design/Detailed Design/M3-Mission-Manager/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M3/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §7 + §15.1 + RFC-006（ReplanResponse） |

## 角色

航次任务跟踪与重规划触发器。从 L1 接收 VoyageTask + 从 L2 接收 PlannedRoute / SpeedProfile，输出 MissionGoal 给 M4，并在需要时反向请求 L2 重规划（RouteReplanRequest）。

## 核心职责

- **VoyageTask 校验**：departure / destination / eta_window / mandatory_waypoints / exclusion_zones（8 校验规则）
- **ETA 投影**：分段线性积分（基于 PlannedRoute + SpeedProfile）
- **RouteReplanRequest 触发**：4 reason — ODD_EXIT / MISSION_INFEASIBLE / MRC_REQUIRED / CONGESTION
- **ReplanResponse 处理**（RFC-006）：4 status — SUCCESS / FAILED_TIMEOUT / FAILED_INFEASIBLE / FAILED_NO_RESOURCES
- **状态机 7 状态**：INIT / IDLE / TASK_VALIDATION / AWAITING_ROUTE / ACTIVE / REPLAN_WAIT / MRC_TRANSIT

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（3 publishers）：
- `/l3/m3/mission_goal` (l3_msgs/MissionGoal) — 0.5 Hz
- `/l3/m3/route_replan_request` (l3_msgs/RouteReplanRequest) — 事件
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz

**订阅**（6 subscribers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l1/voyage_task` (l3_external_msgs/VoyageTask) — 事件
- `/l2/planned_route` (l3_external_msgs/PlannedRoute) — 1 Hz / 事件
- `/l2/speed_profile` (l3_external_msgs/SpeedProfile) — 1 Hz / 事件
- `/l2/replan_response` (l3_external_msgs/ReplanResponse) — 事件 [RFC-006]

## 主要类（include/m3_mission_manager/）

- `VoyageTaskValidator` — 输入校验（8 规则）
- `EtaProjector` — ETA 投影逻辑
- `ReplanRequestTrigger` — 4 reason 触发逻辑 + 30s 重发限速
- `ReplanResponseHandler` — RFC-006 4 status 分支处理
- `MissionStateMachine` — 7 状态 FSM（含 invalid event 安全 nullopt 返回）
- `MissionManagerNode` — ROS2 节点 + DI 容器

## RFC-006 关键约束

- ReplanResponse SUCCESS → 状态机 REPLAN_WAIT → ACTIVE
- ReplanResponse FAILED_* → 状态机 REPLAN_WAIT → MRC_TRANSIT（升级 ToR_RequestMsg 给 M8）
- exclusion_zones 用 `geographic_msgs/GeoPath[]`（GeoJSON Polygon 风格）

## 配置（21 个 [TBD-HAZID] 标注）

- `config/m3_params.yaml`：
  - 重规划 deadline：MRC_REQUIRED 30s / ODD_EXIT critical 60s + degraded 120s / MISSION_INFEASIBLE 120s / CONGESTION 300s
  - ODD 阈值：0.7 / 0.3
  - 缓冲 1s + ETA 抽样 60s
  - 重发限速 30s
- `config/exclusion_zones_template.geojson` — GeoJSON Polygon 模板

## 开发命令

```bash
# Build
colcon build --packages-up-to m3_mission_manager \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Unit tests (5 测试文件 — Validator/EtaProjector/Trigger/Handler/StateMachine)
colcon test --packages-select m3_mission_manager
colcon test-result --verbose --test-result-base build/m3_mission_manager

# Coverage
lcov --capture --directory build/m3_mission_manager --output-file coverage.info

# Static analysis
run-clang-tidy-20 -p build/m3_mission_manager \
                  -config-file=.clang-tidy \
                  -warnings-as-errors='*' \
                  src/m3_mission_manager/src/*.cpp
```

## DoD（PATH-D）

参见 `docs/Implementation/M3/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 90%
- 8 校验规则 / 4 reason 触发 / 4 status 处理 / 7 状态转移全覆盖
- 边界值 / 优先级 / stale input / invalid event 用例
- 全部 [TBD-HAZID] 通过 YAML 注入
- nlohmann::json GeoJSON Polygon 序列化正确

## 库白名单（PATH-D）

- nlohmann::json 3.12.0（GeoJSON Polygon）
- GeographicLib 2.7（distance / bearing）
- Eigen 5.0.0
- spdlog 1.17.0 / fmt 11.0.2
- yaml-cpp 0.8.0
- GTest 1.17.0

## 引用

- **架构基线**：[v1.1.2 §7 M3 Mission Manager](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M3/01-detailed-design.md](../../docs/Design/Detailed%20Design/M3-Mission-Manager/01-detailed-design.md)
- **代码骨架 spec**：[M3/code-skeleton-spec.md](../../docs/Implementation/M3/code-skeleton-spec.md)
- **RFC-006 决议（ReplanResponse 协议）**：[RFC-decisions.md](../../docs/Design/Cross-Team%20Alignment/RFC-decisions.md)
