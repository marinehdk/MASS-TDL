# 阶段 4 · 架构方案最优性评判（D9 核心）

> 每对象做 "v1.0 立场 vs v3.0 Kongsberg 立场 vs 学术最优 vs 工业其他参考 → 综合判定" 五段对照。
> 来源依据：阶段 2 证据库 (`03-evidence-base.md`)、阶段 3 findings (`_temp/findings-raw.md`)。
>
> 完成时间：2026-05-05
>
> 综合判定字典：
> - **保留**：架构方向正确，仅需细节修补（≤ 5 处 P2/P3 finding）
> - **修订**：方向正确但有 P0/P1 级缺陷需补救（保留主框架，调整局部）
> - **替换**：方向不正确，须用 v3.0 / 学术最优替代

---

## 评判模板

```
{对象名} 评判
├─ v1.0 立场: …
├─ v3.0 Kongsberg 立场: …
├─ 学术最优: …（≥3 篇 A/B 级文献）
├─ 工业其他参考: …（≥3 个独立来源）
└─ 综合判定: 保留 / 修订 / 替换 + 理由 + 影响范围
```

---

## 1. 决策一：ODD 作为组织原则

**v1.0 立场（§2.1，行 88–92）**：ODD 是 TDL 的"组织原则"而非监控模块；M1 ODD/Envelope Manager 是唯一权威的 ODD 状态来源，所有行为切换由 M1 状态变化驱动；算法不许各自维护"是否安全"的判断。**v1.0 §2.1 论据**：CCS 白盒可审计 + IMO MASS Code OE 越界识别要求 + Rødseth 2022 OE 工程实践。

**v3.0 Kongsberg 立场**：v3.0 没有显式声明"ODD 作为组织原则"。ODD 类似职责被分布到三处：(a) **Z-TOP 网络安全墙 + IACS UR E26/E27** 定义"系统是否在 IT/OT 安全包络内"；(b) **每层独立 Mode Switcher**（L1 voyage/transit/standby、L2 nominal/重规划、L3 transit/avoidance/MRC、L4 LOS/退化、L5 normal/limit-cycle）；(c) **X-axis Deterministic Checker** 跨层 VETO 越界决策。**v3.0 隐含立场**：ODD 是分布式职责，不集中到单一模块。

**学术最优（R3 + R6 一致结论）**：
- Rødseth (2022) JMSE 27(1)：O = S_ext × S_int + FM；OE 是数学定义，不是组织模块（A 级）
- SAE J3259 (2021) + ISO 34501:2022：ODD 是属性树（无轴线），不是状态机（A 级）
- Lloyd's Register MASS Research Paper v6 (2025.02)：ODD 认证 + 责任框架，将 OE 视为合规证据载体（A 级）
- Fjørtoft & Rødseth (2020) NMDC：显式区分汽车 ODD 与船舶 OE，主张 OE 是工程实践框架（A 级）

**工业其他参考（R1 + R7 + R8）**：
- DNV-CG-0264 + AROS Notations (2025.01)：ODD 完整性是子功能 9.8 警告设备的输入（A 级）
- Sea Machines SM300-NG：ODD 通过 Capability Manifest YAML 静态声明 + 运行时事件触发（A 级）
- Kongsberg K-MATE：ODD 体现为"建议+人工批准"的人机协同模式，不显式作为组织原则（A 级）
- IMO MSC 110 / MASS Code (2025-06)：要求"识别越出 OE"是目标型功能（不指定实现）（A 级）

**综合判定**：**修订**

**理由**：
1. **方向正确**：将 ODD 提升为组织原则（vs 监控模块）符合 IMO MSC 110 "目标型 OE 检测" + Lloyd's Register MASS 责任框架要求，是 D3/D4 自主等级下唯一能保证"白盒可审计"的设计选择。
2. **但实现方式过于集中**：v1.0 把 ODD 完全绑定到 M1 单模块，未显式说明与 v3.0 Z-TOP 网络包络 / X-axis VETO 的协调关系。F-P0-D3-002（X-axis Checker 协调缺失）已是 P0 finding。
3. **三轴框架无来源（F-P0-D1-006 + F-P0-D6-015）**：§2.1 + §3.2 引用 Rødseth 2022 支持三轴（E×T×H），但 R3 PMC 全文确认 Rødseth 是二轴。这是组织原则的根基性引用错误。

**修订范围（影响 §2.1 + §3.2 + §5）**：
- §2.1 决策理由保留"ODD 作为组织原则"主张，但补充与 Z-TOP（系统层）和 X-axis（系统层 Checker）的分层关系
- §3.2 三轴公式改为 Rødseth 二轴 + DNV-CG-0264 AROS 三维认证空间 + SAE J3259 属性树（三层概念）
- §5 M1 模块描述：保留"L3 内部 ODD 状态枢纽"角色，但明确 M1 不负责系统层 ODD（IACS UR + 网络安全包络）

**关联 finding**：F-P0-D1-006、F-P0-D6-015、F-P1-D1-007（IMO MASS Code 章节错引）

---

## 2. 决策二：8 模块功能分解（vs Kongsberg 5 模块）

**v1.0 立场（§2.3，行 110–117）**：L3 内部分 8 模块，按三层组织：(a) 包络层 = M1 + M3；(b) 决策规划层 = M2 + M4 + M5 + M6；(c) 安全接口层 = M7 + M8。**v1.0 弃用方案**：候选 A（7 模块）+ 候选 B（4+1 模块），但**未与 v3.0 Kongsberg 5 模块对照**（F-P1-D1-009）。

**v3.0 Kongsberg 立场（CLAUDE.md §2 + 基线 HTML）**：L3 内部 5 模块：
- Target Tracker（目标跟踪 + 意图估计）
- COLREGs Engine（规则推理）
- Avoidance Planner（避让路径生成 + 4-WP 输出）
- Risk Monitor（持续风险监控）
- Capability Assessment（自身能力声明，v3.0 新加 nt 标记）

