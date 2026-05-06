# L3 战术层架构审计 · 执行摘要

| 字段 | 值 |
|---|---|
| 审计日期 | 2026-04-30 启动 / 2026-05-05 完成 |
| 主审对象 | v1.0 (`MASS_ADAS_L3_TDL_架构设计报告.md`，1168 行) |
| 审计基线 | v3.0 Kongsberg-Benchmarked Industrial Grade |
| **三态结论** | **B 档 — 结构性修订 + ADR** |
| 全表综合分 | **2.35 / 3.0** |
| P0 finding 数 | **5** |
| 总 finding 数 | 40（P0=5 / P1=18 / P2=15 / P3=2）|
| 阶段 2 调研轮 | 8/8 完成（colav_algorithms +91 / safety_verification +64 / maritime_regulations +89 / maritime_human_factors +19）|
| Phase 5 跨层验证 | 4 个 Haiku 4.5 background subagent 并行（Doc From Claude × 1 + Init From Zulip × 3）|

---

## 关键发现（5 条）

### 1. 算法核心方向正确，全部保留（v1.0 最强部分）

阶段 4 三方对照（v1.0 vs v3.0 Kongsberg vs 学术最优）13 对象中：
- **保留 4**：M4 Behavior Arbiter（IvP）、M5 Tactical Planner（双层 MPC + BC-MPC）、M6 COLREGs Reasoner（独立模块）、多船型 Capability Manifest
- **修订 9**：4 决策 + M1/M2/M3/M7/M8
- **替换 0**

R2（学术 COLAV 综述）+ R8（Backseat Driver 工业实例）证实 IvP/MPC/BC-MPC 选择是 2020+ 工业 + 学术金标准。

### 2. 5 条 P0 集中在两个根因上

**根因 A：Doer-Checker 双轨（决策四 + M7）—— 3 条 P0**
- F-P0-D1-008："Checker 简单 100×" 在 [R12] / IEC 61508 / DO-178C 均无量化要求
- F-P0-D3-001：M7 ARBITRATOR 注入轨迹 = Checker 持有规划逻辑（违反 Boeing/NASA Monitor 简化原则）
- F-P0-D3-002：v1.0 §11 完全未提 v3.0 X-axis Deterministic Checker；跨层文档实证已有独立 VETO 协议但无 M7 ↔ Checker 桥接

**根因 B：ODD 框架（决策一 + ODD 框架）—— 2 条 P0**
- F-P0-D1-006：§2.2 引用 Rødseth 2022 支持三轴 H 轴，但 Rødseth 实际是二轴（PMC 全文确认）
- F-P0-D6-015：§3.2 三轴公式 O = E×T×H 无权威来源；§3.6 Conformance_Score 全表权重依赖此

### 3. §15 接口契约总表是最大的跨层缺口

Phase 5.2 跨层对照（vs `docs/Init From Zulip/`）发现 5 条 P1 全部围绕 §15 矩阵：
- **缺 ASDR 接口**（实际 ASDR doc §3.2 + §13 已订阅 L3 多模块）
- **缺 Reflex Arc 协议**（实际 Hardware Override Arbiter §3.1 P2 → L5 已设计；触发阈值未量化）
- **缺 L3 → L2 反向 route_replan_request**（早期裁决 §B-01 已识别）
- **M2 ← Multimodal Fusion 频率/schema 不匹配**（2 vs 4 Hz；CPA/TCPA 缺失；自身状态 50 Hz 独立话题）
- **M5 → L4 层级架构错位**（实际 L4 自生 ψ/u/ROT；M5 输出与 L4 重叠/冲突）

CCS i-Ship 白盒可审计要求接口契约完整，§15 是唯一答卷。

### 4. 决策树命中 B 档（不是早期预判的 A 档）

阶段 4/5 总论曾误判走 A 路径（基于 P0=5 < 6 的简化推理，忽略 A 档 "P0 ≤ 2" 上限）。Phase 6 评分矩阵严格执行 spec §7.6 决策树：

