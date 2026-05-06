# L3 战术决策与避碰层 — 架构设计规约 (Architecture Design Specification)

**基于候选 C：心智骨架 + 算法插件混合架构**

| 属性 | 值 |
|------|-----|
| 文档编号 | SPEC-L3-TDCAL-ARCH-001 |
| 版本 | v1.0 |
| 日期 | 2026-04-28 |
| 作者 | 用户 + Claude |
| 状态 | **Spec 草案**（待 HAZID workshop 冻结 TBD-14/TBD-15） |
| 上游依据 | Research v1.2 (2026-04-28 冻结) + nlm 本地笔记本 + nlm-research 独立验证 |
| 下游输出 | Spec Part 2 (详细设计, 待启动) |

---

## 1. L3 层命名

| 属性 | 值 |
|------|-----|
| **中文全称** | **战术决策与避碰层** |
| **英文全称** | **Tactical Decision & Collision Avoidance Layer** |
| **缩写** | **L3-TDCAL** |
| **定位** | 自主航行系统四层架构的第三层，上承战略层 (L2 Voyage Planner)，下接控制层 (L4 Guidance & Control) |

命名理由：突出本层的两个核心职责——**战术决策**（模式管理、会遇分类、责任判定）和**避碰执行**（轨迹生成、安全校验、闭环监控），与 COLREGs 避碰规则体系和 CCS 智能系统认可对 "决策能力" 的独立评估要求直接对齐。

---

## 2. 架构总览

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
  ┌─────────────────────────────────────────────────────────┐
  │              L3 战术决策与避碰层 (TDCAL)                 │
  │                                                         │
  │  ┌──────────┐   ┌──────────┐   ┌──────────┐            │
  │  │ Stage 1  │──▶│ Stage 2  │──▶│ Stage 3  │──▶ 控制层  │
  │  │   看     │   │   判     │   │   动     │            │
  │  │ 态势感知 │   │ 规则决策 │   │ 轨迹执行 │            │
  │  └────┬─────┘   └────┬─────┘   └────┬─────┘            │
  │       ▲               ▲             ▲                   │
  │       │               │             │                   │
  │       │    ┌──────────┴─────────────┘                   │
  │       │    │  Stage 4「盯」并行监控                     │
  │       │    │  CPA趋势 · 升级/解除 · 紧急通道            │
  │       │    └────────────┬───────────→ ROC (接管请求)     │
  │       │                 │                               │
  │       └─── 闭环反馈 ────┘                               │
  │                                                         │
  │  ┌──────────────────────────────────────────────────┐   │
  │  │  Mode Manager · 模式管理器 (跨层贯通)             │   │
  │  │  D2 ↔ D3 ↔ D4 + MRC + ROC override               │   │
  │  └──────────────────────────────────────────────────┘   │
  │                                                         │
  │  ┌─────── ASDR (决策审计记录) ◀── 所有 Stage ──────┐   │
  └─────────────────────────────────────────────────────────┘
         │
         ▼
  ┌──────────────┐   ┌──────────────┐
  │  控制层 L4   │   │   公共服务   │
  │ 航向/航速/   │   │ 日志/健康/   │
  │ 紧急级别     │   │ ASDR持久化   │
  └──────────────┘   └──────────────┘