v3.0 把 v1.0 的 M1 / M3 / M7 全部移到 **系统层**：
- M1 ODD Manager → IACS UR E26/E27（Z-TOP）+ Parameter Database（横向支持）
- M3 Mission Manager → L1 Mission Layer（独立层级）
- M7 Safety Supervisor → X-axis Deterministic Checker（系统层独立轴）

**学术最优（R2 + R3 + R4 + R8）**：
- Benjamin et al. (2010) MOOS-IvP：4 模块（pHelmIvP / pNodeReporter / pBasicContactMgr / pMarinePID）（A 级）
- Frontiers (2021) COLAV Survey：MPC/VO/IvP/RL 四路线均无 8 模块设计（A 级）
- Jackson 2021 Certified Control：Doer 单体 + 独立 Monitor，**不主张额外的"组织模块"**（A 级）
- Fjørtoft & Rødseth (2020)：OE 工程实践不要求 ODD 集中到单模块（A 级）

**工业其他参考（R1 + R8）**：
- DNV-CG-0264 §4：9 个子功能（不是 8 模块）；功能与模块不一一对应（A 级）
- Sea Machines SM300-NG：核心 4 行为（plan / avoid / hold / dock）+ 接管 UI；**无 ODD 单独模块**（A 级）
- Kongsberg K-MATE：scene-analysis vs control 两层；**无 Mission Manager**（A 级）
- IAI Katana / ATLAS ARCIMS：均为 4–5 模块工程实例（B 级）

**综合判定**：**修订（重构）**

**理由**：
1. **8 模块的"决策规划层"（M4/M5/M6 + M2 World Model）与 v3.0 Kongsberg 5 模块基本对齐**——M4↔Avoidance Planner、M5↔轨迹生成、M6↔COLREGs Engine、M2↔Target Tracker。v1.0 这四个模块是合理的，应保留。
2. **8 模块的"包络层 + 安全接口层"中 M1/M3/M7 三个模块是系统层职责被错位到 L3**：
   - M1 ODD/Envelope Manager 应承认与系统层 IACS UR / X-axis 的协作关系（不剥离，但需协调）
   - M3 Mission Manager 与 L1 Mission Layer 命名+职责冲突（M3 实际是"Voyage Tracker"+ETA 跟踪，不是真正的航次规划）
   - M7 Safety Supervisor 是"L3 内部小 Checker"，应明确与"系统层 X-axis Deterministic Checker"的分层（不是替代关系）
3. **未与 v3.0 对比是审计缺口**（F-P1-D1-009）：弃用方案表必须加 Kongsberg 5 模块作为候选 C。

**修订方向**（建议 v1.1 修订路径，不走 v2.0 替换）：
- 保留 8 模块的 D2 决策、修正模块间关系定义；
- M1 → 添加"L3-internal ODD"前缀，与系统层协调机制写入 §5；
- M3 → 重命名为"M3 Voyage Tracker"或保持 M3 名称但明确职责为"L1 任务令的 L3 局部跟踪 + ETA 投影"，避免误解为航次规划；
- M7 → §11 增加 §11.7 与 X-axis Deterministic Checker 的协作（已经是 F-P0-D3-002 的 fix）。

**影响范围**：v1.1 patch（§2.3 + §5 + §7 + §11.7），不需要 v2.0 重构。

**关联 finding**：F-P1-D1-009、F-P0-D3-002、F-P1-D1-022

---

## 3. 决策三：CMM 映射为 SAT-1/2/3

**v1.0 立场（§2.4，行 120–123）**：将 CMM（Coordinated Machine Mind / 决策可证伪）映射到 Chen ARL-TR-7180 SAT 三层透明性（SAT-1 现状 / SAT-2 推理 / SAT-3 预测）；每模块实现 `current_state()` / `rationale()` / `forecast(Δt)+uncertainty()` 三接口；M8 聚合发布。**v1.0 论点**："SAT-1+2+3 全部可见"提升态势感知且不增加工作负荷（已被 R5 推翻）。

**v3.0 Kongsberg 立场**：无显式 SAT 模型。透明性载体是 Shore Link via DMZ + IMO MASS 4-level 模式指示器（D1/D2/D3/D4 自主度状态显示）。v3.0 隐含立场：透明性是"模式 + 有限关键参数"的简化形式，不分 SAT 三层。

**学术最优（R5 全部）**：
- Chen et al. (2014) ARL-TR-7180：SAT 三层框架原始定义（A 级）
- Stowers et al. (2016)：SAT 在人-智能体协作的实证（A 级）
- Veitch et al. (2024) Ocean Engineering 299:117257：五因子 RCT，"可用时间"效应最大；60s 设计基线得证（A 级）
- USAARL 2026-02：透明度悖论 — 不透明 + 主动发起 = 最优状态；透明 + 被动发起 = 最差状态（A 级）
- NTNU Handover/Takeover (2024)：System-initiated handover 需高 SAT-2；human-initiated 需高 SAT-1+SAT-3（A 级）
- DST Australia (2023)：透明度操作化的关键变量实证（B 级）

**工业其他参考（R1 + R5）**：
- Sea Machines SM300-NG：UI 实现 SAT-1（当前模式 + 威胁列表），SAT-3 部分（接管时窗倒计时），无显式 SAT-2 推理层（A 级）
- NTNU milliAmpere：全景 225° + 空间音频 + 触觉反馈，Layer 0–3 透明度可调（A 级）
- IMO MASS 框架：4-level 模式指示是对透明性的法规约束（B 级）
- Avikus HiNAS 2.0：UI 含 SAT-1/SAT-3，SAT-2 通过单独"决策日志"按需展示（B 级 — 公开资料较少）

**综合判定**：**修订**

