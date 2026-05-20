# 编码规范（Coding Standards）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-CODING-001 |
| 版本 | **v1.2**（D1.4 交付） |
| 日期 | 2026-05-20 |
| 状态 | **Phase 1 D1.4 在制**（CI 强制执行；DEMO-1 6/15 验收） |
| 上一版本 | v1.1（2026-05-14，已归档 `docs/Design/Archive/Old Modules/Implementation/coding-standards.md`） |
| 主基线 | **MISRA C++:2023**（179 = 5 Mandatory + 122 Required Rules + 4 Required Directives + 48 Advisory；覆盖 C++17）+ AUTOSAR C++14 设计指南补充 |
| 主语言 | C++17（GCC 14.3 LTS 基线 / Clang 20.1.8 多样化） |
| 副语言 | Python 3.10（PEP 8 + Ruff + mypy strict） |
| 适用范围 | 8 个 L3 模块（M1–M8）+ `l3_msgs` + `l3_external_msgs` 全部源代码 |
| 关联文件 | `D1.4-plan.md` / `static-analysis-policy.md`（存档） / `.clang-tidy` / `.clang-tidy.path-s` / `.clang-tidy.m8` / `cppcheck-misra-cpp-2023.cfg` / `.gitlab-ci.yml` |

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

### 4.4 PATH-S 安全关键路径严格规则（M1 ODD/Envelope Manager + M7 Safety Supervisor）

PATH-S 是 Doer-Checker 架构中 Checker（M7）和唯一调度枢纽（M1）的代码路径，承担 **IEC 61508 SIL 2 核心安全功能**。以下规则在 PATH-D（M2–M6）基础上**额外收紧**：

| 规则类别 | PATH-D（M2–M6） | PATH-S（M1/M7） | 检测工具 / 依据 |
|---|---|---|---|
| 函数行数上限 | 60 行 | **40 行** | clang-tidy `readability-function-size.LineThreshold` |
| 函数语句上限 | 40 句 | **30 句** | clang-tidy `readability-function-size.StatementThreshold` |
| 认知复杂度上限 | 10 | **8** | clang-tidy `readability-function-cognitive-complexity.Threshold` |
| 非 const 全局变量 | 允许（警告） | **禁止** | clang-tidy `cppcoreguidelines-avoid-non-const-global-variables` |
| 动态内存分配 | 允许 RAII（`std::make_unique`） | **禁止所有 malloc/calloc/realloc/free** | clang-tidy `cppcoreguidelines-no-malloc` |
| 生命周期节点 | 推荐 | **强制** `rclcpp_lifecycle::LifecycleNode` | §1.3 + 代码审查 |
| 并发模型 | `SingleThreadedExecutor`（推荐） | **强制** `SingleThreadedExecutor` | §1.2 + 代码审查 |
| cppcheck 强度 | exhaustive（全 179 规则） | **exhaustive（全 179 规则 + MISRA 规则文本）** | `cppcheck-misra-cpp-2023.cfg` |
| Polyspace | 推荐 | **强制**（形式化抽象解释，TÜV SÜD 认证 SIL 4） | `.gitlab-ci.yml` stage-3-polyspace-path-s |

**配置文件**：仓库根 `.clang-tidy.path-s`（85 行），与主基线 `.clang-tidy`（72 行）的差异见 `.clang-tidy.path-s` 头部注释。

**PATH-S Doer-Checker 独立性子集**（基于 D1.2 `check-doer-checker-independence.sh`，123 行）：
- M7 禁止 `#include` M1–M6 模块内部头文件（仅允许 `l3_msgs/msg/*.hpp` 消息头）
- M7 禁止链接 CasADi / IPOPT / GeographicLib / nlohmann/json / Boost.PropertyTree / 等 Doer 侧库
- M7 CMakeLists.txt / `package.xml` 须通过独立性检查脚本验证
- CI 集成测试：`src/l3_tdl_kernel/m7_safety_supervisor/test/integration/test_doer_checker_independence.cpp`

**独立性子集修复模式**详见 §13.6。

---

### 4.5 M8 HMI 路径裁剪规则（M8 HMI/Transparency Bridge）

M8 是 Web HMI 后端（C++ + Python 混合），承载 SAT-1/2/3 透明性展示，**不参与 Doer-Checker 安全关键链**。以下规则从主基线**豁免**：

**豁免原因**：Web handler 天然长于核心逻辑 / HTTP 请求响应天然动态分配 / Web 框架普遍用异常。

