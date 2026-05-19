# Simulation-Check 屏② 三栏重设计 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 SimulationCheck 从单栏批量 probe 升级为三栏 SSE 流式诊断控制台（左栏 Gate 进度 → 中栏上下文可视化 → 右栏 Quick Fix + 日志流），并新增后端 SSE 端点 + 5 个 ops 端点 + 证据产物写入。

**Architecture:** 三栏 CSS Grid 布局（240px / 1fr / 300px），SSE EventSource 驱动 Gate 逐条点亮，纯 SVG 实现拓扑/容器可视化，Monaco DiffEditor 实现 YAML 对比，Zustand+RKT Query 管理状态。

**Tech Stack:** React 18 + TypeScript + Zustand + RTK Query + FastAPI + asyncio + Vitest + Playwright

**Spec 来源:** `docs/superpowers/specs/2026-05-18-simulation-check-screen2-design.md` (v1.0, brainstorming 产出)

---

## 文件结构映射

### 新增文件 (Create)

```
# Backend
src/sil_orchestrator/ops_routes.py              # 5 个 ops 端点（APIRouter）

# Frontend - Types
web/src/types/gateStream.ts                     # SSE Gate 事件类型 + ops 响应类型

# Frontend - Hooks
web/src/hooks/useGateStream.ts                  # SSE EventSource hook

# Frontend - Components（全部在 screens/shared/ 下）
web/src/screens/shared/GateSequencer.tsx         # 左栏 Gate 进度管线
web/src/screens/shared/DiagnosticCanvas.tsx      # 中栏上下文可视化容器
web/src/screens/shared/Ros2TopologySvg.tsx       # Gate 1/2 ROS2 拓扑 SVG
web/src/screens/shared/ContainerBoundarySvg.tsx  # Gate 5/6 容器隔离 SVG
web/src/screens/shared/YamlDiffViewer.tsx        # Gate 3/4 Monaco Diff 包装
web/src/screens/shared/QuickFixPanel.tsx         # 右栏 Quick Fix 按钮组
web/src/screens/shared/ActionLogs.tsx            # 右栏容器（日志 + Quick Fix）
```

### 修改文件 (Modify)

```
# Backend
src/sil_orchestrator/selfcheck_routes.py         # 新增 SSE stream 端点 + _write_gate_evidence
src/sil_orchestrator/main.py                     # 注册 ops_router
src/sil_orchestrator/lifecycle_bridge.py         # 新增 _copy_preflight_evidence 归档方法

# Frontend
web/src/screens/SimulationCheck.tsx               # 完全重写为三栏布局
web/src/screens/shared/LiveLogStream.tsx          # 新增 nodeFilter prop
web/src/api/silApi.ts                            # 扩展 ops endpoints + SSE 类型定义
web/src/hooks/useHotkeys.ts                       # 新增 R/Esc/D/Enter 处理
web/package.json                                  # 新增 test 脚本
```

---

## 依赖关系图

```
Wave 0: Task 0 (shared types) ── 定义所有接口契约，后续所有任务的基础
  │
  ├── Wave 1a Backend (3 tasks 并行): Task 1, 2, 3
  └── Wave 1b Frontend Foundation (4 tasks 并行): Task 4, 5, 6, 7
        │
        └── Wave 2 Components (7 tasks MAX PARALLEL): Task 8–14
              │  └─ GateSequencer, Ros2TopologySvg, YamlDiffViewer,
              │     ContainerBoundarySvg, QuickFixPanel, DiagnosticCanvas, ActionLogs
              │
              └── Wave 3 Integration (2 tasks): Task 15 → Task 16
                    │  └─ SimulationCheck.tsx 重写 → useHotkeys.ts 扩展
                    │
                    └── Wave 4 Tests (3 tasks 并行): Task 17, 18, 19
                          │  └─ Vitest 组件测试, Playwright E2E, pytest 后端
                          │
                          └── Wave 5 Final Verify: Task 20 (全链路验证)
```

**最大并行度**: Wave 2 可同时调度 7 个 subagent 构建独立组件。

---

## TODOs

### Task 0: 共享类型契约 — `gateStream.ts`

**Files:**
- Create: `web/src/types/gateStream.ts`

> 这是所有后续任务的接口契约基础。定义了前端 SSE 事件类型、ops 端点请求/响应类型、扩展的 GateCheckResult。所有 Wave 1 任务依赖此文件。

- [ ] **Step 1: 写入完整类型定义**

```typescript
// web/src/types/gateStream.ts
// === SSE Gate 事件 ===
export interface GateCheckItem {
  item: string;
  status: 'ok' | 'fail' | 'warn';
  detail: string;
}

export interface GateSSEEvent {
  gate_id: number;
  label: string;
  passed: boolean;
  checks: GateCheckItem[];
  duration_ms: number;
  rationale: string;
}

export interface SSECCompleteEvent {
  type: 'complete';
  all_clear: boolean;
  go_no_go: 'GO' | 'NO-GO';
}

// === Ops 端点类型 ===
export interface OpsResult {
  success: boolean;
  message: string;
  duration_ms: number;
}

export interface OpsRestartNodeRequest {
  name: string;
}

export interface OpsClearHashCacheRequest {
  scenario_id: string;
}

export interface OpsEnsureAsdrDirRequest {
  run_id: string;
}

// === useGateStream hook 返回类型 ===
export interface GateStreamState {
  gates: GateSSEEvent[];
  verdict: 'GO' | 'NO-GO' | null;
  streaming: boolean;
}
```

- [ ] **Step 2: 验证 TypeScript 编译**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors related to gateStream.ts

- [ ] **Step 3: Commit**

```bash
git add web/src/types/gateStream.ts
git commit -m "feat(screen2): add shared type contracts for SSE gate stream + ops endpoints"
```

---

### Task 1: 后端 SSE 流式端点 — `selfcheck_routes.py`

**Files:**
- Modify: `src/sil_orchestrator/selfcheck_routes.py`（新增 `GET /stream` 端点）
- Reference: `src/sil_orchestrator/gate_runner.py`（复用 GateRunner.run_all）
- Reference: `src/sil_orchestrator/config.py`（SCENARIO_DIR 常量）

- [ ] **Step 1: 在 `selfcheck_routes.py` 末尾新增 SSE 端点**

```python
# 在 selfcheck_routes.py 末尾追加

import time
import json
from fastapi.responses import StreamingResponse
from sil_orchestrator.gate_runner import GateRunner, GateResult

@router.get("/stream")
async def probe_stream(scenario_id: str | None = None):
    """SSE 流式 selfcheck — 每完成一道 Gate 立即推送事件"""
    sid = scenario_id or "unknown"
    # 从 ScenarioStore 获取场景数据（用于 Gate 3/4 的 YAML 校验）
    scenario_data = None
    if sid != "unknown":
        from sil_orchestrator.scenario_store import _store
        scenario_data = _store.get(sid)

    runner = GateRunner(sid, scenario_data)

    async def event_generator():
        results: list[GateResult] = []
        for spec in runner.gates:
            t0 = time.monotonic()
            try:
                result = await spec.handler()
            except Exception as exc:
                result = GateResult(
                    gate_id=spec.gate_id,
                    passed=False,
                    checks=[f"[fail] {type(exc).__name__}: {exc}"],
                    duration_ms=0.0,
                    rationale=f"handler crashed: {type(exc).__name__}",
                )
            result.duration_ms = round((time.monotonic() - t0) * 1000, 1)
            results.append(result)

            # 写 staging 证据
            _write_gate_evidence(sid, result)

            # 构建 SSE 事件 payload
            payload = json.dumps({
                "gate_id": result.gate_id,
                "label": runner._gate_label_for(result.gate_id),
                "passed": result.passed,
                "checks": [
                    {"item": c.split("]", 1)[0].lstrip("["), "status": c.split("]", 1)[0].lstrip("["), "detail": c.split("]", 1)[1].strip() if "]" in c else c}
                    if isinstance(c, str) else c
                    for c in result.checks
                ],
                "duration_ms": result.duration_ms,
                "rationale": result.rationale,
            })
            yield f"data: {payload}\n\n"

        all_pass = all(r.passed for r in results)
        final = json.dumps({
            "type": "complete",
            "all_clear": all_pass,
            "go_no_go": "GO" if all_pass else "NO-GO",
        })
        yield f"data: {final}\n\n"

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no",
        },
    )
```

- [ ] **Step 2: 验证端点可通过 curl 调用**

Run:
```bash
# 启动后端
curl -N "http://localhost:8000/api/v1/selfcheck/stream?scenario_id=test_demo"
```
Expected: 看到 `data: {"gate_id":1,...}` 逐条推送，最后 `data: {"type":"complete",...}`

- [ ] **Step 3: Commit**

```bash
git add src/sil_orchestrator/selfcheck_routes.py
git commit -m "feat(screen2): add SSE streaming endpoint GET /api/v1/selfcheck/stream"
```

---

### Task 2: 证据产物写入 + SIL2 条款映射 — `selfcheck_routes.py`

**Files:**
- Modify: `src/sil_orchestrator/selfcheck_routes.py`（新增 `_write_gate_evidence` 函数 + `_SIL2_CLAUSE_MAP` 常量）
- Reference: `src/sil_orchestrator/config.py`（SCENARIO_DIR）
- Reference: `src/sil_orchestrator/gate_runner.py`（GateResult dataclass）

- [ ] **Step 1: 在 `selfcheck_routes.py` 中新增证据写入函数和常量**

在文件顶部（router 定义之后、路由函数之前）添加：

