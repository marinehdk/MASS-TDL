# 屏幕③：Captain HMI（驾驶台人机界面）详细设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003-04 |
| 版本 | v2.0 |
| 日期 | 2026-05-13 |
| 父文档 | [03-sil-detailed-design.md](./03-sil-detailed-design.md) |
| 路由 | `/bridge/:runId` |
| 对应 D-task | D1.3b.3, D2.5, D2.6 |

---

## 1. 页面概述

### 1.1 定位
Captain HMI 是仿真运行期间的实时监控界面，采用 IEC 62288 SA 子集 + IMO S-Mode 关键元素设计。

### 1.2 视图模式
- **Captain 视图**（默认）：本船居中，heading-up 显示
- **God 视图**：俯瞰全场景上帝视角
- **ROC 视图**：远程操控员视角

### 1.3 核心特性
- 全屏 MapLibre GL JS 海图
- Captain HUD 叠加信息
- 仿真控制工具条（底部固定）
- Module Pulse 状态条（顶部固定）
- ASDR 折叠日志（右下角收起）

---

## 2. 页面布局

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ Module Pulse 状态条（M1-M8 GREEN/AMBER/RED，16px 高度）                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                                                                             │
│                           ENC 海图全屏显示                                  │
│                           + Captain HUD 叠加层                             │
│                           + 本船/目标船 sprite                             │
│                           + COG/SOG 矢量                                   │
│                           + CPA/TCPA 标注                                  │
│                           + 环境矢量（风/流）                               │
│                                                                             │
│  ┌──────────────────┐                                                    │
│  │  Captain HUD     │                                                    │
│  │  ┌────────────┐ │                                                    │
│  │  │ CPA: 0.8nm │ │                                                    │
│  │  │ TCPA: 8min │ │                                                    │
│  │  │ Rule: 14   │ │  ← 顶部信息条（左上角浮窗）                         │
│  │  │ Decision:  │ │                                                    │
│  │  │ STANDA-ON  │ │                                                    │
│  │  └────────────┘ │                                                    │
│  └──────────────────┘                                                    │
│                                                                             │
│                                                    ┌──────────────────────┐│
│                                                    │      ASDR 日志       ││
│                                                    │   （可折叠收起）      ││
│                                                    │                      ││
│                                                    │  13:42:01 [M8] ToR   ││
│                                                    │  13:42:05 [M6] R14   ││
│                                                    │  13:42:10 [M7] ALERT ││
│                                                    └──────────────────────┘│
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│ 仿真控制工具条（详见 06-simulation-control-toolbar.md）                       │
│ ▶ ⏸ ⏹ ⏮  ──●────────── 3:42/10:00 ── 2x ▼  [注入故障] [重置]             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. ENC 海图显示

### 3.1 地图引擎
- **引擎**：MapLibre GL JS v4.x
- **渲染方式**：WebGL 矢量渲染（S-57 MVT）
- **性能目标**：1000+ vessel @60FPS

### 3.2 地图类型切换

| 图层 | 数据源 | 快捷键 |
|------|--------|--------|
| ENC 矢量海图 | S-57/S-63 MVT | `E` |
| 卫星底图 | Mapbox/ESRI Satellite | `S` |
| OpenSeaMap | OSM Marine | `O` |
| 地形起伏 | DEM | `T` |

**图层切换**：右上角浮窗，点击图标切换

### 3.3 坐标系
- **Captain 视图**：heading-up（默认），本船居中
- **God 视图**：north-up，俯瞰全场景
- **相对运动**：支持（用于特殊场景）

---

## 4. Captain HUD（抬头显示）

### 4.1 位置与布局
左上角浮窗，采用高端玻璃拟态（`.glass-panel`）背景，带圆角边框，确保与海图背景的视觉融合。

### 4.2 显示内容

