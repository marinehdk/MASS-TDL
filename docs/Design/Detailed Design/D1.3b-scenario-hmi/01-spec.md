# D1.3b · YAML 场景管理 + SIL 调试 HMI + 覆盖率自动报告 · 执行 Spec

**版本**：v1.0  
**日期**：2026-05-09  
**作者**：技术负责人（brainstorming 产出，待 /writing-plans 转执行计划）  
**v3.0 D 编号**：D1.3b  
**Owner**：技术负责人  
**完成日期**：2026-06-15 EOD（DEMO-1 当天）  
**工时预算**：3.0 人周（120h；5/18–6/15，20 个工作日）  
**关闭 finding**：G P0-G-1 (b) · G P1-G-4 · G P1-G-5 · P2-E1

---

## 0. 决策记录（brainstorming 锁定，不可在 D1.3b 内再议）

| 决策 | 内容 | Rationale |
|---|---|---|
| **批量执行器语义** | 几何验证 + 可解性验证（方案 A）；pass/fail = "场景设计是否正确"，非 M1/M6 决策正确性 | D1.3b 阶段 M1/M6 仅 stub；决策验证在 D2.4（M6 ≥500 场景 ≥98%） |
| **Own-ship 物理** | 完整 MMG C++ `rk4_step()`，精度优先 | 保留与 D1.3.1 Qualification Report 同一物理基线；扩展性强 |
| **C++ 集成** | pybind11 绑定，Python 控制场景流程 | Python 逐步调用 `step()`，未来 D2.x ROS2 集成可直接复用绑定层 |
| **SAT/ODD stub 来源** | 新建独立 `sil_mock_publisher` ROS2 节点 | `l3_external_mock_publisher` 保持"外部接口 mock"职责单一；D2.5 真实节点上线时零代码改动（topic 名不变，仅替换 launch 节点） |
| **HTML 报告工具** | Jinja2 静态模板（主体表格）；Plotly 可选（未来 D1.7 覆盖率扩展时加入） | D1.3b 无额外 Python 依赖；静态 HTML 可直接提交 git 作为证据 |
| **D2.5 切换点** | `sil_mock_publisher` 生命周期至 D2.5 SIL Integration（~7/31）；同 topic 名换 M1/M2 真实节点 | topic `/l3/sat/data`、`/l3/m1/odd_state` 名称不变，RosBridge 零改动 |

---

## 1. ≤60s Wall-clock 可行性分析（P2-E1 整改）

### 1.1 性能估算

| 参数 | 值 | 来源 |
|---|---|---|
| 仿真步长 | dt = 0.02 s | `scenario_runner.cpp` 默认值 |
| 目标仿真时长 | 600 s | DoD 要求 |
| 每场景步数 | 600 / 0.02 = 30,000 步 | 计算 |
| C++ RK4 步时间 | ~5 µs / step | D1.3a spec §5.2："~5s for 600s sim → ~100× 实时" |
| pybind11 Python→C++ 调用开销 | ~1–5 µs / call | pybind11 文档（直接函数调用，无 GIL 开销） |
| 每场景估算 wall-clock | (5 + 5) µs × 30,000 = **0.3 s** | 含 pybind11 overhead |
| 10 场景批量 wall-clock | ~3–5 s（含 IO、CPA 计算） | 估算 |

**结论**：≤60s 约束对 pybind11 方案**轻松满足**（0.3s << 60s）。**不需要 headless 模式**（绕过 ROS2 pub/sub 无必要，批量执行器本就不经过 ROS2）。

### 1.2 验收方法

```bash
# task T17 端到端集成时执行
python tools/sil/batch_runner.py --scenarios scenarios/colregs/ --output reports/
# 每个场景输出中检查 "wall_clock_s" 字段
jq '[.[] | .wall_clock_s] | max' reports/batch_results.json
# 期望: max < 60.0
```

---

## 2. 包结构

### 2.1 新增 / 修改文件清单

```
src/
├── fcb_simulator/                              ← 现有 C++ 包（扩展）
│   ├── python/
│   │   └── fcb_sim_py/
│   │       ├── bindings.cpp                    ← NEW: pybind11
│   │       └── __init__.py                     ← NEW
│   └── CMakeLists.txt                          ← 修改: +pybind11_add_module
│
├── sil_mock_publisher/                         ← NEW: Python ROS2 包
│   ├── sil_mock_publisher/
│   │   ├── __init__.py
│   │   └── sil_mock_node.py
│   ├── launch/
│   │   └── sil_mock.launch.py
│   ├── setup.py
│   └── package.xml
│
└── m8_hmi_transparency_bridge/python/
    └── web_server/
        ├── app.py                              ← 修改: +include_router(sil_router, sil_ws)
        ├── ros_bridge.py                       ← 修改: +订阅 /l3/sat/data + /l3/m1/odd_state
        ├── sil_router.py                       ← NEW: REST /sil/*
        ├── sil_ws.py                           ← NEW: WebSocket /ws/sil_debug
        └── sil_schemas.py                      ← NEW: pydantic SIL HMI models
    tests/
    └── test_sil_hmi.py                         ← NEW (现有 4 个测试文件不改动)

scenarios/
└── colregs/
    ├── schema.yaml                             ← NEW: YAML Schema 定义
    ├── colreg-rule14-ho-001-seed42-v1.0.yaml   ← NEW (×10)
    └── ...

tools/
└── sil/
    ├── batch_runner.py                         ← NEW
    ├── coverage_reporter.py                    ← NEW
    └── templates/
        └── coverage_report.html.j2             ← NEW

reports/                                        ← NEW（gitignore，运行时产物）
```

### 2.2 不改动文件列表（约束 G P1-G-5）

以下文件**禁止修改**（任何 D1.3b 实现须绕开它们）：

- `tests/test_websocket.py`
- `tests/test_app.py`
- `tests/test_schemas.py`
- `tests/test_tor_endpoint.py`
- `src/l3_external_mock_publisher/`（`l3_external_mock_publisher` 仅负责外部接口 mock，不扩展）

---

## 3. pybind11 绑定（`fcb_sim_py` 模块）

### 3.1 暴露接口

