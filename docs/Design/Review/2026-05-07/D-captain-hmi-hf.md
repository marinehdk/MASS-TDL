# D 角度评审报告 — 船长 / HMI / 人因（Captain · HMI · Human Factors）

| 属性 | 值 |
|---|---|
| Reviewer Persona | 资深远洋船长（≥15 年 watch-keeping + bridge command）+ 海事人因专家 |
| Scope | 架构 §12 (M8) + M8 详细设计 v1.0 + 8 月计划 v2.0 中 HMI/培训交付物 + 跨模块 SAT/ToR/TMR/CMM 引用 |
| 总体判断 | 🔴 不通过（5 P0 阻断；最严重为"船长培训 0 交付物" + "ToR 60s 单一阈值不分场景" + "船长视图心智模型未验证"） |
| 评审基线 | 架构 v1.1.2 / Plan v2.0 / 8 模块详细设计草稿 |

---

## 1. Executive Summary（≤ 200 字）

M8 在"协议合规层"做得不错——SAT-1/2/3 自适应触发（缓解透明度悖论）、"已知悉 SAT-1"交互验证（IMO meaningful intervention）、回切顺序化（防监控真空）三项设计有学术对照，证据指向明确。但**从船长真实工作流视角出发，本设计仍有 5 项 P0 级缺陷**：(1) 8 月计划全篇 0 个"船长/ROC 培训"交付物，违反 MASS Code Chapter 15「人因」要求；(2) ToR 60s 是 Veitch 2024 的"实验场景固定上限"而非场景自适应阈值——船长在睡舱/卫生间/吃饭时**走到桥楼物理时间已耗 30–45s**，留给 SA 恢复仅 15–30s；(3) "船上船长视图"在 v1.1.2/M8 详设中只有 1 段文字描述 + 1 张矩阵表，**无原型、无实船长访谈、无可用性测试**，不能宣称"符合资深船长使用习惯"；(4) 50 Hz UI 刷新率对船长有"屏幕粘眼"风险（航行中船长视线必须 70% 以上在窗外），需降级模式；(5) M8 设计 100% 假设 FCB ROC 中心化使用，对拖船/渡船的"on-board captain primary, ROC secondary"工作流不兼容。

---

## 2. P0 阻断项（5 条，必须 5 月开工前对齐）

### P0-D-01：8 月计划无任何船长 / ROC 培训交付物，违反 MASS Code Chapter 15

**Finding**：8 个月开发计划（v2.0）从 D1 到 D4 全部 21 个交付物中，**"训练/培训/competency"出现 0 次**（grep 结果中"训练"仅指"RL 对抗智能体训练"，与人员训练无关）。仅 D3.4 的 M8 子项提及"ROC/船长差异化视图"，但无配套的 (a) 船长培训大纲，(b) ROC 操作员资质矩阵，(c) 模拟器训练课程，(d) ToR 协议演练 SOP。

**影响**：
- 12 月实船试航无可用合规船员 → 无法上船 → **直接阻断试航 milestone**
- CCS i-Ship AIP 申请（2026-08–09）须提交「人员培训方案」（DMV-CG-0264 隐含要求）→ 无文件直接退审
- IMO MSC 110（2025-06）已确认 MASS Code Chapter 15 是核心未决议程；CCS 跟随 IMO 节奏，2026-Q3 必查训练证据
- 12 月试航若由"未受过 ToR 训练的常规船员"操作，**法律责任无归属**（IMO meaningful intervention 不成立）

**证据**：
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` 全文 grep `培训|training` = 0 处人员训练命中（line 68 是 RL 训练）
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` grep 同上 = 0 处人员训练命中
- IMO MASS Code Chapter 15 Human Element：高层训练条款已写入，详细 STCW-equivalent 待 HTW（2025-Q1 后续）+ 2027 STCW 修订一并落地（DNV 2025 HTW11 报告）
- Veitch & Alsos (2022) [R23] "From captain to button-presser"：未受 ROC 范式训练的传统船长在监督任务中绩效退化显著

