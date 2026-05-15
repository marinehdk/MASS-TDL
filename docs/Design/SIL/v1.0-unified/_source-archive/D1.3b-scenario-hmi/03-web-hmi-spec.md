# D1.3b.3 · Web HMI + ENC + foxglove_bridge · 执行 Spec

**版本**：v1.1
**日期**：2026-05-11（v1.0 → v1.1: 对齐 HMI Design Spec v1.0 视觉令牌体系 + 三层演进路线）
**作者**：M8 前端负责人 + 技术负责人（brainstorming 产出，下一步走 `/writing-plans`）
**v3.1 D 编号**：D1.3b.3
**Owner**：M8 前端负责人（React + MapLibre GL）+ 技术负责人（ROS2 foxglove_bridge + 架构审）
**完成日期**：6/15 DEMO-1 partial / 7/15 完整
**工时预算**：5.0 人周（DEMO-1 partial ~1.0pw / Phase 2 增项 ~4.0pw）
**关闭 finding**：HMI G P1-G-x（NEW v3.1）

---

## 0. 决策记录（brainstorming 锁定，不可在 D1.3b.3 内再议）

| 决策 | 内容 | 来源 |
|---|---|---|
| **双 WebSocket 架构** | foxglove_bridge (8765, nav data) + FastAPI /ws/sil_debug (8000, decision data) — Option A | 架构 F.9 + SIL DR §9 + nlm-research foxglove_bridge 2026-05-11 |
| **频率节流** | 三层: ROS2 topic_tools throttle (50→10Hz) + Protobuf binary (DEMO-2) + React rAF throttle (always) | nlm-research topic_tools 2026-05-11 |
| **ENC 数据源** | Trondheim: Trondelag.gdb (ESRI File Geodatabase, colav-simulator) — 177K+ features; SF Bay: NOAA ENC (下周提供凭证) | ogrinfo 实测 2026-05-11 |
| **ENC 管线** | GDAL OpenFileGDB → reproject EPSG:4326 → GeoJSONSeq → Tippecanoe → MVT .pbf tiles | 架构 F.9 + GDAL 实测 |
| **前端框架** | React 18 + TypeScript + Vite + MapLibre GL JS 4.x — 用户决策 Q2.5.c | SIL DR §9 |
| **foxglove_bridge 安装** | `apt install ros-humble-foxglove-bridge` + `apt install ros-humble-topic-tools` | ROS Humble docs 实测 |
| **浏览器 ROS2 库** | roslibjs-foxglove (npm, tier4 fork) — JSON mode for DEMO-1 | GitHub tier4/roslibjs-foxglove |
| **JSON vs Protobuf** | JSON for DEMO-1 (dev speed + debug), Protobuf for DEMO-2 (binary, ~5 KB/s with 50 vessels) | foxglove.dev blog + Auth0 benchmark |
| **FastAPI static serving** | `StaticFiles` mount React build → port 8000, 不另起 nginx/Node.js | G P1-G-5 铁律 |

### 0.1 与现有 HMI 设计资产的继承关系

D1.3b.3 不是独立于项目 HMI 体系的设计——它复用并实现以下已有规范的**子集**：

| 资产 | 路径 | 关系 |
|---|---|---|
| **HMI Design Spec v1.0** | `docs/Design/Detailed Design/HMI-Design/MASS_TDL_HMI_Design_Spec_v1.0.md` | **权威视觉规范**。D1.3b.3 的 CSS tokens、字体分层、颜色编码直接引用 Design Spec §8，不做独立版本 |
| **Bridge HMI v0.1** | `docs/Design/Detailed Design/HMI-Design/MASS_TDL_Bridge_HMI.html` | **视觉参考实现**。v0.1 是完整的 SVG 桥楼 HMI；D1.3b.3 在 MapLibre GL 上实现其 SA subset |
| **L3_TDL_SIL_Interactive.html** | `docs/Design/Architecture Design/` | **信息内容基线**。D1.3b.3 复现其侧面板信息块（CPA/TCPA/Rule/Decision/Pulse/ASDR），视觉风格升级为 Design Spec 规范 |

### 0.2 三层演进路线（不推翻重来）

DEMO-1 建地基，代码跨阶段持久。后续只加组件不重构布局骨架：

```
DEMO-1 (6/15)          DEMO-2 (7/31)          D3.4 (8/31)
═════════════════      ═════════════════       ═════════════════
React+Vite+MapLibre     ── 持久 ──▶            ── 持久 ──▶
CSS tokens (Night)      +Day/Dusk 模式         +Bright 模式
ENC Trondheim tiles     +SF Bay tiles           —
foxglove_bridge JSON    +Protobuf               —
Own/Target (2 vessels)  +50 vessels             —
CPA ring + COG vector   —                       —
Side panel (6 cards)    —                       +SAT-2 自然语言摘要
useThrottledTopic       adjust maxFps           —
Grounding hazard        ← NEW                  —
Arrow replay+scrubber   ← NEW                  —
Puppeteer GIF batch     ← NEW                  —
Trajectory ghosting                             ← NEW
ToR countdown panel                             ← NEW
M7 verdict badge                                ← NEW
Captain/Operator 双视图                          ← NEW
ODD badge + TMR/TDL                             ← NEW
Conning Bar 五仪表                               ← NEW
```

