# v1.1 Patch List

> v1.0 → v1.1 字段级 diff，可直接合并执行。每条引用 finding ID 追溯。
>
> **完成时间**：2026-05-05
> **决策树落点**：B 档（结构性修订 + ADR）— 5 条 P0 通过 ADR-001/002/003 解决（详见 `08c-adr-deltas.md`）；本文件含 35 条 P1/P2/P3 字段级 patch
>
> **patch 计数**：P1=18 + P2=15 + P3=2 = 35 项
>
> **执行约束（Karpathy 第 3 条本地化）**：每条 patch 只改其涉及的精确字段；不顺手统一格式 / 改其他章节措辞。如发现新问题，开新 finding 不混入本 patch。

---

## §1 背景与设计约束

### Patch §1.2-01 [F-P2-D1-030] — 设计边界表旧术语

- **行 54**：
  - **旧**：`传感器融合内部算法（L1 感知层）`
  - **新**：`传感器融合内部算法（Multimodal Fusion 多模态感知融合子系统）`
- **行 55**：
  - **旧**：`全局航次规划（L2 战略层）`
  - **新**：`全局航次规划（L1 任务层 Mission Layer + L2 航路规划层 Voyage Planner）`
- 影响：仅表格"在范围外"列两行；不动其他内容

---

## §2 核心架构决策（决策一/二/三/四相关 — 顶层措辞修订；ADR 级别修订见 08c-adr-deltas.md）

### Patch §2.2-01 [F-P0-D1-006] — H 轴来源修订（与 ADR-002 配套）

- **行 95** 段落"Rødseth 等（2022）明确指出..."：
  - **旧**：`Rødseth 等（2022）明确指出，海事 ODD 必须包含人机责任分配维度（H 轴）...本设计采纳三轴（E×T×H）框架 [R8]。`
  - **新**：`Rødseth 等（2022）定义二轴 OE：O = S_ext × S_int + FM（Function Mapping，状态→人机控制权分配）[R8]。本设计在 Rødseth 框架上扩展，将 FM 具体化为 H 轴（Human Availability，含 D1–D4 四级自主度），参考 IMO MASS Code D1–D4 定义 [R2]。三层 ODD 概念（Rødseth 二轴 + DNV-CG-0264 AROS 三维认证空间 + SAE J3259 属性树）的具体框架见 §3.2。`

### Patch §2.2-02 [F-P1-D1-007] — IMO MASS Code 章节错引

- **行 95** 段落"IMO MASS Code 草案..."：
  - **旧**：`IMO MASS Code 草案（MSC 110，2025）在第 15 章明确要求...`
  - **新**：`IMO MASS Code 草案（MSC 110，2025）在 Part 2 Chapter 1（运行环境/运行包络 OE）明确要求...`

### Patch §2.2-03 [F-P2-D1-011] — TMR 60s 引用补充

- **行 95** TMR/TDL 段落：
  - **旧**：`Rødseth 等[R8]给出 TMR 和 TDL 两个量化参数...`
  - **新**：`Rødseth 等（2022）提出 TMR/TDL 概念框架 [R8]；Veitch 等（2024）实证确定 TMR ≥ 60s 设计基线 [R4]。`

### Patch §2.3-01 [F-P1-D1-009] — DNV 版本错 + Kongsberg 候选 C 补充

- **行 116**：
  - **旧**：`DMV-CG-0264（2018）`
  - **新**：`DNV-CG-0264（2025.01）`
- **§2.3 弃用方案对比表**新增第三行：
  ```
  | 候选 C: Kongsberg K-MATE / v3.0 基线 5 模块 | Target Tracker / COLREGs Engine / Avoidance Planner / Risk Monitor / Capability Assessment | 弃用理由：5 组件模型将 ODD 隐式分布在各模块，不满足决策一（ODD 唯一权威）。本设计借鉴其 5 模块对应 v1.0 M2/M6/M5/M7/M1（部分），但 v1.0 通过引入 M1 + M3 + M8 提升认证可审计性。 |
  ```

### Patch §2.4-01 [F-P1-D1-010] — SAT 全展示工作负荷断言修订

- **行 122**：
  - **旧**：`SAT-1+2+3 全部可见时，操作员的态势感知提升且工作负荷不增加 [R11]。`
  - **新**：`SAT-3（预测/投影）是操作员最高优先级；SAT-2（推理过程）按需触发（COLREGs 冲突检测时 / M7 警告激活时 / 操作员显式请求时），不默认全时展示 [R5: USAARL 2026-02 透明度悖论 + NTNU 2024 Layer 偏好实证]。M8 采用自适应分级展示策略，详见 §12.2。`

### Patch §2.5-01 [F-P0-D1-008] — "100×简单"措辞修订（与 ADR-001 配套）

