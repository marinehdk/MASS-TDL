# 阶段 3 Findings 草稿

> 通读 v1.0 各章产出的 finding 暂存。阶段 6 综合时分类、去重、定稿到 05-audit-report.md。
> Finding ID 格式：F-{P0|P1|P2|P3}-D{1..9}-{seq:003}
> 证据引用阶段 2 证据库 R1–R8（见 03-evidence-base.md）。

## §2 核心架构决策（D1 主审）

- id: F-P0-D1-006
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.2, lines: 95 }
  what: >
    §2.2 引用 [R8]（Rødseth 2022）支持"海事 ODD 必须包含人机责任分配维度（H 轴）"，
    暗示 v1.0 三轴（E×T×H）来自 Rødseth 2022。但 R8 实际只定义二轴：O = S_ext × S_int
    + FM（Function Mapping）。H 轴（Human Availability）无权威来源，是 v1.0 无标注的内部扩展。
  why: >
    三轴 ODD 是 §3.2 + §5 M1 的基础；无来源的轴定义无法通过 CCS 审计（白盒可审计要求）。
    R3 研究三轮交叉确认：Rødseth 二轴（PMC 全文）、SAE/ISO 属性树（无轴）、DNV 三维是认证空间（非 ODD）。
  evidence:
    internal: ["§2.2 （'Rødseth...明确指出'[R8]）", "§3.2 三轴定义（引用 §2.2 决策）"]
    external: ["R3 ODD 工程实践（Rødseth 2022 = 二轴 PMC 全文确认）", "R6（IMO MASS Code 目标型，无轴线）", "R7（DNV AROS 三维认证空间，非 ODD 定义）"]
  confidence: 🟢 High（三轮研究一致；R8 Phase 1 预扫已标 Phase 2 反证议题）
  severity: P0
  recommended_fix:
    type: 修订引用 + 框架
    target: §2.2 决策理由段 + §3.2
    proposed_text: >
      将"Rødseth 等（2022）明确指出...三轴 ODD"改为：
      "Rødseth 等（2022）定义二轴 OE：O = S_ext × S_int + FM [R8]。
      人机任务分配（FM）通过 Function Mapping 而非独立 H 轴表达。
      本设计在 Rødseth 框架上扩展，将 FM 具体化为 D1–D4 四级自主度（见 §3.2），
      参考 IMO MASS Code D1–D4 定义 [R2]。"

- id: F-P0-D1-008
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.5, lines: 126-128 }
  what: >
    §2.5 引用 [R12]（Jackson et al. 2021 Certified Control）支持"Checker 的验证逻辑须比
    Doer 简单 100 倍以上"。但 R12 原文（Phase 1 预扫确认）无此量化值，仅称 Monitor
    须"smaller and formally verifiable"。"100×"无文献依据。
  why: >
    100× 是整个决策四、§11 设计、以及 CLAUDE.md §4 的核心约束来源。若无依据，CCS 审计
    时该约束无法被验证，可能被质疑为自设要求。R4 研究确认：工业实践 50:1~100:1，
    但这是设计目标，非规范强制。
  evidence:
    internal: ["§2.5（'[R12]...100 倍以上'）"]
    external: ["R4 Doer-Checker 落地（100× 无 IEC 61508/DO-178C 规范依据；工业实践约 50:1~100:1）", "Phase 1 R12 预扫（'无100×量化'）"]
  confidence: 🟢 High（两轮独立确认；R12 Phase 1 预扫结论明确）
  severity: P0
  recommended_fix:
    type: 修改措辞
    target: §2.5 决策理由段 + CLAUDE.md §4
    proposed_text: >
      将"简单 100 倍以上"改为：
      "Checker 的代码规模须显著小于 Doer（工业实践目标：SLOC 比例 50:1~100:1，
      如 Boeing 777 Monitor ~50:1、Airbus FBW COM/MON >100:1）[R4]，以支持
      DO-178C Level A / IEC 61508 SIL 2 形式化验证。此为设计目标，非规范强制值。"

- id: F-P1-D1-007
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.2, lines: 95 }
  what: >
    §2.2 称"IMO MASS Code 草案（MSC 110，2025）在第 15 章明确要求：系统必须能识别船舶
    是否处于 Operational Envelope 之外" [R2]。但 R6 研究（nlm-ask 查询 maritime_regulations）
    确认：MSC 草案 Part 3 第 15 章是"维护与维修"（Maintenance & Repair），
    ODD/OE 要求在 Part 2 Chapter 1（运行环境）。章节引用错误。
  why: >
    错误章节引用会导致 CCS/DNV 审查员查不到原始依据，产生可信度损失。
  evidence:
    internal: ["§2.2（'第 15 章明确要求...'）"]
    external: ["R6 IMO MASS Code（Part 2 Ch.1 = ODD；Part 3 Ch.15 = 维护与维修）"]
  confidence: 🟢 High（R6 查询结果明确）
  severity: P1
  recommended_fix:
    type: 修正章节引用
    target: §2.2
    proposed_text: >
      将"在第 15 章明确要求"改为"在 Part 2 Chapter 1（运行环境/运行包络 OE）明确要求"。

- id: F-P1-D1-009
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.3, lines: 116 }
  what: >
    (a) §2.3 引用"DMV-CG-0264（2018）" — 应为 DNV-CG-0264（2025.01 现行版），存在文字笔误 + 版本过时。
    (b) §2.3 弃用方案对比表仅列"候选 A（7 模块）"和"候选 B（4+1 模块）"，未与 v3.0
    Kongsberg 5 模块（Target Tracker / COLREGs Engine / Avoidance Planner / Risk Monitor /
    Capability Assessment）对比——而后者正是 v3.0 基线中 L3 的工业实现方案。
  why: >
    (a) 版本错误损害可信度；(b) 缺失与最直接工业对标的对比，D1 替代方案分析不完整。
  evidence:
    internal: ["§2.3（'DMV-CG-0264（2018）'）", "CLAUDE.md §2（v3.0 Kongsberg 5 组件）"]
    external: ["R1 工业 COLAV（Kongsberg 系统架构）", "R7（DNV-CG-0264 2025.01 版）"]
  confidence: 🟢 High（文字笔误显而易见；Kongsberg 缺失基于 CLAUDE.md §2 确认）
  severity: P1
  recommended_fix:
    type: 修正引用 + 补充对比行
    target: §2.3 决策理由 + 对比表
    proposed_text: >
      (a) 将"DMV-CG-0264（2018）"改为"DNV-CG-0264（2025.01）"。
      (b) 对比表增加一行："候选 C（Kongsberg K-MATE / v3.0 基线 5 组件）"，
      并在该行说明弃用理由：5 组件模型将 ODD 隐式分布在各模块，不满足决策一（ODD 唯一权威）。

- id: F-P1-D1-010
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.4, lines: 122 }
  what: >
    §2.4 断言"SAT-1+2+3 全部可见时，操作员的态势感知提升且工作负荷不增加"[R11]（Chen ARL-TR-7180）。
    但 R5 研究（USAARL 2026-02 + NTNU 2024）发现"透明度悖论"：全展示（高 SAT-2 推理层）
    反而升高认知负荷、降低多任务效率；最优策略是"自适应按需触发"而非"全时全层展示"。
  why: >
    若 M8 按"全部可见"设计，实际会损害 ROC 接管绩效；需在设计中明确"自适应触发"条件。
  evidence:
    internal: ["§2.4（'全部可见时...工作负荷不增加'）"]
    external: ["R5 SAT 实证（USAARL 2026-02 透明度悖论；NTNU 2024 Layer 偏好 = Layer 3 而非全层）"]
  confidence: 🟢 High（R5 多源一致）
  severity: P1
  recommended_fix:
    type: 修订设计原则
    target: §2.4 决策理由段 + §12 M8 设计
    proposed_text: >
      将"全部可见时工作负荷不增加"改为：
      "SAT-3（预测/投影）是操作员最高优先级需求；SAT-2（推理过程）按需触发
      （冲突检测时、系统信心下降时、操作员显式请求时），不默认全时展示
      [R5，Veitch 2024 + USAARL 2026-02]。M8 采用自适应分级展示策略。"

