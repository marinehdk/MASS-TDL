# ADR 增量（v1.0 → v1.1 顶层决策修订）

> 本文件含 **3 条 ADR 增量**，对应 5 条 P0 finding。每条 ADR 须经 v1.0 §1 文档变更要求（"系统架构评审委员会批准"）通过后方可合并 patch list（08a）的依赖项。
>
> **完成时间**：2026-05-05
>
> **决策树落点**：B 档（结构性修订 + ADR）— 详见 `_temp/decision-trace.md`
>
> **ADR 模板**：状态 / 日期 / 上下文 / 决策 / 后果 / 证据

---

## ADR-001：决策四 Doer-Checker 双轨架构修订

- **状态**：提议（待架构评审委员会批准）
- **日期**：2026-04-30 提议；2026-05-05 完稿
- **关联 finding**：F-P0-D3-001、F-P0-D3-002、F-P0-D1-008、F-P1-D3-003、F-P1-D6-004、F-P1-D7-005、F-P2-D3-036
- **依赖 patch**：§2.5-01、§11.2-01、§11.3-01/02、§11.4-01、§11-01、§11-02、§11-03

### 上下文

v1.0 §2.5 决策四规定 "M1–M6 = Doer，M7 = Checker；Checker 逻辑须比 Doer 简单 100×"，§11 实现含 ARBITRATOR "直接向 M5 注入安全轨迹" 权限。审计发现三项核心问题：

1. **F-P0-D1-008**："100× 简单" 在 [R12] Jackson 2021 / IEC 61508 / DO-178C 中均无量化要求（仅 R12 称 "smaller and formally verifiable"）。工业实践 50:1~100:1（Boeing 777 / Airbus FBW）是设计目标，非规范强制。

2. **F-P0-D3-001**：M7 ARBITRATOR 注入轨迹 = Checker 持有规划逻辑 = 违反 Doer-Checker 简化原则。Boeing 777 Monitor / NASA Auto-GCAS / Simplex 均不动态生成轨迹，仅触发预定义动作。

3. **F-P0-D3-002**：v1.0 §11 完全未提 v3.0 X-axis Deterministic Checker。实际 Deterministic Checker 文档（`docs/Init From Zulip/MASS ADAS Deterministic Checker/`）已定义独立的 CheckerOutput VETO 协议，但**完全无 M7 ↔ Checker 桥接**——两者协调机制为零。

### 决策

修订决策四 + §11 全章重构，落实分层 Doer-Checker 架构：

#### 1. 措辞修订（§2.5 + CLAUDE.md §4）

将 "Checker 简单 100×" 改为：

> Checker 的代码规模须显著小于 Doer——工业实践目标 SLOC 比例 50:1 ~ 100:1（Boeing 777 Monitor ~50:1；Airbus FBW COM/MON >100:1）[R4]，以支持 DO-178C Level A / IEC 61508 SIL 2 形式化验证。**此为设计目标，非规范强制值**——IEC 61508 / DO-178C 均无量化倍数要求 [R12]。

#### 2. M7 ARBITRATOR 行为约束（§11.2）

M7 ARBITRATOR **不再动态生成轨迹**，仅触发**预定义 MRM 命令集**（M7 持 VETO 权 + 命令集索引）：

```
MRM 命令集（§11.6 新增）：
- MRM-01: 减速至 4 kn 维持航向（30 秒内）
- MRM-02: 停车（速度 → 0），通知 ROC 介入
- MRM-03: 紧急转向（基于威胁方向，预定义反向避让）
- MRM-04: 抛锚序列（仅适用于近岸场景，水深允许）
```

M7 输出 MRM 命令集索引，由 M5 解析并执行；M7 不持有 MPC / 规划逻辑。

#### 3. M7 vs X-axis Deterministic Checker 分层（§11.7 新增）

| 维度 | M7 Safety Supervisor | X-axis Deterministic Checker |
|---|---|---|
| 层级 | L3 内部 Checker | 系统级 Checker（跨 L2/L3/L4/L5）|
| 类型 | IEC 61508 + SOTIF 双轨监控 | 确定性规则 VETO |
| 输出 | 假设违反告警 + MRC 请求 | VETO + nearest_compliant 替代 |
| 频率 | 周期 + 事件 | 与 Doer 同频 |
| 优先级 | 中等（向 M1 报告，M1 仲裁） | 最高（直接 VETO Doer 输出） |

