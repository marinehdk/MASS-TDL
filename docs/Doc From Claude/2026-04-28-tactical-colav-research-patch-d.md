# L3 战术决策层 — Research Patch D

| 属性 | 值 |
|------|-----|
| 文档编号 | RESEARCH-PATCH-D-001 |
| 版本 | v1.0 |
| 日期 | 2026-04-28 |
| 基于 | Research v1.2（冻结）+ 架构评审会话（2026-04-28）|
| 状态 | **草案**（待用户冻结后作为 Spec v2.0 的补充真源）|
| 触发原因 | 架构评审发现 Research v1.2 未覆盖的 5 类结构性缺口（A-02/A-05/B-01/B-02 + L2/L3 功能切割确认）|
| 上游权威 | Research v1.2 全部内容继续有效；本 Patch 仅增量覆盖新缺口 |

---

## 1. Patch 范围

本 Patch 关闭以下 4 个调研缺口，不重复 v1.2 已冻结内容：

| 缺口 ID | 问题 | 调研方法 | 状态 |
|--------|------|---------|------|
| A-02 | CBF（Control Barrier Functions）与 MPC 集成方案 | nlm-research deep + 文献调研 | CLOSED |
| A-05 | Scenario Router（情景路由器）的工业实现范式 | nlm-research deep + MOOS-IvP 调研 | CLOSED |
| B-01 | L3 → L2 反向反馈通道 | nlm-ask + 工业系统调研 | CLOSED（无先例，需原创设计）|
| B-02 | TSS（交通分隔带）Rule 10 路径规划约束 | nlm-ask + COLREGs Rule 10 形式化 | CLOSED |

另附：L2/L3 功能切割确认（来自架构评审 nlm-ask，High confidence）。

---

## 2. Gap A-02：CBF+MPC 集成（控制屏障函数）

### 2.1 背景

候选 C v1.1 使用 RTA Simplex 架构作为安全底线：正常模式走 Stage 2 Doer → Stage 3 中程 MPC → Stage 3 Checker-T；紧急模式单向切换到 Stage 3 BC-MPC，绕过 Stage 2。Simplex 是**离散切换（hard switch）**：模式在阈值处突变。

调研发现，NTNU milliAmpere2 及 2020 年后主流海事 ASV 研究均采用 **CBF（Control Barrier Functions）** 提供连续安全约束，与 MPC 协同工作。

### 2.2 CBF-MPC 数学框架

**CBF 安全集定义**：

令 $h(x): \mathbb{R}^n \to \mathbb{R}$ 为安全函数，安全集 $\mathcal{S} = \{x \mid h(x) \geq 0\}$。

CBF 条件（一阶）：

$$\dot{h}(x, u) \geq -\alpha(h(x))$$

其中 $\alpha$ 为 class-K 函数（线性：$\alpha(h) = \kappa h$，$\kappa > 0$）。

**CBF-MPC 集成（离散时间）**：

在每个 MPC 步长 $k$，将 CBF 约束嵌入 QP 问题：

$$u_k^* = \arg\min_{u_k} \; J(x_k, u_k)$$

$$\text{s.t.} \quad h(x_{k+1}) - h(x_k) \geq -\alpha \cdot h(x_k)$$

$$\quad\quad\quad u_k \in \mathcal{U}, \quad x_{k+1} = f(x_k, u_k)$$

来源：Discrete-Time CBF-MPC — UC Berkeley Hybrid Robotics Lab (ACC 2021) [E4]

**海事专用扩展（Relaxed CBF for Underactuated USVs）**：

欠驱动船舶（仅有推力 + 舵，无侧向推力）无法在所有状态下满足严格 CBF 约束。IEEE 2024 提出 **Relaxed CBF**：

$$\dot{h}(x, u) \geq -\alpha(h(x)) - \delta$$

其中 $\delta \geq 0$ 为松弛变量，在违约时通过惩罚项 $\lambda \delta$ 加入代价函数。

来源：Relaxed CBF for Closest Approach by Underactuated USVs — IEEE TAC 2024 [E4]

**CLF-CBF for ASVs with LiDAR（NOVA 2025）**：

对装备 LiDAR 的 ASV，将 CLF（Control Lyapunov Function，稳定性）+ CBF（安全性）合并为单一 QP：

$$\min_{u, \delta} \|u\|^2 + \lambda \delta^2$$

$$\text{s.t.} \quad \dot{V}(x,u) \leq -\gamma V(x) + \delta \quad \text{（CLF，稳定性）}$$

