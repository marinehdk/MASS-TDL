# SIL v1.0 统一设计文档套件 · README

| 属性 | 值 |
|---|---|
| 文档套件编号 | SANGO-ADAS-L3-SIL-UNIFIED-000 |
| 套件版本 | v1.0（统一基线） |
| 编制日期 | 2026-05-15 |
| 状态 | 设计基线（架构 + 后端 + 前端 + 场景联调四件套）|
| 上游架构 | MASS-L3 TDL 架构报告 **v1.1.3-pre-stub**（2026-05-09，选项 D 混合架构 + SIL 框架架构 patch 锁定）|
| ROS2 版本 | **Humble Hawksbill + Ubuntu 22.04 + PREEMPT_RT**（决策记录 2026-05-09 §3 锁定）|
| 范围 | Phase 1 (5/13–6/15 DEMO-1 Skeleton Live) + Phase 2 (DEMO-2 7/31) + Phase 3 (DEMO-3 8/31) |
| 取代 | 散落在 6 处的 ~33 份设计文档（已 1:1 归档到 `_source-archive/`，不删除原始位置）|

---

## 0. 一句话定位

L3 TDL 战术决策层在硬件接入之前，**端到端跑通"YAML 场景 → 仿真启动 → 真实算法链路 → ENC 上看到避碰过程 → 证据导出"完整工作流**的 Software-in-the-Loop 设计基线。所有"Mock / 假接口 / 占位实现"在 v1.0 范围内均按"现状 → 目标 → 差距"三栏对照记录，不在本文档套件中实施替换 — 替换交由后续 D-task 实施计划。

---

## 1. 套件文档清单

| 编号 | 路径 | 题目 | 主要读者 | 状态 |
|---|---|---|---|---|
| Doc 0 | `00-README.md`（本文档）| 索引 + 阅读路径 + 共用约定 | 全员 | ✅ 基线 |
| Doc 1 | `01-sil-architecture.md` | SIL 系统架构（顶层 + 拓扑 + 数据流 + 生命周期）| 架构师 / V&V 工程师 / 跨团队 | ✅ 基线 |
| Doc 2 | `02-sil-backend-design.md` | 后端设计（FastAPI orchestrator + 10 ROS2 节点 + FMI 桥 + Docker）| 后端工程师 / DevOps | ⏳ Doc 1 通过后启动 |
| Doc 3 | `03-sil-frontend-design.md` | 前端设计（4 屏幕 + 状态管理 + 数据通道 + 设计语言）| 前端工程师 / HF 咨询 | ⏳ Doc 2 通过后启动 |
| Doc 4 | `04-sil-scenario-integration-test.md` | 场景联调测试（YAML schema + 调用链 + DEMO 验收 + Evidence pack）| V&V 工程师 / 安全工程师 / surveyor | ⏳ Doc 3 通过后启动 |

---

## 2. 推荐阅读路径

### 2.1 第一次接触 SIL 系统（开发者新人）

1. **本文档** §3 推荐阅读路径 + §4 共用约定
2. **Doc 1** §0 一句话定位 + §1 在 MASS v3.0 中的位置 + §3 顶层架构图（5 分钟掌握全貌）
3. **Doc 4** §2 场景 YAML schema + §4 端到端调用链（理解"运行一次"是什么）
4. 然后按角色分流：
   - 后端 → Doc 2 全篇
   - 前端 → Doc 3 全篇
   - V&V/Cert → Doc 4 全篇

### 2.2 准备实施某个 D-task

1. Doc 1 §11 D-task 映射表，找到你的 D 任务对应的"产物 + 验收"
2. 对应文档（Doc 2/3/4）相关章节
3. `_source-archive/` 下原始 spec（可选，作历史与决策依据）

### 2.3 准备 DEMO-1/2/3 联调

1. Doc 4 §6 DEMO 验收矩阵
2. Doc 4 §7 KPI 与评分
3. Doc 1 §10 风险与降级（看哪些 KPI 失败时的退路）

### 2.4 准备 CCS surveyor 交付

1. Doc 4 §9 ASDR 证据链 + §10 Evidence pack
2. Doc 1 §12 文件谱系 + §13 调研记录（追溯）
3. Doc 4 §11 仿真器鉴定（D1.3.1 OSP/DNV-RP-0513 映射）