**协调协议**：
- X-axis 否决 M5 输出 → 向 M7 发 `CheckerVetoNotification`（事件，详见 §15.1 IDL）；M7 将否决率纳入 SOTIF 假设违反检测：**Checker 否决率 > 20%（即 20% 的 100 周期窗口内否决次数）→ M7 升级 SOTIF 告警 "COLREGs 推理可信度下降"**
- M7 触发 MRC 请求 → 经 M1 仲裁后下发 MRM；X-axis 不干预 MRC 内部流程
- X-axis 与 M7 通过独立总线通信，**不共享代码 / 库 / 数据结构**（决策四独立性约束）

#### 4. M7 自身降级行为（§11.6 新增）

- **心跳监控**：M7 心跳由 X-axis Deterministic Checker（外部，跨层）监控，频率 1 Hz；同时 M1 订阅 M7 心跳作内部参考
- **失效模式**：Fail-Safe（必须）—— M7 失效时强制触发保守 MRM-01；不允许 Fail-Silent
- **失效后系统降级**：M7 失效 → 系统降级到 D2（船上有海员备援）；禁止 D3/D4 运行

#### 5. SIL 2 SFF 补充（§11.4）

补 IEC 61508-2 Table 3 要求：

> 单通道（HFT=0）+ 诊断覆盖 DC ≥ 90% **+ Safe Failure Fraction SFF ≥ 60%（Type A）/ ≥ 70%（Type B）**。**推荐配置**：关键安全路径采用 HFT=1 双通道冗余，可降低 SFF 要求至 ≥ 90%。

#### 6. M7 SOTIF 轨道 CPA 计算独立性（§11.3）

M7 PERF 监控读 M2 输出的 CPA 但不依赖 M2 推理。**M2 状态 = DEGRADED 或 FAILED 时，M7 PERF 自动降级到保守阈值**（CPA_safe = 0.5 nm 强制告警），不依赖 M2 数值。

### 后果

**正面**：
- v1.0 措辞从"100× 规范要求"改为"50:1~100:1 设计目标"，与 Boeing 777 / Airbus FBW 工业实践一致；不会被 CCS 审查质疑为自设要求
- M7 不再持有规划逻辑，符合 Boeing/NASA/Simplex 主流 Monitor 设计；满足 Doer-Checker 简化原则
- M7 ↔ X-axis 分层协议明确，不再冲突；架构闭环
- SIL 2 SFF 补充使 IEC 61508 合规检查清单完整

**负面 / 待协调**：
- §11 全章重构（§11.2 + §11.3 + §11.4 + 新增 §11.6 / §11.7 / §11.8）— 工作量较大
- 跨团队协议：CheckerVetoNotification 消息 IDL 须与 Deterministic Checker 团队对齐（已写入 §15.1 + 跨团队对齐工作清单）
- MRM 命令集（§11.6）的具体参数（如 4 kn / 30 秒）须 FCB 实船 HAZID 校准

### 证据

- **学术来源（R4 调研）**：
  - Boeing 777 PFCS：1 Command + 2 Monitor，~50:1 SLOC，FAA 30 年认证
  - Airbus FBW COM/MON：硬件 + 软件多样化，DAL A
  - NASA Auto-GCAS：Monitor 触发预定义机动，98.5% 防护率
  - Jackson et al. (2021) Certified Control：Monitor 几百行 vs 导航栈数万行；持"证书"验证
  - Simplex Architecture (1996+)：HPC + HAC + Decider，HAC 是预定义保守控制器
- **工业来源（R1 + R7 调研）**：
  - Sea Machines SM300：Linux PC（Doer）+ Siemens S7 PLC（Checker），DNV/BV 入级
  - DNV-CG-0264 §9.3：监控系统是模块级
  - Ocean Infinity Armada 78：DNV-CG-0264 首个远程操控合规声明
- **Phase 5 跨层实证**：`docs/Init From Zulip/MASS ADAS Deterministic Checker/Deterministic_Checker §1.3 + §17`

---

