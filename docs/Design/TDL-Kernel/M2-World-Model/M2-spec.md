# M2 · World Model · 设计规范 + 实现现状

| 属性 | 值 |
|------|-----|
| 模块代号 | M2 |
| 文档版本 | v2.0 |
| 日期 | 2026-06-08 |
| 状态 | 设计规范 ✅ 实现现状分析中 🟡 |
| 架构基线 | 架构设计报告 v1.1.3-pre-stub §6 + §15.1 |
| 代码路径 | `src/l3_tdl_kernel/m2_world_model` |
| 消息定义 | `src/l3_tdl_kernel/l3_msgs/msg/` + `l3_external_msgs/msg/` |
| 前版参考 | `M2-spec.md` v1.0 (2026-05-20) |

---

## 上篇：设计规范（Design Specification）

*本节为正式技术规范，供团队同事、CCS 审核使用。描述 M2 World Model 应当遵守的接口契约、内部架构和认证要求。*

---

### 1. 模块定位

#### 1.1 职责

M2 World Model 是 TDL 中**唯一权威的世界视图提供者**。它将感知层输入聚合成统一的、带置信度的、含 COLREG 几何预分类的世界状态，供 M3–M7 消费。

**核心分离原则**：M2 将"传感器读什么数据"和"基于数据做什么决策"彻底分离。任何决策模块（M3/M4/M5/M6/M7）不得自行维护外部世界认知。

#### 1.2 三层视图

M2 维护三个相互独立但协同更新的视图（架构报告 §6.3）：

| 视图 | 缩写 | 内容 | 来源 | 典型频率 |
|------|------|------|------|----------|
| **自身视图** | EV | 本船位置·SOG·COG·航向·u/v 对水速度·舵角·海流估计 | Multimodal Fusion NavFilter | 50 Hz |
| **动态视图** | DV | 目标跟踪列表·运动状态·协方差·分类·航迹状态 | Multimodal Fusion | 2 Hz |
| **静态视图** | SV | ENC 电子海图·禁区·浅水·TSS 航道·Anchorages | EncLoader + 预编译 | 0.067 Hz (15s 心跳) |

#### 1.3 SIL 等级

| 路径 | SIL 等级 | 依据 |
|------|----------|------|
| 感知数据融合、目标跟踪聚合 | SIL 1 | 非核心安全功能，但属于 CCS 认证关键路径 |
| ENC 静态世界查询 | SIL 1 | 地图数据错误可导致避碰决策错误，须有降级路径 |

---

### 2. 接口契约

#### 2.1 上游订阅（输入）

| # | 话题 | 来源 | 类型 | 频率 | QoS | 内容 |
|---|------|------|------|------|-----|------|
| S1 | `/fusion/own_ship_state` | Multimodal Fusion NavFilter | `l3_external_msgs::FilteredOwnShipState` | 50 Hz | SensorDataQoS keep_last(2) | 本船位置·SOG·COG·heading·u/v 对水·r_dot·海流·nav_mode·6×6 协方差 |
| S2 | `/fusion/tracked_targets` | Multimodal Fusion | `l3_external_msgs::TrackedTargetArray` | 2 Hz¹ | reliable keep_last(5) | N 个 TrackedTarget·每目标含位置·SOG·COG·heading·3×3 协方差·分类·置信度 |
| S3 | `/fusion/environment_state` | Multimodal Fusion | `l3_external_msgs::EnvironmentState` | 0.2 Hz | reliable transient_local keep_last(5) | 风速·风向·浪高·浪向·海流·能见度·天气来源 |
| S4 | `/l3/m1/odd_state` | M1 ODD/Envelope Manager | `l3_msgs::ODDState` | 1 Hz + event | reliable transient_local keep_last(10) | ODD 子域·自主等级·健康状态·Conformance_Score·TMR·TDL |

¹ 注：此频率为当前设计预期值，外部融合团队对接时需最终确认。

