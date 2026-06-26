# Qwen-3.7 评审报告

评审完成 — 交付物总结
我已完成对 MASS L3 TDL 的 COLREGs 8-Probe 测试平台的全面评审。以下是两份交付文档：
📄 交付物 1: 评审报告
文件: docs/Design/Review/8-Probe-Review-Report.md



📄 交付物 2: 完整设计报告
文件: docs/Design/Review/8-Probe-Design-Proposal.md

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

| 层               | Spec 提议                          | 当前实现状态                                                 | 评审结论                                               |
| ---------------- | ---------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------ |
| L1 场景有效性    | no-action DCPA/TCPA 验证           | `verify_colreg_tier12.py` 检查 DCPA<500m；`scenario_spec.py` 有几何计算 | ✅ 基本覆盖，但缺"无冲突场景不能证明避碰能力"的显式断言 |
| L2 安全底线      | min_separation >= cpa_threshold    | `run_6_scenarios.py:cpa_ok`                                  | ✅ 已实现                                               |
| L3 动态风险      | approach/post-pass 风险拆分        | `compute_risk_metrics()` 有 warning/danger exposure，但**不区分 approach vs post-pass** | ⚠️ 部分实现，缺 post-pass 独立计量                      |
| L4 COLREG 合规   | 按规则独立评估                     | `rule_compliance_evaluator.py` — 仅查 heading_change + CPA，**不查转向方向、时机、cross-ahead** | 🔴 过于粗糙                                             |
| L5 航线回归      | returned_to_route + XTE + corridor | `compute_route_return_status()`                              | ✅ 已实现                                               |
| L6 船艺/效率     | path ratio, XTE 积分, overshoot    | `compute_seamanship_metrics()`                               | ✅ 已实现                                               |
| L7 稳定性/求解器 | toggles, reversals, solver health  | `stability_scorer.py` — **最成熟层**                         | ✅ 优秀                                                 |

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

| COLREGs 规则       | 场景覆盖                                                     | 评审                                           |
| ------------------ | ------------------------------------------------------------ | ---------------------------------------------- |
| Rule 13 追越       | `rule13-ot` (1 场景)                                         | ⚠️ 仅测正后方追越，未测斜后方追越（112.5°附近） |
| Rule 14 对遇       | `rule14-ho` + `rule14-ho-port` (2 场景)                      | ✅ 纯正遇 + 偏左边界，覆盖良好                  |
| Rule 15 穿越       | `rule15-cs` + `rule15-cs-2` + `rule15-cs-edge` + `rule15-ot-boundary` (4 场景) | ✅ 标准穿越 + 短 TCPA + 两条边界                |
| Rule 16 让路船动作 | 隐含在 R15 场景中                                            | ⚠️ 无独立"早而明显动作"量化测试                 |
| Rule 17 直航船     | `rule17-cr-so` (1 场景)                                      | ⚠️ 仅测左舷来船，未测右舷来船（更危险）         |
| Rule 8 一般条款    | 隐含在各场景中                                               | ⚠️ 无"连续小改向"反面测试                       |
| Rule 19 受限能见度 | 无                                                           | 🔴 完全缺失（需 harness 改动，已知）            |
| 多船场景           | 无                                                           | 🔴 完全缺失（已知，属 Imazu-22 范围）           |

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

| 场景                 | 目标速度 | 问题                                  |
| -------------------- | -------- | ------------------------------------- |
| `rule15-cs-edge`     | 29.21 kn | 目标船速是 OS 的 2.4 倍，现实中极少见 |
| `rule15-ot-boundary` | 45.74 kn | 目标船速是 OS 的 4.6 倍，几乎不可能   |

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

| 场景                 | 初始距离 | 初始相对方位   | 目标航向  | DCPA(no-action) | 验证 |
| -------------------- | -------- | -------------- | --------- | --------------- | ---- |
| `rule14-ho`          | 2.0 NM   | 0° (正前方)    | 180°      | ≈0              | ✅    |
| `rule14-ho-port`     | 2.0 NM   | ~355° (偏左5°) | 170°      | ≈0              | ✅    |
| `rule13-ot`          | 0.6 NM   | ~3°            | 0° (同向) | ≈0              | ✅    |
| `rule15-cs`          | ~2.0 NM  | ~50°           | 290°      | ≈0              | ✅    |
| `rule15-cs-2`        | 1.0 NM   | ~60°           | 300°      | ≈0              | ✅    |
| `rule15-cs-edge`     | ~2.0 NM  | ~25°           | 215°      | ≈0              | ✅    |
| `rule15-ot-boundary` | ~2.0 NM  | ~108°          | 300°      | ≈0              | ✅    |
| `rule17-cr-so`       | ~2.0 NM  | ~315°          | 90°       | ≈0              | ✅    |

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

| 指标                           | 来源                                                    | 有效性             |
| ------------------------------ | ------------------------------------------------------- | ------------------ |
| `min_DCPA >= threshold`        | 通用海事实践                                            | 🟢 核心安全指标     |
| `behavior_toggles ≤ 2`         | 工程经验（M6 fishtail 回归锁）                          | 🟢 捕捉行为抖动     |
| `role_onset_changes = 0`       | COLREGs Rule 13(d) "once overtaking, always overtaking" | 🟢 法规直接要求     |
| `turn_starboard` (give-way)    | COLREGs Rules 14/15/16                                  | 🟢 法规直接要求     |
| `premature_giveway < 10°`      | COLREGs Rule 17(a)(i) "shall keep course and speed"     | 🟢 法规直接要求     |
| `path_length_ratio ≤ 1.35`     | 效率指标，η = d₁/d₂                                     | 🟡 合理但阈值需验证 |
| `route_crossing_overshoot ≤ 1` | 航海实践（不应反复穿越航线）                            | 🟢 合理             |

#### ⚠️ 有问题的指标

| 指标                                         | 问题                                                         | 建议                                  |
| -------------------------------------------- | ------------------------------------------------------------ | ------------------------------------- |
| `warning_domain_exposure_s ≤ 120s`           | 120 秒的 warning 暴露时间对于近距起步场景（2 NM，对遇 12+12 kn，TCPA≈300s）来说太宽松——几乎不可能超过 | 降低到 60s 或按场景配置               |
| `danger_domain_exposure_s ≤ 0`               | 零容忍 danger 暴露，但 `close_start_emergency_allowed` 场景允许 danger_ddv > 0。逻辑不一致 | 统一：要么都允许，要么都不允许        |
| `steering_reversals ≤ 4` (give-way)          | 4 次反转对于 give-way 来说太宽松。一个正常的右转→通过→回航只需要 2 次反转（turn-in + return）。4 次意味着允许一次额外的 fishtail | 降低到 ≤ 3                            |
| `max_rot_hold_std_dps < 1.5`                 | 1.5°/s 的偏航率标准差对于"保持段"来说偏高。一个稳定的转向应该 < 0.5°/s | 降低到 < 0.8                          |
| `integrated_abs_xte ≤ 500×600 = 300,000 m·s` | 这个阈值意味着可以在 500m XTE 下持续 600 秒，或 300m XTE 下持续 1000 秒。对于 4-7 分钟的场景来说太宽松 | 降低到 100,000 m·s 或按场景时长归一化 |

#### 🔴 缺失的关键指标

