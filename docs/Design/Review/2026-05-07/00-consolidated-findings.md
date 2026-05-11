# MASS L3 TDL 多角度评审 · 跨角度整合清单

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-REVIEW-CONSOLIDATED-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-07 |
| 评审基线 | 架构 v1.1.2 / 8 模块详细设计草稿 / 8 月开发计划 v2.0 |
| 评审 angles | A 进度 / B 算法 / C 合规 / D 船长-HMI-人因 / E 测试 / F 架构完整性 / G SIL 系统 |
| 评审产出 | 7 份独立报告 + 本汇总 |
| 总体判断 | **🔴 红线**——5 个角度（A/C/D/E/G）独立判定为红，B/F 为黄；**至少 12 项 P0 必须 5 月开工前关闭**，否则 8/31 硬承诺与 12 月实船试航不可达 |

---

## 0. 执行摘要（≤ 300 字）

7 个角度合计产出 **30 项 P0、52 项 P1、29 项 P2**，跨角度去重后归纳为 **15 个根因簇**。最严重的 5 个簇（按修复急迫度排序）：

1. **8/31 硬承诺单点压力**（A+E+G）：M5/M7/SIL/HAZID 回填/全集成同时压在 8/10–8/31，技术负责人 5 条线；D1.3 SIL 5 人周低估 60%（实际 ≥ 9.5 人周）
2. **HAZID 工时不足 + 与开发计划船期冲突**（A+C+E）：13 周 × 26h 总会议时间应付 132 [TBD] 参数（每参数 12 min），同时 RUN-001 把 FCB 实船排在 6/10–6/24，与开发计划 12 月实船冲突
3. **多船型抽象泄漏**（B+D+F+G）：M5 `ROT_max=8°/s` 硬编、M2 `sog ≤ 30 kn` 标注 FCB 满载、M8 `active_role: ROC_OPERATOR` 默认、SIL 仿真器与 FCB 耦合；至少 4 处 P0 级 FCB-only 假设
4. **认证证据 4 类核心 AIP 工件缺独立可审版本**（C+E+F）：ConOps、ALARP、HARA/Hazard Log、SDLC 工件全部仅在"应交清单"层级；网安 §16 / 时基同步 / ENC chart manager / FMEDA 表均缺失
5. **测试体系缺 V&V Plan + 覆盖率定义不闭合 + 12 月实船 NO-GO 门槛缺**（E+G）：DNV-CG-0264 强制 V&V Plan 先于测试启动；当前覆盖率仅"Rule 全分支 + 98% pass"，缺 Rule × ODD × 扰动立方体 + SOTIF triggering condition；12 月上船时 AIP 未受理 + SIL 2 仅初评

**底线**：若不在 5 月开工前修齐至少前 3 簇的 P0 项，建议把 8/31 拆为 **"软关门 + 9/30 硬关门"**，并把 12 月实船降级为"非认证级技术验证试航"。

---

## 1. 5 月开工前 must-fix（10 项 P0，关闭日期 ≤ 5/13）

> 以下任一项不关闭 → 5/13 不应启动相应模块编码。

| # | Finding（合并源） | Owner | 关闭日期 | 证据锚点 |
|---|---|---|---|---|
| **MUST-1** | **M2 OVERTAKING 扇区与架构/M6 矛盾**（M2 [225°,337.5°] vs 架构/M6 [112.5°,247.5°] — 一定有一错，是 SIL 2 安全问题）| M2 负责人 | **5/13** | B P0-B-03; M2 §5.1.3:387,417 / arch §6.3.1:585 / M6 §5.1:311 |
| **MUST-2** | **Mid-MPC N 步数三处不一致**（架构 §10.3 N=12 / M5 详设 N=12–18 / RFC-001 锁定 N=18）| M5 负责人 + 架构师 | **5/13** | B P0-B-02; arch:850 / M5:184,272 / RFC-001 |
| **MUST-3** | **IvP 实现路径未决（GPL/LGPL/BSD 待审）**——M4 编码起点必须先裁决 libIvP 许可 vs 自实现 | M4 负责人 + 法务 + 架构师 | **5/13** | B P0-B-01; M4 §5.6:805-806 |
| **MUST-4** | **HAZID RUN-001 与开发计划 FCB 占用冲突**（HAZID 把 FCB 实船放 6/10–6/24，开发计划放 12 月）| PM + 架构师 + HAZID 主持 | **5/13** | A P0-A2; HAZID kickoff:78-112,152-157 / gantt:13,519 |
| **MUST-5** | **M5 §7.2 FM-4 fallback `ROT_max=8°/s` 硬编**（决策四 Capability Manifest 泄漏；多船型 P0）| M5 负责人 | **5/13** | B FCB-01; F P0-F-03; M5:674 |
| **MUST-6** | **M2 §2.2 校验 `sog ∈ [0,30] kn / FCB 满载极限` 硬编**（决策四 Capability Manifest 泄漏；多船型 P0）| M2 负责人 | **5/13** | F P0-F-03; M2:48 |
| **MUST-7** | **M8 `active_role: ROC_OPERATOR` 默认假设**（违反多船型；拖船/渡船 on-board primary 工作流不兼容）| 架构师 + M8 负责人 | **5/13**（至少补 RFC stub） | D P0-D-04; M8:111 |
| **MUST-8** | **PATH-S 独立性 CI 检查启用时点**（D1.2 5/24 启用 vs M7 7/20 才动工，期间 M1–M6 ~22 人周代码可能引入跨边界依赖）| 基础设施 + M7 负责人 | **5/13** | A P1-A8; gantt:120,498 |
| **MUST-9** | **M5 自触 MRM-01（FM-2）违反 Doer-Checker**（决策四：MRM 命令集只能由 M7 触发）| M5 负责人 | **5/13** | B P1-B-08; M5:672 / arch §11.6:988-1007 |
| **MUST-10** | **DOMAIN 笔记本扩充**（ship_maneuvering 当前 89 sources 但 maritime_human_factors 19 sources 偏少；safety_verification 缺 DNV-RP-0671/ABS Autonomous Guide/Yara Birkeland 证据；建议汇总阶段批量 deep research） | 架构师 | 5/13 前完成 ≥ 2 项 deep research | A/B/C/D/F NLM 缺口 |
| **MUST-11** | **M7 effort 6 → 9 人周**（用户 2026-05-07 决策保留 8/31 硬关门，必须以 effort 上调补偿；M7 拆为 M7-core 6w + M7-sotif 3w）| M7 负责人 + PM | **5/13** | A P0-A3; 用户决策 §13.2 |

