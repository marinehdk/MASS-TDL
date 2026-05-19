# 第三方库政策（Third-Party Library Policy）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-3RDLIB-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（CI 强制 — 库白名单 + 独立性矩阵） |
| 适用范围 | 8 个 L3 模块（M1–M8）+ `l3_msgs` + `l3_external_msgs` 的全部第三方依赖 |
| 关联文件 | `00-implementation-master-plan.md` §3.5 / `coding-standards.md` §3.5 / `static-analysis-policy.md` §8 |

> **强制度**：任何超出本文 §2 白名单的第三方依赖须走 ADR 评审 + reviewer 双签 + （PATH-S 路径下）TUV / CCS 验船师确认。

---

## 1. 政策原则

### 1.1 设计目标

1. **可复现构建**：所有依赖锁定到具体版本，不依赖系统包（除 GCC / Clang / glibc 等）
2. **License 合规**：严禁 GPLv2/v3 强 copyleft 库泄漏到分发件（CCS 入级要求）
3. **Doer-Checker 独立性**（决策四 + ADR-001）：M7 与 M1–M6 不共享第三方库的强约束
4. **可审计供应链**：每个依赖须有 SBOM 记录 + 来源 URL + 校验和
5. **安全补丁可达**：所有依赖须在主流 CVE 数据库（NVD / GitHub Advisory）有覆盖

### 1.2 版本锁定策略

每个第三方库通过以下三层锁定：

```
1. tools/third-party/<lib>-<version>/        ← 源代码 vendor（git submodule）
2. tools/docker/Dockerfile.ci                ← 构建镜像层固化
3. install_requires in package.xml           ← ROS2 依赖声明
```

**禁止**：
- 直接从 system package（`apt install libcasadi-dev`）取依赖（系统包随 Ubuntu 升级漂移）
- 在代码中直接 `find_package(...)` 系统路径而不锁定版本（CMake `find_package(... 3.7.2 EXACT REQUIRED)`）
- 在 CI / Dockerfile 中用浮动 tag（如 `casadi:latest`）

---

## 2. 第三方库白名单（2026-05 锁定）

### 2.1 核心库（所有路径可用）

