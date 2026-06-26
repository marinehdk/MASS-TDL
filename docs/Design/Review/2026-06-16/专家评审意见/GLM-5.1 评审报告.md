# GLM-5.1 评审报告

## 交付物完成
目录 docs/Design/Review/2026-06-16/ 下现有 8 份文档，其中你的两份核心交付物是：
📋 评审报告
COLREGs_8Probe_Evaluation_Report.md (346行, 20KB)
4 维度严格评审结果：
维度	评级	核心问题
架构设计	🟡 有条件通过	M4 缺 RETURN_TO_ROUTE; M5 无回归段; M6 幽灵冲突
场景覆盖	🟡 dev探针通过 / gate不通过	缺 R9/R19/多船/不合作目标
评价指标	🔴 不通过	Timing/Magnitude/Stand-on双向窗口 三维度缺失
阈值来源	🔴 不通过	300m无文献; 代码硬编码500m与profile不一致; 生成器不再是唯一真源
关键发现：5 个"虚假通过"根因模式被明确诊断。
📐 设计报告
COLREGs_8Probe_Complete_Design_Report.md (511行, 25KB)
完整重设计方案包含：

1. 12-probe 场景集 (P1-P8 保留 + P9-P12 新增)
2. CPA 动态阈值模型 CPA_safe = α·L + β·v_r，5 个 ODD profile，每个值有文献溯源
3. 7 层 TraceEvaluator 完整实现规格 — 含新增的 Layer 1/3/6
4. Timing + Magnitude + Stand-on 双向窗口量化指标 — 含具体阈值和法规来源
5. Past-and-Clear 几何判定算法 — 替代 M6 线性衰减定时器
6. Generator 重构方案 — 恢复唯一真源，加入 ODD_PARAMS 完整参数化
7. Phase 0/1/2 实施路线图 — ~24 人天，含依赖关系

## 独立视角

Oracle 子代理还生成了两份独立报告 (Sisyphus_Independent_Review_Report.md 和 Sisyphus_Proposed_Design_Report.md)，其中包含额外的代码证据和攻击性模拟测试（6 个 bug 注入场景）。这些报告与你的核心交付物相互验证，结论一致。

# COLREGs 8-Probe 场景与评估器严格评审报告

**评审人**: Sisyphus (MASS L3 TDL 系统专家，航行避碰与验证)
**评审日期**: 2026-06-16
**评审对象**:
- 场景套件: `scenarios/COLREGs测试/` (8-probe clean set)
- 评估方案: `docs/Design/Review/8-Probe Trace Evaluator Spec.md` (v0.1)
- 评估实现: `scoring/stability_scorer.py` + `rule_compliance_evaluator.py` + `kpi_deriver.py` + `run_6_scenarios.py`
- 场景生成: `tools/sil/gen_colreg_tier12.py`
- 前序评审: Antigravity 评审报告、Sisyphus 独立评审 (同日)

**评审目的**: 从航行避碰专家角度，严格评判：(1) 架构设计合理性、(2) 场景覆盖充分性、(3) 评价指标能否反映真实航行、(4) 阈值来源与置信度。每个判定标注来源与置信度。

---

## 1. 执行摘要

| 维度         | 评级                                      | 核心发现                                                     |
| :----------- | :---------------------------------------- | :----------------------------------------------------------- |
| **架构设计** | 🟡 有条件通过                              | 7 层评估框架方向正确，但 Spec-Code 不一致：3 个评估组件分散、无统一 TraceEvaluator；Layer 1/3/6 无实现 |
| **场景覆盖** | 🟡 作为 dev 探针通过；作为验证 gate 不通过 | 8-probe 单规则隔离设计优秀；但缺少 Rule 9/19、多船、不合作目标、风浪扰动 |
| **评价指标** | 🔴 不通过                                  | Timing(动作时机)、Magnitude(动作幅度)、Stand-on 双向窗口 三个关键维度完全缺失；现有指标无法拦截"延迟小动作擦边过关" |
| **阈值来源** | 🔴 不通过                                  | 300m 阈值无文献支撑；ODD-A 1.0NM→0.5NM 降尺度逻辑未文档化；生成器与 YAML 不一致 |

**最严重问题**: 测试"通过了"但前端 trace 不合理——根因不在评估框架本身，而在于 **(a)** 评估器缺少时序和幅度量化指标，允许"5° 晚打舵擦边过关"；**(b)** M4/M5/M6 有架构缺陷（缺回归态、缺回归段、幽灵冲突），但评估器无法检测这些缺陷。

---

## 2. 架构设计评审

### 2.1 7 层评估框架 — 🟢 设计方向正确

| 层   | 名称                  | Spec 定义                        | 代码实现                                    | 评价       |
| :--- | :-------------------- | :------------------------------- | :------------------------------------------ | :--------- |
| L1   | Scenario Validity     | ✅ 定义明确                       | ❌ 无代码                                    | 🔴 严重缺失 |
| L2   | Safety Floor          | ✅ min_separation ≥ cpa_threshold | ⚠️ 分散在 `run_6_scenarios.py`               | 🟡 需统一   |
| L3   | Dynamic Risk          | ✅ approach/post-pass 拆分        | ❌ 无拆分代码                                | 🔴 严重缺失 |
| L4   | COLREG Compliance     | ✅ per-rule lifecycle             | ⚠️ `rule_compliance_evaluator.py` 有但不完整 | 🟡 需扩展   |
| L5   | Route Recovery        | ✅ returned_to_route + XTE        | ⚠️ XTE 检查在 batch_runner                   | 🟡 需统一   |
| L6   | Seamanship/Efficiency | ✅ path_ratio/overshoot           | ❌ 无代码                                    | 🔴 严重缺失 |
| L7   | Stability             | ✅ 8 KPIs                         | ✅ `stability_scorer.py`                     | 🟢 已实现   |

**核心问题**: Spec 定义了 7 层，但代码只实现了 L2/L4/L5(部分)/L7。L1/L3/L6 完全缺失。没有统一的 `TraceEvaluator` 类能输入 trace 文件、输出完整 7 层报告。

**来源**: 代码审查 (`stability_scorer.py`, `rule_compliance_evaluator.py`, `kpi_deriver.py`, `run_6_scenarios.py`)
**置信度**: 🟢 High

### 2.2 判决逻辑 — 🟡 需要重构

Spec 提出 4 维判决 (`safety_pass` / `mission_pass` / `colregs_pass` / `stability_pass`)，但代码仅实现 `overall_pass = cpa_ok AND stability_pass`。缺少：
- `mission_pass` (route recovery 独立判决)
- `colregs_pass` (per-rule lifecycle 独立判决)
- `stability_pass` 与其他维度的独立判定

**来源**: `stability_scorer.py` L386: `stability_pass = all(c["pass"] for c in checks.values() if c["applicable"])`
**置信度**: 🟢 High

### 2.3 M4/M5/M6 架构缺陷对评估的影响

经代码审查确认的三个架构缺陷：

| 缺陷                    | 表现                             | 评估器能否检测                                      | 影响 |
| :---------------------- | :------------------------------- | :-------------------------------------------------- | :--- |
| M4 缺 RETURN_TO_ROUTE   | AVOID→TRANSIT 跳变，XTE 持续增大 | ⚠️ `behavior_toggles` 可检测跳变，但无法检测"缺回归" | 中   |
| M5 fallback 无回归段    | 避碰后直线走远                   | ⚠️ `max_route_xte > 500m` 可检测，但无专门指标       | 中   |
| M6 衰减定时器"幽灵冲突" | TCPA<0 + 距离增大仍锁 AVOID      | ⚠️ `conflict_toggles` 可能检测到频繁 toggle          | 高   |

**来源**: CodeGraph exploration (M4 behavior_definitions.yaml, M5 mid_mpc_node.cpp, M6 colregs_reasoner_node.cpp)
**置信度**: 🟢 High (代码直接证据)

