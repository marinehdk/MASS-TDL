# M7 Safety Supervisor

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M7-001 |
| 路径强度 | **PATH-S（安全关键路径）+ Doer-Checker 独立路径** — IEC 61508 SIL 2 + ADR-001 + 决策四 |
| 详细设计 | `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M7/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §11 + ADR-001 + RFC-003 + RFC-005 |

## 角色

Doer-Checker 双轨架构中的 **Checker**（运行时安全仲裁器）。M1–M6 是 Doer，M7 独立路径监控其假设违反 + IEC 61508 故障 + 接收 X-axis Deterministic Checker 否决通知，触发预定义 MRM 命令集（不动态生成轨迹）。

CCS 入级核心证据 — Boeing 777 Monitor / Airbus FBW COM/MON / NASA Auto-GCAS 工业先例。

## 核心职责

- **SOTIF 假设监控**：监控 M1–M6 各模块假设违反（confidence < 阈值 / stamp 过时 / SAT-3 不可达 / NLP 求解失败 / WorldState 老化 / ODD 边界）
- **IEC 61508 故障监控**：心跳超时 + DDS QoS 不兼容事件升级
- **CheckerVetoNotification 处理**（RFC-003 enum 化）：仅按 6 项 enum 分类升级，**不解析 free text**
- **100 周期 = 15s 滑窗**（RFC-003 锁定）：监控 Checker 否决率（20% 阈值 [TBD-HAZID]）
- **MRM 命令集**（4 种预定义，不动态生成）：MRM-01 漂移 / MRM-02 抛锚 / MRM-03 顶风停 / MRM-04 系泊
- **MrmSelector**：基于场景（ODD 子域 + 水深 + 速度）选择最适 MRM
- **ASDR 决策日志**：含 SHA-256 防篡改签名

## ROS2 节点拓扑（v1.1.2 §15.2）— **独立 OS 进程，禁止 composable**

**发布**（2 publishers）：
- `/l3/m7/safety_alert` (l3_msgs/SafetyAlert) — 事件
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz（独立 spdlog logger 实例 m7_supervisor）

**订阅**（7 subscribers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState)
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/m4/behavior_plan` (l3_msgs/BehaviorPlan) — 2 Hz
- `/l3/m5/avoidance_plan` (l3_msgs/AvoidancePlan) — 1–2 Hz
- `/l3/m5/reactive_override_cmd` (l3_msgs/ReactiveOverrideCmd) — 事件
- `/l3/m6/colregs_constraint` (l3_msgs/COLREGsConstraint) — 2 Hz
- `/checker/veto_notification` (l3_external_msgs/CheckerVetoNotification) — 事件 — RFC-003 enum 化

## 主要类（include/m7_safety_supervisor/）

- `SotifAssumptionMonitor` — 6 类假设违反检查谓词
- `Iec61508FaultMonitor` — 心跳 / 超时
- `CheckerVetoHandler` — RFC-003 enum 分类升级
- `SlidingWindow15s` — 编译期固定 size = 100 循环 buffer
- `MrmCommandSet` — 4 种预定义 MRM
- `MrmSelector` — MRM 选择逻辑
- `SafetyAlertGenerator` — Safety_AlertMsg 生成
- `Sha256Signer` — ASDR 签名（防篡改）
- `SafetySupervisorNode` — ROS2 节点（独立 OS 进程）

## Doer-Checker 独立性约束（决策四 + ADR-001 强制）

- **代码独立性**：不复用 M1–M6 任何源文件 / header；仅 #include `l3_msgs/msg/*.hpp` + `l3_external_msgs/msg/CheckerVetoNotification.hpp`
- **库白名单**（5 个）：rclcpp / l3_msgs / l3_external_msgs / Eigen 5.0.0 / spdlog 1.17.0 / fmt 11.0.2 / yaml-cpp 0.8.0（仅日志配置）/ GTest 1.17.0
- **库黑名单**（CI 阻断）：CasADi / IPOPT / GeographicLib / nlohmann::json / Boost.Geometry / Boost.PropertyTree
- **数据结构独立性**：不引用 `mass_l3::m1::*` 等命名空间；仅 `l3_msgs::msg::*`
- **构建独立性**：独立 colcon package + CMakeLists.txt + 独立 CI job
- **运行时独立性**：独立 OS 进程（**禁止 composable node**）+ 独立 spdlog instance（独立日志文件 m7.log）
- **自动验证**：`tools/ci/check-doer-checker-independence.sh`（每 commit 跑，0 violation）