**理由**：
1. **方向正确**：SAT 三层映射在学术（Chen 2014/Veitch 2024）+ 工业（NTNU/Sea Machines）都有充分证据。v1.0 选择 SAT 是行业最佳实践之一。
2. **但 v1.0 §2.4 论点违反 R5 实证**（F-P1-D1-010）："SAT-1+2+3 全部可见时工作负荷不增加"被 USAARL 2026-02 透明度悖论直接推翻；NTNU 2024 也发现 SAT-2 全时展示反而拖累绩效。
3. **设计实现（§12）未实现自适应触发**（F-P2-D1-026）：当前 §12 设计仅按角色（ROC vs 船长）分信息密度，未按场景（COLREGs 冲突/M7 警告/操作员请求）触发 SAT-2 推理层。
4. **v3.0 简化模式可作为退化方案**：在 D2 自主度下（船上有海员备援），v3.0 的 4-level 模式指示已经够用；SAT 三层是 D3/D4 远程接管场景的强需求。

**修订方向**：
- §2.4 决策理由：保留 SAT 选择，但论点改为"高质 SAT-3 + 自适应 SAT-2 触发是 D3/D4 接管时窗 60s 的设计前提"
- §12.2：补充自适应触发表（SAT-1 全时；SAT-2 按需；SAT-3 在 TDL 压缩时优先推送）
- §12.3：差异化视图按角色 + 场景双轴

**影响范围**：v1.1 patch（§2.4 + §12.2 + §12.3），不影响其他模块。

**关联 finding**：F-P1-D1-010、F-P2-D1-026

---

## 4. 决策四：Doer-Checker 双轨（vs X-axis Deterministic Checker）

**v1.0 立场（§2.5，行 125–128 + §11）**：M1–M6 = Doer，M7 = Checker；Checker"逻辑须比 Doer 简单 100×"且实现路径独立（不共享代码/库/数据结构）。M7 内置 IEC 61508 + SOTIF 双轨，含 ARBITRATOR "直接向 M5 注入安全轨迹"权限（F-P0-D3-001）。

**v3.0 Kongsberg 立场（CLAUDE.md §2 + Init From Zulip Deterministic Checker）**：**X-axis Deterministic Checker 是系统层独立轴**，跨 L2/L3/L4/L5：
- L2: Route Safety Gate
- L3: COLREGs Compliance
- L4: Corridor Guard
- L5: Actuator Envelope

X-axis 仅有 VETO 权（"reject → fallback to nearest compliant"），**不生成替代轨迹**。Doer 不能绕过 X-axis；X-axis 不持有规划逻辑。

**学术最优（R4 全部）**：
- Boeing 777 PFCS (FAA 1995+)：1 Command + 2 Monitor 通道，PFC 100k+ SLOC vs Monitor <2k SLOC（约 50:1）；Monitor 不生成命令，仅 VETO（A 级）
- Airbus FBW COM/MON：硬件多样化 + 软件多样化，PRIM（复杂）vs SEC（简化），DAL A；Monitor 仅检测 Command 偏离边界（A 级）
- NASA Auto-GCAS：98.5% 防护率；TPA 算法 Monitor 触发预定义"拉起机动"，**不计算完整轨迹**（A 级）
- Jackson et al. (2021) Certified Control：Monitor 几百行 vs 导航栈数万行；Monitor 持有"证书"验证 Doer 输出（A 级）
- Simplex Architecture (1996+)：HPC + HAC + Decider；HAC 是预定义保守控制器，**不动态生成**（A 级）

**工业其他参考（R1 + R4 + R7）**：
- Sea Machines SM300：Linux PC（Doer）+ Siemens S7 PLC（Checker）；PLC 仅做硬限制 VETO，不计算轨迹（A 级）
- DNV-CG-0264 §9.3 监控系统：Checker 做硬件健康检查 + 时序约束，不持有规划（A 级）
- Ocean Infinity Armada 78：DNV-CG-0264 首个远程操控合规声明，确实采用 Doer-Checker 双轨（B 级）

**综合判定**：**修订**（v1.0 思路对，但实现需重构）

**理由**：
1. **Doer-Checker 双轨是工业最佳实践（R4 一致）**——Boeing/Airbus/NASA/Sea Machines 均如此；v1.0 选择正确。
2. **但 v1.0 M7 设计违反工业最佳实践两项核心准则**：
   - **F-P0-D3-001**：M7 ARBITRATOR 直接注入安全轨迹（持有规划逻辑）；Boeing 777/NASA Auto-GCAS 的 Monitor 都不生成轨迹，仅触发预定义动作。
   - **F-P0-D1-008**："100× 简单"在 Jackson 2021/IEC 61508/DO-178C 中均无量化要求；工业实际为 50:1~100:1。
3. **v3.0 X-axis 与 v1.0 M7 是分层关系，不是替代关系**（F-P0-D3-002）：
   - X-axis = 系统层，跨 L2/L3/L4/L5，硬 VETO，不动态计算
   - M7 = L3 内部 Checker，做 SOTIF + IEC 61508 监控（CPA 趋势 / 假设违反），向 M1 报告
   - 两者通过独立总线协调，不共享状态

**修订方向**：
- §2.5：保留 Doer-Checker 双轨，但措辞从"100×简单"改为"工业实践 50:1~100:1，是设计目标非规范强制"（F-P0-D1-008 fix）
- §11：M7 ARBITRATOR 不生成轨迹，仅触发预定义 MRM 命令集（如减速至 4 kn 维持航向、停车抛锚序列）（F-P0-D3-001 fix）
- §11.7（新增）：明确 M7 vs X-axis 分工（F-P0-D3-002 fix）
- §11.4：补充 SFF ≥ 60% 要求（F-P1-D7-005 fix）

**影响范围**：v1.1 patch（§2.5 + §11 全章）。不需要剥离 M7 到系统层（M7 = L3 内部 SOTIF Checker 仍有独立价值）。

