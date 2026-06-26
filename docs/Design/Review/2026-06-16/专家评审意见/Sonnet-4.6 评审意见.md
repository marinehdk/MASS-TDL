# Sonnet 4.6 评审意见

两份交付物已完成。以下是完整摘要：

---

## 交付物

### 📋 [评审报告](file:///Users/marine/.gemini/antigravity/brain/df166c42-d158-4865-a09d-44846254f191/COLREGs_TDL_Review_Report.md) — COLREGs TDL 专家评审

**结论概览**：

| 维度     | 评级       | 核心结论                                                     |
| -------- | ---------- | ------------------------------------------------------------ |
| 架构设计 | 🟡 基本合理 | Layer3动态风险、Layer6船艺效率未实现；评估时间轴不统一       |
| 场景覆盖 | 🟡 骨架完整 | 左舷穿越、Rule19、纯减速避碰、no-action baseline缺失         |
| 评价指标 | 🔴 较大问题 | `acted_early` 死代码；`heading_change_deg` 时序不明；动态风险无分相位 |
| 阈值来源 | 🟡 部分有据 | 185.2m/926m有据；300m可接受但需标注；`min_give_way_turn_deg=5°`不可接受 |

### 📐 [设计方案 v1.0](file:///Users/marine/.gemini/antigravity/brain/df166c42-d158-4865-a09d-44846254f191/COLREGs_TDL_Design_Spec_v1.md) — 完整重建规格

核心内容：

1. **场景修订**：Rule13初始距离0.6→1.5NM；`cs-edge`/`ot-boundary`目标船速降至12/15kn；新增`rule15-cs-port`左舷穿越场景
2. **七层评估器完整规格**：含伪代码、每层职责边界、Heading-On post-pass特殊处理
3. **Rule 8时序化重构**：`acted_early` 从trace计算 `time_before_cpa_s`；30°/120s为full门
4. **四门独立裁决**：`safety_pass AND mission_pass AND colregs_pass AND stability_pass`
5. **阈值体系**：所有4个CPA阈值附文献依据表（Fujii/Goodwin/Coldwell/SIL实践）

---

# COLREGs TDL 测试平台专家评审报告

**审查者身份**: 航行避碰系统专家 / COLAV SIL平台工程师  
**审查范围**: 8-probe场景集 + Trace Evaluator Spec v0.1  
**审查日期**: 2026-06-16  
**来源文件**:
- [`scenarios/COLREGs测试/`](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/scenarios/COLREGs测试/)
- [`docs/Design/Review/8-Probe Trace Evaluator Spec.md`](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/docs/Design/Review/8-Probe%20Trace%20Evaluator%20Spec.md)
- [`scoring/stability_scorer.py`](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/src/sim_workbench/sil_nodes/scoring/scoring/stability_scorer.py)
- [`scoring/rule_compliance_evaluator.py`](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/src/sim_workbench/sil_nodes/scoring/scoring/rule_compliance_evaluator.py)

---

## 执行摘要

当前8-probe场景集和评估框架已具备较好的基础骨架，解决了最核心的几个问题（fishtail检测、onset-latch回归锁）。但存在**5个结构性缺陷**和**12个具体评判标准问题**，导致"测试通过但前端看起来不对"现象持续存在。

评审结论等级：

| 维度     | 评级                       | 核心问题                                                     |
| -------- | -------------------------- | ------------------------------------------------------------ |
| 架构设计 | 🟡 **基本合理，有明确缺口** | Layer3 动态风险缺失；无 no-action baseline                   |
| 场景覆盖 | 🟡 **骨架完整，关键缺失**   | Rule16时序、Rule8最小角度、R17b触发时机未独立测              |
| 评价指标 | 🔴 **较大问题**             | `heading_change_deg` 非时序；动态风险未分相位；CPA阈值和实际行为脱节 |
| 阈值来源 | 🟡 **部分有据，300m孤立**   | 185.2m/926m有合理来源；300m是折中无实证；30°/15°来源缺失     |

---

## 第一部分：架构设计评审

### 1.1 七层评估器结构评价

**整体判断：🟡 基本合理，但Layer3/6严重欠实现**

Spec v0.1提出的7-Layer框架理念正确，将Safety、COLREGs合规、Mission、船艺效率、稳定性分层是目前MASS COLAV评估领域的共识（参考NTNU colav-simulator评估框架 [NLM: high confidence]）。

| 层                      | 评审结论                   | 问题                                                         |
| ----------------------- | -------------------------- | ------------------------------------------------------------ |
| L1 场景有效性           | ✅ 设计正确                 | 需要 no-action baseline 验证每个场景的原始冲突有效性         |
| L2 安全底线 (CPA floor) | ✅ 设计正确                 | 但仅回答"有没有擦近"，与 L3 边界模糊                         |
| L3 动态风险             | 🔴 **只在 Spec 中，未实现** | `stability_scorer.py` 完全没有 approach/post-pass 风险分相位 |
| L4 COLREGs合规          | 🟡 部分实现                 | `rule_compliance_evaluator.py` 存在，但依赖 `heading_change_deg` 标量，非时序 |
| L5 航线回归             | ✅ 有基础实现               | `route_return_xte_m_lt`/`route_corridor` 在YAML中定义；但`final_xte`采集不完整 |
| L6 船艺/效率            | 🔴 **未实现**               | `kpi_deriver.py` 只有 `avg_rot_dpm`/`max_rudder_deg`；无 path ratio、integrated XTE、overshoot |
| L7 稳定性               | ✅ 相对完整                 | `stability_scorer.py` 已实现 fishtail/flap 检测；是8-probe当前最强的层 |

### 1.2 关键架构缺陷

#### 缺陷 A1：缺乏 no-action baseline trace（严重）

**问题**：当前每个场景只跑"有控制器"的trace。没有"不做任何避碰动作时的CPA/TCPA"作为基线验证。

**后果**：无法证明场景确实存在真实碰撞威胁。若DCPA设置不当（如旧版`cs-3`的`cpa_min=0`），场景将永远GREEN，丧失测试意义。

**COLREGs依据**：Rule 7要求"充分运用一切适当手段"评估碰撞风险。评估系统若无法证明原始威胁有效，则无法测试Rule 7的风险识别能力。

**来源**：Spec v0.1 §10问题6已正确提出此需求；README §设计约束已描述"无动作必碰"原则，但未形成可执行验证契约。

#### 缺陷 A2：评估时间轴缺乏统一对齐基准（中等）

**问题**：`stability_scorer.py` 使用 `sim_t` 对齐；`kpi_deriver.py` 使用 `stamp`；`rule_compliance_evaluator.py` 使用单次快照。三个评估器使用不同的时间基准。

**后果**：`heading_change_deg`是什么时间点的测量无法确定；`min_cpa_nm` 在 kpi_deriver 中是整个arrow文件的全局最小值，不区分是否在post-pass阶段。

#### 缺陷 A3：评估结论输出不统一（中等）

**问题**：Spec提出`safety_pass && mission_pass && colregs_pass && stability_pass`，但：
- `run_6_scenarios.py` 输出的 `overall_pass = cpa_ok AND stability_pass`（README第79行）
- 缺少 `mission_pass`（航线回归）作为独立门
- 缺少 `colregs_pass`（规则合规）作为独立门

