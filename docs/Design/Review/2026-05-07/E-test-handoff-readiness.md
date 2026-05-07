# Angle E 评审报告 — 测试策略 SIL→HIL→Sea-Trial Readiness

| 字段 | 值 |
|---|---|
| Reviewer Persona | 海事 V&V engineer + 试航工程师（DNV/CCS class testing 经验）|
| Scope | 8月开发计划 v2.0 Phase 2/3/4 + D1.3 / D2.5 / D3.6 / D3.7 / D4.1–D4.5；架构 v1.1.2 §14 CCS 证据；M6 详细设计 §9 测试策略 |
| 总体判断 | 🔴（SIL→HIL→实船 handoff gates 缺失 + 1000 场景 98% 通过率门槛在 8/19→8/31 两周窗口内不可承诺 + HAZID 回填后回归测试时间不足 + AIP 未到位即试航的 NO-GO 触发条件清单缺失）|
| 评审基线 | 架构 v1.1.2 / Plan v2.0 / 8 模块详细设计草稿 |
| 边界声明（区分 G）| 本报告**不**评审 SIL 系统本体（FCB MMG 仿真器精度 / RL 对抗生成器 / 场景版本化）——那些归 G。本报告聚焦：测试**策略 / 度量定义 / 阶段间 handoff gates / 试航准入** |

---

## 1. Executive Summary（≤ 200 字）

8 月计划 Phase 2/3/4 在**测试基础设施搭建**层面（D1.3 SIL / D3.6 1000+场景 / D3.7 全模块集成）颗粒度合理，但作为 **V&V 项目**存在 4 类系统性缺陷：

1. **SIL→HIL→实船 三阶段 handoff gates 未文档化**——DNV-CG-0264 明确要求"V&V 策略须先于测试启动审批通过"[Citation 1]，本计划无对应文档；
2. **D3.6 "1000+ 场景 ≥ 98% 通过率"在 8/19 HAZID 回填→8/31 收官的 12 日窗口内不可承诺**——HAZID 改 132 个参数后必须重跑 SIL 全集，2 人周资源做不到；
3. **覆盖率定义单一**——仅"Rule 全分支"而无 ODD-coverage × disturbance-coverage × SOTIF triggering condition 三轴乘积；DNV-CG-0264 要求 functional × performance × failure-response 三类覆盖；
4. **12 月实船试航 NO-GO 触发条件清单缺失**——AIP 11 月才提交、SIL 2 评估意见 11 月底才回，但 12 月已计划上船，无门槛即可"硬上"。

**5 项 P0 必须在 5 月开工前关闭。**

---

## 2. P0 阻断项

### P0-E1：SIL→HIL→实船 三阶段 entry/exit gates 未定义 🔴

- **Finding**：8月计划 Phase 3 → Phase 4 衔接处仅写"D3.7 完成 → D4.1 HIL 搭建 → D4.2 HIL 综合测试 → D4.5 实船试航"，但每个箭头**无 entry criteria + exit criteria 闭包条件**。具体：
  - D3.7 → D4.1：未定义"SIL pass 才允许进 HIL"的硬指标（仅有"SIL 8h 无崩溃 + §15.2 24 行 topic 端到端"，但无关键指标如：感知-决策延迟 P95、CPA 误差、MRC 误触发率）
  - D4.2 → D4.5：HIL "≥ 200h" 是孤立 KPI，未约束"哪类场景必须 HIL 实测过才能上船"