**置信度**：🟢 High（IMO MSC 110 + DNV HTW11 + 现状文件 grep + Veitch 2022 多源一致）

**整改建议**：
1. **5 月开工前**：在 8 月计划新增交付物 D2.6（Phase 2 末）+ D3.5（Phase 3 末）+ D4.5（Phase 4 实船前）：
   - D2.6：船长 + ROC 操作员**胜任力矩阵草案**（基于 STCW 二副胜任力 + ROC 增项；引用 MDPI Apps. Sci. 2020 + Sustainability 2024 RO 胜任力研究）
   - D3.5：模拟器训练课程大纲（≥ 30 学时；含 ToR 演练 ≥ 10 次 + MRC 触发认知重建 + 透明度悖论案例）
   - D4.5：实船试航前**至少 2 名船长 + 2 名 ROC 操作员通过模拟器认证**作为试航准入门槛
2. **架构 v1.1.3**：§12.5 新增"人员资质 / 培训 / 演练"小节（≤ 1 页），引用 MASS Code Chapter 15 + 2027 STCW MASS amendments 路线
3. **Owner**：PM + M8 负责人 + 业主方船员事务部门（必须三方对齐）

---

### P0-D-02：ToR 60s 单一阈值，未考虑船长 SA 恢复物理时间（睡舱→桥楼 30–45s）

**Finding**：M8 详设 §5.3 表与架构 §3.4 均把 ToR 时窗硬编码为 60 s，依据 Veitch 2024 [R4]。但 **Veitch 2024 实验本身是"操作员已坐在 ROC 工作站前监督仿真器"的场景**，不是"操作员从睡眠/卫生间/餐厅醒来并跑到工作站"的场景。船长场景中，**物理移动 + SA 恢复**两段时间须分别建模：
- 睡舱→驾驶台：30–45 s（典型 OSV / FCB 桥楼布局）
- 上厕所→驾驶台：15–30 s
- 吃饭→驾驶台：30–60 s（视餐厅位置）
- SA 恢复（看屏 + 看窗 + 听 VHF + 形成意图）：Endsley SAGAT 平均 20–60 s（取决于场景复杂度）

**影响**：
- 60 s ToR 实际只剩 0–30 s 有效"决策时间"，远低于 Veitch n=32 实验的"20 s 困难、60 s 可接受"分界线
- Veitch 2024 实验明确报告：**5 vs 30 min 监督期 + 1 vs 3 船数 + 20 vs 60 s 时间窗 + DSS 有无**为四个独立因子（手动检索 ScienceDirect S0029801824005948）；当时间窗 = 20s + DSS 缺失 + 3 船时 handover 时间从 5s 飙到 15s — 这条曲线说明**60s 不是普适阈值，而是"实验场景上限"**
- 船上船长场景 = "passive monitoring 30+ min" + "中断在 sleep/break 状态" + "DSS 待 SA 恢复后才可读"，比 Veitch 实验的最坏组合更恶劣

