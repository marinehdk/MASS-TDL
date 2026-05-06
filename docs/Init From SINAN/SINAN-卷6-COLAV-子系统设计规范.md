# 司南系统 (SINAN) COLAV 碰撞规避子系统设计规范

## SINAN Collision Avoidance Subsystem Design Specification

| 属性 | 值 |
|------|-----|
| **文档编号** | SINAN-COLAV-SDD-001 |
| **版本** | v2.1 |
| **密级** | 内部受控 |
| **适用船型** | 45m FCB 运载船（VPM 参数化后可扩展至任意船型） |
| **目标硬件** | 船端: NVIDIA AGX Orin 64GB (JetPack 5.1+) |
| **合规基准** | COLREGs 1972 / CCS《智能船舶规范 (2025)》Nn 级 / IMO MSC.1/Circ.1638 |
| **编制日期** | 2026-04-14 |
| **评审状态** | v2.0 — 基于架构评审补充 Rule 17 完整定义 / MPC 代价函数 / GPU 资源管理 / 船型参数完整表 |
| **上游依赖** | SINAN-INFRA-SDD-001 v3.1 / SINAN-SSL-SDD-001 v2.3 / SINAN-VOL-SDD-001 v2.3 |

---

# 第 1 章　文档概述与架构总览

## 1.1　COLAV 在五层架构中的位置

COLAV（Collision Avoidance，碰撞规避子系统）是 SINAN 五层架构中**安全关键级别最高**的业务子系统。

```
┌─────────────────────────────────────────────────────────────┐
│  接口层 (Interface Layer)                                    │
│  感知适配器 → OwnState / TargetList 话题 → COLAV            │
├─────────────────────────────────────────────────────────────┤
│  子系统层 (Subsystem Layer)                                  │
│  ┌──────────┐  ┌─────────────┐  ┌───────┐  ┌───────┐       │
│  │   VRO    │  │ COLAV ◄本文 │  │  UKC  │  │ BERTH │       │
│  │ 全局规划 │  │ 战术避碰    │  │搁浅预警│  │智能靠泊│       │
│  └────┬─────┘  └──────┬──────┘  └───┬───┘  └───┬───┘       │
│  横向禁止直接调用——通过事件总线和共享内存解耦                  │
├───────┼───────────────┼─────────────┼──────────┼────────────┤
│  共享能力层 (Shared Services)                                │
│  ALM · 数字孪生 · 轨迹平滑器 · 事件总线 · AMS · PCF          │
├─────────────────────────────────────────────────────────────┤
│  基础设施层 (Infrastructure)                                 │
│  VPM Registry（QSD 参数源）· Chart（UKC 地形数据）           │
└─────────────────────────────────────────────────────────────┘
```

**核心职责**：在航行过程中实时检测碰撞风险，生成完全符合 COLREGs 1972 的避碰轨迹，通过 VO 编排层下发执行。

**三条架构铁律**：
1. COLAV 不直接向任何执行器发出指令，所有轨迹经 VO 仲裁后由 VO 统一下发
2. COLAV 不与 VRO/UKC/BERTH 横向直调，通过事件总线和共享内存协作
3. COLAV 受 ALM 权限压制——H1 仅告警显示、H2 建议等确认、H3 自主执行

## 1.2　设计目标与合规基准

| 目标 | 量化指标 | COLREGs 依据 |
|------|---------|-------------|
| 法律合规 | 100% 符合 Rules 5, 6, 7, 8, 13, 14, 15, 16, 17, 18 | COLREGs 1972 |
| 良好船艺 | 让路转向量 ≥ 15°，及早行动（TCPA > 8min） | Rule 8 |
| 实时性 | 全流水线端到端 WCET < 100ms | IEC 61508 实时要求 |
| 可解释性 | 每条避碰指令可追溯至具体 COLREGs 条款 + TCPA/DCPA 数值 | CCS Nn 级审图 |
| 平台独立 | 船型参数完全由 VPM 注册表提供，算法核心不含硬编码船型数据 | 架构原则 |

### 1.2.1　V2V 协同已知局限声明（O7）

SINAN v1.0 不具备船间通信协调避碰能力。每艘搭载 SINAN 的船舶独立运行 COLAV，严格遵守 COLREGs 规则进行单边避碰决策。系统将一切遇遇目标视为无理性、无通讯链接的动态随机代理人，由本船单方面主动规避，不假设也不支持与其他智能船舶协商拆解会遇态势。

**已知局限与缓解措施**：
交叉相遇中，COLREGs 的角色分配（让路/直航）本身解决了双方同时让路的问题。在极端场景（双方同时进入 Rule 17 Stage III 采取独自行动时）下，各自减速比各自盲目转向更为安全，此时系统优先判定减速并触发 AMS Emergency 呼叫船员介入。

**未来路线图（v2.0+）**：引入 V2X / V2V 通信协议（基于 VDES/AIS ASM）→ 实现避碰意图广播 → 多船协同避碰最优求解。

## 1.3　COLAV 四级流水线架构总览

```
输入数据流
  │
  ├─ OwnState (10Hz) ──────────────────────────────────────────┐
  ├─ TargetList (2Hz，Radar+AIS 融合后) ───────────────────────┐│
  ├─ VRO 参考航线 (事件总线订阅) ──────────────────────────────┐││
  ├─ ALM 全局等级 (TRANSIENT_LOCAL 订阅) ──────────────────────┐│││
  │                                                            ││││
  ▼                                                            ││││
┌─────────────────────────────────────────────────────────────┘│││
│  模块 1：QSD 态势感知与威胁评估                               │││
│  ├─ KD-Tree 空间初筛（R_fore × 1.5 半径）                    │││
│  ├─ 向量化 DCPA/TCPA 批量计算（GPU）                         │││
│  ├─ 四元数船舶领域（QSD）侵入判定                             │││
│  └─ 输出：Threat_Event 优先队列                              │││
│     WCET < 2ms（200 目标基准）                               │││
├──────────────────────────────────────────────────────────────┘││
│  模块 2：COLREGs 规则仲裁引擎                                  ││
│  ├─ 会遇态势确定性分类（Rule 13/14/15 白盒决策树）              ││
│  ├─ Rule 17 四阶段状态机（基于 TCPA 时间窗口）                  ││
│  ├─ vessel_type_flag 适配（6 种船型规则差异化）                 ││
│  ├─ 特殊船舶豁免判定（RAM/NUC/CBD/Fishing）                    ││
│  └─ 输出：Action_Constraints（硬约束集合）                     ││
│     WCET < 5ms                                               ││
├───────────────────────────────────────────────────────────────┘│
│  模块 3：混合 VO-MPC 避碰轨迹求解器                             │
│  ├─ VO 速度障碍法初筛（消除 90% 碰撞解空间）                    │
│  ├─ MPC 精算（100 候选 × 3min GPU 并行推演）                    │
│  ├─ UKC 否决扫描（共享内存，并行不阻塞）                        │
│  ├─ DT 引擎 P0 级预验证（COLREGs + 物理安全双保险）             │
│  ├─ 紧急反应 COLAV（TCPA < 1min 跳过 MPC，直接满舵）            │
│  └─ 输出：最优避碰轨迹 + 决策置信度                            │
│     WCET：MPC < 50ms，含 DT < 100ms                           │
└───────────────────────────────────────────────────────────────┘
│  模块 4：复航轨迹生成
│  ├─ QSD_Aft 让清条件确认 + 30s 冷却计数器
│  ├─ LOS 切入点计算（K_rtt × XTE 前瞻距离）
│  ├─ 调用共享层轨迹平滑器（B 样条连续化）
│  └─ 输出：Trajectory_RTT → VO → 控制适配器
└─────────────────────────────────────────────────────────────
```

## 1.4　与其他子系统的完整交互矩阵

