# RFC-006: M3 → L2 反向 RouteReplanRequest（重规划请求协议）

| 属性 | 值 |
|---|---|
| RFC 编号 | SANGO-ADAS-RFC-006 |
| 状态 | 草拟（待评审）|
| 阻塞优先级 | 中 |
| 责任团队 | L3 战术层 + L2 Voyage Planner |
| 关联 finding | F-P1-D4-035（早期裁决 §B-01 已识别）|

---

## 1. 背景

### 1.1 v1.1.1 中的相关章节

- §7 M3 Mission Manager（v1.1 修订后含重规划触发机制）
- §15.1 RouteReplanRequest IDL（v1.1.1 完整定义）
- §15.2 接口矩阵第 11 行（M3 → L2 Voyage Planner @ 事件）

### 1.2 当前设计假设

- v1.0 缺陷：§15.2 矩阵无 L3 → L2 反向通道
- 早期裁决（`docs/Doc From Claude/L3 实现框架裁决汇总.md` §B-01）已识别此通道缺失
- v1.1 修订：M3 触发重规划请求时，向 L2 发起 `RouteReplanRequest`（不本地重规划）
- v1.1 §7.2：M3 不做实际重规划，仅向 L2 请求新 PlannedRoute

### 1.3 跨团队对齐的必要性

须 L2 Voyage Planner 团队确认：
- 接收 M3 RouteReplanRequest 后的处理协议（deadline / 优先级 / 拒绝条件）
- L2 重规划计算时延能否满足 M3 deadline
- 失败时反馈协议（L2 无法重规划时 M3 如何降级）

---

## 2. 提议

### 2.1 接口 IDL（引用 v1.1.1 §15.1 — **不变**）

```
# RouteReplanRequest (M3 → L2 Voyage Planner, 事件)
message RouteReplanRequest {
    timestamp    stamp;
    enum reason; # ODD_EXIT|MISSION_INFEASIBLE|MRC_REQUIRED|CONGESTION
    float32      deadline_s;                   # 重规划时间窗口
    string       context_summary;              # SAT-2 摘要供 L2 / ROC 阅
    Position     current_position;
    Polygon[]    exclusion_zones;              # 须避开区域（如冲突 TSS）
}
```

### 2.2 L2 处理协议（提议）

```
M3 RouteReplanRequest 到达
    │
    ├── 优先级判定（reason）：
    │   ├── MRC_REQUIRED（最高，deadline ≤ 30 s）
    │   ├── ODD_EXIT（高，deadline ≤ 60 s）
    │   ├── MISSION_INFEASIBLE（中，deadline ≤ 120 s）
    │   └── CONGESTION（低，deadline ≤ 300 s）
    │
    ├── L2 重规划尝试：
    │   ├── 输入：current_position + exclusion_zones + 原 VoyageTask
    │   ├── 算法：L2 内部规划器（A* / RRT* / 优化）
    │   └── 时延约束：≤ deadline_s × 0.7（余量供 L3 接收 + 切换）
    │
    ├── 成功 → 发布新 PlannedRoute + SpeedProfile
    │         → M3 接收 + 校验 + 切换
    │
    └── 失败（超时 / 不可行）→ 回复 ReplanFailureMsg
              → M3 触发 ToR_RequestMsg → ROC 介入
```

### 2.3 失败反馈协议（v1.1.1 未定义，本 RFC 提议补充）

提议在 v1.1.2 §15.1 增加：

```
# ReplanResponseMsg (L2 → M3, 事件)
message ReplanResponseMsg {
    timestamp    stamp;
    enum status; # SUCCESS | FAILED_TIMEOUT | FAILED_INFEASIBLE | FAILED_NO_RESOURCES
    string       failure_reason;        # 失败时的诊断信息
    bool         retry_recommended;     # 是否建议 M3 在新条件下重试
}
```

### 2.4 重规划期间 M3 行为

```
T0: M3 发 RouteReplanRequest @ deadline_s = D
T0+0:    M3 进入 REPLAN_WAIT 状态
T0+0:    M3 通知 M4 当前任务保持（M4 继续按现有 PlannedRoute 执行避碰）
T0+D×0.8: M3 启动超时预警 → ROC 提前知情
T0+D:     L2 未回复 → M3 触发 ToR_RequestMsg @ deadline_s=60s
          L2 回复 SUCCESS → M3 接收新 PlannedRoute → 切换 + 通知 M4
          L2 回复 FAILED → M3 触发 ToR_RequestMsg + 通知 M1（OUT-of-ODD 评估）
```

---

## 3. 备选方案

### 3.1 方案 A：M3 本地重规划（已弃用）