绑定目标：仅暴露批量执行器所需的最小接口，不暴露 ROS2 节点内部。

```cpp
// src/fcb_simulator/python/fcb_sim_py/bindings.cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace py = pybind11;
using namespace fcb_sim;

PYBIND11_MODULE(fcb_sim_py, m) {
    m.doc() = "FCB MMG simulator Python binding (D1.3b)";

    py::class_<FcbState>(m, "FcbState")
        .def(py::init<>())
        .def_readwrite("x",       &FcbState::x)        // East (m)
        .def_readwrite("y",       &FcbState::y)        // North (m)
        .def_readwrite("psi",     &FcbState::psi)      // heading, math conv (rad)
        .def_readwrite("u",       &FcbState::u)        // surge (m/s)
        .def_readwrite("v",       &FcbState::v)        // sway (m/s)
        .def_readwrite("r",       &FcbState::r)        // yaw rate (rad/s)
        .def_readwrite("phi",     &FcbState::phi)
        .def_readwrite("phi_dot", &FcbState::phi_dot);

    py::class_<MmgParams>(m, "MmgParams")
        .def(py::init<>());     // 默认值已在 types.hpp 定义，D1.3b 使用默认 FCB 参数

    m.def("rk4_step",
          &fcb_sim::rk4_step,
          py::arg("state"),
          py::arg("delta_rad"),   // 舵角: + = starboard (CW), - = port
          py::arg("n_rps"),       // 螺旋桨转速 (rev/s); 0 = 停车
          py::arg("params"),
          py::arg("dt"),
          "Single RK4 step. psi in math convention (0=East, π/2=North).");
}
```

### 3.2 坐标系约定

**重要**：`FcbState.psi` 使用**数学惯例**（CCW from East，rad）。YAML 场景文件使用**航海惯例**（CW from North，deg）。批量执行器在调用 `rk4_step()` 前转换：

```python
# nav_heading_deg → psi_math_rad
psi_math = math.pi / 2.0 - math.radians(nav_heading_deg)

# nav bearing (θ_nav_deg) + range_m → ENU position (dx_east, dy_north)
dx_east  = range_m * math.sin(math.radians(theta_nav_deg))
dy_north = range_m * math.cos(math.radians(theta_nav_deg))
```

### 3.3 CMakeLists.txt 扩展

```cmake
# src/fcb_simulator/CMakeLists.txt 新增（在 ament_package() 之前）
find_package(pybind11_vendor REQUIRED)   # ROS2 Jazzy 提供
find_package(pybind11 REQUIRED)

pybind11_add_module(fcb_sim_py
  python/fcb_sim_py/bindings.cpp
)
target_link_libraries(fcb_sim_py PRIVATE fcb_simulator_core Eigen3::Eigen)
install(TARGETS fcb_sim_py
  LIBRARY DESTINATION ${PYTHON_INSTALL_DIR})
```

### 3.4 Python smoke test

```python
# tools/sil/smoke_test_binding.py
import fcb_sim_py
s = fcb_sim_py.FcbState()
s.u = 6.17   # 12 kn
s.psi = math.pi / 2  # heading north
p = fcb_sim_py.MmgParams()
s2 = fcb_sim_py.rk4_step(s, 0.0, 3.5, p, 0.02)
assert s2.u > 0.0, "surge must remain positive"
assert abs(s2.psi - math.pi / 2) < 0.01, "heading must be stable"
```

---

## 4. YAML 场景 Schema

### 4.1 完整字段定义

```yaml
# scenarios/colregs/schema.yaml
# ── 本文件同时作为 JSON Schema 基准和人工可读的字段文档 ──

schema_version: "1.0"            # 字符串，必填；D1.6 扩展时升为 "2.0"
scenario_id: string              # 命名规范: <rule>-<odd>-<encounter>-v<ver>，eg. colreg-rule14-ho-001-v1.0
description: string              # 人工可读描述，必填
rule_branch_covered:             # 列表，D1.6 requirements_traced + hazid_id 将叠加此字段
  - string                       # eg. ["Rule14_HeadOn", "Rule8_Action"]
vessel_class: string             # pluginlib key，eg. "FCB"；multi_vessel_lint 扫描不到 YAML
odd_zone: string                 # "A" | "B" | "C" | "D"（架构 §5.2 ODD Zone 定义）

initial_conditions:
  own_ship:
    x_m: float                   # ENU East (m)，通常 0.0
    y_m: float                   # ENU North (m)，通常 0.0
    heading_nav_deg: float       # 航海惯例 heading（CW from North）；批量执行器转换为 psi_math
    speed_kn: float              # 初始合速度 (kn)；批量执行器换算 u_mps = speed_kn × 0.5144
    n_rps: float                 # 维持初速对应的螺旋桨转速 (rev/s)；见 §4.2 标定表
  targets:
    - target_id: int             # 唯一正整数
      x_m: float
      y_m: float
      cog_nav_deg: float         # 目标船的 Course Over Ground（航海惯例）
      sog_mps: float             # 目标船速 (m/s)；直线匀速传播（无 MMG）

encounter:
  rule: string                   # "Rule13" | "Rule14" | "Rule15_Stbd" | "Rule15_Port"
  give_way_vessel: string        # "own" | "target" | "none"
  expected_own_action: string    # "turn_starboard" | "turn_port" | "maintain" | "slow_down"
  avoidance_time_s: float        # 在此仿真时间点 own ship 施加避让动作
  avoidance_delta_rad: float     # 避让舵角（+ = starboard，rad；35° = 0.6109 rad）
  avoidance_duration_s: float    # 保持该舵角的持续时间 (s)

disturbance_model:               # G P1-G-4 整改字段
  wind_kn: float                 # 风速 (kn)；0.0 = calm；实现见 §6.3 扰动注入
  wind_dir_nav_deg: float        # 风向（来自方向，航海惯例）
  current_kn: float              # 流速 (kn)
  current_dir_nav_deg: float     # 流向（流往方向，航海惯例）
  vis_m: float                   # 能见度 (m)；< 2000 触发 Rule 19
  wave_height_m: float           # 有义波高 (m)；D1.3b 阶段不进入动力学，仅记录元数据

prng_seed: int                   # 必填，确保 Monte-Carlo 可复现；D1.3b 场景均为确定性（无随机扰动），seed 留作 D1.6 扩展

pass_criteria:
  max_dcpa_no_action_m: float    # 无动作 DCPA 必须 < 此值（确认存在碰撞风险，典型 926.0 m = 0.5 nm）
  min_dcpa_with_action_m: float  # 有动作 DCPA 必须 ≥ 此值（确认可解，典型 926.0 m = 0.5 nm）
  bearing_sector_deg: [float, float]  # [sector_start, sector_end]，目标船方位必须落在此扇区内

simulation:
  duration_s: float              # 仿真时长，必须 ≥ 600.0
  dt_s: float                    # 积分步长，必须 = 0.02（与 D1.3.1 基线一致）
```

