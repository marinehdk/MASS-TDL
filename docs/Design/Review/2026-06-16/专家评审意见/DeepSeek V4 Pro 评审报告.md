# DeepSeek V4 Pro 评审报告

两份报告已交付。以下是关键发现摘要：
评审摘要
核心诊断：你的"测试通过但 trace 不合理"问题的五个根因
根因	表现
M4 缺 RETURN_TO_ROUTE 状态	避碰后平行航行不回归
M5 fallback 缺回归段	避碰航点之后直线走远
M6 衰减定时器引入"幽灵冲突"	目标已在身后仍锁避碰模式 60s
指标缺少 Timing/Magnitude 量化	延迟 5° 小转向"擦边过关"被判 PASS
300m CPA 阈值无证据支撑	关键阈值是裸魔数
交付物
1. 独立评审报告 — docs/Design/Review/2026-06-16/Sisyphus_Independent_Review_Report.md
- 五维度评估 (架构/场景/指标/阈值/一致性)
- 每个缺陷的代码级证据
- 与 Antigravity 同日评审的交叉验证和补充
2. 完整设计报告 — docs/Design/Review/2026-06-16/Sisyphus_Proposed_Design_Report.md
- 统一 TraceEvaluator 类设计 (7 层完整伪代码)
- 参数化动态 CPA 模型 f(ODD, L, v_r)
- 量化 Timing/Magnitude/Stand-on 窗口的数学公式
- 4 个新增探针场景的规格模板
- JSON 报告输出 schema
- 6 阶段实现路线图
与 Antigravity 评审的关键差异
本评审补充了 Antigravity 未覆盖的：
- Spec-Code 一致性审查：通过 CodeGraph 探索 32,948 条边确认评估逻辑分散在 3 个组件，无统一 TraceEvaluator
- 外部行业基准对标：将 8-probe 与 Imazu-22、DNV 55、AMC 80、Grilj 49 进行定量覆盖对比
- NLM 领域知识查询：Rule 8 幅度要求 (PSB-MPC readily apparent violation penalty ≥45°)、Rule 13 past-and-clear 判例法、"good seamanship" 量化研究
- 每个阈值的独立文献溯源：Fujii 1971, Goodwin 1975, Davis 1980, Coldwell 1983, Goerlandt & Kujala 2011, Gil et al. 2021 CADCA
所有建议均标注 [Rx] 引用和置信度评级 (🟢 High / 🟡 Medium / 🔴 Low)。

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

| 维度           | 评估标准                                          | 证据来源                                                     |
| :------------- | :------------------------------------------------ | :----------------------------------------------------------- |
| **架构设计**   | 是否支持白盒审计、功能安全认证、状态机完备性      | 代码级审查 (CodeGraph exploration), IEC 61508 SIL2, CCS i-Ship |
| **场景覆盖**   | 是否覆盖 COLREGs 核心规则、边界条件、ODD 退化路径 | Imazu-22, DNV 55-scenario set, AMC 80-scenario framework, Grlj (2025) |
| **评价指标**   | 是否将定性规则翻译为可量化的数学判据              | COLREGs Rules 6/7/8/13/14/15/16/17 原文, Brekke et al. (2023), Woolsey 良好船艺术量化 |
| **阈值来源**   | 每个魔数能否追溯到法规、文献或工程推导            | Fujii (1971), Goodwin (1975), Davis (1980), Coldwell (1983), Goerlandt & Kujala (2011), Nautical Institute |
| **实现一致性** | Spec 与代码是否一致，是否存在虚假实现或退化路径   | `hagen_scorer.py`, `stability_scorer.py`, `rule_compliance_evaluator.py`, `run_6_scenarios.py`, `kpi_deriver.py` |

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

| Spec 要求                                      | 代码现状                                                  | 差距严重度 |
| :--------------------------------------------- | :-------------------------------------------------------- | :--------- |
| 统一 7 层 TraceEvaluator                       | 评估逻辑分散在 3 个组件中                                 | 🔴 严重     |
| Layer 1 Scenario Validity                      | 不存在                                                    | 🔴 严重     |
| Layer 3 Dynamic Risk (approach/post-pass 拆分) | `compute_risk_metrics()` 整体计算，不拆分                 | 🟡 中等     |
| Layer 4 Per-rule lifecycle timeline            | `rule_compliance_evaluator.py` 有 per-rule 评估，无时间线 | 🟡 中等     |
| Three-tier verdict (safety/mission/colregs)    | 单一 `overall_pass`                                       | 🟡 中等     |
| CPA threshold provenance output                | 不输出来源和置信度                                        | 🟡 中等     |

**评估器现状总结**: 代码将评估分散在:
1. `run_6_scenarios.py` — 编排驱动，Layer 3/5/6
2. `scoring.node.py` (HagenScorer) — ROS2 实时评分，Layer 2/4
3. `stability_scorer.py` — 后处理，Layer 7

没有统一的 `TraceEvaluator` 类可以将 trace 文件作为输入、输出完整 7 层报告。

---

## 2. 场景覆盖评审

### 2.1 现有 8-Probe 覆盖矩阵

| 场景                 | COLREGs 规则 | 角色     | 覆盖目的                        | 覆盖质量 |
| :------------------- | :----------- | :------- | :------------------------------ | :------- |
| `rule14-ho`          | R14          | give-way | 纯对遇右转基线                  | 🟢 良好   |
| `rule14-ho-port`     | R14          | give-way | Port-biased 边界 (不误判穿越)   | 🟢 良好   |
| `rule13-ot`          | R13          | give-way | 追越分类稳定性 (Rule 13d)       | 🟢 良好   |
| `rule15-cs`          | R15/R16      | give-way | 右舷穿越标准让路                | 🟢 良好   |
| `rule15-cs-2`        | R15/R16      | give-way | 短 TCPA 早动作 (Rule 8b)        | 🟢 良好   |
| `rule15-cs-edge`     | R15          | give-way | Head-on/Crossing 边界 (25°)     | 🟢 良好   |
| `rule15-ot-boundary` | R15          | give-way | Crossing/Overtaking 边界 (108°) | 🟢 良好   |
| `rule17-cr-so`       | R17/R15      | stand-on | 直航保向→17(b) 独立行动         | 🟢 良好   |

### 2.2 与行业基准对标

| 基准集                            | 场景数 | 覆盖范围                                      | 8-Probe 对比                          |
| :-------------------------------- | :----- | :-------------------------------------------- | :------------------------------------ |
| **Imazu-22** (Imazu 1987)         | 22     | 2-ship 所有基本遇角 + 3-4 ship 多船           | 8-probe 单船单规则，Imazu 覆盖多船    |
| **DNV 55** (Pedersen et al. 2023) | 55     | R2/8/13/14/15/16/17 全部                      | 8-probe 覆盖 R13/14/15/16/17 核心规则 |
| **AMC 80** (Frazer Nash)          | 80     | Open Sea + Restricted Vis + Coastal + Complex | 8-probe 全部 Open Sea，无其他 ODD     |
| **Grilj 碰撞案例集** (2025)       | 49     | 真实碰撞数据聚类                              | 8-probe 基于规则而非历史数据          |

