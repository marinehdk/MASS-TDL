# Phase 1 · 工程基础 + SIL 框架 + V&V 基线 · Overview

| 属性 | 值 |
|---|---|
| 时间 | 2026-05-13 → 2026-06-15（4.5 周；D1.3.2.2 / D1.3.2.3 / D1.3.3 跨 Phase 1/2 边界至 7/15）|
| 估计人周 | ~28.0（v3.0 18.0 + v3.1 D1.3 拆分 + D1.3.3 FMI bridge 起步段）|
| 阶段目标 | 在写任何业务模块代码前，建立"测试基础设施 + 质量门 + V&V 范式 + 认证证据骨架 + SIL 框架架构（选项 D 混合 + DNV 工具链 3 MUST + RL 隔离三层）"|
| **里程碑** | 🎬 **DEMO-1 Skeleton Live（2026-06-15）** |
| 进度日期 | 2026-05-20（D1.6 ✅ 已关闭）|
| 当前阶段状态 | ⏳ 中段（D1.5 ✅；D1.1/D1.2/D1.3.1/D1.3.2.1/D1.3.2.3/D1.3.3 部分；D1.3.1.2/D1.3.1/D1.4/D1.6/D1.7/D1.8 待补）|

---

## D 任务清单 + 状态

| D 编号 | 主题 | 目录 | 状态 |
|---|---|---|---|
| D1.1 | ROS2 工作区 + IDL 消息 | [D1.1-ros2-workspace/](D1.1-ros2-workspace/) | 🟡 部分（25 .msg 在，schema_version 16/25；mock publisher 在 archive）|
| D1.2 | CI/CD 流水线 + PATH-S 独立性检查 | [D1.2-cicd-pipeline/](D1.2-cicd-pipeline/) | 🟡 部分（9 阶段流水线 + .clang-tidy.path-s ✓；独立性检查脚本缺）|
| **D1.3** | **SIL 框架（父级）** | [D1.3-sil-framework/](D1.3-sil-framework/) | 见子任务 |
| ├─ D1.3.1 | 4-DOF MMG 仿真器 + AIS 历史数据 | [D1.3.1-mmg-simulator/](D1.3-sil-framework/D1.3.1-mmg-simulator/) | 🟡 部分（MMG RK4 真实；AIS 回放 / `ShipMotionSimulator` 抽象缺）|
| ├─ D1.3.2 | Scenario HMI 工具链（父级）| [D1.3.2-scenario-hmi/](D1.3-sil-framework/D1.3.2-scenario-hmi/) | 见子子任务 |
| │ ├─ D1.3.2.1 | YAML scenario + Imazu-22 + Cerberus | [.../D1.3.2.1-yaml-imazu/](D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.1-yaml-imazu/) | 🟡 部分（22 Imazu + frozen hash ✓；双语言验证缺）|
| │ ├─ D1.3.2.2 | AIS-driven scenario authoring | [.../D1.3.2.2-ais-authoring/](D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.2-ais-authoring/) | 🔴 未启（建议推迟到 Phase 4）|
| │ └─ D1.3.2.3 | Web HMI + ENC + foxglove_bridge | [.../D1.3.2.3-web-hmi/](D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/) | 🟡 部分（MapLibre + foxglove + ToR ✓；S-57 MVT + SAT-2/3 handler 缺）|
| └─ D1.3.3 | FMI bridge / libcosim / dds-fmu | [D1.3.3-fmi-bridge/](D1.3-sil-framework/D1.3.3-fmi-bridge/) | 🟡 部分（Humble 容器 ✓；dds-fmu 未集成）|
| D1.3.1' | Simulator Qualification Report | [D1.3.1-mmg-simulator/](D1.3-sil-framework/D1.3.1-mmg-simulator/01-simulator-qualification-report.md) | ✅ 完成（3 ref sol ≤30% + 20x replay σ=7.28e-12 + 7 sweep stable + TCL-3 PASS）|
| D1.4 | 编码规范 + 静态分析工具链 | （未建目录）| 🔴 未启动 |
| D1.5 | V&V Plan v0.1 + Simulator Qualification | [D1.5-vv-plan-scenario-qual/](D1.5-vv-plan-scenario-qual/) | ✅ V&V Plan 完整（含 SIL latency budget + RL rebound + DNV toolchain entry）|
| D1.6 | 场景 schema + farn + 三层 CI 集 | [D1.6-scenario-schema/](D1.6-scenario-schema/) | 🟡 部分（schema doc 3277字 ✓ / traceability CSV 32行 ✓ / farn ospx dry-run ✓ / CI Smoke 10 ✓ / CCS 邮件模板 ✓；22 Imazu schema 验证待 D1.3.2.1 Task C）|
| D1.7 | 覆盖率方法论 + 6 维度评分 | （未建目录）| 🔴 stub 空壳（`docs/Design/SIL/03-coverage-metrics.md`）|
| D1.8 | cert tracking + ConOps stub | （未建目录）| ✅ 双 stub 完整（`docs/Design/Cert/cert-evidence-tracking.md` + `docs/Design/ConOps/01-conops-v0.1.md`）|

