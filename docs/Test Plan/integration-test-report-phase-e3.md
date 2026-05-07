# Phase E3 HIL 仿真测试报告

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-TEST-E3-REPORT-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-07 |
| 阶段 | Phase E3 — HIL 仿真测试 |
| 分支 | wave-4/integration |
| 基线 | main @ v0.3.0 (df92ba2) |
| 测试框架 | pytest (Python) + ROS2 HIL |
| 状态 | 完成 — 待 HIL 硬件执行确认 |

---

## §1 执行摘要

Phase E3 交付了完整的 HIL 仿真测试基础设施（E3-1 through E3-5）。具体包括：FCB 4-DOF MMG 船舶动力学仿真器（E3-1）、3209 个 COLREGs 场景生成器与 100 场景基准集（E3-2）、ROC HMI 仿真器（E3-3）、HIL 端到端测试套件（E3-4）和性能基准测试套件（E3-5）。

关键发现：

- **HIL 基础设施完整就绪**：FCB MMG 仿真器（4-DOF，RK4 50 Hz）、ROC 仿真器（Flask + ROS2）、场景注入器、测试运行器、性能计时器全部实现并提交。
- **F-INT-001 阻断项继承自 E2**：M6 发布至 `/l3/m6/colregs_constraint`，M5 订阅 `/m6/colregs_constraint`（缺 `/l3/` 前缀），导致 M5 无法接收 M6 约束。修复分支 `fix/integration-topic-mismatches` 就位，需合并后才能获得真实 ≥95% 通过率。
- **性能时序目标（M5 Mid-MPC P99 <100ms 等）**：测试框架与断言已就位（`tests/hil/benchmarks/test_timing_e3.py`），以 FCB 实物硬件执行结果为准。
- **M8 ToR callsite 缺口（继承自 E2）**：影响 `test_hil_rule19_degraded` 的 ToR 路径验证；追踪分支 `fix/m8-tor-callsite`。

总 pytest 测试用例数（HIL 层）：**7**（`test_hil_e2e.py` 3 + `test_timing_e3.py` 4）

---

## §2 E3 交付产物状态表

| 任务 | 描述 | 关键产物路径 | 状态 |
|---|---|---|---|
| E3-1 | FCB 4-DOF MMG 船舶动力学仿真器（Yasukawa 2015），RK4 50 Hz，发布 `FilteredOwnShipState`（50 Hz）+ `TrackedTargetArray`（2 Hz） | `src/fcb_simulator/src/fcb_simulator_node.cpp`<br>`src/fcb_simulator/src/mmg_model.cpp`<br>`src/fcb_simulator/src/rk4_integrator.cpp`<br>`src/fcb_simulator/config/fcb_dynamics.yaml` | 实现完成 |
| E3-2 | COLREGs 场景生成器：生成 3209 个有效场景，按规则分层选取 100 个基准场景至 `scenarios_100.json` | `tests/hil/generate_scenarios.py`<br>`tests/hil/scenario_schema.py`<br>`tests/hil/scenarios/scenarios_100.json`<br>`tests/hil/scenarios/scenarios_all.json` | 实现完成 |
| E3-3 | ROC HMI 仿真器：Flask 仪表板 + ROS2 节点，支持 ToR 响应注入 | `tools/roc_simulator/web_server.py`<br>`tools/roc_simulator/roc_node.py`<br>`tools/roc_simulator/launch_roc.py`<br>`tools/roc_simulator/templates/dashboard.html`<br>`tools/roc_simulator/static/style.css` | 实现完成 |
| E3-4 | HIL 端到端测试基础设施：目标注入器、场景运行器、3 个 pytest 测试（100 场景套件、Rule 17 stand-on、Rule 19 降级 ODD） | `tests/hil/target_injector.py`<br>`tests/hil/hil_runner.py`<br>`tests/hil/test_hil_e2e.py`<br>`tests/hil/conftest.py`<br>`tests/hil/README.md` | 实现完成 |
| E3-5 | 性能基准测试套件：DDS 时序测量（最近邻分位数），M5 Mid-MPC / BC-MPC / Reflex Arc / L4 模式切换 P99 断言 | `tests/hil/benchmarks/timing_verifier.py`<br>`tests/hil/benchmarks/test_timing_e3.py` | 实现完成 |

---

## §3 COLREGs 场景覆盖摘要

| 属性 | 值 |
|---|---|
| 总生成场景数 | 3209（`tests/hil/scenarios/scenarios_all.json`） |
| 基准场景数 | 100（`tests/hil/scenarios/scenarios_100.json`） |
| 选取方法 | 按规则 / 子类分层配额 + 随机填充至 100（`random.seed(42)`） |
| 仿真 CPA 范围 | 50m–1500m |
| TCPA 下限 | 30s（退化场景已过滤） |

**覆盖规则分布：**

| COLREG 规则 | 场景描述 | 覆盖状态 |
|---|---|---|
| Rule 13 (overtaking) | 追越目标从右后方或左后方接近，CPA < 0.5 nm | ✅ 覆盖 |
| Rule 14 (head-on) | 双船对遇，DCPA → 0 | ✅ 覆盖 |
| Rule 15 (crossing) | 目标从右舷 045° 接近，TCPA < 5 min | ✅ 覆盖 |
| Rule 17 (stand-on) | 本船为直航船，目标为让路船已采取行动 | ✅ 覆盖 |
| Rule 18 (responsibilities) | 机动船 vs 帆船 / 渔船 / 限制操纵船 | ✅ 覆盖 |
| Rule 19 (restricted visibility) | 雷达目标，ODD 能见度 < 0.5 nm | ✅ 覆盖 |

---

