# DEMO-1 / DEMO-2 GAP 评审报告

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-REVIEW-DEMO-GAP |
| 版本 | v1.0 |
| 审查日期 | 2026-05-25 |
| 审查方法 | 4 并行 subagent 代码审阅 + 4 并行深度验证 + 文档交叉比对 |
| 审查范围 | Git 分支/提交状态 / SIL 框架代码 / M1-M8 模块实现 / CI-CD 与测试基础设施 |
| 基线文档 | [00-master-plan.md](../00-master-plan.md) v3.2-master (2026-05-22) |

---

## 0. 执行摘要

| DEMO | 距截止 | 整体风险 | 一句话 |
|---|---|---|---|
| **DEMO-1** (6/15) | **21 天** | 🟡 **中低** | 代码基本就绪，主要风险在 SAT-1 威胁列表空壳 + ConOps/cert-tracking stub 状态 + 工作树极脏未提交 |
| **DEMO-2** (7/31) | **67 天** | 🔴 **高** | M4 IvP/M5 BC-MPC/M8 SAT 桥接三段断裂是硬阻塞；前端 handler 实际已就绪（与 master plan 记录不一致）；D2.1-D2.4 证据未版本化 |

**最紧急行动**：
1. 🔴 立即提交 working tree 中 D2.1-D2.4 代码到 feature 分支（+12K/-3K 行未版本化）
2. 🔴 M4 IvP solver 接入 node 回调 + ivp_contributions publisher（7/13 必须开工）
3. 🔴 M5 BC-MPC trajectory_candidates 全链路打通（7/13 必须开工）
4. 🔴 M8 新增 3 个 SAT publisher + sil_msgs 消息定义

---

## 1. Git 与版本控制状态

### 1.1 分支全景

| 分支 | 未合并 commits | 状态 | 判定 |
|---|---|---|---|
| `main` (HEAD: 7987177) | — | 集成分支 | ✅ |
| `feat/d2.1-m1-odd-hardening` | 0 | 已合并 | 🧹 可删除 |
| `origin/feat/d2.7-hara-fmeda-m1` | 0 | 已合并 | 🧹 可删除远程 |
| `origin/feat/d2.8-arch-v1.1.3-stub` | 0 | 已合并 | 🧹 可删除远程 |

**CLAUDE.md §13.6 记录的 6 个分支全部已不存在**（d1.3b.1 / d1.3b.3 / d1.3b.3-web-hmi / sil-demo1-head-on / d3.1-m4-behavior-arbiter / d3.3b-m7-sotif），均已合入 main。需更新 CLAUDE.md。

### 1.2 🔴 工作树极脏（最大版本控制风险）

| 指标 | 数值 |
|---|---|
| 总变更项 | **287** |
| 已跟踪文件修改 | **64** |
| 未跟踪文件 | **222**（~160 为 graphify-out 缓存） |
| 代码变更量 | **+12,276 / -3,161 行** |

**未提交的关键文件**：

| 类别 | 文件 | 关联 D 任务 |
|---|---|---|
| L3 内核代码 | M1/M3/M4/M7/M8 node .hpp/.cpp + test | D2.1–D2.4 |
| SIL 基础设施 | docker-compose.yml / sil_topic_bridge.py / lifecycle_*.py | D2.5 |
| Web HMI | SilMapView / SafetyDomainLayer / SimulationMonitor / telemetryStore / vite.config | D1.3.2.3 / D2.5 |
| 闭口证据 | D2.1-report.md + evidence/ / D2.2-report.md + evidence/ / D2.3-report.md + evidence/ / D2.4-report.md + evidence/ | D2.1–D2.4 |

**风险**：如果误操作 `git checkout` 或 `git reset`，D2.1-D2.4 全部工作将丢失。

### 1.3 Stash 状态

| Stash | 描述 | 判定 |
|---|---|---|
| stash@{0} | "On main: local before D2.4 merge" | 🟡 可能含 D2.4 合并前快照 |
| stash@{1} | "WIP on feat/d1.3b-scenario-hmi: build-green" | 🟢 旧分支已不存在，可丢弃 |
| stash@{2} | "WIP on main: check-msg-contract.sh" | 🟢 可能已过时 |