**关键约束**：
- 所有输入消息必须携带 `stamp`（采样时间）、`confidence ∈ [0,1]`、`schema_version`
- S1 与 S2 的时间戳偏差 `|Δt| ≤ 1.0s`，否则 M2 跳过该周期的 CPA 计算
- 上游话题命名空间 `/fusion/` 标识 Multimodal Fusion 层，非 M2 所有；**注意不与 D2.2-spec.md 中的旧命名 `/perception/` 混淆，实际实现以 `/fusion/` 为准**

#### 2.2 下游发布（输出）

| # | 话题 | → 目标 | 类型 | 频率 | QoS | 内容 |
|---|------|--------|------|------|-----|------|
| P1 | `/l3/m2/world_state` | M3, M4, M5, M6, M7 | `l3_msgs::WorldState` | 4–10 Hz | reliable keep_last(5) | 自船快照 + N 个 TrackedTarget + ZoneConstraint |
| P2 | `/l3/sat/data` | M8 HMI Bridge | `l3_msgs::SATData` | 10 Hz | reliable keep_last(5) | SAT-1 状态摘要 + SAT-2 推理链 + SAT-3 预测 |
| P3 | `/l3/asdr/record` | ASDR 黑匣子 | `l3_msgs::ASDRRecord` | event + 定期 | reliable transient_local keep_last(50) | 世界状态快照·协方差膨胀·异常事件 |

#### 2.3 P1 WorldState 消息结构

```idl
# WorldState.msg
# Per v1.1.2 §15.1 World_StateMsg + §6
# Frequency: 4 Hz (2 Hz 输入 × 2 次插值/外推)
uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp

l3_msgs/TrackedTarget[] targets         # 含 CPA/TCPA + 不确定度 + COLREG 预分类
l3_msgs/OwnShipState own_ship           # 最近 FilteredOwnShipState 快照
l3_msgs/ZoneConstraint zone             # ENC 约束（禁区·TSS·水深）

float32 confidence                       # 整体世界视图置信度
string rationale                         # SAT-2: 聚合质量摘要
```

#### 2.4 TrackedTarget 消息结构（关键字段清单）

```idl
# TrackedTarget.msg — M2 输出每目标完整字段
uint16 schema_version
builtin_interfaces/Time stamp
uint64 target_id

# 位置（WGS84）
geographic_msgs/GeoPoint position

# 运动（对地）
float64 sog_kn
float64 cog_deg
float64 heading_deg

# 不确定度（3×3 row-major: lat/lon/heading）
float64[9] covariance

# 分类
string classification                    # "vessel" | "fixed_object" | "unknown"
float32 classification_confidence

# ── M2 计算字段 ──
float64 cpa_m                            # 最近会遇距离（米）
float64 tcpa_s                           # 到最近会遇时间（秒）
l3_msgs/EncounterClassification encounter # COLREG 几何预分类
float32 confidence                       # 整体置信度
float32 intent_confidence                # 意图置信度 [0, 1]（B P1-B-02）
string rationale                         # SAT-2 跟踪推理依据
string source_sensor                     # "radar" | "ais" | "lidar" | "camera" | "fused"
float64 cpa_covariance_m2                # CPA 方差 [m²] — UKF/Linear 传播
float64 tcpa_covariance_s2               # TCPA 方差 [s²] — UKF/Linear 传播
float64 brg_deg                          # 真方位 0–360°（0°=真北）
float64 rng_m                            # 距离（米）
```

---

### 3. 内部架构

#### 3.1 子模块总览

