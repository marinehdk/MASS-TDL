# COLREGs Probe 场景与评估框架 — 完整评价报告

**评审专家**: OpenCode (航行避碰与MASS测试验证专家)
**评审日期**: 2026-06-16
**评审对象**: 8-probe 场景套件、7-Layer Trace Evaluator Spec v0.1、以及评估代码实现
**评审方法**: 独立第三方评审，采用 COLREGs 原文核对、学术文献追溯、代码级差距分析

---

## 目录

1. Executive Summary
2. Architecture Review — 7-Layer Evaluator 设计质量评估
3. Scenario Coverage Analysis — 按规则逐项覆盖差距表
4. Metric & Threshold Review — 每项阈值来源评估
5. Implementation vs Spec Gap Analysis — Spec vs 代码差距表
6. Risk Assessment — 哪些bug能漏检

---

## 1. Executive Summary

### 总体结论

8-probe 套件作为 **开发调试的"听诊器"** 定位精准、设计合理；7-Layer Evaluator Spec 的设计方向正确、分层原则符合国际学术共识。但系统存在 **五个严重等级** 的缺口，若不在验收前修复，以下类别 bug 将能通过当前的 pass/fail gate：

| 严重度 | 问题概要 | 影响 |
|:---|:---|---|
| 🔴 Critical | Generator vs YAML 不一致，导致"单一真源"承诺失效 | 后续测不准、审计不通过 |
| 🔴 Critical | 评估器未统一实现，7 层中 3 层完全缺失 | 关键评价维度归零 |
| 🔴 Critical | CPA 阈值 300m 无证据根基；gen_colreg_tier12.py 与 YAML 实际值冲突 | 阈值置信度崩塌 |
| 🟡 Major | 场景覆盖缺失 Rule 9/18/19 + 多船 + 不合作目标 | 四大类安全红线盲区 |
| 🟡 Major | 动作时机(Timing)/幅度(Magnitude)/直航窗口 未量化 | 延迟小动作可"擦边通过" |
| 🟢 Minor | No-action baseline 未实现 | 无效场景可能未被检出 |
| ⚪ Info | 总时长从 generator 的 260-420s 被 YAML 手动改为 1200s 但未同步 generator | 一致性管理问题 |

### 关键数据: Generator vs YAML CPA 阈值冲突

| 场景 | Generator `cpa_min_m_ge` | YAML 实际 `cpa_min_m_ge` | YAML `cpa_acceptance` profile |
|:---|:---|---:|:---|
| P1 ho | 926 | **185.2** | corridor_close_start_0p1nm |
| P2 ho-port | 926 | **185.2** | corridor_close_start_0p1nm |
| P3 ot | 926 | **300.0** | corridor_follow_or_overtake_0p1nm_to_9loa |
| P4 cs | 926 | 926.0 | open_water_warning_0p5nm ☑ |
| P5 cs-2 | 926 | 926.0 | open_water_warning_0p5nm ☑ |
| P6 cs-edge | **500** | **300.0** | corridor_boundary_0p1nm_to_9loa |
| P7 ot-boundary | **500** | **300.0** | corridor_boundary_0p1nm_to_9loa |
| P8 cr-so | **500** | **185.2** | standon_in_extremis_0p1nm |

**结论**: 除 P4/P5 外，所有 YAML 的 `cpa_min_m_ge` 都与 generator 冲突。gen_colreg_tier12.py 的 README 标注"勿手改 YAML、generator 是唯一真源"，但实际 YAML 已被手动编辑且 generator 不产生 `cpa_acceptance` / `route_return_*` / `route_corridor_*` 字段。这意味着 generator **已不是唯一真源**。

---

## 2. Architecture Review — 7-Layer Evaluator 设计质量评估

### 2.1 整体评价: 方向正确，实现严重落后

7 层分解将 **几何安全(Safety Floor)**、**动态风险(Dynamic Risk)**、**规则合规(COLREGs)**、**任务恢复(Route Recovery)**、**船艺质量(Seamanship)**、**操控稳定(Stability)** 进行正交拆分，与 Brekke et al. (2023) 以及 Woolsey 等人的参数化评估框架高度一致。这是正确的学术方向。

### 2.2 按层评估