- **证据**：
  - `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md:484-489`（D3.7 DoD 仅 3 项 + L488 § 15.2 24 行端到端）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:546-547`（D4.2 仅"≥ 200h 无致命故障 + COLREGs HIL 实测"）
  - DNV-CG-0264：「Before testing begins, a comprehensive V&V strategy must be approved. This strategy must describe the reviews, test-stages, test-types, and test-environments... that the autonomous functions must pass before final acceptance」[NLM safety_verification, citation 3, query 2026-05-07]
- **影响**：CCS 验船师在 AIP 受理时会要求出示 "V&V Plan" / "Verification Strategy" 文档；缺失即不予受理。HIL 阶段若无 SIL exit criteria，可能在 HIL 上反复修缮 SIL 已应清零的 bug，浪费 HIL 时间。
- **置信度**：🟢（DNV-CG-0264 §V&V 直接要求 + Phase 4 计划文本可逐字检索，无文档可指）
- **整改建议**：5/15 前由架构师 + V&V 负责人共同产出 `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`，至少包含：
  - SIL→HIL gate：(a) §15.2 24-row 全通；(b) 1000-scenario 通过率 ≥ 98%；(c) Mid-MPC 求解 P95 < 500ms；(d) M7 Doer-Checker 独立性 0 violation；(e) 故障注入测试 6 类硬约束 100% 命中
  - HIL→Sea-trial gate：(a) HIL ≥ 200h **连续** 无 P0 崩溃；(b) HIL 端到端控制延迟 P95 < 200ms；(c) MRC 三类型各 ≥ 10 次 HIL 实测无误触发；(d) AIP 已受理（受理回执）；(e) SIL 2 第三方初步评估无 P0 不符合项

---

### P0-E2：D3.6 "1000+ 场景 98% 通过率" 在 8/19→8/31 12 日窗口不可承诺 🔴

- **Finding**：HAZID RUN-001 **8/19 完成** → v1.1.3 参数回填 132 个 `[TBD-HAZID]`（D3.5）→ 必须重跑全 1000+ SIL 场景验证回归无退步 → D3.6 **8/31 交付**。窗口仅 12 个日历日，扣周末/会议剩约 8 个工作日。D3.5 + D3.6 合计仅 4 人周（2+2），但 132 参数中含 ODD 阈值 / COLREGs T_standOn/T_act / IvP pieces 数等**改变行为分支结构**的参数，仅 YAML 热加载不能保证 1000 场景通过率不下降。
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:316-326`（甘特图：HAZID 8/19 完成 + D3.5/D3.6 同列 8/31）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:435-471`（D3.5 2 人周 + D3.6 2 人周，目标日期均 8/31）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:264-274`（M6 D2.4 在 7/31 已要"≥500 场景 ≥98%"，HAZID 改参数后必将退化）
  - 架构 §3.6 校准方法：「FCB 实船试航采集 ≥ 100 小时运营数据，用极大似然法或贝叶斯回归拟合权重」（`MASS_ADAS_L3_TDL_架构设计报告.md:328`）—— **HAZID 出值 ≠ 实船验证值**，回归测试不可省略
- **影响**：8/31 交付的 D3.6 报告若数据是 HAZID 改前跑的，**等于伪证据**；若强行赶 12 日窗口，将牺牲 SIL 2 第三方评估的可信度，11 月外审会被打回。
- **置信度**：🟢（计划文本 + HAZID 节奏 + 工时账可直接核算）
- **整改建议**：3 选 1：
  - (a) **推迟 D3.6 交付到 9/14**（HAZID 完成后 ≥ 3 周回归窗口；可承受 Phase 4 启动顺延 2 周）
  - (b) **降级 8/31 D3.6 为"v1.1.2 参数 1000 场景 通过率"**，9/15 再发"v1.1.3 参数 1000 场景"补丁
  - (c) **拆分 132 参数风险等级**：行为分支无关参数（如 conformance_score 权重）热加载即可；行为分支相关参数（T_standOn / IvP pieces / Rule 19 触发能见度）单独标 P1，提前 7/31 锁定初值，不等 HAZID 完成

---

### P0-E3：覆盖率定义不闭合（仅"Rule 全分支"，缺 ODD × Disturbance × SOTIF triggering condition）🔴

- **Finding**：D3.6 / M6 §9.4 KPI 定义"Rule 5/6/7/8/9/13–17/19 全分支"为通过门槛，但 DNV-CG-0264 明确要求**三轴乘积**：functional × performance × failure-response 各覆盖一份完整场景集，且 SOTIF（ISO 21448）要求**触发条件覆盖率**而非规则覆盖率。
- **证据**：
  - DNV-CG-0264：「The test scope must cover functional testing (working as intended), performance testing (responsiveness, stability, and reliability), and failure response testing (handling failures safely and maintaining redundancy)」[NLM safety_verification, citation 6]
  - 同源：「Test cases must be performed in different simulated environmental conditions and vessel settings... including specific situational modes such as dense traffic, coastal navigation, narrow passages, restricted visibility, heavy weather, very cold weather, and ice conditions」[NLM citation 8 + 11]
  - `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md:1086-1096`（§9.4 KPI 全部围绕 Rule 分类准确率，无 ODD × disturbance 维度）
  - 计划 D3.6 DoD：`gantt/...:468-471`（仅"Rule 5/6/7/8/9/13/14/15/16/17/19 全部有场景覆盖"）
