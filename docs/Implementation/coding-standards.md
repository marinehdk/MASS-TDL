# 编码规范（Coding Standards）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-CODING-001 |
| 版本 | v1.1（D1.4） |
| 日期 | 2026-05-14 |
| 状态 | 正式（CI 强制执行） |
| 主基线 | **MISRA C++:2023**（179 规则，覆盖 C++17）+ AUTOSAR C++14 设计指南补充 |
| 主语言 | C++17（GCC 14.3 LTS 基线 / Clang 20.1.8 多样化） |
| 副语言 | Python 3.10（PEP 8 + Ruff + mypy strict） |
| 适用范围 | 8 个 L3 模块（M1–M8）+ `l3_msgs` + `l3_external_msgs` 全部源代码 |
| 关联文件 | `00-implementation-master-plan.md` §3.2 / `static-analysis-policy.md` / `third-party-library-policy.md` / `ci-cd-pipeline.md` |

> **强制度**：本文 §3–§10 规则 **全部由 CI 阻断**。规则违反必须 fix 后重提 PR；豁免须走 `docs/Implementation/exemptions/` 单独审批（默认拒绝）。

---

## 1. ROS2 节点约定

### 1.1 命名空间

所有 C++ 代码必须放在 `mass_l3::m{N}` 嵌套命名空间下：

```cpp
namespace mass_l3::m6_colregs {

class ColregsReasonerNode : public rclcpp::Node {
  // ...
};

}  // namespace mass_l3::m6_colregs
```

| 模块 | 命名空间 |
|---|---|
| M1 | `mass_l3::m1_envelope` |
| M2 | `mass_l3::m2_world_model` |
| M3 | `mass_l3::m3_mission_manager` |
| M4 | `mass_l3::m4_behavior_arbiter` |
| M5 | `mass_l3::m5_tactical_planner` |
| M6 | `mass_l3::m6_colregs` |
| M7 | `mass_l3::m7_safety_supervisor` |
| M8 | `mass_l3::m8_hmi_transparency` |
| 公共工具 | `mass_l3::common` |

### 1.2 执行器模型

统一使用 `rclcpp::executors::SingleThreadedExecutor`：

```cpp
int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
```

**禁止** `MultiThreadedExecutor`（避免回调并发复杂度；多模块并行通过多进程实现）。

### 1.3 生命周期节点

M1 / M7 安全关键路径**强制**实现 `rclcpp_lifecycle::LifecycleNode`；M2–M6 / M8 推荐实现：

```cpp
class OddStateMachineNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit OddStateMachineNode(const rclcpp::NodeOptions& options)
    : LifecycleNode("m1_odd_state_machine", options) {}

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State& /*previous_state*/) override {
    // 分配资源、创建 publishers/subscribers
    return CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State& /*previous_state*/) override {
    // 激活 publishers
    return CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_deactivate(const rclcpp_lifecycle::State& /*previous_state*/) override {
    // 停止 publishers
    return CallbackReturn::SUCCESS;
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State& /*previous_state*/) override {
    // 释放资源
    return CallbackReturn::SUCCESS;
  }
};
```

### 1.4 QoS 配置

| 场景 | QoS 策略 | 示例 |
|---|---|---|
| **Subscriptions（默认）** | `rclcpp::QoS(10).reliable().volatile()` | `create_subscription<T>("/l3/m2/world_state", rclcpp::QoS(10).reliable().volatile(), callback)` |
| **Publications（默认）** | `rclcpp::QoS(N).reliable()` | `create_publisher<T>("/l3/m6/colregs_constraint", rclcpp::QoS(5).reliable())` |
| **传感器高频数据** | `rclcpp::SensorDataQoS().keep_last(K)` | `create_subscription<T>("/fusion/tracked_targets", rclcpp::SensorDataQoS().keep_last(2), callback)` |
| **持久化配置** | `rclcpp::QoS(1).reliable().transient_local()` | `create_publisher<T>("/l3/m1/odd_state", rclcpp::QoS(1).reliable().transient_local())` |