| Layer | Spec 有？ | 代码实现？ | 评价 |
|:---|:---:|:---:|:---|
| L1 Scenario Validity | ✅ | ❌ | **完全缺失**。无 no-action baseline trace，无法证明每个场景确有冲突 |
| L2 Safety Floor | ✅ | 🟡 部分 | `kpi_deriver.py` 输出 `min_cpa_nm`，但 hard floor 检查在 `rule_compliance_evaluator.py` 中用硬编码 `cpa_target_nm=0.27` 而非场景指定值 |
| L3 Dynamic Risk | ✅ | ❌ | **完全缺失**。Spec 要求 approach_warning/danger 与 post-pass 拆分的 `exposure_s`，但无任何计算 |
| L4 COLREG Compliance | ✅ | 🟡 部分 | `rule_compliance_evaluator.py` 实现了 per-rule 逻辑，但 Rule 8 timing 来源于 trace 字段；无 per-rule lifecycle timeline |
| L5 Route Recovery | ✅ | ❌ | **完全缺失**。`kpi_deriver.py` 有 `route_deviation_nm` 但不分解返回状态 |
| L6 Seamanship / Efficiency | ✅ | ❌ | **完全缺失**。Spec 要求 `path_ratio`、overshoot、excessive detour 检测，无实现 |
| L7 Stability / Solver Health | ✅ | ✅ | `stability_scorer.py` 是唯一完整实现的层。但缺少 M5 solver health (plan_valid 段数已覆盖，但 solver_status 细化未覆盖) 和 M7 veto state 检查 |

**架构缺陷严重度**: 🔴 Critical — 7 层中只有 1 层完全实现，3 层完全缺失。

### 2.3 评估器碎片化问题

当前评估逻辑分布在 4 个独立组件中，无统一入口:

```
评估责任碎片化:
  kpi_deriver.py         → 8 项基础 KPI (IPC Arrow 文件输入)
  rule_compliance_evaluator.py → per-rule full/partial/violated (dict 输入)
  stability_scorer.py    → 行为稳定性 KPI + per-check pass/fail (JSONL trace 输入)
  run_6_scenarios.py     → 编排 + seamanship/path 指标 (A4000 host)
```

**问题**:
- 无 `TraceEvaluator` 类接收 trace JSONL 输出完整 7 层报告
- 各组件使用不同的输入格式和调用约定
- 三层判决 (safety/mission/colregs) 不存在——只有 `overall_pass = cpa_ok AND stability_pass`

**建议**: 重构为统一 `TraceEvaluator(trace_path) -> dict{layer1..layer7, verdict}`。

---

## 3. Scenario Coverage Analysis — 按规则逐项覆盖差距表

### 3.1 COLREGs 规则覆盖矩阵

参考: 33 CFR §§83.06-83.19, Imazu-22, DNV 55-scenario, AMC 80-scenario frameworks

| COLREGs 规则 | 场景覆盖 | 覆盖质量 | 缺什么 |
|:---|---:|:---|:---|
| **Rule 6** Safe Speed | ❌ | — | 无速度相关的"安全速度"探针；cs-2 虽然 TCPA 短但不测试减速选项 |
| **Rule 7** Risk of Collision | 🟡 Indirect | M2 CPA 基础 | 无"早期雷达探测 → 风险建立"时间顺序测试 |
| **Rule 8** Action to Avoid Collision | 🟡 部分 | 8 个场景均隐含 R8 | ❌ **无"大幅动作"幅度检查**；无"一连串小动作"禁则测试 |
| **Rule 9** Narrow Channels | ❌ | — | 🔴 **完全缺失**。无 Geofence/浅滩/航道边界下的避碰 |
| **Rule 10** Traffic Separation Schemes | ❌ | — | 🟡 TSS 非当前 ODD 要求，但可加低优先级 |
| **Rule 13** Overtaking | ✅ P3 | 🟢 良好 | 缺少追越中被追越船突然加速的场景 |
| **Rule 14** Head-on | ✅ P1,P2 | 🟢 良好 | 边界覆盖优秀(P2 + P6 夹击) |
| **Rule 15** Crossing | ✅ P4,P5,P6,P7 | 🟢 良好 | 边界覆盖质量高 |
| **Rule 16** Action by Give-way Vessel | 🟡 部分 | P4,P5 隐含 R16 | ❌ **无动作时机硬检查**——延迟动作仍可通过 |
| **Rule 17** Action by Stand-on Vessel | ✅ P8 | 🟢 良好 | 但无"让路船突然机动 → 本船 17(b) 末段响应"场景 |
| **Rule 18** Responsibilities (multi-ship) | ❌ | — | 🔴 **完全缺失**。无多船场景 (Imazu-22 含 3-4 ship) |
| **Rule 19** Restricted Visibility | ❌ | — | 🔴 **完全缺失**。无雾天/双方避让/减速场景 |

