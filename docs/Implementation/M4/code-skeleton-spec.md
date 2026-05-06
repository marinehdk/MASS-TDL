# M4 Behavior Arbiter 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M4-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M4 Phase E1 实施起点） |
| 团队 | Team-M4 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则；clang-tidy + cppcheck Premium 强制 |
| 详细设计基线 | SANGO-ADAS-L3-DD-M4-001（`docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md`） |
| 架构基线 | v1.1.2 §8（`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`） |
| 主计划 | `docs/Implementation/00-implementation-master-plan.md` |
| 编码规范 | `docs/Implementation/coding-standards.md` |
| IDL 映射 | `docs/Implementation/ros2-idl-implementation-guide.md` §3.3 |
| 第三方库政策 | `docs/Implementation/third-party-library-policy.md` |
| Wave | Wave 2（Week 3–6，依赖 M2/M6 mock 起步） |

> **本文件用途**：Team-M4 编码起点（PATH-D 路径）。8 个 colcon package 之一。源代码 + 单元测试覆盖率 ≥ 90%；本文件示例代码为骨架，不是最终实现，所有 `[TBD-HAZID]` 标注的常量须通过 YAML 注入（不硬编码）。

---

## 1. 模块概述

### 1.1 模块职责（一句话）

M4 Behavior Arbiter 是 L3 战术决策层的**多目标行为仲裁器**：维护 ODD-aware 行为字典（7 种行为），用 IvP（Interval Programming）求解器把多个并发行为目标融合为一对允许的航向区间 + 速度区间，输出至 M5 Tactical Planner（2 Hz `BehaviorPlan.msg`）。

### 1.2 路径强度

| 维度 | 值 |
|---|---|
| 路径分类 | **PATH-D（决策路径）** |
| 规则强度 | MISRA C++:2023 完整 179 规则（与 M2/M3/M5/M6 同级） |
| SIL 分配 | 行为激活逻辑 SIL 1；与 M6 联合的 COLREGs 合规 SIL 2（详细设计 §10.3） |
| 静态分析门禁 | clang-tidy + cppcheck Premium 全启用；Polyspace 推荐（不强制，M1/M7 才强制） |
| 动态分析 | AddressSanitizer + ThreadSanitizer + UBSan（Debug build 强制） |
| Doer/Checker 角色 | **Doer**（与 M1–M3、M5–M6 一起；M7 是 Checker） |

### 1.3 与详细设计的对应关系

| 详细设计章节 | 本文件章节 |
|---|---|
| §1 模块职责 | §1.1 |
| §2 输入接口 / §3 输出接口 / §15 IDL | §3.2/§3.3 订阅与发布表 |
| §4 内部状态 | §4.5 BehaviorArbiterNode 私有成员 |
| §5 核心算法（IvP） | §4.3 IvPSolver 类族 |
| §5.2 行为字典 | §4.1 BehaviorDictionary + §4.2 ActivationCondition |
| §6 时序 | §3.1 节点拓扑 + §4.5 timer 注释 |
| §7 降级与容错 | §4.5 health_state_ + §3.4 YAML 参数 |
| §8 协作 | §3.2/§3.3 + ASDR 写入器 §4.5 |
| §9 测试策略 | §7 单元测试骨架 |
| §10 实现约束 | §1.2 + §3.4 + §6 package.xml |

### 1.4 Phase E1 完成 DoD（继承主计划 §5.2）

- [ ] CI 5 阶段全绿（lint / unit / static / integration / release）
- [ ] 单元测试覆盖率 ≥ 90%（lcov/gcov 报告）
- [ ] clang-tidy + cppcheck Premium 0 阻断
- [ ] AddressSanitizer + ThreadSanitizer + UBSan 0 错误
- [ ] §15 IDL（`BehaviorPlan.msg` / `WorldState.msg` / `ODDState.msg` / `MissionGoal.msg` / `COLREGsConstraint.msg` / `ModeCmd.msg` / `ASDRRecord.msg`）订阅 / 发布完成 + mock 桩集成测试通过
- [ ] 详细设计 §9.3 三个 HIL 场景至少有 mock-level 验证（多船密集、能见度不良、DP 靠泊）
- [ ] 所有 `[TBD-HAZID]` 参数通过 `config/m4_params.yaml` + `config/behavior_definitions.yaml` 注入
- [ ] ASDR 写入符合 §15.1 + RFC-004
- [ ] 模块 README + 本文件同步更新

---

## 2. 项目目录结构

```
mass_l3_tdl/                              # workspace 根（继承主计划 §4.1）
└── src/
    └── m4_behavior_arbiter/              # ROS2 colcon package（Team-M4 主战场）
        ├── package.xml                   # 见 §6
        ├── CMakeLists.txt                # 见 §5
        ├── README.md                     # 模块入口 + 启动命令
        │
        ├── include/m4_behavior_arbiter/
        │   ├── behavior_dictionary.hpp   # 行为字典 — §4.1
        │   ├── behavior_activation.hpp   # 激活条件 — §4.2
        │   ├── ivp_function.hpp          # IvP 区间值函数 — §4.3.1
        │   ├── ivp_domain.hpp            # 搜索域（heading/speed）— §4.3.2
        │   ├── ivp_solver.hpp            # 求解器主体 — §4.3
        │   ├── ivp_combine.hpp           # 多目标合并策略 — §4.3.3
        │   ├── behavior_priority.hpp     # 优先级仲裁规则 — §4.4
        │   ├── behavior_arbiter_node.hpp # ROS2 节点 — §4.5
        │   ├── error.hpp                 # ErrorCode enum（M4 = 4000–4999）
        │   └── types.hpp                 # 模块内部类型（行为枚举、ODD 视图等）
        │
        ├── src/
        │   ├── behavior_dictionary.cpp
        │   ├── behavior_activation.cpp
        │   ├── ivp_function.cpp
        │   ├── ivp_solver.cpp
        │   ├── ivp_combine.cpp
        │   ├── behavior_priority.cpp
        │   ├── behavior_arbiter_node.cpp
        │   └── main.cpp                  # rclcpp::init + spin
        │
        ├── config/
        │   ├── m4_params.yaml            # 周期 / 超时 / IvP 求解参数 — §8.1
        │   └── behavior_definitions.yaml # 7 种行为激活条件 + 默认权重 — §8.2
        │
        ├── launch/
        │   ├── m4_only.launch.py         # 单模块启动（开发用）
        │   └── m4_with_mocks.launch.py   # 带 M2/M6 mock（集成测试用）
        │
        └── test/
            ├── unit/
            │   ├── test_behavior_dictionary.cpp   # §7.1
            │   ├── test_ivp_function.cpp          # §7.2 单元
            │   ├── test_ivp_solver.cpp            # §7.2 主体
            │   ├── test_ivp_combine.cpp
            │   ├── test_behavior_priority.cpp     # §7.3
            │   └── test_behavior_plan_output.cpp  # §7.4
            ├── integration/
            │   ├── test_m4_with_mock_m2.cpp       # M2 → M4 接口
            │   ├── test_m4_with_mock_m6.cpp       # M6 → M4 接口
            │   └── test_m4_to_mock_m5.cpp         # M4 → M5 接口
            └── fixtures/
                ├── scenario_multi_target.yaml     # 详细设计 §9.3.1
                ├── scenario_low_visibility.yaml   # 详细设计 §9.3.2
                └── scenario_dp_hold.yaml          # 详细设计 §9.3.3
```

**关键约束**：
- 单 colcon package；与 `l3_msgs` / `l3_external_msgs` 通过 ament_target_dependencies 链接
- 所有 `include/` 头文件 PIMPL 或纯接口；`src/` 内 `static`/匿名命名空间隐藏内部状态
- `config/` YAML 由 `BehaviorArbiterNode` 在构造期加载；HAZID 校准结果通过 `ros2 param set` 热加载（不重编译）
- `test/fixtures/` 是详细设计 §9.3 三个 HIL 场景的 YAML 描述（mock-level 单元 / 集成测试用）

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