> **编号映射**（v3.2 重命名前 → 后）：D1.3a → D1.3.1 / D1.3b → D1.3.2 / D1.3c → D1.3.3 / D1.3b.1 → D1.3.2.1 / D1.3b.2 → D1.3.2.2 / D1.3b.3 → D1.3.2.3。git 分支名（如 `feat/d1.3b.3-gap-015-sil-fixes`）保留原名不改。

---

## DEMO-1 Charter（2026-06-15）

**Scenario**（端到端约 5 min）：
1. AIS 历史数据回放（NOAA 1h 片段）→ Mock M2 → 仿真器 own-ship 运动
2. 1 个 Crossing 场景脚本 live → SIL HMI 显示 own-ship 接近目标 → ODD 评估（Mock M1）→ SAT-1/2/3 视图（wireframe 级，SAT-1 状态文字真实）
3. PR-trigger Smoke 10 跑通 + 覆盖率立方体热图（10 cell 亮）
4. ConOps PDF + V&V Plan PDF + cert-evidence-tracking.md 现场展示
5. 仿真器参考解（旋回）实测 + tool confidence 报告

**Audience × View**：业主（项目可视化）/ PM（进度）/ CCS 早期试映（V&V Plan + ConOps 可接受性）/ 资深船长（SAT-1 直觉反馈）/ HAZID 干系人（schema + traceability）

**Visible Success**：D1.1–D1.8 全部 DoD 通过 / CCS 邮件回执 ≥ 1 份 / 资深船长反馈 ≥ 3 条 / 业主签字资源到位

详见 [00-master-plan.md](../00-master-plan.md) §DEMO-1 + v3.2 archived plan §Phase 1。

---

## 关键风险（2026-05-20 实测）

| # | 风险 | 等级 |
|---|---|---|
| R1.A | 3 个 D 任务（D1.4 / D1.7）全 stub + D1.3.1' Sim Qualification 待跑 3 参考解，DEMO-1 验收风险 | 🔴 高（原 4 个，D1.6 ✅ 已关闭）|
| R1.B | D1.3.2.2 AIS-driven 未启动，建议推迟到 Phase 4（DEMO-1/2 改用 D1.3.2.1 内置 22 Imazu）| 🟡 已决策 |
| R1.C | Web HMI SAT-2/SAT-3 双端真空（消费端组件壳已 mount，但生产端 M8 没发对应 topic + WS handler 缺）| 🔴 DEMO-2 P0 阻塞 |
| R1.D | HF 咨询 6/16 onboard，DEMO-1 不依赖；DEMO-2 (7/31) 阶段才上线 | 🟢 |

详见 [00-master-plan.md](../00-master-plan.md) §0.5 Phase 1 进度快照 + DEMO-2 GAP。

---

## 关联

- **总账**：[00-master-plan.md](../00-master-plan.md)
- **架构权威**：[Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) v1.1.3-pre-stub
- **SIL 设计套件**：[SIL/v1.0-unified/](../SIL/v1.0-unified/) 4 文档（01 架构 / 02 后端 / 03 前端 / 04 场景联调）
- **TDL Kernel 模块视角**：[TDL-Kernel/](../TDL-Kernel/) M1–M8 模块设计 + 进度联动

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版（v3.2 计划 §0.5 + §3 提炼 + 编号重排映射）|
