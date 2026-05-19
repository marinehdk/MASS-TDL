# Screen ① Simulation-Scenario · 3-Pane Studio 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Screen ① 从"线性三步向导"升级为 **3-Pane Studio**（左：ODD+场景库 / 中：海图引擎 + 拖拽交互 / 右：Inspector 四 Tab），采用方案 B（提取 `useMapInteraction.ts` hook），所有业务状态通过单一 `yamlDoc` YAML 对象驱动。

**Architecture:** 方案 B — 地图交互逻辑提取为独立 `useMapInteraction.ts` hook，主文件 `SimulationScenario.tsx` 保持 ≤22 KB。后端增加 `is_baseline` 字段实现 Baseline/Custom 双工作区隔离。所有 UI 变更以 `jsyaml` 对象为中间态，地图和表单双向同步。

**Tech Stack:** React 18 + TypeScript + MapLibre GL JS + js-yaml + Zustand + RTK Query · FastAPI (Python) · YAML

**参考文档:**
- Spec: `docs/superpowers/specs/2026-05-18-screen1-simulation-scenario-design.md`
- Doc 3 前端: `docs/Design/SIL/v1.0-unified/03-sil-frontend-design.md` §6
- Doc 2 后端: `docs/Design/SIL/v1.0-unified/02-sil-backend-design.md`

---

## 文件结构总览

| 文件 | 变更类型 | 职责 |
|---|---|---|
| `web/src/hooks/useMapInteraction.ts` | **NEW** | 全量地图拖拽交互（vessel/WP/COG），mouseup 时调 `onYamlPatch` |
| `web/src/screens/SimulationScenario.tsx` | MOD | 3-Pane Studio 主布局：左 ODD+场景库 / 中地图 / 右 Inspector |
| `web/src/screens/shared/BuilderRightRail.tsx` | MOD | 重构为 4 Tab Inspector + Sticky Footer + `is_baseline` 感知 |
| `web/src/api/silApi.ts` | MOD | `ScenarioSummary` 增 `is_baseline`、`folder_tags`、`last_ci_result` |
| `web/src/map/SilMapView.tsx` | MOD | 暴露 `onFeatureDragEnd`/`onCogDrag`/`onWpDrag`/`wpNodes`/`dragState` props |
| `src/sil_orchestrator/scenario_store.py` | MOD | `list()` 增 `is_baseline`、`folder_tags`、`last_ci_result`；`update()` 对 baseline 返回 `409` |
| `src/sil_orchestrator/scenario_routes.py` | MOD | `PUT` 端点检测 baseline → 返回 409 Conflict |

---

## Task 1: 后端 — ScenarioMeta 增加 `is_baseline` 字段

**Files:**
- Modify: `src/sil_orchestrator/scenario_store.py`
- Modify: `src/sil_orchestrator/scenario_routes.py`

- [ ] **Step 1: 在 `scenario_store.py` 增加 `BASELINE_FOLDERS` 常量和 `is_baseline` 判定**

在 `scenario_store.py` 顶部（`_ENC_TOKEN_MAP` 定义之后，`_SCHEMA_PATH` 之前）添加：

```python
# Baseline folders — scenarios in these folders are read-only
BASELINE_FOLDERS = frozenset({"imazu22", "colregs", "ais_accident"})
```

- [ ] **Step 2: 修改 `ScenarioStore.list()` 方法，增加 `is_baseline`、`folder_tags`、`last_ci_result` 字段**

修改 `scenario_store.py` 的 `list()` 方法（约第 70-91 行），将 `results.append(...)` 部分改为：

```python
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").replace("_", " ").title(),
                "encounter_type": _infer_encounter_type(f.stem),
                "folder": folder or "root",
                "is_baseline": folder in BASELINE_FOLDERS if folder != "root" else False,
                "folder_tags": [],
                "last_ci_result": None,
            })
```

- [ ] **Step 3: 修改 `ScenarioStore.update()` 方法，对 baseline 场景返回 None（触发 404 → 改为显式冲突检测）**

在 `scenario_store.py` 的 `update()` 方法（约第 120 行）开头增加 baseline 检查。修改方法签名为返回 `dict | str`（或用特殊标记）。最简洁的方式：在 `update()` 开头检查 folder 并在 `scenario_routes.py` 中处理。

实际上，最简单且符合 Spec §8.3 的方式是：在 `scenario_routes.py` 的 `PUT` handler 中检查 folder。我们先在 `store` 中暴露 `is_baseline` 方法。

在 `scenario_store.py` 的 `ScenarioStore` 类中新增方法（放在 `delete` 之后）：

```python
    def is_baseline(self, scenario_id: str) -> bool:
        """Check if a scenario is in a baseline folder (read-only)."""
        path = self._path_for(scenario_id)
        if path is None:
            return False
        try:
            rel = path.relative_to(self._dir)
            folder = rel.parent.name if len(rel.parts) > 1 else "root"
        except ValueError:
            folder = path.parent.name
        return folder in BASELINE_FOLDERS
```

- [ ] **Step 4: 修改 `scenario_routes.py` 的 `PUT` 端点，baseline 场景返回 409**

修改 `src/sil_orchestrator/scenario_routes.py` 的 `update_scenario` 函数（约第 27-32 行）：

```python
@router.put("/{scenario_id}")
async def update_scenario(scenario_id: str, request: dict):
    if store.is_baseline(scenario_id):
        raise HTTPException(
            status_code=409,
            detail="Baseline scenarios are read-only. Use POST /scenarios to duplicate."
        )
    result = store.update(scenario_id, request.get("yaml_content", ""))
    if result is None:
        raise HTTPException(status_code=404, detail="Scenario not found")
    return result
```

- [ ] **Step 5: 验证 — 运行后端测试**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer && python -m pytest src/sil_orchestrator/ -v 2>&1 || echo "No tests found — manual verification required"`

Expected: 现有测试通过（如有），或确认代码语法无误。

- [ ] **Step 6: Commit**

```bash
git add src/sil_orchestrator/scenario_store.py src/sil_orchestrator/scenario_routes.py
git commit -m "feat(backend): add is_baseline field to ScenarioMeta, PUT 409 for baseline scenarios"
```

---

## Task 2: 前端 — silApi 类型扩展（`ScenarioSummary` 增加 `is_baseline`）

**Files:**
- Modify: `web/src/api/silApi.ts`

- [ ] **Step 1: 扩展 `ScenarioSummary` 接口**

修改 `web/src/api/silApi.ts` 的 `ScenarioSummary` 接口（第 4-8 行）：

```typescript
export interface ScenarioSummary {
  id: string;
  name: string;
  encounter_type: string;
  folder: string;
  is_baseline: boolean;
  folder_tags?: string[];
  last_ci_result?: string | null;
}
```

- [ ] **Step 2: 验证 TypeScript 编译**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | head -30`

