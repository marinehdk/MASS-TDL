# L3 战术层架构评判审计 · 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 执行 spec `docs/superpowers/specs/2026-04-30-l3-architecture-review-design.md` v1.1 定义的 7 阶段审计，产出 13 个 markdown 交付物，最终给出 A/B/C 三态结论与对应 v1.1 patch / v2.0 替代架构 / ADR 增量。

**Architecture:** 阶段 0 已完成（CLAUDE.md §2 修订）；阶段 1–6 顺序执行，但**阶段 1 内部 + 阶段 2 内部并行** —— 24 条引用核验分 2 批 × 12 条并行，8 轮 deep research 分 2 批 × 4 轮并行。每阶段末尾设 review checkpoint。所有 NLM 调研走 §10.5 subagent 异步模式（Haiku 4.5 + run_in_background）。

**Tech Stack:**
- 交付物：Markdown
- 调研：subagent (`general-purpose` for deep / `quick-researcher` for fast，全部 Haiku 4.5，全部 `run_in_background: true`)
- 工具：Read / Bash / Edit / Write / Agent / WebSearch（subagent 内）
- **不是 git 仓库**——任务结尾用"保存并 ls 验证"，无 commit 步骤

**Plan path：** `docs/superpowers/plans/2026-04-30-l3-architecture-review-implementation-plan.md`

---

## 文件结构

### 待创建（13 个交付物 + 1 个临时工作区）

```
docs/Design/Architecture Design/audit/2026-04-30/
├─ 00-executive-summary.md         # 阶段 6 最终
├─ 01-mass-coordinate-system.md    # 阶段 6 整理（镜像 CLAUDE.md §2）
├─ 02-references-prescan.md        # 阶段 1
├─ 03-evidence-base.md             # 阶段 2（8 轮深调研摘要）
├─ 04-architecture-judgment.md     # 阶段 4（13 对象三方对照）
├─ 05-audit-report.md              # 阶段 3 草拟 → 阶段 6 定稿
├─ 06-cross-doc-check.md           # 阶段 5.1
├─ 07-cross-layer-check.md         # 阶段 5.2
├─ 08-audit-grid.md                # 阶段 6（14×9 评分 + 三层综合分）
├─ 08a-patch-list.md               # 阶段 6 A/B 路径
├─ 08b-v2-architecture.md          # 阶段 6 C 路径
├─ 08c-adr-deltas.md               # 阶段 6 B/C 路径
├─ 99-followups.md                 # 阶段 6
└─ _temp/                          # 临时工作目录
   ├─ refs-extracted.md            # T1.2 输出
   ├─ batch-1-refs.txt             # T1.3 输入
   ├─ batch-2-refs.txt             # T1.4 输入
   └─ findings-raw.md              # T3.x 累积
```

### 只读源文件

```
docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md   # 1168 行 v1.0 主审对象
docs/Doc From Claude/*.md                                          # 早期研究稿 (10+ 文件)
docs/Init From Zulip/MASS ADAS L1 Mission Layer 任务层/*           # 跨层接口
docs/Init From Zulip/MASS ADAS L2 Voyage Planner 航路规划层/*
docs/Init From Zulip/MASS ADAS L4 Guidance Layer 引导层/*
docs/Init From Zulip/MASS ADAS L5 Control & Allocation 控制分配层/*
docs/Init From Zulip/MASS ADAS Multimodal Fusion 多模态感知融合/*
docs/Init From Zulip/MASS ADAS Deterministic Checker/*
docs/Init From Zulip/MASS ADAS ASDR/*
docs/Init From Zulip/MASS ADAS CyberSecurity/*
docs/Init From Zulip/MASS ADAS Hardware Specification 硬件规格书/*
docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html
.nlm/config.json                                                    # 6 个笔记本 ID 路由
CLAUDE.md                                                           # 项目规则（§2 已修订）
```

---

## 通用约定

### Subagent 调用模板（在所有 Agent 工具调用中复用）

**Deep Research（阶段 2 R1-R8 + 阶段 3 临时反证）**：

```
subagent_type: general-purpose
model: haiku
run_in_background: true
prompt:
  你是研究子代理，执行一轮 NotebookLM 深度调研。

  **Round ID**: {Rx}
  **主题**: {topic}
  **入库笔记本**: {notebook_key}（对应 .nlm/config.json 中的 domain_notebooks 键）
  **入库笔记本 UUID**: {notebook_uuid}（从 .nlm/config.json 读 domain_notebooks.{key}.id）
  **目标**: 建立 ≥3 工业 + ≥3 学术 A/B 级证据，找推翻信号

  执行步骤：
  1. 通过 Skill 工具触发 /nlm-research --depth deep --add-sources，主题如上，目标笔记本如上
  2. 等待完成（5–10 分钟正常）
  3. **触发后处理（强制）**——nlm-research 完成后**必须**主动调用一次：
     `bash ~/.claude/skills/nlm/scripts/invoke.sh deduplicate --notebook-id {notebook_uuid}`
     这会 (a) 删除 NLM 爬取失败的 sources（如 paywalled / 404 URL）
     (b) 同步 .nlm/config.json 的 source_count（不跑 dedup 则永远停留在旧值）
     **R1 烟雾测试已证实此 quirk**（imarine.cn + scribd.com 失败需 dedup 清掉）
  4. 重读 .nlm/config.json，记录 dedup 后的 source_count 增量（用于 §11 NotebookLM 入库 source IDs 字段）
  5. 整理输出，严格按以下 markdown 格式：

     # Round {Rx}: {topic}

     ## 核心结论（200–400 字）
     ## 关键证据
     ### 工业参考（≥3）
     - [系统名 / 厂商] (年份) — 简述 — 链接 — 来源等级 A/B/C
     ### 学术文献（≥3）
     - [作者 等] (年份) — 简述 — DOI/链接 — 来源等级
     ## 推翻信号
     ## 来源年龄分布（<6 月 / 6–24 月 / >24 月）
     ## 置信度 🟢/🟡/🔴/⚫
     ## NotebookLM 入库结果
     - 笔记本: {notebook_key}
     - source_count BEFORE: N
     - source_count AFTER (dedup 后): N
     - 增量: ΔN
     - dedup 删除失败来源数: N

  不要凭记忆。每条断言必须有来源。某子主题查不到 ≥3 来源即明说并降级置信度。
  Step 3 的 dedup 是**强制**步骤，不允许跳过。
```

**Quick Reference Verification（阶段 1）**：

```
subagent_type: quick-researcher
model: haiku
run_in_background: true
prompt:
  你是研究子代理，验证一批参考文献的真实性。

  **Batch ID**: {batch-1 或 batch-2}
  **任务**: 对下方每条引用执行：
    1. WebSearch 验证文献存在
    2. WebFetch 检查 DOI / URL 有效
    3. 判断引用处的具体论断是否被原文支持（按提供的 claim_context）

  **输入引用列表**:
  {从 _temp/refs-extracted.md 复制此 batch 的引用条目}

  **输出格式（严格按表）**:

  | ref_id | title_short | exists | doi_valid | supports_claim | confidence | notes |
  |---|---|---|---|---|---|---|
  | R1 | CCS i-Ship... | ✅/❌ | ✅/❌/N/A | ✅/❌/⚠️ | 🟢/🟡/🔴/⚫ | ... |

  exists：能否在网上找到该出版物
  doi_valid：DOI/URL 是否解析（不解析也注明）
  supports_claim：原文（或摘要/索引）是否支持 v1.0 引用处的论断
  confidence：综合评估
  notes：任何异常（例如"DOI 解析但摘要不支持架构论断"）

  不要凭记忆。预算 ≤ 3 分钟/条。
```

### 异步收集协议

- 主 agent 启动 subagent 后 **不主动轮询**；继续做 plan 中下一个非依赖任务
- 系统在 subagent 完成时自动通知（带 `task-id`）
- 主 agent 通知到达时收集 result，按 Round ID / Batch ID 归位写入对应文件
- 一批 subagent 全部回报后才进入下一批

### 文件命名 / Finding ID

- 文件: `audit/2026-04-30/<NN>-<name>.md`
- Finding ID: `F-{P0|P1|P2|P3}-D{1..9}-{seq:03d}`（例 `F-P0-D1-001`）

---

## Phase 0 — 坐标系修正（已完成 ✅）

CLAUDE.md §2 已重写为 v3.0 Kongsberg Benchmark 结构（L1=任务层 / L2=航路规划层 / Multimodal Fusion 独立 / X/Y/Z 三轴）。

无任务待执行。`01-mass-coordinate-system.md` 在阶段 6 镜像 CLAUDE.md §2 写入。

---

## Phase 1 — 引用预扫（已完成 ✅）

> **阶段 1 完成记录（2026-04-30 16:00）**
> - 24 条引用全部经 WebSearch/WebFetch 实际核验（quick-researcher × 2，Haiku 4.5）
> - 🟢 11 / 🟡 8 / 🔴 5 / ⚫ 0；DoD 通过（🔴/⚫ = 5 ≤ 5）
> - 关键发现：R8 二轴确认（PMC 全文）、R12 无"100×"量化、R21 可能幻觉
> - 交付物：`audit/2026-04-30/02-references-prescan.md`（129 行，WebSearch 核验版）
> - batch-1-refs.txt / batch-2-refs.txt 已含 domain_notebook 路由标注

**目标**：24 条 [R1]–[R24] 全部核验真伪，标 🟢/🟡/🔴/⚫，🔴/⚫ 项升入阶段 3 反证议题。

### Task T1.1: 创建审计目录结构

**Files:**
- Create directory: `docs/Design/Architecture Design/audit/2026-04-30/`
- Create directory: `docs/Design/Architecture Design/audit/2026-04-30/_temp/`

- [x] **Step 1: 创建目录**

```bash
mkdir -p "docs/Design/Architecture Design/audit/2026-04-30/_temp"
```

- [x] **Step 2: 验证存在**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/"
```

Expected: `_temp/` 目录可见。

---

### Task T1.2: 提取 24 条引用 + 上下文

**Files:**
- Read: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` （§16 行 1108–1168 + 全文 72 处内联引用）
- Create: `docs/Design/Architecture Design/audit/2026-04-30/_temp/refs-extracted.md`

- [x] **Step 1: 读 §16 完整参考文献**

Read v1.0 行 1108–1168，逐条抄出 [R1]–[R24] 的完整文献条目。

- [x] **Step 2: grep 内联引用位置**

```bash
grep -n '\[R[0-9]\+\]' "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
```

得到每条 [Rx] 的所有出现位置。

- [x] **Step 3: 对每条 [Rx] 配齐"声称"上下文**

对每个 [Rx]，挑选 1–3 处典型内联位置，记录：
- 该位置 v1.0 用 [Rx] 支持什么论断（单句概括）
- 章节号 + 行号

- [x] **Step 4: 写入 _temp/refs-extracted.md**

格式（YAML，便于 subagent prompt 直接拼接）：

```yaml
- ref_id: R1
  full_citation: "CCS《智能船舶规范》（2024/2025）+《船用软件安全及可靠性评估指南》. 中国船级社, 北京."
  inline_locations:
    - section: §1.3
      lines: 61
      claim: "i-Ship (Nx, Ri/Ai) 标志 + 决策须白盒可审计"
  type: 规范
  ...
- ref_id: R2
  ...
```

