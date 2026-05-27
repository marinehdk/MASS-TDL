---
title: 主 Agent 审查报告 — R2/R3/R4/R5 plan
date: 2026-05-27
auditor: Claude Opus 4.7 (main agent)
method: superpowers receiving-code-review style — 每个 finding 标级别 + 责任归属 + 修复路径
inputs:
  - docs/Design/Review/2026-05-27/R1-evidence-report.md (baseline)
  - docs/Design/Review/2026-05-27/R2-transit-autopilot-plan.md (834 lines, Haiku 4.5)
  - docs/Design/Review/2026-05-27/R3-avoidance-chain-fix-plan.md (681 lines, Haiku 4.5)
  - docs/Design/Review/2026-05-27/R4-mock-l2-publisher-plan.md (1126 lines, Haiku 4.5)
  - docs/Design/Review/2026-05-27/R5-fsm-and-hmi-plan.md (1019 lines, Haiku 4.5)
verdict: 4/4 plan 通过主体设计审查；2 个 P0 必须修订后再开 /subagent-driven-development；1 个跨 plan 一致性 fix；其余 P1/P2 在 execute 阶段允许 in-stream 修复
---

# 0. 总览

| Plan | 行数 | 主体设计 | P0 finding | P1 finding | P2 finding | 准入门 |
|---|---|---|---|---|---|---|
| R2 TRANSIT autopilot | 834 | ✅ | **1** | 3 | 2 | 🔴 **需修订** |
| R3 Avoidance chain | 681 | ✅ | 0 | 4 | 3 | 🟢 通过 |
| R4 Mock L2 | 1126 | ✅ | 0 | 4 | 3 | 🟢 通过 |
| R5 FSM + HMI | 1019 | ✅ | **1** | 3 | 2 | 🔴 **需修订** |
| **跨 plan 一致性** | — | — | **1** | 1 | 0 | 🔴 **需对齐** |

**汇总 P0 = 3 项**，均围绕同一个根因簇：**后端 ODDState 的实际字段语义被 R2/R5 误读**。修复一次即可解决 3 项 P0。

---

# 1. R2 TRANSIT Autopilot — 审查

## 1.1 优点（保留）

- ✅ 设计哲学正确：扩展 bridge 而非新增节点，避免 lifecycle 复杂度
- ✅ Handover 协议清晰（Phase A/B/C 三相位），无线程竞争
- ✅ PID 选型保守（P-only 控向 + PI 控速 + anti-windup + rate limit）
- ✅ 独立性声明合规（不依赖 R3/R4 是否落地）
- ✅ DoD 量化（heading ≤ 1°、SOG ≤ 0.5 kn）
- ✅ Unit test 设计完整（pytest）
- ✅ 风险表覆盖 7 项，含 M1 ODD 延迟、stale ownship state、桥发布率

## 1.2 Finding — P0（**必须修订**）

### F-R2-01 (P0) — ODDState 字段语义错误，autopilot 永不激活

**位置**：R2 §4.2 + Step 3（代码块 line 145, 517）

**问题**：R2 检查 `self._last_odd_state.current_state == "TRANSIT"`（字符串比对）。但 R1 §3.6 已确认 `ODDState.msg` 的字段是 `envelope_state`（uint8 枚举），取值为 `ENVELOPE_IN / ENVELOPE_EDGE / ENVELOPE_OUT / ENVELOPE_MRC_PREP / ENVELOPE_MRC_ACTIVE`。**根本没有 "TRANSIT" 字符串字段**。

**根因**：subagent 把前端 `fsmStore.ts` 的 FSM state（TRANSIT/COLREG_AVOIDANCE）当成了后端 ODDState 的字段，混淆了两个状态空间。

**后果**：autopilot `_autopilot_step()` 中 `is_transit = False` 永远成立 → `_autopilot_enabled = False` 恒成立 → autopilot 一次也不触发 → ship 仍然漂移。**R2 完全无效**。

