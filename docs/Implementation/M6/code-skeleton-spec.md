# M6 COLREGs Reasoner 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M6-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M6 编码起点） |
| 路径强度 | **PATH-D（决策路径）** |
| 模块 | M6 COLREGs Reasoner |
| 团队 | Team-M6 |
| 主语言 | C++17（GCC 14.3 LTS） |
| 详细设计基线 | `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md`（SANGO-ADAS-L3-DD-M6-001 v1.0） |
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §9 |
| 实施主计划 | `docs/Implementation/00-implementation-master-plan.md` v1.0 |
| 编码规范 | `docs/Implementation/coding-standards.md` v1.0（MISRA C++:2023 完整 179 规则） |
| IDL 指南 | `docs/Implementation/ros2-idl-implementation-guide.md` §3.3 COLREGsConstraint + RuleActive |
| 第三方库政策 | `docs/Implementation/third-party-library-policy.md` v1.0 |
| 时间尺度 | 中时（2 Hz 输出 COLREGsConstraint） |
| SIL 等级 | SIL 2（推理核心）+ SIL 1（接口校验）— 详见详细设计 §10.3 |
| 引用来源 | [R17] Wang 2021 / [R18] COLREGs 1972 / [R19] Bitar 2019 / [R7] Yasukawa 2015 |

> **本 spec 是 Team-M6 编码工作的起点。** 任何对 §2 项目结构、§4 类设计、§7 测试的改动须走 PR + reviewer 双签；任何对接口（§3 ROS2 节点设计）的改动还须同步更新 v1.1.2 §15.1（架构主文件）+ `ros2-idl-implementation-guide.md`。

---

## 1. 模块概述

### 1.1 模块职责（一句话）

M6 是 L3 战术层中 **COLREGs 1972 Rule 5–19 的独立白盒推理引擎**：根据 M2 World_State 的目标几何状态 + M1 ODD_State 的当前域，推断每个目标对应的 COLREGs 规则、本船角色（Give-Way / Stand-On / Free）、行动方向与最小转向幅度、以及 Rule 17 三阶段时机（PRESERVE_COURSE / SOUND_WARNING / INDEPENDENT_ACTION），并向 M5 Tactical Planner 输出**约束集**（不输出轨迹）。

### 1.2 与详细设计的对应

| 详细设计章节 | 本骨架对应 |
|---|---|
| §2 输入接口 | §3.2 订阅者表格 |
| §3 输出接口 | §3.3 发布者表格 |
| §4 内部状态 | §4.6 ColregsReasonerNode 类成员 |
| §5.1 五层算法 | §4.2 实现类（每条 Rule 一个类） |
| §5.3 ODD-aware 参数表 | §8.2 odd_aware_thresholds.yaml |
| §5.3 规则库插件接口 | §4.5 RuleLibraryLoader |
| §6 时序设计（500 ms 周期）| §4.6 节点 timer + §3.4 参数 |
| §7 降级与容错 | §4.6 节点 health_check_timer |
| §9.1 单元测试 | §7 单元测试骨架（30+ 用例）|
| §10.4 第三方库约束 | §2.2 + 引用第三方库政策 §3 |

### 1.3 PATH-D 路径强度要点

- **MISRA C++:2023 完整 179 规则强制**（CI 阻断）
- 静态分析：clang-tidy 20.1.8 + cppcheck Premium 26.1.0 全启用
- 单元测试覆盖率 ≥ 90%
- 函数 ≤ 60 行；循环复杂度 ≤ 10
- AddressSanitizer + ThreadSanitizer + UBSan 0 错误
- 所有 [TBD-HAZID] 阈值通过 YAML 注入，不硬编码到 .cpp / .hpp

### 1.4 与其他模块的关系

```
                ┌─────────────┐
                │ M1 ODD Mgr  │ 1 Hz odd_state
                └──────┬──────┘
                       │
                       v
┌─────────────┐  4 Hz world_state  ┌─────────────┐  2 Hz colregs_constraint  ┌─────────────┐
│ M2 World    ├───────────────────>│ M6 COLREGs  ├──────────────────────────>│ M5 Tactical │
│ Model       │                    │ Reasoner    │                          │ Planner     │
└─────────────┘                    └──────┬──────┘                          └─────────────┘
                                          │
                                          v
                                    ┌──────────────┐
                                    │ ASDR / M8    │ 事件 + 2 Hz
                                    └──────────────┘
```

---

## 2. 项目目录结构

### 2.1 colcon package 布局

```
mass_l3_tdl/src/m6_colregs_reasoner/
├── package.xml
├── CMakeLists.txt
├── README.md                                  # 模块说明 + 启动方法
│
├── include/m6_colregs_reasoner/
│   ├── colregs_reasoner_node.hpp              # ROS2 节点主类
│   ├── colregs_constraint_generator.hpp       # 约束转换器
│   ├── colregs_phase_classifier.hpp           # T_standOn / T_act / T_postAvoid 阶段
│   ├── rule_library_loader.hpp                # COLREGs ↔ CEVNI 切换（Capability Manifest）
│   ├── target_state_cache.hpp                 # 目标状态缓存（per-target tracking）
│   ├── geometry_utils.hpp                     # bearing / aspect / cpa 几何工具（Eigen + Boost.Geometry 包装）
│   ├── error_codes.hpp                        # M6 错误码（6000–6999）
│   ├── types.hpp                              # M6 私有类型（Role / Phase / EncounterType）
│   │
│   └── rules/
│       ├── colregs_rule_base.hpp              # 抽象规则基类（插件接口）
│       ├── colregs/                           # COLREGs 1972 默认规则库
│       │   ├── rule5_lookout.hpp
│       │   ├── rule6_safe_speed.hpp
│       │   ├── rule7_risk_of_collision.hpp
│       │   ├── rule8_action_to_avoid.hpp
│       │   ├── rule13_overtaking.hpp
│       │   ├── rule14_head_on.hpp
│       │   ├── rule15_crossing.hpp
│       │   ├── rule16_give_way.hpp
│       │   ├── rule17_stand_on.hpp
│       │   ├── rule18_responsibilities.hpp
│       │   └── rule19_restricted_visibility.hpp
│       │
│       └── cevni/                             # 内河船规则库（v1.x 扩展，v1.0 留空 stub）
│           ├── README.md                      # 内河船规则库使用说明
│           ├── cevni_overtaking.hpp           # CEVNI 追越规则（stub）
│           ├── cevni_crossing.hpp             # CEVNI 交叉规则（stub）
│           └── ...                            # 其他 CEVNI 规则
│
├── src/
│   ├── colregs_reasoner_node.cpp
│   ├── colregs_constraint_generator.cpp
│   ├── colregs_phase_classifier.cpp
│   ├── rule_library_loader.cpp
│   ├── target_state_cache.cpp
│   ├── geometry_utils.cpp
│   │
│   ├── rules/
│   │   ├── colregs/
│   │   │   ├── rule5_lookout.cpp
│   │   │   ├── rule6_safe_speed.cpp
│   │   │   ├── rule7_risk_of_collision.cpp
│   │   │   ├── rule8_action_to_avoid.cpp
│   │   │   ├── rule13_overtaking.cpp
│   │   │   ├── rule14_head_on.cpp
│   │   │   ├── rule15_crossing.cpp
│   │   │   ├── rule16_give_way.cpp
│   │   │   ├── rule17_stand_on.cpp
│   │   │   ├── rule18_responsibilities.cpp
│   │   │   └── rule19_restricted_visibility.cpp
│   │   │
│   │   └── cevni/                             # v1.0 仅占位 stub
│   │       └── cevni_stub.cpp
│   │
│   └── main.cpp                               # ROS2 entrypoint
│
├── config/
│   ├── m6_params.yaml                         # 通用参数（确定值）
│   ├── odd_aware_thresholds.yaml              # ODD-A/B/C/D 时机参数（[TBD-HAZID]）
│   ├── colregs_rule_library.yaml              # COLREGs 1972 默认（capability manifest）
│   └── cevni_rule_library.yaml                # 内河船备选（stub）
│
├── launch/
│   ├── m6_standalone.launch.py                # 独立启动 + mock 上游
│   └── m6_with_full_l3.launch.py              # 接入完整 L3 场景
│
└── test/
    ├── unit/
    │   ├── test_rule5_lookout.cpp             # ≥ 5 用例
    │   ├── test_rule6_safe_speed.cpp          # ≥ 5 用例
    │   ├── test_rule7_risk_of_collision.cpp   # ≥ 5 用例
    │   ├── test_rule8_action_to_avoid.cpp     # ≥ 5 用例
    │   ├── test_rule13_overtaking.cpp         # ≥ 5 用例
    │   ├── test_rule14_head_on.cpp            # ≥ 5 用例
    │   ├── test_rule15_crossing.cpp           # ≥ 5 用例
    │   ├── test_rule16_give_way.cpp           # ≥ 3 用例
    │   ├── test_rule17_stand_on.cpp           # ≥ 5 用例（含 T_standOn / T_act 阶段判别）
    │   ├── test_rule18_responsibilities.cpp   # ≥ 3 用例
    │   ├── test_rule19_restricted_visibility.cpp  # ≥ 3 用例
    │   ├── test_phase_classifier.cpp          # ≥ 6 用例（CPA/TCPA → phase）
    │   ├── test_constraint_generator.cpp      # ≥ 4 用例（多目标多 Rule 同时活跃）
    │   ├── test_rule_library_loader.cpp       # ≥ 4 用例（COLREGs ↔ CEVNI 切换）
    │   ├── test_odd_aware_thresholds.cpp      # ≥ 4 用例（ODD-A 宽 vs ODD-B 严格）
    │   └── test_geometry_utils.cpp            # ≥ 5 用例（bearing / aspect / cpa 几何工具）
    │
    └── integration/
        ├── test_node_lifecycle.cpp            # 节点启动 / 关闭 / 参数热加载
        └── test_msg_pipeline.cpp              # 端到端 World_State → COLREGsConstraint
```

