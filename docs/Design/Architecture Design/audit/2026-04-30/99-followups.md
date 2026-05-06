# 后续跟进项（非阻塞）

> 不阻断 v1.1 patch 合并，但需在后续 spec part 2 / 实船试航 / HAZID 校准时处理。
>
> 完成时间：2026-05-05

---

## P3 Findings 详情

### F-P3-D1-029 · §16 孤立引用 [R22] / [R13]

- **现象**：通读 v1.0 §1–§15 全章时未见 [R22] Neurohr 2025（arXiv:2508.00543）+ [R13] Albus 1991（NIST RCS）的内联引用，但两者列于 §16 参考文献。
- **可能原因**：
  - (a) 编写过程中曾使用，后来章节重构时删除了内联但保留了引用条目（最可能）
  - (b) 计划引用但忘了写入正文
  - (c) 拷贝自他人模板的错误（不太可能）
- **影响**：CCS 审查员若复核证据链，孤立引用会引发"v1.0 是否真的读过这些来源"的质疑
- **建议处理**：
  - [R22] Neurohr 2025（SOTIF + ROC 认证）→ 看是否需要在 §11.3 SOTIF 设计或 §12 ROC 接管协议中补引；如不必要，从 §16 删除
  - [R13] Albus 1991（NIST RCS 层次控制）→ 看 §8 M4 Behavior Arbiter 是否参考了 NIST RCS；如是，§8 决策依据补引 [R13]；否则从 §16 删除

### F-P3-D9-042 · FCB 高速段无在线参数辨识

- **现象**：早期 spec `2026-04-28-l3-tdcal-architecture-design-spec.md` §3.3 + 行 257 建议 Vessel Dynamics Plugin 在 FCB 高速段（25–35 节）支持在线 GP（Gaussian Process）/ SI（System Identification）参数辨识。v1.0 §13.4 水动力插件接口仅提供静态预测，无在线辨识流程。
- **影响**：FCB 高速段水动力强非线性，静态参数误差累积可能影响 MPC 预测精度
- **判断为 P3 而非 P2 的理由**：实船试航后 HAZID 校准可补救（不阻塞 AIP）
- **建议处理**：
  - 写入 spec part 2 / FCB 实船 HAZID 议题
  - 若 HAZID 显示必要 → §13.4 增加插件接口 `update_params(samples)` 方法 + 配套 SOTIF 监控（M7 监控参数辨识收敛性）
  - 若 HAZID 显示不必要 → §13.4 决策依据补注 "v1.0 仅静态参数；高速 FCB 在线辨识经实船 HAZID 评估后不需要"

---

## FOLLOWUP 项（来自阶段 1/2/5）

### 阶段 1 引用核验留 🔴/⚫ 项

源自 `02-references-prescan.md` 的预扫结果（5 项 🔴）：
- [R21] Yasukawa & Sano 2024：Phase 1 已标"可能幻觉"，Phase 3 通读后升级为 P1（F-P1-D9-024）；进入 patch list（08a）
- 其他 4 项 🔴 在 Phase 2 R1–R8 调研中已通过替代来源或上下文澄清，无独立 followup

### 阶段 2 R1 笔记本入库失败的非主流系统

R1 调研笔记本 colav_algorithms 中未找到具体架构细节的工业系统：
- Wärtsilä SmartMove
- Avikus HiNAS（韩国）
- Maersk Mayflower（自主船试航案例）

**建议处理**：FCB 实船试航前可单独走一轮 Sea Machines / Avikus / Wärtsilä 的工业架构对比，作为 v1.2 / v2.0 评估输入。当前 v1.1 patch 不依赖这些系统的细节。

### 阶段 5.1 漏吸纳论断（已识别但留作 spec part 2）

| 论断 | 来源 | 严重性 | 留作处理位置 |
|---|---|---|---|
| 多目标冲突消解 CSP/图搜索 5 层路径 | 早期 spec §3.2 | P2 (F-P2-D9-040) | spec part 2 / v1.2 |
| TSS（Rule 10）多边形约束 | 早期裁决 §B-02 | P2 (F-P2-D9-041) | v1.1 §10 patch |
| Emergency Channel + Checker-T 失败触发 | 早期 spec §3.4 | P2 (F-P2-D3-036) | v1.1 §11 patch |
| FCB 高速段在线参数辨识 | 早期 spec §3.3 | P3 (F-P3-D9-042) | spec part 2 / 实船 HAZID |

