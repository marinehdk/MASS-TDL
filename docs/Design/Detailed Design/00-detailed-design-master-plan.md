# 详细功能设计主计划

> **架构基线**：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告_v1.1.1.md`
>
> **启动条件**：v1.1.1 复审通过（A 档加严版，全表综合分 2.89 / P0=0）；6 条新 finding 全部关闭
>
> **创建时间**：2026-05-05

---

## 1. 范围与目标

### 1.1 范围（IN）

本主计划覆盖 6 个**可立即启动**的模块详细功能设计（无外部跨团队对齐依赖）：

| 模块 | 优先级 | 估计行数 | 主要工作 |
|---|---|---|---|
| **M1** ODD/Envelope Manager | 高 | ~1000 | 状态机详细设计 + Conformance_Score 算法 + 事件型补发逻辑 |
| **M2** World Model | 高 | ~1200 | 三视图融合 + CPA/TCPA 计算 + COLREG 几何预分类 + 时间对齐 |
| **M3** Mission Manager | 中 | ~600 | VoyageTask 校验 + ETA 投影 + RouteReplanRequest 触发逻辑 |
| **M4** Behavior Arbiter | 高 | ~1000 | IvP 求解器实现 + 行为字典 + 多目标仲裁权重 |
| **M6** COLREGs Reasoner | 高 | ~1200 | Rule 5–19 状态机 + ODD-aware 参数 + 规则库插件接口 |
| **M8** HMI/Transparency Bridge | 中 | ~800 | SAT 自适应触发器 + ToR 协议 + 接管 UI 状态机 |

**合计**：~5800 行详细设计文档

### 1.2 范围（OUT — 不在本主计划）

- **M5 Tactical Planner**：依赖 L4 跨团队对齐确认（方案 B AvoidancePlan schema）— 留 Y-2 阶段（跨团队 RFC 完成后启动）
- **M7 Safety Supervisor**：依赖 X-axis Checker 跨团队对齐 + Reflex Arc spec 量化 — 留 Y-2 阶段
- **L1/L2/L4/L5/Multimodal Fusion** 等其他层模块：属于其他团队职责
- **算法实现代码**：详细设计阶段产出 spec，不写代码（详见 §5 阶段定义）

### 1.3 第 2 批补充（M5 + M7 — 2026-05-06 完成）

```
M5 + M7 详细设计已基于 RFC 提议方案产出（草稿状态，待跨团队对齐确认后微调）：
   ├── M5 详细设计（1241 行）— 基于 v1.1.1 §10 + RFC-001 方案 B
   │   └── 待 L4 团队对 RFC-001 三模式切换的正式确认 → 可能微调 AvoidancePlanMsg schema
   └── M7 详细设计（1673 行）— 基于 v1.1.1 §11 + RFC-003 + RFC-005
       ├── 待 X-axis Checker 团队对 RFC-003 enum CheckerVetoNotification 的正式确认
       └── 待 Reflex Arc 团队对 RFC-005 触发阈值量化的正式确认
```

---

## 2. 详细功能设计模板（强制）

每个模块的详细设计文档**必须**遵循以下结构：

```markdown
# M{N} {模块名} 详细功能设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-DD-M{N}-001 |
| 版本 | v1.0 |
| 日期 | 2026-XX-XX |
| 状态 | 草稿 / 评审中 / 正式 |
| 架构基线 | v1.1.1（章节锚点：§{N})|
| 上游依赖 | （列出消费的消息，引用 v1.1.1 §15）|
| 下游接口 | （列出发布的消息，引用 v1.1.1 §15）|

## 1. 模块职责（Responsibility）

[简短陈述模块的核心职责，引用 v1.1.1 §{N}.1]

## 2. 输入接口（Input Interfaces）

### 2.1 消息列表

| 消息 | 来源 | 频率 | 必备字段 | 容错处理 |
|---|---|---|---|---|

### 2.2 输入数据校验

[字段范围 / 时间戳 / 置信度阈值 / 缺失/超时处理]

## 3. 输出接口（Output Interfaces）

### 3.1 消息列表

[同上格式]

### 3.2 输出 SLA

[频率保证 / 时延 / 数据新鲜度 / 失效降级]