---

## 2. DEMO-1 Charter 逐项审查（目标 6/15）

### 2.1 逐项就绪度

| # | Charter 要求 | 代码状态 | 就绪度 | GAP 描述 |
|---|---|---|---|---|
| 1 | AIS 历史数据回放（NOAA 1h 片段）→ Mock M2 → 仿真器 own-ship 运动 | `ais_bridge/replay_node.py` 完整实现；`sil_mock_node.py` 发布 Mock M2；`ship_dynamics/node.py` 4-DOF MMG | ✅ 就绪 | 无 |
| 2 | 1 个 Crossing 场景脚本 live → SIL HMI 显示 → ODD 评估（Mock M1）→ SAT-1/2/3 视图 | Crossing YAML 场景存在；SAT-1 状态文字真实；SAT-2/3 wireframe 级 | 🟡 基本就绪 | SAT-1 威胁列表后端硬编码为空；SAT-2/3 仅有 mock 数据 |
| 3 | PR-trigger Smoke 10 跑通 + 覆盖率立方体热图（10 cell 亮） | `run_smoke_10.sh` 存在；`coverage_cube.py` + `coverage_reporter.py` 存在 | 🟡 需运行确认 | 需实际运行验证 10 cells 能亮；CI 未集成覆盖率热图自动生成 |
| 4 | ConOps PDF + V&V Plan PDF + cert-evidence-tracking.md 现场展示 | V&V Plan ✅ 完整；ConOps v0.1 🟡 stub；cert-evidence-tracking 🟡 stub | 🟡 基本就绪 | ConOps 和 cert-tracking 内容偏薄，DEMO-1 展示可通过口头说明规避 |
| 5 | 仿真器参考解（旋回）实测 + tool confidence 报告 | D1.3.1' Sim Qual 3 参考解 pytest + 100 次重跑 + TCL-3 PASS | ✅ 就绪 | 无 |

### 2.2 DEMO-1 风险矩阵

| 风险 | 等级 | 缓解 |
|---|---|---|
| SAT-1 威胁列表硬编码为空 | 🟡 低 | DEMO-1 只需 wireframe 级，口头说明即可 |
| Coverage heatmap 未实际验证 10 cells | 🟡 低 | 提前跑一次确认 |
| ConOps/cert-tracking 内容偏薄 | 🟡 低 | DEMO-1 不要求完整版，v0.1 stub 可接受 |
| 工作树脏导致代码丢失 | 🔴 中 | **立即提交 D2.1-D2.4 到 feature 分支** |

### 2.3 DEMO-1 总评

**DEMO-1 代码层面基本就绪**。所有 D1.x 相关代码已在 main，不存在分支级阻塞。主要风险是工作树未版本化和部分 stub 内容，但均可在 21 天内解决。

---

## 3. DEMO-2 Charter 逐项审查（目标 7/31）

### 3.1 逐项就绪度

| # | Charter 要求 | 代码状态 | 就绪度 | GAP 描述 |
|---|---|---|---|---|
| 1 | 50 综合场景批量回放 + Web HMI 时间轴 scrubber <100ms 跳转 | `batch_runner.py` + `batch_runner_d24.py` 存在；Web HMI 时间轴组件存在 | 🟡 部分 | 批量回放脚本存在但未集成到 CI；scrubber 性能未测 |
| 2 | ODD-A→B→C→D 实时切换 + ToR 自适应矩阵动画 | M1 ODD FSM 实装 ✅；前端 ODD 切换组件存在 | 🟡 部分 | ODD 切换逻辑在代码中但未端到端验证 |
| 3 | SAT-2 全展：M6 5 层 colregs_chain + M4 IvP 8 方向贡献图 + M5 13 弧 BC-MPC 候选轨迹 | **三段断裂**（详见 §3.2） | 🔴 **阻塞** | M4 IvP solver 未接入 node / M5 trajectory_candidates 全链路缺失 / M8 不发布 SAT-2/3 topic |
| 4 | 船长访谈片段 + Figma 原型 + 资深船长签字 | D2.6 框架/模板 ✅ | 🔴 未启动 | 等 6/16 HF 外包 onboard |
| 5 | HARA v0.1 现场评审 + 危险源→SIF→SIL 全链路 demo | D2.7 ✅ HARA 32 + FMEDA 20 | ✅ 就绪 | 无 |
| 6 | 架构 v1.1.3 stub 全文 walkthrough | D2.8 ✅ §16–§22 全部到位 | ✅ 就绪 | 无 |
| 7 | KPI 仪表盘（时延 P95/P99 / 首跑通过率 ≥90% / 6 维度分数分布） | `KpiDashboard.tsx` + `kpi_collector.py` 存在 | 🔴 空壳 | 组件壳存在但无真实数据源；6 维度评分未自动化；时延未测 |

