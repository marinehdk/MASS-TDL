# G 评审报告 — SIL 系统构建（贯穿 L3 全模块）

| 字段 | 值 |
|---|---|
| Reviewer Persona | SIL 平台架构师（船舶物理仿真 / 场景工程 / RL 对抗测试 / DNV OSP & Kongsberg HIL 实践） |
| Scope | 8 月计划 D1.3 / D2.5 / D3.6 / D3.7；M1–M8 §9 测试策略；架构 v1.1.2 §15.2 接口矩阵；SIL 6 子模块（FCB 4-DOF MMG / AIS / ENC / 场景管理 / HMI / RL 对抗） |
| 总体判断 | 🔴（结构合理，但 5 P0 阻塞；其中"5 人周完成 6 子模块"+"无场景库版本/覆盖率工程定义"+"无仿真器自我 V&V"为最严重） |
| 评审基线 | 架构 v1.1.2（2026-05-06）+ 8 模块详细设计草稿 + 8 月开发计划 v2.0 |

---

## 1. Executive Summary（≤ 300 字）

SIL 框架在 v2.0 计划中被正确定位为 Phase 1 最高优先级（D1.3 ⭐），且 6 子模块（FCB 4-DOF MMG / AIS 管道 / ENC 查询 / 场景管理 / 调试 HMI / RL 对抗生成）的拆分与 DNV OSP / Kongsberg K-Sim 的工业实践方向一致。然而，作为"贯穿全 L3 的测试基础设施"，当前设计存在 **5 个 P0 级别阻塞项**：(1) **5 人周交付 6 子模块严重失实**——DNV OSP 项目 2018 年开始仍在迭代，单 FCB 4-DOF MMG 仿真器加积分稳定性 + 参数辨识就需 ≥ 3 人周；(2) **场景库 1000+ 仅在 D3.6 出现，无版本化/schema/CI/diff/回归工程定义**——这是 CCS 提交证据的根，缺失即认证失效；(3) **无 SIL 仿真器自身 V&V 计划**——D1.3 仅要求"停止距离误差 ≤ 10%"，但参考解、确定性回放、MMG 参数变更回归测试均缺失；(4) **覆盖率仅"≥ 98% 通过率"，无 Rule × ODD × 扰动组合立方体覆盖率定义**，违反 ISO 21448 SOTIF 触发条件覆盖要求；(5) **SIL→HIL→实船一致性传递无契约**——同一场景文件在三平台运行须确定性等价（modulo 物理保真度），当前计划未定义场景文件 schema、DDS/时钟/数据格式兼容性。**G2 / G6 涉及 M7 PATH-S 独立可观测性、FMI 标准缺位、RL 对抗工具 6/8 完成时点过紧。立即必修 5 P0；建议引入 DNV OSP / FMI 2.0 标准化 + 提案新 DOMAIN 笔记本"SIL_HIL_Platform_Engineering"。**

---

## 2. P0 阻断项（5 条，必须 5 月开工前解决）

