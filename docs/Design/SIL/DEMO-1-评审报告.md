# DEMO-1 场景全面评审报告

| 字段 | 值 |
|---|---|
| 评审日期 | 2026-05-25 |
| 评审范围 | DEMO-1 imazu-01-ho (Imazu 1987 Case 01 Head-On) 端到端 |
| 评审员 | V&V 工程师 + AI 辅助 |
| 架构基线 | v1.1.3-pre-stub |
| Git HEAD | 0685228 (main) |

---

## 一、Git 分析结论

**前端 Screen 3/4 修改没有丢失。** 两个关键 commit 均在 main HEAD 上：

| Commit | 内容 | 文件数 |
|---|---|---|
| `3f2b2d2` | 第4屏 SimulationEvaluator (Phase A+B) | 9 files |
| `0685228` | 第3屏 SimulationMonitor (aggregated sidebar + hotkeys + COLREGs) | 9 files |

9 个前端文件，+606/-219 行修改完好保存在 main 分支。

---

## 二、4 屏业务流程评审

### 2.1 流程总览

| 屏 | 组件 | 数据流 | 实现状态 | 评分 |
|---|---|---|---|---|
| **Screen 1** 场景选择 | `SimulationScenario.tsx` | 前端→API→YAML 加载 | ⚠️ 组件存在，但依赖 mock 场景列表 | **C** |
| **Screen 2** 前后端检查 | `SimulationCheck.tsx` | API→preflight gates | ⚠️ 部分真实 API，部分 mock | **C** |
| **Screen 3** 仿真监控 | `SimulationMonitor.tsx` | Foxglove WS→ROS2→实时数据 | ✅ 使用 `useFoxgloveLive` 真实数据 | **B** |
| **Screen 4** 仿真评价 | `SimulationEvaluator.tsx` | 前端 mock→无后端评分 | ❌ 评分数据全部 mock/hardcoded | **D** |

### 2.2 业务流程评价

**当前流程设计合理**（场景选择→检查→监控→评价），但**数据流断裂严重**：

- **Screen 1→2**：场景加载依赖 mock，非真实 YAML 解析
- **Screen 2→3**：preflight 检查部分真实，但通过条件可能绕过
- **Screen 3→4**：**最关键断裂**——仿真结束后无真实评分数据传递到第4屏

### 2.3 业务流程改进建议

1. **Screen 1** 应从 `scenarios/` 目录真实扫描 YAML 文件，而非前端硬编码列表
2. **Screen 2** preflight gates 应检查 ROS2 节点存活状态（liveness probe），而非仅检查 API 响应
3. **Screen 3→4** 跳转应由后端仿真完成事件触发，携带 scoring session ID，第4屏据此拉取评分数据
4. **Screen 4** 评分数据必须来自后端 `scoring` 节点，6 维度评分全部由后端计算

---

## 三、数据流审计

### 3.1 完整数据流图

```
Screen 1 (ScenarioBrowser)
  │  silApi.ts → /api/scenarios (⚠️ mock)
  ▼
Screen 2 (PreflightCheck)
  │  silApi.ts → /api/preflight (⚠️ 部分 mock)
  ▼
Screen 3 (SimulationMonitor)
  │  useFoxgloveLive → ws://rosbridge (✅ 真实)
  │  telemetryStore (zustand) ← ROS2 topics
  ▼
Screen 4 (SimulationEvaluator)
  │  ❌ 无真实 API 调用
  │  KpiDashboard 使用 hardcoded 数据
```

### 3.2 关键链路状态

| 链路 | 状态 | 风险 |
|---|---|---|
| 前端→后端 REST API | ⚠️ `silApi.ts` 定义了端点，但后端 `sil_orchestrator` 不完整 | API 调用可能 404 |
| 后端→ROS2 (sil_topic_bridge) | ✅ protobuf 桥接存在 | 部分消息类型未实现 |
| ROS2→前端 (Foxglove WS) | ✅ `useFoxgloveLive` 真实连接 | 依赖 rosbridge 正常运行 |
| 评分数据流 | ❌ **完全断裂** | 第4屏无法展示真实评分 |

