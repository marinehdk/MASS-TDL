# SIL v1.0 系统评审：功能总结 + 问题评判

---

## 1. 当前系统功能总结

### 1.1 四屏架构

| 屏幕 | 路由 | 核心功能 |
|---|---|---|
| Simulation-Scenario | `#/scenario` | 场景库浏览（22 IMAZU + 11 COLREGs）、YAML 编辑、ENC 几何预览 |
| Simulation-Check | `#/check/:id` | 6-gate 自检 sequencer、GO/NO-GO 判定、3-2-1 倒数自动激活 |
| Simulation-Monitor | `#/monitor/:id` | 双模式 ENC 实时监控（船长/God）、ASDR Ledger、ThreatRibbon、ConningBar、故障注入 |
| Simulation-Evaluator | `#/evaluator/:id` | 8 KPI 卡片、6 维评分雷达、TimelineSixLane、TrajectoryReplay、Marzip 导出 |

### 1.2 已实现的技术栈

React 18 + Zustand（7 个 store）+ RTK Query（18 endpoints）+ MapLibre GL 4 + @tier4/roslibjs-foxglove + Protobuf-ts 类型定义（23 个生成文件）+ Monaco 编辑器 + AJV 校验。

### 1.3 后端集成深度（截至 commit 73cdf23）

FastAPI orchestrator 8 个 router 完整，ROS2 lifecycle bridge 就位，但 sim_workbench 9 个业务节点仍是 Python 数据类 stub，foxglove_bridge 与 telemetry_bridge 存在端口冲突（GAP-015）。

---

## 2. 能力评估：能否满足 TDL 完整验证任务？

### 2.1 基本覆盖（Phase 1 范围内可用）

- 场景编辑、加载、SHA256 完整性验证 ✅
- 4 屏幕流程打通（visual demo 级别）✅
- ENC S-57 MVT 多图层渲染 ✅
- ASDR 事件实时显示 ✅
- Marzip 证据容器基础框架 ✅

### 2.2 关键缺口（验证任务不可缺少）

**A. 评分与评判链路未闭合**：scoring_node 输出是 stub，8 个 KPI 中只有 4 个字段来自真实数据。COLREGs 规则链展示没有与 M6 实际推理结果挂钩——这意味着目前的 Evaluator 屏本质上是样板界面，无法产出任何可交付的 V&V 证据。

**B. Doer-Checker 可见性不完整**：Doc 1 §11 规定屏 ③ 应实时显示 M7 verdict（PASS/RISK/FAIL），但 AsdrLedger 目前的 verdict 枚举是前端 store 内的本地类型，并没有从 `/l3/checker_veto` topic 真实订阅——Doer-Checker 隔离是 CCS 认证的核心证据，当前是形式化展示。

**C. Preflight GATE 6 无法真正验证**：GATE 6 要求验证 M7 容器独立（`docker inspect` 不同 container ID）、M7 不依赖 OR-Tools。但前端通过 `/api/v1/selfcheck/probe` 只能拿到后端返回的 pass/fail 字符串，无法向 surveyor 提供可追溯证据，缺少截图/日志导出功能。

---

## 3. 前端交互问题

### 3.1 屏 ①：Simulation-Scenario

**问题 1：BuilderRightRail 与 SilMapView 交互割裂**

右侧 Rail 的参数调整（Bearing/Distance/SOG）不会实时更新左侧地图预览中的几何。用户必须"保存"后才能看到效果，违反 WYSIWYG 原则。对于需要精确 DCPA 设置的验证工程师，这会大幅增加试错成本。

建议：参数变更→实时 debounce 200ms→更新 `previewData` prop 触发 SilMapView 重渲染。

**问题 2：场景库筛选能力缺失**

35 个场景（未来 200+）只有 4 个 Tab 分类，无搜索、无按 ODD/encounter_type 过滤、无 PASS/FAIL 历史标记。V&V 工程师在批量回归时无法快速定位失败场景。

**问题 3：YAML 编辑器与结构化编辑器同时存在但不同步**