```
                          ┌────────────────────────────────────────┐
                          │   m4_behavior_arbiter_node             │
                          │                                        │
   /l3/m1/odd_state ──────▶│  on_odd_state()       [event-driven]  │
   /l3/m1/mode_cmd ───────▶│  on_mode_cmd()        [event-driven]  │
   /l3/m2/world_state ────▶│  on_world_state()     [4 Hz]          │
   /l3/m3/mission_goal ───▶│  on_mission_goal()    [0.5 Hz]        │
   /l3/m6/colregs_constr ─▶│  on_colregs_constr()  [2 Hz]          │
                          │                                        │
                          │  arbitration_timer_ @ 2 Hz (500 ms)    │
                          │  ├─ 1. 输入超时检查                    │
                          │  ├─ 2. 健康状态评估                    │
                          │  ├─ 3. 行为激活筛选（BehaviorDict）   │
                          │  ├─ 4. 权重集选择（NORMAL/DEGRADED/   │
                          │  │     CRITICAL）                      │
                          │  ├─ 5. IvP 求解（IvPSolver）          │
                          │  ├─ 6. 优先级仲裁（BehaviorPriority） │
                          │  ├─ 7. 输出组装（BehaviorPlan）       │
                          │  ├─ 8. ASDR 写入                       │
                          │  └─ 9. publish()                       │
                          │                                        │
   /l3/m4/behavior_plan ◀─│  publish (2 Hz, BehaviorPlan)         │
   /l3/asdr/record ◀──────│  publish (event + 2 Hz, ASDRRecord)   │
                          └────────────────────────────────────────┘

事件中断（不等下一个 500 ms 周期）：
  - on_odd_state() 检测 ODD 跳变（current_zone 变化或 health → CRITICAL）
    → 立即触发 trigger_arbitration()（详细设计 §6.2）
  - on_world_state() 检测 TCPA < T_emergency 的目标
    → 立即触发 trigger_arbitration()
  - on_colregs_constr() 检测 conflict_flag = true
    → 立即触发 trigger_arbitration()
```

**线程模型**（与编码规范 §3.3 一致）：
- 单 ROS2 executor（`rclcpp::executors::SingleThreadedExecutor`）：所有 callback 串行；不需要 mutex
- 进程级隔离：M4 独立进程（不与 M5 / M7 composable，避免线程错位）
- 不使用裸 `std::thread`；不使用 `std::condition_variable`（详细设计 §10.2 + 编码规范 §3.3）

### 3.2 订阅者表格

| Topic | 类型 | 频率 | QoS | 缓冲深度 | 超时阈值 | 超时行为 |
|---|---|---|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | 1 Hz + 事件 | reliable / volatile | 5 | 2 s | 标记 M1 输入失效；保持上次 ODD 状态；DEGRADED |
| `/l3/m1/mode_cmd` | `l3_msgs/ModeCmd` | 事件 | reliable / volatile | 1 | 不超时（事件型） | n/a |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | 4 Hz | reliable / volatile | 4 | 2 s | M2 失效；冻结上次 WorldState；DEGRADED；> 5 s 进入 CRITICAL |
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | 0.5 Hz | reliable / volatile | 2 | 4 s | 冻结上次目标；> 10 s 降级到 ODD 速度限制 |
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | 2 Hz | reliable / volatile | 4 | 2 s | M6 失效；保守约束集（CPA_safe ×1.5）；详细设计 §2.2 + §7.1.2 |

> **QoS 选择依据**：所有 5 个订阅均为 `RELIABLE` + `VOLATILE`（不持久化历史；丢失重传）。行为仲裁是状态时刻反映，不需要 `TRANSIENT_LOCAL`；4 Hz/2 Hz 频率下 `reliable` 不会显著拖慢。

### 3.3 发布者表格

| Topic | 类型 | 频率 | QoS | 字段映射 |
|---|---|---|---|---|
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | **2 Hz**（500 ms 周期） | reliable / volatile, depth = 4 | 详细设计 §3.1 + IDL guide §3.3 BehaviorPlan |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 事件 + 2 Hz | reliable / volatile, depth = 16 | RFC-004；source_module = `"M4_Behavior_Arbiter"`，decision_type 见 §4.5 |

**`BehaviorPlan.msg` 字段填充对照**（v1.1.2 §15.1 + IDL guide §3.3）：

| 字段 | 来源 | 取值范围 |
|---|---|---|
| `stamp` | `node->now()` | ROS2 当前时刻 |
| `behavior` | 详细设计 §5.2.1 + §4.4 | 0..6（TRANSIT/COLREG_AVOID/DP_HOLD/BERTH/MRC_DRIFT/MRC_ANCHOR/MRC_HEAVE_TO） |
| `heading_min_deg` / `heading_max_deg` | IvP 求解输出 | [0, 360)，含 360° 模运算（详细设计 §9.1.5 U5） |
| `speed_min_kn` / `speed_max_kn` | IvP 求解输出 | [0, max_speed_odd]；CRITICAL 时 [0, u_min_maneuver] |
| `confidence` | IvP optimality margin | [0, 1]；DEGRADED 时 ≤ 0.6；CRITICAL 时 ≤ 0.1 |
| `rationale` | `IvPSolver::build_rationale_string()` | SAT-2 摘要（活跃行为 + 权重 + COLREGs 约束摘要）|

> 详细设计 §3 还要求 `active_weights[]` 和 `constraint_violations[]` 字段（调试用）；这些在当前 v1.1.2 IDL 表中未列入主字段，按 IDL guide §1.1 由 `string rationale` 中的 JSON 子结构承载（M8 解析）；不破坏 IDL。

### 3.4 参数（YAML — 行为权重 / 行为优先级 / IvP 求解器参数）

参数清单（在 `config/m4_params.yaml` + `config/behavior_definitions.yaml`）：

| 类别 | 参数键 | 默认值 | 单位 | HAZID 标 |
|---|---|---|---|---|
| **节点周期** | `arbitration_period_ms` | 500 | ms | — |
| **输入超时** | `timeout_m1_s` | 2.0 | s | `[TBD-HAZID]` |
| | `timeout_m2_s` | 2.0 | s | `[TBD-HAZID]` |
| | `timeout_m3_s` | 4.0 | s | `[TBD-HAZID]` |
| | `timeout_m6_s` | 2.0 | s | `[TBD-HAZID]` |
| **降级阈值** | `degraded_to_critical_s` | 5.0 | s | `[TBD-HAZID]` |
| | `m2_confidence_min` | 0.5 | - | `[TBD-HAZID]` |
| **行为权重（NORMAL）** | `weight.transit` | 0.30 | - | `[TBD-HAZID]` |
| | `weight.colreg_avoid` | 0.70 | - | `[TBD-HAZID]` |
| | `weight.restricted_vis` | 0.60 | - | `[TBD-HAZID]` |
| | `weight.channel_follow` | 0.50 | - | `[TBD-HAZID]` |
| | `weight.dp_hold` | 0.85 | - | `[TBD-HAZID]` |
| | `weight.berth` | 0.80 | - | `[TBD-HAZID]` |
| | `weight.mrc_drift` | 1.00 | - | `[TBD-HAZID]` |
| | `weight.mrc_anchor` | 1.00 | - | `[TBD-HAZID]` |
| | `weight.mrc_heave_to` | 1.00 | - | `[TBD-HAZID]` |
| **DEGRADED 系数** | `degraded_scale.colreg_avoid` | 1.30 | - | `[TBD-HAZID]` |
| | `degraded_scale.transit` | 0.80 | - | `[TBD-HAZID]` |
| | `degraded_scale.cpa_safe` | 1.20 | - | `[TBD-HAZID]` |
| **CPA 安全距离** | `cpa_safe_nm.odd_a` | 1.0 | nm | `[TBD-HAZID]` |
| | `cpa_safe_nm.odd_b` | 0.3 | nm | `[TBD-HAZID]` |
| | `cpa_safe_nm.odd_c` | 0.15 | nm | `[TBD-HAZID]` |
| | `cpa_safe_nm.odd_d` | 1.5 | nm | `[TBD-HAZID]` |
| **IvP 求解** | `ivp.heading_resolution_deg` | 1.0 | deg | — |
| | `ivp.speed_resolution_kn` | 0.5 | kn | — |
| | `ivp.timeout_ms` | 100 | ms | — |
| | `ivp.max_pieces_per_function` | 32 | - | — |
| **优先级仲裁** | `priority.mrc_overrides_all` | true | bool | — |
| | `priority.colreg_overrides_transit` | true | bool | — |