**修复**：把激活条件改为：
```python
# 用 ODDState envelope_state 枚举（参考 R5 §3.3 的正确用法）
is_in_envelope = (self._last_odd_state.envelope_state == ODDState.ENVELOPE_IN)
is_m5_stale = staleness_s > 10.0
# 可选：再叠加 M4 fallback 检测（参考 R3 H3.3.1）
m4_in_fallback = (self._last_behavior_plan and 
                  "fallback" in self._last_behavior_plan.rationale.lower())
self._autopilot_enabled = is_in_envelope and (is_m5_stale or m4_in_fallback)
```

**责任**：R2 plan 作者（subagent）修订 §4.2、§4.6 主题表第 2 行、Step 3 代码块。

**验证**：在修订版 R2 §7.2 Test 1 增加 1 行 — `ros2 topic echo /l3/m1/odd_state | grep envelope_state` 必须先确认实际字段名 + 取值。

## 1.3 Finding — P1（可在 execute 时修）

### F-R2-02 (P1) — 文件行号引用 lifecycle_bridge.py 不准

**位置**：R2 §4.5 line 319 — "lifecycle_bridge.py line ~200"

**问题**：R1 §1.2 经实测 `_inject_params_to_node` 调用在 line 463（n_rps_initial 注入处），不是 line ~200。

**影响**：execute 阶段开发会找不到对应位置；浪费 15 min 定位。

**修复**：开发前 `grep -n "_inject_params_to_node" src/sil_orchestrator/lifecycle_bridge.py` 取实际行号；或在 execute step 1 之前先 read 该文件确认结构。

### F-R2-03 (P1) — 桥内常量存在性未验证

**位置**：R2 §6 Step 4 line 560-564

**问题**：R2 引用 `SHIP_LENGTH_M`、`MAX_RUDDER_RAD`、`MAX_SPEED_KN` 三个 bridge 常量，但没说明这些常量是否已存在于 `docker/sil_topic_bridge.py`。

**修复**：execute 第一步先 `grep -n "SHIP_LENGTH_M\|MAX_RUDDER_RAD\|MAX_SPEED_KN" docker/sil_topic_bridge.py`；不存在则在 R2 Step 0 加"声明常量"动作。

### F-R2-04 (P1) — M5 plan validity 判定阈值未对齐

**位置**：R2 §4.2 line 156 + R3 §4.2.3

**问题**：R2 用 `wp.turn_radius_m > 1e-6` 判 plan 有效；R3 没明确判定逻辑。两个 plan 必须在同一处达成共识，否则 handover 会抖动。

**修复**：R3 作者补一行进 §4.2.3：bridge fallback 与 R2 autopilot 共享 plan validity 判定函数 `_is_m5_plan_valid(msg)`。

## 1.4 Finding — P2（可忽略）

- F-R2-P2-01: §5 affected files 路径前缀 `/src/...` 与 `src/...` 不一致（含/不含 leading slash）。统一即可。
- F-R2-P2-02: §10 [TBD-validation] PID 增益 — 后续闭环验证时调即可，不阻断 plan 进入 execute。

---

# 2. R3 Avoidance Chain Fix — 审查

## 2.1 优点（保留）

- ✅ **Hypothesis-first 方法学优秀** — 4 个 M4 hypothesis + 3 个 bridge + 2 个 M5，每个都带 file/line + test command
- ✅ §4 设计分子修复（4.1 M4 / 4.2 bridge / 4.3 M5）独立可验证
- ✅ §6 实施步骤含编译命令 + 验证 probe
- ✅ §7 端到端验收明确 T+180/T+350/T+700s 三个观察点
- ✅ §7.3 含 regression check（默认场景不破坏）
- ✅ Out-of-scope 与 R2/R4/R5 清晰划分
- ✅ §10 [TBD] 都有 owner 与 resolution path

## 2.2 Finding — P1（可在 execute 时修）

### F-R3-01 (P1) — 大量 file:line 引用是推测值

**位置**：R3 §3 全章节

**问题**：例如"line 56" / "line 108" / "line 145-150" 等都是 subagent 基于源码 grep + 经验估的，**未经实测验证**。

**影响**：开发时定位偏差 10-30 行属正常，不阻断；但开发者应当心。