- [x] **Step 5: 保存 + 行数验证**

```bash
wc -l "docs/Design/Architecture Design/audit/2026-04-30/_temp/refs-extracted.md"
```

Expected: ≥ 24 条 × 5 行 ≈ 120 行（最少）。

---

### Task T1.3: 拆分两批分别送 subagent（并行）

**Files:**
- Read: `_temp/refs-extracted.md`
- Create: `_temp/batch-1-refs.txt`, `_temp/batch-2-refs.txt`

- [x] **Step 1: 拆分 R1-R12 到 batch-1**

把 R1–R12 复制到 `_temp/batch-1-refs.txt`（YAML 格式保留）。

- [x] **Step 2: 拆分 R13-R24 到 batch-2**

把 R13–R24 复制到 `_temp/batch-2-refs.txt`。

- [x] **Step 3: 并发启动 2 个 quick-researcher subagent（同一 message 内）**

按"通用约定 / Quick Reference Verification"模板启动 2 个 subagent：
- subagent A: batch-1（R1–R12）
- subagent B: batch-2（R13–R24）

每个 subagent 的 prompt 中粘贴对应 batch 的 YAML 内容。

记录 returned `agentId`：A=`{ID-A}`, B=`{ID-B}`。

- [x] **Step 4: 主 agent 进入下一任务（不等待）**

进入 T1.4（准备 02-references-prescan.md 表头），直到收到完成通知。

---

### Task T1.4: 准备 02-references-prescan.md 表头骨架（与 T1.3 并行）

**Files:**
- Create: `audit/2026-04-30/02-references-prescan.md`

- [x] **Step 1: 写入文件头**

```markdown
# 阶段 1 · 引用预扫

> 对 v1.0 §16 的 [R1]–[R24] 共 24 条引用做真实性核验：是否存在、DOI/URL 是否有效、原文是否支持 v1.0 内联引用处的论断。
>
> 验证方式：quick-researcher subagent（Haiku 4.5）+ WebSearch + WebFetch
>
> 完成时间: {YYYY-MM-DD HH:MM}

---

## 汇总

| 状态 | 数量 |
|---|---|
| 🟢 High（exists + valid + supports） | TBD |
| 🟡 Medium（partial issue） | TBD |
| 🔴 Low（不支持论断/弱来源） | TBD |
| ⚫ Unknown（查不到） | TBD |

🔴/⚫ 数 = TBD。**spec DoD 阈值 ≤ 5**。

---

## Batch 1 · R1–R12

（待 subagent A 回报后填入）

---

## Batch 2 · R13–R24

（待 subagent B 回报后填入）

---

## 升入阶段 3 反证议题清单

🔴/⚫ 引用进入阶段 3 NLM 深度调研。
- TBD
```

- [x] **Step 2: 保存 + 验证**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/02-references-prescan.md"
```

---

### Task T1.5: 收集 subagent 输出 + 填充表

**触发条件**：T1.3 启动的 2 个 subagent 全部回报完成。

**Files:**
- Modify: `audit/2026-04-30/02-references-prescan.md`

- [x] **Step 1: 收集 subagent A 输出（batch-1 表）**

把 subagent A 返回的 markdown 表粘贴到 `## Batch 1 · R1–R12` 章节下。

- [x] **Step 2: 收集 subagent B 输出（batch-2 表）**

把 subagent B 返回的 markdown 表粘贴到 `## Batch 2 · R13–R24` 章节下。

- [x] **Step 3: 计算汇总统计**

数每个置信度档位的数量，回填"汇总"表的 TBD。

- [x] **Step 4: 列升入阶段 3 的反证议题**

把所有 🔴 / ⚫ 引用写到"升入阶段 3 反证议题清单"。例：

```markdown
- R8（Rødseth 2022 Operational Envelope）—— 🔴：DOI 解析但摘要未明确"三轴定义"，需阶段 3 R3 调研补证
```

- [x] **Step 5: 完成时间戳 + 保存**

把"完成时间"占位替换为当前时间。

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/02-references-prescan.md"
wc -l "docs/Design/Architecture Design/audit/2026-04-30/02-references-prescan.md"
```

---

### Task T1.6: 阶段 1 review checkpoint

- [x] **Step 1: 自检 DoD**

逐条核对：
- [x] 24 条引用全部预扫完成
- [x] 🔴/⚫ 数 ≤ 5（如超额，记入风险，但**不阻塞**进入阶段 2）

- [x] **Step 2: 向用户报告**

输出（≤200 字）：
- 阶段 1 完成
- 24 条引用预扫结果（🟢/🟡/🔴/⚫ 各 N 条）
- 升入阶段 3 的反证议题数
- 是否进入阶段 2（默认是）

等待用户确认或叫停。

---

## Phase 2 — 业界 + 学术证据库（已完成 ✅）

> **阶段 2 完成记录（2026-05-04）**
> - 8 轮 deep research 全部完成（批 1 R1–R4 / 批 2 R5–R8），全部入对应 domain 笔记本
> - 笔记本入库增量：colav_algorithms +91 / safety_verification +64 / maritime_regulations +89 / maritime_human_factors +19
> - R6 subagent 中途被 kill：dedup 后通过 nlm-ask 重建报告（手工写入 03-evidence-base.md）
> - R7 因路由两个笔记本（maritime_regulations + safety_verification），按"先 maritime_regulations 入库 → 再切 safety_verification"两阶段执行
> - 5 个推翻信号（已留至阶段 3 集中修补）：
>   1. R8 Rødseth 2022 = 二轴 OE，v1.0 三轴 H 轴无来源
>   2. R12 Jackson 2021 无"100×简单"量化（工业实践 50:1~100:1）
>   3. R6 IMO MASS Code 第 15 章 = 维护，ODD 在 Part 2 Ch.1
>   4. R5 SAT "全展示降负荷"违反 USAARL 2026-02 透明度悖论
>   5. R8 多船型 Capability Manifest 无海事专用标准（YAML / JSON / DDS-X 三选一可行）
> - 交付物：`audit/2026-04-30/03-evidence-base.md`（530 行 + 跨主题观察 + 推翻信号清单）

**目标**：8 轮 NLM deep research，建立 ≥3 工业 + ≥3 学术证据 / 主题，全部入对应 domain 笔记本。

**并行策略**：分 **2 批 × 4 并发**。批 1：R1/R2/R3/R4（COLAV 工业 + COLAV 学术 + ODD + Doer-Checker）。批 2：R5/R6/R7/R8（SAT + IMO MASS + CCS/DNV + Capability Manifest）。

### Task T2.1: 准备 03-evidence-base.md 骨架

**Files:**
- Create: `audit/2026-04-30/03-evidence-base.md`

- [x] **Step 1: 写入文件头 + 8 个空章节**

```markdown
# 阶段 2 · 业界 + 学术证据库

> 8 轮 NLM deep research 摘要。每轮入对应 domain 笔记本（详见路由）。
>
> 工作模式：subagent (general-purpose, Haiku 4.5, run_in_background)，2 批 × 4 并发。

---

## 路由表

| 轮 | 主题 | 入库笔记本 |
|---|---|---|
| R1 | 工业 COLAV 系统综述 | colav_algorithms |
| R2 | 学术 COLAV 主流路线 | colav_algorithms |
| R3 | ODD/Operational Envelope 工程实践 | maritime_regulations |
| R4 | Doer-Checker 海事/航空 SIL 落地 | safety_verification |
| R5 | SAT 模型 ROC 实证 | maritime_human_factors |
| R6 | IMO MASS Code MSC 110/111 | maritime_regulations |
| R7 | CCS i-Ship + DNV-CG-0264 | maritime_regulations + safety_verification |
| R8 | Capability Manifest / Backseat Driver | colav_algorithms |

---

## R1: 工业 COLAV 系统综述
（待 subagent 回报）

## R2: 学术 COLAV 主流路线
（待 subagent 回报）

## R3: ODD / Operational Envelope 工程实践
（待 subagent 回报）

## R4: Doer-Checker 海事 / 航空 SIL 落地
（待 subagent 回报）

## R5: SAT 模型 ROC 实证
（待 subagent 回报）

## R6: IMO MASS Code MSC 110 / 111
（待 subagent 回报）

## R7: CCS i-Ship + DNV-CG-0264
（待 subagent 回报）

## R8: Capability Manifest / Backseat Driver
（待 subagent 回报）

---

## 跨主题观察

（全部回报后整理）
```

- [x] **Step 2: 保存验证**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/03-evidence-base.md"
```

---

### Task T2.2: 启动批 1（R1–R4 并发，subagent × 4）

**Files:**
- Spawn: 4 个 general-purpose subagent
- Read: `.nlm/config.json` 确认笔记本 key

- [x] **Step 1: 同 message 内并发启动 4 个 subagent**

每个用"通用约定 / Deep Research"模板。具体主题：

**R1 — colav_algorithms**
```
主题: 工业 COLAV 系统综述（Kongsberg / Wärtsilä / Sea Machines / Avikus / MUNIN / Yara Birkeland / ReVolt / Maersk Mayflower）。
关注：每个系统的模块分解、Doer-Checker 实现、实船验证案例、公开材料链接。
```

**R2 — colav_algorithms**
```
主题: 学术 COLAV 主流路线对比（MPC-based / VO-based / IvP-based / RL-based）。
关注：方法论实证差异、性能 / 可证明性 trade-offs、在 SIL 系统中的可接受度、近 5 年综述文献。
```

**R3 — maritime_regulations**
```
主题: ODD / Operational Envelope 在海事工程的最新实践。
关注：Rødseth 2022 之后是否有更新；ENC / COLREGs / 天气 ODD 边界的工程映射；MUNIN / SAFEMASS 项目的 ODD 落地。
```

**R4 — safety_verification**
```
主题: Doer-Checker 在海事/航空 SIL 2-3 系统的真实落地案例。
关注：Boeing 777 PFCS、NASA F/A-18 Auto GCAS、海事 Sea Machines / Avikus 的 Checker 路径；MIT Certified Control 进展；100× 简化的现实可达性。
```

记录 4 个 `agentId`。

> **⚠️ 经验记录（2026-04-30）**：R1 和 R2 均路由 `colav_algorithms`，并发时 R2 先完成，R1 完成通知丢失。结论：**同一笔记本同时只能进行一轮 nlm-research，必须串行**。批 2 中 R6/R7 均路由 `maritime_regulations`，须先完成 R6，再启动 R7。

- [x] **Step 2: 主 agent 进入下一任务（不等待）**

进入 T2.3。

---

### Task T2.3: 批 1 等待期 — 准备阶段 3 阅读顺序

**Files:**
- Create: `audit/2026-04-30/_temp/phase3-reading-order.md`

- [x] **Step 1: 复读 v1.0 章节结构**

```bash
grep -n '^##' "docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md"
```

- [x] **Step 2: 按 D3 优先 / D1 次之 / D9 收尾的策略列阅读顺序**

写到 `_temp/phase3-reading-order.md`：