### 4.2 n_rps 速度标定表

n_rps 与稳态速度的近似对应关系（FCB 默认 MMG 参数，仅用于 D1.3b 初值设定；D1.3.1 精确标定后更新）：

| 速度 (kn) | u_mps | n_rps (估算) |
|---|---|---|
| 8 | 4.12 | 2.3 |
| 10 | 5.14 | 3.0 |
| 12 | 6.17 | 3.5 |
| 14 | 7.20 | 4.2 |
| 15 | 7.72 | 4.5 |

批量执行器在无动作阶段使用**简单比例速度控制器**维持初速：
```python
n_rps += 0.1 * (u_target - state.u)   # P 控制，dt=0.02s
n_rps = max(0.0, min(10.0, n_rps))     # 限幅
```

---

## 5. 10 基础场景定义

坐标系：ENU（x=East, y=North），单位 m。航海惯例方位/航向（CW from North，deg）。
`0.5 nm = 926 m`，`1 nm = 1852 m`，`1 kn = 0.5144 m/s`。

### 5.1 场景几何汇总

| ID | Rule | 场景描述 | Own (hdg°, kn) | Target 初始 (x,y) m | Target (cog°, kn) | DCPA 无动作（估算）| TCPA 无动作（估算）| avoidance_time_s | seed |
|---|---|---|---|---|---|---|---|---|---|
| **HO-001** | 14 | 纯正对头，平静 | 0°, 12 | (0, 3704) | 180°, 10 | 0 m | 327 s | 200 s | 42 |
| **HO-002** | 14 | 近对头 8° 偏，风 5 kn | 0°, 12 | (515, 3668) | 188°, 10 | ~282 m | 327 s | 200 s | 43 |
| **HO-003** | 14/19 | 对头高速，能见度 1000 m | 0°, 15 | (0, 3704) | 180°, 12 | 0 m | 267 s | 150 s | 44 |
| **CS-001** | 15/16 | 右舷交叉 60°，own 让路 | 0°, 10 | (2406, 1389) | 270°, 12 | ~473 m | 341 s | 200 s | 10 |
| **CS-002** | 15/16 | 右舷交叉 90°，流 0.5 kn | 0°, 10 | (926, 0) | 270°, 12 | ~593 m | 89 s | **50 s** | 11 |
| **CS-003** | 15/16 | 左舷交叉，own 直航 | 0°, 10 | (−2778, 1389) | 70°, 12 | ~56 m | 475 s | 0 s (maintain) | 12 |
| **CS-004** | 15/16 | 近距交叉 45°，TCPA<200 s | 0°, 12 | (982, 982) | 225°, 14 | ~490 m | 105 s | **60 s** | 13 |
| **OT-001** | 13 | 纯追越（正后方），own 让路 | 0°, 14 | (50, 926) | 0°, 8 | ~50 m | ~300 s | 150 s | 20 |
| **OT-002** | 13 | 追越（左后方 162°），own 让路 | 0°, 14 | (−300, 926) | 0°, 9 | 300 m | 360 s | 200 s | 21 |
| **OT-003** | 13 | 追越（右后方 198°），风 8 kn | 0°, 14 | (300, 926) | 0°, 9 | 300 m | 360 s | 200 s | 22 |

> CS-002（TCPA≈89s）和 CS-004（TCPA≈105s）avoidance_time_s 需设为 TCPA 的约 55%，确保在 CPA 前完成动作。CS-003 为 own stand-on（维持航向，`give_way_vessel="target"`），avoidance_time_s=0 且 avoidance_delta_rad=0。

> DCPA/TCPA 为解析估算值；实际值由批量执行器精确计算并写入 JSON 输出。

### 5.2 扇区合规验证（Rule 映射）

批量执行器自动计算每个场景的目标船方位，校验是否落在 NLM `colav_algorithms` 笔记本确认的扇区（🟢 High confidence）：

| Rule | 扇区（从 own ship 看目标船） | D1.3b 场景覆盖 |
|---|---|---|
| Rule 14 Head-on | 345°–15° | HO-001, HO-002, HO-003 |
| Rule 15 Stbd Crossing | 15°–112.5° | CS-001 (60°), CS-002 (90°), CS-004 (45°) |
| Rule 15 Port Crossing | 247.5°–345° | CS-003 (297°) |
| Rule 13 Overtaking | 112.5°–247.5°（*目标船*视角，own ship 在此扇区内）| OT-001 (180°), OT-002 (162°), OT-003 (198°) |

> Rule 13 扇区判断：需从**目标船**参考系计算 own ship 的方位，而非 own ship 参考系中目标船方位。

### 5.3 disturbance_model 完整赋值

| ID | wind_kn | wind_dir° | current_kn | current_dir° | vis_m | wave_m |
|---|---|---|---|---|---|---|
| HO-001 | 0 | 0 | 0 | 0 | 10000 | 0 |
| HO-002 | 5 | 90 (E) | 0 | 0 | 5000 | 0.5 |
| HO-003 | 0 | 0 | 0 | 0 | 1000 | 0 |
| CS-001 | 0 | 0 | 0 | 0 | 10000 | 0 |
| CS-002 | 0 | 0 | 0.5 | 270 (W) | 10000 | 0 |
| CS-003 | 0 | 0 | 0 | 0 | 10000 | 0 |
| CS-004 | 0 | 0 | 0 | 0 | 10000 | 0 |
| OT-001 | 0 | 0 | 0 | 0 | 10000 | 0 |
| OT-002 | 0 | 0 | 0 | 0 | 10000 | 0 |
| OT-003 | 8 | 270 (W) | 0 | 0 | 10000 | 1.0 |

