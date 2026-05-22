# M8 · Progress · D 任务联动

| 维度 | 说明 |
|---|---|
| 数据更新规则 | PR 合并涉及 M8 时同步更新本表 |
| 最近更新 | 2026-05-22 |
| **Currently Implementing** | D3.4（12 个 SAT-1 topic 已发；SAT-2/3/SOTIF topic 待 DEMO-2 P0）|

| D 任务 | 关系 | 状态 | 详情 |
|---|---|---|---|
| D0.1 | Closed in | ✅ | MUST-7 active_role stub |
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：M8 HMI 裁剪集（豁免动态分配/异常/全局变量，120行/80句） |
| D1.3.2.3 (原 D1.3b.3) | Currently Implementing | 🟡 | Web HMI 部分（MapLibre + foxglove + ToR ≥2s ✅）|
| D3.4 | Currently Implementing | 🟡 | M8 完整 + 7 Web HMI Phase 3 增项；目标 8/24（SAT 桥接子项必须 7/31 前到位，12 个 SAT-1 topic 已发）|
| D2.6 | Closed in | ✅ 2026-05-22 | 船长 HF Ground Truth：(a) 5 访谈模板 + (b) Figma 原型骨架 + (c) 可用性测试报告 + (d) ECDIS 集成草案 + (e) 培训矩阵 + (f) BNWAS 等效设计 + (g) Figma ↔ IEC 62288 对齐 + (h) 透明性问卷/评分表 + (i) §12.3 ground truth 输入（16 文件，20 commits）；[R35-§TBD] 占位待 HF 咨询核实；实现推 D2.8 stub |

## DEMO-2 阻塞贡献

- 🔴 **P0 极高阻塞**：M8 是 Screen 3 双端真空的"生产端"根因；不发 SAT-2/3/SOTIF topic = 前端 Engineer 视图 4 个组件渲染全 null
- 必须 7/31 前从 D3.4 拆出 SAT 桥接 1.5pw 提前完成

---
## 参考 D 任务文档
- D3.4: [Phase 3/D3.4-m8-hmi-transparency/](../../Phase%203/D3.4-m8-hmi-transparency/)（待建）
- D1.3.2.3: [Phase 1/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/](../../Phase%201/D1.3-sil-framework/D1.3.2-scenario-hmi/D1.3.2.3-web-hmi/)
