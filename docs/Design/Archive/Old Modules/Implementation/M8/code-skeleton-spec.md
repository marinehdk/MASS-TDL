# M8 HMI/Transparency Bridge 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M8-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M8 开发起点） |
| 团队 | Team-M8（HMI/Transparency Bridge） |
| 路径强度 | **PATH-H（HMI 路径，MISRA C++:2023 简化裁剪集 ~120 规则）** |
| 详细设计 | `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md`（SANGO-ADAS-L3-DD-M8-001） |
| 架构基线 | v1.1.2 §12（M8 HMI/Transparency）+ §12.2 自适应触发 + §12.4 ToR + §15.2 接口 + RFC-004（ASDR） |
| 关联文件 | `00-implementation-master-plan.md` / `coding-standards.md` §6 / `static-analysis-policy.md` §6 / `ros2-idl-implementation-guide.md` §3 + §4 / `third-party-library-policy.md` §2.3 |

> **注**：本 spec 是 Team-M8 编码起点。所有标 `[TBD-HAZID]` 的参数初始值取自详细设计 §5.3，HAZID RUN-001（2026-05-13 → 2026-08-19）完成后通过 YAML 热加载回填，**不重编译核心代码**。

---

## 1. 模块概述

### 1.1 职责（一句话）

M8 是 L3 战术层中**唯一对 ROC 操作员与船上船长说话的实体**：聚合 M1/M2/M4/M6/M7 的 SAT_DataMsg、按 v1.1.2 §12.2 自适应触发策略推送 SAT-2/SAT-3、实现 ToR（Transfer of Responsibility）协议（含"已知悉 SAT-1"C 交互验证），并通过 Web 后端 + WebSocket 把 UI_State 推到 ROC HMI 前端。

### 1.2 路径定位

| 维度 | 选择 | 理由 |
|---|---|---|
| 路径强度 | **PATH-H（HMI）** | M8 不参与决策核心；其 SAT 聚合 + Web 后端不属 IEC 61508 SIL 2 核心安全功能（属"决策可解释性"辅助层）|
| 编码规范 | MISRA C++:2023 **简化裁剪集**（`coding-standards.md` §6） | 允许动态分配（HTTP 请求天然动态）+ 允许异常（HTTP/WebSocket 框架普遍用异常）|
| 单元测试覆盖率 | **≥ 80%**（PATH-H 阈值）| 比 M1/M7 ≥ 95% 低，比 M2-M6 ≥ 90% 低 |
| 双语言 | C++17（ROS2 主节点）+ Python 3.10（FastAPI 后端） | C++ 处理 ROS2 实时聚合；Python 处理 Web/REST/WebSocket（生态成熟度优于 C++ Web 框架）|

### 1.3 时间尺度

| 任务 | 频率 | 实现 |
|---|---|---|
| SAT 聚合 + 自适应触发 | 10 Hz（与 SATData 输入同步）| C++ 节点 |
| UI_State 发布 | 50 Hz | C++ 节点 + Python WebSocket 推送 |
| ToR 协议 + 倒计时 | 2 Hz 内部检查 / 事件触发 | C++ 节点 |
| ASDR 决策日志 | 事件 + 2 Hz 周期 | C++ 节点 |
| Web HMI（前端推送） | 50 Hz WebSocket | Python FastAPI |

### 1.4 上下游

**订阅**（v1.1.2 §15.2 + IDL 指南 §3.3）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/m3/mission_goal` (l3_msgs/MissionGoal) — 0.5 Hz
- `/l3/m4/behavior_plan` (l3_msgs/BehaviorPlan) — 2 Hz
- `/l3/m5/avoidance_plan` (l3_msgs/AvoidancePlan) — 1-2 Hz
- `/l3/m6/colregs_constraint` (l3_msgs/COLREGsConstraint) — 2 Hz
- `/l3/m7/safety_alert` (l3_msgs/SafetyAlert) — 事件
- `/l3/sat/data` (l3_msgs/SATData) — 10 Hz from M1/M2/M4/M6/M7
- `/override/active_signal` (l3_external_msgs/OverrideActiveSignal) — 事件

**发布**：
- `/l3/m8/ui_state` (l3_msgs/UIState) — 50 Hz
- `/l3/m8/tor_request` (l3_msgs/ToRRequest) — 事件
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz [v1.1.2 RFC-004]

---

## 2. 项目目录结构

```
src/m8_hmi_transparency_bridge/                       # ROS2 colcon package
├── package.xml
├── CMakeLists.txt
├── README.md
├── include/m8_hmi_transparency_bridge/
│   ├── sat_aggregator.hpp                            # 多源 SAT_DataMsg 聚合
│   ├── adaptive_sat_trigger.hpp                      # v1.1.2 §12.2 自适应触发
│   ├── tor_protocol.hpp                              # ToR 状态机 + C 交互验证
│   ├── ui_state_builder.hpp                          # HMI 渲染数据构造
│   ├── tor_request_generator.hpp                     # ToR_Request 构造（60s deadline）
│   ├── asdr_logger.hpp                               # ASDR 事件记录（独立 spdlog instance）
│   ├── module_health_monitor.hpp                     # M1-M7 心跳监控
│   ├── hmi_transparency_bridge_node.hpp              # ROS2 主节点
│   └── types.hpp                                     # 内部类型 + 常量定义
├── src/
│   ├── sat_aggregator.cpp
│   ├── adaptive_sat_trigger.cpp
│   ├── tor_protocol.cpp
│   ├── ui_state_builder.cpp
│   ├── tor_request_generator.cpp
│   ├── asdr_logger.cpp
│   ├── module_health_monitor.cpp
│   ├── hmi_transparency_bridge_node.cpp
│   └── main.cpp                                      # rclcpp::spin
├── launch/
│   ├── m8_hmi_bridge.launch.py                       # 主 launch（C++ 节点 + Python 后端）
│   └── m8_dev_with_mock.launch.py                    # 含 mock SATData 发布器（开发期）
├── config/
│   ├── m8_params.yaml                                # 自适应触发阈值 + ToR deadline（HAZID 注入点）
│   ├── m8_web_server.yaml                            # FastAPI 端口 / CORS / WebSocket
│   └── m8_logger.yaml                                # spdlog sinks
├── test/
│   ├── test_sat_aggregator.cpp                       # GTest 单元
│   ├── test_adaptive_sat_trigger.cpp
│   ├── test_tor_protocol.cpp                         # ToR 状态机覆盖
│   ├── test_ui_state_builder.cpp
│   ├── test_tor_request_generator.cpp
│   ├── test_asdr_logger.cpp
│   └── test_module_health_monitor.cpp
└── python/
    ├── web_server/
    │   ├── __init__.py
    │   ├── app.py                                    # FastAPI 主应用
    │   ├── websocket.py                              # WebSocket UI_State 实时推送
    │   ├── tor_endpoint.py                           # ToR REST 端点（"已知悉"按钮）
    │   ├── ros_bridge.py                             # rclpy 桥接（订阅 UI_State、发布按钮事件）
    │   ├── schemas.py                                # pydantic 模型
    │   └── main.py                                   # uvicorn 入口
    ├── tests/
    │   ├── test_app.py                               # pytest FastAPI 端点测试
    │   ├── test_websocket.py                         # WebSocket 推送回归
    │   ├── test_tor_endpoint.py                      # 按钮点击 → ASDR
    │   ├── test_schemas.py                           # pydantic 校验
    │   └── conftest.py                               # 共享 fixture
    ├── requirements.txt                              # Python 依赖锁定
    └── pyproject.toml                                # Ruff + mypy + pytest 配置
