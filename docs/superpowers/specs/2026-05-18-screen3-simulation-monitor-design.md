# Screen ③ Simulation-Monitor · v1.0 设计规格

| 属性 | 值 |
|---|---|
| 文档路径 | `docs/superpowers/specs/2026-05-18-screen3-simulation-monitor-design.md` |
| 版本 | v1.0 |
| 日期 | 2026-05-18 |
| 状态 | 设计基线（brainstorm 会话完成，等待 writing-plans）|
| 上游 | Doc 3 `03-sil-frontend-design.md` §8（同步更新）|
| 覆盖范围 | `web/src/screens/SimulationMonitor.tsx` 及依赖组件 · foxglove_bridge WebSocket |
| DEMO 目标 | DEMO-1 (6/15 Skeleton)→ DEMO-2 (7/31 算法面板)→ DEMO-3 (8/31 SAT-3 完整) |
| 依赖 | Screen ② Preflight 6-gate 通过后跳转；Screen ④ 停止后跳转 |

---

## 0. 一句话定位

Screen 3 是 SIL 测试的**主舞台**：仿真运行期间同时承担「海事合规级运行监控（船长/审查员视角）」与「TDL 算法全链路白盒调试（测试工程师视角）」，两套视角共享同一份 50 Hz 遥测数据流与同一 FSM 状态机，通过**视图模式切换**而非两个页面分工。

---

## 1. 业务边界与 TDL 覆盖矩阵

| 业务层 | 承载内容 | 对应 TDL 模块 | DEMO 阶段 |
|---|---|---|---|
| 态势感知 | 本船 + N 目标船实时位置/航向/速度 | M2 World Model | DEMO-1 |
| ODD 监控 | 当前 ODD 域（A/B/C/D）+ 模式状态药丸 | M1 ODD Manager | DEMO-1 |
| 避碰行为仲裁 | IvP 行为权重 + 胜出行为 + 风险梯度向量 | M4 Behavior Arbiter | DEMO-2 |
| 战术规划 | Mid-MPC 90s 弧线 + BC-MPC 13 分支轨迹 | M5 Tactical Planner | DEMO-2 |
| COLREGs 推理 | Rule 13/14/15 识别 + 5 层决策溯源树 | M6 COLREGs Reasoner | DEMO-1 skeleton / DEMO-2 完整 |
| 安全监督 | 6 项 SOTIF 假设违反监控 + MRM 触发 + Checker 否决率 | M7 Safety Supervisor | DEMO-2 |
| 透明性输出 | SAT-1/2/3 三级透明性（Chen 2014 [W54]）| M8 HMI Bridge | SAT-1 DEMO-1 / SAT-3 DEMO-3 |
| 故障注入 | AIS dropout / Radar spike / Dist step | fault_injection_node | DEMO-1 |
| 接管处理 | ToR Modal + TMR 60s 三段升级 + MRC 兜底 | M8 → M1 → M5 | DEMO-1 skeleton / DEMO-3 完整 |
| 评分预览 | 6 维实时打分（运行中实时更新）| scoring_node | DEMO-2 |
| 证据链 | ASDR Ledger 流式审计日志 | ASDR / M8 | DEMO-1 |

---

## 2. 三视图模式定义

```typescript
// useUIStore
viewMode: 'captain' | 'engineer' | 'roc'
```

| 维度 | Captain（船长） | Engineer（工程师）| ROC（Phase 2）|
|---|---|---|---|
| 地图朝向 | **Heading-Up**，本船锁定屏幕底部 30% | **North-Up**，可自由缩放拖移 | Heading-Up + 大字号 |
| 信息密度 | **SAT-1 only** 最小化 HUD | **SAT-1/2/3 全展**，4 抽屉可独立开关 | SAT-1 + ToR 优先级最高 |
| 算法可见性 | ❌ 无 MPC 轨迹/IvP 权重 | ✅ 全算法决策链可视 | ❌ 无 |
| 告警样式 | IEC 62288 合规色 + BAM 单条 [W58][W52] | 额外诊断面板 | 超大 ToR 倒计时居中 |
| 快捷键 `G` | → Engineer | → Captain | → Captain |
| 快捷键 `V` | → ROC | → ROC | → Captain |
| 切换审计 | ASDR 记录模式切换时间戳 | 同左 | 同左 |

---

## 3. 全屏布局设计

### 3.1 Captain 视图（IEC 62288:2021 S-Mode 合规）

