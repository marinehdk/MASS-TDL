# M3 Mission Manager — GAP 修复实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 解决 M3 Mission Manager 代码实现与设计规范之间的 7 项已知 GAP，按优先级分组实施。**注意不要执行代码修改——本计划为规划文档。**

**架构方案：** P0 两组（GAP-1 ENC 校验 + GAP-2 speed_recommend）无相互依赖，可并行。GAP-1 完成后自动解决 GAP-3。P1 修复（GAP-4/GAP-5）无依赖，可在 P0 后并行。P2 涉及跨模块协调，排期灵活。

**代码路径：** `src/l3_tdl_kernel/m3_mission_manager/`
**消息路径：** `src/l3_tdl_kernel/l3_msgs/msg/` + `src/l3_tdl_kernel/l3_external_msgs/msg/`
**文档路径：** `docs/Design/TDL-Kernel/M3-Mission-Manager/`

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/enc_route_validator.hpp` | **新建** | ENC 航路校验子模块头文件 |
| `src/l3_tdl_kernel/m3_mission_manager/src/enc_route_validator.cpp` | **新建** | ENC 航路校验实现（水深·禁区·COG） |
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/voyage_task_validator.hpp` | 修改 | 扩展 ValidationResult + 新增 check_enc_*() 方法 |
| `src/l3_tdl_kernel/m3_mission_manager/src/voyage_task_validator.cpp` | 修改 | 集成 EncRouteValidator 校验链 |
| `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp` | 修改 | publish_mission_goal() 填充 speed_recommend；has_enc_check 替换为动态值；Confidence 动态计算；timeout 参数读取 |
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/eta_projector.hpp` | 修改 | 新增 compute_speed_recommendation() 方法 |
| `src/l3_tdl_kernel/m3_mission_manager/src/eta_projector.cpp` | 修改 | 实现速度建议计算逻辑 |
| `src/l3_tdl_kernel/m3_mission_manager/config/m3_params.yaml` | 修改 | 新增 enc 校验参数；timeout 参数激活 |
| `src/l3_tdl_kernel/m3_mission_manager/test/test_voyage_task_validator.cpp` | 修改 | 新增 ENC 校验 5+ 测试用例 |
| `src/l3_tdl_kernel/m3_mission_manager/test/test_mission_state_machine.cpp` | 修改 | 新增 has_enc_check 动态门控测试 |
| `src/l3_tdl_kernel/m3_mission_manager/test/test_eta_projector.cpp` | 修改 | 新增 speed_recommend 7+ 测试用例 |
| `src/l3_tdl_kernel/m3_mission_manager/test/integration/test_m3_enc_integration.cpp` | **新建** | ENC 端到端集成测试 |
| `docs/Design/TDL-Kernel/M3-Mission-Manager/M3-spec.md` | 修改 | GAP 状态更新（✅ 关闭后标记） |
| `docs/Design/TDL-Kernel/M3-Mission-Manager/M3-progress.md` | 修改 | 新增 D2.8/D2.9/D2.10 task 条目 |

---

## 任务分解

---

## P0 🔴 — Task Group A：ENC 校验补全（GAP-1 + GAP-3）

**概要**：新建 `EncRouteValidator` 子模块，实现航路水深/禁区/COG 三项 ENC 可航性校验，集成到 VoyageTaskValidator 校验链中。完成后 `has_enc_check` 条件门控自然激活。

**依赖**：ENC 数据源（M2 ZoneConstraint 或独立 ENC-provider）需提供水深/禁区/COG 查询接口
**工作量**：3–5 pw（含 ENC 接口协商）
**可并行**：与 Task Group B（GAP-2）独立

### Task A-1: 新建 EncRouteValidator 子模块

**Files:**
- Create: `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/enc_route_validator.hpp`
- Create: `src/l3_tdl_kernel/m3_mission_manager/src/enc_route_validator.cpp`

- [ ] **Step 1: 定义 EncRouteValidatorConfig 结构体**

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "geographic_msgs/msg/geo_point.hpp"
#include "m3_mission_manager/error_codes.hpp"

namespace mass_l3::m3 {

struct EncRouteValidatorConfig {
  double min_depth_m;              // 安全吃水（m），来源 Capability Manifest
  double depth_check_interval_m;   // 水深采样间距（m），默认 500
  double forbidden_zone_buffer_m;  // 禁区安全边距（m），默认 200
  double max_cog_deviation_deg;    // COG 最大偏离（°），默认 45
};

struct EncRouteCheckResult {
  bool is_valid;
  ErrorCode error_code;
  std::string failed_check;      // "depth" | "forbidden_zone" | "cog"
  size_t offending_segment_idx;  // 违规航段索引
};

class EncRouteValidator {
 public:
  explicit EncRouteValidator(const EncRouteValidatorConfig& config);
  ~EncRouteValidator() = default;

  /// Full route ENC validation.
  /// @param waypoints 航路点序列（WGS84）
  /// @param zone       M2 ZoneConstraint（含禁区·TSS·水深数据）
  /// @return 校验结果
  EncRouteCheckResult validate(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints,
      const l3_msgs::msg::ZoneConstraint& zone) const;

 private:
  EncRouteCheckResult check_depth_along_route(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints,
      const l3_msgs::msg::ZoneConstraint& zone) const;
  EncRouteCheckResult check_forbidden_zone_penetration(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints,
      const l3_msgs::msg::ZoneConstraint& zone) const;
  EncRouteCheckResult check_cog_compliance(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints,
      const l3_msgs::msg::ZoneConstraint& zone) const;

  EncRouteValidatorConfig config_;
};

}  // namespace mass_l3::m3
```