```python
# === Gate 证据写入 ===
import datetime
from pathlib import Path
from sil_orchestrator.config import SCENARIO_DIR

_SIX_GATE_LABELS = {
    1: "System Readiness",
    2: "Module Health (M1-M8)",
    3: "Scenario Integrity",
    4: "ODD-Scenario Alignment",
    5: "Time Base + Evidence Chain",
    6: "Doer-Checker Independence",
}

_SIL2_CLAUSE_MAP = {
    1: "IEC 61508-3 §5.2 Systematicity — test environment readiness",
    2: "IEC 61508-3 §7.4 Software module testing — component liveness",
    3: "IEC 61508-3 §7.2 Software V&V — artifact integrity",
    4: "IEC 61508-3 §7.2 Software V&V — ODD conformance",
    5: "IEC 61508-1 §8.2.9 Data recording — time base traceability",
    6: "IEC 61508-2 Clause 7.3 Independence — Doer-Checker physical separation",
}

def _write_gate_evidence(scenario_id: str, result) -> None:
    """写 staging 证据产物：scenarios/{id}/.preflight/gate_N.json"""
    staging_dir = SCENARIO_DIR / scenario_id / ".preflight"
    staging_dir.mkdir(parents=True, exist_ok=True)

    # 规范化 checks 为 dict 列表
    checks_out = []
    for c in result.checks:
        if isinstance(c, dict):
            checks_out.append(c)
        elif isinstance(c, str):
            # 解析 "[ok] item detail" 格式
            if "] " in c:
                status_part, detail = c.split("] ", 1)
                status = status_part.lstrip("[")
                checks_out.append({"item": detail.split(" ", 1)[0] if " " in detail else detail, "status": status, "detail": detail})
            else:
                checks_out.append({"item": c, "status": "unknown", "detail": c})

    out = {
        "gate_id": result.gate_id,
        "gate_name": _SIX_GATE_LABELS.get(result.gate_id, f"Gate {result.gate_id}"),
        "timestamp_utc": datetime.datetime.utcnow().isoformat() + "Z",
        "scenario_id": scenario_id,
        "passed": result.passed,
        "checks": checks_out,
        "duration_ms": round(result.duration_ms, 1),
        "rationale": result.rationale,
        "sil2_clause": _SIL2_CLAUSE_MAP.get(result.gate_id, ""),
        "hazid_scenario_ref": None,
    }
    out_path = staging_dir / f"gate_{result.gate_id}.json"
    out_path.write_text(json.dumps(out, indent=2))
```

- [ ] **Step 2: 运行 SSE 端点后验证证据文件生成**

Run:
```bash
curl -N "http://localhost:8000/api/v1/selfcheck/stream?scenario_id=test_demo" > /dev/null
ls -la $(find . -path "*/test_demo/.preflight/gate_*.json" 2>/dev/null)
cat */test_demo/.preflight/gate_1.json | python3 -m json.tool
```
Expected: 6 个 gate_N.json 存在，JSON 合法，包含 `gate_id`, `passed`, `checks[]`, `sil2_clause` 字段

- [ ] **Step 3: Commit**

```bash
git add src/sil_orchestrator/selfcheck_routes.py
git commit -m "feat(screen2): add _write_gate_evidence + SIL2 clause mapping"
```

---

### Task 3: 后端 ops 端点 — `ops_routes.py` + main.py 注册

**Files:**
- Create: `src/sil_orchestrator/ops_routes.py`
- Modify: `src/sil_orchestrator/main.py`（注册 ops_router）
- Reference: `src/sil_orchestrator/selfcheck_routes.py`（路由注册模式）
- Reference: `src/sil_orchestrator/config.py`（SCENARIO_DIR, RUN_DIR）

- [ ] **Step 1: 创建 `ops_routes.py`**

```python
# src/sil_orchestrator/ops_routes.py
"""Ops 控制端点 — Quick Fix 动作后端"""
import re, time, asyncio, json
from pathlib import Path
from fastapi import APIRouter, HTTPException, Query
from sil_orchestrator.config import SCENARIO_DIR, RUN_DIR

router = APIRouter(prefix="/api/v1/ops")
_NAME_PATTERN = re.compile(r"^[a-zA-Z0-9_-]{1,64}$")
_AUDIT_LOG = RUN_DIR / "ops_audit.jsonl"

def _validate_name(name: str) -> str:
    if not _NAME_PATTERN.match(name):
        raise HTTPException(status_code=422, detail=f"Invalid name pattern: {name}")
    return name

def _audit(action: str, params: dict, success: bool) -> None:
    _AUDIT_LOG.parent.mkdir(parents=True, exist_ok=True)
    with open(_AUDIT_LOG, "a") as f:
        f.write(json.dumps({
            "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "action": action, "params": params, "success": success,
        }) + "\n")

async def _run(cmd: list[str], timeout: float) -> tuple[bool, str]:
    try:
        proc = await asyncio.create_subprocess_exec(*cmd,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=timeout)
        ok = proc.returncode == 0
        return ok, (stdout.decode()[:500] if ok else stderr.decode()[:500])
    except asyncio.TimeoutError:
        return False, f"timeout after {timeout}s"

@router.post("/restart_node")
async def restart_node(name: str = Query(...)):
    _validate_name(name)
    t0 = time.monotonic()
    ok, msg = await _run(["docker", "restart", f"$(docker ps -q --filter name={name})"], timeout=15.0)
    _audit("restart_node", {"name": name}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/restart_services")
async def restart_services():
    t0 = time.monotonic()
    ok, msg = await _run(["docker", "compose", "restart"], timeout=30.0)
    _audit("restart_services", {}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/sync_time")
async def sync_time():
    t0 = time.monotonic()
    ok, msg = await _run(["chronyc", "makestep"], timeout=5.0)
    _audit("sync_time", {}, ok)
    return {"success": ok, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/clear_hash_cache")
async def clear_hash_cache(scenario_id: str = Query(...)):
    t0 = time.monotonic()
    cache_path = SCENARIO_DIR / scenario_id / ".hash_cache"
    msg = f"hash_cache cleared for {scenario_id}" if cache_path.exists() and not cache_path.unlink() else f"no hash_cache for {scenario_id}" if not cache_path.exists() else f"cleared {scenario_id}"
    if cache_path.exists():
        cache_path.unlink()
    _audit("clear_hash_cache", {"scenario_id": scenario_id}, True)
    return {"success": True, "message": msg, "duration_ms": round((time.monotonic()-t0)*1000, 1)}

@router.post("/ensure_asdr_dir")
async def ensure_asdr_dir(run_id: str = Query(...)):
    t0 = time.monotonic()
    preflight_dir = RUN_DIR / run_id / "preflight"
    preflight_dir.mkdir(parents=True, exist_ok=True)
    preflight_dir.chmod(0o755)
    _audit("ensure_asdr_dir", {"run_id": run_id}, True)
    return {"success": True, "message": f"ensured: {preflight_dir}", "duration_ms": round((time.monotonic()-t0)*1000, 1)}
```

- [ ] **Step 2: 在 `main.py` 中注册 ops_router**

在 `main.py` 的 import 区域末尾添加：
```python
from sil_orchestrator.ops_routes import router as ops_router
```
在 `app.include_router()` 调用区域末尾添加：
```python
app.include_router(ops_router)
```

- [ ] **Step 3: 验证端点 + 安全约束**

Run:
```bash
curl -s -X POST "http://localhost:8000/api/v1/ops/restart_services" | python3 -m json.tool
curl -s -X POST "http://localhost:8000/api/v1/ops/sync_time" | python3 -m json.tool
curl -s -X POST "http://localhost:8000/api/v1/ops/clear_hash_cache?scenario_id=test" | python3 -m json.tool
curl -s -X POST "http://localhost:8000/api/v1/ops/ensure_asdr_dir?run_id=rx-001" | python3 -m json.tool
# 安全：非法 name 应返回 422
curl -s -o /dev/null -w "%{http_code}" -X POST "http://localhost:8000/api/v1/ops/restart_node?name=;%20rm%20-rf"
```
Expected: 前 4 个返回 `{"success":...}` JSON，最后一个返回 `422`

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/ops_routes.py src/sil_orchestrator/main.py
git commit -m "feat(screen2): add 5 ops endpoints with name-pattern security validation"
```

---

### Task 4: SSE Hook — `useGateStream.ts`

**Files:**
- Create: `web/src/hooks/useGateStream.ts`
- Reference: `web/src/types/gateStream.ts`（GateSSEEvent, SSECompleteEvent）

- [ ] **Step 1: 创建 `useGateStream.ts`**

```typescript
// web/src/hooks/useGateStream.ts
import { useState, useCallback, useEffect, useRef } from 'react';
import type { GateSSEEvent, SSECompleteEvent } from '../types/gateStream';

export interface UseGateStreamReturn {
  gates: GateSSEEvent[];
  verdict: 'GO' | 'NO-GO' | null;
  streaming: boolean;
  error: string | null;
  start: () => void;
  abort: () => void;
}

