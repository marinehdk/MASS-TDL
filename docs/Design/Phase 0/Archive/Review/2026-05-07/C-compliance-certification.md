# C 评审报告 — Compliance & Certification Evidence（合规与认证证据）

| 字段 | 值 |
|---|---|
| Reviewer Persona | CCS 验船师 + IEC 61508 / ISO 21448 功能安全评估师（DNV/ABS 交叉视角） |
| Scope | 架构 v1.1.2（§11/§14/§15）、M1/M7 详细设计、HAZID RUN-001 包、8 月开发计划 v2.0 中的认证证据交付物（D2.7 / D3.3 / D3.5–3.7 / D4.5）|
| 总体判断 | **🔴 红（带条件可走）** — 架构方向与映射矩阵正确，但 11 月 AIP 提交的 4 类核心证据（ConOps、ALARP、HARA/Hazard Log、SDLC 工件）尚未在仓库中以独立交付物形式存在；HAZID 工时与场景规模均显著低于 Yara Birkeland 量级；网络安全（IACS UR E26/E27）的 L3 边界承担方式未澄清 |
| 评审基线 | 架构 v1.1.2 / Plan v2.0 / M1 + M7 详细设计草稿 |

---

## 1. Executive Summary（≤ 200 字）

L3 在「映射」层面是合规的：DNV-CG-0264 §4 九子功能 100% 映射到 M1–M8（架构 §14.2），i-Ship 三阶段递进路径清晰（§14.3），Doer-Checker 双轨 + ADR-001/002/003 修订已闭合 5 条 P0。但作为 11 月 CCS i-Ship AIP 的提交包，下述四类证据仍只存在于"清单格式"而非"可审文档"：(1) ConOps、(2) ALARP demonstration、(3) Hazard Log（HARA/FMEA 实例化）、(4) SDLC 工件（IEC 61508-3 §7 全表）。
HAZID RUN-001 13 周 × 8 干系人 ≈ 208 工时，对 132 个 [TBD-HAZID] 参数 + Catastrophic 级 MRM 阈值偏少；DNV-RP-0671 / IACS PT-MASS 范式下相当规模通常需 400–600 工时（⚫ Unknown — 文献未在仓库内）。SIL 1000+ × 98% 通过对 Type Approval 是必要不充分，需补 SOTIF triggering condition 库与可重放回归基线。Doer-Checker "100×" 简化主张不可被外审稽核，须改为 LOC + 圈复杂度 + 库依赖矩阵三可量化指标。网络安全责任 L3↔Z-TOP 的边界书面无 RFC，存在认证盲点。

---

## 2. P0 阻断项（≤ 5 条）

### P0-C-1 ConOps、ALARP、Hazard Log、SDLC 工件 4 类核心 AIP 证据缺独立可审版本

- **Finding**：架构 §14.4 列出 ConOps / ODD-Spec / HARA / SIL 评估 / 软件安全开发计划 / COLREGs 覆盖率 / HIL 等 10 类证据，但仓库内目前仅 ODD-Spec 在架构 §3 + M1 详细设计中嵌入；ConOps（D2.7）和 ALARP demonstration **未单独成文**；HARA / FMEA 在架构 §14 提及但只有 SIL 分配表（§11.4 共 4 行），缺逐危险源 → SIF → SIL → 缓解的完整映射链；IEC 62443-4-1 SDL 在 §14.4 仅一行（"软件安全开发计划"），无开发流程文件、配置管理、变更控制证据。
- **影响**：CCS 智能船舶规范 § AIP 阶段要求 "ConOps + ODD-Spec + HARA 初版"（架构 §14.3 自陈），三件齐备方可提交。当前 1/3 齐备，11 月提交节点存高失败风险。i-Ship (Nx, Ri/Ai) 进阶标志要求白盒可审计——HARA 链路断裂等于"不可审计"。
- **证据**：
  - `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:1359-1372`（§14.4 证据清单只是"应交"列表，无指针指向已存在文件）
  - `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md:108-111`（明确"v1.0 的 ODD-Spec 论证链断裂（F-P1-D7-028），不能直接提交"——v1.1.2 已修订 ODD-Spec 章节，但 ConOps / HARA 未补）
  - `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md:1023`（D4.5 把 10 类文件全部压在 11 月单一交付里）