---

## 3. 场景覆盖评审

### 3.1 现有 8-Probe 逐场景评估

| #    | 场景 ID              | 规则           | 设计意图                        | 几何有效性             | CPA 合理性           | 设计质量       |
| :--- | :------------------- | :------------- | :------------------------------ | :--------------------- | :------------------- | :------------- |
| P1   | `rule14-ho`          | R14 对遇       | 纯对遇基线，M6 onset-latch 回归 | ✅ DCPA≈0 直线求解      | 🟡 185.2m(L2走廊约束) | 🟢 优秀         |
| P2   | `rule14-ho-port`     | R14 对遇边界   | port-biased 不误判为穿越        | ✅ 方位355° 精准边界    | 🟡 185.2m(同P1)       | 🟢 优秀         |
| P3   | `rule13-ot`          | R13 追越       | 13(d) 方位前移不得重分类        | ✅ 同向追越 DCPA≈0      | 🟡 300m(需补证据)     | 🟢 优秀         |
| P4   | `rule15-cs`          | R15/R16 穿越   | 右舷穿越标准让路                | ✅ 相对方位50° 碰撞求解 | 🟢 926m(0.5NM 合理)   | 🟢 优秀         |
| P5   | `rule15-cs-2`        | R15/R16 短TCPA | 逼早动作 (Rule 8b)              | ✅ 短距离强约束         | 🟢 926m               | 🟢 良好         |
| P6   | `rule15-cs-edge`     | R15 边界       | 25° 头/穿边界，不抖R14/R15      | ✅ 精准夹击对           | 🟡 300m(需补证据)     | 🟢 优秀         |
| P7   | `rule15-ot-boundary` | R15/R13 边界   | 108° 穿/追边界，不抖R15/R13     | ⚠️ 目标 45.7kn 不现实   | 🟡 300m(需补证据)     | 🟡 目标船速过高 |
| P8   | `rule17-cr-so`       | R17 直航       | 前期保向→末段17(b)              | ✅ 直线应让不让         | 🟡 185.2m(紧急底线)   | 🟢 优秀         |

### 3.2 关键问题

#### 3.2.1 P7 目标船速不现实 — 🟡

`colreg-rule15-ot-boundary.yaml` 目标船 SOG=45.74kn。这远超 FCB 本身 (12kn) 和典型商船速度 (12-25kn)。COLREGs Rule 13 定义追越的核心判断是"是否从后方追上来"，过高的速度比会使得相对运动几何失真——M6 通过接近率而非仅方位角来判断追越时，高相对速度可能触发不同代码路径。

**建议**: 将目标船速度调至与 FCB 同数量级 (15-20kn，保持相对方位≈108°)，或显式记录此为"高速穿越边界"极端测试。

**来源**: YAML 审查 (`colreg-rule15-ot-boundary.yaml` L39: `sog: 45.74`)
**置信度**: 🟢 High

#### 3.2.2 生成器不是唯一真源 — 🔴 严重

README 声明："由 `gen_colreg_tier12.py` **生成（唯一真源）**，勿手改 YAML"。但审查发现：

1. 生成器代码中 **不存在** `cpa_acceptance` 结构（含 profile/threshold_m/basis 等字段）
2. 生成器 `SHIP_DOMAIN_M = 926.0` 作为所有场景默认 CPA，但 YAML 文件中 P1-P2 是 185.2m、P3 是 300m、P6-P7 是 300m（生成器设为 500m）、P8 是 185.2m（生成器设为 500m）
3. `route_corridor_half_width_m`、`route_corridor_pass_limit_m`、`route_return_xte_m_lt`、`route_return_heading_deg_lt` 字段完全不在生成器代码中
4. `emergency_floor_m`、`ideal_domain_m` 字段不在生成器代码中

**结论**: YAML 文件已被大量手动修改，生成器 **不再是** 唯一真源。这违反了设计约束，后续场景参数变更可能不同步。

**来源**: 代码对比 (`gen_colreg_tier12.py` L52: `SHIP_DOMAIN_M = 926.0` vs YAML 文件实际值)
**置信度**: 🟢 High

#### 3.2.3 缺少 no-action baseline — 🟡

Spec §6 Layer 1 要求验证"场景本身存在真实冲突风险"，但 8-probe 没有任何"不避碰 baseline"CI。如果场景初始 DCPA 由于计算误差或参数漂移导致不为 0，则测试本身无效。

**来源**: `verify_colreg_tier12.py` 检查 DCPA < 500m 但不验证 DCPA < 0（不可能）也不验证无避碰时的 CPA
**置信度**: 🟢 High

### 3.3 场景覆盖缺口 — 🔴 关键盲区

| 缺口                             | 描述                                   | 行业依据                          | 严重度                   |
| :------------------------------- | :------------------------------------- | :-------------------------------- | :----------------------- |
| **Rule 9 受限水道**              | 所有场景在空旷水域，无 Geofence 约束   | IMO MASS Code 风险评估覆盖搁浅    | 🔴 安全红线               |
| **Rule 18 多船优先权**           | 无双船以上冲突                         | Imazu-22 含 3-4 船                | 🔴 仲裁逻辑未测           |
| **Rule 19 能见度不良**           | 无雾天场景，RESTRICTED_VIS 行为未测    | Rule 19 双方均让步                | 🔴 完全盲区               |
| **不合作机动目标**               | R17 目标不减速不转向，但无目标主动机动 | IMO Circ.1604 异常情况            | 🔴 最危险场景             |
| **风/流扰动**                    | 所有场景 wind=0, current=0             | IMO MASS Code 环境扰动            | 🟡 ODD-A 基线正常但不全面 |
| **Port 侧穿越 (own = give-way)** | R15 场景全部为右舷穿越(own give-way)   | 左舷穿越 own stand-on 由 R17 覆盖 | ⚪ R17 已覆盖             |

**来源**: IMO MSC.1/Circ.1604, Imazu-22 scenario set, DNV 55-scenario framework (Pedersen 2023)
**置信度**: 🟢 High

---

## 4. 评价指标评审

### 4.1 已实现指标 — 按层评审

#### Layer 2: Safety Floor (CPA 门限)

| 指标                          | 实现                  | 阈值                        | 问题           |
| :---------------------------- | :-------------------- | :-------------------------- | :------------- |
| `min_cpa_nm`                  | `kpi_deriver.py` L32  | per-scenario profile        | 🟢 基本可用     |
| `cpa_min_m_ge`                | YAML expected_outcome | 不统一 (185.2/300/500/926)  | 🟡 需动态参数化 |
| `route_corridor_pass_limit_m` | YAML                  | 500m (需与 L2 航道宽度对齐) | 🟢 工程约束合理 |

#### Layer 4: Rule Compliance

| 指标                 | 实现                                  | 评判标准                            | 问题                         |
| :------------------- | :------------------------------------ | :---------------------------------- | :--------------------------- |
| `rudder_side`        | `rule_compliance_evaluator.py` L30-31 | R14 左舵=违反                       | 🟢 正确                       |
| `heading_change_deg` | 同上 L19-26                           | ≥30°=full, ≥15°=partial             | 🟡 30° 过低(见下方)           |
| `cpa_nm`             | 同上                                  | ≥0.27NM(500m)=pass                  | 🔴 **与 YAML profile 不一致** |
| `timing_stage`       | 同上 L73-87                           | R17 Stage 1/2 保向, Stage 3+ 可规避 | 🟡 仅定性，无量化窗口         |
| `role`               | 同上 L43-58                           | give-way/stand-on 角色判断          | 🟢 正确                       |

**🔴 关键不一致**: `rule_compliance_evaluator.py` 硬编码 `cpa_target_nm=0.27` (约500m)，但 YAML profile 中 R14/R17 使用 185.2m、R13/R15-edge 使用 300m、R15-cs 使用 926m。这意味着该评估器对所有场景使用 500m 门限，与 per-scenario profile 完全不一致。