export function useGateStream(scenarioId: string | null, autoStart = true): UseGateStreamReturn {
  const [gates, setGates] = useState<GateSSEEvent[]>([]);
  const [verdict, setVerdict] = useState<'GO' | 'NO-GO' | null>(null);
  const [streaming, setStreaming] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const esRef = useRef<EventSource | null>(null);

  const cleanup = useCallback(() => {
    esRef.current?.close();
    esRef.current = null;
    setStreaming(false);
  }, []);

  const start = useCallback(() => {
    if (!scenarioId) return;
    cleanup();
    setGates([]); setVerdict(null); setError(null); setStreaming(true);
    const es = new EventSource(`/api/v1/selfcheck/stream?scenario_id=${encodeURIComponent(scenarioId)}`);
    esRef.current = es;
    es.onmessage = (e: MessageEvent) => {
      try {
        const data = JSON.parse(e.data);
        if (data.type === 'complete') {
          setVerdict((data as SSECompleteEvent).go_no_go);
          setStreaming(false);
          es.close(); esRef.current = null;
        } else {
          setGates(prev => [...prev, data as GateSSEEvent]);
        }
      } catch (err) { setError(`Parse error: ${(err as Error).message}`); }
    };
    es.onerror = () => { setError('SSE connection lost'); setStreaming(false); es.close(); esRef.current = null; };
  }, [scenarioId, cleanup]);

  const abort = useCallback(() => { cleanup(); setGates([]); setVerdict(null); setError(null); }, [cleanup]);

  useEffect(() => { if (autoStart && scenarioId) start(); return cleanup; }, [scenarioId, autoStart, start, cleanup]);
  return { gates, verdict, streaming, error, start, abort };
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors in useGateStream.ts

- [ ] **Step 3: Commit**

```bash
git add web/src/hooks/useGateStream.ts
git commit -m "feat(screen2): add useGateStream SSE hook with auto-start and abort"
```

---

### Task 5: RTK Query API 扩展 — `silApi.ts`

**Files:**
- Modify: `web/src/api/silApi.ts`（新增 ops endpoint mutations）
- Reference: `web/src/types/gateStream.ts`（OpsResult 类型）

- [ ] **Step 1: 在 `silApi.ts` 中追加 ops mutations**

> ⚠️ 先读取 `web/src/api/silApi.ts` 全文确认现有 `createApi` 结构和导出块位置。

在 `endpoints: (builder) => ({...})` 对象内追加：

```typescript
// --- Ops Quick Fix mutations ---
restartNode: builder.mutation<OpsResult, string>({
  query: (name) => ({ url: `/api/v1/ops/restart_node?name=${encodeURIComponent(name)}`, method: 'POST' }),
}),
restartServices: builder.mutation<OpsResult, void>({
  query: () => ({ url: '/api/v1/ops/restart_services', method: 'POST' }),
}),
syncTime: builder.mutation<OpsResult, void>({
  query: () => ({ url: '/api/v1/ops/sync_time', method: 'POST' }),
}),
clearHashCache: builder.mutation<OpsResult, string>({
  query: (scenarioId) => ({ url: `/api/v1/ops/clear_hash_cache?scenario_id=${encodeURIComponent(scenarioId)}`, method: 'POST' }),
}),
ensureAsdrDir: builder.mutation<OpsResult, string>({
  query: (runId) => ({ url: `/api/v1/ops/ensure_asdr_dir?run_id=${encodeURIComponent(runId)}`, method: 'POST' }),
}),
```

在文件顶部追加类型导入：
```typescript
import type { OpsResult } from '../types/gateStream';
```

在导出块追加：
```typescript
export const {
  useRestartNodeMutation, useRestartServicesMutation,
  useSyncTimeMutation, useClearHashCacheMutation, useEnsureAsdrDirMutation,
} = silApi;
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add web/src/api/silApi.ts
git commit -m "feat(screen2): add 5 ops endpoint RTK Query mutations to silApi"
```

---

### Task 6: LiveLogStream 改造 — nodeFilter prop

**Files:**
- Modify: `web/src/screens/shared/LiveLogStream.tsx`（新增 `nodeFilter` + `maxLines` props）

- [ ] **Step 1: 读取并修改 `LiveLogStream.tsx`**

> ⚠️ 先读取文件全文确认现有 props 接口名和数据来源变量名。

追加/修改 Props 接口：
```typescript
interface LiveLogStreamProps {
  // ... 保留现有 props ...
  nodeFilter?: string;   // 非空时只显示包含该字符串的日志行
  maxLines?: number;     // 最大显示行数，默认 200
}
```

在渲染逻辑中追加过滤：
```typescript
const displayLines = useMemo(() => {
  let lines = allLines;  // allLines 替换为实际变量名
  if (nodeFilter) {
    const lower = nodeFilter.toLowerCase();
    lines = lines.filter((l: string) => l.toLowerCase().includes(lower));
  }
  if (maxLines && lines.length > maxLines) lines = lines.slice(-maxLines);
  return lines;
}, [allLines, nodeFilter, maxLines]);
```

用 `displayLines` 替换原 `allLines` 作为渲染数据源。

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/LiveLogStream.tsx
git commit -m "feat(screen2): add nodeFilter + maxLines props to LiveLogStream"
```

---

### Task 7: 测试基础设施 — package.json scripts

**Files:**
- Modify: `web/package.json`（新增 test 脚本）
- Verify: `web/vite.config.ts`（确认 test 属性存在）

- [ ] **Step 1: 添加 test scripts**

在 `web/package.json` 的 `"scripts"` 块中添加：
```json
"test": "vitest run",
"test:watch": "vitest",
"test:coverage": "vitest run --coverage"
```

- [ ] **Step 2: 确认 Vitest 配置**

检查 `web/vite.config.ts` 包含 `test: { environment: 'jsdom', globals: true, setupFiles: ['./src/test-setup.ts'] }`（探索确认已存在）。

- [ ] **Step 3: 运行现有测试**

Run: `npm test`（在 web/ 目录下）
Expected: 现有 11 个测试全部 PASS

- [ ] **Step 4: Commit**

```bash
git add web/package.json
git commit -m "chore(screen2): add vitest test scripts to package.json"
```

---

### Task 8: 左栏组件 — `GateSequencer.tsx`

**Files:**
- Create: `web/src/screens/shared/GateSequencer.tsx`
- Reference: `web/src/types/gateStream.ts`（GateSSEEvent）
- Reference: `web/src/styles/tokens.css`（CSS 设计令牌）
- Reference: `web/src/screens/shared/GateCard.tsx`（现有 Gate 渲染模式）

- [ ] **Step 1: 创建 `GateSequencer.tsx`**

```typescript
// web/src/screens/shared/GateSequencer.tsx
import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';

const SIX_GATE_LABELS: Record<number, string> = {
  1: 'System Readiness', 2: 'Module Health', 3: 'Scenario Integrity',
  4: 'ODD-Scenario', 5: 'Time Base + Evidence', 6: 'Doer-Checker',
};

interface GateSequencerProps {
  gates: GateSSEEvent[];
  streaming: boolean;
  focusedGateId: number | null;
  onGateSelect: (gateId: number) => void;
  verdict: 'GO' | 'NO-GO' | null;
}

export function GateSequencer({ gates, streaming, focusedGateId, onGateSelect, verdict }: GateSequencerProps) {
  const gateMap = new Map(gates.map(g => [g.gate_id, g]));
  const verdictBg = verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)';
  const verdictLabel = verdict ?? (streaming ? 'CHECKING' : 'IDLE');

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-1)', borderRight: '1px solid var(--line-2)' }}>
      <div style={{ padding: '12px 16px', fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)', borderBottom: '1px solid var(--line-2)' }}>
        GATE PROGRESS
      </div>
      <div style={{ flex: 1, overflowY: 'auto', padding: '8px 0' }}>
        {[1, 2, 3, 4, 5, 6].map(gateId => {
          const result = gateMap.get(gateId);
          const isFocused = focusedGateId === gateId;
          const isPending = !result && streaming;
          const isRunning = isPending && (gates.length + 1 === gateId);
          const isPassed = result?.passed === true;
          const isFailed = result?.passed === false;

          let icon = '○';
          let iconColor = 'var(--txt-3)';
          if (isPassed) { icon = '✅'; iconColor = 'var(--c-stbd)'; }
          else if (isFailed) { icon = '❌'; iconColor = 'var(--c-danger)'; }
          else if (isRunning) { icon = '⟳'; iconColor = 'var(--c-warn)'; }

          return (
            <div key={gateId} onClick={() => onGateSelect(gateId)} style={{
              display: 'flex', alignItems: 'center', gap: 8, padding: '8px 16px',
              cursor: 'pointer', background: isFocused ? 'var(--bg-2)' : 'transparent',
              borderLeft: isFocused ? '3px solid var(--c-phos)' : '3px solid transparent',
              animation: isRunning ? 'pulse 1.5s ease-in-out infinite' : 'none',
            }}>
              <span style={{ fontSize: 14, width: 20, textAlign: 'center', color: iconColor }}>{icon}</span>
              <span style={{ flex: 1, fontSize: 11, fontFamily: 'var(--f-body)', color: 'var(--txt-1)' }}>
                GATE {gateId} · {SIX_GATE_LABELS[gateId]}
              </span>
              {result && <span style={{ fontSize: 10, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>{result.duration_ms}ms</span>}
            </div>
          );
        })}
      </div>
      <div style={{ padding: '12px 16px', borderTop: '1px solid var(--line-2)', textAlign: 'center' }}>
        <span style={{ display: 'inline-block', padding: '4px 12px', borderRadius: 4, background: verdictBg, color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12 }}>
          {verdictLabel}
        </span>
      </div>
    </div>
  );
}
```

> CSS 脉动动画需在 `web/src/styles/tokens.css` 追加：
> ```css
> @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.6; } }
> ```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/GateSequencer.tsx web/src/styles/tokens.css
git commit -m "feat(screen2): add GateSequencer left-pane component"
```

---

### Task 9: SVG 拓扑图 — `Ros2TopologySvg.tsx`

**Files:**
- Create: `web/src/screens/shared/Ros2TopologySvg.tsx`
- Reference: `web/src/types/gateStream.ts`（GateSSEEvent.checks[] 模块状态数据）
- Reference: `web/src/styles/tokens.css`（CSS 变量做 SVG stroke/fill 颜色）

- [ ] **Step 1: 创建 `Ros2TopologySvg.tsx`**