### 3.2 SAT-2 全展三段断裂详细诊断

这是 DEMO-2 的核心阻塞，需要精确理解断裂位置。

#### 3.2.1 M4 IvP Solver — "孤岛"状态

| 层 | 状态 | 详情 |
|---|---|---|
| IvP 求解器底层 | ✅ 真实实现（~376 行） | `ivp_domain.hpp` + `ivp_function.hpp` + `ivp_combine.cpp` + `ivp_solver.cpp`，含两遍网格搜索 + timeout 保护 |
| Node 仲裁回调 | 🔴 **IvP 未接入** | `arbitration_timer_callback()` 走 `max(priority_weight)` 硬优先级，IvPSolver 从未被实例化/调用 |
| `ivp_contributions` topic | 🔴 **完全不存在** | 全项目搜索 `src/` 下零命中；仅存在于前端 TypeScript 类型定义 |
| 前端消费端 | ✅ 完整 | `IvpRiskGradientLayer.tsx` 8 方向风险梯度箭头渲染 + `SimulationMonitor.tsx` 消费 |

**量化**：底层求解器 100% 真实逻辑，但 node 层 0% 调用，`ivp_contributions` publisher 0% 实现。

**DEMO-2 stub 路线**：可用固定权重表生成 8 方向 cost mock，不跑真实 IvP → 约 2 天工作量。

**DEMO-2 完整路线**：接入 solver + IvPFunctionFactory + get_active_subset 去 stub → 约 4 pw。

#### 3.2.2 M5 BC-MPC — 几何枚举骨架

| 层 | 状态 | 详情 |
|---|---|---|
| BC-MPC solver | 🟡 Phase E1 几何枚举 | 5/7 分支 + minimax CPA，**不是真正的 MPC 优化** |
| `trajectory_candidates` topic | 🔴 **全链路缺失** | 从消息定义到 publisher 到数据填充全部不存在 |
| 前端消费端 | ✅ 完整 | `MpcTrajectoryLayer.tsx` 13 弧轨迹渲染 |

**DEMO-2 stub 路线**：线性化 Nomoto + 13 弧枚举兜底 → 约 6.5 pw。

**DEMO-2 完整路线**：CasADi IPOPT 真求解 → 推 Phase 3 D3.2。

#### 3.2.3 M8 SAT-2/3/SOTIF 桥接 — 后端真空

| 层 | 状态 | 详情 |
|---|---|---|
| M8 发布 `/sil/sat2_data` | 🔴 **不发布** | `init_publishers()` 仅 3 个话题，无 SAT-2/3/SOTIF |
| M8 内部 SatAggregator | ✅ 已缓存 SAT-2/3 数据 | 但 `UiStateBuilder::build()` 显式 `(void)sat_cache;` 忽略 |
| `sil_msgs` 消息定义 | 🔴 **3 个 .msg 全部缺失** | `sil_msgs/SAT2Data` / `sil_msgs/SAT3Data` / `sil_msgs/SotifMetrics` 不存在 |
| 前端 handler | ✅ **3 个 handler 已写好** | `useFoxgloveLive.ts` 订阅 + store action + 类型 + 4 个消费组件全部就位 |
| SOTIF 指标计算 | 🔴 **无任何模块实现** | 6 个指标（AIS/雷达一致性 σ 等）零计算逻辑 |
| Mock publisher | 🔴 **无 `/sil/` 命名空间 SAT mock** | `sil_mock_node.py` 只发 `/l3/sat/data`，不发 `/sil/` 话题 |

