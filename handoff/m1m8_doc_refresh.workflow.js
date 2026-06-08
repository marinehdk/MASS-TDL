export const meta = {
  name: 'm1m8-doc-refresh',
  description: 'Rewrite M1-M8 spec.md (clean design target, flow/function/data, mermaid) + progress.md (status matrix + gap, audit-grounded), then build cross-module topic registry into 00-overview',
  phases: [
    { title: 'Modules', detail: '8 sonnet agents — each rewrites its module spec.md + progress.md' },
    { title: 'Overview', detail: '1 agent builds the cross-module topic registry + system data-flow into 00-overview.md' },
  ],
}

const ROOT = '/Users/marine/Code/MASS-L3-Tactical Layer'
const ARCH = 'docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md'
const AUDIT = 'docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md'

const LIVE_TOPICS = `/l1/voyage_task /l2/planned_route /l2/replan_response /l2/speed_profile /fusion/own_ship_state /fusion/tracked_targets /fusion/environment_state /l3/m1/odd_state /l3/m1/mode_cmd /l3/m1/tor_request /l3/m2/world_state /l3/m2/threat_state(声明在list但无源码publisher) /l3/m3/mission_goal /l3/m3/mission_state /l3/m3/route_replan_request /l3/m3/tor_request /l3/m4/behavior_plan /l3/m4/reactive_override_cmd /l3/m5/avoidance_plan /l3/m6/colregs_constraint /l3/m6/rule_assessment /l3/m7/heartbeat /l3/m7/safety_alert /l3/checker/veto /l3/m8/operator_state /l3/m8/ui_state /l3/m8/tor_request /l3/sat/data /l3/fsm_state /l3/colregs_active /l3/diagnostics /l3/safety/concern /l3/reflex/activation /l3/override/active /l4/tracking_error /sil/actuator_cmd /sil/own_ship_state /sil/environment /sil/bridge_state /sil/module_pulse /sil/sat2_data /sil/sat3_data /sil/m8_ui_state /sil/sotif_metrics(由M8 bridge但不在live) /sil/radar_meas /sil/ais_msg /sil/asdr_event /sil/fault/*`

function commonRules() {
  return `你是 MASS-L3 战术层的**技术文档工程师 + 代码核对员**。仓库根：${ROOT}（路径含空格）。

铁律：
1. 只写**指定的文档文件**（.md）。**严禁改任何代码 / 其它模块的文档 / 运行任何 build**。用 codegraph 和 Read 核对代码，不改代码。
2. 用 codegraph 核对当前实现（审计基线是 158bba9d，当前代码可能已变——以**当前代码 file:line** 为准）：先 ToolSearch query "select:mcp__codegraph__codegraph_explore,mcp__codegraph__codegraph_search"，再用 codegraph_explore。.msg/.md/.yaml 用 Read。
3. **拉黑** 任何 .salvage-*/ 与 archive/ 目录（陈旧备份，上一轮审计误报源）。
4. 中文撰写。术语/topic名/类型/file:line 原样。
5. **图文并茂**：用 mermaid 画图（数据流图、内部流水线、状态机），mermaid 用三反引号+mermaid 围栏。
6. **认证内容暂停**（用户指示）：**不写** CCS DMV-CG-0264 子功能映射、FMEDA 表、IEC 61508 SIL 认证审计章节。聚焦**系统流程 / 功能实现 / 数据交互**，面向开发+设计人员。模块身份行可保留一行 "SIL2/PATH-S" 标签（仅身份，不展开审计）。
7. 设计内容**依据架构报告** ${ARCH}（M1≈§5 … M8≈§12；用 Grep 在该文件里定位你模块的章节标题再读那一段，别全读 190KB）。`
}

