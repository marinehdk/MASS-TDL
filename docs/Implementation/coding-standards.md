# 编码规范（Coding Standards）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-CODING-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（CI 强制执行） |
| 主基线 | **MISRA C++:2023**（179 规则，覆盖 C++17）+ AUTOSAR C++14 设计指南补充 |
| 主语言 | C++17（GCC 14.3 LTS 基线 / Clang 20.1.8 多样化） |
| 副语言 | Python 3.10（PEP 8 + Ruff + mypy strict） |
| 适用范围 | 8 个 L3 模块（M1–M8）+ `l3_msgs` + `l3_external_msgs` 全部源代码 |
| 关联文件 | `00-implementation-master-plan.md` §3.2 / `static-analysis-policy.md` / `third-party-library-policy.md` |

> **强制度**：本文 §3–§7 规则 **全部由 CI 阻断**。规则违反必须 fix 后重提 PR；豁免须走 `docs/Implementation/exemptions/` 单独审批（默认拒绝）。

---

## 1. 路径强度分级

| 路径 | 模块 | 规则强度 | 静态分析门禁 |
|---|---|---|---|
| **安全关键路径** | M1（ODD/Envelope）+ M7（Safety Supervisor） | **MISRA C++:2023 完整 179 规则** + AUTOSAR C++14 设计指南 | Polyspace + Coverity + clang-tidy + cppcheck Premium 全启用 |
| **决策路径** | M2 / M3 / M4 / M5 / M6 | MISRA C++:2023 完整 179 规则 | clang-tidy + cppcheck Premium 全启用；M5（CasADi/IPOPT 依赖）部分豁免见 §10 |
| **HMI 路径** | M8 | MISRA C++:2023 简化裁剪集（约 120 规则） | clang-tidy + cppcheck Premium 简化集 |
| **Python 路径** | M2 ENC 工具 / M8 后端 | PEP 8 + Ruff + mypy strict | Ruff + mypy（CI 阻断） |

> **理由**：M1 是模式仲裁中枢，M7 是 SOTIF + IEC 61508 双轨 Checker，二者均映射到 IEC 61508 SIL 2 核心安全功能（v1.1.2 §11.1）；规则强度上调到行业最严以匹配认证证据要求。M8 HMI 路径不涉及决策核心，且部分规则（如禁用动态多态、禁用异常）与 Web 后端模式冲突，按 §6 裁剪。

---

## 2. 语言版本与编译选项

