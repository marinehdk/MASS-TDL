# ROS2 IDL 实现指南（v1.1.2 §15 → .msg 映射）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-IDL-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（与 v1.1.2 §15 IDL 一一对齐） |
| ROS2 发行版 | Jazzy Jalisco（首选）/ Humble Hawksbill（备选） |
| RMW vendor | Cyclone DDS（默认）/ Fast DDS（备选）/ RTI Connext Cert（v1.2 升级路径） |
| 适用范围 | `l3_msgs`（L3 内部消息）+ `l3_external_msgs`（跨层 mock 消息）+ 8 模块订阅 / 发布拓扑 |
| 关联文件 | `00-implementation-master-plan.md` §4.1 / `coding-standards.md` §4.2 / 架构 v1.1.2 §15 |

> **本文是 8 模块代码骨架的接口契约。** 任何 .msg 字段添加 / 重命名 / 类型变更须走 PR review + 自动 schema 校验，且**必须同步更新本文 + 架构 v1.1.x §15.1**。

---

## 1. 范围与原则

### 1.1 两个消息包

```
src/l3_msgs/                          # L3 内部消息（架构 v1.1.2 §15.1 全部）
   ├── package.xml
   ├── CMakeLists.txt
   └── msg/
       ├── ODDState.msg               # M1 发布
       ├── ModeCmd.msg                # M1 → M4
       ├── WorldState.msg             # M2 发布
       ├── MissionGoal.msg            # M3 → M4
       ├── RouteReplanRequest.msg     # M3 → L2（反向）
       ├── BehaviorPlan.msg           # M4 → M5
       ├── COLREGsConstraint.msg      # M6 → M5
       ├── AvoidancePlan.msg          # M5 → L4
       ├── ReactiveOverrideCmd.msg    # M5 → L4（紧急）
       ├── SafetyAlert.msg            # M7 → M1
       ├── ASDRRecord.msg             # 全模块 → ASDR
       ├── ToRRequest.msg             # M8 → ROC
       ├── UIState.msg                # M8 → ROC（HMI 渲染数据）
       ├── SATData.msg                # M1/M2/M4/M6/M7 → M8
       └── 共享子类型/  ─ TrackedTarget.msg / OwnShipState.msg /
           AvoidanceWaypoint.msg / SpeedSegment.msg / Constraint.msg /
           RuleActive.msg / EncounterClassification.msg / ZoneConstraint.msg /
           SAT1Data.msg / SAT2Data.msg / SAT3Data.msg / TimeWindow.msg

src/l3_external_msgs/                 # 跨层 mock 消息（其他团队真实包就位前用）
   ├── package.xml
   ├── CMakeLists.txt
   └── msg/
       ├── VoyageTask.msg             # L1 → M3
       ├── PlannedRoute.msg           # L2 → M3, M5
       ├── SpeedProfile.msg           # L2 → M3, M5
       ├── ReplanResponse.msg         # L2 → M3 [v1.1.2 RFC-006]
       ├── TrackedTargetArray.msg     # Multimodal Fusion → M2
       ├── FilteredOwnShipState.msg   # Multimodal Fusion → M2
       ├── EnvironmentState.msg       # Multimodal Fusion → M2
       ├── CheckerVetoNotification.msg # X-axis Checker → M7
       ├── EmergencyCommand.msg       # Y-axis Reflex Arc → L5
       ├── ReflexActivationNotification.msg  # Y-axis Reflex Arc → M1
       └── OverrideActiveSignal.msg   # Hardware Override Arbiter → M1
```

### 1.2 IDL → ROS2 .msg 类型映射规则

| IDL 类型 | ROS2 .msg 类型 |
|---|---|
| `timestamp` | `builtin_interfaces/Time stamp` |
| `string` | `string` |
| `float32` | `float32` |
| `double` / `float64` | `float64` |
| `int32_t` | `int32` |
| `int64_t` | `int64` |
| `bool` | `bool` |
| `enum X` | `int8` 或 `uint8`，配合常量定义；详见 §3.2 |
| `bytes` | `uint8[]` |
| `Position` | `geometry_msgs/Point` 或 `geographic_msgs/GeoPoint` |
| `Polygon` | `geometry_msgs/Polygon` 或 `geographic_msgs/GeoPath`（含 GeoPoint 列表）|
| `<custom>[]` | `<custom>[]`（动态数组）；上限须在 .msg 注释 |

### 1.3 必备字段（v1.1.2 §4.4 数据总线设计原则）

每条 ROS2 .msg 文件**必须**包含：

```
# Required header per v1.1.2 §4.4 data bus principles
builtin_interfaces/Time stamp           # 采样时间（发布者填）
float32 confidence                       # [0, 1] 置信度（M7 SOTIF 假设验证用）
string rationale                         # SAT-2 决策依据摘要
```

例外：纯命令型消息（`ToRRequest` / `ReactiveOverrideCmd` / `EmergencyCommand` 等），`rationale` 字段不强制；其他必备字段保留。

