# Simulation-Check（屏 ②）完整设计规格

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-SPEC-SIL-SCREEN2-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-18 |
| 状态 | 设计基线（brainstorming 产出，待 writing-plans） |
| 上游 | Doc 2 §2.6 + Doc 3 §7 + gate_runner.py + selfcheck_routes.py |
| 范围 | SimulationCheck.tsx 重设计 + SSE stream endpoint + ops endpoints + 证据产物 |
| 方案 | **Approach A**：3-Pane + SSE 流式 + 强化 6-Gate + Evidence JSON 产物 |
| D-task | D1.3b.3（前端重写）+ D1.3b.3（后端 SSE + ops）|

---

## 0. 一句话定位

把当前 **单栏批量 probe**（10.1 KB，无诊断画布，无 Quick Fix）升级为 **三栏 SSE 流式诊断控制台**：左栏 Gate 进度管线逐条点亮、中栏随失败 Gate 自动切换可视化（SVG 拓扑 / Monaco Diff / 容器边界图）、右栏精准 Quick Fix + 上下文日志流；每道 Gate 产出结构化 JSON 证据文件，满足 CCS 白盒审查 + IEC 61508 SIL 2 证据链要求。

---

## 1. 业务范围与必要性

### 1.1 为什么需要屏 ②

分布式仿真环境（ROS2 + 多容器 + DDS + PTP 时钟）极易发生"静默失败"：

- M7 Safety Supervisor 意外与 M1-M6 共享 cgroup → Doer-Checker 物理隔离被破坏 → 后续 1000 次测试结果在 CCS 审查时全部作废
- `/sim_clock` 偏移 > 10 ms → Mid-MPC 决策延迟预算被虚报 → SIL 2 时序证明失效
- YAML SHA256 不一致 → 测试运行的场景与登记场景不同 → 证据链断裂

屏 ② 的**核心业务价值**：
1. **防止污染测试结果**：所有 6 道 Gate 通过才允许激活仿真引擎
2. **生产认证证据**：每道 Gate 写 `preflight/gate_N.json`，构成 CCS AIP 提交的证据包
3. **工程师闭环排障**：Quick Fix 动作 + 上下文日志，全程不离开浏览器

### 1.2 屏 ② 承载的完整业务

| 业务功能 | 当前状态 | 目标状态 |
|---|---|---|
| 6-Gate 运行时验证 | ✅ 批量 probe（gate_runner.py 实装） | ✅ 保留 + 增加 SSE 流式端点 |
| GO/NO-GO 判定 + 3s 倒数 | ✅ GoNoGoPanel | ✅ 保留，迁入三栏布局 |
| 流式渐进显示（SSE） | ❌ 无 | ✅ Gate 逐条点亮 |
| 三栏诊断控制台 | ❌ 单栏 | ✅ 左/中/右 三栏 |
| 上下文感知诊断画布 | ❌ 无 | ✅ SVG 拓扑 / Monaco Diff / 容器边界图 |
| 上下文过滤日志流 | ⚠️ LiveLogStream（全量） | ✅ 按失败 Gate 过滤节点 stderr |
| Quick Fix 动作 | ❌ 只有全局 Reconfigure | ✅ 定向重启/同步时钟/清缓存 |
| 证据产物（JSON per Gate） | ❌ 无 | ✅ runs/{id}/preflight/gate_N.json |
| Dev-mode Skip + ASDR 记录 | ✅ /selfcheck/skip | ✅ 保留，仅 ?dev=1 可见 |
| 键盘快捷键 | ⚠️ FooterHotkeyHints（R/D/Esc） | ✅ 完整实现 R=Re-run / Esc=Abort |

---

## 2. 操作流程（完整步骤）

