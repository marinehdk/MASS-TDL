# M5 Tactical Planner 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M5-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M5 Phase E1 起点） |
| 团队 | Team-M5 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则强制 |
| 详细设计基线 | `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md` v1.0（SANGO-ADAS-L3-DD-M5-001）|
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §10 + §15.1 + RFC-001 决议（方案 B）|
| 关联 RFC | RFC-001（M5 ↔ L4 双接口三模式）已批准 |
| 关键依赖库 | CasADi 3.7.2 (LGPL-3.0 动态链接) + IPOPT 3.14.19 (EPL-2.0) + GeographicLib 2.7 (MIT) + Eigen 5.0.0 + Boost.Geometry 1.91.0 + spdlog 1.17.0 |
| 适用范围 | M5 Tactical Planner 模块的项目结构、ROS2 双节点拓扑、CasADi/IPOPT NLP 集成、单元测试骨架与 PATH-D 合规性 |

> **本文是 Team-M5 编码起点。** 所有源代码、CMake、参数配置、单元测试须按本骨架实施；任何偏离须走 PR review + Team-M5 lead 双签。算法细节（Mid-MPC NLP 公式、BC-MPC 分支树、CPA/TCPA、ROT_max 修正、TSS 多边形约束等）见详细设计 §5；本骨架不复述。

---

## 1. 模块概述

### 1.1 职责一句话

M5 Tactical Planner 是 L3 中**计算强度最高的模块**，将 M4 Behavior Plan 与 M6 COLREGs Constraint 实时转化为避碰轨迹，输出双接口至 L4 Guidance Layer：

- **AvoidancePlan**（主接口，1–2 Hz）：Mid-MPC ≥ 90 s 时域 NLP 求解结果（4-WP + speed_adjustments）
- **ReactiveOverrideCmd**（紧急接口，事件 / ≤ 10 Hz）：BC-MPC 短时域分支树评估结果（紧急 ψ/u/ROT）

### 1.2 双层 MPC 架构图

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  M5 Tactical Planner — 双 ROS2 进程（独立 executable + composable 可选）       │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Process 1: m5_tactical_planner_mid_mpc                               │  │
│  │  - 求解周期: 1–2 Hz（slot 1000 ms）                                  │  │
│  │  - 求解器: CasADi 3.7.2 + IPOPT 3.14.19 (linear_solver=ma27/mumps)  │  │
│  │  - 时域:   N=12–18 步 × Δt=5 s ≈ 60–90 s [TBD-HAZID]               │  │
│  │  - 输入:   World_State / ODD_State / Behavior_Plan /                 │  │
│  │           COLREGs_Constraint / PlannedRoute / SpeedProfile           │  │
│  │  - 输出:   AvoidancePlan @ /l3/m5/avoidance_plan                    │  │
│  │           ASDR @ /l3/asdr/record                                    │  │
│  │           SAT_Data @ /l3/sat/data                                    │  │
│  │  - SLA:    端到端 < 500 ms（求解 < 350 ms 平均）                     │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                          │                                                   │
│                          │ 共享内存 / 内部 topic（决策 §3.4）                 │
│                          │ 传递 last_plan + cost_breakdown                   │
│                          ▼                                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ Process 2: m5_tactical_planner_bc_mpc                                │  │
│  │  - 触发: 事件 (CPA 短时恶化 / 新威胁 / TCPA 压缩 / Mid-MPC 失败)     │  │
│  │  - 求解周期: ≤ 10 Hz（最小间隔 100 ms）                              │  │
│  │  - 算法: 分支树评估（k=5/7 候选航向 × n_targets × n_intents）        │  │
│  │  - 时域: 10–30 s（dt=2 s）                                           │  │
│  │  - 输入: 同 Mid-MPC + last_plan（共享内存）                          │  │
│  │  - 输出: ReactiveOverrideCmd @ /l3/m5/reactive_override_cmd         │  │
│  │  - SLA:  端到端 < 150 ms（详细设计 §6.2）                            │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

> **节点间通信**：默认采用 **rclcpp Intra-Process Communication (IPC)**（同进程时零拷贝）；如部署到不同物理机或独立 OS 进程，回退为内部 ROS2 topic `/l3/m5/internal/last_plan`。决策见 §3.4。

### 1.3 与 v3.0 + v1.1.2 上下文对齐

| 项目 | 对齐内容 |
|---|---|
| **v3.0 Kongsberg 4-WP** | AvoidancePlan 输出 `waypoints[4]` 固定长度，与 Kongsberg K-Pos 对齐（v1.1.2 §10.2）|
| **RFC-001 方案 B** | L4 三模式：`normal_LOS` / `avoidance_planning` / `reactive_override`；切换 SLA 各 < 100 ms / < 50 ms |
| **决策四（Backseat Driver）** | M5 决策核心代码零船型常量；FCB 水动力参数通过 `fcb_vessel_capability.yaml` 注入（§9.3）|
| **Doer-Checker 独立性** | M5 是 Doer；M7 是 Checker，禁用 CasADi/IPOPT/GeographicLib/Boost.Geometry（`third-party-library-policy.md` §3）|
| **v1.1.2 §15.1 IDL** | AvoidancePlanMsg / ReactiveOverrideCmd 字段 100% 对齐 |

---

## 2. 项目目录结构

```
src/m5_tactical_planner/
├── package.xml                                     # ROS2 ament package + 依赖声明
├── CMakeLists.txt                                  # 双 executable + CasADi/IPOPT 链接
├── README.md                                       # 模块入口（链接本 spec + 详细设计）
│
├── include/m5_tactical_planner/                    # public headers
│   ├── common/
│   │   ├── error.hpp                               # mass_l3::m5::ErrorCode (5000–5999)
│   │   ├── types.hpp                               # 内部类型（轨迹、约束等）
│   │   ├── units.hpp                               # 角度/速度/距离单位转换 (rad/deg/kn/m/nm)
│   │   └── time_alignment.hpp                      # 多源消息时戳对齐辅助
│   ├── shared/
│   │   ├── vessel_dynamics_model.hpp               # 4-DOF MMG（FCB 水动力插件）
│   │   ├── constraint_compiler.hpp                 # COLREGs/Behavior/ENC → CasADi 不等式
│   │   ├── cpa_calculator.hpp                      # CPA/TCPA 几何（详细设计 §5.4.1）
│   │   ├── trajectory_propagator.hpp               # 自船/目标轨迹外推
│   │   └── capability_manifest.hpp                 # FCB Capability Manifest 加载器
│   ├── mid_mpc/
│   │   ├── mid_mpc_nlp_formulation.hpp             # CasADi MX 表达式构造
│   │   ├── mid_mpc_solver.hpp                      # IPOPT 调用包装
│   │   ├── mid_mpc_waypoint_generator.hpp          # 求解结果 → AvoidancePlan
│   │   ├── mid_mpc_warm_start.hpp                  # 上一周期解作为初值
│   │   └── mid_mpc_node.hpp                        # ROS2 节点
│   └── bc_mpc/
│       ├── bc_mpc_collision_detector.hpp           # 短时域触发判据 (§5.3.2)
│       ├── bc_mpc_nlp_formulation.hpp              # 短时域 NLP / 分支树构造
│       ├── bc_mpc_solver.hpp                       # 分支树求解器（k 分支评估）
│       ├── bc_mpc_override_generator.hpp           # 求解结果 → ReactiveOverrideCmd
│       ├── bc_mpc_validity_manager.hpp             # validity_s 续命管理（§5.3.3）
│       └── bc_mpc_node.hpp                         # ROS2 节点
│
├── src/                                            # 实现源文件（与 include 镜像）
│   ├── common/
│   ├── shared/
│   ├── mid_mpc/
│   │   └── main_mid_mpc.cpp                        # executable entry: m5_mid_mpc_node
│   └── bc_mpc/
│       └── main_bc_mpc.cpp                         # executable entry: m5_bc_mpc_node
│
├── config/                                         # YAML 参数文件（HAZID 注入点）
│   ├── m5_mid_mpc_params.yaml                      # Mid-MPC 时域 / 权重 / IPOPT 选项
│   ├── m5_bc_mpc_params.yaml                       # BC-MPC 触发阈值 / 分支数 / validity
│   ├── m5_common_params.yaml                       # 共享：CPA_safe / ROT_max / 频率上限
│   └── fcb_vessel_capability.yaml                  # FCB Capability Manifest（详细设计 §13）
│
├── launch/
│   ├── m5_full.launch.py                           # 双节点一键启动
│   ├── m5_mid_mpc_only.launch.py                   # 仅 Mid-MPC（开发/单元测试）
│   ├── m5_bc_mpc_only.launch.py                    # 仅 BC-MPC（紧急接口压力测试）
│   └── m5_with_mocks.launch.py                     # 含 l3_external_msgs mock publisher
│
├── test/                                           # GTest 单元测试 + 端到端 mock 测试
│   ├── unit/
│   │   ├── test_mid_mpc_nlp_formulation.cpp
│   │   ├── test_mid_mpc_solver.cpp
│   │   ├── test_bc_mpc_collision_detector.cpp
│   │   ├── test_bc_mpc_branching_tree.cpp
│   │   ├── test_constraint_compiler.cpp
│   │   ├── test_vessel_dynamics_model.cpp
│   │   ├── test_cpa_calculator.cpp
│   │   ├── test_trajectory_propagator.cpp
│   │   └── test_capability_manifest.cpp
│   ├── integration/
│   │   ├── test_mid_mpc_node_e2e.cpp               # mock pub/sub → AvoidancePlan
│   │   ├── test_bc_mpc_node_e2e.cpp                # 触发事件 → ReactiveOverride < 150 ms
│   │   └── test_dual_node_handover.cpp             # Mid → BC → Mid 切换
│   └── fixtures/
│       ├── world_state_fixtures.cpp                # 经典场景：直行/对遇/交叉/追越
│       ├── colreg_constraint_fixtures.cpp
│       └── fcb_capability_fixture.yaml
│
├── benchmarks/                                     # 性能基准（Wave 4 集成测试用）
│   ├── bench_mid_mpc_solve_time.cpp                # 目标：< 350 ms 平均
│   └── bench_bc_mpc_response_time.cpp              # 目标：< 150 ms p99
│
└── docs/                                           # 模块内开发笔记（不进 docs/Design/）
    ├── ipopt-tuning-notes.md                       # IPOPT 选项调优记录
    └── casadi-symbolic-graph.md                    # CasADi 表达式图调试技巧
```