**规则**：
- Subscribers 默认 `reliable().volatile()`（接收最新消息，不保证历史）。
- Publishers 默认 `reliable()`（保证消息到达；不启用 durability，除非显式需要）。
- 高频传感器数据（> 10 Hz）用 `SensorDataQoS`（best-effort，允许丢包以降低延迟）。
- 配置类 topic（ODD state、参数）用 `transient_local`（新订阅者获取最后一条消息）。

### 1.5 Timer 创建模式

统一在 `setup_timers()` 方法中集中创建，使用 `std::chrono` 类型：

```cpp
void ColregsReasonerNode::setup_timers() {
  const auto kReasoningPeriod = std::chrono::milliseconds(
    get_parameter("reasoning_period_ms").as_int());

  reasoning_timer_ = create_wall_timer(
    kReasoningPeriod, [this]() { run_reasoning(); });
}
```

**禁止** raw `double` 秒数或 `int` 毫秒直接传入；必须显式转换为 `std::chrono::duration`。

---

## 2. 消息发布约定

### 2.1 强制字段

所有 L3 内部消息（`l3_msgs/msg/*.msg`）发布时必须填充以下字段：

```cpp
l3_msgs::msg::COLREGsConstraint msg;
msg.schema_version = "v1.1.2";          // 架构版本，锁定当前权威版本
msg.stamp = this->now();                // ROS2 时间戳
msg.confidence = 0.95;                  // ∈ [0.0, 1.0]；0 = 完全不可信，1 = 完全可信
msg.rationale = "Rule 15 triggered: target_42 crossing from starboard, "
                "CPA=0.3 NM, TCPA=180 s";  // 人类可读的决策理由
```

| 字段 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `schema_version` | `string` | `"v1.1.2"`（当前） | 消息 schema 版本，按架构主文件版本锁定 |
| `stamp` | `builtin_interfaces/Time` | `this->now()` | 消息生成时刻 |
| `confidence` | `float32` | `[0.0, 1.0]` | 决策/状态置信度；M7 用此 gate SOTIF 假设检查 |
| `rationale` | `string` | 非空（≥ 1 字符） | 决策理由；M8 聚合后向 ROC/船长展示 |

**违反检测**：CI 集成测试扫描所有 `publish()` 调用，验证上述四字段是否显式赋值；缺失 = 构建失败。

### 2.2 跨层消息

消费上游（L2、Fusion、Parameter DB）消息时，须校验 `schema_version` 兼容性：

```cpp
void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg) {
  if (msg->schema_version != "v1.1.2") {
    RCLCPP_WARN(get_logger(), "WorldState schema mismatch: expected v1.1.2, got %s",
                msg->schema_version.c_str());
    // 降级处理：仍使用消息，但标记置信度下降
  }
}
```

---

## 3. 文件命名约定

### 3.1 C++ 文件

| 类型 | 风格 | 示例 |
|---|---|---|
| **源文件** | `snake_case.cpp` | `colregs_reasoner_node.cpp` |
| **头文件** | `snake_case.hpp` | `colregs_reasoner_node.hpp` |
| **类名** | `PascalCase` | `ColregsReasonerNode` |
| **测试文件** | `test_snake_case.cpp` | `test_colregs_reasoner.cpp` |

### 3.2 ROS2 Package 命名

统一前缀 `m{N}_`：

| 模块 | Package 名 |
|---|---|
| M1 | `m1_odd_envelope_manager` |
| M2 | `m2_world_model` |
| M3 | `m3_mission_manager` |
| M4 | `m4_behavior_arbiter` |
| M5 | `m5_tactical_planner` |
| M6 | `m6_colregs_reasoner` |
| M7 | `m7_safety_supervisor` |
| M8 | `m8_hmi_transparency_bridge` |
| 消息 | `l3_msgs` |
| 外部消息 | `l3_external_msgs` |

### 3.3 Python 文件

| 类型 | 风格 | 示例 |
|---|---|---|
| **模块** | `snake_case.py` | `mock_publisher.py` |
| **测试** | `test_snake_case.py` | `test_mock_publisher.py` |

---

## 4. C++ 标准与静态分析

