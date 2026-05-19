# L3 战术决策与避碰层 — 架构设计规约 (Architecture Design Specification)

**基于候选 C：心智骨架 + 算法插件混合架构**

| 属性 | 值 |
|------|-----|
| 文档编号 | SPEC-L3-TDCAL-ARCH-001 |
| 版本 | **v1.1** |
| 日期 | 2026-04-28 |
| 作者 | 用户 + Claude |
| 状态 | **Spec 草案**（v1.0 → v1.1 已关闭全部 34 项 TBD，转 7 项项目级 DEGRADED 假设；待 HAZID workshop 冻结）|
| 上游依据 | Research v1.2 (2026-04-28 冻结) + nlm 本地笔记本 (34 篇来源) + nlm-research 独立验证 (36 篇) + Stage 1-4 全 TBD 关闭报告 |
| 下游输出 | Spec Part 2 (详细设计, 待启动) + Spec §2.5 STPA workshop |

---

## 0. 版本说明（v1.0 → v1.1 关键变化）

v1.1 在 NotebookLM 评审 v1.0 基础上做了以下增量：

1. **§3 各 Stage 关键参数**：从"初始基线 (待 HAZID 冻结)"细化为具体引用 + 4 水域分级（链回 Research §12.2 + IMO MSC.192(79) + Eriksen 2020 + Nguyen 2018 等）
2. **§3.4 Stage 4 紧急通道**：补充 RTA Simplex Architecture 工业先例（NASA Auto-GCAS / NASA TM-20220015734 / Boeing 777 / Black-Box Simplex arxiv 2102.12981）
3. **§4 Doer-Checker 双层**：补充 5 条具体 STL 规则（CHK-01..05）+ Pedersen 2022 STL 形式化引用
4. **§6 ASDR 4 元组**：补充完整 schema 字段（含 ASDR 与 Stage 2/3 决策证书的关系）
5. **§11 (新增)** v1.0 已关闭 TBD 状态总览（34 项中 27 CLOSED + 7 DEGRADED + 0 BLOCKING）
6. **§12 (新增)** v1.0 残留 7 项 DEGRADED 项目级假设条款（明确验证路径 + 失效后处理）
7. **§14 (新增)** 引用证据库（按 E1/E2/E3/E4 级别整理，含 Research v1.2 已建立的引用 + 本文档新增引用）
8. **架构图升级到 v2**：见 `l3_tactical_layer_candidate_c_architecture_v2.svg`，含 (a) Stage 4 → Stage 2 升级反馈虚线、(b) Stage 4 → 控制层反射弧虚线、(c) Checker 跨 Stage 2-3 横向闸门、(d) 全 Stage 显式 ASDR 写入

---

## 1. L3 层命名

| 属性 | 值 |
|------|-----|
| **中文全称** | **战术决策与避碰层** |
| **英文全称** | **Tactical Decision & Collision Avoidance Layer** |
| **缩写** | **L3-TDCAL** |
| **定位** | 自主航行系统四层架构的第三层，上承战略层 (L2 Voyage Planner)，下接控制层 (L4 Guidance & Control) |

**命名理由**：突出本层的两个核心职责——**战术决策**（模式管理、会遇分类、责任判定）和**避碰执行**（轨迹生成、安全校验、闭环监控），与 COLREGs 避碰规则体系和 CCS 智能系统认可对"决策能力"的独立评估要求直接对齐。

---

## 2. 架构总览

### 2.1 ASCII 概念图

```
                   ┌─────────── ROC (远程控制中心) ───────────┐
                   │  override · 模式切换 · 接管确认           │
                   └────────────┬─────────────────────────────┘
                                │
  ┌─ 感知融合 ──┐    ┌─ 战略层 L2 ──┐          ┌─ ROC ──┐
  │ 目标+ENC+自船│    │ 全局航路+时序 │          │         │
  └──────┬──────┘    └──────┬───────┘          └────┬────┘
         │                  │                       │
         ▼                  ▼                       ▼
  ┌────────────────────────────────────────────────────────────┐
  │             L3 战术决策与避碰层 (TDCAL)                     │
  │                                                            │
  │   ┌────────┐    ┌────────┐    ┌────────┐                   │
  │   │Stage 1 │───▶│Stage 2 │───▶│Stage 3 │───▶ 控制层 L4    │
  │   │  看    │    │ 判+Doer│    │  动    │                   │
  │   │态势感知 │    │ 决策   │    │ 轨迹执行│                  │
  │   └───┬────┘    └───┬────┘    └───┬────┘                   │
  │       │      ┌──────┴──────┐      │                        │
  │       │      │ ② Checker   │      │                        │
  │       │      │ 闸门 (跨层) │      │                        │
  │       │      │ Doer-Checker│      │                        │
  │       │      └──────┬──────┘      │                        │
  │       │             │             │                        │
  │       │  ┌──────────┴─────────────┘                        │
  │       │  │ ① 升级反馈 (非紧急)  ┌───────────┐              │
  │       │  ▲                       │  Stage 4  │              │
  │       │  └─────────────────── ── │   盯      │              │
  │       │                          │ 并行监控  │              │
  │       │                          └─────┬─────┘              │
  │       │                                │ ③ 反射弧 (紧急,    │
  │       │                                │   绕过 S2/S3)      │
  │       │                                │                    │
  │       └──── 闭环 (重新看) ──────────────┘                   │
  │                                                            │
  │   ┌────────────────────────────────────────────────────┐   │
  │   │  Mode Manager · 模式管理器 (跨层贯通)               │   │
  │   │  D2 ↔ D3 ↔ D4 + MRC + ROC override                 │   │
  │   └────────────────────────────────────────────────────┘   │
  │                                                            │
  │   ④ ASDR ← 全 Stage 写入 (4 元组决策审计记录)              │
  └────────────────────────────────────────────────────────────┘
         │                                       │
         ▼                                       ▼
  ┌──────────────┐                       ┌──────────────┐
  │  控制层 L4   │ ◀── ③ 反射弧 ────────│   公共服务   │
  │ 航向/航速/   │                       │ 日志/健康/   │
  │ 紧急级别     │                       │ ASDR 持久化  │
  └──────────────┘                       └──────────────┘
```

### 2.2 高保真架构图（v2 SVG）

详见同目录 `l3_tactical_layer_candidate_c_architecture_v2.svg`。该图相对 v1 版本的三处优化（基于 NotebookLM 评审）：

1. **Stage 4 并行监控**：标注"[与 Stage 3 并行]"，并增加 Stage 4 → Stage 2 反向反馈虚线（红色 ⌐ 形）表达"升级触发"语义
2. **Checker 跨层闸门**：从 Stage 3 内部"提"出来，独立成为 Stage 2 → Stage 3 之间的横向组件，明示 5 条 STL 规则（CHK-01..05）跨 Stage 2/3 校验
3. **反射弧虚线**：Stage 4 → 控制层 L4 的红色 L 形虚线，绕过容器右侧外部，明示"绕过 Stage 2 Doer + Stage 3 中程 MPC"的 RTA Simplex 单向切换语义
4. **全 Stage ASDR 写入**：每个 Stage box 内底部加 "→ ASDR ④"标签，公共服务 box 突出"ASDR ← 全 Stage 写入"

### 2.3 候选 C 双层抽象

候选 C 架构的本质是**双层抽象**，对应 CCS 智能系统认可三个硬约束（CCS Nx 单一入级 / D2-D4 全自主等级 / 多船型扩展）：

| 抽象层 | 职责 | 实现 | 可变性 |
|-------|------|------|--------|
| **外层骨架（CMM 心智骨架）** | 4 阶段认知边界（看-判-动-盯）+ 接口契约冻结 | 纯白盒 C++17，确定性逻辑 | v1 冻结后不变（CCS 单一入级） |
| **内层算法插件** | 各 Stage 内部具体算法（CPA/TCPA / EKF / MPC / BC-MPC / Checker 等） | 可独立替换（白盒优先；ML 插件经 Doer-Checker 边界） | 可独立 V&V + 独立替换（多船型扩展 + D2-D4 全谱）|

**两个工程化属性**：
- **骨架稳定**：CMM 4 阶段心智边界与 COLREGs 避碰规则 + Endsley 1995 SA 模型双向锚定，可作为 CCS 验船师评估的稳定参考
- **插件可替换**：FCB → 多船型扩展时只需替换 `VesselDynamicsPlugin` 实现（注入新水动力系数）+ 保留 Stage 3 骨架；ML 增强时通过 Doer-Checker 边界限制 ML 输出（AD-04）

---

## 3. 四 Stage 详细设计

### 3.1 Stage 1「看」— 态势感知引擎 (Situational Awareness Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 看 (Observe) |
| **SA 层级** | Endsley Level 1 (Perception) + Level 2 (Comprehension) |
| **学术基础** | Endsley 1995; Sharma 2019 GDTA; Eriksen 2020 *Frontiers in Robotics and AI* |
| **触发周期** | 1-5 Hz（开阔水域 1 Hz；FCB 高速段 25-35 节启用 5 Hz；可配置）|
| **骨架职责** | 统一感知接口：接收多源融合数据 → 输出标准化威胁列表 |

#### 3.1.1 算法插件（4 个）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 学术 / 工业依据 |
|---|---------|---------|------|------|----------------|
| 1.1 | **CPA/TCPA 计算器** | 几何矢量法（基于相对运动）| 本船状态 + 目标船状态 | DCPA, TCPA, 相对方位, 相对速度 | COLREGs Rule 7; ITU-R M.1371-5; Cockcroft & Lameijer 7th Ed |
| 1.2 | **目标跟踪滤波器** | EKF v1.0（CV + CT 双模型）/ IMM v1.x（交互多模型）| 感知融合后的轨迹点 + 协方差 | 平滑轨迹 + 航速航向估计 + 不确定性协方差 | Bar-Shalom 1988 IMM; IMO MSC.192(79) ARPA 性能标准 |
| 1.3 | **运动预测器** | v1.0 线性外推 + 圆弧预测；v1.x 加 GP 在线学习（Doer-Checker 内启用）| 平滑轨迹 + 协方差 | 5-15 min 预测航迹 + 概率分布 | GP-based: Xue 2022 *Ocean Engineering*; Reza 2025 *Scientific Reports* |
| 1.4 | **威胁评分器** | 多因子加权和（白盒；CPA + TCPA + bearing_rate + target_type）| 目标列表 + CPA/TCPA + 类型 | 按碰撞风险降序的威胁列表 + 5 档 CRI | Nguyen 2018 *Algorithms* 11(12):204 AHP 权重; MarinePublic 2026 ARPA 实操 |

