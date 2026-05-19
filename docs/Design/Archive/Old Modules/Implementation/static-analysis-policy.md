# 静态分析策略（Static Analysis Policy）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-STATIC-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（CI 强制门禁） |
| 适用范围 | 8 个 L3 模块（M1–M8）+ `l3_msgs` |
| 关联文件 | `coding-standards.md`（规则映射）/ `ci-cd-pipeline.md`（pipeline 集成）/ `00-implementation-master-plan.md` §3.3 |

> **强制度**：本文 §4–§7 工具门禁 **全部由 CI 阻断**；M1/M7 安全关键路径 + 决策路径 + HMI 路径分别有不同强度门槛。

---

## 1. 目标与原则

### 1.1 静态分析在 SIL 2 / DAL B 中的角色

按 **IEC 61508-3 Table A.5**（Software Verification Techniques），SIL 2 软件须采用 **静态分析**（Highly Recommended）作为验证手段之一。本项目静态分析门禁直接服务以下证据：

- **CCS《船用软件安全及可靠性评估指南》** — 白盒可审计要求
- **IEC 61508-3 §7.4.4** — 软件设计与开发的系统性故障避免
- **DNV-CG-0264 §4.6** — 自主航行决策功能的形式化证据
- **DO-178C DAL B**（关联参考）— 静态分析作为结构覆盖率补充证据

### 1.2 工具多样性原则（IEC 61508-3 §C.5.4）

> 当用单一工具覆盖 SIL 2 时，须证明该工具的 **诊断覆盖度**（DC）；当用多工具组合时，可通过**多样性**降低单工具失效的影响。

本项目对 **M1 / M7 安全关键路径** 采用 **3+ 工具多样化覆盖**：

```
路径 = M1 / M7
   ├── Polyspace Code Prover（形式化抽象解释，TUV SUD 认证 SIL 4）
   ├── Coverity（多样性 cross-check，TUV SUD 认证 SIL 4）
   ├── clang-tidy（CI 主 lint，MISRA C++:2023 部分规则）
   └── cppcheck Premium（MISRA C++:2023 完整规则覆盖）

路径 = M2 / M3 / M4 / M5 / M6
   ├── clang-tidy
   └── cppcheck Premium

路径 = M8 / l3_msgs
   ├── clang-tidy（裁剪集）
   └── cppcheck Premium（裁剪集）
```

---

## 2. 工具清单与版本锁定

| 工具 | 版本 | 许可类型 | M1/M7 | M2–M6 | M8 |
|---|---|---|---|---|---|
| **clang-tidy** | 20.1.8 | Apache 2.0 (LLVM) | 强制 | 强制 | 强制（裁剪）|
| **cppcheck Premium** | 26.1.0 | 商业（每席位） | 强制 | 强制 | 强制（裁剪） |
| **cppcheck OSS** | 2.20.0 | GPLv3 | 备选 | 备选 | 备选 |
| **Polyspace Code Prover** | R2025b（CI 锁定） | 商业（每席位） | **强制** | 推荐 | — |
| **Coverity（Black Duck）** | 2026.x | 商业 | **推荐**（多样性 cross-check） | — | — |
| **AddressSanitizer / UBSan / TSan** | GCC 14.3 / Clang 20.1.8 自带 | 编译器内嵌 | 强制（debug 构建） | 强制 | 强制 |
| **Ruff**（Python lint） | 0.6.x | MIT | — | — | 强制（M2 工具 + M8 后端） |
| **mypy strict** | 1.10+ | MIT | — | — | 强制（M2 工具 + M8 后端） |

> **"强制"** = CI 阻断；**"推荐"** = 不阻断，但 reviewer 须 review 报告；**"备选"** = 商业版预算无法覆盖时的 fallback。

---

## 3. 路径强度矩阵（详化 `00-implementation-master-plan.md` §3.3）

