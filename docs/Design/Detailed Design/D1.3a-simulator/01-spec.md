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

- [ ] `colcon build --packages-select ship_sim_interfaces fcb_simulator ais_bridge` 零错误
- [ ] `colcon test --packages-select fcb_simulator` 全通过，含：
  - [ ] `test_mmg_steady_turn` PASSED（现有测试，不得回归）
  - [ ] `test_rk4_energy` PASSED（现有测试，不得回归）
  - [ ] `test_stopping_error` PASSED（停船误差 ≤10%，日志含数值）
  - [ ] `test_stability_1h` PASSED（1h 无崩溃）
- [ ] `colcon test --packages-select ais_bridge` 全通过，含 `test_parse_rate` PASSED（≥95%）
- [ ] `simulator.launch.py` 运行 60s：`/filtered_own_ship_state` @ 50Hz + `/tracked_targets` @ 2Hz 同时正常
- [ ] AIS 历史数据片段 ≥1h（NOAA 或 DMA）本地存储，路径记录在 spec
- [ ] `fcb_dynamics.yaml` 含 `hull_class: SEMI_PLANING`、`vessel_class: FCB`、全量 HAZID 注释
- [ ] multi_vessel_lint CI job 通过（src/ais_bridge/*.py 无船型字面量）
- [ ] `run_scenario()` 接口可调用，返回有效 `ScenarioResult`（D1.3.1 钩子）
- [ ] finding MV-4 标 CLOSED（链接到此 PR）
- [ ] finding G P0-G-1(a) 标 CLOSED（链接到此 PR）
- [ ] v3.0 计划文件 "Euler 积分" 备注更新为 "RK4, dt=0.02s"

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