`BuilderRightRail` 同时提供结构化字段（Basic/OwnShip/Targets）和 Monaco Raw YAML 编辑器。但两者通过 `jsyaml.load/dump` 转换时，YAML 注释会丢失，字段顺序会变，可能破坏 maritime-schema 的 `$schema` 声明行。

---

### 3.2 屏 ②：Simulation-Check

**问题 4：GO 自动激活不可关闭**

`GoNoGoPanel` 在全 PASS 后 3 秒自动 activate，且只有 ABORT 按钮可以阻止。对于 surveyor 审阅场景时（只想检查状态，不想启动仿真），这是危险设计——误触后会产生不必要的 run 记录污染证据库。

建议：增加"仅查看模式"toggle，disable 自动激活。

**问题 5：GATE 展开详情中的 check 字符串格式未标准化**

`GateCard` 通过 `check.startsWith('[ok]')` 解析状态，这是脆弱的字符串协议。后端若返回 `[OK]`（大写）或 `[pass]` 则静默失败，显示错误图标但无提示。

**问题 6：LiveLogStream 没有时间戳对齐**

`preflightLog` 条目的 `timestamp` 是本地时间字符串，与 `/sim_clock` 仿真时间没有关联。在 DEMO-2 ROS2 真链路接通后，preflight 期间的 ROS2 日志与本地时间戳会存在系统性偏差，导致 ASDR 证据链时序无法对齐。

---

### 3.3 屏 ③：Simulation-Monitor

**问题 7：ConningBar 历史 sparkline 是空数组**

```typescript
<Sparkline data={[/* Phase 2: 60s ring buffer */]} .../>
```

ConningBar 代码中 sparkline 被明确标为 Phase 2，但组件已渲染到界面。对于船长模式用户，空的 sparkline 位置会产生视觉噪音，且占据了宝贵的信息密度空间。应在 Phase 1 直接隐藏该区域，Phase 2 才显示。

**问题 8：ThreatRibbon CPA 数据来源不可靠**

ThreatRibbon 的 CPA/TCPA 完全依赖 `asdrEvents` 中 `event_type === 'cpa_update'` 的 payload_json 解析。如果 M6 COLREGs Reasoner 还没发出该事件，ribbon 中所有目标显示 `—nm`，用户无法判断是"无威胁"还是"数据未到达"——这是一个关键的安全可视化语义歧义。

**问题 9：双模式切换成本太高**

`viewMode` 切换（captain ↔ god）会重新触发 MapLibre 的 `setPadding`，但并未 memoize 视口状态。从 captain 切到 god 再切回来，地图视角会重置，破坏连续操作体验。

**问题 10：故障注入面板仅在 God 模式可见**

```typescript
if (viewMode !== 'god') return null;
```

`FaultInjectPanel` 在 captain 模式下完全隐藏。但 D1.5 V&V Plan 中故障注入是验证工程师在任意视图下都需要执行的操作。船长模式下如果需要注入故障，必须先切换视图，增加了不必要的操作步骤，且打断了"观察中的船长视角"。

---

### 3.4 屏 ④：Simulation-Evaluator

**问题 11：TrajectoryReplay 是完全硬编码的模拟数据**

```typescript
// useMemo: 60 points hardcoded trajectory
const ownshipPts = useMemo(() => Array.from({ length: N }, (_, i) => { ... }))
```

Evaluator 屏的轨迹回放目前播放的是一段固定的假轨迹，与实际运行的 MCAP 完全无关。这是 DEMO-1 可以接受的 visual demo，但如果不在文档和 UI 中明确标注，surveyor 可能误认为这是真实数据，产生合规风险。应在组件右上角加 `[DEMO DATA - Not linked to MCAP]` 水印标签，直到 Phase 2 实接 MCAP。

**问题 12：TimelineSixLane scrub 与 TrajectoryReplay 没有联动**