**关键原则**：DEMO-1 写的每个 React 组件必须能原样用到 D3.4。后续只替换/扩展组件内部，不碰布局。

---

## 1. 现有原型视觉信息块审计

### 1.1 源文件

`docs/Design/Architecture Design/L3_TDL_SIL_Interactive.html` Section 3 (HMI Demo, `#s3`)

### 1.2 元素 → MapLibre 映射表

#### Map 元素（MapLibre GL 图层）

| # | 原型元素 | 原型实现 | MapLibre 等效 | 数据源 | DEMO-1 |
|---|---|---|---|---|---|
| M1 | ENC 底图 | 无（空白 SVG 圆） | MapLibre raster/symbol tiles (S-57 MVT) | ENC tiles (static) | **必** |
| M2 | Own-ship sprite | 蓝色三角形 + 艏向线 | `symbol` layer, custom icon + `icon-rotate: heading_deg` | foxglove: `/nav/filtered_state_viz` | **必** |
| M3 | Target ship sprite | 红色三角形 + 航向线 | `symbol` layer, 同 icon, `icon-color: #e24b4a` | foxglove: `/world_model/tracks_viz` | **必** |
| M4 | CPA ring | 虚线红圈 (SVG `<circle>`) | `circle` layer, `circle-radius: CPA_m / meters_per_pixel` | foxglove: tracks CPA field | **必** |
| M5 | COG/SOG vector | 蓝色航向线 (SVG `<line>`) | `line` layer, LineString from GPS pos to predicted(pos + sog*60s) | foxglove: `/nav/filtered_state_viz` | **必** |
| M6 | Grounding hazard | 无 | `fill` layer, DEPARE water_depth < vessel_draft → red highlight | ENC tiles (depth_areas layer) | Phase 2 |
| M7 | Avoidance arc | 蓝色弧线 (SVG `<path>`) | `line` layer, M5 BC-MPC heading sweep arc | foxglove + M5 output | Phase 2 |

#### 侧面板元素（React 组件）

| # | 原型元素 | 原型实现 | React 组件 | 数据源 | DEMO-1 |
|---|---|---|---|---|---|
| P1 | CPA 数值 | 大号 mono 数字, 颜色编码(绿>0.5/橙0.3-0.5/红<0.3nm) | `CpaPanel` | `/ws/sil_debug` → CPA | **必** |
| P2 | TCPA 数值 | 中号 mono 数字, 秒 | `TcpaPanel` | `/ws/sil_debug` → TCPA | **必** |
| P3 | M6 COLREGs 分类 | mono 文字, 例 "R14 正遇检测" | `RulePanel` | `/ws/sil_debug` → rule_text | **必** |
| P4 | L3 系统决策 | 动作文字, 例 "M5 BC-MPC 右转10°" | `DecisionPanel` | `/ws/sil_debug` → decision_text | **必** |
| P5 | M1-M8 pulse dots | 8 个圆点, 激活=绿色填充 | `ModulePulseBar` | `/ws/sil_debug` → module_status[] | **必** |
| P6 | ASDR 证据日志 | 滚动日志, mono 字体, 80px 高 | `AsdrLog` | `/ws/sil_debug` → asdr_events[] | **必** |
| P7 | 场景选择器 | 行内按钮组 | `ScenarioSelector` | REST `/sil/scenario/list` | 有则优 |
| P8 | Play/Reset 控件 | 按钮 | `PlaybackControls` | REST `/sil/scenario/run` | 有则优 |

### 1.3 视觉设计令牌（对齐 HMI Design Spec v1.0 §8）

D1.3b.3 的 CSS 变量体系、字体分层、颜色编码**直接引用** `MASS_TDL_HMI_Design_Spec_v1.0.md` §8 作为唯一权威来源，不做独立副本。

**DEMO-1 启用模式**：Night（默认），`data-theme="night"`

**核心色板（从 Design Spec §8.1 提取，MapLibre 海图专用）：**

```css
/* ── 海图层 —— */
--color-water-deep:    #0d2b4a;   /* 深海 */
--color-water-med:     #12406b;   /* 中深度 */
--color-water-shallow: #1a5c8a;   /* 浅水 */
--color-land:          #1e2d1e;   /* 陆地填充 */
--color-coast:         #2a4020;   /* 海岸线 */

/* ── 符号层 —— 对齐 Design Spec 语义色 —— */
--color-phos:   #5BC0BE;   /* phosphor 青 — 本船/雷达航迹 */
--color-stbd:   #3FB950;   /* 右舷绿 — 安全/通过 */
--color-port:   #F26B6B;   /* 左舷红 — 危险/威胁 (Give-Way) */
--color-warn:   #F0B72F;   /* 警告琥珀 — Stand-On 关系 */
--color-info:   #79C0FF;   /* 信息蓝 — 安全目标 */
--color-magenta:#D070D0;   /* 预测路径 (MID-MPC trajectory ghosting, D3.4) */

/* ── 侧面板 —— 对齐 Design Spec 背景/文本 —— */
--bg-0:  #070C13;    --bg-1:  #0B1320;    --bg-2:  #101B2C;
--txt-0: #F1F6FB;    --txt-1:  #C5D2E0;    --txt-2:  #8A9AAD;    --txt-3: #566578;
--line-1:#1B2C44;    --line-2: #243C58;
```

