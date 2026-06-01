# Spec: Scenario Trace Fast-Verify System + Container Stabilization

**Date**: 2026-06-01  
**Status**: Approved  
**Scope**: SIL debug toolchain — does NOT touch L3 kernel logic (M1–M8)

---

## 1. Problem Statement

调试避碰链（colreg-rule14-ho、imazu-01-ho）时存在两类摩擦：

1. **容器不稳定**：worktree 目录名变化导致 docker compose project name 漂移，OrbStack 积累多套堆栈；`_e2e_helpers.py` 里 `CONTAINER` 硬编码失效；每次验证流程不固定（有时 rebuild、有时 restart）。
2. **数据获取慢**：`docker exec ros2 topic echo` 串行采样，慢且不可靠；没有结构化的 M3/M4/M5 接口快照；agent 无法在对话中直接读取数值。

**目标**：

- 任何 Python 改动 → `npm run sil:restart`（30s）→ `npm run test:trace`→ 终端结构化输出，全程不需要 `docker exec`
- 容器名固定，OrbStack 不再积累多套堆栈
- Agent 可通过 `GET /api/v1/debug/snapshot` 在一次 REST 调用中拿到所有关键接口数值

---

## 2. Architecture Overview

```
宿主机
  ├── npm run sil:restart      → docker compose restart（Python 改动）
  ├── npm run sil:rebuild      → docker compose build（C++ / Dockerfile 改动）
  ├── npm run test:trace       → pytest tools/sil/test_scenario_trace.py
  └── npm run sil:prune        → 清理悬空镜像

sil-nodes 容器（network_mode: host）
  └── sil_topic_bridge.py
        └── [新增] DebugTraceWriter
              ├── 订阅 7 个关键话题
              └── 写 → /var/sil/runs/current/trace.jsonl

sil-orchestrator 容器（network_mode: host）
  └── FastAPI
        └── [新增] routers/debug.py
              ├── GET /api/v1/debug/trace?last_n=N   ← 读 trace.jsonl
              ├── GET /api/v1/debug/snapshot          ← 每话题最新一条
              └── GET /api/v1/debug/summary           ← 派生摘要（阶段时间线 + 轨迹统计）

共享卷：./runs:/var/sil/runs（已有）
```

---

## 3. Component Specifications

### 3.1 docker-compose.yml 变更

#### 3.1.1 固定 project name

在文件顶层（services 之前）添加：
```yaml
name: mass-l3-sil
```

效果：容器名永远为 `mass-l3-sil-sil-nodes-1` / `mass-l3-sil-sil-orchestrator-1`，与运行目录无关。

#### 3.1.2 热重载 volume mounts

`sil-nodes` 新增挂载（补充到现有 volumes 列表）：
```yaml
- ./docker:/opt/ws/docker   # shadow COPYed docker scripts；改 Python → restart 即生效
```

`sil-orchestrator` 新增挂载：
```yaml
- ./src/sil_orchestrator:/opt/sil/sil_orchestrator   # shadow COPYed orchestrator；改 Python → restart 即生效
```

注：`./src:/opt/ws/src` 在 sil-nodes 已有，L3 kernel Python（--symlink-install）天然热重载，**不需新增**。

#### 3.1.3 改动决策树（SOP）

```
改动类型？
 ├── Python only
 │    ├── docker/*.py                →  npm run sil:restart
 │    ├── src/l3_tdl_kernel/**/*.py  →  npm run sil:restart  （symlink-install 已实时）
 │    └── src/sil_orchestrator/**   →  npm run sil:restart:orch
 └── C++ / Dockerfile / pip deps    →  npm run sil:rebuild
```

### 3.2 package.json 新增脚本

| 命令 | 等价操作 | 场景 |
|---|---|---|
| `sil:restart` | `docker compose restart sil-nodes sil-orchestrator` | Python 改动后 |
| `sil:restart:orch` | `docker compose restart sil-orchestrator` | 只改了 orchestrator |
| `sil:rebuild` | `docker compose build sil-nodes && docker compose up -d` | C++ / Dockerfile 变更 |
| `sil:rebuild:orch` | `docker compose build sil-orchestrator && docker compose up -d sil-orchestrator` | orchestrator Dockerfile 变更 |
| `sil:prune` | `docker image prune -f && docker builder prune -f --keep-storage 5GB` | 清理 OrbStack 悬空镜像 |
| `sil:logs` | `docker compose logs --tail=100 -f sil-nodes sil-orchestrator` | 实时查看日志 |
| `test:trace` | `pytest tools/sil/test_scenario_trace.py -v -s` | 跑 trace 验证 |