#### 3.1.2 关键参数

**4 水域 CPA_safe 分级**（来源 Research §12.2）：

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| CPA_safe | 1.0 nm (1852m) | 0.5 nm (926m) | 0.3 nm (556m) | 0.2 nm (370m) |
| 能见度不良修正 | < 2 nm 时 CPA_safe × 1.5（COLREGs Rule 19）|

**威胁评分权重**（Nguyen 2018 AHP 因子映射后的 v1.0 初值）：

```yaml
threat_score_weights:
  cpa: 0.20            # 安全距离硬约束（项目调高于 Nguyen 0.10）
  tcpa: 0.45           # 紧迫程度（与 Nguyen 0.5 接近）
  bearing_rate: 0.25   # "方位不变距离近"信号（替代 Nguyen relative_distance 0.3）
  target_type: 0.10    # Rule 18 优先序（替代 Nguyen azimuth）
  # 总和 = 1.0，AHP 起点；最终值待 HAZID workshop 冻结（DEGRADED）
threat_class_thresholds:    # 5 档 CRI 阈值
  safe: [0.0, 0.20)
  monitoring: [0.20, 0.40)
  warning: [0.40, 0.60)
  danger: [0.60, 0.80)
  emergency: [0.80, 1.0]
```

**bearing_rate 阈值**（Cockcroft 7th Ed + Skipper Tips 工业实操）：

```yaml
bearing_rate_alarm:
  default_deg_per_s: 0.05         # 3°/min，开阔水域中距离基线
  modifiers:
    - condition: "range < 2 nm"
      multiplier: 1.5             # 近距离阈值更严格
    - condition: "target_length > 200 m"
      multiplier: 1.5             # 大船 + 拖带阈值更严格
    - condition: "range < 1 nm AND target_length > 200 m"
      multiplier: 2.0             # 复合条件
  arpa_safety_margin_nm: 0.5      # ARPA 工业实操：CPA 上扣 0.5 nm
```

**EKF 协方差初值**（基于 IMO MSC.192(79) 5.25.4.7.1 ARPA 性能标准，95% 概率）：

```yaml
ekf:
  observation_noise_R:
    radar:
      sigma_range_m: "max(50, 0.01 * range_m)"   # IMO MSC.192(79)
      sigma_bearing_rad: 0.0349                   # 2° = 0.0349 rad
    ais:
      sigma_position_m: 10.0                      # AIS Class A 典型 (ITU-R M.1371-5)
      sigma_cog_rad: 0.00175                      # 0.1°
      sigma_sog_mps: 0.05                         # 0.1 kn
  process_noise_Q:
    cv_model_sigma_a_mps2: 1.5                    # 商船加速度白噪声
    fcb_high_speed_sigma_a_mps2: 3.0              # FCB 25-35 节段加大
    ct_model_sigma_omega_radps2: 0.05             # 转弯率加速度
  adaptive: false                                  # v1.0 固定参数；v1.1 启用自适应 EKF
```

**预测时域**（与 Stage 3 中程 MPC 自洽）：

```yaml
prediction:
  horizon_s: 300        # 5 min，对齐 Stage 3 中程 MPC（AD-02）
  step_s: 10            # Stage 1 输出步长
  high_freq_horizon_s: 60   # 用于 BC-MPC 兜底层
  high_freq_step_s: 5       # Eriksen 2019 BC-MPC 一致
```

#### 3.1.3 输出契约

```yaml
output:
  threat_list:
    - target_id: string
      position: [lat, lon]
      speed_kn: float
      course_deg: float
      dcpa_nm: float
      tcpa_min: float
      bearing_deg: float
      bearing_rate_deg_per_s: float
      threat_score: float            # 0.0 - 1.0
      threat_class: enum             # safe / monitoring / warning / danger / emergency
      target_class: enum             # PDV / SV / FV / CBD / RAM / NUC（Rule 18 类型）
      predicted_track:               # 预测航迹
        - timestamp_offset_s: float
          position: [lat, lon]
          cov_2x2: [[float,float],[float,float]]    # 不确定性协方差
  own_ship:
    position: [lat, lon]
    sog_kn: float
    cog_deg: float
    heading_deg: float
    rate_of_turn_deg_per_min: float
  environment:
    water_zone: enum                 # open_sea / coastal / narrow_channel / port
    visibility_nm: float
    sea_state: int                   # Beaufort 0-12
```

#### 3.1.4 ASDR 写入

```yaml
{
  stage: 1,
  timestamp: iso8601,
  cmm_stage: "observe",
  threat_list_snapshot: [...],
  own_ship_snapshot: {...},
  sensor_health: {...},
  plugin_versions: {ekf: "v1.0", predictor: "v1.0", scorer: "v1.0"}
}
```

---

### 3.2 Stage 2「判」— 规则决策引擎 (COLREGs Decision Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 判 (Judge) |
| **认知模型** | Klein 1998 RPD + COLREGs Rule 5-19 形式化 |
| **触发周期** | 1 Hz（每次威胁列表更新即触发）|
| **骨架职责** | 统一决策接口：威胁列表 → Doer 主决策 → 决策证书 → Checker-D 校验 |

#### 3.2.1 6 步推理链

1. **会遇分类** (Rule 13/14/15：追越 / 对遇 / 交叉 / 安全通过)
2. **责任分配** (Rule 16/17：让路船 / 直航船)
3. **类型优先序判定** (Rule 18：失控 > 受限 > 限吃水 > 渔船 > 帆船 > 机动船)
4. **时机判定** (Rule 16 让路船及早行动 / Rule 17 直航船三阶段)
5. **行动选择** (Rule 8：转向优先 / 减速次之 / 停船兜底)
6. **效果验证** (Rule 8 "明显" + 维持时间)

#### 3.2.2 算法插件（4 个 + Checker-D）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 依据 |
|---|---------|---------|------|------|------|
| 2.1 | **会遇分类器** | 几何拓扑判定（Rule 13/14/15 数学形式化）| 目标船相对方位 + 航向差 | head_on / crossing(give-way/stand-on) / overtaking / safe_passage | COLREGs Rule 13-15; Cockcroft & Lameijer 7th Ed |
| 2.2 | **Rule 18 优先序判定器** | 规则链推理（Rule 18 类型层级）| 本船类型 + 目标船类型 | 操纵能力优先级: NUC > RAM > CBD > FV > SV > PDV | COLREGs Rule 18 |
| 2.3 | **多目标冲突消解器** | v1.0 K-means 聚类 + 威胁排序 + 单目标分别处理；v1.x VO / MDP 备选 | 多个会遇关系 + 优先级 | 统一避让方案（避让方向 + 时序） | Eriksen 2020; Frontiers MS 2025-12 |
| 2.4 | **Doer 主决策**（候选方案生成） | 规则推理 + 启发式搜索 +（可选）LLM 语义辅助 | 统一避让方案 | 候选决策集 {direction, magnitude, timing}_i + 决策证书 | Klein 1998 RPD; Jackson 2021 Certified Control |
| **Checker-D** | **决策合规校验器** | 确定性形式化规则（一阶谓词逻辑 / STL）| Doer 输出的候选决策证书 | PASS/FAIL + 违规项清单 + 最近合规替代方案 | Jackson 2021 MIT CSAIL; Pedersen 2022 STL; IEC 61508-3 |

#### 3.2.3 Checker-D 校验规则集（v1.0 基线 5 条 STL，全白盒）

```yaml
# 见 §4.4 Checker 完整规则集
# Stage 2 决策合规层面的 STL 规则：
# CHK-03 Rule 8 大幅: eventually[0,maintain_duration_s] (course_change_actual_deg >= 30)
# CHK-04 Rule 14/17(c) 不左转: always (avoid_direction == 'right' || encounter == 'crossing_port_target')
```

#### 3.2.4 关键参数

**会遇分类阈值**（Research §12.2）：

```yaml
encounter_classification:
  overtaking:
    relative_bearing_min_deg: 112.5     # COLREGs Rule 13(b) "正横后大于 22.5°"
    relative_bearing_max_deg: 247.5
  head_on:
    relative_bearing_max_deg: 6.0       # ±6° 内
    course_difference_min_deg: 170.0
    course_difference_max_deg: 190.0
  crossing:
    fallback: true                      # 既非追越亦非对遇即为交叉
```

**Rule 17 三阶段时机阈值**（4 水域分级，TBD-S2-02 关闭后）：

| 阶段 | 开阔水域 | 沿岸 (×0.75) | 狭水道 (×0.5) | 港内 (×0.25) |
|------|---------|------------|------------|------------|
| T_give_way_early (min) | 12 (T12) | 9 | 6 | 3 |
| T_give_way (min) | 8 | 6 | 4 | 2 |
| T_stand_on_warn (min) | 6 | 4.5 | 3 | 1.5 |
| T_stand_on_act (min) | 4 | 3 | 2 | 1 |
| T_emergency (min) | 1 | 0.75 | 0.5 | 0.25 |

**港内特例**：`rule17_active=false`（Rule 9 + VTS 指令优先于 Rule 17）。
**狭水道特例**：`rule9_priority=true`（Rule 9 narrow channel 优先于常规 Rule 13-17）。
**依据**：MarinePublic 2026 + COLREGs Rule 9/10 + 工业实操（HAZID 调整压缩比）。

**Rule 18 类型优先序**：

```yaml
rule18_priority:    # 数值越小优先级越高
  NUC: 1            # 失控（Not Under Command）
  RAM: 2            # 受限操纵（Restricted in Ability to Maneuver）
  CBD: 3            # 限吃水（Constrained By Draft，仅适用国际航行）
  FV:  4            # 渔船（Fishing Vessel）
  SV:  5            # 帆船（Sailing Vessel）
  PDV: 6            # 机动船（Power-Driven Vessel）
```

**多目标冲突消解参数**（v1.0 K-means）：

