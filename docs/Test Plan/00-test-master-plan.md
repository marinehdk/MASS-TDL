# MASS L3 TDL — Test Master Plan

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-TEST-MASTER-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-07 |
| 阶段 | Wave 4a |
| 状态 | 草稿（待 HAZID 校准完成后升级至正式） |
| 架构基线 | v1.1.2 §11–§15 |

---

## §1 范围

本文档覆盖 **Phase E2（集成测试）** 和 **Phase E3（HIL 仿真测试）**。

**不在范围内：**
- Phase E4（实船海试，Wave 4b — 独立交付物）
- L1/L2/L4/L5 层的测试（其他团队职责）
- 多模态感知融合子系统测试

### 1.1 Phase E2 — 集成测试（Wave 4a）

对象：8 个 ROS2 模块（M1–M8）在 full-stack launch 环境下的端到端消息链路。
场景数：INT-001 至 INT-008，共 8 个集成场景。
环境：`tests/integration/` GTest + pytest；`tools/ci/full_l3_stack.launch.py` 启动全栈。

### 1.2 Phase E3 — HIL 仿真测试（Wave 4a）

对象：COLREGs 场景覆盖（Rule 13/14/15/17/18/19）+ 故障注入。
场景数：≥100 个 COLREGs 场景（目标 1000+）。
环境：`tests/hil/` + `src/fcb_simulator/`（Wave 4a 后期创建）。

---

## §2 Phase E2 完成判据（DoD）

以下全部通过方为 E2 完成：

- [ ] INT-001 至 INT-008 全部 PASS（GTest RED → GREEN）
- [ ] `pytest tests/integration/` 零失败（含 smoke test）
- [ ] `ros2 launch tools/ci/full_l3_stack.launch.py` 无节点崩溃退出（≥30s 稳定运行）
- [ ] `bash tools/ci/check-msg-contract.sh` 零违规
- [ ] CI `stage-4-integration` job 绿色（MR + main 分支均通过）
- [ ] 每个 INT 场景的断言时序满足规定上限（详见 §5）
- [ ] ASDR 记录完整：所有 8 模块均产出 ≥1 条带 sha256 签名的记录

---

## §3 Phase E3 完成判据（DoD）

- [ ] 100 个基准 COLREGs 场景通过率 ≥ 95%（≥95/100）
- [ ] M5 Mid-MPC 计算时延 < 100ms（P99，FCB 硬件）
- [ ] M5 BC-MPC 计算时延 < 50ms（P99，FCB 硬件）
- [ ] Reflex Arc 端到端时延（感知触发 → L5 指令） < 500ms
- [ ] L4 三模式切换（normal_LOS / avoidance_planning / reactive_override）< 100ms
- [ ] Rule 17 Hold-course 场景：M5 不输出规避动作（正确"按兵不动"）
- [ ] 故障注入（感知降级 DEGRADED）：M1 正确切换 ODD 状态，M7 无误报

---

## §4 测试基础设施概述

### 4.1 目录结构

```
tests/
  integration/               ← Phase E2（本 Wave 4a 交付）
    CMakeLists.txt
    package.xml
    test_int_001_*.cpp        ← INT-001 至 INT-008 GTest 文件
    test_int_002_*.cpp
    ...
    test_int_008_*.cpp
    python/
      conftest.py             ← ROS2 session fixture
      test_integration_smoke.py
  hil/                       ← Phase E3（Wave 4a 后期创建）
    (TBD)
tools/
  ci/
    full_l3_stack.launch.py  ← 全栈启动（M1–M8 + mock publisher）
    check-msg-contract.sh    ← 接口契约校验（始终运行）
src/
  fcb_simulator/             ← HIL 船舶动力学仿真（Wave 4a 后期创建）
    (TBD)
```

### 4.2 CI 集成

`stage-4-integration`（`.gitlab-ci.yml` §4）在 `main` 分支和 MR 上自动触发：

1. `colcon build`（含 `l3_integration_tests` 包）
2. `ros2 launch tools/ci/full_l3_stack.launch.py &`（后台启动全栈，等待 5s 稳定）
3. `pytest tests/integration/ --junit-xml=... --timeout=300`
4. 终止全栈进程
5. `bash tools/ci/check-msg-contract.sh`（消息契约校验）

### 4.3 Mock 策略

- `l3_external_mock_publisher` 节点：模拟 L2 WP list、Multimodal Fusion Track、
  X-axis Checker Veto、Y-axis Reflex Arc 通知、Hardware Override 信号。