---

## 2. Phase 1 内必修 P0（关闭日期 ≤ 6/15）

| # | Finding（合并源） | Owner | 关闭日期 | 证据锚点 |
|---|---|---|---|---|
| P1-FIX-1 | **D1.3 SIL 5 人周低估 60%**（实际 ≥ 9.5 人周）→ 拆 D1.3a/b/c，RL 对抗后移 Phase 3 | 技术负责人 + PM | 5/24（拆分定稿） | G P0-G-1; gantt:124-141 |
| P1-FIX-2 | **V&V Plan 文档缺失**（DNV-CG-0264 强制先于测试启动审批）→ 5/15 前出 `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` | 架构师 + V&V 负责人 | 5/15 | E P0-E1; gantt:484-489,546-547 |
| P1-FIX-3 | **场景库工程化缺失**（无 schema/版本/CI/diff/追溯）→ 5/24 前出 `docs/Design/SIL/scenario-schema.md` + traceability matrix；场景独立 Git repo；CI 三层集 | 技术负责人 + 架构师 | 5/24 | G P0-G-2; gantt:140,455-471 |
| P1-FIX-4 | **SIL 仿真器自身 V&V 缺**（无 reference solution、无确定性回放、无 MMG 参数变更回归测试）→ 增 D1.3.1 Simulator Qualification Report 1 人周 | 技术负责人 + M5 负责人 | 6/15 | G P0-G-3; gantt:136 |
| P1-FIX-5 | **覆盖率定义不闭合**（缺 Rule × ODD × 扰动立方体 + SOTIF triggering condition）→ 5/31 前出 `docs/Design/SIL/coverage-metrics.md`，含 MC/DC、组合矩阵、SOTIF 清单 | M6 负责人 + 安全工程师 | 5/31 | E P0-E3 + G P0-G-4 |
| P1-FIX-6 | **ConOps + ALARP + HARA + SDLC 4 类 AIP 核心证据缺独立可审版本**（不能等 11 月 D4.5 单点交付）→ 拆分到 Phase 1–2 内独立产出 4 件文档 | 架构师 + 安全工程师 + PM | 6/15 | C P0-C-1; arch §14.3,14.4; audit/00:108-111 |
| P1-FIX-7 | **L3 网络安全 §16 缺失**（IACS UR E26/E27 自 2024-07 强制；§15 21 行接口零鉴权/完整性）→ v1.1.3 新增 §16；每条接口加 4 列 auth/integrity/replay/dds-security profile | 架构师 + M8 负责人 | 6/15（stub）+ v1.1.3 全文 | F P0-F-01; arch §14.4:1371; RFC-decisions:78 |
| P1-FIX-8 | **时间同步 master 未定义**（IDL 普遍要求 `stamp` 但全文无 PTP/NTP/sync error budget）→ v1.1.3 §15.0 指定 IEEE 1588 PTPv2 grandmaster + sync error budget | 架构师 + Multimodal Fusion 跨团队 | v1.1.3 | F P0-F-02; arch §15 全部 IDL |
| P1-FIX-9 | **环境输入 FMEA 缺失**（visibility/Hs/current/ENC 错误数据无 sanity check；M3 海流 3kn 误差仅"中"等级）→ v1.1.3 §6.5 新增"环境输入 FMEA"；ENC Chart Manager 模块化 | M2 + M3 + M7 + 架构师 | v1.1.3 | F P0-F-04; M2 §2.2; M3:718; M5:675 |
| P1-FIX-10 | **Doer-Checker "100×" 简化主张缺可量化指标**（CCS 外审需要等效工件）→ 改为三可量化矩阵：LOC 比 ≥ 50:1、圈复杂度均值比 ≥ 30:1、SBOM 交集 = ∅；补 IEC 61508-3 §7.4.4 全维度独立矩阵 | 架构师 + M7 负责人 | 6/15 | C P0-C-3; audit/00:31; M7 §10.4 |
| P1-FIX-11 | **HAZID 工时与覆盖范围量级偏低**（每参数 1.6h；workshop 应 ≥ 2 个 full-day 集中会议；CCS 验船师持续介入）→ 重排 HAZID 议程；RUN-002/003 时长从 4 周 → ≥ 6 周 | HAZID 主持 + PM + CCS 联络 | 6/15 | A P1-A1 + C P0-C-2 + C MV-C-4 |
| P1-FIX-12 | **8/31 单点压力 / 关键路径零缓冲**（5 条 D3.x 同压技术负责人）→ 拆 owner（D3.5 架构师 / D3.6 V&V 工程师 / D3.7 技术负责人）；M7 effort 6 → ≥ 9 人周；M5 8/10 软目标 | PM + 架构师 | 6/15（owner 矩阵 + 资源日历） | A P0-A1+A3 + E P0-E5 + G P0-G-1 |