| 档位 | 全表综合分要求 | P0 要求 | 满足？ |
|---|---|---|---|
| A 档（仅 patch）| ≥ 2.5 | ≤ 2 | ❌ |
| **B 档（修订 + ADR）** | [1.5, 2.5] | [3, 5] | **✅** |
| C 档（重写 v2.0）| < 1.5 | ≥ 6 | ❌ |

**B 档**意味着审计不能仅出 patch list（A 档），还须发起 ADR 增量（顶层决策修订）。

### 5. 跨团队对齐工作清单（不阻塞 v1.1 patch 合并，但合并后须立即启动）

| 跨团队对齐项 | 责任团队 |
|---|---|
| M5 → L4 schema（方案 A 或 B）| L3 + L4 Guidance |
| M2 World_StateMsg 内部聚合 | L3 + Multimodal Fusion |
| ASDR_RecordMsg IDL | L3 + ASDR |
| Reflex Arc 触发阈值 + 感知源 + L3 通知 | L3 + Reflex Arc + Multimodal Fusion |
| M7 ↔ X-axis Checker 协调协议 | L3 + Deterministic Checker |
| RouteReplanRequest IDL | L3 + L2 Voyage Planner |

---

## 推荐执行路径（B 档 — 结构性修订 + ADR）

### 第一阶段：架构评审委员会审议（必须先决）

按 v1.0 §1 文档变更要求（"系统架构评审委员会批准"）：

1. **同时审议 ADR-002 + ADR-003**（同根因 P0 + 同核心证据 R3 Rødseth + R7 DNV）
2. **单独审议 ADR-001**（决策四 + §11 重构是独立工作）

### 第二阶段：v1.1 patch 合并（分两批）

**第一批（不依赖 ADR，可立即执行）— 19 项**：
- 旧术语修复（§1.2 / §6 / §7）
- §15 接口契约补全（5 条 P1：ASDR / Reflex / L3→L2 / M2 / M5）
- §10 / §13 / §14.2 字段级修正
- §16 参考文献清理 + 附录 A/B/C 新增

**第二批（依赖 ADR 通过）— 16 项**：
- §2.2 / §2.3 / §2.4 / §2.5 措辞修订（依赖 ADR-001 + ADR-002）
- §3.2 三层概念框架重写（依赖 ADR-003）
- §11 全章重构（依赖 ADR-001）
- §14.4 ODD-Spec 描述（依赖 ADR-002 + ADR-003）

### 第三阶段：跨团队对齐（patch 合并完成后立即启动）

按 §15 接口契约总表的扩展，组织 L3 + 各跨层团队的接口对齐会议，产出统一的"系统接口契约 spec"。

### 第四阶段：v1.1 复审（建议）

合并完成后，对 v1.1 文档运行简化的 Phase 3 + Phase 6 复审，确认：
- 所有 P0/P1 finding 关闭
- 无新引入问题
- 三态结论从 B 档升至 A 档（满足 P0 ≤ 2 + 全表 ≥ 2.5）

### 第五阶段：CCS AIP 申请准备

ADR + patch 完成 + v1.1 复审通过后，才能按 §14.3 路径提交 CCS i-Ship AIP（Approval In Principle）申请。当前 v1.0 的 ODD-Spec 论证链断裂（F-P1-D7-028），不能直接提交。

---

## 阈值取舍 ADR 提示

> 引用 spec §15 业界基准与本 spec 取舍。建议把 A/B/C 结论与阈值写入项目 ADR-000，让 CCS 验船师 / 验证官 / 架构师对齐预期。