### P0-G-1：D1.3 「5 人周 / 6 子模块 / 至 6/8 」工时严重低估
- **finding**：D1.3 SIL 框架 v1.0 计划用 **5 人周（2 人 ≈ 2.5 日历周）** 完成 6 个子模块（FCB 4-DOF MMG 物理仿真器 / AIS 历史数据管道 / YAML 声明式场景管理 / COLREGs 覆盖率自动报告 / SIL 调试 HMI / RL 对抗性场景生成）
- **影响**：FCB 4-DOF MMG 仅"标准 Yasukawa 2015 方法 + Euler 积分 + 1h 不崩溃 + 停船误差 ≤ 10%"就 ≥ 2 人周；AIS NMEA 0183 + 历史数据回放 + 95% 解析率 ≥ 1 人周；RL 对抗智能体（即使最简单 PPO）从框架到产出 ≥ 5 个边界场景 ≥ 2 人周；调试 HMI（实时 SAT-1/2/3）≥ 1.5 人周；YAML 场景管理 + 覆盖率报告 ≥ 1.5 人周。**实际 ≥ 8 人周**，5 人周缺口 60%。后续 Phase 2/3 全部依赖 SIL，D1.3 滑期 = 整盘滑期
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:124-141`（D1.3 5 人周 / 6 子模块 DoD）
  - DNV OSP 项目自 2018 年启动，至 2024 OSP Conference 仍在持续迭代基础设施（MLFMU、sim-explorer、proxy-fmu）。 https://opensimulationplatform.com/2024/09/23/osp-conference-2024-program/  🟢
  - Kongsberg K-Sim 用于 Yara Birkeland 是多年专业团队的产物（"key enabling technologies … sensors and integration … remote and autonomous"）https://www.kongsberg.com/maritime/news-and-events/our-stories/autonomy-is-here--powered-by-kongsberg/ 🟢
- **整改建议**：
  1. **拆分 D1.3** 为 D1.3a（5/24 截止：4-DOF MMG + AIS 管道，含 1 个示例场景跑通）= 4 人周；D1.3b（6/15 截止：场景管理 + 调试 HMI + 覆盖率报告）= 3 人周；D1.3c（**移到 Phase 2 W7-W8**：RL 对抗）= 2.5 人周。**总 ≥ 9.5 人周**
  2. RL 对抗模块从 Phase 1 DoD 中 **移除**（"产生 ≥ 5 个边界场景"是无意义指标，应在 M6 完成后再启动）
  3. 在 Phase 1 缓冲（8.5 人周）中预留 ≥ 3 人周给 D1.3 超支
- **置信度**：🟢
- **Owner**：技术负责人 + PM

### P0-G-2：场景库 1000+ 无工程化（schema / 版本 / CI / diff / 回归 / 可追溯）
- **finding**：D1.3 提及"YAML 声明式场景管理"，D3.6 要求"≥ 1000 场景 ≥ 98% 通过率"，但全计划与全模块详细设计（M1–M8）**均未定义**：(a) 场景文件 schema（JSON Schema / Pydantic / 强类型）；(b) 场景版本化策略（Git 还是单独 manifest？diff 何意？）；(c) 场景 CI（PR 触发的回归集 vs nightly 全量集 vs 周度 RL 生成集）；(d) 场景—需求—Rule—HAZID 双向追溯矩阵（CCS Type Approval 必填）；(e) 场景缺陷 → bug → 修复 → 回归断言 闭环
- **影响**：CCS 审计要求"测试场景必须可追溯到具体安全需求 / 规则条款 / HAZID 风险条目"。当前"1000 场景 + HTML 报告"仅是数量证据，**无质量证据**。提交后审计会以"场景库无版本控制 / 无变更管控 / 无追溯矩阵"驳回，等价于 SIL 1000+ 失效
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:140`（"10 个基础场景"无 schema / 无追溯）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:455-471`（D3.6 仅 "AIS 历史 + YAML 合成 + RL 生成"三类来源混合，无版本化）
  - DNV CG-0264 (2025.01) [R9] 要求"objective evidence … wide variety of scenarios"，含追溯
  - safety_verification NLM 笔记本：HIL/SIL 用于"extended failure mode and sequence testing"，但**不含**当前项目场景库工程范式 [Citation 1] 🟢
- **整改建议**：
  1. **5/24 前**输出 `docs/Design/SIL/scenario-schema.md`（JSON Schema / 强类型）+ `scenario-traceability-matrix.csv`（场景 ↔ Rule 13–17/19 ↔ HAZID 条目 ↔ ODD 子域 ↔ 预期 PASS 行为）
  2. **场景库独立 Git repo**（或 monorepo 子目录），与代码同等审查；每个场景必填 `requirements_traced[]`、`hazid_id[]`、`rule_branch_covered[]`
  3. CI 三层集：PR-trigger Smoke (10 场景) / Nightly Regression (200 场景) / Weekly Full (1000+)
  4. 场景缺陷流：发现失败 → 提 issue → fix → **新增 1 个回归断言场景**（不是修改原场景）
- **置信度**：🟢
- **Owner**：技术负责人 + 架构师

### P0-G-3：SIL 仿真器自身 V&V 缺失（仿真器是 SUT 的依赖项，必须先证可信）
- **finding**：D1.3 验收标准只有"FCB 仿真器连续运行 1 小时无崩溃；停止距离误差 ≤ 10%"。**未定义**：(a) 参考解（哪份 sea-trial 数据 / 模型试验数据 / 商业仿真器对标？）；(b) 确定性回放（同 seed + 同输入 = bit-identical 输出）；(c) MMG 参数变更后的仿真器回归测试集；(d) 数值积分稳定性边界（低速 / 大转向 / 高海况发散判定）；(e) FCB 4-DOF 在 HAZID 校准前用"初始 YAML 参数"，校准后参数偏差 >30% 时仿真器是否仍稳定
- **影响**：仿真器是 SUT（M1–M8）的依赖工具——如果仿真器本身有 bug，SIL 1000+ 通过率毫无意义。CCS / DNV 在 AROS 认证（DNV-CG-0264）中将 simulation tools 视为 verification tools，**必须证明 tool qualification**（类同 ISO 26262 ASIL-C tool confidence level TCL）
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:136`（仅"≤ 10% 停船误差"一条 DoD）
  - 架构 v1.1.2 §15.2（无仿真器 → 模块的"参考真值"接口定义）
  - safety_verification NLM 笔记本：DNV 要求 simulator 用于"objective evidence" [Citation 2]，未提到当前项目方案
  - DNV OSP MLFMU / sim-explorer 工程模式表明工业界对 tool versioning + reproducibility 高度强调（OSP Conference 2024）