- **行 126–128**：
  - **旧**：`Checker 的验证逻辑须比 Doer 简单 100 倍以上 [R12]，以满足 IEC 61508 SIL 2 形式化验证要求。`
  - **新**：`Checker 的代码规模须显著小于 Doer——工业实践目标 SLOC 比例 50:1 ~ 100:1（Boeing 777 Monitor ~50:1；Airbus FBW COM/MON >100:1）[R4]，以支持 DO-178C Level A / IEC 61508 SIL 2 形式化验证。**此为设计目标，非规范强制值**——IEC 61508/DO-178C 均无量化倍数要求 [R12]。`

---

## §3 ODD / Operational Envelope 框架（与 ADR-003 配套）

### Patch §3.2-01 [F-P0-D6-015 + F-P0-D1-006] — 三轴公式框架重写（核心条目，详见 ADR-003）

- **行 140–149** 三轴公式段落：
  - **旧**：`本设计采纳 Rødseth（2022）的 Operational Envelope 扩展定义...三轴：O = E × T × H`
  - **新**：
    ```
    本设计基于三层概念框架：
    
    1. 数学定义层（Rødseth 二轴）[R8]：
       O = S_ext × S_int + FM
       其中 S_ext = 外部环境状态；S_int = 内部技术状态；FM = Function Mapping（人机控制权分配）
    
    2. 工程实现层（v1.1 三轴扩展，本设计选择）：
       O = E × T × H
       - E（Environmental）= S_ext 的工程实现（气象 / 交通密度 / 水域类型）
       - T（Technical）= S_int 的工程实现（传感器健康 / 通信 / 系统健康）
       - H（Human）= FM 的具体化（D1–D4 自主等级 + ROC 接管时窗），参考 IMO MASS Code D1–D4 [R2]
       注：H 轴是本设计在 Rødseth 二轴框架上的具体化扩展，非 Rødseth 2022 原文定义。
    
    3. 认证审查层（DNV-CG-0264 AROS 三维认证空间）[R7]：
       (Mission Complexity, Environmental Difficulty, Human Independence)
       用于风险分级和功能级认证，**非 ODD 定义**。本设计的 ODD 框架（E×T×H）映射到 AROS 三维认证空间作为合规证据。
    
    4. 属性表达层（SAE J3259 属性树）[新增]：
       Scenery / Environmental / Dynamic / Connectivity / Temporal
       用于 ODD 子域属性的离散化表达。
    ```

### Patch §3.4-01 [F-P1-D6-016] — TDL 公式 0.6 系数注释

- **行 173**：
  - **旧**：`TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)`
  - **新**：`TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)  [设计值：0.6 = TCPA 60% 留 40% 余量供操作员接管，基于 Veitch 2024 TMR ≥ 60s 实证基线估算 [R4]；HAZID 中以实际船型校准]`

### Patch §3.3-01 [F-P1-D6-017] — ODD 子域阈值标注

- **行 155–160 子域表格**末尾加注：
  - **新增脚注**：`* 上述 CPA / TCPA 阈值为 FCB 船型初始设计值（参考 Wang et al. 2021 [R17] + Frontiers 2021 综述 TCPA 5–20 min 范围 [R2 文献]），须在项目级 HAZID 中以实际场景验证。`

### Patch §3.5-01 [F-P2-D6-018] — EDGE/Conformance 阈值标注

- **行 196 EDGE 状态描述**：
  - **旧**：`margin < 20% 时进入 EDGE 状态`
  - **新**：`margin < 20% 时进入 EDGE 状态 [初始值，待 HAZID 校正]`
- **§3.6 Conformance_Score 阈值表达式**末尾加注：
  - **新增脚注**：`* 阈值（>0.8 IN, 0.5–0.8 EDGE, <0.5 OUT）为初始建议值，待 FCB 场景 HAZID 校正。`

### Patch §3.6-01 [F-P0-D6-015 配套] — Conformance_Score 公式补注

- **§3.6 公式段落**末尾加注：
  - **新增**：`* 三轴权重 (w_E, w_T, w_H) 默认值 (0.4, 0.3, 0.3) 为 FCB 特定设计值；H 轴权重 w_H = 0.3 须在 HAZID 中以 Veitch 2024 TMR 实证 [R4] 校准。`

---

## §4 系统架构总览

### Patch §4-01 [F-P1-D5-012] — 图 4-1 M5 输出层级修正

- **行 281 mermaid 图节点**：
  - **旧**：`M5 --> L2_EXEC["L2 执行层 / 航向/速度控制器"]`
  - **新**：`M5 --> L4_GUIDE["L4 引导层（Guidance Layer）"]`
- 备注：详细 schema 重定义见 F-P1-D4-032 跨团队对齐（方案 A 或 B），本 patch 仅修正层级标签

### Patch §4-02 [F-P2-D5-013, F-P1-D4-034] — Reflex Arc 接口说明（新增 §4.5）

