# M1 · Progress · D 任务联动

| 维度 | 说明 |
|------|------|
| 数据更新规则 | PR 合并涉及 M1 时同步更新本表 |
| 最近更新 | 2026-06-08 |
| **Currently Implementing** | GAP 修复：schema_version / M7 心跳状态转换 / Capability Manifest 订阅 |

---

## 联动表

| D 任务 | 关系 | 状态 | 详情 |
|--------|------|------|------|
| D0.1 | Closed in | ✅ | MUST-6（sog 校验 → 与 M2 协同）+ MUST-7（active_role stub → 仅 M8）。M1 本体未触及 |
| D1.3.2-integration | Closed in | ✅ 2026-05-20 | SIL L3 pipeline integration：ODD state 真发布到 /sil/odd_state（l3_pipeline.launch.py 落地）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则（LineThreshold=40, 禁 malloc, 禁全局变量）+ 50 修复模式 + clang-tidy/cppcheck/CI 集成 |
| D2.1 | Closed in | ✅ 2026-05-21 | ODD/Envelope Manager 决策级实装：zone/health-aware FSM + EMA 平滑 + ToR 自适应矩阵 + Capability Manifest ROT_max + M7 VETO + FMEDA v0.1（11 失效模式）；PR merge 后同步关闭 |
| D2.7 | Currently Implementing（计划）| 🔴 未启 | FMEDA M1 表 ≥ 20 失效模式；Owner 安全工程师外包 |
| D3.5 | Blocks | ⏳ | M1 ODD 4 子域热加载参数 132 [TBD-HAZID] 回填依赖本模块完整化（HAZID 8/19）|

---

## GAP 追踪（来自 M1-spec-v2.md §8）

| GAP | 优先级 | 状态 | 目标版本 |
|-----|--------|------|----------|
| GAP-1 Capability Manifest 话题订阅 | P1 🟡 | 📋 待开发（依赖 Manifest 系统） | v2.1 |
| GAP-2 schema_version 全链路 = 0 | P0 🔴 | 📋 待修复 | v2.0.1 |
| GAP-3 FMEDA 表 ≥20 失效模式 | P0 🔴 | 🔴 阻塞（D2.7 外包未启） | v2.1 |
| GAP-4 OUT→MRC 未经 M7 路径 | P2 🟡 | 📋 待开发（依赖 M7 对接）| v2.2 |
| GAP-5 M7 心跳丢失→自动 DEGRADED | P1 🟡 | 📋 待修复 | v2.0.1 |
| GAP-6 文档外话题补齐 | P2 🟡 | 📋 待文档更新 | v2.0.1 |

---

## DEMO-2 (7/31) 阻塞贡献

- 🟡 中阻塞：M1 ODD-A → ODD-D 切换 live 是 DEMO-2 核心场景之一，FMEDA + ToR 矩阵未到位会影响 CCS 中期意见会议
- 🟢 低阻塞：M1 FSM + Conformance_Score + TMR/TDL 已通过 104 测试确认工作正常

---

## 参考 D 任务文档

- D1.3.2-integration: [Phase 1/D1.3-sil-framework/D1.3.2-integration/](../../Phase%201/D1.3-sil-framework/D1.3.2-integration/)
- D2.1: [Phase 2/D2.1-m1-odd-hardening/](../../Phase%202/D2.1-m1-odd-hardening/)
- D2.7: [Phase 2/D2.7-hara-fmeda-m1/](../../Phase%202/D2.7-hara-fmeda-m1/)
- M1-spec.md: [TDL-Kernel/M1-ODD-Envelope-Manager/M1-spec.md](M1-spec.md)
- M1-gap-fix-plan.md: [TDL-Kernel/M1-ODD-Envelope-Manager/M1-gap-fix-plan.md](M1-gap-fix-plan.md)

---

## 修订记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-05-22 | 初版 |
| v1.1 | 2026-06-08 | 添加 GAP 追踪表 + 参考链接至 M1-spec-v2.md |