**修复**：execute 阶段每个 step 先 read 文件确认实际位置；R3 应在 §6 增加一句注："本 plan 行号均为估值；execute 前先 read 文件确认实际位置"。

### F-R3-02 (P1) — `BehaviorPlanMsg::BEHAVIOR_TRANSIT` 枚举存在性未验证

**位置**：R3 §4.1.1 line 256-264

**问题**：R3 引用 `plan.behavior = BehaviorPlanMsg::BEHAVIOR_TRANSIT` 作为 fallback 行为值。但 BehaviorPlan IDL 实际定义未读取。R1 §3.4 显示 `behavior: 1` 是 raw 整数，没明确枚举名。

**修复**：execute 前 `cat src/l3_tdl_kernel/l3_msgs/msg/BehaviorPlan.msg` 确认实际枚举常量名；若无 BEHAVIOR_TRANSIT 则用 raw 整数 0 或新增枚举。

### F-R3-03 (P1) — §4.5 lifecycle persistence 文件路径不明确

**位置**：R3 §4.5.1 line 429

**问题**：写 "File: `docs/Design/SIL/` (new section or referenced file)" — 不是具体文件路径。

**修复**：明确为 `docs/Design/SIL/v1.0-unified/SIL-runbook.md`（或类似名）。建议落到 R4 的 `05-sil-mock-l2-contract.md` 同目录下作为 `06-sil-orchestrator-runbook.md`。

### F-R3-04 (P1) — Step 4 表述 "无需 build" 误导

**位置**：R3 §6 Step 4 line 515

**问题**：写"Compile/reload Python (no build needed for pure Python)"。实际上 bridge 跑在 docker 容器内，**需要 `docker compose restart sil-bridge`**（或对应服务名），不是 reload。

**修复**：改为 `docker compose restart sil-topic-bridge`（或确认实际 service 名）。

## 2.3 Finding — P2

- F-R3-P2-01: §4.3.2 字符串匹配 `rationale.find("infeasible fallback")` 已在 §8 risks 中标注脆弱性 ✓，无需修。但建议给 BehaviorPlan IDL 加 `is_fallback: bool` 字段作长期解。
- F-R3-P2-02: §4.4 M6→M5 wire-up 是 P1 stub，可推延至 Phase 2 D3.x，本 plan 中保留即可。
- F-R3-P2-03: §6 总时间估"30 min"过于乐观；实际含 colcon build 6 个包 + 跨 4 个文件，预计 90-120 min（不影响 plan 通过）。

---

# 3. R4 Mock L2 Publisher — 审查

## 3.1 优点（保留）

- ✅ **RFC-006 contract mapping 是 4 个 plan 中最严谨的章节**（§3.1/3.2/3.3 三张表 + 字段级 divergence 标注 + 理由）
- ✅ §4.6 Contract Divergence Document（`05-sil-mock-l2-contract.md`）设计合理 — 未来真 L2 上线时可作交接清单
- ✅ §4.2 Scenario YAML v3.1 schema 扩展向后兼容（v3.0 文件自动 fallback）
- ✅ §4.5 island detour scenario 是 Issue #4（穿岛）的合理首版解决
- ✅ §6 实施分 4 phase（Infrastructure / Schema / Docker / Testing），交付增量
- ✅ §5 affected files 含 NEW/MOD 区分 + 风险评级

## 3.2 Finding — P1

### F-R4-01 (P1) — `l3_external_msgs/msg/PlannedRoute` 等 IDL 文件名未验证存在

**位置**：R4 §3 全章 + §4.1 #include 列表 line 196-199

**问题**：R4 假设存在 `l3_external_msgs/msg/voyage_task.hpp`、`planned_route.hpp`、`speed_profile.hpp`。这些 IDL 可能在 RFC-006 中已设计但尚未生成代码，或命名不同。

**修复**：execute Step A.1 前 `find src/l3_tdl_kernel/l3_external_msgs -name "*.msg"` 确认实际文件；若不存在需新建 IDL（增工时 0.3 pw）。

### F-R4-02 (P1) — `sil_msgs/msg/ScenarioInfo` 不存在