- id: F-P2-D1-011
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §2.2, lines: 95 }
  what: >
    §2.2 将 TMR（Maximum Operator Response Time）和 TDL（Automation Response Deadline）
    的"量化参数"归因于 [R8]（Rødseth 2022）。但 R5 研究中 TMR≥60s 基线来源是
    Veitch 2024 [R4]，而非 Rødseth 2022。Rødseth 2022 引入了 TMR/TDL 概念，
    但 60s 量化值的实证依据是 Veitch。
  why: >
    引用源错位会导致审查时找不到量化值的来源，降低可信度。
  evidence:
    internal: ["§2.2（'[R8]...两个量化参数'）"]
    external: ["R5（Veitch 2024 = TMR 60s 基线实证来源；Rødseth 2022 = 概念框架）"]
  confidence: 🟡 Medium（Rødseth 2022 可能也提到量化范围，需直接验证 §3 中 60s 出处）
  severity: P2
  recommended_fix:
    type: 补充引用
    target: §2.2
    proposed_text: >
      在 [R8] 后补充 [R4]：
      "Rødseth 等（2022）提出 TMR/TDL 概念框架 [R8]；Veitch 等（2024）实证确定
      TMR≥60s 设计基线 [R4]。"

---

## §3 ODD / Operational Envelope 框架（D6 降级路径）

- id: F-P0-D6-015
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §3.2, lines: 140-149 }
  what: >
    §3.2 明文称"本设计采纳 Rødseth（2022）的 Operational Envelope 扩展定义"，随即给出
    三轴公式 O = E × T × H。但 Rødseth 2022 实际定义是二轴（O = S_ext × S_int + FM），
    无 H 轴（Human Availability）。H 轴是 v1.0 无标注扩展。
    §3.6 Conformance_Score 公式（w_E + w_T + w_H）完全依赖三轴框架，若 H 轴来源无效，
    整个合规评分机制的引用链断裂。
  why: >
    三轴 ODD 框架贯穿 §3–§5 全部设计，是 M1 调度逻辑的基础。无权威来源的公理
    无法通过 CCS 白盒可审计要求（CLAUDE.md §5 合规约束）。
  evidence:
    internal: ["§3.2（'采纳 Rødseth（2022）...三维状态空间 E×T×H'）", "§3.6（Conformance_Score 三轴权重）"]
    external: ["R3（Rødseth 2022 = 二轴 PMC 全文；H 轴不存在）", "F-P0-D1-006（§2.2 同根问题）"]
  confidence: 🟢 High（R3 PMC 全文核验）
  severity: P0
  recommended_fix:
    type: 修订框架来源
    target: §3.2 + §3.6
    proposed_text: >
      §3.2 改为：
      "本设计基于 Rødseth（2022）二轴 OE 框架 [R8]：O = S_ext × S_int + FM，
      并将 FM（人机控制权分配）具体化为 H 轴（Human Availability），以适应
      IMO MASS Code D1–D4 四级自主度要求 [R2]。H 轴扩展是本设计选择，
      参考 NTNU 岸控实验室实践，非 Rødseth 2022 原文定义 [R5]。"
      §3.6 权重注解补充：H 轴权重 w_H=0.3 为 FCB 特定设计值，
      须在 HAZID 中以 Veitch 2024 TMR 实证校准。

- id: F-P1-D6-016
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §3.4, lines: 173 }
  what: >
    TDL 计算公式 `TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)` 中
    系数 0.6 无来源说明。这是安全关键计算——0.6 决定了 MRC 触发时机。
  why: >
    若 0.6 系数是工程经验值，需标注来源或依据（如 Veitch 2024 实验数据）；
    若是无依据假设，CCS/DNV 审查会要求 HAZID 验证。
  evidence:
    internal: ["§3.4 TDL 公式（0.6 系数无注释）"]
    external: ["R5（Veitch 2024 TMR 实证：可用时间 60s 效应最大）", "无直接 0.6 系数来源"]
  confidence: 🟢 High（公式中系数裸出，无任何引用标注）
  severity: P1
  recommended_fix:
    type: 补充来源或 HAZID 标注
    target: §3.4 TDL 公式
    proposed_text: >
      在 0.6 系数后加注：
      "[设计值：TCPA 的 60% 作为 TDL 留出 40% 余量用于操作员接管，
      基于 Veitch 2024 TMR≥60s 基线估算；须在 HAZID 中以实际船型校准]"

- id: F-P1-D6-017
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §3.3, lines: 155-160 }
  what: >
    §3.3 四个 ODD 子域的阈值数字（ODD-A: CPA ≥ 1.0nm，TCPA ≥ 12min；
    ODD-B: CPA ≥ 0.3nm，TCPA ≥ 4min 等）无任何引用来源。
    这些是安全关键参数，直接决定 MRC 触发和自主等级允许边界。
  why: >
    R2 研究已发现 v1.0 的具体阈值数字"很可能来自 Wang 2021 原文的特定船型参数
    或作者自行设定"；文献中 TCPA 阈值范围是 5–20 min（Frontiers 2021，MDPI Ships 2022），
    与 v1.0 的 12min/4min 存在差异。无来源的安全参数是 CCS 审查的阻断点。
  evidence:
    internal: ["§3.3 子域表格（无 [Rx] 引用）"]
    external: ["R2（TCPA 阈值文献范围 5–20 min；v1.0 具体数字无来源）", "R6（IMO MASS Code 无具体 CPA/TCPA 数字）"]
  confidence: 🟢 High（表格中无任何引用）
  severity: P1
  recommended_fix:
    type: 补充来源或标注待校准
    target: §3.3 子域表格
    proposed_text: >
      每行数字后加注 "[FCB 特定设计值，参考 Wang 2021 [R?] + TCPA 文献范围；须 HAZID 校正]"。
      或在表格下方统一注明："以上阈值为 FCB 船型初始设计值，基于 [待补充来源] 估算，
      须在项目级 HAZID 中以实际场景验证。"

- id: F-P2-D6-018
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §3.5, lines: 196 }
  what: >
    ODD 状态机中 EDGE 状态阈值"margin < 20%"无来源说明。
    Conformance_Score 阈值（>0.8 IN, 0.5–0.8 EDGE, <0.5 OUT）亦无来源。
  why: >
    EDGE 阈值影响预警时机。相比 P1 的 TDL 系数（直接影响 MRC 触发），
    EDGE 阈值误差影响相对较小（过早/过晚预警），但仍需校准来源。
  evidence:
    internal: ["§3.5 状态机（margin < 20%）", "§3.6（0.5/0.8 阈值无引用）"]
    external: ["R3（无对应 Conformance Score 文献）"]
  confidence: 🟡 Medium（阈值缺引用，但可视为初始设计参数待 HAZID 校正）
  severity: P2
  recommended_fix:
    type: 标注来源或 HAZID 待项
    target: §3.5–§3.6
    proposed_text: >
      在 §3.5 EDGE 条件后加 "[初始值 20% margin，待 HAZID 校正]"；
      在 §3.6 阈值表达式后加 "[初始建议值，待 FCB 场景 HAZID 校正]"。

---

## §6 M2 World Model（D4 接口契约）

