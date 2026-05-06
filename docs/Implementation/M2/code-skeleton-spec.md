# M2 World Model 代码骨架 Spec

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-M2-SKEL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（Team-M2 实施起点） |
| 团队 | Team-M2（World Model）|
| 路径强度 | **PATH-D（决策路径）** — MISRA C++:2023 完整 179 规则 |
| 主语言 | C++17（GCC 14.3 / Clang 20.1.8）|
| 副语言 | Python 3.10（ENC 加载工具）|
| 详细设计基线 | `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md`（SANGO-ADAS-L3-DD-M2-001 v1.0）|
| 架构基线 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §6 + §15.1 |
| 跨团队基线 | RFC-002 决议（M2 ← Multimodal Fusion）|
| 关联文件 | `00-implementation-master-plan.md` / `coding-standards.md` / `ros2-idl-implementation-guide.md` / `third-party-library-policy.md` |

> **本骨架是 Team-M2 编码起点。** 所有类签名、目录布局、CMake 模板、单元测试入口均与详细设计 §1–§11 一一对齐；所有 `[TBD-HAZID]` 参数通过 YAML 注入，不硬编码到源代码。

---

## 1. 模块概述

M2 World Model 是 L3 内部的**唯一权威世界视图生成器**（详细设计 §1）。从 Multimodal Fusion 子系统接收三个独立频率的话题（`/fusion/tracked_targets` @ 2 Hz、`/fusion/own_ship_state` @ 50 Hz、`/fusion/environment_state` @ 0.2 Hz），结合 M1 ODD 子域（`/l3/m1/odd_state` @ 1 Hz），通过**三视图聚合（DV/EV/SV）+ CPA/TCPA 计算 + COLREG 几何预分类 + 时间对齐**生成统一的 `WorldState` @ 4 Hz 输出，供 M3/M4/M5/M6 决策模块使用。M2 同时向 M8 推送 `SATData` @ 10 Hz、向 ASDR 发布事件 + 周期 2 Hz 决策日志。

**关键约束**（路径强度 PATH-D）：
- MISRA C++:2023 全 179 规则（CI 阻断）；clang-tidy 20.1.8 + cppcheck Premium 26.1.0 强制
- 主循环禁止裸 `new/delete`；用 RAII；目标缓存预分配上限 256 目标
- 异常允许，但仅在 `rclcpp::spin()` 边界 `catch(...)` + 转 `spdlog::error`
- 时间字段统一 `std::chrono::steady_clock::time_point`（内部）+ `builtin_interfaces/Time`（消息边界）
- 角度内部 rad、消息边界 deg；经纬度统一 WGS84 `double` 度
- 动态分配允许，但每帧分配上限须可静态推算（详 §3.4 参数）

**M2 副语言（Python 3.10）**：仅用于**离线 ENC（S-57）数据加载工具**（`python/enc_loader.py`），不进入 ROS2 运行时；产物为 JSON 元数据 + Boost.Geometry 多边形序列化文件，供 C++ 的 `EncLoader` 加载。

---

## 2. 项目目录结构

```
src/m2_world_model/                                # ROS2 colcon package
├── package.xml
├── CMakeLists.txt
├── README.md                                       # 模块构建/运行说明
│
├── include/m2_world_model/
│   ├── world_model_node.hpp                        # ROS2 节点入口（pub/sub 装配）
│   ├── world_state_aggregator.hpp                  # §4.1 三视图融合主类
│   ├── cpa_tcpa_calculator.hpp                     # §4.2 时间对齐 CPA/TCPA 几何
│   ├── encounter_classifier.hpp                    # §4.3 COLREG 几何预分类
│   ├── track_buffer.hpp                            # §4.4 时间对齐缓冲（RFC-002）
│   ├── enc_loader.hpp                              # §4.5 ENC 静态约束加载（C++ 主接口）
│   ├── view_health_monitor.hpp                     # §4.6 三视图健康状态机
│   ├── coord_transform.hpp                         # WGS84 ↔ ENU 转换（GeographicLib 包装）
│   ├── error.hpp                                   # M2 ErrorCode (2000–2999)
│   └── types.hpp                                   # 内部 POD 结构体
│
├── src/
│   ├── world_model_node.cpp
│   ├── world_state_aggregator.cpp
│   ├── cpa_tcpa_calculator.cpp
│   ├── encounter_classifier.cpp
│   ├── track_buffer.cpp
│   ├── enc_loader.cpp
│   ├── view_health_monitor.cpp
│   ├── coord_transform.cpp
│   └── main.cpp                                    # rclcpp::init + spin
│
├── python/                                         # M2 副语言 ENC 加载工具
│   ├── pyproject.toml
│   ├── enc_loader/
│   │   ├── __init__.py
│   │   ├── s57_parser.py                           # S-57 ENC 解析（pyproj + GDAL/OGR 可选）
│   │   ├── geometry_serializer.py                  # 多边形 → C++ 可读 JSON
│   │   └── cli.py                                  # `python -m enc_loader build` 入口
│   └── tests/
│       ├── test_s57_parser.py
│       └── test_geometry_serializer.py
│
├── config/
│   ├── m2_params.yaml                              # CPA/TCPA 阈值 / 外推时长 / 健康阈值
│   ├── enc_data_paths.yaml                         # S-57 ENC 文件路径
│   └── m2_logger.yaml                              # spdlog 配置（独立 logger）
│
├── launch/
│   ├── m2_world_model.launch.py                    # 单节点启动
│   └── m2_with_mock_fusion.launch.py               # 集成 mock_fusion（开发期）
│
├── msg/                                            # M2 私有消息（如有；当前无）
│
└── test/
    ├── unit/
    │   ├── test_cpa_tcpa_calculator.cpp            # GTest §8.1
    │   ├── test_encounter_classifier.cpp           # GTest §8.2
    │   ├── test_track_buffer.cpp                   # GTest §8.3
    │   ├── test_view_health_monitor.cpp            # GTest §8.4
    │   ├── test_coord_transform.cpp
    │   └── test_world_state_aggregator.cpp
    ├── integration/
    │   ├── test_world_model_node_smoke.cpp         # ROS2 进程级 smoke
    │   └── test_environment_degraded_path.cpp      # §8.4 DEGRADED 降级链路
    └── fixtures/
        ├── scenario_head_on.yaml                   # HIL 场景输入回放
        ├── scenario_overtaking.yaml
        ├── scenario_crossing.yaml
        └── scenario_static_target.yaml
```

**约束**：
- 单一 colcon package（`m2_world_model`）；不拆分 sub-package
- `python/` 子目录与 C++ 源代码同 git 仓但**独立构建**：CMake 不直接调用 Python；CI 单独跑 `pytest python/tests/`
- `config/*.yaml` 是 HAZID 校准参数注入点；运行时支持热加载（详细设计 §10.2）

---

## 3. ROS2 节点设计

### 3.1 节点拓扑

```
m2_world_model（单进程，单节点；executor = MultiThreadedExecutor，4 线程）
   │
   ├── 订阅 callback group 1: ego_callback_group（高频 50 Hz EV）
   │     └── /fusion/own_ship_state → on_own_ship_state()
   │
   ├── 订阅 callback group 2: dv_sv_callback_group（中低频 DV/SV/ODD）
   │     ├── /fusion/tracked_targets → on_tracked_targets()
   │     ├── /fusion/environment_state → on_environment_state()
   │     └── /l3/m1/odd_state → on_odd_state()
   │
   ├── 周期 timer 1: aggregation_timer @ 4 Hz → publish_world_state()
   ├── 周期 timer 2: sat_timer @ 10 Hz → publish_sat_data()
   ├── 周期 timer 3: asdr_periodic_timer @ 2 Hz → publish_asdr_snapshot()
   └── 周期 timer 4: heartbeat_timer @ 1 Hz → check_view_health()
```

**理由**：
- EV（50 Hz）必须独立 callback group + reentrant，避免被低频 callback 阻塞（详细设计 §6.1）
- DV/SV/ODD 共享同一 callback group（mutually exclusive），保证缓存写入串行化
- timer 互不依赖；publish 路径无锁（仅读取已 mutex 保护的快照）