const SPEC_TEMPLATE = `## spec.md 模板（= 权威设计目标，依据架构报告；剔除创可贴，描述应然的干净架构）

> 定位声明（开头放）：本文是 M{n} 的**权威设计目标**，依据架构报告 §{sec}。描述应然的系统流程/功能/数据交互；**不含 SIL bridge 等过渡创可贴**（那些是实现层的临时偏离，记在 progress）。当前实现现状与偏离见同目录 progress.md。

章节：
1. **模块身份** 表：模块代号 / 职责一句话 / 时间尺度(Hz) / SIL等级(仅标签) / 实现路径(PATH) / colcon包 / 架构报告章节 / 节点入口文件
2. **职责与边界**：(a) 本模块**拥有**的职责（bullet）(b) 明确**不负责**的（防越界，引用相关 ADR，如 ADR-4 决策核心零船型常量、M2 不做控制等）。**这里要体现"创可贴本应归属哪个模块"的正确归属**（如：避碰 latch/航向 clamp/回航 XTE 本应在 M4/M5/M6，不在 bridge）。
3. **接口契约（数据交互）**——调试断流的核心：
   3.1 上游订阅 表：topic | msg_type | 来源模块 | 频率 | 用途
   3.2 下游发布 表：topic | msg_type | 消费模块 | 频率 | 关键字段
   3.3 CMM 契约：每条出消息必带 stamp + schema_version + confidence∈[0,1] + rationale（列出本模块各出消息的这4字段语义）
   + 一张 mermaid 数据流图（本模块 IO：谁喂我→我→我喂谁）
4. **内部系统流程（功能实现）**：子能力分解 + 处理流水线；+ mermaid 流水线图；若有状态机（如 M1 ODD FSM、M3 mission FSM、M6 rule latch）画 mermaid stateDiagram
5. **关键算法 / 数据结构**（设计层面，依架构报告）
6. **降级路径**：DEGRADED / CRITICAL / OUT-of-ODD / 求解失败 时的应然行为
7. **顶层约束**：适用的 ADR-1/2/3/4 映射 + RFC/MUST 锁定项（如 RFC-001 M5 N=18）
8. **关联 D 任务**（指向 progress.md，不重复内容）
9. **修订** 表：追加一行 "2026-06-08 | 依架构报告+系统审计重写 spec（剔除创可贴，补全流程/接口/数据）"`

const PROGRESS_TEMPLATE = `## progress.md 模板（= 诚实实现现状 + Gap 矩阵；偏离/创可贴/MOCK 都记这里，附 file:line）

> 定位声明（开头放）：本文是 M{n} 的**实现现状**对照 spec.md 的设计目标。所有偏离/创可贴/MOCK 记录于此并附 file:line。审计基线 ${AUDIT}（本模块条目已并入下表，但以**当前代码**为准）。

章节：
1. **头部** 表：最近更新 / Currently Implementing / 当前分支 / 当前 LOC
2. **实现状态矩阵**（核心）表：设计职责(对应 spec §) | 状态 {REAL/PARTIAL/STUB/MOCK/MISSING} | 证据 file:line | 备注
3. **接口实现对照**（设计契约 vs 实际）表：topic | 设计(spec) | 实际 file:line | 字段填充(schema_version/confidence/rationale 是否填) | 状态{连通/断流/namespace错/字段空/mock拦截}
4. **已知缺陷**（按严重度 CRITICAL→LOW）表：严重度 | 缺陷 | file:line | 类型{断流/MOCK/脱节/创可贴/死代码}
5. **创可贴 / 越界逻辑**：本应在本模块、当前却在别处（多在 docker/sil_topic_bridge.py）的逻辑清单 + 目标归位。（从审计弹药 _bandaids.md 里挑与本模块相关的）
6. **设计-实现脱节（overclaim 修正）**：旧 progress/spec 声称 ✅ 但实际 X，逐条修正（附证据）
7. **D 任务联动表**：**保留**原有表 + 更新每行真实状态（把虚标 ✅ 的改成真实状态 + 备注偏差）
8. **DEMO 阻塞贡献**：更新真实阻塞
9. **修订** 表：追加 "2026-06-08 | 依系统审计重写 progress（状态矩阵+gap+创可贴+overclaim修正）"`

