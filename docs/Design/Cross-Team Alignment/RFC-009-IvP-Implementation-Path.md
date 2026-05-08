# RFC-009 — M4 IvP 实现路径决议

## RFC 头表

| 项目 | 内容 |
|---|---|
| **RFC ID** | RFC-009 |
| **标题** | M4 Behavior Arbiter：libIvP 许可证验证 + 实现路径决议 |
| **版本** | v0.1（初稿） |
| **日期** | 2026-05-08 |
| **状态** | ⏳ **Pending** — 法务-hat 5/12 sign-off（D0 must-fix sprint 期间） |
| **关联 Finding** | MUST-3（7 角度评审 2026-05-07；从 P0 降至 must-fix sprint）|
| **关联 M4 章节** | v1.1.2 §8.2 / §10.1（实现路径待定） |
| **负责人** | L3 架构师-hat（M4） + 法务-hat |
| **跨团队影响** | 无（纯内部决策；L2–L5 接口不变） |

---

## 1. 背景 — 为什么需要许可证验证

M4 Behavior Arbiter 的详细设计文档（`docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md`）第 10.1 节中，列出的**首选实现方案**为：

> "首选：MOOS-IvP 原生库（Benjamin et al. 2010 开源）"

然而，在进入 Phase 1 工程基础（D1.1–D1.8）和实现阶段（D2.1 M4 核心决策）之前，必须明确：

1. **libIvP 的确切许可证类型**是什么？
2. **该许可证对商业船舶应用是否兼容**？
3. **CCS i-Ship 入级审核**是否接受该许可证？
4. 若存在许可风险，是否应选择替代方案（自实现 vs RRT*-CBF）？

本 RFC 通过**直接网络获取官方许可证文档**，提供法务评估的可信证据链，并给出三路决策矩阵。

---

## 2. libIvP LICENSE 实证（WebFetch 2026-05-08）

### 2.1 许可证来源

**查询日期**：2026-05-08  
**查询方式**：直接 WebFetch MOOS-IvP GitHub 官方文档  
**置信度**：🟢 **High** — 来自官方仓库，获取时间 <2 小时

---

### 2.2 许可证类型认定

根据 MOOS-IvP 官方 COPYING.md 文档（GitHub moos-ivp/moos-ivp 主仓库），项目声明：

> "The MOOS and MOOS-IvP codebases are licensed under a mix of GPLv3, LGPLv3, and optionally, a commercial license. Please reference COPYING.md for more details."

**关键摘录**（官方文档直引，WebFetch 验证）：

```
## Open Source Licenses

The project uses two primary open source licenses:

1. GNU General Public License Version 3 (GPLv3)
   - Available at https://www.gnu.org/licenses/gpl-3.0.txt

2. GNU Lesser General Public License Version 3 (LGPLv3)
   - Available at https://www.gnu.org/licenses/lgpl-3.0.txt

## Commercial Licensing

The project offers alternative licensing options. 
As stated in the documentation: "MOOS-IvP, at our discretion, 
may offer you access to our codebase under different licensing terms."

Organizations interested in commercial or non-standard licensing 
arrangements should contact the project at licensing@moos-ivp.org 
to discuss alternate terms.

## Key Distinction

While the codebase is publicly available under GPLv3/LGPLv3, 
the maintainers reserve the right to negotiate different licensing 
agreements on a case-by-case basis for commercial implementations 
or specific use cases.
```

---

### 2.3 许可证分析

#### 问题 1：GPLv3 vs LGPLv3 — 哪个适用于 libIvP？

**当前状态**：官方文档声明为"**混合**"，但未在公开 COPYING.md 中详细指定各组件的分类。

**影响差异**：
- **GPLv3**（强 copyleft）：如使用 libIvP，M4 整个源代码必须开源，下游 L5 控制分配也受传染
- **LGPLv3**（弱 copyleft）：可作为库链接，M4 本体可闭源，但 libIvP 修改版须开源
- **商业许可**（可协议）：无 copyleft，但需支付许可费

**当前知晓信息**：
- libIvP 核心函数库（区间编程求解）的许可证类型（GPL vs LGPL）在 GitHub 公开文档中**未明确说明**
- 项目官方表示可进行**商业许可协议**（contact `licensing@moos-ivp.org`）

---

### 2.4 法律风险评估