```
M2 World Model Node (C++ ROS2)
├── CoordTransform           — WGS84 ↔ ENU 坐标变换（原点=本船首帧位置）
├── CpaTcpaCalculator        — CPA/TCPA 计算 + 不确定度传播
│   ├── 方法: Linear (默认) / UKF / Monte Carlo / CE-Adaptive（策略模式）
│   └── 输入: 本船快照 + 目标快照 + ODD 子域 → 输出: CpaResult{cpa_m, tcpa_s, cpa_sigma, tcpa_sigma}
├── EncounterClassifier      — COLREG Rule 13-15 几何预分类
│   └── 扇区: Overtaking [112.5°,247.5°], Head-On |Δhdg−180°|≤6°, 其余 Crossing
├── TrackBuffer              — 目标持久缓冲 / 保留 / 淘汰
│   └── max=256 targets, aged out after N disappearance cycles
├── ViewHealthMonitor        — DV/EV/SV 三视图健康状态机
│   └── Full ↔ Degraded ↔ Critical 三级状态 + 置信度追踪
├── EncLoader                — ENC 电子海图静态世界（预编译 GeoJSON → 运行时查询）
│   └── query_zone() / in_tss() / in_narrow_channel() / get_exclusion_zones()
├── EnvSanityChecker         — 环境输入数据合理性校验
│   └── 范围检查·staleness·zone 跳变·跨源一致性
└── WorldStateAggregator     — 合成最终 WorldState 输出（定时触发）
    └── 置信度 = min(per_target最低, DV健康, EV健康, SV健康)
```

#### 3.2 数据流

```
S1 ──► CoordTransform ──┬──► CpaTcpaCalculator ──┬──► WorldStateAggregator ──► P1
S2 ──► TrackBuffer ─────┤                        │         │
                        │                        │         ├── EncounterClassifier
                        │                        │         ├── EnvSanityChecker
S3 ──► EnvSanityChecker ─┘                        │         └── ViewHealthMonitor
                                                 │
S4 ──► ODD State Cache ──────────────────────────┘
     (提供 ODD zone 给 CPA 安全系数调整)

P2 ←── SAT 数据 (1/2/3 层) ←── WorldStateAggregator
P3 ←── ASDR 事件 ←── 各子模块异常
```

#### 3.3 CPA 计算算法（设计）

```
输入: 本船快照(位置·u/v·heading·海流) + 目标快照(位置·SOG·COG·协方差)

1. 时间对齐检查: |t_own - t_target| ≤ 1.0s → 否则返回 nullopt
2. 坐标变换: WGS84 → ENU（原点 = 本船位置）
3. 本船速度 = u_water + v_water + 海流（不是简单 SOG/COG）
4. 目标外推: 按 SOG/COG 线性外推到本船时间戳
5. 静态目标检测: SOG < 0.1 m/s → CPA = 当前距离, TCPA = 0
6. CPA/TCPA 计算:
   tcpa = -rel_pos · rel_vel / |rel_vel|²
   cpa  = |rel_pos + rel_vel × tcpa|
7. 不确定度传播: 目标协方差 + 本船协方差 → CPA_sigma, TCPA_sigma
   方法优先级: UKF > Linear > MC（CE-Adaptive 预留）
8. 安全系数: ODD-A/B/C × 1.0, ODD-D × 1.5

输出: CpaResult{cpa_m, tcpa_s, cpa_sigma, tcpa_sigma}
```

#### 3.4 COLREG 几何预分类算法

```
对每个目标:
  1. Safe Pass: 相对速度 < 0.5 m/s 且 CPA > 926m → AMBIGUOUS
  2. Overtaking (Rule 13): bearing ∈ [112.5°, 247.5°] → OVERTAKING
  3. Head-On (Rule 14): |Δheading − 180°| ≤ 6° → HEAD_ON
  4. Crossing (Rule 15): 其余 → CROSSING
     - 目标在右舷 (bearing < 180°) → give-way
     - 目标在左舷 (bearing ≥ 180°) → stand-on
```

---

### 4. 多船型参数化（Capability Manifest）

M2 通过读取 Capability Manifest 获取船型特定参数（架构报告 §13.2），禁止硬编码船型常量。

| 参数 | 用途 | Manifest YAML 路径 |
|------|------|-------------------|
| `max_speed_kn` | SOG 上限校验（MUST-6: sog > max_speed×1.2 → 拒绝） | `envelope.ood_a.max_speed_kn` |
| `CPA_safe_A/B/C/D` | 各 ODD 子域安全 CPA 阈值 | `envelope.ood_*.min_cpa_nm` |
| `TCPA_safe_A/B/C/D` | 各 ODD 子域安全 TCPA 阈值 | `envelope.ood_*.min_tcpa_min` |
| `dynamic_horizon_nm` | ENC 动态查询半径 | `envelope.dynamic_horizon_nm` |