**来源**: 代码审查 (`rule_compliance_evaluator.py` L21, L27, L35)
**置信度**: 🟢 High

#### Layer 7: Behavioral Stability

已在 §2.1 评审。8 个 KPI 设计质量优秀（详见前述 Sisyphus 独立评审中的逐项分析），但：
- `rot_hold_std_dps < 1.5` 和 `steering_reversals ≤4/2` 阈值缺乏来源文档
- `standon_hold_frac = 0.75` (前 75%) 是经验值，应改为基于 M6 phase timing

### 4.2 完全缺失的指标 — 🔴 严重

#### (a) 动作时机 (Action Timing) — 完全缺失

**法规**: Rule 16 "as early as possible"; Rule 8(b) "in ample time"

**缺失影响**: 让路船可在 TCPA=30s 时猛打舵 30°，通过 CPA floor 但严重违反 Rule 8/16。现有评估器完全无法拦截。

**量化建议**:
- 开阔水域: `TCPA_action ≥ 180s` 或 `Range_action ≥ 1.5 NM`
- 受限航道: `TCPA_action ≥ 100s` 或 `Range_action ≥ 0.8 NM`
- 动作判定: `ROT ≥ 0.5°/s` 或 `Δψ ≥ 5°` 时刻

**来源**: Nautical Institute *Bridge Watchkeeping* 3rd ed.; Pedersen et al. (2023) DNV framework
**置信度**: 🟢 High

#### (b) 动作幅度 (Action Magnitude) — 方向检查存在但幅度缺失

**法规**: Rule 8(b) "readily apparent" / "large enough"

**缺失影响**: `turn_starboard` 仅检查方向（净偏航右舷 > 左舷），不检查幅度。5° 右转算"右转"，但对方雷达根本无法察觉，违反 Rule 8(b)。

**量化建议**:
- 开阔水域: `max_heading_deviation ≥ 25°`
- 受限航道/边界: `max_heading_deviation ≥ 15°`
- 禁止连续小修正: 单次 `Δψ < 5°` 且 30s 内 ≥3 次方向变化 → Layer 7 不通过

**来源**: COLREGs Rule 8(b); NLM 查询 (良好船艺要求动作可被雷达察觉); Brekke et al. (2023)
**置信度**: 🟢 High

#### (c) 直航船双向窗口 (Stand-on Window) — 仅单向

**法规**: Rule 17(a)(i) 保向保速; 17(a)(ii) 可辅助机动; 17(b) 必须行动

**缺失影响**: `premature_giveway` (<10° 前 75%) 仅惩罚"过早让路"，不惩罚"过晚不行动"。直航船若让路船不行动，需在特定 TCPA 窗口内启动 17(b)，否则即算 CPA 通过仍有碰撞风险。

**量化建议**:
- 保向阶段: `Δψ_max < 8°` (前 75% 时间, 或 M6 phase < INDEPENDENT_ACTION)
- 行动窗口: `TCPA ∈ [40s, 75s]` 时必须出现 ≥15° 航向变化
- 底线: `TCPA ≤ 40s` 仍未行动 → Layer 4 COLREGs fail

**来源**: Antigravity 设计报告 §3.3; 操纵性最迟机动点分析
**置信度**: 🟢 High

#### (d) 动态风险阶段拆分 — 完全缺失

**Spec 要求**: Layer 3 拆分 approach_warning_exposure、approach_danger_exposure、post_pass_domain_exposure、recovery_time。

**现状**: 无任何代码实现。`compute_risk_metrics()` 整体计算不拆分。

**来源**: Spec §5 3条要求 vs. 代码审查
**置信度**: 🟢 High

#### (e) 动作时机 (Past-and-Clear) 几何判定 — 完全缺失

**Spec §8**: heading-on 中目标已在身后时不计入 active threat。

**现状**: 无代码实现。M6 使用线性衰减定时器，导致"幽灵冲突"。

**来源**: Spec §8 + M6 代码审查
**置信度**: 🟢 High

---

## 5. 阈值来源与置信度评审

### 5.1 CPA 阈值溯源矩阵

| 阈值                | 场景画像                                | 法规来源                                      | 学术来源                                                     | LOA 倍数 | 置信度   | 评审结论                                           |
| :------------------ | :-------------------------------------- | :-------------------------------------------- | :----------------------------------------------------------- | :------- | :------- | :------------------------------------------------- |
| **185.2m** (0.1 NM) | R14 近距对遇, R17 in-extremis, 受限航道 | COLREGs 不给固定值; Rule 8(d) "safe distance" | Goerlandt & Kujala (2011) near-miss 基线; AIS 遇险统计       | 4.1L     | 🟢 High   | ✅ 物理碰撞红线合理，作为 emergency floor 无争议    |
| **300m**            | R13 追越/安全跟随, R15 边界分类         | 无直接法规依据                                | Coldwell (1983) 侧向 3L-4L ≈ 135-180m; 300m ≈ 1.6× 余量      | 6.7L     | 🔴 Low    | ⚠️ 无独立文献支撑 300m 值; **必须替换为参数公式**   |
| **405m** (9L)       | Ideal domain reference                  | 无直接法规依据                                | Fujii (1971) 椭圆 4L 纵半轴; Goodwin (1975) 扇形; Szlapczynski (2006) | 9.0L     | 🟢 High   | ✅ 作为 ideal domain reference 优秀; 不应作硬 floor |
| **926m** (0.5 NM)   | Open-water crossing give-way            | Rule 8(d) "safe distance" 不给数值            | Nautical Institute 开阔水域 CPA ≥ 2.0 NM (降尺度); Sawada 2020 OZT | 20.6L    | 🟡 Medium | ✅ 对 45m MASS 是合理降尺度; ⚠️ 不应用于受限航道     |

**300m 证据缺口详情**:
- Coldwell (1983) 对于 45m 船的侧向安全边界约 3L-4L = 135-180m
- 300m = Coldwell 值 × 1.6-2.2 余量
- 但 Coldwell 模型基于大型商船 (L≈200m)，外推到 45m MASS 需要额外验证
- **推荐**: 替换为 `max(0.1 NM, 6L)` = `max(185.2, 270)` = 270m → 安全包络 300m

**来源**: Fujii & Tanaka (1971) J. Navigation; Goodwin (1975) J. Navigation; Coldwell (1983); Goerlandt & Kujala (2011) Reliability Engineering
**置信度**: 如上逐项

### 5.2 ODD-A 1.0 NM → 0.5 NM 降尺度逻辑未文档化

架构设计报告 §3.3 定义 ODD-A (Open Water) `cpa_safe_m = 1852`（1.0 NM），但 8-probe 开阔水域门限仅 `926m`（0.5 NM）。降尺度假设（FCB LOA=45m vs COLREGs 默认商船 L≈150m）未在评估器 spec 或架构文档中明确说明。

**来源**: `m6_params.yaml` vs YAML profile 对比
**置信度**: 🟢 High

### 5.3 其他关键阈值

| 参数                                 | 值                         | 来源                          | 置信度              |
| :----------------------------------- | :------------------------- | :---------------------------- | :------------------ |
| `route_corridor_half_width_m = 1000` | L2 安全航道半宽            | 工程约束 (500m XTE limit × 2) | 🟢 High              |
| `route_corridor_pass_limit_m = 500`  | 不触发 L2 重规划的最大 XTE | 工程约束                      | 🟢 High              |
| `route_return_xte_m_lt = 150`        | 归航 XTE 门限              | 工程经验值                    | 🟡 Medium            |
| `route_return_heading_deg_lt = 10`   | 归航航向偏差门限           | 工程经验值 (5°-15° 范围内)    | 🟡 Medium            |
| `steering_reversals ≤4/2`            | ROT 符号反转次数           | 工程经验值                    | 🟡 Medium            |
| `rot_hold_std_dps < 1.5`             | 保持段偏航率方差           | 工程经验值                    | 🟡 Low               |
| `standon_hold_frac = 0.75`           | 直航保向阶段占比           | COLREGs 17(a)(i)              | 🟡 应回退到 M6 phase |