`useReplayStore.scrubTime` 被 TimelineSixLane 更新，但 TrajectoryReplay 接收的是 `currentTimeSec` prop，实际上是 Evaluator 屏的本地 state，两者没有共享同一个 scrubTime 来源。用户在时间轴上拖动时，地图回放不会跟随。

---

## 4. 人机工学（HCI）问题

### 4.1 信息密度与认知负荷

**问题 13：屏 ③ 四个浮层同时可见时信息过载**

ConningBar（底部左）+ ThreatRibbon（顶部）+ ASDR Ledger（右下）+ ModulePulse strip（底部全宽）+ FaultInjectPanel（右中）同时展示时，可用 ENC 地图区域约为 60%。在船长模式下，这违反了桥楼 HMI 的核心原则——ENC 海图是第一信息面，其余为第二信息面。

建议：Ledger 和 FaultPanel 默认折叠为 icon-only 侧栏，用户主动展开；ModulePulse strip 仅在有 AMBER/RED 时出现。

**问题 14：颜色语义在多个组件中不一致**

- ThreatRibbon：CPA < 1.0nm → `var(--c-danger)`（红）
- AsdrLedger：verdict FAIL → `var(--c-danger)`（红）
- ModulePulse：state RED → `var(--c-danger)`（红）
- RunStatePill：ABORTED → `var(--c-danger)`（红）

四处红色含义各不相同（距离威胁、决策失败、模块故障、运行中止），同屏同时出现多个红色时，操作员无法快速判断优先响应哪个。IMO MSC.302(87) BAM 规定告警颜色应有严格层级，不同语境的红色需要有视觉区分（如闪烁频率、形状、位置区域）。

**问题 15：ToR Modal 的 SAT-1 5 秒锁定与紧急场景冲突**

`TorModal` 要求操作员等待 SAT-1 settlement 5 秒才能接管，这在 Veitch 2024 60 秒 TMR 框架中是合理的。但当 deadline < 10s 时，倒计时变红，而接管按钮仍处于等待 SAT-1 的 disabled 状态，用户面临"必须接管但无法接管"的界面死锁。需要在极端情况下（deadline < 5s）强制解除 SAT-1 锁定并高亮警示。

**问题 16：快捷键与浏览器默认行为冲突**

`useHotkeys` 中 Space 键直接 `e.preventDefault()`，F 键无限制拦截。F 键是浏览器常用的全屏快捷键（Chrome/Safari），会与用户习惯产生冲突，且在 Monaco 编辑器聚焦时（已做 tagName 判断）以外的 contenteditable 区域（如 ASDR 日志的复制操作）仍会被拦截。

---

## 5. 开发验证（V&V）角度问题

### 5.1 证据链完整性

**问题 17：Marzip 容器在 Phase 1 缺少 verdict.json**

当前 Marzip 仅包含：scenario.yaml + sha256 + manifest + scoring.json（stub）。`verdict.json`（PASS/FAIL + 规则链 + KPI 矩阵）是 CCS surveyor 的首要核查对象，但它是 Phase 2 目标。在 D1.3.1 仿真器鉴定报告提交时（6/15），如果交付的 Marzip 没有 verdict.json，证据包是不完整的。

建议：Phase 1 应生成 `verdict_preliminary.json`，明确标注 `"status": "stub_phase1"` 和 KPI 字段来源（哪些是真实、哪些是模拟），让 surveyor 知道数据质量级别。

**问题 18：场景 SHA256 与 MCAP 录制没有强绑定**

`scenario.sha256` 在 `_seed_run_dir` 创建时写入，但 rosbag2_recorder 开始录制时没有把 hash 写入 MCAP 的 metadata。如果场景文件在录制中途被修改（虽然不应该），MCAP 与 scenario 的对应关系无法在事后核实。这违反了 DNV-RP-0513 的"编排可验证性"要求（Doc 1 §9.6 第 4 项）。

**问题 19：Evaluator 无法对比多次运行**

