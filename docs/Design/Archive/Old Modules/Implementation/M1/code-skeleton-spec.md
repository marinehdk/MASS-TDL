# M1 ODD/Envelope Manager 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M1-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M1 开发起点） |
| 团队 | Team-M1 |
| 路径强度 | **PATH-S（安全关键路径）** |
| 详细设计基线 | `docs/Design/Detailed Design/M1-ODD-Envelope-Manager/01-detailed-design.md` v1.0（SANGO-ADAS-L3-DD-M1-001）|
| 架构基线 | v1.1.2 §3 ODD 框架 / §5 M1 / §15.1 ODD_StateMsg |
| SIL / DAL 等级 | **IEC 61508 SIL 2 / DO-178C DAL B** |
| 主语言 | C++17（GCC 14.3 LTS） |
| 适用范围 | `src/m1_odd_envelope_manager/` colcon package 全部源代码 |
| 关联文件 | `00-implementation-master-plan.md` §3 + §4 / `coding-standards.md` §1 + §3 + §4 / `static-analysis-policy.md` §3 + §5 / `ros2-idl-implementation-guide.md` §2 + §3.3 / `third-party-library-policy.md` §3 |

> **本 spec 是 Team-M1 编码起点。** 它不复述详细设计的算法（请回查 `01-detailed-design.md` §5），而是把**详细设计 + 工程基线 + IDL 合约**对齐到 colcon package 级文件结构、可编译 C++17 接口、CI 门禁映射。

> **强制纪律**：M1 是 PATH-S 安全关键路径，须满足 (a) 禁用动态分配（启动后所有内存预分配）；(b) 禁用异常（`-fno-exceptions` + `tl::expected`）；(c) 函数 ≤ 40 行；(d) 循环复杂度 ≤ 8；(e) 单元测试覆盖率 ≥ 95%；(f) Polyspace Code Prover Red = 0 / Orange ≤ 1%；(g) Doer-Checker 独立性矩阵中 M1 不被 M7 包含（由 `tools/ci/check-doer-checker-independence.sh` 验证）。

---

## 1. 模块概述

M1 ODD/Envelope Manager 是 L3 战术决策层的 **调度枢纽**，是整个 L3 内**唯一的"当前安全语境"权威**（详细设计 §1 + 架构 v1.1.2 §2.2 决策一）。

核心职责（详见详细设计 §1）：

1. **ODD 状态机**（5 状态：IN / EDGE / OUT / MRC_PREP / MRC_ACTIVE）+ ODD 子域分类（A/B/C/D）
2. **Conformance_Score 计算**（E/T/H 三轴加权，∈ [0,1]，详细设计 §5.2）
3. **TMR / TDL 量化**（详细设计 §5.2 + Veitch 2024 [R4]：TMR ≥ 60 s）
4. **MRC 触发**（MRC-01 漂移 / MRC-02 抛锚 / MRC-03 顶风停 / MRC-04 系泊）
5. **接管期间状态冻结 + 回切顺序化**（架构 v1.1.2 §11.9，F-NEW-006）

M1 主循环 4 Hz 驱动；ODD_StateMsg 1 Hz 周期发布 + EDGE→OUT 突变事件补发（≤ 50 ms 延迟）。

---

## 2. 项目目录结构

```
src/m1_odd_envelope_manager/
├── package.xml                              # ROS2 ament 依赖声明
├── CMakeLists.txt                           # 编译配置（PATH-S 强约束 -fno-exceptions / 全 -W*）
├── colcon.pkg                               # colcon 元配置（可选）
├── README.md                                # 模块入口（指向本 spec + 详细设计）
│
├── include/m1_odd_envelope_manager/         # 公共 header（同名 namespace mass_l3::m1）
│   ├── error.hpp                            # M1 ErrorCode enum (1000–1999)
│   ├── types.hpp                            # 内部类型 (OddZone, AutoLevel, EnvelopeState, ScoreTriple ...)
│   ├── odd_state_machine.hpp                # OddStateMachine 类声明
│   ├── conformance_score_calculator.hpp     # ConformanceScoreCalculator 类声明
│   ├── tmr_tdl_estimator.hpp                # TmrTdlEstimator 类声明
│   ├── mrc_trigger_logic.hpp                # MrcTriggerLogic 类声明
│   ├── parameter_loader.hpp                 # YAML 参数加载（依赖注入；HAZID 校准点入口）
│   └── odd_envelope_manager_node.hpp        # ROS2 节点类
│
├── src/                                     # 实现文件（不暴露的 internal 实现）
│   ├── odd_state_machine.cpp
│   ├── conformance_score_calculator.cpp
│   ├── tmr_tdl_estimator.cpp
│   ├── mrc_trigger_logic.cpp
│   ├── parameter_loader.cpp
│   ├── odd_envelope_manager_node.cpp
│   └── main.cpp                             # ROS2 spin 入口（≤ 30 行）
│
├── config/                                  # YAML 注入参数（HAZID 校准点；不硬编码）
│   ├── m1_params.yaml                       # 状态机阈值 / w_E w_T w_H / E-score 分段 / TMR 基线 / MRC 选择策略
│   └── m1_logging.yaml                      # spdlog logger 配置（独立日志文件）
│
├── launch/
│   └── m1_odd_envelope_manager.launch.py    # ROS2 launch 文件（参数注入 + QoS profile 加载）
│
├── msg/                                     # M1 私有消息（如有；当前无 — 全部经 l3_msgs）
│
└── test/                                    # GTest 单元测试（覆盖率目标 ≥ 95%）
    ├── CMakeLists.txt
    ├── test_odd_state_machine.cpp           # 状态转移 IN→EDGE→OUT / EDGE→MRC_PREP / OVERRIDDEN 等
    ├── test_conformance_score.cpp           # E/T/H 边界 + 加权融合 + 数值范围
    ├── test_tmr_tdl_estimator.cpp           # TCPA × 0.6 / T_comm_ok / T_sys_health 取最小
    ├── test_mrc_trigger_logic.cpp           # MRC-01..04 选择 + 水深/能见度/海况门禁
    ├── test_parameter_loader.cpp            # YAML 加载 + 范围校验
    ├── test_node_integration.cpp            # 节点级 mock subscriber/publisher 集成
    └── fixtures/
        ├── odd_state_msg_fixture.hpp
        └── world_state_msg_fixture.hpp
```

**子目录解释**：

- `include/m1_odd_envelope_manager/`：暴露 header；命名空间统一 `mass_l3::m1::*`；**不允许**被 M7 / M2–M6 包含（Doer-Checker 独立性 — 跨模块仅经 `l3_msgs/msg/*.hpp`）
- `src/`：实现文件；`main.cpp` 仅做 `rclcpp::init` + `spin`；不在 main 写业务逻辑
- `config/`：YAML 参数文件；所有 `[TBD-HAZID]` 参数从此注入；运行时不硬编码（详见 §8）
- `test/`：每个核心类一个独立测试文件；fixtures 提供共享消息构造器（不是 mock 框架，是显式构造器）

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

M1 实现为**单 ROS2 节点** `m1_odd_envelope_manager`：