### 4.1 语言基线

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用 GNU 扩展（保证可移植）
```

### 4.2 路径强度分级

| 路径 | 模块 | 规则强度 | 静态分析门禁 |
|---|---|---|---|
| **PATH-S** 安全关键 | M1 / M7 | **MISRA C++:2023 完整 179 规则** + AUTOSAR C++14 设计指南 | Polyspace + Coverity + clang-tidy + cppcheck Premium |
| **PATH-D** 决策 | M2 / M3 / M4 / M5 / M6 | MISRA C++:2023 完整 179 规则 | clang-tidy + cppcheck Premium |
| **PATH-H** HMI | M8 | MISRA C++:2023 简化裁剪集（约 120 规则） | clang-tidy + cppcheck Premium 简化集 |

**理由**：M1 是模式仲裁中枢，M7 是 SOTIF + IEC 61508 双轨 Checker，二者均映射到 IEC 61508 SIL 2 核心安全功能（v1.1.2 §11.1）；规则强度上调到行业最严以匹配认证证据要求。

### 4.3 编译选项（强制）

```cmake
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
    -Werror                          # 警告即错误
    -Wshadow -Wconversion -Wsign-conversion
    -Wcast-align -Wcast-qual
    -Wold-style-cast -Wzero-as-null-pointer-constant
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2 -Wformat-security
    -Wmissing-declarations
    -Wundef -Wunused
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)

# Debug build 专属
target_compile_options(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
)
target_link_options(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)
```

Release build 强制 `-O2`（不允许 `-O3`，避免激进优化引发的 UB 暴露）。

### 4.4 clang-tidy 配置

```yaml
---
Checks: >
  -*,
  bugprone-*,
  cert-*,
  clang-analyzer-*,
  concurrency-*,
  cppcoreguidelines-*,
  hicpp-*,
  misc-*,
  modernize-*,
  performance-*,
  portability-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers
WarningsAsErrors: '*'
HeaderFilterRegex: '(mass_l3|l3_msgs)/.*'
FormatStyle: file
CheckOptions:
  - key: readability-function-size.LineThreshold
    value: '60'
  - key: readability-function-size.StatementThreshold
    value: '40'
  - key: readability-function-cognitive-complexity.Threshold
    value: '10'
  - key: readability-identifier-naming.NamespaceCase
    value: 'lower_case'
  - key: readability-identifier-naming.ClassCase
    value: 'CamelCase'
  - key: readability-identifier-naming.FunctionCase
    value: 'lower_case'
  - key: readability-identifier-naming.VariableCase
    value: 'lower_case'
  - key: readability-identifier-naming.MemberSuffix
    value: '_'
  - key: readability-identifier-naming.ConstantPrefix
    value: 'k'
  - key: readability-identifier-naming.ConstantCase
    value: 'CamelCase'
```

M1 / M7 专属覆盖（`.clang-tidy.path-s`）：

```yaml
CheckOptions:
  - key: readability-function-size.LineThreshold
    value: '40'
  - key: readability-function-cognitive-complexity.Threshold
    value: '8'
  - key: cppcoreguidelines-no-malloc.Allocations
    value: 'std::malloc;std::calloc;std::realloc;malloc;calloc;realloc'
  - key: cppcoreguidelines-no-malloc.Deallocations
    value: 'std::free;free'