| 缺失指标                           | 为什么重要                                                   | 来源                  |
| ---------------------------------- | ------------------------------------------------------------ | --------------------- |
| **Cross-ahead 检测**               | Rule 15 明确要求 "avoid crossing ahead of the other vessel"。当前评估不检查 OS 是否从目标船前方穿过 | COLREGs Rule 15 原文  |
| **动作时机 (Reaction Time)**       | Rule 8(b)/16 要求 "early and substantial"。当前不量化"早"——只查 CPA 结果 | COLREGs Rule 8(b), 16 |
| **速度变化评估**                   | COLREGs 允许速度变化作为避碰手段（Rule 8(a)），但当前只评估航向变化 | COLREGs Rule 8(a)     |
| **Post-pass 距离趋势**             | Spec §8 提出 post-pass 应检查距离是否增加，但当前不实现      | Spec §8, 良好船艺     |
| **Approach vs Post-pass 风险拆分** | Spec §6 Layer 3 提出拆分，但当前 `compute_risk_metrics()` 不区分 | Spec §6               |
| **M7 veto 严重性**                 | 当前只计数 veto 事件，不区分严重性。M7 critical veto 应该是 hard fail | M7 设计文档           |

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

| 阈值       | 值       | 声称来源                | 实际来源验证                                                 | 置信度                                              |
| ---------- | -------- | ----------------------- | ------------------------------------------------------------ | --------------------------------------------------- |
| **185.2m** | 0.1 NM   | "紧急下限"              | Davis (1980) 船域横向间距 ~0.14 NM ≈ 259m；North Sea AIS 异常对遇阈值 0.35 NM (van Iperen 2015)。**0.1 NM 低于所有学术文献中的安全距离**。作为 danger/immediate-threat 阈值合理，作为 pass 阈值过于激进 | 🔴 低 — 作为 pass 阈值缺乏支撑                       |
| **300m**   | 0.162 NM | "0.1 NM 与 9L 之间折中" | Fujii 狭窄水道半短轴 ~3L ≈ 135m (45m 船)；Sawada 禁区 0.3 NM ≈ 555m。**300m 不直接对应任何权威来源** | 🔴 低 — 工程折中，需补证据                           |
| **405m**   | 9×LOA    | "船域参考"              | MASS 法律程序论文 (MDPI 2023, 11(10), 1850) 从 IMO 操纵性标准推导：advance < 4.5L，双船会遇 = 9L = "last-chance turning distance"。Fujii 开放水域半长轴 8L ≈ 360m | 🟡 中 — 有学术支撑但作为 ideal domain 而非 pass 阈值 |
| **926m**   | 0.5 NM   | "开放水域 warning 基线" | Wang (2009) 对遇安全域 0.5-0.6 NM；Goodwin Sector 1 右舷 0.85 NM；Davis 圆域 0.675 NM；多篇 MASS 论文使用 0.5 NM 作为安全域半径 | 🟢 高 — 最广泛支撑的阈值                             |

**关键发现**: 185.2m (0.1 NM) 作为 pass 阈值是最大的问题。学术文献中：
- 0.35 NM (648m) 是 North Sea AIS 数据中"异常"对遇的下限
- 0.3 NM (555m) 是 Sawada 实验中的禁区
- 0.5 NM (926m) 是多数 MASS 项目的安全域基线
- 0.1 NM (185m) 在所有文献中都属于"immediate danger"级别

**建议**: 185.2m 应作为 danger floor（硬红线，触碰即 RED），不应作为 pass 阈值。Pass 阈值应至少为 300m（受限航道）或 500m（开放水域）。

### 4.2 XTE 阈值

| 阈值                                | 值    | 来源                 | 置信度                  |
| ----------------------------------- | ----- | -------------------- | ----------------------- |
| `route_return_xte < 150m`           | 150m  | 项目工程阈值（3.3L） | 🟡 中 — 合理但无外部来源 |
| `route_corridor_pass_limit < 500m`  | 500m  | L2 安全航道约束      | 🟡 中 — 项目 ODD 定义    |
| `route_corridor_half_width = 1000m` | 1000m | L2 给出的航道半宽    | 🟡 中 — 项目 ODD 定义    |
| `route_return_heading < 10°`        | 10°   | 航海实践             | 🟢 高 — 标准航向恢复判据 |

### 4.3 稳定性阈值

| 阈值                                | 值   | 来源                 | 置信度                                               |
| ----------------------------------- | ---- | -------------------- | ---------------------------------------------------- |
| `behavior_toggles ≤ 2`              | 2    | 工程经验（一起一落） | 🟡 中 — 合理但无外部来源                              |
| `steering_reversals ≤ 4` (give-way) | 4    | 工程经验             | 🟡 中 — 偏宽松                                        |
| `steering_reversals ≤ 5` (stand-on) | 5    | 工程经验             | 🟡 中 — 偏宽松                                        |
| `rot_hold_std < 1.5°/s`             | 1.5  | 工程经验             | 🟡 中 — 偏宽松                                        |
| `conflict_toggles ≤ 2`              | 2    | 工程经验             | 🟡 中 — 合理                                          |
| `role_onset_changes = 0`            | 0    | COLREGs Rule 13(d)   | 🟢 高 — 法规要求                                      |
| `premature_giveway < 10°`           | 10°  | 航海实践             | 🟢 高 — 合理                                          |
| `min_give_way_turn ≥ 5°`            | 5°   | 工程经验             | 🟡 中 — Rule 8(b) 要求"readily apparent"，5° 可能不够 |

### 4.4 风险域阈值

| 阈值                                   | 值   | 来源        | 置信度                    |
| -------------------------------------- | ---- | ----------- | ------------------------- |
| `MAX_WARNING_DOMAIN_EXPOSURE_S = 120`  | 120s | 工程折中    | 🔴 低 — 无外部来源，偏宽松 |
| `MAX_INTEGRATED_ABS_XTE = 300,000 m·s` | 300k | 500m × 600s | 🔴 低 — 过于宽松           |
| `MAX_PATH_LENGTH_RATIO = 1.35`         | 1.35 | 工程折中    | 🟡 中 — 合理但无外部来源   |
| `MAX_PRIMARY_THREAT_SWITCHES = 2`      | 2    | 工程经验    | 🟡 中 — 单船场景应为 0     |
| `DANGER_EXPOSURE_GRACE_S = 5`          | 5s   | 工程折中    | 🟡 中 — 合理               |
| `ROUTE_RETURN_RELEASE_DWELL_S = 10`    | 10s  | 工程折中    | 🟡 中 — 合理               |

---

## 5. 综合评审结论

### 5.1 评分卡

| 维度         | 评分   | 说明                                                         |
| ------------ | ------ | ------------------------------------------------------------ |
| 架构设计     | **B+** | 七层分层概念正确，但实现与 Spec 脱节，评估器与运行器耦合     |
| 场景覆盖     | **B**  | 单船单规则覆盖合理，但缺 port crossing、右舷 stand-on、风流条件 |
| 评价指标     | **C+** | 核心安全指标有效，但缺 cross-ahead、reaction time、post-pass 拆分 |
| 阈值合理性   | **C**  | 0.5 NM 和 9L 有学术支撑；0.1 NM 和 300m 缺乏权威来源；多个工程阈值偏宽松 |
| 与前端一致性 | **C**  | 测试通过但 trace 不合理的根因已识别（CPA 阈值过低、过程指标缺失） |

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