**后果**：稳定性好、CPA达标但完全不归航、或者整个场景用了错误规则，都可能 overall_pass=True。

---

## 第二部分：场景设计覆盖评审

### 2.1 场景覆盖矩阵

| 覆盖维度                    | 当前状态               | 问题                       |
| --------------------------- | ---------------------- | -------------------------- |
| **Rule14** 纯正遇           | ✅ `rule14-ho`          | 完整                       |
| **Rule14** 偏左对遇边界     | ✅ `rule14-ho-port`     | 完整                       |
| **Rule13** 追越             | 🟡 `rule13-ot`          | 参见下方 S1                |
| **Rule15** 右舷穿越         | ✅ `rule15-cs`          | 完整                       |
| **Rule15** 短TCPA穿越       | 🟡 `rule15-cs-2`        | 参见下方 S2                |
| **Rule15** 正遇/穿越边界    | ✅ `rule15-cs-edge`     | 完整                       |
| **Rule15/13** 穿越/追越边界 | ✅ `rule15-ot-boundary` | 完整，但目标船速过高       |
| **Rule17** 直航保向→17(b)   | 🟡 `rule17-cr-so`       | 参见下方 S3                |
| **Rule19** 能见度受限       | ❌ **缺失**             | 已知缺口，但影响Rule8语义  |
| **左舷穿越 (Rule15-port)**  | ❌ **缺失**             | 仅测右舷，左舷穿越语义未验 |
| **多船冲突**                | ❌ **已知缺口**         | Imazu-22范畴               |
| **Speed reduction only**    | ❌ **缺失**             | 减速避碰（无转向）未测     |

### 2.2 具体场景问题

#### S1: `colreg-rule13-ot` — 追越场景

**问题1 - 初始距离过近**：初始距离0.6 NM（`63.44` → `63.454979`约1.7km），OS以14kn追越，TCPA约为220s。Rule 8(f)要求"ample time"，此场景几乎没有给M6/M4足够的时间做反应观察。

**问题2 - `overtake_required: false` 逻辑不严格**：当前允许"安全跟随"作为合规动作，但未定义"安全跟随"的评价标准（跟随多长时间？跟随距离多少？）。若系统永远保持安全跟随不完成追越，`overall_pass=True` 但行为模式完全异常。

**问题3 - Rule 13(d) 测试覆盖不完整**：场景描述"方位前移不得重分类"，但当前 `role_onset_stable` KPI 只检测 duty 翻转，不验证 M6 在方位越过正横后是否错误释放 conflict。

**COLREGs依据**：Rule 13(d) 明确规定"如果不能确定是否构成追越，应视为追越"（ambiguity → treat as overtaking），此场景应有一个明确的"方位前移过正横" `event_checkpoint`。

#### S2: `colreg-rule15-cs-2` — 短TCPA穿越

**问题 - TCPA计算矛盾**：描述中写 `initial range 1.0 NM, t_cpa ~= 540s`。以1.0 NM/12kn接近速度计，若直线DCPA≈0，TCPA应约为240s（1.0 NM ÷ (12+12cos60°) kn），不是540s。TCPA声称540s与"短反应窗口"描述矛盾。

> **需要核实**：是否场景实际TCPA更接近240s，描述有误？

**问题 - Rule 8(b) "timely action"的阈值未定义**：场景目标是测"早期、明确"动作，但评估器中没有任何"avoidance_time_before_cpa_s"门控。目前只检查最终CPA是否≥926m，但不管动作发生在TCPA=300s还是TCPA=30s。

#### S3: `colreg-rule17-cr-so` — Rule17直航→17(b)

**问题1 - 17(b)触发时机无量化验证**：场景期望"前75%保向，末段17(b)"，但 `premature_giveway_deg < 10°` 只检查前75%是否有大转向，不验证17(b)是否**及时触发**（CPA降到什么门槛才触发？）。若系统一直保向直到碰撞，`premature_giveway` PASS，但整个场景实际 FAIL。

**问题2 - `avoidance_time_s: 399.2` 设计意图不明**：这意味着期望在t=399s才开始Rule17(b)动作。若 `total_time=360s`（README第363行），总时长不足以容纳399s的等待。实际YAML中`total_time=1200s`，但README和Spec描述不一致。

**问题3 - CPA门 185.2m对直航船不合理**：Rule17(b)要求独立行动保障安全，但in-extremis时距离已非常近，185.2m（4.1L）是否足够给FCB（45m×12kn）制动/转向？应计算剩余机动空间。

#### S4: `colreg-rule15-cs-edge` — 边界场景

**问题 - 目标速度29.2kn异常**：`cs-edge`场景目标船速29.2kn，极高，不符合典型开放水域遭遇场景，会引入高相对速度下的TCPA压缩效应，边界分类稳定性测试被"速度"因素干扰。

#### S5: `safe_route-left-encounter` — L2集成回归场景

**问题 - 不在8-probe清单中但存在于目录**：此场景的 `cpa_min_m_ge: 926.0`（open water），但场景速度29.16kn（L2 safe_route），对应一个不同的ODD和船速。其 `cpa_acceptance` 字段缺少 `profile`/`emergency_floor_m`/`ideal_domain_m`，评估器无法正确判断来源。需要明确此场景属于哪个测试集，防止与8-probe混跑。

---

## 第三部分：评价指标评审

### 3.1 `rule_compliance_evaluator.py` 问题

#### 指标问题 M1：`heading_change_deg` 是什么时间点的标量？（严重）

```python
def _eval_rule14(s: dict) -> str:
    hc = abs(s.get("heading_change_deg", 0.0))  # 哪个时间点？
```

**问题**：`heading_change_deg` 是快照值还是峰值？从何时到何时的变化量？
- 若是从 t=0 到 t_cpa 的累计：无法区分早动作和晚动作
- 若是瞬时快照：无意义
- 若是全程峰值：会把回归段的反向转向也计入

**COLREGs依据**：Rule 8 要求的是"及时且明显"的动作。评估应使用 **避碰动作窗口内** 的峰值净偏航，而非全程标量。

#### 指标问题 M2：30°阈值来源不明（中等）

```python
if hc >= 30.0 and cpa >= cpa_target:
    return "full"
if hc >= 15.0:
    return "partial"
```

**来源调查结果（NLM high confidence）**：
- Rule 8 原文无具体度数
- 文献中：最严格的合规模型要求转向 ≥45°（< 45°视为违规）；较宽松模型要求转向 ≥30° 才视为 "readily apparent"
- 30° 作为 "full" 门槛：属于较宽松框架，尚可接受，但需明示来源
- 15° 作为 "partial"：过低，15°在海上视觉上几乎不可见，不符合Rule 8"readily observable"要求

**建议**：
- `full`：转向 ≥ 30° **且** 在TCPA>T_min（如2分钟）前完成
- `partial`：转向 ≥ 15° 但未满足时间要求，或仅5-15°减速补偿
- `violated`：转向 < 10°（或向错误方向）

