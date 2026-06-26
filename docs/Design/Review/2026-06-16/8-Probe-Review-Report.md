# COLREGs 8-Probe 测试平台评审报告

**评审人**: Sisyphus (航行避碰专家视角)
**日期**: 2026-06-16
**评审对象**:
- `scenarios/COLREGs测试/` — 8 探针场景集
- `docs/Design/Review/8-Probe Trace Evaluator Spec.md` — 7 层评估器 Spec v0.1
- `scripts/run_6_scenarios.py` — 批量运行器
- `src/sim_workbench/sil_nodes/scoring/` — 评分代码

**评审方法**: 对照 COLREGs 法规原文、船域模型学术文献、AIS 实证数据、海事判例法、MASS 项目实践进行逐项溯源。

---

## 1. 架构设计评审

### 1.1 七层评估器结构

| 层 | Spec 提议 | 当前实现状态 | 评审结论 |
|---|---|---|---|
| L1 场景有效性 | no-action DCPA/TCPA 验证 | `verify_colreg_tier12.py` 检查 DCPA<500m；`scenario_spec.py` 有几何计算 | ✅ 基本覆盖，但缺"无冲突场景不能证明避碰能力"的显式断言 |
| L2 安全底线 | min_separation >= cpa_threshold | `run_6_scenarios.py:cpa_ok` | ✅ 已实现 |
| L3 动态风险 | approach/post-pass 风险拆分 | `compute_risk_metrics()` 有 warning/danger exposure，但**不区分 approach vs post-pass** | ⚠️ 部分实现，缺 post-pass 独立计量 |
| L4 COLREG 合规 | 按规则独立评估 | `rule_compliance_evaluator.py` — 仅查 heading_change + CPA，**不查转向方向、时机、cross-ahead** | 🔴 过于粗糙 |
| L5 航线回归 | returned_to_route + XTE + corridor | `compute_route_return_status()` | ✅ 已实现 |
| L6 船艺/效率 | path ratio, XTE 积分, overshoot | `compute_seamanship_metrics()` | ✅ 已实现 |
| L7 稳定性/求解器 | toggles, reversals, solver health | `stability_scorer.py` — **最成熟层** | ✅ 优秀 |

**架构总评**: 七层分层概念正确，与海事评估文献中的分层方法一致（安全→合规→效率→稳定性）。但当前实现与 Spec 之间存在显著差距，特别是 L3 和 L4。

**来源**: MAXCMAS 项目使用类似分层评估（目标获取→风险判断→早大动作→安全 CPA/TCPA→回归航线），见 MDPI J. Mar. Sci. Eng. 2022, 10(1), 3。
**置信度**: 🟢 高

### 1.2 关键架构问题

#### 问题 A: 评估器与运行器耦合

`run_6_scenarios.py` 既是场景运行器又是评估器（1186 行），评估逻辑散布在 `compute_risk_metrics()`、`compute_seamanship_metrics()`、`compute_domain_gate_status()`、`compute_overall_pass()` 等多个函数中。Spec 提议的七层评估器应独立于运行器。

**建议**: 将评估逻辑抽取为独立的 `TraceEvaluator` 类，接受 trace 数据，输出结构化报告。

#### 问题 B: HagenScorer 与七层的映射不清

`HagenScorer` 有 6 个加权维度（安全 0.30、合规 0.25、延迟 0.12、幅度 0.08、阶段 0.15、合理性 0.10），与七层评估器存在概念重叠但又不完全对应。Spec 未说明 HagenScorer 是保留还是替换。

**建议**: 明确 HagenScorer 定位为"帧级实时质量分"（用于 M8 透明度展示），七层评估器为"后处理验收判定"（用于 CI/CD gate）。两者不互相替代。

#### 问题 C: Overall Verdict 过于扁平

当前 `compute_overall_pass()` 是 7 个布尔条件的简单 AND。Spec 提议的三层结论（safety_pass / mission_pass / colregs_pass）更合理，但未实现。

**问题**: 一个 post-pass domain exposure 超限会导致整体 RED，即使避碰动作本身完全正确。这导致"测试通过但前端 trace 看起来不合理"的反向问题——某些场景因宽松阈值通过，但实际行为有问题。