- **整改建议**：
  1. **D1.3 DoD 增加**：(a) 至少 3 个解析参考解（直线减速 / 标准旋回 / Z 字操舵）误差 ≤ 5%；(b) 同 seed 100 次重跑输出 bit-identical（或确定性误差 < 1e-9）；(c) MMG 参数 ±20% 范围内仿真器不发散
  2. 新增 D1.3.1 「Simulator Qualification Report」**1 人周**，2026-06-15 完成，含 tool confidence 论证
  3. v1.1.3 HAZID 回填若改动 MMG 参数 >10%，**强制重跑全 1000 场景**（不是 incremental）
- **置信度**：🟢
- **Owner**：技术负责人 + M5 负责人（MMG 物理责任）

### P0-G-4：覆盖率指标只有"通过率 98%"，缺组合立方体（Rule × ODD × 扰动）
- **finding**：D2.4 / D3.6 验收标准均使用"≥ X% 通过率"，未定义：(a) Rule 5/6/7/8/9/13–17/19 × 子分支（如 Rule 15 give-way / stand-on × bearing 边界 × aspect ±5°）的 **branch coverage**；(b) ODD-A/B/C/D × Rule 的笛卡尔积覆盖；(c) 扰动维度（风/浪/流/AIS dropout/Radar clutter/目标意图不确定）的 OAT 或 combinatorial test design；(d) ISO 21448 SOTIF triggering condition 覆盖。"98% 通过率"只能说明"做对的事大多数时候做对"，不能说明"覆盖了哪些情况"
- **影响**：CCS i-Ship Nx + DNV AROS 都要求 V&V 完整性证明而非通过率。98% 在 5 个 head-on + 995 个 nominal cruise 场景下也成立，但完全不证明 COLREGs 合规。SOTIF 验证空白 = ISO 21448 不达标
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:271`（D2.4：500+ 场景 ≥ 98% 通过 + "Rule 13/14/15/16/17/9/19 全分支覆盖"无量化定义）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:469-470`（D3.6：≥ 1000 场景 ≥ 98%，"无空白规则分支"无判定方法）
  - M6 详细设计 §9.1（`docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md:983-1004`，分层规则单元测试到位但无 ODD × Rule × 扰动正交矩阵）
  - ISO 21448:2022 [R6] triggering condition coverage 要求 🟢