---

## 3. 共用约定（4 份文档共享，仅此处声明）

### 3.1 章节模板：三栏对照

每个实质性章节按以下结构组织（章节内可省略空栏）：

> **现状**（指向 `src/` 或 `web/` 文件 + commit SHA + 行号）
>
> **目标**（v1.0 统一 Spec — 本文档套件的"是什么"）
>
> **差距**（gap 编号 + 修复路径 + 责任 D-task）

### 3.2 置信度标注（强制）

每个关键断言/选型/参数末尾必带：

- 🟢 **High** — ≥ 3 个 A/B 级独立来源一致 + < 6 个月
- 🟡 **Medium** — 2 个来源一致 / 主流共识有少数反对
- 🔴 **Low** — 来源 < 2 / 过时 > 6 月 / 仅 C 级 / 来源分裂
- ⚫ **Unknown** — 搜不到，明说

来源分层：
- **A** — 官方/规范/IMO/CCS/DNV/IHO/ISO/IEC/ROS2 REP/一作论文
- **B** — 高星 GitHub issue/官方仓库 README/权威博客/IEEE/ASME 会议
- **C** — StackOverflow / 普通博客 / 二手转引

置信度取**最低级**来源的判定。

### 3.3 引用编号

- `[Ex]` — 决策记录 `00-architecture-revision-decisions-2026-05-09.md` 引用源（E1–E33）
- `[R-NLM:N]` — NLM Deep Research 2026-05-12 silhil_platform 报告 bibliography（R1–R52）
- `[Wx]` — 本套件新增 web/调研发现的源（W1, W2, ...，编号由 Doc 1 §13 起列出，跨文档共用）
- `[Rx]` — 架构报告 v1.1.3-pre-stub 参考文献（保留旧编号兼容）

裸贴 URL 不允许 — 必须分配编号并在 §13 调研记录中列条目。

### 3.4 路径约定