| ID    | 来源                                                         | 类型      |
| ----- | ------------------------------------------------------------ | --------- |
| [R1]  | IMO COLREGs 1972 (as amended), Rules 6-8, 13-17              | 法规      |
| [R2]  | 33 CFR §83.06-83.17 (US Code)                                | 法规      |
| [R3]  | Fujii Y, Tanaka K. "Traffic Capacity." J Navig. 1971;24:543-552 | 学术      |
| [R4]  | Goodwin EM. "A Statistical Study of Ship Domains." J Navig. 1975;28(3):328-344 | 学术      |
| [R5]  | Davis PV, Dove MJ, Stockel CT. "A Computer Simulation of Marine Traffic Using Domains and Arenas." J Navig. 1980;33:215-222 | 学术      |
| [R6]  | Wang N, Meng X, Xu Q, Wang Z. "A Unified Analytical Framework for Ship Domains." J Navig. 2009;62(4):643-655 | 学术      |
| [R7]  | Cockcroft AN, Lameijer JNF. *A Guide to the Collision Avoidance Rules*, 6th ed. 2003 | 专业参考  |
| [R8]  | Evergreen Marine v Nautical Challenge [2021] UKSC 6          | 判例法    |
| [R9]  | Kiveli v Afina I [2026] EWCA Civ 251                         | 判例法    |
| [R10] | MDPI J. Mar. Sci. Eng. 2022, 10(1), 3 (MAXCMAS APF)          | MASS 项目 |
| [R11] | MDPI J. Mar. Sci. Eng. 2023, 11(10), 1850 (MASS Legal Procedures) | MASS 项目 |
| [R12] | MDPI J. Mar. Sci. Eng. 2024, 12(7), 1224 (NSGA-II)           | 学术      |
| [R13] | van Iperen, TransNav 2015 (North Sea AIS head-on)            | AIS 实证  |
| [R14] | Vestre, J Navig 2023 (Norwegian AIS near-collision)          | AIS 实证  |
| [R15] | MDPI J. Mar. Sci. Eng. 2021, 9(11), 1202 (Seafarer survey)   | 人因研究  |
| [R16] | Tengesdal & Johansen, 2023 (COLREGs evaluation)              | 学术      |
| [R17] | Sawada et al. (autonomous CA experiment)                     | MASS 项目 |
| [R18] | Prof. Craig H. Allen, UW Law — In Extremis Doctrine          | 法学      |

## 附录 B: 置信度图例

| 标记 | 含义                                               |
| ---- | -------------------------------------------------- |
| 🟢 高 | 有法规/判例/多篇学术文献直接支撑                   |
| 🟡 中 | 有单一学术来源或项目 ODD 定义支撑                  |
| 🔴 低 | 工程折中，无直接权威来源，或作为 pass 阈值过于激进 |

# COLREGs 8-Probe Trace Evaluator — Complete Design Proposal v1.0

**Status**: Design Proposal (post-review)
**Date**: 2026-06-16
**Companion**: `8-Probe-Review-Report.md` (评审报告)
**Scope**: Complete evaluator redesign addressing all P0/P1 review findings

---

## 0. Design Philosophy

### 0.1 Core Principle: "Test What a Navigator Would Judge"

The evaluator must answer the same questions a qualified navigator asks when reviewing a voyage:

1. **Was it safe?** — Did the ships pass at a safe distance? (COLREGs Rule 8(d))
2. **Was it lawful?** — Did each vessel fulfill its COLREGs obligations? (Rules 13-17)
3. **Was it efficient?** — Did the vessel return to track without excessive detour? (Good seamanship)
4. **Was it stable?** — Were the maneuvers smooth and predictable? (Rule 8(b): "readily apparent")

### 0.2 Separation of Concerns

```
┌─────────────────────────────────────────────────────────┐
│                    Scenario Runner                       │
│  (configure → activate → poll → collect trace)          │
│  run_6_scenarios.py → run_colregs_clean_8probe.py       │
└──────────────────────┬──────────────────────────────────┘
                       │ trace_current.jsonl
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  TraceEvaluator                          │
│  (pure function: trace + scenario_spec → verdict)       │
│  No ROS2, no orchestrator, no network                   │
└──────────────────────┬──────────────────────────────────┘
                       │ EvaluationReport (JSON)
                       ▼
┌─────────────────────────────────────────────────────────┐
│                  Report Formatter                        │
│  (JSON → CLI summary / HTML / dashboard)                │
└─────────────────────────────────────────────────────────┘
```

### 0.3 Verdict Structure: Three Independent Dimensions

Replace the flat AND with three independent dimensions plus an overall:

```python
@dataclass
class Verdict:
    safety_pass: bool          # CPA floor + no collision + M7 no critical veto
    colregs_pass: bool         # Rule compliance + role lifecycle + past-and-clear
    mission_pass: bool         # Route return + corridor + no excessive detour
    stability_pass: bool       # No fishtail/flap/chatter

    # Quality scores (0.0-1.0, do NOT replace hard gates)
    safety_quality: float      # How much margin above the floor
    colregs_quality: float     # How well the rule was followed
    mission_quality: float     # How efficiently the route was recovered
    stability_quality: float   # How smooth the maneuvers were

    @property
    def overall_pass(self) -> bool:
        return self.safety_pass and self.colregs_pass and self.mission_pass and self.stability_pass

    # Diagnostic fields
    failed_gates: List[str]    # Which specific gates failed
    first_failure_t: float     # Timestamp of first failure
    warnings: List[str]        # Non-failing concerns
```

---

## 1. CPA Threshold Model (Revised)

### 1.1 Three-Tier Threshold System

Replace the single `cpa_min_m_ge` with a three-tier system:

| Tier                   | Name                   | Value (FCB 45m LOA)        | Meaning                                   | Verdict Impact               |
| ---------------------- | ---------------------- | -------------------------- | ----------------------------------------- | ---------------------------- |
| **T1: Danger Floor**   | Immediate danger       | **185.2m** (0.1 NM, 4.1L)  | Physical collision risk — ships too close | **Hard RED** — safety_fail   |
| **T2: Safety Floor**   | Minimum safe passing   | **max(300m, 6.7L)** = 300m | Below this = unsafe passing               | **safety_pass = false**      |
| **T3: Quality Domain** | Good seamanship target | **scenario-dependent**     | Below this = quality deduction            | **safety_quality reduction** |

### 1.2 Scenario-Specific Thresholds

| Scenario Profile                  | T1 Danger | T2 Safety | T3 Quality    | Basis                                                |
| --------------------------------- | --------- | --------- | ------------- | ---------------------------------------------------- |
| `corridor_close_start` (R14 对遇) | 185.2m    | 300m      | 405m (9L)     | Fujii 8L open water [R3]; MASS legal 9L [R11]        |
| `corridor_overtaking` (R13 追越)  | 185.2m    | 300m      | 405m (9L)     | Goodwin Sector 3: 0.45 NM [R4]                       |
| `corridor_boundary` (边界探针)    | 185.2m    | 300m      | 405m (9L)     | Same as above                                        |
| `open_water_crossing` (R15 穿越)  | 185.2m    | 500m      | 926m (0.5 NM) | Wang 0.5-0.6 NM [R6]; Goodwin Sector 1: 0.85 NM [R4] |
| `standon_in_extremis` (R17 末段)  | 185.2m    | 250m      | 405m (9L)     | In-extremis accepts lower safety margin [R18]        |