- **影响**：CCS i-Ship 提交时审查人会问"在 ODD-D × Hs=2.0m × AIS 丢失 1 目标 × Rule 15 的乘积场景下覆盖率多少"，本设计无法给出。
- **置信度**：🟢（DNV-CG-0264 直接要求 + 文本对照可索引）
- **整改建议**：D3.6 DoD 增加：
  - 矩阵要求：Rule 类型（11）× ODD 子域（4）× 扰动模式（≥ 5：感知降质 / AIS 丢失 / 通信丢失 / 风浪扰动 / 多目标拥塞）≥ 220 cell，每 cell ≥ 3 场景
  - SOTIF 触发条件覆盖：5 类假设违反（架构 §11.4）每类 ≥ 20 场景
  - failure response：6 类硬约束注入测试（M7 §3.3）各类 ≥ 10 场景
  - 报告分别给三类通过率，不取交集只取乘积

---

### P0-E4：12 月实船试航 NO-GO 门槛清单缺失 🔴

- **Finding**：D4.5 "≥ 50 nm + ≥ 100h" 仅是计划目标，无"什么条件下绝对不能上船"的负面清单。AIP 申请 11 月才提交（D4.4），CCS 通常 3-6 个月审批（计划自陈 L597）→ 12 月上船时**AIP 未到手**；SIL 2 第三方意见也只有"初步评估"（D4.3）。
- **证据**：
  - `gantt/...:577-586`（D4.5 仅展望性陈述，无 NO-GO 触发条件）
  - `gantt/...:594-597`（CCS AIP 反馈周期 3-6 个月，意味着 12 月 100% 未拿到）
  - DNV-CG-0264：「simulations cannot replace full-scale testing of the vessel as a whole, which must be used for final validation」[NLM citation 8] —— 但**前提**是先有 AIP 或 Statement of Compliance，参考 Ocean Infinity Armada 78-03 [NLM citation 9]
  - 架构 §14.3：「阶段三（M4 实船试航后）：申请 i-Ship (I, Nx, Ri) 进阶标志」→ 试航**前**应已有阶段一/二的 AIP / 基础标志（`MASS_ADAS_L3_TDL_架构设计报告.md:1346-1357`）
- **影响**：CCS 验船师不在场即试航 = 不可作为认证证据；试航中出事故 = 项目终止级风险。
- **置信度**：🟢（CCS i-Ship 路径 + DNV-CG-0264 + 计划文本逐项对照）
- **整改建议**：5/30 前产出 `docs/Design/V&V_Plan/sea-trial-go-nogo-checklist.md`，至少：
  - **必要门槛（缺一项即 NO-GO）**：(1) AIP 受理回执；(2) HIL ≥ 200h 报告 P0=0；(3) SIL 2 初评无 P0；(4) HAZID v1.1.3 参数已 SIL 全回归；(5) CCS 验船师确认到场；(6) 风浪流均在 ODD-A 内（Hs ≤ 2.5m）；(7) ROC 接管链路独立验证 ≥ 60s TMR
  - **充分目标**：50 nm + 100h + 10 次 ROC 接管 + ASDR 完整签名链
  - **最低保守版**：若 AIP 未到，则 12 月仅做"非认证级技术验证试航"，明文标注不作为 i-Ship 申请证据

---

### P0-E5：HAZID 回填后回归测试无独立 owner / 时间预算 🔴

- **Finding**：D3.5（HAZID 回填）+ D3.6（1000 场景）+ D3.7（全模块集成）三件 8/31 同日交付，皆挂"技术负责人"或"架构师"。HAZID 132 参数回填后的全 SIL 回归是**独立的 V&V 任务**，应由 V&V 工程师而非开发负责人执行（独立性原则）。
- **证据**：
  - `gantt/...:441-489`（D3.5/D3.6/D3.7 责任人）
  - DNV-CG-0264：「Simulator-based testing... must be... validated to ensure it renders real-life behavior and does not mask errors in the target system」[NLM citation 5] —— 隐含 V&V 独立性
  - 架构决策四（Doer-Checker 独立路径）外推：测试与实施也应分离
- **影响**：开发者自测自评，等于 D3.7 集成 bug 发现概率严重低估。
- **置信度**：🟡（V&V 独立性是行业共识；具体 owner 单一是计划现实问题）
- **整改建议**：Phase 1 内补 V&V 工程师角色（可与 SIL 平台工程师合并），D3.5/D3.6/D3.7 三个 owner 分离：D3.5 架构师 / D3.6 V&V 工程师 / D3.7 技术负责人。