- id: F-P1-D4-019
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §6 图6-1, lines: 447-448 }
  what: >
    图6-1 中 RADAR 和 CAMERA 输入标注为"(L1 感知层)"。v3.0 中 Multimodal Fusion 是
    独立感知子系统，不属于 L1–L5 主决策栈。L3 消费的是 Multimodal Fusion 的 Track 级输出，
    不直接接收 L1 原始传感器数据。"L1 感知层"是旧术语（CLAUDE.md §2 已注为审计待修补项）。
  why: >
    接口命名错误会导致 L3/Multimodal Fusion 两个团队的接口契约歧义，
    影响 Track 级消息格式约定。
  evidence:
    internal: ["§6 图6-1（'RADAR → L1 感知层'）", "CLAUDE.md §2 注（§6/§7 旧术语审计待修补）"]
    external: ["docs/Init From Zulip/MASS ADAS Multimodal Fusion（Fusion Pipeline → Track 级输出）"]
  confidence: 🟢 High（CLAUDE.md 明确标注为审计项）
  severity: P1
  recommended_fix:
    type: 修正术语
    target: §6 图6-1 + §6.4 凡出现"L1 感知层"处
    proposed_text: >
      将所有"(L1 感知层)"改为"(Multimodal Fusion)"；
      注释："M2 Dynamic View 输入为 Fusion Pipeline 输出的 Track 级目标（非原始传感器数据）"。

- id: F-P2-D4-020
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §6.3.1, lines: 471-479 }
  what: >
    §6.3.1 COLREG 几何预分类中 `bearing_i in [112.5°, 247.5°]` → OVERTAKING 的角度判断
    代表"目标在本船正横后方扇区（艉端±67.5°）"，即本船是被追越方（stand-on vessel）。
    但代码注释未说明 preliminary_role 的主语是目标还是本船，且角度范围涵盖了部分
    正横位（>90°/< 270°）的 CROSSING 场景，可能产生分类歧义。
  why: >
    预分类错误会使 M6 COLREGs Reasoner 收到错误的上游角色判断，影响避让义务分配。
    虽然 M6 做最终决策，但错误的 preliminary_role 可能引导 M6 搜索错误分支。
  evidence:
    internal: ["§6.3.1 预分类代码（bearing_i 角度范围）"]
    external: ["R2（COLREGs Rule 13：追越角度 > 22.5° abaft beam，即 bearing > 112.5° 或 < 247.5°，覆盖完整艉扇区）"]
  confidence: 🟡 Medium（几何逻辑可能正确但未注释，需与 M6 设计文档交叉验证）
  severity: P2
  recommended_fix:
    type: 增加注释 + 逻辑澄清
    target: §6.3.1
    proposed_text: >
      在角度判断前加注：
      "# 以下判断从 own ship 视角：target bearing 相对本船的位置 → 角色定义"
      并确认 CROSSING 场景在 OVERTAKING 扇区外正确处理（>6° heading_diff 且 bearing 不在艉扇区）。

## §5 M1 ODD/Envelope Manager（D6 + D1 决策一）

> §5.5 决策依据中 [R8] 三轴引用和 [R2] "第15章" 引用均为已知 P0/P1 问题（F-P0-D6-015 / F-P1-D1-007）的二次定位，不重复新建 finding。

- id: F-P2-D6-021
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §5.4, lines: 407 }
  what: >
    M1 `odd_state` 输出频率 1 Hz（§4.3 描述 M1 工作在 0.1–1 Hz）。
    在最恶劣场景（M1 工作在 0.1 Hz = 10s 更新间隔），从 EDGE 状态恶化至 OUT 状态
    可能经历最长 10s 的检测延迟。ODD_A 场景 TCPA 12 min 余量下 10s 延迟可忽略；
    但 ODD-C（港内）或 ODD-D（低能见度）下快速变化场景中是否充分？
  why: >
    若 M1 在 0.1 Hz 极端情况下丢失快速变化的 EDGE→OUT 转变，MRC 触发延迟。
  evidence:
    internal: ["§4.3（M1 频率 0.1–1 Hz）", "§5.4（odd_state 1 Hz）"]
    external: ["R5（Veitch 2024：20s vs 60s 接管时窗效应，10s 误差在 60s 基线下可接受但值得注明）"]
  confidence: 🟡 Medium（依赖具体 ODD 子域场景，非绝对问题）
  severity: P2
  recommended_fix:
    type: 补充说明
    target: §5.4 odd_state 行 + §4.3
    proposed_text: >
      §5.4 频率列补注："最低 1 Hz（M1 在 OUT/EDGE 转变时触发事件型更新，不等待下一周期）"；
      在 §4.3 M1 频率描述后加：
      "注：ODD 状态突变（EDGE→OUT）触发事件型 odd_state 发布，不受 1 Hz 周期限制。"

---

## §7 M3 Mission Manager / §8 M4 / §9 M6 / §10 M5

> **§8 M4 Behavior Arbiter**：通读未发现独立 P0–P3 finding。IvP 求解器选型最优性属 D9 评判议题，
> 留阶段 4 对象"M4 Behavior Arbiter"条目填写。§8 实现与 CLAUDE.md §3 M4 描述一致。

> **§9 M6 COLREGs Reasoner**：通读未发现独立 P0–P3 finding。[R17–R20] 引用链接验证留阶段 4，
> §9 设计与阶段 2 R2 研究结论（COLREGs Rule 8/13–17 覆盖）一致。

- id: F-P1-D1-022
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §7 图7-1, lines: 521 }
  what: >
    §7 图7-1 M3 输入来源标注为"战略层L2"。v3.0 中 L2 = 航路规划层（Voyage Planner），
    不是"战略层"。"战略层"是旧 v1.0 命名（同 §6 中"L1 感知层"性质相同）。
  why: >
    命名错误影响 M3 与 L2 Voyage Planner 的接口契约理解；与 CLAUDE.md §2 v3.0 命名冲突。
  evidence:
    internal: ["§7 图7-1（'战略层L2'）", "CLAUDE.md §2（L2 = 航路规划层 Voyage Planner）"]
    external: ["docs/Init From Zulip/MASS ADAS L2 Voyage Planner 航路规划层（接口参考）"]
  confidence: 🟢 High
  severity: P1
  recommended_fix:
    type: 修正术语
    target: §7 图7-1
    proposed_text: 将"战略层L2"改为"L2 航路规划层（Voyage Planner）"；输入格式注明"Route Exchange / RTZ 格式"保留。

- id: F-P1-D9-024
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §10.5 + §10.6, lines: 728-734 }
  what: >
    §10.5 引用 "[R21] Yasukawa & Sano（2024）— MMG 4-DOF 近岸修正"。
    阶段 1 预扫已将 R21 标注为"可能幻觉"（精确来源未确认）。
    §10.5 中"Hs > 1.5m 时须引入波浪扰动项（参照 Yasukawa & Sano 2024 近岸修正）"
    是高速 FCB 安全关键的水动力模型依据，若原文不存在，该修正无工程支撑。
  why: >
    R21 是高速 FCB 波浪扰动修正的唯一引用来源；若为幻觉，§10.5 的 FCB 修正
    无法通过 CCS 审查的白盒可审计要求。
  evidence:
    internal: ["§10.5（[R21]波浪扰动修正）", "§10.6（[R21]引用列表）"]
    external: ["Phase 1 预扫（R21 状态：可能幻觉）", "注：Yasukawa & Yoshimura 2015 [R7] 确实存在并有效"]
  confidence: 🟢 High（Phase 1 预扫明确标注）
  severity: P1
  recommended_fix:
    type: 替换或移除引用
    target: §10.5 + §10.6
    proposed_text: >
      如 R21 无法验证：(a) 用 Yasukawa & Yoshimura 2015 [R7] 的 4-DOF MMG
      原文中波浪修正章节替代；或 (b) 将"Yasukawa & Sano 2024" 标注为
      "[待验证：Phase 1 预扫标注为存疑，须查 JMSE/JMSTech 数据库确认]"。

> **§10.2 图10-1**："L2 执行层" 错误同 §4 图4-1（见 F-P1-D5-012）；§10 图10-1 为第二个定位点。

---

## §4 系统架构总览（D5 时间尺度）