---

## 2. 命名规范

### 2.1 .msg 文件名

| 来源 IDL（v1.1.2 §15.1） | ROS2 .msg 文件 | 类名（C++） | 备注 |
|---|---|---|---|
| `ODD_StateMsg` | `ODDState.msg` | `ODDState` | 消息类型名去掉 `Msg` 后缀；保留 `ODD` 大写（首字母缩略词） |
| `World_StateMsg` | `WorldState.msg` | `WorldState` | |
| `Mission_GoalMsg` | `MissionGoal.msg` | `MissionGoal` | |
| `RouteReplanRequest` | `RouteReplanRequest.msg` | `RouteReplanRequest` | 已无 `Msg` 后缀，文件名保留 |
| `Behavior_PlanMsg` | `BehaviorPlan.msg` | `BehaviorPlan` | |
| `COLREGs_ConstraintMsg` | `COLREGsConstraint.msg` | `COLREGsConstraint` | `COLREGs` 大写保留 |
| `AvoidancePlanMsg` | `AvoidancePlan.msg` | `AvoidancePlan` | |
| `ReactiveOverrideCmd` | `ReactiveOverrideCmd.msg` | `ReactiveOverrideCmd` | |
| `Safety_AlertMsg` | `SafetyAlert.msg` | `SafetyAlert` | |
| `CheckerVetoNotification` | `CheckerVetoNotification.msg` | `CheckerVetoNotification` | （在 `l3_external_msgs/`）|
| `ASDR_RecordMsg` | `ASDRRecord.msg` | `ASDRRecord` | |
| `EmergencyCommand` | `EmergencyCommand.msg` | `EmergencyCommand` | （在 `l3_external_msgs/`）|
| `ReflexActivationNotification` | `ReflexActivationNotification.msg` | `ReflexActivationNotification` | （在 `l3_external_msgs/`）|
| `OverrideActiveSignal` | `OverrideActiveSignal.msg` | `OverrideActiveSignal` | （在 `l3_external_msgs/`）|
| `ToR_RequestMsg` | `ToRRequest.msg` | `ToRRequest` | |
| `SAT_DataMsg` | `SATData.msg` | `SATData` | |
| `VoyageTask` | `VoyageTask.msg` | `VoyageTask` | （在 `l3_external_msgs/`）|
| `ReplanResponseMsg` | `ReplanResponse.msg` | `ReplanResponse` | （在 `l3_external_msgs/`）[v1.1.2 RFC-006] |

### 2.2 字段命名（继承 `coding-standards.md` §4.2）

- `lower_snake_case`（必须）
- 单位后缀强制：`_m`（米）/ `_s`（秒）/ `_kn`（节）/ `_deg`（度）/ `_rad`（弧度）/ `_hz`（频率）
- 时间字段：`stamp`（采样时间）/ `received_stamp`（接收时间）/ `expiration_stamp`（失效时间）
- 几何字段：`heading_deg` / `cog_deg` / `latitude_deg` / `longitude_deg` / `cpa_m` / `tcpa_s`

### 2.3 ROS2 Topic 命名

```
/<layer>/<source_module>/<message_kind>          # L3 内部
/<source_layer>/<message_kind>                   # 跨层
```

完整清单：