**证据**：
- `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md:121-124, 352-354`（60s 硬编码 + tor_deadline_s）
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:251-261, 1174`（架构同样硬编码）
- Veitch et al. 2024 Ocean Engineering S0029801824005948（不是 117257；架构文档 [R4] 引用号疑误，详 P1-D-04）：实验为"已就位"操作员
- Endsley SA recovery 文献（待 NLM 补给到 maritime_human_factors）

**置信度**：🟡 Medium（Veitch 实验设计明确；船长场景物理时间是常识但**未做项目级测量**）

**整改建议**：
1. **5 月开工前**：架构 §3.4 + M8 详设 §5.3 把 `tor_deadline_s` 从单一常量改为 **场景自适应矩阵**：
   ```
   场景                    建议 tor_deadline_s
   ROC 已坐席监督          60 s（Veitch 2024 基线）
   船上船长 - 在驾驶台      30 s（无物理移动）
   船上船长 - 餐厅/卫生间   90 s（物理 30 + SA 30 + 决策 30）
   船上船长 - 睡舱           120 s（物理 45 + SA 45 + 决策 30）[HAZID 校准]
   ```
2. **HAZID RUN-001**：把"船长物理移动 + SA 恢复时间"列为必测参数（5/13 议程加项）
3. **接口**：`ToR_RequestMsg` 须新增 `assumed_operator_state`（IN_SEAT / OUT_OF_SEAT / SLEEPING）字段；M1 推 ToR 前据 Operator Presence Sensor（B/C 角度议题）填值
4. **Owner**：架构师 + M1 负责人 + M8 负责人

---

### P0-D-03："船上船长视图"零可用性证据 — 不能声称"符合资深船长使用习惯"

**Finding**：用户硬要求"符合资深船长使用习惯"，但 v1.1.2 + M8 详设对"船上船长视图"的规格仅有：
- 架构 §12.3 表（line 1145-1151）：3 行差异描述（"简化"、"直觉式可视化"、"高层摘要"）
- M8 详设附录 A（line 879-887）：5 行场景 × 角色矩阵
- **0 张原型截图、0 个船长访谈纪要、0 个可用性测试报告**

资深船长的实际心智模型（≥15 yr watch-keeping）：船长 70%+ 视线在窗外 + ARPA + 桥楼仪表（罗经/速度/舵角/RPM），**只在"信息不一致时"扫一眼电子海图**。本设计把船长当成"看 UI 屏幕的人"是 ROC 操作员心智模型的简单复制 —— 这是 Veitch & Alsos (2022) [R23] "From captain to button-presser" 论文最尖锐批判的反模式。

**影响**：
- 实船试航时船长**会拒绝使用** UI（航行习惯优先），SAT 数据无人看 → ToR 失效 → 无法满足 IMO meaningful intervention → 试航无效
- CCS 验船师必查"HMI 现场可用性证据"；空文件夹直接判 P0 退审
- 多船型扩展时，拖船船长（看主推 + 拖力 + 拖缆张力）和渡船船长（看靠泊距离 + 风压）的工作流差异**根本未识别**

**证据**：
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:1145-1151`
- `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md:879-887`
- 8 月计划 D3.4（line 425）：M8 4 周交付期内**无可用性测试任务**
- ABB Marine Pilot Vision（行业最强 HMI 对照）：每个航段更新都附 captain feedback round；本项目缺
- Veitch & Alsos 2022 [R23]：明确警告"按钮操作员退化"

**置信度**：🟢 High（项目文件 grep 0 命中 + ABB/Kongsberg 行业实践对比 + Veitch 2022 学术警告 多源一致）

**整改建议**：
1. **5 月开工前**：增加 D3.4.5（M8 内子任务，1.5 周）：船长访谈（≥3 名 FCB 船长 + 1 名拖船船长 + 1 名渡船船长）+ 半保真 Figma 原型 + 桌面可用性测试（n ≥ 5）
2. **架构 v1.1.3**：§12.3 新增"船长心智模型 ground truth"小节，列举船长**实际视线分布、扫视模式、注意优先级**（数据来自访谈），明确"M8 UI 是辅助而非主控"
3. **DoD 增项**：M8 验收须含"≥1 名资深船长签字的可用性认可声明"
4. **Owner**：M8 负责人 + 业主方船员代表 + 第三方人因咨询（建议）

---

### P0-D-04：M8 设计 100% 假设 ROC-centric，与拖船/渡船的 on-board primary 工作流不兼容（多船型 P0）

**Finding**：M8 详设处处假设 "ROC 操作员是主用户、船长是辅助"——
- §4.1 `active_role` 默认 `ROC_OPERATOR`（line 111）
- §6.2/6.3 SLA 全部以 ROC 工作站为目标（"网络传输 < 50 ms"、"WebSocket 50 Hz"）
- §8 角色矩阵：ROC 列写 5 项细节，船长列只 1 句简化描述
- §10.1 UI 框架"React/Vue + WebSocket"暗示"远程瘦客户端"

但拖船/渡船业务逻辑是**船长在驾驶台主导，ROC 是 backup**：
- 拖船：拖力、拖缆张力、被拖物姿态是船长本地决策核心，ROC 远程**无法替代**
- 渡船：靠泊距离、风压、岸基协调是高速本地决策（< 5 s 反应），ROC 网络延迟（典型 200–500 ms RTT）就够呛