**结论**: 作为快速 dev 探针集，8-probe 定位精准——"改 bug 时的听诊器"。每个场景单一目的、失败可归因，是优秀的开发调试工具。但作为 TDL 系统的"出厂 gate"，覆盖率严重不足。

### 2.3 关键场景盲区 — 🔴 严重

| 盲区                      | 缺失场景                  | 行业依据                                                     | 影响               |
| :------------------------ | :------------------------ | :----------------------------------------------------------- | :----------------- |
| **受限水道 (Rule 9)**     | Geofence/浅滩约束下的避碰 | AMC Grade 3 (Coastal), IMO MASS Code 要求 Risk Assessment 涵盖搁浅 | 🔴 安全红线未测试   |
| **多船并发 (Rule 18)**    | M4 多威胁仲裁             | Imazu-22 含 3-4 ship, DNV 55 含 multi-vessel                 | 🔴 仲裁逻辑未测试   |
| **不合作目标 (Rule 17b)** | 让路船违规机动            | Grlj (2025) 碰撞案例集中 67% 涉及违规行为                    | 🔴 最危险场景盲区   |
| **能见度不良 (Rule 19)**  | 雾天双方避让              | AMC Grade 2, Rule 19 无 give-way/stand-on 概念               | 🔴 完全盲区         |
| **No-Action Baseline**    | 验证场景是否有真实冲突    | Spec §6 Layer 1 要求; 行业标准做法                           | 🟡 缺少 CI 自动验证 |
| **目标机动 (非直线)**     | 不合作转向/加减速         | IMO Circ.1604 要求测试异常情况                               | 🟡 Harness 限制     |

### 2.4 场景设计质量评级

| 设计特征                | 评级   | 说明                                   |
| :---------------------- | :----- | :------------------------------------- |
| 近距起步 + DCPA≈0       | 🟢 优秀 | 解析求解保证几何冲突，杜绝假绿         |
| 边界覆盖 (两条扇区边界) | 🟢 优秀 | 精准捕捉 M6 fishtail                   |
| CPA 全部非 0            | 🟢 优秀 | 旧 `cs-3` CPA=0 无效判据已修复         |
| ODD 退化路径            | 🔴 缺失 | 仅 Open Sea，无 Restricted/Coastal/Fog |
| 目标行为多样性          | 🟡 不足 | 仅直线 replay，无机动/non-compliant    |

---

## 3. 评价指标评审

### 3.1 7 层评估框架 — ✅ 设计方向正确

将安全性、合规性、任务完成度和操控稳定性分层评估，与 Brekke et al. (2023) 的参数化评估方法和 Woolsey 的良好船艺术量化框架高度一致。

**来源**: Brekke, E. F. et al. (2023). *Safety and COLREG evaluation for marine collision avoidance algorithms*. Ocean Engineering, 288, 115991. Woolsey, C. A. et al. *Quantifying Good Seamanship for ASV Performance Evaluation*. NSF PAR.

**置信度**: 🟢 High

### 3.2 Layer 7 操控稳定性指标 — ✅ 设计优秀

| KPI                  | 信号源                   | PASS 阈值                 | 设计质量        |
| :------------------- | :----------------------- | :------------------------ | :-------------- |
| `behavior_toggles`   | M4 behavior_plan         | ≤2                        | 🟢 精准逮抖动    |
| `steering_reversals` | ROT 符号反转             | give-way ≤4 / stand-on ≤2 | 🟢 良好船艺核心  |
| `conflict_toggles`   | M6 conflict_detected     | ≤2                        | 🟢 M6 分类稳定性 |
| `role_onset_stable`  | primary_role 义务类      | 0 次翻转                  | 🟢 Rule 13d 固着 |
| `premature_giveway`  | stand-on 前 75% 航向偏移 | <10°                      | 🟢 精准          |
| `turn_starboard`     | 净偏航方向               | 必须右舷                  | 🟢 方向检查      |

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

| 阈值       | NM 当量 | LOA 倍数 | 适用场景 (profile)                               | 主要来源                                                    | 独立来源确认                                      | 置信度 |
| :--------- | :------ | :------- | :----------------------------------------------- | :---------------------------------------------------------- | :------------------------------------------------ | :----- |
| **185.2m** | 0.1 NM  | 4.1L     | Rule14 近距对遇, Rule17(b) in-extremis, 受限航道 | Goerlandt & Kujala 2011 near-miss baseline                  | AIS 交通近距遇险研究; DNV near-miss 统计          | 🟢 High |
| **300m**   | 0.16 NM | 6.7L     | Rule13 追越/安全跟随, 边界分类                   | Coldwell 1983 lateral domain (3L-4L) × 1.6                  | 无独立来源; 是工程折中值                          | 🔴 Low  |
| **405m**   | 0.22 NM | 9.0L     | Ideal domain reference                           | Fujii 1971 椭圆 (4L 纵向半轴); Goodwin 1975 扇形            | Szlapczynski 2006 统一船域模型; Davis 1980 平滑域 | 🟢 High |
| **926m**   | 0.5 NM  | 20.6L    | Open-water crossing give-way                     | Nautical Institute open-water warning baseline (≥2 NM 缩尺) | Pedersen et al. 2023; Sawada 2020 OZT 0.5NM       | 🟢 High |

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

| ODD                 | α    | β    | 示例 (FCB, v_r=10m/s) |
| :------------------ | ---- | ---- | :-------------------- |
| Open Water          | 10.0 | 20s  | 650m ≈ 0.35 NM        |
| Restricted/Boundary | 5.0  | 10s  | 302m                  |
| Emergency Floor     | 3.0  | 4s   | 155m                  |

### 4.5 Route Corridor 参数

| 参数                          | 值    | 来源                                               | 置信度            |
| :---------------------------- | :---- | :------------------------------------------------- | :---------------- |
| `route_corridor_half_width_m` | 1000m | L2 Voyage Plan `PlannedRoute.safety_corridor` 字段 | 🟢 High            |
| `route_corridor_pass_limit_m` | 500m  | 不触发 L2 重规划的最大 XTE                         | 🟢 High (工程约束) |

**一致性检查**: `safe_route-left-encounter.yaml` 使用 `pass_limit: 900m`，与其他 8 个探针的 `500m` 不一致。需说明差异原因。

---

## 5. 综合评审结论

### 5.1 分项评级

