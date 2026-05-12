# MASS L3 战术决策层 HMI 设计规范
## UI Design Specification · v1.0

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-HMI-SPEC-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式 |
| 架构基线 | TDL v1.1.2（M8 详细设计 SANGO-ADAS-L3-DD-M8-001 v1.0） |
| 监管对齐 | DNV-CG-0264 · IMO MSC 108/4 · IEC 62288 · OpenBridge 6.0 |
| 输入文档 | MASS_TDL_Bridge_HMI_v0_1.html（Gemini 初稿）· 深度调研报告（v1.0）|

---

## 0. 文档目的与使用说明

本文档是前端开发团队的**唯一权威需求基准**，覆盖：

1. **Gap 分析**：v0.1 初稿（Gemini 输出）与架构 v1.1.2 及行业规范的差距清单
2. **布局架构**：三区布局的空间分配、响应边界与信息优先级定义
3. **逐组件规范**：每个 UI 区域的数据来源、渲染规则、交互逻辑、降级行为
4. **交互流程**：ToR 协议、模式切换、回切序列的完整状态机
5. **视觉令牌**：颜色、字体、间距、动效的语义化变量系统
6. **合规边界**：OpenBridge 6.0 / IEC 62288 / DNV-CG-0264 的强制性要求

---

## 1. v0.1 Gap 分析（必读）

### 1.1 阻断性缺陷（P0 · 影响架构正确性）

| 编号 | 缺陷描述 | v0.1 现状 | 规范要求 | 来源 |
|---|---|---|---|---|
| **G-P0-01** | ToR 按钮无 ≥5s SAT-1 显示验证 | 点击即可确认 | `tor_acknowledgment_button_enabled` 须等 SAT-1 显示 ≥5s 后才解锁 | M8 DD §4.1 / IMO F-NEW-004 |
| **G-P0-02** | 自主等级标签错误 | "D4·FULL AUTO" | 项目无 D4 定义；架构仅有 D2(手动)/D3(监督)/D4(全自主)，D3 是常态运营等级 | CLAUDE.md §2 |
| **G-P0-03** | 缺失 TMR/TDL 双窗口显示 | 无 | TMR（操作员反应时间窗）与 TDL（系统计算的剩余决策时限）须分别实时显示；TDL<30s 时自动触发 SAT-3 优先提升 | M8 DD §5.1 |
| **G-P0-04** | 缺失回切序列进度 | 无 | 接管解除后须显示 M7_READY → M5_RESUME 的顺序化进度条（时序：T+10ms→T+100ms→T+110ms→T+120ms）| M8 DD §4.2 / v1.1.1 §11.9.2 |
| **G-P0-05** | 缺失 ODD 子域标识 | 仅显示总分 | 须显示当前所在子域（ODD-A/B/C/D），EDGE 状态须触发视觉脉冲预警 | 架构 §3 ODD 框架 |
| **G-P0-06** | M7 消息丢失无 UI 响应 | 无 | M7 消息丢失 ≥2 帧须立即禁止 D3/D4，显示"安全监督失效"红色告警 | M8 DD §2.2 |

### 1.2 重要缺陷（P1 · 影响行业合规与船长认知）

| 编号 | 缺陷描述 | v0.1 现状 | 规范要求 | 来源 |
|---|---|---|---|---|
| **G-P1-01** | 无"幽灵船"轨迹预测 | 仅显示静态规划路径 | 须绘制 M5 输出的 4-WP 序列为平滑样条曲线，标注 T+15s/T+30s/T+60s 位置 | Wärtsilä SmartPredict / 调研报告 §HMI龙头 |
| **G-P1-02** | 无 BC-MPC 分支可视化 | 无 | 须叠加显示 BC-MPC 的多条候选分支（info 色半透明虚线） | M5 设计 §BC-MPC |
| **G-P1-03** | APF/安全气泡无语义分级 | 无气泡 | 须根据 COLREGs 分类为目标渲染不对称 APF 气泡，Give-Way 关系用危险色，Stand-On 用警告色 | M6 COLREGs / 调研报告 §M5/M6 |
| **G-P1-04** | 缺失 E/T/H 三轴分解 | 仅显示 ODD 总分 | 须将三轴独立显示（条形图），任一轴趋近阈值时独立变色 | M1 DD / 调研报告 §M1 |
| **G-P1-05** | 无船长/操作员双视图 | 仅有一种视图 | 须实现 CAPTAIN（自然语言摘要）和 ROC OPERATOR（数字量化+规则链）两种角色视图 | M8 DD §1 §4.1 §12.3 |
| **G-P1-06** | 缺失多模态告警升级 | 仅视觉 | 须定义视觉→听觉→触觉（硬件振动）的三级升级序列 | 调研报告 §认知负荷 / DNV-ST-0324 |
| **G-P1-07** | 无 MRC 方案预显示 | 无 | ToR 超时未应答时须在海图上显示 MRC 执行轨迹预测（漂移/锚泊路径） | M8 DD §4.2 |
| **G-P1-08** | 缺失 ASDR 记录指示器 | 无 | 须有持续可见的"ASDR 记录中"状态指示，ToR 事件后须显示签名验证状态 | M8 DD §3 §4.3 |
| **G-P1-09** | border-radius 8px 不符合船桥规范 | 圆角卡片 | 船桥 HMI 采用直角或极小倒角（≤2px）；圆角降低工业感知可信度，违背 IEC 62288 视觉惯例 | IEC 62288 / OpenBridge |
| **G-P1-10** | 无日间/黄昏/夜间模式切换 | 仅夜间色 | 须实现 4 模式切换（Day/Dusk/Night/Bright），所有颜色用 CSS 语义变量，禁止硬编码 | OpenBridge 6.0 §光环境 |

### 1.3 改善项（P2 · 影响用户体验与认知效率）

