# AGENTS.md

本文件约束本仓库 Agent / Codex 工作方式。内容已从 `CLAUDE.md` 完整迁移，并保留本文件原有 branch / remote 约定。若只维护一个 Agent 入口，优先维护本文件。

## 0. Codex 本地工具说明

- 本项目默认 **RTK + caveman + MemPalace** 三层省 token：RTK 压缩工具输出，caveman 压缩对话输出，MemPalace 承接压缩/结束后的开发记忆。
- RTK CLI 若不在 PATH，使用绝对路径：`/opt/homebrew/bin/rtk`。本仓库已由 `rtk init --codex` 写入 `RTK.md`，执行 shell 命令优先显式加 `rtk`。
- MemPalace CLI 若不在 PATH，使用绝对路径：`/Users/marine/.local/bin/mempalace` 与 `/Users/marine/.local/bin/mempalace-mcp`。
- MemPalace palace 路径：`~/.mempalace/palace`。
- MemPalace 项目 wing：`mass_l3_tactical_layer`（见 `mempalace.yaml`）。
- Codex 启动/压缩/结束应由 `~/.codex/hooks.json` 调用 MemPalace `--harness codex`；禁止恢复旧 handoff 脚本或 `--harness claude-code`。
- MemPalace 状态若出现 quarantined / corrupt HNSW segment，按项目既有规则运行：`/Users/marine/.local/bin/mempalace repair --yes`。
- codegraph MCP 若会话内未暴露，先说明不可用，再用 `rg` / 文件结构兜底定位；不要恢复旧 graphify 工作流。

## 1. 项目状态（2026-06-09）

- 阶段 Phase 1 中段；DEMO-1 6/15（不可取消）· DEMO-2 7/31 · DEMO-3 8/31。总账 [00-master-plan.md](docs/Design/00-master-plan.md)（≤300行）；阶段 [Phase 1/00-overview.md](docs/Design/Phase%201/00-overview.md)。
- 权威文件（勿读归档）：架构主文件 [MASS_ADAS_L3_TDL_架构设计报告.md](docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) v1.1.3-pre-stub · [SIL/v1.0-unified/](docs/Design/SIL/v1.0-unified/) 01–04。历史 RFC 决策参考：[RFC-decisions.md](docs/Design/Phase%200/Archive/Cross-Team%20Alignment/RFC-decisions.md)（6 RFC 批准）。
- 活跃约束：132 项 `[TBD-HAZID]` 待 HAZID RUN-001 校准（8/19 回填 v1.1.3）；2026-12 实船试航降级为非认证 AIS 采集。

## 2. 系统坐标系

本仓库 = **L3 战术层 [sec~min]**，五层主决策栈第三层：

```text
L1 Mission[hrs~days] — L2 Voyage[min~hrs] — L3 Tactical ⬅本仓库 — L4 Guidance[100ms~1s] — L5 Control[10ms~100ms]
独立轴：X-axis Deterministic Checker(VETO) / Y-axis Reflex Arc(极近距 bypass L3)
```

边界：消费 L2 WP + Fusion NavFilter；输出 ψ_cmd/u_cmd/ROT → L4；接受 X-axis VETO。`docs/Init From Zulip/` = 其他层接口参考，**只读**。

## 3. 架构骨架（M1–M8）

| 模块 | 职责 | 频率 |
|---|---|---|
| M1 ODD/Envelope | TDL 调度枢纽，唯一"安全语境"权威 | 0.1–1Hz |
| M2 World Model | 唯一权威世界视图 + COLREG 几何预分类 | 10–50Hz |
| M3 Mission Mgr | 航次计划/ETA/重规划触发 | 长时 |
| M4 Behavior Arbiter | 行为字典 + IvP 多目标仲裁 | 1–4Hz |
| M5 Tactical Planner | Mid-MPC(≥90s)+BC-MPC →(ψ,u,ROT)→L4 | 中+短 |
| M6 COLREGs Reasoner | 规则推理（ODD-aware 参数） | 中时 |
| M7 Safety Supervisor | Doer-Checker，IEC 61508+SOTIF 双轨 | 短时 |
| M8 HMI/Transparency | 唯一对 ROC/船长，SAT-1/2/3 | 50–100Hz |

通信：ROS2 DDS pub-sub，消息强制字段 `stamp`+`schema_version`+`confidence∈[0,1]`+`rationale`。