- 各 INT 场景通过注入特定 mock 消息触发被测链路（详见 §5 和
  `docs/Test Plan/integration-test-spec.md`）。

---

## §5 INT 场景一览表（INT-001 至 INT-008）

| 场景 ID | 被测链路 | 关键时序约束 | 测试文件 |
|---|---|---|---|
| INT-001 | M2→M6→M5 (Rule 14 head-on) | COLREGsConstraint ≤500ms, AvoidancePlan ≤1000ms | `test_int_001_m2_m6_m5_chain.cpp` |
| INT-002 | M3→L2 replan (4 status codes) | ACTIVE→REPLAN_WAIT→ACTIVE | `test_int_002_m3_l2_replan.cpp` |
| INT-003 | X-axis Checker Veto → M7 → M1 DEGRADED | SafetyAlert ≤200ms, 6 veto enum values | `test_int_003_checker_veto.cpp` |
| INT-004 | Y-axis Reflex Arc → M1 OVERRIDDEN broadcast | ≤50ms 全栈冻结 | `test_int_004_reflex_override.cpp` |
| INT-005 | Hardware Override → M1 OVERRIDDEN → M5 freeze | M5 freeze ≤100ms | `test_int_005_006_override_dual_interface.cpp` |
| INT-006 | M5 tri-mode switch + RFC-001 scheme B | 每次模式切换 <100ms | `test_int_005_006_override_dual_interface.cpp` |
| INT-007 | ASDR 全模块覆盖（M1–M8）| 8 模块各 ≥1 条记录，sha256 非空，≤5s | `test_int_007_asdr_full.cpp` |
| INT-008 | M8 SAT-2 分类 + IMO ToR 握手 | 4 触发条件分类正确，ToR 含 60s deadline | `test_int_008_m8_sat_tor.cpp` |

---

## §6 Phase E3 HIL 场景分类

| 分类 | 子场景示例 | 预期结论 |
|---|---|---|
| Rule 13 (overtaking) | 追越目标从右后方接近，CPA < 0.5nm | M6 给出让路约束，M5 输出右转规避 |
| Rule 14 (head-on) | 双船对遇，DCPA → 0 | M6 Rule 14，双方各右转 |
| Rule 15 (crossing) | 目标从右舷 045° 接近，TCPA < 5min | M6 give-way，M5 右转或减速 |
| Rule 17 (stand-on) | 本船为直航船，目标为让路船已采取行动 | M5 保持航向航速，不输出规避 |
| Rule 18 (responsibilities) | 机动船 vs 帆船/渔船场景 | M6 正确识别 vessel_type，输出让路约束 |
| Rule 19 (restricted visibility) | 雷达目标，ODD 能见度 < 0.5nm | M1 切换至 DEGRADED，M5 降速 |
| 故障注入：感知降级 | Multimodal Fusion confidence 跌至 0.2 | M2 置信度传播，M7 触发 SafetyAlert |
| 故障注入：GNSS 丢失 | Nav Filter 位置不可用 | M1 OUT-of-ODD，M3 触发 MRC |
| 多目标场景 | ≥3 个同时接近目标 | M4 IvP 仲裁无死锁，M5 输出可行解 |

---

## §7 CCS 认证证据映射

| 测试场景 | 对应 CCS 子功能（DMV-CG-0264）| 证据类型 |
|---|---|---|
| INT-001 (Rule 14 chain) | 碰撞威胁感知 + COLREGs 规则推理 + 规避路径规划 | JUnit XML + 消息时序日志 |
| INT-002 (M3 replan) | 任务航次管理 | JUnit XML |
| INT-003 (Checker veto) | 安全监督 + 模式管理 | JUnit XML + SafetyAlert 记录 |
| INT-004 (Reflex Arc) | 紧急反射弧（Y-axis bypass） | 时序验证（≤50ms） |
| INT-005/006 (Override) | 硬件接管 + 控制模式切换 | JUnit XML |
| INT-007 (ASDR) | 扩展航行数据记录（IMO MASS §4.3） | ASDR 完整性 + sha256 验证 |
| INT-008 (SAT/ToR) | 人机界面透明性 + IMO ToR 协议 | SAT-1/2/3 触发记录 |
| Phase E3 HIL Rule 14/15/17 | COLREGs 完整性（>95% 通过率） | HIL 场景结果数据库 |

---

*本文档在 Phase E2 全部通过后升级至 v1.1，在 CCS AIP 申请前升级至 v2.0。*
