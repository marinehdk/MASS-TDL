# 屏幕④：运行报告（Run Report）详细设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003-05 |
| 版本 | v2.0 |
| 日期 | 2026-05-13 |
| 父文档 | [03-sil-detailed-design.md](./03-sil-detailed-design.md) |
| 路由 | `/report/:runId` |
| 对应 D-task | D2.4, D2.5, D3.6 |

---

## 1. 页面概述

### 1.1 定位
运行报告是仿真结束后的分析界面，用于查看仿真结果、评分、KPI 指标和证据导出。

### 1.2 核心功能
- **严格的 2x2 网格设计 (Four-Quadrant Grid)**，提升信息密度和专业感
- 轨迹回放地图 (Top-Left)
- ASDR 证据链账本 (Top-Right)
- 6-Lane 时间线 (Events, faults, mode changes) (Bottom-Left)
- COLREGs 评分雷达图 (Bottom-Right)
- KPI 指标卡片横幅

---

## 2. 页面布局

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 报告头部信息：RUN REPORT · {runId}                                         │
│ [← BACK TO BRIDGE]   [↓ EXPORT ASDR]   [NEW RUN →]                          │
├─────────────────────────────────────────────────────────────────────────────┤
│ KPI 指标横幅 (Verdict, Min CPA, COLREGs, Max RUD, TOR Time, P99, Faults)    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────────────────────┐  ┌─────────────────────────────────────┐  │
│  │                              │  │                                     │  │
│  │ Top-Left:                    │  │ Top-Right:                          │  │
│  │ 轨迹回放地图 (Trajectory)     │  │ ASDR 证据链账本 (ASDR Ledger)        │  │
│  │                              │  │                                     │  │
│  └──────────────────────────────┘  └─────────────────────────────────────┘  │
│                                                                             │
│  ┌──────────────────────────────┐  ┌─────────────────────────────────────┐  │
│  │                              │  │                                     │  │
│  │ Bottom-Left:                 │  │ Bottom-Right:                       │  │
│  │ 6-Lane 时间线 (Timeline)      │  │ COLREGs 打分雷达图 (Radar Chart)     │  │
│  │                              │  │                                     │  │
│  └──────────────────────────────┘  └─────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 时间轴 Scrubber

### 3.1 组件设计

```
┌────────────────────────────────────────────────────────────────────────────┐
│ 0:00    2:00     4:00     6:00     8:00     10:00                       │
│  ├───────┼───────┼───────┼───────┼───────┼───────┤                       │
│                          ● ← 当前位置拖动块                               │
│                                                                            │
│  [⏮] [▶ Play/Pause] [⏭]    [0.5x] [1x] [2x] [4x]  ← 播放速度选择        │
└────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 功能

| 功能 | 说明 |
|------|------|
| 拖动定位 | 拖动滑块跳转到任意时刻 |
| 事件标注 | 关键事件位置显示小红点 |
| 缩放 | 支持放大/缩小时间轴 |
| 播放控制 | 播放/暂停/快进/快退 |
| 速度选择 | 0.5x / 1x / 2x / 4x |

### 3.3 交互

| 交互 | 行为 |
|------|------|
| 点击时间轴 | 跳转到点击位置 |
| 拖动滑块 | 实时更新地图和指标 |
| 双击事件点 | 跳转到事件时刻 |
| 鼠标滚轮 | 放大/缩小时间轴 |

---

## 4. 仿真回放地图

### 4.1 地图功能
与 Captain HMI 类似，但添加以下功能：

| 功能 | 说明 |
|------|------|
| 轨迹显示 | 本船 + 所有目标船历史轨迹（彩色线）|
| 事件标注 | 关键事件位置（标注图标）|
| 事件时刻跟随 | 拖动时间轴时，地图同步更新 |
| 速度矢量 | COG 推断矢量 |

### 4.2 轨迹样式

| 船舶 | 轨迹颜色 | 线宽 |
|------|---------|------|
| 本船 | 红色 | 2px |
| 目标船 1 | 蓝色 | 2px |
| 目标船 2 | 绿色 | 2px |
| 目标船 3 | 紫色 | 2px |

### 4.3 事件标注图标

| 事件类型 | 图标 | 颜色 |
|---------|------|------|
| ToR 触发 | ⚠️ | 紫色 |
| COLREG 激活 | 📋 | 黄色 |
| CPA 危险 | 🔴 | 红色 |
| 避让完成 | ✅ | 绿色 |
| 故障注入 | 💥 | 橙色 |

---

## 5. 评分与指标

### 5.1 6 维评分卡片

基于 Hagen 2022 / Woerner 2019 的 6 维评分体系：

| 维度 | 名称 | 权重 | 分数范围 | 颜色映射 |
|------|------|------|---------|----------|
| **Safety** | 安全性 | 30% | 0-1 | 绿>0.8 / 黄0.5-0.8 / 红<0.5 |
| **Rule** | 规则合规 | 20% | 0-1 | 同上 |
| **Delay** | 响应延迟 | 15% | 0-1 | 同上 |
| **Magnitude** | 动作幅度 | 10% | 0-1 | 同上 |
| **Phase** | 时机相位 | 15% | 0-1 | 同上 |
| **Plausibility** | 可信度 | 10% | 0-1 | 同上 |

### 5.2 评分卡片设计

```
┌────────────────────┐
│ Safety             │  ← 维度名称
│                    │
│       0.92         │  ← 分数（大号字体）
│                    │
│  ████████████░░░░  │  ← 进度条（对应分数百分比）
│                    │
│  权重: 30%         │  ← 权重
└────────────────────┘
     120px × 160px