```
步骤 1  用户在屏 ① 选中场景 → 点击 [RUN 🚀]
        → POST /api/v1/lifecycle/cleanup  （清理旧状态，idempotent）
        → POST /api/v1/lifecycle/configure { scenario_id }
        → navigate #/check/:scenarioId
        → SimulationCheck 组件挂载

步骤 2  组件 useEffect 自动启动 SSE 流
        → GET /api/v1/selfcheck/stream?scenario_id={id}
        → EventSource 建立连接（浏览器原生 API）
        → 左栏所有 Gate 显示 [PENDING] 灰色状态

步骤 3  SSE 流开始推送（每道 Gate 完成立即推送）
        Event: {gate_id:1, label:"System Readiness", passed:true/false, checks:[...], duration_ms:230}
        → 左栏 GATE 1 图标变 ✅/❌，右侧出现耗时
        → 若 GATE 1 FAIL：中栏切换到 ROS2 拓扑 SVG，右栏拉取异常日志 + Quick Fix 按钮
        → 若 GATE 1 PASS：继续等待 GATE 2 事件

        （GATE 2-6 同上，顺序流推）

        Final event: {type:"complete", all_clear:bool, go_no_go:"GO"|"NO-GO"}

步骤 4A  GO 路径（6/6 PASS）
        → 中栏出现 [ ALL GATES CLEAR — ENGAGING L3 KERNEL ] 绿色全幅遮罩
        → 3 秒倒数（GoNoGoPanel 现有逻辑）
        → POST /api/v1/lifecycle/activate
        → navigate #/monitor/:scenarioId

步骤 4B  NO-GO 路径（任意 Gate FAIL）
        → SSE 流在 FAIL Gate 后继续运行（后续 Gate 并行探测）
        → final event 到达后显示 NO-GO 面板
        → 工程师使用 Quick Fix 修复 → 点击 [ Re-run Checks ] → 回步骤 2

步骤 5  Abort 路径（任意阶段）
        → 用户点 [ ABORT ] 或按 Esc
        → EventSource.close()
        → POST /api/v1/lifecycle/cleanup
        → navigate #/scenario
```

---

## 3. 6-Gate 完整定义

### 3.1 Gate 规格表

| Gate | 名称 | 探针内容 | PASS 条件 | 时间限制 | 证据产物 |
|---|---|---|---|---|---|
| **1** | System Readiness | docker compose ps + ros2 node list + TCP :8765 + TCP :3000 + WS state | 所有 service healthy / SIL 节点可见 / :8765 + :3000 响应 | 15 s timeout | gate_1.json |
| **2** | Module Health (M1-M8) | M1-M8 modulePulse（Phase 1 硬编码 GREEN）+ M7 pgrep/ps 独立性 | 8/8 GREEN；latency_ms < 50；drops == 0；M7 非 component_container | 10 s timeout | gate_2.json |
| **3** | Scenario Integrity | SHA256(yaml_content) vs stored_hash + YAML.safe_load + expected_outcome 字段 | hash 一致 + YAML 解析无错 + expected_outcome 存在 | 3 s timeout | gate_3.json |
| **4** | ODD-Scenario Alignment | metadata.odd_cell.domain 枚举校验 + visibility_nm / sea_state_beaufort / max_wind_kn 范围 | domain ∈ valid_set + 所有数值在 bounds 内（无 odd_cell → Phase 1 graceful PASS） | 3 s timeout | gate_4.json |
| **5** | Time Base + Evidence Chain | chronyc offset < 10 ms + /clock topic（Phase 1 stub）+ pgrep rosbag2 + ASDR 目录写权限 | 4/4 检查通过 | 8 s timeout | gate_5.json |
| **6** | Doer-Checker Independence [认证红线] | M7 PID 独立（pgrep + ps）+ M7 容器独立（docker inspect cgroup）+ import lint（m7 不引 m1-m6）+ /l3/checker_veto topic（Phase 1 stub）+ VETO 延迟 P99 < 50 ms | 5/5 检查通过 | 20 s timeout | gate_6.json |

### 3.2 证据产物 Schema（每 Gate 写入 `runs/{run_id}/preflight/gate_N.json`）

```json
{
  "gate_id": 1,
  "gate_name": "System Readiness",
  "timestamp_utc": "2026-05-18T14:32:05.123Z",
  "run_id": "rx-78-001",
  "scenario_id": "head_on",
  "passed": true,
  "checks": [
    {"item": "docker_compose", "status": "ok",   "detail": "5/5 healthy"},
    {"item": "ros2_discovery", "status": "ok",   "detail": "9 SIL nodes visible"},
    {"item": "foxglove_ws",    "status": "ok",   "detail": ":8765 listening"},
    {"item": "martin_tiles",   "status": "ok",   "detail": ":3000 listening"},
    {"item": "ws_frontend",    "status": "ok",   "detail": "frontend-reported connected"}
  ],
  "duration_ms": 230.4,
  "rationale": "all 5/5 sub-checks passed",
  "sil2_clause": "IEC 61508-3 §5.2 (Systematicity)",
  "hazid_scenario_ref": null
}
```

`hazid_scenario_ref` 在 Gate 3/4 填入场景对应的 HAZID 编号（如 `"RUN-001-FCB-HO-001"`），Gate 1/2/5/6 为 `null`。

---

## 4. 三栏布局规格