**关键约束**：
- **目录与命名空间一一对应**：`include/m5_tactical_planner/mid_mpc/*.hpp` ↔ `mass_l3::m5::mid_mpc::*`
- **执行 entry 仅 2 个**：`main_mid_mpc.cpp` + `main_bc_mpc.cpp`；其他 `.cpp` 都是被链接的库文件
- **测试 fixture 共享**：`test/fixtures/` 中的场景 fixture 可被 unit + integration 共用

---

## 3. ROS2 节点设计

### 3.1 双节点拓扑（两个独立可执行）

按详细设计 §1.2 + IDL 指南 §2.4，M5 部署为两个独立 ROS2 节点 + 两个独立 executable：

| 节点名 | Executable | 时间尺度 | DDS QoS profile |
|---|---|---|---|
| `m5_tactical_planner_mid_mpc` | `m5_mid_mpc_node` | 中时（1–2 Hz） | `reliable + volatile + keep_last(5)` |
| `m5_tactical_planner_bc_mpc` | `m5_bc_mpc_node` | 实时（事件 / ≤ 10 Hz） | `reliable + keep_last(50)`（紧急接口）|

**部署模式**：
- **默认**：单进程（composable container）+ rclcpp IPC 共享内存（零拷贝传递 last_plan）
- **回退**：独立进程（CycloneDDS 通信，约 100 µs 序列化延迟）
- **认证模式**（v1.2 升级）：独立进程 + RTI Connext Cert（IEC 61508 SIL 3）

### 3.2 Mid-MPC 节点（订阅 / 发布 / 求解周期 1–2 Hz）

#### 3.2.1 订阅消息（v1.1.2 §15.2）

| Topic | 类型 | QoS | 处理 |
|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | `reliable + transient_local + keep_last(10)` | 1 Hz；缺失 > 3 s → 冻结规划 |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | `best_effort + keep_last(2)` | 4 Hz；缺失 > 0.5 s → 通知 BC-MPC |
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | `reliable + keep_last(5)` | 2 Hz；缺失 > 1 s → 沿用旧约束 |
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | `reliable + keep_last(5)` | 2 Hz；缺失 > 1 s → CPA ×1.3 |
| `/l2/planned_route` | `l3_external_msgs/PlannedRoute` | `reliable + transient_local + keep_last(10)` | 1 Hz；缺失 > 5 s → 沿当前航向兜底 |
| `/l2/speed_profile` | `l3_external_msgs/SpeedProfile` | `reliable + transient_local + keep_last(10)` | 1 Hz；缺失 > 5 s → 沿用旧值 |

#### 3.2.2 发布消息

| Topic | 类型 | 频率 | QoS |
|---|---|---|---|
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | 1–2 Hz | `reliable + keep_last(5)` |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 事件 + 2 Hz | `reliable + transient_local + keep_last(50)` |
| `/l3/sat/data` | `l3_msgs/SATData` | 10 Hz | `best_effort + keep_last(2)` |

#### 3.2.3 周期任务（详细设计 §6.1）

```
T=0 ms       Wall-clock timer 触发（1 Hz / 1000 ms slot）
T=0–50 ms    时戳对齐 + 数据校验（time_alignment.hpp）
T=50–150 ms  目标轨迹外推（trajectory_propagator）+ COLREGs 约束编译
T=150–650 ms CasADi + IPOPT NLP 求解（目标 < 350 ms 平均）
T=650–680 ms 输出格式化 (mid_mpc_waypoint_generator) + ASDR 记录
T=680–700 ms 发布 AvoidancePlan via DDS
T=700–1000 ms 等待下一周期 / 更新 SAT_Data 至 M8（10 Hz 子任务）
```

### 3.3 BC-MPC 节点（订阅 / 发布 / 触发条件 / 求解周期 10 Hz）

#### 3.3.1 订阅消息

同 Mid-MPC 全部 6 个 topic + 1 个内部 topic：

| Topic | 类型 | 用途 |
|---|---|---|
| `/l3/m5/internal/last_plan` | `l3_msgs/AvoidancePlan` | 从 Mid-MPC 获取最新 plan + cost_breakdown（共享内存或内部 topic）|

#### 3.3.2 发布消息

| Topic | 类型 | 触发 | QoS |
|---|---|---|---|
| `/l3/m5/reactive_override_cmd` | `l3_msgs/ReactiveOverrideCmd` | 事件 / ≤ 10 Hz | `reliable + keep_last(50)` |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 触发事件 + 续命刷新 | `reliable + transient_local` |

#### 3.3.3 触发判据（详细设计 §5.3.2）

```cpp
// bc_mpc_collision_detector.hpp 伪代码
enum class BcTriggerReason : uint8_t {
    None = 0,
    CpaShortHorizonDegraded,       // 条件 A：Mid-MPC k=2 步 CPA < CPA_safe×0.7
    NewThreatBurst,                 // 条件 B：新目标 CPA < 0.5 nm 或 TCPA < 60 s
    TcpaCompression,                // 条件 C：TCPA < 150 s 且下降率 > 0.5 s/s
    MidMpcSolveFailed,              // 条件 D：Mid-MPC 连续 > 2 周期失败
};
```

#### 3.3.4 周期 / 续命（详细设计 §5.3.3）

- **首次触发** → 100 ms 内输出 ReactiveOverrideCmd（含 validity_s ∈ [1, 3] s）
- **续命周期**：每 100 ms 重评估当前最优分支 CPA；CPA 仍危险 → validity_s += 0.2 s（最大 3 s）
- **退出**：CPA > CPA_safe × 0.9 → 停止发布，回到 Mid-MPC 控制

### 3.4 节点间通信（共享内存 vs 内部 topic — 决策选择）

**决策**：**默认 rclcpp Intra-Process Communication (IPC)**，回退到内部 ROS2 topic。

| 维度 | rclcpp IPC（同进程） | 内部 ROS2 topic（跨进程） |
|---|---|---|
| 延迟 | < 10 µs（零拷贝）| ~ 100 µs（DDS 序列化）|
| 部署 | composable container（同 OS 进程）| 独立 OS 进程 |
| 测试 | `rclcpp::executors::SingleThreadedExecutor` 直接挂载两节点 | 真实 DDS 通路 |
| Doer-Checker 独立性影响 | 无（M5 内部独立性不强制）| 无 |
| 选择 | **默认（开发期 + 性能基准）** | 部署到不同物理机 / 认证模式时启用 |

**实现要点**：
- `mid_mpc_node` 发布 `last_plan` 到 `/l3/m5/internal/last_plan`（intra_process_comm = true）
- `bc_mpc_node` 订阅同 topic；rclcpp 检测到同进程时自动启用 IPC
- launch 文件 `m5_full.launch.py` 默认 ComposableNodeContainer 模式

### 3.5 参数（YAML — HAZID 注入点）

参数加载路径：每个节点启动时从 `config/*.yaml` 加载（ROS2 declare_parameter API）。HAZID 校准结果在 v1.1.3 通过 PR 修改 YAML，**不需要重编译**。

> 关键参数清单与 `[TBD-HAZID]` 标注详见 §9（配置文件章）。

---

## 4. 核心类定义（C++ header 骨架）

> 以下骨架是 **真实 C++17 + CasADi 3.7.2 语法**，可直接作为编码起点。`[TBD-HAZID]` 注释标识 RUN-001 校准回填点（详细设计附录 C）。

### 4.1 Mid-MPC 类族

#### 4.1.1 `MidMpcNlpFormulation`（CasADi 表达式构造）

```cpp
// include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp
#ifndef MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
#define MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_

#include <casadi/casadi.hpp>
#include <Eigen/Dense>
#include <cstdint>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"
#include "m5_tactical_planner/shared/constraint_compiler.hpp"

namespace mass_l3::m5::mid_mpc {

/**
 * @brief Mid-MPC NLP problem formulation (CasADi MX symbolic graph).
 *
 * Per detailed design §5.2.1 (objective + constraints) and §5.4.1 (CPA computation).
 * Builds the NLP once at construction; solve() is called per cycle with parameter
 * injection (no re-construction). Symbolic graph is Function-cached for IPOPT warm
 * start across cycles.
 *
 * @note Decision variables: psi[0..N-1] (heading, rad), u[0..N-1] (speed, m/s).
 * @note Constraint set adapts to ODD_StateMsg.colreg_param_set (CPA_safe varies).
 */
class MidMpcNlpFormulation {
 public:
  struct Config {
    int32_t n_horizon{12};          // [TBD-HAZID] N=12–18; FCB 停止距离 × 安全系数
    double dt_s{5.0};               // 步长 5 s（与 L4 LOS 周期对齐，详细设计 §5.2.3）
    double w_colreg{1000.0};        // [TBD-HAZID] COLREG 合规权重（最高）
    double w_dist{10.0};            // [TBD-HAZID] 航线跟踪权重
    double w_vel{1.0};              // [TBD-HAZID] 速度效率权重
    int32_t max_targets{16};        // 单周期最大目标数（参数化，影响 g 向量维度）
  };

  explicit MidMpcNlpFormulation(const Config& cfg);

  /// Build symbolic NLP graph (one-time at construction); cache as casadi::Function.
  void build_symbolic_graph();

  /// Get the cached nlpsol Function (called by MidMpcSolver per cycle).
  [[nodiscard]] const casadi::Function& solver() const noexcept { return solver_; }

  /// Pack runtime input into IPOPT parameter vector p (decision variable bounds + objective coefs).
  [[nodiscard]] casadi::DM pack_parameters(const MidMpcInput& input) const;

  /// Unpack IPOPT solution x* into trajectory + cost breakdown.
  [[nodiscard]] MidMpcSolution unpack_solution(const casadi::DM& x_opt,
                                                const casadi::Dict& stats) const;

 private:
  Config cfg_;
  casadi::MX psi_;                  // [N] decision variable: heading sequence (rad)
  casadi::MX u_;                    // [N] decision variable: speed sequence (m/s)
  casadi::MX p_;                    // parameter vector (initial state + targets + colreg + ODD)
  casadi::MX J_;                    // objective expression
  casadi::MX g_;                    // constraint vector
  casadi::Function solver_;         // IPOPT-backed nlpsol Function
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
```

