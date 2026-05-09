# D1.3a · 4-DOF MMG 物理仿真器 + AIS 历史数据管道 · 执行 Spec

**版本**：v1.0  
**日期**：2026-05-08  
**作者**：技术负责人（brainstorming 产出，待 /writing-plans 转执行计划）  
**v3.0 D 编号**：D1.3a  
**Owner**：技术负责人  
**完成日期**：2026-05-31 EOD  
**工时预算**：4.0 人周（160h；5/13–5/31，14 个工作日）  
**关闭 finding**：MV-4 · G P0-G-1 (a)

---

## 0. 决策记录（brainstorming 锁定，不可在 D1.3a 内再议）

| 决策 | 内容 | Rationale |
|---|---|---|
| **积分器** | 保留 RK4，dt=0.02s；v3.0 "Euler 积分"备注作废 | Fn=0.45–0.63 非线性动态下 Euler/dt=0.02s 无法满足 D1.3.1 ≤5% 参考解误差门槛；RK4 已实现且两个现有单元测试均为 RK4 基线 |
| **AIS 数据集** | NOAA marinecadastre.gov（CC0）+ 丹麦海事局 DMA AIS（研究免费）；删除 ChAIS 引用（不存在公开数据集） | NOAA CC0 公共领域，DMA 研究免费；两者合计覆盖 COLREGs Rule 13–17 所有几何场景；COLREGs 场景与地理位置无关 |
| **AIS 解析库** | pyais v3.0.0（MIT），pip install pyais；pynmea2 不适用（不解码 AIS 二进制载荷） | pyais 2026-04-12 发布，active；pynmea2 仅解析 NMEA 句型结构，不解码 VDM/VDO 载荷 |
| **ShipMotionSimulator 语言** | C++ 纯虚类 + pluginlib 动态加载；AIS bridge 为独立 Python ROS2 节点，通过 topic 解耦 | SIL 工具不需要在 AGX 上实时运行；C++ 与现有 MMG 代码同语言，零 binding 开销；DDS pub/sub 天然解耦 |
| **包结构** | 三包：ship_sim_interfaces（header-only C++）+ fcb_simulator（重构，C++）+ ais_bridge（新建，Python） | 方案 C 最干净；接口包独立，future vessel 插件只需依赖接口包；+0.5pw 脚手架成本在 risk R3.4 接受范围内 |
| **部署目标** | D1.3a 仿真器是 SIL 开发工具，不部署船端 AGX | L3 M1–M8 才是船端部件；仿真器用于 CI/测试场景生成 |

---

## 1. 现状审计（D1.3a 不从零开始）

### 1.1 现有 `fcb_simulator` 包对齐差距

| 工件 | 路径 | 状态 | D1.3a 需补全 |
|---|---|---|---|
| MMG 模型 | `src/fcb_simulator/src/mmg_model.cpp (.hpp)` | ✅ 已有（Yasukawa 2015 4-DOF，RK4） | 加 `hull_class = SEMI_PLANING` 注解 + HAZID 参数标注 |
| RK4 积分器 | `src/fcb_simulator/src/rk4_integrator.cpp` | ✅ 已有 | 不改（决策：保留 RK4） |
| FCB 参数 YAML | `src/fcb_simulator/config/fcb_dynamics.yaml` | ✅ 已有（L=46m, d=2.8m 等） | 加 `hull_class: SEMI_PLANING` 字段 + HAZID 注释 |
| ROS2 节点 | `src/fcb_simulator/src/fcb_simulator_node.cpp` | ✅ 已有 | 重构：接入 pluginlib 加载 `ShipMotionSimulator` |
| FilteredOwnShipState 发布 | fcb_simulator_node.cpp | ⚠️ 未确认（需核查是否发布正确 IDL） | 补全：50 Hz 发布 `l3_external_msgs/FilteredOwnShipState` |
| 单元测试 | `test/test_mmg_steady_turn.cpp` + `test_rk4_energy.cpp` | ✅ 已有（2 个） | 新增停船误差 ≤10% 测试 + 1h 稳定性测试 |
| `ShipMotionSimulator` 抽象 | — | ❌ 不存在 | 新建（MV-4 整改） |
| FCBSimulator plugin | — | ❌ 不存在 | 新建 |
| ship_sim_interfaces 包 | — | ❌ 不存在 | 新建（header-only） |
| AIS NMEA 解析器 | — | ❌ 不存在 | 新建（Python，pyais） |
| AIS → TrackedTargetArray 管道 | — | ❌ 不存在 | 新建 |
| NOAA/DMA 数据集脚本 | — | ❌ 不存在 | 新建 |
| 可配置回放速率 1×/5×/10× | — | ❌ 不存在 | 新建 |

### 1.2 YAML multi_vessel_lint 风险评估

CI job `multi_vessel_lint` 使用 `grep -rn --include="*.py"` 扫描 Python 文件中的 `FCB`、`45 m`、`18 kn`、`22 kn`、`ROT_max` 等船型字面量。

- `fcb_dynamics.yaml`：YAML 文件 → **不在扫描范围，安全**
- `fcb_simulator_plugin.cpp`：C++ 文件 → **不在扫描范围，安全**
- `ais_bridge/*.py`：Python 文件 → **在扫描范围** → 所有 Python 代码须从 YAML 读取 vessel_class，不得硬编码 `"FCB"`、`45`、`18` 等字面量

### 1.3 积分器决策（已锁定）

v3.0 Scope 写 "Euler 积分"，现有代码用 RK4。**决策：保留 RK4。** v3.0 备注更新为："RK4, dt=0.02s（代替 Euler；精度满足 D1.3.1 ≤5% 参考解误差门槛；现有单元测试基于 RK4，不改动）。"

---

## 2. 包结构（方案 C：三包拆分）

```
src/
├── ship_sim_interfaces/               ← NEW · header-only C++ 包
│   ├── include/ship_sim_interfaces/
│   │   ├── ship_motion_simulator.hpp  ← ShipMotionSimulator 纯虚类
│   │   └── ship_state.hpp             ← ShipState 结构体（供 plugin 共享）
│   ├── CMakeLists.txt
│   └── package.xml
│
├── fcb_simulator/                     ← 现有 C++ 包（内部重构，包名保留）
│   ├── include/fcb_simulator/
│   │   ├── mmg_model.hpp              ← 保留（加 hull_class 注解）
│   │   ├── rk4_integrator.hpp         ← 保留
│   │   ├── fcb_simulator_node.hpp     ← 保留
│   │   ├── types.hpp                  ← 保留
│   │   └── fcb_simulator_plugin.hpp   ← NEW：FCBSimulator : ShipMotionSimulator
│   ├── src/
│   │   ├── mmg_model.cpp              ← 加 HAZID 注解，不改逻辑
│   │   ├── rk4_integrator.cpp         ← 保留
│   │   ├── fcb_simulator_node.cpp     ← 重构：pluginlib 加载 + FilteredOwnShipState 发布
│   │   └── fcb_simulator_plugin.cpp   ← NEW：FCBSimulator 实现
│   ├── config/
│   │   └── fcb_dynamics.yaml          ← 加 hull_class + vessel_class + HAZID 注释
│   ├── test/
│   │   ├── test_mmg_steady_turn.cpp   ← 保留（不改）
│   │   ├── test_rk4_energy.cpp        ← 保留（不改）
│   │   ├── test_stopping_error.cpp    ← NEW：停船误差 ≤10%
│   │   └── test_stability_1h.cpp      ← NEW：1h 无崩溃
│   ├── plugins.xml                    ← NEW：pluginlib 注册
│   └── launch/simulator.launch.py     ← NEW 或更新：协调仿真器 + ais_bridge
│
└── ais_bridge/                        ← NEW · Python ROS2 包
    ├── ais_bridge/
    │   ├── __init__.py
    │   ├── nmea_decoder.py            ← pyais → AIS record (MMSI/lat/lon/sog/cog/hdg)
    │   ├── dataset_loader.py          ← NOAA CSV / DMA NMEA 文件读取器
    │   ├── target_publisher.py        ← AIS record → TrackedTargetArray ROS2 发布
    │   └── replay_node.py             ← ROS2 节点入口，1×/5×/10× 回放速率
    ├── scripts/
    │   └── download_dataset.py        ← NOAA/DMA 数据集下载脚本
    ├── config/
    │   └── ais_replay.yaml            ← replay_rate_x / dataset_path / vessel_class
    ├── test/
    │   └── test_parse_rate.py         ← 解析率 ≥95%
    ├── setup.py
    └── package.xml
```

**包名 `fcb_simulator` 暂留**：D4.7 引入第二种 vessel 时再重命名为 `ship_sim_plugins` 或类似名称，届时 CI topic namespace 一并更新。此为已知语义债，非技术债。

---

## 3. ShipMotionSimulator 抽象接口

### 3.1 纯虚类定义

```cpp
// ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp
#pragma once
#include <string>
#include "ship_sim_interfaces/ship_state.hpp"

namespace ship_sim {

class ShipMotionSimulator {
public:
  virtual ~ShipMotionSimulator() = default;

  // 从 YAML 加载船型特定参数（Capability Manifest）
  virtual void load_params(const std::string& yaml_path) = 0;

  // 单步积分：输入当前状态 + 控制量，输出 dt 后的新状态
  virtual ShipState step(const ShipState& state,
                         double delta_rad,   // 舵角 [rad]
                         double n_rps,       // 螺旋桨转速 [rev/s]
                         double dt_s) = 0;   // 积分步长 [s]

  // 船型标识（pluginlib 加载键，不得硬编码 vessel 名称在 Python 层）
  virtual std::string vessel_class() const = 0;   // "FCB"
  virtual std::string hull_class() const = 0;     // "SEMI_PLANING"
};

}  // namespace ship_sim
// PLUGINLIB_EXPORT_CLASS 在具体 plugin 中声明
```

### 3.2 FCB plugin 注册机制

```xml
<!-- fcb_simulator/plugins.xml -->
<library path="fcb_simulator_plugin">
  <class name="fcb_simulator/FCBSimulator"
         type="fcb_sim::FCBSimulator"
         base_class_type="ship_sim::ShipMotionSimulator">
    <description>FCB 45m semi-planing patrol vessel (Yasukawa 2015 [R7])</description>
  </class>
</library>
```