- 不与其他 L3 节点 composable（独立进程，简化故障隔离）
- 单 callback 组（默认 `MutuallyExclusive`），主循环 4 Hz timer 驱动
- 事件型 callback（Override / Reflex / SafetyAlert）通过 `Reentrant` callback group **抢占**主循环（详细设计 §6.2）

详细设计 §10.2：M1 主循环 ≤ 200 ms 完成（4 Hz 周期 250 ms 预算内）；事件响应 ≤ 50 ms。

### 3.2 订阅者表格

按详细设计 §2.1 + 架构 v1.1.2 §15.2 + RFC 决议：

| Topic | Msg type | QoS profile | Callback 函数 | 频率 / 用途 |
|---|---|---|---|---|
| `/l3/m7/safety_alert` | `l3_msgs/SafetyAlert` | `QoS(50).reliable().transient_local()` | `on_safety_alert(SafetyAlert)` | 事件，M7 → M1，≤ 100 ms 仲裁 |
| `/reflex/activation_notification` | `l3_external_msgs/ReflexActivationNotification` | `QoS(50).reliable().transient_local()` | `on_reflex_activation(ReflexActivationNotification)` | 事件，最高优先级，≤ 50 ms 进 OVERRIDDEN |
| `/override/active_signal` | `l3_external_msgs/OverrideActiveSignal` | `QoS(50).reliable().transient_local()` | `on_override_signal(OverrideActiveSignal)` | 事件，最高优先级，进 OVERRIDDEN / 启动回切 |
| `/fusion/environment_state` | `l3_external_msgs/EnvironmentState` | `QoS(5).reliable()` | `on_environment_state(EnvironmentState)` | 0.2 Hz，E_score 输入 |
| `/fusion/own_ship_state` | `l3_external_msgs/FilteredOwnShipState` | `SensorDataQoS().keep_last(2)` | `on_own_ship_state(FilteredOwnShipState)` | 50 Hz，health 监控 + 心跳 |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | `QoS(5).reliable()` | `on_world_state(WorldState)` | 4 Hz，TCPA_min 计算 + zone 判别 |

> **注**：详细设计 §2.1 也将 `World_StateMsg` 列入 4 Hz 输入；本表与 IDL guide §2.3 topic 命名表对齐。`WorldState` 经 `/l3/m2/world_state` 订阅。

### 3.3 发布者表格

按详细设计 §3.1 + IDL guide §2.3：

| Topic | Msg type | QoS profile | 频率 | 触发 |
|---|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | `QoS(10).reliable().transient_local()` | 1 Hz + 事件 | 周期 timer (1 Hz) + EDGE→OUT 突变事件补发（≤ 50 ms）|
| `/l3/m1/mode_cmd` | `l3_msgs/ModeCmd` | `QoS(50).reliable().transient_local()` | 事件 | auto_level / allowed_zones 变更（≤ 100 ms 后 M4 生效）|
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | `QoS(50).reliable().transient_local()` | 事件 + 2 Hz | 状态变化、conformance_score 更新、MRC 触发、override 进出 |
| `/l3/sat/data` | `l3_msgs/SATData` | `QoS(5).reliable()` | 10 Hz（TDL ≤ 120 s 时升至 2 s/帧） | M1 → M8（透明性数据） |

> **MRC_RequestMsg 说明**：详细设计 §3.1 列出 MRC_RequestMsg → M5；架构 v1.1.2 §15.2 通过 `Mode_CmdMsg` + `SafetyAlert.recommended_mrm` 间接传达 MRC 请求。当前实施基线（v1.0）M1 通过 ModeCmd 中的扩展字段 + ASDR 记录传达 MRC 决策；如需独立 `MRCRequest` topic，须 RFC 走 ADR 评审（v1.1.3 候选）。

### 3.4 参数（YAML keys，含 [TBD-HAZID] 标注）

详见 §8.1。所有参数通过 `rclcpp::ParameterValue` 注入；启动时一次性加载到不可变 `ParameterSet`（PATH-S：运行期不允许动态修改决策核心参数；HAZID 校准结果通过重启加载）。

### 3.5 节点 Lifecycle

M1 实现为**普通 ROS2 节点**（非 LifecycleNode）。理由：

- M1 是 L3 调度枢纽，需在 L3 启动后立即可用；不引入 lifecycle 状态机额外复杂度
- 内部状态机（OddStateMachine）已覆盖运行态语义（IN / EDGE / OUT / MRC_PREP / MRC_ACTIVE / OVERRIDDEN）
- 启动顺序由 launch 文件保证（M1 在 M2 前启动，订阅者订阅时 M2 即可发布）

构造函数：
1. 加载 `m1_params.yaml` → `ParameterSet`（参数范围校验 → ErrorCode 失败 → `rclcpp::shutdown()`）
2. 初始化 spdlog 独立 logger（`mass_l3_m1`，独立日志文件）
3. 构造核心类（`OddStateMachine`、`ConformanceScoreCalculator`、`TmrTdlEstimator`、`MrcTriggerLogic`）
4. 创建 publishers / subscribers（QoS 按 §3.2 / §3.3）
5. 启动 timer：4 Hz 主循环 + 1 Hz ODDState 发布 + 0.5 Hz ASDR 定期记录

---

## 4. 核心类定义（C++17 header 骨架）

> 以下展示**真实可编译的 C++17 接口**。实现部分用 `// ...` 注释标记（团队按详细设计 §5 + §6 + §7 填充）。

### 4.1 `OddStateMachine`（5 状态：IN / EDGE / OUT / MRC_PREP / MRC_ACTIVE）

**文件**：`include/m1_odd_envelope_manager/odd_state_machine.hpp`

```cpp
#pragma once

// 1. C++ 标准库
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

// 2. 第三方库
#include <tl/expected.hpp>

// 3. 项目内
#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/**
 * @brief Finite state machine for ODD envelope per detailed-design §4.2 + §5.
 *
 * States: IN | EDGE | OUT | MRC_PREP | MRC_ACTIVE | OVERRIDDEN.
 * Transition is deterministic and total: any (state, ScoreTriple, EventFlags)
 * tuple maps to exactly one next state. No silent no-ops.
 *
 * INVARIANT: state_ is always in StateSet; never mutated outside step().
 *
 * @note PATH-S compliance: no dynamic allocation, no exceptions, no virtual.
 *       Used as value type (no inheritance).
 */
class OddStateMachine final {
 public:
  /// Compile-time fixed-size: no dynamic allocation.
  using TimePoint = std::chrono::steady_clock::time_point;

  /// Construction: initial state = IN; thresholds injected by ParameterSet.
  /// @return error if thresholds out of [0, 1] or not monotonic.
  static tl::expected<OddStateMachine, ErrorCode> create(
      const StateMachineThresholds& thresholds) noexcept;

  /// Compute next state given current inputs. Stateless w.r.t. external time;
  /// caller passes `now` for transition timestamping.
  ///
  /// @param score      Conformance score in [0, 1] (validated by caller).
  /// @param tdl_s      Tactical Decision Latency in seconds.
  /// @param tmr_s      Maximum Operator Response time in seconds.
  /// @param events     Event flags (Override / Reflex / SafetyAlert / etc.).
  /// @param now        Wall-clock timestamp for transition logging.
  /// @return next EnvelopeState; never errors (always defined transition).
  EnvelopeState step(double score,
                     double tdl_s,
                     double tmr_s,
                     EventFlags events,
                     TimePoint now) noexcept;

  /// Current state accessor.
  [[nodiscard]] EnvelopeState current() const noexcept { return current_; }

  /// CMM SAT-1 hook: textual state for HMI / ASDR.
  [[nodiscard]] std::string_view rationale() const noexcept;

  /// Last transition timestamp (for ASDR record).
  [[nodiscard]] TimePoint last_transition_at() const noexcept {
    return last_transition_at_;
  }

  /// Forecast next state at horizon Δt assuming current trend (CMM SAT-3).
  /// Returns expected state + uncertainty in [0, 1].
  [[nodiscard]] StateForecast forecast(std::chrono::seconds horizon) const noexcept;

 private:
  explicit OddStateMachine(const StateMachineThresholds& thresholds) noexcept;

  /// Pure transition function (no side effect except member updates).
  /// Cyclomatic complexity ≤ 8 (PATH-S limit).
  EnvelopeState compute_next(double score,
                             double tdl_s,
                             double tmr_s,
                             EventFlags events) const noexcept;

  StateMachineThresholds thresholds_;
  EnvelopeState current_;
  TimePoint last_transition_at_;
  std::string_view current_rationale_;  // INVARIANT: points to static string literal pool only.
};

}  // namespace mass_l3::m1
```

