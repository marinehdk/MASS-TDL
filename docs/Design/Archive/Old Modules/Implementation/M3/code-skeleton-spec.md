# M3 Mission Manager 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M3-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 团队 | Team-M3（Mission Manager） |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 详细设计 | `docs/Design/Detailed Design/M3-Mission-Manager/01-detailed-design.md`（SANGO-ADAS-L3-DD-M3-001） |
| 架构基线 | v1.1.2 §7（M3 模块）+ §15.1（VoyageTask / RouteReplanRequest / ReplanResponseMsg / Mission_GoalMsg）+ §15.2（接口矩阵）+ RFC-006 决议 |
| 关联工程规范 | `00-implementation-master-plan.md` / `coding-standards.md` / `ros2-idl-implementation-guide.md` §3.3 / `third-party-library-policy.md` |

> **本文是 Team-M3 的代码起点。** 以下骨架须可直接 `colcon build` 通过（即使函数体为 TODO）；后续按详细设计 §5 算法填充实现。

---

## 1. 模块概述

### 1.1 模块定位

M3 Mission Manager 是 L3 战术决策层的**任务令看门人 + ETA 投影器 + 重规划触发器**，工作于**长时尺度（0.1–1 Hz）**，是 L3 内部对 L1 任务令的局部跟踪权威。

**核心职责**（详见详细设计 §1）：

1. **VoyageTask 校验**：接收 L1 VoyageTask，按 8 项几何 / 时间 / 枚举规则校验
2. **ETA 投影**：基于 M2 World_State + L2 PlannedRoute / SpeedProfile，分段线性积分投影到目标航点的到达时间
3. **RouteReplanRequest 触发**（4 reason）：ODD_EXIT / MISSION_INFEASIBLE / MRC_REQUIRED / CONGESTION
4. **ReplanResponseMsg 处理**（v1.1.2 RFC-006 新增）：处理 L2 回应的 SUCCESS / FAILED_TIMEOUT / FAILED_INFEASIBLE / FAILED_NO_RESOURCES 4 种状态
5. **Mission Goal 推送**：向 M4 发布 Mission_GoalMsg @ 0.5 Hz

### 1.2 边界（Out-of-Scope）

- M3 **不做**航次规划（属 L1 Mission Layer）
- M3 **不做**避碰决策（属 M4/M5/M6）
- M3 **不本地重规划**，仅向 L2 发起请求
- M3 **不维护**避碰相关状态（仅维护任务级状态）

### 1.3 PATH-D 强约束（继承 `coding-standards.md` §1）

- MISRA C++:2023 完整 179 规则强制（CI 阻断）
- clang-tidy + cppcheck Premium 全启用
- 单元测试覆盖率 ≥ 90%
- AddressSanitizer + ThreadSanitizer + UBSan 0 错误
- 禁用 `auto` 隐式推导（除 lambda / range-for / iterator）
- 禁用 C 风格转换、裸 `new/delete`、`std::any`、深模板递归（≤ 4）
- 函数 ≤ 60 行，循环复杂度 ≤ 10
- 浮点比较禁用 `==`，时间用 `std::chrono`，时间窗用 `int64_t` 纳秒戳

---

## 2. 项目目录结构

```
src/m3_mission_manager/
├── package.xml
├── CMakeLists.txt
├── README.md
│
├── include/m3_mission_manager/
│   ├── voyage_task_validator.hpp        # §4.1 校验器
│   ├── eta_projector.hpp                # §4.2 ETA 投影器
│   ├── replan_request_trigger.hpp       # §4.3 4-reason 触发器
│   ├── replan_response_handler.hpp      # §4.4 RFC-006 响应处理器
│   ├── mission_state_machine.hpp        # §4.5 状态机
│   ├── mission_manager_node.hpp         # §4.6 ROS2 节点
│   ├── error_codes.hpp                  # M3 = 3000–3999
│   └── types.hpp                        # 内部 POD 类型
│
├── src/
│   ├── voyage_task_validator.cpp
│   ├── eta_projector.cpp
│   ├── replan_request_trigger.cpp
│   ├── replan_response_handler.cpp
│   ├── mission_state_machine.cpp
│   ├── mission_manager_node.cpp
│   └── main.cpp                         # rclcpp::spin 入口
│
├── config/
│   ├── m3_params.yaml                   # §8.1 参数文件（HAZID 注入点）
│   └── exclusion_zones_template.geojson # §8.2 GeoJSON Polygon 模板
│
├── launch/
│   ├── m3_standalone.launch.py          # 单模块（mock 输入）
│   └── m3_with_mocks.launch.py          # 与 l3_external_msgs mock 联合
│
└── test/
    ├── unit/
    │   ├── test_voyage_task_validator.cpp     # §7.1
    │   ├── test_eta_projector.cpp             # §7.2
    │   ├── test_replan_request_trigger.cpp    # §7.3
    │   ├── test_replan_response_handler.cpp   # §7.4
    │   ├── test_mission_state_machine.cpp     # §7.5
    │   └── fixtures/
    │       ├── voyage_task_fixtures.hpp
    │       └── route_fixtures.hpp
    └── integration/
        ├── test_m3_node_lifecycle.cpp
        └── test_m3_e2e_flow.cpp           # VoyageTask → MissionGoal pipeline
```

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

M3 是单 ROS2 节点（`m3_mission_manager`），作为独立 OS 进程运行（`launch/m3_standalone.launch.py` 中 `executable=m3_mission_manager_node`）。

**节点内部组件**（依赖注入到 `MissionManagerNode`）：

```
MissionManagerNode（rclcpp::Node 子类）
├── VoyageTaskValidator        — 接 VoyageTask 订阅 callback
├── EtaProjector               — 周期 timer (0.5 Hz)
├── ReplanRequestTrigger       — ODD/ETA 监听 callback
├── ReplanResponseHandler      — ReplanResponse 订阅 callback
└── MissionStateMachine        — 全局协调（持有上述 4 个组件）
```

### 3.2 订阅者表格

| Topic | 类型（msg） | 频率 | QoS | Callback | 备注 |
|---|---|---|---|---|---|
| `/l1/voyage_task` | `l3_external_msgs/VoyageTask` | 事件 | `reliable + transient_local + keep_last(50)` | `on_voyage_task()` | 新任务令到达 → 校验 + 触发状态转移 |
| `/l2/planned_route` | `l3_external_msgs/PlannedRoute` | 1 Hz / 事件 | `reliable + keep_last(5)` | `on_planned_route()` | 校验与 VoyageTask.destination 一致性 |
| `/l2/speed_profile` | `l3_external_msgs/SpeedProfile` | 1 Hz / 事件 | `reliable + keep_last(5)` | `on_speed_profile()` | 喂入 ETA 投影 |
| `/l2/replan_response` | `l3_external_msgs/ReplanResponse` | 事件 | `reliable + transient_local + keep_last(50)` | `on_replan_response()` | **v1.1.2 RFC-006**；4 状态分支处理 |
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | 1 Hz + 事件 | `reliable + transient_local + keep_last(10)` | `on_odd_state()` | 监听 conformance_score → 触发重规划 |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | 4 Hz | `best_effort + keep_last(2)` (SensorDataQoS) | `on_world_state()` | 喂入 ETA 投影；超时 0.5s 降级 |

### 3.3 发布者表格

