# RFC 决议归档（跨团队接口锁定证据）

> **目的**：记录 6 个 RFC 的跨团队评审结果。所有 RFC 状态从"草拟（待评审）"升级为"已批准（接口锁定）"，为后续详细设计 / 实现 / CCS i-Ship 入级提供可审计证据链。
>
> **决议日期**：2026-05-06
> **架构基线**：v1.1.1 → 引发 v1.1.2 接口字段补充
> **后续**：v1.1.2 patch 同步合并；M5/M7 详细设计 status 升至"正式"

---

## 决议汇总

| RFC | 主题 | 责任团队 | 决议状态 | 引发 v1.1.x patch |
|---|---|---|---|---|
| RFC-001 | M5 → L4 接口（方案 B AvoidancePlan + ReactiveOverrideCmd）| L3 + L4 Guidance | ✅ **已批准** | 无（v1.1.1 IDL 不变）|
| RFC-002 | M2 ← Multimodal Fusion（三话题聚合）| L3 + Multimodal Fusion | ✅ **已批准** | 无 |
| RFC-003 | M7 ↔ X-axis Checker（CheckerVetoNotification enum）| L3 + Deterministic Checker | ✅ **已批准** | 无 |
| RFC-004 | L3 → ASDR（决策追溯日志）| L3 + ASDR | ✅ **已批准** | 无 |
| RFC-005 | Y-axis Reflex Arc spec 量化 | L3 + Reflex Arc + Fusion + L5 + Override | ✅ **已批准** | 无（触发阈值仍标 [HAZID 校准]） |
| RFC-006 | M3 → L2 反向 RouteReplanRequest | L3 + L2 Voyage Planner | ✅ **已批准 + 协议增补** | **v1.1.2 §15.1 新增 ReplanResponseMsg** |

---

## 详细决议

### RFC-001 决议（M5 → L4 接口）

**接受方案 B**（AvoidancePlan + ReactiveOverrideCmd 双接口）

| 决议项（来自 RFC-001 §5）| 决议结果 |
|---|---|
| 1. L4 三模式切换支持 | ✅ L4 团队承诺改造（normal_LOS / avoidance_planning / reactive_override）|
| 2. 三模式实时性能（< 100 ms / < 50 ms）| ✅ L4 HIL 初步验证可达 |
| 3. AvoidancePlanMsg IDL 字段 | ✅ v1.1.1 §15.1 现有定义足够，无需补充 |
| 4. ReactiveOverrideCmd validity_s 1–3 s | ✅ 接受；BC-MPC 周期可保证刷新 |
| 5. 超时退化协议 | ✅ M5 失联 → L4 退化到 normal_LOS + 通知 M1 |
| 6. L4 → L5 三模式输出格式一致 | ✅ 仍是 (ψ, u, ROT) 三元组 |
| 7. L4 改造工作量 | ✅ 估算 6 周（含 PoC 4 周 + HIL 验证 2 周） |

**实现承诺时间表**：
- T+1 周：L4 改造 PoC 启动
- T+5 周：L4 三模式实现完成
- T+8 周：HIL 端到端验证通过

### RFC-002 决议（M2 ← Multimodal Fusion）

| 决议项 | 决议结果 |
|---|---|
| 1. 三话题输出频率稳定性 | ✅ Fusion 承诺 ≥ 95% 帧到达率（最坏情况）|
| 2. TrackedTarget.confidence 范围 | ✅ [0, 1]，含义按 Fusion §2.2 文档 |
| 3. FilteredOwnShipState (u, v) 含海流估计 | ❌ **不含**；M2 须从 current_speed/direction 自行减去（v1.1.1 §6.4 已说明）|
| 4. Fusion 健康度信号 | ✅ Fusion 添加 `nav_mode` 字段（OPTIMAL/DR_SHORT/DR_LONG/DEGRADED）|
| 5. M2 计算 CPA/TCPA 时间对齐 | ✅ 双方一致：M2 自行处理（取最近 own_ship 快照）|
| 6. EnvironmentState 0.2 Hz 频率 | ✅ 足够（M5 TSS Rule 10 多边形约束更新典型 5 s）|

### RFC-003 决议（M7 ↔ X-axis Checker）

**接受 CheckerVetoNotification enum 化方案**（保持 Doer-Checker 独立性）

