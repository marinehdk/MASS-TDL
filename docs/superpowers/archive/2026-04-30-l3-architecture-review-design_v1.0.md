# L3 战术层架构评判与最优架构推荐 · 审计设计 Spec

| 字段 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-AUDIT-SPEC-001 |
| 版本 | v1.0 |
| 日期 | 2026-04-30 |
| 状态 | 待用户确认 → 进入 writing-plans |
| 主审对象 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（v1.0） |
| 主审者角色 | 资深自主航行软件架构师（≥20 年） |
| 输出基线 | docs/Design/Architecture Design/audit/2026-04-30/ 全套交付物 |

---

## 1. 工作目标

**核心目标**：交付一份**工程上可落地、有真实参考的最优 L3 战术层架构方案**。v1.0 是用户已迭代的"历史最优候选"，不是不可质疑的基线。

**最优的判定**：每条核心架构决策必须满足三件套——

- ≥3 个独立工业参考（Kongsberg / Wärtsilä / Avikus / Sea Machines / MUNIN / Yara Birkeland / ReVolt / Maersk Mayflower 等的可验证公开材料）
- ≥3 篇 A/B 级学术文献（A=官方/规范/一作论文；B=高星 issue/会议/权威博客；按全局 CLAUDE.md 来源分层）
- 与 CCS i-Ship + IMO MASS Code MSC 110/111 + DNV-CG-0264 的合规可映射

**输出三态结论**（必须在阶段 6 给出明确选择）：

- **A. v1.0 ≈ 最优** → 修订成 v1.1，仅产 patch list（字段级 diff）
- **B. v1.0 方向对但有 P0 阻塞** → 修订成 v1.1，含结构性变更 + ADR 增量
- **C. v1.0 方向需调整** → 提出 v2.0 替代架构，含工程证据与迁移路径

---

## 2. 范围

### 2.1 IN（审）