```yaml
multi_target_resolution:
  v1_method: "k_means_priority_serial"
  cluster_count: 3                         # safe / monitoring / danger 三级
  k_means_features: ["threat_score", "tcpa", "bearing_quadrant"]
  v1x_alternatives: ["velocity_obstacle", "mdp_pomdp"]
  doer_checker_required_for_v1x: true
```

**Rule 8 "大幅" 量化**（Wang 2021 + MarinePublic）：

```yaml
rule8_substantial_action:
  course_change_min_deg: 30
  speed_reduction_min_pct: 50
  maintain_duration_min_s: 120              # ≥ 2 min "明显"维持
```

#### 3.2.5 输出契约（含 Doer 决策证书 schema）

```yaml
output:
  decision:
    encounter_type: enum                    # head_on / crossing_give_way / crossing_stand_on / overtaking / safe_passage
    role: enum                              # give_way / stand_on / both_give_way
    action_direction: enum                  # starboard / port / reduce_speed / stop / maintain
    action_magnitude:
      course_change_deg: float              # ≥ 30° if starboard/port
      speed_change_pct: float               # ≥ 50% if reduce_speed
    timing:
      t_start_s: float
      t_maintain_s: float                   # ≥ 120s for Rule 8 "prolonged"
    rule_basis: [string]                    # e.g. ["Rule 15", "Rule 16", "Rule 8"]

  decision_certificate:                     # Jackson 2021 海事适配
    meta:
      timestamp: iso8601
      cycle_id: string
      doer_version: string
    proposed_action: {<同上 decision>}
    safety_evidence:
      applicable_rules: [string]
      encounter_classification: string
      own_role: enum
      rule18_priority_check: object
      cpa_predicted_after_action_m: float
      cpa_safe_required_m: float
      cpa_margin_check: bool                 # cpa_predicted >= cpa_safe * 1.5
      enc_safety_check: bool
      formal_constraint_outputs:
        - rule_id: string                    # CHK-01..05
          passed: bool
          constraint_value: float
    asdr_context:
      input_data_hash: string
      plugin_versions: map<string, string>

  checker_d_result:
    verdict: enum                            # PASS / FAIL
    violations: [string]
    nearest_compliant: {decision}            # only if FAIL
```

#### 3.2.6 ASDR 写入

```yaml
{
  stage: 2,
  timestamp: iso8601,
  cmm_stage: "judge",
  decision: {...},
  decision_certificate: {...},
  encounter_classification: string,
  rule_basis: [...],
  checker_d_verdict: PASS | FAIL,
  input_threat_snapshot_hash: string
}
```

---

### 3.3 Stage 3「动」— 轨迹执行引擎 (Trajectory Execution Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 动 (Act) |
| **控制基础** | Fossen 2021 船舶水动力学 + Eriksen 2020 三层 hybrid 多时间尺度 |
| **触发周期** | 中程 MPC: 0.2 Hz (5s) / 近距 BC-MPC: 1 Hz / Checker-T: 每次新轨迹生成即触发 |
| **骨架职责** | 统一执行接口：避碰决策 + 全局航路 → 可执行轨迹 → Checker-T 校验 → 控制层 L4 |

#### 3.3.1 算法插件（5 个 + Checker-T）

| # | 插件名称 | 算法选型 | 预测时域 | 触发条件 | 依据 |
|---|---------|---------|---------|---------|------|
| 3.1 | **中程 MPC** | Tube MPC + MMG 3-DOF + Yasukawa 2016 高速修正 | 3-5 min | 常规避碰（mode_normal） | Eriksen 2020 *Frontiers in Robotics and AI* 7:11; Yasukawa 2015/2016 |
| 3.2 | **近距 BC-MPC** | Branching-Course MPC（5 course × 3 speed @ L1，3×1 后续）| 25-60 s | mode_warning / mode_emergency | Eriksen 2019 *J. Field Robotics* 36(7):1222 |
| 3.3 | **航点序列生成器** | 转向起始点 → 维持段 → 回归过渡点 → 切入点 4 段式航点链 | — | 每次 MPC 输出后 | COLREGs Rule 8 "明显"; Szlapczynski 2015 *JNAOE* |
| 3.4 | **ENC 安全校验器** | S-57/S-101 电子海图查询 + 浅水/禁区多边形碰撞检测 | — | 航点序列生成后 | IHO S-57/S-101; IHO S-129 UKC; PIANC Report 121 (2014) |
| 3.5 | **船舶动力学插件**（VesselDynamicsPlugin） | MMG 3-DOF（可替换：不同船型注入不同水动力系数）| — | 每次 MPC 预测时调用 | Yasukawa & Yoshimura 2015 *JMST*; Yasukawa 2016 *J. Ship Res.* |
| 3.6 | **时间尺度协调器** | 中程 MPC 输出 waypoint 序列 → BC-MPC reactive tracking | 链路 | 持续运行 | Eriksen 2020 三层 hybrid |
| **Checker-T** | **轨迹安全校验器** | 前向仿真 + ODD 边界验证 + COLREGs 复检 + STL 校验 | 10-15 min | 每次轨迹生成后 | Jackson 2021; Pedersen 2022 STL; Johansen 2016 *MOOS-IvP* |

#### 3.3.2 Checker-T 校验规则（v1.0 5 条 STL）

详见 §4.4 完整规则集（CHK-01 CPA / CHK-02 UKC / CHK-05 禁区 → trajectory 层面）。

#### 3.3.3 多船型扩展机制

- 船舶动力学插件接口在 v1 冻结为 `VesselDynamicsPlugin` 抽象类
- FCB 首发使用 `FCB_MMG_3DOF` 实现（含 Yasukawa 2016 高速修正项）
- 其他船型注入对应水动力系数矩阵即可，**不修改 Stage 3 骨架逻辑**
- FCB 高速段（25-35 节）：Tube MPC 显式建模不确定性 + 可选在线 GP/SI 辨识

#### 3.3.4 关键参数

**Eriksen 2020 三层 hybrid 切换条件**（TBD-S3-03 关闭后）：

```yaml
mpc_layer_authority:
  mode_normal:                  # TCPA > 4 min
    mid_term_priority: 1.0      # 中程 MPC 主导
    bc_mpc_priority: 0.0        # 仅运行不输出
  mode_warning:                 # 2 min ≤ TCPA ≤ 4 min
    mid_term_priority: 0.7
    bc_mpc_priority: 0.3        # BC-MPC 启用作辅助
  mode_emergency:               # TCPA < 2 min 或 CPA < 0.5*CPA_safe
    mid_term_priority: 0.0
    bc_mpc_priority: 1.0        # BC-MPC 主导
    one_way_switch: true        # RTA Simplex 单向切换（参 §3.4.4 / §4.5）
```

**中程 MPC 代价函数权重**（Eriksen 2020 基线作 HAZID 起点；DEGRADED）：

```yaml
mid_term_mpc_cost:
  w_trajectory_deviation: 1.0
  w_course_change: 2.0          # 惩罚不必要转向
  w_speed_change: 1.5
  w_colregs_compliance: 5.0     # COLREGs 违规重罚
  w_cpa_margin: 3.0             # CPA 不足时惩罚
  w_energy_optimization: 0.5    # 与高层能效对齐
```

**BC-MPC 候选机动数**（Eriksen 2019）：

```yaml
bc_mpc:
  trajectory_total_duration_s: 25
  level1_course_branches: 5
  level1_speed_branches: 3
  level_n_course_branches: 3
  level_n_speed_branches: 1
  maneuver_duration_s: 5
  rate_hz: 5                    # BC-MPC 内部高频
```

**Tube MPC 不确定性约束半径**（链路：Stage 1 EKF 协方差 → Tube 半径）：

```yaml
tube_mpc:
  uncertainty_source: "stage1_threats[].predicted_track[].cov_2x2"
  tube_radius_formula: "k_safety * sqrt(trace(cov_2x2) * horizon_steps)"
  k_safety: 1.96                # 95% 置信区间
  min_radius_m: 50              # IMO MSC.192(79) ARPA range error 下限
  fcb_high_speed_multiplier: 1.5
```

**UKC 安全水深**（PIANC Report 121, 2014 + IHO S-129）：

```yaml
ukc_check:
  open_sea_pct_static_draft: 50         # PIANC 远洋
  coastal_pct: 20                       # PIANC 沿岸
  narrow_channel_pct: 10                # PIANC 受限水域
  port_pct: 10                          # PIANC 港内（最低 7%）
  monitoring_threshold_x_draft: 2.5     # 进入 UKC 监控阶段（H < 2.5×T）
  net_ukc_min_static_m: 0.5             # PIANC 2014 net UKC 推荐
  net_ukc_min_rocky_bottom_m: 1.0       # 硬底场景
  squat_correction:
    method: "barrass_1979"               # Barrass 1979 *ISP* 26:44-47
    formula_simplified: "Squat_m = 2 * Cb * V_kn^2 / 100"
    open_sea_blockage: 0.100
    confined_channel_blockage: 0.265
    fcb_high_speed_supercritical_fallback: "[CFD + 拖曳水池 + 实船试航 三段标定]"
    # FCB 高速 25-35 节 + 浅水 H/T<2.5 + Fnh>0.7 时进入 supercritical 区
    # 所有传统 squat 经验公式失效，必须项目级 CFD + 试验数据（DEGRADED）
```

**Waypoint 生成参数**：

```yaml
waypoint_generation:
  spacing_formula: "current_speed_mps * 30"      # 30 节 → 463m / 10 节 → 154m
  spacing_min_m: 100
  spacing_max_m: 1000
  curve_smoothness:
    interpolation: "cubic_spline"
    max_curvature: "1 / (turning_radius * 1.2)"
```

**其他关键参数**（Research §12.2）：

| 参数 | 值 | 依据 |
|------|-----|------|
| 中程 MPC 预测时域 | 3-5 min | Eriksen 2020 |
| 近距 BC-MPC 预测时域 | 25-60 s | Eriksen 2019 |
| 避让维持时间 | ≥ 2 min | Rule 8 "明显" 量化 |
| 转弯半径 | ≥ 旋回半径 × 1.2 | Fossen 2021 + Szlapczynski 2015 |
| 切入角 | ≤ 30° | 工业实操（超过则插入过渡点）|
| 避让最大速度 | ≤ 0.85 × V_max | 保留 15% 推力储备 |