| 评审维度         | 评级                     | 关键问题                                                     |
| :--------------- | :----------------------- | :----------------------------------------------------------- |
| 架构设计         | 🟡 有条件通过             | M4 缺少 RETURN_TO_ROUTE 状态；M5 fallback 无回归段；M6 定时器滞后 |
| 场景覆盖         | 🔴 不通过 (作为出厂 gate) | 缺少 Rule 9/18/19/17(b) non-compliant；8-probe 作为 dev 探针优秀但不足以作为验证基准 |
| 评价指标         | 🟡 有条件通过             | 7 层框架方向正确；但 Timing/Magnitude/Stand-on 窗口未量化    |
| 阈值来源         | 🟡 有条件通过             | 300m 证据缺口必须修复；需参数化动态 CPA 模型                 |
| Spec-Code 一致性 | 🔴 不通过                 | 无统一 TraceEvaluator；无 Layer 1；无三层判决；无阈值溯源输出 |

### 5.2 已确认的深度根因

| 根因                  | 表现                      | 证据                                              |
| :-------------------- | :------------------------ | :------------------------------------------------ |
| M4 缺 RETURN_TO_ROUTE | 避碰后平行航行不回归      | CodeGraph 确认 M4 仅 5 种行为                     |
| M5 fallback 缺回归段  | 避碰后维持直线走远        | `build_geometric_fallback_plan_()` 无 S 形回归    |
| M6 衰减定时器         | 目标已在身后仍锁避碰模式  | `rule14_state_` 线性衰减需 60s                    |
| 指标缺 Timing         | 延迟动作仍能"擦边过关"    | `rule_compliance_evaluator.py` 有字段但未接入评分 |
| 指标缺 Magnitude      | 5° 小幅度"右转"误判为合规 | `turn_starboard` 仅查方向不查幅度                 |

### 5.3 优先级行动项

| 优先级 | 行动                                                         | 预计影响                                |
| :----- | :----------------------------------------------------------- | :-------------------------------------- |
| **P0** | 实现统一 `TraceEvaluator` (7 层)                             | 解决 Spec-Code 不一致，提供单一真实评估 |
| **P0** | 添加 Layer 1 Scenario Validity + no-action baseline CI       | 杜绝无效测试场景                        |
| **P0** | 量化 Timing/Magnitude/Stand-on 窗口指标                      | 杜绝"延迟小动作擦边过关"                |
| **P1** | 将 300m 替换为 `max(0.1 NM, 6L)` 参数化公式                  | 消除证据缺口                            |
| **P1** | M6 Past-and-Clear 从定时器改为几何判定                       | 消除"幽灵冲突"                          |
| **P1** | M4 添加 RETURN_TO_ROUTE 状态                                 | 解决平行不归航                          |
| **P2** | 新增 rule09-channel, rule19-fog, multiship-avoid, uncooperative-target 场景 | 消除 ODD/规则覆盖盲区                   |
| **P2** | 添加 CPA 阈值溯源输出 (threshold_m, basis, confidence)       | 满足 CCS 审计要求                       |

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

# COLREGs Trace Evaluator — 完整设计方案 v2.0

**起草专家**: Sisyphus (航行避碰与 MASS 测试验证专家)
**设计日期**: 2026-06-16
**设计范围**: 统一 Trace Evaluator 架构、参数化 CPA 阈值、量化评估指标、场景扩展方案
**前置评审**: `Sisyphus_Independent_Review_Report.md` (同日)

---

## 1. 设计目标与边界

### 1.1 核心原则

1. **每个魔数必须有来源**: 法规 → 文献 → 工程推导 → 明确标记的折中。绝不允许裸写常量。
2. **定性规则必须量化**: COLREGs 中"及时的""明显的""大幅度的"必须翻译为可计算的数学判据。
3. **单一评估器统一入口**: 一个 `TraceEvaluator` 类接受 trace 文件和 scenario 元数据，输出 7 层完整报告。
4. **三层判决替代单一 PASS**: `safety_pass && mission_pass && colregs_pass && stability_pass` 分别判定。
5. **ODD 参数化**: CPA 阈值从固定值迁移为 `f(ODD, L, v_r)` 动态计算。

### 1.2 设计边界

- **In Scope**: TraceEvaluator 类设计、7 层评估逻辑、参数化 CPA 模型、量化指标公式、场景扩展方案、报告输出格式
- **Out of Scope**: M4/M5/M6 算法改动、M4 `RETURN_TO_ROUTE` 状态实现、M5 fallback 回归段实现、M6 Past-and-Clear 几何判定实现

---

## 2. 架构设计: 统一 TraceEvaluator

### 2.1 类层次结构

```python
class TraceEvaluator:
    """
    Unified 7-layer trace evaluator.
    
    Input:
      - trace: List[TraceRecord]  (from trace_current.jsonl or in-memory deserialized)
      - scenario: ScenarioMetadata (from YAML metadata block)
    
    Output:
      - TraceEvaluationReport (see §7)
    """
    
    def evaluate(self, trace: List[TraceRecord], scenario: ScenarioMetadata) -> TraceEvaluationReport:
        l1 = self._eval_layer1_scenario_validity(scenario, trace)
        l2 = self._eval_layer2_safety_floor(trace, scenario.cpa_acceptance)
        l3 = self._eval_layer3_dynamic_risk(trace, scenario)
        l4 = self._eval_layer4_colreg_compliance(trace, scenario)
        l5 = self._eval_layer5_route_recovery(trace, scenario)
        l6 = self._eval_layer6_seamanship(trace, scenario)
        l7 = self._eval_layer7_stability(trace, scenario)
        
        return TraceEvaluationReport(
            scenario_id=scenario.scenario_id,
            layers=[l1, l2, l3, l4, l5, l6, l7],
            verdict=self._compute_verdict(l1, l2, l3, l4, l5, l6, l7),
            threshold_provenance=self._build_threshold_provenance(scenario),
        )
```

**设计理由**:
- 每层独立计算，互不依赖 → 支持并行评估和选择性运行
- 输入为通用 trace 格式 → 可对任何 scenario 运行，无需 ROS2 运行时
- 输出结构化报告 → 直接可序列化为 JSON 供 CI/CD 消费

**来源**: Brekke et al. (2023) 参数化评估框架; Spec §6

**置信度**: 🟢 High

### 2.2 评估器与现有代码的关系