## 4. 内部状态（Internal State）

### 4.1 状态变量
### 4.2 状态机（如适用）
### 4.3 持久化（哪些状态需 ASDR 记录）

## 5. 核心算法（Core Algorithm）

### 5.1 算法选择
### 5.2 数据流
### 5.3 关键参数（含 [HAZID 校准] 标注）
### 5.4 复杂度分析（时间 + 空间）

## 6. 时序设计（Timing Design）

### 6.1 周期任务
### 6.2 事件触发任务
### 6.3 延迟预算

## 7. 降级与容错（Degradation & Fault Tolerance）

### 7.1 降级路径（DEGRADED / CRITICAL / OUT-of-ODD）
### 7.2 失效模式分析（FMEA — 与 §11 M7 对齐）
### 7.3 心跳与监控

## 8. 与其他模块协作（Collaboration）

### 8.1 与上下游模块的握手
### 8.2 SAT-1/2/3 输出（详见 §12 M8）
### 8.3 ASDR 决策追溯日志格式

## 9. 测试策略（Test Strategy）

### 9.1 单元测试
### 9.2 模块集成测试
### 9.3 HIL 测试场景
### 9.4 关键 KPI

## 10. 实现约束（Implementation Constraints）

### 10.1 编程语言 / 框架
### 10.2 实时性约束
### 10.3 SIL / DAL 等级要求
### 10.4 第三方库约束（避免共享路径，详见决策四独立性）

## 11. 决策依据（References）

[引用架构 §{N}.4 / 学术文献 / 工业先例]

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
```

---

## 3. 详细设计 DoD

每个模块详细设计**必须**满足以下 DoD：

- [ ] 12 个章节全部填写（模板 §1–§12）
- [ ] 输入/输出接口明确引用 v1.1.1 §15 IDL
- [ ] 所有算法关键参数含 `[HAZID 校准]` 标注或可追溯来源
- [ ] 降级路径覆盖 DEGRADED / CRITICAL / OUT-of-ODD 三态
- [ ] FMEA 与 v1.1.1 §11 M7 假设违反清单一致
- [ ] SAT-1/2/3 输出 schema 与 v1.1.1 §12 M8 接口一致
- [ ] ASDR 决策日志格式符合 v1.1.1 §15.1 ASDR_RecordMsg
- [ ] 引用具体 v1.1.1 章节 + 学术 / 工业证据
- [ ] 测试策略含 ≥ 3 个 HIL 场景
- [ ] 复杂度分析（时间/空间）

---

## 4. 启动顺序与依赖

### 4.1 优先级矩阵

| 模块 | 算法核心？ | 跨团队依赖？ | 优先级 |
|---|---|---|---|
| M1 ODD/Envelope Manager | ✓ | ❌（无外部跨团队）| **高 — 第 1 批** |
| M2 World Model | ✓ | ⚠️（M2 ← Multimodal Fusion，但 v1.1.1 §6.4 已定义聚合策略，可独立设计）| **高 — 第 1 批** |
| M3 Mission Manager | ❌ | ⚠️（M3 ← L1，但 v1.1.1 §15.1 VoyageTask IDL 已定义）| **中 — 第 1 批** |
| M4 Behavior Arbiter | ✓ | ❌ | **高 — 第 1 批** |
| M6 COLREGs Reasoner | ✓ | ❌ | **高 — 第 1 批** |
| M8 HMI/Transparency Bridge | ❌ | ❌ | **中 — 第 1 批** |
| **M5 Tactical Planner** | ✓ | ✓（M5↔L4 待 RFC-001）| **第 2 批 — 跨团队对齐后** |
| **M7 Safety Supervisor** | ✓ | ✓（M7↔X-axis Checker 待 RFC-003 + RFC-005）| **第 2 批 — 跨团队对齐后** |

### 4.2 第 1 批（本计划覆盖）— 并行启动

```
启动条件：v1.1.1 已正式生效
执行方式：6 个模块详细设计并行（Claude subagent 并行执行）
预计时长：12-16 周（人类工程师视角）；本次 Claude 产出第一版草稿
```

### 4.3 第 2 批（M5 + M7）— 跨团队对齐完成后

```
启动条件：
  ├── RFC-001 通过（M5↔L4 schema）
  ├── RFC-003 通过（M7↔X-axis Checker 协调协议）
  ├── RFC-005 通过（Reflex Arc spec 量化）