| 交互对象 | 方向 | 机制 | 话题/服务 | 内容 |
|---------|------|------|----------|------|
| VRO | COLAV ← 订阅 | 事件总线 | `/events/vro/reference_route` | 参考航线（当前段 + 下一段） |
| VRO | COLAV → 发布 | 事件总线 | `/events/colav/status` | ENGAGED/DISENGAGED/RETURNING |
| UKC | COLAV → 写入 | 共享内存（FastDDS SHM） | `colav_candidates_shm` | 100 条候选轨迹状态序列 |
| UKC | COLAV ← 读取 | 共享内存 | `colav_candidates_shm` | UKC 成本覆写数组 |
| DT 引擎 | COLAV → 请求 | ROS 2 Service | `/sinan/dt/simulate_p0` | 紧急预演请求（P0 优先级） |
| DT 引擎 | COLAV ← 响应 | ROS 2 Service | `/sinan/dt/simulate_p0` | 轨迹碰撞/触底/物理异常判定 |
| ALM | COLAV ← 订阅 | 事件总线 TRANSIENT_LOCAL | `/alm/global_state` | 全局等级 + 行为 B 触发信号 |
| AMS | COLAV → 发布 | 事件总线 RELIABLE | `/ams/events` | COLAV 相关告警事件 |
| VO | COLAV → 发布 | 事件总线 RELIABLE | `/sinan/{vessel_id}/vo/override_requests` | 避碰轨迹抢占请求 |
| PCF | COLAV ← 订阅 | 事件总线 | `/telemetry/system_confidence` | σ_system（置信度不足时提升 CPA 阈值） |

---

# 第 2 章　业务生命周期

## 2.1　ROS 2 生命周期状态定义

COLAV 作为 ROS 2 LifecycleNode，由 VO 航次编排层统一管理节点状态。

| 状态 | 资源占用 | 行为描述 | VO 阶段 |
|------|---------|---------|---------|
| **UNCONFIGURED** | 最小（仅节点注册） | 节点已创建，参数未加载 | S0 在港休眠 |
| **INACTIVE** | 低（参数加载完毕） | 传感器已订阅，GPU Context 预创建，不处理数据 | S1 离港准备 |
| **MONITORING** | 中（模块 1 常驻运行） | QSD 持续计算 CRI，阈值未触发时不生成轨迹，HMI 可见态势信息 | S2-S7 非避碰状态 |
| **ENGAGED** | 高（全流水线激活） | 模块 1-3 全速运行，正在生成和执行避碰轨迹 | 检测到碰撞风险 |
| **RETURNING** | 中（模块 4 运行） | 避碰完成，复航轨迹跟踪中，VRO 待唤醒 | 目标让清后 |

**重要约束**：S2-S6 航行期间，COLAV 禁止进入 UNCONFIGURED 或 INACTIVE 状态。最低维持 MONITORING，确保 COLREG Rule 2/5 要求的随时可用性。

## 2.2　状态转换条件与时序

```
UNCONFIGURED ──[on_configure: 加载 VPM + vessel_type_config]──► INACTIVE
INACTIVE ──[VO: S2 进入, GPU Context 预热完成]──► MONITORING
MONITORING ──[任意目标 CRI > CRI_threshold=1.0]──► ENGAGED
ENGAGED ──[所有威胁目标 CRI < 0.5 且持续 30s]──► RETURNING
RETURNING ──[XTE < 50m（约半船宽）且 XTE 单调收敛]──► MONITORING
ENGAGED ──[ALM 降级请求：行为 B 协议]──► 完成当前避碰段（≤30s）→ MONITORING
MONITORING/ENGAGED ──[S0 进入]──► INACTIVE
```

## 2.3　与 VO 编排的抢占协议

COLAV 进入 ENGAGED 状态时，通过严格的握手协议抢占 VRO 控制权：

```
COLAV ENGAGED 触发
  │
  ├─ Step 1: 发布 EVENT_COLAV_ENGAGED 到 /sinan/{vessel_id}/vo/override_requests
  │          payload: { source: "COLAV", priority: 4, trajectory: ... }
  │
  ├─ Step 2: VO 接收后执行 Cmd MUX 切换
  │          挂起 VRO nav_command 发布权
  │          COLAV 轨迹获得底盘控制权
  │
  ├─ Step 3: 避碰执行中（ENGAGED/RETURNING）
  │          COLAV 每 200ms 更新避碰轨迹（在线滚动优化）
  │
  └─ Step 4: 发布 EVENT_COLAV_DISENGAGED
             VO 发出 CMD_VRO_RESUME
             VRO 从当前位置重新计算航段
             COLAV 释放 40% GPU 算力
```

---

# 第 3 章　模块一：QSD 态势感知与威胁评估

## 3.1　四元数船舶领域（QSD）精确参数化

QSD 是一个由本船实时水动力参数驱动的**偏心多边形保护域**，而非简单的同心圆。域的大小随航速、装载状态和操纵性能实时变化。

### QSD 四方向半径精确计算公式

**正前方（$R_{fore}$）** — 最关键，保证本船有足够的冲程余量：
$$R_{fore} = S_{stop}(V) + V \times T_{react} + L_{oa}$$
其中：
- $S_{stop}(V)$：当前航速下的制停冲程（来自 VPM `get_maneuvering_limits`）
- $T_{react} = 60s$（操作员反应时间余量，CCS Nn 要求）
- $L_{oa} = 45m$（本船全长，来自 VPM 静态参数）

**右舷（$R_{stbd}$）** — 受 COLREGs 右转偏好加权：
$$R_{stbd} = R_{min\_turn} + \beta_{COLREGs} \times V$$
其中 $\beta_{COLREGs} = 15.0$（COLREGs 右转偏好系数），$R_{min\_turn}$来自 VPM。

**左舷（$R_{port}$）** — 最小旋回半径：
$$R_{port} = R_{min\_turn}$$

**正后方（$R_{aft}$）** — 被追越基本安全距离：
$$R_{aft} = L_{oa} + 0.5 \times R_{min\_turn}$$

### RAM 拖带工况的定向扩展（强制要求）

拖带工况不得对四方向 QSD 做等比例放大。对于 45m FCB，后向危险区必须显式纳入作业设备几何：

$$R_{aft,RAM} = \max\left(R_{aft},\ L_{towline} + L_{towed\_object} + M_{ram}\right)$$

其中：

- $L_{towline}$：拖缆有效长度
- $L_{towed\_object}$：被拖船 / 被拖构件特征长度
- $M_{ram}$：安全余量，当前版本取 $\max(50m, 0.5 \times L_{oa})$

其余方向采用保守但非对称放大：

- $R_{fore,RAM} = 1.2 \times R_{fore}$
- $R_{stbd,RAM} = 1.2 \times R_{stbd}$
- $R_{port,RAM} = 1.0 \times R_{port}$

**设计目的**：避免把 RAM 状态简化为“全方向 ×1.5”，从而低估追越船对拖缆尾端和被拖物的威胁。

### 45m FCB 典型 QSD 参数（参考值，以 VPM 输出为准）

| 航速 (kn) | $R_{fore}$ (m) | $R_{stbd}$ (m) | $R_{port}$ (m) | $R_{aft}$ (m) |
|----------|--------------|--------------|--------------|-------------|
| 4 | 820 | 180 | 120 | 75 |
| 8 | 1050 | 240 | 120 | 75 |
| 12 | 1450 | 300 | 120 | 75 |

*注：$R_{port}$ 和 $R_{aft}$ 仅受船体物理约束，不随航速大幅变化*

### 3.1.4　VPM v1.1 集成：非对称 QSD 与 CRI 动态调整（新）

> **追溯说明**：本小节参照卷4 §5A Cmd MUX 的 UKC 优先级上升决策，引入 VPM 推荐航向的 QSD 非对称化。
> **改动依据**：卷0 ADR-002（VPM 基础设施集成），卷2 §2.18（参数版本化）。

#### VPM 推荐航向作为 QSD 参考起点

VPM v1.1 提供 `/vpm/dt/p0_recommendation` 话题，发布数字孪生模块的短期最优航向预测（频率 ~10Hz）。COLAV 在生成避碰轨迹前，使用该推荐航向作为起始参考点，而非从当前船艏方向计算：

$$\theta_{ref} = \theta_{VPM\_P0} \quad \text{（而非 }\theta_{ego}\text{）}$$