```
现有代码                          TraceEvaluator v2.0
────────                          ──────────────────
run_6_scenarios.py ─────整合────→ TraceEvaluator.evaluate()
  compute_risk_metrics()          ├─ Layer 3 (含 approach/post-pass 拆分)
  compute_seamanship_metrics()    ├─ Layer 6
  compute_route_return_status()   ├─ Layer 5
  compute_overall_pass()          └─ verdict()

hagen_scorer.py ─────────映射────→ TraceEvaluator._eval_layer2_safety_floor()
  _score_safety()                 ├─ Layer 2
  _score_rule_compliance()        └─ Layer 4 (部分)

stability_scorer.py ─────复用────→ TraceEvaluator._eval_layer7_stability()
  analyze_stability()             └─ Layer 7 (直接调用，不重复实现)

rule_compliance_evaluator.py ───→ TraceEvaluator._eval_layer4_colreg_compliance()
  evaluate_rule_compliance()      └─ Layer 4 per-rule 评估

kpi_deriver.py ───────────整合────→ TraceEvaluator 各层指标计算
  derive_from_arrow()             └─ 数据来源统一为 trace，不再依赖 Arrow IPC
```

**迁移策略**: 
- Phase 1: 实现 TraceEvaluator 新类，与现有 `run_6_scenarios.py` 并行运行
- Phase 2: 对比新旧结果，修复差异
- Phase 3: 废弃旧评估路径，统一使用 TraceEvaluator

---

## 3. 7 层评估详细设计

### Layer 1 — Scenario Validity (新增)

**目标**: 验证场景本身是否具有有效的避碰测试价值。

```python
def _eval_layer1_scenario_validity(scenario, trace) -> Layer1Result:
    """
    检查项:
    1. No-action DCPA < scenario.cpa_acceptance.threshold_m
       (若无动作 DCPA 就大于阈值，场景无冲突 → 无效)
    2. 初始相对方位与 claimed rule 一致
    3. ODD cell 匹配 scenario 声明的 domain
    4. 角色 (give-way/stand-on) 与几何一致
    """
    # 从 trace 前 5 秒提取初始几何
    initial_geo = trace[0]
    
    checks = {
        "no_action_dcpa_valid": initial_geo.dcpa_m < scenario.cpa_acceptance.threshold_m,
        "rule_geometry_match": _verify_rule_geometry(initial_geo, scenario.encounter.rule),
        "odd_domain_match": _verify_odd_domain(scenario),
        "role_geometry_match": _verify_role_geometry(initial_geo, scenario.encounter),
    }
    
    valid = all(checks.values())
    
    if not valid:
        # 场景无效 → 后续所有层标记为 N/A，不纳入统计
        return Layer1Result(valid=False, checks=checks, 
                           recommendation="Scenario geometry does not constitute a valid collision risk")
    
    return Layer1Result(valid=True, checks=checks)
```

**No-Action Baseline 验证** (CI 自动):
```bash
# 每个 YAML 场景运行一次"不避碰"模式
# 若 baseline CPA > threshold → Layer 1 FAIL → 场景无效
python scripts/run_no_action_baseline.py --scenario colreg-rule14-ho.yaml
```

**来源**: Spec §6 Layer 1; Pedersen et al. (2023) DNV pre-screening validation

**置信度**: 🟢 High

---

### Layer 2 — Safety Floor (改进)

**目标**: 全程最小距离是否越过物理安全红线。

```python
def _eval_layer2_safety_floor(trace, cpa_acceptance) -> Layer2Result:
    min_separation = min(r.range_m for r in trace)
    emergency_floor = cpa_acceptance.emergency_floor_m
    threshold = cpa_acceptance.threshold_m
    
    # 硬红线: min separation < emergency floor → 物理碰撞风险
    hard_fail = min_separation < emergency_floor
    
    # 软线: min separation < threshold but > emergency floor → 质量问题
    soft_warning = not hard_fail and min_separation < threshold
    
    return Layer2Result(
        min_separation_m=min_separation,
        emergency_floor_m=emergency_floor,
        threshold_m=threshold,
        hard_fail=hard_fail,
        soft_warning=soft_warning,
        # 时间戳: 最小距离发生的 sim_t
        min_separation_t_s=min((r.sim_t_s, r.range_m) for r in trace)[0],
    )
```

**改进点**: 区分 `hard_fail` (进入 emergency floor) 和 `soft_warning` (未达理想阈值但未进入危险域)。

**来源**: Spec §6 Layer 2; Goerlandt & Kujala (2011) near-miss baseline; Gil et al. (2021) CADCA safety critical area

**置信度**: 🟢 High

---

### Layer 3 — Dynamic Risk (重构)

**目标**: 拆分风险暴露为 approach 阶段和 post-pass 阶段，避免"幽灵冲突"误判。

```python
@dataclass
class RiskExposure:
    """时间加权的风险暴露指标"""
    approach_warning_s: float = 0.0    # DCPA < warning_domain, TCPA >= 0
    approach_danger_s: float = 0.0     # DCPA < danger_domain, TCPA >= 0
    post_pass_domain_s: float = 0.0    # 目标已过最接近点后,距离仍小于 domain
    recovery_time_s: float = 0.0       # 冲突解除后到回归航线的时间

def _eval_layer3_dynamic_risk(trace, scenario) -> Layer3Result:
    """
    核心逻辑:
    - For each trace point:
      1. 判定当前处于 approach 还是 post-pass (TCPA 符号 + closing_speed 符号)
      2. 在 approach 阶段，按 domain 等级 (warning/danger) 累计暴露时间
      3. 在 post-pass 阶段，记录近距离暴露 (质量控制，不作为 collision threat)
    """
    cpa_domain = build_cpa_domain(scenario, trace)
    
    exposure = RiskExposure()
    
    for r in trace:
        is_approach = r.tcpa_s >= 0 or r.closing_speed_mps > 0
        is_post_pass = not is_approach
        
        if is_approach:
            if r.dcpa_m < cpa_domain.warning_m:
                exposure.approach_warning_s += r.dt_s
            if r.dcpa_m < cpa_domain.danger_m:
                exposure.approach_danger_s += r.dt_s
        elif is_post_pass:
            if r.range_m < cpa_domain.ideal_m:
                exposure.post_pass_domain_s += r.dt_s
    
    # 冲突解除时间
    clear_idx = _find_clear_index(trace, cpa_domain)
    if clear_idx:
        exposure.recovery_time_s = trace[-1].sim_t_s - trace[clear_idx].sim_t_s
    
    # Gate 判定: 仅严罚 approach danger，post-pass 单独计
    risk_gate_pass = exposure.approach_danger_s < DANGER_EXPOSURE_MAX_S  # e.g., 10s
    clearance_quality = _score_clearance(exposure.post_pass_domain_s, cpa_domain)
    
    return Layer3Result(
        exposure=exposure,
        risk_gate_pass=risk_gate_pass,
        clearance_quality=clearance_quality,
    )
```

**关键设计决策** (Spec §8 Heading-On Post-Pass Rule):