**⚠️ 修正 master plan 记录**：master plan 说"前端 useFoxgloveLive 缺 3 handler"，**实际前端 handler 已完整**。真正阻塞在后端生产侧——M8 不发布、sil_msgs 无定义、无桥接节点。前端是"空转"状态，不是"缺件"状态。

**端到端数据流断点图**：

```
M1/M2/M4/M6/M7 ──→ /l3/sat/data ──→ M8 SatAggregator.ingest() ✅
                                                │
                                                │ (void)sat_cache; ← 显式忽略！
                                                ▼
                                      /l3/m8/ui_state ✅ 但无 SAT 字段

  ──── 断裂点 1：M8 不发布 /sil/sat2_data, /sil/sat3_data, /sil/sotif_metrics ────
  ──── 断裂点 2：sil_msgs 包无 SAT2Data/SAT3Data/SotifMetrics .msg 定义 ────
  ──── 断裂点 3：无桥接节点将 l3_msgs → sil_msgs 话题转换 ────

  foxglove_bridge ──→ WebSocket ──→ useFoxgloveLive TOPIC_MAP
                                      │
                    /sil/sat2_data ──→ handler 存在但无数据 ❌
                    /sil/sat3_data ──→ handler 存在但无数据 ❌
                    /sil/sotif_metrics ──→ handler 存在但无数据 ❌
```

### 3.3 M6 COLREGs Reasoner 状态

| 层 | 状态 | 详情 |
|---|---|---|
| ROS2 node | ✅ 真实实现 | `colregs_reasoner_node.cpp` 完整 |
| `colregs_chain` 发布 | 🟡 部分 | IDL + Arrow 评分管线 ✅，C++ chain 填充 + colcon 测试待关闭（SIL-6） |
| chain 截图 | 🔴 缺失 | D2.4 T10 要求 ≥10 场景 Playwright 截图，未完成 |

### 3.4 D2.1-D2.4 证据状态

| D 任务 | 代码 | Report | Evidence | 版本化 |
|---|---|---|---|---|
| D2.1 M1 ODD | 🟡 working tree 修改 | 🔴 untracked | 🔴 untracked | ❌ 未提交 |
| D2.2 M2 World Model | 🟡 working tree 修改 | 🔴 untracked | 🔴 untracked | ❌ 未提交 |
| D2.3 M3 Mission | ✅ 实装+测试 | 🔴 untracked | 🔴 untracked | ❌ 未提交 |
| D2.4 M6 COLREGs | 🟡 IDL+评分管线 | 🔴 untracked | 🔴 untracked | ❌ 未提交 |

### 3.5 KPI 仪表盘状态

| KPI | 后端计算 | 前端展示 | 自动化 |
|---|---|---|---|
| 端到端时延 P95/P99 | `kpi_collector.py` + `dds_fmu_latency.py` 存在 | `KpiDashboard.tsx` 壳存在 | ❌ 未集成 CI |
| 首跑通过率 ≥90% | ❌ 无测量工具 | ❌ 无组件 | ❌ 未自动化 |
| 6 维度分数分布 | `scoring_node.py` 存在 | ❌ 无分布图组件 | ❌ 未自动化 |
| 50 场景批量 GIF/PNG | `batch_runner_d24.py` 存在 | ❌ Puppeteer 导出未实现 | ❌ 未自动化 |

---

## 4. M1-M8 模块实现状态汇总

