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

| # | ID | 规则 | 角色 | 核心行为 | CPA Profile | LOA 倍数 | 来源 / 置信度 |
|:---|:---|:---|:---|:---|:---|:---|:---|
| P1 | `rule14-ho` | R14 | give-way | 纯对遇右转 | `corridor_close` 185.2m | 4.1L | 🟢 Goerlandt (2011) near-miss 基线 |
| P2 | `rule14-ho-port` | R14 | give-way | port-biased 仍右转 | `corridor_close` 185.2m | 4.1L | 🟢 同 P1 |
| P3 | `rule13-ot` | R13 | give-way | 追越/安全跟随 | `corridor_follow` 270m | 6.0L | 🟡 `max(0.1NM, 6L)` 公式 |
| P4 | `rule15-cs` | R15/R16 | give-way | 右舷穿越让路 | `open_water` 926m | 20.6L | 🟢 Nautical Institute 0.5NM 基线 |
| P5 | `rule15-cs-2` | R15/R16 | give-way | 短 TCPA 早动作 | `open_water` 926m | 20.6L | 🟢 同 P4 |
| P6 | `rule15-cs-edge` | R15 | give-way | 头/穿边界 25° | `corridor_boundary` 270m | 6.0L | 🟡 同 P3 公式 |
| P7 | `rule15-ot-boundary` | R15 | give-way | 穿/追边界 108° | `corridor_boundary` 270m | 6.0L | 🟡 同 P3 公式 |
| P8 | `rule17-cr-so` | R17/R15 | stand-on | 保向→17(b) | `extremis` 185.2m | 4.1L | 🟢 Rule 17(b) 紧急底线 |
| P9* | `rule09-channel` | R9/R14 | give-way | 受限航道对遇 | `channel` 270m | 6.0L | 🟡 新增; 工程约束 |
| P10* | `multiship-avoid` | R14+R15 | give-way | 多船交叉冲突 | `open_water` 926m | 20.6L | 🟡 新增; 仲裁测试 |
| P11* | `uncooperative-target` | R17(b) | stand-on | 让路船违规左转 | `extremis` 185.2m | 4.1L | 🟡 新增; Grlj (2025) 67% 碰撞涉及违规 |
| P12* | `rule19-fog` | R19 | both | 雾天双方避让 | `fog` 405m | 9.0L | 🟡 新增; Rule 19 无 give-way |

* P9-P12 为新增场景，需 harness 改动（Geofence 支持、多目标、目标机动脚本、能见度控制）

### 1.3 CPA 动态阈值模型

**核心公式**: `CPA_safe(ODD, L, v_r) = α(ODD) · L + β(ODD) · v_r`

| ODD Profile | α | β (s) | FCB 示例 (L=45m, v_r=10m/s) | 硬 Floor | 对应场景 |
|:---|:---|:---|:---|:---|:---|
| `open_water` | 10.0 | 20 | 10×45+20×10 = 650m ≈ 0.35 NM | 926m (0.5NM, 项目基线) | P4, P5, P10 |
| `corridor` (近距/边界/追越) | 5.0 | 10 | 5×45+10×10 = 325m → 取 270m | max(0.1NM, 270m)=270m → 包络 300m | P1-P3, P6-P7 |
| `extremis` (R17 末段) | 3.0 | 4 | 3×45+4×10 = 175m → 取 185.2m (0.1NM) | 185.2m (0.1NM) | P8, P11 |
| `channel` (受限航道) | 5.0 | 10 | 325m → 取 270m → 包络 300m | max(0.1NM, XTE_limit) | P9 |
| `fog` (能见度不良) | 7.0 | 15 | 7×45+15×10 = 465m → 取 405m (9L) | 9L = 405m | P12 |

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

| 检查项 | 条件 | 来源 |
|:---|:---|:---|
| `no_action_dcpa` | 无避碰动作时 DCPA < 500m | `simulate.py` kinematic 验证 |
| `tcpa_approach` | 无避碰动作时 TCPA > 0 且 closing_speed > 0 | 几何验证 |
| `initial_rel_bearing` | 初始相对方位与场景声明一致 (±5°) | M2 分类验证 |
| `conflict_genuine` | M6 在无避碰时 conflict_detected=true | 仿真验证 |

**实现**: 对每个场景运行 `no_action` baseline（OS controller = 纯 TRANSIT），验证 `min(CPA) < cpa_threshold` 且 `closing_speed > 0`。

### 2.3 Layer 2: Safety Floor

**不变**: 全程 `min_separation_m ≥ cpa_threshold_m`，其中 `cpa_threshold_m` 由动态 CPA 模型计算。