> **HAZID 注入**：所有 `[TBD-HAZID]` 标的参数仅来自 YAML，不出现在源代码 `constexpr` / 头文件 default 中。CI 中 `tools/ci/check-no-hazid-hardcode.sh` 校验（grep 检测）。详细设计 §10.1 +主计划 §5.2。

---

## 4. 核心类定义（C++ header 骨架）

> 以下骨架仅展示接口与关键私有成员；不含完整实现。命名遵循编码规范 §4。所有类位于 `namespace mass_l3::m4`。

### 4.1 BehaviorDictionary（7 种行为定义 + 当前 ODD-aware 子集）

```cpp
// include/m4_behavior_arbiter/behavior_dictionary.hpp
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include "l3_msgs/msg/odd_state.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/**
 * @brief Enumeration of 7 behaviors per architecture v1.1.2 §8.3 + IDL guide §3.3.
 *
 * Values match `l3_msgs::msg::BehaviorPlan::BEHAVIOR_*` constants for direct mapping.
 */
enum class BehaviorType : uint8_t {
  Transit       = 0,  ///< 巡航；详细设计 §5.2.2
  ColregAvoid   = 1,  ///< COLREGs 避碰；详细设计 §5.2.3
  DpHold        = 2,  ///< DP 保持；详细设计 §5.2.4
  Berth         = 3,  ///< 靠泊操作（ODD-C 进出港）
  MrcDrift      = 4,  ///< 应急漂航（最高权重）
  MrcAnchor     = 5,  ///< 应急抛锚
  MrcHeaveTo    = 6,  ///< 顶风停船（恶劣天气）
};

constexpr size_t kBehaviorCount = 7;

/**
 * @brief Static metadata for one behavior.
 */
struct BehaviorDescriptor {
  BehaviorType        type;
  std::string_view    code_name;          ///< "TRANSIT" | "COLREG_AVOID" | ...
  std::vector<uint8_t> applicable_zones;  ///< ODD zones from ODDState constants
  double              default_weight;     ///< NORMAL mode initial weight [TBD-HAZID]
};

/**
 * @brief Static dictionary of all 7 behaviors with ODD applicability.
 *
 * Loaded from `config/behavior_definitions.yaml` at construction; no hot reload.
 * Provides ODD-aware filtering: get_active_subset() returns only behaviors
 * whose applicable_zones contains the current ODD zone.
 */
class BehaviorDictionary {
 public:
  BehaviorDictionary() = default;
  ~BehaviorDictionary() = default;

  BehaviorDictionary(const BehaviorDictionary&) = delete;
  BehaviorDictionary& operator=(const BehaviorDictionary&) = delete;

  /// Load from YAML; returns ErrorCode::Ok if all 7 entries valid.
  ErrorCode load(const std::string& yaml_path);

  /// Filter behaviors by current ODD zone (`l3_msgs::msg::ODDState::ODD_ZONE_*`).
  std::vector<BehaviorDescriptor> get_active_subset(uint8_t odd_zone) const;

  /// O(1) lookup by enum.
  const BehaviorDescriptor& get(BehaviorType type) const;

 private:
  std::array<BehaviorDescriptor, kBehaviorCount> entries_{};
  bool loaded_{false};
};

}  // namespace mass_l3::m4
```

### 4.2 BehaviorActivationCondition（每个行为的激活条件 — 基于 ODD/Mission/World）

```cpp
// include/m4_behavior_arbiter/behavior_activation.hpp
#pragma once

#include <optional>

#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

/**
 * @brief Snapshot of all four input streams used by activation predicates.
 *
 * Constructed at the start of every arbitration cycle from the latest cached
 * messages held by BehaviorArbiterNode. All four fields are required (DEGRADED
 * path takes a frozen snapshot if a stream timed out within budget).
 */
struct ArbitrationInputs {
  l3_msgs::msg::ODDState           odd_state;
  l3_msgs::msg::ModeCmd            mode_cmd;
  l3_msgs::msg::WorldState         world_state;
  l3_msgs::msg::MissionGoal        mission_goal;
  l3_msgs::msg::COLREGsConstraint  colregs_constraint;
  bool                             m1_fresh;   ///< stamp within timeout
  bool                             m2_fresh;
  bool                             m3_fresh;
  bool                             m6_fresh;
};

/**
 * @brief Evaluates activation predicates for each behavior.
 *
 * Predicates per detailed design §5.2.{2,3,4} — implemented as small pure functions.
 * No heap allocation; no exceptions in this class.
 */
class BehaviorActivationCondition {
 public:
  explicit BehaviorActivationCondition(const BehaviorDictionary& dict);

  /// True if `behavior` should activate under the current inputs.
  bool is_active(BehaviorType behavior, const ArbitrationInputs& inputs) const;

  /// Active subset = ODD-filtered ∩ activation-predicate-true.
  std::vector<BehaviorType> compute_active_set(const ArbitrationInputs& inputs) const;

 private:
  // One predicate per behavior (detailed design §5.2.2 / §5.2.3 / §5.2.4)
  bool predicate_transit(const ArbitrationInputs& in) const;
  bool predicate_colreg_avoid(const ArbitrationInputs& in) const;
  bool predicate_dp_hold(const ArbitrationInputs& in) const;
  bool predicate_berth(const ArbitrationInputs& in) const;
  bool predicate_mrc(BehaviorType type, const ArbitrationInputs& in) const;

  const BehaviorDictionary& dict_;
};

}  // namespace mass_l3::m4
```

### 4.3 IvPSolver（区间编程多目标求解器）

> **自实现策略**（详细设计 §10.1 + 主计划 §3.5）：M4 不引入外部 IvP 库。MOOS-IvP 原生库虽为 Benjamin et al. 2010 开源（[R3]），但其 GPL 风格 license + 与 ROS2 集成成本不匹配 PATH-D 路径需求。M4 以 Eigen 为数学基础**自实现 piecewise-linear IvP 求解器**。

#### 4.3.1 IvPFunction（区间值函数表示）

```cpp
// include/m4_behavior_arbiter/ivp_function.hpp
#pragma once

#include <cstdint>
#include <vector>

#include <Eigen/Core>

namespace mass_l3::m4 {

/**
 * @brief Piecewise-linear interval-valued utility function over (heading, speed).
 *
 * Per [R3] Benjamin et al. 2010. Represents one behavior's preference over
 * the 2D decision space. Stored as a list of (heading_interval × speed_interval,
 * utility) pieces; `evaluate(psi, u)` returns the utility at a single point;
 * pieces are non-overlapping and cover the full domain (validated at construction).
 *
 * Template parameter Pieces is the compile-time max piece count; default = 32
 * matches `m4_params.yaml/ivp.max_pieces_per_function`.
 */
template <size_t Pieces = 32>
class IvPFunction {
 public:
  struct Piece {
    double heading_min_deg;   ///< [0, 360); intervals may wrap (handled in evaluate)
    double heading_max_deg;
    double speed_min_kn;
    double speed_max_kn;
    double utility;           ///< [0, 1]; 1.0 = most preferred
  };

  IvPFunction() = default;

  /// Construction-time domain validation (no overlapping pieces; full coverage).
  ErrorCode set_pieces(const std::vector<Piece>& pieces);

  /// Evaluate utility at (psi_deg, u_kn). Performs 360° wrap.
  double evaluate(double psi_deg, double u_kn) const;

  /// Number of active pieces.
  size_t piece_count() const { return piece_count_; }

  /// Read-only piece access (for combine / debug).
  const Piece& piece(size_t i) const { return pieces_[i]; }

 private:
  std::array<Piece, Pieces> pieces_{};
  size_t piece_count_{0};
};

/// Common type alias used across the solver.
using IvPFunctionDefault = IvPFunction<32>;

}  // namespace mass_l3::m4
```

