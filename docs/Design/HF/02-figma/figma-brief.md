# Figma 原型对齐简报 · D2.6

| 版本 | 2026-06-16 v1.0 |
| 用途 | M8 Owner + HF 咨询 onboard 后第一个工作日阅读；约束 Figma 设计范围 |

## 背景参照（先读再做 Figma）

1. D1.3.2.3 Web HMI 实装：React + MapLibre GL JS (WebGL) + foxglove_bridge
   - 已有 `/l3/sat/data @1Hz` SAT-1/2/3 stub topic
   - 视觉语言：深色背景 (#1A1A2E) / 强调色 (#4FC3F7) / 等宽字体
2. 架构 §12.3 当前设计（待校准）：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §12.3
3. IEC 62288:2021 AMD1:2024 SA subset（具体子条款须 HF 咨询 onboard 后核实 [R35-§TBD]）
4. IMO MSC.1/Circ.1609 (2019) S-Mode 设计原则

## Figma 必须包含的内容

| Frame | 色系 | 主要元素 |
| --- | --- | --- |
| Captain-View | 暖色系：#E8F5E9 背景 / #FF8F00 强调 / 字≥16px | 大图标自主等级 / 罗经图 / Top-3 威胁卡 / 一键接管 |
| ROC-View | 冷色系：#E3F2FD 背景 / #1565C0 强调 / 密集数值 | 完整规则链 / CPA/TCPA 完整列表 / 分步 ToR 确认 |
| Switching Layer | — | 角色切换按钮（同一 Figma frame，层可见性切换） |

## 5 个必含显示要素（spec §4.2）

1. 当前自主等级（D2/D3）
2. ODD 子域 + Conformance 状态
3. 目标船威胁列表
4. SAT-2 决策推理（船长：1 句话摘要；ROC：完整规则链）
5. ToR 接管界面（船长：一键；ROC：分步确认）

## IEC 62288 对照要求

- 船长层 + ROC 层各要素须标注对应 IEC 62288 子条款号
- **[R35-§TBD]**：具体子条款在 HF 咨询 onboard 后核实，暂留占位
- 产出物：`iec62288-alignment.md`（Task B2 产出）

## 推迟项（Phase 3+ spawn_task 已创建）

- 完整差异化双 frame（纯航海语言 captain 专用 Figma）→ D3.4 或 D3.5'
- 完整 ECDIS S-100 图幅渲染 → D3.4

## 截图导出规范

存 `docs/Design/HF/02-figma/screenshots/` 目录：

- 命名：`figma-{captain|roc}-{feature}.png`，如 `figma-captain-tor-button.png`
- 尺寸：1920×1080
- 格式：PNG（无损）
- 数量：≥4 张（captain 视图 ≥2 + ROC 视图 ≥2）

## Verification

Confirm file exists and contains the 5 display elements, screenshot export specs, and `[R35-§TBD]`.