## PATH-S 强约束（编码强制）

- **禁动态分配** — 启动后所有内存预分配（`-fno-exceptions` + `cppcoreguidelines-no-malloc`）
- **禁异常** — 用 `tl::expected<T, ErrorCode>` / 自定义 `NoExceptionResult` 模式
- **禁 RTTI**（`-fno-rtti`）— 减小二进制 + Polyspace 分析面
- **函数 ≤ 40 行 + 循环复杂度 ≤ 8**（`.clang-tidy.path-s` 强制）
- **Polyspace Code Prover** — Red = 0 / Orange ≤ 1%（CCS 入级证据）
- **单测覆盖率 ≥ 95%**

## 配置（YAML 注入；含 RFC-003 锁定 + [TBD-HAZID]）

- `config/m7_params.yaml`：
  - 100 周期窗口 = 15s（RFC-003 **锁定**，不可变）
  - Checker 否决率 20% 阈值（[TBD-HAZID]）
  - SOTIF 阈值（[TBD-HAZID]）
- `config/m7_logger.yaml` — 独立 spdlog logger 配置（m7.log）
- `config/mrm_command_set.yaml` — 4 种 MRM 预定义参数

## 开发命令

```bash
# Build (PATH-S strict)
colcon build --packages-up-to m7_safety_supervisor \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined -fno-exceptions -fno-rtti"

# Unit tests
colcon test --packages-select m7_safety_supervisor

# Coverage (PATH-S threshold ≥ 95%)
lcov --capture --directory build/m7_safety_supervisor --output-file coverage.info
bash tools/ci/check-coverage-threshold.sh coverage.info m7_safety_supervisor

# Static analysis (PATH-S strict — uses .clang-tidy.path-s)
run-clang-tidy-20 -p build/m7_safety_supervisor \
                  -config-file=.clang-tidy.path-s \
                  -warnings-as-errors='*' \
                  src/m7_safety_supervisor/src/*.cpp

# Doer-Checker independence (always run — 0 violation strict)
bash tools/ci/check-doer-checker-independence.sh

# Polyspace Code Prover (PATH-S only)
polyspace-bug-finder-server -prog m7_safety_supervisor \
                            -lang cpp -cpp-version cpp17 \
                            -checkers MISRA_Cpp_2023_Mandatory,MISRA_Cpp_2023_Required,IEC_61508_Mandatory \
                            -results-dir build/polyspace-m7_safety_supervisor
bash tools/ci/check-polyspace-thresholds.sh build/polyspace-m7_safety_supervisor
```

## DoD（PATH-S — 强约束）

参见 `docs/Implementation/M7/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 95%
- 0 dynamic allocation（启动后）
- 0 throw / 0 exception
- Polyspace Red = 0 / Orange ≤ 1%
- Doer-Checker independence CI 0 violation
- 全部 [TBD-HAZID] 通过 YAML 注入
- ASDR 决策日志含 SHA-256 签名（RFC-004）
- 提交件含独立性矩阵报告（CCS 入级证据）

## 引用

- **架构基线**：[v1.1.2 §11 M7 Safety Supervisor + ADR-001](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M7/01-detailed-design.md](../../docs/Design/Detailed%20Design/M7-Safety-Supervisor/01-detailed-design.md)
- **代码骨架 spec**：[M7/code-skeleton-spec.md](../../docs/Implementation/M7/code-skeleton-spec.md)
- **第三方库政策 §3.1 独立性矩阵**：[third-party-library-policy.md](../../docs/Implementation/third-party-library-policy.md)
- **RFC-003 决议（CheckerVetoNotification enum 化）+ RFC-005（Reflex Arc spec）**：[RFC-decisions.md](../../docs/Design/Cross-Team%20Alignment/RFC-decisions.md)
- **工业先例**：Boeing 777 Monitor / Airbus FBW COM/MON / NASA Auto-GCAS [R4] / Jackson 2021 Certified Control [R12]