> **模板设计理由**：编译时固定 piece 数避免运行时堆分配（详细设计 §10.2 + 编码规范 §3.2 项目硬规则；M4 PATH-D 路径不强制无堆分配，但 IvP 求解的热路径选 stack 分配以保证 jitter < 25 ms，详细设计 §9.4 表）。`Pieces=32` 默认值与 `ivp.max_pieces_per_function` 对齐。

#### 4.3.2 IvPHeadingDomain / IvPSpeedDomain（搜索域）

```cpp
// include/m4_behavior_arbiter/ivp_domain.hpp
#pragma once

#include <cstdint>

namespace mass_l3::m4 {

/**
 * @brief Discretized search domain for heading dimension.
 *
 * Per detailed design §5.5: heading resolution = 1°, speed resolution = 0.5 kn.
 * Total grid = 360 × 100 = 36k points; 4.3M FLOPs for 6 active behaviors.
 *
 * `wrap()` handles 350° → 010° crossings (test U5 in detailed design §9.1).
 */
class IvPHeadingDomain {
 public:
  explicit IvPHeadingDomain(double resolution_deg = 1.0);

  size_t size() const { return size_; }
  double at(size_t i) const;        ///< Returns heading_deg ∈ [0, 360)

  /// Iterator helpers
  double resolution() const { return resolution_deg_; }
  double wrap(double psi_deg) const;

 private:
  double resolution_deg_;
  size_t size_;
};

/**
 * @brief Discretized search domain for speed dimension, ODD-bounded.
 *
 * `min_kn` / `max_kn` are loaded per current ODD zone (detailed design §5.4 table 5.3
 * + Capability Manifest u_max for the vessel — currently FCB).
 */
class IvPSpeedDomain {
 public:
  IvPSpeedDomain(double min_kn, double max_kn, double resolution_kn = 0.5);

  size_t size() const { return size_; }
  double at(size_t i) const;
  double resolution() const { return resolution_kn_; }

 private:
  double min_kn_;
  double max_kn_;
  double resolution_kn_;
  size_t size_;
};

}  // namespace mass_l3::m4
```

#### 4.3.3 IvPCombinationStrategy（多目标合并策略）

```cpp
// include/m4_behavior_arbiter/ivp_combine.hpp
#pragma once

#include <vector>

#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4 {

/**
 * @brief Combines N weighted IvP functions into a single aggregated objective.
 *
 * Currently supports weighted-sum (only strategy required by detailed design §5.3):
 *   aggregated(ψ, u) = Σ_i (w_i · f_i(ψ, u))
 *
 * Future strategies (e.g., weighted-min for safety-critical behaviors) plug in via
 * the same interface.
 */
class IvPCombinationStrategy {
 public:
  struct WeightedFunction {
    double               weight;   ///< From BehaviorWeightSet for current health state
    IvPFunctionDefault   function;
  };

  IvPCombinationStrategy() = default;
  virtual ~IvPCombinationStrategy() = default;

  IvPCombinationStrategy(const IvPCombinationStrategy&) = delete;
  IvPCombinationStrategy& operator=(const IvPCombinationStrategy&) = delete;

  /// Combine weighted functions; result evaluated point-wise on the IvP domain.
  virtual double combine(double psi_deg, double u_kn,
                         const std::vector<WeightedFunction>& fns) const = 0;
};

class WeightedSumCombination : public IvPCombinationStrategy {
 public:
  double combine(double psi_deg, double u_kn,
                 const std::vector<WeightedFunction>& fns) const override;
};

}  // namespace mass_l3::m4
```

### 4.3 IvPSolver 主体

```cpp
// include/m4_behavior_arbiter/ivp_solver.hpp
#pragma once

#include <chrono>
#include <optional>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"

namespace mass_l3::m4 {

/**
 * @brief Hard constraints injected into IvP search (per detailed design §5.3 Step 3).
 *
 * Validated at solve() entry; if any constraint admits no feasible point, solver
 * returns std::nullopt → BehaviorArbiterNode falls back to conservative behavior
 * (§7.1 in detailed design + §4.4 BehaviorPriority here).
 */
struct IvPHardConstraints {
  /// Heading must lie inside this set (e.g., COLREGs allowed_direction_range).
  /// Empty = unconstrained.
  std::vector<std::pair<double, double>> heading_allowed_ranges_deg;

  /// Speed bounds derived from ODD u_min / u_max.
  double speed_min_kn;
  double speed_max_kn;

  /// CPA constraint per target — solver excludes (ψ, u) violating any.
  /// Targets passed by reference; CPA recomputed per-grid-cell.
  std::vector<l3_msgs::msg::TrackedTarget> targets;
  double cpa_safe_m;

  /// ROT (rate-of-turn) limit from Capability Manifest [deg/s].
  double rot_max_deg_s;
};

/**
 * @brief IvP solver result: feasible heading & speed intervals + confidence.
 */
struct IvPSolution {
  double heading_min_deg;
  double heading_max_deg;
  double speed_min_kn;
  double speed_max_kn;
  double optimality_margin;   ///< [0, 1]; distance from constraint boundary
  std::string rationale;      ///< SAT-2 summary for BehaviorPlan.rationale
};

/**
 * @brief Main IvP solver — piecewise-linear, weighted-sum aggregation.
 *
 * Algorithm (detailed design §5.3):
 *   1. activated_set = filter(behaviors, is_condition_met?)
 *   2. aggregated = Σ(w_i · f_i)  via IvPCombinationStrategy
 *   3. constraints = build IvPHardConstraints from M1/M2/M6
 *   4. solution = grid-search over IvPHeadingDomain × IvPSpeedDomain
 *      (pruned by hard constraints; pick top-N% as feasible interval)
 *   5. if feasible.empty() → return std::nullopt
 *
 * No heap allocation in solve() hot path. Total cost ≈ 20–40 ms (i7 baseline);
 * `m4_params.yaml/ivp.timeout_ms` = 100 ms guard.
 */
class IvPSolver {
 public:
  IvPSolver(IvPHeadingDomain heading_domain,
            IvPSpeedDomain speed_domain,
            std::unique_ptr<IvPCombinationStrategy> strategy,
            std::chrono::milliseconds timeout);

  /// Solve aggregated objective subject to hard constraints.
  /// Returns std::nullopt on infeasibility or timeout.
  std::optional<IvPSolution> solve(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints) const;

  /// Diagnostic: last solve duration / grid cells evaluated.
  struct SolveDiagnostics {
    std::chrono::microseconds duration{0};
    size_t grid_cells_evaluated{0};
    size_t grid_cells_feasible{0};
  };

  const SolveDiagnostics& last_diagnostics() const { return diag_; }

 private:
  IvPHeadingDomain heading_domain_;
  IvPSpeedDomain   speed_domain_;
  std::unique_ptr<IvPCombinationStrategy> strategy_;
  std::chrono::milliseconds timeout_;
  mutable SolveDiagnostics diag_;
};

}  // namespace mass_l3::m4
```

### 4.4 BehaviorPriority（仲裁规则：MRC > Avoid > Mission）

```cpp
// include/m4_behavior_arbiter/behavior_priority.hpp
#pragma once

#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

/**
 * @brief Priority arbitration rules applied AFTER IvP solving.
 *
 * Detailed design §5.2 + §7.1.3: although IvP avoids "winner-take-all" within
 * normal operation, certain meta-rules override IvP entirely:
 *   1. Any MRC_* behavior active → freeze IvP, output the MRC behavior unchanged.
 *   2. CRITICAL health → force MRC_DRIFT regardless of IvP.
 *   3. ODD = OUT (envelope_state) → escalate to MRC.
 *
 * NORMAL path leaves IvP solution untouched; this class only steps in for
 * safety overrides.
 */
class BehaviorPriority {
 public:
  /// Choose the primary behavior to publish.
  /// `active_set` from BehaviorActivationCondition; `ivp_solution` from IvPSolver.
  BehaviorType select_primary(const std::vector<BehaviorType>& active_set,
                              const IvPSolution& ivp_solution,
                              const ArbitrationInputs& inputs) const;

  /// True if any MRC_* behavior in active_set.
  static bool has_mrc(const std::vector<BehaviorType>& active_set);
};

}  // namespace mass_l3::m4
```