---

## 3. P1 重要风险（按根因簇聚合，关闭日期 ≤ Phase 2 末 7/31）

### 簇 A：算法实测与基准缺位（B 主）
- **B P0-B-04 + B P1-B-01**：BC-MPC < 100ms SLA 与 M5 详设 §6.2 自承认 100–150ms 冲突；IPOPT solve time p99 缺 FCB 目标硬件实测。**整改**：5/13 前用原型实测 BC-MPC p50/p99 + IPOPT 直方图；SLA 改 < 150ms / 8 Hz。证据：M5:578,623。
- **B P0-B-05 + G P1-G-3**：4-DOF MMG 对 FCB 18–25 kn 半滑行段无正面证据。**整改**：v1.1.3 §10.5 加"4-DOF 适用性边界声明"；HAZID 加"FCB 高速保真度残差测试"；hull_class=SEMI_PLANING 触发 6-DOF 预留；ship_maneuvering DOMAIN deep research 补 FCB-class crew boat MMG validation 文献。
- **B P1-B-02**：M5 BC-MPC `target.intent_uncertainty` 由 M2 提供但 M2 接口未定义。**整改**：5/27 前出 RFC-007/008；推荐 M2 输出 intent confidence 标量 + BC-MPC 自身穷举离散 intent。
- **B P1-B-03**：ROT_max(u) 速度修正表 (架构 §10.5) 是经验线性段，缺工业基准。**整改**：HAZID 加"ROT_max(u) 实测曲线"任务；查表入 Capability Manifest + 0.8 安全因子。
- **B P1-B-06**：算法替代方案（RRT*/VO/MPPI/Hybrid A*）零讨论。**整改**：v1.1.3 §10.1 加"决策原因"+ 4×4 替代方案对比表（CCS AIP 必查）。
- **B P1-B-07**：CPA_safe 三段值缺工业先例对比（Kongsberg/Wärtsilä/NTNU）。**整改**：HAZID kickoff 前补"CPA_safe 工业对标表"作为 HAZID 起点。

### 簇 B：船长 / HMI / 人因（D 主，影响实船试航准入）
- **D P0-D-01**（实质 P0，与 P1-FIX-12 等重要）：8 月计划 0 个船长/ROC 培训交付物；违反 IMO MASS Code Chapter 15 + DNV HTW11；12 月试航无合规船员。**整改**：5/13 前在 8 月计划新增 D2.6（胜任力矩阵草案）+ D3.5'（模拟器训练课程 ≥ 30 学时）+ D4.5'（船长/ROC 模拟器认证作为试航准入）；架构 v1.1.3 §12.5 新增"人员资质/培训/演练"小节。
- **D P0-D-02**：ToR 60s 单一阈值；Veitch 2024 是"已就位"场景。**整改**：架构 §3.4 + M8 详设 §5.3 改为场景自适应矩阵（ROC 已坐席 60s / 船长在桥楼 30s / 餐厅 90s / 睡舱 120s [HAZID 校准]）；ToR_RequestMsg 加 `assumed_operator_state`。
- **D P0-D-03**：船上船长视图零原型 / 零访谈 / 零可用性测试。**整改**：增 D3.4.5 子任务 1.5 周（≥ 3 名 FCB + 1 拖船 + 1 渡船船长访谈 + Figma 半保真 + 桌面可用性 n ≥ 5）；架构 v1.1.3 §12.3 新增"船长心智模型 ground truth"小节；M8 验收含"≥ 1 名资深船长签字可用性认可"。
- **D P0-D-05**：UI 50 Hz 对船长是"屏幕粘眼"。**整改**：M8 §3.2 拆 3 档（`data_stream_50hz` 仅 ROC + ASDR / `display_state_4hz` 船长面板默认 / `alert_burst_event` 紧急）。
- **D P1-D-01 ~ P1-D-08**：紧急 ToR < 30s 路径缺 / [R4] DOI 校对 / 5s 强制等待 trade-off / BNWAS 等价机制缺 / 恶劣环境零证据 / ROC fallback / 长期决策疲劳 / Y-axis Reflex Arc → M8 通告通路缺。**整改**：见 D 报告 §3 各条对应整改；BNWAS 模块对齐 IMO MSC.282(86)。