| 豁免规则 | clang-tidy check | 豁免原因 |
|---|---|---|
| 动态内存分配 | `cppcoreguidelines-no-malloc`, `cppcoreguidelines-owning-memory`, `hicpp-no-malloc` | HTTP 请求/响应天然动态 |
| 异常禁用 | `cert-err58-cpp`, `cert-err60-cpp` | Web 框架（Crow/cpp-httplib）普遍用异常 |
| 全局变量 | `cppcoreguidelines-avoid-non-const-global-variables` | Web server 单例模式 |
| 函数认知复杂度 | `readability-function-cognitive-complexity` | handler 天然长（放宽至 120 行/80 语句） |
| 特殊成员函数 | `cppcoreguidelines-special-member-functions`, `hicpp-special-member-functions` | Web 框架类继承 |
| owning memory | `cppcoreguidelines-owning-memory` | 见动态分配 |
| 数组边界 | `cppcoreguidelines-pro-bounds-constant-array-index`, `cppcoreguidelines-pro-bounds-array-to-pointer-decay` | JSON 解析库交互 |
| union 访问 | `cppcoreguidelines-pro-type-union-access` | DDS IDL union 类型交互 |
| AUTOSAR OO 严格 | `misc-no-recursion`, `misc-non-private-member-variables-in-classes`, `misc-throw-by-value-catch-by-reference`, `hicpp-explicit-conversions`, `hicpp-use-auto`, `hicpp-vararg` | 模板深度/递归/成员可见性放宽 |
| 风格豁免 | `modernize-use-auto`, `modernize-use-nodiscard`, `performance-unnecessary-value-param`, `readability-avoid-const-params-in-decls`, `readability-convert-member-functions-to-static`, `readability-make-member-function-const`, `readability-simplify-boolean-expr`, `readability-uppercase-literal-suffix` | HMI 代码可读性优先于性能微优化 |

**M8 仍强制执行**：`bugprone-*` / `cert-*`（除已列豁免）/ `clang-analyzer-*` / `concurrency-*` / `misc-*`（除已列豁免）/ `modernize-*`（除已列豁免）/ `performance-*`（除已列豁免）/ `portability-*` / `readability-*`（除已列豁免）。WarningsAsErrors 保持全开。

**M8 函数尺寸放宽**（相对于 PATH-D 60 行/40 句）：

| 参数 | PATH-D | M8 |
|---|---|---|
| LineThreshold | 60 | **120** |
| StatementThreshold | 40 | **80** |
| cognitive-complexity | 10 | **豁免** |

**配置文件**：仓库根 `.clang-tidy.m8`（99 行）。

---

### 4.6 clang-tidy 9 大类配置说明

本项目 `.clang-tidy` 主基线采用全局禁用后逐类启用的策略（`-*` 后逐类开启），共 **10 个大类**（含 `clang-analyzer-*` 和 `hicpp-*`）：

| 大类 | 检查项数 | 用途 | 典型 check | MISRA C++:2023 映射 |
|---|---|---|---|---|
| **bugprone-\*** | ~80 | 易错模式：内存泄漏、无限循环、悬空句柄、错误 swap | `bugprone-macro-parentheses`, `bugprone-branch-clone`, `bugprone-narrowing-conversions` | Rule 6.x 控制流 / Rule 5.x 隐式转换 / Rule 21.x 标准库安全 |
| **cert-\*** | ~30+ | SEI CERT C/C++ 安全编码标准（多数为其他大类的 alias） | `cert-err58-cpp`, `cert-oop58-cpp`, `cert-msc50-cpp` | Rule 21.x 标准库安全 / Rule 18.x 异常安全 |
| **clang-analyzer-\*** | ~90 | Clang 静态分析器深度路径分析 | `clang-analyzer-core.*`, `clang-analyzer-cplusplus.*`, `clang-analyzer-unix.*` | Rule 0.x 未定义行为 / Rule 1.x 实现定义行为 |
| **concurrency-\*** | ~2 | 并发安全检查 | `concurrency-mt-unsafe` | Rule 19.x 并发 |
| **cppcoreguidelines-\*** | ~30 | C++ Core Guidelines 最佳实践（与 MISRA 重叠度最高） | `cppcoreguidelines-owning-memory`, `cppcoreguidelines-pro-type-*`, `cppcoreguidelines-init-variables` | Rule 4.x 类型 / Rule 7.x 指针 / Rule 8.x 声明 / Rule 15.x 资源 |
| **hicpp-\*** | ~30 | High Integrity C++（Perforce/PRQA，与 AUTOSAR C++14 高度对齐） | `hicpp-no-malloc`, `hicpp-special-member-functions`, `hicpp-explicit-conversions` | Rule 21.x + AUTOSAR C++14 设计指南 |
| **misc-\*** | ~25 | 杂项静态检查 | `misc-no-recursion`, `misc-const-correctness`, `misc-static-assert` | Rule 2.x 预处理 / Rule 17.x 通用 |
| **modernize-\*** | ~40 | C++11/14/17 现代化建议 | `modernize-use-nullptr`, `modernize-use-override`, `modernize-make-unique` | Rule 10.x 现代 C++ / 编码风格 |
| **performance-\*** | ~20 | 性能优化建议 | `performance-unnecessary-value-param`, `performance-move-const-arg`, `performance-for-range-copy` | 项目本地规则 PROJ-LR-004 |
| **readability-\*** | ~55 | 可读性检查 + 命名规则强制执行 | `readability-identifier-naming`, `readability-function-size`, `readability-container-size-empty` | Rule 5.x 标识符 / 项目命名约定 |

**已排除的规则及理由**：