### 3.3 Protobuf 消息定义

| Proto 文件 | 用途 | 状态 |
|---|---|---|
| `own_ship_state_pb2.py` | 本船状态 | ✅ 已定义 |
| `target_vessel_state_pb2.py` | 目标船状态 | ✅ 已定义 |
| `scoring_row_pb2.py` | 评分数据 | ✅ 已定义 |
| `lifecycle_control_pb2.py` | 生命周期控制 | ✅ 已定义 |
| `fault_trigger_pb2.py` | 故障触发 | ✅ 已定义 |
| `environment_state_pb2.py` | 环境状态 | ✅ 已定义 |
| `radar_measurement_pb2.py` | 雷达测量 | ✅ 已定义 |

---

## 四、前端接口完整度审计

### 4.1 组件结构

| 屏 | 组件文件 | 核心依赖 | Mock 程度 |
|---|---|---|---|
| Screen 1 | `SimulationScenario.tsx` | `silApi.ts` | 高 |
| Screen 2 | `SimulationCheck.tsx` | `silApi.ts` | 中 |
| Screen 3 | `SimulationMonitor.tsx` | `useFoxgloveLive`, `telemetryStore` | 低 |
| Screen 3 共享 | `TimelineSixLane.tsx` | `useFoxgloveLive` | 低 |
| Screen 3 共享 | `TrajectoryReplay.tsx` | `useFoxgloveLive` | 低 |
| Screen 3 共享 | `AsdrLedger.tsx` | `telemetryStore` | 中 |
| Screen 3 共享 | `FaultInjectPanel.tsx` | `silApi.ts` | 高 |
| Screen 4 | `SimulationEvaluator.tsx` | 本地 state | **极高** |
| Screen 4 共享 | `KpiDashboard.tsx` | hardcoded | **极高** |

### 4.2 API/WebSocket 调用清单

| 调用方式 | 文件 | 端点/URL | 真实/Mock |
|---|---|---|---|
| REST | `silApi.ts` | `/api/scenarios` | ⚠️ 后端部分实现 |
| REST | `silApi.ts` | `/api/preflight` | ⚠️ 后端部分实现 |
| REST | `silApi.ts` | `/api/lifecycle` | ⚠️ 后端部分实现 |
| REST | `silApi.ts` | `/api/telemetry` | ⚠️ 后端部分实现 |
| REST | `silApi.ts` | `/api/scoring` | ❌ 后端未实现 |
| WebSocket | `useFoxgloveLive.ts` | `ws://localhost:8765` | ✅ 真实 Foxglove |
| WebSocket | `sil_topic_bridge.py` | ROS2↔Backend | ✅ 桥接存在 |

### 4.3 Mock 数据和假接口清单

| 文件 | Mock 内容 | 严重程度 |
|---|---|---|
| `SimulationEvaluator.tsx` | 评分数据全部 hardcoded | 🔴 阻断 |
| `KpiDashboard.tsx` | 6 维度评分仅 2 维可见，数据 fake | 🔴 阻断 |
| `SimulationScenario.tsx` | 场景列表 mock | 🟡 重要 |
| `SimulationCheck.tsx` | 部分 preflight 结果 mock | 🟡 重要 |
| `FaultInjectPanel.tsx` | 故障注入 API 未对接 | 🟢 改善 |

### 4.4 状态管理

| Store | 用途 | 真实数据占比 |
|---|---|---|
| `telemetryStore` (zustand) | 仿真遥测数据 | ~60%（Foxglove 真实 + 部分 mock） |
| `scenarioStore` (zustand) | 场景选择状态 | ~20%（场景列表 mock） |
| `uiStore` (zustand) | UI 状态 | N/A（纯前端） |
| `fsmStore` (zustand) | 仿真 FSM 状态 | ~40%（部分真实） |

