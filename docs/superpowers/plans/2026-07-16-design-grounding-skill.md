# Design Grounding Skill 实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建 `design-grounding` Skill(1 主文件 + 5 references),在 brainstorming 之前用调研显式化专家判断,让非专家产出可判别、可溯源、有技术分解完整性的工程方案。

**Architecture:** 方案 B(主文件 + references)。SKILL.md 管流程编排(身份/HARD-GATE/三机制/6 步清单/决策门总表/移交契约),5 个 references 各管一个深度方法。Step4/Step6 内联主文件。文件全部在 repo 外的 `~/.zcode/skills/design-grounding/`,不进 git;只有本计划文档进 git。spec-preflight 标记废弃。

**Tech Stack:** Markdown skill 文件(frontmatter + 正文 + references 引用)。无代码、无测试框架。验证靠:文件齐全性检查 + frontmatter 解析 + 引用闭环检查 + 人工走读。

## Global Constraints

- **文件位置**: 所有 skill 文件在 `/home/marine.huang/.zcode/skills/design-grounding/`(repo 外),不进 git。
- **frontmatter 格式**: YAML,含 `name` 和 `description` 两个字段。description 必须覆盖触发场景(方案设计/模块详细设计/技术选型/MPC 建型/COLREGs 设计等)以保证触发可靠。
- **reference 引用**: SKILL.md 正文中引用 references 用相对路径 `references/<file>.md`(参照 spec-preflight 的 `[granularity-routing.md](references/granularity-routing.md)` 写法)。
- **日志标准 ID 体系**: 八类前缀(`DP-/TD-/BL-/RNN/SC-/VR-/ALT-/TS-`)在 references 和 SKILL.md 中必须一致,不混用。
- **方案包八组件**: 术语表/技术规约表/决策卡片集/证据矩阵/技术分解树/弃用方案/需求场景+验收边界/已知冲突与盲区,顺序固定。
- **spec 来源**: 本计划的所有内容来自 spec `docs/superpowers/specs/2026-07-16-design-grounding-skill-design.md` 第 2-7 节,不引入 spec 外的新设计。
- **废弃 spec-preflight**: 只改其 SKILL.md frontmatter 加 deprecated,不删目录、不改其 references。

---

## File Structure

| 文件 | 职责 | 行数预估 |
|---|---|---|
| `/home/marine.huang/.zcode/skills/design-grounding/SKILL.md` | 主文件:身份/定位/HARD-GATE/三机制概要/6步强制清单/决策门总表/Step4内联/Step6内联/移交brainstorming | ~250 |
| `…/references/design-log-standard.md` | 决策树日志文档标准:文件命名/稳定ID体系/刚性骨架模板/排版硬规则(第3节) | ~150 |
| `…/references/step1-decision-discovery.md` | Step1 深度方法:模式判定/快速调研查询构造/决策点提取/技术分解触发 | ~130 |
| `…/references/step2-grilling.md` | Step2 深度方法:三视角追问话术/需求场景记录/盲区采集/技术分解子模块专项 | ~130 |
| `…/references/step3-deep-research.md` | Step3 深度方法:按优先级调研链(NLM→web→deep)/盲区转证据/并行subagent边界 | ~130 |
| `…/references/step5-design-it-twice.md` | Step5 深度方法:对比对象选择/竞争方案设计/并行对比/决策卡片七维模板/裁决 | ~140 |

依赖关系:`design-log-standard.md` 被 SKILL.md 和所有 step references 引用,是最底层共享规约,先建。SKILL.md 依赖全部 5 个 references,最后建。Step1-3-5 references 之间无依赖,可并行。Step2 弱依赖 step1(知道决策点格式),但内容独立。

---

## Task 1: 创建 design-log-standard.md(共享日志规约,最底层)

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/references/design-log-standard.md`

**Interfaces:**
- Consumes: spec 第 3 节(决策树日志文档标准)
- Produces: 日志标准,被 SKILL.md 和所有 step references 引用

- [ ] **Step 1: 创建 references 目录**

```bash
mkdir -p /home/marine.huang/.zcode/skills/design-grounding/references
```

- [ ] **Step 2: 写 design-log-standard.md**

写入文件 `/home/marine.huang/.zcode/skills/design-grounding/references/design-log-standard.md`,内容含以下小节(对照 spec 第 3 节,逐字落实到文件):

```markdown
# 决策树日志文档标准

决策树日志要同时满足"2 个月后能溯源"和"agent 能稳定解析索引"。采用注册表(当前态,可变快照)+ 演进日志(历史态,append-only 时序)双层结构。

## 1. 文件与命名

- 路径: `docs/superpowers/design-logs/YYYY-MM-DD-<topic>-design-log.md`
- 与 spec/plan 同目录族,便于关联

## 2. 稳定 ID 体系

全文档唯一,永不复用,不重编号。

| 前缀 | 含义 | 示例 |
|---|---|---|
| `DP-NN` | 决策点 | DP-01 |
| `TD-NN` | 技术分解(父节点) | TD-01 |
| `BL-NN` | 用户盲区 | BL-03 |
| `RNN` | 参考文献/证据 | [R1] |
| `SC-NN` | 需求场景 | SC-02 |
| `VR-NN` | 裁决记录 | VR-01 |
| `ALT-NN` | 备选/弃用方案 | ALT-02 |
| `TS-NN` | 技术规约 | TS-01 |