- id: F-P1-D5-012
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §4 图4-1, lines: 281 }
  what: >
    图4-1 中 M5 Tactical Planner 输出指向"L2 执行层 / 航向/速度控制器"。
    但 v3.0 架构中 L2 = 航路规划层（Voyage Planner），M5 的下游应是 L4 引导层
    （Guidance Layer：LOS 跟踪、漂移补偿），L5 才是控制执行层。
    "L2 执行层"使用旧术语且层级错误。
  why: >
    接口指向错误会导致 L4 引导层设计团队与 L3 的输入约定不一致。
    CLAUDE.md §2 明确 L3 下游输出为"→ L4（Avoidance WP + speed adj；
    或 v1.0 设计中的 ψ_cmd/u_cmd/ROT）"。
  evidence:
    internal: ["§4 图4-1（'M5 → L2 执行层'）", "CLAUDE.md §2（L3 下游 = L4）"]
    external: ["docs/Init From Zulip/MASS ADAS L4 Guidance Layer 引导层（接口参考）"]
  confidence: 🟢 High（CLAUDE.md §2 明确记录）
  severity: P1
  recommended_fix:
    type: 修正层级标注
    target: §4 图4-1
    proposed_text: 将"L2 执行层 / 航向/速度控制器"改为"L4 引导层（Guidance Layer）"。

- id: F-P2-D5-013
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §4 全章, lines: 237-313 }
  what: >
    §4 架构图未展示 v3.0 Y-axis Emergency Reflex Arc（感知极近距离 → 旁路 L3/L4 → 直达 L5 <500ms）。
    当 Reflex Arc 激活时，L3 各模块（尤其 M1/M7）如何感知、如何避免与 Reflex Arc
    输出的执行层指令冲突，§4 无任何说明。
  why: >
    Reflex Arc 旁路时 L3 可能仍在生成 M5 轨迹指令，造成 L4/L5 收到两套冲突指令。
  evidence:
    internal: ["CLAUDE.md §2（Y-axis Reflex Arc）"]
    external: ["docs/Init From Zulip/（Reflex Arc 接口参考）"]
  confidence: 🟡 Medium（需读 Reflex Arc 接口文档确认是否有仲裁机制）
  severity: P2
  recommended_fix:
    type: 补充接口说明
    target: §4（新增 §4.5 与 Y-axis Reflex Arc 的接口）
    proposed_text: >
      说明 Reflex Arc 激活时 L3 的感知机制：M1 应订阅 Reflex Arc 激活信号，
      激活时暂停 M5 输出（或打低优先级标记），避免与 Reflex Arc 冲突。

---

## §11 M7 Safety Supervisor（D3 主审）

- id: F-P0-D3-001
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11.2 图11-1 注释, lines: 778 }
  what: >
    M7 ARBITRATOR 有权"直接向 M5 注入安全轨迹"——Checker 承担了部分 Doer 职能（轨迹生成），违反 Doer-Checker 职责边界。
  why: >
    顶层决策四规定 Checker 逻辑必须比 Doer 简单 100×；v3.0 X-axis Deterministic Checker 仅有 VETO 权（"回退 nearest compliant"）。
    M7 若动态生成轨迹，则需包含规划逻辑，无法满足简单性约束。Boeing 777 Monitor/Airbus COM MON 均为投票/VETO，
    不生成替代轨迹；NASA Auto-GCAS 是"简化预定义拉起机动"而非完整 MPC。
  evidence:
    internal: ["§11.2 图11-1 注释（'直接注入安全轨迹'）", "CLAUDE.md §4 决策四"]
    external: ["R4 Doer-Checker 落地（Boeing 777 ~50:1 SLOC；Monitor 不生成轨迹）", "R4 Simplex/Certified Control（HA 控制器为预定义安全集）"]
  confidence: 🟢 High（设计文字明确，原则冲突清晰）
  severity: P0
  recommended_fix:
    type: 约束注入行为
    target: §11.2 + §11（新增 §11.6 MRC 命令集）
    proposed_text: >
      将"直接注入安全轨迹"改为"触发预定义 MRM 命令集（M7 VETO 权）"。
      MRM 命令集在 §11.6 中枚举（如：减速至 4 kn 维持航向、停车抛锚点序列），
      由 M5 执行而非 M7 动态计算。M7 不持有任何规划逻辑。

- id: F-P0-D3-002
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11 全章, lines: 738-809 }
  what: >
    §11 全章未提及 v3.0 X-axis Deterministic Checker。M7（L3 内部 Checker）
    与 X-axis Checker（系统级跨层 Checker）的协调机制、MRC 触发优先级、VETO 冲突处理均未定义。
  why: >
    CLAUDE.md §2 明确 X-axis Checker 对 L3 有 VETO 权。若 M7 和 X-axis 同时触发 MRC，
    优先级未知；若 X-axis 已 VETO，M7 的仲裁器是否仍运行？两个 Checker 层级的设计缺失
    是架构完整性的 P0 缺口。
  evidence:
    internal: ["CLAUDE.md §2（v3.0 架构，X-axis VETO 权）", "docs/Init From Zulip/MASS ADAS Deterministic Checker（接口参考）"]
    external: ["R4（Doer-Checker 独立性：两级 Checker 需显式协调）"]
  confidence: 🟢 High（CLAUDE.md 明确记录 X-axis 的存在和权限）
  severity: P0
  recommended_fix:
    type: 补充架构描述
    target: §11（新增 §11.7 与 X-axis Checker 的协作关系）
    proposed_text: >
      明确分层：M7 为 L3 内部 IEC 61508 + SOTIF Checker；X-axis Deterministic Checker
      为系统级确定性 VETO 层（更高优先级）。M7 触发 MRC 请求送 M1；X-axis 触发时直接
      VETO L3 输出，M7 不干预。M7 与 X-axis 通过独立总线通信，不共享状态。

- id: F-P1-D3-003
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11.3 PERF 行, lines: 788 }
  what: >
    M7 SOTIF 轨道的 PERF 监控使用"CPA 趋势恶化"触发条件。CPA 计算需要目标位置/速度，
    若 M7 复用 M2（World Model）的 CPA 输出，则当 M2 失效时 M7 同步失效（单点故障）。
    若 M7 独立计算 CPA，则实现了与 M6 部分重叠的逻辑。两种情况均未在 §11 中明确。
  why: >
    顶层决策四要求 Checker"不共享代码/库/数据结构"；单点故障会使 Doer-Checker 双轨
    降为单轨。
  evidence:
    internal: ["§11.3 PERF 行（'CPA 趋势恶化'）", "CLAUDE.md §4 决策四"]
    external: ["R4（Certified Control 独立实现路径要求）"]
  confidence: 🟢 High（数据依赖明确，独立性要求明确）
  severity: P1
  recommended_fix:
    type: 明确数据源
    target: §11.3 PERF 行 + §11（接口契约）
    proposed_text: >
      选择 (a) M7 有独立最小化感知路径（直接读原始雷达/AIS，不经 M2），或
      (b) 将 CPA 监控改为 M2 独立模块的简化计算结果（M7 读 M2 的 CPA 但
      在接口契约中标注"M2 失效时 PERF 轨道降级为保守保守阈值"）。任择其一，
      在 §11.3 中明确。

- id: F-P1-D6-004
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11 全章, lines: 738-809 }
  what: >
    §11 未定义 M7 自身失效时的行为：M7 静默失效（Fail-Silent）？告警到 M8？强制触发 MRC？
    M7 心跳由谁监控？
  why: >
    IEC 61508 SIL 2 要求所有安全功能的失效模式须显式分析。M7 失效时 Doer 链（M1–M6）
    无 Checker 监督，是系统已知最高风险状态。
  evidence:
    internal: ["§11 全章（无 M7 自身降级说明）"]
    external: ["R4（IEC 61508 SIL 2：安全功能失效模式分析）", "R7（DNV-CG-0264 9.3 监控系统：Checker: 硬件健康检查）"]
  confidence: 🟢 High（缺失项明确）
  severity: P1
  recommended_fix:
    type: 补充降级说明
    target: §11（新增 §11.6 M7 自身降级行为）
    proposed_text: >
      M7 心跳由 X-axis Deterministic Checker（外部）或 M1（内部）监控。
      M7 失效模式：Fail-Safe（触发保守 MRM），不允许 Fail-Silent。
      M7 失效后系统降级到 D2（船上备援激活），禁止 D3/D4 运行。