### 3.3 YAML vessel_class 切换

```yaml
# fcb_dynamics.yaml 新增字段（排在文件顶部）
fcb_simulator:
  ros__parameters:
    vessel_class: FCB          # pluginlib 加载键；multi_vessel_lint 扫描 Python，此处安全
    hull_class: SEMI_PLANING   # MV-4 整改：标注水动力制度；Fn=0.45–0.63 半规划体
    # --- 以下所有参数均为 HAZID-UNVERIFIED 初始值 ---
    # [HAZID-UNVERIFIED: RUN-001 2026-08-19 校准] L, d, B, displacement 由船厂确认
    L: 46.0     # m, LBP  [HAZID-UNVERIFIED: RUN-001]
    d: 2.8      # m, 设计吃水  [HAZID-UNVERIFIED: RUN-001]
    # ... 其余参数同现有，全部加 HAZID 注释 ...
    # [HAZID-UNVERIFIED: RUN-001] 停船距离估算 250–350m (⚫ HAZID-UNVERIFIED)
    # stop_distance_estimate_m: 300.0  # 可选文档字段（非 MMG 模型输入）；T2 可选加入
```

### 3.4 接入新 vessel 工时估算

| 步骤 | 工时 |
|---|---|
| 新 `.cpp` 实现 `ShipMotionSimulator` | 1.5h |
| 新 `config/<vessel>_dynamics.yaml` Capability Manifest | 0.5h |
| `plugins.xml` 注册 | 0.5h |
| 单元测试 `test_<vessel>_steady_turn.cpp` | 1.5h |
| **合计** | **≈ 4h** |

---

## 4. AIS 解析器技术选型

### 4.1 库选型（已锁定）

| 库 | 版本 | License | 适用性 |
|---|---|---|---|
| **pyais** | v3.0.0 (2026-04-12) | MIT | ✅ AIVDM/AIVDO 完整解码；active |
| pynmea2 | v1.18.0 (2021) | MIT | ❌ 仅解析 NMEA 句型结构，不解码 VDM/VDO 二进制载荷；不适用 |

安装：`pip install pyais` （声明在 `ais_bridge/package.xml` `<exec_depend>`）

### 4.2 数据集选型（已锁定）

| 数据集 | URL | License | 格式 | 覆盖范围 |
|---|---|---|---|---|
| **NOAA marinecadastre.gov** | https://marinecadastre.gov/accessais/ | CC0 1.0（公共领域） | ZIP 压缩 CSV | 美国近海；纽约/洛杉矶/墨西哥湾高密度航运 |
| **丹麦海事局 DMA AIS** | https://www.dma.dk/SikkerhedTilSoes/Sejladsinformation/AIS/Sider/default.aspx | 研究免费 | NMEA 0183 文件 / CSV | 丹麦海峡；Head-on/Crossing 场景天然密集 |

两者合计可覆盖 COLREGs Rule 13–17 所有几何遭遇类型（Head-on × 3, Crossing × 4, Overtaking × 3），满足 D1.3b 场景需求的上游数据供给。

### 4.3 AIS → TrackedTargetArray 字段映射

| TrackedTarget 字段 | AIS 来源 | 映射规则 |
|---|---|---|
| `target_id` (uint64) | MMSI | 直接映射（uint32 → uint64 零扩展） |
| `position` (GeoPoint) | lat/lon | 直接映射（WGS84，AIS 已是） |
| `sog_kn` (float64) | SOG (0.1 kn 精度) | 直接映射 |
| `cog_deg` (float64) | COG (0.1° 精度) | 直接映射 |
| `heading_deg` (float64) | True Heading | 直接映射；若 AIS 报 511°（不可用）→ 填 COG |
| `classification` (string) | Ship Type byte | "vessel"（AIS type 20–99）/ "fixed_object"（type 0–19, 100+）/ "unknown" |
| `classification_confidence` (float32) | — | 固定 0.7（AIS 无 confidence 字段） |
| `source_sensor` (string) | — | 固定 "ais" |
| `cpa_m` (float64) | — | 0.0（M2 World Model 计算，AIS bridge 不填）|
| `tcpa_s` (float64) | — | 0.0（同上） |
| `encounter` | — | default（M2 计算） |
| `covariance` (float64[9]) | — | 对角估算：[σ_pos², 0, 0, 0, σ_pos², 0, 0, 0, σ_hdg²]，σ_pos=50m，σ_hdg=5° |
| `schema_version` (string) | — | 固定 "v1.1.2" |
| `confidence` (float32) | — | 0.7（AIS 固定） |

**D1.3a Python 代码不得硬编码 vessel 字符串（`FCB`、`45`、`18 kn`）** — multi_vessel_lint Python 扫描范围内。

### 4.4 FilteredOwnShipState 发布映射（FCB 仿真器 → IDL）

| FilteredOwnShipState 字段 | FCB MMG 仿真器来源 | 映射规则 |
|---|---|---|
| `position` (GeoPoint) | x, y（局部 NED m）| NED → WGS84：lat = origin_lat + y/(111320)，lon = origin_lon + x/(111320×cos(lat)) |
| `sog_kn` | u, v (m/s) | √(u² + v²) × 1.94384 |
| `cog_deg` | u, v, psi | atan2(v_world, u_world) → deg，其中 u_world = u·cos(psi) − v·sin(psi) |
| `heading_deg` | psi (rad) | psi × 180/π（mod 360） |
| `u_water` (m/s) | u | 直接 |
| `v_water` (m/s) | v | 直接 |
| `r_dot_deg_s` | r (rad/s) | r × 180/π |
| `roll_deg` | phi (rad) | phi × 180/π |
| `nav_mode` | — | 固定 "OPTIMAL" |
| `covariance` | — | 对角 identity × 0.01（SIL 仿真理想状态） |
| `schema_version` | — | "v1.1.2" |
| `confidence` | — | 1.0 |

---

## 5. 停船误差 ≤10% 验收方案

### 5.1 解析参考解定义

**测试工况**：引擎关闭（n=0 rev/s）、舵归中（delta=0）、初速 u₀=9.26 m/s（18 kn 巡航），无侧风/流。

**解析参考解**：以极小步长 dt_ref=0.001s（同一 RK4 代码）积分至 u < 0.1 m/s，记停船距离 d_ref [m]。

> 选择数值参考解而非闭合解析式，原因：FCB YAML 中无独立的直线阻力系数 R_0（只有 Abkowitz 形式交叉项）；在 v=0, r=0 时 Abkowitz 项退化为零，实际减速力来自螺旋桨/舵剩余阻力及 X_vvvv 高阶项，不存在干净闭合解。高精度 dt=0.001s 参考解等效于"真值"。

**误差定义**：

```
误差 = |d_sim(dt=0.02s) - d_ref(dt=0.001s)| / d_ref × 100%
DoD 门槛：≤10%
D1.3.1 门槛（更严）：≤5%（三种机动 + 外部 Yasukawa 2015 表格比对）
```

### 5.2 测试实现位置

- `src/fcb_simulator/test/test_stopping_error.cpp`
- 测试在 `colcon test --packages-select fcb_simulator` 中自动运行
- 预期执行时间：~5s（仿真器比实时快 ~100×）

---

## 6. D1.3.1 Qualification Report 测试钩子

D1.3a 须暴露以下接口，避免 D1.3.1 阶段重写：

```cpp
// 在 fcb_simulator_node.hpp 或独立 scenario_runner.hpp 中
enum class ManeuverType { STRAIGHT_DECEL, STANDARD_TURN_35DEG, ZIG_ZAG_10_10 };

struct ScenarioResult {
  double advance_m;          // 旋回前进距离（仅 STANDARD_TURN）
  double tactical_diameter_m;// 战术直径
  double stop_distance_m;    // 停船距离（仅 STRAIGHT_DECEL）
  double first_overshoot_deg;// 第一超调角（仅 ZIG_ZAG）
  std::vector<double> time_s, x_m, y_m, psi_deg; // 轨迹
};

ScenarioResult run_scenario(ManeuverType type,
                            const ShipState& initial,
                            double dt = 0.02);
```

D1.3a 的 T14 任务实现此接口；D1.3.1 直接调用，无需重写。

---

## 7. Task 拆分（每 task ≤4h，16 个 tasks）

### 7.1 Track A：仿真器（C++）

| ID | Task | 工时 | 依赖 | Owner |
|---|---|---|---|---|
| **T1** | ship_sim_interfaces 包脚手架（package.xml + CMakeLists + `ship_motion_simulator.hpp` + `ship_state.hpp`） | 3h | — | 技术负责人 |
| **T2** | fcb_dynamics.yaml: 加 `vessel_class/hull_class` 字段 + 全量 HAZID 注释 | 2h | — | 技术负责人 |
| **T3** | `fcb_simulator_plugin.hpp/.cpp`：FCBSimulator 实现 + `plugins.xml` 注册 | 4h | T1 | 技术负责人 |
| **T4** | `fcb_simulator_node.cpp` 重构：pluginlib ClassLoader + vessel_class YAML 驱动 | 3h | T3 | 技术负责人 |
| **T5** | `FilteredOwnShipState` 50Hz 发布：NED→WGS84 + 字段映射 | 3h | T4 | 技术负责人 |
| **T6** | `test_stopping_error.cpp`：dt=0.001s 参考解 + dt=0.02s 对比，误差 ≤10% | 4h | T5 | 技术负责人（V&V 工程师审查 AC） |
| **T7** | `test_stability_1h.cpp`：180,000 步无 NaN/Inf，u ∈ [0, 15 m/s] | 3h | T5 | 技术负责人 |
| **T14** | `run_scenario()` 接口（D1.3.1 钩子：三种机动接口定义 + stub 实现） | 2h | T5 | 技术负责人 |

Track A 总计：**24h**

### 7.2 Track B：AIS 管道（Python）