### 1.3 Threshold Provenance Output

Every evaluation report MUST include:

```json
{
  "cpa_thresholds": {
    "danger_floor_m": 185.2,
    "safety_floor_m": 300.0,
    "quality_domain_m": 405.0,
    "profile": "corridor_close_start",
    "basis": "Fujii 8L open water domain; MASS legal 9L last-chance turn",
    "nm_equivalent": [0.1, 0.162, 0.219],
    "loa_multiplier": [4.1, 6.7, 9.0],
    "source_confidence": ["high", "moderate", "moderate"]
  }
}
```

---

## 2. Seven-Layer Evaluator (Revised)

### Layer 1: Scenario Validity (Pre-Flight)

**Purpose**: Verify the scenario is a valid test before evaluating the system.

```python
class Layer1ScenarioValidity:
    def evaluate(self, scenario_spec, no_action_trace=None) -> LayerResult:
        checks = {
            "has_conflict_geometry": self._check_dcpa_no_action(scenario_spec),
            "valid_role_assignment": self._check_role(scenario_spec),
            "valid_rule_coverage": self._check_rule(scenario_spec),
            "odd_within_bounds": self._check_odd(scenario_spec),
            "no_action_baseline": self._check_no_action(no_action_trace),
        }
        return LayerResult(passed=all(checks.values()), checks=checks)

    def _check_dcpa_no_action(self, spec) -> bool:
        """No-action DCPA must be < 500m to prove the scenario has a real conflict."""
        # Computed from initial geometry
        dcpa = compute_no_action_dcpa(spec)
        return dcpa < 500.0

    def _check_no_action(self, trace) -> bool:
        """If no-action baseline trace exists, verify collision would occur."""
        if trace is None:
            return True  # Graceful degradation
        min_dist = min_distance_in_trace(trace)
        return min_dist < 100.0  # Would collide without action
```

**New**: `no_action_baseline` check — if a no-action trace exists, verify the scenario actually creates a collision risk.

### Layer 2: Safety Floor

**Purpose**: Hard safety红线.

```python
class Layer2SafetyFloor:
    def evaluate(self, trace, thresholds) -> LayerResult:
        min_separation = trace.min_separation_m()

        checks = {
            "above_danger_floor": min_separation >= thresholds.danger_floor_m,
            "above_safety_floor": min_separation >= thresholds.safety_floor_m,
            "no_m7_critical_veto": not trace.has_critical_veto(),
        }

        quality = min(1.0, (min_separation - thresholds.safety_floor_m) /
                      (thresholds.quality_domain_m - thresholds.safety_floor_m))

        return LayerResult(
            passed=checks["above_danger_floor"] and checks["above_safety_floor"] and checks["no_m7_critical_veto"],
            checks=checks,
            quality=max(0.0, quality),
        )
```

**Key change**: Two-tier safety (danger + safety), with quality as a continuous score.

### Layer 3: Dynamic Risk (Revised — Approach/Post-Pass Split)

**Purpose**: Quantify risk exposure, split by encounter phase.

```python
class Layer3DynamicRisk:
    def evaluate(self, trace, thresholds) -> LayerResult:
        # Phase detection
        phases = self._detect_phases(trace)

        # Approach phase: from conflict onset to CPA
        approach = self._evaluate_approach(trace, phases, thresholds)

        # Post-pass phase: from CPA to past-and-clear
        post_pass = self._evaluate_post_pass(trace, phases, thresholds)

        checks = {
            "approach_danger_exposure_s": approach.danger_s <= thresholds.max_approach_danger_s,
            "approach_warning_exposure_s": approach.warning_s <= thresholds.max_approach_warning_s,
            "post_pass_close_domain_s": post_pass.close_domain_s <= thresholds.max_post_pass_close_s,
            "post_pass_distance_increasing": post_pass.distance_increasing,
            "risk_recovery_ok": approach.risk_recovery_ok,
        }

        return LayerResult(passed=all(checks.values()), checks=checks)

    def _detect_phases(self, trace) -> EncounterPhases:
        """Split trace into approach / CPA / post-pass phases."""
        cpa_idx = trace.argmin_separation()
        cpa_t = trace.t[cpa_idx]

        # Post-pass: TCPA < 0 AND closing_speed <= 0 AND target abaft
        post_pass_start = None
        for i in range(cpa_idx, len(trace)):
            if (trace.tcpa[i] < 0 and
                trace.closing_speed[i] <= 0 and
                self._target_abaft(trace, i)):
                post_pass_start = trace.t[i]
                break

        return EncounterPhases(
            approach=(trace.t[0], cpa_t),
            post_pass=(post_pass_start or cpa_t, trace.t[-1]),
        )

    def _target_abaft(self, trace, idx) -> bool:
        """Is the target abaft the own ship's beam?"""
        rel_bearing = trace.rel_bearing[idx]
        return abs(rel_bearing) > 90.0
```

**Key changes**:
1. Explicit phase detection (approach vs post-pass)
2. Post-pass checks: distance increasing, target abaft
3. Rule 13 exception: overtaking duty persists regardless of TCPA sign

### Layer 4: COLREG Compliance (Revised — Per-Rule Detailed)

**Purpose**: Evaluate rule-specific compliance with full behavioral context.

```python
class Layer4ColregsCompliance:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        rule = scenario_spec.encounter.rule
        role = scenario_spec.encounter.give_way_vessel

        # Common checks
        checks = {
            "role_lifecycle": self._check_role_lifecycle(trace, role),
            "past_and_clear": self._check_past_and_clear(trace),
        }

        # Rule-specific checks
        if rule == "Rule14":
            checks.update(self._check_rule14(trace))
        elif rule == "Rule13":
            checks.update(self._check_rule13(trace))
        elif rule in ("Rule15", "Rule15_Stbd"):
            checks.update(self._check_rule15(trace))
        elif rule == "Rule17":
            checks.update(self._check_rule17(trace))

        return LayerResult(passed=all(checks.values()), checks=checks)

    def _check_rule14(self, trace) -> dict:
        """Rule 14: Head-on — both alter starboard, port-to-port pass."""
        return {
            "turn_starboard": trace.max_starboard_dev >= trace.max_port_dev and trace.max_starboard_dev >= 10.0,
            "no_port_turn": trace.max_port_dev < 15.0,  # No significant port turn
            "port_to_port_pass": self._verify_port_pass(trace),
            "no_left_turn_crossing": not self._misclassified_as_crossing(trace),
        }

    def _check_rule13(self, trace) -> dict:
        """Rule 13: Overtaking — keep clear until finally past and clear."""
        return {
            "keep_clear_duty_held": self._duty_held_throughout(trace, "give_way"),
            "no_reclassification": self._no_rule_reclassification(trace),
            "safe_follow_or_pass": self._safe_follow_or_pass(trace),
            "past_and_clear_before_release": self._past_and_clear_before_release(trace),
        }

    def _check_rule15(self, trace) -> dict:
        """Rule 15/16: Crossing give-way — early, substantial, starboard, no cross-ahead."""
        return {
            "turn_starboard": trace.max_starboard_dev >= trace.max_port_dev and trace.max_starboard_dev >= 10.0,
            "early_action": self._action_before_tcpa(trace, threshold_s=120.0),
            "substantial_action": trace.max_heading_change >= 30.0,
            "no_cross_ahead": not self._crossed_ahead_of_target(trace),
            "passed_astern": self._passed_astern_of_target(trace),
        }

    def _check_rule17(self, trace) -> dict:
        """Rule 17: Stand-on — hold course initially, act only when required."""
        return {
            "hold_course_phase": self._hold_course_for_initial_phase(trace, hold_frac=0.75),
            "no_premature_action": trace.max_heading_dev_in_hold < 10.0,
            "independent_action_taken": self._independent_action_taken(trace),
            "no_port_turn_for_port_vessel": self._no_port_turn_for_port_vessel(trace),
        }

    def _crossed_ahead_of_target(self, trace) -> bool:
        """Did own ship cross ahead of the target vessel? (Rule 15 violation)"""
        for i in range(len(trace)):
            # Own ship's along-track position relative to target
            own_along = self._along_track(trace, i, reference="target")
            target_along = 0.0  # Target is the reference
            lateral = self._cross_track(trace, i, reference="target")

            if own_along > target_along and abs(lateral) < 200.0:
                # Own ship is ahead of target and within 200m laterally
                return True
        return False

    def _action_before_tcpa(self, trace, threshold_s) -> bool:
        """Did the give-way vessel start avoiding before TCPA reached threshold?"""
        avoidance_onset = trace.avoidance_onset_t()
        if avoidance_onset is None:
            return False
        tcpa_at_onset = trace.tcpa_at(avoidance_onset)
        return tcpa_at_onset >= threshold_s
```

