# M8 HMI/Transparency Bridge

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M8-001 |
| 路径强度 | **PATH-H（HMI 路径，简化裁剪规则集 ~120 规则）** |
| 详细设计 | `docs/Design/Detailed Design/M8-HMI-Transparency-Bridge/01-detailed-design.md` |
| 代码骨架 spec | `docs/Implementation/M8/code-skeleton-spec.md` |
| 架构基线 | v1.1.2 §12 + 决策三 SAT-1/2/3 + RFC-004 ASDR |

## 角色

**唯一对 ROC / 船长说话的实体**。聚合 M1/M2/M4/M6/M7 的 SAT_DataMsg，按 §12.2 自适应触发策略呈现给操作员；ToR（Transfer of Responsibility）协议执行 IMO MASS Code "有意味的人为干预"合规流程。

CMM（Captain Mental Model）的"对外可见聚合点 + 翻译边界"——但本身不持有内部心智状态机（决策三 [F-P1-D1-010]）。

## 核心职责

- **多源 SAT 聚合**：订阅 8 topic 的 SAT_DataMsg + 7 个 L3 内部消息（ODD/World/Mission/Behavior/Avoidance/COLREGs/SafetyAlert）
- **AdaptiveSatTrigger**（v1.1.2 §12.2）：4 触发条件决定 SAT-2 推送时机
  - COLREGs 冲突检测
  - 系统信心下降
  - 操作员显式请求
  - TDL 压缩 < 30 s
- **TorProtocol** 状态机：IDLE → REQUESTED → ACKNOWLEDGED → TIMEOUT_MRC
  - SAT-1 ≥ 5 s 强制等待（防止误点）
  - 60 s 超时 → MRC 升级
  - IMO MASS Code C 交互验证（操作员主动点击"已知悉"按钮）
- **UiStateBuilder**：HMI 渲染数据构造（角色×场景双轴差异化 ROC vs 船长）
- **ModuleHealthMonitor**：监控上游 7 模块心跳 + 失活告警
- **ASDR 决策日志**：含 SHA-256 防篡改签名（独立实现，不复用 M7 的 `Sha256Signer`）

## ROS2 节点拓扑（v1.1.2 §15.2）

**发布**（3 publishers）：
- `/l3/m8/ui_state` (l3_msgs/UIState) — 50 Hz（HMI 渲染数据 → ROC HMI 前端）
- `/l3/m8/tor_request` (l3_msgs/ToRRequest) — 事件
- `/l3/asdr/record` (l3_msgs/ASDRRecord) — 事件 + 2 Hz

**订阅**（8 subscribers — Wave 1+2+3 全部上游）：
- `/l3/m1/odd_state` (l3_msgs/ODDState) — 1 Hz + 事件
- `/l3/m2/world_state` (l3_msgs/WorldState) — 4 Hz
- `/l3/m3/mission_goal` (l3_msgs/MissionGoal) — 0.5 Hz
- `/l3/m4/behavior_plan` (l3_msgs/BehaviorPlan) — 2 Hz
- `/l3/m5/avoidance_plan` (l3_msgs/AvoidancePlan) — 1–2 Hz
- `/l3/m6/colregs_constraint` (l3_msgs/COLREGsConstraint) — 2 Hz
- `/l3/m7/safety_alert` (l3_msgs/SafetyAlert) — 事件
- `/l3/sat/data` (l3_msgs/SATData) — 10 Hz from M1/M2/M4/M6/M7

## 主要类（include/m8_hmi_transparency_bridge/）

**C++ 主节点**：
- `SatAggregator` — 多源 SAT 聚合（线程安全，mutex 缩 lock 范围）
- `AdaptiveSatTrigger` — 4 条件 SAT-2 触发
- `TorProtocol` — IMO MASS Code C 交互验证状态机（≥ 5 s SAT-1 + 60 s 超时）
- `UiStateBuilder` — HMI 渲染数据
- `TorRequestGenerator` — ToR 消息构造
- `ModuleHealthMonitor` — 上游模块心跳监控
- `AsdrLogger` — 独立 ASDR 决策日志（含 SHA-256 数字摘要）
- `Sha256` — 独立 SHA-256 实现（M8 内部，不依赖 M7）
- `HmiTransparencyBridgeNode` — ROS2 节点（8 订阅 + 4 timer + 3 publish）

**Python FastAPI 后端**（`python/`）：
- `web_server/app.py` — FastAPI lifespan + CORS
- `web_server/websocket.py` — WebSocket 50 Hz UI_State 广播
- `web_server/tor_endpoint.py` — REST "已知悉"按钮端点
- `web_server/schemas.py` — pydantic schema (UiStateSchema / TorAcknowledgeRequest)

## 配置（YAML 注入）