**关联 finding**：F-P0-D3-001、F-P0-D3-002、F-P0-D1-008、F-P1-D3-003、F-P1-D6-004、F-P1-D7-005

---

## 5. M1 ODD / Envelope Manager

**v1.0 立场（§5）**：调度枢纽，唯一权威 ODD 状态来源，1 Hz `odd_state` 发布；ODD 子域 IN/EDGE/OUT 状态机；Conformance Score 量化合规性。

**v3.0 立场**：v3.0 没有 M1 等价模块。ODD 类似职责通过：(a) Z-TOP IACS UR E26/E27（系统层网络包络）；(b) 各层 Mode Switcher（每层独立切换状态）；(c) Parameter Database（参数适配，含降级回退）共同实现。

**学术（R3）**：
- Rødseth (2022) JMSE 27(1)：OE = S_ext × S_int + FM；OE 状态机由各功能模块共同响应，不集中到单模块（A 级）
- Fjørtoft & Rødseth (2020) NMDC：将 OE 作为"工程契约"概念，可分布式实现（A 级）
- Lloyd's Register MASS Research v6 (2025.02)：ODD 监控可由独立模块或分布式实现（A 级）

**工业（R1 + R8）**：
- Sea Machines SM300-NG：ODD 静态部分进 Capability Manifest，运行时事件由 plan/avoid/hold 行为切换响应；**无独立 ODD 模块**（A 级）
- Kongsberg K-MATE：ODD 通过场景分类器分布式实现（A 级）
- DNV-CG-0264 §9.8：ODD 边界检测是子功能，不规定模块化方式（A 级）

**综合判定**：**修订**

**理由**：
1. **集中式 M1 设计有 D3/D4 自主等级下的可审计优势**：单一权威源便于 ASDR 日志审查（CCS i-Ship 白盒可审计要求），符合 D2 决策"ODD 作为组织原则"。这点 v1.0 优于 Sea Machines 分布式实现。
2. **但需明确 M1 边界与系统层分工**：M1 仅负责 L3 内部 ODD（FCB 操纵性、传感器降质、COLREGs 几何包络），不替代 IACS UR / Z-TOP 网络包络监控。
3. **F-P0-D1-006 + F-P0-D6-015**：M1 的状态空间（E×T×H 三轴）无权威来源，须改为 Rødseth 二轴 + DNV-CG-0264 AROS 三维 + SAE J3259 属性树的混合表述。
4. **F-P2-D6-021**：M1 频率 0.1–1 Hz 在 EDGE→OUT 突变时可能延迟最长 10 s，应补"事件驱动型 odd_state"机制。

**修订内容**：
- §5.1 模块描述：明确 M1 是"L3 内部 OE 调度枢纽"，与系统层 ODD 监控（IACS UR / Z-TOP）协作
- §5.2/§5.3：状态空间公式按 Rødseth 二轴 + DNV-AROS 三维 + SAE 属性树重写
- §5.4：odd_state 频率列加事件驱动型说明

**关联 finding**：F-P0-D1-006、F-P0-D6-015、F-P2-D6-021、F-P1-D6-016、F-P1-D6-017

---

## 6. M2 World Model

**v1.0 立场（§6）**：唯一权威世界视图（Static View / Dynamic View / Self View），含 COLREG 几何预分类（preliminary_role / bearing 角度区间）；4 Hz 发布。

**v3.0 立场**：v3.0 把世界模型职责分到三处：(a) **Multimodal Fusion** 子系统输出 Track 级目标（独立感知子系统，非 L1）+ Nav Filter EKF 输出统一自船状态；(b) **L3 内 Target Tracker** 做意图估计；(c) **L2 Voyage Planner** 持有 ENC 静态信息。

**学术（R2 + R3）**：
- Frontiers Robotics & AI (2021) COLAV Survey：World Model 是工业级 COLAV 标准模块（A 级）
- MDPI Ships (2022)：World Model + Target Tracker 分离是常见设计（A 级）
- IEEE (2024) CP-Based Verification of COLREG：COLREG 几何预分类作为 World Model 输出是合理设计（B 级）

**工业（R1 + R8）**：
- DNV-CG-0264 工业 4 模块（Condition Detection / Analysis / Action Planning / Control）：M2 对应 "Condition Analysis"（A 级）
- MOOS-IvP `pBasicContactMgr`：相当于 World Model（A 级）
- Sea Machines SM300：State Detection + State Analysis 两阶段（A 级）

**综合判定**：**修订**

**理由**：
1. **架构方向正确**：单一权威世界模型 + 几何预分类，与工业/学术一致（保留主结构）。
2. **但接口标注错误（F-P1-D4-019）**：§6 图6-1 把 RADAR/CAMERA 输入标注为 "L1 感知层"，应为 "Multimodal Fusion"（独立感知子系统）。M2 消费的是 Track 级目标，不是原始传感器数据。
3. **几何预分类逻辑需注释（F-P2-D4-020）**：§6.3.1 `bearing_i in [112.5°, 247.5°]` → OVERTAKING 的判断主语不明（目标 vs 本船视角）。需补注释。
4. **与 v3.0 Target Tracker 分工需明确**：意图估计是否在 M2 内（v1.0 立场）还是独立 Target Tracker（v3.0）？建议 M2 含基础几何分类，意图估计交由 M6 COLREGs Reasoner 协作（与 R2 学术习惯一致）。

**修订内容**：
- §6 图6-1：标签全部改为 "Multimodal Fusion"
- §6.3.1：补充注释明确视角主语
- §6.4 接口契约：明确 M2 输入为 Track 级（Fusion Pipeline 输出）+ Nav Filter EKF 状态

**关联 finding**：F-P1-D4-019、F-P2-D4-020

---

## 7. M3 Mission Manager（vs L1 Mission Layer 命名冲突）