**Key changes**:
1. **Cross-ahead detection** for Rule 15 (critical missing check)
2. **Reaction time** quantification (Rule 8(b)/16 "early")
3. **Substantial action** threshold (Rule 8(b) "readily apparent" → ≥30° heading change)
4. **Rule 17(c)** constraint: no port turn for port-side vessel
5. **Past-and-clear** verification before conflict release

### Layer 5: Route Recovery

**Purpose**: Evaluate route return quality.

```python
class Layer5RouteRecovery:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        expected = scenario_spec.expected_outcome

        checks = {
            "returned_to_route": self._returned(trace, expected),
            "final_xte_ok": abs(trace.final_xte) < expected.route_return_xte_m_lt,
            "final_heading_ok": abs(trace.final_heading_dev) < expected.route_return_heading_deg_lt,
            "corridor_ok": abs(trace.max_route_xte) < expected.route_corridor_pass_limit_m,
            "no_corridor_violation": abs(trace.max_route_xte) < expected.route_corridor_half_width_m,
        }

        # Quality metrics
        quality = {
            "final_xte_m": trace.final_xte,
            "final_heading_dev_deg": trace.final_heading_dev,
            "max_route_xte_m": trace.max_route_xte,
            "recovery_time_s": trace.recovery_time_s,
            "transit_after_avoidance_s": trace.transit_after_avoidance_s,
        }

        return LayerResult(passed=all(checks.values()), checks=checks, quality=quality)
```

### Layer 6: Seamanship / Efficiency (Revised)

**Purpose**: Evaluate maneuver quality beyond safety.

```python
class Layer6Seamanship:
    def evaluate(self, trace, scenario_spec) -> LayerResult:
        route_distance = scenario_spec.route_distance_m

        checks = {
            "path_length_ratio_ok": trace.path_length_ratio <= 1.35,
            "integrated_xte_ok": trace.integrated_abs_xte_m_s <= self._xte_budget(trace, scenario_spec),
            "route_crossing_ok": trace.route_crossing_overshoot_count <= 1,
            "no_excessive_detour": trace.max_route_xte <= scenario_spec.expected_outcome.route_corridor_pass_limit_m,
        }

        quality = {
            "path_length_ratio": trace.path_length_ratio,
            "integrated_abs_xte_m_s": trace.integrated_abs_xte_m_s,
            "route_crossing_overshoot_count": trace.route_crossing_overshoot_count,
            "speed_variation": self._speed_variation(trace),
        }

        return LayerResult(passed=all(checks.values()), checks=checks, quality=quality)

    def _xte_budget(self, trace, spec) -> float:
        """Normalize XTE budget by scenario duration."""
        duration_s = trace.duration_s
        # Allow ~200m average XTE for the scenario duration
        return 200.0 * duration_s
```

**Key change**: XTE budget normalized by scenario duration instead of fixed 300,000 m·s.

### Layer 7: Stability / Solver Health (Revised)

**Purpose**: Detect fishtail, flap, chatter.

```python
class Layer7Stability:
    """Revised from existing stability_scorer.py — the most mature layer."""

    REVISED_THRESHOLDS = {
        # Tightened from current values
        "max_behavior_toggles": 2,           # Unchanged
        "max_plan_valid_segments": 2,        # Unchanged
        "max_steering_reversals_gw": 3,      # Was 4 — tightened
        "max_steering_reversals_so": 3,      # Was 5 — tightened
        "max_rot_hold_std_dps": 0.8,         # Was 1.5 — tightened
        "max_conflict_toggles": 2,           # Unchanged
        "max_role_onset_changes": 0,         # Unchanged (Rule 13(d))
        "max_premature_giveway_deg": 10.0,   # Unchanged
        "min_give_way_turn_deg": 10.0,       # Was 5.0 — tightened (Rule 8(b) "readily apparent")
        "rot_deadband_dps": 1.0,             # Unchanged
    }
```

**Key changes**:
1. `steering_reversals` tightened: give-way 4→3, stand-on 5→3
2. `rot_hold_std` tightened: 1.5→0.8 °/s
3. `min_give_way_turn` tightened: 5°→10° (Rule 8(b) "readily apparent")

---

## 3. Heading-On Post-Pass Rule (Spec §8 Implementation)

```python
class PostPassClassifier:
    """Implements Spec §8: Heading-On Post-Pass Rule."""

    def classify(self, trace, idx) -> str:
        """Returns 'active_threat' | 'post_pass_clearance' | 'post_pass_close'."""
        tcpa = trace.tcpa[idx]
        closing_speed = trace.closing_speed[idx]
        rel_bearing = trace.rel_bearing[idx]
        distance = trace.separation[idx]

        # Active collision threat: TCPA >= 0 OR closing, not past-and-clear
        if tcpa >= 0 or closing_speed > 0:
            return "active_threat"

        # Post-pass: TCPA < 0, closing_speed <= 0, target abaft
        target_abaft = abs(rel_bearing) > 90.0

        if target_abaft and distance > trace.separation[idx - 1]:
            return "post_pass_clearance"  # Distance increasing — good

        if target_abaft:
            return "post_pass_close"  # Target abaft but distance not increasing

        return "active_threat"  # Not abaft — still active

    def rule13_exception(self, trace, idx, rule) -> str:
        """Rule 13: overtaking duty persists regardless of TCPA sign."""
        if rule == "Rule13":
            # Overtaking vessel must keep clear until finally past and clear
            # "Finally past and clear" = overtaken vessel fully ahead and drawing away
            if not self._fully_past(trace, idx):
                return "active_threat"  # Duty persists
        return self.classify(trace, idx)
```

