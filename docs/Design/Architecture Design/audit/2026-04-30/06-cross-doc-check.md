# 阶段 5.1 · 跨文档对照（vs `docs/Doc From Claude/`）

> 对比 v1.0（`MASS_ADAS_L3_TDL_架构设计报告.md`）与早期研究稿，检查：
> 1. 漏吸纳论断（早期提出但 v1.0 漏吸纳）
> 2. 静默推翻清单（早期立场被 v1.0 推翻但无显式弃用理由）
> 3. 术语漂移
>
> 完成时间：2026-05-05
>
> 对照文件清单：
> - `2026-04-28-tactical-colav-research-v1.2.md`（1554 行 — 最新研究稿）
> - `2026-04-28-l3-tdcal-architecture-design-spec.md`（589 行 — TDCAL 架构 spec）
> - `2026-04-28-tactical-colav-research-patch-d.md`（patch-D，§16 [R21] 来源）
> - `L3 实现框架裁决汇总.md`（477 行 — 实现框架决策汇总）
> - `MASS L3 Tactical Decision Layer (TDL) 功能架构深度研究报告.md`（497 行）

---

## 漏吸纳论断（早期提出但 v1.0 漏吸纳）

| # | 论断 | 早期来源 | v1.0 状态 | 严重性 | finding ID |
|---|---|---|---|---|---|
| 1 | **L3 → L2 反向反馈通道**（`route_replan_request`） | `L3 实现框架裁决汇总.md` §B-01 | 完全删除（v1.0 §15 仅 L2→L3 入口，无反向） | P1 | F-P1-D4-035 |
| 2 | 多目标冲突消解需要图搜索 / CSP 算法（不仅仅是优先级排序） | `2026-04-28-l3-tdcal-architecture-design-spec.md` §3.2（"多目标冲突消解器"插件） | 完全删除（v1.0 M4 仅用 IvP 多目标优化，未定义 5 层冲突消解路径） | P2 | F-P2-D9-040 |
| 3 | TSS（Rule 10）须在 Stage 3 Checker-T 增加多边形约束检查 | `L3 实现框架裁决汇总.md` §B-02（TSS 路径规划感知缺失） | 部分提及（M6 有 Rule 10 条款，但 §10 MPC 无 TSS 几何约束） | P2 | F-P2-D9-041 |
| 4 | Emergency Channel 触发条件应包括 Checker-T 连续失败（非仅 CPA<阈值） | `2026-04-28-l3-tdcal-architecture-design-spec.md` §3.4 Stage 4 | 部分提及（M7 有 Checker-T 失败处理，但与 Emergency Channel 级联关系未明确） | P2 | F-P2-D3-036 |
| 5 | Vessel Dynamics Plugin 在 FCB 高速段（25–35 节）需在线 GP/SI 参数辨识 | `2026-04-28-l3-tdcal-architecture-design-spec.md` §3.3 + 行 257 | 部分提及（M5 有插件架构，但无在线辨识流程） | P3 | F-P3-D9-042 |

> **观察**：5 条漏吸纳中 4 条来自 spec/裁决汇总，1 条来自研究稿。最严重的（#1 反向通道）是架构闭环性问题，须 v1.1 patch；其余 4 条可作 v2.0 / spec part 2 扩展。

---

## 静默推翻清单（v1.0 推翻早期立场，但有/无弃用理由）

| # | 早期立场 | v1.0 推翻立场 | v1.0 是否给出弃用理由 | 评估 |
|---|---|---|---|---|
| 1 | CMM 应作为系统内部状态机实现（心智模型驱动） | CMM 不在系统内实现状态机；通过 SAT-1/2/3 三层接口对外可见（v1.0 §2.4） | ✅ 是（§2.4 引用 Veitch 2024 + Chen SAT 模型，论证"系统内部 CMM 导致语义鸿沟"） | **正确推翻** — 决策三 |
| 2 | ODD 可作为 TDL 内部监控模块（旁路架构） | ODD 是 TDL 整体组织原则，驱动模式切换（v1.0 §2.2） | ✅ 是（§2.2 引用 Rødseth 2022，论证"旁路架构导致一致性危机"） | **正确推翻** — 决策一 |
| 3 | M6 COLREGs Reasoner 可嵌入 M4 Behavior Arbiter | M6 必须独立存在（v1.0 §2.3 表格） | ✅ 是（引用 DNV-CG-0264 要求 COLREGs 可独立验证） | **正确推翻** — 决策二局部 |