- **方案**：M3 内部实现简化版 A* 算法本地重规划
- **弃用理由**：违反决策二（M3 = 任务跟踪器，不做航次规划，属 L1/L2 职责）；与 v3.0 Kongsberg 立场冲突

### 3.2 方案 B：M1 直接向 L2 请求（已弃用）

- **方案**：ODD 越界检测在 M1，M1 直接向 L2 发请求
- **弃用理由**：违反 M1 职责（ODD 调度），M3 是任务令看门人，更适合做重规划接口；架构简洁性优先

---

## 4. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| L2 重规划时延 > deadline_s（特别是 MRC_REQUIRED 30 s）| 中 | **高**（M3 触发 ToR；ROC 工作量上升）| L2 团队评估各 reason 类型时延能力；过严 deadline 调整 |
| L2 反规划失败但未回复（超时）| 中 | 中（M3 deadline 后才发现）| ReplanResponseMsg 协议（§2.3）+ 心跳机制 |
| exclusion_zones 多边形格式不一致 | 低 | 低（L2 解析失败）| L3 / L2 双方使用同一 GeoJSON 格式（参考 ENC SUL 标准）|
| 重规划期间 M3 状态切换错乱（与 M4 任务冲突）| 中 | 中（行为短时不一致）| §2.4 时序明确 + REPLAN_WAIT 状态机 |
| M3 多次发起 RouteReplanRequest（短时密集）| 中 | 中（L2 处理压力）| M3 内部限速：reason=同类时间窗 < 30 s 内不重发 |
| L2 重规划结果绕过原 VoyageTask 约束（如违反 mandatory_waypoints）| 低 | 高（任务令逻辑破坏）| L2 重规划须遵守原 VoyageTask 的 mandatory / exclusion 约束；M3 接收后校验 |

---

## 5. 决议项清单

| # | 决议项 | 预期答复方 |
|---|---|---|
| 1 | **L2 是否能接收 RouteReplanRequest** + 按 reason 优先级处理？ | L2 团队 |
| 2 | **L2 重规划时延 SLA**：MRC_REQUIRED 30 s / ODD_EXIT 60 s / MISSION_INFEASIBLE 120 s / CONGESTION 300 s 是否可达？ | L2 团队 |
| 3 | **ReplanResponseMsg 协议**（§2.3）是否需新增到 v1.1.2 §15.1？ | L2 + L3 |
| 4 | **失败反馈机制**：L2 无法重规划时，是否能在 deadline_s × 0.5 内回复 FAILED？ | L2 团队 |
| 5 | **exclusion_zones 格式**：是否使用 GeoJSON / WKT / 自定义？ | L2 + L3 |
| 6 | **重规划期间 M3 状态切换**：M4 是否能继续按现有 PlannedRoute 执行避碰？是否有特殊约束？ | L3（M4 设计师）|
| 7 | **M3 重发限速**：30 s 时间窗是否合理？是否需 reason 特定速率？ | L3 + L2 |
| 8 | **L2 重规划是否保证遵守原 VoyageTask 约束**（mandatory_waypoints / exclusion_zones / eta_window）？ | L2 团队 |

---

## 6. 验收标准

- ✅ L2 团队确认 RouteReplanRequest 可接收 + 按优先级处理
- ✅ L2 重规划时延 SLA 通过 HIL 验证（4 类 reason 各 ≥ 10 个场景）
- ✅ ReplanResponseMsg 协议补充到 v1.1.2 §15.1（如决议）
- ✅ exclusion_zones 格式锁定（双方文档同步）
- ✅ M3 REPLAN_WAIT 状态机实现 + HIL 测试

---

## 7. 时间表

| 里程碑 | 日期 |
|---|---|
| Kick-off | T+1 周 |
| L2 团队评审 | T+1.5 周 |
| 深度对齐会议 | T+2 周（1 小时）|
| 决议签署 | T+2.5 周 |
| HIL 重规划场景验证 | T+8 周 |

---

## 8. 参与方

- **L3 架构师**（M3 设计师）— 提议方
- **L2 架构师**（Voyage Planner）— 接受 / 反对
- **PM** — 时间表协调
- **CCS 验船师**（咨询）— 重规划路径的认证可审计性

---

## 9. 参考

- **v1.1.1 锚点**：§7.2 / §15.1 RouteReplanRequest / §15.2 接口矩阵
- **早期识别**：`docs/Doc From Claude/L3 实现框架裁决汇总.md` §B-01
- **L2 现有设计**：`docs/Init From Zulip/MASS ADAS L2 Voyage Planner/MASS_ADAS_WP_Generator_技术设计文档.md`
- **法规依据**：DNV-CG-0264 §4.5 Deviation from planned route