| Topic | 类型（msg） | 频率 | QoS | 触发 | 备注 |
|---|---|---|---|---|---|
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | 0.5 Hz | `reliable + keep_last(5)` | 周期 timer (2 s) + ETA 变化 > 5% 事件 | M4 订阅 |
| `/l3/m3/route_replan_request` | `l3_msgs/RouteReplanRequest` | 事件 | `reliable + transient_local + keep_last(50)` | 4 reason 之一触发 | L2 订阅 |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 事件 + 2 Hz | `reliable + transient_local + keep_last(100)` | 决策事件 + 心跳 | ASDR external 订阅 |

### 3.4 参数（YAML keys）

`config/m3_params.yaml` 中的所有 key，**不在源码硬编码**（[TBD-HAZID] 标注的须 RUN-001 后回填到 v1.1.3）：

| YAML key | 类型 | 默认 | 来源 | 说明 |
|---|---|---|---|---|
| `voyage_task.departure_distance_max_km` | double | 2.0 | 详细设计 §5.3.1 [HAZID 校准] | Departure 与当前位置最大距离 |
| `voyage_task.eta_window_min_s` | int64 | 600 | §5.3.1 [HAZID 校准] | ETA 窗口下限 10 min |
| `voyage_task.eta_window_max_s` | int64 | 259200 | §5.3.1 | ETA 窗口上限 72 hrs（运营约束）|
| `voyage_task.waypoint_max_distance_nm` | double | 50.0 | §5.3.1 | 航段最大距离（海图精度）|
| `voyage_task.exclusion_zone_buffer_m` | double | 500.0 | §5.3.1 | 排斥区与 departure 缓冲 |
| `eta.sampling_interval_s` | int32 | 60 | §5.3.2 [HAZID 校准] | 梯形积分时间步长 |
| `eta.forecast_horizon_max_s` | int64 | 3600 | §5.3.2 [HAZID 校准] | ETA 投影时域上限 1 hr |
| `eta.sea_current_uncertainty_kn` | double | 0.3 | §5.3.2 [HAZID 校准] | 海流估计不确定性 |
| `eta.world_state_age_threshold_s` | double | 0.5 | §7.3 [HAZID 校准] | World_State 新鲜度阈值 |
| `replan.deadline_mrc_required_s` | double | 30.0 | §5.3.3 [HAZID 校准] | MRC_REQUIRED 重规划 deadline |
| `replan.deadline_odd_exit_critical_s` | double | 60.0 | §5.3.3 [HAZID 校准] | ODD_EXIT critical deadline |
| `replan.deadline_odd_exit_degraded_s` | double | 120.0 | §5.3.3 [HAZID 校准] | ODD_EXIT degraded deadline |
| `replan.deadline_mission_infeasible_s` | double | 120.0 | §5.3.3 [HAZID 校准] | MISSION_INFEASIBLE deadline |
| `replan.deadline_congestion_s` | double | 300.0 | §5.3.3 [HAZID 校准] | CONGESTION deadline |
| `replan.attempt_max_count` | int32 | 3 | §7.3 [HAZID 校准] | 重规划重试上限（超出升级 ToR） |
| `odd.degraded_threshold` | double | 0.7 | §5.3.3 (架构 §3.6) | ODD DEGRADED 阈值 |
| `odd.critical_threshold` | double | 0.3 | §5.3.3 (架构 §3.6) | ODD CRITICAL 阈值 |
| `odd.degraded_buffer_s` | double | 1.0 | §5.3.3 [HAZID 校准] | DEGRADED 缓冲（防抖） |
| `eta.infeasible_margin_s` | double | 600.0 | §5.3.3 (10 min 接管时窗) | ETA 超期裕度 |
| `mission_goal.publish_rate_hz` | double | 0.5 | 详细设计 §3.1 | Mission_GoalMsg 发布频率 |
| `asdr.heartbeat_rate_hz` | double | 2.0 | 详细设计 §6.1 | ASDR 心跳频率 |
| `timeout.voyage_task_s` | double | 600.0 | §7.3 [HAZID 校准] | VoyageTask 超时阈值 |
| `timeout.planned_route_s` | double | 3.0 | §7.3 [HAZID 校准] | PlannedRoute 新鲜度 |
| `timeout.speed_profile_s` | double | 3.0 | §7.3 [HAZID 校准] | SpeedProfile 新鲜度 |
| `timeout.odd_state_s` | double | 2.0 | §7.3 [HAZID 校准] | ODD_StateMsg 新鲜度 |

### 3.5 状态机（v1.1.2 RFC-006 扩展）

详细设计 §4.2 的状态机经 RFC-006 ReplanResponse 接入扩展为以下 7 状态：

```
                          [*] (启动)
                           │
                           v
                       ┌── INIT ──┐
                       │          │
                  无任务 │          │ VoyageTask 接收
                       v          v
                       IDLE ─→ TASK_VALIDATION
                       ^             │
              校验失败 │校验失败       │ 校验通过
                       │             v
                       │       AWAITING_ROUTE
                       │             │
                       │  PlannedRoute + SpeedProfile 到达
                       │             v
                       │         ACTIVE ◀───────────────────────┐
                       │      (周期发布 Mission_GoalMsg @ 0.5 Hz) │
                       │             │                           │
                       │             ├── ODD < 0.7 + 缓冲 1s     │
                       │             │   或 ETA > eta_window+10m │
                       │             │   或 MRC_REQUIRED         │
                       │             │   或 CONGESTION           │
                       │             v                           │
                       │   REPLAN_WAIT (deadline 倒计时)         │
                       │   ───────┬─────────┬─────────┬─────     │
                       │ deadline │ResponseFAILED_*  │SUCCESS    │
                       │ 到 0     │INFEASIBLE        │           │
                       │          │NO_RESOURCES      │           │
                       │          │TIMEOUT           │           │
                       │          v                  │           │
                       │   MRC_TRANSIT◀───────       │           │
                       │   (升级 ToR_RequestMsg)     │           │
                       │          │                  │           │
                       │   接管完成│                  │           │
                       └──────────┘                  └───────────┘
                                  ▲
                                  │
                       conformance_score < 0.3
                       (CRITICAL 立即转移)
```

**关键转移条件（实施层硬约束）**：

| From | To | 触发条件 | 备注 |
|---|---|---|---|
| `INIT` | `IDLE` | 节点初始化完成、subscribers 就绪 | 首次发布 ASDR 心跳 |
| `IDLE` | `TASK_VALIDATION` | 接收 `/l1/voyage_task` | callback 内同步执行 |
| `TASK_VALIDATION` | `IDLE` | `VoyageTaskValidator::validate()` 返回 `false` | 同时发 ToR_RequestMsg(MANUAL_REQUEST) |
| `TASK_VALIDATION` | `AWAITING_ROUTE` | 校验通过 | 等待 L2 PlannedRoute |
| `AWAITING_ROUTE` | `ACTIVE` | 接收到 `/l2/planned_route` 且 destination 匹配 | 启动 ETA timer |
| `ACTIVE` | `REPLAN_WAIT` | `ReplanRequestTrigger::should_trigger() == true` | 4 reason 之一；调 `publish_replan_request()` 后转移 |
| `REPLAN_WAIT` | `ACTIVE` | 接收 `/l2/replan_response` 且 `status == STATUS_SUCCESS` 且新 PlannedRoute 到达 | RFC-006 SUCCESS 路径 |
| `REPLAN_WAIT` | `MRC_TRANSIT` | `ReplanResponse.status ∈ {FAILED_TIMEOUT, FAILED_INFEASIBLE, FAILED_NO_RESOURCES}` 或 deadline 倒计时到 0 | RFC-006 FAILED 路径，升级 ToR_RequestMsg |
| `MRC_TRANSIT` | `IDLE` | 接管完成（M8 通知 / 操作员清空任务） | 清空内部状态 |
| `ACTIVE` | `IDLE` | distance(current_pos, destination) < 50 m | 任务完成 |

