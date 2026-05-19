# Screen ① Simulation-Scenario · v1.0 设计规格

| 属性 | 值 |
|---|---|
| 文档路径 | `docs/superpowers/specs/2026-05-18-screen1-simulation-scenario-design.md` |
| 版本 | v1.0 |
| 日期 | 2026-05-18 |
| 状态 | 设计基线（brainstorm 会话完成，等待 writing-plans）|
| 上游 | Doc 3 `03-sil-frontend-design.md` §6（同步更新）|
| 覆盖范围 | `web/src/screens/SimulationScenario.tsx` 及依赖组件 · 后端 `scenario_store.py` |
| DEMO 目标 | DEMO-1 (6/15 Skeleton Live) — 屏①全量可用 |

---

## 0. 一句话定位

把 Screen ① 从"线性三步向导"升级为**3-Pane Studio**（左：ODD+库 / 中：海图引擎 / 右：Inspector），采用**方案 B**：提取 `useMapInteraction.ts` hook 承载全量地图交互逻辑，主文件保持精简，所有业务状态通过单一 `yamlDoc` YAML 对象驱动。

---

## 1. 架构决策（方案 B 锁定）

### 1.1 选型理由

| 维度 | 方案 A（内嵌） | **方案 B（hook 提取）** | 方案 C（完全重构） |
|---|---|---|---|
| 地图交互逻辑位置 | SimulationScenario.tsx 内嵌 | **独立 useMapInteraction.ts** | 独立 hook + 独立子组件 |
| 主文件预估大小 | ~28 KB（爆炸） | **~22 KB（可控）** | ~12 KB（shell） |
| 可复用性 | 难复用 | **可被屏③ Monitor 复用** | 可复用 |
| Karpathy 第 3 条 | ✓ | **✓** | ✗（过度抽象） |
| DEMO-1 工作量 | ~2.5 人天 | **~3.5 人天** | ~5 人天（风险高） |

### 1.2 关键约束（不可变更）

1. **ODD 是组织原则**：左侧 ODD 选择必须写入 `metadata.odd_cell`，Preflight Gate 4 硬依赖。
2. **单一 YAML Doc 驱动**：所有 UI 变更都以 `jsyaml` 对象为中间态，地图和表单双向同步该对象，禁止各自维护独立 state。
3. **Baseline 场景只读**：后端 `is_baseline=true` 的场景禁止 `PUT`，前端强制"另存为 Custom"流程。
4. **Tab 2 / Tab 3 DEMO-1 范围**：渲染外壳 + disabled 状态，Phase 2 (D2.x) 填充真实业务。

---

## 2. 文件变更清单

### 2.1 前端 (`web/src/`)

| 文件 | 变更类型 | 变更要点 |
|---|---|---|
| `hooks/useMapInteraction.ts` | **NEW** | 全量地图交互 hook（drag vessel / WP node / COG vector）|
| `screens/SimulationScenario.tsx` | MOD | ODD filter 内联 JSX + Baseline/Custom 分组 + 消费 hook |
| `screens/shared/BuilderRightRail.tsx` | MOD | Tab 2/3 外壳 + ODD→YAML 写入路径 |
| `api/silApi.ts` | MOD | `ScenarioListItem` 类型增 `is_baseline: boolean` + `folder_tags?: string[]` |
| `map/SilMapView.tsx` | MOD | 暴露 `onFeatureDragEnd` / `onCogDrag` / `onWpDrag` / `wpNodes` props |

### 2.2 后端 (`src/`)

| 文件 | 变更类型 | 变更要点 |
|---|---|---|
| `sil_orchestrator/scenario_store.py` | MOD | `ScenarioMeta` 增 `is_baseline: bool` 字段 |
| `sil_orchestrator/scenario_routes.py` | MOD | `GET /scenarios` 响应 + `GET /scenarios/{id}` 响应含 `is_baseline` |
| `sim_workbench/scenario_authoring/scenario_index.py` | MOD | 扫描时按 folder 判定 `is_baseline`（见 §5.2）|