**v1.0 立场（§7）**：L3 内部模块；负责航次计划、ETA 投影、重规划触发；上游来自"L2 战略层"（v1.0 用旧术语，应为 L2 Voyage Planner，F-P1-D1-022）。

**v3.0 立场**：**M3 与 v3.0 L1 Mission Layer 强冲突**——L1 是系统层独立层级，负责 voyage order + weather routing + ETA/fuel optimization。L3 内部不应有"Mission Manager"职责。

**学术（R8）**：
- Benjamin et al. (2010) MOOS-IvP：Backseat Driver 范式中"Mission"由前座（控制层）下发到后座（自主层），后座只接收任务指令，不做航次规划（A 级）
- "Three Patterns for Autonomous Robot Control Architecting" (UPV, 2013)：分层架构中 Mission 是顶层职责（B 级）

**工业（R1 + R8）**：
- Kongsberg K-MATE：scene-analysis 层不持有 Mission；Mission 在 Sounder 上层 + 远程操作员（A 级）
- Sea Machines SM300-NG：Mission 由 SMLink 远程下发，本地仅持有"任务执行状态"（A 级）
- Ocean Infinity Armada：Mission 由 ROC 持有，船端只跟踪任务进度（B 级）

**综合判定**：**修订（重命名 + 缩范围）**

**理由**：
1. **命名冲突明确**：L3 内部叫"Mission Manager"会与 L1 Mission Layer 混淆，团队接口讨论时必然误解。
2. **v1.0 实际职责更窄**：§7 描述的是"L3 内部对 L1 任务令的局部跟踪 + 重规划触发"，不是真正的航次规划。这与 Sea Machines / Kongsberg "本地任务执行状态"职责对应。
3. **替换为重命名 + 缩范围（不是删除 M3）**：
   - 重命名为"M3 Voyage Tracker"或"M3 Local Mission Executor"
   - 明确职责：(a) 接收 L1/L2 下发的任务令 + 路径；(b) L3 内部 ETA 投影；(c) 触发重规划（不做实际重规划，仅请求 L2）
   - 删除"航次计划"措辞（这是 L1 职责）

**修订内容**：
- §7.1 模块名 + 简介改写
- §7 图7-1：上游来源改为 "L2 航路规划层（Voyage Planner）"（F-P1-D1-022 fix）
- §7.2/§7.3：职责清单删除"航次规划"，保留"任务跟踪"

**关联 finding**：F-P1-D1-022

---

## 8. M4 Behavior Arbiter（IvP 选择是否最优）

**v1.0 立场（§8）**：行为字典 + IvP 多目标仲裁；2 Hz 输出 Behavior_PlanMsg（行为类型 + 允许航向/速度区间）；引用 [R3] Benjamin 2010 + [R16] Pirjanian 1999。

**v3.0 立场**：v3.0 L3 5 模块中没有显式 "Behavior Arbiter"——避让决策隐含在 Avoidance Planner 中。v3.0 简化为单模块（plan + arbitrate 合一），不分离行为字典。

**学术（R2 + R8）**：
- Benjamin et al. (2010) MOOS-IvP DTIC ADA559856：IvP Helm 多目标仲裁原始定义；行为字典 + Interval Programming（A 级）
- MIT Marine Autonomy Lab BHV_AvdColregsV22 (2024 维护)：IvP Helm 在 SIL 2 环境下仍是金标准（A 级）
- Frontiers (2021) COLAV Survey：IvP vs MPC vs VO 对比，IvP 在任务级权重管理"仍为金标准"（A 级）
- MDPI Ships (2022)：IvP 与 MPC 互补使用（A 级）

**工业（R1 + R8）**：
- Sea Machines SM300-NG：行为库标准化（plan/avoid/hold/dock）；底层是否用 IvP 未公开，但范式相同（A 级）
- Kongsberg K-MATE：scene-based 行为切换，疑似简化版仲裁（B 级）
- IAI Katana / ATLAS ARCIMS：均含行为字典 + 仲裁层（B 级）

**综合判定**：**保留**

**理由**：
1. **IvP 是工业 + 学术双重金标准（R2 + R8 一致）**：MOOS-IvP 是 Backseat Driver 范式工业基座，Sea Machines / Kongsberg 都受其影响。
2. **行为字典 + 多目标仲裁的设计有充分文献支撑**：Pirjanian (1999) Behavior coordination 综述 + Benjamin 2010 IvP 工程实现。
3. **与 v3.0 简化路线相比，v1.0 拆分模块（M4 行为仲裁 + M5 轨迹规划）反而更利于 D3 决策的可审计**——M4 输出"允许区间"作为 SAT-2 推理日志，对 ROC 操作员透明性更友好。
4. **无 P0/P1 finding 触发**：§8 通读未发现独立缺陷（findings-raw.md §7-§10 注记）。

**保留范围（仅 P3 改进，不动主结构）**：
- §8 通读时未发现需要修订的 P0/P1 项；
- 阶段 5 跨文档对照时，可补充 IvP 求解器的具体软件包（pHelmIvP）+ 配置参数说明，作为 v1.1 patch 的可选扩展。

**关联 finding**：无（§8 无独立 finding）

---

## 9. M5 Tactical Planner（Mid-MPC + BC-MPC）

**v1.0 立场（§10）**：双层 MPC——Mid-MPC（≥ 90 s 时域，COLREGs 合规）+ BC-MPC（短时域，反应式 Branching-Course MPC）；输出 (ψ_cmd, u_cmd, ROT_cmd) 至 L4（v1.0 错写为 L2，F-P1-D5-012）；§10.5 含 FCB 高速修正，引用 [R7] Yasukawa 2015 MMG + [R21] Yasukawa & Sano 2024（疑似幻觉，F-P1-D9-024）。

**v3.0 立场**：v3.0 L3 Avoidance Planner 输出 4-WP（4 个航点 + 速度），不显式分双层 MPC。简化为单层路径生成。

