# M7 Safety Supervisor — Effort Split v2.1

| Field | Value |
|---|---|
| Document | M7 work breakdown — core vs SOTIF explicit split |
| Version | v2.1 (MUST-11) |
| Date | 2026-05-08 |
| Status | Draft — M7-hat sign-off pending (5/12 PM) |
| References | v3.0 plan §0.1 +3.0 pw closure (line 47) · user decision §13.2 (2026-05-07) |

---

## §1 Background

The v3.0 plan allocated M7 Safety Supervisor **9 person-weeks** total (v2.0 had 6 pw). The +3 pw increase was resolved in user decision §13.2 (2026-05-07) as a necessary investment to properly cover both IEC 61508 SIL 2 baseline and ISO 21448 SOTIF analysis. This document makes the split explicit.

---

## §2 M7-core (6 person-weeks, 7/13–8/31)

**Delivery**: D3.7 (target: 8/31 DEMO-3)

### §2.1 Deliverables

| # | Deliverable | Effort | Dependency |
|---|---|---|---|
| 2.1.1 | Doer-Checker arbitration logic implementation | 2.0 pw | D2.7 HARA output (SIF list) |
| 2.1.2 | IEC 61508 SIL 2 baseline implementation (M7 safety functions) | 1.5 pw | D2.5 SIL tool chain; D1.5 V&V Plan |
| 2.1.3 | FMEDA table (≥20 failure modes, M7 internal) | 1.0 pw | D2.7 HARA + external FMEDA M1 |
| 2.1.4 | Watchdog + heartbeat client (RFC-007 M7 side) | 0.5 pw | RFC-007 v1.0 (post 5/13 evaluation) |
| 2.1.5 | M7 unit tests (SIL-grade coverage ≥95%) | 1.0 pw | D1.6 scenario schema |

**Total M7-core**: 6.0 pw

### §2.2 Acceptance Criteria

- [ ] M7 arbitration: MRM triggered within 500ms of SIF activation (IEC 61508 timing requirement)
- [ ] M7 FMEDA PFH budget: < 2×10⁻⁷/hr (SIL 2 allocation; combined M7+M1 < 5×10⁻⁷/hr)
- [ ] RFC-007 heartbeat: M7 liveness signal to X-axis Deterministic Checker operational
- [ ] Unit test coverage: **≥90% on SIF-critical paths**; ≥80% overall *(v2.1 PM-hat confirmed: Option A AC降标，维持 6.0pw；IEC 61508 SIL 2 最低要求 ≥90% 路径覆盖)*
- [ ] FMEDA failure modes: **≥15 modes** with PFH contribution *(v2.1 AC降标，从 ≥20 降至 ≥15；完整 ≥20 版延至 D4.7 B4 触发后)*
- [ ] Doer-Checker independence: M7 code has ZERO shared modules with M1–M6 (enforced by `path_s_dry_run` CI job)

---

## §3 M7-sotif (3 person-weeks, 7/13–8/31)

**Delivery**: Part of D3.7 and D3.9 closure

### §3.1 Deliverables

| # | Deliverable | Effort | Dependency |
|---|---|---|---|
| 3.1.1 | ISO 21448 SOTIF mapping: 6 SOTIF categories → M7 monitoring hooks | 1.0 pw | D2.7 HARA; ISO 21448:2022 |
| 3.1.2 | SOTIF triggering condition catalog (M2 perception degradation → M7 response) | 1.0 pw | D1.6 scenario schema; M2 track confidence model |
| 3.1.3 | RFC-007 integration: M7 SOTIF hooks into heartbeat + X-axis liveness token | 0.5 pw | RFC-007 v1.0 |
| 3.1.4 | D2.7 HARA feedback loop: SOTIF findings → HARA residual risk closure | 0.5 pw | D2.7 HARA (concurrent) |

**Total M7-sotif**: 3.0 pw

### §3.2 Acceptance Criteria

- [ ] ≥6 SOTIF categories documented with M7 monitoring response
- [ ] SOTIF triggering condition catalog: ≥15 conditions covering M2 perception failure modes
- [ ] RFC-007 liveness token includes SOTIF health flag (field defined in RFC-007 HeartbeatMsg)
- [ ] HARA residual risk: M7-sotif findings integrated into HARA by D2.7 deadline (7/31)