---

## 3. `useMapInteraction` Hook 规格

### 3.1 接口契约

```typescript
// web/src/hooks/useMapInteraction.ts

type DragTarget =
  | { kind: 'vessel'; id: string }   // 本船 or 目标船
  | { kind: 'wp';     idx: number }  // 航路点节点
  | { kind: 'cog';    id: string }   // COG 预测线端点
  | { kind: 'none' };

interface DragState {
  active: DragTarget;
  ghostPos: [number, number] | null;  // [lon, lat] ghost 位置（拖拽中间态）
}

interface WaypointNode {
  idx: number;
  lon: number;
  lat: number;
}

interface MapInteractionOptions {
  mapRef: RefObject<maplibregl.Map>;
  previewData: PreviewData | null;
  onYamlPatch: (path: string, value: unknown) => void;
  // path 使用点号路径如 "ownShip.initial.position.latitude"
}

interface MapInteractionReturn {
  dragState: DragState;
  wpNodes: WaypointNode[];
  setWpNodes: Dispatch<SetStateAction<WaypointNode[]>>;
}

export function useMapInteraction(opts: MapInteractionOptions): MapInteractionReturn;
```

### 3.2 onYamlPatch 路径映射

| 交互 | path | value 类型 |
|---|---|---|
| 本船拖拽 → lat | `ownShip.initial.position.latitude` | `number` (6 位小数) |
| 本船拖拽 → lon | `ownShip.initial.position.longitude` | `number` |
| 目标船拖拽 → lat | `targetShips[n].initial.position.latitude` | `number` |
| 目标船拖拽 → lon | `targetShips[n].initial.position.longitude` | `number` |
| WP 节点拖拽 → lat | `voyageTask.waypoints[n].lat` | `number` |
| WP 节点拖拽 → lon | `voyageTask.waypoints[n].lon` | `number` |
| COG 拉伸 → 本船航向 | `ownShip.initial.heading` | `number` (0–360°) |
| COG 拉伸 → 目标航向 | `targetShips[n].initial.heading` | `number` |

### 3.3 内部实现约束

- `mousedown` 时检测命中目标（vessel symbol / wp node / cog line endpoint），设置 `dragState.active`
- `mousemove` 时更新 `ghostPos`（用于渲染拖拽预览），**不调用** `onYamlPatch`（避免高频写 YAML）
- `mouseup` 时调用 `onYamlPatch` 一次，传最终位置
- `onYamlPatch` 在 `SimulationScenario.tsx` 内用 200ms debounce 包装后触发地图重绘
- MapLibre cursor 在拖拽目标 hover 时切换为 `grab`，拖拽中切换为 `grabbing`
- 触摸屏（touch events）：`touchstart/touchmove/touchend` 同样路由到同一逻辑

---

## 4. 左栏：ODD 过滤器 + 场景库

### 4.1 ODD 过滤器（顶部固定区）

**渲染结构**（内联于 `SimulationScenario.tsx`，不提取组件）：

```
┌─ ODD 全局过滤器 ─────────────────┐
│ 🔒 航区域:  [Open Sea        ▾] │
│    海况:    [Beaufort ≤ 5    ▾] │
│    能见度:  [> 2 nm          ▾] │
│ ⚠ 右侧超出 ODD 范围的参数将被锁死 │
└──────────────────────────────────┘
```

**变更行为**：ODD 任意字段变更时，同步执行两个动作：

1. **写入 YAML**：
   ```
   metadata.odd_cell.domain           ← 航区域选项值（open_sea/coastal/fairway/port_entry/ofw）
   metadata.odd_cell.visibility_nm    ← 能见度数值（如 2.0）
   metadata.odd_cell.sea_state_beaufort ← 海况数值（如 5）
   ```