```

**相对原始候选 C SVG 图的三处优化**：

1. **Stage 4 并行监控**：Stage 4 不再是 Stage 3 的后续步骤，而是并行运行于 Stage 2/3 之上，通过反向虚线箭头随时触发升级信号
2. **Checker 双层校验**：Checker 从仅校验 Stage 3 轨迹 → 扩展为 Stage 2 决策校验 (Checker-D) + Stage 3 轨迹校验 (Checker-T) 双层
3. **ASDR 全链路写入**：每个 Stage 均向 ASDR 写入决策依据链记录

---

## 3. 四 Stage 详细设计

### 3.1 Stage 1「看」— 态势感知引擎 (Situational Awareness Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 看 (Observe) |
| **SA 层级** | Endsley Level 1 (Perception) + Level 2 (Comprehension) |
| **学术基础** | Endsley 1995; Sharma 2019 GDTA; Eriksen 2020 |
| **触发周期** | 1 Hz (可配置，适应传感器刷新率) |
| **骨架职责** | 统一感知接口：接收多源数据 → 输出标准化目标列表 |

#### 算法插件（4 个）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 学术/工业依据 |
|---|---------|---------|------|------|-------------|
| 1.1 | **CPA/TCPA 计算器** | 几何矢量法 (基于相对运动) | 本船状态 + 目标船状态 | DCPA, TCPA, 相对方位, 相对速度 | COLREGs Rule 7; ITU-R M.1371 AIS |
| 1.2 | **目标跟踪滤波器** | Kalman Filter / IMM (交互多模型) | 传感器原始轨迹点 | 平滑轨迹 + 航速/航向估计 + 不确定性协方差 | Bar-Shalom 1988 IMM; 海事雷达跟踪标准 |
| 1.3 | **运动预测器** | 线性外推 (短时) / 圆弧预测 (转向) / GP 回归 (在线学习) | 平滑轨迹 | 5-15 min 预测航迹 + 概率分布 | GP-based: Xue 2022 Ocean Engineering; Reza 2025 Scientific Reports |
| 1.4 | **威胁评分器** | 多因子加权评分 (DCPA + TCPA + Range + Aspect + Bearing Rate) | 目标列表 + CPA/TCPA | 按碰撞风险降序的威胁列表 | MarinePublic 2026 工业实操; STCW 雷达模拟训练 |

#### 关键参数（初始基线，待 HAZID 冻结）

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| CPA_safe | 1.0 nm (1852m) | 0.5 nm (926m) | 0.3 nm (556m) | 0.2 nm (370m) |
| 能见度不良修正 | < 2 nm 时 CPA_safe × 1.5 |

#### 输出契约

```yaml
output:
  threat_list:
    - target_id: string
      position: [lat, lon]
      speed: knots
      course: degrees
      dcpa: nm
      tcpa: min
      bearing: degrees
      bearing_rate: deg/s
      threat_score: 0-100
      class: enum[powered, fishing, restricted, constrained, sailing, NUC]
  own_ship:
    position: [lat, lon]
    sog: knots
    cog: degrees
    heading: degrees
    rate_of_turn: deg/min
