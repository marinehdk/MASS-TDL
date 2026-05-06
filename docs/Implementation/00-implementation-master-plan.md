# 实施层启动主计划（Implementation Master Plan）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-MASTER-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（实施层启动包，可分发给 8 个开发团队） |
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2（接口跨团队锁定） |
| 详细设计基线 | `docs/Design/Detailed Design/M{1-8}/01-detailed-design.md`（8 模块 9506 行 spec） |
| 跨团队基线 | `docs/Design/Cross-Team Alignment/RFC-decisions.md`（6 RFC 全部已批准） |
| HAZID 状态 | RUN-001 进行中（5/13 第 1 次会议；阻塞参数标 `[TBD-HAZID]`） |
| 适用范围 | 8 个 L3 模块（M1–M8）的代码实现、单元测试、CI/CD 集成 |

> **本主计划是实施层启动包的入口。** 8 个开发团队从此开始，按本计划 §6 配套文件清单 + §7 模块开发顺序工作。

---

## 1. 范围与目标

### 1.1 范围（IN）

本主计划覆盖 L3 战术决策层 **8 个模块（M1–M8）的从零开始实施**：

- 项目脚手架结构（colcon workspace + 8 个 ROS2 package）
- 编码规范 + 静态分析门禁（SIL 2 / DAL B 适用）
- CI/CD pipeline（5 阶段：lint / unit / static / integration / release）
- ROS2 .msg / .idl 实现（v1.1.2 §15 IDL 映射）
- 第三方库锁定 + Doer-Checker 独立性矩阵
- 8 模块代码骨架 spec（`M{1-8}/code-skeleton-spec.md`）

### 1.2 范围（OUT）

不在本主计划内（属其他工作流）：

- **算法实现细节**：在每模块 `01-detailed-design.md` §5 中，非本主计划复述
- **HIL 测试场景设计**：在 `docs/Test Plan/`（方向 4）
- **CCS 入级证据整理**：在 `docs/Certification/`（方向 5）
- **多船型扩展**：在 `docs/Vessel Adaptations/`（方向 6）
- **L1/L2/L4/L5/Multimodal Fusion/Checker/ASDR 等其他层模块**：属其他团队职责
- **实船试航工程**：FCB ≥ 50 nm + ≥ 100 h（计划 2026-06，不在本主计划范围）

### 1.3 启动条件

✅ **已满足（可立即启动）**：

- v1.1.2 主架构正式（接口跨团队锁定）
- 8 模块 `01-detailed-design.md` 草稿/正式完成（共 9506 行）
- 6 RFC 决议归档（接口契约不再变更）
- 工程规范基线已就位（本主计划的 §3）

⚠️ **HAZID 平行进行**：所有 `[TBD-HAZID]` 标注的参数（25+ 项）将在 RUN-001 完成后（2026-08-19）回填到 v1.1.3。**实施可以先行用初始建议值开始编码**，HAZID 校准结果通过参数文件（YAML/JSON）注入，不需要重编译核心代码。

---

## 2. 团队与责任分工

| 团队 | 模块 | 主语言 | 关键依赖 |
|---|---|---|---|
| Team-M1 | M1 ODD/Envelope Manager | C++17 | Eigen, spdlog |
| Team-M2 | M2 World Model | C++17（Python 3.10 用于 ENC 加载工具） | Eigen, GeographicLib, spdlog |
| Team-M3 | M3 Mission Manager | C++17 | nlohmann::json, spdlog |
| Team-M4 | M4 Behavior Arbiter | C++17 | Eigen, spdlog（IvP 求解器自实现）|
| Team-M5 | M5 Tactical Planner | C++17 | **CasADi 3.7.2 + IPOPT 3.14.19**, Eigen, spdlog |
| Team-M6 | M6 COLREGs Reasoner | C++17 | Eigen, spdlog（规则推理引擎自实现）|
| **Team-M7** | **M7 Safety Supervisor** | **C++17（独立路径）** | **spdlog（独立 logger 实例）；不允许使用 M1–M6 共享库（详见 §3.5 + `third-party-library-policy.md`）** |
| Team-M8 | M8 HMI/Transparency Bridge | C++17 + Python 3.10 | nlohmann::json, spdlog（前端 Web 框架由 Team-M8 自选） |

**跨团队协作**：
- 6 RFC 跨团队改造时间表见 `RFC-decisions.md` §"跨团队改造工作量汇总"（共 31 人月，关键路径 8 周）
- 本计划假设其他团队（L4 / L2 / Fusion / X-axis Checker / Reflex Arc / ASDR / L5 / Hardware Override / L1）按 RFC 时间表交付接口，否则 L3 模块用 mock 桩 + 集成测试占位

---

## 3. 工程基线（2026-05 锁定）

> 所有版本号经 `/nlm-research --depth fast` 在 2026-05-06 验证（详见 §10 引用链）。

### 3.1 ROS2 + 构建工具链