2. **客户端过滤场景列表**：将 `odd_cell` 不兼容的场景条目标红（`dot-red` + tooltip 说明冲突原因），不隐藏。

**航区域选项枚举**：

| UI 显示 | YAML 值 | 含义 |
|---|---|---|
| Open Sea | `open_sea` | 开阔海域 |
| Coastal | `coastal` | 近岸 |
| Fairway | `fairway` | 航道 |
| Port Entry | `port_entry` | 港口进出 |
| Offshore Wind Farm | `ofw` | 海上风场 |

### 4.2 船型能力清单（ODD 区下方）

单行展示当前船型 + 关键物理限制，点击弹出完整 Capability Manifest 浮层（DEMO-1 内展示 FCB-45 硬编码数据，Phase 2 接后端 Vessel Registry）：

```
🚢 FCB-45 m  |  吃水 3.2m · 旋回半径 340m · 制动 ≥0.6nm  [切换▾]
```

切换船型时：右侧 Inspector 各参数滑块上限联动更新（如最大 SOG 从 15kn 变为 12kn）。

### 4.3 场景库（双工作区）

**后端 `is_baseline` 判定规则**（`scenario_index.py`）：

```python
BASELINE_FOLDERS = {"imazu22", "colregs", "ais_accident"}
# scenario 的 folder 字段在 BASELINE_FOLDERS 内 → is_baseline = True
# 否则 → is_baseline = False
```

**前端渲染规则**：

| 条件 | 渲染行为 |
|---|---|
| `is_baseline=true` | 显示 🔒 图标；SAVE 按钮替换为"另存为 Custom"；POST 新建到 Custom 区 |
| `is_baseline=false` | 显示 ✏ 图标；SAVE 正常；支持 Rename / Delete / Duplicate |

**场景条目（Rich List Item）**：

```
[●] R14 · Head-on (Rule 14) — 5nm    [NCDM] [1 TGT]   🔒
[●] R15 · Crossing (Rule 15) — 4nm   [NCDM] [2 TGT]   🔒
[🔴] R16 · Give-way — ODD 冲突       [ODD⚠]           🔒
```

- 健康点：🟢 schema 通过 / 🟡 警告 / 🔴 schema 失败或 ODD 冲突
- hover 显示快捷操作（Baseline：Duplicate to Custom；Custom：Rename / Delete）

**快捷过滤 toggles**（搜索栏下方一行）：`[多船]` `[含故障]` `[上次PASS]` `[上次FAIL]`

---

## 5. 中栏：地图引擎 + 交互

### 5.1 底图（保持现有 MVT/martin 方案）

文档套件 Doc 3 §6.3.2 已提及 PMTiles 方案，但当前实现使用 `martin` tile server（port 3000）+ MVT。**DEMO-1 维持现有 MVT/martin 方案不变**，PMTiles 换代推到 Phase 3（D3.x）。

现有 `SilMapView.tsx` 已实现：ENC S-57 MVT 多图层 + S-52 样式 + MapLayerSwitcher + ENC/SAT/OSM 三底图切换。

### 5.2 新增 Props（SilMapView.tsx MOD）

```typescript
interface SilMapViewProps {
  // 现有 props（保留）
  followOwnShip: boolean;
  viewMode: 'captain' | 'god' | 'roc';
  previewData?: PreviewData;
  onMapClick?: (lon: number, lat: number) => void;
  substrate: 'enc' | 'sat' | 'osm';
  geometry?: GeoJSON.FeatureCollection;

  // 新增 props（方案 B）
  onFeatureDragEnd?: (id: string, lon: number, lat: number) => void;
  onCogDrag?: (id: string, bearingDeg: number) => void;
  onWpDrag?: (idx: number, lon: number, lat: number) => void;
  wpNodes?: WaypointNode[];
  dragState?: DragState;   // 用于渲染 ghost overlay
}
```

### 5.3 地图交互完整覆盖（DEMO-1）