### 2.2 第三方库依赖（决策路径白名单）

引用 `third-party-library-policy.md` §2.1 + §2.2，M6 允许使用：

| 库 | 版本 | 用途 |
|---|---|---|
| **Eigen** | 5.0.0 | 向量与矩阵运算（bearing / aspect / cpa 几何） |
| **Boost.Geometry**（仅头文件子集）| 1.91.0 | TSS lane 多边形 / 夹角运算（Rule 9 / Rule 10 几何检查） |
| **spdlog** | 1.17.0 | 日志（logger 名 `m6_colregs`） |
| **fmt** | 11.0.x | 格式化（spdlog 内嵌） |
| **yaml-cpp** | 0.8.0 | YAML 配置加载（odd_aware_thresholds.yaml / colregs_rule_library.yaml） |
| **GTest / GMock** | 1.17.0 | 单元测试 |
| **rclcpp** | 系统（ROS2 Jazzy） | ROS2 框架 |
| **l3_msgs** | 项目内 | 共享消息（COLREGsConstraint / WorldState / ODDState / ASDRRecord / SATData） |

**禁用**（详见详细设计 §10.4）：
- 通用规则引擎（Drools / CLIPS / JESS / Prolog）— 违反 Doer-Checker 独立性
- ML 框架（TensorFlow / PyTorch / ONNX）— 违反白盒可审计要求
- ROS2 navigation2 规则库 — 应用领域不同
- nlohmann::json — M6 不直接序列化 JSON（ASDRRecord 由节点公共 helper 处理；如需则改为 yaml-cpp 或下放到通用工具）

> **注**：实际编码时 `decision_json` 字段如需 JSON 序列化，可在 `colregs_reasoner_node.cpp` 通过 `l3_msgs/ASDRRecord` 提供的 `string decision_json` 字段直接拼接（M6 内部不引 nlohmann::json，简化依赖）。

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

```
            ┌─────────────────────────────────┐
            │   m6_colregs_reasoner (Node)    │
            │                                 │
            │   500 ms reasoning timer (2 Hz) │
            │   1 s health check timer        │
            │   1 s asdr snapshot timer       │
            └─────────────────────────────────┘
                ↑   ↑                ↓   ↓   ↓
       ┌────────┘   │                │   │   └────────┐
       │            │                │   │            │
   ┌───┴───┐   ┌────┴────┐      ┌────┴───┴─────┐  ┌──┴──┐
   │ /l3/  │   │ /l3/    │      │ /l3/m6/      │  │/l3/ │
   │ m1/   │   │ m2/     │      │ colregs_     │  │asdr/│
   │ odd_  │   │ world_  │      │ constraint   │  │record│
   │ state │   │ state   │      │ (2 Hz)       │  │     │
   └───────┘   └─────────┘      └──────────────┘  └─────┘
                                       ↓
                                   ┌──────────┐
                                   │ /l3/sat/ │
                                   │ data     │
                                   │ (10 Hz)  │
                                   └──────────┘
```

**节点单一进程**（不与 M7 / M5 复合编译）；DDS QoS 见 §3.5。

### 3.2 订阅者表格

| Topic | 类型 | QoS | 频率 | 来源 | 处理函数 | 容错 |
|---|---|---|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | `QoS(10).reliable().transient_local()` | 1 Hz + 事件 | M1 | `on_odd_state(msg)` | 超时 > 10 s → ODD_UNKNOWN（最保守参数）|
| `/l3/m2/world_state` | `l3_msgs/WorldState` | `SensorDataQoS().keep_last(2)` | 4 Hz | M2 | `on_world_state(msg)` | 超时 > 5 s → confidence = 0.5；> 10 s → confidence = 0 |

```cpp
// 订阅声明（colregs_reasoner_node.cpp::create_subscribers）
odd_sub_ = create_subscription<l3_msgs::msg::ODDState>(
    "/l3/m1/odd_state",
    rclcpp::QoS(10).reliable().transient_local(),
    [this](const l3_msgs::msg::ODDState::ConstSharedPtr msg) {
      on_odd_state(*msg);
    });

world_sub_ = create_subscription<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state",
    rclcpp::SensorDataQoS().keep_last(2),
    [this](const l3_msgs::msg::WorldState::ConstSharedPtr msg) {
      on_world_state(*msg);
    });
```

### 3.3 发布者表格

| Topic | 类型 | QoS | 频率 | 订阅者 | 触发 | SLA |
|---|---|---|---|---|---|---|
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | `QoS(5).reliable()` | 2 Hz（500 ms 周期）| M5 | reasoning_timer | 端到端延迟 ≤ 250 ms；可用率 ≥ 99.5% (ODD-A/B/C) / ≥ 98% (ODD-D) |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | `QoS(50).reliable().transient_local()` | 事件 + 2 Hz 快照 | ASDR | rule_change / role_change / phase_transition / odd_switch / 1 s timer | 事件触发 < 100 ms |
| `/l3/sat/data` | `l3_msgs/SATData` | `SensorDataQoS().keep_last(2)` | 10 Hz（SAT-1 持续）+ 事件（SAT-2 推理）| M8 | sat_timer + 触发型 | SAT-1: 10 Hz；SAT-2: 变化或 confidence < 0.8 |

```cpp
// 发布声明（colregs_reasoner_node.cpp::create_publishers）
constraint_pub_ = create_publisher<l3_msgs::msg::COLREGsConstraint>(
    "/l3/m6/colregs_constraint",
    rclcpp::QoS(5).reliable());

asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
    "/l3/asdr/record",
    rclcpp::QoS(50).reliable().transient_local());

sat_pub_ = create_publisher<l3_msgs::msg::SATData>(
    "/l3/sat/data",
    rclcpp::SensorDataQoS().keep_last(2));
```

### 3.4 节点参数（YAML）

`config/m6_params.yaml` 顶层参数（所有 [TBD-HAZID] 在 §8.2 单独维护，本节仅列固定值）：

```yaml
m6_colregs_reasoner:
  ros__parameters:
    # === 节点周期 ===
    reasoning_period_ms: 500          # 推理周期（2 Hz）— 详细设计 §6.1
    health_check_period_ms: 1000      # 自检周期
    asdr_snapshot_period_ms: 500      # 2 Hz 快照写入
    sat_publish_period_ms: 100        # SAT-1 10 Hz

    # === 超时阈值 ===
    world_state_timeout_s: 5.0        # 详细设计 §2.2
    odd_state_timeout_s: 10.0
    rule_library_load_timeout_s: 5.0  # 启动加载超时

    # === 规则库 ===
    rule_library_path: "config/colregs_rule_library.yaml"   # 默认 COLREGs 1972
    rule_library_validate_against_colregs: true
    rule_library_fallback_to_default: true                  # 加载失败回退

    # === 路径 / 调试 ===
    odd_thresholds_path: "config/odd_aware_thresholds.yaml"
    log_level: "info"                  # spdlog level
    enable_sat2_on_low_confidence: true
    sat2_confidence_threshold: 0.8

    # === 性能上限 ===
    max_targets: 50                    # 详细设计 §5.4 设计上限
    wcet_budget_ms: 50                 # 推理预算
```

### 3.5 DDS QoS 设计依据

引用 `ros2-idl-implementation-guide.md` §4.1：

- 订阅 `/l3/m2/world_state`（短时 4 Hz）：`SensorDataQoS()` — 实时优先，允许丢包
- 订阅 `/l3/m1/odd_state`（长时 1 Hz）：`reliable + transient_local` — 后启动节点能拿到最新状态
- 发布 `/l3/m6/colregs_constraint`（中时 2 Hz）：`reliable + volatile` — 不允许丢约束，但不留旧
- 发布 `/l3/asdr/record`（事件 + transient_local）：保证 ASDR 后启动也能取齐审计快照

---

## 4. 核心类定义（C++ header 骨架）

### 4.1 ColregsRuleBase（规则插件接口 — Capability Manifest 切换点）

`include/m6_colregs_reasoner/rules/colregs_rule_base.hpp`：