```
┌─── Module Pulse Strip 16px ──────────────────────────────────────────────────────────┐
│  M1▉ M2▉ M3▉ M4▉ M5▉ M6▉ M7▉ M8▉   GREEN/AMBER/RED  hover→延迟μs + 错误码         │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ ┌─ ODD Badge ──┐  ┌──────── ThreatRibbon (CPA-sorted chips, top) ───────────────┐   │
│ │ ODD-A 🟢     │  │ [TGT-12 CPA 0.42nm 4min 🔴] [TGT-08 CPA 0.91nm 7min 🟡]   │   │
│ └──────────────┘  └─────────────────────────────────────────────────────────────┘   │
│                                                                                       │
│  ╔═══════════════ MapLibre GL JS · S-57 MVT ENC · Heading-Up ════════════════════╗   │
│  ║                                                                                ║   │
│  ║  [目标船：IEC 62288 三角形，CPA 风险着色]                                     ║   │
│  ║      ↗ COG vector 6min 虚线                                                   ║   │
│  ║  [CPA ring: 虚线圆，标注最近会遇点]                                           ║   │
│  ║  [PPI rings: 1/2/5nm 白色虚线同心圆，NESW 标注]                               ║   │
│  ║  [ENC 图层: 等深线 / 陆地 / 航标 / 限制区域]                                  ║   │
│  ║                                                                                ║   │
│  ║                          ▲                                                     ║   │
│  ║                   [本船: 红色 ▲ 三角形，IEC 62288 S-Mode]                     ║   │
│  ║                   [COG line: 6min 红色虚线 + 箭头]                            ║   │
│  ║                   [True-heading indicator: 红色实线]                          ║   │
│  ║                                                                                ║   │
│  ╚════════════════════════════════════════════════════════════════════════════════╝   │
│                                                                                       │
│  ┌─ ConningBar (bottom, 48px) ────────────────────────────────────────────────────┐  │
│  │  HDG 045° │ SOG 12.0kn │ COG 047° │ ROT +1.2°/s │ RUD 5° │ THR 72% │ DPT 18m │  │
│  │  ▂▃▅▇ sparkline (60s 历史缓冲) per field                                      │  │
│  └────────────────────────────────────────────────────────────────────────────────┘  │
│  ┌─ FooterHotkeyHints ──────────────────────────────────────────────────────────────┐│
│  │  P=暂停  R=继续  1/2/4=仿真倍速  F=故障注入  G=工程视图  S=停止  Esc=返回        ││
│  └──────────────────────────────────────────────────────────────────────────────────┘│
│                                                        右下角：[ASDR 日志 +] 24px 条 │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

**IEC 62288:2021 S-Mode 合规要素**（🟢 High [W58]）：

| 元素 | 规格 |
|---|---|
| 本船符号 | 红色实心三角，船头指示线（true-heading） |
| 目标船符号 | 三角形，按 CPA 风险着色：绿 >2nm / 琥珀 1-2nm / 红 <1nm |
| COG 向量 | 6 分钟预测线，虚线+箭头 |
| PPI 距离环 | 0.5/1/2/4/8nm 可切换，四基点标注，白色虚线 |
| 日/昏/夜配色 | 深蓝底+白字（日）；黄昏 ±15° 太阳仰角自动切换 |
| BAM 告警 | 每类异常**仅一条**，路由到对应工作站（COLAV 告警→COLAV 站） [W52] |

---

### 3.2 Engineer 视图（全白盒调试台）

```
┌─── Module Pulse Strip 16px + Latency μs ──────────────────────────────────────────────┐
│                                                                                         │
│ ┌─LEFT DRAWER (300px, 可折叠) ─────────────┐   ╔══ MapLibre North-Up (可拖拽缩放) ══╗  │
│ │ ① ARPA Target Table                       │   ║                                     ║  │
│ │  TGT-12  MMSI:123456789                   │   ║  [M5 BC-MPC: 13 候选轨迹弧线]      ║  │
│ │   ├─ CPA: 0.42nm 🔴  TCPA: 4min          │   ║  ━━ green=最优  ┄┄ red=拒绝         ║  │
│ │   ├─ BCR: 0.89nm  BCT: 6.1min            │   ║                                     ║  │
│ │   ├─ COG: 245°    SOG: 8.2kn             │   ║  [M5 Mid-MPC: 90s 预测弧，绿实线]  ║  │
│ │   └─ Rule: 14 (Head-on)  Conf: 0.94      │   ║                                     ║  │
│ │  TGT-08  MMSI:987654321                   │   ║  [M4 风险梯度向量]                  ║  │
│ │   ├─ CPA: 0.91nm 🟡  TCPA: 7min          │   ║  本船 sprite 周围 8 方向箭头        ║  │
│ │   └─ Rule: 15 (Crossing)  Conf: 0.87     │   ║  长度∝代价，颜色绿→琥珀→红         ║  │
│ │ ─────────────────────────────────────     │   ║                                     ║  │
│ │ ② M6 Decision Rationale Tree             │   ║  [COLREGs 几何叠层]                 ║  │
│ │  TGT-12 (ACTIVE) 耗时: 2.3ms             │   ║  绿扇区/红扇区/白艉（导航灯色）     ║  │
│ │  [1.ODD ] ODD-A → Rule 5,6,7,8,13-17    │   ║                                     ║  │
│ │  [2.会遇] Rule 14 ✓ hdg_diff=178°        │   ║  [三级安全椭圆域]                   ║  │
│ │  [3.责任] GIVE-WAY (Rule 16)             │   ║  ··Observation Zone 2nm             ║  │
│ │  [4.方向] STARBOARD ≥30° (Rule 8)        │   ║  ░░Action Zone 1nm                 ║  │
│ │  [5.时机] TCPA≤T_act=4min ⚠️ STAGE_3    │   ║  ██Critical Zone 0.3nm             ║  │
│ │  → {GIVE_WAY, STBD, 30°, STAGE_3}        │   ║                                     ║  │
│ └──────────────────────────────────────────┘   ╚═════════════════════════════════════╝  │
│                                                                                         │
│                                                  ┌─RIGHT DRAWER (280px, 可折叠) ──────┐ │
│                                                  │ ③ M7 SOTIF 假设违反监控带          │ │
│                                                  │  AIS/雷达一致性   ████░  1.8σ 🟢   │ │
│                                                  │  目标可预测性 RMS ████░  41m  🟢   │ │
│                                                  │  感知覆盖充分性   █████  95%  🟢   │ │
│                                                  │  COLREGs解析失败  ████░  0次  🟢   │ │
│                                                  │  通信链路质量     █████  120ms 🟢  │ │
│                                                  │  Checker 否决率   ██░░░  8%   🟢   │ │
│                                                  │ ─────────────────────────────────  │ │
│                                                  │ ④ ASDR Ledger（流式，可折叠）      │ │
│                                                  │  10:42:18 [M6] Rule 14 activated   │ │
│                                                  │  10:42:18 [M4] IvP→COLREGs 0.70   │ │
│                                                  │  10:42:19 [M5] MPC→STBD 30°        │ │
│                                                  │  10:42:19 [M7] CPA trend nominal   │ │
│                                                  │ ─────────────────────────────────  │ │
│                                                  │ ⑤ ScoringHUD（6 维实时）           │ │
│                                                  │  Safety: 0.92  Rule: 0.88          │ │
│                                                  │  Smooth: 0.95  Effic: 0.82         │ │
│                                                  └───────────────────────────────────┘ │
│                                                                                         │
│ ┌─ Decision Chain Timing Strip (24px, bottom) ────────────────────────────────────────┐│
│ │ M1[0.8ms]→M2[3.2ms]→M4[1.1ms]→M6[2.3ms]→M5[18.7ms]→M7[4.1ms]  total: 30.2ms       ││
│ └─────────────────────────────────────────────────────────────────────────────────────┘│
│ ┌─ ConningBar (bottom, 展开含 sparklines) ───────────────────────────────────────────┐ │
└──┴────────────────────────────────────────────────────────────────────────────────────┴─┘
```

---

## 4. 核心算法决策可视化规格

### 4.1 M4 IvP 风险梯度向量（地图叠层）

**原理**（🟢 High — NLM colav_algorithms 笔记本有充分先例）：

- 本船 sprite 周围辐射 8 方向箭头（0°/45°/90°/135°/180°/225°/270°/315°）
- 箭头长度 ∝ 该方向综合目标函数代价（越长=越不利）
- 箭头颜色：绿（低代价/可行）→ 琥珀（中等）→ 红（高代价/禁止）
- 当前胜出行为名称以金色文字标注于本船上方（如 `COLREGs_Avoidance 0.70`）
- 更新频率：4 Hz（M4 IvP 求解频率）

```
行为字典权重（架构报告 §8.3）：
  Transit              0.3
  COLREGs_Avoidance    0.7  ← 当前激活（金色标注）
  Restricted_Visib.    0.6
  DP_Hold              0.8
  MRC_Drift            1.0