| 交互 | 实现位置 | 触发 onYamlPatch |
|---|---|---|
| 点击地图放置本船/目标船（现有） | `SimulationScenario.tsx` `handleMapClick` | ✓ |
| 拖拽本船改位置 | `useMapInteraction` | ✓ mouseup |
| 拖拽目标船改位置 | `useMapInteraction` | ✓ mouseup |
| 拖拽 COG 线端点改航向 | `useMapInteraction` | ✓ mouseup |
| 拖拽 WP 节点改坐标 | `useMapInteraction` | ✓ mouseup |
| 地图点击新增 WP 节点 | `SimulationScenario.tsx` placementMode | ✓ |

### 5.4 底部测绘工具栏

固定在地图底部：

```
[📍 31°52.4'N · 122°06.1'E] [Depth: 42.3m] [Scale 1:45,000] | [EBL] [VRM] [📐 Measure] | [⊕ Reset to OS]
```

- 游标经纬度：MapLibre `mousemove` event 实时更新
- 水深：从 S-57 layer feature 查询（`queryRenderedFeatures` at cursor point）
- EBL/VRM：Phase 2（DEMO-1 展示按钮但 disabled）

---

## 6. 右栏：Inspector 四 Tab

### 6.1 Tab 1 — Vessels & Mission（完整实现，DEMO-1）

#### 本船（Own Ship）

| 字段 | YAML 路径 | 类型 |
|---|---|---|
| 初始航速 SOG | `ownShip.initial.sog` | number (kn) |
| 初始航向 HDG | `ownShip.initial.heading` | number (°T, 0–360) |
| 任务终点纬度 | `voyageTask.waypoints[-1].lat` | number |
| 任务终点经度 | `voyageTask.waypoints[-1].lon` | number |
| ETA 时间窗 | `voyageTask.eta_window_s` | number (seconds) |

#### 目标船（Target Ships Array）

- 可折叠手风琴列表，支持 `[+ 添加目标]` / `[🗑 删除]`
- 双 Schema 格式切换 toggle（影响同一 target 的 YAML 输出格式）：
  - **Lat/Lon 模式**（IMAZU v2.0）：写 `position.latitude` / `position.longitude`
  - **ENU 模式**（COLREGs v1.0）：写 `initial.x_m` / `initial.y_m`（以本船为原点）
- 驱动模型 `model` 选项：`NCDM` / `AIS_Replay` / `Constant_Velocity`

### 6.2 Tab 2 — Env + Timeline Faults（DEMO-1 外壳）

渲染 disabled 卡片占位：

```
┌─ Tab 2: 环境 + 时间轴故障注入 ─────────────────┐
│                                               │
│  🔒 Phase 2 功能 (D2.x)                       │
│  当前版本: 仅展示 metadata.environment 字段摘要│
│                                               │
│  [风速] [海流] [能见度] — 只读                 │
│  [故障时间轴] — 不可编辑                       │
└───────────────────────────────────────────────┘
```

**只读显示**（从已加载 YAML 中读取 `environment` 字段展示）；不允许编辑；不允许添加时间轴事件。

### 6.3 Tab 3 — Behavioral Assertions（DEMO-1 外壳）

渲染 disabled 卡片占位：

```
┌─ Tab 3: 行为断言 Oracle ───────────────────────┐
│                                               │
│  🔒 Phase 2 功能 (D2.x)                       │
│  当前版本: 仅展示 expected_outcome 字段摘要   │
│                                               │
│  cpa_min_m_ge: 925m (0.5nm)                   │
│  colregs_rules: [R14, R8, R16]                │
│  grounding: forbidden                         │
│                                               │
│  [编辑断言] — 不可用                          │
└───────────────────────────────────────────────┘
```

**只读显示**（从 `expected_outcome` 字段展示摘要）；Preflight Gate 3 验证该字段是否存在。

### 6.4 Tab 4 — YAML Source（已实现，DEMO-1 保持）