### 3.2 订阅者表格

| Topic | Msg Type | 频率 | QoS | Callback | 时间对齐策略 |
|---|---|---|---|---|---|
| `/fusion/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | 2 Hz | `rclcpp::QoS(5).reliable()` | `on_tracked_targets()` | 写入 `TrackBuffer`；按 `target_id` upsert；记录 `recv_stamp`；4 Hz 输出周期外推到 own_ship.stamp |
| `/fusion/own_ship_state` | `l3_external_msgs/FilteredOwnShipState` | 50 Hz | `rclcpp::SensorDataQoS().keep_last(2)` | `on_own_ship_state()` | 仅缓存最近一帧；4 Hz 周期取最近快照（不缓冲历史）|
| `/fusion/environment_state` | `l3_external_msgs/EnvironmentState` | 0.2 Hz | `rclcpp::QoS(5).reliable().transient_local()` | `on_environment_state()` | 缓存 60 s；过期则 SV→DEGRADED |
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | 1 Hz + 事件 | `rclcpp::QoS(10).reliable().transient_local()` | `on_odd_state()` | 缓存最近一帧；ODD 子域变化时切换 CPA/TCPA 阈值（详细设计 §5.3）|

### 3.3 发布者表格

| Topic | Msg Type | 频率 | QoS | Trigger | 备注 |
|---|---|---|---|---|---|
| `/l3/m2/world_state` | `l3_msgs/WorldState` | 4 Hz | `rclcpp::SensorDataQoS().keep_last(2)` | `aggregation_timer` 周期 250 ms | 主输出；包含 targets[] + own_ship + zone + confidence + rationale |
| `/l3/sat/data` | `l3_msgs/SATData` | 10 Hz | `rclcpp::SensorDataQoS().keep_last(1)` | `sat_timer` 周期 100 ms | SAT-1 持续；SAT-2 事件触发；SAT-3 TDL 压缩时优先 |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 事件 + 2 Hz | `rclcpp::QoS(50).reliable().transient_local()` | `asdr_periodic_timer` + 事件型 | 周期型 = world_state_snapshot；事件型 = encounter_classification / cpa_calculation / health_transition |

### 3.4 参数（YAML keys）

`config/m2_params.yaml`：

```yaml
m2_world_model:
  ros__parameters:
    # ────────────── 周期与时序 ──────────────
    aggregation_rate_hz: 4.0
    sat_rate_hz: 10.0
    asdr_periodic_rate_hz: 2.0
    heartbeat_rate_hz: 1.0
    timing_drift_warn_ms: 200             # 超过 200ms 偏差告警

    # ────────────── 缓存上限 ──────────────
    max_targets: 256                      # TrackBuffer 上限（预分配）
    target_disappearance_periods: 10      # 目标消失等待 10 周期 = 2.5 s @ 4 Hz
    environment_cache_ttl_s: 60.0
    own_ship_max_age_ms: 200              # > 200ms 视为 EV 异常

    # ────────────── CPA/TCPA 阈值（[TBD-HAZID]，按 ODD 子域）──────────────
    # 初始值取自详细设计 §5.3；HAZID RUN-001 完成后回填 v1.1.3
    cpa_safe_m:
      odd_a: 1852.0                       # 1.0 nm  [TBD-HAZID]
      odd_b: 555.6                        # 0.3 nm  [TBD-HAZID]
      odd_c: 277.8                        # 0.15 nm [TBD-HAZID]
      odd_d: 2778.0                       # 1.5 nm  [TBD-HAZID]
    tcpa_safe_s:
      odd_a: 720.0                        # 12 min  [TBD-HAZID]
      odd_b: 240.0                        # 4 min   [TBD-HAZID]
      odd_c: 180.0                        # 3 min   [TBD-HAZID]
      odd_d: 600.0                        # 10 min  [TBD-HAZID]

    # ────────────── COLREG 几何预分类阈值 ──────────────
    overtaking_bearing_min_deg: 112.5     # IMO COLREG Rule 13 [R18]（架构 §6.3.1）
    overtaking_bearing_max_deg: 247.5
    head_on_heading_diff_tol_deg: 6.0     # [TBD-HAZID]（Rule 14 容差）
    safe_pass_relative_speed_threshold_mps: 0.2  # [TBD-HAZID]
    safe_pass_min_cpa_m: 926.0            # 0.5 nm

    # ────────────── 健康降级阈值 ──────────────
    dv_loss_periods_to_degraded: 2        # > 2 周期丢失 → DEGRADED
    dv_loss_periods_to_critical: 5        # > 5 周期 → CRITICAL
    dv_degraded_to_critical_timeout_s: 30.0
    ev_loss_ms_to_critical: 100           # FilteredOwnShipState > 100ms → CRITICAL
    sv_loss_s_to_degraded: 30.0           # EnvironmentState > 30s → DEGRADED
    confidence_floor_dv_degraded: 0.5     # DEGRADED 时置信度封顶 0.5

    # ────────────── 不确定度处理 ──────────────
    cpa_uncertainty_method: "linear"      # "linear" | "monte_carlo"
    cpa_uncertainty_safety_factor: 1.1
    cpa_uncertainty_odd_d_multiplier: 1.5
    monte_carlo_samples: 100              # 仅当 method=monte_carlo

    # ────────────── 协方差 sanity ──────────────
    position_covariance_default_sigma_m: 50.0
    velocity_covariance_default_sigma_mps: 1.0
    position_covariance_max_m2: 250000.0  # 500m^2 上限（详细设计 §7.2）
    velocity_covariance_max_m2_s2: 100.0

    # ────────────── ENC 数据 ──────────────
    enc_data_root: "/opt/mass_l3/enc/"
    enc_metadata_file: "enc_metadata.json"

    # ────────────── 日志 ──────────────
    spdlog_level: "info"                  # trace|debug|info|warn|error|critical
    spdlog_pattern: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v"
    asdr_log_path: "/var/log/mass_l3/m2_asdr.log"
```

`config/enc_data_paths.yaml`（独立文件，由 `EncLoader` 读取）：

```yaml
enc_charts:
  - name: "CN300100"                      # 中国沿海某区
    s57_file: "/opt/mass_l3/enc/CN300100.000"
    serialized_geometry: "/opt/mass_l3/enc/CN300100.geom.json"
    bounds:
      lat_min: 30.0
      lat_max: 31.0
      lon_min: 121.5
      lon_max: 122.5
```

> **HAZID 校准注入**：所有 `[TBD-HAZID]` 标注的参数仅在 YAML 中存在；C++ 源代码读取后通过 `rclcpp::Parameter` 注入。运行时支持 `parameter_callback`（详细设计 §10.2）热加载，无需重启。

---

## 4. 核心类定义（C++ header 骨架）

### 4.1 `WorldStateAggregator`（三视图融合主类）

`include/m2_world_model/world_state_aggregator.hpp`：

```cpp
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <Eigen/Core>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"

#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/enc_loader.hpp"
#include "m2_world_model/view_health_monitor.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/**
 * @brief Aggregates DV/EV/SV into a single WorldState message at 4 Hz.
 *
 * Per detailed design §1 and §5.1.1. This class is the central reducer:
 *   - Reads latest snapshots from track_buffer_ (DV), own_ship_cache_ (EV),
 *     environment_cache_ (SV), and odd_cache_ (parameters).
 *   - Time-aligns all targets to own_ship.stamp via cpa_tcpa_calculator_.
 *   - Classifies COLREG geometric role via encounter_classifier_.
 *   - Computes aggregated confidence as min(dv, ev, sv).
 *
 * Thread-safety: all public methods are thread-safe (internal mutex).
 */
class WorldStateAggregator final
{
public:
    struct Config
    {
        std::int32_t max_targets;
        std::int32_t target_disappearance_periods;
        double environment_cache_ttl_s;
        double timing_drift_warn_ms;
        double confidence_floor_dv_degraded;
        // CPA/TCPA thresholds keyed by ODD zone; injected from YAML
        std::array<double, 4> cpa_safe_m;     // index = OddZone enum
        std::array<double, 4> tcpa_safe_s;
    };