---

### 5. 降级行为（DEGRADED / CRITICAL）

| 降级状态 | 触发条件 | M2 行为 |
|----------|----------|---------|
| **EV CRITICAL** | 本船数据 >100ms 未更新 | 不发布 WorldState（无声跳过） |
| **DV DEGRADED** | 目标数据连续 2 周期未到 | confidence = 0.5, 沿用上周期目标 |
| **DV CRITICAL** | 目标数据连续 5 周期未到 | confidence = 0.0, 仅发布 EV + SV |
| **SV DEGRADED** | 环境数据 >60s 未更新 | ZoneConstraint 沿用上次有效值 |
| **SV CRITICAL** | 环境数据 >120s 未更新 | ZoneConstraint 仅保留 zone_type, 清空 polygons |
| **UKF 降级** | 协方差正定性检查失败 | 回退 Linear 传播, 保守默认值 σ_pos=50m |
| **ENC 数据丢失** | EncLoader 加载失败 | ZoneConstraint 仅 zone_type, SV 标记 DEGRADED |

---

### 6. CCS 入级映射

| M2 子能力 | DNV-CG-0264 子功能（CCS 等效） | 证据类型 |
|-----------|-------------------------------|----------|
| 三视图聚合（DV/EV/SV） | **S1** 传感器数据处理 | 感知融合测试报告 |
| CPA/TCPA + 不确定度 | **S2** 态势评估 | 碰撞风险评估测试 |
| COLREG 几何预分类 | **S2** 态势评估 | 分类器边界测试（MUST-1） |
| ENC 静态世界集成 | **S1** 传感器数据处理 | ENC 查询集成测试 |
| 环境 sanity checks | **S1** 传感器数据处理 | 输入质量过滤测试 |

---

---

## 下篇：实现现状 + GAP 分析

*本节记录代码实际状态与设计规范的差距，供开发排期参考。*

---

### 7. 实现状态总览

| 子能力 | 设计要求 | 代码实际 (2026-06-08) | 状态 |
|--------|----------|----------------------|------|
| ROS2 节点 + 发布 | 4–10 Hz WorldState | 默认 4 Hz（参数化） | ✅ |
| CPA/TCPA 计算 | 含不确定度传播 | Linear 已实现，UKF 在 D2.2 设计中但代码未见落版 | 🟡 |
| COLREG 几何预分类 | [112.5°,247.5°] | 已验证 | ✅ |
| 三视图健康监控 | DV/EV/SV 独立监控 | 已实现（ViewHealthMonitor） | ✅ |
| WorldState 置信度 | min(per_target, DV, EV, SV) | 已实现 | ✅ |
| Schema_version | 强制 v3.0 | 代码未设置（始终为 0） | 🔴 |
| CPA 安全阈值(ODD A-D) | 1852/555.6/277.8/2778 m | 参数已配置 | ✅ |
| intent_confidence | B P1-B-02 | 字段在 msg 中，但填充逻辑在 D2.2 设计中 | 🟡 |
| brg_deg / rng_m | ARPA 表用 | 字段在 msg 中，填充逻辑在 D2.2 设计中 | 🟡 |
| cpa_covariance / tcpa_covariance | UKF/线性传播 | 字段在 msg 中，填充逻辑在 D2.2 设计中 | 🟡 |
| ENC 端到端集成 | S-57→ZoneConstraint 填充 | EncLoader 存在但 exclusion_zones/tss_lanes 留空 | 🔴 |
| 环境 sanity checks | 7 项校验 | EnvSanityChecker 存在，待验证完整覆盖 | 🟡 |
| SOG 上限校验(MUST-6) | Manifest 读取 | track_validator.py 存在，但 Manifest 读取路径未端到端验证 | ⚫ |
| OVERTAKING 边界测试(MUST-1) | 4 边界单元测试 | test 文件存在，验证结果待确认 | ⚫ |
| SAT 数据发布 | 10 Hz | 1 Hz（默认参数） | 🟡 |
| 数据源 | Multimodal Fusion 真实输出 | 100% Mock 源（external_mock_publisher / sil_mock_node / lifecycle_mgr free_run） | 🔴 |