| Topic | 类型 | 频率 | 发布者 | 订阅者 |
|---|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | 1 Hz + 事件 | M1 | M2, M3, M4, M5, M6, M7, M8 |
| `/l3/m1/mode_cmd` | `l3_msgs/ModeCmd` | 事件 | M1 | M4 |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | 4 Hz | M2 | M3, M4, M5, M6 |
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | 0.5 Hz | M3 | M4 |
| `/l3/m3/route_replan_request` | `l3_msgs/RouteReplanRequest` | 事件 | M3 | L2（external） |
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | 2 Hz | M4 | M5 |
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | 2 Hz | M6 | M5 |
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | 1–2 Hz | M5 | L4（external） |
| `/l3/m5/reactive_override_cmd` | `l3_msgs/ReactiveOverrideCmd` | 事件 / ≤ 10 Hz | M5 | L4（external） |
| `/l3/m7/safety_alert` | `l3_msgs/SafetyAlert` | 事件 | M7 | M1 |
| `/l3/asdr/record` | `l3_msgs/ASDRRecord` | 事件 + 2 Hz | 全 L3 | ASDR（external） |
| `/l3/m8/tor_request` | `l3_msgs/ToRRequest` | 事件 | M8 | ROC（external） |
| `/l3/m8/ui_state` | `l3_msgs/UIState` | 50 Hz | M8 | ROC HMI 前端 |
| `/l3/sat/data` | `l3_msgs/SATData` | 10 Hz | M1/M2/M4/M6/M7 | M8 |
| `/l1/voyage_task` | `l3_external_msgs/VoyageTask` | 事件 | L1（external） | M3 |
| `/l2/planned_route` | `l3_external_msgs/PlannedRoute` | 1 Hz / 事件 | L2（external） | M3, M5 |
| `/l2/speed_profile` | `l3_external_msgs/SpeedProfile` | 1 Hz / 事件 | L2（external） | M3, M5 |
| `/l2/replan_response` | `l3_external_msgs/ReplanResponse` | 事件 | L2（external） | M3 |
| `/fusion/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | 2 Hz | Fusion（external） | M2 |
| `/fusion/own_ship_state` | `l3_external_msgs/FilteredOwnShipState` | 50 Hz | Fusion（external） | M2 |
| `/fusion/environment_state` | `l3_external_msgs/EnvironmentState` | 0.2 Hz | Fusion（external） | M2 |
| `/checker/veto_notification` | `l3_external_msgs/CheckerVetoNotification` | 事件 | X-axis Checker | M7 |
| `/reflex/emergency_command` | `l3_external_msgs/EmergencyCommand` | 事件 | Reflex Arc | L5（external） |
| `/reflex/activation_notification` | `l3_external_msgs/ReflexActivationNotification` | 事件 | Reflex Arc | M1 |
| `/override/active_signal` | `l3_external_msgs/OverrideActiveSignal` | 事件 | Hardware Override | M1 |

### 2.4 Node 命名

```
m1_odd_envelope_manager
m2_world_model
m3_mission_manager
m4_behavior_arbiter
m5_tactical_planner_mid_mpc          # M5 含两个 node
m5_tactical_planner_bc_mpc
m6_colregs_reasoner
m7_safety_supervisor                   # 独立 node + 独立进程
m8_hmi_transparency_bridge
```

> **M5 双节点设计理由**：Mid-MPC（≥ 90 s 时域，1–2 Hz）+ BC-MPC（短时域，事件触发）实时性要求差异巨大，分开 node 便于 DDS QoS 独立配置。

> **M7 独立进程理由**：Doer-Checker 独立性约束（决策四 + ADR-001）— M7 不仅 node 独立，且在 launch 时配置为独立 OS 进程（不与其他 L3 node composable）。

---

## 3. 完整 .msg 定义

### 3.1 共享子类型

#### `TrackedTarget.msg`

```
# Tracked target with classification + COLREG-relevant geometry
# Per v1.1.2 §6.2 (M2 World Model) + §15.1 World_StateMsg
builtin_interfaces/Time stamp           # 跟踪起点时间
uint64 target_id                         # 全局唯一 ID
geographic_msgs/GeoPoint position        # WGS84 (lat/lon/alt)
float64 sog_kn                           # Speed Over Ground
float64 cog_deg                          # Course Over Ground (0–360)
float64 heading_deg                      # 真航向（含偏航；雷达不一定可得）
float64[9] covariance                    # 3x3 row-major (lat/lon/heading)
string classification                    # "vessel" | "fixed_object" | "unknown"
float32 classification_confidence        # [0, 1]
float64 cpa_m                            # M2 计算的 CPA
float64 tcpa_s                           # M2 计算的 TCPA
EncounterClassification encounter        # COLREG 几何预分类（M2 §6.2 子模块）
float32 confidence                       # 整体置信度
string source_sensor                     # "radar" | "ais" | "lidar" | "camera" | "fused"
```

#### `OwnShipState.msg`

```
# Own ship state from latest FilteredOwnShipState snapshot per v1.1.2 §15.1
builtin_interfaces/Time stamp
geographic_msgs/GeoPoint position
float64 sog_kn                           # 对地速度（用于 CPA 计算）
float64 cog_deg                          # 对地航向
float64 heading_deg                      # 真航向
float64 u_water                          # 对水纵速 (m/s)
float64 v_water                          # 对水横速 (m/s)
float64 r_dot_deg_s                      # 转向率 (deg/s)
float64 current_speed_kn                 # 海流速度估计
float64 current_direction_deg            # 海流方向估计
float64[36] covariance                   # 6x6 row-major (pos x3, vel x3)
string nav_mode                          # "OPTIMAL" | "DR_SHORT" | "DR_LONG" | "DEGRADED"（RFC-002）
```

#### `EncounterClassification.msg`

```
# COLREG geometric pre-classification per v1.1.2 §6.2
# 由 M2 World Model 在 TrackedTarget 内置；M6 在此基础上做规则推理
uint8 encounter_type
uint8 ENCOUNTER_TYPE_NONE = 0
uint8 ENCOUNTER_TYPE_HEAD_ON = 1         # Rule 14
uint8 ENCOUNTER_TYPE_OVERTAKING = 2      # Rule 13
uint8 ENCOUNTER_TYPE_CROSSING = 3        # Rule 15
uint8 ENCOUNTER_TYPE_OVERTAKEN = 4       # 被超越（直航船）
uint8 ENCOUNTER_TYPE_CROSSED_BY = 5      # 被横越（直航船）
uint8 ENCOUNTER_TYPE_AMBIGUOUS = 6       # 边界情况