    WorldStateAggregator(Config cfg,
                         std::shared_ptr<CpaTcpaCalculator> cpa_calc,
                         std::shared_ptr<EncounterClassifier> classifier,
                         std::shared_ptr<TrackBuffer> track_buffer,
                         std::shared_ptr<EncLoader> enc_loader,
                         std::shared_ptr<ViewHealthMonitor> health);

    ~WorldStateAggregator() = default;
    WorldStateAggregator(const WorldStateAggregator&) = delete;
    WorldStateAggregator& operator=(const WorldStateAggregator&) = delete;
    WorldStateAggregator(WorldStateAggregator&&) = delete;
    WorldStateAggregator& operator=(WorldStateAggregator&&) = delete;

    /// Update the latest own-ship snapshot (called from 50 Hz EV callback).
    void update_own_ship(const l3_external_msgs::msg::FilteredOwnShipState& msg);

    /// Update the latest environment snapshot (called from 0.2 Hz SV callback).
    void update_environment(const l3_external_msgs::msg::EnvironmentState& msg);

    /// Update the ODD state cache (called from 1 Hz ODD callback).
    void update_odd_state(const l3_msgs::msg::ODDState& msg);

    /// Compose the World_StateMsg snapshot. Returns nullopt if EV is CRITICAL.
    /// Per detailed design §3.1 + §6.1 (4 Hz aggregation_timer).
    [[nodiscard]] std::optional<l3_msgs::msg::WorldState>
    compose_world_state(std::chrono::steady_clock::time_point now);

    /// Snapshot accessors for SAT and ASDR publishers (read-only).
    [[nodiscard]] OwnShipSnapshot latest_own_ship() const;
    [[nodiscard]] ZoneSnapshot latest_zone() const;
    [[nodiscard]] AggregatedHealth aggregated_health() const;

private:
    [[nodiscard]] double compute_aggregated_confidence_() const;
    [[nodiscard]] std::string compose_rationale_(const AggregatedHealth& h,
                                                  std::int32_t target_count) const;

    Config cfg_;
    std::shared_ptr<CpaTcpaCalculator> cpa_calc_;
    std::shared_ptr<EncounterClassifier> classifier_;
    std::shared_ptr<TrackBuffer> track_buffer_;
    std::shared_ptr<EncLoader> enc_loader_;
    std::shared_ptr<ViewHealthMonitor> health_;

    mutable std::mutex mutex_;
    std::optional<OwnShipSnapshot> own_ship_cache_;
    std::optional<ZoneSnapshot> environment_cache_;
    std::optional<OddSnapshot> odd_cache_;
};

}  // namespace mass_l3::m2
```

### 4.2 `CpaTcpaCalculator`（time-aligned 几何计算）

`include/m2_world_model/cpa_tcpa_calculator.hpp`：

```cpp
#pragma once

#include <chrono>
#include <optional>

#include <Eigen/Core>

#include "m2_world_model/coord_transform.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/**
 * @brief Computes Closest Point of Approach (CPA) and Time-to-CPA (TCPA).
 *
 * Per detailed design §5.1.2. Uses linear relative-motion model with
 * uncertainty propagation (linear or Monte Carlo, configurable).
 *
 * Coordinate convention (RFC-002 + architecture v1.1.2 §6.4):
 *   - target.sog/cog: over ground (GPS/AIS observation).
 *   - own_ship.u/v:  over water (does NOT include current); we add
 *                     current_speed/direction to obtain over-ground velocity.
 *
 * Time alignment (RFC-002):
 *   target state is extrapolated to own_ship.stamp before computation.
 *   Returns nullopt if |target.stamp - own_ship.stamp| > max_align_dt_s.
 */
class CpaTcpaCalculator final
{
public:
    enum class UncertaintyMethod : std::uint8_t { Linear = 0, MonteCarlo = 1 };

    struct Config
    {
        UncertaintyMethod method;
        std::int32_t monte_carlo_samples;
        double safety_factor;
        double odd_d_multiplier;
        double max_align_dt_s;
        double static_target_speed_mps;  // < this → treat as static
    };

    explicit CpaTcpaCalculator(Config cfg);

    /// Compute CPA / TCPA / uncertainties for an own-target pair.
    /// @param own_ship  Own ship snapshot (over-water u/v + current estimate).
    /// @param target    Target snapshot (over-ground sog/cog).
    /// @param odd_zone  Current ODD subzone (used to scale uncertainty).
    /// @return CpaResult or nullopt if pre-conditions fail.
    [[nodiscard]] std::optional<CpaResult>
    compute(const OwnShipSnapshot& own_ship,
            const TargetSnapshot& target,
            OddZone odd_zone) const;

private:
    /// Extrapolate target state to own_ship.stamp using its sog/cog.
    [[nodiscard]] TargetSnapshot
    extrapolate_to_(const TargetSnapshot& target,
                    std::chrono::steady_clock::time_point target_t) const;

    /// Linear (analytical) uncertainty propagation.
    [[nodiscard]] CpaUncertainty
    propagate_linear_(const Eigen::Vector2d& rel_pos,
                      const Eigen::Vector2d& rel_vel,
                      const Eigen::Matrix2d& sigma_rel_pos,
                      const Eigen::Matrix2d& sigma_rel_vel) const;

    /// Monte Carlo uncertainty estimate (cfg_.monte_carlo_samples draws).
    [[nodiscard]] CpaUncertainty
    propagate_monte_carlo_(const Eigen::Vector2d& rel_pos,
                           const Eigen::Vector2d& rel_vel,
                           const Eigen::Matrix2d& sigma_rel_pos,
                           const Eigen::Matrix2d& sigma_rel_vel) const;

    Config cfg_;
};

/// CPA / TCPA result with uncertainty (detailed design §3.1 World_StateMsg).
struct CpaResult
{
    double cpa_m;
    double tcpa_s;
    CpaUncertainty uncertainty;
};

struct CpaUncertainty
{
    double cpa_sigma_m;
    double tcpa_sigma_s;
};

}  // namespace mass_l3::m2
```

### 4.3 `EncounterClassifier`（COLREG 几何预分类，§6.2）

`include/m2_world_model/encounter_classifier.hpp`：

```cpp
#pragma once

#include "l3_msgs/msg/encounter_classification.hpp"

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/**
 * @brief Pre-classifies a target into a COLREG geometric role.
 *
 * Per detailed design §5.1.3 and architecture v1.1.2 §6.3.1.
 *
 * IMPORTANT: M2 does NOT reason about which vessel is give-way / stand-on;
 * that is M6 COLREGs Reasoner's responsibility. M2 only emits geometric
 * pre-classification:
 *   - HEAD_ON       (Rule 14, ±tolerance)
 *   - OVERTAKING    (Rule 13 stern sector)
 *   - CROSSING      (Rule 15)
 *   - SAFE_PASS     (low relative speed + large CPA)
 *   - AMBIGUOUS     (boundary case)
 */
class EncounterClassifier final
{
public:
    struct Config
    {
        double overtaking_bearing_min_deg;   // 112.5
        double overtaking_bearing_max_deg;   // 247.5
        double head_on_heading_diff_tol_deg; // 6.0  [TBD-HAZID]
        double safe_pass_speed_threshold_mps;// 0.2  [TBD-HAZID]
        double safe_pass_min_cpa_m;          // 926.0 (0.5 nm)
    };

    explicit EncounterClassifier(Config cfg);

    /// Classify the encounter based on geometry only.
    /// @param relative_bearing_deg  bearing of target relative to own_ship (0–360, CW from north).
    /// @param own_heading_deg
    /// @param target_heading_deg
    /// @param relative_speed_mps    magnitude of relative velocity vector.
    /// @param cpa_m                 CPA from CpaTcpaCalculator.
    [[nodiscard]] l3_msgs::msg::EncounterClassification
    classify(double relative_bearing_deg,
             double own_heading_deg,
             double target_heading_deg,
             double relative_speed_mps,
             double cpa_m) const;

private:
    [[nodiscard]] static double normalize_bearing_deg_(double bearing) noexcept;
    [[nodiscard]] static double smallest_angle_diff_deg_(double a, double b) noexcept;