```

#### ASDR 写入

```yaml
{stage: 1, timestamp, threat_list_snapshot, own_ship_snapshot, sensor_health}
```

---

### 3.2 Stage 2「判」— 规则决策引擎 (COLREGs Decision Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 判 (Judge) |
| **认知模型** | Klein 1998 RPD + COLREGs Rule 5-19 形式化 |
| **触发周期** | 1 Hz (每次威胁列表更新即触发) |
| **骨架职责** | 统一决策接口：接收威胁列表 → 输出避碰决策 + Doer 候选方案 → 经 Checker-D 校验 |

#### 6 步推理链

1. 会遇分类 (Rule 13/14/15：追越 / 对遇 / 交叉 / 安全通过)
2. 责任分配 (Rule 16/17：让路船 / 直航船)
3. 类型优先序判定 (Rule 18：失控 > 受限 > 限吃水 > 渔船 > 帆船 > 机动船)
4. 时机判定 (Rule 16 让路船及早行动 / Rule 17 直航船三阶段)
5. 行动选择 (Rule 8：转向优先 / 减速次之 / 停船兜底)
6. 效果验证 (Rule 8 "明显" + 维持时间)

#### 算法插件（4 个 + Checker-D）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 依据 |
|---|---------|---------|------|------|------|
| 2.1 | **会遇分类器** | 几何拓扑判定 (Rule 13/14/15 数学形式化) | 目标船相对方位 + 航向差 | 会遇类型: head-on / crossing (give-way/stand-on) / overtaking / safe-passage | COLREGs Rule 13-15; Cockcroft & Lameijer 2012 |
| 2.2 | **Rule 18 优先序判定器** | 规则链推理 (Rule 18 类型层级) | 本船类型 + 目标船类型 | 操纵能力优先级: NUC > RAM > CBD > Fishing > Sailing > Power | COLREGs Rule 18; 海事本体论 |
| 2.3 | **多目标冲突消解器** | 图搜索 / 约束满足 (CSP) | 多个会遇关系 + 优先级 | 统一避让方案 (避让方向 + 时序) | Eriksen 2020; COLREGs Rule 8 "大幅" 约束 |
| 2.4 | **Doer 主决策 (候选方案生成)** | 规则推理 + 启发式搜索 + (可选) LLM 语义辅助 | 统一避让方案 | 候选决策集 {direction, magnitude, timing}_i | Klein 1998 RPD; CORALL LLM (TechRxiv 2025) |
| **Checker-D** | **决策合规校验器** | **确定性形式化规则** (一阶谓词逻辑 / LTL) | Doer 输出的候选决策集 | PASS/FAIL + 违规项清单 + 最近合规替代方案 | Jackson 2021 MIT CSAIL; IEC 61508-3 |

#### Checker-D 校验规则

1. 避让方向不得违反 Rule 14 (对遇右转) / Rule 15 (交叉不向左转) / Rule 19 (能见度不良不向左转)
2. 行动时机满足 Rule 16 "及早" 约束 (T_give_way ≤ 8 min)
3. 行动幅度满足 Rule 8 "大幅" 约束 (航向变化 ≥ 30° 或速度减半)
4. 避让方案不违反 Rule 18 类型优先序
5. Rule 17 三阶段时机阈值不冲突

#### Rule 17 三阶段量化基线

| 阶段 | 时机阈值 | 本船义务 | 行动 |
|------|---------|---------|------|
| 阶段 1 (Rule 17(a)(i)) | TCPA > 12 min | 保持航向航速 | 仅监视 |
| 阶段 2 (Rule 17(a)(ii)) | TCPA 6-12 min | 可以单独行动 | 汽笛信号 + 可采取避让 |
| 阶段 3 (Rule 17(b)) | TCPA 4-6 min | 必须行动 | 最有助于避碰的行动 |
| 紧急 (Rule 17(b) 极限) | TCPA < 1 min | 反射弧 | 紧急通道 |

#### 关键参数（初始基线）

| 参数 | 值 | 依据 |
|------|-----|------|
| 最小航向变化 | 30° | Rule 8 "大幅" 量化 (MarinePublic 2026 + Wang 2021 MDPI JMSE) |
| 追越扇区 | 相对方位 112.5° ~ 247.5° | Rule 13 "正横后大于 22.5°" |
| 对遇判定 | 相对方位 ±6° 内 + 航向差 170°-190° | COLREGs Rule 14 工业解释 |
| 安全通过距离 | ≥ 0.5 nm (开阔水域 / 良好能见度) | 工业实操基线 |

#### 输出契约

```yaml
output:
  decision:
    encounter_type: enum[head_on, crossing_give_way, crossing_stand_on, overtaking, safe_passage]
    role: enum[give_way, stand_on, both_give_way]
    action_direction: enum[starboard, port, reduce_speed, stop, maintain]
    action_magnitude:
      course_change_deg: float (≥30 if starboard/port)
      speed_change_percent: float (≥50 if reduce_speed)
    timing:
      t_start: seconds
      t_maintain: seconds (≥120 for Rule 8 "prolonged")
    rule_basis: [string]  # e.g. ["Rule 15", "Rule 16", "Rule 8"]
  checker_d_result:
    verdict: PASS | FAIL
    violations: [string]
    nearest_compliant: {decision}  # only if FAIL