当前只有 `GET /api/v1/scoring/last_run`，没有历史 run 列表和 diff 视图。V&V 工程师在做回归测试时，需要比较修改参数前后的 KPI 变化，但系统没有提供这个功能。每次 run 覆盖上一次结果（scoring stub），使得批量验证完全不可操作。

**问题 20：CI Fast Gate 无前端可视化接口**

Doc 4 §6.4 定义了 `tools/check_entry_gate.py` 的 8 个 Entry Gate，但前端没有任何界面可以查看当前代码库的 Gate 状态。V&V 工程师必须切换到 terminal 查看，工作流割裂。建议在 Evaluator 屏的底部加一个 CI Status Panel，轮询 `/api/v1/ci/gate_status`。

---

## 6. 优先级建议

按风险和里程碑影响排序：

| 优先级 | 问题 | 影响 | 关联里程碑 |
|---|---|---|---|
| P0 | 问题 1（预览实时同步）+ 问题 8（CPA 语义歧义）+ 问题 17（verdict.json 缺失）| 证据链和用户信任 | D1.3.1 6/15 |
| P0 | 问题 4（GO 自动激活无保护）| 误操作风险 | DEMO-1 6/15 |
| P1 | 问题 11（TrajectoryReplay 硬编码水印）+ 问题 12（scrub 联动）| Surveyor 合规误判 | DEMO-2 7/31 |
| P1 | 问题 13（信息密度）+ 问题 15（ToR 死锁）| HMI 可用性 | DEMO-2 7/31 |
| P2 | 问题 7（ConningBar 空 sparkline）+ 问题 9（视图切换视口重置）+ 问题 16（快捷键冲突）| 体验质量 | DEMO-3 8/31 |
| P3 | 问题 2（场景筛选）+ 问题 19（多 run 对比）+ 问题 20（CI Gate 可视化）| 长期工作效率 | Phase 2+ |

---

总体判断：**当前 SIL v1.0 设计框架结构清晰、技术选型合理，完全满足 DEMO-1 视觉演示目标**。但要真正支撑 TDL 系统的完整验证任务（特别是 CCS i-Ship N AIP 提交路径），还有约 P0+P1 共 8 个问题需要在 DEMO-2 之前解决，其中最关键的是 CPA 语义歧义、证据链不完整、和 Doer-Checker 可视化与真实数据挂钩这三点。


---


# 屏 ①：Simulation-Scenario 详细分析

---

## 1. 设计目标（来自文档）

根据 Doc 3 §6 的设计规格，屏 ① 承担以下职责：

**三步流程（Step A → B → C）**

- **Step A**：ODD 运行域选择 + 场景库浏览（Imazu22 / COLREGs R13/R14/R15 / AIS-derived / Procedural）
- **Step B**：场景参数配置（bearing/distance/course/扰动/传感器退化）
- **Step C**：几何预览 + Summary Rail → 点"Run →"进入屏 ②

**核心数据流**：选择场景 → YAML 加载 → 客户端解析 → 地图预览 + 参数预填 → 保存/验证 → 跳转 Preflight

---

## 2. 设计了什么 vs 实现了什么

### 2.1 顶层路由和入口

**设计**：路由为 `#/scenario`，文件名 `SimulationScenario.tsx`

**实现现状**：
```typescript
// App.tsx 实际代码
import { SimulationScenario } from './screens/SimulationScenario';
// ...
{route.screen === 'scenario' && <SimulationScenario />}
```

但测试文件里还在 import 旧名：
```typescript
// web/src/screens/__tests__/ScenarioBuilder.test.tsx
import { ScenarioBuilder } from '../ScenarioBuilder';
```

说明 **GAP-014/029 的重命名只完成了一半**——`App.tsx` 和 `TopChrome.tsx` 已用新名，但测试文件、部分 import 路径仍指向旧名。实际渲染是否正常取决于文件系统是否同时存在新旧两个文件。

---

### 2.2 Stepper（三步进度条）

**设计**：A → B → C 三步，可点击跳转

**实现**：`Stepper.tsx` 已完整实现