| 路径 | 模块 | 工具组合 | MISRA 规则覆盖 | 阻断阈值 |
|---|---|---|---|---|
| **PATH-S** 安全关键 | M1 / M7 | Polyspace + (Coverity 推荐) + clang-tidy + cppcheck Premium + ASan/UBSan/TSan | **完整 179 规则** + AUTOSAR 设计补充 | **0 critical / 0 major / ≤ 5 minor** |
| **PATH-D** 决策 | M2 / M3 / M4 / M5 / M6 | clang-tidy + cppcheck Premium + ASan/UBSan/TSan | **完整 179 规则** | **0 critical / ≤ 3 major / ≤ 15 minor** |
| **PATH-H** HMI | M8 | clang-tidy（裁剪）+ cppcheck Premium（裁剪）+ ASan/UBSan | 裁剪集 ~120 规则 | **0 critical / ≤ 5 major / ≤ 20 minor** |
| **PATH-Y** Python | M2 工具 + M8 后端 | Ruff + mypy strict | PEP 8 + Ruff 全 + mypy strict | **0 error / 0 warning** |

**严重度定义**：
- **critical** = MISRA Rule "Mandatory" 违反 + 任何 UB 检测 + 任何内存安全检测
- **major** = MISRA Rule "Required" 违反 + 项目硬规则违反（详见 `coding-standards.md` §3）
- **minor** = MISRA Rule "Advisory" 违反 + 风格 / 可读性建议

---

## 4. MISRA C++:2023 规则集（179 条 + 项目添加项）

### 4.1 规则分类（按 MISRA C++:2023 官方）

| 类别 | 数量 | 示例规则 | 重点关注 |
|---|---|---|---|
| Mandatory（强制） | 18 | Rule 4.1.1（类型显式大小） | 全部启用，无豁免可能 |
| Required（必需） | 132 | Rule 6.6.x（switch default） | 全部启用，豁免须三方签 |
| Advisory（建议） | 29 | Rule 8.4.1（命名风格） | PATH-S/D 启用，PATH-H 部分 |

### 4.2 项目添加项（10 条 LOCAL 规则）

> 这些是项目针对海事自主航行场景添加的额外规则，**不在** MISRA C++:2023 官方清单中。

| 规则 ID | 规则要点 | 触发示例 | 强度 |
|---|---|---|---|
| **PROJ-LR-001** | M1 / M7 禁止动态内存分配 | `make_unique<T>()` / `new T` 在 `m7_safety_supervisor` package | PATH-S 强制 |
| **PROJ-LR-002** | M1 / M7 禁用异常 | `throw` 在 PATH-S；编译时 `-fno-exceptions` | PATH-S 强制 |
| **PROJ-LR-003** | M7 禁用 CasADi / IPOPT / GeographicLib / nlohmann::json 包含（独立性约束）| `#include <casadi/...>` 在 M7 | PATH-S 强制 |
| **PROJ-LR-004** | 禁止 M7 包含 M1–M6 内部头文件（仅允许 `l3_msgs/msg/*.hpp`） | `#include "mass_l3/m1/odd_state_machine.hpp"` 在 M7 | PATH-S 强制 |
| **PROJ-LR-005** | 函数循环复杂度阈值（M1/M7 ≤ 8；其他 ≤ 10） | clang-tidy `readability-function-cognitive-complexity` | 全路径 |
| **PROJ-LR-006** | 浮点 `==` / `!=` 比较禁用（用 `std::abs(a-b) < eps`） | `if (cpa == 0.0)` | 全路径 |
| **PROJ-LR-007** | 禁止 raw seconds 浮点；时间用 `std::chrono` | `double dt = (t2 - t1)` | 全路径 |
| **PROJ-LR-008** | 角度跨模块边界统一 deg；模块内部统一 rad | `compute_heading()` 返回 rad 但 .msg 字段是 deg 命名 `heading_min_deg` | 全路径 |
| **PROJ-LR-009** | 禁止 `std::thread::detach()` | `t.detach()` 在任何路径 | 全路径 |
| **PROJ-LR-010** | 禁止 `auto` 隐藏类型（除 lambda / range-for / iterator）| `auto x = computeWorldState();` 函数返回类型不明显 | PATH-S/D 强制；PATH-H advisory |

### 4.3 豁免（Suppression）规则

> **默认禁止豁免**。豁免须满足以下三条件：
>
> 1. PR 中提供 `// MISRA-suppress: <rule-id> reason: <text>` 注释
> 2. PR 描述链接 `docs/Implementation/exemptions/exemption-{N}.md`，含技术理由 + reviewer 双签 + （PATH-S 路径下）TUV / CCS 验船师确认
> 3. 单 PR 累计豁免 ≤ 3 处；超过须拆 PR

豁免文件模板：