```

#### ASDR 写入

```yaml
{stage: 2, timestamp, decision, encounter_classification, rule_basis, checker_d_verdict, input_threat_snapshot}
```

---

### 3.3 Stage 3「动」— 轨迹执行引擎 (Trajectory Execution Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 动 (Act) |
| **控制基础** | Fossen 2021 船舶水动力学 + Eriksen 2020 多时间尺度混合 |
| **触发周期** | 中程 MPC: 0.2 Hz (5s) / 近距 BC-MPC: 1 Hz / Checker-T: 每次 Doer 输出即触发 |
| **骨架职责** | 统一执行接口：接收避碰决策 + 全局航路 → 输出可执行轨迹 → 经 Checker-T 校验 |

#### 算法插件（5 个 + Checker-T）

| # | 插件名称 | 算法选型 | 预测时域 | 触发条件 | 依据 |
|---|---------|---------|---------|---------|------|
| 3.1 | **中程 MPC** | Tube MPC + MMG 3-DOF + Yasukawa 2016 高速修正 | 3-5 min | 威胁 TCPA > 5 min (常规避碰) | Eriksen 2020; Yasukawa 2015/2016 |
| 3.2 | **近距 BC-MPC** | Branching-Course MPC (多分支并行) | 30-60 s | 威胁 TCPA < 5 min 或 Rule 17(b) 触发 | Eriksen 2020; Rule 17(b) |
| 3.3 | **航点序列生成器** | 转向起始点 → 维持段 → 回归过渡点 → 切入点的 4 段式航点链 | — | 每次 MPC 输出后 | COLREGs Rule 8 "明显"; Szlapczynski 2015 JNAOE |
| 3.4 | **ENC 安全校验器** | S-57/S-100 电子海图查询 + 浅水/禁区多边形碰撞检测 | — | 航点序列生成后 | IHO S-100; COLREGs Rule 2 "良好船艺" |
| 3.5 | **船舶动力学插件** | MMG 3-DOF 模型 (可替换: 不同船型注入不同水动力系数) | — | 每次 MPC 预测时调用 | Yasukawa & Yoshimura 2015 MMG; Yasukawa 2016 高速修正 |
| **Checker-T** | **轨迹安全校验器** | 前向仿真 (10-15 min) + ODD 边界验证 + COLREGs 复检 | 10-15 min | 每次轨迹生成后 | Jackson 2021; Johansen 2016; CCS ODD 规范 |

#### Checker-T 校验规则

1. 前向仿真 10-15 min: 在仿真环境中模拟轨迹执行 → 是否产生新的碰撞风险
2. ODD 边界验证: 轨迹是否超出本船认证的运行设计域 (浅水 / 禁区 / 超出通信覆盖)
3. COLREGs 复检: 轨迹执行后是否违反避让方向约束
4. 船舶动力学可行性: 航速 / 转弯半径 / 加速度是否在船舶物理包络内

#### 多船型扩展机制

- 船舶动力学插件接口在 v1 冻结为 `VesselDynamicsPlugin` 抽象类
- FCB 首发使用 `FCB_MMG_3DOF` 实现 (含 Yasukawa 2016 高速修正项)
- 其他船型注入对应的水动力系数矩阵即可，不修改 Stage 3 骨架逻辑
- FCB 高速段 (25-35 节): Tube MPC 显式建模不确定性 + 可选在线 GP/SI 辨识

#### 关键参数（初始基线）

| 参数 | 值 | 依据 |
|------|-----|------|
| 中程 MPC 预测时域 | 3-5 min | Eriksen 2020 |
| 近距 BC-MPC 预测时域 | 30-60 s | Eriksen 2020 |
| 避让维持时间 | ≥ 2 min | Rule 8 "明显" 量化 |
| 转弯半径 | ≥ 旋回半径 × 1.2 | 船舶操纵安全裕度 |
| 切入角 | ≤ 30° | 超过则插入过渡点 |
| 避让最大速度 | ≤ 0.85 × V_max | 保留 15% 推力储备 |

#### 输出契约

```yaml
output:
  trajectory:
    waypoints:
      - position: [lat, lon]
        arrival_time: seconds
        speed: knots
        heading: degrees
    emergency_level: enum[none, warning, critical]
  checker_t_result:
    verdict: PASS | FAIL
    forward_sim_result:
      min_cpa_next_15min: nm
      odd_violation: bool
      grounding_risk: bool
    nearest_compliant_trajectory: {trajectory}  # only if FAIL