| 项 | 本审计取舍 | 业界基准 |
|---|---|---|
| 全表综合分计算 | 简单算术平均（non-N/A cells）| spec §9.2 |
| Severity 分级 | P0/P1/P2/P3 四级 | spec §4 |
| Doer-Checker 简化比例 | 50:1~100:1 设计目标（非规范强制）| Boeing 777 / Airbus FBW [R4] |
| Checker 否决率告警阈值 | > 20%（即 100 周期内 > 20 次否决）| 工业经验值（建议 HAZID 校准）|
| TMR 设计基线 | ≥ 60s | Veitch 2024 RCT [R4] |
| TDL 公式系数 0.6 | TCPA × 60% | 工程经验（HAZID 校准）|
| ODD 子域阈值 | FCB 特定设计值 | 文献范围 TCPA 5–20 min [R2] |
| H 轴 ODD 扩展 | 本设计原创（基于 Rødseth FM 具体化 + IMO D1–D4） | 无直接业界基准（合规性通过 ADR-002 论证）|

---

## 完整交付物清单

| 文件 | 规模 | 状态 |
|---|---|---|
| `00-executive-summary.md`（本文件）| ~150 行 | ✅ |
| `01-mass-coordinate-system.md`（CLAUDE.md §2 镜像）| ~75 行 | ✅ |
| `02-references-prescan.md`（24 条引用预扫）| 129 行 | ✅ Phase 1 |
| `03-evidence-base.md`（8 轮 deep research）| 530 行 | ✅ Phase 2 |
| `04-architecture-judgment.md`（13 对象三方对照）| 556 行 | ✅ Phase 4 |
| `05-audit-report.md`（40 条 finding 定稿）| ~440 行 | ✅ Phase 6 |
| `06-cross-doc-check.md`（vs Doc From Claude）| 105 行 | ✅ Phase 5.1 |
| `07-cross-layer-check.md`（vs Init From Zulip）| 217 行 | ✅ Phase 5.2 |
| `08-audit-grid.md`（14×9 评分矩阵）| ~130 行 | ✅ Phase 6 |
| `08a-patch-list.md`（35 条 P1/P2/P3 字段级 diff）| ~360 行 | ✅ Phase 6 |
| `08c-adr-deltas.md`（3 条 ADR 增量 ADR-001/002/003）| ~270 行 | ✅ Phase 6 |
| `99-followups.md`（P3 + FOLLOWUP + 跨团队 + CLAUDE.md 同步）| ~110 行 | ✅ Phase 6 |
| `_temp/findings-raw.md`（finding 累积草稿）| 993 行 | 临时（B 档完成后可清理）|
| `_temp/refs-extracted.md`（24 条引用提取）| 单独维护 | 临时 |
| `_temp/decision-trace.md`（决策树落点追溯）| ~100 行 | 临时 |

> **注**：08b-v2-architecture.md 不出（仅 C 档需要）。

---

## 审计 DoD 终检（详见 T6.9）

- [x] 13 对象 D9 评判全部完成（阶段 4）
- [x] 24 条引用全部预扫；🔴/⚫ ≤ 5（阶段 1）
- [x] 14×9 评分矩阵全部填分或 N/A；行/列/全表综合分均算出（阶段 6 T6.1+T6.2）
- [x] 阶段 2 NLM deep research 8 轮全部完成；全部由 subagent 异步执行
- [x] 所有 P0/P1 finding 置信度 ≥ 🟡，配 fix 建议
- [x] 跨文档（5.1）+ 跨层（5.2）对照各产 ≥ 1 节
- [x] 摘要给出明确 B 档结论；阈值取舍写入 ADR
- [x] 决策树落点严格按 spec §7.6 计算（修正了阶段 4/5 的预判错误）

---

## 一句话结论

> **v1.0 架构方向正确，但顶层决策一/四 + ODD 框架须经 ADR 修订，§15 接口契约须补全 5 条 P1 跨层接口；建议立即启动架构评审委员会审议 ADR-001/002/003，并行执行 19 条非依赖 patch。预计 4–6 周内完成 v1.1 草稿，CCS AIP 申请可在 v1.1 复审后启动。**