- id: F-P1-D7-005
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11.4, lines: 798 }
  what: >
    §11.4 SIL 2 架构要求仅列"单通道 + 诊断覆盖 ≥ 90%"。IEC 61508-2 Table 3 对 HFT=0
    的 SIL 2 还要求 SFF（Safe Failure Fraction）≥ 60%（Type A）或 ≥ 70%（Type B）。
    文档未提 SFF，认证路径不完整。
  why: >
    CCS 入级审查（R7）会核查 IEC 61508 完整性；不完整描述可能导致审查不通过。
  evidence:
    internal: ["§11.4（SIL 2 分配表）"]
    external: ["R4（IEC 61508-2 Table 3 SIL 2 架构约束）", "R7（CCS 认证流程中的 SIL 核查）"]
  confidence: 🟢 High（标准要求明确）
  severity: P1
  recommended_fix:
    type: 补充 SFF 要求
    target: §11.4 SIL 分配表
    proposed_text: >
      在"架构要求"列补充"SFF ≥ 60%（Type A 元件）或采用 HFT=1 双通道
      冗余（推荐：关键安全路径 HFT=1，降低 SFF 要求至 ≥ 90%）"。

---

## §12 M8 HMI / Transparency Bridge

- id: F-P2-D1-025
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §12.5, lines: 882-885 }
  what: >
    §12.5 将 [R23] 标注为"Hareide et al.（NTNU Shore Control Lab，2022）"。
    但 §16 参考文献列表中 [R23] = Veitch, E., & Alsos, O.A. (2022) "From captain to
    button-presser"。论文标题（中英对照"从'船长'到'按钮操作员'"）与内容一致，
    仅文内作者标注错误（Hareide 是 NTNU 另一研究组）。
  why: >
    错误作者归因在 CCS/DNV 审查时无法通过文献溯源，损害可信度；且混淆两个独立研究组。
  evidence:
    internal: ["§12.5（'Hareide et al. [R23]'）", "§16 [R23]（Veitch & Alsos 2022）"]
    external: []
  confidence: 🟢 High（同文档内两处直接矛盾）
  severity: P2
  recommended_fix:
    type: 修正作者标注
    target: §12.5
    proposed_text: 将"Hareide et al."改为"Veitch & Alsos (2022)"，与 §16 [R23] 保持一致。

- id: F-P2-D1-026
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §12.2–§12.3, lines: 817-851 }
  what: >
    §12.2 定义 SAT-1/2/3 全层同时展示，§12.3 仅按操作员角色（ROC vs 船长）区分信息密度，
    未设计"按需自适应触发"策略。F-P1-D1-010 已确认透明度悖论（USAARL 2026-02 + NTNU 2024）：
    SAT-2 推理层全时展示反而增加认知负荷，最优策略是按需触发。§12 是 F-P1-D1-010
    建议修订的目标章节，但当前设计未实现该修订。
  why: >
    全层同时展示设计与实证依据冲突；高压接管场景下 ROC 认知超载风险。
    F-P1-D1-010 §2.4 根因 + §12 设计实现须同步修改。
  evidence:
    internal: ["§12.2（SAT-1/2/3 全层展示定义）", "§12.3（角色区分但无按需触发逻辑）", "F-P1-D1-010（§2.4 根因）"]
    external: ["R5（USAARL 2026-02：全展示增加认知负荷；NTNU 2024：SAT-3 优先级最高）"]
  confidence: 🟢 High（R5 多源一致；§12 设计无自适应触发逻辑）
  severity: P2
  recommended_fix:
    type: 补充自适应触发设计
    target: §12.2–§12.3
    proposed_text: >
      §12.2 新增触发条件：SAT-1 默认全时展示；SAT-2 在{COLREGs 冲突检测 / M7 警告激活 /
      操作员显式请求}时按需展示；SAT-3 在 TDL 压缩时自动优先推送。
      §12.3 各角色视图在信息密度基础上补充各层触发时机说明。

---

## §13 多船型参数化设计

- id: F-P2-D4-027
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §13.5, lines: 964-966 }
  what: >
    §13.5 扩展路径表"长江内河船"行注明"仅 M6 规则库"需修改，暗示 M6 支持可切换规则集
    （COLREGs → 内水规则）。但 §9 M6 COLREGs Reasoner 全章未设计规则库插件架构，
    §13.2 Capability Manifest（B 层）亦无规则库路径字段。
    §13.5 的扩展承诺与 §9 当前设计不一致，低估实际扩展代价。
  why: >
    按 §13.5 承诺扩展内河版本时，实际需要 §9 M6 架构级修改（非仅参数配置），
    可能导致内河版本开发阶段的架构返工。
  evidence:
    internal: ["§13.5（'仅 M6 规则库'修改）", "§9 全章（无规则库插件接口）", "§13.2 Manifest（无 rules_lib_path 字段）"]
    external: ["R2（COLREGs 与内水规则框架差异显著）"]
  confidence: 🟢 High（§13.5 承诺 vs §9 设计缺口明确）
  severity: P2
  recommended_fix:
    type: 补充设计或修正预期
    target: §13.5 + §9 M6
    proposed_text: >
      选择 (a) 在 §9 补充 M6 规则库插件接口（rules_loader），Manifest 增加 rules_lib_path 字段；
      或 (b) §13.5"仅 M6 规则库"改为"M6 规则库 + 架构级修改（非纯参数化，需独立版本分支）"，
      如实反映扩展代价。

---

## §14 CCS 入级路径映射

> **延伸注记（F-P1-D1-009）**：§14.2 明文引用"DNV-CG-0264（2018）§4"，与 §2.3 的"DMV-CG-0264（2018）"
> 属同一版本错误（2018 → 2025.01）。修复 F-P1-D1-009 时须同步更新 §14.2。

- id: F-P1-D7-028
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §14.4, lines: 1014-1016 }
  what: >
    §14.4 关键证据文件清单中，ODD-Spec 描述为"三轴包络定义，CCS 审查基础"，将三轴 ODD（E×T×H）
    列为 CCS AIP 申请的认证核心依据。但 F-P0-D1-006 + F-P0-D6-015 已确认：H 轴无权威来源，
    三轴框架是 v1.0 无标注扩展。未经溯源的三轴框架作为"CCS 审查基础"，
    将导致 AIP 申请中 ODD-Spec 论证链在审查时断裂。
  why: >
    CCS AIP 审查会追溯 ODD-Spec 框架来源；P0 级文献缺失（F-P0-D1-006）未修复前提交 AIP，
    存在申请被退回风险。F-P0 根因通过 §14.4 传导至认证里程碑，须前置修复。
  evidence:
    internal: ["§14.4（ODD-Spec: '三轴包络定义，CCS 审查基础'）", "F-P0-D1-006（H 轴无来源）", "F-P0-D6-015（三轴公式无来源）"]
    external: ["R7（CCS AIP 流程：ODD-Spec 为核心审查文件，框架须可溯源）"]
  confidence: 🟢 High（P0 根因已确认，传导路径逻辑清晰）
  severity: P1
  recommended_fix:
    type: 修正 ODD-Spec 描述 + 标注前置依赖
    target: §14.4
    proposed_text: >
      将"三轴包络定义，CCS 审查基础"改为：
      "OE 框架定义（基于 Rødseth 2022 二轴 + H 轴 IMO MASS Code D1–D4 扩展，见 §3.2 修订版），
      CCS 审查基础。[前置依赖：F-P0-D1-006 + F-P0-D6-015 修订完成后方可提交 AIP]"

---