**新增**: 输出 `threshold_m` / `profile` / `basis` / `nm_equivalent` / `loa_multiplier` / `source_confidence` 到报告。

### 2.4 Layer 3: Dynamic Risk (新增实现)

**拆分风险暴露为 4 个阶段**:

| 阶段 | 定义 | 检查 | 来源 |
|:---|:---|:---|:---|
| **approach_warning** | `TCPA > 0, closing_speed > 0, range > cpa_safe` | `exposure_duration_s < threshold` | Rule 7 风险判断 |
| **approach_danger** | `TCPA > 0, closing_speed > 0, range ≤ cpa_safe` | `exposure_duration_s < max_danger_duration` | Rule 8 及早行动 |
| **post_pass_clearance** | `TCPA < 0, closing_speed ≤ 0, target abaft` | `min_separation_m ≥ emergency_floor_m` | Good seamanship |
| **recovery** | `conflict_detected = false, XTE > return_threshold` | `recovery_time_s < max_recovery_time` | Mission completion |

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

| 规则 | 阶段检查 | 评估标准 | 来源 |
|:---|:---|:---|:---|
| R14 | `onset` → `avoiding` → `past-and-clear` | 右转≥25°(开阔)/15°(边界); 不左转; port-to-port pass | Rule 14(a) |
| R13 | `onset` → `avoiding` → `past-and-clear` | 追越义务持续; 方位前移不重分类; 最终超车 | Rule 13(d) |
| R15/R16 | `onset` → `early_action` → `clearing` | 右转; 不穿目标前方; 及早(Rule 16) | Rule 15/16 |
| R17 | `hold` → `extremis` → `independent_action` | 前75%偏航<8°; 17(b)在窗口[40s,75s]TCPA内启动 | Rule 17(a)(i)/(b) |
| R19 | `both_give_way` → `slow_speed` → `avoid` | 无角色划分; 减速至安全速度; 不左转 | Rule 19(a)(b) |

### 2.6 Layer 5: Route Recovery

**不变**: `returned_to_route`, `final_xte_m`, `max_route_xte_m`, `XTE ≤ 500m` (核心 gate)

**新增**: `recovery_time_s` — 从 `conflict_detected=false` 到 `XTE < route_return_xte_m` 的时间。对开阔水域场景不应超过 120s。

### 2.7 Layer 6: Seamanship / Efficiency (新增实现)

| 指标 | 计算 | Gate | 来源 |
|:---|:---|:---|:---|
| `path_ratio` | `actual_distance / great_circle_distance` | ≤1.5 (open water) / ≤2.0 (channel) | Seafaring efficiency |
| `integrated_xte_m` | `∫|XTE(t)|dt` | ≤ `cpa_threshold * avoidance_duration` | Route discipline |
| `overshoot_deg` | 归航时最大反向偏航 | ≤ 15° | Good seamanship |
| `post_pass_clearance_m` | 过船后最近距离 | ≥ `emergency_floor_m` | Rule 8(d) safe distance |
| `max_excursion_m` | 避碰过程中最大 XTE | ≤ `route_corridor_half_width_m` | Mission corridor |

### 2.8 Layer 7: Stability (现有 + 修订)

| KPI | 修订 | 新 Pass 阈值 | 来源 |
|:---|:---|:---|:---|
| `behavior_toggles` | 不变 | ≤2 (一起一落) | 工程经验 |
| `plan_valid_segments` | 不变 | ≤2 | 工程经验 |
| `steering_reversals` | 不变 | give-way ≤4 / stand-on ≤2 | 船艺操作极限 |
| `rot_hold_std_dps` | 不变 | <1.5 | 工程经验 |
| `conflict_toggles` | 不变 | ≤2 | M6 分类稳定性 |
| `role_onset_stable` | 不变 | 0 次义务翻转 | Rule 13(d) |
| `turn_starboard_magnitude` | **新增幅度** | give-way max_dev ≥ 25°(OW) / 15°(restricted) | Rule 8(b) |
| `premature_giveway` | **修订阈值** | stand-on 前75% < **8°** (从10°收紧) | Rule 17(a)(i) |
| `stand_on_action_window` | **新增** | TCPA ∈ [40s, 75s] 出现 ≥15° 转向 | Rule 17(b) |
| `action_timing` | **新增** | give-way TCPA ≥ 180s(OW) / 100s(restricted) | Rule 16 |

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