### 4.5 前端总评

**评分：C** — 组件结构完整，Screen 3 有真实数据流，但 Screen 4 完全依赖 mock，Screen 1/2 部分依赖 mock。

---

## 五、后端 ROS2 节点审计

### 5.1 仿真节点清单

| 节点 | 包路径 | 语言 | 实现状态 | 关键问题 |
|---|---|---|---|---|
| `ship_dynamics` | `sil_nodes/ship_dynamics/` | Python | ⚠️ 部分 | MMG 模型参数存在，但部分逻辑 placeholder |
| `scoring` | `sil_nodes/scoring/` | Python | ⚠️ 部分 | Hagen scorer 骨架存在，评分逻辑未完成 |
| `scenario_authoring` | `sil_nodes/scenario_authoring/` | Python | ❌ stub | 场景加载逻辑未实现 |
| `target_vessel` | `sil_nodes/target_vessel/` | Python | ❌ stub | 目标船仿真逻辑缺失 |
| `env_disturbance` | `sil_nodes/env_disturbance/` | Python | ❌ stub | 风/流干扰未发布 |
| `fault_injection` | `sil_nodes/fault_injection/` | Python | ❌ stub | 故障注入未实现 |
| `sensor_mock` | `sil_nodes/sensor_mock/` | Python | ⚠️ 部分 | 传感器数据发布不完整 |
| `tracker_mock` | `sil_nodes/tracker_mock/` | Python | ⚠️ 部分 | 关键传感器数据缺失 |

### 5.2 L3 核心模块节点

| 模块 | 节点 | 语言 | 实现状态 | DEMO-1 影响 |
|---|---|---|---|---|
| M1 ODD Manager | `odd_envelope_manager_node` | C++ | ⚠️ 部分 | ODD 状态判断可能不完整 |
| M2 World Model | `world_model_node` | C++ | ❌ stub | 无权威世界视图 |
| M3 Mission Manager | `mission_manager_node` | C++ | ❌ stub | 无航次管理 |
| M4 Behavior Arbiter | `behavior_arbiter_node` | C++ | ❌ stub | 无行为仲裁 |
| M5 Tactical Planner | `mid_mpc_node` | C++ | ❌ stub | **无避碰决策输出** |
| M6 COLREGs Reasoner | `colregs_reasoner_node` | C++ | ❌ stub | **无法识别 Rule 14** |
| M7 Safety Supervisor | `safety_supervisor_node` | C++ | ⚠️ 部分 | SOTIF metrics 存在，核心逻辑不完整 |

### 5.3 Topic/Service 完整度矩阵

| Topic/Service | 发布者 | 订阅者 | 消息类型 | 状态 |
|---|---|---|---|---|
| `/own_ship/state` | ship_dynamics | sil_topic_bridge | own_ship_state | ⚠️ 部分实现 |
| `/target_vessel/state` | target_vessel | sil_topic_bridge | target_vessel_state | ❌ stub |
| `/scoring/result` | scoring | sil_topic_bridge | scoring_row | ⚠️ 部分实现 |
| `/environment/state` | env_disturbance | ship_dynamics | environment_state | ❌ stub |
| `/sensor/radar` | sensor_mock | tracker_mock | radar_measurement | ⚠️ 部分实现 |
| `/lifecycle/control` | sil_orchestrator | all nodes | lifecycle_control | ✅ 已定义 |
| `/fault/trigger` | fault_injection | target nodes | fault_trigger | ❌ stub |
| `/colregs/decision` | M6 colregs_reasoner | M5 tactical_planner | — | ❌ stub |
| `/tactical/avoidance_cmd` | M5 tactical_planner | ship_dynamics | — | ❌ stub |

### 5.4 Docker 仿真环境