**字体分层（Design Spec §8.2）：**

```css
--fnt-disp: 'Saira Condensed', 'Noto Sans SC', sans-serif;  /* 标签/标题 */
--fnt-mono: 'JetBrains Mono', ui-monospace, monospace;       /* 数字/数值 */
--fnt-body: 'Noto Sans SC', 'Saira Condensed', sans-serif;   /* 正文/摘要 */
```

**间距与圆角（Design Spec §8.3）：**

```css
--radius-none: 0;     /* 面板/按钮/卡片 — 船桥工业风格，禁止圆角 ≥4px */
--radius-min:  2px;   /* 小型徽章/标签 */
```

**DEMO-2 追加模式**：`data-theme="day"` / `data-theme="dusk"`，仅加 CSS 变量不改造型。
**D3.4 追加模式**：`data-theme="bright"`（强光甲板）。

---

## 2. 技术架构

### 2.1 端口分工与前端连接拓扑

```
Browser (React + MapLibre GL)
  │
  ├─ ws://localhost:8765  ── foxglove_bridge (C++, Protobuf-capable)
  │     Topics subscribed: /nav/filtered_state_viz, /world_model/tracks_viz
  │     DEMO-1: JSON text frames (default)
  │     DEMO-2: Protobuf binary frames (switch)
  │
  ├─ ws://localhost:8000/ws/sil_debug ── FastAPI SilDebugSchema push
  │     Fields: CPA, TCPA, rule_text, decision_text, module_status[], asdr_events[]
  │
  └─ http://localhost:8000 ── FastAPI
        GET  /              → React build static files (production)
        GET  /sil/scenario/list      → scenarios/ dir listing
        POST /sil/scenario/run       → trigger batch_runner.py
        GET  /sil/scenario/status/{id}
        GET  /sil/report/latest      → reports/coverage_*.html
```

### 2.2 React Build Static 服务方案

```python
# web_server/app.py — 追加 (不改已有端点)
from fastapi.staticfiles import StaticFiles

WEB_BUILD = Path(__file__).resolve().parents[4] / "web" / "dist"

def create_app(cors_origins: list[str]) -> FastAPI:
    app = FastAPI(...)
    # ... existing routers ...
    if WEB_BUILD.exists():
        app.mount("/", StaticFiles(directory=str(WEB_BUILD), html=True), name="web")
    return app
```

开发模式: Vite dev server (port 5173) proxy → FastAPI (8000) + foxglove_bridge (8765)
生产模式: FastAPI 直接 serve React build (port 8000), 无需 Vite

### 2.3 ENC MVT Tiles 服务方案

Tiles 作为 `web/public/tiles/` 下的静态文件，Vite/React 构建时自动复制到 `dist/` 目录。
FastAPI `StaticFiles` mount 同时 serve tile .pbf 文件，无需独立 tile server。

Tile URL 模板: `http://localhost:8000/tiles/trondheim/{z}/{x}/{y}.pbf`

---

## 3. ENC 数据转换管线

### 3.1 源数据

| 区域 | 数据源 | 格式 | 路径 | 状态 |
|---|---|---|---|---|
| Trondheim Fjord | Kartverket Norway | ESRI File Geodatabase (.gdb) | `/Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb` | ✅ 已有 |
| San Francisco Bay | NOAA ENC (RNCE) | S-57 (.000) | TBD (下周提供凭证) | ⏳ 待下载 |

### 3.2 关键图层

从 Trondelag.gdb 提取以下图层（挪威语 → S-57 等效 → 用途）：

| 图层名 | S-57 等效 | 几何类型 | 要素数 | 用途 |
|---|---|---|---|---|
| `landareal` | LNDARE | Multi Polygon | 34,664 | 陆地填充 |
| `kystkontur` | COALNE | Multi Line String | 97,002 | 海岸线 |
| `dybdeareal` | DEPARE | Multi Polygon | 45,610 | 水深区域 + 搁浅告警 |
| `dybdekurve` | DEPCNT | Multi Line String | — | 等深线 |
| `fareomrade` | — | Multi Polygon | — | 危险区域 |
| `grunne` | UWTROC | Point | — | 浅滩/礁石 |
| `skjer` | UWTROC | Point | — | 明礁 |
| `kaibrygge` | — | Multi Line String | — | 码头 |

### 3.3 转换命令