## ADR-002：决策一 ODD 作为组织原则修订

- **状态**：提议（待架构评审委员会批准）
- **日期**：2026-04-30 提议；2026-05-05 完稿
- **关联 finding**：F-P0-D1-006（H 轴无来源）、F-P1-D1-007（IMO 章节错引）、F-P1-D7-028（CCS 认证链）
- **依赖 patch**：§2.2-01/02/03、§14.4-01（间接：§3.2-01 详见 ADR-003）

### 上下文

v1.0 §2.1 决策一规定"ODD 是 TDL 组织原则而非监控模块"，§2.2 引用 [R8] Rødseth 2022 支持"海事 ODD 必须包含人机责任分配维度（H 轴）"。审计发现：

1. **F-P0-D1-006**：Rødseth 2022 实际定义是二轴（O = S_ext × S_int + FM），无 H 轴。R3 PMC 全文 + R6 IMO MASS Code（无轴线，目标型）+ R7 DNV AROS（三维认证空间，非 ODD 定义）三轮交叉确认。H 轴是 v1.0 无标注的内部扩展。

2. **F-P1-D1-007**：v1.0 §2.2 称 "IMO MASS Code 在第 15 章明确要求识别 OE 越界"。R6 调研确认：MSC 草案 Part 3 第 15 章是"维护与维修"（Maintenance），ODD/OE 要求实际在 Part 2 Chapter 1。

3. **F-P1-D7-028**：v1.0 §14.4 关键证据文件清单中 ODD-Spec 描述为"三轴包络定义，CCS 审查基础"，将三轴 ODD（E×T×H）列为 AIP 申请认证依据。但 H 轴无来源 → AIP 申请论证链断裂。

### 决策

保留"ODD 作为组织原则"主张（方向正确，是 D3/D4 自主等级下唯一能保证白盒可审计的设计选择），但修订引用与论证：

#### 1. 引用修订（§2.2）

- 将 "Rødseth 等（2022）明确指出，海事 ODD 必须包含人机责任分配维度（H 轴）...本设计采纳三轴（E×T×H）框架 [R8]" 改为：

> Rødseth 等（2022）定义二轴 OE：O = S_ext × S_int + FM（Function Mapping，状态→人机控制权分配）[R8]。本设计在 Rødseth 框架上扩展，将 FM 具体化为 H 轴（Human Availability，含 D1–D4 四级自主度），参考 IMO MASS Code D1–D4 定义 [R2]。三层 ODD 概念框架（Rødseth 二轴 + DNV-CG-0264 AROS 三维认证空间 + SAE J3259 属性树）的具体定义见 §3.2（详见 ADR-003）。

- 将 "IMO MASS Code 草案在第 15 章" 改为 "IMO MASS Code 草案在 Part 2 Chapter 1（运行环境/运行包络 OE）"

- TMR/TDL 引用补充：从 [R8] 改为 "Rødseth 概念框架 [R8] + Veitch 60s 实证基线 [R4]"

#### 2. 论证扩展（§2.1 段落补充）

§2.1 末尾增加段落：

> 本决策的"ODD 作为组织原则"立场与 v3.0 Kongsberg-Benchmarked 架构兼容：v3.0 通过 IACS UR E26/E27（Z-TOP）+ 各层 Mode Switcher + X-axis Deterministic Checker 分布式实现 ODD 监控；本设计将 L3 内部 ODD 集中到 M1 ODD/Envelope Manager，与 v3.0 系统层 ODD 监控形成分层协作（M1 = L3 内部 OE 调度枢纽；系统层 ODD = IACS UR + Z-TOP 网络包络 + X-axis 跨层 VETO）。M1 不替代系统层 ODD 监控，而是补充 L3 内部场景的可审计调度。

#### 3. CCS 认证链修复（§14.4）

将 ODD-Spec 描述 "三轴包络定义，CCS 审查基础" 改为：

> OE 框架定义（Rødseth 2022 二轴 [R8] + H 轴 IMO MASS Code D1–D4 扩展 [R2]，详见 §3.2 三层概念框架），CCS 审查基础。**前置依赖**：F-P0-D1-006 + F-P0-D6-015 修订完成 + ADR-002 + ADR-003 通过架构评审委员会批准后方可提交 AIP。