---

## §4 Timeline (Candidate 8-Week Window)

| Week | M7-core focus | M7-sotif focus |
|---|---|---|
| W1 (7/13) | Arbitration logic skeleton | SOTIF mapping §3.1.1 |
| W2 (7/20) | IEC 61508 baseline + SIF impl | Triggering condition catalog §3.1.2 |
| W3 (7/27) | FMEDA table draft | HARA feedback loop §3.1.4 |
| W4 (8/03) | Watchdog + heartbeat client §2.1.4 | RFC-007 SOTIF integration §3.1.3 |
| W5 (8/10) | Unit tests §2.1.5 | Catalog review + HARA closure |
| W6 (8/17) | SIL coverage verification | SOTIF catalog final |
| W7 (8/24) | Integration: M7-core + M7-sotif combined tests | — |
| W8 (8/31) | DEMO-3 readiness; D3.7 closure review | D3.7 closure review |

---

## §4a 上游依赖里程碑（M7-hat sign-off 条件 — 补入）

以下里程碑是 §4 时间线的硬前置依赖。任意一条延期 1 周则对应 M7 交付物自动顺延，PM 须立即触发 §0.3 闭口备选路径。

| 里程碑日期 | 里程碑内容 | 影响的 M7 交付物 |
|---|---|---|
| **5/13** | RFC-007 v1.0 接口稳定确认（跨团队评审通过） | §2.1.4 heartbeat client + §3.1.3 SOTIF 集成 |
| **7/10** | D2.7 HARA 交付（外包安全工程师）| §2.1.1 仲裁逻辑 SIF 输入 + §2.1.3 FMEDA |
| **7/10** | M2 track confidence 字段 IDL 冻结（D2.1 接口定义子任务）| §3.1.2 SOTIF 触发条件目录 |
| **7/17** | D2.5 SIL 工具链 M7 接入测试通过（V&V 工程师）| §2.1.2 IEC 61508 SIL 2 基线 |

若 RFC-007 评审（5/13）触发字段变动超 20%，§2.1.4 工时从 0.5pw 升至 1.0pw，从 W1 buffer（7/13–7/19）吸收。

---

## §5 v3.0 Plan Annotation

v3.0 plan §0.1 line 47 `+3.0 pw` (previously unlabeled in v2.0) is now explicitly attributed:
- **+3.0 pw = M7-sotif (3 pw) + reclassification of M7-core from 6→6 pw** (no change to M7-core; the +3 is net new SOTIF work)

v2.1 acknowledge (2026-05-12): this document is the formal record of the split. Plan mathematics remain unchanged (87.0 pw total work / 84.0 pw capacity / -3.0 gap in §0.3 closure path).

---

## §6 M7-hat Sign-off

## M7-hat Sign-off — Effort Split v2.1

**审查日期**：2026-05-08
**总体判断**：有条件接受

**时间线一致性问题**：

§4 表格 W1 = 7/13 开始"Arbitration logic skeleton"（M7-core 列），但 §2.1 文字写"7/20–8/31"。裁定：将 §2.1 开窗时间修正为"7/13–8/31"（7 周窗口），对应工作量维持 6.0pw 不变——额外 1 周是 buffer，不新增可交付物。此修正须在 5/12 前并入文件。