### 4.1 总体布局

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ TopChrome · SIMULATION-CHECK · RunID: rx-78-001 · [状态 Badge]                  │
├──────────────────────┬─────────────────────────────────┬────────────────────────┤
│ 左栏 (240px)         │ 中栏 (flex)                     │ 右栏 (300px)           │
│ GateSequencer        │ DiagnosticCanvas                │ ActionLogs             │
├──────────────────────┼─────────────────────────────────┼────────────────────────┤
│ [PENDING] GATE 1  —— │ 上下文可视化                    │ 上下文日志流           │
│ [RUNNING] GATE 2  ⟳  │ (随左侧焦点 Gate 切换)          │ + Quick Fix 按钮组     │
│ [PASS]    GATE 3  ✅  │                                 │ + Re-run Checks        │
│ ...                  │                                 │                        │
│ ─────────────────── │                                 │                        │
│ 总体：NO-GO 🔴       │                                 │                        │
└──────────────────────┴─────────────────────────────────┴────────────────────────┘
│ FooterHotkeyHints  R=Re-run / Esc=Abort / D=Dev-Skip(dev only)                  │
└──────────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 左栏：GateSequencer（240px 固定）

```typescript
// 每条 Gate 行
<GateRow>
  <StatusIcon />   // PENDING=灰圆 / RUNNING=spinner / PASS=绿✅ / FAIL=红❌
  <GateLabel />    // "GATE N · {name}"  （点击 → 中栏切换到该 Gate 诊断）
  <TimingBadge />  // "{Nms}" 完成后显示
</GateRow>

// 底部
<VerdictBanner go={allClear} />  // GO (绿) / NO-GO (红) / CHECKING (脉动动画)
```

- Gate 行点击后中栏切换到该 Gate 的诊断视图（不论 PASS/FAIL）
- SSE 推送到达时该行状态实时更新
- RUNNING 态：Gate 行有轻微脉动背景动画（CSS animation，无 JS 开销）

### 4.3 中栏：DiagnosticCanvas（flex，上下文感知）

根据当前**焦点 Gate**（自动跟随最后一个 FAIL Gate，或用户手动选中的 Gate）渲染不同可视化：

#### 视图 A：ROS2 Hub-and-Spoke 拓扑（Gate 1 / 2 焦点时）

纯 SVG 实现（无额外包）：

```
        ┌──────────────────────────────────────────────────┐
        │          DDS Bus (中心椭圆)                        │
        │  ●M1  ●M2  ●M3  ●M4  ●M5  ●M6  ●M8  ✗M7         │
        │  Orchestrator●  Foxglove●  Martin●               │
        │                                                   │
        │  实线 = 心跳正常                                   │
        │  红色虚线 = 断连/超时                              │
        │  节点颜色: GREEN=绿 / AMBER=黄 / RED=红 / UNKNOWN=灰│
        └──────────────────────────────────────────────────┘
```

数据来源：Gate 2 探针结果中的 `modulePulses[]` + Gate 1 的 docker/foxglove 状态。

#### 视图 B：Monaco DiffEditor（Gate 3 / 4 焦点时）

```typescript
import { DiffEditor } from '@monaco-editor/react';  // 已安装 v4.7.0

<DiffEditor
  original={storedYamlContent}   // 后端存储的 YAML（"期望"）
  modified={submittedYamlContent} // 用户提交的 YAML（"实际"）
  language="yaml"
  theme="vs-dark"
  options={{
    readOnly: true,
    renderSideBySide: true,
    minimap: { enabled: false },
    fontSize: 11,
    wordWrap: 'on',
  }}
/>
```

- Gate 3 FAIL 时：左侧显示 stored hash 对应 YAML，右侧显示 computed YAML，diff 标红不一致行
- Gate 4 FAIL 时：左侧显示 ODD schema 期望值，右侧显示实际 `odd_cell` 内容，越界字段高亮

#### 视图 C：Container Boundary Diagram（Gate 5 / 6 焦点时）

纯 SVG 实现：

```
  ┌─────────────────────────────┐    ┌──────────────────┐
  │   Doer Container (M1-M6)    │    │  Checker Container│
  │                             │    │      (M7 only)    │
  │  ●M1  ●M2  ●M3  ●M4  ●M5  ●M6 │    │     ●M7           │
  │                             │    │                  │
  └──────────────�┬──────────────┘    └────────┬─────────┘
                 │                            │
                 └──────── DDS /l3/checker_veto ────────┘
                                ↑ VETO 单向
```

- Gate 6 PASS：隔离线绿色实线，容器边框绿色
- Gate 6 FAIL：隔离线红色闪烁，`FATAL: 物理隔离被破坏` 标注
- Gate 5 FAIL：时钟图标 + UTC drift 数值突出显示

