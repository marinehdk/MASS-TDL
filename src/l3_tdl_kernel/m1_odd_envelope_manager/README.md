# M1 ODD/Envelope Manager

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M1-001 |
| 路径强度 | **PATH-S（安全关键路径）** — IEC 61508 SIL 2 核心安全功能 |
| 详细设计 | `docs/Design/Detailed Design/M1-ODD-Envelope-Manager/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M1/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §3 + §5 + §11.6 + ADR-002 |

## 角色

整个 L3 战术决策层的**调度枢纽**，唯一的"当前安全语境"权威。M1 ODD 状态变化是 L3 内部所有其他模块的行为切换权威来源（决策一）。

## 核心职责

- **5 状态 ODD 状态机**：IN / EDGE / OUT / MRC_PREP / MRC_ACTIVE（架构 §3.5）
- **Conformance_Score 三轴评分**：E（环境）/ T（任务） / H（人因）权重融合（架构 §3.6）
- **TMR / TDL 量化**：Maximum Operator Response Time / Automation Response Deadline
- **MRC 触发逻辑**：选择 MRM-01 漂移 / MRM-02 抛锚 / MRM-03 顶风停 / MRM-04 系泊
- **接管 / 回切顺序化**：响应 Hardware Override + Reflex Arc + M7 SOTIF 告警

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（4 publishers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件型补发
- `/l3/m1/mode_cmd` (l3_msgs/ModeCmd) — 事件
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz
- `/l3/sat/data` (l3_msgs/SATData) — 10 Hz

**订阅**（6 subscribers）：
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/m7/safety_alert` (l3_msgs/SafetyAlert) — 事件
- `/fusion/own_ship_state` (l3_external_msgs/FilteredOwnShipState) — 50 Hz
- `/fusion/environment_state` (l3_external_msgs/EnvironmentState) — 0.2 Hz
- `/reflex/activation_notification` (l3_external_msgs/ReflexActivationNotification) — 事件
- `/override/active_signal` (l3_external_msgs/OverrideActiveSignal) — 事件

## 主要类（include/m1_odd_envelope_manager/）

- `OddStateMachine` — 5 状态 FSM + 转移函数
- `ConformanceScoreCalculator` — E/T/H 三轴权重计算
- `TmrTdlEstimator` — TMR/TDL 实时计算
- `MrcTriggerLogic` — MRC 选择 + 触发
- `OddEnvelopeManagerNode` — ROS2 节点 + Lifecycle 管理

## PATH-S 强约束（编码强制）

- **禁动态分配** — 启动后所有内存预分配（`-fno-exceptions` + `cppcoreguidelines-no-malloc`）
- **禁异常** — 用 `tl::expected<T, ErrorCode>`（C++17 polyfill）
- **函数 ≤ 40 行 + 循环复杂度 ≤ 8**（`.clang-tidy.path-s` 强制）
- **Polyspace Code Prover** — Red = 0 / Orange ≤ 1%（CCS 入级证据）
- **Doer-Checker 独立性** — M7 不可包含 M1 内部 header（CI 自动验证）

## 配置（YAML 注入；37 个 [TBD-HAZID] 标注）

- `config/m1_params.yaml` — Conformance_Score 权重 / EDGE 阈值 / CPA/TCPA 阈值（HAZID RUN-001 校准）
- `config/m1_logging.yaml` — spdlog 独立 logger 实例

## 开发命令

```bash
# Build (debug + ASan + UBSan + coverage)
colcon build --packages-up-to m1_odd_envelope_manager \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Run unit tests
colcon test --packages-select m1_odd_envelope_manager
colcon test-result --verbose

# Coverage report
lcov --capture --directory build/m1_odd_envelope_manager --output-file coverage.info
genhtml coverage.info --output-directory coverage-html

# Static analysis (PATH-S strict — uses .clang-tidy.path-s)
run-clang-tidy-20 -p build/m1_odd_envelope_manager \
                  -config-file=.clang-tidy.path-s \
                  -warnings-as-errors='*' \
                  src/m1_odd_envelope_manager/src/*.cpp

# Doer-Checker independence (always run)
bash tools/ci/check-doer-checker-independence.sh
```

## DoD（PATH-S）

参见 `docs/Implementation/M1/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 95%
- 0 dynamic allocation（启动后）
- 0 throw / 0 exception
- Polyspace Red = 0 / Orange ≤ 1%
- 全部 [TBD-HAZID] 通过 YAML 注入（不硬编码）
- 跨模块接口字段对齐架构 v1.1.2 §15.1
- ASDR 决策日志含 SHA-256 签名（RFC-004）

## 引用

- **架构基线**：[v1.1.2 §3 ODD/Operational Envelope 框架](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) + §5 + §11.6
- **详细设计**：[M1/01-detailed-design.md](../../docs/Design/Detailed%20Design/M1-ODD-Envelope-Manager/01-detailed-design.md)
- **代码骨架 spec**：[M1/code-skeleton-spec.md](../../docs/Implementation/M1/code-skeleton-spec.md)
- **CCS / IEC 61508 / ISO 21448 SOTIF**：详见架构 §1.3 引用 [R1, R5, R6]
- **Veitch 2024 TMR ≥ 60s 实证基线**：架构 §3.4 + [R4]