**设计目的**：VPM P0 已考虑当前水流、浅滩影响，以及与规划航线的偏差，比船舶当前航向更准确地反映"理想下一步"。COLAV 避碰决策基于此参考点，可降低因航向追踪滞后而产生的虚假威胁判断。

#### 非对称 QSD：CRI 动态时间权重

传统 QSD 是对称多边形（四方向半径固定）。新版本引入 CRI 时间依赖的非对称调整：

$$R_{QSD}(\theta_{target}, CRI) = R_{base}(\theta_{target}) \times \left(1 + k_{CRI} \times \frac{CRI - 1.0}{CRI_{max}}\right)$$

其中：
- $R_{base}(\theta_{target})$：原四方向基础半径（线性插值）
- $k_{CRI}$：非对称扩展系数，建议 $k_{CRI} = 0.3$（扩展 ≤ 30%）
- $CRI_{max} = 3.0$（饱和阈值）

| CRI 范围 | 非对称扩展 | 解释 |
|---------|-----------|------|
| CRI < 0.5 | 无（R = R_base） | 无风险，QSD 保持标准 |
| 0.5 ~ 1.0 | R × (1.0 + (CRI-1)/2) → 约 R × 0.75 | 预警，QSD 收缩以强化初期判定 |
| 1.0 ~ 2.0 | R × (1.0 + (CRI-1)/3) → 约 R × 1.33 | 威胁，QSD 前向扩展，后向缩小 |
| CRI > 2.0 | R × (1.0 + 0.3) = R × 1.3（饱和） | 紧急，QSD 最大不对称性 |

**前后不对称性的具体实现**：

前向 ($\theta \in [350°, 10°]$) 更激进扩展：
$$R_{fore,asym} = R_{fore} \times (1 + k_{CRI}^{front} \times factor)$$
其中 $k_{CRI}^{front} = 0.4$（比基础系数更大）

后向 ($\theta \in [170°, 190°]$) 保守缩小：
$$R_{aft,asym} = R_{aft} \times (1 - k_{CRI}^{aft} \times factor)$$
其中 $k_{CRI}^{aft} = 0.15$（减少后方保护域宽度，给本船回旋空间）

#### 与 Rule 17 的配合

Rule 17 四阶段状态机（见卷6 §4）的各阶段触发条件与 CRI 阈值关联：

| Rule 17 阶段 | 触发 CRI 范围 | QSD 调整 | 说明 |
|-----------|-------------|--------|------|
| **Stage I（及早行动）** | 0.5 ~ 1.0 | 前向 -20%, 后向 +0% | 有时间充裕，QSD 略小 |
| **Stage II（采取行动）** | 1.0 ~ 1.5 | 前向 +10%, 后向 -10% | 风险上升，前向守卫加强 |
| **Stage III（独自采取行动）** | 1.5 ~ 2.0 | 前向 +30%, 后向 -20% | 紧急避碰，前向大幅扩展 |
| **Stage IV（最后手段停船）** | CRI > 2.0 | 前向 +30%, 后向 -30% | 极端情况，优先停船 |

#### VDR 审计记录

每 10Hz COLAV 周期记录：
```json
{
  "timestamp_ns": 1684593847500000,
  "qsd_mode": "ASYMMETRIC_VPM_INTEGRATED",
  "vpm_reference_heading_deg": 45.2,
  "cri_value": 1.35,
  "qsd_front_radius_m": 1550,
  "qsd_aft_radius_m": 62,
  "asymmetry_factor": 0.21,
  "cri_zone": "STAGE_II",
  "qsd_version": "v2.1_asymmetric_cri"
}
```

---

## 3.2　DCPA/TCPA 向量精确计算

$$\vec{V}_{rel} = \vec{V}_{target} - \vec{V}_{ego}$$
$$\vec{P}_{rel} = \vec{P}_{target} - \vec{P}_{ego}$$
$$TCPA = -\frac{\vec{P}_{rel} \cdot \vec{V}_{rel}}{|\vec{V}_{rel}|^2} \quad \text{（若 TCPA < 0 表示已过最近会遇点）}$$
$$DCPA = |\vec{P}_{rel} + \vec{V}_{rel} \times TCPA|$$

当 $\vec{V}_{rel} \approx 0$（平行同速航行）时，TCPA → ∞，当前距离即为 DCPA，无碰撞风险。

## 3.3　碰撞风险指数（CRI）

$$CRI = \frac{R_{QSD}(\theta_{target})}{DCPA} \times \frac{T_{limit}}{max(TCPA,\ \epsilon)}$$

其中：
- $R_{QSD}(\theta_{target})$：目标所在方位角 $\theta_{target}$ 对应的 QSD 半径（线性插值四方向半径）
- $T_{limit} = 600s$（10 分钟，COLREGs 推荐及早行动的时间基准）
- $\epsilon = 1s$（防止除零）

| CRI 值 | 威胁等级 | 系统响应 |
|--------|---------|---------|
| < 0.5 | 无风险 | 忽略 |
| 0.5 ~ 1.0 | 预警 | HMI 黄色高亮，AMS Caution |
| > 1.0 | **威胁，触发模块 2** | ENGAGED 状态，全流水线激活 |
| > 2.0 | 高危 | 同时提升 MPC 求解优先级 |

## 3.4　计算加速：两级过滤架构

**第一级 KD-Tree 初筛（CPU，O(log n)）**
- 空间邻近过滤：仅处理在 $R_{fore} \times 1.5$ 半径内的目标
- 港口密集场景（200+ 目标）→ 通常仅 10-20 个需要精算
- WCET < 0.5ms

**第二级 GPU 张量化精算**
- 将通过初筛的目标构造 `[N_target, state_dim]` 张量
- CUDA Kernel 批量并行计算 DCPA/TCPA/CRI
- N_target ≤ 50 时，WCET < 2ms

**超载保护（当目标数 > 50 时）**：
- 按距离排序，优先保留最近 50 个目标
- 向 AMS 发布 Warning "目标数超过 COLAV 精算上限"
- H3 模式下此情况禁止保持，自动触发 ALM→H2

## 3.5　输出消息定义

```
# ThreatEvent.msg
# 发布话题: /sinan/{vessel_id}/colav/threat_events
# QoS: RELIABLE + VOLATILE + DEADLINE=100ms

uint64   timestamp_ns
uint64   target_id              # MMSI 或雷达跟踪号
uint8    target_source          # 1:RADAR 2:AIS 3:FUSED
float32  relative_bearing_deg   # 相对方位角（0=正前，顺时针）
float32  heading_crossing_angle # 航向交叉角（CA）
float32  speed_ratio            # V_target / V_ego
float32  dcpa_m                 # 最近会遇距离（m）
float32  tcpa_s                 # 到达最近会遇时间（s）
float32  cri                    # 碰撞风险指数
float32  current_distance_m     # 当前距离（m）
float32  target_confidence      # PCF 目标置信度（0-1）
bool     is_dark_target         # 无 AIS 确认的纯雷达目标
```

---

# 第 4 章　模块二：COLREGs 规则仲裁引擎

## 4.1　设计原则：100% 白盒确定性规则树

**严禁**在仲裁引擎中使用任何机器学习、模糊逻辑或随机算法。所有会遇分类和角色分配必须可追溯至 COLREGs 具体条款和量化阈值。

理由：CCS Nn 级认证要求系统决策可解释、可审计、可重复——任何黑盒成分都会导致审图拒绝。

## 4.2　会遇态势确定性分类

基于相对方位角（RB）和航向交叉角（CA）的完整分类矩阵：

| 会遇态势 | COLREGs 规则 | RB 范围（目标相对本船）| CA 约束 | 本船角色 | 典型场景 |
|---------|-------------|---------------------|--------|---------|---------|
| **正面对遇** | Rule 14 | 355°~005° | 170°~190° | 让路（双方均右转） | 迎头航行 |
| **右舷交叉** | Rule 15 | 005°~112.5° | 任意 | **让路** | 目标从右舷横穿 |
| **左舷交叉** | Rule 15 | 247.5°~355° | 任意 | **直航** | 目标从左舷横穿 |
| **前方追越** | Rule 13 | 005°~175° | CA < 22.5° 且 V_tgt > V_ego | 让路 | 追越前方慢船 |
| **被追越** | Rule 13 | 112.5°~247.5° | CA < 67.5° 且 K > 1 | **直航** | 被后方快船追越 |
| **精确正前** | Rule 14 特例 | 005°~355°（CA ≈ 180°） | 170°~190° | 让路 | 视为对遇处理 |