---

## 4. Revised Scenario Set

### 4.1 Current 8-Probe (Retained with Fixes)

| ID                          | Rule | Fix Required                                            |
| --------------------------- | ---- | ------------------------------------------------------- |
| `colreg-rule14-ho`          | R14  | Fix CPA threshold: 185.2→300m safety floor              |
| `colreg-rule14-ho-port`     | R14  | Fix CPA threshold: 185.2→300m; fix README inconsistency |
| `colreg-rule13-ot`          | R13  | Add wind/current variant                                |
| `colreg-rule15-cs`          | R15  | Add cross-ahead check                                   |
| `colreg-rule15-cs-2`        | R15  | Add reaction time check                                 |
| `colreg-rule15-cs-edge`     | R15  | Mark as "geometry stress test" (target 29 kn)           |
| `colreg-rule15-ot-boundary` | R15  | Mark as "geometry stress test" (target 45 kn)           |
| `colreg-rule17-cr-so`       | R17  | Fix CPA threshold: 185.2→250m (in-extremis)             |

### 4.2 New Scenarios (Recommended Additions)

| ID                         | Rule | Purpose                                                      | Priority |
| -------------------------- | ---- | ------------------------------------------------------------ | -------- |
| `colreg-rule15-cs-port`    | R15  | Port-side crossing (OS give-way) — tests left turn to pass astern | P1       |
| `colreg-rule17-cr-so-stbd` | R17  | Right-side stand-on — tests Rule 17(c) no-port-turn constraint | P1       |
| `colreg-rule14-ho-wind`    | R14  | Head-on with moderate wind/current — tests robustness        | P2       |
| `colreg-rule13-ot-slow`    | R13  | Overtaking with small speed difference (2 kn) — tests patience | P2       |

### 4.3 No-Action Baseline Requirement

Each scenario MUST have a corresponding no-action baseline run:

```python
def generate_no_action_baseline(scenario_spec):
    """Run the scenario with own ship maintaining course and speed."""
    modified = deepcopy(scenario_spec)
    modified.ownShip.controller = "none"  # No avoidance
    modified.metadata.simulation_settings.total_time = 300.0  # Shorter
    return modified
```

The baseline trace proves: "Without avoidance, the ships would collide (min_distance < 100m)."

---

## 5. Report Output Format

### 5.1 Per-Scenario Report

```json
{
  "scenario_id": "colreg-rule14-ho-v2.0",
  "run_id": "run-20260616-001",
  "timestamp": "2026-06-16T10:30:00Z",

  "verdict": {
    "safety_pass": true,
    "colregs_pass": true,
    "mission_pass": true,
    "stability_pass": true,
    "overall_pass": true,
    "safety_quality": 0.85,
    "colregs_quality": 0.92,
    "mission_quality": 0.78,
    "stability_quality": 0.95
  },

  "cpa_thresholds": {
    "danger_floor_m": 185.2,
    "safety_floor_m": 300.0,
    "quality_domain_m": 405.0,
    "profile": "corridor_close_start",
    "basis": "Fujii 8L; MASS legal 9L",
    "source_confidence": ["high", "moderate", "moderate"]
  },

  "layer_results": {
    "L1_scenario_validity": { "passed": true, "checks": {...} },
    "L2_safety_floor": {
      "passed": true,
      "min_separation_m": 387.2,
      "danger_floor_margin_m": 202.0,
      "safety_floor_margin_m": 87.2,
      "quality": 0.85
    },
    "L3_dynamic_risk": {
      "passed": true,
      "approach": {
        "warning_exposure_s": 45.2,
        "danger_exposure_s": 0.0,
        "risk_recovery_ok": true
      },
      "post_pass": {
        "close_domain_s": 12.3,
        "distance_increasing": true,
        "min_post_pass_separation_m": 420.5
      }
    },
    "L4_colregs_compliance": {
      "passed": true,
      "rule": "Rule14",
      "checks": {
        "turn_starboard": { "value": true, "max_stbd_deg": 42.5 },
        "no_port_turn": { "value": true, "max_port_deg": 3.2 },
        "port_to_port_pass": { "value": true },
        "role_lifecycle": { "value": true, "duty_changes": 0 },
        "past_and_clear": { "value": true, "time_s": 285.3 }
      }
    },
    "L5_route_recovery": {
      "passed": true,
      "final_xte_m": 45.2,
      "final_heading_dev_deg": 2.1,
      "max_route_xte_m": 312.5,
      "recovery_time_s": 85.0
    },
    "L6_seamanship": {
      "passed": true,
      "path_length_ratio": 1.12,
      "integrated_abs_xte_m_s": 45230.0,
      "route_crossing_overshoot_count": 0
    },
    "L7_stability": {
      "passed": true,
      "behavior_toggles": 2,
      "steering_reversals": 2,
      "rot_hold_std_dps": 0.35,
      "conflict_toggles": 2,
      "role_onset_changes": 0
    }
  },

  "failed_gates": [],
  "warnings": [
    "Post-pass close domain exposure 12.3s — within limit but notable"
  ],

  "trace_artifact_path": "runs/run-20260616-001/trace_current.jsonl",
  "plot_path": "runs/run-20260616-001/trajectory.png"
}
```

### 5.2 Batch Summary

```
==================================================
COLREGs Clean 8-Probe Batch Results
==================================================
Date: 2026-06-16 | FCB LOA: 45.0m | Rate: 10x

[PASS] colreg-rule14-ho          safety=0.85 colregs=0.92 mission=0.78 stability=0.95
[PASS] colreg-rule14-ho-port     safety=0.82 colregs=0.90 mission=0.75 stability=0.93
[PASS] colreg-rule13-ot          safety=0.78 colregs=0.88 mission=0.82 stability=0.91
[PASS] colreg-rule15-cs          safety=0.91 colregs=0.95 mission=0.85 stability=0.97
[PASS] colreg-rule15-cs-2        safety=0.88 colregs=0.93 mission=0.80 stability=0.94
[PASS] colreg-rule15-cs-edge     safety=0.75 colregs=0.85 mission=0.72 stability=0.88
[PASS] colreg-rule15-ot-boundary safety=0.72 colregs=0.82 mission=0.70 stability=0.85
[PASS] colreg-rule17-cr-so       safety=0.68 colregs=0.90 mission=0.65 stability=0.92

OVERALL: 8/8 PASS
Safety: 8/8 | COLREGs: 8/8 | Mission: 8/8 | Stability: 8/8

Warnings:
  - rule15-ot-boundary: target speed 45.7 kn (4.6× OS) — geometry stress test
  - rule17-cr-so: safety_quality 0.68 — close to safety floor (in-extremis expected)
```

---

## 6. Implementation Plan

### Phase 1: Threshold Fix (P0 — 1 day)

1. Update all scenario YAML files:
   - Add `cpa_acceptance.safety_floor_m` field
   - Keep `cpa_acceptance.threshold_m` as danger floor (185.2m)
   - Add `cpa_acceptance.quality_domain_m` field
2. Fix README CPA inconsistencies
3. Update `expected_cpa_floor_m()` in `run_6_scenarios.py` to use safety floor

### Phase 2: Cross-Ahead + Reaction Time (P0 — 2 days)