```cpp
#pragma once

#include <Eigen/Core>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules {

/**
 * @brief 单个 COLREGs / CEVNI 规则的抽象基类（Capability Manifest 插件接口）
 *
 * 每条 Rule 实现一个派生类。RuleLibraryLoader 在启动时根据
 * Capability Manifest 选择 colregs/ 或 cevni/ 子目录的实现。
 *
 * Per 详细设计 §5.1 + §5.3 规则库插件接口；架构 v1.1.2 §13.5 多船型扩展.
 *
 * INVARIANT: 所有派生类的 evaluate() 必须是无副作用 + 线程安全（const 方法）.
 */
class ColregsRuleBase {
public:
  virtual ~ColregsRuleBase() = default;

  /**
   * @brief 规则编号（COLREGs Rule X / CEVNI 对应 Article X）.
   */
  virtual std::int32_t rule_id() const noexcept = 0;

  /**
   * @brief 规则名称（"Rule13_Overtaking" / "CEVNI_Article6.04" 等）.
   */
  virtual std::string rule_name() const noexcept = 0;

  /**
   * @brief 检查规则是否在当前 ODD 与目标几何下可激活.
   *
   * @param geo  目标几何状态（来自 M2 World_State + M2 EncounterClassification）
   * @param odd  当前 ODD 子域
   * @param params  ODD-aware 时机参数（来自 odd_aware_thresholds.yaml）
   *
   * @return RuleEvaluation { is_active, role, encounter_type, confidence, rationale }
   */
  virtual RuleEvaluation evaluate(
      const TargetGeometricState& geo,
      OddDomain odd,
      const RuleParameters& params) const = 0;

protected:
  ColregsRuleBase() = default;
  ColregsRuleBase(const ColregsRuleBase&) = default;
  ColregsRuleBase(ColregsRuleBase&&) = default;
  ColregsRuleBase& operator=(const ColregsRuleBase&) = default;
  ColregsRuleBase& operator=(ColregsRuleBase&&) = default;
};

/**
 * @brief 规则工厂——RuleLibraryLoader 内部使用.
 */
using RulePtr = std::unique_ptr<ColregsRuleBase>;
using RuleSet = std::vector<RulePtr>;

}  // namespace mass_l3::m6_colregs::rules
```

`types.hpp` 关键定义：

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace mass_l3::m6_colregs {

enum class OddDomain : std::uint8_t {
  ODD_A = 0,   // 开阔水域
  ODD_B = 1,   // 狭水道 / VTS
  ODD_C = 2,   // 港内 / 靠泊
  ODD_D = 3,   // 能见度不良
  ODD_UNKNOWN = 255  // 最保守模式
};

enum class Role : std::uint8_t {
  STAND_ON = 0,
  GIVE_WAY = 1,
  BOTH_GIVE_WAY = 2,   // Rule 14 对遇双方
  FREE = 3
};

enum class EncounterType : std::uint8_t {
  NONE = 0,
  HEAD_ON = 1,         // Rule 14
  OVERTAKING = 2,      // Rule 13
  CROSSING = 3,        // Rule 15
  RESTRICTED_VIS = 4,  // Rule 19
  AMBIGUOUS = 5
};

enum class TimingPhase : std::uint8_t {
  PRESERVE_COURSE = 0,    // tcpa > T_standOn
  SOUND_WARNING = 1,      // T_standOn ≥ tcpa > T_act
  INDEPENDENT_ACTION = 2, // tcpa ≤ T_act
  CRITICAL_ACTION = 3     // tcpa ≤ T_emergency（让路船专属）
};

struct TargetGeometricState {
  std::uint64_t target_id;
  double bearing_deg;          // [0, 360)
  double aspect_deg;           // [0, 360)
  double relative_speed_kn;    // 可负
  double cpa_m;                // ≥ 0
  double tcpa_s;               // ≥ 0 或 ∞
  double ownship_heading_deg;
  double ownship_speed_kn;
  std::int32_t target_ship_type_priority;  // Rule 18 优先序，详见详细设计 §5.1 第三层
  std::chrono::system_clock::time_point stamp;
};

struct RuleParameters {
  // ODD-aware（详细设计 §5.3 表格）
  double t_standOn_s;          // [TBD-HAZID]
  double t_act_s;              // [TBD-HAZID]
  double t_emergency_s;        // [TBD-HAZID]
  double min_alteration_deg;   // [TBD-HAZID]
  double cpa_safe_m;           // [TBD-HAZID]
  double max_turn_rate_deg_s;  // 来自 Capability Manifest（FCB 4-DOF MMG）
  double rule_9_weight;        // ODD-B/C 用
};

struct RuleEvaluation {
  bool is_active;
  std::int32_t rule_id;
  EncounterType encounter_type;
  Role role;
  TimingPhase phase;
  double min_alteration_deg;
  std::string preferred_direction;  // "STARBOARD" | "PORT" | "REDUCE_SPEED" | "HOLD"
  float confidence;                  // [0, 1]
  std::string rationale;             // SAT-2 摘要
};

}  // namespace mass_l3::m6_colregs
```

### 4.2 实现类（Rule 5–19）

每条 Rule 继承 `ColregsRuleBase`。下文为代表性 header / cpp 骨架，其他 Rule 类同模式。

#### 4.2.1 Rule5_LookOut（瞭望 — 基础规则）

`rules/colregs/rule5_lookout.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 5: 瞭望（Look-Out）— 基础义务，Rule 不会激活成约束，
 *        但若上游 Multimodal Fusion confidence 过低，本规则记录到 ASDR.
 *
 * Per [R18] COLREGs Rule 5 + 详细设计 §5.1 第一层 applicable_rules 必含 Rule 5.
 */
class Rule5LookOut final : public ColregsRuleBase {
 public:
  Rule5LookOut() = default;