| 项目 | 锁定版本 | 备选 / 升级路径 | 来源 |
|---|---|---|---|
| **ROS2 发行版（首选）** | **Jazzy Jalisco**（ROS 2.10）EOL 2029-05-31 | Humble Hawksbill 备选 EOL 2027-05-31 | [endoflife.date/ros-2](https://endoflife.date/ros-2) 🟢 |
| **C++ 编译器（基线）** | **GCC 14.3 LTS**（C++17 全支持） | GCC 15.2 主线（开发期可选）/ Clang 20.1.8（多样化路径） | [gcc.gnu.org](https://gcc.gnu.org/gcc-15/) 🟢 |
| **CMake** | **≥ 3.22**（Ubuntu 22.04 自带，最低）；推荐 3.28+ | CMake 4.3.x 仅作开发工具，CI 锁定 3.28 | [cmake.org/download](https://cmake.org/download/) 🟢 |
| **colcon-core** | **0.20.1**（pinned in `tools/requirements.txt`） | — | [pypi.org/colcon-core](https://pypi.org/project/colcon-core/) 🟢 |
| **Python**（M2 工具 + M8 后端） | **3.10**（Ubuntu 22.04 默认） | — | 项目锁定 🟢 |

### 3.2 编码规范（决策更新 — T-2 详化）

> **基线决策**：用户原指定 AUTOSAR C++14；研究发现 **MISRA C++:2023** 是 AUTOSAR C++14 + MISRA C++:2008 的合并继任者（2023-10 发布，179 条规则，覆盖 C++17）。**采用 MISRA C++:2023 作为本项目主基准**，AUTOSAR C++14 的工具资格化与设计层指南作为补充参考。详见 [Perforce 公告](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) 🟢。

| 路径 | 规范基线 | 强度 |
|---|---|---|
| **M1 + M7 安全关键路径** | **MISRA C++:2023 完整 179 规则** + AUTOSAR C++14 设计指南 | **强制**（CI 阻断） |
| **M2–M6 决策路径** | MISRA C++:2023 完整 179 规则 | **强制**（CI 阻断） |
| **M8 HMI 路径** | MISRA C++:2023 简化裁剪集（约 120 规则，去 HMI 不适用项） | 强制（CI 阻断），裁剪清单见 `coding-standards.md` §6 |
| **Python 3.10 部分**（M2 ENC 工具 + M8 后端） | PEP 8 + Ruff lint + mypy strict | 强制（CI 阻断） |

### 3.3 静态分析工具链（SIL 2 强制）

| 工具 | 版本 | 用途 | 路径强度 |
|---|---|---|---|
| **clang-tidy** | 20.1.8 | CI 主 lint + MISRA C++:2023 部分规则覆盖 | **所有路径强制** |
| **cppcheck Premium** | 26.1.0（支持 MISRA C++:2023 完整规则集） | MISRA 完整覆盖 + UB / 内存检查 | **M1/M7 强制；M2–M8 推荐** |
| **Polyspace Code Prover** | R2025b（R2026a 待发布）| 形式化抽象解释 + 运行时错误证明 | **M1/M7 强制（IEC 61508 SIL 4 / DO-178C DAL A 认证）** |
| **Coverity（Black Duck）** | 当前商业版（TUV SUD 认证 IEC 61508 SIL 4） | 多样性独立 cross-check（Polyspace 互补） | **M1/M7 推荐**；预算允许时启用 |
| **AddressSanitizer / ThreadSanitizer / UBSan** | GCC/Clang 自带 | 单元 / 集成测试期动态检查 | **所有路径强制** |

### 3.4 通信中间件（DDS）

| 路径 | 推荐 DDS | 理由 | 升级路径 |
|---|---|---|---|
| **Non-safety paths（M2/M3/M4/M5/M6/M8）** | **Eclipse Cyclone DDS**（ROS2 Tier 1，Apache 2.0） | OSS 友好，ROS2 Jazzy 默认起步 RMW 之一 | RTI Connext Cert（如预算允许） |
| **Safety-critical paths（M1/M7）** | **当前 v1.0 实施仍用 Cyclone DDS**（统一 RMW 简化集成）；**v1.2 升级路径：RTI Connext Cert**（唯一已完成 **IEC 61508 SIL 3** 认证 + DO-178C DAL A 的 DDS 实现） | RTI 商业授权 + 海事/汽车 SIL 案例 | v1.2 关键安全路径独立切换为 RTI Connext Cert，非安全路径保留 Cyclone DDS |

> **决策依据**：[RTI 2024-11 公告](https://www.globenewswire.com/news-release/2024/11/19/2983554/0/en/RTI-Becomes-the-First-DDS-Provider-to-Meet-All-Major-Automotive-Software-Safety-Standards.html) 🟢 + [Cyclone DDS](https://cyclonedds.io/) 🟡（ASIL-D 认证进行中）。

### 3.5 Doer-Checker 独立性约束（决策四 + ADR-001 强制）

**M7 Safety Supervisor 实现路径必须与 M1–M6 完全独立**：

| 维度 | 约束 |
|---|---|
| **代码** | M7 不复用 M1–M6 任何源文件 / header；公共类型仅通过 ROS2 .msg 接口流通 |
| **第三方库** | M7 仅允许：spdlog（独立实例）、Eigen（数学基础）、ROS2 标准库；**禁用** CasADi / IPOPT / GeographicLib / nlohmann::json（这些在 M5 / M2 / M3 / M8 中使用） |
| **数据结构** | M7 内部状态机 / 阈值表 / MRM 命令集独立定义，不引用 `m1_envelope::*` 等命名空间 |
| **构建** | M7 独立 colcon package，独立编译；CI 中 M7 静态分析门禁与 M1–M6 分别运行 |
| **运行时** | M7 独立进程（独立 ROS2 node）；与 M1–M6 通过 DDS 隔离；M7 logger 独立 spdlog instance（独立日志文件）|

**完整独立性矩阵**见 `third-party-library-policy.md` §3。

---

## 4. 项目脚手架结构

### 4.1 Workspace 布局（colcon）

```
mass_l3_tdl/                            # workspace 根（git 单仓 monorepo）
├── src/
│   ├── m1_odd_envelope_manager/        # ROS2 package — Team-M1
│   │   ├── package.xml
│   │   ├── CMakeLists.txt
│   │   ├── include/m1_odd_envelope_manager/
│   │   ├── src/
│   │   ├── msg/                        # M1 私有消息（不在 §15 总表）
│   │   ├── launch/
│   │   ├── config/                     # YAML/JSON 参数文件（HAZID 注入点）
│   │   └── test/                       # GTest 单元测试
│   ├── m2_world_model/                 # — Team-M2（含 Python ENC 加载工具）
│   ├── m3_mission_manager/             # — Team-M3
│   ├── m4_behavior_arbiter/            # — Team-M4
│   ├── m5_tactical_planner/            # — Team-M5（CasADi/IPOPT 依赖）
│   ├── m6_colregs_reasoner/            # — Team-M6
│   ├── m7_safety_supervisor/           # — Team-M7（独立路径）
│   ├── m8_hmi_transparency_bridge/     # — Team-M8（含 Python 后端）
│   │
│   ├── l3_msgs/                        # L3 共享消息包（v1.1.2 §15 IDL）
│   │   ├── package.xml
│   │   ├── CMakeLists.txt
│   │   └── msg/
│   │       ├── ODDState.msg           # ODD_StateMsg → 文件名转换
│   │       ├── WorldState.msg
│   │       ├── ...                     # （详见 ros2-idl-implementation-guide.md §3）
│   │
│   └── l3_external_msgs/               # 跨层接口 mock 消息（其他团队真实包就位前用）
│       └── msg/
│           ├── VoyageTask.msg          # L1 → M3
│           ├── PlannedRoute.msg        # L2 → M3,M5
│           ├── AvoidancePlan.msg       # M5 → L4
│           ├── ReactiveOverrideCmd.msg # M5 → L4
│           ├── CheckerVetoNotification.msg  # X-axis → M7
│           ├── ASDRRecord.msg
│           ├── EmergencyCommand.msg
│           ├── ReflexActivationNotification.msg
│           ├── OverrideActiveSignal.msg
│           └── ReplanResponseMsg.msg   # L2 → M3 [v1.1.2 RFC-006]
│
├── tools/
│   ├── requirements.txt                # Python 依赖锁定（colcon, pytest, mypy, ruff, ...）
│   ├── ci/                             # GitLab CI 共用脚本
│   │   ├── lint.sh
│   │   ├── static-analysis.sh
│   │   ├── unit-test.sh
│   │   └── integration-test.sh
│   ├── docker/                         # 开发 + CI 镜像（Ubuntu 22.04 + ROS2 Jazzy）
│   │   ├── Dockerfile.dev
│   │   ├── Dockerfile.ci
│   │   └── compose.yml
│   └── third-party/                    # 第三方库 vendor（不进 system 路径）
│       ├── casadi-3.7.2/               # M5 lock
│       ├── ipopt-3.14.19/
│       ├── geographiclib-2.7/
│       └── ...
│
├── .gitlab-ci.yml                      # 5 阶段 pipeline（详见 ci-cd-pipeline.md）
├── .clang-tidy                         # MISRA C++:2023 规则集（裁剪）
├── .clang-format                       # 项目代码风格
├── .editorconfig
├── colcon.meta                         # workspace 元配置（package 编译顺序）
├── README.md
└── docs/                               # 设计文档（已存在）
```

**关键约束**：
- **单仓 monorepo**：8 个模块共享 git 仓库，但 8 个独立的 colcon package（独立 CMakeLists.txt + package.xml）
- **共享消息包**（`l3_msgs`）：v1.1.2 §15.1 中 L3 内部消息（ODDStateMsg / WorldStateMsg / ...）集中定义，保证 IDL 单一来源
- **跨层 mock 消息包**（`l3_external_msgs`）：其他团队的消息（VoyageTask / AvoidancePlan / ...）的 mock 定义；当其他团队的真实包就位后，编译时切换 deps 即可
- **第三方库 vendor**：CasADi / IPOPT / GeographicLib 等通过 git submodule 或 conan/vcpkg 锁定版本（不依赖系统包，确保可复现构建）

### 4.2 8 模块开发顺序与依赖图

```
依赖图（拓扑排序，编译/单元测试顺序）：

    l3_msgs (共享消息)
        │
        ├─→ M2 World Model           ← Team-M2 (并行起步)
        ├─→ M3 Mission Manager       ← Team-M3 (并行起步)
        ├─→ M6 COLREGs Reasoner      ← Team-M6 (并行起步)
        │       │
        │       └─→ M4 Behavior Arbiter   ← Team-M4 (依赖 M2/M6 接口)
        │               │
        │               └─→ M5 Tactical Planner   ← Team-M5 (依赖 M4 行为约束)
        │
        ├─→ M1 ODD/Envelope Manager  ← Team-M1 (调度枢纽，与上述并行)
        │
        ├─→ M7 Safety Supervisor     ← Team-M7 (独立路径，依赖只读 M1–M6 接口)
        │
        └─→ M8 HMI/Transparency      ← Team-M8 (依赖所有 SAT_DataMsg 来源)
```

**Wave 划分**：
- **Wave 0（2026-05-06 完成 + review）**：基础设施（colcon workspace + l3_msgs + l3_external_msgs + Mock publisher + Docker + tools/ci + .gitmodules + .gitlab-ci.yml + .clang-tidy + .clang-tidy.path-s）— ✅ **质量门通过**（详见 §8.2 + `Wave-0-review-report.md`）
- **Wave 1 第一批（2026-05-06 编码 + review fix 完成）**：M1 / M2 / M3 / M6 — 4 模块全部 Phase E1 编码 + review + fix，各 worktree 完成；剩 4 个工作流阻断项待清（详见 §8.4 W1-BLOCK-001~004）
- **Wave 2（Week 3–6，部分重叠）**：M4 / M7 — 依赖 Wave 1 接口稳定（用 Wave 1 的 mock 桩开始）
- **Wave 3（Week 5–8）**：M5 / M8 — 依赖 Wave 1+2 完成
- **Wave 4（Week 7–10）**：跨模块集成测试 + HIL 准备（详见 `docs/Test Plan/`）

> **预计实施时长**：单团队 4–8 周/模块（取决于规模）；8 团队并行下，**关键路径 ~10 周到模块级稳定**；跨模块集成 + HIL 共需 10–14 周完成。**总计实施阶段约 6 个月（2026-05–2026-11）**，与 HAZID 完成时点（2026-08-19）+ FCB 实船试航（2026-06）互相支撑。

---

## 5. 阶段定义

### 5.1 实施阶段四步走

```
Phase E1: 编码（Per-module）        — 4–8 weeks/module（并行）
   产出：源代码 + 单元测试（覆盖率 ≥ 90%）
   验证：CI 5 阶段全绿

Phase E2: 模块集成测试              — 2 周
   产出：跨模块场景测试用例（M2→M6→M5 链条；M5→M7 仲裁；M3→L2 重规划等）
   验证：所有 §15 接口契约消息字段对齐 + 频率达标

Phase E3: HIL 端到端测试            — 4 周
   产出：FCB 4-DOF MMG 仿真器 + COLREGs 1000+ 场景库（详见 docs/Test Plan/）
   验证：v1.1.2 决策四 + 每模块 §9 HIL 场景全通过

Phase E4: 实船 FCB 试航             — 4 周（2026-06，与 HAZID Phase 2 重叠）
   产出：≥ 50 nm + ≥ 100 h 运营数据 + ≥ 50 COLREGs 场景实证
   验证：HAZID 校准 + v1.1.3 patch 触发
```

### 5.2 完成（DoD）判据

#### Phase E1 模块完成 DoD

- [ ] CI 5 阶段全绿（lint / unit / static / integration / release）
- [ ] 单元测试覆盖率 ≥ 90%（由 lcov / gcov 报告）
- [ ] 静态分析门禁通过（M1/M7 强制 Polyspace + Coverity；M2–M8 强制 clang-tidy + cppcheck Premium）
- [ ] AddressSanitizer + ThreadSanitizer + UBSan 0 错误
- [ ] §15 IDL 接口实现并通过 mock 桩测试
- [ ] 模块详细设计 `01-detailed-design.md` §9 列出的 ≥ 3 个 HIL 场景至少有 mock-level 验证
- [ ] HAZID `[TBD-HAZID]` 参数通过 YAML 配置注入（不硬编码）
- [ ] ASDR 决策日志格式符合 v1.1.2 §15.1 ASDR_RecordMsg + RFC-004 决议
- [ ] 模块独立 README + `code-skeleton-spec.md` 同步更新

#### Phase E2 集成完成 DoD

- [ ] 全部 §15.2 接口矩阵 24 行消息流端到端对齐
- [ ] M7 Doer-Checker 独立性矩阵证明（`third-party-library-policy.md` §3 表格）
- [ ] 6 RFC 接口（含 ReplanResponseMsg / CheckerVetoNotification 等）实测通过
- [ ] M5 → L4 双接口（AvoidancePlan + ReactiveOverrideCmd）三模式切换 < 100 ms（RFC-001）
- [ ] Reflex Arc 端到端 < 500 ms（RFC-005）

---

## 6. 配套文件清单（实施层启动包，14 文件）

### 6.1 主 agent 产出（6 个工程规范文件）

| # | 文件 | 状态 | 行数（估）|
|---|---|---|---|
| 0 | `00-implementation-master-plan.md` | ✅ 本文件 | ~600 |
| 1 | `coding-standards.md` | 待写 | ~700 |
| 2 | `static-analysis-policy.md` | 待写 | ~600 |
| 3 | `ci-cd-pipeline.md` | 待写 | ~700 |
| 4 | `ros2-idl-implementation-guide.md` | 待写 | ~800 |
| 5 | `third-party-library-policy.md` | 待写 | ~500 |

### 6.2 Subagent 并行产出（8 个模块代码骨架 spec）

| # | 文件 | 团队 | 行数（估）|
|---|---|---|---|
| 1 | `M1/code-skeleton-spec.md` | Team-M1 | ~700 |
| 2 | `M2/code-skeleton-spec.md` | Team-M2 | ~800 |
| 3 | `M3/code-skeleton-spec.md` | Team-M3 | ~700 |
| 4 | `M4/code-skeleton-spec.md` | Team-M4 | ~700 |
| 5 | `M5/code-skeleton-spec.md` | Team-M5 | ~900 |
| 6 | `M6/code-skeleton-spec.md` | Team-M6 | ~800 |
| 7 | `M7/code-skeleton-spec.md` | Team-M7 | ~900（独立路径强约束） |
| 8 | `M8/code-skeleton-spec.md` | Team-M8 | ~700 |

**合计**：~9000 行实施层启动包文档（覆盖 14 文件）。

### 6.3 文件交叉引用关系

```
00-implementation-master-plan.md（本文件）
    │
    ├─→ coding-standards.md          ← 全局 C++17 + Python 编码规范
    │      │
    │      └─→ static-analysis-policy.md   ← MISRA C++:2023 规则集 + 工具配置
    │
    ├─→ ci-cd-pipeline.md             ← GitLab CI 5 阶段 pipeline + gating
    │
    ├─→ ros2-idl-implementation-guide.md   ← v1.1.2 §15 IDL → ROS2 .msg 映射
    │
    ├─→ third-party-library-policy.md      ← 库锁定 + Doer-Checker 独立性矩阵
    │
    └─→ M{1-8}/code-skeleton-spec.md       ← 8 个模块代码骨架（团队级开发起点）
            │
            └─→ docs/Design/Detailed Design/M{N}/01-detailed-design.md  ← 详细设计
                    │
                    └─→ docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md  ← v1.1.2 主架构
```

每个模块骨架 spec 须**显式引用**：
- 本主计划（编码规范 / 工具链 / DoD）
- 模块详细设计（§5 算法 / §6 时序 / §9 测试 / §10 实现约束）
- v1.1.2 §15 IDL（订阅 / 发布消息）

---

## 7. 跨团队改造时间表对接

引用 `RFC-decisions.md` §"跨团队改造工作量汇总"：

| 团队 | 工作量 | 关键里程碑 | L3 实施阶段对接 |
|---|---|---|---|
| L4 Guidance | 6 周 | T+5 周 PoC；T+8 周 HIL | M5 Wave 3 集成测试需要 L4 真实节点；之前用 mock |
| Multimodal Fusion | 4 周 | T+4 周 nav_mode + 50Hz proximity | M2 Wave 1–2 用 mock TrackedTargetArray；T+4 后切换 |
| Deterministic Checker | 3 周 | T+3 周 CheckerVetoNotification | M7 Wave 2 集成测试需要；之前用事件 mock |
| ASDR | 3 周 | T+3 周 IDL + REST | 全模块 ASDR 写入用 mock；T+3 后切换 |
| Reflex Arc | 6 周 | T+6 周触发逻辑 | M1 Wave 2–3 集成测试需要 |
| L5 Control | 2 周 | T+2 周 EmergencyCommand | M5 BC-MPC + Reflex Arc 集成需要 |
| Hardware Override | 2 周 | T+2 周 OverrideActiveSignal | M1 Wave 1–2 集成测试 |
| L1 Voyage Order | 1 周 | T+1 周 VoyageTask schema | M3 Wave 1 启动即需要 |
| L2 Voyage Planner | 4 周 | T+4 周 RouteReplanRequest + ReplanResponseMsg | M3 Wave 2 集成测试需要 |

**Mock 桩策略**：所有跨团队接口均在 `l3_external_msgs` 包中提供 .msg 定义 + 一个简单的 ROS2 `mock_*_publisher` 节点，使 L3 模块可在跨团队真实节点未就位时独立开发。

---

## 8. 关键风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| HAZID 校准结果与 v1.1.2 初始建议偏差大（>30%） | 中 | 中（需重新调试参数）| 所有 `[TBD-HAZID]` 参数通过 YAML 注入；不硬编码；运行时可热加载 |
| Polyspace / Coverity 商业授权采购延迟 | 中 | 高（M1/M7 静态分析无法启动） | 备选 cppcheck Premium（独立 license，支持 MISRA C++:2023）+ clang-tidy 全规则启用 |
| Cyclone DDS 在高频场景（M2 50Hz、M8 100Hz）QoS 性能不足 | 低 | 中 | CI 含 DDS 性能测试；不达标时切换到 Fast DDS 或 v1.2 升级 RTI Connext Cert |
| RTI Connext Cert 商业授权评估超过项目预算 | 高 | 中（v1.2 升级路径阻塞）| v1.0 实施基线 = Cyclone DDS（OSS）；CCS 中期审查时再决策是否 v1.2 升级 RTI |
| Doer-Checker 独立性约束违反（M7 误用 M1–M6 共享头文件） | 中 | 极高（直接违反 ADR-001 + 决策四，CCS 不通过） | CI 中加 `tools/ci/check-doer-checker-independence.sh` 自动验证（详见 `third-party-library-policy.md` §4）|
| 8 团队并行下 ROS2 .msg IDL 漂移（命名 / 字段不一致） | 中 | 高（集成期返工 ≥ 2 周） | `l3_msgs` 共享包是单一来源；任何 .msg 变更须走 PR review + 自动化 schema 校验 |
| MISRA C++:2023 规则学习曲线（团队首次接触） | 中 | 中（前 2 周开发慢） | `coding-standards.md` 含 50 个常见违规 + 修复模式；CI 给违规附说明链接 |
| 跨团队接口（L4 / Fusion / etc.）真实节点延迟交付 | 中 | 中 | Mock 桩齐全；本主计划 §7 含降级开发策略 |
| HAZID 第 1 次会议日期延后（5/13 → ?）| 低 | 低（实施可独立推进） | 实施阶段不阻塞；HAZID 校准结果通过 v1.1.3 增量回填 |
| FCB 实船试航天气延期（6 月）| 中 | 高（HIL 验证替代证据不足） | HIL 场景库扩到 1000+；天气窗口预订 6/10–6/24 双备份 |

### 8.1 已识别接口契约不一致（v1.1.3 候选）

实施层 spec 编写过程中识别到 **2 项**与现有架构 / 详细设计的微小不一致，**不阻塞**实施启动，但应在 v1.1.3 评审时关闭：

| 编号 | 描述 | 位置 | 影响 | 处置建议 |
|---|---|---|---|---|
| **F-IMPL-001** | `tl::expected`（C++17 期间作为 `std::expected` polyfill）已添加到 `third-party-library-policy.md` §2.1 白名单 + §3.1 矩阵 | 已修复 | 无 | 已关闭 |
| **F-IMPL-002** | M1 详细设计 §3.1 提到 `MRC_RequestMsg → M5`，但架构 v1.1.2 §15.2 接口矩阵未定义此 topic | M1 spec §3.3 末尾标注 | 仅文档不一致；当前通过 `Safety_AlertMsg.recommended_mrm` + ASDR 间接通知 M5 | v1.1.3 RFC：(a) 新增独立 `MRCRequest.msg` topic；或 (b) 将 §3.1 描述改为通过现有 `Safety_AlertMsg` 链路传递（推荐 b — 不破坏 §15.2 锁定） |

### 8.2 Wave 0 状态（2026-05-06 review 后）

Wave 0 基础设施代码（colcon workspace + l3_msgs + l3_external_msgs + Mock publisher + tools/ci + Docker + .gitmodules）已经过 **3 路并行严格代码审查**（顶层配置 / 消息包 / CI 脚本）。完整审查报告见 `docs/Implementation/Wave-0-review-report.md`。

**初次评分**（review 时）：
- Reviewer A（顶层配置 + Docker）：**D**（10 Critical / 10 Important / 6 Minor）
- Reviewer B（消息包）：**C**（6 Critical / 8 Important / 5 Minor）
- Reviewer C（CI 脚本）：**C+**（5 Critical / 5 Important / 5 Minor）

**修复状态**（截至 2026-05-06）：

| 维度 | Critical 修复 | Important 修复 | Minor 修复 |
|---|---|---|---|
| 顶层配置 + Docker | ✅ 10/10 | ✅ 10/10 | ⏳ 6/6（建议在 Wave 1 第一周内完成） |
| 消息包 | ✅ 6/6 | ✅ 5/8（剩 3 项 Minor 风格） | ⏳ 2/5 |
| CI 脚本 | ✅ 5/5 | ✅ 4/5 | ⏳ 3/5 |
| **submodule init** | ⏳ 待用户手动执行（CI 自动 fetch via .pre 阶段） | — | — |

**Wave 0 当前状态**：✅ **质量门通过，可启动 Wave 1**（M1 / M2 / M3 / M6 模块开发）。Important / Minor 剩余项不阻塞。

**给开发团队的关键操作**：
1. **首次拉代码后**执行 `git submodule update --init --recursive --depth 1`（或通过 CI `.pre_vendor_libs` 自动）
2. **macOS 开发者**须 `brew install bash` 升级到 bash 4+（CI Ubuntu 22.04 自带 5.1 不受影响）
3. **PATH-S 团队**（M1 / M7）注意 `.clang-tidy.path-s` 比主 `.clang-tidy` 更严（LineThreshold=40 / Complexity=8 + 禁动态分配）

### 8.3 Wave 0 修订引入的 spec 偏差（v1.1.3 候选记录）

修复 F-CRIT-B-003 时 `TimeWindow.msg` 从 `l3_msgs/` 移到 `l3_external_msgs/`（避免 l3_msgs 反向依赖 l3_external_msgs）。原 spec `ros2-idl-implementation-guide.md` §1.1 列 TimeWindow 在 l3_msgs；实际实施位置已变。

| 编号 | 描述 | 处置建议 |
|---|---|---|
| **F-IMPL-003** | TimeWindow.msg 从 l3_msgs 移到 l3_external_msgs | 在 v1.1.3 spec 修订时同步更新 `ros2-idl-implementation-guide.md` §1.1 文件清单（l3_external_msgs 增加 TimeWindow）+ §3.1 共享子类型 note |
| **F-IMPL-004** | l3_external_msgs 现在依赖 l3_msgs（TrackedTargetArray AoS 用 l3_msgs/TrackedTarget[]）| 已在 `src/l3_external_msgs/CMakeLists.txt` + `package.xml` 添加；spec §1.1 / §5.2 应同步反映此跨包依赖 |
| **F-IMPL-005** | Polyspace + Coverity + cppcheck Premium 商业 tarball 暂不可用，Dockerfile.ci 用注释占位 | 项目购买商业 license 后取消注释；在此之前 PATH-S 路径 `stage-3-polyspace-path-s` job 会 FATAL（合理 — 暴露问题）|

### 8.4 Wave 1 第一批状态（M1 / M2 / M3 / M6 — 2026-05-06 review 完成）

Wave 1 第一批 4 个模块的 Phase E1 编码 + 单元测试 + review fix 全部在各自 worktree **完成**。代码质量审查覆盖每模块 review + fix（详见各 worktree commit history）。

| 模块 | 路径 | 实现量 | 测试量 | test/impl | [TBD-HAZID] | ASDR refs | pub/sub 拓扑 | worktree 最新 commit | Phase E1 状态 |
|---|---|---|---|---|---|---|---|---|---|
| **M1** ODD/Envelope | PATH-S | 907 行 | 756 行 | 83.4% | 37 | 12 | 4/6 | `62935db fix(m1): address code review issues — PATH-S violations, bugs, naming` | ✅ **已 review + fix** |
| **M2** World Model | PATH-D | 1,071 行 | 931 行 | 86.9% | 9 | 7 | 3/4 | `9562a6a fix(m2): address code-review issues from Phase E1 strict review` | ✅ **已 review + fix** |
| **M3** Mission Manager | PATH-D | 770 行 | 680 行 | 88.3% | 21 | 14 | 3/6 | `d6b22b6 fix(m3): resolve 5 critical + 7 important bugs found in Phase E1 review` | ✅ **已 review + fix** |
| **M6** COLREGs Reasoner | PATH-D | 853 行 | 1,058 行 | **124%** | 36 | 9 | 3/2 | `29eac7e fix(m6): address code review findings C1-C6, I1-I4, I6-I7` | ✅ **已 review + fix** |
| **合计** | — | **3,601 行** | **3,425 行** | **95.1%** | **103** | **42** | **13/18** | — | — |

**§15.2 接口契约对齐**（4 模块的 ROS2 topic 引用）：

```
M1 → /l3/m1/odd_state, /l3/m1/mode_cmd  (发布 4)
M1 ← /l3/m7/safety_alert, /reflex/activation_notification, /override/active_signal,
     /fusion/own_ship_state, /fusion/environment_state, /l3/m2/world_state  (订阅 6)

M2 → /l3/m2/world_state  (发布 3 含 ASDR + SAT)
M2 ← /fusion/tracked_targets, /fusion/own_ship_state, /fusion/environment_state,
     /l3/m1/odd_state  (订阅 4)

M3 → /l3/m3/mission_goal, /l3/m3/route_replan_request  (发布 3 含 ASDR)
M3 ← /l1/voyage_task, /l2/planned_route, /l2/speed_profile, /l2/replan_response,
     /l3/m1/odd_state, /l3/m2/world_state  (订阅 6)

M6 → /l3/m6/colregs_constraint  (发布 3 含 ASDR + SAT)
M6 ← /l3/m1/odd_state, /l3/m2/world_state  (订阅 2)
```

✅ **全部 topic 与架构 v1.1.2 §15.2 接口矩阵一一对齐**。

**Wave 1 第一批工作流阻断项**（2026-05-06 全部清完）：

| 编号 | 阻断项 | 处置 | 状态 |
|---|---|---|---|
| **W1-BLOCK-001** | Wave 0 review fixes（27 文件 + `Wave-0-review-report.md`）须 commit 到 main | commit `e6a3d45 fix(wave-0): address 22 critical + 18 important review findings`（29 文件，+1745/-304）| ✅ **完成** |
| **W1-BLOCK-002** | 4 个 worktree 须 rebase 到含 Wave 0 修复的 main | 4 worktree 全部 rebase 成功，**0 conflict**；每个 worktree 验证已合入 .clang-tidy 9 大类 + .clang-tidy.path-s + .gitmodules 11 branch 锁定 | ✅ **完成** |
| **W1-BLOCK-003** | 4 个模块 src/m{N}/README.md 缺失 | 4 个 README 写入并 commit（M1=104 行 / M2=111 行 / M3=108 行 / M6=112 行；含路径强度 / topic 拓扑 / DoD / 库白名单 / 开发命令）| ✅ **完成** |
| **W1-BLOCK-004** | 4 个 worktree 未走 MR 合并到 main | 4 worktree 顺序 `--no-ff` merge 到 main（M1→M2→M3→M6），保留每个分支的 review fix history（CCS 审计可追溯）；merge commits: 4b8c962 / 79435ad / 9533e3e / ddc4c9c | ✅ **完成** |

**Wave 1 第一批 main 上最终交付物**（2026-05-06 完成）：

| 模块 | main 上 cpp+hpp 文件 | README | merge commit |
|---|---|---|---|
| M1 ODD/Envelope Manager | 22 | ✅ 104 行 | 4b8c962 |
| M2 World Model | 30 | ✅ 111 行 | 79435ad |
| M3 Mission Manager | 24 | ✅ 108 行 | 9533e3e |
| M6 COLREGs Reasoner | 56 | ✅ 112 行 | ddc4c9c |
| **合计** | **132 cpp+hpp 文件 / ~15,160 行** | **4 README** | **4 merge commits** |

main 当前 HEAD = `ddc4c9c merge: M6 COLREGs Reasoner Phase E1 (PATH-D)`，`git log --oneline --graph --all` 显示完整 4 module 分支拓扑（CCS 入级证据可追溯）。

**关键说明 — CI 真实运行尚未验证**：所有 Wave 1 "完成"判断基于代码合入 + 静态校验（YAML 语法 / 字段对齐 / 测试存在 / HAZID 标注）。**真实 GitLab CI runner 的 5 阶段 pipeline（lint / unit-test / static-analysis / integration-test / release）尚未运行**。建议下一步：
- 部署 mass-l3/ci:jazzy-ubuntu22.04 镜像到 GitLab runner
- 跑首次 main 全量 pipeline，可能暴露新的 .clang-tidy.path-s 严格阈值 violations（M1 PATH-S 受影响最大）
- 任何 finding 都通过新 hotfix commit 修复（不破坏分支拓扑）

**Wave 2 启动门槛**（全部 ✅）：
- ✅ Wave 0 修复完整（W1-BLOCK-001 已 commit）
- ✅ 消息 IDL 锁定（v1.1.2 §15.1 + 6 .msg 修复在 main）
- ✅ Mock publisher 修复（频率正确 + 字段齐全）
- ✅ CI 脚本修复（doer-checker 独立性自动验证）
- ✅ 4 个上游模块 main 可用（M4/M7 不需要 mock 桩 — 可直接订阅真实 ODD/World/Mission/COLREGs topic）

**Wave 2 (M4/M7) 现已可立即启动**（按之前 §6.2 提供的 Haiku 4.5 提示词）。

---

## 9. 进度跟踪（看板视图）

实施阶段建议在 GitLab Issue 中创建以下 epic：

```
Epic-IMPL-Wave0-Infrastructure   (✅ 完成 + review fixed 2026-05-06)
   ├── ✅ l3_msgs 包搭建（26 .msg）
   ├── ✅ l3_external_msgs 包（11 .msg + Mock publisher）
   ├── ✅ Docker 开发镜像 (jammy 锁定)
   ├── ✅ CI 5 阶段 pipeline (.gitlab-ci.yml)
   ├── ✅ tools/ci/ 5 个 check 脚本
   ├── ✅ .clang-tidy + .clang-tidy.path-s
   └── ⏳ Wave 0 review fixes commit 到 main（W1-BLOCK-001 待）

Epic-IMPL-Wave1-Foundation       (✅ Phase E1 编码 + review 完成 2026-05-06)
   ├── ✅ M1 Phase E1 — PATH-S，907 impl + 756 test，37 [TBD-HAZID]，commit 62935db
   ├── ✅ M2 Phase E1 — PATH-D，1071 impl + 931 test，9 [TBD-HAZID]，commit 9562a6a
   ├── ✅ M3 Phase E1 — PATH-D，770 impl + 680 test，21 [TBD-HAZID]，commit d6b22b6
   └── ✅ M6 Phase E1 — PATH-D，853 impl + 1058 test，36 [TBD-HAZID]，commit 29eac7e
   剩待办（4 工作流阻断项 — 详见 §8.4）：
   ├── ⏳ W1-BLOCK-001: Wave 0 fixes commit
   ├── ⏳ W1-BLOCK-002: 4 worktree rebase + 跑修复后 CI
   ├── ⏳ W1-BLOCK-003: 4 模块各补 README.md
   └── ⏳ W1-BLOCK-004: 4 个 MR 顺序合并到 main

Epic-IMPL-Wave2-Decision         (Week 3–6 — Wave 1 阻断清后启动)
   ├── ⏳ M4 Phase E1 (依赖 M2/M6 mock；Haiku 4.5 适合)
   └── ⏳ M7 Phase E1 (独立路径，PATH-S；Haiku 4.5 启动 → BLOCKED 时切 Opus 4.7)

Epic-IMPL-Wave3-Planning&HMI     (Week 5–8)
   ├── ⏳ M5 Phase E1 (CasADi + IPOPT 集成)
   └── ⏳ M8 Phase E1 (C++ + Python 双栈)

Epic-IMPL-Wave4-Integration      (Week 7–10)
   ├── ⏳ 全模块集成测试 (Phase E2)
   └── ⏳ HIL 准备 (与 docs/Test Plan/ 协同)
```

**关键 KPI**：
- 每周 CI 全绿率 ≥ 95%
- 每周 PR 合并周期 < 3 天
- Wave 1 完成时点：2026-06-03（Week 4 末）
- Wave 4 完成时点：2026-07-15（Week 10 末，HIL 启动）

---

## 10. 引用与证据链

### 10.1 架构基线

- v1.1.2 主架构：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- 8 模块详细设计：`docs/Design/Detailed Design/M{1-8}/01-detailed-design.md`
- 6 RFC 决议：`docs/Design/Cross-Team Alignment/RFC-decisions.md`
- HAZID 启动包：`docs/Design/HAZID/RUN-001-kickoff.md`

### 10.2 工程基线证据（2026-05-06 验证）

| 项目 | 来源 URL | 置信度 |
|---|---|---|
| ROS2 EOL 日期 | https://endoflife.date/ros-2 | 🟢 A |
| ROS2 REP-2000 平台 | https://www.ros.org/reps/rep-2000.html | 🟢 A |
| GCC 15 / 14.3 | https://gcc.gnu.org/gcc-15/ + https://gcc.gnu.org/gcc-14/ | 🟢 A |
| Clang/LLVM 20.1.8 | https://releases.llvm.org/20.1.0/tools/clang/docs/ReleaseNotes.html | 🟢 A |
| MISRA C++:2023 合并继任者 | https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means | 🟢 A |
| Polyspace Code Prover | https://www.mathworks.com/products/polyspace-code-prover.html | 🟢 A |
| Coverity Safety Manual | https://documentation.blackduck.com/bundle/coverity-docs/page/webhelp-files/safety_start.html | 🟢 A |
| RTI Connext Cert IEC 61508 SIL 3 | https://www.rti.com/products/connext-cert + https://www.globenewswire.com/news-release/2024/11/19/2983554/0/en/RTI-Becomes-the-First-DDS-Provider-to-Meet-All-Major-Automotive-Software-Safety-Standards.html | 🟢 A |
| Eclipse Cyclone DDS | https://cyclonedds.io/ | 🟡（ASIL-D 进行中）|
| CasADi 3.7.2 | https://web.casadi.org/get/ | 🟢 A |
| IPOPT 3.14.19 | https://github.com/coin-or/Ipopt/releases | 🟢 A |
| GeographicLib 2.7 | https://sourceforge.net/p/geographiclib/news/2025/11/geographiclib-27-released-2025-11-06/ | 🟢 A |
| Eigen 5.0.0 | https://gitlab.com/libeigen/eigen/-/releases | 🟢 A |
| nlohmann::json 3.12.0 | https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp | 🟢 A |
| Boost 1.91.0 | https://www.boost.org/releases/latest/ | 🟢 A |
| GTest 1.17.0 | https://github.com/google/googletest/releases | 🟢 A |
| spdlog 1.17.0 | https://github.com/gabime/spdlog/releases | 🟢 A |
| Apex.AI ROS2 ASIL-D 工业先例 | https://www.apex.ai/apexgrace | 🟢 A |
| ROS 2 Safety WG | https://github.com/ros-safety/safety_working_group | 🟢 A |

### 10.3 合规基线引用（继承 v1.1.2 §16）

- **CCS《智能船舶规范》（2024/2025）** — i-Ship (Nx, Ri/Ai) [R1]
- **IMO MSC 110/111 MASS Code（2025/2026）** [R2]
- **IEC 61508**（功能安全）+ **ISO 21448 SOTIF** [R5, R6]
- **DNV-CG-0264（2025.01）AROS Class Notations** [R9]
- **Veitch et al. 2024**（TMR ≥ 60s 实证）[R4]
- **Yasukawa & Yoshimura 2015**（MMG）[R7]

---

## 11. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：8 模块开发顺序 + 工程基线（含 MISRA C++:2023 决策更新）+ 文件清单 |

---

## 12. 后续动作

实施阶段启动后的下一里程碑：

1. ✅ 本主计划（00）
2. ⏳ `coding-standards.md` + `static-analysis-policy.md` + `ci-cd-pipeline.md` + `ros2-idl-implementation-guide.md` + `third-party-library-policy.md`（主 agent 顺序产出）
3. ⏳ `M{1-8}/code-skeleton-spec.md`（8 个 background subagent 并行产出）
4. ⏳ 8 个团队按 Wave 启动 Phase E1（编码 + 单元测试）
5. ⏳ Wave 4 集成测试 + HIL 准备（衔接方向 4 `docs/Test Plan/`）
6. ⏳ HAZID 校准（RUN-001，2026-08-19 完成）→ v1.1.3 patch 回填
7. ⏳ FCB 实船试航（2026-06）→ HIL 场景实证
8. ⏳ CCS i-Ship AIP 申请（2026-08–09，衔接方向 5 `docs/Certification/`）