**冷启动 / 容错**：
- 节点重启后从 `INIT` 进入；不持久化任务状态（重启等价于操作员重新下发任务）
- 异常退出（segfault / OOM）由 systemd / launch 重启策略处理

---

## 4. 核心类定义（C++ header 骨架）

> 所有 header 强制 `#pragma once`。包含顺序遵循 `coding-standards.md` §9.1。

### 4.1 VoyageTaskValidator

`include/m3_mission_manager/voyage_task_validator.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "m3_mission_manager/error_codes.hpp"

namespace mass_l3::m3 {

struct ValidationResult {
  bool is_valid;
  ErrorCode error_code;        // M3 error code (3000–3999)
  std::string failed_check;    // 校验失败的具体规则名（SAT-2 摘要用）
};

struct VoyageTaskValidatorConfig {
  double departure_distance_max_km;
  int64_t eta_window_min_s;
  int64_t eta_window_max_s;
  double waypoint_max_distance_nm;
  double exclusion_zone_buffer_m;
};

/**
 * @brief Validates VoyageTask per detailed design §5.3.1 (8 rules).
 *
 * Stateless except config. All checks are deterministic (no float ==).
 *
 * @note M3 PATH-D: All cyclomatic complexity ≤ 10; no exceptions thrown
 *       (returns ValidationResult instead).
 */
class VoyageTaskValidator {
 public:
  explicit VoyageTaskValidator(const VoyageTaskValidatorConfig& config);
  ~VoyageTaskValidator() = default;

  // Non-copyable, movable
  VoyageTaskValidator(const VoyageTaskValidator&) = delete;
  VoyageTaskValidator& operator=(const VoyageTaskValidator&) = delete;
  VoyageTaskValidator(VoyageTaskValidator&&) = default;
  VoyageTaskValidator& operator=(VoyageTaskValidator&&) = default;

  /**
   * @brief Run all 8 validation checks against the given VoyageTask.
   *
   * @param task                Incoming task from L1.
   * @param current_position    Own ship position at receive time (WGS84).
   * @param current_time_ns     UTC nanoseconds at receive time.
   * @return ValidationResult with is_valid + error_code + failed_check.
   */
  ValidationResult validate(const l3_external_msgs::msg::VoyageTask& task,
                            const geographic_msgs::msg::GeoPoint& current_position,
                            int64_t current_time_ns) const;

 private:
  // Sub-checks (each ≤ 40 lines per coding-standards §3.5)
  ValidationResult check_timestamp(int64_t task_stamp_ns, int64_t now_ns) const;
  ValidationResult check_departure_distance(
      const geographic_msgs::msg::GeoPoint& departure,
      const geographic_msgs::msg::GeoPoint& current_position) const;
  ValidationResult check_destination(const geographic_msgs::msg::GeoPoint& destination) const;
  ValidationResult check_eta_window(int64_t earliest_ns, int64_t latest_ns,
                                    int64_t now_ns) const;
  ValidationResult check_mandatory_waypoints(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints) const;
  ValidationResult check_exclusion_zones(
      const l3_external_msgs::msg::VoyageTask& task) const;
  ValidationResult check_optimization_priority(const std::string& priority) const;

  // Geometric helpers (deterministic; no exceptions)
  static double haversine_distance_m(const geographic_msgs::msg::GeoPoint& a,
                                     const geographic_msgs::msg::GeoPoint& b);
  static bool is_valid_wgs84(const geographic_msgs::msg::GeoPoint& p);
  static bool point_in_polygon(const geographic_msgs::msg::GeoPoint& p,
                               const std::vector<geographic_msgs::msg::GeoPoint>& polygon);

  VoyageTaskValidatorConfig config_;
};

}  // namespace mass_l3::m3
```

### 4.2 EtaProjector

`include/m3_mission_manager/eta_projector.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m3 {

struct EtaResult {
  double eta_to_target_s;        // -1 if unavailable
  double confidence;             // [0, 1]
  std::string method;            // "piecewise_linear_integration" | "extrapolated" | "unavailable"
  double world_state_age_s;      // input freshness (SAT-2)
  double distance_remaining_m;   // for SAT-2 rationale
};

struct EtaProjectorConfig {
  int32_t sampling_interval_s;
  int64_t forecast_horizon_max_s;
  double sea_current_uncertainty_kn;
  double world_state_age_threshold_s;
};

/**
 * @brief Piecewise linear path integration ETA projection per §5.3.2.
 *
 * Deterministic; no random; no exceptions. Returns {-1, 0.0} when input is
 * unavailable (caller decides ASDR record + degradation).
 *
 * @note Time complexity O(n + horizon/dt); typical n ≤ 20 waypoints.
 */
class EtaProjector {
 public:
  explicit EtaProjector(const EtaProjectorConfig& config);
  ~EtaProjector() = default;

  // Non-copyable
  EtaProjector(const EtaProjector&) = delete;
  EtaProjector& operator=(const EtaProjector&) = delete;
  EtaProjector(EtaProjector&&) = default;
  EtaProjector& operator=(EtaProjector&&) = default;

  /**
   * @brief Compute ETA from current position to last waypoint of planned route.
   *
   * @param planned_route   L2 latest waypoint sequence.
   * @param speed_profile   L2 segmented speed schedule.
   * @param world_state     M2 latest snapshot (for own-ship + sea current).
   * @param now_ns          UTC nanoseconds at call time.
   * @return EtaResult with eta_to_target_s + confidence + method.
   */
  EtaResult project(const l3_external_msgs::msg::PlannedRoute& planned_route,
                    const l3_external_msgs::msg::SpeedProfile& speed_profile,
                    const l3_msgs::msg::WorldState& world_state,
                    int64_t now_ns) const;

 private:
  // Internal helpers (each ≤ 40 lines)
  std::vector<geographic_msgs::msg::GeoPoint> select_remaining_waypoints(
      const geographic_msgs::msg::GeoPoint& current_pos,
      const std::vector<geographic_msgs::msg::GeoPoint>& route_waypoints) const;

  double compute_total_distance_m(const geographic_msgs::msg::GeoPoint& current_pos,
                                  const std::vector<geographic_msgs::msg::GeoPoint>& wps) const;

  double integrate_eta_seconds(double total_distance_m, double current_sog_kn,
                               const l3_external_msgs::msg::SpeedProfile& profile,
                               double sea_current_along_track_kn) const;

  double compute_confidence(double world_state_confidence, size_t remaining_wp_count,
                            double world_state_age_s) const;

  EtaProjectorConfig config_;
};

}  // namespace mass_l3::m3
```

### 4.3 ReplanRequestTrigger

`include/m3_mission_manager/replan_request_trigger.hpp`：

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"