```

**关键设计点**：
- **C++ ROS2 节点**：处理订阅 / 聚合 / 触发判定 / ASDR 日志，发布 `/l3/m8/ui_state` 与 `/l3/m8/tor_request`
- **Python FastAPI 后端**：通过 `rclpy` 订阅 `/l3/m8/ui_state`（C++ 发布），WebSocket 推送给浏览器；REST 端点接收"已知悉 SAT-1"点击 → 通过 rclpy 发布到 `/l3/m8/operator_action`（M8 私有 topic）回到 C++ 节点
- **前端**：不在本仓库；ROC HMI 前端团队基于 `python/web_server/schemas.py` 构造 React/Vue 客户端
- **Doer-Checker 边界**：M8 是 Doer 之一（仅可视化，不参与决策），M7 监督；M8 内部不持有任何 M1-M7 共享 header

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

```
┌────────────────────────────────────────────────────────────┐
│                                                            │
│   m8_hmi_transparency_bridge   (C++ ROS2 节点)              │
│   ─────────────────────────                                │
│   订阅: SATData / ODDState / WorldState / SafetyAlert / .. │
│   发布: UIState (50 Hz) / ToRRequest (事件) / ASDR        │
│   私有 topic（与 Python 后端通信）：                         │
│     - /l3/m8/_internal/operator_action (按钮点击)          │
│                                                            │
└──────┬─────────────────────────────────────────────────────┘
       │ rclpy DDS（同 host，IPC）
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│   m8_web_backend  (Python FastAPI + rclpy 节点)              │
│   ──────────────                                            │
│   订阅 /l3/m8/ui_state → WebSocket 广播给浏览器              │
│   REST: POST /api/tor/acknowledge → 发 operator_action       │
│   uvicorn 端口: 8080（config/m8_web_server.yaml）            │
└─────────────────────────────────────────────────────────────┘
```

**为什么 C++ 与 Python 双进程**：
- C++ 节点保留高频 ROS2 实时性（50 Hz UIState 发布、< 100 ms M7 降级告警显示）
- Python 处理 HTTP/WebSocket（FastAPI/uvicorn 生态成熟）；rclpy 仅作 DDS 桥接
- 解耦：Python 后端崩溃不影响 C++ 节点的 ASDR 日志写入

### 3.2 订阅者表格

| Topic | 类型 | QoS Profile | 回调线程 | 说明 |
|---|---|---|---|---|
| `/l3/sat/data` | `l3_msgs/SATData` | `SensorDataQoS().keep_last(2)` | sat_callback | 多源订阅（M1/M2/M4/M6/M7 都发到同一 topic，按 source_module 字段区分） |
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | `QoS(10).reliable().transient_local()` | odd_callback | 长时（1 Hz + 事件补发）|
| `/l3/m2/world_state` | `l3_msgs/WorldState` | `SensorDataQoS().keep_last(2)` | world_callback | 短时（4 Hz）|
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | `QoS(5).reliable()` | mission_callback | 中时（0.5 Hz）|
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | `QoS(5).reliable()` | behavior_callback | 中时（2 Hz）|
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | `QoS(5).reliable()` | avoid_callback | 中时（1-2 Hz）|
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | `QoS(5).reliable()` | colreg_callback | 中时（2 Hz）|
| `/l3/m7/safety_alert` | `l3_msgs/SafetyAlert` | `QoS(50).reliable().transient_local()` | safety_callback | **CRITICAL**：< 100 ms 显示时延（接管期间）|
| `/override/active_signal` | `l3_external_msgs/OverrideActiveSignal` | `QoS(50).reliable().transient_local()` | override_callback | 事件 |
| `/l3/m8/_internal/operator_action` | `l3_msgs/OperatorAction`（M8 私有 .msg）| `QoS(10).reliable()` | operator_action_callback | 来自 Python 后端的"已知悉"按钮点击 |

> **多线程策略**：M7 SafetyAlert 与 M8 内部聚合用独立 callback group（`MutuallyExclusive` for state，但 M7 用 `Reentrant` 以便 < 100 ms 响应）。详见 §6 main.cpp。

### 3.3 发布者表格

| Topic | 类型 | QoS Profile | 频率 | 触发 |
|---|---|---|---|---|
| `/l3/m8/ui_state` | `l3_msgs/UIState` | `SensorDataQoS().keep_last(1)` | 50 Hz | 20 ms 定时器 |
| `/l3/m8/tor_request` | `l3_msgs/ToRRequest` | `QoS(50).reliable().transient_local()` | 事件 | TDL 触发 / 操作员请求 |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | `QoS(50).reliable().transient_local()` | 事件 + 2 Hz | RFC-004 决议；签名 SHA-256 |

### 3.4 参数（YAML — `config/m8_params.yaml`）

```yaml
m8_hmi_transparency_bridge:
  ros__parameters:
    # ============= 自适应 SAT 触发（v1.1.2 §12.2） =============
    sat_threat_confidence_threshold: 0.7        # SAT-1 列表过滤  [TBD-HAZID]
    sat2_rule_confidence_threshold: 0.8         # M6 confidence < 此值则 SAT-2 显示"推理置信度不足"  [TBD-HAZID]
    sat3_priority_high_tdl_s: 30.0              # TDL < 此值时 SAT-3 自动加粗  [TBD-HAZID]
    sat2_system_confidence_drop_threshold: 0.6  # 系统 confidence 跌破此值触发 SAT-2  [TBD-HAZID]

    # ============= ToR 协议（v1.1.2 §12.4 + Veitch 2024 [R4]）=============
    tor_deadline_s: 60.0                        # 接管时窗  [TBD-HAZID]
    tor_sat1_min_display_s: 5.0                 # "已知悉"按钮启用前的强制等待
    tor_retry_interval_s: 30.0                  # 30s 未应答则重推一次
    tor_max_retries: 1                          # 最多重推 1 次（之后 60s 超时触发 MRC）

    # ============= 通信延迟告警阈值 =============
    comm_latency_warn_m1_ms: 500                # M1 → M8 延迟 > 此值橙色告警  [TBD-HAZID]
    comm_latency_warn_m2_ms: 1000               # M2 → M8 延迟 > 此值橙色告警  [TBD-HAZID]
    comm_latency_critical_m7_ms: 200            # M7 → M8 延迟 > 此值红色告警  [TBD-HAZID]

    # ============= 心跳超时 =============
    heartbeat_timeout_m1_s: 2.0
    heartbeat_timeout_m2_s: 1.0
    heartbeat_timeout_m4_s: 1.0
    heartbeat_timeout_m6_s: 1.0
    heartbeat_timeout_m7_s: 2.0                 # ≥ 此值假设 M7 失效 → 强制 D2

    # ============= 消息丢失（连续帧）=============
    msg_loss_critical_m7_frames: 2              # M7 连续 2 帧丢失 → CRITICAL  [TBD-HAZID]
    msg_loss_critical_m2_frames: 5              # M2 连续 5 帧丢失 → 禁止决策  [TBD-HAZID]
    msg_loss_degraded_general_frames: 3         # 通用 ≥ 3 帧丢失 → DEGRADED

    # ============= ASDR =============
    asdr_periodic_snapshot_hz: 2.0              # UI 状态周期快照（事后回放）
    asdr_signature_alg: "SHA256"                # SHA-256 签名

    # ============= UI 发布频率 =============
    ui_state_pub_hz: 50.0
    sat_aggregation_hz: 10.0
```

```yaml
# config/m8_web_server.yaml
m8_web_backend:
  ros__parameters:
    fastapi_host: "0.0.0.0"
    fastapi_port: 8080
    cors_allowed_origins:
      - "http://localhost:3000"                # ROC HMI 开发前端
      - "http://hmi.roc.local"                 # ROC HMI 生产前端
    websocket_max_clients: 8                   # 同时 ≥ 1 ROC + 备用
    websocket_ping_interval_s: 10.0
    websocket_send_timeout_s: 1.0
    rclpy_spin_threads: 2
```

```yaml
# config/m8_logger.yaml — spdlog sinks（独立于 M1-M7 logger）
m8_logger:
  level: "info"                                  # debug | info | warn | error
  sinks:
    - type: "stdout_color_mt"
    - type: "rotating_file_mt"
      file_path: "/var/log/mass_l3/m8_hmi_bridge.log"
      max_size_mb: 50
      max_files: 10
  pattern: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [thread %t] %v"
```

---

## 4. 核心类定义（C++ Header 骨架）

### 4.1 SatAggregator

```cpp
// include/m8_hmi_transparency_bridge/sat_aggregator.hpp
#ifndef MASS_L3_M8_SAT_AGGREGATOR_HPP_
#define MASS_L3_M8_SAT_AGGREGATOR_HPP_

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/sat1_data.hpp"
#include "l3_msgs/msg/sat2_data.hpp"
#include "l3_msgs/msg/sat3_data.hpp"

namespace mass_l3::m8 {

/// 多源 SAT_DataMsg 聚合器
///
/// 职责：缓冲 M1/M2/M4/M6/M7 各自发布的 SAT_DataMsg，按 source_module 区分；
/// 给出"最新 SAT-1/2/3 视图 + 各模块新鲜度"的统一查询接口。
class SatAggregator final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  /// 5 个固定上游模块（v1.1.2 §15.2）
  enum class SourceModule : uint8_t {
    kM1 = 0,
    kM2 = 1,
    kM4 = 2,
    kM6 = 3,
    kM7 = 4,
    kCount = 5
  };