```

---

## 5. 命名约定

### 5.1 C++ 命名

| 类型 | 风格 | 示例 |
|---|---|---|
| **命名空间** | `snake_case` | `mass_l3::m1_envelope`, `mass_l3::common` |
| **类 / 结构体 / enum class** | `PascalCase` | `OddStateMachine`, `EncounterClassification` |
| **函数 / 方法** | `lower_snake_case` | `compute_conformance_score()`, `update_world_state()` |
| **变量（局部 / 参数）** | `lower_snake_case` | `current_zone`, `tcpa_s`, `target_id` |
| **成员变量** | `lower_snake_case_` | `current_zone_`, `last_update_stamp_` |
| **静态常量 / constexpr** | `kPascalCase` | `kMaxTargets = 256`, `kCpaThresholdM = 50.0` |
| **宏（避免使用）** | `UPPER_SNAKE_CASE` | `MASS_L3_M1_INCLUDE_GUARD_HPP_` |
| **模板参数** | `PascalCase` 或 `T` | `template<typename TargetT>` |

**项目硬规则**：
- 不使用匈牙利前缀（`m_`, `p_`, `i_` 等）。
- 不使用单字母变量名（除 `i`/`j`/`k` 循环 + lambda 短捕获）。
- 不缩写易混淆词（`tgt` -> `target`；`cnt` -> `count`；但 `cpa` / `tcpa` / `tmr` / `tdl` / `mrm` 因领域共识保留）。

### 5.2 ROS2 节点 / Topic / Service 命名

| 类型 | 风格 | 示例 |
|---|---|---|
| **节点名** | `snake_case` | `m1_envelope_manager`, `m7_safety_supervisor` |
| **Topic 名** | `/<layer>/<source_module>/<message_kind>` | `/l3/m1/odd_state`, `/l3/m2/world_state`, `/l3/m5/avoidance_plan` |
| **跨层 topic** | `/<source_layer>/<message_kind>` | `/fusion/tracked_targets`, `/checker/veto_notification` |
| **Service 名** | `/<layer>/<message_kind>` | `/l3/m3/route_replan_request` |
| **参数（YAML key）** | `lower_snake_case` | `cpa_threshold_m`, `tmr_min_s` |

---

## 6. 测试约定

### 6.1 C++ 测试

使用 **GoogleTest** + `ament_add_gtest`：

```cmake
# CMakeLists.txt
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_colregs_reasoner
    test/test_colregs_reasoner.cpp
    test/test_rule_library_loader.cpp
  )
  target_link_libraries(test_colregs_reasoner ${PROJECT_NAME})
endif()
```

测试文件命名：`test_<module>.cpp`，放在 `<package>/test/` 目录：

```cpp
// test/test_colregs_reasoner.cpp
#include <gtest/gtest.h>
#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

TEST(ColregsReasonerTest, BasicInitialization) {
  rclcpp::init(0, nullptr);
  auto node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();
  EXPECT_EQ(node->get_name(), std::string("m6_colregs_reasoner"));
  rclcpp::shutdown();
}
```

**覆盖率门槛**：
- PATH-S（M1/M7）：≥ 95%
- PATH-D（M2–M6）：≥ 90%
- PATH-H（M8）：≥ 85%

### 6.2 Python 测试

使用 **pytest**（不用 `unittest`）：

```python
# test/test_mock_publisher.py
import pytest
from l3_external_mock_publisher.mock_publisher import MockPublisher

def test_publisher_initialization():
    pub = MockPublisher("test_topic")
    assert pub.topic == "test_topic"
```

### 6.3 测试组织

```
<package>/
  test/
    test_<module>.cpp          # 单元测试
    test_<module>_integration.cpp  # 集成测试（可选）
  test_data/
    fixture_*.yaml             # 测试夹具数据
```

---

## 7. Git 约定

### 7.1 分支命名

每个 D-task 对应一个分支：

```
feat/d{阶段}.{序号}-{短描述}
```

示例：
- `feat/d1.3b-scenario-mgmt`
- `feat/d2.1-m1-odd-manager`
- `feat/d3.1-m4-behavior-arbiter`

**规则**：
- 一个 D-task = 一个 branch，不同 D-task 不共用。
- 从 `main` 切出，merge 回 `main` 后立即删除（本地 + remote）。
- 禁止保留"备用"分支——已 merge 历史在 git log 中，不需要分支指针。

### 7.2 Commit 消息

遵循 **Conventional Commits**：

| 前缀 | 用途 | 示例 |
|---|---|---|
| `feat:` | 新功能 | `feat(m6): add Rule 15 crossing evaluation` |
| `fix:` | Bug 修复 | `fix(m1): correct ODD zone transition logic` |
| `docs:` | 文档更新 | `docs: update ROS2 QoS policy in coding standards` |
| `test:` | 测试相关 | `test(m5): add BC-MPC boundary condition tests` |
| `ci:` | CI/CD 变更 | `ci: enable Polyspace for M7 in pipeline` |
| `refactor:` | 重构（无行为变更）| `refactor(m2): split target cache into own header` |
| `perf:` | 性能优化 | `perf(m4): reduce IvP objective evaluation by 30%` |
| `chore:` | 杂项（依赖升级、配置）| `chore: bump spdlog to 1.17.0` |

**格式**：

```
<type>(<scope>): <subject>

<body>

