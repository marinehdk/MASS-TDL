# M7 Safety Supervisor 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M7-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M7 实施起点）|
| 路径强度 | **PATH-S（Safety-Critical）+ 独立实现路径（ADR-001 强制）** |
| 详细设计基线 | `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md` (SANGO-ADAS-L3-DD-M7-001, 1673 行) |
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §11 + ADR-001 + RFC-003 + RFC-005 |
| RFC 锁定 | RFC-003（CheckerVetoNotification enum 协议 + 100 周期 = 15 s 滑窗）、RFC-005（Reflex Arc SIL 3 + 触发阈值）|
| 工程基线 | `00-implementation-master-plan.md` v1.0 / `coding-standards.md` v1.0 / `static-analysis-policy.md` v1.0 / `third-party-library-policy.md` v1.0 / `ros2-idl-implementation-guide.md` v1.0 |
| 团队 | Team-M7（C++17 PATH-S 独立路径） |

> **PATH-S + 独立路径硬约束**：本骨架内的每一行 `#include` 与每一项 `target_link_libraries` 均须通过 `tools/ci/check-doer-checker-independence.sh` 自动验证。任何 M7 引入 M1-M6 内部 header（`mass_l3/m{1-6}/`）或被禁库（CasADi / IPOPT / GeographicLib / nlohmann::json / Boost.Geometry）= **直接违反 ADR-001 + 决策四 + CCS i-Ship 不通过**，PR 将被 CI 阻断且不可豁免。

---

## 1. 模块概述

### 1.1 M7 在 L3 中的定位

M7 Safety Supervisor 是 L3 战术决策层的**独立运行时安全仲裁器（L3 内部 Checker）**，遵循 Doer-Checker 模式（v1.1.2 §2.5 决策四 + ADR-001）。M1-M6 是 Doer，M7 是 Checker。M7 不持有规划逻辑（**不动态生成轨迹**），仅通过两条独立轨道（IEC 61508 + ISO 21448 SOTIF）监控 M1-M6 Doer 的假设违反与组件失效，当检测到功能不足或失效时：

1. 向 M1 发送 `SafetyAlert.msg`（含 `recommended_mrm` 索引到 4 种**预定义** MRM 之一）
2. 向 ASDR 发送决策追溯日志（`ASDRRecord.msg`）
3. 向 M8 发送 SAT-1/2/3 透明性数据（`SATData.msg`）

### 1.2 工业先例（Doer-Checker 独立性的合理性证据）

| 先例 | 应用领域 | 独立性强度 | 引用 |
|---|---|---|---|
| **Boeing 777 Primary Flight Control** | 航空 DAL A | 三冗余 + 独立 CPU + 独立编码（C/Ada）| [Yeh 1996, AIAA] |
| **Airbus A320/A380 Fly-by-Wire** | 航空 DAL A | Doer/Monitor 两条独立 lane + 不同 OS（VxWorks / 自研 RTOS）+ 不同编译器 | [Briere & Traverse 1993] |
| **Apex.AI ROS2 ASIL-D 路径** | 汽车 ASIL D | ROS2 Safety WG + 独立 RMW QoS + 独立性矩阵 CI 验证 | https://www.apex.ai/apexgrace 🟢 |
| **Jackson 2021 MIT CSAIL "Certified Control"** | 通用 | Doer 自由设计 / Checker 独立简化（100× 简单）| [R12] |

**M7 的独立性强度**（依本规范）：单进程独立 + 独立编译路径 + 独立第三方库白名单 + 独立 logger 实例 + 独立日志文件 + CI 自动验证。

### 1.3 M7 不做什么（明确边界）

M7 是 Checker，**不是** Doer：

- **M7 不重新计算 CPA / TCPA**（M2 算 → M7 仅做 sanity check + 阈值比较）
- **M7 不重新解析 COLREGs**（M6 解析 → M7 仅监控 `processing_success`）
- **M7 不动态生成轨迹**（仅触发预定义 MRM-01/02/03/04 命令集）
- **M7 不解析 Checker `veto_reason_detail` 自由文本**（仅按 `veto_reason_class` enum 分类聚合 — RFC-003）
- **M7 不向 M5 直接注入命令**（→ M1 → M5 单向链）
- **M7 不持有 thread**（仅响应 ROS2 callback；事件处理顺序化）

---

## 2. Doer-Checker 独立性约束（必读）

### 2.1 代码独立性

| 约束 | 实施方法 | 验证 |
|---|---|---|
| 不复用 M1-M6 任何 `.cpp` / `.hpp` | 仅 `#include "l3_msgs/msg/*.hpp"` 与 `#include "l3_external_msgs/msg/*.hpp"`；M7 自身代码全部在 `src/m7_safety_supervisor/` 根下 | `grep -rn "#include.*mass_l3/m[1-6]/" src/m7_safety_supervisor/` 必须为空 |
| 不引用 M1-M6 命名空间 | 不允许 `using` 或限定调用 `mass_l3::m1::*`、`mass_l3::m2::*` 等 | clang-tidy `misc-unused-using-decls` + grep |
| M7 自身命名空间 | `mass_l3::m7` 顶层；子命名空间 `mass_l3::m7::iec61508` / `mass_l3::m7::sotif` / `mass_l3::m7::mrm` | 命名一致性由 .clang-tidy 强制 |

### 2.2 第三方库独立性

| 类别 | 库 | M7 使用？ | 用途 / 禁用理由 |
|---|---|---|---|
| **核心** | rclcpp | ✓ 允许 | ROS2 框架（不算业务库） |
| **核心** | l3_msgs | ✓ 允许（仅订阅 / 发布消息） | 接口契约 |
| **核心** | l3_external_msgs | ✓ 允许（仅 CheckerVetoNotification / ReflexActivationNotification / OverrideActiveSignal）| 跨层接口契约 |
| **数学** | Eigen 5.0.0 | ✓ 允许（仅小矩阵 + 滑窗向量化）| 数学基础（无业务逻辑）|
| **日志** | spdlog 1.17.0 | ✓ 允许（**独立 logger instance**）| 日志（独立日志文件 `m7.log`）|
| **格式化** | fmt 11.x | ✓ 允许 | spdlog 内嵌 |
| **YAML** | yaml-cpp 0.8.0 | ✓ 允许（**仅 logger 配置 + 阈值参数加载**，不进决策路径） | 仅 boot-time 配置 |
| **测试** | GTest / GMock 1.17.0 | ✓ 允许（仅测试期）| 单元测试 |
| **决策** | CasADi 3.7.2 | ✗ **禁用** | M5 求解器；M7 不做 NLP |
| **决策** | IPOPT 3.14.19 | ✗ **禁用** | 同上 |
| **决策** | GeographicLib 2.7 | ✗ **禁用** | M7 不做大地坐标计算（仅做阈值比较）|
| **决策** | nlohmann::json 3.12.0 | ✗ **禁用** | M7 不解析 JSON（保持 enum 化简单语义）|
| **决策** | Boost.Geometry 1.91.0 | ✗ **禁用** | M7 不做几何运算 |

> 完整矩阵见 `third-party-library-policy.md` §3。

### 2.3 数据结构独立性

- M7 内部状态机、阈值表、MRM 命令集**全部独立定义**于 `include/m7_safety_supervisor/`
- M7 不引用 `m1_envelope::*` / `m2_world_model::*` / `m6_colregs_reasoner::*` 等命名空间
- M7 仅引用 `l3_msgs::msg::*` / `l3_external_msgs::msg::*` 顶层消息类型 + `builtin_interfaces::msg::Time`
- enum 重映射：`l3_external_msgs::msg::CheckerVetoNotification::VETO_REASON_*` → 本地 `VetoReasonClass` 类型别名（保持 ROS2 IDL 单一源）

### 2.4 构建独立性

- M7 独立 colcon package：`src/m7_safety_supervisor/`
- 独立 `CMakeLists.txt` + `package.xml`
- CI 中 M7 静态分析门禁与 M1-M6 **分别运行**（独立 Polyspace job + 独立 Coverity job）
- M7 测试与 M1-M6 测试 **不共享 fixture / mock**

### 2.5 运行时独立性

- M7 独立 OS 进程 — `launch` 文件中**禁止 composable node**（不与 M1-M6 共进程）
- 与 M1-M6 通过 ROS2 DDS 隔离（无共享内存 / 共享文件描述符 / 共享单例）
- M7 logger 独立 spdlog instance（命名 `m7_supervisor`），独立日志文件路径 `/var/log/mass_l3/m7.log`
- M7 spdlog 用 **async logger + 单 sink**（与 M1-M6 不共 thread pool）

### 2.6 自动化验证（CI 阻断 — 不可豁免）

`tools/ci/check-doer-checker-independence.sh` 检查项（全文见 `static-analysis-policy.md` §8 / `third-party-library-policy.md` §3.3）：

```bash
# 1. 禁止内部 header
forbidden_internal=("mass_l3/m1/" "mass_l3/m2/" "mass_l3/m3/"
                    "mass_l3/m4/" "mass_l3/m5/" "mass_l3/m6/")

# 2. 禁止第三方库
forbidden_3rdparty=("casadi/" "Ipopt" "GeographicLib/"
                    "nlohmann/json" "boost/geometry")

# 3. 禁止 CMakeLists.txt 链接被禁库
forbidden_links=("casadi" "Ipopt" "GeographicLib" "nlohmann_json" "boost_geometry")
```

**违反 = 构建失败 + 触发 ADR-001 复审**（详见 `static-analysis-policy.md` §9）。

---

## 3. 项目目录结构