- **§4 末尾新增小节**：
  ```
  ### §4.5 与 Y-axis Emergency Reflex Arc 的接口
  
  v3.0 架构含 Y-axis Emergency Reflex Arc（Perception 极近距离 → bypass L3/L4 → 直达 L5 < 500 ms）。L3 各模块通过以下接口感知 Reflex Arc 激活：
  - **M1** 订阅 Reflex Arc 激活信号（ReflexActivationNotification 消息），激活时切换到"Reflex Bypass"模式
  - **M5** 在 Reflex 激活期间冻结 MPC 求解（保持状态供回切），不向 L4/L5 输出新指令
  - **M7** 暂停 SOTIF 监控（避免触发虚假告警）
  - **M8** 向 ROC 推送"Reflex Active"高优先级通知 + 紧急 SAT-3
  
  Reflex Arc 触发阈值、感知输入源、时序由跨团队 Reflex Arc spec 定义（待补）。详细消息 schema 见 §15.1。
  ```

---

## §6 M2 World Model

### Patch §6-01 [F-P1-D4-019] — 图 6-1 L1 感知层旧术语

- **行 447–448** mermaid 图节点：
  - **旧**：
    ```
    RADAR["雷达\n(L1 感知层)"]
    CAMERA["摄像头\n(L1 感知层)"]
    ```
  - **新**：
    ```
    FUSION["多模态融合输出\n(Multimodal Fusion)\nTrack 级目标 + Nav Filter EKF 自船状态"]
    ```
- **行 460 注释**：
  - **旧**：`L1 感知层的融合输出（Track 级别，非原始传感器数据）输入 M2`
  - **新**：`Multimodal Fusion 子系统输出（TrackedTargetArray + FilteredOwnShipState + EnvironmentState 三个独立话题，详见 §6.4 接口契约）输入 M2，由 M2 内部聚合为统一 World_StateMsg`

### Patch §6.3.1-01 [F-P2-D4-020] — 几何预分类主语注释

- **§6.3.1 COLREG 几何预分类代码**前加注释：
  - **新增（在角度判断代码块之前）**：
    ```
    # 以下角度判断从 own ship 视角，bearing_i = 目标相对本船的方位（度，0° = 正北 / 360°）。
    # OVERTAKING 判定：bearing_i 在本船艉扇区（艉端 ± 67.5°）= [112.5°, 247.5°]
    #   → 本船是被追越方（stand-on vessel）
    # CROSSING 判定：bearing_i 在本船舷扇区（[5°, 112.5°] 或 [247.5°, 355°]）
    #   且 |target_heading - own_heading| > 6° → 交叉相遇
    # HEAD-ON 判定：bearing_i 在本船首扇区（[355°, 360°] ∪ [0°, 5°]）
    #   且 |target_heading - own_heading + 180°| < 6° → 对遇
    ```

### Patch §6.4-01 [F-P1-D4-031] — 接口契约 Multimodal Fusion 输入聚合说明

- **§6.4 接口契约**末尾新增小节：
  ```
  #### M2 内部数据聚合（与 Multimodal Fusion 三话题对接）
  
  M2 订阅 Multimodal Fusion 三个独立话题：
  | 话题 | 频率 | 内容 |
  |---|---|---|
  | /perception/targets | 2 Hz | TrackedTargetArray |
  | /nav/filtered_state | 50 Hz | FilteredOwnShipState |
  | /perception/environment | 0.2 Hz | EnvironmentState |
  
  M2 内部以 2 Hz 输入聚合 + 1 次插值/外推 → 4 Hz 输出 World_StateMsg。
  - **CPA / TCPA 计算**：M2 自行计算（Multimodal Fusion 不提供），加入 World_StateMsg.targets[i].cpa_m / tcpa_s 字段
  - **自身状态时间对齐**：M2 取最近的 FilteredOwnShipState 快照（50 Hz vs 4 Hz 差异通过取最近时间戳处理）
  - **ENC 约束**：M2 从 EnvironmentState.zone_type / in_tss / in_narrow_channel 提取，加入 World_StateMsg.zone_constraint
  - **坐标系约定**：目标速度 = 对地（sog/cog，from TrackedTarget）；自身速度 = 对水（u/v，from FilteredOwnShipState）+ 海流估计（current_speed/direction）
  ```

---

## §7 M3 Mission Manager

### Patch §7-01 [F-P1-D1-022 + F-P2-D4-038] — 图 7-1 上游 + L1 VoyageTask 入口

- **行 521 mermaid 图节点**：
  - **旧**：`L2_STRAT["战略层L2\n航次计划"]`
  - **新**：
    ```
    L1_MISSION["L1 任务层 (Mission Layer)\nVoyageTask（事件触发）"]
    L2_VOYAGE["L2 航路规划层 (Voyage Planner)\nPlannedRoute + SpeedProfile（规划周期）"]
    ```
- **§7.1 模块描述**末尾补：
  ```
  M3 上游消费两类消息：
  - L1 VoyageTask（事件触发）：含 departure / destination / eta_window / optimization_priority / mandatory_waypoints / exclusion_zones
  - L2 PlannedRoute + SpeedProfile（规划周期 1 Hz 或事件）：含航点序列 + 速度曲线
  M3 不直接做航次规划（属 L1 职责）；其职责为：(1) 接收 L1 任务令并校验有效性；(2) 跟踪 ETA 投影；(3) 检测 ODD 越界 → 触发重规划请求（向 L2 发 RouteReplanRequest）。
  ```