#### 4.1.2 `MidMpcSolver`（IPOPT 调用包装）

```cpp
// include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp
namespace mass_l3::m5::mid_mpc {

class MidMpcSolver {
 public:
  struct IpoptOptions {
    int32_t max_iter{150};                    // [TBD-HAZID] 详细设计 §5.2.4
    double tol{1e-4};                          // [TBD-HAZID] convergence tolerance
    double timeout_s{0.5};                     // [TBD-HAZID] 500 ms wall-clock cap
    std::string linear_solver{"mumps"};        // 默认 MUMPS（开源）；ma27/ma57 商业可选
    std::string hessian_approximation{"limited-memory"};  // L-BFGS（FCB 状态空间合理）
    int32_t print_level{0};                    // 生产环境关闭 IPOPT 输出
  };

  enum class SolveStatus : uint8_t {
    Converged = 0,
    Timeout = 1,
    Infeasible = 2,
    NumericalFailure = 3,
    NotInitialized = 4,
  };

  MidMpcSolver(const MidMpcNlpFormulation& formulation, const IpoptOptions& opts);

  /**
   * @brief Solve one Mid-MPC cycle.
   * @param input Runtime input snapshot (must be time-aligned).
   * @param warm_start Previous-cycle solution; nullptr → cold start (linear extrapolation).
   * @return MidMpcSolution with x_opt, status, cost_breakdown, solve_duration_ms.
   * @note Thread-safe IFF caller serializes calls; IPOPT internal state is not reentrant.
   */
  [[nodiscard]] MidMpcSolution solve(const MidMpcInput& input,
                                      const MidMpcSolution* warm_start);

 private:
  const MidMpcNlpFormulation& formulation_;
  IpoptOptions opts_;
  casadi::Dict ipopt_dict_;          // pre-built CasADi options
  std::int64_t consecutive_failures_{0};
};

}  // namespace mass_l3::m5::mid_mpc
```

#### 4.1.3 `MidMpcWaypointGenerator`（求解结果 → AvoidancePlan）

```cpp
// include/m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp
#include "l3_msgs/msg/avoidance_plan.hpp"

namespace mass_l3::m5::mid_mpc {

class MidMpcWaypointGenerator {
 public:
  /**
   * @brief Convert MidMpcSolution into AvoidancePlanMsg (4 waypoints + speed segments).
   *
   * Per RFC-001 方案 B: waypoints[4] fixed length (Kongsberg K-Pos compatible);
   * sampling indices from MPC trajectory at evenly spaced lookahead distances.
   *
   * @param solution Optimized MPC solution (N=12–18 trajectory points).
   * @param own_ship_lat Initial latitude (WGS84 deg, from World_StateMsg).
   * @param own_ship_lon Initial longitude (WGS84 deg).
   * @return AvoidancePlanMsg with 4 GeoPoints + speed_adjustments + status + rationale.
   * @pre solution.status == SolveStatus::Converged.
   */
  [[nodiscard]] l3_msgs::msg::AvoidancePlan generate(
      const MidMpcSolution& solution,
      double own_ship_lat,
      double own_ship_lon) const;

 private:
  /// Sample 4 waypoints from N-point MPC trajectory at lookahead distances.
  /// Uses GeographicLib::Geodesic for WGS84 forward projection.
  [[nodiscard]] std::vector<geographic_msgs::msg::GeoPoint> sample_waypoints(
      const std::vector<TrajectoryPoint>& trajectory) const;

  /// Compose human-readable rationale (SAT-2 transparency).
  /// Format: "MPC converged in <ms> ms; cost J=<val> [colreg=<>, dist=<>, vel=<>];
  ///          active constraints: [<list>]"
  [[nodiscard]] std::string compose_rationale(const MidMpcSolution& solution) const;
};

}  // namespace mass_l3::m5::mid_mpc
```

#### 4.1.4 `MidMpcNode`（ROS2 节点）

```cpp
// include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp
#include <rclcpp/rclcpp.hpp>
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"

namespace mass_l3::m5::mid_mpc {

class MidMpcNode : public rclcpp::Node {
 public:
  explicit MidMpcNode(const rclcpp::NodeOptions& options);

 private:
  /// 1 Hz wall-clock timer callback (detailed design §6.1)
  void on_solve_cycle();

  /// Subscriber callbacks (snapshot to internal cache, no heavy work)
  void on_odd_state(const l3_msgs::msg::ODDState::ConstSharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::ConstSharedPtr msg);
  void on_behavior_plan(const l3_msgs::msg::BehaviorPlan::ConstSharedPtr msg);
  void on_colregs_constraint(const l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg);
  void on_planned_route(const l3_external_msgs::msg::PlannedRoute::ConstSharedPtr msg);
  void on_speed_profile(const l3_external_msgs::msg::SpeedProfile::ConstSharedPtr msg);

  /// Time alignment + validity check; populates MidMpcInput.
  /// @return std::nullopt if any required input is stale beyond tolerance (§6 timing).
  [[nodiscard]] std::optional<MidMpcInput> assemble_input();

  /// Publish AvoidancePlan + ASDRRecord + SATData
  void publish_outputs(const MidMpcSolution& solution);

  // Subscriptions
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_sub_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr behavior_sub_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr colregs_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr route_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr speed_sub_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::AvoidancePlan>::SharedPtr plan_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;

  // Internal state cache (latest snapshots; protected by std::scoped_lock if multi-threaded executor)
  std::optional<l3_msgs::msg::ODDState> latest_odd_;
  std::optional<l3_msgs::msg::WorldState> latest_world_;
  std::optional<l3_msgs::msg::BehaviorPlan> latest_behavior_;
  std::optional<l3_msgs::msg::COLREGsConstraint> latest_colregs_;
  std::optional<l3_external_msgs::msg::PlannedRoute> latest_route_;
  std::optional<l3_external_msgs::msg::SpeedProfile> latest_speed_;
  std::mutex cache_mutex_;

  // Solver components
  std::unique_ptr<MidMpcNlpFormulation> formulation_;
  std::unique_ptr<MidMpcSolver> solver_;
  std::unique_ptr<MidMpcWaypointGenerator> waypoint_generator_;

  // Warm-start state
  std::optional<MidMpcSolution> last_solution_;

  // Wall-clock timer
  rclcpp::TimerBase::SharedPtr solve_timer_;
};

}  // namespace mass_l3::m5::mid_mpc
```

### 4.2 BC-MPC 类族

#### 4.2.1 `BcMpcCollisionDetector`（短时域碰撞预警）

```cpp
// include/m5_tactical_planner/bc_mpc/bc_mpc_collision_detector.hpp
namespace mass_l3::m5::bc_mpc {

enum class BcTriggerReason : uint8_t {
  None = 0,
  CpaShortHorizonDegraded = 1,    // Mid-MPC 预测 CPA @ k=2 < CPA_safe×0.7
  NewThreatBurst = 2,              // 新目标 CPA < 0.5 nm 或 TCPA < 60 s
  TcpaCompression = 3,             // TCPA < 150 s 且下降率 > 0.5 s/s [TBD-HAZID]
  MidMpcSolveFailed = 4,           // Mid-MPC 连续 > 2 周期 failed
};

class BcMpcCollisionDetector {
 public:
  struct Thresholds {
    double cpa_safe_nm{1.0};                  // [TBD-HAZID] from M1 ODD_StateMsg
    double cpa_short_horizon_factor{0.7};     // [TBD-HAZID] 触发系数
    double new_threat_cpa_nm{0.5};            // [TBD-HAZID]
    double new_threat_tcpa_s{60.0};           // [TBD-HAZID]
    double tcpa_compression_s{150.0};         // [TBD-HAZID]
    double tcpa_compression_rate{0.5};        // [TBD-HAZID]
    int32_t mid_mpc_consecutive_fail_threshold{2};
  };

  explicit BcMpcCollisionDetector(const Thresholds& thresholds);

  /**
   * @brief Evaluate trigger conditions A–D (detailed design §5.3.2).
   * @return BcTriggerReason::None if no trigger; specific reason otherwise.
   * @note Conditions A & C require 2-cycle confirmation; conditions B & D fire immediately.
   */
  [[nodiscard]] BcTriggerReason evaluate(const BcMpcInput& input,
                                          const MidMpcSolution* last_plan);

 private:
  Thresholds thresholds_;
  int32_t condition_a_consecutive_count_{0};   // 条件 A 滑动计数
  std::optional<double> last_tcpa_s_;          // 用于条件 C 趋势
  int32_t mid_mpc_failure_count_{0};
};

}  // namespace mass_l3::m5::bc_mpc
```

#### 4.2.2 `BcMpcNlpFormulation`（短时域 NLP / 分支树构造）