**影响**：
- CLAUDE.md §4 决策四（"决策核心零船型常量"）虽然限定算法层；但 M8 是**人机接口层**，需要的是"用户角色零假设"
- 13 章扩展计划（line 1308-1313）说"决策核心修改 = 零"，但 **M8 角色架构是船型相关的**——拖船/渡船扩展时 M8 必须重写一遍 → 违反多船型扩展承诺

**证据**：
- M8 详设 line 111-112, 425（计划文件）；架构 §13.5 line 1308-1313
- Wärtsilä SmartMove / IntelliTug（拖船自主案例）：on-board operator 是主，ROC 是 backup（行业常识，待 NLM 补研究）
- Yara Birkeland（KSE 集装箱船）：从 manned → ROC-supervised → autonomous 是 3 阶段而非一次到位

**置信度**：🟡 Medium（行业范式公认，但本项目 M8 详设确实"100% 文字证据"显示 ROC 默认）

**整改建议**：
1. **架构 v1.1.3**：§12.3 重写为"对称双角色"——`active_role` 配置为 `PRIMARY_ON_BOARD | PRIMARY_ROC | DUAL_OBSERVATION`，**消息格式 / SLA / UI 渲染均同等优先级**
2. M8 详设 §6.2/6.3 SLA 拆双套：`UI_StateMsg_local`（船端，DDS 直连，< 20 ms）+ `UI_StateMsg_roc`（远程，50 Hz WebSocket，< 100 ms）
3. **Owner**：架构师 + M8 负责人；多船型 PM 验收

---

### P0-D-05：50 Hz UI 刷新率对船长是"屏幕粘眼"风险（人因 P0）

**Finding**：M8 详设硬要求 UI_StateMsg @ 50 Hz（line 80, 86-91, 815）。但**船长航行中 70%+ 视线在窗外**（基于 SOLAS V/22 视线要求 + 桥楼布置原则）；50 Hz 高刷新率 + 自动加粗/换色（line 290-294）是"驾驶辅助场景"逻辑（如汽车 ADAS HUD），不是"watch-keeping bridge"逻辑。

人因机制：
- Wickens "attentional capture"：高频闪烁/移动/换色会**强迫**捕获注意力；船长一旦"被屏幕吸住"，**离窗外目视瞭望就更远**
- SOTIF 反例：屏幕颗粒太多 → 船长形成"被动监视者"心智 → Veitch 2022 警告的"button-presser"退化加速

**影响**：
- 实船试航中船长会要求关掉/降级 UI → SAT-1 持续展示假设破裂 → "已知悉"按钮的 5 s 强制等待无意义
- 与 IMO COLREGs Rule 5（瞭望）潜在冲突

**证据**：
- M8 详设 line 80, 86-91, 815
- ABB Marine Pilot Vision：HMI 主屏更新典型 1–4 Hz（数据），动画限频；50 Hz 是 ADAS HUD 数量级（待 NLM 补对照）
- SOLAS V/22 桥楼可视范围要求（行业常识，待外部规范引用补全）

**置信度**：🟡 Medium（人因机制学术共识高 + ABB 对标显示 50 Hz 罕见，但**未做本项目实测**）

**整改建议**：
1. **5 月开工前**：M8 详设 §3.2 把 UI_StateMsg 拆 3 档：
   - `data_stream_50hz`：仅给 ROC 大屏 + ASDR；船长面板**不订阅**
   - `display_state_4hz`：船长面板默认；动画/换色限频 1 Hz
   - `alert_burst_event`：仅紧急事件触发，最多 1 个
2. M8 §3.2 + 9.4 新增 KPI："船长面板每分钟主动注意力捕获事件 ≤ 1 次"（HAZID 校准）
3. **Owner**：M8 负责人 + 人因咨询

---

## 3. P1 重要风险（≤ 8 条）

### P1-D-01：紧急 ToR（30 s 内）路径未设计 SA "fast-onboarding"

