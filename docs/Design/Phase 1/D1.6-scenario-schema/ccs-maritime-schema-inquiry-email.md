# CCS maritime-schema 接受度查询邮件（模板）

**状态**：D1.6 产出模板 | **实际发送**：D1.8 由 PM + CCS 联络人执行

---

**收件人**：CCS 技术中心 [TBD-CCS-contact] / Brinav 联络人 [TBD-Brinav-contact]  
**抄送**：MASS ADAS L3 项目组  
**主题**：MASS ADAS L3 项目 — DNV maritime-schema v0.2.x 作 SIL 场景 evidence container 接受度查询

---

尊敬的 [CCS 技术中心 / Brinav 联络人]：

MASS ADAS L3 Tactical Decision Layer 项目（以下简称"本项目"）目前处于 Phase 1 工程基础建设阶段，目标 2026-11 提交 CCS i-Ship (Nx, Ri/Ai) AIP 申请。

### 背景

本项目 SIL（Software-in-the-Loop）仿真测试的场景数据采用 **DNV maritime-schema v0.2.x `TrafficSituation` 扩展格式** 作为场景 YAML 的权威格式。选择理由如下：

1. **互操作性**：maritime-schema 是 DNV 开源的船舶交通场景标准格式，已在 NTNU colav-simulator、DNV HIL 平台中使用。使用社区标准避免内部格式与外部工具链的格式转换开销。
2. **工具链贯通**：maritime-schema 原生集成 DNV farn（case folder generator）和 ospx（OSP 系统结构 author），形成从场景定义到仿真执行的完整工具链。
3. **国际先例**：CCS-DNV-Brinav 2024 MoU + Brinav Armada 78 03 案例已使用 maritime-schema 作 evidence container（证据 [R25]）。

### 查询事项

请确认以下事项：

1. **CCS i-Ship AIP 审查中，maritime-schema v0.2.x `TrafficSituation` 格式的场景数据是否可接受作为 SIL 验证证据的 evidence container？**
2. **如 CCS 要求中文专用格式，maritime-schema 退为内部表示 + 加导出器（本项目已预案），请告知格式要求细节。**
3. **AIP 审查阶段需要提供哪些额外的 schema 文档或自鉴定证据？**

### 附件

- 附件 1：maritime-schema TrafficSituation 样例 YAML（Imazu-01 Head-On）
- 附件 2：FCB metadata.* 扩展字段表（§3 of 02-scenario-schema.md）
- 附件 3：DNV-RP-0513 自鉴定证据预览（V&V Plan §8 摘要）

### 项目时间线

- 2026-06-15：DEMO-1 Skeleton Live（maritime-schema + 22 Imazu 场景现场展示）
- 2026-08-31：DEMO-3 Full-Stack with Safety（1000 场景 SIL 回归）
- 2026-11：CCS i-Ship AIP 提交

盼复。如有任何疑问，请随时联系。

此致

[PM 姓名]  
MASS ADAS L3 项目经理  
[TBD-email]  
[TBD-phone]

---

**模板使用说明**：
- `[TBD-*]` 字段由 PM 填入实际联系人和联系方式
- 附件 1–3 由 D1.6 产出后附加
- 若 CCS 2026-06 回函要求中文专用格式，启动决策记录 §5.5 的导出器预案
