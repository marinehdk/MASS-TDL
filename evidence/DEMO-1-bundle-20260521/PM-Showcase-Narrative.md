# DEMO-1 Showcase PM Narrative: What This Means for TDL

## 1. 概述与核心突破 (Overview & Core Breakthroughs)
DEMO-1（基于 IMAZU-08 R14 对遇场景的实船闭环战术规避）的成功跑通，代表了 MASS ADAS L3 战术决策层（TDL）在核心控制范式上的重大工程里程碑。这不仅是一次全功能链的 SIL 联调，更是对我国自主智能船舶规避决策系统的一次核心能力背书。

## 2. Y轴反射弧控制回路闭环 (Y-axis Reflex Arc Control Loop)
在闭环控制方面，DEMO-1 首次打通了**Y轴反射弧（避碰控制回路）**：
- **感知与跟踪**：`tracker_mock` 提取障碍船状态并发布 `/sil/tracked_targets`。
- **避碰决策与规划**：L3 内核的避碰决策模块（M6 COLREGs 避碰规则推理机与 M5 BC-MPC 避碰航路规划器）实时接收态势，根据《1972年国际海上避碰规则》（COLREGs）第十四条（对遇局面）进行规则判定，自动计算避让航向与航速，并向下发布避让控制指令。
- **执行与运动反馈**：SIL 容器内的运动学模块（`ship_dynamics`）实时执行操舵，并通过毫米级运动解算反馈至网页端 HMI。

此回路实现了从“态势感知 → 规则推理 → 轨迹规划 → 执行操舵”的无缝闭环，确保自主规避的操纵灵敏度与路径安全性。

## 3. Z轴硬连线急停保护与双层核决 (Z-axis Hardware Override & Doer-Checker)
在安全性架构上，系统严格遵循 **Doer-Checker（执行-核决）** 的双安全范式：
- **常规避碰**：M5 / M6（Doer）主导航向调整；
- **安全边界核决**：M7 Safety Validator（Checker）持续对规划轨迹进行时空碰撞区校验，拥有终极的一票否决权（Veto）。
- **Z轴硬连线安全保障**：对于极端失控或硬件失效态势，系统集成了零软件介入的**Z轴硬件急停保护**（Bypassing Software Override）。一旦触发极限边界，硬件继电器直接断开执行机构，将操舵模式安全降级至人工应急接管。

## 4. 中国船级社 (CCS) 型式认可与工程价值 (CCS Type Approval Value)
本演示包中包含的五项必需交付物，特别是高度结构化的 **ASDR (Autonomous Safety & Decision Registry) 决策注册日志**，为我国自主船舶安全合规性树立了标杆：
1. **数据存证不可篡改**：ASDR 日志完整记录了每次主导决策的逻辑脉络与时间戳，支持追溯每一个操舵动作对应的避碰规则判断，满足 IACS UR E26/E27 网络安全与数据审计要求。
2. **场景完备覆盖**：通过 SIL 数字孪生环境的 IMAZU 22 项基准对遇测试，在实验室阶段即实现了全空间覆盖的置信度检验。

这为后续通过中国船级社（CCS）针对 L3/L4 级自主航行操纵控制系统的型式认可（Type Approval）奠定了最为坚实的客观证据链路。