| 编号 | 缺陷描述 | 规范要求 |
|---|---|---|
| **G-P2-01** | 模块标签色过于突出 | 所有 M-Tag 须降低对比度，避免抢占目标状态的视觉层级 |
| **G-P2-02** | 缺失通信链路 RTT 显示 | 底部 conning 条须显示当前 ROC 链路 RTT（毫秒级），>500ms 变橙，>2000ms 变红 |
| **G-P2-03** | 威胁列表无 COLREGs 角色标注 | 每条目标须标注当前 COLREGs 角色（Give-Way / Stand-On / Overtaking / Head-On）|
| **G-P2-04** | 无消息年龄/陈旧度指示 | 某模块消息年龄 >1s 时须灰显该模块的 UI 区域，提示数据陈旧 |
| **G-P2-05** | 雷达扫线颜色为 info 蓝（错误） | 雷达扫线应为 phosphor 青（#5BC0BE），符合传统夜间雷达视觉语言 |
| **G-P2-06** | 缺失 COG/ROT 实时仪表 | 底部 conning 须包含：HDG/SOG/COG/ROT/舵角 五件套（IMO Res. A.694 最小集）|
| **G-P2-07** | 无场景轴自动切换逻辑 | M8 状态机切换（TRANSIT→COLREG_AVOIDANCE→MRC_PREP）须在 UI 中显式可见 |

---

## 2. 整体布局架构

### 2.1 三区五段布局

```
┌────────────────────────────────────────────────────────────────────────┐
│  TOPBAR  (64px 固定高度)                                               │
│  [ Brand ] [ 船名/IMO ] [ 自主等级徽章 ] [ ODD 子域 ] [ TMR/TDL ]      │
│  [ 健康摘要 ] [ UTC 时间 ] [ 光照模式 ] [ 角色切换 ]                   │
├───────────────────────────────────────────┬────────────────────────────┤
│                                           │                            │
│   CHART AREA (战术画布)                   │  DECISION PANEL (右侧面板) │
│   60% 宽度                                │  360px 固定宽              │
│                                           │                            │
│   [海图底图层]                             │  [SAT-2 决策理由]           │
│   [ENC 叠加层]                            │  [COLREGs 推理卡]           │
│   [雷达扫描层]                            │  [SAT-3 预测面板]           │
│   [APF 势场层]                            │  [不确定性量化]             │
│   [目标 AIS 层]                           │  [M7 安全监督]              │
│   [轨迹预测层]  ← Ghost Ship 幽灵船        │  [ASDR 状态]               │
│   [BC-MPC 分支层]                         │                            │
│   [本船 + 计划航线层]                      │                            │
│                                           │                            │
├───────────────────────────────────────────┴────────────────────────────┤
│  CONNING BAR (96px 固定高度)                                           │
│  [ HDG ] [ SOG ] [ COG ] [ ROT ] [ 舵角 ] │ 威胁列表 │ 操作按钮组       │
└────────────────────────────────────────────────────────────────────────┘
```

**比例约束**：
- Topbar 高度：64px（固定，不随内容伸缩）
- Chart Area：`calc(100vw - 360px)` 宽，`calc(100vh - 64px - 96px)` 高
- Decision Panel：360px 固定宽，独立垂直滚动
- Conning Bar：96px（固定），分三列：5 仪表（1fr） / 威胁列表（1.2fr） / 操作区（0.7fr）
- **严禁**使用比例宽度（%）定义 Chart Area 与 Panel，必须用 CSS Grid 的 `1fr / 360px`

### 2.2 信息优先级分层

```
优先级 1（全时可见，不可被遮挡）
  → Topbar 全部内容
  → Conning HDG/SOG 数字
  → 自主等级徽章
  → 当前活跃威胁（CPA < 1nm）在海图上的标注

优先级 2（正常情况下可见，紧急时可压缩）
  → Chart Area 主图层
  → Decision Panel SAT-2 区域
  → Conning 威胁列表

优先级 3（按需展示，紧急时自动折叠）
  → SAT-3 预测图表
  → ASDR 状态区
  → M7 安全监督细节

紧急全屏覆盖（ToR 模态层，优先级 0）
  → 须完全屏蔽 P2/P3 内容，保留 P1 的 Chart Area 透明度 30%
```

### 2.3 场景轴自动布局切换

| M8 场景 | Chart 层级变化 | Panel 内容 | Topbar 状态 |
|---|---|---|---|
| **TRANSIT（常态）** | APF 隐藏，轨迹透明度 60% | SAT-2 折叠，仅显 SAT-3 基线 | 正常亮度 |
| **COLREG_AVOIDANCE** | APF 全显，轨迹突出，幽灵船可见 | SAT-2 展开（自动），COLREGs 卡展开 | 边框轻微警告色脉冲 |
| **MRC_PREPARATION** | 非相关图层透明度 30%，MRC 轨迹绿显 | SAT-3 最高优先，MRC 方案卡展开 | 全行橙色高亮 |
| **MRC_ACTIVE** | 仅保留本船+MRC 执行轨迹，其余隐藏 | 仅显 MRC 状态 + M7 告警 | 红色闪烁边框 |
| **OVERRIDE_ACTIVE** | 海图正常但操作按钮区高亮"接管模式" | 接管时M7降级监测卡持续显示 | D2 蓝色标识 |

---

## 3. Topbar 组件规范

### 3.1 数据来源映射

| UI 元素 | 数据来源 | 字段路径 | 刷新率 |
|---|---|---|---|
| 自主等级徽章 | M1 | `ODD_StateMsg.auto_level` | 事件驱动 |
| ODD 子域标识 | M1 | `ODD_StateMsg.current_zone` | 1 Hz + 事件 |
| ODD 合规分 | M1 | `ODD_StateMsg.conformance_score` | 1 Hz |
| TMR 值 | M1 | `ODD_StateMsg.tmr_s` | 1 Hz |
| TDL 值 | M1 | `ODD_StateMsg.tdl_s` | 1 Hz |
| 系统健康 | M7 | `Safety_AlertMsg.overall_health` | 事件 |
| UTC 时间 | 本地 / GNSS | — | 1 Hz |
| 通信链路 RTT | M8 内部 | `M8_State.communication_link_status` | 2 Hz |

### 3.2 自主等级徽章规范