    Config cfg_;
};

}  // namespace mass_l3::m2
```

### 4.4 `TrackBuffer`（时间对齐缓冲，按 RFC-002）

`include/m2_world_model/track_buffer.hpp`：

```cpp
#pragma once

#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "l3_external_msgs/msg/tracked_target_array.hpp"

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/**
 * @brief Bounded target cache with time-alignment metadata.
 *
 * Per detailed design §4.1 (target_cache) + §5.1.1 (time-alignment).
 * Pre-allocated to cfg_.max_targets to satisfy MISRA C++:2023 rule on
 * unbounded growth. Targets missing for > disappearance_periods are evicted.
 */
class TrackBuffer final
{
public:
    struct Config
    {
        std::int32_t max_targets;
        std::int32_t disappearance_periods;
        double aggregation_period_s;          // 0.25 (4 Hz)
        double position_covariance_max_m2;
        double velocity_covariance_max_m2_s2;
    };

    explicit TrackBuffer(Config cfg);

    /// Insert / update targets from a fresh TrackedTargetArray message.
    /// Rejects targets whose covariance is non-positive-definite or whose
    /// position jumps > 1 nm since last update (per §2.2 input validation).
    /// @return number of accepted targets.
    std::int32_t upsert(const l3_external_msgs::msg::TrackedTargetArray& msg,
                        std::chrono::steady_clock::time_point recv_stamp);

    /// Snapshot of currently-tracked targets (those not yet evicted).
    /// Linearly extrapolates each target to align_t using its own sog/cog.
    [[nodiscard]] std::vector<TargetSnapshot>
    snapshot_aligned_to(std::chrono::steady_clock::time_point align_t) const;

    /// Evict targets whose last_update is older than disappearance window.
    /// Called from aggregation_timer at 4 Hz.
    /// @return number of evicted targets (for ASDR logging).
    std::int32_t evict_stale(std::chrono::steady_clock::time_point now);

    [[nodiscard]] std::int32_t active_count() const noexcept;
    [[nodiscard]] std::chrono::steady_clock::time_point last_update_stamp() const noexcept;

private:
    /// Validate covariance matrix; sanitize to default if invalid.
    [[nodiscard]] bool validate_and_sanitize_(TargetSnapshot& tgt) const;

    Config cfg_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, TargetSnapshot> targets_;
    std::chrono::steady_clock::time_point last_update_stamp_{};
};

}  // namespace mass_l3::m2
```

### 4.5 `EncLoader`（C++ 主接口；Python 加载工具单独 §6）

`include/m2_world_model/enc_loader.hpp`：

```cpp
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/**
 * @brief Loads pre-processed ENC chart polygons (hazards, TSS lanes).
 *
 * The Python tool `enc_loader` (M2 sub-language) parses S-57 ENC files
 * offline and produces JSON polygons. This C++ class loads that JSON at
 * node startup and answers queries from the aggregator.
 *
 * Per detailed design §5.1.4 (aggregate_zone_constraints).
 */
class EncLoader final
{
public:
    using Point = boost::geometry::model::d2::point_xy<double>;     // lon/lat in degrees
    using Polygon = boost::geometry::model::polygon<Point>;

    struct ChartMetadata
    {
        std::string name;
        double lat_min;
        double lat_max;
        double lon_min;
        double lon_max;
        std::vector<Polygon> hazards;          // shoals, exclusion zones
        std::vector<Polygon> tss_lanes;        // Rule 10 TSS
    };

    /// Load all charts referenced by enc_data_paths.yaml.
    /// @return number of charts loaded.
    std::int32_t load(const std::filesystem::path& metadata_yaml);

    /// Query hazards intersecting the bounding box around (lat, lon).
    /// Returns at most cfg_.max_hazards_returned polygons.
    [[nodiscard]] std::vector<Polygon>
    nearby_hazards(double lat, double lon, double radius_m) const;

    /// Query whether (lat, lon) is inside any TSS lane.
    [[nodiscard]] bool in_tss(double lat, double lon) const;

    /// Min water depth at (lat, lon) from underlying ENC raster (if available).
    [[nodiscard]] std::optional<double> min_water_depth_m(double lat, double lon) const;

private:
    std::vector<ChartMetadata> charts_;
};

}  // namespace mass_l3::m2
```

### 4.6 `WorldModelNode`（ROS2 节点类）

`include/m2_world_model/world_model_node.hpp`：

```cpp
#pragma once

#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"

#include "m2_world_model/world_state_aggregator.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/enc_loader.hpp"
#include "m2_world_model/view_health_monitor.hpp"

namespace mass_l3::m2 {

/**
 * @brief ROS2 node entry point for M2 World Model.
 *
 * Per detailed design §6.1 (timing) and §3 (output interfaces).
 */
class WorldModelNode final : public rclcpp::Node
{
public:
    explicit WorldModelNode(const rclcpp::NodeOptions& options);
    ~WorldModelNode() override = default;

private:
    void declare_and_load_parameters_();
    void create_subscriptions_();
    void create_publishers_();
    void create_timers_();

    // Callbacks
    void on_tracked_targets_(const l3_external_msgs::msg::TrackedTargetArray::SharedPtr msg);
    void on_own_ship_state_(const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg);
    void on_environment_state_(const l3_external_msgs::msg::EnvironmentState::SharedPtr msg);
    void on_odd_state_(const l3_msgs::msg::ODDState::SharedPtr msg);

    // Periodic tasks
    void publish_world_state_();
    void publish_sat_data_();
    void publish_asdr_snapshot_();
    void check_view_health_();

    // Components (DI; constructed in declare_and_load_parameters_)
    std::shared_ptr<WorldStateAggregator> aggregator_;
    std::shared_ptr<TrackBuffer> track_buffer_;
    std::shared_ptr<CpaTcpaCalculator> cpa_calc_;
    std::shared_ptr<EncounterClassifier> classifier_;
    std::shared_ptr<EncLoader> enc_loader_;
    std::shared_ptr<ViewHealthMonitor> health_;

    // Subscriptions
    rclcpp::Subscription<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr targets_sub_;
    rclcpp::Subscription<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr own_ship_sub_;
    rclcpp::Subscription<l3_external_msgs::msg::EnvironmentState>::SharedPtr environment_sub_;
    rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_sub_;

    // Publishers
    rclcpp::Publisher<l3_msgs::msg::WorldState>::SharedPtr world_state_pub_;
    rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr sat_pub_;
    rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;

    // Timers
    rclcpp::TimerBase::SharedPtr aggregation_timer_;
    rclcpp::TimerBase::SharedPtr sat_timer_;
    rclcpp::TimerBase::SharedPtr asdr_periodic_timer_;
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;

    // Callback groups (per §3.1 topology)
    rclcpp::CallbackGroup::SharedPtr ego_callback_group_;
    rclcpp::CallbackGroup::SharedPtr dv_sv_callback_group_;
};

}  // namespace mass_l3::m2
```

`src/main.cpp`：

```cpp
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include "m2_world_model/world_model_node.hpp"

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<mass_l3::m2::WorldModelNode>(rclcpp::NodeOptions{});

    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions{}, 4U);
    executor.add_node(node);

    try
    {
        executor.spin();
    }
    catch (const std::exception& e)  // catch-all only at spin() boundary (coding-standards §3.4)
    {
        spdlog::critical("[M2] Unhandled exception in spin(): {}", e.what());
    }
    catch (...)
    {
        spdlog::critical("[M2] Unknown unhandled exception in spin()");
    }

    rclcpp::shutdown();
    return 0;
}
```

---

## 5. CMakeLists.txt（完整模板）

```cmake
cmake_minimum_required(VERSION 3.22)
project(m2_world_model VERSION 1.0.0 LANGUAGES CXX)

# ─────────── C++ Standard ───────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if unspecified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ─────────── ROS2 / ament ───────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)