$$\quad\quad\quad \dot{h}(x,u) \geq -\alpha h(x) \quad \text{（CBF，安全性）}$$

来源：Guaranteed CA for ASVs with LiDAR using CLF-CBF — NOVA Research Portal 2025 [E4]

### 2.3 CBF vs RTA Simplex 对比

| 维度 | RTA Simplex（候选 C v1.1）| CBF-MPC（候选 C v2.0 建议）|
|------|--------------------------|--------------------------|
| 安全约束类型 | 离散切换（阈值触发一刀切）| 连续约束（始终软性"推离"危险边界）|
| 行为平滑性 | 紧急通道激活时控制量突变 | 平滑渐进，无突然切换感 |
| 与 MPC 集成 | RTA 在 MPC 之外独立运行 | CBF 直接作为 MPC 的 QP 约束 |
| 形式化可验证性 | 可用混合程序验证切换时机 | CBF 前向不变集有数学证明 |
| 欠驱动船舶适用性 | 适用（BC-MPC 处理机动限制）| 需 Relaxed CBF（IEEE 2024）|
| CCS 认证友好度 | 白盒规则，易解释 | 数学约束，需附加解释性工具 |

### 2.4 候选 C v2.0 集成方案

**不替换 RTA Simplex，而是在 Stage 3 MPC 中增加 CBF 约束层，RTA 保留为最终兜底：**

```
Stage 3 MPC（修改后）:
  ├── 代价函数 J（轨迹偏差 + 能效）
  ├── 动力学约束（MMG 3-DOF + Yasukawa）
  ├── [新增] CBF 约束层（连续安全推力约束）
  │     ├── CBF-01: CPA 安全集 h₁(x) = cpa(x) - cpa_safe ≥ 0
  │     ├── CBF-02: UKC 安全集 h₂(x) = depth(x) - draft - ukc_min ≥ 0
  │     └── CBF-03: TSS 分隔带 h₃(x) = dist_to_separation_zone(x) ≥ 0
  └── [保留] RTA Simplex 紧急切换（当 CBF-QP 无可行解时激活）
```

CBF-QP 无可行解（船舶动力学无法满足约束）→ 触发 Stage 4 RTA 紧急通道（不变）。

**置信度**：🟢 High（UC Berkeley 2021 + IEEE 2024 + NOVA 2025 三源一致）

---

## 3. Gap A-05：Scenario Router 工业实现范式

### 3.1 背景

候选 C v1.1 的 Stage 2 将所有情景（开阔水域会遇、TSS 穿越、狭水道、进港）导入同一 6 步 COLREGs 推理链。没有机制在进入 Stage 2 前判断"当前是什么战术情景"。

### 3.2 工业实现调研

**发现 1：MOOS-IvP 不使用离散路由器**

MOOS-IvP IvP Helm 的核心机制是**行为权重竞争（Behavior Arbitration）**，而非离散情景切换：

- 每个行为（`BHV_Waypoint`、`BHV_AvoidCollision`、`BHV_FollowTSSLane` 等）独立输出 IvP 目标函数
- Helm 在每个周期对所有激活行为的目标函数求**加权和最优解**
- 情景差异通过动态调整行为的**权重（priority）和激活条件（condition）**实现，不是硬切换

来源：MOOS-IvP Helm Release 24.8 — MIT Marine Autonomy Lab [E3]

**发现 2：TransNav 量化 SA 处理框架（情景分类层）**

TransNav 论文提出对自主船航行的情景感知进行量化处理，包含情景类型分类：

- 情景分类器基于：水域类型（ENC 查询）+ 交通密度 + 与 TSS/VTS 区域的相对位置
- 输出：情景权重向量，激活对应行为集合

来源：Quantitative Processing of SA for Autonomous Ships Navigation — TransNav Journal [E4]

**发现 3：TSS Rule 10 特殊性分析**

[PFRI Rijeka 大学] Rule 10 TSS 穿越不仅是 COLREGs 会遇，还有特殊操作要求：

- 穿越角度需接近 90°（硬性要求，不同于开阔水域的最优化选择）
- TSS 内帆船不得妨碍机动船（优先权反转）
- 需识别分隔带多边形避免进入

来源：From Misunderstanding to Safety: Insights into COLREGs Rule 10 (TSS) Crossing Problem — PFRI 2024 [E4]

### 3.3 候选 C v2.0 Scenario Router 设计

基于调研，采用**两级机制**（非纯离散路由，也非纯 MOOS-IvP 权重）：

