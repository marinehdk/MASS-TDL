# MASS L3 TDL Architecture

本文件是 Agent 快速入口。权威细节仍在 [MASS_ADAS_L3_TDL_架构设计报告.md](docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)。不要把本文件当作替代设计规格；改架构时先改权威文档，再同步本索引。

## 1. 系统边界

本仓库实现 MASS ADAS 五层栈中的 **L3 Tactical Decision Layer**，时间尺度为秒到分钟。

```text
L1 Mission[hrs~days] -> L2 Voyage[min~hrs] -> L3 Tactical -> L4 Guidance[100ms~1s] -> L5 Control[10ms~100ms]
                                           ^
                                           本仓库
```

L3 消费 L1/L2 任务与航路、Fusion/NavFilter 态势、系统级 X-axis Checker / Y-axis Reflex / Hardware Override 通知；向 L4 输出避让路径或紧急覆盖命令；向 ROC/船长输出 SAT-1/2/3 透明性信息。

## 2. 必读入口

| 目的 | 先读 | 再读 |
|---|---|---|
| 全局架构/ADR | [架构报告 §1-4](docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) | [TDL Kernel overview](docs/Design/TDL-Kernel/00-tdl-kernel-overview.md) |
| 模块设计 | 架构报告 §5-12 | `docs/Design/TDL-Kernel/M{n}-*/M{n}-spec.md` |
| 当前实现状态 | [TDL Kernel overview](docs/Design/TDL-Kernel/00-tdl-kernel-overview.md) | `M{n}-progress.md` |
| 接口/消息 | 架构报告 §15 | [l3_msgs](src/l3_tdl_kernel/l3_msgs/msg) / [l3_external_msgs](src/l3_tdl_kernel/l3_external_msgs/msg) |
| SIL / HMI | [SIL unified](docs/Design/SIL/v1.0-unified/00-README.md) | [web](web/src) / [sil_orchestrator](src/sil_orchestrator) |
| 计划与阶段 | [00-master-plan.md](docs/Design/00-master-plan.md) | `docs/Design/Phase N/00-overview.md` |

## 3. 不可破坏 ADR

1. **ODD 是组织原则**：M1 ODD 状态是 L3 行为切换唯一权威。算法模块不得各自维护“是否安全”判断。
2. **8 模块分解**：M1 包络控制；M2-M6 决策规划；M7-M8 安全与接口。
3. **SAT-1/2/3 透明性**：每模块提供当前状态、理由、预测与不确定性，由 M8 聚合给 ROC/船长。
4. **Doer-Checker 双轨**：M1-M6 是 Doer；M7 是独立 Checker；M7 不动态生成轨迹，只触发预定义 MRM。
5. **Backseat Driver**：决策核心不写船型常量；船型差异从 Capability Manifest / PVA / 水动力插件进入。

## 4. 模块地图

| 模块 | 职责 | 设计章节 | 模块文档 | 代码入口 | 关键消息 |
|---|---|---|---|---|---|
| M1 ODD / Envelope Manager | L3 安全语境、ODD 状态机、TMR/TDL、MRC 触发 | 架构报告 §5 | [M1-spec](docs/Design/TDL-Kernel/M1-ODD-Envelope-Manager/M1-spec.md) / [M1-progress](docs/Design/TDL-Kernel/M1-ODD-Envelope-Manager/M1-progress.md) | [m1_odd_envelope_manager](src/l3_tdl_kernel/m1_odd_envelope_manager) | `ODDState`, `ModeCmd`, `SafetyAlert` |
| M2 World Model | 静态/动态/自身三视图，CPA/TCPA，COLREG 几何预分类 | 架构报告 §6 | [M2-spec](docs/Design/TDL-Kernel/M2-World-Model/M2-spec.md) / [M2-progress](docs/Design/TDL-Kernel/M2-World-Model/M2-progress.md) | [m2_world_model](src/l3_tdl_kernel/m2_world_model) | `WorldState`, `TrackedTarget`, `OwnShipState` |
| M3 Mission Manager | L1 任务令本地跟踪、ETA、L2 重规划请求 | 架构报告 §7 | [M3-spec](docs/Design/TDL-Kernel/M3-Mission-Manager/M3-spec.md) / [M3-progress](docs/Design/TDL-Kernel/M3-Mission-Manager/M3-progress.md) | [m3_mission_manager](src/l3_tdl_kernel/m3_mission_manager) | `VoyageTask`, `MissionGoal`, `RouteReplanRequest` |
| M4 Behavior Arbiter | ODD-aware 行为字典，IvP 多目标仲裁 | 架构报告 §8 | [M4-spec](docs/Design/TDL-Kernel/M4-Behavior-Arbiter/M4-spec.md) / [M4-progress](docs/Design/TDL-Kernel/M4-Behavior-Arbiter/M4-progress.md) | [m4_behavior_arbiter](src/l3_tdl_kernel/m4_behavior_arbiter) | `BehaviorPlan`, `COLREGsConstraint`, `SafetyConcernEvent` |
| M5 Tactical Planner | Mid-MPC + BC-MPC，避让路径与紧急覆盖 | 架构报告 §10 | [M5-spec](docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-spec.md) / [M5-progress](docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-progress.md) | [m5_tactical_planner](src/l3_tdl_kernel/m5_tactical_planner) | `AvoidancePlan`, `ReactiveOverrideCmd` |
| M6 COLREGs Reasoner | Rule 5-19 分层推理，ODD-aware 参数 | 架构报告 §9 | [M6-spec](docs/Design/TDL-Kernel/M6-COLREGs-Reasoner/M6-spec.md) / [M6-progress](docs/Design/TDL-Kernel/M6-COLREGs-Reasoner/M6-progress.md) | [m6_colregs_reasoner](src/l3_tdl_kernel/m6_colregs_reasoner) | `COLREGsConstraint`, `RuleAssessment` |
| M7 Safety Supervisor | IEC 61508 + SOTIF 双轨监控，MRM 索引，X-axis veto 统计 | 架构报告 §11 | [M7-spec](docs/Design/TDL-Kernel/M7-Safety-Supervisor/M7-spec.md) / [M7-progress](docs/Design/TDL-Kernel/M7-Safety-Supervisor/M7-progress.md) | [m7_safety_supervisor](src/l3_tdl_kernel/m7_safety_supervisor) | `SafetyAlert`, `SafetyConcernEvent`, `CheckerVetoNotification` |
| M8 HMI / Transparency Bridge | 唯一 ROC/船长接口，SAT 聚合，ToR 协议，ASDR | 架构报告 §12 | [M8-spec](docs/Design/TDL-Kernel/M8-HMI-Transparency-Bridge/M8-spec.md) / [M8-progress](docs/Design/TDL-Kernel/M8-HMI-Transparency-Bridge/M8-progress.md) | [m8_hmi_transparency_bridge](src/l3_tdl_kernel/m8_hmi_transparency_bridge) | `SATData`, `UIState`, `ToRRequest`, `ASDRRecord` |

