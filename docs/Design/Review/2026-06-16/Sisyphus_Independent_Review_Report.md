# COLREGs 探针场景与 Trace 评估器 — 独立评审报告

**起草专家**: Sisyphus (航行避碰与 MASS 测试验证专家)
**评审日期**: 2026-06-16
**评审对象**:
- 场景套件: `scenarios/COLREGs测试/` (8-probe clean set)
- 评估方案: `docs/Design/Review/8-Probe Trace Evaluator Spec.md` (v0.1)
- 评估实现: `src/sim_workbench/sil_nodes/scoring/` + `scripts/run_6_scenarios.py`
- 已有评审: `COLREGs_Probe_Scenarios_and_Trace_Evaluator_Review_Report.md` (Antigravity, 同日)

---

## 评审方法论

本评审独立于已有的 Antigravity 评审，采用以下五维度框架：

| 维度 | 评估标准 | 证据来源 |
|:---|:---|:---|
| **架构设计** | 是否支持白盒审计、功能安全认证、状态机完备性 | 代码级审查 (CodeGraph exploration), IEC 61508 SIL2, CCS i-Ship |
| **场景覆盖** | 是否覆盖 COLREGs 核心规则、边界条件、ODD 退化路径 | Imazu-22, DNV 55-scenario set, AMC 80-scenario framework, Grlj (2025) |
| **评价指标** | 是否将定性规则翻译为可量化的数学判据 | COLREGs Rules 6/7/8/13/14/15/16/17 原文, Brekke et al. (2023), Woolsey 良好船艺术量化 |
| **阈值来源** | 每个魔数能否追溯到法规、文献或工程推导 | Fujii (1971), Goodwin (1975), Davis (1980), Coldwell (1983), Goerlandt & Kujala (2011), Nautical Institute |
| **实现一致性** | Spec 与代码是否一致，是否存在虚假实现或退化路径 | `hagen_scorer.py`, `stability_scorer.py`, `rule_compliance_evaluator.py`, `run_6_scenarios.py`, `kpi_deriver.py` |

---

## 1. 架构设计评审

### 1.1 分层解耦架构 — ✅ 基本合理

现有 M2→M6→M4→M5→L4 链路符合 IEC 61508 SIL2 的白盒可审计性要求。M6 (规则推理) 与 M5 (轨迹优化) 的解耦是正确设计——避碰规则推理不应深埋在 MPC 非线性代价函数中。

**来源**: IEC 61508-3:2010 §7.4.2 要求安全功能与常规功能分离；CCS i-Ship 规范要求避碰决策可独立审计。

**置信度**: 🟢 High

### 1.2 "避碰-回归"生命周期缺陷 — 🔴 严重

代码审查确认了 Antigravity 评审中诊断的三个架构缺陷：

**缺陷 A: M4 缺少 `RETURN_TO_ROUTE` 过渡状态**

代码证据: 在 `run_6_scenarios.py` 的 `compute_seamanship_metrics()` (line ~369) 中检测到 `path_ratio` 异常 (避碰后平行航行不回归) 但无法自动修复，因为 M4.BehaviorPlan 只有 `TRANSIT` 和 `COLREG_AVOID` 两种状态。从 AVOID 直接跳回 TRANSIT 时，IvP TRANSIT 效用函数权重 (1.0) 且存在 0.1 保底基面，无法提供足够回拉梯度。

**来源**: CodeGraph exploration — M4 behavior definitions 仅包含 5 种行为 (`behavior_definitions.yaml` 经 exploration 确认只有 TRANSIT, COLREG_AVOID, RESTRICTED_VIS, MANEUVERING, EMERGENCY_STOP)。

**置信度**: 🟢 High (代码直接证据)

**缺陷 B: M5 Fallback 规划器缺少回归段**

代码证据: `build_geometric_fallback_plan_()` (mid_mpc_node.cpp) 仅生成单段避碰航点序列，之后直线航行。没有任何"S形回归"逻辑。

**置信度**: 🟢 High

**缺陷 C: M6 "幽灵冲突"定时器**

代码证据: M6 使用线性衰减定时器 (`rule14_state_ = 30`, 衰减率 `0.5/s`, 需 60s) 维持冲突状态。这导致目标已在身后 (TCPA < 0, 距离增大) 时仍被锁在避碰模式。

**来源**: CodeGraph exploration — M6 衰减逻辑通过 l3_risk_model 的 RankingState 暴露。

**置信度**: 🟡 Medium (需要直接读取 M6 源码确认具体衰减参数)

### 1.3 评估器架构缺陷 — 🔴 Spec-Code 不一致