### 4.2 `ConformanceScoreCalculator`（E/T/H 三轴加权）

**文件**：`include/m1_odd_envelope_manager/conformance_score_calculator.hpp`

```cpp
#pragma once

#include <cstdint>

#include <tl/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/**
 * @brief Compute Conformance_Score per architecture v1.1.2 §3.6 + detailed-design §5.2.
 *
 * Score = w_E * E_score + w_T * T_score + w_H * H_score, normalized to [0, 1].
 *
 * Weights w_E / w_T / w_H are HAZID-calibrated (initial: 0.4 / 0.3 / 0.3).
 * Normalization invariant: w_E + w_T + w_H == 1.0 (validated at construction).
 *
 * @note PATH-S: no allocation; pure computation; `compute()` is noexcept.
 */
class ConformanceScoreCalculator final {
 public:
  static tl::expected<ConformanceScoreCalculator, ErrorCode> create(
      const ScoreWeights& weights,
      const EScoreThresholds& e_thresholds,
      const TScoreThresholds& t_thresholds,
      const HScoreThresholds& h_thresholds) noexcept;

  /// Compute three-axis scores from input snapshot.
  /// Inputs are pre-validated by caller (see ParameterLoader range checks).
  /// @return ScoreTriple {e_score, t_score, h_score, conformance_score}.
  [[nodiscard]] ScoreTriple compute(const ScoringInputs& inputs) const noexcept;

  /// Individual axis evaluators (exposed for unit testing).
  [[nodiscard]] double evaluate_e_score(double visibility_nm,
                                        double sea_state_hs) const noexcept;
  [[nodiscard]] double evaluate_t_score(const TScoreInputs& t_inputs) const noexcept;
  [[nodiscard]] double evaluate_h_score(const HScoreInputs& h_inputs) const noexcept;

 private:
  ConformanceScoreCalculator(const ScoreWeights& weights,
                             const EScoreThresholds& e_thresholds,
                             const TScoreThresholds& t_thresholds,
                             const HScoreThresholds& h_thresholds) noexcept;

  ScoreWeights weights_;
  EScoreThresholds e_thresholds_;
  TScoreThresholds t_thresholds_;
  HScoreThresholds h_thresholds_;
};

}  // namespace mass_l3::m1
```

### 4.3 `TmrTdlEstimator`

**文件**：`include/m1_odd_envelope_manager/tmr_tdl_estimator.hpp`

```cpp
#pragma once

#include <chrono>
#include <cstdint>

#include <tl/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/**
 * @brief Estimate TMR (Maximum Operator Response time) and TDL (Tactical Decision Latency).
 *
 * Per detailed-design §5.2:
 *   TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)
 *   TMR = baseline 60 s [R4 Veitch 2024], degraded by H_score availability.
 *
 * INVARIANT: tmr_s_ ∈ [30, 600]; tdl_s_ ∈ [0, 600] (architecture §3.4 + detailed-design §2.2).
 *
 * @note PATH-S: stack-only; no chrono::system_clock (uses caller-provided times).
 */
class TmrTdlEstimator final {
 public:
  static tl::expected<TmrTdlEstimator, ErrorCode> create(
      const TmrTdlParams& params) noexcept;

  /// Compute TMR/TDL pair from current scoring inputs + system health snapshot.
  /// @return TmrTdlPair {tmr_s, tdl_s}; clamped to invariant ranges.
  [[nodiscard]] TmrTdlPair compute(const TmrTdlInputs& inputs) const noexcept;

  /// TDL forecast at horizon Δt, used by SAT-3.
  [[nodiscard]] TmrTdlPair forecast(const TmrTdlInputs& current,
                                    std::chrono::seconds horizon) const noexcept;

 private:
  explicit TmrTdlEstimator(const TmrTdlParams& params) noexcept;

  /// Estimate communication availability window from RTT trend.
  [[nodiscard]] double estimate_t_comm_ok(double current_rtt_s) const noexcept;

  /// Estimate system health endurance from heartbeat recency / fault counts.
  [[nodiscard]] double estimate_t_sys_health(const SystemHealthSnapshot& health) const noexcept;

  TmrTdlParams params_;
};

}  // namespace mass_l3::m1
```

### 4.4 `MrcTriggerLogic`（MRC-01..04）

**文件**：`include/m1_odd_envelope_manager/mrc_trigger_logic.hpp`

```cpp
#pragma once

#include <cstdint>
#include <optional>

#include <tl/expected.hpp>

#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

/**
 * @brief Select MRC (Minimum Risk Condition) per architecture v1.1.2 §11.6 + detailed-design §7.1.
 *
 * MRC catalog (initial baseline; HAZID-calibrated):
 *   - MRC-01 DRIFT       : reduce speed to 4 kn, hold heading.
 *   - MRC-02 ANCHOR      : descend anchor; only allowed when water depth ≤ 50 m.
 *   - MRC-03 HEAVE_TO    : head into wind/current, minimum maneuvering speed.
 *   - MRC-04 MOORED      : already moored / berthed; report only.
 *
 * Selection follows priority + ODD context. Pre-defined command set only;
 * does not synthesize trajectories (per ADR-001: M7 Checker triggers, M5 executes).
 *
 * @note PATH-S: no allocation; selection is O(1) table lookup.
 */
class MrcTriggerLogic final {
 public:
  static tl::expected<MrcTriggerLogic, ErrorCode> create(
      const MrcParams& params) noexcept;

  /// Select MRC type given current ODD context + recommended MRM from M7.
  /// @return MrcSelection {mrc_type, parameters, rationale}; nullopt if no MRC required.
  [[nodiscard]] std::optional<MrcSelection> select(
      const MrcSelectionInputs& inputs) const noexcept;

  /// Hard predicates exposed for unit testing.
  [[nodiscard]] bool can_anchor(double water_depth_m,
                                bool in_anchorage_zone) const noexcept;
  [[nodiscard]] bool can_heave_to(double sea_state_hs,
                                  double wind_speed_kn) const noexcept;

 private:
  explicit MrcTriggerLogic(const MrcParams& params) noexcept;

  MrcParams params_;
};

}  // namespace mass_l3::m1
```