### 2.1 C++ 标准

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)        # 禁用 GNU 扩展（保证可移植）
```

### 2.2 编译选项（强制）

所有 colcon package 须在 `CMakeLists.txt` 启用以下 flags：

```cmake
target_compile_options(${PROJECT_NAME} PRIVATE
    # 警告级别（强制）
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

    # MISRA C++:2023 与编译器联动
    -Wuseless-cast                   # GCC 专属
    -Wlogical-op
    -Wduplicated-cond -Wduplicated-branches

    # 安全
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

**Release build 强制 `-O2`**（不允许 `-O3`，避免激进优化引发的 UB 暴露）。

### 2.3 Python 版本

- **基线 3.10**（Ubuntu 22.04 默认；与 ROS2 Jazzy 兼容）
- 不允许 3.9 及以下（缺少 `match` 语句、PEP 604 union 语法等本规范依赖的特性）
- **不允许 3.11+ 特性**（`Self` type、exception groups 等）— 保持兼容性

---

## 3. C++ MISRA C++:2023 主规则集

> **完整 179 规则清单**见 `static-analysis-policy.md` §4 + 工具配置文件 `.clang-tidy` / `cppcheck-misra-cpp-2023.cfg`。本节列项目特有的强约束 + 高频违规说明。

### 3.1 类型安全（重要 MISRA 规则）

| MISRA Rule | 规则要点 | 项目应用 |
|---|---|---|
| **Rule 4.1.1** | 类型须显式定义大小（用 `<cstdint>` 而非 `int` / `long`） | 所有时间戳用 `int64_t`；所有航向 / 速度等浮点用 `double`（不用 `float`，防精度丢失）|
| **Rule 5.0.x** | 禁止隐式类型转换（数值缩窄 / 符号变化） | 强制使用 `static_cast`；浮点 → 整型必须显式 |
| **Rule 7.0.x** | 禁止使用 C 风格强转（`(T)x`） | 全部用 `static_cast` / `dynamic_cast` / `reinterpret_cast` / `const_cast` |
| **Rule 7.1.x** | 函数参数类型须匹配（包括 const 修饰） | 所有 `const` 引用必加 `const`；不允许"const 借口" |
| **Rule 11.x** | 指针算术受限 | L3 中**禁止裸指针算术**；用 `std::span<>` / `std::array<>` |

**项目硬规则**：
- **禁用 `auto` 推导隐藏类型**：除 lambda / range-for / iterator 三种例外，其他 `auto` 必须能让人立即看出类型
- **禁用 `std::any` / `std::variant<>` 包裹超过 5 个类型**：可读性差，推理负担高
- **禁用 `reinterpret_cast`**：除非在极少数 ROS2 序列化层，且须代码注释 + Polyspace 豁免 + reviewer 双签

### 3.2 内存与资源管理

| 规则 | 项目应用 |
|---|---|
| **MISRA Rule 21.x** | 禁用 `malloc/free` / `new/delete` 裸调用 |
| **AUTOSAR A18-5-2** | 强制 RAII；所有资源用 `std::unique_ptr<>` / `std::shared_ptr<>` / 自定义 RAII 包装 |
| **项目硬规则** | **M1 / M7 安全关键路径禁用动态分配**（启动后所有内存预分配；用 `std::array<>` / fixed-size buffers / 对象池） |
| **项目硬规则** | M5（MPC 求解）允许动态分配，但须：(1) 有上限（不允许无界增长）；(2) reviewer 标注；(3) ASan/Valgrind 测试 0 leak |

### 3.3 并发与异步

| 规则 | 项目应用 |
|---|---|
| **MISRA Rule 18.0.x** | 信号 / 异步处理须按 POSIX async-signal-safe 列表 |
| **AUTOSAR A14-1-1** | 模板递归深度受限（项目限 ≤ 4） |
| **项目硬规则** | **禁用裸 `std::thread::detach()`**：所有 thread 必须 joinable；用 `std::jthread`（C++20 待升级，C++17 期间用包装） |
| **项目硬规则** | 所有 mutex 用 `std::lock_guard<>` / `std::scoped_lock<>` RAII；禁用裸 `lock()` / `unlock()` |
| **项目硬规则** | **M7 不使用条件变量 / 信号量**：M7 仅响应 ROS2 callback，不持有内部 thread（保持 Doer-Checker 独立简单性）|

### 3.4 异常处理

| 规则 | 项目应用 |
|---|---|
| **MISRA Rule 18.1.x** | 所有 throw 须有显式 catch 链 |
| **项目硬规则** | **M1 / M7 安全关键路径禁用异常**：所有错误用 `std::expected<T, ErrorCode>`（C++23 待升级期间用 `tl::expected` 或自定义）；编译时 `-fno-exceptions` |
| **项目硬规则** | M2–M6 + M8 允许异常，但：(1) catch-all `catch(...)` 仅在 ROS2 node `spin()` 边界；(2) 异常类型须从 `std::exception` 派生；(3) 不允许在析构函数 throw |

### 3.5 控制流

| 规则 | 项目应用 |
|---|---|
| **MISRA Rule 6.x** | `if/else` / `switch` / 循环须有 `{}` 块（即使单语句） |
| **MISRA Rule 6.6.x** | `switch` 须有 `default:` 分支 |
| **MISRA Rule 6.4.4** | `case` 不允许 fall-through；除非显式 `[[fallthrough]];` |
| **项目硬规则** | 函数循环复杂度（cyclomatic complexity）≤ 10（M1/M7 ≤ 8）；超出须重构 |
| **项目硬规则** | 函数行数 ≤ 60 行（M1/M7 ≤ 40 行）；超出须拆分 |

### 3.6 浮点

| 规则 | 项目应用 |
|---|---|
| **AUTOSAR A0-4-2** | `float` / `double` 比较禁用 `==` / `!=`；用 `std::abs(a-b) < eps` |
| **项目硬规则** | 时间用 `std::chrono`，**不用** raw `double` 秒数（避免精度漂移）|
| **项目硬规则** | 角度统一用 **弧度（rad）**；**.msg 边界**统一用 **度（deg）**；模块内部转换在 `utils/angles.hpp` 中央实现 |
| **项目硬规则** | 经纬度统一用 **WGS84 度**（标量），**不用** GeographicLib 的内部 `Geocentric` / `LocalCartesian` 类型流出模块边界 |

---

## 4. 命名约定

### 4.1 C++ 命名

| 类型 | 风格 | 示例 |
|---|---|---|
| **命名空间** | `snake_case`（lower） | `mass_l3::m1_envelope`, `mass_l3::common` |
| **类 / 结构体 / enum class** | `PascalCase` | `OddStateMachine`, `EncounterClassification`, `enum class VetoReasonClass` |
| **函数 / 方法** | `lower_snake_case` | `compute_conformance_score()`, `update_world_state()` |
| **变量（局部 / 参数）** | `lower_snake_case` | `current_zone`, `tcpa_s`, `target_id` |
| **成员变量** | `lower_snake_case_` （末尾下划线）| `current_zone_`, `last_update_stamp_` |
| **静态常量 / constexpr** | `kPascalCase` | `kMaxTargets = 256`, `kCpaThresholdM = 50.0` |
| **宏（避免使用）** | `UPPER_SNAKE_CASE`（仅头文件守卫 / 极少 platform 抽象）| `MASS_L3_M1_INCLUDE_GUARD_HPP_` |
| **模板参数** | `PascalCase` 或 `T`（单参） | `template<typename TargetT>`, `template<typename T>` |
| **文件名** | `lower_snake_case.{hpp,cpp}` | `odd_state_machine.hpp`, `cpa_calculator.cpp` |

**项目硬规则**：
- 不使用匈牙利前缀（`m_`, `p_`, `i_` 等）
- 不使用单字母变量名（除 `i`/`j`/`k` 循环 + lambda 短捕获）
- 不缩写易混淆词（`tgt` ✗ → `target` ✓；`cnt` ✗ → `count` ✓；但 `cpa` / `tcpa` / `tmr` / `tdl` / `mrm` 因领域共识保留）

### 4.2 ROS2 节点 / topic / service 命名

| 类型 | 风格 | 示例 |
|---|---|---|
| **节点名** | `snake_case` | `m1_envelope_manager`, `m7_safety_supervisor` |
| **Topic 名** | `/<layer>/<source_module>/<message_kind>` | `/l3/m1/odd_state`, `/l3/m2/world_state`, `/l3/m5/avoidance_plan` |
| **跨层 topic** | `/<source_layer>/<message_kind>` | `/fusion/tracked_targets`, `/fusion/own_ship_state`, `/checker/veto_notification` |
| **Service 名** | `/<layer>/<message_kind>` | `/l3/m3/route_replan_request` |
| **参数（YAML key）** | `lower_snake_case` | `cpa_threshold_m`, `tmr_min_s` |

**完整 topic 命名清单**见 `ros2-idl-implementation-guide.md` §4。

### 4.3 Python 命名

PEP 8 标准：
- 模块 / 函数 / 变量：`lower_snake_case`
- 类：`PascalCase`
- 常量：`UPPER_SNAKE_CASE`
- 私有：`_leading_underscore`

---

## 5. 注释规范

> **本项目继承 v1.1.2 全局规则**：注释只解释 WHY，不解释 WHAT。well-named identifiers 已说明 WHAT。

### 5.1 强制注释场景

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

### 5.2 禁止注释场景

- **解释 WHAT**：`// increment counter` ✗（命名已说明）
- **任务 / PR 历史**：`// added by John for issue #123` ✗（git blame 即可）
- **被注释掉的代码**：`// auto x = old_method();` ✗（git history 即可）
- **任何形式的祝福 / 装饰**：`// ✨ super cool function ✨` ✗

### 5.3 函数级 doc 注释（强制项）

公共 API 函数（header 中暴露的）须有 Doxygen 风格 doc，含以下字段：

```cpp
/**
 * @brief Compute Closest Point of Approach for an own-ship/target pair.
 *
 * Per v1.1.2 §6.2 + IMO COLREG geometric definition. Time-aligned to own_ship.stamp
 * (target state extrapolated to own_ship.stamp before computation).
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

模块**内部**函数（`.cpp` 中 `static` 或匿名命名空间）**不要求** doc 注释，除非命中 §5.1 五种场景之一。

---

## 6. M8 HMI 简化裁剪集

M8 涉及 Web 后端 + REST + WebSocket，部分 MISRA C++:2023 规则与现代 Web 模式冲突。**裁剪后的规则集（约 120 规则）**：

**保留（强制）**：
- §3.1 类型安全全部
- §3.2 RAII + 资源管理全部（保留 `unique_ptr` / `shared_ptr`）
- §3.5 控制流全部
- §3.6 浮点比较

**裁剪（M8 豁免）**：
- §3.2 项目硬规则"M1/M7 禁用动态分配"不适用于 M8（HTTP 请求 / 响应天然动态）
- §3.4 项目硬规则"M1/M7 禁用异常"不适用（HTTP 框架普遍用异常）
- AUTOSAR A8-5-2 / A12-1-1 等 OO 严格规则裁剪（保留基础，去掉模板深度限制）

**完整裁剪清单**见 `static-analysis-policy.md` §6。

---

## 7. Python 编码规范

### 7.1 工具链

```
.python-version       → 3.10
ruff.toml             → Ruff lint 配置
mypy.ini              → mypy strict 配置
.pre-commit-config.yaml → Black / isort / ruff / mypy 钩子
pyproject.toml        → 依赖锁定（poetry / pip-tools）
```

### 7.2 强制规则

- **PEP 8 完整**（line ≤ 100 chars，不是 79）
- **Black 格式化**（一致风格，不可争论）
- **Ruff lint 全规则**（含 E / W / F / C / B / SIM 类）
- **mypy strict mode**：所有函数必须有完整类型注解；`Any` 仅在第三方库 stub 缺失时；CI 阻断 `# type: ignore` 滥用
- **import 顺序**：标准库 / 第三方 / 项目（`isort` 自动）

### 7.3 项目硬规则

- 所有 IO（文件 / 网络）用 `pathlib` / `httpx`，**不用** `os.path` / `requests`
- 所有 dataclass 用 `dataclasses.dataclass(frozen=True, slots=True)`（默认不可变 + 内存优化）
- 单元测试用 `pytest`，**不用** `unittest`
- 禁用 `*` 通配 import（CI 阻断）
- 禁用 `print()` 直接输出（用 `logging` 或项目 `mass_l3_log` wrapper）

---

## 8. 错误处理

### 8.1 C++ 错误处理决策树

```
错误来源是？
├── 编程错误（precondition 违反 / API 误用）
│   └── M1 / M7 路径   → assert + 崩溃（不允许在生产路径忽略）
│       M2–M6 / M8     → 异常 std::logic_error
│
├── 运行时错误（外部输入 / 资源不足）
│   ├── 可恢复：消息字段超范围 / 来源置信度低
│   │   └── M1 / M7   → std::expected<T, ErrorCode>（不抛异常）
│   │       M2–M8     → 异常 std::runtime_error 或 expected
│   │
│   └── 不可恢复：系统状态损坏 / 内存耗尽
│       └── 全模块     → 触发 M1 CRITICAL 状态 + 进入 MRC（v1.1.2 §3.5）
│
└── ROS2 通信错误（消息丢失 / 反序列化失败）
    └── 全模块         → 节点级别 try-catch 转 spdlog::error + 心跳标记 DEGRADED
```

### 8.2 错误码 enum

每个模块定义自己的 `ErrorCode` enum（在 `<module>/error.hpp`），但**根错误类型**集中在 `mass_l3::common::ErrorCode`：

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

模块特定错误码从 `1000` 起：M1 = 1000–1999，M2 = 2000–2999，..., M8 = 8000–8999。

### 8.3 错误日志规范

每条错误日志须包含：
- **timestamp**（自动）
- **module**（spdlog logger name）
- **error_code**（M3 = `mass_l3::m3::ErrorCode::eta_window_invalid`）
- **context**（足够 reproduce 的字段值）

```cpp
spdlog::error("[M3][{}] eta_window_invalid: voyage_task.id={} earliest={} latest={} now={}",
              error_code_str(ErrorCode::EtaWindowInvalid),
              voyage_task.id, voyage_task.eta_window.earliest,
              voyage_task.eta_window.latest, now);
```

**严重等级映射**：
- `spdlog::trace` — 高频调试（默认 release 关闭）
- `spdlog::debug` — 单次决策细节
- `spdlog::info` — 正常状态变化（ODD 切换、行为切换）
- `spdlog::warn` — 退化但仍工作（DEGRADED 心跳）
- `spdlog::error` — 模块功能失效，但系统仍可用
- `spdlog::critical` — 系统级风险，触发 M1 CRITICAL / MRC

---

## 9. 头文件 + 包含

### 9.1 包含顺序（强制）

```cpp
// 1. 自身 header（如果存在对应的 .hpp）
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

### 9.2 头文件守卫

强制 `#pragma once`，**不用** `#ifndef` 宏（GCC 14 / Clang 20 完全支持）。

### 9.3 前向声明

允许且鼓励：减少编译依赖。但跨模块前向声明须慎用（避免破坏接口契约）。

---

## 10. 高频违规 + 修复模式（团队学习曲线）

> 团队首次接触 MISRA C++:2023，前 2 周开发速度会下降。本节列出 50 个高频违规 + 修复，加速适应。

### 10.1 类型安全 Top 10

```cpp
// ❌ Rule 4.1.1 violation
int x = 100;
long y = x + 1000;

// ✅ Fixed
int32_t x = 100;
int64_t y = static_cast<int64_t>(x) + 1000;

// ❌ Rule 5.0.6 violation (implicit narrowing)
double speed = 12.5;
int kn = speed;

// ✅ Fixed
double speed = 12.5;
int32_t kn = static_cast<int32_t>(speed);

// ❌ Rule 7.0.x violation (C-style cast)
auto* node = (rclcpp::Node*)raw_ptr;

// ✅ Fixed
auto* node = static_cast<rclcpp::Node*>(raw_ptr);
```

### 10.2 资源管理 Top 10

```cpp
// ❌ MISRA 21.2.x violation (raw new/delete)
auto* tracker = new TargetTracker();
// ... use tracker ...
delete tracker;

// ✅ Fixed (RAII)
auto tracker = std::make_unique<TargetTracker>();
// ... use tracker ...
// auto-cleanup on scope exit

// ❌ AUTOSAR A18-5-2 violation (no RAII for FILE*)
FILE* f = fopen("config.json", "r");
// ... read ...
fclose(f);

// ✅ Fixed
struct FileCloser { void operator()(FILE* f) const { if (f) fclose(f); } };
std::unique_ptr<FILE, FileCloser> f(fopen("config.json", "r"));
```

### 10.3 控制流 Top 10

```cpp
// ❌ MISRA 6.6.x violation (no default in switch)
switch (zone) {
  case OddZone::A: return 1.0;
  case OddZone::B: return 0.8;
  case OddZone::C: return 0.5;
  case OddZone::D: return 0.3;
}

// ✅ Fixed
switch (zone) {
  case OddZone::A: return 1.0;
  case OddZone::B: return 0.8;
  case OddZone::C: return 0.5;
  case OddZone::D: return 0.3;
  default:
    // INVARIANT: zone is one of A/B/C/D; reaching default means uninitialized state.
    spdlog::critical("[M1] zone={} out of enum range", static_cast<int>(zone));
    return 0.0;  // safest fallback
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

### 10.4 浮点 Top 5

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

### 10.5 并发 Top 5

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
  // ...
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

### 10.6 项目硬规则 Top 10（M1 / M7 安全关键路径）

```cpp
// ❌ Project rule violation (dynamic alloc in M7 hot path)
void M7Supervisor::on_safety_alert(const SafetyAlertMsg& msg) {
  auto handler = std::make_unique<AlertHandler>(msg);  // ❌
  handler->process();
}

// ✅ Fixed (object pool or stack alloc)
void M7Supervisor::on_safety_alert(const SafetyAlertMsg& msg) {
  AlertHandler handler(msg);  // stack allocation; no heap
  handler.process();
}

// ❌ Project rule violation (exception in M1 / M7)
void M1OddManager::compute_score() {
  if (input.invalid()) {
    throw std::runtime_error("invalid input");  // ❌ in M1/M7
  }
}

// ✅ Fixed (expected pattern)
ErrorOr<double> M1OddManager::compute_score() {
  if (input.invalid()) {
    return ErrorCode::InvalidArgument;
  }
  return /* score */;
}
```

### 10.7 Doer-Checker 独立性 Top 5

```cpp
// ❌ M7 violates independence by including M1 internal header
#include "mass_l3/m1/odd_state_machine.hpp"  // ❌ M7 不可包含 M1 内部 .hpp
class M7Supervisor {
  mass_l3::m1::OddStateMachine sm_;  // ❌
};

// ✅ Fixed (M7 reads M1 only via ROS2 messages)
#include "l3_msgs/msg/odd_state.hpp"  // ✅ 跨模块仅通过消息
class M7Supervisor {
  rclcpp::Subscription<l3_msgs::msg::OddState>::SharedPtr odd_sub_;
  // 通过 ROS2 callback 读 ODD 状态，不持有 M1 内部对象
};

// ❌ M7 uses CasADi (forbidden in M7)
#include <casadi/casadi.hpp>  // ❌ M7 禁用 CasADi
class M7Supervisor { /* ... */ };

// ✅ Fixed (M7 使用独立简单实现，不依赖 M5 优化栈)
class M7Supervisor {
  // M7 仅做阈值比较 + enum 分类，不做优化求解
};
```

---

## 11. 代码评审 checklist

PR review 时强制 reviewer 走以下 checklist（PR 模板自动注入）：

- [ ] 命名遵循 §4（snake_case / PascalCase / 等）
- [ ] 注释命中 §5.1 五种合法场景
- [ ] M1 / M7 路径无动态分配 / 无异常 / 无第三方非白名单库（§3.2 + §3.4 + `third-party-library-policy.md`）
- [ ] 错误处理符合 §8.1 决策树
- [ ] 头文件包含顺序符合 §9.1
- [ ] 函数 ≤ 60 行（M1/M7 ≤ 40 行）；循环复杂度 ≤ 10（M1/M7 ≤ 8）
- [ ] 单元测试覆盖率 ≥ 90%（用 lcov 报告）
- [ ] CI 5 阶段全绿（lint / unit / static / integration / release）
- [ ] 跨模块接口（订阅 / 发布消息）字段对齐 v1.1.2 §15.1 IDL（`ros2-idl-implementation-guide.md`）
- [ ] HAZID 校准点用 `[TBD-HAZID]` 注释 + YAML 注入（§5.1 第 4 项）

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：MISRA C++:2023 主基线 + AUTOSAR C++14 设计指南补充 + 50 高频违规修复 |

---

## 13. 引用

- **MISRA C++:2023 合并继任者公告**（2023-10）— [Perforce](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) 🟢 A
- **MISRA C++:2023 完整指南**（179 规则）— [Parasoft](https://www.parasoft.com/blog/misra-cpp-2023-guide/) 🟢 B
- **AUTOSAR C++14 Coding Guidelines**（设计层指南，仍有参考价值）
- **GCC 15 / 14 系列说明**— [gcc.gnu.org](https://gcc.gnu.org/gcc-15/) 🟢 A
- **Clang/LLVM 20.1 Release Notes** — [llvm.org](https://releases.llvm.org/20.1.0/tools/clang/docs/ReleaseNotes.html) 🟢 A
- **PEP 8** — Python 编码规范
- **ROS 2 C++ Style Guide** — [docs.ros.org](https://docs.ros.org/en/jazzy/Contributing/Code-Style-Language-Versions.html) 🟢 A