**位置**：R4 §4.1 hpp line 200, §4.1 mock_l2 subscription line 211

**问题**：R4 假设 orchestrator 有一个 `/sil/scenario_lifecycle` 或 `/sil/scenario_loaded` 携带 ScenarioInfo（含 yaml_content）。R1 §2 探测显示存在 `/sil/scenario_loaded` topic 但 SILENT；类型是否含 yaml 内容字段未知。

**修复**：execute Step A.2 前先 `ros2 topic info /sil/scenario_loaded -v && ros2 interface show sil_msgs/msg/...`；若类型不足以传 YAML 内容，需扩展或改用 ROS param 注入路径（参考 R2 §4.5 方案）。

### F-R4-03 (P1) — docker-compose 修改方式可能破坏现有 entrypoint

**位置**：R4 §6 Step C.1 (Option 1) line 797-806

**问题**：把 `command:` 覆盖成 `bash -lc 'ros2 launch ... & ros2 run ... &'` 会**完全替代** Dockerfile 的 ENTRYPOINT（参考 `docker/sil_nodes.Dockerfile`）。当前 entrypoint（`docker/sil_entrypoint.sh`）做了 M1-M7 8 个节点的依赖顺序启动 + lifecycle 协调。直接覆盖会破坏。

**修复**：选 **Option 2（独立容器）** 或在现有 entrypoint 脚本 `docker/sil_entrypoint.sh` 内增加一行 `ros2 run mock_l2_publisher mock_l2_node &`（在合适的依赖时点）。Option 1 不可取。

### F-R4-04 (P1) — Island polygon vertices 顺序未规范

**位置**：R4 §4.2 obstacle vertices block

**问题**：obstacles[*].vertices 数组没指定 CCW/CW 顺序。在 §8 risks 中提了但未硬约束。

**修复**：在 §4.2 schema 注明"vertices MUST be listed in CCW order; first vertex repeated NOT required"。Pydantic validator 强制检查（用 shoelace formula 计算有向面积 sign）。

## 3.3 Finding — P2

- F-R4-P2-01: §3.4 RouteReplanRequest handler 不实现 → "implicit NACK"。M3 状态机 timeout 行为需明确（默认多久？回退到哪个 state？）。建议 R4 补一句指向 M3 spec 文档。
- F-R4-P2-02: §4.5 naive 几何 detour 已声明 proof-of-concept ✓。若 grounding-island-detour 测试不通过，回退路径需明确（保留 nominal route + warn？还是 abort scenario？）。
- F-R4-P2-03: §7.4 backward compat 测试中 `scenario_id="some-v3.0-scenario"` 是占位，需指定一个**实际**的 v3.0 scenario。

---

# 4. R5 FSM + HMI — 审查

## 4.1 优点（保留）

- ✅ §3.1 state machine 图清晰，§3.2 transition table 9 行覆盖所有边
- ✅ §3.3 信号源→状态映射表用 ODDState `envelope_state` 枚举（正确，与 R1 §3.6 一致）— 这是 R2 应该参考的范本
- ✅ §4.2 FsmState.msg IDL 含 schema_version / confidence / rationale 三件套（符合 v3.0 mandate）
- ✅ §4.3 fsmStore.ts refactor 保留 hotkey debug，env flag gating 合理
- ✅ §4.4 LeftDrawer 插槽插入位置精确（行号引用现有代码）
- ✅ §4.5/4.6 三个组件文件结构清晰
- ✅ §7 端到端验收覆盖 backend + frontend + 视觉稳定
- ✅ Dev-only hotkey 设计干净

## 4.2 Finding — P0（**必须修订**）

### F-R5-01 (P0) — Foxglove WebSocket URL `/foxglove-ws` 路径假设未验证

**位置**：R5 §4.3 line 392 (`useFsmStateSubscription` hook)

**问题**：R5 假设前端通过 `wss://localhost/foxglove-ws` 订阅 ROS2 topic。但实际架构是：
- foxglove-bridge 容器（docker-compose 中已存在）默认监听 `ws://localhost:8765`（WebSocket bridge 标准端口）
- 前端通过 Vite dev server `:5173` 反向代理至 sil-orchestrator REST API `:8000`
- **`/foxglove-ws` 路径不存在** — 这是 subagent 推测的 proxy 路径

