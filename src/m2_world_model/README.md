# M2 World Model

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M2-001 |
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 详细设计 | `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M2/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §6 + §15.1 World_StateMsg + RFC-002 |

## 角色

唯一权威世界视图。聚合外部 Multimodal Fusion 三话题输入（targets / own_ship / environment），输出 4 Hz 时间对齐的统一 `WorldState` 给 M3 / M4 / M5 / M6。

## 核心职责

- **三视图融合**：目标列表 / 自身状态（含海流估计）/ ENC 区域约束
- **CPA / TCPA 计算**：time-aligned 几何（own_ship 最新快照外推）
- **COLREG 几何预分类**：HEAD_ON / OVERTAKING / CROSSING / OVERTAKEN / CROSSED_BY / AMBIGUOUS（架构 §6.2）
- **TrackBuffer 时间对齐**：处理跨话题 stamp 漂移（RFC-002 决议）
- **ENC 加载**（Python 工具）：S-57 chart → ZoneConstraint 几何

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（3 publishers）：
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz
- `/l3/sat/data` (l3_msgs/SATData) — 10 Hz

**订阅**（4 subscribers）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件
- `/fusion/tracked_targets` (l3_external_msgs/TrackedTargetArray) — 2 Hz（AoS 含完整 TrackedTarget[]）
- `/fusion/own_ship_state` (l3_external_msgs/FilteredOwnShipState) — 50 Hz（含 RFC-002 nav_mode）
- `/fusion/environment_state` (l3_external_msgs/EnvironmentState) — 0.2 Hz

## 主要类（include/m2_world_model/）

- `WorldStateAggregator` — 三视图融合主类
- `CpaTcpaCalculator` — Time-aligned 几何计算
- `EncounterClassifier` — COLREG 几何预分类
- `TrackBuffer` — 时间对齐缓冲（RFC-002）
- `EncLoader` — C++ ENC 主接口
- `WorldModelNode` — ROS2 节点

**Python 工具**（python/）：
- `enc_loader.py` — S-57 ENC 加载（pyproj + GeographicLib）

## RFC-002 关键约束

- 目标速度 = **对地**（sog/cog from TrackedTarget）
- 自身速度 = **对水**（u/v from FilteredOwnShipState）+ 海流估计
- M2 自行处理时间对齐（取 own_ship 最近快照外推）
- nav_mode 字段（OPTIMAL/DR_SHORT/DR_LONG/DEGRADED）— Fusion 提供，M2 用于置信度门禁

## 配置（9 个 [TBD-HAZID] 标注）

- `config/m2_params.yaml` — CPA/TCPA 阈值（4 个 ODD 子域 × 各 2）+ Rule 14 容差 + SAFE_PASS 速度 + 数据丢失容限
- `config/enc_data_paths.yaml` — S-57 chart 路径

## 开发命令

```bash
# Build
colcon build --packages-up-to m2_world_model \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Unit tests (C++ + Python)
colcon test --packages-select m2_world_model
pytest src/m2_world_model/python/tests/

# Coverage
lcov --capture --directory build/m2_world_model --output-file coverage.info

# Static analysis
run-clang-tidy-20 -p build/m2_world_model \
                  -config-file=.clang-tidy \
                  -warnings-as-errors='*' \
                  src/m2_world_model/src/*.cpp

# Python lint
ruff check src/m2_world_model/python/
mypy src/m2_world_model/python/
```

## DoD（PATH-D）

参见 `docs/Implementation/M2/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 90%（C++ + Python 各自）
- MISRA C++:2023 完整 179 规则
- 函数 ≤ 60 行 + 循环复杂度 ≤ 10
- 全部 [TBD-HAZID] 通过 YAML 注入
- 跨模块接口字段对齐 v1.1.2 §15.1
- 时间对齐覆盖：CPA/TCPA 6 边界场景 + Encounter 4 Rule + 边界 + TrackBuffer 5 场景

## 库白名单（PATH-D）

- Eigen 5.0.0（线性代数）
- GeographicLib 2.7（WGS84 / 大地坐标）
- Boost.Geometry（多边形交集）
- nlohmann::json（ENC 元数据）
- spdlog 1.17.0
- yaml-cpp 0.8.0

## 引用

- **架构基线**：[v1.1.2 §6 M2 World Model](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M2/01-detailed-design.md](../../docs/Design/Detailed%20Design/M2-World-Model/01-detailed-design.md)
- **代码骨架 spec**：[M2/code-skeleton-spec.md](../../docs/Implementation/M2/code-skeleton-spec.md)
- **RFC-002 决议**：[RFC-decisions.md](../../docs/Design/Cross-Team%20Alignment/RFC-decisions.md)