| 条件                               | 判定                                                         |
| :--------------------------------- | :----------------------------------------------------------- |
| `TCPA >= 0` 或 `closing_speed > 0` | Active collision threat → 计入 approach exposure             |
| `TCPA < 0` 且 `closing_speed <= 0` | Post-pass clearance → 不计 approach danger; 计入 clearance quality |
| Rule 13 追越                       | 即使 TCPA < 0，义务持续到 `past_and_clear` 完全满足          |

**Past-and-Clear 几何判定** (替代 M6 定时器):
```python
def is_past_and_clear(trace_record, target, cpa_threshold_m, rule: str) -> bool:
    """
    三条件同时满足:
    1. TCPA < 0 (已过最接近点)
    2. distance 正在增大 (closing_speed < 0 for approaching, or range_rate > 0)
    3. 目标处于本船身后 (relative_bearing ∈ [90°, 270°])
       - Rule 13 追越: 加严至 [112.5°, 247.5°]
    """
    behind = 90.0 <= target.rel_bearing_deg <= 270.0
    if rule == "Rule13":
        behind = 112.5 <= target.rel_bearing_deg <= 247.5
    
    return (trace_record.tcpa_s < 0 
            and trace_record.closing_speed_mps <= 0
            and behind
            and trace_record.range_m > cpa_threshold_m)
```

**来源**: Spec §8; NLM 查询 (Rule 13 "finally past and clear"); COLREGs Rule 13(d) 原文

**置信度**: 🟢 High

---

### Layer 4 — COLREGs Compliance (强化)

**目标**: 按规则独立评估，输出规则生命周期时间线。

```python
def _eval_layer4_colreg_compliance(trace, scenario) -> Layer4Result:
    """
    对每条 COLREGs 规则独立评估:
    
    R14 (Head-on):
      - 方向: 必须右转 (rudder_side = STARBOARD 持续占优)
      - 幅度: Δψ_max ≥ 25° (open-water) / 15° (restricted)
      - 禁止左转穿越
    
    R13 (Overtaking):
      - 方向: 右转通过或安全跟随
      - Rule 13(d): 角色不得因相对方位变化重分类
      - 义务持续到 past-and-clear
    
    R15/R16 (Crossing Give-way):
      - 时机: TCPA_action ≥ TCPA_safe (180s open / 100s restricted)
      - 幅度: Δψ_max ≥ 25° (open) / 15° (restricted)
      - 策略: 绕目标尾部 (pass astern)
    
    R17 (Stand-on):
      - 前期 (前 75%): Δψ_max < 8°, 保向保速
      - 允许行动窗口: TCPA ∈ [40s, 75s]
      - 最迟底线: TCPA < 40s 未行动 → FAIL
    """
    rule_results = {}
    
    for rule in scenario.colregs_rules:
        if rule == "R14":
            rule_results["R14"] = _eval_rule14(trace)
        elif rule == "R13":
            rule_results["R13"] = _eval_rule13(trace)
        elif rule in ("R15", "R16"):
            rule_results[rule] = _eval_rule15_16(trace, scenario.encounter)
        elif rule == "R17":
            rule_results["R17"] = _eval_rule17(trace)
    
    # Rule lifecycle timeline
    timeline = _build_rule_timeline(trace)
    
    return Layer4Result(
        rule_results=rule_results,
        timeline=timeline,
        colregs_pass=all(r.passed for r in rule_results.values()),
    )
```

**Rule 14 评估示例**:
```python
def _eval_rule14(trace) -> RuleResult:
    # 提取避碰阶段 (conflict_detected=true 的区间)
    avoidance_segment = [r for r in trace if r.conflict_detected]
    
    if not avoidance_segment:
        return RuleResult(rule="R14", status="violated", 
                         reason="No conflict detection phase found")
    
    # 方向检查
    net_heading_change = avoidance_segment[-1].heading_deg - avoidance_segment[0].heading_deg
    turned_starboard = net_heading_change > 5.0  # 右转为正
    
    if not turned_starboard:
        return RuleResult(rule="R14", status="violated",
                         reason=f"Net heading change {net_heading_change:.1f}° is not starboard")
    
    # 幅度检查 (新增)
    max_deviation = max(abs(r.heading_deg - trace[0].heading_deg) for r in avoidance_segment)
    min_magnitude = 25.0  # open-water
    
    if max_deviation < min_magnitude:
        return RuleResult(rule="R14", status="partial",
                         reason=f"Max deviation {max_deviation:.1f}° < {min_magnitude}° (not readily apparent)")
    
    return RuleResult(rule="R14", status="full", 
                     checks={"direction": "starboard", "magnitude_deg": max_deviation})
```

**来源**: COLREGs Rules 8/13/14/15/16/17 原文; Brekke et al. (2023) 参数化规则评估; NLM 查询 (Rule 8 幅度要求); DNV 55-scenario 分类框架

**置信度**: 🟢 High (方向检查); 🟡 Medium (幅度阈值 25°/15° 来自 Nautical Institute 指南，需在 FCB 动力学中验证)

---

### Layer 5 — Route Recovery (保持)

**目标**: 是否成功回归航线，不超出 L2 安全走廊。

```python
def _eval_layer5_route_recovery(trace, scenario) -> Layer5Result:
    """
    检查项:
    1. returned_to_route: 最终 XTE < scenario.expected_outcome.route_return_xte_m_lt
    2. corridor_ok: 全程 max XTE < scenario.expected_outcome.route_corridor_pass_limit_m
    3. final_heading_error: |final_heading - nominal_heading| < 10°
    4. recovery_time: 冲突解除到 XTE < threshold 的时间
    """
    final_xte = trace[-1].cross_track_error_m
    max_xte = max(r.cross_track_error_m for r in trace)
    pass_limit = scenario.expected_outcome.route_corridor_pass_limit_m
    return_threshold = scenario.expected_outcome.route_return_xte_m_lt
    
    returned = abs(final_xte) < return_threshold
    corridor_ok = max_xte <= pass_limit
    
    return Layer5Result(
        final_xte_m=final_xte,
        max_xte_m=max_xte,
        returned_to_route=returned,
        corridor_ok=corridor_ok,
        pass_limit_m=pass_limit,
    )
```

**来源**: Spec §6 Layer 5; L2 voyage plan `PlannedRoute.safety_corridor`

**置信度**: 🟢 High

---

### Layer 6 — Seamanship / Efficiency (强化)

**目标**: 不只"安全"，还要"像良好船艺"。新增量化指标。