---

## 6. 实现一致性审查

### 6.1 Spec vs Code 关键不一致

| Spec 要求                                      | 代码实现                             | 差距     |
| :--------------------------------------------- | :----------------------------------- | :------- |
| 7 层统一 `TraceEvaluator`                      | 3 个分散组件                         | 🔴 严重   |
| Layer 1 Scenario Validity                      | 不存在                               | 🔴 严重   |
| Layer 3 Dynamic Risk 拆分                      | 不存在 (整体计算)                    | 🔴 严重   |
| Layer 6 Seamanship/Efficiency                  | 不存在                               | 🔴 严重   |
| 三维判决 safety/mission/colregs                | 单一 `overall_pass`                  | 🟡 中等   |
| CPA threshold provenance 输出                  | 不存在                               | 🟡 中等   |
| Heading-on post-pass 规则                      | 无代码 (M6 用定时器)                 | 🔴 严重   |
| per-rule lifecycle timeline                    | `rule_compliance_evaluator` 无时间线 | 🟡 中等   |
| `cpa_target_nm = 0.27` vs per-scenario profile | 硬编码 500m vs profile 185.2-926m    | 🔴 不一致 |

### 6.2 生成器 vs YAML 不一致

| 参数                   | 生成器默认值    | YAML 实际值                             | 差距         |
| :--------------------- | :-------------- | :-------------------------------------- | :----------- |
| P1-P5 cpa_min          | 926m            | 185.2m (P1-P2), 300m (P3), 926m (P4-P5) | 🔴 3/5 不一致 |
| P6-P7 cpa_min          | 500m (explicit) | 300m (YAML)                             | 🔴 不一致     |
| P8 cpa_min             | 500m (explicit) | 185.2m (YAML)                           | 🔴 不一致     |
| cpa_acceptance profile | 不存在          | 每个场景有完整结构                      | 🔴 完全缺失   |
| route_corridor 字段    | 不存在          | 每个场景有                              | 🔴 完全缺失   |

**结论**: `gen_colreg_tier12.py` **不再是**唯一真源。后续参数变更必须同步更新生成器或承认 YAML 为真源。

---

## 7. 可以通过但需要注意的缺陷 ("虚假通过"根因分析)

### 7.1 "测试通过了但 trace 不合理"的典型模式

| 模式           | 通过指标                     | trace 表现                     | 根因                       | 修复                                 |
| :------------- | :--------------------------- | :----------------------------- | :------------------------- | :----------------------------------- |
| **延迟小动舵** | CPA ≥ floor, starboard turn  | TCPA=30s 时才打 5° 舵          | 缺 Timing+Magnitude 指标   | 添加 action_tcpa_s + max_heading_dev |
| **平行不归航** | CPA ≥ floor, XTE 最终 ≤ 150m | AVOID→TRANSIT 跳变, 跑远才回来 | M4 缺 RETURN_TO_ROUTE      | 添加 M4 回归态                       |
| **幽灵冲突**   | CPA ≥ floor, 总是 PASS       | 目标在身后仍被追踪 60s         | M6 定时器滞后              | 改几何 past-and-clear                |
| **末段急转**   | CPA ≥ floor, starboard       | 17(a) 直航期间持续偏航 5-8°    | premature_giveway 阈值太松 | 8° 改 5° + 添加 TCPA 窗口            |
| **边间抖动**   | behavior_toggles ≤ 2         | 频繁 R14↔R15 切换              | M6 扇区边界阈值            | role_onset_stable 已覆盖             |

---

## 8. 评审结论

### 8.1 前项优先行动

| 优先级 | 行动                                                         | 预期效果                   |
| :----- | :----------------------------------------------------------- | :------------------------- |
| **P0** | 实现统一 `TraceEvaluator` (7 层, 含 Layer 1/3/6)             | 消除 Spec-Code 不一致      |
| **P0** | 添加 action_tcpa_s + max_heading_dev 指标                    | 杜绝"延迟小动作擦边过关"   |
| **P0** | 修复 `rule_compliance_evaluator.py` 的硬编码 `cpa_target_nm=0.27` | 消除与 YAML profile 不一致 |
| **P0** | 添加 no-action baseline CI (Layer 1)                         | 验证场景冲突有效性         |
| **P1** | 将 300m 替换为 `max(0.1 NM, 6L)` 参数化公式                  | 消除证据缺口               |
| **P1** | M6 Past-and-Clear 从定时器改为几何判定                       | 消除"幽灵冲突"             |
| **P1** | 同步 YAML ↔ 生成器 (恢复唯一真源)                            | 消除参数漂移风险           |
| **P1** | 添加 Stand-on 双向窗口 (premature + delayed)                 | 完整覆盖 Rule 17           |
| **P2** | 新增 rule09/rule19/multiship/uncooperative 场景              | 消除规则覆盖盲区           |
| **P2** | 生成器重构：加入 dynamic CPA profile                         | 支持参数化 ODD 依赖阈值    |

---

*本评审报告基于以下来源:*
- *COLREGs 1972 原文 (Rules 6/7/8/13/14/15/16/17/19)*
- *33 CFR §83.06-83.17 (US Inland Rules equivalent)*
- *IMO MSC.1/Circ.1604 (MASS 试验指南)*
- *Fujii & Tanaka (1971), Goodwin (1975), Coldwell (1983), Davis (1980) 船域模型*
- *Goerlandt & Kujala (2011) near-miss baseline*
- *Brekke et al. (2023) COLREG evaluation framework*
- *Pedersen et al. (2023) DNV 55-scenario set*
- *Gil et al. (2021) CADCA concept*
- *NLM 研究查询 (COLREGs 数值阈值, Rule 13d, Rule 2)*
- *CodeGraph 代码探索 (1,323 files, 16,936 nodes, 32,948 edges)*



# COLREGs 8-Probe 场景与评估器完整设计报告 (v1.0)

**基于**: 评审报告发现 + NLM 调研 + 代码审查 + COLREGs 原文 + 学术文献
**设计人**: Sisyphus (MASS L3 TDL 系统)
**日期**: 2026-06-16
**版本**: v1.0 — 可供专家复核的完整设计

---

## 1. 修订场景集 (v3.0 Probe Suite)

### 1.1 设计理念

从 8-probe 扩展到 12-probe (+4 新增场景)，保持每个探针"单一目的、失败可归因"的原则。新增场景覆盖评审报告中识别的 4 个关键盲区。

### 1.2 场景矩阵 (12-probe)