**学术（R2 + R7 引用）**：
- Eriksen et al. (2020) Frontiers Robotics & AI 7:11：BC-MPC，Rule 17 两阶段切换实测验证（A 级）
- Bitar et al. (2019) arXiv:1907.00198：Hybrid Collision Avoidance for ASVs Compliant with COLREGs Rules 8 and 13–17（A 级）
- Frontiers (2021) COLAV Survey：双层 MPC（长 + 短）是 2020+ 主流设计（A 级）
- MDPI Ships (2022)：单层 MPC 在密集场景下时延不达标，双层是主流（A 级）

**工业（R1）**：
- Kongsberg K-MATE：未公开是否双层 MPC，但 4-WP 输出格式接近"折中方案"（B 级）
- Sea Machines SM300：未公开 MPC 架构（B 级）
- IAI Katana / ATLAS ARCIMS：均使用某种形式的预测控制（B 级）

**综合判定**：**保留**（双层 MPC 是学术最优）

**理由**：
1. **双层 MPC 是 R2 学术综述明确的 2020+ 主流（高于 v3.0 简化的 4-WP 输出）**：FCB 高速场景下，单层 MPC 时延不达标，必须双层。v1.0 选择正确。
2. **F-P1-D5-012**：M5 输出层级标错（L2 应为 L4），属术语笔误，不影响架构方向。
3. **F-P1-D9-024**：[R21] Yasukawa & Sano 2024 疑似幻觉；FCB 波浪扰动修正应改用 [R7] Yasukawa & Yoshimura 2015 的 4-DOF MMG 章节（替代来源充分）。
4. **§10.5 FCB 高速修正"硬编码船型常量"风险**：§10.5 引入 FCB 专属修正项（Hs > 1.5 m 阈值），但是否为决策核心硬编码？联动 §13 多船型 Capability Manifest——若硬编码则违反 D4 决策（决策核心零船型常量）。但 v1.0 §10.5 的措辞是"参数化注入"，与 §13 一致，未违反（已确认）。

**保留范围 + 必要 patch**：
- §10 图 10-1：M5→L2 改为 M5→L4（同 F-P1-D5-012）
- §10.5 + §10.6：[R21] 替换为 [R7] 的 4-DOF MMG 章节，或标注 [待验证]
- 主架构（双层 MPC）保留

**关联 finding**：F-P1-D5-012、F-P1-D9-024

---

## 10. M6 COLREGs Reasoner

**v1.0 立场（§9）**：独立模块，与 M4 协作，2 Hz 输出 COLREGs_ConstraintMsg（规则约束集 + 时机阶段）；引用 [R17] Wang 2021 + [R18] COLREGs 1972/2002 + [R19] Bitar 2019 + [R20] Eriksen 2020。

**v3.0 立场**：v3.0 L3 5 模块中 **COLREGs Engine 是独立模块**——与 v1.0 一致。

**学术（R2 + R7）**：
- Frontiers (2021) COLAV Survey：独立 COLREGs 模块 + 与 Avoidance Planner 协作是工程主流（A 级）
- IEEE (2024) Constraint Programming-Based Verification of COLREG Safety：独立 COLREGs 推理便于形式化验证（B 级）
- Bitar et al. (2019) + Eriksen et al. (2020)：COLREGs 8/13–17 规则编码 + Rule 17 切换实测（A 级）
- DNV-CG-0264 §4.7 Safe speed + §4.8 Manoeuvring：独立 COLREGs 推理便于 9 子功能映射（A 级）

**工业（R1）**：
- Kongsberg K-MATE：内置 COLREGs 推理引擎（独立）（A 级）
- Sea Machines SM300-NG：COLREGs 模块独立，与 plan/avoid 行为协作（A 级）
- ATLAS ARCIMS：MCA 认证 Sense & Avoid 含独立 COLREGs 模块（B 级）

**综合判定**：**保留**

**理由**：
1. **v3.0 与 v1.0 在此对象上完全一致**：独立 COLREGs Engine 是工业主流。
2. **学术（R2）+ 工业（R1）+ 认证（R7 DNV §4.7/§4.8）三方一致**：独立 COLREGs 推理便于形式化验证 + 认证映射。
3. **R2 阈值专项**：v1.0 §9.3 ODD-aware 参数（T_standOn 8/6/10 min；T_act 4/3/5 min）"无法找到与 v1.0 完全一致的数值"，但学术范围（5–20 min）覆盖 v1.0 数值；v1.0 阈值很可能来自 Wang 2021 特定船型设定，须 HAZID 校准（已是 F-P1-D6-017 的 fix）。
4. **§9 通读未发现独立 P0/P1 finding**（findings-raw.md 已注记）。

**保留范围**：
- §9 主结构保留
- §9.3 参数注明"FCB 特定设计值，HAZID 待校准"（部分由 F-P1-D6-017 涵盖）

**关联 finding**：F-P1-D6-017（间接，是 §3 ODD 阈值，但 §9.3 同源）

---

## 11. M7 Safety Supervisor（vs X-axis Checker）

**v1.0 立场（§11）**：L3 内部 Checker，IEC 61508 + SOTIF 双轨；ARBITRATOR 直接注入安全轨迹到 M5（F-P0-D3-001）；SIL 2 单通道 + DC ≥ 90%（缺 SFF，F-P1-D7-005）。

**v3.0 立场**：**X-axis Deterministic Checker = 系统层独立轴**，跨 L2/L3/L4/L5。L3 内的 X-axis 子项叫 "COLREGs Compliance"，仅 VETO + 回退 nearest compliant，不注入轨迹。v1.0 M7 与 v3.0 X-axis 不在同一层级。