```
外观：矩形边框（border-radius: 2px），左侧彩色呼吸灯，文字用 Saira Condensed 600
尺寸：约 140×38px，边框宽 1px，背景半透明（opacity 0.08 底色）

状态 → 颜色映射：
  D4 (Full Auto)     → --color-auto-d4: #3FB950（绿）  ← 最高自主
  D3 (Supervised)    → --color-auto-d3: #79C0FF（蓝）  ← 常态运营等级
  D2 (Manual/RO)     → --color-auto-d2: #F0B72F（琥珀）← 人工接管
  MRC               → --color-mrc:     #F85149（危险红）← 最低风险状态

文字格式：
  D4 → "D4 · FULL AUTO"
  D3 → "D3 · SUPERVISED"
  D2 → "D2 · MANUAL"（若由 ROC 接管显示 "D2 · MANUAL (RO)"）
  MRC → "MRC · [方案名]"（如 "MRC · DRIFT" / "MRC · ANCHOR"）

呼吸灯：
  正常状态：2.4s 周期 opacity 1.0→0.45→1.0
  告警状态（ODD EDGE）：1.2s 加速
  临界状态（ToR 触发）：0.6s 快速，同时触发边框闪烁
```

### 3.3 ODD 子域卡规范

```
显示内容：
  第一行（label）：  "ODD ZONE"  · Saira Condensed 9px · tracking 0.18em · --color-phos
  第二行（zone）：   "A · OPEN WATER"  · JetBrains Mono 14px · bold · --txt-0
  第三行（status）：  "CONF 0.87 · IN-ENVELOPE"  · JetBrains Mono 10px · --txt-2

状态 → 边框颜色：
  IN-ENVELOPE  → --color-phos（青）
  EDGE         → --color-warn（琥珀）+ 整卡轻微闪烁（0.8s 边框透明度振荡）
  OUT          → --color-danger（红）+ 持续高亮，触发场景切换

子域代码含义（须与 M1 参数库对齐）：
  A → Open Water
  B → Traffic Separation Scheme / VTS
  C → Restricted Waters（港口/狭水道）
  D → Fog / Visibility < ODD 阈值
```

### 3.4 TMR/TDL 双窗口规范

```
外观：一个组件显示两行，带一条进度条

[TMR]   60 s     ← JetBrains Mono 12px bold，橙色阈值以下绿色
[TDL]  184 s     ← JetBrains Mono 12px bold，--color-stbd 绿色

进度条语义：
  长度 = TDL 剩余比例（TDL / TDL_initial，归一化）
  颜色渐变：绿（>120s）→ 琥珀（60-120s）→ 危险红（<60s）
  TMR 阈值标记线：在进度条 TMR/TDL_initial 处显示 1.5px 橙色竖线

TDL < 30s 触发行为：
  → 该组件背景变危险红半透明
  → Topbar 全行底部边框红色扫光
  → Decision Panel SAT-3 面板自动滚动到顶部并加粗字体
  → ASDR 自动记录"SAT-3 优先级提升"事件
```

### 3.5 角色/视图切换器

```
样式：2 段切换（无动画淡入），左侧 CAPTAIN / 右侧 ROC OPERATOR
激活状态：--color-phos 背景，--bg-0 文字
非激活：透明背景，--txt-2 文字
字体：Saira Condensed 11px · tracking 0.12em · font-weight 500
边框：1px solid --line-2，直角

角色切换触发的 UI 变化（详见 §7）：
  CAPTAIN → 决策面板显示"自然语言摘要"，COLREGs 卡显示简化规则结论
  ROC OPERATOR → 决策面板显示完整 IvP 数学分解，COLREGs 卡展开全链
```

---

## 4. 海图区（Chart Area）分层规范

### 4.1 图层堆叠顺序（z-index 从低到高）

```
z-index 1   ENC 海图底图（灰蓝色调，仅显示重要水深线/航标）
z-index 2   网格/罗经环/距离环（低对比度，--line-1 色）
z-index 3   ODD 包络渐变圈（琥珀色边界虚线，EDGE 状态高亮）
z-index 4   雷达扫描层（phosphor 扫线，radial-gradient 余晖）
z-index 5   APF 势场层（各目标 APF 气泡，半透明渐变）
z-index 6   计划航线层（L2 下发的 WP 序列，--color-stbd 实线）
z-index 7   BC-MPC 候选分支层（--color-info 半透明虚线）
z-index 8   幽灵船预测轨迹层（--color-magenta 虚线 + T+Xs 标注）
z-index 9   目标 AIS/雷达融合层（各目标符号 + 速度向量 + COLREGs 标注）
z-index 10  本船层（phosphor 三角 + 航向线 + 过去航迹）
z-index 20  浮层 UI（行为标签、图例、刻度尺、MRC 状态）
z-index 30  ToR 全屏模态（覆盖全部 Chart Area，保留 30% 透明度）
```

### 4.2 本船符号规范（ECDIS IMO MSC.191(79)）

```
形状：    正三角（顶部朝航向），填充 --color-phos，边框 1px
航向线：  自本船顶点延伸，长度 = SOG × 6 分钟（默认），--color-phos
COG 线：  区别于 HDG 线，虚线，风流差明显时显示，颜色略暗
过去航迹：历史 3 分钟点迹，phosphor 色渐淡点虚线，每点间距 30s
尺度标注：在本船旁侧显示 "HDG 005° · 18.4 kn"，Saira Condensed 11px
```

### 4.3 AIS/雷达目标符号规范

```
基础形状：等边三角，顶点指向 COG 方向（ECDIS 惯例，非 HDG）
速度向量：自目标中心延伸，长度比例同本船
尺寸：    大型目标（>100m）三角边长 16px；小型 12px；本船 14px

COLREGs 角色颜色编码（M6 输出）：
  Give-Way 关系对  → 目标边框 --color-danger（红），APF 气泡红色
  Stand-On 关系对  → 目标边框 --color-warn（琥珀），APF 气泡琥珀色
  安全目标        → 目标边框 --color-info（蓝），APF 气泡蓝色淡显
  追越关系        → 目标边框 --color-info，底部加 "OT" 文字标签

置信度显示：
  AIS+雷达融合 (confidence ≥ 0.9)：实心三角
  仅雷达 (0.7 ≤ confidence < 0.9)：空心三角
  低置信度 (< 0.7)：不显示目标（M8 DD §2.2 过滤规则）

信息标注（目标旁）：
  第一行："T01 · MARITIME STAR"  → Saira Condensed 11px
  第二行："CPA 0.42 nm · TCPA 4'47""  → JetBrains Mono 9px
  第三行（仅当 COLREGs 触发）："RULE 15 · GIVE-WAY"  → Saira Condensed 9px
```

