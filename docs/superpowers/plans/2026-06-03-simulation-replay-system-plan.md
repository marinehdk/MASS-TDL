# 仿真报告同步回放系统 (Simulation Replay System) 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现仿真报告（Screen 4）的全局同步回放系统，打通后端 MCAP 到 Arrow 的数据转换接口，前端以 A2（55% : 22.5% : 22.5%）紧凑比例进行排版布局，并通过全局时间状态联动地图、时间轴与账本表格。

**Architecture:**
1. 后端：提供 `POST /api/v1/export/arrow` 异步调用 PyArrow 转换工具生成 `runs/{run_id}/replay.arrow`，并通过 StaticFiles 静态挂载暴露该文件，支持 Range 分片读取。
2. 前端：在 `SimulationEvaluator` 中定义统一时间 `currentTimeSec`，全局 Scrubber 拖拽时双向同步时间，各子面板（轨迹图、时间轴、账本行）动态响应，并利用 CSS 截断技术消灭多余留白与文字溢出。

**Tech Stack:** Python 3.10 + pyarrow + rosbags · FastAPI · React 18 + Zustand · gsap · Playwright

---

## 文件结构

| 操作 | 文件 | 职责 |
|------|------|------|
| Create | `tools/vv/mcap_to_arrow.py` | 后端 MCAP 转换为 Arrow 列存的脚本工具 |
| Create | `tools/vv/test_mcap_to_arrow.py` | 验证 mcap 转换及 Arrow schema 正确性的单元测试 |
| Create | `src/sil_orchestrator/arrow_routes.py` | 暴露 POST /export/arrow 及 status 查询接口的 FastAPI 路由 |
| Modify | `src/sil_orchestrator/main.py` | 引入并注册 arrow_routes 路由，且挂载 runs 静态资源路径 |
| Modify | `web/src/screens/SimulationEvaluator.tsx` | 重组为 A2 (55% : 22.5% : 22.5%) 布局，定义全局回放时间状态 |
| Modify | `web/src/screens/shared/AsdrLedger.tsx` | ASDR 账本高度自适应，表格溢出 ellipsis 截断及 title tooltip 悬停提示，行点击触发跳转 |
| Modify | `web/src/screens/shared/TimelineSixLane.tsx` | 事件时间轴拖动及点击事件，点亮播放当前秒的指针 |
| Modify | `web/src/screens/shared/TrajectoryReplay.tsx` | 地图回放播放/暂停定时器状态，拖动滑动条事件分发 |

---

### Task 1: 后端 Arrow 转换工具与 API 路由

**Files:**
- Create: `tools/vv/mcap_to_arrow.py`
- Create: `tools/vv/test_mcap_to_arrow.py`
- Create: `src/sil_orchestrator/arrow_routes.py`
- Modify: `src/sil_orchestrator/main.py:240-265`

- [ ] **Step 1: 编写转换工具的测试用例**
  创建 `tools/vv/test_mcap_to_arrow.py`，断言转换输出的 Arrow 文件存在且 Schema 列结构正确：
  ```python
  import pytest
  from pathlib import Path
  import pyarrow.ipc as pa_ipc

  def test_empty_mcap_produces_arrow(tmp_path):
      from tools.vv.mcap_to_arrow import convert_mcap_to_arrow
      out = tmp_path / "replay.arrow"
      convert_mcap_to_arrow(str(tmp_path / "nonexistent.mcap"), str(out), topics=["/sil/own_ship_state"])
      assert out.exists()
      reader = pa_ipc.open_file(str(out))
      assert reader.schema is not None
      assert set(reader.schema.names) == {"timestamp_ns", "channel", "payload_bytes"}
  ```

- [ ] **Step 2: 运行测试并确认失败**
  Run: `pytest tools/vv/test_mcap_to_arrow.py -v`
  Expected: FAIL (ModuleNotFoundError or nonexistent path)