**学术（R4）**：
- Boeing 777 PFCS：Monitor 是模块级（不跨层），但与 PFC 完全独立（A 级）
- Airbus FBW COM/MON：模块级 Monitor，物理 + 软件双重独立（A 级）
- NASA Auto-GCAS：模块级，触发预定义机动（A 级）
- Jackson 2021 Certified Control：Monitor 持"证书"验证，模块级（A 级）

**结论：学术界 Monitor 多为模块级（不跨层），v3.0 系统层 X-axis 是航空"完整性架构"在海事的扩展。**

**工业（R1 + R7）**：
- Sea Machines SM300：PLC（模块级 Checker）+ 单独的 PLC ODD 边界监控（A 级）
- DNV-CG-0264 §9.3：监控系统是模块级（A 级）
- Ocean Infinity Armada 78：合规声明含 Doer-Checker，未明确层级（B 级）

**综合判定**：**修订**

**理由**：
1. **v1.0 模块级 M7 + v3.0 系统级 X-axis 是分层互补，不是替代关系**——这是关键正确认识：
   - X-axis = 系统层硬 VETO，跨层（防止 L2/L3/L4/L5 任一层产生越界决策）
   - M7 = L3 内部 SIL 2 + SOTIF 监控，做"假设违反检测 + CPA 趋势分析"，向 M1 报告
2. **F-P0-D3-001：M7 ARBITRATOR 注入轨迹违反 Monitor 简化原则**——必须改为触发预定义 MRM 命令集
3. **F-P0-D3-002：M7 vs X-axis 协调机制缺失**——必须补 §11.7
4. **F-P1-D3-003：M7 SOTIF 轨道 CPA 计算可能与 M2 共享逻辑**——必须明确独立计算路径或保守阈值降级
5. **F-P1-D7-005：SIL 2 SFF 要求缺失**——必须补 SFF ≥ 60%

**修订方向**：
- §11.2：M7 ARBITRATOR 不注入轨迹，仅触发 MRM 命令集索引
- §11.3 PERF 行：明确 CPA 计算独立性（独立感知通路 OR 保守阈值降级）
- §11.4：补 SFF ≥ 60% Type A 或 HFT=1 双通道
- §11.6（新增）：M7 自身降级行为（Fail-Safe，不允许 Fail-Silent）
- §11.7（新增）：M7 vs X-axis Checker 分工（M7 = L3 内 SIL 2 + SOTIF；X-axis = 系统层 VETO；通过独立总线协调）

**影响范围**：v1.1 patch（§11 全章重构）

**关联 finding**：F-P0-D3-001、F-P0-D3-002、F-P1-D3-003、F-P1-D6-004、F-P1-D7-005

---

## 12. M8 HMI / Transparency Bridge

**v1.0 立场（§12）**：唯一对 ROC/船长说话的实体；SAT-1/2/3 三层透明性；ToR（Transfer of Responsibility）协议含 60 s 时窗（[R4] Veitch 2024 正确引用）；差异化视图（ROC vs 船长）按角色分信息密度。

**v3.0 立场**：Shore Link via DMZ + IMO MASS 4-level 模式指示。透明性是简化的"模式 + 关键参数"，不分 SAT 三层。

**学术（R5 + R6）**：
- Chen et al. (2014) ARL-TR-7180：SAT 三层框架原始定义（A 级）
- Veitch et al. (2024) Ocean Engineering 299:117257：60 s 设计基线得证（A 级）
- USAARL 2026-02：透明度悖论 — SAT-2 全时展示反而增负荷（A 级）
- NTNU Handover/Takeover (2024)：System-initiated handover 需高 SAT-2；human-initiated 需高 SAT-1+SAT-3（A 级）
- Time Budget for Merchant Ship Control Takeover (预印本)：船长经验 3–6 min，60 s 是激进基线（B 级）

**工业（R1 + R5）**：
- Sea Machines SM300-NG UI：SAT-1（模式 + 威胁）+ SAT-3（接管时窗）+ SAT-2（按需展示）（A 级）
- NTNU milliAmpere：全景 225° + 空间音频 + 触觉，Layer 0–3 透明度可调（A 级）
- IMO MASS 4-level：法规层定义透明性下限（B 级）

**综合判定**：**修订**

**理由**：
1. **SAT 三层选型正确**（决策三已论证）：保留主结构。
2. **F-P1-D1-010 + F-P2-D1-026**：v1.0 §12.2 设计的"全层全时展示"违反 USAARL 2026-02 透明度悖论；§12.3 仅按角色分密度，未实现按场景的自适应触发。
3. **F-P2-D1-025**：§12.5 引用 [R23] 作者错（Hareide → Veitch & Alsos）。
4. **ToR 60 s 时窗设计正确**：[R4] 引用准确，Veitch 2024 RCT 实证支持。但 §12.4 "验证操作员上下文充分性（至少已阅 SAT-1）"的具体实现方式（presence detection / time-based）需补设计细节（属 P3 改进）。

**修订方向**：
- §12.2 SAT 触发表：SAT-1 全时；SAT-2 按需（COLREGs 冲突 / M7 警告 / 操作员请求）；SAT-3 在 TDL 压缩时优先推送
- §12.3：差异化视图按角色 + 场景双轴
- §12.5：[R23] 作者改为 Veitch & Alsos
- §12.4 ToR 协议：保留 60 s 设计基线，可作为 D2/D3 适配的边界条件

**关联 finding**：F-P1-D1-010、F-P2-D1-025、F-P2-D1-026

---

## 13. 多船型 Capability Manifest + Backseat Driver

**v1.0 立场（§13）**：三层解耦——A 层（vessel-agnostic 决策核心）+ B 层（Capability Manifest YAML）+ C 层（vessel-specific 适配 PVA + 水动力插件）；引用 [R3] Benjamin 2010 + [R24] Sea Machines SM300。

**v3.0 立场（CLAUDE.md §2）**：v3.0 用 Parameter Database（manoeuvring coeffs / stopping / draught / wind/current / thruster config / degraded fallback）+ YAML 配置 + Backseat Driver 范式。**v3.0 与 v1.0 §13 设计高度一致**。

