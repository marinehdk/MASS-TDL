# Figma ↔ D1.3.2.3 Web HMI · IEC 62288 SA Subset 对齐文档 · D2.6

| 版本 | 2026-06-16 v1.0 |
|---|---|
| 关联 | D1.3.2.3 Web HMI / 架构 §12 M8 / D2.8 §21 |

> **注：** IEC 62288:2021 AMD1:2024 具体子条款号须 HF 咨询 onboard 后核实，当前标注 [R35-§TBD]。

## 对照表

| 显示要素 | D1.3.2.3 实装 | Figma 船长层 | Figma ROC 层 | 标准依据 |
|---|---|---|---|---|
| 当前自主等级（D2/D3）| `/l3/sat/data.mode` @1Hz | 大图标（≥48px）/ 颜色编码 | 文字 + 精确模式 ID | IEC 62288:2021 [R35-§TBD] |
| ODD 子域 + Conformance | `/l3/odd_state` @1Hz | 颜色指示灯 + 单词（NORMAL/DEGRADED）| 数值 + 百分比（conformance_score）| IEC 62288:2021 [R35-§TBD] |
| 目标船威胁列表 | `/l3/tracks` @10Hz | Top-3 威胁卡（图标 + 名称 + 距离）| 完整列表（CPA/TCPA/conf/heading）| IEC 62288:2021 [R35-§TBD] |
| SAT-2 决策推理 | `/l3/sat/data.colregs_chain` @1Hz | 1 句话摘要（如"右转 15° 避让 VESSEL-A"）| 完整规则链（Rule 14 → stand-on → 建议航向）| IEC 62288:2021 [R35-§TBD] |
| ToR 接管界面 | `/l3/tor/request` event | 一键接管按钮（绿色，≥44px）+ 情境总结 | 分步确认（Step 1 已知悉 SAT-1 → Step 2 确认接管）| IMO MSC.1/Circ.1609 [R-IMO-SMODE] |

## S-Mode 合规项（IMO MSC.1/Circ.1609）

| S-Mode 要求 | Figma 实现 | 状态 |
|---|---|---|
| 所有安全相关信息一键可及 | ToR 接管按钮在首屏 | ✅ |
| 告警层级视觉区分 | 红/黄/绿三色编码 | ✅ |
| 导航相关字体 ≥14pt | 主信息 ≥16pt | ✅ |

## 差异记录（Figma 设计意图 vs Web HMI 实装）

| 差异点 | Figma 设计意图 | Web HMI 现状 | 对齐行动 |
|---|---|---|---|
| [待访谈洞察注入后填写] | | | |

## 参考文献

- [R35] IEC 62288:2021 AMD1:2024（子条款号待核实 [R35-§TBD]）
- [R-IMO-SMODE] IMO MSC.1/Circ.1609 (2019) S-Mode