- [ ] **Step 2: 实现 check_depth_along_route()**

算法：
```
对每个航段 (wp[i] → wp[i+1]):
  航段长度 L = haversine(wp[i], wp[i+1])
  采样点数 N = max(2, ceil(L / depth_check_interval_m))
  for j in 0..N:
    采样点 p = 线性插值(wp[i], wp[i+1], j/N)
    从 ZoneConstraint.bathymetry 查询 p 处水深 d_j
    if d_j < min_depth_m:
      return INVALID + offending_segment_idx = i
return VALID
```

ZoneConstraint 水深数据格式：使用 M2 提供的 `depth_grid`（二维规则网格）或 `depth_polygon`（等深线多边形）。当前 M2 EncLoader 的 `exclusion_zones/tss_lanes` 字段留空（M2 GAP-6），ENC 水深接口需与 M2 团队协商。

- [ ] **Step 3: 实现 check_forbidden_zone_penetration()**

算法：
```
对每个航段 (wp[i] → wp[i+1]):
  对每个禁区 polygon in zone.exclusion_zones:
    if 航段线段与禁区 polygon 相交（射线法或分离轴判定）:
      且最近距离 < forbidden_zone_buffer_m:
        return INVALID
return VALID
```

- [ ] **Step 4: 实现 check_cog_compliance()**

算法：
```
对每个航段 (wp[i] → wp[i+1]):
  航段 COG = bearing(wp[i], wp[i+1])
  if zone.tss_lanes 非空:
    允许 COG 范围 = TSS 航道的方向 ± max_cog_deviation_deg
    if COG 不在允许范围内:
      return INVALID（单向航道不得逆行）
return VALID
```

- [ ] **Step 5: 编译验证**

```bash
cd /workspace && colcon build --packages-select m3_mission_manager --event-handlers console_direct+ 2>&1 | tail -20
```

Expected: "Summary: 1 package finished"

---

### Task A-2: 扩展 VoyageTaskValidator 集成 ENC 校验

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/voyage_task_validator.hpp`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/voyage_task_validator.cpp`

- [ ] **Step 1: VoyageTaskValidator 构造函数接受 ZoneConstraint**

```cpp
// voyage_task_validator.hpp 修改
ValidationResult validate(
    const l3_external_msgs::msg::VoyageTask& task,
    const geographic_msgs::msg::GeoPoint& current_position,
    int64_t current_time_ns,
    const l3_msgs::msg::ZoneConstraint& zone   // 新增参数
) const;
```