```

**数据源**：`M4 → M8 → SAT2Data.ivp_contribution[]` @ 4 Hz

---

### 4.2 M5 MPC 候选轨迹（地图叠层）

**Mid-MPC 90s 弧线**（主接口，1-2 Hz）：
- 1 条选定轨迹：绿色实线，N=18 预测点（步长 5s）
- 显示范围：当前位置 → 90s 后预测终点
- 标注：终点处显示预测 (COG, SOG)

**BC-MPC 13 候选分支**（🟢 High — ±90° 步进 15°，4-10 Hz）：
- 13 条候选弧线，颜色编码 Red→Green（代价高→低）
- 最优分支（绿）与 Mid-MPC 弧线叠加时加粗 3px
- 拒绝分支：2s 后渐隐消失（opacity 1.0 → 0.0）
- 显示范围：当前 → 30s 后预测终点

**Phase 3 新增（D3.4）**：
- `+5min / +10min` ghost trails（浅色轨迹线）
- Uncertainty ellipses（置信椭圆）围绕各预测点

**数据源**：`M5 → M8 → SAT3Data.trajectory_candidates[]`

---

### 4.3 M6 COLREGs 5 层决策溯源树（左抽屉 ② 区域）

```
Panel 布局（每层一行，展开显示输入→输出）：