```

#### ASDR 写入

```yaml
{stage: 3, timestamp, trajectory, mpc_cost_function, checker_t_verdict, forward_sim_summary, vessel_dynamics_plugin_id}
```

---

### 3.4 Stage 4「盯」— 闭环监控引擎 (Risk Monitor & Feedback Engine)

| 属性 | 值 |
|------|-----|
| **CMM 对应** | 盯 (Monitor) |
| **SA 层级** | Endsley Level 3 (Projection) + 控制反馈环 |
| **运行模式** | **并行监控** (独立于 Stage 2/3 持续运行，非串行末端步骤) |
| **触发周期** | 1 Hz (持续监控) / 100 ms (紧急通道) |
| **骨架职责** | 统一监控接口：持续接收执行效果 → 升级/解除/紧急切换 → 反馈到 Stage 1/2/3 或 ROC |

#### 算法插件（4 个）

| # | 插件名称 | 算法选型 | 输入 | 输出 | 依据 |
|---|---------|---------|------|------|------|
| 4.1 | **CPA 趋势分析器** | 线性回归 (滑动窗口 30s) | 实时 CPA/TCPA 时间序列 | 趋势: improving (>1 m/s) / stable / deteriorating (<-0.5 m/s) | Endsley 1995 L3; Hareide-Veitch 2024 transparency |
| 4.2 | **升级决策器** | 阈值触发 + 趋势确认 | CPA 趋势 + TCPA | escalate / no_action | COLREGs Rule 17(b); Wang 2021 MDPI JMSE |
| 4.3 | **解除判定器** | 四条件 AND 门 + 持续确认 ≥ 60s | CPA ≥ CPA_safe + TCPA < 0 + range_rate > 0 + threat_score < 20 | release / maintain_avoidance | COLREGs Rule 8 "明显" + "过去并让清" |
| 4.4 | **紧急通道** | 硬编码反射弧 (绕过 Doer-Checker 常规路径) | CPA < 0.5×CPA_safe + TCPA < T_emergency | 紧急指令 → 直接下发控制层 + ROC 通知 | COLREGs Rule 17(b); Veitch 2024 Ocean Engineering |

#### 并行监控的反馈路径

Stage 4 不是线性流水线的末端，而是并行运行于 Stage 2/3 之上：

| 反馈目标 | 触发条件 | 动作 |
|---------|---------|------|
| → Stage 1 | CPA 趋势恶化 | 触发重新感知 (新威胁扫描) |
| → Stage 2 | CPA 持续恶化 + TCPA < T_escalate | 触发重新决策 (升级避让方案, 加大幅度) |
| → Stage 3 | CPA < 0.5×CPA_safe + TCPA < T_emergency | 触发紧急指令 (紧急通道绕过常规 Checker) |
| → Mode Manager | 紧急通道激活 / 连续升级 | 触发模式降级 (D4→D3→MRC) |
| → ROC | 自主能力不足 / 升级无效 | 发出接管请求 (TOR, Take-Over Request) |

#### 关键参数（初始基线，待 HAZID 冻结）

| 参数 | 开阔水域 | 沿岸 | 狭水道 | 港内 |
|------|---------|------|--------|------|
| CPA_safe | 1.0 nm | 0.5 nm | 0.3 nm | 0.2 nm |
| T_escalate (升级 TCPA 阈值) | 2 min | 1.5 min | 1 min | 0.75 min |
| T_emergency (紧急 TCPA 阈值) | 1 min | 0.75 min | 0.5 min | 0.33 min |
| CPA 趋势改善阈值 | 线性回归斜率 > 1 m/s |
| 解除确认持续时间 | 60s |
| 解除后监控窗口 | 120s |

#### ASDR 写入

```yaml
{stage: 4, timestamp, cpa_trend, escalation_triggered, release_decision, emergency_channel_activated, tor_sent}
```

---

## 4. Doer-Checker 双轨架构（完整双层设计）

相对原始候选 C SVG 图的关键改进——Checker 从仅校验 Stage 3 轨迹扩展为双层：

```
  Stage 2「判」                    Stage 3「动」
  ┌─────────────┐                 ┌─────────────┐
  │  Doer 主决策 │─── candidate ──▶│  中程 MPC   │
  │  (候选方案)  │    decisions    │  近距 BC-MPC│
  └──────┬──────┘                 └──────┬──────┘
         │                               │
         ▼                               ▼
  ┌─────────────┐                 ┌─────────────┐
  │  Checker-D  │                 │  Checker-T  │
  │  决策合规校验│                 │  轨迹安全校验│
  │             │                 │             │
  │ · COLREGs   │                 │ · 前向仿真   │
  │   方向/时机 │                 │   10-15 min  │
  │ · Rule 18   │                 │ · ODD 边界   │
  │   优先序    │                 │ · 浅水/禁区  │
  │ · 最近合规  │                 │ · 最近合规   │
  │   替代方案  │                 │   替代轨迹   │
  └──────┬──────┘                 └──────┬──────┘
         │                               │
         └─────── PASS/FAIL ─────────────┘
                      │
                      ▼
              ┌──────────────┐
              │ Mode Manager │  ← 任一层校验 FAIL 即触发
              │ (降级/MRC)   │
              └──────────────┘