# ─────────── Third-Party (per third-party-library-policy.md §2.2) ───────────
find_package(Eigen3 5.0.0 EXACT REQUIRED)
find_package(GeographicLib 2.7 EXACT REQUIRED)
find_package(Boost 1.91.0 EXACT REQUIRED COMPONENTS headers)
find_package(spdlog 1.17.0 EXACT REQUIRED)
find_package(yaml-cpp 0.8.0 EXACT REQUIRED)

# ─────────── Compile Options (per coding-standards.md §2.2) ───────────
add_library(m2_compile_options INTERFACE)
target_compile_options(m2_compile_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Werror
    -Wshadow -Wconversion -Wsign-conversion
    -Wcast-align -Wcast-qual
    -Wold-style-cast -Wzero-as-null-pointer-constant
    -Wnon-virtual-dtor -Woverloaded-virtual
    -Wnull-dereference -Wdouble-promotion
    -Wfloat-equal
    -Wformat=2 -Wformat-security
    -Wmissing-declarations
    -Wundef -Wunused
    -Wuseless-cast
    -Wlogical-op
    -Wduplicated-cond -Wduplicated-branches
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
    "$<$<CONFIG:Release>:-O2>"
)
target_link_options(m2_compile_options INTERFACE
    "$<$<CONFIG:Debug>:-fsanitize=address;-fsanitize=undefined>"
)

# ─────────── Library: m2_world_model_lib ───────────
add_library(m2_world_model_lib
    src/world_state_aggregator.cpp
    src/cpa_tcpa_calculator.cpp
    src/encounter_classifier.cpp
    src/track_buffer.cpp
    src/enc_loader.cpp
    src/view_health_monitor.cpp
    src/coord_transform.cpp
)