| 信息 | 数据源 | 格式 | 颜色 |
|------|--------|------|------|
| **CPA** | M2 world_model | X.X nm | 白色 |
| **TCPA** | M2 world_model | XX min | 白色 |
| **COLREG Rule** | M6 colregs_reasoner | "Rule 14" | 规则对应色 |
| **Decision** | M6 colregs_reasoner | "STAND-ON" / "GIVE-WAY" | 绿/黄 |
| **本船 SOG** | own_ship_state | XX.X kn | 白色 |
| **本船 COG** | own_ship_state | XXX.X° | 白色 |
| **ROT** | own_ship_state | ±X.X°/s | 正常白色/超限红色 |

### 4.3 样式规范

```
┌─────────────────────────────┐
│  CPA    0.8 nm             │
│  TCPA   8 min              │
│  ─────────────────────────  │
│  Rule   14 (Head-on)       │
│  Decision  STAND-ON        │
│  ─────────────────────────  │
│  SOG    12.0 kn            │
│  COG    045.0°             │
│  ROT    +2.1°/s            │
└─────────────────────────────┘
          │
          └── 240px × auto，padding 12px
```

---

## 5. 船舶显示

### 5.1 本船 sprite（IEC 62288 S-Mode）

| 元素 | 说明 | 样式 |
|------|------|------|
| 船体符号 | heading-up 三角 | 红色实线，20px |
| COG 矢量 | 6分钟推断线 | 红色虚线，带箭头 |
| 船名标注 | 本船标识 | 红色文字，本船上方 |
| 航迹 | 历史轨迹 | 红色点线，保留最近 5min |

### 5.2 目标船 sprite

| 元素 | 说明 | 样式 |
|------|------|------|
| 船体符号 | 蓝色三角 | 蓝色实线，16px |
| COG 矢量 | 目标船航向 | 蓝色虚线 |
| MMSI 标注 | 目标船标识 | 蓝色文字 |
| CPA 圈 | 最近会遇距离 | 黄色虚线圆 |
| 危险标注 | CPA < 阈值 | 红色闪烁 |

### 5.3 PPI 距离圈

| 功能 | 说明 |
|------|------|
| 距离显示 | 以本船为中心，1nm / 2nm / 5nm 圈 |
| 样式 | 白色虚线圆，标注距离 |

---

## 6. 相对方位罗盘

### 6.1 位置
屏幕左上角（或 Captain HUD 内），方位罗盘 360° 显示。

### 6.2 显示内容

```
         N
         │
    315 ┌┴┐ 45
    W ──┤  ├── E
        │
    225 └──/ 135
         S
         │
    ← 本船 heading 指示
```

### 6.3 样式
- 圆形罗盘，120px 直径
- 方位刻度，每 30° 标注
- 本船 heading 红色指针
- 目标船方位彩色点标注

---

## 7. 环境矢量显示

### 7.1 位置
右上角浮窗或叠加在海图上。

### 7.2 显示内容

| 环境要素 | 图标 | 单位 | 颜色 |
|---------|------|------|------|
| 风 | → 风向箭头 | XX m/s | 青色 |
| 流向 | → 流向箭头 | XX m/s | 蓝色 |
| 能见度 | 文字 | >2nm / 1-2nm / <1nm | 绿/黄/红 |
| 海况 | Beaufort | X 级 | 对应色 |

---

## 8. Module Pulse 状态条

### 8.1 位置
页面最顶部，固定 16px 高度。

### 8.2 显示方式

```
┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
│🟢│🟢│🟢│🟢│🟢│🟢│🟢│🟢│ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │
└─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 M1  M2  M3  M4  M5  M6  M7  M8         [间距]
```

### 8.3 状态颜色

| 状态 | 颜色 | 说明 |
|------|------|------|
| GREEN | 🟢 绿色 | 正常，< 50ms |
| AMBER | 🟡 黄色 | 轻微延迟，50-100ms |
| RED | 🔴 红色 | 严重延迟，> 100ms 或超时 |
| UNKNOWN | ⚪ 灰色 | 未收到消息 |

### 8.4 悬停信息
鼠标悬停显示详细信息：
- 模块名称
- 最新消息延迟（ms）
- 消息丢失数
- 错误码（如有）

---

## 9. ASDR 折叠日志

### 9.1 位置
右下角，可折叠/展开。

### 9.2 展开状态