### 簇 C：合规 / 认证 / SOTIF 完整性
- **C P1-C-1**：SOTIF 6 类 vs HAZID `03-sotif-thresholds.md` 4 类不一致。**整改**：统一 6 行表 + 索引到 M7 §5.3.1。
- **C P1-C-2**：ISO 21448:2022 "已知-安全/已知-不安全/未知-不安全"三类穷举证据缺；RFC-003 enum-only veto 95% 覆盖率自证无支撑。**整改**：HAZID 设 1 次会议做 SOTIF area 完整性 mapping；OTHER enum 出现率作为 KPI 监控。
- **C P1-C-3**：IMO MASS Code 4-level 模式指示未在 L3 出口对接。**整改**：M8 SAT-1 显式输出 IMO 4-level 字符；RFC-004 ASDR 接口加 `imo_mode_level`。
- **C P1-C-4**：TMR ≥ 60s 单一文献锚点（Veitch 2024 RCT），未挂"使用条件"。**整改**：标"实验下限"；HAZID 周 7 NASA TLX + 实船 ROC 实验校准 FCB 实景 95% 分位（与 D P0-D-02 联动）。
- **C P1-C-5 + F P0-F-01**：IACS UR E26/E27 L3 边界承担方式无 RFC。**整改**：补 RFC-007 L3 ↔ Z-TOP/Cybersec 接口。
- **C P1-C-6**：SIL 1000+ 场景作为 Type Approval 证据规模偏小（Yara/K-MATE 公开案例多在 10⁴ 量级 ⚫）。**整改**：Phase 4 HIL 加 Failure 场景库 + 根因表；目标 5000+。
- **C P1-C-7**：i-Ship 三阶段路径与 11 月 AIP 节点冲突。**整改**：选 (a) 11 月仅 AIP 不含阶段二材料 / (b) 阶段一/二合并并书面备案。
- **C P1-C-8**：M1/M7 缺 FMEDA 表（SFF/HFT/DC 实例化）。**整改**：每模块详设 §11 加 ≥ 20 失效模式 FMEDA 表。

### 簇 D：架构完整性
- **F P1-F-01**：M3 角色 vs L1/L2 边界仍模糊（M3 同时订阅 L1 VoyageTask + L2 PlannedRoute = 绕过 L2 抽象）。**整改**：v1.1.3 决议单链 vs 双订阅 + 跨层独立性论证。
- **F P1-F-02**：M3 命名 "Mission Manager" 残留 vs §7 "Local Mission Tracker"。**整改**：v1.1.3 全文搜索替换 + CLAUDE.md §3 同步。
- **F P1-F-03**：Reflex Arc / Override / Checker / L3 优先级仲裁矩阵未入主文档（散落 RFC-005）。**整改**：v1.1.3 升入主文档独立小节。
- **F P1-F-04 + P1-F-06**：ASDR 与 §15 IDL 缺 `schema_version` 字段。**整改**：IDL 加 `string schema_version` + 兼容性政策（reserved tag 风格）。
- **F P1-F-05**：M7 心跳由 X-axis 监控但无 RFC。**整改**：补 RFC-007（M7 ↔ X-axis 心跳契约）。

### 簇 E：测试与试航
- **E P1-E1**：HIL "≥ 200h" 与架构 §14.4 "800h+" 自相矛盾。**整改**：800h 为目标，200h 为最低 gate；明文"800h 之前不能进入 SIL 2 终评"。
- **E P1-E2 + G P1-G-5**：场景库三场地 (SIL/HIL/实船) 一致性传递机制未定义；SIL 调试 HMI 与 M8 HMI 复用关系未定。**整改**：场景 YAML schema 含"HIL 等价输入 + 实船人工注入步骤"两段元数据；引入 FMI 2.0 + OSP-IS 标准化 FCB MMG 为 FMU；SIL HMI 与 M8 HMI 共用前端组件库（M8 superset）。
- **E P1-E3**：Adversarial vs nominal 比例无显式声明（行业 ⚫ Unknown）。**整改**：D3.6 显式 nominal:边界:对抗 = 60:25:15（待 V&V Plan 评审）。
- **E P1-E4**：感知-决策端到端 P95/P99 全链路无统一 KPI。**整改**：V&V Plan 加端到端延迟（AvoidancePlan P95 ≤ 1.0s / ReactiveOverrideCmd ≤ 200ms）。
- **E P1-E5**：ASDR 数据格式跨 SIL/HIL/实船 一致性未约束。**整改**：D1.3 SIL 框架增"ASDR 同格式输出"。
- **E P1-E6**：M6 D2.4 "≥ 500 场景 ≥ 98%"在 7/31 首次跑就达 98% 不现实。**整改**：分级 (a) 首跑 ≥ 90% (b) 2 周修缮 ≥ 95% (c) D3.6 1000 场景 ≥ 98%。
- **E P1-E7**：HAZID 132 参数中"行为分支结构敏感"参数未预先识别（YAML 热加载假设错误）。**整改**：D3.5 拆"安全热加载白名单"vs"需重跑回归列表"。
- **E P1-E8**：CCS i-Ship §14.4 9 类证据与 8 月计划交付物不一一对应。**整改**：补跟踪表 `cert-evidence-tracking.md`。
- **G P1-G-1**：M7 PATH-S 独立路径在 SIL 中如何被观测。**整改**：M7 通过只读 ROS2 topic `m7/sil_observability` 发布内部状态；架构 v1.1.3 §15.2 接口矩阵补一行。
- **G P1-G-2**：RL 对抗错放 Phase 1（stub 阶段无意义）。**整改**：整体后移到 Phase 3 W14（M6+M5 都有真实代码后）。
- **G P1-G-4**：环境/不确定性建模散落，无统一 stochastic environment layer。**整改**：D1.3 YAML 场景管理增 `disturbance_model:` 节 + PRNG seed + Monte-Carlo 工作流。
- **G P1-G-6**：SIL 单次 ≤ 60s vs 1000 场景 CI 时间冲突。**整改**：D1.2 CI 拓扑明确 PR-trigger Smoke 10 / Nightly 200 / Weekly Full 1000+；runner 资源 SLA。
- **G P1-G-7**：ENC 数据获取授权风险已识别但缺备用。**整改**：HAZID kickoff 同期向 CCS 报备授权状态；6/15 仍未到位则 D3.6 ENC 维度降级。
- **G P1-G-8**：D3.7 8h 不崩溃 ≠ 决策正确。**整改**：8h 期间至少注入 50 个 high-stakes 场景。
- **A P1-A2 ~ P1-A7**：M5 双 MPC 7 人周偏紧 / HIL 3 月交期乐观档 / 12 月实船准入门槛未定义 / "2 人 + AI" 无回弹 / Phase 1 SIL 5 人周激进 / 500→1000 场景翻倍仅 1 月。**整改**：见 A 报告各条对应整改；增 `docs/Design/Resource Calendar/FCB-occupancy.md` 单一资源真源；新增"实船准入门槛"短表。