- **主审对象**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`（v1.0，1168 行，72 处引用，24 条参考）
- **跨文档证据**：`docs/Doc From Claude/`（v1.1/v1.2/patch-d、L3 裁决汇总、深度研究报告、notebooklm-sources）——只读，验证 v1.0 是否漏吸纳/静默推翻
- **跨层接口参照**：`docs/Init From Zulip/{L1 Mission, L2 Voyage Planner, L4 Guidance, L5 Control & Allocation, Multimodal Fusion, Deterministic Checker, ASDR, CyberSecurity, HW Spec, Sim}` 与 `‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html`——只读，作为 §15 接口契约的对外契约真值

### 2.2 OUT（不审）

- 不修改 v1.0 原文（只产 patch-list / v2-architecture）
- 不审 `docs/Init From SINAN/`（项目 CLAUDE.md 标定为外部输入只读）
- 不审 ROC 基础设施硬件、L1/L2/L4/L5 内部实现、Multimodal Fusion 内部算法（按 v1.0 §1.2 已声明边界外）
- 不在审计完成前修订项目 CLAUDE.md 的 §3/§4/§5（M1–M8 模块清单与"顶层不可让步项"是 v1.0 的断言，正在被审）

### 2.3 已落实的元数据决定

- **E1**（NotebookLM 笔记本）：固守当前 `.nlm/config.json` 的 6 个笔记本（local + 5 domain），从 0 开始填充。**不**碰 `8d64174a-...`（其他项目笔记本）
- **E2**（系统坐标系）：以 `docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html` 为 MASS 真值。**项目 CLAUDE.md §2 已在本 spec 写作前完成修订**（L1=任务层、L2=航路规划层、Multimodal Fusion 独立层、X/Y/Z 三轴）

---

## 3. 审计维度（D1–D9）

每个维度有"严重性上限"——决定单条 finding 可达到的最高 Severity。**所有 9 个维度都必扫**。

| ID | 维度 | 描述 | 上限 |
|---|---|---|---|
| **D1** | 顶层决策可证伪性 | 第 2 章 4 决策（ODD 组织原则 / 8 模块分解 / CMM-SAT / Doer-Checker）的论据是否站得住、替代方案分析是否充分 | **P0** |
| **D2** | 引用真实性 | 24 条 [Rx] 是否真实存在 / DOI 有效 / 原文是否真支持 v1.0 论断（防 Web Research 幻觉） | **P0** |
| **D3** | Doer-Checker 独立性 | M7 vs M1–M6 是否真"100×简单 + 实现路径独立"；与 v3.0 X-axis Deterministic Checker 的关系 | **P0** |
| **D4** | 接口契约完整性 | §15 总表 + 每模块"输入/输出/频率/置信度"四件套；与 Zulip 各层 + Multimodal Fusion + Checker 的契约一致性 | **P1** |
| **D5** | 时间尺度一致性 | 长 / 中 / 短时（0.1–1Hz / 1–4Hz / 10–50Hz）在模块间下游传递是否自洽；与 v3.0 Reflex Arc <500ms 是否兼容 | **P1** |
| **D6** | 降级路径完备性 | 每模块在 DEGRADED / CRITICAL / OUT-of-ODD 下的行为是否明确、可观测、可验证；MRC 触发是否可追溯 | **P1** |
| **D7** | 合规映射可审计性 | CCS i-Ship Nx/Ri/Ai / IMO MASS Code MSC 110/111 / IEC 61508 SIL 2 / ISO 21448 SOTIF / COLREGs Rule 5–19 / TMR ≥ 60s 的论据链 | **P1** |
| **D8** | 多船型不侵入 | "决策核心代码零船型常量"在 8 个模块设计里是否真做到；与 v3.0 Parameter Database / YAML 配置范式是否对齐。**注**：v1.0 决策四的核心承诺，做不到即动摇决策四 | **P1** |
| **D9** | 方案最优性（核心） | 顶层 4 决策与 8 模块在业界 / 学术中是否被工程验证；存在被工程验证的更优替代方案吗（v3.0 Kongsberg 5 模块 vs v1.0 8 模块即一例） | **P0** |

---

## 4. Severity 模型

- **P0 阻断**：动摇顶层决策、CCS/IMO 入级路径、或证伪关键引用的发现。**必须阻塞 v1.1 合并**；若多条则触发"三态结论 C"
- **P1 重要**：模块边界、接口契约、降级路径、时间尺度、合规映射有缺陷。**v1.1 应处理**
- **P2 改进**：表述不严谨、引用弱支撑（B/C 级但不致动摇论断）、术语不一致。**v1.1 建议处理**
- **P3 备忘**：风格、笔误、可选优化。**记入 followups 不阻塞合并**

---

## 5. Finding 模式（每条审计发现的 8 字段）

```yaml
- id: F-{P0|P1|P2|P3}-D{1..9}-{seq:03d}    # 例 F-P0-D1-001
  locator:
    file: docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md
    section: §2.4
    lines: 124-128
  what: 一句陈述（可被复述）
  why: |
    违反的原则——
    ▪ 架构原则 / 引用真实性 / CCS 条款 / 内部一致性 / 工程主流实践
  evidence:
    internal:                                # 文档内对照
      - "§N.M 行 X 与本节冲突，因 …"
    external:                                # NLM/Web 证据
      - source: "[作者 年份] / 工业系统名"
        confidence: 🟢/🟡/🔴/⚫
        url_or_doi: "..."
        snippet_or_summary: "..."
  confidence: 🟢/🟡/🔴/⚫              # 取证据中最低级
  severity: P0/P1/P2/P3
  recommended_fix:                          # 字段级 / 章节级
    type: edit | add | remove | restructure
    target: §X.Y 第 N 行 ...
    proposed_text: |
      ...