```bash
# 1. 安装工具
brew install gdal tippecanoe   # macOS
sudo apt install gdal-bin tippecanoe  # Ubuntu

# 2. 导出图层 → GeoJSONSeq (newline-delimited, 内存友好)
mkdir -p /tmp/enc_tiles
for layer in landareal kystkontur dybdeareal fareomrade grunne skjer kaibrygge; do
  ogr2ogr -f GeoJSONSeq -t_srs EPSG:4326 \
    /tmp/enc_tiles/trondelag_${layer}.geojsons \
    /Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb \
    $layer
done

# 3. 合并 → MVT (Z6-Z14, simplification for lower zooms)
cat /tmp/enc_tiles/trondelag_*.geojsons > /tmp/enc_tiles/trondelag_all.geojsons
tippecanoe -Z6 -z14 \
  --drop-densest-as-needed \
  --simplification=4 \
  --maximum-zoom=14 \
  --layer=enc \
  -o /tmp/trondheim.mbtiles \
  /tmp/enc_tiles/trondelag_all.geojsons

# 4. 提取为目录结构
mb-util /tmp/trondheim.mbtiles web/public/tiles/trondheim/

# 产物: web/public/tiles/trondheim/{z}/{x}/{y}.pbf
```

### 3.4 预估

| 指标 | Trondelag.gdb | SF Bay (预估) |
|---|---|---|
| 转换时间 | ~2-5 min | ~1-3 min |
| .mbtiles 大小 | ~10-30 MB | ~5-15 MB |
| 浏览器加载 | <2s (CDN/本地) | <2s |
| Zoom 范围 | Z6 (挪威全景) → Z14 (码头级) | Z8 → Z16 |

---

## 4. MapLibre GL 组件设计

### 4.1 Map 初始化

```typescript
// Trondheim Fjord demo viewport
const TRONDHEIM_CENTER: [number, number] = [10.395, 63.435]; // [lng, lat]
const TRONDHEIM_ZOOM = 11;

const map = new maplibregl.Map({
  container: 'map',
  style: {
    version: 8,
    sources: {
      enc: {
        type: 'vector',
        tiles: [`${window.location.origin}/tiles/trondheim/{z}/{x}/{y}.pbf`],
        minzoom: 6,
        maxzoom: 14,
      },
    },
    layers: [
      // Layer order: bottom → top
      { id: 'enc-depth',    source: 'enc', 'source-layer': 'enc', type: 'fill', filter: [...] },
      { id: 'enc-land',     source: 'enc', 'source-layer': 'enc', type: 'fill', filter: [...] },
      { id: 'enc-coast',    source: 'enc', 'source-layer': 'enc', type: 'line', filter: [...] },
      { id: 'own-track',    source: 'own-track',    type: 'line' },
      { id: 'cpa-ring',     source: 'cpa-ring',     type: 'circle' },
      { id: 'target-ships', source: 'target-ships', type: 'symbol' },
      { id: 'own-ship',     source: 'own-ship',     type: 'symbol' },
    ],
  },
  center: TRONDHEIM_CENTER,
  zoom: TRONDHEIM_ZOOM,
});
```

### 4.2 Own-ship Layer（对齐 Design Spec §4.2 + Bridge HMI phosphor 色）

```typescript
// GeoJSON source, updated per frame from foxglove_bridge data
const ownShipSource: GeoJSON.Feature = {
  type: 'Feature',
  geometry: { type: 'Point', coordinates: [lng, lat] },
  properties: {
    heading_deg: 0,   // for icon-rotate
    sog_kn: 18.0,     // for COG vector length
    cog_deg: 0,       // for COG vector direction
  },
};

// MapLibre symbol layer config
{
  id: 'own-ship',
  type: 'symbol',
  source: 'own-ship',
  layout: {
    'icon-image': 'ship-own',         // custom sprite: 正三角 + 艏向线, color: --color-phos (#5BC0BE)
    'icon-rotate': ['get', 'heading_deg'],
    'icon-rotation-alignment': 'map',
    'icon-size': 0.08,
  },
}
```

Custom sprite 规范（对齐 Design Spec §4.2）：
- 形状：正三角（顶点朝艏向），填充 `#5BC0BE` (--color-phos)，边框 1px
- 艏向线：自顶点延伸，长度 = SOG × 6min 比例，同色
- 尺寸：14px 边长 @ zoom 11
- 标签："FCB-01" + "HDG 005° · 18.4 kn"，Saira Condensed 11px

### 4.3 Target Ship Layer（对齐 Design Spec §4.3 COLREGs 颜色编码）

```typescript
// GeoJSON FeatureCollection, updated per frame
{
  id: 'target-ships',
  type: 'symbol',
  source: 'target-ships',
  layout: {
    'icon-image': 'ship-target',      // same triangle sprite
    'icon-rotate': ['get', 'heading_deg'],
    'icon-rotation-alignment': 'map',
    'icon-size': 0.06,
  },
  paint: {
    'icon-color': [
      'match', ['get', 'colreg_role'],
      'give-way',  '#F26B6B',   // --color-port  (Rule 16 GIVE_WAY — 本船让路)
      'stand-on',  '#F0B72F',   // --color-warn  (Rule 17 STAND_ON — 本船直航)
      'overtaking','#79C0FF',   // --color-info  (Rule 13 OVERTAKING)
      'safe',      '#79C0FF',   // --color-info  (安全目标)
      '#79C0FF',                // default fallback
    ],
  },
}
```