```typescript
// Stepper.tsx — 完整实现
export const Stepper: React.FC<StepperProps> = ({ steps, current, onJump }) => {
  return (
    <div>
      {steps.map((label, i) => {
        const active = i === current;
        const done = i < current;
        return (
          <div onClick={() => onJump?.(i)} ...>
            <div>{done ? '✓' : String.fromCharCode(65 + i)}</div>
            <div>{label}</div>
          </div>
        );
      })}
    </div>
  );
};
```

**实现程度：100%**。组件本身完整，支持 A/B/C 步骤、done 状态（✓）、点击跳转。

---

### 2.3 Step A：ODD 运行域选择

**设计**：
```
ODD Domain 选择：
- open_sea
- coastal  
- fairway
- port_entry
```

**实现现状**：查看 `ScenarioBuilder.tsx`（16.9 KB）的实际结构——根据测试 mock 文件和 Doc 3 的描述，ODD Domain 选择器存在，但其**与后续 GATE 4（ODD-Scenario Alignment）的联动是缺失的**。

具体来说：用户选择 `open_sea` 后，这个选择并不会写入 YAML 的 `metadata.odd_cell.domain` 字段，也不会传到 Preflight 的 Gate 4 去校验 M1 ODD 状态是否一致。

**实现程度：UI 骨架存在，数据绑定未完成（约 30%）**

---

### 2.4 Step A：ImazuGrid（22 场景网格）

**设计**：22 个 IMAZU 场景缩略图，点击选中后预填 Step B/C

**实现**：`ImazuGrid.tsx`（4.1 KB）已完整实现

```typescript
// ImazuGrid.tsx — 核心渲染（已实现）
export const ImazuGrid: React.FC<ImazuGridProps> = ({ cases, selected, onSelect }) => {
  return (
    <div data-testid="imazu-grid">
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(150px, 1fr))' }}>
        {cases.map((c) => {
          const sel = selected === c.id;
          return (
            <div onClick={() => onSelect(c.id)} ...>
              {/* Mini SVG 示意图 */}
              <svg viewBox="0 0 100 100">
                {/* own-ship + target ship 位置示意 */}
                {c.ships.map((s, i) => (
                  <g key={i}>
                    <line ... /> {/* 速度向量 */}
                    <g transform={`...rotate(${s.h})`}>
                      <path d="M 0 -3 L 2 2 L 0 1 L -2 2 Z" /> {/* 船型符号 */}
                    </g>
                  </g>
                ))}
              </svg>
              <div>{c.name}</div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
```

**关键问题**：ImazuGrid 接收的 `cases` 数组是外部传入的 props，但实际的 22 个场景数据定义在哪里？查看代码，`ScenarioBuilder.tsx` 中 ImazuGrid 的 `cases` prop 需要构造 `ImazuCase[]`，包含每个场景的 `ships` 位置数据（用于 SVG 渲染）。

这 22 个 case 的几何数据（船位、航向）**必须手工定义或从 YAML 反解析**。根据文档的进度描述，这部分数据目前要么是硬编码的样板数据，要么还没有完全填充。

**实现程度：组件完整（100%），数据层完整性不确定（约 60-70%）**

---

### 2.5 Step B：BuilderRightRail（参数配置侧栏）

这是屏 ① 中最复杂的组件，`BuilderRightRail.tsx`（12.4 KB），分 6 个 Tab。

**设计的 Tab 列表**（Doc 3 描述）：
- Encounter（DCPA/bearing/course 参数）
- Scenarios（场景变体）
- AIS（AIS 回放设置）
- High-fidelity（高保真模式）

**实际实现的 Tab 列表**（代码实际）：
```typescript
const TABS = [
  { id: 'basic',       label: '基本配置',    icon: <LucideSettings2 size={24} /> },
  { id: 'ownship',     label: '本船配置',    icon: <LucideShip size={24} /> },
  { id: 'targets',     label: '目标船配置',  icon: <LucideTarget size={24} /> },
  { id: 'environment', label: '环境配置',    icon: <LucideCloudRain size={24} /> },
  { id: 'sensor',      label: '传感器配置',  icon: <LucideRadio size={24} /> },
  { id: 'raw',         label: '源码 (YAML)', icon: <LucideFileJson size={24} /> },
] as const;
```