Monaco Editor + `@monaco-editor/react` 已实现。DEMO-1 内保持现状：双向同步通过 `jsyaml.load/dump`。

已知限制（Phase 2 解决）：`jsyaml.dump` 会丢失 YAML 注释，改用保留 AST 的解析库。

### 6.5 Sticky Action Footer

固定在 Inspector 底部，不随 Tab 滚动：

```
[● / 🔴] Schema 校验状态 · 错误路径提示                [sha256: xxxx]
[另存变体]    [⚄ MC扫掠 (Phase2)]    [🚀 RUN → ②]
```

- **校验指示灯**：调用 `useSchemaValidation(yamlEditor)` 实时校验（GAP-022）
- **sha256**：SAVE 后从后端 POST `/scenarios` 响应获取（GAP-025），非前端计算
- **另存变体**：
  - 若当前场景为 Baseline（`is_baseline=true`）→ POST `/scenarios` 新建 Custom 副本
  - 若当前场景为 Custom → PUT `/scenarios/{id}` 更新
- **MC 扫掠**：DEMO-1 显示按钮但 disabled（Phase 2 D2.4）
- **RUN**：`scenarioStore.setScenario(id, hash)` → `window.location.hash = '#/check/:id'`

---

## 7. 数据流（单向 YAML Doc 驱动）

```
ODD 选择变更
    │ 写入 yamlDoc.metadata.odd_cell
    ↓
场景选中 → GET /scenarios/{id}
    │ jsyaml.load(yaml_content) → yamlDoc
    ↓
地图拖拽（useMapInteraction）
    │ onYamlPatch(path, value) → jsyaml 局部 merge
    │ 200ms debounce → SilMapView previewData re-render
    ↓
表单编辑（BuilderRightRail onUpdateYaml）
    │ 同一 yamlDoc merge → 地图同步重绘（反向）
    ↓
useSchemaValidation(yamlEditor) 实时校验
    ↓
SAVE
    │ Custom: POST /scenarios → 拿 hash + scenario_id
    │ Baseline: POST /scenarios (duplicate) → 新 Custom 条目
    │ 写 scenarioStore.setScenario(id, hash)
    ↓
RUN → navigate #/check/:id
    │ Preflight Gate 3: 校验 hash + schema + expected_outcome
    │ Preflight Gate 4: 校验 odd_cell vs M1 ODD state
```

---

## 8. 后端变更详细规格

### 8.1 `scenario_store.py` — ScenarioMeta 变更

```python
@dataclass
class ScenarioMeta:
    id: str
    folder: str
    yaml_content: str
    hash: str
    is_baseline: bool          # 新增
    folder_tags: list[str] = field(default_factory=list)  # 新增（CI 标签预留）
    last_ci_result: str | None = None  # 新增（"pass"/"fail"/None，CI 历史标签）
```

### 8.2 `scenario_index.py` — 判定逻辑

```python
BASELINE_FOLDERS = frozenset({"imazu22", "colregs", "ais_accident"})

def index_scenario(path: Path, folder: str) -> ScenarioMeta:
    ...
    is_baseline = folder in BASELINE_FOLDERS
    return ScenarioMeta(..., is_baseline=is_baseline)
```

### 8.3 `scenario_routes.py` — API 响应变更

`GET /api/v1/scenarios` 响应 item 格式：

```json
{
  "id": "imazu22/head_on_r14",
  "folder": "imazu22",
  "is_baseline": true,
  "hash": "a3f2...c8b1",
  "last_ci_result": "pass",
  "folder_tags": []
}
```

Baseline 场景的 `PUT /api/v1/scenarios/{id}` 返回 `409 Conflict`：

```json
{"detail": "Baseline scenarios are read-only. Use POST /scenarios to duplicate."}
```

---

## 9. GAP 关闭台账（Screen ① 全量）