### 3.2 ODD 覆盖矩阵

| ODD 维度 | 覆盖 | 说明 |
|:---|---:|:---|
| Open Sea / Offshore Wind Farm | ✅ 全部 8 个 | 当前唯一 ODD |
| Restricted Waters / Channel | ❌ 缺 | 无航道约束避碰 |
| Fog / Low Visibility | ❌ 缺 | 无 visibility_nm < 1.0 场景 |
| Night | ❌ 缺 | 光学传感器退化 |
| Wind / Current Disturbance | ❌ 缺 | 所有场景 wind=0, current=0 |
| Degraded Sensor | ❌ 缺 | 无传感器降级路径 |

### 3.3 目标行为多样性

| 目标行为 | 覆盖 | 说明 |
|:---|---:|:---|
| 直线匀速 | ✅ 全部 | ais_replay_vessel 恒定航向航速 |
| 脚本化机动 | ❌ 缺 | Harness 限制：target_vessel_node 仅支持 replay/NCDM |
| 合作机动 | ❌ 缺 | 无 target 主动让路验证 (R17 中让路船合规行为的 baseline) |
| 不合作机动 | ❌ 缺 | 无让路船违规切入、突然减速 |
| 多目标异构行为 | ❌ 缺 | 无双船组合 |

### 3.4 与行业基准集对标

| 基准集 | 场景数 | 8-Probe 差异 |
|:---|---|:---|
| **Imazu-22** (Imazu 1987) | 22 | 8-probe 覆盖 R13/14/15/17 核心规则，但缺多船、R9、R19 |
| **DNV 55** (Pedersen et al. 2023) | 55 | 8-probe 覆盖约 8/55 ≈ 15% |
| **AMC 80** (Frazer Nash) | 80 | 8-probe 全部 Open Sea，0% 覆盖 Coastal/Restricted Vis/Complex |
| **Grlj 碰撞案例集** (2025) | 49 | 无基于真实碰撞数据的场景 |

---

## 4. Metric & Threshold Review — 每项阈值来源评估

### 4.1 CPA 四项阈值溯源

当前共有 4 个 CPA 阈值(185.2m / 300m / 405m / 926m)，分布在 4 个配置 profile 中。

#### 阈值 1: 185.2m (0.1 NM) — emergency_floor

| 评估项 | 内容 |
|:---|:---|
| **适用场景** | Rule14 close-start (P1,P2), Rule17 in-extremis (P8) |
| **文献来源** | Goerlandt & Kujala (2011) AIS near-miss baseline; 行业共识为近距避碰物理底线 |
| **船长倍数** | 4.1L (L=45m) |
| **项目文档标注** | ✅ "emergency_floor_m: 185.2" + basis 说明 |
| **置信度** | 🟢 **High** — 可接受 |
| **评审结论** | ✅ **通过**。0.1NM 作为紧急物理下限是行业标准做法，无需改动。但 YAML 将此值同时用作 P1/P2 的 `cpa_min_m_ge` 而 generator 要求 926m — 这是场景选型决策而非阈值本身的问题。 |

#### 阈值 2: 300m — corridor/crossing edge/boundary

| 评估项 | 内容 |
|:---|:---|
| **适用场景** | Rule13 ot (P3), cs-edge (P6), ot-boundary (P7) |
| **文献来源** | ❌ **无直接来源**。Coldwell (1983) 侧向安全边界 3L-4L = 135-180m，但 300m 是其 1.6-2.2 倍，是工程折中 |
| **船长倍数** | 6.7L |
| **项目文档标注** | 🔴 "需补证据" (Spec 自身标记)；YAML 无来源字段 |
| **置信度** | 🔴 **Low** — 无独立文献支撑 |
| **评审结论** | 🔴 **必须替换**。推荐 `max(0.1NM, k·L)` 参数化，k=6.0 → 对 FCB 为 max(185.2, 270) ≈ **300m**。这样保留 300m 的工程实践值，但根因变为可解释的公式。 |

#### 阈值 3: 405m (9×LOA) — ideal_domain

| 评估项 | 内容 |
|:---|:---|
| **适用场景** | 所有场景的 `ideal_domain_m` 参考值 (YAML 仅 P1,P3,P6,P7,P8 使用) |
| **文献来源** | Fujii & Tanaka (1971) 纵向半轴 4L (全长 8L)；Goodwin (1975) 扇形避让半径 4-6L；Szlapczynski (2006) 统一模型确认 |
| **船长倍数** | 9.0L |
| **项目文档标注** | ✅ YAML 有 `ideal_domain_m: 405.0` 但无来源引用 |
| **置信度** | 🟢 **High** — 学术共识 |
| **评审结论** | 🟢 **通过**。但不应用作 hard floor — 仅在 quality score 中扣分 |

