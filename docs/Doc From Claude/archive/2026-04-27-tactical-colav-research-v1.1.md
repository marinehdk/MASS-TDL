# 战术决策层避碰架构调研备忘（2026-04-27）

| 属性 | 值 |
|------|-----|
| 文档编号 | RESEARCH-TACTICAL-COLAV-001 |
| 版本 | **v1.1**（v1.0 冻结后的合并修订；含 SIL 验证场景模拟器章节 + N+/Nx 修订）|
| 日期 | 2026-04-27 |
| 作者 | 用户 + Claude（marine-research-planner 协作）|
| 状态 | **冻结**（v1.1 是后续 Spec 的唯一真源）|
| 前序版本 | v0.1 (初稿) → v0.2 (13 项 TBD 闭环) → v1.0 (用户冻结) → v1.1 (合并 Patch-A) |
| 触发 Spec | `2026-04-27-tactical-colav-design-part1-bluf-adr.md`（已产出 Spec Part 1）|
| 影响范围 | 自主航行系统战术决策层；上接战略层（Voyage Planner），下接控制层（Trajectory/Heading Controller）；横向接 ROC 与感知融合 |
| **目标船级社** | **CCS（中国船级社）单一入级**（用户 2026-04-27 确认）|
| **首发船型** | 45m FCB（Fast Crew Boat），架构须扩展到所有船型 |
| **自主等级覆盖** | D2-D4 全谱（含降级）|

---

## 1. 问题陈述

设计自主航行系统的**战术决策层**（Tactical Decision Layer），承担船舶在 D2-D4 自主等级谱系下的避碰决策、局部重规划、模式管理与上下游接口契约。系统首发对象是 45m 级 FCB（Fast Crew Boat），但架构必须能扩展到任意船型。决策必须满足 **CCS《智能船舶规范》(2025)** 的入级要求，并对接 **IMO MASS Code（MSC 111 2026-05 通过的非强制版本）** 的目标基础与 **IACS UR E26/E27 Rev.1（2024-07-01 强制）** 的网络韧性要求。

**现状问题**：

- 业界尚无统一的"战术决策层"接口契约定义，工业界（Kongsberg / Wärtsilä / Avikus / Sea Machines）的实现各自划界、各自命名、各自对接控制层
- COLREGs 在 IMO MSC 110 / 111 阶段未做修订，自主船仍受现行 COLREGs 约束，规则解释边界（特别是 Rule 2 "良好船艺" 和 Rule 17 "保向船的最后避让"）需在算法中显式形式化
- CCS《智能船舶规范》采用 **GBS 开放式框架**（不规定具体实现，要求验证证据）+ **N / Nx / R1 / R2 / A1 / A2 / A3 标志组合**，需在架构上同时支持多组合与无缝降级
- 单船型设计往往与水动力模型耦合过紧，多船型扩展时需重写避碰核心，违反开闭原则
- D2-D4 自主等级降级路径（特别是 ROC 失联、传感器降质、关键子系统故障）未在主流学术架构中显式建模

**变更目标**：

- 输出一个**模块化、平台化、CCS 可入级**的战术决策层架构候选方案（含证据链）
- 架构必须显式支持 D2-D4 谱系切换 + Fallback 状态机
- 架构必须显式支持多船型参数化（船舶动力学插件化）
- 与战略层 / 控制层 / ROC / 感知层的接口边界明确可冻结
- 调研结论冻结后作为后续 Spec 阶段的唯一真源

**不在本调研范围**：

- 感知融合内部算法（多传感器融合、目标识别等属于感知层范畴）
- 控制层内部算法（PID / NMPC track control 等）
- 战略层航次规划具体算法（A* 全局路径、能效优化等）
- 感知/控制/通信子系统的硬件选型
- ROC 物理基础设施（HMI 设计、岸基服务器架构等）
- 网络安全实现细节（IACS UR E26/E27 + CCS《船舶网络安全指南》(2024) 合规独立调研）

---

## 2. 上游权威清单

### 2.1 项目内权威

用户确认**目前没有项目级文件**。本调研在"无项目专有约束"假设下进行；未来用户提供项目文档后需重新评估冲突项。

| 项目文档 / 章节 | 关键内容 | 是否已读 |
|----------------|---------|---------|
| 项目 CLAUDE.md | — | ❌ 用户确认无 |
| 项目 SDD | — | ❌ 用户确认无 |
| 项目 ICD（接口控制文档）| — | ❌ 用户确认无 |

**项目侧已知约束（用户口头确认）**：

- 首发船型：45m FCB，高速半滑行船型
- 系统须扩展到所有船型
- 自主等级覆盖 D2-D4 全谱（带降级）
- 战术层范围：避碰 + 局部重规划 + 模式管理 + 战略/控制接口契约 + 完整独立子系统 + 公共服务调用
- 目标船级社：**CCS 单一**

### 2.2 外部规则 / 标准（CCS 主导，DNV / ABS / BV / IACS / IMO 作为对照与补充）

| 规范 | 版本 | 相关条款 | 访问时间 |
|------|------|---------|---------|
| **CCS《智能船舶规范》(2025)** | **2025-04-01 生效** | 第 8 章（远程控制船舶）+ 第 9 章（自主操作船舶）+ 智能系统认可原则 + 远程控制站 HMI | 2026-04-27 |
| CCS《船舶网络安全指南》(2024) | 2024 版 | III 类计算机系统（含远程控制 / 自主控制）网络安全等级 | 2026-04-27 |
| CCS《船用软件安全及可靠性评估指南》| CCS 基础性指南 | 智能船舶软件安全、可靠性评估通用要求 | 2026-04-27 |
| CCS《自主货物运输船舶指南》(2018) | 2018 | 场景感知、航行控制、远程控制中心、网络安全 | 2026-04-27（参考性，FCB 非货船）|
| CCS《无人水面艇检验指南》(2018) | 2018 | USV 自主航行 / 远程遥控分级标志 | 2026-04-27（参考性，FCB 有人不适用）|
| IACS UR E26 Rev.1 | 2024-07-01 强制（500 GT 以上新建船）| 整个 UR | 2026-04-27 |
| IACS UR E27 Rev.1 | 2024-07-01 强制 | 整个 UR | 2026-04-27 |
| IACS UR E22 Rev.3 | 2023 | 计算机系统 Cat. I/II/III 分类 | 2026-04-27 |
| DNV-CG-0264 "Autonomous and remotely operated ships" | 2024 ed. | Sec.2 主原则、Sec.3 评估流程、Sec.4 NAV 功能 | 2026-04-27（参照性）|
| DNV AROS 类标志 | 2025-01-01 生效 | NAV/ENG/OPS/SAF × RC/DS/SA/FA × OB/OS/HC | 2026-04-27（参照性）|
| ABS Requirements (2024-10, Pub. 323)| 2024-10 | CONOPS goal-based 框架；AUTONOMOUS / REMOTE-CON 标志 | 2026-04-27（参照性）|
| BV NI 641 / NR 681 USV | 2017/2019 / 2022 | 4 度自动化；NR 681 USV < 500 GRT + **无人** | 2026-04-27（参照性，FCB 不属 NR 681 范畴）|
| IMO MSC.1/Circular.1455 | 通用 | 自主船审批通用框架 | 2026-04-27 |
| IMO MASS Code | 非强制版 MSC 111（2026-05）通过预期；强制版 2032 | 全部 | 2026-04-27 |
| COLREGs 1972（含修订）| 当前版本（IMO 表态 MASS Code 通过前不修订）| Rule 2 / 5 / 6 / 7 / 8 / 13-19 | 2026-04-27 |
| IEC 61508 / IEEE 1362-1998 ConOps | 通用 | SIL 分配、CONOPS 文档结构 | 2026-04-27 |

### 2.3 已知开放项

用户确认无项目级开放项编号体系。本调研产出的 TBD 由用户后续统一编号纳入项目级跟踪。

---

## 3. 工业参考实现调研

### 3.1 候选方案 A · Kongsberg K-MATE + K-Bridge / K-Pos / K-Chief 栈

**引用来源**：