```markdown
# 阶段 3 阅读顺序（spec §13.3 建议）

理由：M7 (D3 Doer-Checker 独立性) 是 P0 级风险，先看；4 决策 (D1) 是基石，第二轮；模块按依赖逆序。

1. 第 11 章 M7 Safety Supervisor — D3 主审
2. 第 2 章 4 决策 — D1 主审
3. 第 4 章 系统架构总览 — D5 时间尺度
4. 第 3 章 ODD 框架 — D6 降级路径
5. 第 6 章 M2 World Model — D4 接口契约（Multimodal Fusion 输入）
6. 第 5 章 M1 ODD/Envelope Manager — D6 + D1 决策一
7. 第 8 章 M4 Behavior Arbiter — D9 IvP 选型
8. 第 10 章 M5 Tactical Planner — D5 + D9 (Mid-MPC + BC-MPC 双层)
9. 第 9 章 M6 COLREGs Reasoner — D9 (独立模块 vs 嵌入)
10. 第 7 章 M3 Mission Manager — D9 (vs L1 Mission Layer 命名冲突)
11. 第 12 章 M8 HMI/Transparency Bridge — D7 (SAT)
12. 第 13 章 多船型设计 — D8
13. 第 14 章 CCS 入级映射 — D7
14. 第 15 章 接口契约总表 — D4 横扫
15. 第 1 章 背景 — 兜底（FCB 特殊性等）

所有章节出 finding 草拟到 _temp/findings-raw.md。
```

- [x] **Step 3: 保存验证**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/_temp/phase3-reading-order.md"
```

---

### Task T2.4: 收集批 1 输出 (R1–R4)

**触发条件**：T2.2 的 4 个 subagent 全部回报完成。

**Files:**
- Modify: `audit/2026-04-30/03-evidence-base.md`

- [x] **Step 1: 把 R1 输出粘贴到 03-evidence-base.md `## R1` 章节**

格式按 subagent prompt 中定义的 markdown 模板。

- [x] **Step 2–4: 同样处理 R2, R3, R4**

- [x] **Step 5: 验证每轮的"≥3 工业 + ≥3 学术 A/B 级证据"硬门槛**

如果某轮证据 < 阈值，标记 FOLLOWUP：在 `99-followups.md`（如未创建则先创建）添加：

```markdown
- [FOLLOWUP-PHASE2] R{x} 证据不足（{found-industrial} 工业 + {found-academic} 学术），需在阶段 3 触发补充调研
```

- [x] **Step 6: 保存 + 行数验证**

```bash
wc -l "docs/Design/Architecture Design/audit/2026-04-30/03-evidence-base.md"
```

实际：269 行（R1–R4 填充完成）。

> **批 1 完成摘要（2026-04-30）**：R1 colav_algorithms +38 src（26→64）；R2 colav_algorithms +26 src（0→26）；R3 maritime_regulations +51 src；R4 safety_verification +35 src。关键发现：R1 = 4 模块工业标准/Kongsberg K-MATE 需人类审批；R3 = v1.0 E×T×H 三轴无权威来源（Rødseth 2022 二轴）；R4 = "100×"为设计目标，IEC 61508/DO-178C 无定量比值要求。

---

### Task T2.5: 启动批 2（R5–R8 并发）

**Files:**
- Spawn: 4 个 general-purpose subagent

> **⚠️ 串行约束**：R6 和 R7 均路由 `maritime_regulations`，**不可同时启动**。执行顺序：R5 + R6 并发启动（R5 → maritime_human_factors，互不冲突）→ 等 R6 完成 → 再启动 R7（maritime_regulations + safety_verification）→ R8 可与 R7 并发（路由 colav_algorithms）。

- [x] **Step 1: 按串行约束分两轮启动**

**R5 — maritime_human_factors**
```
主题: SAT (Situation Awareness-based Agent Transparency) 模型在海事 ROC 的实证研究。
关注：Veitch 2024 之外的研究；Chen ARL-TR-7180 SAT 三层模型应用；操作员负荷 + 接管时窗 TMR 的最新证据；NTNU Shore Control Lab 成果。
```

**R6 — maritime_regulations**
```
主题: IMO MASS Code MSC 110 / 111 最新条款。
关注：MSC 110 (2025) 输出、MSC 111 (2026-05) 即将通过的非强制版；Operational Envelope 第 15 章要求；4 级模式仲裁；与 SOLAS 衔接。
```

**R7 — maritime_regulations + safety_verification**
```
主题: CCS i-Ship Nx/Ri/Ai 标志认证案例 + DNV-CG-0264 9 子功能在工业的应用。
关注：已有的 i-Ship 入级案例；DNV-CG-0264 §4.2-§4.10 9 子功能的工程映射；CCS 与 DNV 的等效性条款。
```

**R8 — colav_algorithms**
```
主题: 多船型 Capability Manifest / Backseat Driver 范式的工程实例。
关注：MOOS-IvP（Benjamin 2010）的 Backseat Driver 实现；Sea Machines SM300 / SM300-NG 的多船型适配；YAML / JSON Schema 配置范式；水动力插件接口。
```

记录 4 个 `agentId`。

- [x] **Step 2: 主 agent 进入 T2.6**

---

### Task T2.6: 批 2 等待期 — 起草 04-architecture-judgment.md 骨架

**Files:**
- Create: `audit/2026-04-30/04-architecture-judgment.md`

- [x] **Step 1: 写入 13 对象空章节**

```markdown
# 阶段 4 · 架构方案最优性评判（D9 核心）

> 每对象做"v1.0 立场 vs v3.0 Kongsberg 立场 vs 学术最优"三方对照。
> 来源依据：阶段 2 证据库 (`03-evidence-base.md`)。

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

## 1. 决策一: ODD 作为组织原则
（待阶段 4 填写）

## 2. 决策二: 8 模块功能分解（vs Kongsberg 5 模块）
（待阶段 4 填写）

## 3. 决策三: CMM 映射为 SAT-1/2/3
（待阶段 4 填写）

## 4. 决策四: Doer-Checker 双轨（vs X-axis Deterministic Checker）
（待阶段 4 填写）

## 5. M1 ODD / Envelope Manager
（待阶段 4 填写）

## 6. M2 World Model
（待阶段 4 填写）

## 7. M3 Mission Manager（vs L1 Mission Layer 命名冲突）
（待阶段 4 填写）

## 8. M4 Behavior Arbiter（IvP 选择是否最优）
（待阶段 4 填写）

## 9. M5 Tactical Planner（Mid-MPC + BC-MPC）
（待阶段 4 填写）

## 10. M6 COLREGs Reasoner
（待阶段 4 填写）

## 11. M7 Safety Supervisor（vs X-axis Checker）
（待阶段 4 填写）

## 12. M8 HMI / Transparency Bridge
（待阶段 4 填写）

## 13. 多船型 Capability Manifest + Backseat Driver
（待阶段 4 填写）

---

## D9 综合统计

| 对象 | 综合判定 | 行综合分（来自 §9） |
|---|---|---|
| 决策一 | TBD | TBD |
| 决策二 | TBD | TBD |
| ... | ... | ... |
```

- [x] **Step 2: 保存验证**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/04-architecture-judgment.md"
```

---

### Task T2.7: 收集批 2 输出 (R5–R8)

**触发条件**：T2.5 的 4 个 subagent 全部回报完成。

**Files:**
- Modify: `audit/2026-04-30/03-evidence-base.md`

- [x] **Step 1–4: 把 R5–R8 输出粘贴到对应章节**（R6 通过 nlm-ask 重建）

- [x] **Step 5: 同样验证 ≥3+≥3 硬门槛，追加 FOLLOWUP 如需**（R5/R6/R7 全部 🟢，R8 部分 🟡/🔴/⚫，无 FOLLOWUP）

- [x] **Step 6: 写"跨主题观察"章节**

横扫 R1–R8 的相互印证 / 矛盾，写 100–300 字摘要。例：

```markdown
## 跨主题观察

- R1 vs R2：工业系统普遍走 IvP（MOOS-IvP）或 MPC，少有 RL 实船——印证 v1.0 决策（IvP 仲裁）方向
- R4 vs R7：Doer-Checker 在 DNV-CG-0264 9 子功能中映射不直接，需要在 ADR 显式辩护
- ...
```

- [x] **Step 7: 保存验证**

```bash
wc -l "docs/Design/Architecture Design/audit/2026-04-30/03-evidence-base.md"
# → 530 行
```

---

### Task T2.8: 阶段 2 review checkpoint

- [x] **Step 1: 自检 DoD**

- [x] 8 轮 deep research 全部完成（不允许跳）
- [x] 每轮含核心结论 + 推翻信号 + 来源年龄 + 置信度
- [x] ≥3 工业 + ≥3 学术 A/B 级（不达标 → FOLLOWUP）
- [x] 全部入对应 domain 笔记本（subagent 已自动 --add-sources）
- [x] NotebookLM 配额未超

- [x] **Step 2: 向用户报告**

输出（≤300 字）：
- 阶段 2 完成
- 8 轮各置信度（🟢/🟡/🔴/⚫）
- 主要推翻信号摘要（3-5 条最重要的）
- FOLLOWUP 数
- 是否进入阶段 3（默认是）

等待用户确认。**这是 spec §13.3 建议的第一个用户确认点**。

> **用户决议（2026-05-04）**：5 个推翻信号留至阶段 3 集中修补，进入阶段 3。

---

## Phase 3 — Smell-Test 通读（已完成 ✅）

> **阶段 3 完成记录（2026-05-05）**
> - v1.0 全部 15 章 + §16 参考文献通读完毕
> - 阶段 2 证据库 R1–R8 + 5 个推翻信号已系统映射到 finding（每条 finding 引用对应 Rx）
> - **finding 总数 28**：P0=5 / P1=13 / P2=9 / P3=1（外加 4 条延伸注记，无独立 ID）
> - **P0 风险评估**：P0 = 5 < 6（spec §14 触发线），不触发"P0 爆炸→直接走 C 路径"风险
> - **P0 五条**：D3-001 (M7 注轨迹违反 Doer-Checker) / D3-002 (X-axis Checker 协调缺失) / D1-006 (H 轴无来源) / D1-008 (100× 无来源) / D6-015 (三轴公式无来源)
> - **跨章节传播确认**：F-P0-D3-001 在 §15.2 接口契约固化 / F-P1-D5-012 在 §15.2 同样错误 / F-P1-D1-009 同时存在于 §2.3+§14.2
> - 交付物：`audit/2026-04-30/_temp/findings-raw.md`（约 700 行）

**目标**：在阶段 2 证据库支撑下，按章节逐节读 v1.0，沿 D1–D9 抓异常 → finding 草稿。

**章节阅读顺序**：见 `_temp/phase3-reading-order.md`（T2.3 已写）。

**通用 finding 草稿格式**：每发现一处，按 spec §5 八字段写入 `_temp/findings-raw.md`：

```yaml
- id: F-{Pn}-D{n}-{seq}
  locator: { file: ..., section: ..., lines: ... }
  what: ...
  why: ...
  evidence:
    internal: [...]
    external: [...]   # 引用阶段 2 证据库的 R{x} 章节
  confidence: ...
  severity: ...
  recommended_fix: { type: ..., target: ..., proposed_text: ... }
```

**自检规则**（每章读完做）：
- 该章涉及 D1–D9 的哪些维度？是否每维度都扫到？
- 是否调用了证据库 R1–R8 中的相关证据？
- finding 数 = 0 是否合理（小章节 OK，大章节 = 可疑，需复审）

### Task T3.1: 创建 findings-raw.md + audit-report.md 骨架

**Files:**
- Create: `_temp/findings-raw.md`
- Create: `audit/2026-04-30/05-audit-report.md`

- [x] **Step 1: 写 _temp/findings-raw.md 头**

```markdown
# 阶段 3 Findings 草稿