- [ ] **Step 3: 实现 mcap_to_arrow.py 转换功能**
  创建 `tools/vv/mcap_to_arrow.py`：
  ```python
  #!/usr/bin/env python3
  import argparse
  import sys
  from pathlib import Path
  import pyarrow as pa
  import pyarrow.ipc as pa_ipc

  DEFAULT_TOPICS = [
      "/sil/own_ship_state",
      "/sil/target_vessel_state",
      "/sil/sat2_data",
      "/sil/sat3_data",
      "/sil/sotif_metrics",
      "/sil/asdr_event",
  ]
  BATCH_WINDOW_NS = 10 * 10**9

  SCHEMA = pa.schema([
      pa.field("timestamp_ns", pa.int64()),
      pa.field("channel", pa.string()),
      pa.field("payload_bytes", pa.binary()),
  ])

  def _empty_file(out_path: Path) -> None:
      with pa_ipc.new_file(str(out_path), SCHEMA) as writer:
          pass

  def convert_mcap_to_arrow(mcap_path: str, out_path: str, topics: list[str] | None = None) -> int:
      if topics is None:
          topics = DEFAULT_TOPICS
      mcap_file = Path(mcap_path)
      out = Path(out_path)
      out.parent.mkdir(parents=True, exist_ok=True)
      if not mcap_file.exists() or mcap_file.stat().st_size == 0:
          _empty_file(out)
          return 0
      try:
          from rosbags.highlevel import AnyReader
      except ImportError:
          _empty_file(out)
          return 0

      timestamps, channels, payloads = [], [], []
      total = 0
      with AnyReader([mcap_file]) as reader:
          connections = [c for c in reader.connections if c.topic in topics]
          for conn, timestamp, rawdata in reader.messages(connections=connections):
              timestamps.append(timestamp)
              channels.append(conn.topic)
              payloads.append(bytes(rawdata))
              total += 1
      triples = sorted(zip(timestamps, channels, payloads), key=lambda x: x[0])
      with pa_ipc.new_file(str(out), SCHEMA) as writer:
          if triples:
              t_start = triples[0][0]
              batch_ts, batch_ch, batch_pl = [], [], []
              for ts, ch, pl in triples:
                  if ts - t_start >= BATCH_WINDOW_NS and batch_ts:
                      writer.write_batch(pa.record_batch([batch_ts, batch_ch, batch_pl], schema=SCHEMA))
                      batch_ts, batch_ch, batch_pl = [], [], []
                      t_start = ts
                  batch_ts.append(ts)
                  batch_ch.append(ch)
                  batch_pl.append(pl)
              if batch_ts:
                  writer.write_batch(pa.record_batch([batch_ts, batch_ch, batch_pl], schema=SCHEMA))
      return total

  def main():
      ap = argparse.ArgumentParser()
      ap.add_argument("--run-dir", required=True)
      ap.add_argument("--output")
      args = ap.parse_args()
      run_dir = Path(args.run_dir)
      mcap_files = list(run_dir.glob("*.mcap"))
      mcap_path = str(mcap_files[0]) if mcap_files else str(run_dir / "nonexistent.mcap")
      out_path = args.output or str(run_dir / "replay.arrow")
      count = convert_mcap_to_arrow(mcap_path, out_path)
      print(f"[OK] Wrote {count} messages -> {out_path}")

  if __name__ == "__main__":
      main()
  ```

- [ ] **Step 4: 重新运行单元测试验证通过**
  Run: `pytest tools/vv/test_mcap_to_arrow.py -v`
  Expected: PASS

- [ ] **Step 5: 实现 arrow_routes.py 并挂载静态路由**
  创建 `src/sil_orchestrator/arrow_routes.py`：
  ```python
  import subprocess
  import sys
  from pathlib import Path
  from fastapi import APIRouter, BackgroundTasks, HTTPException
  from sil_orchestrator.config import RUN_DIR

  router = APIRouter(prefix="/api/v1/export")
  _arrow_status = {}

  def _build_arrow(run_id: str) -> None:
      run_path = RUN_DIR / run_id
      out_path = run_path / "replay.arrow"
      res = subprocess.run([
          sys.executable, "tools/vv/mcap_to_arrow.py",
          "--run-dir", str(run_path), "--output", str(out_path)
      ], capture_output=True, text=True)
      if res.returncode != 0:
          _arrow_status[run_id] = {"status": "error", "detail": res.stderr}
      else:
          _arrow_status[run_id] = {"status": "ready", "path": str(out_path)}

  @router.post("/arrow")
  async def export_arrow(request: dict, background_tasks: BackgroundTasks):
      run_id = request.get("run_id", "")
      if not run_id or not (RUN_DIR / run_id).exists():
          raise HTTPException(status_code=404, detail="Run not found")
      _arrow_status[run_id] = {"status": "processing"}
      background_tasks.add_task(_build_arrow, run_id)
      return {"status": "processing", "run_id": run_id}

  @router.get("/arrow/status/{run_id}")
  async def arrow_status(run_id: str):
      return _arrow_status.get(run_id, {"status": "unknown"})
  ```

  修改 `src/sil_orchestrator/main.py` 引入路由并挂载静态目录：
  ```python
  from fastapi.staticfiles import StaticFiles
  from sil_orchestrator.arrow_routes import router as arrow_router
  app.include_router(arrow_router)
  app.mount("/runs", StaticFiles(directory=str(RUN_DIR)), name="runs")
  ```

- [ ] **Step 6: Commit**
  ```bash
  git add tools/vv/mcap_to_arrow.py tools/vv/test_mcap_to_arrow.py src/sil_orchestrator/arrow_routes.py src/sil_orchestrator/main.py
  git commit -m "feat(sil-orchestrator): add arrow export routes and converter script"
  ```