M8 状态机假设 ToR 始终有 60 s。但极端场景（M7 + Y-axis Reflex Arc 未捕获的）可能 30 s 内必须移交。当前设计无此路径。  
🟡 Medium · 整改：架构 §12.4 新增 "URGENT_TOR" 子分支（< 30 s）— 仅显示"采纳系统建议（Y/N）"+ 单一关键字段（CPA/方位），**禁用 SAT-2 全规则链**避免 cognitive freeze。
证据：M8 详设 §4.2 状态机仅 1 条 ToR 路径。

### P1-D-02：[R4] 引用号在架构中可能误用为 117257

DNV/CCS 审计会逐条核对。架构 §16 line 1658 写 "DOI: 10.1016/j.oceaneng.2024.117257"；但 ScienceDirect 检索 PII = S0029801824005948 对应卷 299，确切 article number 须再核（搜索结果未直接给出 117257）。可能 OK，但建议 v1.1.3 校对。  
🟡 Medium · 整改：v1.1.3 修订时核对 PII / DOI / 卷号 / 起止页（一次性查 ScienceDirect 即可）。

### P1-D-03：5 s "已知悉"按钮强制等待——cognitive workload vs 紧急性的 trade-off 未论证

5 s 防误触保护设计源自 F-NEW-004（IMO meaningful intervention）。但若操作员**已经监督 30 min 并清醒在线**，5 s 是反生产的延迟（Endsley：连续监督下 SA 已饱和，按钮就是确认）。  
🟡 Medium · 整改：架构 §12.4.1 加条件分支——若 `tor_reason == MANUAL_OPERATOR_INITIATED` 且 `monitoring_continuous_minutes > 5` 则 sat1_min_display 可降至 1 s。

### P1-D-04：操作员失能（卡或猝死）检测缺位

ToR 60 s 超时 → 自动 MRC 是兜底，但**操作员未失能仅是分心**的状态在 60 s 内无干预。Wärtsilä SmartMove / 船桥 watch alarm 系统通常有"30 s 无活动 → 闪灯 → 60 s 无活动 → 二副告警 → 90 s 无活动 → 船长告警"梯度。M8 缺。  
🟡 Medium · 整改：M8 详设 §4 新增 Vigilance_Watchdog 子机（参考 BNWAS / IMO MSC.282(86) Bridge Navigational Watch Alarm System 标准）。

### P1-D-05：HMI 在恶劣环境的可用性零证据

雾、夜航、5° 横摇、震动、咸雾下的 HMI（屏幕反光、触屏失效、戴手套）。M8 详设 §10.1 写"React/Vue WebSocket"，是干燥办公室技术栈。  
🟡 Medium · 整改：M8 §10.1 新增"环境适应"子节，引用 IEC 60945（船用电子设备环境）+ IMO Resolution A.694(17)。

### P1-D-06："ROC 工作站连接性"假设未做 fallback 设计

§4.1 line 142 `roe_workstation_connected` 仅作状态标记。链路断 → MRC 是兜底，但没有"船端独立 backup 显示"路径。Yara Birkeland 设计：船上独立平板 + 蜂鸣器，与 ROC 解耦。  
🟡 Medium · 整改：M8 §3.2 增 BackupChannel_DisplayMsg。

### P1-D-07：决策疲劳 / 透明度悖论 在常态多日航次下未做长期实证

SAT-2 自适应触发依据是 USAARL 2026-02 + NTNU 2024 的**短时实验**。多日航次（24/48/72 h）下 SAT-2 触发频率累积是否仍优于全展示？无证据。  
🟡 Medium · 整改：HAZID RUN-001 加测点；或注入 "SAT-2 sleep mode"（连续触发 N 次后操作员可关闭 1 h）。

### P1-D-08：M8 与 Y-axis Emergency Reflex Arc 的 UI 协议不存在

Reflex Arc 跳过 L3 直发 L5（CLAUDE.md 系统坐标）。这一刻 M8 应该如何告诉船长？详设 §3 / §6 无 Y-axis 相关消息。  
🟡 Medium · 整改：M8 §2.1 输入消息表新增 `ReflexArcActivatedMsg`（来自 Y-axis）；§4.2 状态机加 REFLEX_ACTIVE 子态（红色全屏 + 蜂鸣）。