- `config/m8_params.yaml`：
  - 自适应触发阈值（system_confidence drop / TDL 压缩阈值 — `[TBD-HAZID]`）
  - ToR deadline 60 s + SAT-1 强制等待 5 s（IMO MASS Code 合规）
- `config/m8_web_server.yaml` — FastAPI 端口 + CORS + WebSocket
- `config/m8_logger.yaml` — spdlog 配置

## 双进程拓扑（C++ + Python）

```
┌────────────────────────────┐    rclpy DDS     ┌──────────────────────────┐
│ HmiTransparencyBridgeNode  │ ───────────────→ │ Python FastAPI Backend   │
│ (C++ ROS2 — 实时 50 Hz)    │ /l3/m8/ui_state  │ • WebSocket 50 Hz 广播   │
│ • SAT 聚合 + ToR 状态机    │                  │ • REST POST /tor/ack     │
│ • ASDR 决策日志 + SHA-256  │ ←─────────────── │ • pydantic schema        │
└────────────────────────────┘   ToR ack ack    └──────────────────────────┘
                                                          ↓ HTTP/WS
                                                   ┌──────────────┐
                                                   │ ROC HMI 前端  │
                                                   └──────────────┘
```

## PATH-H 裁剪规则集

允许（PATH-D 之外的放宽）：
- 动态分配（HTTP 请求 / 响应天然动态）
- 异常（HTTP 框架普遍用异常）

仍保留：
- 函数 ≤ 60 行 + 循环复杂度 ≤ 10
- 浮点 `==` 禁用（用 `std::abs(a-b) < eps`）
- 角度 deg/rad 边界统一
- thread detach 禁用（所有 thread joinable）
- MISRA Rule 21.x（RAII，除 HTTP 框架内部）

## 开发命令

```bash
# Build (C++)
colcon build --packages-up-to m8_hmi_transparency_bridge \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
               -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -fsanitize=address,undefined"

# Unit tests (C++ GTest)
colcon test --packages-select m8_hmi_transparency_bridge

# Python tests (pytest)
cd src/m8_hmi_transparency_bridge/python
pip install -r requirements.txt
pytest tests/ --cov --cov-report=html

# Static analysis (PATH-H 裁剪集)
run-clang-tidy-20 -p build/m8_hmi_transparency_bridge \
                  -config-file=.clang-tidy \
                  -warnings-as-errors='*' \
                  src/m8_hmi_transparency_bridge/src/*.cpp

# Python lint
ruff check src/m8_hmi_transparency_bridge/python/
mypy src/m8_hmi_transparency_bridge/python/
```

## DoD（PATH-H）

参见 `docs/Implementation/M8/code-skeleton-spec.md` §10。关键阈值：

- 单测覆盖率 ≥ 80%（C++ + Python 各自）
- TorProtocol 状态机：SAT-1 ≥ 5 s + 60 s 超时全场景覆盖
- 自适应 SAT-2 触发：4 条件 + 边界用例
- ASDR 决策日志含 SHA-256 签名（独立 Sha256 实现）
- 跨进程通信：rclpy DDS bridge + WebSocket 50 Hz 验证
- 无 data race（已通过 Code Review 修复 — see commit 0088954）
- JSON injection 防御（已通过 Code Review 修复）

## 库白名单

**C++**（PATH-H 裁剪集）：
- rclcpp / l3_msgs / l3_external_msgs
- Eigen 5.0.0
- spdlog 1.17.0 / fmt 11.0.2
- nlohmann::json 3.12.0（HMI 渲染 + ASDR 序列化）
- yaml-cpp 0.8.0
- GTest 1.17.0

**Python 3.10**：
- rclpy（系统 ROS2 Jazzy）
- FastAPI 0.115.x
- uvicorn 0.30.x
- websockets 13.x
- pydantic 2.x
- pytest 8.3.x
- mypy 1.10.x / ruff 0.6.x

## 引用

- **架构基线**：[v1.1.2 §12 M8 + 决策三 SAT-1/2/3](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)
- **详细设计**：[M8/01-detailed-design.md](../../docs/Design/Detailed%20Design/M8-HMI-Transparency-Bridge/01-detailed-design.md)
- **代码骨架 spec**：[M8/code-skeleton-spec.md](../../docs/Implementation/M8/code-skeleton-spec.md)
- **RFC-004（ASDR 决策日志）**：[RFC-decisions.md](../../docs/Design/Cross-Team%20Alignment/RFC-decisions.md)
- **学术依据**：Chen ARL-TR-7180 SAT 三层框架 [R11] + Veitch 2024 TMR ≥ 60 s 实证 [R4] + USAARL 2026-02 + NTNU 2024 透明度悖论 [R5-aug]
- **IMO MASS Code MSC 110 Part 2**（C 交互验证 — "有意味的人为干预"）
