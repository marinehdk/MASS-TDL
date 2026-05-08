# RFC-007 — M7 Safety Supervisor ↔ X-axis Deterministic Checker: Heartbeat Contract

| 字段 | 值 |
|---|---|
| RFC ID | RFC-007 |
| 版本 | v0.1（草稿，待 5/13 跨团队评审） |
| 起草日 | 2026-05-08 |
| 起草方 | L3 架构师-hat |
| 状态 | Pending — 5/13 X-axis 团队 + Cyber 团队评审 |
| 关联 Finding | F P0-F-05（7 角度评审 2026-05-07）；MUST-11 M7-core/M7-sotif 拆分 |
| 关联 D-task | D0.2（草稿）/ D3.9（落地实装）|

---

## §1 背景与动机

M7 Safety Supervisor 是 Doer-Checker 架构中唯一的 Checker 实体（IEC 61508 SIL 2）。X-axis Deterministic Checker 对 M1–M6 Doer 决策具有 VETO 权。两者的存活性互相依赖：

- M7 故障时，X-axis 需要知道并降级或触发安全停车
- X-axis 故障时，M7 需要知道并报告给 M8（SAT-3 透明性）和 Shore Link

当前架构 v1.1.2 §11.3 定义了 VETO 语义，但未定义心跳协议格式、周期、超时分级。本 RFC 填补此缺口。

**置信度说明**：§3 超时阈值参考 IEC 61508-2 §7.4.7 watchdog 设计原则（🟡 — 待 5/13 评审验证后升级）。

---

## §2 心跳消息格式 (HeartbeatMsg)

DDS Topic：
- `/l3/m7/heartbeat`（M7 → X-axis，20 Hz）
- `/l3/xaxis/heartbeat`（X-axis → M7，20 Hz）

```idl
struct HeartbeatMsg {
    builtin_interfaces::msg::Time stamp;   // ROS2 时间戳
    uint32 seq;                            // 单调递增序号（reset on restart = gap detection）
    string liveness_token;                 // SHA-256(seq ‖ salt) — IACS UR E27 防重放
    uint8 internal_health;                 // 0=HEALTHY, 1=DEGRADED, 2=CRITICAL
    string rationale;                      // 内部状态简述（≤64 chars）
    float32 confidence;                    // [0,1] — 组件自评置信度
};
```

QoS：`RELIABLE`, `KEEP_LAST(1)`, `DEADLINE(50ms)`

---

## §3 心跳周期候选与推荐

| 候选 | 周期 | 优点 | 风险 |
|---|---|---|---|
| A | 10 Hz（100ms）| 与 M7 主循环对齐；DDS DEADLINE 50ms 可检测单次丢包 | 漏检概率略高 |
| B | 20 Hz（50ms）| 细粒度检测；满足 IEC 61508 §7.4.7 watchdog 最小采样要求 🟡 | 需评估 DDS QoS budget |

**推荐**：候选 B（20 Hz），待 5/13 X-axis 团队确认 DDS budget 可行性后锁定。

---

## §4 超时分级语义

| 级别 | 超时阈值 | M7 行为 | X-axis 行为 |
|---|---|---|---|
| WARNING | 50 ms（1 心跳周期） | 记录 + SAT-3 透明性上报 | 记录 |
| DEGRADED | 200 ms（4 周期） | 触发 `m7_health_degraded` 事件 → M8；Shore Link 告警 | 可选 VETO 暂停（待 X-axis 定义）|
| SAFETY_STOP | 500 ms（10 周期）| 触发 MRM + Shore Link 紧急停车请求 🟡 | VETO 全部 Doer 决策 |

注：500 ms SAFETY_STOP 阈值参考 IEC 61508-2 §7.4.7 watchdog 超时设计原则（🟡，待评审）。

---

## §5 故障语义与升级路径

**M7 超时（X-axis 未收到 M7 心跳 ≥500ms）**：
1. X-axis VETO 所有 L3 Doer 输出
2. X-axis 向 ASDR 上报 `m7_watchdog_timeout` 事件
3. L3 进入 MRM-01（保向减速）或 MRM-02（就地停车），由 X-axis 根据 ODD-D 判定

**X-axis 超时（M7 未收到 X-axis 心跳 ≥500ms）**：
1. M7 继续运行但标记 `checker_unavailable=true`
2. M7 向 M8 推 SAT-3 `checker_unavailable` 状态
3. M8 向 ROC 推送接管请求（ToR，TMR≥60s 窗口开始）

---

## §6 IACS UR E26/E27 合规说明

IACS UR E26（On-Board Computers and Computer Networks）+ E27 要求：
- 心跳报文在 OT VLAN 内传输，不经过 IT 网络
- `liveness_token` = SHA-256(seq ‖ salt) 防止重放攻击（UR E27 §3.4）
- `RELIABLE` QoS 确保丢包可检测（区别于 BEST_EFFORT）
- VLAN 拓扑与 Data Diode 位置由 Cyber 团队（RFC-007 联署方）定义

---

## §7 5/13 评审待答问题

1. **X-axis 团队**：20 Hz 心跳是否超过 DDS QoS budget？若是，降至 10 Hz 对 SAFETY_STOP 检测的影响？
2. **Cyber 团队**：`liveness_token` salt 轮换策略（per-session vs per-message）？与 IACS UR E27 §3.4 的具体映射？
3. **M7-hat**（D3.7 实装时）：`internal_health` 字段是否需扩展到 8 值以区分失效模式？
4. **X-axis 团队**：DEGRADED 级 VETO（200ms）是 X-axis 自主决定，还是需要 M7 显式触发？

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-08 | 初稿；D0.2 产出；待 5/13 评审 |