namespace mass_l3::m3 {

enum class ReplanReason : uint8_t {
  kOddExit = 0,                  // ODD_StateMsg.conformance_score < threshold
  kMissionInfeasible = 1,        // ETA > eta_window.latest + margin
  kMrcRequired = 2,              // M7 SafetyAlert with SEVERITY_MRC_REQUIRED
  kCongestion = 3,               // Future: congestion replanning (placeholder)
};

struct TriggerInput {
  std::optional<l3_msgs::msg::ODDState> latest_odd;
  double current_eta_s;          // -1 if unavailable
  int64_t eta_window_latest_ns;
  double eta_infeasible_margin_s;
  bool mrc_requested;            // From M7 SafetyAlert subscription
  bool congestion_detected;      // Future hook
  int64_t now_ns;
  int64_t degraded_first_seen_ns;  // For 1s buffer; -1 if not yet
};

struct TriggerOutput {
  bool should_trigger;
  ReplanReason reason;
  double deadline_s;             // Per reason × severity matrix
  std::string context_summary;   // SAT-2 summary
};

struct ReplanTriggerConfig {
  double odd_degraded_threshold;           // 0.7
  double odd_critical_threshold;           // 0.3
  double odd_degraded_buffer_s;            // 1.0
  double deadline_mrc_required_s;          // 30.0
  double deadline_odd_exit_critical_s;     // 60.0
  double deadline_odd_exit_degraded_s;     // 120.0
  double deadline_mission_infeasible_s;    // 120.0
  double deadline_congestion_s;            // 300.0
};

/**
 * @brief Decision tree for 4-reason replan request triggering per §5.3.3.
 *
 * Stateless except config + caller-supplied buffer timer (degraded_first_seen_ns).
 * Priority order (highest first): MRC_REQUIRED > ODD_EXIT(critical) > ODD_EXIT(degraded)
 * > MISSION_INFEASIBLE > CONGESTION.
 */
class ReplanRequestTrigger {
 public:
  explicit ReplanRequestTrigger(const ReplanTriggerConfig& config);
  ~ReplanRequestTrigger() = default;

  ReplanRequestTrigger(const ReplanRequestTrigger&) = delete;
  ReplanRequestTrigger& operator=(const ReplanRequestTrigger&) = delete;
  ReplanRequestTrigger(ReplanRequestTrigger&&) = default;
  ReplanRequestTrigger& operator=(ReplanRequestTrigger&&) = default;

  /**
   * @brief Evaluate trigger conditions and return decision.
   *
   * @return TriggerOutput.should_trigger == true if any reason fires.
   *         deadline_s is selected per reason × severity.
   */
  TriggerOutput evaluate(const TriggerInput& input) const;

 private:
  // Each reason check returns std::optional<TriggerOutput> (nullopt = not fired)
  std::optional<TriggerOutput> check_mrc_required(const TriggerInput& input) const;
  std::optional<TriggerOutput> check_odd_exit(const TriggerInput& input) const;
  std::optional<TriggerOutput> check_mission_infeasible(const TriggerInput& input) const;
  std::optional<TriggerOutput> check_congestion(const TriggerInput& input) const;

  ReplanTriggerConfig config_;
};

}  // namespace mass_l3::m3
```

### 4.4 ReplanResponseHandler（v1.1.2 RFC-006 新增）

`include/m3_mission_manager/replan_response_handler.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <string>

#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_msgs/msg/to_r_request.hpp"

namespace mass_l3::m3 {

enum class ReplanResponseAction : uint8_t {
  kAcceptNewRoute = 0,         // SUCCESS → wait for new PlannedRoute, transition ACTIVE
  kRetry = 1,                  // FAILED_TIMEOUT + retry_recommended==true → re-send request
  kEscalateToTor = 2,          // FAILED_INFEASIBLE / FAILED_NO_RESOURCES / max retry → ToR_Request
  kIgnore = 3,                 // Stale / mismatched response (SAT-2 logged)
};

struct ResponseDecision {
  ReplanResponseAction action;
  std::string sat2_summary;
  // For kEscalateToTor:
  std::string tor_reason_code;    // "ODD_EXIT" | "SAFETY_ALERT" | "MANUAL_REQUEST"
  double tor_deadline_s;
};

struct ReplanResponseHandlerConfig {
  int32_t replan_attempt_max_count;
};

/**
 * @brief Process L2 ReplanResponse per RFC-006 (4 status branches).
 *
 * Status mapping:
 *   STATUS_SUCCESS              → kAcceptNewRoute (transition REPLAN_WAIT → ACTIVE on next PlannedRoute)
 *   STATUS_FAILED_TIMEOUT       → kRetry if attempts < max && retry_recommended; else kEscalateToTor
 *   STATUS_FAILED_INFEASIBLE    → kEscalateToTor (no replan possible, operator must take over)
 *   STATUS_FAILED_NO_RESOURCES  → kEscalateToTor (L2 cannot allocate; operator decides)
 *
 * @note Stateless except config + caller-passed attempt_count.
 */
class ReplanResponseHandler {
 public:
  explicit ReplanResponseHandler(const ReplanResponseHandlerConfig& config);
  ~ReplanResponseHandler() = default;

  ReplanResponseHandler(const ReplanResponseHandler&) = delete;
  ReplanResponseHandler& operator=(const ReplanResponseHandler&) = delete;
  ReplanResponseHandler(ReplanResponseHandler&&) = default;
  ReplanResponseHandler& operator=(ReplanResponseHandler&&) = default;

  /**
   * @brief Decide what to do upon receiving ReplanResponse.
   *
   * @param response          L2 reply.
   * @param attempt_count     Caller-tracked retry count.
   * @return ResponseDecision with action + downstream parameters.
   */
  ResponseDecision handle(const l3_external_msgs::msg::ReplanResponse& response,
                          int32_t attempt_count) const;

 private:
  ResponseDecision handle_success() const;
  ResponseDecision handle_failed_timeout(const l3_external_msgs::msg::ReplanResponse& r,
                                         int32_t attempt_count) const;
  ResponseDecision handle_failed_infeasible(
      const l3_external_msgs::msg::ReplanResponse& r) const;
  ResponseDecision handle_failed_no_resources(
      const l3_external_msgs::msg::ReplanResponse& r) const;

  ReplanResponseHandlerConfig config_;
};

}  // namespace mass_l3::m3
```

### 4.5 MissionStateMachine

`include/m3_mission_manager/mission_state_machine.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "m3_mission_manager/replan_request_trigger.hpp"

namespace mass_l3::m3 {

enum class MissionState : uint8_t {
  kInit = 0,
  kIdle = 1,
  kTaskValidation = 2,
  kAwaitingRoute = 3,
  kActive = 4,
  kReplanWait = 5,
  kMrcTransit = 6,
};

const char* to_string(MissionState s);  // For logging / ASDR

struct StateTransitionEvent {
  MissionState from;
  MissionState to;
  std::string trigger_reason;  // SAT-2 summary
  int64_t stamp_ns;
};

/**
 * @brief Mission Manager state machine per §3.5 (post-RFC-006).
 *
 * Single-threaded; all callbacks dispatch through the same executor.
 * Transitions are explicit (no implicit/silent transitions).
 *
 * @note PATH-D: cyclomatic complexity per `tick()` ≤ 10; pure switch dispatch.
 */
class MissionStateMachine {
 public:
  MissionStateMachine();
  ~MissionStateMachine() = default;

  MissionState current_state() const { return state_; }
  int64_t state_entered_ns() const { return state_entered_ns_; }

  // Event handlers — each returns the resulting transition (if any)
  std::optional<StateTransitionEvent> on_voyage_task_received(int64_t now_ns);
  std::optional<StateTransitionEvent> on_voyage_task_validated(bool valid, int64_t now_ns);
  std::optional<StateTransitionEvent> on_planned_route_received(bool destination_match,
                                                                 int64_t now_ns);
  std::optional<StateTransitionEvent> on_replan_trigger(ReplanReason reason, int64_t now_ns);
  std::optional<StateTransitionEvent> on_replan_response(
      const l3_external_msgs::msg::ReplanResponse& response, int64_t now_ns);
  std::optional<StateTransitionEvent> on_replan_deadline_expired(int64_t now_ns);
  std::optional<StateTransitionEvent> on_mission_complete(int64_t now_ns);
  std::optional<StateTransitionEvent> on_takeover_complete(int64_t now_ns);