```

**强制规则**：

- 🔴 / ⚫ 置信度的 fix 不可写入 P0/P1（必须升级到 🟡 / 🟢 才能写成 fix），否则改记 followups
- P0/P1 finding 不允许"暂无证据"——若 evidence.external 为空，必须升级到阶段 3 NLM 深度调研

---

## 6. 置信度（按全局 CLAUDE.md）

- 🟢 High：≥3 个 A/B 级独立来源一致 + <6 个月（或经典稳定知识）
- 🟡 Medium：2 个来源一致或主流共识有少数反对
- 🔴 Low：来源 <2 / 过时 / 仅 C 级 / 分裂
- ⚫ Unknown：搜不到，明说

来源分层：A=官方/规范/一作论文；B=高星 issue/权威博客/会议；C=SO/普通博客/二手。**置信度取最低级来源**。

---

## 7. 工作流（7 阶段）

### 阶段 0 — 坐标系修正（已完成）

- ✅ 读完 `docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html`
- ✅ 项目 CLAUDE.md §2 已修订（L1=任务层、L2=航路规划层、Multimodal Fusion 独立层、X/Y/Z 三轴）
- ✅ E1 / E2 在 §2.3 落实
- **产出**：`audit/2026-04-30/01-mass-coordinate-system.md`（在阶段 5 综合时整理；坐标系已在 CLAUDE.md 落地，此文件用于审计记录）

### 阶段 1 — 引用预扫（C 精神）

- 对 [R1]–[R24] 逐条：DOI 检索、Web Search 验证存在、原文是否支持 v1.0 引用处的具体论断
- 每条 5 列：`{ref_id, title, exists, supports_claim, confidence}`
- 工具：`/nlm-research --depth fast` 单引用验证 / Web Search DOI 检索
- 任意 🔴/⚫ 项升入阶段 3 调研议题
- **产出**：`audit/2026-04-30/02-references-prescan.md`
- **预算**：≤45 分钟

### 阶段 2 — 业界 + 学术证据库构建

8 轮 `/nlm-research --depth deep --add-sources`，全部命中对应 domain 笔记本：

| 轮 | 主题 | 入库笔记本 |
|---|---|---|
| **R1** | 工业 COLAV 系统综述（Kongsberg / Wärtsilä / Sea Machines / Avikus / MUNIN / Yara / ReVolt） | `colav_algorithms` |
| **R2** | 学术 COLAV 主流路线（MPC-based / VO-based / IvP-based / RL-based）的对比与实证差异 | `colav_algorithms` |
| **R3** | ODD/Operational Envelope 在海事的工程实践（Rødseth 之外是否有更新；ENC/COLREGs/天气 ODD 的工程映射） | `maritime_regulations` |
| **R4** | Doer-Checker 在海事 / 航空 SIL 2-3 系统中的真实落地（Boeing 777 / NASA Auto GCAS / 海事案例 / Certified Control 进展） | `safety_verification` |
| **R5** | SAT 模型在海事 ROC 的实证研究（Veitch 之外；AGENT-T 应用） | `maritime_human_factors` |
| **R6** | IMO MASS Code MSC 110/111 最新条款（特别是 Operational Envelope 要求 + 4 级模式仲裁） | `maritime_regulations` |
| **R7** | CCS i-Ship Nx/Ri/Ai 标志认证案例 + DNV-CG-0264 9 子功能的工业应用 | `maritime_regulations` + `safety_verification` |
| **R8** | 多船型 Capability Manifest / Backseat Driver 的工程实例（MOOS-IvP / Sea Machines / YAML 配置范式） | `colav_algorithms` |

每轮调研报告必须含：**核心结论 + 推翻信号 + 来源年龄检查 + 置信度**。

- **产出**：`audit/2026-04-30/03-evidence-base.md`（按主题组织摘要，链接到 NLM 入库的具体 source）
- **预算**：8 轮，每轮 ~10–15 分钟，合计 ~100 分钟

### 阶段 3 — Smell-Test 通读（A 主线）

- 在阶段 2 证据库支撑下，按章节顺序通读 v1.0 全文（第 1–16 章）
- 沿 D1–D9 各维度抓异常，**不再凭空主观**
- 每发现一处 → 创建 finding 草稿（按 §5 八字段）
- 任何"未达置信度阈值"的 finding 触发回溯到阶段 2 加做 1–2 轮深度调研（**预算上限 12 轮**，超额征询用户）
- **预算**：~3–4 小时

### 阶段 4 — 架构方案最优性评判（D9 核心）

对每个对象做"v1.0 立场 vs 业界主流 vs 学术最优"三方对照：

**对象清单**（共 13 项）：
1. 决策一：ODD 作为组织原则
2. 决策二：8 模块功能分解（vs v3.0 Kongsberg 5 模块）
3. 决策三：CMM 映射为 SAT-1/2/3
4. 决策四：Doer-Checker 双轨（vs v3.0 X-axis Deterministic Checker 的差异）
5. M1 ODD/Envelope Manager
6. M2 World Model
7. M3 Mission Manager（vs L1 Mission Layer 命名冲突）
8. M4 Behavior Arbiter（IvP 选择是否最优）
9. M5 Tactical Planner（Mid-MPC + BC-MPC 双层）
10. M6 COLREGs Reasoner（独立模块 vs 嵌入 Avoidance Planner）
11. M7 Safety Supervisor（与 v3.0 X-axis Checker 的关系）
12. M8 HMI/Transparency Bridge
13. 多船型 Capability Manifest + Backseat Driver

**每对象输出表**：

```
{对象} 评判
├─ v1.0 立场：…
├─ v3.0 Kongsberg 立场：…
├─ 学术最优：…（≥3 篇 A/B 级文献）
├─ 工业其他参考：…（≥3 个独立来源）
└─ 综合判定：保留 / 修订 / 替换 + 理由 + 影响范围
```

- **产出**：`audit/2026-04-30/04-architecture-judgment.md`
- **预算**：~90 分钟

### 阶段 5 — 跨文档 + 跨层对照

#### 5.1 跨文档（vs `docs/Doc From Claude/`）

- v1.1 / v1.2 / patch-d / L3 裁决汇总 / 深度研究报告 中提出过但 v1.0 未吸纳的论断
- v1.0 静默推翻早期论断的清单
- 术语漂移（"L3 战术决策层 TDL" vs "L3 战术层 Tactical Layer" vs "L3 TDCAL"）

#### 5.2 跨层（vs `docs/Init From Zulip/` + `‼️mass_adas_v3 Kongsberg.html`）

- §15 接口总表 vs L2/L4 实际输入输出
- M2 World Model 输入 vs Multimodal Fusion 实际输出（Track 级 + Nav Filter）
- M7 Safety Supervisor vs X-axis Deterministic Checker 是否冲突 / 重叠 / 互补
- ASDR 决策日志契约 vs L3 输出
- Reflex Arc <500ms 跳过 L3 时 L3 是否需感知并响应

- **产出**：`audit/2026-04-30/05-cross-doc-check.md` + `06-cross-layer-check.md`
- **预算**：~60 分钟

### 阶段 6 — 综合 + 三态结论

- 汇总所有 finding（草稿 → 正式）
- 填 14×9 审计评分矩阵（§9），算出每行 / 每列 / 全表综合分
- **三态决策树**（基于全表综合分 + P0 计数；阈值经业界基准调研确定，见 §16）：

  | 三态 | 全表综合分 | P0 计数 | 含义 |
  |---|---|---|---|
  | **A** | ≥ 2.5 | ≤ 2 | 架构基本最优，仅需 patch list |
  | **B** | 1.5 – 2.5 | 3 – 5 | 结构性修订 + ADR 增量 |
  | **C** | < 1.5 | ≥ 6 | 架构需重写为 v2.0 |

  **降级规则**：满足任一 C 触发条件（综合分 < 1.5 **或** P0 ≥ 6）即降为 C；同样，P0 计数与综合分中较劣者决定结论档位。

  **业界基准与本 spec 取舍**（详见 §15）：
  - 航空 DO-178C / IEEE 1012 严格惯例：**P0 = 0** 才允许发布。本 spec 容忍 A 档 P0 ≤ 2 是**务实选择**——v1.0 不是适航最终评审，允许带 P0 进入 v1.1 修复
  - 若本审计是 CCS 入级前最终评审：建议把 A 档收紧到 **P0 = 0 且综合分 ≥ 2.7**
  - 阈值本身是**设计决策**而非标准要求；A/B/C 结论须配套写入 ADR，让架构师 / 验证官 / CCS 验船师三方对齐预期

- 写：
  - **必出**：`00-executive-summary.md`、`05-audit-report.md`、`08-audit-grid.md`、`99-followups.md`
  - **A/B 走**：`08a-patch-list.md`（字段级 diff，可直接用于 v1.1 合并）
  - **B 走**：`08c-adr-deltas.md`（顶层决策的 ADR 增量）
  - **C 走**：`08b-v2-architecture.md`（替代架构提案，含工程证据 + 迁移路径）
- **预算**：~60–90 分钟

---

## 8. 交付物结构

```
docs/Design/Architecture Design/audit/2026-04-30/
├─ 00-executive-summary.md         # 一页摘要 + A/B/C 结论 + 关键证据 + 合并建议
├─ 01-mass-coordinate-system.md    # 阶段 0 产物：MASS v3.0 坐标系记录（与 CLAUDE.md §2 一致）
├─ 02-references-prescan.md        # 阶段 1：[R1]-[R24] 预扫表（5 列）
├─ 03-evidence-base.md             # 阶段 2：8 轮 deep research 摘要（按主题组织）
├─ 04-architecture-judgment.md     # 阶段 4：13 对象的三方对照评判
├─ 05-audit-report.md              # 主审计报告（按 finding ID 排序，每条含 §5 八字段）
├─ 06-cross-doc-check.md           # 阶段 5.1：与 Doc From Claude 历史稿对照
├─ 07-cross-layer-check.md         # 阶段 5.2：与 Zulip 各层 + Kongsberg Benchmark 对照
├─ 08-audit-grid.md                # 14×9 评分矩阵 + 行/列/全表综合分（§9）
├─ 08a-patch-list.md               # （A/B 走）字段级 diff
├─ 08b-v2-architecture.md          # （C 走）替代架构提案
├─ 08c-adr-deltas.md               # （B/C 走）顶层 ADR 增量
└─ 99-followups.md                 # 不阻塞合并的备忘 + P3 项
```

**Finding ID 规则**：`F-{P0|P1|P2|P3}-D{1..9}-{seq:03d}`（例 `F-P0-D1-001` = 第 1 条 D1 维度 P0 级 finding）。

---

## 9. 审计评分矩阵（14 × 9 + 行/列/全表三聚合）

**取代原 ✅/⚠️/N/A 三状态网格**——以"行为主、0–3 评分、N/A 接受"组织：信息密度更高，且能聚合出对象级 / 维度级 / 全表三个层次的综合分，直接驱动 §7.6 三态结论决策。

### 9.1 矩阵维度

**14 行**（被评判对象）：第 2 章 4 决策（决策一–四）+ 8 模块（M1–M8）+ ODD 框架 + §15 接口总表 = 14 项

> 行标签用"决策一-四"而非"D1.1-D1.4"——避免与列维度 ID（D1–D9）混淆。

**9 列**（评判维度）：D1 决策可证伪 / D2 引用真实 / D3 Doer-Checker / D4 接口契约 / D5 时间尺度 / D6 降级路径 / D7 合规映射 / D8 多船型不侵入 / D9 方案最优性

### 9.2 单格评分量表（0–3 + N/A）

| 分数 | 含义 | 对应 finding |
|---|---|---|
| **3** | 通过——与业界主流一致或更优；论据充分 | 无 finding 或仅 P3 |
| **2** | 可改进——基本可保留，存在改进空间 | P2 finding |
| **1** | 需修订——有明确缺陷需要 v1.1 处理 | P1 finding |
| **0** | 严重问题——动摇核心论断 / 引用幻觉 / 设计漏洞 | P0 finding |
| **N/A** | 不适用——该 (对象, 维度) 组合无意义 | — |

**说明**：N/A 不参与聚合分母。可接受的 N/A 例：§15 接口总表 在 D9 方案最优性维度（接口契约本身不是"方案"）；ODD 框架 在 D2 引用真实性维度若不依赖独立引用。

### 9.3 三层聚合

- **行综合分**（每个对象，14 个）= mean(non-N/A 格子) ∈ [0, 3]
- **列综合分**（每个维度，9 个）= mean(non-N/A 格子) ∈ [0, 3]
- **全表综合分**（架构整体）= mean(全部 non-N/A 格子) ∈ [0, 3]

### 9.4 行综合分映射到 D9 评判三态

| 行综合分 | D9 三态判定 |
|---|---|
| ≥ 2.5 | **保留**（v1.0 该对象基本最优） |
| 1.5 – 2.5 | **修订**（v1.0 该对象方向对，需补丁） |
| < 1.5 | **替换**（v1.0 该对象不最优，需替代方案） |

### 9.5 矩阵作用

1. **覆盖证据**：每格有分或 N/A 即证明审过了 (行, 列)，CCS 评审 / 内部回溯可追溯
2. **驱动结论**：§7.6 决策树直接读取全表综合分 + P0 计数得出 A/B/C
3. **可视化**：行排序后能直观看出"哪个对象最弱"；列排序后能看出"哪个维度问题最普遍"

---

## 10. NLM 调研规则

### 10.1 默认行为（按全局 + 项目 CLAUDE.md）

- `/nlm-research --depth fast` 用于事实 / 版本 / DOI / 单点引用核验
- `/nlm-research --depth deep --add-sources` 用于选型 / 对比 / 反证调研，**自动入对应 domain 笔记本**
- **不允许 `--no-add-sources`**

### 10.2 调研入库路由

| 调研主题 | 入库笔记本 |
|---|---|
| COLAV 算法 / IvP / MPC / VO / RL / 工业 COLAV 系统 | `colav_algorithms` |
| ODD / Operational Envelope / IMO MASS / COLREGs Rule | `maritime_regulations` |
| IEC 61508 / ISO 21448 SOTIF / Doer-Checker / Certified Control / SIL | `safety_verification` |
| SAT / ROC HF / 接管 TMR / 操作员负荷 | `maritime_human_factors` |
| 操纵性 / 水动力 / MMG / 推进 / 风流响应 | `ship_maneuvering` |

### 10.3 触发深度调研的条件

任一即触发：
1. 顶层决策（D1）的论据被发现弱支撑或与现有证据冲突
2. 引用 [Rx] 经预扫标 🔴 或 ⚫
3. Doer-Checker 独立性论断在代码 / 接口层面无法证明
4. 一处发现被多个模块共享（系统性问题）
5. D9 评判中 v1.0 立场与业界主流冲突且证据不足

### 10.4 预算

阶段 2 强制 8 轮；阶段 3+ 总计上限 12 轮；超额征询用户。NotebookLM 每日 500 次询问额度——20 轮 deep research × 每轮内部 ~10–20 次 NLM 调用 = ~200–400 次/天，落在配额内。

### 10.5 调研执行模式（subagent + 异步 + 并行）

按用户指示：**主 agent 不亲自跑 deep research；统一交给 subagent 异步执行**。

**Subagent 选型**：

| 调研类型 | Subagent 类型 | 模型 | 工具 | 入笔记本？ |
|---|---|---|---|---|
| 阶段 1 单条引用核验（fast） | `quick-researcher` | Haiku 4.5 | WebSearch + WebFetch | ❌（不是 L3 领域知识） |
| 阶段 2 主题深调研（deep） | `general-purpose` | Haiku 4.5 | * (含 Skill)，由 subagent 内部触发 `/nlm-research --depth deep --add-sources` | ✅ 自动入对应 domain 笔记本 |
| 阶段 3+ 临时反证调研 | `general-purpose` | Haiku 4.5 | 同上 | ✅ |
| 元设计调研（如本次阈值标准） | `quick-researcher` | Haiku 4.5 | WebSearch + WebFetch | ❌（非 L3 领域） |

**异步执行规约**：

1. **主 agent 不等待**：所有 subagent 调用 `run_in_background: true`；主 agent 在 subagent 跑的同时做其他工作（编辑文档、整理已有结果、规划下一步）
2. **完成时收集**：subagent 完成时系统自动通知，主 agent 在通知到达时收集结果并存入对应 deliverable（如 `03-evidence-base.md`）
3. **不轮询**：主 agent 不主动 SendMessage 询问"做完了吗"；只在系统通知时反应
4. **并行批次**：阶段 2 的 8 轮 deep research **分 2–3 批并发**（每批 ≤4 个 subagent），避免 NLM 后端 burst rate limit；批与批之间主 agent 整理上一批结果
5. **阶段 2 完成前不走 `/nlm-ask`**：当前 6 个笔记本 source_count = 0，ask 无源可查，必须先 deep research 入库；阶段 2 后笔记本充实后，nlm-ask 可作为 deep 调研的免费补充（不耗 NLM 配额，详见 §14）

**主 agent 在阶段 2 的工作模式**：

```
for batch in [(R1, R2, R3, R4), (R5, R6, R7, R8)]:
    # 并发启动一批 subagent（同 message 内多个 Agent 调用）
    for round in batch:
        spawn general-purpose subagent (model: haiku, run_in_background: true)
            with prompt: "执行 /nlm-research --depth deep --add-sources，主题 {topic}，
                          入库笔记本 {notebook}，输出含核心结论 / 推翻信号 / 来源列表 / 置信度"
    
    # 主 agent 此时可以做：整理 02-references-prescan.md 中的待回填项 /
    # 起草 04-architecture-judgment.md 的对象框架 / 准备阶段 3 阅读顺序
    
    # 系统通知到达 → 收集每个 subagent 的输出，写入 03-evidence-base.md 对应章节
    
    # 全批完成后启动下一批