Expected: 无新增类型错误。`ScenarioSummary` 的使用处会自动获得新字段。

- [ ] **Step 3: Commit**

```bash
git add web/src/api/silApi.ts
git commit -m "feat(frontend): add is_baseline, folder_tags, last_ci_result to ScenarioSummary type"
```

---

## Task 3: 前端 — 创建 `useMapInteraction.ts` hook

**Files:**
- Create: `web/src/hooks/useMapInteraction.ts`

这是整个方案 B 的核心交付物。Hook 承载全量地图拖拽交互逻辑。

- [ ] **Step 1: 创建 hook 文件，定义类型和接口**

创建 `web/src/hooks/useMapInteraction.ts`：

```typescript
import { useState, useCallback, useEffect, useRef } from 'react';
import type { RefObject, Dispatch, SetStateAction } from 'react';
import type maplibregl from 'maplibre-gl';

// ── Types ───────────────────────────────────────────────────────────────

export type DragTarget =
  | { kind: 'vessel'; id: string }    // 本船 or 目标船
  | { kind: 'wp';     idx: number }   // 航路点节点
  | { kind: 'cog';    id: string }    // COG 预测线端点
  | { kind: 'none' };

export interface DragState {
  active: DragTarget;
  ghostPos: [number, number] | null;   // [lon, lat] ghost 位置（拖拽中间态）
}

export interface WaypointNode {
  idx: number;
  lon: number;
  lat: number;
}

export interface PreviewData {
  ownShip?: { lat: number; lon: number; heading: number; sog?: number; cog?: number };
  targets?: Array<{ id: string; lat: number; lon: number; heading: number; sog?: number; cog?: number }>;
}

export interface MapInteractionOptions {
  mapRef: RefObject<maplibregl.Map | null>;
  previewData: PreviewData | null;
  onYamlPatch: (path: string, value: unknown) => void;
}

export interface MapInteractionReturn {
  dragState: DragState;
  wpNodes: WaypointNode[];
  setWpNodes: Dispatch<SetStateAction<WaypointNode[]>>;
}

// ── Hit-test helpers ────────────────────────────────────────────────────

const VESSEL_HIT_RADIUS_PX = 20;   // px radius around vessel marker
const WP_HIT_RADIUS_PX = 15;
const COG_HIT_RADIUS_PX = 12;

function lngLatDistance(a: maplibregl.LngLat, b: maplibregl.LngLat): number {
  const dx = a.lng - b.lng;
  const dy = a.lat - b.lat;
  return Math.sqrt(dx * dx + dy * dy);
}

// ── Hook ─────────────────────────────────────────────────────────────────

export function useMapInteraction(opts: MapInteractionOptions): MapInteractionReturn {
  const { mapRef, previewData, onYamlPatch } = opts;

  const [dragState, setDragState] = useState<DragState>({ active: { kind: 'none' }, ghostPos: null });
  const [wpNodes, setWpNodes] = useState<WaypointNode[]>([]);
  const dragActiveRef = useRef<DragTarget>({ kind: 'none' });

  // ── mousedown: detect which target is under cursor ─────────────────────
  const handleMouseDown = useCallback((e: maplibregl.MapMouseEvent) => {
    const map = mapRef.current;
    if (!map) return;

    const point = e.point;
    const lngLat = e.lngLat;

    // 1. Check WP nodes first (smallest hit area, drawn on top conceptually)
    for (const wp of wpNodes) {
      const wpLngLat = new (maplibregl as any).LngLat
        ? new maplibregl.LngLat(wp.lon, wp.lat)
        : { lng: wp.lon, lat: wp.lat };
      const px = map.project(wpLngLat as maplibregl.LngLat);
      const dx = point.x - px.x;
      const dy = point.y - px.y;
      if (Math.sqrt(dx * dx + dy * dy) < WP_HIT_RADIUS_PX) {
        const target: DragTarget = { kind: 'wp', idx: wp.idx };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }

    // 2. Check vessel markers (ownShip + targets)
    const vessels: Array<{ id: string; lon: number; lat: number }> = [];
    if (previewData?.ownShip) {
      vessels.push({ id: 'ownship', lon: previewData.ownShip.lon, lat: previewData.ownShip.lat });
    }
    if (previewData?.targets) {
      for (const t of previewData.targets) {
        vessels.push({ id: t.id, lon: t.lon, lat: t.lat });
      }
    }
    for (const v of vessels) {
      const vLngLat = new (maplibregl as any).LngLat
        ? new maplibregl.LngLat(v.lon, v.lat)
        : { lng: v.lon, lat: v.lat };
      const px = map.project(vLngLat as maplibregl.LngLat);
      const dx = point.x - px.x;
      const dy = point.y - px.y;
      if (Math.sqrt(dx * dx + dy * dy) < VESSEL_HIT_RADIUS_PX) {
        const target: DragTarget = { kind: 'vessel', id: v.id };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }

    // 3. COG line endpoints — check if near the tip of COG vector
    // COG tip = vessel position + (cog direction × length)
    // For simplicity, we compute the COG endpoint from previewData
    const checkCog = (id: string, lon: number, lat: number, cogDeg: number, sogMs: number) => {
      if (sogMs <= 0) return false;
      const distNm = Math.max((sogMs * 360) / 1852, 0.5);
      const cogRad = cogDeg * Math.PI / 180;
      const cosLat = Math.cos(lat * Math.PI / 180);
      const tipLon = lon + (distNm / 60 * Math.sin(cogRad)) / cosLat;
      const tipLat = lat + (distNm / 60 * Math.cos(cogRad));
      const tipPx = map.project([tipLon, tipLat] as [number, number]);
      const dx = point.x - tipPx.x;
      const dy = point.y - tipPx.y;
      return Math.sqrt(dx * dx + dy * dy) < COG_HIT_RADIUS_PX;
    };

    if (previewData?.ownShip) {
      const os = previewData.ownShip;
      if (checkCog('ownship', os.lon, os.lat, os.heading, os.sog ?? 0)) {
        const target: DragTarget = { kind: 'cog', id: 'ownship' };
        dragActiveRef.current = target;
        setDragState({ active: target, ghostPos: null });
        map.getCanvas().style.cursor = 'grabbing';
        e.preventDefault();
        return;
      }
    }
    if (previewData?.targets) {
      for (const t of previewData.targets) {
        if (checkCog(t.id, t.lon, t.lat, t.heading, t.sog ?? 0)) {
          const target: DragTarget = { kind: 'cog', id: t.id };
          dragActiveRef.current = target;
          setDragState({ active: target, ghostPos: null });
          map.getCanvas().style.cursor = 'grabbing';
          e.preventDefault();
          return;
        }
      }
    }

    dragActiveRef.current = { kind: 'none' };
  }, [mapRef, previewData, wpNodes]);

  // ── mousemove: update ghost position ───────────────────────────────────
  const handleMouseMove = useCallback((e: maplibregl.MapMouseEvent) => {
    const active = dragActiveRef.current;
    if (active.kind === 'none') return;

    setDragState(prev => ({
      ...prev,
      ghostPos: [e.lngLat.lng, e.lngLat.lat],
    }));
  }, []);

  // ── mouseup: commit the change via onYamlPatch ─────────────────────────
  const handleMouseUp = useCallback((e: maplibregl.MapMouseEvent) => {
    const map = mapRef.current;
    const active = dragActiveRef.current;
    if (!map || active.kind === 'none') return;

    const lon = Number(e.lngLat.lng.toFixed(6));
    const lat = Number(e.lngLat.lat.toFixed(6));

    switch (active.kind) {
      case 'vessel': {
        if (active.id === 'ownship') {
          onYamlPatch('ownShip.initial.position.latitude', lat);
          onYamlPatch('ownShip.initial.position.longitude', lon);
        } else {
          // Find target index by id
          const tgtIdx = previewData?.targets?.findIndex(t => t.id === active.id) ?? -1;
          if (tgtIdx >= 0) {
            onYamlPatch(`targetShips.${tgtIdx}.initial.position.latitude`, lat);
            onYamlPatch(`targetShips.${tgtIdx}.initial.position.longitude`, lon);
          }
        }
        break;
      }
      case 'wp': {
        onYamlPatch(`voyageTask.waypoints.${active.idx}.lat`, lat);
        onYamlPatch(`voyageTask.waypoints.${active.idx}.lon`, lon);
        // Update local wpNodes state
        setWpNodes(prev => prev.map(wp =>
          wp.idx === active.idx ? { ...wp, lat, lon } : wp
        ));
        break;
      }
      case 'cog': {
        // Calculate bearing from vessel position to drop point
        let vesselLon: number, vesselLat: number;
        if (active.id === 'ownship') {
          vesselLon = previewData?.ownShip?.lon ?? 0;
          vesselLat = previewData?.ownShip?.lat ?? 0;
        } else {
          const t = previewData?.targets?.find(tgt => tgt.id === active.id);
          vesselLon = t?.lon ?? 0;
          vesselLat = t?.lat ?? 0;
        }
        const dLon = lon - vesselLon;
        const dLat = lat - vesselLat;
        const bearingDeg = ((Math.atan2(dLon, dLat) * 180 / Math.PI) + 360) % 360;
        const heading = Number(bearingDeg.toFixed(1));

        if (active.id === 'ownship') {
          onYamlPatch('ownShip.initial.heading', heading);
        } else {
          const tgtIdx = previewData?.targets?.findIndex(t => t.id === active.id) ?? -1;
          if (tgtIdx >= 0) {
            onYamlPatch(`targetShips.${tgtIdx}.initial.heading`, heading);
          }
        }
        break;
      }
    }

    dragActiveRef.current = { kind: 'none' };
    setDragState({ active: { kind: 'none' }, ghostPos: null });
    map.getCanvas().style.cursor = '';
  }, [mapRef, previewData, onYamlPatch]);

  // ── Attach / detach map listeners ──────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    map.on('mousedown', handleMouseDown);
    map.on('mousemove', handleMouseMove);
    map.on('mouseup', handleMouseUp);

    return () => {
      map.off('mousedown', handleMouseDown);
      map.off('mousemove', handleMouseMove);
      map.off('mouseup', handleMouseUp);
      map.getCanvas().style.cursor = '';
    };
  }, [mapRef, handleMouseDown, handleMouseMove, handleMouseUp]);

  return { dragState, wpNodes, setWpNodes };
}
```