- **整改建议**：
  1. 5 月开工首周拆 D2.7（5/1–6/15 期）独立产出 `docs/Design/ConOps/01-conops.md` + `docs/Design/Safety/01-hara.md` + `docs/Design/Safety/02-alarp-demo.md` + `docs/Design/SDLC/01-iec61508-3-plan.md` 4 件；不要等 D4.5
  2. HARA 必须逐危险源建表（≥ 30 项危险源，对应 ADR-001/002/003 + ODD 4 子域 × 3 健康态 = 12 模式 × 平均危险数）
  3. ALARP 论证须有"风险接受准则 + 残余风险列表 + 不可降低风险的代偿措施"三段式——架构内目前无此结构
- **置信度**：🟢（架构 §14.3 自陈 + audit/00-executive-summary.md:111 的 v1.0 论证链断裂证据 + IEC 61508-1 §7.4.2 必要工件清单为常识 A 级）

### P0-C-2 HAZID RUN-001 工时与覆盖范围量级偏低，难以作为 i-Ship 进阶标志的 HAZID 证据

- **Finding**：RUN-001 设计为 12–13 周 × 8 干系人 × ~2 h/会议 × 13 次会议 ≈ **208 工时**，覆盖 132 个 [TBD-HAZID] 参数（含 8 项 RPN ≥ 9 阻塞参数）。CLAUDE.md §5 明确 i-Ship (Nx, Ri/Ai) 强制白盒可审计 + IEC 61508 SIL 2 + ISO 21448 SOTIF。Yara Birkeland（DNV 路径，最强工业 analog）的 HAZID 据公开资料持续 2 年以上，每轮覆盖范围更广（⚫ — 未在仓库内查到具体工时；NLM safety_verification 笔记本未直接命中）。
- **影响**：
  - 132 / 208 ≈ **每参数仅 1.6 工时**（包含会议组织 + 数据采集 + 多方审核），对 Catastrophic 级 MRM-001/002（速度 4 kn / 减速 30 s）严重不足
  - HAZID 输出 "v1.1.3 patch 计划 2 人周"（D3.5）说明工作组并未承担参数论证而是"接收校准结果"，HAZID 实质退化为 Review meeting
- **证据**：
  - `docs/Design/HAZID/RUN-001-kickoff.md:77-112`（12 周时间表 — 周 1–4 review，周 5–8 试航数据，周 9–12 评审/闭环）
  - `docs/Design/HAZID/RUN-001-kickoff.md:120-135`（参数清单 RPN ≥ 9 共 8 项含 4 个 RPN=12 阻塞项）
  - `docs/Design/HAZID/02-mrm-parameters.md:10-30`（MRM-01 RPN 12 — Catastrophic）
- **整改建议**：
  1. 将"HAZID 第 1–4 周"由 review 改为 **per-参数 worksheet 推进**（每参数 ≥ 4 工时：触发条件 + 失效模式 + 缓解 + 残余风险）
  2. CCS 验船师不应只在周 4/8/10 出席，应持续在线评审周报（合规审计要求"持续介入"非"段位检查"）
  3. RUN-001 范围与 RUN-002/003 的拖船/渡船分离合理，但 FCB 单船 HAZID 须包含 ≥ 60 危险源（CCS《智能船舶规范》示例量级，⚫ — 仓库内未引用具体条款编号）
- **置信度**：🟡（方法论判断 A 级；具体工时基准 ⚫ — 未引用 DNV-RP-0671 或 IACS PT-MASS 文献条款；NLM safety_verification 64 sources 未命中具体数值）

### P0-C-3 Doer-Checker "100×" 简化主张缺可量化、可外审验证

- **Finding**：架构决策四（§2.5）+ §11.7 + M7 §1.4 反复声称"M7 比 Doer 简单 100×"，audit/00-executive-summary.md:31 已识别 F-P0-D1-008 — "[R12] / IEC 61508 / DO-178C 均无量化要求"。当前 v1.1.2 仍保留这一定性主张；M7 详细设计仅提供"禁用列表 / 允许列表 / CI grep 检查头文件"作为独立性证据（M7 §10.3 / Plan D3.3）。
- **影响**：
  - CCS / DNV 外审无法用一行 grep 验证"100× 简化"——验船师会要求等效工件（LOC 比、圈复杂度比、依赖图、独立失效模式分析）
  - IEC 61508-3 §7.4.4 独立性要求覆盖：实现路径、人员、工具链、共因失效分析（CCF）。当前仅"代码独立"达成，未见 CCF 分析、人员独立性、版本控制路径独立证据
  - PATH-S 仅约束第三方库 + 头文件 + colcon package + 进程独立 + spdlog 实例独立（Plan D3.3:391-398），未约束："开发人员、需求/设计 review 流程、测试用例库"独立——这是 IEC 61508-3 Annex F "diversity" 的硬性条目