---

## §10 M5 Tactical Planner

### Patch §10-01 [F-P1-D5-012] — 图 10-1 下游层级

- **§10.2 mermaid 图节点**：
  - **旧**：`M5 --> L2_EXEC["L2 执行层"]`
  - **新**：`M5 --> L4_GUIDE["L4 引导层（Guidance Layer）"]`

### Patch §10.5-01 [F-P1-D9-024] — [R21] 替换或标注

- **§10.5 FCB 高速修正段落**：
  - **旧**：`Hs > 1.5m 时须引入波浪扰动项（参照 Yasukawa & Sano 2024 近岸修正）[R21]`
  - **新**：`Hs > 1.5m 时须引入波浪扰动项，参照 Yasukawa & Yoshimura（2015）4-DOF MMG 标准方法的波浪修正章节 [R7]。[注：早期稿曾引用 Yasukawa & Sano 2024 [R21]，但来源未在 JMSE/JMSTech 数据库确证，建议从参考文献剔除，详见 99-followups.md。]`
- **§10.6 引用列表**：
  - **删除**：`[R21] Yasukawa & Sano 2024（待 JMSE 数据库确证）`

### Patch §10-02 [F-P2-D9-041] — TSS Rule 10 多边形约束

- **§10 MPC 约束节**末尾新增段落：
  ```
  #### COLREGs Rule 10（TSS）多边形约束
  
  当 EnvironmentState.in_tss = true 时，Mid-MPC 状态约束加入 TSS lane polygon：
  - 从 ENC 获取当前 TSS lane 的多边形顶点序列（typically 4–6 顶点）
  - 状态约束：position(t) ∈ TSS_lane_polygon ∀ t ∈ [0, MPC_horizon]
  - 形式化保证：MPC 求解的轨迹完全位于指定 lane 内，不偏离至对向 lane 或分隔带
  ```

### Patch §10-03 [F-P2-D6-037] — Override 时 M5 冻结说明

- **§10 末尾新增小节 §10.7**：
  ```
  ### §10.7 人工接管时 M5 行为
  
  Hardware Override Arbiter 激活（接收 override_active 信号）时：
  - M5 冻结 MPC 求解（保持当前状态供回切，不再消费新输入）
  - M5 输出 status = OVERRIDDEN（不再输出新轨迹指令）
  - ASDR 在接管期间标记所有 M5 输出为 "overridden"
  - 回切（Arbiter 解除接管）时 M5 重新读取当前状态 + 重启 MPC（含积分项重置）
  ```

---

## §11 M7 Safety Supervisor（与 ADR-001 配套；§11.6 / §11.7 / §11.8 是 ADR 衍生新增章节，本文件给字段级 patch）

### Patch §11.2-01 [F-P0-D3-001] — ARBITRATOR 行为约束（详见 ADR-001）

- **§11.2 图 11-1 ARBITRATOR 注释**（行 778）：
  - **旧**：`ARBITRATOR 直接向 M5 注入安全轨迹`
  - **新**：`ARBITRATOR 触发预定义 MRM 命令集（M7 仅持 VETO 权 + 命令集索引；不动态生成轨迹）`
- 配套：新增 §11.6 见下

### Patch §11.3-01 [F-P1-D3-003] — CPA 计算独立性

- **§11.3 PERF 行**：
  - **旧**：`CPA 趋势恶化 → 触发 PERF 告警`
  - **新**：`CPA 趋势恶化 → 触发 PERF 告警。**CPA 计算独立性约束**：M7 PERF 监控读 M2 输出的 CPA 但不依赖 M2 推理；当 M2 状态 = DEGRADED 或 FAILED 时，M7 PERF 自动降级到保守阈值（CPA_safe = 0.5 nm 强制告警），不依赖 M2 数值。`

### Patch §11.3-02 [F-P2-D3-036] — Checker 否决率作为 SOTIF 输入

- **§11.3 SOTIF 假设违反清单**新增一行：
  ```
  | L3 Checker 否决率 | Checker VETO 次数 / 100 周期 | > 20（即 20% 否决率） | M7 升级 SOTIF 告警："COLREGs 推理可信度下降"，触发降级到 D2 评估 |
  ```

### Patch §11.4-01 [F-P1-D7-005] — SIL 2 SFF 补充

- **§11.4 SIL 2 架构要求**：
  - **旧**：`单通道 + 诊断覆盖 ≥ 90%`
  - **新**：`单通道（HFT=0）+ 诊断覆盖 DC ≥ 90% + Safe Failure Fraction SFF ≥ 60%（Type A 元件）/ ≥ 70%（Type B 元件）。**推荐配置**：关键安全路径采用 HFT=1 双通道冗余，可降低 SFF 要求至 ≥ 90%（IEC 61508-2 Table 3）。`

### Patch §11-01 [F-P1-D6-004] — 新增 §11.6 M7 自身降级行为