### 后果

**正面**：
- 论证链可溯源：每个轴 / 维度都有明确来源（Rødseth / IMO / DNV / SAE）
- CCS AIP 申请论证链闭合，不会被审查员追问"H 轴依据"
- 与 v3.0 Kongsberg 架构形成分层协作，避免被解读为"重复实现系统层职责"

**负面 / 待协调**：
- §3.2 三轴公式须重写为三层概念框架（详见 ADR-003）
- §14.4 ODD-Spec 文档须按修订版重新撰写后才能提交 AIP

### 证据

- **R3 调研（PMC 全文 + 多源）**：
  - Rødseth (2022) JMSE 27(1)：O = S_ext × S_int + FM（A 级，PMC 全文确认）
  - Fjørtoft & Rødseth (2020) NMDC：OE 是工程契约，可分布式（A 级）
  - SAE J3259 (2021) + ISO 34501:2022：ODD 属性树，无轴线（A 级）
  - DNV-CG-0264 + AROS (2025.01)：三维认证空间，非 ODD 定义（A 级）
- **R6 调研（IMO MASS Code）**：
  - MSC 草案 Part 2 Chapter 1 = OE 要求；Part 3 Chapter 15 = 维护与维修
- **R7 调研（CCS i-Ship）**：
  - CCS AIP 流程：ConOps + ODD-Spec 是核心审查文件，框架须可溯源

---

## ADR-003：ODD 框架（§3.x）三层概念重写

- **状态**：提议（待架构评审委员会批准）
- **日期**：2026-04-30 提议；2026-05-05 完稿
- **关联 finding**：F-P0-D6-015（三轴公式无来源）、F-P1-D6-016（TDL 系数）、F-P1-D6-017（ODD 阈值）、F-P2-D6-018（EDGE/Conformance 阈值）
- **依赖 patch**：§3.2-01、§3.3-01、§3.4-01、§3.5-01、§3.6-01

### 上下文

v1.0 §3.2 明文称"采纳 Rødseth 2022 OE 扩展定义"，给出三轴公式 O = E × T × H。§3.6 Conformance_Score 公式（w_E + w_T + w_H）完全依赖三轴框架。但：

1. **F-P0-D6-015**：Rødseth 2022 实际是二轴（无 H 轴）。三轴公式无权威来源，整个 §3 ODD 框架的引用链断裂。

2. **F-P1-D6-016**：§3.4 TDL 公式 `min(TCPA_min × 0.6, T_comm_ok, T_sys_health)` 中 0.6 系数无注释。

3. **F-P1-D6-017**：§3.3 四个 ODD 子域阈值（CPA/TCPA）无引用来源。

4. **F-P2-D6-018**：§3.5 EDGE 状态阈值 "margin < 20%" + §3.6 Conformance_Score 阈值（>0.8 IN, 0.5–0.8 EDGE, <0.5 OUT）无来源。

### 决策

§3 ODD 框架按**三层概念框架**重写（与 ADR-002 配套）：

#### 1. §3.2 三层概念框架（核心条目）

将三轴公式段落改为：

```
本设计基于三层概念框架：

1. 数学定义层（Rødseth 二轴）[R8]：
   O = S_ext × S_int + FM
   - S_ext = 外部环境状态（气象 / 交通 / 水域）
   - S_int = 内部技术状态（传感器 / 通信 / 系统健康）
   - FM = Function Mapping（状态 → 人机控制权分配）

2. 工程实现层（v1.1 三轴扩展，本设计选择）：
   O = E × T × H
   - E（Environmental）= S_ext 工程实现（Beaufort 风级 / 能见度 / Hs / 流速 / 交通密度 / 水域类型）
   - T（Technical）= S_int 工程实现（GNSS 状态 / 雷达健康 / 通信延迟 / 系统温度）
   - H（Human）= FM 具体化（D1–D4 自主等级 + ROC 接管能力 + TMR/TDL）
   注：H 轴是本设计在 Rødseth 二轴框架上的具体化扩展，非 Rødseth 2022 原文定义。
   v1.1 三轴是工程实现选择，参考 IMO MASS Code D1–D4 定义 [R2]。

3. 认证审查层（DNV-CG-0264 AROS 三维认证空间）[R7]：
   (Mission Complexity, Environmental Difficulty, Human Independence)
   用于风险分级和功能级认证，**非 ODD 定义**。本设计的 ODD 框架（E×T×H）映射到 AROS 三维认证空间作为合规证据。

4. 属性表达层（SAE J3259 属性树）[新增]：
   Scenery / Environmental / Dynamic / Connectivity / Temporal
   用于 ODD 子域属性的离散化表达（与 v1.0 §3.3 子域分类兼容）。
```