```typescript
// web/src/screens/shared/Ros2TopologySvg.tsx
import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';

interface Ros2TopologySvgProps { gates: GateSSEEvent[] }

// 11 个节点的布局坐标（SVG viewBox 0 0 600 400）
const NODES: { id: string; label: string; x: number; y: number; group: 'module' | 'infra' }[] = [
  { id: 'M1', label: 'M1', x: 120, y: 60, group: 'module' },
  { id: 'M2', label: 'M2', x: 220, y: 40, group: 'module' },
  { id: 'M3', label: 'M3', x: 340, y: 40, group: 'module' },
  { id: 'M4', label: 'M4', x: 460, y: 60, group: 'module' },
  { id: 'M5', label: 'M5', x: 120, y: 300, group: 'module' },
  { id: 'M6', label: 'M6', x: 220, y: 320, group: 'module' },
  { id: 'M7', label: 'M7', x: 340, y: 320, group: 'module' },
  { id: 'M8', label: 'M8', x: 460, y: 300, group: 'module' },
  { id: 'orch', label: 'Orch', x: 30, y: 180, group: 'infra' },
  { id: 'foxglove', label: 'Foxglove', x: 520, y: 140, group: 'infra' },
  { id: 'martin', label: 'Martin', x: 520, y: 240, group: 'infra' },
];

const STATUS_COLORS: Record<string, { stroke: string; fill: string }> = {
  ok:     { stroke: 'var(--c-stbd)',  fill: 'rgba(0,227,179,0.15)' },
  fail:   { stroke: 'var(--c-danger)', fill: 'rgba(248,81,73,0.15)' },
  warn:   { stroke: 'var(--c-warn)',   fill: 'rgba(240,183,47,0.15)' },
  unknown:{ stroke: 'var(--txt-3)',     fill: 'var(--bg-2)' },
};

export function Ros2TopologySvg({ gates }: Ros2TopologySvgProps) {
  // 从 Gate 2 checks 提取模块状态；若 Gate 2 未到达则从 Gate 1 docker 状态推断
  const gate2 = gates.find(g => g.gate_id === 2);
  const gate1 = gates.find(g => g.gate_id === 1);

  function nodeStatus(nodeId: string): string {
    if (gate2?.checks) {
      const check = gate2.checks.find(c => c.item?.toLowerCase().includes(nodeId.toLowerCase()));
      if (check) return check.status;
    }
    if (nodeId === 'orch' || nodeId === 'foxglove' || nodeId === 'martin') {
      if (gate1?.passed) return 'ok';
      return 'unknown';
    }
    return 'unknown';
  }

  const cx = 300, cy = 180, rx = 160, ry = 45;

  return (
    <svg viewBox="0 0 600 400" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      {/* DDS Bus 中心椭圆 */}
      <ellipse cx={cx} cy={cy} rx={rx} ry={ry} fill="none" stroke="var(--line-2)" strokeWidth={1.5} strokeDasharray="6 3" />
      <text x={cx} y={cy + 4} textAnchor="middle" fill="var(--txt-2)" fontSize={10} fontFamily="var(--f-mono)">DDS Bus</text>

      {/* 连线 + 节点 */}
      {NODES.map(n => {
        const status = nodeStatus(n.id);
        const colors = STATUS_COLORS[status] || STATUS_COLORS.unknown;
        const isFailed = status === 'fail';
        return (
          <g key={n.id}>
            <line x1={n.x} y1={n.y} x2={cx} y2={cy}
              stroke={isFailed ? 'var(--c-danger)' : 'var(--line-2)'}
              strokeWidth={1.5} strokeDasharray={isFailed ? '4 3' : undefined} />
            <circle cx={n.x} cy={n.y} r={isFailed ? 16 : 13} fill={colors.fill} stroke={colors.stroke} strokeWidth={2} />
            <text x={n.x} y={n.y + 4} textAnchor="middle" fill={colors.stroke} fontSize={9} fontFamily="var(--f-mono)">{n.label}</text>
            {isFailed && <text x={n.x + 16} y={n.y - 10} fill="var(--c-danger)" fontSize={14}>✗</text>}
          </g>
        );
      })}

      {/* 图例 */}
      <g transform="translate(10, 370)">
        {[{ status: 'ok', label: 'Healthy' }, { status: 'fail', label: 'Failed' }, { status: 'unknown', label: 'Unknown' }].map((s, i) => {
          const c = STATUS_COLORS[s.status];
          return (
            <g key={s.status} transform={`translate(${i * 110}, 0)`}>
              <circle cx={6} cy={6} r={5} fill={c.fill} stroke={c.stroke} strokeWidth={1.5} />
              <text x={16} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">{s.label}</text>
            </g>
          );
        })}
      </g>
    </svg>
  );
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/Ros2TopologySvg.tsx
git commit -m "feat(screen2): add Ros2TopologySvg hub-and-spoke component"
```

---

### Task 10: Monaco Diff 包装 — `YamlDiffViewer.tsx`

**Files:**
- Create: `web/src/screens/shared/YamlDiffViewer.tsx`
- Reference: package.json 确认 `@monaco-editor/react: ^4.7.0` 已安装

- [ ] **Step 1: 创建 `YamlDiffViewer.tsx`**

```typescript
// web/src/screens/shared/YamlDiffViewer.tsx
import React from 'react';
import { DiffEditor } from '@monaco-editor/react';
import type { GateSSEEvent } from '../../types/gateStream';

interface YamlDiffViewerProps {
  original: string;     // 存储的 YAML（"期望"）
  modified: string;     // 提交的 YAML（"实际"）
  gate: GateSSEEvent;
}

export function YamlDiffViewer({ original, modified, gate }: YamlDiffViewerProps) {
  const hasDiff = original !== modified;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-0)' }}>
      <div style={{ padding: '8px 16px', borderBottom: '1px solid var(--line-2)', display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)' }}>
          YAML Diff · Gate {gate.gate_id}
        </span>
        <span style={{
          fontSize: 10, fontFamily: 'var(--f-mono)', padding: '2px 8px', borderRadius: 3,
          background: gate.passed ? 'rgba(0,227,179,0.15)' : 'rgba(248,81,73,0.15)',
          color: gate.passed ? 'var(--c-stbd)' : 'var(--c-danger)',
        }}>
          {gate.passed ? 'MATCH' : 'MISMATCH'}
        </span>
      </div>
      <div style={{ flex: 1 }}>
        <DiffEditor
          original={original}
          modified={modified}
          language="yaml"
          theme="vs-dark"
          options={{
            readOnly: true,
            renderSideBySide: true,
            minimap: { enabled: false },
            fontSize: 11,
            wordWrap: 'on',
            lineNumbers: 'on',
            scrollBeyondLastLine: false,
          }}
        />
      </div>
      {!gate.passed && (
        <div style={{ padding: '8px 16px', background: 'rgba(248,81,73,0.1)', borderTop: '1px solid var(--c-danger)' }}>
          <span style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--c-danger)' }}>
            ⚠ {gate.rationale}
          </span>
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/YamlDiffViewer.tsx
git commit -m "feat(screen2): add YamlDiffViewer Monaco Diff wrapper component"
```

---

### Task 11: 容器隔离图 — `ContainerBoundarySvg.tsx`

**Files:**
- Create: `web/src/screens/shared/ContainerBoundarySvg.tsx`
- Reference: `web/src/types/gateStream.ts`（GateSSEEvent）
- Reference: `web/src/styles/tokens.css`（CSS 变量）

- [ ] **Step 1: 创建 `ContainerBoundarySvg.tsx`**

```typescript
// web/src/screens/shared/ContainerBoundarySvg.tsx
import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';

interface ContainerBoundarySvgProps {
  gate6Result: GateSSEEvent | undefined;
  gate5Result: GateSSEEvent | undefined;
}

const DOER_MODULES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6'];

export function ContainerBoundarySvg({ gate6Result, gate5Result }: ContainerBoundarySvgProps) {
  const isolationPassed = gate6Result?.passed === true;
  const timePassed = gate5Result?.passed === true;

  const borderColor = isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)';
  const borderAnim = isolationPassed ? undefined : 'flicker 0.8s ease-in-out infinite';

  return (
    <svg viewBox="0 0 700 350" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      {/* Doer Container */}
      <rect x={20} y={30} width={400} height={280} rx={8}
        fill="rgba(0,227,179,0.03)" stroke={borderColor} strokeWidth={2} style={{ animation: borderAnim }} />
      <text x={220} y={50} textAnchor="middle" fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-disp)">Doer Container (M1–M6)</text>

      {DOER_MODULES.map((m, i) => {
        const col = i % 3; const row = Math.floor(i / 3);
        const nx = 80 + col * 130, ny = 80 + row * 80;
        return (
          <g key={m}>
            <rect x={nx - 20} y={ny - 12} width={80} height={28} rx={4}
              fill="var(--bg-2)" stroke="var(--line-2)" strokeWidth={1} />
            <text x={nx + 20} y={ny + 6} textAnchor="middle" fill="var(--txt-1)" fontSize={10} fontFamily="var(--f-mono)">{m}</text>
          </g>
        );
      })}

      {/* Checker Container */}
      <rect x={470} y={30} width={180} height={120} rx={8}
        fill="rgba(248,81,73,0.03)" stroke={borderColor} strokeWidth={2} style={{ animation: borderAnim }} />
      <text x={560} y={50} textAnchor="middle" fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-disp)">Checker (M7)</text>
      <rect x={520} y={60} width={80} height={28} rx={4} fill="var(--bg-2)" stroke="var(--line-2)" strokeWidth={1} />
      <text x={560} y={78} textAnchor="middle" fill="var(--txt-1)" fontSize={10} fontFamily="var(--f-mono)">M7</text>

      {/* VETO 路径 */}
      <line x1={420} y1={120} x2={470} y2={90} stroke={isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)'}
        strokeWidth={2} markerEnd="url(#arrow)" />
      <defs>
        <marker id="arrow" viewBox="0 0 10 10" refX={9} refY={5} markerWidth={6} markerHeight={6} orient="auto">
          <path d="M0,0 L10,5 L0,10 Z" fill={isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)'} />
        </marker>
      </defs>
      <text x={445} y={115} textAnchor="middle" fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-mono)">VETO →</text>

      {/* Gate 5 时间漂移信息 */}
      {!timePassed && gate5Result && (
        <g transform="translate(470, 170)">
          <text x={0} y={0} fill="var(--c-warn)" fontSize={11} fontFamily="var(--f-body)">⏱ Clock Drift</text>
          <text x={0} y={16} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-mono)">{gate5Result.rationale}</text>
        </g>
      )}

      {/* Gate 6 隔离失败标注 */}
      {!isolationPassed && (
        <g transform="translate(470, 220)">
          <text x={0} y={0} fill="var(--c-danger)" fontSize={12} fontFamily="var(--f-disp)" fontWeight="bold">FATAL</text>
          <text x={0} y={16} fill="var(--c-danger)" fontSize={10} fontFamily="var(--f-body)">物理隔离被破坏</text>
        </g>
      )}

      {/* 图例 */}
      <g transform="translate(20, 330)">
        <rect x={0} y={0} width={12} height={12} rx={2} fill="none" stroke="var(--c-stbd)" strokeWidth={2} />
        <text x={18} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">隔离正常</text>
        <rect x={100} y={0} width={12} height={12} rx={2} fill="none" stroke="var(--c-danger)" strokeWidth={2} />
        <text x={118} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">隔离失败</text>
      </g>
    </svg>
  );
}
```