  SatAggregator() = default;
  ~SatAggregator() = default;
  SatAggregator(const SatAggregator&) = delete;
  SatAggregator& operator=(const SatAggregator&) = delete;

  /// 摄入一帧 SAT_DataMsg（线程安全）
  void ingest(const l3_msgs::msg::SATData& msg, TimePoint receive_time);

  /// 查询某模块最新 SAT-1（若无则 nullopt）
  std::optional<l3_msgs::msg::SAT1Data> latest_sat1(SourceModule src) const;
  std::optional<l3_msgs::msg::SAT2Data> latest_sat2(SourceModule src) const;
  std::optional<l3_msgs::msg::SAT3Data> latest_sat3(SourceModule src) const;

  /// 某模块消息的接收年龄（秒）；返回正无穷表示从未收到
  double age_seconds(SourceModule src, TimePoint now) const;

  /// 该模块是否陈旧（age > stale_threshold_s）
  bool is_stale(SourceModule src, TimePoint now, double stale_threshold_s) const;

  /// SourceModule 名 ⇄ string
  static std::string to_string(SourceModule src);
  static std::optional<SourceModule> from_string(const std::string& name);

 private:
  struct PerSourceCache {
    l3_msgs::msg::SAT1Data sat1{};
    l3_msgs::msg::SAT2Data sat2{};
    l3_msgs::msg::SAT3Data sat3{};
    TimePoint last_receive_time{};
    bool has_data{false};
  };

  mutable std::mutex mutex_;
  std::map<SourceModule, PerSourceCache> cache_{};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_SAT_AGGREGATOR_HPP_
```

### 4.2 AdaptiveSatTrigger

```cpp
// include/m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp
#ifndef MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
#define MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_

#include <string>
#include <vector>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/colreg_s_constraint.hpp"
#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

/// v1.1.2 §12.2 自适应 SAT 触发策略
///
/// SAT-1 全时展示；SAT-2 按需触发（4 条件）；SAT-3 基线展示，TDL < 30s 时优先级提升。
struct SatTriggerDecision {
  bool sat1_visible{true};                    // 始终 true
  bool sat2_visible{false};
  bool sat3_visible{true};                    // 始终基线展示
  bool sat3_priority_high{false};             // TDL < 30s 时 true（加粗）
  std::vector<std::string> sat2_trigger_reasons{};  // ["colreg_conflict", "m7_sotif", ...]
  std::string sat3_alert_color{"normal"};     // "normal" | "bold_red"
};

class AdaptiveSatTrigger final {
 public:
  struct Thresholds {
    double sat3_priority_high_tdl_s{30.0};        // [TBD-HAZID]
    double sat2_system_confidence_threshold{0.6};  // [TBD-HAZID]
    float threat_confidence_threshold{0.7F};       // [TBD-HAZID]
    float rule_confidence_threshold{0.8F};         // [TBD-HAZID]
  };

  explicit AdaptiveSatTrigger(Thresholds t) noexcept : thresholds_(t) {}

  /// 主入口：综合 ODD / SAT 缓冲 / M7 告警 / 操作员标志，决定显示策略
  SatTriggerDecision decide(
      const l3_msgs::msg::ODDState& odd,
      const SatAggregator& sat_cache,
      const std::optional<l3_msgs::msg::SafetyAlert>& latest_m7_alert,
      const std::optional<l3_msgs::msg::COLREGsConstraint>& latest_m6_constraint,
      bool operator_requested_sat2,
      SatAggregator::TimePoint now) const;

  /// 单元测试访问点
  bool has_colreg_conflict_for_test(
      const std::optional<l3_msgs::msg::COLREGsConstraint>& m6) const;
  bool m7_alert_triggers_sat2_for_test(
      const std::optional<l3_msgs::msg::SafetyAlert>& alert) const;