**K 值定义（追越判定）**：$K = V_{target\_along\_ego\_track} / V_{ego}$，K > 1 表示对方沿本船航迹方向速度大于本船，即被追越。

## 4.3　约束生成：Action_Constraints

仲裁引擎输出 `Action_Constraints` 结构，作为模块 3 MPC 的硬约束输入：

| 本船角色 | 生成约束 | COLREGs 依据 |
|---------|---------|------------|
| **让路（对遇）** | 禁止左转 + 转向量 ≥ 15° + 拓扑=从对方尾部过 | Rule 14 + Rule 8(b) |
| **让路（右舷交叉）** | 禁止左转 + 禁止加速 + 拓扑=从目标尾部绕行 | Rule 16 + Rule 8(b) |
| **直航（左舷交叉）** | 航向偏差 < 2° + 航速偏差 < 0.5kn | Rule 17a-i |
| **让路（追越他船）** | 禁止与目标船同侧转向（不允许并行切速） | Rule 13 |
| **直航（被追越）** | 航向航速保持 + Rule 17 降级状态机生效 | Rule 13 + Rule 17 |

## 4.4　Rule 17 四阶段状态机（完整定义）

Rule 17 管理"直航船"在让路船不采取行动时的行为升级。这是 COLAV 仲裁引擎中**最复杂、最安全关键**的部分。

### 4.4.1　触发前提

Rule 17 状态机仅在本船角色为 STAND_ON（直航船）时激活。激活条件：
- CRI > 1.0（威胁确认）
- 目标持续保持原航向航速（对方不采取让路行动），判断依据：$|\Delta \psi_{target}| < 5°$ 且 $|\Delta V_{target}| < 0.5kn$，连续 60s

### 4.4.2　四阶段完整定义

**阶段 Ⅰ——绝对直航（Rule 17a-i）**

触发条件：TCPA > 8min 且 DCPA > $0.3 \times R_{QSD}$

约束：
- 航向偏差绝对值 < 2°
- 航速偏差绝对值 < 0.5kn
- 禁止 COLAV 生成任何机动轨迹
- AMS: Caution "目标接近，保持直航"

---

**阶段 Ⅱ——允许引起注意的微小机动（Rule 17a-ii）**

触发条件：TCPA ≤ 8min 且 CRI > 1.2 且对方 60s 内未行动

约束（解除 Ⅰ 的部分限制）：
- 允许 ±5° 以内的航向调整（引起注意）
- 允许 ±1kn 的航速调整
- 禁止实质性的让路机动（不能转向绕行）
- AMS: Warning "对方未采取行动，小幅调整引起注意"
- HMI: 橙色高亮目标船 + "请关注目标 [MMSI/ID]"

---

**阶段 Ⅲ——独自避碰（Rule 17b）**

触发条件：TCPA ≤ 4min 且 DCPA < $0.5 \times R_{QSD}$ 且仅凭对方行动已无法避免危险（数字孪生仿真确认）

约束（完全开放避碰权限）：
- 允许任意机动（转向、减速、加速）
- **唯一禁止**：禁止向左转向（Rule 17c 明确禁止，因为这会切入让路船的规避路径）
- MPC 以最小 CPA 最大化为主目标，Rule 17c 约束以无穷大惩罚强制执行
- AMS: Alarm "Rule 17b 独自避碰——对方未让路，本船主动规避"
- HMI: 红色全屏提示，通知值班船员

---

**阶段 Ⅳ——极度危险紧急脱离（Rule 17c 极端）**

触发条件：TCPA ≤ 1min 或 DCPA < $0.1 \times R_{QSD}$（即将碰撞）

约束（放弃 MPC 优化，执行紧急反应 COLAV）：
- **跳过 MPC**，直接执行预定义的紧急脱离策略（详见第 5 章 §5.5）
- 满舵右转 + 主机全速（FCB：三桨+双侧推协调响应）
- 同时触发 MRC 请求（VO 仲裁是否执行）
- AMS: Emergency "极度危险——紧急脱离"
- GMDSS: 广播危险预警

### 4.4.3　状态机时序图

```
TCPA > 8min
   │ ─────────────── 阶段 Ⅰ：绝对直航 ─────────────────
   │ AMS: Caution
   │
TCPA = 8min, 对方未行动 60s
   │ ─────────────── 阶段 Ⅱ：引起注意的微小机动 ───────
   │ AMS: Warning | 允许 ±5° / ±1kn
   │
TCPA = 4min, 仅靠对方无法避碰
   │ ─────────────── 阶段 Ⅲ：独自避碰 ─────────────────
   │ AMS: Alarm | MPC 全权限（禁止左转）
   │
TCPA = 1min, 即将碰撞
   └─────────────── 阶段 Ⅳ：紧急脱离 ──────────────────
     AMS: Emergency | 满舵右转 + MRC 请求
```

### 4.4.4　阶段降级保护

阶段提升是单向不可逆的（危险情况不允许回退到更宽松的阶段）。但如果目标船采取了让路行动（$|\Delta\psi_{target}| > 15°$ 或 DCPA 开始增大），允许在 60s 稳定观察后重置为 Ⅰ 阶段。

## 4.5　特殊船舶豁免与规则适配（vessel_type_flag）

COLAV 通过 `vessel_type_flag` 实现对不同类型船舶的差异化规则处理。该标志由 VO 在船型变更时注入，由 AIS 报文中的航行状态（Navigation Status）字段辅助确认。

### 4.5.1　六种船型的完整参数差异表

| vessel_type_flag | AIS 航行状态码 | COLREGs 分类 | QSD 倍率 | CPA 阈值 | 可用避让动作 | Rule 17 Stage Ⅲ 触发 | 对方豁免让路 |
|-----------------|--------------|------------|--------|---------|------------|---------------------|-----------|
| **POWER_DRIVEN** | 0 (在航) | 机动船 | 1.0× | 0.5 NM | 转向 + 减速 + 停车 | TCPA < 4min | 否 |
| **RAM** | 3 (受限操纵) | 操纵受限船 Rule 18(a) | **前/右=1.2×，左=1.0×，后向=拖带几何扩展** | 1.0 NM | **仅减速 + 停车**（转向受拖缆限制） | TCPA < 8min（更早行动） | **是**（对方应让路本船） |
| **NUC** | 2 (失去控制) | 失控船 Rule 18(a) | 2.0× | 2.0 NM | **无**（不主动避碰） | N/A | **是**（最高优先被让路） |
| **CBD** | 4 (受吃水限制) | 吃水受限船 Rule 18(d) | 1.3× | 0.8 NM | 仅减速（不能改航） | TCPA < 6min | 是（在深水航路内） |
| **SAILING** | 8 (帆船) | 帆船 Rule 12 | 1.2× | 0.6 NM | 转向 + 减速 | TCPA < 4min | 否（对机动船） |
| **FISHING** | 7 (从事捕鱼) | 捕鱼船 Rule 18(c) | 1.4× | 0.8 NM | 减速 + 小幅转向 | TCPA < 5min | 是（在作业中） |

### 4.5.2　vessel_type_flag 注入时序