- **证据**：
  - `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:171-180` 决策四原文 / `:1011-1040` §11.7 含 v1.1.1 F-NEW-002 关闭说明
  - `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md:31`（F-P0-D1-008 原文）
  - `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md:1554-1562`（CI 仅 grep 头文件）
- **整改建议**：
  1. 将"100×"改为 **三可量化指标矩阵**：(a) LOC 比 ≥ 50:1（Boeing 777 PFCS Monitor 量级，⚫ 未在仓库引用具体文献），(b) McCabe 圈复杂度均值比 ≥ 30:1，(c) 第三方依赖交集为空集（已在 Plan D3.3 中部分覆盖）
  2. 补 IEC 61508-3 §7.4.4 全维度独立矩阵：代码 / 库 / 人员 / 工具链 / 测试库 / Review 路径 / CCF — 在 `docs/Design/Safety/03-doer-checker-independence.md` 立卷
  3. CI 检查脚本 (`check-doer-checker-independence.sh`) 须输出可归档的 SBOM + 独立性证书（HTML/PDF）作为每次 build 工件
- **置信度**：🟢（audit 已识别 + IEC 61508-3 §7.4.4 是 A 级硬条款，常识知识，未在仓库内引用条款原文则降一级 🟡）

---

## 3. P1 重要风险（≤ 8 条）

### P1-C-1 SOTIF 假设违反 6 类完整度有 1 项数据矛盾

架构 §11.3 + M7 §5.3.1 列 6 项假设（含 Checker 否决率），但 HAZID 工作包 `03-sotif-thresholds.md:11-16` 只列 4 项（漏 SOTIF-005 通信链路 + 区分两类不一致）。三处文档不一致 → CCS 审计直接命中。证据：`docs/Design/HAZID/03-sotif-thresholds.md:11-16` vs `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md:418-584` vs `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:956-963`。整改：SOTIF 阈值表统一为 6 行 + 索引到 M7 §5.3.1。🟢

### P1-C-2 ISO 21448:2022 触发条件分类的"完整性论证"缺失

ISO 21448:2022 §6.1 要求触发条件按"已知-安全 / 已知-不安全 / 未知-不安全"三类穷举；本设计 6 项假设是"按工程经验枚举"，并未引用 ISO 21448 §6 所要求的 SOTIF area 分类法。RFC-003 enum-only veto 限定为 6 个 VetoReasonClass，覆盖率自陈 ≥ 95%（RFC-003 §4 "覆盖 95%+"），但无支撑数据。证据：`docs/Design/Cross-Team Alignment/RFC-003-M7-Checker-Coordination.md:109`、架构 §11.3。整改：在 HAZID 中专设 1 次会议做 SOTIF area 完整性 mapping；OTHER enum 出现率作为 SOTIF triggering condition KPI 监控。🟡

### P1-C-3 IMO MASS Code 4-level 模式指示未在 L3 出口对接

CLAUDE.md §2 标注 4-level 模式指示在 ASDR Z-BOTTOM。L3 输出 SAT 数据 + ASDR_RecordMsg，但 M8 §12 / 架构 §15 未声明 L3 是否承担"模式指示器"上层数据贡献（ROC 操作员看到的"当前 D2/D3/D4"+"OOD/MRC"）。MSC 110/111 公布以后会要求"模式指示符"在桥楼可见。证据：架构 `:1099-1188`（M8 章节未提 4-level 字符）、CLAUDE.md §2。整改：M8 SAT-1 须显式输出 IMO 4-level 模式字符串，在 RFC-004 ASDR 接口里加 `imo_mode_level` 字段。🟡

### P1-C-4 TMR ≥ 60 s 锚点单一文献，未挂"使用条件"