> **观察**：3 条静默推翻全部有充分弃用理由（学术 + 规范双层支撑）。这是 v1.0 最强的部分——架构演进路径透明可追溯。**无需补救**。

---

## 术语漂移

### 1. 文档名词漂移

| 早期稿 | v1.0 | 影响 |
|---|---|---|
| **L3-TDCAL**（Tactical Decision & Collision Avoidance Layer） | **L3 TDL**（Tactical Decision Layer） | 仅文档命名差异，无实质功能差异；建议在 v1.1 加术语对照附录注明 TDL = TDCAL 的简化命名 |

### 2. 模块顺序漂移（M5 ↔ M6 编号交换）

| 早期稿（深度研究报告.md）| v1.0 |
|---|---|
| M5 Tactical Planner（先）→ M6 COLREGs Reasoner（后） | M5 Tactical Planner 排在 §10；**M6 COLREGs Reasoner 排在 §9（先）** |

- v1.0 §4 架构总览中模块编号 M5/M6 顺序未变（M5=Tactical Planner，M6=COLREGs Reasoner），但**章节编号顺序与模块编号不一致**
- 影响：阅读 §9（标"M6"）→ §10（标"M5"）会引发文档导航困惑
- 建议：v1.1 patch 修正章节顺序（§9 改为 M5，§10 改为 M6）或显式注明"按主审依赖顺序排列"

### 3. Stage 1–4 → Module M1–M8 映射缺失（关键缺失）

| 早期稿 | 含义 |
|---|---|
| Stage 1（看） | 看局——感知融合（M2 World Model 前身） |
| Stage 2（判） | 判局——规则判断（M6 COLREGs Reasoner 前身） |
| Stage 3（动） | 动作——轨迹规划（M5 Tactical Planner 前身） |
| Stage 4（盯） | 并行监控（M7 Safety Supervisor + M1 ODD Manager 前身） |

v1.0 完全删除 Stage 概念，改用模块名（M1–M8）。**无 Stage→Module 对照表**，新读者无法追溯早期研究的"看/判/动/盯"框架与 v1.0 模块的对应关系。

- 严重性：P2（finding F-P2-D1-039）
- 建议：v1.1 patch 加术语对照附录（参见 `MASS L3 Tactical Decision Layer (TDL) 功能架构深度研究报告.md` §3）

### 4. Doer-Checker 实现含义漂移

| 早期稿（TDCAL 设计.md §4） | v1.0（§11 M7） |
|---|---|
| Checker-D（决策层） + Checker-T（轨迹层），分别校验 M2 和 M3 输出 | M7 包含 IEC 61508 + SOTIF 双轨；Checker-D / Checker-T 命名消失 |

- 影响：架构功能基本不变（IEC 61508 ≈ Checker-D 的简化版；SOTIF ≈ Checker-T 的扩展），但**两层 Checker 的概念边界变模糊**。
- 建议：v1.1 §11 中加注释"M7 IEC 61508 轨道继承 Checker-D 设计；SOTIF 轨道继承 Checker-T 设计"

---

## 总结

**阶段 5.1 整体观察**：

1. **v1.0 是对早期研究的结构性升级，非推翻**：3 条静默推翻全部带有充分学术 / 规范理由（Veitch + Chen + Rødseth + DNV-CG-0264），这是 v1.0 最透明的部分，**无需补救**。

2. **5 条漏吸纳中只有 1 条 P1**（反向通道 `route_replan_request`），其余 4 条可作 v1.1 patch 或 spec part 2 扩展。

3. **术语漂移问题主要源于"研究阶段多维度探索"→"设计阶段规范化命名"的自然演进**：核心是 Stage→Module 映射的文档化缺失，建议 v1.1 加术语对照附录解决。

4. **CLAUDE.md 中已显式标记部分扩展项**（如 §10 阅读入口推荐顺序），但未列出"v1.0 故意搁置的业务范围扩展"（如 #1 反向通道、#2 CSP 多目标消解）。这是文档边界透明性的次要缺口。

**Phase 5.1 新生 finding 共 7 条**（同步到 `_temp/findings-raw.md`）：
- P1: F-P1-D4-035（L3→L2 反向通道缺失）
- P2: F-P2-D9-040（CSP 多目标消解缺失）、F-P2-D9-041（TSS 多边形约束缺失）、F-P2-D3-036（Emergency Channel 触发条件缺失）、F-P2-D1-039（Stage→Module 映射缺失）
- P3: F-P3-D9-042（Vessel Dynamics 在线辨识缺失）

> 注：finding ID 编号继续 Phase 3 的全局序列（030 之后）。