```yaml
# vessel_type_config.yaml（可热加载，修改须经 VDR 版本记录）

current_vessel_type: "POWER_DRIVEN"

type_configs:
  POWER_DRIVEN:
    qsd_multiplier: 1.0
    cpa_threshold_nm: 0.5
    give_way_exempt: false
    available_evasion: ["TURN_STARBOARD", "TURN_PORT", "SLOW_DOWN", "STOP"]
    rule17_stage3_tcpa_min: 4
    ais_nav_status: 0
    colregs_rules: [8, 13, 14, 15, 16, 17]

  RAM:
    qsd_fore_multiplier: 1.2
    qsd_starboard_multiplier: 1.2
    qsd_port_multiplier: 1.0
    qsd_aft_mode: "TOW_GEOMETRY"
    tow_safety_margin_m: 50
    cpa_threshold_nm: 1.0
    give_way_exempt: true
    available_evasion: ["SLOW_DOWN", "STOP"]      # 禁止转向
    rule17_stage3_tcpa_min: 8
    ais_nav_status: 3
    colregs_rules: [8, 18]

  NUC:
    qsd_multiplier: 2.0
    cpa_threshold_nm: 2.0
    give_way_exempt: true
    available_evasion: []                          # 不主动避碰
    rule17_stage3_tcpa_min: null
    ais_nav_status: 2
    auto_mrc: true
    colregs_rules: [18]
```

  **运行时热切换**：拖轮挂缆时，船长 HMI 切换"作业模式" → ALM→H1 → 事件总线广播 `vessel_type_changed: RAM` → COLAV 在 100ms 内热加载 RAM 参数集，AIS 广播更新为受限操纵状态（航行状态码 3）并附加拖带安全相关文本。

## 4.6　输出消息定义

```
# ActionConstraints.msg
# 发布话题: /sinan/{vessel_id}/colav/action_constraints

uint64   timestamp_ns
uint64   encounter_id             # 本次会遇唯一标识
uint64   primary_target_id        # 主威胁目标 ID
uint8    current_rule_applied     # 13/14/15/17
uint8    ego_role                 # 1:GIVE_WAY 2:STAND_ON 3:ESCAPE
uint8    rule17_stage             # 1/2/3/4（仅 STAND_ON 时有效）
bool     port_turn_allowed
bool     stbd_turn_allowed
bool     deceleration_allowed
bool     acceleration_allowed
float32  min_delta_heading_deg    # Rule 8 最小转向幅度
float32  max_heading_variance_deg # 最大允许航向偏差
float32  max_speed_variance_kts   # 最大允许航速偏差
uint8    required_topology        # 0:ANY 1:PASS_AHEAD 2:PASS_ASTERN
string   rule_citation            # 供 VDR 记录的 COLREGs 条款引用
```

---

# 第 5 章　模块三：混合 VO-MPC 避碰轨迹求解器

## 5.1　架构设计：两阶段混合优化

模块 3 采用 VO 法初筛 + MPC 精算的两阶段架构。VO 法在速度空间中快速消除不可行解，MPC 在可行空间内寻找最优解。

```
速度空间 (VO 法)                轨迹空间 (MPC 精算)
┌──────────────────────┐        ┌──────────────────────────┐
│ 碰撞圆锥计算          │        │ 100 条候选控制序列生成    │
│ 叠加 Action_Constraints│──────►│ Nomoto 4-DOF 并行推演    │
│ 输出: 可行速度窗口    │        │ UKC 否决扫描             │
│ WCET: ~10ms         │        │ 代价函数评估              │
└──────────────────────┘        │ WCET: ~50ms             │
                                └──────────────────────────┘
```

### 5.1.1　模型精度等级与 DT 复核边界

COLAV 的快速候选筛选允许使用简化操纵模型，但最终可发布轨迹不得仅依赖 Nomoto 近似。当前版本采用双层精度策略：

1. **GPU 批量筛选层**：使用简化模型进行 100 条候选控制序列的快速并行推演。
2. **DT 最终复核层**：候选最优轨迹必须通过共享层 DT 引擎的 Fossen 4-DOF 预演复核后，方可形成 OverrideRequest。

以下任一条件成立时，Fossen 4-DOF 复核为**强制**：

- 预测转向量 `|Δψ| > 15°`
- 浅水 / 近岸 / 岸壁效应显著
- `VPM Grade = C`
- 预计 CPA 裕量 < 0.2 NM

若简化模型与 Fossen 复核结果在 CPA / DCPA 上偏差 > 10%，则该候选轨迹直接作废，COLAV 回退为更保守的减速 / 停车策略。

## 5.2　VO 速度障碍法初筛

**碰撞圆锥（Velocity Obstacle, VO）**定义为：本船为避免与目标船在时域 $[t_0, t_0+T]$ 内碰撞，所有不可选取的本船速度向量集合。

$$VO_{ego}^{target} = \{ \vec{u}_{ego} : \exists t \in [t_0, T], |\vec{P}_{rel}(t)| < D_{safe} \}$$

其中 $D_{safe} = R_{QSD}(\theta)$（QSD 半径，随目标方位角变化）。

**多目标叠加**：当存在多个威胁目标时，所有目标的 VO 集合取并集，形成复合禁用速度空间 $VO_{total} = \bigcup_{i} VO_{ego}^{target_i}$。

**约束叠加**：将 `Action_Constraints` 中的硬约束（如禁止左转）映射到速度空间并覆盖。

**输出**：可行速度窗口 $\mathcal{V}_{feasible}$，约消除 90% 不可行解。

## 5.3　MPC 代价函数（完整定义）

在可行速度窗口内，生成 100 组候选控制序列，使用 Nomoto 简化模型推演 $T_{horizon} = 180s$，评估以下代价函数：

$$J = \omega_1 J_{safety} + \omega_2 J_{deviation} + \omega_3 J_{control} + \omega_4 J_{rule} + \omega_5 J_{ukc}$$

### 各项代价定义与权重

**$J_{safety}$（安全代价）**——最小化与威胁目标的碰撞风险：
$$J_{safety} = \sum_{i} \max\left(0,\ 1 - \frac{DCPA_i}{R_{QSD,i}}\right)^2$$
权重 $\omega_1 = 100$（最高权重，安全第一）

**$J_{deviation}$（偏航代价）**——最小化对原计划航线的偏离：
$$J_{deviation} = \sum_{k=1}^{N} XTE_k^2 + ETA\_delay_k^2 \cdot \lambda_{ETA}$$
权重 $\omega_2 = 10$；$\lambda_{ETA} = 0.1$（ETA 延误权重较小，安全优先）

**$J_{control}$（操控代价）**——最小化操舵和变速的幅度（平顺性、燃油）：
$$J_{control} = \sum_{k=1}^{N} \Delta\delta_k^2 + \Delta V_k^2$$
权重 $\omega_3 = 1$

**$J_{rule}$（规则代价）**——对违反 `Action_Constraints` 的轨迹施加天罚：
$$J_{rule} = \begin{cases} +\infty & \text{if violated any hard constraint in Action\_Constraints} \\ 0 & \text{otherwise} \end{cases}$$
权重 $\omega_4 = +\infty$（强制约束，硬天花板）

**$J_{ukc}$（UKC 否决代价）**——整合 UKC 模块的触底惩罚：
$$J_{ukc} = \sum_{k=1}^{N} UKC\_penalty_k \quad \text{（由 UKC 模块写入共享内存）}$$
权重 $\omega_5 = 1000$（触底轨迹等效不可选）

### 权重调整条件

| 场景 | 权重调整 | 理由 |
|------|---------|------|
| PCF σ_system < 0.7 | $\omega_1$ × 1.5，CPA 阈值扩大 | 感知不确定时更保守 |
| VPM Grade C | $\omega_3$ / 2 | 模型不准时允许更大操控 |
| RAM 状态 | $\omega_3$ × 5（惩罚大幅转向） | 拖缆限制下不能剧烈转向 |

## 5.4　GPU 资源管理与算力保障

### CUDA Stream 优先级配置

COLAV 使用最高优先级 CUDA Stream，**不可被 VRO 的 NSGA-III 等低优先级任务抢占**：

```cpp
// COLAV 初始化时创建高优先级 Stream
int leastPriority, greatestPriority;
cudaDeviceGetStreamPriorityRange(&leastPriority, &greatestPriority);
cudaStreamCreateWithPriority(&colav_stream_, 
                              cudaStreamNonBlocking,
                              greatestPriority);  // 最高优先级
```

### GPU 预算保障

| 模块 | GPU 占比 | 优先级 | 抢占保护 |
|------|---------|--------|---------|
| COLAV 全流水线 | 40% | 最高 | **不可被抢占** |
| DT 引擎 P0（为 COLAV 服务）| 20% | 高 | 不可被 VRO 抢占 |
| VRO NSGA-III | 20% | 正常 | 可被 COLAV/DT-P0 抢占 |
| DT 引擎 P1（为 VRO 服务）| 15% | 低 | 可被更高优先级抢占 |
| 系统保留 | 5% | — | OOM 缓冲 |