```
┌─────────────────────────────────────────────────────┐
│              情景路由器（Scenario Router）             │
│                                                     │
│  输入：水域类型 + ENC 查询 + 本船状态 + VTS 状态       │
│                                                     │
│  ① 情景分类（Scenario Classifier）                   │
│     → 输出: scenario_type + priority_profile         │
│                                                     │
│  ② 约束集选择器（Constraint Set Selector）           │
│     → 激活 Stage 2 对应规则子集                      │
│     → 激活 Stage 3 对应 MPC 约束集（含 CBF 参数）     │
└─────────────────────────────────────────────────────┘
```

| `scenario_type` | 触发条件 | Stage 2 主规则 | Stage 3 额外约束 |
|-----------------|---------|---------------|----------------|
| `open_sea` | water_zone=open_sea | Rule 5-19 全集 | 标准 CBF 参数 |
| `coastal_tss` | in_tss_zone=true | Rule 10 优先 + Rule 13-17 | TSS 多边形 CBF-03 + 90° crossing 强制 |
| `narrow_channel` | water_zone=narrow_channel | Rule 9 最高优先 | 航道边界 CBF + 最大速度限制 |
| `port_approach` | water_zone=port AND arrival_mode=true | VTS + Rule 9 + 引航协议 | 港口速度限制 + 拖轮会合窗 |
| `pilot_transfer` | pilot_requested=true AND range_to_pilot<2nm | 引航艇会合规程 | 低速 MPC（≤5kn）+ 会合点 waypoint |
| `waiting_vtc` | vtc_clearance_pending=true | 保持等待点 | Holding Pattern（圆圈或固定点 DP）|
| `anchor_approach` | L2_anchor_cmd=true | 进锚 COLAV + 浅水警戒 | UKC CBF 收紧 + 低速 |

**置信度**：🟡 Medium（MOOS-IvP + TransNav 提供分类机制，但具体 scenario_type 枚举为项目级设计）

---

## 4. Gap B-01：L3 → L2 反向反馈通道

### 4.1 调研核心发现

**工业界无先例：** K-MATE 和 MOOS-IvP 均未实现显式 L3→L2 路由重规划请求：

- **K-MATE**：遇到无法解决的避碰时，执行"停船 + 操作员警报"→ MRC；依靠人工介入，不自动向上触发重规划。
- **MOOS-IvP**：无 L3→L2 反向通道概念；通过行为权重压制路径跟随行为来"解锁"局部死锁；若极端无解则行为权重异常 → 操作员告警。

来源：nlm-ask（High confidence）；K-MATE: SM300 / K-MATE deployment docs [E3]

**关键区分（本项目原创需求）**：

| 失败类型 | 当前 v1.1 处理 | v2.0 建议处理 |
|---------|--------------|-------------|
| **安全失败**（Doer-Checker 连续 FAIL，算法能力边界）| MRC ✅ | MRC（不变）|
| **路线不兼容**（局部轨迹合法但全局航路与当前交通冲突）| MRC ❌（过度保守）| `replan_request` → L2 |
| **外部限制**（VTS 封航、临时禁区、异常等待超时）| MRC ❌ | `replan_request` + ROC 通知 → L2 |

### 4.2 候选 C v2.0 反向通道设计

**新增接口：`tactical_status` DDS Topic（L3 → L2 上行）**

```yaml
tactical_status_event:
  timestamp: iso8601
  status_type: enum
    # safe_deviation        - 正常避让偏离，不需要 L2 介入
    # route_incompatible    - 局部轨迹合法但无法回归全局航路（触发 L2 重规划）
    # external_constraint   - VTS/封航/TSS 导致无解（触发 L2 重规划 + ROC 通知）
    # safety_failure        - Doer-Checker 连续失败（触发 MRC，不触发 L2 重规划）
  
  context:
    blocking_reason: string           # 人类可读原因描述
    estimated_wait_min: float         # 外部限制时的预计等待时间（若已知）
    current_position: [lat, lon]
    blocking_zone_polygon: [[lat,lon]] # 封航/TSS 区域（若适用）
    suggested_waypoint: [lat, lon]    # L3 建议的绕行起点（辅助 L2 重规划）
  
  action_requested: enum
    # none                  - 仅通报状态
    # replan_route          - 请求 L2 重新规划全局航路
    # update_eta            - 请求 L2 更新 ETA（等待场景）
    # notify_roc            - 通知 ROC（人类判断）
```

**触发条件**（在 Stage 4 升级决策器中新增判断分支）：