- **§11 末尾新增小节**：
  ```
  ### §11.6 M7 自身降级行为
  
  - **心跳监控**：M7 心跳由 X-axis Deterministic Checker（外部，跨层）监控，频率 1 Hz；同时 M1 订阅 M7 心跳作内部参考
  - **失效模式**：Fail-Safe（必须）—— M7 失效时强制触发保守 MRM 命令（减速至 4 kn 维持航向 + 通知 M8）；不允许 Fail-Silent
  - **失效后的系统降级**：M7 失效后系统自动降级到 D2（船上有海员备援）；禁止 D3/D4 运行
  - **MRM 命令集（预定义）**：
    - MRM-01: 减速至 4 kn 维持航向（30 秒内）
    - MRM-02: 停车（速度 → 0），通知 ROC 介入
    - MRM-03: 紧急转向（基于当前威胁方向，预定义反向避让）
    - MRM-04: 抛锚序列（仅适用于近岸场景，水深允许）
  ```

### Patch §11-02 [F-P0-D3-002] — 新增 §11.7 与 X-axis Checker 协作（详见 ADR-001）

- **§11 新增小节 §11.7**：
  ```
  ### §11.7 与 X-axis Deterministic Checker 的协作
  
  本设计 M7 与 v3.0 X-axis Deterministic Checker **不在同一层级，是分层互补关系**：
  
  | 维度 | M7 Safety Supervisor | X-axis Deterministic Checker |
  |---|---|---|
  | 层级 | L3 内部 Checker | 系统级 Checker（跨 L2/L3/L4/L5）|
  | 类型 | IEC 61508 + SOTIF 双轨监控 | 确定性规则 VETO |
  | 输出 | 假设违反告警 + MRC 请求 | VETO + nearest_compliant 替代 |
  | 频率 | 周期 + 事件 | 与 Doer 同频 |
  | 优先级 | 中等（向 M1 报告，M1 仲裁） | 最高（直接 VETO Doer 输出） |
  
  **协调协议**：
  - X-axis 否决 M5 输出 → 向 M7 发 CheckerVetoNotification（事件）；M7 将否决率纳入 SOTIF 假设违反检测（详见 §11.3）
  - M7 触发 MRC 请求 → 经 M1 仲裁后下发 MRM 命令；X-axis 不干预 MRC 内部流程
  - X-axis 与 M7 通过独立总线通信，**不共享代码 / 库 / 数据结构**（决策四独立性约束）
  
  详细协调协议见 ADR-001。
  ```

### Patch §11-03 [F-P2-D6-037] — 新增 §11.8 人工接管时 L3 行为

- **§11 新增小节 §11.8**：
  ```
  ### §11.8 人工接管时 L3 内部行为
  
  Hardware Override Arbiter 激活（接收 override_active 信号）时：
  - **M1**：模式状态机切换到 OVERRIDDEN，停止 ODD 周期更新；保持 ODD 状态快照供 ASDR 记录
  - **M5**：冻结 MPC 求解（保持状态供回切，不再输出新轨迹），详见 §10.7
  - **M7**：暂停 SOTIF 监控（避免触发虚假告警），保留心跳输出
  - **M8**：切换到"接管模式"UI，显示 ROC 操作员状态 + 接管时段 ASDR 摘要
  - **ASDR**：标记接管期间所有 L3 输出为 "overridden"，记录接管起止时间
  - **回切**（Arbiter 解除接管）时：M5 重启 MPC（积分项重置）；M7 重启 SOTIF；M1 恢复 ODD 周期；ASDR 标记 "override_released" 事件
  ```

---

## §12 M8 HMI / Transparency Bridge

### Patch §12.2-01 [F-P1-D1-010 + F-P2-D1-026] — SAT 自适应触发表

- **§12.2 SAT 三层透明性设计**末尾新增：
  ```
  #### SAT 自适应触发规则
  
  与早期 v1.0 草案"全层全时展示"不同，v1.1 采用按场景自适应触发（基于 R5 USAARL 2026-02 + NTNU 2024 透明度悖论实证）：
  
  | 层 | 触发条件 |
  |---|---|
  | **SAT-1（现状）** | 全时展示（持续刷新）|
  | **SAT-2（推理）** | 按需触发：(1) COLREGs 冲突检测；(2) M7 SOTIF 警告激活；(3) M7 IEC 61508 故障告警；(4) 操作员显式请求 |
  | **SAT-3（预测）** | 基线展示 + 优先级提升：当 TDL（Time to Decision Latency）压缩到 < 30 s 时自动推送 + 加粗 |
  
  这一策略遵循 Veitch 2024 "可用时间是最重要因子" + USAARL 2026-02 "透明 + 被动 = 最差状态" 实证结论。
  ```

### Patch §12.3-01 [F-P1-D1-010 + F-P2-D1-026] — 差异化视图按角色 + 场景双轴

