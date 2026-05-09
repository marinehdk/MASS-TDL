# rl_workbench/ — Phase 4 RL 训练工作空间（占位）

> **当前状态（2026-05-09）**：占位目录。**禁止任何 Python/C++ 代码进入**。
> Phase 4（10–12 月，B2 触发）启动后才允许填充内容。

## 目的

本目录是 MASS L3 TDL 三层 RL 隔离架构 L1 Repo 边界的第三象限（per 架构
v1.1.3-pre-stub 附录 F.4 + 计划 v3.1 §0.4 决策 3）：

| 层 | 边界 | 强制实现 |
|---|---|---|
| **L1 Repo** | `/src/l3_tdl_kernel/**` (C++/MISRA, frozen) vs `/src/sim_workbench/**` (Python sim) vs `/src/rl_workbench/**` (Phase 4) | 三 colcon 包独立；CI lint 检测 cross-import 报错 |
| **L2 Process** | RL 训练独立 Docker container；通过 OSP libcosim FMI socket 调相同 MMG FMU + scenario YAML | docker-compose 隔离 namespace |
| **L3 Artefact** | 训练完毕 ONNX → mlfmu → target_policy.fmu → libcosim 加载到 certified SIL；Python/PyTorch 永不入 certified loop | mlfmu build 是边界 |

## Phase 4 启动条件（before any code lands here）

1. B2（RL 对抗启动条件）触发——Phase 3 SIL 1000 场景 baseline 完成
2. git history audit 通过——D1.3a/b/c 已遵守 L1/L2 边界（架构 F.4 行 2166）
3. DNV-RP-0671 (2024) AI-enabled systems assurance 计划就位
4. mlfmu 工具链鉴定通过

## 当前 CI 行为

`tools/ci/check-rl-isolation.sh` 双向扫描：
- 任何文件在本目录引用 `l3_tdl_kernel/*` 或 `sim_workbench/*` → exit 1
- `l3_tdl_kernel/*` / `sim_workbench/*` 引用本目录 → exit 1

## 引用

- 架构 v1.1.3-pre-stub 附录 F.4（行 2156-2167）
- SIL 决策记录 §4 RL 隔离三层强制边界
- 计划 v3.1 §0.4 决策 3
- spec D1.3a §v3.1.1 + §v3.1.3