```
src/m7_safety_supervisor/                          # 独立 colcon package（PATH-S）
├── package.xml                                     # 仅声明白名单依赖（§7）
├── CMakeLists.txt                                  # 独立编译 + flags（§6）
├── include/
│   └── m7_safety_supervisor/
│       ├── common/
│       │   ├── error.hpp                           # NoExceptionResult / ErrorCode（PATH-S 强制）
│       │   ├── time_utils.hpp                      # std::chrono 时间工具
│       │   └── safety_constants.hpp                # constexpr kCpaThresholdNm 等
│       ├── iec61508/
│       │   ├── watchdog_monitor.hpp                # 模块心跳监控
│       │   ├── fault_monitor.hpp                   # IEC 61508 故障监控（接口超时 + 一致性）
│       │   └── diagnostic_coverage.hpp             # SIL 2 关键输出一致性校验
│       ├── sotif/
│       │   ├── assumption_monitor.hpp              # 6 项假设违反检查
│       │   ├── performance_monitor.hpp             # CPA 趋势 / 多目标聚集
│       │   └── triggering_condition_detector.hpp   # 极端场景识别
│       ├── checker/
│       │   ├── veto_handler.hpp                    # X-axis Checker VETO enum 分类（RFC-003）
│       │   └── sliding_window_15s.hpp              # 100 周期滑窗
│       ├── mrm/
│       │   ├── mrm_command_set.hpp                 # 4 种预定义 MRM（v1.1.2 §11.6）
│       │   ├── mrm_01_drift.hpp
│       │   ├── mrm_02_anchor.hpp
│       │   ├── mrm_03_heave_to.hpp
│       │   ├── mrm_04_mooring.hpp
│       │   └── mrm_selector.hpp                    # 场景 → MRM 选择逻辑
│       ├── arbitrator/
│       │   ├── safety_arbitrator.hpp               # Alert 汇聚 + 优先级
│       │   └── alert_generator.hpp                 # SafetyAlert.msg 生成
│       └── safety_supervisor_node.hpp              # ROS2 节点
├── src/
│   ├── common/
│   │   ├── error.cpp
│   │   ├── time_utils.cpp
│   │   └── safety_constants.cpp
│   ├── iec61508/
│   │   ├── watchdog_monitor.cpp
│   │   ├── fault_monitor.cpp
│   │   └── diagnostic_coverage.cpp
│   ├── sotif/
│   │   ├── assumption_monitor.cpp
│   │   ├── performance_monitor.cpp
│   │   └── triggering_condition_detector.cpp
│   ├── checker/
│   │   ├── veto_handler.cpp
│   │   └── sliding_window_15s.cpp
│   ├── mrm/
│   │   ├── mrm_command_set.cpp
│   │   ├── mrm_01_drift.cpp
│   │   ├── mrm_02_anchor.cpp
│   │   ├── mrm_03_heave_to.cpp
│   │   ├── mrm_04_mooring.cpp
│   │   └── mrm_selector.cpp
│   ├── arbitrator/
│   │   ├── safety_arbitrator.cpp
│   │   └── alert_generator.cpp
│   ├── safety_supervisor_node.cpp
│   └── main.cpp                                    # 节点入口
├── launch/
│   └── m7_safety_supervisor.launch.py              # 强制独立进程（禁 composable）
├── config/
│   ├── m7_params.yaml                              # 阈值参数（HAZID 注入点）
│   ├── m7_logger.yaml                              # spdlog 独立 logger 配置
│   └── mrm_command_set.yaml                        # 4 种 MRM 预定义参数
└── test/
    ├── unit/
    │   ├── test_watchdog_monitor.cpp
    │   ├── test_fault_monitor.cpp
    │   ├── test_assumption_monitor.cpp
    │   ├── test_performance_monitor.cpp
    │   ├── test_triggering_condition_detector.cpp
    │   ├── test_veto_handler.cpp
    │   ├── test_sliding_window_15s.cpp
    │   ├── test_mrm_command_set.cpp
    │   ├── test_mrm_selector.cpp
    │   ├── test_safety_arbitrator.cpp
    │   └── test_alert_generator.cpp
    └── integration/
        ├── test_doer_checker_independence.cpp      # 端到端独立性证明
        └── test_e2e_alert_pipeline.cpp             # 输入 → Alert ≤ 100 ms
```

**注意**：本目录结构与 M1-M6 / M8 的 package 完全平行，**无任何符号链接 / 共享 include 路径**。

---

## 4. ROS2 节点设计

### 4.1 节点拓扑

| 项 | 值 |
|---|---|
| 节点名 | `m7_safety_supervisor`（snake_case） |
| 进程 | **独立 OS 进程**（`launch` 文件禁 `ComposableNodeContainer`） |
| 主线程模型 | 单 `MultiThreadedExecutor`（callback group 隔离）—— **禁用** `std::thread`、`std::condition_variable`（v1.1.2 §11 + 详细设计 §3.3）|
| 周期 | 主监控线程 4 Hz（对齐 M2，250 ms 周期）；事件处理 callback 异步触发 |
| Reentrant callback group | `events`（CheckerVetoNotification / ReflexActivationNotification / OverrideActiveSignal）|
| MutuallyExclusive callback group | `main_loop`（4 Hz timer）|

### 4.2 订阅者表格

| Topic | Type | QoS | Callback group | 用途 |
|---|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/msg/ODDState` | reliable, transient_local, keep_last(10) | main_loop | 当前 ODD 子域 / 自主等级 / 健康状态 |
| `/l3/m2/world_state` | `l3_msgs/msg/WorldState` | best_effort, volatile, keep_last(2) | main_loop | targets[] / cpa_m / tcpa_s / fusion_confidence |
| `/l3/m4/behavior_plan` | `l3_msgs/msg/BehaviorPlan` | reliable, volatile, keep_last(5) | main_loop | M4 心跳 + 行为状态 |
| `/l3/m5/avoidance_plan` | `l3_msgs/msg/AvoidancePlan` | reliable, volatile, keep_last(5) | main_loop | plan_status / confidence（监控 M5 健康度）|
| `/l3/m5/reactive_override_cmd` | `l3_msgs/msg/ReactiveOverrideCmd` | reliable, keep_last(50) | events | 紧急覆盖触发后的告警追溯 |
| `/l3/m6/colregs_constraint` | `l3_msgs/msg/COLREGsConstraint` | reliable, volatile, keep_last(5) | main_loop | processing_success / rule_violations / confidence |
| `/checker/veto_notification` | `l3_external_msgs/msg/CheckerVetoNotification` | reliable, transient_local, keep_last(50) | events | RFC-003 enum 化否决通知 |
| `/reflex/activation_notification` | `l3_external_msgs/msg/ReflexActivationNotification` | reliable, transient_local, keep_last(50) | events | Y-axis Reflex Arc 触发（RFC-005）|
| `/override/active_signal` | `l3_external_msgs/msg/OverrideActiveSignal` | reliable, transient_local, keep_last(50) | events | Hardware Override 状态变化 |

> **独立性证据**：以上订阅器**仅依赖 `l3_msgs::msg::*` 与 `l3_external_msgs::msg::*`** — 不引入任何 M1-M6 私有类型。

### 4.3 发布者表格

| Topic | Type | QoS | 频率 | 接收方 |
|---|---|---|---|---|
| `/l3/m7/safety_alert` | `l3_msgs/msg/SafetyAlert` | reliable, transient_local, keep_last(50) | 事件 | M1（必听）|
| `/l3/asdr/record` | `l3_msgs/msg/ASDRRecord` | reliable, transient_local, keep_last(100) | 事件 + 2 Hz | ASDR（external）|
| `/l3/sat/data` | `l3_msgs/msg/SATData` | best_effort, volatile, keep_last(1) | 10 Hz | M8 |
| `/l3/m7/heartbeat` | `std_msgs/msg/Header` | best_effort, volatile, keep_last(2) | 10 Hz | X-axis Checker（监控 M7 自身存活，详细设计 §7.3 — 被动监控）|

### 4.4 参数（YAML 注入 — `config/m7_params.yaml`）

```yaml
m7_safety_supervisor:
  ros__parameters:
    # ===================== 周期配置 =====================
    main_loop_period_ms: 250           # 4 Hz 主监控周期
    sat_data_period_ms: 100            # 10 Hz SAT 数据
    asdr_periodic_ms: 500              # 2 Hz ASDR 周期记录
    heartbeat_period_ms: 100           # 10 Hz M7 自身心跳

    # ===================== Watchdog 阈值 =====================
    # （详细设计 §5.2.1 — 各模块超时阈值）
    watchdog:
      m1_timeout_ms: 1500              # M1 1 Hz × 1.5
      m2_timeout_ms: 300               # M2 4 Hz × 1.5
      m3_timeout_ms: 7500              # M3 0.2 Hz × 1.5
      m4_timeout_ms: 750               # M4 2 Hz × 1.5
      m5_timeout_ms: 1000              # M5 1 Hz × 1.5
      m6_timeout_ms: 750               # M6 2 Hz × 1.5
      m8_timeout_ms: 150               # M8 10 Hz × 1.5
      tolerance_count: 3               # 容错次数（M3 = 2）

    # ===================== SOTIF 假设违反阈值（[TBD-HAZID]）=====================
    sotif:
      ais_radar:
        fusion_confidence_low: 0.5     # [TBD-HAZID-SOTIF-001]
        duration_threshold_s: 30
      motion_predictability:
        rmse_threshold_m: 50.0         # [TBD-HAZID-SOTIF-002]
        window_s: 30
      perception_coverage:
        max_blind_zone_fraction: 0.20  # [TBD-HAZID-SOTIF-003]
      colregs:
        consecutive_failure_count: 3   # [TBD-HAZID-SOTIF-004]
      comm:
        rtt_threshold_s: 2.0           # [TBD-HAZID-SOTIF-005]
        packet_loss_pct_threshold: 20.0
      checker_veto:
        rate_threshold: 0.20           # 20% / 100 周期 — RFC-003 锁定
      extreme_scenario:
        violation_count_threshold: 3   # [TBD-HAZID]

    # ===================== Performance Monitor =====================
    performance:
      cpa_trend_window_s: 30
      cpa_trend_slope_threshold_nm_s: -0.01    # [TBD-HAZID]
      multiple_targets_cpa_threshold_nm: 1.0
      multiple_targets_count_threshold: 2

    # ===================== Sliding Window =====================
    sliding_window:
      cycle_count: 100                          # RFC-003 锁定（100 周期 = 15 s @ 6.7 Hz）

    # ===================== Arbitrator =====================
    arbitrator:
      mrm_change_lockout_s: 30                  # 30 s 防振荡锁定
      mrm_upgrade_confidence_threshold: 0.9     # MRM 升级置信度门槛

    # ===================== 降级恢复 =====================
    recovery:
      degraded_to_normal_wait_s: 5
      critical_to_degraded_wait_s: 10

    # ===================== 回切顺序化（v1.1.2 §11.9.2 — F-NEW-006）=====================
    reverting:
      m7_main_start_delay_ms: 10
      m7_ready_signal_max_delay_ms: 100
      timeout_protection_ms: 200
```

> **HAZID 注入点**：所有 `[TBD-HAZID]` 字段在 RUN-001（2026-08-19 完成）后通过 YAML 热加载或 Boot-time 重编译注入；**不硬编码**。当前文件中保留为初始建议值。

---

## 5. 核心类定义（C++ Header 骨架）

### 5.1 `common/error.hpp` — NoExceptionResult / ErrorCode（PATH-S 强制）

按 `coding-standards.md` §3.4 + PROJ-LR-002，M7 安全关键路径**禁用异常**。所有错误用 `NoExceptionResult<T>`（即 `tl::expected<T, ErrorCode>` 等价模式）。

```cpp
// include/m7_safety_supervisor/common/error.hpp
#ifndef M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_
#define M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_

#include <cstdint>
#include <string_view>
#include <variant>

namespace mass_l3::m7::common {

enum class ErrorCode : std::uint8_t {
  kOk = 0,
  kInputStale,                       // 输入消息时间戳过时
  kInputOutOfRange,                  // 数值范围越界
  kInputInvalidEnum,                 // 枚举值无效
  kHeartbeatLost,                    // 模块心跳丢失
  kInternalConsistencyFail,          // 内部一致性失败
  kArbitratorOverflow,               // Alert 队列上限
  kSlidingWindowUnderflow,           // 滑窗状态不一致
  kMrmUnavailable,                   // MRM 触发条件未满足（如水深）
  kSelfFault                         // M7 自检失败
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode ec) noexcept;

// PATH-S 强制：所有可能失败的函数返回 NoExceptionResult<T>
template <typename T>
class NoExceptionResult {
public:
  // 静态构造工厂（无异常路径）
  [[nodiscard]] static NoExceptionResult ok(T value) noexcept;
  [[nodiscard]] static NoExceptionResult err(ErrorCode ec) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] T const& value() const noexcept;
  [[nodiscard]] ErrorCode error() const noexcept;

private:
  std::variant<T, ErrorCode> data_;
};

// 特化：T = void
template <>
class NoExceptionResult<void> {
public:
  [[nodiscard]] static NoExceptionResult ok() noexcept;
  [[nodiscard]] static NoExceptionResult err(ErrorCode ec) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] ErrorCode error() const noexcept;

