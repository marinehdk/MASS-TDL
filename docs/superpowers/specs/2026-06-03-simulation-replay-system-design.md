# 仿真报告同步回放系统设计规范 (Simulation Replay System Spec)

| 属性 | 值 |
|---|---|
| 文档名称 | 仿真报告同步回放系统设计规范 |
| 创建日期 | 2026-06-03 |
| 状态 | 草稿已确认 |
| 比例配置 | 方案 A2 (左 55% / 中 22.5% / 右 22.5%) |

---

## 1. 业务目标与定位
实现仿真报告页面（Screen 4）的“全纪录”多模式同步回放系统。该回放系统应完美对齐仿真监控页面（Screen 3）的运行直觉，支持全局播放/暂停、速率调节、时间轴拖拽（Scrubber），并实现“轨迹回放”、“事件时间轴”、“避碰决策树”和“ASDR 账本”的全局双向联动，使用户能够秒级定位到关键避碰事件和决策点。

---

## 2. 系统数据流与后端设计 (Backend Data Flow)

由于仿真生成的是海量 `MCAP` 原始日志，直接在前端全量加载解析会导致严重的网络卡顿和浏览器 CPU 占满。因此后端提供高性能的列式数据转换与文件流式分块读取服务。

```
+------------------+                   +--------------------+
| 仿真运行结束       |                   |  前端 Web HMI       |
| 产生 MCAP 日志包   |                   |  GSAP Scrubber 拖动 |
+------------------+                   +--------------------+
         |                                       ^
         | (POST /export/arrow)                  | (HTTP Range 请求)
         v                                       |
+-----------------------------------+            |
| 后端 PyArrow 转换服务               |            |
| (按 10s 分片写入 replay.arrow)      |------------+
+-----------------------------------+
```

### 2.1 转换服务 `mcap_to_arrow.py`
* **职责**：将仿真产出的 `.mcap` 转换为列式存储的 `.arrow` IPC 格式。
* **文件路径**：`tools/vv/mcap_to_arrow.py`
* **写入策略**：为支持前端的 HTTP Range 范围请求，每 10 秒（仿真时间）的遥测数据序列化为一个 Record Batch。
* **Schema 结构**：
  ```json
  {
    "timestamp_ns": "int64",     // 统一 PTP 时钟时间戳
    "channel": "string",         // 话题频道 (如 '/sil/own_ship_state')
    "payload_bytes": "binary"    // 原始序列化 Protobuf/ROS2 字节数据
  }
  ```

### 2.2 Orchestrator FastAPI 接口设计
在 `src/sil_orchestrator/arrow_routes.py` 中新增：
* `POST /api/v1/export/arrow`：
  - 入参：`{"run_id": "RUN-XXXX"}`
  - 动作：在后台拉起转换任务，生成 `runs/{run_id}/replay.arrow`。
* `GET /api/v1/export/arrow/status/{run_id}`：
  - 返回状态：`{"status": "processing" | "ready" | "error"}`
* **静态挂载**：在 `main.py` 中挂载 `runs` 目录，使 `GET /runs/{run_id}/replay.arrow` 支持 HTTP Range 请求。

---

## 3. 前端 A2 布局与“零空隙”自适应设计 (Frontend Layout)

为将视觉重心完全偏向海图态势展示，将三栏的弹性系数和自适应规则配置如下：

```
+---------------------------------------------------------------------------------------------+
|                                仿真报告：RUN-19E8                                           |
+---------------------------------------------------------------------------------------------+
|  [左栏: 55% 宽度]                   |  [中栏: 22.5% 宽度]      |  [右栏: 22.5% 宽度]        |
|  +-------------------------------+  |  +--------------------+  |  +----------------------+  |
|  |                               |  |  | 事件时间轴 (22.5%) |  |  | KPI网格 (22.5%)       |  |
|  |            地图轨迹           |  |  +--------------------+  |  +----------------------+  |
|  |            (55% 宽)           |  |  | 决策树 (22.5%)     |  |  | 诊断建议 (22.5%)       |  |
|  |                               |  |  +--------------------+  |  +----------------------+  |
|  +-------------------------------+  |  | 雷达图 (22.5%)     |  |  | ASDR 账本 (22.5%)      |  |
|  | ▶ ⏸ 进度条                   |  |  +--------------------+  |  +----------------------+  |
|  +-------------------------------+  |                          |                            |
+---------------------------------------------------------------------------------------------+
```

### 3.1 Flex 占比调整
* **左栏 (TrajectoryReplay)**：`flex: 55`，最大化海图态势范围。
* **中栏 (TimelineSixLane + ColregsDecisionTree + ScoringRadarChart)**：`flex: 22.5`
* **右栏 (Kpis + BoundaryDiagnostics + AsdrLedger)**：`flex: 22.5`

### 3.2 界面排版压缩与文字溢出控制
* **KPI 网格单元**：内部 Padding 压缩至 `3px 6px`，描述文字超出使用 `text-overflow: ellipsis` 并配合 `white-space: nowrap`。
* **ASDR 账本高度自适应与自动截断 (AsdrLedger)**：
  - 容器配置为 `flex: 1` 占满下方高度，超出部分纵向滚动。
  - 对于账本中的 `PAYLOAD` 和 `SHA-256` 字符串列，列宽固定，内容使用 `ellipsis` 截断。
  - 必须绑定 `title` 属性或 Tooltip，当鼠标悬停在截断行上时，显示完整 JSON Payload 和完整的 SHA-256 校验和。
* **避碰决策树（ColregsDecisionTree）**：
  - 节点 Padding 缩减为 `8px`，层级垂直外边距缩减为 `4px`。

---

## 4. 全局时间状态与双向联动机制 (Interactive Scrubber)

### 4.1 全局时间状态定义
在 [SimulationEvaluator.tsx](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/screens/SimulationEvaluator.tsx) 中统一定义：
```typescript
const [currentTimeSec, setCurrentTimeSec] = useState<number>(0);
```

### 4.2 双向跳转与交互行为
1. **轨迹 Scrubber 拖动**：
   - 滑动左下方的 Progress Bar，触发 `onTimeChange(t)`，即时更新 `currentTimeSec`。地图实时读取对应秒数的 OWN/T01 船位渲染。
2. **事件时间轴 (6-Lane Audit) 拖动**：
   - 拖拽时间线或点击任何带颜色的事件圆点，触发 `onScrub(t)` 改变 `currentTimeSec`。
3. **ASDR 账本行点击**：
   - 点击账本的某行记录，根据该记录的 `time`（例如 `T+00:52`）转换为秒数，触发全局状态同步跳转。
4. **决策树 (ColregsDecisionTree) 高亮**：
   - 监听 `currentTimeSec`，匹配对遇过程的时序逻辑：
     - $t \ge 0$: 激活并点亮 L1 ODD Check；
     - $t \ge 25$: 激活并点亮 L2 Encounter Classification；
     - $t \ge 49$: 激活并点亮 L3 Responsibility；
     - $t \ge 52$: 激活并点亮 L4 Maneuver；
     - $t \ge 152$: 激活并点亮 L5 Status Execution。

### 4.3 播放控制与账本自动滚动
* **播放开关**：点击 **▶/⏸** 按钮开启定时器，以 `0.1s * rate` 累加时间。
* **速率支持**：支持 $\times 0.5$、$\times 1$、$\times 2$、$\times 4$、$\times 10$ 等倍率。
* **账本自动跟随**：随着回放进度，账本数据表检测最接近的事件索引，使用 `scrollIntoView({ block: 'nearest' })` 让当前活跃的事件行始终处于表格中间高亮显示。