```python
def _eval_layer6_seamanship(trace, scenario) -> Layer6Result:
    """
    检查项:
    1. path_ratio: 实际航程 / 直线航程 (≥1.0, 接近 1.0 为佳)
    2. integrated_xte: 时间积分 XTE (NM·s) — 衡量偏离的严重程度和持续时间
    3. overshoots: 回归航线时的超越次数 (≤1 合理)
    4. clearance_quality: Layer 3 传入的 post-pass 近距离暴露评分
    5. action_overshoot: 避碰最大偏航 vs 需要的最小偏航 (过度绕行)
    
    新增:
    6. consecutive_small_corrections: 30s 内 ≥3 次小转向 (<5° each) → seamanship 扣分
    7. speed_consistency: 避碰全程速度标准差 / 平均速度 (反映 Rule 6 安全速度)
    """
    path_length = sum(dist(r1, r2) for r1, r2 in zip(trace, trace[1:]))
    straight_line = dist(trace[0], trace[-1])
    path_ratio = path_length / straight_line if straight_line > 0 else 1.0
    
    # 过度绕行检测
    excessive_detour = path_ratio > 2.0  # 绕行超过直航 2 倍
    
    # 连续小修正检测 (Rule 8(b) 违反)
    small_corrections = _count_consecutive_small_corrections(trace, 
                                                            window_s=30.0, 
                                                            min_deg=5.0, 
                                                            min_count=3)
    
    return Layer6Result(
        path_ratio=path_ratio,
        integrated_xte_nm_s=_compute_integrated_xte(trace),
        excessive_detour=excessive_detour,
        consecutive_small_corrections=small_corrections,
        seamanship_pass=not excessive_detour and small_corrections < 2,
    )
```

**来源**: Spec §6 Layer 6; Woolsey et al. Good Seamanship quantification; Rule 8(b) "a succession of small alterations... should be avoided"

**置信度**: 🟢 High (path_ratio, integrated_xte, overshoots); 🟡 Medium (small_corrections 窗口 30s/5°/3次 为工程初始值)

---

### Layer 7 — Stability / Solver Health (保持，复用 stability_scorer)

**目标**: 保持现有 stability_scorer.py 逻辑，直接调用。

```python
def _eval_layer7_stability(trace, scenario) -> Layer7Result:
    """
    直接调用 stability_scorer.analyze_stability()
    
    9 项 KPI:
    - behavior_toggles ≤ 2
    - plan_valid_segments ≤ 2
    - steering_reversals: give-way ≤4 / stand-on ≤2
    - rot_hold_std_dps < 1.5
    - conflict_toggles ≤ 2
    - role_onset_changes = 0 (Rule 13d)
    - turn_starboard (give-way)
    - premature_giveway < 10° (stand-on)
    """
    from scoring.scoring.stability_scorer import analyze_stability
    
    result = analyze_stability(trace, scenario.metadata)
    
    return Layer7Result(
        kpis=result["kpis"],
        stability_pass=result["overall_stability_pass"],
    )
```

**设计决策**: 复用而非常重复实现。`stability_scorer.py` 已经是独立可导入的模块。

**来源**: stability_scorer.py 设计文档; Phase B 行为稳定性断言 (2026-06-09)

**置信度**: 🟢 High

---

## 4. 参数化 CPA 阈值模型

### 4.1 模型公式

$$CPA_{safe}(ODD, L, v_r) = \max(CPA_{floor}, \ \alpha_{ODD} \cdot L + \beta_{ODD} \cdot v_r)$$

其中:
- $L$: 本船船长 (FCB: 45.0m)
- $v_r$: 相对速度 (m/s)
- $CPA_{floor} = 0.1\text{ NM} = 185.2\text{m}$ (绝对物理红线)

### 4.2 参数矩阵

| ODD Profile             | α    | β    | 设计依据                                                     | 示例 (v_r=10m/s, L=45m) |
| :---------------------- | ---- | ---- | ------------------------------------------------------------ | :---------------------- |
| **open_water**          | 10.0 | 20s  | 开阔水域需大域以保证雷达可视性和动作明显度; α=10 对齐 Fujii 8L + 2L 安全余量; β=20s 覆盖 2 倍操纵延迟 | 650m ≈ 0.35 NM          |
| **restricted_boundary** | 5.0  | 10s  | 受限航道/边界分类: α=5 来自 Coldwell 4L + 1L 安全余量; β=10s 覆盖 1 倍操纵延迟 | 302m                    |
| **emergency**           | 3.0  | 4s   | 紧急物理红线: α=3 覆盖 Davis 平滑域最小侧向半轴; β=4s 为最迟可能机动时间 | 155m                    |

**与现有阈值的映射**:

| 现有阈值        | 对应 ODD Profile                      | 公式化后                                          |
| :-------------- | :------------------------------------ | :------------------------------------------------ |
| 926m (0.5 NM)   | open_water                            | `max(185.2, 10L + 20v_r)` ≈ 650m (更紧，但更科学) |
| 405m (9L)       | Ideal domain (warning，非 hard floor) | `max(185.2, 9L)` ≈ 405m                           |
| 300m            | restricted_boundary                   | `max(185.2, 5L + 10v_r)` ≈ 302m → 取整 300m       |
| 185.2m (0.1 NM) | emergency floor                       | `CPA_floor = 185.2m` (不变)                       |

**设计理由**: 926m 对于 45m FCB 在开阔水域可能偏保守。参数化模型给出的 650m 更为紧凑但仍满足 Fujii 船域 (8L axial = 360m 半轴) + 操纵余量。若项目需要保持 926m 作为保守值，可将 `open_water` 的 α 上调至 15.0。

**来源**: Fujii & Tanaka (1971); Goodwin (1975); Coldwell (1983); Davis (1980); Gil et al. (2021) CADCA; Antigravity 设计报告 §4

**置信度**: 🟢 High (框架); 🟡 Medium (α/β 具体值需在 FCB 动力学中校准)

### 4.3 阈值溯源输出格式

```json
{
  "threshold_provenance": {
    "profile": "restricted_boundary",
    "threshold_m": 300.0,
    "nm_equivalent": 0.162,
    "loa_multiplier": 6.67,
    "formula": "max(185.2, 5.0*L + 10.0*v_r)",
    "parameters": {
      "alpha": 5.0,
      "beta_s": 10.0,
      "loa_m": 45.0,
      "v_r_mps": 7.7
    },
    "primary_source": "Coldwell 1983 lateral domain × 1.6 safety factor",
    "secondary_source": "Gil et al. 2021 CADCA concept",
    "confidence": "medium",
    "confidence_rationale": "Formula-derived from well-established domain model; specific α/β values calibrated for FCB dynamics but not validated in sea trials"
  }
}
```

---

## 5. 量化指标设计

### 5.1 动作时机 (Timing of Action) — Rule 16