| 组件 | 配置文件 | 状态 | 问题 |
|---|---|---|---|
| `sil_nodes.Dockerfile` | `docker/sil_nodes.Dockerfile` | ⚠️ | 部分安装路径 placeholder |
| `docker-compose.yml` | `docker/docker-compose.yml` | ⚠️ | 缺少 rosbridge 服务 |
| `sil_topic_bridge.py` | `docker/sil_topic_bridge.py` | ✅ | 桥接逻辑存在 |
| rosbridge | — | ❌ | 未在 compose 中配置 |

### 5.5 后端总评

**评分：D** — 8 个仿真节点中 5 个为 stub，L3 核心 M4/M5/M6 均未实现，无法支撑完整仿真闭环。

---

## 六、场景实现审计（imazu-01-ho）

### 6.1 场景参数对比

| 参数 | Demo-1场景.md 文档 | imazu-01-ho.yaml 实现 | 一致性 |
|---|---|---|---|
| 本船初始位置 | (63.44°N, 10.38°E) | (63.44°N, 10.38°E) | ✅ |
| 本船 SOG | 10.0 kn | 10.0 kn | ✅ |
| 本船 COG | 0.0° (正北) | 0.0° | ✅ |
| 目标船初始位置 | (63.557451°N, 10.38°E) | (63.557451°N, 10.38°E) | ✅ |
| 目标船 SOG | 10.0 kn | 10.0 kn | ✅ |
| 目标船 COG | 180.0° (正南) | 180.0° | ✅ |
| 仿真总时长 | 700s | 700s | ✅ |
| CPA 阈值 | 500m (0.27 NM) | 500m | ✅ |
| 避让偏角 | ~35° (0.61 rad) | — | ⚠️ 未在 YAML 中定义 |

### 6.2 五阶段实现状态

| 阶段 | 文档要求 | 实现状态 | 差距 |
|---|---|---|---|
| 阶段1：初始直航 (0-200s) | OS 10kn 北行，TS 10kn 南行 | ⚠️ YAML 参数正确，但 `target_vessel` 节点 stub | 目标船可能不动 |
| 阶段2：探测分类 (200-300s) | M6 识别 Rule 14 | ❌ M6 colregs_reasoner stub | 无法识别对遇局面 |
| 阶段3：右转避让 (300-450s) | M5 输出 35° 右转 | ❌ M5 tactical_planner stub | 无避碰决策 |
| 阶段4：驶过回归 (450-600s) | CPA > 500m，回归航线 | ❌ 依赖 M5/M6 | 无法实现 |
| 阶段5：仿真评估 (600-700s) | 自动评分，跳转第4屏 | ❌ scoring 节点不完整 | 无真实评分 |

### 6.3 Preflight Gates

| Gate | 文件 | 检查内容 | 状态 |
|---|---|---|---|
| gate_1 | `.preflight/gate_1.json` | — | ⚠️ 存在 |
| gate_2 | `.preflight/gate_2.json` | — | ⚠️ 存在 |
| gate_3 | `.preflight/gate_3.json` | — | ⚠️ 存在 |
| gate_4 | `.preflight/gate_4.json` | — | ⚠️ 存在 |
| gate_5 | `.preflight/gate_5.json` | — | ⚠️ 存在 |
| gate_6 | `.preflight/gate_6.json` | — | ⚠️ 存在 |

### 6.4 场景端到端可执行性

**评分：D** — 5 个阶段中仅阶段 1 的场景参数正确，阶段 2-5 均因后端 stub 无法实现。

---

## 七、需修改和提升的开发项

### 🔴 P0 阻断项（必须修复，否则 DEMO-1 无法演示）