```

**级联关系**：
- Checker-D PASS → 方案进入 Stage 3 轨迹生成
- Checker-T PASS → 轨迹下发控制层 L4
- 任一层 FAIL → 生成 "最近合规替代方案"
- 连续 N 个周期无方案通过 → Mode Manager 触发 MRC

**Checker-D vs Checker-T 职责边界**：

| 维度 | Checker-D (决策层) | Checker-T (轨迹层) |
|------|-------------------|-------------------|
| 校验对象 | 避碰决策 (方向/时机/幅度) | 执行轨迹 (空间/时间/动力学) |
| 校验方式 | 形式化规则 (LTL) | 前向仿真 + ODD 边界查询 |
| 响应时间 | < 100 ms | < 500 ms |
| 失败后果 | 拒绝决策，要求 Doer 重新生成 | 拒绝轨迹，要求 MPC 重新规划 |
| 连续失败 | 触发降级 | 触发紧急通道 |

**工业先例**：
- 航空：Boeing 777 主控制器 + 安全控制器双轨架构
- 海军航空：Runtime Assurance (RTA) 输入监视器 + 控制器/安全监视器
- 汽车：MIT Certified Control 范式 (Jackson 2021)
- 海事：NTNU + DNV 联合验证中

---

## 5. Mode Manager 模式管理器

| 属性 | 值 |
|------|-----|
| **位置** | 跨层贯通 (独立于 4 Stage，监控所有 Stage 的状态) |
| **实现** | 分层有限状态机 (HFSM) |
| **运行周期** | 10 Hz (高优先级) |

### 5.1 模式状态定义

| 模式 | 对应 CCS 标志 | 描述 | 触发条件 |
|------|-------------|------|---------|
| **D2** | N (基础辅助) | 决策辅助模式，船员主导，系统提供预警 + 建议 | 默认 / D3/D4 降级 |
| **D3** | Nx + R1 | 自主航行 + 船员监视 + ROC 备份 | 船长授权 + ODD 满足 |
| **D4** | A1/A2/A3 | 全自主航行（根据 ODD 分级） | 船长授权 + ODD 满足 + 系统自检全部 PASS |
| **MRC** | — | 最小风险态 (减速 / 停船 / 锚泊) | Doer-Checker 连续失败 / 通信中断 / 传感器降级 |
| **ROC_Override** | — | ROC 直接控制 | ROC 发起 override 请求 |

### 5.2 模式切换矩阵

```
         ┌──────┐    授权     ┌──────┐    ODD满足    ┌──────┐
         │  D2  │ ────────▶  │  D3  │ ──────────▶  │  D4  │
         └──────┘            └──────┘              └──────┘
              ▲                   ▲                     │
              │                   │                     │ 故障/降级
              │                   │                     ▼
              │  ┌──────────┐    │                ┌──────┐
              └──│   MRC    │◀───┴────────────────│ MRC  │
                 └──────────┘    严重故障          └──────┘
                                                      │
                                                      │ ROC接管
                                                      ▼
                                                ┌──────────────┐
                                                │ ROC_Override │
                                                └──────────────┘