### 3.3 DebugTraceWriter（`docker/sil_topic_bridge.py`）

新增类 `DebugTraceWriter`，在 `SilTopicBridge.__init__` 末尾实例化。

#### 3.3.1 订阅话题

| 话题 | 消息类型 | 记录字段 |
|---|---|---|
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | `task_validity`, `target_wp_lat`, `target_wp_lon`, `schema_version` |
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | `phase`, `cmd_heading_deg`, `cmd_sog_kn`, `rationale` |
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | `solver_status`, `waypoints[:3]`（前三点经纬度）, `confidence` |
| `/sil/own_ship_state` | `l3_external_msgs/FilteredOwnShipState` | `heading_deg`, `sog_kn`, `lat`, `lon`, `rot_deg_s` |
| `/l3/fsm_state` | `sil_msgs/LifecycleStatus` | `state` |
| `/sil/scoring` | `sil_msgs/*` | 完整 msg（体积小） |
| `/l3/checker/veto` | `l3_msgs/*` | `veto_active`, `reason` |

#### 3.3.2 写入格式

每条消息追加写入 `/var/sil/runs/current/trace.jsonl`，格式：
```json
{"sim_t": 234.5, "wall_t": 1717000000.123, "topic": "/l3/m4/behavior_plan", "phase": "AVOIDANCE", "cmd_heading_deg": 32.1, "cmd_sog_kn": 8.0, "rationale": "Rule14 starboard turn"}
```

#### 3.3.3 容量控制

- 内存中每话题维护 `collections.deque(maxlen=2000)` 作为 ring buffer
- 每 **2 秒** 批量 flush 一次（threading.Timer，不阻塞 ROS2 callback）
- 文件超过 **50 MB** 时自动 rotate（重命名为 `trace_<timestamp>.jsonl.gz` 压缩归档）
- 场景 cleanup 时关闭 writer（`close()` 方法）

#### 3.3.4 sim_t 来源

调用 `SilTopicBridge` 已有的 `self._get_sim_time()` 方法（该方法已在 `/sim_clock` callback 中维护 sim clock state）。

### 3.4 REST Debug API（`src/sil_orchestrator/routers/debug.py`）

新建路由模块，在 `main.py` 中 `include_router`。

#### 3.4.1 端点列表

**`GET /api/v1/debug/trace`**

参数：`last_n: int = 500`（URL query param）

返回：`{"records": [...], "total_in_file": int, "scenario_id": str}`

实现：读 `/var/sil/runs/current/trace.jsonl` 最后 `last_n` 行，JSONL → list。

---

**`GET /api/v1/debug/snapshot`**

无参数。

返回：每话题各取最新一条，合并为单个 dict：
```json
{
  "sim_t": 412.3,
  "m3_mission_goal": {"task_validity": 1, "target_wp_lat": 60.123, ...},
  "m4_behavior_plan": {"phase": "AVOIDANCE", "cmd_heading_deg": 33.5, ...},
  "m5_avoidance_plan": {"solver_status": "OPTIMAL", ...},
  "own_ship_state": {"heading_deg": 31.2, "sog_kn": 8.1, "lat": 60.120, "lon": 5.015},
  "fsm_state": {"state": "active"},
  "last_veto": {"veto_active": false, "reason": ""}
}
```

实现：从 trace.jsonl 逆序扫描，每话题各找最新一条。

---

**`GET /api/v1/debug/summary`**

