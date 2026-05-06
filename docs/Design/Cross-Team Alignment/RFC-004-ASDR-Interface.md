# RFC-004: L3 → ASDR（决策追溯日志 IDL）

| 属性 | 值 |
|---|---|
| RFC 编号 | SANGO-ADAS-RFC-004 |
| 状态 | 草拟（待评审）|
| 阻塞优先级 | 低 |
| 责任团队 | L3 战术层 + ASDR 子系统 |
| 关联 finding | F-P1-D4-033 |

---

## 1. 背景

### 1.1 v1.1.1 中的相关章节

- §15.1 ASDR_RecordMsg IDL（v1.1.1 完整定义）
- §15.2 接口矩阵第 19 行（M1, M2, M4, M6, M7 → ASDR）
- §11.9.1 接管期间 M7 降级告警的 ASDR 标记
- §12.4.1 ToR 交互验证的 ASDR 完整 schema（含 `tor_acknowledgment_clicked`）

### 1.2 当前设计假设

- v1.0 缺陷：§15.2 完全无 ASDR 接口
- v1.1 修订：§15.1 新增 ASDR_RecordMsg IDL；§15.2 新增 5 类发布者（M1, M2, M4, M6, M7）→ ASDR @ 事件 + 2 Hz
- ASDR 现有设计文档（`docs/Init From Zulip/MASS ADAS ASDR/`）已定义订阅 L3 多模块 + JSON schema `AiDecisionRecord`

### 1.3 跨团队对齐的必要性

ASDR 是 CCS i-Ship 白盒可审计的核心证据载体（DNV-CG-0264 §10 Alert management 依据）。须 ASDR 团队确认：
- ASDR_RecordMsg IDL 字段是否完整覆盖 CCS / DNV 审计需求
- L3 各模块发送频率是否在 ASDR 处理能力范围内
- SHA-256 签名机制是否实现
- 与 ToR / Override / SOTIF 等关键事件的同步策略

---

## 2. 提议

### 2.1 接口 IDL（引用 v1.1.1 §15.1 — **不变**）

```
# ASDR_RecordMsg (所有 L3 模块 → ASDR, 频率: 事件 + 2 Hz)
message ASDR_RecordMsg {
    timestamp     stamp;
    string        source_module;    # "M1_ODD_Manager"|"M2_World_Model"|"M4_Behavior_Arbiter"|"M6_COLREGs"|"M7_Safety_Supervisor"|...
    string        decision_type;    # "encounter_classification"|"avoid_wp"|"cpa_alert"|"checker_veto"|"sotif_alert"|"mrm_triggered"|"tor_acknowledgment_clicked"|...
    string        decision_json;    # JSON 序列化（兼容 ASDR §5.2 AiDecisionRecord schema）
    bytes         signature;        # SHA-256 防篡改
}
```

### 2.2 关键事件清单（强制记录）

| 模块 | 事件类型（decision_type）| 触发频率 | 必备字段 |
|---|---|---|---|
| M1 | `odd_state_transition` | 事件（IN→EDGE/OUT）+ 2 Hz 周期快照 | from_zone / to_zone / conformance_score / reason |
| M1 | `mrm_triggered` | 事件 | mrm_index / source_alert / context |
| M1 | `override_active` | 事件 | activation_source / pre_override_state |
| M1 | `reflex_activation` | 事件 | trigger_time / cpa_at_trigger / sensor_source |
| M2 | `world_state_update` | 2 Hz | targets_count / cpa_min / tcpa_min / preliminary_role_summary |
| M2 | `target_classification_change` | 事件 | target_id / from_role / to_role / heading_diff |
| M4 | `behavior_transition` | 事件 | from_behavior / to_behavior / ivp_summary |
| M4 | `behavior_plan_published` | 2 Hz | behavior / heading_min/max / speed_min/max / rationale |
| M6 | `colregs_role_assignment` | 事件 | target_id / role / rule_basis / phase |
| M6 | `colregs_constraint_published` | 2 Hz | active_rules / phase / constraint_summary |
| M7 | `sotif_alert` | 事件 | assumption / violated_metric / threshold / actual_value |
| M7 | `iec61508_fault` | 事件 | fault_type / module / detection_method |
| M7 | `mrm_recommended` | 事件 | mrm_index / trigger_alert / m1_decision |
| M7 | `checker_veto_received` | 事件 | veto_reason_class / vetoed_module / counter |
| M8 | `tor_request_pushed` | 事件 | reason / deadline_s / target_level |
| M8 | `tor_acknowledgment_clicked` | 事件 | sat1_display_duration_s / threats_visible / odd_zone_at_click / conformance_score_at_click（详见 v1.1.1 §12.4.1）|
| M8 | `sat2_triggered` | 事件 | trigger_source（COLREGs/M7/operator）|

### 2.3 频率与时延 SLA

| 项 | 要求 |
|---|---|
| **事件触发延迟**（决策发生 → ASDR 接收）| < 100 ms |
| **2 Hz 周期发布抖动** | < 50 ms |
| **签名计算时延**（M{n} 端）| < 10 ms（SHA-256 + 32-byte payload）|
| **ASDR 接收 + 持久化时延** | < 200 ms（避免 L3 模块发布阻塞）|
| **吞吐量（峰值）**| ≥ 50 events/s（多船密集场景）|

