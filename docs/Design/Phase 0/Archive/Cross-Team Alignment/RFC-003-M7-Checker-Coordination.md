# RFC-003: M7 ↔ X-axis Deterministic Checker（CheckerVetoNotification 协议）

| 属性 | 值 |
|---|---|
| RFC 编号 | SANGO-ADAS-RFC-003 |
| 状态 | 草拟（待评审）|
| 阻塞优先级 | **高** — M7 详细设计启动的硬阻塞 |
| 责任团队 | L3 战术层 + Deterministic Checker 子系统 |
| 关联 finding | F-P0-D3-002 + F-P2-D3-036 + F-NEW-002 |

---

## 1. 背景

### 1.1 v1.1.1 中的相关章节

- §11.7 与 X-axis Deterministic Checker 的协作（v1.1 新增）
- §11.3 SOTIF 假设违反清单（含 "Checker 否决率 > 20% → 升级 SOTIF 告警"）
- §15.1 CheckerVetoNotification IDL（v1.1.1 修订为 enum）

### 1.2 当前设计假设

- v1.0 缺陷（F-P0-D3-002）：v1.0 §11 完全未提 v3.0 X-axis Deterministic Checker
- v1.1 修订：M7（L3 内部 Checker）与 X-axis（系统级 Checker）**分层互补**关系
  - M7 = L3 内部 IEC 61508 + SOTIF 双轨监控
  - X-axis = 系统级 VETO（跨 L2/L3/L4/L5），优先级最高
- v1.1.1 修订（F-NEW-002 关闭）：CheckerVetoNotification 的 veto_reason 改为**受限枚举 enum**（`VetoReasonClass`），保持 Doer-Checker 独立性

### 1.3 跨团队对齐的必要性

须 Deterministic Checker 团队确认：
- L3 X-axis 子项（COLREGs Compliance）已在 Checker 文档中实现
- CheckerVetoNotification 消息可向 M7 推送
- VetoReasonClass enum 6 项分类是否覆盖 X-axis 实际否决场景
- M7 ↔ X-axis 优先级仲裁规则

---

## 2. 提议

### 2.1 接口 IDL（引用 v1.1.1 §15.1 — v1.1.1 修订后）

```
enum VetoReasonClass {
    COLREGS_VIOLATION,    # COLREGs 规则违反（不细分具体 Rule）
    CPA_BELOW_THRESHOLD,  # CPA 低于硬约束
    ENC_CONSTRAINT,       # ENC 约束违反（水深 / 禁区 / TSS）
    ACTUATOR_LIMIT,       # 执行器物理极限违反
    TIMEOUT,              # X-axis 自身超时（保守 VETO）
    OTHER                 # 兜底分类（仅 ASDR 记录用）
}

# CheckerVetoNotification (X-axis Checker → M7, 事件触发)
message CheckerVetoNotification {
    timestamp           stamp;
    string              checker_layer;        # "L2"|"L3"|"L4"|"L5"
    string              vetoed_module;        # 被否决的 Doer 模块名
    VetoReasonClass     veto_reason_class;    # 受限枚举（M7 解析）
    string              veto_reason_detail;   # 详细描述（仅 ASDR 用，M7 不解析）
    bool                fallback_provided;    # X-axis 是否提供 nearest_compliant
}
```

### 2.2 协调协议（v1.1.1 §11.7）

| 维度 | M7 Safety Supervisor | X-axis Deterministic Checker |
|---|---|---|
| **层级** | L3 内部 | 系统级（跨 L2/L3/L4/L5）|
| **类型** | IEC 61508 + SOTIF | 确定性规则 VETO |
| **输出** | Safety_AlertMsg + recommended_mrm | CheckerOutput { approved, nearest_compliant } |
| **优先级** | 中等（向 M1 报告，M1 仲裁）| 最高（直接 VETO Doer 输出）|

**双向消息流**：
1. **X-axis → M7**：CheckerVetoNotification（事件触发）
   - M7 按 enum 分类做统计聚合（Checker 否决率 > 20% / 100 周期 → SOTIF 告警升级）
   - **M7 不重新推理 X-axis 的具体规则违反内容**（保持独立性）
2. **M7 → X-axis**：无（M7 不会向 X-axis 发送指令；M7 通过 M1 仲裁后下发 MRM）

### 2.3 Doer-Checker 独立性（决策四 §2.5）

- **共享词汇**：VetoReasonClass enum 是受限分类词汇表（类似 Boeing 777 Monitor 与 PFC 共享 "airspeed envelope" 术语）
- **不共享**：代码 / 库 / 数据结构 / 失效模式
- **形式化验证**：M7 处理 CheckerVetoNotification 的逻辑 = enum 计数器 + 阈值比较，复杂度远低于 100:1 比例

---

## 3. 备选方案

### 3.1 方案 A：veto_reason 自由文本（v1.1 原始 — 已弃用）