## §15 接口契约总表

> **延伸注记（F-P0-D3-001）**：§15.2 接口矩阵明文列出
> "M7 → M5 | Emergency_CmdMsg | 事件（紧急）| 直接安全轨迹（绕过M4）"，
> 将 F-P0-D3-001（Checker 执行 Doer 职能）固化到接口契约层。
> F-P0-D3-001 修复（改为预定义 MRM 命令集索引）时须同步修正 §15.2 该行描述。

> **延伸注记（F-P1-D5-012）**：§15.2 接口矩阵
> "M5 → L2 | Trajectory_CmdMsg | 10 Hz" 与 §4 图4-1 是同一错误；
> 修复 F-P1-D5-012 时须同步将 §15.2 该行改为"M5 → L4"。

---

## §16 参考文献

> **延伸注记（F-P1-D9-024）**：§16 [R21] 条目含括注"（引用于 Patch-B §6.10 注记）"，
> 明示该引用来源于未集成主文档的补丁文件，而非正式章节文本流中引入。
> 强化 F-P1-D9-024 的可疑判断；[R21] 须独立查证（JMSE / JMSTech 2024 数据库）。

- id: F-P3-D1-029
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §16, lines: 1162-1165 }
  what: >
    §16 参考文献列表中两个条目通读 §1–§15 全章未见 [Rx] 引用，疑为孤立条目（Dead Reference）：
    (a) [R22] Neurohr et al. (2025) arXiv:2508.00543 — 未见于正文任何章节；
    (b) [R13] Albus (1991) NIST RCS — 未见于正文（§8 M4 层次控制相关但未标注 [R13]）。
  why: >
    孤立引用使审查员混淆证据链；若是误删引用，则存在论证缺口未被察觉。
  evidence:
    internal: ["§16（[R22] [R13] 列出）", "§1–§15 通读（未见任何 [R22]/[R13] 标注）"]
    external: []
  confidence: 🟡 Medium（基于通读结论，存在极小漏读可能）
  severity: P3
  recommended_fix:
    type: 补充正文引用或移除孤立条目
    target: §16 + 相关章节
    proposed_text: >
      (a) [R22] 若属 §11/§12 SOTIF·ROC 认证相关，在对应段落补 [R22] 标注；否则删除。
      (b) [R13] 若 §8 M4 借鉴 NIST RCS 层次控制，在 §8 相关段落补 [R13] 标注；否则删除。

---

## §1 背景与设计约束

- id: F-P2-D1-030
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §1.2, lines: 54-55 }
  what: >
    §1.2 设计边界表格"在范围外"列两行使用旧术语：
    (a) "全局航次规划（L2 战略层）" — v3.0 中 L2 = 航路规划层（Voyage Planner），非"战略层"；
    (b) "传感器融合内部算法（L1 感知层）" — v3.0 中感知融合为独立 Multimodal Fusion 子系统，非"L1"。
    与 F-P1-D4-019（§6）、F-P1-D1-022（§7）同一旧术语遗留（CLAUDE.md §2 注为审计待修补）。
  why: >
    §1.2 是文档门面章节；旧术语在此处出现在读者建立初始概念框架时就引入命名歧义。
  evidence:
    internal: ["§1.2（'L2 战略层'、'L1 感知层'）", "CLAUDE.md §2（§1.2 旧术语审计待修补项）"]
    external: []
  confidence: 🟢 High
  severity: P2
  recommended_fix:
    type: 修正术语
    target: §1.2 设计边界表格
    proposed_text: >
      (a) "全局航次规划（L2 战略层）"→"全局航次规划（L2 航路规划层，Voyage Planner）"
      (b) "传感器融合内部算法（L1 感知层）"→"传感器融合内部算法（Multimodal Fusion 子系统）"

---

## §15 接口契约 vs 实际跨层设计（D4 主审 — 阶段 5.2 新生 finding）

- id: F-P1-D4-031
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §15.2 + §6, lines: 1090-1108 + 422-508 }
  what: >
    v1.0 §15.2 期望 M2 输入 World_StateMsg @ 4 Hz 含目标 + 自身状态 + ENC 约束。
    实际 Multimodal Fusion 提供：
      - TrackedTargetArray @ 2 Hz（仅目标，无 CPA/TCPA）
      - FilteredOwnShipState @ 50 Hz（自身状态，独立话题）
      - EnvironmentState @ 0.2 Hz（含 ENC 约束）
    四点差异：(1) 频率 2 vs 4 Hz；(2) CPA/TCPA 实际由 M2 自行计算（v1.0 §6.3 假设但 §15 未明示）；
    (3) 自身状态/ENC 跨三话题；(4) 对地速度 vs 对水速度未指定。
  why: >
    M2 输出频率受限于源数据（2 Hz），影响 M5 决策刷新；多话题须时间对齐；坐标系不明可能影响 M5 操纵模型输入。
    认证审查（CCS i-Ship 白盒可审计）须 §15 明示 M2 内部数据聚合策略。
  evidence:
    internal: ["§15.2（M2 → 下游 4 Hz）", "§6.3（M2 内部 CPA/TCPA 计算需求）"]
    external: ["docs/Init From Zulip/MASS ADAS Multimodal Fusion/Multi_Sensor_Fusion §2.2", "Navigation_Filter §2.2", "Situational_Awareness §2.2"]
  confidence: 🟢 High（三个跨层文档实证）
  severity: P1
  recommended_fix:
    type: 补充 §15 IDL + M2 数据聚合说明
    target: §15.1 + §15.2 + §6.4
    proposed_text: >
      §15.1 World_StateMsg IDL 明确：
        - 频率：M2 内部以 2 Hz 输入聚合，4 Hz 输出（含 1 次插值/外推）
        - 字段：TrackedTarget[] + cpa_m / tcpa_s（M2 计算）+ FilteredOwnShipState 快照 + ZoneConstraint zone
        - 坐标系：目标速度=对地（sog/cog）；自身=对水（u/v）+ 海流估计
      §6.4 增加"M2 内部数据聚合"小节，明示三话题时间对齐策略。

- id: F-P1-D4-032
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §10 + §15.2, lines: 655-736 + 1100 }
  what: >
    v1.0 §10 + §15 描述 M5 输出 (ψ_cmd, u_cmd, ROT_cmd) @ 10 Hz 直送下游。
    实际 L4 设计（WOP_Module）订阅 L2 PlannedRoute + SpeedProfile，**自身用 LOS+WOP 生成 (ψ_cmd, u_cmd, ROT_cmd) @ 10 Hz → L5**。
    L4 不消费 M5 的轨迹三元组——M5 输出与 L4 输出**重叠 / 冲突**。
    扩展 F-P1-D5-012（"L2 应为 L4" 仅文字层；本 finding 是层级架构层）。
  why: >
    若 M5 与 L4 同频输出 (ψ, u, ROT)，L5 收到两套指令——架构闭环性破坏。
    认证审查会要求"指令唯一来源"；当前设计无法回答"避让模式下 L4 是否旁路自身 LOS 转发 M5 指令"。
  evidence:
    internal: ["§10.2 图 10-1（M5 → L2 错写）", "§15.2 矩阵（M5 → L2，错）", "F-P1-D5-012（文字层 finding）"]
    external: ["docs/Init From Zulip/MASS ADAS L4/WOP_Module §2 + §7.2"]
  confidence: 🟢 High（L4 doc 实证）
  severity: P1
  recommended_fix:
    type: 重新定义 M5 → L4 接口（与 L4 团队对齐）
    target: §10 + §15.2
    proposed_text: >
      方案 A（保留 v1.0）：M5 输出 (ψ, u, ROT) @ 10 Hz；L4 在避让模式下旁路自身 LOS、转发 M5 指令；
      L4 须支持模式切换（normal / avoidance bypass）。
      方案 B（与 v3.0 对齐）：M5 输出 Avoidance_WP + speed_adj（覆盖 L2 PlannedRoute）；L4 始终用 LOS+WOP 处理。
      建议方案 B（更符合 v3.0 Kongsberg 立场 + L4 现有设计）。须 v1.1 §10 + §15 重写 M5 输出 schema。