---

## 3. P1 重要风险

### P1-E1：HIL "≥ 200h" 不足以作为 SIL 2 第三方证据 🟡

- **Finding**：D4.2 "≥ 200h" 数字孤立。架构 §14.4 关键证据清单写的是 "**HIL 测试报告 800h+**"（`MASS_ADAS_L3_TDL_架构设计报告.md:1369`），与 D4.2 不一致。
- **证据**：架构文档与计划自相矛盾。
- **置信度**：🟢
- **整改建议**：以 800h 为目标，200h 为 minimum gate；明文记录"800h 之前不能进入 SIL 2 终评"。

### P1-E2：场景库三场地（SIL/HIL/实船）一致性传递机制未定义 🟡

- **Finding**：D3.6 场景库归档可"在 Phase 4 HIL 中继续使用"（gantt/...:466），但未说明场景文件格式如何在 SIL（YAML 声明式）→ HIL（实物 PLC）→ 实船（人工注入）之间一致映射。
- **证据**：D1.3 / D3.6 / D4.2 文档独立，无场景统一 schema。
- **置信度**：🟡
- **整改建议**：D1.3 SIL 场景 YAML schema 必须含"HIL 等价输入" + "实船人工注入步骤"两段元数据；M6 §9.3 HIL 三场景已有伪代码，可作模板。

### P1-E3：Adversarial vs nominal 比例无显式声明 🟡

- **Finding**：1000+ 场景由"AIS 历史回放 + YAML 合成 + RL 对抗"三源构成（gantt/...:464），但比例无定义。RL 对抗占比过高将偏离实际营运分布；过低则边界场景未充分覆盖。
- **证据**：DNV-CG-0264 仅要求"diverse range" + "abnormal/degraded conditions"[NLM citation 4]，未给具体比例 → ⚫ Unknown 行业典型比例。
- **置信度**：🔴（无来源给出具体比例）
- **整改建议**：D3.6 内显式声明并辩护比例（建议 nominal:边界:对抗 = 60:25:15，待 V&V Plan 评审），不要让审查方反向追问。

### P1-E4：感知-决策端到端延迟门槛仅 M6 列出，全链路无统一 KPI 🟡

- **Finding**：M6 §9.4 写"P95 ≤ 250ms"（M2→M6），M5 D3.2 写"Mid-MPC < 500ms / BC-MPC < 100ms"，M7 D3.3 写"< 10ms"，但 **L4 接到指令前的端到端 P95 / P99** 全链路无 KPI。
- **置信度**：🟢
- **整改建议**：V&V Plan 增加端到端延迟 KPI（建议 P95 ≤ 1.0s for AvoidancePlan, ≤ 200ms for ReactiveOverrideCmd），SIL/HIL 共用一份。

### P1-E5：ASDR 数据格式跨 SIL/HIL/实船 一致性未约束 🟡

- **Finding**：架构 §15 + RFC-004 决议要求 SHA-256 ASDR 签名，但 SIL 测试报告（D2.5/D3.7）未声明产出 ASDR 日志、HIL（D4.2）只提"P0 Bug 清零"无 ASDR 提交项、实船（D4.5）写"ASDR 日志完整"。三场地数据存档格式 / 审计追溯机制不一致。
- **证据**：`gantt/...:478-489`（D3.7）/ `:546-547`（D4.2）/ `:586`（D4.5）
- **置信度**：🟢
- **整改建议**：D1.3 SIL 框架增加 "ASDR 同格式输出" 子项；CCS 提交时三场地 ASDR 可统一回放。

### P1-E6：M6 D2.4 "≥500 场景 ≥98%" 在 7/31 是否首次跑就达 98% 不现实 🟡

- **Finding**：M6 7/31 即要 500 场景 98% 通过率（gantt/...:269），首次完整跑通即达成的概率低。R2.1 已识别"不足时降级到 300"——但 300 也未必能 98%。
- **证据**：架构 §9 五层推理 + M6 §9.1 单元测试目标 ≥95% **代码覆盖率**（路径覆盖），而场景级 98% **行为覆盖率**是更严格指标。
- **置信度**：🟡（行业无对标，但首次跑 98% 是经验性偏高）
- **整改建议**：D2.4 DoD 改为分级：(a) 首次跑通过率 ≥ 90%；(b) 经过 2 周修缮 ≥ 95%；(c) D3.6 1000 场景再压到 98%——避免 7/31 假绿灯。

