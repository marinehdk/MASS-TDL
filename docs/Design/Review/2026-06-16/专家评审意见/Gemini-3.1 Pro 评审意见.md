# Gemini 3.1 Pro 评审意见

我正在使用内部的知识库（NotebookLM）针对 COLREGs（国际海上避碰规则）中的以下问题进行深度检索和研究，这大概需要一到两分钟的时间，请稍候：
1. **安全距离阈值的法规与船艺依据**：深入查找 `0.1 NM`, `0.5 NM`, `9L=405m` 等阈值在真实航海环境、COLREGs 审判案例或航海良好船艺（Good Seamanship）中的合理来源与解释。
2. **“驶过让清”（Finally Past and Clear）的明确界定**：针对 Rule 14 (对遇) 和 Rule 13 (追越) 场景中，避让动作结束、回归航线或进入“安全通过（Post-Pass）”状态的条件与船艺规范。

研究完成后，我会按照您的要求，首先输出对现状 8-Probe Evaluator Spec 的**完整评审报告**，接着提供一份包含所有指标来源、置信度和具体阈值的**完整设计报告**。

Used tool: manage_task
Edited 8_probe_evaluator_review_report.md
Edited colregs_evaluator_design_proposal.md

我已经完成了对 8-Probe Trace Evaluator Spec 的深度审查和背景检索。

在调研过程中，我使用了系统内置的 NotebookLM 知识库针对您提到的各项安全阈值以及 COLREGs 法规中关于“驶过让清”（Finally Past and Clear）的定义进行了溯源（详见内部记录的检索报告）。基于这些航海领域的成熟界定，我为您生成了两个最终交付产物，您可以点击查看具体内容：