### WCET 监控与超时处理

```
主循环 100ms WCET 看门狗:
  ├─ 90ms: 正常完成 → 发布避碰轨迹
  ├─ 90~100ms: 接近超时 → 压缩候选数量至 20
  └─ > 100ms: 超时
      ├─ 使用上一帧缓存轨迹（最多 3 帧/600ms）
      ├─ AMS: Alarm "COLAV WCET 超时"
      └─ 第 3 次超时: ALM 降级 H2 + 人工接管提示
```

## 5.5　紧急反应 COLAV（Reactive Escape）

当 Rule 17 进入**阶段 Ⅳ**（TCPA ≤ 1min）时，跳过 MPC 优化，直接执行预定义的紧急脱离策略：

```
紧急反应 COLAV 执行序列:

FCB 多推进器特化 (45m 运载船):
  Step 1: 三桨+双侧推协调转向（全力右转向量）
  Step 2: 三桨同时最大转速输出
  Step 3: 通过 VO 发送 EMERGENCY 优先级 NavCommand
  Step 4: 同时向 ALM 请求 MRC（VO 仲裁是否执行）
  Step 5: AIS 广播受限操纵状态；若推进或舵机实际失效，再升级为 NUC

预期效果: 45m FCB 在 30s 内转向 90°（约 1/4 旋回圈），
          脱离即将碰撞的几何路径
```

---

# 第 6 章　模块四：复航轨迹生成

## 6.1　让清条件：四项缺一不可

| 条件 | 判断方法 | 说明 |
|------|---------|------|
| 空间让清 | 目标距离 > $R_{aft}$（QSD 后方域） | 目标已脱离本船保护域 |
| 方位稳定 | 目标相对方位变化率 < 0.5°/min，持续 30s | 防止目标仍在转向追来 |
| CRI 趋零 | CRI < 0.5，持续 30s | 碰撞风险已消除 |
| 冷却计数 | 让清确认后计时 30s | 防止过早复航又陷入同一会遇 |

## 6.2　复航切入点计算（LOS Merge）

**前瞻距离**（切入点相对本船的距离）：
$$D_{lookahead} = K_{rtt} \times XTE_{current} + V_{ego} \times T_{buffer}$$
- $K_{rtt} = 3.0 \sim 5.0$（复航平缓系数，远洋取 5.0，港口取 3.0，来自 VO 阶段配置）
- $T_{buffer} = 30s$（过渡时间缓冲）

**切入角硬约束**：$\alpha_{merge} \leq 30°$（Rule 8 良好船艺要求过渡平顺）

## 6.3　与共享层轨迹平滑器的职责分工

| 职责 | 负责方 | 内容 |
|------|--------|------|
| 决定**在哪里切入**原航线 | COLAV 模块 4 | 计算 $P_{merge}$ 坐标 |
| 决定**如何画曲线** | 共享层轨迹平滑器 | B 样条连续化，保证 $C^2$ 连续 |
| 验证曲线物理可执行性 | VPM（通过 DT 引擎） | 最大转向率、最大加速度约束 |

## 6.4　控制权交接完整时序

```
XTE 收敛监测 (1Hz):
  ├─ |XTE| < 50m 且 XTE 单调减少 → 触发交接流程
  │
  ├─ Step 1: COLAV 向事件总线发布 EVENT_COLAV_DISENGAGED
  ├─ Step 2: VO 接收后发出 CMD_VRO_RESUME 指令
  ├─ Step 3: VRO 从当前坐标重新计算航段 (L3 局部重规划)
  ├─ Step 4: VO Cmd MUX 切换回 VRO 控制权
  ├─ Step 5: COLAV 模块 3/4 进入 MONITORING 状态
  └─ Step 6: COLAV 释放 40% GPU 占比 → VRO 可临时借用
```

---

# 第 7 章　多目标复杂场景处理

## 7.1　港口高密度场景（≥5 个威胁目标）

港口是 COLAV 最复杂的运行场景。多目标同时逼近时，模块 3 的 MPC 面对多约束叠加。

**处理策略**：

**优先级排序**：按 $\min(TCPA_i)$ 升序处理，最急迫目标优先约束注入 MPC。

**约束叠加顺序**：所有 `Action_Constraints` 取交集。若交集为空（例如多条渔船不按套路逼近导致可行解空间整体死锁）：

#### 7.1.1　约束死锁检测

MPC 在每个求解周期（100ms）检查可行性标志。约束死锁的判定条件为：

```
DEADLOCK := (MPC_solver_status ∈ {INFEASIBLE, MAX_ITER_REACHED})
            AND (N_active_constraints ≥ 3)
            AND (连续 3 个求解周期不可行)
```

检测到 DEADLOCK 后，立即进入 §7.1.2 应急降级模型。

#### 7.1.2　最小碰撞动能应急降级模型

**设计意图**：当理想的全合规无碰解不存在时，系统不躺平等死，而是切换到**损害最小化**目标函数。

**优化目标**：

$$
\min_{\mathbf{u}} \; J_{emergency} = \sum_{i=1}^{N_{threat}} w_i \cdot \frac{1}{2} m_{own} \| \mathbf{v}_{rel,i}(\mathbf{u}) \|^2 \cdot \cos^2(\theta_{impact,i})
$$

其中：
- $\mathbf{u} = [n_{rpm,stbd},\; n_{rpm,ctr},\; n_{rpm,port},\; T_{bow},\; T_{stern}]^T$ 为推进器控制向量（**三轴转速 + 双侧推推力**）⟵ [P0-1修正 2026-04-21，原公式使用 ASD 方位角格式]
- $\mathbf{v}_{rel,i}$ 为本船相对第 $i$ 个威胁目标的相对速度向量
- $\theta_{impact,i}$ 为预测碰撞角（0°=正碰，90°=擦碰切向），$\cos^2$ 权重使系统**偏好切向擦碰**而非正碰
- $w_i = \frac{1}{TCPA_i + \epsilon}$ 为时间紧迫性权重（TCPA 越小权重越大）
- $m_{own}$ 为本船质量（常量，可省略但保留以明确物理意义）

**硬约束**：

$$
V_{own} \geq V_{min\text{-}steerage} = 3 \text{ kn}
$$

**拒绝完全停船**：降速至维持基本舵效的低航速 3kn，保有水动力控制权，防止在风浪中丧失机动性。

**软约束（按优先级逐级松弛）**：

| 松弛优先级 | 约束 | 松弛条件 |
| ---------- | ---- | -------- |
| 1（最先松弛） | COLREG Rule 14/15 交叉/对遇让路方向 | 对方为非合规目标或多目标矛盾 |
| 2 | CPA ≥ 安全距离（按目标类型） | 无法同时满足所有目标时 |
| 3 | COLREG Rule 8 "显著动作" 幅度要求 | 空间不足时允许小幅调整 |
| 4（最后松弛） | 航道边界 / UKC 安全水深约束 | 仅在碰撞不可避免时允许越界 |

**求解算法伪代码**：