  /**
   * @brief Periodic tick (called from 0.5 Hz timer).
   *
   * Used to evaluate timeout-driven transitions (e.g. REPLAN_WAIT deadline).
   */
  std::optional<StateTransitionEvent> tick(int64_t now_ns,
                                           int64_t replan_deadline_remaining_s);

 private:
  std::optional<StateTransitionEvent> transition(MissionState to, std::string reason,
                                                  int64_t now_ns);

  MissionState state_;
  int64_t state_entered_ns_;
};

}  // namespace mass_l3::m3
```

### 4.6 MissionManagerNode

`include/m3_mission_manager/mission_manager_node.hpp`：

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"
#include "l3_msgs/msg/world_state.hpp"

#include "m3_mission_manager/eta_projector.hpp"
#include "m3_mission_manager/mission_state_machine.hpp"
#include "m3_mission_manager/replan_request_trigger.hpp"
#include "m3_mission_manager/replan_response_handler.hpp"
#include "m3_mission_manager/voyage_task_validator.hpp"

namespace mass_l3::m3 {

/**
 * @brief Main ROS2 node for M3 Mission Manager.
 *
 * Wraps 5 components (Validator / Projector / Trigger / ResponseHandler / StateMachine)
 * + 6 subscribers / 3 publishers / 2 timers. All callbacks single-threaded
 * (default rclcpp executor).
 */
class MissionManagerNode : public rclcpp::Node {
 public:
  MissionManagerNode();
  ~MissionManagerNode() override = default;

  MissionManagerNode(const MissionManagerNode&) = delete;
  MissionManagerNode& operator=(const MissionManagerNode&) = delete;

 private:
  // ---- Initialization ----
  void declare_parameters();
  void load_config();
  void create_publishers();
  void create_subscribers();
  void create_timers();

  // ---- Subscriber callbacks ----
  void on_voyage_task(const l3_external_msgs::msg::VoyageTask::SharedPtr msg);
  void on_planned_route(const l3_external_msgs::msg::PlannedRoute::SharedPtr msg);
  void on_speed_profile(const l3_external_msgs::msg::SpeedProfile::SharedPtr msg);
  void on_replan_response(const l3_external_msgs::msg::ReplanResponse::SharedPtr msg);
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg);

  // ---- Periodic timer callbacks ----
  void on_mission_goal_tick();        // 0.5 Hz
  void on_asdr_heartbeat_tick();      // 2 Hz

  // ---- Internal helpers ----
  void publish_mission_goal();
  void publish_replan_request(ReplanReason reason, double deadline_s,
                              const std::string& context);
  void publish_tor_request(const std::string& reason_code, double deadline_s,
                           const std::string& context);
  void publish_asdr_record(const std::string& decision_type,
                           const std::string& decision_json);
  void log_state_transition(const StateTransitionEvent& evt);
  int64_t now_ns() const;

  // ---- Components (DI) ----
  std::unique_ptr<VoyageTaskValidator> validator_;
  std::unique_ptr<EtaProjector> projector_;
  std::unique_ptr<ReplanRequestTrigger> trigger_;
  std::unique_ptr<ReplanResponseHandler> response_handler_;
  std::unique_ptr<MissionStateMachine> state_machine_;

  // ---- ROS2 entities ----
  rclcpp::Subscription<l3_external_msgs::msg::VoyageTask>::SharedPtr voyage_task_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr planned_route_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr speed_profile_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::ReplanResponse>::SharedPtr replan_response_sub_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_state_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_state_sub_;

  rclcpp::Publisher<l3_msgs::msg::MissionGoal>::SharedPtr mission_goal_pub_;
  rclcpp::Publisher<l3_msgs::msg::RouteReplanRequest>::SharedPtr replan_request_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;

  rclcpp::TimerBase::SharedPtr mission_goal_timer_;
  rclcpp::TimerBase::SharedPtr asdr_heartbeat_timer_;

  // ---- Cached state (latest message snapshots; protected by single-thread executor) ----
  std::optional<l3_external_msgs::msg::VoyageTask> latest_voyage_task_;
  std::optional<l3_external_msgs::msg::PlannedRoute> latest_planned_route_;
  std::optional<l3_external_msgs::msg::SpeedProfile> latest_speed_profile_;
  std::optional<l3_msgs::msg::ODDState> latest_odd_state_;
  std::optional<l3_msgs::msg::WorldState> latest_world_state_;

  int64_t voyage_task_stamp_ns_ = -1;
  int64_t planned_route_stamp_ns_ = -1;
  int64_t speed_profile_stamp_ns_ = -1;
  int64_t odd_state_stamp_ns_ = -1;
  int64_t world_state_stamp_ns_ = -1;

  int64_t replan_deadline_ns_ = -1;          // -1 if not in REPLAN_WAIT
  int64_t degraded_first_seen_ns_ = -1;      // For 1s ODD DEGRADED buffer
  int32_t replan_attempt_count_ = 0;
};

}  // namespace mass_l3::m3
```

`src/main.cpp`：

```cpp
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include "m3_mission_manager/mission_manager_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m3::MissionManagerNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
```

---

## 5. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(m3_mission_manager LANGUAGES CXX)

# ---- Standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Compile options (per coding-standards.md §2.2) ----
add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual
  -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion
  -Wfloat-equal
  -Wformat=2 -Wformat-security
  -Wmissing-declarations
  -Wundef -Wunused
  -Wuseless-cast
  -Wlogical-op
  -Wduplicated-cond -Wduplicated-branches
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
)

# Sanitizers in Debug
add_compile_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)
add_link_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# Release uses -O2 (NOT -O3)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# ---- Dependencies ----
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 5.0.0 EXACT REQUIRED)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(nlohmann_json 3.12.0 EXACT REQUIRED)
find_package(GeographicLib 2.7 EXACT REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)

# ---- Library: m3 components (testable independently) ----
add_library(${PROJECT_NAME}_core STATIC
  src/voyage_task_validator.cpp
  src/eta_projector.cpp
  src/replan_request_trigger.cpp
  src/replan_response_handler.cpp
  src/mission_state_machine.cpp
)
target_include_directories(${PROJECT_NAME}_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}_core PUBLIC
  Eigen3::Eigen
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  ${GeographicLib_LIBRARIES}
  yaml-cpp
)
ament_target_dependencies(${PROJECT_NAME}_core PUBLIC
  rclcpp
  builtin_interfaces
  geographic_msgs
  l3_msgs
  l3_external_msgs
)

# ---- Executable: ROS2 node ----
add_executable(${PROJECT_NAME}_node
  src/mission_manager_node.cpp
  src/main.cpp
)
target_link_libraries(${PROJECT_NAME}_node PRIVATE ${PROJECT_NAME}_core)
ament_target_dependencies(${PROJECT_NAME}_node PUBLIC rclcpp)

install(TARGETS ${PROJECT_NAME}_node
        DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config
        DESTINATION share/${PROJECT_NAME})