<footer>
```

示例：

```
feat(m6): implement Rule 17 Stand-On Ship phase classifier

- Add T_standOn / T_act / T_postAvoid phase transitions
- ODD-aware threshold loading from YAML
- Integration with M2 WorldState target cache

Refs: v1.1.2 §9.3
```

### 7.3 PR 规范

PR 描述必须包含：
- 关联 D-task 编号
- 变更摘要（≤ 5 条 bullet）
- 测试覆盖说明
- 静态分析结果（clang-tidy / cppcheck 无新增违规）
- 跨模块接口变更（如有）

---

## 8. 错误处理

### 8.1 ErrorCode 枚举

每个模块定义自己的 `ErrorCode` enum（在 `<module>/error.hpp`），根错误类型集中在 `mass_l3::common::ErrorCode`：

```cpp
enum class ErrorCode : int32_t {
    Ok = 0,
    InvalidArgument = 1,
    OutOfRange = 2,
    PreconditionFailed = 3,
    ResourceUnavailable = 4,
    Timeout = 5,
    NotImplemented = 6,
    InternalError = 7,
    SafetyViolation = 8,        // M7 专属：触发 MRC
    OddViolation = 9,           // M1 专属：触发 OUT 状态
};
```

模块特定错误码范围：

| 模块 | 范围 |
|---|---|
| M1 | 1000–1999 |
| M2 | 2000–2999 |
| M3 | 3000–3999 |
| M4 | 4000–4999 |
| M5 | 5000–5999 |
| M6 | 6000–6999 |
| M7 | 7000–7999 |
| M8 | 8000–8999 |

示例（M6）：

```cpp
namespace mass_l3::m6_colregs {

enum class ErrorCode : int32_t {
    Ok = 0,
    // M6 专属
    RuleLibraryLoadFailed = 6001,
    InvalidEncounterGeometry = 6002,
    StaleWorldState = 6003,
    OddThresholdMissing = 6004,
    ConstraintGenerationFailed = 6005,
};

}  // namespace mass_l3::m6_colregs
```

### 8.2 日志规范

统一使用 **spdlog**（C++）或项目 `mass_l3_log` wrapper（Python）：

```cpp
spdlog::error("[M6][{}] rule_library_load_failed: path={} reason={}",
              error_code_str(ErrorCode::RuleLibraryLoadFailed),
              rule_lib_path, e.what());
```

**严重等级映射**：
- `spdlog::trace` — 高频调试（默认 release 关闭）
- `spdlog::debug` — 单次决策细节
- `spdlog::info` — 正常状态变化（ODD 切换、行为切换）
- `spdlog::warn` — 退化但仍工作（DEGRADED 心跳）
- `spdlog::error` — 模块功能失效，但系统仍可用
- `spdlog::critical` — 系统级风险，触发 M1 CRITICAL / MRC

### 8.3 异常策略

**禁止异常穿越 ROS2 边界**：

```cpp
// ❌ 禁止：异常从 callback 逃逸到 rclcpp::spin()
void on_world_state(const WorldState::SharedPtr msg) {
    auto result = compute_something(msg);  // 可能 throw
    publish(result);
}

// ✅ 正确：边界内捕获，转日志 + 降级
void on_world_state(const WorldState::SharedPtr msg) {
    try {
        auto result = compute_something(msg);
        publish(result);
    } catch (const std::exception& e) {
        spdlog::error("[M6] on_world_state failed: {}", e.what());
        publish_degraded_constraint("exception_in_callback");
    }
}
```

M1 / M7 安全关键路径 **完全禁用异常**（编译时 `-fno-exceptions`），使用 `std::expected<T, ErrorCode>` 或 `tl::expected`：

```cpp
// M1 / M7 路径
tl::expected<double, ErrorCode> compute_score() {
    if (input.invalid()) {
        return tl::unexpected(ErrorCode::InvalidArgument);
    }
    return score;
}
```

M2–M6 / M8 允许异常，但：
1. `catch(...)` 仅在 ROS2 node `spin()` 边界。
2. 异常类型须从 `std::exception` 派生。
3. 不允许在析构函数 throw。

---

## 9. 多船型规则（零船型常量）

**A 层决策代码禁止任何船型特定常量**。

```cpp
// ❌ 禁止
if (vessel_type == "FCB") {
    cpa_safe_m = 1852.0;  // 船型硬编码
}