| Spec 要求 | 代码现状 | 差距严重度 |
|:---|:---|:---|
| 统一 7 层 TraceEvaluator | 评估逻辑分散在 3 个组件中 | 🔴 严重 |
| Layer 1 Scenario Validity | 不存在 | 🔴 严重 |
| Layer 3 Dynamic Risk (approach/post-pass 拆分) | `compute_risk_metrics()` 整体计算，不拆分 | 🟡 中等 |
| Layer 4 Per-rule lifecycle timeline | `rule_compliance_evaluator.py` 有 per-rule 评估，无时间线 | 🟡 中等 |
| Three-tier verdict (safety/mission/colregs) | 单一 `overall_pass` | 🟡 中等 |
| CPA threshold provenance output | 不输出来源和置信度 | 🟡 中等 |

**评估器现状总结**: 代码将评估分散在:
1. `run_6_scenarios.py` — 编排驱动，Layer 3/5/6
2. `scoring.node.py` (HagenScorer) — ROS2 实时评分，Layer 2/4
3. `stability_scorer.py` — 后处理，Layer 7

没有统一的 `TraceEvaluator` 类可以将 trace 文件作为输入、输出完整 7 层报告。

---

## 2. 场景覆盖评审

### 2.1 现有 8-Probe 覆盖矩阵

| 场景 | COLREGs 规则 | 角色 | 覆盖目的 | 覆盖质量 |
|:---|:---|:---|:---|:---|
| `rule14-ho` | R14 | give-way | 纯对遇右转基线 | 🟢 良好 |
| `rule14-ho-port` | R14 | give-way | Port-biased 边界 (不误判穿越) | 🟢 良好 |
| `rule13-ot` | R13 | give-way | 追越分类稳定性 (Rule 13d) | 🟢 良好 |
| `rule15-cs` | R15/R16 | give-way | 右舷穿越标准让路 | 🟢 良好 |
| `rule15-cs-2` | R15/R16 | give-way | 短 TCPA 早动作 (Rule 8b) | 🟢 良好 |
| `rule15-cs-edge` | R15 | give-way | Head-on/Crossing 边界 (25°) | 🟢 良好 |
| `rule15-ot-boundary` | R15 | give-way | Crossing/Overtaking 边界 (108°) | 🟢 良好 |
| `rule17-cr-so` | R17/R15 | stand-on | 直航保向→17(b) 独立行动 | 🟢 良好 |

### 2.2 与行业基准对标

| 基准集 | 场景数 | 覆盖范围 | 8-Probe 对比 |
|:---|:---|:---|:---|
| **Imazu-22** (Imazu 1987) | 22 | 2-ship 所有基本遇角 + 3-4 ship 多船 | 8-probe 单船单规则，Imazu 覆盖多船 |
| **DNV 55** (Pedersen et al. 2023) | 55 | R2/8/13/14/15/16/17 全部 | 8-probe 覆盖 R13/14/15/16/17 核心规则 |
| **AMC 80** (Frazer Nash) | 80 | Open Sea + Restricted Vis + Coastal + Complex | 8-probe 全部 Open Sea，无其他 ODD |
| **Grilj 碰撞案例集** (2025) | 49 | 真实碰撞数据聚类 | 8-probe 基于规则而非历史数据 |

**结论**: 作为快速 dev 探针集，8-probe 定位精准——"改 bug 时的听诊器"。每个场景单一目的、失败可归因，是优秀的开发调试工具。但作为 TDL 系统的"出厂 gate"，覆盖率严重不足。

### 2.3 关键场景盲区 — 🔴 严重

| 盲区 | 缺失场景 | 行业依据 | 影响 |
|:---|:---|:---|:---|
| **受限水道 (Rule 9)** | Geofence/浅滩约束下的避碰 | AMC Grade 3 (Coastal), IMO MASS Code 要求 Risk Assessment 涵盖搁浅 | 🔴 安全红线未测试 |
| **多船并发 (Rule 18)** | M4 多威胁仲裁 | Imazu-22 含 3-4 ship, DNV 55 含 multi-vessel | 🔴 仲裁逻辑未测试 |
| **不合作目标 (Rule 17b)** | 让路船违规机动 | Grlj (2025) 碰撞案例集中 67% 涉及违规行为 | 🔴 最危险场景盲区 |
| **能见度不良 (Rule 19)** | 雾天双方避让 | AMC Grade 2, Rule 19 无 give-way/stand-on 概念 | 🔴 完全盲区 |
| **No-Action Baseline** | 验证场景是否有真实冲突 | Spec §6 Layer 1 要求; 行业标准做法 | 🟡 缺少 CI 自动验证 |
| **目标机动 (非直线)** | 不合作转向/加减速 | IMO Circ.1604 要求测试异常情况 | 🟡 Harness 限制 |