| 排除的 check | 理由 |
|---|---|
| `modernize-use-trailing-return-type` | 团队偏好前置返回类型 |
| `readability-magic-numbers` | 海上物理常量（g=9.81, 1852 m/NM, π）须保留字面量 |
| `cppcoreguidelines-avoid-magic-numbers` | 同上；M1/M7 通过 PATH-S 禁非 const 全局变量间接约束 |
| `readability-identifier-length` | MASS 领域缩写（cpa/tcpa/sog/cog/rot/cog）短标识符合法 |
| `cppcoreguidelines-pro-bounds-constant-array-index` | 与 ROS2 消息固定数组交互冲突 |
| `cppcoreguidelines-pro-type-union-access` | 与 DDS IDL union 类型交互冲突 |
| `portability-template-virtual-member-function` | GCC 14.3 / Clang 20.1.8 双编译器行为一致 |

**命名规则**（通过 `readability-identifier-naming` 强制执行）：

| 实体 | Case | 示例 |
|---|---|---|
| Namespace | `lower_case` | `mass_l3::m6_colregs` |
| Class / Struct | `CamelCase` | `ColregsReasonerNode` |
| Enum | `CamelCase` | `OddZone` |
| Function / Method | `lower_case` | `compute_cpa()` |
| Variable / Parameter | `lower_case` | `own_ship` |
| Member field | `lower_case` + `_` 后缀 | `targets_` |
| Constant / Constexpr | `CamelCase` + `k` 前缀 | `kTmrMinS` |
| Macro | `UPPER_CASE` | `MASS_L3_NODISCARD` |
| Template parameter | `CamelCase` | `typename T` |

---

### 4.7 cppcheck MISRA C++:2023 配置

**配置文件**：仓库根 `cppcheck-misra-cpp-2023.cfg`（XML 格式，143 行）。

**工具版本与许可**：

| 版本 | 许可 | 用途 | MISRA C++:2023 覆盖 |
|---|---|---|---|
| **cppcheck Premium 26.1.0** | 商业（每席位） | CI 主用 | 完整 179 规则覆盖（含 MISRA addon） |
| **cppcheck OSS 2.20.0** | GPLv3 | 本地开发备选 | 部分覆盖（MISRA addon 主要覆盖 C:2012；C++:2023 需 Premium） |

**路径强度配置**：

| 路径 | 模块 | check-level | MISRA 规则 | CI 阻断阈值 |
|---|---|---|---|---|
| **PATH-S** | M1, M7 | exhaustive | 全 179 规则 + 规则文本 | **0 critical / 0 major / ≤5 minor** |
| **PATH-D** | M2–M6 | exhaustive | 全 179 规则 | **0 critical / ≤3 major / ≤15 minor** |
| **PATH-H** | M8 | normal | 裁剪集 ~120 规则 | **0 critical / ≤5 major / ≤20 minor** |

**严重度定义**：
- **critical** = MISRA Mandatory 违反 + 任何 UB 检测（nullPointer / arrayIndexOutOfBounds / memleak） + 任何内存安全检测
- **major** = MISRA Required 违反 + 项目硬规则（PROJ-LR-001~010）违反
- **minor** = MISRA Advisory 违反 + 风格 / 可读性建议

**本地运行命令**：

```bash
# PATH-D（M2-M6）完整检查
cppcheck --enable=all \
         --project=cppcheck-misra-cpp-2023.cfg \
         --suppressions-list=cppcheck-suppressions.txt \
         src/l3_tdl_kernel/m2_world_model/ \
         src/l3_tdl_kernel/m3_mission_manager/ \
         src/l3_tdl_kernel/m4_behavior_arbiter/ \
         src/l3_tdl_kernel/m5_tactical_planner/ \
         src/l3_tdl_kernel/m6_colregs_reasoner/

# PATH-S（M1/M7）安全关键 — 额外加 exhaustive
cppcheck --enable=all \
         --project=cppcheck-misra-cpp-2023.cfg \
         --check-level=exhaustive \
         src/l3_tdl_kernel/m1_odd_envelope_manager/ \
         src/l3_tdl_kernel/m7_safety_supervisor/

# PATH-H（M8）裁剪集
cppcheck --enable=all \
         --project=cppcheck-misra-cpp-2023.cfg \
         --check-level=normal \
         src/l3_tdl_kernel/m8_hmi_transparency_bridge/

# OSS fallback（无 Premium 许可时）
cppcheck --enable=all --addon=misra --check-level=exhaustive \
         --suppressions-list=cppcheck-suppressions.txt \
         src/l3_tdl_kernel/m{1..7}_*/
```

**排除路径**（配置文件内 `<excludes>`）：
- `build/` / `install/` / `.worktrees/` — 构建产物与临时目录
- `test/` / `tests/` — 测试代码允许有意违规（如内存泄漏测试、异常注入测试）

**项目宏定义**（配置文件内 `<defines>`，避免 cppcheck 误报）：
- `MASS_L3_NODISCARD` → `[[nodiscard]]`
- `MASS_L3_NOEXCEPT` → `noexcept`
- `MASS_L3_UNREACHABLE` → `__builtin_unreachable()`
- `MASS_L3_ASSERT(x)` → 空宏（生产构建禁用）
- `PLUGINLIB_EXPORT_CLASS(x,y)` → 空宏

---