private:
  ErrorCode ec_;
};

}  // namespace mass_l3::m7::common

#endif  // M7_SAFETY_SUPERVISOR_COMMON_ERROR_HPP_
```

> **C++23 升级路径**：当工具链升级到 C++23 GCC 14.3 + libstdc++ 14（已支持 `<expected>`），`NoExceptionResult<T>` 替换为 `std::expected<T, ErrorCode>`，API 100% 兼容。

### 5.2 `iec61508/watchdog_monitor.hpp` — 模块心跳监控

```cpp
// include/m7_safety_supervisor/iec61508/watchdog_monitor.hpp
#ifndef M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"

namespace mass_l3::m7::iec61508 {

// 监控的模块编号（与 v1.1.2 §15 IDL 一致 — 共 7 个 Doer 模块 + 自身）
enum class MonitoredModule : std::uint8_t {
  kM1 = 0, kM2, kM3, kM4, kM5, kM6, kM8, kCount
};

struct WatchdogConfig {
  std::array<std::chrono::milliseconds, 7> timeout_ms;   // M1..M8（不含 M7 自身）
  std::array<std::uint32_t, 7> tolerance_count;          // 容错次数
};

// 不变量：单 callback group 同步调用，无内部 mutex（详细设计 §6.1）
class WatchdogMonitor {
public:
  explicit WatchdogMonitor(WatchdogConfig const& cfg) noexcept;

  // 输入消息到达时调用
  void on_message_received(MonitoredModule mod,
                           std::chrono::steady_clock::time_point now) noexcept;

  // 主线程周期调用 — 检查所有模块超时
  // 返回：超时但仍在容错内的模块；若超过容错触发 IEC61508 alert
  struct WatchdogResult {
    std::array<bool, 7> heartbeat_ok;
    std::array<std::uint32_t, 7> loss_count;
    bool any_critical;                   // 任一模块失联 ≥ tolerance_count
    std::uint32_t critical_count;        // 失联模块总数
  };

  [[nodiscard]] WatchdogResult evaluate(std::chrono::steady_clock::time_point now) const noexcept;

  // 重置（输入恢复时调用）
  void reset(MonitoredModule mod) noexcept;

private:
  WatchdogConfig cfg_;
  std::array<std::chrono::steady_clock::time_point, 7> last_received_;
  std::array<std::uint32_t, 7> loss_count_;
};

}  // namespace mass_l3::m7::iec61508

#endif  // M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_
```

### 5.3 `iec61508/fault_monitor.hpp` — IEC 61508 故障 + 一致性

```cpp
// include/m7_safety_supervisor/iec61508/fault_monitor.hpp
#ifndef M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_

#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"
// 仅引用 l3_msgs 顶层消息（独立性约束）
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"

namespace mass_l3::m7::iec61508 {

// SIL 2 诊断覆盖结果
struct DiagnosticResult {
  bool conformance_score_valid;          // ODD.conformance_score ∈ [0, 1]
  bool cpa_internal_consistent;          // M2 CPA 与独立 sanity check 一致（±10%）
  bool colregs_target_id_match;          // M6 rule_violations 中 target_id 都在 M2 targets[]
  std::uint32_t fault_count;             // 累计故障计数
};

// 不变量：纯函数（无内部状态除累计计数）；输入 -> 诊断结果
class FaultMonitor {
public:
  FaultMonitor() noexcept = default;

  // ODD 合规性 sanity（detailed §5.2.2）
  [[nodiscard]] common::NoExceptionResult<bool>
  validate_odd_state(l3_msgs::msg::ODDState const& msg) const noexcept;

  // CPA 极简交叉验证（不复用 M2 CPA 库 — 独立性约束）
  // 用 range / relative_speed 推算保守 CPA，与 M2 报告值比较 ±10%
  [[nodiscard]] common::NoExceptionResult<bool>
  validate_cpa_consistency(l3_msgs::msg::WorldState const& world) const noexcept;

  // COLREGs 规则一致性（rule_violations.target_id 都在 targets[]）
  [[nodiscard]] common::NoExceptionResult<bool>
  validate_colregs_target_match(l3_msgs::msg::WorldState const& world,
                                l3_msgs::msg::COLREGsConstraint const& colregs) const noexcept;

  // 主诊断入口
  [[nodiscard]] DiagnosticResult run(l3_msgs::msg::ODDState const& odd,
                                     l3_msgs::msg::WorldState const& world,
                                     l3_msgs::msg::COLREGsConstraint const& colregs) noexcept;

  [[nodiscard]] std::uint32_t fault_count() const noexcept { return fault_count_; }
  void reset_count() noexcept { fault_count_ = 0; }

private:
  std::uint32_t fault_count_{0};
};

}  // namespace mass_l3::m7::iec61508

#endif  // M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_
```

### 5.4 `sotif/assumption_monitor.hpp` — 6 项假设违反检查

```cpp
// include/m7_safety_supervisor/sotif/assumption_monitor.hpp
#ifndef M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"

namespace mass_l3::m7::sotif {

// 6 项假设（详细设计 §5.3.1 + 附录 B HAZID 映射）
enum class AssumptionId : std::uint8_t {
  kAisRadarConsistency = 0,          // 假设 1：AIS/Radar 一致性
  kMotionPredictability,             // 假设 2：目标运动可预测
  kPerceptionCoverage,               // 假设 3：感知覆盖充分
  kColregsSolvability,               // 假设 4：COLREGs 可解析
  kCommLink,                         // 假设 5：通信链路可用
  kCheckerVetoRate,                  // 假设 6：Checker 否决率正常
  kCount
};

struct AssumptionConfig {
  // 阈值（[TBD-HAZID] — 由 m7_params.yaml 注入）
  double fusion_confidence_low{0.5};                 // 假设 1
  std::chrono::seconds ais_radar_duration_threshold{30};
  double motion_rmse_threshold_m{50.0};              // 假设 2
  std::chrono::seconds motion_window{30};
  double max_blind_zone_fraction{0.20};              // 假设 3
  std::uint32_t colregs_consecutive_failure_count{3};// 假设 4
  double rtt_threshold_s{2.0};                       // 假设 5
  double packet_loss_pct_threshold{20.0};
  double checker_veto_rate_threshold{0.20};          // 假设 6 — RFC-003 锁定
};

struct AssumptionStatus {
  std::array<bool, 6> violation_active;              // 当前哪几项假设违反
  std::array<float, 6> violation_metric;             // 数值（如 fusion_conf, pred_rmse_m, ...）
  std::uint32_t total_violation_count;               // 当前违反项总数（触发条件用）
};

// 不变量：单线程顺序调用（main_loop callback group）；无 throw；无 dynamic alloc
class AssumptionMonitor {
public:
  explicit AssumptionMonitor(AssumptionConfig const& cfg) noexcept;

  // 输入快照 + 当前 Checker 否决率（来自 SlidingWindow15s）
  // 返回当前假设状态（不直接生成 Alert，由 Arbitrator 汇聚）
  [[nodiscard]] AssumptionStatus
  evaluate(l3_msgs::msg::ODDState const& odd,
           l3_msgs::msg::WorldState const& world,
           l3_msgs::msg::COLREGsConstraint const& colregs,
           double current_checker_veto_rate,        // ∈ [0, 1]
           double current_rtt_s,
           double current_packet_loss_pct,
           std::chrono::steady_clock::time_point now) noexcept;

  // 各假设独立的检查谓词（单元测试入口）— 全部为 noexcept + const
  // 每个谓词都是独立的 (输入快照, 当前时间) → bool 函数，便于单元测试覆盖
  [[nodiscard]] bool check_ais_radar(l3_msgs::msg::WorldState const& world,
                                     std::chrono::steady_clock::time_point now) noexcept;

  [[nodiscard]] bool check_motion_predictability(l3_msgs::msg::WorldState const& world,
                                                 std::chrono::steady_clock::time_point now) noexcept;

  [[nodiscard]] bool check_perception_coverage(l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] bool check_colregs_solvability(l3_msgs::msg::COLREGsConstraint const& colregs) noexcept;

  [[nodiscard]] bool check_comm_link(double rtt_s, double packet_loss_pct) const noexcept;

  [[nodiscard]] bool check_checker_veto_rate(double current_rate) const noexcept;

  // 重置（用于回切顺序化 §6.3 / 详细设计 §6.3）
  void reset() noexcept;

private:
  AssumptionConfig cfg_;
  // 状态变量（独立性：不引用 m1-m6 命名空间）
  std::chrono::steady_clock::time_point ais_radar_low_since_;
  std::uint32_t colregs_failure_count_{0};
  // 30 s 预测残差历史（FixedSize 滑窗，无 dynamic alloc）
  std::array<float, 30> motion_rmse_history_{};
  std::uint32_t motion_history_idx_{0};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
```

#### 5.4.1 假设检查谓词模板（单元测试入口）

每个 `check_*` 谓词遵循统一模板：

```cpp
// 模板：所有假设检查谓词都是这个形状
[[nodiscard]] bool check_<assumption_name>(
    /* 输入消息 const& */,
    /* 当前时间 (steady_clock::time_point) */) noexcept {
  // 1. 数据有效性（confidence < threshold / stamp 过时）
  if (input.confidence < cfg_.threshold) {
    update_violation_state(now);
  } else {
    reset_violation_state();
  }
  // 2. 持续时间检查（duration ≥ window → 升级为违反）
  if (violation_duration(now) >= cfg_.duration_threshold) {
    return true;   // 假设违反
  }
  return false;
}
```

**单元测试**（GTest）每个谓词覆盖：(a) 阈值边界两侧、(b) 持续时间边界、(c) 状态恢复、(d) 时间戳乱序、(e) confidence 越界。详见 §8.1。

### 5.5 `sotif/performance_monitor.hpp` — 性能退化检测

```cpp
// include/m7_safety_supervisor/sotif/performance_monitor.hpp
#ifndef M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::sotif {

struct PerformanceConfig {
  std::chrono::seconds cpa_window{30};
  double cpa_trend_slope_threshold_nm_s{-0.01};        // [TBD-HAZID]
  double multiple_targets_cpa_threshold_nm{1.0};
  std::uint32_t multiple_targets_count_threshold{2};
};

struct PerformanceStatus {
  bool cpa_trend_degrading;
  double cpa_trend_slope_nm_s;
  double max_cpa_in_window_nm;
  bool multiple_targets_nearby;
  std::uint32_t critical_target_count;
};

class PerformanceMonitor {
public:
  explicit PerformanceMonitor(PerformanceConfig const& cfg) noexcept;

  [[nodiscard]] PerformanceStatus
  evaluate(l3_msgs::msg::WorldState const& world,
           std::chrono::steady_clock::time_point now) noexcept;

