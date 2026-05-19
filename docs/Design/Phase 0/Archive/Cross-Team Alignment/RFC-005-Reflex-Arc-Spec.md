# RFC-005: Y-axis Reflex Arc spec 量化（触发阈值 + 感知输入源）

| 属性 | 值 |
|---|---|
| RFC 编号 | SANGO-ADAS-RFC-005 |
| 状态 | 草拟（待评审）|
| 阻塞优先级 | **高** — Reflex Arc 现有 spec 未量化，是系统层 Y 轴安全关键缺口 |
| 责任团队 | L3 战术层 + Reflex Arc 子系统 + Multimodal Fusion + L5 控制分配层 + Hardware Override Arbiter 团队 |
| 关联 finding | F-P1-D4-034 + F-P2-D5-013 |

---

## 1. 背景

### 1.1 v1.1.1 中的相关章节

- §4.5 与 Y-axis Emergency Reflex Arc 的接口（v1.1 新增）
- §11.9 + §11.9.1 接管期间 L3 行为（含 ReflexActivationNotification 处理）
- §15.1 EmergencyCommand + ReflexActivationNotification IDL（v1.1.1 完整定义）
- §15.2 接口矩阵第 16、17 行

### 1.2 当前设计假设

- v1.0 缺陷（F-P1-D4-034）：v1.0 §15 完全无 Reflex Arc 协议；v3.0 Kongsberg HTML 仅声明 "Perception 极近距离 → bypass L3/L4 → 直达 L5（< 500 ms）" 无量化
- v1.1 修订：§15.1 IDL 含字段占位（cpa_at_trigger_m / range_at_trigger_m / sensor_source / action）
- **关键缺口**：触发阈值（CPA、距离、时间）+ 感知输入源 + L3 通知协议**仍未量化**，须跨团队补 spec

### 1.3 跨团队对齐的必要性

Reflex Arc 是 v3.0 Y 轴安全关键路径，跨多个团队：
- **Reflex Arc 团队**：核心算法 + 触发逻辑（DDS 传输到 L5）
- **Multimodal Fusion**：极近距离感知输入源（雷达 / LiDAR / 距离）
- **Hardware Override Arbiter 团队**：Reflex Arc 与人工接管的优先级仲裁
- **L5 控制分配层**：接收 EmergencyCommand 后的执行机构反应
- **L3 战术层（M1）**：接收 ReflexActivationNotification 后的内部冻结

---

## 2. 提议

### 2.1 接口 IDL（引用 v1.1.1 §15.1 — **不变**）

```
# EmergencyCommand (Reflex Arc → L5, 事件)
message EmergencyCommand {
    timestamp    trigger_time;
    float32      cpa_at_trigger_m;
    float32      range_at_trigger_m;
    string       sensor_source;       # "fusion"|"lidar_emergency"|...
    enum action; # STOP|REVERSE|HARD_TURN
    float32      confidence;
}

# ReflexActivationNotification (Reflex Arc → M1, 事件)
message ReflexActivationNotification {
    timestamp    activation_time;
    string       reason;
    bool         l3_freeze_required;  # true → L3 进入 OVERRIDDEN 模式
}
```

### 2.2 触发阈值量化（提议初始值，待跨团队评审）

| 触发条件 | 提议初始值 [HAZID 校准] | 工程依据 |
|---|---|---|
| **CPA 极近距离** | CPA < 50 m **且** TCPA < 5 s | 物理碰撞前 5 秒；FCB 18 kn 速度下 5 s = 46 m 接近距离 |
| **绝对距离阈值** | range < 100 m | 雷达 / LiDAR 极近距离感知可靠区间下限 |
| **时间到碰撞**（time-to-collision，TTC）| TTC < 3 s | 人工接管时窗下限（< TMR 60 s 极远）；Reflex Arc < 500 ms 旁路时延 |
| **多目标聚合触发** | 同时 ≥ 2 目标满足上述任一 → 升级 HARD_TURN | 紧急避让 multi-threat 场景 |

### 2.3 感知输入源（提议）

```
┌──────────────────────────────────────────────────────┐
│  Multimodal Fusion 感知融合输出（独立通路）          │
│    ├── /perception/targets @ 2 Hz（Track 级）─→ M2  │
│    └── /perception/proximity_emergency @ 50 Hz ───┐  │
│         （新增高频极近距离话题）                  │  │
└─────────────────────────────────────────────────┬─┘
                                                   ↓
                                       Reflex Arc 触发逻辑
                                                   │
                          ┌────────────────────────┴───┐
                          ↓                            ↓
                  EmergencyCommand            ReflexActivationNotification
                       → L5                          → M1
                       (DDS, < 500 ms)               (DDS, < 50 ms)
```