### 4.8 项目本地规则（PROJ-LR-001 ~ PROJ-LR-010）

以下 10 条规则针对海事自主航行场景添加，**不在** MISRA C++:2023 官方清单中，但均为项目 CI 强制项：

| 规则 ID | 规则要点 | 强度 | 检测工具 |
|---|---|---|---|
| **PROJ-LR-001** | M1 / M7 禁止动态内存分配（启动后所有内存预分配） | PATH-S 强制 | clang-tidy `cppcoreguidelines-no-malloc` |
| **PROJ-LR-002** | M1 / M7 禁止非 const 全局变量（状态须封装在类中） | PATH-S 强制 | clang-tidy `cppcoreguidelines-avoid-non-const-global-variables` |
| **PROJ-LR-003** | 禁止 C 风格类型转换（`(Type)expr`） | 全路径强制 | clang-tidy `cppcoreguidelines-pro-type-cstyle-cast` |
| **PROJ-LR-004** | 禁止裸 `new` / `delete`（统一 RAII + 智能指针） | 全路径强制 | clang-tidy `cppcoreguidelines-owning-memory` |
| **PROJ-LR-005** | 禁止线程 `detach()`（所有线程必须 join 或 RAII 管理） | 全路径强制 | clang-tidy `concurrency-mt-unsafe` + 代码审查 |
| **PROJ-LR-006** | COLREGs 决策函数须输出 `rationale` 字段（白盒可审计要求） | 全路径强制 | 代码审查 + D1.2 `path_s_dry_run` CI job |
| **PROJ-LR-007** | `stamp` 时间戳字段对齐容差 ≤1.0 s（`own_ship.stamp` 与 `target.stamp`） | 全路径强制 | 代码审查 |
| **PROJ-LR-008** | 禁止在多船型代码中硬编码 FCB 参数（`35.0`/`18.0`/`22.0`/`ROT_max`） | 全路径强制 | `multi_vessel_lint` CI job |
| **PROJ-LR-009** | M7 禁止包含 M1–M6 内部头文件（仅允许 `l3_msgs/msg/*.hpp`） | PATH-S 强制 | `check-doer-checker-independence.sh` |
| **PROJ-LR-010** | 异常不得穿越 ROS2 消息回调边界（回调内 try/catch + spdlog + fallback） | 全路径强制 | clang-tidy `cert-err60-cpp` + 代码审查 |

---

### 4.9 CI 集成与本地运行

#### 流水线架构

本项目 `.gitlab-ci.yml`（668 行）采用 9 阶段流水线，编码规范相关阶段如下：

```
lint
  ├── clang-format              # 代码格式化检查（.clang-format 文件）
  ├── ruff / mypy               # Python 代码质量（ruff check + mypy strict）
  ├── multi_vessel_lint         # 多船型禁字表 grep 检查
  ├── path_s_dry_run (Python)   # PATH-S Doer→Checker 跨模块引用（grep from m7）
  ├── path_s_dry_run (C++)     # PATH-S Doer→Checker 跨模块引用（check-doer-checker-independence.sh）
  └── buf-lint                  # Protobuf IDL lint

static-analysis
  ├── stage-3-clang-tidy        # clang-tidy 按模块并行（M1/M7 用 .clang-tidy.path-s 覆盖）
  ├── stage-3-cppcheck-misra    # cppcheck Premium MISRA C++:2023（含 OSS fallback）
  ├── stage-3-polyspace-path-s  # Polyspace Code Prover PATH-S 形式化验证（M1/M7）
  └── stage-3-doer-checker-independence  # Doer-Checker 独立性检查（不可豁免）

integration-test
  └── stage-4-integration       # colcon build 全工作区编译验证
```

#### 本地运行 clang-tidy

```bash
# PATH-D（M2-M6：主基线 .clang-tidy）
find src/l3_tdl_kernel/m{2..6}_*/  -name "*.cpp" -o -name "*.hpp" \
  | xargs clang-tidy -p build/ --config-file=.clang-tidy

# PATH-S（M1/M7：严格基线 .clang-tidy.path-s）
find src/l3_tdl_kernel/m1_odd_envelope_manager/ \
     src/l3_tdl_kernel/m7_safety_supervisor/ \
     -name "*.cpp" -o -name "*.hpp" \
  | xargs clang-tidy -p build/ --config-file=.clang-tidy.path-s

# PATH-H（M8：简化基线 .clang-tidy.m8）
find src/l3_tdl_kernel/m8_hmi_transparency_bridge/ \
     -name "*.cpp" -o -name "*.hpp" \
  | xargs clang-tidy -p build/ --config-file=.clang-tidy.m8
```

#### 本地运行 cppcheck

参见 §4.7 本地运行命令。推荐先运行 PATH-D 再运行 PATH-S。

#### Doer-Checker 独立性检查

**脚本**：`tools/ci/check-doer-checker-independence.sh`（123 行）

**检查项**：
1. M7 代码不 `#include` 任何 M1–M6 内部头文件（仅允许 `l3_msgs/msg/*.hpp`）
2. M7 CMakeLists.txt 不链接禁用第三方库（CasADi / IPOPT / GeographicLib / nlohmann/json / Boost.PropertyTree 等 12 类库）
3. M7 `package.xml` 不声明禁用依赖