> 通读 v1.0 各章产出的 finding 暂存。阶段 6 综合时分类、去重、定稿到 05-audit-report.md。

---
```

- [x] **Step 2: 写 05-audit-report.md 骨架**

```markdown
# 审计报告

> v1.0 (`MASS_ADAS_L3_TDL_架构设计报告.md`) 全维度审计结论。
> Severity 模型见 spec §4。每条 finding 含 §5 八字段。

---

## 摘要统计

| Severity | 数量 |
|---|---|
| P0 阻断 | TBD |
| P1 重要 | TBD |
| P2 改进 | TBD |
| P3 备忘 | TBD |

---

## P0 Findings
（待阶段 6 定稿）

## P1 Findings
（待阶段 6 定稿）

## P2 Findings
（待阶段 6 定稿）

## P3 Findings
（待阶段 6 定稿）
```

- [x] **Step 3: 保存验证**

---

### Task T3.2: 通读第 11 章 M7 Safety Supervisor（D3 主审）

**Files:**
- Read: `MASS_ADAS_L3_TDL_架构设计报告.md` 行 738–810
- Modify: `_temp/findings-raw.md`

- [x] **Step 1: 读 §11.1–§11.5 全部**

- [x] **Step 2: 沿 D3 (Doer-Checker 独立性) 逐节扫**

关键问题（spec §3 D3 + §10.3 触发条件）：
- M7 是否真"100× 简单"？章节里有量化吗？
- M7 vs M1–M6 的实现路径独立性如何保障？是否有"共享代码 / 库 / 数据结构"的描述？
- M7 与 v3.0 X-axis Deterministic Checker（基线 HTML）的关系是什么？是同一物 / 互补 / 冲突？
- M7 SIL 分配是否与§14 CCS 映射一致？

- [x] **Step 3: 沿 D1 (决策可证伪) 扫**

- 决策四 (Doer-Checker 双轨) 在本章的体现是否完整？
- [R12] (Jackson 2021 Certified Control) 是否真支持 100× 论断？（联动阶段 1 R12 预扫结果）

- [x] **Step 4: 沿 D6 (降级路径) 扫**

- M7 自身在 DEGRADED / CRITICAL 下行为？
- M7 失效时谁兜底？

- [x] **Step 5: 沿 D7 (合规映射) 扫**

- §11.4 SIL 分配建议 vs IEC 61508 SIL 2 要求

- [x] **Step 6: 把每一处异常写入 _temp/findings-raw.md**

按 spec §5 八字段。每条引用阶段 2 证据库（特别是 R4 Doer-Checker 海事/航空 SIL）。

预估 finding 数：3–6 条（M7 是 P0 级 D3 重点）。

- [x] **Step 7: 保存**

---

### Task T3.3: 通读第 2 章 4 决策（D1 主审）

**Files:**
- Read: `MASS_ADAS_L3_TDL_架构设计报告.md` 行 83–130

- [x] **Step 1: 读 §2.1–§2.5 全部**

- [x] **Step 2: 对每个决策（一/二/三/四）做 D1 五问**

- 替代方案分析是否充分？（现有 §2 已有候选 A/B 表，是否足够）
- 论据引用是否真支持决策？（联动阶段 1 + 阶段 2）
- 替代方案的弃用理由是否站得住？
- 是否与 v3.0 Kongsberg 立场对齐 / 冲突？
- 业界主流 (R1) 与学术最优 (R2) 与本决策一致吗？

- [x] **Step 3: 决策一专项 (ODD 组织原则)**

调用 R3 (ODD 工程实践)。Rødseth 2022 之外有没有更新？组织原则 vs 监控模块的工程证据强度？

- [x] **Step 4: 决策二专项 (8 模块分解)**

调用 R1 (工业 COLAV)。**核心质疑**：v3.0 Kongsberg L3 只 5 模块（Target Tracker / COLREGs Engine / Avoidance Planner / Risk Monitor + Capability Assessment），v1.0 拆 8 个 (M1–M8)。8 vs 5 哪个更优？预案是 D9 阶段 4 详评，本任务先记 finding。

- [x] **Step 5: 决策三专项 (CMM-SAT)**

调用 R5 (SAT 实证)。

- [x] **Step 6: 决策四专项 (Doer-Checker)**

调用 R4 (Doer-Checker 落地)。"100× 简单"在 R4 中有支撑吗？

- [x] **Step 7: 写入 _temp/findings-raw.md + 保存**

预估：6–10 条 finding（决策章节是 D1 主战场）。

---

### Task T3.4: 通读第 4 章 系统架构总览（D5 时间尺度）

**Files:**
- Read: `MASS_ADAS_L3_TDL_架构设计报告.md` 行 237–313

- [x] **Step 1: 读 §4.1–§4.4**

- [x] **Step 2: 重点 §4.3 时间分层 + 调用频率**

构建表：每模块声明频率 vs 实际可达性？M2 World Model 10–50 Hz 与 M5 Tactical Planner 1–4 Hz 的下游传递如何？

- [x] **Step 3: 与 v3.0 Reflex Arc <500ms 兼容性**

v3.0 Y-axis Reflex Arc 跳过 L3。v1.0 第 4 章是否考虑？M2 / M7 是否要感知 Reflex 触发？

- [x] **Step 4: §4.4 数据总线设计**

DDS 选型论据？强类型 + stamp + confidence + rationale 是否在 §15 接口契约里有 schema 实例？

- [x] **Step 5: 写入 _temp/findings-raw.md + 保存**

预估：2–4 条 finding。

---

### Task T3.5: 通读第 3 章 ODD 框架（D6 降级路径）

**Files:**
- Read: 行 132–235

- [x] **Step 1: 读 §3.1–§3.6**

- [x] **Step 2: §3.2 三轴 (E×T×H) 定义 vs Rødseth 2022 (R3)**

H 轴 (Human Availability) 是 v1.0 创新还是承袭？

- [x] **Step 3: §3.3 四个 ODD 子域 (A/B/C/D)**

阈值数字（CPA ≥ 1.0nm 等）是否有出处？是否与 R6 (IMO MASS Code) 对齐？

- [x] **Step 4: §3.4 TMR/TDL 公式**

简化公式 `TDL = min(TCPA × 0.6, T_comm_ok, T_sys_health)` 中的 0.6 是怎么来的？联动阶段 1 R4 (Veitch 2024)。

- [x] **Step 5: §3.5 ODD 状态机 + §3.6 边界检测**

状态机 transition 的完备性？margin < 20% 的 EDGE 状态阈值有出处吗？

- [x] **Step 6: 写入 + 保存**

预估：3–5 条 finding（ODD 是核心，论证密度高）。

---

### Task T3.6: 通读第 6 章 M2 World Model（D4 接口契约）

**Files:**
- Read: 行 422–508

- [x] **Step 1: 读 §6.1–§6.4**

- [x] **Step 2: 输入契约 vs Multimodal Fusion 实际输出（v3.0 Kongsberg）**

v1.0 §6 提"L1 感知层" — **已知错误（系统坐标系修订前留下的）**。Multimodal Fusion 输出是 Track 级 + Nav Filter 自船状态，v1.0 是否正确消费？

- [x] **Step 3: 三视图（静态/动态/自身）的 schema 完整性**

每视图的字段、置信度、更新频率？

- [x] **Step 4: COLREG 几何预分类**

是 M2 还是 M6 的责任？模块边界冲突？

- [x] **Step 5: 写入 + 保存**

预估：3–5 条 finding（含 1–2 条层级名错误的 P1）。

---

### Task T3.7: 通读第 5 章 M1 ODD/Envelope Manager（D6 + D1 决策一）

**Files:**
- Read: 行 315–420

- [x] **Step 1: 读 §5.1–§5.5**

- [x] **Step 2: M1 是"调度枢纽"的论据**

§5 是否实现了"ODD 状态变化是行为切换的唯一权威"？还是有其他模块也在做模式判断？

- [x] **Step 3: M1 的输出契约 vs M4 Behavior Arbiter**

M1 → M4 的 mode 信息频率 / 置信度？

- [x] **Step 4: 写入 + 保存**

预估：2–4 条 finding。

---

### Task T3.8: 通读第 8 章 M4 Behavior Arbiter（D9 IvP 选型）

**Files:**
- Read: 行 545–597

- [x] **Step 1: 读 §8.1–§8.5**

- [x] **Step 2: IvP 选型 vs R2 学术最优**

阶段 2 R2 应该已涵盖 IvP / MPC / VO / RL 对比。v1.0 选 IvP 是否最优？

- [x] **Step 3: 行为字典完整性**

行为列表是否覆盖 ODD-A/B/C/D 四子域所有任务？

- [x] **Step 4: 与 M6 COLREGs Reasoner 协作**

§8.4 描述了协作，但消息流方向（M6 → M4 还是双向）是否清晰？

- [x] **Step 5: 写入 + 保存**

预估：2–4 条 finding。

---

### Task T3.9: 通读第 10 章 M5 Tactical Planner（D5 + D9 双层 MPC）

**Files:**
- Read: 行 655–736

- [x] **Step 1: 读 §10.1–§10.6**

- [x] **Step 2: Mid-MPC ≥90s 的合理性**

v1.0 §1.4 用 FCB 18kn / 600–800m 制动距离论证 90s。R2 / R8 学术最优实践是多少？

- [x] **Step 3: BC-MPC 与 Mid-MPC 的双层切换**

切换条件、切换时序、是否有 race condition？

- [x] **Step 4: §10.5 FCB 高速修正 vs 多船型不侵入 (D8)**

是不是有 FCB 特定参数硬编码到 M5？还是真做到了 capability manifest？

- [x] **Step 5: 写入 + 保存**

预估：3–5 条 finding。

---

### Task T3.10: 通读第 9 章 M6 COLREGs Reasoner（D9 独立 vs 嵌入）

**Files:**
- Read: 行 599–653

- [x] **Step 1: 读 §9.1–§9.4**

- [x] **Step 2: 独立模块 vs 嵌入 Avoidance Planner 的工程证据**

阶段 2 R1 Kongsberg / Avikus / Sea Machines 怎么做？

- [x] **Step 3: §9.3 ODD-aware 参数**

CPA / TCPA 阈值在 ODD-A/B/C/D 四子域的具体值是否完整？是否能审计？

- [x] **Step 4: 写入 + 保存**

预估：2–4 条 finding。

---

### Task T3.11: 通读第 7 章 M3 Mission Manager（D9 + 命名冲突）

**Files:**
- Read: 行 509–543

- [x] **Step 1: 读 §7.1–§7.3**

- [x] **Step 2: 关键审计——M3 vs L1 Mission Layer 命名冲突**

v3.0 Kongsberg 中 L1 是 Mission Layer。v1.0 把内部模块叫 M3 Mission Manager，且 §7.2 的 INPUT 是 "战略层L2 航次计划输入"——同时引用错的层名 + 与 L1 命名重复。

- [x] **Step 3: 边界审视**

L1 (Mission Layer) 与 M3 (Mission Manager) 谁负责什么？是否有职责重叠？

- [x] **Step 4: 写入 + 保存**

预估：2–4 条 finding（含命名冲突的 P1）。

---

### Task T3.12: 通读第 12 章 M8 HMI/Transparency Bridge（D7 SAT）

**Files:**
- Read: 行 811–887

- [x] **Step 1: 读 §12.1–§12.5**

- [x] **Step 2: SAT-1/2/3 实现细节 vs R5 实证**

每模块的 `current_state()` / `rationale()` / `forecast()` 接口是否真都列了？

- [x] **Step 3: §12.4 责任移交协议 vs Veitch 2024 TMR ≥60s**

§12.4 是否有"<60s 接管时拒绝移交"的规则？

- [x] **Step 4: 差异化视图（ROC vs 船长）**

视图差异是否有出处？

- [x] **Step 5: 写入 + 保存**

预估：2–4 条 finding。

---

### Task T3.13: 通读第 13–15 章（多船型 + CCS + 接口）

**Files:**
- Read: 行 889–1106

- [x] **Step 1: §13 多船型设计 (D8 主战场)**

- §13.1–§13.5 全读
- 重点：§13.4 水动力插件接口 vs R8 工程实例
- "决策核心代码零船型常量" 在 §13 描述是否充分？

- [x] **Step 2: §14 CCS 入级 (D7)**

- §14.2 DNV-CG-0264 9 子功能 100% 覆盖矩阵 vs R7
- §14.3 i-Ship 申请路径 vs R7 既有案例
- §14.4 关键证据文件清单完整性

- [x] **Step 3: §15 接口契约总表 (D4)**

- §15.1 核心消息定义 schema 完整性
- §15.2 接口矩阵 vs Zulip 各层（阶段 5 跨层会再深入，本任务先标 finding）

- [x] **Step 4: 写入 + 保存**

预估：5–8 条 finding。

---

### Task T3.14: 通读第 1 章 + 兜底扫尾

**Files:**
- Read: 行 1–82

- [x] **Step 1: §1.1–§1.4 全读**

- [x] **Step 2: §1.2 设计边界表 — 已知层级名错误（L1 感知层 / L2 战略层）**

记录 P1 finding（已经在 CLAUDE.md 修订时识别，但需要在 patch list 里 fix）。

- [x] **Step 3: §1.3 关键约束 [R1]–[R6]**

每条 [Rx] 联动阶段 1 R 预扫结果。

- [x] **Step 4: §1.4 FCB 特殊性 vs D8**

FCB 边界测试船型是否影响"零船型常量"承诺？

- [x] **Step 5: 全文兜底——任何前面 12 任务没扫到的章节再过一遍**

特别是图（mermaid）。

- [x] **Step 6: 写入 + 保存**

预估：3–5 条 finding。

---

### Task T3.15: 阶段 3 review checkpoint

- [x] **Step 1: 统计 _temp/findings-raw.md 的 finding 总数**

```bash
grep -c '^- id:' "docs/Design/Architecture Design/audit/2026-04-30/_temp/findings-raw.md"
```

- [x] **Step 2: 按 Severity 分类**

```bash
grep -c 'severity: P0' findings-raw.md
grep -c 'severity: P1' findings-raw.md
grep -c 'severity: P2' findings-raw.md
grep -c 'severity: P3' findings-raw.md
```

- [x] **Step 3: 自检 DoD（部分）**

- [x] 每条 P0/P1 finding 置信度 ≥ 🟡（如有 🔴/⚫，回头补阶段 3+ 调研）
- [x] 每条 P0/P1 finding 配 fix 建议
- [x] 每章节都扫过（看 Task T3.2–T3.14 全部勾完）

- [x] **Step 4: 向用户报告**

输出（≤300 字）：
- 阶段 3 完成
- finding 总数 + Severity 分布
- 是否触发"P0 数量爆炸"风险（spec §14）：P0 ≥ 6 即 spec 风险触发，建议直接走三态 C
- 是否进入阶段 4（默认是；如果 P0 ≥ 6 用户可能选择跳过 4 直接走 6）

等待用户确认。

---

## Phase 4 — D9 三方对照评判（已完成 ✅）

> **阶段 4 完成记录（2026-05-05）**
> - 13 对象（4 顶层决策 + 9 模块/多船型）全部完成五段对照（v1.0 + v3.0 + 学术 + 工业 + 综合判定）
> - **保留 4** / **修订 9** / **替换 0**
> - **保留**：M4 Behavior Arbiter（IvP）、M5 Tactical Planner（双层 MPC + BC-MPC）、M6 COLREGs Reasoner、多船型 Capability Manifest
> - **修订**：4 个决策（决策一/二/三/四）、M1、M2、M3（重命名 + 缩范围）、M7（与 X-axis 分层）、M8（自适应 SAT 触发）
> - **关键观察**：
>   1. 算法核心（M4/M5/M6）三模块完全保留——v1.0 最强部分
>   2. 修订压力集中在"L3 内部模块 vs 系统层职责"分层关系
>   3. 阶段 6 决策树预测走 **A 路径（v1.1 patch）**——P0=5 < 6 阈值，不需要 v2.0 替换或 C 路径重写
> - 交付物：`audit/2026-04-30/04-architecture-judgment.md`（约 380 行）

**目标**：13 对象逐项做"v1.0 立场 vs v3.0 Kongsberg vs 学术最优"对照，给综合判定（保留/修订/替换）。

**输入**：
- v1.0 第 2 / 5–13 章
- 阶段 2 证据库（03-evidence-base.md）
- v3.0 Kongsberg HTML（已读过）
- 阶段 3 findings（_temp/findings-raw.md）

### Task T4.1: 决策一 — ODD 作为组织原则

**Files:**
- Modify: `04-architecture-judgment.md` `## 1. 决策一` 章节