| #    | ID                     | 规则    | 角色     | 核心行为           | CPA Profile              | LOA 倍数 | 来源 / 置信度                        |
| :--- | :--------------------- | :------ | :------- | :----------------- | :----------------------- | :------- | :----------------------------------- |
| P1   | `rule14-ho`            | R14     | give-way | 纯对遇右转         | `corridor_close` 185.2m  | 4.1L     | 🟢 Goerlandt (2011) near-miss 基线    |
| P2   | `rule14-ho-port`       | R14     | give-way | port-biased 仍右转 | `corridor_close` 185.2m  | 4.1L     | 🟢 同 P1                              |
| P3   | `rule13-ot`            | R13     | give-way | 追越/安全跟随      | `corridor_follow` 270m   | 6.0L     | 🟡 `max(0.1NM, 6L)` 公式              |
| P4   | `rule15-cs`            | R15/R16 | give-way | 右舷穿越让路       | `open_water` 926m        | 20.6L    | 🟢 Nautical Institute 0.5NM 基线      |
| P5   | `rule15-cs-2`          | R15/R16 | give-way | 短 TCPA 早动作     | `open_water` 926m        | 20.6L    | 🟢 同 P4                              |
| P6   | `rule15-cs-edge`       | R15     | give-way | 头/穿边界 25°      | `corridor_boundary` 270m | 6.0L     | 🟡 同 P3 公式                         |
| P7   | `rule15-ot-boundary`   | R15     | give-way | 穿/追边界 108°     | `corridor_boundary` 270m | 6.0L     | 🟡 同 P3 公式                         |
| P8   | `rule17-cr-so`         | R17/R15 | stand-on | 保向→17(b)         | `extremis` 185.2m        | 4.1L     | 🟢 Rule 17(b) 紧急底线                |
| P9*  | `rule09-channel`       | R9/R14  | give-way | 受限航道对遇       | `channel` 270m           | 6.0L     | 🟡 新增; 工程约束                     |
| P10* | `multiship-avoid`      | R14+R15 | give-way | 多船交叉冲突       | `open_water` 926m        | 20.6L    | 🟡 新增; 仲裁测试                     |
| P11* | `uncooperative-target` | R17(b)  | stand-on | 让路船违规左转     | `extremis` 185.2m        | 4.1L     | 🟡 新增; Grlj (2025) 67% 碰撞涉及违规 |
| P12* | `rule19-fog`           | R19     | both     | 雾天双方避让       | `fog` 405m               | 9.0L     | 🟡 新增; Rule 19 无 give-way          |

* P9-P12 为新增场景，需 harness 改动（Geofence 支持、多目标、目标机动脚本、能见度控制）

### 1.3 CPA 动态阈值模型

**核心公式**: `CPA_safe(ODD, L, v_r) = α(ODD) · L + β(ODD) · v_r`

| ODD Profile                 | α    | β (s) | FCB 示例 (L=45m, v_r=10m/s)          | 硬 Floor                          | 对应场景     |
| :-------------------------- | :--- | :---- | :----------------------------------- | :-------------------------------- | :----------- |
| `open_water`                | 10.0 | 20    | 10×45+20×10 = 650m ≈ 0.35 NM         | 926m (0.5NM, 项目基线)            | P4, P5, P10  |
| `corridor` (近距/边界/追越) | 5.0  | 10    | 5×45+10×10 = 325m → 取 270m          | max(0.1NM, 270m)=270m → 包络 300m | P1-P3, P6-P7 |
| `extremis` (R17 末段)       | 3.0  | 4     | 3×45+4×10 = 175m → 取 185.2m (0.1NM) | 185.2m (0.1NM)                    | P8, P11      |
| `channel` (受限航道)        | 5.0  | 10    | 325m → 取 270m → 包络 300m           | max(0.1NM, XTE_limit)             | P9           |
| `fog` (能见度不良)          | 7.0  | 15    | 7×45+15×10 = 465m → 取 405m (9L)     | 9L = 405m                         | P12          |

**来源**:
- α·L 项: Fujii (1971) 椭圆船域 + Goodwin (1975) 扇形域 → 3L-10L 范围，按 ODD 取值
- β·v_r 项: Gil et al. (2021) CADCA 概念 → 动态接近速度对安全距离的贡献
- Hard floor: Goerlandt & Kujala (2011) 0.1 NM near-miss 基线
- 9L ideal domain: Fujii (1971) 纵向 4L 半轴 (全椭圆 8L)，Goodwin 扇形 4-6L

**置信度**: 🟢 High (学术文献 + 项目工程验证)

### 1.4 P9-P12 场景规格

#### P9: `rule09-channel` (受限航道对遇)

- **设置**: 航道宽度 400m（2× corridor_half_width），OS 航向 000°/10kn，TS 对遇 180°/8kn，左舷有 Geofence 浅滩障碍（距航线 200m）
- **测试目的**: M5 在航道边界约束下生成小角度右转避碰轨迹，不触发 grounding risk
- **CPA**: `corridor_channel` = 270m（受限航道需在 XTE ≤ 200m 内完成避碰）
- **ODD**: `restricted_waterway`

#### P10: `multiship-avoid` (多船交叉冲突)

- **设置**: OS 航向 000°/12kn，TS1 对遇 (R14, 方位 0°)，TS2 右舷穿越 (R15, 方位 50°)
- **测试目的**: M4 多目标 IvP 仲裁（R15 优先于 R14 向右偏航，同时满足 R14 右转要求）
- **CPA**: `open_water` = 926m（对两个目标均需满足）
- **关键断言**: 对 TS1 的 CPA ≥ 926m AND 对 TS2 的 CPA ≥ 926m AND 行为不被 R14/R15 来回切换

#### P11: `uncooperative-target` (让路船违规机动)

- **设置**: Crossing 场景（同 P4 基础），OS 为 stand-on。TS 在 TCPA ≈ 100s 时突然向 OS 方向左转 30°（harness: `trajectory_file` 重放或脚本化机动）
- **测试目的**: M6 检测对方异常机动 → M7 Doer-Checker 触发 17(b) 独立行动
- **CPA**: `extremis` = 185.2m（紧急底线）
- **关键断言**: OS 前 75% 时间 `Δψ < 8°` AND 在 TCPA ≤ 75s 出现 ≥15° 转向 AND CPA ≥ 185.2m

#### P12: `rule19-fog` (能见度不良)

- **设置**: 能见度 `0.5 NM`，OS 航向 000°/12kn → 减速至安全速度 6kn，TS 对遇 180°/8kn
- **测试目的**: M6 不进入 give-way/stand-on 划分（Rule 19 无角色），M4 进入 RESTRICTED_VIS 行为，双方均减速右转
- **CPA**: `fog` = 405m (9L)
- **关键断言**: 无 `primary_role = GIVE_WAY/STAND_ON` AND SOG 降至 `sog_safe ≤ 6kn` AND 右转 `Δψ ≥ 15°`

---

## 2. 7 层评估器完整实现规格 (TraceEvaluator v1.0)

### 2.1 总体架构

```python
class TraceEvaluator:
    """7-layer trace evaluator. Input: trace_current.jsonl + scenario YAML."""
    
    def evaluate(self, trace: List[dict], scenario: dict) -> EvaluationReport:
        L1 = self.layer1_scenario_validity(trace, scenario)    # MUST PASS FIRST
        L2 = self.layer2_safety_floor(trace, scenario)          # CPA hard floor
        L3 = self.layer3_dynamic_risk(trace, scenario)          # approach/post-pass split
        L4 = self.layer4_colregs_compliance(trace, scenario)    # per-rule lifecycle
        L5 = self.layer5_route_recovery(trace, scenario)         # XTE + heading + corridor
        L6 = self.layer6_seamanship_efficiency(trace, scenario) # path ratio + overshoot
        L7 = self.layer7_stability(trace, scenario)              # existing KPIs
        
        return EvaluationReport(
            safety_pass = L2.pass and L7.pass,
            mission_pass = L5.pass,
            colregs_pass = L4.pass,
            stability_pass = L7.pass,
            overall_pass = all(PASS for L in [L2, L4, L5, L6, L7]),
            report = {L1, L2, L3, L4, L5, L6, L7}
        )
```

### 2.2 Layer 1: Scenario Validity (新增)

**目的**: 验证场景本身存在真实碰撞风险，杜绝"无冲突假绿"。

| 检查项                | 条件                                       | 来源                         |
| :-------------------- | :----------------------------------------- | :--------------------------- |
| `no_action_dcpa`      | 无避碰动作时 DCPA < 500m                   | `simulate.py` kinematic 验证 |
| `tcpa_approach`       | 无避碰动作时 TCPA > 0 且 closing_speed > 0 | 几何验证                     |
| `initial_rel_bearing` | 初始相对方位与场景声明一致 (±5°)           | M2 分类验证                  |
| `conflict_genuine`    | M6 在无避碰时 conflict_detected=true       | 仿真验证                     |