| 决议项 | 决议结果 |
|---|---|
| 1. X-axis 产生 CheckerVetoNotification | ✅ Checker 团队承诺实现 |
| 2. VetoReasonClass 6 项 enum 覆盖 | ✅ 接受（COLREGS_VIOLATION / CPA_BELOW_THRESHOLD / ENC_CONSTRAINT / ACTUATOR_LIMIT / TIMEOUT / OTHER）|
| 3. veto_reason_detail 自由文本脱敏 | ✅ 不脱敏（仅 ASDR 内部使用，不外发）|
| 4. M7 监控 100 周期窗口 = 多长时间 | ✅ 锁定 = **15 s**（M7 周期约 6.7 Hz × 100 = 15 s 滑窗）|
| 5. Checker 否决率 20% 阈值 | ⚠️ **保留 [HAZID 校准]**（FCB 实船试航后调整）|
| 6. M7 ↔ X-axis 优先级冲突 | ✅ X-axis VETO 直接生效（最高）；M7 MRM 通过 M1 仲裁后置 |
| 7. ASDR 记录格式 | ✅ 与 v1.1.1 §15.1 ASDR_RecordMsg 一致（含 `checker_veto_received` decision_type）|
| 8. X-axis L2/L3/L4/L5 子项独立性 | ✅ Checker 团队确认物理隔离 |

### RFC-004 决议（L3 → ASDR）

| 决议项 | 决议结果 |
|---|---|
| 1. ASDR 接收 ASDR_RecordMsg | ✅ ASDR 团队承诺实现 v1.1.1 §15.1 IDL |
| 2. AiDecisionRecord JSON schema 兼容性 | ✅ ASDR §5.2 与 v1.1.1 §15.1 锁定（无字段差异）|
| 3. tor_acknowledgment_clicked 事件 | ✅ ASDR 端可解析 + 回放 |
| 4. SHA-256 签名机制 | ✅ 足够（CCS 审计端独立校验）；HMAC 留 v1.2 安全升级 |
| 5. 吞吐量 50 events/s | ✅ ASDR 处理能力可达（峰值场景已 HIL 验证）|
| 6. **M5 / M3 → ASDR 接口补充** | ✅ **接受**：v1.1.2 §15.2 矩阵增补 M3 + M5 → ASDR 行（事件 + 2 Hz）|
| 7. 持久化策略 | ✅ 本地 + Shore Link 同步（5 min 周期）|
| 8. CCS 审计回放接口 | ✅ ASDR 提供 REST API（时间窗查询）|

### RFC-005 决议（Y-axis Reflex Arc spec 量化）

**最复杂 RFC — 多团队联合接受**

| 决议项 | 决议结果 |
|---|---|
| 1. 触发阈值初始值 | ✅ 接受 v1.1.1 提议（CPA < 50 m / range < 100 m / TTC < 3 s）；**[HAZID 校准]** |
| 2. 多重触发 voting | ✅ 任一条件即触发；多目标聚合升级 HARD_TURN |
| 3. `/perception/proximity_emergency` 50 Hz 话题 | ✅ Multimodal Fusion 团队承诺实现（仅在距离 < 200 m 内启用）|
| 4. EmergencyCommand action 枚举 | ✅ STOP / REVERSE / HARD_TURN 足够 |
| 5. L5 端到端时延 < 500 ms | ✅ L5 团队承诺可达（DDS 优先级 + 硬件预留）|
| 6. 优先级仲裁 | ✅ Hardware Override > Reflex Arc > M5 ReactiveOverride > M5 AvoidancePlan > L4 LOS |
| 7. L3 内部冻结时延 | ✅ M1 接收 ReflexActivationNotification → L3 全部 OVERRIDDEN < 50 ms（并行广播）|
| 8. Reflex Arc 心跳监控 | ✅ X-axis Checker 监控（最高独立性）|
| 9. SIL 评估 | ✅ Reflex Arc 评定 **SIL 3**（最高安全等级）|
| 10. HAZID 校准计划 | ✅ 纳入 HAZID 工作组（详见 `HAZID/`）|

### RFC-006 决议（M3 → L2 反向 RouteReplanRequest）

**接受 + 协议增补 — 引发 v1.1.2 patch**

