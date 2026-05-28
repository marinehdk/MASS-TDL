# R6 DEMO-1 全栈落地 — 会话续接 Handoff（2026-05-27 晚）

> **本文件用途**：从前一长会话切换到新会话时的状态快照与起手指南。新会话**先读本文**，再决定执行路径。

---

## 1. 任务总目标

实现 `scenarios/IMAZU标准测试/imazu-01-ho.yaml`（Imazu-01 Head-On）的 **DEMO-1 端到端落地**：在真实 M1-M8 决策链下跑出 [`docs/Design/SIL/Demo-1场景.md`](../../SIL/Demo-1场景.md) §1-§5 五阶段物理过程，9 条断言（A-1 ~ A-9）+ 3 条验证（V1-V3）全 PASS。

- **Milestone**：DEMO-1 Skeleton Live **6/15**（不可取消）
- **架构基线**：v1.1.3-pre-stub
- **不走捷径**：全部 M1-M8 模块能力真落地（用户明确要求）

---

## 2. 当前已交付物（按时间顺序）

| 文件 | 内容 | 状态 |
|---|---|---|
| `docs/Design/Review/2026-05-27/R6-DEMO1-full-stack-spec.md` | 10 W-item + 3 V-item spec | ✅ committed (`794d56b2`) |
| `docs/Design/Review/2026-05-27/R6-plan-A-decision-chain.md` | W1-W5 plan (1356 行) | ✅ committed (`1152f36f`) |
| `docs/Design/Review/2026-05-27/R6-plan-B-physical-loop.md` | W6-W7 plan (848 行) | ✅ committed (`1152f36f`) |
| `docs/Design/Review/2026-05-27/R6-plan-C-scoring-watchdog.md` | W8-W9 plan (550 行) | ✅ committed (`1152f36f`) |
| `docs/Design/Review/2026-05-27/R6-plan-D-demo-cleanup.md` | W10 plan (856 行) | ✅ committed (`1152f36f`) |
| `docs/Design/Review/2026-05-27/R6-plan-E2E-validation.md` | V1-V3 + 9 断言 plan (785 行) | ✅ committed (`1152f36f`) |

## 3. 已执行任务（subagent-driven）

| Task | Worktree / Branch | Commits | 状态 |
|---|---|---|---|
| **W8** scoring Arrow open_file fix | `.worktrees/d-demo1-r6-c` / `feat/d-demo1-r6-c` | `f6b51b98` | ✅ DONE (spec ✅ + code ✅) |
| **W1** mock_l2 publisher integration | `.worktrees/d-demo1-r6-a` / `feat/d-demo1-r6-a` | `27973b28`, `1e4cc597`, `9c…drop unused Path` | ✅ DONE (spec ✅ + code ✅) |
| **W2** M3 RouteReceived wiring + FSM logging | 同上 worktree A | `96510035` | ⚠️ **DONE_WITH_CONCERNS**（见 §5） |

剩余：**W3, W4, W5, W6, W7, W9, W10A, W10B-E, E2E（共 10 个 task）**

---

## 4. 4 个独立 worktree（已建好）

```
.worktrees/d-demo1-r6-a   feat/d-demo1-r6-a   # Plan A 决策链 (W1, W2 已 commit；W3-W5 待做)
.worktrees/d-demo1-r6-b   feat/d-demo1-r6-b   # Plan B 物理闭环 (W6-W7 待做；依赖 W3+W4 merge)
.worktrees/d-demo1-r6-c   feat/d-demo1-r6-c   # Plan C 评分看门狗 (W8 已 commit；W9 待做，依赖 W3 merge)
.worktrees/d-demo1-r6-d   feat/d-demo1-r6-d   # Plan D demo 拆除 (W10 待做，完全独立)
```

E2E 用主 worktree（路径 `/Users/marine/Code/MASS-L3-Tactical Layer`），等 A/B/C/D 全 merge 后跑。

---

## 5. 🔴 两个关键 Blocker（执行 W3+ 前必须处置）

### Blocker 1 — Phase 1 H-A 假设错位

**事实**：`src/l3_tdl_kernel/l3_msgs/msg/MissionState.msg` 真实 schema：
```
uint16 schema_version  # 120
builtin_interfaces/Time stamp
float64 water_depth_m
bool in_anchorage_zone
bool is_moored
float32 confidence
string rationale
```

它是**水深/锚泊上下文消息**，**不是** M3 FSM state（Init/Idle/TaskValidation/AwaitingRoute/Active）。

**因此**：Phase 1 实测 `/l3/m3/mission_state` Publisher count=0 **不能用来证明 M3 卡在 AwaitingRoute**。M3 FSM 状态实际通过 `/l3/m3/mission_goal` 与 `/l3/asdr/record` 间接暴露。