```markdown
# 豁免登记 - <项目模块> - <YYYY-MM-DD>

| 字段 | 值 |
|---|---|
| 豁免 ID | EXEMPT-<MODULE>-<NNN> |
| 路径 | PATH-S / PATH-D / PATH-H |
| 规则 ID | MISRA-CPP-2023 R<x.y.z> 或 PROJ-LR-<NNN> |
| 文件 + 行号 | `src/m5/mid_mpc.cpp:123` |
| 违反原因 | （技术细节）|
| 替代缓解 | （单元测试 / 运行时检查 / 等）|
| Reviewer 双签 | <name1> + <name2> |
| TUV / CCS 确认 | （PATH-S 必填）|
| 有效期 | 至 v<X.Y> 完成或 <YYYY-MM-DD> |
```

---

## 5. 工具具体配置

### 5.1 clang-tidy（`.clang-tidy`）

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
    value: '10'    # PATH-S/D
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
  - key: cppcoreguidelines-avoid-do-while.IgnoreMacros
    value: 'true'
```

**M1 / M7 专属覆盖**（`.clang-tidy.path-s`）：

```yaml
CheckOptions:
  - key: readability-function-size.LineThreshold
    value: '40'
  - key: readability-function-cognitive-complexity.Threshold
    value: '8'
  - key: cppcoreguidelines-avoid-non-const-global-variables.IgnorePolymorphic
    value: 'false'
  # 禁用动态分配的额外 lint
  - key: cppcoreguidelines-no-malloc.Allocations
    value: 'std::malloc;std::calloc;std::realloc;malloc;calloc;realloc'
  - key: cppcoreguidelines-no-malloc.Deallocations
    value: 'std::free;free'
```

### 5.2 cppcheck Premium（`cppcheck-misra-cpp-2023.cfg`）

```ini
# Cppcheck Premium config for MASS L3 TDL
[platform]
type = unix64
charset = utf-8

[standard]
language = c++17
posix = true

[misra-cpp-2023]
enable = all
mandatory = error          # all Mandatory rules → error
required = error           # all Required rules → error
advisory = warning         # all Advisory rules → warning

[suppressions]
# 仅在 docs/Implementation/exemptions/ 下登记后才允许添加
# 格式: <rule-id>:<file>:<line> reason="<text>"

[output]
format = sarif             # SARIF 格式供 GitLab Code Quality 集成
file = build/cppcheck-report.sarif
```

**M1 / M7 专属**：所有 `advisory` 也升级为 `error`。

### 5.3 Polyspace Code Prover（M1 / M7 强制）

Polyspace **Code Prover**（形式化抽象解释 — 证明运行时错误**不存在**，而非检测错误）。

```matlab
% Polyspace project file (m1.psprj 或 m7.psprj)
project = polyspace.Project('mass_l3_m7_safety_supervisor');
project.Configuration.TargetCompiler = 'gnu14';
project.Configuration.CppStandardVersion = 'cpp17';

% Code Prover 模式（vs. Bug Finder）
project.Configuration.Solver = 'code-prover';

% 检查项
project.Configuration.CodeProverChecksEnabled = {
    'OutOfBoundsArray', ...           % 数组越界
    'NonInitializedVariable', ...     % 未初始化变量
    'IllegalRead', ...                % 非法读
    'IllegalDereference', ...         % 非法解引用
    'OverflowUnderflow', ...          % 溢出
    'DivisionByZero', ...             % 除零
    'IllegalShift', ...               % 移位越界
    'NonReachable', ...               % 不可达代码
    'CertC_Cpp', ...                  % CERT C/C++
    'IEC_61508_Mandatory', ...        % IEC 61508 强制项
    'MISRA_Cpp_2023_Mandatory', ...
    'MISRA_Cpp_2023_Required'
};

% 结果级别
project.Configuration.AssumeUnreachableSafe = false;
project.Configuration.MaxOrangeStrategy = 'extensive';   % 尽可能消除 'orange' 不确定结果

% 报告
project.Configuration.ReportFormat = 'html';
project.Configuration.ReportTemplate = 'CodeProver';
```

**Polyspace 结果分类**：
- **Red**（证明有错误）— **0 容忍**，CI 阻断
- **Green**（证明无错误）— 目标
- **Orange**（不确定）— **PATH-S 路径**：阈值 ≤ 1% 的"orange"占比；超过须人工分析或追加 assertion 收紧条件
- **Gray**（不可达代码）— 检查是否真的不可达

### 5.4 Coverity（推荐，M1 / M7 多样性 cross-check）

```bash
# Coverity build capture
cov-configure --gcc
cov-build --dir cov-int colcon build --packages-select m1_odd_envelope_manager m7_safety_supervisor