## 4. 顶层 ADR（不可让步，改 = breaking change 须独立讨论）

1. **ODD = 唯一权威** — 行为切换唯一来源是 M1 ODD；算法禁止各自维护"是否安全"判断。
2. **Doer-Checker 双轨** — M7 须比 Doer 简单 100×，实现路径独立（不共享代码/库/数据结构）。
3. **CMM 三接口** — 每模块 `current_state()`/`rationale()`/`forecast(Δt)+uncertainty()`，由 M8 聚合。
4. **Backseat Driver** — 决策核心零船型常量；**严禁** `if vessel==FCB` 进 A 层。

## 5. 强制约束（认证驱动，触发即讨论）

CCS i-Ship 白盒可审计（引黑箱 ML 时）· IMO MASS Code ODD 可识别 · IEC 61508 SIL2（M1仲裁/M7/MRC 路径的依赖/三方库/状态共享）· ISO 21448 SOTIF（感知降质）· TMR ≥ 60s（Veitch 2024）。

## 6. 设计"完成"判据

追溯（每断言指向文档章节/`[Rx]`/NLM 源）· 接口契约（in/out 消息+频率+置信度，模板见架构报告 §15）· 降级路径（DEGRADED/CRITICAL/OUT-of-ODD）· CCS 映射（DMV-CG-0264 9 子功能，矩阵见架构报告 §14）· 置信度标注 🟢🟡🔴⚫。

## 7. 文档编辑规则

- 改一模块 = 只改那一章 + 接口契约表，**不顺手改其他**；发现他模块问题**指出不改**。
- 引用 `[Rx]` 须分配编号入参考文献，**禁裸贴 URL**。主架构文件名恒 `MASS_ADAS_L3_TDL_架构设计报告.md`（旧版 `git mv` 到 `archive/vX.Y.Z_*.md`）。
- 分层：L1 master-plan · L2 `Phase N/00-overview.md` · L3a `Phase N/D{x.y}-*/D{x.y}-{spec,plan,report}.md` · L3b `TDL-Kernel/M{n}-*/M{n}-{spec,progress}.md`。
- 新 D 任务流：brainstorming→spec→writing-plans→plan→executing-plans→evidence/→report→更新 M{n}-progress.md。编号纯数字点分（D1.3.1）。

## 8. 文件夹读写权限

- ✅ 主战场：`00-master-plan.md`·`Phase N/00-overview.md`·`Phase N/D{x.y}-*/`·`TDL-Kernel/`·`Architecture Design/`(主文件+audit/)·`SIL/v1.0-unified/`·`V&V_Plan/`·`Cross-Team Alignment/`·`Safety/`·`HF/`·`Cert/`·`ConOps/`·`Cybersecurity/`
- ❌ 只读：`*/archive/`·`Phase 0/Archive/`·`Archive/Old Modules/`·`Detailed Design/`(废弃)·`Init From Zulip|SINAN/`(接口参考)
- ⚠️ `docs/Doc From Claude/` 仅追加。`.nlm/`·`.claude/settings.local.json` 由工具管理勿手改。

## 9. 调研路由（项目 7 个 DOMAIN 笔记本，`.nlm/config.json`）

`safety_verification`(IEC61508/SOTIF/FMEDA) · `maritime_regulations`(COLREGs/SOLAS/IMO) · `colav_algorithms`(MPC/IvP/VO/RRT*) · `maritime_human_factors`(HMI/ToR，稀→deep) · `ship_maneuvering`/`silhil_platform`/`cybersecurity`(研究在 local)。用法 `/nlm-ask --notebook colav_algorithms "..."`。

## 10. 阅读入口（任务驱动，不全读）

D 任务前必读 master-plan → Phase N/00-overview → D{x.y}-spec → M{n}-progress。架构查询主文件（§目录+§1-4 骨架，M1=§5…M8=§12）。历史决策 `mempalace search`。

## 11. Git 分支

- `main` 集成分支**禁直接 commit**；功能分支 `feat/d{n}.{m}-{描述}`，一 D-task 一 branch，merge 后删。
- Worktree `.worktrees/{slug}/` 由 `superpowers:using-git-worktrees` 管理勿手动；`claude/*` 临时分支框架管理禁手留。
- **三端同步**：本地 main = GitHub origin/main = GitLab `l3-tdl`，push 新提交须同步三端。

## 12. Docker 构建