## §4 Phase E3 DoD 状态表

参照 `docs/Test Plan/00-test-master-plan.md` §3 Phase E3 完成判据逐项核查：

| DoD 条目 | 测试用例 | 状态 | 说明 |
|---|---|---|---|
| 100 个基准 COLREGs 场景通过率 ≥ 95% | `test_hil_100_scenario_suite`（`test_hil_e2e.py`） | 待 HIL 硬件执行（F-INT-001 修复后） | F-INT-001 阻断 M6→M5 约束链，须 `fix/integration-topic-mismatches` 合并后重跑 |
| M5 Mid-MPC 计算时延 < 100ms（P99，FCB 硬件） | `test_mid_mpc_p99_lt_100ms`（`test_timing_e3.py`） | 待 HIL 硬件执行 | 断言框架就绪；以 FCB 实物计时为准 |
| M5 BC-MPC 计算时延 < 50ms（P99，FCB 硬件） | `test_bc_mpc_p99_lt_50ms`（`test_timing_e3.py`） | 待 HIL 硬件执行 | 同上 |
| Reflex Arc 端到端时延（感知触发 → L5 指令）< 500ms | `test_reflex_arc_p99_lt_500ms`（`test_timing_e3.py`） | 待 HIL 硬件执行 | 同上 |
| L4 三模式切换 < 100ms | `test_l4_mode_switch_p99_lt_100ms`（`test_timing_e3.py`） | 待 HIL 硬件执行 | 同上 |
| Rule 17 Hold-course：M5 不输出规避动作 | `test_hil_rule17_standon`（`test_hil_e2e.py`） | 测试框架就绪 | 取决于 F-INT-001 修复；场景注入路径完整 |
| 故障注入（感知降级 DEGRADED）：M1 正确切换 ODD | `test_hil_rule19_degraded`（`test_hil_e2e.py`） | 测试框架就绪（ToR 路径受限） | M8 ToR callsite 缺口影响 ToR 分支验证；`fix/m8-tor-callsite` 待合并 |

---

## §5 已知 Gap / 阻断项

### F-INT-001（继承自 E2）：M6/M5 topic 前缀不一致

- **位置**：`src/m6_colregs_reasoner/src/colregs_reasoner_node.cpp:214`（发布 `/l3/m6/colregs_constraint`）与 `src/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:58`（订阅 `/m6/colregs_constraint`，缺 `/l3/` 前缀）
- **影响**：M5 永远收不到 M6 约束输出，COLREGs 推理链断路，100 场景通过率无法达到 ≥95%
- **优先级**：P0
- **修复分支**：`fix/integration-topic-mismatches`

### F-INT-002（继承自 E2）：M7 订阅错误 ODD 状态 topic

- **位置**：`src/m7_safety_supervisor/src/safety_supervisor_node.cpp:69`（订阅 `/l3/m2/odd_state`，应为 `/l3/m1/odd_state`）
- **影响**：M7 Safety Supervisor 在 ODD 降级时无法接收状态更新；影响 `test_hil_rule19_degraded` IEC 61508 SIL 2 路径
- **优先级**：P0
- **修复分支**：`fix/integration-topic-mismatches`

### HIL 硬件依赖

- 所有时序 DoD 条目（Mid-MPC / BC-MPC / Reflex Arc / L4 模式切换）须在实物 FCB 硬件上执行后确认；仿真环境计时不作为认证证据

### M8 ToR callsite 缺口（继承自 E2）

- `publish_tor_request()` 方法存在但无自动触发点，影响 `test_hil_rule19_degraded` 中 ToR 路径验证
- 修复追踪：`fix/m8-tor-callsite`

---

## §6 测试工具链概述

| 工具 | 路径 | 技术栈 | 职责 |
|---|---|---|---|
| FCB MMG 仿真器 | `src/fcb_simulator/` | C++ / ROS2 | 4-DOF 船舶动力学，RK4 50 Hz，发布自船状态 + 目标跟踪 |
| ROC HMI 仿真器 | `tools/roc_simulator/` | Python / Flask + ROS2 | 仿真 ROC 操作员界面，支持 ToR 响应注入 |
| 场景生成器 | `tests/hil/generate_scenarios.py` | Python | 生成 3209 个 COLREGs 场景，分层抽样 100 基准场景 |
| 目标注入器 | `tests/hil/target_injector.py` | Python / ROS2 | 按场景定义向 DDS 总线注入目标 + 环境消息（1 Hz） |
| HIL 场景运行器 | `tests/hil/hil_runner.py` | Python | 驱动单场景执行，30s 超时，捕获结果 |
| 性能计时验证器 | `tests/hil/benchmarks/timing_verifier.py` | Python / ROS2 | DDS 时序测量，最近邻分位数（P99），discovery-wait 循环 |

---

## §7 下一步

| 优先级 | 任务 |
|---|---|
| P0 | 合并本 PR → main（wave-4/integration） |
| P0 | 修复 F-INT-001 + F-INT-002（topic 前缀不一致），分支 `fix/integration-topic-mismatches` |
| P0 | 修复 M8 ToR callsite，解除 `test_hil_rule19_degraded` ToR 路径阻断，分支 `fix/m8-tor-callsite` |
| P1 | 在实物 FCB 硬件上执行 100 场景套件，目标：≥95/100 通过率（需 F-INT-001 已修复） |
| P1 | 在实物 FCB 硬件上采集时序基准（Mid-MPC / BC-MPC / Reflex Arc / L4 切换 P99 实测值） |
| P2 | HAZID RUN-001 完成 → v1.1.3 参数回填（2026-08-19） |
| P2 | Phase E4 实船试航启动（计划 2026-06） |