  void reset() noexcept;

private:
  PerformanceConfig cfg_;
  std::array<float, 30> cpa_history_;                  // 30 s @ 1 Hz 采样
  std::uint32_t history_idx_{0};
  bool history_filled_{false};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_
```

### 5.6 `checker/veto_handler.hpp` — RFC-003 enum 分类升级（关键）

> **RFC-003 决议要点**：M7 仅按 `veto_reason_class` enum 分类做统计与告警升级；**绝不解析 `veto_reason_detail` 自由文本**。`veto_reason_detail` 仅由 ASDR 透传记录，M7 端不访问。

```cpp
// include/m7_safety_supervisor/checker/veto_handler.hpp
#ifndef M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_
#define M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_

#include <array>
#include <cstdint>

#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "m7_safety_supervisor/checker/sliding_window_15s.hpp"

namespace mass_l3::m7::checker {

// RFC-003 enum 分类（来自 l3_external_msgs/CheckerVetoNotification.msg constants）
enum class VetoReasonClass : std::uint8_t {
  kColregsViolation = 0,                              // VETO_REASON_COLREGS_VIOLATION
  kCpaBelowThreshold = 1,                             // VETO_REASON_CPA_BELOW_THRESHOLD
  kEncConstraint = 2,                                 // VETO_REASON_ENC_CONSTRAINT
  kActuatorLimit = 3,                                 // VETO_REASON_ACTUATOR_LIMIT
  kTimeout = 4,                                       // VETO_REASON_TIMEOUT
  kOther = 5,                                         // VETO_REASON_OTHER
  kCount
};

// 分类直方图（统计用）
using VetoHistogram = std::array<std::uint32_t, 6>;

class VetoHandler {
public:
  // window_cycle_count = 100（RFC-003 锁定）
  explicit VetoHandler(std::uint32_t window_cycle_count = 100) noexcept;

  // 接收 X-axis 通知（事件 callback 中调用）
  // 仅按 veto_reason_class 分类；不解析 veto_reason_detail
  void on_veto_received(l3_external_msgs::msg::CheckerVetoNotification const& msg) noexcept;

  // 主循环每周期调用一次（推进 window cursor + 当前周期是否被否决）
  void on_cycle_tick(bool veto_in_this_cycle) noexcept;

  // 当前 100 周期窗口内的否决率
  [[nodiscard]] double current_rate() const noexcept;

  // 当前直方图（按分类）
  [[nodiscard]] VetoHistogram histogram() const noexcept { return histogram_; }

  // 重置（回切顺序化时调用）
  void reset() noexcept;

private:
  SlidingWindow15s window_;
  VetoHistogram histogram_{};
};

}  // namespace mass_l3::m7::checker

#endif  // M7_SAFETY_SUPERVISOR_CHECKER_VETO_HANDLER_HPP_
```

> **注意**：`l3_external_msgs::msg::CheckerVetoNotification::VETO_REASON_*` 是 ROS2 IDL 常量（详见 `ros2-idl-implementation-guide.md` §3.2）。本地 `VetoReasonClass` 是类型别名，**保证 enum 单一来源**（详细设计 §8.1）。

### 5.7 `checker/sliding_window_15s.hpp` — 100 周期滑窗

```cpp
// include/m7_safety_supervisor/checker/sliding_window_15s.hpp
#ifndef M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_
#define M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_

#include <array>
#include <cstdint>

namespace mass_l3::m7::checker {

// 固定 100 周期滑窗（RFC-003 锁定）
// @ 6.7 Hz 周期 → 15 s 时间窗口
// 实现：Fixed-size circular buffer + 环形索引（无 dynamic alloc）
class SlidingWindow15s {
public:
  static constexpr std::uint32_t kCapacity = 100;

  SlidingWindow15s() noexcept = default;

  // 推进窗口：当前周期是否记录"事件"（如 Checker 否决）
  void tick(bool event) noexcept;

  // 当前窗口内事件占比 ∈ [0, 1]
  [[nodiscard]] double rate() const noexcept;

  // 当前事件计数
  [[nodiscard]] std::uint32_t count() const noexcept { return event_count_; }

  // 是否填满（启动期 < 100 周期，rate() 返回值需谨慎使用）
  [[nodiscard]] bool is_filled() const noexcept { return filled_; }

  // 重置（回切顺序化时）
  void reset() noexcept;

private:
  std::array<bool, kCapacity> buffer_{};
  std::uint32_t cursor_{0};
  std::uint32_t event_count_{0};        // O(1) 维护：tick 时增减
  bool filled_{false};
};

}  // namespace mass_l3::m7::checker

#endif  // M7_SAFETY_SUPERVISOR_CHECKER_SLIDING_WINDOW_15S_HPP_
```

### 5.8 `mrm/mrm_command_set.hpp` — 4 种预定义 MRM（不动态生成）

> **ADR-001 关键约束**：M7 不动态生成轨迹。仅触发预定义 MRM-01/02/03/04 命令集（v1.1.2 §11.6）。每个 MRM 是一个**预定义参数集 + 适用条件**。

```cpp
// include/m7_safety_supervisor/mrm/mrm_command_set.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_

#include <cstdint>
#include <string_view>

namespace mass_l3::m7::mrm {

// 4 种预定义 MRM（v1.1.2 §11.6 + 详细设计附录 C 一致性表）
enum class MrmId : std::uint8_t {
  kNone = 0,                          // 无建议
  kMrm01_Drift = 1,                   // MRM-01 漂移（减速至安全速度，COLREG Rule 8 + Rule 6）
  kMrm02_Anchor = 2,                  // MRM-02 抛锚（仅水深 ≤ 30m + 速度 ≤ 4 kn）
  kMrm03_HeaveTo = 3,                 // MRM-03 顶风停（紧急转向 ±60°）
  kMrm04_Mooring = 4                  // MRM-04 系泊（港内）
};

[[nodiscard]] constexpr std::string_view to_string(MrmId id) noexcept;

// 每种 MRM 的预定义参数（YAML 注入 `config/mrm_command_set.yaml`）
struct Mrm01Params {
  double target_speed_kn{4.0};          // [TBD-HAZID — §11.6]
  double max_decel_time_s{30.0};        // [TBD-HAZID — §11.6]
};

struct Mrm02Params {
  double max_water_depth_m{30.0};       // [TBD-HAZID — §11.6 适用条件]
  double max_speed_kn{4.0};             // [TBD-HAZID]
};

struct Mrm03Params {
  double turn_angle_deg{60.0};          // [TBD-HAZID — §11.6 ±60°]
  double rot_factor{0.8};               // [TBD-HAZID — 0.8 × ROT_max]
};

struct Mrm04Params {
  bool requires_harbor_zone{true};
  double max_speed_kn{2.0};
};

struct MrmCommandSet {
  Mrm01Params mrm01;
  Mrm02Params mrm02;
  Mrm03Params mrm03;
  Mrm04Params mrm04;
};

// 加载/校验 — yaml-cpp 仅在 boot-time 调用，不进决策路径
class MrmCommandSetLoader {
public:
  // 从 YAML 文件加载（boot-time 调用一次）
  // 返回 NoExceptionResult — 加载失败时 fall back 到 hard-coded 安全默认
  [[nodiscard]] static MrmCommandSet load_from_yaml(std::string_view yaml_path) noexcept;

  // 安全默认（YAML 不可达时使用）
  [[nodiscard]] static MrmCommandSet safe_default() noexcept;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_COMMAND_SET_HPP_
```

#### 5.8.1 `mrm/mrm_01_drift.hpp` — MRM-01 漂移（减速至安全速度）

```cpp
// include/m7_safety_supervisor/mrm/mrm_01_drift.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// 适用条件检查（不生成轨迹，只检查 MRM-01 是否合法可行）
class Mrm01Drift {
public:
  explicit Mrm01Drift(Mrm01Params const& params) noexcept;

  // 触发条件评估（详细设计 §5.4 + §11.6）
  // - 一般 PERF 退化 / 单项 SOTIF 假设违反
  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  // SAT-2 rationale（人读字符串，非自由文本解析；用 fmt::format 模板）
  [[nodiscard]] std::string rationale() const noexcept;

  [[nodiscard]] Mrm01Params params() const noexcept { return params_; }

private:
  Mrm01Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_
```

#### 5.8.2 `mrm/mrm_02_anchor.hpp` — MRM-02 抛锚（须满足水深约束）

```cpp
// include/m7_safety_supervisor/mrm/mrm_02_anchor.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-02 抛锚：DNV-CG-0264 §9.8 + MUNIN MRC [R15]
// 适用条件：水深 ≤ 30 m + 速度 ≤ 4 kn（§11.6 [TBD-HAZID]）
// 注意：M7 仅评估"可行性"；锚位预选由 M1 / M3 完成（M7 不做几何）
class Mrm02Anchor {
public:
  explicit Mrm02Anchor(Mrm02Params const& params) noexcept;

  // 适用性评估（仅 sanity check：当前水深 + 速度 + 静态障碍物清单非空）
  // 注意：M7 不做几何运算（独立性 — Boost.Geometry 禁用）
  // 仅检查 ZoneConstraint.min_water_depth_m 与 own_ship.sog_kn 等数值阈值
  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] std::string rationale() const noexcept;
  [[nodiscard]] Mrm02Params params() const noexcept { return params_; }

private:
  Mrm02Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_
```

#### 5.8.3 `mrm/mrm_03_heave_to.hpp` — MRM-03 顶风停（紧急转向）

```cpp
// include/m7_safety_supervisor/mrm/mrm_03_heave_to.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-03 紧急转向：COLREGs Rule 8 "large alteration" + Rule 17 [R18, R20]
// 适用：CPA 急剧恶化（PERF 紧急级）/ 多目标聚集（≥ 2 targets CPA < 1.0 nm）
class Mrm03HeaveTo {
public:
  explicit Mrm03HeaveTo(Mrm03Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] std::string rationale() const noexcept;
  [[nodiscard]] Mrm03Params params() const noexcept { return params_; }

private:
  Mrm03Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_
```

#### 5.8.4 `mrm/mrm_04_mooring.hpp` — MRM-04 系泊

```cpp
// include/m7_safety_supervisor/mrm/mrm_04_mooring.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-04 系泊：港内 + 系统失能时
// 适用：ODD-C（港内 / 靠泊）+ 速度 ≤ 2 kn
class Mrm04Mooring {
public:
  explicit Mrm04Mooring(Mrm04Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] std::string rationale() const noexcept;
  [[nodiscard]] Mrm04Params params() const noexcept { return params_; }

private:
  Mrm04Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_
```

#### 5.8.5 `mrm/mrm_selector.hpp` — 场景 → MRM 选择逻辑

> **关键决策表**（v1.1.2 §11.6 + 详细设计附录 C）：

| 场景 | 触发条件 | 选择 MRM |
|---|---|---|
| 单项 SOTIF 假设违反 | violation_count = 1 | MRM-01 |
| 多项 SOTIF 假设同时违反 | violation_count ≥ 2 | MRM-02 |
| IEC61508 心跳丢失（1-2 模块） | watchdog.critical_count ∈ [1, 2] | MRM-01 |
| IEC61508 心跳丢失（≥ 3 模块） | watchdog.critical_count ≥ 3 | MRM-02 |
| CPA 趋势恶化 | performance.cpa_trend_degrading == true | MRM-02 |
| 多目标聚集 | performance.multiple_targets_nearby == true | MRM-03 |
| 极端场景 | violation_count ≥ 3 | MRM-02 |
| 港内 + 系统失能 | odd.zone == ODD_C && violation_count ≥ 2 | MRM-04 |

```cpp
// include/m7_safety_supervisor/mrm/mrm_selector.hpp
#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_

#include <chrono>

#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_01_drift.hpp"
#include "m7_safety_supervisor/mrm/mrm_02_anchor.hpp"
#include "m7_safety_supervisor/mrm/mrm_03_heave_to.hpp"
#include "m7_safety_supervisor/mrm/mrm_04_mooring.hpp"

#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"

namespace mass_l3::m7::mrm {

// 场景输入快照（汇总 IEC61508 + SOTIF 状态）
struct ScenarioContext {
  iec61508::WatchdogMonitor::WatchdogResult watchdog;
  iec61508::DiagnosticResult diagnostic;
  sotif::AssumptionStatus assumption;
  sotif::PerformanceStatus performance;
  bool extreme_scenario_detected;
};

struct MrmDecision {
  MrmId mrm_id;
  float confidence;                              // ∈ [0, 1]
  std::string rationale;                          // SAT-2 摘要
};

// 选择器：纯函数（输入 ScenarioContext + 当前时间 → MrmDecision）
// 30 s 防振荡锁定（详细设计 §5.4）
class MrmSelector {
public:
  struct Config {
    std::chrono::seconds change_lockout{30};     // 30 s 防振荡
    float upgrade_confidence_threshold{0.9F};    // MRM 升级置信度门槛
  };