#### 视图 D：Idle / Checking 状态

无焦点（全部 PENDING 或 CHECKING 中）时，中栏显示：
- 检查进度环形动画（CSS）
- 当前正在运行的 Gate 名称
- 累计耗时计数器

### 4.4 右栏：ActionLogs（300px 固定）

#### 上半部：上下文日志流（ContextLogStream，~60% 高度）

- **来源**：复用现有 `LiveLogStream` 组件，该组件已轮询 `/api/v1/lifecycle/status` 中的日志字段（或 foxglove WS 日志话题）并渲染文本流。
- **过滤机制**：在 `LiveLogStream` 上增加 `nodeFilter?: string` prop，用于客户端字符串匹配过滤——不新增后端端点（Phase 1 实现成本最低）：

  ```typescript
  // LiveLogStream.tsx 新增 prop
  interface LiveLogStreamProps {
    nodeFilter?: string;  // 若非空，只显示包含该字符串的行
    maxLines?: number;    // 默认 200
  }
  ```

- 焦点 Gate → `nodeFilter` 映射：

  | 焦点 Gate | nodeFilter 值 |
  |---|---|
  | Gate 1（foxglove/docker 问题） | `"foxglove"` \| `"docker"` |
  | Gate 2（M7 unhealthy） | `"m7_safety"` |
  | Gate 3/4（场景/ODD）| `"scenario"` \| `"odd"` |
  | Gate 5（时钟）| `"clock"` \| `"chrony"` |
  | Gate 6（隔离）| `"m7"` \| `"cgroup"` |
  | 无失败 Gate | `undefined`（全量显示）|

- 日志行按 `[ERROR]`/`[WARN]`/`[INFO]` 着色（现有行为保留）

#### 下半部：Quick Fix 动作（~40% 高度）

按焦点 Gate 动态渲染对应的修复按钮：

| 焦点 Gate / 失败场景 | Quick Fix 按钮 | 后端调用 |
|---|---|---|
| Gate 1：docker 服务 unhealthy | `[↻ Restart All SIL Services]` | POST /api/v1/ops/restart_services |
| Gate 1：foxglove WS 无响应 | `[↻ Restart Foxglove Bridge]` | POST /api/v1/ops/restart_node?name=foxglove-bridge |
| Gate 2：M-N 节点 RED | `[↻ Restart M{N} Container]` | POST /api/v1/ops/restart_node?name=m{n}_* |
| Gate 3：hash mismatch | `[🗑 Clear Hash Cache]` | POST /api/v1/ops/clear_hash_cache |
| Gate 4：ODD parse fail | `[↻ Reload M1 ODD Config]` | POST /api/v1/ops/restart_node?name=m1_* |
| Gate 5：PTP drift | `[⚡ Force Sync PTP Clock]` | POST /api/v1/ops/sync_time |
| Gate 5：ASDR 无写权限 | `[🔧 Create ASDR Directory]` | POST /api/v1/ops/ensure_asdr_dir |
| Gate 6：M7 隔离失败 | `[↻ Restart M7 Isolated]` | POST /api/v1/ops/restart_node?name=m7_* |
| 任意 | `[🛑 Global Reconfigure]` | POST /api/v1/lifecycle/cleanup |

所有 Quick Fix 完成后自动触发右栏 Re-run Checks：

```
[ Re-run Checks ]   — 重启 SSE 流，回步骤 2
```

---

## 5. 双态流转

### 5.1 GO 路径

```
所有 6/6 Gates PASS
  → final SSE event {all_clear: true}
  → 中栏切换为全幅绿色遮罩
    文字："ALL GATES CLEAR — ENGAGING L3 KERNEL"
    副文字："Auto-activating in N seconds..."
  → 3 秒倒数（GoNoGoPanel 现有逻辑，迁入中栏）
  → POST /api/v1/lifecycle/activate
  → navigate #/monitor/:scenarioId
```

### 5.2 NO-GO 路径

```
任意 Gate FAIL
  → SSE 继续推送剩余 Gate（并行探测，不因失败中止）
  → final SSE event {all_clear: false, go_no_go: "NO-GO"}
  → 左栏 FAIL Gate 高亮（红色背景）
  → 中栏自动切换到 FAIL Gate 诊断视图
  → 右栏显示对应 Quick Fix + 过滤日志
  → 底部 [Re-run Checks] 按钮激活
  → [ABORT] 按钮始终可用 → cleanup → #/scenario
```