> D1.3b 阶段 `disturbance_model` 字段仅**记录元数据**，不进入 MMG 动力学（风/流力尚未集成至 C++ 层）。G P1-G-4 整改满足条件：字段已存在于 YAML schema，动力学集成在 D2.x 完成。

---

## 6. 批量执行器架构（`tools/sil/batch_runner.py`）

### 6.1 顶层流程

```
for each YAML in scenarios/colregs/:
  spec = load_and_validate_yaml(yaml_path)
  
  # Pass 1: No-action run
  result_noaction = simulate(spec, apply_avoidance=False)
  geometric_pass  = result_noaction.dcpa_m < spec.pass_criteria.max_dcpa_no_action_m
  bearing_pass    = bearing_in_sector(spec)
  
  # Pass 2: With-action run
  result_action   = simulate(spec, apply_avoidance=True)
  solvability_pass = result_action.dcpa_m >= spec.pass_criteria.min_dcpa_with_action_m
  
  # Stability check
  stability_pass   = result_action.stable  # no NaN/Inf in trajectory
  
  # Wall-clock
  wall_clock_pass  = result_action.wall_clock_s <= 60.0
  
  overall_pass = geometric_pass and bearing_pass and solvability_pass \
                 and stability_pass and wall_clock_pass
  
  write_json(output_dir, spec.scenario_id, {
    "result": "PASS" if overall_pass else "FAIL",
    "sub_checks": {...},
    "metrics": {...},
    "wall_clock_s": ...,
  })

generate_html_report(output_dir)
```

### 6.2 `simulate()` 内部流程

```python
def simulate(spec: ScenarioSpec, apply_avoidance: bool) -> SimResult:
    import fcb_sim_py
    
    # 初始化 own ship 状态（坐标系转换）
    state = fcb_sim_py.FcbState()
    state.x   = spec.initial_conditions.own_ship.x_m
    state.y   = spec.initial_conditions.own_ship.y_m
    state.psi = math.pi/2 - math.radians(spec.initial_conditions.own_ship.heading_nav_deg)
    state.u   = spec.initial_conditions.own_ship.speed_kn * 0.5144
    params    = fcb_sim_py.MmgParams()  # 默认 FCB 参数
    
    # 初始化目标船（直线匀速）
    targets = init_targets(spec)   # list of (x, y, vx, vy)
    
    n_rps     = spec.initial_conditions.own_ship.n_rps
    delta_rad = 0.0
    dt        = spec.simulation.dt_s
    n_steps   = int(spec.simulation.duration_s / dt)
    t_wall_start = time.perf_counter()
    
    own_traj, tgt_traj = [], []
    for i in range(n_steps):
        t_sim = i * dt
        
        # 避让控制
        if apply_avoidance and abs(t_sim - spec.encounter.avoidance_time_s) < dt/2:
            delta_rad = spec.encounter.avoidance_delta_rad
        if apply_avoidance and t_sim > spec.encounter.avoidance_time_s + spec.encounter.avoidance_duration_s:
            delta_rad = 0.0
        
        # 速度控制（简单 P 控制器，维持初始速度）
        n_rps += 0.1 * (u_target - state.u)
        n_rps = max(0.0, min(10.0, n_rps))
        
        # Own ship: MMG RK4
        state = fcb_sim_py.rk4_step(state, delta_rad, n_rps, params, dt)
        
        # Target ships: 直线外推
        targets = [(x + vx*dt, y + vy*dt, vx, vy) for x, y, vx, vy in targets]
        
        own_traj.append((state.x, state.y))
        tgt_traj.append([(x, y) for x, y, _, _ in targets])
        
        # 稳定性检测
        if not math.isfinite(state.u) or not math.isfinite(state.psi):
            return SimResult(stable=False)
    
    wall_clock = time.perf_counter() - t_wall_start
    dcpa_m, tcpa_s = compute_min_cpa(own_traj, tgt_traj)
    
    return SimResult(
        stable=True,
        dcpa_m=dcpa_m,
        tcpa_s=tcpa_s,
        wall_clock_s=wall_clock,
        own_trajectory=own_traj,
        target_trajectories=tgt_traj,
    )
```

### 6.3 扰动注入（D1.3b 元数据记录，不进入动力学）

D1.3b 阶段 `disturbance_model` 字段仅写入 JSON 输出，不修改 `rk4_step()` 调用。完整力学集成（风力/流力附加项）在 D2.x 完成，届时 YAML schema 无需改变。

### 6.4 CPA 计算

```python
def compute_min_cpa(own_traj, tgt_trajs):
    """Compute DCPA and TCPA over sampled trajectory."""
    min_d = float('inf')
    min_t = 0.0
    for i, (ox, oy) in enumerate(own_traj):
        for tgt in tgt_trajs:
            tx, ty = tgt[i]
            d = math.hypot(ox - tx, oy - ty)
            if d < min_d:
                min_d = d
                min_t = i * dt
    return min_d, min_t
```

---

## 7. JSON 输出格式

每个场景运行后生成 `reports/<scenario_id>-<timestamp>.json`：

```json
{
  "schema_version": "1.0",
  "scenario_id": "colreg-rule14-ho-001-v1.0",
  "scenario_yaml": "scenarios/colregs/colreg-rule14-ho-001-seed42-v1.0.yaml",
  "run_timestamp": "2026-06-01T09:00:00Z",
  "result": "PASS",
  "sub_checks": {
    "geometric_compliance": true,
    "bearing_sector": true,
    "solvability": true,
    "stability": true,
    "wall_clock_le_60s": true
  },
  "metrics": {
    "dcpa_no_action_m": 12.4,
    "dcpa_with_action_m": 1124.7,
    "tcpa_no_action_s": 327.2,
    "target_bearing_deg": 0.1,
    "own_max_heading_change_deg": 34.8,
    "own_min_speed_kn": 11.2
  },
  "performance": {
    "wall_clock_s": 0.31,
    "n_steps": 30000,
    "sim_duration_s": 600.0
  },
  "disturbance_recorded": {
    "wind_kn": 0.0,
    "current_kn": 0.0,
    "vis_m": 10000.0
  },
  "trajectory_points": 300
}
```