---

## 3. 备选方案

### 3.1 方案 A：每模块独立 IDL（已弃用）

- **方案**：M1 / M2 / M4 / M6 / M7 各自定义专用 ASDR IDL
- **弃用理由**：ASDR 端处理逻辑膨胀；无法统一 SHA-256 签名 + JSON schema 校验

### 3.2 方案 B：单一 JSON 字段无 schema（已弃用）

- **方案**：ASDR_RecordMsg.decision_json 完全自由格式
- **弃用理由**：CCS 审查无法验证决策日志完整性；ASDR §5.2 已定义 AiDecisionRecord schema，本 RFC 须遵循

---

## 4. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 高频事件（2 Hz × 5 模块 = 10 events/s 持续 + 突发事件）超出 ASDR 处理能力 | 中 | 中（事件丢失）| ASDR 端 buffer + 优先级丢弃（保 SOTIF / MRM / ToR；丢周期快照）|
| SHA-256 签名计算阻塞 M{n} 实时性 | 低 | 中（M2/M4/M6 周期延误）| 签名异步线程；非关键路径 |
| decision_json schema 漂移（M{n} 实现与 ASDR 端不一致）| 高 | 中（ASDR 解析失败）| 强制 v1.1.1 §15.1 + ASDR §5.2 双方文档锁定；CI 校验 schema 兼容 |
| ToR `tor_acknowledgment_clicked` 字段在 ASDR 端缺失（v1.1.1 §12.4.1 新定义）| 中 | **高**（IMO 法律责任无证据）| 本 RFC 强制 ASDR 端实现 + 提供 ROC 操作员审计回放 |

---

## 5. 决议项清单

| # | 决议项 | 预期答复方 |
|---|---|---|
| 1 | **ASDR 端是否能接收 ASDR_RecordMsg**？是否符合 v1.1.1 §15.1 IDL？ | ASDR 团队 |
| 2 | **AiDecisionRecord JSON schema** 是否兼容 v1.1.1 §15.1 decision_json？是否需修订？ | ASDR + L3 |
| 3 | **`tor_acknowledgment_clicked` 事件 schema**（v1.1.1 §12.4.1 + §11.9 ToR 协议）是否在 ASDR 端可解析 / 回放？ | ASDR + L3（M8 设计师）|
| 4 | **签名机制**：SHA-256 是否足够？是否需 HMAC（防 ROC 端伪造）？ | ASDR + Cybersec 团队 |
| 5 | **吞吐量峰值 50 events/s** 是否在 ASDR 处理能力范围？是否需限流？ | ASDR 团队 |
| 6 | **M5 / M3 → ASDR**：v1.1.1 §15.2 矩阵中未列 M3 / M5；是否需补充？（M3 重规划请求 / M5 AvoidancePlan 也是关键决策日志）| L3 + ASDR |
| 7 | **ASDR 持久化策略**：本地 vs Shore Link 同步频率？回放工具是否支持时间窗查询？ | ASDR 团队 |
| 8 | **CCS 审计接口**：ASDR → CCS 验船师的审计回放接口是否存在？格式？ | ASDR + CCS 验船师 |

---

## 6. 验收标准

- ✅ ASDR 团队确认 v1.1.1 §15.1 IDL 可接收
- ✅ AiDecisionRecord schema 在 ASDR §5.2 + v1.1.1 §15.1 双方文档锁定
- ✅ `tor_acknowledgment_clicked` 事件可被 ASDR 完整记录 + 回放
- ✅ M5 / M3 接口补充（如决议）
- ✅ 吞吐量 HIL 测试通过（50 events/s × 1 hour 无丢失）
- ✅ 签名验证机制实现（CCS 审计端可独立校验）

---

## 7. 时间表

| 里程碑 | 日期 |
|---|---|
| Kick-off | T+1 周 |
| ASDR 团队评审 | T+1.5 周 |
| 简短对齐会议 | T+2 周（0.5 小时 — 最简单 RFC）|
| 决议签署 | T+2.5 周 |
| 吞吐量 HIL 测试 | T+6 周 |

---

## 8. 参与方

- **L3 架构师**（提议方 — 含 M1/M2/M4/M6/M7/M8 详细设计师）
- **ASDR 团队架构师** — 接受 / 反对
- **Cybersec 团队**（咨询）— 签名 / 数据完整性
- **CCS 验船师**（咨询）— 审计接口需求

---

## 9. 参考

- **v1.1.1 锚点**：§15.1 ASDR_RecordMsg / §15.2 矩阵 / §12.4.1 / §11.9.1
- **ASDR 现有设计**：`docs/Init From Zulip/MASS ADAS ASDR/MASS_ADAS_ASDR_技术设计文档.md`（§3.2 + §5.2 AiDecisionRecord JSON schema + §13 协作关系）
- **法规依据**：DNV-CG-0264 §10 Alert management；CCS《智能船舶规范》白盒可审计要求