```bash
# 本地运行
./tools/ci/check-doer-checker-independence.sh
# Expected: "Doer-Checker independence: OK"
```

#### CI 门禁阈值（2026-05-11 D1.2 锁定）

| 路径 | critical | major | minor | CI 行为 |
|---|---|---|---|---|
| **PATH-S** | 0 | 0 | ≤5 | **阻断**（WarningsAsErrors + CI job fail） |
| **PATH-D** | 0 | ≤3 | ≤15 | **阻断** |
| **PATH-H** | 0 | ≤5 | ≤20 | **阻断** |
| **PATH-Y** (Python) | 0 error | 0 warning | — | **阻断**（ruff + mypy strict） |

> **DEMO-1 阶段（6/15）**：clang-tidy 为 `allow_failure: true`（warning 模式不阻塞 merge），`path_s_dry_run` 同为 warning 模式。**6 月底前升级强制**（`allow_failure: false`）。

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

## 13. 高频违规 + 修复模式（50 模式）

> 以下 50 个模式覆盖本项目 CI 中最常见的 MISRA C++:2023 / AUTOSAR C++14 / PROJ-LR 违规。
> 每条模式含 4 部分：**违规代码** → **检测规则 ID** → **修复代码** → **解释为什么**（不仅"规则要求"）。

| 子类 | 模式数 | 涉及规则 |
|---|---|---|
| §13.1 类型安全 | 5 | Rule 4.1.1, 5.0.6, 7.0.x, PROJ-LR-003 |
| §13.2 资源管理 | 5 | Rule 21.2.x, AUTOSAR A18-5-2, PROJ-LR-001, PROJ-LR-004 |
| §13.3 控制流 | 5 | Rule 6.6.x, 6.4.4, 0.0.2 |
| §13.4 浮点 | 3 | AUTOSAR A0-4-2, Rule 5.0.6, PROJ rule |
| §13.5 并发 | 3 | PROJ-LR-005, Rule 19.x |
| §13.6 Doer-Checker 独立性 | 3 | PROJ-LR-009 |
| §13.7 隐式转换 | 5 | Rule 5.0.4, 5.0.6, 5.0.8, 4.1.1, PROJ-LR-008 |
| §13.8 指针与引用 | 5 | Rule 7.0.1, 7.0.5, 7.0.6, 7.0.7, 15.0.1 |
| §13.9 初始化与生命周期 | 5 | Rule 8.4.1, 8.4.2, 15.3.1, 18.2.1, 3.1.1 |
| §13.10 容器与迭代器 | 4 | Rule 21.1.1, 21.1.2, 21.1.3, 5.0.2 |
| §13.11 异常安全 | 3 | PROJ-LR-010, Rule 18.2.2, 18.4.1 |
| §13.12 Lambda 与现代 C++ | 2 | Rule 7.0.5 变体, bugprone-use-after-move |
| **合计** | **50** | — |

---

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

### 13.7 隐式转换 Top 5

```cpp
// ❌ MISRA Rule 5.0.6: Implicit narrowing conversion
int32_t speed_kn = get_speed();  // get_speed() returns double，隐式截断
// 检测: cppcheck signConversion / clang-tidy cppcoreguidelines-narrowing-conversions
// 解释: double→int32_t 隐式截断在小数部分丢失精度，海事场景中速度误差会被累计放大
// ✅ Fixed: 显式四舍五入
int32_t speed_kn = static_cast<int32_t>(std::round(get_speed()));

// ❌ MISRA Rule 5.0.4: Implicit signed/unsigned conversion
for (int i = 0; i < targets.size(); ++i) { }  // size_t vs int 符号不匹配
// 检测: clang-tidy cppcoreguidelines-narrowing-conversions / -Wsign-compare
// 解释: 当 targets.size() > INT_MAX 时溢出未定义，虽在 L3 不现实但 MISRA 要求消除所有警告
// ✅ Fixed: 统一使用 size_t
for (size_t i = 0; i < targets.size(); ++i) { }

// ❌ MISRA Rule 5.0.8: Implicit floating→integer conversion
auto timestamp = now.time_since_epoch().count();  // duration::count() 返回 rep 类型
// 检测: cppcheck truncLongCastAssignment
// 解释: duration::count() 在不同平台返回不同精度，隐式转为 auto 可能导致纳秒级截断
// ✅ Fixed: 显式 duration_cast
auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
    now.time_since_epoch()).count();

// ❌ MISRA Rule 4.1.1: 使用未指定宽度的 int 类型
int flag = 1;
long distance_m = 1852;
// 检测: clang-tidy cppcoreguidelines-init-variables / cppcheck
// 解释: int/long 宽度在 32/64 位平台不同，本地 64 位→Jetson ARM64 可能导致截断
// ✅ Fixed: <cstdint> 固定宽度类型
int32_t flag = 1;
int64_t distance_m = 1852;

// ❌ PROJ-LR-008: 硬编码船型参数在核心决策代码
double rudder_limit = 35.0 * M_PI / 180.0;  // FCB 舵角限幅，换船型即错
// 检测: multi_vessel_lint CI job（grep "35.0\|18.0\|22.0" src/*.cpp）
// 解释: 违反架构顶层决策 #4（多船型 = Capability Manifest + PVA 适配），FCB 常量潜入 A 层
// ✅ Fixed: 从 Capability Manifest 读取
double rudder_limit = vessel_capability_.max_rudder_angle_rad;
```