#### 指标问题 M3：Rule 16 `acted_early` 字段从未定义（严重）

```python
def _eval_rule16(s: dict) -> str:
    early = s.get("acted_early", False)  # 这个字段从哪来？
    if hc >= 30.0 and cpa >= cpa_target and early:
        return "full"
```

**问题**：`acted_early` 从未在trace schema、场景YAML或scoring pipeline中被赋值。默认 `False`，意味着 Rule 16 永远无法达到 "full" 合规。这是一个死代码bug。

**COLREGs依据**：Rule 16要求 give-way 船只"ample time"采取行动。需要定义：`avoidance_start_time_s < tcpa_s - T_min_s`。

#### 指标问题 M4：动态风险完全缺失（严重）

Spec v0.1 §6 Layer 3提出：
- `approach_warning_exposure_s`
- `approach_danger_exposure_s`  
- `post_pass_domain_exposure_s`

但在所有现有实现文件中均不存在。这意味着：
- 系统可能在接近阶段持续处于danger zone而不被捕捉
- 过早进入post-pass清除而没有惩罚
- 前端看到的"危险时间段持续很长"在测试结果中完全不体现

### 3.2 `stability_scorer.py` 问题

#### 指标问题 M5：`steering_reversals` 使用 ROT 而非 rudder（已知，文档已说明，但阈值偏宽）

**现状**：用 ROT sign reversal 代理 rudder reversal，dead band 1.0°/s。

**问题**：give-way ≤ 4次是合理的（转右、稳定、回归、稳定 = 4次）。但1.0°/s死区对于45m船只的操舵噪声来说可能偏大，会漏掉真实的fishtail。应结合船速和L考虑：
- FCB 45m，12kn → 正常稳定ROT约 0.3-0.8°/s
- 建议dead band降至 0.5°/s

#### 指标问题 M6：`rot_hold_std_dps < 1.5` 阈值无明确来源

**现状**：硬编码1.5°/s作为yaw-rate平滑度门。

**来源**：此值在YAML、文档、代码注释中均无依据说明。

**船动力学参考**：FCB 45m cargo，12kn，L/B≈5，正常稳定转向yaw rate标准差应 < 0.3°/s。1.5°/s是否太宽？需要实测数据校准。

#### 指标问题 M7：`turn_starboard` 检查过于粗糙（中等）

```python
"turn_starboard": max_stbd >= max_port and max_stbd >= min_give_way_turn_deg
```

**问题**：`min_give_way_turn_deg = 5.0°`，这意味着只要偏右超过5°，就算"满足give-way右转"。5°在海上几乎不可见，完全违背Rule 8"substantial action"要求。

**建议**：应与Rule 8合规评估对齐，give-way turn ≥ 30° 才视为明显动作。

---

## 第四部分：阈值来源和置信度评审

### 4.1 CPA阈值来源

| 阈值             | 当前来源说明                       | NLM验证结果                                                  | 置信度 | 结论                                                         |
| ---------------- | ---------------------------------- | ------------------------------------------------------------ | ------ | ------------------------------------------------------------ |
| `185.2m = 0.1NM` | 项目紧急下限，corridor约束下L2驱动 | Goodwin模型 astern domain ≈ 0.45NM >> 185.2m；但受限航道情况 EMSA/IALA接受 0.1NM 紧急阈 | 🟡 中等 | **接受**，但必须标注为"restricted channel emergency floor"，不可用于开放水域 |
| `300m`           | "0.1NM与9L=405m之间折中"           | 无文献直接支持；Fujii-Tanaka at 10kn forward semi-axis = 4L = 180m；Goodwin side = 0.7NM = 1296m | 🔴 低   | **需替换为有依据公式**：`max(0.1NM, 4L×scale_factor)`        |
| `405m = 9L`      | 项目船域参考                       | Fujii模型 8L forward；Goodwin 0.45NM astern ≈ 833m；文献船域9L数值罕见 | 🟡 中等 | **接受为 warning domain**，但需说明是"项目定义船域"非文献直接值 |
| `926m = 0.5NM`   | 项目ODD开放水域warning baseline    | NLM: 标准DCPA阈值0.6NM（标准），0.4NM（拥挤水道）；文献collision zone 0.5NM + awareness zone 3.0NM | ✅ 高   | **合理**，0.5NM作为开放水域give-way探针warning基线有多个文献支撑 |

### 4.2 动作幅度阈值来源

| 阈值                         | 当前使用处                               | 来源分析                                                     | 置信度               |
| ---------------------------- | ---------------------------------------- | ------------------------------------------------------------ | -------------------- |
| `30°` full compliance        | `rule_compliance_evaluator.py` L22,36,49 | Rule 8原文无具体度数；文献中"readily apparent" ≥ 30° 有支撑 [NLM high] | 🟡 中等，来源说明缺失 |
| `15°` partial                | `rule_compliance_evaluator.py`           | 文献认为 <15° 视觉上不明显；严格模型要求≥45° full            | 🔴 低，偏宽松         |
| `5°` `min_give_way_turn_deg` | `stability_scorer.py` DEFAULT_THRESHOLDS | 无任何文献或工程依据                                         | 🔴 不可接受           |
| `avoidance_time_s: 25.0`     | 多个YAML `encounter` 字段                | 期望25s内开始避碰动作；Rule 8"及时"通常解释为TCPA>5-10min开始 | 🟡 工程经验，需说明   |

### 4.3 稳定性阈值来源

| 阈值                      | 来源分析                                 | 置信度                 |
| ------------------------- | ---------------------------------------- | ---------------------- |
| `behavior_toggles ≤ 2`    | 设计合理：一上一下 = 正常生命周期        | 🟢 高，有清晰逻辑依据   |
| `plan_valid_segments ≤ 2` | 逻辑合理，一engagement                   | 🟢 高                   |
| `steering_reversals ≤ 4`  | 合理（转入→稳→转回→稳）；但dead band偏大 | 🟡 中等                 |
| `rot_hold_std_dps < 1.5`  | **无来源**；数值可能偏宽                 | 🔴 低                   |
| `premature_giveway < 10°` | 工程折中；对应Rule 17"保持"              | 🟡 中等，需对齐船动力学 |

---

## 第五部分：综合发现

### 5.1 "测试通过但前端不对"的根本原因

1. **评估器测的是结果，不是过程**：当前 `overall_pass = cpa_ok AND stability_pass`。若系统偶然产生足够大的CPA（因为目标船速较慢），即使整个决策过程完全错误，也可以通过。
2. **没有时序门控**：Rule 16 的 "early action" 和 Rule 8 的 "ample time" 在评估器中完全缺失。系统可以在最后10秒猛打舵通过CPA门。
3. **post-pass 阶段惩罚不当**：Heading-On场景中，过船后目标船在本船身后，但若评估器仍在计算"碰撞风险"，会产生假红色；若完全不计，又漏掉真正的post-pass clearance质量问题。
4. **航线回归没有独立门**：`returned_to_route_required: true` 在YAML中定义，但在 `overall_pass` 公式中不参与计算。

### 5.2 优先级排列的问题清单