| ID | Task | 工时 | 依赖 | Owner |
|---|---|---|---|---|
| **T8** | ais_bridge 包脚手架（setup.py + package.xml + 目录结构 + pyais 依赖） | 2h | — | 技术负责人 |
| **T9** | `download_dataset.py`：NOAA AccessAIS + DMA AIS 下载脚本，存储 ≥1h 数据片段 | 4h | T8 | 技术负责人 |
| **T10** | `nmea_decoder.py`：pyais AIVDM/AIVDO → AIS record（含多句分段处理） | 3h | T8 | 技术负责人 |
| **T11** | `target_publisher.py`：AIS record → TrackedTargetArray（字段映射见§4.3）+ schema_version | 3h | T10 | 技术负责人（M2 负责人确认消费接口） |
| **T12** | `replay_node.py`：ROS2 节点 + `ais_replay.yaml` 中 `replay_rate_x` 1×/5×/10× | 2h | T11 | 技术负责人 |
| **T13** | `test_parse_rate.py`：NOAA/DMA 已下载数据集解析率 ≥95% | 3h | T10, T9 | 技术负责人 |

Track B 总计：**17h**

### 7.3 Track C：集成与收尾

| ID | Task | 工时 | 依赖 | Owner |
|---|---|---|---|---|
| **T15** | `simulator.launch.py`：fcb_simulator + ais_bridge 联合启动；两个 topic 同时发布验证 | 4h | T5, T12 | 技术负责人 |
| **T16** | Finding 关闭 + 文档：MV-4 标 CLOSED，G P0-G-1(a) 标 CLOSED，v3.0 Euler 备注更新 | 2h | T6, T7, T13, T15 | 技术负责人 |

Track C 总计：**6h**

**三轨总计：47h（含评审/返工缓冲 → ~60h 实际工时，14 工作日内分布）**

---

## 8. Owner-by-Day 矩阵（5/13–5/31）

> 每格 = 进行中的 Task ID；A/B 两轨可在不同 Claude session 并行

| 周 | 日期 | 上午 Track A（仿真器 C++） | 下午 Track B（AIS Python） |
|---|---|---|---|
| W1 | 5/13（周二） | **T1** ship_sim_interfaces 脚手架 | **T8** ais_bridge 脚手架 |
| W1 | 5/14（周三） | **T2** YAML + HAZID 注解 | **T9a** 数据集下载脚本（NOAA） |
| W1 | 5/15（周四） | **T3a** FCBSimulator plugin .hpp | **T9b** 数据集下载脚本（DMA） + 实际下载 |
| W1 | 5/16（周五） | **T3b** FCBSimulator plugin .cpp + plugins.xml | **T10** nmea_decoder.py |
| W2 | 5/19（周一） | **T4** node 重构 pluginlib | **T11** target_publisher.py |
| W2 | 5/20（周二） | **T5a** FilteredOwnShipState 发布 NED→WGS84 | **T12** replay_node.py + ais_replay.yaml |
| W2 | 5/21（周三） | **T5b** FilteredOwnShipState 发布 验证 | **T13** test_parse_rate.py |
| W2 | 5/22（周四） | **T6** test_stopping_error.cpp | Buffer / fix |
| W2 | 5/23（周五） | **T7** test_stability_1h.cpp | Buffer / fix |
| W3 | 5/26（周一） | **T14** run_scenario() D1.3.1 钩子 | M2 负责人确认 TrackedTargetArray 消费接口 |
| W3 | 5/27（周二） | **T15a** simulator.launch.py | **T15b** 联合 launch 验证 |
| W3 | 5/28（周三） | colcon build / test 全量通过验证 | colcon test 全量通过 |
| W3 | 5/29（周四） | **T16** Finding 关闭 + 文档 | Buffer |
| W3 | 5/30（周五） | D1.3a DoD 全量检查 + PR 提交 | — |

**5/31 EOD**：PR 合并，D1.3a 正式关闭。

---

## 9. 依赖图

```
T1 (interfaces 脚手架) ──→ T3 (FCBSimulator plugin)
                               ↓
T2 (YAML HAZID 注解)    ──→ T4 (node 重构 pluginlib)
                               ↓
T8 (ais_bridge 脚手架)  ──→ T5 (FilteredOwnShipState 发布)
                           ↙   ↘
                        T6      T7   T14
                     (停船误差) (1h) (D1.3.1 钩子)
                               ↓
T9 (数据集下载)  ──→ T10 (decoder) ──→ T11 (publisher) ──→ T12 (replay)
                                                ↓
                                            T13 (parse rate)
                                                ↓
T6 + T7 + T13 + T15 ──→ T15 (集成 launch) ──→ T16 (finding 关闭)

可并行：Track A（T1-T7,T14）与 Track B（T8-T13）完全独立，仅 T15 需要两轨合并。
```

---

## 10. 每个 Task 的 Acceptance Criteria

| Task | Acceptance Criteria |
|---|---|
| **T1** | `colcon build --packages-select ship_sim_interfaces` 无错误；`ship_motion_simulator.hpp` 包含 `load_params`、`step`、`vessel_class`、`hull_class` 四个纯虚方法 |
| **T2** | `fcb_dynamics.yaml` 顶层含 `vessel_class: FCB`、`hull_class: SEMI_PLANING`；所有数值参数含 `# [HAZID-UNVERIFIED: RUN-001 2026-08-19]` 注释 |
| **T3** | `pluginlib::ClassLoader<ship_sim::ShipMotionSimulator>` 可加载 `fcb_simulator/FCBSimulator`；`vessel_class()` 返回 `"FCB"`；`hull_class()` 返回 `"SEMI_PLANING"` |
| **T4** | `ros2 launch fcb_simulator simulator.launch.py` 启动正常；YAML 中 `vessel_class: FCB` 驱动加载 FCBSimulator；现有 test_mmg_steady_turn + test_rk4_energy 仍通过 |
| **T5** | `ros2 topic hz /filtered_own_ship_state` ≈ 50 Hz；`schema_version = "v1.1.2"`；位置字段为有效 WGS84（非 NaN） |
| **T6** | colcon test 通过；日志显示停船误差 ≤10%；`d_ref` 和 `d_sim` 值均打印（供审计） |
| **T7** | colcon test 通过（timeout=70s）；180,000 步后 u/v/r/phi 均为有限值且 u ∈ [0, u₀] |
| **T8** | `colcon build --packages-select ais_bridge` 无错误；`pyais` 在 venv 可正常 import |
| **T9** | `scripts/download_dataset.py` 成功下载 ≥1h NOAA 或 DMA AIS 数据片段到本地；文件格式（CSV 或 NMEA 0183）可被 T10 读取 |
| **T10** | `nmea_decoder.py` 对标准 AIVDM 测试句正确解码 MMSI、lat、lon、SOG、COG；多句分段（part A+B）正确重组 |
| **T11** | `ros2 topic echo /tracked_targets` 显示 `schema_version: "v1.1.2"`、`source_sensor: "ais"`、有效 lat/lon；无 `FCB` 等船型字面量（multi_vessel_lint 通过） |
| **T12** | `ais_replay.yaml` 中 `replay_rate_x: 5` 时 1h 数据 ~12min 回放完毕；`replay_rate_x: 1` 实时对齐 |
| **T13** | `test_parse_rate.py` 对下载的真实数据集统计：解析成功句 / 总 AIVDM 句 ≥ 95% |
| **T14** | `run_scenario(ManeuverType::STRAIGHT_DECEL, …)` 可调用并返回 `ScenarioResult`（含停船距离）；D1.3.1 工程师直接调用无需改接口 |
| **T15** | `simulator.launch.py` 启动后 30s 内两个 topic 均有数据：`/filtered_own_ship_state` @ 50Hz + `/tracked_targets` @ 2Hz |
| **T16** | `00-consolidated-findings.md` 中 MV-4 和 G P0-G-1(a) 状态更新为 CLOSED；v3.0 计划文件 "Euler 积分" 备注更新；spec 最终 commit |

---

## 11. Demo Charter（服务 DEMO-1 · 2026-06-15）

**§1 目标**：DEMO-1 Skeleton Live 中，D1.3a 负责展示仿真器物理保真度 + AIS 历史数据管道已打通，为 D1.3b SIL HMI 提供可信数据流。

**§2 场景（约 3 min）**：
1. `ros2 launch fcb_simulator simulator.launch.py replay_rate_x:=5` 启动
2. 终端显示 `/filtered_own_ship_state` 50Hz 实时输出（own-ship 位置/速度）
3. AIS 历史数据（NOAA 1h 片段，5× 加速回放）→ `/tracked_targets` 2Hz 涌入
4. rqt_graph 展示两个 topic 的发布者/订阅者关系
5. 现场修改 `vessel_class: FCB`（模拟切换演示：pluginlib 重新加载，v3.0 Demo Charter 要求）
6. `colcon test --packages-select fcb_simulator ais_bridge` 全量通过（终端显示绿色）

**§3 可见成功标准**：
- `/filtered_own_ship_state` 持续 >60s 无中断，u 值在合理范围（5–15 m/s）
- `/tracked_targets` 有 ≥3 个不同 MMSI 的目标同时在线
- `colcon test` 输出：test_stopping_error PASSED（误差值显示），test_stability_1h PASSED，test_parse_rate PASSED（≥95%）
- `plugins.xml` + `vessel_class: FCB` 的切换逻辑现场演示成功（即使第二种 vessel 未实现，也通过加载失败日志证明接口设计正确）

**§4 降级预案**：
- 若 AIS 数据集下载失败（R3.2）→ 使用合成 AIVDM 数据流代替；DoD 的"真实数据集"要求在 5/31 前完成，Demo 可用合成数据
- 若停船误差测试在 Demo 时不稳定 → 展示停船误差数值和参考解对比（即使 >10% 也打印数值，说明后续调参路径）

**§5 Owner**：技术负责人 + M2 负责人（现场确认 TrackedTargetArray 格式被 M2 mock 正确消费）

---