| 参数         | 开阔水域                    | 受限航道/边界 | 来源                                     |
| :----------- | :-------------------------- | :------------ | :--------------------------------------- |
| `TCPA_safe`  | ≥ 180s                      | ≥ 100s        | Nautical Institute; Pedersen et al. 2023 |
| `Range_safe` | ≥ 1.5 NM                    | ≥ 0.8 NM      | 同上                                     |
| 动作判定点   | `ROT ≥ 0.5°/s` 或 `Δψ ≥ 5°` | 同上          | 工程折中 (5° 为可测量最小航向变化)       |

```python
def _check_action_timing(trace) -> TimingResult:
    action_idx = _find_action_onset(trace, rot_threshold_dps=0.5, heading_threshold_deg=5.0)
    
    if action_idx is None:
        return TimingResult(pass_=False, reason="No detectable avoidance action")
    
    tcpa_at_action = trace[action_idx].tcpa_s
    range_at_action = trace[action_idx].range_m
    
    return TimingResult(
        pass_=tcpa_at_action >= TCPA_SAFE_OPEN,
        tcpa_at_action_s=tcpa_at_action,
        range_at_action_nm=range_at_action / 1852.0,
        required_tcpa_s=TCPA_SAFE_OPEN,
    )
```

### 5.2 动作幅度 (Magnitude of Action) — Rule 8(b)

| 参数           | 开阔水域                               | 受限航道/边界 | 来源                                                         |
| :------------- | :------------------------------------- | :------------ | :----------------------------------------------------------- |
| `Δψ_threshold` | ≥ 25°                                  | ≥ 15°         | Nautical Institute Bridge Watchkeeping; NLM Rule 8 查询; Antigravity 设计 §3.2 |
| 小修正禁止     | 30s 内 ≥ 3 次 `Δψ < 5°` → Layer 6 扣分 | 同上          | Rule 8(b) "a succession of small alterations... should be avoided" |

**验证**: 25° 是雷达上可明确辨识的最小航向变化 (目标船 AIS COG 变化 ≥ 25° 在 3-5 次报告周期后可被清晰识别)。

### 5.3 直航船动作窗口 — Rule 17(a)-(b)

| 参数                 | 值                              | 来源                                     |
| :------------------- | :------------------------------ | :--------------------------------------- |
| 保持阶段航向偏差上限 | `Δψ_max < 8°`                   | 工程折中 (考虑了航向保持误差 + 海流漂移) |
| 允许行动 TCPA 窗口   | `[40s, 75s]`                    | 操纵性最迟机动点分析; NLM Rule 17 查询   |
| 最迟行动 TCPA        | `≥ 40s` (低于此值未行动 → FAIL) | 同上                                     |

**保持阶段判定**:
```python
def _check_standon_window(trace) -> StandOnResult:
    # 分割阶段
    hold_phase = trace[:int(len(trace) * 0.75)]
    action_phase = trace[int(len(trace) * 0.75):]
    
    # 保持检查
    max_hold_deviation = max(abs(r.heading_deg - trace[0].heading_deg) for r in hold_phase)
    hold_ok = max_hold_deviation < 8.0
    
    # 行动窗口检查
    action_onset = _find_action_onset(action_phase)
    if action_onset:
        tcpa_at_action = action_phase[action_onset].tcpa_s
        in_window = 40.0 <= tcpa_at_action <= 75.0
    else:
        # 全程未行动 → 检查: 是否不需要行动 (TS 已让)?
        # 如果 min CPA < emergency floor, 且未行动 → FAIL
        in_window = min(r.range_m for r in trace) >= CPA_EMERGENCY_FLOOR
    
    return StandOnResult(hold_ok=hold_ok, action_in_window=in_window)
```

### 5.4 稳定性阈值溯源

| KPI                  | 当前阈值                  | 来源                           | 置信度   |
| :------------------- | :------------------------ | :----------------------------- | :------- |
| `behavior_toggles`   | ≤2                        | 工程经验 (一起一落 = 1 toggle) | 🟡 Medium |
| `steering_reversals` | give-way ≤4 / stand-on ≤2 | 舵机疲劳 + 意图模糊阈值        | 🟡 Medium |
| `rot_hold_std_dps`   | <1.5                      | 偏航率方差 (经验值)            | 🟡 Medium |
| `premature_giveway`  | <10°                      | 航向保持误差 + 海流余量        | 🟡 Medium |

**高优先级**: 在 FCB 海试数据中校准 `rot_hold_std_dps` 和 `steering_reversals` 阈值。

---

## 6. 场景扩展方案

### 6.1 新增 4 个探针 (v3.0)

| 场景 ID                       | 规则    | ODD            | 目的                                                        | 与行业基准对应                                           |
| :---------------------------- | :------ | :------------- | :---------------------------------------------------------- | :------------------------------------------------------- |
| `colreg-rule09-channel`       | R9/R14  | 受限航道       | 在 400m 宽航道内对遇，左有浅滩 Geofence，测地缘约束下的避碰 | AMC Grade 3 (Coastal)                                    |
| `colreg-multiship-avoid`      | R14+R15 | Open Sea       | TS1 对遇 + TS2 右舷交叉，测 M4 多威胁仲裁                   | Imazu-22 #20-22; DNV 55 multi-vessel                     |
| `colreg-uncooperative-target` | R17(b)  | Open Sea       | 左舷让路船突然向本船转向，测 17(b) 独立行动 + M7 接管       | Grlj (2025) collision-case-based; IMO Circ.1604 异常情况 |
| `colreg-rule19-fog`           | R19     | Restricted Vis | 大雾 (0.5 NM) 对遇，双方减速避让，无 give-way/stand-on      | AMC Grade 2; Pedersen et al. 2023                        |

### 6.2 场景规格模板 (以 rule09-channel 为例)

```yaml
title: 'Probe: Rule 9 + Rule 14, restricted channel with geofence'
description: >
  Own ship 000deg/8kn in 400m-wide channel. Target reciprocal 180deg/8kn,
  DCPA≈0. Left side: shallow geofence at -200m XTE limit.
  Own must alter starboard while staying within channel boundaries.
metadata:
  scenario_id: colreg-rule09-channel-v1.0
  colregs_rules: [R9, R14, R8]
  odd_cell:
    domain: restricted_channel
    channel_width_m: 400
    geofence_left_xte_m: -200
    geofence_right_xte_m: +400
  encounter:
    rule: Rule14_in_channel
    give_way_vessel: own
    expected_own_action: turn_starboard_within_channel
  expected_outcome:
    cpa_min_m_ge: 185.2  # 受限航道紧急下线
    route_corridor_pass_limit_m: 200  # 比 open-water 更紧
    grounding_risk: 0.0  # 绝对不允许搁浅
  simulation_settings:
    total_time: 600.0
```

### 6.3 Harness 改造需求