# Coverity analyze
cov-analyze --dir cov-int \
    --enable-default \
    --enable-constraint-fpp \
    --concurrency \
    --security \
    --coding-standard MISRA-CPP-2023 \
    --coding-standard CERT-CPP

# Defect 阈值（M1/M7）
# - High impact: 0
# - Medium impact: 0
# - Low impact: ≤ 5
cov-format-errors --dir cov-int --html-output cov-report
```

### 5.5 AddressSanitizer / UBSan / ThreadSanitizer

在 Debug 构建启用（`CMAKE_BUILD_TYPE=Debug`）：

```bash
# Build
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
             -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

# Run unit tests with ASan
ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
colcon test
```

**ThreadSanitizer**（M5 / M2 多线程模块独立运行）：

```bash
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
             --packages-select m2_world_model m5_tactical_planner

TSAN_OPTIONS=halt_on_error=1 colcon test --packages-select m2_world_model m5_tactical_planner
```

> **重要**：ASan 与 TSan **不可同时启用**（互斥）；CI 中分别跑两次。

### 5.6 Ruff（Python lint）

`ruff.toml`：

```toml
target-version = "py310"
line-length = 100

[lint]
select = [
    "E", "W",      # pycodestyle
    "F",           # pyflakes
    "C",           # mccabe complexity
    "B",           # flake8-bugbear
    "SIM",         # flake8-simplify
    "I",           # isort
    "N",           # pep8-naming
    "D",           # pydocstyle (Google style)
    "UP",          # pyupgrade
    "ANN",         # type annotations required
    "S",           # bandit security
    "BLE",         # blind-except
    "PT",          # pytest style
    "RET",         # return logic
    "PL",          # pylint subset
]
ignore = [
    "D100",        # missing module docstring (allowed for tools/)
    "D104",        # missing package docstring
    "ANN101",      # missing self type
    "ANN102",      # missing cls type
]
[lint.mccabe]
max-complexity = 10

[lint.pydocstyle]
convention = "google"

[lint.per-file-ignores]
"tests/**" = ["D", "ANN", "S101"]      # tests 允许 assert
```

### 5.7 mypy strict（Python 类型）

`mypy.ini`：

```ini
[mypy]
python_version = 3.10
strict = True
warn_unused_ignores = True
warn_redundant_casts = True
warn_unreachable = True
disallow_any_generics = True
disallow_subclassing_any = True
disallow_untyped_calls = True
disallow_untyped_defs = True
disallow_incomplete_defs = True
disallow_untyped_decorators = True
no_implicit_optional = True
strict_equality = True

[mypy-rclpy.*]
ignore_missing_imports = True   # ROS2 Python stub 缺失（rclpy 自身有 stub，但子包不全）

[mypy-geographiclib.*]
ignore_missing_imports = True
```

---

## 6. M8 HMI 简化裁剪集

M8 涉及 Web 后端模式（HTTP 请求 / WebSocket / 异常控制流），部分 MISRA C++:2023 规则与现代 Web 模式冲突。**裁剪后约 120 规则**：

### 6.1 裁剪规则清单（M8 豁免）

| 规则类别 | 裁剪 |
|---|---|
| **动态分配限制** | 裁剪 PROJ-LR-001（HTTP 请求 / 响应天然动态）|
| **异常禁用** | 裁剪 PROJ-LR-002（HTTP 框架普遍用异常）|
| **AUTOSAR A8-5-2 / A12-1-1** | 裁剪部分 OO 严格规则（保留基础）|
| **MISRA Rule 14.x.x**（模板深度） | 裁剪（Web 框架模板深度高）|
| **AUTOSAR A18-9-1**（lambda 捕获） | 裁剪（callback 模式必需）|

### 6.2 M8 仍保留的强约束

| 规则 | 理由 |
|---|---|
| **PROJ-LR-005** 循环复杂度 ≤ 10 | 可读性 |
| **PROJ-LR-006** 浮点 `==` 禁用 | 数值正确性 |
| **PROJ-LR-008** 角度 deg/rad 边界 | 跨模块一致性 |
| **PROJ-LR-009** thread detach 禁用 | 资源泄漏防护 |
| **MISRA Rule 21.x** 禁用 malloc/free 裸调用 | RAII 一致性（除 HTTP 框架内部） |

### 6.3 M8 Python 部分（FastAPI / Flask）

完全沿用 §5.6 Ruff + §5.7 mypy strict 配置；**无裁剪**。

---

## 7. CI 集成（详细见 `ci-cd-pipeline.md`）

静态分析对应 CI Phase 3 "static analysis"：

```yaml
# .gitlab-ci.yml 摘录
stages:
  - lint
  - unit-test
  - static-analysis
  - integration-test
  - release