[层1 · ODD]   ODD-A {active}
              → 适用规则集: Rule 5,6,7,8,13,14,15,16,17,18

[层2 · 会遇]  Rule 14 ✓  (Head-on)
              bearing: 3.2°  hdg_diff: 178°  range: 2.1nm

[层3 · 责任]  GIVE-WAY  [Rule 16]

[层4 · 方向]  STARBOARD ≥30°  [Rule 8]  "大幅早行动"
              首选大幅右转；减速次之；停车兜底

[层5 · 时机]  STAGE_3 "独立避让"
              TCPA=4.0min ≤ T_act=4.0min  ⚠️ 边界触发

输出汇总：
  {role: GIVE_WAY, direction: STBD, min_action: 30°,
   timing_stage: STAGE_3, escalation_flag: true}
  Confidence: 0.94 │ 推理耗时: 2.3ms │ 更新: 1 Hz
```

**地图联动**：
- 层2 激活时，地图高亮 Rule 14 几何扇区（导航灯配色：绿右舷/红左舷/白艉）[R18][W58]
- 层5 STAGE_3 激活时，ThreatRibbon 对应目标 chip 变红 + 添加"独立避让"标签
- 决策树面板折叠状态下，仅用 `ColregsDecisionTree.tsx`（已实现的简版组件）保持最小可见

**数据源**：`M6 → M8 → SAT2Data.colregs_chain[]` @ 1 Hz

---

### 4.4 M7 SOTIF 假设违反监控带（右抽屉 ③ 区域）

```
6 行水平进度条监控：

指标                    阈值               当前值    状态
AIS/雷达一致性残差      >2σ 持续10s        1.8σ      🟢
目标可预测性 RMS        >50m/30s           41m       🟢
感知覆盖充分性          <80% of 360°       95%       🟢
COLREGs 解析失败数      连续3次            0次       🟢
通信链路（RTT/丢包）    RTT>2s 或丢包>20%  120ms/0%  🟢
Checker 否决率          >20%/15s 滑窗      8%        🟢

颜色逻辑：绿（0-79%）→ 黄（80-99%）→ 红（100%=违反，行闪烁）
```

**MRM 推荐显示**（违反时在监控带下方弹出）：
```
[⚠️ SOTIF 告警] AIS 一致性残差 >2σ 持续 28s
推荐：MRM-01（减速至安全速度，保持航向）
等待 M1 仲裁...
```

**数据源**：`M7 → M8 → Safety_AlertMsg + CheckerVetoNotification`（架构报告 §11.3/§11.7）

---

### 4.5 决策链时延监控条（底部固定 24px）

```
M1[Nms] → M2[Nms] → M4[Nms] → M6[Nms] → M5[Nms] → M7[Nms]  total: Nms

着色规则（每段独立）：
  绿  < 5ms
  琥珀  5-20ms
  红  > 20ms（M5 容忍到 50ms，因 MPC 求解周期 500ms-1s）