**来源**: Cockcroft & Lameijer, *A Guide to the Collision Avoidance Rules*, 6th ed. (2003) — 避碰评估应区分安全、合规、效率三个独立维度。
**置信度**: 🟢 高

#### 问题 D: 缺少 No-Action Baseline Trace

Spec §10 第 6 问提出"是否应补 no-action baseline trace"。当前 `verify_colreg_tier12.py` 仅做几何验证（DCPA<500m），不运行无动作仿真。

**建议**: 每个场景应有一个 no-action baseline run，证明"不避就碰"。这是证明场景有效性的黄金标准。

**来源**: Imazu-22 标准测试集要求 no-action DCPA ≈ 0 作为场景有效性前提。
**置信度**: 🟢 高

---

## 2. 场景设计评审

### 2.1 覆盖度分析

| COLREGs 规则 | 场景覆盖 | 评审 |
|---|---|---|
| Rule 13 追越 | `rule13-ot` (1 场景) | ⚠️ 仅测正后方追越，未测斜后方追越（112.5°附近） |
| Rule 14 对遇 | `rule14-ho` + `rule14-ho-port` (2 场景) | ✅ 纯正遇 + 偏左边界，覆盖良好 |
| Rule 15 穿越 | `rule15-cs` + `rule15-cs-2` + `rule15-cs-edge` + `rule15-ot-boundary` (4 场景) | ✅ 标准穿越 + 短 TCPA + 两条边界 |
| Rule 16 让路船动作 | 隐含在 R15 场景中 | ⚠️ 无独立"早而明显动作"量化测试 |
| Rule 17 直航船 | `rule17-cr-so` (1 场景) | ⚠️ 仅测左舷来船，未测右舷来船（更危险） |
| Rule 8 一般条款 | 隐含在各场景中 | ⚠️ 无"连续小改向"反面测试 |
| Rule 19 受限能见度 | 无 | 🔴 完全缺失（需 harness 改动，已知） |
| 多船场景 | 无 | 🔴 完全缺失（已知，属 Imazu-22 范围） |

**覆盖度总评**: 对于单船单规则的"听诊器"定位，8 探针覆盖合理。但存在以下盲区：

### 2.2 关键场景问题

#### 问题 E: 缺少左舷穿越（Port Crossing, OS give-way）

当前所有穿越场景都是右舷来船（OS give-way）。但 Rule 15 下，如果目标从左舷穿越且 OS 是 give-way（例如目标航向使 OS 处于其右舷），这种场景完全缺失。

**实际影响**: 左舷穿越是实际航行中最容易出错的场景之一，因为 give-way 船需要向左转向（绕目标船尾），但 Rule 17(c) 禁止直航船向左转向。

**建议**: 补充 `colreg-rule15-cs-port` 场景。

#### 问题 F: Rule 17 仅测左舷来船

`rule17-cr-so` 中目标从左舷（rel_brg 315°）接近。但 Rule 17(c) 的关键约束——"不得对左舷船向左转"——在这个几何下不会被触发（因为目标在左舷，OS 不应左转，而应右转）。

**建议**: 补充右舷来船的 stand-on 场景，测试 Rule 17(c) 约束。

#### 问题 G: 目标速度不现实

| 场景 | 目标速度 | 问题 |
|---|---|---|
| `rule15-cs-edge` | 29.21 kn | 目标船速是 OS 的 2.4 倍，现实中极少见 |
| `rule15-ot-boundary` | 45.74 kn | 目标船速是 OS 的 4.6 倍，几乎不可能 |

**影响**: 这些极端速度比是为了制造 DCPA≈0 的几何条件，但会导致避碰动作的物理可行性失真。一个 12 kn 的货船面对 45 kn 的目标，避碰窗口极短，不代表真实航行场景。

**建议**: 在 README 中明确标注这些场景为"几何压力测试"而非"真实场景模拟"，并在评估时考虑速度比的合理性。

#### 问题 H: 所有场景零风零流

所有 8 个场景的 `wind` 和 `current` 均为 0。真实航行中风流对船舶操纵有显著影响，特别是低速追越场景。

**建议**: 至少在 `rule13-ot`（追越，低速差）中加入中等风流条件。

#### 问题 I: README CPA 期望值与 YAML 不一致