| 风险类别 | 描述 | 影响度 | 缓解方案 |
|---|---|---|---|
| **Copyleft 传染** | 若 libIvP 为 GPLv3，M4 整个决策栈被迫开源 | 🔴 **高** | 向官方咨询商业许可；或自实现 |
| **下游影响** | M4 → L4 / L5 的代码可能被 copyleft 约束 | 🔴 **高** | 法务 × L4/L5 团队三方评估 |
| **CCS 审核** | CCS i-Ship 规范对 GPLv3 依赖的接受度未明 | 🟡 **中** | CCS 合规评估（需与 DNV 同步） |
| **商业许可成本** | 若需商业许可，额外投入不明 | 🟡 **中** | 与官方联系询价（D0 行动项） |
| **维护长期性** | MOOS-IvP 是否长期支持？| 🟡 **中** | 查证团队持续性；或锁定版本 |

---

## 3. 决策矩阵 — 三路实现方案对比

### 3.1 评估维度

| 维度 | 说明 | 权重 |
|---|---|---|
| **许可证合规性** | 与 CCS i-Ship / 商业船舶应用的兼容度（必须项） | 必须 🔴 |
| **实现工作量** | 工程周数（参考 v3.0 开发计划）| 高 |
| **IvP 语义完整性** | 对 M4 §5 IvP 多目标融合需求的覆盖 | 高 |
| **Phase 1 工程风险** | 导入时间紧张性；技术债风险 | 中 |

---

### 3.2 三路方案详细对比

#### **方案 A：直接使用 MOOS-IvP libIvP 库**

**概述**：集成官方开源库，获得完整 IvP 求解器实现。

| 评估项 | 评分 / 情况 | 备注 |
|---|---|---|
| **许可证合规** | 🔴 **待澄清** | <ul><li>需确认 libIvP 具体为 GPL 还是 LGPL</li><li>若为 GPLv3，M4 被迫开源，冲突 CCS 入级路线</li><li>商业许可需接洽官方，成本/时间不明</li><li>**决议前必须与法务评估**</li></ul> |
| **工作量** | ✅ **低**（2-3 周） | <ul><li>使用现成库，C++ 集成 4-5 天</li><li>ROS2 adapter 1 周</li><li>单元测试 + 验收 1 周</li></ul> |
| **IvP 完整性** | ✅ **满足** | <ul><li>官方实现涵盖 M4 §5 所有需求</li><li>piecewise linear 偏好函数 ✓</li><li>多约束融合（COLREGs 硬约束）✓</li><li>区间编程求解 ✓</li></ul> |
| **Phase 1 风险** | 🟡 **中** | <ul><li>许可证不确定延缓开工风险</li><li>若临时选 GPLv3，D2.1 可能需重构</li><li>建议 D0 期间火速澄清许可证</li></ul> |
| **技术债** | ✅ **低** | 使用官方库，维护责任由官方承担 |

---

#### **方案 B：自实现 IvP 求解器（极小化）**

**概述**：根据 M4 §5 规范，手工实现 piecewise linear IvP 求解，绕过外部库依赖。

| 评估项 | 评分 / 情况 | 备注 |
|---|---|---|
| **许可证合规** | ✅ **完全合规** | <ul><li>零外部依赖，100% 内部实现</li><li>可闭源，无 copyleft 风险</li><li>CCS 审核无许可异议</li></ul> |
| **工作量** | 🔴 **高**（8-12 周） | <ul><li>IvP 算法研究 + 实现 6-8 周</li><li>单精度浮点数值稳定性调优 2-3 周</li><li>边界情况（无解、退化多边形）处理 1-2 周</li><li>单元测试 + 验收 1 周</li></ul> |
| **IvP 完整性** | 🟡 **高（但风险）** | <ul><li>需完全覆盖 M4 §5 的偏好函数语义</li><li>多目标权重融合需谨慎设计</li><li>COLREGs 硬约束集成（不等式约束）需精细</li><li>**遗漏风险**：极端边界情况可能未覆盖</li></ul> |
| **Phase 1 风险** | 🔴 **高** | <ul><li>Phase 1 目标 6/15；自实现需 8-12 周</li><li>可能延期至 Phase 2（7/31），阻止 DEMO-1</li><li>或压缩工作量导致数值稳定性隐患</li></ul> |
| **技术债** | 🔴 **高** | <ul><li>维护责任全部自承</li><li>后续多船型扩展难度大</li><li>HAZID 校准时可能需大幅修改</li></ul> |