- [K-MATE Kongsberg Maritime Autonomy Engine 文档（GEBCO 2017）](https://www.gebco.net/sites/default/files/documents/lee_04_2_2017.pdf) · 访问时间 2026-04-27
- [Autonomous obstacle avoidance · Kongsberg](https://www.kongsberg.com/newsroom/stories/2017/9/autonomous-obstacle-avoidance/) · 访问时间 2026-04-27
- [AUTOSEA project · Kongsberg + NTNU + DNV + Maritime Robotics](https://www.kongsberg.com/maritime/news-and-events/news-archive/2015/new-research-project-to-investigate-sensor-fusion-and-collision-avoidance-for/) · 访问时间 2026-04-27
- [Yara Birkeland 项目栈](https://www.kongsberg.com/maritime/news-and-events/news-archive/2016/automated-ships-ltd-and-kongsberg-to-build-first-unmanned-and-fully-autonomous/) · 访问时间 2026-04-27
- [Kongsberg AiP 转移关键角色到岸基（DNV，2024-06）](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/regulatory/) · 访问时间 2026-04-27

**技术特征**：

- K-MATE 是统一的自主决策引擎，**与 HUGIN/MUNIN 水下平台共用架构**（共享代码库降低多船型移植成本）
- 4 模块组：Control & Guidance、Navigation & Positioning、Scene Analysis、Payload Control
- 3 操作模式：Autonomous / Supervised / Direct control
- 能力：Waypoint Following、Radar+Lidar 避碰、Virtual anchor、AUV following
- Backseat Driver 模式：K-MATE → autopilot/DP（可挂接已有控制层）
- AUTOSEA 项目（NTNU 主导）专门验证传感器融合 + COLREGs 合规导航控制

**工业落地案例**：Yara Birkeland、Hrönn、2024-06 DNV AiP（关键角色转岸基化）

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型（45m FCB + 多船型扩展）| H | "Adaptable autonomy kit for almost any hull" |
| 认证目标（**CCS**）| L-M | 主战场 DNV，无明确 CCS 认证；范式可借鉴 |
| 计算资源 | M | 工业级嵌入式 |
| 与现有架构集成 | H | Backseat Driver 范式天然解耦 |

**已知问题**：Yara Birkeland 完全无人航行延迟多年；K-MATE 闭源，自研无法直接复用代码。

### 3.2 候选方案 B · Wärtsilä NACOS Platinum + SmartMove Suite + AIM

**引用来源**：

- [Wärtsilä Voyage 概览](https://www.wartsila.com/voyage?id=492) · 访问时间 2026-04-27
- [NACOS Platinum Navigation 文档](https://www.wartsila.com/voyage/integrated-vessel-control-systems/navigation/smartquay) · 访问时间 2026-04-27
- [SmartMove + American Courage 案例](https://www.wartsila.com/media/news/13-01-2021-ship-control-redefined-wartsila-smartmove-suite-sets-sail-with-the-american-steamship-company-3253273) · 访问时间 2026-04-27
- [AIM + Lighthouse COLREGS / Chalmers 验证](https://www.wartsila.com/insights/article/the-evolving-intelligence-behind-smart-navigation) · 访问时间 2026-04-27
- [IntelliTug autonomous level 4 + ClassNK COLREGs 认证](https://www.wartsila.com/insights/article/simulating-the-future-of-maritime) · 访问时间 2026-04-27

**技术特征**：

- NACOS Platinum：集成 Navigation / Automation / Dynamic Positioning（含 BNWAS、TCS）
- SmartMove Suite 5 应用：SmartDock / SmartTransit / SmartEntry / SmartPredict / SmartDrive
- AIM：他船操纵 **15-30 分钟前瞻** + COLREGs 合规候选机动
- ClassNK 已认证 SmartMove COLREGs 功能

**工业落地案例**：MV American Courage（194m）首装；IntelliTug + PSA Polaris 拖船 autonomous level 4。

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | M | 案例从大型船到拖船；FCB 高速段无公开案例 |
| 认证目标（**CCS**）| L | 主合作 LR / ClassNK |
| 与现有架构集成 | M | 强绑定 NACOS 平台 |

**已知问题**：AIM 当前定位决策辅助（advisor），与 D4 全自主有差距。

### 3.3 候选方案 C · Avikus HiNAS Control（HD Hyundai 子公司）

**引用来源**：

- [Avikus HiNAS Control 获 DNV Type Approval（2026-04-07）](https://www.koreaherald.com/article/10711851) · 访问时间 2026-04-27
- [Avikus HiNAS 产品手册 PDF](https://avikus.ai/hubfs/45548415/HiNAS_Brochure_0823.pdf?hsLang=en) · 访问时间 2026-04-27
- [H-Line Shipping 30 船商业合同（2024-12）](https://avikus.ai/en-us/press/hd-hyundais-avikus-supplies-autonomous-navigation-solutions-to-a-large-fleet-of-30-vessels) · 访问时间 2026-04-27

**技术特征**：

- **首个跨船型量产自主航行系统获国际船级社 Type Approval**（DNV，2026-04-07）
- 三段架构（与本调研推荐架构高度对齐）：**Perception → Decision-Making → Control**
- COLREGs 形式化决策 + 视觉 + 多传感器融合（含夜间和恶劣天气）
- 定位 IMO MASS Level 2（D2，类 ADAS Level 2）
- HD Hyundai 自 2023 起标配，累计 500+ 订单
- H-Line Shipping 2024-12 首批 5 船商业合同（计划扩展 30 船）
- 9334 km 试航 15% 节油

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 跨船型量产是核心卖点 |
| 认证目标（**CCS**）| L | DNV TA 已获，CCS 未公开；架构启发价值 H |
| 计算资源 | H | 量产型 |

**已知问题**：当前 D2 为主，D3/D4 路径在公开文档中未明确。

### 3.4 候选方案 D · Sea Machines SM300 / SM400 + TALOS

**引用来源**：

- [Sea Machines 官网 SM300 系列](https://sea-machines.com/products/sm300-autonomous-command-control/) · 访问时间 2026-04-27
- [SM300-NG class-society approved 硬件](https://sea-machines.com/sm300-ng/) · 访问时间 2026-04-27
- [Sea Machines SM300 + Foss tug 获 ABS AiP（2022-08-23）](https://sea-machines.com/sea-machines-sm300-system-onboard-foss-tug-earns-abs-approval/) · 访问时间 2026-04-27
- [Nellie Bly 1027 nm 自主航行案例](https://www.workboat.com/bluewater/sea-machines-completes-world-s-first-1-000-nautical-mile-autonomous-voyage) · 访问时间 2026-04-27
- [SM300 + 美海军 ALPV 项目](https://www.workboat.com/sea-machines-sm300-selected-for-marine-corps-low-profile-usv-program) · 访问时间 2026-04-27

**技术特征**：

- TALOS 中间件，连接推进 + 导航传感
- **SM300 覆盖 10-300 ft 全船型**，与 45m / ~150ft FCB **直接匹配**
- SM400 AI 视觉感知；SMLink Connect-API（数据）+ Control-API（控制）
- COLREGs 自认 "abides by parts of"
- 200+ 全球部署
- Nellie Bly 1027 nm 自主航行：96.9% 自主，31 次避碰机动

**认证状态（TBD-12 关闭）**：

- **SM200**：ABS 完整认可（用于美旗拖船 ATB 配套）
- **SM300**：ABS Approval in Principle（Foss tug Rachael Allen，2022-08-23）
- **SM300-NG**："Class-Society approved" 硬件
- **CCS / DNV / BV** 公开资料中均无明确 Type Approval

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 直接覆盖 FCB 尺寸段 |
| 认证目标（**CCS**）| L | 主走 ABS 路径 |
| 与现有架构集成 | H | Connect/Control API 显式开放 |

### 3.5 候选方案 E · MOOS-IvP / IvP Helm（MIT 开源参考实现）

**引用来源**：

- [MOOS-IvP 官方文档：Helm Design Intro](https://oceanai.mit.edu/ivpman/pmwiki/pmwiki.php?n=Helm.HelmDesignIntro) · 访问时间 2026-04-27
- Benjamin et al. (2010) "Nested autonomy for unmanned marine vehicles with MOOS-IvP". *Journal of Field Robotics* 27(6):834-875. [DOI: 10.1002/rob.20370](https://doi.org/10.1002/rob.20370)

**技术特征**：

- **Backseat Driver 范式**原典：自主与控制完全解耦
- IvP Helm 多目标优化（Interval Programming）仲裁多个并发"行为"
- 130,000+ 行 C++ 开源；BSD 许可
- 数百小时实船验证

**与本项目场景的适配性**：

| 维度 | 适配度 | 说明 |
|------|--------|------|
| 船型 | H | 抽象层完全船型无关 |
| 认证目标（**CCS**）| L | 学术 / 防务为主；架构原则可借鉴 |
| 工程化路径 | M | 大型有人船商业部署案例少 |

### 3.6 工业方案对比小结

| 维度 | A · Kongsberg | B · Wärtsilä | C · Avikus | D · Sea Machines | E · MOOS-IvP |
|------|--------------|--------------|------------|------------------|--------------|
| 核心范式 | 模块化 + Backseat | 决策辅助渐进式 | 三段流水线 | 中间件 + Connect/Control API | 多目标优化 + Backseat |
| 多船型支持 | H | M | H | H | H |
| 船级社路径 | DNV 强 | LR/ClassNK 强 | DNV TA | ABS AiP | 学术/防务 |
| **CCS 路径** | L | L | L | L | L |
| D2-D4 全谱 | H | M | L-M | H | H |
| 商业开源 | 闭源 | 闭源 | 闭源 | 闭源 | 开源 BSD |
| 对自研架构启发价值 | H | M | H | H | H |

**关键观察**：四家商业厂商**均无公开 CCS 认证记录**——这是中国市场自研项目的机会窗口。架构设计可融合 5 家的优点，但 CCS 合规路径需要项目自建。

---

## 4. 学术文献调研

### 4.1 核心文献

| 引用 | 主题 | 与本调研关联 | DOI / URL |
|------|------|------------|-----------|
| Eriksen, B.-O. H., Bitar, G., Breivik, M., & Lekkas, A. M. (2020). "Hybrid Collision Avoidance for ASVs Compliant with COLREGs Rules 8 and 13–17." *Frontiers in Robotics and AI*, 7, 18. | 多时间尺度混合避碰架构（高层规划器 + 中程 MPC + 短期 BC-MPC）| **本调研推荐架构的学术基线** | [DOI: 10.3389/frobt.2020.00011](https://doi.org/10.3389/frobt.2020.00011) |
| Burmeister, H.-C., & Constapel, M. (2021). "Autonomous Collision Avoidance at Sea: A Survey." *Frontiers in Robotics and AI*, 8, 739013. | MPC 文献最常见；CPA 普遍触发；DRL 罕见 | 算法选型证据 | [DOI: 10.3389/frobt.2021.739013](https://doi.org/10.3389/frobt.2021.739013) |
| Huang, Y., Chen, L., Chen, P., Negenborn, R. R., & van Gelder, P. H. A. J. M. (2020). "Ship collision avoidance methods: State-of-the-art." *Safety Science*, 121, 451-473. | 避碰方法系统综述 | 算法基础 | [DOI: 10.1016/j.ssci.2019.09.018](https://doi.org/10.1016/j.ssci.2019.09.018) |
| Benjamin, M. R., Schmidt, H., Newman, P. M., & Leonard, J. J. (2010). "Nested autonomy for unmanned marine vehicles with MOOS-IvP." *Journal of Field Robotics*, 27(6), 834-875. | Backseat Driver 范式奠基 | 架构解耦原则 | [DOI: 10.1002/rob.20370](https://doi.org/10.1002/rob.20370) |
| Chiang, H.-T. L., & Tapia, L. (2018). "COLREG-RRT: An RRT-Based COLREGS-Compliant Motion Planner for Surface Vehicle Navigation." *IEEE Robotics & Automation Letters*, 3(3), 2024-2031. | RRT 类规划器 + COLREGs 嵌入 | 局部重规划备选 | [DOI: 10.1109/LRA.2018.2801881](https://doi.org/10.1109/LRA.2018.2801881) |
| **Veitch, E., et al. (2024). "Human factor influences on supervisory control of remotely operated and autonomous vessels." *Ocean Engineering*** | **ROC 接管时间窗实验：20s 与 60s 对比；接管 / 切换在不同决策支持系统下的人因影响** | **TBD-06 关闭主证据** | [URL](https://www.sciencedirect.com/science/article/pii/S0029801824005948) |
| Yasukawa, H., Hirata, N., Nakayama, Y. (2016). "High-speed ship maneuverability." *Journal of Ship Research*, 60(4), 239-258. | 高速船操纵性建模 | **FCB 半滑行段水动力建模学术基线** | J. Ship Research |
| Yasukawa, H., Yoshimura, Y. (2015). "Introduction of MMG standard method for ship maneuvering predictions." *Journal of Marine Science and Technology*, 20(1), 37-52. | MMG 操纵建模工业标准 | MPC 内部预测模型基础 | DOI: 10.1007/s00773-014-0293-y |

### 4.2 综述 / 较新文献

| 引用 | 关键章节 | 用途 |
|------|---------|------|
| MDPI *J. Mar. Sci. Eng.* 13(8):1570 (2025-08) "A Review of Research on Autonomous Collision Avoidance Performance Testing and an Evaluation of Intelligent Vessels" · [URL](https://www.mdpi.com/2077-1312/13/8/1570) | DWA / VO / APF / MPC 算法对比表 | 算法对比基础 |
| MDPI *J. Mar. Sci. Eng.* 13(7):1246 (2025-06) "Model Predictive Control for Autonomous Ship Navigation with COLREG Compliance and Chart-Based Path Planning" · [URL](https://www.mdpi.com/2077-1312/13/7/1246) | MPC + 海图全局规划 + COLREGs 16 类场景分类 | 中程 MPC 实现 |
| Cambridge *Journal of Navigation* (2026-02) "Path planning and collision avoidance technologies for MASS: a review" · [URL](https://www.cambridge.org/core/journals/journal-of-navigation/article/abs/path-planning-and-collision-avoidance-technologies-for-maritime-autonomous-surface-ships-a-review-of-colregs-compliance-algorithmic-trends-and-the-navigationgpt-framework/964649D8A39D86D58004B14CAD8A3C39) | LLM-aided navigation 等前沿方向 | 长期路线参考 |
| He et al. (2025) "COLREGs-compliant model predictive collision avoidance for autonomous ships in restricted environments." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/abs/pii/S0029801825016725) | 16 类场景分类（4×4）+ 受限水域 MPC 实船验证 | 受限水域参考 |
| Reza, M.A. et al. (2025). "Digital twin syncing for autonomous surface vessels using reinforcement learning and nonlinear model predictive control." *Scientific Reports* · [URL](https://www.nature.com/articles/s41598-025-93635-9) | RL + NMPC 混合 + 数字孪生 + 系统辨识 | **TBD-07 关闭：不确定性建模可行路径** |
| 智能船舶航行功能测试验证的方法体系 (2020) · *Ship Research* | 虚拟仿真初试 + 模型测试中试 + 实船验证终试 | 中国体系下测试方法基线 |

### 4.3 主流算法对比

| 算法 | 实时性 | COLREGs 覆盖度 | 计算成本 | 工程成熟度 | 主流文献用法 |
|------|--------|---------------|---------|-----------|-------------|
| Dynamic Window Approach (DWA) | H | L-M | H | M | USV 局部避碰 |
| Velocity Obstacle (VO) | H | M | L-M | M | 触发判定 + 候选机动 |
| Artificial Potential Field (APF) | H | L | L | L-M | 仅辅助 |
| **Model Predictive Control (MPC)** | M | H | H | H | **文献主流，工业首选** |
| Branching-Course MPC (BC-MPC) | H | H | L-M | M-H | 多分支兜底 |
| Robust MPC（Tube / Min-max）| M | H | M-H | M | **不确定性显式处理** |
| GP-based / SI-based Data-Driven MPC | M | H | M | M | **在线模型辨识** |
| Deep RL | H（推理）| 视训练 | L | L（认证难）| 学术前沿 |
| **Hybrid: 中程 MPC + 短期 BC-MPC**（Eriksen 2020）| 最优 | 最高 | 中 | M-H | **工业级标杆架构** |

### 4.4 学术空白补充说明

- FCB 高速半滑行段（25-35 节）的水动力建模：基础理论由 **Yasukawa 2016 + MMG 2015** 提供；不确定性处理由 **Robust MPC（Tube/Min-max）** + **GP/SI 在线辨识** 兜底（详见 §6.10）
- V2V 多船协同：详见 §6.6，IMO MASS Code 阶段不强制要求

---

## 5. 候选方案对比

| 维度 | 方案 1：单一 MPC（仅中程）| 方案 2：多时间尺度混合（Eriksen 2020 模型）| 方案 3：端到端 DL（黑盒）| 方案 4：行为式（IvP / 多目标）| 权重 |
|------|-------------------|-----------------------|---------------|------------------|------|
| 安全（兜底机制）| M | **H（中程 + 短期分层）**| L（可解释性差）| M-H | 1.0 |
| 合规（CCS GBS 白盒可审）| H | **H（白盒 + 多层显式逻辑）**| L | H | 0.9 |
| 可扩展（多船型）| H | **H** | M | H | 0.7 |
| 工程复杂度 | L | M | M-H | M-H | 0.4 |
| 计算性能 | M | **M-H** | H | M | 0.4 |
| 证据链强度 | M | **H（学术 + Avikus 工业 + ClassNK 验证）**| L | M | 参考 |

**推荐方案**：**方案 2 · 多时间尺度混合（中程 MPC + 短期 BC-MPC）**

**推荐理由**：

1. **安全分层**：中程 MPC 处理 COLREGs Rule 8 / 13-17（前置避让），短期 BC-MPC 应对 Rule 17(b) 保向船最后避让 / 异常船 / Rule 19 受限能见度（依据 §3.1 / §3.2 / §3.3 工业实践 + §4.1 Eriksen 2020）
2. **CCS 合规可审**：白盒决策路径符合 CCS《智能船舶规范》(2025) 智能系统认可原则（§6.2 详）+ CCS《船用软件安全及可靠性评估指南》要求
3. **多船型能力**：船舶动力学作为 MPC 内部参数化插件，FCB 首发后可注入其他船型水动力模型而不改决策核心（§3.3 + §3.4 工业证据）
4. **工程节奏可控**：现有学术与工业证据充分；自研无版权风险

---

## 6. 船级社合规影响（CCS 主导）

### 6.1 CCS 智能船舶规范 (2025) 功能标志映射 — TBD-03 关闭

CCS 智能船舶 i-Ship 附加标志体系：

| 标志类 | 子标志 | 含义 | 与战术层关联 |
|-------|-------|------|-------------|
| **N**（智能航行）| N（基础）| 辅助决策 + 航路/航速优化 + 视觉增强 + 碰撞预警 + 搁浅预警 | 决策辅助场景 |
| **N**（智能航行）| Nx（高级补充功能）| 在 N（基础辅助决策：航路/航速优化、视觉增强、碰撞预警、搁浅预警、综合信息显示）基础上叠加：开阔水域自主航行 / 自动靠离码头 / 狭窄水道高级自主 | **战术层主战场** |
| Hh / Hm | 船体维护 / 监测决策 | 船体相关 | 间接 |
| M | 智能机舱 | 机械状态监测 + 健康评估 + 辅助决策 | 跨层数据接口 |
| E | 智能能效 | 能效监测 / 优化 | 战术层选机动时可参考 |
| C | 智能货物管理 | 货物参数监测 | FCB 不适用 |
| I | 智能集成平台 | 数据汇聚 / 船岸交互 / 内部通信冗余 | **战术层数据流的承载平台** |
| **R1**（远程控制 + 船上有船员）| —— | 主要功能由 ROC 控制；船上船员监视 + 应急接管 | **D3 模式直接对应** |
| **R2**（远程控制 + 船上无船员）| —— | ROC 全控制；船上无船员 | D3 扩展（FCB 通常 R1）|
| **A1 / A2 / A3**（自主操作）| —— | 自主范围与能力分级（A1 < A2 < A3）| **D4 模式分级**；A3 ≈ FA |

**FCB 入级建议**（v1.1 修订；CCS 标志体系实际无"N+"标志）：

按运营场景二选一：

- **基础选项**：**i-Ship (N, R1, M, E, I)** — 主决策由船员主导，战术层提供辅助决策（碰撞/搁浅预警 + 综合显示），船端遥控备份
- **进阶选项**：**i-Ship (Nx, R1, M, E, I)** — 含开阔水域自主航行能力（D3-D4），船员监视 + ROC 应急接管

默认按进阶选项设计，兼容基础选项作为降级配置。最终选择由 CCS 验船师 + 船东根据具体运营水域和复杂度敲定。

实船入级参考：

- "新红专"号智能研究与教学实训两用船（2024-07）：i-Ship (I.Nx, Mx, Hx, Ex, Ri)
- "中远海运智利"/"中远海运荷花"/"明远"/"明卓"号 / "厦港拖30"：i-Ship (N, M, E, I)
- 当前 CCS 已授标的智能船舶以 N + M + E + I 四标志组合最常见；Nx + Ri 为研究/示范船型；A1/A2/A3 自主级别尚无公开授标实船。

来源：

- [CCS《智能船舶规范》(2025) 2025-04-01 生效公告](https://www.ccs.org.cn/ccswz/specialDetail?id=202503120375512073) · 访问时间 2026-04-27
- [《智能船舶规范》(2025) 修订要点解读](https://m.thepaper.cn/newsDetail_forward_30640809) · 访问时间 2026-04-27
- [信德海事网 CCS R1/R2/A1/A2/A3 定义](https://www.xindemarinenews.com/topic/zhinenhangyun/2020/0917/23622.html) · 访问时间 2026-04-27
- [国际船舶网 CCS 智能船舶规范全面解读](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27

### 6.2 CCS 智能系统认可原则（2025 新增）— TBD-03 补充关闭

CCS《智能船舶规范》(2025) 新增的智能系统认可原则要点：

| 要求项 | 内容 | 战术层落地 |
|-------|------|-----------|
| 智能系统**独立认可** | 每个智能子系统必须可独立认可，软硬件 + 传感器持证 | 战术层 7 个子模块 + 模式管理器须各自提交证据 |
| **III 类计算机系统** | 远程控制系统 / 自主控制系统 = III 类 CBS（最高等级），建议高等级网络安全 | 战术层主决策属 III 类，对接 CCS《船舶网络安全指南》(2024) + IACS UR E26 / E27 Rev.1 |
| **远程控制站 HMI 要求** | 设计 / 布置 / 照明 / 选址技术要求新增 | ROC 接口契约对接此要求 |
| **场景感知 + 自主决策** | 锚泊设备 + 一般智能场景需场景感知和自主决策能力 | 战术层 = 场景感知决策的工程载体 |

### 6.3 CCS 配套基础指南（TBD-03 / TBD-08 关闭）

CCS 在《智能船舶规范》之外发布的配套指南：

- 《船用软件安全及可靠性评估指南》—— 智能船舶软件验证通用要求（白盒证据要求）
- 《船舶网络系统要求及安全评估指南》—— 网络安全通用要求
- 《智能机舱检验指南》（2017-11-06 生效）—— 机舱专项
- 《智能集成平台检验指南》—— 集成平台专项
- 《无人水面艇检验指南》(2018) —— USV 专项（FCB 不适用，仅参考）

来源：

- [国际船舶网 CCS 智能船舶规范全面解读 — 配套指南清单](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27
- [中国水运报 CCS 发布智能机舱检验指南 2017](http://www.cansi.org.cn/cms/document/8496.html) · 访问时间 2026-04-27

### 6.4 CCS 对 AI / ML 的政策（TBD-08 关闭）

**关键发现**：CCS《智能船舶规范》采用 **GBS 开放式框架**——不规定 AI/ML 是否可用，而是要求基于风险评估和软件可靠性评估自证。具体路径：

1. **CCS《智能船舶规范》(2025) 智能系统认可原则**：要求软件 + 硬件独立持证；不限制具体技术
2. **CCS《船用软件安全及可靠性评估指南》**：白盒可审是默认要求；ML / 黑盒需提供等同安全证据
3. **III 类计算机系统**：建议高等级网络安全 + 软件可靠性证据
4. **测试验证方法体系**（《Ship Research》2020）：CCS 推荐"虚拟仿真初试 + 模型测试中试 + 实船验证终试"三段式

**工程结论**（与 §9 决策建议一致）：
- **主决策路径必须白盒**（MPC + COLREGs 形式化规则）—— CCS 认证路径成熟
- **辅助子模块（感知预处理、他船意图预测、传感器融合层）可用 ML**—— 需重型 V&V 证据 + 软件可靠性评估证据

来源：

- [《智能船舶规范》(2025) 修订要点解读 — III 类 CBS 网络安全](https://m.thepaper.cn/newsDetail_forward_30640809) · 访问时间 2026-04-27
- [国际船舶网 CCS 智能船舶规范全面解读](http://wap.eworldship.com/index.php/eworldship/news/article?id=163502) · 访问时间 2026-04-27

### 6.5 CCS 框架下 ROC 接管时间硬限（TBD-06 关闭）

**关键发现**：CCS、DNV、ABS、BV **均不发布固定秒级 ROC 接管硬限**，全部采用 **goal-based + risk-based 框架**。具体含义：

- 项目自证：在风险评估（HAZID / HAZOP / FMEA）和 CONOPS 中显式声明每个工况下的接管时间预算 + 系统响应时间分布 + 失败回退路径
- 学术参考值（实验室设计参数）：**Veitch et al. (2024) Ocean Engineering** 使用 **20 秒（紧急介入窗）** 和 **60 秒（监督介入窗）** 两档；接管成功率受 5 因素影响（操作员技能、监视时长、监管船数、可用时间、决策支持系统）
- 行业默认实践（**项目级建议默认值，需用户确认或在 CONOPS 中正式冻结**）：
  - 紧急避碰 ≤ 30 秒
  - 模式切换 ≤ 60 秒
  - 非紧急监督 ≤ 5 分钟
- 通信链路 RTT + 延迟必须显式建模（卫星链路 0.5-2s 量级），接管时间 = RTT + 决策时间 + 命令链路时间

来源：

- Veitch, E., et al. (2024). "Human factor influences on supervisory control of remotely operated and autonomous vessels." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/pii/S0029801824005948) · 访问时间 2026-04-27
- [DNV AROS 类标志框架描述（goal-based 路径）](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/aros-class-notation/) · 访问时间 2026-04-27
- [arxiv 2508.00543 — MASS-ROC 认证框架，含 STPA + EMSA RBAT 风险评估方法](https://arxiv.org/html/2508.00543) · 访问时间 2026-04-27

### 6.6 V2V / 多船协同（TBD-09 关闭）

**关键发现**：

- IMO MSC 110（2025-06）冻结大部分 MASS Code 章节，MSC 111（2026-05）将通过非强制版；**MASS Code 不强制要求 V2V**
- COLREGs 现行版本不修订，自主船仍受现行 COLREGs（基于声光信号 + AIS）约束
- 行业开放议题：EU VTMIS Directive 2002/59/EC + IMO MASS-JWG 在讨论"船-船 / 船-VTS / 船-ROC 信息流"，但无强制协议
- AIS（现行）+ VHF（现行）是当前唯一可用的工业级船间通信
- 工程建议：v1 架构**预留 V2V 接口**（消息总线增加 v2v topic 占位），但 v1 不实现

来源：

- [IMO 自主航运专题](https://www.imo.org/en/mediacentre/hottopics/pages/autonomous-shipping.aspx) · 访问时间 2026-04-27
- [EU 海事自主船舶（VTMIS Directive 2002/59/EC）](https://transport.ec.europa.eu/transport-modes/maritime/maritime-autonomous-ships-and-shipping_en) · 访问时间 2026-04-27
- [MSC 110 路线图分析](https://www.imo.org/en/mediacentre/meetingsummaries/pages/msc-110th-session.aspx) · 访问时间 2026-04-27

### 6.7 ABS CONOPS 模板内容（TBD-04 关闭，CCS 路径仅参考）

CCS 路径不强制 CONOPS，但 ABS CONOPS 内容是行业事实标准，CCS 智能系统认可可参考此结构。

ABS Advisory on Autonomous Functionality §4 要求 CONOPS 至少包含：

| 章节 | 内容 |
|------|------|
| **Operational Envelope** | 预期操作区域 + 限制 + 受限项 |
| **Operating Modes** | 海上 / 港内 / 引航 / 锚泊 / 应急 / 过渡 / 钻井 / 生产 |
| **角色与责任** | 各方角色（船员、ROC、岸基支持等）+ 责任划分 |
| **ROC 角色与责任详细** | 操作员位置、人员配置、人员资质、备份、岗位权限 |
| **与外部系统通信** | VTS、其他船、岸基服务的责任边界 |
| **网络安全** | ROC 接入与战术层之间的安全屏障 |
| **故障与降级响应** | 失联 / 失能 / 紧急情况下的响应预案 |

补充学术模板：**IEEE 1362-1998 ConOps Document** 提供更通用的 CONOPS 章节骨架，CCS 项目的 CONOPS 可结合 IEEE 1362 + ABS Section 4 复合使用。

来源：

- [ABS Advisory on Autonomous Functionality §4](https://maritimesafetyinnovationlab.org/wp-content/uploads/2020/09/ABS-Advisory-on-Autonomous-Functionality.pdf) · 访问时间 2026-04-27
- [SAFETY4SEA — ABS CONOPS 内容要点摘要](https://safety4sea.com/concept-of-operations-document-for-autonomous-systems-and-its-purpose/) · 访问时间 2026-04-27
- [Methodology for Approval of Autonomous Ship Systems CONOPS（DTU + Danish Maritime）](https://zenodo.org/records/6792507) · 访问时间 2026-04-27

### 6.8 DNV-CG-0264 (2024 ed.) 章节级细节（TBD-01 / TBD-02 降级为参考性）

DNV-CG-0264 章节结构（用于对照设计，CCS 路径不强制）：

| Sec. | 内容 |
|------|------|
| Sec.2 | 主要原则（goal-based + risk-based foundation）|
| Sec.3 | 评估流程：Sec.3 [2] 新概念审批；Sec.3 [4] 技术开发者验证流程 |
| Sec.4 | NAV 功能：远程控制导航功能配置和技术 |
| Sec.5 | ENG 功能：远程控制工程功能 |
| Sec.6 / Sec.7 | OPS / SAF 功能（2024 ed. 新增）|

DNV AROS 类标志组合公式（与 CCS R1/R2/A1/A2/A3 部分等价）：

`AROS-{NAV/ENG/OPS/SAF}({RC/DS/SA/FA}, {OB/OS/HC})`

DNV 与 CCS 标志映射近似关系（**项目级建议，需 CCS 与 DNV 双方书面确认才生效；本调研仅提供参考映射**）：

| CCS | DNV AROS 近似 |
|-----|---------------|
| N（基础辅助）| AROS-NAV(DS, OB) |
| Nx（高级自主）| AROS-NAV(SA, OB) 或 (SA, HC) |
| R1 | AROS-NAV(RC, HC) 或 (RC, OB) 含船员 |
| R2 | AROS-NAV(RC, OS) 无人 |
| A1 / A2 / A3 | AROS-NAV(SA, *) → (FA, *) 渐进 |

来源：

- [DNV-CG-0264 章节结构（GlobalSpec 摘要）](https://standards.globalspec.com/std/13057765/dnvgl-cg-0264) · 访问时间 2026-04-27
- [DNV AROS Class Notations 公开文档](https://www.dnv.com/maritime/autonomous-remotely-operated-ships/aros-class-notation/) · 访问时间 2026-04-27

### 6.9 BV NR 681 适用性确认（TBD-05 关闭）

BV NR 681 USV 适用范围：

| 条件 | 是否满足（FCB 场景）|
|------|-----------------|
| Unmanned surface vessel **(no human aboard)** | ❌ FCB 通常运送人员，不无人 |
| < 500 GT | ✅ 45m FCB 一般 < 500 GT |
| 钢 / 铝 / 复合材料建造 | ✅ |
| 自主等级 A1/A2/A3 + DC0（无直接控制）+ RC1/RC2/RC3 | 局部满足 |

**结论**：FCB 不属于 BV NR 681 USV 范畴（"no human aboard" 是硬约束）。

**用户确认走 CCS 单一路径**：FCB 应按 CCS 常规船入级 + 智能/自主标志组合。具体建议为 **i-Ship (Nx, R1, M, E, I)** 进阶选项（或 i-Ship (N, R1, M, E, I) 基础选项备选），最终标志组合在 Spec 阶段细化。**注：v1.0 中曾出现的 "N+" 写法在 CCS 标志体系中不存在，v1.1 已修订。**

来源：

- [BV NR681 USV (July 2022) 适用范围](https://www.intertekinform.com/en-us/standards/bv-nr681-2022-1303869_saig_bv_bv_3169824/) · 访问时间 2026-04-27
- [BV USV NR681 范围（PDF）](https://erules.veristar.com/dy/data/bv/pdf/681-NR_2022-07.pdf) · 访问时间 2026-04-27

### 6.10 FCB 高速半滑行段水动力建模（TBD-07 关闭）

**学术状况**：

- **MMG 标准方法**（Yasukawa & Yoshimura 2015）是国际公认的船舶操纵建模标准方法
- **高速船操纵建模**（Yasukawa et al. 2016, *J. Ship Res.* 60(4):239-258）专门覆盖高速船型
- **FCB 高速半滑行段（25-35 节）**：单体高速船设计文献已有水动力优化与捕获建模
- **不确定性处理**（针对 5 分钟前瞻预测的累积误差）：
  - **Robust MPC（Tube MPC, Min-max MPC）**：显式处理不确定性
  - **Gaussian Process based Data-Driven MPC**（Xue et al. 2022, *Ocean Engineering*）：在线辨识水动力参数
  - **MMG-based Event-Triggered MPC**：高效嵌入 MMG 模型
  - **Reinforcement Learning + NMPC + 数字孪生**（Reza et al. 2025, *Scientific Reports*）：在线模型自调优

**工程结论**：FCB 高速段不是学术空白，而是**多种成熟方法的组合问题**。Spec 阶段建议：
- 中程 MPC 内部预测模型 = MMG 3-DOF + Yasukawa 高速修正项
- 在 5 分钟时域内通过 Robust MPC（Tube）显式建模不确定性
- 可选：在线 GP 辨识或 SI 校正（作为辅助子模块，不在主决策路径）
- 关键参数（侧向力系数、阻尼系数等）需通过 CFD + 拖曳水池试验或 captive model test 标定

来源：

- Yasukawa & Yoshimura (2015). "Introduction of MMG standard method for ship maneuvering predictions." *JMST* 20(1):37-52
- Yasukawa et al. (2016). "High-speed ship maneuverability." *J. Ship Res.* 60(4):239-258
- Xue et al. (2022). "Data-driven model predictive control for ships with Gaussian process." *Ocean Engineering* · [URL](https://www.sciencedirect.com/science/article/abs/pii/S0029801822027032)
- Reza et al. (2025). "Digital twin syncing for autonomous surface vessels using reinforcement learning and nonlinear model predictive control." *Scientific Reports* · [URL](https://www.nature.com/articles/s41598-025-93635-9)
- KTH Master Thesis (2019). "Robust Model Predictive Control for Marine Vessels" · [URL](https://kth.diva-portal.org/smash/get/diva2:1299209/FULLTEXT01.pdf)

### 6.11 形式化前置依赖

- **SIL 分配**：战术层多个模块（避碰核心、模式管理）属 III 类计算机系统，参照 IEC 61508-3 SIL 2-3。**最终 SIL 分配在 Spec 阶段做项目级 HAZID + LOPA 分析后确定**
- **第三方评估**：自主航行系统须走 CCS 智能系统认可路径
- **FMEA / HAZID / HAZOP**：是。新增模式状态机（Fallback、ROC 接管）引入新失效模式，FMEA 必须覆盖
- **网络安全测试程序**（IACS UR E26 + CCS《船舶网络安全指南》(2024) 强制要求）：项目级义务

---

## 7. 置信度自评

| 章节 | 置信度 | 理由 |
|------|--------|------|
| §3 工业参考 | **H** | 5 个候选均有公开 URL + 厂商官方文档 + 工业落地案例；Avikus 有 DNV TA、Sea Machines 有 ABS AiP |
| §4 学术 | **H** | 8 篇核心文献均带 DOI；2 篇 2025 综述给出量化对比表；与工业实现交叉验证 |
| §5 候选方案对比 | **H** | 主方案推荐有学术 + 工业双重支撑；FCB 高速 MPC 精度 + ROC 接管 + AI 政策已通过 §6 关闭 |
| §6.1-6.4 CCS 合规 | **M-H** | CCS 标志体系 + 配套指南清单 + 智能系统认可原则均已通过公开来源核实；具体技术条款细节仍需用户购置规范原文确认 |
| §6.5-6.10 ROC 接管 / V2V / FCB 水动力 | **M-H** | 学术与行业实践证据充分；项目级具体数值需 Spec 阶段做 HAZID + 仿真后冻结 |

**整体置信度**：**H**

**TBD 关闭后保留的项目级风险**（Spec 阶段持续关注）：

1. CCS 规范原文章节级细节未读 —— 建议用户购置或获取 CCS《智能船舶规范》(2025) 出版物
2. ROC 接管硬限的项目级数值需 HAZID 后在 CONOPS 中冻结
3. FCB 高速 MPC 的水动力关键参数需 CFD + 试验标定
4. CCS 智能系统认可对每个智能子系统的具体认可清单 / 流程细节，需在 Spec 阶段直接联系 CCS 专家

---

## 8. 调研缺口（TBD Inventory）— 全部 13 项闭环说明

| ID | 内容 | 关闭状态 | 关闭依据 |
|----|------|---------|---------|
| TBD-01 | DNV-CG-0264 (2024 ed.) NAV 功能 Sec.4 各小节具体技术指标 | **降级为参考性**（CCS 单一路径不强制）| §6.8 提供章节级结构与 AROS 标志映射 |
| TBD-02 | DNV AROS 类标志各组合具体功能需求清单 | **降级为参考性**（CCS 单一路径不强制）| §6.8 提供 AROS 公式 + CCS-DNV 近似映射 |
| TBD-03 | CCS《智能船舶规范》(2025) N / R / A 标志技术要求详细条款 | **关闭** | §6.1 + §6.2 + §6.3 公开来源完整覆盖 |
| TBD-04 | ABS Pub. 323 CONOPS 模板格式 | **降级为参考性**（CCS 不强制 ABS CONOPS）| §6.7 提供 ABS Advisory §4 章节清单 + IEEE 1362-1998 通用模板 |
| TBD-05 | FCB 入级类别走 USV 还是常规船 | **关闭** | §6.9 + 用户确认走 CCS 单一路径：FCB 不属 BV NR 681；走 CCS 常规船 + i-Ship (Nx, R1, M, E, I) 进阶 / i-Ship (N, R1, M, E, I) 基础（v1.1 已修订 N+ 写法）|
| TBD-06 | ROC 接管时间硬限 | **关闭** | §6.5：船级社采用 goal-based + risk-based 框架，无固定秒级硬限；学术参考 20s/60s（Veitch 2024）；项目数值在 CONOPS 冻结 |
| TBD-07 | FCB 高速半滑行段 MPC 5 分钟前瞻精度 | **关闭** | §6.10：MMG（Yasukawa 2015）+ 高速修正（Yasukawa 2016）+ Robust MPC + 在线 GP/SI；非学术空白 |
| TBD-08 | DNV-CG-0264 + CCS 对 AI / ML 的政策 | **关闭** | §6.4：CCS GBS 开放式不禁 AI；III 类 CBS 高等级网络安全 + 软件可靠性证据；主决策白盒、辅助子模块可用 ML |
| TBD-09 | V2V 多船协同行业进展 | **关闭** | §6.6：MASS Code 不强制；v1 架构预留接口，v1 不实现 |
| TBD-10 | 项目级 CLAUDE.md / SDD / ICD 文档 | **关闭**（用户确认无）| 用户 2026-04-27 确认：目前无项目级文件 |
| TBD-11 | Kongsberg / Wärtsilä 闭源实现细节差异 | **保持开放**（非阻塞）| 闭源信息无法关闭；自研架构无需复制其实现 |
| TBD-12 | Sea Machines 是否有 ABS / DNV / CCS / BV TA | **关闭** | §3.4：SM200 已获 ABS 完整认可，SM300 已获 ABS AiP（2022-08，Foss tug Rachael Allen），SM300-NG 是 class-society approved 硬件；CCS / DNV / BV 公开资料中无明确 TA |
| TBD-13 | 项目目标船级社组合 | **关闭**（用户确认）| 用户 2026-04-27 确认：CCS 单一入级 |

**结论**：13 项 TBD 中，**10 项关闭、2 项降级为参照性、1 项保持开放（闭源信息，非阻塞）**。所有阻塞项均已闭环。

---

## 9. 给 Spec 的输入要点

### 核心决策建议

1. **架构范式：3T（Three-Tier）+ Backseat Driver + 多时间尺度混合避碰**
   - 依据：§3.1 Kongsberg + §3.5 MOOS-IvP + §4.1 Eriksen 2020
   - 置信度：**H**

2. **战术层模块划分（7 模块 + 1 模式管理器）**：态势聚合 / COLREGs 规则引擎 / 风险评估 / 中程避碰 (MPC) / 近距避碰 (BC-MPC) / 局部重规划 / 指令仲裁器；模式管理器
   - 依据：§3.3 Avikus 三段流水线 + §3.1 Kongsberg 4 模块组 + §4.1 Eriksen 三层混合
   - 置信度：**H**

3. **避碰算法选型**：中程 = MPC（COLREGs 8/13-17 + Rule 18 + Rule 19 部分约束嵌入）；短期 = BC-MPC 兜底
   - 依据：§4.3 + §4.1 + §4.2
   - 置信度：**H**

4. **多船型支持机制**：船舶动力学作为 MPC 内部参数化插件；FCB 首发但插件接口在 v1 即冻结；FCB 高速段用 MMG 3-DOF + Yasukawa 高速修正（§6.10）+ Tube MPC 不确定性显式建模
   - 依据：§6.10 + §3.3 + §3.4
   - 置信度：**H**

5. **模式管理状态机**：D2 / D3 / D4 三正常态 + MRC 兜底态 + ROC override 路径
   - **CCS 标志映射**：D2 ≈ N（基础辅助）+ R1 远程辅助；D3 ≈ Nx + R1（船上船员）/ R2（无人）；D4 ≈ A1 → A2 → A3 渐进
   - 接管硬限 = 项目级在 CONOPS 中冻结的具体数值（§6.5）；架构层支持 20s（紧急）/ 60s（监督）/ 5min（非紧急）三档作为初始设计基线
   - 依据：§6.1 + §6.5
   - 置信度：**M-H**

6. **接口契约**（5 组，**v1 即冻结**）：
   - 战略 → 战术：全局航路 + 航点 + 时序约束 + 优先级
   - 战术 → 控制：参考航向 / 航速 / 加速度（或参考轨迹）+ 紧急级别标志
   - 战术 ↔ ROC：模式切换请求 / 取消、override 命令、当前状态、心跳与超时
   - 战术 ← 感知融合：他船目标列表 + 静态障碍物 + ENC + 自船状态
   - 战术 ↔ 公共服务：日志、记录、健康监测、告警上报
   - 依据：§3.5 MOOS-IvP + §3.4 Sea Machines Connect/Control API
   - 置信度：**M-H**

7. **ML / DRL 边界**：仅用于辅助子模块（如他船意图预测、感知预处理、传感器融合下游）；**主决策路径白盒**
   - 依据：§6.4 CCS GBS 开放式 + III 类 CBS 软件可靠性证据 + §4.1 Burmeister 2021 文献统计
   - 置信度：**H**

8. **CCS 入级标志组合（FCB 首发推荐）**：**i-Ship (Nx, R1, M, E, I)** 进阶 / **i-Ship (N, R1, M, E, I)** 基础（v1.1 修订；详见 §6.9）
   - 依据：§6.1 + §6.9
   - 置信度：**M-H**（最终在 Spec 阶段与 CCS 专家确认）

9. **CCS 智能系统认可的最小提交件**（每个智能子模块独立认可）：
   - 软件可靠性 + 安全证据（参照 CCS《船用软件安全及可靠性评估指南》）
   - 网络安全证据（CCS《船舶网络安全指南》(2024) + IACS UR E27 Rev.1）
   - HAZID + FMEA + HAZOP 报告
   - 测试验证三段：虚拟仿真初试 + 模型测试中试 + 实船验证终试（§4.2）
   - V&V 文档 + 仿真用例覆盖矩阵
   - 依据：§6.2 + §6.3
   - 置信度：**H**

### 必须在 Spec 中显式提及的约束

- **CCS《智能船舶规范》(2025) 智能系统认可**：每个模块必须可独立认可（接口隔离 + 文档化）
- **CCS《船舶网络安全指南》(2024) + IACS UR E26 / E27 Rev.1**（2024-07-01 强制）：战术层属 III 类 CBS，必须满足高等级网络安全要求
- **COLREGs 当前版本**：MASS Code 通过前不修订；战术层必须严格遵循现行 Rule 5-19，对 Rule 2 / 17 留显式形式化处理
- **多船型扩展性**：船舶动力学插件接口在 v1 冻结
- **D2-D4 全谱 + Fallback**：模式状态机不能简化为单一等级
- **白盒主决策路径**：主决策模块禁用黑盒 ML；ML 限制在辅助子模块且需重型 V&V

### 项目级变量（在 Spec 阶段需用户/HAZID 确认的具体数值）

- 紧急 / 监督 / 非紧急三档接管时间预算
- 中程 MPC 预测时域（推荐起点：3-5 分钟）
- 近距 BC-MPC 预测时域（推荐起点：30-60 秒）
- DCPA / TCPA 触发阈值
- ROC 通信链路延迟容忍上限
- MRC（最小风险态）的具体定义（减速 / 锚泊 / 漂泊）
- SIL 分配（IEC 61508-3）

---

---

## 11. SIL 验证路径与场景模拟器（v1.1 新增）

### 11.1 章节范围与定位

本章节是 v1.1 合并的新章节，回答两个相关问题：

1. **SIL 分配方法学**（关闭 v1.0 项目级变量"SIL 分配 IEC 61508-3"中的开放项）
2. **避碰系统场景模拟器**（用户 2026-04-27 重点关注：用于"测试和展示避碰算法的场景模拟器"，技术栈倾向 Unreal/Unity 等游戏引擎驱动的高保真渲染）

**本章节不修改 §1-§9 的任何架构决策**，而是把 V&V 验证基础设施作为 Spec 阶段必须显式设计的独立章节进行调研储备。Spec §2 详细设计中将基于本章节产出"V&V 基础设施"专节。

### 11.2 SIL 分配方法学（项目级变量闭环）

#### 11.2.1 IEC 61508 / IEC 61511 框架

业界 4 种 SIL 等级（SIL 1 最低 ~ SIL 4 最高）和 5 种主要分配方法（Wikipedia *Safety integrity level* 与 IEC 61511-3 综合）：

| 方法 | 性质 | 工业地位 | 适用 |
|------|------|---------|------|
| **Layer of Protection Analysis (LOPA)** | 定量 | 工业最常用（特别是过程行业、化工、油气）；ANSI/ISA-S84 推荐 | 离散事件 + 独立保护层数量易确定 |
| **Risk Graph (RG)** | 定性 | IEC 61508-5 例子；初筛常用 | 早期评估、参数轴清晰的场景 |
| **Safety Layer Matrix (SLM)** | 定性 | 简化版 | 极初筛 |
| **Fault Tree Analysis / Event Tree Analysis (FTA/ETA)** | 定量 | 复杂场景的金标准 | 多重故障路径、定量风险评估 |
| **Risk Matrix** | 定性 | 简单 | 初步筛选 |

#### 11.2.2 自主船舶领域的迁移趋势

传统 LOPA 不能很好处理"软件控制系统的功能不充分性"（不是组件失效，而是控制结构 emergent 行为）。业界已系统性迁移到组合方法学：

| 方法 | 来源 | 与 LOPA 的关系 |
|------|------|---------------|
| **STPA (System-Theoretic Process Analysis)** | MIT Nancy Leveson；STPA Handbook 2018 | 替代 / 补充 LOPA，处理软件控制结构 emergent 失效 |
| **STPA + Software FMEA + Bayesian Network** | Park & Kim 2024 *MDPI JMSE* 12(1):4 | 综合方法，专门针对自主船舶 |
| **STPA-SynSS（安全 + 安保综合）** | Bolbot 等 *Ocean Engineering* | STPA 扩展，覆盖网络安全 |
| **STPA + ODD（运行设计域）** | *ScienceDirect* 2025-03 | 处理自主船舶在 OE/ODD 边界的退化态 |
| **EMSA RBAT（Risk-Based Assessment Tool）** | EMSA 专为 MASS 开发的方法 | 欧盟监管机构的官方风险评估工具 |
| **DNV SQ（System Qualification）流程** | DNV-CG-0264 Sec.3.3 | 船级社指南级流程，已签发首份合规声明（Ocean Infinity Armada 78 03） |

#### 11.2.3 关键文献

- **Pedersen et al. 2020** *Safety Science* 129:104799 "Towards simulation-based verification of autonomous navigation systems" — NTNU + DNV 总体框架
- **Skjetne & Egeland 2006** *Modeling, Identification and Control* 27:239-258 — 海事控制系统 HIL 测试基础
- **Smogeli & Skogdalen 2011** OnePetro 22018-MS — 第三方 HIL 测试安全关键船舶控制系统软件
- **Park & Kim 2024** *MDPI JMSE* 12(1):4 — STPA + Software FMEA + BN 综合框架，专门用于自主船舶
- **Frontiers Marine Science 2025** — STPA 在 MASS 退化态的应用
- **Wrobel, Montewka & Kujala 2018** *Reliability Engineering & System Safety* 178:209-224 — 自主商船系统理论模型
- **Dugan et al. 2023** *TransNav* 17(2):375-381 — STPA 在避碰决策支持系统集成测试中的应用（127 个 UCAs case study）

#### 11.2.4 X-in-the-loop 测试金字塔（业界共识）

```
                     ┌──────────────────┐
                     │  Sea Trial       │ 实船试航（最贵、最少次数）
                     │  (Field Test)    │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  HIL             │ 硬件在环（控制器+真实传感器+仿真模型）
                     │  (Hardware ITL)  │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  SIL             │ 软件在环（软件+仿真模型）
                     │  (Software ITL)  │
                     └────────┬─────────┘
                              │
                     ┌────────┴─────────┐
                     │  MIL             │ 模型在环（模型+模型）（最便宜、最多次数）
                     │  (Model ITL)     │
                     └──────────────────┘
```

每层有进入 / 退出准则，下一层接收上一层证据。这与 CCS 智能系统认可"虚拟仿真初试 → 模型测试中试 → 实船验证终试"三段式 V&V 完全对齐（§6.4 已建立）。**SIL（软件在环）是本章节场景模拟器（§11.3 + §11.4）的主要承载层**。

#### 11.2.5 推荐做法（FCB 项目，作为 Spec §2 详细设计输入）

1. **HAZID 阶段**（Spec §2 详细设计前置任务）：用 **STPA** 从战术层主决策路径反推 Unsafe Control Actions（UCAs）和 Hazardous Scenarios。参考 Dugan et al. 2023：避碰 DSS 案例已识别 127 个 UCAs。
2. **SIL 候选分配**：用 Risk Graph 做初筛 → LOPA 对关键 SIF 做定量 → Software FMEA 补充软件失效模式 → Bayesian Network 综合定量化（Park & Kim 2024 路径）
3. **战术层各模块的 SIL 候选**（Spec ADR 表 AD-23 已给出）：
   - 主决策路径（中程 MPC + BC-MPC + 仲裁器 + 模式管理器 + COLREGs 引擎）= **SIL 2-3 候选**
   - 辅助子模块（态势聚合、风险评估、局部重规划）= **SIL 1-2 候选**
   - **最终 SIL 由项目级 HAZID 后冻结**（必须有 CCS 验船师参与的 workshop）
4. **方法学顺序**：STPA（系统级）→ FMEA（组件级）→ BN 综合 → 量化 SIL → V&V 计划
5. **可参照模板**：DNV-CG-0264 Sec.3.3 SQ 流程；EMSA RBAT 工具；CCS 智能系统认可（§6.2 已建立）

### 11.3 避碰场景模拟器领域命名与方法学

#### 11.3.1 行业领域命名（标准化术语）

| 命名 | 来源 | 释义 |
|------|------|------|
| **"Scenario-based validation"** | 自动驾驶领域 → ASAM 标准 → 海事迁移 | 通用术语：用预定义场景集合验证自主系统 |
| **"Simulation-based verification of autonomous navigation systems"** | Pedersen et al. 2020 *Safety Science* | 海事专用学术说法 |
| **"Automatic test scenario generation"** | Torben et al. 2022 *Risk and Reliability* | 自动化场景生成方法学 |
| **"Scenario-based testing"** | DLR 2025 / MIT / ASAM 通用 | 场景驱动测试方法学的总称 |
| **"Smart testing of CAS using STL"** | Pedersen 2022 *J. Phys.: Conf. Ser.* 2618 | STL 形式化方法 |
| **"Virtual reality fusion testing"** | Zhu et al. 2022 *MDPI JMSE* 10(11):1588 | 虚实结合测试 |

#### 11.3.2 IMO MASS Code 五段式测试流程

参考 MDPI JMSE 13(8):1570 (2025-08) 综述论文 *"A Review of Research on Autonomous Collision Avoidance Performance Testing"*：

```
testing scenario construction      ← 场景构造（核心环节）
        ↓
virtual simulation testing         ← 虚拟仿真测试（MIL/SIL）
        ↓
model testing                      ← 模型测试（HIL + 物理水池）
        ↓
full-scale vessel testing          ← 实船试航（Sea Trial）
        ↓
evaluation and optimization        ← 评估与优化（闭环迭代）
```

#### 11.3.3 场景构造方法学三类（业界公认分类）

参考 MDPI JMSE 13(8):1570 (2025) + Zhu et al. 2022：

| 方法类 | 核心思想 | 主要技术 | 优势 | 局限 |
|--------|---------|---------|------|------|
| **Real Data-Driven** | 从历史 AIS / radar / VTS 数据中**提取**真实会遇场景 | AIS 解码 → MMSI 轨迹聚合 → 时空重叠提取 → 概率分布参数化 | 真实性高 | 罕见极端场景缺失 |
| **Mechanism Modeling-Based** | 基于 COLREGs / 海事规则的**机制重构** | 几何会遇拓扑（head-on / crossing / overtaking）+ 参数化 | 边界场景可控 | 真实性受机制简化影响 |
| **Machine Learning-Based** | 从数据中**学习**场景生成器 | GAN、扩散模型（DDPM）、强化学习 | 多样性、可生成稀有场景 | 训练数据需求大、可解释性弱 |

#### 11.3.4 关键学术工作

**NTNU + DNV 团队（业界领先）**：

- **Pedersen et al. 2020** *Safety Science* 129:104799 — 仿真验证总体框架
- **Torben et al. 2022** *Risk and Reliability* 237(2):293-313 — **Gaussian Process + Signal Temporal Logic (STL)** 自动化测试。核心思路：
  1. 用 STPA 推导测试需求
  2. 用 STL 形式化为可计算的鲁棒性度量（robustness metric）
  3. 用 Gaussian Process 估计 CAS 在参数空间的得分曲面
  4. 用 GP 不确定性引导新场景采样（贝叶斯优化）
  5. 在 STL robustness metric 下评估每个生成场景，定量化测试覆盖率
- **Pedersen et al. 2022** *J. Phys.: Conf. Ser.* 2311(1):012016 — STPA → 自动化测试场景生成
- **Glomsrud et al.** "Generating structured set of encounters for verifying automated collision and grounding avoidance systems" *J. Phys.: Conf. Ser.*
- **DNV "Modular assurance of an Autonomous Ferry using Contract-Based Design (CBD) and Simulation-based Verification Principles"** — 用 CBD 形式化 ODD 假设和系统行为保证

**AIS 数据驱动**：

- **Zhu, Zhou, Lu 2022** *MDPI JMSE* 10(11):1588 — 从 2900 个 AIS 衍生场景批量测试避碰算法
- *"Generation and complexity analysis of ship encounter scenarios using AIS data"* *Ocean Engineering* 2024-08（基于宁波-舟山港 AIS 数据，引入会遇拓扑演化复杂度评估）

**形式化场景生成（汽车业方法迁移）**：

- *"Towards Automated Test Scenario Generation for Assuring COLREGs Compliance of Autonomous Surface Vehicles"* *MODELS 2024 (ACM/IEEE)* — 用 model-driven engineering 形式化 COLREGs 场景；揭示了即使所有船遵守 COLREGs 也可能发生三船潜在碰撞（揭示规则歧义）
- **Bolbot et al. 2022** — Sobol 序列采样 + 风险度量过滤
- **Stankiewicz & Mullins 2019** *OCEANS 2019-Marseille* — 改进 ASV COLREGs 评估方法学

**实船验证**：

- **Kufoalor et al. 2020** *Journal of Field Robotics* 37(3):387-403 — 北海实船试验验证 ASV MPC 避碰，揭示了 Rule 2/5/7 在局部水域复杂性

**综述**：

- **MDPI JMSE 13(8):1570 (2025-08)** — 自主避碰性能测试综述

### 11.4 游戏引擎驱动的高保真仿真生态

> **用户技术栈倾向（2026-04-27 确认）**：Unreal Engine / Unity 等游戏引擎驱动的高保真渲染（不是 ROS 2/Gazebo，不是 MATLAB/Simulink 路径）。

#### 11.4.1 Unreal Engine 海事仿真生态

| 平台 | 类型 | 维护状态 | 关键特性 | 与本项目契合度 |
|------|------|---------|---------|--------------|
| **AILiveSim**（芬兰，商业）| 商业级海事 V&V 平台 | 活跃；2024+ 与 AMD Silo AI 合作 | 基于 Unreal Engine 5；真实传感器（雷达、摄像头、LiDAR、IMU、GNSS）；动态场景 + 自动化变化；用于芬兰 Åboat 自主船舶项目数字孪生 | **★★★★★** 直接契合 |
| **Kongsberg K-Sim Navigation**（挪威，商业）| 全任务桥楼仿真器（DNV 认证）| 活跃 | 高保真物理 + 水动力建模；DNV STCW 认证；用于韩国 Ulsan 自主船舶研究中心、AUTOSHIP 项目、Yara Birkeland、AutoBarge 等；DIS / HLA 标准接入 | **★★★★★** 工业头部标杆 |
| **Wärtsilä NACOS Platinum + Navi-Trainer**（芬兰，商业）| 多功能桥楼仿真 | 活跃 | DNV 认证；ECDIS / radar / conning 集成 | ★★★ 训练为主 |
| **HoloOcean** | 学术（BYU）| 活跃 | Unreal 4，水下 + 水面，多传感器 | ★★★ 偏水下 |
| **UNav-Sim** | 学术 | 活跃 | Unreal，水下 USV | ★★★ |
| **NVIDIA Omniverse / Isaac Sim** | 工业 SDK | 活跃 | RTX ray tracing；PhysX 5；多传感器；与 ROS 2 桥接 | ★★★★ 跨域可扩展 |
| **MathWorks Simulation 3D**（基于 Unreal 引擎嵌入）| 商业 SDK | 活跃 | Unreal 嵌入 Simulink；Simulation 3D Lidar/Radar block；与 Vehicle Dynamics Blockset 集成 | ★★★★ 关键集成路径 |
| **Maritime VR / FUNWAVE 集成（US Army Corps of Engineers）** | 学术研究 | 一次性 | Unreal + FUNWAVE 浅水波 | ★★★ |

#### 11.4.2 Unity 海事仿真生态

| 平台 | 类型 | 维护状态 | 关键特性 | 与本项目契合度 |
|------|------|---------|---------|--------------|
| **MARUS** | 学术开源 | 活跃 | Unity + ROS 2；丰富传感器；水面 + 水下；视觉算法重点 | ★★★★ 开源 + ROS 2 |
| **SMaRCSim**（瑞典 KTH WASP）| 学术开源 | 活跃 | Unity；多域（水下 + 水面 + 空中 + 陆地）混合保真度；ROS 2 桥接；适合学习与多智能体 | ★★★★ |
| **NTNU colav_simulator** | 学术（Tor Arne Johansen 团队）| 活跃 | 标准场景 + AIS 历史场景库；评估工具计算 COLREGs / 安全 / 性能罚分 | **★★★★★** 直接对接学术 SOTA |
| **Aeolus Ocean** | 学术（arxiv 2307.06688）| 一次性 | Unity，COLREGs DRL + USV | ★★★ |
| **Vasstein 2020 Autoferry Gemini** | 学术 | 一次性 | 实时电磁辐射传感器仿真 | ★★ |
| **Vagale 2021 Evaluation Simulator Platform** | 学术 *J. Marine Sci. Eng.* | 一次性 | ASV 扩展碰撞风险评估 | ★★★ |
| **Velodyne Unity Sensor SDK** | 工业 | 活跃 | Unity HDRP + ray tracing LiDAR；可参考方法论 | ★★★ |

#### 11.4.3 海况建模工具（不论 Unreal / Unity）

| 工具 | 平台 | 特性 |
|------|------|------|
| **NVIDIA WaveWorks 2.0** | Unreal 5 | 实时高保真海面模拟 |
| **FUNWAVE 3.4** | C 库（可导入 Unreal）| Boussinesq 浅水波 + 近岸 |
| **Niagara Ocean Simulation** | Unreal 原生 | Niagara 粒子系统 |
| **Realistic Ocean Simulator** | Unreal Marketplace 插件 | Beaufort 等级 0-12 海况 + 泡沫系统 |
| **Crest Ocean System** | Unity Asset Store | Gerstner + FFT 波 |
| **Ceto** | Unity Asset Store | 开源 |
| **UNIGINE 海事行业套件** | UNIGINE 引擎 | 实时 3D 引擎 + 数字孪生 |

#### 11.4.4 ROS 2 / Simulink 与游戏引擎的桥接

| 桥接路径 | 引擎侧 | 客户侧 | 性能 | 备注 |
|---------|--------|--------|------|------|
| **rosbridge** + websocket | Unreal / Unity | ROS 2 | 中 | 通用，跨语言 |
| **ROS-TCP-Connector** | Unity | ROS 2 | 高 | Unity 官方推荐 |
| **UnrealROSBridge / rclUE** | Unreal | ROS 2 | 高 | Unreal 第三方 |
| **NVIDIA Isaac Sim ROS 2 桥接** | Omniverse / Isaac Sim | ROS 2 | 高 | NVIDIA 官方维护 |
| **MathWorks Simulink 3D Animation block** | Unreal（嵌入）| Simulink | 高 | MathWorks 官方支持，自动同步实时步长 |
| **DDS 直接接入** | 任意（C++ binding）| ROS 2 / 其他 DDS 客户端 | 最高 | 自研路径 |
| **HLA / DIS** | Kongsberg K-Sim 等 | 任意 HLA 兼容 | 中 | 国防 / 多仿真器联邦 |

#### 11.4.5 场景描述语言（汽车业标准向海事迁移）

| 标准 | 版本 | 性质 | 海事迁移情况 |
|------|------|------|------------|
| **ASAM OpenSCENARIO XML** | 1.x（最新 1.2） | XML schema，concrete scenarios | 海事尚无直接对应；学术界已开始借鉴（汽车 → 海事的方法论迁移）|
| **ASAM OpenSCENARIO DSL** | 2.1 (2024-03)；从 OSC 2.x 演进 | DSL，abstract / logical / concrete 三抽象级 | 海事尚无直接迁移，但抽象层次理念已被 *MODELS 2024* 论文借鉴用于 COLREGs |
| **ASAM OpenDRIVE** | 1.7 | 静态道路网络 | **海事无对应**（电子海图 ENC 是替代）|

**现状评估**：海事专用场景描述语言尚未标准化。可能的路径：

- **短期**：自定义 YAML / XML schema（参考 NTNU colav_simulator 已有格式）
- **中期**：跟踪 ASAM 是否扩展到海事；可参考 OpenSCENARIO DSL 的抽象层级理念自研
- **长期**：等 IMO MASS Code 强制要求出现后，可能产生海事 OpenSCENARIO 标准（IACS 或 ASAM 主导）

### 11.5 验证基础设施栈选项（FCB 项目，调研结论；属 Spec 阶段决策）

> **声明**：以下是基于调研给出的工程参考。**本调研不做最终选型决策**——选型是 Spec § 2 详细设计的任务（Spec ADR 中将引入新的 ADR 处理）。

#### 选项 A · 商业级商用（最快上手 + 最高保真）

```
[算法侧]              [桥接]              [仿真侧]
战术决策层 (C++)       DDS / TCP          Kongsberg K-Sim
COLREGs 引擎      ───────────────►   - DNV 认证
中程 MPC + BC-MPC                    - 已用于 AUTOSHIP / Yara Birkeland
模式管理器                            - 物理 + 水动力建模 + 视觉
                                      - HLA / DIS 联邦
                  ◄───────────────  
                  传感器 / 状态反馈

成本：高（License + 集成）
保真度：★★★★★
认证适合度：★★★★★（DNV 认证已有）
```

#### 选项 B · 商业海事 + Unreal（中等成本，最高视觉保真）

```
[算法侧]              [桥接]              [仿真侧]
战术决策层 (C++)       rosbridge / DDS    AILiveSim (Unreal Engine 5)
                                          - 海事专用 V&V
                                          - 真实传感器仿真
                                          - 已用于 Åboat 自主船舶
                                       
                                          或 NVIDIA Omniverse + Isaac Sim
                                          - RTX ray tracing
                                          - PhysX 5

成本：中
保真度：★★★★★（视觉）/ ★★★★（物理）
认证适合度：★★★★（需要补 V&V 证据）
```

#### 选项 C · 开源 Unity + ROS 2（最低成本，最大定制空间）

```
[算法侧]              [桥接]                [仿真侧]
战术决策层 (ROS 2)     ROS-TCP-Connector    MARUS / SMaRCSim (Unity)
                                            - 多船型支持
                                            - 海事传感器
                                            - ROS 2 原生

                  AIS 历史数据 → 场景库（colav_simulator 适配）
                  STL + Gaussian Process → 自动场景生成（Torben 2022 复现）

成本：低（人力为主）
保真度：★★★★（视觉）/ ★★★（物理）
认证适合度：★★★（V&V 证据完全自建）
```

#### 选项 D · MATLAB / Simulink + Unreal（与算法集成最紧）

```
[算法侧]              [集成]                [仿真侧]
战术决策层            Simulink 3D Block     Unreal Engine（嵌入）
（MATLAB/Simulink）                          - Simulation 3D Lidar
                                            - Simulation 3D Radar
                                            - Vehicle Dynamics Blockset

（不在用户技术栈倾向中，作为参考）

成本：中
保真度：★★★★（视觉）/ ★★★★（物理）
认证适合度：★★★★（MathWorks 工具链可追溯）
```

#### 11.5.1 调研给出的工程建议（Spec 决策时参考）

**对于 FCB 项目**：

- **MIL/SIL 早期**：选项 C（Unity + ROS 2 + colav_simulator）—— 快速迭代，低成本
- **SIL 后期 + HIL**：选项 B（AILiveSim Unreal）或选项 A（K-Sim） —— 高保真传感器 + COLREGs 场景库
- **商业认证阶段**：选项 A（K-Sim） —— DNV 已认证，CCS 接受相对容易（K-Sim 是 IACS 内的国际通用工具）
- **场景生成方法学**：跨选项独立。强烈建议采用 **STPA → STL → Gaussian Process** 自动化（Torben 2022 + Pedersen 2022 方法论），并结合 **AIS 历史数据驱动**（Zhu 2022 方法论）做混合场景库

### 11.6 与其他章节的关联

- **§5（CCS 智能系统认可路径）**：本章节 §11.2 SIL 分配方法学 + §11.4 场景模拟器是 CCS 认可"虚拟仿真初试 → 模型测试中试 → 实船验证终试"三段式 V&V 的具体技术实现
- **§6.2 智能系统认可**：本章节 §11.5 验证基础设施栈是 III 类 CBS 认可的 V&V 证据基础
- **§7 决策建议**：本章节为 Spec 阶段引入"V&V 基础设施"决策提供素材（不在 §7 既有决策范围内，而是作为 Spec 阶段新增决策）

### 11.7 关键文献清单（用于 Spec 阶段深入）

| # | 文献 | 核心贡献 | 重要度 |
|---|------|---------|------|
| 1 | Pedersen et al. 2020 *Safety Science* 129:104799 | 仿真验证总体框架 | ★★★★★ |
| 2 | Torben et al. 2022 *Risk and Reliability* 237(2):293-313 | GP + STL 自动化测试 | ★★★★★ |
| 3 | Pedersen et al. 2022 *J. Phys.: Conf. Ser.* 2311(1):012016 | STPA → 自动场景生成 | ★★★★★ |
| 4 | Kufoalor et al. 2020 *Journal of Field Robotics* 37(3):387-403 | 北海 ASV 实船验证 | ★★★★ |
| 5 | Zhu, Zhou, Lu 2022 *MDPI JMSE* 10(11):1588 | AIS 数据驱动 2900 场景 | ★★★★ |
| 6 | Bolbot et al. 2022 | Sobol 序列采样 + 风险过滤 | ★★★★ |
| 7 | MDPI JMSE 13(8):1570 (2025-08) | 综述：自主避碰性能测试 | ★★★★★ |
| 8 | *MODELS 2024 (ACM/IEEE)* | OpenSCENARIO 思路迁移海事 | ★★★★ |
| 9 | Bingham et al. 2019 *MTS/IEEE OCEANS* | VRX (Gazebo 海事仿真) | ★★★ |
| 10 | Vekinis & Perantonis 2023 *arxiv 2307.06688* | Aeolus Ocean (Unity DRL) | ★★★ |
| 11 | Cieślak 2019 *OCEANS Marseille* | Stonefish 开源水下 | ★★ |
| 12 | Park & Kim 2024 *MDPI JMSE* 12(1):4 | STPA + Software FMEA + BN | ★★★★ |
| 13 | Dugan et al. 2023 *TransNav* 17(2):375-381 | STPA 集成测试避碰 DSS | ★★★★ |
| 14 | *arxiv 2603.02487 (2026-03)* | Robust Simulation Framework + Unity Simulink | ★★★★ |
| 15 | DNV-CG-0264 + AROS Class Notation (2025-01) | DNV 自主船 SQ 流程 | ★★★★ |

### 11.8 §11 章节置信度

**H**（所有结论均有公开文献或工业实证支撑，无幻觉项）。

**边界**：
- 验证基础设施栈选型属 Spec § 2 详细设计任务，本章节不做最终决策
- SIL 数值最终值依赖项目级 HAZID workshop（CCS 验船师 + 用户 + 船东三方参与）
- 海事专用场景描述语言尚未标准化，建议短期用自定义 YAML schema

---

## 10. 变更历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|---------|
| v0.1 | 2026-04-27 | 用户 + Claude | 初稿，13 项 TBD 待关闭 |
| v0.2 | 2026-04-27 | 用户 + Claude | **TBD 全 13 项闭环**：CCS 单一入级确认（TBD-13）；FCB 走常规船入级（TBD-05）；项目无 CLAUDE.md 确认（TBD-10）；CCS 标志体系（TBD-03）+ 配套指南（TBD-03）+ 智能系统认可（TBD-03）+ AI 政策（TBD-08）+ ROC 接管（TBD-06）+ V2V（TBD-09）+ FCB 高速 MPC（TBD-07）+ Sea Machines TA（TBD-12）+ ABS CONOPS（TBD-04 降级）+ DNV CG-0264 章节（TBD-01/02 降级）；TBD-11 闭源信息保持开放（不阻塞）|
| **v1.0** | **2026-04-27** | **用户 + Claude** | **用户冻结**；进入阶段 B（Spec）；产出 Spec Part 1（BLUF + ADR 表，33 条 ADR） |
| **v1.1** | **2026-04-27** | **用户 + Claude** | **合并 Patch-A**：(1) 修订 N+ 写法（CCS 标志体系实际无 N+，§6.1 §6.9 多处修订）；(2) 新增 §11 SIL 验证路径与场景模拟器章节（含 SIL 分配方法学、IMO MASS Code 五段式测试流程、场景构造方法学三类、Unreal/Unity 海事仿真生态、4 种验证基础设施栈选项）；(3) 关闭 Spec Part 1 识别的 6 项 [TBD]（LOW-01 ~ LOW-06）；(4) FCB 入级建议改为 i-Ship (Nx, R1, M, E, I) 进阶 / i-Ship (N, R1, M, E, I) 基础双方案 |

---

## 附录 A · 候选架构与三张可视化图

本调研附带 3 张架构可视化图（Claude 在对话中渲染）：

- **图 1 · 战术决策层系统上下文**：三层架构（战略 / 战术 / 控制）+ 感知融合输入 + ROC 双向接口 + 船舶执行机构
- **图 2 · 战术决策层内部模块**：7 模块 + 模式管理器
- **图 3 · 模式管理状态机**：D2 ↔ D3 ↔ D4 + MRC 兜底 + ROC override

图 2、图 3 是 §9 决策建议的视觉对应。

---

*本 Research v1.1 已冻结，是后续 Spec 阶段的唯一真源。Spec 中的任何决策不得超出本调研的证据范围；如需扩展，要么先更新本调研到 v1.2，要么在 Spec 中显式标记为 [TBD]。*

*v1.1 在 v1.0 基础上合并了 Patch-A（6 项 [TBD] 关闭报告 + 避碰 SIL 验证场景模拟器深度调研 + N+ 写法修订），主要架构决策（§1-§9）保持不变；新增 §11 SIL 验证路径与场景模拟器章节，作为后续 Spec § 2 详细设计的输入。*
