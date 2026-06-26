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

| 维度 | 评级 | 核心发现 |
|:---|:---|:---|
| **架构设计** | 🟡 有条件通过 | 7 层评估框架方向正确，但 Spec-Code 不一致：3 个评估组件分散、无统一 TraceEvaluator；Layer 1/3/6 无实现 |
| **场景覆盖** | 🟡 作为 dev 探针通过；作为验证 gate 不通过 | 8-probe 单规则隔离设计优秀；但缺少 Rule 9/19、多船、不合作目标、风浪扰动 |
| **评价指标** | 🔴 不通过 | Timing(动作时机)、Magnitude(动作幅度)、Stand-on 双向窗口 三个关键维度完全缺失；现有指标无法拦截"延迟小动作擦边过关" |
| **阈值来源** | 🔴 不通过 | 300m 阈值无文献支撑；ODD-A 1.0NM→0.5NM 降尺度逻辑未文档化；生成器与 YAML 不一致 |

**最严重问题**: 测试"通过了"但前端 trace 不合理——根因不在评估框架本身，而在于 **(a)** 评估器缺少时序和幅度量化指标，允许"5° 晚打舵擦边过关"；**(b)** M4/M5/M6 有架构缺陷（缺回归态、缺回归段、幽灵冲突），但评估器无法检测这些缺陷。

---

## 2. 架构设计评审

### 2.1 7 层评估框架 — 🟢 设计方向正确

| 层 | 名称 | Spec 定义 | 代码实现 | 评价 |
|:---|:---|:---|:---|:---|
| L1 | Scenario Validity | ✅ 定义明确 | ❌ 无代码 | 🔴 严重缺失 |
| L2 | Safety Floor | ✅ min_separation ≥ cpa_threshold | ⚠️ 分散在 `run_6_scenarios.py` | 🟡 需统一 |
| L3 | Dynamic Risk | ✅ approach/post-pass 拆分 | ❌ 无拆分代码 | 🔴 严重缺失 |
| L4 | COLREG Compliance | ✅ per-rule lifecycle | ⚠️ `rule_compliance_evaluator.py` 有但不完整 | 🟡 需扩展 |
| L5 | Route Recovery | ✅ returned_to_route + XTE | ⚠️ XTE 检查在 batch_runner | 🟡 需统一 |
| L6 | Seamanship/Efficiency | ✅ path_ratio/overshoot | ❌ 无代码 | 🔴 严重缺失 |
| L7 | Stability | ✅ 8 KPIs | ✅ `stability_scorer.py` | 🟢 已实现 |

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

| 缺陷 | 表现 | 评估器能否检测 | 影响 |
|:---|:---|:---|:---|
| M4 缺 RETURN_TO_ROUTE | AVOID→TRANSIT 跳变，XTE 持续增大 | ⚠️ `behavior_toggles` 可检测跳变，但无法检测"缺回归" | 中 |
| M5 fallback 无回归段 | 避碰后直线走远 | ⚠️ `max_route_xte > 500m` 可检测，但无专门指标 | 中 |
| M6 衰减定时器"幽灵冲突" | TCPA<0 + 距离增大仍锁 AVOID | ⚠️ `conflict_toggles` 可能检测到频繁 toggle | 高 |

**来源**: CodeGraph exploration (M4 behavior_definitions.yaml, M5 mid_mpc_node.cpp, M6 colregs_reasoner_node.cpp)
**置信度**: 🟢 High (代码直接证据)

---

## 3. 场景覆盖评审

### 3.1 现有 8-Probe 逐场景评估

| # | 场景 ID | 规则 | 设计意图 | 几何有效性 | CPA 合理性 | 设计质量 |
|:---|:---|:---|:---|:---|:---|:---|
| P1 | `rule14-ho` | R14 对遇 | 纯对遇基线，M6 onset-latch 回归 | ✅ DCPA≈0 直线求解 | 🟡 185.2m(L2走廊约束) | 🟢 优秀 |
| P2 | `rule14-ho-port` | R14 对遇边界 | port-biased 不误判为穿越 | ✅ 方位355° 精准边界 | 🟡 185.2m(同P1) | 🟢 优秀 |
| P3 | `rule13-ot` | R13 追越 | 13(d) 方位前移不得重分类 | ✅ 同向追越 DCPA≈0 | 🟡 300m(需补证据) | 🟢 优秀 |
| P4 | `rule15-cs` | R15/R16 穿越 | 右舷穿越标准让路 | ✅ 相对方位50° 碰撞求解 | 🟢 926m(0.5NM 合理) | 🟢 优秀 |
| P5 | `rule15-cs-2` | R15/R16 短TCPA | 逼早动作 (Rule 8b) | ✅ 短距离强约束 | 🟢 926m | 🟢 良好 |
| P6 | `rule15-cs-edge` | R15 边界 | 25° 头/穿边界，不抖R14/R15 | ✅ 精准夹击对 | 🟡 300m(需补证据) | 🟢 优秀 |
| P7 | `rule15-ot-boundary` | R15/R13 边界 | 108° 穿/追边界，不抖R15/R13 | ⚠️ 目标 45.7kn 不现实 | 🟡 300m(需补证据) | 🟡 目标船速过高 |
| P8 | `rule17-cr-so` | R17 直航 | 前期保向→末段17(b) | ✅ 直线应让不让 | 🟡 185.2m(紧急底线) | 🟢 优秀 |

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