- **整改建议**：
  1. **5/31 前**定义 `docs/Design/SIL/coverage-metrics.md`：(a) MC/DC（modified condition decision coverage）on Rule 决策树；(b) 11 Rule 分支 × 4 ODD × N 扰动级别 → 报告 hit count + miss list；(c) SOTIF triggering condition 清单（≥ 50 条）覆盖率；(d) 接受 RL 对抗找到的"漏洞场景"自动入库为新分支
  2. D3.6 DoD 改为：(a) 100% Rule 分支 hit；(b) ≥ 95% (Rule × ODD × 高扰动) cell hit；(c) ≥ 80% SOTIF triggering condition 覆盖；(d) **失败场景必须暴露**（report.failures.csv 不为空时审计员可见，不掩饰）
- **置信度**：🟢
- **Owner**：M6 负责人 + 安全工程师

### P0-G-5：SIL → HIL → 实船 一致性传递契约缺失
- **finding**：D4.1（HIL）和 D4.5（实船）描述"基于 SIL 1000+ 场景库精选"，但**未定义**：(a) 同一场景 YAML 文件能否在三平台一字不改地运行？(b) 时间戳 / 时钟同步 / DDS QoS / 消息序列化在三平台的一致性约束；(c) 仿真物理与 HIL 实物 / 实船的延迟差（仿真零延迟 vs HIL 100ms vs 实船 200ms+）的处理协议；(d) 通过 SIL → HIL 时回归阈值（例如 CPA 偏差 ≤ 5%）；(e) 场景在三平台 confidence 等级（SIL = unit；HIL = integration；实船 = acceptance）
- **影响**：违反 V-model + DNV CG-0264 § "objective evidence" 链。Phase 4 HIL 启动时会发现"场景跑不起来 / 行为不同 / Bug 重现失败"，时间已经无法挽回（HIL 硬件 10 月到货 + AIP 11 月提交）
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:534-547`（D4.1/D4.2 仅"基于 SIL 场景库精选"，无契约）
  - 架构 v1.1.2 §15.2（接口矩阵无 SIL/HIL 适配层定义）
  - DNV OSP / FMI 2.0 标准提供了行业级解（FMU 跨平台一致性，OSP-IS 的 a priori validation）—— 当前项目**未采用** FMI/FMU/SSP，等于在重新发明轮子但没轮子。https://open-simulation-platform.github.io/ 🟢
- **整改建议**：
  1. **5/24 前**输出 `docs/Design/SIL/sil-hil-trial-equivalence.md`：定义"deterministic equivalence modulo physical fidelity"判据
  2. **强烈建议引入 FMI 2.0 + OSP-IS**——FCB MMG 打包为 FMU；其余模块 ROS2 节点保持，通过 ROS2 ↔ FMU bridge（已有开源方案）
  3. 场景文件 schema **预留**`platform_overrides:` 字段（SIL 默认零延迟 / HIL 注入实测延迟 / 实船仅作为参考）
  4. Phase 3 W18（8/24–8/31）增加 1 人周「场景跨平台冒烟测试」（取 10 个场景在 mock-HIL Docker 上跑通）
- **置信度**：🟢
- **Owner**：技术负责人 + 架构师

---

## 3. P1 重要风险（8 条）

### P1-G-1：M7 PATH-S 独立路径在 SIL 中如何被观测？
- **finding**：M7 必须 Doer-Checker 独立（禁用 M1–M6 头文件、独立日志、独立进程，详见 `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md:45`）。SIL 调试 HMI 需展示 SAT-1/2/3 含 M7 内部状态（IEC61508 fault count / SOTIF 假设违反 / Checker veto histogram），但若 SIL 在 M7 中插入观测 hook，可能违反"实现路径独立"约束（CLAUDE.md §4 决策二）
- **影响**：要么 SIL 观测不足（认证审计无法看到 M7 真实状态），要么 SIL 注入打破独立性
- **证据**：M7 §10.4 第三方库白名单 / `gantt:392-413`（D3.3 PATH-S 约束）
- **整改建议**：M7 通过**只读 ROS2 topic**`m7/sil_observability`（不引入新依赖、不打破独立性）发布内部状态；SIL 订阅该 topic 即可。在架构 v1.1.3 §15.2 接口矩阵补一行
- **置信度**：🟢
- **Owner**：M7 负责人 + 架构师

### P1-G-2：RL 对抗场景生成被错误地放在 Phase 1
- **finding**：D1.3 要求"RL 对抗生成模块可产生 ≥ 5 个令当前 stub 出错的边界场景"。Phase 1 时只有 mock publishers + stub，让 RL 找 stub 的 bug **完全没有信号意义**（找出来的"边界"是 stub 的，不是真实算法的）
- **影响**：浪费 1.5–2 人周；产生误导性的 baseline
- **整改建议**：RL 对抗整体后移到 **Phase 3 W14（M6 + M5 都有真实代码后）**；Phase 1 仅保留"RL 对抗框架占位 + 接口契约"
- **证据**：`gantt:140`
- **置信度**：🟢

### P1-G-3：4-DOF MMG 对 FCB 高速段保真度未论证
- **finding**：D1.3 选择 "Yasukawa 2015 标准 4-DOF MMG"，但 FCB（高速 crew boat，18+ kn）波浪修正、空泡、planing 转半-planing 区段不在 Yasukawa 2015 的标准验证范围内。架构 v1.1.2 §10.5 也明确"原 [R21] Yasukawa & Sano 2024 因来源未在 JMSE/JMSTech 数据库确证（疑似引用幻觉）→ 已删除。FCB 高速波浪修正改用 [R7]"——这意味着 FCB 高速保真度引用是**降级使用 [R7]**
- **影响**：D3.6 SIL 1000 场景在 FCB 18 kn 段的保真度不足；HIL/实船时 BC-MPC 高速 ROT_max 表 [TBD-HAZID] 校准结果可能与 SIL 偏差 >30%
- **证据**：架构 v1.1.2 §16 引用清理段（行 1686-1688）；M5 §9.1 ROT_max 修正表用例
- **整改建议**：(1) D1.3 物理仿真器明确"低速段（≤ 10 kn）保真目标 ±5%；高速段（> 10 kn）±15%"；(2) HAZID RUN-001 议程加入"FCB 高速保真度 sea-trial 数据需求"；(3) 启动 ship_maneuvering DOMAIN 的 deep research 补 FCB-class crew boat MMG validation 文献
- **置信度**：🟡（需 ship_maneuvering DOMAIN deep research 补强）

### P1-G-4：环境 / 不确定性建模散落，无统一随机化与可重现框架
- **finding**：风/浪/流来自 Multimodal Fusion 的 EnvironmentState；AIS dropout 在 M2 §9.2 测试；目标意图不确定性在 M5 BC-MPC 内嵌；M7 SOTIF 检测 6 类违反——分散在多个模块，**未统一**为 SIL 框架的 "stochastic environment layer"。Seed 管理、reproducibility、ablation study 接口缺失
- **影响**：场景"通过"后无法 ablation；HAZID 校准时无法系统性扫参数
- **整改建议**：D1.3 的 YAML 场景管理增加 `disturbance_model:` 节，统一控制风浪流 + 传感器噪声 + 目标意图分布；引入 PRNG seed 字段；Monte-Carlo 工作流（每个场景 100 seed）成为 D3.6 必填
- **证据**：M2 §9.1 / M5 §9.3 / M7 §9.3 分散，无中心化定义
- **置信度**：🟢

### P1-G-5：SIL 调试 HMI 与 M8 HMI 的关系/复用 未定义
- **finding**：D1.3 的"SIL 调试 HMI"显示 SAT-1/2/3；D3.4 的 M8 HMI/Transparency Bridge 也聚合 SAT-1/2/3。两者关系（复用 / 独立 / 共享 React 组件库）**未定义**
- **影响**：要么重复 1.5–2 人周；要么 SIL HMI 与 M8 HMI 渲染不一致 → 调试看到的 ≠ 操作员看到的
- **整改建议**：在 D1.3 DoD 标注"SIL HMI 与 M8 HMI 共用一套前端组件库（M8 是 superset，含 ToR）；D1.3 仅出 dev-grade UI；D3.4 M8 出 ops-grade UI"
- **证据**：`gantt:124, 416-432`
- **置信度**：🟢

### P1-G-6：SIL 单次场景 ≤ 60s 与 1000 场景 CI 时间冲突
- **finding**：D1.3 要求"SIL 单次场景运行时间 ≤ 60s"。1000 场景串行 ≥ 16h；并行需要 ≥ 16 vCPU 仍要 1h+。CI 设计是否支持？8 模块 SIL 全系统集成（D3.7 SIL 8h）会被 1000 场景 CI 挤占
- **影响**：CI 跑不动 = 实际不跑 = 通过率假成绩
- **整改建议**：D1.2 CI 拓扑明确：PR-trigger Smoke 10 场景（< 5 分钟）/ Nightly 200 场景（< 1h，需 GitLab runner pool ≥ 8 并发）/ Weekly Full 1000+（自托管 GPU box）。在 D1.2 DoD 增加 runner 资源 SLA
- **证据**：`gantt:141, 488`
- **置信度**：🟢

### P1-G-7：ENC 数据获取（D2.2/SIL）授权风险已识别但缺备用
- **finding**：R2.2 风险条目"先用 OpenSeaMap 验证接口逻辑；向航海图志局申请开发测试授权"。但 SIL 1000+ 场景需要真实 ENC 才能体现禁区/航道/水深约束
- **影响**：若授权 8 月仍未拿到，D3.6 中"ENC zone 约束"测试只能用 OpenSeaMap，不能作为 CCS 提交证据
- **整改建议**：HAZID kickoff 同期向 CCS 项目组报备 ENC 授权状态；如 6/15 仍未到位，D3.6 DoD 在 ENC 维度降级 + 后续 HIL 阶段补
- **证据**：`gantt:301`（R2.2）
- **置信度**：🟢

### P1-G-8：D3.7 "8 模块联合 SIL 8h 不崩溃"是必要不充分
- **finding**：8h 测试只验证内存泄漏 / 时钟漂移 / 长跑稳定性，**不**验证决策正确性。但 D3.7 是 Phase 3 的最终交付物，CCS 会以此为准
- **影响**：8h 期间若无足够多样化的场景注入，"无崩溃"可能等于"长时间巡航无威胁"
- **整改建议**：D3.7 增加"8h 期间至少注入 50 个 high-stakes 场景（MRC / Reflex Arc / SOTIF 假设违反）"
- **证据**：`gantt:486-489`
- **置信度**：🟢

---

## 4. P2 改进建议（5 条）

- **P2-G-1**：考虑采用 **FMI 2.0 + OSP-IS** 标准化 FCB 仿真器为 FMU，便于后续与 L1/L2/L4/L5 团队联合 SIL（DNV OSP 路径）。https://opensimulationplatform.com/ 🟢
- **P2-G-2**：场景库文件命名规范缺失，建议 `<rule>-<odd>-<encounter>-<seed>-<version>.yaml`（如 `R15-A-crossing-0042-v3.yaml`），便于 git 自动 diff 与 traceability
- **P2-G-3**：调试 HMI 增加"时间倒退 + 复盘 + 对比两次运行 diff"功能（OSP sim-explorer 已有相关启发，2024 OSP Conference 演示）
- **P2-G-4**：RL 对抗实现选型尚未定（PPO / SAC / curriculum）。建议在 Phase 3 启动前用 1 人天评估 stable-baselines3 现成实现 vs 自实现（决策原则：测试工具不是认证对象，可用黑箱库）
- **P2-G-5**：场景生成可接入 OSP Conference 2025 介绍的 **`sim-explorer`** 模式（"facilitates simulation experimentation through simple configuration files"），减少自研负担

---

## 5. 架构 / 模块缺失项

1. **架构 v1.1.2 §15.2 接口矩阵无 SIL 观测面**：建议补一行 `M{1..8} → SIL Observability Bus | SIL_ProbeMsg | 10 Hz | 内部状态 + 计算耗时 + 协方差`
2. **CLAUDE.md §3 八模块表无 "SIL 框架" 一项**：作为唯一的横向工具基础设施，应作为"M0 Test Infra"或专门附录
3. **架构 v1.1.2 §11 / §14（CCS 映射）未把"工具鉴定"列为子功能**：DNV CG-0264 §4.x 子功能映射应增加 "verification & validation tools qualification"
4. **缺失 `docs/Design/SIL/` 目录**：当前 SIL 全靠开发计划描述，无独立设计文档。建议新增 `docs/Design/SIL/01-platform-architecture.md`（v0.1，Phase 1 W2 输出）

---

## 6. 调研引用清单

### 仓库内
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §15.2 接口矩阵（行 1596-1633）；§16 引用清理段（行 1686-1688）
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` D1.3 行 124-141；D2.4 行 253-275；D3.6 行 455-471；D3.7 行 475-489；R2.1/R2.2 行 298-302；R3.x 行 495-501
- `docs/Design/Detailed Design/M1-ODD-Envelope-Manager/01-detailed-design.md` §9.3 行 832-925
- `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` §9.1-9.3 行 832-891
- `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md` §9 行 681-740
- `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md` §9 行 780-895
- `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md` §9 行 979-1097
- `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md` §9 行 1290-1490；§10.4 行 45
- `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md` §9 行 660-750