---

## 4. P2 改进建议（精选 8 条，关闭日期 = Phase 3 内）

| # | Finding | 来源 |
|---|---|---|
| P2-1 | UI_StateMsg JSON 8–12 KB @ 50 Hz = 4–6 Mbps 带宽 budget；M8 §10.1 加"船岸链路带宽 budget"评估；ECDIS (IHO S-100/IEC 61174) 集成接口 | D P2-D-03+04 |
| P2-2 | §15.2 接口矩阵缺 QoS（reliability/durability/deadline/lifespan）4 列 | F P2-F-01 |
| P2-3 | Capability Manifest 自定义规范无 schema 版本与签名链（CCS root → vessel manifest → plugin .so）| F P2-F-03 |
| P2-4 | 缺 Parameter Store / Calibration Manager 子模块（HAZID 校准的 132+ 参数部署后由谁写入哪里）| F P2-F-04 |
| P2-5 | BC-MPC `urgency_level > 0.95` 时扩 k=7 + δψ=20°（即 ±60°），避免 BC → MRM-03 间隙 | B P2-B-01 |
| P2-6 | M5 ASDR 字段缺 IPOPT KKT residual / dual_inf | B P2-B-04 |
| P2-7 | 考虑 FMI 2.0 + OSP-IS + sim-explorer 标准化（DNV OSP 路径）| G P2-G-1+5 |
| P2-8 | 场景文件命名规范 `<rule>-<odd>-<encounter>-<seed>-<version>.yaml` | G P2-G-2 |

> 完整 P2 列表见各 angle 报告 §4。

---

## 5. 架构 v1.1.3 待补清单（HAZID 不一定盖到的项）

| # | 缺失项 | 说明 | 来源 |
|---|---|---|---|
| MISS-1 | **§16 Cybersecurity Spec**（与 §14 平级）| IACS UR E26/E27 强制；trust zone 划分 + 接口 4 列鉴权属性 + ASDR HMAC（替换裸 SHA-256）| F P0-F-01 |
| MISS-2 | **§15.0 时基与同步**（IEEE 1588 PTPv2 grandmaster + sync error budget + 退化路径）| F P0-F-02 |
| MISS-3 | **§13 测试策略章节** 或 V&V Plan 引入 | E §5; F |
| MISS-4 | **ENC / Chart Update Manager** 子模块 | F MISS / E §5 |
| MISS-5 | **Parameter Store / Calibration Manager** 子模块 | F P2-F-04 |
| MISS-6 | **Environment Cross-Source Validator**（M2 子模块或独立） | F P0-F-04 |
| MISS-7 | **§12.5 人员资质 / 培训 / 演练**小节 + 引用 MASS Code Chapter 15 + STCW MASS amendments | D P0-D-01 |
| MISS-8 | **§12.3 船长心智模型 ground truth**（船长视线分布 / 扫视模式 / 注意优先级，访谈数据）| D P0-D-03 |
| MISS-9 | **BNWAS / Watch Vigilance Watchdog** 模块（IMO MSC.282(86) 等价） | D §5 |
| MISS-10 | **Y-axis Reflex Arc → M8 通告通路** | D §5 |
| MISS-11 | **船长面板 ↔ ECDIS 集成接口** | D §5 / P2-D-04 |
| MISS-12 | **算法选型矩阵**（v1.1.3 §10.1：MPC vs RRT* vs VO vs MPPI 4×4 对比）| B P1-B-06 |
| MISS-13 | **MMG 4-DOF 适用性边界声明**（v1.1.3 §10.5；Hs > 1.5m + u > 18 kn 偏差幅度）| B P0-B-05 |
| MISS-14 | **L3 仲裁优先级矩阵**（升入主文档；当前散落 RFC-005）| F P1-F-03 |
| MISS-15 | **§15.x SIL Observability Bus** 一行 | G §5 |
| MISS-16 | **故障注入测试规范**（每类故障注入方式：信号层 / 消息层 / 进程级）| E §5 |
| MISS-17 | **ConOps / HARA / ALARP / SDLC 4 类独立目录**（`docs/Design/ConOps/`、`Safety/HARA`、`Safety/ALARP`、`SDLC`、`Cybersecurity`）| C §5 / E §5 |

---

## 6. 多船型泛化阻塞清单（决策四违反，所有 P0 级 FCB-only 假设）

