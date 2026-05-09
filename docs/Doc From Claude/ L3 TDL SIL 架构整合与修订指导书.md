好的，以下是根据我们两次深度对话的完整结论，生成的一份可直接用于修订《架构设计报告》和《开发计划》的精确指导文档。每个修改点都附带了来自你提供的SIL研究报告、NTNU论文及认证实践的明确证据，杜绝“拍脑袋”决策。

---

# L3 TDL SIL 架构整合与修订指导书

**版本**：v1.0  
**日期**：2026-05-09  
**目的**：将零散的SIL架构研究与决策，整合并细化到《MASS ADAS L3 TDL 架构设计报告》（下称架构文档）和《MASS ADAS L3 8个月完整开发计划》（下称开发计划）中，确保每个修订都有认证依据或工程证据支撑。

## 1. 背景与修订目标

在D0冲刺完成、即将进入Phase 1之际，我们需要对SIL（软件在环）框架进行最终技术定型和文档固化。经过两次深度研究（产出两份SIL对比报告）、对NTNU `colav-simulator`架构论文的评审，以及与你的架构交互，我们已达成一个清晰的路线图。

本次修订的核心目标是：

1. **锁定“选项D”混合架构为唯一技术方案**，并移除架构文档中对其他替代方案的不确定描述。
2. **明确SIL框架的认证属性**：它并非随船交付产品，而是V&V（验证与确认）体系的核心组成部分，其产出的证据直接支撑CCS i-Ship (No) 认证。
3. **将SIL架构细节和工程实践注入架构文档和开发计划**，使Phase 1-3的任务有明确的“施工图”。
4. **对齐认证证据链**，确保架构设计满足CCS《智能船舶规范2024》和DNV-CG-0264等规范的白盒可审计要求。

## 2. 核心修订结论

### 2.1 技术路线：最终采用“选项D”混合架构

**结论**：全面采用两份SIL研究报告中所推荐的 **“选项D: Hybrid (ROS2-native SIL kernel with FMI/maritime-schema interface at the boundary)”** 。这是唯一能同时满足“测试真实代码”和“生成标准认证证据”双重约束的路径。

**修订证据**：
- 第一份SIL报告指出：“Options A and C couple your certification evidence to a Python orchestration that is *not* the production binary — the CCS surveyor will ask which artefact corresponds to the binary that runs onboard.”
- 第二份SIL报告强调：“Build your Stage-1 SIL stack on the … ROS2-native SIL kernel … This is the only stack today that is simultaneously open-source, commercially usable, FMI-conformant, and already being accepted by a Tier-1 class society as V&V evidence.”

### 2.2 SIL定位：认证证据生成器

**结论**：SIL框架是开发过程中进行软件验证的工具，其本身需通过**仿真器鉴定**（对应D1.3.1任务），以满足认证对工具置信度的要求；其产出的测试场景、覆盖率报告和ASDR日志，是CCS AiP提交的证据包主体。

**修订证据**：
- DNV-RP-0513明确要求仿真模型和工具本身的质量保证证据，这是V&V被接受的前提。
- ISO 26262-8对软件工具鉴定的要求（TCL3级别）是海事认证的类比基准。

## 3. 对《架构设计报告》的精确修订

### 3.1 新增 / 重写章节：SIL架构与V&V基础

**当前缺陷**：架构文档v1.1.2未对SIL框架做独立定义，缺乏对测试边界、模拟层策略和认证证据生成流程的描述。

**修订动作**：

1. **新增章节：SIL框架与测试基础设施**（可置于§4之后或作为§15的姊妹篇）
   - **内容包含**：
     - **架构图**：直接引用第二份报告中的“Layer Interface Diagram”或基于你的交互式HTML SVG，新增一节详细描述SIL边界、L1/L2模拟层、FMI动力仿真接口和CCS证据生成器的关系。
     - **原则声明**：“L3 TDL的生产C++二进制文件（M1-M8）直接在SIL内核中运行，所有SIL测试结果可直接追溯至目标系统。” 这是对抗“测试包装器而非产品”风险的措施。
     - **模拟层策略**：精确复制第二份报告中的“three-tier mock policy”，规定L1（感知）的三种模式（synthetic/rosbag/ais_replay）、L2的YAML桩、动力仿真的MMG插件化设计。
     - **认证证据生成器**：描述CI如何通过`launch_testing`和`farn`自动生成Parquet格式的“覆盖立方体”报告，并与场景文件、HAZID ID、git commit hash相关联。

2. **修改现有§7（M3）和§15（接口契约）**：
   - 在§7中明确：M3的`RouteReplanRequest`接口是为SIL中的L2桩设计的，实际部署时与L2 Voyage Planner通信，但SIL中通过桩模拟。
   - 在§15.1中增加由SIL特有的模拟层消息定义（如`MockWorldModelMsg`），并明确标注这些消息仅在SIL阶段存在，不进入生产总线。

**证据映射**：
- SIL报告A：“We adopt a three-tier mock policy, varied per test campaign by ROS2 launch arguments.”
- SIL报告B：“This gives CCS-grade evidence quality … keeps the production ROS2 Jazzy nodes as the tested artifact.”