总回路 > 100ms → 全条变红 + Module Pulse M5 格变红
```

**数据源**：M1-M8 消息 `stamp` 字段（架构报告 §4.4 + §15 接口契约）

---

## 5. 三级动态安全域

三视图共享数据，渲染精度按模式不同。

| Tier | 名称 | 距离 | 形状 | Captain 显示 | Engineer 显示 |
|---|---|---|---|---|---|
| 1 | Observation Zone | 2.0nm | 椭圆虚线 | ❌（不渲染） | ✅ 灰色虚线椭圆 |
| 2 | Action Zone | 1.0nm | 椭圆半透明填充 | 🔵 CPA 圆形（简化） | ✅ 琥珀色半透明椭圆 |
| 3 | Critical Zone | 0.3nm | 椭圆红色高亮边界 | ✅ 红色实心圆 | ✅ 红色高亮椭圆 |

**RM Vector（相对运动矢量）**：工程视图叠于地图，与 Action Zone 的交点 = 可视 CPA 点。

**参数来源**：M1 Capability Manifest（ODD-A: 2.0/1.0/0.3nm；ODD-B: 0.5/0.3/0.1nm）

> **[TBD-HAZID]**：实际距离阈值须 HAZID RUN-001 校准（8/19）后回填至 Capability Manifest。

---

## 6. FSM 5 态 UI 表现

| FSM 状态 | 状态药丸 | 屏幕边框 | ThreatRibbon | 关键 UI 动作 |
|---|---|---|---|---|
| **TRANSIT** | 🟢 ACTIVE | 正常深蓝 | 无红色 chip | — |
| **COLREG_AVOIDANCE** | 🟡 AVOIDING（脉动） | 正常 | 红色 chip 置顶 | 工程视图：M6 全 5 层激活，M5 弧线出现 |
| **TOR** | 🔴 TOR（高频闪烁） | 琥珀色脉动光晕 | 红色 chip 高亮 | ToR Modal 强制居中弹出（见§7） |
| **OVERRIDE** | ⬜ MANUAL | 正常 | 红色 chip | 键盘 ←/→ 打舵；M7 降级监测线程保持 |
| **MRC** | 🔴 MRC（持续） | **血红色全帧** | 红色 chip | 执行 MRM-{01-04}；ASDR 记录时间戳 |

---

## 7. ToR Modal · 合规接管交互

**触发链**：`M7 Safety_AlertMsg → M1 仲裁 → useFsmStore.setFsmState(TOR) → ToR Modal 弹出`

```
╔════════════════════════════════════════════════════════════════════╗
║  ⚠  TRANSFER OF RESPONSIBILITY REQUEST                             ║
║                                                                    ║
║  原因：[Safety_AlertMsg.alert_description]                        ║
║  MRM 建议：MRM-01（减速至安全速度，保持航向）                     ║
║                                                                    ║
║  ┌──────────────────────────────────────────────────────────────┐ ║
║  │  ██████████████████████████░░░░░░░░░░░░   32s / 60s         │ ║
║  │  [0-20s] 静默倒计                                            │ ║
║  │  [20-45s] 音频告警（BAM IMO MSC.302 合规）                   │ ║
║  │  [45-60s] 红色强化 + 触觉（硬件支持时）                     │ ║
║  └──────────────────────────────────────────────────────────────┘ ║
║                                                                    ║
║  [  TAKE OVER  ]   ← 持续按压 ≥2 秒（物理锁）                   ║
║  按压进度条：░░░░░░░░░░░░░░░░░░░  (实时显示)                     ║
║                                                                    ║
║  ⚠ 倒计时归零将自动执行 MRC → MRM-01                            ║
╚════════════════════════════════════════════════════════════════════╝
```

**物理锁规格**（🟢 IMO MASS Code Part 2-A §6.3.2）：
- `onPointerDown` → 启动计时器
- `onPointerUp` 或 `onPointerLeave` < 2s → 重置，Toast 提示"请持续按压 ≥2 秒"
- `onPointerUp` ≥ 2s → `useOverrideMutation()` → FSM: TOR → OVERRIDE
- Modal 背景**不可点击关闭**（`pointer-events: none` 覆盖层）

**三段升级时序**（🟡 Medium [W57]）：

| 阶段 | 时间区间 | 告警方式 |
|---|---|---|
| Phase 0 静默 | 0 – 20s | 倒计时数字更新，边框琥珀色脉动 |
| Phase 1 音频 | 20 – 45s | 持续蜂鸣（BAM 合规单音），边框加快脉动 |
| Phase 2 强化 | 45 – 60s | 边框血红 + 触觉（硬件）+ 音频保持 |

> **[TBD-HAZID]**：Phase 0→1 精确触发点（20s 待 Veitch 2024 完整 PDF 确认）；推荐 `--depth deep` 调研锁定。DEMO-3 ToR 验收前必须闭口。

---

## 8. SAT-1/2/3 三级透明性落地映射

基于 Chen et al. 2014 [W54] 框架：

| SAT 级 | 语义 | UI 位置 | 内容 | 模式 | 数据源 |
|---|---|---|---|---|---|
| **SAT-1** 当前状态 | 系统正在做什么 | ConningBar + ODD Badge + ThreatRibbon | HDG/SOG/COG/ROT/RUD/THR/DPT + ODD 域 + CPA 排名 chip | 两种模式 | `l3_msgs/SAT1Data` |
| **SAT-2** 推理 | 为什么这样做 | ASDR Ledger + M6 Decision Rationale Tree + M4 IvP 风险梯度向量 | COLREGs 5 层溯源 + 行为权重 + 置信度 (0-1) + 规则条款引用 | 工程视图 | `l3_msgs/SAT2Data` |
| **SAT-3** 预测/不确定性 | 接下来会发生什么 | M5 MPC 弧线 + ghost trails @+5/+10min + 置信椭圆 | 90s 预测轨迹 + 13 分支 + uncertainty bands | 工程视图（DEMO-3 D3.4）| `l3_msgs/SAT3Data` |

---

## 9. 操作流程 · 仿真监控核心步骤

### 9.1 入场

```
Screen 2 (Preflight) 6-gate 全 PASS
  → navigate('/monitor/:runId')
  → useScenarioStore.scenarioId / runId 已就位
  → WebSocket 连接 foxglove_bridge (GAP-026 选项 A 标准协议)
  → useTelemetryStore.subscribeToTopics()
  → 默认视图：Captain 模式
  → FSM 检测：RUNNING 态才显示内容，否则 redirect → /builder