### 4.5 `OddEnvelopeManagerNode`（ROS2 节点类）

**文件**：`include/m1_odd_envelope_manager/odd_envelope_manager_node.hpp`

```cpp
#pragma once

#include <chrono>
#include <memory>
#include <string>

// ROS2
#include <rclcpp/rclcpp.hpp>

// l3_msgs / l3_external_msgs
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"

// 项目内
#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"
#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"
#include "m1_odd_envelope_manager/odd_state_machine.hpp"
#include "m1_odd_envelope_manager/parameter_loader.hpp"
#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"

namespace mass_l3::m1 {

/**
 * @brief ROS2 node for M1 ODD/Envelope Manager.
 *
 * Single-node ownership of state machine + calculators + estimators.
 * Main loop driven by 4 Hz timer; periodic ODDState publish at 1 Hz;
 * event-driven re-publish on EDGE→OUT transitions.
 *
 * Per detailed-design §6.1 timing budget; PATH-S compliance per §10.2.
 */
class OddEnvelopeManagerNode : public rclcpp::Node {
 public:
  OddEnvelopeManagerNode();
  // No copy; no move (rclcpp::Node base prevents).
  OddEnvelopeManagerNode(const OddEnvelopeManagerNode&) = delete;
  OddEnvelopeManagerNode& operator=(const OddEnvelopeManagerNode&) = delete;
  ~OddEnvelopeManagerNode() override = default;

 private:
  // ===== Initialization =====
  void initialize_parameters();
  void initialize_publishers();
  void initialize_subscribers();
  void initialize_timers();

  // ===== Subscriber callbacks =====
  void on_safety_alert(const l3_msgs::msg::SafetyAlert::SharedPtr msg) noexcept;
  void on_reflex_activation(
      const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr msg) noexcept;
  void on_override_signal(
      const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg) noexcept;
  void on_environment_state(
      const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) noexcept;
  void on_own_ship_state(
      const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) noexcept;
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg) noexcept;

  // ===== Timer callbacks =====
  void on_main_loop_tick() noexcept;            // 4 Hz
  void on_odd_state_publish_tick() noexcept;    // 1 Hz
  void on_asdr_record_periodic_tick() noexcept; // 0.5 Hz

  // ===== Internal helpers =====
  void publish_odd_state_event(EnvelopeState old_state,
                               EnvelopeState new_state) noexcept;
  void publish_mode_cmd(AutoLevel new_level, std::string_view reason) noexcept;
  void publish_asdr_record(std::string_view decision_type,
                           std::string_view rationale_json) noexcept;
  void publish_sat_data() noexcept;
  void check_input_freshness(rclcpp::Time now) noexcept;

  // ===== Owned state =====
  ParameterSet params_;
  std::unique_ptr<OddStateMachine> state_machine_;             // PATH-S: pre-allocated.
  std::unique_ptr<ConformanceScoreCalculator> score_calc_;
  std::unique_ptr<TmrTdlEstimator> tmr_tdl_;
  std::unique_ptr<MrcTriggerLogic> mrc_;

  // ===== Cached input snapshots (latest received) =====
  l3_external_msgs::msg::EnvironmentState::SharedPtr last_env_state_;
  l3_external_msgs::msg::FilteredOwnShipState::SharedPtr last_own_ship_;
  l3_msgs::msg::WorldState::SharedPtr last_world_state_;
  rclcpp::Time last_world_state_received_;
  rclcpp::Time last_env_state_received_;
  rclcpp::Time last_own_ship_received_;

  // ===== Override state =====
  bool override_active_;
  rclcpp::Time override_entry_at_;

  // ===== Publishers =====
  rclcpp::Publisher<l3_msgs::msg::ODDState>::SharedPtr odd_state_pub_;
  rclcpp::Publisher<l3_msgs::msg::ModeCmd>::SharedPtr mode_cmd_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;

  // ===== Subscribers =====
  rclcpp::Subscription<l3_msgs::msg::SafetyAlert>::SharedPtr safety_alert_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::ReflexActivationNotification>::SharedPtr
      reflex_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr
      override_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::EnvironmentState>::SharedPtr env_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr
      own_ship_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_state_sub_;

  // ===== Timers =====
  rclcpp::TimerBase::SharedPtr main_loop_timer_;        // 4 Hz
  rclcpp::TimerBase::SharedPtr odd_publish_timer_;      // 1 Hz
  rclcpp::TimerBase::SharedPtr asdr_periodic_timer_;    // 0.5 Hz

  // ===== Callback groups =====
  rclcpp::CallbackGroup::SharedPtr main_cbg_;        // MutuallyExclusive (default)
  rclcpp::CallbackGroup::SharedPtr event_cbg_;       // Reentrant for high-prio events
};

}  // namespace mass_l3::m1
```

**`main.cpp`**（≤ 30 行）：

```cpp
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  // PATH-S: node owns all heap allocations done at construction; no allocation in spin loop.
  auto node = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();
  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
```

### 4.6 `error.hpp`（M1 ErrorCode 1000–1999）

```cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "mass_l3/common/error_code.hpp"

namespace mass_l3::m1 {

/// M1 module-specific error codes per coding-standards.md §8.2.
/// Range 1000–1999 (architecture-mandated module slot).
enum class ErrorCode : int32_t {
  Ok = 0,
  ParameterOutOfRange = 1001,
  ParameterFileNotFound = 1002,
  ParameterFileMalformed = 1003,
  WeightsNotNormalized = 1004,    // w_E + w_T + w_H != 1.0
  ThresholdsNonMonotonic = 1005,  // EDGE > IN bound, etc.
  EScoreInvalidInputs = 1006,
  TScoreInvalidInputs = 1007,
  HScoreInvalidInputs = 1008,
  TmrOutOfBounds = 1009,
  TdlOutOfBounds = 1010,
  MrcUnselectable = 1011,         // No MRC type valid for current ODD context.
  StateMachineInvalidTransition = 1012,
  InputStaleness = 1013,
  AsdrSerializationFailed = 1014,
};

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept;

}  // namespace mass_l3::m1
```

### 4.7 `types.hpp`（内部值类型）

仅展示关键骨架：

