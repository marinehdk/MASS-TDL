# Role Review Prompt — M4-hat · RFC-009 IvP 实现路径决策

> **粘贴此文件全文到新 Claude 会话**，然后说"开始评审"。
>
> ⚠️ **前置依赖**：此评审须在 `01-legal-hat-rfc009.md` 法务-hat 评审完成并获得法律结论后进行。请将法务-hat 的 sign-off 结论（方案 A/B/C + 法律风险评级）一并粘贴到会话里。

---

你现在扮演的角色是 **M4 Behavior Arbiter 模块负责人（M4-hat）**。

你的任务是基于法务-hat 的法律结论，在 RFC-009 的三个 IvP 实现候选方案中做出最终决策，并评估该决策对 D2.1 M4 实装工作量的影响。

---

## 你需要阅读的文件

**主文件**：`docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md`

完整阅读。重点关注：
- §2 MOOS-IvP license 实证结论
- §3 三候选方案决策矩阵（12 个单元）
- §4 推荐段（当前为空/待填）
- §5 法务-hat sign-off 段（填入法务-hat 结论）

**参考文件**（按需阅读）：
- `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md`：M4 的完整设计，理解 IvP 在架构中的位置（搜索 `IvP` 或 `utility aggregator`）
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`：搜索 `D2.1` 查看 M4 实装的时间窗口（6/16–7/31，约 6 周）

---

## 三个候选方案详情

### 方案 A — 引入 libIvP（LGPL v3 动态链接）

**技术路径**：
- 从 moos-ivp 仓库提取 `lib_ivp`（LGPL v3）
- 以动态链接（.so）方式集成到 M4 Python/C++ 混合实现
- M4 调用 `IvPDomain` + `IvPFunction` 接口定义行为函数
- `IvPSolver` 执行 utility 最大化求解

**优势**：
- IvP 语义完整（behavior isolation、utility aggregation 均有成熟实现）
- 已有 MOOS-IvP 社区文档和示例
- 最小化 M4 自研工作量（~0.5pw 适配层）

**风险**：
- LGPL v3 动态链接的法律边界有争议（法务-hat 已评审）
- 依赖上游库版本稳定性
- CCS 审计时需提供 libIvP 来源和 license 文档

---

### 方案 B — 自实现 minimal IvP

**技术路径**：
- 仅实现 IvP 核心概念：behavior-set（每个行为输出一个 piecewise linear utility function over [ψ, u] space）+ utility aggregator（逐格取 weighted sum，求 argmax）
- 不复制 libIvP 代码，只复现算法思想（算法本身不受版权保护）
- 决策空间：航向 ψ ∈ [0°, 360°)，速度 u ∈ [0, max_speed]，网格分辨率可配置
- 与 M6 COLREGs Reasoner 的接口契约不变（M6 仍输出 COLREGs 行为函数）

**优势**：
- 零 license 风险
- 代码完全自控，可针对 SIL 2 要求做极简化实现
- CCS 审计友好（无外部库依赖）

**劣势**：
- 额外 ~1.5pw 开发工作量（在 D2.1 6/16–7/31 窗口内需吸收）
- IvP 语义完整性需要自行验证（边界行为、数值稳定性）
- 如果后续需要完整 MOOS-IvP 功能，迁移成本较高

---

### 方案 C — 替换为 RRT\*-CBF

**技术路径**：
- 完全放弃 IvP 多目标仲裁范式
- 改用 RRT\*（快速探索随机树）规划候选路径 + CBF（Control Barrier Function）确保安全约束
- M6 COLREGs Reasoner 的输出接口需重设计（不再是 utility function，而是 constraint set）

**优势**：
- 算法成熟，有丰富文献支持
- 与 M5 Mid-MPC 的接口耦合更自然

**劣势**：
- 接口破坏性变更：M6 整个输出格式需重设计（估计 +3pw M6 额外工作量）
- 不符合当前架构设计（M4 详设已基于 IvP 范式）
- DEMO-2（7/31）前完成 M4+M6 同步重构风险极高

---

## 决策约束

你的选择必须在以下约束内：

1. **时间约束**：D2.1 M4 实装窗口 6/16–7/31（约 6 周），DEMO-2 不可取消
2. **工作量约束**：v3.0 计划 M4 分配 5.5pw，已含 IvP 核心实装。方案 A 可在此范围内；方案 B 需 +1.5pw（从哪里来？）；方案 C 会拖垮 M6
3. **法务约束**：以法务-hat sign-off 的结论为准（方案 A 是否法律可行）
4. **CCS 约束**：外部库依赖需要 license 文档归档；自实现代码 CCS 审计最干净

---

## 你需要输出的评审结论

```
## M4-hat Sign-off — RFC-009 IvP Implementation Path

**法务-hat 输入**（复述法务-hat 结论）：
方案 [A/B/C]，法律风险 [高/中/低]，附加条件：____

**M4-hat 决策**：方案 [A/B/C]

**决策理由**（≤200字）：
[为什么在法务结论基础上选择该方案，工程视角的核心考量]

**工作量影响**：
- D2.1 M4 实装工作量调整：原 5.5pw → 实际 ____pw
- 额外工作量来源（如方案 B +1.5pw）：[从哪个 D-task buffer 挤出，或触发 B4 contingency]

**接口影响**：
- M6 COLREGs Reasoner 接口是否需要修改？[是/否 + 说明]
- M5 Tactical Planner 接口是否需要修改？[是/否 + 说明]

**DEMO-2（7/31）交付物**：
[在选定方案下，7/31 M4 能展示什么，不能展示什么]

**风险**（≤100字）：
[选定方案的主要执行风险]

签字：M4-hat ✅ [日期]
```

---

## 评审纪律

- 这是一个**不可逆的技术架构决策**，一旦确定，D2.1 实装就基于它。请认真权衡，不要因为工作量低就选 A，也不要因为"感觉更干净"就选 B。
- 必须先消化法务-hat 结论，再给出 M4-hat 决策——两者是串行关系。
- 输出必须包含工作量调整数字（是否触发 B4 contingency 的判断依据）。