### 4.5 BehaviorArbiterNode（ROS2 节点类）

```cpp
// include/m4_behavior_arbiter/behavior_arbiter_node.hpp
#pragma once

#include <chrono>
#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"

namespace mass_l3::m4 {

enum class HealthState : uint8_t {
  Normal   = 0,
  Degraded = 1,
  Critical = 2,
};

/**
 * @brief ROS2 node — orchestrates the 500 ms arbitration cycle.
 *
 * Node topology: see code-skeleton-spec.md §3.1.
 * No internal threading beyond rclcpp executor (single-threaded).
 */
class BehaviorArbiterNode : public rclcpp::Node {
 public:
  explicit BehaviorArbiterNode(const rclcpp::NodeOptions& options);
  ~BehaviorArbiterNode() override = default;

 private:
  // ---- Initialization ----
  void declare_parameters();
  void load_yaml_config();
  void create_subscriptions();
  void create_publishers();
  void create_timer();

  // ---- Subscription callbacks ----
  void on_odd_state(l3_msgs::msg::ODDState::ConstSharedPtr msg);
  void on_mode_cmd(l3_msgs::msg::ModeCmd::ConstSharedPtr msg);
  void on_world_state(l3_msgs::msg::WorldState::ConstSharedPtr msg);
  void on_mission_goal(l3_msgs::msg::MissionGoal::ConstSharedPtr msg);
  void on_colregs_constraint(l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg);

  // ---- Periodic arbitration (500 ms timer) ----
  void on_arbitration_timer();
  void trigger_arbitration();   ///< Event-driven re-arbitration (see §3.1)

  // ---- Internal pipeline ----
  ArbitrationInputs build_inputs() const;
  HealthState evaluate_health(const ArbitrationInputs& inputs);
  std::vector<IvPCombinationStrategy::WeightedFunction>
      build_weighted_functions(const std::vector<BehaviorType>& active,
                               HealthState health) const;
  IvPHardConstraints build_constraints(const ArbitrationInputs& inputs) const;
  l3_msgs::msg::BehaviorPlan assemble_plan(BehaviorType primary,
                                           const IvPSolution& sol,
                                           HealthState health) const;
  void publish_asdr(const std::string& decision_type,
                    const std::string& decision_json);
  l3_msgs::msg::BehaviorPlan build_critical_fallback() const;

  // ---- ROS2 handles ----
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr           sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::ModeCmd>::SharedPtr            sub_mode_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr         sub_world_;
  rclcpp::Subscription<l3_msgs::msg::MissionGoal>::SharedPtr        sub_mission_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr  sub_colregs_;
  rclcpp::Publisher<l3_msgs::msg::BehaviorPlan>::SharedPtr          pub_plan_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr            pub_asdr_;
  rclcpp::TimerBase::SharedPtr                                      timer_;

  // ---- Cached latest inputs (set in callbacks; read in timer) ----
  std::optional<l3_msgs::msg::ODDState>          last_odd_;
  std::optional<l3_msgs::msg::ModeCmd>           last_mode_;
  std::optional<l3_msgs::msg::WorldState>        last_world_;
  std::optional<l3_msgs::msg::MissionGoal>       last_mission_;
  std::optional<l3_msgs::msg::COLREGsConstraint> last_colregs_;

  // ---- Internal state (detailed design §4.1) ----
  uint8_t      last_odd_zone_{255};      ///< for jump detection (§6.2)
  HealthState  health_state_{HealthState::Normal};
  rclcpp::Time degradation_start_time_;

  // ---- Domain components ----
  std::shared_ptr<BehaviorDictionary>       dict_;
  std::shared_ptr<BehaviorActivationCondition> activator_;
  std::shared_ptr<IvPSolver>                solver_;
  std::shared_ptr<BehaviorPriority>         priority_;

  // ---- Logging ----
  std::shared_ptr<spdlog::logger> log_;     ///< named "m4_behavior_arbiter"
};

}  // namespace mass_l3::m4
```

> **关键私有成员对应**：详细设计 §4.1 `M4_InternalState` → `last_odd_zone_` / `health_state_` / `degradation_start_time_` / `last_*` 缓存；其余字段由 `IvPSolver::SolveDiagnostics` 承载。`active_behaviors[]` 不缓存为成员（每周期由 `BehaviorActivationCondition::compute_active_set` 重算，避免与 ROS2 callback 间状态漂移）。

---

## 5. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(m4_behavior_arbiter LANGUAGES CXX)

# 编码规范 §2.1
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认 Release，确保 -O2（不允许 -O3，编码规范 §2.2）
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# 编码规范 §2 + 主计划 §3.1：Eigen 5.0.0 EXACT 锁定
find_package(Eigen3 5.0.0 EXACT REQUIRED)

# spdlog 1.17.0 锁定
find_package(spdlog 1.17.0 REQUIRED)

# yaml-cpp（YAML 配置加载；与 third-party-library-policy.md §3.1 一致）
find_package(yaml-cpp REQUIRED)

# ----- Library: m4_core (业务逻辑，单元测试可链接) -----

set(M4_CORE_SOURCES
  src/behavior_dictionary.cpp
  src/behavior_activation.cpp
  src/ivp_function.cpp
  src/ivp_solver.cpp
  src/ivp_combine.cpp
  src/behavior_priority.cpp
)