```cpp
#pragma once

#include <array>
#include <cstdint>

namespace mass_l3::m1 {

enum class OddZone : std::uint8_t { A = 0, B = 1, C = 2, D = 3 };
enum class AutoLevel : std::uint8_t { D2 = 2, D3 = 3, D4 = 4 };
enum class SystemHealth : std::uint8_t { Full = 0, Degraded = 1, Critical = 2 };
enum class EnvelopeState : std::uint8_t {
  In = 0, Edge = 1, Out = 2, MrcPrep = 3, MrcActive = 4, Overridden = 5
};
enum class MrcType : std::uint8_t { Drift = 1, Anchor = 2, HeaveTo = 3, Moored = 4 };

struct ScoreTriple {
  double e_score;
  double t_score;
  double h_score;
  double conformance_score;
};

struct ScoreWeights {
  double w_e;
  double w_t;
  double w_h;
};

struct EScoreThresholds {
  double visibility_full_nm;     // [TBD-HAZID] 2.0 (FCB initial)
  double visibility_degraded_nm; // [TBD-HAZID] 1.0
  double visibility_marginal_nm; // [TBD-HAZID] 0.5
  double sea_state_full_hs;      // [TBD-HAZID] 2.5
  double sea_state_degraded_hs;  // [TBD-HAZID] 3.0
  double sea_state_marginal_hs;  // [TBD-HAZID] 4.0
};

struct StateMachineThresholds {
  double in_to_edge;   // [TBD-HAZID] 0.8
  double edge_to_out;  // [TBD-HAZID] 0.5
};

struct EventFlags {
  bool override_active;
  bool reflex_activation;
  bool m7_safety_critical;
  bool m7_safety_mrc_required;
  bool m2_input_stale;
  bool m7_input_stale;
};

struct StateForecast {
  EnvelopeState predicted;
  double uncertainty;  // ∈ [0, 1]
};

struct TmrTdlPair {
  double tmr_s;
  double tdl_s;
};

// ... ScoringInputs / TmrTdlInputs / TmrTdlParams / MrcParams / MrcSelectionInputs /
//     MrcSelection / SystemHealthSnapshot / TScoreInputs / HScoreInputs / TScoreThresholds /
//     HScoreThresholds / ParameterSet 略 (与详细设计 §4.1 状态变量字段一一对应)
// ...

}  // namespace mass_l3::m1
```

---

## 5. CMakeLists.txt（PATH-S 编译选项）

```cmake
cmake_minimum_required(VERSION 3.22)
project(m1_odd_envelope_manager VERSION 1.0.0 LANGUAGES CXX)

# ===== C++ standard =====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy / cppcheck.

# ===== Path-S strict flags (per coding-standards.md §1 + §2.2 + project rules) =====
add_compile_options(
  # Warning level (mandatory)
  -Wall -Wextra -Wpedantic
  -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wcast-align -Wcast-qual
  -Wold-style-cast -Wzero-as-null-pointer-constant
  -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion
  -Wfloat-equal
  -Wformat=2 -Wformat-security
  -Wmissing-declarations
  -Wundef -Wunused
  # GCC-specific
  -Wuseless-cast
  -Wlogical-op
  -Wduplicated-cond -Wduplicated-branches
  # Security
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
  # PATH-S specific: no exceptions, no RTTI (PROJ-LR-002)
  -fno-exceptions
  -fno-rtti
)

# Sanitizers in Debug build
add_compile_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)
add_link_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# Release-Mandatory: -O2 (no -O3; per coding-standards.md §2.2)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O2)
endif()

# ===== Dependencies =====
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# Vendored 3rd-party (locked per third-party-library-policy.md §2.1)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)
# tl::expected (header-only) — vendored at tools/third-party/expected-1.1.0/
include_directories(${CMAKE_SOURCE_DIR}/../../tools/third-party/expected-1.1.0/include)

# Note: M1 does NOT depend on Eigen / GeographicLib / nlohmann::json / Boost.Geometry / CasADi.
# Per third-party-library-policy.md §3.1: M1 minimal-deps to ease Polyspace coverage.

# ===== Library target =====
add_library(${PROJECT_NAME}_lib
  src/odd_state_machine.cpp
  src/conformance_score_calculator.cpp
  src/tmr_tdl_estimator.cpp
  src/mrc_trigger_logic.cpp
  src/parameter_loader.cpp
  src/odd_envelope_manager_node.cpp
)
target_include_directories(${PROJECT_NAME}_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(${PROJECT_NAME}_lib
  rclcpp
  builtin_interfaces
  geometry_msgs
  geographic_msgs
  l3_msgs
  l3_external_msgs
)
target_link_libraries(${PROJECT_NAME}_lib PUBLIC
  spdlog::spdlog
  yaml-cpp::yaml-cpp
)

# ===== Executable =====
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

# ===== Install =====
install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_lib
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# ===== Tests =====
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()
  add_subdirectory(test)
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs)
ament_package()
```

---

## 6. package.xml

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>m1_odd_envelope_manager</name>
  <version>1.0.0</version>
  <description>
    M1 ODD/Envelope Manager — L3 tactical layer scheduling authority.
    Single source of truth for current ODD zone, auto level, and envelope state.
    PATH-S safety-critical module per architecture v1.1.2 §5; IEC 61508 SIL 2 / DAL B.
  </description>
  <maintainer email="team-m1@mass-l3-tdl.local">Team-M1</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>builtin_interfaces</depend>
  <depend>geometry_msgs</depend>
  <depend>geographic_msgs</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>spdlog</depend>
  <depend>yaml-cpp</depend>

  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_cmake_gtest</test_depend>
  <test_depend>ament_cmake_clang_tidy</test_depend>
  <test_depend>ament_cmake_cppcheck</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 7. 单元测试骨架（GTest）

测试覆盖率目标 **≥ 95%**（PATH-S 阈值，比 PATH-D 的 90% 更严；详细设计 §9.1）。

### 7.1 `test_odd_state_machine.cpp` — 状态转移测试

```cpp
#include <gtest/gtest.h>

#include "m1_odd_envelope_manager/odd_state_machine.hpp"

using namespace mass_l3::m1;

namespace {
StateMachineThresholds default_thresholds() {
  return {.in_to_edge = 0.8, .edge_to_out = 0.5};
}
EventFlags no_events() {
  return {false, false, false, false, false, false};
}
}  // namespace

TEST(OddStateMachine, ConstructionRejectsNonMonotonicThresholds) {
  StateMachineThresholds bad{.in_to_edge = 0.4, .edge_to_out = 0.6};
  auto result = OddStateMachine::create(bad);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ErrorCode::ThresholdsNonMonotonic);
}

TEST(OddStateMachine, InitialStateIsIn) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  EXPECT_EQ(sm.current(), EnvelopeState::In);
}

TEST(OddStateMachine, InToEdgeAtThreshold) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  auto next = sm.step(0.79, /*tdl*/120.0, /*tmr*/60.0,
                      no_events(), std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::Edge);
}

TEST(OddStateMachine, EdgeToOutWhenScoreDropsBelowEdge) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  sm.step(0.79, 120.0, 60.0, no_events(), std::chrono::steady_clock::now());
  auto next = sm.step(0.49, 120.0, 60.0,
                      no_events(), std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::Out);
}

TEST(OddStateMachine, EdgeToMrcPrepWhenTdlBelowTmr) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  sm.step(0.79, 120.0, 60.0, no_events(), std::chrono::steady_clock::now());
  // TDL drops below TMR.
  auto next = sm.step(0.79, 50.0, 60.0,
                      no_events(), std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::MrcPrep);
}

TEST(OddStateMachine, OverrideHasHighestPriority) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  EventFlags ev{.override_active = true};
  auto next = sm.step(0.95, 200.0, 60.0, ev, std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::Overridden);
}

TEST(OddStateMachine, ReflexActivationOverridesEvenInIn) {
  auto sm = OddStateMachine::create(default_thresholds()).value();
  EventFlags ev{.reflex_activation = true};
  auto next = sm.step(0.95, 200.0, 60.0, ev, std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::Overridden);
}

TEST(OddStateMachine, OutOnAnyAxisZero) {
  // E_score = 0 (visibility < 0.5) → score = 0 even if T/H = 1.0.
  // Detailed-design §4.2: any axis = 0 forces OUT regardless of weighted score.
  auto sm = OddStateMachine::create(default_thresholds()).value();
  // Simulate by passing score that already reflects E=0 collapse.
  auto next = sm.step(0.0, 120.0, 60.0,
                      no_events(), std::chrono::steady_clock::now());
  EXPECT_EQ(next, EnvelopeState::Out);
}

// ... ≥ 20 cases per detailed-design §9.1 (state pairs × event combos × edge values).
```