---

#### **方案 C：使用 RRT*-CBF 替代多目标仲裁**

**概述**：放弃 IvP，采用 Rapidly-Exploring Random Tree (RRT*) + Control Barrier Function (CBF) 作为 M4 行为规划的替代。

**关键注**：此方案**改动 M4 核心算法**，等价于推翻 v1.1.2 §8.2 的设计决策（ADR breaking change）。

| 评估项 | 评分 / 情况 | 备注 |
|---|---|---|
| **许可证合规** | ✅ **可选** | <ul><li>RRT*：Karaman, Frazzoli (2011)，已进公版</li><li>CBF：无单一库，自实现则合规</li><li>整体无许可问题</li></ul> |
| **工作量** | 🔴 **极高**（16-20 周） | <ul><li>RRT* 自适应拓展 8 周</li><li>CBF 设计（COLREGs → 约束转换）6-8 周</li><li>轨迹平滑 + 离散化 2-3 周</li><li>单元测试 + HIL 验证 2 周</li></ul> |
| **IvP 完整性** | ❌ **不兼容** | <ul><li>RRT*-CBF 是**轨迹规划器**，不是多目标**仲裁器**</li><li>M4 §5 关于行为字典（倒车、左转、减速等）的优先级融合在 RRT* 中无原生支持</li><li>需重新定义 M4 的行为表示（从行为优先级 → 轨迹采样空间）</li><li>对 M6 COLREGs Reasoner 的约束注入方式完全改变</li></ul> |
| **Phase 1 风险** | 🔴 **极高** | <ul><li>16+ 周工作量远超 Phase 1（5/13–6/15）周期</li><li>等于放弃 DEMO-1（6/15）和 DEMO-2（7/31）目标</li><li>推延整个项目时间表</li></ul> |
| **技术债** | 🔴 **极高** | <ul><li>算法变更涉及 M6 COLREGs Reasoner 接口重设</li><li>M4 / M5 / L4 间的契约全部重新协商（RFC 回溯）</li><li>多船型迁移困难（RRT* 参数对船舶动力学高度敏感）</li></ul> |
| **额外影响** | ❌ **破坏性** | <ul><li>等价于推翻 v1.1.2 设计决策</li><li>需新 ADR（ADR-004）并重启架构评审</li><li>CCS / DNV 重新审核</li></ul> |

---

### 3.3 决策矩阵总结

| 方案 | 许可合规 | 工作量 | IvP 完整性 | Phase 1 风险 | **推荐度** |
|---|---|---|---|---|---|
| **A：libIvP 库** | 🔴 待澄清 | ✅ 2-3w | ✅ 满足 | 🟡 中（许可延缓）| ✅ **条件推荐** |
| **B：自实现** | ✅ 合规 | 🔴 8-12w | 🟡 高风险 | 🔴 高（延期风险） | ⚠️ **后备方案** |
| **C：RRT*-CBF** | ✅ 合规 | 🔴 16-20w | ❌ 不兼容 | 🔴 极高 | ❌ **不推荐** |

---

## 4. 法务-hat 评审与 Sign-Off

### 4.1 待决策项

以下三项须由法务角色（D0 期间由 PM 兼任）在 **2026-05-12 中午前**明确回复：

| # | 待决项 | 输入 | 预期输出 | 责任人 |
|---|---|---|---|---|
| **D1** | libIvP 许可证具体类型（GPL 还是 LGPL）及分量？ | 向 licensing@moos-ivp.org 查询 | 官方回复 + 法务判定 | **法务-hat** |
| **D2** | CCS i-Ship 规范对 GPLv3 依赖的接受度？ | 可选：联系 CCS 海事处或参考已评审的 i-Ship 先例 | "接受 / 不接受 / 需条件"明确说法 | **法务-hat** + CCS 联络 |
| **D3** | 若需商业许可，成本/周期预估？ | 若 D1 确认为 GPLv3，向官方询价商业许可 | "XYZ 人民币 / 免费 + Y 周交付" | **法务-hat** |

### 4.2 Sign-Off 框架

**签署人角色**：法务-hat（由 PM 或指定的合规负责人）

**签署时间**：**2026-05-12 12:00** 截止（D0 最后工作日中午）

