# M4 Behavior Arbiter

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M4-001 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 详细设计 | `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M4/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §8 |

## 角色

行为字典 + IvP（Interval Programming）多目标仲裁器。基于 ODD State / Mission Goal / World State / COLREGs Constraint，输出 (heading_min, heading_max, speed_min, speed_max) 区间约束给 M5 Tactical Planner。

## 核心职责

- **行为字典**：7 行为枚举（TRANSIT / COLREG_AVOID / DP_HOLD / BERTH / MRC_DRIFT / MRC_ANCHOR / MRC_HEAVE_TO）+ 每行为激活谓词
- **IvP 求解器**（自实现，不引外部 IvP 库）：编译期固定 piece 数 `IvPFunction<Pieces=32>` 避免堆分配
- **多目标合并策略**：WeightedSumCombination 等
- **行为优先级仲裁**：MRC 仅 override 机制（非固定优先级，对齐 [R16] Pirjanian arbitration）
- **输出**：BehaviorPlanMsg @ 2 Hz

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（2 publishers）：
- `/l3/m4/behavior_plan` (l3_msgs/BehaviorPlan) — 2 Hz
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz

**订阅**（5 subscribers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件
- `/l3/m1/mode_cmd` (l3_msgs/ModeCmd) — 事件
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/m3/mission_goal` (l3_msgs/MissionGoal) — 0.5 Hz
- `/l3/m6/colregs_constraint` (l3_msgs/COLREGsConstraint) — 2 Hz

## 主要类（include/m4_behavior_arbiter/）

- `BehaviorDictionary` — 7 行为定义 + ODD-aware 子集
- `BehaviorActivationCondition` — 每行为的激活谓词
- `IvPFunction<Pieces=32>` — 区间值函数（编译期固定 piece 数模板）
- `IvPHeadingDomain` / `IvPSpeedDomain` — 搜索域（含 360° wrap）
- `IvPCombinationStrategy` 抽象 + `WeightedSumCombination` — 多目标合并
- `IvPSolver` — 经典 IvP 求解（Benjamin 2002 PhD [R10]）
- `BehaviorPriority` — MRC 仅 override 仲裁规则
- `BehaviorArbiterNode` — ROS2 节点（DI 容器）

## 配置（YAML 注入）

- `config/m4_params.yaml` — IvP 求解器参数 + 行为权重 / 优先级（[TBD-HAZID] 标注）
- `config/behavior_definitions.yaml` — 7 行为激活条件 + 默认权重（HAZID 校准）

## 开发命令

```bash
# Build (debug + ASan + UBSan + coverage)
colcon build --packages-up-to m4_behavior_arbiter \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Unit tests
colcon test --packages-select m4_behavior_arbiter
colcon test-result --verbose

# Coverage
lcov --capture --directory build/m4_behavior_arbiter --output-file coverage.info
genhtml coverage.info --output-directory coverage-html

# Static analysis
run-clang-tidy-20 -p build/m4_behavior_arbiter \
                  -config-file=.clang-tidy \
                  -warnings-as-errors='*' \
                  src/m4_behavior_arbiter/src/*.cpp

# Doer-Checker independence (always run — M4 is in Doer side)
bash tools/ci/check-doer-checker-independence.sh
```

## DoD（PATH-D）

参见 `docs/Implementation/M4/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 90%
- IvP 求解器 360° wrap + 退化场景全覆盖
- BehaviorPriority MRC override 测试（非固定优先级）
- 全部 [TBD-HAZID] 通过 YAML 注入
- IvPFunction 编译期固定 piece 数（static_assert 验证 sizeof）

## 库白名单（PATH-D）

- Eigen 5.0.0（IvP 内部矩阵）
- spdlog 1.17.0 / fmt 11.0.2
- yaml-cpp 0.8.0
- GTest 1.17.0

> **不需要** CasADi/IPOPT（M5 才用）；不需要 Boost.Geometry / GeographicLib

## 引用

- **架构基线**：[v1.1.2 §8 M4 Behavior Arbiter](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M4/01-detailed-design.md](../../docs/Design/Detailed%20Design/M4-Behavior-Arbiter/01-detailed-design.md)
- **代码骨架 spec**：[M4/code-skeleton-spec.md](../../docs/Implementation/M4/code-skeleton-spec.md)
- **IvP 经典论文**：Benjamin 2010 [R3] / Benjamin 2002 PhD [R10] / Pirjanian arbitration [R16]