### P1-E7：HAZID 132 参数中"行为分支结构敏感"参数未预先识别 🟡

- **Finding**：YAML 热加载假设是"参数改了不影响代码路径"，但 IvP pieces 数 / T_standOn / Rule 19 触发能见度阈值改变后将进入新代码分支，旧 SIL 用例可能无覆盖。
- **证据**：架构 §10 IvP 描述 + §3 ODD-D 切换条件
- **置信度**：🟡
- **整改建议**：D3.5 拆"安全热加载白名单"和"需重跑回归列表"两类，前者 Phase 3 内热加载，后者 Phase 4 集中验证。

### P1-E8：CCS i-Ship 第十四章证据清单与 8 月计划交付物不一一对应 🟡

- **Finding**：架构 §14.4 列 9 类证据文件（ConOps / ODD-Spec / HARA / FMEA / SIL 评估 / SDL / COLREGs 覆盖率 / HIL / 实船试航 / 网络安全 / ASDR），D3.x 未明确产出"HARA"和"软件安全开发计划"。
- **置信度**：🟢
- **整改建议**：8 月计划补一个跟踪表 `cert-evidence-tracking.md`，每个 CCS 证据对应到具体 D 编号。

---

## 4. P2 改进建议

### P2-E1：SIL "单次场景运行时间 ≤ 60s"（D1.3 DoD）需说明等比缩放因子

- 60s 仿真时间对应多少仿真船时？若 1×→60s = 1 min sim，1000 场景 = 17 sim hours，CI 可承受；若代表 1 hour sim，则 CI 跑 1000 = 1000 hours 不可行。
- **整改**：D1.3 DoD 增加"60s wall-clock = ≥ 600s sim time"或类似声明。

### P2-E2：M7 Doer-Checker 独立性的 V&V 责任归属

- D3.3 DoD 含 CI 检查 forbidden include / lib，但语法层独立 ≠ 行为层独立。建议 V&V 工程师在 D3.7 内单独跑"M1-M6 故障注入但 M7 行为不变"专项。

### P2-E3：HIL 200h 是否纯 8 模块联调 vs 包含外接 L1/L2/L4 stub

- D4.2 未说明 HIL 接口边界。若 L4 仍是仿真，则 "HIL" 名不副实。

### P2-E4：实船试航 ASDR 数据反哺 SIL 场景库的闭环未设计

- D4.5 只写"实测 AIS 数据回收作为 SIL 场景库真实数据补充"，但格式 / 转换 pipeline / 何时回归未定义。

### P2-E5：缺"测试基础设施自身 V&V"

- D1.3 SIL 框架自身正确性（仿真器精度、确定性回放）未列入 D1 DoD。这本属 G angle，但 E 角度看：用一个未经 V&V 的工具产出认证证据 = 证据链断裂。

---

## 5. 架构/模块缺失项

1. **缺失 §13 测试策略章节**：架构 v1.1.2 全文跳过"测试与验证"独立章节（§13 是多船型，§14 是 CCS 映射，§15 是接口）。建议 v1.1.3 补 §13' "V&V 总策略"，将本评审 P0-E1 的 V&V Plan 引入架构。
2. **缺失 ConOps 文档**：架构 §14.4 第一项即 ConOps，但仓库 `docs/Design/` 下无 `ConOps/` 目录。AIP 申请缺它直接被驳回。
3. **缺失故障注入测试规范**：M7 D3.3 列了 6 类硬约束，但每类故障的注入方式（信号层 / 消息层 / 进程级）未规范。

---

## 6. 调研引用清单

- **NLM safety_verification**（query 2026-05-07，confidence high）：
  - DNV-CG-0264 V&V 策略要求（citation 1, 3, 4, 5）
  - 三类测试覆盖（functional / performance / failure response）（citation 2, 6）
  - 场景多样性（dense traffic / narrow passage / restricted visibility / heavy weather / ice）（citation 8, 11）
  - 故障模式注入（broken wires / out-of-range / signal noise）（citation 9）
  - Ocean Infinity Armada 78-03 = 首个 DNV-CG-0264 远程操作合规声明（citation 9）
  - K-MATE 仅作为参考被提及（citation 10）
  - **未命中**：Yara Birkeland 具体 sea trial 时长 / Kongsberg K-MATE 具体 entry/exit gate / CCS i-Ship 数字门槛 / industry adversarial:nominal 比例