架构 §3.4 与 audit/00-executive-summary.md:127 用 [R4] Veitch 2024 RCT 作为 TMR ≥ 60 s 唯一锚点。Veitch RCT 是被试在 ROC 模拟器内的反应时间研究（实船 ROC 工作环境差异显著）。整改：把 60 s 标为"实验下限"，要求 HAZID 周 7（7/1）的 NASA TLX + 实船 ROC 实验给出"FCB 实景下的 TMR 95% 分位"作为校准目标，并和 Veitch 60 s 互验。🟢（基础引用 A 级，外推条件待验）

### P1-C-5 IACS UR E26/E27 在 L3 边界的承担方式无 RFC 文档

CLAUDE.md §2 / 架构 §14.4 / 8 月计划 D3.5 都提到 E26/E27，但 L3 内部承担与 Z-TOP / Cybersec 团队边界没有 RFC（6 RFC 全部对其它接口）。E27 含 30 项基础能力（ASDR 哈希链、通信加密、事件日志等）；M8 §12 仅有"SHA-256 ASDR 签名"一处对应（计划 D3.4:425）。整改：补 RFC-007 L3↔Z-TOP/Cybersec 接口（接管 E27 中哪些条目由 L3 承担）；至少要明确 ASDR_RecordMsg 完整性、M8 ↔ ROC 通信加密由谁实现。🟡

### P1-C-6 SIL 1000+ 场景作为 Type Approval 证据规模偏小