### 4.4 APF 势场气泡规范（M6 输出映射）

```
形状：不对称椭圆（水滴形），参数由 M6 输出的 APF 数学域决定
  前向半轴 > 后向半轴（前方危险区更大）
  侧向按会遇角度不对称

渲染：径向渐变，从中心 opacity 0.25 → 外边缘 opacity 0.0
颜色：
  Give-Way 关系 → rgba(242,107,107, 0.25) 红
  Stand-On 关系 → rgba(240,183,47,  0.18) 琥珀
  安全目标      → rgba(121,192,255, 0.12) 蓝

CPA 安全圈（独立显示）：
  半径 = 1.0 nm（默认安全距离）
  样式：虚线圆环，--color-danger，stroke-dasharray 3 3
  仅在 TCPA < 10min 的目标周围显示
```

### 4.5 幽灵船预测轨迹（Ghost Ship · M5 输出）

```
数据来源：M5 输出的 4-WP 避让序列（4 个航路点 + 速度剖面）

渲染方式：
  1. 将 4-WP 用 Catmull-Rom 或二次贝塞尔样条插值为平滑曲线
  2. 曲线样式：--color-magenta 虚线，stroke-dasharray: 6 4，stroke-width: 2
  3. 在曲线上标注时间节点：
     T+15s → 半透明小船轮廓（scale 0.6） + 时间标注
     T+30s → 半透明小船轮廓（scale 0.6）
     T+60s → 半透明小船轮廓（scale 0.6）

BC-MPC 候选分支（同时叠加）：
  3-5 条细虚线，--color-info，opacity 0.35，stroke-dasharray: 3 3
  表示 BC-MPC 的多条备选轨迹候选

交互：
  鼠标悬停轨迹节点 → tooltip 显示该时刻的预测 (ψ, u, ROT)

回退状态：
  系统处于 OVERRIDE_ACTIVE → 幽灵船轨迹隐藏（M5 冻结，无法预测）
```

### 4.6 MRC 预备轨迹显示

```
触发条件：M8 场景 = MRC_PREPARATION 或 ToR 超时

显示内容：
  MRC-01 漂移轨迹：绿色虚线，标注"DRIFT · MRC-01"
  MRC-02 锚泊路径（若在适合锚位附近）：青色虚线，标注"ANCHOR · MRC-02"
  MRC-03 顺风漂泊：蓝色虚线

样式要求：
  背景非相关图层降低至 opacity 30%，MRC 轨迹 opacity 100%，突出显示
  MRC 轨迹端点标注"MRC TARGET POSITION"
```

---

## 5. 右侧决策面板规范

### 5.1 面板分区结构

```
决策面板（360px 固定宽，独立滚动）
│
├── § SAT-2 当前决策依据（常显，COLREGs 冲突时自动展开）
│   ├── 行为摘要卡（Captain 模式：自然语言 / Operator 模式：IvP 分解）
│   └── COLREGs 推理卡（每个威胁一张）
│
├── § SAT-3 预测与不确定性
│   ├── 预测指标网格（2×2）
│   ├── 不确定性条形图（4 条）
│   └── 微型趋势 sparkline 图
│
├── § M7 安全监督状态
│   ├── SOTIF 检查项清单
│   └── Doer-Checker 验证状态
│
└── § ASDR 审计状态
    ├── 记录状态指示器
    └── 最近事件时间戳
```

### 5.2 SAT-2 行为摘要卡

**CAPTAIN 模式（自然语言）：**

```
外观：左侧 3px 竖线（颜色跟随当前行为类型），背景半透明警告色

主句：Saira Condensed 17px · font-weight 500 · --txt-0 · line-height 1.4
  关键参数（数字、船名、方向）：强调色，font-weight 600

次句：12.5px · --txt-2 · line-height 1.55（解释依据）

示例（COLREGs Avoidance 场景）：
  主句："正在向右舷 35° 避让前方目标 Maritime Star，预计 8 分 30 秒后恢复原航线。"
  次句："目标船从右前方交叉接近（Rule 15），本船作为让路船须及早大幅转向。当前避让满足 COLREGs Rule 8 与 Rule 16，CPA 将在 4 分 47 秒后达到 0.42 nm。"
```

**ROC OPERATOR 模式（技术分解）：**

```
外观：等宽字体代码块风格，深色背景

必须显示内容：
  M4 IvP 权重分解：
    "COLREGS_AVOIDANCE × 0.71 ⊕ TRANSIT × 0.22 ⊕ MISSION_ETA × 0.07"
    "→ heading_range [035°, 050°]"
    "→ speed_range  [16.0, 18.5] kn"

  M5 Mid-MPC 求解结果：
    "ψ_cmd = 040° · u_cmd = 17.2 kn · ROT = 6.4°/s"
    "horizon = 60s · steps = 12 · solve_time = 18 ms"

  当前 M5 工作模式（动态高亮）：
    NORMAL_LOS → 低对比度灰色
    AVOIDANCE_PLANNING → 琥珀色高亮
    REACTIVE_OVERRIDE → 红色闪烁
```

### 5.3 COLREGs 推理卡

```
每张卡对应一个威胁目标（按 CPA 距离排序，最近在最上）

卡头（card-head）：
  左：目标名称，Saira Condensed 13px · --txt-0
  右：COLREGs 角色徽章
    Give-Way 本船 → 红色实心徽章，"本船 让路船"，白色文字
    Stand-On 本船 → 绿色实心徽章，"本船 直航船"，白色文字

卡体数据行（grid-template-columns: 88px 1fr）：
  Bearing     | 044° · 右前方
  Aspect      | −118°
  CPA         | 0.42 nm  ← 颜色：<1nm 红，1-2nm 琥珀，>2nm 正常
  TCPA        | 4'47"    ← 颜色：<5min 红，5-10min 琥珀，>10min 正常
  阶段         | 独立避让（T_act 已触发）
  会遇分类     | 交叉会遇 · Rule 15

规则推理链（左侧 2px phos 竖线）：
  "会遇分类 → Rule 15 (Crossing) → Rule 16 (Give-way) → Rule 8 大幅转向 ≥30° → 输出 STBD +35°"

SAT-2 置信度门控：
  M6.confidence ≥ 0.8 → 显示完整规则链
  M6.confidence < 0.8 → 显示"规则推理置信度不足（0.72），请人工确认"
```