```cpp
namespace mass_l3::m5::bc_mpc {

/**
 * @brief BC-MPC branching tree formulation (Eriksen et al. 2020, [R20]).
 *
 * Generates k candidate headings (k ∈ {5, 7} from urgency) and evaluates
 * worst-case CPA over target intent uncertainty distribution.
 *
 * @note This is NOT a single CasADi NLP; it's a branching tree solved by
 *       discrete enumeration. CasADi MX is used for fast vectorized CPA
 *       evaluation per branch.
 */
class BcMpcNlpFormulation {
 public:
  struct Config {
    int32_t k_branches_high_urgency{7};      // [TBD-HAZID]
    int32_t k_branches_medium_urgency{5};    // [TBD-HAZID]
    double heading_range_high_deg{30.0};     // [TBD-HAZID]
    double heading_range_medium_deg{20.0};   // [TBD-HAZID]
    double horizon_s{20.0};                  // [TBD-HAZID] 10–30 s
    double dt_s{2.0};                         // 粗粒度（加快求解）
    double urgency_threshold{0.7};           // [TBD-HAZID]
  };

  explicit BcMpcNlpFormulation(const Config& cfg);

  /// Build vectorized CPA evaluation Function (CasADi MX → DM).
  void build_symbolic_graph();

  /// Generate k candidate headings symmetric around current heading.
  [[nodiscard]] std::vector<double> generate_candidate_headings(
      double current_heading_rad, double urgency_level) const;

  /// Vectorized CPA evaluation for one branch over all targets × intents.
  [[nodiscard]] double evaluate_branch_worst_case_cpa(
      double psi_candidate_rad,
      const BcMpcInput& input) const;

 private:
  Config cfg_;
  casadi::Function cpa_eval_func_;        // (psi, targets, intents) → min_cpa
};

}  // namespace mass_l3::m5::bc_mpc
```

#### 4.2.3 `BcMpcSolver`（分支树求解器）

```cpp
namespace mass_l3::m5::bc_mpc {

class BcMpcSolver {
 public:
  enum class Status : uint8_t {
    Resolved = 0,                  // CPA recovered → no override needed
    OverrideRequested = 1,
    SolveFailed = 2,
  };

  struct Solution {
    Status status{Status::Resolved};
    double optimal_heading_rad{0.0};
    double optimal_speed_mps{0.0};
    double rot_cmd_rad_s{0.0};
    double worst_case_cpa_m{0.0};
    double validity_s{0.0};        // 1–3 s typical
    double confidence{0.0};
    BcTriggerReason trigger_reason{BcTriggerReason::None};
    std::int64_t solve_duration_us{0};
  };

  explicit BcMpcSolver(const BcMpcNlpFormulation& formulation);

  [[nodiscard]] Solution solve(const BcMpcInput& input, BcTriggerReason reason);

 private:
  const BcMpcNlpFormulation& formulation_;
};

}  // namespace mass_l3::m5::bc_mpc
```

#### 4.2.4 `BcMpcOverrideGenerator`（求解结果 → ReactiveOverrideCmd）

```cpp
#include "l3_msgs/msg/reactive_override_cmd.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcOverrideGenerator {
 public:
  /**
   * @brief Pack BcMpcSolver::Solution into ReactiveOverrideCmd.
   *
   * Per v1.1.2 §15.1 + RFC-001 紧急接口 schema.
   * @pre solution.status == OverrideRequested.
   */
  [[nodiscard]] l3_msgs::msg::ReactiveOverrideCmd generate(
      const BcMpcSolver::Solution& solution,
      const rclcpp::Time& trigger_time) const;

 private:
  /// Map BcTriggerReason enum to canonical trigger_reason string per IDL.
  /// "CPA_EMERGENCY" | "COLLISION_IMMINENT" | "MID_MPC_FAILURE" | "NEW_THREAT_BURST"
  [[nodiscard]] std::string map_trigger_reason(BcTriggerReason reason) const;
};

}  // namespace mass_l3::m5::bc_mpc
```

#### 4.2.5 `BcMpcValidityManager`（validity_s 续命管理）

```cpp
namespace mass_l3::m5::bc_mpc {

class BcMpcValidityManager {
 public:
  struct Config {
    double min_validity_s{1.0};        // [TBD-HAZID]
    double max_validity_s{3.0};        // [TBD-HAZID]
    double refresh_period_s{0.1};      // 100 ms (10 Hz cap)
    double cpa_safe_factor_extend{0.5};  // CPA < safe×0.5 → extend
    double cpa_safe_factor_exit{0.9};    // CPA > safe×0.9 → exit
  };

  explicit BcMpcValidityManager(const Config& cfg);

  /// Update internal state on each refresh tick (10 Hz).
  /// @return true if BC-MPC should continue publishing; false if exit.
  [[nodiscard]] bool tick(double current_worst_cpa_m, double cpa_safe_m);

  [[nodiscard]] double remaining_validity_s() const noexcept { return remaining_validity_s_; }

  void reset(double initial_validity_s) noexcept;

 private:
  Config cfg_;
  double remaining_validity_s_{0.0};
  bool active_{false};
};

}  // namespace mass_l3::m5::bc_mpc
```

#### 4.2.6 `BcMpcNode`（ROS2 节点）

```cpp
namespace mass_l3::m5::bc_mpc {

class BcMpcNode : public rclcpp::Node {
 public:
  explicit BcMpcNode(const rclcpp::NodeOptions& options);

 private:
  /// Triggered by World_StateMsg arrival (event-driven, ~4 Hz from M2).
  /// Evaluates collision detector; if triggered, runs BC-MPC solver and publishes.
  void on_world_state(const l3_msgs::msg::WorldState::ConstSharedPtr msg);

  /// Snapshot from sibling Mid-MPC node (intra-process or topic).
  void on_internal_last_plan(const l3_msgs::msg::AvoidancePlan::ConstSharedPtr msg);

  /// 100 ms timer callback — manages validity_s decay + republish if active.
  void on_validity_tick();

  // Subscriptions (same set as Mid-MPC + internal last_plan)
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_sub_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;
  // ... other subs ...
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr internal_plan_sub_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr override_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;

  // Solver components
  std::unique_ptr<BcMpcCollisionDetector> detector_;
  std::unique_ptr<BcMpcNlpFormulation> formulation_;
  std::unique_ptr<BcMpcSolver> solver_;
  std::unique_ptr<BcMpcOverrideGenerator> override_gen_;
  std::unique_ptr<BcMpcValidityManager> validity_mgr_;

  // Active branch state (for tick refresh)
  std::optional<BcMpcSolver::Solution> active_solution_;
  std::mutex solution_mutex_;

  // Validity tick timer (10 Hz)
  rclcpp::TimerBase::SharedPtr validity_timer_;
};

}  // namespace mass_l3::m5::bc_mpc
```

### 4.3 共享类（Mid-MPC + BC-MPC 共用）

#### 4.3.1 `VesselDynamicsModel`（4-DOF MMG）

```cpp
// include/m5_tactical_planner/shared/vessel_dynamics_model.hpp
namespace mass_l3::m5::shared {

/**
 * @brief 4-DOF MMG vessel dynamics model (Yasukawa & Yoshimura 2015 [R7]).
 *
 * Decision-core code is vessel-agnostic; FCB-specific hydrodynamic coefficients
 * are loaded from CapabilityManifest at startup (Backseat Driver pattern, ADR-001).
 *
 * @note Used by both Mid-MPC (trajectory propagation) and BC-MPC (branch evaluation).
 * @note Speed-dependent ROT_max: high-speed rudder efficiency reduction (§5.4.2).
 */
class VesselDynamicsModel {
 public:
  struct State {
    double x_m{0.0};
    double y_m{0.0};
    double psi_rad{0.0};
    double u_mps{0.0};       // surge
    double v_mps{0.0};       // sway
    double r_rad_s{0.0};     // yaw rate
  };

  struct Input {
    double rudder_rad{0.0};
    double rpm_rps{0.0};
  };

  explicit VesselDynamicsModel(const CapabilityManifest& manifest);

  /// One-step Euler / RK4 integration (configurable).
  [[nodiscard]] State step(const State& s0, const Input& cmd, double dt_s) const;

  /// Speed-dependent ROT_max with rough-sea correction (§5.4.2).
  /// @param u_mps Current surge speed.
  /// @param hs_m Significant wave height (Hs); 0 = calm.
  [[nodiscard]] double rot_max_rad_s(double u_mps, double hs_m) const;

  /// Stopping distance (FCB calibrated curve; HAZID input for N_horizon sizing).
  [[nodiscard]] double stopping_distance_m(double u_mps) const;

 private:
  CapabilityManifest manifest_;       // FCB hydrodynamic coefficients
};

}  // namespace mass_l3::m5::shared
```

#### 4.3.2 `ConstraintCompiler`（COLREGs/Behavior/ENC → CasADi 不等式）

```cpp
namespace mass_l3::m5::shared {

/**
 * @brief Compile high-level constraints (COLREGs rules / behavior bounds / ENC zones / TSS lanes)
 *        into CasADi MX inequality expressions for inclusion in MPC NLP.
 *
 * Per detailed design §5.2.4 + §5.4.3 (TSS Rule 10 polygon constraint).
 *
 * @note Pure compiler: produces MX symbolic; runtime parameter values injected by
 *       MidMpcNlpFormulation::pack_parameters().
 */
class ConstraintCompiler {
 public:
  struct CompiledConstraints {
    casadi::MX g;                    // stacked inequality vector (g >= 0)
    std::vector<std::string> names;  // for active-set logging (SAT-2)
  };

  /// Compile all constraints for a given trajectory (psi, u sequences).
  [[nodiscard]] CompiledConstraints compile(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ConstraintInputs& inputs) const;

  /// Decompose non-convex TSS lane polygon into convex sub-polygons (§5.4.3).
  /// Uses Boost.Geometry triangulation.
  [[nodiscard]] std::vector<Polygon2D> decompose_polygon(const Polygon2D& lane) const;

 private:
  /// Half-plane constraints for convex polygon (point inside test).
  [[nodiscard]] casadi::MX point_inside_convex(
      const casadi::MX& point_xy,
      const Polygon2D& polygon) const;
};

}  // namespace mass_l3::m5::shared
```

#### 4.3.3 `CpaCalculator` + `TrajectoryPropagator` + `CapabilityManifest`