| 参数 | 值 | 公式来源 | 文献来源 | 置信度 |
|:---|:---|:---|:---|:---|
| `alpha_open` | 10.0 | α·L 项 | Fujii (1971), Goodwin (1975), Szlapczynski (2006) | 🟢 High |
| `beta_open` | 20s | β·v_r 项 | Gil et al. (2021) CADCA | 🟢 High |
| `alpha_restricted` | 5.0 | α·L 项 | Coldwell (1983) 侧向 3L-4L | 🟡 Medium |
| `beta_restricted` | 10s | β·v_r 项 | 项目工程折中 | 🟡 Medium |
| `alpha_emergency` | 3.0 | α·L 项 | Goerlandt & Kujala (2011) near-miss | 🟢 High |
| `beta_emergency` | 4s | β·v_r 项 | 项目工程折中 | 🟡 Medium |
| `loa_m` | 45.0 | FCB LOA | `fcb_simulator_plugin.cpp` L76 | 🟢 High |
| `standon_hold_max_deg` | 8.0 | Rule 17(a)(i) 容差 | COLREGs + 工程经验 (从10°收紧) | 🟡 Medium |
| `standon_window_tcpa` | (40, 75) | 操纵性最迟机动点 | Antigravity 设计报告 §3.3 | 🟢 High |
| `min_alteration_open_deg` | 25.0 | Rule 8(b) "readily apparent" | Nautical Institute; Brekke (2023) | 🟢 High |
| `min_alteration_restricted_deg` | 15.0 | Rule 8(b) 受限水域 | 工程经验 | 🟡 Medium |
| `tcpa_safe_open_s` | 180 | Rule 16 "as early as possible" | Nautical Institute *Bridge Watchkeeping* | 🟢 High |
| `tcpa_safe_restricted_s` | 100 | Rule 16 受限水域 | 工程经验 | 🟡 Medium |
| `route_corridor_half_width_m` | 1000 | L2 VoyagePlan safety corridor | 文档约束 | 🟢 High |
| `route_corridor_pass_limit_m` | 500 | 不触发 L2 重规划 | 工程约束 | 🟢 High |

---

## 6. 实现优先级

### Phase 1: 必须修复 (P0) — 测试平台正确性保障

| 优先级 | 任务 | 预计工作量 | 依赖 |
|:---|:---|:---|:---|
| P0-1 | 实现 `TraceEvaluator` 统一 7 层评估器 | 3 人天 | 无 |
| P0-2 | 添加 Layer 1 Scenario Validity + no-action baseline CI | 1 人天 | simulate.py |
| P0-3 | 添加 action_tcpa_s + max_heading_dev 指标 | 1 人天 | TraceEvaluator |
| P0-4 | 修复 `rule_compliance_evaluator.py` 硬编码 cpa_target_nm=0.27 | 0.5 人天 | 无 |
| P0-5 | 添加 Stand-on 双向窗口 (premature + delayed) | 1 人天 | TraceEvaluator |
| P0-6 | 重构 `gen_colreg_tier12.py` 恢复唯一真源 | 2 人天 | ODD_PARAMS |

### Phase 2: 架构修复 (P1) — 消除"虚假通过"根因

| 优先级 | 任务 | 预计工作量 | 依赖 |
|:---|:---|:---|
| P1-1 | 300m 替换为 `max(0.1 NM, 6L)` 参数化公式 | 0.5 人天 | gen_colreg_tier12 |
| P1-2 | M6 Past-and-Clear 从定时器改为几何判定 | 3 人天 | M6 代码 |
| P1-3 | M4 添加 RETURN_TO_ROUTE 状态 (weight=0.55) | 2 人天 | M4 代码 |
| P1-4 | M5 fallback 添加回归段 (3-segment arc) | 3 人天 | M5 代码 |
| P1-5 | Layer 3 Dynamic Risk 拆分实现 | 2 人天 | TraceEvaluator |

### Phase 3: 覆盖扩展 (P2) — 消除规则盲区

| 优先级 | 任务 | 预计工作量 | 依赖 |
|:---|:---|:---|:---|
| P2-1 | P9 rule09-channel 场景 (需 Geofence harness) | 2 人天 | SIL harness 改动 |
| P2-2 | P10 multiship-avoid 场景 (需多目标支持) | 1 人天 | SIL harness 改动 |
| P2-3 | P11 uncooperative-target 场景 (需脚本化机动) | 2 人天 | target_vessel_node |
| P2-4 | P12 rule19-fog 场景 (需能见度控制) | 2 人天 | ODD-D 触发路径 |
| P2-5 | CPA 阈值溯源输出 (threshold_m, basis, confidence) | 1 人天 | TraceEvaluator |

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