### 5.3 Dev-Mode Bypass

```
URL 带 ?dev=1 且 phase === 'failed' 时
  → 右栏底部显示：
    "DEV MODE: SKIP PREFLIGHT"
    [SKIP PREFLIGHT → MONITOR]（点击弹出 reason 输入）
    reason 非空 → POST /api/v1/selfcheck/skip {scenario_id, reason}
               → 写 ASDR preflight_skips.jsonl
               → navigate #/monitor/:scenarioId
    verdict 强制标记 warning_unverified_run
```

---

## 6. SSE 流式协议

### 6.1 后端端点（新增）

```
GET /api/v1/selfcheck/stream?scenario_id={id}
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
X-Accel-Buffering: no
```

**FastAPI 实现骨架**（`selfcheck_routes.py` 新增）：

```python
from fastapi.responses import StreamingResponse

@router.get("/stream")
async def probe_stream(scenario_id: str | None = None):
    sid = scenario_id or "unknown"
    data = store.get(sid) if sid != "unknown" else None
    runner = GateRunner(sid, data)

    async def event_generator():
        results: list[GateResult] = []
        for spec in runner.gates:
            t0 = time.monotonic()
            try:
                result = await spec.handler()
            except Exception as exc:
                result = GateResult(
                    gate_id=spec.gate_id, passed=False,
                    checks=[f"[fail] {exc}"], duration_ms=0.0,
                    rationale=f"crashed: {type(exc).__name__}",
                )
            result.duration_ms = (time.monotonic() - t0) * 1000
            results.append(result)

            # 写证据产物（staging 路径，activate 时归档到 runs/{run_id}/preflight/）
            _write_gate_evidence(sid, result)

            payload = json.dumps({
                "gate_id": result.gate_id,
                "label": runner._gate_label_for(result.gate_id),
                "passed": result.passed,
                "checks": result.checks,
                "duration_ms": round(result.duration_ms, 1),
                "rationale": result.rationale,
            })
            yield f"data: {payload}\n\n"

        all_pass = all(r.passed for r in results)
        yield f'data: {json.dumps({"type":"complete","all_clear":all_pass,"go_no_go":"GO" if all_pass else "NO-GO"})}\n\n'

    return StreamingResponse(event_generator(), media_type="text/event-stream",
        headers={"Cache-Control":"no-cache","X-Accel-Buffering":"no"})
```

注：`_write_gate_evidence` 写 `runs/{run_id}/preflight/gate_{N}.json`，`run_id` 从 scenario_id derive 或从 RUN_DIR 当前活跃 run_id 读取。

### 6.2 前端 Hook

```typescript
// hooks/useGateStream.ts
export function useGateStream(scenarioId: string | null, enabled: boolean) {
  const [gates, setGates] = useState<GateCheckResult[]>([]);
  const [verdict, setVerdict] = useState<'GO' | 'NO-GO' | null>(null);
  const [streaming, setStreaming] = useState(false);

  const start = useCallback(() => {
    if (!scenarioId) return;
    setGates([]);
    setVerdict(null);
    setStreaming(true);

    const es = new EventSource(
      `/api/v1/selfcheck/stream?scenario_id=${encodeURIComponent(scenarioId)}`
    );

    es.onmessage = (e) => {
      const data = JSON.parse(e.data);
      if (data.type === 'complete') {
        setVerdict(data.go_no_go);
        setStreaming(false);
        es.close();
      } else {
        setGates((prev) => [...prev, data as GateCheckResult]);
      }
    };

    es.onerror = () => {
      setStreaming(false);
      es.close();
    };

    return () => es.close();
  }, [scenarioId]);

  return { gates, verdict, streaming, start };
}
```

### 6.3 向后兼容

`POST /api/v1/selfcheck/probe` 批量端点保留，供：
- 自动化测试（pytest integration test）直接调用
- 不支持 EventSource 的测试客户端

---

## 7. 后端新增端点（GAP-NEW-002 实现规格）

### 7.1 ops 端点清单

| Method | Path | 参数 | 行为 | 最大耗时 |
|---|---|---|---|---|
| POST | `/api/v1/ops/restart_node` | `?name=<pattern>` | `docker restart $(docker ps -q --filter name=<pattern>)` | 15 s |
| POST | `/api/v1/ops/restart_services` | — | `docker compose restart` | 30 s |
| POST | `/api/v1/ops/sync_time` | — | `chronyc makestep` | 5 s |
| POST | `/api/v1/ops/clear_hash_cache` | `?scenario_id=<id>` | 删除 `scenarios/{id}/.hash_cache` | 2 s |
| POST | `/api/v1/ops/ensure_asdr_dir` | `?run_id=<id>` | `mkdir -p runs/{id}/preflight` + chmod | 2 s |