### 13.8 指针与引用 Top 5

```cpp
// ❌ MISRA Rule 7.0.1: 未检查指针解引用
auto* target = get_target_by_id(id);
double cpa = target->cpa_m;  // target 可能为 nullptr
// 检测: clang-analyzer-core.NullDereference / cppcheck nullPointer
// 解释: get_target_by_id 在 MMSI/ID 不存在时返回 nullptr，直接解引用 crash L3 战术层
// ✅ Fixed: 显式 nullptr 检查
auto* target = get_target_by_id(id);
if (!target) { spdlog::warn("[M2] target id={} not found", id); return std::nullopt; }
double cpa = target->cpa_m;

// ❌ MISRA Rule 7.0.5: 函数返回局部变量地址/引用
const OddState& get_state() {
  OddState local_state;
  return local_state;  // dangling reference
}
// 检测: clang-tidy bugprone-return-const-ref-from-parameter / clang-analyzer
// 解释: 局部变量在函数返回后析构，引用指向已释放栈内存，读写出 UB
// ✅ Fixed: 按值返回（RVO/NRVO 保证零拷贝，C++17 强制 copy elision）
OddState get_state() {
  OddState local_state;
  return local_state;
}

// ❌ MISRA Rule 7.0.6: 裸指针作为公共接口
TargetTracker* create_tracker();  // 谁负责 delete？调用者 vs 库 → 所有权模糊
// 检测: clang-tidy cppcoreguidelines-owning-memory / hicpp-*
// 解释: 裸指针无法表达所有权语义，"谁分配谁释放"在大型代码库中必然泄漏
// ✅ Fixed: 智能指针表达所有权
std::unique_ptr<TargetTracker> create_tracker();

// ❌ MISRA Rule 7.0.7: 指针算术
auto* next = target_ptr + 1;  // 不可移植（数组 vs 单对象语义）
// 检测: clang-tidy cppcoreguidelines-pro-bounds-pointer-arithmetic
// 解释: 指针算术依赖 sizeof、对齐、数组连续性假设，Jetson ARM64 与 x86 行为可能不同
// ✅ Fixed: 容器索引
auto& next = targets[i + 1];  // std::vector<Target>::operator[] 有边界（debug）/ .at() 有异常

// ❌ MISRA Rule 15.0.1: 空引用创建（解引用空指针）
int& get_ref() {
  int* p = nullptr;
  return *p;  // 未定义行为：*nullptr
}
// 检测: clang-analyzer-core.uninitialized.* / cppcheck nullPointer
// 解释: 空引用不存在于 C++ 类型系统中，创建即 UB（ISO C++ §8.3.2/5）
// ✅ Fixed: 返回可空类型
std::optional<std::reference_wrapper<int>> get_ref() { return std::nullopt; }
```

### 13.9 初始化与生命周期 Top 5

```cpp
// ❌ MISRA Rule 8.4.1: 变量未初始化
double cpa_m;
double tcpa_s;
// 检测: clang-tidy cppcoreguidelines-init-variables / cppcheck uninitvar
// 解释: 未初始化的 double 在栈上取随机比特，读取出具任意值 → 决策基于垃圾数据
// ✅ Fixed: 列表初始化
double cpa_m = 0.0;
double tcpa_s = 0.0;

// ❌ MISRA Rule 8.4.2: 构造函数未初始化成员
struct CpaResult {
  double cpa_m;
  double tcpa_s;
  CpaResult() {}  // 成员未初始化 — 默认初始化 = 不确定值
};
// 检测: clang-tidy cppcoreguidelines-pro-type-member-init / cppcheck uninitMemberVar
// 解释: POD 类型的默认初始化不设零；cpa_m 取栈上残留值 → 假 CPA 报警
// ✅ Fixed: 成员初始化器
struct CpaResult {
  double cpa_m = 0.0;
  double tcpa_s = 0.0;
  CpaResult() = default;
};

// ❌ MISRA Rule 15.3.1: 虚函数在构造函数中调用
class Base {
public:
  Base() { init(); }  // 构造时调用虚函数 → 派生类重载尚未构造
  virtual void init() {}
};
// 检测: clang-tidy clang-analyzer-optin.cplusplus.VirtualCall / cppcheck virtualCallInConstructor
// 解释: 构造期间 vtable 指向当前类而非最终派生类，init() 调用的是 Base::init() 而非预期重载
// ✅ Fixed: 两阶段初始化或 final 标记
class Base {
public:
  Base() = default;
  virtual void init() {}  // 调用者显式调用 init()，构造与初始化分离
};

// ❌ MISRA Rule 18.2.1: 成员初始化顺序与声明不一致
struct State {
  double sog;    // 声明顺序: sog, cog
  double cog;
  State(double s, double c) : cog(c), sog(s) {}  // 初始化顺序与声明反 -> 未定义行为
};
// 检测: clang-tidy cppcoreguidelines-pro-type-member-init / -Wreorder
// 解释: C++ 成员初始化顺序只由声明顺序决定，初始化列表顺序无关。交叉引用导致隐蔽 bug
// ✅ Fixed: 初始化列表顺序与声明顺序严格一致
struct State {
  double sog;
  double cog;
  State(double s, double c) : sog(s), cog(c) {}
};

// ❌ MISRA Rule 3.1.1 / PROJ-LR-011: 未使用的变量
int result = compute_cpa();  // result 未被读取
// 检测: cppcheck unreadVariable / unusedVariable / clang-tidy misc-unused-parameters
// 解释: 未使用变量表明意图不明确——是有意忽略还是忘记使用？
// ✅ Fixed: [[maybe_unused]] 显式标注意图
[[maybe_unused]] int result = compute_cpa();
```