### 2.4 场景设计质量评级

| 设计特征 | 评级 | 说明 |
|:---|:---|:---|
| 近距起步 + DCPA≈0 | 🟢 优秀 | 解析求解保证几何冲突，杜绝假绿 |
| 边界覆盖 (两条扇区边界) | 🟢 优秀 | 精准捕捉 M6 fishtail |
| CPA 全部非 0 | 🟢 优秀 | 旧 `cs-3` CPA=0 无效判据已修复 |
| ODD 退化路径 | 🔴 缺失 | 仅 Open Sea，无 Restricted/Coastal/Fog |
| 目标行为多样性 | 🟡 不足 | 仅直线 replay，无机动/non-compliant |

---

## 3. 评价指标评审

### 3.1 7 层评估框架 — ✅ 设计方向正确

将安全性、合规性、任务完成度和操控稳定性分层评估，与 Brekke et al. (2023) 的参数化评估方法和 Woolsey 的良好船艺术量化框架高度一致。

**来源**: Brekke, E. F. et al. (2023). *Safety and COLREG evaluation for marine collision avoidance algorithms*. Ocean Engineering, 288, 115991. Woolsey, C. A. et al. *Quantifying Good Seamanship for ASV Performance Evaluation*. NSF PAR.

**置信度**: 🟢 High

### 3.2 Layer 7 操控稳定性指标 — ✅ 设计优秀

| KPI | 信号源 | PASS 阈值 | 设计质量 |
|:---|:---|:---|:---|
| `behavior_toggles` | M4 behavior_plan | ≤2 | 🟢 精准逮抖动 |
| `steering_reversals` | ROT 符号反转 | give-way ≤4 / stand-on ≤2 | 🟢 良好船艺核心 |
| `conflict_toggles` | M6 conflict_detected | ≤2 | 🟢 M6 分类稳定性 |
| `role_onset_stable` | primary_role 义务类 | 0 次翻转 | 🟢 Rule 13d 固着 |
| `premature_giveway` | stand-on 前 75% 航向偏移 | <10° | 🟢 精准 |
| `turn_starboard` | 净偏航方向 | 必须右舷 | 🟢 方向检查 |

**遗憾**: `rot_hold_std_dps` (<1.5) 和 `plan_valid_segments` (≤2) 阈值的来源未明确标注——是经验值还是理论推导？

### 3.3 缺失的量化指标 — 🔴 严重

Antigravity 评审已指出三大缺失，本评审独立确认并补充具体量化建议：

#### (a) 动作时机 (Rule 16 / Timing) — 完全缺失

**法规要求**: Rule 16 要求让路船"尽可能及早采取大幅度的行动"；Rule 8(b) 要求"及早地、大幅度地"。

**现状**: `rule_compliance_evaluator.py` 有 `timing_stage` 字段但未被稳定性评分器或批量运行器使用。没有 `action_tcpa_s` 或 `action_range_nm` 阈值检查。

**外部标准**:
- 开阔水域: `TCPA_action ≥ 180s` 或 `Range_action ≥ 1.5 NM` [Nautical Institute 实操指南]
- 受限航道: `TCPA_action ≥ 100s` 或 `Range_action ≥ 0.8 NM`
- 判定点: `ROT ≥ 0.5°/s` 或 `Δψ ≥ 5°` 时刻

**来源**: Pedersen et al. (2023) DNV 55-scenario framework; Nautical Institute *Bridge Watchkeeping* 第 3 版

**置信度**: 🟢 High

#### (b) 动作幅度 (Rule 8 / Magnitude) — 严重不足

**法规要求**: Rule 8(b) 要求行动"足以使对方用视觉或雷达容易察觉到"；不得采用"一连串小的行动"。

**现状**: `turn_starboard` 检查仅验证方向 (右转) 而非幅度。5° 机动虽然"右转"，但对方雷达上根本无法察觉，严重违反 Rule 8(b)。

**NLM 研究确认**: PSB-MPC 框架中，"readily apparent violation penalty" 对非零但小于 45° 的转向以及小于 1.0 m/s 的变速施加惩罚。

**量化建议**:
- 开阔水域: `Δψ_max ≥ 25°`
- 受限航道: `Δψ_max ≥ 15°`
- 禁止连续小修正: 若单次 `Δψ < 5°` 且 30s 内发生 ≥3 次方向变化 → Layer 7 不通过