| 缺口 | 描述 | 行业依据 | 严重度 |
|:---|:---|:---|:---|
| **Rule 9 受限水道** | 所有场景在空旷水域，无 Geofence 约束 | IMO MASS Code 风险评估覆盖搁浅 | 🔴 安全红线 |
| **Rule 18 多船优先权** | 无双船以上冲突 | Imazu-22 含 3-4 船 | 🔴 仲裁逻辑未测 |
| **Rule 19 能见度不良** | 无雾天场景，RESTRICTED_VIS 行为未测 | Rule 19 双方均让步 | 🔴 完全盲区 |
| **不合作机动目标** | R17 目标不减速不转向，但无目标主动机动 | IMO Circ.1604 异常情况 | 🔴 最危险场景 |
| **风/流扰动** | 所有场景 wind=0, current=0 | IMO MASS Code 环境扰动 | 🟡 ODD-A 基线正常但不全面 |
| **Port 侧穿越 (own = give-way)** | R15 场景全部为右舷穿越(own give-way) | 左舷穿越 own stand-on 由 R17 覆盖 | ⚪ R17 已覆盖 |

**来源**: IMO MSC.1/Circ.1604, Imazu-22 scenario set, DNV 55-scenario framework (Pedersen 2023)
**置信度**: 🟢 High

---

## 4. 评价指标评审

### 4.1 已实现指标 — 按层评审

#### Layer 2: Safety Floor (CPA 门限)

| 指标 | 实现 | 阈值 | 问题 |
|:---|:---|:---|:---|
| `min_cpa_nm` | `kpi_deriver.py` L32 | per-scenario profile | 🟢 基本可用 |
| `cpa_min_m_ge` | YAML expected_outcome | 不统一 (185.2/300/500/926) | 🟡 需动态参数化 |
| `route_corridor_pass_limit_m` | YAML | 500m (需与 L2 航道宽度对齐) | 🟢 工程约束合理 |

#### Layer 4: Rule Compliance

| 指标 | 实现 | 评判标准 | 问题 |
|:---|:---|:---|:---|
| `rudder_side` | `rule_compliance_evaluator.py` L30-31 | R14 左舵=违反 | 🟢 正确 |
| `heading_change_deg` | 同上 L19-26 | ≥30°=full, ≥15°=partial | 🟡 30° 过低(见下方) |
| `cpa_nm` | 同上 | ≥0.27NM(500m)=pass | 🔴 **与 YAML profile 不一致** |
| `timing_stage` | 同上 L73-87 | R17 Stage 1/2 保向, Stage 3+ 可规避 | 🟡 仅定性，无量化窗口 |
| `role` | 同上 L43-58 | give-way/stand-on 角色判断 | 🟢 正确 |

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

| 阈值 | 场景画像 | 法规来源 | 学术来源 | LOA 倍数 | 置信度 | 评审结论 |
|:---|:---|:---|:---|:---|:---|:---|
| **185.2m** (0.1 NM) | R14 近距对遇, R17 in-extremis, 受限航道 | COLREGs 不给固定值; Rule 8(d) "safe distance" | Goerlandt & Kujala (2011) near-miss 基线; AIS 遇险统计 | 4.1L | 🟢 High | ✅ 物理碰撞红线合理，作为 emergency floor 无争议 |
| **300m** | R13 追越/安全跟随, R15 边界分类 | 无直接法规依据 | Coldwell (1983) 侧向 3L-4L ≈ 135-180m; 300m ≈ 1.6× 余量 | 6.7L | 🔴 Low | ⚠️ 无独立文献支撑 300m 值; **必须替换为参数公式** |
| **405m** (9L) | Ideal domain reference | 无直接法规依据 | Fujii (1971) 椭圆 4L 纵半轴; Goodwin (1975) 扇形; Szlapczynski (2006) | 9.0L | 🟢 High | ✅ 作为 ideal domain reference 优秀; 不应作硬 floor |
| **926m** (0.5 NM) | Open-water crossing give-way | Rule 8(d) "safe distance" 不给数值 | Nautical Institute 开阔水域 CPA ≥ 2.0 NM (降尺度); Sawada 2020 OZT | 20.6L | 🟡 Medium | ✅ 对 45m MASS 是合理降尺度; ⚠️ 不应用于受限航道 |

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

| 参数 | 值 | 来源 | 置信度 |
|:---|:---|:---|:---|
| `route_corridor_half_width_m = 1000` | L2 安全航道半宽 | 工程约束 (500m XTE limit × 2) | 🟢 High |
| `route_corridor_pass_limit_m = 500` | 不触发 L2 重规划的最大 XTE | 工程约束 | 🟢 High |
| `route_return_xte_m_lt = 150` | 归航 XTE 门限 | 工程经验值 | 🟡 Medium |
| `route_return_heading_deg_lt = 10` | 归航航向偏差门限 | 工程经验值 (5°-15° 范围内) | 🟡 Medium |
| `steering_reversals ≤4/2` | ROT 符号反转次数 | 工程经验值 | 🟡 Medium |
| `rot_hold_std_dps < 1.5` | 保持段偏航率方差 | 工程经验值 | 🟡 Low |
| `standon_hold_frac = 0.75` | 直航保向阶段占比 | COLREGs 17(a)(i) | 🟡 应回退到 M6 phase |

