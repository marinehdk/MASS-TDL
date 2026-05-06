# RFC-002: M2 ← Multimodal Fusion（三话题聚合 + CPA/TCPA 计算责任）

| 属性 | 值 |
|---|---|
| RFC 编号 | SANGO-ADAS-RFC-002 |
| 状态 | 草拟（待评审）|
| 阻塞优先级 | 中 |
| 责任团队 | L3 战术层 + Multimodal Fusion 子系统 |
| 关联 finding | F-P1-D4-019 + F-P1-D4-031 |

---

## 1. 背景

### 1.1 v1.1.1 中的相关章节

- §6 M2 World Model（图 6-1 修订后；输入改为 Multimodal Fusion 子系统）
- §6.4 接口契约（M2 内部数据聚合策略）
- §15.1 World_StateMsg IDL（v1.1.1 完整定义）

### 1.2 当前设计假设

- v1.0 错误：图 6-1 标 "L1 感知层"
- v1.1 修订：M2 订阅 Multimodal Fusion 子系统**三个独立话题**：
  - `/perception/targets` TrackedTargetArray @ 2 Hz
  - `/nav/filtered_state` FilteredOwnShipState @ 50 Hz
  - `/perception/environment` EnvironmentState @ 0.2 Hz
- M2 内部聚合 → World_StateMsg @ 4 Hz 输出 M3/M4/M5/M6
- **CPA/TCPA 计算责任在 M2**（Multimodal Fusion 不提供）

### 1.3 跨团队对齐的必要性

须 Multimodal Fusion 团队确认：
- 三话题输出 schema + 频率是否稳定
- 坐标系约定（对地 vs 对水）
- FilteredOwnShipState 中 (u, v) 是否含海流估计
- 失效模式 / 降级状态对 M2 的通知方式

---

## 2. 提议

### 2.1 接口 IDL（引用 v1.1.1 §15.1 + §6.4 — **不变**）

```
# TrackedTargetArray (Multimodal Fusion → M2, 2 Hz)
/perception/targets:
  TrackedTarget[]: track_id, mmsi, lat/lon, cog/sog, heading/rot,
                   length/beam, position/velocity_covariance,
                   confidence, classification, source_sensors

# FilteredOwnShipState (Multimodal Fusion → M2, 50 Hz)
/nav/filtered_state:
  lat/lon, sog/cog, u/v (船体), heading/yaw_rate, roll/pitch,
  position/heading_uncertainty, nav_mode, current_speed/direction

# EnvironmentState (Multimodal Fusion → M2, 0.2 Hz)
/perception/environment:
  visibility, sea_state, traffic_density, zone_type/in_tss/in_narrow_channel,
  SensorCoverage, BlindSector
```

### 2.2 M2 内部聚合策略（v1.1.1 §6.4）

- **频率适配**：M2 以 2 Hz 输入聚合 + 1 次插值/外推 → 4 Hz World_StateMsg
- **CPA/TCPA 计算**：M2 自行计算（M2 详细设计 §5.3）
- **自身状态时间对齐**：M2 取最近 FilteredOwnShipState 快照
- **ENC 约束聚合**：M2 从 EnvironmentState 提取 zone_constraint
- **坐标系**：目标速度 = 对地（sog/cog）；自身 = 对水（u/v）+ 海流估计

---

## 3. 备选方案

### 3.1 方案 A：CPA/TCPA 计算移到 Multimodal Fusion（已弃用）

- **方案**：Fusion 内部计算 CPA/TCPA，附加到 TrackedTargetArray
- **弃用理由**：
  - Fusion 是感知子系统，不应持有 COLREG 推理基础数据
  - CPA/TCPA 依赖本船状态（自身位置 / 速度），跨子系统时序对齐复杂
  - DNV-CG-0264 §9.3 监控独立性：感知 vs 决策须分离

### 3.2 方案 B：M2 订阅单一聚合话题（已弃用）