#### 3.3.5 输出契约

```yaml
output:
  trajectory:
    waypoints:
      - position: [lat, lon]
        arrival_time_s: float
        speed_kn: float
        heading_deg: float
        curvature_inv_m: float
    emergency_level: enum                # none / warning / critical
    plugin_versions: {mpc: "v1.0", bc_mpc: "v1.0", dynamics: "FCB_MMG_3DOF_v1.0"}
  checker_t_result:
    verdict: PASS | FAIL
    forward_sim_result:
      min_cpa_next_15min_nm: float
      odd_violation: bool
      grounding_risk: bool
      stl_violations: [string]
    nearest_compliant_trajectory: {trajectory}    # only if FAIL
```

#### 3.3.6 ASDR 写入

```yaml
{
  stage: 3,
  timestamp: iso8601,
  cmm_stage: "act",
  trajectory: {...},
  mpc_cost_function_value: float,
  mpc_layer_active: enum,                # mid_term / bc_mpc / hybrid
  checker_t_verdict: PASS | FAIL,
  forward_sim_summary: {...},
  vessel_dynamics_plugin_id: string
}
```

---

### 3.4 Stage 4「盯」— 闭环监控引擎 (Risk Monitor & Feedback Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 盯 (Monitor) |
| **SA 层级** | Endsley Level 3 (Projection) + 控制反馈环 |
| **运行模式** | **并行监控**（独立于 Stage 2/3 持续运行，非串行末端步骤）|
| **触发周期** | 1 Hz（持续监控）/ 100 ms（紧急通道）|
| **骨架职责** | 持续接收执行效果 → 升级/解除/紧急切换 → 反馈到 Stage 1/2/3 或 ROC |

#### 3.4.1 算法插件（4 个）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 依据 |
|---|---------|---------|------|------|------|
| 4.1 | **CPA 趋势分析器** | 双窗口线性回归（短期 30s + 长期 180s）| 实时 CPA/TCPA 时间序列 | 趋势：improving (>1 m/s) / stable / deteriorating (<-0.5 m/s) | Endsley 1995 L3; MarinePublic 2026 ARPA 工业实操 |
| 4.2 | **升级决策器（DM）** | 阈值触发 + 趋势确认 | CPA 趋势 + TCPA + 4 水域 T_escalate | escalate / no_action | COLREGs Rule 17(b); Wang 2021 *MDPI JMSE* 9(6):584 |
| 4.3 | **解除判定器** | 四条件 AND 门 + 持续确认 ≥ 60s | CPA ≥ CPA_safe + TCPA < 0 + range_rate > 0 + threat_score < 20 | release / maintain_avoidance | COLREGs Rule 8 "明显" + "过去并让清" |
| 4.4 | **紧急通道（反射弧）** | RTA Simplex Architecture（绕过 Doer-Checker 常规路径）| CPA < 0.5×CPA_safe + TCPA < T_emergency | 紧急指令 → 直接下发控制层 + ROC 通知 | NASA Auto-GCAS; Black-Box Simplex arxiv 2102.12981; Seto 2020 *J. Guidance Control Dyn.*; Veitch 2024 *Ocean Engineering* |

#### 3.4.2 并行监控的反馈路径

Stage 4 不是线性流水线的末端，而是**并行运行**于 Stage 2/3 之上：

| 反馈目标 | 触发条件 | 动作 |
|---------|---------|------|
| → **Stage 1** | CPA 趋势恶化（短窗 < -0.5 m/s）| 触发重新感知扫描（新威胁可能漏检） |
| → **Stage 2** | CPA 持续恶化 + TCPA < T_escalate | 触发重新决策（升级避让方案，加大幅度）|
| → **Stage 3** | CPA < 0.5×CPA_safe + TCPA < T_emergency | 紧急通道（绕过常规 Checker，直接下发）|
| → **Mode Manager** | 紧急通道激活 / 连续升级 | 触发模式降级（D4 → D3 → MRC）|
| → **ROC** | 自主能力不足 / 升级无效 | 发出接管请求（TOR, Take-Over Request）|

#### 3.4.3 关键参数

**4 水域 升级 / 紧急 TCPA 阈值**（Research §12.2）：

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| CPA_safe | 1.0 nm | 0.5 nm | 0.3 nm | 0.2 nm |
| T_escalate (升级 TCPA 阈值, min) | 2 | 1.5 | 1 | 0.75 |
| T_emergency (紧急 TCPA 阈值, min) | 1 | 0.75 | 0.5 | 0.33 |

**CPA 趋势分析双窗口**（TBD-S4-01 关闭后）：

```yaml
cpa_trend:
  short_window_s: 30              # 短期反应（≥ 6 个 5s 采样点）
  long_window_s: 180              # 长期趋势（3 min ARPA 工业惯例）
  improvement_slope_threshold_mps: 1.0      # Research §12.2
  deterioration_slope_threshold_mps: -0.5
  high_speed_window_correction_pct: -50     # FCB 高速段窗口缩短 50%
```

**解除判定 4 条件 + 时间窗**：

```yaml
release_check:
  conditions_all_true:                     # AND 门
    - "cpa_actual >= cpa_safe_required"
    - "tcpa < 0"                            # 已过 CPA 时刻
    - "range_rate > 0"                      # 距离持续增大
    - "threat_score < 0.20"                 # 链回 Stage 1 5 档分级 [0,20)=safe
  confirm_duration_s: 60                    # 4 水域统一不分级（船舶动力响应延迟决定）
  monitor_window_after_release_s: 120       # 解除后继续监控（4 水域统一不分级）
```

#### 3.4.4 紧急通道：RTA Simplex Architecture（TBD-S4-07 关闭后）

紧急通道 = 海事 RTA Simplex Architecture，工业先例参 NASA Auto-GCAS（F-16 自动地面避障系统）+ Boeing 777 双控架构 + Black-Box Simplex (arxiv 2102.12981)：

```yaml
emergency_reflex_channel:
  architecture: "simplex"                                  # AC + DM + BC
  components:
    advanced_controller: "stage2_doer + stage3_mid_term_mpc"     # AC = 主决策链路
    decision_module: "stage4_escalation_plugin"                  # DM = 升级决策插件
    baseline_controller: "stage3_bc_mpc"                         # BC = 兜底
    terminal_recovery: "stage3_emergency_full_astern_or_hard_starboard"   # MRC
  trigger:
    cpa_threshold: "cpa_predicted < 0.5 * cpa_safe_required"
    tcpa_threshold: "tcpa < T_emergency"
    one_way_switch: true                                          # RL-RTA 单向切换
  release:
    stage4_release_check: true                                    # 解除判定（§3.4.3）
  cross_layer_routing:                                            # 反射弧绕过 Stage 2 / 3 中段
    bypass: ["stage2_doer", "stage3_mid_term_mpc"]
    direct_to: "L4_control_layer"
    bypass_event_recorded: true                                   # ASDR 强制记录
```

#### 3.4.5 ASDR 写入

```yaml
{
  stage: 4,
  timestamp: iso8601,
  cmm_stage: "monitor",
  cpa_trend: {short_window: float, long_window: float},
  escalation_triggered: bool,
  release_decision: bool,
  emergency_channel_activated: bool,
  bypass_path: [string],                  # 紧急通道时记录绕过的 Stage 列表
  tor_sent: bool,
  rule_basis: [string]
}
```

---

## 4. Doer-Checker 双轨架构（完整双层设计）

相对原始候选 C SVG 图（v1）的关键改进——Checker 从仅校验 Stage 3 轨迹**扩展为双层**（Checker-D + Checker-T），由 NotebookLM 评审确认并体现在 v2 SVG 架构图上。

### 4.1 双层架构图

```
  Stage 2「判」                                Stage 3「动」
  ┌───────────────┐                            ┌───────────────┐
  │  Doer 主决策  │ ── decision certificate ──▶│  中程 MPC     │
  │  (候选方案)   │    (Jackson 2021)          │  近距 BC-MPC  │
  └───────┬───────┘                            └───────┬───────┘
          │                                            │
          ▼                                            ▼
  ┌───────────────┐                            ┌───────────────┐
  │  Checker-D    │                            │  Checker-T    │
  │  决策合规校验 │                            │  轨迹安全校验 │
  │               │                            │               │
  │ · CHK-03      │                            │ · CHK-01 CPA  │
  │   Rule 8 大幅 │                            │ · CHK-02 UKC  │
  │ · CHK-04      │                            │ · CHK-05 禁区 │
  │   Rule 14/17  │                            │ · 前向仿真    │
  │ · Rule 18     │                            │   10-15 min   │
  │   优先序      │                            │ · ODD 边界    │
  │ · 最近合规    │                            │ · 最近合规    │
  │   替代方案    │                            │   替代轨迹    │
  └───────┬───────┘                            └───────┬───────┘
          │                                            │
          └─────────────── PASS/FAIL ──────────────────┘
                              │
                              ▼
                      ┌───────────────┐
                      │ Mode Manager  │ ← 任一层 FAIL 即触发
                      │ (降级 / MRC)  │
                      └───────────────┘
```

### 4.2 级联关系

- **Checker-D PASS** → 决策证书进入 Stage 3 轨迹生成
- **Checker-T PASS** → 轨迹下发控制层 L4
- **任一层 FAIL** → 生成"最近合规替代方案"
- **连续 N 个周期无方案通过**（v1.0 N=3） → Mode Manager 触发 MRC

### 4.3 Checker-D vs Checker-T 职责边界

| 维度 | Checker-D（决策层）| Checker-T（轨迹层）|
|------|------------------|------------------|
| 校验对象 | 避碰决策（方向 / 时机 / 幅度）| 执行轨迹（空间 / 时间 / 动力学）|
| 校验方式 | 形式化规则（一阶谓词逻辑 / STL）| 前向仿真（10-15 min）+ ODD 边界查询 |
| 响应时间 | < 100 ms | < 500 ms |
| 失败后果 | 拒绝决策，要求 Doer 重新生成 | 拒绝轨迹，要求 MPC 重新规划 |
| 连续失败 | 触发降级 | 触发紧急通道 |

### 4.4 完整 STL 规则集（v1.0 基线 5 条 + 后续 STPA workshop 扩展）