- [ ] **Step 2: 在 validate() 末尾追加 ENC 校验链**

```cpp
// voyage_task_validator.cpp 修改
// 8. Check ENC depth along route
{
  const auto result = enc_validator_->check_depth_along_route(
      task.mandatory_waypoints, zone);
  if (!result.is_valid) { return {false, result.error_code, result.failed_check}; }
}
// 9. Check forbidden zone penetration
{
  const auto result = enc_validator_->check_forbidden_zone_penetration(
      task.mandatory_waypoints, zone);
  if (!result.is_valid) { return {false, result.error_code, result.failed_check}; }
}
// 10. Check COG compliance
{
  const auto result = enc_validator_->check_cog_compliance(
      task.mandatory_waypoints, zone);
  if (!result.is_valid) { return {false, result.error_code, result.failed_check}; }
}
```

- [ ] **Step 3: 编译验证**

---

### Task A-3: 激活 has_enc_check 门控

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`

- [ ] **Step 1: 替换硬编码 enc_ok**

定位到当前代码中 `bool enc_ok = true;` 的位置（~line 890），替换为：

```cpp
// 在 publish_mission_goal() 或 update_task_validity() 调用前
const bool enc_ok = (enc_validator_ != nullptr)
    ? enc_validator_->validate(route.waypoints, last_zone_).is_valid
    : true;  // fallback when ENC not available
```

- [ ] **Step 2: 编译验证**

---

### Task A-4: ENC 校验测试用例

**Files:**
- Create: `src/l3_tdl_kernel/m3_mission_manager/test/integration/test_m3_enc_integration.cpp`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/test/test_voyage_task_validator.cpp`

- [ ] **Step 1: ENC 水深校验测试（3 个用例）**

| 用例 | 场景 | 期望 |
|------|------|------|
| A-ENC-01 | 航路全水深 ≥ 安全吃水 | VALID |
| A-ENC-02 | 航路中段水深不足 | INVALID + offending_segment_idx |
| A-ENC-03 | ENC 数据不可用时的降级行为 | VALID (降级通过) |

- [ ] **Step 2: ENC 禁区穿透测试（2 个用例）**

| 用例 | 场景 | 期望 |
|------|------|------|
| A-ENC-04 | 航路避开所有禁区 | VALID |
| A-ENC-05 | 航段穿越禁区 polygon | INVALID |

- [ ] **Step 3: COG 合规测试（1 个用例）**

| 用例 | 场景 | 期望 |
|------|------|------|
| A-ENC-06 | 航段 COG 与 TSS 单向航道方向相反 | INVALID |

- [ ] **Step 4: has_enc_check 门控集成测试**

| 用例 | 场景 | 期望 |
|------|------|------|
| INT-ENC-01 | ENC 通过 → TaskValidity=VALID | VALID |
| INT-ENC-02 | ENC 失败 → TaskValidity=INVALID | INVALID |
| INT-ENC-03 | ENC 不可用 → 降级通过 | VALID (降级模式) |

- [ ] **Step 5: 运行新测试**

```bash
cd /workspace && colcon test --packages-select m3_mission_manager --event-handlers console_direct+ 2>&1 | tail -30
```

---

---

## P0 🔴 — Task Group B：speed_recommend 字段补全（GAP-2）

**概要**：实现速度建议计算逻辑，使 `MissionGoal.speed_recommend_kn` 从永为 0 变为有意义的调速建议。

**依赖**：无
**工作量**：1 pw
**可并行**：与 Task Group A（GAP-1）独立

### Task B-1: EtaProjector 添加 compute_speed_recommendation()

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/eta_projector.hpp`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/eta_projector.cpp`

- [ ] **Step 1: 新增方法签名**