```

### 9.2 正常巡航（TRANSIT）

```
50 Hz 遥测 → MapLibre 更新本船位置/姿态
ConningBar 7 字段实时刷新
Module Pulse M1-M8 全 GREEN
ThreatRibbon 无告警（或全绿 chip）
ASDR Ledger 显示正常巡航日志
```

### 9.3 COLREGs 遭遇

```
目标进入 Observation Zone（工程视图灰色椭圆）
  → ARPA 目标 chip 变黄（CPA 1-2nm）
  → RM Vector 出现于地图
  → 工程视图：M6 Decision Tree 层1-2 激活

RM Vector 刺穿 Action Zone
  → FSM: TRANSIT → COLREG_AVOIDANCE
  → ThreatRibbon chip 变红、置顶
  → 状态药丸脉动（🟡 AVOIDING）
  → 工程视图：M6 全 5 层激活
  → 工程视图：M5 BC-MPC 13 分支出现
  → 工程视图：M4 IvP 胜出行为 = COLREGs_Avoidance（金色）
  → 工程视图：M7 SOTIF 监控带开始高频刷新
```

### 9.4 人工故障注入（测试场景）

```
快捷键 F → FaultInjectPanel 弹出
  选择：ais_dropout / radar_spike / dist_step
  → fault_injection_node 触发故障
  → 观察：M7 SOTIF 监控带"AIS/雷达一致性"行变化
  → 观察：M6 COLREGs 解析失败计数上升
  → 观察：Module Pulse M2/M6 格变黄/红
  → ASDR Ledger 记录故障注入事件
```

### 9.5 ToR 触发

```
触发路径：M7 VETO（假设违反 >阈值）或 X-axis Checker 否决率 >20%
  → M7 Safety_AlertMsg → M1 仲裁 → FSM: COLREG_AVOIDANCE → TOR
  → 屏幕边缘琥珀色光晕
  → ToR Modal 弹出，开始 60s 倒计时（§7 规格）
  → Captain 视图：Modal 覆盖全屏，倒计时最大化
  → 工程视图：Modal 居中弹出，抽屉不关闭（保留诊断面板）
```

### 9.6 船长接管（OVERRIDE）

```
在 60s 内，持续按压"TAKE OVER" ≥2s
  → FSM: TOR → OVERRIDE
  → 状态药丸：⬜ MANUAL
  → Modal 关闭
  → 键盘 ←/→ 手工打舵（±5° per key）
  → ASDR 记录：接管时间戳 + 接管延迟 ms（从 M7 VETO 触发起算）
  → M7 降级监测线程保持（通信/传感器告警继续显示，M8 §11.9.1）
  → M5 冻结：AvoidancePlan.status = OVERRIDDEN
```

### 9.7 MRC 兜底（60s 未响应）

```
ToR 倒计时 → 0
  → FSM: TOR → MRC
  → 屏幕边框血红色全帧
  → M1 执行 MRM-{01-04}（基于 M7 Safety_AlertMsg.recommended_mrm）
  → 回切协议（架构报告 §11.9.2）：
      T0+0ms:   M1 进入"回切准备"
      T0+10ms:  M7 主仲裁线程重启 + SOTIF 重置
      T0+100ms: M7 发 M7_READY → M1
      T0+110ms: M1 向 M5 发 M5_RESUME
      T0+120ms: M5 输出首个 AvoidancePlan (status=NORMAL)
  → ASDR 记录：MRC 时间戳 + MRM 编号 + 回切时序
```

### 9.8 停止/完成

```
快捷键 S 或 Stop 按钮
  → useDeactivateLifecycleMutation()
  → FSM: RUNNING → REPORT
  → navigate('/evaluator/:runId')，携带 scenarioId + runId + 最终 FSM 状态
```

---

## 10. 状态管理（Zustand Stores）

```typescript
// useTelemetryStore  （50 Hz 订阅，useShallow() 精细选择器 [W59]）
interface TelemetryStore {
  ownShip: OwnShipState;          // HDG/SOG/COG/ROT/RUD/THR/DPT
  targets: TrackedTarget[];       // MMSI/pos/COG/SOG/CPA/TCPA/BCR/BCT
  environment: EnvironmentState;  // 风/流/能见度/海况
  modulePulse: ModulePulse[];     // M1-M8 GREEN/AMBER/RED + latency_us
  asdrEvents: ASDREvent[];        // 流式事件日志
  sat1: SAT1Data;                 // 当前状态透明性
  sat2: SAT2Data;                 // 推理透明性（工程视图）
  sat3: SAT3Data;                 // 预测透明性（Phase 3）
  simTime: Time;
  wallTime: Time;
  simRate: number;
}

// useFsmStore
interface FsmStore {
  fsmState: 'TRANSIT'|'COLREG_AVOIDANCE'|'TOR'|'OVERRIDE'|'MRC';
  torCountdown: number;           // seconds remaining
  mrmRecommended: 'MRM-01'|'MRM-02'|'MRM-03'|'MRM-04' | null;
  setFsmState: (s: FsmState) => void;
}