```cpp
// CpaCalculator: §5.4.1 closed-form CPA/TCPA
namespace mass_l3::m5::shared {

class CpaCalculator {
 public:
  struct CpaResult {
    double cpa_m{0.0};
    double tcpa_s{0.0};
    double uncertainty_m{0.0};
  };

  /// Closed-form CPA for linear-motion own-ship/target pair.
  /// @pre Time-aligned states (own.stamp == target.stamp).
  [[nodiscard]] static CpaResult compute_linear(
      const VesselDynamicsModel::State& own,
      const VesselDynamicsModel::State& target);

  /// Trajectory-based CPA (sampled along discretized trajectories).
  [[nodiscard]] static CpaResult compute_trajectory(
      const std::vector<Eigen::Vector2d>& own_traj,
      const std::vector<Eigen::Vector2d>& target_traj,
      double dt_s);
};

// TrajectoryPropagator: forward-extrapolate own / target states.
class TrajectoryPropagator {
 public:
  [[nodiscard]] std::vector<VesselDynamicsModel::State> propagate_own(
      const VesselDynamicsModel::State& s0,
      const std::vector<double>& psi_seq,
      const std::vector<double>& u_seq,
      double dt_s,
      const VesselDynamicsModel& dynamics) const;

  /// Predict target trajectory under given intent scenario (BC-MPC).
  [[nodiscard]] std::vector<Eigen::Vector2d> propagate_target(
      const TargetState& tgt,
      TargetIntent intent,
      double horizon_s,
      double dt_s) const;
};

// CapabilityManifest: FCB hydrodynamic coefficients loader (Backseat Driver).
class CapabilityManifest {
 public:
  struct Config {
    double mass_kg{0.0};
    double length_m{0.0};
    double rot_max_at_18kn_rad_s{0.0};        // [TBD-HAZID] FCB pool test
    double stopping_distance_at_18kn_m{0.0};   // [TBD-HAZID]
    // ... more MMG coefficients per Yasukawa 2015 [R7] ...
  };

  [[nodiscard]] static CapabilityManifest load_from_yaml(const std::string& yaml_path);
  [[nodiscard]] const Config& config() const noexcept { return config_; }

 private:
  Config config_;
};

}  // namespace mass_l3::m5::shared
```

---

## 5. CasADi + IPOPT 集成

### 5.1 CMake `find_package` + 链接（参考 `third-party-library-policy.md` §5）

```cmake
# CMakeLists.txt 关键片段
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/casadi-3.7.2/install)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/ipopt-3.14.19/install)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/geographiclib-2.7/install)

find_package(casadi 3.7.2 EXACT REQUIRED)
find_package(IPOPT 3.14.19 EXACT REQUIRED)
find_package(GeographicLib 2.7 EXACT REQUIRED)
find_package(Eigen3 5.0.0 EXACT REQUIRED NO_MODULE)
find_package(Boost 1.91 EXACT REQUIRED COMPONENTS headers)
find_package(spdlog 1.17.0 EXACT REQUIRED)

# Mid-MPC executable
add_executable(m5_mid_mpc_node
    src/mid_mpc/main_mid_mpc.cpp
    src/mid_mpc/mid_mpc_node.cpp
    src/mid_mpc/mid_mpc_nlp_formulation.cpp
    src/mid_mpc/mid_mpc_solver.cpp
    src/mid_mpc/mid_mpc_waypoint_generator.cpp
    src/shared/vessel_dynamics_model.cpp
    src/shared/constraint_compiler.cpp
    src/shared/cpa_calculator.cpp
    src/shared/trajectory_propagator.cpp
    src/shared/capability_manifest.cpp
    src/common/time_alignment.cpp
)

target_link_libraries(m5_mid_mpc_node PUBLIC
    casadi                # LGPL-3.0 dynamic linking only
    ${IPOPT_LIBRARIES}    # EPL-2.0
    GeographicLib::GeographicLib
    Eigen3::Eigen
    Boost::headers
    spdlog::spdlog
)

ament_target_dependencies(m5_mid_mpc_node
    rclcpp
    l3_msgs
    l3_external_msgs
    geographic_msgs
    geometry_msgs
    builtin_interfaces
)

# BC-MPC executable (separate)
add_executable(m5_bc_mpc_node
    src/bc_mpc/main_bc_mpc.cpp
    src/bc_mpc/bc_mpc_node.cpp
    src/bc_mpc/bc_mpc_collision_detector.cpp
    src/bc_mpc/bc_mpc_nlp_formulation.cpp
    src/bc_mpc/bc_mpc_solver.cpp
    src/bc_mpc/bc_mpc_override_generator.cpp
    src/bc_mpc/bc_mpc_validity_manager.cpp
    src/shared/vessel_dynamics_model.cpp
    src/shared/cpa_calculator.cpp
    src/shared/trajectory_propagator.cpp
    src/shared/capability_manifest.cpp
)

target_link_libraries(m5_bc_mpc_node PUBLIC
    casadi
    GeographicLib::GeographicLib
    Eigen3::Eigen
    Boost::headers
    spdlog::spdlog
)
ament_target_dependencies(m5_bc_mpc_node
    rclcpp l3_msgs l3_external_msgs geographic_msgs geometry_msgs builtin_interfaces
)
```

> **License 合规**：CasADi (LGPL-3.0) **必须动态链接**，禁用 `-static`。`tools/ci/sbom-license-check.sh` 在 release 阶段验证（`third-party-library-policy.md` §4.3）。

### 5.2 NLP 构造模式（CasADi function vs MX 表达式）

**模式选择**：**One-shot symbolic graph + parameter injection**（构造期一次性 `nlpsol`，运行期仅注入参数）。

```cpp
// mid_mpc_nlp_formulation.cpp 核心片段
void MidMpcNlpFormulation::build_symbolic_graph() {
  using namespace casadi;
  const int32_t N = cfg_.n_horizon;

  // 决策变量
  psi_ = MX::sym("psi", N);
  u_ = MX::sym("u", N);
  MX x = MX::vertcat({psi_, u_});

  // 参数（运行期注入）：初始状态 + 目标列表 + COLREG 约束 + ODD CPA_safe + 权重
  // 维度固定（max_targets 上界），未使用槽位填零 + mask
  p_ = MX::sym("p", parameter_dim_());

  // 目标函数
  MX J_colreg = build_colreg_cost_(psi_, u_, p_);
  MX J_dist = build_distance_cost_(psi_, u_, p_);
  MX J_vel = build_velocity_cost_(u_, p_);
  J_ = cfg_.w_colreg * J_colreg + cfg_.w_dist * J_dist + cfg_.w_vel * J_vel;

  // 约束 g >= 0
  ConstraintCompiler compiler;
  auto compiled = compiler.compile(psi_, u_, /* inputs derived from p_ */);
  g_ = compiled.g;

  // 创建 NLP
  MXDict nlp = {{"x", x}, {"p", p_}, {"f", J_}, {"g", g_}};

  Dict opts;
  opts["ipopt.max_iter"] = 150;                 // [TBD-HAZID]
  opts["ipopt.tol"] = 1e-4;                     // [TBD-HAZID]
  opts["ipopt.print_level"] = 0;
  opts["ipopt.linear_solver"] = "mumps";
  opts["ipopt.hessian_approximation"] = "limited-memory";  // L-BFGS
  opts["ipopt.max_cpu_time"] = 0.45;            // 450 ms safety margin under 500 ms SLA
  opts["print_time"] = 0;

  solver_ = nlpsol("mid_mpc_solver", "ipopt", nlp, opts);
}
```

### 5.3 IPOPT 求解器配置

| 选项 | 值 | 理由 |
|---|---|---|
| `max_iter` | 150 [TBD-HAZID] | early termination 防超时；详细设计 §6.3 |
| `tol` | 1e-4 [TBD-HAZID] | 工程精度（位置 < 10 m / 角度 < 0.6°）|
| `linear_solver` | `mumps`（默认） | 开源；`ma27/ma57` 商业可选（HSL 库）|
| `hessian_approximation` | `limited-memory` | L-BFGS：FCB 状态空间 ~ 30 维，二阶 Hessian 计算贵 |
| `max_cpu_time` | 0.45 s | 留 50 ms 余量给输出格式化 + DDS 发布 |
| `print_level` | 0 | 生产环境关闭 stdout |

### 5.4 求解失败处理（infeasible → fallback to standby）

```cpp
// mid_mpc_solver.cpp 核心片段
MidMpcSolution MidMpcSolver::solve(const MidMpcInput& input,
                                    const MidMpcSolution* warm_start) {
  using namespace casadi;
  const auto t_start = std::chrono::steady_clock::now();

  // Pack parameters
  DM p_val = formulation_.pack_parameters(input);

  // Warm start: previous solution OR linear extrapolation
  DM x0_val = warm_start
      ? pack_warm_start_(*warm_start)
      : pack_cold_start_(input);  // 直线外推 (ROT=0)

  // Solve
  DMDict arg = {{"x0", x0_val}, {"p", p_val},
                {"lbg", -DM::inf()}, {"ubg", DM::zeros(g_dim_())}};
  DMDict res;
  try {
    res = formulation_.solver()(arg);
  } catch (const std::exception& e) {
    spdlog::error("[M5][MidMPC] IPOPT threw: {}", e.what());
    consecutive_failures_++;
    return MidMpcSolution{.status = SolveStatus::NumericalFailure};
  }

  // Parse stats
  const Dict stats = formulation_.solver().stats();
  const std::string ipopt_status = stats.at("return_status").as_string();
  const auto t_end = std::chrono::steady_clock::now();
  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

  MidMpcSolution sol = formulation_.unpack_solution(res.at("x"), stats);
  sol.solve_duration_ms = static_cast<int32_t>(duration_ms);

  if (ipopt_status == "Solve_Succeeded") {
    sol.status = SolveStatus::Converged;
    consecutive_failures_ = 0;
  } else if (ipopt_status == "Maximum_Iterations_Exceeded" ||
             ipopt_status == "Maximum_CpuTime_Exceeded") {
    sol.status = SolveStatus::Timeout;
    consecutive_failures_++;
  } else if (ipopt_status == "Infeasible_Problem_Detected") {
    sol.status = SolveStatus::Infeasible;
    consecutive_failures_++;
    // FM-2: collision unavoidable → request M7 MRM via SafetyAlert
    spdlog::critical("[M5][MidMPC] Infeasible: collision unavoidable; M7 MRM expected");
  } else {
    sol.status = SolveStatus::NumericalFailure;
    consecutive_failures_++;
  }

  return sol;
}
```

**降级路径**（详细设计 §7.1）：
- 单次 timeout / infeasible → 重用上一可行解，发布 `status=DEGRADED`
- 连续 > 5 周期失败 → 升级 CRITICAL，触发 M7 MRM-02