| 模块 | ROS2 Node | 真实逻辑 | 发布 Topic | SAT 桥接 | DEMO-1 | DEMO-2 |
|---|---|---|---|---|---|---|
| **M1** ODD | ✅ | ✅ FSM 实装 | `/l3/m1/odd_state` | — | ✅ | 🟡 需 evidence |
| **M2** World Model | ✅ | ✅ 5 轨道设计 | `/world_model/tracks` | — | ✅ | 🟡 需 evidence |
| **M3** Mission | ✅ | ✅ 实装+测试 | `/l3/m3/mission_state` | — | ✅ | 🟡 需 report |
| **M4** Behavior Arbiter | ✅ | 🟡 IvP 孤岛 | `/l3/m4/behavior_plan` | 🔴 无 ivp_contributions | 🟡 | 🔴 **P0 阻塞** |
| **M5** Tactical Planner | ✅ | 🟡 几何枚举 | `/l3/m5/planning_output` | 🔴 无 trajectory_candidates | 🟡 | 🔴 **P0 阻塞** |
| **M6** COLREGs | ✅ | ✅ 推理逻辑 | `/l3/m6/colregs_output` | 🟡 chain 填充待关闭 | ✅ | 🟡 chain 截图缺 |
| **M7** Safety | ✅ | ✅ Doer-Checker | `/l3/m7/safety_decision` | — | ✅ | 🟡 |
| **M8** HMI Bridge | ✅ | 🟡 SAT 桥接断裂 | `/l3/m8/ui_state` | 🔴 无 SAT-2/3/SOTIF | 🟡 | 🔴 **P0 阻塞** |

---

## 5. CI/CD 与测试基础设施

| 维度 | 状态 | DEMO-1 GAP | DEMO-2 GAP |
|---|---|---|---|
| CI 流水线 | `.gitlab-ci.yml` build/test/coverage/deploy | 🟡 覆盖率热图未自动生成 | 🔴 50 场景批量未集成 |
| Docker | `docker-compose.yml` ROS2 Humble + SIL | ✅ | 🟡 运行时配置待定义 |
| Smoke 10 | `run_smoke_10.sh` 存在 | 🟡 需实际验证 | — |
| 覆盖率立方体 | `coverage_cube.py` + `coverage_reporter.py` | 🟡 需验证 10 cells | 🔴 6 维度分布未自动化 |
| 时延测量 | `kpi_collector.py` + `dds_fmu_latency.py` | — | 🔴 P95/P99 阈值未设 |
| 首跑通过率 | ❌ 无工具 | — | 🔴 ≥90% 未测 |
| Evidence pack | `batch_runner_d24.py` 存在 | — | 🔴 GIF/PNG 导出未实现 |
| Playwright E2E | 存在 | ✅ | 🟡 chain 截图 ≥10 未完成 |

---

## 6. DEMO-2 P0 GAP 汇总（按急迫度重排）

### 🔴 P0 阻塞项（7/31 前必须完成）

| # | GAP 项 | 根因 | 工时 | 关键路径 | 修正说明 |
|---|---|---|---|---|---|
| 1 | **M4 IvP 接入 node + ivp_contributions publisher** | Solver 已写好但从未被 node 调用；topic 完全不存在 | 4.0 pw | 7/13 必须开工 | master plan 描述准确 |
| 2 | **M5 BC-MPC trajectory_candidates 全链路** | 消息定义→publisher→数据填充全部缺失 | 6.5 pw | 7/13 必须开工 | master plan 描述准确 |
| 3 | **M8 新增 3 个 SAT publisher + sil_msgs 定义** | M8 不发布 /sil/sat2_data 等；sil_msgs 无 .msg | 1.5 pw | 7/31 | master plan 描述准确 |
| 4 | **SOTIF 指标计算逻辑** | 6 个指标零计算；无任何模块实现 | 0.4 pw | 7/31 | master plan 未单独列出此项 |
| 5 | **前端 useFoxgloveLive handler** | — | — | — | ⚠️ **master plan 需修正**：前端 3 handler 已完整，不是"缺件"而是"空转" |
| 6 | **D2.4 chain_screenshots ≥10 场景** | Playwright 截图未完成 | 0.5 pw | 7/31 | master plan 描述准确 |
| 7 | **M6 colregs_chain C++ 填充 + colcon 测试** | SIL-6 待关闭 | 0.5 pw | 7/31 | master plan 描述准确 |
| 8 | **D1.7 6 维度 rubric Group A1 关闭** | 评分验收依赖 | 1.5 pw | 7/31 | master plan 描述准确 |
| 9 | **D1.6 traceability-matrix.csv** | HAZID 干系人展示 | 2.0 pw | 7/31 | master plan 描述准确 |
| 10 | **D2.1-D2.4 代码+证据提交到版本控制** | working tree +12K/-3K 行未版本化 | 0.5 pw | **立即** | master plan 未列出此项 |