```cpp
// eta_projector.hpp
struct SpeedRecommendation {
  float speed_recommend_kn;    // 建议速度（kn）
  float confidence;            // 建议置信度 [0,1]
  std::string reason;          // SAT-2 理由
};

class EtaProjector {
 public:
  // 现有方法保持不变...

  /// Compute speed recommendation based on ETA deviation and ODD constraints.
  /// @param current_eta_s    当前 ETA 预测（秒）
  /// @param planned_eta_s    计划 ETA（秒）
  /// @param sog_kn           当前对地速度（kn）
  /// @param odd_speed_limit  当前 ODD zone 速度上限（kn），默认不限制
  /// @param watchdog_factor  L1 看门狗置信度系数 [0.4, 1.0]
  SpeedRecommendation compute_speed_recommendation(
      float current_eta_s, float planned_eta_s,
      float sog_kn, float odd_speed_limit_kn,
      float watchdog_factor) const;
};
```

- [ ] **Step 2: 实现速度建议逻辑**

```cpp
// eta_projector.cpp
SpeedRecommendation EtaProjector::compute_speed_recommendation(
    float current_eta_s, float planned_eta_s,
    float sog_kn, float odd_speed_limit_kn,
    float watchdog_factor) const
{
  SpeedRecommendation rec{};
  rec.speed_recommend_kn = 0.0f;
  rec.confidence = 1.0f;

  if (current_eta_s <= 0.0f || planned_eta_s <= 0.0f) {
    rec.reason = "ETA unavailable, no speed recommendation";
    return rec;
  }

  const float delta_s = current_eta_s - planned_eta_s;
  const float margin_s = config_.infeasible_margin_s;  // 默认 600s

  if (delta_s <= 0.0f) {
    // Ahead of schedule — maintain or relax
    rec.speed_recommend_kn = sog_kn;
    rec.reason = "on/ahead of schedule, maintain current speed";
  } else if (delta_s < margin_s) {
    // Within acceptable margin — no adjustment needed
    rec.speed_recommend_kn = sog_kn;
    rec.reason = "ETA deviation within acceptable margin";
  } else {
    // Behind schedule — recommend speed increase (capped by ODD limit)
    const float required_speed = sog_kn * (current_eta_s / planned_eta_s);
    float target = std::min(required_speed, odd_speed_limit_kn);
    // Apply watchdog confidence penalty
    target *= watchdog_factor;
    rec.speed_recommend_kn = target;
    rec.confidence = watchdog_factor;
    rec.reason = "behind schedule: ETA " + std::to_string(current_eta_s)
               + "s vs planned " + std::to_string(planned_eta_s) + "s";
  }

  return rec;
}
```

- [ ] **Step 3: 编译验证**

---

### Task B-2: publish_mission_goal() 填充 speed_recommend_kn

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`

定位到 `publish_mission_goal()` 中 `msg.speed_recommend_kn = 0.0f;` 的位置，替换为：

```cpp
// mission_manager_node.cpp ~line 740
// 原: msg.speed_recommend_kn = 0.0f;

// 新: 调用 EtaProjector 计算速度建议
const auto speed_rec = eta_projector_->compute_speed_recommendation(
    eta_to_target_s,
    planned_eta_s_,                        // 从 VoyageTask 记录
    last_ws_ ? last_ws_->own_ship.sog_kn : 0.0f,
    odd_speed_limit_kn_,                   // 从 ODD state 或 Capability Manifest
    watchdog_factor);                       // 从 L1WatchdogResult
msg.speed_recommend_kn = speed_rec.speed_recommend_kn;
// confidence 已在前面设置，可在 rationale 中追加速度建议理由
```

- [ ] **Step 2: 编译验证**

---

### Task B-3: speed_recommend 测试用例

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/test/test_eta_projector.cpp`

- [ ] **Step 1: 7 个速度建议测试用例**

| 用例 | 场景 | ETA 偏差 | 期望 |
|------|------|----------|------|
| B-REC-01 | 准时 — no change | 0 | speed=sog |
| B-REC-02 | 提前 — maintain | <0 | speed=sog |
| B-REC-03 | 延迟在 margin 内 — maintain | +400s (margin=600) | speed=sog |
| B-REC-04 | 延迟超出 margin — speed up | +900s | speed > sog (capped) |
| B-REC-05 | 延迟 + ODD 限速 cap | +900s, limit=15kn | speed ≤ 15kn |
| B-REC-06 | 延迟 + L1 WARNING (×0.6) | +900s | speed = required × 0.6 |
| B-REC-07 | ETA 不可用 — return 0 | NaN | speed=0 |