- id: F-P1-D4-033
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §15, lines: 1026-1108 }
  what: >
    v1.0 §15.2 接口矩阵完全无 ASDR 接口，但实际 ASDR 子系统订阅 L3 多模块（COLREGs Engine、
    Risk Monitor、Avoidance Planner、Checker、Target Tracker）@ 事件 + 2 Hz，
    JSON schema AiDecisionRecord（ASDR §5.2）。
    违反 v1.0 §15"接口契约总表"原则（任何系统交互应在 §15 显式定义）。
  why: >
    CCS i-Ship 白盒可审计要求接口契约完整；ASDR 是决策追溯证据载体（DNV-CG-0264 §9.8 警告设备依据），
    v1.0 §15 缺失会使 AIP 申请的 ASDR 接口验证缺乏文档基础。
  evidence:
    internal: ["§15.2 矩阵（无 ASDR 行）"]
    external: ["docs/Init From Zulip/MASS ADAS ASDR §3.2 + §5.2 + §13"]
  confidence: 🟢 High（ASDR doc 明示订阅 L3 多模块）
  severity: P1
  recommended_fix:
    type: 补充 §15 IDL + 矩阵
    target: §15.1 + §15.2
    proposed_text: |
      §15.1 IDL 增加：
        message ASDR_RecordMsg {
            timestamp   stamp;
            string      source_module;     // "COLREGs_Engine"|"Risk_Monitor"|...
            AiDecision  decision;
            string      decision_json;     // JSON 序列化（兼容 ASDR §5.2）
            bytes       signature;         // SHA-256 防篡改
        }
      §15.2 矩阵增加：M2/M4/M6/M7 → ASDR @ 事件 + 2 Hz。

- id: F-P1-D4-034
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §15, lines: 1026-1108 }
  what: >
    v1.0 §15.2 接口矩阵无 Reflex Arc（Y-axis Emergency Reflex Arc）协议。
    实际 Hardware Override Arbiter §3.1 P2 优先级通过 DDS 到 L5 旁路 L3/L4，§14.1 称 "<500 ms"，
    但触发阈值（CPA/距离/时间）未量化、感知输入源未明、L3 是否被通知未定义。
  why: >
    Reflex Arc 是系统层 Y 轴的安全关键路径；v1.0 §15 完全未定义会导致：
    (1) Reflex 触发时 M5 仍在输出，L5 收到两套指令；
    (2) M1 不知 Reflex 已激活，无法暂停 ODD 状态机；
    (3) 认证审查无法验证 <500 ms 时延承诺。
  evidence:
    internal: ["§15.2 矩阵（无 Reflex Arc 行）", "CLAUDE.md §2（Y-axis Emergency Reflex Arc）"]
    external: ["docs/Init From Zulip/MASS ADAS Deterministic Checker/Hardware_Override_Arbiter §3.1 + §14.1"]
  confidence: 🟢 High（实际 Arbiter doc 设计未量化已确认）
  severity: P1
  recommended_fix:
    type: 跨团队补 Reflex Arc spec + v1.0 §15 增条目
    target: §15.1 + §15.2 + 跨团队 spec 补充
    proposed_text: |
      §15.1 IDL 增加：
        message EmergencyCommand {        // Reflex Arc → L5
            timestamp   trigger_time;
            float32     cpa_at_trigger_m;
            float32     range_at_trigger_m;
            string      sensor_source;
            enum action; // STOP|REVERSE|HARD_TURN
            float32     confidence;
        }
        message ReflexActivationNotification {  // Reflex Arc → M1
            timestamp   activation_time;
            string      reason;
            bool        l3_freeze_required;
        }
      §15.2 矩阵增加 Reflex Arc → L5 / Reflex Arc → M1 两行。
      须跨团队补 Reflex Arc spec（量化触发阈值、感知输入源、时序）。

- id: F-P1-D4-035
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §15.2 + §7, lines: 1090-1108 + 510-543 }
  what: >
    v1.0 §15.2 矩阵无 L3 → L2 反向通道（route_replan_request）。
    早期裁决（`L3 实现框架裁决汇总.md` §B-01）已识别此缺口：
    M3 触发重规划时须能向 L2 发起重规划请求；M5 持续无法生成可行轨迹时升级请求 L2。
  why: >
    无反向通道则 L3 检测 ODD 越界 + 重规划触发 → 只能本地 MRC，无法请求 L2 重新生成 PlannedRoute。
    架构闭环性破坏；早期 spec 已明示但 v1.0 漏吸纳。
  evidence:
    internal: ["§15.2 矩阵（仅 L2 → L3 单向）", "§7 M3 重规划触发但无 L2 反向接口"]
    external: ["docs/Doc From Claude/L3 实现框架裁决汇总.md §B-01"]
  confidence: 🟢 High（早期 spec 已识别）
  severity: P1
  recommended_fix:
    type: 补充 §15 IDL + 矩阵
    target: §15.1 + §15.2
    proposed_text: |
      §15.1 IDL 增加：
        message RouteReplanRequest {
            timestamp   stamp;
            enum reason; // ODD_EXIT|MISSION_INFEASIBLE|MRC_REQUIRED|CONGESTION
            float32     deadline_s;
            string      context_summary;
            Position    current_position;
            Position[]  exclusion_zones;
        }
      §15.2 矩阵增加：M3 → L2 @ 事件，频率：每次重规划触发。

- id: F-P2-D3-036
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11.3, lines: 788 }
  what: >
    M7 SOTIF 假设违反检测（v1.0 §11.3）涵盖传感器/预测/通信，但**未涵盖 L3 Checker 否决率**。
    Checker 频繁否决 M5 轨迹是 COLREGs 推理可信度下降的明显信号。
    实际 Deterministic Checker doc §17 未定义"Checker → M7 否决事件通知"——M7 被蒙在鼓里。
    早期 TDCAL spec §3.4 Stage 4 也已识别"Emergency Channel 触发应包括 Checker-T 失败"。
  why: >
    Checker-T 失败是 SOTIF 关键信号；M7 不能被蒙在鼓里。
    F-P0-D3-002（M7↔Checker 协调缺失）的具体子项之一。
  evidence:
    internal: ["§11.3 假设违反清单（无 Checker 否决率项）"]
    external: ["docs/Init From Zulip/MASS ADAS Deterministic Checker/Deterministic_Checker §17", "docs/Doc From Claude/2026-04-28-l3-tdcal-architecture-design-spec.md §3.4"]
  confidence: 🟢 High（双源确认）
  severity: P2
  recommended_fix:
    type: 补充 §11.3 + 跨团队加 Checker 通知接口
    target: §11.3 + Checker doc §17
    proposed_text: |
      §11.3 假设违反清单增加：
        | L3 Checker 否决率 | 否决次数 / 100 周期 | > 20（即 20% 否决率） | M7 升级 SOTIF 告警："COLREGs 推理可信度下降" |
      跨团队 Checker doc §17 增加：
        message CheckerVetoNotification { ... } // Checker → M7 @ 事件