**安全约束**：
- `name` 参数白名单验证：只允许匹配 `^[a-zA-Z0-9_-]{1,64}$`，防止 shell injection
- 所有 ops 端点在 production build（`ENV=production`）时要求 `X-SIL-Dev-Token` header（Phase 2 加固）
- 操作日志写入 `runs/ops_audit.jsonl`（timestamp + action + params + result）

### 7.2 证据写入辅助

证据产物写入分两阶段：

- **staging 阶段**（SSE 流运行时）：写入 `SCENARIO_DIR/{scenario_id}/.preflight/gate_N.json`，场景目录已在 `configure` 时确保存在。
- **归档阶段**（`activate` 成功后）：`lifecycle_bridge.py` 的 `activate()` 调用 `_seed_run_dir()` 时，将 `.preflight/` 目录复制到 `RUN_DIR/{run_id}/preflight/`，形成不可变证据快照。

```python
# selfcheck_routes.py 新增
def _write_gate_evidence(scenario_id: str, result: GateResult) -> None:
    # 写 staging 路径：scenarios/{id}/.preflight/gate_N.json
    staging_dir = SCENARIO_DIR / scenario_id / ".preflight"
    staging_dir.mkdir(parents=True, exist_ok=True)
    out = {
        "gate_id": result.gate_id,
        "gate_name": _SIX_GATE_LABELS.get(result.gate_id, f"Gate {result.gate_id}"),
        "timestamp_utc": datetime.utcnow().isoformat() + "Z",
        "scenario_id": scenario_id,
        "passed": result.passed,
        "checks": result.checks,
        "duration_ms": round(result.duration_ms, 1),
        "rationale": result.rationale,
        "sil2_clause": _SIL2_CLAUSE_MAP.get(result.gate_id, ""),
        "hazid_scenario_ref": None,
    }
    (staging_dir / f"gate_{result.gate_id}.json").write_text(json.dumps(out, indent=2))
```

`_SIL2_CLAUSE_MAP` 常量：

```python
_SIL2_CLAUSE_MAP = {
    1: "IEC 61508-3 §5.2 Systematicity — test environment readiness",
    2: "IEC 61508-3 §7.4 Software module testing — component liveness",
    3: "IEC 61508-3 §7.2 Software V&V — artifact integrity",
    4: "IEC 61508-3 §7.2 Software V&V — ODD conformance",
    5: "IEC 61508-1 §8.2.9 Data recording — time base traceability",
    6: "IEC 61508-2 Clause 7.3 Independence — Doer-Checker physical separation",
}
```

---

## 8. 前端组件结构

### 8.1 新组件树（相对 `web/src/`）

```
screens/
  SimulationCheck.tsx            ← 重写（主协调器）
  shared/
    GateCard.tsx                 ← 保留（复用）
    GoNoGoPanel.tsx              ← 保留（迁入 GO overlay）
    LiveLogStream.tsx            ← 保留（右栏日志源）
    GateSequencer.tsx            ← 新增（左栏 Gate 进度列表）
    DiagnosticCanvas.tsx         ← 新增（中栏上下文可视化容器）
    Ros2TopologySvg.tsx          ← 新增（Gate 1/2 SVG 拓扑）
    ContainerBoundarySvg.tsx     ← 新增（Gate 5/6 容器隔离图）
    YamlDiffViewer.tsx           ← 新增（Gate 3/4 Monaco DiffEditor 包装）
    QuickFixPanel.tsx            ← 新增（右栏 Quick Fix 按钮组）
hooks/
  useGateStream.ts               ← 新增（SSE EventSource hook）
  useHotkeys.ts                  ← 保留（R/Esc/D 快捷键）
api/
  silApi.ts                      ← 扩展（ops endpoints RTK Query）
```

### 8.2 SimulationCheck.tsx 顶层状态

