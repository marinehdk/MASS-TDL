# M6 COLREGs Reasoner

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M6-001 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 详细设计 | `docs/Design/Detailed Design/M6-COLREGs-Reasoner/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M6/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §9 + §15.1 COLREGsConstraintMsg |

## 角色

国际海上避碰规则（COLREGs Rule 5–19）的实时推理引擎。基于 M2 World Model 的 EncounterClassification 触发对应 Rule，输出 COLREGsConstraint 给 M5 Tactical Planner。

## 核心职责

- **11 条 COLREGs Rule 推理**：Rule 5（瞭望）/ 6（安全速度）/ 7（碰撞危险）/ 8（让路船行动）/ 13（追越）/ 14（对头）/ 15（横越）/ 16（让路船）/ 17（直航船 + T_standOn / T_act 阶段）/ 18（船舶类型间责任）/ 19（能见度不良 — ODD-D 触发）
- **阶段判别**：T_standOn → T_act → T_postAvoid（CPA / TCPA → phase）
- **ODD-aware 参数**：每个 ODD 子域（A/B/C/D）的 T_standOn / T_act 阈值差异化
- **规则库插件接口**：COLREGs ↔ CEVNI（内河船）切换 — Capability Manifest 范式
- **Constraint 生成**：Rule → 航向 / 速度约束集 → COLREGsConstraintMsg

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（3 publishers）：
- `/l3/m6/colregs_constraint` (l3_msgs/COLREGsConstraint) — 2 Hz
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz
- `/l3/sat/data` (l3_msgs/SATData) — 10 Hz

**订阅**（2 subscribers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — ODD-aware 参数切换
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz

## 主要类（include/m6_colregs_reasoner/）

**抽象基类 + 工厂**：
- `ColregsRuleBase` — 规则插件接口（ABC）
- `RuleLibraryLoader` — COLREGs vs CEVNI 切换（工厂模式）

**11 Rule 实现**（src/rules/colregs/）：
- `Rule5_LookOut` / `Rule6_SafeSpeed` / `Rule7_RiskOfCollision`
- `Rule8_ActionToAvoidCollision` / `Rule13_Overtaking` / `Rule14_HeadOn`
- `Rule15_Crossing` / `Rule16_GiveWayShip` / `Rule17_StandOnShip`
- `Rule18_ResponsibilitiesBetweenVessels` / `Rule19_ConductInRestrictedVisibility`

**支持类**：
- `ColregsPhaseClassifier` — T_standOn / T_act / T_postAvoid 阶段判别
- `ColregsConstraintGenerator` — Rule → Constraint 转换
- `ColregsReasonerNode` — ROS2 节点

## 配置（36 个 [TBD-HAZID] 标注 — Wave 1 模块中最多）

- `config/m6_params.yaml` — Rule 通用参数
- `config/odd_aware_thresholds.yaml` — 每个 ODD 子域的 T_standOn / T_act 阈值（4 ODD × 多 Rule）
- `config/colregs_rule_library.yaml` — COLREGs 默认规则参数
- `config/cevni_rule_library.yaml` — 内河船 CEVNI（备选 — Wave 2+ 启用）

## 开发命令

```bash
# Build
colcon build --packages-up-to m6_colregs_reasoner \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Unit tests (16 测试文件 — 每条 Rule ≥ 5 用例 + Phase / Generator / Loader / ODD)
colcon test --packages-select m6_colregs_reasoner
colcon test-result --verbose

# Coverage
lcov --capture --directory build/m6_colregs_reasoner --output-file coverage.info

# Static analysis
run-clang-tidy-20 -p build/m6_colregs_reasoner \
                  -config-file=.clang-tidy \
                  -warnings-as-errors='*' \
                  src/m6_colregs_reasoner/src/*.cpp
```

## DoD（PATH-D）

参见 `docs/Implementation/M6/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 90%（实际 test/impl ratio = 124% — 测试规模超实现）
- ≥ 30 个 COLREGs Rule 测试用例（每 Rule ≥ 5）
- COLREGs ↔ CEVNI 工厂切换覆盖
- ODD-aware 阈值切换：4 ODD × 多 Rule 矩阵
- 全部 [TBD-HAZID] 通过 YAML 注入

## 库白名单（PATH-D）

- Boost.Geometry（多边形 / 夹角运算）
- Eigen 5.0.0
- spdlog 1.17.0 / fmt 11.0.2
- yaml-cpp 0.8.0
- GTest 1.17.0

> **不引入** nlohmann::json（M6 不做 JSON 解析；保持依赖最小）

## 多船型扩展路径（Wave 4+）

- FCB（首发） → COLREGs（默认）
- 拖船 → COLREGs（参数差异化）
- 渡船 → COLREGs（船型特定）
- 内河船 → **CEVNI 切换**（v1.1.2 §13.5 + F-P2-D4-027）

## 引用

- **架构基线**：[v1.1.2 §9 M6 COLREGs Reasoner](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M6/01-detailed-design.md](../../docs/Design/Detailed%20Design/M6-COLREGs-Reasoner/01-detailed-design.md)
- **代码骨架 spec**：[M6/code-skeleton-spec.md](../../docs/Implementation/M6/code-skeleton-spec.md)
- **COLREGs 1972**（IMO） + **CEVNI**（UN-ECE 内河）— 详见详细设计 §11 引用