> `trajectory_points`：每隔 100 步（2 s）抽样存储 own ship 轨迹，共 300 点，控制文件大小。完整轨迹不写入 JSON（内存驻留，用于 CPA 计算后丢弃）。

---

## 8. COLREGs 覆盖率 HTML 报告

### 8.1 报告结构

Jinja2 模板生成单文件 `reports/coverage_report_<timestamp>.html`，包含：

1. **摘要表**：10 场景总体 PASS/FAIL 统计
2. **Rule × 场景 矩阵**（主体）：行 = COLREGs Rule，列 = 子检查项，单元格 = ✅/❌ + 链接
3. **未覆盖 Rule 列表**：Rule 5/6/7/8/9/17/19 标注"D1.3b 未覆盖，待 D1.6 扩展"
4. **场景详情折叠块**：每个场景展开显示 metrics + disturbance

### 8.2 矩阵结构

| Rule | 场景 ID | 几何合规 | 可解性 | 稳定性 | ≤60s | JSON 链接 |
|---|---|---|---|---|---|---|
| Rule 14 Head-on | HO-001 | ✅ | ✅ | ✅ | ✅ | [run.json](../reports/...) |
| Rule 14 Head-on | HO-002 | … | … | … | … | … |
| Rule 14 Head-on | HO-003 | … | … | … | … | … |
| Rule 15/16 Stbd | CS-001 | … | … | … | … | … |
| … | … | … | … | … | … | … |
| Rule 5/6/7/8/9/17/19 | — | ⚠️ 待 D1.6 | — | — | — | — |

每个 ✅/❌ 单元格超链接到对应 JSON 文件，满足"每个 cell 须链接到场景 YAML + 运行 JSON 输出"要求（HTML 报告可追溯，Finding G P0-G-1(b)）。

### 8.3 `coverage_reporter.py` 接口

```python
def generate_report(results_dir: Path, output_path: Path) -> None:
    """
    Load all JSON results from results_dir,
    render Jinja2 template, write HTML to output_path.
    """
```

---

## 9. SIL Mock Publisher（`sil_mock_publisher` 包）

### 9.1 发布的 topic

| Topic | 消息类型 | 频率 | 内容 |
|---|---|---|---|
| `/l3/sat/data` | `l3_msgs/msg/SATData` | 1 Hz | Stub SAT-1/2/3 数据（可解析 `UiStateSchema` 的所有字段）|
| `/l3/m1/odd_state` | `l3_msgs/msg/ODDState` | 1 Hz | Stub ODD 状态（zone=A, envelope=IN, confidence=0.9）|

### 9.2 sil_mock_node.py 关键实现

```python
class SilMockNode(rclpy.node.Node):
    def __init__(self):
        super().__init__("sil_mock_publisher")
        self._pub_sat = self.create_publisher(SATData, "/l3/sat/data", 10)
        self._pub_odd = self.create_publisher(ODDState, "/l3/m1/odd_state", 10)
        self._timer = self.create_timer(1.0, self._publish_tick)
        self._scenario_id: str = "idle"   # 由 /sil/scenario/run 更新（通过 ROS2 param）

    def _publish_tick(self):
        stamp = self.get_clock().now().to_msg()
        self._pub_sat.publish(self._make_sat_stub(stamp))
        self._pub_odd.publish(self._make_odd_stub(stamp))

    def _make_sat_stub(self, stamp) -> SATData:
        msg = SATData()
        msg.header.stamp = stamp
        msg.schema_version = "v1.1.2"
        msg.confidence = 0.8
        # SAT-1: 1 threat stub
        # SAT-2: reasoning chain stub
        # SAT-3: TDL/TMR stub
        return msg
```

### 9.3 D2.5 切换路径

```python
# launch/sil_stack.launch.py

# D1.3b–D2.4 阶段：
Node(package="sil_mock_publisher", executable="sil_mock_node", ...)

# D2.5 起，注释上行，取消注释下行（topic 名不变，HMI 零改动）：
# Node(package="m1_odd_envelope_manager", executable="odd_envelope_manager_node", ...)
# Node(package="m2_world_model", executable="world_model_node", ...)
```

---

## 10. SIL HMI 扩展（M8 前端复用，G P1-G-5）

### 10.1 app.py 修改

```python
# web_server/app.py — 仅添加两行，其余不改
from web_server.sil_router import router as sil_router
from web_server.sil_ws import router as sil_ws_router

def create_app(cors_origins: list[str]) -> FastAPI:
    app = FastAPI(...)
    app.add_middleware(CORSMiddleware, ...)
    app.include_router(tor_router, prefix="/api")
    app.include_router(ws_router)
    app.include_router(sil_router, prefix="/sil")     # NEW
    app.include_router(sil_ws_router)                 # NEW
    return app
```

### 10.2 `ros_bridge.py` 扩展

```python
# 在 RosBridge.start() 中添加（不改动现有发布方法）:
self._sub_sat = node.create_subscription(
    SATData, "/l3/sat/data", self._on_sat_data, 10)
self._sub_odd = node.create_subscription(
    ODDState, "/l3/m1/odd_state", self._on_odd_state, 10)
self.latest_sat: SATData | None = None
self.latest_odd: ODDState | None = None

def _on_sat_data(self, msg: SATData):
    self.latest_sat = msg
    asyncio.run_coroutine_threadsafe(
        sil_ws.broadcast_sil_state(self.latest_sat, self.latest_odd),
        self._loop
    )
```

### 10.3 sil_router.py 新增 endpoint

| Method | Path | 功能 |
|---|---|---|
| `GET` | `/sil/scenario/list` | 返回 `scenarios/colregs/` 下所有 YAML 文件名 |
| `POST` | `/sil/scenario/run` | 触发单个或批量场景异步执行，返回 `{"job_id": str}` |
| `GET` | `/sil/scenario/status/{job_id}` | 返回执行进度 `{"status": "running"\|"done"\|"failed", "progress": 0–100}` |
| `GET` | `/sil/report/latest` | 返回最新 HTML 报告文件内容（text/html）|