- [x] **Step 1: v1.0 立场（从 §2.2 抄出核心论断）**

- [x] **Step 2: v3.0 Kongsberg 立场**

参考 Kongsberg HTML：v3.0 没有显式的"ODD 作为组织原则"声明，而是用"Adjacent-layer nominal" + 各层独立时间尺度。但 Z-TOP 网络安全 + Z-BOTTOM 硬件覆写 + X-axis Checker 都是组织原则。"ODD 是不是组织原则"在 v3.0 角度看其实是开放的。

- [x] **Step 3: 学术最优（调阶段 2 R3）**

- [x] **Step 4: 工业其他参考（调 R1）**

- [x] **Step 5: 综合判定**

填入：保留 / 修订 / 替换 + 理由 + 影响范围。

- [x] **Step 6: 保存**

---

### Task T4.2: 决策二 — 8 模块功能分解（vs Kongsberg 5 模块）

**Files:**
- Modify: `04-architecture-judgment.md` `## 2. 决策二` 章节

- [x] **Step 1: v1.0 立场（M1–M8）**

8 模块 + 三层组织（包络 / 决策规划 / 安全接口）

- [x] **Step 2: v3.0 Kongsberg 立场（5 模块）**

L3 内部 5 模块（Target Tracker / COLREGs Engine / Avoidance Planner / Risk Monitor + Capability Assessment）。Capability Assessment 是 v3.0 新加（v3.0 标记 nt）。

**核心问题**：8 模块多出来的 (M1 ODD Manager / M3 Mission Manager / M7 Safety Supervisor) 在 Kongsberg 5 模块里是不是被合并到了**层级架构**而非模块层？例：
- M1 ODD Manager → 整个系统的 Z-TOP/Z-BOTTOM 通过 IACS UR + ASDR + Hardware Override 实现，不是 L3 内部的"模块"
- M3 Mission Manager → L1 Mission Layer 的职责
- M7 Safety Supervisor → X-axis Deterministic Checker 的职责

如果上述对应成立，**v1.0 在 L3 内部重复实现了系统层职责**。这是 D2 决策的核心 P0 风险。

- [x] **Step 3: 学术最优（调 R2）**

- [x] **Step 4: 工业其他参考（调 R1 + R8）**

MOOS-IvP / Sea Machines / Avikus 内部模块切分？

- [x] **Step 5: 综合判定**

填入：很可能"修订"或"替换"——8 → 5（剥离 M1/M3/M7 到系统层）。

- [x] **Step 6: 保存**

---

### Task T4.3: 决策三 — CMM 映射为 SAT-1/2/3

**Files:**
- Modify: `04-architecture-judgment.md` `## 3. 决策三` 章节

- [x] **Step 1: v1.0 立场**

- [x] **Step 2: v3.0 Kongsberg 立场**

v3.0 没有显式 SAT 模型，但 Shore Link + Override Status (IMO MASS 4-level) 是其透明性载体。

- [x] **Step 3: 学术最优（调 R5）**

Chen ARL-TR-7180 + Veitch 2024。

- [x] **Step 4: 工业其他参考**

Sea Machines SM300 接管 UI、Avikus HiNAS UI 是否实现了 SAT-1/2/3？

- [x] **Step 5: 综合判定**

- [x] **Step 6: 保存**

---

### Task T4.4: 决策四 — Doer-Checker 双轨（vs X-axis Checker）

**Files:**
- Modify: `04-architecture-judgment.md` `## 4. 决策四` 章节

- [x] **Step 1: v1.0 立场**

M1–M6 = Doer，M7 = Checker。100× 简单 + 实现路径独立。

- [x] **Step 2: v3.0 Kongsberg 立场（X-axis Deterministic Checker）**

v3.0 X-axis 是**系统层**的 Checker，对 L2/L3/L4/L5 都有 VETO。每层的 Checker 子项：
- Route Safety Gate ← L2
- COLREGs Compliance ← L3
- Corridor Guard ← L4
- Actuator Envelope ← L5

**核心问题**：v1.0 把 Checker 实现为 L3 内部的 M7（一个模块）；v3.0 把 Checker 实现为系统层的独立轴（跨 L2-L5）。**两种架构的 Checker 哲学不同**。哪个更好？

- [x] **Step 3: 学术最优（调 R4）**

Boeing 777 / NASA Auto GCAS / MIT Certified Control 怎么做？是模块级还是系统级？

- [x] **Step 4: 工业其他参考（调 R1）**

- [x] **Step 5: 综合判定**

很可能"替换"——L3 内部 M7 → 系统层 X-axis（与决策二修订连带）。

- [x] **Step 6: 保存**

---

### Task T4.5: M1 ODD / Envelope Manager 评判

**Files:**
- Read: v1.0 §5（行 315–420）
- Modify: `04-architecture-judgment.md` `## 5. M1` 章节

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | v1.0 §5.1–§5.3：M1 是"调度枢纽"，唯一权威 ODD 状态来源 |
| v3.0 对应 | v3.0 没有 M1 等价模块；ODD 类似职责被 IACS UR + 各层 Mode Switcher 分担 |
| 学术 | 阶段 2 R3 (ODD 工程实践) |
| 工业 | 阶段 2 R1 (工业 COLAV 系统是否有 ODD-as-枢纽 的实例) |

- [x] **Step 1: 抄 v1.0 §5 立场**
- [x] **Step 2: 写 v3.0 立场（注：v3.0 中 ODD 职责分布于多层，不集中）**
- [x] **Step 3: 调阶段 2 R3 + R1 写学术 / 工业证据**
- [x] **Step 4: 综合判定（保留 / 修订 / 替换）+ 与 T4.2 决策二的修订连带说明**
- [x] **Step 5: 保存**

---

### Task T4.6: M2 World Model 评判