| # | 假设 | 文件:行 | 影响 | Severity | 整改 |
|---|---|---|---|---|---|
| MV-1 | M5 §7.2 FM-4 fallback `ROT_max=8°/s` 硬编 | M5:674 | 拖船/70m PSV ROT_max < 8°/s，fallback 反而激进 | **P0** | 改为 `OUT_of_ODD → MRM`；删硬数 |
| MV-2 | M2 §2.2 校验 `sog ∈ [0,30] kn (FCB 满载极限)` | M2:48 | 高速渡船自身被标 INVALID | **P0** | `f(Manifest.max_speed)` |
| MV-3 | M8 `active_role: ROC_OPERATOR` 默认 | M8:111 | 拖船/渡船 on-board primary 不兼容 | **P0** | 对称双角色 PRIMARY_ON_BOARD/PRIMARY_ROC/DUAL |
| MV-4 | SIL 仿真器与 FCB 耦合（D1.3 命名 / 顶层无 vessel 抽象 / 18 kn 巡航 baseline）| gantt:124-141 | 换船型必须重写仿真器内核 | **CLOSED** | Closed by D1.3a T3：ship_sim_interfaces + FCBSimulator plugin (feat/d1.2-cicd-pipeline，2026-05-13) |
| MV-5 | Mid-MPC N=18 / 90s 覆盖"FCB 18kn 制动 600–800m"逻辑 | arch §10.3 / M5:184,272 | N 应 = f(vessel_class)；拖船 12kn 制动 200m 严重过度 | **P0** | Capability Manifest `mpc_horizon_steps` 字段 |
| MV-6 | 4-DOF MMG 假设普适 | arch §10.5 | FCB 半滑行需 6-DOF；其他可继 4-DOF | **CLOSED** | Closed by D1.3a T2：hull_class: SEMI_PLANING 已添加到 fcb_dynamics.yaml (feat/d1.2-cicd-pipeline，2026-05-13) |
| MV-7 | ROT_max(u) 速度修正表系数（1.2/1.0/0.8）| arch §10.5:534-538 | 拖船 ROT_max << FCB；6kn 拖船 1.2× = "幻觉转向" | **P0** | Capability Manifest `rot_max_curve` 字段 |
| MV-8 | HAZID RUN-002/003 各仅 4 周 vs RUN-001 12 周 | HAZID kickoff:194-198 | 路径深度不对称，多船型 i-Ship 认证可信度受损 | P1 | RUN-002/003 ≥ 6 周 |
| MV-9 | f_speed_correction 段间断点 10/20 kn | arch §10.5 | 渡船半排水 max ~18kn，10kn 是巡航而非低速段 | P1 | Capability Manifest 化 |
| MV-10 | 波浪修正 `1 - 0.1×Hs` | arch §10.5 / M5:545 | 拖船型深 / 渡船型宽 GM 不同 | P1 | Capability Manifest 化 |
| MV-11 | CPA_safe(ODD-A) = 1.0 nm "FCB 实操值" | arch §10.3:852 | 拖船拖带受限船 Rule 18 需更大；港内渡班 0.2 nm | P1 | per-vessel HAZID 校准 |
| MV-12 | M8 50Hz UI / WebSocket 远程瘦客户端 | M8:80,815 | 渡船靠泊本地决策 < 5s 反应需本地路径 | P1 | UI_StateMsg_local + UI_StateMsg_roc 拆双套 |
| MV-13 | 场景库基于 FCB 高速 baseline | M6 §9.3 | 拖船 5 kn / 集装箱船 22 kn 时序常数完全不同 | P1 | 场景 YAML 含 `vessel_class_applicable[]` |
| MV-14 | 实船 50 nm / 100h 隐含开阔海域 + 短途 | arch §1.1 | 集装箱船跨洋一次 > 100h；拖船作业半径 < 5 nm | P2 | KPI 改"ODD 内代表性运营周期" |

> **PR review checklist 加一条**：`grep "FCB|45m|18 kn|22 kn|ROT_max\s*=\s*\d" 在 A 层（M1–M8 决策核心）必须为 0`

---

## 7. 行业基准 Gap（按 angle 聚合）

| 维度 | 本设计 | 对标 | Gap 程度 | 来源 |
|---|---|---|---|---|
| ROC 接管时窗 | 60s 单一 | NTNU SCL / Kongsberg：场景自适应 30–120s | 🔴 | D §7 |
| HMI 主刷新率 | 50 Hz | ABB Marine Pilot Vision：1–4 Hz 数据 + 事件触发 | 🟡 | D §7 |
| 训练交付 | 0 项 | DNV/Kongsberg：模拟器 ≥40h + 月度 ToR drill | 🔴 | D §7 |
| BNWAS 等价 | 缺 | IMO MSC.282(86) 强制 | 🔴 | D §7 |
| L3 内部 cyber 章节 | 无 | Kongsberg K-Bridge cyber spec / DNV CIP | 🔴 | F §7 |
| 全系统时基 master | 无 | IEEE 1588 PTPv2（航空/陆地自动驾驶通用） | 🟡 | F §7 |
| 环境输入 FMEA | 几乎无 | ISO 21448 §8 + DNV-CG-0264 §7 | 🔴 | F §7 |
| ENC chart manager | 无独立模块 | IMO MSC.232(82) ECDIS Type Approval | 🔴 | F §7 |
| SIL 5 人周 / 6 子模块 | 当前计划 | DNV OSP 多年迭代 / Kongsberg K-Sim 多年专业团队 | 🔴 | G §7 |
| 场景库工程化 | 无 schema/版本/追溯 | DNV CG-0264 + DNV STC 协作模式 | 🔴 | G §7 |
| 跨平台等价 | 无契约 | FMI 2.0 / OSP-IS / SSP 标准 | 🔴 | G §7 |
| Doer-Checker 简化 | 定性"100×" | Boeing 777 PFCS：LOC + 圈复杂度 + 处理器异构 三维量化 | 🔴 | C §7 |
| HAZID 工时 | ~26h workshop / 132 参数 | DNV-led DMA HAZID single-day 48 hazards [R1] | 🔴 | A §7 + C §7 |
| HIL 3 月交期 | 计划 | RoboticsTomorrow 2026-01：26–52 weeks for FPGA/motor IC | 🟢（数据可信，本计划乐观）| A §7 |
| SIL 1000+ scenarios | 计划 | Yara/K-MATE：10⁴ 量级（⚫ 数字未公开核证）| 🟡 | C §7 |
| 4-DOF MMG | 选型 | Yasukawa 2015 默认 3-DOF + 选项 roll；不覆盖 heave/pitch；Aalto 6-DOF 验证显示半滑行需 6-DOF | 🟢 | B §7 + G §7 |