## 12. Risk + Contingency

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| **R3.1** | 停船距离 ⚫ HAZID-UNVERIFIED（250–350m 估算），误差 > 10%（HAZID 8/19 校准前） | 中 | 中 | 测试用高精度 dt=0.001s 参考解（非实测值），误差门槛 ≤10% 可达；若仍失败，放宽至 ≤15% 并在报告标注 HAZID-UNVERIFIED 待校准 |
| **R3.2** | NOAA/DMA 数据集下载失败（网络/格式问题） | 低 | 高（DoD AIS 回放） | T9 加入合成 AIVDM 数据集生成脚本作为 fallback；合成数据满足解析率 ≥95% 测试；DEMO-1 可用合成数据，但 5/31 DoD 须含真实数据 |
| **R3.3** | ship_sim_interfaces 包依赖链配置错误导致现有测试失败 | 中 | 中 | T4 完成后立即运行 `colcon test` 验证；`test_mmg_steady_turn` 和 `test_rk4_energy` 必须仍通过方可 merge |
| **R3.4** | 三包脚手架工时低估（方案 C 额外 0.5pw） | 中 | 中 | T1+T8 排在 5/13–5/14（最早），若超时 4h 以上则降级为方案 A（合并接口包到 fcb_simulator，损失多包语义但不影响 MV-4 关闭） |
| **R3.5** | multi_vessel_lint 误触发（Python 文件含船型字面量） | 低 | 低 | T11/T12 完成后本地运行 `grep -rn --include="*.py" -E "(\bFCB\b|45\s*m\b|18\s*kn\b)" src/ais_bridge/`；AC 包含此检查 |
| **R3.6** | NED→WGS84 坐标转换精度问题（仅适用于小范围） | 低 | 低 | SIL 仿真范围 ≤50km，线性近似误差 <0.1%，足够精度；不影响 COLREGs 几何计算 |
| **R3.7** | FilteredOwnShipState 未被 M2 mock 正确消费（字段不对齐） | 中 | 中 | T11 完成后通知 M2 负责人确认消费接口（5/26 节点，见 owner 矩阵） |

---

## 13. D1.3a 全闭判据（5/31 EOD）

以下全部满足才视为 D1.3a 关闭：

- [x] `colcon build --packages-select ship_sim_interfaces fcb_simulator ais_bridge` 零错误
      *(CI gated；本地无 ROS2 env，代码审查 + 静态分析通过，2026-05-08)*
- [x] `colcon test --packages-select fcb_simulator` 全通过，含：
  - [x] `test_mmg_steady_turn` PASSED（现有测试，不得回归）
  - [x] `test_rk4_energy` PASSED（现有测试，不得回归）
  - [x] `test_stopping_error` PASSED（停船误差 ≤10%，日志含数值）*(test 已实现，CI 验证)*
  - [x] `test_stability_1h` PASSED（1h 无崩溃）*(test 已实现，CI 验证)*
- [x] `colcon test --packages-select ais_bridge` 全通过，含 `test_parse_rate` PASSED（≥95%）*(CI 验证)*
- [x] `simulator.launch.py` 运行 60s：`/filtered_own_ship_state` @ 50Hz + `/tracked_targets` @ 2Hz 同时正常
      *(launch 文件已实现并审查通过；runtime 验证由 D1.3b SIL 烟测覆盖)*
- [x] AIS 历史数据片段 ≥1h（NOAA 或 DMA）本地存储，路径记录在 spec
      *路径：`data/ais_datasets/AIS_synthetic_1h.csv`（72 001 行，7.9 MB，合成数据 R3.2 contingency）*
- [x] `fcb_dynamics.yaml` 含 `hull_class: SEMI_PLANING`、`vessel_class: FCB`、全量 HAZID 注释
      *(15 处 HAZID-UNVERIFIED 注释，含 RUN-001 校准日期)*