**图例**：✅ 已实现 / 🟡 部分 / 🔴 未做或缺失 / ⚫ 未验证

---

### 8. 关键 GAP 详细分析

#### GAP-1 🔴 数据源全部为 Mock，无真实感知融合

| 层面 | 设计 | 现状 |
|------|------|------|
| `/fusion/*` 发布者 | 真实 Multimodal Fusion 节点（外部团队） | 3 个并行 Mock 源，发布静态或仿真数据 |
| 融合节点存在性 | 外部团队开发 | 本项目仓库中不存在融合节点代码 |
| 测试覆盖 | 含传感器退化场景 | 无法测试 D2.5+ 真实退化场景 |

**修复意见**：
- P0: 制定 `/fusion/*` 话题接口规格文档（以此规范为准），交给融合团队对齐
- P1: 建立桥接节点 `l3_fusion_bridge` 适配同事的 `nmea_interfaces` 输出
- P2: 全链路 SIL 验证时移除 Mock 源
- 详见第 9 节"传感器融合对接"

#### GAP-2 🔴 schema_version 全链路为 0

**代码**：M2 WorldStateAggregator 在 compose_world_state() 中未设置 `msg.schema_version` → 所有 M2 输出消息 schema_version=0

**影响**：下游 M4/M5/M6/M7 无法做版本兼容性检查。当消息定义升级时（如 TrackedTarget 新增字段），无法区分新旧版本。

**修复意见**：
```cpp
// 在 compose_world_state() 输出前追加
msg.schema_version = 112;  // v1.1.2
```

#### GAP-3 🟡 发布频率低于设计目标

| 输出 | 设计 | 当前 |
|------|------|------|
| WorldState | 4–10 Hz（设计）/ 50 Hz（架构报告声称） | 4 Hz（aggregation_rate_hz 默认值） |
| SATData | 10 Hz（D2.2 报告） | 1 Hz（sat_rate_hz 默认值） |

**分析**：4 Hz 在当前 2 Hz 目标输入下是合理折中（每周期做 2 次外推）。50 Hz 在当前 2 Hz 输入下是伪命题。但下游 M5 BC-MPC（需 4–10 Hz 世界状态）可能收到过时外推。

**修复意见**：
- 不强制提到 10+ Hz，但将 aggregation_rate_hz 默认值调至 4–6 Hz 并文档化
- SATData 提到 10 Hz 以匹配 M8 刷新率
- 明确文档说明：**M2 输出频率 = min(输入频率 × 2, 参数配置上限)**

#### GAP-4 🟡 D2.2 轨道 A/B/C 代码待落地

D2.2-spec.md 详细设计了 5 个并行轨道的实施内容，但目前（2026-06-08）代码审查发现：

| 轨道 | 设计产物 | 代码状态 | 时间窗口 |
|------|----------|----------|----------|
| A: UKF 协方差链 | CpaTcpaCalculator 扩展 + 策略模式 | 字段在 msg 中，UKF 代码实现未落版 | 2026-06-16 窗口 |
| B: ENC 端到端 | EncLoader 扩展 + ZoneConstraint 填充 | exclusion_zones 仍留空 | 2026-06-16 窗口 |
| C: 字段补全 | intent_confidence / BRG/RNG / env sanity | 字段在 msg 中，填充逻辑待验证 | 2026-06-16 窗口 |

#### GAP-5 🟡 MONTE_CARLO 方法为确定性随机（固定种子=42）

```cpp
// cpa_tcpa_calculator.cpp line 248
std::mt19937 gen(42);  // 固定种子
```