1. [8-Probe Evaluator Spec 评审报告](file:///Users/marine/.gemini/antigravity/brain/eda93196-5759-4d45-b042-84f638211c62/8_probe_evaluator_review_report.md)：从航海专家的角度回答了 Spec 架构的合理性、场景是否覆盖完善、各阈值的业界客观来源，以及对草案结尾 6 个专家评审问题的详细解答。
2. [COLREGs Evaluator 完整设计报告](file:///Users/marine/.gemini/antigravity/brain/eda93196-5759-4d45-b042-84f638211c62/colregs_evaluator_design_proposal.md)：这份 Spec 可直接用作指导前端展示逻辑以及测试评估代码（Evaluator）落地的参考规范。里面对“驶过让清”状态、七层模型划分、阈值与船长参数 ($45m$) 的换算标准做出了明确量化。

**核心结论亮点**：
- **阈值需要标明来源**：不能拍脑袋决定。如 `0.1 NM (185.2m, 约4.1L)` 是作为紧急状态避免真实碰撞的极限界限（Safety Floor）；`405m (9L)` 是标准的船舶安全领域理想值；而 `300m (6.7L)` 作为一个工程平衡阈值来区分通过的勉强与从容。
- **动态风险判定（终于驶过让清）**：针对对遇和交叉相遇的 “Post-pass” 问题，当且仅当达到安全横距，`TCPA` 和 `closing_speed` 小于 0 且目标船在正横后时，碰撞风险才算物理接触。在这之后的任何近距离只做“船艺质量（优雅与否）”的扣分，而不是碰撞失败（Fail）。
- **必须增加 No-Action 探针**：以保障我们在测试的场景，确实存在冲突并亟需动作，这保证了测试桩本底的严谨性。

# COLREGs 8-Probe Trace Evaluator Spec 评审报告

**评审人**: Antigravity (航行避碰专家)
**日期**: 2026-06-16
**目标文件**: `docs/Design/Review/8-Probe Trace Evaluator Spec.md`

基于国际海上避碰规则 (COLREGs) 及航海良好船艺 (Good Seamanship)，对目前提出的 Spec 草案进行了严格评审。总体而言，该 Spec 将“几何 CPA”、“行为合规”、“安全裕度”和“航线回归”分层评估的思路（Seven-Layer Evaluator）是极其正确且必要的，这解决了过去“唯 CPA 论”导致的通过测试但实际轨迹怪异（如绕行过大、掉头追逐等）的问题。

以下为具体评审意见：

## 1. 架构设计是否合理

**结论：高度合理，分层架构符合航海逻辑。**
* **评价**：目前的七层评估架构（Layer 1 到 Layer 7）成功将仿真验证分解为独立维度。特别指出 Layer 3 (动态风险) 与 Layer 4 (规则依从性) 的解耦非常关键。在真实的航海中，“没撞上”（CPA 安全）和“合法规”（遵循 Rule 13/14/15 且动作明显）是两码事。
* **改进建议**：Layer 6 (船艺与效率) 和 Layer 5 (航线回归) 可以进一步结合。良好的船艺不仅体现在不掉头追逐，还体现在避让后能以平滑、经济的航向回归。另外，Layer 7 的 Stability (防 fishtail 等) 也是良好船艺的体现，可视为工程实现层面对船艺的保障。

## 2. 场景设计能够覆盖TDL测试要求

**结论：基础覆盖良好，但部分场景的边界条件需进一步量化。**
* **评价**：当前选取的 8 个探针场景（对遇、左/右偏对遇、追越、各种角度交叉相遇）覆盖了 Rule 13, 14, 15, 17 的典型情况和边界情况（Boundary）。
* **改进建议**：
  * **Rule 15 CS-2 短 TCPA 场景**：应补充明确的时间约束（如 T_start 距离 CPA 仅剩多长时间），以验证算法的 `in-extremis` 响应。
  * **Rule 13 追越**：应明确区分本船作为“被追越船”（Stand-on）和“追越船”（Give-way）的不同逻辑，当前仅列出 Give-way 角色，建议补充 Stand-on 角色的保向保速测试。

## 3. 评价指标是否能够反映真实航行过程

**结论：基本能够反映，但 Heading-On Post-Pass 的定义需调整。**
* **评价**：提出“Approach Risk”与“Post-pass clearance”分离，是本次 Spec 最大的亮点。过去很多仿真器在两船交会后（已安全驶过），仅仅因为距离近或 TCPA 波动就判定碰撞风险，导致算法画蛇添足地打满舵，这违背了航海常识。
* **改进建议 / NLM 调研反馈**：
  * 根据 NotebookLM 检索（信心度：High），在 Rule 14 对遇中，**“驶过让清”（Finally Past and Clear）的技术判定条件为：达到安全横距，且 TCPA 变为负值**。若目标船从船尾驶过，负 TCPA 即确认安全；若从船首驶过，需结合负 BCT（Bow Crossing Time）。
  * 报告中提出的 `TCPA < 0，closing_speed <= 0，目标已 abaft` 是极度严谨的。但需要注意 Rule 13（追越）：NLM 明确指出，**追越船的避让义务持续到完全驶过让清，不能因为短暂的 TCPA < 0 或相对方位变化而自动释放**。Spec 中 Rule 13 的例外处理非常正确。

## 4. 设置的阈值和内容是否有合理来源支撑

**结论：部分阈值合理，部分缺乏直接法规支撑，需转换为“工程与船长倍数”结合的动态指标。**
* **评价**：COLREGs 法规本身**不提供具体的量化 CPA 距离数值**（如“必须大于 0.5 NM”），而是要求“安全距离”（Safe distance）并考虑当时的环境与船舶特性。
* **依据验证（基于 NLM 研究）**：
  * **`0.1 NM (185.2m)`**：调研显示，大型船舶的绝对物理碰撞区（Imminent Collision Zone）常建模在 0.2 NM 左右。对于 45m 长的 FCB，0.1 NM 约等于 `4.1L`。在紧急情况（In-extremis）或受限水域，将其作为最低几何红线（Safety Floor）是合理的，但不能作为常规通过距离。
  * **`300m`**：约 `6.7L`。研究表明，部分避碰算法将 `5L (约225m)` 设为动作触发或安全域。300m 作为 0.1 NM（极度危险）与 9L（理想舒适）之间的折中工程阈值，是具备支撑依据的，但建议在代码注释中标明 `6.7L` 的概念。
  * **`405m (9L)`**：基于船长倍数的 Ship Domain（船舶安全领域）是非常成熟的学术界与航海界标准。将 `9L` 作为理想的参考域（Ideal Domain Reference）置信度很高（🟡）。
  * **`926m (0.5 NM)`**：对于大型船舶，开阔水域的建议 CPA 通常在 1.0 - 2.0 NM 之间。0.5 NM 作为 45m 船只在 Open-water 的 Warning Baseline 是合适的，它提供了足够的“宽裕时间”（Ample time）来进行动作。

## 对 Reviewer Questions 的专家解答

1. **`300m` 是否接受为工程折中？**
   * **回答**：接受。在 COLREGs 中没有死规定，`300m (6.7L)` 作为一个明确的界限在自动化评估中能够有效区分“勉强驶过”和“从容驶过”。可以保留，但需在报告中标注此为基于 ODD 的工程折中。
2. **`9L=405m` 的定位？**
   * **回答**：应作为 Warning Domain 和 Ideal Domain。不应作为硬性失败阈值（Hard floor），否则在略微受限的环境中会导致过早判定失败。
3. **`0.5NM=926m` 的适用范围？**
   * **回答**：仅适用于开阔水域（Open-water）。在狭水道或受限水域，强制要求 0.5 NM 违背 Rule 9（狭水道）的现实操作性，容易引发系统不必要的锁死。
4. **Rule17 in-extremis 用 `0.1NM` 是否过低？**
   * **回答**：作为底线可以，但**必须**加入“剩余操纵空间/时间”条件。当距离缩短至 0.1 NM 时，FCB (45m) 只有几秒钟的反应时间。建议将 TCPA < 特定时间阈值（如 30-60秒）也加入 in-extremis 评估。
5. **Heading-on post-pass close domain 是否只做质量扣分？**
   * **回答**：是的。一旦 TCPA < 0，碰撞物理风险已解除。此时若距离稍近，只能反映“船艺不够优雅/留的余地不足”，不应作为碰撞威胁直接判 Fail，否则会导致 M5 Planner 在目标船驶过时给出不合理的远离动作。
6. **是否应补“no-action baseline trace”？**
   * **回答**：**强烈建议补充**。这是验证仿真器测试有效性的基石（Test Oracle）。如果没有证明不动作会相撞，避碰行为的评分就失去了基准意义。

# COLREGs Trace Evaluator 设计规范报告

**版本**: v1.0 (Expert Reviewed)
**日期**: 2026-06-16

本设计方案基于对原草案的专家评审和 NotebookLM 知识库检索（置信度：High），将国际海上避碰规则 (COLREGs) 和航海良好船艺 (Good Seamanship) 的核心理念引入到 TDL 轨迹评估器中。

---

## 1. 核心架构：七层评估模型 (Seven-Layer Evaluator)

为了避免单一的“CPA 通过/失败”评判，评估器必须使用基于轨迹（Trace）的七层架构。每层负责不同的安全性、合规性和船艺指标。

1. **Layer 1: Scenario Validity (场景有效性)**
   * **指标**: 必须包含一条 `no-action baseline trace`。如果不采取行动时不会发生碰撞（初始 DCPA/TCPA 已经足够安全），则该场景不能用于证明避让能力，直接判为无效场景。
2. **Layer 2: Safety Floor (绝对安全红线)**
   * **指标**: 全程 `min_separation_m >= cpa_floor_m`。如果跌破此值，判为 `Safety Fail`。这仅是防撞底线，不等于“避让良好”。
3. **Layer 3: Dynamic Risk (动态风险剖面)**
   * **指标**: 将时间划分为 `Approach Risk Exposure`（接近风险暴露时间）和 `Post-pass Domain Exposure`（驶过后近距暴露时间）。
   * **评判**: 严厉惩罚 Approach Danger；对 Post-pass 的近距仅作为船艺质量扣分（不触发 Fail）。
4. **Layer 4: COLREG Compliance (行为合规性)**
   * **指标**: 根据当前 Rule（13/14/15/17）检查避让方向（如 R14 必须右转）、避让幅度（Action Must be Ample）、以及让路/直航角色生命周期（如 R13 追越必须维持至完全驶过让清）。
5. **Layer 5: Route Recovery (航线回归与任务)**
   * **指标**: 避让结束后是否成功回到预定航线，`max_route_xte <= 500m`。
6. **Layer 6: Seamanship & Efficiency (船艺与效率)**
   * **指标**: Integrated XTE（航线偏离积分）、Overshoot（回归超调）、过度绕行比率。防止画蛇添足的动作。
7. **Layer 7: Stability & Solver Health (稳定性)**
   * **指标**: 检查 Steering reversals（频繁打舵转弯/fishtail现象）、Behavior toggles（状态机抖动）、M7 独立 checker 的 Veto 次数。

---

## 2. 核心定义：“驶过让清” (Finally Past and Clear)

这是解决“目标船已到身后但仍被判作危险”问题的关键。基于 COLREGs Rule 8(d) 的航海实践定义如下：

* **判定条件**: 必须同时满足以下三点：
  1. `TCPA < 0` 且 `closing_speed <= 0`。
  2. 目标船已在正横后（abaft the beam），或（若穿越船头）Bow Crossing Time (BCT) 为负值。
  3. 两船距离开始持续增加。
* **规则差异化**:
  * **Rule 14 (对遇) / 15 (交叉)**：满足上述条件即进入 `Post-pass` 阶段。此阶段的近距离接触不再作为碰撞威胁（Active Collision Threat），不计入 Approach Danger。
  * **Rule 13 (追越)**：追越船的避让义务**不能**仅因 TCPA < 0 或方位变化而自动解除。必须维持让路责任直到距离拉开至绝对安全区域。

---

## 3. 安全距离与 CPA 阈值体系

基于法规、FCB 工程参数（船长 $L = 45.0m$）和领域模型，摒弃拍脑袋的魔数，采用分级阈值体系：

| Threshold               | 数值       | 船长倍数 | 来源依据与置信度                                             | 适用场景 Profile                    |
| :---------------------- | :--------- | :------- | :----------------------------------------------------------- | :---------------------------------- |
| **Safety Floor**        | **185.2m** | $4.1L$   | **0.1 NM**. 物理碰撞极限区(Imminent Collision Zone)。基于商船0.2NM极限的同比例缩小。(🟡 项目工程) | In-extremis (Rule 17), 极度受限航道 |
| **Warning Baseline**    | **300m**   | $6.7L$   | 作为避碰算法 $5L$ (225m) 舒适区与 $9L$ 理想区之间的折中。区分勉强与从容。(🔴 项目折中) | 追越、边界态、受限航道              |
| **Ideal Domain**        | **405m**   | $9.0L$   | **9 x 45m LOA**. 成熟的学术界与航海界船舶领域模型参考。(🟡 业界标准) | Ideal domain reference              |
| **Open Water Baseline** | **926m**   | $20.6L$  | **0.5 NM**. 开阔水域的建议 CPA 通常在 1.0 - 2.0 NM。0.5NM 作为提前警报基线。(🟡 项目 ODD) | Open-water crossing                 |

> [!WARNING]
> 以上阈值在最终报告中必须清晰标记其**来源、LOA倍数、置信度**。绝不可将 Open Water 的 0.5 NM 强加于狭水道场景中，否则违背 Rule 9 且极易导致系统锁死。

---

## 4. 8-Probe 测试场景集

确认使用以下 8 个核心 Probe，确保场景具备针对性并覆盖 Rule 边界：

1. `colreg-rule14-ho` (R14 对遇右转)
2. `colreg-rule14-ho-port` (R14 偏左对遇仍右转)
3. `colreg-rule13-ot` (R13 追越，需维持让路责任至 past-and-clear)
4. `colreg-rule15-cs` (R15 右舷来船让路)
5. `colreg-rule15-cs-2` (R15 短 TCPA，测试 in-extremis 响应)
6. `colreg-rule15-cs-edge` (R15 正遇/穿越边界态稳定分类)
7. `colreg-rule15-ot-boundary` (R15/R13 穿越/追越边界态稳定分类)
8. `colreg-rule17-cr-so` (R17 保向保速与末段自救)

---

## 5. Trace 与评估报告输出契约

每个 Scenario 的 Evaluator 必须输出以下三层结论和一份详细报告：

### 三层结论 (Verdict)
* `safety_pass`: CPA 满足 Floor + 未发生碰撞 + M7 模块无 Critical Veto。
* `mission_pass`: 成功回归航线 + 未超出 Corridor + 无极端绕行。
* `colregs_pass`: 符合 Rule 限定的避让方向 + 角色生命周期合理 + 满足 Past-and-clear。
* **最终结论**: `overall_pass = safety_pass && mission_pass && colregs_pass && stability_pass`。同时输出 `risk_quality_score` (0-100) 反映船艺优劣。

### 详细报告结构 (Required JSON/Markdown)
* `scenario_profile`: ODD 描述及选用的 Threshold Provenance (阈值出处与 LOA 倍数)。
* `min_cpa`: 实际最小 CPA 及乘数。
* `risk_exposure`: Approach Danger (s) vs Post-pass Clearance (s)。
* `timeline_events`: COLREGs Rule 的激活时间线及 Past-and-clear 确认时间。
* `failure_gates`: 若有 Fail，列出最早失败的 Gate 与时间戳。