| GAP | 描述 | 关闭方式 | DEMO-1 状态 |
|---|---|---|---|
| **GAP-022** | Builder validate 仅查空字符串 | `useSchemaValidation` hook 实时校验 + Monaco 内联报错 | ✓ 完整关闭 |
| **GAP-023** | ODD 选单不写 YAML | ODD 变更 → `yamlDoc.metadata.odd_cell.{domain,visibility_nm,sea_state_beaufort}` | ✓ 完整关闭 |
| **GAP-024** | 双 Schema 格式碎片化 | Tab 1 Target card: Lat/Lon ↔ ENU toggle + 双格式 jsyaml 解析 | ✓ 完整关闭 |
| **GAP-025** | SHA256 前端硬编码 | SAVE 后从 `POST /scenarios` 响应取 hash，Sticky Footer 展示 | ✓ 完整关闭 |
| **GAP-029** | 文件名/路由未重命名 | 已完成（SimulationScenario.tsx + #/scenario） | ✓ 已完成 |
| **GAP-NEW-001** | Baseline 场景无只读保护 | 后端 `is_baseline` 字段 + 前端禁用 SAVE + 409 HTTP 返回 | ✓ 完整关闭 |

---

## 10. 验收条件（DEMO-1 · 6/15）

以下条件**全部满足**才视为屏①交付完成：

- [ ] **ODD 写入**：选择 ODD 后，保存的 YAML 文件中 `metadata.odd_cell` 包含正确的 domain / visibility_nm / sea_state_beaufort
- [ ] **Baseline 只读**：点击 Imazu22 场景的 SAVE → 触发"另存 Custom"流程，不覆盖原文件
- [ ] **Custom SAVE**：Custom 场景保存后 Sticky Footer 显示来自后端的真实 sha256 hash（非占位符）
- [ ] **拖拽本船**：鼠标拖拽本船 sprite → 松开后右侧 Lat 字段更新，地图重绘
- [ ] **拖拽目标船**：同上，针对 TGT-1
- [ ] **COG 拉伸**：拖拽 COG 预测线端点 → HDG 字段更新
- [ ] **WP 节点拖拽**：拖拽 WP 节点 → voyageTask.waypoints 对应条目更新
- [ ] **Schema 校验**：输入非法参数（如 SOG=999）→ 校验指示灯变红 + 错误路径提示
- [ ] **Tab 2/3 外壳**：Tab 2 / Tab 3 可点击进入，显示 Phase 2 锁定提示，不报错不崩溃
- [ ] **RUN 流程**：点击 RUN → navigate 到 `#/check/:id`，Preflight 屏正常加载
- [ ] **Preflight Gate 4**：Gate 4 能读到 `metadata.odd_cell.domain` 并与 M1 ODD 状态对比

---

## 11. 不在范围内（DEMO-1 范围外）

| 功能 | 推迟到 |
|---|---|
| Tab 2 Env+Fault 真实实现（时间轴事件注入） | Phase 2 D2.x |
| Tab 3 Behavioral Assertions 真实实现 | Phase 2 D2.x |
| EBL/VRM 测量工具 | Phase 2 |
| Monte Carlo 扫掠（⚄ 按钮） | Phase 2 D2.4 |
| PMTiles 换代（替换 MVT/martin） | Phase 3 D3.x |
| Vessel Registry 后端（多船型 API） | Phase 2 |
| YAML AST 保留注释（Monaco 双向同步无损） | Phase 2 |
| CI 历史标签（上次 PASS/FAIL 真实数据） | Phase 2 D2.5 |
| S-102 UKC 动态禁航区叠加 | Phase 3 |

---

## 12. 修订记录

| 版本 | 日期 | 改动 |
|---|---|---|
| v1.0 | 2026-05-18 | 初稿。brainstorm 会话产出：方案 B 锁定 + Tab2/3 DEMO-1 外壳 + ODD→metadata.odd_cell 三字段 + is_baseline 后端字段 + 全量地图交互（drag+WP+COG）。上游 Doc 3 §6 同步更新。|