---

## 4. P2 改进建议（5 条）

- **P2-D-01**：附录 B ASDR 事件类型枚举可压缩 20%（"sat3_priority_high" 与 "scenario_changed" 语义重叠）
- **P2-D-02**：§5.3 关键参数表 11 项中 6 项标 [HAZID 校准]——建议为每项加"暂代值 / 上限 / 下限"三列，避免 HAZID 期间编码用错
- **P2-D-03**：UI_StateMsg JSON 8–12 KB @ 50 Hz = 4–6 Mbps；建议 §10.1 加"船岸链路带宽 budget 评估"
- **P2-D-04**：船长视图与 ECDIS（电子海图显示信息系统）integration plan 缺；ECDIS 是船长 muscle memory，应同屏共渲（IHO S-100 / IEC 61174）
- **P2-D-05**：M8 详设 §11 [R4] 引用号与架构 §16 一并核

---

## 5. 架构 / 模块缺失项

1. **缺失：操作员能力 / 资格 / 训练 章节**（架构 v1.1.3 应新增 §12.5 或独立章）
2. **缺失：船长视图 ground truth 章节**（船长心智模型 / 视线分布 / 实船访谈）
3. **缺失：BNWAS / Watch Vigilance Watchdog 模块**（建议作为 M8 子组件，对齐 IMO MSC.282(86)）
4. **缺失：Y-axis Reflex Arc → M8 通告通路**
5. **缺失：船长面板 ↔ ECDIS 集成接口**（多船型适配必备）

---

## 6. 调研引用清单

### 6.1 仓库内文件
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` lines 28, 94, 124, 126, 163-167, 251-261, 1107-1160, 1164-1225, 1308-1313, 1658-1682
- `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md` lines 17-37, 80-101, 108-142, 168-197, 242-297, 348-362, 410-445, 458-516, 559-588, 802-851, 856-866, 879-887
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` lines 67-68, 124-139, 310-322, 416-430, 519-577, 619, 648
- `CLAUDE.md` §4 决策四 + §5 强制约束 (TMR ≥ 60s)

### 6.2 外部引用
- Veitch et al. (2024). "Human factor influences on supervisory control of remotely operated and autonomous vessels." *Ocean Engineering* 299. ScienceDirect PII: S0029801824005948. Dataset: NTNU DataverseNO doi:10.18710/WYFMMP. URL: https://www.sciencedirect.com/science/article/pii/S0029801824005948
- IMO MSC 110 (2025-06) MASS Code Chapter 15 Human Element draft：https://www.imo.org/en/mediacentre/hottopics/pages/autonomous-shipping.aspx
- DNV (2025) HTW 11 report (sub-committee on human element / training / watchkeeping)：https://www.dnv.com/news/2025/imo-sub-committee-on-human-element-training-and-watchkeeping-htw11/
- Veitch & Alsos (2022) "From captain to button-presser." *J. Phys. Conf. Ser.* 2311(1)
- MDPI Sustainability (2024) RO sustainable competencies: https://www.mdpi.com/2071-1050/16/12/4875
- MDPI Applied Sciences (2020) RO competence regulatory analysis: https://www.mdpi.com/2076-3417/10/23/8751

### 6.3 未验证 / 待 NLM 补给
- Veitch 2024 实测 takeover-time 分布 P50/P90 — 须从 NTNU DataverseNO 原始数据计算（建议 nlm-research --depth deep 入 maritime_human_factors）
- Endsley SAGAT SA-recovery 时间分布（船舶场景）
- ABB Marine Pilot Vision 实际刷新率与帧率 specs
- IEC 60945 / SOLAS V/22 桥楼可视性 / IMO MSC.282(86) BNWAS 条款（应入 maritime_regulations）
- Wickens "attentional capture" 在 vigilance 场景实证