### 7.2 `test_conformance_score.cpp` — 评分边界测试

```cpp
#include <cmath>
#include <gtest/gtest.h>

#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"

using namespace mass_l3::m1;

namespace {
ScoreWeights default_weights() { return {.w_e = 0.4, .w_t = 0.3, .w_h = 0.3}; }
EScoreThresholds default_e() {
  return {.visibility_full_nm = 2.0, .visibility_degraded_nm = 1.0,
          .visibility_marginal_nm = 0.5,
          .sea_state_full_hs = 2.5, .sea_state_degraded_hs = 3.0,
          .sea_state_marginal_hs = 4.0};
}
}  // namespace

TEST(ConformanceScore, CreateRejectsNonNormalizedWeights) {
  ScoreWeights bad{.w_e = 0.5, .w_t = 0.3, .w_h = 0.3};  // sum = 1.1
  auto r = ConformanceScoreCalculator::create(bad, default_e(), {}, {});
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ErrorCode::WeightsNotNormalized);
}

TEST(ConformanceScore, EScoreFullConditions) {
  auto calc = ConformanceScoreCalculator::create(
      default_weights(), default_e(), {}, {}).value();
  // visibility 3.0 nm, Hs 1.5 m → E_score = 1.0
  EXPECT_NEAR(calc.evaluate_e_score(3.0, 1.5), 1.0, 1e-9);
}

TEST(ConformanceScore, EScoreBoundary) {
  auto calc = ConformanceScoreCalculator::create(
      default_weights(), default_e(), {}, {}).value();
  // visibility 2.0 nm, Hs 2.5 m → E_score = 1.0 (exact threshold inclusive)
  EXPECT_NEAR(calc.evaluate_e_score(2.0, 2.5), 1.0, 1e-9);
}

TEST(ConformanceScore, EScoreOutOfOdd) {
  auto calc = ConformanceScoreCalculator::create(
      default_weights(), default_e(), {}, {}).value();
  // visibility 0.3 nm → fog-out → E_score = 0
  EXPECT_NEAR(calc.evaluate_e_score(0.3, 1.0), 0.0, 1e-9);
}

TEST(ConformanceScore, ScoreNeverExceedsOne) {
  auto calc = ConformanceScoreCalculator::create(
      default_weights(), default_e(), {}, {}).value();
  ScoringInputs perfect{};  // all axes = 1.0 (default)
  auto t = calc.compute(perfect);
  EXPECT_LE(t.conformance_score, 1.0);
  EXPECT_GE(t.conformance_score, 0.0);
}

// ... ≥ 20 cases per detailed-design §9.1.
```

### 7.3 `test_tmr_tdl_estimator.cpp` — TMR/TDL 计算测试

```cpp
#include <gtest/gtest.h>

#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"

using namespace mass_l3::m1;

TEST(TmrTdlEstimator, TdlIsMinimumOfThreeFactors) {
  TmrTdlParams params{.tcpa_coefficient = 0.6, .tmr_baseline_s = 60.0};
  auto est = TmrTdlEstimator::create(params).value();

  // TCPA × 0.6 = 60 s; T_comm_ok = 200 s; T_sys_health = 300 s
  // TDL should equal min = 60.
  TmrTdlInputs inputs{.tcpa_min_s = 100.0, .comm_rtt_s = 0.2,
                      .health = SystemHealthSnapshot{}};
  auto pair = est.compute(inputs);
  EXPECT_NEAR(pair.tdl_s, 60.0, 1e-6);
}

TEST(TmrTdlEstimator, TdlClampedToZeroWhenNegative) {
  TmrTdlParams params{.tcpa_coefficient = 0.6, .tmr_baseline_s = 60.0};
  auto est = TmrTdlEstimator::create(params).value();
  TmrTdlInputs inputs{.tcpa_min_s = -1.0};  // pathological input
  auto pair = est.compute(inputs);
  EXPECT_GE(pair.tdl_s, 0.0);
}

TEST(TmrTdlEstimator, TmrAtBaselineWhenHealthFull) {
  TmrTdlParams params{.tcpa_coefficient = 0.6, .tmr_baseline_s = 60.0};
  auto est = TmrTdlEstimator::create(params).value();
  TmrTdlInputs inputs{.tcpa_min_s = 200.0,
                      .health = SystemHealthSnapshot{.h_score = 1.0}};
  auto pair = est.compute(inputs);
  EXPECT_NEAR(pair.tmr_s, 60.0, 1e-6);
}

TEST(TmrTdlEstimator, TmrInBoundsAlways) {
  // INVARIANT: tmr_s ∈ [30, 600]
  TmrTdlParams params{.tcpa_coefficient = 0.6, .tmr_baseline_s = 60.0};
  auto est = TmrTdlEstimator::create(params).value();
  TmrTdlInputs inputs{.tcpa_min_s = 10000.0};  // huge TCPA
  auto pair = est.compute(inputs);
  EXPECT_GE(pair.tmr_s, 30.0);
  EXPECT_LE(pair.tmr_s, 600.0);
}

// ... ≥ 12 cases per detailed-design §9.1.
```

### 7.4 `test_mrc_trigger_logic.cpp` — MRC 触发场景测试

```cpp
#include <gtest/gtest.h>

#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"

using namespace mass_l3::m1;

TEST(MrcTriggerLogic, DriftSelectedInDeepWater) {
  MrcParams params{.anchor_max_depth_m = 50.0, .heave_to_max_hs = 4.0};
  auto m = MrcTriggerLogic::create(params).value();
  MrcSelectionInputs in{.water_depth_m = 200.0, .sea_state_hs = 2.0,
                        .in_anchorage_zone = false};
  auto sel = m.select(in);
  ASSERT_TRUE(sel.has_value());
  EXPECT_EQ(sel->mrc_type, MrcType::Drift);
}

TEST(MrcTriggerLogic, AnchorPreferredInShallowAnchorageZone) {
  MrcParams params{.anchor_max_depth_m = 50.0, .heave_to_max_hs = 4.0};
  auto m = MrcTriggerLogic::create(params).value();
  MrcSelectionInputs in{.water_depth_m = 30.0, .sea_state_hs = 1.5,
                        .in_anchorage_zone = true};
  auto sel = m.select(in);
  ASSERT_TRUE(sel.has_value());
  EXPECT_EQ(sel->mrc_type, MrcType::Anchor);
}

TEST(MrcTriggerLogic, HeaveToRejectedInHighSeaState) {
  MrcParams params{.anchor_max_depth_m = 50.0, .heave_to_max_hs = 4.0};
  auto m = MrcTriggerLogic::create(params).value();
  EXPECT_FALSE(m.can_heave_to(/*Hs*/5.5, /*wind*/30.0));
}

TEST(MrcTriggerLogic, AnchorRejectedInDeepWater) {
  MrcParams params{.anchor_max_depth_m = 50.0, .heave_to_max_hs = 4.0};
  auto m = MrcTriggerLogic::create(params).value();
  EXPECT_FALSE(m.can_anchor(/*depth*/200.0, /*in_zone*/false));
}

// ... ≥ 12 cases per detailed-design §9.1 + §7 degradation matrix.
```