### 5.4 SAT-3 预测面板

```
2×2 网格指标卡（predict-grid）：
  [CPA · T01]        [恢复原航线]
  [ETA · WP-12]      [下一接管窗口]

每个指标卡包含：
  label：9.5px · Saira Condensed · --txt-3 · tracking 0.16em
  value：JetBrains Mono 18px · font-weight 600 · --txt-0
  trend：10px · 方向箭头 + 说明文字（颜色：良好→绿，需注意→琥珀）
  sparkline：24px 高 SVG 微图表（折线图）

TDL < 30s 触发行为：
  SAT-3 面板整体加粗字体，背景变为危险红半透明
  所有指标卡 label 颜色变 --color-danger
  自动展开至最大高度

不确定性条形图（4 条）：
  [T01 意图]    ████░░░░  0.62（越低越确定）
  [感知融合]    ██░░░░░░  0.92
  [通信链路]    ███░░░░░  0.88
  [水动力模型]  ███░░░░░  0.84

  条形颜色：fill 渐变从 --color-stbd → --color-warn（随不确定度升高而偏橙）
  标注格式：右侧 4 位小数表示置信度（不是百分比）
```

### 5.5 M7 安全监督状态区

```
必须显示的 SOTIF/IEC 61508 检查项（来自 M7 Safety_AlertMsg）：
  · AIS / Radar 一致性     → ✓ OK / ⚠ 降级 / ✗ 失效
  · 目标运动可预测性        → ✓ OK / ⚠ 低
  · 感知覆盖盲区比例        → 数值 % + 阈值判断
  · COLREGs 可解析性        → ✓ 已解析 / ⚠ 置信度不足
  · 通信链路 RTT            → 数值 ms + 阈值判断（>500ms 橙，>2000ms 红）
  · M7 心跳状态             → ✓ 正常 / ✗ 失效（若失效须同时触发 G-P0-06 规则）

M7 消息陈旧/丢失处理：
  若 M7 消息年龄 > 200ms：整个 M7 区域灰显，显示"M7 数据陈旧 [XXXms]"橙色
  若 M7 丢帧 ≥ 2：整区变红背景，"安全监督失效" + 强制禁止 D3/D4（需联动 Topbar 自主等级徽章）
```

### 5.6 ASDR 审计状态区

```
持续可见的记录状态指示器（常驻在面板底部）：
  状态点（绿色 / 红色）+ "ASDR · 记录中" 文字
  最近事件：如 "06:42:18 · tor_acknowledgment_clicked"
  签名状态：SHA-256 验证图标（✓ 已签名 / ⚠ 待确认）

ToR 事件后额外显示：
  事件类型、时间戳、SAT-1 显示时长（须 ≥5s 才允许确认）
```

---

## 6. Conning Bar 组件规范

### 6.1 五件套仪表（IMO Res. A.694 最小集）

```
布局：5 列等宽，每列一个仪表

仪表通用结构：
  第一行（label）：Saira Condensed 9.5px · tracking 0.22em · --txt-3 · font-weight 600
  第二行（value）：JetBrains Mono 24px · font-weight 600 · --txt-0 · display flex align-baseline
                  数字 + 小号 unit（11px · --txt-3）
  第三行（meta）： JetBrains Mono 10px · --txt-2（辅助信息）

五件套配置：
  [1] HDG      | 005 °T      | → CMD 040°（MPC 指令，颜色对比）
  [2] SOG      | 18.4 kn     | CMD 17.2 kn
  [3] COG      | 008 °T      | SET 002° · DRIFT 0.4kn
  [4] ROT      | +6.4 °/s    | 向右旋回（正值绿/STBD，负值红/PORT）
  [5] RUDDER   | 12 °S       | 舵角可视化条（水平细条，中心线，左红右绿）
```

### 6.2 通信链路 RTT 指示器（新增）

```
位置：Conning Bar 最右侧，仪表组之后

样式：与仪表同格式
  label："ROC RTT"
  value：JetBrains Mono 16px，颜色：≤500ms 绿，≤2000ms 琥珀，>2000ms 红
  若 RTT > 2000ms：触发 M7 降级告警逻辑

数据来源：M8_State.communication_link_status（2 Hz 刷新）
```

### 6.3 威胁列表

```
标题行：Saira Condensed 9.5px · tracking 0.22em + 目标计数（琥珀色）

每行 grid（4 列）：
  [烈度图标] [目标名称] [CPA] [TCPA]

烈度图标：
  CPA < 1nm（危险）  → 实心红圆 ●  · pulse-danger 动效
  CPA 1-2nm（警告） → 实心琥珀三角 ▲
  CPA > 2nm（监控） → 空心蓝圆 ○

行高亮：CPA < 1nm 的行整行背景 rgba(242,107,107, 0.06)

交互：点击目标行 → 海图自动居中该目标 + 展开 COLREGs 推理卡（仅 Operator 模式）
```

### 6.4 操作按钮区

```
布局：2 列网格 + 1 个跨全列的 REQUEST TAKEOVER 按钮

[REQUEST TAKEOVER 按钮] （跨 2 列，高 42px）
  正常状态：--color-warn 边框，--color-warn 文字，背景 rgba(240,183,47,0.06)
  左侧呼吸灯：8px 圆点，--color-warn 色
  悬停：背景变 --color-warn，文字变 --bg-0（整按钮反色）
  副标签："D3 → D2 · ToR PROTOCOL"

[HOLD COURSE]  [MRC · DRIFT]
  HOLD COURSE：正常灰色风格，副标签 "PAUSE M5"
  MRC · DRIFT：--color-danger 边框，危险样式

按钮通用规范：
  border-radius: 0（直角，无圆角）
  font-family: Saira Condensed
  letter-spacing: 0.18em
  font-weight: 600
  padding: 0（内容 flex 居中）
  cursor: pointer
  transition: all 0.15s
```

---

## 7. ToR 协议交互流程（完整规范）

### 7.1 触发条件（来自 M8 DD §4.2）

```
自动触发：
  A. M1 计算 TDL ≤ TMR（系统判断接管时窗已不充裕）
  B. ODD 状态 = OUT（已出域）
  C. M7 消息丢帧 ≥ 2（安全监督失效）
  D. M7 发出 CRITICAL 告警

人工触发：
  A. 船长/操作员点击"REQUEST TAKEOVER"按钮
```