---

## 6. CMakeLists.txt（双 executable + CasADi/IPOPT 依赖）

```cmake
cmake_minimum_required(VERSION 3.22)
project(m5_tactical_planner VERSION 1.0.0 LANGUAGES CXX)

# ===== C++17 + 编译选项（继承 coding-standards.md §2）=====
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ===== 第三方库（version-locked，参考 third-party-library-policy.md §5）=====
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/casadi-3.7.2/install)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/ipopt-3.14.19/install)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/../../tools/third-party/geographiclib-2.7/install)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

find_package(casadi 3.7.2 EXACT REQUIRED)
find_package(IPOPT 3.14.19 EXACT REQUIRED)
find_package(GeographicLib 2.7 EXACT REQUIRED)
find_package(Eigen3 5.0.0 EXACT REQUIRED NO_MODULE)
find_package(Boost 1.91 EXACT REQUIRED COMPONENTS headers)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)

# ===== 编译选项（PATH-D 强制）=====
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wconversion -Wsign-conversion
    -Wcast-align -Wcast-qual
    -Wold-style-cast -Wzero-as-null-pointer-constant
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2 -Wformat-security
    -Wmissing-declarations
    -Wundef -Wunused
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)
# Release: -O2 (NOT -O3, per coding-standards §2.2)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Debug build: ASan + UBSan (per coding-standards §2.2)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# ===== 包含目录 =====
include_directories(include)

# ===== 共享库（同时被两个 executable 链接）=====
add_library(m5_shared_lib STATIC
    src/common/error.cpp
    src/common/types.cpp
    src/common/units.cpp
    src/common/time_alignment.cpp
    src/shared/vessel_dynamics_model.cpp
    src/shared/constraint_compiler.cpp
    src/shared/cpa_calculator.cpp
    src/shared/trajectory_propagator.cpp
    src/shared/capability_manifest.cpp
)
target_link_libraries(m5_shared_lib PUBLIC
    casadi
    GeographicLib::GeographicLib
    Eigen3::Eigen
    Boost::headers
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)
ament_target_dependencies(m5_shared_lib
    rclcpp builtin_interfaces geometry_msgs geographic_msgs
    l3_msgs l3_external_msgs
)

# ===== Mid-MPC executable =====
add_executable(m5_mid_mpc_node
    src/mid_mpc/main_mid_mpc.cpp
    src/mid_mpc/mid_mpc_node.cpp
    src/mid_mpc/mid_mpc_nlp_formulation.cpp
    src/mid_mpc/mid_mpc_solver.cpp
    src/mid_mpc/mid_mpc_waypoint_generator.cpp
    src/mid_mpc/mid_mpc_warm_start.cpp
)
target_link_libraries(m5_mid_mpc_node PUBLIC
    m5_shared_lib
    ${IPOPT_LIBRARIES}    # EPL-2.0
)
ament_target_dependencies(m5_mid_mpc_node
    rclcpp l3_msgs l3_external_msgs
)

# ===== BC-MPC executable =====
add_executable(m5_bc_mpc_node
    src/bc_mpc/main_bc_mpc.cpp
    src/bc_mpc/bc_mpc_node.cpp
    src/bc_mpc/bc_mpc_collision_detector.cpp
    src/bc_mpc/bc_mpc_nlp_formulation.cpp
    src/bc_mpc/bc_mpc_solver.cpp
    src/bc_mpc/bc_mpc_override_generator.cpp
    src/bc_mpc/bc_mpc_validity_manager.cpp
)
target_link_libraries(m5_bc_mpc_node PUBLIC
    m5_shared_lib
    # NOTE: BC-MPC does not link IPOPT (uses CasADi MX-only branching tree)
)
ament_target_dependencies(m5_bc_mpc_node
    rclcpp l3_msgs l3_external_msgs
)

# ===== 安装 =====
install(TARGETS m5_mid_mpc_node m5_bc_mpc_node
    RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY launch config
    DESTINATION share/${PROJECT_NAME}
)

# ===== 单元测试（GTest）=====
if(BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)
    find_package(ament_lint_auto REQUIRED)
    ament_lint_auto_find_test_dependencies()

    ament_add_gtest(test_mid_mpc_nlp_formulation
        test/unit/test_mid_mpc_nlp_formulation.cpp)
    target_link_libraries(test_mid_mpc_nlp_formulation m5_shared_lib)

    ament_add_gtest(test_bc_mpc_collision_detector
        test/unit/test_bc_mpc_collision_detector.cpp)
    target_link_libraries(test_bc_mpc_collision_detector m5_shared_lib)

    ament_add_gtest(test_constraint_compiler
        test/unit/test_constraint_compiler.cpp)
    target_link_libraries(test_constraint_compiler m5_shared_lib)

    ament_add_gtest(test_vessel_dynamics_model
        test/unit/test_vessel_dynamics_model.cpp)
    target_link_libraries(test_vessel_dynamics_model m5_shared_lib)

    ament_add_gtest(test_cpa_calculator
        test/unit/test_cpa_calculator.cpp)
    target_link_libraries(test_cpa_calculator m5_shared_lib)

    # Integration tests (rclcpp-aware)
    ament_add_gtest(test_mid_mpc_node_e2e test/integration/test_mid_mpc_node_e2e.cpp)
    target_link_libraries(test_mid_mpc_node_e2e m5_shared_lib)
    ament_target_dependencies(test_mid_mpc_node_e2e rclcpp l3_msgs l3_external_msgs)

    ament_add_gtest(test_bc_mpc_node_e2e test/integration/test_bc_mpc_node_e2e.cpp)
    target_link_libraries(test_bc_mpc_node_e2e m5_shared_lib)
    ament_target_dependencies(test_bc_mpc_node_e2e rclcpp l3_msgs l3_external_msgs)
endif()

ament_package()
```

---

## 7. package.xml

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>m5_tactical_planner</name>
  <version>1.0.0</version>
  <description>
    M5 Tactical Planner: dual-layer MPC (Mid-MPC + BC-MPC) for L3 collision avoidance.
    Per architecture v1.1.2 §10 + RFC-001 (M5 ↔ L4 dual interface).
  </description>
  <maintainer email="team-m5@mass-l3.example">Team-M5</maintainer>
  <license>Proprietary (CCS i-Ship submission)</license>
  <author>Team-M5</author>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>builtin_interfaces</depend>
  <depend>geometry_msgs</depend>
  <depend>geographic_msgs</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>

  <!-- Third-party (managed via tools/third-party/, see third-party-library-policy.md §5) -->
  <depend>casadi</depend>
  <depend>libipopt-dev</depend>
  <depend>libgeographiclib-dev</depend>
  <depend>eigen</depend>
  <depend>libboost-dev</depend>
  <depend>spdlog</depend>
  <depend>yaml-cpp</depend>

  <test_depend>ament_cmake_gtest</test_depend>
  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_lint_common</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 8. 单元测试骨架（GTest）

> 覆盖率目标 **≥ 90%**（由 lcov / gcovr 报告，PATH-D DoD per `00-implementation-master-plan.md` §5.2）。

### 8.1 `MidMpcNlpFormulation`（求解经典场景）

```cpp
// test/unit/test_mid_mpc_nlp_formulation.cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

using namespace mass_l3::m5::mid_mpc;

class MidMpcNlpTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MidMpcNlpFormulation::Config cfg;
    cfg.n_horizon = 12;
    cfg.dt_s = 5.0;
    formulation_ = std::make_unique<MidMpcNlpFormulation>(cfg);
    formulation_->build_symbolic_graph();

    MidMpcSolver::IpoptOptions opts;
    opts.max_iter = 150;
    opts.tol = 1e-4;
    solver_ = std::make_unique<MidMpcSolver>(*formulation_, opts);
  }
  std::unique_ptr<MidMpcNlpFormulation> formulation_;
  std::unique_ptr<MidMpcSolver> solver_;
};

// 场景 1：直行（无目标，应平滑跟踪 PlannedRoute）
TEST_F(MidMpcNlpTest, StraightLineNoTargets) {
  MidMpcInput input = make_straight_line_input();  // fixture
  auto sol = solver_->solve(input, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(sol.solve_duration_ms, 350);
  // 期望：航向序列单调，最大变化 < 1 度
  EXPECT_LT(max_heading_delta_deg(sol), 1.0);
}

// 场景 2：迎面让路（COLREG Rule 14 — 双方向右）
TEST_F(MidMpcNlpTest, HeadOnGiveWayRightTurn) {
  MidMpcInput input = make_head_on_input();
  auto sol = solver_->solve(input, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  // 期望：最终航向偏转 > 30 度（向右）
  EXPECT_GT(final_heading_change_deg(sol), 30.0);
  EXPECT_GE(min_cpa_along_traj_nm(sol, input.targets), 1.0);  // CPA_safe
}

// 场景 3：交叉让路（COLREG Rule 15 — give-way 转向 + 减速）
TEST_F(MidMpcNlpTest, CrossingGiveWay) {
  MidMpcInput input = make_crossing_give_way_input();
  auto sol = solver_->solve(input, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GE(min_cpa_along_traj_nm(sol, input.targets), 1.0);
}

// 场景 4：无可行解（碰撞不可避免 — Infeasible 路径）
TEST_F(MidMpcNlpTest, InfeasibleProblem) {
  MidMpcInput input = make_infeasible_input();   // 多目标围堵
  auto sol = solver_->solve(input, nullptr);
  EXPECT_TRUE(sol.status == MidMpcSolver::SolveStatus::Infeasible ||
              sol.status == MidMpcSolver::SolveStatus::Timeout);
  // 期望：不崩溃；返回明确 status；ASDR 记录 g 全部活跃
}

// 场景 5：Warm start 收敛加速
TEST_F(MidMpcNlpTest, WarmStartFasterThanColdStart) {
  MidMpcInput input = make_crossing_give_way_input();
  auto cold = solver_->solve(input, nullptr);
  auto warm = solver_->solve(input, &cold);
  EXPECT_LT(warm.solve_duration_ms, cold.solve_duration_ms);
}
```