 private:
  Thresholds thresholds_;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
```

### 4.3 TorProtocol

```cpp
// include/m8_hmi_transparency_bridge/tor_protocol.hpp
#ifndef MASS_L3_M8_TOR_PROTOCOL_HPP_
#define MASS_L3_M8_TOR_PROTOCOL_HPP_

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace mass_l3::m8 {

/// ToR 状态机（v1.1.2 §12.4 + §12.4.1 "已知悉 SAT-1" C 交互验证）
///
/// 状态：IDLE → REQUESTED → ACKNOWLEDGED（或 TIMEOUT → MRC）
class TorProtocol final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  enum class State : uint8_t {
    kIdle = 0,
    kRequested = 1,        // ToR_Request 已推送，等待操作员
    kAcknowledged = 2,     // 操作员点击"已知悉" → 接管授权
    kTimeoutMrc = 3,       // 60s 超时 → 触发 MRC 准备
  };

  enum class Reason : uint8_t {
    kOddExit = 0,
    kManualRequest = 1,
    kSafetyAlert = 2,
  };

  struct Config {
    double deadline_s{60.0};            // [R4] Veitch 2024  [TBD-HAZID]
    double sat1_min_display_s{5.0};     // 按钮启用前的强制等待
    double retry_interval_s{30.0};
    int max_retries{1};
  };

  /// 操作员点击事件的快照（写入 ASDR）
  struct AcknowledgmentSnapshot {
    TimePoint click_time{};
    double sat1_display_duration_s{0.0};
    std::vector<std::string> sat1_threats_visible{};
    std::string odd_zone_at_click{};
    float conformance_score_at_click{0.0F};
    std::string operator_id{};
  };

  explicit TorProtocol(Config cfg) noexcept : config_(cfg) {}

  /// 触发新一轮 ToR
  /// 返回 true 表示已进入 REQUESTED；false 表示已在进行中（拒绝重入）
  bool trigger(Reason reason, TimePoint now);

  /// SAT-1 显示是否已 ≥ min_display_s（决定按钮是否启用）
  bool is_acknowledgment_button_enabled(TimePoint now) const;

  /// 操作员点击 → 推进到 ACKNOWLEDGED
  /// 返回 nullopt 表示按钮未启用或状态非 REQUESTED（拒绝点击）
  std::optional<AcknowledgmentSnapshot> on_acknowledgment_clicked(
      TimePoint now,
      const std::vector<std::string>& current_sat1_threats,
      const std::string& current_odd_zone,
      float current_conformance_score,
      const std::string& operator_id);

  /// 周期性 tick（500 ms）：检查超时；返回 true 表示刚进入 TIMEOUT_MRC
  bool tick(TimePoint now);

  /// 显式重置（接管完成后回到 IDLE）
  void reset();

  // accessors
  State state() const { return state_; }
  Reason last_reason() const { return last_reason_; }
  TimePoint requested_time() const { return requested_time_; }
  double remaining_deadline_s(TimePoint now) const;

 private:
  Config config_;
  State state_{State::kIdle};
  Reason last_reason_{Reason::kOddExit};
  TimePoint requested_time_{};
  int retries_{0};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_PROTOCOL_HPP_
```

### 4.4 UiStateBuilder

```cpp
// include/m8_hmi_transparency_bridge/ui_state_builder.hpp
#ifndef MASS_L3_M8_UI_STATE_BUILDER_HPP_
#define MASS_L3_M8_UI_STATE_BUILDER_HPP_

#include "l3_msgs/msg/ui_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colreg_s_constraint.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"
#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

/// 把所有内部状态构造成可发布的 UIState 消息。
class UiStateBuilder final {
 public:
  enum class Role : uint8_t { kRocOperator = 0, kShipCaptain = 1 };
  enum class Scenario : uint8_t {
    kTransit = 0,
    kColregAvoidance = 1,
    kMrcPreparation = 2,
    kMrcActive = 3,
    kOverrideActive = 4,
    kHandbackPreparation = 5
  };

  struct BuildContext {
    SatAggregator::TimePoint now;
    Role role;
    Scenario scenario;
    SatTriggerDecision sat_decision;
    std::optional<l3_msgs::msg::ODDState> odd;
    std::optional<l3_msgs::msg::WorldState> world;
    std::optional<l3_msgs::msg::BehaviorPlan> behavior;
    std::optional<l3_msgs::msg::COLREGsConstraint> colreg;
    std::optional<l3_msgs::msg::SafetyAlert> latest_alert;
    TorProtocol::State tor_state;
    double tor_remaining_s{0.0};
    bool override_active{false};
    bool m7_degradation_alert_active{false};
    std::string m7_degradation_alert_text{};
  };

  l3_msgs::msg::UIState build(const BuildContext& ctx,
                              const SatAggregator& sat_cache) const;

 private:
  /// 角色 × 场景双轴差异化（v1.1.2 §12.3 + 详细设计 附录 A）
  void apply_role_scenario_filter(
      Role role, Scenario scenario, l3_msgs::msg::UIState& msg) const;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_UI_STATE_BUILDER_HPP_
```

### 4.5 ToRRequestGenerator

```cpp
// include/m8_hmi_transparency_bridge/tor_request_generator.hpp
#ifndef MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
#define MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_

#include <string>

#include "l3_msgs/msg/tor_request.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

/// 构造 ToR_RequestMsg —— 60s deadline、SAT-1 上下文摘要、推荐动作
class ToRRequestGenerator final {
 public:
  l3_msgs::msg::ToRRequest generate(
      TorProtocol::Reason reason,
      const l3_msgs::msg::ODDState& odd,
      const std::string& sat1_context_summary,
      double deadline_s) const;

 private:
  static uint8_t reason_to_msg_enum(TorProtocol::Reason r);
  static std::string recommended_action_for(TorProtocol::Reason r,
                                            const l3_msgs::msg::ODDState& odd);
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
```

### 4.6 HmiTransparencyBridgeNode

```cpp
// include/m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp
#ifndef MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
#define MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_

#include <memory>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/colreg_s_constraint.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/ui_state.hpp"
#include "l3_msgs/msg/tor_request.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"
#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"
#include "m8_hmi_transparency_bridge/tor_request_generator.hpp"
#include "m8_hmi_transparency_bridge/asdr_logger.hpp"
#include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

namespace mass_l3::m8 {

class HmiTransparencyBridgeNode : public rclcpp::Node {
 public:
  explicit HmiTransparencyBridgeNode(const rclcpp::NodeOptions& options);
  ~HmiTransparencyBridgeNode() override = default;

 private:
  // ============= 订阅回调 =============
  void on_sat_data(const l3_msgs::msg::SATData::SharedPtr msg);
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg);
  void on_behavior_plan(const l3_msgs::msg::BehaviorPlan::SharedPtr msg);
  void on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_colreg_constraint(
      const l3_msgs::msg::COLREGsConstraint::SharedPtr msg);
  void on_safety_alert(const l3_msgs::msg::SafetyAlert::SharedPtr msg);
  void on_override_signal(
      const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg);

  // ============= 定时器 =============
  void on_ui_publish_tick();           // 50 Hz
  void on_tor_tick();                  // 2 Hz（超时检查）
  void on_health_check_tick();         // 1 Hz（心跳监控）
  void on_asdr_snapshot_tick();        // 2 Hz（UI 快照）

  // ============= 内部辅助 =============
  void load_parameters();
  UiStateBuilder::Scenario infer_scenario() const;
  void publish_tor_request(TorProtocol::Reason reason);
  void emit_asdr_event(const std::string& event_type,
                       const std::string& decision_json);

  // ============= 成员 =============
  std::unique_ptr<SatAggregator> sat_aggregator_;
  std::unique_ptr<AdaptiveSatTrigger> adaptive_trigger_;
  std::unique_ptr<TorProtocol> tor_protocol_;
  std::unique_ptr<UiStateBuilder> ui_builder_;
  std::unique_ptr<ToRRequestGenerator> tor_generator_;
  std::unique_ptr<AsdrLogger> asdr_logger_;
  std::unique_ptr<ModuleHealthMonitor> health_monitor_;

  // 最新输入快照（mutex 保护）
  mutable std::mutex state_mutex_;
  std::optional<l3_msgs::msg::ODDState> latest_odd_;
  std::optional<l3_msgs::msg::WorldState> latest_world_;
  std::optional<l3_msgs::msg::BehaviorPlan> latest_behavior_;
  std::optional<l3_msgs::msg::AvoidancePlan> latest_avoidance_;
  std::optional<l3_msgs::msg::COLREGsConstraint> latest_colreg_;
  std::optional<l3_msgs::msg::SafetyAlert> latest_alert_;
  bool override_active_{false};

  // 订阅 / 发布 / 定时器
  rclcpp::Subscription<l3_msgs::msg::SATData>::SharedPtr sub_sat_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr sub_world_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoid_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr sub_colreg_;
  rclcpp::Subscription<l3_msgs::msg::SafetyAlert>::SharedPtr sub_alert_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr
      sub_override_;
  rclcpp::Publisher<l3_msgs::msg::UIState>::SharedPtr pub_ui_state_;
  rclcpp::Publisher<l3_msgs::msg::ToRRequest>::SharedPtr pub_tor_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr pub_asdr_;
  rclcpp::TimerBase::SharedPtr timer_ui_;
  rclcpp::TimerBase::SharedPtr timer_tor_;
  rclcpp::TimerBase::SharedPtr timer_health_;
  rclcpp::TimerBase::SharedPtr timer_asdr_snapshot_;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
```

---

## 5. Python Web 后端

### 5.1 `python/web_server/app.py`（FastAPI 主应用）

```python
"""FastAPI 主应用 — M8 Web 后端。

职责：
  1. 通过 rclpy 订阅 /l3/m8/ui_state（C++ 节点发布）
  2. 通过 WebSocket 把 UIState 实时广播给浏览器（50 Hz）
  3. 接收 REST 请求 POST /api/tor/acknowledge → 通过 rclpy 发布
     /l3/m8/_internal/operator_action 让 C++ 节点感知"已知悉"按钮点击

不参与决策；不处理 SAT 聚合；仅做 ROS2 ↔ HTTP 桥接。
"""
from __future__ import annotations

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from web_server.ros_bridge import RosBridge
from web_server.tor_endpoint import router as tor_router
from web_server.websocket import router as ws_router

logger = logging.getLogger("m8_web_backend")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """启动期初始化 RosBridge；关闭期清理 rclpy 资源。"""
    bridge = RosBridge()
    await bridge.start()
    app.state.ros_bridge = bridge
    logger.info("M8 web backend started; rclpy bridge online")
    try:
        yield
    finally:
        await bridge.stop()
        logger.info("M8 web backend shut down")


def create_app(cors_origins: list[str]) -> FastAPI:
    app = FastAPI(
        title="MASS L3 M8 HMI Backend",
        version="1.0.0",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=cors_origins,
        allow_credentials=True,
        allow_methods=["GET", "POST"],
        allow_headers=["*"],
    )
    app.include_router(tor_router, prefix="/api")
    app.include_router(ws_router)
    return app
```

### 5.2 `python/web_server/websocket.py`

```python
"""WebSocket 端点：把 UIState 50 Hz 推送给前端。"""
from __future__ import annotations

import asyncio
import logging
from typing import Set

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from web_server.schemas import UiStateSchema

router = APIRouter()
logger = logging.getLogger("m8_web_backend.websocket")

_active_clients: Set[WebSocket] = set()
_clients_lock = asyncio.Lock()


@router.websocket("/ws/ui_state")
async def ui_state_stream(ws: WebSocket) -> None:
    await ws.accept()
    async with _clients_lock:
        _active_clients.add(ws)
    logger.info("WS client connected; total=%d", len(_active_clients))
    try:
        while True:
            # 心跳（客户端可发任意帧；保持长连接）
            _ = await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        async with _clients_lock:
            _active_clients.discard(ws)
        logger.info("WS client disconnected; total=%d", len(_active_clients))


async def broadcast_ui_state(payload: UiStateSchema) -> None:
    """C++ → ros_bridge 接收 UIState 后调用：广播给所有 WS 客户端。"""
    text = payload.model_dump_json()
    async with _clients_lock:
        clients = list(_active_clients)
    dead: list[WebSocket] = []
    for ws in clients:
        try:
            await asyncio.wait_for(ws.send_text(text), timeout=1.0)
        except (asyncio.TimeoutError, RuntimeError):
            dead.append(ws)
    if dead:
        async with _clients_lock:
            for ws in dead:
                _active_clients.discard(ws)
```

### 5.3 `python/web_server/tor_endpoint.py`

```python
"""POST /api/tor/acknowledge — "已知悉 SAT-1" 按钮点击端点。

设计点（v1.1.2 §12.4.1 C 交互验证）：
  - C++ TorProtocol 已校验"按钮启用"（SAT-1 显示 ≥ 5s）；本端点仅转发点击事件
  - 把点击 timestamp + operator_id 通过 rclpy 发布到 /l3/m8/_internal/operator_action
  - C++ 节点接收后调用 TorProtocol::on_acknowledgment_clicked() → 写 ASDR
"""
from __future__ import annotations

import logging
from datetime import datetime, timezone

from fastapi import APIRouter, HTTPException, Request

from web_server.schemas import TorAcknowledgeRequest, TorAcknowledgeResponse

router = APIRouter()
logger = logging.getLogger("m8_web_backend.tor")


@router.post("/tor/acknowledge", response_model=TorAcknowledgeResponse)
async def acknowledge_tor(
    payload: TorAcknowledgeRequest, request: Request
) -> TorAcknowledgeResponse:
    bridge = request.app.state.ros_bridge
    if bridge is None or not bridge.is_ready():
        raise HTTPException(status_code=503, detail="ROS bridge not ready")
    click_time = datetime.now(tz=timezone.utc)
    accepted = bridge.send_operator_action(
        action_type="tor_acknowledged",
        operator_id=payload.operator_id,
        click_time=click_time,
    )
    if not accepted:
        raise HTTPException(
            status_code=409,
            detail="ToR not in REQUESTED state or button not yet enabled",
        )
    logger.info(
        "ToR acknowledged by %s @ %s", payload.operator_id, click_time.isoformat()
    )
    return TorAcknowledgeResponse(
        accepted=True,
        click_time=click_time,
        operator_id=payload.operator_id,
    )
```

### 5.4 `python/web_server/schemas.py`（pydantic 数据模型）

```python
"""pydantic 模型 — REST/WebSocket 边界 schema。

与 ROS2 .msg 字段一一对应；MyPy strict 通过。
"""
from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class TorAcknowledgeRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    operator_id: str = Field(..., min_length=1, max_length=64)


class TorAcknowledgeResponse(BaseModel):
    accepted: bool
    click_time: datetime
    operator_id: str


class Sat1ThreatSchema(BaseModel):
    target_id: int
    cpa_m: float
    tcpa_s: float
    aspect: Literal["head_on", "crossing", "overtaking", "overtaken", "ambiguous"]
    confidence: float = Field(..., ge=0.0, le=1.0)


class UiStateSchema(BaseModel):
    """对应 l3_msgs/UIState 的 JSON 投影（FastAPI ↔ 浏览器边界）。"""

    model_config = ConfigDict(extra="forbid")

    timestamp: datetime
    role: Literal["roc_operator", "ship_captain"]
    scenario: Literal[
        "transit",
        "colreg_avoidance",
        "mrc_preparation",
        "mrc_active",
        "override_active",
        "handback_preparation",
    ]
    sat1_visible: bool
    sat2_visible: bool
    sat3_visible: bool
    sat3_priority_high: bool
    sat3_alert_color: Literal["normal", "bold_red"]
    sat1_state_summary: str
    sat1_threats: list[Sat1ThreatSchema]
    sat2_reasoning_chain: str | None = None
    sat2_trigger_reasons: list[str] = []
    sat3_predicted_state: str | None = None
    sat3_tdl_s: float
    sat3_tmr_s: float
    tor_active: bool
    tor_remaining_s: float
    tor_button_enabled: bool
    override_active: bool
    m7_degradation_alert: str | None = None
```

### 5.5 `python/tests/` pytest 骨架

```python
# python/tests/test_tor_endpoint.py
"""Pytest 覆盖：POST /api/tor/acknowledge 流程。"""
from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from web_server.app import create_app


@pytest.fixture()
def app(monkeypatch: pytest.MonkeyPatch):
    """构造无 rclpy 的测试 app；用 fake bridge 替换。"""
    import web_server.app as app_mod

    class FakeBridge:
        def __init__(self) -> None:
            self.ready = True
            self.last_action: dict | None = None

        async def start(self) -> None:
            pass

        async def stop(self) -> None:
            pass

        def is_ready(self) -> bool:
            return self.ready

        def send_operator_action(
            self, action_type: str, operator_id: str, click_time
        ) -> bool:
            self.last_action = {
                "action_type": action_type,
                "operator_id": operator_id,
                "click_time": click_time,
            }
            return True

    monkeypatch.setattr(app_mod, "RosBridge", FakeBridge)
    return create_app(cors_origins=["*"])


def test_acknowledge_returns_accepted(app):
    client = TestClient(app)
    resp = client.post(
        "/api/tor/acknowledge", json={"operator_id": "ROC-OP-001"}
    )
    assert resp.status_code == 200
    body = resp.json()
    assert body["accepted"] is True
    assert body["operator_id"] == "ROC-OP-001"


def test_acknowledge_rejects_when_bridge_not_ready(app):
    app.state.ros_bridge.ready = False  # type: ignore[attr-defined]
    client = TestClient(app)
    resp = client.post(
        "/api/tor/acknowledge", json={"operator_id": "ROC-OP-001"}
    )
    assert resp.status_code == 503


def test_acknowledge_rejects_empty_operator_id(app):
    client = TestClient(app)
    resp = client.post("/api/tor/acknowledge", json={"operator_id": ""})
    assert resp.status_code == 422  # pydantic 校验失败
```

---

## 6. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(m8_hmi_transparency_bridge LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# 编译选项（coding-standards.md §2.2）
add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual
  -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wfloat-equal
  -Wformat=2 -Wformat-security
  -Wmissing-declarations -Wundef -Wunused
  -Wuseless-cast -Wlogical-op
  -Wduplicated-cond -Wduplicated-branches
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
)
add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>")
add_link_options("$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>")

# ============== 依赖 ==============
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(nlohmann_json 3.12.0 REQUIRED)
find_package(spdlog 1.17.0 REQUIRED)
find_package(yaml-cpp REQUIRED)

# ============== 库 ==============
add_library(m8_core
  src/sat_aggregator.cpp
  src/adaptive_sat_trigger.cpp
  src/tor_protocol.cpp
  src/ui_state_builder.cpp
  src/tor_request_generator.cpp
  src/asdr_logger.cpp
  src/module_health_monitor.cpp
)
target_include_directories(m8_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(m8_core
  rclcpp l3_msgs l3_external_msgs
)
target_link_libraries(m8_core
  Eigen3::Eigen
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  yaml-cpp
)

# ============== 主节点可执行 ==============
add_executable(m8_hmi_transparency_bridge_node
  src/hmi_transparency_bridge_node.cpp
  src/main.cpp
)
target_link_libraries(m8_hmi_transparency_bridge_node m8_core)
ament_target_dependencies(m8_hmi_transparency_bridge_node
  rclcpp l3_msgs l3_external_msgs
)

# ============== 安装 ==============
install(TARGETS m8_core m8_hmi_transparency_bridge_node
  EXPORT export_m8_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY python/ DESTINATION lib/${PROJECT_NAME}/python
        FILES_MATCHING PATTERN "*.py")

# ============== 测试 ==============
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_sat_aggregator test/test_sat_aggregator.cpp)
  target_link_libraries(test_sat_aggregator m8_core)

  ament_add_gtest(test_adaptive_sat_trigger test/test_adaptive_sat_trigger.cpp)
  target_link_libraries(test_adaptive_sat_trigger m8_core)

  ament_add_gtest(test_tor_protocol test/test_tor_protocol.cpp)
  target_link_libraries(test_tor_protocol m8_core)

  ament_add_gtest(test_ui_state_builder test/test_ui_state_builder.cpp)
  target_link_libraries(test_ui_state_builder m8_core)

  ament_add_gtest(test_tor_request_generator test/test_tor_request_generator.cpp)
  target_link_libraries(test_tor_request_generator m8_core)

  ament_add_gtest(test_asdr_logger test/test_asdr_logger.cpp)
  target_link_libraries(test_asdr_logger m8_core)

  ament_add_gtest(test_module_health_monitor
                  test/test_module_health_monitor.cpp)
  target_link_libraries(test_module_health_monitor m8_core)

  # Python 测试入口（pytest 由 CI 单独跑；本处仅做 ament 集成）
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(m8_python_tests python/tests/
    PYTHON_EXECUTABLE python3
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python
  )
endif()

ament_package()
```

---

## 7. package.xml

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>m8_hmi_transparency_bridge</name>
  <version>0.1.0</version>
  <description>
    M8 HMI/Transparency Bridge — SAT-1/2/3 三层透明性 + ToR 协议 + Web 后端
    （L3 战术决策层；架构 v1.1.2 §12 + RFC-004）
  </description>
  <maintainer email="team-m8@mass-l3.local">Team-M8</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_cmake</buildtool_depend>
  <buildtool_depend>ament_cmake_gtest</buildtool_depend>
  <buildtool_depend>ament_cmake_pytest</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>rclpy</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>eigen</depend>
  <depend>nlohmann-json-dev</depend>
  <depend>spdlog</depend>
  <depend>yaml-cpp</depend>

  <exec_depend>python3-fastapi</exec_depend>
  <exec_depend>python3-uvicorn</exec_depend>
  <exec_depend>python3-websockets</exec_depend>
  <exec_depend>python3-pydantic</exec_depend>

  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_lint_common</test_depend>
  <test_depend>python3-pytest</test_depend>
  <test_depend>python3-pytest-cov</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 8. 单元测试骨架

### 8.1 SatAggregator（多源聚合）

```cpp
// test/test_sat_aggregator.cpp
#include <gtest/gtest.h>
#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

using mass_l3::m8::SatAggregator;
using SrcMod = SatAggregator::SourceModule;

namespace {
l3_msgs::msg::SATData make_sat_msg(const std::string& src,
                                   const std::string& summary) {
  l3_msgs::msg::SATData m{};
  m.source_module = src;
  m.sat1.state_summary = summary;
  return m;
}
}  // namespace

TEST(SatAggregator, IngestSingleM1Msg_LatestSat1Returned) {
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M1", "TRANSIT @ D3, ODD-A, 18 kn"), now);
  auto sat1 = agg.latest_sat1(SrcMod::kM1);
  ASSERT_TRUE(sat1.has_value());
  EXPECT_EQ(sat1->state_summary, "TRANSIT @ D3, ODD-A, 18 kn");
  EXPECT_FALSE(agg.latest_sat1(SrcMod::kM7).has_value());
}

TEST(SatAggregator, MissingModule_AgeReturnsInfinity) {
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  EXPECT_GT(agg.age_seconds(SrcMod::kM7, now), 1e9);
}

TEST(SatAggregator, StaleDetectionUsesThreshold) {
  SatAggregator agg;
  auto t0 = SatAggregator::Clock::now();
  agg.ingest(make_sat_msg("M2", "x"), t0);
  auto t1 = t0 + std::chrono::milliseconds(1500);
  EXPECT_TRUE(agg.is_stale(SrcMod::kM2, t1, 1.0));
  EXPECT_FALSE(agg.is_stale(SrcMod::kM2, t1, 2.0));
}

TEST(SatAggregator, SourceModuleNameRoundTrip) {
  EXPECT_EQ(SatAggregator::to_string(SrcMod::kM7), "M7");
  EXPECT_EQ(SatAggregator::from_string("M4").value(), SrcMod::kM4);
  EXPECT_FALSE(SatAggregator::from_string("Mx").has_value());
}
```

### 8.2 AdaptiveSatTrigger（自适应触发）

```cpp
// test/test_adaptive_sat_trigger.cpp
#include <gtest/gtest.h>
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"

using mass_l3::m8::AdaptiveSatTrigger;
using mass_l3::m8::SatAggregator;

namespace {
AdaptiveSatTrigger::Thresholds default_thresholds() {
  return AdaptiveSatTrigger::Thresholds{
      30.0, 0.6, 0.7F, 0.8F};
}
l3_msgs::msg::ODDState make_odd(float tdl_s) {
  l3_msgs::msg::ODDState m{};
  m.tdl_s = tdl_s;
  m.tmr_s = tdl_s + 30.0F;
  m.current_zone = l3_msgs::msg::ODDState::ODD_ZONE_A;
  return m;
}
l3_msgs::msg::SafetyAlert make_alert(uint8_t severity, uint8_t type) {
  l3_msgs::msg::SafetyAlert a{};
  a.severity = severity;
  a.type = type;
  return a;
}
}  // namespace

TEST(AdaptiveSatTrigger, NormalTransitOnlySat1And3Visible) {
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  auto decision = trig.decide(make_odd(120.0F), agg, std::nullopt,
                              std::nullopt, false, now);
  EXPECT_TRUE(decision.sat1_visible);
  EXPECT_FALSE(decision.sat2_visible);
  EXPECT_TRUE(decision.sat3_visible);
  EXPECT_FALSE(decision.sat3_priority_high);
}

TEST(AdaptiveSatTrigger, TdlBelow30sPromotesSat3) {
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();
  auto decision =
      trig.decide(make_odd(25.0F), agg, std::nullopt, std::nullopt, false, now);
  EXPECT_TRUE(decision.sat3_priority_high);
  EXPECT_EQ(decision.sat3_alert_color, "bold_red");
}

TEST(AdaptiveSatTrigger, M7SotifAlertTriggersSat2) {
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto alert = make_alert(/*severity*/ 2, /*type SOTIF*/ 1);
  auto decision = trig.decide(make_odd(60.0F), agg, alert, std::nullopt, false,
                              SatAggregator::Clock::now());
  EXPECT_TRUE(decision.sat2_visible);
  EXPECT_TRUE(std::find(decision.sat2_trigger_reasons.begin(),
                        decision.sat2_trigger_reasons.end(),
                        "m7_sotif_warning") !=
              decision.sat2_trigger_reasons.end());
}

TEST(AdaptiveSatTrigger, OperatorRequestTriggersSat2EvenWithoutConflict) {
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto decision = trig.decide(make_odd(90.0F), agg, std::nullopt, std::nullopt,
                              /*operator_requested*/ true,
                              SatAggregator::Clock::now());
  EXPECT_TRUE(decision.sat2_visible);
}
```

### 8.3 TorProtocol（IMO MASS Code C 交互验证）

```cpp
// test/test_tor_protocol.cpp
#include <gtest/gtest.h>
#include <thread>
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

using mass_l3::m8::TorProtocol;

namespace {
TorProtocol::Config default_cfg() {
  return TorProtocol::Config{60.0, 5.0, 30.0, 1};
}
}  // namespace

TEST(TorProtocol, IdleAtConstruction) {
  TorProtocol p(default_cfg());
  EXPECT_EQ(p.state(), TorProtocol::State::kIdle);
}

TEST(TorProtocol, TriggerMovesToRequested) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  EXPECT_TRUE(p.trigger(TorProtocol::Reason::kOddExit, t0));
  EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
  EXPECT_FALSE(p.trigger(TorProtocol::Reason::kManualRequest, t0));  // 拒绝重入
}

TEST(TorProtocol, ButtonDisabledWithin5sOfRequest) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_early = t0 + std::chrono::seconds(3);
  EXPECT_FALSE(p.is_acknowledgment_button_enabled(t_early));
  auto t_after = t0 + std::chrono::seconds(6);
  EXPECT_TRUE(p.is_acknowledgment_button_enabled(t_after));
}

TEST(TorProtocol, ClickBefore5sIsRejected) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_early = t0 + std::chrono::seconds(3);
  auto snap = p.on_acknowledgment_clicked(t_early, {}, "ODD_A", 0.9F, "OP-001");
  EXPECT_FALSE(snap.has_value());
}

TEST(TorProtocol, ClickAfter5sCapturesSnapshotAndAdvances) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_click = t0 + std::chrono::seconds(7);
  auto snap = p.on_acknowledgment_clicked(
      t_click, {"target_42", "target_57"}, "ODD_B", 0.72F, "OP-001");
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(p.state(), TorProtocol::State::kAcknowledged);
  EXPECT_DOUBLE_EQ(snap->sat1_display_duration_s, 7.0);
  EXPECT_EQ(snap->sat1_threats_visible.size(), 2U);
  EXPECT_EQ(snap->odd_zone_at_click, "ODD_B");
  EXPECT_FLOAT_EQ(snap->conformance_score_at_click, 0.72F);
}

TEST(TorProtocol, Tick60sTimeoutLeadsToMrc) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_late = t0 + std::chrono::seconds(61);
  EXPECT_TRUE(p.tick(t_late));
  EXPECT_EQ(p.state(), TorProtocol::State::kTimeoutMrc);
}

TEST(TorProtocol, ResetReturnsToIdle) {
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  p.reset();
  EXPECT_EQ(p.state(), TorProtocol::State::kIdle);
}
```

### 8.4 UiStateBuilder

```cpp
// test/test_ui_state_builder.cpp
#include <gtest/gtest.h>
#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"

using mass_l3::m8::UiStateBuilder;
using mass_l3::m8::SatAggregator;
using mass_l3::m8::SatTriggerDecision;
using mass_l3::m8::TorProtocol;

TEST(UiStateBuilder, TransitScenarioProducesSat1OnlyForCaptain) {
  UiStateBuilder builder;
  SatAggregator agg;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kShipCaptain;
  ctx.scenario = UiStateBuilder::Scenario::kTransit;
  ctx.sat_decision = SatTriggerDecision{};
  ctx.tor_state = TorProtocol::State::kIdle;
  auto msg = builder.build(ctx, agg);
  EXPECT_TRUE(msg.sat1_visible);
  // Captain 视图：SAT-2/3 隐藏（详细设计附录 A）
  EXPECT_FALSE(msg.sat2_visible);
}

TEST(UiStateBuilder, MrcPreparationShowsSat3PriorityHigh) {
  UiStateBuilder builder;
  SatAggregator agg;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kRocOperator;
  ctx.scenario = UiStateBuilder::Scenario::kMrcPreparation;
  ctx.sat_decision.sat3_priority_high = true;
  ctx.sat_decision.sat3_alert_color = "bold_red";
  ctx.tor_state = TorProtocol::State::kRequested;
  ctx.tor_remaining_s = 47.0;
  auto msg = builder.build(ctx, agg);
  EXPECT_TRUE(msg.sat3_priority_high);
  EXPECT_EQ(msg.sat3_alert_color, "bold_red");
  EXPECT_TRUE(msg.tor_active);
  EXPECT_NEAR(msg.tor_remaining_s, 47.0, 1e-9);
}

TEST(UiStateBuilder, OverrideActiveShowsM7DegradationAlert) {
  UiStateBuilder builder;
  SatAggregator agg;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kRocOperator;
  ctx.scenario = UiStateBuilder::Scenario::kOverrideActive;
  ctx.override_active = true;
  ctx.m7_degradation_alert_active = true;
  ctx.m7_degradation_alert_text = "comm_link_rtt_3s";
  auto msg = builder.build(ctx, agg);
  EXPECT_TRUE(msg.override_active);
  EXPECT_EQ(msg.m7_degradation_alert, "comm_link_rtt_3s");
}
```

### 8.5 Python pytest（FastAPI 端点 / WebSocket）

```python
# python/tests/test_app.py
"""Pytest 覆盖：FastAPI 应用启停 + CORS。"""
from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from web_server.app import create_app


def test_app_creation_with_cors():
    app = create_app(cors_origins=["http://localhost:3000"])
    client = TestClient(app)
    resp = client.options(
        "/api/tor/acknowledge",
        headers={
            "Origin": "http://localhost:3000",
            "Access-Control-Request-Method": "POST",
        },
    )
    assert resp.status_code in {200, 204}


def test_unknown_origin_blocked_by_cors():
    app = create_app(cors_origins=["http://localhost:3000"])
    client = TestClient(app)
    resp = client.options(
        "/api/tor/acknowledge",
        headers={
            "Origin": "http://evil.example.com",
            "Access-Control-Request-Method": "POST",
        },
    )
    # CORS 不匹配时无 Allow-Origin 头
    assert "access-control-allow-origin" not in {
        k.lower() for k in resp.headers.keys()
    } or resp.headers.get("access-control-allow-origin") != "http://evil.example.com"


# python/tests/test_schemas.py
"""pydantic 模型校验 — UiStateSchema 边界情况。"""
import pytest
from pydantic import ValidationError

from web_server.schemas import (
    Sat1ThreatSchema, TorAcknowledgeRequest, UiStateSchema
)


def test_sat1_threat_confidence_bounds():
    Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                     aspect="head_on", confidence=0.0)
    Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                     aspect="head_on", confidence=1.0)
    with pytest.raises(ValidationError):
        Sat1ThreatSchema(target_id=1, cpa_m=500.0, tcpa_s=120.0,
                         aspect="head_on", confidence=1.5)


def test_tor_acknowledge_rejects_empty_id():
    with pytest.raises(ValidationError):
        TorAcknowledgeRequest(operator_id="")


def test_ui_state_rejects_unknown_scenario():
    from datetime import datetime, timezone
    with pytest.raises(ValidationError):
        UiStateSchema(
            timestamp=datetime.now(tz=timezone.utc),
            role="roc_operator",
            scenario="unknown_scenario",  # invalid Literal
            sat1_visible=True, sat2_visible=False, sat3_visible=True,
            sat3_priority_high=False, sat3_alert_color="normal",
            sat1_state_summary="x", sat1_threats=[],
            sat3_tdl_s=120.0, sat3_tmr_s=150.0,
            tor_active=False, tor_remaining_s=0.0,
            tor_button_enabled=False, override_active=False,
        )


# python/tests/test_websocket.py（占位，需要 anyio + WebSocket TestClient）
def test_websocket_broadcast_smoke():
    """烟雾测试：模拟 broadcast_ui_state 调用不抛异常。
    完整的 WS 长连接测试需要 pytest-anyio + uvicorn 实例 — 后续 wave 补。
    """
    # TODO(team-m8): 完整端到端 WS 测试在 Phase E2 集成测试中补
```

---

## 9. 配置文件

### 9.1 `config/m8_params.yaml`

参见 §3.4 完整内容（自适应触发阈值 + ToR deadline + 心跳超时 + 消息丢失阈值；所有 `[TBD-HAZID]` 标注的参数将在 RUN-001 完成后通过 YAML 热加载回填）。

### 9.2 `config/m8_web_server.yaml`

参见 §3.4（FastAPI 端口 + CORS 白名单 + WebSocket 客户端上限）。

### 9.3 `config/m8_logger.yaml`

参见 §3.4（spdlog rotating file sink + stdout color）。

### 9.4 `launch/m8_hmi_bridge.launch.py`（启动 C++ 节点 + Python 后端）

```python
"""M8 主 launch — 同时拉起 C++ ROS2 节点与 Python FastAPI 后端。"""
from launch import LaunchDescription
from launch.actions import ExecuteProcess
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.substitutions import PathJoinSubstitution
import os


def generate_launch_description() -> LaunchDescription:
    pkg_share = FindPackageShare("m8_hmi_transparency_bridge")
    params_file = PathJoinSubstitution([pkg_share, "config", "m8_params.yaml"])
    web_cfg_file = PathJoinSubstitution(
        [pkg_share, "config", "m8_web_server.yaml"]
    )

    cpp_node = Node(
        package="m8_hmi_transparency_bridge",
        executable="m8_hmi_transparency_bridge_node",
        name="m8_hmi_transparency_bridge",
        parameters=[params_file],
        output="screen",
    )

    # Python 后端（独立进程 — uvicorn）
    py_path = os.path.join(
        "$(ros2 pkg prefix m8_hmi_transparency_bridge)",
        "lib", "m8_hmi_transparency_bridge", "python",
    )
    py_backend = ExecuteProcess(
        cmd=[
            "python3", "-m", "web_server.main",
            "--config", web_cfg_file,
        ],
        cwd=py_path,
        output="screen",
    )

    return LaunchDescription([cpp_node, py_backend])
```

---

## 10. PATH-H 合规性 Checklist

> 来源：`coding-standards.md` §6 + `static-analysis-policy.md` §6（M8 简化裁剪集 ~120 规则）。CI 阻断条件由 `tools/ci/lint.sh` + `tools/ci/static-analysis.sh` 强制；本 checklist 用于 PR 自检。

### 10.1 强制保留（CI 阻断）

- [ ] **§3.1 类型安全完整保留**：所有 `<cstdint>` 显式宽度类型；`static_cast` 替代 C 风格强转；时间戳统一 `int64_t`，浮点统一 `double`
- [ ] **§3.2 RAII 完整保留**：所有资源用 `std::unique_ptr<>` / `std::shared_ptr<>` / RAII 包装；M8 类成员 `Pimpl` 或裸字段都需符合 RAII
- [ ] **§3.5 控制流完整保留**：`if/else`/`switch`/loop 必有 `{}`；`switch` 必含 `default:`；fall-through 须 `[[fallthrough]]`；函数行数 ≤ 60 行；圈复杂度 ≤ 10
- [ ] **§3.6 浮点比较**：禁用 `==` / `!=`；所有时间用 `std::chrono`；角度统一 rad（边界 deg）
- [ ] **clang-tidy 规则集**：`.clang-tidy` 中 PATH-H 配置；CI 阻断
- [ ] **cppcheck Premium**：MISRA C++:2023 简化裁剪规则集；CI 阻断
- [ ] **AddressSanitizer + UBSan**：单元测试在 Debug 构建下运行；0 错误
- [ ] **Python：Ruff + mypy strict**：CI 阻断；`pyproject.toml` 锁配置

### 10.2 PATH-H 豁免（M8 专属）

- [x] **动态分配允许**：HTTP 请求/响应、WebSocket 帧、JSON 序列化天然动态；不强制 M1/M7 的"启动后零分配"硬规则
- [x] **异常允许**：FastAPI / pydantic / nlohmann::json 普遍用异常；C++ 节点 `rclcpp::spin()` 边界 `catch(...)` 即可
- [x] **AUTOSAR A8-5-2 / A12-1-1 等模板深度严约束**：裁剪（保留基础 RAII + 多态）
- [x] **MISRA Rule 21.x 全禁用 malloc/new**：M8 允许在控制下使用（须经 `nlohmann::json` / std 容器内部的分配；不裸 `new`）

### 10.3 测试覆盖率门禁

- [ ] C++ 单元测试覆盖率（lcov/gcov）**≥ 80%**（PATH-H 阈值）
- [ ] Python 单元测试覆盖率（pytest-cov）**≥ 80%**
- [ ] C++ 关键文件 100% 行覆盖：`tor_protocol.cpp`（IMO MASS Code 法律合规）、`asdr_logger.cpp`（CCS 审计追溯）

### 10.4 接口契约检查

- [ ] 所有发布的 .msg 含 `stamp` + `confidence` + `rationale`（命令型 `ToRRequest` 不强制 rationale；详见 IDL 指南 §1.3）
- [ ] `/l3/m8/ui_state` 实测 50 Hz @ Cyclone DDS（CI integration stage 验证）
- [ ] `/l3/m8/tor_request` 端到端推送时延 ≤ 200 ms（HIL 场景测试）
- [ ] M7 SafetyAlert → UI 显示 < 100 ms（接管期间；HIL 测试）
- [ ] ASDR 记录格式符合 RFC-004 决议 + v1.1.2 §15.1 ASDR_RecordMsg + SHA-256 签名

### 10.5 Doer-Checker 边界（M8 仅可视化，不参与决策）

- [ ] M8 不直接修改 M1-M7 的状态；不发布任何决策型 topic（如 BehaviorPlan / AvoidancePlan）
- [ ] M8 不调用 M1-M7 的 header 或库（`tools/ci/check-doer-checker-independence.sh` 通过）；与 Doer-Checker 主独立性约束（M7 vs M1-M6）正交，不冲突
- [ ] M8 自身的 Checker 仍由 M7 监督（M7 通过 ASDR 间接验证 M8 是否漏发 ToR / UIState）

---

## 11. 引用

| 引用 | 文献 | 用途 |
|---|---|---|
| [R4] | Veitch et al. (2024). *Ocean Engineering* 299. | ToR 时间窗口 60s 实证基线（§4.3 TorProtocol::Config + §3.4 yaml） |
| [R11] | Chen et al. (ARL-TR-7180, 2014). | SAT 三层透明性（§4.2 SatTriggerDecision） |
| [R2] | IMO MSC 110/111 MASS Code (2025/2026), Part 2 Chapter 1. | "有意味的人为干预" → "已知悉 SAT-1" C 交互验证（§4.3 TorProtocol::on_acknowledgment_clicked） |
| [R23] | Veitch & Alsos (2022), NTNU Shore Control Lab. | 从船长到按钮操作员认知退化 → SAT 自适应触发 |
| [R5-aug] | USAARL (2026-02) + NTNU Handover/Takeover (2024). | 透明度悖论 → SAT-2 按需触发（§4.2） |
| 详细设计 SANGO-ADAS-L3-DD-M8-001 | M8 详细设计 v1.0（2026-05-05） | 本 spec 上位规范 |
| 架构 v1.1.2 §12 + §12.2 + §12.4 + §12.4.1 | 主架构文件 | M8 章节 + SAT 自适应 + ToR + C 交互验证 |
| 架构 v1.1.2 §15.1 + §15.2 | 主架构文件 | 接口契约 IDL（SATData / ToRRequest / UIState / ASDRRecord） |
| RFC-004 决议 | `docs/Design/Cross-Team Alignment/RFC-decisions.md` | ASDR 接收 IDL + 50 events/s 吞吐 + REST 审计 API |
| `coding-standards.md` §6 | 项目编码规范 | M8 简化裁剪集 ~120 规则 |
| `static-analysis-policy.md` §6 | 项目静态分析 | clang-tidy / cppcheck Premium PATH-H 配置 |
| `ros2-idl-implementation-guide.md` §3 + §4 | 项目 IDL 指南 | SATData / ToRRequest / UIState .msg 字段 + QoS |
| `third-party-library-policy.md` §2.3 | 项目库策略 | FastAPI 0.115.x / uvicorn 0.30.x / websockets 13.x / pydantic 2.x |

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Team-M8（Claude 实施层 kickoff） | 初稿：M8 PATH-H 代码骨架；C++ 主节点（SatAggregator / AdaptiveSatTrigger / TorProtocol / UiStateBuilder / ToRRequestGenerator / HmiTransparencyBridgeNode）+ Python FastAPI 后端（app / websocket / tor_endpoint / schemas / pytest）+ CMake/package.xml/单元测试骨架 + PATH-H 合规 checklist |

---

## 附录 A：[TBD-HAZID] 参数清单（HAZID RUN-001 回填）

| 参数（YAML key） | 初始值 | 单位 | HAZID 决策点 |
|---|---|---|---|
| `sat_threat_confidence_threshold` | 0.7 | — | 多船 / 雷达-AIS 不一致场景下的最优阈值 |
| `sat2_rule_confidence_threshold` | 0.8 | — | M6 边界规则（如 Rule 17 直航船反向避让）显示决策 |
| `sat3_priority_high_tdl_s` | 30.0 | s | TDL 何时触发 SAT-3 全屏加粗（避免过度告警疲劳）|
| `sat2_system_confidence_drop_threshold` | 0.6 | — | 系统总体信心跌破值时是否触发 SAT-2 |
| `tor_deadline_s` | 60.0 | s | [R4] 基线，但 D4 远程 vs D3 协同可能需要分级 |
| `comm_latency_warn_m1_ms` | 500 | ms | M1 → M8 延迟告警阈值 |
| `comm_latency_warn_m2_ms` | 1000 | ms | M2 → M8 延迟告警阈值 |
| `comm_latency_critical_m7_ms` | 200 | ms | M7 → M8 延迟告警（CRITICAL）阈值 |
| `msg_loss_critical_m7_frames` | 2 | 帧 | M7 连续丢失多少帧后假设 M7 失效 |
| `msg_loss_critical_m2_frames` | 5 | 帧 | M2 连续丢失多少帧后禁止决策 |

> **回填流程**：HAZID 工作组完成 RUN-001 后，输出 `docs/Design/HAZID/RUN-001-results.md`；Team-M8 在 v1.1.3 patch 中：(1) 仅修改 `config/m8_params.yaml` 数值；(2) 不重编译 C++；(3) 通过 `ros2 param load` 热加载 → 生产 ASDR 记录中保留参数版本号。

---

## 附录 B：CI 5 阶段对应（`ci-cd-pipeline.md`）

| 阶段 | M8 检查 |
|---|---|
| **Stage 1: Lint** | `clang-format`（C++）+ `Ruff`（Python） + `.clang-tidy` PATH-H 规则集 |
| **Stage 2: Unit** | GTest（7 个 test_*.cpp）+ pytest（4 个 test_*.py）+ coverage ≥ 80% |
| **Stage 3: Static** | cppcheck Premium MISRA C++:2023 裁剪集 + mypy strict + `tools/ci/check-doer-checker-independence.sh`（验证 M8 不引用 M1-M7 header）|
| **Stage 4: Integration** | mock SATData publisher → 完整 SAT 聚合链路验证；ToR 60s 时窗精度 ± 100 ms；UIState 50 Hz 实测 ≥ 45 fps |
| **Stage 5: Release** | colcon build Release `-O2` + DDS QoS 实测 + Docker 镜像构建 |

---

**文档结束**