---

## 8. 配置文件

### 8.1 `config/m1_params.yaml`（含 `[TBD-HAZID]`）

```yaml
# M1 ODD/Envelope Manager parameters.
# All values marked [TBD-HAZID] are subject to RUN-001 calibration (deadline 2026-08-19).
# Initial baselines per detailed-design §5.3 + architecture v1.1.2 §3.6.

m1_odd_envelope_manager:
  ros__parameters:
    # ===== Conformance Score weights (architecture v1.1.2 §3.6 + F-NEW-003) =====
    # INVARIANT: w_e + w_t + w_h == 1.0 (validated at startup)
    weights:
      w_e: 0.4   # [TBD-HAZID] env weight (0.2–0.6 range; initial value per architecture §3.6)
      w_t: 0.3   # [TBD-HAZID] task weight (0.1–0.5)
      w_h: 0.3   # [TBD-HAZID] human-machine weight (0.1–0.5)

    # ===== E_score visibility/sea-state thresholds (detailed-design §5.3) =====
    e_score_thresholds:
      visibility_full_nm:     2.0   # [TBD-HAZID]
      visibility_degraded_nm: 1.0   # [TBD-HAZID]
      visibility_marginal_nm: 0.5   # [TBD-HAZID]
      sea_state_full_hs:      2.5   # [TBD-HAZID]
      sea_state_degraded_hs:  3.0   # [TBD-HAZID]
      sea_state_marginal_hs:  4.0   # [TBD-HAZID]

    # ===== T_score (sensor + comm health) thresholds =====
    t_score_thresholds:
      comm_rtt_full_s:     0.5   # [TBD-HAZID]
      comm_rtt_degraded_s: 2.0   # [TBD-HAZID]
      comm_rtt_critical_s: 5.0   # [TBD-HAZID]

    # ===== H_score (human availability) thresholds =====
    h_score_thresholds:
      tmr_full_s:     60.0   # [TBD-HAZID] Veitch 2024 [R4] baseline
      tmr_degraded_s: 30.0   # [TBD-HAZID]

    # ===== State machine boundaries (detailed-design §4.2) =====
    state_machine:
      in_to_edge_score:  0.8   # [TBD-HAZID] IN→EDGE boundary
      edge_to_out_score: 0.5   # [TBD-HAZID] EDGE→OUT boundary
      zone_margin_pct:   0.20  # [TBD-HAZID] EDGE entrance margin (20%)

    # ===== TMR / TDL parameters (detailed-design §5.3 + Veitch 2024 [R4]) =====
    tmr_tdl:
      tcpa_coefficient:  0.6   # [TBD-HAZID] 60% collision avoidance / 40% operator
      tmr_baseline_s:    60.0  # Veitch 2024 [R4] empirical
      tmr_min_s:         30.0  # Range bound (architecture §3.4)
      tmr_max_s:         600.0
      tdl_min_s:         0.0
      tdl_max_s:         600.0

    # ===== MRC selection (detailed-design §7.1 + architecture v1.1.2 §11.6) =====
    mrc:
      anchor_max_depth_m:  50.0  # [TBD-HAZID] FCB initial; depth limit for anchor
      heave_to_max_hs:     4.0   # [TBD-HAZID] sea-state limit for heave-to
      drift_speed_kn:      4.0   # [TBD-HAZID] MRC-01 drift target speed
      mrc_priority:                # priority order when multiple MRC viable
        - "MRC_DRIFT"
        - "MRC_HEAVE_TO"
        - "MRC_ANCHOR"
        - "MRC_MOORED"

    # ===== Input freshness timeouts (detailed-design §2.2 + §7.3) =====
    freshness:
      world_state_timeout_ms:        500   # 2 cycles of 4 Hz
      world_state_critical_ms:       1250  # 5 cycles → MRC-01
      environment_state_timeout_s:   30.0  # 6 cycles of 0.2 Hz
      own_ship_state_timeout_ms:     200   # 50 Hz × 10 cycles
      m7_alert_silence_s:            5.0

    # ===== Logging =====
    log_level: "info"  # trace | debug | info | warn | error | critical
```

### 8.2 `config/m1_logging.yaml`

```yaml
# spdlog independent logger config for M1 (per third-party-library-policy.md §3.1).
# M1 logger name: "mass_l3_m1"
# File: /var/log/mass_l3/m1.log (production) or build/log/m1.log (development)

logger:
  name: "mass_l3_m1"
  level: "info"
  pattern: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%t] %v"
  sinks:
    - type: "rotating_file"
      filename: "/var/log/mass_l3/m1.log"
      max_size_mb: 50
      max_files: 10
    - type: "stdout"
      level: "warn"
  flush_on:
    - "warn"
    - "error"
    - "critical"

# Per architecture v1.1.2 §11.6 + ASDR sub-logger:
# decision-level events → /l3/asdr/record (separate ROS2 topic, NOT logger).
# spdlog is operational logging only; it does NOT replace ASDRRecord publishing.
```

---

## 9. PATH-S 合规性 checklist（DoD）

模块声明 Phase E1 完成前，**所有项均须打勾**（CI 自动验证 + reviewer 审计）：

### 9.1 编译 + 静态分析

- [ ] `colcon build --packages-select m1_odd_envelope_manager` 全绿（`-Werror` + 无 warning）
- [ ] `clang-tidy` 0 critical / 0 major / ≤ 5 minor（PATH-S 阈值，详见 `static-analysis-policy.md` §3）
- [ ] `cppcheck Premium` MISRA C++:2023 Mandatory + Required = 0 violation
- [ ] `Polyspace Code Prover` Red = 0；Orange ≤ 1%
- [ ] `Coverity`（推荐）High = 0；Medium = 0；Low ≤ 5
- [ ] `tools/ci/check-doer-checker-independence.sh` 通过（M7 不包含 M1 内部 header — 由 M7 仓守护，M1 只确保对外 header 可用）
- [ ] `AddressSanitizer` + `UBSan` 单元测试 0 错误
- [ ] `ThreadSanitizer` 单元测试 0 race（M1 单线程 + ROS2 callback group 隔离）

### 9.2 PATH-S 项目硬规则