```python
def emergency_deadlock_handler(threat_list, own_state, mpc_solver):
    """
    约束死锁应急处理——最小碰撞动能模型
    在 COLAV MPC 确认 DEADLOCK 后调用
    """
    # Step 1: 立即触发 AMS Alarm
    publish_ams_event(
        code="COLAV_CONSTRAINT_DEADLOCK",
        severity=ALARM,
        text=f"多目标约束冲突死锁（N={len(threat_list)}），已转入最小动能应急降级"
    )
    
    # Step 2: 按 TCPA 排序 + 分配权重
    threats_sorted = sorted(threat_list, key=lambda t: t.tcpa)
    for t in threats_sorted:
        t.weight = 1.0 / (t.tcpa + 0.1)  # ε=0.1s 防除零
    
    # Step 3: 构建应急 MPC（松弛约束集）
    emergency_mpc = mpc_solver.clone()
    emergency_mpc.set_objective(J_emergency)       # 切换目标函数
    emergency_mpc.set_hard_constraint(V_min=3.0)   # 3kn 舵效下限
    
    relaxation_level = 0
    while not emergency_mpc.is_feasible():
        relaxation_level += 1
        if relaxation_level > 4:
            break  # 全部松弛仍不可行 → MRC
        emergency_mpc.relax_constraint(priority=relaxation_level)
    
    # Step 4: 求解或 MRC fallback
    if emergency_mpc.is_feasible():
        solution = emergency_mpc.solve(timeout_ms=50)
        # 记录 VDR
        log_vdr(event="DEADLOCK_RESOLVED",
                relaxation_level=relaxation_level,
                predicted_min_cpa=solution.min_cpa,
                predicted_impact_angle=solution.impact_angles)
        return solution.control_command
    else:
        # Step 5: MPC 全松弛仍不可行 → 升级至 MRC
        trigger_mrc(reason="COLAV_DEADLOCK_UNRESOLVABLE")
        log_vdr(event="DEADLOCK_MRC_ESCALATION",
                n_threats=len(threat_list))
        return CMD_FREEZE  # 冻结最后安全指令，MRC 接管
```

**MRC 升级时限**：从 DEADLOCK 检测到 MRC 触发的最大允许延迟为 **5s**——在此期间应急 MPC 尝试松弛求解。若 5s 内未找到可行解，无条件升级 MRC。

**计算资源分配**：多目标场景 DT 引擎推演时间线可能超 WCET，采用分级降级：
- ≤5 个目标：标准 100 候选 × 180s 推演
- 6~10 个目标：50 候选 × 120s 推演
- \>10 个目标：20 候选 × 60s 推演 + AMS Warning

## 7.2　雷达/AIS 冲突目标的 COLAV 处理

在 PCF 裁决矩阵（SINAN-SSL-SDD-001 §8.3）的基础上，COLAV 特有处理规则：

| 冲突类型 | PCF 裁决 | COLAV 额外处理 |
|---------|---------|--------------|
| 仅 Radar（暗目标） | σ=0.6，保守假设机动船 | QSD 正前方扩大 10%（不确定性裕量）|
| 仅 AIS 60s 无雷达确认 | σ=0.3，幽灵船降级 | 降为 Caution 级，继续弱监控 |
| Radar+AIS 位置偏差>500m | 分两个目标跟踪 | 两个目标分别计算 CRI，取较大值决策 |
| AIS 报告 NUC/RAM | PCF 接受 | **激活 vessel_type_flag 豁免判定**（见第 4 章 §4.5）|

---

# 第 8 章　横切关注点

## 8.1　与 ALM 的完整交互协议

| ALM 全局等级 | COLAV 行为 | 允许的最高输出 |
|------------|----------|--------------|
| **H1 手动** | 仅运行模块 1（QSD 监控），CRI 结果显示在 HMI | 告警显示，不生成轨迹 |
| **H2 监督** | 全流水线运行，避碰轨迹在 HMI 显示为建议 | 等待船员 ACK 后 VO 执行 |
| **H3 自主** | 全流水线运行，ENGAGED 后直接通过 VO 下发 | 自主执行 |

**行为 B 协议**（H3 降级为 H1 时，COLAV 处于 ENGAGED 状态）：
- ALM 收到 H1 降级请求
- COLAV 收到 `FREEZE_AT_NEXT_SAFE_POINT` 信号
- 最多完成当前避碰轨迹段 30 秒
- 30 秒内如完成 → 发布 EVENT_COLAV_FROZEN
- 30 秒后仍在高风险区 → 强制冻结 + MRC

## 8.2　与 AMS 的告警编码

| 告警编码 | 优先级 | 触发条件 | HMI 显示文本 |
|---------|--------|---------|------------|
| `COLAV_THREAT_DETECTED` | Caution | CRI > 0.5 | "目标 [ID] 碰撞风险升高" |
| `COLAV_ENGAGED` | Warning | CRI > 1.0，系统开始避碰 | "碰撞规避已激活" |
| `COLAV_RULE17_STAGE2` | Warning | Rule 17 Ⅱ 触发 | "直航警告：对方未行动，微调引起注意" |
| `COLAV_RULE17_STAGE3` | Alarm | Rule 17 Ⅲ 触发 | "Rule 17b：独自避碰启动" |
| `COLAV_RULE17_STAGE4` | Emergency | Rule 17 Ⅳ 触发 | "极度危险！紧急脱离！" |
| `COLAV_WCET_TIMEOUT` | Alarm | MPC 超时 | "COLAV 计算超时，使用缓存轨迹" |
| `COLAV_CONSTRAINT_CONFLICT` | Alarm | 多目标约束无交集 | "多目标约束冲突，自动减速" |
| `COLAV_TARGET_COUNT_EXCEEDED` | Warning | 目标数 > 50 | "目标数超限，COLAV 降级简化模式" |

## 8.3　GPU 资源管理开发规范

**开发红线（禁止违反）**：

| 编号 | 禁止项 |
|------|--------|
| COLAV-RL-01 | 禁止 VRO 或任何低优先级任务抢占 COLAV CUDA Stream |
| COLAV-RL-02 | COLAV 主循环中禁止使用 cudaDeviceSynchronize()（会阻塞所有 CUDA 操作） |
| COLAV-RL-03 | 禁止在 COLAV MPC Kernel 中使用浮点原子操作（破坏确定性，CCS 审查拒绝） |
| COLAV-RL-04 | GPU OOM 时禁止直接崩溃，必须切换至 CPU 降级模式后触发 ALM H1 |
| COLAV-RL-05 | 仲裁引擎禁止使用任何机器学习或随机算法（CCS 可解释性要求） |

**CPU 降级模式**（GPU 故障时）：
- 切换至 Eigen3 单线程 CPU 积分（Nomoto 简化模型）
- 最大支持目标数量：5 个（算力限制）
- WCET：< 500ms（牺牲实时性换取可用性）
- 同时：ALM→H2，AMS Alarm "GPU 故障，COLAV 降级模式"

## 8.4　VDR 完整记录要求

每次 COLAV ENGAGED 事件生成独立 VDR 会话，以下数据全程记录（1Hz，ENGAGED 期间 10Hz）：

| 记录项 | 来源 | 保留时间 |
|--------|------|---------|
| Threat_Event 序列（含 CRI/DCPA/TCPA） | 模块 1 | 事件起止 + 前后 5min |
| Action_Constraints 序列（含 COLREGs 条款引用） | 模块 2 | 事件起止 |
| Rule 17 阶段切换时间点（精确时间戳） | 模块 2 | 事件起止 |
| MPC 最优轨迹 + 次优轨迹 + 代价值 | 模块 3 | 事件起止 |
| 候选轨迹 UKC 成本（UKC 否决记录） | 模块 3 | 事件起止 |
| 最终执行轨迹 + 决策置信度 | 模块 3 | 事件起止 |
| 复航切入点坐标 + XTE 收敛曲线 | 模块 4 | 事件起止 |
| vessel_type_flag 状态 | 模块 2 | 全程 |
| ALM 等级（H1/H2/H3） | ALM 广播 | 全程 |

**法律意义**：VDR 中的 Rule 17 阶段切换记录和 COLREGs 条款引用，是事故调查中判断系统决策合规性的关键证据。

## 8.5　ROS 2 接口总表

| 话题/服务 | 类型 | QoS | 发布/订阅 | 说明 |
|----------|------|-----|----------|------|
| `/sinan/{id}/telemetry/own_state` | OwnState.msg | BEST_EFFORT, 10Hz | 订阅 | 本船状态输入 |
| `/sinan/{id}/telemetry/target_list` | TargetList.msg | BEST_EFFORT, 2Hz | 订阅 | 目标列表输入 |
| `/sinan/{id}/colav/threat_events` | ThreatEvent.msg | RELIABLE, 10Hz | 发布 | 威胁评估输出 |
| `/sinan/{id}/colav/action_constraints` | ActionConstraints.msg | RELIABLE, 5Hz | 发布 | 规则约束输出 |
| `/sinan/{id}/colav/evasion_trajectory` | Trajectory.msg | RELIABLE, 5Hz | 发布 | 避碰轨迹（供 VO 路由） |
| `/sinan/{id}/events/colav/status` | EventStatus.msg | RELIABLE, 事件 | 发布 | ENGAGED/DISENGAGED |
| `/sinan/{id}/alm/global_state` | ALMState.msg | TRANSIENT_LOCAL | 订阅 | ALM 等级监听 |
| `/sinan/{vessel_id}/vo/override_requests` | OverrideRequest.msg | RELIABLE | 发布 | VO 抢占请求 |
| `/sinan/{id}/ams/events` | AMSEvent.msg | RELIABLE | 发布 | 告警事件 |