```

### 5.3 ROC 接管时间预算（初始设计基线）

| 类型 | 时间预算 | 学术依据 |
|------|---------|---------|
| 紧急避碰 | ≤ 30s | Veitch 2024 (20s 紧急介入窗) |
| 模式切换 | ≤ 60s | Veitch 2024 (60s 监督介入窗) |
| 非紧急监督 | ≤ 5 min | 行业默认实践 |

> 注意：船级社不发布固定秒级硬限。上表数值为项目级初始设计基线，最终值在 CONOPS 中经 HAZID 冻结。

---

## 6. ASDR (决策审计记录) 全链路设计

每个 Stage 向 ASDR 显式写入决策依据链记录，形成完整的可追溯链路。

### 6.1 各 Stage 写入内容

| Stage | 写入事件 | 记录内容 | 保留策略 |
|-------|---------|---------|---------|
| **Stage 1** | `situational_snapshot` | 威胁列表快照 + 本船状态 + 传感器健康 + 时间戳 | ≥ 30 天循环 |
| **Stage 2** | `decision_rationale` | 会遇分类 + 责任判定 + 规则依据 + Checker-D 裁决 + 候选方案 | ≥ 30 天循环 |
| **Stage 3** | `trajectory_rationale` | 生成航点序列 + 代价函数值 + Checker-T 裁决 + 前向仿真摘要 + 动力学插件 ID | ≥ 30 天循环 |
| **Stage 4** | `monitor_event` | CPA 趋势 + 升级/解除触发 + 紧急通道激活 + TOR 发出 | ≥ 30 天循环 |
| **Mode Manager** | `mode_transition` | 旧模式 → 新模式 + 触发原因 + 时间戳 | 永久保留 |

### 6.2 ASDR 事件 Schema

```yaml
asdr_event:
  event_id: uuid
  stage: enum[1, 2, 3, 4, mode_manager]
  timestamp: iso8601
  cmm_stage: enum[observe, judge, act, monitor]
  decision_4_tuple:
    applicable_rules: [string]         # e.g. ["Rule 15", "Rule 16", "Rule 8"]
    input_snapshot:                    # 输入数据快照
      threat_list: [...]
      own_ship: {...}
      water_type: enum[open_sea, coastal, narrow_channel, harbour]
      visibility: nm
    decision_path:                     # 决策选择路径
      encounter_class: string
      responsibility: string
      action: string
      expected_cpa: nm
    outcome_comparison:                # 预期 vs 实际
      predicted_cpa: nm
      actual_cpa: nm
      deviation: nm
  checker_verdict: PASS | FAIL
  source_evidence: [string]            # 上游证据引用