### 3.2 明确RL边界与认证隔离

**当前缺陷**：架构文档§13多船型参数化设计和v1.1.3 stub中提及RL，但未明确RL环境与认证内核的隔离边界。

**修订动作**：
在架构文档中新增一节“RL工作台与认证内核的隔离”（或在§13或独立的SIL章节中）。
- **强制规定**：“绝对禁止任何RL训练循环导入或由L3认证内核的ROS2节点导入。RL训练在独立容器（如`rl_workbench`）中进行，通过Gymnasium适配器调用相同的MMG FMU和场景YAML，但不接触C++代码。”
- **Artefact回注通路**：“训练后的策略导出为ONNX，通过`mlfmu`打包为FMU，再由认证内核通过`libcosim`加载。此时策略FMU本身需经过DNV-RP-0510/0671的数据驱动模型鉴定。”
- **代码层面**：`/src/l3_tdl_kernel/**`和`/src/rl_workbench/**`在Git中为独立包，CI lint检查交叉引用即报错。

**证据映射**：
- SIL报告A：“The boundary is enforced at three levels: Repo-level, Process-level, Artefact-level.”
- SIL报告B：“Keep the safety-critical SIL kernel in a frozen, version-tagged repo subject to DNV-RP-0513 model-assurance; let RL training, scenario fuzzing and adversarial generation live *outside* that boundary.”

### 3.3 场景库与覆盖立方体方法论的固化和引用

**当前缺陷**：架构文档§3 ODD框架描述了子域，但缺少场景生成与覆盖率分析的系统方法。

**修订动作**：
在架构文档中增加对场景库设计的引用（或与§15.2接口矩阵并列）：
- **场景定义格式**：直接引用第二份报告中的YAML示例（`title: "Crossing-from-port, FCB own ship, two targets, Beaufort 5..."`），并规定所有场景必须遵循扩展后的`maritime-schema` `TrafficSituation`格式。
- **覆盖立方体**：明确定义“11 COLREGs rules × 4 ODD subdomains × 5 disturbance levels = 220 cells × ≈5 seeds ≈ 1100 concrete cases”的覆盖率方法，并将其与CCS认证要求绑定。
- **基准集**：强制规定Imazu-22为回归测试集，任何核心算法变更必须重新通过全部22个场景。

**证据映射**：
- SIL报告A：“Build the CCS evidence package around a 1,100-cell coverage cube… Map artifacts directly to CCS Rules for Intelligent Ships 2024.”
- SIL报告B：“Anchor on the Imazu 22-case canonical set + the COLREG Rules 5–10 / 13–19 grid, parameterise via Sobol sampling… target a coverage-cube of 11 rules × 4 ODD subdomains × 5 disturbance levels.”

## 4. 对《开发计划》的精确修订

### 4.1 增强Phase 1的SIL基础设施任务细节

**当前缺陷**：D1.3a (MMG + AIS管道) 和 D1.3b (场景管理 + 调试HMI) 的任务描述过于概括，未与“选项D”架构绑定。

**修订动作**：

1. **D1.3a 标题和范围扩充**：
   - **原标题**：D1.3a · 4-DOF MMG 物理仿真器 + AIS 历史数据管道
   - **修订为**：**D1.3a · `ShipMotionSimulator` 抽象层与FCB MMG插件 + AIS数据管道**
   - **修订内容**：明确要求实现第二份报告中描述的C++抽象类 `ShipMotionSimulator`，MMG模型作为插件（FCBPlugin）封装。接口需支持后续FMI导出，但不强制Phase 1实现FMI封装。
   - **新增DoD**：必须通过`ShipMotionSimulator`抽象运行，支持YAML配置切换vessel_class。

2. **D1.3b 场景格式强制要求**：
   - **Scope新增**：所有场景YAML必须通过`maritime-schema`的`TrafficSituation` schema验证。编写的10个基础场景必须包含文档中列出的`metadata`扩展字段（scenario_id, hazid_refs, colregs_rules等），建立与认证追溯矩阵的链接。
   - **Demo Charter新增**：展示时需突出场景YAML与`maritime-schema`的兼容性。

3. **D1.3.1 (仿真器鉴定报告) 的目标强化**：
   - **依据补充**：在报告中明确引用DNV-RP-0513 §4模型保证，说明该鉴定报告是对标船级社要求的工具置信度证据。

**证据映射**：
- SIL报告A：“The same plugin object serves all three duties (own-ship SIL, target-ship SIL, RL environment) by being instantiated multiple times… do **not** fork the model.”
- 开发计划原v3.0 D1.3a仅提及`ship_model.predict_response()`接口，未强调抽象化的重要性和认证价值。

### 4.2 将场景覆盖立方体和评价指标融入Phase 2/3

**当前缺陷**：D2.5 (SIL集成测试), D3.6 (SIL 1000+) 虽提及场景数量，但缺乏对覆盖立方体和结构化评价指标的具体要求。

**修订动作**：