- [ ] **Step 2: 运行测试**

---

---

## P1 🟡 — Task Group C：Confidence 字段动态计算（GAP-4）

**概要**：将 RouteReplanRequest.confidence、ToRRequest.confidence、ASDRRecord.confidence 从硬编码值改为基于系统状态的动态计算。

**依赖**：无
**工作量**：0.5 pw
**可并行**：与 Task Group D（GAP-5）独立

### Task C-1: RouteReplanRequest.confidence 动态计算

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`

定位到 `RouteReplanRequest` 填充代码（~line 910），替换 `msg.confidence = 1.0f;` 为：

```cpp
// 根据触发原因计算置信度
float confidence = 1.0f;
switch (last_replan_reason_) {
  case ReplanReason::MRC_REQUIRED:
    confidence = 0.95f;  // MRC 触发高度可信
    break;
  case ReplanReason::ODD_EXIT:
    confidence = 1.0f - last_odd_state_.conformance_score;  // ODD 越界确定性
    break;
  case ReplanReason::MISSION_INFEASIBLE: {
    const float eta_ratio = planned_eta_s_ > 0
        ? last_eta_to_target_s_ / planned_eta_s_ : 1.0f;
    confidence = std::max(0.0f, 1.0f - (eta_ratio - 1.0f));
    break;
  }
  case ReplanReason::CONGESTION:
    confidence = 1.0f / static_cast<float>(replan_attempt_count_);
    break;
  default:
    confidence = 0.5f;
}
msg.confidence = confidence;
```

### Task C-2: ToRRequest.confidence 动态计算

替换 `msg.confidence = 1.0f;` 为：

```cpp
msg.confidence = (l1_watchdog_)
    ? l1_watchdog_->last_result().confidence_factor
    : 1.0f;
```

### Task C-3: ASDRRecord.confidence 填充

在 ASDR 发布代码路径中追加：

```cpp
asdr_record.confidence = last_mission_goal_.confidence;
```

### Task C-4: Confidence 测试用例（4 个）

| 用例 | 场景 | 期望 |
|------|------|------|
| C-CONF-01 | MRC_REQUIRED replan | confidence ≈ 0.95 |
| C-CONF-02 | ODD_EXIT with conformance=0.4 | confidence ≈ 0.60 |
| C-CONF-03 | MISSION_INFEASIBLE + 2× ETA | confidence ≈ 0.00 |
| C-CONF-04 | ToR with L1 WARNING | confidence = 0.60 |

---

---

## P1 🟡 — Task Group D：timeout 参数激活（GAP-5）

**概要**：将 4 个未使用的 `m3_params.yaml timeout.*` 参数接入代码，用 YAML 值替换硬编码超时。

**依赖**：无
**工作量**：0.5 pw
**可并行**：与 Task Group C（GAP-4）独立

### Task D-1: 超时参数读取

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/config/m3_params.yaml`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`

- [ ] **Step 1: 确认 YAML 参数**

当前 YAML 中的 `timeout` 区块已声明。无需修改 YAML。

- [ ] **Step 2: 在 declare_parameters() 中追加读取**

```cpp
// mission_manager_node.cpp declare_parameters() 末尾追加
declare_parameter("timeout.voyage_task_s", 600.0);
declare_parameter("timeout.planned_route_s", 3.0);
declare_parameter("timeout.speed_profile_s", 3.0);
declare_parameter("timeout.odd_state_s", 2.0);
```

- [ ] **Step 3: 替换硬编码超时值**

| 位置 | 硬编码值 | 替换为 |
|------|----------|--------|
| `eta_projector.cpp`: world_state_age_threshold | 0.5s（来自 eta params） | 不变（已参数化） |
| `mission_manager_node.cpp:on_planned_route()` | 3s 硬编码检查 | `get_parameter("timeout.planned_route_s").as_double()` |
| `mission_manager_node.cpp:on_speed_profile()` | 与 planned_route 同 | `get_parameter("timeout.speed_profile_s").as_double()` |
| `mission_manager_node.cpp:on_odd_state()` | 无超时检查 | 新增：`last_odd_stamp_ > timeout.odd_state_s` → 触发降级 |

- [ ] **Step 4: ODD state 超时降级逻辑**

```cpp
// mission_manager_node.cpp on_odd_state()
void MissionManagerNode::on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg) {
  last_odd_state_ = *msg;
  last_odd_stamp_ = now();

  // Check freshness in timer callback
}