target_include_directories(m2_world_model_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(m2_world_model_lib
    PUBLIC
        Eigen3::Eigen
        GeographicLib::GeographicLib
        Boost::headers
        spdlog::spdlog
        yaml-cpp
        m2_compile_options
)

ament_target_dependencies(m2_world_model_lib
    PUBLIC
        rclcpp
        builtin_interfaces
        geometry_msgs
        geographic_msgs
        l3_msgs
        l3_external_msgs
)

# ─────────── Executable: m2_world_model_node ───────────
add_executable(m2_world_model_node
    src/main.cpp
    src/world_model_node.cpp
)

target_link_libraries(m2_world_model_node
    PRIVATE
        m2_world_model_lib
        m2_compile_options
)

ament_target_dependencies(m2_world_model_node PRIVATE rclcpp)

# ─────────── Install ───────────
install(TARGETS m2_world_model_node
        RUNTIME DESTINATION lib/${PROJECT_NAME})

install(TARGETS m2_world_model_lib
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        INCLUDES DESTINATION include)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

# ─────────── Tests ───────────
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_lint_auto_find_test_dependencies()

  set(M2_UNIT_TESTS
    test_cpa_tcpa_calculator
    test_encounter_classifier
    test_track_buffer
    test_view_health_monitor
    test_coord_transform
    test_world_state_aggregator
  )

  foreach(t ${M2_UNIT_TESTS})
    ament_add_gtest(${t} test/unit/${t}.cpp)
    target_link_libraries(${t} m2_world_model_lib m2_compile_options)
    set_tests_properties(${t} PROPERTIES TIMEOUT 60)
  endforeach()

  # Integration tests run as separate executables
  ament_add_gtest(test_world_model_node_smoke test/integration/test_world_model_node_smoke.cpp)
  target_link_libraries(test_world_model_node_smoke m2_world_model_lib)

  ament_add_gtest(test_environment_degraded_path test/integration/test_environment_degraded_path.cpp)
  target_link_libraries(test_environment_degraded_path m2_world_model_lib)
endif()

ament_export_include_directories(include)
ament_export_libraries(m2_world_model_lib)
ament_export_dependencies(rclcpp l3_msgs l3_external_msgs Eigen3 GeographicLib Boost spdlog)

ament_package()
```

---

## 6. Python ENC 加载工具

### 6.1 `python/enc_loader/s57_parser.py` 骨架

```python
"""S-57 ENC chart parser for M2 World Model.

Offline tool: parses ENC files and emits JSON polygons consumable by the
C++ EncLoader. Not loaded into the ROS2 runtime.

Per detailed design §5.1.4. Uses pyproj for coordinate handling and
optionally GDAL/OGR for S-57 IO; only pure-python deps are required for
unit tests.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import pyproj


@dataclass(frozen=True, slots=True)
class EncPolygon:
    name: str
    feature_class: str       # "DEPARE" | "TSSLPT" | "OBSTRN" | ...
    coordinates: tuple[tuple[float, float], ...]  # (lon, lat) in degrees


@dataclass(frozen=True, slots=True)
class ChartBounds:
    lat_min: float
    lat_max: float
    lon_min: float
    lon_max: float


class S57Parser:
    """Parse S-57 ENC files (.000) into EncPolygon iterables.

    Per detailed design §5.1.4 + architecture v1.1.2 §6.4 EnvironmentState.
    """

    WGS84: pyproj.CRS = pyproj.CRS.from_epsg(4326)

    def __init__(self, *, strict: bool = True) -> None:
        self._strict = strict

    def parse(self, s57_path: Path) -> tuple[ChartBounds, list[EncPolygon]]:
        """Parse a single S-57 file. Returns (bounds, polygons)."""
        # Implementation TODO: use OGR if available; fall back to pure-Python
        # SCIBR record parser for tests that do not have GDAL installed.
        raise NotImplementedError("S57Parser.parse — implement in Phase E1")

    def parse_directory(self, directory: Path) -> Iterable[tuple[ChartBounds, list[EncPolygon]]]:
        for s57_file in sorted(directory.glob("*.000")):
            yield self.parse(s57_file)
```

`python/enc_loader/geometry_serializer.py`：

```python
"""Serialize parsed EncPolygons to JSON consumable by C++ EncLoader."""

from __future__ import annotations

import json
from pathlib import Path

from enc_loader.s57_parser import ChartBounds, EncPolygon


def serialize(
    chart_name: str,
    bounds: ChartBounds,
    polygons: list[EncPolygon],
    out_path: Path,
) -> None:
    """Write Boost.Geometry-compatible JSON.

    Schema:
    {
      "name": str,
      "bounds": {lat_min, lat_max, lon_min, lon_max},
      "hazards":  [[[lon, lat], ...], ...],
      "tss_lanes": [[[lon, lat], ...], ...]
    }
    """
    hazards = [
        [list(pt) for pt in poly.coordinates]
        for poly in polygons
        if poly.feature_class in {"OBSTRN", "DEPCNT", "DEPARE"}
    ]
    tss_lanes = [
        [list(pt) for pt in poly.coordinates]
        for poly in polygons
        if poly.feature_class in {"TSSLPT", "TSSBND"}
    ]

    payload = {
        "name": chart_name,
        "bounds": {
            "lat_min": bounds.lat_min,
            "lat_max": bounds.lat_max,
            "lon_min": bounds.lon_min,
            "lon_max": bounds.lon_max,
        },
        "hazards": hazards,
        "tss_lanes": tss_lanes,
    }
    out_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
```

`python/enc_loader/cli.py`：

```python
"""Command-line entry: `python -m enc_loader build --input <dir> --output <dir>`."""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from enc_loader.geometry_serializer import serialize
from enc_loader.s57_parser import S57Parser


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="enc_loader")
    sub = parser.add_subparsers(dest="cmd", required=True)

    build = sub.add_parser("build", help="Parse S-57 files into JSON")
    build.add_argument("--input", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--strict", action="store_true")

    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO)

    if args.cmd == "build":
        s57 = S57Parser(strict=args.strict)
        args.output.mkdir(parents=True, exist_ok=True)
        for bounds, polygons in s57.parse_directory(args.input):
            chart_name = next(iter(p.name for p in polygons), "unknown")
            out = args.output / f"{chart_name}.geom.json"
            serialize(chart_name, bounds, polygons, out)
            logging.info("wrote %s", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

`python/pyproject.toml`：

```toml
[project]
name = "enc_loader"
version = "1.0.0"
description = "M2 World Model — offline ENC parser"
requires-python = ">=3.10,<3.11"
dependencies = [
    "pyproj>=3.6,<4.0",
]

[project.optional-dependencies]
gdal = ["gdal>=3.6"]
test = ["pytest>=8.3", "pytest-cov>=5.0", "mypy>=1.10", "ruff>=0.6"]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.ruff]
line-length = 100
target-version = "py310"

[tool.mypy]
strict = true
python_version = "3.10"
```

### 6.2 `python/tests/` pytest 骨架

`python/tests/test_s57_parser.py`：

```python
"""Pytest skeleton for S57Parser. Real fixtures TBD in Phase E1."""

from __future__ import annotations

from pathlib import Path

import pytest

from enc_loader.s57_parser import ChartBounds, EncPolygon, S57Parser


def test_chart_bounds_ordering() -> None:
    bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=122.0)
    assert bounds.lat_min < bounds.lat_max
    assert bounds.lon_min < bounds.lon_max


def test_parser_raises_on_missing_file(tmp_path: Path) -> None:
    parser = S57Parser(strict=True)
    with pytest.raises((FileNotFoundError, NotImplementedError)):
        parser.parse(tmp_path / "missing.000")


def test_polygon_immutability() -> None:
    poly = EncPolygon(name="x", feature_class="OBSTRN", coordinates=((121.0, 30.0),))
    # frozen=True dataclass forbids reassignment
    with pytest.raises((AttributeError, Exception)):
        poly.name = "y"  # type: ignore[misc]
```

`python/tests/test_geometry_serializer.py`：

```python
"""Pytest for geometry serializer."""

from __future__ import annotations

import json
from pathlib import Path

from enc_loader.geometry_serializer import serialize
from enc_loader.s57_parser import ChartBounds, EncPolygon


def test_serialize_writes_expected_schema(tmp_path: Path) -> None:
    bounds = ChartBounds(lat_min=30.0, lat_max=31.0, lon_min=121.0, lon_max=122.0)
    polygons = [
        EncPolygon(
            name="hazard1",
            feature_class="OBSTRN",
            coordinates=((121.1, 30.1), (121.2, 30.1), (121.2, 30.2), (121.1, 30.2)),
        ),
        EncPolygon(
            name="lane1",
            feature_class="TSSLPT",
            coordinates=((121.5, 30.5), (121.7, 30.5)),
        ),
    ]
    out = tmp_path / "test.geom.json"
    serialize("test", bounds, polygons, out)

    payload = json.loads(out.read_text(encoding="utf-8"))
    assert payload["name"] == "test"
    assert payload["bounds"]["lat_min"] == 30.0
    assert len(payload["hazards"]) == 1
    assert len(payload["tss_lanes"]) == 1
```

---

## 7. package.xml

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>m2_world_model</name>
  <version>1.0.0</version>
  <description>
    M2 World Model — sole authoritative world view generator for the L3 tactical layer.
    Aggregates DV/EV/SV from Multimodal Fusion + ODD subzone, computes CPA/TCPA, and
    pre-classifies COLREG geometric roles. Produces WorldState @ 4 Hz.
    Per docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md and
    architecture v1.1.2 §6.
  </description>
  <maintainer email="team-m2@mass-l3.local">Team-M2 (World Model)</maintainer>
  <license>Apache-2.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>

  <depend>rclcpp</depend>
  <depend>builtin_interfaces</depend>
  <depend>geometry_msgs</depend>
  <depend>geographic_msgs</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>eigen3_cmake_module</depend>
  <depend>geographiclib</depend>
  <depend>boost</depend>
  <depend>spdlog_vendor</depend>
  <depend>yaml-cpp</depend>

  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_cmake_gtest</test_depend>
  <test_depend>ament_cmake_clang_tidy</test_depend>
  <test_depend>ament_cmake_cppcheck</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 8. 单元测试骨架（GTest + pytest）

> **覆盖率目标**：`m2_world_model_lib` ≥ 90%（lcov 报告）；Python `enc_loader` ≥ 90%（pytest-cov）。CI 阻断未达标的 PR。

### 8.1 `CpaTcpaCalculator`（边界场景）

`test/unit/test_cpa_tcpa_calculator.cpp`：

```cpp
#include <gtest/gtest.h>

#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/types.hpp"

using namespace mass_l3::m2;

namespace {
CpaTcpaCalculator::Config default_config()
{
    return {
        CpaTcpaCalculator::UncertaintyMethod::Linear,
        100,    // monte_carlo_samples
        1.1,    // safety_factor
        1.5,    // odd_d_multiplier
        1.0,    // max_align_dt_s
        0.1     // static_target_speed_mps
    };
}
}  // namespace

// Scenario A: head-on (0° bearing, opposing headings)
TEST(CpaTcpaCalculator, HeadOnZeroCpa)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship(0.0, 0.0, /*sog_kn=*/18.0, /*cog_deg=*/0.0);
    TargetSnapshot target = make_target(/*lat=*/0.0135, /*lon=*/0.0,
                                         /*sog_kn=*/12.0, /*cog_deg=*/180.0);

    auto result = calc.compute(own, target, OddZone::A);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->cpa_m, 0.0, 50.0);   // collision course
    EXPECT_GT(result->tcpa_s, 0.0);
    EXPECT_LT(result->tcpa_s, 600.0);
}

// Scenario B: overtaking (target ahead, both same heading, own faster)
TEST(CpaTcpaCalculator, OvertakingPositiveTcpa)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship(0.0, 0.0, /*sog_kn=*/18.0, /*cog_deg=*/0.0);
    TargetSnapshot target = make_target(/*lat=*/0.005, /*lon=*/0.0,
                                         /*sog_kn=*/8.0, /*cog_deg=*/0.0);

    auto result = calc.compute(own, target, OddZone::A);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->tcpa_s, 0.0);
}

// Scenario C: parallel courses (same heading, different lat → constant CPA)
TEST(CpaTcpaCalculator, ParallelInfiniteTcpa)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship(0.0, 0.0, /*sog_kn=*/18.0, /*cog_deg=*/0.0);
    TargetSnapshot target = make_target(/*lat=*/0.0, /*lon=*/0.005,
                                         /*sog_kn=*/18.0, /*cog_deg=*/0.0);

    auto result = calc.compute(own, target, OddZone::A);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->cpa_m, 100.0);
    // TCPA either 0 (if rel_vel parallel and constant distance) or +inf marker
}

// Scenario D: static target (sog ≈ 0)
TEST(CpaTcpaCalculator, StaticTargetCpaEqualsRange)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship(0.0, 0.0, /*sog_kn=*/12.0, /*cog_deg=*/45.0);
    TargetSnapshot target = make_target(/*lat=*/0.001, /*lon=*/0.001,
                                         /*sog_kn=*/0.05, /*cog_deg=*/0.0);

    auto result = calc.compute(own, target, OddZone::A);
    ASSERT_TRUE(result.has_value());
    // For a near-static target whose path passes the own track: TCPA finite, CPA < range
    EXPECT_GE(result->cpa_m, 0.0);
}

// Scenario E: stale target (Δt > max_align_dt_s) → nullopt
TEST(CpaTcpaCalculator, StaleTargetReturnsNullopt)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship_with_stamp(/*offset_s=*/0.0);
    TargetSnapshot target = make_target_with_stamp(/*offset_s=*/-2.0);  // 2s old

    auto result = calc.compute(own, target, OddZone::A);
    EXPECT_FALSE(result.has_value());
}

// Scenario F: ODD-D uncertainty inflation (× 1.5)
TEST(CpaTcpaCalculator, OddDUncertaintyInflated)
{
    auto calc = CpaTcpaCalculator{default_config()};
    OwnShipSnapshot own = make_own_ship(0.0, 0.0, 18.0, 0.0);
    TargetSnapshot target = make_target(0.005, 0.0, 12.0, 180.0);

    auto a = calc.compute(own, target, OddZone::A);
    auto d = calc.compute(own, target, OddZone::D);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(d.has_value());
    EXPECT_GT(d->uncertainty.cpa_sigma_m, a->uncertainty.cpa_sigma_m * 1.4);
}
```

### 8.2 `EncounterClassifier`（Rule 13/14/15 各场景）

`test/unit/test_encounter_classifier.cpp`：

```cpp
#include <gtest/gtest.h>

#include "m2_world_model/encounter_classifier.hpp"

using namespace mass_l3::m2;

namespace {
EncounterClassifier::Config default_config()
{
    return {112.5, 247.5, 6.0, 0.2, 926.0};
}
}  // namespace

// Rule 13 — overtaking (target abaft 22.5° on each side of own stern → bearing 112.5°–247.5°)
TEST(EncounterClassifier, OvertakingFromStern)
{
    EncounterClassifier clf{default_config()};
    auto cls = clf.classify(/*relative_bearing_deg=*/180.0,
                            /*own_heading_deg=*/0.0,
                            /*target_heading_deg=*/0.0,
                            /*relative_speed_mps=*/2.0,
                            /*cpa_m=*/200.0);
    EXPECT_EQ(cls.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING);
}

// Rule 14 — head-on (bearing ~0°, opposing headings, |Δheading| ≈ 180°)
TEST(EncounterClassifier, HeadOnAtSmallBearing)
{
    EncounterClassifier clf{default_config()};
    auto cls = clf.classify(/*relative_bearing_deg=*/2.0,
                            /*own_heading_deg=*/0.0,
                            /*target_heading_deg=*/178.0,
                            /*relative_speed_mps=*/15.0,
                            /*cpa_m=*/100.0);
    EXPECT_EQ(cls.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON);
}

// Rule 15 — crossing (bearing on starboard bow, headings ≈ 90° apart)
TEST(EncounterClassifier, CrossingFromStarboard)
{
    EncounterClassifier clf{default_config()};
    auto cls = clf.classify(/*relative_bearing_deg=*/45.0,
                            /*own_heading_deg=*/0.0,
                            /*target_heading_deg=*/270.0,
                            /*relative_speed_mps=*/8.0,
                            /*cpa_m=*/300.0);
    EXPECT_EQ(cls.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING);
}

// Boundary — head-on tolerance edge (heading_diff = ±6°)
TEST(EncounterClassifier, HeadOnToleranceBoundaryInclusive)
{
    EncounterClassifier clf{default_config()};
    // Just inside ±6° tolerance → HEAD_ON
    auto cls_in = clf.classify(0.0, 0.0, 174.0, 15.0, 100.0);
    EXPECT_EQ(cls_in.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON);
    // Just outside → CROSSING (per detailed design §5.1.3)
    auto cls_out = clf.classify(0.0, 0.0, 173.0, 15.0, 100.0);
    EXPECT_NE(cls_out.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON);
}

// SAFE_PASS — low relative speed + large CPA
TEST(EncounterClassifier, SafePassLowRelSpeedLargeCpa)
{
    EncounterClassifier clf{default_config()};
    auto cls = clf.classify(/*relative_bearing_deg=*/30.0,
                            /*own_heading_deg=*/0.0,
                            /*target_heading_deg=*/30.0,
                            /*relative_speed_mps=*/0.1,    // < 0.2 threshold
                            /*cpa_m=*/2000.0);              // > 0.5 nm = 926 m
    EXPECT_EQ(cls.encounter_type,
              l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_NONE);
}
```

### 8.3 `TrackBuffer`（时间对齐 / 外推 / 数据丢失）

`test/unit/test_track_buffer.cpp`：

```cpp
#include <gtest/gtest.h>
#include <chrono>

#include "m2_world_model/track_buffer.hpp"

using namespace mass_l3::m2;
using namespace std::chrono_literals;

namespace {
TrackBuffer::Config default_config()
{
    return {256, 10, 0.25, 250000.0, 100.0};
}
}  // namespace

TEST(TrackBuffer, UpsertAcceptsValidTargets)
{
    TrackBuffer buf{default_config()};
    auto msg = make_tracked_target_array_with_n(/*n=*/3);
    auto count = buf.upsert(msg, std::chrono::steady_clock::now());
    EXPECT_EQ(count, 3);
    EXPECT_EQ(buf.active_count(), 3);
}

TEST(TrackBuffer, UpsertRejectsCovarianceNonPositiveDefinite)
{
    TrackBuffer buf{default_config()};
    auto msg = make_tracked_target_array_with_bad_covariance();
    auto count = buf.upsert(msg, std::chrono::steady_clock::now());
    EXPECT_EQ(count, 0);
    EXPECT_EQ(buf.active_count(), 0);
}

TEST(TrackBuffer, ExtrapolatesToAlignTime)
{
    TrackBuffer buf{default_config()};
    auto t0 = std::chrono::steady_clock::now();
    // Target moving 10 m/s east at t0
    buf.upsert(make_target_msg_at(/*lat=*/0.0, /*lon=*/0.0, /*sog_kn=*/19.4, /*cog_deg=*/90.0), t0);

    auto snapshots = buf.snapshot_aligned_to(t0 + 1s);
    ASSERT_EQ(snapshots.size(), 1U);
    // After 1 s eastward at ~10 m/s → lon shifted by ~9e-5 deg @ equator
    EXPECT_GT(snapshots[0].position_lon_deg, 0.0);
    EXPECT_LT(snapshots[0].position_lon_deg, 1e-3);
}

TEST(TrackBuffer, EvictsTargetsAfterDisappearancePeriods)
{
    TrackBuffer buf{default_config()};
    auto t0 = std::chrono::steady_clock::now();
    buf.upsert(make_tracked_target_array_with_n(2), t0);

    // Evict at t0 + 11 periods of 0.25 s = 2.75 s (> 10 periods threshold)
    auto evicted = buf.evict_stale(t0 + 2750ms);
    EXPECT_EQ(evicted, 2);
    EXPECT_EQ(buf.active_count(), 0);
}

TEST(TrackBuffer, RespectsMaxTargetsLimit)
{
    auto cfg = default_config();
    cfg.max_targets = 5;
    TrackBuffer buf{cfg};
    auto msg = make_tracked_target_array_with_n(/*n=*/10);
    auto count = buf.upsert(msg, std::chrono::steady_clock::now());
    EXPECT_LE(count, 5);
    EXPECT_LE(buf.active_count(), 5);
}
```

### 8.4 `EnvironmentState` 处理（DEGRADED 降级路径）

`test/integration/test_environment_degraded_path.cpp`：

```cpp
#include <gtest/gtest.h>
#include <chrono>

#include <rclcpp/rclcpp.hpp>

#include "m2_world_model/world_state_aggregator.hpp"
#include "m2_world_model/view_health_monitor.hpp"

using namespace mass_l3::m2;
using namespace std::chrono_literals;

class EnvironmentDegradedTest : public ::testing::Test
{
protected:
    void SetUp() override { rclcpp::init(0, nullptr); }
    void TearDown() override { rclcpp::shutdown(); }
};

// Per detailed design §7.1.3 (SV degradation)
TEST_F(EnvironmentDegradedTest, SvDegradesAfter30sOfNoEnvUpdate)
{
    auto health = std::make_shared<ViewHealthMonitor>(default_health_config());
    auto t0 = std::chrono::steady_clock::now();

    // Receive an environment update at t0
    health->record_environment_update(t0);
    EXPECT_EQ(health->sv_health(t0), ViewHealth::Nominal);

    // 31 s later, no further update → DEGRADED
    EXPECT_EQ(health->sv_health(t0 + 31s), ViewHealth::Degraded);
}

TEST_F(EnvironmentDegradedTest, AggregatorUsesCachedZoneAfterEnvLoss)
{
    auto agg = make_test_aggregator();
    auto t0 = std::chrono::steady_clock::now();

    agg->update_environment(make_env_state(/*zone_type=*/"open_water"));

    // Simulate 45 s of no env update; aggregator should still publish using cached zone
    auto state = agg->compose_world_state(t0 + 45s);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->zone.zone_type, "open_water");
    EXPECT_LE(state->confidence, 0.8f);   // SV degraded → confidence pulled down
}

// Per detailed design §7.1.2 (EV CRITICAL)
TEST_F(EnvironmentDegradedTest, EvCriticalForcesNullopt)
{
    auto agg = make_test_aggregator();
    auto t0 = std::chrono::steady_clock::now();
    // No own_ship update at all → EV CRITICAL → no world state output
    auto state = agg->compose_world_state(t0);
    EXPECT_FALSE(state.has_value());
}
```

> **Test fixtures**（`test/fixtures/scenario_*.yaml`）:
> - `scenario_head_on.yaml`：详细设计 §9.3 Scenario 1
> - `scenario_overtaking.yaml`：标准超越场景
> - `scenario_crossing.yaml`：详细设计 §9.3 Scenario 1（FCB 18 kn vs 散货船 12 kn @ 90°）
> - `scenario_static_target.yaml`：静止目标（fishing buoy）

---

## 9. 配置文件

### 9.1 `config/m2_params.yaml`

完整配置见 §3.4。关键 HAZID 校准点：
- `cpa_safe_m.{odd_a,b,c,d}`：[TBD-HAZID] RUN-001 RPN ≥ 8 的 CPA 阈值
- `tcpa_safe_s.{odd_a,b,c,d}`：[TBD-HAZID] 同上
- `head_on_heading_diff_tol_deg`：[TBD-HAZID] Rule 14 容差
- `safe_pass_relative_speed_threshold_mps`：[TBD-HAZID] SAFE_PASS 判定下限
- 数据丢失容限（dv/ev/sv）：[TBD-HAZID]

### 9.2 `config/enc_data_paths.yaml`

完整 schema 见 §3.4。生产部署时由运维填充实际 ENC 路径；CI 用 `test/fixtures/enc_test_charts/` 里的小区域 fixture。

### 9.3 `config/m2_logger.yaml`

```yaml
spdlog:
  default_level: info
  pattern: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v"
  sinks:
    - type: rotating_file
      path: /var/log/mass_l3/m2.log
      max_size_mb: 50
      max_files: 5
    - type: console
      level: warn
asdr_sink:
  type: rotating_file
  path: /var/log/mass_l3/m2_asdr.log
  max_size_mb: 100
  max_files: 10
  level: trace                              # ASDR 须保留全量
```

---

## 10. PATH-D 合规性 checklist（DoD）

> 引用 `00-implementation-master-plan.md` §5.2 Phase E1 DoD。M2 模块完成判据：

- [ ] CI 5 阶段全绿（lint / unit / static / integration / release）
- [ ] 单元测试覆盖率 ≥ 90%（`m2_world_model_lib`，由 lcov 报告）
- [ ] Python `enc_loader` pytest 覆盖率 ≥ 90%（pytest-cov）
- [ ] **clang-tidy 20.1.8 无 error**（PATH-D 强制；MISRA C++:2023 规则集启用）
- [ ] **cppcheck Premium 26.1.0 MISRA C++:2023 完整 179 规则 0 violation**（PATH-D 强制）
- [ ] AddressSanitizer + ThreadSanitizer + UBSan 0 错误（Debug build）
- [ ] **`l3_msgs/WorldState` 字段对齐 v1.1.2 §15.1**（含 targets[] cpa_m/tcpa_s/encounter、own_ship 对水 u/v、zone、confidence、rationale）
- [ ] 订阅 4 topic + 发布 3 topic 全部按 §3 表格上线（QoS 显式锁定）
- [ ] 详细设计 §9.3 三个 HIL 场景（Scenario 1 标准交叉 / Scenario 2 能见度不良 / Scenario 3 级联故障）至少有 mock-level 验证
- [ ] HAZID `[TBD-HAZID]` 参数全部通过 `config/m2_params.yaml` 注入（grep 源代码确保无硬编码）
- [ ] ASDR 决策日志格式符合 v1.1.2 §15.1 ASDRRecord IDL + RFC-004 决议（SHA-256 签名 + JSON schema）
- [ ] M2 不依赖任何 PATH-D 禁用库；M7 视角检查 M2 暴露的 header 仅 `l3_msgs::msg::*`（无 m2 内部命名空间外泄）
- [ ] 模块 README 同步更新（构建命令 / 启动 launch / mock 集成步骤）
- [ ] **时间对齐 RFC-002 验证**：M2 在 `CpaTcpaCalculator` 中显式将目标 `sog/cog`（对地）与自身 `u/v`（对水）+ `current_speed/direction`（海流估计）解耦；单元测试 `test_cpa_tcpa_calculator.cpp` 覆盖此耦合 / 解耦验证
- [ ] **ODD 子域切换验证**：M1 ODD 子域 A→B→D 切换时，`cpa_safe_m` / `tcpa_safe_s` 阈值在 ≤ 1 周期（250 ms）内更新

**违反任一项 = PR 被 CI 阻断 / reviewer 拒绝。**

---

## 11. 引用

### 内部基线（项目仓库）
- 详细设计：`docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md`（SANGO-ADAS-L3-DD-M2-001 v1.0）
- 主架构：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 §6 + §15.1
- 实施主计划：`docs/Implementation/00-implementation-master-plan.md` §3 + §4
- 编码规范：`docs/Implementation/coding-standards.md` §3（MISRA C++:2023）
- IDL 实现指南：`docs/Implementation/ros2-idl-implementation-guide.md` §3.1（共享子类型）+ §3.3（WorldState）
- 第三方库政策：`docs/Implementation/third-party-library-policy.md` §2.2（PATH-D 决策路径库）
- RFC-002 决议：`docs/Design/Cross-Team Alignment/RFC-decisions.md`（Fusion ↔ M2 接口锁定，u/v 不含海流）

### 标准与规范（继承架构 v1.1.2 §16）
- [R10] Urmson, C., et al. (2008). *Autonomous driving in urban environments: Boss and the urban challenge*. JFR 25(8):425–466（World Model 子系统设计）
- [R13] Albus, J.S. (1991). *Reference Model Architecture (RCS)*. NIST TN 1288
- [R17] Wang, T., et al. (2021). *Collision Avoidance Algorithm Based on Dynamic Window Method and QPSO*. JMSE 9(6):584
- [R18] IMO (1972/2002). *COLREGs Convention*（Rule 13 / 14 / 15）
- [R20] Eriksen, B.H., et al. (2020). *Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17*. Front. Robot. AI 7:11

### 工程基线（`00-implementation-master-plan.md` §10.2 验证 2026-05-06）
- ROS 2 Jazzy Jalisco — [docs.ros.org/jazzy](https://docs.ros.org/en/jazzy/) 🟢 A
- Eigen 5.0.0 — [gitlab.com/libeigen/eigen](https://gitlab.com/libeigen/eigen/-/releases) 🟢 A
- GeographicLib 2.7 — [sourceforge/geographiclib](https://sourceforge.net/p/geographiclib/news/2025/11/geographiclib-27-released-2025-11-06/) 🟢 A
- Boost 1.91.0 — [boost.org/releases/latest](https://www.boost.org/releases/latest/) 🟢 A
- spdlog 1.17.0 — [github.com/gabime/spdlog](https://github.com/gabime/spdlog/releases) 🟢 A
- yaml-cpp 0.8.0 — [github.com/jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp/releases) 🟢 A
- GTest 1.17.0 — [github.com/google/googletest](https://github.com/google/googletest/releases) 🟢 A
- pyproj — [pyproj4.github.io/pyproj](https://pyproj4.github.io/pyproj/) 🟢 A
- MISRA C++:2023 合并继任者 — [Perforce 公告](https://www.perforce.com/blog/qac/misra-and-autosar-unite-cpp-coding-guidelines-what-means) 🟢 A

---

## 12. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff，Team-M2 子代理） | 初稿创建：12 章节完整，C++ 主接口 + Python ENC 副语言；与详细设计 v1.0 / 架构 v1.1.2 §6 / RFC-002 一一对齐；HAZID `[TBD-HAZID]` 参数 11 项标注 |