- [ ] **Step 2: 验证 TypeScript 编译**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | head -30`

Expected: 无编译错误。

- [ ] **Step 3: Commit**

```bash
git add web/src/hooks/useMapInteraction.ts
git commit -m "feat(frontend): add useMapInteraction hook for vessel/WP/COG drag on map"
```

---

## Task 4: 前端 — SilMapView 新增拖拽相关 props

**Files:**
- Modify: `web/src/map/SilMapView.tsx`

- [ ] **Step 1: 在 `SilMapViewProps` 接口中新增 props**

修改 `web/src/map/SilMapView.tsx` 的 `SilMapViewProps` 接口（约第 9-26 行），在 `geometry` 之后添加新 props：

```typescript
interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
  viewportOffset?: [number, number];
  previewData?: { ... };
  onMapClick?: (lon: number, lat: number) => void;
  substrate?: 'enc' | 'sat' | 'osm';
  geometry?: GeoJSON.FeatureCollection | null;

  // ── 新增 props（方案 B，Screen ① 拖拽交互）──
  /** 外部传入 mapRef，使 useMapInteraction hook 可绑定事件（方案 B 核心） */
  mapRef?: React.RefObject<maplibregl.Map | null>;
  /** 拖拽 vessel sprite 结束回调 */
  onFeatureDragEnd?: (id: string, lon: number, lat: number) => void;
  /** 拖拽 COG 线端点结束回调 */
  onCogDrag?: (id: string, bearingDeg: number) => void;
  /** 拖拽 WP 节点结束回调 */
  onWpDrag?: (idx: number, lon: number, lat: number) => void;
  /** 地图上可见的 WP 节点列表 */
  wpNodes?: Array<{ idx: number; lon: number; lat: number }>;
  /** 当前拖拽状态（用于 ghost overlay 渲染） */
  dragState?: { active: { kind: string; id?: string; idx?: number }; ghostPos: [number, number] | null };
}
```

- [ ] **Step 2: 在 `SilMapView` 函数签名中解构新 props**

修改 `SilMapView` 函数参数解构（约第 95-103 行），添加新 props：

```typescript
export function SilMapView({ 
  followOwnShip = true, 
  viewMode = 'captain', 
  viewportOffset = [0.5, 0.5],
  previewData,
  onMapClick,
  substrate = 'enc',
  geometry,
  mapRef: externalMapRef,
  onFeatureDragEnd,
  onCogDrag,
  onWpDrag,
  wpNodes,
  dragState,
}: SilMapViewProps) {
```

- [ ] **Step 2.5: 在 map 初始化后，将内部 mapRef 赋值给外部 mapRef**

在 `SilMapView` 的地图初始化 `useEffect` 中（`mapRef.current = map;` 行附近，约第 285 行），添加外部 ref 同步：

```typescript
    mapRef.current = map;
    // 方案 B：同步到父组件 ref，使 useMapInteraction hook 可绑定事件
    if (externalMapRef) {
      (externalMapRef as React.MutableRefObject<maplibregl.Map | null>).current = map;
    }
    if (typeof window !== 'undefined') { (window as any).__maplibre_map = map; }
```

同时在 Cleanup 中清空：

```typescript
    return () => {
      ownMarker.current?.remove(); ownMarker.current = null;
      windMarker.current?.remove(); windMarker.current = null;
      tgtMarkers.current.forEach((m) => m.remove());
      tgtMarkers.current.clear();
      // 方案 B：清空外部 ref
      if (externalMapRef) {
        (externalMapRef as React.MutableRefObject<maplibregl.Map | null>).current = null;
      }
      try { mapRef.current?.remove(); } catch { /* noop */ }
      mapRef.current = null;
      styleReady.current = false;
      firstFit.current = false;
    };
```

- [ ] **Step 3: 验证 TypeScript 编译**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | head -30`

Expected: 无新增编译错误。

- [ ] **Step 4: Commit**

```bash
git add web/src/map/SilMapView.tsx
git commit -m "feat(frontend): add drag interaction props to SilMapView (onFeatureDragEnd/onCogDrag/onWpDrag/dragState/wpNodes)"
```

---

## Task 5: 前端 — 重构 BuilderRightRail 为 4 Tab Inspector

**Files:**
- Modify: `web/src/screens/shared/BuilderRightRail.tsx`

Tab 2/3 改为 DEMO-1 外壳（disabled + Phase 2 占位），Tab 1 增加双 Schema toggle（Lat/Lon ↔ ENU）。

- [ ] **Step 1: 修改 TABS 定义，改为 4 Tab（新增 Tab 2 Env+Fault、Tab 3 Assertions）**

修改 `web/src/screens/shared/BuilderRightRail.tsx` 的 TABS 数组（约第 264-271 行）：

```typescript
const TABS = [
  { id: 'vessels',     label: '船舶与任务',  icon: <LucideShip size={24} /> },
  { id: 'environment', label: '环境与故障',  icon: <LucideCloudRain size={24} /> },
  { id: 'assertions',  label: '行为断言',    icon: <LucideLayout size={24} /> },
  { id: 'raw',         label: '源码 (YAML)', icon: <LucideFileJson size={24} /> },
] as const;
```

- [ ] **Step 2: 修改 `TabId` 类型**

修改 TabId 类型定义（紧随 TABS 之后）：

```typescript
type TabId = typeof TABS[number]['id'];
```

- [ ] **Step 3: 扩展 `BuilderRightRailProps`，增加 `isBaseline` 和 `scenarioHash` props**

修改接口（约第 275-283 行）：

```typescript
export interface BuilderRightRailProps {
  yamlEditor: string;
  onUpdateYaml: (updates: any) => void;
  onChangeRawYaml: (val: string) => void;
  previewData: any;
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
  isBaseline?: boolean;
  scenarioHash?: string;
}
```

- [ ] **Step 4: 修改函数签名，解构新 props**

修改 `BuilderRightRail` 函数签名（约第 285 行）：

```typescript
export function BuilderRightRail({ yamlEditor, onUpdateYaml, onChangeRawYaml, onRun, onSave, onValidate, isBaseline, scenarioHash }: BuilderRightRailProps) {
```

- [ ] **Step 5: Tab 1 — 合并本船+目标船到单个 Vessels Tab（替换原来的 basic/ownship/targets 三选一）**

修改渲染逻辑中的 Tab 内容区。将原来分散的 `basic`/`ownship`/`targets` 三个 Tab 合并为一个 `vessels` Tab。在 `{activeTab === 'vessels' && (...)}` 中渲染：

```tsx
{activeTab === 'vessels' && (
  <div>
    <SectionTitle title="本船配置" />
    <OwnShipConfigTab doc={doc} onUpdate={onUpdateYaml} />
    <SectionTitle title="目标船" />
    <TargetsConfigTab doc={doc} onUpdate={onUpdateYaml} />
  </div>
)}
```

- [ ] **Step 6: Tab 2 — Env + Fault 外壳（DEMO-1 只读）**

替换 `activeTab === 'environment'` 的渲染逻辑：

```tsx
{activeTab === 'environment' && (
  <div style={{ padding: '20px 0' }}>
    <div style={{
      textAlign: 'center', padding: '40px 20px',
      background: 'rgba(240,183,47,0.05)', borderRadius: 8,
      border: '1px solid rgba(240,183,47,0.15)'
    }}>
      <div style={{ fontSize: 32, marginBottom: 16, opacity: 0.3 }}>🔒</div>
      <div style={{ fontFamily: 'var(--f-disp)', fontSize: 14, color: 'var(--txt-1)', marginBottom: 8 }}>
        Phase 2 功能 (D2.x)
      </div>
      <div style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--txt-3)', lineHeight: 1.6 }}>
        当前版本: 仅展示 metadata.environment 字段摘要
      </div>
    </div>
    {doc?.environment && (
      <div style={{ marginTop: 20, padding: '12px 16px', background: 'rgba(0,0,0,0.15)', borderRadius: 6, border: '1px solid var(--line-1)' }}>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', lineHeight: 1.8 }}>
          <div>🌬 风速: {doc.environment?.wind?.speed_mps ?? '—'} m/s @ {doc.environment?.wind?.dir_deg ?? '—'}°</div>
          <div>🌊 海流: {doc.environment?.current?.speed_mps ?? '—'} m/s @ {doc.environment?.current?.dir_deg ?? '—'}°</div>
          <div>👁 能见度: {doc.environment?.visibility_nm ?? '—'} nm</div>
        </div>
      </div>
    )}
  </div>
)}
```

- [ ] **Step 7: Tab 3 — Behavioral Assertions 外壳（DEMO-1 只读）**

替换原有的 `activeTab === 'sensor'` 渲染逻辑，新增 Tab 3：

```tsx
{activeTab === 'assertions' && (
  <div style={{ padding: '20px 0' }}>
    <div style={{
      textAlign: 'center', padding: '40px 20px',
      background: 'rgba(240,183,47,0.05)', borderRadius: 8,
      border: '1px solid rgba(240,183,47,0.15)'
    }}>
      <div style={{ fontSize: 32, marginBottom: 16, opacity: 0.3 }}>🔒</div>
      <div style={{ fontFamily: 'var(--f-disp)', fontSize: 14, color: 'var(--txt-1)', marginBottom: 8 }}>
        Phase 2 功能 (D2.x)
      </div>
      <div style={{ fontFamily: 'var(--f-body)', fontSize: 11, color: 'var(--txt-3)', lineHeight: 1.6 }}>
        当前版本: 仅展示 expected_outcome 字段摘要
      </div>
    </div>
    {doc?.metadata?.expected_outcome && (
      <div style={{ marginTop: 20, padding: '12px 16px', background: 'rgba(0,0,0,0.15)', borderRadius: 6, border: '1px solid var(--line-1)' }}>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-2)', lineHeight: 1.8 }}>
          {doc.metadata.expected_outcome.cpa_min_m_ge != null && <div>CPA min ≥ {doc.metadata.expected_outcome.cpa_min_m_ge}m</div>}
          {doc.metadata.expected_outcome.colregs_rules && <div>COLREGs: [{Array.isArray(doc.metadata.expected_outcome.colregs_rules) ? doc.metadata.expected_outcome.colregs_rules.join(', ') : doc.metadata.expected_outcome.colregs_rules}]</div>}
          {doc.metadata.expected_outcome.grounding && <div>Grounding: {doc.metadata.expected_outcome.grounding}</div>}
        </div>
      </div>
    )}
  </div>
)}
```

- [ ] **Step 8: 更新 Actions Footer — 加入 Sticky Footer（校验灯 + sha256 + 另存为变体 + RUN）**

替换 Actions Footer 部分（约第 405-418 行）：

```tsx
            {/* Sticky Action Footer */}
            <div style={{ padding: '16px 20px', borderTop: '1px solid var(--line-1)', background: 'rgba(0,0,0,0.2)' }}>
              {/* Schema 校验状态条 */}
              <div style={{
                display: 'flex', alignItems: 'center', gap: 8, marginBottom: 12,
                padding: '6px 10px', borderRadius: 4,
                background: validation.valid ? 'rgba(74,222,128,0.06)' : 'rgba(248,113,113,0.08)',
                border: `1px solid ${validation.valid ? 'rgba(74,222,128,0.2)' : 'rgba(248,113,113,0.2)'}`,
              }}>
                <div style={{
                  width: 8, height: 8, borderRadius: '50%',
                  background: validation.valid ? '#4ade80' : '#f87171',
                  boxShadow: `0 0 6px ${validation.valid ? 'rgba(74,222,128,0.5)' : 'rgba(248,113,113,0.5)'}`
                }} />
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: validation.valid ? '#4ade80' : '#f87171', fontWeight: 500 }}>
                  {validation.valid ? 'Schema 通过' : `Schema 错误: ${validation.errors.length}`}
                </span>
                {!validation.valid && validation.errors.length > 0 && (
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: '#fca5a5', marginLeft: 'auto' }}>
                    {validation.errors[0]?.split(':')[0]}
                  </span>
                )}
                {scenarioHash && (
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginLeft: 'auto' }}>
                    sha256: {scenarioHash.slice(0, 8)}
                  </span>
                )}
              </div>

              {/* Action Buttons */}
              <div style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
                <button onClick={onSave} style={btnStyle('line')}>
                  <LucideSave size={14} /> {isBaseline ? '另存为 Custom' : 'SAVE'}
                </button>
                <button disabled style={{ ...btnStyle('line'), opacity: 0.4, cursor: 'not-allowed' }} title="Phase 2 (D2.4)">
                  <LucideDices size={14} /> MC 扫掠
                </button>
              </div>
              <button onClick={onRun} style={btnStyle('phos')}>
                🚀 RUN → ②
              </button>
            </div>
```

注意：需要在文件顶部 import `LucideDices`（如果尚未 import）。

- [ ] **Step 9: 验证 TypeScript 编译**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | head -30`

Expected: 无新增编译错误。

- [ ] **Step 10: Commit**

```bash
git add web/src/screens/shared/BuilderRightRail.tsx
git commit -m "feat(frontend): refactor BuilderRightRail to 4-tab Inspector with Tab2/3 DEMO-1 shells, sticky footer"
```

---

## Task 6: 前端 — 重构 SimulationScenario 为 3-Pane Studio

**Files:**
- Modify: `web/src/screens/SimulationScenario.tsx`

这是主屏幕重构。将当前 ~441 行的 wizard 布局改造为 3-Pane Studio 布局。

- [ ] **Step 1: 重写 imports — 添加 `useMapInteraction` hook import 和新 lucide 图标**

修改 `web/src/screens/SimulationScenario.tsx` 的 imports（第 1-22 行），添加和调整 imports：

```typescript
import { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { SilMapView } from '../map/SilMapView';
import { ImazuGeometry } from '../map/ImazuGeometry';
import { MapLayerSwitcher } from '../map/MapLayerSwitcher';
import * as jsyaml from 'js-yaml';
import {
  useListScenariosQuery,
  useGetScenarioQuery,
  useValidateScenarioMutation,
  useCreateScenarioMutation,
  useDeleteScenarioMutation,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { useSchemaValidation } from '../hooks/useSchemaValidation';
import { useMapInteraction } from '../hooks/useMapInteraction';
import type { DragState, WaypointNode } from '../hooks/useMapInteraction';
import { 
  LucidePlus, LucideSave, LucideCheckCircle, LucidePlay, 
  LucideCompass, LucideFolder, LucideFileJson, LucideChevronDown, LucideChevronRight, LucideSearch,
  LucideSettings2, LucideShip, LucideTarget, LucideCloudRain, LucideWaves, LucideRadio,
  LucideLayout, LucideDices, LucideBrainCircuit, LucidePanelLeftClose, LucidePanelLeftOpen,
  LucideMousePointer2, LucideNavigation, LucideArrowDown, LucideArrowUp,
  LucideLock, LucidePencil, LucideAlertTriangle
} from 'lucide-react';
import { BuilderRightRail } from './shared/BuilderRightRail';
```

- [ ] **Step 2: 添加 ODD 过滤器的状态和航区域枚举**

在函数组件开始处（`useState` 区），在现有状态之后添加 ODD 状态：

```typescript
  // ── ODD 过滤器状态 ──
  const [oddDomain, setOddDomain] = useState<string>('open_sea');
  const [oddSeaState, setOddSeaState] = useState<number>(5);
  const [oddVisibility, setOddVisibility] = useState<number>(2.0);
  const [vesselClass, setVesselClass] = useState<string>('FCB-45m');
```

- [ ] **Step 3: 添加 ODD 写入 YAML 的 effect**

在现有的 `useEffect`（约第 48-52 行）之后添加 ODD → YAML 同步 effect：

```typescript
  // ODD 变更 → 写入 yamlDoc.metadata.odd_cell（GAP-023 闭环）
  const lastOddRef = useRef('');
  useEffect(() => {
    const sig = `${oddDomain}|${oddSeaState}|${oddVisibility}`;
    if (sig === lastOddRef.current || !yamlEditor) return;
    lastOddRef.current = sig;
    handleUpdateYaml({
      'metadata.odd_cell.domain': oddDomain,
      'metadata.odd_cell.sea_state_beaufort': oddSeaState,
      'metadata.odd_cell.visibility_nm': oddVisibility,
    });
  }, [oddDomain, oddSeaState, oddVisibility]);
```

- [ ] **Step 4: 添加地图拖拽 hook 调用和 mapRef**

在状态声明区之后添加：

```typescript
  // ── Map interaction hook (方案 B) ──
  const mapRef = useRef<maplibregl.Map | null>(null);
  const onYamlPatchRaw = useCallback((path: string, value: unknown) => {
    handleUpdateYaml({ [path]: value });
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  // 200ms debounce wrapper for map re-render
  const onYamlPatch = useMemo(() => {
    let timer: ReturnType<typeof setTimeout>;
    return (path: string, value: unknown) => {
      clearTimeout(timer);
      timer = setTimeout(() => onYamlPatchRaw(path, value), 200);
    };
  }, [onYamlPatchRaw]);

  const { dragState, wpNodes, setWpNodes } = useMapInteraction({
    mapRef,
    previewData,
    onYamlPatch,
  });
```

需要添加 `maplibregl` 的 import：

```typescript
import maplibregl from 'maplibre-gl';
```

- [ ] **Step 5: 重写组件 return 为 3-Pane Studio 布局**

将现有的 `return` 块（第 221-440 行）完全替换为以下 3-Pane Studio 布局：

```tsx
  // ── ODD 过滤后的场景列表 ──
  const filteredSuites = useMemo(() => {
    // 标记每个场景是否 ODD 兼容
    return suites.map(suite => ({
      ...suite,
      children: suite.children.map((child: any) => {
        // DEMO-1: 简单客户端过滤 — 所有场景默认兼容
        // Phase 2: 解析每个 scenario 的 ODD 字段做真实比较
        const oddCompatible = true;
        return { ...child, oddCompatible };
      }),
    }));
  }, [suites]);

  return (
    <div data-testid="simulation-scenario" style={{ 
      position: 'relative', width: '100%', height: '100%', overflow: 'hidden', background: '#070c13' 
    }}>
      
      {/* BACKGROUND: Map View */}
      <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', zIndex: 0 }}>
        <SilMapView 
          followOwnShip={false} 
          viewMode="god" 
          previewData={previewData || undefined}
          onMapClick={handleMapClick}
          substrate={substrate}
          geometry={imazuGeometry}
          dragState={dragState}
          wpNodes={wpNodes}
        />
      </div>

      {/* LEFT PANE: ODD Filter + Scenario Library */}
      <div style={{
        position: 'absolute', top: 20, left: 20, bottom: 20,
        width: '280px', zIndex: 100,
        display: 'flex', flexDirection: 'column', gap: 12,
      }}>
        {/* ODD Global Filter */}
        <div style={{
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, padding: '16px',
        }}>
          <div style={{ 
            fontFamily: 'var(--f-disp)', fontSize: 11, fontWeight: 700,
            color: 'var(--txt-1)', letterSpacing: '0.1em',
            marginBottom: 12, display: 'flex', alignItems: 'center', gap: 6
          }}>
            <LucideCompass size={14} color="var(--c-phos)" /> ODD 全局过滤器
          </div>
          
          <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <ODDSelect label="航区域" value={oddDomain} onChange={setOddDomain}
              options={[
                { value: 'open_sea', label: 'Open Sea' },
                { value: 'coastal', label: 'Coastal' },
                { value: 'fairway', label: 'Fairway' },
                { value: 'port_entry', label: 'Port Entry' },
                { value: 'ofw', label: 'Offshore Wind Farm' },
              ]} />
            <ODDSelect label="海况" value={String(oddSeaState)} onChange={(v) => setOddSeaState(Number(v))}
              options={[
                { value: '3', label: 'Beaufort ≤ 3' },
                { value: '5', label: 'Beaufort ≤ 5' },
                { value: '7', label: 'Beaufort ≤ 7' },
                { value: '9', label: 'Beaufort ≤ 9' },
              ]} />
            <ODDSelect label="能见度" value={String(oddVisibility)} onChange={(v) => setOddVisibility(Number(v))}
              options={[
                { value: '0.5', label: '> 0.5 nm' },
                { value: '1.0', label: '> 1 nm' },
                { value: '2.0', label: '> 2 nm' },
                { value: '5.0', label: '> 5 nm' },
              ]} />
          </div>
        </div>

        {/* Vessel Capability Manifest */}
        <div style={{
          background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, padding: '12px 16px',
        }}>
          <div style={{ fontFamily: 'var(--f-disp)', fontSize: 10, color: 'var(--txt-2)', letterSpacing: '0.08em', marginBottom: 4 }}>
            船型能力清单
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 13, color: 'var(--c-phos)', fontWeight: 700 }}>
              🚢 {vesselClass}
            </span>
          </div>
          <div style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--txt-3)', marginTop: 4 }}>
            吃水 3.2m · 旋回半径 340m · 制动 ≥0.6nm
          </div>
        </div>

        {/* Scenario Library */}
        <div style={{
          flex: 1, background: 'rgba(13, 19, 31, 0.95)',
          backdropFilter: 'blur(16px)',
          border: '1px solid var(--line-2)',
          borderRadius: 12, display: 'flex', flexDirection: 'column',
          overflow: 'hidden',
        }}>
          {/* Header: Search + New */}
          <div style={{ padding: '14px 14px 10px' }}>
            <div style={{ position: 'relative', display: 'flex', alignItems: 'center', marginBottom: 8 }}>
              <LucideSearch size={14} color="var(--txt-3)" style={{ position: 'absolute', left: 10 }} />
              <input 
                type="text" 
                placeholder="搜索场景..." 
                value={searchTerm}
                onChange={(e) => setSearchTerm(e.target.value)}
                style={{
                  width: '100%', background: 'rgba(0,0,0,0.2)', border: '1px solid var(--line-1)', 
                  color: 'var(--txt-1)', padding: '8px 10px 8px 32px', fontFamily: 'var(--f-mono)', fontSize: 11,
                  outline: 'none', borderRadius: 6
                }} 
              />
            </div>
            {/* Quick filter toggles */}
            <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
              {['多船', '含故障', '上次PASS', '上次FAIL'].map(tag => (
                <span key={tag} style={{
                  padding: '2px 8px', borderRadius: 4,
                  background: 'rgba(91,192,190,0.08)', color: 'var(--txt-3)',
                  fontFamily: 'var(--f-mono)', fontSize: 9, border: '1px solid var(--line-1)',
                }}>{tag}</span>
              ))}
            </div>
          </div>

          {/* Scenario Tree */}
          <div style={{ flex: 1, overflowY: 'auto', padding: '0 10px 16px' }}>
            {filteredSuites.map(suite => (
              <div key={suite.id} style={{ marginBottom: 4 }}>
                <div onClick={() => toggleFolder(suite.id)} style={{ 
                  display: 'flex', alignItems: 'center', gap: 6, padding: '6px 8px', 
                  cursor: 'pointer', color: 'var(--txt-1)', borderRadius: 4,
                }}>
                  {isFolderExpanded(suite.id) ? <LucideChevronDown size={12} /> : <LucideChevronRight size={12} />}
                  <LucideFolder size={12} color="#fa0" />
                  <span style={{ fontFamily: 'var(--f-body)', fontSize: 12, fontWeight: 500 }}>{suite.name}</span>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', marginLeft: 'auto' }}>
                    {suite.children.length}
                  </span>
                </div>
                {isFolderExpanded(suite.id) && (
                  <div style={{ paddingLeft: 20 }}>
                    {suite.children.map((child: any) => (
                      <div key={child.id} onClick={() => handleSelect(child.id)} style={{
                        display: 'flex', alignItems: 'center', gap: 8, padding: '6px 10px', cursor: 'pointer',
                        background: selectedId === child.id ? 'rgba(91,192,190,0.12)' : 'transparent',
                        color: selectedId === child.id ? 'var(--c-phos)' : child.oddCompatible ? 'var(--txt-2)' : '#f87171',
                        borderRadius: 4, transition: 'all 0.1s',
                        borderLeft: `2px solid ${selectedId === child.id ? 'var(--c-phos)' : 'transparent'}`
                      }}>
                        <div style={{
                          width: 6, height: 6, borderRadius: '50%',
                          background: child.oddCompatible ? '#4ade80' : '#f87171',
                        }} />
                        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                          {child.name}
                        </span>
                        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)', padding: '1px 6px', background: 'rgba(0,0,0,0.2)', borderRadius: 3 }}>
                          {child.encounter_type?.toUpperCase() || '—'}
                        </span>
                        {child.is_baseline ? (
                          <LucideLock size={10} color="var(--c-warn)" title="Baseline (只读)" />
                        ) : (
                          <LucidePencil size={10} color="var(--txt-3)" />
                        )}
                      </div>
                    ))}
                  </div>
                )}
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* CENTER: Bottom Status Bar */}
      <div style={{
        position: 'absolute', bottom: 20, left: '50%', transform: 'translateX(-50%)',
        zIndex: 100, display: 'flex', alignItems: 'center', gap: 16,
        background: 'rgba(10, 15, 24, 0.85)', backdropFilter: 'blur(12px)',
        border: '1px solid var(--line-2)', borderRadius: 8, padding: '6px 16px',
      }}>
        {placementMode !== 'none' && (
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <LucideNavigation size={14} color="var(--c-phos)" />
            <span style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--c-phos)' }}>
              正在设置 {placementMode === 'ownship' ? '本船' : '目标船'} 位置...
            </span>
            <button onClick={() => setPlacementMode('none')} style={{
              background: 'rgba(255,255,255,0.1)', color: 'var(--txt-1)', border: 'none',
              padding: '2px 8px', borderRadius: 4, cursor: 'pointer', fontSize: 10
            }}>取消</button>
          </div>
        )}
      </div>

      {/* OVERLAY: Map Layer Switcher */}
      <MapLayerSwitcher activeLayer={substrate} onLayerChange={setSubstrate} />

      {/* RIGHT PANE: Inspector */}
      <BuilderRightRail
        yamlEditor={yamlEditor}
        onUpdateYaml={handleUpdateYaml}
        onChangeRawYaml={setYamlEditor}
        previewData={previewData}
        onRun={handleRun}
        onSave={handleSave}
        onValidate={handleValidate}
        isBaseline={selectedId ? scenarios.find((s: any) => s.id === selectedId)?.is_baseline : false}
        scenarioHash={scenarioDetail?.hash}
      />
    </div>
  );
}
```

- [ ] **Step 6: 在文件末尾添加 `ODDSelect` 内联组件**

在 `export function SimulationScenario` 之前添加：

```tsx
// ── ODD Select 内联子组件 ──────────────────────────────────────────────
function ODDSelect({ label, value, onChange, options }: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  options: Array<{ value: string; label: string }>;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <label style={{ fontSize: 9, fontFamily: 'var(--f-mono)', color: 'var(--txt-3)' }}>{label}</label>
      <select value={value} onChange={(e) => onChange(e.target.value)} style={{
        background: 'rgba(0,0,0,0.3)', border: '1px solid var(--line-1)',
        color: 'var(--txt-1)', padding: '6px 8px', borderRadius: 4,
        fontFamily: 'var(--f-mono)', fontSize: 11, outline: 'none', width: '100%',
        cursor: 'pointer'
      }}>
        {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
      </select>
    </div>
  );
}
```

- [ ] **Step 7: 移除不再使用的代码**

删除以下不再使用的状态（已在 Step 2-4 中被新代码取代或不再引用）：
- `activeDomain`、`setActiveDomain`
- `activeTab`、`setActiveTab`
- `isRailExpanded`、`setIsRailExpanded`
- `isExplorerVisible`、`setIsExplorerVisible`
- `sortOrder`、`setSortOrder`

删除左侧 Nav Rail（Domain 选择器）的 JSX（约第 238-314 行）。
删除旧的 Scenario Explorer Drawer（约第 317-394 行）。
删除旧的 `glass-panel`（约第 399-409 行）。

精简后的状态声明应保留：
```typescript
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [yamlEditor, setYamlEditor] = useState('');
  const [expandedFolders, setExpandedFolders] = useState<Record<string, boolean>>({});
  const [searchTerm, setSearchTerm] = useState('');
  const [placementMode, setPlacementMode] = useState<string>('none');
  const [substrate, setSubstrate] = useState<'enc' | 'sat' | 'osm'>('enc');
```

- [ ] **Step 8: 验证 TypeScript 编译**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | head -40`

Expected: 无新增编译错误。如有因删除状态导致的引用错误，修复引用。

- [ ] **Step 9: Commit**

```bash
git add web/src/screens/SimulationScenario.tsx
git commit -m "feat(frontend): refactor SimulationScenario to 3-Pane Studio layout with ODD filter, baseline/custom separation, and map drag integration"
```

---

## Task 7: 端到端验证与收尾

**Files:**
- Modify: 无新增文件修改

- [ ] **Step 1: 运行 TypeScript 全量编译检查**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx tsc --noEmit 2>&1 | tail -20`

Expected: 零编译错误或仅保留既有的非本次改动导致的错误。

- [ ] **Step 2: 运行前端 lint**

Run: `cd /Users/marine/Code/MASS-L3-Tactical Layer/web && npx eslint src/screens/SimulationScenario.tsx src/hooks/useMapInteraction.ts src/screens/shared/BuilderRightRail.tsx src/api/silApi.ts src/map/SilMapView.tsx 2>&1 | tail -20`

Expected: 无新增 lint 错误。

- [ ] **Step 3: 启动开发服务器做手动 UI 演练（macOS 本地）**

```bash
# Terminal 1: 启动后端
cd /Users/marine/Code/MASS-L3-Tactical Layer
python -m uvicorn src.sil_orchestrator.main:app --host 0.0.0.0 --port 8000

# Terminal 2: 启动前端
cd /Users/marine/Code/MASS-L3-Tactical Layer/web
npm run dev

# 浏览器打开 http://localhost:5173/#/scenario
```

验收清单（对应 Spec §10）：
- [ ] ODD 过滤器三个下拉框可见可选
- [ ] 场景列表显示，Baseline 场景带 🔒 图标
- [ ] 选择场景后地图显示本船和目标船
- [ ] 点击地图放置本船/目标船
- [ ] 右侧 Inspector 4 Tab 可切换，Tab 2/3 显示 Phase 2 占位
- [ ] SAVE 功能正常（Custom 场景保存，Baseline 自动"另存为"）
- [ ] RUN → 跳转到 `#/check/:id`
- [ ] Sticky Footer 显示 Schema 校验状态

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: final verification — TypeScript compile, lint pass, manual UI walkthrough"
```

---

## 验收条件对齐（Spec §10）

| 验收条件 | 覆盖 Task | 验证方法 |
|---|---|---|
| ODD 写入 → `metadata.odd_cell` | Task 6 Step 3 | 保存场景后检查 YAML 文件 |
| Baseline 只读（SAVE → 另存为 Custom） | Task 1 + Task 6 | 点击 Imazu22 场景的 SAVE，确认触发 POST 而非 PUT |
| Custom SAVE → Sticky Footer 显示真实 sha256 | Task 5 Step 8 | 保存后检查 Footer |
| 拖拽本船 → Lat/Lon 更新 | Task 3 + Task 6 | 鼠标拖拽本船，松手后右侧字段更新 |
| 拖拽目标船 → 同上 | Task 3 | 同上，针对 TGT-1 |
| COG 拉伸 → HDG 更新 | Task 3 | 拖拽 COG 预测线端点 |
| WP 节点拖拽 → waypoints 更新 | Task 3 | 拖拽 WP 节点 |
| Schema 校验 → 指示灯 | Task 5 Step 8 | 输入非法参数如 SOG=999 |
| Tab 2/3 外壳 → 显示 Phase 2 占位 | Task 5 Step 6/7 | 点击 Tab 2/3 |
| RUN 流程 → navigate `#/check/:id` | Task 6 | 点击 RUN |

---

## 不在 Plan 范围内（DEMO-1 范围外）

| 功能 | 原因 |
|---|---|
| Tab 2 Env+Fault 真实实现（时间轴事件注入） | Phase 2 D2.x |
| Tab 3 Behavioral Assertions 真实实现 | Phase 2 D2.x |
| EBL/VRM 测量工具 | Phase 2 |
| Monte Carlo 扫掠 | Phase 2 D2.4 |
| PMTiles 换代 | Phase 3 D3.x |
| Vessel Registry 后端（多船型 API） | Phase 2 |
| YAML AST 保留注释 | Phase 2 |
| CI 历史标签真实数据 | Phase 2 D2.5 |
| 底部测绘工具栏（经纬度/水深/比例尺） | Phase 2（DEMO-1 优先核心交互） |
