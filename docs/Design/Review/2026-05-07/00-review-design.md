# MASS L3 TDL 多角度评审设计（5 月开工前）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-REVIEW-DESIGN-001 |
| 版本 | v0.2 |
| 创建日期 | 2026-05-07 |
| 评审基线 | 架构 v1.1.2 + 8 模块详细设计草稿 + 8 月开发计划 v2.0 |
| 评审目的 | 在 2026-05-06 严格按计划开工前，识别 P0/P1/P2 阻断项与整改清单 |
| 评审产出 | 7 份独立角度报告 + 1 份跨角度汇总（含证据链验证、行业 gap、多船型泛化阻塞清单）|

---

## 1. 评审目标与范围

### 1.1 评审目标
对以下三类制品进行独立多角度评审，输出 5 月开工前可消化的整改清单：
- (a) 架构设计稿：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2
- (b) 8 模块详细设计：`docs/Design/Detailed Design/M{1-8}/01-detailed-design.md`
- (c) 8 月开发计划：`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` v2.0

### 1.2 评审深度
- **重点**：Phase 1+2+3（2026-05–08）硬承诺的 7 个交付物 + HAZID + SIL 1000 场景
- **次要**：Phase 4（2026-09–12）展望仅评估"是否影响前 4 个月决策"
- **底线**：12 月实船试航仅做"准入门槛"层级提示

### 1.3 用户硬约束（不可让步）
1. **零幻觉**：每个决策/选型必须有可索引证据（文件:行号 / NLM source ID / URL+片段）
2. **行业基准对标**：参考龙头成功路线（§5 列表）
3. **多船型泛化**：决策核心零船型常量（CLAUDE.md §4 决策四，Backseat Driver 范式）

---

## 2. 评审拓扑

**7 个并行 subagent + 1 个汇总主 agent**。subagent 之间不互相读取、不串行依赖；冲突由主 agent 在汇总阶段裁决。

| Angle | Persona（subagent 扮演） | 主要锚点文件 |
|---|---|---|
| **A** 进度可行性 & 关键路径 | 资深 PM / 工程经理（船舶项目经验）| 8月计划 + 主计划 |
| **B** 算法 & 技术深度 | 船舶运动控制 + COLREGs 算法专家 | 架构 §6/§8/§9/§10 + M2/M4/M5/M6 详细设计 |
| **C** 合规 & 认证证据 | CCS 验船师 + 功能安全（IEC 61508 / ISO 21448）| 架构 §11/§14 + M1/M7 详细设计 + HAZID 包 |
| **D** 船长 / HMI / 人因 | 资深远洋船长 + 海事人因专家 | M8 详细设计 + 架构 §12 + ToR/SAT 接口 |
| **E** 测试 → SIL→HIL→实船 | 海事 V&V / 试航工程师 | 8月计划 Phase 2/3/4 + D2.5 / D3.6 / D3.7 |
| **F** 架构 & 模块完整性 | 系统架构师（DNV/ABS 验证官风格）| 架构 v1.1.2 全文 + 8 模块详细设计 |
| **G** SIL 系统构建（贯穿）| SIL 平台架构师（仿真物理 + 场景工程 + RL 测试）| 8月计划 D1.3 + Phase 2/3 SIL 验证 + L3 全模块 SIL 接入面 |

### 2.1 G angle 子面（贯穿全 L3，全部 7 子面强制覆盖）
- G1 SIL 架构本体（FCB 4-DOF MMG / AIS / ENC / 场景管理 / HMI / RL）
- G2 L3 全模块在 SIL 中的可观测性（SAT/ASDR/中间状态）
- G3 场景库工程（生成/版本化/回归/CCS 可追溯）
- G4 覆盖率方法论（Rule 全分支 / ODD 三轴 / SOTIF 触发条件）
- G5 环境与不确定性建模（风浪流 / 传感器噪声 / 目标意图）
- G6 SIL → HIL → 实船 一致性传递
- G7 SIL 自身 V&V（仿真器精度 / 积分稳定 / 确定性回放）

---

## 3. 证据链铁律（所有 angle 强制）

每条 finding / 整改建议必须满足：

```
finding/建议
  ├── 证据 ≥ 1 条：精确到 (a) repo 文件:行号 (b) NLM source ID (c) URL+片段
  ├── 置信度标注：🟢 ≥3 A/B 来源 ｜ 🟡 2 来源 ｜ 🔴 <2 ｜ ⚫ 搜不到（明说）
  └── 来源不存在 → 不许下结论；写"未验证 — 需 /nlm-research"
```

**禁用语**（出现即视为无效 finding）：
"我记得 / 一般来说 / 通常 / 业界 / 大概率 / 应该 / 似乎 / 想必 / 应当"

---

## 4. 调研权限矩阵

| 触发条件 | 工具 | 用法 |
|---|---|---|
| 简单事实/版本/语法/报错 | Web search | 直接查 |
| 概念/接口/规范条款，置信度 🟡 | `/nlm-ask` | 5 个领域笔记本（safety_verification / ship_maneuvering / maritime_human_factors / maritime_regulations / colav_algorithms）|
| 关键决策、选型、合规裁决 | `/nlm-research --depth deep --add-sources` | 默认入本地笔记本，不允许 `--no-add-sources` |
| 仓库内已有结论 | 直接引用，不重复调研 | 引用 v1.1.2 章节锚点或 audit/ |