```

### 5.3 综合评分

```
┌─────────────────────────────────────┐
│                                     │
│     Overall Score: 0.91             │
│                                     │
│     Verdict: ✅ PASS                │
│                                     │
│     (综合分数 ≥ 0.8 → PASS)         │
│                                     │
└─────────────────────────────────────┘
```

### 5.4 Verdict 规则

| Overall Score | Verdict | 颜色 |
|---------------|---------|------|
| ≥ 0.8 | ✅ PASS | 绿色 |
| 0.5-0.8 | ⚠️ MARGINAL | 黄色 |
| < 0.5 | ❌ FAIL | 红色 |

---

## 6. COLREGs Rule 触发链

### 6.1 可视化设计

```
┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐
│ R5   │───→│ R7   │───→│ R14  │───→│ R17  │───→│ R8   │
│Lookout│   │ Risk │   │Head-on│   │Stand-on│   │Action│
└──────┘    └──────┘    └──────┘    └──────┘    └──────┘
  ✓         ✓         ✓         ✓         ✓
 2:30       3:15       4:00       5:30       6:45
```

### 6.2 规则节点说明

| 规则 | 名称 | 激活时间 | 状态 |
|------|------|---------|------|
| R5 | Lookout | 时间戳 | ✓ 满足 / ✗ 违反 |
| R6 | Safe Speed | 时间戳 | ✓ 满足 / ✗ 违反 |
| R7 | Risk of Collision | 时间戳 | ✓ 满足 / ✗ 违反 |
| R8 | Action to Avoid | 时间戳 | ✓ 满足 / ✗ 违反 |
| R13 | Overtaking | 时间戳 | ✓ 满足 / ✗ 违反 |
| R14 | Head-on | 时间戳 | ✓ 满足 / ✗ 违反 |
| R15 | Crossing | 时间戳 | ✓ 满足 / ✗ 违反 |
| R16 | Give-way | 时间戳 | ✓ 满足 / ✗ 违反 |
| R17 | Stand-on | 时间戳 | ✓ 满足 / ✗ 违反 |
| R19 | Restricted Visibility | 时间戳 | ✓ 满足 / ✗ 违反 |

### 6.3 交互
- **点击规则节点**：展开详细信息（判断依据、本船动作、目标船状态）
- **悬停**：显示触发时间戳

---

## 7. 事件时间线

### 7.1 列表设计

```
┌─────────────────────────────────────────────────────────────────┐
│ 事件时间线                                                      │
├─────────────────────────────────────────────────────────────────┤
│ ● 3:42:01 [M8] ToR triggered                                  │
│   Reason: TDL (15s) < TMR (60s)                               │
│   Action: ToR → ROC, Reason: TDL < TMR                        │
├─────────────────────────────────────────────────────────────────┤
│ ● 5:15:23 [M6] COLREG Rule 14 activated                        │
│   Target: MMSI 123456789                                       │
│   Own-ship action: Maintain course                             │
├─────────────────────────────────────────────────────────────────┤
│ ● 7:30:45 [M2] CPA reached: 0.8nm                              │
│   TCPA: 0 min                                                  │
│   Risk level: MODERATE                                         │
├─────────────────────────────────────────────────────────────────┤
│ ● 9:45:12 [M5] Clearance confirmed                             │
│   Situation: CPA > 1.0nm, passed clear                        │
│   Decision: Resume original course                             │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 事件类型图标