**提议**：Multimodal Fusion 新增高频话题 `/perception/proximity_emergency` @ 50 Hz，专供 Reflex Arc 触发（与 M2 主输入 2 Hz 解耦）。

### 2.4 优先级仲裁（提议）

```
优先级（最高 → 最低）：
1. Hardware Override Arbiter（人工接管）→ 任何路径都不能覆盖
2. Reflex Arc EmergencyCommand → 旁路 L3/L4，直接 L5
3. M5 ReactiveOverrideCmd → L4 reactive_override 模式（详见 RFC-001）
4. M5 AvoidancePlan → L4 avoidance_planning 模式
5. L2 PlannedRoute + L4 LOS+WOP → L4 normal_LOS 模式（默认）
```

### 2.5 L3 内部响应（v1.1.1 §11.9 + §11.9.1）

接收 ReflexActivationNotification 后：

- M1：模式状态机切换到 OVERRIDDEN（reflex 触发原因）
- M5：冻结双层 MPC 求解
- M7：暂停主仲裁，保留降级监测线程
- M8：实时显示 "Reflex Active" 高优先级告警（红色，< 100 ms 时延）
- ASDR：标记 `reflex_activation` 事件 + 全字段记录

---

## 3. 备选方案

### 3.1 方案 A：纯硬件 Reflex Arc（已弃用）

- **方案**：Reflex Arc 是硬件级（如直接接近开关 → 继电器 → 紧急停车），不经过软件感知
- **弃用理由**：FCB 高速场景下，纯硬件无法识别 "目标船" vs "海浪 / 渔具"；误触发率不可接受

### 3.2 方案 B：Reflex Arc 嵌入 M7（已弃用）

- **方案**：取消独立 Reflex Arc 子系统，把 < 500 ms 旁路嵌入 M7
- **弃用理由**：违反 Doer-Checker 独立性（M7 Checker 不应持有控制路径）；与 v3.0 Y 轴架构不一致

### 3.3 方案 C：使用 M2 现有 2 Hz Track 流（已弃用）

- **方案**：Reflex Arc 直接订阅 M2 的 World_StateMsg @ 4 Hz
- **弃用理由**：4 Hz 不足以保证 < 500 ms 时延；Reflex Arc 须独立感知通路（决策四独立性约束）

---

## 4. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| **触发阈值过保守 → 误触发率高** | 高 | **高**（频繁 reflex bypass 干扰正常运营）| 阈值标 [HAZID 校准]（附录 E.1）；FCB 实船试航采集误触发率统计 |
| **触发阈值过激进 → 漏触发碰撞** | 低 | **极高**（碰撞）| 多重触发条件（CPA / 距离 / TTC 任一）+ 兜底 ≥ 2 目标聚合 |
| **`/perception/proximity_emergency` 50 Hz 高频负载** | 中 | 中（Multimodal Fusion 计算开销）| Fusion 团队评估：仅在距离 < 200 m 范围内启用 50 Hz 输出 |
| **Reflex Arc 与 M5 BC-MPC ReactiveOverrideCmd 时序冲突** | 中 | **高**（L4/L5 收到两套指令）| 优先级仲裁明确（§2.4）；L4 须实现优先级处理逻辑 |
| **L3 内部冻结时延 > 100 ms**（M1 → M5/M7/M8 串行通知）| 中 | 中（旧指令短时残留）| M1 接收 ReflexActivationNotification 后采用并行广播；监控 ROS2 总线时延 |
| **Reflex Arc 自身失效未被检测** | 低 | **极高**（碰撞前无最后兜底）| Reflex Arc 心跳监控（独立总线，X-axis Checker 监控）|
| **CCS / DNV 审查 Reflex Arc 形式化验证缺失** | 高 | 中（认证延迟）| Reflex Arc 触发逻辑须形式化模型化（提议：状态机 + 阈值 + 多重 voting）|

---

## 5. 决议项清单