float64 relative_bearing_deg             # 相对方位 (-180–180)
float64 aspect_angle_deg                 # 目标对本船的舷角
bool is_giveway                          # 本船是否为让路船
```

#### `ZoneConstraint.msg`

```
# ENC-derived zone constraints per v1.1.2 §6.4
string zone_type                         # "open_water" | "narrow_channel" | "tss" | "harbor" | "anchorage"
bool in_tss                              # 是否处于分道通航制（Rule 10）
bool in_narrow_channel                   # 是否处于狭水道（Rule 9）
geographic_msgs/GeoPath[] tss_lanes      # TSS 通航带几何（如适用）
float32 min_water_depth_m                # 当前位置水深下限
float64[] exclusion_zones                # GeoJSON Polygon 序列化（lat/lon 对）
```

#### `AvoidanceWaypoint.msg`

```
# Avoidance path waypoint per v1.1.2 §10 + RFC-001 方案 B
geographic_msgs/GeoPoint position
float64 wp_distance_m                    # 距前一 WP 的距离
float64 safety_corridor_m                # 此段允许的横向偏差
float64 target_speed_kn                  # 此 WP 处的目标速度
float64 turn_radius_m                    # 转弯半径（FCB 高速时关键）
```

#### `SpeedSegment.msg`

```
# Speed adjustment segment per v1.1.2 §10
float64 start_distance_m                 # 沿路径累计距离起点
float64 end_distance_m
float64 target_speed_kn
string phase                             # "accel" | "cruise" | "decel" | "hold"
```

#### `Constraint.msg`

```
# Generic constraint slot used in COLREGsConstraint and BehaviorPlan
string constraint_type                   # "colregs" | "kinematic" | "envelope" | "tss"
string description                       # SAT-2 摘要（人读）
float64 numeric_value                    # 数值约束值
string unit                              # "deg" | "kn" | "m" | ...
```

#### `RuleActive.msg`

```
# Active COLREGs rule per v1.1.2 §9
uint8 rule_id                            # 8, 13, 14, 15, 16, 17 ...
string rule_phase                        # "T_standOn" | "T_act" | "T_postAvoid"
uint64 target_id                         # 关联目标
float32 rule_confidence
string rationale
```

#### `SAT1Data.msg` / `SAT2Data.msg` / `SAT3Data.msg`

```
# SAT1Data — Status/State (Chen ARL-TR-7180 [R11])
string state_summary                     # "TRANSIT @ D3, ODD-A, 18 kn"
string[] active_alerts                   # ["DEGRADED: GNSS" ...]
```

```
# SAT2Data — Reasoning (按需触发)
string trigger_reason                    # "colregs_conflict" | "system_low_confidence" | "operator_request"
string reasoning_chain                   # 推理摘要
float32 system_confidence                # 系统对当前决策的置信度
```

```
# SAT3Data — Forecast (TDL 压缩时优先推送)
float64 forecast_horizon_s
string predicted_state
float32 prediction_uncertainty           # [0, 1]
float64 tdl_s
float64 tmr_s
```

#### `TimeWindow.msg`

```
# Time window for ETA constraints per v1.1.2 §15.1 VoyageTask
builtin_interfaces/Time earliest
builtin_interfaces/Time latest
```

### 3.2 Enum 在 ROS2 .msg 中的实现

ROS2 `.msg` 不原生支持 enum；用 **常量字段 + 整型字段**模式：

```
# CheckerVetoNotification.msg

builtin_interfaces/Time stamp
string checker_layer                     # "L2" | "L3" | "L4" | "L5"
string vetoed_module                     # 被否决的 Doer 模块名
uint8 veto_reason_class                  # 见下 enum
uint8 VETO_REASON_COLREGS_VIOLATION = 0
uint8 VETO_REASON_CPA_BELOW_THRESHOLD = 1
uint8 VETO_REASON_ENC_CONSTRAINT = 2
uint8 VETO_REASON_ACTUATOR_LIMIT = 3
uint8 VETO_REASON_TIMEOUT = 4
uint8 VETO_REASON_OTHER = 5
string veto_reason_detail                # 仅 ASDR 记录用，M7 不解析
bool fallback_provided                   # X-axis 是否提供 nearest_compliant
float32 confidence
string rationale
```

C++ 使用：

```cpp
#include "l3_external_msgs/msg/checker_veto_notification.hpp"
namespace mass_l3::common {
using VetoReasonClass = uint8_t;
constexpr VetoReasonClass kColregsViolation =
    l3_external_msgs::msg::CheckerVetoNotification::VETO_REASON_COLREGS_VIOLATION;
// ...
}
```

### 3.3 主要消息（v1.1.2 §15.1 全部）

#### `ODDState.msg`

```
# Per v1.1.2 §15.1 ODD_StateMsg + §3.5 state machine + §3.6 Conformance_Score
# Frequency: 1 Hz + event-driven补发 (EDGE→OUT 不等周期)
builtin_interfaces/Time stamp