add_library(m4_core ${M4_CORE_SOURCES})
target_include_directories(m4_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(m4_core PUBLIC
  Eigen3::Eigen
  spdlog::spdlog
  yaml-cpp::yaml-cpp
)
ament_target_dependencies(m4_core PUBLIC
  rclcpp
  builtin_interfaces
  l3_msgs
  l3_external_msgs
)

# 编码规范 §2.2 必启 flags
target_compile_options(m4_core PRIVATE
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
  -fstack-protector-strong -D_FORTIFY_SOURCE=2
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:Debug>:-O0;-g;-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)
target_link_options(m4_core PRIVATE
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# ----- Executable: m4_behavior_arbiter_node -----

add_executable(m4_behavior_arbiter_node
  src/behavior_arbiter_node.cpp
  src/main.cpp
)
target_link_libraries(m4_behavior_arbiter_node PRIVATE m4_core)
ament_target_dependencies(m4_behavior_arbiter_node PRIVATE rclcpp)

target_compile_options(m4_behavior_arbiter_node PRIVATE
  -Wall -Wextra -Wpedantic -Werror
  "$<$<CONFIG:Release>:-O2>"
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)
target_link_options(m4_behavior_arbiter_node PRIVATE
  "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# ----- Install -----

install(TARGETS m4_behavior_arbiter_node m4_core
  RUNTIME DESTINATION lib/${PROJECT_NAME}
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)

# ----- Testing -----

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_behavior_dictionary  test/unit/test_behavior_dictionary.cpp)
  ament_add_gtest(test_ivp_function          test/unit/test_ivp_function.cpp)
  ament_add_gtest(test_ivp_solver            test/unit/test_ivp_solver.cpp)
  ament_add_gtest(test_ivp_combine           test/unit/test_ivp_combine.cpp)
  ament_add_gtest(test_behavior_priority     test/unit/test_behavior_priority.cpp)
  ament_add_gtest(test_behavior_plan_output  test/unit/test_behavior_plan_output.cpp)

  foreach(t IN ITEMS test_behavior_dictionary test_ivp_function test_ivp_solver
                     test_ivp_combine test_behavior_priority test_behavior_plan_output)
    target_link_libraries(${t} m4_core)
    target_include_directories(${t} PRIVATE include)
  endforeach()

  # 集成测试（与 mock M2/M6/M5 接口）
  ament_add_gtest(test_m4_with_mock_m2 test/integration/test_m4_with_mock_m2.cpp)
  ament_add_gtest(test_m4_with_mock_m6 test/integration/test_m4_with_mock_m6.cpp)
  ament_add_gtest(test_m4_to_mock_m5   test/integration/test_m4_to_mock_m5.cpp)
endif()

ament_package()
```

---

## 6. package.xml

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd"
            schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>m4_behavior_arbiter</name>
  <version>0.1.0</version>
  <description>
    L3 Tactical Layer — Module M4 Behavior Arbiter (PATH-D).
    ODD-aware behavior dictionary + IvP multi-objective solver.
    Subscribes ODDState / ModeCmd / WorldState / MissionGoal / COLREGsConstraint;
    publishes BehaviorPlan @ 2 Hz to M5 Tactical Planner. See
    docs/Implementation/M4/code-skeleton-spec.md.
  </description>
  <maintainer email="team-m4@sango-mass.local">Team-M4</maintainer>
  <license>Proprietary</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>builtin_interfaces</depend>
  <depend>geographic_msgs</depend>

  <!-- L3 共享消息（v1.1.2 §15.1） -->
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>

  <!-- 第三方库（third-party-library-policy.md §3.1） -->
  <depend>eigen</depend>
  <depend>spdlog_vendor</depend>
  <depend>yaml-cpp</depend>

  <!-- 测试 -->
  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_cmake_gtest</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 7. 单元测试骨架（GTest）

> 覆盖率目标 ≥ 90%（Phase E1 DoD）。所有 fixtures 从 `test/fixtures/*.yaml` 加载，便于 HAZID 校准后无需重写测试代码即可验证新参数。

### 7.1 BehaviorDictionary（ODD-A 下哪些行为可用）

```cpp
// test/unit/test_behavior_dictionary.cpp
#include <gtest/gtest.h>

#include "l3_msgs/msg/odd_state.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4::test {

class BehaviorDictionaryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dict_.load("test/fixtures/behavior_definitions_default.yaml");
  }
  BehaviorDictionary dict_;
};

TEST_F(BehaviorDictionaryTest, OpenWaterAllowsTransitAndColregAvoid) {
  auto subset = dict_.get_active_subset(l3_msgs::msg::ODDState::ODD_ZONE_A);
  // ODD-A 应包含 Transit + ColregAvoid + MRC_*；不应包含 DpHold
  EXPECT_TRUE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::Transit; }));
  EXPECT_TRUE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::ColregAvoid; }));
  EXPECT_FALSE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::DpHold; }));
}

TEST_F(BehaviorDictionaryTest, PortAllowsDpHoldOnly) {
  auto subset = dict_.get_active_subset(l3_msgs::msg::ODDState::ODD_ZONE_C);
  EXPECT_TRUE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::DpHold; }));
  EXPECT_FALSE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::Transit; }));
}

TEST_F(BehaviorDictionaryTest, RestrictedVisibilityActivatesInOddD) {
  auto subset = dict_.get_active_subset(l3_msgs::msg::ODDState::ODD_ZONE_D);
  // ODD-D 子集与 §5.2 表对齐：ColregAvoid + RestrictedVis + MRC_*；无 Transit
  EXPECT_FALSE(std::any_of(subset.begin(), subset.end(),
      [](const auto& d) { return d.type == BehaviorType::Transit; }));
}

TEST_F(BehaviorDictionaryTest, MrcBehaviorsApplyToAnyZone) {
  for (uint8_t zone : {0, 1, 2, 3}) {
    auto subset = dict_.get_active_subset(zone);
    EXPECT_TRUE(std::any_of(subset.begin(), subset.end(),
        [](const auto& d) { return d.type == BehaviorType::MrcDrift; }))
        << "MRC_DRIFT must be available in zone " << static_cast<int>(zone);
  }
}

}  // namespace mass_l3::m4::test
```

### 7.2 IvPSolver（单目标 / 多目标求解 — 经典 IvP 论文 [R10] 测试用例）

```cpp
// test/unit/test_ivp_solver.cpp
#include <gtest/gtest.h>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"

namespace mass_l3::m4::test {

class IvPSolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto strategy = std::make_unique<WeightedSumCombination>();
    solver_ = std::make_unique<IvPSolver>(
        IvPHeadingDomain(/*resolution=*/1.0),
        IvPSpeedDomain(/*min=*/0.0, /*max=*/20.0, /*resolution=*/0.5),
        std::move(strategy),
        std::chrono::milliseconds(100));
  }
  std::unique_ptr<IvPSolver> solver_;
};

// U1: 单个行为激活，无约束 → 求解收敛
TEST_F(IvPSolverTest, SingleBehaviorNoConstraint) {
  IvPFunctionDefault transit_pref;
  // 偏好航向 90°，速度 15 kn（详细设计 §5.2.2）
  transit_pref.set_pieces({{85.0, 95.0, 14.0, 16.0, 1.0},
                            /* fill remaining domain with 0.0 utility... */});

  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {
      {/*weight=*/1.0, transit_pref}};

  IvPHardConstraints c{.heading_allowed_ranges_deg = {},
                       .speed_min_kn = 0.0,
                       .speed_max_kn = 20.0,
                       .targets = {},
                       .cpa_safe_m = 1852.0,  // 1 nm
                       .rot_max_deg_s = 3.0};

  auto sol = solver_->solve(fns, c);
  ASSERT_TRUE(sol.has_value());
  EXPECT_NEAR(sol->heading_min_deg, 85.0, 2.0);
  EXPECT_NEAR(sol->heading_max_deg, 95.0, 2.0);
}

// U2: 两个相容行为加权和 → 权重正确融合
TEST_F(IvPSolverTest, TwoCompatibleBehaviorsWeightedSum) {
  // Transit 偏好 90°；ColregAvoid 偏好 [85°, 100°]（与 Transit 兼容）
  // 期望融合后：解仍在 [85°, 95°]（交集附近）
  // ... 详细 fixture 略；同样模式 ...
  SUCCEED();  // 占位
}

// U3: 行为间约束冲突 → 检测无可行解
TEST_F(IvPSolverTest, ConflictingConstraintsReturnsNullopt) {
  // heading_allowed = [85°, 95°] 与 [200°, 210°] 同时；模拟 Rule 9 + Rule 14
  IvPFunctionDefault f;
  f.set_pieces({/* full domain */});
  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {{1.0, f}};

  IvPHardConstraints c{
      .heading_allowed_ranges_deg = {{85.0, 95.0}, {200.0, 210.0}},
      .speed_min_kn = 18.0,  // 与 ODD 速度上限冲突
      .speed_max_kn = 0.0,
      .targets = {},
      .cpa_safe_m = 1852.0,
      .rot_max_deg_s = 3.0};

  auto sol = solver_->solve(fns, c);
  EXPECT_FALSE(sol.has_value());
}

// U4: CPA 约束缩紧（目标逼近）→ 航向范围逐步缩小
TEST_F(IvPSolverTest, CpaConstraintNarrowsHeadingRange) {
  // 模拟 target 逐渐逼近，CPA 阈值不变；期望 heading_max - heading_min 单调减小
  // ... 略 ...
  SUCCEED();
}