```typescript
// 核心状态
const { gates, verdict, streaming, start } = useGateStream(scenarioId, true);
const [focusedGateId, setFocusedGateId] = useState<number | null>(null);

// scenarioYaml 来源：RTK Query 缓存的场景详情（GET /api/v1/scenarios/{id}）
// useGetScenarioQuery 已在 silApi.ts 中定义，返回 { yaml_content, hash }
const { data: scenarioDetail } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
const scenarioYaml = scenarioDetail?.yaml_content ?? '';

// 焦点 Gate 自动跟随最后一个 FAIL Gate
useEffect(() => {
  const lastFail = [...gates].reverse().find(g => !g.passed);
  if (lastFail) setFocusedGateId(lastFail.gate_id);
}, [gates]);

// 三栏布局
return (
  <div style={{ display: 'grid', gridTemplateColumns: '240px 1fr 300px', height: '100%' }}>
    <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
                   onGateSelect={setFocusedGateId} verdict={verdict} />
    <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates} scenarioYaml={scenarioYaml} />
    <ActionLogs focusedGateId={focusedGateId} gates={gates}
                onRerun={start} onAbort={handleAbort} />
  </div>
);
```

---

## 9. 状态机

```
IDLE
  → [mount / Re-run] → CHECKING
      SSE streaming，Gate 逐条 PENDING → RUNNING → PASS/FAIL
  → [all PASS + final event] → PASSED
      GO overlay + 3s countdown
      → [countdown=0] → navigate #/monitor
      → [user click Proceed] → navigate #/monitor
  → [any FAIL + final event] → FAILED
      NO-GO，Quick Fix 可用
      → [Re-run Checks] → CHECKING
      → [ABORT / Esc] → cleanup → navigate #/scenario
      → [dev skip] → navigate #/monitor (warning_unverified_run)
  → [ABORT during CHECKING] → EventSource.close() → cleanup → navigate #/scenario
```

---

## 10. 键盘快捷键

| 快捷键 | 作用 | 生效范围 |
|---|---|---|
| `R` | Re-run Checks | FAILED 态 |
| `Esc` | Abort → 返回屏 ① | 任何态 |
| `D` | Dev Skip（仅 ?dev=1） | FAILED 态 + dev mode |
| `Enter` | Proceed Now（GO 态） | PASSED 态 |

---

## 11. DiagnosticCanvas 中栏视图路由逻辑

```typescript
function DiagnosticCanvas({ focusedGateId, gates, scenarioYaml }) {
  if (!focusedGateId || gates.length === 0) return <CheckingIdleView />;

  const gate = gates.find(g => g.gate_id === focusedGateId);

  if (focusedGateId === 1 || focusedGateId === 2) {
    return <Ros2TopologySvg gates={gates} />;
  }
  if (focusedGateId === 3 || focusedGateId === 4) {
    const storedYaml = /* from RTK Query cache */;
    return <YamlDiffViewer original={storedYaml} modified={scenarioYaml}
                           gate={gate} />;
  }
  if (focusedGateId === 5 || focusedGateId === 6) {
    return <ContainerBoundarySvg gate6Result={gates.find(g => g.gate_id === 6)}
                                  gate5Result={gates.find(g => g.gate_id === 5)} />;
  }
  return null;
}
```

---

## 12. Ros2TopologySvg 实现规格

纯 SVG，无额外依赖：

```
节点列表：M1 M2 M3 M4 M5 M6 M7 M8 Orchestrator Foxglove Martin
中心：DDS Bus（椭圆，宽 200px，高 60px）
布局：上弧 M1-M4，下弧 M5-M8，左侧 Orchestrator，右侧 Foxglove + Martin
连线：每节点 → DDS Bus 中心，2px stroke

颜色映射（modulePulse.state）：
  1（GREEN） → stroke: var(--c-stbd)  / fill: rgba(0,227,179,0.2)
  2（AMBER）  → stroke: var(--c-warn) / fill: rgba(240,183,47,0.2)
  3（RED）    → stroke: var(--c-danger) / fill: rgba(248,81,73,0.2)
  UNKNOWN    → stroke: var(--txt-3) / fill: var(--bg-2)

失败节点：连线变为红色 stroke-dasharray="4 4"（虚线）+ 节点标 ❌

Gate 1 来源：docker/foxglove/martin 状态注入 Orchestrator/Foxglove/Martin 节点颜色
Gate 2 来源：modulePulses[] 注入 M1-M8 颜色
```

---

## 13. GAP 映射（本设计关闭的 GAP）