---

## 5. 行业基准对标（cross-cutting，所有 angle 强制）

每条涉及"选型 / 指标 / 做法"的 finding **必须**举至少 1 个工业先例 + 证据：

| 对标对象 | 主要参考 | 优先 NLM 笔记本 |
|---|---|---|
| Kongsberg K-MATE / K-Bridge | 自主航行栈、船桥集成、ROC HMI | maritime_regulations + colav_algorithms |
| Wärtsilä SmartMove / IntelliTug | 拖船自主、靠泊辅助、避碰 | colav_algorithms |
| Rolls-Royce / Kongsberg Yara Birkeland | 全自主集成、CCS/DNV 路径 | safety_verification |
| ABB Ability Marine Pilot Vision | 态势感知融合、HMI 透明性 | maritime_human_factors |
| MIT MOOS-IvP | IvP 行为仲裁基线（M4 必比）| colav_algorithms |
| DNV Open Simulation Platform / Kongsberg HIL | SIL/HIL 工业实践（G angle 必比）| safety_verification |

⚫ Unknown 时显式标注 "**No industry analog found**"，不允许编造。

---

## 6. 多船型泛化检查（cross-cutting，所有 angle 强制）

每个 subagent 在审完 finding 后必须额外回答：

> "本 angle 下被审查的设计/计划，是否引入了**只对 FCB 成立**的隐含假设？如果是，列出该假设、影响范围（文件:行）、以及换到拖船/渡船/集装箱船会失效的具体场景。"

**锚定**：CLAUDE.md §4 顶层决策 4（Capability Manifest + PVA 适配 + 水动力插件 / Backseat Driver / 决策核心零船型常量）。任何 "if vessel == FCB" 类潜入即 **P0**。

---

## 7. 输出契约（每个 subagent 必填）

落盘到 `docs/Design/Review/2026-05-07/{A-G}-{topic}.md`：

```markdown
# {Angle} 评审报告 — {topic}

| Reviewer Persona | ... |
| Scope            | ... |
| 总体判断         | 🟢/🟡/🔴 |
| 评审基线         | 架构 v1.1.2 / Plan v2.0 / 模块详细设计草稿 |

## 1. Executive Summary（≤ 200 字）
## 2. P0 阻断项（≤ 5 条）
   每条：finding / 影响 / 证据 / 整改建议（含文件:章节）/ 置信度
## 3. P1 重要风险（≤ 8 条）
## 4. P2 改进建议（≤ 5 条）
## 5. 架构/模块缺失项（如发现）
## 6. 调研引用清单（NLM source IDs / Web URL / 仓库文件:行）
## 7. 行业基准对标（Industry Benchmark）
   涉及选型/指标/做法的 finding，列出对标对象 + 证据 + 差距
## 8. 多船型泛化检查（Multi-vessel Generalization）
   列出本 angle 发现的 FCB-only 隐含假设（含 P0 标注）
```

---

## 8. 汇总产出（主 agent 在 7 份就绪后）

`docs/Design/Review/2026-05-07/00-consolidated-findings.md`：

- 跨角度去重 + 合并同源 finding
- 按 P0/P1/P2 重排（不分角度）
- 每条整改 finding 标 owner（架构师 / M{N} 负责人 / SIL 负责人 / PM）+ 建议关闭日期（5 月开工前 / Phase 1 内 / Phase 2 内）
- "5 月开工前 must-fix" 短表
- "架构 v1.1.3 待补"清单（HAZID 不一定能盖到的项）
- **附录 A**：证据链验证表（主 agent 抽查每条 P0 的证据真实性）
- **附录 B**：行业基准 Gap 清单（按 angle 聚合）
- **附录 C**：多船型泛化阻塞项（所有 P0 级 FCB-only 假设）

---

## 9. 备选方案（已考虑并放弃）

- ❌ 单一 mega-agent 串行：失去独立性、互相妥协
- ❌ 3 阶段分波次：链路过长，5 月开工前来不及
- ✅ **7 并行 + 1 汇总**（当前方案）

---

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| subagent 越权改文件 | prompt 硬约束："只读 + 只写 docs/Design/Review/2026-05-07/" |
| 同一 finding 跨角度重复 | 汇总按"根因"去重 |
| 调研失控耗 token | NLM 优先；Web 限快查；deep research 仅关键决策 |
| 评审与 v1.1.2 已知 P3 冲突 | 主 agent 比对 audit/2026-04-30/99-followups.md 后再下笔 |
| subagent 编造证据 | §3 铁律 + §7 引用清单强制 + 汇总附录 A 抽查 |
| FCB-only 假设漏审 | §6 cross-cutting 强制每个 angle 单独回答 |

---

## 11. 派单与执行

派单方式：本会话内单条消息发起 7 个 Agent 并行 tool call（A/B/C/D/E/F/G）。
等待全部返回后，主 agent 整合产出 `00-consolidated-findings.md`。
全部文件 commit 一次性入库。

---

## 12. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-07 | 初稿（6 angle，无证据链铁律）|
| v0.2 | 2026-05-07 | 加入 G SIL 角度；加入用户三条硬约束（零幻觉/行业对标/多船型泛化）|