### 🟡 P1 重要项（DEMO-2 前完成但不阻塞演示）

| # | GAP 项 | 详情 |
|---|---|---|
| 11 | KPI 仪表盘真实数据源 | 组件壳存在但无数据；6 维度分数分布未自动化 |
| 12 | 50 场景批量 GIF/PNG evidence pack | Puppeteer headless 导出未实现 |
| 13 | 首跑通过率 ≥90% 测量 | 无自动化工具 |
| 14 | 端到端时延 P95 ≤800ms / P99 ≤1200ms | 测量工具存在但未集成 CI，阈值未设 |
| 15 | D2.6 船长 HF 访谈执行 | 等 6/16 HF 外包 onboard |

---

## 7. 与 Master Plan GAP 表的差异

| Master Plan # | Master Plan 描述 | 本次审查发现 | 差异 |
|---|---|---|---|
| GAP #3 | "M8 增发 SAT-2/3/SOTIF 3 topic + IDL" | 确认准确，但需补充：sil_msgs 3 个 .msg 完全不存在（不只是 IDL） | 🔴 更严重 |
| GAP #4 | "前端 useFoxgloveLive 缺 3 handler" | **前端 3 handler 已完整**（handler + store action + 类型 + 4 个消费组件全部就位），真正缺的是后端生产侧 | ⚠️ **需修正** |
| — | 未列出 | SOTIF 指标 6 个字段零计算逻辑 | 🔴 新增项 |
| — | 未列出 | D2.1-D2.4 代码+证据未版本化（+12K/-3K 行 working tree） | 🔴 新增项 |
| — | 未列出 | M4 IvP solver 已写好但被 node 架空（不是"未实现"而是"未接入"） | 🟡 修正理解 |

---

## 8. 建议行动计划

### 8.1 立即行动（本周内）

1. **提交 D2.1-D2.4 工作到 feature 分支** — 避免代码丢失风险
2. **清理过期分支** — 删除 `feat/d2.1-m1-odd-hardening`（0 commit）+ 远程 `feat/d2.7-*` / `feat/d2.8-*`
3. **更新 CLAUDE.md §13.6** — 分支表严重过时

### 8.2 7/13 前必须开工

4. **M4 IvP stub 路线**：固定权重表生成 8 方向 cost → mock ivp_contributions publisher（~2 天）
5. **M5 BC-MPC stub 路线**：线性化 Nomoto + 13 弧枚举 → mock trajectory_candidates publisher（~3 天）
6. **sil_msgs 新增 3 个 .msg** + **M8 新增 3 个 publisher**（~1.5 pw）

### 8.3 7/31 前完成

7. D2.4 chain_screenshots ≥10 场景
8. M6 colregs_chain C++ 填充 + colcon 测试
9. D1.7 Group A1 关闭
10. D1.6 traceability-matrix.csv
11. SOTIF 指标 stub 计算
12. KPI 仪表盘真实数据接入
13. 50 场景批量 evidence pack

---

## 9. 置信度标注

| 发现 | 置信度 | 依据 |
|---|---|---|
| M4 IvP solver 未接入 node | 🟢 高 | 逐行阅读 `behavior_arbiter_node.cpp` 确认 |
| M5 trajectory_candidates 全链路缺失 | 🟢 高 | 全项目搜索确认 |
| M8 不发布 SAT-2/3/SOTIF topic | 🟢 高 | 逐行阅读 `hmi_transparency_bridge_node.cpp` 确认 |
| 前端 3 handler 已完整 | 🟢 高 | 逐行阅读 `useFoxgloveLive.ts` + store + 类型 + 组件确认 |
| SOTIF 指标零计算逻辑 | 🟢 高 | 全项目搜索确认 |
| DEMO-1 基本就绪 | 🟡 中 | 部分项需实际运行验证（coverage heatmap / smoke 10） |
| DEMO-2 工时估算 | 🟡 中 | M4/M5 stub 路线工时基于代码复杂度估算，实际可能偏差 ±20% |

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-05-25 | 初版：4 并行 subagent 代码审阅 + 4 并行深度验证 + 文档交叉比对 |