执行方式：M5 + M7 详细设计单独启动（不在本主计划内）
```

---

## 5. 阶段定义

### 5.1 详细设计阶段（本计划）

- **产出**：详细设计 spec 文档（每模块 ~500–1500 行 markdown）
- **不产出**：可执行代码（C++/Python）
- **验证方式**：架构师 / SIL 评估师 / CCS 验船师 spec 审查
- **依赖**：v1.1.1 架构 + 本主计划模板

### 5.2 详细设计 → 实现的边界

详细设计的 DoD 满足后，**实现阶段**才能启动：
- 实现阶段产出：C++/Python 源码 + 单元测试 + HIL 测试结果
- 实现阶段不在本主计划内（属下一里程碑）

---

## 6. 与其他工作流的关系

| 工作流 | 文件 | 与本主计划关系 |
|---|---|---|
| 跨团队接口对齐 | `docs/Design/Cross-Team Alignment/` | 提供 M5/M7 的接口契约（解锁第 2 批）|
| FCB 初始 HAZID | `docs/Design/HAZID/` | 提供详细设计中所有 [HAZID 校准] 参数的实际值；HAZID 通过后回填详细设计 |
| 架构审计交付物 | `docs/Design/Architecture Design/audit/2026-04-30/` | 详细设计 spec 须不引入新的 finding；与审计 99-followups.md 同步 |

---

## 7. 关键风险

| 风险 | 缓解 |
|---|---|
| 详细设计草稿引入新架构问题 | 每模块详细设计完成后，运行简化的"模块级审计"（D4/D6/D7 维度）|
| HAZID 校准结果回填详细设计的工作量被低估 | 在详细设计 spec 中明确所有 [HAZID 校准] 标注 + 回填位置；HAZID 完成后运行批量回填脚本 |
| M1/M2/M3/M4/M6/M8 之间接口契约理解不一致 | 强制每模块详细设计的输入/输出引用 v1.1.1 §15 具体 IDL；不允许"自定义" |
| 详细设计 spec 评审时间过长导致进度延误 | 模块设计完成后立即提交单模块评审，不等所有模块齐全 |

---

## 8. 8 模块详细设计交付物清单（2026-05-06 全部完成）

| 模块 | 交付文件 | 行数 | 状态 |
|---|---|---|---|
| M1 | `M1-ODD-Envelope-Manager/01-detailed-design.md` | 1066 | ✅ 草稿完成 |
| M2 | `M2-World-Model/01-detailed-design.md` | 1021 | ✅ 草稿完成 |
| M3 | `M3-Mission-Manager/01-detailed-design.md` | 1326 | ✅ 草稿完成 |
| M4 | `M4-Behavior-Arbiter/01-detailed-design.md` | 982 | ✅ 草稿完成 |
| **M5** | `M5-Tactical-Planner/01-detailed-design.md` | **1241** | ✅ 草稿完成（依 RFC-001 方案 B；待 L4 跨团队对齐）|
| M6 | `M6-COLREGs-Reasoner/01-detailed-design.md` | 1280 | ✅ 草稿完成 |
| **M7** | `M7-Safety-Supervisor/01-detailed-design.md` | **1673** | ✅ 草稿完成（依 RFC-003 + RFC-005；待 X-axis Checker + Reflex Arc 跨团队对齐）|
| M8 | `M8-HMI-Transparency-Bridge/01-detailed-design.md` | 917 | ✅ 草稿完成 |

**合计**：9506 行模块详细设计 + 253 行主计划 = **9759 行**

---

## 9. 后续动作

1. ✅ 主计划 + 模板（本文档）
2. ⏳ 6 个 subagent 并行启动详细设计（本会话执行）
3. ⏳ 主 agent 完成 6 个 RFC + HAZID 工作包（本会话执行）
4. ⏳ subagent 回报 → 主 agent 整合 + 一致性检查
5. ⏳ 用户判断是否进入下一阶段（M5/M7 详细设计 / 实现阶段）