# ---- Testing ----
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_voyage_task_validator
    test/unit/test_voyage_task_validator.cpp)
  target_link_libraries(test_voyage_task_validator ${PROJECT_NAME}_core)

  ament_add_gtest(test_eta_projector
    test/unit/test_eta_projector.cpp)
  target_link_libraries(test_eta_projector ${PROJECT_NAME}_core)

  ament_add_gtest(test_replan_request_trigger
    test/unit/test_replan_request_trigger.cpp)
  target_link_libraries(test_replan_request_trigger ${PROJECT_NAME}_core)

  ament_add_gtest(test_replan_response_handler
    test/unit/test_replan_response_handler.cpp)
  target_link_libraries(test_replan_response_handler ${PROJECT_NAME}_core)

  ament_add_gtest(test_mission_state_machine
    test/unit/test_mission_state_machine.cpp)
  target_link_libraries(test_mission_state_machine ${PROJECT_NAME}_core)
endif()

ament_package()
```

---

## 6. package.xml

```xml
<?xml version="1.0"?>
<package format="3">
  <name>m3_mission_manager</name>
  <version>0.1.0</version>
  <description>
    M3 Mission Manager — VoyageTask validation, ETA projection,
    RouteReplanRequest triggering (4 reasons), ReplanResponse handling
    (RFC-006), Mission Goal publishing. PATH-D / MISRA C++:2023.
  </description>
  <maintainer email="m3-team@sango-l3.example">Team-M3</maintainer>
  <license>Proprietary (Sango ADAS L3 internal)</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>builtin_interfaces</depend>
  <depend>geographic_msgs</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>eigen</depend>
  <depend>spdlog</depend>
  <depend>nlohmann-json</depend>
  <depend>geographiclib</depend>
  <depend>yaml-cpp</depend>

  <test_depend>ament_cmake_gtest</test_depend>
  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_lint_common</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 7. 单元测试骨架（GTest）

> 全部使用 `ament_add_gtest`；覆盖率目标 **≥ 90%**（lcov / gcovr 报告）。每个测试文件 ≤ 800 行；fixture 共享放 `test/unit/fixtures/`。

### 7.1 VoyageTaskValidator 测试

`test/unit/test_voyage_task_validator.cpp`：

```cpp
#include <gtest/gtest.h>

#include "m3_mission_manager/voyage_task_validator.hpp"
#include "test/unit/fixtures/voyage_task_fixtures.hpp"

namespace mass_l3::m3 {
namespace {

class VoyageTaskValidatorTest : public ::testing::Test {
 protected:
  VoyageTaskValidatorConfig default_config() {
    return VoyageTaskValidatorConfig{
        /*departure_distance_max_km=*/2.0,
        /*eta_window_min_s=*/600,
        /*eta_window_max_s=*/259200,
        /*waypoint_max_distance_nm=*/50.0,
        /*exclusion_zone_buffer_m=*/500.0};
  }
};

// ---- Legal task acceptance ----
TEST_F(VoyageTaskValidatorTest, LegalTaskPasses) {
  VoyageTaskValidator v(default_config());
  auto task = fixtures::make_legal_task();
  auto pos = fixtures::current_position_near_departure();
  auto result = v.validate(task, pos, fixtures::now_ns());
  EXPECT_TRUE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::kOk);
}

// ---- 8 rule rejections ----
TEST_F(VoyageTaskValidatorTest, RejectsTimestampInFuture) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsDepartureTooFar) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsInvalidDestinationLat) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsInvalidDestinationLon) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsEtaWindowEarliestInPast) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsEtaWindowInverted) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsEtaWindowTooNarrow) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsEtaWindowTooLong) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsWaypointDistanceExcessive) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsExclusionZoneInvalidPolygon) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsExclusionZoneConflictsDeparture) { /* ... */ }
TEST_F(VoyageTaskValidatorTest, RejectsInvalidOptimizationPriority) { /* ... */ }

// ---- Boundary cases ----
TEST_F(VoyageTaskValidatorTest, EtaWindowExactlyMin) { /* 600s 边界 */ }
TEST_F(VoyageTaskValidatorTest, EtaWindowExactlyMax) { /* 259200s 边界 */ }
TEST_F(VoyageTaskValidatorTest, DepartureExactly2km) { /* 边界 */ }

}  // namespace
}  // namespace mass_l3::m3
```

**覆盖目标**：8 个校验规则各至少 1 个 PASS + 1 个 FAIL + 边界值。

### 7.2 EtaProjector 测试

`test/unit/test_eta_projector.cpp`：

```cpp
#include <gtest/gtest.h>
#include "m3_mission_manager/eta_projector.hpp"

namespace mass_l3::m3 {
namespace {

class EtaProjectorTest : public ::testing::Test { /* ... config */ };

TEST_F(EtaProjectorTest, StableSpeedKnownDistance) {
  // sog=12.5kn, distance=60nm, no current → eta ≈ 4.8 hrs (±5%)
  /* ... */
}

TEST_F(EtaProjectorTest, AccelerationSegmentReducesEta) { /* ... */ }
TEST_F(EtaProjectorTest, DecelerationSegmentIncreasesEta) { /* ... */ }
TEST_F(EtaProjectorTest, AlongTrackCurrentReducesEta) { /* +1kn current */ }
TEST_F(EtaProjectorTest, AgainstCurrentIncreasesEta) { /* -1kn current */ }
TEST_F(EtaProjectorTest, ReachedDestinationReturnsMinusOne) { /* dist < 50m */ }
TEST_F(EtaProjectorTest, EmptyRouteReturnsMinusOne) { /* 0 waypoints */ }
TEST_F(EtaProjectorTest, ForecastHorizonClipsEta) { /* > 1h horizon */ }
TEST_F(EtaProjectorTest, StaleWorldStateReducesConfidence) {
  // age = 1.5s → confidence < 0.7 (per §5.3.2 confidence rule)
}
TEST_F(EtaProjectorTest, ConfidenceMonotonicityWaypointCount) {
  // 3 wps confidence > 7 wps confidence
}

}  // namespace
}  // namespace mass_l3::m3
```

### 7.3 ReplanRequestTrigger 测试（4 reason）

`test/unit/test_replan_request_trigger.cpp`：

```cpp
namespace mass_l3::m3 {
namespace {

class ReplanRequestTriggerTest : public ::testing::Test { /* ... */ };

// ---- ODD_EXIT ----
TEST_F(ReplanRequestTriggerTest, OddExitCriticalImmediateTrigger) {
  // conformance_score = 0.2 → reason=ODD_EXIT, deadline=60s
}

TEST_F(ReplanRequestTriggerTest, OddExitDegradedRequiresBuffer) {
  // conformance_score = 0.5 first time → no trigger (buffer not elapsed)
  // 1.5s later, still 0.5 → trigger reason=ODD_EXIT, deadline=120s
}

TEST_F(ReplanRequestTriggerTest, OddNormalNoTrigger) {
  // conformance_score = 0.85 → no trigger
}

// ---- MISSION_INFEASIBLE ----
TEST_F(ReplanRequestTriggerTest, MissionInfeasibleEtaOverdue) {
  // current_eta_s > eta_window_latest_ns + 600s margin → trigger
  // reason=MISSION_INFEASIBLE, deadline=120s
}

TEST_F(ReplanRequestTriggerTest, MissionInfeasibleNotTriggeredWithinMargin) {
  // current_eta_s = window_latest + 300s (within 10min margin) → no trigger
}

// ---- MRC_REQUIRED ----
TEST_F(ReplanRequestTriggerTest, MrcRequiredHighestPriority) {
  // mrc_requested=true AND odd<0.3 → reason=MRC_REQUIRED (priority over ODD_EXIT)
  // deadline=30s
}

// ---- CONGESTION ----
TEST_F(ReplanRequestTriggerTest, CongestionTriggersWithLowestDeadline) {
  // congestion_detected=true → reason=CONGESTION, deadline=300s
}

// ---- Priority ordering ----
TEST_F(ReplanRequestTriggerTest, PriorityOrder_MrcOverOddOverInfeasibleOverCongestion) {
  // 全 true → expects MRC
}

}  // namespace
}  // namespace mass_l3::m3
```