const MODULES = [
  { id: 'M1', name: 'ODD/Envelope Manager', sec: 5, dir: 'M1-ODD-Envelope-Manager', node: 'src/l3_tdl_kernel/m1_odd_envelope_manager/src/odd_envelope_manager_node.cpp', extra: 'M1=调度枢纽+唯一安全语境权威(ADR-1)。重点画 ODD FSM 状态机(In/Edge/Out/MrCPrep/MrCActive/Overridden) + E/T/H 三轴评分流水线 + ToR 自适应矩阵。注意现状：zone 冻结 ZONE_A、ModeCmd 多模式无人消费、tor_request 无消费者。' },
  { id: 'M2', name: 'World Model', sec: 6, dir: 'M2-World-Model', node: 'src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp', extra: 'M2=唯一权威世界视图。重点：CPA/TCPA 计算 + COLREG encounter 预分类 + 目标聚合 + EnvSanity。设计上 CPA/TCPA 应由 M2 算并发布(threat_state)，当前 bridge 在重算(创可贴)。' },
  { id: 'M3', name: 'Mission Manager', sec: 7, dir: 'M3-Mission-Manager', node: 'src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp', extra: 'M3=航次状态机+task_validity 4条件门+ETA+replan触发+回航。重点画 mission FSM + task_validity 门。现状：mission_state 不发、speed_recommend=0、ENC 校验是桩、回航实际在 bridge XTE(创可贴)。也读同目录 M3-gap-fix-plan.md。' },
  { id: 'M4', name: 'Behavior Arbiter', sec: 8, dir: 'M4-Behavior-Arbiter', node: 'src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp', extra: '忽略 .salvage-d3.1。M4=IvP多目标行为仲裁。重点：行为字典 + ODD-aware 激活 + IvP 加权求解 + 应消费 M6 primary_preferred_direction 决定转向。现状：无视 M6 方向硬编码右转、reactive_override_cmd 不发、若干行为(Restricted_Vis/Channel_Follow)死代码。' },
  { id: 'M5', name: 'Tactical Planner', sec: 9, dir: 'M5-Tactical-Planner', node: 'src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp + .../bc_mpc/bc_mpc_node.cpp', extra: 'M5=Mid-MPC(IPOPT/CasADi,N=18/90s)+BC-MPC(13候选/事件驱动)。⚠重要更新：上一会话已修 Mid-MPC J_colreg 重设计(Restoration_Failed 50→0；ROT约束改平滑lbx/ubx；J含真colreg+asym cost)——用 codegraph 读当前 mid_mpc_nlp_formulation.cpp 反映这个**已修**状态，别照搬审计片里"NLP always fails"的旧结论。仍成立的现状：BC-MPC整层未launch、waypoint CMM字段空、/m5/namespace脆、无ODD/veto gate、cost_colreg在rationale里仍报0(可观测性桩)。设计上 M5 直出 L4(无bridge)。' },
  { id: 'M6', name: 'COLREGs Reasoner', sec: 10, dir: 'M6-COLREGs-Reasoner', node: 'src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp', extra: 'M6=规则推理(Rule5-19,ODD-aware)+5层决策链+RuleLatch。重点：encounter→role/phase/direction/min-alteration 生成 + 5层链。现状(已修)：D5 conflict_detected role驱动、D6 RuleLatch已修。仍成立：rule_assessment 只为Rule14发、colregs_chain 不入SAT2、schema_version=0。注意 A4000 有未提交编辑——以当前工作树文件为准。' },
  { id: 'M7', name: 'Safety Supervisor', sec: 11, dir: 'M7-Safety-Supervisor', node: 'src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp', extra: '忽略 sotif/.salvage-d3.3b。M7=Doer-Checker(Checker角色,独立路径,ADR-2)。设计：6硬约束(HC)+同步veto硬门+SOTIF监测+MRM。重点画 Doer-Checker 关系 + HC 检查流 + veto 路径。⚠现状极差(认证级)：/l3/checker/veto 从不发布、6个HC全是死代码(run_hard_constraint_checks 空壳)、SOTIF永stub_mode、丢弃M4 plan内容。这些是 progress 的 CRITICAL gap；spec 仍写应然的同步硬门设计。**认证/FMEDA章节不写**。' },
  { id: 'M8', name: 'HMI/Transparency Bridge', sec: 12, dir: 'M8-HMI-Transparency-Bridge', node: 'src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp', extra: 'M8=唯一对ROC/船长说话者。设计：聚合M1-M7的CMM triplet(current_state/rationale/forecast)→SAT-1/2/3→前端UIState+ToR协议。重点画 SAT 聚合数据流 + ToR 状态机 + 前后端(foxglove/REST)交互。现状：M8是sat2/sat3并行发布者非聚合者、UIState位置/航向恒0、operator_state不发、SAT聚合拿空、前端大量断流/schema不匹配/硬编码。含 C++节点 + python web_server 两半。' },
]

function modulePrompt(m) {
  return `${commonRules()}

你的范围：**仅 ${m.id} — ${m.name}**。改写下面 2 个文件（覆盖式重写，但保留 progress 的 D 任务联动表与修订历史）：
- 设计文档：docs/Design/TDL-Kernel/${m.dir}/${m.id}-spec.md
- 现状文档：docs/Design/TDL-Kernel/${m.dir}/${m.id}-progress.md

必读输入：
1. 当前 2 文件（保留 D 任务表 + 修订历史，其余可重构）
2. 架构报告 ${ARCH} —— Grep 定位 ${m.id}（约 §${m.sec}）章节再读那段，作为**设计目标**来源
3. 审计弹药（本模块）：handoff/audit_slices/${m.id}.md
4. 创可贴目录（挑与本模块相关的）：handoff/audit_slices/_bandaids.md
5. 系统审计全文（背景）：${AUDIT}
6. 实现入口：${m.node} —— 用 codegraph_explore 核对**当前**订阅/发布/职责实现（审计基线 158bba9d 可能已变）

模块要点：${m.extra}

${SPEC_TEMPLATE}

${PROGRESS_TEMPLATE}

写法要求：
- spec = 应然设计目标（剔除创可贴，描述干净架构）；progress = 现状 + gap（创可贴/mock/断流/overclaim 都在这，附**当前代码** file:line）。
- 设计上未明确的点用 [TBD-<原因>]（含原因），别编造设计。
- 至少 2 张 mermaid 图（spec 里：IO 数据流 + 内部流水线/状态机）。
- 完成后用 Write 工具落盘两个文件。

返回结构化摘要。`
}