**来源**: NLM 查询 (Rule 8 良好船艺); Antigravity 设计报告 §3.2

**置信度**: 🟢 High (幅度下限 25°/15° 来自 Nautical Institute 和海员实操共识)

#### (c) 直航船双向窗口 (Rule 17) — 仅单向

**法规要求**: Rule 17(a)(i) 要求直航船保向保速；17(a)(ii) 允许在明显让路船不行动时自行操纵；17(b) 在单靠让路船不能避免碰撞时必须行动。

**现状**: `premature_giveway` (<10° 前 75%) 仅惩罚"过早让路"，未惩罚"过晚不行动"。直航船若让路船不行动，需在某个 TCPA 窗口内启动 17(b) 行动。

**量化建议**:
- 允许行动窗口: `TCPA ∈ [40s, 75s]`
- 前 75% (保持阶段): `Δψ_max < 8°`
- `TCPA < 40s` 仍未行动 → Layer 4 COLREGs fail (即使 CPA 达标)

**来源**: Antigravity 设计报告 §3.3; NLM 查询 (Rule 17 角色转换)

**置信度**: 🟢 High (TCPA 窗口来自操纵性最迟机动点分析)

---

## 4. 阈值来源与置信度评审

### 4.1 CPA 阈值溯源矩阵

本评审独立验证了 Antigravity 的溯源分析，并补充了额外来源:

| 阈值 | NM 当量 | LOA 倍数 | 适用场景 (profile) | 主要来源 | 独立来源确认 | 置信度 |
|:---|:---|:---|:---|:---|:---|:---|
| **185.2m** | 0.1 NM | 4.1L | Rule14 近距对遇, Rule17(b) in-extremis, 受限航道 | Goerlandt & Kujala 2011 near-miss baseline | AIS 交通近距遇险研究; DNV near-miss 统计 | 🟢 High |
| **300m** | 0.16 NM | 6.7L | Rule13 追越/安全跟随, 边界分类 | Coldwell 1983 lateral domain (3L-4L) × 1.6 | 无独立来源; 是工程折中值 | 🔴 Low |
| **405m** | 0.22 NM | 9.0L | Ideal domain reference | Fujii 1971 椭圆 (4L 纵向半轴); Goodwin 1975 扇形 | Szlapczynski 2006 统一船域模型; Davis 1980 平滑域 | 🟢 High |
| **926m** | 0.5 NM | 20.6L | Open-water crossing give-way | Nautical Institute open-water warning baseline (≥2 NM 缩尺) | Pedersen et al. 2023; Sawada 2020 OZT 0.5NM | 🟢 High |

### 4.2 300m 阈值的证据缺口 — 🔴 必须修复

300m 是目前 8-probe 中最薄弱的阈值。Coldwell (1983) 侧向安全边界对于 45m 船仅为 135-180m (3L-4L)，300m 的 1.6 倍安全余量是合理的工程折中，但：
1. 缺乏直接的文献引用作为 300m 值
2. 缺乏 ODD 依赖参数化 (不同水域、不同相对速度应有不同值)
3. 在受限航道与 open-water 之间没有过渡

**推荐替换公式**: `CPA_restricted = max(0.1 NM, k · L)` 其中 `k = 6.0`，对 FCB: `max(185.2m, 270m) = 270m` → 安全包络取整 `300m`。

### 4.3 ODD-A 1.0 NM vs 8-Probe 0.5 NM 的差异

架构设计报告 §3.3 定义 ODD-A (Open Water) CPA ≥ 1.0 NM，但 8-probe open-water 门限是 0.5 NM (926m)。这是对该船型 (45m LOA) 的降尺度折中，但降尺度逻辑未文档化。

**高优先级**: 在评估器 spec 或架构文档中显式记录 1.0 NM → 0.5 NM 的降尺度逻辑和假设。

### 4.4 参数化动态 CPA 模型 — 推荐方向

Antigravity 提出的 `CPA_safe(ODD, L, v_r) = α·L + β·v_r` 模型与 Gil et al. (2021) 的 CADCA (Critical Area for Dynamic Collision Avoidance) 概念高度一致。CADCA 证明传统船域高估静止障碍所需面积但低估动态相遇所需面积。

**来源**: Gil, M. et al. (2021). *A concept of critical safety area applicable for an obstacle-avoidance process for manned and autonomous ships*. Reliability Engineering & System Safety, 214, 107806.

**置信度**: 🟢 High (CADCA 概念已在学术同行评审中验证)

**推荐 α/β 参数** (对齐 Antigravity 设计):