**Files:**
- Read: v1.0 §6（行 422–508）

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | 唯一权威世界视图（静态/动态/自身），含 COLREG 几何预分类 |
| v3.0 对应 | v3.0 Multimodal Fusion → Situational Awareness 层 + Nav Filter EKF；可能与 M2 重叠/分工 |
| 学术 | 阶段 2 R2 (学术 COLAV) |
| 工业 | 阶段 2 R1（World Model 在 Sea Machines/Avikus 等如何实现） |

- [x] **Step 1: 抄 v1.0 §6 立场**
- [x] **Step 2: 写 v3.0 立场（注：M2 输入要消费 Multimodal Fusion 的 Track 级 + Nav Filter，不是"L1 感知层"）**
- [x] **Step 3: 调 R1 + R2 写证据**
- [x] **Step 4: 综合判定**
- [x] **Step 5: 保存**

---

### Task T4.7: M3 Mission Manager 评判（命名冲突核心）

**Files:**
- Read: v1.0 §7（行 509–543）
- Read: v3.0 Kongsberg HTML L1 描述

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | L3 内部模块；负责航次计划、ETA、重规划触发；上游来自"L2 战略层" |
| v3.0 对应 | L1 Mission Layer（系统层！），负责 voyage order + weather routing + ETA/fuel opt。**v1.0 M3 与 L1 命名冲突 + 职责重叠** |
| 学术 | 阶段 2 R8 (Backseat Driver) |
| 工业 | 阶段 2 R1（Kongsberg/Wärtsilä 是否在 L3 内有"Mission Manager"实例） |

- [x] **Step 1: 抄 v1.0 §7 立场**
- [x] **Step 2: 描述 v3.0 L1 vs M3 命名 / 职责冲突**
- [x] **Step 3: 调 R1 写工业证据（L3 内是否真需要 Mission Manager）**
- [x] **Step 4: 综合判定（很可能"替换"——M3 → 移到 L1 Mission Layer，或重命名为 M3 Voyage Tracker）**
- [x] **Step 5: 保存**

---

### Task T4.8: M4 Behavior Arbiter 评判（IvP 选型）

**Files:**
- Read: v1.0 §8（行 545–597）

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | 行为字典 + IvP 多目标仲裁 |
| v3.0 对应 | v3.0 L3 5 模块中 Avoidance Planner 隐含行为决策；不显式分模块 |
| 学术 | 阶段 2 R2（IvP vs MPC vs VO vs RL 主流路线对比）→ **核心证据来源** |
| 工业 | 阶段 2 R1 + R8（MOOS-IvP 是 Backseat Driver 创始；Sea Machines / Avikus 是否用 IvP？） |

- [x] **Step 1: 抄 v1.0 §8 立场**
- [x] **Step 2: 写 v3.0 立场（v3.0 简化为单 Avoidance Planner 模块）**
- [x] **Step 3: 调 R2 验证 IvP 是否仍是工业最优；调 R8 看 IvP 工程证据**
- [x] **Step 4: 综合判定（保留 IvP 还是改 MPC/VO？）**
- [x] **Step 5: 保存**

---

### Task T4.9: M5 Tactical Planner 评判（双层 MPC）

**Files:**
- Read: v1.0 §10（行 655–736）

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | Mid-MPC（≥90s）+ BC-MPC 双层；输出 (ψ, u, ROT) 至 L4 |
| v3.0 对应 | v3.0 L3 5 模块中 Avoidance Planner 输出"4-WP"，不分双层 MPC |
| 学术 | 阶段 2 R2（双层 MPC vs 单层 MPC vs hybrid） |
| 工业 | 阶段 2 R1（哪家工业系统真用了双层 MPC） |

- [x] **Step 1: 抄 v1.0 §10 立场**
- [x] **Step 2: 写 v3.0 立场（v3.0 用 4-WP 直接出 WP，更简化）**
- [x] **Step 3: 调 R2 比较双层 MPC / 4-WP / hybrid 的工程实证**
- [x] **Step 4: §10.5 FCB 高速修正——是否硬编码船型常量？联动 T4.13 D8 检查**
- [x] **Step 5: 综合判定**
- [x] **Step 6: 保存**

---

### Task T4.10: M6 COLREGs Reasoner 评判

**Files:**
- Read: v1.0 §9（行 599–653）

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | 独立模块，与 M4 协作 |
| v3.0 对应 | v3.0 L3 中 COLREGs Engine 是独立模块（与 v1.0 一致） |
| 学术 | 阶段 2 R2（COLREGs 集成的工程方式：独立 vs 嵌入 Avoidance Planner） |
| 工业 | 阶段 2 R1 |

- [x] **Step 1: 抄 v1.0 §9 立场**
- [x] **Step 2: 写 v3.0 立场（与 v1.0 兼容）**
- [x] **Step 3: 调 R2 看学术界对独立 COLREGs 模块的评价**
- [x] **Step 4: §9.3 ODD-aware 参数完整性**
- [x] **Step 5: 综合判定（很可能"保留"或小"修订"）**
- [x] **Step 6: 保存**

---

### Task T4.11: M7 Safety Supervisor 评判（决策四连带）

**Files:**
- Read: v1.0 §11（行 738–810）
- Read: v3.0 Kongsberg HTML X-axis Deterministic Checker

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | L3 内部模块（M7），实现 Doer-Checker 中的 Checker；"100× 简单 + 实现路径独立" |
| v3.0 对应 | X-axis Deterministic Checker 是**系统层独立轴**，跨 L2/L3/L4/L5；Doer 不能绕过；VETO + nearest compliant |
| 学术 | 阶段 2 R4（Boeing 777 / NASA Auto GCAS / Certified Control 是模块级还是系统级 Checker） |
| 工业 | 阶段 2 R1（Kongsberg/Wärtsilä 的 Checker 实现模式） |

- [x] **Step 1: 抄 v1.0 §11 立场**
- [x] **Step 2: 详写 v3.0 X-axis 立场**
- [x] **Step 3: 调 R4 关键问题：100× 简单是 v1.0 的论断还是业界共识？**
- [x] **Step 4: 综合判定（很可能"替换"——M7 剥离到系统层 X-axis；与 T4.4 决策四连带）**
- [x] **Step 5: 保存**

---

### Task T4.12: M8 HMI / Transparency Bridge 评判

**Files:**
- Read: v1.0 §12（行 811–887）

| 维度 | 输入锚点 |
|---|---|
| v1.0 立场 | 唯一对 ROC/船长说话的实体；SAT-1/2/3 透明性 |
| v3.0 对应 | Shore Link via DMZ + Override Status (IMO MASS 4-level)；没有显式 SAT |
| 学术 | 阶段 2 R5（SAT 实证 / Veitch 2024 / Chen ARL-TR-7180） |
| 工业 | 阶段 2 R1（Sea Machines/Avikus 的 HMI UI 是否实现了 SAT-1/2/3） |

- [x] **Step 1: 抄 v1.0 §12 立场**
- [x] **Step 2: 写 v3.0 立场（v3.0 没有 SAT 显式实现，但 4-level 模式指示是其简化形式）**
- [x] **Step 3: 调 R5 + R1 写证据**
- [x] **Step 4: §12.4 责任移交协议 vs Veitch TMR ≥60s**
- [x] **Step 5: 综合判定**
- [x] **Step 6: 保存**

**注**：T4.7 (M3) 与 T4.11 (M7) 与 T4.2 决策二、T4.4 决策四的修订紧密关联，可能"替换"——剥离到系统层。**T4.5/T4.6/T4.7/T4.11 的判定不应独立做，做完后回到 T4.2/T4.4 决策评判检查一致性**。

---

### Task T4.13: 多船型 Capability Manifest + Backseat Driver

**Files:**
- Modify: `04-architecture-judgment.md` `## 13` 章节

- [x] **Step 1: v1.0 立场（从 §13 抄）**

- [x] **Step 2: v3.0 Kongsberg 立场**

v3.0 用 Parameter Database (manoeuvring coeffs / stopping / draught / wind/current / thruster config / degraded fallback) + YAML 配置 + Backseat Driver 范式。

- [x] **Step 3: 学术最优 / 工业参考（调 R8）**

MOOS-IvP / Sea Machines。

- [x] **Step 4: 综合判定**

很可能"修订"——v1.0 已含 Capability Manifest 但需要明确与 v3.0 Parameter Database 的对应关系。

- [x] **Step 5: 保存**

---

### Task T4.14: 填 D9 综合统计表 + 04-architecture-judgment.md 收尾

**Files:**
- Modify: `04-architecture-judgment.md` `## D9 综合统计` 表

- [x] **Step 1: 填表 13 行**

每行：对象名、综合判定（保留/修订/替换）、行综合分（占位"待 §6 填"，因为评分矩阵在阶段 6 算）。

- [x] **Step 2: 写一段总论**

13 对象中保留 X / 修订 Y / 替换 Z 个。整体架构方向是否需要重大调整？

- [x] **Step 3: 保存**

```bash
wc -l "docs/Design/Architecture Design/audit/2026-04-30/04-architecture-judgment.md"
```

---

### Task T4.15: 阶段 4 review checkpoint

- [x] **Step 1: 自检 DoD**

- [x] 13 对象全部评判
- [x] 每个保留/采纳决策有 ≥3 工业 + ≥3 学术证据（调阶段 2 R）
- [x] 每个修订/弃用决策有"为什么不行"反证

- [x] **Step 2: 向用户报告**

输出（≤400 字）：
- 阶段 4 完成
- 13 对象的保留 / 修订 / 替换分布
- 关键发现 (3-5 条最重要的)
- 是否进入阶段 5（默认是）

**这是 spec §13.3 建议的第二个用户确认点**。

---

## Phase 5 — 跨文档 + 跨层对照（已完成 ✅）

> **阶段 5 完成记录（2026-05-05）**
> - **执行模式**：4 个 Haiku 4.5 background subagent 并行（T5.1 跨文档 × 1 + T5.2 跨层 × 3）
> - 跨文档对照（vs `docs/Doc From Claude/`）：4 份历史稿全文阅读
>   - 5 条漏吸纳论断（最严重 P1 = L3 → L2 反向通道缺失）
>   - 3 条静默推翻全部带有充分弃用理由（CMM/ODD/M6 独立性）— 无需补救
>   - 4 项术语漂移（TDL/TDCAL、M5/M6 顺序、Stage→Module 映射、Doer-Checker 拆分）
> - 跨层对照（vs `docs/Init From Zulip/` 8 个层级文档）：
>   - **关键发现**：v1.0 §15 接口契约总表完整性是最大缺口（5 条 P1 全部围绕 §15）
>   - M2 ← Multimodal Fusion 频率/schema 不匹配（2 vs 4 Hz；CPA/TCPA 缺失；自身状态独立 50 Hz 话题）
>   - M5 → L4 层级架构错位（L4 自生 ψ/u/ROT，M5 输出冗余/冲突）— 扩展 F-P1-D5-012
>   - §15 矩阵缺 ASDR、缺 Reflex Arc、缺 L3→L2 反向通道
>   - F-P0-D3-002（M7 vs X-axis Checker 协调缺失）通过实际 Checker doc 实证强化
> - **Phase 5 新生 finding 共 12 条**：P1=5（D4-031/032/033/034/035）+ P2=6（D3-036、D6-037、D4-038、D1-039、D9-040/041）+ P3=1（D9-042）
> - **总 finding 累计 40**（Phase 3 = 28 + Phase 5 = 12）；P0 仍 = 5，未触及"P0 爆炸阈值（≥ 6 → 走 C 路径）"
> - 交付物：`audit/2026-04-30/06-cross-doc-check.md`（约 145 行）+ `07-cross-layer-check.md`（约 200 行）