```yaml
route_incompatible_detection:
  trigger:
    # Doer-Checker 连续 K 次失败，但每次失败原因是"回归轨迹与交通冲突"
    # 而非"当前避让动作违规"
    consecutive_return_failures: 3      # Stage 3 连续 3 次无法规划回归段
    return_horizon_blocked_s: 300       # 5 分钟内无法找到回归 L2 航路的轨迹
  action:
    publish: tactical_status(route_incompatible)
    local_behavior: "hold_safe_position"  # 不触发 MRC，保持安全位置等待 L2 响应
    timeout_s: 120                        # 120s 无 L2 响应则升级为 safety_failure → MRC
```

**置信度**：🟡 Medium（无先例，设计基于分层规划理论 + AV 领域 Apollo/CARLA 架构类比；需 HAZID 验证超时参数）

---

## 5. Gap B-02：TSS Rule 10 路径规划约束

### 5.1 调研核心发现

**发现 1：TSS 应作为 MPC 硬几何约束，而非 COLREGs 规则检查**

TSS 合规与开阔水域 COLREGs 会遇的本质区别：

| 维度 | 开阔水域 COLREGs 会遇 | TSS Rule 10 |
|------|---------------------|------------|
| 决策类型 | 响应式（检测到他船后决定如何避让）| 预防式（主动保持在正确车道内）|
| 约束来源 | 他船相对运动（动态）| ENC 多边形（静态地理约束）|
| 实现层级 | Stage 2 规则推理 → Stage 3 轨迹生成 | Stage 3 CBF 几何约束（嵌入 MPC）|
| 优先权改变 | 标准 Rule 13-17 | 帆船/小船不得妨碍车道内机动船 |

来源：PFRI 2024 + nlm-ask High confidence

**发现 2：Rule 10 的 6 项技术要求**

| 要求 | 技术实现 |
|------|---------|
| 在正确车道内行驶 | CBF-03：`dist_to_separation_zone(x) ≥ d_min` |
| 保持车道方向行驶 | MPC 代价：偏离车道方向角 `θ_deviation` 惩罚项 |
| 穿越时接近 90° | Stage 2 crossing 决策：强制 heading 约束为 `abs(θ_crossing - 90°) ≤ 10°` |
| 加入/离开在车道端部 | Stage 3 waypoint 生成：TSS 模式下强制从车道终端切入 |
| 不进入分隔带 | CBF-03 硬约束（violation_action=block）|
| VTS 报告区自动识别 | Stage 1 ENC 查询扩展：从 S-57/S-101 读取 TSS/VTS 多边形 |

来源：COLREGs Rule 10 原文 + PFRI 2024 + nlm-ask

**发现 3：TSS 内优先权反转**（候选 C v1.1 Stage 2 Rule 18 表未覆盖）

| 场景 | 开阔水域 | TSS 内（Rule 10(j)）|
|------|---------|---------------------|
| 机动船 vs 帆船 | 机动船让帆船 | **帆船不得妨碍车道内机动船** |
| 机动船 vs <20m 小船 | 机动船让小船（Rule 18）| **小船不得妨碍车道内机动船** |

v2.0 Stage 2 Rule 18 优先序需增加 TSS 上下文判断。

### 5.2 候选 C v2.0 TSS 实现方案

**新增插件 `TSSConstraintPlugin`（Stage 3）**：

```yaml
tss_constraint_plugin:
  data_source: "ENC S-57/S-101 TSS polygons"
  constraints:
    - cbf_id: CBF-03
      formula: "dist_to_separation_zone(x) >= 0.1 nm"
      violation_action: block
    - lane_heading_cost:
        weight: w_tss_heading          # 偏离车道方向的代价权重（DEGRADED，HAZID 冻结）
        formula: "theta_deviation^2"
  crossing_mode:
    heading_constraint: "abs(cross_track_heading - 90deg) <= 10deg"
    entry_exit_via_lane_end: true
```

**新增 STL 规则 CHK-06（Checker-T）**：

```yaml
CHK-06:
  spec: "always (in_tss_zone => dist_to_separation_zone_m >= 185)"  # 0.1 nm
  description: "TSS 分隔带禁入（Rule 10(b)）"
  violation_action: block
  layer: T
  active_condition: "scenario_type == coastal_tss"
```

**置信度**：🟢 High（COLREGs Rule 10 原文 E1 + PFRI 2024 E4 + nlm-ask High confidence）

---

## 6. L2/L3 功能切割确认

来源：nlm-ask High confidence + Kongsberg K-MATE / Yara Birkeland 工业参考 + 本会话架构评审