**P0 - 立即需要修复（阻止假绿）**：
- [ ] `acted_early` 字段从未赋值 → Rule 16 永远无法 full
- [ ] `overall_pass` 缺少 route_return 门和 colregs 门
- [ ] `turn_starboard` 阈值 5° 过低 → 任何微小右偏都视为合规

**P1 - 需要修复（修正误判）**：
- [ ] `heading_change_deg` 需明确时间窗口定义
- [ ] 动态风险分相位（approach/post-pass）实现
- [ ] Rule 17(b) 触发时机验证
- [ ] 300m 阈值需替换或补证据

**P2 - 建议改进（提升可信度）**：
- [ ] 增加 no-action baseline 验证
- [ ] `rot_hold_std_dps` 标定
- [ ] Rule13 past-and-clear 触发点验证
- [ ] `safe_route-left-encounter` 归属明确

---

## 附录：NLM查询结果摘要

**查询1 - Rule 8时序和幅度（高置信度，global:3ef2890b）**：
- **时机(TCPA)**：TCPA 10分钟作为触发计算阈值；量化框架中TCPA<5min被视为"late"
- **幅度(course alteration)**：`readily apparent` → ≥30°；严格合规模型 → ≥45°（<45°直接判违规）；减速合规 → ΔV ≥ 0.5m/s
- **当前项目偏差**：30°作为full阈值尚在文献范围内；15°作为partial偏宽（文献认为<15°视觉不明显）；5°作为`min_give_way_turn_deg`不可接受

**查询2 - 船域模型CPA阈值（高置信度，global:3ef2890b）**：

FCB 45m LOA各模型数值对照：

| 模型                            | Forward            | Side (Beam)        | Astern            |
| ------------------------------- | ------------------ | ------------------ | ----------------- |
| Fujii-Tanaka（标准，10kn）      | 4L = **180m**      | 2.25L = **101m**   | ~2L = **90m**     |
| Fujii（扩展8L）                 | 8L = **360m**      | 4L = **180m**      | -                 |
| Goodwin（固定NM，不随船长缩放） | 0.85NM = **1574m** | 0.70NM = **1296m** | 0.45NM = **833m** |
| Coldwell（受限水域）            | 3L = **135m**      | 0.8L = **36m**     | -                 |

SIL/HIL实践阈值（文献）：
- 开放水域通用DCPA门：0.4NM（繁忙水道）~ 0.6NM（开放水域），对应740m~1111m
- 双圈架构：0.5NM碰撞圈 + 3.0NM COLREGs意识圈
- VO/RRT测试：500~550m分离距离门

项目现有阈值评估（更新后）：
- `185.2m = 0.1NM`：介于Coldwell前向(135m)和Fujii标准前向(180m)之间，受限航道 emergency floor ✅ 合理
- `300m`：介于Fujii标准前向(180m)和Fujii扩展侧向(180m)之间，高于Coldwell，低于Goodwin — corridor折中 🟡 **可接受但需标注**
- `405m = 9L`：无直接文献，介于Fujii扩展(360m)和Goodwin astern(833m)之间 🟡 **项目warning domain，可接受**
- `926m = 0.5NM`：与SIL实践collision zone重合，有明确多源文献支撑 ✅ 合理

**查询3 - Imazu-22场景覆盖要求（高置信度，global:3ef2890b）**：
- Imazu-22是**必须通过的强制基准**（22个基础案例，单船到4船复杂场景）
- **完整验收需要**：22个Imazu + 200个ODD扩展（11规则×4 ODD×5扰动×5种子 = 1100格）+ 蒙特卡洛10,000样本
- **当前8-probe定位**：开发阶段快速探针集（"dev听诊器"），不是验收基准，**定位正确**
- **文献确认的边界角度**：Head-on ±5°~±15°；Crossing 0°~112.5°；Overtaking >22.5° abaft beam（112.5°~247.5°）
- **当前覆盖缺口（对照Imazu-22要求）**：
  - ❌ Rule 19（受限能见度）未覆盖
  - ❌ 左舷穿越（give-way从port side）未覆盖
  - ❌ 纯减速避碰（不转向）未覆盖
  - ❌ 多船冲突（≥2目标船）已知缺口（Imazu范畴）
  - ✅ Head-on ±5°边界：`rule14-ho-port` + `rule15-cs-edge` 夹击正确

---

*报告生成时间：2026-06-16 13:30 UTC+8*

# COLREGs TDL 测试平台完整设计方案 v1.0

**状态**: Draft — 待审批后实施  
**基于**: 8-Probe Trace Evaluator Spec v0.1 + 专家评审报告  
**日期**: 2026-06-16  
**作者**: 航行避碰专家评审 + Antigravity  
**NLM证据来源**: global:3ef2890b (NTNU colav-simulator, high confidence)

---

## 一、设计目标

解决"测试通过但前端看起来不对"的根本问题：

1. **评估过程，不只是结果** — 引入时序门控（什么时候做的动作）
2. **来源可追溯** — 所有数字有文献或工程依据，不裸写魔数
3. **假绿清零** — 堵住 Rule16 `acted_early` 死代码、5°门、无航线回归门等已知漏洞
4. **分层裁决** — `safety_pass`、`mission_pass`、`colregs_pass`、`stability_pass` 四门独立
5. **no-action baseline** — 证明每个场景有真实碰撞威胁

---

## 二、场景集修订（8+1 probe）

### 2.1 保留并更新的8个场景

#### 场景1：`colreg-rule14-ho`（Rule 14 纯正遇）

| 参数                               | 当前值      | 修订值          | 依据                                                         |
| ---------------------------------- | ----------- | --------------- | ------------------------------------------------------------ |
| `cpa_min_m_ge`                     | 185.2       | **185.2**       | 保留；Coldwell前向 3L=135m 和 Fujii标准 4L=180m 之间，corridor emergency floor 合理 |
| `avoidance_time_s`                 | 25.0        | **25.0**        | 保留；TCPA~300s，25s内触发满足Rule 8"ample time"             |
| `avoidance_delta_rad`              | 1.047 (60°) | **1.047 (60°)** | 保留；60° >> Rule 8 30° minimum，明显动作                    |
| `route_return_xte_m_lt`            | 150.0       | **150.0**       | 保留                                                         |
| 新增：`no_action_cpa_m`            | 缺失        | **< 50m**       | 生成时计算验证，确保无动作必碰                               |
| 新增：`avoidance_start_tcpa_min_s` | 缺失        | **> 120s**      | Rule 8 "ample time"最低2min                                  |

**注**：场景设计无需改动，仅YAML新增验证字段。

---

#### 场景2：`colreg-rule14-ho-port`（Rule 14 偏左对遇边界）

| 参数                                    | 当前值 | 修订值          | 依据                 |
| --------------------------------------- | ------ | --------------- | -------------------- |
| `cpa_min_m_ge`                          | 185.2  | **185.2**       | 保留                 |
| 新增：`classification_must_not_flip_to` | 缺失   | **`[Rule15]`**  | 偏左5°不得误判为穿越 |
| 新增：`expected_direction`              | 缺失   | **`STARBOARD`** | Rule 14 明确右转     |

---

#### 场景3：`colreg-rule13-ot`（Rule 13 追越）