#### 阈值 4: 926m (0.5 NM) — open_water_warning

| 评估项 | 内容 |
|:---|:---|
| **适用场景** | open-water give-way crossing (P4,P5) |
| **文献来源** | Nautical Institute open-water CPA ≥ 2 NM 对大型商船；对 45m MASS 降尺度至 0.5 NM；Sawada (2020) OZT 0.5NM 当量；Pedersen et al. (2023) |
| **船长倍数** | 20.6L |
| **项目文档标注** | ✅ YAML 有 `profile: open_water_warning_0p5nm` + basis |
| **置信度** | 🟢 **High** |
| **评审结论** | 🟢 **通过**。但需文档化 2 NM → 0.5 NM 降尺度逻辑 |

### 4.2 规则合规评估器阈值审查

`rule_compliance_evaluator.py` 中使用的阈值:

| 阈值 | 值 | 来源 | 置信度 |
|:---|:---|:---|---:|
| `cpa_target_nm` (R13) | 0.27 NM (500m) | 硬编码，与场景 YAML 冲突 | 🔴 Low — YAML 实际值为 300m |
| `cpa_target_nm` (R17) | 0.1 NM (185.2m) | 场景指定 | 🟢 OK |
| `cpa_target_nm` (R14) | 0.1 NM | 场景指定 | 🟢 OK |
| `cpa_target_nm` (R15/16) | 0.5 NM (926m) | 场景指定 | 🟢 OK |
| `substantial_deg` (R8) | 30° | NLM 学术共识 | 🟢 High |
| `early_min_tcpa_s` | 120s | Rule 8 "ample time" | 🟡 Medium — 项目工程决定 |
| `timely_min_tcpa_s` | 60s | 项目 TMR ≥ 60s ODD | 🟡 Medium |
| Rule 16 `early_min_tcpa_s` | 180s | Rule 16 穿越给更早要求 | 🟡 Medium |
| Rule 17(b) `trigger_min_tcpa_s` | 60s | 项目 TMR ≥ 60s | 🟡 Medium |

**关键缺陷**: `_eval_rule13` 中使用 `cpa_target_nm=0.27` (500m) 与 P3 YAML `cpa_min_m_ge: 300.0` 不一致。此硬编码在 P0.2 修复中未更新。

### 4.3 Stability Scorer 阈值审查

| 阈值 | 值 | 来源 | 置信度 |
|:---|:---|---:|:---|
| `max_behavior_toggles` | 2 | 工程经验 | 🟡 Medium |
| `max_plan_valid_segments` | 2 | 工程经验 | 🟡 Medium |
| `max_steering_reversals` | 4/5 | 船艺经验 | 🟡 Medium |
| `max_rot_hold_std_dps` | 0.8 | FCB 正常 trim ROT < 0.3°/s + 余量 | 🟡 Medium |
| `max_conflict_toggles` | 2 | 工程经验 | 🟡 Medium |
| `max_role_onset_changes` | 0 | Rule 13(d) 要求 | 🟢 High |
| `max_premature_giveway_deg` | 10.0 | 船艺经验 | 🟡 Medium |
| `min_give_way_turn_deg` | 30.0 | P0.1 修正；NLM 学术共识 ≥30° | 🟢 High |
| `rot_deadband_dps` | 0.5 | FCB trim 特性 | 🟢 High |

---

## 5. Implementation vs Spec Gap Analysis

### 5.1 全面差距矩阵