// ✅ 正确：Capability Manifest + PVA 适配
const auto kManifest = load_capability_manifest();
const double kCpaSafeM = kManifest->min_cpa_m();  // 来自配置
```

**规则**：
- 禁止 `if vessel == "FCB"` / `if vessel == "Tug"` 等判断潜入 A 层。
- 船型差异通过 **Capability Manifest YAML** 注入。
- 水动力系数通过 **PVA（Plugin-Vehicle-Adapter）** 模式加载。
- 决策核心代码零船型常量；M2 World Model 负责解析 manifest 并填充 `VesselCharacteristics` 消息。

示例 Capability Manifest：

```yaml
# config/capability_manifest.yaml
vessel:
  type: "FCB"
  length_m: 85.0
  beam_m: 14.0
  draft_m: 5.2
  max_speed_kn: 20.0
  max_turn_rate_deg_s: 5.0
  min_cpa_m: 1852.0          # 0.5 NM
  tmr_min_s: 60.0
```

---

## 10. Python 编码规范

### 10.1 工具链

```
.python-version       → 3.10
ruff.toml             → Ruff lint 配置
mypy.ini              → mypy strict 配置
.pre-commit-config.yaml → Black / isort / ruff / mypy 钩子
pyproject.toml        → 依赖锁定
```

### 10.2 强制规则

- **PEP 8 完整**（line ≤ 100 chars，不是 79）。
- **Black 格式化**（一致风格，不可争论）。
- **Ruff lint 全规则**（含 E / W / F / C / B / SIM 类）。
- **mypy strict mode**：所有函数必须有完整类型注解；`Any` 仅在第三方库 stub 缺失时；CI 阻断 `# type: ignore` 滥用。
- **import 顺序**：标准库 / 第三方 / 项目（`isort` 自动）。

### 10.3 项目硬规则

- 所有 IO（文件 / 网络）用 `pathlib` / `httpx`，**不用** `os.path` / `requests`。
- 所有 dataclass 用 `dataclasses.dataclass(frozen=True, slots=True)`（默认不可变 + 内存优化）。
- 单元测试用 `pytest`，**不用** `unittest`。
- 禁用 `*` 通配 import（CI 阻断）。
- 禁用 `print()` 直接输出（用 `logging` 或项目 `mass_l3_log` wrapper）。

---

## 11. 注释规范

> **本项目继承 v1.1.2 全局规则**：注释只解释 WHY，不解释 WHAT。well-named identifiers 已说明 WHAT。

### 11.1 强制注释场景

只有以下五种情况**强制**写注释：

1. **隐藏约束 / 不变量**：
   ```cpp
   // INVARIANT: targets_ is sorted by tcpa_s ascending; rely on this in select_priority_target()
   std::vector<TrackedTarget> targets_;
   ```

2. **跨模块契约引用**：
   ```cpp
   // Per v1.1.2 §15.1: confidence ∈ [0, 1]; M7 reads this to gate SOTIF assumption checks (§11.6).
   double confidence;
   ```

3. **Workaround 标记**：
   ```cpp
   // WORKAROUND: Cyclone DDS 0.10.4 has known reordering bug under high load (issue cyclonedds#1234).
   // Re-sort by stamp on receive until upstream fix is integrated.
   std::sort(buffer.begin(), buffer.end(), by_stamp);
   ```

4. **HAZID 校准点**：
   ```cpp
   // [TBD-HAZID] Initial value 50.0 m (v1.1.2 §11.6); final value pending HAZID RUN-001 (RPN=12).
   constexpr double kCpaEmergencyM = 50.0;
   ```

5. **算法关键证据引用**：
   ```cpp
   // Reference: Veitch et al. (2024), TMR ≥ 60s empirical baseline [R4].
   // Architecture v1.1.2 §3.4 fixes this as design baseline.
   constexpr double kTmrMinS = 60.0;
   ```

### 11.2 禁止注释场景

- **解释 WHAT**：`// increment counter`（命名已说明）。
- **任务 / PR 历史**：`// added by John for issue #123`（git blame 即可）。
- **被注释掉的代码**：`// auto x = old_method();`（git history 即可）。
- **任何形式的祝福 / 装饰**：`// super cool function`。