### Task T5.1: 跨文档对照（vs Doc From Claude）

**Files:**
- Read: `docs/Doc From Claude/*.md` (10+ 文件)
- Create: `audit/2026-04-30/06-cross-doc-check.md`

- [x] **Step 1: 列出 docs/Doc From Claude/ 下所有 markdown**

```bash
ls "docs/Doc From Claude/"
```

- [x] **Step 2: 读关键早期稿**

优先：
- `2026-04-27-tactical-colav-research-v1.1.md`
- `2026-04-28-tactical-colav-research-v1.2.md`
- `2026-04-28-tactical-colav-research-patch-d.md`
- `L3 实现框架裁决汇总.md`
- `MASS L3 Tactical Decision Layer (TDL) 功能架构深度研究报告.md`

- [x] **Step 3: 找早期提出但 v1.0 漏吸纳的论断**

每条记 (源文件 / 论断 / v1.0 是否吸纳 / 严重性建议)。

- [x] **Step 4: 找 v1.0 静默推翻早期论断的清单**

每条记 (早期立场 / v1.0 立场 / 是否在 v1.0 给出弃用理由)。

- [x] **Step 5: 找术语漂移**

如"L3 战术决策层 TDL" vs "L3 战术层 Tactical Layer" vs "L3 TDCAL"。

- [x] **Step 6: 写入 06-cross-doc-check.md**

```markdown
# 阶段 5.1 · 跨文档对照（vs Doc From Claude/）

## 漏吸纳论断
（每条 P 级建议）

## 静默推翻清单
（每条标"是否需补弃用理由"）

## 术语漂移
（统一建议）
```

如果 5.3/5.4/5.5 之一为空，写"已对照，无发现"。

- [x] **Step 7: 保存**

---

### Task T5.2: 跨层对照（vs Init From Zulip + v3.0 Kongsberg）

**Files:**
- Read: `docs/Init From Zulip/{L1,L2,L4,L5,Multimodal Fusion,Deterministic Checker,ASDR,...}/*.md`
- Read: v3.0 Kongsberg HTML（已读）
- Create: `audit/2026-04-30/07-cross-layer-check.md`

- [x] **Step 1: 列出 Zulip 下各层 readme/spec**

```bash
find "docs/Init From Zulip/" -name "*.md" -maxdepth 3 | head -40
```

- [x] **Step 2: 抽取每层的输入/输出契约（接口部分）**

为每层（L1/L2/L4/L5/Multimodal Fusion/Deterministic Checker/ASDR）记录：发出什么 / 接收什么 / 频率 / schema。

- [x] **Step 3: 对照 v1.0 §15 接口契约总表**

对每条 v1.0 §15 的接口，验证对应 Zulip 层的契约是否一致（消息名、字段、频率）。

- [x] **Step 4: 关键审计点**

- M2 World Model 输入 vs Multimodal Fusion 实际输出（Track 级 + Nav Filter 自船状态）—— **预期 finding**
- M7 Safety Supervisor vs X-axis Deterministic Checker 是否冲突 / 重叠 —— **预期 P0 finding**
- ASDR 决策日志契约 vs L3 输出格式（v1.0 §15 是否有 ASDR 接口？）
- Reflex Arc <500ms 跳过 L3 时 L3 是否需感知（v1.0 §15 是否有 Reflex 信号入口？）

- [x] **Step 5: 写入 07-cross-layer-check.md**

```markdown
# 阶段 5.2 · 跨层对照（vs Init From Zulip + v3.0 Kongsberg）

## §15 接口契约总表 vs 各层实际契约
（每接口一行，✅/⚠️/❌）

## 关键审计点
### M2 输入 vs Multimodal Fusion 输出
### M7 vs X-axis Checker
### ASDR 接口
### Reflex Arc 感知
```

- [x] **Step 6: 保存**

---

### Task T5.3: 阶段 5 review checkpoint（轻量）

- [x] **Step 1: 自检 DoD**

- [x] 06-cross-doc-check.md 含 ≥1 节（即使 0 finding 也写"已对照"）
- [x] 07-cross-layer-check.md 含 ≥1 节
- [x] 关键审计点 4 项都覆盖（M2/M7/ASDR/Reflex）

- [x] **Step 2: 把跨层 / 跨文档 finding 同步到 _temp/findings-raw.md**

每个 finding 用 spec §5 八字段，severity 与置信度按规则。

- [x] **Step 3: 简短报告（不停下）**

向用户输出 1–2 句"阶段 5 完成，跨层 / 跨文档发现 X 条 finding，进入阶段 6"。

---

## Phase 6 — 综合 + 三态结论（已完成 ✅）

> **阶段 6 完成记录（2026-05-05）**
> - 14×9 评分矩阵：行综合分 1.3–2.9 / 列综合分 0.0–3.0 / **全表综合分 = 2.35**
> - **决策树落点：B 档（结构性修订 + ADR）**
>   - 全表 2.35 ∈ [1.5, 2.5] ✅
>   - P0 = 5 ∈ [3, 5] ✅
>   - 与阶段 4/5 早期"A 档"预判**校正**（早期忽略 A 档 P0 ≤ 2 上限）
> - **关键弱点**：
>   - 最弱对象：决策四 (1.3) → ADR-001；ODD 框架 (1.7) → ADR-003
>   - 最弱维度：D3 Doer-Checker (0.0) / D1 决策可证伪 (1.8) / D6 降级路径 (2.1)
> - **B 档输出物全部完成**：
>   - `00-executive-summary.md`（150 行）
>   - `01-mass-coordinate-system.md`（75 行，CLAUDE.md §2 镜像）
>   - `05-audit-report.md`（440 行，40 finding 定稿）
>   - `08-audit-grid.md`（130 行，14×9 矩阵）
>   - `08a-patch-list.md`（360 行，35 条字段级 diff）
>   - `08c-adr-deltas.md`（270 行，3 条 ADR：决策一 + 决策四 + ODD 框架）
>   - `99-followups.md`（110 行）
>   - `_temp/decision-trace.md`（100 行）
> - **总交付物**：12 个 markdown（00–08c + 99）
> - **DoD 终检**：spec §11 全部 10 项 ✅

**目标**：所有 finding 定稿、评分矩阵、A/B/C 决策、对应输出物。

### Task T6.1: 填 14×9 评分矩阵

**Files:**
- Read: `_temp/findings-raw.md`、`04-architecture-judgment.md`
- Create: `audit/2026-04-30/08-audit-grid.md`

- [x] **Step 1: 写 08-audit-grid.md 头**

```markdown
# 审计评分矩阵（14×9 + 三层综合分）

> 评分量表（按 spec §9.2）：3 通过 / 2 可改进 / 1 需修订 / 0 严重 / N/A 不适用
> 行: 14 项（4 决策 + 8 模块 + ODD 框架 + §15 接口总表）
> 列: D1–D9 九个维度

---

## 评分矩阵

| 对象 \ 维度 | D1 决策可证伪 | D2 引用真实 | D3 Doer-Checker | D4 接口契约 | D5 时间尺度 | D6 降级路径 | D7 合规映射 | D8 多船型 | D9 方案最优 | 行综合分 |
|---|---|---|---|---|---|---|---|---|---|---|
| 决策一 | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| 决策二 | ... |
| 决策三 | ... |
| 决策四 | ... |
| M1 | ... |
| M2 | ... |
| M3 | ... |
| M4 | ... |
| M5 | ... |
| M6 | ... |
| M7 | ... |
| M8 | ... |
| ODD 框架 | ... |
| §15 接口总表 | ... |
| **列综合分** | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD | **全表综合分** |
```

- [x] **Step 2: 对每个 (行, 列) 格子赋分**

- 该 (对象, 维度) 上有 P0 finding 即 0
- 有 P1 finding（无 P0）即 1
- 有 P2 finding（无 P0/P1）即 2
- 无 finding 或仅 P3 即 3
- 不适用即 N/A（spec §9.2 例：§15 接口总表 在 D9；ODD 框架 在 D2 若不依赖独立引用）

- [x] **Step 3: 保存**

---

### Task T6.2: 计算行 / 列 / 全表综合分

**Files:**
- Modify: `08-audit-grid.md`

- [x] **Step 1: 行综合分（每对象一个）**

行综合分 = mean(non-N/A 格子)，保留 1 位小数。

- [x] **Step 2: 列综合分（每维度一个）**

列综合分 = mean(non-N/A 格子)，保留 1 位小数。

- [x] **Step 3: 全表综合分**

mean(全部 non-N/A 格子)，保留 2 位小数。

- [x] **Step 4: 把数字回填到表**

- [x] **Step 5: 写最弱对象 / 最弱维度**

```markdown
## 最弱对象 / 维度

- 最弱对象（行综合分最低）: {对象名} = {分}
- 最弱维度（列综合分最低）: {维度名} = {分}

→ 对应阶段 6 决策树阈值：
  - A 档要求行综合分 ≥ 2.5 + P0 ≤ 2 → {满足 / 不满足}
  - B 档要求 1.5–2.5 + P0 3–5 → {... }
  - C 档要求 < 1.5 或 P0 ≥ 6 → {... }
```

- [x] **Step 6: 保存**

---

### Task T6.3: 应用决策树 → A/B/C

**Files:**
- Modify: `08-audit-grid.md`、新建 `_temp/decision-trace.md`

- [x] **Step 1: 数 P0 总数**

```bash
grep -c 'severity: P0' "docs/Design/Architecture Design/audit/2026-04-30/_temp/findings-raw.md"
```

- [x] **Step 2: 读全表综合分（T6.2 算出）**

- [x] **Step 3: 应用 spec §7.6 决策树**

```
if 全表综合分 < 1.5 OR P0 ≥ 6:
    结论 = C  # 重写 v2.0
elif 1.5 ≤ 全表综合分 ≤ 2.5 AND 3 ≤ P0 ≤ 5:
    结论 = B  # 结构性修订 + ADR
elif 全表综合分 ≥ 2.5 AND P0 ≤ 2:
    结论 = A  # 仅 patch list
else:
    # 边界情况，按更劣档位归
    结论 = max(全表分对应档, P0 对应档)
```

- [x] **Step 4: 写入 _temp/decision-trace.md**

```markdown
# 三态决策追溯

- 全表综合分: X.XX
- P0 计数: N
- 决策树命中分支: A / B / C
- 理由: ...
- 关键弱点对象: {从 T6.2 抄}
- 关键弱点维度: {从 T6.2 抄}
```

- [x] **Step 5: 保存**

---

### Task T6.4: 写 01-mass-coordinate-system.md（镜像 CLAUDE.md §2）

**Files:**
- Read: `CLAUDE.md` §2
- Create: `audit/2026-04-30/01-mass-coordinate-system.md`

- [x] **Step 1: 复制 CLAUDE.md §2 内容**

```bash
sed -n '/^## 2\. 系统坐标系/,/^## 3\. /p' CLAUDE.md
```