**v1.0 基线 5 条 STL（Pedersen 2022 + Jackson 2021 工业范式）**：

```yaml
checker_rules:                          # 全白盒，纯 STL 形式化（不引入 ML）
  CHK-01:                                # Checker-T（轨迹层）
    spec: "always (cpa_predicted_m >= cpa_safe_required_m * 1.5)"
    target: "all targets in threats[]"
    violation_action: "block_and_request_replan"
    layer: "T"

  CHK-02:                                # Checker-T（轨迹层）
    spec: "always (water_depth_predicted_m - draft_dynamic_m >= ukc_threshold_m)"
    description: "UKC 安全水深，链回 §3.3.4 PIANC 标准"
    violation_action: "block"
    layer: "T"

  CHK-03:                                # Checker-D（决策层）
    spec: "eventually[0,maintain_duration_s] (course_change_actual_deg >= 30)"
    description: "Rule 8 大幅 + ≥ 2 min 维持"
    violation_action: "warn"
    layer: "D"

  CHK-04:                                # Checker-D（决策层）
    spec: "always (avoid_direction == 'right' || encounter == 'crossing_port_target' || encounter == 'overtaking')"
    description: "Rule 14 / 17(c) 不向左转规则"
    violation_action: "block"
    layer: "D"

  CHK-05:                                # Checker-T（轨迹层）
    spec: "always !(planned_route intersects forbidden_zone)"
    description: "ENC 禁区 + 浅水多边形"
    violation_action: "block"
    layer: "T"
```

**后续 STPA workshop 扩展（DEGRADED 项 TBD-S3-08）**：v1.0 5 条规则覆盖核心安全约束；完整规则集需 Spec §2.5 STPA workshop 扩展（参 Dugan 2023 *TransNav* 给出避碰 DSS 127 个 UCA 上限）。

### 4.5 Doer-Checker 与 RTA Simplex Architecture 的关系

Doer-Checker 双层（§4）作用于 **mode_normal / mode_warning** 工况（常规决策路径）。
RTA Simplex Architecture（§3.4.4）作用于 **mode_emergency** 工况（紧急通道）。

| 工况 | 主决策路径 | Checker 是否激活 |
|------|----------|----------------|
| mode_normal | Stage 2 Doer → Checker-D → Stage 3 中程 MPC → Checker-T → L4 | 双层激活 |
| mode_warning | 同上 + Stage 3 BC-MPC 启用作辅助（30%）| 双层激活 |
| mode_emergency | RTA 单向切换：Stage 4 DM → Stage 3 BC-MPC → L4（绕过 Stage 2 Doer + 中程 MPC）| **Checker 旁路**（紧急时安全 vs 速度权衡）|

**工业先例**：
- 航空：**NASA Auto-GCAS**（F-16 自动地面避障系统，已部署）
- 航天：**Black-Box Simplex Architecture** (arxiv 2102.12981) 神经网络 + 形式化兜底
- 海军航空：Runtime Assurance (RTA) 输入监视器 + 控制器 / 安全监视器（Seto 2020 *J. Guidance Control Dyn.*）
- 汽车：MIT Certified Control 范式（Jackson D., Newman P. et al. 2021 · arxiv 2104.06178）
- 海事：NTNU + DNV 联合验证中（Eriksen 2020）

---

## 5. Mode Manager 模式管理器

| 属性 | 值 |
|------|-----|
| **位置** | 跨层贯通（独立于 4 Stage，监控所有 Stage 的状态）|
| **实现** | 分层有限状态机（HFSM）|
| **运行周期** | 10 Hz（高优先级）|

### 5.1 模式状态定义

| 模式 | 对应 CCS 标志 | 描述 | 触发条件 |
|------|-------------|------|---------|
| **D2** | N（基础辅助）| 决策辅助模式，船员主导，系统提供预警 + 建议 | 默认 / D3/D4 降级 |
| **D3** | Nx + R1 | 自主航行 + 船员监视 + ROC 备份 | 船长授权 + ODD 满足 |
| **D4** | A1/A2/A3 | 全自主航行（根据 ODD 分级）| 船长授权 + ODD 满足 + 系统自检全部 PASS |
| **MRC** | — | 最小风险态（减速 / 停船 / 锚泊）| Doer-Checker 连续失败 / 通信中断 / 传感器降级 |
| **ROC_Override** | — | ROC 直接控制 | ROC 发起 override 请求 |

### 5.2 模式切换矩阵

```
         ┌──────┐    授权    ┌──────┐   ODD满足   ┌──────┐
         │  D2  │ ─────────▶ │  D3  │ ──────────▶ │  D4  │
         └──────┘            └──────┘             └───┬──┘
              ▲                  ▲                    │
              │                  │                    │ 故障/降级
              │                  │                    ▼
              │ ┌──────────┐     │              ┌──────┐
              └─│   MRC    │◀────┴──────────────│ MRC  │
                └──────────┘   严重故障          └──────┘
                                                     │
                                                     │ ROC 接管
                                                     ▼
                                              ┌──────────────┐
                                              │ ROC_Override │
                                              └──────────────┘
```

### 5.3 ROC 接管时间预算（初始设计基线）

| 类型 | 时间预算 | 学术依据 |
|------|---------|---------|
| 紧急避碰 | ≤ 30 s | Veitch 2024 *Ocean Engineering*（20s 紧急介入窗）|
| 模式切换 | ≤ 60 s | Veitch 2024（60s 监督介入窗）|
| 非紧急监督 | ≤ 5 min | 行业默认实践 |

> 注意：船级社不发布固定秒级硬限。上表数值为项目级初始设计基线，最终值在 CONOPS 中经 HAZID 冻结。

### 5.4 Mode Manager 与 Stage 4 的协作

Stage 4「盯」紧急通道激活后：
1. Stage 4 → Mode Manager 发降级请求
2. Mode Manager 根据严重度选择 D4 → D3 / D3 → MRC / MRC → ROC_Override
3. 模式切换记录写入 ASDR（永久保留，不参与 30 天循环）

---

## 6. ASDR (决策审计记录) 全链路设计

每个 Stage 向 ASDR 显式写入决策依据链记录，形成完整的可追溯链路。源于 Research v1.2 §6.12.2 + IACS UR E27 Rev.1 网络安全审计要求。

### 6.1 各 Stage 写入内容

| Stage | 写入事件 | 记录内容 | 保留策略 |
|-------|---------|---------|---------|
| **Stage 1** | `situational_snapshot` | 威胁列表快照 + 本船状态 + 传感器健康 + 时间戳 | ≥ 30 天循环 |
| **Stage 2** | `decision_rationale` | 会遇分类 + 责任判定 + 规则依据 + Checker-D 裁决 + 候选方案 + 决策证书 | ≥ 30 天循环 |
| **Stage 3** | `trajectory_rationale` | 生成航点序列 + 代价函数值 + Checker-T 裁决 + 前向仿真摘要 + 动力学插件 ID | ≥ 30 天循环 |
| **Stage 4** | `monitor_event` | CPA 趋势 + 升级/解除触发 + 紧急通道激活 + TOR 发出 + bypass 路径 | ≥ 30 天循环 |
| **Mode Manager** | `mode_transition` | 旧模式 → 新模式 + 触发原因 + 时间戳 | **永久保留** |

### 6.2 ASDR 4 元组事件 Schema

```yaml
asdr_event:
  event_id: uuid
  stage: enum[1, 2, 3, 4, mode_manager]
  timestamp: iso8601
  cmm_stage: enum[observe, judge, act, monitor]

  # ----- 4 元组核心字段 -----
  decision_4_tuple:
    # ① 规则条款
    applicable_rules: [string]              # e.g. ["Rule 15", "Rule 16", "Rule 8"]

    # ② 输入快照
    input_snapshot:
      threat_list: [...]                    # Stage 1 输出哈希或完整记录
      own_ship: {...}
      water_zone: enum                      # open_sea / coastal / narrow_channel / port
      visibility_nm: float
      sensor_health: {...}

    # ③ 决策路径
    decision_path:
      encounter_class: string               # head_on / crossing / overtaking ...
      responsibility: string                # give_way / stand_on / both
      action: string                        # 30° starboard, maintain ≥ 2 min
      expected_cpa_nm: float
      mpc_layer_used: enum                  # mid_term / bc_mpc / hybrid
      checker_d_verdict: PASS | FAIL
      checker_t_verdict: PASS | FAIL

    # ④ 预期 vs 实际
    outcome_comparison:
      predicted_cpa_nm: float
      actual_cpa_nm: float                   # 链回 Stage 4 监控
      deviation_nm: float
      stl_violations_actual: [string]

  # ----- 元数据 -----
  source_evidence: [string]                  # 上游证据引用，e.g. ["Eriksen 2020", "Wang 2021 Rule 17"]
  plugin_versions: map<string, string>
  asdr_signature: string                     # 加密签名（防篡改）
```

### 6.3 ASDR 与 IACS UR E27 的关系

| 维度 | IACS UR E27 事件记录 | ASDR 决策审计 |
|------|---------------------|--------------|
| 目的 | 网络入侵审计 | 决策审计 |
| 核心问题 | Who attacked? When? What was accessed? | Why this decision? On what evidence? Predicted vs actual? |
| 持久化 | 加密日志 | 加密日志（共用基础设施）|
| Schema | 网络事件 | 决策事件（4 元组）|
| 保留策略 | ≥ 30 天循环 | ≥ 30 天循环（模式切换永久）|

### 6.4 ASDR 与 Stage 2 决策证书的关系

Stage 2 输出的 **决策证书**（Jackson 2021 Certified Control schema，见 §3.2.5）是 ASDR 4 元组的**部分输入**：
- 决策证书的 `applicable_rules` → ASDR ① 规则条款
- 决策证书的 `safety_evidence.proposed_action` → ASDR ③ 决策路径
- 决策证书的 `formal_constraint_outputs` → ASDR ③ Checker 裁决
- ASDR 在决策证书基础上增加 ② 输入快照（Stage 1 上游）+ ④ 预期/实际对比（Stage 4 下游）

---

## 7. 接口契约汇总

### 7.1 接口列表