> CSS flicker 动画需在 `tokens.css` 追加：
> ```css
> @keyframes flicker { 0%,100% { opacity: 1; } 50% { opacity: 0.4; } }
> ```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/ContainerBoundarySvg.tsx web/src/styles/tokens.css
git commit -m "feat(screen2): add ContainerBoundarySvg doer-checker isolation diagram"
```

---

### Task 12: Quick Fix 面板 — `QuickFixPanel.tsx`

**Files:**
- Create: `web/src/screens/shared/QuickFixPanel.tsx`
- Reference: `web/src/api/silApi.ts`（ops mutations hooks）
- Reference: `web/src/types/gateStream.ts`（GateSSEEvent, OpsResult）

- [ ] **Step 1: 创建 `QuickFixPanel.tsx`**

```typescript
// web/src/screens/shared/QuickFixPanel.tsx
import React, { useState } from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import { useRestartServicesMutation, useRestartNodeMutation, useSyncTimeMutation, useClearHashCacheMutation, useEnsureAsdrDirMutation } from '../../api/silApi';

interface QuickFixPanelProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioId: string | null;
  runId: string | null;
  onFixApplied: () => void;
}

interface QuickFixAction {
  label: string;
  gateIds: number[];
  check: (gates: GateSSEEvent[]) => boolean;  // 是否需要显示此按钮
  action: () => void;
}

export function QuickFixPanel({ focusedGateId, gates, scenarioId, runId, onFixApplied }: QuickFixPanelProps) {
  const [restartServices] = useRestartServicesMutation();
  const [restartNode] = useRestartNodeMutation();
  const [syncTime] = useSyncTimeMutation();
  const [clearHashCache] = useClearHashCacheMutation();
  const [ensureAsdrDir] = useEnsureAsdrDirMutation();
  const [runningAction, setRunningAction] = useState<string | null>(null);
  const [lastResult, setLastResult] = useState<string | null>(null);

  const failedGateIds = gates.filter(g => !g.passed).map(g => g.gate_id);
  const focusedGate = gates.find(g => g.gate_id === focusedGateId);

  async function execute(label: string, fn: () => Promise<unknown>) {
    setRunningAction(label); setLastResult(null);
    try {
      const res = await fn();
      setLastResult(typeof res === 'object' && res !== null && 'data' in res
        ? JSON.stringify((res as any).data) : 'OK');
      onFixApplied();
    } catch (e) {
      setLastResult(`FAILED: ${(e as Error).message}`);
    } finally { setRunningAction(null); }
  }

  if (!focusedGateId || failedGateIds.length === 0) return null;

  return (
    <div style={{ padding: '12px 16px', borderTop: '1px solid var(--line-2)', background: 'var(--bg-1)' }}>
      <div style={{ fontFamily: 'var(--f-disp)', fontSize: 12, color: 'var(--txt-0)', marginBottom: 8 }}>QUICK FIX</div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>

        {focusedGateId === 1 && (
          <>
            <FixButton label="↻ Restart All SIL Services" running={runningAction}
              onClick={() => execute('Restart Services', () => restartServices())} />
            <FixButton label="↻ Restart Foxglove Bridge" running={runningAction}
              onClick={() => execute('Restart Foxglove', () => restartNode('foxglove-bridge'))} />
          </>
        )}

        {focusedGateId === 2 && focusedGate && (
          <FixButton label={`↻ Restart M${focusedGateId} Container`} running={runningAction}
            onClick={() => execute(`Restart M${focusedGateId}`, () => restartNode(`m${focusedGateId}_*`))} />
        )}

        {focusedGateId === 3 && scenarioId && (
          <FixButton label="🗑 Clear Hash Cache" running={runningAction}
            onClick={() => execute('Clear Cache', () => clearHashCache(scenarioId!))} />
        )}

        {focusedGateId === 4 && (
          <FixButton label="↻ Reload M1 ODD Config" running={runningAction}
            onClick={() => execute('Reload M1', () => restartNode('m1_*'))} />
        )}

        {focusedGateId === 5 && (
          <>
            <FixButton label="⚡ Force Sync PTP Clock" running={runningAction}
              onClick={() => execute('Sync Time', () => syncTime())} />
            <FixButton label="🔧 Create ASDR Directory" running={runningAction}
              onClick={() => execute('Ensure ASDR', () => ensureAsdrDir(runId ?? 'unknown'))} />
          </>
        )}

        {focusedGateId === 6 && (
          <FixButton label="↻ Restart M7 Isolated" running={runningAction}
            onClick={() => execute('Restart M7', () => restartNode('m7_*'))} />
        )}

        <div style={{ marginTop: 8, borderTop: '1px solid var(--line-2)', paddingTop: 8 }}>
          <FixButton label="🛑 Global Reconfigure" running={runningAction} variant="danger"
            onClick={() => execute('Global Reconfigure', () => restartServices())} />
        </div>

        {lastResult && (
          <div style={{ marginTop: 4, padding: '4px 8px', background: 'var(--bg-2)', borderRadius: 3, fontSize: 10, fontFamily: 'var(--f-mono)', color: 'var(--txt-2)' }}>
            {lastResult}
          </div>
        )}
      </div>
    </div>
  );
}

function FixButton({ label, running, onClick, variant }: {
  label: string; running: string | null; onClick: () => void; variant?: 'danger';
}) {
  const isActive = running === label;
  return (
    <button onClick={onClick} disabled={running !== null} style={{
      padding: '6px 10px', border: 'none', borderRadius: 4, cursor: running ? 'not-allowed' : 'pointer',
      background: variant === 'danger' ? 'rgba(248,81,73,0.15)' : 'var(--bg-2)',
      color: variant === 'danger' ? 'var(--c-danger)' : 'var(--txt-1)',
      fontFamily: 'var(--f-body)', fontSize: 11, textAlign: 'left', opacity: running ? 0.5 : 1,
    }}>
      {isActive ? '⟳ Working...' : label}
    </button>
  );
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors (确认 ops mutation hooks 在 silApi.ts 已导出)

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/QuickFixPanel.tsx
git commit -m "feat(screen2): add QuickFixPanel with gate-aware fix buttons"
```

---

### Task 13: 中栏容器 — `DiagnosticCanvas.tsx`

**Files:**
- Create: `web/src/screens/shared/DiagnosticCanvas.tsx`
- Reference: `web/src/screens/shared/Ros2TopologySvg.tsx`, `YamlDiffViewer.tsx`, `ContainerBoundarySvg.tsx`

- [ ] **Step 1: 创建 `DiagnosticCanvas.tsx`**

```typescript
// web/src/screens/shared/DiagnosticCanvas.tsx
import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import { Ros2TopologySvg } from './Ros2TopologySvg';
import { YamlDiffViewer } from './YamlDiffViewer';
import { ContainerBoundarySvg } from './ContainerBoundarySvg';

interface DiagnosticCanvasProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioYaml: string;
  storedYaml: string;       // 从 RTK Query 缓存的存储 YAML（期望值）
  verdict: 'GO' | 'NO-GO' | null;
  countdown: number;        // GO 路径倒数秒数，0 表示不倒数
}

export function DiagnosticCanvas({ focusedGateId, gates, scenarioYaml, storedYaml, verdict, countdown }: DiagnosticCanvasProps) {
  // GO overlay
  if (verdict === 'GO') {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'rgba(0,227,179,0.08)' }}>
        <div style={{ fontFamily: 'var(--f-disp)', fontSize: 22, color: 'var(--c-stbd)', marginBottom: 8 }}>
          ALL GATES CLEAR — ENGAGING L3 KERNEL
        </div>
        <div style={{ fontFamily: 'var(--f-body)', fontSize: 13, color: 'var(--txt-2)' }}>
          Auto-activating in {countdown} seconds...
        </div>
      </div>
    );
  }

  // Idle / Checking view
  if (!focusedGateId || gates.length === 0) {
    return (
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', background: 'var(--bg-0)' }}>
        <div style={{ width: 60, height: 60, borderRadius: '50%', border: '3px solid var(--line-2)', borderTopColor: 'var(--c-phos)', animation: 'spin 1s linear infinite' }} />
        <div style={{ marginTop: 16, fontFamily: 'var(--f-disp)', fontSize: 14, color: 'var(--txt-1)' }}>
          {gates.length > 0 ? `Checking Gate ${gates.length + 1}...` : 'Initializing...'}
        </div>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', marginTop: 4 }}>
          {gates.length > 0 ? `${gates.length}/6 complete` : 'Waiting for SSE stream'}
        </div>
      </div>
    );
  }

  const gate = gates.find(g => g.gate_id === focusedGateId);
  if (!gate) return null;

  // 视图路由
  if (focusedGateId === 1 || focusedGateId === 2) {
    return <Ros2TopologySvg gates={gates} />;
  }
  if (focusedGateId === 3 || focusedGateId === 4) {
    return <YamlDiffViewer original={storedYaml} modified={scenarioYaml} gate={gate} />;
  }
  if (focusedGateId === 5 || focusedGateId === 6) {
    return <ContainerBoundarySvg gate6Result={gates.find(g => g.gate_id === 6)} gate5Result={gates.find(g => g.gate_id === 5)} />;
  }
  return null;
}
```

> CSS spin 动画需在 `tokens.css` 追加：
> ```css
> @keyframes spin { to { transform: rotate(360deg); } }
> ```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/DiagnosticCanvas.tsx web/src/styles/tokens.css
git commit -m "feat(screen2): add DiagnosticCanvas center-pane view router"
```

---

### Task 14: 右栏容器 — `ActionLogs.tsx`