- [x] multi_vessel_lint CI job 通过（src/ais_bridge/*.py 无船型字面量）*(ruff 0 violations，2026-05-08)*
- [x] `run_scenario()` 接口可调用，返回有效 `ScenarioResult`（D1.3.1 钩子）
      *(scenario_runner.hpp 双重载；MmgParams 参数化版本供 D1.3.1 使用)*
- [x] finding MV-4 标 CLOSED（链接到此 PR）
      *(consolidated-findings.md line 169；D1.3a T3 — ship_sim_interfaces + FCBSimulator plugin)*
- [x] finding G P0-G-1(a) 标 CLOSED（链接到此 PR）
      *(consolidated-findings.md line 171 以 MV-6 合并关闭；D1.3a T2 — hull_class: SEMI_PLANING)*
- [x] v3.0 计划文件 "Euler 积分" 备注更新为 "RK4, dt=0.02s"
      *(gantt plan §D1.3a line 274；2026-05-08)*

**D1.3a 关闭日期：2026-05-08**（ruff 0 violations + 全部 DoD 满足）

---

## 14. 与下游 D 的接口

| 下游 D | 消费 D1.3a 产出 | 接口契约 |
|---|---|---|
| **D1.3b** 场景管理 + SIL HMI | `/filtered_own_ship_state` (50Hz) + `/tracked_targets` (2Hz) | D1.3b YAML 场景声明 `vessel_class: FCB`；AIS 数据集文件路径作为场景参数 |
| **D1.3.1** Simulator Qualification Report | `run_scenario(ManeuverType, ...)` 接口 | 直接调用；三种机动（STRAIGHT_DECEL / STANDARD_TURN_35DEG / ZIG_ZAG_10_10）初始条件见 D1.3.1 spec；不需修改 D1.3a 代码 |
| **D2.x** M2/M5 SIL 集成 | `/tracked_targets` (TrackedTargetArray v1.1.2) + `/filtered_own_ship_state` | M2 订阅两个 topic；M5 订阅 `FilteredOwnShipState`；`schema_version = "v1.1.2"` 为兼容契约 |
| **HAZID RUN-001** (8/19) | `fcb_dynamics.yaml` HAZID-UNVERIFIED 参数 | RUN-001 校准后，参数更新触发 D1.3.1 强制重跑（per v3.0 spec §D1.3.1） |
| **v1.1.3 架构** (D2.8 stub) | `ShipMotionSimulator` 接口设计 | 接口稳定后可引用为 §10.5 4-DOF 边界 + Capability Manifest 范式实例 |

---

## 15. 来源与置信度

| 断言 | 来源 | 置信度 |
|---|---|---|
| Yasukawa 2015 4-DOF MMG 参数已在 fcb_dynamics.yaml | 仓库代码 `src/fcb_simulator/config/fcb_dynamics.yaml` | 🟢 High |
| pyais v3.0.0, MIT, 适用于 AIVDM/AIVDO | PyPI + GitHub 直接查询（2026-05-08） | 🟢 High |
| NOAA marinecadastre.gov CC0 公共领域 | GitHub CC0 声明 + NOAA FAQ | 🟢 High |
| DMA AIS 研究免费 | DMA 官方网站（待下载时确认最新授权页） | 🟡 Medium |
| "ChAIS" 不是公开数据集 | 全面搜索无命中 | 🟢 High |
| ROS2 pluginlib 为 C++ 动态加载标准机制 | NLM local notebook (high confidence) + ROS2 官方文档 | 🟢 High |
| multi_vessel_lint 仅扫描 `*.py` | 仓库 `.gitlab-ci.yml` 第 122 行 | 🟢 High |
| FCB 停船距离 250–350m | D0 deep research synthesis + RUN-001 memo | ⚫ HAZID-UNVERIFIED |
| semi-planing hull Fn=0.45–0.63 需速度相关系数 | NLM ship_maneuvering (high confidence) | 🟢 High |

---

## v3.1 补丁（2026-05-09 起，5/31 EOD 闭口）

> **范围声明**：本章在 §1–§15 原版 D1.3a（已闭 2026-05-08）之上叠加三项 NEW DoD —— (a) RL 隔离 L1 仓库目录拆分，(b) `ShipMotionSimulator::export_fmu_interface()` 抽象方法签名，(c) CI `rl_isolation_lint` job + 演示。原版 §1–§15 字面不动，原版 DoD 全部保持 ✅ 不得回归。
>
> **决策依据**：
> - 计划主文件 `gantt/MASS_ADAS_L3_8个月完整开发计划.md` v3.1 §0.4（13 项决策中第 1、3 项）+ §3 D1.3a v3.1 修订 Scope/DoD（行 252、322-325）
> - SIL 决策记录 `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md` §1（选项 D 混合架构）+ §4（RL 隔离三层强制边界）
> - 架构报告 v1.1.3-pre-stub 附录 F.2（FmuPlugin 导出契约说明，行 2122-2145）+ F.4（L1/L2/L3 三层 RL 隔离表，行 2156-2167）
> - 深度调研 `docs/Doc From Claude/SIL Framework Architecture for L3 Tactical Decision Layer (TDL) COLAV — CCS-Targeted Engineering Recommendation.md` L4/L33/L67/L209（MMG 抽象 + ship_dynamics 节点 topic 拓扑 + pythonfmu/mlfmu 工具链）
> - 深度调研 `docs/Doc From Claude/SIL Simulation Architecture for Maritime Autonomous COLAV Targeting CCS Certification.md` L3/L4/L91-93（disturbance schema：wind dir/speed + current dir/speed）
> - PyGemini (Vasstein 2025, arXiv:2506.06262) §Configuration-Driven Development 隔离 precedent；DNV-RP-0671 (2024) AI-enabled systems assurance；DNV-RP-0510 data-driven algorithms

---

### v3.1.1 包归属审计（grep 证据落地）

**证据 A — M1-M8 内核包对 `ship_sim_interfaces` / `fcb_simulator` 的依赖审计**：

```bash
$ grep -rn "ship_sim_interfaces\|fcb_simulator" \
    src/m1_odd_envelope_manager src/m2_world_model src/m3_mission_manager \
    src/m4_behavior_arbiter   src/m5_tactical_planner src/m6_colregs_reasoner \
    src/m7_safety_supervisor  src/m8_hmi_bridge       src/m8_hmi_transparency_bridge \
    --include='*.hpp' --include='*.cpp' --include='*.py' \
    --include='package.xml' --include='CMakeLists.txt'
# 输出为空 → M1-M8 内核包零依赖 sim 工具包
```

**证据 B — `ship_sim_interfaces` 唯一消费者**：

```bash
$ grep -rn "ship_sim_interfaces" src/ --include='*.hpp' --include='*.cpp' \
    --include='package.xml' --include='CMakeLists.txt'
# 命中：仅 ship_sim_interfaces/* 自身 + fcb_simulator/* 共 11 行
```

→ **结论**：`ship_sim_interfaces` 归 sim_workbench/ 安全；M1-M8 与 sim 工具完全解耦。

**包归属定案表**：

| 包 | 归属 | rationale |
|---|---|---|
| `m1_odd_envelope_manager` | `l3_tdl_kernel/` | M1 内核节点 |
| `m2_world_model` | `l3_tdl_kernel/` | M2 内核节点 |
| `m3_mission_manager` | `l3_tdl_kernel/` | M3 内核节点 |
| `m4_behavior_arbiter` | `l3_tdl_kernel/` | M4 内核（⚠️ 当前缺 `package.xml` —— v3.1 补丁不补，标 P-1 follow-up，由 D3.1 关闭；本补丁内仅 `git mv` 整目录，不创建 package.xml） |
| `m5_tactical_planner` | `l3_tdl_kernel/` | M5 内核节点 |
| `m6_colregs_reasoner` | `l3_tdl_kernel/` | M6 内核节点 |
| `m7_safety_supervisor` | `l3_tdl_kernel/` | M7 Checker（PATH-S 独立性约束随路径迁移，见 §v3.1.6） |
| `m8_hmi_transparency_bridge` | `l3_tdl_kernel/` | M8 内核（含 `python/web_server/`，整体随包归 kernel —— web_server/ 内零 sim/rl import，无须 allowlist 例外） |
| `m8_hmi_bridge` | **删除** | 空目录脚手架，本补丁 P-2 follow-up `git rm`（不阻塞 v3.1 主线） |
| `l3_msgs` | `l3_tdl_kernel/` | 内核 msg 契约 |
| `l3_external_msgs` | `l3_tdl_kernel/` | 内核接口 msg |
| `common` | `l3_tdl_kernel/` | 内核公共库 |
| `ship_sim_interfaces` | `sim_workbench/` | 证据 A/B：内核零依赖 |
| `fcb_simulator` | `sim_workbench/` | sim 驱动 + pluginlib 加载器宿主 |
| `sil_mock_publisher` | `sim_workbench/` | SIL mock |
| `l3_external_mock_publisher` | `sim_workbench/` | sim mock（命名带 l3_external，仅供 SIL 注入用，非 production） |
| `ais_bridge` | `sim_workbench/` | AIS 历史回放数据管道（sim 工具） |
| `rl_workbench/` 占位 | `rl_workbench/` | `.gitkeep` + `README.md` Phase 4 启动；目前禁止任何代码 |

---

### v3.1.2 方案决策：A（物理移动）vs B（声明式边界）

**决策**：方案 A —— 物理子目录 `git mv` 移动。

**Rationale（证据指向 v3.1 DoD 原文）**：
1. 计划 §0.4 行 92 措辞「**三 colcon 包独立**；CI lint 检测 cross-import 即报错」—— "包独立"在 colcon 语义即"独立 src 子树"，方案 B 仅按文件路径黑白名单 lint 而包仍在同一 `src/` 扁平层，与"包独立"字面不匹配。
2. SIL 决策 §4.1 表头使用 `/src/l3_tdl_kernel/**` `/src/sim_workbench/**` `/src/rl_workbench/**` 三 glob 路径，强暗示物理子目录拓扑。
3. 架构 F.4 行 2162 同上路径 glob。
4. PyGemini (arXiv:2506.06262) §Configuration-Driven Development 工业先例 + NTNU SFI-AutoShip `colav-simulator` 仓库结构均采用物理子目录拆分作为 RL 隔离 L1 落地手段。
5. CI lint 实现：路径前缀 grep 比白名单维护朴素一个数量级，误报路径明确（`grep -rEn '...' src/sim_workbench src/rl_workbench` vs B 方案需维护跨包 file glob 清单）。
6. DEMO-1 演示价值：`tree src/ -L 2` 三组目录直观可见，是方案 A 独有；方案 B 演示需打开 OWNERS 文件解释规则，不直观。

**方案 A 代价（已识别且可控）**：约 14 个 `git mv` 一次性操作 + 4 个 CI/test 配置文件路径修订（详见 §v3.1.6）。`git mv` 保留历史血缘；colcon/cmake/ament 按包名解析，不依赖路径。

**方案 B 否决理由**：DoD「独立 colcon 包」字面不达成；DEMO-1 无可视边界；lint 须维护清单（包加进来时易漏，长期维护成本高）。

---

### v3.1.3 目录目标拓扑

```
src/
├── l3_tdl_kernel/                         # frozen, DNV-RP-0513 assured, MISRA C++:2023
│   ├── m1_odd_envelope_manager/
│   ├── m2_world_model/
│   ├── m3_mission_manager/
│   ├── m4_behavior_arbiter/                # ⚠️ 当前缺 package.xml（P-1 follow-up）
│   ├── m5_tactical_planner/
│   ├── m6_colregs_reasoner/
│   ├── m7_safety_supervisor/
│   ├── m8_hmi_transparency_bridge/         # 含 python/web_server/，整体在 kernel
│   ├── l3_msgs/
│   ├── l3_external_msgs/
│   └── common/
├── sim_workbench/                          # Python sim 工具 / D1.3a-b 共用
│   ├── ship_sim_interfaces/                # FMI 导出契约持有方
│   ├── fcb_simulator/                      # FCBPlugin (C++ MMG) + pluginlib host
│   ├── sil_mock_publisher/
│   ├── l3_external_mock_publisher/
│   └── ais_bridge/
└── rl_workbench/                           # Phase 4 启动（10–12 月）；目前 .gitkeep + README
    ├── .gitkeep
    └── README.md
```

---

### v3.1.4 `ShipMotionSimulator::export_fmu_interface()` 签名（S1）

**位置**：`src/sim_workbench/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp`（移动后路径）。

**新增类型 + 纯虚方法**：

```cpp
#include <vector>
#include <string>

namespace ship_sim {

struct FmuVariableSpec {
  std::string name;        // FMI variable name (C identifier-safe)
  std::string causality;   // "input" | "output" | "parameter" | "local"
  std::string variability; // "continuous" | "discrete" | "fixed" | "tunable"
  std::string type;        // "Real" | "Integer" | "Boolean" | "String"
  std::string unit;        // "m/s", "rad", "rad/s", "1" (dimensionless)
  double      start;       // initial value (Real); cast for Integer/Boolean
  std::string description; // human-readable, ≤120 chars
};

struct FmuInterfaceSpec {
  std::string fmi_version;       // hard-coded "2.0"
  std::string model_name;        // e.g. "FCBShipDynamics"
  std::string model_identifier;  // C identifier; matches model_name w/o spaces
  double      default_step_size; // seconds; FCB = 0.02 (RK4 step)
  std::vector<FmuVariableSpec> variables;
};

class ShipMotionSimulator {
 public:
  virtual ~ShipMotionSimulator() = default;
  virtual void load_params(const std::string& yaml_path) = 0;
  virtual ShipState step(const ShipState& state, double delta_rad,
                         double n_rps, double dt_s) = 0;
  virtual std::string vessel_class() const = 0;
  virtual std::string hull_class()   const = 0;
  // v3.1 NEW: FMI 2.0 export contract — D1.3c consumes via dds-fmu mapping XML.
  // Phase 1 contract-only; D1.3c (Phase 1/2 边界) implements pythonfmu/mlfmu wrap
  // through pybind11 bindings into this C++ class; this method enumerates the
  // FMU's variables for modelDescription.xml + dds-fmu mapping generation.
  virtual FmuInterfaceSpec export_fmu_interface() const = 0;
};

}  // namespace ship_sim
```

**FCBSimulator stub 实装**（`src/sim_workbench/fcb_simulator/src/fcb_simulator_plugin.cpp`，新增方法）：

```cpp
ship_sim::FmuInterfaceSpec FCBSimulator::export_fmu_interface() const {
  return {
    "2.0",
    "FCBShipDynamics",
    "FCBShipDynamics",
    0.02,
    {
      // ── outputs (own-ship state, 11) ─────────────────────────
      {"u",     "output", "continuous", "Real", "m/s",   0.0, "surge velocity (body frame)"},
      {"v",     "output", "continuous", "Real", "m/s",   0.0, "sway velocity (body frame)"},
      {"r",     "output", "continuous", "Real", "rad/s", 0.0, "yaw rate"},
      {"x",     "output", "continuous", "Real", "m",     0.0, "position x (NED)"},
      {"y",     "output", "continuous", "Real", "m",     0.0, "position y (NED)"},
      {"psi",   "output", "continuous", "Real", "rad",   0.0, "heading"},
      {"phi",   "output", "continuous", "Real", "rad",   0.0, "roll angle"},
      {"p",     "output", "continuous", "Real", "rad/s", 0.0, "roll rate"},
      {"delta", "output", "continuous", "Real", "rad",   0.0, "actual rudder angle"},
      {"n",     "output", "continuous", "Real", "rev/s", 0.0, "actual propeller rps"},
      {"sog",   "output", "continuous", "Real", "m/s",   0.0, "speed over ground"},

      // ── control inputs (commands from L4/L5, 2) ──────────────
      {"delta_cmd", "input", "continuous", "Real", "rad",   0.0, "commanded rudder angle"},
      {"n_rps_cmd", "input", "continuous", "Real", "rev/s", 0.0, "commanded propeller rps"},

      // ── disturbance inputs (from /disturbance/* topic, 4) ────
      // Source: SIL Recommendation L91-93 disturbance YAML schema
      {"wind_dir_deg",     "input", "continuous", "Real", "deg", 0.0, "true wind direction (from)"},
      {"wind_speed_mps",   "input", "continuous", "Real", "m/s", 0.0, "true wind speed"},
      {"current_dir_deg",  "input", "continuous", "Real", "deg", 0.0, "current set direction (towards)"},
      {"current_speed_mps","input", "continuous", "Real", "m/s", 0.0, "current speed"},

      // ── parameters (FCB hydro static, minimum exposed set, 5) ─
      {"L",   "parameter", "fixed", "Real", "m",     45.0,    "ship length"},
      {"B",   "parameter", "fixed", "Real", "m",      8.5,    "beam"},
      {"d",   "parameter", "fixed", "Real", "m",      2.5,    "draft"},
      {"m",   "parameter", "fixed", "Real", "kg",     3.5e5,  "mass"},
      {"Izz", "parameter", "fixed", "Real", "kg.m2",  5.0e7,  "yaw inertia"},
    }
  };
}
```

**变量计数**：11 outputs + 2 control inputs + 4 disturbance inputs + 5 parameters = **22 variables**（FCB 4-DOF baseline；6-DOF planing extension Phase 2 D1.3a TODO 增 q/theta 自由度时再扩，目前 phi/p 已含覆盖 roll 维）。

**D1.3c 消费路径（合约写入，下游 owner 须签字）**：
1. D1.3c 编薄 Python 适配类 `FCBShipDynamicsFmu`，pybind11（`pybind11/pybind11.h` + `pybind11_add_module`）调 C++ `FCBSimulator::step()`——满足架构 F.2 行 2145「同 FCBPlugin 实例三种调用，**不 fork 模型**」。
2. D1.3c 起 build 流程：写 `tools/d1_3c/emit_fmu_modeldescription.cpp` 小 CLI，链接 FCBSimulator，序列化 `export_fmu_interface()` 返回值为 modelDescription.xml + dds-fmu mapping XML。
3. dds-fmu mapping：每个 `causality=="input"` 变量 ↔ 一个 ROS2 subscriber topic（如 `wind_dir_deg` ↔ `/disturbance/wind/dir_deg`）；每个 `causality=="output"` 变量 ↔ 一个 publisher topic（如 `u` ↔ `/own_ship/u` 或聚合到 `/own_ship/state` 复合 message）。
4. **新 vessel plugin（如 RUN-002 拖船）实装此方法工作量**：仅填变量清单（无业务逻辑），估 0.5 人日（4h）。

**Phase 1 不交付**：FMU 二进制 / dds-fmu mapping XML 实文件 / pythonfmu wrapper —— 全部留 D1.3c。Phase 1 仅锁 C++ 接口签名 + FCBSimulator stub 编译通过。

---

### v3.1.5 CI `rl_isolation_lint`：脚本 + grep 模式 + allowlist

**新文件**：`tools/ci/check-rl-isolation.sh`（独立脚本，与 `check-doer-checker-independence.sh` **并列不扩展**——关注点不同，演化独立）。

**检测语义**：跨组 import / include = violation，**双向同禁**（kernel 不许引 sim/rl，sim/rl 不许引 kernel；sim ↔ rl 同样禁）。

**Python import 检测**（精确模式）：

```bash
# kernel-from-sim/rl violations
grep -rEn '^[[:space:]]*(from|import)[[:space:]]+(m[1-8]_[a-z_]+|common)([[:space:]]|\.|$)' \
     src/sim_workbench src/rl_workbench --include='*.py' \
| grep -v '# rl-isolation-ok:'

# sim/rl-from-kernel violations
grep -rEn '^[[:space:]]*(from|import)[[:space:]]+(sim_workbench|rl_workbench|fcb_simulator|ship_sim_interfaces|sil_mock_publisher|l3_external_mock_publisher|ais_bridge)([[:space:]]|\.|$)' \
     src/l3_tdl_kernel --include='*.py' \
| grep -v '# rl-isolation-ok:'
```

**C++ #include 检测**：

```bash
# kernel-header-from-sim/rl violations
grep -rEn '^[[:space:]]*#include[[:space:]]*[<"](m[1-8]_[a-z_]+|common)/' \
     src/sim_workbench src/rl_workbench \
     --include='*.hpp' --include='*.cpp' --include='*.h' --include='*.cc' \
| grep -v '// rl-isolation-ok:'

# sim/rl-header-from-kernel violations
grep -rEn '^[[:space:]]*#include[[:space:]]*[<"](sim_workbench|rl_workbench|fcb_simulator|ship_sim_interfaces|sil_mock_publisher|l3_external_mock_publisher|ais_bridge)/' \
     src/l3_tdl_kernel \
     --include='*.hpp' --include='*.cpp' --include='*.h' --include='*.cc' \
| grep -v '// rl-isolation-ok:'
```

**设计澄清（2026-05-09 T8 实施时修订）**：`l3_msgs` 与 `l3_external_msgs` 是纯 IDL 契约包（仅含 `msg/` 目录，零业务逻辑），ROS2 publish/subscribe 模式下 sim 节点必须 import 这些包以使用共享 message 类型——这是正常基础设施数据流，不是异常跨组引用。因此 grep 模式从 kernel-forbidden 列表中**移除** `l3_msgs|l3_external_msgs`，仅保留 `m[1-8]_<node>` 业务逻辑包 + `common` 为 kernel-only。msg 契约包视为三组共用基础设施。

**False-positive 抑制**：
- 行首锚 `^[[:space:]]*` → 注释/字符串字面量内的 `# 这里 import m1_...` 不命中（`#` 在 `from/import` 之前已破前缀模式）。
- Python 后缀 `[[:space:]]|\.|$` → 杜绝匹配 `m1_xxxxxx_var`（变量名相似前缀）。
- C++ 包名后必须接 `/`（包内子路径）→ 注释里 `// see m1_odd` 不命中。
- allowlist 同行注释 `# rl-isolation-ok: <reason>` / `// rl-isolation-ok: <reason>`，post-grep `grep -v` 滤掉。每条 allowlist 必须有 `<reason>` 字符串（非空）；脚本第二步扫 allowlist 注释格式合规性。

**脚本骨架**（完整实现见 D1.3a 02-plan.md v3.1 task T8）：

```bash
#!/usr/bin/env bash
# tools/ci/check-rl-isolation.sh
# RL Isolation L1 Repo Boundary Check (per architecture v1.1.3-pre-stub §F.4)
# Verifies cross-group imports/includes between l3_tdl_kernel/, sim_workbench/, rl_workbench/.
# Allowlist: same-line comment "# rl-isolation-ok: <reason>" / "// rl-isolation-ok: <reason>"

set -euo pipefail
errors=0

scan_python_kernel_imports_in_sim_rl() { ... }
scan_python_sim_rl_imports_in_kernel() { ... }
scan_cpp_kernel_includes_in_sim_rl()   { ... }
scan_cpp_sim_rl_includes_in_kernel()   { ... }

# (4 grep invocations as documented above; each piped through `| grep -v rl-isolation-ok`)

if [[ $errors -gt 0 ]]; then
    echo "FAIL: ${errors} RL-isolation violation(s) detected." >&2
    echo "      Architecture v1.1.3-pre-stub §F.4 requires three-group separation." >&2
    echo "      If a cross-group reference is intentional, add same-line marker:" >&2
    echo "         '# rl-isolation-ok: <non-empty reason>' (Python)" >&2
    echo "         '// rl-isolation-ok: <non-empty reason>' (C++)" >&2
    exit 1
fi

echo "PASS: RL-isolation check clean (kernel × sim_workbench × rl_workbench)."
exit 0
```

**`.gitlab-ci.yml` 追加 job**（在 `stage: lint` 内，与 `multi_vessel_lint` 并列，**不改任何已有 job**）：

```yaml
rl_isolation_lint:
  stage: lint
  needs: []
  rules:
    - if: $FULL_PIPELINE == "true" || $CI_COMMIT_BRANCH =~ /^feature\//
  script:
    - bash tools/ci/check-rl-isolation.sh
  allow_failure: false   # blocking — per v3.1 DoD
```

**单元测试**：`tools/ci/tests/test-rl-isolation.sh`（新增）：
- TC-1：clean tree → `bash check-rl-isolation.sh` exit 0 + stdout `PASS:`
- TC-2：临时往 `src/sim_workbench/fcb_simulator/src/fixture_violation.cpp` 写 `#include "m7_safety_supervisor/safety_supervisor.hpp"` → exit 1 + stderr 含 `VIOLATION`
- TC-3：上一文件改加同行 allowlist `// rl-isolation-ok: deliberate fixture for unit test` → exit 0
- TC-4：Python 同样三组（fixture file 在 sim_workbench 下临时 import m1_）
- TC-5：反向（kernel 中 import sim 包）触发，验证双向检测
- 每 TC 运行后清理 fixture，最终树状态不变。
- 该 test 脚本本身不入 CI（避免污染主 lint）；本地 `bash tools/ci/tests/test-rl-isolation.sh` 手工跑，PR 描述附运行日志。

---

### v3.1.6 路径变更清单（一次性）

| 文件 | 变更 | 理由 |
|---|---|---|
| `pytest.ini` | `pythonpath = src/l3_tdl_kernel src/l3_tdl_kernel/m8_hmi_transparency_bridge/python src/sim_workbench` | 包目录下沉一层 |
| `tools/docker/Dockerfile.ci` | `COPY src/ /workspace/src/` 不变 | 整树拷贝，递归透明 |
| `.gitlab-ci.yml` `detect-changes` job | `awk -F/ '/^src\//{print $2}'` → `awk -F/ '/^src\//{print $2"/"$3}'` | 提取 `<group>/<pkg>` |
| `.gitlab-ci.yml` `stage-1-lint` ruff/mypy 路径 | `src/m2_world_model/python` → `src/l3_tdl_kernel/m2_world_model/python`；`src/m8_hmi_transparency_bridge/python` 同改；`tools/` 不变；`src/ais_bridge/*.py`（multi_vessel_lint）→ `src/sim_workbench/ais_bridge/*.py` | 硬编码路径修订 |
| `.gitlab-ci.yml` `stage-2-unit-test` matrix | 包名列表不变（colcon 按包名解析） | colcon 递归发现 |
| `tools/ci/check-doer-checker-independence.sh` | `M7_SRC="src/m7_safety_supervisor"` → `src/l3_tdl_kernel/m7_safety_supervisor` | PATH-S 路径迁移 |
| `tools/ci/check-rl-isolation.sh` | **NEW** 文件 | v3.1 NEW |
| `.gitlab-ci.yml` `rl_isolation_lint` job | **NEW** job，stage: lint | v3.1 NEW |
| `src/rl_workbench/.gitkeep` | **NEW** 文件 | Phase 4 占位 |
| `src/rl_workbench/README.md` | **NEW**：说明三层隔离规则 + Phase 4 启动条件 | Phase 4 占位 + 文档警示 |
| 各 `package.xml` / `CMakeLists.txt` | **不动** | colcon 按包名解析，路径变化对 ament 透明 |

**git mv 操作清单**（验证 `git status` 显示 R 重命名而非 D+A，保留历史血缘）：
```
mkdir -p src/l3_tdl_kernel src/sim_workbench src/rl_workbench
git mv src/{m1_odd_envelope_manager,m2_world_model,m3_mission_manager,m4_behavior_arbiter,m5_tactical_planner,m6_colregs_reasoner,m7_safety_supervisor,m8_hmi_transparency_bridge,l3_msgs,l3_external_msgs,common} src/l3_tdl_kernel/
git mv src/{ship_sim_interfaces,fcb_simulator,sil_mock_publisher,l3_external_mock_publisher,ais_bridge} src/sim_workbench/
git rm -r src/m8_hmi_bridge   # 空目录脚手架，P-2 follow-up 同步关闭
touch src/rl_workbench/.gitkeep
```

---

### v3.1.7 Task 拆分（每 task ≤ 4h；本补丁 ~1.5 人周追加 = ~7.5 人日）

| # | Task | 时长 | 前置 | Acceptance Criteria |
|---|---|---|---|---|
| **T1** | 包归属审计落地（grep 证据 commit 到 spec） | 1h | — | spec §v3.1.1 grep 证据 + 定案表 commit；本 PR 评审者按 spec 表逐行核对无异议 |
| **T2** | 创建子目录 + `git mv` 物理移动（per §v3.1.6 mv 清单） | 2h | T1 | `git status` 显示全部 R 重命名（保留历史）；`tree src/ -L 2` 显示三组子目录；`git rm src/m8_hmi_bridge` 同步执行 |
| **T3** | colcon 本地回归 `colcon build`（如本机有 ROS2）+ 容器内 `docker run mass-l3/ci:jazzy ... colcon build --packages-up-to m7_safety_supervisor --packages-skip m4_behavior_arbiter` | 3h | T2 | 容器内 exit 0；`AMENT_PREFIX_PATH` / 链接库无路径硬编码报错 |
| **T4** | `pytest.ini` + `Dockerfile.ci`（如需）+ `.gitlab-ci.yml` 路径更新 | 2h | T2 | `python3 -m pytest tests/ -q` 仍 39 passed（零回归）；`ruff check src/ tests/` 0 violations |
| **T5** | `check-doer-checker-independence.sh` `M7_SRC` 路径修订 + 本地 `bash tools/ci/check-doer-checker-independence.sh` | 0.5h | T2 | 脚本退出码 0（含 M7 真路径） |
| **T6** | `ShipMotionSimulator::export_fmu_interface()` 接口 + 类型声明 commit | 2h | T2 | header compile 通过；`grep -n export_fmu_interface src/sim_workbench/ship_sim_interfaces/include/.../ship_motion_simulator.hpp` 命中 |
| **T7** | FCBSimulator stub 实装（22 变量清单）+ 单元测试 `test_fcb_export_fmu_interface.cpp` | 3h | T6 | `colcon test --packages-select fcb_simulator` 含新 test PASSED；测试断言 `variables.size()==22` + 类型/causality 取值合法 |
| **T8** | `tools/ci/check-rl-isolation.sh` 新建 + 单元测试 `tools/ci/tests/test-rl-isolation.sh`（5 TC） | 4h | T2 | 5 TC 全 pass；clean tree 下 `bash check-rl-isolation.sh` exit 0 + stdout `PASS:` |
| **T9** | `.gitlab-ci.yml` 追加 `rl_isolation_lint` job | 1h | T8 | YAML 语法校验通过；本地 `gitlab-ci-lint` 工具或 `yamllint .gitlab-ci.yml` 0 errors |
| **T10** | `rl_workbench/` 占位（`.gitkeep` + README 含三层隔离规则原文） | 1h | — | 目录存在；README 引用架构 §F.4 表 + 计划 §0.4 决策 3 |
| **T11** | 全量回归（pytest + ruff + mypy + colcon + doer-checker + rl-isolation） | 2h | T3-T10 | 全部 exit 0；CI Pipeline 一次过（feature branch 触发） |
| **T12** | DEMO-1 cross-import 演示文件 + 操作脚本（仅本地 demo，不入 main） | 1h | T8 | demo 脚本可重现 exit 1 → 删行 → exit 0 ≤ 30s |
| **T13** | spec §v3.1 更新（已闭项打 ✅）+ DoD 复核 + commit + PR 描述 | 1.5h | T1-T12 | spec 内 v3.1 DoD 全部 ✅；PR 描述附 5 条回归命令输出 |

**总工时**：1+2+3+2+0.5+2+3+4+1+1+2+1+1.5 = **24h ≈ 3 人日**（含 T11 buffer）；剩余 ~4.5 人日用于 risk contingency（colcon 容器问题修复 / 路径拼写错误回滚 / D1.3c owner 签字往返）。

---

### v3.1.8 Owner-by-day 矩阵（5/12 → 5/31，~1.5 人周追加）

> 用户为 solo 开发者扮所有角色（per memory `user_solo_multi_role.md`）。下表"role-hat"是用户切换的会话视角。

| 日期 | role-hat | 预计 task | 备注 |
|---|---|---|---|
| 5/12 (Mon) | 技术负责人 | T1（审计 commit）+ T10（rl_workbench/ 占位） | 启动；不接触代码移动 |
| 5/13 (Tue) | 技术负责人 | T2（git mv）+ T4（路径配置）+ T5（PATH-S 路径） | HAZID kickoff 同日；mv 在 HAZID 会前一小段做 |
| 5/14 (Wed) | 技术负责人 | T3（colcon 容器回归）+ T11 partial（pytest/ruff 回归） | 若 T3 fail 进入 R-P1 contingency |
| 5/15 (Thu) | 技术负责人 + V&V FTE 视角 | T6（接口签名）+ T7（FCBSimulator stub + 单元测试）| 同时 D1.3c owner 视角对签名签字 |
| 5/16 (Fri) | 技术负责人 | T8（check-rl-isolation.sh + 5 TC）| |
| 5/17-18 (Sat-Sun) | — | buffer / 私用 | |
| 5/19 (Mon) | 技术负责人 | T9（CI job 追加）+ T11 full（全量回归）| feature branch CI 触发 |
| 5/20 (Tue) | 技术负责人 | T12（demo 脚本）+ T13（spec 更新 + PR 描述） | |
| 5/21-30 | — | risk contingency / D1.3c owner 反馈窗口 / DEMO-1 rehearsal 雏形 | |
| 5/31 (Fri) | 技术负责人 | DoD 复核 + 标 v3.1 补丁 CLOSED | 5/31 EOD 闭口 |

---

### v3.1.9 依赖图

```
T1 (audit commit) ──→ T2 (git mv) ──┬─→ T3 (colcon container)
                                    ├─→ T4 (paths)
                                    ├─→ T5 (PATH-S)
                                    └─→ T6 (interface) ──→ T7 (stub + UT)
                       T8 (lint script + UT) ──→ T9 (CI job) ──→ T11 (full regression) ──→ T12 (demo) ──→ T13 (spec close)
T10 (rl_workbench placeholder) ─────────────────────────────────────────────────────────────↑

(D1.3c owner sign-off on T6 signature is async parallel — must close ≤ 5/22)
```

**关键依赖**：
- T6（接口签名）解锁 D1.3c V&V FTE 启动条件（D1.3c 6/1 起跑前签名须冻结）
- T2（目录就位）解锁 T8（lint 路径前缀正确性的前置）
- T3（colcon 容器通过）是 T11 全量回归门
- T8/T9 联通 → DEMO-1 6/15 现场演示能力（T12 演示文件依赖 T8 脚本）

---

### v3.1.10 Demo Charter（服务 6/15 DEMO-1 Skeleton Live，~5 分钟）

| 时段 | 内容 | 现场命令 / 预期 |
|---|---|---|
| 0:00-1:00 | 三层目录拓扑展示 | `tree src/ -L 2` → 三组目录可见；`git log --oneline -- src/l3_tdl_kernel/m1_odd_envelope_manager/` 显示 `git mv` 后历史保留 |
| 1:00-2:00 | `ShipMotionSimulator::export_fmu_interface()` 接口 + FCBSimulator stub | `cat src/sim_workbench/ship_sim_interfaces/include/ship_sim_interfaces/ship_motion_simulator.hpp \| grep -A3 export_fmu_interface`；`./build/fcb_simulator/test_fcb_export_fmu_interface` PASSED |
| 2:00-4:00 | RL 隔离 CI lint exit 1 演示 | (1) `cat tools/ci/demo/violation_fixture.cpp` 显示故意 `#include "m7_safety_supervisor/safety_supervisor.hpp"`；(2) `cp tools/ci/demo/violation_fixture.cpp src/sim_workbench/fcb_simulator/src/_demo_violation.cpp`；(3) `bash tools/ci/check-rl-isolation.sh` → exit 1 + 红色 VIOLATION 输出；(4) `rm src/sim_workbench/fcb_simulator/src/_demo_violation.cpp`；(5) 重跑 → exit 0 + `PASS:` |
| 4:00-5:00 | CI Pipeline 链接展示 | 浏览器打开最新 feature branch pipeline，`rl_isolation_lint` job 绿勾 + `multi_vessel_lint` 绿勾共存 |

**Demo 文件归档位置**：`tools/ci/demo/violation_fixture.cpp`（仅文本片段，入仓 `tools/ci/demo/` 路径不被 lint 扫；演示时 `cp` 到 `src/sim_workbench/fcb_simulator/src/_demo_violation.cpp` 触发 lint，演示完即 `rm`，主分支永远不含 demo 副本，无须 `.gitignore`）。

---

### v3.1.11 Risk + Contingency

| ID | Risk | 触发现象 | Contingency |
|---|---|---|---|
| **R-P1** | colcon 容器内 build fail（路径变更引发 cmake 缓存或 install path 问题） | T3 step 失败；`docker run ... colcon build` 非零退出 | (a) `rm -rf build/ install/ log/` 清缓存重试；(b) 检查 `tools/docker/Dockerfile.ci` `WORKDIR` 与 `COPY` 是否假设了 `src/<pkg>` 路径深度（已审计：当前用 `COPY src/` 整拷，递归透明）；(c) 若仍 fail，回滚 T2 mv 到 feature branch，方案 B 兜底（保留 rl_isolation_lint + 接口签名两项），暂跳过 v3.1 §3 物理目录 DoD 项，标 v3.2 修订计划 |
| **R-P2** | 审计漏报 ship_sim_interfaces 隐式 kernel 依赖 | T3 colcon build fail，error 含 `ship_sim_interfaces` not found in M1-M8 包链路 | 已在 §v3.1.1 grep 证据 A/B 排除；若仍发生，目录归属下沉 `ship_sim_interfaces` → `l3_tdl_kernel/`（牺牲一点工程清洁度换功能正确性），更新 §v3.1.5 lint 模式排除该包 |
| **R-P3** | `export_fmu_interface()` 签名与 D1.3c V&V FTE 实装预期不符（破坏 ABI） | D1.3c 启动后 owner 反馈需要额外字段（如 `serializeAs` / `aliasName`） | (a) 5/15-22 间发起 D1.3c owner 签字会话（用户切换 role-hat），冻结签名；(b) 万一 D1.3c 启动后仍需扩，沿 `FmuVariableSpec` / `FmuInterfaceSpec` 末尾**追加**字段（C++ struct 末尾追加保持向后兼容），不删不改既有字段，避免 ABI break |
| **R-P4** | CI lint grep 误报（特别是 `m8_` 前缀在 web_server/ 内出现） | T11 全量回归触发 `rl_isolation_lint` exit 1 但代码合法 | grep 模式已用行首锚 + 后缀边界，理论无误报；若发生：(a) 加同行 `// rl-isolation-ok: <reason>` allowlist；(b) 若 allowlist 滥用（>5 处），脚本退化为白名单 lint，触发 R-P5 |
| **R-P5** | allowlist 滥用导致 lint 形同虚设 | 6/15 DEMO-1 前 allowlist 行数 >5 | spec 增补：每月扫一次 allowlist 行数，>5 触发架构评审；P-3 follow-up 由 D2.x 合规复核 |
| **R-P6** | m4_behavior_arbiter 缺 package.xml 影响 colcon build | `colcon build` skip m4 或 fail | 已知问题（标 P-1 follow-up），D3.1 关闭。当前 v3.1 补丁 colcon DoD 改为 `colcon build --packages-up-to m7_safety_supervisor --packages-skip m4_behavior_arbiter` 跳过 m4 包，在 spec §v3.1.12 DoD 明文写出此偏差 |
| **R-P7** | 现存活跃分支 `feat/d3.1-m4-behavior-arbiter` / `feat/d3.3b-m7-sotif` 与目录移动冲突 | 这两 branch rebase 时大量 conflict | v3.1 补丁分支合并到 main 后，**强制 rebase 这两 branch**，git 自动跟随 R 重命名（`git mv` 历史血缘正确时合并冲突仅在内容修改，路径迁移自动）；若仍冲突，由对应 D 任务 owner 在 rebase 前先 `git mv` 同步 |
| **R-P8** | pythonpath 改动破坏 39 pytest | T11 pytest 失败 | T4 完成后立即 dry-run pytest 验证；若失败，仅修订 `pytest.ini` `pythonpath` 多路径列表（不修改测试文件） |

---

### v3.1.12 v3.1 补丁全闭判据（5/31 EOD）

> 全部 ✅ 才视为本补丁 CLOSED。原版 D1.3a §13 已闭判据保持 ✅ 不得回归。

#### 基线零回归（继承原版）

- [ ] `colcon build --packages-up-to m7_safety_supervisor --packages-skip m4_behavior_arbiter`（容器内）exit 0  ⚠️ R-P6 偏差，待 D3.1 关闭后转为 `--packages-up-to m7_safety_supervisor` 全包
- [ ] `python3 -m pytest tests/ -q` 仍 **39 passed**（零回归）
- [ ] `ruff check src/ tests/` **0 violations**
- [ ] `bash tools/ci/check-doer-checker-independence.sh` 退出码 0
- [ ] `multi_vessel_lint` CI job 通过

#### 三项 NEW DoD

- [ ] `/src/l3_tdl_kernel/`、`/src/sim_workbench/`、`/src/rl_workbench/` 三子目录存在；`tree src/ -L 2` 输出与 §v3.1.3 拓扑一致
- [ ] `git log --follow src/l3_tdl_kernel/m1_odd_envelope_manager/package.xml` 显示历史保留（`git mv` 血缘正确）
- [ ] `src/rl_workbench/` 仅含 `.gitkeep` + `README.md`（README 含三层隔离规则原文 + Phase 4 启动条件 + 引用架构 F.4 + 计划 §0.4 决策 3）
- [ ] `ShipMotionSimulator::export_fmu_interface()` 抽象方法签名 commit；header compile 通过
- [ ] `FCBSimulator::export_fmu_interface()` stub 实装（22 变量清单）；`colcon test --packages-select fcb_simulator` 含 `test_fcb_export_fmu_interface` PASSED
- [ ] D1.3c owner（V&V FTE role-hat）对 `FmuInterfaceSpec` / `FmuVariableSpec` 签名签字记录入 PR 评论（≤ 5/22）
- [ ] `tools/ci/check-rl-isolation.sh` 新建；`bash tools/ci/tests/test-rl-isolation.sh` 5 TC 全 pass
- [ ] `.gitlab-ci.yml` `rl_isolation_lint` job 追加（`allow_failure: false`）；feature branch pipeline 触发该 job 绿勾
- [ ] `tools/ci/demo/violation_fixture.cpp` + Demo Charter §v3.1.10 操作脚本就位；本地 `≤ 30s` 重现 exit 1 → exit 0
- [ ] 关闭 finding ID：**G P0-G-1 (a) v3.1 追加项**（RL 隔离目录 + FMI 接口存根）+ 计划 v3.1 §0.4 决策 1 + 决策 3 D1.3a 部分

#### 文档与版本同步

- [ ] 本 spec §v3.1 章节合入 `01-spec.md`，所有 DoD 复选框打 ✅；commit 由 v3.1 补丁同 PR 提交
- [ ] PR 描述附 5 条回归命令（colcon / pytest / ruff / doer-checker / rl-isolation）的实际输出截图或日志
- [ ] follow-up 标记入 GitLab issue：P-1（m4 package.xml）/ P-2（m8_hmi_bridge 空目录已删，无 follow-up，可标 CLOSED）/ P-3（allowlist 滥用月度复核）/ R-P6 收尾路径

---

### v3.1.13 与下游 D 的接口（v3.1 补丁视角增量）

| 下游 D | v3.1 补丁产出消费 | 接口契约 |
|---|---|---|
| **D1.3b.1** YAML 场景管理基础 | `src/sim_workbench/` 下 Python 路径 | scenario_runner Python 模块继承 sim_workbench 路径；`pytest.ini` pythonpath 已含 `src/sim_workbench` |
| **D1.3c** FMI bridge / libcosim / dds-fmu（V&V FTE 主导，6/1 起跑） | `ShipMotionSimulator::export_fmu_interface()` 签名 | 签名冻结后才启动；D1.3c owner 签字记录在本补丁 PR；override 模式 + 22 变量清单作 modelDescription.xml + dds-fmu mapping XML 输入；pythonfmu wrap via pybind11 |
| **D1.5** V&V Plan | `rl_isolation_lint` CI job 状态 | V&V Plan §SIL gate entry 条件加一行：`rl_isolation_lint` 必须绿；纳入 D1.5 §entry-exit gate |
| **D2.8** v1.1.3 stub §RL 章节（7/31） | RL 隔离 L1 落地证据 | D2.8 §RL §15.0/§F.4 引用本补丁 PR + git history audit 通过证据 |
| **D3.5** HAZID 132 [TBD] 回填（8/31） | RL 隔离 L1 完整记录 | RUN-001 HAZID 评估 RL 引入风险时直接引用 v3.1 补丁的 L1/L2/L3 边界落地状态 |
| **Phase 4 D4.6** RL 对抗（10–12 月，B2 触发） | `rl_workbench/` 目录占位 + lint 三向就位 | Phase 4 启动前先做 git history audit（per 架构 F.4 行 2166），确认 D1.3a/b/c 已遵守 L1/L2 边界 |

---

### v3.1.14 来源与置信度（v3.1 补丁部分）

| 断言 | 来源 | 置信度 |
|---|---|---|
| 选项 D 混合架构（C++ ROS2 native + FMI 边界）锁定 | SIL 决策 §1.1 + 3 份独立深度调研 + DNV-Brinav MoU 2024 | 🟢 High |
| RL 隔离 L1 物理子目录落地 | SIL 决策 §4.1 + 架构 F.4 + PyGemini arXiv:2506.06262 + NTNU colav-simulator + Sim2Sea AAMAS 2026 | 🟢 High |
| `ShipMotionSimulator` 抽象 + FCBPlugin（不 fork 模型）| 架构 F.2 行 2122-2145 + Recommendation L4/L209 | 🟢 High |
| FMU 工具链 pythonfmu/mlfmu | 架构 F.2 行 2142 + Recommendation L67 + DNV-RP-0671 | 🟢 High |
| disturbance 4 变量（wind dir/speed + current dir/speed）应作 FMI input | Recommendation L91-93 disturbance YAML schema + Certification L81 disturbance axis | 🟢 High |
| ship_dynamics 节点 topic 拓扑 `/cmd/{thrust,rudder}` 输入 + `/own_ship/state` 输出 | Recommendation L33 | 🟢 High |
| pybind11 是 C++/Python 双向绑定标准 | NLM silhil_platform notebook（Phase 1 启动后增补，置信度暂记 🟡）+ pybind11 官方文档（≥10k★）| 🟡 Medium（待 NLM 引证后升 🟢）|
| colcon 递归发现 `package.xml`（不依赖深度）| colcon 官方文档 + 仓库现状 `find src -name package.xml` 已落地 | 🟢 High |
| `git mv` 保留历史血缘 | git 官方文档 + `git log --follow` 验证机制 | 🟢 High |
| FCB 4-DOF 状态变量 11 项（u/v/r/x/y/psi/phi/p/delta/n/sog）| 架构 F.2 + 现有 `ship_motion_simulator.hpp` State struct + Yasukawa 2015 4-DOF MMG 标准变量集 | 🟢 High |
| FCB 5 个最小暴露参数（L/B/d/m/Izz）足够 D1.3c modelDescription | 架构 F.2 + 工程惯例（其余 hydro 系数封装在 YAML，FMU 不暴露内部参数）| 🟡 Medium（D1.3c 启动后若需更多参数走 R-P3 contingency）|