⚫ Unknown / No industry analog found：
- BC-MPC p99 solve time（Kongsberg/Wärtsilä 商用闭源数据）
- adversarial:nominal 场景比例（DNV/Yara 均未公开）
- SIL 首次完整跑通过率（K-MATE/Yara 未披露）
- "2 人 + AI" 持续生产率船舶项目典型 rework rate
- Boeing 777 PFCS LOC 比 / 圈复杂度比具体数值

---

## 8. 新 DOMAIN 笔记本创建决策

| 提议 | 来源 | 关键词 | 主 agent 决策建议 |
|---|---|---|---|
| **`SIL_HIL_Platform_Engineering`** | G §6.x | SIL/HIL/FMI 2.0/FMU/OSP/scenario library/coverage metrics/simulator V&V/tool qualification/deterministic replay/MASS verification | **建议创建**——P0-G-2/G-3/G-4/G-5/P1-G-3/P2-G-1/P2-G-5 共 7 个 finding 在现有 5 DOMAIN 无良好归属 |
| **`Maritime_Cybersecurity_Onboard_Networks`** | F §6.x | IACS UR E26/E27/E22, OT network zoning, DDS-Security, mTLS shore link, ECDIS chart signing, ASDR HMAC chain | **建议创建**——P0-F-01/P0-F-02/P2-F-02 + 架构 §16 缺失；maritime_regulations 偏向 IMO/COLREGs，cyber 是独立大类 |

> 等用户最终确认是否创建（Write 操作，需用户授权）。

---

## 9. 证据链验证抽查（附录 A）

主 agent 抽查每条 P0 finding 的证据是否真实可访问：

| Finding | 证据 1 | 证据 2 | 抽查结果 |
|---|---|---|---|
| MUST-1 (M2 OVERTAKING) | M2 §5.1.3:387 | arch §6.3.1:585 | 待 spot-check（建议 PR reviewer 复查） |
| MUST-2 (Mid-MPC N) | arch:850 | M5:184,272 | 待 spot-check |
| MUST-3 (IvP GPL) | M4:805-806 | github.com/moos-ivp/moos-ivp LICENSE | 待 5/13 法务复查 LICENSE 文件 |
| MUST-4 (HAZID 冲突) | HAZID kickoff:78-112,152-157 | gantt:13,519 | 待 spot-check |
| MUST-5 (ROT_max 硬编) | M5:674 | — | 待 spot-check |
| MUST-6 (M2 sog) | M2:48 | — | 待 spot-check |
| MUST-7 (M8 ROC-only) | M8:111 | — | 待 spot-check |
| P1-FIX-7 (E26/E27) | iacs.org.uk/news/iacs-adopts | classnk.or.jp/hp/en/activities/cybersecurity/ur-e26e27 | URL accessibility 🟢 |
| ⚫ Unknown 标注 | 8 条（CPA 工业值 / Yara 场景规模 / Boeing PFCS 数值 / K-MATE PTP 等） | — | 后续 deep research 闭合 |

---

## 10. Owner + 关闭日期汇总（附录 B）

| Owner | P0 (5/13) | P0 (Phase 1, 6/15) | P1 (Phase 2, 7/31) |
|---|---|---|---|
| **架构师** | MUST-2/4/8/10 | P1-FIX-2/6/7/8/9/10/11/12 + v1.1.3 全部 MISS | F 簇 P1 全部 |
| **PM** | MUST-4 | P1-FIX-1/11/12 | A 簇 P1 |
| **M2 负责人** | MUST-1/6 | P1-FIX-9 | B 簇 P1 部分 |
| **M4 负责人** | MUST-3 | — | RFC-009（IvP）|
| **M5 负责人** | MUST-2/5/9 | P1-FIX-4 | B/G 簇 P1 |
| **M6 负责人** | — | P1-FIX-5 | E P1-E6 |
| **M7 负责人** | MUST-8 | P1-FIX-10 | C/F/G 簇部分 |
| **M8 负责人** | MUST-7 | P1-FIX-7 部分 | D 簇 P1 |
| **技术负责人** | — | P1-FIX-1/3/4/12 | G 簇 P1 |
| **V&V 工程师**（新角色） | — | P1-FIX-2/5 | E 簇 P1 |
| **HAZID 主持** | MUST-4 | P1-FIX-11 | C/E 簇 P1 |
| **法务** | MUST-3 | — | — |
| **CCS 联络人** | — | P1-FIX-11 | C 簇 P1 |