**设计与实现的 Tab 差异很大**——设计文档说的是 Encounter/Scenarios/AIS/High-fidelity，但实际实现变成了 Basic/OwnShip/Targets/Environment/Sensor/Raw，更接近"通用 YAML 字段编辑器"而非"场景参数快速配置器"。

逐个 Tab 分析：

#### Tab 1：基本配置（BasicConfigTab）

```typescript
function BasicConfigTab({ doc, onUpdate }) {
  return (
    <div>
      <Field label="场景名称" value={doc?.title || doc?.name || ''} onChange={...} />
      <Field label="描述" value={doc?.description || ''} onChange={...} />
      <Field label="Schema Version" value={metadata?.schema_version || ''} onChange={...} />
      <Field label="Scenario ID" value={metadata?.scenario_id || ''} onChange={...} />
      <Select label="遭遇类型" value={encounter?.type || 'HO'} options={['HO','CR_GW','CR_SO','OT','Custom']} />
      <Select label="COLREGs 规则" value={encounter?.rule || 'Rule14'} options={['Rule14','Rule15',...]} />
      <Select label="底图区域" value={...} options={['Norwegian Sea','Trondheim Fjord',...]} />
    </div>
  );
}
```

**实现程度：UI 完整，但 `onUpdate` 函数的实现方式有问题**：

```typescript
// BuilderRightRail 内部的 onUpdateYaml 处理
const handleTabClick = (id: TabId) => { ... }
// 但 onUpdateYaml prop 的实际处理逻辑在 ScenarioBuilder 父组件中
// 问题：嵌套路径写入（如 'metadata.odd_zone'）需要深度 merge，
// 简单的对象 spread 无法处理
```

字段变更没有实时触发 `SilMapView` 的预览更新，参数改变后地图不动。

#### Tab 2：本船配置（OwnShipConfigTab）

```typescript
function OwnShipConfigTab({ doc, onUpdate }) {
  const pos = doc?.ownShip?.initial?.position || {};
  return (
    <div>
      <Field label="初始纬度" value={pos?.latitude ?? ''} onChange={...} unit="LAT" />
      <Field label="初始经度" value={pos?.longitude ?? ''} onChange={...} unit="LON" />
      <Field label="初始航向" value={initial?.heading ?? ''} onChange={...} unit="°" />
      <Field label="初始航速" value={initial?.sog ?? ''} onChange={...} unit="kn" />
    </div>
  );
}
```

**实现程度：仅覆盖 IMAZU v2.0 schema（lat/lon 格式），不支持 COLREGs v1.0 schema（ENU x_m/y_m 格式）。** 两套 schema 并存（GAP-030），但编辑器只处理其中一种，打开 COLREGs 格式场景时字段全为空。

#### Tab 3：目标船配置（TargetsConfigTab）

```typescript
function TargetsConfigTab({ doc, onUpdate }) {
  const targets = doc?.targetShips || doc?.targets || [];
  return (
    <div>
      {targets.map((tgt, idx) => (
        <div key={idx}>
          <Field label="Target ID" value={tgt.id || ''} onChange={...} />
          <Select label="运动控制模式" value={tgt.model || '固定航路'}
                  options={['fcb_mmg_vessel', 'ais_replay_vessel', 'psbmpc_wrapper']} />
          {/* 初始位置和航速字段 */}
        </div>
      ))}
    </div>
  );
}
```

**实现程度：可以编辑已有目标船，但无法新增或删除目标船**。对于需要从单目标场景变为多目标场景的验证工程师，这是缺失的核心操作。

#### Tab 4/5：环境配置 + 传感器配置