```
┌─────────────────────────────────────┐
│ ASDR 日志                    [−]    │  ← 收起按钮
├─────────────────────────────────────┤
│ 13:42:01.234 [M8] ToR → ROC        │
│              Reason: TDL < TMR      │
│ 13:42:01.567 [M8] ToR → 船长        │
│              Reason: Operator req    │
│ 13:42:05.123 [M6] R14 activated     │
│              Target: MMSI 123456789 │
│ 13:42:10.456 [M7] Safety Alert     │
│              Level: WARNING         │
│              Msg: ODD boundary edge  │
│ 13:42:15.789 [M5] Route Replan      │
│              Trigger: ODD change    │
└─────────────────────────────────────┘
```

### 9.3 收起状态

```
┌─────────────────────────────────────┐
│ ASDR 日志              [+ 展开]     │
└─────────────────────────────────────┘
  ↑ 24px 高度
```

### 9.4 日志级别颜色

| 级别 | 颜色 | 触发模块 |
|------|------|---------|
| INFO | 白色 | 所有模块 |
| WARNING | 黄色 | M6, M7 |
| ALERT | 红色 | M7, M8 |
| ToR | 紫色 | M8 |

---

## 10. 视图切换

### 10.1 切换方式
顶部工具栏或快捷键：

| 视图 | 快捷键 | 说明 |
|------|--------|------|
| Captain | `C` | 本船居中，heading-up（默认）|
| God | `G` | north-up，俯瞰 |
| ROC | `R` | ROC 操作员视角 |

### 10.2 视图差异

| 特性 | Captain | God | ROC |
|------|---------|-----|-----|
| 中心点 | 本船 | 场景中心 | 操作员位置 |
| 朝向 | heading-up | north-up | 可配置 |
| 显示范围 | 本船周围 5nm | 全场景 | 全场景 |
| HUD 显示 | 详细 | 简化 | 简化 + ROC 状态 |

---

## 11. 状态管理（Zustand）

```typescript
// useTelemetryStore (50Hz 订阅)
interface TelemetryStore {
  // 50Hz 数据
  ownShip: OwnShipState;
  targets: TrackedTarget[];
  environment: EnvironmentState;
  
  // 模块状态
  modulePulse: ModulePulse[];
  
  // ASDR 日志
  asdrEvents: ASDREvent[];
  
  // 时间
  simTime: Time;
  wallTime: Time;
  simRate: number;
  
  // 操作
  subscribeToTopics: () => void;
  unsubscribeFromTopics: () => void;
}

// useControlStore
interface ControlStore {
  simRate: number;          // 0.5, 1, 2, 4, 10, 20, 50
  isPaused: boolean;
  faultsActive: Fault[];
  
  setRate: (rate: number) => Promise<void>;
  pause: () => Promise<void>;
  resume: () => Promise<void>;
  stop: () => Promise<void>;
  reset: () => Promise<void>;
  injectFault: (type: FaultType) => Promise<void>;
}

// useUIStore
interface UIStore {
  viewMode: 'captain' | 'god' | 'roc';
  mapLayers: MapLayer[];
  hudVisible: boolean;
  asdrLogExpanded: boolean;
  compassVisible: boolean;
  
  setViewMode: (mode: ViewMode) => void;
  toggleMapLayer: (layer: MapLayer) => void;
  toggleASDRLog: () => void;
}
```

---

## 12. API 接口

| 接口 | 方法 | 路径 | 说明 |
|------|------|------|------|
| 50Hz 遥测 | WebSocket | `ws://host/ws/telemetry` | foxglove_bridge 推送 |
| 仿真控制 | WebSocket | `ws://host/ws/control` | 速率/暂停控制 |
| 模块状态 | WebSocket | `ws://host/ws/modules` | M1-M8 health |
| ASDR 日志 | WebSocket | `ws://host/ws/asdr` | 事件流 |
| 注入故障 | WS/REST | `/api/v1/fault/trigger` | 触发故障 |
| 设置速率 | WS/REST | `/api/v1/sim_clock/set_rate` | 设置仿真速率 |
| 暂停 | WS/REST | `/api/v1/lifecycle/deactivate` | 暂停仿真 |