**M7-core 工作量评估**：
- 2.1.1 仲裁逻辑（2.0pw）：合理，但有条件 — 前提是 D1.5 V&V Plan 在 6/15 前明确定义仲裁逻辑的形式规约（pre/post-conditions），否则升至 2.5pw。🟡 Medium
- 2.1.2 IEC 61508 SIL 2 基线（1.5pw）：偏低 — D2.5 工具链必须在 7/17 前完成 M7 接入测试，否则 §2.1.2 延期 1 周。依赖准时则 1.5pw 可接受。🟡 Medium
- 2.1.3 FMEDA（1.0pw）：**偏低，建议调整为 1.5pw** — 20+ 失效模式 SIL 2 FMEDA 含 failure rate 数据源 + 诊断措施 + PFH 贡献计算，5 天的密度过高。替代方案：维持 1.0pw 但将失效模式下限从 ≥20 降至 ≥15。🔴 Low（当前估算置信度）
- 2.1.4 heartbeat 客户端（0.5pw）：合理，有前提 — RFC-007 评审后接口若变动超 20% 字段则升至 1.0pw。🟡 Medium
- 2.1.5 SIL-grade 测试（1.0pw）：**偏低，建议调整为 1.5pw** — 路径覆盖 ≥95% + 零共享约束意味着测试框架也必须独立，5 天无任何 buffer。替代方案：维持 1.0pw 但将覆盖率从 ≥95% 降至 ≥90%（仍符合 IEC 61508 SIL 2 最低要求）。🟡 Medium

**M7-sotif 工作量评估**：
- 3.1.1 ISO 21448 映射（1.0pw）：合理 — 分析工作 + 接口定义，有 HARA 输入则 1.0pw 可行。🟢 High
- 3.1.2 触发条件目录（1.0pw）：有条件接受 — M2 track confidence IDL 必须在 7/10 前冻结，否则 §3.1.2 延期 1–2 周。若 IDL 已锁，1.0pw 合理。🟡 Medium
- 3.1.3 RFC-007 SOTIF 集成（0.5pw）：合理 — 与 §2.1.4 同依赖。🟢 High
- 3.1.4 HARA 反馈闭环（0.5pw）：合理，注意并发协调 — 确认外包安全工程师合同涵盖 7/10 后 HARA 修订服务。🟡 Medium

**时间线风险**：

最大风险是上游依赖链条在 7/10–7/20 的 10 天内集中到位：D2.7 HARA（7/10）、M2 IDL 冻结（7/10）、D2.5 SIL 工具链接入（7/17）、RFC-007 稳定（5/13）。任意一个延期 1 周，直接压缩末端串行的 FMEDA + SIL-grade 测试窗口，冲击 DEMO-3（8/31）。**最可能滑期的交付物**：§2.1.3 FMEDA 和 §2.1.5 SIL-grade 测试。

**修订建议**：

1. §2.1 开窗时间修正为"7/13–8/31"（7 周含 1 周 buffer），5/12 前并入；
2. 在 §4 之后补充依赖里程碑表（§4a）：7/10 HARA 交付 / 7/10 M2 IDL 冻结 / 7/17 D2.5 工具链 M7 接入通过 / 5/13 RFC-007 接口稳定确认；
3. 在 FMEDA/SIL 测试工时调整与 Acceptance Criteria 降标之间二选一：
   - 选项 A（推荐）：FMEDA ≥15 modes + 测试覆盖 ≥90%，维持总量 9.0pw；
   - 选项 B：FMEDA ≥20 modes + 测试覆盖 ≥95%，总量升至 10.0pw，v3.0 缺口扩大至 -4.0pw，在 §0.3 注记；
4. PM-hat 在 5/12 PM 前确认选项后本 sign-off 方为完成。

**B4 Contingency 触发判断**：

当前不触发 B4。触发条件：若 D2.7 HARA 延期至 7/17 后，或 D2.5 SIL 工具链在 7/24 前未完成 M7 接入，则 §2.1.3 FMEDA 无法在 8/31 前达到 SIL 2 标准——触发 B4-minor：将 FMEDA 完整版推至 D4.7，8/31 交付"M7 FMEDA skeleton（≥10 modes，PFH budget confirmed）"作为 DEMO-3 最低 gate。

**签字：M7-hat ✅ 2026-05-08（全部条件已关闭）**

条件关闭记录：
- ✅ (a) §2.1 开窗时间已修正为 "7/13–8/31"
- ✅ (b) §4a 依赖里程碑表已补入（4 个上游 gate + RFC-007 弹性条款）
- ✅ (c) PM-hat 确认 **Option A（AC降标）**：FMEDA ≥15 modes + 测试覆盖 ≥90%，总量维持 9.0pw

---

*v2.1 — 2026-05-08 — MUST-11 product · References: user decision §13.2 (2026-05-07)*