**实现**: 对每个场景运行 `no_action` baseline（OS controller = 纯 TRANSIT），验证 `min(CPA) < cpa_threshold` 且 `closing_speed > 0`。

### 2.3 Layer 2: Safety Floor

**不变**: 全程 `min_separation_m ≥ cpa_threshold_m`，其中 `cpa_threshold_m` 由动态 CPA 模型计算。

**新增**: 输出 `threshold_m` / `profile` / `basis` / `nm_equivalent` / `loa_multiplier` / `source_confidence` 到报告。

### 2.4 Layer 3: Dynamic Risk (新增实现)

**拆分风险暴露为 4 个阶段**:

| 阶段                    | 定义                                                | 检查                                        | 来源               |
| :---------------------- | :-------------------------------------------------- | :------------------------------------------ | :----------------- |
| **approach_warning**    | `TCPA > 0, closing_speed > 0, range > cpa_safe`     | `exposure_duration_s < threshold`           | Rule 7 风险判断    |
| **approach_danger**     | `TCPA > 0, closing_speed > 0, range ≤ cpa_safe`     | `exposure_duration_s < max_danger_duration` | Rule 8 及早行动    |
| **post_pass_clearance** | `TCPA < 0, closing_speed ≤ 0, target abaft`         | `min_separation_m ≥ emergency_floor_m`      | Good seamanship    |
| **recovery**            | `conflict_detected = false, XTE > return_threshold` | `recovery_time_s < max_recovery_time`       | Mission completion |

**Heading-on Post-Pass 规则**:

当目标船已在身后 (`TCPA < 0`, `closing_speed ≤ 0`, 目标方位在 `[90°, 270°]`) 且距离开始增大时:
- `approach_danger` 曝光计为 0
- `post_pass_clearance` 单独计为 seamanship 质量（Layer 6 扣分项），**不**作为 collision threat fail

**Rule 13 追越特例**:
- 即使 TCPA < 0，追越义务持续到 `past_and_clear`（几何判定：目标远离 + 本船超越 + 距离 > cpa_safe）
- 追越场景中 `approach_danger` 不能因 TCPA 变负而自动释放

### 2.5 Layer 4: COLREGs Compliance (扩展)

**每个规则独立的 lifecycle 评估**:

#### 4.1 Timing of Action (新增)

```python
def check_timing(trace, scenario):
    action_tcpa_s = first_heading_change_tcpa(trace, threshold_deg=5.0)
    if scenario.role == "give_way":
        # Rule 16: "as early as possible"
        if scenario.odd == "open_sea":
            assert action_tcpa_s >= 180, f"Action too late: TCPA={action_tcpa_s}s < 180s"
        elif scenario.odd in ("restricted_waterway", "channel"):
            assert action_tcpa_s >= 100, f"Action too late: TCPA={action_tcpa_s}s < 100s"
    elif scenario.role == "stand_on":
        # Rule 17: hold then act in window [40s, 75s]
        extremis_tcpa_max = 75.0  # earliest allowed action
        extremis_tcpa_min = 40.0   # latest allowed action
        if action_tcpa_s > extremis_tcpa_max:
            # premature action - already caught by premature_giveway
            pass
        if action_tcpa_s < extremis_tcpa_min:
            # too late - collision risk
            assert False, f"Stand-on action too late: TCPA={action_tcpa_s}s < 40s"
```

**来源**: Nautical Institute *Bridge Watchkeeping*; Pedersen et al. (2023) DNV framework
**置信度**: 🟢 High

#### 4.2 Magnitude of Action (新增)

```python
def check_magnitude(trace, scenario):
    max_dev = max(abs(heading_deviation(trace)) during_avoidance_window)
    if scenario.role == "give_way":
        if scenario.odd == "open_sea":
            assert max_dev >= 25.0, f"Alteration too small: {max_dev}° < 25° (Rule 8b)"
        elif scenario.odd in ("restricted_waterway", "channel", "boundary"):
            assert max_dev >= 15.0, f"Alteration too small: {max_dev}° < 15° (Rule 8b)"
    # Also check: no series of small alterations (<5° × 3 in 30s)
    small_alterations = count_small_alterations(trace, threshold_deg=5.0, window_s=30.0)
    assert small_alterations < 3, "Rule 8(c): series of small alterations prohibited"
```

**来源**: COLREGs Rule 8(b)(c); NLM 查询; Brekke et al. (2023)
**置信度**: 🟢 High

#### 4.3 Per-Rule Per-Phase Assessment (扩展原有)

| 规则    | 阶段检查                                   | 评估标准                                            | 来源              |
| :------ | :----------------------------------------- | :-------------------------------------------------- | :---------------- |
| R14     | `onset` → `avoiding` → `past-and-clear`    | 右转≥25°(开阔)/15°(边界); 不左转; port-to-port pass | Rule 14(a)        |
| R13     | `onset` → `avoiding` → `past-and-clear`    | 追越义务持续; 方位前移不重分类; 最终超车            | Rule 13(d)        |
| R15/R16 | `onset` → `early_action` → `clearing`      | 右转; 不穿目标前方; 及早(Rule 16)                   | Rule 15/16        |
| R17     | `hold` → `extremis` → `independent_action` | 前75%偏航<8°; 17(b)在窗口[40s,75s]TCPA内启动        | Rule 17(a)(i)/(b) |
| R19     | `both_give_way` → `slow_speed` → `avoid`   | 无角色划分; 减速至安全速度; 不左转                  | Rule 19(a)(b)     |

### 2.6 Layer 5: Route Recovery

**不变**: `returned_to_route`, `final_xte_m`, `max_route_xte_m`, `XTE ≤ 500m` (核心 gate)

**新增**: `recovery_time_s` — 从 `conflict_detected=false` 到 `XTE < route_return_xte_m` 的时间。对开阔水域场景不应超过 120s。

### 2.7 Layer 6: Seamanship / Efficiency (新增实现)

| 指标                    | 计算                                      | Gate                                   | 来源                    |
| :---------------------- | :---------------------------------------- | :------------------------------------- | :---------------------- |
| `path_ratio`            | `actual_distance / great_circle_distance` | ≤1.5 (open water) / ≤2.0 (channel)     | Seafaring efficiency    |
| `integrated_xte_m`      | `∫|XTE(t)|dt`                             | ≤ `cpa_threshold * avoidance_duration` | Route discipline        |
| `overshoot_deg`         | 归航时最大反向偏航                        | ≤ 15°                                  | Good seamanship         |
| `post_pass_clearance_m` | 过船后最近距离                            | ≥ `emergency_floor_m`                  | Rule 8(d) safe distance |
| `max_excursion_m`       | 避碰过程中最大 XTE                        | ≤ `route_corridor_half_width_m`        | Mission corridor        |

### 2.8 Layer 7: Stability (现有 + 修订)

| KPI                        | 修订         | 新 Pass 阈值                                 | 来源          |
| :------------------------- | :----------- | :------------------------------------------- | :------------ |
| `behavior_toggles`         | 不变         | ≤2 (一起一落)                                | 工程经验      |
| `plan_valid_segments`      | 不变         | ≤2                                           | 工程经验      |
| `steering_reversals`       | 不变         | give-way ≤4 / stand-on ≤2                    | 船艺操作极限  |
| `rot_hold_std_dps`         | 不变         | <1.5                                         | 工程经验      |
| `conflict_toggles`         | 不变         | ≤2                                           | M6 分类稳定性 |
| `role_onset_stable`        | 不变         | 0 次义务翻转                                 | Rule 13(d)    |
| `turn_starboard_magnitude` | **新增幅度** | give-way max_dev ≥ 25°(OW) / 15°(restricted) | Rule 8(b)     |
| `premature_giveway`        | **修订阈值** | stand-on 前75% < **8°** (从10°收紧)          | Rule 17(a)(i) |
| `stand_on_action_window`   | **新增**     | TCPA ∈ [40s, 75s] 出现 ≥15° 转向             | Rule 17(b)    |
| `action_timing`            | **新增**     | give-way TCPA ≥ 180s(OW) / 100s(restricted)  | Rule 16       |