### 6.4 建议新增 DOMAIN 笔记本
本评审中所有外部研究均归属现有 `maritime_human_factors` + `maritime_regulations`，**无须新增 DOMAIN**。但发现 `maritime_human_factors` 当前未在 NLM library 加载（API 返回 not found），建议主 agent 汇总阶段验证 NLM library 状态并触发 `/nlm-research --depth deep --add-sources` 把 Veitch 2024 全文 + dataset + IMO MASS Code + DNV HTW11 + Wickens vigilance 主要文献入库。

---

## 7. 行业基准对标（Industry Benchmark）

| 选型 / 指标 | 本设计 | 行业对照 | 来源 | Gap |
|---|---|---|---|---|
| ROC 接管时窗 | 60 s 单一阈值 | Kongsberg K-Bridge / NTNU SCL：场景自适应（30–120s） | Veitch 2024 + NTNU SCL site | 🔴 Gap：缺场景自适应 |
| HMI 主刷新率 | 50 Hz | ABB Marine Pilot Vision：1–4 Hz 数据，事件触发 | ABB 产品页（待 NLM 实证） | 🟡 Gap：50 Hz 偏高 |
| ROC vs 船长心智模型 | M8 假设差异化但未实证 | Yara Birkeland / Wärtsilä SmartMove：船端 primary, ROC backup 三阶段 | KSE Yara Birkeland 公告 | 🔴 Gap：单 ROC-centric |
| 接管验证 | 5 s 强制 + 主动点击 | Kongsberg：multi-modal（按钮 + 语音 + 视线追踪） | Kongsberg K-MATE specs（待补） | 🟡 Gap：单一通道 |
| 训练交付 | 0 项 | DNV/Kongsberg：模拟器 ≥40 h + 月度 ToR drill | DNV HTW11 + STCW MASS 草案 | 🔴 Gap：完全缺失 |
| BNWAS 等价机制 | 缺 | IMO MSC.282(86) 强制 | IMO 公约 | 🔴 Gap：合规缺口 |

🟢 行业对照充分；本设计在 SAT 自适应 + 已知悉按钮两点上**领先**多数现成 ROC 系统（这两项确属新工作）；但在多模态接管确认、训练计划、船长心智模型实证、HMI 刷新率工程实践上**显著落后**行业最佳实践。

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

本 angle 发现的 FCB-only 假设：

| 假设 | 文件:行 | 影响 | 等级 |
|---|---|---|---|
| `active_role: ROC_OPERATOR` 默认 | M8 详设:111 | 拖船/渡船的 on-board primary 工作流不兼容 | **P0**（见 P0-D-04）|
| 60 s ToR 单一时窗 | 架构:251, M8 详设:124 | FCB 高速场景与拖船低速场景差异巨大 | P1 |
| WebSocket 50 Hz 远程瘦客户端 | M8 详设:80, 815 | 渡船靠泊本地决策反应需 < 5 s 本地事件 | P1 |
| ROC 操作员 UI 信息密度（数字量化） | 架构:1147-1151 | 拖船船长需要"拖力/拖缆张力"专属字段，集装箱船船长需要"积载稳性" | P1 |
| 8 月计划 D3.4 仅训练 1 套 UI（FCB） | gantt:425 | 船型扩展时 M8 重写不止"水动力插件"层 | P1 |
| HMI 环境（无横摇/震动/咸雾设计） | M8 详设 §10.1 | FCB 高速横摇 ≠ 拖船低速作业 ≠ 渡船频繁靠泊震动 | P1 |

**结论**：M8 未达 CLAUDE.md §4 决策四"决策核心零船型常量"在 HMI 层的等价要求。整体而言，**M8 详设全部 12 章应在 v1.1.3 重新审视"角色 = ROC"假设**——这是一项与 §13 多船型扩展不兼容的根本性偏差，须 5 月开工前在架构 §12 / §13 做正式 ADR 修订。

---

**评审者签字**：D 角度独立评审 — 资深远洋船长 + 海事人因专家 persona
**完成日期**：2026-05-07
**下一步**：交主 agent 整合到 `00-consolidated-findings.md`；P0-D-01/02/03/04/05 须在 5 月开工前对齐