  std::int32_t rule_id() const noexcept override { return 5; }
  std::string rule_name() const noexcept override { return "Rule5_LookOut"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.2 Rule6_SafeSpeed（安全速度）

`rules/colregs/rule6_safe_speed.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 6: Safe Speed — 检查本船速度是否与能见度 / 交通密度 / 操纵性相称.
 *        激活约束："REDUCE_SPEED" 当 ODD-D 或交通密度 > threshold.
 *
 * Per [R18] COLREGs Rule 6 + 详细设计 §5.3 ODD-D 保守减速.
 */
class Rule6SafeSpeed final : public ColregsRuleBase {
 public:
  Rule6SafeSpeed() = default;

  std::int32_t rule_id() const noexcept override { return 6; }
  std::string rule_name() const noexcept override { return "Rule6_SafeSpeed"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.3 Rule7_RiskOfCollision

`rules/colregs/rule7_risk_of_collision.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 7: 碰撞危险判定 — 基于 CPA / TCPA 阈值.
 *        若 cpa < cpa_safe AND tcpa > 0 AND tcpa < ∞ → has_risk = true.
 *
 * Per [R18] COLREGs Rule 7 + 详细设计 §5.1 第二层 cpa < params.cpa_safe.
 */
class Rule7RiskOfCollision final : public ColregsRuleBase {
 public:
  Rule7RiskOfCollision() = default;

  std::int32_t rule_id() const noexcept override { return 7; }
  std::string rule_name() const noexcept override { return "Rule7_RiskOfCollision"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.4 Rule8_ActionToAvoidCollision（让路船行动）

`rules/colregs/rule8_action_to_avoid.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 8: 避碰行动 — "尽早采取，大幅转向" (large alteration).
 *
 * 工业量化 [R17 Wang 2021]：
 *  - min_alteration_deg = 30° (ODD-A) / 20° (ODD-B) / 15° (ODD-C) / 30° (ODD-D)
 *  - preferred_direction = "STARBOARD"
 *  - 严禁小幅转向 / 严禁对称转向
 *
 * Per [R18] COLREGs Rule 8 + 详细设计 §5.1 第四层 + §5.3.
 */
class Rule8ActionToAvoid final : public ColregsRuleBase {
 public:
  Rule8ActionToAvoid() = default;

  std::int32_t rule_id() const noexcept override { return 8; }
  std::string rule_name() const noexcept override { return "Rule8_ActionToAvoid"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.5 Rule13_Overtaking

`rules/colregs/rule13_overtaking.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 13: 追越 — 追越船总是让路船.
 *
 * 几何判定（详细设计 §5.1 第二层）:
 *   bearing ∈ [112.5°, 247.5°)  AND
 *   |aspect| ≤ 30°               AND
 *   relative_speed < 0
 *
 * Per [R18] COLREGs Rule 13.
 */
class Rule13Overtaking final : public ColregsRuleBase {
 public:
  Rule13Overtaking() = default;

  std::int32_t rule_id() const noexcept override { return 13; }
  std::string rule_name() const noexcept override { return "Rule13_Overtaking"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

`rules/colregs/rule13_overtaking.cpp`（关键骨架）：

```cpp
#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"

#include <cmath>

#include "m6_colregs_reasoner/geometry_utils.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

namespace {
// Reference: [R17] Wang 2021 ±30° aspect tolerance for overtaking classification.
constexpr double kOvertakingBearingMinDeg = 112.5;
constexpr double kOvertakingBearingMaxDeg = 247.5;
constexpr double kOvertakingAspectToleranceDeg = 30.0;
}  // namespace

RuleEvaluation Rule13Overtaking::evaluate(
    const TargetGeometricState& geo,
    OddDomain /*odd*/,
    const RuleParameters& params) const {
  RuleEvaluation eval{};
  eval.rule_id = rule_id();
  eval.is_active = false;
  eval.confidence = 1.0F;

  if (geo.relative_speed_kn >= 0.0) {
    eval.rationale = "Rule13_Overtaking: relative_speed >= 0, no overtaking";
    return eval;
  }

  const bool bearing_in_stern_arc =
      (geo.bearing_deg >= kOvertakingBearingMinDeg) &&
      (geo.bearing_deg < kOvertakingBearingMaxDeg);
  const bool aspect_aligned =
      std::abs(geometry::wrap_to_180_deg(geo.aspect_deg)) <=
      kOvertakingAspectToleranceDeg;

  if (bearing_in_stern_arc && aspect_aligned) {
    eval.is_active = true;
    eval.encounter_type = EncounterType::OVERTAKING;
    eval.role = Role::GIVE_WAY;
    eval.min_alteration_deg = params.min_alteration_deg;
    eval.preferred_direction = "STARBOARD";
    eval.rationale =
        "Rule13_Overtaking: bearing in [112.5,247.5), |aspect|<=30, rel_speed<0";
  } else {
    eval.rationale = "Rule13_Overtaking: geometric criteria not met";
  }
  return eval;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.6 Rule14_HeadOn

`rules/colregs/rule14_head_on.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 14: 对遇 — 双方各自向右转 (BOTH_GIVE_WAY).
 *
 * 几何判定（详细设计 §5.1 第二层）:
 *   |bearing - 180°| ≤ 6°    AND
 *   |aspect - 180°| ≤ 10°    AND
 *   relative_speed < 0
 *
 * Per [R18] COLREGs Rule 14.
 */
class Rule14HeadOn final : public ColregsRuleBase {
 public:
  Rule14HeadOn() = default;

  std::int32_t rule_id() const noexcept override { return 14; }
  std::string rule_name() const noexcept override { return "Rule14_HeadOn"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.7 Rule15_Crossing

`rules/colregs/rule15_crossing.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 15: 交叉相遇 — 右舷船优先 (有目标在右舷的本船 GIVE_WAY).
 *
 * 几何判定（详细设计 §5.1 第三层）:
 *   非 Rule 13/14    AND
 *   cpa < cpa_safe   AND
 *   tcpa > 0
 *   IF bearing ∈ [0°, 180°]  → ownship GIVE_WAY
 *   ELSE                     → ownship STAND_ON
 *
 * Per [R18] COLREGs Rule 15.
 */
class Rule15Crossing final : public ColregsRuleBase {
 public:
  Rule15Crossing() = default;

  std::int32_t rule_id() const noexcept override { return 15; }
  std::string rule_name() const noexcept override { return "Rule15_Crossing"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.8 Rule16_GiveWayShip

`rules/colregs/rule16_give_way.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 16: 让路船——必须及早采取大幅、明显行动 (early & substantial action).
 *
 * 此规则不独立分类目标，而是在已被 Rule 13/14/15 判定为 GIVE_WAY 的目标上
 * 叠加 "minimum alteration / preferred direction" 约束.
 *
 * Per [R18] COLREGs Rule 16 + 详细设计 §5.1 第四层.
 */
class Rule16GiveWay final : public ColregsRuleBase {
 public:
  Rule16GiveWay() = default;

  std::int32_t rule_id() const noexcept override { return 16; }
  std::string rule_name() const noexcept override { return "Rule16_GiveWay"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.9 Rule17_StandOnShip（含 T_standOn / T_act 阶段判别）

`rules/colregs/rule17_stand_on.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 17: 直航船三阶段时机模型.
 *
 * Phase 1 (PRESERVE_COURSE):  tcpa > T_standOn          → 保向保速
 * Phase 2 (SOUND_WARNING):    T_standOn ≥ tcpa > T_act  → 发声号 + 准备行动
 * Phase 3 (INDEPENDENT_ACTION): tcpa ≤ T_act           → 直航船独立避让
 *
 * Reference [R17] Wang 2021: 直航船三阶段实验验证模型.
 * Per [R18] COLREGs Rule 17 + 详细设计 §5.1 第五层 + §5.3.
 */
class Rule17StandOn final : public ColregsRuleBase {
 public:
  Rule17StandOn() = default;

  std::int32_t rule_id() const noexcept override { return 17; }
  std::string rule_name() const noexcept override { return "Rule17_StandOn"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.10 Rule18_ResponsibilitiesBetweenVessels

`rules/colregs/rule18_responsibilities.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 18: 船舶之间的责任 — 优先序仲裁.
 *
 * 优先序（[R18] COLREGs Rule 18）:
 *   1. NotUnderCommand (失控船)
 *   2. RestrictedManeuverability (操纵能力受限)
 *   3. ConstrainedByDraft (限制吃水)
 *   4. Fishing (渔船)
 *   5. Sailing (帆船)
 *   6. PowerDriven (机动船，本船基础假设)
 *
 * 当 target_ship_type_priority < ownship 时，本船须 GIVE_WAY (Rule 18 优先序).
 *
 * Per 详细设计 §5.1 第三层尾段 + Capability Manifest.
 */
class Rule18Responsibilities final : public ColregsRuleBase {
 public:
  Rule18Responsibilities() = default;

  std::int32_t rule_id() const noexcept override { return 18; }
  std::string rule_name() const noexcept override { return "Rule18_Responsibilities"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

#### 4.2.11 Rule19_ConductInRestrictedVisibility（ODD-D 触发）

`rules/colregs/rule19_restricted_visibility.hpp`：

```cpp
#pragma once
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

/**
 * @brief Rule 19: 能见度不良条件下的行动 — 仅在 ODD-D 激活.
 *        Rule 13–17 全部被本规则取代（声号驾驶 + Rule 8 大幅转向）.
 *
 * Per [R18] COLREGs Rule 19 + 详细设计 §5.1 第一层 ODD-D 切换 + §7.1 OUT_of_ODD.
 */
class Rule19RestrictedVisibility final : public ColregsRuleBase {
 public:
  Rule19RestrictedVisibility() = default;

  std::int32_t rule_id() const noexcept override { return 19; }
  std::string rule_name() const noexcept override { return "Rule19_RestrictedVisibility"; }

  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
```

### 4.3 ColregsPhaseClassifier（T_standOn / T_act / T_postAvoid 阶段）

`include/m6_colregs_reasoner/colregs_phase_classifier.hpp`：

```cpp
#pragma once

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

/**
 * @brief Rule 17 三阶段时机分类器（独立可测试单元）.
 *
 * 抽离规则推理外部，使 ColregsPhaseClassifier 可被
 *   - 单元测试独立调用（不需 ROS2 / 全栈）
 *   - 多个 Rule 共享（e.g., Rule 17 直接使用，Rule 13 在追越 Phase 1 也用）
 *
 * Per 详细设计 §5.1 第五层 + §6.2 Rule 变更事件触发.
 */
class ColregsPhaseClassifier {
 public:
  ColregsPhaseClassifier() = default;

  /**
   * @brief 根据 role + tcpa + 当前 ODD 参数，分类时机阶段.
   *
   * @pre params.t_standOn_s > params.t_act_s > params.t_emergency_s ≥ 0
   * @param role  来自 Rule 13/14/15 的角色判定
   * @param tcpa_s  TCPA（≥ 0）
   * @param params  ODD-aware 阈值（[TBD-HAZID]）
   *
   * @return 阶段 + escalation_signal (是否触发声号)
   */
  TimingPhase classify(Role role,
                       double tcpa_s,
                       const RuleParameters& params) const noexcept;

  /**
   * @brief 是否应触发声号（IEC 60936 5s + 2s 序列）.
   *        仅 Rule 17 SOUND_WARNING / INDEPENDENT_ACTION 阶段.
   */
  bool should_sound_warning(Role role, TimingPhase phase) const noexcept;
};

}  // namespace mass_l3::m6_colregs
```

### 4.4 ColregsConstraintGenerator（Rule → Constraint 转换）

`include/m6_colregs_reasoner/colregs_constraint_generator.hpp`：

```cpp
#pragma once

#include <vector>

#include "l3_msgs/msg/colreg_s_constraint.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

/**
 * @brief 将每个目标的 RuleEvaluation[] 汇聚为 COLREGsConstraint.msg.
 *
 * 处理流程：
 *   1. 对每个 target，遍历 RuleSet 调用 evaluate()
 *   2. 收集 is_active 的 RuleEvaluation
 *   3. 解决冲突（多个 Rule 同时活跃，Rule 18 优先序仲裁）
 *   4. 汇聚为 active_rules[] + constraints[] 输出消息
 *
 * Per 详细设计 §5.2 数据流 + §3.1 输出 SLA.
 */
class ColregsConstraintGenerator {
 public:
  ColregsConstraintGenerator() = default;

  /**
   * @brief 主入口：从 World_State + ODD + RuleSet 生成 COLREGsConstraint.
   *
   * @param world_state  M2 输入
   * @param odd  M1 当前 ODD
   * @param params  ODD-aware 参数
   * @param rules  当前 RuleSet（COLREGs 或 CEVNI）
   * @param phase_classifier  阶段分类器
   *
   * @return 准备发布的 COLREGsConstraint.msg
   */
  l3_msgs::msg::COLREGsConstraint generate(
      const l3_msgs::msg::WorldState& world_state,
      OddDomain odd,
      const RuleParameters& params,
      const rules::RuleSet& rules,
      const ColregsPhaseClassifier& phase_classifier) const;

 private:
  /**
   * @brief Rule 18 优先序仲裁 — 多个 Rule 活跃时选最强.
   */
  RuleEvaluation arbitrate_(const std::vector<RuleEvaluation>& evals) const;
};

}  // namespace mass_l3::m6_colregs
```

### 4.5 RuleLibraryLoader（COLREGs vs CEVNI 切换 — Capability Manifest）

`include/m6_colregs_reasoner/rule_library_loader.hpp`：

```cpp
#pragma once

#include <optional>
#include <string>

#include "m6_colregs_reasoner/error_codes.hpp"
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs {

enum class RuleLibraryType : std::uint8_t {
  COLREGS_1972 = 0,
  CEVNI_INLAND = 1,
  CUSTOM = 2,
  UNKNOWN = 255
};

struct RuleLibraryConfig {
  RuleLibraryType lib_type;
  std::string lib_version;
  std::string lib_path;                    // 配置文件路径（YAML）
  bool validate_against_colregs;
  bool fallback_to_default;
};

/**
 * @brief 规则库加载与切换 — 实现 Capability Manifest 多船型扩展点.
 *
 * v1.0 行为:
 *   - 启动时读取 rule_library_path 字段
 *   - 若 type = COLREGS_1972 → 实例化 colregs/ 子目录的 11 条规则
 *   - 若 type = CEVNI_INLAND → 实例化 cevni/ 子目录的规则（v1.0 stub）
 *   - 加载失败若 fallback_to_default = true，回退 COLREGs 1972
 *
 * Per 详细设计 §5.3 规则库插件接口 + 架构 v1.1.2 §13.5 多船型扩展.
 */
class RuleLibraryLoader {
 public:
  RuleLibraryLoader() = default;

  /**
   * @brief 加载规则库.
   * @return RuleSet 或 ErrorCode（M1/M7 风格 expected 模式）
   */
  std::optional<rules::RuleSet> load(const RuleLibraryConfig& cfg);

  /**
   * @brief 校验加载的规则集是否覆盖 Rule 5/7/8/13/14/15/17（最小集）.
   */
  bool validate_minimum_coverage(const rules::RuleSet& set) const;

  /**
   * @brief 获取上次加载的库类型与版本（供 ASDR 记录）.
   */
  RuleLibraryType current_type() const noexcept { return current_type_; }
  std::string current_version() const noexcept { return current_version_; }

 private:
  rules::RuleSet load_colregs_1972_();
  rules::RuleSet load_cevni_inland_();   // v1.0 stub

  RuleLibraryType current_type_ = RuleLibraryType::UNKNOWN;
  std::string current_version_;
};

}  // namespace mass_l3::m6_colregs
```

### 4.6 ColregsReasonerNode（ROS2 节点）

`include/m6_colregs_reasoner/colregs_reasoner_node.hpp`：

```cpp
#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/colreg_s_constraint.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"
#include "m6_colregs_reasoner/rule_library_loader.hpp"
#include "m6_colregs_reasoner/target_state_cache.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class ColregsReasonerNode final : public rclcpp::Node {
 public:
  explicit ColregsReasonerNode(const rclcpp::NodeOptions& options);

  // 不可拷贝 / 不可移动（rclcpp::Node 子类约定）
  ColregsReasonerNode(const ColregsReasonerNode&) = delete;
  ColregsReasonerNode& operator=(const ColregsReasonerNode&) = delete;

 private:
  // === 初始化 ===
  void declare_parameters_();
  void load_rule_library_();
  void load_odd_thresholds_();
  void create_subscribers_();
  void create_publishers_();
  void create_timers_();

  // === 订阅回调 ===
  void on_odd_state_(const l3_msgs::msg::ODDState& msg);
  void on_world_state_(const l3_msgs::msg::WorldState& msg);

  // === 周期任务 ===
  void on_reasoning_timer_();        // 500 ms (2 Hz)
  void on_health_check_timer_();     // 1 s
  void on_asdr_snapshot_timer_();    // 500 ms
  void on_sat_publish_timer_();      // 100 ms (10 Hz)

  // === 输出生成 ===
  void publish_constraint_(const l3_msgs::msg::COLREGsConstraint& msg);
  void publish_asdr_event_(const std::string& decision_type,
                           const std::string& decision_json);
  void publish_sat_data_();

  // === 状态变化检测（详细设计 §6.2 事件触发任务）===
  void detect_rule_changes_(const l3_msgs::msg::COLREGsConstraint& msg);
  void detect_odd_switch_(OddDomain new_odd);

  // === 降级（详细设计 §7.1）===
  bool is_world_state_fresh_() const;
  bool is_odd_state_fresh_() const;
  float compute_confidence_() const;

  // === 成员 ===
  // INVARIANT: 所有 mutex_ 保护的字段必须在持锁时访问
  std::mutex state_mutex_;
  OddDomain current_odd_ = OddDomain::ODD_UNKNOWN;
  RuleParameters current_params_{};
  std::optional<l3_msgs::msg::WorldState> latest_world_state_;
  rclcpp::Time latest_world_state_stamp_;
  rclcpp::Time latest_odd_state_stamp_;

  // 规则库与算法组件
  RuleLibraryLoader rule_loader_;
  rules::RuleSet rules_;
  ColregsPhaseClassifier phase_classifier_;
  ColregsConstraintGenerator constraint_generator_;
  TargetStateCache target_cache_;

  // ODD-aware 参数（依 ODD 切换查表）
  std::unordered_map<OddDomain, RuleParameters> odd_params_;

  // 健康状态
  struct HealthStatus {
    rclcpp::Time last_successful_reasoning;
    std::int32_t failed_reasoning_count = 0;
    bool rule_library_loaded = false;
  } health_;

  // 上次发布的约束（用于检测 rule_change / phase_transition）
  std::optional<l3_msgs::msg::COLREGsConstraint> last_constraint_;

  // ROS2 接口
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_sub_;
  rclcpp::Publisher<l3_msgs::msg::COLREGsConstraint>::SharedPtr constraint_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;
  rclcpp::TimerBase::SharedPtr reasoning_timer_;
  rclcpp::TimerBase::SharedPtr health_check_timer_;
  rclcpp::TimerBase::SharedPtr asdr_snapshot_timer_;
  rclcpp::TimerBase::SharedPtr sat_publish_timer_;
};

}  // namespace mass_l3::m6_colregs
```

`src/main.cpp`：

```cpp
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>(
      rclcpp::NodeOptions{});
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
```

---

## 5. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(m6_colregs_reasoner LANGUAGES CXX)

# C++17 强制（coding-standards.md §2.1）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项（coding-standards.md §2.2）
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
  -Wuseless-cast -Wlogical-op
  -Wduplicated-cond -Wduplicated-branches
  -fstack-protector-strong -D_FORTIFY_SOURCE=2
)

# Debug 启用 ASan / UBSan
add_compile_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)
add_link_options(
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# Release: -O2（不允许 -O3）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O2)
endif()

# === 依赖 ===
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(Eigen3 5.0.0 EXACT REQUIRED)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)
find_package(Boost 1.91.0 EXACT REQUIRED)  # Boost.Geometry header-only

# === 库源文件 ===
set(M6_LIB_SOURCES
  src/colregs_reasoner_node.cpp
  src/colregs_constraint_generator.cpp
  src/colregs_phase_classifier.cpp
  src/rule_library_loader.cpp
  src/target_state_cache.cpp
  src/geometry_utils.cpp
  src/rules/colregs/rule5_lookout.cpp
  src/rules/colregs/rule6_safe_speed.cpp
  src/rules/colregs/rule7_risk_of_collision.cpp
  src/rules/colregs/rule8_action_to_avoid.cpp
  src/rules/colregs/rule13_overtaking.cpp
  src/rules/colregs/rule14_head_on.cpp
  src/rules/colregs/rule15_crossing.cpp
  src/rules/colregs/rule16_give_way.cpp
  src/rules/colregs/rule17_stand_on.cpp
  src/rules/colregs/rule18_responsibilities.cpp
  src/rules/colregs/rule19_restricted_visibility.cpp
  src/rules/cevni/cevni_stub.cpp
)

add_library(m6_colregs_lib STATIC ${M6_LIB_SOURCES})
target_include_directories(m6_colregs_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(m6_colregs_lib
  Eigen3::Eigen
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  Boost::headers
)
ament_target_dependencies(m6_colregs_lib rclcpp l3_msgs)

# === 可执行节点 ===
add_executable(m6_colregs_reasoner_node src/main.cpp)
target_link_libraries(m6_colregs_reasoner_node m6_colregs_lib)
ament_target_dependencies(m6_colregs_reasoner_node rclcpp l3_msgs)

install(TARGETS
  m6_colregs_lib
  m6_colregs_reasoner_node
  DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

# === 单元测试 ===
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)

  set(M6_TESTS
    test/unit/test_rule5_lookout.cpp
    test/unit/test_rule6_safe_speed.cpp
    test/unit/test_rule7_risk_of_collision.cpp
    test/unit/test_rule8_action_to_avoid.cpp
    test/unit/test_rule13_overtaking.cpp
    test/unit/test_rule14_head_on.cpp
    test/unit/test_rule15_crossing.cpp
    test/unit/test_rule16_give_way.cpp
    test/unit/test_rule17_stand_on.cpp
    test/unit/test_rule18_responsibilities.cpp
    test/unit/test_rule19_restricted_visibility.cpp
    test/unit/test_phase_classifier.cpp
    test/unit/test_constraint_generator.cpp
    test/unit/test_rule_library_loader.cpp
    test/unit/test_odd_aware_thresholds.cpp
    test/unit/test_geometry_utils.cpp
  )

  foreach(test_src ${M6_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    ament_add_gtest(${test_name} ${test_src})
    target_link_libraries(${test_name} m6_colregs_lib)
  endforeach()

  # 集成测试
  ament_add_gtest(test_node_lifecycle test/integration/test_node_lifecycle.cpp)
  target_link_libraries(test_node_lifecycle m6_colregs_lib)
  ament_add_gtest(test_msg_pipeline test/integration/test_msg_pipeline.cpp)
  target_link_libraries(test_msg_pipeline m6_colregs_lib)
endif()

ament_package()
```

---

## 6. package.xml

```xml
<?xml version="1.0"?>
<package format="3">
  <name>m6_colregs_reasoner</name>
  <version>0.1.0</version>
  <description>
    M6 COLREGs Reasoner — Rule 5–19 inference engine for MASS L3 Tactical Decision Layer.
    Implements ODD-aware parameters and pluggable rule library (COLREGs 1972 / CEVNI inland).
    See docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md.
  </description>
  <maintainer email="team-m6@mass-l3.example">Team-M6</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>l3_msgs</depend>
  <depend>eigen</depend>
  <depend>spdlog</depend>
  <depend>yaml-cpp</depend>
  <depend>libboost-dev</depend>

  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 7. 单元测试骨架（GTest）

> 总计 ≥ 30 个 COLREGs 规则测试用例覆盖；目标行覆盖率 ≥ 95%（`coding-standards.md` §11 + 详细设计 §9.1）.

### 7.1 Rule13/14/15 各场景（每条 Rule 至少 5 个场景）

`test/unit/test_rule13_overtaking.cpp`（示例）：

```cpp
#include <gtest/gtest.h>

#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"
#include "m6_colregs_reasoner/types.hpp"

using namespace mass_l3::m6_colregs;
using namespace mass_l3::m6_colregs::rules::colregs;

namespace {
RuleParameters default_params() {
  return RuleParameters{
      /*t_standOn_s*/ 480.0,
      /*t_act_s*/ 240.0,
      /*t_emergency_s*/ 60.0,
      /*min_alteration_deg*/ 30.0,
      /*cpa_safe_m*/ 1852.0,
      /*max_turn_rate_deg_s*/ 12.0,
      /*rule_9_weight*/ 1.0};
}

TargetGeometricState base_geo() {
  return TargetGeometricState{
      /*target_id*/ 1,
      /*bearing_deg*/ 135.0,
      /*aspect_deg*/ 10.0,
      /*relative_speed_kn*/ -5.0,
      /*cpa_m*/ 800.0,
      /*tcpa_s*/ 240.0,
      /*ownship_heading_deg*/ 0.0,
      /*ownship_speed_kn*/ 15.0,
      /*target_ship_type_priority*/ 6,
      /*stamp*/ std::chrono::system_clock::now()};
}
}  // namespace

TEST(Rule13Overtaking, ActiveWhenInSternArcAndAspectAligned) {
  Rule13Overtaking rule;
  auto geo = base_geo();          // bearing=135, aspect=10, rel_speed=-5
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_TRUE(eval.is_active);
  EXPECT_EQ(eval.encounter_type, EncounterType::OVERTAKING);
  EXPECT_EQ(eval.role, Role::GIVE_WAY);
  EXPECT_EQ(eval.preferred_direction, "STARBOARD");
  EXPECT_DOUBLE_EQ(eval.min_alteration_deg, 30.0);
}

TEST(Rule13Overtaking, InactiveWhenOutsideSternArc) {
  Rule13Overtaking rule;
  auto geo = base_geo();
  geo.bearing_deg = 90.0;          // 不在 [112.5, 247.5)
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_FALSE(eval.is_active);
}

TEST(Rule13Overtaking, InactiveWhenAspectTooLarge) {
  Rule13Overtaking rule;
  auto geo = base_geo();
  geo.aspect_deg = 45.0;           // > 30°
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_FALSE(eval.is_active);
}

TEST(Rule13Overtaking, InactiveWhenRelativeSpeedNonNegative) {
  Rule13Overtaking rule;
  auto geo = base_geo();
  geo.relative_speed_kn = 2.0;     // 背离
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_FALSE(eval.is_active);
}

TEST(Rule13Overtaking, BoundaryBearingExactly112_5) {
  Rule13Overtaking rule;
  auto geo = base_geo();
  geo.bearing_deg = 112.5;          // 边界值（包含）
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_TRUE(eval.is_active);
}

TEST(Rule13Overtaking, BoundaryBearingExactly247_5) {
  Rule13Overtaking rule;
  auto geo = base_geo();
  geo.bearing_deg = 247.5;          // 边界值（不包含）
  auto eval = rule.evaluate(geo, OddDomain::ODD_A, default_params());
  EXPECT_FALSE(eval.is_active);
}
```

类似地，每条 Rule 都有自己的 `test/unit/test_ruleN_*.cpp` 文件，覆盖：
- Rule 5 / 6 / 7 / 8（基础规则）：3-5 用例（is_active / 边界 / 退化）
- Rule 13 / 14 / 15：每条 ≥ 5 用例（含边界 / 跨象限 / 退化 / 模糊场景）
- Rule 16 / 17 / 18：每条 ≥ 3-5 用例
- Rule 19：≥ 3 用例（ODD-D 激活 / 非 ODD-D 不激活 / 与 Rule 13 互斥）

**累计 Rule 单元测试 ≥ 30 用例**（详细设计 §9.1 KPI）.

### 7.2 ColregsPhaseClassifier（CPA / TCPA → phase）

`test/unit/test_phase_classifier.cpp`：

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"

using namespace mass_l3::m6_colregs;

TEST(PhaseClassifier, StandOnPreserveCourseWhenTcpaAboveStandOn) {
  ColregsPhaseClassifier cls;
  RuleParameters p{};
  p.t_standOn_s = 480.0; p.t_act_s = 240.0; p.t_emergency_s = 60.0;
  EXPECT_EQ(cls.classify(Role::STAND_ON, 600.0, p), TimingPhase::PRESERVE_COURSE);
}

TEST(PhaseClassifier, StandOnSoundWarningInTransition) {
  ColregsPhaseClassifier cls;
  RuleParameters p{}; p.t_standOn_s = 480; p.t_act_s = 240; p.t_emergency_s = 60;
  EXPECT_EQ(cls.classify(Role::STAND_ON, 300.0, p), TimingPhase::SOUND_WARNING);
}

TEST(PhaseClassifier, StandOnIndependentActionWhenBelowTAct) {
  ColregsPhaseClassifier cls;
  RuleParameters p{}; p.t_standOn_s = 480; p.t_act_s = 240; p.t_emergency_s = 60;
  EXPECT_EQ(cls.classify(Role::STAND_ON, 120.0, p), TimingPhase::INDEPENDENT_ACTION);
}

TEST(PhaseClassifier, GiveWayCriticalWhenBelowEmergency) {
  ColregsPhaseClassifier cls;
  RuleParameters p{}; p.t_standOn_s = 480; p.t_act_s = 240; p.t_emergency_s = 60;
  EXPECT_EQ(cls.classify(Role::GIVE_WAY, 30.0, p), TimingPhase::CRITICAL_ACTION);
}

TEST(PhaseClassifier, BoundaryTcpaEqualsTStandOn) {
  ColregsPhaseClassifier cls;
  RuleParameters p{}; p.t_standOn_s = 480; p.t_act_s = 240; p.t_emergency_s = 60;
  // tcpa = T_standOn 边界归 SOUND_WARNING（详细设计 §5.1 第五层 IF tcpa > T_standOn）
  EXPECT_EQ(cls.classify(Role::STAND_ON, 480.0, p), TimingPhase::SOUND_WARNING);
}

TEST(PhaseClassifier, ShouldSoundWarningInRule17Phases2And3) {
  ColregsPhaseClassifier cls;
  EXPECT_TRUE(cls.should_sound_warning(Role::STAND_ON, TimingPhase::SOUND_WARNING));
  EXPECT_TRUE(cls.should_sound_warning(Role::STAND_ON, TimingPhase::INDEPENDENT_ACTION));
  EXPECT_FALSE(cls.should_sound_warning(Role::STAND_ON, TimingPhase::PRESERVE_COURSE));
  EXPECT_FALSE(cls.should_sound_warning(Role::GIVE_WAY, TimingPhase::INDEPENDENT_ACTION));
}
```

### 7.3 ColregsConstraintGenerator（多目标多 Rule 同时活跃）

`test/unit/test_constraint_generator.cpp`：

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
// ...

TEST(ConstraintGenerator, SingleTargetRule15GiveWay) {
  // 输入：1 个目标 bearing=125°, cpa=0.8nm, tcpa=240s
  // 期望：active_rules 含 RuleId=15, role=GIVE_WAY, phase=PRESERVE_COURSE
}

TEST(ConstraintGenerator, MultipleTargetsDifferentRules) {
  // 输入：3 个目标 (Rule13 / Rule14 / Rule15)
  // 期望：active_rules.size() = 3，每个目标的约束独立列出
}

TEST(ConstraintGenerator, SimultaneousRule18AndRule15Arbitration) {
  // 输入：目标船优先级（限制吃水）+ Rule 15 几何（本应 STAND_ON）
  // 期望：Rule 18 优先序覆盖 → role = GIVE_WAY
}

TEST(ConstraintGenerator, EmptyWorldStateProducesEmptyConstraints) {
  // 输入：targets[] 为空
  // 期望：active_rules 仅含 Rule 5 (lookout) / Rule 6 (safe speed) 基础约束
}
```

### 7.4 RuleLibraryLoader（COLREGs ↔ CEVNI 切换）

`test/unit/test_rule_library_loader.cpp`：

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rule_library_loader.hpp"

using namespace mass_l3::m6_colregs;

TEST(RuleLibraryLoader, LoadDefaultColregs1972) {
  RuleLibraryLoader loader;
  RuleLibraryConfig cfg{
      RuleLibraryType::COLREGS_1972, "1972", "config/colregs_rule_library.yaml",
      true, true};
  auto rs = loader.load(cfg);
  ASSERT_TRUE(rs.has_value());
  EXPECT_EQ(loader.current_type(), RuleLibraryType::COLREGS_1972);
  EXPECT_TRUE(loader.validate_minimum_coverage(*rs));
}

TEST(RuleLibraryLoader, LoadCevniInlandStubV1) {
  // v1.0: CEVNI 是 stub，只验证 type + 最小覆盖
  RuleLibraryLoader loader;
  RuleLibraryConfig cfg{
      RuleLibraryType::CEVNI_INLAND, "stub", "config/cevni_rule_library.yaml",
      false, false};
  auto rs = loader.load(cfg);
  ASSERT_TRUE(rs.has_value());
  EXPECT_EQ(loader.current_type(), RuleLibraryType::CEVNI_INLAND);
}

TEST(RuleLibraryLoader, FallbackToColregsWhenInvalidPath) {
  RuleLibraryLoader loader;
  RuleLibraryConfig cfg{
      RuleLibraryType::CUSTOM, "broken", "/nonexistent/path.yaml",
      true, /*fallback=*/true};
  auto rs = loader.load(cfg);
  ASSERT_TRUE(rs.has_value());
  EXPECT_EQ(loader.current_type(), RuleLibraryType::COLREGS_1972);  // 已回退
}

TEST(RuleLibraryLoader, NoFallbackReturnsNullopt) {
  RuleLibraryLoader loader;
  RuleLibraryConfig cfg{
      RuleLibraryType::CUSTOM, "broken", "/nonexistent/path.yaml",
      true, /*fallback=*/false};
  auto rs = loader.load(cfg);
  EXPECT_FALSE(rs.has_value());
}
```

### 7.5 ODD-aware 参数（ODD-A 宽 vs ODD-B 严格）

`test/unit/test_odd_aware_thresholds.cpp`：

```cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/types.hpp"
// 加载 odd_aware_thresholds.yaml 后比较

TEST(OddAwareThresholds, OddAHasWidestThresholds) {
  // ODD-A: T_standOn=480s, T_act=240s, min_alt=30°
  // ODD-B: T_standOn=360s, T_act=180s, min_alt=20°
  // 验证 ODD-A 严格大于 ODD-B 的时间 + ODD-A 的 min_alt 严格大于 ODD-B
}

TEST(OddAwareThresholds, OddDLongestStandOn) {
  // ODD-D: T_standOn=600s（最长，能见度不良须更长观察期）
}

TEST(OddAwareThresholds, OddCMostStrictMinAlteration) {
  // ODD-C: min_alt=15°（港内空间受限）
}

TEST(OddAwareThresholds, OddUnknownReturnsMostConservative) {
  // 未知 ODD 应返回最保守组合（T_standOn=600s + min_alt=30°）
}
```

### 7.6 单元测试覆盖率门禁

```bash
# 在 CI 中（ci-cd-pipeline.md §X 单元测试阶段）
colcon test --packages-select m6_colregs_reasoner --return-code-on-test-failure
lcov --capture --directory build/ --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/test/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html
# 阈值：line coverage ≥ 90%；branch coverage ≥ 85%
```

---

## 8. 配置文件

### 8.1 config/m6_params.yaml（Rule 通用参数）

见 §3.4。

### 8.2 config/odd_aware_thresholds.yaml（每个 ODD 子域的 T_standOn / T_act 阈值）

```yaml
# Per 详细设计 §5.3 ODD-aware 参数表 + [HAZID 校准]
# 当前值来自 [R17] Wang 2021 机动船基线；FCB 实船试航后通过 RUN-001 校准回填.
# 文件加载位置：m6_params_yaml.odd_thresholds_path
# v1.1.3 HAZID 校准回填后版本号 → "1.1.0"

version: "1.0"
calibration_status: "INITIAL_BASELINE_R17_WANG_2021"

odd_zones:
  # === ODD-A: 开阔水域 ===
  ODD_A:
    t_standOn_s: 480.0       # [TBD-HAZID] 8 min, ref [R17]
    t_act_s: 240.0           # [TBD-HAZID] 4 min, ref [R17]
    t_emergency_s: 60.0      # [TBD-HAZID] 1 min, ref [R17]
    min_alteration_deg: 30.0 # [TBD-HAZID] Rule 8 大幅, ref [R17]
    cpa_safe_m: 1852.0       # [TBD-HAZID] 1.0 nm, ref [R17]
    max_turn_rate_deg_s: 12.0  # 来自 Capability Manifest（FCB MMG）
    rule_9_weight: 1.0

  # === ODD-B: 狭水道 / VTS ===
  ODD_B:
    t_standOn_s: 360.0       # [TBD-HAZID] 6 min
    t_act_s: 180.0           # [TBD-HAZID] 3 min
    t_emergency_s: 45.0      # [TBD-HAZID] 0.75 min
    min_alteration_deg: 20.0 # [TBD-HAZID] 受限环境
    cpa_safe_m: 556.0        # [TBD-HAZID] 0.3 nm
    max_turn_rate_deg_s: 10.0
    rule_9_weight: 2.0       # Rule 9 靠右权重加倍

  # === ODD-C: 港内 / 靠泊 ===
  ODD_C:
    t_standOn_s: 300.0       # [TBD-HAZID] 5 min
    t_act_s: 150.0           # [TBD-HAZID] 2.5 min
    t_emergency_s: 30.0      # [TBD-HAZID] 0.5 min
    min_alteration_deg: 15.0 # [TBD-HAZID] 港内空间受限
    cpa_safe_m: 185.0        # [TBD-HAZID] 0.1 nm
    max_turn_rate_deg_s: 8.0
    rule_9_weight: 3.0

  # === ODD-D: 能见度不良 ===
  ODD_D:
    t_standOn_s: 600.0       # [TBD-HAZID] 10 min (保守)
    t_act_s: 300.0           # [TBD-HAZID] 5 min
    t_emergency_s: 90.0      # [TBD-HAZID] 1.5 min
    min_alteration_deg: 30.0 # [TBD-HAZID] Rule 19 + Rule 8
    cpa_safe_m: 2778.0       # [TBD-HAZID] 1.5 nm (保守)
    max_turn_rate_deg_s: 12.0
    rule_9_weight: 0.0       # ODD-D 不适用 Rule 9

  # === ODD_UNKNOWN: 最保守（OUT_of_ODD 退路）===
  ODD_UNKNOWN:
    t_standOn_s: 600.0       # 取 ODD-D 保守值
    t_act_s: 300.0
    t_emergency_s: 90.0
    min_alteration_deg: 30.0
    cpa_safe_m: 2778.0
    max_turn_rate_deg_s: 8.0  # 取 ODD-C 最低
    rule_9_weight: 0.0
```

### 8.3 config/colregs_rule_library.yaml（COLREGs 默认）

```yaml
# Capability Manifest — COLREGs 1972 默认规则库
# Per 详细设计 §5.3 + 架构 v1.1.2 §13.5 多船型扩展

library:
  type: "COLREGS_1972"
  version: "1972.2003.amendments"
  applicable_vessel_types: ["FCB", "TUG", "GENERAL_POWER_DRIVEN"]

rules:
  # 启用规则列表（Rule 5–19 全启用）
  - id: 5
    name: "Rule5_LookOut"
    enabled: true
  - id: 6
    name: "Rule6_SafeSpeed"
    enabled: true
  - id: 7
    name: "Rule7_RiskOfCollision"
    enabled: true
  - id: 8
    name: "Rule8_ActionToAvoid"
    enabled: true
  - id: 13
    name: "Rule13_Overtaking"
    enabled: true
  - id: 14
    name: "Rule14_HeadOn"
    enabled: true
  - id: 15
    name: "Rule15_Crossing"
    enabled: true
  - id: 16
    name: "Rule16_GiveWay"
    enabled: true
  - id: 17
    name: "Rule17_StandOn"
    enabled: true
  - id: 18
    name: "Rule18_Responsibilities"
    enabled: true
  - id: 19
    name: "Rule19_RestrictedVisibility"
    enabled: true
    activation: "ODD_D_ONLY"  # 仅 ODD-D 激活，取代 Rule 13–17

# Rule 18 优先序（[R18] COLREGs Rule 18）
ship_priority:
  NotUnderCommand: 1
  RestrictedManeuverability: 2
  ConstrainedByDraft: 3
  Fishing: 4
  Sailing: 5
  PowerDriven: 6  # 本船基线（FCB）
```

### 8.4 config/cevni_rule_library.yaml（内河船 — 备选 stub）

```yaml
# Capability Manifest — CEVNI 内河船规则库（v1.0 stub）
# Per 架构 v1.1.2 §13.5 + F-P2-D4-027 多船型扩展.
# v1.0 仅占位；正式实现待船型扩展工作流（docs/Vessel Adaptations/）.

library:
  type: "CEVNI_INLAND"
  version: "stub-v0.1"
  applicable_vessel_types: ["INLAND_FERRY", "INLAND_TUG"]
  notes: "v1.0 stub only; full CEVNI implementation in v1.x via 多船型扩展"

rules:
  # CEVNI 关键 Article（与 COLREGs 不完全对应）— v1.0 仅声明，行为转 stub
  - id: 6   # CEVNI Article 6.04 — 让路船 / 直航船
    name: "CEVNI_Overtaking"
    enabled: false   # v1.0 stub disabled
  - id: 7
    name: "CEVNI_Crossing"
    enabled: false
  - id: 8
    name: "CEVNI_HeadOn"
    enabled: false
```

---

## 9. PATH-D 合规性 checklist

| 项 | 要求 | 验证方式 | 状态 |
|---|---|---|---|
| MISRA C++:2023 完整 179 规则 | CI 强制（clang-tidy + cppcheck Premium）| `tools/ci/static-analysis.sh m6` | ⏳ 实施期 |
| 编译选项 `-Werror -Wall -Wextra ...` | `coding-standards.md` §2.2 全套 | CMake `add_compile_options` | ✅ §5 已写入 |
| 函数 ≤ 60 行 / 循环复杂度 ≤ 10 | `coding-standards.md` §3.5 | clang-tidy `readability-function-size` | ⏳ |
| AddressSanitizer + ThreadSanitizer + UBSan 0 错误 | Debug build 必跑 | CI test 阶段 | ⏳ |
| 单元测试覆盖率 ≥ 90% | lcov 报告 | CI gate（§7.6） | ⏳ |
| ≥ 30 个 COLREGs 规则测试用例 | 详细设计 §9.1 | §7 已枚举 16 个测试文件 | ✅ §7 已规划 |
| 端到端延迟 ≤ 250 ms | CI 集成测试 + HIL 验证 | `test_msg_pipeline.cpp` | ⏳ |
| 2 Hz 输出可用率 ≥ 99.5% (ODD-A/B/C) | HIL 1000+ 场景 | docs/Test Plan/ | ⏳ Wave 4 |
| Rule 分类准确率 ≥ 99.5% | HIL 10000+ 场景 vs 专家判断 | docs/Test Plan/ | ⏳ Wave 4 |
| 所有 [TBD-HAZID] 通过 YAML 注入 | grep 检查 .cpp / .hpp 内不含硬编码阈值 | CI 检查脚本 | ✅ §8.2 已分离 |
| ASDR_RecordMsg 含 SHA-256 签名 | `decision_json` + `signature` 字段 | 详细设计 §8.3 | ⏳ |
| Doer-Checker 独立性（M6 不引 M7 任何 header） | CI grep | `tools/ci/check-doer-checker-independence.sh` | ✅ 设计上已隔离 |
| 第三方库白名单符合 §2.2 | `third-party-library-policy.md` §3.1 | CI link grep | ✅ §5 CMakeLists 已限定 |
| 路径 PATH-D 不允许 ML / 黑箱 | 详细设计 §10.4 + 编码规范 §3.x | code review | ✅ 设计禁用 ML |
| HAZID 校准点 `[TBD-HAZID]` 注释 | `coding-standards.md` §5.1 第 4 项 | CI grep 验证均带 `[TBD-HAZID]` | ✅ §8.2 全部标注 |
| §15 IDL 接口符合 v1.1.2 + ros2-idl-implementation-guide.md §3.3 | PR review | l3_msgs/COLREGsConstraint 字段对齐 | ✅ §3.2/3.3 已对齐 |

---

## 10. 引用

### 10.1 项目内文档

- 架构主文件 v1.1.2 — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §9（M6）/ §13.5（多船型扩展）/ §15.1（IDL）/ §15.2（接口矩阵）
- M6 详细设计 — `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md` v1.0（SANGO-ADAS-L3-DD-M6-001）
- 实施主计划 — `docs/Implementation/00-implementation-master-plan.md` v1.0
- 编码规范 — `docs/Implementation/coding-standards.md` v1.0
- 静态分析政策 — `docs/Implementation/static-analysis-policy.md` v1.0
- CI/CD 管道 — `docs/Implementation/ci-cd-pipeline.md` v1.0
- ROS2 IDL 指南 — `docs/Implementation/ros2-idl-implementation-guide.md` v1.0 §3.3
- 第三方库政策 — `docs/Implementation/third-party-library-policy.md` v1.0
- 6 RFC 决议 — `docs/Design/Cross-Team Alignment/RFC-decisions.md`
- HAZID 启动包 — `docs/Design/HAZID/RUN-001-kickoff.md`

### 10.2 外部规范与学术引用（继承详细设计 §11）

| 引用 | 全称 / 出处 | 用途 |
|---|---|---|
| [R18] | IMO COLREGs 1972（含 2003 修订） | Rule 5–19 法律定义；§4.2 各 Rule 类核心依据 |
| [R17] | Wang et al. (2021) — MDPI JMSE 9(6):584 | T_standOn=8min / T_act=4min FCB 基线；min_alteration_deg=30° 工业量化；§8.2 odd_aware_thresholds.yaml 初值来源 |
| [R19] | Bitar et al. (2019) — arXiv:1907.00198 | COLREGs 形式化状态机；§4.2 分层规则结构设计依据 |
| [R20] | Eriksen, Bitar, Breivik et al. (2020) — Frontiers in Robotics & AI 7:11 | BC-MPC 避碰算法；M5 集成依据；右转优先依据 |
| [R7] | Yasukawa & Yoshimura (2015) — J Mar Sci Tech 20:37–52 | MMG 4-DOF 模型；HAZID 校准回填依据 |
| [R5] | IEC 61508:2010 Functional Safety | SIL 2 推理核心 + SIL 1 接口；§1.3 PATH-D 路径强度 |
| [R6] | ISO 21448:2022 SOTIF | 与 M7 Checker 双轨架构 |
| [R4] | IMO MSC 110/111 MASS Code (2025/2026) | 系统须识别越出 Operational Envelope |
| [R8] | CCS《智能船舶规范》(DMV-CG-0264) | 入级要求：白盒可审计 |

### 10.3 ROS2 / 工具链证据（2026-05-06 验证）

| 项目 | 来源 URL | 置信度 |
|---|---|---|
| ROS2 Jazzy Jalisco | [docs.ros.org/en/jazzy](https://docs.ros.org/en/jazzy/) | 🟢 A |
| Eigen 5.0.0 | [gitlab.com/libeigen/eigen](https://gitlab.com/libeigen/eigen/-/releases) | 🟢 A |
| Boost 1.91.0 | [boost.org/releases/latest](https://www.boost.org/releases/latest/) | 🟢 A |
| spdlog 1.17.0 | [github.com/gabime/spdlog](https://github.com/gabime/spdlog/releases) | 🟢 A |
| yaml-cpp 0.8.0 | [github.com/jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp/releases) | 🟢 A |
| GTest 1.17.0 | [github.com/google/googletest](https://github.com/google/googletest/releases) | 🟢 A |
| MISRA C++:2023 | [Perforce 公告](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) | 🟢 A |

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude (Subagent) | 初稿创建：M6 代码骨架 11 章 + 项目结构 + ROS2 节点设计 + 11 条 Rule 类骨架（Rule 5/6/7/8/13/14/15/16/17/18/19）+ ColregsPhaseClassifier + ColregsConstraintGenerator + RuleLibraryLoader（COLREGs ↔ CEVNI）+ CMakeLists/package.xml + ≥ 30 单元测试用例规划 + 4 YAML 配置文件（HAZID 阈值分离）+ PATH-D 合规 checklist |

---

**文档完成。Team-M6 可开始 Phase E1 编码（Wave 1，Week 1–4）。**