引用一律用 [RNN];参考文献集中一处;正文不写裸 URL。

## 3. 刚性骨架模板

(此处放入 spec 3.3 的完整 markdown 骨架,含 ## 0. 决策树状态 下的 0.1-0.8 八个注册表,
以及 ## 参考文献 和 ## 演进日志下的 Step1-6 时序区块。逐字复制 spec 3.3 的代码块内容。)

## 4. 排版硬规则

1. ID 唯一且稳定:一旦分配永不复用、不重编号。删除项保留 ID 标记 ~~废弃~~。
2. 双层一致:演进日志每次状态变更必须更新对应注册表。注册表是索引,演进日志是证据链,两者 ID 对齐。
3. 表格固定列:每个注册表列序固定(第 3 节即标准),新增字段加到最右,不插中间。
4. 三置信度分列:检索置信 / 来源权威 / 场景适用,不允许合并成一列。
5. 引用闭环:正文出现 [RNN] → 参考文献必须有对应条目;注册表引用列必须用 [RNN],不允许裸文字。
6. 时序区块:每个 Step 一个二级标题,带时间戳,内部按事件列,只追加不删改历史。改裁决 = 新增 VR 行 + 更新 DP 状态,不抹掉旧 VR。
7. 状态枚举固定:决策点状态只用 未决|调研中|已裁决|暂停(EXTERNAL_CONFIRMATION_REQUIRED);证据适用性只用 高|中|低|不适用。
```

> 执行者注意:spec 第 3.3 节代码块内的完整骨架(含 0.1-0.8 表格和 Step1-3 时序区块)必须逐字落入本文件的"第 3 节 刚性骨架模板"。

- [ ] **Step 3: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/references/design-log-standard.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 4: 验证八类 ID 前缀全部出现**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/references/design-log-standard.md
for p in "DP-NN" "TD-NN" "BL-NN" "RNN" "SC-NN" "VR-NN" "ALT-NN" "TS-NN"; do
  echo -n "$p: "; grep -c "$p" "$f"
done
```
Expected: 每个 `$p` 至少出现 1 次(即每个计数 ≥ 1)。

- [ ] **Step 5: 验证无裸 URL**

```bash
grep -nE "https?://" /home/marine.huang/.zcode/skills/design-grounding/references/design-log-standard.md || echo "无裸URL"
```
Expected: `无裸URL`(裸 URL 违反规则 5)。

- [ ] **Step 6: 此任务无 git 提交(skill 文件在 repo 外)**

记录完成状态到会话,不执行 git commit。

---

## Task 2: 创建 step1-decision-discovery.md

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/references/step1-decision-discovery.md`

**Interfaces:**
- Consumes: spec 第 4.1 节;引用 design-log-standard.md 的 ID 体系和注册表格式
- Produces: Step1 深度方法,被 SKILL.md 清单第 1 步引用

- [ ] **Step 1: 写 step1-decision-discovery.md**

写入文件,内容含(对照 spec 4.1 节):

```markdown
# Step 1: 行业调研 · 发现决策点

## 目标
不预设用户知道哪些决策维度。先快速扫描行业/研究现状,从文献/实践中自动提取该领域的关键决策维度,变成决策点交给后续 grilling。同时判定新建/重构模式。

## 执行

### 1.1 模式判定(机制B)
用 codegraph + 读现有代码/设计文档,检测目标模块是否已有实现/设计。
- 已有 → 重构模式:现有代码/设计是主证据之一,目标是评审查漏补缺
- 无 → 新建模式:外部权威为主
- 模式写入决策树日志首条(见 design-log-standard.md 日志头"模式"字段)

### 1.2 快速调研(限时、广度优先)
按以下顺序,目标是找"业界决策点共识与争议",不是找具体答案:

NLM 调研查询构造:
- 读 .nlm/config.json 选定相关 domain
- nlm-research --depth fast 扫描,查询词模板:
  "<需求关键词> design decisions" / "<技术名> trade-offs pitfalls" / "<领域> comparison survey"
- 提取该领域的标准决策维度

Web 扫描查询构造:
- 关键词 = 需求 + "design decisions | trade-offs | pitfalls | lessons learned | comparison"
- 目标:业界对这个问题的决策点共识与争议

代码库调研(机制B,仅理解业务约束):
- codegraph_explore 查目标模块当前实现
- 读上下游接口、已有设计文档
- 重构模式下:提取现有决策点作为评审对象

### 1.3 提取决策点
把调研发现的决策维度转成决策点,写入日志注册表 0.1 [DP]。每个决策点:
- id(DP-NN,按 design-log-standard.md 分配)
- 描述
- 类型: 架构|算法|约束|阈值|接口|技术
- 来源(哪条调研发现的,引用 [RNN])
- 状态: 未决

### 1.4 技术分解触发(机制C)
对每个类型为"技术"的决策点(答案形态是"采用某技术"),立即分解:
- 从调研/NLM 获取该技术的标准内部结构
- 每个内部子模块变成独立决策点(DP-NN),挂到父决策点下
- 在日志注册表 0.2 [TD] 记录技术分解树
- 不可停留在"用某技术"这一层

判定标志:决策点答案形态是"采用某技术/算法/框架"而非"选某个数值/架构选择"。

常见技术分解参考(非穷举,从调研获取为准):
- MPC → 状态量/控制量, 预测模型, 目标函数, 约束层级(硬约束/软约束+slack), 求解器, 参考跟踪, 失败回退
- NLP → 状态量, 动力学约束, 边界约束, 避碰约束, 目标函数, 求解器, 热启动, 失败回退
- COLREGs FSM → 角色/态势分类, 触发条件, 行为选择, 释放条件, 与规划器交互, 失效回退
- EKF → 状态方程, 量测方程, 噪声建模, 数据关联, 异常处理

## 决策门(进 Step2 的条件)
- ✅ 至少识别出该领域的主干决策维度(非空)
- ✅ 所有技术型决策点已触发技术分解,子模块全部成为独立决策点
- ✅ 决策树日志已含决策点列表(注册表 0.1) + 技术分解树(注册表 0.2) + 模式判定
- ❌ 调研结果为空或过于肤浅 → 回退重调,或标注 EXTERNAL_CONFIRMATION_REQUIRED 暂停

## 输出到日志
- 注册表 0.1 [DP]: 决策点列表
- 注册表 0.2 [TD]: 技术分解树
- 日志头: 模式判定
- 演进日志 Step1 区块: 调研来源清单、新增决策点、触发的技术分解

## 分阶段输出形态
向用户展示决策点清单(含技术分解树),请求确认后再进 Step2。
```

- [ ] **Step 2: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/references/step1-decision-discovery.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 3: 验证引用日志标准**

```bash
grep -c "design-log-standard.md" /home/marine.huang/.zcode/skills/design-grounding/references/step1-decision-discovery.md
```
Expected: `≥ 1`

- [ ] **Step 4: 验证技术分解触发逻辑和决策门都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/references/step1-decision-discovery.md
grep -c "机制C" "$f"   # 技术分解
grep -c "决策门" "$f"  # 决策门
```
Expected: 两项均 `≥ 1`

- [ ] **Step 5: 无 git 提交(repo 外)**

---

## Task 3: 创建 step2-grilling.md

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/references/step2-grilling.md`

**Interfaces:**
- Consumes: spec 第 4.2 节;Step1 产出的决策点列表;日志标准
- Produces: Step2 深度方法,被 SKILL.md 清单第 2 步引用

- [ ] **Step 1: 写 step2-grilling.md**

写入文件,内容含(对照 spec 4.2 节):

```markdown
# Step 2: grilling 压力测试

## 目标
对每个决策点(含技术分解出的子模块)逐个压力测试,用三视角追问逼出失效边界、用户真实需求场景、用户知识盲区。

## 执行

### 2.1 逐决策点 grilling
一个一个来,不批量。对每个决策点(DP-NN,含技术分解子模块)施加三视角追问:

专家视角:
- 这个决策在领域标准/论文里公认怎么做?
- 有什么前置条件?
- 引用证据 [RNN] 支撑

新手视角:
- 为什么不是最简单的做法?
- 为什么不是现状(重构模式)?

悲观视角(最关键):
- 这个决策选错了/缺失了会怎么失效?
- 边界条件是什么?
- 在什么输入/场景下崩溃?

### 2.2 记录需求场景
grilling 过程中用户描述的真实运营/操作场景必须记录到日志注册表 0.5 [SC]:
- 场景描述
- 约束/边界(如 u<2kn, 受限水域)
- 驱动的决策点 [DP-NN]
这是后续验收边界的依据。

### 2.3 采集盲区(关键产出)
当用户对某追问无法判断/回答时,记录为盲区,不逼用户猜。写入日志注册表 0.3 [BL]:
- 问题
- 归属决策点 [DP-NN]
- 为什么用户答不了:知识空白 / 需要领域权威 / 需要实验数据
- 调研优先级:高/中/低(由该盲区对实现成败的影响决定)

优先级判定参考:
- 高:该盲区不解决会导致实现返工或安全风险(如 MPC 约束层级未定)
- 中:该盲区影响方案质量但不阻断实现(如调参范围)
- 低:该盲区可在实现后验证(如日志格式细节)

### 2.4 技术分解子模块专项 grilling(机制C)
对机制C分解出的每个子模块,悲观视角必须追问:
"这个子模块如果被默认实现成最简版,会导致什么?"
这正是 M5 MPC 返工的根因预防——防止预测模型/优化问题等核心子模块被默认最简实现。

示例(MPC 技术分解):
- 预测模型子模块:默认用恒速直线模型 → 失效:大转向角下预测轨迹严重偏离
- 优化问题子模块:默认无约束 → 失效:输出不可行解(超速度/超转向)
- 约束层级子模块:默认全硬约束 → 失效:求解器频繁无解

## 决策门
- ✅ 每个决策点都经过三视角追问
- ✅ 所有技术分解子模块都经过"默认最简版会失效什么"追问
- ✅ 盲区清单非空时,每条都有调研优先级
- ✅ 需求场景已记录(注册表 0.5)
- ❌ 仍有决策点未经过压力测试 → 不进 Step3

## 输出到日志
- 演进日志 Step2 区块: grilling 记录(三视角)
- 注册表 0.3 [BL]: 盲区清单
- 注册表 0.5 [SC]: 需求场景

## 分阶段输出形态
Step2 用 grilling 记录 + 裁决 形态展示给用户。格式:
[专家] <结论> [RNN]
[新手] <追问>
[悲观] <失效边界>
```

- [ ] **Step 2: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/references/step2-grilling.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 3: 验证三视角和机制C专项都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/references/step2-grilling.md
grep -c "专家视角" "$f"
grep -c "新手视角" "$f"
grep -c "悲观视角" "$f"
grep -c "默认最简版" "$f"
```
Expected: 四项均 `≥ 1`

- [ ] **Step 4: 无 git 提交(repo 外)**

---

## Task 4: 创建 step3-deep-research.md

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/references/step3-deep-research.md`

**Interfaces:**
- Consumes: spec 第 4.3 节;Step2 产出的盲区清单(注册表 0.3);日志标准
- Produces: Step3 深度方法,被 SKILL.md 清单第 3 步引用

- [ ] **Step 1: 写 step3-deep-research.md**

写入文件,内容含(对照 spec 4.3 节):

```markdown
# Step 3: 自主深度调研(Web + NLM)

## 目标
针对 Step2 采集的盲区,自主深度调研,不把技术问题抛回给用户。把盲区转成带来源、带置信度、带适用性评估的证据。

## 执行

### 3.1 按优先级调研链
高优先级盲区先调(读注册表 0.3 [BL] 的优先级)。每个盲区按以下链路逐级深入:

第1级 - NLM 快查:
- nlm-ask 查 curated 笔记本
- nlm-research --depth fast
- 适用于:领域知识库已覆盖的问题

第2级 - Web 深搜:
- NLM 不够或过时时
- 目标:论文/标准/工程实践/已知失效案例
- 查询词:技术名 + "standard / specification / production deployment / failure mode / lesson learned"

第3级 - NLM 深调:
- nlm-research --depth deep
- 每个 topic 至多一次,需写明理由
- 适用于:需要深挖笔记本已有源的问题

第4级 - Web 通用检索兜底:
- 仅在前三级都不够时

### 3.2 盲区转证据
每条调研结果必须回答原盲区问题,填入日志注册表 0.4 [EV],标注三类置信度(分开,不合并):
- 来源类型: NLM | web | 代码库
- 引用: [RNN]
- 检索置信: NLM 的检索覆盖度(高/中/低)
- 来源权威: 来源的权威性/独立性/时效(高/中/低)
- 场景适用: 对当前具体场景是否成立(高/中/低/不适用)

适用性评估要点:
- 论文用全速域,本场景是低速大角 → 适用性打折
- 工程实践来自不同船型/尺度 → 适用性打折
- 标准条文有适用范围限定 → 核对是否覆盖本场景

### 3.3 并行调研(纯流程技能唯一用 subagent 的场景)
多个高优先级盲区可并行调研:
- 用通用 subagent,每个盲区一个
- subagent 只返回证据,不做方案裁决——裁决永远在主线程
- subagent 的 dispatch 必须包含:盲区问题、调研范围授权(NLM/web allowed)、输出契约(证据+三类置信度+引用)

subagent 边界(硬约束):
- ✅ 返回:证据、引用 [RNN]、三类置信度、适用性评估
- ❌ 不返回:方案裁决、推荐、决策
- ❌ 不做:写文件、改代码、问用户

### 3.4 填充证据矩阵
把所有证据填入注册表 0.4 [EV],按决策点 [DP-NN] 归组。

## 决策门
- ✅ 每个盲区都有对应证据或显式标注 UNKNOWN/EXTERNAL_CONFIRMATION_REQUIRED
- ✅ 证据的三类置信度(检索/权威/适用)分开标注
- ✅ 技术分解子模块的盲区全部覆盖(机制C)
- ❌ 关键盲区无证据且无法调研 → 标 EXTERNAL_CONFIRMATION_REQUIRED 暂停,问用户是否接受该未知继续

## 输出到日志
- 注册表 0.4 [EV]: 证据矩阵(按决策点归组,含三类置信度)
- 演进日志 Step3 区块: 各盲区闭环记录、新增证据

## 分阶段输出形态
Step3 用对比矩阵式 展示不同置信度的方案/证据:
| 方案/证据 | 来源 | 检索置信 | 来源权威 | 场景适用 |
```

- [ ] **Step 2: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/references/step3-deep-research.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 3: 验证调研链四级和subagent边界都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/references/step3-deep-research.md
grep -c "nlm-ask" "$f"
grep -c "nlm-research --depth deep" "$f"
grep -c "不做方案裁决" "$f"
```
Expected: 三项均 `≥ 1`

- [ ] **Step 4: 无 git 提交(repo 外)**

---

## Task 5: 创建 step5-design-it-twice.md

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/references/step5-design-it-twice.md`

**Interfaces:**
- Consumes: spec 第 4.5 节;Step4 的初步推荐;日志标准
- Produces: Step5 深度方法,被 SKILL.md 清单第 5 步引用

- [ ] **Step 1: 写 step5-design-it-twice.md**

写入文件,内容含(对照 spec 4.5 节):

```markdown
# Step 5: DESIGN-IT-TWICE 方案对比

## 目标
对关键决策点做并行方案对比,而非单一选择。逼出"如果不这么做会怎样",防止默认走最简版。借鉴 Matt Pocock codebase-design/DESIGN-IT-TWICE。

## 执行

### 5.1 选定对比对象
不所有决策点都做 DESIGN-IT-TWICE(太重)。选择标准:
- 技术分解的整体技术方案(如"MPC 整体建模方式"作为一组对比)
- 影响实现成败的核心决策点(风险高、证据冲突、子模块多)

跳过(直接采纳 Step4 推荐):
- 低风险、证据一致、单一数值的决策点

### 5.2 设计竞争方案
为每个对比对象设计 2-3 个竞争方案。每个方案必须完整自洽(覆盖该技术/决策点的所有子模块),不允许只比局部。

示例(MPC 整体建模对比,三方案都必须完整覆盖五维):
- 方案A: 非线性连续 MPC + 序列二次规划求解器 + 软约束+slack + Nomoto预测模型 + 速度回退
- 方案B: 线性时变 MPC + 内点法 + 硬约束 + 线性化预测模型 + 路径保持回退
- 方案C: 分层(BC-MPC航迹 + Mid-MPC速度) + 各自求解器 + 混合约束 + 多模型预测 + 分层回退
(以上为示例格式,实际方案由 Step1-4 调研证据决定)

### 5.3 并行对比(纯流程技能第二次用 subagent)
可用通用 subagent 并行,每方案一个。每个 subagent 深化一个方案,返回完整刻画。
subagent 不做裁决,只返回方案刻画。

subagent 输出契约:
- 方案完整描述(覆盖所有子模块)
- 各子模块的技术选择和理由 [RNN]
- 失效边界
- 实现复杂度评估

### 5.4 决策卡片(核心产出)
每个对比对象出一张决策卡片,七维固定:

┌─ 方案 X: <名称> ─────────────────────────────┐
│ 来源      │ [R1]... [R2]...                    │
│ 工程验证  │ nav2 生产✓ / 仅论文✗ / 仅本项目✗   │
│ 技术分解  │ 子模块1✓ 子模块2✓ ... (逐项)       │
│ 失效边界  │ >X 时求解超时(见 SC-NN)           │
│ 实现风险  │ 高/中/低 + 具体风险来源             │
│ 可测性    │ SIL 场景 SC-NN 可证                │
│ 推荐度    │ ★☆☆☆☆ ~ ★★★★★                  │
└──────────────────────────────────────────────┘

七维说明:
- 来源:支撑该方案的证据 [RNN]
- 工程验证:该方案在生产/开源项目中的落地程度
- 技术分解:该方案对所有子模块的覆盖(逐项打钩)
- 失效边界:该方案在什么条件下失效,关联场景 [SC-NN]
- 实现风险:实现难度/实时性/依赖等风险
- 可测性:用什么测试/场景能证明或证伪
- 推荐度:综合评分

### 5.5 裁决
对每个对比对象给出最终采纳方案 + 弃用方案及理由。裁决必须解释:
为什么采纳方在证据/工程验证/技术分解完整性/失效边界上优于弃用方。
写入日志注册表 0.6 [VR](最终裁决覆盖 Step4 初稿)和 0.7 [ALT](弃用方案)。

## 决策门
- ✅ 所有关键决策点/技术方案都经过 DESIGN-IT-TWICE 或显式标注"低风险直接采纳"
- ✅ 每张决策卡片维度完整(七维全填)
- ✅ 裁决有证据链理由,弃用方案有明确弃用理由
- ❌ 两方案证据相当、无法用证据裁决 → 标记"运营/风险取舍",用场景化问题问用户(只在此处打断用户)

## 输出到日志
- 演进日志 Step5 区块: 决策卡片全文、裁决记录
- 注册表 0.6 [VR]: 最终裁决
- 注册表 0.7 [ALT]: 弃用方案

## 分阶段输出形态
Step5 用决策卡片 展示最终对比结论。
```

- [ ] **Step 2: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/references/step5-design-it-twice.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 3: 验证决策卡片七维都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/references/step5-design-it-twice.md
for d in "来源" "工程验证" "技术分解" "失效边界" "实现风险" "可测性" "推荐度"; do
  echo -n "$d: "; grep -c "$d" "$f"
done
```
Expected: 七项均 `≥ 1`

- [ ] **Step 4: 无 git 提交(repo 外)**

---

## Task 6: 创建 SKILL.md 主文件(依赖全部 5 references)

**Files:**
- Create: `/home/marine.huang/.zcode/skills/design-grounding/SKILL.md`

**Interfaces:**
- Consumes: spec 第 2、4.4、4.6、5、6 节;全部 5 个 references
- Produces: 完整可执行的 design-grounding Skill 主文件

- [ ] **Step 1: 写 SKILL.md**

写入文件 `/home/marine.huang/.zcode/skills/design-grounding/SKILL.md`。frontmatter 的 description 必须覆盖触发场景。完整结构(对照 spec 第 2、5.1 节):

```markdown
---
name: design-grounding
description: Use when a module, feature, behavior, algorithm choice, or technical design (MPC modeling, NLP constraints, COLREGs FSM, ODD policy, ROS2 interface, GNC contract) needs grounded solution design with evidence sourcing and stress-testing before superpowers:brainstorming writes a formal Spec. Activates for detailed design, technical selection, constraint modeling, redesign/review of existing implementation, and any case where the user is not a domain expert and needs traceable options with failure boundaries.
---

# Design Grounding

## 目的与定位

让非专家也能产出有溯源、有压力测试、有技术分解完整性、有风险量化的可判别工程方案。

定位:独立前置 Skill,在 superpowers:brainstorming 之前。链路:
design-grounding → brainstorming → writing-plans → exec → verify → finish

核心解决:brainstorming 默认你是专家;本 Skill 用"调研显式化专家判断"代替"假设你已有专家判断"。

服务领域:MASS-TDL / 海事(COLREGs、MPC、NLP、ODD、ROS2 DDS 等)。

<HARD-GATE>
不写正式 Spec、不调用 superpowers:brainstorming、不写代码、不 scaffold 解决方案,
直到 Step6 方案包产出且用户接受。
</HARD-GATE>

## 三个核心机制(贯穿全部 6 步)

### 机制A:决策树状态文件
单个 Skill 运行期间维护一份持续演进、落盘的决策树状态(追加式 LOG Markdown)。
完整文档标准见 [design-log-standard.md](references/design-log-standard.md)。
2 个月后仍能完整溯源决策、证据、弃用方案。

### 机制B:证据权重与重构支持
证据按权威性分层,代码库/现有设计是合法证据源(非零权重)。支持两种模式:
- 新建设计(Greenfield):外部权威为主,代码库用于理解约束
- 重构评审(Redesign):现有代码/设计是主证据之一,外部权威用于验证/补强/纠偏
Step1 自动判定模式。证据分层:DOMAIN_EVIDENCE > PROJECT_FACT > DOCUMENTED_INTENT > ASSUMPTION/UNKNOWN。

### 机制C:技术分解
当决策点答案是"采用某技术/算法/框架"时,立即分解为关键内部流程/子模块决策点,
逐一对齐。不可停留在"用某技术"这一层。防止实现默认走最简版导致返工。
MPC 示例:用MPC → 状态量/控制量, 预测模型, 目标函数, 约束层级, 求解器, 参考跟踪, 失败回退。

## 强制清单(按序执行)

创建 TodoWrite,每步一个 todo,决策门作为 todo 完成条件。

1. **Step1 行业调研·发现决策点** → 见 [step1-decision-discovery.md](references/step1-decision-discovery.md)
2. **Step2 grilling 压力测试** → 见 [step2-grilling.md](references/step2-grilling.md)
3. **Step3 自主深度调研** → 见 [step3-deep-research.md](references/step3-deep-research.md)
4. **Step4 汇总分析·推荐方案** → 见下方内联
5. **Step5 DESIGN-IT-TWICE** → 见 [step5-design-it-twice.md](references/step5-design-it-twice.md)
6. **Step6 术语+技术规约+方案包** → 见下方内联

## 决策门总表

| 步骤 | 进下一步的硬条件 |
|---|---|
| Step1 | 决策点非空;技术型决策点已全部分解;日志含决策点+模式判定 |
| Step2 | 每决策点经三视角;技术子模块经"默认最简版失效"追问;盲区有优先级;场景已记 |
| Step3 | 每盲区有证据或标UNKNOWN;三类置信度分列;技术子模块盲区全覆盖 |
| Step4 | 每决策点有推荐+证据+弃用理由;技术分解子模块无遗漏(DECOMPOSITION_INCOMPLETE标出);推荐有风险+失效边界+验证 |
| Step5 | 关键决策点经DESIGN-IT-TWICE或标低风险采纳;卡片七维全填;裁决有理由 |
| Step6 | 术语表全;技术规约表无遗漏;方案包八组件齐;契约明确;DECOMPOSITION闭环或标暂停 |

## Step4:汇总分析 · 推荐方案(内联)

目标:把 Step1-3 累积的决策点、盲区、证据综合成带推荐的方案分析。

1. 逐决策点综合(按 DP 编号):
   - 汇总证据 [RNN]、grilling 三视角结论、技术分解子模块状态
   - 给初步推荐及理由(证据链形式:[R1][R2] 一致 → 推荐 X)
   - 列备选/弃用方案,每条弃用写理由

2. 技术分解完整性校验(机制C专项):
   - 对每个 TD-NN 检查子模块决策点是否都已综合
   - 任一子模块留有未闭环盲区 → 该技术标 DECOMPOSITION_INCOMPLETE,不允许进 Step5
   - 输出技术分解就绪度:每项子模块 [✓已裁决 | ○调研中 | ✗盲区未闭环]

3. 风险量化:每个推荐标注实现风险(高/中/低)+ 风险来源 + 失效边界 + 验证需求

4. 冲突标注:证据矛盾时显式记录冲突,不投票抹平,留 Step5 解决

决策门:每决策点有推荐+证据+弃用;技术分解无遗漏;推荐有风险量化。
若 DECOMPOSITION_INCOMPLETE 且盲区可调研 → 回 Step3;不可调研 → 标暂停问用户。

输出:演进日志 Step4;注册表 0.6(初稿)+ 0.7。

## Step6:术语表 + 技术规约 + 方案包(内联)

目标:固定术语、固化技术语义、打包交付 brainstorming。

1. 术语表:每术语含定义[RNN] / 本方案具体含义 / 边界(不是什么) / 关联[DP-NN]

2. 技术规约表(方案包独立组件,六类):
   - 坐标系:全局(WGS84/UTM)、当地(ENU/NED)、船体(body);原点、轴定义、转换链
   - 物理量单位:每物理量标准单位,接口/公式/阈值统一
   - 符号约定:角度正负向、旋转方向、参考零点
   - 时序约定:时间戳基准、频率、相位关系
   - 数值边界:物理可行域、饱和、无效值表示
   - 接口语义:关键输入输出字段含义、缺失/无效处理
   每条标来源[RNN]或DESIGN_DECISION;标关联[DP-NN]/接口;重构模式下标与现状差异

3. 方案包组装(八组件,顺序固定):
   1. 术语表  2. 技术规约表  3. 决策卡片集  4. 证据矩阵
   5. 技术分解完整树  6. 弃用方案及理由  7. 需求场景+验收边界  8. 已知冲突与盲区

4. 方案包契约(brainstorming 权限边界,写入方案包首部):
   - ✓ 可做:工程细节设计(架构/组件/数据流/错误处理/测试),已裁决方案内优化拔高
   - ✗ 不可做:推翻已裁决核心方案,除非发现新矛盾证据(回炉 design-grounding)
   - ✗ 不可做:重提已弃用方案(ALT注册表)
   - ✗ 不可做:擅自修改技术规约(单位/坐标系/符号),需改则回 design-grounding

5. 移交:
   - 方案包存 docs/superpowers/specs/YYYY-MM-DD-<topic>-solution-pack.md
   - 日志标"已交付 brainstorming"
   - 调用 superpowers:brainstorming,方案包作为权威输入注入

决策门:术语表全;技术规约表无遗漏无歧义;方案包八组件齐;契约明确;DECOMPOSITION闭环或标暂停。
技术规约表有未定项 → 不交付。

## 移交 brainstorming

调用 brainstorming 时声明:
"本方案的核心技术决策已通过 design-grounding 裁决。brainstorming 负责工程细节设计,
不得推翻已裁决方案/重提弃用方案/修改技术规约,除非发现新矛盾证据则回炉 design-grounding。"

回炉机制:brainstorming 发现新矛盾证据 → 暂停 → 回 design-grounding 带新证据重跑受影响决策点。
形成闭环:design-grounding → brainstorming →(新矛盾)→ 回 design-grounding →(无矛盾)→ writing-plans
```

- [ ] **Step 2: 验证文件创建**

```bash
test -f /home/marine.huang/.zcode/skills/design-grounding/SKILL.md && echo "OK"
```
Expected: `OK`

- [ ] **Step 3: 验证 frontmatter name 和 description 都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/SKILL.md
head -5 "$f"
```
Expected: 前 5 行含 `name: design-grounding` 和 `description:` 字段。

- [ ] **Step 4: 验证 description 覆盖关键触发词**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/SKILL.md
for kw in "MPC" "COLREGs" "module" "design" "brainstorming"; do
  echo -n "$kw: "; grep -ic "$kw" "$f"
done
```
Expected: 五项均 `≥ 1`(在 description 或正文出现,保证触发可靠)。

- [ ] **Step 5: 验证五个 reference 引用都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/SKILL.md
for r in "step1-decision-discovery" "step2-grilling" "step3-deep-research" "step5-design-it-twice" "design-log-standard"; do
  echo -n "$r: "; grep -c "$r" "$f"
done
```
Expected: 五项均 `≥ 1`。

- [ ] **Step 6: 验证决策门总表六行齐全**

```bash
grep -cE "^\| Step[0-9]" /home/marine.huang/.zcode/skills/design-grounding/SKILL.md
```
Expected: `6`

- [ ] **Step 7: 验证方案包八组件和契约都在**

```bash
f=/home/marine.huang/.zcode/skills/design-grounding/SKILL.md
grep -c "方案包契约" "$f"
grep -c "回炉" "$f"
grep -c "技术规约" "$f"
```
Expected: 三项均 `≥ 1`。

- [ ] **Step 8: 无 git 提交(repo 外)**

---

## Task 7: 标记 spec-preflight 废弃

**Files:**
- Modify: `/home/marine.huang/.zcode/skills/spec-preflight/SKILL.md`(仅改 frontmatter)

**Interfaces:**
- Consumes: spec 第 7 节
- Produces: spec-preflight 标记 deprecated,避免破坏现有引用

- [ ] **Step 1: 读 spec-preflight SKILL.md 前 5 行确认当前 frontmatter**

```bash
head -5 /home/marine.huang/.zcode/skills/spec-preflight/SKILL.md
```
Expected: 显示现有 frontmatter(name/description),确认要修改的位置。

- [ ] **Step 2: 在 frontmatter 加 deprecated 字段**

用 Edit 工具,把:
```
---
name: spec-preflight
description: Use when an architecture, module, feature, behavior, safety-critical change, or failed scenario needs investigation and readiness review before superpowers:brainstorming writes a formal Spec.
---
```
改为:
```
---
name: spec-preflight
description: DEPRECATED — use design-grounding instead. Use when an architecture, module, feature, behavior, safety-critical change, or failed scenario needs investigation and readiness review before superpowers:brainstorming writes a formal Spec.
deprecated: true
deprecated_by: design-grounding
---
```

- [ ] **Step 3: 验证 deprecated 标记**

```bash
grep -c "deprecated" /home/marine.huang/.zcode/skills/spec-preflight/SKILL.md
```
Expected: `≥ 2`(description 里的 DEPRECATED + deprecated 字段)。

- [ ] **Step 4: 验证 references 目录未被改动**

```bash
ls /home/marine.huang/.zcode/skills/spec-preflight/references/
```
Expected: 仍是 `architect-contract.md  granularity-routing.md  preflight-brief-template.md`(未删)。

- [ ] **Step 5: 无 git 提交(repo 外)**

---

## Task 8: 端到端验证 + git 提交计划文档

**Files:**
- 无文件创建/修改,仅验证
- git 提交:本计划文档(已在 worktree repo 内)

**Interfaces:**
- Consumes: 全部前 7 个 Task 的产出
- Produces: 验证报告

- [ ] **Step 1: 验证文件结构齐全(6 文件)**

```bash
d=/home/marine.huang/.zcode/skills/design-grounding
echo "=== SKILL.md ==="; test -f "$d/SKILL.md" && echo OK || echo MISSING
echo "=== design-log-standard ==="; test -f "$d/references/design-log-standard.md" && echo OK || echo MISSING
echo "=== step1 ==="; test -f "$d/references/step1-decision-discovery.md" && echo OK || echo MISSING
echo "=== step2 ==="; test -f "$d/references/step2-grilling.md" && echo OK || echo MISSING
echo "=== step3 ==="; test -f "$d/references/step3-deep-research.md" && echo OK || echo MISSING
echo "=== step5 ==="; test -f "$d/references/step5-design-it-twice.md" && echo OK || echo MISSING
```
Expected: 全部 `OK`,无 `MISSING`。

- [ ] **Step 2: 验证所有 ID 前缀在全局一致(SKILL.md + references 聚合)**

```bash
d=/home/marine.huang/.zcode/skills/design-grounding
for p in "DP-" "TD-" "BL-" "SC-" "VR-" "ALT-" "TS-"; do
  cnt=$(cat "$d/SKILL.md" "$d"/references/*.md | grep -o "$p" | wc -l)
  echo "$p: $cnt"
done
```
Expected: 每个前缀 `≥ 1`(八类 ID 体系全局可用)。

- [ ] **Step 3: 验证 SKILL.md 对 5 个 references 的引用闭环**

```bash
d=/home/marine.huang/.zcode/skills/design-grounding
for r in design-log-standard step1-decision-discovery step2-grilling step3-deep-research step5-design-it-twice; do
  ref_exists=$(test -f "$d/references/$r.md" && echo yes || echo no)
  cited=$(grep -c "$r" "$d/SKILL.md")
  echo "$r: file=$ref_exists cited_in_skill=$cited"
done
```
Expected: 每行 `file=yes` 且 `cited_in_skill ≥ 1`(引用闭环)。

- [ ] **Step 4: 验证 spec-preflight 已废弃但不删**

```bash
grep "deprecated" /home/marine.huang/.zcode/skills/spec-preflight/SKILL.md | head -3
test -d /home/marine.huang/.zcode/skills/spec-preflight/references && echo "references 目录保留"
```
Expected: 显示 deprecated 标记 + `references 目录保留`。

- [ ] **Step 5: 提交计划文档到 worktree repo**

```bash
cd /home/marine.huang/Code/mass-l3/.worktrees/design-grounding-skill
git add docs/superpowers/plans/2026-07-16-design-grounding-skill.md
git commit -m "docs(plan): implement design-grounding skill

8 个 Task 的实施方案:
- Task1-5: 创建 5 个 references(日志标准/step1/step2/step3/step5)
- Task6: 创建 SKILL.md 主文件(流程编排+Step4/6内联)
- Task7: 标记 spec-preflight 废弃
- Task8: 端到端验证

每个 Task 含文件创建+验证步骤+预期输出。验证靠
文件齐全性/frontmatter/引用闭环/ID一致性检查。"
```
Expected: commit 成功。

- [ ] **Step 6: 确认 worktree 分支状态**

```bash
cd /home/marine.huang/Code/mass-l3/.worktrees/design-grounding-skill
git log --oneline -3
```
Expected: 显示 spec 提交 + plan 提交。

---

## Self-Review

### 1. Spec 覆盖检查

| Spec 要求 | 对应 Task |
|---|---|
| 机制A 决策树状态文件落盘 | Task1(design-log-standard.md) |
| 机制B 证据权重与重构支持 | Task6(SKILL.md 机制B 段) + Task1(日志头模式字段) |
| 机制C 技术分解 | Task2(step1 技术分解触发) + Task3(step2 子模块专项) |
| Step1 调研发现决策点 | Task2 |
| Step2 grilling 三视角+盲区 | Task3 |
| Step3 自主深调+并行subagent | Task4 |
| Step4 汇总推荐+DECOMPOSITION_INCOMPLETE | Task6(SKILL.md 内联) |
| Step5 DESIGN-IT-TWICE+决策卡片 | Task5 |
| Step6 术语+技术规约+方案包八组件 | Task6(SKILL.md 内联) |
| 日志标准(ID体系/骨架/硬规则) | Task1 |
| brainstorming 衔接(方案包注入+回炉) | Task6(SKILL.md 移交段) |
| 废弃 spec-preflight | Task7 |
| 文件结构(主文件+5references) | Task1-6 |

无遗漏。所有 spec 第 2-7 节要求均有对应 Task。

### 2. Placeholder 扫描

计划中无 TBD/TODO/待定。所有内容来自 spec,reference 文件内容给出了完整的小节骨架和要点。

### 3. 命名/ID 一致性

- 八类 ID 前缀(DP-/TD-/BL-/RNN/SC-/VR-/ALT-/TS-)在 Task1-6 中一致使用。
- 方案包八组件在 Task6(SKILL.md)中顺序固定:术语表/技术规约表/决策卡片集/证据矩阵/技术分解树/弃用方案/需求场景+验收/已知冲突。
- 决策卡片七维在 Task5 和 Task6 中一致:来源/工程验证/技术分解/失效边界/实现风险/可测性/推荐度。

无不一致。