> [!CAUTION]
> 此场景存在结构性问题，需要最多修改。

| 参数                               | 当前值         | 修订值                                                     | 依据                                                         |
| ---------------------------------- | -------------- | ---------------------------------------------------------- | ------------------------------------------------------------ |
| 初始距离                           | 0.6 NM（太近） | **1.5 NM**                                                 | Rule 8 "ample time"；14kn追越7kn时TCPA≈(1.5NM/7kn×60)≈13min，满足10min触发阈值 |
| `cpa_min_m_ge`                     | 300.0          | **300.0**                                                  | 保留；Fujii标准前向(180m)与扩展前向(360m)之间，corridor折中合理 |
| `overtake_required`                | false          | **false（保留）**                                          | 受限航道可接受安全跟随                                       |
| 新增：`safe_following_definition`  | 缺失           | `range_m >= 300 for >= 120s`                               | 定义"安全跟随"可接受状态                                     |
| 新增：`rule13d_checkpoint`         | 缺失           | `bearing_forward_of_beam_event: assert_conflict_continues` | Rule 13(d)：方位前移后冲突不得自动释放                       |
| 新增：`avoidance_start_tcpa_min_s` | 缺失           | **> 120s**                                                 | 早动作门                                                     |

---

#### 场景4：`colreg-rule15-cs`（Rule 15 右舷穿越标准）

| 参数                               | 当前值 | 修订值     | 依据                                     |
| ---------------------------------- | ------ | ---------- | ---------------------------------------- |
| `cpa_min_m_ge`                     | 926.0  | **926.0**  | 保留；0.5NM与SIL实践collision zone一致   |
| 新增：`must_pass_astern_of_target` | 缺失   | **true**   | Rule 16要求绕目标船尾部，不得cross ahead |
| 新增：`avoidance_start_tcpa_min_s` | 缺失   | **> 180s** | 开放水域穿越，Rule 8要求更早动作         |

---

#### 场景5：`colreg-rule15-cs-2`（Rule 15 短TCPA穿越）

> [!WARNING]
> 当前YAML中TCPA描述矛盾（描述540s vs 几何计算约240s），需要验证。

| 参数                                   | 当前值 | 修订值    | 依据                                    |
| -------------------------------------- | ------ | --------- | --------------------------------------- |
| `cpa_min_m_ge`                         | 926.0  | **926.0** | 保留                                    |
| 新增：`avoidance_start_tcpa_min_s`     | 缺失   | **> 90s** | 短TCPA场景降低要求，但不能最后10s才行动 |
| 新增：`max_action_delay_from_detect_s` | 缺失   | **< 30s** | 从M6检出到M4开始COLREG_AVOID的最大延迟  |

**待修正**：需要重新计算场景几何验证实际TCPA，更新description。

---

#### 场景6：`colreg-rule15-cs-edge`（正遇/穿越边界）

> [!NOTE]
> 目标船速29.2kn异常，建议降速至12kn，减少高速效应干扰。

| 参数                                      | 当前值 | 修订值         | 依据                                 |
| ----------------------------------------- | ------ | -------------- | ------------------------------------ |
| 目标船速                                  | 29.2kn | **12.0kn**     | 降低速度，聚焦分类稳定性而非速度效应 |
| `cpa_min_m_ge`                            | 300.0  | **300.0**      | 保留                                 |
| 新增：`classification_stability_window_s` | 缺失   | **全程不翻转** | 边界场景核心测试项                   |

---

#### 场景7：`colreg-rule15-ot-boundary`（穿越/追越边界）

| 参数                                      | 当前值 | 修订值         | 依据                                           |
| ----------------------------------------- | ------ | -------------- | ---------------------------------------------- |
| 目标船速                                  | 45.7kn | **15.0kn**     | 降低速度，消除超高速特殊效应；保留108°相对方位 |
| `cpa_min_m_ge`                            | 300.0  | **300.0**      | 保留                                           |
| 新增：`classification_stability_window_s` | 缺失   | **全程不翻转** | 核心测试项                                     |

---

#### 场景8：`colreg-rule17-cr-so`（Rule 17 直航船→17(b)）

> [!CAUTION]
> 此场景的17(b)触发时机验证是最关键的缺失，需要新增触发门控。

| 参数                                         | 当前值 | 修订值    | 依据                                                   |
| -------------------------------------------- | ------ | --------- | ------------------------------------------------------ |
| `cpa_min_m_ge`                               | 185.2  | **185.2** | 保留；in-extremis floor                                |
| 新增：`standon_hold_phase_max_deviation_deg` | 10°    | **10°**   | 与 stability_scorer.py 对齐                            |
| 新增：`rule17b_trigger_cpa_threshold_m`      | 缺失   | **300m**  | 当预测CPA<300m（Fujii扩展前向）且目标不让时，触发17(b) |
| 新增：`rule17b_must_trigger_before_cpa_s`    | 缺失   | **> 60s** | TMR≥60s规范（项目 ODD 规范要求）                       |
| 修改：`total_time`                           | 1200s  | **1200s** | 保留；description写360s有误，以YAML为准                |

---

### 2.2 新增场景

#### 新增场景9：`colreg-rule15-cs-port`（Rule 15 左舷穿越——直航保向）

**目的**：覆盖本船是 stand-on 的穿越场景（目标从本船左舷来）。

```yaml
title: 'Probe: Rule 15 crossing stand-on (port side target)'
encounter:
  rule: Rule15_Port
  give_way_vessel: target   # 本船是 stand-on
  expected_own_action: maintain
cpa_min_m_ge: 185.2          # stand-on in-extremis floor
avoidance_time_s: 399.0      # 前期保持航向
```

**覆盖盲点**：当前8个probe全部是 give-way 角色，缺少左舷穿越下的 stand-on 行为验证。

---

### 2.3 no-action baseline验证合约

每个场景YAML必须新增 `scenario_validity` 字段：

```yaml
scenario_validity:
  no_action_cpa_m_lt: 50.0      # 无动作时CPA必须<50m，证明原始威胁有效
  no_action_tcpa_s_gt: 0.0      # TCPA>0，冲突在未来
  initial_dcpa_m_lt: 100.0      # 初始DCPA<100m，接近纯对遇/追越
```

**实现**：生成器 `gen_colreg_tier12.py` 在生成时自动计算并填写；验证器 `verify_colreg_tier12.py` 运行kinematic模拟确认。

---

## 三、七层评估器完整规格

### Layer 1: Scenario Validity（场景有效性）

```python
class ScenarioValidityLayer:
    """
    Gate: 场景必须有真实碰撞威胁，否则不能用于验证避碰能力。
    """
    def evaluate(self, no_action_sim: KinematicTrace) -> LayerResult:
        # 1. no-action CPA < 50m（无动作必碰）
        # 2. initial DCPA < 100m（起始状态接近碰撞线）
        # 3. TCPA > 0（冲突在未来）
        # 4. ODD域匹配（open_sea / corridor）
        # 5. M2 分类与 expected rule 一致
```

**输出字段**：`validity_pass`, `no_action_cpa_m`, `initial_dcpa_m`, `tcpa_s`, `odd_match`