| 战术业务场景 | 归属层 | 依据 |
|------------|-------|------|
| 航次计划、泊位/锚地选择 | L2 | 战略层离线规划 |
| 生成进港标称轨迹（waypoint 序列）| L2 | 静态航路规划 |
| 进港航道动态 COLAV（Rule 9 + 他船）| L3 | 动态障碍，战术层 |
| TSS 车道保持 + Rule 10 执行 | L3 | 动态约束，战术层 |
| VTS 实时指令执行（速度/等待/许可）| L3 | 实时响应，战术层 |
| 引航员接送艇会合机动 | L3 | 动态机动，战术层 |
| VTS 等待时的 Holding Pattern | L3 | 局部执行，战术层 |
| 拖轮会合定位（靠泊前）| L3 | 动态机动，战术层 |
| 靠泊精确定位（DP 控制）| L4 | 精确控制，控制层 |
| 系缆作业 | L4 | 硬件执行，控制层 |
| 锚位评估（底质/水深/旋回空间）| L2 | 战略评估 |
| 进锚航线执行（低速 COLAV）| L3 | 动态障碍，战术层 |

---

## 7. TBD Inventory（Patch D 新增）

| ID | 内容 | 影响 | 关闭路径 |
|----|------|------|---------|
| TBD-PD-01 | CBF 松弛变量权重 $\lambda$（Relaxed CBF）| Stage 3 MPC 可行性 | FCB 水动力仿真 + HAZID |
| TBD-PD-02 | `route_incompatible` 触发：`consecutive_return_failures=3` 和 `return_horizon_blocked_s=300` | B-01 超时策略 | HAZID + 运营场景演练 |
| TBD-PD-03 | Scenario Router `scenario_type` 枚举完整性 | A-05 覆盖度 | CONOPS 场景清单 + ODD 定义 |
| TBD-PD-04 | TSS 车道方向代价权重 `w_tss_heading` | B-02 MPC 调参 | HAZID workshop |
| TBD-PD-05 | Holding Pattern 实现：圆圈 vs 固定点 DP | port_approach 场景 | 与 L4 DP 能力确认 |

---

## 8. 调研来源

| 来源 | 级别 | 用途 |
|------|------|------|
| COLREGs Rule 10 原文（IMO）| E1 | B-02 TSS 形式化依据 |
| Discrete-Time CBF-MPC — UC Berkeley ACC 2021 | E4 | A-02 CBF-MPC 数学基础 |
| Relaxed CBF for Underactuated USVs — IEEE TAC 2024 | E4 | A-02 欠驱动船舶 CBF |
| CLF-CBF for ASVs — NOVA Research Portal 2025 | E4 | A-02 海事 LiDAR 场景验证 |
| Unified MPC+CBF — rkcosner.com 2024 | E4 | A-02 集成可行性 |
| milliAmpere2 Design and Testing — NTNU JOMAE 2024 | E4 | A-02 工业验证 |
| MOOS-IvP Helm Release 24.8 — MIT Marine Autonomy Lab | E3 | A-05 行为仲裁范式 |
| Quantitative Processing of SA for Autonomous Ships — TransNav | E4 | A-05 情景分类框架 |
| TSS Crossing Problem (Rule 10) — PFRI Rijeka 2024 | E4 | B-02 TSS 技术要求 |
| K-MATE Deployment docs + nlm-ask | E3 | B-01 工业先例（无先例确认）|
| Narrow Channel Crossing — ISOPE 2025 | E4 | A-05 narrow_channel 场景 |
| Port Positioning Concepts — Theseus 2024 | E4 | A-05 port_approach 场景 |

---

## 9. Patch D 与 Research v1.2 的合并关系

本 Patch D 与 Research v1.2 合并后构成 **Research v2.0 基线**，供 Spec v2.0 使用：

| 章节 | 来源 | 状态 |
|------|------|------|
| §1-11（核心 COLAV 调研）| Research v1.2 | 全部有效，继承 |
| §12（关键参数）| Research v1.2 | 全部有效，继承 |
| §13（L3 模块结构）| Research v1.2 | 部分更新（Scenario Router 新增）|
| **§14（CBF+MPC 集成）** | **Patch D §2** | **新增** |
| **§15（Scenario Router）** | **Patch D §3** | **新增** |
| **§16（L3→L2 反向通道）** | **Patch D §4** | **新增** |
| **§17（TSS Rule 10）** | **Patch D §5** | **新增** |
| **§18（L2/L3 功能切割）** | **Patch D §6** | **新增（确认）** |

---

*请确认是否冻结本 Patch D 作为 Spec v2.0 的补充真源。冻结后进入 Spec v2.0 BLUF + ADR 表阶段。*