uint8 current_zone                       # ODD-A/B/C/D
uint8 ODD_ZONE_A = 0                     # 开阔水域
uint8 ODD_ZONE_B = 1                     # 狭水道/VTS
uint8 ODD_ZONE_C = 2                     # 港内/靠泊
uint8 ODD_ZONE_D = 3                     # 能见度不良

uint8 auto_level                         # D2/D3/D4
uint8 AUTO_LEVEL_D2 = 2
uint8 AUTO_LEVEL_D3 = 3
uint8 AUTO_LEVEL_D4 = 4

uint8 health
uint8 HEALTH_FULL = 0
uint8 HEALTH_DEGRADED = 1
uint8 HEALTH_CRITICAL = 2

uint8 envelope_state                     # IN | EDGE | OUT | MRC_PREP | MRC_ACTIVE
uint8 ENVELOPE_IN = 0
uint8 ENVELOPE_EDGE = 1
uint8 ENVELOPE_OUT = 2
uint8 ENVELOPE_MRC_PREP = 3
uint8 ENVELOPE_MRC_ACTIVE = 4

float32 conformance_score                # [0, 1]
float32 tmr_s                            # Maximum Operator Response Time
float32 tdl_s                            # Automation Response Deadline
string zone_reason                       # SAT-2: ODD 子域判断理由

uint8[] allowed_zones                    # 当前健康状态下可允许的 ODD 子域

float32 confidence
string rationale
```

#### `WorldState.msg`

```
# Per v1.1.2 §15.1 World_StateMsg + §6
# Frequency: 4 Hz (M2 内部 2 Hz 输入聚合 + 1 次插值/外推)
builtin_interfaces/Time stamp

l3_msgs/TrackedTarget[] targets         # 含 cpa_m / tcpa_s（M2 计算）
l3_msgs/OwnShipState own_ship           # 取自最近 FilteredOwnShipState 快照
l3_msgs/ZoneConstraint zone             # ENC 约束

float32 confidence                       # 整体世界视图置信度
string rationale                         # SAT-2: 聚合质量摘要
```

#### `MissionGoal.msg`

```
# Per v1.1.2 §15.1 Mission_GoalMsg
builtin_interfaces/Time stamp
geographic_msgs/GeoPoint current_target_wp
float32 eta_to_target_s
float32 speed_recommend_kn
float32 confidence
string rationale
```

#### `RouteReplanRequest.msg`

```
# Per v1.1.2 §15.1 + RFC-006 决议
builtin_interfaces/Time stamp

uint8 reason                             # 见下 enum
uint8 REASON_ODD_EXIT = 0
uint8 REASON_MISSION_INFEASIBLE = 1
uint8 REASON_MRC_REQUIRED = 2
uint8 REASON_CONGESTION = 3

float32 deadline_s
string context_summary                   # SAT-2 摘要供 L2 / ROC 阅
geographic_msgs/GeoPoint current_position
geographic_msgs/GeoPath[] exclusion_zones  # GeoJSON Polygon 格式（RFC-006 锁定）

float32 confidence
string rationale
```

#### `ReplanResponse.msg` （v1.1.2 RFC-006 新增）

```
# L2 Voyage Planner → M3 (event-driven)
# Per v1.1.2 §15.1 + RFC-006 决议（接口锁定）
builtin_interfaces/Time stamp

uint8 status
uint8 STATUS_SUCCESS = 0
uint8 STATUS_FAILED_TIMEOUT = 1
uint8 STATUS_FAILED_INFEASIBLE = 2
uint8 STATUS_FAILED_NO_RESOURCES = 3

string failure_reason                    # 失败时填充
bool retry_recommended                   # 是否建议 M3 重试
```

#### `BehaviorPlan.msg`

```
# Per v1.1.2 §15.1 Behavior_PlanMsg
builtin_interfaces/Time stamp

uint8 behavior
uint8 BEHAVIOR_TRANSIT = 0
uint8 BEHAVIOR_COLREG_AVOID = 1
uint8 BEHAVIOR_DP_HOLD = 2
uint8 BEHAVIOR_BERTH = 3
uint8 BEHAVIOR_MRC_DRIFT = 4
uint8 BEHAVIOR_MRC_ANCHOR = 5
uint8 BEHAVIOR_MRC_HEAVE_TO = 6

float32 heading_min_deg
float32 heading_max_deg
float32 speed_min_kn
float32 speed_max_kn

float32 confidence
string rationale                         # IvP 求解摘要（SAT-2）
```

#### `COLREGsConstraint.msg`

```
# Per v1.1.2 §15.1 COLREGs_ConstraintMsg
builtin_interfaces/Time stamp

l3_msgs/RuleActive[] active_rules        # Rule 8/13/14/15/16/17 ...
string phase                             # "T_standOn" | "T_act" | "T_postAvoid"
l3_msgs/Constraint[] constraints

float32 confidence
string rationale
```

#### `AvoidancePlan.msg`

```
# Per v1.1.2 §15.1 AvoidancePlanMsg + RFC-001 方案 B
# Frequency: 1–2 Hz; L4 在避让模式下用此覆盖 L2 PlannedRoute
builtin_interfaces/Time stamp