| 决议项 | 决议结果 |
|---|---|
| 1. L2 接收 RouteReplanRequest | ✅ L2 团队承诺实现 |
| 2. L2 重规划时延 SLA | ✅ MRC_REQUIRED 30s / ODD_EXIT 60s / MISSION_INFEASIBLE 120s / CONGESTION 300s 可达 |
| 3. **ReplanResponseMsg 协议** | ✅ **接受 — 引发 v1.1.2 §15.1 新增** |
| 4. L2 失败回复 deadline_s × 0.5 | ✅ 可达 |
| 5. exclusion_zones 格式 | ✅ **GeoJSON Polygon**（与 ENC SUL 兼容）|
| 6. 重规划期间 M4 行为 | ✅ M4 按现有 PlannedRoute 继续（M3 通知）|
| 7. M3 重发限速 30 s | ✅ 接受 |
| 8. L2 遵守原 VoyageTask 约束 | ✅ L2 承诺 + M3 接收后校验 |

---

## v1.1.1 → v1.1.2 Patch List（RFC 决议触发）

仅 RFC-006 引发 IDL 变更：

### Patch v1.1.2-01：§15.1 新增 ReplanResponseMsg IDL

```
# ReplanResponseMsg (L2 Voyage Planner → M3, 事件)
# v1.1.2 新增 — RFC-006 §2.3 决议
message ReplanResponseMsg {
    timestamp    stamp;
    enum status; # SUCCESS | FAILED_TIMEOUT | FAILED_INFEASIBLE | FAILED_NO_RESOURCES
    string       failure_reason;        # 失败时的诊断信息
    bool         retry_recommended;     # 是否建议 M3 在新条件下重试
}
```

### Patch v1.1.2-02：§15.2 接口矩阵增加

```
| **L2 Voyage Planner → M3** | ReplanResponseMsg | 事件 | 重规划响应（SUCCESS / FAILED） **[v1.1.2 RFC-006]** |
| **M3, M5 → ASDR** | ASDR_RecordMsg | 事件 + 2 Hz | M3/M5 决策追溯日志 **[v1.1.2 RFC-004]** |
```

### Patch v1.1.2-03：§7 M3 状态机更新

`REPLAN_WAIT` 状态下增加对 `ReplanResponseMsg` 的处理（成功 → ACTIVE / 失败 → ToR_RequestMsg）

---

## 跨团队改造工作量汇总

| 团队 | 工作量（人月）| 关键里程碑 |
|---|---|---|
| L4 Guidance | 6 周 | T+5 周三模式 PoC；T+8 周 HIL 验证 |
| Multimodal Fusion | 4 周 | T+4 周 nav_mode 字段 + `/perception/proximity_emergency` 50 Hz |
| Deterministic Checker | 3 周 | T+3 周 CheckerVetoNotification enum 实现 |
| ASDR | 3 周 | T+3 周 IDL 实现 + REST 审计接口 |
| Reflex Arc | 6 周 | T+6 周触发逻辑实现（依赖 HAZID 校准）|
| L5 Control | 2 周 | T+2 周 EmergencyCommand 优先级处理 |
| Hardware Override Arbiter | 2 周 | T+2 周 OverrideActiveSignal 接口 |
| L1 Voyage Order | 1 周 | T+1 周 VoyageTask schema 确认 |
| L2 Voyage Planner | 4 周 | T+4 周 RouteReplanRequest + ReplanResponseMsg 实现 |

**总人月**：约 31 人月（多团队并行，关键路径 8 周）

---

## CCS / DNV 评审通过状态

- ✅ Doer-Checker 独立性（决策四 + ADR-001）通过 enum 化方案保留
- ✅ Reflex Arc SIL 3 + 形式化验证（状态机 + voting）
- ✅ §15 接口契约总表完整闭包（21 行 + v1.1.2 增 2 行）
- ✅ ASDR 决策追溯链通过（CCS 白盒可审计）

---

## 后续动作

1. ✅ **v1.1.2 patch**：将 ReplanResponseMsg + ASDR 矩阵更新合并到架构文档
2. ✅ **M5 / M7 详细设计**：状态从"草稿"升至"正式"
3. ⏳ **跨团队实现启动**：各团队按改造时间表开工
4. ⏳ **HAZID 启动**：详见 `docs/Design/HAZID/RUN-001-kickoff.md`
5. ⏳ **CCS AIP 申请准备**：HAZID 校准 + 实现 PoC 完成后启动（预计 T+14–16 周）