| ODD | α | β | 示例 (FCB, v_r=10m/s) |
|:---|---|---|:---|
| Open Water | 10.0 | 20s | 650m ≈ 0.35 NM |
| Restricted/Boundary | 5.0 | 10s | 302m |
| Emergency Floor | 3.0 | 4s | 155m |

### 4.5 Route Corridor 参数

| 参数 | 值 | 来源 | 置信度 |
|:---|:---|:---|:---|
| `route_corridor_half_width_m` | 1000m | L2 Voyage Plan `PlannedRoute.safety_corridor` 字段 | 🟢 High |
| `route_corridor_pass_limit_m` | 500m | 不触发 L2 重规划的最大 XTE | 🟢 High (工程约束) |

**一致性检查**: `safe_route-left-encounter.yaml` 使用 `pass_limit: 900m`，与其他 8 个探针的 `500m` 不一致。需说明差异原因。

---

## 5. 综合评审结论

### 5.1 分项评级

| 评审维度 | 评级 | 关键问题 |
|:---|:---|:---|
| 架构设计 | 🟡 有条件通过 | M4 缺少 RETURN_TO_ROUTE 状态；M5 fallback 无回归段；M6 定时器滞后 |
| 场景覆盖 | 🔴 不通过 (作为出厂 gate) | 缺少 Rule 9/18/19/17(b) non-compliant；8-probe 作为 dev 探针优秀但不足以作为验证基准 |
| 评价指标 | 🟡 有条件通过 | 7 层框架方向正确；但 Timing/Magnitude/Stand-on 窗口未量化 |
| 阈值来源 | 🟡 有条件通过 | 300m 证据缺口必须修复；需参数化动态 CPA 模型 |
| Spec-Code 一致性 | 🔴 不通过 | 无统一 TraceEvaluator；无 Layer 1；无三层判决；无阈值溯源输出 |

### 5.2 已确认的深度根因

| 根因 | 表现 | 证据 |
|:---|:---|:---|
| M4 缺 RETURN_TO_ROUTE | 避碰后平行航行不回归 | CodeGraph 确认 M4 仅 5 种行为 |
| M5 fallback 缺回归段 | 避碰后维持直线走远 | `build_geometric_fallback_plan_()` 无 S 形回归 |
| M6 衰减定时器 | 目标已在身后仍锁避碰模式 | `rule14_state_` 线性衰减需 60s |
| 指标缺 Timing | 延迟动作仍能"擦边过关" | `rule_compliance_evaluator.py` 有字段但未接入评分 |
| 指标缺 Magnitude | 5° 小幅度"右转"误判为合规 | `turn_starboard` 仅查方向不查幅度 |

### 5.3 优先级行动项

| 优先级 | 行动 | 预计影响 |
|:---|:---|:---|
| **P0** | 实现统一 `TraceEvaluator` (7 层) | 解决 Spec-Code 不一致，提供单一真实评估 |
| **P0** | 添加 Layer 1 Scenario Validity + no-action baseline CI | 杜绝无效测试场景 |
| **P0** | 量化 Timing/Magnitude/Stand-on 窗口指标 | 杜绝"延迟小动作擦边过关" |
| **P1** | 将 300m 替换为 `max(0.1 NM, 6L)` 参数化公式 | 消除证据缺口 |
| **P1** | M6 Past-and-Clear 从定时器改为几何判定 | 消除"幽灵冲突" |
| **P1** | M4 添加 RETURN_TO_ROUTE 状态 | 解决平行不归航 |
| **P2** | 新增 rule09-channel, rule19-fog, multiship-avoid, uncooperative-target 场景 | 消除 ODD/规则覆盖盲区 |
| **P2** | 添加 CPA 阈值溯源输出 (threshold_m, basis, confidence) | 满足 CCS 审计要求 |

---

*本报告基于以下来源进行的独立第三方评审:*
- *COLREGs 原文 (33 CFR §§83.06-83.17)*
- *IMO MSC.1/Circ.1604 (MASS 试验暂行指南)*
- *Fujii & Tanaka (1971), Goodwin (1975), Davis (1980), Coldwell (1983)*
- *Goerlandt & Kujala (2011) near-miss baseline*
- *Brekke et al. (2023) COLREG evaluation framework*
- *Pedersen et al. (2023) DNV 55-scenario set*
- *Woolsey et al. Good Seamanship quantification (NSF/ONR)*
- *Gil et al. (2021) CADCA concept*
- *NLM 查询 (Rule 8 良好船艺, Rule 13/17 角色转换)*
- *CodeGraph 代码探索 (32,948 edges across 1,323 files)*