### 11.3 函数级 doc 注释（强制项）

公共 API 函数须有 Doxygen 风格 doc：

```cpp
/**
 * @brief Compute Closest Point of Approach for an own-ship/target pair.
 *
 * Per v1.1.2 §6.2 + IMO COLREG geometric definition.
 *
 * @param own_ship  Filtered own-ship state (covariance-aware).
 * @param target    Tracked target state (sog/cog over ground).
 * @return CpaResult with cpa_m, tcpa_s, and uncertainty_m.
 *
 * @pre  own_ship.stamp and target.stamp differ by ≤ 1.0 s (else returns nullopt).
 * @note Linear extrapolation only; turning targets need M2 §6.3 fallback path.
 */
std::optional<CpaResult> compute_cpa(const OwnShipState& own_ship,
                                     const TrackedTarget& target);
```

---

## 12. 头文件 + 包含

### 12.1 包含顺序（强制）

```cpp
// 1. 自身 header
#include "mass_l3/m1/odd_state_machine.hpp"

// 2. C 系统头
#include <unistd.h>

// 3. C++ 标准库
#include <chrono>
#include <memory>
#include <vector>

// 4. 第三方库
#include <Eigen/Core>
#include <spdlog/spdlog.h>

// 5. ROS2
#include <rclcpp/rclcpp.hpp>

// 6. 项目内其他模块（仅 .hpp 暴露，不允许 m7 包含 m1 私有 .hpp）
#include "l3_msgs/msg/odd_state.hpp"
#include "mass_l3/common/types.hpp"
```

### 12.2 头文件守卫

强制 `#pragma once`，**不用** `#ifndef` 宏。

### 12.3 前向声明

允许且鼓励：减少编译依赖。但跨模块前向声明须慎用（避免破坏接口契约）。

---

## 13. 高频违规 + 修复模式

### 13.1 类型安全 Top 5

```cpp
// ❌ Rule 4.1.1 violation
int x = 100;

// ✅ Fixed
int32_t x = 100;

// ❌ Rule 5.0.6 violation (implicit narrowing)
double speed = 12.5;
int kn = speed;

// ✅ Fixed
int32_t kn = static_cast<int32_t>(speed);

// ❌ Rule 7.0.x violation (C-style cast)
auto* node = (rclcpp::Node*)raw_ptr;

// ✅ Fixed
auto* node = static_cast<rclcpp::Node*>(raw_ptr);
```

### 13.2 资源管理 Top 5

```cpp
// ❌ MISRA 21.2.x violation (raw new/delete)
auto* tracker = new TargetTracker();
delete tracker;

// ✅ Fixed (RAII)
auto tracker = std::make_unique<TargetTracker>();

// ❌ AUTOSAR A18-5-2 violation (no RAII for FILE*)
FILE* f = fopen("config.json", "r");
fclose(f);

// ✅ Fixed
struct FileCloser { void operator()(FILE* f) const { if (f) fclose(f); } };
std::unique_ptr<FILE, FileCloser> f(fopen("config.json", "r"));
```

### 13.3 控制流 Top 5

```cpp
// ❌ MISRA 6.6.x violation (no default in switch)
switch (zone) {
  case OddZone::A: return 1.0;
  case OddZone::B: return 0.8;
}

// ✅ Fixed
switch (zone) {
  case OddZone::A: return 1.0;
  case OddZone::B: return 0.8;
  default:
    spdlog::critical("[M1] zone={} out of enum range", static_cast<int>(zone));
    return 0.0;
}

// ❌ MISRA 6.4.4 violation (case fall-through)
switch (rule) {
  case Rule::Crossing:
    log_event();
  case Rule::HeadOn:  // unintended fallthrough!
    apply_giveway();
}

// ✅ Fixed
switch (rule) {
  case Rule::Crossing:
    log_event();
    [[fallthrough]];
  case Rule::HeadOn:
    apply_giveway();
    break;
  default:
    break;
}
```

### 13.4 浮点 Top 3