- **仓库文件**：
  - `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`（L124–172, 196–301, 306–501, 504–598, 601–648）
  - `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（§14 L1319–1372；§3.6 L328；§11.4 L969）
  - `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md`（§9 L979–1096）

### 6.x 建议新增 DOMAIN 笔记本

- 暂不建议——本评审中所有 finding 均可由 safety_verification 笔记本支撑（DNV-CG-0264 是核心来源），且空缺项（Yara/K-MATE/CCS 数字门槛）属"工业先例数字"，建议由汇总 agent 决定是否启动 `/nlm-research --depth deep` 补给。

---

## 7. 行业基准对标（Industry Benchmark）

| Finding | 工业先例 | 证据 | 差距 |
|---|---|---|---|
| P0-E1 V&V Plan 缺失 | DNV-CG-0264 §V&V Strategy 强制 | NLM citation 3 | 本计划无 V&V Plan 文档；🟢 |
| P0-E1 SIL→HIL 闭环 | Ocean Infinity Armada 78-03 + DNV simulator on-target hardware 要求 | NLM citation 4, 9 | 本计划 HIL 在"实物 PLC"是符合的，但未要求"控制系统硬件 = 上船同款" |
| P0-E3 三类测试覆盖 | DNV-CG-0264 functional × performance × failure-response | NLM citation 6 | 本计划 D3.6 仅 functional 维度 |
| P0-E3 多模式场景集 | DNV-CG-0264 dense traffic/narrow/低能见度/heavy weather/ice 等模式 | NLM citation 8, 11 | 本计划 ODD-A/B/C/D 4 子域 ≈ 部分匹配；ice/heavy weather 缺失合理（FCB 不入冰区） |
| P0-E4 实船 + AIP 顺序 | CCS i-Ship 阶段制 + Ocean Infinity 拿到 SoC 后才远程作业 | 架构 §14.3 + NLM citation 9 | 本计划 12 月 AIP 未到即试航，序错 |
| P1-E3 比例 | ⚫ No industry analog found（DNV/Yara 均未公开） | — | 需要 V&V Plan 自证合理性 |
| P1-E6 首次 98% | ⚫ No industry analog found（K-MATE/Yara 未披露首次通过率）| — | 建议分级降级 |

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

本评审检查了 D1.3–D4.5 全部 V&V 交付物。**发现 4 处 FCB-only 隐含假设**：

1. **D1.3 SIL 框架默认绑定 FCB MMG（Yasukawa 2015 标准参数）**（gantt/...:64-65）—— 拖船 / 渡船 MMG 系数完全不同；SIL 框架若硬编 FCB ζ/m/I 矩阵，换船须重写仿真器内核而非热替换 YAML。**整改：** D1.3 DoD 增 "MMG 系数纯 YAML 注入，无 C++ 硬编"。⚠️ **P1**
2. **M6 D2.4 + D3.6 场景库基于 FCB 高速船（15 kn 北向初始）**（M6 §9.3 场景 A）—— 拖船 5 kn / 集装箱船 22 kn 的几何 / 时序常数（T_standOn 8min）完全不适用。**整改：** 场景 YAML 必须含 vessel_type 字段，T_standOn / T_act 通过 Capability Manifest 注入。⚠️ **P1**
3. **D2.4 BC-MPC ROT_max 速度查表（[TBD-HAZID]）= FCB 高速段特性**（gantt/...:373）—— 拖船吊缆作业时 ROT 受拖力约束，与速度查表逻辑不同。**整改：** 查表本身在水动力插件，不在 BC-MPC 求解器内。⚠️ **P0** if leaks into M5 core
4. **D4.5 实船试航 50 nm + 100h 隐含开阔海域 + OWEC 短途**（架构 §1.1）—— 集装箱船跨洋单航次即超 100h；拖船作业半径 < 5 nm 凑不到 50 nm。**整改：** 试航 KPI 应是 ODD 内"代表性运营周期"而非绝对里程数。⚠️ **P2**

无明显 "if vessel == FCB" 代码级潜入风险（V&V 阶段为文档审查，决策核心不直接受影响），但**场景库 / 仿真器 / 试航 KPI** 三处必须显式 vessel-type-aware。

---

*评审产出 · 2026-05-07 · Angle E（V&V / Test Strategy）· READ-ONLY · 仅写入 docs/Design/Review/2026-05-07/E-test-handoff-readiness.md*
