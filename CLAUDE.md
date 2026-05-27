# CLAUDE.md

本文件只记录全局 `~/.claude/CLAUDE.md` 未覆盖的项目专属规则。**全局规则优先。**

---

## 1. 项目当前状态（2026-05-27）

- **阶段**：Phase 1 中段 → DEMO-1 Skeleton Live **6/15**（不可取消）；DEMO-2 7/31；DEMO-3 8/31
- **路线进度**：详见 [docs/Design/00-master-plan.md](docs/Design/00-master-plan.md)（≤300 行总账）
- **Phase 1 活跃任务**：D1.3.1/D1.3.2/D1.3.3 并行；🔴 stub 空壳：D1.4 / D1.6 / D1.7
- **当前阶段 overview**：[docs/Design/Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md)

**当前权威文件（不要读归档）：**

| 文件 | 版本 |
|---|---|
| [docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) | v1.1.3-pre-stub |
| [docs/Design/SIL/v1.0-unified/](docs/Design/SIL/v1.0-unified/) | 01–04 套件 |
| [docs/Design/Cross-Team Alignment/RFC-decisions.md](docs/Design/Cross-Team%20Alignment/RFC-decisions.md) | 6 RFC 全批准 |
| `docs/Design/Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md` | ❌ 只读归档，含完整 DoD |

**架构活跃约束：**
- 仍有 **132 项 [TBD-HAZID]** 参数待 HAZID RUN-001 校准（8/19 → v1.1.3 回填）
- 2026-12 实船试航已降级为非认证级 AIS 数据采集（详见 consolidated-findings §13.3）

---

## 2. 系统坐标系

本仓库 = **L3 战术层 [sec~min]**，是完整 MASS 系统五层主决策栈的第三层：

```
L1 Mission [hrs~days] — L2 Voyage Planner [min~hrs] — L3 Tactical ⬅⬅ 本仓库
L4 Guidance [100ms~1s] — L5 Control [10ms~100ms]
独立轴：X-axis Deterministic Checker（VETO 权）/ Y-axis Reflex Arc（极近距 bypass L3）
```

**L3 接口边界：** 消费 L2 WP + Fusion NavFilter；输出 ψ_cmd/u_cmd/ROT → L4；接受 X-axis VETO。  
`docs/Init From Zulip/` = 其他层接口参考，**只读，不改**。

---

## 3. 架构骨架（M1–M8）

| 模块 | 职责 | 时间尺度 |
|---|---|---|
| **M1** ODD/Envelope Manager | TDL 调度枢纽，唯一"安全语境"权威 | 0.1–1 Hz |
| **M2** World Model | 唯一权威世界视图 + COLREG 几何预分类 | 10–50 Hz |
| **M3** Mission Manager | 航次计划、ETA、重规划触发 | 长时 |
| **M4** Behavior Arbiter | 行为字典 + IvP 多目标仲裁 | 1–4 Hz |
| **M5** Tactical Planner | Mid-MPC（≥90s）+ BC-MPC → (ψ,u,ROT) → L4 | 中时+短时 |
| **M6** COLREGs Reasoner | 规则推理（ODD-aware 参数） | 中时 |
| **M7** Safety Supervisor | Doer-Checker Checker，IEC 61508 + SOTIF 双轨 | 短时 |
| **M8** HMI/Transparency | 唯一对 ROC/船长说话，SAT-1/2/3 透明性 | 50–100 Hz |

模块通信：ROS2 DDS 发布-订阅，消息强制字段：`stamp` + `schema_version` + `confidence∈[0,1]` + `rationale`。

---

## 4. 顶层架构决策（ADR — 不可让步）

1. **ODD = 唯一权威** — M1 ODD 状态是行为切换的唯一来源；算法禁止各自维护"是否安全"判断
2. **Doer-Checker 双轨** — M7 逻辑须比 Doer 简单 100×；实现路径独立（不共享代码/库/数据结构）
3. **CMM 三接口** — 每模块须实现 `current_state()` / `rationale()` / `forecast(Δt)+uncertainty()`，由 M8 聚合
4. **Backseat Driver 范式** — 决策核心零船型常量；**严禁** `if vessel == FCB` 进入 A 层