l3_msgs/AvoidanceWaypoint[] waypoints   # 避让路径航点序列
l3_msgs/SpeedSegment[] speed_adjustments # 速度调整曲线
float32 horizon_s                        # 有效时域（典型 60–120 s）

string status                            # "NORMAL" | "OVERRIDDEN"
string[] active_constraints              # 激活的约束列表（SAT-2）

float32 confidence
string rationale                         # IvP / MPC 求解摘要
```

#### `ReactiveOverrideCmd.msg`

```
# Per v1.1.2 §15.1 + RFC-001 紧急避让接口
# Frequency: 事件 / 上限 10 Hz
builtin_interfaces/Time trigger_time

string trigger_reason                    # "CPA_EMERGENCY" | "COLLISION_IMMINENT" | ...
float32 heading_cmd_deg
float32 speed_cmd_kn
float32 rot_cmd_deg_s
float32 validity_s                       # 命令有效期（典型 1–3 s）

float32 confidence
```

#### `SafetyAlert.msg`

```
# Per v1.1.2 §15.1 + ADR-001 (M7 仅触发预定义 MRM 命令集，不注入轨迹)
builtin_interfaces/Time stamp

uint8 alert_type
uint8 ALERT_IEC61508_FAULT = 0
uint8 ALERT_SOTIF_ASSUMPTION = 1
uint8 ALERT_PERFORMANCE_DEGRADED = 2

uint8 severity
uint8 SEVERITY_INFO = 0
uint8 SEVERITY_WARNING = 1
uint8 SEVERITY_CRITICAL = 2
uint8 SEVERITY_MRC_REQUIRED = 3

string description

string recommended_mrm                   # "MRM-01" | "MRM-02" | "MRM-03" | "MRM-04"
                                          # （详见 v1.1.2 §11.6 MRM 命令集）

float32 confidence
string rationale
```

#### `ASDRRecord.msg`

```
# Per v1.1.2 §15.1 + RFC-004 决议
# Frequency: 事件 + 2 Hz
builtin_interfaces/Time stamp
string source_module                     # "M1_ODD_Manager" | "M2_World_Model" | ... | "M7_Safety_Supervisor"

string decision_type                     # "encounter_classification" | "avoid_wp" |
                                          # "cpa_alert" | "checker_veto_received" |
                                          # "sotif_alert" | "mrm_triggered" | ...

string decision_json                     # JSON 序列化（兼容 ASDR §5.2 AiDecisionRecord schema）
uint8[] signature                        # SHA-256 签名（防篡改）
```

#### `ToRRequest.msg`

```
# Per v1.1.2 §15.1 + §12.4 ToR 协议（C 交互验证：操作员主动点击）
builtin_interfaces/Time stamp

uint8 reason
uint8 REASON_ODD_EXIT = 0
uint8 REASON_MANUAL_REQUEST = 1
uint8 REASON_SAFETY_ALERT = 2

float32 deadline_s                       # 接管时间窗口（典型 60 s）[R4]

uint8 target_level                       # AutoLevel D2/D3/D4
string context_summary                   # SAT-1 摘要（操作员须阅读）
string recommended_action
```

#### `SATData.msg`

```
# Per v1.1.2 §15.1 + §12.2 自适应触发策略
builtin_interfaces/Time stamp

string source_module                     # 数据来源模块名
l3_msgs/SAT1Data sat1                    # 现状（持续刷新）
l3_msgs/SAT2Data sat2                    # 推理（按需触发）
l3_msgs/SAT3Data sat3                    # 预测（TDL 压缩时优先推送）
```

> 完整 17 个核心 .msg + 14 个共享子类型的字段定义见上。其余跨层 mock 消息（`VoyageTask` / `PlannedRoute` / `EmergencyCommand` / `OverrideActiveSignal` / `EnvironmentState` / 等）在 `l3_external_msgs/` 中定义；字段与 v1.1.2 §15.1 一致，由本文 §1.1 文件清单驱动。

---

## 4. DDS QoS 配置

### 4.1 时间尺度 → QoS 映射

| 时间尺度 | 频率 | 模块 | QoS profile | rclcpp 选项 |
|---|---|---|---|---|
| **长时（1 Hz）** | 1 Hz + 事件 | M1, M3 | `reliable + transient_local + keep_last(10)` | `rclcpp::QoS(10).reliable().transient_local()` |
| **中时（1–4 Hz）** | 0.5–4 Hz | M2, M4, M6 | `reliable + volatile + keep_last(5)` | `rclcpp::QoS(5).reliable()` |
| **短时（10–50 Hz）** | 10–50 Hz | M2 World_State, M5 Mid-MPC, M7 | `best_effort + volatile + keep_last(2)` | `rclcpp::SensorDataQoS().keep_last(2)` |
| **实时（50–100 Hz）** | 50–100 Hz | M8, M5 BC-MPC | `best_effort + volatile + keep_last(1)` | `rclcpp::SensorDataQoS().keep_last(1)` |
| **事件型** | 事件 | ToR, RouteReplanRequest, ReplanResponse, CheckerVeto, ReflexActivation | `reliable + transient_local + keep_last(50)` | `rclcpp::QoS(50).reliable().transient_local()` |

### 4.2 关键 topic 的 QoS 显式锁定

```cpp
// M1 ODD State publisher — long-time, reliable, transient_local for late joiners
auto odd_pub = create_publisher<l3_msgs::msg::ODDState>(
    "/l3/m1/odd_state",
    rclcpp::QoS(10).reliable().transient_local());