| 接口 | 方向 | 数据内容 | 协议 | 频率 |
|------|------|---------|------|------|
| 感知融合 → Stage 1 | 入 | 目标列表（Radar/AIS/LiDAR/Camera 融合）+ ENC + 本船状态 | DDS / ROS 2 | 1-10 Hz |
| 战略层 L2 → Stage 3 | 入 | 全局航路点 + 时序约束 + 优先级 | DDS | 按需（航路更新）|
| ROC ↔ Mode Manager | 双向 | 模式切换请求 / 确认 + override 命令 + 心跳 | TCP/TLS | 1 Hz（心跳）|
| Stage 1 → Stage 2 | 内部 | 威胁列表（§3.1.3 schema） | 内部总线 | 1-5 Hz |
| Stage 2 → Stage 3 | 内部（经 Checker-D）| 决策证书（§3.2.5 schema）| 内部总线 | 1 Hz |
| Stage 3 → 控制层 L4 | 出 | 参考航向 + 航速 + 紧急级别（§3.3.5 schema）| DDS / CAN | 10 Hz |
| Stage 3 → 控制层（紧急通道）| 出（反射弧）| BC-MPC 直接输出 + emergency_level=critical | DDS（高优先级） | 10 Hz（绕过 Checker）|
| 所有 Stage → ASDR | 出 | 决策审计事件（§6.2 schema） | 内部总线 → 加密持久化 | 事件驱动 |
| Stage 4 → Stage 1/2/3 反馈 | 反向 | 升级触发信号 / 重新感知请求 | 内部总线 | 事件驱动 |
| 公共服务 | 出 | 日志 + 健康监测 + 告警上报 | syslog + DDS | 持续 |

### 7.2 接口冻结策略

- **v1.0 冻结**：Stage 1/2/3/4 输入输出 schema + Decision Certificate schema + ASDR schema
- **v1.x 可演化**：内部插件接口（VesselDynamicsPlugin / Stage1Plugin 等）可演化（v1 → v1.1 → v2）
- **v2 重大变更**：仅当 CCS 重新评估时启用（多船型升级 / D2-D4 等级提升）

---

## 8. 部署与运行环境

| 维度 | 规格 |
|------|------|
| **计算机系统等级** | CCS III 类 CBS（最高等级）|
| **SIL 候选** | 主决策路径 SIL 2-3；辅助子模块 SIL 1-2（待 HAZID 冻结）|
| **网络安全** | IACS UR E26 / E27 Rev.1 + CCS《船舶网络安全指南》(2024) |
| **冗余** | 双路电源 + 热备份控制器（基于 Boeing 777 双控范式）|
| **操作系统** | 实时 Linux (PREEMPT_RT) 或 VxWorks |
| **中间件** | DDS（RTI Connext / eProsima Fast DDS）|
| **开发语言** | 骨架层：C++17；插件层：C++17 / Python 3（ML 插件）|
| **工具链** | CasADi + IPOPT（MPC）/ Eigen（线性代数）/ CGAL（几何计算）/ Boost STL（形式化校验）|

---

## 9. 与 CCS 智能系统认可的对接矩阵

| CCS 要求 | L3-TDCAL 应对 | 证据材料 |
|---------|-------------|---------|
| 智能系统独立认可 | 4 Stage + Mode Manager 各自独立，接口明确冻结 | 各 Stage 的输入 / 输出契约 YAML schema |
| III 类 CBS 网络安全 | 骨架层纯白盒，插件层 ML 边界清晰（AD-04）| 网络安全测试报告 + IACS UR E27 合规声明 |
| 决策可解释性 | CMM 4 阶段语义 + 决策 4 元组记录 + ASDR | 决策依据链重放（任意时间窗）|
| 场景感知 + 自主决策 | Stage 1（感知）+ Stage 2（决策）独立评估 | 各 Stage 独立 V&V 测试报告 |
| 软件可靠性 | 骨架与插件分离，骨架无 ML，Checker 确定性校验 | CCS《船用软件安全及可靠性评估指南》合规声明 |
| 三段式 V&V | MIL → SIL → HIL → Sea Trial | 每阶段测试覆盖矩阵 + STPA UCA 矩阵 |
| 多船型扩展 | VesselDynamicsPlugin 抽象接口冻结 | FCB → 其他船型注入水动力系数即可 |
| D2-D4 全谱 + Fallback | Mode Manager 5 模式 + RTA Simplex 紧急通道 | Mode Manager HFSM 测试 + 紧急通道演练记录 |

---

## 10. CMM 形式化映射（双向追溯表）

| Spec ADR | 主要 CMM Stage | 上游证据 | 关闭后状态 |
|---------|--------------|---------|-----------|
| AD-02 多时间尺度混合 MPC + BC-MPC | Stage 2 + Stage 3 | Eriksen 2020 *Frontiers in Robotics and AI* 7:11; COLREGs Rule 8 / 13-17 | CLOSED |
| AD-04 主决策白盒 + ML 边界 | Stage 2 | CCS《船用软件安全及可靠性评估指南》; Hareide-Veitch 2024 transparency | CLOSED |
| AD-15 COLREGs 9 条规则形式化 | Stage 2 | IMO COLREGs Rule 5-19; Wang 2021 *MDPI JMSE* 9(6):584 | CLOSED |
| AD-09 MRC 三模式 | Stage 4 | COLREGs Rule 17(b); Veitch 2024 *Ocean Engineering* | CLOSED |
| AD-23 SIL 2-3 候选 | 全部 4 Stage | IEC 61508-3; Park & Kim 2024 | DEGRADED（HAZID 冻结）|
| AD-25a STPA + Software FMEA + BN | 全部 4 Stage | STPA Handbook 2018; Dugan 2023 *TransNav* | CLOSED（v1.0 5 条 STL；扩展待 STPA workshop）|
| AD-04a CMM 横向锚定 | Stage 1-4 自身 | Endsley 1995; Klein 1998 RPD; 控制论 | CLOSED |
| AD-25f Doer-Checker 双轨 | Stage 2 + Stage 3 + Stage 4 | Jackson 2021 MIT CSAIL; Boeing 777; NASA RTA | CLOSED |
| AD-19a ASDR 决策审计 | 全部 4 Stage | Research §6.12; IACS UR E27 Rev.1 | CLOSED |
| AD-26 船舶动力学插件化 | Stage 3 | Yasukawa 2015/2016; Fossen 2021 | CLOSED |
| AD-03 整体架构候选 C | 全部 4 Stage | NotebookLM 评审 + nlm-research 独立验证 | CLOSED |

---

## 11. v1.0 已关闭 TBD 状态总览

v1.1 在 v1.0 草案基础上完成全部 34 项 TBD 调研关闭。证据级别按 E1（IMO/IHO 公约）/ E2（PIANC 工程规范）/ E3（船级社/工业实操）/ E4（peer-reviewed 学术）四级评定。

### 11.1 总览统计

| Stage | TBD 数 | CLOSED | DEGRADED | BLOCKING | 备注 |
|-------|--------|--------|----------|----------|------|
| Stage 1「看」 | 7 | 4 | 3 | 0 | EKF 协方差 / 步长锁定到 IMO MSC.192(79)；权重 / 高频流水线 / ENC 接入降级为项目级假设 |
| Stage 2「判」 | 8 | 7 | 1 | 0 | COLREGs 形式化阈值 + Doer 决策证书 schema 全锁定；Rule 17 4 水域压缩比降级 |
| Stage 3「动」 | 12 | 9 | 3 | 0 | Eriksen 2020 三层 hybrid + UKC 标准 + waypoint spacing 全锁定；MPC 权重 / FCB 高速水动力 / Checker 完整规则集降级 |
| Stage 4「盯」 | 7 | 7 | 0 | 0 | RTA Simplex Architecture + 4 水域 TCPA 阈值 + 解除条件全锁定 |
| **合计** | **34** | **27** | **7** | **0** | **所有 BLOCKING 在工业惯例层关闭** |

### 11.2 Stage 1「看」TBD 关闭明细

| ID | 内容 | 状态 | 关闭依据 |
|----|------|------|---------|
| TBD-S1-01 | FCB 高速段 bearing_rate 修正系数 | CLOSED | 工业惯例按距离 + 目标尺寸双因子修正（不按船速）；Cockcroft 7th Ed |
| TBD-S1-02 | 4 水域 bearing_rate 阈值 | CLOSED | 业界统一 0.05°/s 默认（不水域分级，差异由 CPA_safe 4 档承担）|
| TBD-S1-03 | 威胁评分加权和 4 个权重 | DEGRADED | Nguyen 2018 *Algorithms* 11(12):204 AHP 起点；HAZID workshop 冻结 |
| TBD-S1-04 | EKF 协方差初值 | CLOSED | IMO MSC.192(79) §5.25.4.7.1 ARPA 性能标准（95% 概率）|
| TBD-S1-05 | prediction.step_s 步长 | CLOSED | Eriksen 2019 BC-MPC + Eriksen 2020 中程 MPC 自洽（10s）|
| TBD-S1-06 | 高频流水线 | DEGRADED | 业界共识感知层负责异步融合；项目级感知层 SDD 验证 |
| TBD-S1-07 | water_zone 数据源 | DEGRADED | IHO S-57 / S-101 ENC 工业标准；项目级 ECDIS 接入待 SDD |

### 11.3 Stage 2「判」TBD 关闭明细

| ID | 内容 | 状态 | 关闭依据 |
|----|------|------|---------|
| TBD-S2-01 | COLREGs 形式化阈值 | CLOSED | Research §12.2 + IMO COLREGs Rule 13(b) |
| TBD-S2-02 | Rule 17 三阶段时机 4 水域分级 | DEGRADED | 4 水域压缩比 0.75/0.5/0.25 工程化基线；HAZID + CCS 验船师评审 |
| TBD-S2-03 | Rule 18 类型优先序 | CLOSED | COLREGs Rule 18 直接编码 |
| TBD-S2-04 | 多目标冲突消解算法 | CLOSED | v1.0 K-means + 威胁排序 + 单目标处理；v1.x VO/MDP 备选 |
| TBD-S2-05 | Doer 决策证书 schema | CLOSED | Jackson 2021 MIT CSAIL Certified Control 海事适配 |
| TBD-S2-06 | Rule 8 "大幅" 量化 | CLOSED | Research §12.2 + Wang 2021 + MarinePublic |
| TBD-S2-07 | head-on 双方让路 | CLOSED | COLREGs Rule 14 直接编码 |
| TBD-S2-08 | narrow channel 通行 | CLOSED | COLREGs Rule 9/10 直接编码 |