- **§12.3 差异化视图设计**：
  - **新增段落**：
    ```
    差异化视图按"角色 + 场景"双轴：
    - 角色轴（ROC vs 船长）：信息密度差异（ROC 完整数字；船长简化可视化）
    - 场景轴（常态 vs 冲突 vs MRC）：
      - 常态：仅 SAT-1 全展示，SAT-2/3 简化
      - 冲突：SAT-2 推送展开（按 §12.2 触发规则）
      - MRC：SAT-3 优先级最高，全屏 / 加粗展示 + 接管时窗倒计时
    ```

### Patch §12.5-01 [F-P2-D1-025] — [R23] 作者归属

- **行 882–885**：
  - **旧**：`[R23] Hareide et al.（NTNU Shore Control Lab，2022）`
  - **新**：`[R23] Veitch & Alsos（2022）"From captain to button-presser"，Journal of Physics: Conference Series 2311(1), NTNU Shore Control Lab`

---

## §13 多船型参数化设计

### Patch §13.1-01 [F-P2-D4-027 配套] — Capability Manifest 自定义规范注明

- **§13.1 设计原则**末尾新增：
  ```
  > **注**：本设计的 Capability Manifest 是**自定义规范**，参考 MOOS-IvP（Benjamin 2010）+ awvessel.github.io 开源舰艇 Manifest 格式。海事行业目前无专属 Capability Manifest 标准（Sea Machines / Kongsberg 商业机密；DDS-X 通用 IoT Schema 不直接对接海事）。本设计自定义规范作为 v1.0 原创工程实践记录，待行业标准成熟后可迁移。
  ```

### Patch §13.5-01 [F-P2-D4-027] — 长江内河船扩展路径修订

- **§13.5 表"长江内河船"行**：
  - **旧**：`仅 M6 规则库`
  - **新**：`M6 规则库（COLREGs → 内水规则）+ 架构级修改（非纯参数化扩展）。须 (a) §9 M6 增加规则库插件接口（rules_loader），(b) §13.2 Capability Manifest 增加 rules_lib_path 字段。**扩展代价**：内河版本须独立分支版本，不能仅靠静态参数文件切换。`

### Patch §9-01 [F-P2-D4-027 配套] — M6 规则库插件接口（可选 — 取决于内河版本是否实施）

- 注：仅在确认要支持内河版本时执行
- **§9 末尾新增小节 §9.6 规则库插件接口**：
  ```
  M6 支持规则库可插拔（v1.0 默认 COLREGs 1972/2002；v1.x 可扩展中国内水规则）：
  
  接口定义：
  ```cpp
  class ColregsRulesPlugin {
  public:
      virtual std::vector<Rule> getRules() const = 0;
      virtual std::string getRuleSetName() const = 0;
      virtual bool applyRule(const TrafficScenario& scene, RuleDecision& out) = 0;
  };
  ```
  
  Capability Manifest 增加字段：`rules_lib_path: ./plugins/colregs_1972.so`（默认）或 `./plugins/inland_china.so`（内河）
  ```

---

## §14 CCS 入级路径映射

### Patch §14.2-01 [F-P1-D1-009] — DNV 版本错（同 §2.3 patch）

- **行 980 表头**：
  - **旧**：`DNV-CG-0264（2018）§4`
  - **新**：`DNV-CG-0264（2025.01）§4`

### Patch §14.4-01 [F-P1-D7-028] — ODD-Spec 描述修订

- **行 1014–1016 ODD-Spec 行**：
  - **旧**：`三轴包络定义，CCS 审查基础`
  - **新**：`OE 框架定义（Rødseth 2022 二轴 [R8] + H 轴 IMO MASS Code D1–D4 扩展 [R2]，详见 §3.2 三层概念框架），CCS 审查基础。**前置依赖**：F-P0-D1-006 + F-P0-D6-015 修订完成 + ADR-002 + ADR-003 通过架构评审委员会批准后方可提交 AIP。`

---

## §15 接口契约总表（5 条 P1 集中修复 + 1 条 P2）

### Patch §15.1-01 [F-P1-D4-031] — World_StateMsg IDL 完善

- **§15.1 IDL 定义**新增 / 修订 World_StateMsg：
  ```
  # World_StateMsg (发布者: M2, 频率: 4 Hz — M2 内部以 2 Hz 输入聚合 + 1 次插值/外推)
  message World_StateMsg {
      timestamp        stamp;
      TrackedTarget[]  targets;          // 含 cpa_m / tcpa_s（M2 计算）
      OwnShipState     own_ship;         // 取自最近 FilteredOwnShipState 快照
      ZoneConstraint   zone;             // ENC 约束：zone_type / in_tss / in_narrow_channel
      float32          confidence;
      string           rationale;        // SAT-2: 聚合质量摘要
  }
  
  # 注：目标速度 = 对地（sog/cog from TrackedTarget）；自身速度 = 对水（u/v from FilteredOwnShipState）+ 海流估计
  ```

### Patch §15.1-02 [F-P1-D4-033] — ASDR_RecordMsg IDL（新增）