// M2 World State publisher — short-time, best_effort
auto world_pub = create_publisher<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state",
    rclcpp::SensorDataQoS().keep_last(2));

// M5 ReactiveOverride publisher — event-driven, reliable + sub-100ms latency
auto override_pub = create_publisher<l3_msgs::msg::ReactiveOverrideCmd>(
    "/l3/m5/reactive_override_cmd",
    rclcpp::QoS(50).reliable());

// M7 Safety Alert publisher — event-driven, reliable, transient_local
auto alert_pub = create_publisher<l3_msgs::msg::SafetyAlert>(
    "/l3/m7/safety_alert",
    rclcpp::QoS(50).reliable().transient_local());
```

### 4.3 Cyclone DDS 项目级配置（QoS profile XML）

`config/cyclonedds-mass-l3.xml`：

```xml
<CycloneDDS xmlns="https://cdds.io/config" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Domain id="any">
    <General>
      <Interfaces>
        <NetworkInterface name="lo" priority="default" />
        <NetworkInterface name="eth0" />
      </Interfaces>
      <AllowMulticast>spdp</AllowMulticast>
      <MaxMessageSize>65500B</MaxMessageSize>
    </General>

    <!-- 资源限制 -->
    <Internal>
      <SocketReceiveBufferSize min="default" max="2MiB" />
      <SocketSendBufferSize min="default" max="2MiB" />
    </Internal>

    <!-- 优先级配置（M5 ReactiveOverride / M7 SafetyAlert / Reflex 高优先级）-->
    <Tracing>
      <Verbosity>config</Verbosity>
      <OutputFile>/tmp/cyclonedds-mass-l3.log</OutputFile>
    </Tracing>
  </Domain>
</CycloneDDS>
```

环境变量启用：`CYCLONEDDS_URI=file:///path/to/config/cyclonedds-mass-l3.xml`

### 4.4 QoS 协商失败检测

每个订阅者须在 `qos_callback` 中检查 QoS 不兼容：

```cpp
auto sub_options = rclcpp::SubscriptionOptions();
sub_options.event_callbacks.incompatible_qos_callback =
    [this](rclcpp::QOSRequestedIncompatibleQoSInfo& event) {
        spdlog::error("[{}] Incompatible QoS for topic: total_count={}, last_policy_kind={}",
                      get_name(), event.total_count, event.last_policy_kind);
        // 升级为 M7 SOTIF 假设违反告警
    };
```

---

## 5. CMakeLists.txt 模板

### 5.1 `l3_msgs` package

```cmake
cmake_minimum_required(VERSION 3.22)
project(l3_msgs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)

set(MSG_FILES
  msg/ODDState.msg
  msg/ModeCmd.msg
  msg/WorldState.msg
  msg/MissionGoal.msg
  msg/RouteReplanRequest.msg
  msg/BehaviorPlan.msg
  msg/COLREGsConstraint.msg
  msg/AvoidancePlan.msg
  msg/ReactiveOverrideCmd.msg
  msg/SafetyAlert.msg
  msg/ASDRRecord.msg
  msg/ToRRequest.msg
  msg/UIState.msg
  msg/SATData.msg
  msg/TrackedTarget.msg
  msg/OwnShipState.msg
  msg/EncounterClassification.msg
  msg/ZoneConstraint.msg
  msg/AvoidanceWaypoint.msg
  msg/SpeedSegment.msg
  msg/Constraint.msg
  msg/RuleActive.msg
  msg/SAT1Data.msg
  msg/SAT2Data.msg
  msg/SAT3Data.msg
  msg/TimeWindow.msg
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${MSG_FILES}
  DEPENDENCIES builtin_interfaces geometry_msgs geographic_msgs
  ADD_LINTER_TESTS
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
```

### 5.2 `l3_external_msgs` package

类似上文，仅 .msg 列表替换为 §1.1 中 `l3_external_msgs/` 部分。

---

## 6. 消息有效性校验

### 6.1 每模块发布前自检

```cpp
// 通用模板（在每模块 publisher 包装函数中调用）
template <typename MsgT>
bool validate_msg(const MsgT& msg, std::string& error_out) {
  if (msg.stamp.sec == 0 && msg.stamp.nanosec == 0) {
    error_out = "stamp not set";
    return false;
  }
  if constexpr (requires { msg.confidence; }) {
    if (msg.confidence < 0.0f || msg.confidence > 1.0f) {
      error_out = "confidence out of [0, 1]";
      return false;
    }
  }
  return true;
}
```