---

### Task 2: 前端 A2 布局宽度微调与空白溢出规避

**Files:**
- Modify: `web/src/screens/SimulationEvaluator.tsx:164-280`
- Modify: `web/src/screens/shared/AsdrLedger.tsx:124-192`

- [ ] **Step 1: 在 SimulationEvaluator 中将布局配比改为 55% : 22.5% : 22.5%**
  修改 `web/src/screens/SimulationEvaluator.tsx` 的 Column 分配比：
  ```typescript
  // 修改 Column 1 宽度占比
  <div style={{
    flex: 55, // 原 42
    display: 'flex',
    flexDirection: 'column',
    height: '100%',
    minWidth: 0,
  }}>

  // 修改 Column 2 宽度占比
  <div style={{
    flex: 22.5, // 原 28
    display: 'flex',
    flexDirection: 'column',
    gap: 12,
    height: '100%',
    minWidth: 0,
  }}>

  // 修改 Column 3 宽度占比
  <div style={{
    flex: 22.5, // 原 30
    display: 'flex',
    flexDirection: 'column',
    gap: 12,
    height: '100%',
    minWidth: 0,
  }}>
  ```

- [ ] **Step 2: 对 AsdrLedger 的大文本列（Payload、SHA）进行 Ellipsis 截断处理**
  修改 `web/src/screens/shared/AsdrLedger.tsx` 对应渲染行的 CSS：
  ```typescript
  // Payload 列
  <td
    title={typeof e.payload === 'object' ? JSON.stringify(e.payload) : String(e.payload)}
    style={{
      padding: '2px 4px', maxWidth: 120,
      overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
    }}
  >
    {typeof e.payload === 'object' ? JSON.stringify(e.payload) : e.payload}
  </td>

  // Hash 列
  <td
    title={e.hash}
    style={{
      padding: '2px 4px', color: 'var(--txt-3)', fontSize: 7.5,
      maxWidth: 60, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
    }}
  >
    {e.hash ? e.hash.slice(0, 8) : ''}
  </td>
  ```

- [ ] **Step 3: 运行静态类型检查**
  Run: `npm run build --prefix web`
  Expected: 编译通过，无 TypeScript 错误。

- [ ] **Step 4: Commit**
  ```bash
  git add web/src/screens/SimulationEvaluator.tsx web/src/screens/shared/AsdrLedger.tsx
  git commit -m "style(hmi): adjust column weights to 55-22.5-22.5 and add text ellipsis to ledger"
  ```

---

### Task 3: 双向时间状态联动与跳转

**Files:**
- Modify: `web/src/screens/SimulationEvaluator.tsx`
- Modify: `web/src/screens/shared/TrajectoryReplay.tsx`
- Modify: `web/src/screens/shared/TimelineSixLane.tsx`
- Modify: `web/src/screens/shared/AsdrLedger.tsx`

- [ ] **Step 1: 在 SimulationEvaluator 中声明统一的回放状态并传递**
  ```typescript
  export function SimulationEvaluator() {
    const [currentTimeSec, setCurrentTimeSec] = useState(0);
    // ... 将 currentTimeSec 与 setCurrentTimeSec 通过组件 Props 完整传入三个子组件 ...
  }
  ```

- [ ] **Step 2: 绑定 TrajectoryReplay 进度条及播放控制**
  修改 `web/src/screens/shared/TrajectoryReplay.tsx`，当拖拽 input slider 时调用全局 `onTimeChange`；点击播放时开启定时器累加进度。

- [ ] **Step 3: 绑定 TimelineSixLane 的 scrub 点击与事件圆圈跳转**
  修改 `web/src/screens/shared/TimelineSixLane.tsx`，在轨道容器上点击或移动时，使用百分比计算出对应的时间点并回传给全局状态。

- [ ] **Step 4: 绑定 AsdrLedger 的行点击事件与 scrollIntoView**
  在 `web/src/screens/shared/AsdrLedger.tsx` 中，增加 `useEffect` 监听 `currentTimeSec`：
  ```typescript
  useEffect(() => {
    if (activeRowRef.current) {
      activeRowRef.current.scrollIntoView({
        behavior: 'smooth',
        block: 'nearest'
      });
    }
  }, [closestIndex]);
  ```

- [ ] **Step 5: 运行前端所有单元测试进行验证**
  Run: `npm test --prefix web -- --run`
  Expected: All 158 tests passed.

- [ ] **Step 6: Commit**
  ```bash
  git add web/src/screens/SimulationEvaluator.tsx web/src/screens/shared/TrajectoryReplay.tsx web/src/screens/shared/TimelineSixLane.tsx web/src/screens/shared/AsdrLedger.tsx
  git commit -m "feat(hmi): wire bidirectional scrubbing synchronization and autoscroll ledger lines"
  ```