### 7.4 ReplanResponseHandler 测试（4 status）

`test/unit/test_replan_response_handler.cpp`：

```cpp
namespace mass_l3::m3 {
namespace {

TEST(ReplanResponseHandlerTest, SuccessAcceptsNewRoute) {
  // status=STATUS_SUCCESS → action=kAcceptNewRoute
}

TEST(ReplanResponseHandlerTest, FailedTimeoutWithRetryRecommendedRetries) {
  // status=FAILED_TIMEOUT, retry_recommended=true, attempts=1<3 → kRetry
}

TEST(ReplanResponseHandlerTest, FailedTimeoutMaxAttemptsEscalates) {
  // status=FAILED_TIMEOUT, attempts=3 → kEscalateToTor
}

TEST(ReplanResponseHandlerTest, FailedTimeoutNoRetryEscalates) {
  // status=FAILED_TIMEOUT, retry_recommended=false → kEscalateToTor
}

TEST(ReplanResponseHandlerTest, FailedInfeasibleEscalatesToTor) {
  // status=FAILED_INFEASIBLE → kEscalateToTor (regardless of attempts)
  // tor_reason_code="ODD_EXIT" or "SAFETY_ALERT"
}

TEST(ReplanResponseHandlerTest, FailedNoResourcesEscalatesToTor) {
  // status=FAILED_NO_RESOURCES → kEscalateToTor
}

TEST(ReplanResponseHandlerTest, FailedInfeasibleSurfaceFailureReason) {
  // failure_reason field surfaces in sat2_summary
}

}  // namespace
}  // namespace mass_l3::m3
```

### 7.5 MissionStateMachine 测试

`test/unit/test_mission_state_machine.cpp`：

```cpp
namespace mass_l3::m3 {
namespace {

class StateMachineTest : public ::testing::Test {
 protected:
  MissionStateMachine sm;
};

// ---- Linear happy path ----
TEST_F(StateMachineTest, InitToIdleOnReady) {
  EXPECT_EQ(sm.current_state(), MissionState::kInit);
  // boot complete → kIdle
}

TEST_F(StateMachineTest, IdleToValidationOnTask) {
  sm.on_voyage_task_received(/*now=*/100);
  EXPECT_EQ(sm.current_state(), MissionState::kTaskValidation);
}

TEST_F(StateMachineTest, ValidationToAwaitingOnValid) { /* ... */ }
TEST_F(StateMachineTest, ValidationToIdleOnInvalid) { /* ... */ }
TEST_F(StateMachineTest, AwaitingToActiveOnRoute) { /* ... */ }

// ---- Replan flow (RFC-006 happy path) ----
TEST_F(StateMachineTest, ActiveToReplanWaitOnTrigger) {
  // Simulate trigger reason=ODD_EXIT → kReplanWait
}

TEST_F(StateMachineTest, ReplanWaitToActiveOnSuccess) {
  l3_external_msgs::msg::ReplanResponse r;
  r.status = l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS;
  sm.on_replan_response(r, /*now=*/200);
  EXPECT_EQ(sm.current_state(), MissionState::kActive);
}

// ---- Replan flow (RFC-006 sad paths) ----
TEST_F(StateMachineTest, ReplanWaitToMrcOnFailedInfeasible) {
  l3_external_msgs::msg::ReplanResponse r;
  r.status = l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_INFEASIBLE;
  sm.on_replan_response(r, /*now=*/200);
  EXPECT_EQ(sm.current_state(), MissionState::kMrcTransit);
}

TEST_F(StateMachineTest, ReplanWaitToMrcOnFailedNoResources) { /* ... */ }
TEST_F(StateMachineTest, ReplanWaitToMrcOnFailedTimeout) { /* ... */ }
TEST_F(StateMachineTest, ReplanWaitToMrcOnDeadlineExpired) {
  // deadline_remaining_s = 0 → kMrcTransit
}

// ---- Mission completion ----
TEST_F(StateMachineTest, ActiveToIdleOnComplete) { /* ... */ }
TEST_F(StateMachineTest, MrcToIdleOnTakeover) { /* ... */ }

// ---- Invalid transitions are no-ops (not crashes) ----
TEST_F(StateMachineTest, InvalidEventInIdleNoOp) {
  // on_replan_response in IDLE → no transition + ASDR warning
  auto evt = sm.on_replan_response(/*...*/);
  EXPECT_FALSE(evt.has_value());
  EXPECT_EQ(sm.current_state(), MissionState::kIdle);
}

}  // namespace
}  // namespace mass_l3::m3
```

---

## 8. 配置文件

### 8.1 `config/m3_params.yaml`

```yaml
# M3 Mission Manager Parameters
# Loaded by MissionManagerNode at startup; HAZID-tagged values are placeholders
# pending RUN-001 (2026-08-19) → v1.1.3 backfill.

m3_mission_manager:
  ros__parameters:
    # ---- VoyageTask validation thresholds (§5.3.1) ----
    voyage_task:
      departure_distance_max_km: 2.0     # [TBD-HAZID]
      eta_window_min_s: 600              # [TBD-HAZID] 10 min
      eta_window_max_s: 259200           # 72 hrs (operational ceiling)
      waypoint_max_distance_nm: 50.0     # ENC accuracy bound
      exclusion_zone_buffer_m: 500.0     # [TBD-HAZID]

    # ---- ETA projection (§5.3.2) ----
    eta:
      sampling_interval_s: 60                # [TBD-HAZID] integration step
      forecast_horizon_max_s: 3600           # [TBD-HAZID] 1 hr
      sea_current_uncertainty_kn: 0.3        # [TBD-HAZID]
      world_state_age_threshold_s: 0.5       # [TBD-HAZID]
      infeasible_margin_s: 600.0             # 10 min operator window

    # ---- Replan request triggers (§5.3.3) ----
    replan:
      deadline_mrc_required_s: 30.0          # [TBD-HAZID]
      deadline_odd_exit_critical_s: 60.0     # [TBD-HAZID]
      deadline_odd_exit_degraded_s: 120.0    # [TBD-HAZID]
      deadline_mission_infeasible_s: 120.0   # [TBD-HAZID]
      deadline_congestion_s: 300.0           # [TBD-HAZID]
      attempt_max_count: 3                   # [TBD-HAZID]

    # ---- ODD thresholds (architecture §3.6) ----
    odd:
      degraded_threshold: 0.7
      critical_threshold: 0.3
      degraded_buffer_s: 1.0                 # [TBD-HAZID] anti-flicker

    # ---- Publishing rates ----
    mission_goal:
      publish_rate_hz: 0.5
    asdr:
      heartbeat_rate_hz: 2.0

    # ---- Input freshness timeouts (§7.3) ----
    timeout:
      voyage_task_s: 600.0                   # [TBD-HAZID]
      planned_route_s: 3.0                   # [TBD-HAZID]
      speed_profile_s: 3.0                   # [TBD-HAZID]
      odd_state_s: 2.0                       # [TBD-HAZID]
      world_state_s: 0.5                     # [TBD-HAZID]

    # ---- Logging ----
    logging:
      level: info                            # trace | debug | info | warn | error | critical
      file_path: /var/log/mass_l3/m3.log
      asdr_record_enabled: true
```

### 8.2 `config/exclusion_zones_template.geojson`