// In publish_mission_goal() or timer callback:
const auto odd_age = (now() - last_odd_stamp_).seconds();
if (odd_age > get_parameter("timeout.odd_state_s").as_double()) {
  // ODD state stale — set confidence to degraded
  odd_state_stale_ = true;
  // Publish alert via ASDR
}
```

---

---

## P2 🟢 — Task Group E：haversine 去重（GAP-6）

**概要**：提取公共 geoutil 模块，统一 `voyage_task_validator.cpp` 与 `mission_manager_node.cpp` 中的重复 haversine 实现。

**依赖**：需与项目架构负责人协调公共模块位置
**工作量**：1 pw
**可并行**：独立于所有 P0/P1 组

### Task E-1: 创建 `l3_geoutil` 公共包

**Files:**
- Create: `src/l3_tdl_kernel/l3_geoutil/include/l3_geoutil/haversine.hpp`
- Create: `src/l3_tdl_kernel/l3_geoutil/src/haversine.cpp`
- Create: `src/l3_tdl_kernel/l3_geoutil/CMakeLists.txt`
- Create: `src/l3_tdl_kernel/l3_geoutil/package.xml`

- [ ] **Step 1: 定义公共函数**

```cpp
// haversine.hpp
#pragma once
#include <cstdint>

namespace mass_l3::geoutil {

/// Haversine great-circle distance in km.
double haversine_km(double lat1, double lon1, double lat2, double lon2) noexcept;

/// Haversine great-circle distance in nautical miles.
double haversine_nm(double lat1, double lon1, double lat2, double lon2) noexcept;

/// Initial bearing from point 1 to point 2 (degrees, 0-360).
double bearing_deg(double lat1, double lon1, double lat2, double lon2) noexcept;

/// Destination point given start, bearing, and distance.
struct GeoPoint { double lat; double lon; };
GeoPoint destination(double lat, double lon, double bearing_deg, double distance_nm) noexcept;

}  // namespace mass_l3::geoutil
```

- [ ] **Step 2: 编译 + 单元测试 `l3_geoutil`**

4 个测试用例：km/nmi 精度验证，bearing 边界，destination 往返

- [ ] **Step 3: 更新 m3_mission_manager CMakeLists.txt**

```cmake
find_package(l3_geoutil REQUIRED)
ament_target_dependencies(m3_mission_manager_lib ... l3_geoutil ...)
```

- [ ] **Step 4: 替换调用点**

`voyage_task_validator.cpp` → `#include <l3_geoutil/haversine.hpp>` → 使用 `mass_l3::geoutil::haversine_km()`
`mission_manager_node.cpp` → 同上

---

---

## P2 ⚫ — Task Group F：L4 IDL 确认（GAP-7）

**概要**：与 L4 Guidance 团队确认 `TrackingError.msg` 最终字段定义，移除 `[TBD-L4TEAM]` stub 标记。

**依赖**：L4 Guidance 团队
**工作量**：外部协调（0 pw 代码修改，~1h 沟通）
**可并行**：独立于所有任务组

### Task F-1: 与 L4 团队对齐

- [ ] **Step 1: 发送 IDL 确认邮件**