### 10.4 sil_ws.py — `/ws/sil_debug`

独立 WebSocket，不共享 `_active_clients`，不影响 `/ws/ui_state`：

```python
@router.websocket("/ws/sil_debug")
async def sil_debug_stream(ws: WebSocket) -> None: ...

async def broadcast_sil_state(sat: SATData | None, odd: ODDState | None) -> None:
    """Called by ros_bridge on each SAT/ODD update; pushes SilDebugSchema to HMI."""
```

### 10.5 sil_schemas.py

```python
class SilSAT1Panel(BaseModel):
    threat_count: int
    threats: list[Sat1ThreatSchema]   # 复用现有 schemas.py 的 Sat1ThreatSchema

class SilODDPanel(BaseModel):
    zone: str
    envelope_state: str
    conformance_score: float
    confidence: float

class SilDebugSchema(BaseModel):
    timestamp: datetime
    scenario_id: str
    sat1: SilSAT1Panel | None = None
    sat2_reasoning: str | None = None
    sat3_tdl_s: float = 0.0
    sat3_tmr_s: float = 0.0
    odd: SilODDPanel | None = None
    job_status: str = "idle"   # "idle" | "running" | "done"
```

---

## 11. Task 拆分（每 task ≤ 4h，共 23 tasks）

### Track A：pybind11 绑定

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T1** | fcb_simulator CMakeLists.txt 添加 pybind11_vendor + pybind11_add_module；`colcon build` 验证通过 | 4h | — |
| **T2a** | `bindings.cpp`：FcbState + MmgParams pybind11 类绑定；Python `import fcb_sim_py` 验证 | 3h | T1 |
| **T2b** | `bindings.cpp`：rk4_step 函数绑定 + smoke test（初速保持 + psi 稳定） | 3h | T2a |

### Track B：YAML 场景文件

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T3** | `schema.yaml` 字段定义 + Pydantic `ScenarioSpec` 模型（Python） | 3h | — |
| **T4a** | HO-001 / HO-002 / HO-003 三个 YAML 文件 + 方位扇区验证脚本 | 3h | T3 |
| **T4b** | CS-001 / CS-002 / CS-003 / CS-004 四个 YAML 文件 + 扇区验证 | 4h | T3 |
| **T4c** | OT-001 / OT-002 / OT-003 三个 YAML 文件 + Rule 13 扇区验证（目标船视角） | 3h | T3 |

### Track C：批量执行器 + 报告

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T5** | `simulate()` 函数：初始化 + RK4 步进循环 + target 直线外推 + CPA 计算 | 4h | T2b, T3 |
| **T6** | `simulate()` 扩展：避让动作注入 + 速度 P 控制器 + JSON 输出 | 4h | T5 |
| **T7** | `batch_runner.py` 主函数：遍历 YAML + 双 pass 运行 + 汇总结果 | 3h | T6 |
| **T8** | `coverage_report.html.j2` Jinja2 模板（Rule×场景矩阵 + cell 链接） | 3h | — |
| **T9** | `coverage_reporter.py`：加载 JSON 结果 + 渲染模板 + 写 HTML | 2h | T7, T8 |

### Track D：SIL Mock Publisher

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T10** | `sil_mock_publisher` 包脚手架（package.xml + setup.py + `__init__.py`） | 2h | — |
| **T11** | `sil_mock_node.py`：SATData + ODDState stub 发布 + launch file | 4h | T10 |

### Track E：M8 SIL HMI

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T12** | `sil_schemas.py`：SilSAT1Panel + SilODDPanel + SilDebugSchema pydantic 模型 | 2h | — |
| **T13** | `ros_bridge.py` 扩展：订阅 /l3/sat/data + /l3/m1/odd_state，推 asyncio queue | 3h | T11, T12 |
| **T14** | `sil_ws.py`：/ws/sil_debug WebSocket + `broadcast_sil_state()` | 3h | T13 |
| **T15** | `sil_router.py`：4 个 REST endpoint + 异步 job 管理（asyncio.create_task）| 4h | T7, T12 |
| **T16** | `app.py` 添加两行 include_router + 现有测试不回归验证 | 1h | T14, T15 |
| **T17** | `test_sil_hmi.py`：≥10 单元测试（list, run, status, report, ws 推送）| 4h | T14, T15, T16 |

### Track F：集成

| ID | Task | 工时 | 依赖 |
|---|---|---|---|
| **T18** | 端到端：10 场景全量运行 + wall-clock 验证 + HTML 报告生成确认 | 3h | T7, T9 |
| **T19** | SIL HMI 集成：launch file + mock publisher + M8 web server 联调 | 3h | T11, T16 |
| **T20** | DoD 全项验证 + finding 关闭证据文档（log + 截图 + HTML 报告 commit）| 3h | T18, T19 |

**Task 总计**：20 tasks，约 63h optimistic / ~90h realistic（含调试）。3.0pw=120h 预算内，余量 30h 覆盖风险。

---

## 12. Owner-by-Day 矩阵（5/18–6/15，20 工作日）

| 日期 | Track A (pybind11) | Track B (YAML) | Track C (批量+报告) | Track D (Mock) | Track E (HMI) | Track F (集成) |
|---|---|---|---|---|---|---|
| **5/18 (一)** | T1 | T3 | — | — | — | — |
| **5/19 (二)** | T2a | T4a | — | — | — | — |
| **5/20 (三)** | T2b | T4b | — | — | — | — |
| **5/21 (四)** | — | T4c | T5 | — | — | — |
| **5/22 (五)** | — | — | T5 (续) | — | — | — |
| **5/25 (一)** | — | — | T6 | T10 | T12 | — |
| **5/26 (二)** | — | — | T6 (续) | T11 | T13 | — |
| **5/27 (三)** | — | — | T7 | — | T13 (续) | — |
| **5/28 (四)** | — | — | T8 | T11 (续) | T14 | — |
| **5/29 (五)** | — | — | T9 | — | T14 (续) | — |
| **6/1 (一)** | — | — | — | — | T15 | — |
| **6/2 (二)** | — | — | — | — | T15 (续) | — |
| **6/3 (三)** | — | — | — | — | T16 | T18 |
| **6/4 (四)** | — | — | — | — | T17 | T18 (续) |
| **6/5 (五)** | — | — | — | — | T17 (续) | T19 |
| **6/8 (一)** | — | — | — | — | — | T19 (续) |
| **6/9 (二)** | — | — | — | — | — | T20 |
| **6/10 (三)** | **缓冲** | **缓冲** | **缓冲** | **缓冲** | **缓冲** | **缓冲** |
| **6/11 (四)** | **缓冲** | **缓冲** | **缓冲** | **缓冲** | **缓冲** | **缓冲** |
| **6/12 (五)** | **DEMO-1 预演** | | | | | |
| **6/15 (一)** | **DEMO-1 当天 / DoD 验收** | | | | | |