| 库 | 版本 | License | 用途 | 来源 URL |
|---|---|---|---|---|
| **ROS 2 Jazzy Jalisco** | 系统 | Apache 2.0 | 框架（rclcpp, rclpy） | [docs.ros.org/jazzy](https://docs.ros.org/en/jazzy/) 🟢 A |
| **Eigen** | 5.0.0（2025-09-30） | MPL-2.0 | 矩阵 / 向量运算 | [gitlab.com/libeigen/eigen](https://gitlab.com/libeigen/eigen/-/releases) 🟢 A |
| **spdlog** | 1.17.0（2026-01-05） | MIT | 日志 | [github.com/gabime/spdlog](https://github.com/gabime/spdlog/releases) 🟢 A |
| **fmt** | 11.0.x（spdlog 内嵌或独立） | MIT | 格式化（`std::format` 后备） | [github.com/fmtlib/fmt](https://github.com/fmtlib/fmt) 🟢 A |
| **tl::expected** | 1.1.0（header-only） | CC0-1.0（公有领域）| `std::expected` 的 C++17/20 polyfill（`coding-standards.md` §3.4 异常禁用路径用）| [github.com/TartanLlama/expected](https://github.com/TartanLlama/expected) 🟢 A |
| **GTest** | 1.17.0（要求 C++17） | BSD-3-Clause | C++ 单元测试 | [github.com/google/googletest](https://github.com/google/googletest/releases) 🟢 A |
| **GMock** | 1.17.0（含在 GTest） | BSD-3-Clause | C++ mock 框架 | 同上 🟢 A |
| **Cyclone DDS** | 0.10.x（ROS2 Jazzy 兼容版） | EPL-2.0 / EDL-1.0 | DDS RMW（默认） | [cyclonedds.io](https://cyclonedds.io/) 🟡（ASIL-D 进行中）|

### 2.2 决策路径专属（M2–M6 + M8 可用，M1/M7 禁用 — 详见 §3）

| 库 | 版本 | License | 用途 | 模块 | 来源 URL |
|---|---|---|---|---|---|
| **CasADi** | 3.7.2（2025-09-10） | LGPL-3.0 | NLP 框架（M5 MPC 求解） | M5 | [web.casadi.org/get](https://web.casadi.org/get/) 🟢 A |
| **IPOPT** | 3.14.19（CasADi 后端 NLP solver） | EPL-2.0 | 内点法求解器 | M5（经 CasADi 调用） | [github.com/coin-or/Ipopt](https://github.com/coin-or/Ipopt/releases) 🟢 A |
| **GeographicLib** | 2.7（2025-11-06） | MIT | WGS84 / 大地坐标投影 | M2, M3, M5 | [sourceforge/geographiclib](https://sourceforge.net/p/geographiclib/news/2025/11/geographiclib-27-released-2025-11-06/) 🟢 A |
| **nlohmann::json** | 3.12.0 | MIT | JSON 序列化（ASDR 决策日志 / M3 配置 / M8 后端） | M2 ENC 工具, M3, M8 | [github.com/nlohmann/json](https://github.com/nlohmann/json) 🟢 A |
| **Boost.Geometry** | 1.91.0（仅头文件子集） | BSL-1.0 | 多边形 / TSS 几何运算 | M2, M5, M6 | [boost.org/releases/latest](https://www.boost.org/releases/latest/) 🟢 A |
| **Boost.PropertyTree** | 1.91.0（仅头文件） | BSL-1.0 | YAML / XML 配置加载 | M2, M3, M8 | 同上 🟢 A |
| **yaml-cpp** | 0.8.0 | MIT | YAML 配置加载（备选 PropertyTree） | M2, M3, M8 | [github.com/jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp/releases) 🟢 A |

### 2.3 HMI 路径专属（M8 可用）

| 库 | 版本 | License | 用途 | 来源 URL |
|---|---|---|---|---|
| **FastAPI**（Python） | 0.115.x | MIT | Web 后端框架 | [fastapi.tiangolo.com](https://fastapi.tiangolo.com/) 🟢 A |
| **uvicorn**（Python） | 0.30.x | BSD-3-Clause | ASGI 服务器 | [github.com/encode/uvicorn](https://github.com/encode/uvicorn) 🟢 A |
| **websockets**（Python） | 13.x | BSD-3-Clause | WebSocket 实时推送 | [github.com/python-websockets/websockets](https://github.com/python-websockets/websockets) 🟢 A |
| **pydantic**（Python） | 2.x | MIT | 数据 schema 校验 | [docs.pydantic.dev](https://docs.pydantic.dev/) 🟢 A |
| **rclpy**（Python） | 系统（ROS2 Jazzy） | Apache 2.0 | ROS2 Python 绑定 | [docs.ros.org/jazzy/rclpy](https://docs.ros.org/en/jazzy/Tutorials/Beginner-Client-Libraries/Using-Python-Packages.html) 🟢 A |

### 2.4 测试 / 开发工具（不进生产分发）

| 工具 | 版本 | License | 用途 |
|---|---|---|---|
| **pytest**（Python） | 8.3.x | MIT | Python 单元测试 |
| **pytest-cov** | 5.0.x | MIT | 覆盖率 |
| **mypy** | 1.10.x | MIT | 类型检查 |
| **Ruff** | 0.6.x | MIT | Python lint |
| **clang-tidy** | 20.1.8 | Apache 2.0（LLVM） | C++ lint |
| **clang-format** | 20.1.8 | Apache 2.0 | C++ 格式化 |
| **cppcheck Premium** | 26.1.0 | 商业 | MISRA C++:2023 规则覆盖 |
| **lcov / gcovr** | apt 系统包 | GPLv2 | 覆盖率（仅开发期）|
| **Polyspace Code Prover** | R2025b | 商业 | 形式化验证（CI 容器） |
| **Coverity（Black Duck）** | 2026.x | 商业 | 多样化 cross-check |

> **GPLv2 注意**：lcov / gcovr 仅在 CI / 开发期使用，不链接到分发件，不影响 CCS 入级 license 合规。

### 2.5 禁用清单（License / 安全 / 不一致）

| 库 | 禁用原因 |
|---|---|
| **Boost.Asio**（如版本 < 1.66） | 已 deprecate；使用 ROS2 rclcpp 内置异步 |
| **OpenSSL** 直接调用 | 安全敏感；通过 ROS2 `rmw_dds_security` + Cyclone DDS Security 间接使用 |
| **rapidjson** / **jsoncpp** | 与 nlohmann::json 重复；选 nlohmann::json 唯一 |
| **glog** | 与 spdlog 重复；选 spdlog 唯一 |
| **MQTT 客户端** | 未在 §15 接口契约中；如需用须 ADR |
| **gRPC** | ROS2 已用 DDS；引入 gRPC 等于引入第二个 IPC 框架 |
| **任何 GPLv2 / GPLv3 强 copyleft 库**（链接到生产代码）| CCS 入级 license 合规要求 |
| **任何未在白名单中的 Crypto / Network 库** | 安全审查未通过前禁用 |

---

## 3. Doer-Checker 独立性矩阵（决策四 + ADR-001 强制）

### 3.1 矩阵总表

| 库 | M1 | M2 | M3 | M4 | M5 | M6 | M7 | M8 | 备注 |
|---|---|---|---|---|---|---|---|---|---|
| **rclcpp / rclpy** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 框架基础（不算共享业务库）|
| **l3_msgs（共享消息）** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **✓ 仅订阅** | ✓ | M7 通过消息读 M1-M6 状态 |
| **Eigen** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 数学基础（不带业务逻辑） |
| **spdlog** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **独立 logger 实例** | ✓ | M7 用 `m7_supervisor` 命名 logger，独立日志文件 |
| **fmt** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 格式化基础 |
| **tl::expected** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | header-only；M1/M7 PATH-S 禁异常路径强烈推荐；其他路径可选 |
| **GTest / GMock** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 仅测试期 |
| **Cyclone DDS** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | RMW 层（不算业务库） |
| **CasADi** | — | — | — | — | ✓ | — | **✗** | — | M7 禁用：求解器复杂，违反 Checker 简化原则 |
| **IPOPT** | — | — | — | — | ✓ | — | **✗** | — | 同上 |
| **GeographicLib** | — | ✓ | ✓ | — | ✓ | — | **✗** | — | M7 禁用：M7 不做大地坐标计算（仅做阈值比较） |
| **nlohmann::json** | — | ✓ | ✓ | — | — | — | **✗** | ✓ | M7 禁用：M7 不解析 JSON（保持 enum 化简单语义） |
| **Boost.Geometry** | — | ✓ | — | — | ✓ | ✓ | **✗** | — | M7 禁用：M7 不做几何运算 |
| **Boost.PropertyTree / yaml-cpp** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | **仅 spdlog 配置** | ✓ | M7 仅用 yaml-cpp 加载日志配置（不进决策路径） |
| **FastAPI / uvicorn / websockets** | — | — | — | — | — | — | — | ✓ | M8 专属 |
| **pydantic** | — | — | — | — | — | — | — | ✓ | M8 专属 |

**符号**：
- ✓ = 允许使用
- ✗ = 强制禁用（CI 阻断）
- — = 不需要（业务上不相关）

### 3.2 M7 独立性证明（CCS 审计提交件）

按 ADR-001 + 决策四 + v1.1.2 §11.3 + §11.6，M7 须证明以下独立性：

```
独立性维度        | M7 实施方法
─────────────────|──────────────────────────────────────────────────────────────
代码              | M7 不复用 M1–M6 任何源文件 / header
                 | 仅 #include "l3_msgs/msg/*.hpp" 共享消息
                 | 自动验证：tools/ci/check-doer-checker-independence.sh
─────────────────|──────────────────────────────────────────────────────────────
第三方库          | M7 白名单：rclcpp, l3_msgs, Eigen, spdlog, fmt, GTest, yaml-cpp
                 | 禁用：CasADi, IPOPT, GeographicLib, nlohmann::json, Boost.Geometry
                 | 自动验证：CI #include grep + library link grep
─────────────────|──────────────────────────────────────────────────────────────
数据结构          | M7 内部状态机 / 阈值表 / MRM 命令集独立定义
                 | 不引用 m1_envelope::*, m2_world_model::* 等命名空间
                 | 仅引用 l3_msgs::msg::* 顶层消息类型
─────────────────|──────────────────────────────────────────────────────────────
构建              | M7 独立 colcon package（src/m7_safety_supervisor/）
                 | 独立 CMakeLists.txt，独立 package.xml
                 | CI 中 M7 静态分析门禁与 M1–M6 分别运行
─────────────────|──────────────────────────────────────────────────────────────
运行时            | M7 独立进程（独立 ROS2 node + 独立 launch 文件）
                 | 与 M1–M6 通过 DDS 隔离（无共享内存 / 共享文件描述符）
                 | M7 logger 独立 spdlog instance（独立日志文件 m7.log）
                 | M7 独立 OS 进程（不与其他 L3 node composable）
─────────────────|──────────────────────────────────────────────────────────────
认证证据          | CI 自动产出 M7 独立性矩阵报告（每 PR 附带）
                 | 提交 CCS 验船师审查（M7 独立性是 CCS 入级前置条件）
```

### 3.3 自动化验证脚本

`tools/ci/check-doer-checker-independence.sh`（详细见 `static-analysis-policy.md` §8）：

```bash
# 检查 M7 包含的 header
forbidden_internal=("mass_l3/m1/" "mass_l3/m2/" "mass_l3/m3/" "mass_l3/m4/" "mass_l3/m5/" "mass_l3/m6/")
forbidden_3rdparty=("casadi/" "Ipopt" "GeographicLib/" "nlohmann/json" "boost/geometry")

for h in "${forbidden_internal[@]}" "${forbidden_3rdparty[@]}"; do
    if grep -rn "#include.*${h}" src/m7_safety_supervisor/; then
        echo "VIOLATION: M7 includes forbidden header '${h}'"
        exit 1
    fi
done

# 检查 CMakeLists.txt 链接
forbidden_links=("casadi" "Ipopt" "GeographicLib" "nlohmann_json" "boost_geometry")
for lib in "${forbidden_links[@]}"; do
    if grep -E "target_link_libraries.*${lib}" src/m7_safety_supervisor/CMakeLists.txt; then
        echo "VIOLATION: M7 links forbidden library '${lib}'"
        exit 1
    fi
done

echo "Doer-Checker independence: OK"
```

**违反 = 构建失败，不可豁免，触发 ADR-001 复审**。

---

## 4. License 合规

### 4.1 允许的 License（CCS 入级 + 商业分发兼容）

```
Apache-2.0
MIT
BSD-2-Clause
BSD-3-Clause
ISC
BSL-1.0 (Boost)
MPL-2.0 (Eigen)
EPL-2.0 (Cyclone DDS / IPOPT — 弱 copyleft，仅修改库本身需开源)
EDL-1.0
LGPL-2.1 / LGPL-3.0（仅动态链接，不静态链接 — CasADi）
GPL-3.0-with-classpath-exception（特殊 — 仅工具链，不进生产）
```

### 4.2 禁止的 License（强 copyleft）

```
GPLv2（无 classpath exception）
GPLv3（无 classpath exception）
AGPL（任何版本）
SSPL
Commons Clause（视情况）
```

### 4.3 LGPL 注意事项（CasADi）

CasADi 使用 LGPL-3.0；项目通过 **动态链接** 使用，不静态链接。CMake 配置：

```cmake
find_package(casadi 3.7.2 EXACT REQUIRED)
target_link_libraries(m5_tactical_planner PUBLIC casadi)  # 动态链接
# 不允许静态链接：禁止 -static
```

LGPL 合规：
- 仓库不修改 CasADi 源码（如修改须分发修改后源码）
- 分发件附带 CasADi `LICENSE` 文件
- SBOM 显式列出 CasADi 3.7.2 LGPL-3.0

### 4.4 SBOM 自动生成（CycloneDX）

详见 `ci-cd-pipeline.md` §9.2。每次 release 触发：

```bash
syft packages dir:install -o cyclonedx-json > sbom-${VERSION}.json
bash tools/ci/sbom-license-check.sh sbom-${VERSION}.json
```

CCS 入级提交件之一。

---

## 5. Vendor 策略（依赖版本固化）

### 5.1 git submodule（首选，纯源代码）

```bash
# 添加 submodule
cd tools/third-party
git submodule add -b 5.0.0 https://gitlab.com/libeigen/eigen.git eigen-5.0.0
git submodule add -b v3.7.2 https://github.com/casadi/casadi.git casadi-3.7.2
git submodule add -b v3.14.19 https://github.com/coin-or/Ipopt.git ipopt-3.14.19
git submodule add -b v2.7 https://github.com/geographiclib/geographiclib.git geographiclib-2.7
git submodule add -b v3.12.0 https://github.com/nlohmann/json.git nlohmann-json-3.12.0
git submodule add -b v1.17.0 https://github.com/google/googletest.git googletest-1.17.0
git submodule add -b v1.17.0 https://github.com/gabime/spdlog.git spdlog-1.17.0

# 锁定到具体 commit
git submodule update --init --recursive
git submodule status   # 记录到 .gitmodules
```

`.gitmodules`：

```ini
[submodule "tools/third-party/eigen-5.0.0"]
    path = tools/third-party/eigen-5.0.0
    url = https://gitlab.com/libeigen/eigen.git
    branch = 5.0.0

[submodule "tools/third-party/casadi-3.7.2"]
    path = tools/third-party/casadi-3.7.2
    url = https://github.com/casadi/casadi.git
    branch = v3.7.2
# ... 类推
```

### 5.2 CMake `find_package` + EXACT VERSION

```cmake
# 不依赖系统包，从 vendored 路径找
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/tools/third-party/eigen-5.0.0/install)
find_package(Eigen3 5.0.0 EXACT REQUIRED)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/tools/third-party/casadi-3.7.2/install)
find_package(casadi 3.7.2 EXACT REQUIRED)

# ...
```

### 5.3 替代：vcpkg（Windows / 跨平台开发期）

`vcpkg.json`：

```json
{
  "name": "mass-l3-tdl",
  "version": "1.0.0",
  "dependencies": [
    {"name": "eigen3", "version>=": "5.0.0"},
    {"name": "casadi", "version>=": "3.7.2"},
    {"name": "ipopt", "version>=": "3.14.19"},
    {"name": "geographiclib", "version>=": "2.7"},
    {"name": "nlohmann-json", "version>=": "3.12.0"},
    {"name": "boost-geometry", "version>=": "1.91.0"},
    {"name": "yaml-cpp", "version>=": "0.8.0"},
    {"name": "spdlog", "version>=": "1.17.0"},
    {"name": "gtest", "version>=": "1.17.0"}
  ],
  "builtin-baseline": "<commit-hash>"
}
```

> **CI 基线用 git submodule（Linux Ubuntu 22.04）**；vcpkg 仅作开发者本地辅助。

---

## 6. 升级流程

### 6.1 升级触发条件

| 条件 | 优先级 |
|---|---|
| 安全 CVE 公告（CVSS ≥ 7.0） | **极高 — 立即升级** |
| LTS 版本 EOL | 高（提前 6 个月规划升级）|
| 上游主版本 release（功能驱动） | 中（评估后决定） |
| 跨依赖的版本约束矛盾（如 Boost 与 CasADi） | 高 |
| 跨平台 / 工具链兼容性问题 | 中 |

### 6.2 升级 PR 流程

1. **影响评估**：列出受影响的模块 / API / .msg
2. **PoC 分支**：`feature/upgrade-<lib>-<version>`
3. **CI 全跑**：lint / unit / static / integration 全绿
4. **Performance 回归测试**：对比关键场景的延迟 / 吞吐量
5. **CCS 通知**（如 M1/M7 路径）：通知 CCS 验船师工具链版本变更（认证证据链一致性）
6. **合并 + tag**

### 6.3 锁定版本变更日志

`tools/third-party/CHANGELOG.md`：

```markdown
## 2026-05-06 (Project Init)

- ROS 2 Jazzy Jalisco
- Eigen 5.0.0
- CasADi 3.7.2 / IPOPT 3.14.19
- GeographicLib 2.7
- nlohmann::json 3.12.0
- Boost 1.91.0
- yaml-cpp 0.8.0
- spdlog 1.17.0
- GTest 1.17.0

## <Future>
- ...
```

---

## 7. 安全更新政策

### 7.1 CVE 监控

CI 中集成 `dependency-check` 或 `trivy`：

```yaml
security-scan:
  stage: lint
  script:
    - trivy fs --security-checks vuln,config,secret \
               --severity HIGH,CRITICAL \
               --exit-code 1 \
               .
    - trivy sbom sbom.json --severity HIGH,CRITICAL
  allow_failure: false
```

### 7.2 升级 SLA

| 严重度 | 响应时间 |
|---|---|
| Critical（CVSS ≥ 9.0） | 24 h 内提交升级 PR |
| High（CVSS 7.0–8.9） | 7 天 |
| Medium（CVSS 4.0–6.9） | 30 天 |
| Low（CVSS < 4.0） | 下次例行升级 |

### 7.3 例外：M7 安全关键路径

M7 路径上引用的库（极少：仅 spdlog / Eigen / yaml-cpp / rclcpp / l3_msgs）的升级须额外步骤：
- 通知 CCS 验船师 + DNV 验证官（v1.1.2 + 后续版本认证证据链）
- 重新跑 Polyspace + Coverity 全量分析
- 工具链版本变更归档到 `docs/Implementation/exemptions/upgrade-log/<YYYY-MM-DD>.md`

---

## 8. 添加新依赖的流程

### 8.1 ADR 模板（强制）

```markdown
# ADR-IMPL-NNN: 添加新第三方依赖 <库名> v<版本>

## 上下文
（为什么需要这个库？现有白名单为什么不够？）

## 决策
（选择 <库名> v<版本>，license 类型，进入哪个路径白名单）

## 影响
- 受益模块：M3, M5
- License 合规：MIT，符合本政策 §4.1
- M7 独立性：N/A（M7 禁用）/ N+1 个允许库（如适用）
- SBOM 影响：新增条目
- CI 时间：+ X min（编译 / 静态分析）

## 替代方案
（评估的其他库 + 弃用理由）

## Reviewer
- [ ] 项目架构师 双签
- [ ] 安全工程师（如 PATH-S 路径）
- [ ] CCS 验船师（如 PATH-S 路径）
```

### 8.2 拒绝原则

如下情形 **默认拒绝**：
- 未在 GitHub / GitLab 公开（无法审计）
- License 不在 §4.1 白名单
- 单 PR 加 ≥ 3 个新依赖（拆分）
- 在 M7 路径添加新依赖（除非通过 ADR-001 复审 + 触发 Doer-Checker 独立性重新评估）
- 与现有库功能重复（详见 §2.5 禁用清单）

---

## 9. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：白名单 + Doer-Checker 独立性矩阵 + License 合规 + Vendor 策略 |

---

## 10. 引用

- **架构 v1.1.2 §2.5 决策四 Doer-Checker** — 独立性约束源头
- **架构 v1.1.2 §11.3 + §11.6** — M7 独立性具体要求
- **ADR-001 — 决策四修订** — `docs/Design/Architecture Design/audit/2026-04-30/08c-adr-deltas.md`
- **CycloneDX SBOM** — [cyclonedx.org](https://cyclonedx.org/) 🟢 A
- **NIST NVD CVE Database** — [nvd.nist.gov](https://nvd.nist.gov/) 🟢 A
- **Eigen 5.0.0 release** — [gitlab.com/libeigen/eigen](https://gitlab.com/libeigen/eigen/-/releases) 🟢 A
- **CasADi 3.7.2** — [web.casadi.org/get](https://web.casadi.org/get/) 🟢 A
- **IPOPT 3.14.19** — [github.com/coin-or/Ipopt](https://github.com/coin-or/Ipopt/releases) 🟢 A
- **GeographicLib 2.7** — [sourceforge/geographiclib](https://sourceforge.net/p/geographiclib/news/2025/11/geographiclib-27-released-2025-11-06/) 🟢 A
- **nlohmann::json 3.12.0** — [github.com/nlohmann/json](https://github.com/nlohmann/json) 🟢 A
- **Boost 1.91.0** — [boost.org/releases/latest](https://www.boost.org/releases/latest/) 🟢 A
- **GTest 1.17.0** — [github.com/google/googletest](https://github.com/google/googletest/releases) 🟢 A
- **spdlog 1.17.0** — [github.com/gabime/spdlog](https://github.com/gabime/spdlog/releases) 🟢 A