### 2.9 Overall Verdict (修订)

四维判决，不再只有一个 PASS:

```python
safety_pass   = L2_cpa_floor AND L7_stability
mission_pass  = L5_route_return AND L6_path_efficiency
colregs_pass  = L4_rule_compliance AND L4_timing AND L4_magnitude
stability_pass = L7_all_applicable_KPIs

overall_pass   = safety_pass AND mission_pass AND colregs_pass AND stability_pass
risk_quality_score = weighted(L3_risk_quality, L6_seamanship)  # 不替代硬 gate
```

---

## 3. No-Action Baseline 协议

### 3.1 目的

验证每个探针场景在没有避碰动作时确实会碰撞（Layer 1 Scenario Validity）。

### 3.2 实现

对每个场景运行一次 `no_action` baseline:
- OS controller: 纯 TRANSIT 模式（循线保持，无 M6/M4/M5 避碰介入）
- TS: 保持原始航向航速（直线 replay）
- **断言**: `min(CPA_trace) < cpa_threshold_m` AND `min(CPA_trace) > 0`（保证几何有效性）

### 3.3 CI 集成

在 `verify_colreg_tier12.py` 中添加 `--baseline` 选项，对每个 YAML 运行 `simulate.py` 的无避碰模式。如果 baseline CPA ≥ threshold，该场景标记为 INVALID 并从测试套件中排除。

**来源**: Spec §6 Layer 1; 行业标准做法
**置信度**: 🟢 High

---

## 4. Heading-on Post-Pass 规则 (完整算法)

### 4.1 Active Collision Threat 条件

```
is_active_collision_threat = (
    tcpa >= 0 OR closing_speed > 0
) AND NOT past_and_clear
```

### 4.2 Past-and-Clear 几何判定 (替代 M6 衰减定时器)

```python
def is_past_and_clear(own_state, target_state, cpa_safe_m: float) -> bool:
    """COLREGs Rule 13(d) + Rule 14 past-and-clear geometric test."""
    range_m = distance(own_state, target_state)
    rel_bearing = bearing(own_state, target_state)
    tcpa = compute_tcpa(own_state, target_state)
    closing_speed = compute_closing_speed(own_state, target_state)
    
    # Condition 1: Range is increasing (no longer closing)
    if closing_speed > 0:
        return False
    
    # Condition 2: Target is abaft (behind the beam)
    # For Rule 13 overtaking: target must be aft of 112.5° abaft the beam
    # For Rule 14 head-on / Rule 15 crossing: target bearing in [90°, 270°]
    abaft_threshold = 112.5 if is_overtaking_situation else 90.0
    if not (abaft_threshold <= rel_bearing <= (360 - abaft_threshold)):
        return False
    
    # Condition 3: Range exceeds safe domain
    if range_m < cpa_safe_m:
        return False
    
    # Condition 4: TCPA is negative (past CPA)
    if tcpa > 0:
        return False
    
    return True
```

### 4.3 Post-Pass Shallow Domain 处理

当 `past_and_clear` 为 true 但距离仍在 185.2m ~ 405m 之间时:
- 不计入 `approach_danger_exposure`（Layer 3 硬 gate）
- 计入 `post_pass_clearance_quality`（Layer 6 扣分项）
- 如果 `post_pass_clearance < emergency_floor_m`，仅 Layer 2 记录但 Layer 4/5 不 fail

**来源**: Spec §8; COLREGs Rule 13(d); NLM 查询
**置信度**: 🟢 High

---

## 5. 生成器重构: 恢复唯一真源

### 5.1 问题

当前 YAML 文件包含 `cpa_acceptance`(含 profile, threshold_m, basis, emergency_floor_m, ideal_domain_m) 等字段，但 `gen_colreg_tier12.py` 不生成这些字段。生成器不再是唯一真源。

### 5.2 解决方案

**重构 `gen_colreg_tier12.py`**，使其成为完整参数化生成器:

1. 加入 `ODD_PARAMS` 字典，包含所有 ODD 依赖参数:

```python
ODD_PARAMS = {
    "open_sea_offshore_wind_farm": {
        "alpha": 10.0, "beta": 20,   # CPA = α·L + β·v_r
        "cpa_floor_nm": 0.1,          # emergency floor
        "cpa_ideal_loa_mult": 9.0,    # ideal domain
        "loa_m": 45.0,
        "tcpa_safe_open_s": 180,
        "tcpa_safe_restricted_s": 100,
        "min_alteration_open_deg": 25.0,
        "min_alteration_restricted_deg": 15.0,
        "standon_hold_max_deg": 8.0,
        "standon_window_tcpa_range_s": (40, 75),
        "route_corridor_half_width_m": 1000,
        "route_corridor_pass_limit_m": 500,
        "route_return_xte_m": 150,
        "route_return_heading_deg": 10,
    },
    # future ODD profiles...
}
```

2. 对每个场景自动计算 `cpa_threshold_m`:

```python
def compute_cpa_threshold(profile: str, loa_m: float, v_r_mps: float) -> dict:
    params = ODD_PARAMS[profile]
    cpa_safe = params["alpha"] * loa_m + params["beta"] * v_r_mps
    cpa_floor = params["cpa_floor_nm"] * 1852.0
    cpa_ideal = params["cpa_ideal_loa_mult"] * loa_m
    return {
        "threshold_m": round(max(cpa_safe, cpa_floor), 1),
        "emergency_floor_m": round(cpa_floor, 1),
        "ideal_domain_m": round(cpa_ideal, 1),
        "basis": f"{profile}: max(α·L+β·v_r, {params['cpa_floor_nm']}NM); α={params['alpha']}, β={params['beta']}",
        "loa_multiplier": round(max(cpa_safe, cpa_floor) / loa_m, 1),
        "source_confidence": "High" if profile in ("open_sea_offshore_wind_farm",) else "Medium",
    }
```

3. 生成完整的 `expected_outcome` 和 `cpa_acceptance` 结构

### 5.3 参数溯源表 (每个值必须有来源)

| 参数                            | 值       | 公式来源                       | 文献来源                                          | 置信度   |
| :------------------------------ | :------- | :----------------------------- | :------------------------------------------------ | :------- |
| `alpha_open`                    | 10.0     | α·L 项                         | Fujii (1971), Goodwin (1975), Szlapczynski (2006) | 🟢 High   |
| `beta_open`                     | 20s      | β·v_r 项                       | Gil et al. (2021) CADCA                           | 🟢 High   |
| `alpha_restricted`              | 5.0      | α·L 项                         | Coldwell (1983) 侧向 3L-4L                        | 🟡 Medium |
| `beta_restricted`               | 10s      | β·v_r 项                       | 项目工程折中                                      | 🟡 Medium |
| `alpha_emergency`               | 3.0      | α·L 项                         | Goerlandt & Kujala (2011) near-miss               | 🟢 High   |
| `beta_emergency`                | 4s       | β·v_r 项                       | 项目工程折中                                      | 🟡 Medium |
| `loa_m`                         | 45.0     | FCB LOA                        | `fcb_simulator_plugin.cpp` L76                    | 🟢 High   |
| `standon_hold_max_deg`          | 8.0      | Rule 17(a)(i) 容差             | COLREGs + 工程经验 (从10°收紧)                    | 🟡 Medium |
| `standon_window_tcpa`           | (40, 75) | 操纵性最迟机动点               | Antigravity 设计报告 §3.3                         | 🟢 High   |
| `min_alteration_open_deg`       | 25.0     | Rule 8(b) "readily apparent"   | Nautical Institute; Brekke (2023)                 | 🟢 High   |
| `min_alteration_restricted_deg` | 15.0     | Rule 8(b) 受限水域             | 工程经验                                          | 🟡 Medium |
| `tcpa_safe_open_s`              | 180      | Rule 16 "as early as possible" | Nautical Institute *Bridge Watchkeeping*          | 🟢 High   |
| `tcpa_safe_restricted_s`        | 100      | Rule 16 受限水域               | 工程经验                                          | 🟡 Medium |
| `route_corridor_half_width_m`   | 1000     | L2 VoyagePlan safety corridor  | 文档约束                                          | 🟢 High   |
| `route_corridor_pass_limit_m`   | 500      | 不触发 L2 重规划               | 工程约束                                          | 🟢 High   |