- **方案**：Multimodal Fusion 内部聚合三话题为单一 SituationalAwarenessMsg @ 4 Hz
- **弃用理由**：
  - Fusion 内部三个话题来自不同传感器频率，强制聚合引入额外时延
  - 50 Hz FilteredOwnShipState 降频到 4 Hz 浪费高频自身状态信息（M5 BC-MPC 需要）

---

## 4. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| Fusion 三话题频率不稳定（实际 < 2 Hz）| 中 | 高（M2 4 Hz 输出 SLA 违反）| Fusion 提供"健康度信号"（最近 N 帧的实际频率统计）→ M2 触发降级 |
| TrackedTarget.confidence 字段含义不一致 | 高 | 中（M2 / M7 SOTIF 阈值无法标准化）| 双方约定 confidence 范围 + 计算依据；F-NEW-002 类似的 enum 化思路 |
| 坐标系混淆（对地 vs 对水）| 中 | **极高**（M5 操纵模型用错速度 → 轨迹预测错误）| v1.1.1 §6.4 已明示；本 RFC 强制双方书面确认 |
| FilteredOwnShipState (u, v) 是否含海流估计 | 中 | 中（M5 MPC 用错船体速度）| Fusion 团队书面说明；如不含，M2 须从 current_speed/direction 自行减去 |
| Fusion 失效时 M2 降级路径 | 高 | 高（M2 World_StateMsg 输出停滞）| Fusion 提供 nav_mode（OPTIMAL/DR_SHORT/DR_LONG/DEGRADED）；M2 按 nav_mode 切换降级行为 |

---

## 5. 决议项清单

| # | 决议项 | 预期答复方 |
|---|---|---|
| 1 | 三话题输出频率稳定性保证（最坏情况下 < ?% 帧丢失？）| Multimodal Fusion |
| 2 | TrackedTarget.confidence 范围 + 计算方法 | Multimodal Fusion |
| 3 | FilteredOwnShipState (u, v) 是否含海流估计？current_speed/direction 是否独立可减？ | Multimodal Fusion |
| 4 | Fusion 健康度信号（用于 M2 降级触发）的 schema 与频率 | Multimodal Fusion |
| 5 | M2 计算 CPA/TCPA 是否引发本船状态时间对齐复杂性？是否需 Fusion 提供 own_ship_at_target_time 辅助？| L3 + Fusion |
| 6 | EnvironmentState 0.2 Hz 是否足够？（M5 TSS Rule 10 多边形约束更新频率需求）| L3（M6 设计师）+ Fusion |

---

## 6. 验收标准

- ✅ Multimodal Fusion 团队书面确认三话题 schema + 频率 + 坐标系
- ✅ M2 内部聚合策略测试通过（HIL：2 Hz 输入 → 4 Hz 输出无丢帧）
- ✅ Fusion DEGRADED 状态下 M2 降级路径明确
- ✅ TrackedTarget.confidence 含义统一

---

## 7. 时间表

| 里程碑 | 日期 |
|---|---|
| Kick-off | T+1 周 |
| Fusion 团队书面回复 | T+2 周 |
| 深度对齐会议 | T+2.5 周（1 小时）|
| 决议签署 | T+3 周 |

---

## 8. 参与方

- **L3 架构师**（M2 设计师）— 提议方
- **Multimodal Fusion 团队架构师** — 接受 / 反对
- **L5 控制分配层**（咨询）— 海流估计在控制环路的影响

---

## 9. 参考

- **v1.1.1 锚点**：§6 / §6.4 / §15.1 World_StateMsg
- **Multimodal Fusion 现有设计**：`docs/Init From Zulip/MASS ADAS Multimodal Fusion/`
  - `MASS_ADAS_Perception_Multi_Sensor_Fusion_技术设计文档.md`
  - `MASS_ADAS_Navigation_Filter_技术设计文档.md`
  - `MASS_ADAS_Perception_Situational_Awareness_技术设计文档.md`