---

### Layer 2: Safety Floor（安全底线）

```python
class SafetyFloorLayer:
    """
    Gate: 全程最小分离距离不得低于场景阈值。
    硬红线，失败即 overall_pass=False。
    """
    def evaluate(self, trace: Trace, threshold_m: float) -> LayerResult:
        min_sep = min(trace.range_m)
        return LayerResult(
            safety_pass=(min_sep >= threshold_m),
            min_separation_m=min_sep,
            threshold_m=threshold_m,
            threshold_profile=scenario.cpa_acceptance.profile,
            threshold_basis=scenario.cpa_acceptance.basis,
        )
```

**来源说明要求（必须输出）**：

```json
{
  "threshold_m": 926.0,
  "profile": "open_water_warning_0p5nm",
  "basis": "0.5NM collision zone, consistent with SIL practice and multiple literature sources",
  "nm_equivalent": 0.5,
  "loa_multiplier": 20.6,
  "source_confidence": "high",
  "literature_references": [
    "Fujii-Tanaka (1971): 4L=180m forward domain at 10kn",
    "Goodwin (1975): 0.45-0.85NM sector domain",
    "SIL practice: 0.4-0.6NM DCPA for autonomous ship testing"
  ]
}
```

---

### Layer 3: Dynamic Risk（动态风险分相位）

> [!IMPORTANT]
> 这是当前最缺失的层，必须新建实现。

```python
class DynamicRiskLayer:
    """
    分三个相位独立评估风险暴露时间。
    绝不把 post-pass clearance 等同于 collision threat。
    """
    
    def evaluate(self, trace: Trace, scenario: Scenario) -> LayerResult:
        # 相位判定
        phases = self._classify_phases(trace)
        
        # 相位1：Approach Warning (DCPA<2×threshold, TCPA>0, closing)
        approach_warning_s = sum(
            dt for dt, p in phases if p == "APPROACH_WARNING"
        )
        
        # 相位2：Approach Danger (DCPA<threshold, TCPA>0, closing)
        approach_danger_s = sum(
            dt for dt, p in phases if p == "APPROACH_DANGER"
        )
        
        # 相位3：Post-Pass Clearance (TCPA<0, range increasing, target abaft)
        # 注意：不惩罚为 collision threat
        post_pass_exposure_s = sum(
            dt for dt, p in phases if p == "POST_PASS_CLOSE"
        )
        
        return LayerResult(
            approach_warning_s=approach_warning_s,
            approach_danger_s=approach_danger_s,
            post_pass_exposure_s=post_pass_exposure_s,
            approach_danger_pass=(approach_danger_s == 0),  # 硬门：不得进入danger
        )
    
    def _past_and_clear(self, r: Record) -> bool:
        """Rule 8(d): 直到 past-and-clear，才能认为机动义务解除。"""
        return (
            r.tcpa_s < 0 and          # 已过CPA
            r.closing_speed_mps <= 0 and  # 不再接近
            r.target_abaft and         # 目标在本船正横以后
            r.range_m > self.clear_threshold_m  # 距离开始扩大
        )
```

**Rule 13 特殊处理**：追越场景中，`_past_and_clear` 必须额外验证"彻底超越"（目标方位从正前向过到正后，且保持安全距离）。TCPA<0不自动触发。

**Heading-On Post-Pass Rule**（来自Spec §8）：
```
if rule == Rule14 and target_abaft and tcpa < 0 and range_increasing:
    phase = "POST_PASS_CLEARANCE"  # 不计入 approach_danger
    # 但仍计入 post_pass_exposure_s 作为 quality 评分
```

---

### Layer 4: COLREGs Compliance（规则合规——时序化重构）

> [!IMPORTANT]
> 必须重构 `rule_compliance_evaluator.py`，引入时序窗口和 acted_early 实现。

#### 重构后的 `_eval_rule8_timing`（新增基础检查）

```python
def _eval_rule8_timing(
    trace: Trace,
    avoidance_start_t: float,
    cpa_t: float,
    min_heading_change_deg: float = 30.0,
) -> dict:
    """
    Rule 8 时序合规检查。
    来源：文献共识 (NLM high confidence):
      - TCPA触发阈值：10分钟（600s）
      - 'substantial action'：heading change >= 30°
      - 严格合规：heading change >= 45°
    """
    time_before_cpa = cpa_t - avoidance_start_t
    
    # 避碰窗口内的峰值净偏航（排除回归段）
    peak_stbd_turn = max_heading_dev_in_avoidance_window(trace)
    
    return {
        "time_before_cpa_s": time_before_cpa,
        "early_action": time_before_cpa >= 120.0,     # >=2min前开始
        "timely_action": time_before_cpa >= 60.0,     # >=1min前开始（最低）
        "substantial_action_30": peak_stbd_turn >= 30.0,   # readily apparent
        "substantial_action_45": peak_stbd_turn >= 45.0,   # strict
        "peak_course_change_deg": peak_stbd_turn,
    }
```

#### 重构后的各规则评估函数

**Rule 14**（对遇）：
```python
def _eval_rule14(s: dict) -> str:
    # s 包含时序化字段
    if s.get("rudder_side") == "port":
        return "violated"  # 左转=直接违规
    
    timing = s.get("rule8_timing", {})
    substantial = timing.get("substantial_action_30", False)
    early = timing.get("early_action", False)
    cpa_ok = s.get("cpa_m", 0) >= s.get("cpa_target_m", 185.2)
    
    if substantial and early and cpa_ok:
        return "full"
    if timing.get("timely_action") and s.get("peak_course_change_deg", 0) >= 15.0:
        return "partial"
    return "violated"
```

**Rule 15 + Rule 16**（穿越，重点修复 acted_early）：
```python
def _eval_rule15_rule16(s: dict) -> str:
    timing = s.get("rule8_timing", {})
    # Rule 16: early action = 在TCPA >= 3min时开始
    acted_early = timing.get("time_before_cpa_s", 0) >= 180.0
    
    substantial = timing.get("substantial_action_30", False)
    cpa_ok = s.get("cpa_m", 0) >= s.get("cpa_target_m", 926.0)
    passed_astern = s.get("passed_astern_of_target", True)
    
    if substantial and acted_early and cpa_ok and passed_astern:
        return "full"
    if timing.get("timely_action") and s.get("peak_course_change_deg", 0) >= 15.0:
        return "partial"
    return "violated"
```

**Rule 17**（直航→17(b)，重点：触发时机验证）：
```python
def _eval_rule17(s: dict) -> str:
    stage = s.get("timing_stage")
    max_hold_deviation = s.get("max_hold_deviation_deg", 999)
    
    # Stand-on hold phase
    if stage in ("STAGE_1", "STAGE_2"):
        if max_hold_deviation < 5.0:
            return "full"
        if max_hold_deviation < 10.0:
            return "partial"
        return "violated"  # 提前大转向
    
    # Rule 17(b) independent action phase
    if stage == "STAGE_3":
        hc = abs(s.get("peak_course_change_in_action_phase_deg", 0))
        trigger_time_before_cpa = s.get("rule17b_trigger_time_before_cpa_s", 0)
        
        # 必须在CPA前60s以上触发（TMR≥60s项目规范）
        timely_trigger = trigger_time_before_cpa >= 60.0
        
        if hc >= 30.0 and timely_trigger:
            return "full"
        if hc >= 15.0:
            return "partial"
        return "partial"  # 小动作总比碰撞好
    
    return "violated"
```