- **§15.1 IDL 定义**新增：
  ```
  # ASDR_RecordMsg (发布者: 所有 L3 模块, 频率: 事件 + 2 Hz)
  message ASDR_RecordMsg {
      timestamp     stamp;
      string        source_module;     // "COLREGs_Engine"|"Risk_Monitor"|"Avoidance_Planner"|"Checker"|"Target_Tracker"|"M7"|...
      string        decision_type;     // "encounter_classification"|"avoid_wp"|"cpa_alert"|"checker_veto"|"sotif_alert"|...
      string        decision_json;     // JSON 序列化（兼容 ASDR §5.2 AiDecisionRecord schema）
      bytes         signature;         // SHA-256 防篡改（ASDR §13）
  }
  ```

### Patch §15.1-03 [F-P1-D4-034] — Reflex Arc 协议 IDL（新增）

- **§15.1 IDL 定义**新增两条：
  ```
  # EmergencyCommand (发布者: Reflex Arc, 订阅者: L5, 频率: 事件)
  message EmergencyCommand {
      timestamp     trigger_time;
      float32       cpa_at_trigger_m;
      float32       range_at_trigger_m;
      string        sensor_source;       // "fusion"|"lidar_emergency"|...
      enum action;                       // STOP|REVERSE|HARD_TURN
      float32       confidence;
  }
  
  # ReflexActivationNotification (发布者: Reflex Arc, 订阅者: M1 (L3), 频率: 事件)
  message ReflexActivationNotification {
      timestamp     activation_time;
      string        reason;
      bool          l3_freeze_required;  // true → L3 进入 OVERRIDDEN 模式（同 §11.8）
  }
  ```

### Patch §15.1-04 [F-P1-D4-035] — RouteReplanRequest IDL（新增）

- **§15.1 IDL 定义**新增：
  ```
  # RouteReplanRequest (发布者: M3, 订阅者: L2, 频率: 事件)
  message RouteReplanRequest {
      timestamp     stamp;
      enum reason;                       // ODD_EXIT|MISSION_INFEASIBLE|MRC_REQUIRED|CONGESTION
      float32       deadline_s;
      string        context_summary;     // SAT-2 摘要供 L2 / ROC 阅
      Position      current_position;
      Position[]    exclusion_zones;     // 须避开区域（如冲突 TSS）
  }
  ```

### Patch §15.2-01 [F-P1-D4-031~035 + F-P1-D5-012 + F-P2-D5-013 + F-P2-D4-038] — 接口矩阵补全

- **§15.2 接口矩阵**完整重写：
  ```
  | 发布者 → 订阅者 | 消息类型 | 频率 | 关键内容 |
  |---|---|---|---|
  | L1 Voyage Order → M3 | VoyageTask | 事件 | 任务级参数（departure/destination/eta_window）|
  | L2 WP_Generator → M3,M5 | PlannedRoute | 1 Hz / 事件 | 航点序列 |
  | L2 Speed_Profiler → M3,M5 | SpeedProfile | 1 Hz / 事件 | 速度曲线 |
  | Multimodal Fusion → M2 | TrackedTargetArray | 2 Hz | 目标列表 |
  | Multimodal Fusion → M2 | FilteredOwnShipState | 50 Hz | 自身状态 |
  | Multimodal Fusion → M2 | EnvironmentState | 0.2 Hz | 能见度/海况/交通密度 |
  | M1 → M2,M3,M4,M5,M6,M7,M8 | ODD_StateMsg | 1 Hz | ODD 子域、AutoLevel、TMR/TDL |
  | M1 → M4 | Mode_CmdMsg | 事件 | 行为集约束变更 |
  | M2 → M3,M4,M5,M6 | World_StateMsg | 4 Hz | 目标列表（含 CPA/TCPA）+ 自身状态 + ENC 约束 |
  | M3 → M4 | Mission_GoalMsg | 0.5 Hz | 当前任务目标、航段、ETA |
  | M3 → L2 Voyage Planner | RouteReplanRequest | 事件 | 重规划请求（ODD 越界 / MRC / 冲突）**[F-P1-D4-035 新增]** |
  | M4 → M5 | Behavior_PlanMsg | 2 Hz | 行为类型、允许航向/速度区间 |
  | M6 → M5 | COLREGs_ConstraintMsg | 2 Hz | 规则约束集、时机阶段 |
  | M5 → L4 Guidance Layer | Trajectory_CmdMsg | 10 Hz | (ψ_cmd, u_cmd, ROT_cmd) **[L4 在避让模式下旁路自身 LOS 转发，详见 F-P1-D4-032 跨团队对齐方案]** |
  | M7 → M1 | Safety_AlertMsg | 事件 | 告警类型、严重度、MRC 请求 |
  | X-axis Checker → M7 | CheckerVetoNotification | 事件 | Checker 否决事件 **[F-P0-D3-002 + F-P2-D3-036 新增]** |
  | Reflex Arc → L5 | EmergencyCommand | 事件 | 紧急停车/转向 **[F-P1-D4-034 新增]** |
  | Reflex Arc → M1 | ReflexActivationNotification | 事件 | 通知 L3 进入 OVERRIDDEN 模式 **[F-P1-D4-034 新增]** |
  | Hardware Override Arbiter → M1 | OverrideActiveSignal | 事件 | 通知 L3 切换到 OVERRIDDEN 模式 **[F-P2-D6-037 新增]** |
  | M1,M2,M4,M6,M7 → ASDR | ASDR_RecordMsg | 事件 + 2 Hz | 决策追溯日志 **[F-P1-D4-033 新增]** |
  | M1,M2–M7 → M8 | SAT_DataMsg | 10 Hz | 各模块 SAT-1/2/3 数据流 |
  | M8 → ROC/Captain | UI_StateMsg | 50 Hz | 渲染就绪的 HMI 数据 |
  ```