### 11.4 Stage 3「动」TBD 关闭明细

| ID | 内容 | 状态 | 关闭依据 |
|----|------|------|---------|
| TBD-S3-01 | 中程 MPC 代价函数权重 | DEGRADED | Eriksen 2020 基线作 HAZID workshop 起点 |
| TBD-S3-02 | BC-MPC 候选机动数 | CLOSED | Eriksen 2019 *J. Field Robotics* 36(7):1222 |
| TBD-S3-03 | 中程 MPC vs BC-MPC 切换 | CLOSED | Eriksen 2020 三层 hybrid 并行 + RTA Simplex 单向切换 |
| TBD-S3-04 | Tube MPC 不确定性约束半径 | CLOSED | KTH 2019 Robust MPC + Stage 1 EKF 协方差链路 |
| TBD-S3-05 | FCB 高速段水动力系数 | DEGRADED | Yasukawa 2015/2016 学术基础锁；CFD + 拖曳水池 + 实船试航三段标定 |
| TBD-S3-06 | 转向半径 | CLOSED | Research §12.2（≥ 旋回半径 × 1.2）|
| TBD-S3-07 | 切入角 | CLOSED | Research §12.2（≤ 30°）|
| TBD-S3-08 | Checker 形式化规则集 | DEGRADED | v1.0 5 条 STL 基线；STPA workshop 扩展（Dugan 2023 127 UCA 上限）|
| TBD-S3-09 | 浅水 UKC 阈值 | CLOSED | PIANC Report 121 (2014) + IHO S-129 + Barrass 1979 squat |
| TBD-S3-10 | GP/SI 启用条件 | CLOSED | v1.0 不启用；v1.x EKF 残差超阈值时启用，必须经 Checker |
| TBD-S3-11 | 时间尺度协调器接口 | CLOSED | Eriksen 2020 hybrid 工业范式（中程 → BC-MPC waypoint 序列）|
| TBD-S3-12 | waypoint spacing | CLOSED | V × 30s 业界惯例 + cubic_spline 插值 |

### 11.5 Stage 4「盯」TBD 关闭明细

| ID | 内容 | 状态 | 关闭依据 |
|----|------|------|---------|
| TBD-S4-01 | CPA 趋势线性回归窗口 | CLOSED | 双窗口 30s + 180s（短期 + 长期）；MarinePublic 2026 ARPA 工业惯例 |
| TBD-S4-02 | 4 水域 升级/紧急 TCPA 阈值 | CLOSED | Research §12.2 4 档已锁 |
| TBD-S4-03 | 解除条件 4 阈值 | CLOSED | Research §12.2 4 阈值已锁；与 Stage 1 5 档分级自洽 |
| TBD-S4-04 | 解除持续时间 60s 是否水域分级 | CLOSED | 统一 60s（船舶动力响应延迟与水域无关）|
| TBD-S4-05 | 监控窗口 120s 是否水域分级 | CLOSED | 统一 120s（同上）|
| TBD-S4-06 | 紧急通道触发条件 | CLOSED | Research §12.2 已锁（CPA<0.5×CPA_safe + TCPA<T_emergency）|
| TBD-S4-07 | 反射弧式触发模式 | CLOSED | RTA Simplex Architecture（NASA Auto-GCAS / Black-Box Simplex 工业先例）|

### 11.6 跨 Stage 一致性核查

✓ Stage 1 → Stage 2：威胁评分（PL-S1-04）→ K-means 聚类（TBD-S2-04）→ Doer 主决策
✓ Stage 2 → Stage 3：决策证书（TBD-S2-05）→ Checker-D（CHK-03/04）→ 中程 MPC（TBD-S3-01）→ Checker-T（CHK-01/02/05）
✓ Stage 3 内部协调：中程 MPC ↔ BC-MPC 时间尺度协调器（TBD-S3-11）+ 切换条件（TBD-S3-03）
✓ Stage 4 → 跨层：紧急通道（TBD-S4-07 RTA Simplex）= 绕过 Stage 2 Doer + Stage 3 中程 MPC，直接 BC-MPC → L4
✓ Doer-Checker 横向 ADR（AD-25f）：Stage 2 Doer + Stage 3 Checker（CHK-01..05 STL 规则）+ Stage 4 监控形成完整运行时安全闭环
✓ CMM 4 阶段 + 算法插件 + 横向 ADR 三者契合：候选 C 双层抽象保持，所有 TBD 关闭后无新冲突

---

## 12. v1.0 残留 DEGRADED 项目级假设条款

下表 7 项假设是 v1.0 设计基线，**不阻塞 Spec v1.0 设计的展开**，但需要在项目级路径上验证 / 冻结。任一项失效都不破坏候选 C 架构（骨架稳定 + 插件可替换特征保持）。

### 12.1 假设清单

| ID | 假设内容 | 验证路径 | 失效后处理 |
|----|---------|---------|-----------|
| **A-01** (TBD-S1-03) | 威胁评分权重采用 Nguyen 2018 AHP 因子映射后初值（0.20/0.45/0.25/0.10）；5 档 CRI 阈值 | HAZID workshop（CCS 验船师 + 船长 + 引航员）| 调权重和阈值；架构 + 插件接口不动 |
| **A-02** (TBD-S1-06) | 感知融合层负责异步多速率融合 + 输出 1-5 Hz 统一速率，Stage 1 不做单独高频流水线 | 感知层 SDD 出来后验证 | Stage 1 补 200-500ms 高频流水线（一个新 Stage1Plugin）|
| **A-03** (TBD-S1-07) | 可调用 ECDIS 或本地 ENC 缓存查询 S-57/S-101 水域分类 | 感知层 + 战略层 SDD 出来后定具体接入 | 全按 `open_sea` 处理 + 标降级标志（Mode Manager 自动降级到 D3）|
| **A-04** (TBD-S2-02) | Rule 17 三阶段时机 4 水域压缩比 0.75/0.5/0.25 | HAZID + CCS 验船师评审 | 调压缩比；阈值表参数化 |
| **A-05** (TBD-S3-01) | 中程 MPC 代价函数权重采用 Eriksen 2020 基线 | HAZID workshop | 调权重；MPC 实现不动 |
| **A-06** (TBD-S3-05) | FCB 高速段（25-35 节）+ 浅水段（H/T<2.5）水动力系数走 Yasukawa 2016 修正；supercritical 区（Fnh>1）需 CFD + 拖曳水池 + 实船试航三段标定 | 项目级 CFD 仿真（ANSYS CFX/OpenFOAM）+ 模型试验（PMM）+ 实船试航 squat 实测 | 调系数；AD-26 VesselDynamicsPlugin 接口不动 |
| **A-07** (TBD-S3-08) | Checker 完整规则集采用 v1.0 5 条 STL 基线；后续按 STPA workshop 扩展（Dugan 2023 127 UCA 上限作参考）| Spec §2.5 STPA workshop | 规则集扩展；Checker 框架不动 |

### 12.2 风险等级与影响范围

| ID | 项目级风险等级 | 影响范围 | 触发模式降级 |
|----|--------------|---------|-------------|
| A-01 | 中 | Stage 1 威胁评分 → 影响 Stage 2 时机 | 否 |
| A-02 | 高（感知层 SDD 未出）| Stage 1 输入速率 | 否（除非感知层无法异步融合）|
| A-03 | 高（战略层 SDD 未出）| Stage 1 + Stage 3 ENC 查询 | 是（无 ENC 时降级到 D3）|
| A-04 | 中 | Stage 2 时机判定 | 否 |
| A-05 | 中 | Stage 3 中程 MPC 输出质量 | 否 |
| A-06 | **高**（FCB 高速段是首发船型核心）| Stage 3 BC-MPC 仿真精度 | 否（影响 Sea Trial 阶段）|
| A-07 | 中 | Checker 校验完整性 | 是（FAIL 时触发 MRC）|

### 12.3 假设的 v1.x 演化路径

| ID | v1.0 状态 | v1.1 演化 | v2.0 目标 |
|----|----------|----------|----------|
| A-01 | Nguyen AHP 起点 | HAZID 调权重 | 多船型差异化权重 |
| A-02 | 假设感知层异步 | 感知层 SDD 出后验证 | 跨层异步性能优化 |
| A-03 | 假设 ECDIS 可用 | SDD 后定具体接入 | S-101 + S-129 全集成 |
| A-04 | 4 水域压缩比 0.75/0.5/0.25 | HAZID 调压缩比 | 港内 VTS 集成 |
| A-05 | Eriksen 基线 | HAZID 调权重 | 多船型差异化代价函数 |
| A-06 | Yasukawa 2016 学术 | CFD + 模型试验 | 实船试航 + 在线 GP/SI |
| A-07 | 5 条 STL 基线 | STPA workshop 扩展 | UCA 完整覆盖（≤ 127）|

---

## 13. 后续 Spec 阶段待决策项（融合 NotebookLM TBD 表）

| ID | 内容 | 依赖 | 关联 §12 假设 |
|----|------|------|--------------|
| TBD-14 | 模块结构最终选型确认（v1.1 已采纳候选 C；待 HAZID 冻结）| Spec §2.1 HAZID workshop | — |
| TBD-15 | CMM 与各模块的绑定方式（v1.1 已明确：骨架 = CMM 语义边界）| TBD-14 闭合后 | — |
| TBD-20 | Stage 间接口的 Protobuf / IDL 详细定义 | Spec §3 | — |
| TBD-21 | Checker-D 和 Checker-T 的具体 STL 规则集（v1.1 已给 5 条基线）| Spec §2.5 STPA workshop | A-07 |
| TBD-22 | ASDR 持久化策略与加密方案 | Spec §2.5 + 网络安全评估 | — |
| TBD-23 | 各 Stage 的 SIL 最终值（STPA → FMEA → BN → 量化）| HAZID workshop + CCS 验船师确认 | — |
| TBD-24 | 紧急 / 监督 / 非紧急三档接管时间预算的最终值 | CONOPS + HAZID | — |
| TBD-25 | DCPA / TCPA 触发阈值的最终值（v1.1 已给 4 水域基线）| HAZID workshop | A-01, A-04 |
| TBD-26 | MRC 的具体定义（减速 / 锚泊 / 漂泊）的最终选择 | CONOPS | — |
| TBD-27 (新增) | A-06 FCB 高速段水动力系数标定方案 | CFD + 拖曳水池 + 实船试航 | A-06 |
| TBD-28 (新增) | A-02 感知层异步融合接口验证 | 感知层 SDD 出来后 | A-02 |
| TBD-29 (新增) | A-03 ECDIS / ENC 接入方案 | 感知层 + 战略层 SDD | A-03 |