const MODULE_SCHEMA = {
  type: 'object',
  required: ['module', 'spec_written', 'progress_written', 'summary'],
  properties: {
    module: { type: 'string' },
    spec_written: { type: 'boolean' },
    progress_written: { type: 'boolean' },
    mermaid_count: { type: 'number' },
    spec_sections: { type: 'array', items: { type: 'string' } },
    gaps_documented: { type: 'number' },
    key_design_points: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
    summary: { type: 'string' },
  },
}

const OVERVIEW_SCHEMA = {
  type: 'object',
  required: ['written', 'summary'],
  properties: {
    written: { type: 'boolean' },
    topic_count: { type: 'number' },
    broken_edges: { type: 'number' },
    mermaid_count: { type: 'number' },
    summary: { type: 'string' },
  },
}

// ---------- run ----------
phase('Modules')
const modResults = (await parallel(MODULES.map(m => () =>
  agent(modulePrompt(m), { model: 'sonnet', phase: 'Modules', label: `doc:${m.id}`, schema: MODULE_SCHEMA })
    .then(d => ({ id: m.id, data: d }))
    .catch(e => ({ id: m.id, data: { module: m.id, spec_written: false, progress_written: false, summary: 'agent failed: ' + ((e && e.message) ? e.message : String(e)) } }))
))).filter(Boolean)
log(`Phase 1 done: ${modResults.length}/${MODULES.length} module doc pairs`)

phase('Overview')
const ovPrompt = `${commonRules()}

你的范围：**仅** 更新 docs/Design/TDL-Kernel/00-tdl-kernel-overview.md —— 加入**跨模块接口契约总表（topic registry）**+ 系统数据流总图。**保留该文件已有内容**（D 任务索引/进度快照等），在合适位置**新增/整合**下述章节。

必读：
1. 当前 00-tdl-kernel-overview.md（保留原有内容）
2. 刚被本批 agent 重写的 8 个模块 spec.md 的「§3 接口契约」表：docs/Design/TDL-Kernel/M{1..8}-*/M{n}-spec.md（读各文件 §3）
3. 创可贴/契约审计：handoff/audit_slices/_bandaids.md（含 contracts 段：CMM 字段缺失、namespace 错位、threat_state 无发布者等）
4. 系统审计全文：${AUDIT}

要写的内容：
A. **系统数据流总图**（mermaid graph）：M1-M8 + 边界(L1/L2/Fusion/L4) + bridge(标注为"过渡创可贴层")的 topic 级数据流，颜色/注释标出断流边与 mock 拦截边。
B. **跨模块 Topic Registry 总表**：每个 topic 一行：topic | msg_type | 发布者 | 订阅者(们) | 频率 | 现状{🟢连通 / 🔴断流(无发布者/无订阅者) / 🟡namespace错位 / 🟠mock拦截 / ⚪字段未填充} | 备注(file:line 或审计引用)。覆盖所有 /l1 /l2 /fusion /l3/* /l4 /sil/* 关键 topic。
C. **断流速查**：把所有 🔴🟡🟠 行单独汇成一个"已知问题 topic"小表，链到系统审计 + 各模块 progress。

live topic 实测列表（A4000，作 ground-truth 核对）：
${LIVE_TOPICS}

mermaid 用三反引号+mermaid 围栏。完成后用 Write/Edit 落盘。返回结构化摘要。`

let ov = null
try {
  ov = await agent(ovPrompt, { model: 'sonnet', phase: 'Overview', label: 'doc:00-overview', schema: OVERVIEW_SCHEMA })
} catch (e) {
  ov = { written: false, summary: 'overview agent failed: ' + ((e && e.message) ? e.message : String(e)) }
}

return { modules: modResults, overview: ov, stats: { module_pairs: modResults.length } }