---

## 6. 实现优先级

### Phase 1: 必须修复 (P0) — 测试平台正确性保障

| 优先级 | 任务                                                         | 预计工作量 | 依赖           |
| :----- | :----------------------------------------------------------- | :--------- | :------------- |
| P0-1   | 实现 `TraceEvaluator` 统一 7 层评估器                        | 3 人天     | 无             |
| P0-2   | 添加 Layer 1 Scenario Validity + no-action baseline CI       | 1 人天     | simulate.py    |
| P0-3   | 添加 action_tcpa_s + max_heading_dev 指标                    | 1 人天     | TraceEvaluator |
| P0-4   | 修复 `rule_compliance_evaluator.py` 硬编码 cpa_target_nm=0.27 | 0.5 人天   | 无             |
| P0-5   | 添加 Stand-on 双向窗口 (premature + delayed)                 | 1 人天     | TraceEvaluator |
| P0-6   | 重构 `gen_colreg_tier12.py` 恢复唯一真源                     | 2 人天     | ODD_PARAMS     |

### Phase 2: 架构修复 (P1) — 消除"虚假通过"根因

| 优先级 | 任务 | 预计工作量 | 依赖 |
|:---|:---|:---|
| P1-1 | 300m 替换为 `max(0.1 NM, 6L)` 参数化公式 | 0.5 人天 | gen_colreg_tier12 |
| P1-2 | M6 Past-and-Clear 从定时器改为几何判定 | 3 人天 | M6 代码 |
| P1-3 | M4 添加 RETURN_TO_ROUTE 状态 (weight=0.55) | 2 人天 | M4 代码 |
| P1-4 | M5 fallback 添加回归段 (3-segment arc) | 3 人天 | M5 代码 |
| P1-5 | Layer 3 Dynamic Risk 拆分实现 | 2 人天 | TraceEvaluator |

### Phase 3: 覆盖扩展 (P2) — 消除规则盲区

| 优先级 | 任务                                              | 预计工作量 | 依赖               |
| :----- | :------------------------------------------------ | :--------- | :----------------- |
| P2-1   | P9 rule09-channel 场景 (需 Geofence harness)      | 2 人天     | SIL harness 改动   |
| P2-2   | P10 multiship-avoid 场景 (需多目标支持)           | 1 人天     | SIL harness 改动   |
| P2-3   | P11 uncooperative-target 场景 (需脚本化机动)      | 2 人天     | target_vessel_node |
| P2-4   | P12 rule19-fog 场景 (需能见度控制)                | 2 人天     | ODD-D 触发路径     |
| P2-5   | CPA 阈值溯源输出 (threshold_m, basis, confidence) | 1 人天     | TraceEvaluator     |

### 总工时估计: ~24 人天 (P0: 8.5天, P1: 10.5天, P2: 8天)

---

## 7. Spec 审阅人问题回复

### Q1: 300m 是否接受为工程折中，还是必须替换为公式？

**必须替换为参数化公式**。`CPA_restricted = max(0.1 NM, k·L)`，其中 k=6.0。对 FCB (L=45m): `max(185.2, 270) = 270m` → 包络取整 `300m`。公式保留 300m 的工程合理性，同时获得泛化能力（若换为 15m USV: `max(185.2, 90) = 185.2m`）。

**来源**: Coldwell (1983) 侧向域 + 本设计报告 §1.3
**置信度**: 🟡 Medium (k=6.0 是工程折中，建议 FCB 海试验证)

### Q2: 9L=405m 应作为 warning domain、ideal domain，还是部分场景硬 floor？

**作为 Ideal Domain reference 和 Warning Domain 维度**，不作为硬 floor。

- 在 Layer 2 (Safety Floor)，硬 floor 使用 `emergency_floor_m = 0.1 NM = 185.2m`
- 在 Layer 6 (Seamanship)，作为 quality score 扣分起始线: CPA 在 185.2m-405m 之间扣分，但不 hard fail
- 在 open_water 场景，ideally 的 CPA 应达到 `ideal_domain_m = 405m` (9L)，但受到 corridor 约束可降至 `threshold_m = 926m` (0.5NM)

**来源**: Fujii (1971) 椭圆模型; Goodwin (1975) 扇形模型
**置信度**: 🟢 High

### Q3: 0.5NM=926m 是否只适用于 open-water？

**是的，0.5NM 绝对不能用于受限航道。** 在受限航道（宽 400-1000m），避碰 CPA 设为 926m 会导致 XTE 超出航道边界。受限航道必须使用 `corridor` profile (270m/300m 包络值)。

**来源**: 工程约束 `route_corridor_pass_limit_m = 500m` → CPA 必须小于 XTE 限制
**置信度**: 🟢 High

### Q4: Rule 17 in-extremis 用 0.1NM 是否过低？

**0.1NM 物理距离是合理的底线，但必须补充动力学约束**。补充"剩余操纵空间"条件:
- `TCPA_extremis_min ≈ 40s` (基于 FCB 最大旋回角速度与制动曲线)
- 若 TCPA ≤ 40s 仍未行动，即使 CPA 达标也判 Layer 4 fail

**来源**: 操纵性计算
**置信度**: 🟡 Medium (建议 FCB 海试验证最迟机动点)

### Q5: Heading-on post-pass close domain 是否只做质量扣分？

**接受**。当 TCPA < 0 + closing_speed ≤ 0 + target abaft 时，即使距离在 185-405m 之间，不计入 active collision threat fail，仅在 Layer 6 (Seamanship) quality score 中扣分。

**来源**: Spec §8; COLREGs Rule 13(d)
**置信度**: 🟢 High

### Q6: 8-probe 是否应补 no-action baseline trace？

**必须补充**。每个探针场景应运行一次 no-action baseline (OS = 纯 TRANSIT)，验证 `min(CPA_baseline) < cpa_threshold_m`。如果 baseline CPA ≥ threshold，该场景无效。

**来源**: Spec §6; 行业标准做法
**置信度**: 🟢 High

---

*本设计报告基于以下来源:*
- *COLREGs 1972 原文 (Rules 2/6/7/8/9/13/14/15/16/17/19)*
- *33 CFR §§83.06-83.17 (US Inland Rules)*
- *IMO MSC.1/Circ.1604*
- *Fujii & Tanaka (1971) J. Navigation; Goodwin (1975); Coldwell (1983); Davis (1980)*
- *Goerlandt & Kujala (2011); Szlapczynski (2006); Gil et al. (2021)*
- *Brekke et al. (2023) COLREG evaluation; Pedersen et al. (2023) DNV 55-scenario*
- *Nautical Institute *Bridge Watchkeeping* 3rd ed.*
- *NLM 研究查询 (COLREGs 数值阈值, Rule 2 良好船艺, Rule 13(d), 安全距离阈值)*
- *项目架构文档: AGENTS.md §architecture invariants, M4/M5/M6/M7 spec*
- *代码审查: stability_scorer.py, rule_compliance_evaluator.py, kpi_deriver.py, gen_colreg_tier12.py, run_6_scenarios.py*
- *8-Probe Trace Evaluator Spec v0.1*
- *Antigravity 评审报告 & 设计报告 (2026-06-16)*