README 中 `rule14-ho` 和 `rule14-ho-port` 的 CPA 期望写的是 `≥275 m`，但 YAML 中 `cpa_min_m_ge` 是 `185.2`。这个不一致会导致人工对照时产生混淆。

**来源**: 直接对比 `scenarios/COLREGs测试/README.md` 第 154 行与 `colreg-rule14-ho.yaml` 第 69 行。
**置信度**: 🟢 确定

### 2.3 场景几何验证

| 场景 | 初始距离 | 初始相对方位 | 目标航向 | DCPA(no-action) | 验证 |
|---|---|---|---|---|---|
| `rule14-ho` | 2.0 NM | 0° (正前方) | 180° | ≈0 | ✅ |
| `rule14-ho-port` | 2.0 NM | ~355° (偏左5°) | 170° | ≈0 | ✅ |
| `rule13-ot` | 0.6 NM | ~3° | 0° (同向) | ≈0 | ✅ |
| `rule15-cs` | ~2.0 NM | ~50° | 290° | ≈0 | ✅ |
| `rule15-cs-2` | 1.0 NM | ~60° | 300° | ≈0 | ✅ |
| `rule15-cs-edge` | ~2.0 NM | ~25° | 215° | ≈0 | ✅ |
| `rule15-ot-boundary` | ~2.0 NM | ~108° | 300° | ≈0 | ✅ |
| `rule17-cr-so` | ~2.0 NM | ~315° | 90° | ≈0 | ✅ |

**几何总评**: 所有场景的 no-action DCPA≈0 设计正确，确保"不避就碰"。边界场景的方位选择（25° 和 108°）合理地位于分类边界附近。

**来源**: COLREGs Rule 13(b) 定义追越边界为 22.5° abaft beam = 112.5° from bow；Rule 14(b) 定义对遇为 "ahead or nearly ahead"（±6°）。
**置信度**: 🟢 高

---

## 3. 评价指标评审

### 3.1 当前指标体系

当前 `run_6_scenarios.py` 的 overall_pass 由以下条件 AND 组成：

1. `cpa_ok`: min_DCPA >= cpa_floor（场景配置值）
2. `stability_pass`: 8 项稳定性断言全通过
3. `returned_to_route`: 最终 XTE<150m 且航向偏差<10°
4. `route_corridor_ok`: max_XTE < 500m（或场景配置值）
5. `overtake_completed`: 如果要求追越
6. `risk_gate_ok`: danger_domain + warning_domain + danger_ddv + risk_recovery + threat_switch
7. `seamanship_gate_ok`: integrated_XTE + route_crossing + path_length

### 3.2 指标有效性分析

#### ✅ 有效指标（能反映真实航行）

| 指标 | 来源 | 有效性 |
|---|---|---|
| `min_DCPA >= threshold` | 通用海事实践 | 🟢 核心安全指标 |
| `behavior_toggles ≤ 2` | 工程经验（M6 fishtail 回归锁） | 🟢 捕捉行为抖动 |
| `role_onset_changes = 0` | COLREGs Rule 13(d) "once overtaking, always overtaking" | 🟢 法规直接要求 |
| `turn_starboard` (give-way) | COLREGs Rules 14/15/16 | 🟢 法规直接要求 |
| `premature_giveway < 10°` | COLREGs Rule 17(a)(i) "shall keep course and speed" | 🟢 法规直接要求 |
| `path_length_ratio ≤ 1.35` | 效率指标，η = d₁/d₂ | 🟡 合理但阈值需验证 |
| `route_crossing_overshoot ≤ 1` | 航海实践（不应反复穿越航线） | 🟢 合理 |

#### ⚠️ 有问题的指标

| 指标 | 问题 | 建议 |
|---|---|---|
| `warning_domain_exposure_s ≤ 120s` | 120 秒的 warning 暴露时间对于近距起步场景（2 NM，对遇 12+12 kn，TCPA≈300s）来说太宽松——几乎不可能超过 | 降低到 60s 或按场景配置 |
| `danger_domain_exposure_s ≤ 0` | 零容忍 danger 暴露，但 `close_start_emergency_allowed` 场景允许 danger_ddv > 0。逻辑不一致 | 统一：要么都允许，要么都不允许 |
| `steering_reversals ≤ 4` (give-way) | 4 次反转对于 give-way 来说太宽松。一个正常的右转→通过→回航只需要 2 次反转（turn-in + return）。4 次意味着允许一次额外的 fishtail | 降低到 ≤ 3 |
| `max_rot_hold_std_dps < 1.5` | 1.5°/s 的偏航率标准差对于"保持段"来说偏高。一个稳定的转向应该 < 0.5°/s | 降低到 < 0.8 |
| `integrated_abs_xte ≤ 500×600 = 300,000 m·s` | 这个阈值意味着可以在 500m XTE 下持续 600 秒，或 300m XTE 下持续 1000 秒。对于 4-7 分钟的场景来说太宽松 | 降低到 100,000 m·s 或按场景时长归一化 |