```typescript
{activeTab === 'environment' && 
  <div style={{ color: 'var(--txt-3)', padding: 20 }}>
    Environment Configuration (WIP)
  </div>
}
{activeTab === 'sensor' && 
  <div style={{ color: 'var(--txt-3)', padding: 20 }}>
    Sensor Configuration (WIP)
  </div>
}
```

**实现程度：完全未实现，占位文字。0%**

这两个 Tab 对应的字段（风速/风向/能见度/radar_dropout_pct）在 Doc 3 设计中是验证工程师最常调节的参数（用于测试 M1 ODD 边界条件和传感器退化场景），但当前完全空白。

#### Tab 6：Raw YAML（Monaco 编辑器）

```typescript
{activeTab === 'raw' && (
  <div style={{ flex: 1, height: '400px', margin: '0 -20px' }}>
    <Editor
      height="100%"
      language="yaml"
      theme="vs-dark"
      value={yamlEditor}
      onChange={(value) => onChangeRawYaml(value ?? '')}
      options={{ minimap: { enabled: false }, fontSize: 12, wordWrap: 'on' }}
    />
  </div>
)}
```

Monaco 编辑器本身可用，但：
1. 没有 JSON Schema 关联（`useSchemaValidation` hook 存在但连接到 `/api/v1/schema/fcb_traffic_situation` 这个端点，后端这个端点**并不在** `silApi.ts` 的 18 个 endpoints 中，是 GAP）
2. Raw 编辑与结构化 Tab 之间的同步是单向的——Raw 改了可以更新结构，但结构 Tab 改了后切到 Raw，Monaco 反映的是旧值还是新值取决于 `yamlEditor` state 的更新时机

**实现程度：Monaco 编辑器本体 80%，Schema 校验联动 20%**

---

### 2.6 底部操作按钮区域

```typescript
{/* Actions Footer */}
<div>
  <div style={{ display: 'flex', gap: 8, marginBottom: 8 }}>
    <button onClick={onSave} style={btnStyle('line')}>
      <LucideSave size={14} /> SAVE
    </button>
    <button onClick={onValidate} style={btnStyle('line')}>
      <LucideCheckCircle size={14} /> VALIDATE
    </button>
  </div>
  <button onClick={onRun} style={btnStyle('phos')}>
    RUN SCENARIO <LucidePlay size={14} />
  </button>
</div>
```

**SAVE** 调用 `onSave` → 父组件的 `useCreateScenarioMutation` → `POST /api/v1/scenarios` → 后端写文件。**链路完整**。

**VALIDATE** 调用 `onValidate` → 目前 `scenario_routes.py` 的 validate 端点只检查空字符串（GAP-017），不做真正的 schema 校验。**链路存在但实现是 stub**。

**RUN SCENARIO** 调用 `onRun` → 触发屏 ① → 屏 ② 跳转。**链路完整**。

---

### 2.7 Step C：SilMapView 预览

**设计**：显示 own-ship + targets + CPA rings + ImazuGeometry，展示当前场景 YAML 的几何信息

**实现**：`SilMapView` 接受 `previewData` prop，支持预览模式：

```typescript
// SilMapView.tsx 中的预览数据处理
const ownShip = previewData?.ownShip ? {
  pose: { 
    lat: previewData.ownShip.lat, 
    lon: previewData.ownShip.lon, 
    heading: previewData.ownShip.heading / RAD 
  },
  kinematics: { 
    sog: previewData.ownShip.sog || 0, 
    cog: (previewData.ownShip.cog || previewData.ownShip.heading) / RAD 
  }
} : ownShipFromStore;
```

**核心问题**：`previewData` 需要从 YAML 解析后构造，而这个解析和构造逻辑在 `ScenarioBuilder` 父组件中。根据 `BuilderRightRail` 的 props 定义：

```typescript
export interface BuilderRightRailProps {
  yamlEditor: string;
  onUpdateYaml: (updates: any) => void;
  onChangeRawYaml: (val: string) => void;
  previewData: any;    // ← 这个 prop 存在
  onRun: () => void;
  onSave: () => void;
  onValidate: () => void;
}
```