```

**Subagent prompt 模板**（统一格式以便结果可机械收集）：

```
你是研究子代理，负责一轮 NotebookLM 深度调研。

**主题**：{round 主题，例 "工业 COLAV 系统综述"}
**入库笔记本**：{notebook，例 "colav_algorithms"}
**目标**：建立 ≥3 工业 + ≥3 学术 A/B 级证据，找推翻信号

**执行步骤**：
1. 调用 Skill 工具触发 /nlm-research --depth deep --add-sources，主题如上
2. 等待 deep research 完成（5–10 分钟正常）
3. 整理输出，要求格式：

   ## 核心结论（200–400 字）
   ## 关键证据（≥3 工业 + ≥3 学术 A/B 级）
   ## 推翻信号（任何反对意见 / 异常发现）
   ## 来源年龄分布（<6 月 / 6–24 月 / >24 月）
   ## 置信度 🟢/🟡/🔴/⚫
   ## NotebookLM 入库 source IDs（用于 spec 追溯）

不要凭记忆。每条断言必须有来源。如果某子主题查不到 ≥3 来源，明说并降级置信度。
```

---

## 11. Definition of Done

- [ ] **每个保留 / 采纳的核心决策**有 ≥3 工业 + ≥3 学术证据
- [ ] **每个修订 / 弃用的决策**有"为什么不行"的反证（推翻信号）
- [ ] 24 条引用全部预扫完成；🔴/⚫ 数 ≤ 5
- [ ] 14 × 9 评分矩阵全部填分或 N/A（无空格）；行综合分 / 列综合分 / 全表综合分均算出
- [ ] 阶段 2 NLM deep research **8 轮全部完成**（不允许跳）；全部由 subagent 异步执行（§10.5）
- [ ] 阶段 3+ NLM 调研轮次 ≤ 12
- [ ] 所有 P0 / P1 finding 的置信度均 ≥ 🟡，配 fix 建议（type/target/proposed_text）
- [ ] 跨文档 + 跨层对照各产 ≥ 1 节（即使为 0 finding 也要写"已对照，无发现"）
- [ ] 摘要给出明确 A/B/C 结论 + 推荐合并 / 重写的具体路径；阈值与业界基准（§15）的取舍写入 ADR
- [ ] D9 评判覆盖全部 13 对象，每对象给出综合判定（保留 / 修订 / 替换）

---

## 12. 资源预算估算

| 阶段 | 内容 | 预算 |
|---|---|---|
| 0 | 坐标系修正（已完成） | — |
| 1 | 24 条引用预扫 | 45 min |
| 2 | 8 轮 deep research | ~100 min |
| 3 | Smell-Test 通读 | 3–4 h |
| 4 | 13 对象 D9 评判 | 90 min |
| 5 | 跨文档 + 跨层对照 | 60 min |
| 6 | 综合 + 三态结论 | 60–90 min |

**总计**：~7–8 小时执行时间。**写 spec 后必须经 writing-plans 切任务、再 executing-plans 实跑，不能一坐到底**。

---

## 13. 待用户确认 / 已知边界

### 13.1 已落实

- ✅ E1：固守现有 `.nlm/config.json`（local + 5 domain）
- ✅ E2：MASS v3.0 Kongsberg Benchmark 是真值；CLAUDE.md §2 已修订

### 13.2 spec 写作前已对齐的关键决策

- ✅ 双轨并行（smell-test + 选择性 NLM 深度调研）
- ✅ 输出形态：审计报告 + 补丁清单（不直接动 v1.0）
- ✅ 水平范围：A + B + C 全开（架构本身 + 跨文档 + 跨层）
- ✅ 方法论：A 主线 + C 引用预扫 + B 网格覆盖
- ✅ 三态结论 A/B/C 结构
- ✅ 8 轮 deep research 主题
- ✅ DoD 含"≥3 工业 + ≥3 学术"硬门槛

### 13.3 未在本 spec 中决定的（留待 writing-plans 阶段）

- 阶段 3 Smell-Test 的章节阅读顺序（建议第 2 章 → 第 4 章 → 第 11 章 M7 → 其他模块，因 M7 涉及 D3 Doer-Checker 独立性）
- 阶段 4 D9 评判的 13 对象的执行顺序（建议先 4 决策再 8 模块）
- 进度检查点（建议在阶段 2 结束、阶段 4 结束设两次用户确认点）
- 是否在阶段 3 发现某项 P0 后即时通知用户（建议是；用户可随时叫停或调整方向）

---

## 14. 风险

| 风险 | 触发条件 | 缓解 |
|---|---|---|
| 阶段 2 8 轮 deep research 单轮超时 | NLM 服务限流 / 主题过宽 / subagent 回报延迟 | 拆主题为 2 个并行 subagent；不可走 nlm-ask 兜底（笔记本空，ask 无效）；subagent 失败的轮重启 |
| NotebookLM 500/天 配额耗尽 | 阶段 3+ 临时调研 + 阶段 2 内部多次查询累计超额 | 主 agent 跟踪每日累计；接近 80% 时暂停下发新 subagent，次日继续；已入笔记本来源可重复 nlm-ask 不耗 deep 配额（仅在阶段 2 后期使用） |
| Subagent 异步通知错位 | 多个 subagent 同时完成，回报顺序与发起顺序不一致 | 每个 subagent prompt 包含 round ID（R1–R8），收集时按 ID 归位 |
| 阶段 3 发现 P0 finding 数量爆炸 | v1.0 实际质量低于预期 | 阶段 6 直接走三态 C，节省阶段 4 评判精力 |
| 24 条引用 🔴/⚫ 数量 > 5 | v1.0 引用幻觉广泛 | 触发独立的"引用补救子任务"（subagent 并行核验），不阻塞主审 |
| 阶段 4 业界证据找不到 ≥3 个独立来源 | 主题过窄或公开材料稀缺 | 降级到 ≥2 工业 + ≥3 学术，并在 finding 标注 confidence ≤ 🟡；同时在 ADR 注明证据弱 |
| 决策树阈值的工业基准缺失（已确认 §15） | 业界对 P0 计数 / 综合分阈值无统一标准 | 本 spec 阈值是设计决策；A/B/C 结论须配套写入 ADR，让验船师 / 验证官 / 架构师对齐预期 |
| 用户中途调整目标 | 审计中发现需要重新调整 audit 设计 | spec 是 living document，迭代到 v1.1 |

---

## 15. 阈值业界基准（决策树阈值的来源）

§7.6 三态决策树的阈值（A/B/C）经业界基准调研得出。本节记录证据，使阈值选择可追溯、可质疑、可调整。

### 15.1 关键发现

**业界对"X 个 critical finding = reject"无统一量化标准**——主流认证标准（DO-178C / ISO 26262 / IEC 61508 / IEEE 1012 / NPR 7150.2）都用程序性要求（覆盖率、活动完整性、文档证据）而非发现计数。

### 15.2 典型工业惯例

| 标准 | 处理方式 |
|---|---|
| **DO-178C DAL A**（航空） | 71 个验证目标必须全部满足；critical 评审发现"必须在发布前解决"（AC 20-189） |
| **DO-254 DAL A**（航空硬件） | 100% Code Coverage + 100% Requirement Coverage；formal methods 并行 |
| **IEC 61508 SIL 2/3** | "Systematic Capability"评估 + 架构冗余约束（Route 1h/2h）；无发现计数线 |
| **IEEE 1012 V&V** | Critical anomaly 必须在下一生命周期前解决；其他无量化通过线 |
| **NPR 7150.2D**（NASA Class A） | 强制独立验证；圈复杂度 ≤15；MC/DC 覆盖率目标 |
| **DNV-CG-0264**（海事自主） | 用 AROS Class Notations 标记**能力声明**而非通过/失败；架构 pass/fail 阈值在公开文档中**缺失** |

### 15.3 评分量表惯例

业界没有统一的 0–3 / 0–5 评分标度。三种主流做法：

1. **航空（DO-178/254）**：不打分，用 checklist——所有目标达成即通过
2. **汽车（ISO 26262）**：ASIL A–D 文本级分类
3. **工业（IEC 61508）**：Systematic Capability 等级（SC1–SC3），用于 SIL 确定

本 spec §9 采用 0–3 评分是**项目自定义工程方法**，不是业界标准。

### 15.4 本 spec 阈值的依据

- **航空 P0 = 0 是底线**——是设计决策（业界共识），不是法规
- **海事 DNV 没有数字阈值**——必须项目自定义
- **本 spec A 档 P0 ≤ 2 是务实选择**：v1.0 是早期审计，不是 CCS 入级最终评审
- **A 档综合分 ≥ 2.5（≈83%）**：参考 DO-178C 71 个目标全部达成 ≈ 100%，本 spec 容忍度更宽

### 15.5 推荐做法

1. A/B/C 结论与阈值取舍写入 ADR（与架构师 / 验证官 / CCS 验船师对齐）
2. 若审计是 CCS 入级最终评审：升级 A 档到 P0 = 0 + 综合分 ≥ 2.7
3. 阈值经多次审计校准后，可作为本项目的内部 V&V 标准

### 15.6 调研来源

| 来源 | 类型 | 链接 |
|---|---|---|
| DO-178C | A | https://www.do178.org/ |
| IEC 61508 / TÜV SÜD | A/B | https://www.tuvsud.com/en-us/services/functional-safety/iec-61508 |
| IEEE 1012-2016 | A | https://ieeexplore.ieee.org/document/8055462/ |
| NASA NPR 7150.2D / LDRA | B | https://ldra.com/npr7150-2d/ |
| DNV-CG-0264 | A | https://www.dnv.com/maritime/autonomous-remotely-operated-ships/class-guideline/ |
| ISO 25010 / Perforce | B | https://www.perforce.com/blog/qac/what-is-iso-25010 |
| INCOSE Handbook | A | https://www.incose.org/resources-publications/technical-publications/se-handbook/ |
| DO-254 / Cadence | B | https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/solutions/aerospace-and-defense/do-254-explained-wp.pdf |

**本节调研置信度** 🟡 Medium（A 级标准均查实存在，但都不含具体数字阈值；推断与项目化建议为本审计设计师的工程判断）

---

## 16. 后续动作

1. **本 spec 自审**（brainstorming skill 内建步骤）— ✅ 已完成
2. **用户审 spec**（待）
3. **调用 superpowers:writing-plans** 切实施计划（每个阶段 → 多个可独立执行的 task）
4. **调用 superpowers:executing-plans** 实跑（含每阶段 review checkpoint，全部 NLM 调研走 §10.5 subagent 异步模式）

---

*本 spec 在 brainstorming 阶段产出，对应 superpowers 工作流。spec 与实施计划是分离的：spec 定义"做什么 / 为什么"，writing-plans 负责"怎么做 / 顺序 / 验证"。*