### 13.10 容器与迭代器 Top 4

```cpp
// ❌ MISRA Rule 21.1.1: 越界访问
std::vector<Target> targets(10);
auto& t = targets[10];  // operator[] 不做边界检查，访问第 11 个元素 = UB
// 检测: cppcheck arrayIndexOutOfBounds / stlOutOfBounds / ASan
// 解释: targets[10] 访问第 11 个元素（0-indexed），10 个元素的 vector 越界一块栈内存
// ✅ Fixed: .at() 抛异常 或 先检查 size()
if (index < targets.size()) { auto& t = targets[index]; }

// ❌ MISRA Rule 21.1.2: 迭代器失效
auto it = targets.begin();
targets.push_back(new_target);  // push_back 可能 realloc → 所有迭代器/指针/引用失效
targets.erase(it);              // 使用失效迭代器 = UB
// 检测: clang-analyzer-cplusplus.InnerPointer / cppcheck invalidIterator
// 解释: vector 扩容时重新分配内存，旧迭代器指向释放内存；erase 再触发双重释放
// ✅ Fixed: push_back 后重新获取迭代器
auto it = std::find_if(targets.begin(), targets.end(), pred);
targets.push_back(new_target);  // 可能 realloc
it = std::find_if(targets.begin(), targets.end(), pred);  // 刷新
targets.erase(it);

// ❌ MISRA Rule 21.1.3: 空容器操作
std::vector<double> speeds;
double max_speed = speeds.front();  // 空 vector 调用 front() = UB
// 检测: cppcheck stlBoundaries / clang-analyzer
// 解释: C++ 标准未定义空容器 front() 行为（与 operator[] 一致），无异常抛出
// ✅ Fixed: 先检查 empty()
double max_speed = 0.0;
if (!speeds.empty()) { max_speed = speeds.front(); }

// ❌ MISRA Rule 5.0.2: 整型溢出
int32_t booking_id = INT32_MAX;
booking_id++;  // 有符号溢出 = UB（ISO C++ §5/4）
// 检测: cppcheck integerOverflow / UBSan
// 解释: 有符号整型溢出是 UB，编译器可能优化掉溢出检查导致安全漏洞（如 CVE-2019-11477）
// ✅ Fixed: 检查边界
if (booking_id < INT32_MAX) { booking_id++; }
else { spdlog::critical("[M3] booking_id overflow"); }
```

### 13.11 异常安全 Top 3

```cpp
// ❌ PROJ-LR-010: 异常穿越 ROS2 回调边界导致节点崩溃
void on_timer() {
  throw std::runtime_error("unexpected");  // 直接 crash ROS2 node
}
// 检测: cert-err60-cpp + 代码审查
// 解释: ROS2 回调内异常无上层 handler，直接 terminate L3 节点 → ship enters DEGRADED
// ✅ Fixed: try/catch 包围所有回调 + fallback
void on_timer() {
  try {
    // 业务逻辑
  } catch (const std::exception& e) {
    spdlog::error("[M4] on_timer exception: {}", e.what());
    transition_to_fallback_mode();
  }
}

// ❌ MISRA Rule 18.2.2: 析构函数抛异常
~Tracker() {
  if (cleanup_failed) throw std::runtime_error("cleanup failed");
  // 析构中抛异常 → 栈展开未完成 → 若已有一个异常在处理中 → std::terminate()
}
// 检测: clang-tidy cert-err31-cpp / cppcheck exceptThrowInDestructor
// 解释: C++11 起析构函数隐式 noexcept，显式抛异常调用 std::terminate
// ✅ Fixed: 析构函数 noexcept + 内部捕获
~Tracker() noexcept {
  try { cleanup(); }
  catch (...) { spdlog::critical("[M2] Tracker destructor failed"); }
}

// ❌ MISRA Rule 18.4.1: 空 catch 块吞异常
try { process_targets(); }
catch (...) {}  // 吞掉所有异常 → 无日志、无告警、无 fallback
// 检测: clang-tidy bugprone-empty-catch
// 解释: 空 catch(...) 是最危险的异常处理模式，所有故障静默消失 → 长期隐蔽 bug
// ✅ Fixed: 至少记录日志
try { process_targets(); }
catch (const std::exception& e) { spdlog::error("process_targets() failed: {}", e.what()); }
```