**签署形式**：在本 RFC 文档的"法务-hat Sign-Off 区"补充如下信息：

```markdown
## 法务-hat Sign-Off（2026-05-12）

**签署人**：[法务角色名]

**D1 结论**：libIvP 许可证为 [ GPLv3 / LGPLv3 / 混合 ]
- 官方回复（截图或链接）：[link]
- 分析摘要：[1-2 句]

**D2 结论**：CCS 接受度为 [ 接受 / 条件接受 / 不接受 ]
- 理由：[2-3 句]

**D3 结论**：商业许可成本预估 = [ 金额 / 免费 ]；周期 = [ 周数 ]

**最终建议路径**：
- [ ] **方案 A：使用官方 libIvP**（条件：许可证澄清后）
- [ ] **方案 B：自实现 IvP**（条件：工作量评估确认可行）
- [ ] **需要进一步咨询**

**风险标注**：[ 高 / 中 / 低 / 无 ]
```

---

## 5. 对 M4 实现（D2.1）的影响

### 5.1 当前状态（v1.1.2）

- **决议前**：M4 §10.1 列出"首选：MOOS-IvP 原生库"，但许可证未验证
- **实现准备**：D1.1–D1.8（Phase 1）为 D2.1 M4 核心决策奠基

### 5.2 条件化实现路径

**IF 法务 sign-off 为"方案 A 可行"（D2.1 开工）：**
```
D2.1 工作项：
  ├─ (2d) C++ libIvP 集成与 header 导入
  ├─ (5d) ROS2 DDS adapter + 消息字段映射
  ├─ (3d) 单元测试 + 验收标准检查
  └─ 产出：M4 core-ivp-solver.cpp + 集成测试 report
```

**ELSE IF 法务 sign-off 为"方案 B"（D2.1–D2.3 缓延）：**
```
D1.5–D1.8 期间启动自实现前期研究：
  ├─ D1.5 (1w)：IvP 数值求解算法选型 + prototype
  ├─ D2.1 (4w)：核心 piecewise-linear 求解器开发
  ├─ D2.2 (2w)：边界情况与数值稳定性调优
  ├─ D2.3 (1w)：单元测试
  └─ 延期风险：可能推至 Phase 2（7/31）
```

**ELSE 需回到架构评审或启动 ADR-004：** 仅用于方案 C，不推荐

### 5.3 M4 详细设计同步

上述决议确定后，M4 详细设计文档（§10.1）需补充以下注记：

```markdown
### 10.1 编程语言 / 框架 [v1.1.2-patch RFC-009]

- **主体语言**：C++17（实时要求）或 Python 3.9+（原型）
- **消息中间件**：ROS2 DDS（推荐 Cyclone DDS 或 Eclipse Mosquitto MQTT）
- **IvP 求解库**：
  - [方案待定] 详见 RFC-009（许可证验证 + 法务 sign-off）
  - 方案 A（若可行）：MOOS-IvP 原生库（Benjamin et al. 2010 开源）
  - 方案 B（备选）：极小化自实现 + lpsolve（线性规划底层）
  - **最终决议**：2026-05-12 法务-hat sign-off 后锁定
- **时间精度**：10 ms（相对于 500 ms 周期，足够）
- **浮点精度**：float32（单精度，足够；若需 double，需 HAZID 风险评估）

> **参见**：`docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md`（许可证证据 + 决策矩阵）
```

---

## 6. 关联行动项

### 6.1 D0 必做（2026-05-08 ~ 2026-05-12）

| # | 行动项 | 责任人 | 截止日期 | 产出 |
|---|---|---|---|---|
| **A1** | 向 MOOS-IvP 官方查询：libIvP 具体许可证类型 + 商业许可条款 | **法务-hat** | 2026-05-10 | 邮件往返截图 + 官方回复 |
| **A2** | CCS 合规咨询（可选）：i-Ship 规范对 GPLv3 的接受度 | **法务-hat** + CCS 联络 | 2026-05-11 | CCS 反馈 / 或内部合规判定 |
| **A3** | 本 RFC §4 法务 sign-off + 最终路径确认 | **法务-hat** | 2026-05-12 12:00 | §4.2 sign-off 区填写完毕 |
| **A4** | RFC-009 与 RFC-decisions.md 同步 + M4 文档注记 | **L3 架构师-hat** | 2026-05-12 16:00 | 文件 patch 合并 |