---

## 14. 引用证据库

按证据级别（E1 IMO/IHO 公约 / E2 PIANC 工程规范 / E3 船级社/工业实操 / E4 peer-reviewed 学术）整理。

### 14.1 E1 — IMO/IHO/ITU 公约与标准

- **IMO COLREGs 1972**（含 1981/1987/2003/2007 修订版）— Convention on the International Regulations for Preventing Collisions at Sea. Rule 5-19 全集
- **IMO MSC.192(79)** (2004) — Adoption of the Revised Performance Standards for Radar Equipment. §5.25.4.7.1 ARPA 跟踪精度（95% 概率：range ≤ 50m 或 ±1% target range；bearing ≤ 2°）
- **IMO Resolution MSC.74(69) Annex 3** — AIS Performance Standards
- **IHO S-57 / S-101** — Electronic Navigational Chart Product Specification（含 TSSLPT/TSSBND/TSELNE/TSEZNE/ISTZNE/PRCARE/DWRTPT/TWRTPT 8 类对象）
- **IHO S-129** — Under Keel Clearance Management
- **ITU-R M.1371-5** — Technical characteristics for an automatic identification system using time-division multiple access in the VHF maritime mobile band

### 14.2 E2 — PIANC 工程规范

- **PIANC Report 121** (2014) — Harbour Approach Channels Design Guidelines（UKC 管理 + net UKC 0.5m / 硬底 1.0m）
- **PIANC** (1997) — Approach Channels: A Guide for Design（推荐 ICORELS 1980 + Barrass 1981 + Eryuzlu 1994 三种 squat 公式）

### 14.3 E3 — 船级社 / 工业实操

- **CCS《智能船舶规范》(2025)** — 智能系统认可要求
- **CCS《船舶网络安全指南》(2024)**
- **CCS《船用软件安全及可靠性评估指南》**
- **IACS UR E26 / E27 Rev.1** — 网络安全统一要求
- **NOAA ENC Design Handbook** (2024-06) — S-57 + S-101 双格式过渡（2026 起）
- **IALA Guideline 1082 Ed 2.0** (2016) — AIS Overview
- **Cockcroft & Lameijer 7th Ed** (2017) — A Guide to Collision Avoidance Rules（5° 方位变化 in 2 nm 仍 close quarter 等工业实操经验）
- **MarinePublic** (2026) — Rule 17 三阶段 + close-quarter 阈值（CPA<0.5nm 或 TCPA<12min）+ ARPA 工业实操
- **NASA/TM-20220015734** (2022) — Runtime Assurance of Aeronautical Systems（Auto-GCAS 等 RTA 实战部署）
- **Barrass C.B.** (1979) — The Phenomenon of Ship Squat. *International Shipbuilding Progress* 26:44-47
- **STPA Handbook** (Leveson & Thomas 2018)

### 14.4 E4 — Peer-reviewed 学术

#### 架构与决策框架
- **Jackson D., Richmond V., Wang M., Chow J., Guajardo U., Kong S., Campos S., Litt G., Arechiga N.** (2021). "Certified Control: An Architecture for Verifiable Safety of Autonomous Vehicles." MIT CSAIL · arxiv 2104.06178 ⬅ Doer-Checker 决策证书 schema 核心引用
- **Eriksen B.-O.H., Bitar G., Breivik M., Lekkas A.M.** (2020). "Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17." *Frontiers in Robotics and AI* 7:11. **DOI: 10.3389/frobt.2020.00011** ⬅ 三层 hybrid 核心引用
- **Eriksen B.-O.H., Bitar G., Breivik M., Lekkas A.M.** (2019). "The branching-course model predictive control algorithm for maritime collision avoidance." *Journal of Field Robotics* 36(7):1222-1249. **DOI: 10.1002/rob.21900** ⬅ BC-MPC 核心引用
- **Seto Y. et al.** (2020). "Runtime Assurance for Autonomous Aerospace Systems." *Journal of Guidance, Control, and Dynamics*
- **Black-Box Simplex Architecture** (2021) arxiv 2102.12981
- **Pedersen** (2022) "STL Robustness Metric for Maritime CAS." *J. Phys. Conf. Ser.* 2618

#### COLREGs 形式化与决策
- **Wang J.** (2021). "Quantitative Analysis of COLREGs for Autonomous Maritime Surface Ships." *MDPI JMSE* 9(6):584 ⬅ Rule 17 直航船 4 阶段 + Rule 8 大幅量化
- **Nguyen M., Zhang S., Wang X.** (2018). "A Novel Method for Risk Assessment and Simulation of Collision Avoidance for Vessels based on AIS." *Algorithms* 11(12):204. **DOI: 10.3390/a11120204** ⬅ AHP 权重 0.5/0.3/0.1/0.1 核心引用
- **Dugan** (2023). "Collision Avoidance DSS UCA Analysis." *TransNav* ⬅ STPA UCA 上限参考（127）

#### 跟踪与感知
- **Bar-Shalom Y.** (1988). "Tracking and Data Association." Academic Press（IMM 框架）
- **Yang J. et al.** (2025). "Research on ship target tracking with multi-sensor data fusion." *Sensors and Actuators*
- **Adaptive UKF for target tracking with time-varying noise covariance** (2021). *Sensors* 21(17):5808. DOI: 10.3390/s21175808

#### 船舶动力学
- **Yasukawa H., Yoshimura Y.** (2015). "Introduction of MMG standard method for ship maneuvering predictions." *Journal of Marine Science and Technology* 20(1):37-52
- **Yasukawa H. et al.** (2016). "High-speed ship maneuverability." *Journal of Ship Research* 60(4):239-258
- **Fossen T.I.** (2021). "Handbook of Marine Craft Hydrodynamics and Motion Control." 2nd Ed. Wiley

#### 人机协同与情境感知
- **Endsley M.R.** (1995). "Toward a theory of situation awareness in dynamic systems." *Human Factors* 37(1):32-64 ⬅ SA L1/L2/L3 模型
- **Klein G.** (1998). "Sources of Power: How People Make Decisions." MIT Press（RPD 模型）
- **Sharma A. et al.** (2019). "Situation Awareness in Maritime Navigation: GDTA Application." 
- **Hareide O.S., Veitch E.** (2024). "Transparency in Maritime Autonomous Systems." *Ocean Engineering*
- **Veitch E.** (2024). "Take-over time and operator interaction in maritime autonomous surface ships." *Ocean Engineering*
- **Park S., Kim J.** (2024). "SIL allocation for autonomous ship systems."

#### 路径规划与水动力建模
- **Szlapczynski R., Krata P., Szlapczynska J.** (2018). "Ship domain applied to determining distances for collision avoidance manoeuvres in give-way situations." *Ocean Engineering* 165:43-54
- **Szlapczynski R.** (2015). "Evolutionary planning of safe ship tracks in restricted waters." *Journal of Navigation and Aerospace Engineering* (JNAOE)
- **Xue Y. et al.** (2022). "GP-based vessel motion prediction." *Ocean Engineering*
- **Reza et al.** (2025). "Digital twin syncing for ASV using RL + NMPC." *Scientific Reports*
- **KTH Master Thesis** (2019) — Robust Model Predictive Control for Marine Vessels（Tube MPC 约束半径公式）
- **Johansen T.A.** (2016). "Ship Collision Avoidance and COLREGS Compliance Using Simulation-Based Control Behavior Selection With Predictive Hazard Assessment." *IEEE Trans. Intell. Transp. Syst.* 17(12):3407-3422

#### 多目标决策
- **Kochenderfer M.J.** (2017). "Markov Decision Processes for autonomous collision avoidance"
- **Frontiers MS** (2025-12). "COLREGs-compliant ship collision avoidance based on PPO"
- **ScienceDirect** (2025-11). "MASS busy waterways DRL + K-means"

---

## 15. 变更历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|---------|
| v1.0 | 2026-04-28 | 用户 + Claude | 初稿：基于 Research v1.2 + nlm 笔记本 + nlm-research 独立验证 + 候选 C SVG 三处优化（Stage 4 并行监控 / Checker 双层校验 / ASDR 全链路写入）。采纳候选 C 心智骨架 + 算法插件混合架构，定义 L3-TDCAL 完整模块设计 |
| **v1.1** | **2026-04-28** | **用户 + Claude** | **基于 v1.0 增量：(1) 完成全部 34 项 TBD 调研关闭（27 CLOSED + 7 DEGRADED + 0 BLOCKING）；(2) 各 Stage 关键参数从"初始基线"细化为具体引用值（IMO MSC.192(79) / Eriksen 2020 / Nguyen 2018 / PIANC 2014 等）；(3) 新增 §11 已关闭 TBD 状态总览 + §12 残留 7 项 DEGRADED 项目级假设条款 + §14 完整引用证据库；(4) 架构图升级到 v2 SVG（含 Stage 4 反向反馈 + 反射弧 + Checker 跨层 + ASDR 全 Stage 写入）；(5) 补充 RTA Simplex Architecture 详细配置；(6) 补充 Checker 5 条 STL 规则（CHK-01..05）;(7) Doer 决策证书 schema 完整字段; (8) 跨 Stage 一致性核查通过** |

---

*本架构设计规约 v1.1 基于 Research v1.2 (2026-04-28 冻结) + nlm 本地笔记本 34 篇来源 + nlm-research 36 篇独立验证 + 候选 C SVG 图的三处优化 + 全部 34 项 TBD 调研关闭。所有决策均锚定到 Endsley 1995 / Eriksen 2020 / Jackson 2021 / Nguyen 2018 / COLREGs 1972 / IMO MSC.192(79) / PIANC 2014 / IHO S-129 / CCS《智能船舶规范》(2025) / IACS UR E26/E27 Rev.1 的证据链。*