**影响**：
- 真正的 root cause 链可能不是 "M3 没 Active → M4 IvP infeasible" 这样
- **W3 Plan**（task_validity 子状态加到 MissionState.msg）的设计前提错了——MissionState.msg 不该承载 FSM state；若要发布子状态，需要：
  - 选项 A：扩展 mission_goal.msg
  - 选项 B：新建专门的 `M3FSMState.msg`/topic `/l3/m3/fsm_state`
  - 选项 C：放进 asdr 心跳的 mission_state 字段
- **W4 Plan**（M4 fallback snapshot）的根因假设也要复验——M4 IvP 真的 infeasible 吗？是不是因为别的（M2 没发 threats？M6 没发 rule？）

### Blocker 2 — sil-nodes 容器损坏

W2 implementer 执行 `docker compose build sil-nodes` 时，新镜像缺 `librmw_cyclonedds_cpp.so`，导致：
```
ERROR rcl: Error getting RMW implementation identifier
'librmw_cyclonedds_cpp.so' cannot open shared object file
```

容器状态：`mass-l3-tacticallayer-sil-nodes-1` Exited(1)。其他 3 个容器（foxglove-bridge, sil-orchestrator, martin-tile-server）仍正常运行。

**根因猜测**（待验证）：
- BuildKit cache 失效或 `ros:humble-ros-base` apt source 变了
- 修复方向：`docker pull ros:humble-ros-base` + `docker compose build sil-nodes --no-cache`，或在 `docker/sil_nodes.Dockerfile` 显式 `apt-get install ros-humble-rmw-cyclonedds-cpp`

**影响**：任何需要 ROS2 topic echo 来验证的 W 任务 + V1/V3 + e2e test 全部受阻。

---

## 6. 推荐起手序列（Phase 1.7 → 修容器 → 重新设计 W3/W4 → 续推）

新会话**不直接走 subagent-driven-development**，先做 1.7 + 修容器：

### Step 1.7-A: 修容器（≤30 min）
```bash
docker compose stop sil-nodes
docker pull ros:humble-ros-base
docker compose build sil-nodes --no-cache --pull
# 验证: docker compose up -d sil-nodes && sleep 30 && docker logs ...-sil-nodes-1 | grep "Stage 3"
```

如果重建失败，读 `docker/sil_nodes.Dockerfile` 看 RUN apt-get 块是否有 `ros-humble-rmw-cyclonedds-cpp` 显式安装。补 1 行：
```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-humble-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*
```

### Step 1.7-B: 重诊断 M3/M4 真实状态（≤30 min）
容器修好后，激活 imazu-01-ho 跑 60s，实测：

```bash
# 启动场景
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/cleanup -H "Content-Type: application/json"
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 30

# 真实 FSM state（M3）
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -c "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 5 ros2 topic echo /l3/m3/mission_goal --once"
docker logs mass-l3-tacticallayer-sil-nodes-1 2>&1 | grep "M3 FSM" | tail -10  # W2 加的日志

# M4 实际 rationale
docker exec ... bash -c "... ros2 topic echo /l3/m4/behavior_plan --once" | head -20

# M6 rule
docker exec ... bash -c "... ros2 topic echo /l3/m6/rule_assessment --once" | head -10

# M2 threats
docker exec ... bash -c "... ros2 topic echo /l3/m2/threat_state --once" | head -20
```

根据观测决定：
- 若 M3 FSM=Active 且 mission_goal 有正常 waypoints → M3 OK，问题在 M4 之后
- 若 M3 FSM 仍 AwaitingRoute → 真有 wiring 问题，W2 加的 [M3 FSM] log 应该显示原因
- 若 M4 rationale 已不再 "IvP infeasible" → W4 不需要 fallback 改造
- 若 M6 仍 applicable_rule='' → W5 还需要做

### Step 1.7-C: 根据 1.7-B 结果**改写 R6 spec/plan**（在新会话内）

- 若 W3 设计错位 → 改 `R6-DEMO1-full-stack-spec.md` §4.1 W3 + `R6-plan-A-decision-chain.md` Task 3
- 若 W4 假设无效 → 改 spec §4.1 W4 + plan Task 4
- 在新会话提交一个 `docs(demo1): R6 spec/plan revise after Phase 1.7 re-diagnosis` commit

### Step 2: 启动 subagent-driven-development 续做 W3+

完整修正后续 plan 后，进 `superpowers:subagent-driven-development` 跑 W3 → W4 → W5 → W6 → W7 → W9 → W10A → W10B-E → E2E。

---