### 8.2 `BcMpcCollisionDetector`

```cpp
// test/unit/test_bc_mpc_collision_detector.cpp
TEST(BcMpcCollisionDetectorTest, ConditionA_RequiresTwoCycles) {
  BcMpcCollisionDetector::Thresholds th;
  th.cpa_safe_nm = 1.0;
  th.cpa_short_horizon_factor = 0.7;
  BcMpcCollisionDetector det(th);

  BcMpcInput in = make_input_with_cpa_short_horizon(0.6);  // < 0.7
  EXPECT_EQ(det.evaluate(in, nullptr), BcTriggerReason::None);  // cycle 1
  EXPECT_EQ(det.evaluate(in, nullptr), BcTriggerReason::CpaShortHorizonDegraded);  // cycle 2
}

TEST(BcMpcCollisionDetectorTest, ConditionB_FiresImmediately) {
  // ...
}

TEST(BcMpcCollisionDetectorTest, ConditionD_OnConsecutiveSolveFailures) {
  // ...
}
```

### 8.3 `ConstraintCompiler`（COLREGs Rule 14/15/17 约束生成）

```cpp
// test/unit/test_constraint_compiler.cpp
TEST(ConstraintCompilerTest, Rule14_HeadOn_BothShallTurnRight) {
  ConstraintInputs in = make_head_on_constraint();
  ConstraintCompiler compiler;
  auto compiled = compiler.compile(/* psi, u */, in);
  // 验证 g 中包含 "rule_14_starboard_turn" 命名约束
  EXPECT_TRUE(std::find(compiled.names.begin(), compiled.names.end(),
                         "rule_14_starboard_turn") != compiled.names.end());
}

TEST(ConstraintCompilerTest, Rule15_Crossing_GiveWayShallTurnStarboard) { /* ... */ }
TEST(ConstraintCompilerTest, Rule17_StandOn_SmallMotionBound) { /* ... */ }
TEST(ConstraintCompilerTest, NonConvexTSSPolygon_Decomposed) { /* ... */ }
```

### 8.4 `VesselDynamicsModel`（FCB 制动 / 转弯半径）

```cpp
// test/unit/test_vessel_dynamics_model.cpp
TEST(VesselDynamicsModelTest, RotMaxLowSpeedHigher) {
  auto manifest = CapabilityManifest::load_from_yaml("test/fixtures/fcb_capability_fixture.yaml");
  VesselDynamicsModel model(manifest);
  EXPECT_GT(model.rot_max_rad_s(/* u_mps= */ 5.0, 0.0),
            model.rot_max_rad_s(/* u_mps= */ 12.0, 0.0));
  // Yasukawa 2015 [R7]：低速段舵效高
}

TEST(VesselDynamicsModelTest, RoughSeaReducesRotMax) {
  auto manifest = CapabilityManifest::load_from_yaml("test/fixtures/fcb_capability_fixture.yaml");
  VesselDynamicsModel model(manifest);
  const double calm = model.rot_max_rad_s(9.0, 0.0);
  const double rough = model.rot_max_rad_s(9.0, 2.5);  // Hs=2.5 m
  EXPECT_NEAR(rough / calm, 1.0 - 0.1 * 2.5, 0.05);  // §5.4.2 公式
}

TEST(VesselDynamicsModelTest, StoppingDistanceMonotonicInSpeed) { /* ... */ }
```

### 8.5 端到端 mock 测试（World_State + Behavior_Plan + COLREGs_Constraint → AvoidancePlan）

```cpp
// test/integration/test_mid_mpc_node_e2e.cpp
TEST(MidMpcNodeE2ETest, EndToEndMockPubSubProducesValidAvoidancePlan) {
  auto context = std::make_shared<rclcpp::Context>();
  context->init(0, nullptr);
  rclcpp::NodeOptions opts;
  opts.context(context);

  auto mid_node = std::make_shared<MidMpcNode>(opts);
  auto mock_pub = std::make_shared<rclcpp::Node>("mock_publisher", opts);

  auto world_pub = mock_pub->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SensorDataQoS().keep_last(2));
  auto behavior_pub = mock_pub->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", rclcpp::QoS(5).reliable());
  // ... other mock publishers ...

  std::optional<l3_msgs::msg::AvoidancePlan> received;
  auto sub = mock_pub->create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", rclcpp::QoS(5).reliable(),
      [&](l3_msgs::msg::AvoidancePlan::SharedPtr msg) { received = *msg; });

  // Publish mock fixture data
  publish_crossing_scenario(world_pub, behavior_pub, /* etc. */);

  // Spin both nodes for 2 cycles (2 s)
  rclcpp::executors::SingleThreadedExecutor exec(rclcpp::ExecutorOptions(), 0, false, context);
  exec.add_node(mid_node);
  exec.add_node(mock_pub);
  for (int i = 0; i < 200; ++i) {
    exec.spin_some(std::chrono::milliseconds(10));
  }

  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->waypoints.size(), 4u);   // RFC-001 4-WP fixed
  EXPECT_GT(received->confidence, 0.5f);
  EXPECT_FALSE(received->rationale.empty());

  context->shutdown();
}
```

---

## 9. 配置文件

> 所有 `[TBD-HAZID]` 标注的参数将在 RUN-001 校准（2026-08-19）后通过 PR 修改 YAML，**不需要重编译**。详细设计附录 C 列出 8 个核心校准参数。

### 9.1 `config/m5_mid_mpc_params.yaml`

```yaml
# Mid-MPC 节点参数（PATH-D / [TBD-HAZID] 标注 RUN-001 阻塞参数）
# Per detailed design §5.2.3 + RFC-001 方案 B
m5_tactical_planner_mid_mpc:
  ros__parameters:
    # ===== 时域 =====
    n_horizon: 12              # [TBD-HAZID] 12–18 (FCB 停止距离 × 安全系数)
    dt_s: 5.0                  # 与 L4 LOS 周期对齐

    # ===== 目标函数权重 =====
    w_colreg: 1000.0           # [TBD-HAZID] COLREG 合规（最高优先级）
    w_distance: 10.0           # [TBD-HAZID] 航线跟踪
    w_velocity: 1.0            # [TBD-HAZID] 速度效率

    # ===== IPOPT 选项 =====
    ipopt:
      max_iter: 150             # [TBD-HAZID]
      tol: 1.0e-4               # [TBD-HAZID]
      max_cpu_time_s: 0.45      # 500 ms SLA - 50 ms 余量
      linear_solver: "mumps"
      hessian_approximation: "limited-memory"
      print_level: 0

    # ===== 周期 =====
    solve_period_hz: 1.0       # 1 Hz primary; degrades to 2 Hz on timeout
    output_freshness_ms: 200   # World_State must be < 200 ms old

    # ===== 输出 =====
    waypoints_count: 4          # RFC-001 fixed 4-WP
    avoidance_plan_topic: "/l3/m5/avoidance_plan"
    asdr_record_topic: "/l3/asdr/record"
    sat_data_topic: "/l3/sat/data"
    sat_data_period_hz: 10.0

    # ===== 输入 topics =====
    odd_state_topic: "/l3/m1/odd_state"
    world_state_topic: "/l3/m2/world_state"
    behavior_plan_topic: "/l3/m4/behavior_plan"
    colregs_constraint_topic: "/l3/m6/colregs_constraint"
    planned_route_topic: "/l2/planned_route"
    speed_profile_topic: "/l2/speed_profile"

    # ===== 失效阈值 =====
    world_state_timeout_s: 0.5     # > 0.5 s → BC-MPC 自动激活
    odd_state_timeout_s: 3.0       # > 3 s → 冻结规划
    behavior_plan_timeout_s: 1.0   # > 1 s → 沿用旧约束
    consecutive_failure_critical: 5 # > 5 周期 failed → CRITICAL

    # ===== Capability Manifest 路径 =====
    capability_manifest_path: "config/fcb_vessel_capability.yaml"
```

### 9.2 `config/m5_bc_mpc_params.yaml`

```yaml
# BC-MPC 节点参数（紧急接口）
# Per detailed design §5.3 + RFC-001 紧急接口
m5_tactical_planner_bc_mpc:
  ros__parameters:
    # ===== 触发阈值（[TBD-HAZID] CPA emergency）=====
    cpa_safe_default_nm: 1.0           # [TBD-HAZID] from M1 ODD_StateMsg dynamic
    cpa_short_horizon_factor: 0.7      # [TBD-HAZID]
    new_threat_cpa_nm: 0.5             # [TBD-HAZID]
    new_threat_tcpa_s: 60.0            # [TBD-HAZID]
    tcpa_compression_threshold_s: 150.0 # [TBD-HAZID]
    tcpa_compression_rate: 0.5          # [TBD-HAZID]
    mid_mpc_consecutive_fail_threshold: 2

    # ===== 分支树参数 =====
    k_branches_high_urgency: 7          # [TBD-HAZID]
    k_branches_medium_urgency: 5        # [TBD-HAZID]
    heading_range_high_deg: 30.0        # [TBD-HAZID] ±30°
    heading_range_medium_deg: 20.0      # [TBD-HAZID]
    horizon_s: 20.0                     # [TBD-HAZID] 10–30 s
    dt_s: 2.0                            # 粗粒度
    urgency_threshold: 0.7              # [TBD-HAZID]

    # ===== Validity 续命 =====
    min_validity_s: 1.0                 # [TBD-HAZID]
    max_validity_s: 3.0                 # [TBD-HAZID]
    refresh_period_ms: 100              # 10 Hz cap (硬约束)
    cpa_extend_factor: 0.5              # CPA < safe×0.5 → extend
    cpa_exit_factor: 0.9                # CPA > safe×0.9 → exit

    # ===== 输出 =====
    reactive_override_topic: "/l3/m5/reactive_override_cmd"
    asdr_record_topic: "/l3/asdr/record"

    # ===== 输入 =====
    world_state_topic: "/l3/m2/world_state"
    odd_state_topic: "/l3/m1/odd_state"
    internal_last_plan_topic: "/l3/m5/internal/last_plan"
    # ... full sub list ...

    # ===== Capability Manifest =====
    capability_manifest_path: "config/fcb_vessel_capability.yaml"
```