1. Add `_crossed_ahead_of_target()` to `run_6_scenarios.py`
2. Add `_action_before_tcpa()` reaction time check
3. Wire both into `compute_overall_pass()`

### Phase 3: Approach/Post-Pass Split (P0 — 2 days)

1. Implement `PostPassClassifier` (Spec §8)
2. Split `compute_risk_metrics()` into approach and post-pass phases
3. Add post-pass quality metrics to report

### Phase 4: Stability Threshold Tightening (P1 — 0.5 day)

1. Update `DEFAULT_THRESHOLDS` in `stability_scorer.py`
2. Update test expectations in `test_stability_scorer.py`

### Phase 5: Evaluator Decoupling (P1 — 3 days)

1. Create `TraceEvaluator` class with clean interface
2. Extract evaluation logic from `run_6_scenarios.py`
3. Create `EvaluationReport` dataclass
4. Rename runner to `run_colregs_clean_8probe.py`

### Phase 6: New Scenarios (P2 — 2 days)

1. Add `colreg-rule15-cs-port` to `gen_colreg_tier12.py`
2. Add `colreg-rule17-cr-so-stbd` to `gen_colreg_tier12.py`
3. Generate and validate new scenarios

### Phase 7: No-Action Baseline (P2 — 1 day)

1. Add no-action baseline generation to scenario runner
2. Add baseline validation to Layer 1

---

## 7. Trace Input Contract (Formalized)

```python
@dataclass
class TraceContract:
    """Required trace fields for evaluation."""

    # Own ship (from /sil/own_ship_state)
    ownship_fields = [
        "t_s", "lat", "lon", "heading_deg", "sog_kn", "rot_deg_s",
        # Optional: "rudder_angle_deg", "cog_deg"
    ]

    # Target (from /sil/target_vessel_state or analytical)
    target_fields = [
        "id", "lat", "lon", "cog", "sog_kn",
    ]

    # Geometry (computed by evaluator)
    geometry_fields = [
        "range_m", "rel_bearing_deg", "dcpa_m", "tcpa_s", "closing_speed_mps",
    ]

    # Route (computed by evaluator)
    route_fields = [
        "cross_track_error_m", "route_progress",
    ]

    # M2/M6 (from /l3/m6/colregs_constraint)
    m6_fields = [
        "rule", "role", "phase", "conflict_detected", "confidence", "rationale",
    ]

    # M4 (from /l3/m4/behavior_plan)
    m4_fields = [
        "behavior", "avoidance_active", "heading_min", "heading_max",
    ]

    # M5 (from /l3/m5/avoidance_plan)
    m5_fields = [
        "solver_status", "route_points",
    ]

    # M7 (from /l3/checker/veto)
    m7_fields = [
        "veto_state", "safety_margin",
    ]

    # L4 output (from /l4/guidance_cmd)
    l4_fields = [
        "psi_cmd", "u_cmd", "rot_cmd", "mode",
    ]
```

---

## 8. Summary of Changes from Current System

| Aspect                   | Current                             | Proposed                                | Impact                                                     |
| ------------------------ | ----------------------------------- | --------------------------------------- | ---------------------------------------------------------- |
| CPA pass threshold       | 185.2m (0.1 NM) for all close-start | 300m safety floor + 185.2m danger floor | Scenarios that barely pass at 200m will now correctly fail |
| Cross-ahead check        | Missing                             | Added for Rule 15                       | Will catch "passed in front of target" violations          |
| Reaction time            | Missing                             | Added (TCPA at avoidance onset ≥ 120s)  | Will catch "acted too late" violations                     |
| Approach/post-pass split | Not implemented                     | Full implementation per Spec §8         | Post-pass close domain no longer counts as approach danger |
| Steering reversals       | ≤4 (give-way), ≤5 (stand-on)        | ≤3 (both)                               | Tighter fishtail detection                                 |
| ROT hold std             | <1.5 °/s                            | <0.8 °/s                                | Tighter smoothness requirement                             |
| Min give-way turn        | ≥5°                                 | ≥10°                                    | Ensures "readily apparent" action                          |
| Integrated XTE           | ≤300,000 m·s                        | ≤200m × duration_s                      | Normalized by scenario length                              |
| Verdict structure        | Single overall_pass                 | 4-dimension verdict + quality scores    | More diagnostic information                                |
| Evaluator coupling       | Embedded in runner                  | Independent TraceEvaluator class        | Cleaner architecture                                       |

---

## Appendix A: Threshold Provenance Matrix

| Threshold                   | Value                              | Source                                                       | Reference         | Confidence                              |
| --------------------------- | ---------------------------------- | ------------------------------------------------------------ | ----------------- | --------------------------------------- |
| Danger floor                | 185.2m (0.1 NM)                    | Davis (1980) lateral separation ~0.14 NM; immediate danger convention | [R5]              | 🟡 Moderate                              |
| Safety floor (corridor)     | 300m (6.7L)                        | Engineering compromise between 0.1 NM and 9L                 | —                 | 🔴 Low (needs project ODD justification) |
| Safety floor (open water)   | 500m (11.1L)                       | Wang (2009) head-on 0.5-0.6 NM; common MASS practice         | [R6][R10]         | 🟢 High                                  |
| Quality domain              | 405m (9L)                          | IMO maneuverability advance < 4.5L × 2 ships                 | [R11]             | 🟡 Moderate                              |
| Quality domain (open water) | 926m (0.5 NM)                      | Goodwin, Davis, Wang consensus; MAXCMAS                      | [R4][R5][R6][R10] | 🟢 High                                  |
| Head-on sector              | ±6° (354°-006°)                    | Half-point convention; Kiveli v Afina I [2026] EWCA          | [R9]              | 🟢 High                                  |
| Overtaking boundary         | 112.5° from bow                    | COLREGs Rule 13(b) exact text                                | [R1]              | 🟢 High (law)                            |
| Substantial action          | ≥30° heading change                | Maritime practice; Rule 8(b) "readily apparent"              | [R7]              | 🟢 High                                  |
| Early action                | TCPA ≥ 120s at onset               | Norwegian AIS: mean 14-40 min; conservative for close-start  | [R14]             | 🟡 Moderate                              |
| Past-and-clear              | Target abaft + distance increasing | COLREGs Rule 8(d), 13(d); Cockcroft & Lameijer               | [R1][R7]          | 🟢 High                                  |
| Premature give-way          | <10° in hold phase                 | Rule 17(a)(i) "shall keep course and speed"                  | [R1]              | 🟢 High                                  |
| Path length ratio           | ≤1.35                              | η = d₁/d₂ efficiency metric                                  | [R16]             | 🟡 Moderate                              |

## Appendix B: Academic Evaluation Frameworks

Three major COLREGs evaluation frameworks from the literature provide theoretical anchors for this design:

### B.1 Woerner et al. (2019) — MIT "Road Test"