| Spec 要求 | 代码现状 | 差距 | 严重度 |
|:---|---|:---|:---:|
| 单一 TraceEvaluator 入口 | 分散在 4 组件 | 无统一入口 | 🔴 |
| Layer 1 Scenario Validity | 不存在 | 完全缺失 | 🔴 |
| Layer 2 Safety Floor min_separation | `kpi_deriver.min_cpa_nm` 基础值 | 有值但无 profile-aware hard floor check | 🟡 |
| Layer 3 Dynamic Risk exposure_s | 不存在 | 完全缺失 | 🔴 |
| Layer 4 Per-rule lifecycle timeline | `rule_compliance_evaluator` 有 per-rule 但无时间线 | 无时间序列 | 🟡 |
| Layer 5 Route Recovery | `kpi_deriver.route_deviation_nm` | 无 returned_to_route/ corridor 检查 | 🔴 |
| Layer 6 Seamanship / Efficiency | 不存在 | 完全缺失 | 🔴 |
| Layer 7 Stability / Solver Health | `stability_scorer.py` | 完整但无 M7 veto check | 🟢 |
| 三层判决 verdict | `overall_pass` 单一 | 无 safety/mission/colregs 分化 | 🟡 |
| CPA threshold provenance output | 不存在 | 无每个阈值的来源/置信度输出 | 🟡 |
| Generator → YAML 一致 | YAML 手动编辑产生额外字段 | 不一致 | 🔴 |
| No-action baseline CI | 不存在 | 完全缺失 | 🟡 |

### 5.2 Generator vs YAML 具体不一致

| 字段 | Generator 值 | YAML 实际值 |
|:---|---:|---:|
| `total_time` | 260-420s (per-scenario) | **1200s** (全部统一) |
| `cpa_min_m_ge` (P1 ho) | 926.0 | 185.2 |
| `cpa_min_m_ge` (P2 ho-port) | 926.0 | 185.2 |
| `cpa_min_m_ge` (P3 ot) | 926.0 | 300.0 |
| `cpa_min_m_ge` (P6 cs-edge) | 500.0 | 300.0 |
| `cpa_min_m_ge` (P7 ot-boundary) | 500.0 | 300.0 |
| `cpa_min_m_ge` (P8 cr-so) | 500.0 | 185.2 |
| `cpa_acceptance` | ❌ 不存在 | ✅ 手写 |
| `route_return_xte_m_lt` | ❌ 不存在 | ✅ 手写 |
| `route_corridor_half_width_m` | ❌ 不存在 | ✅ 手写 |
| `route_corridor_pass_limit_m` | ❌ 不存在 | ✅ 手写 |

**结论**: 当前 `gen_colreg_tier12.py` 重新生成 YAML 会**覆盖**手动添加的 `cpa_acceptance` / `route_return_*` / `route_corridor_*` 字段，且会用 generator 的默认值覆盖手动调整的 `cpa_min_m_ge`。这是 **操作危险** — 任何人运行 `python -m tools.sil.gen_colreg_tier12` 都会破坏当前的场景配置。

### 5.3 KPI Deriver 问题

`kpi_deriver.py` 中:
- `min_cpa_nm` 来自 IPC Arrow 文件中 `cpa_nm` 列的最小值 — 无 profile 比较，无 pass/fail 判定
- `tcpa_min_s` 从 simulation start 而非从 detection/action onset 计算
- `avg_rot_dpm` 全运行平均，而非避碰窗口内
- `grounding_risk_score` 为 stub (硬编码 1.0)
- `time_to_mrm_s` 为 stub (硬编码 0.0)
- ❌ 无 approach/post-pass 分解
- ❌ 无 Timing/Magnitude 指标

---

## 6. Risk Assessment — 哪些 bug 可以漏检

### 6.1 能通过当前 gate 的危险 bug 分类

#### 🔴 高冲击 (可能导至碰撞或违规)

| Bug 类别 | 漏检原因 | 现实后果 |
|:---|---|:---|
| **延迟大幅度转向** (TCPA=30s 时才 30° 右转) | 无 timing metric—CPA 可能仍达标 | 违反 Rule 16 早期性要求；对方无法预测 |
| **微小连续转向** (4×5° 右转累积 20° 偏航) | `turn_starboard` 只检查方向，不查单次幅度；`steering_reversals` ≤4 可能通过 | 违反 Rule 8(b)"一连串小动作"禁则 |
| **对遇中 15° 右转但 CPA 为 250m** | P1/P2 `cpa_min_m_ge` 为 185.2m → 250m > 185.2m 通过 | 实际接近距离不安全，碰撞风险未充分消除 |
| **M6 角色抖动但最终 CPA 达标** | `conflict_toggles` 检查仅依赖 M6 话题存在；若 bridge 未打补丁 → 自动降级为 n/a | 角色抖动可能导致突然停止避碰 |
| **直航船在撞前 35s 才行动** (17(b) 太晚) | `rule_compliance_evaluator` 中 17(b) trigger_tcpa ≥ 60s 检查；但整体 verdict 仍可能通过 | 来不及用舵避让 |

#### 🟡 中冲击 (违反良好船艺但安全保底)