VoyageTask 中携带的 `exclusion_zones` 与 RouteReplanRequest 中携带的 `exclusion_zones` 序列化模板（GeoJSON Polygon, RFC-006 已锁定）。M3 内部用 `nlohmann::json` 解析。

```json
{
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "properties": {
        "zone_id": "EXCL_001",
        "zone_name": "Marine Protected Area Alpha",
        "source": "ENC chart layer SUBPLA",
        "valid_from": "2026-05-06T00:00:00Z",
        "valid_until": "2026-12-31T23:59:59Z"
      },
      "geometry": {
        "type": "Polygon",
        "coordinates": [
          [
            [4.700, 58.300],
            [4.750, 58.300],
            [4.750, 58.350],
            [4.700, 58.350],
            [4.700, 58.300]
          ]
        ]
      }
    }
  ]
}
```

**字段约束**：
- 每个 Polygon 第一个和最后一个顶点必须重合（GeoJSON 规范）
- 顶点数 ≥ 4（首尾重复后实际 ≥ 3）
- 经度先于纬度（GeoJSON 规范：`[lon, lat]`）—— **注意与项目内部 `GeoPoint{lat, lon}` 的转换在 `nlohmann::json` 序列化层处理**

---

## 9. PATH-D 合规性 checklist

PR 合并到 `main` 前 reviewer 须确认（继承 `coding-standards.md` §11，附 M3 专属项）：

### 9.1 通用（继承 coding-standards §11）

- [ ] 命名遵循 `coding-standards.md` §4（`snake_case` / `PascalCase` / 等）
- [ ] 注释命中 `coding-standards.md` §5.1 五种合法场景
- [ ] 错误处理符合 `coding-standards.md` §8.1 决策树（M2–M6 允许异常，但 catch-all 仅在 `spin()` 边界）
- [ ] 头文件包含顺序符合 `coding-standards.md` §9.1
- [ ] 函数 ≤ 60 行；循环复杂度 ≤ 10
- [ ] 单元测试覆盖率 ≥ 90%（lcov 报告）
- [ ] CI 5 阶段全绿
- [ ] AddressSanitizer + ThreadSanitizer + UBSan 0 错误

### 9.2 M3 专属

- [ ] 所有 `[TBD-HAZID]` 参数通过 `m3_params.yaml` 注入，**不硬编码**（grep 检查 `*.cpp` / `*.hpp` 中无形如 `0.7` / `0.3` / `60.0` 等魔数）
- [ ] 4 reason 触发逻辑在 `ReplanRequestTrigger::evaluate()` 单点封装，不散落到 `MissionManagerNode`
- [ ] RFC-006 ReplanResponse 4 status 全部覆盖（`ReplanResponseHandler::handle()` switch 必须有 `default:` 升级 ToR_RequestMsg）
- [ ] 状态机所有转移 explicit 显式触发；invalid event 在 `kIdle` / `kInit` 等状态下返回 `nullopt`，不引发 crash
- [ ] `std::optional` 不滥用作 return-value-based error；用 `ValidationResult` / `EtaResult` POD 含错误码
- [ ] `GeoPoint{lat, lon}` 模型与 GeoJSON `[lon, lat]` 转换在 nlohmann::json 序列化层 explicit 处理
- [ ] 浮点比较（如 conformance_score 阈值）用 `std::abs(a - b) < eps` 或 `<` / `>` 单向比较，**不用 `==` / `!=`**
- [ ] 时间戳全部用 `int64_t` 纳秒戳（与 `builtin_interfaces/Time stamp` 转换在 helper 中央实现）
- [ ] ETA 投影中所有距离 / 速度 / 时间单位带 suffix（`_m` / `_kn` / `_s` / `_ns`）
- [ ] ASDR_RecordMsg.decision_json 字段格式符合详细设计 §8.3 + RFC-004 决议（用 `nlohmann::json` 构造）
- [ ] M3 仅订阅 / 发布 §3.2 / §3.3 表中列出的 topic；**不**直接调用其他模块的 service

### 9.3 跨团队接口

- [ ] `/l1/voyage_task` 字段对齐 v1.1.2 §15.1 VoyageTask
- [ ] `/l2/planned_route` + `/l2/speed_profile` + `/l2/replan_response` 字段对齐 §15.1
- [ ] `/l3/m3/mission_goal` + `/l3/m3/route_replan_request` 字段对齐 §15.1
- [ ] `/l3/asdr/record` 字段对齐 §15.1 + RFC-004
- [ ] QoS 设置匹配 `ros2-idl-implementation-guide.md` §4.1 时间尺度表

### 9.4 第三方库（继承 `third-party-library-policy.md` §3）

- [ ] 仅使用白名单库：rclcpp / l3_msgs / l3_external_msgs / Eigen / spdlog / nlohmann::json / GeographicLib / yaml-cpp / fmt / GTest
- [ ] **未引入** CasADi / IPOPT / Boost.Geometry / FastAPI / pydantic（不属 M3 路径）
- [ ] CMake `find_package(... EXACT REQUIRED)` 锁定版本（不依赖系统包）

---

## 10. 引用

### 10.1 项目内部

- **架构 v1.1.2 §7（M3 模块）+ §15.1（VoyageTask / RouteReplanRequest / ReplanResponseMsg / Mission_GoalMsg）+ §15.2（接口矩阵）** — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- **M3 详细设计** — `docs/Design/Detailed Design/M3-Mission-Manager/01-detailed-design.md`（SANGO-ADAS-L3-DD-M3-001）
- **RFC-006 ReplanResponseMsg 决议** — `docs/Design/Cross-Team Alignment/RFC-decisions.md`
- **实施层主计划** — `docs/Implementation/00-implementation-master-plan.md`
- **编码规范** — `docs/Implementation/coding-standards.md`
- **ROS2 IDL 实现指南** — `docs/Implementation/ros2-idl-implementation-guide.md`
- **第三方库政策** — `docs/Implementation/third-party-library-policy.md`
- **静态分析政策** — `docs/Implementation/static-analysis-policy.md`
- **CI/CD pipeline** — `docs/Implementation/ci-cd-pipeline.md`

### 10.2 外部规范（继承 v1.1.2 §16）

- **MISRA C++:2023 完整 179 规则** — Perforce 公告 [Rcpp-1] 🟢 A
- **CCS《智能船舶规范》（2024/2025）** — i-Ship (Nx, Ri/Ai) [R1] 🟢 A
- **IMO MSC 110/111 MASS Code（2025/2026）** [R2] 🟢 A
- **IEC 61508 SIL 2 + ISO 21448 SOTIF** [R5, R6] 🟢 A
- **ROS 2 Jazzy Jalisco** — [docs.ros.org/en/jazzy](https://docs.ros.org/en/jazzy/) 🟢 A
- **GeographicLib 2.7** — WGS84 大圆距离（haversine） 🟢 A
- **GeoJSON RFC 7946** — Polygon 序列化规范 🟢 A
- **nlohmann::json 3.12.0** — JSON 序列化库 🟢 A

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff，subagent for Team-M3） | 初稿创建：M3 PATH-D 代码骨架 — 5 核心类（VoyageTaskValidator / EtaProjector / ReplanRequestTrigger / ReplanResponseHandler / MissionStateMachine）+ ROS2 节点 + 7-state FSM（含 RFC-006 ReplanResponse 4 status 路径）+ CMakeLists / package.xml + 单元测试骨架 ≥ 90% 覆盖 + m3_params.yaml + GeoJSON exclusion zones 模板 |