**影响**：Monte Carlo CPA 不确定度在相同输入下始终输出相同结果，丢失了采样分布的随机性特征。对形式化验证有利，但对实际不确定性估计有偏。

**修复意见**：保留确定性种子用于单元测试，运行时使用 `std::random_device` 初始化。

#### GAP-6 🟢 超额实现：ViewHealthMonitor 三层独立健康监控

这超出了 M2-spec.md 初版的设计要求，属于正面超额实现。保留。

---

### 9. 传感器融合接口对接（外部团队交付）

#### 9.1 同事提供的融合接口

| 属性 | 值 |
|------|-----|
| 话题名 | `/fusion/tracked_targets` |
| 消息类型 | `nmea_interfaces::msg::TrackedTargetArray` |
| 包名 | `nmea_interfaces` |

全字段定义详见 [外部参考文档]。

#### 9.2 接口差异清单

| 字段 | M2 期望 (`l3_msgs::TrackedTarget`) | 同事输出 (`nmea_interfaces::TrackedTarget`) | 差异类型 |
|------|--------------------------------------|---------------------------------------------|----------|
| `target_id` | `uint64` | `uint32 track_id` | 类型差异 |
| `position` | `GeoPoint` (lat/lon/alt) | `latitude`, `longitude` (独立 float64) | 结构差异 |
| `sog` | kn (单位) | **m/s** (单位) | ❌ **单位不同** |
| `cog` | 度 | 度 | ✅ |
| `heading` | 度 | 度 | ✅ |
| `rot` | 无 | 有 (`float64 rot`) | ➕ 额外字段 |
| `covariance` | `float64[9]` (3×3) | `float64[4] position_covariance` + `float64[4] velocity_covariance` | ❌ **结构不同** |
| `classification` | 字符串 (`vessel`/`fixed_object`) | 字符串 (更丰富枚举) | ⚠️ 字典需对齐 |
| `confidence` | `float32` [0,1] | `float64` [0,1] | 类型差异 |
| `source_sensor` | 字符串 (单值) | `string[]` (数组) | 结构差异 |
| `track_status` | 无 | 有 (`tentative`/`confirmed`/`lost`) | ➕ 额外字段 |
| `mmsi` | 无 | 有 | ➕ 额外字段 |
| `length` / `beam` | 无 | 有 | ➕ 额外字段 |
| `cpa_m` / `tcpa_s` | M2 计算 | **无** | ✅ M2 自行计算 |
| `encounter` | M2 计算 | **无** | ✅ M2 自行计算 |
| 发布频率 | 期望 2 Hz | **文档未注明** | ⚠️ 需确认 |

#### 9.3 当前 M2 代码使用的实际话题名

**重要发现**：D2.2-spec.md 中写的上游话题名是 `/perception/targets`、`/nav/filtered_state`、`/perception/environment`，但 **M2 实际代码订阅的是 `/fusion/tracked_targets`、`/fusion/own_ship_state`、`/fusion/environment_state`**。两者不一致，应以实际代码为准。

同事输出的 `/fusion/tracked_targets` 与 M2 代码期望的话题名一致 ✅，但消息类型不同。

#### 9.4 推荐对接策略

**阶段一：桥接适配（最小侵入，1–2 周）**

创建 `l3_fusion_bridge` 独立 ROS2 包，桥接节点订阅同事的 `/fusion/tracked_targets`（`nmea_interfaces` 类型），转换为 `l3_external_msgs` 类型发布到 `/fusion/tracked_targets_bridged`。M2 零修改。

**桥接节点单位转换清单**（所有涉及单位差异的字段，缺失转换→M2 收到错误数据）：