---

### Layer 5: Route Recovery（航线回归）

**现状**：YAML有字段，`overall_pass` 中不参与。

**修复**：加入 `mission_pass` 门：

```python
class RouteRecoveryLayer:
    def evaluate(self, trace: Trace, scenario: Scenario) -> LayerResult:
        # 1. 回归XTE门：最终XTE < route_return_xte_m_lt
        final_xte = trace.final_xte_m
        returned = final_xte < scenario.expected_outcome.route_return_xte_m_lt
        
        # 2. 走廊门：全程最大XTE < route_corridor_pass_limit_m
        in_corridor = trace.max_xte_m < scenario.expected_outcome.route_corridor_pass_limit_m
        
        # 3. 回归时机门：避碰结束后X分钟内回归
        recovery_time = trace.route_recovery_time_s
        
        return LayerResult(
            mission_pass=(returned and in_corridor),
            final_xte_m=final_xte,
            max_xte_m=trace.max_xte_m,
            recovery_time_s=recovery_time,
        )
```

---

### Layer 6: Seamanship / Efficiency（船艺效率——新建）

```python
class SeamanshipLayer:
    """
    检查路径效率和操船质量。
    目标：不只"安全"，还要"像好的船长开的"。
    """
    def evaluate(self, trace: Trace, no_action_trace: Trace) -> LayerResult:
        # 1. 路径比：实际路径 / 直线路径（越接近1越好）
        path_ratio = trace.total_path_m / no_action_trace.straight_path_m
        
        # 2. 过冲量：避碰峰值转向 vs 最终归航转向的差值
        overshoot_deg = trace.peak_avoidance_turn_deg - trace.final_course_change_deg
        
        # 3. 集成XTE（全程横向偏差面积）
        integrated_xte = sum(abs(xte) * dt for xte, dt in trace.xte_timeseries)
        
        # 4. 无掉头追逐（Rule 13专用：无反向加速追逐动作）
        no_hunt = not self._detect_hunting_behavior(trace)
        
        return LayerResult(
            path_ratio=path_ratio,
            overshoot_deg=overshoot_deg,
            integrated_xte_m_s=integrated_xte,
            no_hunting=no_hunt,
            seamanship_score=self._compute_score(path_ratio, overshoot_deg, integrated_xte),
        )
```

**注**：L6输出`seamanship_score`，是质量评分，**不作为独立硬门**，但输入到 `risk_quality_score` 综合评分。

---

### Layer 7: Stability / Solver Health（稳定性——修订阈值）

基于当前 `stability_scorer.py`，修订以下阈值：

| KPI                             | 当前阈值 | 修订值    | 修订依据                                                     |
| ------------------------------- | -------- | --------- | ------------------------------------------------------------ |
| `behavior_toggles`              | ≤ 2      | **≤ 2**   | 保留；设计合理                                               |
| `plan_valid_segments`           | ≤ 2      | **≤ 2**   | 保留                                                         |
| `steering_reversals` (give-way) | ≤ 4      | **≤ 4**   | 保留                                                         |
| `steering_reversals` (stand-on) | ≤ 5      | **≤ 5**   | 保留                                                         |
| `rot_hold_std_dps`              | < 1.5    | **< 0.8** | FCB 45m/12kn 正常稳定yaw rate std估算 < 0.3°/s，1.5太宽，0.8保留工程余量 |
| `rot_deadband_dps`              | 1.0      | **0.5**   | 降低死区，减少漏检真实fishtail                               |
| `min_give_way_turn_deg`         | **5.0°** | **30.0°** | 对齐Rule 8 "readily apparent" ≥30°，5°不可接受               |
| `conflict_toggles`              | ≤ 2      | **≤ 2**   | 保留                                                         |
| `role_onset_changes`            | 0        | **0**     | 保留；Rule 13(d) 义务不得翻转                                |
| `premature_giveway_deg`         | < 10°    | **< 10°** | 保留；对应Rule 17 hold phase                                 |

---

## 四、综合裁决机制

### 4.1 四门独立判决

```python
@dataclass
class OverallVerdict:
    # 门1：安全底线（硬红线）
    safety_pass: bool    # Layer2(cpa_floor) AND Layer3(approach_danger==0) AND NOT M7.critical_veto
    
    # 门2：任务完成（航线回归）
    mission_pass: bool   # Layer5.returned_to_route AND Layer5.in_corridor
    
    # 门3：COLREGs规则合规
    colregs_pass: bool   # Layer4.all_rules_full_or_partial AND Layer1.classification_stable
    
    # 门4：行为稳定性
    stability_pass: bool # Layer7.all_checks_pass
    
    # 综合门：四门全绿才算真正PASS
    overall_pass: bool   = field(init=False)
    
    # 质量评分（不替代硬门，只做附加输出）
    risk_quality_score: float  # 0-100
    
    def __post_init__(self):
        self.overall_pass = (
            self.safety_pass and 
            self.mission_pass and 
            self.colregs_pass and 
            self.stability_pass
        )
```

### 4.2 三色报告输出

```
[scenario_id]          [overall_pass]
  Safety:     [GREEN/RED]  min_sep=XXm (threshold=XXm)
  Mission:    [GREEN/RED]  final_xte=XXm, max_xte=XXm
  COLREGs:    [GREEN/YELLOW/RED]  Rule14: full, Rule8-timing: timely+substantial
  Stability:  [GREEN/RED]  toggles=X, reversals=X
  Quality:    [score/100]  path_ratio=X.XX, seamanship=XX
  
  First failure at: t=XXs [field: XXX]
```

### 4.3 `risk_quality_score` 计算

```
risk_quality_score = 
  safety_margin_score(0-25) +     # min_sep / ideal_domain 百分比
  timing_score(0-25) +            # time_before_cpa_s归一化
  efficiency_score(0-25) +        # path_ratio, integrated_xte
  stability_score(0-25)           # toggle/reversal余量
```

---

## 五、CPA阈值体系（修订后正式版）

### 5.1 阈值定义表（所有数字有来源）

| 配置名                      | 阈值(m)   | NM等值  | LOA倍数 | 文献依据                                                     | 适用场景                             | 置信度                                         |
| --------------------------- | --------- | ------- | ------- | ------------------------------------------------------------ | ------------------------------------ | ---------------------------------------------- |
| `corridor_emergency_floor`  | **185.2** | 0.1NM   | 4.1L    | Coldwell受限水域前向3L=135m；Fujii标准前向4L=180m；0.1NM EMSA/IALA受限航道紧急阈 | Rule14 corridor; Rule17 in-extremis  | 🟡 中等（工程折中，受限航道context合理）        |
| `corridor_boundary_probe`   | **300**   | 0.162NM | 6.7L    | Fujii扩展前向8L=360m的83%；高于Fujii标准4L=180m；corridor内压缩值 | Rule13追越corridor; R15/R13边界probe | 🟡 中等（Fujii和Goodwin之间的项目折中，需标注） |
| `corridor_warning_domain`   | **405**   | 0.219NM | 9.0L    | 介于Fujii扩展(360m)和Goodwin astern(833m)之间；项目警戒船域  | 理想domain参考；不强制作为floor      | 🟡 中等（项目定义，非文献直接值）               |
| `open_water_collision_zone` | **926**   | 0.5NM   | 20.6L   | 多源SIL实践collision zone；Goodwin side(1296m)的71%；文献DCPA标准0.4-0.6NM区间内 | R15/R16 开放水域 give-way probe      | ✅ 高（多文献支撑）                             |