| Bug 类别 | 漏检原因 | 现实后果 |
|:---|---|:---|
| **避碰后平行航行不归航** | `route_deviation_nm` 有值但无硬 gate；stability_scorer 只查行为不查归航 | 航线偏离，L2 触发不必要重规划 |
| **避碰动作过大超过 L2 corridor** | `route_corridor_pass_limit_m: 500m` 在 YAML 但评估器不使用 | 离开安全走廊 |
| **目标通过后距离仅 250m (post-pass)** | 无 post-pass clearance quality 检查 | 虽然碰撞已不迫在眉睫但过于贴近 |
| **规则边界反复跳但最终稳定** | `max_conflict_toggles` 上限 2 次翻转；但探头设计上可能存在 3 次翻转仍通过且 CPA 达标 | 边界分类不稳定 |
| **追越后过早回航** | R13 past-and-clear 无显式几何判定；仅依赖 `conflict_detected` false | 可能尚未 fully clear 就回航 |

#### 🟢 低冲击 (信息缺口但不直接危险)

| Bug 类别 | 漏检原因 |
|:---|---|
| **无场景无效检测** | Layer 1 不存在 — 若场景几何有错导致 DCPA > threshold，仍会跑 |
| **阈值来源不在评估输出中** | 无 provenance 输出 → CCS 验船师无法审计 |
| **总时长 1200s 但 generator 设为 260-420s** | 测试时间过长但不影响安全判断 |

### 6.2 模拟攻击测试 (Hypothetical Bug Injection)

| 假设 bug | 当前 gate 结果 | 正确期望 | 是否漏检 |
|:---|---|:---|---:|
| M6 总是延迟 120s 产生冲突 | CPA 可能仍达标 | 违反 Rule 8/Rule 16 早期性 | 🔴 漏检 |
| M5 只做 8° 右转而非 ≥30° | 方向正确(turn_starboard=通过)，CPA 接近阈值 | 违反 Rule 8 明显性 | 🔴 漏检 |
| M4 AVOID→TRANSIT→AVOID 抖动 3 次 | behavior_toggles=3 > 2 → RED 🟢 捕获 | 正确捕获 | 🟢 已捕获 |
| R17 stand-on 一开始就 12° 右转 | `premature_giveway`: 12° > 10° → RED 🟢 捕获 | 正确捕获 | 🟢 已捕获 |
| 无 M6 话题的 trace | `conflict_toggles`/`role_onset_stable` → n/a → 自降级通过 | 应标记为 insufficient trace | 🟡 漏检(降级问题) |

---

## 7. 综合评分

| 评审维度 | 评级 | 要修复的关键项 |
|:---|:---|:---|
| 架构设计 | 🟡 有条件的通过 | 统一 TraceEvaluator；实现 L1/L3/L5/L6；三层判决 |
| 场景覆盖 | 🔴 不通过(作为验收 gate) | 需补 R9/R17-noncompliance/R18/R19；无多船；无不合作目标 |
| 评价指标 | 🟡 有条件的通过 | 必须量化 Timing/Magnitude/Stand-on 窗口 |
| 阈值来源 | 🟡 有条件的通过 | 300m 必须公式化；cpa_target_nm=0.27 硬编码必须修复 |
| Generator vs 实现一致性 | 🔴 不通过 | generator 必须产生全部 YAML 字段 或 正式放弃"唯一真源"承诺 |
| 评估代码完备性 | 🔴 不通过 | 7 层中仅 1 层完全实现 |

---

*本报告基于以下来源:*
- *COLREGs 33 CFR §§83.06-83.19 原文*
- *IMO MSC.1/Circ.1604 MASS 试验暂行指南*
- *Fujii & Tanaka (1971), Goodwin (1975), Coldwell (1983)*
- *Goerlandt & Kujala (2011) near-miss baseline*
- *Brekke et al. (2023) COLREG evaluation framework, Ocean Engineering*
- *Pedersen et al. (2023) DNV 55-scenario framework*
- *Woolsey et al., Quantifying Good Seamanship (NSF/ONR)*
- *Gil et al. (2021) CADCA concept, Reliability Engineering & System Safety*
- *项目代码审查: gen_colreg_tier12.py, rule_compliance_evaluator.py, stability_scorer.py, kpi_deriver.py*
- *项目场景 YAML: 8 个 colreg-rule*.yaml 文件*
- *现有评审: Antigravity 评审报告, Sisyphus 独立评审报告*
- *NLM 查询 (maritime_regulations, colav_algorithms, ship_maneuvering notebooks)*