### 阶段 5.2 跨团队对齐工作清单

> 这些 finding 修复需要跨团队（L4 / Multimodal Fusion / Deterministic Checker / ASDR / Reflex Arc）协作，不能单独由 L3 团队完成。

| Finding | 跨团队对齐内容 | 责任团队 |
|---|---|---|
| F-P1-D4-031 | M2 World_StateMsg 内部聚合策略 vs Multimodal Fusion 三话题输出（CPA/TCPA 计算放 M2 还是 Fusion？） | L3 + Multimodal Fusion |
| F-P1-D4-032 | M5 输出 schema：方案 A（保留 ψ/u/ROT）vs 方案 B（Avoidance_WP + speed_adj） | L3 + L4 Guidance |
| F-P0-D3-002 + F-P2-D3-036 | M7 ↔ X-axis Checker 协调协议（独立总线 + CheckerVetoNotification 消息） | L3 + Deterministic Checker |
| F-P1-D4-033 | ASDR_RecordMsg IDL 定义 + L3 各模块的 ASDR 输出频率 | L3 + ASDR |
| F-P1-D4-034 | Reflex Arc 触发阈值（CPA/距离/时间）+ 感知输入源 + L3 通知协议 | L3 + Reflex Arc + Multimodal Fusion |
| F-P1-D4-035 | RouteReplanRequest IDL + L2 处理协议 | L3 + L2 Voyage Planner |

---

## CLAUDE.md 同步任务

> 审计中发现 CLAUDE.md 部分章节也需同步更新（不阻塞 v1.1 patch，但保持文档体系一致性）。

| 章节 | 修订内容 | 触发 finding |
|---|---|---|
| §4 决策四（Checker 简单 100×）| 改为"工业实践 50:1~100:1，是设计目标，非规范强制"（与 ADR-001 一致）| F-P0-D1-008 |
| §5 强制约束（CCS 入级）| 加注"CCS i-Ship 流程含 AIP / 基础 / 进阶三阶段（详见 v1.0 §14.3）"| 自然延伸 |
| §10 阅读入口推荐顺序 | 加 "Phase 5 跨层对照所发现的接口差异：M5→L4 / M2 ← Fusion / Reflex Arc / ASDR / X-axis Checker（详见 07-cross-layer-check.md）" | F-P1-D4-031~035 |

---

## 后续审计建议（v1.1 patch 验证后）

1. **patch 验证审计**：v1.1 patch 合并后，对 v1.1 文档运行简化的"Phase 3 + Phase 6"复审，确认所有 P0/P1 finding 关闭，且无新的引入问题
2. **跨团队接口对齐审计**：以本次 Phase 5.2 跨层 finding 为依据，组织 L3 + L2/L4/Multimodal Fusion/Checker/ASDR/Reflex 跨团队接口对齐会议；产出统一的"系统接口契约 spec"作为 §15 的扩展
3. **HAZID 校准任务清单**：所有标注 "[HAZID 校正]" 的参数（F-P1-D6-016/017、F-P2-D6-018、F-P3-D9-042 等）汇总进入 FCB 项目级 HAZID 计划
4. **v1.2 / spec part 2 议题**：CSP 多目标消解（F-P2-D9-040）、FCB 高速在线辨识（F-P3-D9-042）、Wärtsilä/Avikus 工业对比（FOLLOWUP）

---

## 不在审计范围内的事项

以下事项明确**不在本审计范围**，作为知识传承记录：

- v1.0 模块内部的具体算法实现（如 IvP 求解器实现、MPC 数值方法、EKF 滤波参数）— 属于详细设计阶段
- L2 / L4 / Multimodal Fusion 等其他层的内部设计正确性 — 属于其他团队的审计范围
- 软件实现层面（编程语言、ROS2 / DDS 配置、容器化方案）— 属于软件架构阶段
- CCS i-Ship 实际入级申请 — 属于认证流程，本审计仅识别"AIP 申请前置依赖"