COLREGs 角色颜色编码（Design Spec §4.3 强制）：
| 角色 | 颜色 | CSS 变量 |
|---|---|---|
| Give-Way 本船（Rule 16） | 红 | `--color-port` (#F26B6B) |
| Stand-On 本船（Rule 17） | 琥珀 | `--color-warn` (#F0B72F) |
| Overtaking（Rule 13） | 蓝 | `--color-info` (#79C0FF) |
| 安全目标 | 蓝 | `--color-info` |

置信度显示规则（Design Spec §4.3）：
- confidence ≥ 0.9：实心三角
- 0.7 ≤ confidence < 0.9：空心三角（仅边框）
- confidence < 0.7：不显示目标

### 4.4 CPA Ring Layer

```typescript
{
  id: 'cpa-ring',
  type: 'circle',
  source: 'cpa-ring',
  paint: {
    'circle-radius': ['/', ['get', 'cpa_m'], 10], // scale: meters → pixels (~1:10 at zoom 11)
    'circle-color': '#F26B6B',        // --color-port (危险)
    'circle-opacity': 0.4,
    'circle-stroke-width': 1,
    'circle-stroke-color': '#F26B6B', // --color-port
    'circle-stroke-dasharray': [3, 2], // dashed (IMO 惯例)
  },
}
```

### 4.5 COG/SOG Vector Layer

```typescript
// LineString from own-ship pos to predicted position (pos + COG direction × SOG × 60s)
{
  id: 'own-track',
  type: 'line',
  source: 'own-track',
  paint: {
    'line-color': '#5BC0BE',   // --color-phos (phosphor 青)
    'line-width': 1.5,
    'line-dasharray': [4, 2],  // dashed (区别于 heading line 实线)
  },
}
```

### 4.6 Grounding Hazard（Phase 2）

```typescript
{
  id: 'grounding-hazard',
  type: 'fill',
  source: 'enc',
  'source-layer': 'enc',
  filter: ['all',
    ['==', ['get', '_layer'], 'dybdeareal'],
    ['<', ['get', 'depth_m'], 3.0],  // draft of FCB ~2.1m + margin
  ],
  paint: { 'fill-color': '#F85149', 'fill-opacity': 0.3 },  // --color-danger (严重危险)
}
```

---

## 5. foxglove_bridge → MapLibre 数据流

### 5.1 订阅 Topic 清单

| ROS2 Topic | 频率 | 内容 | 前端使用 |
|---|---|---|---|
| `/nav/filtered_state_viz` | 10 Hz (throttled from 50Hz) | Own-ship lat/lon/sog/cog/heading | Own-ship symbol + COG vector |
| `/world_model/tracks_viz` | 2 Hz | Target[] — lat/lon/sog/cog/heading/CPA/mmsi | Target symbols + CPA ring |

### 5.2 foxglove_bridge 配置

```xml
<!-- launch/foxglove_sil.launch.xml -->
<launch>
  <!-- Throttle nodes -->
  <node pkg="topic_tools" exec="throttle" name="throttle_nav">
    <param name="input_topic" value="/nav/filtered_state"/>
    <param name="output_topic" value="/nav/filtered_state_viz"/>
    <param name="message_type" value="l3_msgs/msg/FilteredOwnShipState"/>
    <param name="rate" value="10.0"/>
  </node>
  <node pkg="topic_tools" exec="throttle" name="throttle_tracks">
    <param name="input_topic" value="/world_model/tracks"/>
    <param name="output_topic" value="/world_model/tracks_viz"/>
    <param name="message_type" value="l3_msgs/msg/TrackedTargetArray"/>
    <param name="rate" value="2.0"/>
  </node>

  <!-- foxglove_bridge -->
  <include file="$(find-pkg-share foxglove_bridge)/launch/foxglove_bridge_launch.xml">
    <arg name="port" value="8765"/>
    <arg name="topic_whitelist" value='[".*_viz", "/l3/.*"]'/>
  </include>
</launch>
```

### 5.3 前端 roslibjs-foxglove 消费

```typescript
// hooks/useFoxgloveBridge.ts
import ROSLIB from 'roslibjs-foxglove';

const ros = new ROSLIB.Ros({ url: 'ws://localhost:8765' });

ros.on('connection', () => {
  // Subscribe to throttled nav data
  const navTopic = new ROSLIB.Topic({
    ros,
    name: '/nav/filtered_state_viz',
    messageType: 'l3_msgs/msg/FilteredOwnShipState',
  });
  navTopic.subscribe((msg) => {
    // msg contains: lat, lon, sog, cog, heading
    updateOwnShip(msg);
  });

  // Subscribe to throttled tracks
  const tracksTopic = new ROSLIB.Topic({
    ros,
    name: '/world_model/tracks_viz',
    messageType: 'l3_msgs/msg/TrackedTargetArray',
  });
  tracksTopic.subscribe((msg) => {
    updateTargets(msg.targets);
  });
});
```

### 5.4 React 渲染节流

```typescript
// hooks/useThrottledTopic.ts
function useThrottledTopic<T>(callback: (data: T) => void, maxFps: number = 30) {
  const lastUpdate = useRef(0);
  const minInterval = 1000 / maxFps;

  return useCallback((data: T) => {
    const now = performance.now();
    if (now - lastUpdate.current >= minInterval) {
      lastUpdate.current = now;
      callback(data);
    }
  }, [callback, minInterval]);
}
```

MapLibre GeoJSON source 更新通过 `source.setData(geojson)` 完成，不在 React render 循环中触发（直接操作 MapLibre 实例）。

---

## 6. DEMO-1 场景数据链路（不依赖 D1.3b.1）

### 6.1 组件链

```
sil_mock_node (synthetic mode)
  └─ /nav/filtered_state @50Hz (own-ship: FCB 63.0°N 10.3°E, COG 0°, SOG 18kn)
  └─ /l3/sat/data @1Hz (SAT-1/2/3 stub)
  └─ /l3/m1/odd_state @1Hz (ODD zone=A)
      │
      ▼
topic_tools throttle: 50Hz → 10Hz (/nav/filtered_state_viz)
      │
      ▼
foxglove_bridge (port 8765, JSON mode)
      │ ws://localhost:8765
      ▼
Browser: roslibjs-foxglove → MapLibre setData() → render
      │
      ├─ Own-ship: lat/lon → MapLibre Point → symbol layer (icon-rotate: heading)
      └─ COG/SOG: lat/lon → LineString([pos, predicted_pos]) → line layer
```

### 6.2 Target Ship（人工注入）

DEMO-1 阶段 `/world_model/tracks` 可能无真实数据。前端在 MapLibre 中创建 1 个静态 target ship GeoJSON 对象：

```typescript
// DEMO-1 hardcoded target: R14 head-on encounter
const DEMO_TARGET = {
  type: 'Feature',
  geometry: { type: 'Point', coordinates: [10.395, 63.458] }, // ~2.5nm north of FCB
  properties: { heading_deg: 180, sog_kn: 10, mmsi: 'DEMO001' },
};
```

Phase 2 (`/world_model/tracks` 有真实数据后) 删除此硬编码。

### 6.3 DEMO-1 现场 SOP（5 分钟）

```
T+0:00   ros2 launch foxglove_sil.launch.xml
         → topic_tools throttle + foxglove_bridge 启动
T+0:10   python web_server/app.py
         → FastAPI 启动 (port 8000 with /ws/sil_debug)
T+0:15   cd web && npm run dev  (开发模式) 或 打开 http://localhost:8000 (生产模式)
         → 浏览器显示 Trondheim Fjord ENC 底图
T+0:20   浏览器自动连接 ws://localhost:8765 (foxglove_bridge)
         → Own-ship (FCB) 出现在 Trondheim 峡湾中心 (63.435°N 10.395°E)
         → 1 个 Target ship (R14 head-on) 出现在北方 ~2.5nm
T+0:25   CPA ring 显示 (红色虚线圈)
         → COG vector (蓝色线, 指向 0°)
T+0:30   侧面板显示: CPA 0.18nm / TCPA 520s / Rule "R14 Head-on" / Decision "M6: 双方各右转"
T+1:00   M1-M8 pulse dots 逐个激活 (绿色)
         → ASDR 日志滚动输出
T+3:00   演示结束 — Q&A
```

---

## 7. Task 拆分

| ID | Task | 工时 | 依赖 | 产出 | 验收标准 |
|---|---|---|---|---|---|
| **T1** | React + Vite + MapLibre GL 项目初始化 | 4h | — | `web/` 目录, `npm run dev` 可启动 | Vite dev server 在 5173 可访问, MapLibre 空白地图渲染 |
| **T2** | ENC 数据转换管线 | 4h | GDAL, tippecanoe | `web/public/tiles/trondheim/{z}/{x}/{y}.pbf` | 浏览器加载 ENC tiles <2s, Trondheim 地形可辨识 |
| **T3** | MapLibre 海图样式 (ENC tiles + S-52 color) | 3h | T1, T2 | ENC 底图: 陆地/水深/海岸线分层着色 | Land = #f2f0e9, Depth deep = #c8dce8, shallow = #e8f0f4 |
| **T4** | Own-ship + Target ship sprite layer | 4h | T3 | 船形图标 + icon-rotate + COG/SOG vector | 浏览器显示 2 个 triangle 图标, heading 旋转流畅 |
| **T5** | foxglove_bridge 安装 + launch file + topic_tools | 4h | ROS2 Humble | `foxglove_sil.launch.xml`, foxglove_bridge 在 8765 可用 | `ros2 launch` 后 `ws://localhost:8765` WebSocket 可连接 |
| **T6** | foxglove_bridge WebSocket → React 数据流 | 4h | T4, T5 | roslibjs-foxglove 订阅 `/nav/filtered_state_viz` + `/world_model/tracks_viz` | Own-ship 位置实时更新, <200ms 延迟 |
| **T7** | 信息块迁移 (CPA/TCPA/Rule/Decision panel) | 4h | — | `CpaPanel`, `TcpaPanel`, `RulePanel`, `DecisionPanel` React 组件 | 与原 HTML 视觉风格一致 (font/mono/color-coding) |
| **T8** | M1-M8 Pulse + ASDR Log | 3h | T7 | `ModulePulseBar` + `AsdrLog` 组件 | 8 个 dot 激活=绿色, 日志滚动 <100ms |
| **T9** | FastAPI static serving + Vite proxy 配置 | 2h | T1 | 开发/生产双模式 | `npm run build` → FastAPI `StaticFiles` serve; Vite proxy → FastAPI 8000 |
| **T10** | DEMO-1 R14 Head-on 端到端联调 | 4h | T3-T9 | 5 分钟 live demo SOP | 按 §6.3 SOP 步骤完整跑通, 无报错 |
| **T11** | Phase 2 stub + DoD 验证 | 2h | T10 | Apache Arrow replay 接口预留, Puppeteer GIF placeholder endpoint | `SilDebugSchema` 增加 `arrow_batch_id: null` 字段 |

**合计**: 38h ≈ 1.0 pw (DEMO-1 partial). Phase 2 增项 (7/15) 占剩余 ~4.0pw.

---

## 8. Owner-by-Day 矩阵（5/25–6/15, DEMO-1 partial, ~1.0 pw）

| 日期 | T1-T2 (基础) | T3-T4 (海图) | T5-T6 (桥) | T7-T8 (面板) | T9-T11 (集成) |
|---|---|---|---|---|---|
| 5/25 (一) | T1 | — | — | — | — |
| 5/26 (二) | T2 | — | — | — | — |
| 5/27 (三) | — | T3 | T5 | T7 | — |
| 5/28 (四) | — | T4 | T5 (续) | T7 (续) | — |
| 5/29 (五) | — | T4 (续) | T6 | T8 | — |
| 6/1 (一) | — | — | T6 (续) | T8 (续) | T9 |
| 6/2 (二) | — | — | — | — | T10 |
| 6/3 (三) | — | — | — | — | T10 (续) |
| 6/4 (四) | — | — | — | — | T11 |
| 6/5 (五) | **缓冲** | **缓冲** | **缓冲** | **缓冲** | **缓冲** |
| 6/8–6/12 | **DEMO-1 彩排** | | | | |
| 6/15 (一) | **DEMO-1 Skeleton Live** | | | | |

6/16–7/15: Phase 2 增项 (Apache Arrow replay + GSAP scrubber + Puppeteer GIF + multi-target + TLS/WSS)

---

## 9. 依赖图

```
T1 ──► T3 ──► T4 ──► T6 ──► T10 ──► T11
T2 ──► T3               │
T5 ──────────────────────┘
T7 ──► T8 ──► T10

外部依赖:
  D1.3a MMG 仿真器 (已完成 — 提供 sil_mock_node)
  D1.3b.1 场景 YAML (软依赖 — DEMO-2 覆盖率热图需要)
  D1.3b.2 AIS replay (软依赖 — DEMO-2 ais_replay 模式)
```

---

## 10. Risk + Contingency

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| **R1** | foxglove_bridge apt 包在 Humble 不可用 | Low | +0.5pw | Source build from foxglove-sdk (CMake, 已验证可行) |
| **R2** | Tippecanoe S-57 转换几何失真 (UTM→WGS84 投影) | Medium | T2 返工 | GDAL `-t_srs EPSG:4326` 已验证正确; fallback: manimaul/s57tiler Docker |
| **R3** | roslibjs-foxglove JSON mode 不支持 l3_msgs 自定义消息类型 | Medium | T6 返工 | foxglove_bridge JSON mode 自动 schema advertisement; fallback: `/ws/sil_debug` 聚合所有数据 |
| **R4** | topic_tools throttle 不支持自定义消息类型 | Low | T5 返工 | topic_tools 是 content-agnostic relay — 仅管频率不管内容 |
| **R5** | DEMO-1 sil_mock_node 运行但 foxglove_bridge topic whitelist 不匹配 | Low | T10 延时 | `ros2 topic list` 预检; whitelist regex 使用 `[".*"]` 兜底 |
| **R6** | FastAPI StaticFiles + WebSocket 路由冲突 | Low | T9 延时 | `/ws/*` 在 `StaticFiles` mount 前注册; FastAPI 路由匹配优先级保证 |
| **R7** | Trondelag.gdb 图层名与预期不符 (挪威语 → 英语 mapping 错误) | Low | T2 延时 | ogrinfo 已列出所有 32 个图层名; 本 spec §3.2 mapping 已验证 |

---

## 11. D1.3b.3 全闭判据

### DEMO-1 partial (6/15)

- [ ] S-57 → MVT 管线: Trondheim MVT tiles 在 `web/public/tiles/trondheim/` 生成
- [ ] foxglove_bridge 在 8765 端口可用，JSON mode, topic_tools throttle 生效
- [ ] MapLibre HMI: 加载 ENC 底图 + own-ship sprite + 1 target sprite 同屏
- [ ] CPA / TCPA / Rule / Decision 信息块与原 HTML 视觉一致
- [ ] M1-M8 pulse + ASDR 日志实时显示
- [ ] **R14 head-on DEMO-1 现场 5 分钟 live demo 跑通**
- [ ] 现有 `test_sil_hmi.py` 30 个测试 (D1.3b.1) 全部 PASS, 无回归

### 完整 (7/15)

- [ ] Apache Arrow replay (Arrow IPC → MapLibre timeline 数据注入)
- [ ] GSAP 时间线 scrubber (任意点跳转 < 100ms)
- [ ] Puppeteer GIF/PNG batch 生成 (CI 可调用)
- [ ] Multi-target 同屏 (≥3 target ships)
- [ ] Grounding hazard 高亮 (水深 < 3m → 红色填充)
- [ ] TLS/WSS 加密 (foxglove_bridge `tls` param + FastAPI HTTPS)
- [ ] 关闭 finding ID: HMI G P1-G-x (NEW v3.1)

---

## 12. 与下游 D 的接口

| 下游 D | 消费 D1.3b.3 产物 | 所需扩展 |
|---|---|---|
| **D2.5** (SIL 集成) | foxglove_bridge + topic_tools 配置; Apache Arrow replay hooks | Arrow IPC schema → MapLibre timeline 注入; GSAP scrubber 需要 Arrow 列格式 |
| **D2.6** (船长 ground truth) | MapLibre HMI 作为可用性测试平台 | Figma 原型 → HMI 布局映射; 决策透明性理解度评分 UI |
| **D3.4** (M8 完整) | Trajectory ghosting + ToR countdown + M7 verdict badge + S-Mode 完整对齐 | `AvoidancePlanMsg` (M5 → L4) 的 ghosting 路径; ToR 倒计时 panel; M7 PASS/VETO badge |
| **D3.6** (SIL 1000+) | Puppeteer GIF evidence pack 一键生成 | 1000 场景批量 Puppeteer 截图; `coverage_report.html` 联动 |

---

## 13. 文件夹结构（Karpathy 第 3 条：前端代码在 `web/`, 不放入 `src/`）

```
web/
├── public/
│   └── tiles/
│       └── trondheim/
│           └── {z}/{x}/{y}.pbf      ← ENC MVT tiles (gitignore)
├── src/
│   ├── main.tsx                      ← React entry
│   ├── App.tsx                       ← Layout: Map + SidePanel
│   ├── components/
│   │   ├── MapView.tsx               ← MapLibre GL map container
│   │   ├── SidePanel.tsx             ← Right panel container
│   │   ├── CpaPanel.tsx              ← CPA numeric display (JetBrains Mono, color-coded)
│   │   ├── TcpaPanel.tsx             ← TCPA numeric display
│   │   ├── RulePanel.tsx             ← COLREGs rule text (Saira Condensed, --color-phos)
│   │   ├── DecisionPanel.tsx         ← L3 decision text (--color-warn)
│   │   ├── ModulePulseBar.tsx        ← M1-M8 dots (--color-stbd fill when active)
│   │   └── AsdrLog.tsx               ← ASDR scroll log (JetBrains Mono, --txt-2)
│   ├── hooks/
│   │   ├── useFoxgloveBridge.ts      ← roslibjs-foxglove connection
│   │   ├── useThrottledTopic.ts      ← rAF render throttle
│   │   └── useSilDebug.ts            ← /ws/sil_debug WebSocket
│   ├── styles/
│   │   └── tokens.css                ← Design Spec §8 CSS custom properties (Night mode default)
│   └── types/
│       └── sil.ts                    ← TypeScript types for SilDebugSchema
├── index.html
├── package.json
├── tsconfig.json
└── vite.config.ts                    ← proxy → localhost:8000

视觉规范来源:
  tokens.css ← MASS_TDL_HMI_Design_Spec_v1.0.md §8.1 颜色 + §8.2 字体 + §8.3 间距/圆角
  ship sprites ← Design Spec §4.2 (本船) + §4.3 (目标 COLREGs 颜色编码)
  侧面板 ← L3_TDL_SIL_Interactive.html (信息内容) + Design Spec §8 (视觉风格)
```

---

## 14. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-05-11 | 初版。brainstorming 产出，含完整架构、21 项决策、11 task 拆分 |
| **v1.1** | 2026-05-11 | 对齐 HMI Design Spec v1.0 §8 视觉令牌体系；新增 §0.2 三层演进路线；更新 §4.2-4.3 船舶符号为 Design Spec 颜色编码；CSS tokens 改为引用 Design Spec 而非独立定义 |

---

*本 spec 版本 v1.1。决策记录见 §0。视觉规范权威来源: `MASS_TDL_HMI_Design_Spec_v1.0.md` §8。*
*ENC 数据源: Kartverket Trondelag.gdb (已有) + NOAA SF Bay ENC (待提供凭证)。*
*下一步: `/writing-plans` → `/executing-plans`。*