- **方案**：veto_reason 是 `string`，M7 解析具体规则违反内容
- **弃用理由**（DNV 验证官评审）：
  - M7 解析自由文本 → 难以形式化验证 → DNV 审查质疑 Checker 简化原则
  - M7 实质上重复 X-axis 的规则推理 → 违反独立性

### 3.2 方案 C：X-axis 直接 VETO，不通知 M7（已弃用）

- **方案**：X-axis 否决时仅通知 M5（被否决模块），不通知 M7
- **弃用理由**：
  - M7 SOTIF 假设违反检测无法纳入"Checker 否决率"指标（早期 TDCAL spec §3.4 已识别）
  - 系统层缺失关键监控信号

---

## 4. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| **X-axis 团队拒绝 enum 化**（认为限制了 VETO 表达力）| 中 | 高（CheckerVetoNotification 设计回退）| 提供 6 项 enum 已覆盖 95%+ X-axis 否决场景的统计分析；OTHER 分类作为兜底 |
| Checker 否决率 20% 阈值不合理 | 高 | 中（M7 SOTIF 误报或漏报）| 阈值标 [HAZID 校准]（附录 E.3）；FCB 试航后调整 |
| X-axis 自身超时（TIMEOUT enum）频次过高 | 中 | 中（M7 SOTIF 大量误报）| X-axis 内部 timeout 设计须充分；M7 对 TIMEOUT 类别单独阈值 |
| 多层 Checker（L2 + L3 + L4 + L5）的否决在 M7 端如何聚合 | 中 | 中（M7 状态空间膨胀）| 提议：M7 只监控 L3 Checker 否决率；其他层由系统层 ASDR 单独聚合 |
| M7 接收频率超过预期（X-axis 频繁否决）| 低 | 中（M7 处理能力）| CheckerVetoNotification 是事件触发，非周期；M7 限制处理速率 |

---

## 5. 决议项清单

| # | 决议项 | 预期答复方 |
|---|---|---|
| 1 | **X-axis 是否能产生 CheckerVetoNotification 消息**？是否能按 v1.1.1 §15.1 IDL 实现？ | Deterministic Checker |
| 2 | **VetoReasonClass enum 6 项分类**是否覆盖 X-axis 实际否决场景？是否需补充？ | Deterministic Checker |
| 3 | **veto_reason_detail 自由文本** 是否会泄漏 X-axis 内部实现？是否需脱敏？ | Deterministic Checker + L3（M7 设计师）|
| 4 | **M7 监控 L3 Checker 否决率** 的具体定义：100 周期窗口（10–20 s）是否合理？ | L3（M7 设计师）|
| 5 | **Checker 否决率 20% 阈值**：HAZID 校准前的初始值是否合理？ | L3 + Deterministic Checker |
| 6 | **X-axis 与 M7 的优先级冲突**：若 X-axis 否决 M5 同时 M7 触发 MRM，谁优先？（提议：X-axis VETO 优先；M7 MRM 后置）| L3 + Deterministic Checker |
| 7 | **CheckerVetoNotification 在 ASDR 中的记录格式**：是否与 v1.1.1 §15.1 ASDR_RecordMsg 一致？ | L3 + ASDR + Checker |
| 8 | **X-axis Checker 子项（L2/L3/L4/L5）独立性**：L3 Checker 是否真的与 L2/L4/L5 Checker 物理隔离？ | Deterministic Checker |

---

## 6. 验收标准

- ✅ Deterministic Checker 团队确认 enum 化 CheckerVetoNotification 可实现
- ✅ 6 项 VetoReasonClass 覆盖 ≥ 95% 实际场景
- ✅ M7 ↔ X-axis 优先级仲裁规则锁定（提议：X-axis VETO 直接生效；M7 MRM 通过 M1 仲裁后置）
- ✅ HIL 测试：100 个 X-axis 否决场景 → M7 SOTIF 告警阈值统计验证

---

## 7. 时间表

| 里程碑 | 日期 |
|---|---|
| Kick-off | T+1 周 |
| Checker 团队评审 | T+1.5 周 |
| 深度对齐会议 | T+2 周（1.5 小时）|
| 决议签署 | T+2.5 周 |
| HIL 100 场景验证 | T+8 周 |

---

## 8. 参与方

- **L3 架构师**（M7 设计师）— 提议方
- **Deterministic Checker 团队架构师** — 接受 / 反对
- **CCS 验船师**（咨询）— Checker 独立性合规审查
- **DNV 验证官**（咨询）— enum 化方案的 SIL 形式化验证

---

## 9. 参考

- **v1.1.1 锚点**：§11.2 / §11.3 / §11.6 / §11.7 / §15.1 CheckerVetoNotification
- **学术参考**：[R12] Jackson 2021 Certified Control / [R4] Boeing 777 PFCS / Simplex Architecture
- **工业参考**：DNV-CG-0264 §9.3 监控系统独立性 / IEC 61508-2 Table 3
- **Checker 现有设计**：`docs/Init From Zulip/MASS ADAS Deterministic Checker/MASS_ADAS_Deterministic_Checker_技术设计文档.md`