### 13.12 Lambda 与现代 C++ Top 2

```cpp
// ❌ MISRA Rule 7.0.5 变体: Lambda 捕获悬垂引用
auto timer = rclcpp::create_timer(node, 1s, [&target]() {
  process(*target);  // target 引用可能已失效（异步回调）
});
// 检测: clang-analyzer-cplusplus.NewDeleteLeaks / cppcheck danglingReference
// 解释: ROS2 timer 回调异步执行，引用捕获的局部变量在回调触发前已析构
// ✅ Fixed: shared_ptr 延长生命周期
auto target_ptr = std::make_shared<Target>(target);
auto timer = rclcpp::create_timer(node, 1s, [target_ptr]() { process(*target_ptr); });

// ❌ MISRA Rule 15.0.1 变体: 移动后使用（use-after-move）
auto state = std::move(temp_state);
double sog = temp_state.sog;  // temp_state 已移走，处于 valid-but-unspecified 状态
// 检测: clang-tidy bugprone-use-after-move / clang-analyzer
// 解释: Move 后原对象处于"有效但未指定"状态，读其成员是 UB（取决于实现）
// ✅ Fixed: 移动后不再使用原对象
auto state = std::move(temp_state);
// temp_state 不再使用 — 作用域结束后自然析构
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
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：MISRA C++:2023 主基线 + AUTOSAR C++14 设计指南补充 + 50 高频违规修复（注：规则计数后经核实为 MISRA C:2012 混用，v1.2 已修正） |
| v1.1 | 2026-05-14 | Claude（D1.4） | D1.4 初版整合：新增 ROS2 节点约定（§1）、消息发布约定（§2）、Git 约定（§7）、多船型规则（§9）、ErrorCode 范围（§8.1）；整合 static-analysis-policy.md 关键配置；精简高频违规示例至 Top 3/5。已归档至 `docs/Design/Archive/Old Modules/Implementation/coding-standards.md` |
| v1.2 | 2026-05-20 | 架构师（D1.4 交付） | **D1.4 正式交付**：修正 MISRA C++:2023 规则计数为官方值（5 Mandatory + 122 Required Rules + 4 Required Directives + 48 Advisory = 179）；新增 PATH-S 严格规则独立章节（§4.4）；新增 M8 HMI 裁剪集（§4.5）；新增 clang-tidy 9 大类说明（§4.6）；新增 cppcheck MISRA C++:2023 配置章节（§4.7）；新增 10 条项目本地规则 PROJ-LR-001~010（§4.8）；新增 CI 集成章节（§4.9）；§13 修复模式扩至 50 个（新增 §13.7–13.12 共 6 子节 24 模式）；更新 §16 引用为官方来源 |

---

## 16. 引用

- **MISRA C++:2023 官方页**（2023-10 发布，C++17 目标）— [MISRA](https://misra.org.uk/product/misra-cpp2023/) 🟢 A
- **MISRA C++:2023 规则执行摘要**（179 = 5 Mandatory + 122 Required Rules + 4 Required Directives + 48 Advisory；175 可判定 + 4 不可判定）— [Helix QAC Enforcement Summary](https://help.perforce.com/helix-qac/enforcement/doc/MISRA_M2CPP.html) 🟢 A
- **MISRA C++:2023 完整规则清单**（Klocwork 映射）— [Klocwork](https://help.klocwork.com/2025.4/en-us/concepts/misracpp2023mappedtoklocworkcheckers.htm) 🟢 B
- **AUTOSAR C++14 Coding Guidelines**（设计层指南，MISRA C++:2023 吸收了 ~91%）🟢 B
- **GCC 14.3 LTS** — [gcc.gnu.org](https://gcc.gnu.org/) 🟢 A
- **LLVM clang-tidy 完整检查列表**（23.0.0git）— [clang.llvm.org](https://clang.llvm.org/extra/clang-tidy/checks/list.html) 🟢 A
- **cppcheck 官方文档** — [cppcheck.sourceforge.io](https://cppcheck.sourceforge.io/) 🟢 A
- **PEP 8** — Python 编码规范
- **ROS 2 C++ Style Guide** — [docs.ros.org](https://docs.ros.org/en/jazzy/Contributing/Code-Style-Language-Versions.html) 🟢 A
- **v1.1.3-pre-stub 架构报告** — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（接口契约、ODD 框架、模块全景、§11 PATH-S 独立性要求）
- **static-analysis-policy.md**（v1.0 存档）— `docs/Design/Archive/Old Modules/Implementation/static-analysis-policy.md`（Polyspace / Coverity / ASan / Ruff / mypy 配置完整版）
- **D1.2 CI/CD 关闭报告** — `docs/Design/Phase 1/D1.2-cicd-pipeline/D1.2-report.md`（clang-tidy 全 15 包 green / independence OK）
- **IEC 61508-3 Table A.5** — Software Verification Techniques（SIL 2 静态分析 Highly Recommended）
- **DNV-CG-0264 §4.6** — 自主航行决策功能形式化证据要求