#### 🔴 缺失的关键指标

| 缺失指标 | 为什么重要 | 来源 |
|---|---|---|
| **Cross-ahead 检测** | Rule 15 明确要求 "avoid crossing ahead of the other vessel"。当前评估不检查 OS 是否从目标船前方穿过 | COLREGs Rule 15 原文 |
| **动作时机 (Reaction Time)** | Rule 8(b)/16 要求 "early and substantial"。当前不量化"早"——只查 CPA 结果 | COLREGs Rule 8(b), 16 |
| **速度变化评估** | COLREGs 允许速度变化作为避碰手段（Rule 8(a)），但当前只评估航向变化 | COLREGs Rule 8(a) |
| **Post-pass 距离趋势** | Spec §8 提出 post-pass 应检查距离是否增加，但当前不实现 | Spec §8, 良好船艺 |
| **Approach vs Post-pass 风险拆分** | Spec §6 Layer 3 提出拆分，但当前 `compute_risk_metrics()` 不区分 | Spec §6 |
| **M7 veto 严重性** | 当前只计数 veto 事件，不区分严重性。M7 critical veto 应该是 hard fail | M7 设计文档 |

**来源**:
- Cross-ahead: COLREGs Rule 15 — "shall, if the circumstances of the case admit, avoid crossing ahead of the other vessel"
- Reaction time: COLREGs Rule 16 — "take early and substantial action to keep well clear"
- Speed changes: COLREGs Rule 8(a) — "Any action taken to avoid collision shall... be such as to result in passing at a safe distance" (includes speed changes)
**置信度**: 🟢 高

### 3.3 指标与前端 trace 不一致的根因分析

用户报告"测试通过但前端 trace 看起来不合理"。根据代码分析，根因是：

1. **CPA 阈值过低**: 185.2m (0.1 NM) 作为 pass 阈值，意味着两船可以在 185m 内通过仍然 PASS。对于 45m LOA 的船来说，这只有 4.1 倍船长——在前端 trace 上看起来非常危险。
2. **Risk exposure 阈值过宽**: warning_domain_exposure ≤ 120s 允许长时间处于 warning 状态。
3. **缺少过程指标**: 只查结果（CPA、XTE），不查过程（是否从船尾绕过、是否早动作、是否平稳）。
4. **Post-pass 不计风险**: 对遇通过后，如果两船距离仍然很近（比如 200m），当前不计入任何风险指标。

---

## 4. 阈值来源与置信度评审

### 4.1 CPA 阈值

| 阈值 | 值 | 声称来源 | 实际来源验证 | 置信度 |
|---|---|---|---|---|
| **185.2m** | 0.1 NM | "紧急下限" | Davis (1980) 船域横向间距 ~0.14 NM ≈ 259m；North Sea AIS 异常对遇阈值 0.35 NM (van Iperen 2015)。**0.1 NM 低于所有学术文献中的安全距离**。作为 danger/immediate-threat 阈值合理，作为 pass 阈值过于激进 | 🔴 低 — 作为 pass 阈值缺乏支撑 |
| **300m** | 0.162 NM | "0.1 NM 与 9L 之间折中" | Fujii 狭窄水道半短轴 ~3L ≈ 135m (45m 船)；Sawada 禁区 0.3 NM ≈ 555m。**300m 不直接对应任何权威来源** | 🔴 低 — 工程折中，需补证据 |
| **405m** | 9×LOA | "船域参考" | MASS 法律程序论文 (MDPI 2023, 11(10), 1850) 从 IMO 操纵性标准推导：advance < 4.5L，双船会遇 = 9L = "last-chance turning distance"。Fujii 开放水域半长轴 8L ≈ 360m | 🟡 中 — 有学术支撑但作为 ideal domain 而非 pass 阈值 |
| **926m** | 0.5 NM | "开放水域 warning 基线" | Wang (2009) 对遇安全域 0.5-0.6 NM；Goodwin Sector 1 右舷 0.85 NM；Davis 圆域 0.675 NM；多篇 MASS 论文使用 0.5 NM 作为安全域半径 | 🟢 高 — 最广泛支撑的阈值 |