修改上述任一项 = **ADR breaking change**，须独立讨论。

---

## 5. 强制约束（认证驱动）

| 约束 | 触发讨论的场景 |
|---|---|
| CCS i-Ship (Nx, Ri/Ai)，白盒可审计 | 引入黑箱 ML / 隐式状态 |
| IMO MASS Code，ODD envelope 可识别 | 使"是否在 ODD 内"判断变模糊 |
| IEC 61508 SIL 2（M1 仲裁/M7/MRC） | 这些路径上的依赖、第三方库、状态共享 |
| ISO 21448 SOTIF | 感知降质、ML 功能不足 |
| TMR ≥ 60s（Veitch 2024）| 压缩 ROC 可用接管时间 |

---

## 6. 设计"完成"判据

- [ ] 追溯：每条断言指向仓库文档章节 / `[Rx]` 引用 / NLM 笔记本来源
- [ ] 接口契约：输入消息 / 输出消息 / 频率 / 置信度字段（模板见架构报告 §15）
- [ ] 降级路径：DEGRADED / CRITICAL / OUT-of-ODD 行为说明
- [ ] CCS 映射：决策模块映射到 DMV-CG-0264 的 9 个子功能（覆盖矩阵见架构报告第 14 章）
- [ ] 置信度标注：网络/推断结论用 🟢🟡🔴⚫（全局规则）

---

## 7. 文档编辑规则

- 改一个模块 = 只改那一章 + 接口契约表。**不顺手改其他章节/格式**
- 引用编号 `[Rx]` 硬约束：新增引用须分配编号入参考文献，**禁止裸贴 URL**
- 发现其他模块问题：**指出，不要自己改**
- 主架构文件名永远是 `MASS_ADAS_L3_TDL_架构设计报告.md`（无版本后缀）；旧版 `git mv` 到 `archive/vX.Y.Z_*.md`

### 7.1 文档分层（v3.2-master）

| 层 | 路径 | 用途 |
|---|---|---|
| L1 总账 | `docs/Design/00-master-plan.md` | 工时/DEMO milestone/导航，≤300 行 |
| L2 阶段 | `docs/Design/Phase N/00-overview.md` | D 任务索引 + 进度快照 |
| L3a D 任务 | `docs/Design/Phase N/D{x.y}-*/D{x.y}-{spec,plan,report}.md` | spec→plan→report |
| L3b M 模块 | `docs/Design/TDL-Kernel/M{n}-*/M{n}-{spec,progress}.md` | 模块设计 + D 任务联动表 |

**新 D 任务流程：** brainstorming → spec → writing-plans → plan → executing-plans → evidence/ → report → 更新 M{n}-progress.md

**D 任务编号**：纯数字点分（D1.3.1/D2.4），git 分支名保留旧 a/b/c 不强改。

---

## 8. 文件夹读写权限

| 路径 | 可写？ |
|---|---|
| `docs/Design/00-master-plan.md` + `Phase N/00-overview.md` + `Phase N/D{x.y}-*/` + `TDL-Kernel/` | ✅ 主战场 |
| `docs/Design/Architecture Design/`（主文件 + audit/）| ✅ 主战场 |
| `docs/Design/SIL/v1.0-unified/` · `V&V_Plan/` · `Cross-Team Alignment/` · `Safety/` · `HF/` · `Cert/` · `ConOps/` · `Cybersecurity/` | ✅ 主战场 |
| `docs/Design/*/archive/` · `Phase 0/Archive/` · `Archive/Old Modules/` | ❌ 只读历史 |
| `docs/Design/Detailed Design/` | ❌ 废弃，不写新文件 |
| `docs/Doc From Claude/` | ⚠️ 仅追加，不改旧文件 |
| `docs/Init From Zulip/` · `docs/Init From SINAN/` | ❌ 只读接口参考 |
| `.nlm/` · `.claude/settings.local.json` | 由对应工具管理，不手动编辑 |

---

## 9. 调研路由（项目专属 NLM 笔记本）

全局规则已定路由；本项目额外 7 个 DOMAIN 笔记本（`.nlm/config.json`）：