### 5.2 阈值选择决策树

```
场景类型?
├─ 开放水域 + give-way
│   └─ 使用 open_water_collision_zone (926m)
├─ corridor内 + in-extremis (Rule17, 近距Rule14)
│   └─ 使用 corridor_emergency_floor (185.2m)
├─ corridor内 + 追越/边界分类probe
│   └─ 使用 corridor_boundary_probe (300m)
└─ 全场景 warning domain (非floor)
    └─ 使用 corridor_warning_domain (405m) 作为 ideal_domain_m 参考输出
```

---

## 六、Rule 8 时序合规阈值（修订）

| 参数                            | 修订阈值           | 文献依据                                              | 置信度                                  |
| ------------------------------- | ------------------ | ----------------------------------------------------- | --------------------------------------- |
| `substantial_action_full_deg`   | **≥ 30°**          | NLM high: "readily apparent" ≥30°；文献共识           | 🟡 中等                                  |
| `substantial_action_strict_deg` | ≥ 45°              | NLM high: 严格合规模型 <45°=违规                      | 🟡 中等（作为质量分，不作为全/部分门）   |
| `partial_action_min_deg`        | **≥ 15°**          | 保留partial门，但明示"视觉不明显"                     | 🔴 低（偏宽，实际中15°很难被目标船察觉） |
| `early_action_tcpa_s`           | **≥ 120s（2min）** | Rule 8 "ample time"；量化框架TCPA 10min触发，2min最低 | 🟡 中等（工程取保守值）                  |
| `timely_action_min_tcpa_s`      | **≥ 60s（1min）**  | TMR ≥ 60s 项目规范                                    | ✅ 项目规范（非文献）                    |
| `rule17b_trigger_before_cpa_s`  | **≥ 60s**          | 项目TMR≥60s ODD规范；CPA<300m时触发                   | ✅ 项目规范                              |

---

## 七、实施路线图

### Phase 1（立即修复，P0 - 一周内）

| 任务                              | 文件                                     | 改动                                             |
| --------------------------------- | ---------------------------------------- | ------------------------------------------------ |
| 修复 `acted_early` 死代码         | `rule_compliance_evaluator.py`           | 从 trace 计算 `time_before_cpa_s`，传入evaluator |
| 修复 `min_give_way_turn_deg`      | `stability_scorer.py` DEFAULT_THRESHOLDS | 5.0 → 30.0                                       |
| `overall_pass` 加 mission_pass 门 | `run_6_scenarios.py` / batch runner      | `cpa_ok AND stability_pass AND route_return`     |
| `heading_change_deg` 明确时间窗口 | `rule_compliance_evaluator.py`           | 改为avoidance window内的峰值净偏航               |

### Phase 2（核心修复，P1 - 两周内）

| 任务                                     | 文件                           | 改动                                        |
| ---------------------------------------- | ------------------------------ | ------------------------------------------- |
| 实现 Layer 3 动态风险分相位              | 新建 `dynamic_risk_layer.py`   | approach/post-pass相位分离                  |
| 实现 Rule 17(b) 触发时机验证             | `rule_compliance_evaluator.py` | 新增 STAGE_3 `trigger_time_before_cpa_s` 门 |
| 降低 `rot_deadband_dps`                  | `stability_scorer.py`          | 1.0 → 0.5                                   |
| 降低 `rot_hold_std_dps` 阈值             | `stability_scorer.py`          | 1.5 → 0.8                                   |
| 修正 `colreg-rule13-ot` 初始距离         | `gen_colreg_tier12.py`         | 0.6NM → 1.5NM                               |
| 降低 `cs-edge` 和 `ot-boundary` 目标船速 | `gen_colreg_tier12.py`         | 29.2kn/45.7kn → 12.0kn/15.0kn               |

### Phase 3（完整性，P2 - 一个月内）

| 任务                           | 文件                         | 改动                                  |
| ------------------------------ | ---------------------------- | ------------------------------------- |
| 实现 no-action baseline runner | 新建 `no_action_baseline.py` | kinematic验证每个场景原始威胁有效     |
| 实现 Layer 6 船艺效率          | 新建 `seamanship_layer.py`   | path_ratio, overshoot, integrated_xte |
| 新增 `rule15-cs-port` 场景     | `gen_colreg_tier12.py`       | 左舷穿越直航保向                      |
| 完整 `OverallVerdict` 四门输出 | 更新 batch runner            | safety/mission/colregs/stability 四门 |
| `rot_hold_std_dps` 实测校准    | A4000上跑基线场景            | 用真实trace数据校准1.5→X              |

---

## 八、验证计划

### 8.1 回归验证命令

```bash
# 1. 重生成修正后场景
python -m tools.sil.gen_colreg_tier12

# 2. schema + kinematic 验证（含 no-action baseline）
python -m tools.sil.verify_colreg_tier12 --with-no-action-baseline

# 3. 单元测试（含修复后的 stability_scorer）
pytest tests/sim_workbench/scoring/ -v

# 4. 本地门（OrbStack）
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

### 8.2 修复前后对比验证

修复后，以下场景应从"假绿"转为正确判断：
- Rule 16 `acted_early` 修复后：late-action场景应降为 `partial`
- `min_give_way_turn_deg` 修复后：5°微小右转应从 `turn_starboard: PASS` 变为 `FAIL`
- `overall_pass` 加路由回归门后：不归航场景应从 `PASS` 变为 `FAIL`

---

## 九、开放问题（需要确认后才能固化）

1. **`rot_hold_std_dps` 实测基准**：建议在A4000上跑一个"无冲突直航"场景，测量正常稳定航行的ROT标准差，作为阈值校准依据。

2. **`rule17b_trigger_cpa_threshold_m` = 300m 是否合适**：若用Fujii扩展(360m)是否更有文献依据？还是项目取300m更保守？

3. **`safe_following_definition`（追越场景）**：`range_m >= 300 for >= 120s` 是否足够？还是需要加方位稳定性条件（方位变化 < 5°/min）？

4. **`colreg-rule15-cs-2` TCPA矛盾**：需要重新计算几何，确认实际TCPA是240s还是540s，更新description。

5. **`colreg-rule15-cs-port`（新增场景）的CPA阈值**：left-side crossing stand-on，CPA应用 `standon_in_extremis_0p1nm` (185.2m) 还是更大的值？

---

*设计文档版本：v1.0 | 日期：2026-06-16 | 状态：请审批后实施*