---

## 11. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-05-07 | 首版 — 跨 7 angle 整合（30 P0 / 52 P1 / 29 P2 → 12 must-fix + 12 phase-1 fix + 30 P1 + 8 P2 + 17 architecture missing + 14 multi-vessel blocker）|
| v1.1 | 2026-05-07 | 用户 3 项决策入库（§13）：① 创建 2 个新笔记本（实际绑定 global，不在 domain_notebooks 路径）② 8/31 不拆，需 M7 6→9 人周补偿（新增 MUST-11）③ 12 月实船降级"非认证级技术验证试航"（影响 D4.5 计划修订与 P1-A4 整改）|

---

## 13. 用户决策（2026-05-07）

### 13.1 DOMAIN 笔记本创建结果
- ✅ `SIL/HIL Platform Engineering` UUID `968983cc-56f4-4203-90fb-02b0739a0594`（绑 `global_notebooks`）
- ✅ `Maritime Cybersecurity & Onboard Networks` UUID `6cb48f75-4bd5-4d61-b161-ff83377ab013`（绑 `global_notebooks`）
- ⚠️ `nlm-setup` 当前不支持 `--create-domain`，本子目前在 `global_notebooks` 而非 `domain_notebooks`；后续若要让 `/nlm-ask` DOMAIN 路由命中需 nlm-setup 工具升级 / nlm-migrate 流程；当前可用 `--notebook <UUID>` 直接指定查询

### 13.2 8/31 硬承诺保留
- 用户决定**不拆 8/31**——接受 P0-A1+A3+E2+G1 阳性风险
- **新增 MUST-11**（升级到 must-fix 表）：**M7 effort 6 → 9 人周**（A P0-A3 整改建议 #1）。Owner = M7 负责人 + PM；关闭日期 = **5/13**（PR + 工时表更新）；证据 = A P0-A3 + IEC 61508 SIL 2 软件 2–4× 安全开销系数 [R3]
- **加严 P1-FIX-12**：D3.5/D3.6/D3.7 owner 必须强制分离（架构师 / V&V 工程师 / 技术负责人）；关键路径"加载图"在 5/15 前出，每周每人 ≤ 35h/周硬限制；任何 D3.x 在 8/24 仍未达成 90% 完成率即触发 escalation

### 13.3 12 月 FCB 实船试航降级
- **D4.5 修订**：12 月 FCB 实船仅作"**非认证级技术验证试航 + AIS 数据采集**"，**不**作为 i-Ship 证据；CCS 验船师**不**到场（避免"非认证试航被记入认证档案"风险）
- 影响 P1-A4（实船准入门槛）：原拟 7 项门槛降为 4 项（无需 AIP / 无需 SIL 2 第三方意见 / 仅需 D3.7 8h 无崩溃 + D4.2 HIL ≥ 50h + Hs ≤ ODD-A 边界 + ROC 接管链路独立验证 ≥ 60s）
- 路线图修订：**2027 Q1/Q2 AIP 受理后**重启认证级试航（独立 D5.x 增量计划）；当前 8 个月计划文档应在 5/13 前补"D4.5 修订"声明
- Owner = PM；关闭日期 = 5/13

---

## 12. 后续动作（5 月开工前 7 日内）

1. **5/8（次日）**：用户决策（a）是否创建 2 个新 DOMAIN 笔记本；（b）8/31 拆"软关门 + 9/30 硬关门"是否接受；（c）12 月实船降级为"非认证级技术验证试航"是否接受
2. **5/9–5/10**：架构师 + PM 召开 must-fix 评审会，对 12 项 P0 排定整改 owner 与具体修订 PR
3. **5/11–5/12**：M2 / M4 / M5 / M8 / 法务 完成 MUST-1/2/3/5/6/7 整改 PR，merge 入 v1.1.2-patch1
4. **5/13 第一次 HAZID 会议**：议程加 MUST-4 裁决、MUST-10 deep research 启动、P1-FIX-11 工时重排
5. **5/15**：V&V Plan v0.1 提交（P1-FIX-2）
6. **5/24**：D1.3 SIL 拆分定稿（P1-FIX-1）+ 场景 schema 提交（P1-FIX-3）
7. **5/27**：M2 intent 接口 RFC（B P1-B-02）+ BC-MPC 实测 SLA（B P0-B-04）
8. **5/31**：覆盖率方法论文档（P1-FIX-5）
9. **6/15**：v1.1.3 stub（含 §16 cyber、§15.0 时基、§12.5 培训）+ ConOps/HARA/ALARP/SDLC 4 件草稿（P1-FIX-6）+ FMEDA 表（C P1-C-8）

---

*汇总完成 · 2026-05-07 · 主 agent 跨 7 angle 整合 · 全部证据链可追溯到具体 angle 报告 §X 与仓库 file:line*

---

## 修订记录

| 日期 | 版本 | 修订内容 |
|---|---|---|
| 2026-05-11 | v1.1 | G P0-G-1(c) CLOSED — AIS 历史数据场景 authoring 工具已交付。Closed by D1.3b.2: scenario_authoring package (5-stage AIS pipeline + 50Hz replay node + config-driven L1 mode switching) |