---

## 6. 实现一致性审查

### 6.1 Spec vs Code 关键不一致

| Spec 要求 | 代码实现 | 差距 |
|:---|:---|:---|
| 7 层统一 `TraceEvaluator` | 3 个分散组件 | 🔴 严重 |
| Layer 1 Scenario Validity | 不存在 | 🔴 严重 |
| Layer 3 Dynamic Risk 拆分 | 不存在 (整体计算) | 🔴 严重 |
| Layer 6 Seamanship/Efficiency | 不存在 | 🔴 严重 |
| 三维判决 safety/mission/colregs | 单一 `overall_pass` | 🟡 中等 |
| CPA threshold provenance 输出 | 不存在 | 🟡 中等 |
| Heading-on post-pass 规则 | 无代码 (M6 用定时器) | 🔴 严重 |
| per-rule lifecycle timeline | `rule_compliance_evaluator` 无时间线 | 🟡 中等 |
| `cpa_target_nm = 0.27` vs per-scenario profile | 硬编码 500m vs profile 185.2-926m | 🔴 不一致 |

### 6.2 生成器 vs YAML 不一致

| 参数 | 生成器默认值 | YAML 实际值 | 差距 |
|:---|:---|:---|:---|
| P1-P5 cpa_min | 926m | 185.2m (P1-P2), 300m (P3), 926m (P4-P5) | 🔴 3/5 不一致 |
| P6-P7 cpa_min | 500m (explicit) | 300m (YAML) | 🔴 不一致 |
| P8 cpa_min | 500m (explicit) | 185.2m (YAML) | 🔴 不一致 |
| cpa_acceptance profile | 不存在 | 每个场景有完整结构 | 🔴 完全缺失 |
| route_corridor 字段 | 不存在 | 每个场景有 | 🔴 完全缺失 |

**结论**: `gen_colreg_tier12.py` **不再是**唯一真源。后续参数变更必须同步更新生成器或承认 YAML 为真源。

---

## 7. 可以通过但需要注意的缺陷 ("虚假通过"根因分析)

### 7.1 "测试通过了但 trace 不合理"的典型模式

| 模式 | 通过指标 | trace 表现 | 根因 | 修复 |
|:---|:---|:---|:---|:---|
| **延迟小动舵** | CPA ≥ floor, starboard turn | TCPA=30s 时才打 5° 舵 | 缺 Timing+Magnitude 指标 | 添加 action_tcpa_s + max_heading_dev |
| **平行不归航** | CPA ≥ floor, XTE 最终 ≤ 150m | AVOID→TRANSIT 跳变, 跑远才回来 | M4 缺 RETURN_TO_ROUTE | 添加 M4 回归态 |
| **幽灵冲突** | CPA ≥ floor, 总是 PASS | 目标在身后仍被追踪 60s | M6 定时器滞后 | 改几何 past-and-clear |
| **末段急转** | CPA ≥ floor, starboard | 17(a) 直航期间持续偏航 5-8° | premature_giveway 阈值太松 | 8° 改 5° + 添加 TCPA 窗口 |
| **边间抖动** | behavior_toggles ≤ 2 | 频繁 R14↔R15 切换 | M6 扇区边界阈值 | role_onset_stable 已覆盖 |

---

## 8. 评审结论

### 8.1 前项优先行动

| 优先级 | 行动 | 预期效果 |
|:---|:---|:---|
| **P0** | 实现统一 `TraceEvaluator` (7 层, 含 Layer 1/3/6) | 消除 Spec-Code 不一致 |
| **P0** | 添加 action_tcpa_s + max_heading_dev 指标 | 杜绝"延迟小动作擦边过关" |
| **P0** | 修复 `rule_compliance_evaluator.py` 的硬编码 `cpa_target_nm=0.27` | 消除与 YAML profile 不一致 |
| **P0** | 添加 no-action baseline CI (Layer 1) | 验证场景冲突有效性 |
| **P1** | 将 300m 替换为 `max(0.1 NM, 6L)` 参数化公式 | 消除证据缺口 |
| **P1** | M6 Past-and-Clear 从定时器改为几何判定 | 消除"幽灵冲突" |
| **P1** | 同步 YAML ↔ 生成器 (恢复唯一真源) | 消除参数漂移风险 |
| **P1** | 添加 Stand-on 双向窗口 (premature + delayed) | 完整覆盖 Rule 17 |
| **P2** | 新增 rule09/rule19/multiship/uncooperative 场景 | 消除规则覆盖盲区 |
| **P2** | 生成器重构：加入 dynamic CPA profile | 支持参数化 ODD 依赖阈值 |

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