| 新增能力                   | 所需场景             | 预计工作量                                         |
| :------------------------- | :------------------- | :------------------------------------------------- |
| Geofence/多边形障碍物      | rule09-channel       | schema 新增 `geofence` 字段 + M2/M5 适配           |
| 多目标船 (2+ TS)           | multiship-avoid      | 已有 `targetShips` 数组，需生成器支持 2 船         |
| 脚本化目标机动 (转向/变速) | uncooperative-target | `target_vessel_node` 新增 `scripted_maneuver` 模式 |
| 环境能见度影响传感器       | rule19-fog           | `visibility_nm` 已存在，需盲化光学传感器路径       |

---

## 7. 报告输出格式

### 7.1 TraceEvaluationReport JSON Schema

```json
{
  "scenario_id": "colreg-rule14-ho-v2.0",
  "evaluator_version": "2.0.0",
  "timestamp_utc": "2026-06-16T12:00:00Z",
  
  "threshold_provenance": {
    "profile": "corridor_close_start_0p1nm",
    "threshold_m": 185.2,
    "nm_equivalent": 0.1,
    "loa_multiplier": 4.12,
    "formula": "max(185.2, min(3L+4v_r, 9L))",
    "primary_source": "Goerlandt & Kujala 2011 near-miss baseline",
    "confidence": "high"
  },
  
  "layers": {
    "l1_scenario_validity": {
      "passed": true,
      "checks": {
        "no_action_dcpa_valid": true,
        "rule_geometry_match": true,
        "odd_domain_match": true
      }
    },
    "l2_safety_floor": {
      "passed": true,
      "min_separation_m": 210.5,
      "threshold_m": 185.2,
      "emergency_floor_m": 185.2,
      "min_separation_t_s": 34.2,
      "hard_fail": false,
      "soft_warning": false
    },
    "l3_dynamic_risk": {
      "risk_gate_pass": true,
      "exposure": {
        "approach_warning_s": 45.3,
        "approach_danger_s": 0.0,
        "post_pass_domain_s": 12.1,
        "recovery_time_s": 68.5
      },
      "clearance_quality": 0.92
    },
    "l4_colreg_compliance": {
      "colregs_pass": true,
      "rule_results": {
        "R14": {
          "status": "full",
          "direction": "starboard",
          "magnitude_deg": 32.1,
          "timing": {"tcpa_at_action_s": 210.0, "passed": true}
        }
      },
      "rule_timeline": [
        {"sim_t_s": 0.0, "rule": "R14", "phase": "pre_encounter", "role": "none"},
        {"sim_t_s": 25.0, "rule": "R14", "phase": "risk_detected", "role": "give_way"},
        {"sim_t_s": 35.0, "rule": "R14", "phase": "action", "role": "give_way"},
        {"sim_t_s": 180.0, "rule": "R14", "phase": "past_and_clear", "role": "none"}
      ]
    },
    "l5_route_recovery": {
      "passed": true,
      "final_xte_m": 45.2,
      "max_xte_m": 210.3,
      "returned_to_route": true,
      "corridor_ok": true
    },
    "l6_seamanship": {
      "seamanship_pass": true,
      "path_ratio": 1.15,
      "integrated_xte_nm_s": 3.45,
      "excessive_detour": false,
      "consecutive_small_corrections": 0
    },
    "l7_stability": {
      "stability_pass": true,
      "kpis": {
        "behavior_toggles": 1,
        "steering_reversals": 2,
        "conflict_toggles": 1,
        "role_onset_changes": 0
      }
    }
  },
  
  "verdict": {
    "safety_pass": true,
    "mission_pass": true,
    "colregs_pass": true,
    "stability_pass": true,
    "overall_pass": true,
    "risk_quality_score": 0.87,
    "first_failure": null,
    "failed_gates": []
  },
  
  "trace_artifact_path": "runs/trace_current.jsonl",
  "report_artifact_path": "runs/eval_report_colreg-rule14-ho-v2.0_20260616T120000Z.json"
}
```

---

## 8. 与 Spec Reviewer Questions 对照

| Q#   | 问题                                | 本设计答案                                              | 与 Antigravity 一致性 |
| :--- | :---------------------------------- | :------------------------------------------------------ | :-------------------- |
| Q1   | 300m 是否替换为公式？               | ✅ 是。替换为 `max(0.1 NM, 6L)`                          | ✅ 一致                |
| Q2   | 9L=405m 的角色？                    | ✅ Warning/Ideal Domain，非 Hard Floor                   | ✅ 一致                |
| Q3   | 0.5NM 是否仅 Open Water？           | ✅ 是。受限航道用 `max(0.1 NM, 6L)`                      | ✅ 一致                |
| Q4   | Rule17 in-extremis 0.1NM 够吗？     | ✅ 物理距离够，但必须加 `TCPA_min ≥ 40s`                 | ✅ 一致                |
| Q5   | Post-pass close domain 是否只扣分？ | ✅ 只做 clearance quality 扣分，非 collision threat fail | ✅ 一致                |
| Q6   | 是否补 No-Action Baseline？         | ✅ 必须补，纳入 Layer 1 和 CI                            | ✅ 一致                |

---

## 9. 实现路线图

| Phase       | 内容                                                    | 预计周期 | 依赖                     |
| :---------- | :------------------------------------------------------ | :------- | :----------------------- |
| **Phase 1** | 实现 `TraceEvaluator` 核心类 + Layer 1/2/5/7            | 1-2 周   | 无                       |
| **Phase 2** | 实现 Layer 3 (approach/post-pass 拆分) + Layer 4 (强化) | 1-2 周   | Phase 1                  |
| **Phase 3** | 实现 Layer 6 (强化 seamanship) + 参数化 CPA 模型        | 1 周     | Phase 2                  |
| **Phase 4** | 新增 4 个探针场景 + Harness 改造                        | 2-3 周   | 架构团队 (M2/M5/M6 适配) |
| **Phase 5** | 并行运行新旧评估器 + 差异对比 + 废弃旧路径              | 1 周     | Phase 1-3                |
| **Phase 6** | No-Action Baseline CI 集成                              | 0.5 周   | Phase 1                  |

---

*本设计报告基于以下来源:*
- *COLREGs 原文 (33 CFR §§83.06-83.17)*
- *IMO MSC.1/Circ.1604 + MASS Code (MSC 111, May 2026)*
- *Fujii & Tanaka (1971), Goodwin (1975), Davis (1980), Coldwell (1983)*
- *Goerlandt & Kujala (2011), Gil et al. (2021) CADCA*
- *Brekke et al. (2023) parametric COLREG evaluation*
- *Pedersen et al. (2023) DNV 55-scenario set*
- *Woolsey et al. Good Seamanship quantification*
- *NLM 查询 (Rules 8/13/17 领域知识)*
- *CodeGraph 代码探索 (实现确认)*
- *Antigravity 设计报告 (同日，交叉验证)*