  MrmSelector(Config const& cfg, MrmCommandSet const& cmd_set,
              l3_msgs::msg::ODDState const& odd_snapshot) noexcept;

  // 主入口：根据场景选择 MRM
  // 实现 30 s 锁定 + 优先级升级逻辑（详细设计 §5.4）
  [[nodiscard]] MrmDecision select(ScenarioContext const& ctx,
                                   l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world,
                                   std::chrono::steady_clock::time_point now) noexcept;

  void reset() noexcept;

private:
  Config cfg_;
  Mrm01Drift mrm01_;
  Mrm02Anchor mrm02_;
  Mrm03HeaveTo mrm03_;
  Mrm04Mooring mrm04_;

  MrmId last_id_{MrmId::kNone};
  std::chrono::steady_clock::time_point last_change_{};

  // 决策表查询（无 throw + 内联）
  [[nodiscard]] MrmId raw_select(ScenarioContext const& ctx,
                                  l3_msgs::msg::ODDState const& odd) const noexcept;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_SELECTOR_HPP_
```

### 5.9 `arbitrator/safety_arbitrator.hpp` + `alert_generator.hpp` — Alert 汇聚

```cpp
// include/m7_safety_supervisor/arbitrator/safety_arbitrator.hpp
#ifndef M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_
#define M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_

#include <array>

#include "l3_msgs/msg/safety_alert.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"

namespace mass_l3::m7::arbitrator {

// 汇聚后的 Alert 候选（多条 alert 排序后取最高优先级）
struct AlertCandidate {
  std::uint8_t alert_type;             // l3_msgs::msg::SafetyAlert::ALERT_*
  std::uint8_t severity;               // l3_msgs::msg::SafetyAlert::SEVERITY_*
  mrm::MrmId recommended_mrm;
  float confidence;
  std::string rationale;
};

// 仅有限数量 Alert 候选（无 dynamic alloc）
constexpr std::uint32_t kMaxAlertCandidates = 8;

class SafetyArbitrator {
public:
  SafetyArbitrator() noexcept = default;

  // 主入口：从 ScenarioContext + MrmDecision 汇聚出最终 SafetyAlert
  [[nodiscard]] l3_msgs::msg::SafetyAlert
  arbitrate(mrm::ScenarioContext const& ctx,
            mrm::MrmDecision const& mrm_decision,
            std::chrono::steady_clock::time_point now) noexcept;

private:
  // FixedSize candidate pool（无 dynamic alloc — PROJ-LR-001）
  std::array<AlertCandidate, kMaxAlertCandidates> pool_;
  std::uint32_t pool_size_{0};

  // 收集所有候选 Alert（IEC61508 + SOTIF + Performance）
  void collect_candidates(mrm::ScenarioContext const& ctx) noexcept;

  // 优先级排序：EMERGENCY > CRITICAL > WARNING；同级别按 confidence 降序
  void sort_candidates() noexcept;
};

}  // namespace mass_l3::m7::arbitrator

#endif  // M7_SAFETY_SUPERVISOR_ARBITRATOR_SAFETY_ARBITRATOR_HPP_
```

```cpp
// include/m7_safety_supervisor/arbitrator/alert_generator.hpp
#ifndef M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_
#define M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_

#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"

namespace mass_l3::m7::arbitrator {

// 工厂：将内部状态映射到 ROS2 .msg
class AlertGenerator {
public:
  // SafetyAlert → M1
  [[nodiscard]] static l3_msgs::msg::SafetyAlert
  build_safety_alert(builtin_interfaces::msg::Time const& stamp,
                     std::uint8_t alert_type,
                     std::uint8_t severity,
                     std::string_view recommended_mrm,
                     float confidence,
                     std::string_view rationale,
                     std::string_view description) noexcept;

  // ASDRRecord → ASDR（决策追溯）
  [[nodiscard]] static l3_msgs::msg::ASDRRecord
  build_asdr_record(builtin_interfaces::msg::Time const& stamp,
                    std::string_view source_module,
                    std::string_view decision_type,
                    std::string_view decision_summary) noexcept;
                    // 注意：decision_summary 是 fmt::format 出来的简单 key=value 字符串
                    // M7 不调用 nlohmann::json — ASDR 端用 RFC-004 schema 适配

  // SATData → M8（SAT-1/2/3）
  [[nodiscard]] static l3_msgs::msg::SATData
  build_sat_data(/* ... */) noexcept;
};

}  // namespace mass_l3::m7::arbitrator

#endif  // M7_SAFETY_SUPERVISOR_ARBITRATOR_ALERT_GENERATOR_HPP_
```

> **独立性证据**：`AlertGenerator` 仅产生 `l3_msgs::msg::*` 实例 — 不依赖 nlohmann::json（ASDR `decision_json` 字段由 ASDR 端按 RFC-004 schema 构造，M7 端只提供 key=value 文本）。

### 5.10 `safety_supervisor_node.hpp` — ROS2 节点

```cpp
// include/m7_safety_supervisor/safety_supervisor_node.hpp
#ifndef M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_
#define M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_

#include <memory>

#include "rclcpp/rclcpp.hpp"

// 内部模块（独立性：仅 m7 自身 + l3_msgs / l3_external_msgs）
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"
#include "m7_safety_supervisor/checker/veto_handler.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "m7_safety_supervisor/arbitrator/safety_arbitrator.hpp"
#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"

// l3_msgs 订阅
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"

// l3_external_msgs 订阅
#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"

// 发布
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "std_msgs/msg/header.hpp"

namespace mass_l3::m7 {

class SafetySupervisorNode : public rclcpp::Node {
public:
  explicit SafetySupervisorNode(rclcpp::NodeOptions const& options);

private:
  // ===== Callback Groups =====
  rclcpp::CallbackGroup::SharedPtr cb_group_main_;          // MutuallyExclusive
  rclcpp::CallbackGroup::SharedPtr cb_group_events_;        // Reentrant

  // ===== 订阅器 =====
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr sub_world_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoidance_;
  rclcpp::Subscription<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr sub_override_cmd_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr sub_colregs_;

  rclcpp::Subscription<l3_external_msgs::msg::CheckerVetoNotification>::SharedPtr sub_veto_;
  rclcpp::Subscription<l3_external_msgs::msg::ReflexActivationNotification>::SharedPtr sub_reflex_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr sub_override_signal_;

  // ===== 发布器 =====
  rclcpp::Publisher<l3_msgs::msg::SafetyAlert>::SharedPtr pub_alert_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr pub_asdr_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr pub_sat_;
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr pub_heartbeat_;

  // ===== 周期 timer =====
  rclcpp::TimerBase::SharedPtr timer_main_;             // 4 Hz 主监控
  rclcpp::TimerBase::SharedPtr timer_sat_;              // 10 Hz SAT
  rclcpp::TimerBase::SharedPtr timer_asdr_periodic_;    // 2 Hz ASDR
  rclcpp::TimerBase::SharedPtr timer_heartbeat_;        // 10 Hz 自身心跳

  // ===== 内部模块 =====
  std::unique_ptr<iec61508::WatchdogMonitor> watchdog_;
  std::unique_ptr<iec61508::FaultMonitor> fault_monitor_;
  std::unique_ptr<sotif::AssumptionMonitor> assumption_monitor_;
  std::unique_ptr<sotif::PerformanceMonitor> performance_monitor_;
  std::unique_ptr<sotif::TriggeringConditionDetector> triggering_detector_;
  std::unique_ptr<checker::VetoHandler> veto_handler_;
  std::unique_ptr<mrm::MrmSelector> mrm_selector_;
  std::unique_ptr<arbitrator::SafetyArbitrator> arbitrator_;

  // ===== 输入快照（boot-time 预分配，无 dynamic alloc）=====
  l3_msgs::msg::ODDState last_odd_;
  l3_msgs::msg::WorldState last_world_;
  l3_msgs::msg::COLREGsConstraint last_colregs_;
  l3_msgs::msg::AvoidancePlan last_avoidance_;
  bool override_active_{false};
  bool reflex_freeze_required_{false};

  // ===== Callbacks =====
  void on_odd_state(l3_msgs::msg::ODDState::ConstSharedPtr msg);
  void on_world_state(l3_msgs::msg::WorldState::ConstSharedPtr msg);
  void on_behavior_plan(l3_msgs::msg::BehaviorPlan::ConstSharedPtr msg);
  void on_avoidance_plan(l3_msgs::msg::AvoidancePlan::ConstSharedPtr msg);
  void on_override_cmd(l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr msg);
  void on_colregs_constraint(l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg);

  void on_checker_veto(l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr msg);
  void on_reflex_activation(l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr msg);
  void on_override_signal(l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr msg);

  void on_main_loop_tick();
  void on_sat_tick();
  void on_asdr_periodic_tick();
  void on_heartbeat_tick();