- [x] **Step 2: 写 01-mass-coordinate-system.md**

```markdown
# 阶段 0 · MASS v3.0 系统坐标系记录

> 本文件镜像项目 CLAUDE.md §2，作为审计基线。来源 `docs/Init From Zulip/‼️mass_adas_architecture_v3_industrial（Kongsberg Benchmark).html`。
> 用于审计追溯：所有跨层接口审计、命名一致性核查均以此为真值。

---

{从 CLAUDE.md §2 复制的全部内容}

---

## 与 v1.0 架构文档的差异

v1.0 §1.2 / §6 / §7 中残留 "L1 感知层"、"L2 战略层" 等错术语。审计的 patch list (08a) 包含字段级修复。
```

- [x] **Step 3: 保存**

---

### Task T6.5: 把 _temp/findings-raw.md 定稿到 05-audit-report.md

**Files:**
- Read: `_temp/findings-raw.md`
- Modify: `audit/2026-04-30/05-audit-report.md`

- [x] **Step 1: 按 Severity 排序所有 finding**

P0 在前 → P1 → P2 → P3。

- [x] **Step 2: 去重 + 合并**

如果两条 finding 实际是同一问题（例：同一 chapter 多处展现），合并为一条，多个 locator。

- [x] **Step 3: 重新分配 finding ID**

按 `F-{Pn}-D{n}-{seq}` 顺序号。例：所有 P0-D1 的 finding 顺序号 001/002/...

- [x] **Step 4: 写入 05-audit-report.md**

替换 `## P0 Findings` / `## P1 Findings` / 等占位为实际内容。

- [x] **Step 5: 填 `## 摘要统计` 表**

- [x] **Step 6: 保存 + 行数验证**

---

### Task T6.6: 写 99-followups.md

**Files:**
- Create: `audit/2026-04-30/99-followups.md`

- [x] **Step 1: 收集所有 [FOLLOWUP-*] 标记**

```bash
grep -rn 'FOLLOWUP' "docs/Design/Architecture Design/audit/2026-04-30/"
```

- [x] **Step 2: 收集所有 P3 finding**

- [x] **Step 3: 写入 99-followups.md**

```markdown
# 后续跟进项（非阻塞）

## P3 Findings
（细节）

## FOLLOWUP 项
（来自阶段 2 / 5 等的"留待解决"）

## CLAUDE.md 同步任务
- 后续会继续审计中发现的 CLAUDE.md 其他陈旧描述
```

- [x] **Step 4: 保存**

---

### Task T6.7: 分支 — 写对应路径的输出

**根据 T6.3 的 A/B/C 结果，执行对应分支：**

#### 分支 A: 写 08a-patch-list.md

**Files:**
- Create: `audit/2026-04-30/08a-patch-list.md`

- [x] **Step 1: 从 05-audit-report.md 提取所有 P0/P1 finding 的 recommended_fix**

- [x] **Step 2: 按 v1.0 文件位置组织字段级 diff**

```markdown
# v1.1 Patch List

> 字段级 diff，可直接合并到 v1.0 → v1.1。每条引用 finding ID 追溯。

## §1.2 设计边界表
- [x] [F-P1-D7-001] 行 54: "（L2 战略层）" → "（L2 航路规划层 Voyage Planner）"
- [x] [F-P1-D7-002] 行 55: "（L1 感知层）" → "（Multimodal Fusion 多模态感知融合层）"

## §6.3 详细设计 mermaid 图
- [x] [F-P1-D7-003] 行 447: `RADAR["雷达\n(L1 感知层)"]` → `RADAR["雷达\n(Multimodal Fusion)"]`
- ...

## §11 M7 Safety Supervisor
- [x] ...
```

- [x] **Step 3: 保存**

#### 分支 B: 写 08a-patch-list.md + 08c-adr-deltas.md

- [x] **Step 1: 同分支 A 写 08a-patch-list.md**

- [x] **Step 2: 写 08c-adr-deltas.md（顶层决策的 ADR 增量）**

```markdown
# ADR 增量（v1.0 → v1.1 顶层决策修订）

## ADR-001 (NEW): {原决策} 的修订

- 状态: 提议
- 日期: 2026-04-30
- 上下文: 原 v1.0 §2.X 决策 ...
- 决策: 修订为 ...
- 后果: ...
- 证据: 引用阶段 2 R{x} + finding F-P0-D{n}-{seq}
```

每个修订/替换的顶层决策一个 ADR。

- [x] **Step 3: 保存**

#### 分支 C: 写 08b-v2-architecture.md + 08c-adr-deltas.md

- [x] **Step 1: 写 08b-v2-architecture.md（替代架构提案）**

```markdown
# v2.0 替代架构提案

## 摘要

v1.0 综合分 X.XX，命中阶段 6 决策树 C 档。本提案给出 v2.0 替代架构的核心思路。

## 核心调整

1. {从 04-architecture-judgment.md 抄"替换"判定的对象}
2. ...

## v2.0 模块分解

（基于阶段 4 评判结论 + Kongsberg Benchmark）

## 迁移路径

- 立即可用：{阶段 4 判保留的对象，直接保留}
- 需重新设计：{判替换的对象，给重新设计要点}
- 接口契约修订：{基于阶段 5 跨层 finding}

## 工程证据

引用阶段 2 R1–R8 + 阶段 4 评判。
```

- [x] **Step 2: 同分支 B 写 08c-adr-deltas.md（每个替换决策一个 ADR）**

- [x] **Step 3: 保存**

---

### Task T6.8: 写 00-executive-summary.md

**Files:**
- Read: 所有先前交付物
- Create: `audit/2026-04-30/00-executive-summary.md`

- [x] **Step 1: 写一页摘要**

```markdown
# L3 战术层架构审计 · 摘要

| 字段 | 值 |
|---|---|
| 审计日期 | 2026-04-30 |
| 主审对象 | v1.0 (`MASS_ADAS_L3_TDL_架构设计报告.md`) |
| 审计基线 | v3.0 Kongsberg-Benchmarked |
| **三态结论** | **A / B / C** |
| 全表综合分 | X.XX / 3.0 |
| P0 finding 数 | N |
| 阶段 2 调研轮 | 8/8 完成 |

---

## 关键发现（≤5 条）

1. **{最重要的 P0 / 替换判定}**
2. ...

---

## 推荐合并 / 重写路径

{基于 A/B/C 结论的具体步骤}

---

## 阈值取舍 ADR 提示

引用 spec §15 业界基准与本 spec 取舍。建议把 A/B/C 结论与阈值写入项目 ADR-000，让 CCS 验船师 / 验证官 / 架构师对齐预期。

---

## 完整交付物清单

- 00 (本文件)
- 01-mass-coordinate-system.md
- 02-references-prescan.md
- 03-evidence-base.md
- 04-architecture-judgment.md
- 05-audit-report.md
- 06-cross-doc-check.md
- 07-cross-layer-check.md
- 08-audit-grid.md
- {对应分支输出}
- 99-followups.md
```

- [x] **Step 2: 保存**

---

### Task T6.9: 全套 DoD 终检

- [x] **Step 1: 走 spec §11 全部 DoD 项**

```
- [x] 每个保留 / 采纳的核心决策有 ≥3 工业 + ≥3 学术证据
- [x] 每个修订 / 弃用的决策有"为什么不行"的反证
- [x] 24 条引用全部预扫完成；🔴/⚫ 数 ≤ 5
- [x] 14 × 9 评分矩阵全部填分或 N/A；行 / 列 / 全表综合分均算出（2.35）
- [x] 阶段 2 NLM deep research 8 轮全部完成；全部由 subagent 异步执行
- [x] 阶段 3+ NLM 调研轮次 ≤ 12（实际 = 0，未触发临时反证调研）
- [x] 所有 P0 / P1 finding 的置信度均 ≥ 🟡，配 fix 建议
- [x] 跨文档 + 跨层对照各产 ≥ 1 节
- [x] 摘要给出明确 B 档结论；阈值取舍写入 ADR-001/002/003
- [x] D9 评判覆盖全部 13 对象
```

- [x] **Step 2: 列出文件存在性**

```bash
ls -la "docs/Design/Architecture Design/audit/2026-04-30/"
```

Expected：含 00–99 全套 markdown（11–13 个，根据 A/B/C 分支不同）。

- [x] **Step 3: 清理 _temp/**（保留作为审计追溯，不删）

```bash
ls "docs/Design/Architecture Design/audit/2026-04-30/_temp/"
```

如果 _temp/ 中文件已纳入正式交付物，可保留作为审计追溯。**不删**——审计的中间产物本身有审查价值。

- [x] **Step 4: 最终向用户报告**

输出（≤500 字）：
- 审计完成
- 三态结论 (A/B/C)
- 全表综合分 + P0 数
- 关键发现 5 条
- 输出文件列表
- 推荐下一步（合并到 v1.1 / 重写 v2.0 / ADR 评审会等）

---

## 风险与回退

| 风险 | 触发条件 | 回退动作 |
|---|---|---|
| 阶段 1 subagent 失败 | quick-researcher 超时或返回错误 | 主 agent 同步 fallback：用 WebSearch + WebFetch 直接核验失败 batch |
| 阶段 2 某轮 NLM 入库失败 | --add-sources 报错 | 重启该 round subagent；超过 2 次重试则人工介入 |
| 阶段 2 NLM 配额逼近 80% | 累计已用 > 400 calls | 主 agent 暂停下发新 subagent，向用户报告；可选项次日继续 |
| 阶段 3 P0 finding 数量 ≥ 6 | 早期触发 spec §14 风险 | 跳过阶段 4 部分对象 → 直接进入阶段 6 走三态 C |
| 阶段 4 某对象证据不足 | <3 工业 OR <3 学术 | 触发额外 subagent 调研（计入阶段 3+ 的 12 轮上限） |
| 阶段 5 跨层文档缺失 | Zulip 某层 readme 找不到 | 改用 ‼️mass_adas_architecture_v3_industrial HTML 兜底 |
| 阶段 6 决策树边界情况 | 全表分 ∈ [1.5, 2.5] 但 P0 = 0 | 按"较劣档位归"原则，落 B 档（spec §7.6 已定） |

---

## Self-Review 检查（写完此 plan 后做）

- [x] **Spec 覆盖**：spec §1–§16 每节都映射到 plan 的 phase / task
- [x] **Placeholder 扫描**：除 T6 分支内的 v1.1 patch 实例（"§1.2 行 54: ..."）必然有 TBD（运行时填）外，无失职 TBD
- [x] **Type 一致**：subagent prompt 模板、Round ID（R1–R8）、Finding ID 格式（F-Pn-Dn-NNN）、文件名（00-99）全文一致
- [x] **Bite-sized**：每 Step 2–10 分钟（研究步骤略长于代码步骤可接受）
- [x] **NLM 路由**：阶段 2 R1–R8 的入库笔记本与 spec §10.2 / §7.2 一致

---

## 执行交接

Plan 完成并保存到 `docs/superpowers/plans/2026-04-30-l3-architecture-review-implementation-plan.md`。两种执行选择：

**1. Subagent-Driven（推荐）** — 我每个 task 派 fresh subagent，task 间复审，迭代快

**2. Inline Execution** — 在本 session 内直接执行 task（用 superpowers:executing-plans），批次间设 checkpoint

请选择执行方式。