| # | 决议项 | 预期答复方 |
|---|---|---|
| 1 | **触发阈值初始值**（CPA < 50 m, range < 100 m, TTC < 3 s）是否合理？ | Reflex Arc + Multimodal Fusion |
| 2 | **多重触发条件 voting**（任一 vs ≥2 同时）是否清晰？ | Reflex Arc 团队 |
| 3 | **`/perception/proximity_emergency` 50 Hz 话题** 是否可在 Multimodal Fusion 中实现？计算开销？ | Multimodal Fusion 团队 |
| 4 | **EmergencyCommand action 枚举**（STOP / REVERSE / HARD_TURN）是否覆盖所有场景？是否需补充？ | Reflex Arc + L5 |
| 5 | **L5 接收 EmergencyCommand 后的执行机构响应**：< 500 ms 端到端时延是否可达？ | L5 控制分配层 |
| 6 | **优先级仲裁**（§2.4）是否清晰？特别是 Hardware Override 与 Reflex Arc 的优先级（人工接管能否阻断 Reflex）？ | Hardware Override Arbiter + Reflex Arc |
| 7 | **L3 内部冻结时延**：M1 接收 ReflexActivationNotification → M5/M7/M8 全部进入 OVERRIDDEN 状态的端到端时延 SLA？ | L3 (M1 设计师) |
| 8 | **Reflex Arc 心跳监控**：由谁监控？X-axis Checker？M1？独立 watchdog？ | 系统架构组 |
| 9 | **CCS / DNV 审查接口**：Reflex Arc 触发逻辑是否需 SIL 评估？提议 SIL 3（最高安全等级）| CCS 验船师 + Reflex Arc |
| 10 | **HAZID 校准计划**：Reflex Arc 触发阈值的实船试航数据采集计划？ | HAZID 工作组（详见附录 E）|

---

## 6. 验收标准

- ✅ Reflex Arc 团队补充完整 spec 文档（量化触发阈值 + 感知输入源 + 时序）
- ✅ 5 个决议项（阈值、感知话题、优先级、SIL、HAZID 计划）跨团队签署
- ✅ HIL 测试：100 个紧急碰撞场景 → 触发率 ≥ 99%；同期 100 个正常场景 → 误触发率 ≤ 1%
- ✅ FCB 实船试航前完成初始 HAZID 校准
- ✅ 端到端时延（感知 → EmergencyCommand → L5 执行）< 500 ms 通过 HIL 验证
- ✅ Reflex Arc 形式化模型（状态机 + voting）通过 CCS / DNV 评审

---

## 7. 时间表

| 里程碑 | 日期 |
|---|---|
| Kick-off | T+1 周 |
| Reflex Arc + Multimodal Fusion 联合评审 | T+2 周 |
| 深度对齐会议（多团队）| T+2.5 周（2 小时 — **最复杂 RFC**）|
| spec 补充完成 | T+4 周 |
| 决议签署 | T+5 周 |
| HIL 100 + 100 场景验证 | T+10 周 |
| FCB 实船 HAZID 校准 | T+12–14 周 |

---

## 8. 参与方

| 角色 | 团队 | 职责 |
|---|---|---|
| **架构师**（L3 主提议方）| L3 战术层 | 提议 + L3 内部响应设计 |
| **架构师**（Reflex Arc）| Reflex Arc 子系统 | 核心算法 + 触发逻辑 + spec 补充 |
| **架构师**（Multimodal Fusion）| Multimodal Fusion 子系统 | 高频感知输入源 |
| **架构师**（L5）| L5 控制分配层 | EmergencyCommand 执行 + 时延 SLA |
| **架构师**（Hardware Override）| Override Arbiter 团队 | 优先级仲裁 |
| **CCS 验船师** | 外部 | SIL 评估 + 形式化验证审查 |
| **PM** | 系统集成 | 跨多团队协调（最复杂）|

---

## 9. 参考

- **v1.1.1 锚点**：§4.5 / §11.9 / §15.1 EmergencyCommand / §15.1 ReflexActivationNotification / §15.2
- **v3.0 Kongsberg 基线**：CLAUDE.md §2 "Y-axis Emergency Reflex Arc — Perception 极近距离 → bypass L3/L4 → 直达 L5（< 500 ms）"
- **Hardware Override 现有设计**：`docs/Init From Zulip/MASS ADAS Deterministic Checker/MASS_ADAS_Hardware_Override_Arbiter_技术设计文档.md`（§3.1 P2 优先级 + §14.1 < 500 ms）
- **学术参考**：NASA Auto-GCAS（98.5% 防护率，Monitor 触发预定义机动）；Simplex Architecture HAC 保守控制器
- **法规依据**：IMO MASS Code MSC 110 Part 2 Chapter 1 OE 越界响应；DNV-CG-0264 §9.7 / §9.8