### 6.2 单元测试约束

每个 .msg 字段在 `tests/unit/test_<msg>_validation.cpp` 中须有：
- 边界值测试（confidence = 0.0 / 1.0 / 0.5 / -0.1 / 1.1）
- 必备字段测试（stamp 缺失 / rationale 空字符串 / 等）
- enum 越界测试

---

## 7. 版本管理与向后兼容

### 7.1 .msg 字段添加（向后兼容）

ROS2 .msg 字段添加到 **末尾** 是向后兼容的（已发布订阅者可继续工作）。**禁止**：
- 字段重命名（破坏性）
- 字段删除（破坏性）
- 字段类型变更（破坏性）
- 字段顺序调整（部分 RMW 取决于）

### 7.2 v1.1.2 → v1.1.3 / v1.2 迁移规划

预计 HAZID 完成（2026-08-19）后，v1.1.3 引入 patch；目前仅 RFC-006 引入 ReplanResponse 新消息（v1.1.2 已合并）。**v1.2 升级路径**：

- 多船型扩展可能引入 `VesselCapabilityManifest.msg`（待方向 6）
- M1/M7 切换 RTI Connext Cert（如启用）需测试 .msg 在 RTI IDL 下的二进制兼容（应自动）

### 7.3 .msg schema 自动校验

`tools/ci/check-msg-schema.sh`：

```bash
#!/usr/bin/env bash
# 与 git history 比较：检查是否有破坏性变更
set -euo pipefail
git diff origin/main HEAD -- 'src/l3_msgs/msg/*.msg' 'src/l3_external_msgs/msg/*.msg' | \
  grep -E "^-[^-].*\b(uint8|int8|int32|int64|uint32|uint64|float32|float64|bool|string)\s+\w+" | \
  while read -r line; do
    field=$(echo "$line" | awk '{print $NF}')
    echo "BREAKING: removed field $field"
    exit 1
  done
echo ".msg schema: backward-compatible OK"
```

---

## 8. l3_external_msgs Mock 节点策略

跨团队真实节点未就位前，每个 external 消息须有 mock publisher 节点：

```
src/l3_external_msgs/launch/
   ├── mock_l1_voyage_order.launch.py
   ├── mock_l2_voyage_planner.launch.py
   ├── mock_fusion.launch.py
   ├── mock_checker.launch.py
   ├── mock_reflex_arc.launch.py
   ├── mock_override_arbiter.launch.py
   └── full_external_stack.launch.py    # 一键启动所有 mock
```

每个 mock node 实现：
- 按 §15.2 接口矩阵注明的频率发布
- 字段填充随机但合理的值（经过 §6 validation）
- 接受参数化场景（YAML 配置）触发不同测试场景

示例：

```python
# src/l3_external_msgs/scripts/mock_fusion.py
import rclpy
from rclpy.node import Node
from l3_external_msgs.msg import TrackedTargetArray, FilteredOwnShipState

class MockFusion(Node):
    def __init__(self):
        super().__init__('mock_fusion')
        self.targets_pub = self.create_publisher(TrackedTargetArray, '/fusion/tracked_targets', 10)
        self.own_ship_pub = self.create_publisher(FilteredOwnShipState, '/fusion/own_ship_state', 50)
        self.create_timer(0.5, self.publish_targets)        # 2 Hz
        self.create_timer(0.02, self.publish_own_ship)      # 50 Hz

    def publish_targets(self):
        msg = TrackedTargetArray()
        # ... fill scenario from YAML ...
        self.targets_pub.publish(msg)

    # ...

if __name__ == '__main__':
    rclpy.init()
    rclpy.spin(MockFusion())
```

---

## 9. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：v1.1.2 §15.1 IDL → ROS2 .msg 一一映射 + DDS QoS 时间尺度分级 + Cyclone DDS 配置 + Mock 节点策略 |

---

## 10. 引用

- **架构 v1.1.2 §15.1 IDL 定义** — `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`
- **架构 v1.1.2 §15.2 接口矩阵** — 同上
- **RFC-001/002/003/004/005/006 决议** — `docs/Design/Cross-Team Alignment/RFC-decisions.md`
- **ROS 2 Jazzy 文档** — [docs.ros.org/en/jazzy](https://docs.ros.org/en/jazzy/Concepts/About-Different-Middleware-Vendors.html) 🟢 A
- **rclcpp QoS** — [docs.ros.org/en/jazzy/About-Quality-of-Service-Settings.html](https://docs.ros.org/en/jazzy/Concepts/About-Quality-of-Service-Settings.html) 🟢 A
- **Cyclone DDS 配置参考** — [cyclonedds.io](https://cyclonedds.io/) 🟢 A
- **rosidl 接口生成** — [design.ros2.org/articles/interface_definition.html](https://design.ros2.org/articles/interface_definition.html) 🟢 A