## 7. 关键文档路径速查

| 用途 | 路径 |
|---|---|
| **Spec（权威）** | `docs/Design/Review/2026-05-27/R6-DEMO1-full-stack-spec.md` |
| Plan A (决策链 W1-W5) | `docs/Design/Review/2026-05-27/R6-plan-A-decision-chain.md` |
| Plan B (物理闭环 W6-W7) | `docs/Design/Review/2026-05-27/R6-plan-B-physical-loop.md` |
| Plan C (评分看门狗 W8-W9) | `docs/Design/Review/2026-05-27/R6-plan-C-scoring-watchdog.md` |
| Plan D (demo 拆除 W10) | `docs/Design/Review/2026-05-27/R6-plan-D-demo-cleanup.md` |
| Plan E2E (V1-V3 + 9 断言) | `docs/Design/Review/2026-05-27/R6-plan-E2E-validation.md` |
| **本 handoff** | `docs/Design/Review/2026-05-27/R6-handoff-resumption.md` |
| Demo-1 场景规约 | `docs/Design/SIL/Demo-1场景.md` |
| Phase 1 评审 | `docs/Design/SIL/DEMO-1-评审报告.md` |
| 实施前置评审 | `docs/Design/Review/2026-05-27/POST-IMPL-REVIEW-R2-to-R5.md` |
| 项目 CLAUDE.md | `CLAUDE.md` |
| 全局 CLAUDE.md | `~/.claude/CLAUDE.md` |

---

## 8. 项目硬约束（来自 CLAUDE.md）

- **Worktree**：`.worktrees/{branch-slug}/`，由 `superpowers:using-git-worktrees` 管理，不手动 mkdir/rm
- **分支**：`feat/d-demo1-r6-{a,b,c,d}`，合 main 后立即删
- **commit**：HEREDOC + `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>`，禁止 `--amend` / `--no-verify`
- **caveman 模式**：默认对话压缩 ~75%，代码块照旧完整
- **置信度标注**：网络/推断结论用 🟢🟡🔴⚫
- **NLM 路由**：调研触发器在全局 CLAUDE.md 已定，事实/版本走 `/nlm-research --depth fast`，选型走 `--depth deep`
- **资源入库默认行为**：`--add-sources` 默认开（不要 `--no-add-sources`）
- **Docker 构建**：`# syntax=docker/dockerfile:1.5` + `--mount=type=cache` 必须保留（CLAUDE.md §12）

---

## 9. 9 条验收断言（DEMO-1 通过 = 全 PASS）

| ID | 描述 | 阈值 |
|---|---|---|
| A-1 | §1 直航阶段无避碰 | `max(\|own_heading_rad\|) for t∈[0,200]` ≤ 0.087 rad (5°) |
| A-2 | §2 Rule 14 触发 | `applicable_rule == "Rule 14"` 在 t∈[180,320] 内出现 |
| A-3 | §3 右舷转向（防过避） | `max(own_heading_rad) for t∈[200,500]` 在 0.436 ~ 0.785 rad (25°~45°) |
| A-4 | §4 安全 CPA | `min(cpa_m)` ≥ 500 m |
| A-5 | §4 归航 | `\|own_heading_rad\| at t=650s` ≤ 0.087 rad (5°) |
| A-6 | §5 仿真自动 stop | `lifecycle_state == "inactive"` at wall-time of t=700s+10s |
| A-7 | §5 scoring 完整 | `kpis≠null AND scoring_dimensions≠null AND verdict in {pass,fail}` |
| A-8 | M3 ACTIVE 链路真打通 | `applicable_rule≠""` AND M4 plan.rationale 不含 "IvP infeasible" 在 §3 期间 |
| A-9 | demo 已下线 | `grep "demo" src/sil_orchestrator/main.py` 返回 0 行 |

---

## 10. 切到 subagent-driven-development 的起手 prompt（在新会话中用）

```
（在新会话第一条消息粘贴）

我要续接 R6 DEMO-1 全栈落地工作。请先 Read 这份 handoff：

docs/Design/Review/2026-05-27/R6-handoff-resumption.md

按其 §6 推荐起手序列：先做 Step 1.7-A 修容器、Step 1.7-B 重诊断 M3/M4
真实状态、Step 1.7-C 视情况改写 R6 spec/plan 的 W3/W4 章节，然后再进入
superpowers:subagent-driven-development 跑 W3 → W4 → W5 → W6 → W7 → W9 →
W10A → W10B-E → E2E。

W8/W1/W2 已 commit 完毕（见 §3 表格），分别在 worktree c/a/a。

每一步先把发现告诉我，必要时让我决定方向再动手。caveman 模式默认开。
```