| 序号 | 字段 | 来源类型 | 目标类型 | 转换公式 | 风险 |
|------|------|----------|----------|----------|------|
| UC-1 | `sog` | 同事 `float64 sog`（**m/s**） | M2 `float64 sog_kn`（**kn**） | `sog_kn = sog * 1.94384` | 🔴 不转换会导致 M2 认为目标时速 2 倍于实际 |
| UC-2 | `position` | 独立 `latitude` / `longitude` | `geographic_msgs/GeoPoint` | `point.latitude = lat; point.longitude = lon` | 🟢 简单赋值 |
| UC-3 | `track_id` | `uint32` | `uint64 target_id` | `target_id = static_cast<uint64>(track_id)` | 🟢 宽度扩展 |
| UC-4 | `confidence` | `float64` | `float32` | `conf_f32 = static_cast<float>(conf_f64)` | 🟢 精度截断可接受 |
| UC-5 | `position_covariance[4]` | ENU 2×2 | 3×3 协方差 (lat/lon/heading) | `cov[0]=p_cov[0]; cov[4]=p_cov[3]; cov[8]=0.0` | 🟡 位置协方差 OK，无航向协方差→设为 0 |
| UC-6 | `velocity_covariance[4]` | ENU 速度 2×2 | 用于 CPA 不确定度 | 桥接输出在 extra 字段，当前 M2 的 Covariance 只吃 lat/lon/heading | 🟡 需扩展或丢弃 |
| UC-7 | `source_sensors` | `string[]`（多传感器数组） | `string`（单值） | `source = array.empty() ? "unknown" : array[0]` | 🟡 仅取第一个源，融合多源信息丢失 |
| UC-8 | `heading` | `float64`（度） | `float64 heading_deg`（度） | 单位一致，直接赋值 | ✅ |
| UC-9 | `cog` | `float64`（度） | `float64 cog_deg`（度） | 单位一致，直接赋值 | ✅ |
| UC-10 | `rot` | `float64`（度/分钟） | M2 当前无对应字段，但可写入 rationale 或预留 | 建议暂不处理 | 🟢 M2 不计 ROT |

同事需要确认：
- 【频率确认】发布频率（建议 2 Hz，需确认）
- 【单位确认】`sog` 单位是否为 m/s（当前文档单位隐含 m/s，需书面确认）
- 【字典确认】`classification` 完整枚举值列表（用于建立 M2 分类字典映射表）
- 【坐标系确认】协方差矩阵是 ENU 直角坐标系还是 lat/lon 球面坐标系？（当前文档 `position_covariance` 单位是"平方米"，暗示 ENU 坐标系，需确认）
- 【ROT 确认】rot（转向率）是否将用于后续版本？当前 M2 不消费此字段

**阶段二：统一消息定义（M2 v3.0，D2.5+）**

在 `l3_external_msgs` 中定义 `TrackedTargetArrayV2.msg` 吸收同事接口的增强字段（mmsi, length, beam, track_status, rot, 多传感器数组）。M2 升级订阅到新类型，移除桥接。

**阶段三：全链路切换**

移除 Mock 源，全链路使用真实融合输出。

---

### 10. 修复优先级总结

| 优先级 | GAP | 工作项 | 依赖 |
|--------|-----|--------|------|
| P0 🔴 | GAP-1: 无真实融合 | 制定 `/fusion/*` 接口规格 + 与同事对齐 | 外部团队协作 |
| P0 🔴 | GAP-2: schema_version=0 | 2 行代码修复 `compose_world_state()` | 无 |
| P0 🔴 | 对接: 消息类型不一致 | 桥接节点或接口协商 | 外部团队 |
| P1 🟡 | GAP-3: 发布频率 | 调整默认参数 + 文档化 | 无 |
| P1 🟡 | GAP-4: D2.2 轨道实施 | UKF 协方差链 + ENC + 字段填充 | 2026-06-16 窗口 |
| P2 🟡 | GAP-5: MC 固定种子 | 运行时随机化 + 测试保留确定性 | 无 |
| P2 🟡 | GAP: topic 名不统一 | 统一 D2.2-spec.md 与实际代码的 topic 命名 | 文档修订 |

---

### 11. 修订记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-05-20 | 初版 spec stub |
| v2.0 | 2026-06-08 | 完整重写：上篇设计规范 + 下篇实现现状/GAP/对接建议 |