缓冲 6/10–6/12 用于：风险 R4.1（pybind11 超时）/ R4.2（FastAPI 环境冲突）修复；DEMO-1 预演至少提前 1 个工作日完成。

---

## 13. 依赖图

```
T1 ────► T2a ──► T2b ──────────────────────► T5 ──► T6 ──► T7 ──► T9 ──► T18
T3 ──────────────────────────────────────────► T5        │         │
T4a/b/c ────────────────────────────────────────────────────────────────────────
                                                          └──► T15 ──► T16 ──► T17
T10 ──► T11 ────────────────────────────────────────────────────────────► T19
T12 ─────────────────────────────────────────────────────► T13 ──► T14 ──►┘ │
T8 ──► T9 ──────────────────────────────────────────────────────────────── T18

并行路径：
  • Track A (T1-T2) 与 Track B (T3-T4) 完全并行
  • Track D (T10-T11) 与 Track C (T5-T9) 完全并行
  • Track E (T12-T17) 依赖 T2b (pybind11) 和 T11 (mock publisher)
  • Track F (T18-T20) 最后，依赖所有 track
```

---

## 14. 每个 Task 的 Acceptance Criteria

| ID | Acceptance Criteria |
|---|---|
| T1 | `colcon build --packages-select fcb_simulator` 成功；`python -c "import fcb_sim_py"` 无报错 |
| T2a | `fcb_sim_py.FcbState()` 可构造；`FcbState.x = 1.0` 可赋值；`fcb_sim_py.MmgParams()` 可构造 |
| T2b | smoke test 通过：rk4_step 调用后 u > 0、psi 保持稳定；测试文件 `tools/sil/smoke_test_binding.py` 跑通 |
| T3 | `ScenarioSpec.model_validate(yaml.safe_load(...))` 可解析本文档中的 schema 示例；所有必填字段缺失时 Pydantic 报错 |
| T4a | HO-001/002/003 YAML 文件通过 `ScenarioSpec` 验证；目标船方位经脚本验证落在 Rule 14 扇区 [345°,15°] |
| T4b | CS-001/002 目标船方位 ∈ [15°,112.5°]；CS-003 方位 ∈ [247.5°,345°]；CS-004 方位 ∈ [15°,112.5°] |
| T4c | OT-001/002/003 从目标船视角计算 own ship 方位 ∈ [112.5°,247.5°] |
| T5 | HO-001 无动作运行：dcpa_no_action_m < 50 m；OT-001 无动作运行：dcpa_no_action_m < 100 m；无 NaN |
| T6 | HO-001 有动作运行：dcpa_with_action_m > 926 m；wall_clock_s < 60；JSON 文件写出可解析 |
| T7 | 10 个场景全量运行成功，输出 10 个 JSON 文件；batch 总 wall-clock < 120 s |
| T8 | HTML 模板渲染后 Rule 14 行有 3 个场景单元格；每个 PASS 单元格有超链接指向 JSON 文件 |
| T9 | `reports/coverage_report_*.html` 存在且可浏览器打开；Rule 13/14/15/16 全覆盖行可见 |
| T10 | `colcon build --packages-select sil_mock_publisher` 成功 |
| T11 | `ros2 topic hz /l3/sat/data` 显示 ~1 Hz；`ros2 topic hz /l3/m1/odd_state` 显示 ~1 Hz |
| T12 | `SilDebugSchema.model_validate({...})` 可解析含 sat1 + odd 的完整字典 |
| T13 | `ros_bridge.latest_sat` 在 mock publisher 运行时不为 None；broadcast 被调用 |
| T14 | `/ws/sil_debug` WebSocket 连接后，在 mock publisher 运行时每 ~1 s 收到一条 SilDebugSchema JSON |
| T15 | `GET /sil/scenario/list` 返回 10 个文件名；`POST /sil/scenario/run` 返回 job_id；`GET /sil/report/latest` 返回 HTML |
| T16 | 现有 4 个测试文件（test_websocket/test_app/test_schemas/test_tor_endpoint）**全部 PASS**，无修改 |
| T17 | `pytest tests/test_sil_hmi.py` 全绿，覆盖 ≥10 个测试函数 |
| T18 | 10 × JSON 输出存在；所有场景 result="PASS"；max(wall_clock_s) < 60；HTML 报告可访问 |
| T19 | launch 文件启动后 /ws/sil_debug 实时推送 SilDebugSchema；手动触发 POST /sil/scenario/run 后 JSON 生成 |
| T20 | DoD 4 项全部满足（见§18）；finding 关闭文档写入 `docs/Design/Review/2026-05-07/finding-closure/D1.3b.md` |

---

## 15. Demo Charter（DEMO-1，2026-06-15）

**Scenario**：在 DEMO-1 现场展示 SIL 调试 HMI 实时运行 + 批量 10 场景覆盖率报告。

**Audience × View**：
- 技术负责人 + 项目负责人：SIL 工具链完整可用、DEMO-1 milestone 交付
- CCS 审查员（可选远程）：场景管理可追溯、COLREGs 覆盖 Rule 13/14/15/16、HTML 报告 per-cell 链接到 JSON
- V&V 工程师：D1.3b 批量框架可直接扩展为 D1.6 CI 三层集（Smoke 10 / Nightly 200 / Weekly 1000+）