clang-tidy:
  stage: static-analysis
  script:
    - colcon build --packages-select-by-dep mass_l3
    - find src -name "*.cpp" -exec clang-tidy --warnings-as-errors=* {} +
  artifacts:
    reports:
      codequality: build/clang-tidy.sarif

cppcheck-misra:
  stage: static-analysis
  script:
    - cppcheck-premium --project=mass_l3.cppcheck --addon=misra-cpp-2023 --inline-suppr
  artifacts:
    reports:
      codequality: build/cppcheck-report.sarif

polyspace-m1-m7:
  stage: static-analysis
  rules:
    - if: $CI_COMMIT_BRANCH == "main" || $CI_PIPELINE_SOURCE == "merge_request_event"
      changes:
        - src/m1_odd_envelope_manager/**
        - src/m7_safety_supervisor/**
  script:
    - polyspace-cli -prog mass_l3_m1_m7 -lang cpp -cpp-version cpp17 \
                    -checkers MISRA_Cpp_2023_Mandatory,MISRA_Cpp_2023_Required,IEC_61508_Mandatory \
                    -results-dir polyspace-results
    - polyspace-cli -gen-report -results-dir polyspace-results -format html
  artifacts:
    paths:
      - polyspace-results/
    when: always

asan-ubsan:
  stage: unit-test    # 与单元测试合并
  script:
    - colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug \
                   -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    - ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 colcon test

tsan:
  stage: unit-test
  rules:
    - changes:
        - src/m2_world_model/**
        - src/m5_tactical_planner/**
  script:
    - colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug \
                   -DCMAKE_CXX_FLAGS="-fsanitize=thread" \
                   --packages-select m2_world_model m5_tactical_planner
    - TSAN_OPTIONS=halt_on_error=1 colcon test --packages-select m2_world_model m5_tactical_planner

ruff-mypy:
  stage: lint
  script:
    - ruff check src/m2_world_model/python tools/ src/m8_hmi_transparency_bridge/python
    - mypy src/m2_world_model/python tools/ src/m8_hmi_transparency_bridge/python
```

---

## 8. Doer-Checker 独立性自动验证

按决策四 + ADR-001，M7 Safety Supervisor 不允许包含 M1–M6 内部头文件 / 第三方库。**CI 自动验证**：

`tools/ci/check-doer-checker-independence.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail

# Forbidden headers in M7 (M1-M6 internal headers)
FORBIDDEN_INTERNAL=(
    "mass_l3/m1/"
    "mass_l3/m2/"
    "mass_l3/m3/"
    "mass_l3/m4/"
    "mass_l3/m5/"
    "mass_l3/m6/"
)

# Forbidden 3rd-party libraries in M7
FORBIDDEN_3RD_PARTY=(
    "casadi/"
    "Ipopt"
    "GeographicLib/"
    "nlohmann/json"
)

VIOLATIONS=0

for header in "${FORBIDDEN_INTERNAL[@]}" "${FORBIDDEN_3RD_PARTY[@]}"; do
    matches=$(grep -rn "#include.*${header}" src/m7_safety_supervisor/ || true)
    if [[ -n "$matches" ]]; then
        echo "VIOLATION: M7 includes forbidden header '${header}':"
        echo "$matches"
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

# Allowed: only ROS2 standard, l3_msgs/msg/*.hpp, std::*, Eigen, spdlog
if [[ $VIOLATIONS -gt 0 ]]; then
    echo "Doer-Checker independence VIOLATED. See ADR-001 + decision四 (v1.1.2 §2.5 + §11.3)."
    exit 1
fi

echo "Doer-Checker independence: OK"
```

CI 集成：

```yaml
doer-checker-independence:
  stage: static-analysis
  script:
    - bash tools/ci/check-doer-checker-independence.sh
```

**违反 = 构建失败，不可豁免**。任何 M7 对 M1–M6 内部 header 的引用，意味着 ADR-001 失效，须立即架构层面解决。

---

## 9. 失败模式 + 升级路径

| 失败场景 | 阻断阶段 | 升级路径 |
|---|---|---|
| clang-tidy critical / major 违规 | CI Phase 1 lint | PR 作者 fix；24h 内未 fix → 自动 close PR |
| cppcheck Premium MISRA Mandatory 违规 | CI Phase 3 static-analysis | 同上 |
| Polyspace Red defect（PATH-S） | CI Phase 3 | PR 作者 + reviewer 双签 + 提交豁免登记（默认拒绝）；如不可避免，触发 ADR 评审 |
| Polyspace Orange defect 占比 > 1% | CI Phase 3 | 警告（不阻断）；累计 5 个 PR 超阈值 → 触发架构 finding |
| ASan / UBSan 错误 | CI Phase 2 unit-test | 必须 fix；不可豁免 |
| TSan race condition | CI Phase 2 | 必须 fix；不可豁免 |
| Doer-Checker independence 违反 | CI Phase 3 | **不可豁免**；触发 ADR-001 复审 |
| Coverity 商业授权失效（推荐项） | 不阻断 | 单独通知；fallback 到 cppcheck Premium 单工具覆盖（PATH-S 路径降级文档化）|
| Polyspace 商业授权失效 | CI Phase 3 阻断 PATH-S | 立即升级路径：Coverity TUV SUD 认证版 + cppcheck Premium 双工具替补；通知 CCS 验船师 |

---

## 10. 度量与持续改进

### 10.1 关键指标（每月报告）

| 指标 | 目标 | 实际（每月填）|
|---|---|---|
| CI 全绿率 | ≥ 95% | |
| clang-tidy 报告条数（PR-平均） | ≤ 3 | |
| cppcheck MISRA 违规率（每 1k 行）| ≤ 0.5（PATH-S）/ ≤ 2（PATH-D）/ ≤ 5（PATH-H）| |
| Polyspace Orange 占比（PATH-S） | ≤ 1% | |
| 豁免登记数（累计） | ≤ 20（项目周期）| |
| 单元测试覆盖率（lcov）| ≥ 90% | |

### 10.2 工具升级流程

工具版本升级（如 clang-tidy 20.1.8 → 21.x）须走以下流程：

1. 在 `tools/docker/Dockerfile.ci` 创建 PR 升级版本
2. 跑全量 CI；评估新版本引入的"假阳性"违规
3. 评估"真阳性"违规（之前漏检）— 列入修复 backlog
4. CCS 中期审查时报告工具版本变更（认证证据链一致性）

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：4 工具组合 + MISRA C++:2023 完整覆盖 + Doer-Checker 独立性 CI 自动验证 |

---

## 12. 引用

- **MISRA C++:2023** — [Parasoft 完整指南](https://www.parasoft.com/blog/misra-cpp-2023-guide/) 🟢 / [Perforce 公告](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) 🟢
- **Polyspace Code Prover R2025b** — [MathWorks](https://www.mathworks.com/products/polyspace-code-prover.html) 🟢
- **Coverity Safety Manual**（IEC 61508 SIL 4 / DO-178C DAL A 认证）— [Black Duck](https://documentation.blackduck.com/bundle/coverity-docs/page/webhelp-files/safety_start.html) 🟢
- **Polyspace IntegratedWorkflow R2026a**（前瞻）— [MathWorks](https://www.mathworks.com/products/polyspace/static-analysis-notes/integrated-workflow-r2026a.html) 🟡
- **clang-tidy 20.1.8** — [LLVM](https://releases.llvm.org/20.1.0/tools/clang/tools/extra/docs/ReleaseNotes.html) 🟢
- **cppcheck Premium**（支持 MISRA C++:2023） — [github.com/danmar/cppcheck](https://github.com/danmar/cppcheck/releases) 🟢
- **IEC 61508-3 Table A.5**（软件验证技术）
- **DNV-CG-0264 §4.6**（自主航行决策功能形式化证据）