Topics to confirm:
- Field names and types in `TrackingError.msg`
- `xte_m`（横迹误差，米）→ M3 需转换为海里（× 0.000539957）
- `sea_current_u_kn` / `sea_current_v_kn`（对水速度分量，kn）
- 发布频率（预期 ~10 Hz）
- 时间戳语义（`stamp` = L4 当前位置对应时间）

- [ ] **Step 2: 收到确认后更新 IDL**

更新 `src/l3_tdl_kernel/l3_external_msgs/msg/TrackingError.msg`，移除 `[TBD-L4TEAM]` 注释，添加版本号注释。

- [ ] **Step 3: 更新 M3 单位转换逻辑**

确认 `CurrentErrorMonitor` 中的单位转换正确：
```cpp
// 当前: xte_nm 计算
// 假设 L4 输出 xte 为米 → 转换: xte_nm = xte_m * 0.000539957
```

---

---

## 测试要求汇总

### 新增单元测试（C++，GTest）

| Task Group | 测试文件 | 新增用例数 | 覆盖 GAP |
|-----------|----------|-----------|----------|
| A | `test_voyage_task_validator.cpp` | +5 (ENC-01~05) | GAP-1, GAP-3 |
| A | `test_m3_enc_integration.cpp` | +3 (INT-ENC-01~03) | GAP-1, GAP-3 |
| B | `test_eta_projector.cpp` | +7 (B-REC-01~07) | GAP-2 |
| C | `test_mission_state_machine.cpp` | +4 (C-CONF-01~04) | GAP-4 |
| D | `test_mission_state_machine.cpp` | +2 (D-TIMEOUT-01~02) | GAP-5 |
| E | `l3_geoutil/test/` | +4 (haversine/bearing/dest) | GAP-6 |
| **合计** | | **+25** | |

### 回归测试（全部已有测试不得破）

| 测试文件 | 现有用例数 |
|----------|-----------|
| `test_voyage_task_validator.cpp` | 12 |
| `test_eta_projector.cpp` | 9 |
| `test_replan_request_trigger.cpp` | 9 |
| `test_replan_response_handler.cpp` | 8 |
| `test_mission_state_machine.cpp` | 12 |
| `test_l1_watchdog_monitor.cpp` | 5 |
| `test_current_error_monitor.cpp` | 7 |
| `test_idl_schema_version.cpp` | 4 |
| `test_route_received.cpp` | 4 |
| `test_m3_node_lifecycle.cpp` | 1 |
| `test_m3_dual_subscription.cpp` | 10 |
| `test_int_002_m3_l2_replan.cpp` | 3 |
| **现有合计** | **84** |

### 执行命令

```bash
# 完整测试（已有 + 新增）
colcon test --packages-select m3_mission_manager --event-handlers console_direct+

# 仅新增 ENC 测试
colcon test --packages-select m3_mission_manager \
  --ctest-args -R "enc" --event-handlers console_direct+

# 仅回归测试
colcon test --packages-select m3_mission_manager \
  --ctest-args -E "enc|speed_rec|conf|timeout" --event-handlers console_direct+
```

---

## 执行顺序建议

```
Phase 1 (P0 并行，当前可启动):
  ┌─ Task Group A: ENC 校验 (3-5pw, 依赖 ENC 数据源)
  └─ Task Group B: speed_recommend (1pw, 无依赖)

Phase 2 (P1 批量，P0 完成后):
  ┌─ Task Group C: Confidence 动态计算 (0.5pw, 无依赖)
  └─ Task Group D: timeout 参数激活 (0.5pw, 无依赖)

Phase 3 (P2，排期灵活):
  ├─ Task Group E: haversine 去重 (1pw, 需跨模块协调)
  └─ Task Group F: L4 IDL 确认 (外部协调)

里程碑:
  M1 (P0 complete): M3 具备航路合法性验证 + 速度建议能力 → M4/M5 可获取完整 MissionGoal
  M2 (P1 complete): M3 置信度可审计 + 超时参数可控 → 运维调试效率提升
  M3 (P2 complete): 代码质量统一 + 外部接口稳定
```

---

## 修订记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-06-08 | 初版：7 GAP 对应 6 个 Task Group |