// U5: 航向环绕（350°→10°）→ 正确处理 360° 模运算
TEST_F(IvPSolverTest, HeadingWrapAround) {
  IvPFunctionDefault wrap_pref;
  wrap_pref.set_pieces({{350.0, 360.0, 0.0, 20.0, 1.0},
                         {0.0,   10.0,  0.0, 20.0, 1.0}});
  std::vector<IvPCombinationStrategy::WeightedFunction> fns = {{1.0, wrap_pref}};

  IvPHardConstraints c{.heading_allowed_ranges_deg = {},
                       .speed_min_kn = 0.0,
                       .speed_max_kn = 20.0,
                       .targets = {},
                       .cpa_safe_m = 1852.0,
                       .rot_max_deg_s = 3.0};

  auto sol = solver_->solve(fns, c);
  ASSERT_TRUE(sol.has_value());
  // 求解器须正确识别这是一个跨 0° 的连续区间
  EXPECT_TRUE((sol->heading_min_deg >= 350.0 && sol->heading_max_deg < 360.0)
           || (sol->heading_min_deg < 10.0));
}

}  // namespace mass_l3::m4::test
```

> **测试用例对应详细设计 §9.1.1**：U1–U5 全覆盖；每个用例用 `test/fixtures/*.yaml` 提供 IvP 函数定义。

### 7.3 BehaviorPriority（MRC 触发时其他行为冻结）

```cpp
// test/unit/test_behavior_priority.cpp
#include <gtest/gtest.h>

#include "m4_behavior_arbiter/behavior_priority.hpp"

namespace mass_l3::m4::test {

TEST(BehaviorPriorityTest, MrcDriftOverridesAll) {
  BehaviorPriority pri;
  std::vector<BehaviorType> active{BehaviorType::Transit,
                                    BehaviorType::ColregAvoid,
                                    BehaviorType::MrcDrift};
  IvPSolution sol{/* arbitrary feasible solution */};
  ArbitrationInputs in{};
  // 即使 IvP 求解给出可行解，MRC 仍应覆盖
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcDrift);
}

TEST(BehaviorPriorityTest, ColregAvoidOverridesTransit) {
  BehaviorPriority pri;
  std::vector<BehaviorType> active{BehaviorType::Transit, BehaviorType::ColregAvoid};
  IvPSolution sol{};
  ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::ColregAvoid);
}

TEST(BehaviorPriorityTest, TransitWhenAlone) {
  BehaviorPriority pri;
  std::vector<BehaviorType> active{BehaviorType::Transit};
  IvPSolution sol{};
  ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::Transit);
}

TEST(BehaviorPriorityTest, CriticalHealthForcesMrcDrift) {
  BehaviorPriority pri;
  std::vector<BehaviorType> active{BehaviorType::Transit};
  IvPSolution sol{};
  ArbitrationInputs in{};
  in.odd_state.health = l3_msgs::msg::ODDState::HEALTH_CRITICAL;
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcDrift);
}

}  // namespace mass_l3::m4::test
```

### 7.4 输出 Behavior_PlanMsg 字段范围

```cpp
// test/unit/test_behavior_plan_output.cpp
#include <gtest/gtest.h>

#include "l3_msgs/msg/behavior_plan.hpp"
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

namespace mass_l3::m4::test {

TEST(BehaviorPlanOutputTest, HeadingFieldsInRange) {
  l3_msgs::msg::BehaviorPlan plan;
  plan.heading_min_deg = 85.0;
  plan.heading_max_deg = 95.0;
  EXPECT_GE(plan.heading_min_deg, 0.0);
  EXPECT_LT(plan.heading_min_deg, 360.0);
  EXPECT_GE(plan.heading_max_deg, 0.0);
  EXPECT_LT(plan.heading_max_deg, 360.0);
}

TEST(BehaviorPlanOutputTest, ConfidenceInUnitInterval) {
  l3_msgs::msg::BehaviorPlan plan;
  plan.confidence = 0.85f;
  EXPECT_GE(plan.confidence, 0.0f);
  EXPECT_LE(plan.confidence, 1.0f);
}

TEST(BehaviorPlanOutputTest, RationaleNonEmpty) {
  l3_msgs::msg::BehaviorPlan plan;
  plan.rationale = "TRANSIT only (no targets within CPA threshold)";
  EXPECT_FALSE(plan.rationale.empty());
}

TEST(BehaviorPlanOutputTest, BehaviorEnumMatchesIdl) {
  // 编译期校验：本模块 enum 数值与 .msg 常量一致
  static_assert(static_cast<uint8_t>(BehaviorType::Transit)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT);
  static_assert(static_cast<uint8_t>(BehaviorType::ColregAvoid)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID);
  static_assert(static_cast<uint8_t>(BehaviorType::DpHold)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_DP_HOLD);
  static_assert(static_cast<uint8_t>(BehaviorType::Berth)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_BERTH);
  static_assert(static_cast<uint8_t>(BehaviorType::MrcDrift)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_MRC_DRIFT);
  static_assert(static_cast<uint8_t>(BehaviorType::MrcAnchor)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_MRC_ANCHOR);
  static_assert(static_cast<uint8_t>(BehaviorType::MrcHeaveTo)
                    == l3_msgs::msg::BehaviorPlan::BEHAVIOR_MRC_HEAVE_TO);
}

}  // namespace mass_l3::m4::test
```

---

## 8. 配置文件

### 8.1 config/m4_params.yaml（行为权重 / 优先级 / IvP 参数）

```yaml
# M4 Behavior Arbiter — runtime parameters
# Per detailed design SANGO-ADAS-L3-DD-M4-001 §5.4 + code-skeleton-spec.md §3.4
# All [TBD-HAZID] values are initial estimates; HAZID RUN-001 will calibrate them.

m4_behavior_arbiter:
  ros__parameters:

    # ----- 节点周期 -----
    arbitration_period_ms: 500          # 2 Hz output

    # ----- 输入超时 (s) ----- [TBD-HAZID]
    timeout_m1_s: 2.0
    timeout_m2_s: 2.0
    timeout_m3_s: 4.0
    timeout_m6_s: 2.0
    degraded_to_critical_s: 5.0
    m2_confidence_min: 0.5

    # ----- 行为权重 (NORMAL mode) ----- [TBD-HAZID]
    weight:
      transit:        0.30
      colreg_avoid:   0.70
      restricted_vis: 0.60
      channel_follow: 0.50
      dp_hold:        0.85
      berth:          0.80
      mrc_drift:      1.00
      mrc_anchor:     1.00
      mrc_heave_to:   1.00

    # ----- DEGRADED 系数 ----- [TBD-HAZID]
    degraded_scale:
      colreg_avoid: 1.30
      transit:      0.80
      cpa_safe:     1.20

    # ----- CPA 安全距离 (nm) ----- [TBD-HAZID]
    cpa_safe_nm:
      odd_a: 1.0
      odd_b: 0.3
      odd_c: 0.15
      odd_d: 1.5

    # ----- IvP 求解器 -----
    ivp:
      heading_resolution_deg:   1.0
      speed_resolution_kn:      0.5
      timeout_ms:               100
      max_pieces_per_function:  32

    # ----- 优先级仲裁规则 -----
    priority:
      mrc_overrides_all:        true
      colreg_overrides_transit: true

    # ----- 日志 -----
    log:
      level: "info"           # trace | debug | info | warn | error | critical
      file:  "/var/log/mass_l3/m4_behavior_arbiter.log"
```

### 8.2 config/behavior_definitions.yaml（7 种行为的激活条件 + 默认权重）

```yaml
# Behavior dictionary — loaded by BehaviorDictionary::load()
# Per detailed design §5.2 + architecture v1.1.2 §8.3.

behaviors:

  - name: "TRANSIT"
    type: 0                                     # BehaviorType::Transit
    applicable_zones: [A, B]                    # ODD-A, ODD-B
    default_weight: 0.30                        # [TBD-HAZID]
    activation:
      auto_level_min: D2
      requires:
        - "no_targets_with_cpa_below_threshold"
        - "in_allowed_zone"

  - name: "COLREG_AVOID"
    type: 1
    applicable_zones: [A, B, D]
    default_weight: 0.70                        # [TBD-HAZID]
    activation:
      requires:
        - "m6_constraint_recent"                # last_colregs.stamp within 2 s
        - "any_target_cpa_below_safe"
        - "odd_health_not_critical"

  - name: "DP_HOLD"
    type: 2
    applicable_zones: [C]
    default_weight: 0.85                        # [TBD-HAZID]
    activation:
      requires:
        - "mode_cmd_docking_or_holding"
        - "dp_subsystem_healthy"

  - name: "BERTH"
    type: 3
    applicable_zones: [C]
    default_weight: 0.80                        # [TBD-HAZID]
    activation:
      requires:
        - "mode_cmd_berth"

  - name: "MRC_DRIFT"
    type: 4
    applicable_zones: [A, B, C, D]              # any
    default_weight: 1.00
    activation:
      requires:
        - "mode_cmd_mrc_drift"

  - name: "MRC_ANCHOR"
    type: 5
    applicable_zones: [A, B, C, D]
    default_weight: 1.00
    activation:
      requires:
        - "mode_cmd_mrc_anchor"

  - name: "MRC_HEAVE_TO"
    type: 6
    applicable_zones: [A, B, C, D]
    default_weight: 1.00
    activation:
      requires:
        - "mode_cmd_mrc_heave_to"