#### 2. §3.4 TDL 公式系数注释

`TDL = min(TCPA_min × 0.6, T_comm_ok, T_sys_health)` 后加注：

> 0.6 = TCPA 60% 留 40% 余量供操作员接管，基于 Veitch 2024 TMR ≥ 60s 实证基线 [R4] 估算。HAZID 中以实际船型校准。

#### 3. §3.3 / §3.5 / §3.6 阈值标注

- §3.3 子域阈值表加脚注："FCB 特定设计值，参考 Wang 2021 [R17] + TCPA 文献范围 5–20 min [R2 文献]，HAZID 校正"
- §3.5 EDGE 阈值 "margin < 20%" 加注 "[初始值，待 HAZID 校正]"
- §3.6 Conformance_Score 阈值（0.5/0.8）加脚注 "[初始建议值，待 FCB 场景 HAZID 校正]"
- §3.6 三轴权重 (w_E, w_T, w_H) = (0.4, 0.3, 0.3) 加脚注 "FCB 特定设计值；H 轴权重须 HAZID 用 Veitch 2024 TMR 实证 [R4] 校准"

### 后果

**正面**：
- §3 框架可溯源，每层都有权威来源（Rødseth / DNV / SAE / IMO）
- v1.0 三轴公式 E×T×H 保留作为"工程实现层"，不是凭空发明而是 Rødseth 二轴的具体化扩展
- 与 ADR-002 形成完整论证链，CCS AIP 申请可顺利通过

**负面 / 待协调**：
- §3 全章重写，工作量中等
- 所有阈值（0.6 / 20% / 0.5/0.8 / w_E/w_T/w_H 等）须 HAZID 校准——校准结果回填本审计后续版本

### 证据

- **R3 调研**：Rødseth 2022 PMC 二轴 + Fjørtoft & Rødseth 2020 + SAE J3259 + ISO 34501
- **R5 调研**：Veitch 2024 TMR 60s 实证基线
- **R7 调研**：DNV-CG-0264 AROS 三维认证空间
- **F-P0-D1-006**：根因 finding（与 ADR-002 共享）
- 所有阈值待 HAZID 校准的工程合理性 — Wang 2021 [R17] + Frontiers 2021 综述 [R2]

---

## ADR 增量 vs Patch 依赖关系

```
ADR-001（决策四 + §11 重构）
   └─→ 依赖 patch: §2.5-01, §11.2-01, §11.3-01/02, §11.4-01, §11-01, §11-02, §11-03

ADR-002（决策一 + §2.2 + §14.4 修订）
   └─→ 依赖 patch: §2.2-01/02/03, §14.4-01
   └─→ 与 ADR-003 协同：§14.4 ODD-Spec 条目修订须依赖 ADR-003 §3.2 三层框架

ADR-003（§3 ODD 框架三层概念重写）
   └─→ 依赖 patch: §3.2-01, §3.3-01, §3.4-01, §3.5-01, §3.6-01
   └─→ 与 ADR-002 协同：§3.2 是 ADR-002 §2.2 论证的具体展开
```

**架构评审委员会审议顺序建议**：
1. ADR-002 + ADR-003 同时审议（同源 P0 finding F-P0-D1-006 / F-P0-D6-015 + 同核心证据 R3）
2. ADR-001 单独审议（决策四 + §11 重构是独立工作）

**审议批准后执行顺序**：
1. ADR 通过 → patch list（08a）第二批 patch 解锁 → 与第一批 patch 一并合并 → v1.1 草稿出
2. v1.1 草稿 → 简化 Phase 3 + Phase 6 复审 → 确认所有 P0/P1 关闭 → 准备 CCS AIP 申请