### NLM 笔记本（safety_verification, 64 sources）
- 查询 1（高 confidence）：HIL/SIL 一般实践 / DNV-Yara Birkeland 涉及 / Kongsberg K-MATE 概述。**No specific OSP/FMI/scenario-coverage 内容** — 提示该 DOMAIN 缺 SIL 平台工程文献

### Web Sources
- DNV Open Simulation Platform 主页：https://open-simulation-platform.github.io/ 🟢
- OSP Conference 2024 program (MLFMU, sim-explorer, proxy-fmu)：https://opensimulationplatform.com/2024/09/23/osp-conference-2024-program/ 🟢
- OSP Conference 2025 program (SeaZero autodocking, FMU4cpp)：https://opensimulationplatform.com/2025/10/21/osp-conference-2025/ 🟢
- DNV Simulation Trust Center：https://www.dnv.com/services/simulation-trust-center-collaboration-platform-207515/ 🟢
- DNV MLFMU technical insights：https://technologyinsights.dnv.com/creating-functional-mock-up-unit-fmu-models-from-machine-learning-models/ 🟢
- proxy-fmu (distributed FMU)：https://github.com/open-simulation-platform/proxy-fmu 🟢
- Kongsberg K-Sim 仿真平台：https://www.kongsberg.com/maritime/products/simulation/ 🟢
- Kongsberg K-Mag autonomous（Yara Birkeland 关联）：https://www.kongsberg.com/maritime/news-and-events/our-stories/autonomy-is-here--powered-by-kongsberg/ 🟢