| GAP | 描述 | 关闭方式 |
|---|---|---|
| GAP-023 | Preflight.tsx 6-gate 前端 sequencer 重写 | SimulationCheck.tsx 三栏重写 + useGateStream |
| GAP-025 | SKIP 按钮在 production 移除 | ?dev=1 判断 + IS_DEV 变量 |
| GAP-NEW-002（前端）| 三栏排障诊断 UI | DiagnosticCanvas + QuickFixPanel |
| GAP-NEW-002（后端）| /ops/* 控制端点 | 5 个 ops 端点实现 |
| GAP-005 部分 | selfcheck_routes SSE 端点 | GET /api/v1/selfcheck/stream |

**延后至 D1.6 / D2.5 的 GAP（本设计不覆盖）：**
- Gate 7（CM 完整性：git hash + docker digest）→ D1.6
- Gate 8（工具鉴定检查表）→ D2.5 SIL 集成
- Gate 2 真实 ROS2 topic 订阅（Phase 1 仍为硬编码）→ D2.1 M1 实装后联动
- Gate 5 真实 /sim_clock 话题检测 → D1.3a ROS2 节点实装后联动

---

## 14. 实施边界（D1.3b.3 范围）

### In Scope

- [ ] `SimulationCheck.tsx` 三栏布局重写（主协调器）
- [ ] `GateSequencer.tsx` 组件（左栏 SSE 渐进进度）
- [ ] `DiagnosticCanvas.tsx` 容器 + 路由逻辑
- [ ] `Ros2TopologySvg.tsx`（纯 SVG，Gate 1/2）
- [ ] `YamlDiffViewer.tsx`（Monaco DiffEditor 包装，Gate 3/4）
- [ ] `ContainerBoundarySvg.tsx`（纯 SVG，Gate 5/6）
- [ ] `QuickFixPanel.tsx`（Quick Fix 按钮组）
- [ ] `useGateStream.ts` hook（EventSource）
- [ ] `silApi.ts` 扩展（ops endpoints RTK Query mutations）
- [ ] `GET /api/v1/selfcheck/stream` SSE 端点（selfcheck_routes.py）
- [ ] `_write_gate_evidence()` 证据产物写入
- [ ] `POST /api/v1/ops/{restart_node, restart_services, sync_time, clear_hash_cache, ensure_asdr_dir}` 端点（新增 ops_routes.py）
- [ ] `runs/{run_id}/preflight/gate_N.json` 证据产物 schema

### Out of Scope（本设计不包含）

- Gate 7/8/9/10（IEC 61508 SIL 2 扩展门控）→ D1.6 / D2.5
- Gate 2 真实 modulePulse ROS2 topic 订阅 → D2.1
- Gate 5 /sim_clock 真实话题检测 → D1.3a
- Gate 4 M1 ODD cross-check → D2.1
- ops 端点 production security token → Phase 2

---

## 15. 验收条件（Karpathy §4 本地化）

| 验收项 | 验证方法 |
|---|---|
| 三栏布局渲染正确 | 浏览器截图 + playwright 视觉快照 |
| SSE 流式渐进显示 | Network tab 看 event-stream，Gate 逐条出现 |
| GO 路径：3s 自动跳屏③ | playwright 端到端（mock GO response） |
| NO-GO 路径：Quick Fix 按钮出现 | playwright 端到端（mock FAIL Gate 6） |
| DiagnosticCanvas 视图切换 | 点击不同 Gate → 中栏内容变化（playwright） |
| 证据产物写入 | 运行后检查 `runs/current/preflight/gate_*.json` 存在且 JSON 合法 |
| Dev skip ASDR 记录 | GET `runs/preflight_skips.jsonl` 包含 warning_unverified_run |
| ops restart_node 白名单拒绝 | `POST /ops/restart_node?name=; rm -rf` → 422 |
| 键盘 R/Esc 快捷键 | playwright keyboard events |

---

## 16. 调研来源与置信度

| 来源 | 内容 | 置信度 |
|---|---|---|
| FastAPI 官方文档 (fastapi.tiangolo.com) | StreamingResponse + text/event-stream 模式 | 🟢 |
| gate_runner.py + selfcheck_routes.py 代码读 | 现有 6-gate 实现确认 asyncio 兼容 | 🟢 |
| @monaco-editor/react v4.7.0 package.json 确认 | DiffEditor 可用，无新包 | 🟢 |
| IEC 61508 Part 3 §5.2 Systematicity | SIL 2 需 CM 完整性 + 工具鉴定 | 🟡（标准付费文献，通过 TÜV 二手引用） |
| CCS 《智能船舶规范》(2024) | 白盒可读性要求 + 2-run 认证锁 | 🟡（2024 规范较新，CCS 官网确认存在）|
| ROS2 Humble EventSource / DDS | asyncio create_subprocess_exec 兼容性 | 🟢（代码已实装确认）|

---

## 17. 修订记录

| 版本 | 日期 | 改动 |
|---|---|---|
| v1.0 | 2026-05-18 | 初始版本，brainstorming 产出。方案 A 三栏 + SSE + 强化 6-Gate + Evidence JSON。 |

---

*Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>*