**学术（R8）**：
- Benjamin et al. (2010) MOOS-IvP DTIC ADA559856：Backseat Driver 原始架构（A 级）
- Newman/Benjamin et al. IvP Helm User Guide 4.2.1：行为字典 + 前座/后座解耦完整规范（A 级）
- Three Patterns for Autonomous Robot Control Architecting (UPV, 2013)：前座/后座解耦形式化设计（B 级）
- Toward Maritime Robotic Simulation in Gazebo：水动力插件接口设计（B 级）

**工业（R8）**：
- Sea Machines SM300-NG：多船型 ABS/DNV 入级；行为库标准化部署到测量/巡逻/跟踪（A 级）
- Kongsberg K-MATE + Sounder USV：多船型可部署的工业实证（A 级）
- awvessel.github.io Manifest Spec：开源舰艇 Capability Manifest（B 级）
- MIT Marine Autonomy Lab MOOS-IvP：长期维护至 2025（A 级）

**综合判定**：**保留**（注明原创设计）

**理由**：
1. **三层解耦架构是 R8 工业 + 学术一致认可的范式**：Sea Machines / Kongsberg / MOOS-IvP 都是这条路线。v1.0 选择正确。
2. **Capability Manifest 海事专属规范缺失（R8 推翻信号 1）**：仅 awvessel.github.io 1 个开源样本，Sea Machines / Kongsberg 商业机密。**v1.0 §13 是原创设计**，须在文档中注明"自定义规范，参考 MOOS-IvP + awvessel 格式"，不能声称引用现成行业标准。
3. **F-P2-D4-027**：§13.5 表"长江内河船：仅 M6 规则库"修改与 §9 当前设计不一致（§9 无规则库插件接口）。需补 §9 设计或修正 §13.5 措辞。
4. **§13.4 水动力插件接口（C++ ABI 稳定）设计合理**：与 R8 Gazebo Hydrodynamics.hh 实现思路一致。
5. **零船型常量审计缺失（R8 推翻信号 4）**：v1.0 主张"决策核心零船型常量"，但无独立审计验证。须在 v1.1 加 ASDR 自动校验机制（运行时扫描决策核心是否有船型 if 分支）。

**保留范围 + 必要 patch**：
- §13.1：补注"Capability Manifest 为本设计自定义规范，参考 MOOS-IvP + awvessel.github.io 格式"
- §13.5：解决 §9 规则库插件 vs §13.5 promise 矛盾（F-P2-D4-027）
- 补 ASDR 校验机制：运行时扫描"零船型常量"

**关联 finding**：F-P2-D4-027

---

## D9 综合统计

> 行综合分（占位"待 §6 填"）将在阶段 6（T6.1 14×9 评分矩阵）后回填。
> 综合判定来自上述 13 节"综合判定"段。

| 对象 | 综合判定 | 行综合分（来自 §9） |
|---|---|---|
| 决策一 ODD 作为组织原则 | 修订 | TBD |
| 决策二 8 模块 vs Kongsberg 5 模块 | 修订（重构） | TBD |
| 决策三 CMM-SAT 映射 | 修订 | TBD |
| 决策四 Doer-Checker 双轨 | 修订 | TBD |
| M1 ODD/Envelope Manager | 修订 | TBD |
| M2 World Model | 修订 | TBD |
| M3 Mission Manager | 修订（重命名+缩范围） | TBD |
| M4 Behavior Arbiter | **保留** | TBD |
| M5 Tactical Planner | **保留** | TBD |
| M6 COLREGs Reasoner | **保留** | TBD |
| M7 Safety Supervisor | 修订（与 X-axis 分层） | TBD |
| M8 HMI/Transparency Bridge | 修订 | TBD |
| 多船型 Capability Manifest | **保留**（注明原创） | TBD |

---

## 阶段 4 总论

**判定分布（13 对象）**：
- **保留**：4 个（M4、M5、M6、多船型 Capability Manifest）
- **修订**：9 个（4 决策 + M1/M2/M3/M7/M8）
- **替换**：0 个

**关键观察**：

1. **架构方向正确，无需 v2.0 替换**：13 对象中无一个被判"替换"。这意味着 v1.0 的核心架构（ODD 组织原则 + 8 模块 + Doer-Checker + SAT 透明性 + 多船型 Backseat Driver）在工业 + 学术对照下方向正确。

2. **修订集中在系统-模块层级关系**：4 个决策 + 4 个模块（M1/M3/M7/M8）的修订都围绕"L3 内部模块如何与系统层（IACS UR / X-axis Checker / L1 Mission Layer / Multimodal Fusion）协调"展开。这是 v3.0 Kongsberg 立场带来的最大修订压力——但不是替换，是定位修订。

3. **算法选型完全保留**：M4（IvP）、M5（双层 MPC + BC-MPC）、M6（独立 COLREGs）三个算法核心模块全部保留，符合 R2 学术综述结论 + R1 工业实践。这是 v1.0 最强的部分。

4. **整体架构方向不需重大调整**：阶段 6 决策树预测走 **A 路径（v1.1 patch）**——补 4 个决策 + 4 个模块的修订项，不需要 v2.0 替代架构（B 路径）也不需要重新设计（C 路径）。F-P0 = 5（D3-001/D3-002/D1-006/D1-008/D6-015）数量在 P0 爆炸阈值（≥ 6）以下。

5. **后续阶段重点**：
   - 阶段 5 跨文档对照：检查 v1.1 patch 与 v3.0 Kongsberg 接口契约（X-axis VETO 协议、Multimodal Fusion Track 接口、L1/L2 上下游消息格式）的一致性
   - 阶段 6 评分矩阵：填 13 对象 × D1–D9 评分 → 行/列/全表综合分 → 写 patch list（A 路径）+ 99-followups.md