// useUIStore
interface UIStore {
  viewMode: 'captain' | 'engineer' | 'roc';
  leftDrawerOpen: boolean;
  rightDrawerOpen: boolean;
  mapLayers: MapLayer[];
  hudVisible: boolean;
  asdrLogExpanded: boolean;
  setViewMode: (m: ViewMode) => void;
  toggleLeftDrawer: () => void;
  toggleRightDrawer: () => void;
}

// useControlStore
interface ControlStore {
  simRate: number;       // 0.5 | 1 | 2 | 4 | 10 | 20 | 50
  isPaused: boolean;
  faultsActive: Fault[];
  pause: () => Promise<void>;
  resume: () => Promise<void>;
  stop: () => Promise<void>;
  injectFault: (type: FaultType) => Promise<void>;
}
```

**性能约束**：50 Hz 遥测使用 `useShallow()` 精细选择器 [W59]，配合 100ms debounce 防帧丢失。M5 MPC 轨迹（可能 13 条弧线 × 30 点）使用 `requestAnimationFrame` 平滑渲染。

---

## 11. API / WebSocket 接口契约

| 接口 | 协议 | 路径 | 内容 | 频率 |
|---|---|---|---|---|
| 50Hz 遥测 | foxglove WS（标准协议 [W49][W50]）| `ws://host:8765` | OwnShip / Targets / Modules / ASDR / SAT | 50 Hz |
| 仿真控制 | REST | `POST /api/v1/sim_clock/set_rate` | `{rate: number}` | 按需 |
| 暂停/继续 | REST | `POST /api/v1/lifecycle/{pause,resume}` | — | 按需 |
| 停止 | REST | `POST /api/v1/lifecycle/deactivate` | — | 按需 |
| 故障注入 | REST | `POST /api/v1/fault/trigger` | `{type, duration_s}` | 按需 |
| 运行状态 | REST | `GET /api/v1/lifecycle/status` | FSM state | 轮询 1 Hz |

---

## 12. 组件清单（新增与复用）

### 12.1 已实现（复用）

| 组件 | 复用方式 |
|---|---|
| `TopChrome.tsx` | 照常复用，增加 viewMode 切换按钮 |
| `ConningBar.tsx` | 照常复用，Engineer 视图展开 sparklines |
| `ThreatRibbon.tsx` | 照常复用 |
| `AsdrLedger.tsx` | 照常复用，Engineer 视图默认展开 |
| `ArpaTargetTable.tsx` | 照常复用，移入 LEFT DRAWER |
| `ColregsDecisionTree.tsx` | 照常复用（简版），Engineer 视图升级为 5 层完整版 |
| `ModuleReadinessGrid.tsx` | Module Pulse strip 复用（压缩为 16px）|
| `FaultInjectPanel.tsx` | 照常复用 |
| `TorModal.tsx` | 照常复用，物理锁逻辑更新（≥2s 持续按压）|
| `ScoringGauges.tsx` | 移入 RIGHT DRAWER ⑤ 区域 |
| `FooterHotkeyHints.tsx` | 照常复用 |

### 12.2 新增组件（Engineer 视图专属）

| 组件 | 职责 | 数据源 |
|---|---|---|
| `IvpRiskGradientLayer.tsx` | MapLibre 层：8 方向风险梯度向量 | `SAT2Data.ivp_contribution[]` |
| `MpcTrajectoryLayer.tsx` | MapLibre 层：Mid-MPC 弧线 + BC-MPC 13 分支（Red→Green）| `SAT3Data.trajectory_candidates[]` |
| `ColregsRationaleTree.tsx` | 5 层 COLREGs 决策溯源树（升级版）| `SAT2Data.colregs_chain[]` |
| `SotifMonitorStrip.tsx` | 6 行 SOTIF 假设违反进度条 + MRM 推荐 | `Safety_AlertMsg` |
| `DecisionChainTimingBar.tsx` | 底部 24px M1→M8 时延条 | 消息 stamp 字段 |
| `ColregsGeometryLayer.tsx` | MapLibre 层：Rule 13/14/15 导航灯色扇区 | `SAT2Data.colregs_chain[2].meeting_type` |
| `SafetyDomainLayer.tsx` | MapLibre 层：三级椭圆安全域 + RM Vector | `SAT1Data.targets[].cpa_ring` + Capability Manifest |
| `RmVectorLayer.tsx` | MapLibre 层：目标船相对运动矢量线 | `SAT1Data.targets[].rm_vector` |

---

## 13. GAP 台账（Screen 3 范围）