## 5. 运行时数据流

```text
L1 VoyageTask + L2 PlannedRoute
        -> M3 Mission Manager
Fusion targets/nav/env + ENC
        -> M2 World Model
M1 ODD state
        -> M2/M3/M4/M5/M6/M7/M8
M2 WorldState + M3 MissionGoal + M6 COLREGsConstraint
        -> M4 Behavior Arbiter
M4 BehaviorPlan + M6 constraints + M2 WorldState
        -> M5 Tactical Planner
M5 AvoidancePlan / ReactiveOverrideCmd
        -> L4 Guidance
M7 SafetyAlert
        -> M1/M8
M1/M2/M4/M6/M7 SATData
        -> M8 HMI / ROC
All key decisions
        -> ASDR
```

核心接口闭包看架构报告 §15；实际 ROS2 IDL 看 [l3_msgs/msg](src/l3_tdl_kernel/l3_msgs/msg) 与 [l3_external_msgs/msg](src/l3_tdl_kernel/l3_external_msgs/msg)。

## 6. 代码查找规则

- 模块主节点：`src/l3_tdl_kernel/m{n}_*/src/*_node.cpp`。
- 模块参数：`src/l3_tdl_kernel/m{n}_*/config/*.yaml`。
- 模块启动：`src/l3_tdl_kernel/m{n}_*/launch/*.launch.py`；全链路入口：[l3_pipeline.launch.py](src/l3_tdl_kernel/launch/l3_pipeline.launch.py)。
- 模块测试：`src/l3_tdl_kernel/m{n}_*/test/`。
- 内部消息：`src/l3_tdl_kernel/l3_msgs/msg/`。
- 外部边界消息：`src/l3_tdl_kernel/l3_external_msgs/msg/`。
- SIL 仿真节点：`src/sim_workbench/`。
- SIL 编排 API：`src/sil_orchestrator/`。
- Web HMI：`web/src/`，地图组件在 `web/src/map/`，仿真屏在 `web/src/screens/`。
- A4000 / Docker / runtime glue：`docker/`, `scripts/`, `docker-compose.yml`, `ecosystem.config.cjs`。

## 7. 常见任务定位

| 任务 | 查找路径 |
|---|---|
| ODD / TMR / MRC | 架构报告 §3, §5, §15；`m1_odd_envelope_manager/src/` |
| CPA/TCPA / encounter 分类 | 架构报告 §6；`m2_world_model/src/cpa_tcpa_calculator.cpp`, `encounter_classifier.cpp` |
| 航次任务 / L2 重规划 | 架构报告 §7, §15；`m3_mission_manager/src/` |
| IvP 行为仲裁 | 架构报告 §8；`m4_behavior_arbiter/src/` |
| MPC / 避让路径 | 架构报告 §10；`m5_tactical_planner/src/mid_mpc/`, `src/bc_mpc/` |
| COLREGs Rule 5-19 | 架构报告 §9；`m6_colregs_reasoner/src/rules/` |
| Doer-Checker / SOTIF / MRM | 架构报告 §11；`m7_safety_supervisor/src/` |
| SAT / ToR / ASDR | 架构报告 §12, §15；`m8_hmi_transparency_bridge/src/` |
| HMI 画面与回放 | `web/src/screens/`, `web/src/map/`, `web/src/store/` |
| SIL scenario / orchestration | `src/sil_orchestrator/`, `src/sim_workbench/scenario_authoring/`, `scenarios/` |

## 8. Agent 工作约束

- 不确定设计意图时，先读对应架构报告章节和 `M{n}-spec.md`，再读代码。
- 不要修改 `docs/Design/Archive/`、`docs/Init From Zulip/`、`docs/Init From SINAN/`；这些是历史或外部参考。
- 新断言必须能指向架构章节、D-task 文档、代码、测试或 NLM 来源。
- 改接口必须同步：架构报告 §15、`l3_msgs` / `l3_external_msgs`、相关模块 spec/progress、测试。
- 若 codegraph MCP 可用，先用 codegraph 定位 symbol；若不可用，说明并用 `rg` 兜底。
- 若需要历史语义召回，优先用 `/Users/marine/.local/bin/mempalace wake-up` 或 `/Users/marine/.local/bin/mempalace search "<kw>"`。