---

## §16 参考文献

### Patch §16-01 [F-P3-D1-029] — 孤立引用清理

- **[R22] Neurohr 2025**：
  - 检查是否在 §11/§12 SOTIF·ROC 认证段落补引；如未补引则从 §16 删除
  - 详见 99-followups.md
- **[R13] Albus 1991**：
  - 检查 §8 M4 是否参考 NIST RCS；如是则 §8 决策依据补 [R13]；否则从 §16 删除
  - 详见 99-followups.md

### Patch §16-02 [F-P1-D9-024] — [R21] 处理

- 见 §10.5 patch（删除 [R21] 或标注 "[待 JMSE 数据库确证]"）

---

## 附录 A 术语对照（v1.1 新增）[F-P2-D1-039]

- **§16 后新增附录 A**：
  ```
  ## 附录 A 术语对照（早期 Stage 框架 → v1.0 模块）
  
  | 早期框架 | v1.0 模块 | 备注 |
  |---|---|---|
  | Stage 1（看，看局）| M2 World Model | 感知融合 → 世界视图 |
  | Stage 2（判，判局）| M6 COLREGs Reasoner + 部分 M4 | 规则推理 + 行为仲裁 |
  | Stage 3（动，动作）| M5 Tactical Planner | 双层 MPC 轨迹生成 |
  | Stage 4（盯，并行监控）| M7 Safety Supervisor + M1 ODD Manager | Doer-Checker 双轨 + ODD 调度 |
  
  ## 附录 B 术语对照（早期 Doer-Checker → v1.0 双轨）
  
  | 早期框架 | v1.0 实现 | 继承关系 |
  |---|---|---|
  | Checker-D（决策层 Checker）| M7 IEC 61508 轨道（功能安全） | Checker-D 简化版 |
  | Checker-T（轨迹层 Checker）| M7 SOTIF 轨道（功能不足） | Checker-T 扩展 + ISO 21448 |
  
  ## 附录 C 章节顺序说明 [F-P2-D1-039]
  
  v1.0 §9 = M6 COLREGs Reasoner（先）；§10 = M5 Tactical Planner（后）。章节顺序按"主审依赖"排列：M6 输出约束 → M5 消费约束。模块编号 M5/M6 不变。
  ```

---

## Patch 应用顺序建议（B 档执行路径）

**先决（合并前）**：ADR-001/002/003 通过架构评审委员会审议（详见 `08c-adr-deltas.md`）

**第一批 patch（不依赖 ADR）— 19 项**：
1. §1.2-01（旧术语）
2. §6-01, §6.3.1-01, §6.4-01（M2 / Multimodal Fusion）
3. §7-01（M3 上游）
4. §10-01, §10.5-01, §10-02, §10-03（M5）
5. §11.3-01, §11.3-02, §11.4-01（M7 部分修订）
6. §12.2-01, §12.3-01, §12.5-01（M8 SAT）
7. §13.1-01, §13.5-01（多船型）
8. §14.2-01（CCS 版本）
9. §15.1-01~04 + §15.2-01（接口契约总表）
10. §16-01, §16-02, 附录 A/B/C（参考文献 + 附录）

**第二批 patch（依赖 ADR）— 16 项**：
11. §2.2-01（依赖 ADR-002）
12. §2.2-02, §2.2-03（自然延伸）
13. §2.3-01（依赖 ADR-001 候选 C 决议）
14. §2.4-01（自然延伸 F-P1-D1-010）
15. §2.5-01（依赖 ADR-001）
16. §3.2-01（依赖 ADR-003）
17. §3.4-01, §3.3-01, §3.5-01, §3.6-01（自然延伸）
18. §4-01, §4-02（自然延伸）
19. §11.2-01（依赖 ADR-001）
20. §11-01, §11-02, §11-03（依赖 ADR-001 §11.6/§11.7/§11.8）
21. §14.4-01（依赖 ADR-002 + ADR-003）

**跨团队对齐（不阻塞 patch 合并，但 patch 完成后须立即启动）**：
- F-P1-D4-032：M5 → L4 schema 方案 A vs B 决议 — L3 + L4 团队
- F-P1-D4-033：ASDR_RecordMsg IDL 与 ASDR 团队对齐
- F-P1-D4-034：Reflex Arc spec 量化 — L3 + Reflex Arc + Multimodal Fusion 团队
- F-P0-D3-002 + F-P2-D3-036：M7 ↔ Checker 协调协议 — L3 + Deterministic Checker 团队
