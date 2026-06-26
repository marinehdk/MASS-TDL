# Phase 3 自检 HMI 与实时日志流标准化设计规范

*   **创建日期**：2026-05-27
*   **状态**：已批准 (Approved)
*   **关联任务**：D1.3.2.3 Web HMI / Phase 3 Integration
*   **涉及模块**：M8 (HMI/Transparency Bridge), self-check backend (GateRunner)

---

## 1. 业务目标与需求背景

系统已成功演进至 **Phase 3**，所有 8 个算法内核模块已完成核心研发并具备基本集成条件。为了对齐最新的 Phase 3 安全校验标准与认证规范，并提供精细化、高水平的 Web 排障体验，本规范定义以下重构动作：

1.  **DDS 心跳与警告文案对齐**：修改自检引擎后端，剔除遗留的 "Phase 1" 及 "not deployed" 字样，替换为匹配 Phase 3 现状的专业警告语。
2.  **动态容器健康监视器 (OrbStack-Style HMI)**：将原有的静态“容器详情”重构为 100% 真实的动态监测面板，以 Gate 1 (基础架构就绪度) 和 Gate 2 (模块健康度) 的实时探针结果为驱动源，提供仿 OrbStack 的外发光状态灯与交互微动效。
3.  **精炼工业日志流 (Industrial Console Format - Option A)**：重写自检日志流生成与收集逻辑，实现高度精简、严整对齐、信息无冗余的控制台日志流，重点对 M1-M8 重复的 UNSPECIFIED 警告进行总结压缩。

---

## 2. 详细技术方案

### 2.1 后端自检引擎警告文案重构

**目标文件**：[src/sil_orchestrator/gate_runner.py](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/src/sil_orchestrator/gate_runner.py)

1.  **心跳警告文案修订**：
    将 `UNSPECIFIED` (状态 `0`) 的回退信息从 `(Phase 1: L3 kernel nodes not deployed)` 修订为 `(Phase 3: L3 kernel nodes undetected)`。
2.  **Gate Rationale 修订**：
    将 Gate 2 的总体说明由 `Phase 1: X/8 modules UNSPECIFIED...` 修改为 `Phase 3: X/8 modules UNSPECIFIED (L3 kernel nodes undetected)...`。

### 2.2 前端容器健康监视器重构 (ContainerSpecPanel)

**目标文件**：
1.  [web/src/screens/shared/ContainerSpecPanel.tsx](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/screens/shared/ContainerSpecPanel.tsx)
2.  [web/src/screens/shared/ActionLogs.tsx](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/screens/shared/ActionLogs.tsx)

**推导规则 (Status Derivation)**：
前端解析自检 `gates` 数组的检查列表：
*   `sil-orchestrator-1`：若自检 SSE 通信活跃，直接标记 `RUNNING`。
*   `foxglove-bridge-1`：若 `gate_id === 1` 中含有 `foxglove_bridge` / `8765` 的检查项状态为 `"ok"`，标记为 `RUNNING`，否则若自检已运行且失败，标记为 `OFFLINE`，未运行标记为 `PENDING`。
*   `martin-tile-server-1`：同上，解析 `martin` / `3000` 的子检查状态。
*   `sil-nodes-1`：解析 `gate_id === 1` 中的 `ROS2 DDS` 或 `Gate 2` 中任意 M1-M8 心跳状态。若发现心跳全为 `UNSPECIFIED` 且 DDS 话题未注册，展示 `ACTIVE (no DDS)` 蓝色警告指示灯（对齐 OrbStack 的真实运行状态）；若发现任意 `GREEN`/`AMBER`，展示 `RUNNING` 绿色指示灯。
*   `web-1`：始终展示 `RUNNING`。

**视觉元素**：
*   **OrbStack 微光呼吸灯**：
    ```css
    @keyframes pulse-green {
      0% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(52, 199, 89, 0.7); }
      70% { transform: scale(1); box-shadow: 0 0 0 5px rgba(52, 199, 89, 0); }
      100% { transform: scale(0.95); box-shadow: 0 0 0 0 rgba(52, 199, 89, 0); }
    }
    ```
    对应绿色指示灯（`#34c759`）、蓝色指示灯（`#0a84ff`）、红色指示灯（`#ff3b30`）及灰色指示灯。

### 2.3 前端自检日志流标准化 (Industrial Console Format)

**目标文件**：[web/src/hooks/useGateStream.ts](file:///Users/marine/Code/MASS-L3-Tactical%20Layer/web/src/hooks/useGateStream.ts)

重新编写日志收集拼接逻辑：
1.  **精炼输出与前缀统一**：
    *   统一使用 `[13:12:29]` 精准格式（如果时间戳丢失使用系统当前时间）。
    *   统一使用 `[OK]`、`[WARN]`、`[SYS]` 作为统一长度前缀。
2.  **心跳警告去冗余压缩 (Consolidation)**：
    *   在接收到 **Gate 2** 的心跳事件时，如果 8 个模块中存在多个 `UNSPECIFIED` (且无 RED 故障)，**不在日志流中输出 8 行重复的警告**，而是将其**合并压缩为单行高度概括的警告信息**：
        `[WARN] M1-M8 pulses: UNSPECIFIED (Phase 3: L3 kernel nodes undetected on DDS)`
    *   只有当某个模块出现真正的红色 `RED` 故障或 `M7` 隔离失败时，才输出独立的致命报错行以供排障。

---

## 3. 验证方案

1.  **UI 动效测试**：启动系统自检，确认右侧“容器详情”面板中的指示灯根据门控 1/2 的结果，从灰色 PENDING 渐变到绿色 RUNNING 或蓝色 ACTIVE，呼吸动效无撕裂。
2.  **日志紧凑度测试**：确认点击自检后，右侧控制台展示的日志格式整齐划一，无冗余多余 of 的折行，Gate 2 不再疯狂刷屏 8 行重复警告。
