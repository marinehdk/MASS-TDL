# M2 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M2 时同步更新本表 |
| 最近更新 | 2026-05-21 |
| **Currently Implementing** | —（D2.2 已关闭）|

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-1（OVERTAKING [112.5°, 247.5°]）+ MUST-6（sog 校验改读 Manifest）|
| D1.3.2.3 | Closed in | ✅ 2026-05-20 | Web HMI：CPA/TCPA 真发布到 /sil/cpa_tcpa（foxglove_bridge 消费端落地）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：全模块适用 |
| D2.2 | Closed in | ✅ 2026-05-21 | v3.0 修订完整化：UKF 协方差链 / ENC 端到端集成 / intent_confidence / BRG-RNG / env sanity / MUST-1+6 验证固化 / Python ENC 测试。14 提交，~900 LOC C++ + 18 Python tests。|
| D2.5 | Blocks | ⏳ | SIL 集成（M1-M6）依赖 M2 50Hz 真实输出（已在） + intent_distribution（缺）|

## DEMO-2 阻塞贡献

- 🟢 不阻塞：BRG/RNG 已落地（TrackedTarget.msg 字段已追加），ARPA 表解除硬编码
- 🟢 不阻塞：intent_confidence 字段已落地（B P1-B-02 决议闭环）
- 🟢 不阻塞：M2 主功能 CPA/TCPA 已可用，UKF 协方差链已落地

---
## 参考 D 任务文档
- D2.2: [Phase 2/D2.2-m2-world-model-enc/](../../Phase%202/D2.2-m2-world-model-enc/)（spec + plan）
- D1.3.2.3: [Phase 1/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/](../../Phase%201/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/)