| # | 修改项 | 工作量 | 说明 |
|---|---|---|---|
| P0-1 | **`target_vessel` 节点实现** | 3d | 目标船必须能按 YAML 参数运动（AIS replay 或恒速直线），否则 Screen 3 无船可看 |
| P0-2 | **`ship_dynamics` MMG 模型完善** | 2d | 本船运动必须响应 rudder 命令，当前有骨架但逻辑不完整 |
| P0-3 | **`scenario_authoring` 节点实现** | 2d | 场景 YAML → ROS2 参数的加载链路，否则无法启动仿真 |
| P0-4 | **Screen 4 评分 API 对接** | 2d | `SimulationEvaluator.tsx` 必须调用真实后端评分 API，替换所有 mock 数据 |

**P0 合计：9 人天**

### 🟡 P1 重要项（DEMO-1 演示质量关键）

| # | 修改项 | 工作量 | 说明 |
|---|---|---|---|
| P1-1 | **M6 COLREGs Reasoner 最小实现** | 3d | 至少实现 Rule 14 Head-On 识别，输出 `turn_starboard` 决策 |
| P1-2 | **M5 Tactical Planner 最小实现** | 3d | 至少实现恒定右转避让（不需要完整 MPC），接收 M6 决策后输出 rudder 命令 |
| P1-3 | **`scoring` 节点完善** | 2d | CPA 计算 + 6 维度评分至少实现 safety + rule_compliance |
| P1-4 | **Screen 1 真实场景列表 API** | 1d | 替换 mock 场景列表，从 `scenarios/` 目录读取真实 YAML |
| P1-5 | **`sil_orchestrator` 仿真启动流程** | 2d | API → Docker compose → ROS2 launch 的完整链路 |

**P1 合计：11 人天**

### 🟢 P2 改善项（提升演示完整度）

| # | 修改项 | 工作量 | 说明 |
|---|---|---|---|
| P2-1 | **Screen 3 G 键工程视角** | 1d | M6 决策溯源树、M5 MPC 路径候选可视化 |
| P2-2 | **`env_disturbance` 节点** | 1d | 风/流干扰模拟 |
| P2-3 | **`fault_injection` 节点** | 1d | Screen 3 FaultInjectPanel 对接 |
| P2-4 | **Docker Compose 完善** | 1d | rosbridge + 所有节点服务编排 |
| P2-5 | **Screen 2 preflight gates 真实检查** | 1d | 替换 mock 检查结果 |

**P2 合计：5 人天**

### 总工作量

| 优先级 | 项数 | 工作量 |
|---|---|---|
| P0 阻断 | 4 | 9 人天 |
| P1 重要 | 5 | 11 人天 |
| P2 改善 | 5 | 5 人天 |
| **合计** | **14** | **25 人天** |

---

## 八、总评

| 维度 | 评分 | 说明 |
|---|---|---|
| **数据流完整性** | **D** | 4 屏中仅 Screen 3 有真实数据流，Screen 4 完全断裂 |
| **前端接口完整度** | **C** | 组件结构完整，但大量 mock 数据，Screen 4 无真实 API |
| **后端完整度** | **D** | 8 个 ROS2 节点中 5 个为 stub，L3 核心 M4/M5/M6 均未实现 |
| **场景端到端可执行性** | **D** | 5 阶段仅阶段 1 参数正确，阶段 2-5 因后端 stub 无法闭环 |
| **业务流程设计** | **B** | 4 屏流程设计合理，状态管理 (zustand) 架构清晰 |

### 核心结论

DEMO-1 当前最大的阻断在**后端**——M5/M6 的避碰决策链路未实现，导致仿真无法产生真实的避碰行为。前端 Screen 3 已有较好的实时数据展示能力（Foxglove WS），但 Screen 4 的评分完全依赖 mock。

**建议优先攻克 P0-1~P0-4（约 9 人天）**，使仿真至少能跑通"两船对遇→本船右转→安全驶过"的基本闭环，再推进 P1 项提升评分和决策质量。

---

*V&V 工程师签字*: ________________ &emsp; 日期: ________