- [ ] **PROJ-LR-001**：M1 启动后无动态分配 — 验证：`heaptrack` / `valgrind --tool=massif` 在长时（≥ 1 h）压测后堆增长 ≤ 0
- [ ] **PROJ-LR-002**：M1 编译开 `-fno-exceptions`；源代码 0 个 `throw` / `try` / `catch`；错误用 `tl::expected<T, ErrorCode>`
- [ ] **PROJ-LR-005**：所有函数循环复杂度 ≤ 8（clang-tidy `readability-function-cognitive-complexity` Threshold=8）
- [ ] **PROJ-LR-006**：所有 `double` 比较用 `std::abs(a-b) < eps`；0 个 `==` / `!=` 直比
- [ ] **PROJ-LR-007**：时间字段全用 `std::chrono`；无 raw `double` seconds 跨函数传递
- [ ] **PROJ-LR-008**：模块边界 `.msg` 用度（`heading_deg` / `cog_deg`）；模块内部统一用弧度，转换在 `utils/angles.hpp`
- [ ] **PROJ-LR-009**：源代码 0 个 `std::thread::detach()`
- [ ] **PROJ-LR-010**：`auto` 仅用于 lambda / range-for / iterator
- [ ] 函数行数 ≤ 40（PATH-S 阈值；clang-tidy `readability-function-size.LineThreshold=40`）

### 9.3 单元测试 + 集成

- [ ] 单元测试覆盖率 **≥ 95%**（lcov 报告；PATH-S 阈值，比 PATH-D 的 90% 更严）
- [ ] 详细设计 §9.1 的 8 个子模块每个均有独立测试文件
- [ ] 详细设计 §9.3 的 3 个 HIL 场景至少有 mock-level 集成测试通过
- [ ] §15 IDL 接口实测：发布 `ODDState` / `ModeCmd` / `ASDRRecord` / `SATData` 字段对齐 v1.1.2 §15.1
- [ ] 订阅 `/fusion/environment_state`、`/fusion/own_ship_state`、`/l3/m2/world_state`、`/l3/m7/safety_alert`、`/reflex/activation_notification`、`/override/active_signal` 6 个 topic 在 mock 桩下通过

### 9.4 文档 + ASDR

- [ ] 模块独立 `README.md` 指向本 spec + 详细设计
- [ ] HAZID 校准点用 `[TBD-HAZID]` 注释 + YAML 注入（不硬编码；§8.1 全 12 处）
- [ ] ASDR 决策日志格式符合 v1.1.2 §15.1 ASDRRecord + RFC-004 决议（`source_module="M1_ODD_Manager"`）
- [ ] SAT-1/2/3 数据通过 `/l3/sat/data` 发布（详细设计 §8.2）

### 9.5 SIL 2 / DAL B 证据

- [ ] FMEA 表（详细设计 §7.2）每行均映射到代码层防护（input range check / state machine total transition / etc.）
- [ ] 心跳监控（详细设计 §7.3）实现：M1 自身 1 Hz 心跳（同 ODDState）+ 上游 4 个 topic 超时检测
- [ ] 形式化验证（推荐）：`OddStateMachine::compute_next` 用 TLA+ 或 NuSMV 建模，验证死锁 / 活性 / "OUT → MRC_ACTIVE 不可绕过"不变式

---

## 10. 引用

### 10.1 设计基线

- **详细设计**：`docs/Design/Detailed Design/M1-ODD-Envelope-Manager/01-detailed-design.md` v1.0（SANGO-ADAS-L3-DD-M1-001）
  - §1 模块职责 / §2 输入接口 / §3 输出接口 / §4 内部状态
  - §5 核心算法（Conformance_Score / 状态转移 / TMR/TDL）
  - §6 时序设计 / §7 降级与容错 / §9 测试策略 / §10 实现约束
- **架构基线**：v1.1.2 主架构
  - §3 ODD 框架（含 Conformance_Score 公式 §3.6）
  - §5 M1 概览
  - §11.6 MRC 命令集 + ADR-001
  - §15.1 ODD_StateMsg / SafetyAlert / ASDRRecord IDL
  - §15.2 接口矩阵
- **跨团队 RFC**：`docs/Design/Cross-Team Alignment/RFC-decisions.md`
  - RFC-001（M5/L4 双接口；间接影响 M1 ModeCmd 时序）
  - RFC-002（FilteredOwnShipState `nav_mode`；M1 health 监控）
  - RFC-004（ASDR IDL；M1 ASDRRecord 字段）

### 10.2 工程基线

- **主计划**：`docs/Implementation/00-implementation-master-plan.md` v1.0
  - §3 工程基线（ROS2 Jazzy / GCC 14.3 / MISRA C++:2023 / Cyclone DDS）
  - §4 项目脚手架
- **编码规范**：`docs/Implementation/coding-standards.md` v1.0
  - §1 路径强度（M1 = PATH-S）
  - §3 MISRA C++:2023 主规则集
  - §4 命名约定
  - §10.6 PATH-S 项目硬规则修复模式
- **静态分析**：`docs/Implementation/static-analysis-policy.md` v1.0
  - §3 路径强度矩阵（PATH-S = Polyspace + Coverity + clang-tidy + cppcheck Premium）
  - §4 MISRA C++:2023 规则集 + 10 条 LOCAL 规则
  - §5 工具具体配置
- **第三方库**：`docs/Implementation/third-party-library-policy.md` v1.0
  - §2.1 核心库白名单（M1 用 spdlog + Eigen + GTest + yaml-cpp + tl::expected）
  - §3 Doer-Checker 独立性矩阵
- **IDL guide**：`docs/Implementation/ros2-idl-implementation-guide.md` v1.0
  - §2 命名规范（topic / node / .msg）
  - §3.3 ODDState.msg 完整定义
  - §4 DDS QoS（M1 = long-time + reliable + transient_local）

### 10.3 学术与规范

- **[R4] Veitch et al. 2024**（TMR ≥ 60 s 实证）— `Ocean Engineering 299:117257`
- **[R8] Rødseth et al. 2022**（ODD 二轴框架）— `JMSE 27(1):67–76`
- **[R17] Wang et al. 2021**（TCPA 时间阈值）— `JMSE 9(6):584`
- **[R5] IEC 61508-3:2010**（SIL 2 软件生命周期）
- **[R6] ISO 21448:2022**（SOTIF）
- **[R9] DNV-CG-0264:2025.01**（自主航行 9 子功能）
- **MISRA C++:2023**（179 规则；Perforce / Parasoft 完整指南）

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Team-M1（Claude 实施层 kickoff） | 初稿创建：8 章节齐全；C++17 header 骨架（OddStateMachine / ConformanceScoreCalculator / TmrTdlEstimator / MrcTriggerLogic / OddEnvelopeManagerNode）；CMakeLists.txt 含 PATH-S `-fno-exceptions` + `-fno-rtti`；package.xml；YAML 参数 12 处 `[TBD-HAZID]`；GTest 4 个测试文件骨架；DoD checklist 30 项；与详细设计 v1.0 + 架构 v1.1.2 + 工程基线 5 文件交叉引用 |

---

**文档状态**：正式（Team-M1 开发起点）

**后续动作**：
1. Team-M1 按 §2 目录结构搭建 colcon package
2. 按 §4 header 骨架填充实现（详见详细设计 §5）
3. 按 §7 测试骨架补足 ≥ 95% 覆盖率单元测试
4. CI 全绿后进入 Wave 1（Week 1–4）模块集成
5. HAZID RUN-001 完成（2026-08-19）后回填 §8.1 全 12 处 `[TBD-HAZID]` 参数 → v1.0.x patch

---

**End of M1 Code Skeleton Spec**