### 6.x 建议新增 DOMAIN 笔记本
- 名称：DOMAIN · SIL_HIL_Platform_Engineering · Research
- 关键词：[SIL, HIL, FMI 2.0, FMU, OSP, scenario library, coverage metrics, simulator V&V, tool qualification, deterministic replay, MASS verification, scenario-based testing]
- 触发原因：本评审 P0-G-2 / P0-G-3 / P0-G-4 / P0-G-5 / P1-G-3 / P2-G-1 / P2-G-5 共 7 个 finding 在现有 5 个 DOMAIN 中均无良好归属：
  - safety_verification 偏 HARA/FMEA/SIL 等级，不含平台工程
  - ship_maneuvering 当前为空（且本评审 P1-G-3 已识别该 DOMAIN 需 deep research 补 FCB MMG 文献）
  - 其余 3 个 DOMAIN 主题不匹配
- 等待主 agent 在汇总阶段统一审议创建

---

## 7. 行业基准对标（Industry Benchmark）

| Finding | 对标对象 | 证据 | Gap | 置信度 |
|---|---|---|---|---|
| P0-G-1 | DNV OSP 项目（2018 启动至今迭代）；Kongsberg K-Sim（多年专业团队） | OSP Conference 2024/2025；Kongsberg autonomy stories | 5 人周 / 6 子模块严重偏离行业实例 | 🟢 |
| P0-G-2 | DNV CG-0264 [R9] §4.x objective evidence 要求 + DNV STC 协作模式 | NLM safety_verification citation 2 + DNV STC URL | 当前无场景库版本/追溯，CCS 不可接受 | 🟢 |
| P0-G-3 | OSP MLFMU + sim-explorer；ISO 26262 TCL（类比） | OSP 2024/2025 conference | 当前无 simulator qualification report | 🟢 |
| P0-G-4 | ISO 21448:2022 SOTIF triggering condition coverage; MC/DC 标准 | [R6] 引用 | 当前仅 pass-rate，无组合覆盖 | 🟢 |
| P0-G-5 | FMI 2.0 / OSP-IS / SSP 标准 | OSP 主页 + proxy-fmu | 当前未采用任何跨平台标准 | 🟢 |
| P1-G-3 | Yasukawa & Yoshimura 2015 [R7]（4-DOF MMG 标准） | 架构 §16 + ship_maneuvering DOMAIN（待补） | FCB 高速段不在 [R7] 主要验证域 | 🟡 |
| P1-G-1 | DNV-CG-0264 Doer-Checker 独立性（等同 ISO 26262 ASIL decomposition） | M7 §10.4 + RFC-003 | SIL 观测须不打破 PATH-S 独立性 | 🟢 |
| P2-G-1/G-5 | DNV STC + OSP sim-explorer 工业模式 | OSP Conference 2024/2025 | 自研路径 vs 站在巨人肩膀 | 🟢 |