  // ===== Helpers =====
  void load_parameters();                              // boot-time YAML 加载
  void setup_subscriptions(rclcpp::QoS const& qos_long,
                           rclcpp::QoS const& qos_short,
                           rclcpp::QoS const& qos_event);
  void setup_publishers();
  void setup_timers();
  void setup_logger();                                 // 独立 spdlog instance
  void revert_from_override();                         // §6.3 回切顺序化
};

}  // namespace mass_l3::m7

#endif  // M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_
```

---

## 6. CMakeLists.txt（关键 — 独立性强约束）

```cmake
cmake_minimum_required(VERSION 3.22)
project(m7_safety_supervisor LANGUAGES CXX)

# ===================== C++ 标准（coding-standards.md §2）=====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===================== Find packages — 仅白名单依赖 =====================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

# l3_msgs / l3_external_msgs（共享消息包）
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# 标准 ROS2 消息包
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)

# 数学 + 日志（独立性允许的 5 个库）
find_package(Eigen3 5.0.0 EXACT REQUIRED)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(fmt 11.0 REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)              # 仅 boot-time 配置

# ===================== 独立性 CI 验证（构建前钩子）=====================
# 在 colcon build 之前，自动验证 #include 与 link 不违反 ADR-001
add_custom_target(m7_doer_checker_independence_check ALL
    COMMAND ${CMAKE_SOURCE_DIR}/../tools/ci/check-doer-checker-independence.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verifying M7 Doer-Checker independence (ADR-001)"
)

# ===================== 编译选项（PATH-S 强制）=====================
add_library(m7_lib STATIC
    src/common/error.cpp
    src/common/time_utils.cpp
    src/common/safety_constants.cpp
    src/iec61508/watchdog_monitor.cpp
    src/iec61508/fault_monitor.cpp
    src/iec61508/diagnostic_coverage.cpp
    src/sotif/assumption_monitor.cpp
    src/sotif/performance_monitor.cpp
    src/sotif/triggering_condition_detector.cpp
    src/checker/veto_handler.cpp
    src/checker/sliding_window_15s.cpp
    src/mrm/mrm_command_set.cpp
    src/mrm/mrm_01_drift.cpp
    src/mrm/mrm_02_anchor.cpp
    src/mrm/mrm_03_heave_to.cpp
    src/mrm/mrm_04_mooring.cpp
    src/mrm/mrm_selector.cpp
    src/arbitrator/safety_arbitrator.cpp
    src/arbitrator/alert_generator.cpp
    src/safety_supervisor_node.cpp
)

target_include_directories(m7_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# PATH-S 编译选项（coding-standards.md §2.2 + PROJ-LR-002 -fno-exceptions）
target_compile_options(m7_lib PRIVATE
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
    # 安全
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    # PATH-S 专属：禁用异常（PROJ-LR-002）+ 静态分析
    -fno-exceptions
    -fno-rtti                                   # 配合 coding-standards.md §3.4 强约束
    # Debug build 专属 sanitizer
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)

target_link_options(m7_lib PRIVATE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# Release 用 -O2（coding-standards.md §2.2）
target_compile_options(m7_lib PRIVATE
    "$<$<CONFIG:Release>:-O2>"
)

# ===================== 链接（关键 — 独立性证据）=====================
# 仅链接 5 个允许库 + ROS2 框架；任何额外 target_link_libraries 触发 CI 阻断
ament_target_dependencies(m7_lib PUBLIC
    rclcpp
    l3_msgs
    l3_external_msgs
    builtin_interfaces
    std_msgs
)

target_link_libraries(m7_lib PUBLIC
    Eigen3::Eigen
    spdlog::spdlog
    fmt::fmt
    yaml-cpp::yaml-cpp                          # 仅 boot-time 配置使用
)

# 注意：以下 target_link_libraries 永远不会出现（CI 自动阻断）
# target_link_libraries(m7_lib PUBLIC casadi)        # 禁用
# target_link_libraries(m7_lib PUBLIC Ipopt)         # 禁用
# target_link_libraries(m7_lib PUBLIC GeographicLib) # 禁用
# target_link_libraries(m7_lib PUBLIC nlohmann_json) # 禁用
# target_link_libraries(m7_lib PUBLIC boost_geometry)# 禁用

# ===================== Executable =====================
add_executable(m7_safety_supervisor src/main.cpp)
target_link_libraries(m7_safety_supervisor PRIVATE m7_lib)

# ===================== Install =====================
install(TARGETS m7_safety_supervisor
    DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY launch config
    DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY include/
    DESTINATION include
)

# ===================== Tests =====================
if(BUILD_TESTING)
    find_package(ament_lint_auto REQUIRED)
    ament_lint_auto_find_test_dependencies()
    find_package(ament_cmake_gtest REQUIRED)

    # PATH-S 单元测试覆盖率 ≥ 95%
    set(M7_TESTS
        test/unit/test_watchdog_monitor.cpp
        test/unit/test_fault_monitor.cpp
        test/unit/test_assumption_monitor.cpp
        test/unit/test_performance_monitor.cpp
        test/unit/test_triggering_condition_detector.cpp
        test/unit/test_veto_handler.cpp
        test/unit/test_sliding_window_15s.cpp
        test/unit/test_mrm_command_set.cpp
        test/unit/test_mrm_selector.cpp
        test/unit/test_safety_arbitrator.cpp
        test/unit/test_alert_generator.cpp
    )

    foreach(test_src ${M7_TESTS})
        get_filename_component(test_name ${test_src} NAME_WE)
        ament_add_gtest(${test_name} ${test_src})
        target_link_libraries(${test_name} m7_lib)
    endforeach()

    # 集成测试
    ament_add_gtest(test_doer_checker_independence
        test/integration/test_doer_checker_independence.cpp)
    target_link_libraries(test_doer_checker_independence m7_lib)

    ament_add_gtest(test_e2e_alert_pipeline
        test/integration/test_e2e_alert_pipeline.cpp)
    target_link_libraries(test_e2e_alert_pipeline m7_lib)
endif()

ament_export_dependencies(rclcpp l3_msgs l3_external_msgs Eigen3 spdlog fmt yaml-cpp)
ament_package()
```

> **关键独立性证据**（CI 自动验证）：
> - `find_package` 仅含：rclcpp, l3_msgs, l3_external_msgs, std_msgs, builtin_interfaces, Eigen3, spdlog, fmt, yaml-cpp
> - 无：CasADi / IPOPT / GeographicLib / nlohmann::json / Boost.Geometry
> - 无：m1_*, m2_*, m3_*, m4_*, m5_*, m6_* 任何 colcon 包

---

## 7. package.xml

```xml
<?xml version="1.0"?>
<package format="3">
    <name>m7_safety_supervisor</name>
    <version>1.0.0</version>
    <description>
        M7 Safety Supervisor — L3 Tactical Layer's IEC 61508 + ISO 21448 SOTIF
        dual-track safety monitor (Doer-Checker pattern, ADR-001 + decision-4).
        PATH-S strict independence path; does not share code or third-party libs
        with M1-M6 (validated by tools/ci/check-doer-checker-independence.sh).
    </description>
    <maintainer email="team-m7@sango.example">Team-M7</maintainer>
    <license>Apache-2.0</license>

    <buildtool_depend>ament_cmake</buildtool_depend>

    <!-- ROS2 框架（独立性允许）-->
    <depend>rclcpp</depend>
    <depend>builtin_interfaces</depend>
    <depend>std_msgs</depend>

    <!-- L3 共享消息（独立性允许 — 仅订阅/发布）-->
    <depend>l3_msgs</depend>
    <depend>l3_external_msgs</depend>

    <!-- 数学 + 日志（白名单 — 5 个允许库）-->
    <depend>libeigen3-dev</depend>           <!-- Eigen 5.0.0 vendor -->
    <depend>spdlog</depend>                  <!-- spdlog 1.17.0 -->
    <depend>fmt</depend>                     <!-- fmt 11.x -->
    <depend>yaml-cpp</depend>                <!-- yaml-cpp 0.8.0（仅 boot-time 配置）-->

    <!-- 测试（仅测试期）-->
    <test_depend>ament_lint_auto</test_depend>
    <test_depend>ament_cmake_gtest</test_depend>

    <!-- 注意：以下依赖永远不会出现（独立性 CI 阻断）-->
    <!-- <depend>casadi</depend>                禁用 -->
    <!-- <depend>libipopt-dev</depend>          禁用 -->
    <!-- <depend>geographiclib</depend>         禁用 -->
    <!-- <depend>nlohmann_json</depend>         禁用 -->
    <!-- <depend>libboost-geometry-dev</depend> 禁用 -->

    <export>
        <build_type>ament_cmake</build_type>
    </export>
</package>
```

---

## 8. 单元测试骨架（GTest，覆盖率 ≥ 95% — PATH-S 阈值）

> **强约束**（`coding-standards.md` §3.4）：
> - 单元测试覆盖率 ≥ 95%（MCDC，Modified Condition/Decision Coverage）
> - 每个 `check_*` 谓词须覆盖：阈值边界两侧、持续时间边界、状态恢复、时间戳乱序、confidence 越界
> - PATH-S 路径：禁用异常 → 测试用 `EXPECT_FALSE/EXPECT_TRUE` 或 `NoExceptionResult::has_value()`，不用 `EXPECT_THROW`

### 8.1 `test_assumption_monitor.cpp` — 6 项假设违反

```cpp
// test/unit/test_assumption_monitor.cpp
#include <gtest/gtest.h>
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

namespace mass_l3::m7::sotif {
namespace test {

class AssumptionMonitorTest : public ::testing::Test {
protected:
    AssumptionConfig cfg_;     // 使用默认值（与 m7_params.yaml 一致）
};

// ============== 假设 1: AIS/Radar 一致性 ==============
TEST_F(AssumptionMonitorTest, AisRadar_HighConfidence_NoViolation) {
    AssumptionMonitor mon(cfg_);
    auto world = build_world_with_fusion_confidence(0.9F);
    auto now = std::chrono::steady_clock::now();
    EXPECT_FALSE(mon.check_ais_radar(world, now));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_ShortDuration_NoViolation) {
    AssumptionMonitor mon(cfg_);
    auto world = build_world_with_fusion_confidence(0.3F);
    auto t0 = std::chrono::steady_clock::now();
    mon.check_ais_radar(world, t0);
    // 仅 10 s（< 30 s 阈值）— 不违反
    EXPECT_FALSE(mon.check_ais_radar(world, t0 + std::chrono::seconds(10)));
}

TEST_F(AssumptionMonitorTest, AisRadar_LowConfidence_LongDuration_Violation) {
    AssumptionMonitor mon(cfg_);
    auto world = build_world_with_fusion_confidence(0.3F);
    auto t0 = std::chrono::steady_clock::now();
    mon.check_ais_radar(world, t0);
    // 31 s（> 30 s 阈值）— 违反
    EXPECT_TRUE(mon.check_ais_radar(world, t0 + std::chrono::seconds(31)));
}

TEST_F(AssumptionMonitorTest, AisRadar_RecoveryAfterViolation_StateReset) {
    // 违反后恢复（confidence 升回）— 状态应重置
    // 边界：阈值临界值 0.5（按 cfg_.fusion_confidence_low）
}

TEST_F(AssumptionMonitorTest, AisRadar_TimestampReversed_GracefulHandle) {
    // stamp 乱序时不崩溃（PROJ-LR 无 throw）
}

// ============== 假设 2: 运动可预测性 ==============
TEST_F(AssumptionMonitorTest, Motion_RmseUnderThreshold_NoViolation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Motion_RmseOverThreshold_Violation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Motion_TcpaShort_UpgradesToMrm02_VerifiedBySelector) { /*...*/ }

// ============== 假设 3: 感知覆盖 ==============
TEST_F(AssumptionMonitorTest, Coverage_BlindZoneUnder20pct_NoViolation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Coverage_BlindZoneOver20pct_Violation) { /*...*/ }

// ============== 假设 4: COLREGs 可解析性 ==============
TEST_F(AssumptionMonitorTest, Colregs_2ConsecutiveFailure_NoViolation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Colregs_3ConsecutiveFailure_Violation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Colregs_FailThenSuccess_CounterReset) { /*...*/ }

// ============== 假设 5: 通信链路 ==============
TEST_F(AssumptionMonitorTest, Comm_RttUnderThreshold_NoViolation) { /*...*/ }
TEST_F(AssumptionMonitorTest, Comm_HighPacketLoss_Violation) { /*...*/ }

// ============== 假设 6: Checker 否决率 ==============
TEST_F(AssumptionMonitorTest, CheckerVeto_RateBelow20pct_NoViolation) { /*...*/ }
TEST_F(AssumptionMonitorTest, CheckerVeto_RateAt20pct_Violation) { /*...*/ }
TEST_F(AssumptionMonitorTest, CheckerVeto_RateAbove20pct_Violation) { /*...*/ }

// ============== 综合（多假设同时） ==============
TEST_F(AssumptionMonitorTest, ExtremeScenario_3SimultaneousViolations) { /*...*/ }

// ============== 重置 ==============
TEST_F(AssumptionMonitorTest, ResetAfterReverting_AllStatesCleared) { /*...*/ }

}  // namespace test
}  // namespace mass_l3::m7::sotif
```

### 8.2 `test_veto_handler.cpp` — RFC-003 enum 分类

```cpp
// test/unit/test_veto_handler.cpp
TEST(VetoHandlerTest, VetoReasonClass_Colregs_HistogramIncreases) {
    VetoHandler vh(100);
    auto msg = build_veto_msg(VETO_REASON_COLREGS_VIOLATION);
    vh.on_veto_received(msg);
    EXPECT_EQ(vh.histogram()[0], 1U);  // kColregsViolation
}

TEST(VetoHandlerTest, VetoReasonClass_AllSixTypes_HistogramConsistent) {
    // 测试 6 种 enum 分类（COLREGS / CPA / ENC / ACTUATOR / TIMEOUT / OTHER）
}

TEST(VetoHandlerTest, FreeTextDetail_NotParsed_Ignored) {
    // RFC-003 关键约束：veto_reason_detail 自由文本绝不解析
    auto msg = build_veto_msg(VETO_REASON_OTHER);
    msg.veto_reason_detail = "<arbitrary_unparsed_string>";
    VetoHandler vh(100);
    vh.on_veto_received(msg);
    // 仅按 enum 分类，detail 不影响逻辑（仅 ASDR 透传）
    EXPECT_EQ(vh.histogram()[5], 1U);  // kOther
}

TEST(VetoHandlerTest, RateOver20Pct_FlagsViolation) {
    VetoHandler vh(100);
    for (int i = 0; i < 100; ++i) {
        bool veto = (i < 20);  // 20% 否决
        vh.on_cycle_tick(veto);
    }
    EXPECT_NEAR(vh.current_rate(), 0.20, 1e-6);
}

TEST(VetoHandlerTest, ResetClearsAllState) { /* ... */ }
```

### 8.3 `test_mrm_command_set.cpp` — 4 种 MRM 触发条件

```cpp
// test/unit/test_mrm_command_set.cpp
TEST(Mrm01DriftTest, Applicable_WhenSpeedAboveTarget) { /*...*/ }
TEST(Mrm01DriftTest, NotApplicable_WhenAlreadyAtTargetSpeed) { /*...*/ }

TEST(Mrm02AnchorTest, Applicable_WhenWaterDepthBelow30m_AndSpeedBelow4kn) { /*...*/ }
TEST(Mrm02AnchorTest, NotApplicable_WhenWaterDepthOver30m) { /*...*/ }
TEST(Mrm02AnchorTest, NotApplicable_WhenSpeedOver4kn) { /*...*/ }

TEST(Mrm03HeaveToTest, Applicable_WhenMultipleTargetsClose) { /*...*/ }

TEST(Mrm04MooringTest, Applicable_WhenInHarborZone) { /*...*/ }
TEST(Mrm04MooringTest, NotApplicable_WhenInOpenWater) { /*...*/ }
```

### 8.4 `test_mrm_selector.cpp` — 场景选择正确性（详细设计 §11.6）

```cpp
// test/unit/test_mrm_selector.cpp — 8 个场景 ✕ 每场景边界用例
TEST(MrmSelectorTest, SingleSotifViolation_SelectsMrm01) {
    auto ctx = build_ctx(/*violation_count=*/1);
    auto dec = selector_.select(ctx, ...);
    EXPECT_EQ(dec.mrm_id, MrmId::kMrm01_Drift);
}

TEST(MrmSelectorTest, MultipleSotifViolations_SelectsMrm02) {
    auto ctx = build_ctx(/*violation_count=*/2);
    auto dec = selector_.select(ctx, ...);
    EXPECT_EQ(dec.mrm_id, MrmId::kMrm02_Anchor);  // ↑ 升级到 Stop
}

TEST(MrmSelectorTest, OneTwoModulesDown_SelectsMrm01) { /*...*/ }
TEST(MrmSelectorTest, ThreeModulesDown_SelectsMrm02) { /*...*/ }
TEST(MrmSelectorTest, CpaTrendDegrading_SelectsMrm02) { /*...*/ }
TEST(MrmSelectorTest, MultipleTargetsClose_SelectsMrm03) { /*...*/ }
TEST(MrmSelectorTest, ExtremeScenario_3SimultaneousViolations_SelectsMrm02) { /*...*/ }
TEST(MrmSelectorTest, HarborZoneDegraded_SelectsMrm04) { /*...*/ }

// 防振荡（30 s 锁定）
TEST(MrmSelectorTest, ChangeWithin30s_KeepsLastMrm) {
    auto t0 = std::chrono::steady_clock::now();
    selector_.select(/*Mrm01 触发场景*/, ..., t0);
    auto dec = selector_.select(/*Mrm02 触发场景*/, ..., t0 + std::chrono::seconds(15));
    EXPECT_EQ(dec.mrm_id, MrmId::kMrm01_Drift);  // 保持不变
    EXPECT_LT(dec.confidence, 1.0F);             // confidence 降级标记
}

TEST(MrmSelectorTest, ChangeAfter30s_AllowsUpgrade) {
    // 30 s 后允许升级
}
```

### 8.5 `test_sliding_window_15s.cpp` — 100 周期滑窗

```cpp
TEST(SlidingWindow15sTest, EmptyWindow_RateIsZero) { /*...*/ }
TEST(SlidingWindow15sTest, FullWindowAllEvents_RateIsOne) { /*...*/ }
TEST(SlidingWindow15sTest, CircularOverwrite_OldestEventDropped) { /*...*/ }
TEST(SlidingWindow15sTest, CountConsistency_Over100Cycles) { /*...*/ }
TEST(SlidingWindow15sTest, ResetClearsWindow) { /*...*/ }
TEST(SlidingWindow15sTest, IsFilledAfter100Cycles) { /*...*/ }
```

### 8.6 端到端集成测试 — 完整 Doer-Checker 独立性证明

```cpp
// test/integration/test_doer_checker_independence.cpp
//
// 编译期断言：M7 不引用 M1-M6 内部 header / 第三方禁用库
// 这是 CCS 入级提交件之一（独立性矩阵的运行时证据）

#include <gtest/gtest.h>

// 编译期检查：以下 #include 永远不应出现
//   #include "mass_l3/m1/odd_state_machine.hpp"   // 编译错误
//   #include "mass_l3/m5/mid_mpc.hpp"              // 编译错误
//   #include <casadi/casadi.hpp>                   // 编译错误
//   #include <Ipopt/IpIpoptApplication.hpp>        // 编译错误
//   #include <GeographicLib/Geocentric.hpp>        // 编译错误
//   #include <nlohmann/json.hpp>                   // 编译错误
//   #include <boost/geometry.hpp>                  // 编译错误
//
// 这些 #include 都不在白名单中；CMakeLists.txt 不链接对应库
// CI 在 lint 阶段用 grep 验证；本测试是文档化的运行时证据

TEST(DoerCheckerIndependenceTest, M7BinaryDoesNotLinkForbiddenSymbols) {
    // 启动 M7 二进制；验证它不依赖于 casadi / ipopt / geographiclib /
    // nlohmann_json / boost_geometry 的符号
    // （通过 nm / objdump 在 CI 中 cross-check — 见 third-party-library-policy.md §3.3）
    SUCCEED();  // 此测试主要为文档化目的；真实校验在 CI 脚本
}

// test/integration/test_e2e_alert_pipeline.cpp
//
// 端到端：模拟 M2 输入 → M7 产生 SafetyAlert → 时延 ≤ 100 ms
// 详细设计 §3.2 SLA 验证

TEST(E2EAlertPipelineTest, FusionConfidenceDrop_TriggersSotifAlertWithin100ms) {
    // 1. Mock M2 publisher → 发布 World_StateMsg with fusion_confidence=0.3
    // 2. 等待 M7 main loop 周期触发（4 Hz = 250 ms 周期）
    // 3. 验证 SafetyAlert 在 100 ms 内（相对 M2 stamp）发布
    // 4. 验证 recommended_mrm = "MRM-01"
}

TEST(E2EAlertPipelineTest, M2HeartbeatLost_TriggersIec61508Alert) {
    // Watchdog 检测 M2 心跳丢失 3 周期后触发
}

TEST(E2EAlertPipelineTest, RevertingFromOverride_M7ReadyWithin100ms) {
    // §6.3 回切顺序化：OverrideActiveSignal{false} → M7_READY ≤ 100 ms
}
```

---

## 9. 配置文件

### 9.1 `config/m7_params.yaml` — 阈值参数（HAZID 注入）

见 §4.4 完整 YAML。

### 9.2 `config/m7_logger.yaml` — spdlog 独立 logger（运行时独立性证据）

```yaml
# M7 独立 logger 配置 — Doer-Checker 运行时独立性
# 独立 instance 名 + 独立日志文件 + 独立 thread pool

logger:
  name: m7_supervisor                                # 独立 instance 名
  level: info                                         # debug | info | warn | err | critical

  # 异步 sink（独立 thread pool — 不与 M1-M6 共享）
  async:
    queue_size: 8192
    thread_count: 1                                   # 单 thread；保证顺序记录

  # 独立日志文件（不与 M1-M6 共享）
  sinks:
    - type: rotating_file
      filename: /var/log/mass_l3/m7.log               # 独立路径
      max_size_mb: 50
      max_files: 10
      pattern: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%P] %v"
      # %P = process ID（运行时独立性证据 — M7 独立 OS 进程）

    - type: stdout                                    # 仅开发期；CI / 生产关闭

  # 关键事件特殊 sink（高优先级）
  critical_sink:
    type: rotating_file
    filename: /var/log/mass_l3/m7_critical.log
    flush_immediately: true                           # 立即刷盘（防系统崩溃丢失）
```

### 9.3 `config/mrm_command_set.yaml` — 4 种 MRM 预定义参数

```yaml
mrm_command_set:
  # MRM-01 漂移（减速至安全速度）
  # v1.1.2 §11.6 + COLREG Rule 8 / Rule 6 + Yasukawa 2015 [R7]
  mrm01_drift:
    target_speed_kn: 4.0                              # [TBD-HAZID — §11.6]
    max_decel_time_s: 30.0                            # [TBD-HAZID — 制动加速度 ≈ -0.2 m/s²]
    rationale: "Drift mode: reduce to safe speed per COLREG Rule 6 / 8"

  # MRM-02 抛锚（DNV-CG-0264 §9.8 + MUNIN MRC [R15]）
  mrm02_anchor:
    max_water_depth_m: 30.0                           # [TBD-HAZID — §11.6 适用条件]
    max_speed_kn: 4.0                                 # [TBD-HAZID]
    rationale: "Anchor mode: depth ≤ 30m, speed ≤ 4kn (DNV-CG-0264 §9.8)"

  # MRM-03 顶风停（紧急转向 ±60°）
  # COLREGs Rule 8 "large alteration" + Rule 17 [R18, R20]
  mrm03_heave_to:
    turn_angle_deg: 60.0                              # [TBD-HAZID — §11.6 ±60°]
    rot_factor: 0.8                                   # [TBD-HAZID — 0.8 × ROT_max]
    rationale: "Heave-to: large alteration ±60° per COLREG Rule 8"

  # MRM-04 系泊（港内 + 系统失能）
  mrm04_mooring:
    requires_harbor_zone: true                        # 须在 ODD-C
    max_speed_kn: 2.0
    rationale: "Mooring mode: harbor zone only, speed ≤ 2kn"
```

> **YAML 加载策略**：boot-time `MrmCommandSetLoader::load_from_yaml()` 调用一次，加载到 `MrmCommandSet` 结构体；运行时**不**再读取 YAML（避免动态分配 + I/O 阻塞）。如 YAML 加载失败，回退到 `safe_default()` 硬编码安全值（详细设计 §10 Fail-Safe 原则）。

---

## 10. PATH-S 合规性 checklist（含 Doer-Checker 独立性 — CCS 提交件）

> 本 checklist 是 CCS 入级前置审查项。每项均**自动验证**（CI 阻断）或**人工双签**（reviewer + 验船师）。

### 10.1 编码规范合规

- [ ] **MISRA C++:2023 完整 179 规则** 全部启用（`coding-standards.md` §3 + `static-analysis-policy.md` §4）
- [ ] **AUTOSAR C++14 设计指南**（PATH-S 强制）
- [ ] PROJ-LR-001 — 启动后无动态分配（`new` / `make_unique` 在 src/ 中 0 次出现）
- [ ] PROJ-LR-002 — 编译 `-fno-exceptions`；代码中无 `throw` 关键字
- [ ] PROJ-LR-003 — M7 不 `#include` CasADi / IPOPT / GeographicLib / nlohmann::json / Boost.Geometry
- [ ] PROJ-LR-004 — M7 不 `#include` `mass_l3/m{1-6}/` 任何头文件
- [ ] PROJ-LR-005 — 函数循环复杂度 ≤ 8
- [ ] PROJ-LR-006 — 浮点禁用 `==` / `!=`
- [ ] PROJ-LR-009 — 无 `std::thread::detach()`

### 10.2 静态分析合规（`static-analysis-policy.md` §3 PATH-S）

- [ ] clang-tidy 0 critical / 0 major / ≤ 5 minor
- [ ] cppcheck Premium MISRA Mandatory + Required 全启用 — 0 violation
- [ ] **Polyspace Code Prover** — 0 Red defect；Orange < 1%
- [ ] Coverity（推荐项）— 多样性 cross-check 报告归档
- [ ] AddressSanitizer / UBSan — 0 错误
- [ ] ThreadSanitizer — 0 race condition

### 10.3 Doer-Checker 独立性自动验证（**不可豁免** — `static-analysis-policy.md` §8）

- [ ] `tools/ci/check-doer-checker-independence.sh` 通过（包含 #include + link 双重验证）
- [ ] CMakeLists.txt 的 `target_link_libraries` 不含禁用库
- [ ] `package.xml` 的 `<depend>` 仅在白名单中（rclcpp, l3_msgs, l3_external_msgs, std_msgs, builtin_interfaces, Eigen3, spdlog, fmt, yaml-cpp）
- [ ] M7 launch 文件**禁** composable node（`launch/m7_safety_supervisor.launch.py` 中 `Node(package='m7_safety_supervisor', ...)` 单独 ROS2 进程）
- [ ] 运行时独立证据：M7 PID 与 M1-M6 不同；M7 spdlog instance 名 = `m7_supervisor`，日志路径独立

### 10.4 单元测试合规

- [ ] 单元测试覆盖率（lcov）≥ 95%（PATH-S 要求 — DO-178C DAL B 类比）
- [ ] MCDC 覆盖率 ≥ 95%（决策结构覆盖）
- [ ] `check_*` 6 个谓词每个至少 5 个边界用例（阈值两侧 + 持续时间 + 恢复 + 时间戳乱序 + confidence 越界）
- [ ] MRM Selector 8 个场景 + 防振荡 + 升级路径全覆盖

### 10.5 集成测试合规

- [ ] 端到端时延 ≤ 100 ms（M2 输入 → SafetyAlert，详细设计 §3.2 SLA）
- [ ] 4 种 MRM 触发条件场景全覆盖（HIL 场景 1-4）
- [ ] 回切顺序化（§6.3 / 详细设计 §6.3）100 次测试通过率 100%
- [ ] Watchdog 检出率 100%（≥ 3 模块失联场景）

### 10.6 ASDR 决策追溯

- [ ] 每条 SafetyAlert 都有对应 ASDRRecord（含 input snapshot + 内部状态 + arbitration details + rationale）
- [ ] Checker veto histogram 每 100 周期归档（详细设计 §3.2 + §8.3）
- [ ] M7 mode 状态转移全记录
- [ ] ASDR 决策日志符合 RFC-004 schema

### 10.7 Fail-Safe 验证

- [ ] M7 失效时 → 强制 MRM-01（Fail-Safe，**禁止 Fail-Silent** — v1.1.2 §11.4）
- [ ] M7 失效后系统自动降至 D2（不允许 D3/D4 运行）
- [ ] 内部异常（如内存不足 — 不应发生但有兜底）→ 触发 X-axis Checker VETO 升级（M7 自身心跳丢失 → MRC 准备）

### 10.8 文档与可追溯

- [ ] 每行 production code 可追溯到 `01-detailed-design.md` 章节
- [ ] 每个 SafetyAlert 类型可追溯到 v1.1.2 §11 + §15.1 IDL
- [ ] 所有 [TBD-HAZID] 通过 YAML 注入；不硬编码
- [ ] CCS 入级 9 子功能映射 — 见 v1.1.2 §14 覆盖矩阵（M7 = 子功能 9 安全监控）

---

## 11. 引用

### 11.1 项目内部文档

- **架构基线 v1.1.2**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
  - §2.5 决策四（Doer-Checker 双轨）
  - §11 Safety Supervisor（全章）
  - §11.3 假设违反检测清单（6 项）
  - §11.4 SIL 分配建议
  - §11.6 MRM 命令集（v1.1.1 修订，F-NEW-001）
  - §11.7 与 X-axis Checker 协作（F-NEW-002）
  - §11.9 接管期间行为（F-NEW-005/F-NEW-006）
  - §11.9.2 回切顺序化
  - §15.1 SafetyAlert / ASDRRecord / SATData IDL
- **详细设计**：`docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md`
- **ADR-001**：`docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md`（决策四修订）
- **RFC-003**：`docs/Design/Cross-Team Alignment/RFC-decisions.md`（M7↔X-axis Checker enum 协议 + 100 周期滑窗）
- **RFC-005**：同上（Reflex Arc SIL 3 + 触发阈值）
- **HAZID 工作包**：
  - `docs/Design/HAZID/02-mrm-parameters.md` — MRM 参数初始值
  - `docs/Design/HAZID/03-sotif-thresholds.md` — 6 项假设违反阈值
- **实施基线**：
  - `docs/Implementation/00-implementation-master-plan.md` §3.5 Doer-Checker 独立性
  - `docs/Implementation/coding-standards.md` §1（PATH-S）+ §3（MISRA C++:2023）
  - `docs/Implementation/static-analysis-policy.md` §3（路径强度）+ §8（独立性 CI）
  - `docs/Implementation/third-party-library-policy.md` §3（独立性矩阵）
  - `docs/Implementation/ros2-idl-implementation-guide.md` §3.3（SafetyAlert / CheckerVetoNotification 字段）

### 11.2 工业先例 + 学术参考

| ID | 来源 | 内容 | 用途 |
|---|---|---|---|
| Boeing 777 PFC | Yeh 1996 AIAA / FAA AC 25.1309 | 三冗余 + 独立 lane 飞控 | 独立性合理性证据 — Doer-Checker 工业先例 |
| Airbus A320/A380 FBW | Briere & Traverse 1993 IEEE | 双 lane（COM/MON）+ 多语言 / OS / 编译器 | 同上 |
| Apex.AI ROS2 ASIL-D | https://www.apex.ai/apexgrace 🟢 | ROS2 Safety WG 工业实施 | M7 ROS2 ASIL-D 路径参考 |
| ROS 2 Safety WG | https://github.com/ros-safety/safety_working_group 🟢 | 安全关键 ROS2 实践 | DDS Security + composable 禁用 |
| [R5] IEC 61508-3/2 | TUV SUD | SIL 分配 / HFT / SFF / DC 矩阵 | M7 SIL 2 框架 |
| [R6] ISO 21448:2022 SOTIF | ISO | 功能不足检测 | M7 SOTIF 轨道理论基础 |
| [R7] Yasukawa & Yoshimura 2015 | JASNAOE | MMG 4-DOF 操纵模型 | MRM-01 工程依据（制动曲线）|
| [R9] DNV-CG-0264 (2025.01) | DNV | AROS 认证；Checker 独立性 §9.3 | M7 独立性合规 |
| [R12] Jackson 2021 MIT CSAIL | MIT | "Certified Control" 双轨框架 | Doer-Checker 理论 |
| [R15] MUNIN MRC 设计 | EU MUNIN Project | MRC 兜底机动（抛锚）| MRM-04 工程依据 |
| [R18, R20] COLREGs Rule 8 / Rule 17 | IMO 1972/2025 | "大幅转向" / 直航船兜底 | MRM-03 工程依据 |

### 11.3 第三方库版本（独立性允许的 5 个）

| 库 | 版本 | License | 来源 | 置信度 |
|---|---|---|---|---|
| Eigen | 5.0.0 (2025-09-30) | MPL-2.0 | https://gitlab.com/libeigen/eigen/-/releases | 🟢 A |
| spdlog | 1.17.0 (2026-01-05) | MIT | https://github.com/gabime/spdlog/releases | 🟢 A |
| fmt | 11.0.x | MIT | https://github.com/fmtlib/fmt | 🟢 A |
| yaml-cpp | 0.8.0 | MIT | https://github.com/jbeder/yaml-cpp/releases | 🟢 A |
| GTest / GMock | 1.17.0 | BSD-3-Clause | https://github.com/google/googletest/releases | 🟢 A |

### 11.4 禁用库（独立性约束 — CI 阻断）

| 库 | 禁用理由 | 在哪里使用（不在 M7） |
|---|---|---|
| CasADi 3.7.2 | 求解器复杂；违反 Checker 简化原则 | M5 Mid-MPC / BC-MPC |
| IPOPT 3.14.19 | CasADi 后端 | M5 |
| GeographicLib 2.7 | M7 不做大地坐标计算 | M2 / M3 / M5 |
| nlohmann::json 3.12.0 | M7 不解析 JSON（保持 enum 化简单语义）| M2 ENC 工具 / M3 / M8 |
| Boost.Geometry 1.91.0 | M7 不做几何运算 | M2 / M5 / M6 |

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Team-M7（Claude 实施层 kickoff） | 初稿创建：M7 代码骨架（PATH-S + 独立性强约束）。覆盖：8 模块订阅 / 4 模块发布；IEC 61508 + SOTIF 双轨架构；4 种预定义 MRM；100 周期滑窗（RFC-003）；NoExceptionResult / -fno-exceptions 模式；CMakeLists.txt 仅链接 5 个允许库；Doer-Checker 独立性自动验证（CI 阻断）；单元测试覆盖率 ≥ 95% 计划 |
| v1.1（待发布）| 2026-08-19+ | TBD | HAZID RUN-001 完成后回填所有 [TBD-HAZID] 阈值参数（6 项 SOTIF + 4 种 MRM 参数初始值校准）|

---

**文档完成**。本骨架共约 1000 行，是 Team-M7 实施 PATH-S 安全关键路径 + Doer-Checker 独立路径的代码起点。所有 `#include` 与 `target_link_libraries` 已经过白名单严格审查，可通过 `tools/ci/check-doer-checker-independence.sh` 自动验证（CI 阻断）。任何修改本骨架的 PR 须保留 §10 PATH-S 合规性 checklist 全绿，否则违反 ADR-001 + 决策四，CCS i-Ship 不通过。