**Files:**
- Create: `web/src/screens/shared/ActionLogs.tsx`
- Reference: `web/src/screens/shared/LiveLogStream.tsx`（复用为上半部日志流）
- Reference: `web/src/screens/shared/QuickFixPanel.tsx`（下半部 Quick Fix）

- [ ] **Step 1: 创建 `ActionLogs.tsx`**

```typescript
// web/src/screens/shared/ActionLogs.tsx
import React from 'react';
import type { GateSSEEvent } from '../../types/gateStream';
import { LiveLogStream } from './LiveLogStream';
import { QuickFixPanel } from './QuickFixPanel';

// 焦点 Gate → nodeFilter 映射
const GATE_FILTER_MAP: Record<number, string> = {
  1: 'foxglove|docker',
  2: 'm7_safety',
  3: 'scenario|odd',
  4: 'scenario|odd',
  5: 'clock|chrony',
  6: 'm7|cgroup',
};

interface ActionLogsProps {
  focusedGateId: number | null;
  gates: GateSSEEvent[];
  scenarioId: string | null;
  runId: string | null;
  onRerun: () => void;
  onAbort: () => void;
  onFixApplied: () => void;
}

export function ActionLogs({ focusedGateId, gates, scenarioId, runId, onRerun, onAbort, onFixApplied }: ActionLogsProps) {
  const nodeFilter = focusedGateId ? GATE_FILTER_MAP[focusedGateId] : undefined;
  const failedCount = gates.filter(g => !g.passed).length;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', background: 'var(--bg-1)', borderLeft: '1px solid var(--line-2)' }}>
      {/* 顶部标题栏 */}
      <div style={{ padding: '12px 16px', borderBottom: '1px solid var(--line-2)', display: 'flex', gap: 8, alignItems: 'center' }}>
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 13, color: 'var(--txt-0)' }}>ACTIONS & LOGS</span>
        {failedCount > 0 && (
          <span style={{ fontSize: 10, fontFamily: 'var(--f-mono)', padding: '2px 6px', borderRadius: 3, background: 'rgba(248,81,73,0.15)', color: 'var(--c-danger)' }}>
            {failedCount} FAIL
          </span>
        )}
      </div>

      {/* 上半部：上下文日志流 (~60%) */}
      <div style={{ flex: '1 1 60%', overflow: 'hidden', borderBottom: '1px solid var(--line-2)' }}>
        <LiveLogStream nodeFilter={nodeFilter} maxLines={200} />
      </div>

      {/* 下半部：Quick Fix (~40%) */}

      {/* Re-run + Abort 按钮 */}
      <div style={{ padding: '8px 16px', display: 'flex', gap: 8, borderTop: '1px solid var(--line-2)' }}>
        <button onClick={onRerun} style={{
          flex: 1, padding: '6px 12px', border: 'none', borderRadius: 4, cursor: 'pointer',
          background: 'var(--c-phos)', color: '#000', fontFamily: 'var(--f-disp)', fontSize: 12,
        }}>
          ↻ Re-run Checks
        </button>
        <button onClick={onAbort} style={{
          flex: 1, padding: '6px 12px', border: '1px solid var(--c-danger)', borderRadius: 4, cursor: 'pointer',
          background: 'transparent', color: 'var(--c-danger)', fontFamily: 'var(--f-disp)', fontSize: 12,
        }}>
          ✕ ABORT
        </button>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/ActionLogs.tsx
git commit -m "feat(screen2): add ActionLogs right-pane with log stream + quick fix"
```

---

### Task 15: 主协调器重写 — `SimulationCheck.tsx`

**Files:**
- Modify: `web/src/screens/SimulationCheck.tsx`（完全重写为三栏布局）
- Reference: 所有新组件（GateSequencer, DiagnosticCanvas, ActionLogs）
- Reference: `web/src/hooks/useGateStream.ts`, `web/src/hooks/useHotkeys.ts`
- Reference: `web/src/api/silApi.ts`（useGetScenarioQuery, lifecycle mutations）
- Reference: `web/src/store/scenarioStore.ts`（useScenarioStore）

- [ ] **Step 1: 读取现有 `SimulationCheck.tsx` 并重写**

> ⚠️ 先 `git mv web/src/screens/SimulationCheck.tsx web/src/screens/SimulationCheck.tsx.bak` 保留旧版参照。