| GAP | 描述 | 当前状态 | 修复路径 | D-task |
|---|---|---|---|---|
| **GAP-026** | WebSocket 非标协议 | `telemetry_bridge.py` 自定义帧 | 切 foxglove 标准 WS 协议（[W49][W50]）| D1.3b.3 |
| **GAP-029** | 路由未重命名 | `/bridge/:runId` | rename → `/monitor/:runId` + `SimulationMonitor.tsx` | D1.3b.3 |
| **GAP-NEW-003** | ToR 物理锁逻辑 | 5s 灰显防误触 | 改为 ≥2s 持续按压（IMO MASS Code Part 2-A §6.3.2）| D1.3b.3 |
| **GAP-NEW-004** | M6 5 层决策树缺失 | 仅有简版 `ColregsDecisionTree.tsx` | 新增 `ColregsRationaleTree.tsx`（5 层完整版）| D2.4 |
| **GAP-NEW-005** | M4 IvP 可视化缺失 | 无 | 新增 `IvpRiskGradientLayer.tsx` | D2.4 |
| **GAP-NEW-006** | M5 MPC 轨迹可视化缺失 | 无 | 新增 `MpcTrajectoryLayer.tsx` | D2.4 |
| **GAP-NEW-007** | M7 SOTIF 监控带缺失 | 无 | 新增 `SotifMonitorStrip.tsx` | D2.5 |
| **GAP-NEW-008** | 决策链时延条缺失 | 无 | 新增 `DecisionChainTimingBar.tsx` | D2.4 |
| **GAP-NEW-009** | SAT-3 ghost trails 缺失 | 无（Phase 3）| D3.4 完整化 | D3.4 |
| **[TBD-HAZID]** | ToR 三段升级精确秒数 | 20s/45s 暂定 | HAZID RUN-001 校准 → Veitch 2024 完整 PDF | D3 前 |
| **[TBD-HAZID]** | 三级安全域距离阈值 | ODD-A: 2.0/1.0/0.3nm 暂定 | HAZID RUN-001 校准 → 回填 Capability Manifest | HAZID 8/19 |

---

## 14. 分阶段交付计划

| 阶段 | DEMO 目标 | Screen 3 可用功能 |
|---|---|---|
| **DEMO-1 (6/15)** | Skeleton Live | Captain 视图全量；WebSocket foxglove 标准协议；FSM 5 态 UI；ToR Modal（≥2s 物理锁）；路由重命名；ASDR Ledger；Module Pulse；M6 简版决策树 skeleton |
| **DEMO-2 (7/31)** | Decision-Capable | Engineer 视图全量；M4 IvP 风险梯度向量；M5 MPC 13 分支；M6 5 层完整决策溯源树；M7 SOTIF 监控带；6 维实时评分；决策链时延条；ROC 视图 |
| **DEMO-3 (8/31)** | Full-Stack + Safety + ToR | SAT-3 ghost trails + 置信椭圆；ToR 三段精确升级时序（锁定 [W57]）；接管延迟量化记录；MRC 回切协议完整化；触觉告警（硬件依赖）|

---

## 15. 引用

| 编号 | 来源 | 置信度 |
|---|---|---|
| [W49] | `@tier4/roslibjs-foxglove` v0.0.4 — ROS2 foxglove WS client | 🟡 |
| [W50] | foxglove_bridge protocol spec (docs.foxglove.dev) | 🟢 |
| [W52] | IMO MSC.302(87) + IEC 62923-1:2018 — Alert management | 🟢 |
| [W54] | Chen et al. 2014 — SAT-1/2/3 透明性框架 | 🟢 |
| [W57] | Veitch 2024 — 60s TMR baseline（PDF 未入库 [W-pending-3]）| 🟡 |
| [W58] | IEC 62288:2021 Ed 3.0 — Navigation display presentation | 🟢 |
| [W59] | Zustand v5 `useShallow()` — 50Hz 精细选择器 | 🟢 |
| [R3] | Benjamin et al. 2010 — MOOS-IvP IvP 多目标优化 | 🟢 |
| [R7] | Yasukawa & Yoshimura 2015 — MMG 4-DOF | 🟢 |
| [R18] | IMO COLREGs 1972（现行版） | 🟢 |
| [R20] | Eriksen et al. 2020 — BC-MPC 分支树算法 | 🟢 |

---

## 16. 修订记录

| 版本 | 日期 | 改动 |
|---|---|---|
| v1.0 | 2026-05-18 | 基线建立。整合 Doc 3 §8（v1.0.2）+ SIL-Design-v2.0 Captain HMI 档案 + 架构报告 §8-§11（M4/M5/M6/M7）+ NLM colav_algorithms 调研（IvP 风险梯度向量先例、BC-MPC 13 分支配色、Decision Rationale Tree）+ NLM maritime_human_factors 调研（IEC 62288 S-Mode 强制元素、SAT-1/2/3 UI 映射、ToR 物理锁 ≥2s 规格）。新增 6 个工程视图专属组件设计规格（IvP/MPC/COLREGs/SOTIF/时延/安全域）；修正 ToR 物理锁（5s 灰显→≥2s 持续按压 [IMO MASS Code Part 2-A §6.3.2]）；开 9 个 GAP-NEW。 |