- **当前活跃实现**：`/Users/marine/Code/MASS-L3-Tactical Layer/{src/,web/,docker/,scenarios/,pyproject.toml,docker-compose.yml}`
- **架构源 spec**：`docs/Design/SIL/v1.0-unified/_source-archive/`（本套件创建时归档）
- **架构主报告**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.3-pre-stub
- **8 月计划主文件**：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` v3.0（v3.1 修订在制）

### 3.5 commit 锚点（v1.0 基线）

文档套件参照以下 git 提交快照（2026-05-08 至 2026-05-14）：

- `73cdf23` feat: add External Mock Publisher + DEMO-1 scenarios (R13/R14/R15)
- `ace10b8` feat: add M4 Behavior Arbiter ROS2 node + M5 topic remap + integration tests
- `e1a13e5` docs: add D1.5 V&V Plan v0.1
- `acad427` feat(demo): SIL Demo-1 Head-On 完整实现 + 4 项限制修复
- `74af635` feat(demo): Head-On analytical trajectory generator + tests
- `2879f2c` feat: implement high-fidelity ENC chart visualization with multi-layer MVT pipeline and S-52 styling
- `f9997c9` docs: add D1.4 coding standards document

设计与代码的偏差在各文档"差距"栏中标注 commit SHA + 文件路径 + 行号。

### 3.6 不可让步项（4 文档共同上游约束）

直接继承架构报告 v1.1.3-pre-stub 第 2 章四项顶层决策（不重复论述，仅在相关章节引用）：

1. **ODD 是组织原则**，M1 ODD 状态变化是行为切换唯一权威
2. **Doer-Checker 双轨** — M1–M6 是 Doer，M7 是 Checker，路径独立
3. **CMM 通过 SAT-1/2/3 接口对外可见**，不在系统内实现状态机
4. **多船型 = Capability Manifest + PVA 适配 + 水动力插件**，决策核心代码零船型常量

SIL 系统在以上约束下不引入新的"架构层"破坏 — 仅承担"测试 harness"职责。

### 3.7 ROS2 与 OS 版本

**全套件统一**：ROS2 **Humble Hawksbill**（Ubuntu 22.04 + PREEMPT_RT，决策记录 2026-05-09 §3 🟢 High）。

任何文档中残留 `ros:jazzy` / `mass-l3/ci:jazzy-*` 引用均为 **GAP-001** 待修复（见 Doc 1 §10 风险表 + Doc 2 §11 容器构建链）。

### 3.8 选项 D 混合架构（决策 2026-05-09 §1 锁定）

- production C++/MISRA ROS2 Humble 节点直接运行于 SIL 内核（"测试目标即部署目标"，保 CCS i-Ship N 认证可追溯）
- FMI 2.0 / OSP `libcosim` 仅在 ship dynamics + 未来 RL FMU 边界使用
- DNV `maritime-schema` 作 scenario / output 互认 schema
- **不** 重构为 Python orchestration 包装器
- **不** 裸 ROS2（评据格式必互认）
- **M7 Safety Supervisor 严格留 ROS2 native，不过 FMI 边界**（理由：端到端 KPI < 10ms，dds-fmu 单次 exchange 即吃完 KPI）

### 3.9 DNV 工具链锁定深度（决策 2026-05-09 §2）

- **MUST**：`dnv-opensource/maritime-schema` v0.2.x · `open-simulation-platform/libcosim` · `dnv-opensource/farn` · `dnv-opensource/ospx`
- **NICE-deferred**：`dnv-opensource/ship-traffic-generator`（Phase 2 D2.4）· `dnv-opensource/mlfmu`（Phase 4 D4.6）

### 3.10 RL 三层隔离边界（决策 2026-05-09 §4）

| 层 | 边界 | 强制 |
|---|---|---|
| **L1 Repo** | `/src/l3_tdl_kernel/**` vs `/src/sim_workbench/**` vs `/src/rl_workbench/**`（Phase 4 启动）| 三 colcon 包独立；CI lint 检测 cross-import |
| **L2 Process** | RL 训练独立 Docker container；仅通过 OSP libcosim FMI socket 调相同 MMG FMU + scenario YAML | docker-compose 隔离 namespace；只读挂载 certified binaries |
| **L3 Artefact** | 训练完毕 ONNX → mlfmu → `target_policy.fmu` → libcosim 加载到 certified SIL；Python/PyTorch 永不入 certified loop | `mlfmu build` 是边界；FMU 进 evidence store 须经 DNV-RP-0671 鉴定 |

SIL v1.0 仓库结构 + CI lint rule 须在 Phase 1 落地，即使 B2 RL 推到 Phase 4。

---

## 4. 4 屏幕统一约定

Doc 3 详述，本节仅声明命名 + 路由 + 生命周期对应（4 文档统一引用）：

| # | 中文名 | 英文 / 路由 | ROS2 Lifecycle 状态 | 主要 active 节点子集 |
|---|---|---|---|---|
| ① | 仿真场景 | **Simulation-Scenario** · `/scenario` | UNCONFIGURED | scenario_authoring only |
| ② | 仿真检查 | **Simulation-Check** · `/check/:runId` | INACTIVE（configured）| + self_check + perception_mock |
| ③ | 仿真运行 | **Simulation-Monitor** · `/monitor/:runId` | ACTIVE（50 Hz tick）| 全栈 + L3 kernel + rosbag2 |
| ④ | 仿真报告 | **Simulation-Evaluator** · `/evaluator/:runId` | INACTIVE（after deactivate）| scoring + scenario_authoring |

**命名一致性**：本套件 Doc 1–4 统一使用 `Simulation-Scenario / Simulation-Check / Simulation-Monitor / Simulation-Evaluator`。归档源文档中的旧英文名（Scenario Builder / Pre-flight / Bridge HMI / Run Report）保留作历史指针。代码侧（`web/src/screens/*.tsx`、`src/sil_orchestrator/*_routes.py`、路由 path）的文件名 / 路径 / 组件 ID 重命名由 D1.3b.3+ 后续 sprint 统一执行（未在本套件 v1.0 实施），目前作 **GAP-014** 记入差距台账。

8 个交互动作（Load / Start / Pause-Resume / Speed × / Reset / Inject Fault / Stop / Export）→ ROS2 Lifecycle service + custom service 映射在 Doc 1 §6 + Doc 2 §10。

---

## 5. 归档源文件清单

`_source-archive/` 子目录，归档于 2026-05-15。原始路径**未删除**。

| 归档子目录 | 原始位置 | 文件数 |
|---|---|---|
| `SIL-root/` | `docs/Design/SIL/*.md`（顶层 6 份）| 6 |
| `SIL-Design-v2.0/` | `docs/Design/SIL/Design v2.0/*.md` | 9 |
| `D1.3a-simulator/` | `docs/Design/Detailed Design/D1.3a-simulator/` | 2 |
| `D1.3b-scenario-hmi/` | `docs/Design/Detailed Design/D1.3b-scenario-hmi/` | 7 |
| `D1.3c-fmi-bridge/` | `docs/Design/Detailed Design/D1.3c-fmi-bridge/` | 3 |
| `HMI-Design/` | `docs/Design/Detailed Design/HMI-Design/` | 5（含 4 HTML 原型）|
| **合计** | | **33** |

归档版本对照（仅最关键的"权威源"，全表在 Doc 1 §12）：

| 文档 | 日期 | 角色 |
|---|---|---|
| `SIL-root/00-architecture-revision-decisions-2026-05-09.md` | 2026-05-09 | **决策记录**（顶层不可让步项）|
| `SIL-root/2026-05-12-sil-architecture-design.md` | 2026-05-12 | **架构 v1.0 greenfield**（具体化决策 → 实施）|
| `SIL-root/2026-05-13-sil-hmi-dual-mode-design.md` + `2026-05-13-sil-hmi-dual-mode.md` | 2026-05-13 | **HMI 双模式设计**（Bridge HMI 详细化）|
| `D1.3b-scenario-hmi/03-web-hmi-spec.md` | 2026-05-13 | Builder + Preflight + Report 屏具体化 |
| `D1.3c-fmi-bridge/01-spec.md` | 2026-05-11 | FMI 桥（D1.3c 范围）|
| `HMI-Design/MASS_TDL_HMI_Design_Spec_v1.0.md` | 2026-05-10 | HMI 设计语言 + 双模式视觉原型 |
| `SIL-root/2026-05-11-colav-simulator-capability-inventory.md` | 2026-05-11 | 业务能力清单（NTNU colav-simulator 映射）|
| `SIL-root/2026-05-11-colav-simulator-cpp-implementation.md` | 2026-05-11 | C++ 参考实现（**软参考**，决策记录已明示降级）|

---

## 6. 与现有实现的关系

### 6.1 实现现状（commit `73cdf23` @ 2026-05-14）

```
src/
├── l3_tdl_kernel/                # L3 TDL 8 模块（C++/Python ROS2 节点）
│   ├── m1_odd_envelope_manager/
│   ├── m2_world_model/
│   ├── m3_mission_manager/
│   ├── m4_behavior_arbiter/       # ace10b8 新增 ROS2 节点
│   ├── m5_tactical_planner/
│   ├── m6_colregs_reasoner/
│   ├── m7_safety_supervisor/
│   ├── m8_hmi_transparency_bridge/
│   ├── l3_external_mock_publisher/  # ← MOCK，DEMO-1 后退役
│   ├── l3_external_msgs/            # IDL（RFC 锁定）
│   ├── l3_msgs/                     # IDL（v1.1.2 锁定）
│   ├── sil_proto/                   # Protobuf IDL（greenfield）
│   └── common/
├── sil_orchestrator/             # FastAPI REST + rclpy 桥
│   ├── main.py
│   ├── config.py
│   ├── scenario_routes.py / scenario_store.py
│   ├── lifecycle_bridge.py
│   ├── telemetry_bridge.py
│   ├── selfcheck_routes.py
│   └── export_routes.py
└── sim_workbench/                # 仿真节点（colcon 包族）
    ├── sil_lifecycle/                  # LifecycleNode mgr
    ├── sil_nodes/                      # 业务节点集合（ship_dynamics/env_disturbance/...）
    ├── scenario_authoring/             # YAML CRUD + AIS 管线
    ├── sil_msgs/                       # SIL 专用 ROS2 IDL
    ├── ship_sim_interfaces/            # 现存 IDL（保留）
    ├── fcb_simulator/                  # → 升级为 ship_dynamics 节点
    ├── fmi_bridge/                     # D1.3c FMI 桥（stub）
    ├── ais_bridge/                     # → 已迁入 scenario_authoring AIS 管线
    ├── l3_external_mock_publisher/     # ← MOCK
    └── sil_mock_publisher/             # ← MOCK
```

### 6.2 实现 vs 设计对齐度

| 范畴 | 对齐度 | 备注 |
|---|---|---|
| 后端节点拓扑 | 🟢 高 | sil_nodes 8 业务节点 + sil_lifecycle 已就位；与设计 9 节点 + 1 mgr 一致（差 1 个 fault_injection 单独节点，目前合并在 sil_nodes 内）|
| Orchestrator REST | 🟢 高 | 6 router 全部就位 |
| ROS2 版本 | 🟡 中 | sil_nodes Dockerfile 用 humble ✅；**orchestrator Dockerfile 用 jazzy ❌ → GAP-001** |
| Mock vs 真链路 | 🔴 低 | 两个 *_mock_publisher 仍活跃；DEMO-1 范围内允许，DEMO-2 前必须替换 → GAP-002 |
| 场景 YAML schema | 🔴 低 | `scenarios/head_on.yaml` 仍用 NTNU colav-simulator schema（csog_state / waypoints / los / kf / flsc）；设计目标是 DNV `maritime-schema` TrafficSituation → GAP-003 |
| Protobuf IDL | 🟡 中 | `sil_proto/` 包已建；尚未通过 buf CI gate → GAP-004 |
| Web HMI | 🟢 高 | 4 屏全部已开发（Builder/Preflight/Bridge/Report），OpenBridge token + S-52 styling 已落地（commit 4fc0522 + 2879f2c）|
| Foxglove bridge | 🟢 高 | docker-compose.yml 已含服务（host network）|

完整 gap 列表见 Doc 1 §10。

---

## 7. 套件本身的修订纪律

- 4 文档作为一个集合演进。本套件 v1.0 是**全集快照**，下次修订全集统一升 v1.1。
- 单文档内部章节修订写入文档末尾"修订记录"，不升套件版本。
- 任何与架构 v1.1.3-pre-stub 顶层决策冲突的修订须先报架构师 sign-off，再回填本套件。
- 实施代码偏离设计 ≥ 2 处时不修改本套件，由实施 D-task 出独立"偏差修复计划"，本套件下次修订一并吸收。

---

## 8. 版本谱系

```
分散设计阶段（2026-05-09 → 2026-05-14）
├── 2026-05-09  决策记录 v1.0 （SIL 框架架构 patch 锁定，选项 D 混合架构）
├── 2026-05-11  capability-inventory + cpp-implementation 参考
├── 2026-05-12  SIL 架构 v1.0 greenfield（9 节点 + lifecycle mgr）
├── 2026-05-13  HMI dual-mode design + dual-mode（船长 vs God 视图）
├── 2026-05-13  D1.3b web-hmi-spec（4-step builder + preflight + report）
└── 2026-05-14  Demo-1 Head-On 完整实现 + 4 项修复（commit acad427）

统一基线（2026-05-15）
└── v1.0 统一设计文档套件（本套件，4 doc）  ← 当前
        ├── 00-README.md
        ├── 01-sil-architecture.md
        ├── 02-sil-backend-design.md
        ├── 03-sil-frontend-design.md
        └── 04-sil-scenario-integration-test.md

后续
└── v1.1 修订（待 DEMO-1 6/15 通过 + Doc 2/3/4 实际撰写时发现的差距批量回填）
```

---

## 9. 联系与责任

- **套件维护者**：marinehdk（角色：架构师 + V&V 工程师 + 后端 + 前端，本人在 4 角色独立 Claude 会话中循环）
- **跨团队接口**：6 RFC + 新增 RFC-007/008/009 全部已批准，跨团队改动须先发函（详见 `docs/Design/Cross-Team Alignment/RFC-decisions.md`）
- **认证 surveyor**：CCS（首发）；DNV/Brinav MoU 2024 作工具链合规背书

---

*README 版本 v1.0 · 2026-05-15 · 套件创建。Doc 1 架构同步交付。*
