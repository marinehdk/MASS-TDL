# MASS L3 TDL · SIL 框架详细设计文档

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003 |
| 版本 | **v2.0**（详细设计基线）|
| 日期 | 2026-05-13 |
| 状态 | 设计锁定 |
| 适用 | Phase 1–3（DEMO-1 6/15 → DEMO-3 8/31）|
| 上游依据 | (a) MASS-TDL 架构设计报告 v1.1.3-pre-stub；(b) 8个月开发计划 v3.1；(c) colav-simulator capability inventory；(d) NLM Deep Research 调研结果 |
| 认证目标 | CCS i-Ship (Nx, Ri/Ai) + DNV-CG-0264 |

---

## 文档结构

本文档为 SIL 框架详细设计，包含以下子文档：

| 文档 | 编号 | 描述 |
|------|------|------|
| [01-总览与架构](./01-overview.md) | SANGO-ADAS-L3-SIL-DD-003-01 | 系统拓扑、模块责任矩阵、技术栈选型 |
| [02-场景构建器页面](./02-screen-builder.md) | SANGO-ADAS-L3-SIL-DD-003-02 | 屏幕① 场景构建器详细设计（含三大运行域管理架构） |
| [03-预检页面](./03-screen-preflight.md) | SANGO-ADAS-L3-SIL-DD-003-03 | 屏幕② 预检详细设计 |
| [04-Captain-HMI页面](./04-screen-captain-hmi.md) | SANGO-ADAS-L3-SIL-DD-003-04 | 屏幕③ Captain HMI 详细设计 |
| [05-运行报告页面](./05-screen-report.md) | SANGO-ADAS-L3-SIL-DD-003-05 | 屏幕④ 运行报告详细设计 |
| [06-仿真控制工具条](./06-simulation-control-toolbar.md) | SANGO-ADAS-L3-SIL-DD-003-06 | 统一工具条详细设计 |
| [07-后端API与数据流](./07-backend-api.md) | SANGO-ADAS-L3-SIL-DD-003-07 | REST API、数据流、状态机 |
| [08-认证证据链](./08-evidence-chain.md) | SANGO-ADAS-L3-SIL-DD-003-08 | CCS/DNV 证据链设计 |

---

## 修订记录

- 2026-05-13 v2.0：基线建立。基于 MASS-TDL v1.1.3-pre-stub + 8个月开发计划 v3.1 重新设计。
- 2026-05-13 v2.1 (Draft)：更新 Scenario Builder 顶层架构，引入“单一场景、蒙特卡洛、深度学习环境”三大运行域 (Operation Domains) 管理模型与参数化动态表单设计。