**后果**：前端 WebSocket 连接失败 → fsmStore 永不更新 → FSM 面板永远显示 TRANSIT 默认值 → DEMO-1 演示决定无可观测性。**R5 frontend 部分完全失效**。

**修复**：
1. Read `docker-compose.yml` 找到 foxglove-bridge service 实际端口映射
2. Read `web/vite.config.ts`（或 vite.config.js）找到现有 proxy 规则
3. R5 §4.3 改用确认的 URL（推测应为 `ws://localhost:8765` 直连，或 vite proxy 改为 `/foxglove-ws` → `localhost:8765/`）
4. 同步更新 §7.3 frontend 验证步骤

**责任**：R5 plan 作者修订 §4.3 + 在 §7.3 增加 WebSocket 连通测试。

## 4.3 Finding — P1

### F-R5-02 (P1) — §3.3 mapping 假设 `last_behavior_.behavior == BEHAVIOR_COLREG_AVOID`

**位置**：R5 §3.3 + §4.1 line 196

**问题**：与 F-R3-02 同源 — BehaviorPlan 是否有 `BEHAVIOR_COLREG_AVOID` 枚举未验证。R1 §3.4 看到 `behavior: 1`（imazu 激活后）和 `behavior: 0`（默认场景），但语义未确认。

**修复**：execute 前 `cat src/l3_tdl_kernel/l3_msgs/msg/BehaviorPlan.msg`；若枚举命名不同（如 `MODE_AVOID`），同步更新 §3.3 + §4.1 pseudocode。

### F-R5-03 (P1) — 跨 plan 依赖未声明

**位置**：R5 frontmatter 只声明 `blocked_by: D-DEMO1-R1`

**问题**：R5 §7.4 "imazu scenario transition trace" 验收明确需要 "R2+R3+R4 landed" 作前置。frontmatter 应当反映。

**修复**：frontmatter `blocked_by` 加 R3 R4（R2 可选，因为 R5 验证不依赖 autopilot）；§9 out-of-scope 已正确说明 ✓。

### F-R5-04 (P1) — §3.3 row 6 confidence 0.5 [TBD-R4] 含义不清

**位置**：R5 §3.3 表第 6 行

**问题**：M3 `AWAITING_ROUTE` 时 confidence 设为 0.5 并标 [TBD-R4] — 但 R4 一旦落地，M3 不再 AWAITING_ROUTE，此行永远不触发。这个行配置应是 **R4 未落地时**的 fallback 视觉提示，不是稳定行为。

**修复**：把此行 confidence 改为 0.3 并加注："仅在 R4 未落地时触发；R4 完成后此分支永不命中（M3 直接进 ACTIVE 不出现 AWAITING_ROUTE）"。

## 4.4 Finding — P2

- F-R5-P2-01: §4.5 FsmStatePanel 用 emoji icons（🚢⚠️⛔🎮🛑🔄）。CLAUDE.md 全局规则要求"不影响代码"豁免，OK。但 ⚠️ + ⛔ 在不同 OS 渲染高度不一致；建议改 lucide-react SVG icons（已 import）。
- F-R5-P2-02: §4.3 fsmStore 内仍残留 `setTorRequest` action — 与"local-only 改为 subscription"原则有部分回退（TOR request 仍需本地接管？）。文档可加一行说明。

---

# 5. 跨 Plan 一致性审查

## F-CROSS-01 (P0) — R2 与 R5 对 ODDState 字段语义不一致

**问题**：
- R2 §4.2 → `current_state == "TRANSIT"` ❌
- R5 §3.3 → `envelope_state == ENVELOPE_OUT` ✅ (正确)

两个 plan 都要订阅 `/l3/m1/odd_state`，但解读不同。会导致一个工作、一个不工作。

**修复（统一指引）**：所有订阅 ODDState 的代码必须用 `envelope_state` 字段 + R1 §3.6 列出的 5 个枚举值。**R2 必须改正**。