⚫ Unknown / No industry analog found：
- "5 人周 6 子模块"无任何工业先例（所有公开案例都是数十至数百人周量级）

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

> "本 angle 下被审查的设计/计划，是否引入了**只对 FCB 成立**的隐含假设？"

### FCB-only 假设清单

1. **🔴 P0**：D1.3 「FCB 4-DOF MMG 物理仿真器」**仿真器与船型耦合**——若直接命名 `FCB_MMG_Simulator` 类、若 `params.yaml` 顶层无 vessel 抽象、若 SIL 场景文件硬编码 18 kn 巡航假设，则换拖船（typically ≤ 12 kn / 4-DOF 也变化）/ 渡船 / 集装箱船时仿真器必须重写。
   - **影响范围**：`D1.3` 的所有 6 个子模块；`D3.6` 的 1000 场景库（场景里如果嵌入"自船 18 kn"成为 baseline 即 FCB-only）
   - **整改**：仿真器接口为 `ShipMotionSimulator`（abstract），FCB 是 plugin；YAML 顶层 `vessel_class: FCB | TUG | FERRY | CONTAINER`；场景文件中速度参数化（`v_cruise: ${vessel.v_cruise}`），不写硬编码

2. **🟡 P1**：D2.4 / D3.6 的"COLREGs 500/1000 场景"——场景设计若只覆盖 FCB 高速 crew boat 典型遭遇（如 18 kn 对遇 12 kn 商船），缺少拖船低速场景（5 kn 拖带 + 受限操纵）和渡船重复路径场景。当前计划未要求场景库按 vessel class 分集
   - **整改**：场景 schema 增加 `vessel_class_applicable: [FCB, TUG, FERRY, CONTAINER]` 字段；D3.6 DoD 增加"≥ 60% 场景对 FCB 适用，≥ 20% 对 TUG / FERRY 通用"

3. **🟡 P1**：D1.3 的"FCB 高速段 ROT_max 速度查表 [TBD-HAZID]"在 SIL 中测试——若 BC-MPC 测试用例硬编码 FCB 的 ROT_max 表 索引，换船型时 SIL 失效
   - **整改**：ROT_max 表通过 Capability Manifest YAML 注入，SIL 测试用例引用 manifest 字段而非具体数值

4. **🟢 OK**：架构 v1.1.2 §4 决策四"决策核心零船型常量"已强制；M5 设计采用 Capability Manifest YAML 驱动参数。SIL 框架若严格遵循 Backseat Driver 范式即可避免大多数 FCB-only 陷阱

### 锚定
CLAUDE.md §4 顶层决策四（Capability Manifest + PVA 适配 + 水动力插件 / Backseat Driver）；架构 v1.1.2 §10.5（FCB 适配章节）

---

*报告完成 — G angle SIL System Construction. 2026-05-07*