## 8.6　FMEA 失效影响完整摘要

> 严重度（S）：1=性能退化；2=功能降级；3=安全临界；4=灾难性（可能碰撞）
> 发生概率（F）：1=低（<1/10000h）；2=中低；3=中高；4=高（>1/1000h）
> 可接受性（A）：A=可接受；M=需缓解；U=不可接受须设计消除

| 失效模式 | 触发条件 | S | F | S×F | A | 缓解措施 | 残余风险 |
|---------|---------|---|---|-----|---|---------|---------|
| QSD 计算超时（目标>50） | 港口高密度场景 | 4 | 2 | 8 | **U** | **GPU 张量化+目标上限裁剪+ALM→H2**（须设计消除） | 降级后 H2 人工监督 |
| Rule 17 阶段误判（对方突然转向） | 目标蛇行/大幅变速 | 3 | 2 | 6 | M | Rule 17 有 60s 稳定观察窗 + 四级兜底 | 极端机动目标 |
| MPC 求解发散/NaN | 约束空间无可行解 | 4 | 1 | 4 | M | F4 合理性检查 + 上一帧缓存 + 3 帧后 MRC | 600ms 缓存期 |
| 复航过早（让清误判） | QSD_Aft 计算偏差 | 4 | 1 | 4 | M | 30s 冷却计数器 + 四条让清条件全满足 | 极端追越场景 |
| GPU 故障（过热/OOM） | 机舱高温/显存泄漏 | 4 | 1 | 4 | M | CPU 降级模式（5目标上限）+ ALM→H1 | H1 期间人工避碰 |
| 仲裁引擎误分类（边界条件） | RB/CA 恰好在分类边界 | 3 | 1 | 3 | M | 边界扩大 2° 迟滞 + 持续重分类（1Hz） | 边界漂移 |
| 全部感知输入丧失 | Radar+AIS 同时故障 | 4 | 1 | 4 | M | PCF D12 触发 ALM→H1→MRC | 降级后人工接管 |
| vessel_type_flag 注入错误 | 拖带时未切换 RAM | 3 | 2 | 6 | M | ALM→H1 时自动检查（船速<0.5kn+绞缆负载传感器） | H1 监控兜底 |
| COLAV 与 VRO 指令冲突 | 抢占协议握手失败 | 3 | 1 | 3 | M | VO Cmd MUX 严格串行仲裁，COLAV 最高优先级 | VO 作为唯一仲裁者 |

## 8.7　开发完成标准（DoD）

| 编号 | 验收项 | 验证方法 |
|------|--------|---------| 
| COLAV-D1 | 5 种标准会遇态势角色判定 100% 正确（含 3 类追越亚型） | 参数化单元测试（30+ 用例） |
| COLAV-D2 | Rule 17 四阶段切换时序精确（TCPA 阈值 ±1s 误差内） | 时序仿真测试 |
| COLAV-D3 | 让路时避碰转向量 ≥ 15°（Rule 8 良好船艺） | 边界值测试 |
| COLAV-D4 | 直航船禁止向左转向（Rule 17c 硬约束有效） | 约束验证测试 |
| COLAV-D5 | 全流水线 WCET < 100ms（包含 DT P0 验证） | AGX Orin 实测，99th percentile |
| COLAV-D6 | UKC 否决扫描闭环工作（触底轨迹不被选中） | UKC 集成测试 |
| COLAV-D7 | vessel_type_flag = RAM 时禁止转向动作生效 | 参数注入测试 |
| COLAV-D8 | vessel_type_flag = NUC 时不主动发出避碰轨迹 | 场景测试 |
| COLAV-D9 | H2 模式下避碰轨迹仅作建议，未经确认不执行 | ALM 集成测试 |
| COLAV-D10 | 复航切入角 ≤ 30°（良好船艺约束） | 几何学验证 |
| COLAV-D11 | GPU 故障后 CPU 降级模式正常工作（5 目标上限） | 故障注入测试 |
| COLAV-D12 | CUDA Stream 优先级保证 COLAV 不被 VRO 抢占 | CUDA Profiler 验证 |
| COLAV-D13 | VDR 完整记录每次 ENGAGED 事件（含 COLREGs 引用） | 日志完整性审查 |
| COLAV-D14 | 20 船高密度场景 WCET < 100ms（降级候选数） | 压力测试 |
| COLAV-D15 | 仲裁引擎 100% 确定性（相同输入 → 相同输出） | 确定性回归测试（100 次运行哈希一致）|
| COLAV-D16 | 约束死锁检测在连续 3 个不可行周期后 ≤ 300ms 内触发（§7.1.1） | MPC 故障注入测试 |
| COLAV-D17 | 最小碰撞动能模型松弛求解 + MRC 升级总时限 ≤ 5s（§7.1.2） | 多目标死锁场景 HIL |
| COLAV-D18 | 死锁模型保持 $V_{own} \geq 3$ kn 舵效下限硬约束 | 边界值验证 |
| COLAV-D19 | 所有 COLAV VDR 记录符合卷1 §7.3 统一信封格式 | 日志格式合规审查 |

---

# 附录 A　COLREGs 规则覆盖完整性矩阵

| COLREGs 规则 | 适用内容 | COLAV 模块 | 覆盖状态 |
|-------------|---------|-----------|---------|
| Rule 5 | 瞭望义务 | 模块 1 QSD 持续监测 | ✅ 已覆盖 |
| Rule 6 | 安全航速 | UKC + VRO（不在 COLAV 内） | ✅ 跨模块覆盖 |
| Rule 7 | 碰撞风险判断 | 模块 1 CRI 计算 | ✅ 已覆盖 |
| Rule 8 | 避碰行动（及早、大幅、显著） | 模块 3 转向量 ≥ 15° 约束 | ✅ 已覆盖 |
| Rule 13 | 追越 | 模块 2 追越分类 | ✅ 已覆盖 |
| Rule 14 | 对遇 | 模块 2 对遇分类 | ✅ 已覆盖 |
| Rule 15 | 交叉相遇 | 模块 2 交叉分类 | ✅ 已覆盖 |
| Rule 16 | 让路船义务 | 模块 3 约束集合 | ✅ 已覆盖 |
| Rule 17 | 直航船义务 | 模块 2 四阶段状态机 | ✅ 已覆盖（本版完整定义）|
| Rule 18 | 特殊船舶让路优先级 | 模块 2 vessel_type_flag | ✅ 已覆盖（6 种船型）|
| Rule 19 | 能见度不良 | PCF σ_system 降级时 H2 限制 | ✅ 间接覆盖 |

---

*文档编号：SINAN-COLAV-SDD-001 | 版本：v2.1 | 发布日期：2026-04-16*  
*本文档受控，未经授权不得复制或传播。*
*变更说明：v2.0 在 v1.0 基础上补充：Rule 17 四阶段完整定义（含 TCPA 精确阈值）/ MPC 代价函数完整权重矩阵 / GPU 资源管理与 CUDA Stream 优先级 / vessel_type_flag 六种船型完整参数表 / 紧急反应 COLAV / FMEA 完整 S×F×A 矩阵 / ROS 2 接口总表 / COLREGs 规则覆盖完整性矩阵*
*v2.1 Nn认证关闭项：§7.1 新增约束死锁检测判定条件(§7.1.1)、最小碰撞动能应急降级模型目标函数与伪代码(§7.1.2)、约束松弛优先级层次、MRC升级时限；DoD新增COLAV-D16~D19*