`sil_nodes.Dockerfile` 用 BuildKit `--mount=type=cache`——**勿删** `# syntax=docker/dockerfile:1.5` 与 `--mount=type=cache`（删则退化全量编译）。cache：`/root/.ccache`=shared（ccache 线程安全）/ `/opt/ws/build`=private（colcon 不能并发写）。新增含 `colcon build` 的 Dockerfile 遵此模板。

## 13. SIL 部署 = A4000 服务器（治本：本地宿主 CPU 争用使仿真倍速非确定）

| 项 | 值 |
|---|---|
| SSH | `ssh a4000` → 192.168.121.50，user `marine.huang` |
| 仓库 | `~/Code/mass-l3`，跟踪 GitLab `l3-tdl`（origin=gitlab） |
| 端口 | orchestrator 18000 / foxglove 18765 / Vite 5173（8000/8765 被生产栈占，**勿碰**） |
| 启停 | `source scripts/a4000-env.sh` 后 `npm run sys:{start,stop,status}`；HMI http://192.168.121.50:5173 |

- 一键验收 `source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh`（`--sync` 先 pull）。倍速纯后端 `rtf_headless_sweep.py`（需 `export ORCH_URL=https://127.0.0.1:18000`）；HMI 一致性 `cd web && RATE=10 ORCH_PORT=18000 FOX_PORT=18765 npx playwright test e2e/mvp_consistency.spec.ts`（RTF_BAND[7,12] 只对 10× 有效）。
- 编辑循环：本地改 → scp 同路径到 a4000 → 容器内 `colcon build --packages-select <pkg>` → `restart sil-nodes`（bridge 是 Python，scp+restart 免 build）。场景 YAML 同样 scp（orchestrator 每请求重扫，免 build/restart）。
- ⚠️ **A4000 走 scp 部署不走 git**：A4000 的 `git fetch gitlab` 不通 + 工作树常有并行会话未提交的 docs → **禁 `git pull`/`git reset`**（会毁并行会话的活）。更新一律本地 push 三端 + scp 到 A4000。
- **env_disturbance wedge**：单驱动快速循环不复现 → 双驱动并发 race（前端 + CLI 同时 configure 撞 SetParameters → 15s 超时）。**规约：同一时刻只一个 configure 驱动**；复位 `docker compose restart sil-nodes`。

## 14. 代码图谱 = codegraph（MCP，替代已删的 graphify）

- 定位代码/继承/调用链：优先 `codegraph_explore`（一次调用返回相关 symbol 源码，多数问题一次够）；细分 `codegraph_search`/`codegraph_callers`/`codegraph_callees`/`codegraph_impact`。
- **自动索引**：文件 watcher 约 1s 跟进写入，**无需手动 update**。
- **禁暴力搜索**：未定位前禁大范围 grep / 读整文件；先 codegraph 拿坐标再读特定范围。

## 15. 记忆与接力

- **唯一记忆持久层 = MemPalace**。本项目不再依赖旧 `handoff/` 多应用接力流程；不要自动调用 `handoff-save.py` / `handoff-recall.py`，也不要恢复已删除的 handoff workflow。
- 新 Codex 对话开始时，先运行：`mempalace wake-up --wing mass_l3_tactical_layer`；任务有关键词时再运行：`mempalace search --wing mass_l3_tactical_layer "<kw>"`。
- 会话压缩前与结束时，由 Codex hooks 调用：`MEMPAL_DIR="/Users/marine/Code/MASS-L3-Tactical Layer" mempalace hook run --hook precompact --harness codex` 与 `... --hook stop --harness codex`，确保当前 transcript 与项目上下文入库。
- 若需要手动补救当前会话，定位 Codex transcript 后用：`mempalace mine <transcript_or_dir> --mode convos --wing sessions`；项目文件补采集用：`mempalace mine "/Users/marine/Code/MASS-L3-Tactical Layer"`。
- 完成重要工作时，可在最终回复里写清当前分支、commit、验证命令和剩余风险；不再额外维护 `handoff/workspace_log.md`。

## branch / remote conventions

- 本地开发与 GitHub 以 `main` 作为主分支。
- A4000 上的 GitLab 仓库因权限限制，以 `l3-tdl` 作为主分支/同步目标。
- 涉及 A4000 部署、验收或 `scripts/a4000-acceptance.sh --sync` 时，目标分支使用 `l3-tdl`；不要把 A4000 GitLab 主线误判为 `main`。

@RTK.md