**Visible Success**：
1. 浏览器打开 SIL HMI，/ws/sil_debug 实时显示 SAT-1/2/3 stub 数据 + ODD 状态（更新 ~1 Hz）
2. 点击"Run All Scenarios"按钮，控制台输出 10 个场景逐一完成，total wall-clock < 30 s
3. "View Report"打开 HTML 报告，Rule 13/14/15/16 行全绿，未覆盖 Rule 5/6/7/8/9 有⚠️标注
4. 点击任意 ✅ 单元格跳转至对应 JSON（含 dcpa_no_action/dcpa_with_action/wall_clock_s 字段）

**Showcase Bundle**：
- `reports/coverage_report_20260615.html`（静态文件，可离线查看）
- `reports/batch_results.json`（汇总 JSON，供 CCS 技术审查）
- 浏览器截图 × 3（HMI 实时面板 + 报告主体 + 单个场景 JSON 详情）

---

## 16. Risk + Contingency

| ID | 风险 | 概率 | 影响 | 缓解方案 |
|---|---|---|---|---|
| **R4.1** | pybind11 与 ROS2 Jazzy ament_cmake 集成配置复杂，T1 超出 4h | Medium | -0.5pw | 缓冲期 6/10–6/12 消化；若 T1 超 8h → 降级方案：改用 subprocess 调用 C++ 可执行文件（仅影响 T5 重写，不影响 YAML/HMI track）|
| **R4.2** | M8 前端 `fastapi==0.115.6` 与 ROS2 Jazzy Python 环境冲突（pip vs apt） | Low-Medium | Track E 全停 | 用 Python venv 隔离 FastAPI 层；ROS2 节点单独 sourced；D1.3a 已有类似问题的 venv 方案可复用 |
| **R4.3** | OT-001/002/003 Rule 13 扇区验证逻辑（目标船视角）实现有误 | Low | T4c 返工 | T4c AC 要求显式打印从目标船计算的 own ship 方位，手工核对后再 commit |
| **R4.4** | n_rps P 控制器在高速场景（CS-004 14kn 目标）导致 own ship 速度振荡，DCPA 计算失真 | Low | T6 返工 | 控制器增益 0.1 经 D1.3a 验证（停船场景）；高速场景实测后调整；备选：固定 n_rps 不用控制器 |
| **R4.5** | sil_mock_publisher 发布的 SATData stub 字段与 M8 `ros_bridge.py` 解析逻辑不一致 | Low | T13 返工 | T11 完成后立即用 `ros2 topic echo /l3/sat/data` 核对字段；T13 AC 明确要求 broadcast 被调用验证 |

---

## 17. 与 D1.3b 相关的 Finding 关闭路径

| Finding ID | 内容 | D1.3b 关闭条件 |
|---|---|---|
| **G P0-G-1 (b)** | YAML 场景管理 | 10 个 schema-compliant YAML 文件 commit + HTML 覆盖率报告可访问 |
| **G P1-G-4** | 无统一 stochastic environment layer | YAML schema 中 `disturbance_model` 字段存在（含 wind/current/vis/wave）；元数据已记录于 JSON 输出；完整动力学集成在 D2.x |
| **G P1-G-5** | SIL HMI 与 M8 HMI 复用关系未定 | SIL HMI 通过 `sil_router` + `sil_ws` 扩展 M8 前端；不新建独立 Web 应用；D3.4 共享组件库 stub |
| **P2-E1** | 单场景 ≤ 60s wall-clock | 所有 10 个场景 `wall_clock_s < 60`；evidence: batch_results.json `performance.wall_clock_s` 字段 |

---

## 18. D1.3b 全闭判据（6/15 EOD，DEMO-1 当天）

- [ ] `scenarios/colregs/` 下 10 个 YAML 文件存在，通过 `ScenarioSpec` schema 验证
- [ ] `reports/coverage_report_20260615.html` 存在，Rule 13/14/15/16 覆盖行全绿
- [ ] `max(wall_clock_s)` from `batch_results.json` < 60 s
- [ ] `/ws/sil_debug` WebSocket 实时推送 SAT-1/2/3 + ODD（mock publisher 运行时）
- [ ] `POST /sil/scenario/run` 可触发单个场景执行并返回 job_id
- [ ] 现有 4 个 M8 测试文件全部 PASS
- [ ] `pytest tests/test_sil_hmi.py` 全绿（≥10 个测试）
- [ ] Finding 关闭文档写入 `docs/Design/Review/2026-05-07/finding-closure/D1.3b.md`
- [ ] 关闭 finding ID：G P0-G-1 (b) + G P1-G-4 + G P1-G-5 + P2-E1

---

## 19. 下游 D 接口

| 下游 D | 消费 D1.3b 哪些产物 | 所需扩展 |
|---|---|---|
| **D1.3.1** Simulator Qualification Report | 复用 `batch_runner.py` 的 `simulate()` 函数；对 STRAIGHT_DECEL / TURN / ZIG_ZAG 三类机动运行参考解 | D1.3.1 向 `simulate()` 传入不同 `ScenarioSpec`（ManeuverType 场景）；不需改动 batch_runner 核心 |
| **D1.6** 场景库 schema + 追溯矩阵 | 在 D1.3b `schema.yaml` 基础上扩展 3 个字段：`requirements_traced[]`、`hazid_id[]`、`vessel_class_applicable[]`；D1.3b 10 个 YAML 文件全部 schema-compliant | D1.6 升 schema_version 到 "2.0"；D1.3b YAML 文件回填新字段（~4h）|
| **D1.7** 覆盖率方法论 | 在 D1.3b `coverage_reporter.py` 基础上扩展覆盖率计算脚本（11 Rule × 4 ODD × 5 扰动级别 = 220 cell）| D1.7 扩展 Jinja2 模板行维度；batch_runner 读取 ODD zone / disturbance 字段 |
| **D2.x** M1/M2 实装接入 | D1.3b `sil_mock_publisher` 直接替换为真实节点（topic 名不变）；`ros_bridge.py` 零改动；SIL HMI 前端零改动 | D2.5 SIL Integration 时修改 launch file |
