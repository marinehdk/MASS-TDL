# Slide Deck Outline

**Topic**: TDL战术决策系统架构与避碰流程
**Style**: sango-ai
**Dimensions**: paper + warm + editorial + dense
**Audience**: general
**Language**: zh
**Slide Count**: 20 slides
**Generated**: 2026-05-29 13:15

---

<STYLE_INSTRUCTIONS>
Design Aesthetic: Clean 2D technical briefing with vintage blueprint aesthetic, aged cream paper texture, and bilingual explanatory text boxes. Dense information organized with clear visual hierarchy and multiple labeled callouts.

Background:
  Texture: Subtle aged paper texture with light creases
  Base Color: Aged Cream (#F5F0E6)

Typography:
  Headlines: Bold display, dark maroon (#5D3A3A), ALL CAPS in brackets for main titles.
  Body: Clean geometric sans-serif, Near Black (#1A1A1A). Simplified Chinese callout labels in clean sans-serif.

Color Palette:
  Background: Aged Cream (#F5F0E6) - Primary background
  Primary Text: Dark Maroon (#5D3A3A) - Headlines
  Secondary Text: Near Black (#1A1A1A) - Body explanations
  Accent 1: Teal (#2F7373) - Primary illustrations
  Accent 2: Warm Brown (#8B7355) - Secondary elements
  Tertiary: Maroon (#722F37) - Titles, emphasis
  Outline: Deep Charcoal (#2D2D2D) - Boundaries and strokes

Visual Elements:
  - Isometric/2D technical illustrations
  - 3-5 explanatory text boxes per slide
  - Simplified Chinese callout labels
  - Faded thematic background patterns
  - Clean black outlines on all elements
  - Split or triptych layouts

Density Guidelines:
  - Content per slide: Dense information organized with clear visual hierarchy, 3-5 explanatory text boxes
  - Whitespace: Balanced but compact, using text boxes and frames to structure space

Style Rules:
  Do:
    - Include substantive content from source
    - Use Simplified Chinese callout labels
    - Retain subtle aged paper texture
    - Maintain clear visual hierarchy
  Don't:
    - Use photorealistic renders
    - Apply digital gradients or glossy effects
    - Include slide numbers, footers, or logos
</STYLE_INSTRUCTIONS>

---

## Slide 1 of 20

**Type**: Cover
**Filename**: 01-slide-cover.png

// NARRATIVE GOAL
介绍汇报的主题：战术决策系统(TDL)的架构与避碰闭环业务流程，展示专业复古的学术/工业设计感。

// KEY CONTENT
Headline: [战术决策系统架构与避碰流程汇报]
Sub-headline: TDL项目总体设计、内核模块协同及SIL仿真验证体系 / TDL Architecture & Collision Avoidance Flow

// VISUAL
A large central blueprint graphic showing a ship draft line schema overlapping with a circular steering control system diagram. Technical notations in faded red lines are scattered in the background. Thin dark lines connect different dimensions. Hand-drawn 2D layout. Aged cream background.

// LAYOUT
Layout: title-hero
Large centralized header at the top, a prominent system engineering draft pattern in the middle, and subtitles in dark maroon at the bottom.

---

## Slide 2 of 20

**Type**: Content
**Filename**: 02-slide-requirements.png

// NARRATIVE GOAL
概述L3级自主船艇战术决策层(TDL)的项目背景、核心业务诉求与安全红线。

// KEY CONTENT
Headline: [项目需求背景与核心挑战]
Sub-headline: 面向复杂通航环境的L3级自主避碰业务诉求
Body:
- 避碰合规性约束: 必须严格遵循国际海上避碰规则(COLREGs)第5-19条的所有量化与非量化条款。
- 动态避碰时效性: 在中短时尺度下进行避碰规划，算法与控制回路端到端延迟要求低于10ms。
- 安全冗余机制: 在执行(Doer)算法出现异常时，必须由监督(Checker)模块实现硬接管并降级。

// VISUAL
A side-by-side comparison diagram. The left side shows a traditional paper chart concept with rules text overlapping (COLREGs). The right side shows a modern electronic chart vector mesh with target vessels and threat indicator arrows. Thick outlines and dark accents highlight the threat area.

// LAYOUT
Layout: split-screen
Left side: structured text box with 3 bullet items. Right side: a split-screen 2D graphical illustration of COLREGs rules mapping onto a nautical grid.

---

## Slide 3 of 20

**Type**: Content
**Filename**: 03-slide-top-architecture.png

// NARRATIVE GOAL
详细剖析战术决策系统(TDL)的顶层决策架构，重点介绍Doer-Checker双轨制与ODD包络枢纽。

// KEY CONTENT
Headline: [TDL系统顶层架构设计]
Sub-headline: Doer-Checker 双轨隔离与以ODD为中心的控制拓扑
Body:
- ODD作为组织原则: M1 Envelope Manager作为调度枢纽，ODD状态是行为模式切换的唯一权威源。
- Doer-Checker双轨制: M1-M6充当Doer角色执行业务规划，M7充当Checker角色进行独立安全监督。
- 系统极简解耦: 逻辑简化大于100倍，实现路径完全物理隔离，避免安全共因失效。

// VISUAL
A structured system topology diagram showing two clear lanes (Doer and Checker). Doer lane contains boxes for M1 through M6, connected by flow lines. Checker lane contains M7, linked via a bold crossing monitor point. Faded gray arrows indicate feedback.

// LAYOUT
Layout: binary-comparison
Two horizontal blocks. The upper block (Doer) contains sequential cards of M1-M6. The lower block (Checker) is a single highlighted box representing M7, directly tapping into the output control line.

---

## Slide 4 of 20

**Type**: Content
**Filename**: 04-slide-eight-modules.png

// NARRATIVE GOAL
全面展示TDL Kernel中M1至M8八个ROS2 Native内核模块的职责全景与运行频率。

// KEY CONTENT
Headline: [8大核心内核模块全景]
Sub-headline: TDL Kernel ROS2 Native模块的职责与时间尺度分布
Body:
- 战略层管理 (0.1-1 Hz): M1 包络管理器、M3 任务管理器，负责长时航次与安全包络状态决策。
- 态势与推理 (10-50 Hz): M2 世界模型融合态势、M6 规则推理机判定COLREGs约束。
- 决策与执行 (1-4 Hz): M4 行为仲裁器(IvP solver)、M5 战术规划器(BC-MPC)与M8 HMI透明桥。
- 独立 Checker: M7 安全监督器(100 Hz)，负责假设监控与故障接管。

// VISUAL
A structured grid chart containing 8 cards (M1 to M8). Each card has a technical line-art icon (compass, radar sweep, gears, flowchart, MPC curve, rule booklet, checkmark, bridge panel), accompanied by its name and update frequency.

// LAYOUT
Layout: icon-grid
A 4x2 matrix of cards with dark outlines, thin border lines, and cream backgrounds. Accent colors (teal and warm brown) are used to highlight frequencies.

---

## Slide 5 of 20

**Type**: Content
**Filename**: 05-slide-idl-contracts.png

// NARRATIVE GOAL
介绍8大核心模块之间的通信接口契约(IDL)，确保数据流的一致性与可追溯性。

// KEY CONTENT
Headline: [模块协同接口契约]
Sub-headline: 强约束消息IDL保障全链路置信度与可解释性
Body:
- 统一时间戳: 所有消息强制携带rclcpp Time `stamp`，保证全链路数据同步与时延监控。
- 置信度传递: 数据在多源融合后必须携带 `confidence` 置信度指标（0至1之间）。
- 决策依据携带: 模块间输出必须附带结构化 `rationale`（决策依据），以供HMI透明度呈现。
- 架构防篡改: 强制schema版本验证，保障跨模块数据结构的强契约约束。

// VISUAL
A 2D technical diagram showing a ROS2 Message block dissected into four main sections: Header/Stamp, Version, Value/Confidence, and String/Rationale. Visual callout boxes point to each section with technical annotations in Chinese.

// LAYOUT
Layout: split-screen
Left side: Data flow structure explanation. Right side: Detailed graphical layout of the ROS2 message header format with callout lines.

---

## Slide 6 of 20

**Type**: Content
**Filename**: 06-slide-workflow-overview.png

// NARRATIVE GOAL
梳理航行避碰全业务流程的五个阶段，作为后续详细流程幻灯片的总纲。

// KEY CONTENT
Headline: [避碰业务流程概览]
Sub-headline: 威胁感知、逻辑研判、避碰决策、安全监视至回归航路的闭环
Body:
- 步骤一: 威胁信息分析 — M2世界模型融合传感器输入，对目标船只进行几何预分类与危险评估。
- 步骤二: 综合态势研判 — M6推理机基于COLREGs判断来船相遇态势与双方避碰责任。
- 步骤三: 避碰决策生成 — M4行为仲裁器求解IvP目标函数，M5规划器输出具体控制矢量。
- 步骤四: 安全监视 & 步骤五: 规避回归 — M7实时监控状态，M3判定危险解除后平滑引回主航线。

// VISUAL
A horizontal pipeline chevron chart representing 5 stages from left to right: 01.威胁感知 -> 02.综合研判 -> 03.避碰决策 -> 04.安全监视 -> 05.回归航路. Each step is numbered and decorated with a small line diagram.

// LAYOUT
Layout: linear-progression
A single horizontal chain spanning the center of the slide with brief descriptive text boxes underneath each milestone.

---

## Slide 7 of 20

**Type**: Content
**Filename**: 07-slide-step1-threat-analysis.png

// NARRATIVE GOAL
展示避碰流程第一步：威胁信息分析。后端算法模块是如何通过多源融合进行CPA/TCPA和几何预分类的。

// KEY CONTENT
Headline: [步骤一：威胁信息分析]
Sub-headline: M2 世界模型多源数据融合与CPA/TCPA几何预分类
Body:
- 数据融合输入: M2 整合AIS回放数据、雷达目标与视觉图像，建立高精度世界模型视图。
- CPA/TCPA计算: 动态计算与所有障碍船的最近相遇距离(CPA)和最近相遇时间(TCPA)。
- 几何预分类: 依据相对航向和方位，将障碍船分类为Head-On（对遇）、Crossing（交叉）或Overtaking（追越）。
- 触发推理机: 当障碍船侵入安全包络圈时，将分类数据封装输出至M6推理机。

// VISUAL
A radar sweep graphic on a grid. Own ship (green triangle) and a target vessel (red dot) are plotted. Dotted lines indicate their heading vectors. The point where the vectors come closest is highlighted with a circle labeled "CPA Point", with TCPA countdown text next to it.

// LAYOUT
Layout: split-screen
Left side: 4 bullet points detail backend data fusion and geometry classification. Right side: Blueprint-style radar plot indicating CPA calculation.

---

## Slide 8 of 20

**Type**: Content
**Filename**: 08-slide-step1-hmi.png

// NARRATIVE GOAL
介绍步骤一的前端交互设计：HMI是如何将威胁目标和CPA/TCPA可视化呈现的。

// KEY CONTENT
Headline: [威胁感知与CPA可视化]
Sub-headline: 态势感知图层设计：直观呈现通航环境威胁
Body:
- 危险船舶高亮: ENC海图上，侵入防撞包络的目标船显示为红色高亮状态，并标示CPA指标。
- 危险矢量线: 绘制Own ship与障碍船的航向矢量线，相交点即为CPA空间投影。
- CPA/TCPA警报窗: 悬浮面板展示来船的船名、MMSI、航速、CPA值及TCPA倒计时。
- 交互测量尺: 支持操作员在海图上点击测量目标与我船的实时距离及相对方位。

// VISUAL
A 2D layout mockup of the HMI Simulation-Monitor view. The left side is a nautical vector map showing ships. An overlay card showing CPA parameters sits on the right. A prominent warning banner reading "CPA Risk Warning" is visible.

// LAYOUT
Layout: split-screen
Left side: Visual description of HMI chart display showing the danger zones. Right side: The UI panel mockup with metrics and interactive buttons.

---

## Slide 9 of 20

**Type**: Content
**Filename**: 09-slide-step2-judgment.png

// NARRATIVE GOAL
展示避碰流程第二步：综合态势研判。后端算法如何通过COLREGs推理机和包络管理器实现逻辑研判。

// KEY CONTENT
Headline: [步骤二：综合态势研判]
Sub-headline: M1包络控制与M6规则推理机责任与态势判定
Body:
- 避碰责任划分: M6 依据COLREGs第12-17条判断我船是让路船(Give-way)还是直航船(Stand-on)。
- 条款规则激活: 激活具体责任条款，如“Rule 15 交叉相遇情景下，让路船应及早采取宽让行动”。
- ODD包络评估: M1评估风浪流等外部环境扰动，判断我船操控包络线是否具备安全规避的机动能力。
- 约束集输出: M6将避碰方向（如“仅限右转规避”）及避碰时机打包为 `/l3/m6/colregs_constraint`。

// VISUAL
A schematic of COLREGs rule classification. Three circular sectors radiating from a center ship represent Overtaking (135°), Crossing (112.5°), and Head-On (22.5°). Arrows indicate the allowed maneuver directions (mostly to starboard).

// LAYOUT
Layout: split-screen
Left side: Logic description of Rule Reasoner and ODD state evaluation. Right side: Technical drawing of the nautical collision avoidance sectors with rule numbers.

---

## Slide 10 of 20

**Type**: Content
**Filename**: 10-slide-step2-hmi.png

// NARRATIVE GOAL
介绍步骤二的前端交互设计：HMI如何向操作员解释COLREGs推理结果以提升决策透明度。

// KEY CONTENT
Headline: [规则推理透视与透明度呈现]
Sub-headline: 避碰研判面板设计：展示决策的规则依据
Body:
- 避碰状态牌: 界面顶部显眼处展示“避碰规则激活：COLREGs Rule 15”、“直航/让路：我船让路”。
- 规则推理树: 交互树状图，操作员可点击查看“判断对遇 -> 相对方位右舷 -> 让路义务”的完整因果链。
- 机动范围指示: 海图上用淡褐色虚线扇形标示出法律允许的规避航向范围，规制非合规航向。
- 决策依据汇聚: 汇总M6和M1的数据，通过M8桥接器展示结构化的避碰行动说明。

// VISUAL
HMI UI mockup showing a "COLREGs Rational Tree" window. It displays a hierarchical flowchart starting from "Input Scenario" leading to "M2 Classify: Crossing" and ending in "Action: Starboard Turn Only". Warm brown and teal tones.

// LAYOUT
Layout: split-screen
Left side: UI design details for explaining decision transparently. Right side: Interactive logic flowchart mockup showing how reasoning is verified by operators.

---

## Slide 11 of 20

**Type**: Content
**Filename**: 11-slide-step3-decision.png

// NARRATIVE GOAL
展示避碰流程第三步：避碰决策生成。介绍M4和M5如何生成具体的最优控制指令与避碰路径。

// KEY CONTENT
Headline: [步骤三：避碰决策生成]
Sub-headline: M4行为仲裁器与M5战术规划器的协同求解
Body:
- M4 IvP行为仲裁: M4 利用区间规划(IvP)算法，在航线跟踪、避碰合规和操纵性之间寻找Pareto最优解。
- 约束投影化: M6的COLREGs约束集作为刚性惩罚项，限制M4的目标函数求解空间。
- M5 规划器求解: M5 采用BC-MPC（基于动力学模型的预测控制）对规划的航向进行曲线平滑。
- 控制矢量输出: 输出我船目标航向(ψ)、目标船速(u)和回转率(ROT)命令，并携带 stamp 与 rationale。

// VISUAL
A 2D curve plot showing multiple trajectory candidates (wavy lines) diverging from a ship's current position to bypass an obstacle vessel. The chosen trajectory is highlighted with a solid dark line, while others are gray and dashed. Annotations of target parameters.

// LAYOUT
Layout: split-screen
Left side: Mathematical and logical synergy between IvP arbitration and MPC path planning. Right side: Trajectory planning grid showing how Candidates are generated and pruned.

---

## Slide 12 of 20

**Type**: Content
**Filename**: 12-slide-decision-algorithms.png

// NARRATIVE GOAL
深度剖析避碰决策背后的核心算法机理：IvP多目标优化与BC-MPC动力学规划。

// KEY CONTENT
Headline: [多目标行为仲裁与规划算法]
Sub-headline: 支撑智能避碰决策的底层数学机制与水动力适配
Body:
- IvP 目标函数叠加: 叠加“效率函数”（趋向主航线）与“安全函数”（远离障碍），多维度加权求解。
- BC-MPC 状态预测: 引入Nomoto响应模型或三自由度MMG动力学模型，预测未来30s至3min内的船舶状态。
- 水动力插件解耦: 将决策算法与具体船型参数解耦，支持通过Capability Manifest适配多类船舶。
- 防振荡机制: 对航向改变指令引入死区控制与平滑门限，避免多船会遇时决策反复振荡。

// VISUAL
A technical layout split into two boxes: The left shows an mathematical mesh (IvP objective space) with peak points indicating optimal solution regions. The right shows the MMG hydrodynamic model equation schematics with forces labeled.

// LAYOUT
Layout: split-screen
Left: Graph of IvP multivariable grid. Right: C++ algorithm design schema for the BC-MPC controller.

---

## Slide 13 of 20

**Type**: Content
**Filename**: 13-slide-step3-hmi.png

// NARRATIVE GOAL
介绍步骤三的前端交互设计：HMI如何可视化渲染IvP目标函数空间与避碰预选航线。

// KEY CONTENT
Headline: [决策空间与备选轨迹可视化]
Sub-headline: 规划运行面板设计：展示决策求解的多方案博弈
Body:
- 备选路径网络: ENC海图上用浅色半透明线条绘制M5生成的13条避碰备选弧线。
- 推荐航路渲染: 最终选中的避碰路径用醒目的实线绘制，并附带未来预估航迹带。
- 决策热力图: 可视化呈现IvP优选热力分布，直观展示当前航向选择是安全性与经济性的最佳平衡。
- 动态避碰指示器: 在电子海图罗盘上用扇区热力色块，指示安全的改变航向区间。

// VISUAL
A beautiful UI layout depicting the HMI map. From the ship icon, a fan-like spread of potential tracks is displayed. The central track is green (selected). Faint heat-map colored grids overlay the map representing the risk cost.

// LAYOUT
Layout: split-screen
Left side: HMI screen UI components description. Right side: Dashboard mockup with ENC mapping showing alternative trajectory fan lines.

---

## Slide 14 of 20

**Type**: Content
**Filename**: 14-slide-step4-safety.png

// NARRATIVE GOAL
展示避碰流程第四步：安全监督与兜底。介绍M7如何作为独立的Checker对系统进行实时验证。

// KEY CONTENT
Headline: [步骤四：安全监督与兜底]
Sub-headline: M7 Safety Supervisor 的 Checker 角色与独立防碰撞机制
Body:
- 独立于Doer链路: M7 拥有独立的ROS包、独立的生命周期管理以及独立的运行路径，逻辑极简化。
- 关键假设监控: 实时监控系统关键假设（如“来船AIS数据可信”、“我船舵角响应正常”）。
- SOTIF/功能安全防线: 依据ISO 21448与IEC 61508，检测Doer是否出现超越包络、算法停转或不可行路径。
- Veto接管权限: 当评估防碰撞时延逼近红线且Doer无响应时，M7直接触发接管并执行最小风险操作(MRC)。

// VISUAL
A large circular lock shield icon with a technical checkmark inside. Connecting lines flow from sensors directly to the shield, then bypass the main controller to go directly to the rudder actuator schema, indicating the veto takeover channel.

// LAYOUT
Layout: split-screen
Left side: Logic rules and safety standard compliance of the Checker. Right side: Takeover logic flowchart illustrating the bypass control scheme.

---

## Slide 15 of 20

**Type**: Content
**Filename**: 15-slide-step4-hmi.png

// NARRATIVE GOAL
介绍步骤四的前端交互设计：HMI在发生安全警报或Checker接管时，如何向船员提供交互以进行应急处理。

// KEY CONTENT
Headline: [安全警报与接管交互设计]
Sub-headline: 安全检查面板设计：确保人机共驾的最终安全防线
Body:
- 异构状态显式指示: 用独立的UI色板高亮标识“Doer健康状态”与“Checker监控状态”。
- 强声光接管弹窗: Checker介入接管时，屏幕中央弹出红色卡片，显示“Checker已执行接管，进入安全兜底模式”。
- 故障信息透明: 详细列出导致接管的具体原因，例如“M5路径计算超时”或“来船侵入核心安全圈”。
- 一键紧急切回: 提供物理级显眼的人工一键接管/恢复正常航行交互按钮，保障人在回路(HITL)安全。

// VISUAL
A dramatic high-contrast alert dashboard mockup. The center of the screen is overlaid with a large red alarm popup box with a warning triangle icon, displaying the takeover log: "TIMEOUT: M5 PLANNER". A large black button reads "CONFIRM EMERGENCY HANDOVER".

// LAYOUT
Layout: split-screen
Left side: UI alerts and takeover layout description. Right side: Visual UI mockup showing the high-priority takeover dialog box.

---

## Slide 16 of 20

**Type**: Content
**Filename**: 16-slide-step5-return.png

// NARRATIVE GOAL
展示避碰流程第五步：规避完成与回归航路。后端算法如何判断规避完成，并实现平滑过渡。

// KEY CONTENT
Headline: [步骤五：规避完成与回归航路]
Sub-headline: 危险状态解除判定与平滑并线规划
Body:
- 规避结束判定: M2判定我船已跨过障碍船CPA点，两船距离逐渐增大且风险值降为零。
- 重规划触发: M3 任务管理器接收到安全解除信号，下发指令要求回归主航次路径。
- 回归路径平滑: M5 战术规划器以当前状态为起点，计算一条平滑的并线曲线，回归原始航路。
- 稳定并入航线: 抑制回归过程中的超调与舵角摆动，平稳融入直航跟踪算法。

// VISUAL
A trajectory chart showing a ship bypassing a red circle representing an obstacle. The route curves back toward a green dashed line (original route) and aligns with it. Small arrows along the curve show heading adjustments.

// LAYOUT
Layout: split-screen
Left side: Logic for risk clearance, re-planning triggering, and smooth line-in tracking. Right side: Nautical chart plot showing the return-to-route path.

---

## Slide 17 of 20

**Type**: Content
**Filename**: 17-slide-step5-hmi.png

// NARRATIVE GOAL
介绍步骤五的前端交互设计：HMI如何反馈避碰结束、回归原航路的状态给操作员。

// KEY CONTENT
Headline: [回归航线状态与状态指示]
Sub-headline: 仿真报告与航行评价面板设计：完成闭环业务流程
Body:
- 避碰结束通告: 浮动警告窗自动淡出，变为绿色的“避碰完成 / Collision Avoidance Completed”常态通知。
- 引导路径闪烁: 在海图上，回归段的新路径线会进行短暂的虚线闪烁提示，提示航道已更新。
- 自动化过程复盘: 自动呼出评估模块，在浮窗中列出本次避碰的评价指标（最大侵入深度、最小CPA）。
- 数据包自动打包: 触发M8增发SOTIF metrics，准备向后台导出本次避碰过程的仿真与运行评估记录。

// VISUAL
HMI UI mockup showing a post-flight summary card. It displays a checklist with checkmarks for "CPA Cleared", "COLREGs Compliant", and "Returned to Track", alongside statistical performance graphs of cross-track error.

// LAYOUT
Layout: split-screen
Left side: Description of UI feedback for return-to-route and reporting. Right side: Mockup of the post-incident assessment report screen on the HMI.

---

## Slide 18 of 20

**Type**: Content
**Filename**: 18-slide-sil-simulator.png

// NARRATIVE GOAL
详细介绍TDL项目目前建设的SIL（软件在环）仿真系统架构与容器化构建。

// KEY CONTENT
Headline: [SIL软件在环仿真系统设计]
Sub-headline: 基于 ROS2 Humble 容器与 FastAPI 编排的仿真试验平台
Body:
- 统一运行环境: 采用 ROS2 Humble + Ubuntu 22.04 + PREEMPT_RT 保证测试目标与真实部署一致。
- FastAPI 协同编排: 基于 FastAPI REST 与 rclpy 建立 orchestrator 节点，实现测试用例一键分发。
- 模块拓扑拟真: 仿真环境中运行真实的 L3 决策算法，障碍船由 AIS 或 scenario_authoring 驱动生成。
- 统一生命周期管理: sil_lifecycle 节点集中控制整个仿真系统中 9 个核心节点的加载、运行与重置。

// VISUAL
A multi-layered Docker container stack schema. The base layer is Ubuntu OS, middle layer contains ROS2 Humble and libcosim FMI bridge, top layer contains the orchestrator API and 9 SIL logic nodes. Surrounding arrows show config files.

// LAYOUT
Layout: split-screen
Left: System stack layers description of the SIL Docker runtime. Right: Technical flowchart showing orchestrator loading and lifecycle state transitions.

---

## Slide 19 of 20

**Type**: Content
**Filename**: 19-slide-sil-importance.png

// NARRATIVE GOAL
强调SIL系统在项目开发中的关键支撑作用，以及在多船型动力学仿真和CCS/DNV适航认证中的价值。

// KEY CONTENT
Headline: [SIL仿真系统对研发与认证的支撑]
Sub-headline: 集成 FMI 2.0 动力学插件与适航验证证据链
Body:
- FMI 2.0 水动力集成: 采用 libcosim 加载 MMG 动力学 FMU 插件，支持无缝更换本船船型仿真。
- 自动化场景测试: 支持 1000 个场景自动回归测试与 TCL-3 级别通过判定，自动导出证据包。
- 适航认证追溯: 对标 DNV-RP-0513 规范，导出 traceable-matrix.csv 证明每条危险场景都已闭环通过。
- 降级与兜底检验: 可注入各类虚拟故障（如“传感器延迟”、“舵机卡死”），检验 M7 Checker 模块的动作。

// VISUAL
An certificate envelope icon overlapping with a data sheet layout showing a table with columns: Scenario ID, Rule ID, Test Result (PASS). An official DNV or CCS stamp concept is lightly overlaid.

// LAYOUT
Layout: split-screen
Left side: Explanation of FMI/OSP tools, automation, and compliance certification support. Right side: 2D diagram representing the traceability matrix verification pipeline.

---

## Slide 20 of 20

**Type**: Content
**Filename**: 20-slide-conclusion-next.png

// NARRATIVE GOAL
总结本汇报，列出后续开发对SIL系统的强依赖和冲刺计划，为项目组内部讨论提供明确行动方向。

// KEY CONTENT
Headline: [总结与后续开发依赖]
Sub-headline: 冲刺 DEMO-2 与 DEMO-3，SIL 仿真系统的核心依托
Body:
- 算法开发强依赖: M4 IvP arbiter 与 M5 BC-MPC 实装均面临工期缺口，开发与验证完全依托 SIL 回归。
- DEMO-2 P0冲刺目标 (7/31): 50个综合会遇场景测试、IvP 求解空间与 BC-MPC 多轨迹双端可视化打通。
- DEMO-3 目标 (8/31): 1000个场景 8 小时无故障集成测试、Safety Checker 独立接管实测。
- 行动指令: 7/13 必须开工 M4/M5 后端重组，同步在 SIL 框架内启动 maritime-schema 场景迁移。

// VISUAL
A roadmap milestone chart indicating: 6/15 DEMO-1 Skeleton -> 7/31 DEMO-2 Decision-Capable (BC-MPC/IvP stub) -> 8/31 DEMO-3 Full-Stack -> 11月 CCS AIP Submission. Bullet details are marked on each flag.

// LAYOUT
Layout: winding-roadmap
A clean curved roadmap graphic stretching from bottom-left to top-right with flags marking key milestones, timeline dates, and primary dependencies.
