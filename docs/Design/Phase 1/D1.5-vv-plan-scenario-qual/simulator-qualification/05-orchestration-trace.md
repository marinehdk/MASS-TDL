# 05 — libcosim API Trace & Communication Step Audit

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-05 |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 🟡 Projected — libcosim 已集成至 SIL 编排器, trace 工具待开发 |
| 上游 | D1.3.1 模拟器鉴定报告 README §数据可用性分级；8 月计划 v3.0 D1.3.1；SIL 架构设计 v1.0 §1 |
| 下游交付物 | `06-evidence-matrix.md` §7 Tool Integration & Data Flow——traceability 证据 |
| 置信度 | 🟡 Projected（拓扑 + API 设计完整 / trace dump 数据待采集） |

---

## §1 Proof Objective

证明 SIL 编排层的 libcosim 主控循环调用在以下维度是可审计且确定性的：

- **(O1) 时间正确性**：每个 FMU simulation step 的实际步长与标称步长偏差 < ±0.001s (1ms)
- **(O2) 通信完整性**：libcosim → 3 个 FMU slaves 之间的变量读写序列无丢包、无乱序、无重复
- **(O3) FMU 执行顺序**：`fmu_1 (env_disturbance)` → `fmu_2 (ship_dynamics)` → `fmu_3 (target_vessel)` 的执行顺序与设计文档一致
- **(O4) DDS 桥接一致性**：libcosim 每个 step 与 ROS2 DDS topic 发布次数匹配（1 step → 1 publish to `ship_state` → 1 publish to `target_state`)
- **(O5) 端到端可追溯**：每条 rosbag2 MCAP 记录可反向追溯到唯一的 libcosim step 调用轨迹

---

## §2 SIL Orchestration Topology

```
┌──────────────────────────────────────────────────────┐
│                   sil_orchestrator                     │
│                     (FastAPI + rclpy)                  │
│                                                        │
│  ┌──────────────────────────────────────────────┐     │
│  │            Scenario Lifecycle Manager          │     │
│  │                                                │     │
│  │  configure() → activate() → run() → deactivate()│     │
│  └──────────────────┬───────────────────────────┘     │
│                     │                                  │
│                     ▼                                  │
│  ┌──────────────────────────────────────────────┐     │
│  │           libcosim Co-Simulation Master        │     │
│  │                                                │     │
│  │  for t in [0, T]:                             │     │
│  │    step(t, dt) ──────────────┐                │     │
│  │    observer::step_complete() │                │     │
│  └──────────────────────────────┼────────────────┘     │
│                                 │                       │
│              ┌──────────────────┼──────────────────┐   │
│              │                  │                  │   │
│              ▼                  ▼                  ▼   │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│  │ FMU Slave 1   │  │ FMU Slave 2   │  │ FMU Slave 3   │
│  │ env_disturb.  │  │ ship_dynamics │  │ target_vessel │
│  │ (Gauss-Markov)│  │ (FCB 4-DOF)   │  │ (ncdm_intell.)│
│  └───────┬───────┘  └───────┬───────┘  └───────┬───────┘
│          │                  │                  │
│          └──────────────────┼──────────────────┘
│                             │
│                    ┌────────▼────────┐
│                    │  DDS Bridge     │
│                    │  (ros2_fmu      │
│                    │   proxy node)   │
│                    └────────┬────────┘
│                             │
│              ┌──────────────┼──────────────┐
│              ▼              ▼              ▼
│          ship_state    target_state    env_state
│         (l3_msgs/msg) (l3_msgs/msg) (l3_msgs/msg)
│                             │
│                    ┌────────▼────────┐
│                    │  rosbag2 MCAP   │
│                    │  (50Hz record)  │
│                    └─────────────────┘
└──────────────────────────────────────────────────────┘
```

**拓扑说明**：
- **FastAPI** 对外提供 REST API (场景 CRUD / 自检 / 导出)；`rclpy` 对内调用 ROS2 原语
- **libcosim** 作为 co-simulation master，顺序驱动 3 个 FMU 2.0 slaves
- **ros2_fmu proxy node** 运行在 `sim_workbench` composable node 进程内，将每个 step 的 FMU 输出转换为 ROS2 topic
- **rosbag2 MCAP** 由 `sil_orchestrator` 在 `configure()` 阶段自动启动录制，`deactivate()` 阶段停止并导出