1. **D2.5 (M1-M6 SIL集成测试报告) 的V&V标准细化**：
   - 明确50个综合测试场景的选取必须符合“11规则 × 4 ODD × 典型扰动”的抽样原则，不能随机挑选。
   - 要求输出第一版的“覆盖立方体”热图，即使覆盖率不高，也为后续D3.6建立基线。

2. **D3.6 (SIL 1000+ 场景 COLREGs 覆盖率报告) 与认证证据深度绑定**：
   - **修改目标**：明确这1000+场景就是CCS AiP提交的**证据制品#5**（覆盖率报告）的核心内容。
   - **补充交付物**：不仅要有通过率，还要生成与Parquet证据文件对应的`traceability.csv`，实现“scenario_id → hazid_ref → colreg_rule → verdict”的完整追溯链。
   - **引入NTNU论文的评价方法**：在D3.6的DoD中增加一项：“集成NTNU Hagen(2022) / Woerner(2019)风格的基于规则和阶段（如让路船/直航船的动作延迟、幅度不足惩罚）的量化评分系统，为每个场景生成结构化的合规分数，而非简单的PASS/FAIL二进制结果。” 这将极大增强CCS对系统“懂规则”的认可度。

**证据映射**：
- NTNU论文（Hagen 2022, Woerner 2019）及你提供的PDF中描述的“惩罚与评分机制”（如Table I, II的`Pdelay`, `S14`等），这正是CCS乐于看到的精细评估方式。
- SIL报告A：“Use `trafficgen gen-situation` for parametric coverage of head-on/crossing/overtaking with relative-bearing sweeps; the tool ships baseline cases under MIT.”

### 4.3 提前介入HMI认证元素

**当前缺陷**：D3.4 (M8 HMI完整)和D2.6 (船长Ground Truth) 侧重于可用性，但缺少将其与“透明性认证标准”直接挂钩的表述。

**修订动作**：
在D3.4和D2.6的DoD中增加：
- M8的SAT-2/3视图必须实时展示：**当前COLREGs规则依据（如Rule 15 crossing）、当前扮演角色（give-way/stand-on）、系统正在执行的参考行动（如'altering course to starboard, target 30°'）**。这正是你HTML原型所展示的关键信息。
- 船长可用性测试必须包含场景：在系统做出决策时，向船长询问“您是否理解系统为何如此行动？”，并记录理解分数。这直接对应IMO MASS Code对“有意味的人为干预”的要求。

**证据映射**：
- 你的HTML交互原型本身就是一个强有力的证据雏形，展示了如何将决策透明化。
- 架构文档§12.2的SAT三层模型需要落地，此修订正是其工程实现。

## 5. 证据与信息来源追溯表

| 修订点                        | 来源文档       | 具体章节/页面                                                | 核心证据                                                     |
| ----------------------------- | -------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 3.1 SIL边界与三模模拟层       | SIL报告A       | “Layer Interface Architecture in SIL (Q2)”                   | “three-tier mock policy… synthetic, rosbag, ais_replay”      |
| 3.1 认证证据追溯的Parquet格式 | SIL报告A       | “Traceability artifact (one CSV row per test run)”           | “Persist as Parquet … aligning with maritime-schema's output table convention so DNV/CCS surveyors get a familiar format.” |
| 3.2 RL边界的三层隔离          | SIL报告A       | “RL boundary architecture (Q5)”                              | “Boundary rule: No RL artefact … is permitted to import or be imported by any ROS2 node that is part of the L3 certified kernel.” |
| 3.3 覆盖立方体和YAML格式      | SIL报告A       | “Scenario library architecture (Q4)”, “Concrete scenario YAML” | 1100-cell cube定义和完整YAML示例                             |
| 3.3 Imazu-22基准集            | SIL报告B       | “The 1000+ scenario library, anchor on the Imazu 22-case canonical set” | “The Imazu problem (22 canonical encounters) is the universally accepted minimum benchmark.” |
| 4.1 C++抽象插件模式           | SIL报告A       | “Plugin pattern”                                             | `ShipMotionSimulator` 抽象类代码片段和`FCBPlugin`实现        |
| 4.2 细粒度的COLREGs评分       | NTNU论文 (PDF) | Section II.C, Table I & II                                   | 包含`safety score`, `rule score`, `delay penalty`等的评估方法定义 |
| 4.3 决策透明化HMI的设计原则   | 架构文档v1.1.2 | §12.2 SAT-1/2/3 自适应触发表                                 | “SAT-2 推理层按需触发 … (1) COLREGs 冲突检测 (2) M7 SOTIF 警告激活 …” |
| 全局：仿真器自身鉴定要求      | SIL报告B       | “Tool-qualification analog: Maritime has no published TCL-3-equivalent. Closest reference is DNV-RP-0513.” | DNV-RP-0513作为仿真工具置信度的论证依据                      |

---

*这份指导书旨在提供可执行的修改路线图。所有建议均紧密围绕你已论证正确的“选项D”架构，并将分散的决策收敛为可以对架构文档和开发计划进行精确更新的条款。如需对某个章节的具体修订文字进行细化，我可以随时进行下一步工作。*