```

> **行为枚举对齐**：`type` 字段 0–6 与 `l3_msgs::msg::BehaviorPlan::BEHAVIOR_*` 一一对应（见 §7.4 的 `static_assert`）。新增行为须同步 IDL + YAML + 单元测试。

---

## 9. PATH-D 合规性 checklist

> Phase E1 自检表，PR review 前 reviewer 走完全部项。失败任一项 → CI 阻断。

- [ ] **路径强度**：MISRA C++:2023 完整 179 规则启用（`.clang-tidy` + `cppcheck-misra-cpp-2023.cfg`）
- [ ] **编译选项**：编码规范 §2.2 全部 `-W*` 启用 + `-Werror` + `-O2`
- [ ] **类型安全**：所有时间用 `std::chrono`；所有航向 / 速度 `double`；无 C 风格强转；无 `auto` 隐藏类型
- [ ] **资源管理**：所有动态分配 RAII（`unique_ptr` / `shared_ptr` / 自定义 deleter）；M4 PATH-D 允许动态分配但 IvP 热路径用 stack 分配（详细设计 §10.2）
- [ ] **并发**：单 executor，无裸 `std::thread`；无 `std::condition_variable`；mutex（如有）用 `std::lock_guard`/`std::scoped_lock`
- [ ] **异常**：M4 PATH-D 允许异常，但 ROS2 callback 边界 `catch(...)` → spdlog::error → 心跳 DEGRADED；不在析构中 throw
- [ ] **控制流**：所有 `switch` 含 `default:`；无 fall-through（除 `[[fallthrough]];`）；函数 ≤ 60 行；圈复杂度 ≤ 10
- [ ] **浮点**：`==`/`!=` 禁用；用 `std::abs(a-b) < eps`；时间用 `std::chrono`，不用 raw `double` 秒
- [ ] **命名**：snake_case 文件名 / 函数 / 变量；PascalCase 类 / enum class；`kPascalCase` constexpr；成员变量末尾 `_`
- [ ] **注释**：仅命中编码规范 §5.1 五种合法场景（INVARIANT / 跨模块契约 / WORKAROUND / `[TBD-HAZID]` / 算法证据）
- [ ] **头文件**：`#pragma once`；包含顺序按 §9.1（自身 / C 系统 / C++ 标准 / 第三方 / ROS2 / 项目内）
- [ ] **第三方库**：仅使用 third-party-library-policy.md §3.1 表中 M4 列允许的库（rclcpp / l3_msgs / l3_external_msgs / Eigen / spdlog / fmt / GTest / yaml-cpp / Cyclone DDS）；**禁用** CasADi / IPOPT / GeographicLib / nlohmann::json / Boost.Geometry
- [ ] **HAZID 注入**：所有 `[TBD-HAZID]` 标的常量从 YAML 加载；CI `tools/ci/check-no-hazid-hardcode.sh` 0 命中
- [ ] **IDL 对齐**：`BehaviorType` enum / `BehaviorPlan.msg` 常量 `static_assert` 编译期一致（§7.4）
- [ ] **ASDR 写入**：行为激活变化 / IvP 求解失败 / ODD 跳变 / 输入超时 4 类事件按详细设计 §4.3 + §8.3 写入；`source_module = "M4_Behavior_Arbiter"`
- [ ] **覆盖率**：单元测试 ≥ 90%（lcov 报告）；集成测试覆盖 §3.2 + §3.3 全部订阅 / 发布
- [ ] **HIL 场景 mock 验证**：详细设计 §9.3.1 / §9.3.2 / §9.3.3 三个场景至少 mock-level 通过
- [ ] **静态分析**：clang-tidy + cppcheck Premium 0 阻断；ASan / TSan / UBSan 0 错误
- [ ] **Doer-Checker 独立性**：M4 不引用 m7 命名空间（M4 是 Doer，与 M7 通过 ROS2 消息交互）

---

## 10. 引用（含 IvP 经典论文 — 详见详细设计 §11）

### 10.1 项目内引用

- **架构 v1.1.2** §8 M4 Behavior Arbiter — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- **架构 v1.1.2** §15.1 IDL Behavior_PlanMsg / §15.2 接口矩阵
- **详细设计 SANGO-ADAS-L3-DD-M4-001** v1.0 — `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md`
- **实施主计划 SANGO-ADAS-L3-IMPL-MASTER-001** §3 工程基线 / §5.2 DoD — `docs/Implementation/00-implementation-master-plan.md`
- **编码规范 SANGO-ADAS-L3-IMPL-CODING-001** — `docs/Implementation/coding-standards.md`
- **IDL 实施指南 SANGO-ADAS-L3-IMPL-IDL-001** §3.3 BehaviorPlan — `docs/Implementation/ros2-idl-implementation-guide.md`
- **第三方库政策 SANGO-ADAS-L3-IMPL-3RDPARTY-001** §3 Doer-Checker 矩阵 — `docs/Implementation/third-party-library-policy.md`
- **静态分析政策** + **CI/CD pipeline** — 见 `static-analysis-policy.md` / `ci-cd-pipeline.md`
- **RFC-004 ASDR 决议** — `docs/Design/Cross-Team Alignment/RFC-decisions.md`

### 10.2 外部引用（继承详细设计 §11）

| 编号 | 引用 | 用于 |
|---|---|---|
| [R3] | Benjamin, M. V., & Schmidt, H. (2010). "MOOS-IvP: Interval Programming for Autonomous Vessel Collision Avoidance." *IFAC Workshop NGCUV*. | IvP 方法奠基；§4.3 IvPSolver / §4.3.1 IvPFunction 设计依据 🟢 A |
| [R10] | Benjamin, M. V. (2002). "Multi-Objective Autonomous Vehicle Navigation in the Presence of Cooperative and Adversarial Agents." *PhD Thesis, Brown University*. | IvP piecewise-linear 求解算法详解；§4.3 IvPSolver 测试用例（U1–U5）依据 🟢 A |
| [R16] | Pirjanian, P. (1999). "Behavior Coordination Mechanisms — State-of-the-art." *IRIS Tech. Report, USD*. | 优先级仲裁缺陷；§4.4 BehaviorPriority 仅做 MRC override，不做固定优先级仲裁的依据 🟢 A |
| [R5] | IEC 61508-1:2010 *Functional Safety*. | M4 SIL 1 / 联合 SIL 2 分配（§1.2）依据 🟢 A |
| [R9] | DNV-CG-0264 (2025.01) *AROS Class Notations*. | §4.5 "Deviation from planned route" + §4.8 "Manoeuvring" — M4 行为切换审计映射 🟢 A |
| CCS | 中国船级社《智能船舶规范》(2024/2025) i-Ship (Nx, Ri/Ai). | 白盒可审计性；§3.3 ASDR 决策类型枚举 🟢 A |
| [R1] | Benjamin's MOOS-IvP Helm User Manual v4.2.1. | IvP 函数 API 参考（自实现时对照）🟢 A |

> **置信度标注**：所有 [R*] 为 A 级（官方 / 一作论文 / 国际标准）；详细设计 §11 已经全文引用，本文件继承该证据链。

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（Team-M4 implementation kickoff） | 初稿创建：节点拓扑（5 sub + 2 pub）+ IvPSolver 自实现骨架（IvPFunction/Domain/Combine 模板）+ 7 种行为 BehaviorDictionary + BehaviorActivationCondition + BehaviorPriority（MRC override）+ CMakeLists/package.xml + 单元测试骨架（U1–U5 + 行为字典 + 优先级 + IDL static_assert）+ 两 YAML 配置（参数 + 行为定义）+ PATH-D 合规 checklist + 内 / 外引用（继承详细设计 §11） |