### 7.2 模态层规范

```
遮罩：fixed inset-0，background rgba(7,12,19,0.75)，backdrop-filter blur(6px)
      Chart Area 透明度降至 30%（保持态势感知）

模态框尺寸：560px 宽（响应式：min-width 480px），居中
边框：2px solid --color-warn，border-radius: 0

头部扫光动效：顶部 4px 渐变光条，持续横扫（表示倒计时流逝）

必须显示的信息块：
  1. 触发原因（自然语言）：17px · --txt-0
  2. 2×2 上下文摘要格：CPA · 推荐行动 · 降级方案 · 目标等级
  3. 60s 倒计时（大号数字：JetBrains Mono 32px · bold · --color-warn）
     + 倒计时进度条（宽度随时间线性递减）
  4. 两个操作按钮（见下）
```

### 7.3 "已知悉"按钮解锁规则（F-NEW-004）

```
关键约束：
  按钮在 SAT-1 数据面板显示 ≥ 5s 之前必须禁用（灰色，不可点击）
  禁用状态文字："ACCEPT · 请先确认当前态势（Xs 后解锁）"
  解锁后文字："ACCEPT · 我接管 (D2)"

ASDR 记录内容（点击时刻快照）：
  tor_acknowledgment_clicked = true
  sat1_display_duration_s（实际显示时长，须 ≥ 5.0）
  sat1_threats_visible（当前可见威胁列表）
  odd_zone_at_click, conformance_score_at_click
  operator_id（从角色切换器读取）
```

### 7.4 超时降级行为（60s 无响应）

```
T = 0s：推送 ToR 模态，启动倒计时
T = 30s：若未应答，重推一次（多通道），触发听觉告警升级
T = 60s：
  a. 自动关闭 ToR 模态
  b. ASDR 记录"接管确认超时"
  c. M8 切换场景到 MRC_PREPARATION
  d. 海图显示 MRC 执行轨迹（§4.6）
  e. Topbar 自主等级徽章变为 "MRC · [方案名]"（红色）
  f. 触发三级告警：视觉（全屏闪烁 1 次）+ 听觉（连续警报音）

DECLINE（操作员拒绝接管）：
  维持当前自主等级
  ASDR 记录"操作员拒绝接管"+ 拒绝理由字段（空白）
  系统继续运行，TMR 计时继续
```

### 7.5 接管激活后 UI 状态

```
Topbar 变化：
  自主等级徽章 → "D2 · MANUAL (RO)"（琥珀色）
  ODD 子域卡显示"OVERRIDE ACTIVE"覆盖文字

Chart Area 变化：
  幽灵船轨迹隐藏（M5 冻结）
  本船符号航向线变为琥珀色

Decision Panel 变化：
  SAT-2 区域显示"接管模式 · M7 降级监测中"
  新增 M7 降级监测卡（实时刷新 < 100ms 显示时延）：
    监测项目：通信链路 RTT / 传感器状态 / 新威胁 CPA / M7 心跳

操作按钮变化：
  REQUEST TAKEOVER 按钮变为"RETURN TO AUTO"（返回自主）
  按钮颜色变蓝（--color-info）
```

### 7.6 回切序列显示（F-NEW-006）

```
触发：操作员点击"RETURN TO AUTO"，OverrideActiveSignal { override_active=false }

显示位置：Decision Panel 顶部新增"回切进度"区块

进度条（4 步顺序显示）：
  ① T+0ms    M1 进入"回切准备"    → 完成标记 ✓
  ② T+10ms   M7 启动              → 等待中 / ✓
  ③ T+100ms  M7 心跳确认 (READY)  → 等待中 / ✓
  ④ T+110ms  M5 恢复规划          → 等待中 / ✓
  ⑤ T+120ms  首个 NORMAL 避碰计划  → 等待中 / ✓

每步完成后实时标绿，未完成用灰色虚线圆
超时保护：若 M7 未在 200ms 内确认 → 步骤变红，触发告警
完成后：回切进度区块自动折叠，Topbar 恢复 D3 标识
```

---

## 8. 视觉令牌系统（CSS 变量）

### 8.1 颜色语义变量（禁止硬编码）

```css
/* —— OpenBridge 6.0 Four-Mode 光照系统 —— */
/* Night Mode（默认） */
:root[data-theme="night"] {
  --bg-0: #070C13;        /* 最深背景（全局底色）*/
  --bg-1: #0B1320;        /* 面板背景 */
  --bg-2: #101B2C;        /* 卡片背景 */
  --bg-3: #16263A;        /* 悬浮卡片 */
  --line-1: #1B2C44;      /* 基础分割线 */
  --line-2: #243C58;      /* 强调分割线 */
  --line-3: #3A5677;      /* 高亮分割线 */

  --txt-0: #F1F6FB;       /* 最高对比文字（关键数值）*/
  --txt-1: #C5D2E0;       /* 次级文字 */
  --txt-2: #8A9AAD;       /* 辅助文字 */
  --txt-3: #566578;       /* 标签/单位 */

  /* —— 语义功能色（Night 下值）—— */
  --color-phos: #5BC0BE;        /* phosphor 青（雷达/航迹/品牌）*/
  --color-phos-dim: #2D6A6A;
  --color-stbd: #3FB950;        /* 右舷绿 / 安全 */
  --color-port: #F26B6B;        /* 左舷红 / 危险 */
  --color-warn: #F0B72F;        /* 警告琥珀 */
  --color-info: #79C0FF;        /* 信息蓝 */
  --color-danger: #F85149;      /* 严重危险红 */
  --color-magenta: #D070D0;     /* 预测路径（ECDIS 惯例）*/

  /* —— 自主等级语义色 —— */
  --color-auto-d4: #3FB950;
  --color-auto-d3: #79C0FF;
  --color-auto-d2: #F0B72F;
  --color-mrc: #F85149;
}

/* Day Mode */
:root[data-theme="day"] {
  --bg-0: #E8EDF2;
  --bg-1: #F2F5F8;
  --bg-2: #FFFFFF;
  --bg-3: #FAFBFC;
  --line-1: #C8D4DE;
  --line-2: #A8B8C8;
  --line-3: #8898A8;
  --txt-0: #0A1828;
  --txt-1: #1A2E44;
  --txt-2: #3A5268;
  --txt-3: #6A8298;
  /* 功能色与 Night 相同，不变 */
}

/* Dusk Mode */
:root[data-theme="dusk"] {
  --bg-0: #101820;
  --bg-1: #182030;
  --bg-2: #202838;
  --txt-0: #D8E8F4;
  --txt-1: #A8C0D4;
  --txt-2: #6888A4;
  --txt-3: #3A5878;
}

/* Bright Mode（强光甲板使用）*/
:root[data-theme="bright"] {
  --bg-0: #C8D8E8;
  --txt-0: #000810;
  /* 高对比度 */
}
```