无参数。派生摘要，供 agent 一次性定位问题：
```json
{
  "scenario_id": "colreg-rule14-ho",
  "sim_duration_s": 650.0,
  "m3_validity_timeline": [
    {"from_sim_t": 0, "to_sim_t": 650, "task_validity": 0, "target_wp": [0.0, 0.0]}
  ],
  "m4_phase_timeline": [
    {"phase": "TRANSIT", "from_sim_t": 0, "to_sim_t": 245},
    {"phase": "AVOIDANCE", "from_sim_t": 245, "to_sim_t": 520},
    {"phase": "TRANSIT", "from_sim_t": 520, "to_sim_t": 650}
  ],
  "m5_solver_stats": {
    "total_plans": 850,
    "OPTIMAL": 0,
    "INFEASIBLE": 850,
    "convergence_rate_pct": 0.0
  },
  "own_ship_trajectory_sampled": [
    {"sim_t": 0, "lat": 60.100, "lon": 5.000, "hdg_deg": 0.0, "sog_kn": 8.0},
    ...
  ],
  "max_starboard_turn_deg": 33.2,
  "max_turn_sim_t": 312.0,
  "veto_events": []
}
```

实现：对 trace.jsonl 全量扫描，O(N) 一次完成。

### 3.5 pytest Trace Harness（`tools/sil/test_scenario_trace.py`）

#### 3.5.1 CLI 选项

在 `conftest.py` 注册 `--scenario`（默认 `colreg-rule14-ho`）。

#### 3.5.2 Fixture

```python
@pytest.fixture(scope="module")
def summary(scenario_id):
    _post("/lifecycle/cleanup")
    _post("/lifecycle/configure", {"scenario_id": scenario_id})
    _post("/lifecycle/activate")
    _wait_until_sim_t(700, timeout_wall_s=900)
    return _get("/debug/summary")
```

#### 3.5.3 测试项

| 测试函数 | 断言内容 | 失败时输出 |
|---|---|---|
| `test_m3_task_validity` | `task_validity != 0` 至少出现一次（有 VALID 阶段） | M3 task_validity 时间线 |
| `test_m4_entered_avoidance` | AVOIDANCE 阶段存在，duration > 10s | M4 阶段时间线 |
| `test_starboard_turn_magnitude` | `max_starboard_turn_deg` ∈ [20, 50] | 实际最大转向角 |
| `test_m5_convergence_rate` | `convergence_rate_pct` > 0 | M5 solver stats（INFEASIBLE 数量）|
| `test_min_cpa_500m` | `/scoring/last_run` min_cpa_m ≥ 500 | 实际 min CPA |
| `test_route_return` | TRANSIT 阶段在 AVOIDANCE 之后出现 | M4 阶段时间线 |

#### 3.5.4 _e2e_helpers.py 修复

将 `CONTAINER = "mass-l3-tacticallayer-sil-nodes-1"` 改为 `CONTAINER = "mass-l3-sil-sil-nodes-1"`（与固定 project name 对齐）。

---

## 4. Interface Contracts

### 4.1 trace.jsonl 文件位置

| 路径 | 容器内 | 宿主机 |
|---|---|---|
| 当前场景 trace | `/var/sil/runs/current/trace.jsonl` | `./runs/current/trace.jsonl` |
| 归档 | `/var/sil/runs/current/trace_<ts>.jsonl.gz` | `./runs/current/trace_*.jsonl.gz` |

### 4.2 容器命名（固定后）

| 服务 | 容器名 |
|---|---|
| sil-nodes | `mass-l3-sil-sil-nodes-1` |
| sil-orchestrator | `mass-l3-sil-sil-orchestrator-1` |
| foxglove-bridge | `mass-l3-sil-foxglove-bridge-1` |
| martin-tile-server | `mass-l3-sil-martin-tile-server-1` |

---

## 5. Out of Scope

- L3 kernel 模块内部逻辑（M1–M8）不在此 spec 范围
- rosbag2 录制/回放集成（后续 D 任务）
- CI/CD 集成（后续阶段）
- 多场景并行运行

---

## 6. Implementation Order

1. `docker-compose.yml`：固定 project name + 热重载 volume（**无需 rebuild 后即可验证热重载效果**）
2. `package.json`：新增 npm 脚本
3. `_e2e_helpers.py`：修复 CONTAINER 硬编码
4. `sil_topic_bridge.py`：添加 `DebugTraceWriter`
5. `routers/debug.py` + `main.py`：注册 debug 路由
6. `tools/sil/test_scenario_trace.py`：pytest harness + conftest `--scenario` 选项

每步完成后用 `npm run sil:restart` 验证，不需要全量 rebuild。