| 类型 | 图标 | 颜色 |
|------|------|------|
| ToR | ⚠️ | 紫色 |
| COLREG | 📋 | 黄色 |
| CPA | 📍 | 红色/黄色/绿色 |
| Safety | 🚨 | 红色 |
| MRM | 🛑 | 橙色 |
| Clearance | ✅ | 绿色 |

---

## 8. KPI 指标卡片

### 8.1 核心 KPI

| KPI | 说明 | 计算方法 |
|-----|------|---------|
| **Min CPA** | 最近会遇距离 | min(distance(t)) |
| **Max CPA** | 最大会遇距离 | max(distance(t)) |
| **Avg SOG** | 平均航速 | mean(SOG(t)) |
| **Max ROT** | 最大转向率 | max(abs(ROT(t))) |
| **Total Distance** | 航行总距离 | ∫|v|dt |
| **Sim Duration** | 仿真时长 | t_end - t_start |

### 8.2 展示样式

```
┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐
│ Min CPA   │ │ Max ROT    │ │ Avg SOG    │ │Duration   │
│  0.8 nm   │ │  3.2°/s    │ │  11.8 kn   │ │  10:00    │
└────────────┘ └────────────┘ └────────────┘ └────────────┘
```

---

## 9. 证据导出

### 9.1 导出按钮

| 按钮 | 格式 | 说明 |
|------|------|------|
| 导出 Marzip | .marzip | CCS/DNV 证据包（推荐）|
| 导出 CSV | .csv | KPI 表格 |
| 导出 JSON | .json | 完整结果数据 |
| 导出 MCAP | .mcap | 原始 50Hz 录制 |

### 9.2 Marzip 证据包内容

```
{run_id}_evidence.marzip
├── scenario.yaml              ← maritime-schema 场景定义
├── scenario.sha256             ← SHA256 签名
├── results.arrow               ← KPI 列存表
├── results.bag.mcap            ← 完整 50Hz 录像
├── results.bag.mcap.sha256
├── asdr_events.jsonl           ← ASDR 事件流
├── verdict.json                ← PASS/FAIL + 理由 + KPI
└── manifest.yaml               ← 版本/作者/工具链/内核 SHA
```

### 9.3 导出流程

```
用户点击"导出 Marzip"
    ↓
POST /api/v1/export/marzip?run_id={runId}
    ↓
后台任务：
1. 读取 MCAP 文件
2. 提取 scoring topics → Arrow
3. 计算 KPI + Verdict
4. 读取 YAML + hash + manifest
5. 打包为 .marzip
    ↓
返回下载 URL
    ↓
前端显示"导出完成"
```

---

## 10. 状态管理（Zustand）

```typescript
// useReplayStore
interface ReplayStore {
  runId: string;
  
  // 时间轴
  currentTime: number;          // 秒
  duration: number;            // 总时长
  isPlaying: boolean;
  playbackRate: number;
  
  // 事件
  events: ReplayEvent[];
  currentEvent: ReplayEvent | null;
  
  // KPI
  kpis: KPIMetrics;
  
  // 评分
  scoring: SixDimensionScoring;
  overallScore: number;
  verdict: 'PASS' | 'MARGINAL' | 'FAIL';
  
  // 操作
  seekTo: (time: number) => void;
  play: () => void;
  pause: () => void;
  setPlaybackRate: (rate: number) => void;
  exportMarzip: () => Promise<string>;
  exportCSV: () => Promise<string>;
}
```

---

## 11. API 接口

| 接口 | 方法 | 路径 | 说明 |
|------|------|------|------|
| 运行报告 | GET | `/api/v1/runs/{runId}/report` | 获取完整报告数据 |
| KPI 数据 | GET | `/api/v1/runs/{runId}/kpi` | 获取 KPI 指标 |
| 评分数据 | GET | `/api/v1/runs/{runId}/scoring` | 获取 6 维评分 |
| 事件列表 | GET | `/api/v1/runs/{runId}/events` | 获取事件时间线 |
| 导出 Marzip | POST | `/api/v1/export/marzip` | 导出证据包 |
| 导出 CSV | GET | `/api/v1/runs/{runId}/export/csv` | 导出 KPI CSV |
| 导出 JSON | GET | `/api/v1/runs/{runId}/export/json` | 导出完整结果 |
| 轨迹数据 | GET | `/api/v1/runs/{runId}/trajectory` | 获取轨迹数据（用于回放）|