### 9.3 `config/fcb_vessel_capability.yaml`（FCB Capability Manifest）

```yaml
# FCB Vessel Capability Manifest (Backseat Driver pattern, ADR-001)
# Decision-core code is vessel-agnostic; FCB params injected via this file.
# Per detailed design §13 + Yasukawa 2015 [R7] MMG model.
fcb_capability:
  vessel_id: "FCB-001"
  vessel_class: "FCB"

  # ===== 几何 =====
  length_m: 28.0                 # [HAZID-VERIFY] FCB hull length
  beam_m: 6.5
  draft_m: 1.4
  mass_kg: 95000.0

  # ===== 操纵性（Yasukawa MMG）=====
  rot_max_at_18kn_rad_s: 0.20944  # 12 deg/s [TBD-HAZID] FCB 水池试验
  rot_max_speed_correction:        # f(u) per §5.4.2
    low_speed_kn: 10.0
    low_speed_factor: 1.2
    high_speed_kn: 20.0
    high_speed_factor: 0.8
  rough_sea_factor_per_hs: 0.10    # 每米 Hs 减幅 10%

  # ===== 制动 =====
  stopping_distance_at_18kn_m: 250.0  # [TBD-HAZID] FCB 停船试验
  crash_stop_max_decel_mps2: 0.8      # [TBD-HAZID]

  # ===== 速度限制 =====
  speed_min_kn: 0.0
  speed_max_kn: 28.0
  service_speed_kn: 18.0

  # ===== MMG 系数（详细设计 §13）=====
  mmg_coefficients:
    # Surge / Sway / Yaw 力矩 / 阻力等数十项；此处省略
    surge_added_mass_factor: 0.05
    sway_added_mass_factor: 0.40
    yaw_added_inertia_factor: 0.07
    # ... [TBD-HAZID] FCB 水动力试验完整系数 ...
```

---

## 10. PATH-D 合规性 checklist

按 `00-implementation-master-plan.md` §5.2 + `coding-standards.md` §1，M5 Phase E1 完成 DoD：

### 10.1 编码规范

- [ ] **MISRA C++:2023 完整 179 规则**（CI 强制）— `clang-tidy` + `cppcheck Premium` 全启用
- [ ] **clang-format**：项目 `.clang-format` 一致格式
- [ ] **C++17** 严格模式（`-std=c++17` + `-Wpedantic`）；不使用 GNU 扩展
- [ ] **Release**：`-O2`（不允许 `-O3`，避免激进优化暴露 UB）
- [ ] **无裸指针算术**：用 `std::span<>` / `std::array<>` / Eigen
- [ ] **RAII 全覆盖**：所有资源用 smart pointer / 自定义 RAII；无裸 `new` / `delete`
- [ ] **角度单位**：内部 rad，.msg 边界 deg；统一在 `common/units.hpp`
- [ ] **时间单位**：用 `std::chrono::nanoseconds` / `rclcpp::Time`，禁用 raw `double` 秒数

### 10.2 静态分析

- [ ] `clang-tidy` 全规则启用（PATH-D `clang-tidy` 配置 `static-analysis-policy.md` §3）
- [ ] `cppcheck Premium` MISRA C++:2023 完整规则集
- [ ] AddressSanitizer + UBSan + ThreadSanitizer 0 错误（Debug build）
- [ ] **CasADi/IPOPT 路径例外**：M5 因 LGPL 库内部存在不可避免的违规（`.cpp` 内部非 const-correctness 等），按 `coding-standards.md` §10 写明豁免范围 + reviewer 双签

### 10.3 单元测试

- [ ] 覆盖率 **≥ 90%**（lcov / gcovr 报告）
- [ ] §8 列出的 9 个 unit test + 3 个 integration test 全绿
- [ ] mock fixtures 覆盖详细设计 §9.3 四个 HIL 场景的关键 unit 子集
- [ ] **Mid-MPC 求解时间基准**：< 350 ms 平均（IPC 测试机基线）
- [ ] **BC-MPC 端到端响应**：< 150 ms p99

### 10.4 接口契约

- [ ] AvoidancePlanMsg / ReactiveOverrideCmd 字段与 `ros2-idl-implementation-guide.md` §3 完全一致
- [ ] ASDRRecord 格式符合详细设计 §8.3
- [ ] DDS QoS 配置与 `ros2-idl-implementation-guide.md` §4 一致
- [ ] 6 个订阅 topic 全部用 `l3_external_msgs` mock publisher 端到端测试通过

### 10.5 HAZID 注入点

- [ ] 所有 `[TBD-HAZID]` 参数通过 YAML 注入（不硬编码）
- [ ] 详细设计附录 C 8 个校准参数在 `config/*.yaml` 中均有占位
- [ ] 附录 C 参数在源代码注释中**显式标注** `[TBD-HAZID]` + RPN（详细设计 §C 表）

### 10.6 文档同步

- [ ] 模块 README.md 链接本骨架 spec + 详细设计
- [ ] 关键 NLP 公式（详细设计 §5.2.1 / §5.3.1）在 header 注释中引用对应章节
- [ ] CasADi 表达式调试技巧记录在 `docs/casadi-symbolic-graph.md`
- [ ] IPOPT 选项调优记录在 `docs/ipopt-tuning-notes.md`

### 10.7 Doer-Checker 独立性影响

- [ ] M5 是 Doer，**不受**独立性约束（CasADi/IPOPT/GeographicLib/Boost.Geometry 全部允许）
- [ ] M5 不引用 M7 任何 header / 库（反向独立性，避免循环依赖）
- [ ] M5 通过 `l3_msgs/SafetyAlert` 接收 M7 告警（仅订阅 `/l3/m7/safety_alert` 用于状态机降级，详细设计 §4.2）

### 10.8 License 合规

- [ ] CasADi (LGPL-3.0) **动态链接**（CMake 配置无 `-static`）
- [ ] IPOPT (EPL-2.0) 修改如有须回贡献（项目不修改库本身）
- [ ] SBOM 自动生成（CycloneDX）含 CasADi 3.7.2 / IPOPT 3.14.19 完整条目
- [ ] LICENSE 文件随分发件附带

---

## 11. 引用

### 11.1 上游设计文档

- `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2（§10 全章 / §10.6 / §10.7 / §15.1 / §15.2）
- `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md` v1.0（SANGO-ADAS-L3-DD-M5-001）
- `docs/Design/Cross-Team Alignment/RFC-decisions.md` RFC-001 决议（方案 B）

### 11.2 实施层基线文档

- `docs/Implementation/00-implementation-master-plan.md` v1.0
- `docs/Implementation/coding-standards.md` v1.0（PATH-D MISRA C++:2023）
- `docs/Implementation/static-analysis-policy.md` v1.0
- `docs/Implementation/ci-cd-pipeline.md` v1.0
- `docs/Implementation/ros2-idl-implementation-guide.md` v1.0（§3.3 AvoidancePlan + ReactiveOverrideCmd）
- `docs/Implementation/third-party-library-policy.md` v1.0（§2.2 CasADi/IPOPT/GeographicLib + §3 独立性矩阵 + §4.3 LGPL 合规）

### 11.3 算法 / 工程参考

- **[R3]** Benjamin, M. (2010). *MOOS-IvP Overview.* CITR Tech Rep. — Backseat Driver 范式
- **[R4]** Veitch, B. et al. (2024). *Remote Operation Window and Transparency Trade-offs in Maritime Autonomy.* Ocean Eng, 299:117257. — TMR ≥ 60s
- **[R7]** Yasukawa, H. & Yoshimura, Y. (2015). *Introduction to MMG method.* J Mar Sci Tech, 20(37–52). — FCB 高速操纵修正
- **[R20]** Eriksen, B., Bitar, G., Breivik, M. et al. (2020). *Branching-Course MPC for Collision Avoidance.* Frontiers Robot AI, 7:11. — BC-MPC 算法原理
- **CasADi 3.7.2** [web.casadi.org/get](https://web.casadi.org/get/) 🟢 A
- **IPOPT 3.14.19** [github.com/coin-or/Ipopt](https://github.com/coin-or/Ipopt/releases) 🟢 A
- **GeographicLib 2.7** [sourceforge/geographiclib](https://sourceforge.net/p/geographiclib/news/2025/11/geographiclib-27-released-2025-11-06/) 🟢 A

### 11.4 合规基线

- **[R1]** CCS《智能船舶规范》v1.0（i-Ship Nx, Ri/Ai） — 决策可审计 / 白盒要求
- **[R2]** IMO MSC 110/111 MASS Code（2025–2026） — Operational Envelope
- **IEC 61508 SIL 2** — M5 SIL 2（CPA 约束校验 + 输出约束活跃集，详细设计 §10.3）
- **ISO 21448 SOTIF** — 感知降质 / 预测误差处理

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Team-M5（Claude Code 实施层 kickoff） | 初稿创建：双 ROS2 节点 + CasADi/IPOPT 集成 + 9 unit + 3 integration tests + 3 YAML config + 8 项 HAZID 注入点 + PATH-D DoD checklist |

---

**本骨架完成状态**：✅ 12 章；与详细设计 v1.0 + v1.1.2 §15 + RFC-001 一致；可分发给 Team-M5 作为 Phase E1 编码起点。

**Team-M5 启动后续动作**：

1. ⏳ 创建 `src/m5_tactical_planner/` workspace（按 §2 目录结构）
2. ⏳ 实现 §4.3 共享类（VesselDynamicsModel + CapabilityManifest 优先，其他模块依赖）
3. ⏳ 实现 §4.1 Mid-MPC 类族 + §8.1 + §8.5 单元 / 集成测试
4. ⏳ 实现 §4.2 BC-MPC 类族 + §8.2 + §8.5 单元 / 集成测试
5. ⏳ Wave 3 集成测试（`docs/Implementation/00-implementation-master-plan.md` §5）
6. ⏳ HAZID RUN-001 校准结果（2026-08-19）回填 §9 YAML