---

## §3 libcosim API Trace Design

### §3.1 Traced API Calls

以下 5 个 libcosim API 调用被注入 trace 钩子，每个调用记录结构化 JSON 事件，写入 `annex/test-results/d1_3_1_orch_trace_{run_id}.jsonl`。

| # | libcosim API | 事件名称 | 记录字段 |
|---|---|---|---|
| 1 | `cosim::simulator::step(double current_time, cosim::duration dt)` | `simulator_step` | `{run_id, step_idx, t, dt_requested, dt_actual, wall_clock_us, slave_states}` |
| 2 | `cosim::slave::set_real(variable_id, value)` | `slave_set_real` | `{run_id, step_idx, slave_index, variable_id, value_set, timestamp_ns}` |
| 3 | `cosim::slave::get_real(variable_id)` | `slave_get_real` | `{run_id, step_idx, slave_index, variable_id, value_read, timestamp_ns}` |
| 4 | `cosim::observer::simulator_added(simulator_index)` | `observer_sim_added` | `{run_id, observer_id, sim_index, sim_name, sim_description}` |
| 5 | `cosim::observer::step_complete(step_number, duration, time_point)` | `observer_step_complete` | `{run_id, step_idx, step_number, dt_elapsed, time_in_step_ns, overrun: bool}` |

### §3.2 JSON Output Format Spec

每个事件为单行 JSON (`*.jsonl`) 以支持流式追加写入，单行不超过 2KB。

```json
{
  "run_id": "sil_001_2026-05-15T103000Z",
  "event": "simulator_step",
  "timestamp_utc": "2026-05-15T10:30:01.234Z",
  "data": {
    "step_idx": 0,
    "t": 0.0,
    "dt_requested": 0.02,
    "dt_actual": 0.01998,
    "wall_clock_us": 1243,
    "slave_states": [
      {"slave": "env_disturbance", "status": "OK"},
      {"slave": "ship_dynamics", "status": "OK"},
      {"slave": "target_vessel", "status": "OK"}
    ]
  }
}
```

**字段约束**：
- `run_id`：格式为 `sil_{batch}_{ISO8601}`, 与 rosbag2 filename 一致
- `timestamp_utc`：ISO 8601 毫秒精度
- `slave_states[].status` 枚举：`"OK" | "DEGRADED" | "ERROR" | "TIMEOUT"`
- `overrun`：`true` 当 `wall_clock_us` > `dt_actual * 1.0e6` 时标志超限

### §3.3 Communication Step Audit Checklist

每次 trace dump 需通过以下审核清单：

| # | 检查项 | 判据 | 失败后果 |
|---|---|---|---|
| C1 | **dt 一致性** | 所有 step 的实际 dt 与请求 dt 偏差 < ±0.001s | 标记 DEGRADED step，≥1% DEGRADED→拒绝该 run 的统计数据 |
| C2 | **无丢步 (no dropped steps)** | step_idx 严格递增无间断 | 如果出现间隙 → 审计 alarm，阻断出口 |
| C3 | **FMU 执行顺序** | `set_real/get_real` 序列为 slave_1 → slave_2 → slave_3 | 乱序 → 架构违规，提交 bug |
| C4 | **DDS publish count** | 每个 step 的 `ship_state` 发布次数 = 1, `target_state` 发布次数 = 1 | 不匹配 → 通信审计失败 |
| C5 | **overrun = false** | 所有 step 的 `wall_clock_us` ≤ `dt_actual * 1.0e6` | 任何 overrun → 延迟审计失败 |
| C6 | **FMU state consistency** | `slave_states[].status` 全为 `"OK"` 时 ODD=ACTIVE | 任一 `"ERROR"` → 检查 ODD transition |

---

## §4 Audit Automation Tool

### §4.1 Tool Specification

| 属性 | 值 |
|---|---|
| 工具名称 | `d1_3_1_orch_trace.py` |
| 存放路径 | `tools/sil/d1_3_1_orch_trace.py` |
| 运行方式 | `python tools/sil/d1_3_1_orch_trace.py --run-id <id> --threshold-overrun 0 --threshold-degraded 0.01` |
| 输入 | `annex/test-results/d1_3_1_orch_trace_{run_id}.jsonl` |
| 输出 | `annex/test-results/d1_3_1_orch_audit_{run_id}.json` (PASS/FAIL + 6 项检查结果) |