`previewData` 作为 prop 传入，说明父组件 `ScenarioBuilder` 负责解析 YAML → 构造预览数据 → 传给地图。但 **BuilderRightRail 内部实际上没有用到 `previewData` 这个 prop**（代码里没有任何对它的引用），它只存在于接口定义中。

**实现程度：SilMapView 组件支持 previewData（80%），但从 YAML → previewData 的解析转换链路未完成（约 40%）**

---

### 2.8 SummaryRail

```typescript
// SummaryRail.tsx — 内部数据是硬编码
export const SummaryRail: React.FC<SummaryRailProps> = ({ summary, ... }) => {
  return (
    <div>
      {/* SHA 是硬编码 */}
      <div>SHA: 7f3a · v1.1.2</div>
      
      {/* 验证状态是硬编码 */}
      {[
        ['几何参数完整', true],
        ['ODD 包络一致', true],
        ['评估指标已配置', true],
        ['故障剧本已审核', false],  // ← 永远是这个状态
      ].map((row, i) => (...))}
    </div>
  );
};
```

SHA 显示是硬编码字符串 `7f3a`，验证状态 4 项中 3 项永远是绿色。即使用户加载了不同的场景，这里显示的信息不会变化。

**实现程度：UI 骨架 90%，数据真实性 10%**

---

## 3. 完整汇总表

| 功能模块 | 设计 | 实现程度 | 主要问题 |
|---|---|---|---|
| 路由 + 文件名 | `#/scenario` + SimulationScenario.tsx | 70% | 新旧文件名混用，测试仍指向旧名 |
| Stepper A→B→C | 三步带状态 | 100% | 完整 |
| ODD Domain 选择 | 4 种域 + Gate 4 联动 | 30% | UI 存在，数据不写入 YAML，不联动 Preflight |
| ImazuGrid 22 场景 | SVG 缩略图 + 点击预填 | 60-70% | 组件完整，几何数据填充程度不确定 |
| Tab：基本配置 | 场景元信息编辑 | 70% | 嵌套路径更新有 bug，不触发地图刷新 |
| Tab：本船配置 | lat/lon + 航向 + 航速 | 50% | 只支持 IMAZU v2.0 schema，COLREGs v1.0 字段为空 |
| Tab：目标船配置 | 多目标船编辑 | 40% | 可编辑现有目标，无法新增/删除 |
| Tab：环境配置 | 风/流/能见度/Beaufort | 0% | WIP 占位 |
| Tab：传感器配置 | radar_dropout / AIS 退化 | 0% | WIP 占位 |
| Tab：Raw YAML | Monaco 编辑器 | 80% | Schema 校验端点缺失，双向同步时序问题 |
| Schema 实时校验 | AJV + maritime-schema | 20% | hook 存在，后端 schema 端点不存在 |
| SilMapView 预览 | YAML → 地图几何实时同步 | 40% | 组件支持 previewData，但解析链路未接通 |
| CPA rings 预览 | 0.5nm + 1.0nm 圆 | 80% | 组件完整，但圆的半径是固定值而非基于场景 DCPA 目标 |
| ImazuGeometry overlay | 场景特定几何线 | 60% | 组件存在，数据来源依赖上层 |
| SummaryRail | SHA + 验证状态 | 10% | SHA 硬编码，4 项验证永远固定 |
| SAVE 操作 | 写入后端 | 100% | 完整 |
| VALIDATE 操作 | maritime-schema 校验 | 10% | 后端 stub，仅查空字符串 |
| RUN 操作 | 跳转 Preflight | 100% | 完整 |

**整体实现程度：约 50-55%**

核心问题是"组件骨架完整，但数据联动链路普遍断开"——每个 UI 元素看起来都在，但用户在上面做的任何操作，几乎都不会产生预期的联动效果（改参数地图不动、SHA 不更新、ODD 选择不传递、两套 schema 只兼容一种）。对于需要快速配置和验证场景的工程师，屏 ① 当前更接近一个高保真原型，而不是可操作的工具。