```cpp
// ❌ AUTOSAR A0-4-2 violation (== on double)
if (cpa == 0.0) { /* ... */ }

// ✅ Fixed
constexpr double kEpsCpa = 1e-9;
if (std::abs(cpa) < kEpsCpa) { /* ... */ }

// ❌ Project rule violation (raw double seconds)
double dt = (t2 - t1).count();

// ✅ Fixed (use std::chrono)
auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
double dt_s = std::chrono::duration<double>(dt).count();
```

### 13.5 并发 Top 3

```cpp
// ❌ Project rule violation (detached thread)
std::thread t([] { /* ... */ });
t.detach();

// ✅ Fixed (use jthread wrapper or RAII)
class JoinableThread {
  std::thread t_;
public:
  template <typename F> JoinableThread(F&& f) : t_(std::forward<F>(f)) {}
  ~JoinableThread() { if (t_.joinable()) t_.join(); }
};
JoinableThread t([] { /* ... */ });

// ❌ Project rule violation (raw mutex lock/unlock)
mu.lock();
counter++;
mu.unlock();

// ✅ Fixed
{
  std::lock_guard<std::mutex> lock(mu);
  counter++;
}
```

### 13.6 Doer-Checker 独立性 Top 3

```cpp
// ❌ M7 violates independence by including M1 internal header
#include "mass_l3/m1/odd_state_machine.hpp"

// ✅ Fixed (M7 reads M1 only via ROS2 messages)
#include "l3_msgs/msg/odd_state.hpp"
```

---

## 14. 代码评审 Checklist

PR review 时强制 reviewer 走以下 checklist（PR 模板自动注入）：

- [ ] 命名遵循 §5（snake_case / PascalCase / 等）。
- [ ] 注释命中 §11.1 五种合法场景。
- [ ] M1 / M7 路径无动态分配 / 无异常 / 无第三方非白名单库。
- [ ] 错误处理符合 §8（ErrorCode 范围 + spdlog + 无异常穿越 ROS2 边界）。
- [ ] 头文件包含顺序符合 §12.1。
- [ ] 函数 ≤ 60 行（M1/M7 ≤ 40 行）；循环复杂度 ≤ 10（M1/M7 ≤ 8）。
- [ ] 单元测试覆盖率 ≥ 90%（M1/M7 ≥ 95%）。
- [ ] CI 5 阶段全绿（lint / unit / static / integration / release）。
- [ ] 跨模块接口（订阅 / 发布消息）字段对齐 v1.1.2 §15.1 IDL。
- [ ] 多船型规则 §9 未引入船型硬编码。
- [ ] HAZID 校准点用 `[TBD-HAZID]` 注释 + YAML 注入。

---

## 15. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：MISRA C++:2023 主基线 + AUTOSAR C++14 设计指南补充 + 50 高频违规修复 |
| v1.1 | 2026-05-14 | Claude（D1.4） | D1.4 升级：新增 ROS2 节点约定（§1）、消息发布约定（§2）、Git 约定（§7）、多船型规则（§9）、ErrorCode 范围（§8.1）；整合 static-analysis-policy.md 关键配置；精简高频违规示例至 Top 3/5 |

---

## 16. 引用

- **MISRA C++:2023 合并继任者公告**（2023-10）— [Perforce](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) 🟢 A
- **MISRA C++:2023 完整指南**（179 规则）— [Parasoft](https://www.parasoft.com/blog/misra-cpp-2023-guide/) 🟢 B
- **AUTOSAR C++14 Coding Guidelines**（设计层指南，仍有参考价值）
- **GCC 15 / 14 系列说明**— [gcc.gnu.org](https://gcc.gnu.org/gcc-15/) 🟢 A
- **Clang/LLVM 20.1 Release Notes** — [llvm.org](https://releases.llvm.org/20.1.0/tools/clang/docs/ReleaseNotes.html) 🟢 A
- **PEP 8** — Python 编码规范
- **ROS 2 C++ Style Guide** — [docs.ros.org](https://docs.ros.org/en/jazzy/Contributing/Code-Style-Language-Versions.html) 🟢 A
- **v1.1.2 架构报告** — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（接口契约、ODD 框架、模块全景）
- **static-analysis-policy.md** — 工具配置完整版（Polyspace / Coverity / ASan / Ruff / mypy）