**Reference**: Woerner, Benjamin, Novitzky, Leonard. *Quantifying protocol evaluation for autonomous collision avoidance*. Autonomous Robots 43(4):967-991. DOI: [10.1007/s10514-018-9779-y](https://doi.org/10.1007/s10514-018-9779-y)

**Dual-metric architecture** — directly maps to our safety + colregs split:
- **Safety score** = f(CPA range, CPA pose) — scalar 0-100%
- **Protocol compliance** = rule-specific score per Rules 13-17 → 0-100%
- Uses three threshold rings: collision (red), near-miss (yellow), acceptable range (green)
- COLREGs rules converted into mathematical functions using relative bearing and contact angle at CPA

**Adoption**: Our `safety_quality` and `colregs_quality` scores follow this dual-metric approach.

### B.2 Brekke, Johansen, Hagen et al. (2023) — NTNU/FFI

**Reference**: Hagen, Vassbotn, Skogvold, Johansen, Brekke. *Safety and COLREG evaluation for marine collision avoidance algorithms*. Ocean Engineering 288:115991. DOI: [10.1016/j.oceaneng.2023.115991](https://doi.org/10.1016/j.oceaneng.2023.115991)

**Penalty function approach** — parametrized weights for continuous scoring:
- Head-on potential: `K_HO = 40`, steepness `α_x = 1/500, α_y = 1/400`
- Give-way potential: `K_GW = 40`, port-side bias `y_0 = -500 m`
- Course-derivative penalty: `K_χ̇ = 2.5` — penalizes non-smooth maneuvers
- SOG-derivative penalty: `K_U̇ = 0.3` — penalizes rapid speed changes
- COLREGs collision cost: proportional to `1/t^q` where `q=4`, prediction horizon `T=300s`

**2024 extension**: Hagen, Murvold, Johansen, Brekke. *Grounding hazard considerations*. Ocean Engineering 308:118204. Adds ENC-based grounding penalty when COLREGs action is restricted by land.

**Adoption**: Our Layer 6 seamanship evaluation could adopt the course-derivative penalty `K_χ̇` as a continuous smoothness metric, replacing the discrete `steering_reversals` count.

### B.3 Gleeson, Dunbabin, Ford (2024) — QUT

**Reference**: *COLREG Scenario classification and Compliance Evaluation with temporal and multi-vessel awareness*. Ocean Engineering 313(Part 3):119552. DOI: [10.1016/j.oceaneng.2024.119552](https://doi.org/10.1016/j.oceaneng.2024.119552)

**Two contributions**:
1. **TAG-CSC**: Temporally Aware Geometric COLREG Scenario Classification — adds hysteresis + confidence estimate to maintain temporal stability
2. **CCE**: COLREG Compliance Evaluation — alternative penalty functions with vehicle reaction time approach and uncertainty bounds
- First framework to aggregate CCE scores for >2 vessels using collision-risk-based method

**Adoption**: The TAG-CSC hysteresis approach should inform our M6 classification stability requirements. The 2-5° hysteresis at sector boundaries (recommended in the paper) should be adopted in M6 to prevent Rule14↔Rule15 chattering.

### B.4 Stankiewicz & Mullins (2019) — Good Seamanship Quantification

**Reference**: *Quantifying Good Seamanship For Autonomous Surface Vessel Performance Evaluation*. OCEANS 2019. [par.nsf.gov/servlets/purl/10268283](https://par.nsf.gov/servlets/purl/10268283)

**Two dimensions**:
1. **Reduction of overall collision risk** — measured by maximum mutual ship domain violation
2. **Early, appropriate action** — whether the ASV takes timely action before risk becomes critical

**Adoption**: Our Layer 6 should add an "overall risk reduction" metric: `max_domain_violation` across all targets, not just the primary threat.

### B.5 Pedersen et al. (2023) — 55-Scenario Extension

**Reference**: Pedersen, Vasanthan, Karolius et al. *Generating Structured Set of Encounters for Verifying Automated Collision and Grounding Avoidance Systems*. ICMASS 2023. DOI: [10.1088/1742-6596/2618/1/012013](https://doi.org/10.1088/1742-6596/2618/1/012013)

Extends Imazu-22 to **55 scenarios** with systematic rule coverage. Open-source toolbox: [github.com/dnv-opensource/ship-traffic-generator](https://github.com/dnv-opensource/ship-traffic-generator)

**Adoption**: When expanding beyond 8-probe, use Pedersen's systematic generation to ensure complete rule coverage. The 55-scenario set covers Rules 2, 8, 13/16, 13/17, 14, 15/16, 15/17.

### B.6 ClassNK Evaluation Area Diagrams (2025)

**Reference**: Nakamura & Yamada. *Objective evaluation criteria for the safety certification of autonomous navigation system*. ICMASS 2025. DOI: [10.1088/1742-6596/3123/1/012032](https://doi.org/10.1088/1742-6596/3123/1/012032)

Three risk zones:
- **Safety area**: no constraint
- **Caution area**: based on relative distance + bearing change rate
- **Danger area**: imminent collision

If ANS navigates to avoid entering Caution/Danger areas, it qualifies as "reducing risk before risk exists."

**Adoption**: Our three-tier CPA model (danger/safety/quality) maps directly to this three-zone approach.

---

## Appendix C: Additional Missing Metrics from Literature

Based on the comprehensive literature survey, the following well-established metrics should be added in future iterations:

| Metric                                   | Definition                                                   | Source                          | Priority               |
| ---------------------------------------- | ------------------------------------------------------------ | ------------------------------- | ---------------------- |
| **Safe speed compliance** (Rule 6)       | Ratio of actual speed to COLREGs-appropriate speed given visibility/traffic | COLREGs Rule 6                  | P2                     |
| **Apparent maneuver magnitude** (Rule 8) | Binary: heading_change ≥ 30° within 2 prediction cycles      | Woerner 2019; Rule 8(b)         | P1 (already in design) |
| **Heading oscillation frequency**        | FFT on heading signal; flag if dominant frequency > 0.02 Hz (period < 50s) | Brekke/Hagen 2023 `K_χ̇`         | P2                     |
| **Multi-vessel risk accumulation**       | `Φ_S(t) = max_i(CRI_i) + Σ(secondary_risk)`                  | Stankiewicz 2019                | P3 (multi-ship phase)  |
| **Speed variation penalty**              | `K_U̇ = 0.3` on SOG derivative                                | Brekke/Hagen 2023; Eriksen 2020 | P2                     |
| **Grounding-aware evaluation**           | ENC-based grounding penalty when COLREGs action restricted by land | Hagen 2024                      | P3                     |

---

## Appendix D: Reference List

Same as Review Report Appendix A, plus:

| ID    | Source                                                   | Type          |
| ----- | -------------------------------------------------------- | ------------- |
| [R19] | Woerner et al. (2019) Autonomous Robots 43(4):967-991    | MASS 评估框架 |
| [R20] | Hagen, Brekke et al. (2023) Ocean Engineering 288:115991 | MASS 评估框架 |
| [R21] | Gleeson et al. (2024) Ocean Engineering 313:119552       | MASS 评估框架 |
| [R22] | Stankiewicz & Mullins (2019) OCEANS 2019                 | 良好船艺量化  |
| [R23] | Pedersen et al. (2023) ICMASS 2023                       | 场景生成      |
| [R24] | Nakamura & Yamada (2025) ICMASS 2025                     | 认证评估      |
| [R25] | Hagen et al. (2024) Ocean Engineering 308:118204         | 搁浅感知评估  |
| [R26] | Eriksen (2020) Frontiers in Robotics and AI              | Hybrid COLAV  |
| [R27] | Papadimitrakis (2021) Sensors 21-06959                   | MPC 风险效率  |