### 8.2 字体系统

```css
/* 显示字体（标签、标题、仪表名）*/
--fnt-disp: 'Saira Condensed', 'Noto Sans SC', sans-serif;

/* 数字字体（所有仪表数值、坐标、计时器）*/
--fnt-mono: 'JetBrains Mono', ui-monospace, monospace;

/* 正文字体（自然语言摘要、说明）*/
--fnt-body: 'Noto Sans SC', 'Saira Condensed', sans-serif;

/* 使用规则：
  所有变化的数字 → --fnt-mono（保证等宽，数字不跳动）
  所有标签/代号 → --fnt-disp（字符紧凑，信息密度高）
  所有解释性文字 → --fnt-body（中英文兼容，可读性优先）
*/
```

### 8.3 间距与圆角规范

```css
/* 边框圆角：严格限制 */
--radius-none: 0;       /* 按钮、仪表框（主要使用）*/
--radius-min: 2px;      /* 小型徽章、标签（允许使用）*/
--radius-none: 0;       /* 大型面板、模态框（强制直角）*/

/* 禁止：border-radius ≥ 4px 用于面板和卡片
  原因：IEC 62288 工业视觉惯例 + OpenBridge 规范 + 行业认知习惯 */

/* 间距基准 */
--sp-xs: 4px;
--sp-sm: 8px;
--sp-md: 12px;
--sp-lg: 18px;
--sp-xl: 24px;
```

### 8.4 动效规范

```css
/* 正常状态：允许的动效 */
--dur-fast: 0.15s;      /* 按钮 hover */
--dur-normal: 0.25s;    /* 面板展开 */
--dur-slow: 0.35s;      /* 模态进入 */

/* 禁止的动效：
  1. 超过 0.5s 的入场动画（延迟关键信息呈现）
  2. 并行超过 3 个同时运行的 CSS animation
  3. 雷达扫线以外的旋转动效
  4. 任何会影响文字可读性的模糊/毛玻璃效果（backdrop-filter 须控制）
*/

/* 告警专属动效 */
@keyframes phos-pulse {           /* 普通状态指示灯 */
  0%, 100% { opacity: 1.0; }
  50% { opacity: 0.45; }
}
@keyframes warn-pulse-fast {      /* EDGE/告警状态 */
  0%, 100% { opacity: 1.0; }
  50% { opacity: 0.3; }
  animation-duration: 0.8s;
}
@keyframes danger-border-flash {  /* MRC_ACTIVE 状态 */
  0%, 100% { border-color: var(--color-danger); }
  50% { border-color: transparent; }
  animation-duration: 0.6s;
}
```

---

## 9. 多模态告警升级序列

### 9.1 三级升级规则

| 等级 | 触发条件 | 视觉 | 听觉 | 触觉（硬件） |
|---|---|---|---|---|
| **Level 1 · Advisory** | ODD EDGE · TCPA 10-15min | 橙色脉冲边框（0.8s周期） | 无 | 无 |
| **Level 2 · Warning** | ODD OUT · TCPA 5-10min · TDL < 60s | 橙色全 Topbar 高亮 + 模态弹出 | 间歇蜂鸣 | 振动（若支持）|
| **Level 3 · Emergency** | MRC 触发 · 安全监督失效 · ToR 超时 | 全屏红色边框闪烁（0.6s） | 连续急促警报 | 强振动 |

### 9.2 告警抑制规则（LR 警报合理化原则）

```
以下告警会被 M8 自动合并/抑制，不单独弹出：
  · 同一目标的重复 CPA 告警（5s 内同源合并）
  · 非激活场景下的低优先级 SAT-2 内容（confidence < 0.7 过滤）
  · 通信 RTT 在 500ms 阈值附近反复跳变的告警（须持续 3s 以上才触发）

告警抑制的可见化：
  在 M7 安全监督区域底部显示"已抑制告警：N 条"小字说明
```

---

## 10. 性能与技术约束

### 10.1 渲染性能要求

```
海图 SVG 刷新率：
  目标列表更新：10 Hz（来自 M2 WorldState @ 10 Hz）
  幽灵船轨迹更新：2 Hz（M5 输出频率 ~2 Hz）
  仪表数值更新：10 Hz

渲染框架要求：
  不得使用纯 HTML5 Canvas 进行所有图层渲染（调试困难，无障碍性差）
  推荐：SVG 主图层 + CSS 动效（避免 requestAnimationFrame 超负荷）
  或：React + D3.js（Chart Area）+ CSS-in-JS（面板区域）

内存限制：
  历史航迹点数：最多保留 6 分钟，超出自动裁剪（避免内存泄漏）
  BC-MPC 分支：最多渲染 5 条（超出按概率取 Top-5）
```

### 10.2 WebSocket 接入规范（M8 输出接口）

```
M8 输出：UI_StateMsg @ 50 Hz via WebSocket
前端接收：onmessage handler，解析 JSON，驱动 UI 状态更新

状态字段映射（UI_StateMsg → UI 组件）：
  sat1_data.auto_level        → Topbar 自主等级徽章
  sat1_data.odd_zone          → ODD 子域卡
  sat1_data.conformance_score → ODD 子域卡置信分
  sat1_data.tmr_s             → TMR/TDL 双窗口
  sat1_data.tdl_s             → TMR/TDL 双窗口
  sat1_data.threats[]         → 威胁列表 + 海图目标
  sat2_visible                → Decision Panel SAT-2 展开/折叠
  sat2_data.colreg_cards[]    → COLREGs 推理卡
  sat2_data.behavior_summary  → 行为摘要卡
  sat3_data.*                 → SAT-3 预测面板
  tor_active                  → ToR 模态层
  tor_deadline_remaining_s    → 60s 倒计时
  tor_acknowledgment_button_enabled → 确认按钮解锁状态
  override_active             → 接管模式 UI 切换
  m7_degradation_alert        → M7 降级告警（接管期间）
  handback_sequence_state     → 回切进度条

消息年龄检测（前端实现）：
  每条消息包含 stamp（Unix ms）
  前端计算 (Date.now() - stamp)，若 > 1000ms 触发对应模块灰显
```