### §4.2 Audit Output Format

```json
{
  "run_id": "sil_001_2026-05-15T103000Z",
  "audit_timestamp": "2026-05-15T10:31:00Z",
  "result": "PASS",
  "checks": {
    "C1_dt_consistency": {"status": "PASS", "max_deviation": 0.0003, "degraded_steps": 0, "total_steps": 5000},
    "C2_no_drops": {"status": "PASS", "gaps_found": 0},
    "C3_fmu_order": {"status": "PASS", "reordered_steps": 0},
    "C4_dds_count": {"status": "PASS", "mismatch_steps": 0},
    "C5_overrun": {"status": "PASS", "overrun_steps": 0, "max_wall_clock_us": 8432},
    "C6_fmu_state": {"status": "PASS", "error_steps": 0}
  },
  "summary": {
    "total_steps": 5000,
    "simulated_time_s": 100.0,
    "wall_time_s": 2.84,
    "speedup_ratio": 35.2,
    "P99_step_latency_us": 3940
  }
}
```

---

## §5 Results — Audit Summary

> 🟡 **Projected** — 以下表格为期望值。数据待 `tools/sil/d1_3_1_orch_trace.py` 开发完成后由 CI 自动化填充。

| run_id | Steps | Sim Time (s) | Max dt Deviation (s) | Dropped Steps | Overruns | FMU Order OK | DDS Match | P99 Latency (μs) | Result |
|---|---|---|---|---|---|---|---|---|---|
| `sil_001` | 5000 | 100.0 | 0.0003 | 0 | 0 | ✅ | ✅ | 3940 | PASS |
| `sil_002` | 5000 | 100.0 | 0.0005 | 0 | 0 | ✅ | ✅ | 4120 | PASS |
| `sil_003` | 15000 | 300.0 | 0.0004 | 0 | 0 | ✅ | ✅ | 4030 | PASS |
| `sil_004` | 15000 | 300.0 | 0.0002 | 0 | 0 | ✅ | ✅ | 3880 | PASS |
| `sil_005` | 25000 | 500.0 | 0.0006 | 0 | 0 | ✅ | ✅ | 4010 | PASS |
| `sil_006` | 50000 | 1000.0 | 0.0003 | 0 | 0 | ✅ | ✅ | 3950 | PASS |
| `sil_007` | 50000 | 1000.0 | 0.0005 | 0 | 0 | ✅ | ✅ | 4080 | PASS |
| `sil_008` | 100000 | 2000.0 | 0.0004 | 0 | 0 | ✅ | ✅ | 3980 | PASS |

---

## §6 Acceptance Criteria

| # | 判据 | 阈值 | 验证方式 |
|---|---|---|---|
| AC-5.1 | 全量 SIL run 的 step P99 latency | < 5 ms | `d1_3_1_orch_trace.py` 自动统计 |
| AC-5.2 | overrun step 数（任一 step 的 wall clock > dt） | 0 (zero overruns) | trace JSONL 断言 |
| AC-5.3 | 丢步数（dropped steps） | 0 | step_idx 连续性检查 |
| AC-5.4 | FMU slave state error step 数 | 0 | `slave_states[].status == "ERROR"` 检查 |

---

## §7 References

| Ref | Source | Description |
|---|---|---|
| [R-SIL-ARCH §1] | SIL 架构设计 v1.0 §1 | 系统拓扑 5 维度决策 |
| [R-SIL-ARCH §8] | SIL 架构设计 v1.0 §8 (ship_dynamics_node) | FMU slave 2 ship_dynamics 接口 |
| [R-libcosim] | libcosim 0.10.x API docs, https://github.com/open-simulation-platform/libcosim | Co-simulation master API |
| [R-FMI-2.0] | FMI 2.0 Standard, https://fmi-standard.org/ | FMU slave interface contract |
| [R-VVP §6] | V&V Plan v0.1 §6 SIL Latency Budget | SIL 延迟预算与 jitter buffer 设计 |
| [R-D1.6] | Scenario schema D1.6 | 场景到 FMU 变量的映射 |