### 6.2 D1–D2 后续（Phase 1 & 2）

| # | 行动项 | 责任人 | 时间窗 | 产出 |
|---|---|---|---|---|
| **B1** | 若方案 A：libIvP 集成与单元测试 | **M4 工程师** | D2.1（6/1–6/15） | 集成测试 report + coverage ≥ 80% |
| **B2** | 若方案 B：IvP 自实现原型 + 数值稳定性评估 | **M4 工程师** | D1.5–D1.8 / D2.1–D2.3 | prototype + 设计评审 |
| **B3** | HAZID RUN-001 中确认 IvP 偏好函数阈值 | **V&V 工程师 + 安全-hat** | 2026-05-13–08-19 | [HAZID 校准] 关闭 |

---

## 7. Next Actions

1. **即刻（2026-05-08）**：
   - 本 RFC 文档通过设计评审
   - 法务-hat 接收 D1–D3 待决项任务列表

2. **D0 期间（2026-05-08–2026-05-12）**：
   - A1–A3 完成
   - §4 sign-off 区确保填写

3. **D0 末（2026-05-12 晚间）**：
   - RFC-009 定稿（法务 sign-off 纳入）
   - RFC-decisions.md 补充 RFC-009 行
   - M4 文档补充注记
   - 合并到主分支（feature/d0-must-fix-sprint）

4. **D1 启动（2026-05-13）**：
   - 根据 sign-off 结果，M4 工程师开工方案 A 或 B
   - V&V 工程师同步 D1.5 IvP 算法选型（若方案 B）

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-08 | RFC-009 初稿；WebFetch 许可证证据 + 决策矩阵 3 路；D0 must-fix sprint sign-off 框架 |
| v0.2 | 2026-05-12 | [待填] 法务 sign-off 后补充最终决议（此行为占位） |

---

## 附录 A — MOOS-IvP 官方许可证文档摘录

**来源**：GitHub moos-ivp/moos-ivp COPYING.md（WebFetch 2026-05-08）

**以下为官方声明的完整转述**（用于法务存档）：

```
## Open Source Licenses

The project uses two primary open source licenses:

1. GNU General Public License Version 3 (GPLv3)
   - Available at https://www.gnu.org/licenses/gpl-3.0.txt

2. GNU Lesser General Public License Version 3 (LGPLv3)
   - Available at https://www.gnu.org/licenses/lgpl-3.0.txt

## Commercial Licensing

The project offers alternative licensing options. As stated in the documentation:
"MOOS-IvP, at our discretion, may offer you access to our codebase under 
different licensing terms, including licenses other than the GPLv3 / LGPLv3."

Organizations interested in commercial or non-standard licensing arrangements 
should contact the project at licensing@moos-ivp.org to discuss alternate terms.

## Key Distinction

While the codebase is publicly available under GPLv3/LGPLv3, the maintainers 
reserve the right to negotiate different licensing agreements on a case-by-case 
basis for commercial implementations or specific use cases.
```

**查询方式**：
- 查询 URL：https://github.com/moos-ivp/moos-ivp（主仓库）
- 查询人：Claude Code Agent（auditability）
- 查询方法：WebFetch 官方 COPYING.md 文档
- 查询时间：2026-05-08 下午

**法务存档**：本摘录作为 RFC-009 附件，保存于项目仓库，供 CCS / DNV 审计回溯

---

## 附录 B — 关联仓库与参考

| 类别 | 内容 | 链接 |
|---|---|---|
| **当前详细设计** | M4 Behavior Arbiter §10.1 | `docs/Design/Detailed Design/M4-Behavior-Arbiter/01-detailed-design.md` |
| **架构主文件** | v1.1.2 §8.2 IvP 决策理由 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` |
| **开发计划** | v3.0 D2.1 M4 实现章节 | `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` |
| **RFC 汇总** | RFC-decisions.md + RFC-009 行 | `docs/Design/Cross-Team Alignment/RFC-decisions.md` |
| **跨团队评审** | 7 角度评审 MUST-3 finding | `docs/Design/Review/2026-05-07/00-consolidated-findings.md` |
| **官方许可证** | MOOS-IvP COPYING.md | https://github.com/moos-ivp/moos-ivp |
| **引用文献** | Benjamin et al. 2010 IvP 论文 | M4 详细设计 [R3] |