```typescript
// web/src/screens/SimulationCheck.tsx (重写)
import React, { useState, useEffect, useCallback } from 'react';
import { useGateStream } from '../hooks/useGateStream';
import { useHotkeys } from '../hooks/useHotkeys';
import { useScenarioStore } from '../store';
import { useGetScenarioQuery, useActivateLifecycleMutation, useCleanupLifecycleMutation } from '../api/silApi';
import { GateSequencer } from './shared/GateSequencer';
import { DiagnosticCanvas } from './shared/DiagnosticCanvas';
import { ActionLogs } from './shared/ActionLogs';

const IS_DEV = typeof import.meta !== 'undefined' && (import.meta as any).env?.DEV;

export function SimulationCheck() {
  // 从 hash 路由提取 scenarioId: #/check/{id}
  const scenarioId = window.location.hash.replace('#/check/', '') || null;
  const { runId, lifecycleState } = useScenarioStore();
  const { data: scenarioDetail } = useGetScenarioQuery(scenarioId ?? '', { skip: !scenarioId });
  const [activateLifecycle] = useActivateLifecycleMutation();
  const [cleanupLifecycle] = useCleanupLifecycleMutation();

  const { gates, verdict, streaming, error, start, abort } = useGateStream(scenarioId, true);
  const [focusedGateId, setFocusedGateId] = useState<number | null>(null);
  const [countdown, setCountdown] = useState(0);
  const [devSkipReason, setDevSkipReason] = useState('');

  // 焦点自动跟随最后一个 FAIL Gate
  useEffect(() => {
    const lastFail = [...gates].reverse().find(g => !g.passed);
    if (lastFail) setFocusedGateId(lastFail.gate_id);
  }, [gates]);

  // GO 路径：3 秒倒数后跳转
  useEffect(() => {
    if (verdict === 'GO') {
      setCountdown(3);
      const timer = setInterval(() => {
        setCountdown(prev => {
          if (prev <= 1) { clearInterval(timer); handleProceed(); return 0; }
          return prev - 1;
        });
      }, 1000);
      return () => clearInterval(timer);
    }
  }, [verdict]);

  // 键盘快捷键
  useHotkeys({
    onTor: verdict !== 'GO' ? () => start() : undefined,             // R → Re-run
    onFault: () => handleAbort(),                                     // F → Abort (fallback to Esc)
    onMrc: IS_DEV && verdict === 'NO-GO' ? () => handleDevSkip() : undefined,  // M → Dev Skip
  });

  const handleProceed = useCallback(async () => {
    if (!scenarioId) return;
    try {
      await activateLifecycle({ scenario_id: scenarioId });
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('activate failed:', e); }
  }, [scenarioId, activateLifecycle]);

  const handleAbort = useCallback(async () => {
    abort();
    try { await cleanupLifecycle(); } catch {}
    window.location.hash = '#/scenario';
  }, [abort, cleanupLifecycle]);

  const handleDevSkip = useCallback(async () => {
    if (!scenarioId || !devSkipReason.trim()) return;
    try {
      await fetch('/api/v1/selfcheck/skip', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scenario_id: scenarioId, reason: devSkipReason }),
      });
      window.location.hash = `#/monitor/${scenarioId}`;
    } catch (e) { console.error('dev skip failed:', e); }
  }, [scenarioId, devSkipReason]);

  if (!scenarioId) {
    return <div style={{ padding: 40, color: 'var(--c-danger)', fontFamily: 'var(--f-body)' }}>No scenario selected</div>;
  }

  return (
    <div style={{ display: 'grid', gridTemplateColumns: '240px 1fr 300px', height: '100vh', overflow: 'hidden' }}>
      {/* 左栏 */}
      <GateSequencer gates={gates} streaming={streaming} focusedGateId={focusedGateId}
        onGateSelect={setFocusedGateId} verdict={verdict} />

      {/* 中栏 */}
      <DiagnosticCanvas focusedGateId={focusedGateId} gates={gates}
        scenarioYaml={scenarioDetail?.yaml_content ?? ''}
        storedYaml={scenarioDetail?.yaml_content ?? ''}  // TODO: 从后端获取 stored hash YAML
        verdict={verdict} countdown={countdown} />

      {/* 右栏 */}
      <ActionLogs focusedGateId={focusedGateId} gates={gates}
        scenarioId={scenarioId} runId={runId ?? 'unknown'}
        onRerun={start} onAbort={handleAbort} onFixApplied={() => {}} />

      {/* Dev Skip overlay */}
      {IS_DEV && verdict === 'NO-GO' && (
        <div style={{ position: 'fixed', bottom: 40, right: 320, zIndex: 100, padding: '12px 16px', background: 'var(--bg-2)', border: '1px solid var(--c-warn)', borderRadius: 6 }}>
          <div style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--c-warn)', marginBottom: 8 }}>DEV MODE: SKIP PREFLIGHT</div>
          <input value={devSkipReason} onChange={e => setDevSkipReason(e.target.value)}
            placeholder="Reason for skip..." style={{ padding: '4px 8px', marginRight: 8, border: '1px solid var(--line-2)', borderRadius: 3, background: 'var(--bg-0)', color: 'var(--txt-0)', fontSize: 11 }} />
          <button onClick={handleDevSkip} disabled={!devSkipReason.trim()}
            style={{ padding: '4px 12px', background: 'var(--c-warn)', color: '#000', border: 'none', borderRadius: 3, cursor: 'pointer', fontFamily: 'var(--f-disp)', fontSize: 11 }}>
            SKIP → MONITOR
          </button>
        </div>
      )}

      {/* 错误提示 */}
      {error && (
        <div style={{ position: 'fixed', top: 8, right: 320, zIndex: 100, padding: '8px 16px', background: 'rgba(248,81,73,0.15)', border: '1px solid var(--c-danger)', borderRadius: 4, fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-danger)' }}>
          SSE Error: {error}
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`
Expected: No errors（确认所有 import 路径正确，新组件导出为命名导出）

- [ ] **Step 3: 手动视觉验证** — 在浏览器中打开 `#/check/{scenario_id}`

验证点：
- 三栏布局正常渲染（240px / flex / 300px）
- SSE 流启动后 Gate 逐条出现
- 点击左栏 Gate 行 → 中栏切换视图
- Abort 按钮可用 → 返回 #/scenario

- [ ] **Step 4: Commit**

```bash
git add web/src/screens/SimulationCheck.tsx
git commit -m "feat(screen2): rewrite SimulationCheck as 3-pane SSE diagnostic console"
```

---

### Task 16: 快捷键扩展 — `useHotkeys.ts`

**Files:**
- Modify: `web/src/hooks/useHotkeys.ts`（新增 R=Re-run / Esc=Abort / D=Dev Skip / Enter=Proceed）

- [ ] **Step 1: 读取并修改 `useHotkeys.ts`**

> ⚠️ 先读取 `web/src/hooks/useHotkeys.ts` 确认现有接口和处理逻辑。

在 `HotkeyHandlers` 接口中追加（若不存在）：
```typescript
interface HotkeyHandlers {
  // ... 现有 ...
  onTor?: () => void;       // 复用为 R=Re-run
  onFault?: () => void;     // 复用为 Esc=Abort
  onMrc?: () => void;       // 复用为 D=Dev Skip
  onSpace?: () => void;     // 复用为 Enter=Proceed
}
```

在 `useEffect` 的 keydown handler 中追加映射：
```typescript
// 在现有 keydown handler 的 switch/case 或 if 块中追加：
case 'r': case 'R': handlers.onTor?.(); break;
case 'Escape': handlers.onFault?.(); break;
case 'd': case 'D': handlers.onMrc?.(); break;
case 'Enter': handlers.onSpace?.(); break;
```

确保不拦截 input/textarea 聚焦时的按键（现有逻辑已处理 `document.activeElement?.tagName`）。

- [ ] **Step 2: TypeScript 编译检查**

Run: `npx tsc --noEmit --project web/tsconfig.json`

- [ ] **Step 3: Commit**

```bash
git add web/src/hooks/useHotkeys.ts
git commit -m "feat(screen2): add R/Esc/D/Enter hotkey handlers for screen2"
```

---

### Task 17: 证据归档 — `lifecycle_bridge.py`

**Files:**
- Modify: `src/sil_orchestrator/lifecycle_bridge.py`（新增 `_copy_preflight_evidence` 方法）
- Reference: `src/sil_orchestrator/config.py`（SCENARIO_DIR, RUN_DIR）
- Reference: `src/sil_orchestrator/main.py`（_seed_run_dir 调用位置）

- [ ] **Step 1: 在 `lifecycle_bridge.py` 中新增归档方法**

```python
# 在 lifecycle_bridge.py 中追加
import shutil
from pathlib import Path
from sil_orchestrator.config import SCENARIO_DIR, RUN_DIR

def _copy_preflight_evidence(scenario_id: str, run_id: str) -> None:
    """将 staging 证据从 .preflight/ 归档到 runs/{run_id}/preflight/"""
    src = SCENARIO_DIR / scenario_id / ".preflight"
    dst = RUN_DIR / run_id / "preflight"
    if not src.exists():
        return
    dst.mkdir(parents=True, exist_ok=True)
    for gate_file in src.glob("gate_*.json"):
        shutil.copy2(gate_file, dst / gate_file.name)
```

- [ ] **Step 2: 在 `activate()` 路径中调用归档**

在 `lifecycle_bridge.py` 的 `activate()` 成功返回后（或在 `main.py` 的 `activate` 路由处理中），调用：
```python
_copy_preflight_evidence(scenario_id, run_id)
```

> 具体插入位置取决于现有 activate 流程。若 `main.py` 中直接处理 activate 路由，则在路由 handler 中调用。若 lifecycle_bridge 封装了 activate，则在 `activate()` 方法末尾调用。

- [ ] **Step 3: 验证归档**

Run:
```bash
# 先运行完整 selfcheck → activate 流程
ls -la $(find runs/ -name "gate_*.json" -path "*/preflight/*")
```
Expected: `runs/{run_id}/preflight/gate_1.json` 等 6 个文件存在，内容与 staging 一致

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/lifecycle_bridge.py
git commit -m "feat(screen2): add _copy_preflight_evidence archive on activate"
```

---

### Task 18: Vitest 组件测试 — 新组件

**Files:**
- Create: `web/src/hooks/useGateStream.test.ts`
- Create: `web/src/screens/shared/__tests__/GateSequencer.test.tsx`
- Create: `web/src/screens/shared/__tests__/DiagnosticCanvas.test.tsx`
- Create: `web/src/screens/shared/__tests__/QuickFixPanel.test.tsx`
- Create: `web/src/screens/__tests__/SimulationCheck.test.tsx`
- Reference: `web/src/test-setup.ts`（jsdom polyfills）
- Reference: 现有测试（`store/__tests__/*`）的 mock 模式

- [ ] **Step 1: 编写 `useGateStream.test.ts`** — mock EventSource

```typescript
// web/src/hooks/useGateStream.test.ts
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useGateStream } from './useGateStream';

class MockEventSource {
  onmessage: ((e: MessageEvent) => void) | null = null;
  onerror: (() => void) | null = null;
  close = vi.fn();
  constructor(public url: string) {}
}
(globalThis as any).EventSource = MockEventSource;

describe('useGateStream', () => {
  beforeEach(() => { vi.clearAllMocks(); });

  it('starts streaming on mount with autoStart=true', () => {
    const { result } = renderHook(() => useGateStream('test-scenario', true));
    expect(result.current.streaming).toBe(true);
    expect(result.current.gates).toEqual([]);
    expect(result.current.verdict).toBeNull();
  });

  it('does not auto-start when autoStart=false', () => {
    const { result } = renderHook(() => useGateStream('test-scenario', false));
    expect(result.current.streaming).toBe(false);
  });

  it('returns null scenario handling', () => {
    const { result } = renderHook(() => useGateStream(null, true));
    expect(result.current.streaming).toBe(false);
  });
});
```

- [ ] **Step 2: 编写 `GateSequencer.test.tsx`** — 渲染 6 行 + verdict

```typescript
// web/src/screens/shared/__tests__/GateSequencer.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { GateSequencer } from '../GateSequencer';
import type { GateSSEEvent } from '../../../types/gateStream';

const mockGates: GateSSEEvent[] = [
  { gate_id: 1, label: 'System Readiness', passed: true, checks: [], duration_ms: 230, rationale: 'ok' },
  { gate_id: 2, label: 'Module Health', passed: false, checks: [], duration_ms: 150, rationale: 'M7 fail' },
];

describe('GateSequencer', () => {
  it('renders 6 gate rows', () => {
    render(<GateSequencer gates={mockGates} streaming={false} focusedGateId={null}
      onGateSelect={() => {}} verdict={null} />);
    for (let i = 1; i <= 6; i++) {
      expect(screen.getByText(new RegExp(`GATE ${i}`))).toBeDefined();
    }
  });

  it('shows GO verdict banner', () => {
    render(<GateSequencer gates={[]} streaming={false} focusedGateId={null}
      onGateSelect={() => {}} verdict="GO" />);
    expect(screen.getByText('GO')).toBeDefined();
  });

  it('calls onGateSelect on click', () => {
    const fn = vi.fn();
    render(<GateSequencer gates={mockGates} streaming={false} focusedGateId={null}
      onGateSelect={fn} verdict={null} />);
    fireEvent.click(screen.getByText(/GATE 1/));
    expect(fn).toHaveBeenCalledWith(1);
  });
});
```

- [ ] **Step 3: 编写 `DiagnosticCanvas.test.tsx`** — 视图路由

```typescript
// web/src/screens/shared/__tests__/DiagnosticCanvas.test.tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DiagnosticCanvas } from '../DiagnosticCanvas';

describe('DiagnosticCanvas', () => {
  it('shows GO overlay when verdict is GO', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict="GO" countdown={3} />);
    expect(screen.getByText(/ALL GATES CLEAR/)).toBeDefined();
    expect(screen.getByText(/Auto-activating in 3/)).toBeDefined();
  });

  it('shows idle spinner when no focusedGateId', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict={null} countdown={0} />);
    expect(screen.getByText('Initializing...')).toBeDefined();
  });
});
```

- [ ] **Step 4: 运行测试**

Run: `cd web && npx vitest run`
Expected: 5 个新测试 + 11 个现有测试全部 PASS

- [ ] **Step 5: Commit**

```bash
git add web/src/hooks/useGateStream.test.ts web/src/screens/shared/__tests__/ web/src/screens/__tests__/SimulationCheck.test.tsx
git commit -m "test(screen2): add vitest tests for useGateStream, GateSequencer, DiagnosticCanvas"
```

---

### Task 19: Playwright E2E 测试

**Files:**
- Modify: `web/e2e/preflight.spec.ts`（扩展或新建 screen2 专用 spec）
- Reference: `web/playwright.config.ts`

- [ ] **Step 1: 创建 `web/e2e/screen2-check.spec.ts`**

```typescript
// web/e2e/screen2-check.spec.ts
import { test, expect } from '@playwright/test';

test.describe('Screen 2 – Simulation Check', () => {
  test('three-pane layout renders', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    // 等待三栏中的关键元素
    await expect(page.locator('text=GATE PROGRESS')).toBeVisible({ timeout: 5000 });
    await expect(page.locator('text=ACTIONS & LOGS')).toBeVisible({ timeout: 5000 });
  });

  test('gate rows render 6 items', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    // 等待至少一个 Gate 行出现
    await page.waitForSelector('text=GATE 1', { timeout: 10000 });
    for (let i = 1; i <= 6; i++) {
      await expect(page.locator(`text=GATE ${i}`).first()).toBeVisible();
    }
  });

  test('click gate row switches diagnostic view', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForSelector('text=GATE 3', { timeout: 10000 });
    await page.locator('text=GATE 3').first().click();
    // Gate 3/4 应该显示 YAML Diff 视图
    await expect(page.locator('text=YAML Diff')).toBeVisible({ timeout: 5000 });
  });

  test('abort button returns to scenario screen', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForSelector('text=ABORT', { timeout: 10000 });
    await page.locator('text=ABORT').first().click();
    await expect(page).toHaveURL(/#\/scenario/);
  });

  test('keyboard R triggers re-run', async ({ page }) => {
    await page.goto('/#/check/test_demo');
    await page.waitForTimeout(2000);
    await page.keyboard.press('r');
    // 验证 streaming 状态重新开始（Gate 行为重置）
    await page.waitForTimeout(1000);
  });
});
```

- [ ] **Step 2: 运行 Playwright E2E**

Run:
```bash
cd web && npx playwright test e2e/screen2-check.spec.ts --reporter=list
```
Expected: 5 tests PASS（依赖后端 SSE endpoint 运行中）

- [ ] **Step 3: Commit**

```bash
git add web/e2e/screen2-check.spec.ts
git commit -m "test(screen2): add Playwright E2E tests for screen2 three-pane flow"
```

---

### Task 20: 后端 pytest 测试 — SSE + ops

**Files:**
- Create: `src/sil_orchestrator/tests/test_selfcheck_stream.py`
- Create: `src/sil_orchestrator/tests/test_ops_routes.py`
- Reference: 现有测试目录结构（`src/sil_orchestrator/tests/` 若不存在则创建）

- [ ] **Step 1: 编写 `test_selfcheck_stream.py`**

```python
# src/sil_orchestrator/tests/test_selfcheck_stream.py
import json
import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app

@pytest.mark.asyncio
async def test_sse_stream_returns_events():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        async with client.stream("GET", "/api/v1/selfcheck/stream?scenario_id=test_demo") as resp:
            assert resp.status_code == 200
            assert resp.headers["content-type"] == "text/event-stream"
            events = []
            async for line in resp.aiter_lines():
                if line.startswith("data: "):
                    data = json.loads(line[6:])
                    events.append(data)
                    if data.get("type") == "complete":
                        break
            assert len(events) >= 2  # 至少 1 个 gate + complete
            assert events[-1]["type"] == "complete"
            assert events[-1]["go_no_go"] in ("GO", "NO-GO")

@pytest.mark.asyncio
async def test_sse_stream_missing_scenario():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        async with client.stream("GET", "/api/v1/selfcheck/stream") as resp:
            assert resp.status_code == 200  # scenario_id optional
```

- [ ] **Step 2: 编写 `test_ops_routes.py`**

```python
# src/sil_orchestrator/tests/test_ops_routes.py
import pytest
from httpx import AsyncClient, ASGITransport
from sil_orchestrator.main import app

@pytest.mark.asyncio
async def test_restart_node_rejects_invalid_name():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/restart_node?name=;%20rm%20-rf")
        assert resp.status_code == 422

@pytest.mark.asyncio
async def test_clear_hash_cache_accepts_valid_id():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/clear_hash_cache?scenario_id=test_valid")
        assert resp.status_code == 200
        data = resp.json()
        assert data["success"] is True

@pytest.mark.asyncio
async def test_ensure_asdr_dir():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post("/api/v1/ops/ensure_asdr_dir?run_id=rx-test-001")
        assert resp.status_code == 200
        data = resp.json()
        assert data["success"] is True
```

- [ ] **Step 3: 运行 pytest**

Run: `cd src/sil_orchestrator && python -m pytest tests/ -v`
Expected: 5 个后端测试 PASS

- [ ] **Step 4: Commit**

```bash
git add src/sil_orchestrator/tests/
git commit -m "test(screen2): add pytest tests for SSE stream + ops endpoints"
```

---

### Task 21: 全链路集成验证

**Files:**
- Verify: 所有新增/修改文件
- Evidence: `.sisyphus/evidence/` 截图 + curl 输出

- [ ] **Step 1: 运行全部前端测试**

Run:
```bash
cd web && npx vitest run
```
Expected: 所有 Vitest 测试 PASS（≥16 tests, 0 failures）

- [ ] **Step 2: 运行全部后端测试**

Run:
```bash
cd src/sil_orchestrator && python -m pytest tests/ -v
```
Expected: 所有 pytest PASS（≥5 tests）

- [ ] **Step 3: TypeScript 全量编译检查**

Run:
```bash
cd web && npx tsc --noEmit
```
Expected: No errors

- [ ] **Step 4: SSE 端点到前端端到端验证**

Run:
```bash
# 终端 1：启动后端
python -m sil_orchestrator.main

# 终端 2：验证 SSE 流
curl -N "http://localhost:8000/api/v1/selfcheck/stream?scenario_id=test_demo" 2>&1 | head -30

# 终端 3：验证证据产物
find . -path "*/.preflight/gate_*.json" -exec echo "=== {} ===" \; -exec head -5 {} \;
```
Expected: curl 看到 6 个 gate 事件 + complete 事件，证据文件 6 个且 JSON 合法

- [ ] **Step 5: ops 端点全量验证**

Run:
```bash
# 依次调用 5 个端点
echo "--- restart_services ---" && curl -s -X POST "http://localhost:8000/api/v1/ops/restart_services" | python3 -m json.tool
echo "--- sync_time ---" && curl -s -X POST "http://localhost:8000/api/v1/ops/sync_time" | python3 -m json.tool
echo "--- clear_hash_cache ---" && curl -s -X POST "http://localhost:8000/api/v1/ops/clear_hash_cache?scenario_id=test" | python3 -m json.tool
echo "--- ensure_asdr_dir ---" && curl -s -X POST "http://localhost:8000/api/v1/ops/ensure_asdr_dir?run_id=rx-001" | python3 -m json.tool
echo "--- security check ---" && curl -s -o /dev/null -w "%{http_code}" -X POST "http://localhost:8000/api/v1/ops/restart_node?name=;rm-rf"
```
Expected: 前 4 个返回 `{"success":...}` JSON，最后一个返回 `422`

- [ ] **Step 6: Playwright E2E 测试**

Run:
```bash
cd web && npx playwright test e2e/screen2-check.spec.ts --reporter=html
```
Expected: 5 E2E tests PASS

- [ ] **Step 7: 验收清单对照**

| 验收项 | 验证方法 | 状态 |
|---|---|---|
| 三栏布局渲染正确 | Playwright 视觉验证 | ☐ |
| SSE 流式渐进显示 | curl -N 验证 6 gate events | ☐ |
| GO 路径：3s 自动跳屏③ | Playwright mock GO | ☐ |
| NO-GO 路径：Quick Fix 按钮出现 | Playwright mock FAIL | ☐ |
| DiagnosticCanvas 视图切换 | 点击 Gate → 中栏变化 | ☐ |
| 证据产物写入 | `find .preflight/gate_*.json` | ☐ |
| ops restart_node 白名单拒绝 | 422 status code | ☐ |
| 键盘 R/Esc 快捷键 | Playwright keyboard | ☐ |

---

## 成功标准

### 运行命令

```bash
# 前端单元测试
cd web && npx vitest run              # Expected: ≥16 tests PASS

# 后端测试
cd src/sil_orchestrator && python -m pytest tests/ -v  # Expected: ≥5 tests PASS

# TypeScript 编译
cd web && npx tsc --noEmit           # Expected: No errors

# E2E 测试
cd web && npx playwright test e2e/screen2-check.spec.ts  # Expected: 5 tests PASS

# SSE 端点验证
curl -N "http://localhost:8000/api/v1/selfcheck/stream?scenario_id=test_demo"
# Expected: 6 gate events + complete event in text/event-stream format

# 证据产物验证
find . -path "*/.preflight/gate_*.json" | wc -l  # Expected: 6

# ops 安全约束
curl -s -o /dev/null -w "%{http_code}" -X POST \
  "http://localhost:8000/api/v1/ops/restart_node?name=;%20rm%20-rf"
# Expected: 422
```

### 最终检查清单

- [ ] 所有新增组件（7 个 TSX）已创建并编译通过
- [ ] SimulationCheck.tsx 三栏布局功能完整
- [ ] SSE 端点返回符合 text/event-stream 协议
- [ ] 5 个 ops 端点全部可用 + 安全白名单生效
- [ ] 证据产物 `gate_N.json` 包含完整 SIL2 条款引用
- [ ] activate 时证据从 staging 归档到 runs/
- [ ] LiveLogStream nodeFilter 过滤正确
- [ ] Dev-mode skip 仅 ?dev=1 可见
- [ ] 键盘快捷键 R/Esc/D/Enter 生效
- [ ] 所有 Vitest + pytest + Playwright 测试 PASS

---

## 并行执行总结

| Wave | 任务数 | 最大并行 | 关键依赖 |
|---|---|---|---|
| 0 (Contract) | 1 | 1 | — |
| 1a (Backend) | 3 | 3 | Task 0 |
| 1b (Frontend Foundation) | 4 | 4 | Task 0 |
| 2 (Components) | 7 | **7** | Tasks 4-7 |
| 3 (Integration) | 3 | 1+2 | Tasks 8-14 |
| 4 (Tests) | 3 | 3 | Tasks 15-17 |
| 5 (Verify) | 1 | 1 | Tasks 18-20 |

**总任务数**: 21  
**理论最大并行度**: Wave 2 可同时调度 7 个 subagent  
**建议 subagent 分配**:

```
Session 1: Task 0 (shared types) — 基础契约，所有后续任务依赖
Session 2 (parallel): Tasks 1, 2, 3 (backend) + Tasks 4, 5, 6, 7 (frontend foundation)
Session 3 (parallel): Tasks 8, 9, 10, 11, 12, 13, 14 (7 components)
Session 4: Tasks 15, 16, 17 (integration + evidence archive)
Session 5 (parallel): Tasks 18, 19, 20 (tests)
Session 6: Task 21 (final verification)
```

---

*Plan generated: 2026-05-18 | Spec: SANGO-SPEC-SIL-SCREEN2-001 v1.0 | D-task: D1.3b.3*