**关键发现**: 185.2m (0.1 NM) 作为 pass 阈值是最大的问题。学术文献中：
- 0.35 NM (648m) 是 North Sea AIS 数据中"异常"对遇的下限
- 0.3 NM (555m) 是 Sawada 实验中的禁区
- 0.5 NM (926m) 是多数 MASS 项目的安全域基线
- 0.1 NM (185m) 在所有文献中都属于"immediate danger"级别

**建议**: 185.2m 应作为 danger floor（硬红线，触碰即 RED），不应作为 pass 阈值。Pass 阈值应至少为 300m（受限航道）或 500m（开放水域）。

### 4.2 XTE 阈值

| 阈值 | 值 | 来源 | 置信度 |
|---|---|---|---|
| `route_return_xte < 150m` | 150m | 项目工程阈值（3.3L） | 🟡 中 — 合理但无外部来源 |
| `route_corridor_pass_limit < 500m` | 500m | L2 安全航道约束 | 🟡 中 — 项目 ODD 定义 |
| `route_corridor_half_width = 1000m` | 1000m | L2 给出的航道半宽 | 🟡 中 — 项目 ODD 定义 |
| `route_return_heading < 10°` | 10° | 航海实践 | 🟢 高 — 标准航向恢复判据 |

### 4.3 稳定性阈值

| 阈值 | 值 | 来源 | 置信度 |
|---|---|---|---|
| `behavior_toggles ≤ 2` | 2 | 工程经验（一起一落） | 🟡 中 — 合理但无外部来源 |
| `steering_reversals ≤ 4` (give-way) | 4 | 工程经验 | 🟡 中 — 偏宽松 |
| `steering_reversals ≤ 5` (stand-on) | 5 | 工程经验 | 🟡 中 — 偏宽松 |
| `rot_hold_std < 1.5°/s` | 1.5 | 工程经验 | 🟡 中 — 偏宽松 |
| `conflict_toggles ≤ 2` | 2 | 工程经验 | 🟡 中 — 合理 |
| `role_onset_changes = 0` | 0 | COLREGs Rule 13(d) | 🟢 高 — 法规要求 |
| `premature_giveway < 10°` | 10° | 航海实践 | 🟢 高 — 合理 |
| `min_give_way_turn ≥ 5°` | 5° | 工程经验 | 🟡 中 — Rule 8(b) 要求"readily apparent"，5° 可能不够 |

### 4.4 风险域阈值

| 阈值 | 值 | 来源 | 置信度 |
|---|---|---|---|
| `MAX_WARNING_DOMAIN_EXPOSURE_S = 120` | 120s | 工程折中 | 🔴 低 — 无外部来源，偏宽松 |
| `MAX_INTEGRATED_ABS_XTE = 300,000 m·s` | 300k | 500m × 600s | 🔴 低 — 过于宽松 |
| `MAX_PATH_LENGTH_RATIO = 1.35` | 1.35 | 工程折中 | 🟡 中 — 合理但无外部来源 |
| `MAX_PRIMARY_THREAT_SWITCHES = 2` | 2 | 工程经验 | 🟡 中 — 单船场景应为 0 |
| `DANGER_EXPOSURE_GRACE_S = 5` | 5s | 工程折中 | 🟡 中 — 合理 |
| `ROUTE_RETURN_RELEASE_DWELL_S = 10` | 10s | 工程折中 | 🟡 中 — 合理 |

---

## 5. 综合评审结论

### 5.1 评分卡