- id: F-P2-D6-037
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §11.2 + §15, lines: 760-780 + 1090-1108 }
  what: >
    Hardware Override Arbiter §7.1 接管时通知 L3 "override_active"，要求 L3"暂停所有 AI 控制输出"。
    但 v1.0 §11 + §15 未定义：M5 内部是否冻结（停止 MPC 求解）还是继续运行（仅输出旁路）？
    若 M5 继续运行：CPA 预测继续 → ASDR 记虚假 AI 预测；Risk Monitor 继续监控 → 触发虚假告警；
    M7 基于 M5 假设做 SOTIF 监控 → 错误决策。
  why: >
    人工接管时 L3 内部状态机未明示，导致接管 + 回切阶段 M5/M7 行为不可预测。
  evidence:
    internal: ["§11.2 M7 架构图（无人工接管状态）", "§15.2 矩阵（无 Override Arbiter → L3 通知行）"]
    external: ["docs/Init From Zulip/MASS ADAS Deterministic Checker/Hardware_Override_Arbiter §7.1 + §6.2"]
  confidence: 🟢 High（实际 Arbiter doc 明示需 L3 暂停）
  severity: P2
  recommended_fix:
    type: 补充 L3 接管行为规范
    target: §11（新增 §11.8 人工接管时 L3 行为）+ §15.2
    proposed_text: >
      §11.8（新增）：人工接管激活 → M5 冻结 MPC 求解（保持状态供回切）；M7 SOTIF 监控暂停；
      M2 World Model 继续刷新（用于 ROC 显示）；ASDR 记 override_event 起止时间，期间所有 L3 输出标 "overridden"。
      §15.2 矩阵增加 Hardware Override Arbiter → M1 通知行。

- id: F-P2-D4-038
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §7 + §15.2, lines: 510-543 + 1095 }
  what: >
    v1.0 §7 M3 上游仅称"L2 战略层"（已是 F-P1-D1-022 旧术语 finding），但实际 L1 Voyage Order 子系统
    输出 VoyageTask（含 departure/destination/eta_window/optimization_priority/exclusion_zones），
    通过 task_publisher 间接路径到 M3。v1.0 §7 完全未承认 L1 入口。
  why: >
    M3 实际须订阅两个上游（L1 VoyageTask 事件 + L2 PlannedRoute/SpeedProfile 周期），
    v1.0 仅定义 L2 入口会导致接口契约不闭包。
  evidence:
    internal: ["§7 图 7-1（仅'战略层 L2'）", "§15.2 矩阵（无 L1 → M3 行）"]
    external: ["docs/Init From Zulip/MASS ADAS L1/Voyage_Order §6.1 + §10.1"]
  confidence: 🟢 High
  severity: P2
  recommended_fix:
    type: 补充 §7 + §15.2 上游说明
    target: §7 图 7-1 + §15.2
    proposed_text: >
      §7：M3 上游 = L1 VoyageTask（事件，含任务级参数）+ L2 PlannedRoute + SpeedProfile（规划周期）。
      §15.2 矩阵增加 L1 Voyage Order → M3 @ 事件行。

- id: F-P2-D1-039
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: 全文档（无术语对照附录）, lines: N/A }
  what: >
    早期研究稿用 Stage 1（看）→ Stage 2（判）→ Stage 3（动）→ Stage 4（盯）框架。
    v1.0 完全删除 Stage 概念，改用模块名（M1–M8）。
    无 Stage→Module 对照表，新读者无法追溯早期"看/判/动/盯"框架与 v1.0 模块的对应关系。
    另：M5/M6 章节顺序（§9 = M6, §10 = M5）与模块编号顺序不一致，引发文档导航困惑。
  why: >
    术语漂移降低可读性；CCS 审查员若需复核早期研究证据链时无法追溯。
  evidence:
    internal: ["v1.0 全文档无 Stage 术语 + 无术语附录"]
    external: ["docs/Doc From Claude/MASS L3 Tactical Decision Layer (TDL) 功能架构深度研究报告.md §3", "docs/Doc From Claude/2026-04-28-l3-tdcal-architecture-design-spec.md（"看/判/动/盯"框架）"]
  confidence: 🟢 High
  severity: P2
  recommended_fix:
    type: v1.1 加术语对照附录
    target: v1.0（新增"附录 A 术语对照"）
    proposed_text: |
      附录 A 术语对照（Stage → Module）：
        Stage 1（看）→ M2 World Model
        Stage 2（判）→ M6 COLREGs Reasoner
        Stage 3（动）→ M5 Tactical Planner
        Stage 4（盯）→ M7 Safety Supervisor + M1 ODD Manager（并行）
      附录 B：Checker-D / Checker-T 与 M7 双轨（IEC 61508 / SOTIF）的继承关系。

- id: F-P2-D9-040
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §8 M4, lines: 545-597 }
  what: >
    早期 spec（`2026-04-28-l3-tdcal-architecture-design-spec.md` §3.2）定义"多目标冲突消解器"插件
    含 5 层冲突消解路径（冲突识别 → 规则链推理 → IvP 搜索 → CSP 约束求解 → 最近合规替代）。
    v1.0 M4 仅用 IvP 多目标优化，未保留 CSP / 图搜索路径。
  why: >
    密集场景（多船 + TSS + UKC）下 IvP 单一路径可能不足；早期 spec 已设计多层 fallback。
    P2 而非 P1 因 v1.0 在简单场景下 IvP 充分；密集场景须 HAZID 驱动是否补足。
  evidence:
    internal: ["§8 M4（仅 IvP）"]
    external: ["docs/Doc From Claude/2026-04-28-l3-tdcal-architecture-design-spec.md §3.2"]
  confidence: 🟢 High
  severity: P2
  recommended_fix:
    type: v1.1 / spec part 2 评估是否补 CSP fallback
    target: §8 M4 + 后续 spec
    proposed_text: >
      §8 决策依据中说明"v1.0 仅 IvP；密集场景 CSP / 图搜索 fallback 留 v1.1 / spec part 2 评估"。

- id: F-P2-D9-041
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §10 M5 MPC 约束, lines: 655-736 }
  what: >
    早期裁决（`L3 实现框架裁决汇总.md` §B-02）已识别 v1.0 缺 TSS（COLREGs Rule 10）多边形约束：
    M6 §9.x 含 Rule 10 条款，但 §10 MPC 约束未引入 TSS lane 的几何 polygon 约束。
  why: >
    通过 TSS 时 v1.0 无法形式化保证不偏离指定 lane；DNV-CG-0264 §4.7/§4.8 形式化验证可能不通过。
  evidence:
    internal: ["§9 M6（Rule 10 条款）", "§10 M5（MPC 约束未含 TSS polygon）"]
    external: ["docs/Doc From Claude/L3 实现框架裁决汇总.md §B-02"]
  confidence: 🟢 High
  severity: P2
  recommended_fix:
    type: §10 MPC 约束加 TSS polygon
    target: §10
    proposed_text: >
      §10 MPC 约束节增加："当 EnvironmentState.in_tss = true 时，MPC 状态约束加入 TSS lane polygon
      （来自 ENC），保证轨迹完全位于指定 lane 内。"

- id: F-P3-D9-042
  locator: { file: MASS_ADAS_L3_TDL_架构设计报告.md, section: §10.5 + §13.4, lines: 657-734 + 933-956 }
  what: >
    早期 spec（§3.3 + 行 257）建议 Vessel Dynamics Plugin 在 FCB 高速段（25–35 节）须支持
    在线 GP（Gaussian Process）或 SI（System Identification）参数辨识。
    v1.0 §13.4 水动力插件接口仅提供静态预测，无在线辨识流程。
  why: >
    FCB 高速段水动力强非线性，静态参数误差累积可能影响 MPC 预测精度；
    P3 而非 P2 因实船试航后 HAZID 校准也可补救。
  evidence:
    internal: ["§13.4 插件接口（仅 predict + getLimits）", "§10.5 FCB 修正（无在线辨识）"]
    external: ["docs/Doc From Claude/2026-04-28-l3-tdcal-architecture-design-spec.md §3.3"]
  confidence: 🟡 Medium（FCB 实船试航数据反馈才能确证必要性）
  severity: P3
  recommended_fix:
    type: 后续 spec part 2 / 实船 HAZID 后评估
    target: §13.4 + spec part 2
    proposed_text: >
      §13.4 决策依据中说明"v1.0 仅静态参数；高速 FCB 在线辨识留实船试航 HAZID 后评估"。
      若 HAZID 显示必要，spec part 2 增加 plugin 接口 update_params(samples) 方法。

---