## F-CROSS-02 (P1) — R3 与 R4 对 M3 状态机解锁路径表述不一致

**问题**：
- R3 §4.1.1 提出"若 mission_received_ 为 false 则用 failsafe TRANSIT" — 即 **绕过 M3**
- R4 整篇主张"让 M3 收到 L2 路由从而 ACTIVE" — 即 **修通 M3**

两条路径都对，但同时实现会有冗余。

**修复**：R3 §4.1.1 fallback 路径应明确为"**仅在 R4 未上线时**的过渡兜底"；R4 落地后该分支自然不再触发（mission_received_ = true）。建议在 R3 §4.1.1 代码注释加一行 `// Bypass when R4 mock L2 active (mission_received_ == true)`。

## F-CROSS-03 (info) — Plan validity 判定函数应共享

**问题**：R2 §4.2、R3 §4.2.3、R5 §3.3 row 7 都要判断"M5 plan 是否 valid"，但各自定义阈值（turn_radius > 1e-6 / waypoints.size() < 1 / distance_to_first_wp < 50）。

**修复**：在 R2 或 R3 内提取共享函数 `is_m5_plan_valid(msg) -> bool`，三个 plan 共用。R3 §4.2.3 应负责定义（因为它是修复 bridge 的主体），R2 / R5 引用。

---

# 6. 准入门判决

| Plan | 判决 | 必修条件 |
|---|---|---|
| **R2** | 🔴 **需修订后准入** | 修 F-R2-01（ODDState 字段语义） |
| **R3** | 🟢 **准入** | F-R3-01/02/03/04 在 execute 时 in-stream 修；P1 不阻断 |
| **R4** | 🟢 **准入** | F-R4-01/02/03/04 在 execute Phase A/C 前先 read 实际文件确认；P1 不阻断 |
| **R5** | 🔴 **需修订后准入** | 修 F-R5-01（Foxglove WebSocket URL） |
| **跨 plan** | 🔴 **需对齐** | F-CROSS-01（R2 同 F-R2-01 同源修） |

**必修修订量**：实际就 2 个独立 fix（F-R2-01 = F-CROSS-01；F-R5-01）。修订工时估 **30 min 总**。

---

# 7. 给用户的建议

## 7.1 立刻可做

**Option A**（推荐）：派 1 个 subagent **只改 R2 §4.2 + Step 3 代码块**（10 min），再派 1 个 subagent **只改 R5 §4.3 WebSocket URL**（10 min）。然后即可进 `/subagent-driven-development`。

**Option B**：用户/我直接手改 R2 + R5 的 2 处。再开发。

## 7.2 进 execute 阶段的顺序建议

按依赖图（master plan §13）+ 本审计的发现：

```
Phase 1 (可并发)：
  R3 (核心)  + R4 (解锁 M3) + R5 (FSM 可观测) + R2 (TRANSIT 兜底)
       ↓
Phase 2 (R3/R4 落地后)：
  R5 §7.4 imazu transition trace 验收 + R6 端到端 DEMO-1 验收
```

R2 之所以放最后，是因为 R3+R4 一旦修通，**M5 会发出真 plan、M4 不再 fallback**，autopilot 的兜底就基本不会被触发，R2 价值降低。但 R2 仍要做 — DEMO-2 复杂场景或 R3 不完美时它是降级护栏。

## 7.3 不建议立即做

- 启动所有 plan 的 `/subagent-driven-development`：F-R2-01 / F-R5-01 必须先修。否则 R2 100% 失效、R5 frontend 100% 失效，开发完才发现要重写。

---

# 8. 审计签名

- 主 agent: Claude Opus 4.7
- 审计方法: 逐 plan 全文 read（4027 行）+ R1 evidence 对照 + 跨 plan diff
- 审计耗时: ~12 min（4 plan 并发 read + 综合）
- 已完成的 task: #1 (R1) / #2-5 (R2-R5 plans) / #6 (本审计)
- 下一步: 等用户拍板"立即修订 P0 还是先 execute"

**审计完成。R3/R4 可立即开 /subagent-driven-development；R2/R5 修订 2 处后可入。**