| 维度 | 评分 | 说明 |
|---|---|---|
| 架构设计 | **B+** | 七层分层概念正确，但实现与 Spec 脱节，评估器与运行器耦合 |
| 场景覆盖 | **B** | 单船单规则覆盖合理，但缺 port crossing、右舷 stand-on、风流条件 |
| 评价指标 | **C+** | 核心安全指标有效，但缺 cross-ahead、reaction time、post-pass 拆分 |
| 阈值合理性 | **C** | 0.5 NM 和 9L 有学术支撑；0.1 NM 和 300m 缺乏权威来源；多个工程阈值偏宽松 |
| 与前端一致性 | **C** | 测试通过但 trace 不合理的根因已识别（CPA 阈值过低、过程指标缺失） |

### 5.2 必须修复的问题（P0）

1. **185.2m 不应作为 pass 阈值** — 应作为 danger floor。Pass 阈值至少 300m
2. **补充 cross-ahead 检测** — Rule 15 的核心要求
3. **补充 approach vs post-pass 风险拆分** — Spec §8 的核心设计
4. **README CPA 期望值与 YAML 不一致** — `rule14-ho` README 写 275m，YAML 写 185.2m

### 5.3 应该修复的问题（P1）

5. **补充 reaction time 指标** — 量化 Rule 8(b)/16 的 "early"
6. **收紧 steering_reversals 阈值** — give-way 从 4 降到 3
7. **收紧 integrated_XTE 阈值** — 从 300,000 降到 ~100,000 m·s
8. **补充 port crossing 场景**
9. **补充右舷 stand-on 场景**

### 5.4 建议改进（P2）

10. **评估器与运行器解耦** — 独立 TraceEvaluator 类
11. **补充 no-action baseline trace**
12. **补充速度变化评估**
13. **在部分场景中加入风流条件**
14. **标注极端速度比场景为"几何压力测试"**

---

## 附录 A: 参考文献

| ID | 来源 | 类型 |
|---|---|---|
| [R1] | IMO COLREGs 1972 (as amended), Rules 6-8, 13-17 | 法规 |
| [R2] | 33 CFR §83.06-83.17 (US Code) | 法规 |
| [R3] | Fujii Y, Tanaka K. "Traffic Capacity." J Navig. 1971;24:543-552 | 学术 |
| [R4] | Goodwin EM. "A Statistical Study of Ship Domains." J Navig. 1975;28(3):328-344 | 学术 |
| [R5] | Davis PV, Dove MJ, Stockel CT. "A Computer Simulation of Marine Traffic Using Domains and Arenas." J Navig. 1980;33:215-222 | 学术 |
| [R6] | Wang N, Meng X, Xu Q, Wang Z. "A Unified Analytical Framework for Ship Domains." J Navig. 2009;62(4):643-655 | 学术 |
| [R7] | Cockcroft AN, Lameijer JNF. *A Guide to the Collision Avoidance Rules*, 6th ed. 2003 | 专业参考 |
| [R8] | Evergreen Marine v Nautical Challenge [2021] UKSC 6 | 判例法 |
| [R9] | Kiveli v Afina I [2026] EWCA Civ 251 | 判例法 |
| [R10] | MDPI J. Mar. Sci. Eng. 2022, 10(1), 3 (MAXCMAS APF) | MASS 项目 |
| [R11] | MDPI J. Mar. Sci. Eng. 2023, 11(10), 1850 (MASS Legal Procedures) | MASS 项目 |
| [R12] | MDPI J. Mar. Sci. Eng. 2024, 12(7), 1224 (NSGA-II) | 学术 |
| [R13] | van Iperen, TransNav 2015 (North Sea AIS head-on) | AIS 实证 |
| [R14] | Vestre, J Navig 2023 (Norwegian AIS near-collision) | AIS 实证 |
| [R15] | MDPI J. Mar. Sci. Eng. 2021, 9(11), 1202 (Seafarer survey) | 人因研究 |
| [R16] | Tengesdal & Johansen, 2023 (COLREGs evaluation) | 学术 |
| [R17] | Sawada et al. (autonomous CA experiment) | MASS 项目 |
| [R18] | Prof. Craig H. Allen, UW Law — In Extremis Doctrine | 法学 |

## 附录 B: 置信度图例

| 标记 | 含义 |
|---|---|
| 🟢 高 | 有法规/判例/多篇学术文献直接支撑 |
| 🟡 中 | 有单一学术来源或项目 ODD 定义支撑 |
| 🔴 低 | 工程折中，无直接权威来源，或作为 pass 阈值过于激进 |