```

### 6.3 ASDR 与 IACS UR E27 的关系

| 维度 | IACS UR E27 事件记录 | ASDR 决策审计 |
|------|---------------------|-------------|
| 目的 | 网络入侵审计 | 决策审计 |
| 核心问题 | Who attacked? When? What was accessed? | Why this decision? On what evidence? Predicted vs actual outcome? |
| 持久化 | 加密日志 | 加密日志 (共用基础设施) |
| Schema | 网络事件 | 决策事件 |
| 保留策略 | ≥ 30 天循环 | ≥ 30 天循环 (模式切换永久) |

---

## 7. 接口契约汇总

| 接口 | 方向 | 数据内容 | 协议 | 频率 |
|------|------|---------|------|------|
| 感知融合 → Stage 1 | 入 | 目标列表 (Radar/AIS/LiDAR/Camera) + ENC + 本船状态 | DDS / ROS 2 | 1-10 Hz |
| 战略层 L2 → Stage 3 | 入 | 全局航路点 + 时序约束 + 优先级 | DDS | 按需 (航路更新) |
| ROC ↔ Mode Manager | 双向 | 模式切换请求/确认 + override 命令 + 心跳 | TCP/TLS | 1 Hz (心跳) |
| Stage 3 → 控制层 L4 | 出 | 参考航向 + 航速 + 紧急级别标志 | DDS / CAN | 10 Hz |
| 所有 Stage → ASDR | 出 | 决策审计事件 (见 §6.2) | 内部总线 → 持久化 | 事件驱动 |
| 公共服务 | 出 | 日志 + 健康监测 + 告警上报 | syslog + DDS | 持续 |

---

## 8. 部署与运行环境

| 维度 | 规格 |
|------|------|
| **计算机系统等级** | CCS III 类 CBS (最高等级) |
| **SIL 候选** | 主决策路径 SIL 2-3; 辅助子模块 SIL 1-2 (待 HAZID 冻结) |
| **网络安全** | IACS UR E26/E27 Rev.1 + CCS《船舶网络安全指南》(2024) |
| **冗余** | 双路电源 + 热备份控制器 |
| **操作系统** | 实时 Linux (PREEMPT_RT) 或 VxWorks |
| **中间件** | DDS (RTI Connext / eProsima Fast DDS) |
| **开发语言** | 骨架层: C++17; 插件层: C++17 / Python 3 (ML 插件) |

---

## 9. 与 CCS 智能系统认可的对接矩阵

| CCS 要求 | L3-TDCAL 应对 | 证据材料 |
|---------|-------------|---------|
| 智能系统独立认可 | 4 Stage + Mode Manager 各自独立，接口明确冻结 | 各 Stage 的输入/输出契约 YAML schema |
| III 类 CBS 网络安全 | 骨架层纯白盒，插件层 ML 边界清晰 | 网络安全测试报告 + IACS UR E27 合规声明 |
| 决策可解释性 | CMM 4 阶段语义 + 决策 4 元组记录 + ASDR | 决策依据链重放 (任意时间窗) |
| 场景感知 + 自主决策 | Stage 1 (感知) + Stage 2 (决策) 独立评估 | 各 Stage 独立 V&V 测试报告 |
| 软件可靠性 | 骨架与插件分离，骨架无 ML，Checker 确定性校验 | CCS《船用软件安全及可靠性评估指南》合规声明 |
| 三段式 V&V | MIL → SIL → HIL → Sea Trial | 每阶段测试覆盖矩阵 + STPA UCA 矩阵 |

---

## 10. CMM 形式化映射 (双向追溯表)

| Spec ADR | 主要 CMM Stage | 上游证据 |
|---------|--------------|---------|
| AD-02 多时间尺度混合 MPC + BC-MPC | Stage 2 + Stage 3 | Eriksen 2020 + COLREGs Rule 8/13-17 |
| AD-04 主决策白盒 + ML 边界 | Stage 2 | CCS《船用软件安全及可靠性评估指南》+ Hareide-Veitch 2024 transparency |
| AD-15 COLREGs 9 条规则形式化 | Stage 2 | IMO COLREGs Rule 5-19 + Wang 2021 MDPI JMSE 9(6):584 |
| AD-09 MRC 三模式 | Stage 4 | COLREGs Rule 17(b) + Veitch 2024 Ocean Engineering |
| AD-23 SIL 2-3 候选 | 全部 4 Stage | IEC 61508-3 + Park & Kim 2024 |
| AD-25a STPA + Software FMEA + BN | 全部 4 Stage | STPA Handbook 2018 + Dugan 2023 TransNav |
| AD-04a CMM 横向锚定 | Stage 1-4 自身 | Endsley 1995 + Klein 1998 + 控制论 |
| AD-25f Doer-Checker 双轨 | Stage 2 + Stage 3 + Stage 4 | Jackson 2021 MIT CSAIL + Boeing 777 + RTA 海军航空 |
| AD-19a ASDR 决策审计 | 全部 4 Stage | §6.12 + IACS UR E27 Rev.1 |

---

## 11. 后续 Spec 阶段待决策项 (TBD)

| ID | 内容 | 依赖 |
|----|------|------|
| TBD-14 | 模块结构最终选型确认 (本文档已采纳候选 C) | Spec §2.1 HAZID workshop |
| TBD-15 | CMM 与各模块的绑定方式 (已明确: 骨架=CMM 语义边界) | TBD-14 闭合后 |
| TBD-20 | Stage 间接口的 Protobuf/IDL 详细定义 | Spec §3 |
| TBD-21 | Checker-D 和 Checker-T 的具体 LTL 规则集 | Spec §2.5 |
| TBD-22 | ASDR 持久化策略与加密方案 | Spec §2.5 + 网络安全评估 |
| TBD-23 | 各 Stage 的 SIL 最终值 (STPA → FMEA → BN → 量化) | HAZID workshop + CCS 验船师确认 |
| TBD-24 | 紧急 / 监督 / 非紧急三档接管时间预算的最终值 | CONOPS + HAZID |
| TBD-25 | DCPA / TCPA 触发阈值的最终值 | HAZID workshop |
| TBD-26 | MRC 的具体定义 (减速 / 锚泊 / 漂泊) 的最终选择 | CONOPS |

---

## 12. 变更历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|---------|
| v1.0 | 2026-04-28 | 用户 + Claude | 初稿：基于 Research v1.2 + nlm 笔记本 + nlm-research 独立验证 + 候选 C SVG 三处优化 (Stage 4 并行监控 / Checker 双层校验 / ASDR 全链路写入)。采纳候选 C 心智骨架+算法插件混合架构，定义 L3-TDCAL 完整模块设计 |

---

*本架构设计规约基于 Research v1.2 (2026-04-28 冻结) + nlm 本地笔记本 34 篇来源 + nlm-research 36 篇独立验证 + 候选 C SVG 图的三处优化。所有决策均锚定到 Endsley 1995 / Eriksen 2020 / Jackson 2021 / COLREGs 1972 / CCS《智能船舶规范》(2025) / IACS UR E26/E27 Rev.1 的证据链。*