| key | sources | 覆盖领域 |
|---|---|---|
| `safety_verification` | 64 | IEC 61508, SOTIF, FMEDA |
| `maritime_regulations` | 89 | COLREGs, SOLAS, IMO, MASS Code |
| `colav_algorithms` | 91 | MPC, IvP, VO, RRT*, MPPI |
| `maritime_human_factors` | 19 | HMI, ToR, BNWAS（偏稀→优先 deep）|
| `ship_maneuvering` | 0 | MMG, hydrodynamics（研究在 local_notebook）|
| `silhil_platform` | 0 | SIL/HIL, FMI 2.0, Kongsberg |
| `cybersecurity` | 0 | IACS UR E26/E27, DDS-Security |

用法：`/nlm-ask --notebook colav_algorithms "IvP 多目标仲裁权重设定"`

---

## 10. 阅读入口（按任务驱动，不要全读）

**任何 D 任务前必读：** master-plan.md → Phase N/00-overview.md → D{x.y}-spec.md → M{n}-progress.md  
**架构查询：** 架构主文件（§目录 + §1-4 骨架；M1=§5…M8=§12）  
**历史决策：** `mempalace search "关键词"` → sessions wing  
**只读审计：** `docs/Design/Phase 0/Archive/Review/2026-05-07/00-consolidated-findings.md`

---

## 11. Git 分支与 Worktree

- `main`：集成分支，**禁止直接 commit**
- 功能分支：`feat/d{n}.{m}-{短描述}`，一个 D-task = 一个 branch，merge 后立即删除
- Worktree：`.worktrees/{branch-slug}/`，由 `superpowers:using-git-worktrees` 管理，不手动 mkdir/rm
- `claude/*` 临时分支由 subagent 框架管理，**禁止手动保留**
- 清理判据：`git log --oneline {branch} ^main` 输出为空 → 可删

---

## 12. Docker 构建规范（OrbStack + ROS2）

`sil_nodes.Dockerfile` 使用 BuildKit `--mount=type=cache`——**不要删除** `# syntax=docker/dockerfile:1.5` 声明和 `--mount=type=cache` 参数，删除会退化为全量编译。

| cache mount | sharing | 原因 |
|---|---|---|
| `/root/.ccache` | `shared` | ccache 线程安全 |
| `/opt/ws/build` | `private` | colcon 中间产物不能并发写 |

新增含 `colcon build` 的 Dockerfile 必须遵循此模板（见现有 `sil_nodes.Dockerfile`）。

---

## 13. 本地+容器混合部署（PM2 + Docker）

宿主机 Native 运行前端 HMI + Docker 运行 4 个后端容器。统一通过根目录 npm 脚本控制：

| 命令 | 作用 |
|---|---|
| `npm run sys:start` | Docker up + PM2 start |
| `npm run sys:stop` | PM2 delete + Docker down |
| `npm run sys:status` | PM2 list + Docker ps |

单独调试：`pm2 restart sil-frontend` / `docker compose restart sil-orchestrator`

---

## graphify

This project has a knowledge graph at `graphify-out/`.

- 代码结构/跨文件关系 → `graphify query "<question>"` / `graphify path "<A>" "<B>"` / `graphify explain "<concept>"`
- 广义架构浏览 → `graphify-out/wiki/index.md`（若存在）；`GRAPH_REPORT.md` 仅用于全局架构审查
- 修改代码后：`graphify update .`（AST-only，无 API 成本）
- **分工**：代码结构用 graphify；对话决策历史/设计讨论用 mempalace（↓）

## mempalace

Palace: `~/.mempalace/palace`（通用命令/hooks 见全局 `~/.claude/CLAUDE.md`）

| Wing | Drawers | 内容 |
|---|---|---|
| `mass_l3_tactical_layer` | 53k | 项目文件：`technical`(22k) · `documentation`(17k) · `architecture`(5k) · `planning`(1k) |
| `sessions` | 18k | 历史 Claude Code 对话（架构决策、模块设计讨论、调试记录） |

**搜索触发器：** "之前为什么选 X" / "上次 M5 怎么设计的" → `sessions` wing；找设计文档位置 → `documentation`/`architecture` room  
**MCP：** `mcp__mempalace__mempalace_search`（支持 `wing`/`room` 过滤）