架构 §14.4 + 8 月计划 D3.6 给定"≥ 1000 场景 ≥ 98% 通过"。Yara Birkeland / Kongsberg K-MATE 的 type-approval 证据据公开资料量级在 10⁴–10⁵ 场景（⚫ 未在仓库内查证具体数字 / 笔记本未命中）。98% 通过率 + 1000 场景 → 95% CI 下通过率 [97.0%, 98.7%]；CCS 审计若问"剩余 2% 未通过场景的根因 + 整改"则需要单独的失败场景档案。证据：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md:455-471`。整改：Phase 4 HIL 加入"Failure 场景库 + 根因表"作为 D4.5 强制工件；规模目标设 5000+ 场景。🟡（数量级判断 🟡 — 因 Yara Birkeland 数字未在仓库证实）

### P1-C-7 i-Ship 三阶段路径与 11 月 AIP 节点冲突

架构 §14.3 写"阶段一 AIP / 阶段二 i-Ship (I, N, R1) / 阶段三 i-Ship (I, Nx, Ri)"，分别绑 M1/M2/M4 完成节点。8 月计划把所有 10 类文档的 AIP 提交压到 D4.5（11 月）。但 §14.3 阶段二要"系统架构 + 安全分析 + SIL 评估 + 仿真测试"全套——这本应在阶段一之后再做。两者并不一致。证据：架构 `:1341-1357` vs 计划 `:1023`。整改：选择 (a) 把 11 月节点降级为"AIP only"（不含阶段二材料），(b) 或将阶段一/二合并并在文档说明合并的 CCS 审批认可。🟡

### P1-C-8 SIL 2 SFF/HFT 矩阵未在 M1/M7 实例化

架构 §11.4 给"SIL 2 单通道 HFT=0 + DC ≥ 90% + SFF ≥ 60% (Type A) / ≥ 70% (Type B)"通用要求。M7 详细设计 §5.2.2 给"DC ≥ 90% (Type A) / 70% (Type B)"覆盖率分类，但**没有 FMEDA 表实例化**（每元件失效模式 → 安全 vs 危险 → 已检测 vs 未检测 → SFF 计算）。M1 §5 / §6 同样缺。CCS / DNV 审计 SIL 2 必须看 FMEDA。证据：M7 `:410-414`、架构 `:973-979`。整改：每模块在详细设计 §11 加 FMEDA 表（≥ 20 失效模式 / 表）。🟢

---

## 4. P2 改进建议（≤ 5 条）

- **P2-C-1**：架构 §14.2 DNV-CG-0264 子功能映射表缺**版本号绑定**（"2025.01 现行版"在头部出现一次）。建议在矩阵表内嵌入条款级引用（§4.2 / §4.3 ...）以便审计追溯。证据：架构 `:1325-1339`。🟢
- **P2-C-2**：M1 详细设计未读全（45 KB），但从 audit/05/06 看 ODD-Spec 论证链已修订；建议在 M1 §11 增"ODD-Spec → CCS § 引用映射表"两栏对齐。🟡
- **P2-C-3**：HAZID 主持人 / CCS 验船师 / DNV 验证官 8 个角色"待填"（RUN-001:14-23）— 5/13 第一次会议无可议题主持人将拖延认证证据起算时间。建议本周锁定。🟢
- **P2-C-4**：架构 §15.1 ASDR_RecordMsg 是 RFC-004 决议产物；CCS i-Ship 白盒可审计要求 ASDR 是核心证据。建议把 ASDR 完整性（哈希链、防篡改、时间戳源）作为独立 SDLC 评估项目，而非附属 M8 子功能。🟡
- **P2-C-5**：MRM-04 抛锚（架构 §11.6 + HAZID 02-mrm-parameters.md）在 D4 全自主下若锚位预选失败的责任归属未定；与 Reflex Arc / 硬件 Override 的优先级仲裁需在 RFC-005 中显式画一张优先图。🟡

---

## 5. 架构 / 模块缺失项

- **缺**：`docs/Design/ConOps/` 目录（CCS AIP 头号必需件）
- **缺**：`docs/Design/Safety/HARA/` 目录（HARA 实例化 → CCS § + IEC 61508-1 §7.4 必需）
- **缺**：`docs/Design/Safety/ALARP/` ALARP 风险接受准则 + 残余风险表（CCS i-Ship Ri/Ai 必需）
- **缺**：`docs/Design/SDLC/` 软件安全开发计划（IEC 61508-3 §7 全表 / IEC 62443-4-1 SDL 配置管理 + 工具链鉴定）
- **缺**：`docs/Design/Cybersecurity/` IACS UR E26/E27 L3 责任划分 + 渗透测试 plan（计划 D3.5 提到，文档不在仓库）
- **缺**：FMEDA 表（M1 / M7 详细设计 § 11 至少各一）
- **缺**：RFC-007 L3↔Z-TOP/Cybersec 接口

---

## 6. 调研引用清单

仓库文件（accessed via Read）：
- `docs/Design/Review/2026-05-07/00-review-design.md:62-71`（证据链铁律）
- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md:118-180`（决策一/四）、`:906-1097`（§11 M7 全章）、`:1319-1372`（§14 CCS 路径）、`:1376-1398`（§15 接口 IDL 起始）
- `docs/Design/Architecture Design/audit/2026-04-30/00-executive-summary.md` 全文
- `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md:1-110, 280-584, 1540-1668`
- `docs/Design/HAZID/RUN-001-kickoff.md` 全文
- `docs/Design/HAZID/02-mrm-parameters.md:1-40`
- `docs/Design/HAZID/03-sotif-thresholds.md:1-50`
- `docs/Design/Cross-Team Alignment/RFC-003-M7-Checker-Coordination.md` 全文
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md:380-500, 1023`

外部引用（架构内已挂的来源；本审未对外网新调研）：
- [R4] Veitch et al. 2024 — TMR ≥ 60 s
- [R5] IEC 61508-3:2010 §7.4.4 独立性 / §7.4.2 工件清单（仓库未引用条款原文 → 标 🟡）
- [R6] ISO 21448:2022 SOTIF 触发条件分类
- [R9] DNV-CG-0264 (2025.01) §4 / §9.3 监控独立性
- [R12] Jackson 2021 Certified Control（量化 100× 来源）

⚫ 未命中（**No notebook hit / not verified in repo**）：
- DNV-RP-0671 Autonomous and Remotely Operated Ships **典型 HAZID 工时 / scope 量级**（safety_verification 64 sources 未命中具体数值）
- Yara Birkeland / Kongsberg K-MATE 公开 SIL 场景规模 / type-approval 证据条数
- ABS Guide for Autonomous and Remote Control Functions 最新版本号 / 条款
- CCS《智能船舶规范》最新版条款编号与 AIP 强制证据列表

> **未验证 — 需 `/nlm-research --depth deep --add-sources` 补给 safety_verification + maritime_regulations 两笔记本**：本审 P0-C-2 / P1-C-6 数量级判断当前为 🟡 / ⚫，须以上述 4 项原始文献闭合。

### 6.x 建议新增 DOMAIN 笔记本

无需新增。现有 5 个 DOMAIN 充足；建议向 `safety_verification` 注入 DNV-RP-0671、ABS Autonomous Guide、Yara Birkeland CCS/DNV evidence、CCS 智能船舶规范 4 项 deep research 输出。

---

## 7. 行业基准对标（Industry Benchmark）

| Finding | 对标对象 | 现状 vs 对标 | 证据/差距 |
|---|---|---|---|
| P0-C-1 ConOps/HARA 缺 | Yara Birkeland（DNV/Kongsberg 路径，最强 analog）| Yara 项目 ConOps + HARA 在 AIP 阶段已成形可审单行（公开新闻稿）⚫ 仓库未存原件 | 须在 5 月开工首两周补 4 件独立文档 |
| P0-C-2 HAZID 工时 | DNV-RP-0671 / IACS PT-MASS HAZID 实务 | RUN-001 ≈ 208 工时；Yara 多年 HAZID 持续 ⚫；本设计每参数 1.6 工时 | 工时下限缺权威文献，建议 deep research 补 |
| P0-C-3 100× 简化 | Boeing 777 PFCS Monitor（架构 §11.7 类比）/ Airbus FBW [R4] | Boeing 用 LOC + 圈复杂度 + 处理器异构 三维度量化；本设计仅"代码独立 grep"一维 | 须把简化主张落到 LOC + 圈复杂度 + SBOM 三可量化矩阵 |
| P1-C-3 IMO 4-level | Kongsberg K-Bridge 显示 | K-Bridge 对外 4-level 模式字符已是 ASDR/桥楼 UI 标准字段 ⚫ | 须在 M8 SAT-1 + RFC-004 加字段 |
| P1-C-5 E26/E27 | ABB Ability / Wärtsilä Smartship 网安实践 | 业界普遍把 IACS UR E26/E27 30 项分包到 IT/OT 团队 ⚫ | 须 RFC-007 划界 |
| P1-C-6 SIL 1000+ | Kongsberg / DNV Open Simulation Platform | OSP 公开案例多在 10⁴ 量级 ⚫ | 1000 偏低，建 5000+ 目标 |

⚫ 标注 = "在本仓库 + safety_verification 笔记本未直接命中具体数字"；不构成幻觉，仅说明结论需 deep research 闭合。

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

按 CLAUDE.md §4 决策四 + 评审设计 §6 强制项审查：

| 编号 | 是否 FCB-only 隐含假设 | 影响范围 | 拖船/渡船失效场景 | 严重性 |
|---|---|---|---|---|
| MV-C-1 | **MRM-01 速度 4 kn / 减速 30 s（架构 §11.6 + HAZID 02-mrm-parameters.md）** | M5 / M7 / Capability Manifest | 拖船带缆作业 4 kn 仍可能拉断系缆；渡船靠泊状态下 4 kn 已是危险下限 | 🟡 P1（已在 §11.6 标 [HAZID 校准] 且每船型独立 — 决策四 4 已经有缓解，但 RUN-002 / RUN-003 各 4 周时间窗对船型差异是否够大需 PM 评估）|
| MV-C-2 | **MRM-04 抛锚水深 ≤ 30 m / 速度 ≤ 4 kn** | M7 §5.4 | 大型集装箱船吃水 14 m，30 m 水深近港；高速渡船 4 kn 已停 | 🟡 P1（同上：[HAZID 校准] 标记缓解）|
| MV-C-3 | **TMR ≥ 60 s 引用 [R4] Veitch 2024** | M1 / 架构 §3.4 | Veitch 实验是通用模拟器，与具体船型耦合弱，但 ROC 工作站 UI 差异（拖船 vs 渡船 vs 货船）会改变实证 TMR | 🟡 P2（不构成 FCB lock-in，但需在 RUN-002/003 重测）|
| MV-C-4 | **HAZID RUN-001 12 周完成 FCB**；RUN-002 拖船 / RUN-003 渡船仅各 4 周（kickoff §8） | RUN-002/003 计划 | 4 周对新船型 HARA + ALARP + 132 参数复校准过短；可能形成"FCB 路径过深、拖船/渡船证据浅"不对称 | **🔴 P1**（影响 i-Ship 多船型扩展认证可信度；建议 RUN-002/003 ≥ 6 周）|

**结论**：未发现"if vessel == FCB"硬编码（决策四 §13 Backseat Driver 范式架构层达成）。但 **HAZID 工作流程对 FCB 路径过深、拖船/渡船过浅**（MV-C-4）是 P1 级泛化风险，建议 PM + 架构师在 RUN-001 第 4 周完成时重新评估 RUN-002/003 时长。

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-05-07 | 首版 — Angle C 评审报告（CCS + IEC 61508 + ISO 21448 + IMO MASS + IACS）|