### 10.3 REST API 调用规范

```
POST /api/tor/acknowledge
  Body: { operator_id, timestamp, sat1_display_duration_s, threats_visible, odd_zone }
  响应：{ success: true, asdr_record_id, timestamp_signed }

POST /api/mode/switch
  Body: { target_auto_level: "D2"|"D3"|"D4", reason, operator_id }
  响应：{ success, current_level, timestamp }

GET /api/status
  响应：{ m8_state, tor_state, override_state, module_heartbeats }
```

---

## 11. 船桥工效学合规检查清单

实施前须通过以下检查（对标 OpenBridge 6.0 + IEC 62288 + DNV-CG-0264）：

### 11.1 视觉合规

- [ ] 所有颜色仅通过 CSS 变量引用，无硬编码 HEX（光照模式切换测试）
- [ ] 日间/黄昏/夜间/强光 4 模式下，所有文字对比度 ≥ 4.5:1（WCAG AA）
- [ ] 面板 border-radius ≤ 2px（大型组件直角）
- [ ] 雷达扫线颜色为 phosphor 青（#5BC0BE），非信息蓝
- [ ] 左舷色（红）不用于非"左舷/危险"语义（防歧义）
- [ ] 告警色层级清晰：danger > warn > info > phos（不混用）

### 11.2 信息架构合规

- [ ] SAT-1 全时可见，任何模态层不得完全遮挡 Topbar SAT-1 核心信息
- [ ] ToR 模态层保留 Chart Area 30% 透明度（船长仍可感知海图）
- [ ] TDL < 30s 时，SAT-3 面板自动升至最高优先级（字体加粗，背景红色）
- [ ] M7 失效时，UI 禁止 D3/D4 操作（联动控制）
- [ ] 每条威胁目标有且仅有一个 COLREGs 角色标签（避免歧义）

### 11.3 ToR 协议合规（IMO MASS Code）

- [ ] "已知悉"按钮须在 SAT-1 显示 ≥ 5s 后解锁（F-NEW-004）
- [ ] ASDR 记录 SAT-1 显示时长（须 ≥ 5.0s 才视为合法确认）
- [ ] 60s 超时无应答须自动触发 MRC（不等待操作员）
- [ ] 回切序列进度（M7→M5）须在 UI 中顺序可见（F-NEW-006）
- [ ] 所有 ToR 事件含 SHA-256 签名（防篡改 ASDR 审计）

### 11.4 认知负荷合规（LR 人为因素工程）

- [ ] 同时运行的动效不超过 3 个（防视觉噪音）
- [ ] 告警合理化：同源 5s 内重复告警合并显示
- [ ] 数字仪表使用等宽字体（数字切换不引起位移跳动）
- [ ] Captain 视图中无原始数学公式（仅自然语言摘要）
- [ ] 场景切换（TRANSIT→COLREG_AVOIDANCE）有明确视觉标志（非仅内容变化）

---

## 12. 实施优先级建议

### Wave 1（核心可用性，2 周）

> 完成后系统可进行初步船长演示

1. G-P0-01：ToR ≥5s SAT-1 验证逻辑
2. G-P0-02：自主等级标签修正（D3 → D2/D3/D4）
3. G-P0-03：TMR/TDL 双窗口
4. G-P0-05：ODD 子域 EDGE 脉冲
5. §6.1：Conning 五件套仪表
6. §4.3：本船符号 + 过去航迹
7. §8.1：CSS 语义变量体系（夜间/日间 2 模式）

### Wave 2（行业规范对齐，2 周）

> 完成后符合船级社 HMI 基本要求

8. G-P1-01：幽灵船轨迹（Ghost Ship）
9. G-P1-03：APF 气泡（分 Give-Way/Stand-On 颜色）
10. G-P1-04：E/T/H 三轴分解
11. G-P1-05：船长/操作员双视图切换
12. G-P0-04：回切序列进度条
13. §7.4：ToR 超时 MRC 轨迹显示
14. §5.4：SAT-3 不确定性条形图

### Wave 3（完整合规，3 周）

> 完成后可提交 DNV/CCS HMI 审查

15. G-P0-06：M7 失效 UI 响应 + D3/D4 禁止联动
16. G-P1-02：BC-MPC 候选分支可视化
17. G-P1-08：ASDR 审计状态区
18. G-P1-06：多模态告警升级序列（Level 1/2/3）
19. §8.1：黄昏/强光 2 模式补充
20. §11：工效学合规检查清单全部 pass

---

## 附录 A：与 v0.1 初稿实现路径对比

| v0.1 已有 | 可复用程度 | 建议 |
|---|---|---|
| CSS 变量色彩体系 | 80%（需重命名语义化 + 补充 4 模式）| 扩展，不重写 |
| Tailwind + 原始 CSS 混用 | 40%（Tailwind 动态类名难以支持主题切换）| 逐步替换为纯 CSS 变量 |
| 雷达扫线动效 | 70%（颜色需修正）| 改色复用 |
| ToR 倒计时模态 | 30%（缺 ≥5s 验证，缺 MRC 降级，缺签名）| 重写核心逻辑 |
| 3 场景演示按钮 | 保留（演示用途，生产版本用 WebSocket 替换）| 仅演示用 |
| M1/M2/M4/M5/M7 数据卡结构 | 60%（内容需按本规范重排）| 调整布局 + 数据绑定 |
| border-radius: 8px | 0%（违规）| 全部改为 0 或 2px |
| 偏心雷达 70% 下沉 | 90%（正确的 head-up 视野设计）| 保留并扩展图层 |

---

*本文档由 SANGO FCB 项目 HMI 工作组发布，对齐 TDL 架构 v1.1.2。*
*下一版本（v1.1）将在 Wave 1 完成后根据船长用户测试结果更新。*
