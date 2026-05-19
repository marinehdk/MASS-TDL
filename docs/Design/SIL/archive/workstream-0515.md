

# 共用前缀

项目根：/Users/marine/Code/MASS-L3-Tactical Layer
权威设计基线：docs/Design/SIL/v1.0-unified/  (commit d6f7fd7, 5 docs ~209KB)
  - 00-README.md           索引 + 共用约定 + 4 屏命名（Simulation-Scenario/Check/Monitor/Evaluator）
  - 01-sil-architecture.md 顶层架构 + GAP-001~014
  - 02-sil-backend-design.md 后端 + GAP-015~021
  - 03-sil-frontend-design.md 前端 + GAP-022~029
  - 04-sil-scenario-integration-test.md 场景联调 + GAP-030~032

架构主文件：docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md (v1.1.3-pre-stub)
V&V Plan：docs/Design/V&V_Plan/00-vv-strategy-v0.1.md (commit e1a13e5)
8 月计划：docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md (v3.0)

ROS2 版本：Humble Hawksbill + Ubuntu 22.04 + PREEMPT_RT（决策记录 §3 锁定 🟢）
约束：Karpathy 四则（Think First / Simplicity / Surgical / Goal-Driven），TDD，superpowers:verification-before-completion 强制
DEMO-1 deadline：2026-05-25  /  DEMO-2 deadline：2026-05-31
当前日期：2026-05-15

## Workstream A — 9 LifecycleNode + ship_dynamics 4-DOF MMG

[共用前缀]

任务：将 src/sim_workbench/ 下 9 个业务节点从 pure-Python 数据类 stub 升级为完整
rclpy LifecycleNode，并实现 ship_dynamics_node 的 Yasukawa 2015 4-DOF MMG 物理模型。

现状：
- src/sim_workbench/sil_nodes/{ship_dynamics,env_disturbance,target_vessel,
  sensor_mock,tracker_mock,fault_injection,scoring}/  各 node.py 仅为 23 行
  Python class，main() 仅打印"ready"
- src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py 是 pure-Python
  FSM stub，无 rclpy.Node 继承
- ship_dynamics/node.py 是 kinematic-only stub，无水动力

目标（per Doc 2 §3.3 完整责任表）：
- 10 节点全部继承 rclpy.lifecycle.LifecycleNode
- 实现 on_configure/on_activate/on_deactivate/on_cleanup 4 callback
- 注册 publisher 到 sil_msgs/* topic + timer @ 指定 Hz（ship_dynamics 50Hz,
  target_vessel 10Hz, sensor_mock radar 5Hz + AIS 0.1Hz, ...）
- ship_dynamics 实施 Yasukawa & Yoshimura 2015 MMG 4-DOF
  (DOI: 10.1007/s00773-015-0299-0)：M_RB + M_A + C(ν) + D(ν) + g(η) =
  τ_hull + τ_prop + τ_rud + τ_env；状态 [x,y,ψ,φ,u,v,r,p]；RK4 dt=0.02s
- FCB 45m 系数初值取自 NTNU colav-simulator models.py:ViknesParams
  (github.com/NTNU-TTO/colav-simulator)，按 FCB 45m 比例近似缩放
- scenario_lifecycle_mgr 作为 LifecycleNode + 业务 mgr，编排 9 业务节点的
  change_state 顺序调用（参 Nav2 lifecycle_manager 模式）；publish
  /sim_clock @ 1kHz + /sil/lifecycle_status @ 1Hz
- DDS QoS 按 Doc 2 §7.3 qos_overrides.yaml 配置

V&V 验收：
- pytest 单元测试：每节点 LifecycleNode state transition 4 个 callback PASS
- pytest 集成测试：lifecycle_mgr.activate() 后所有节点进入 ACTIVE 内 <5s（V&V Plan E1.6）
- ship_dynamics 集成测试：head_on.yaml 场景 + 35° 右转指令 → 轨迹误差 vs
  解析解 < 2% RMS（DNV-RP-0513 §模型保证基线）
- MultiThreadedExecutor(num_threads=4) + ReentrantCallbackGroup 50Hz p99 < 22ms
  实测 profile 通过（Doc 2 §2.2 GAP-016 联动）

输出 plan 含：
1. 升级顺序（建议：sil_lifecycle_mgr → ship_dynamics → env_disturbance →
   target_vessel → sensor_mock → tracker_mock → scenario_authoring →
   self_check → fault_injection → scoring）
2. 每节点 callback 内 publisher/subscription/timer 注册清单
3. ship_dynamics MMG 4-DOF 实施 sub-task（状态向量 + RK4 + 系数表 + 测试）
4. CI 单元测试 + 集成测试 fixture
5. 与 D-task D1.3b.3 (workstream D/E) 协调点

## Workstream B — 容器构建链 humble 统一 + FMI 桥 stub

[共用前缀]

任务：修复 docker-compose.yml 与 Dockerfile 中残留的 jazzy 引用，统一到
ros:humble；为 D1.3c FMI 桥建立可工作 stub。

现状：
- docker/sil_orchestrator.Dockerfile FROM mass-l3/ci:jazzy-ubuntu22.04 ❌
- docker-compose.yml foxglove-bridge image: mass-l3/ci:jazzy-ubuntu22.04 ❌
- docker/sil_nodes.Dockerfile FROM ros:humble-ros-base ✅
- src/sim_workbench/fmi_bridge/  仅 colcon scaffold，无 dds-fmu/libcosim 集成

目标（per Doc 2 §9.2 修复路径）：
- 自建本地 CI 镜像 mass-l3/ci:humble-ubuntu22.04（基于 ros:humble-ros-base +
  colcon + ros-humble-{rclcpp-components,foxglove-bridge,rosbag2-storage-mcap}
  + python3-pip）；提供 docker/ci.Dockerfile
- 改 sil_orchestrator.Dockerfile 与 foxglove-bridge service 镜像引用为
  humble 版本
- FMI 桥 stub：fmi_bridge/ 内集成 libcosim (MPL-2.0) + dds-fmu mediator +
  PyFMI 适配器；scenario YAML 字段 simulation_settings.dynamics_mode:
  internal|fmi 切换支持；先跑通 own_ship.fmu loading + doStep 单元测试
- network_mode: host 强制（DDS multicast 必需 [W44]）；ROS_DOMAIN_ID=0 全栈共享

V&V 验收：
- docker compose up -d 5 service 全部 healthy；docker compose ps 输出无 jazzy
- ros2 multicast {send,receive} 跨 service 互通（不同 container 同 ROS_DOMAIN_ID）
- fmi_bridge pytest：load own_ship.fmu (OSP 参考 FMU 或 wrap nikpau/mmgdynamics)
  + 1000 doStep 决定性 replay 输出 ±0.1s 时间偏移内（V&V Plan X2.1）
- FMI co-sim 单次 exchange latency p95 < 10ms（决策记录 §2.4 KPI）

输出 plan 含：
1. ci.Dockerfile 多阶段构建（base layer + ROS2 + Python + colcon prebuild）
2. sil_orchestrator.Dockerfile humble 切换 diff
3. docker-compose.yml foxglove-bridge service 镜像 + command 修订
4. FMI 桥 sub-task：libcosim 集成 → dds-fmu wrapper → PyFMI adapter →
   own_ship.fmu loader → doStep timer 集成 → latency profile
5. OrbStack 启动验证脚本
6. 与 workstream A (lifecycle_mgr) 协调点（dynamics_mode 切换）

## Workstream C — Scenario Schema 迁移到 maritime-schema 统一

[共用前缀]

任务：将 35 个 scenario YAML（当前 2 套 schema 并存：COLREGs v1.0 ENU +
IMAZU v2.0 lat/lon）统一迁移到 DNV maritime-schema TrafficSituation v0.2.x
+ metadata 扩展节，并接入三语言 JSON Schema 校验。

现状（per Doc 4 §2）：
- scenarios/COLREGs测试/colreg-rule{13,14,15}-*.yaml (11 个) 用 v1.0 ENU x_m/y_m
- scenarios/IMAZU标准测试/imazu-{01..22}-*.yaml (22 个) 用 v2.0 lat/lon + metadata
- scenarios/head_on.yaml NTNU 风格残留
- scenarios/5c93bf30f54c.yaml UUID 用户场景
- 服务端校验 scenario_store.validate() 仅查空字符串（GAP-017）
- 前端 useValidateScenarioMutation 无 schema 提示（GAP-022）

目标（per Doc 4 §2.2 + 决策记录 §10 完整模板）：
- 35 个 YAML 全部迁移到 maritime-schema TrafficSituation root 结构（title,
  description, startTime, ownShip, targetShips, environment）+ metadata
  扩展节（scenario_id, hazid_refs, colregs_rules, odd_cell, disturbance,
  seed, vessel_class, expected_outcome, simulation_settings）
- 安装 dnv-opensource/maritime-schema v0.2.x（PyPI）+ Python 校验
  (cerberus + pydantic)
- 安装 cerberus-cpp (github.com/dokempf/cerberus-cpp) C++ 校验
- 前端 ajv + monaco-editor 集成 JSON Schema 实时校验提示
- 字段映射器 tools/migrate_scenario_yaml.py 一次性迁移现有 35 个 YAML
- CI gate tools/validate_scenarios.py 每 PR 跑 35 场景 schema 校验
- 现有 IMAZU metadata.encounter / pass_criteria / disturbance_model 保留作
  扩展节，无信息损失
- scenarios/schema/fcb_traffic_situation.schema.json 作 canonical schema 提供

V&V 验收：
- 35/35 YAML 通过 maritime-schema + FCB 扩展 schema 校验
- Imazu-22 SHA256 hash 化 manifest（freeze 不可改）
- 前端 ScenarioBuilder Save 错误 YAML 时立即报错（非提交后 422）
- CI tools/validate_scenarios.py 退出码 0
- 端到端：场景 YAML → ScenarioStore.get → 后端 cerberus → 后端 cerberus-cpp →
  前端 ajv 四处校验一致

输出 plan 含：
1. fcb_traffic_situation.schema.json 完整字段定义（参 Doc 4 §2.2 / §2.3）
2. 字段映射 v1.0 ENU + v2.0 lat/lon → maritime-schema（35 yaml × 字段表）
3. tools/migrate_scenario_yaml.py 迁移脚本 + 单元测试
4. 服务端 scenario_store.validate() 升级到 cerberus + maritime-schema
5. 前端 ScenarioBuilder + monaco-editor 集成
6. CI gate 集成
7. Imazu-22 hash manifest freeze 步骤

## Workstream D — Simulation-Check 6-Gate Sequencer 重设计

[共用前缀]

任务：重写 Simulation-Check 屏（当前 Preflight.tsx 7.9KB + selfcheck_routes.py
44 行 stub），从 5 项硬编码 PASS 升级为架构对齐的 6-Gate Sequencer，含
Doer-Checker 隔离验证 + ODD 对齐 + 真实 GO/NO-GO 判定。

现状（per Doc 3 §7.1）：
- web/src/screens/Preflight.tsx 5 项硬编码 check：ENC / ASDR / UTC / M1-M8 /
  Hash；600ms 假延迟 sequential；失败 detail 仅字符串；SKIP PREFLIGHT 按钮
  一键绕过（认证不可接受）
- src/sil_orchestrator/selfcheck_routes.py 返回 hardcoded all-PASS

目标（per Doc 3 §7.2 6-Gate Sequencer 完整设计）：
- 6 个 Gate 完整实施：
  GATE 1 系统物理就绪（docker + ROS2 DDS + foxglove + martin + WS）
  GATE 2 模块健康（M1-M8 全 GREEN + M7 进程独立验证）
  GATE 3 场景完整性（hash 一致 + maritime-schema 通过 + expected_outcome 完整）
  GATE 4 ODD-场景对齐（scenario.odd_cell ⊆ M1 ODD state）
  GATE 5 时基与证据链（UTC PTP <10ms + sim_clock 通 + rosbag2 ready + ASDR ready）
  GATE 6 Doer-Checker 隔离合规（M7 PID 独立 + 不引用 OR-Tools + 测试 VETO 50ms 回灌）
- 后端 selfcheck_routes.py 真实查询（subprocess 验 PID + docker inspect +
  ROS2 service 探 + ros2 topic echo 1s 内收帧）
- 前端 6 折叠 Gate 卡片 + 详细 detail rows + 失败时 DEACTIVATE+RECONFIGURE
  按钮 + GO/NO-GO 倒数
- SKIP PREFLIGHT 在 production build 完全移除（process.env.NODE_ENV）；
  dev mode (?dev=1) 才显示，且 SKIP 必写 ASDR + verdict warning_unverified_run
- LiveLogStream 实时显示 preflight_log 订阅
- 6-Gate 任一 FAIL → 红框 + 展开 + 自动 cleanup → 返 #/scenario

V&V 验收：
- 单元测试：每 Gate 真实 PASS + FAIL 各 ≥ 1 case
- 集成测试：Imazu-01 场景全 6 Gate PASS 内 < 5s
- 集成测试：故意 stop M7 容器 → GATE 6 FAIL + ASDR 记 verdict
- 集成测试：scenario.odd_cell 改为不匹配 → GATE 4 FAIL
- pytest M7 watchdog Python stub ≥1 PASS（V&V Plan E1.8）

输出 plan 含：
1. selfcheck_routes.py 后端 6-gate sub-task（每 gate 实施 + 测试）
2. 前端 Preflight.tsx 完全重写（保持已有 LiveLogStream 等组件）
3. GATE 6 Doer-Checker 隔离验证脚本（PID + container ID + import lint）
4. SKIP 按钮 conditional render + ASDR 记录
5. e2e Playwright 测试覆盖 6 Gate
6. 与 workstream A 协调（M1 ODD state endpoint 真实接通）

## Workstream E — Orchestrator improvements: telemetry 退役 + MultiThread Executor + Marzip 完整化

[共用前缀]

任务：完成 GAP-015 端口冲突决断（退役 telemetry_bridge.py，统一 foxglove_bridge
标准协议）+ orchestrator rclpy threading 重构（双 SingleThread → 单
MultiThread + ReentrantCallbackGroup）+ Marzip 后处理完整化。

现状（per Doc 2 §2.2 + Doc 3 §4.2）：
- src/sil_orchestrator/telemetry_bridge.py:112 自启 websockets.serve :8765，
  与 docker-compose foxglove-bridge :8765 端口冲突
- main.py 启 2 个 SingleThreadedExecutor（LifecycleBridge + TelemetryBridge），
  50 Hz p99 实测可能 > 50ms
- web/src/hooks/useFoxgloveLive.ts 消费 telemetry_bridge 自制 JSON 帧
  {topic, payload}，非 foxglove 标准协议
- export_routes.py Marzip 仅 manifest + scenario.yaml + sha256 + scoring.json

目标（per Doc 2 §2.4 + §11 + Doc 3 §4.2）：
- 删除 src/sil_orchestrator/telemetry_bridge.py + main.py:26,45 import
- orchestrator 仅留 REST 控制面
- main.py 改 MultiThreadedExecutor(num_threads=4) + LifecycleBridge 用
  ReentrantCallbackGroup
- foxglove_bridge :8765 唯一占有；docker-compose foxglove-bridge service
  command 配置 advertise 11 个 /sil/* + /l3/* topic
- 前端 useFoxgloveLive.ts 改用 @tier4/roslibjs-foxglove (已在 package.json
  依赖) 标准 ROS2 client，subscribe 11 topics
- export_routes._build_marzip 后台 task 扩展：读 runs/{id}/bag.mcap →
  polars DataFrame → arrow.ipc；计算 derived KPIs；写 asdr_events.jsonl；
  写 verdict.json
- Marzip 7 件容器完整：scenario.yaml + sha256 + manifest.yaml +
  scoring.arrow + results.bag.mcap + results.bag.mcap.sha256 +
  asdr_events.jsonl + verdict.json

V&V 验收：
- 单元测试：MultiThreadedExecutor 50Hz p99 < 22ms profiling 通过
- 单元测试：useFoxgloveLive 接 foxglove_bridge advertise/subscribe 协商 PASS
- 集成测试：Imazu-01 run 完毕，POST /api/v1/export/marzip → 7 件齐全
- 集成测试：MCAP → Arrow 后处理 1000s 录像 < sim 时长（决策记录 否决信号）
- e2e：前端 useFoxgloveLive 连 ws://127.0.0.1:8765 → 11 topic 持续 1min 0 drop

输出 plan 含：
1. telemetry_bridge.py 删除 + main.py 重构（MultiThreadedExecutor）
2. foxglove-bridge service compose 配置（topic advertise list + QoS overrides）
3. useFoxgloveLive.ts 重写（roslibjs-foxglove subscribe API）
4. export_routes._build_marzip 7 件完整化（含 MCAP→Arrow 后处理）
5. polars DataFrame schema for scoring + KPI derived
6. asdr_events.jsonl human-readable 格式 + verdict.json schema

## Workstream F — Mock 退役 + scoring 真实化 + DEMO-1→DEMO-2 cutover

[共用前缀]

任务：DEMO-1 通过后，cutover 全栈到 ROS2 真链路；退役 4 个 Mock；实施
scoring_node 真实 6 维 Hagen 2022 评分；8 KPI cards 真实数据。

现状（per Doc 4 §10）：
- tools/demo/demo_server.py + demo_ws_server.py + trajectory.py
  DEMO-1 standalone（非 ROS2）
- src/l3_tdl_kernel/l3_external_mock_publisher/ Mock Fusion 输出
- src/sim_workbench/sil_mock_publisher/ Mock ship_dynamics 输出
- src/sil_orchestrator/main.py:67-83 _seed_run_dir 写硬编码 scoring stub
  {min_cpa_nm: 0.42, avg_rot_dpm: 2.1, ...}
- web/src/screens/RunReport.tsx 8 KPI cards 仅 4 字段实数据

目标（per Doc 4 §5.2 + Doc 2 §3.1）：
- scenario YAML 字段 simulation_settings.backend: demo|ros2 feature flag
  实现；ros2 模式全栈跑通后强制 ros2，删除 demo backend 路径
- 退役 4 Mock：
  * tools/demo/demo_server.py 删除
  * tools/demo/demo_ws_server.py 删除
  * src/l3_tdl_kernel/l3_external_mock_publisher/ 删除 → 真链路 sensor_mock +
    tracker_mock 接 M2
  * src/sim_workbench/sil_mock_publisher/ 删除 → 真链路 ship_dynamics
- scoring_node 实施 6 维 Hagen 2022 + Woerner 2019 评分：
  safety + rule_compliance + delay_penalty + action_magnitude_penalty +
  phase_score + trajectory_implausibility；total_score 加权计算
- scoring_node 50Hz 采样输入 + 1Hz publish ScoringRow + 旁路写 Arrow IPC
  到 runs/{run_id}/scoring.arrow
- main.py _seed_run_dir scoring stub 移除；GET /api/v1/scoring/last_run
  读 scoring.arrow → 计算 derived 8 KPI
- 8 KPI 完整化：min_cpa_nm, tcpa_min_s, avg_rot_dpm, max_rudder_deg,
  grounding_risk_score, route_deviation_nm, time_to_mrm_s, decision_count
- RunReport.tsx 8 KPI cards 全部填实数据；6 维 ScoringRadarChart 实时

V&V 验收：
- Imazu-22 全 22 场景 ros2 backend PASS（V&V Plan X1.5）
- 50 baseline 场景 ≥ 95% PASS（X1.1）
- KPI 矩阵 30 runs 全 KPI 内（X1.2）
- Coverage cube ≥ 10/1100 cells lit（X1.4）
- M7 watchdog 关键路径 7 modules timeout/recovery 覆盖（X1.6）
- 6 维评分总分 vs ground truth 校准 < 5% diff

输出 plan 含：
1. simulation_settings.backend feature flag 后端 + 前端 + scenario schema
2. Mock 退役顺序 + parallel run 验证（demo + ros2 对比 < 2% trajectory diff）
3. scoring_node 6 维实施 sub-task（含 Hagen 2022 / Woerner 2019 公式）
4. scoring.arrow 列存 schema + polars adapter
5. RunReport.tsx 8 KPI cards 数据接入
6. CI Imazu-22 + 50 baseline 自动跑（V&V Plan E1.4-E1.6）
7. cutover commit 序列（每 Mock 独立 commit + 引用 V&V hash）

## Workstream G — D1.3.1 仿真器鉴定报告

[共用前缀]

任务：产出 D1.3.1 Simulator Qualification Report（DNV-RP-0513 +
DNV-CG-0264 §3 映射），作为 CCS AIP 提交（D4.4 11月）头号必需件。

现状：
- 套件 v1.0 Doc 4 §11 已锁定 D1.3.1 范围 + 4 项核心证明 + 交付物清单
- DNV-RP-0513 + DNV-CG-0264 仅引摘要，完整付费 PDF 未访问（GAP-032）
- self_check 当前 stub（GAP-005 由 workstream D 修复）

目标（per Doc 4 §11）：
- 创建 docs/Design/SIL/D1.3.1-simulator-qualification/ 完整目录
- 7 份交付物：
  01-overview.md 范围 + 验证策略
  02-model-fidelity-report.md MMG vs FCB 池实验 / CFD diff（RMS ≤ 5%）
  03-determinism-replay.md 1000 次 replay 重复性（±0.1s 时间偏移, ±0.5° 航向）
  04-sensor-confidence.md per-sensor 退化 vs CG-0264 §6 限值
  05-orchestration-trace.md libcosim API trace + 通信步长审计
  06-evidence-matrix.md 4 项证明 → DNV-RP-0513 条款映射
  07-ccs-mapping.md CCS《智能船舶规范 2024》§9.1 性能验证条款映射
- annex/test-results/ CI artifact dump
- annex/csv/ 数据原件
- DNV-RP-0513 + DNV-CG-0264 完整 PDF 从 DNV 渠道或 CCS 渠道购入

V&V 验收：
- 7 份交付物 + annex 全部 commit
- MMG 4-DOF 在 head_on.yaml + crossing + overtaking 三场景 vs ground truth
  RMS error ≤ 5%（与 workstream A ship_dynamics 联动）
- 1000-run determinism replay 完整脚本 + 重复性数据 < 阈值
- CCS surveyor 预读 sign-off（D1.8 早发函）
- V&V Plan v0.1 §8 DNV Toolchain Entry Conditions 全部勾选

输出 plan 含：
1. 7 份文档结构 + 章节清单
2. MMG fidelity benchmark 测试套件（CFD 替代池实验路径，[W33] nikpau MMG）
3. Determinism replay 自动化脚本（rosbag2 + diff + report 生成）
4. Sensor confidence 校准实验设计
5. libcosim trace + 步长审计工具
6. DNV-RP-0513 条款映射表完整
7. CCS surveyor 沟通时间表

## Workstream H — DEMO-3 Phase 3 完整化（ToR + Doer-Checker verdict + 1000 场景立方体）

[共用前缀]

任务：DEMO-2 通过后实施 DEMO-3 Full-Stack with Safety + ToR：
1000 场景立方体覆盖 + ToR 倒计时 panel + Doer-Checker verdict 显示 +
S-Mode 完整对齐 + HAZID 132 [TBD] 回填 + Cybersec RFC-007 TLS/WSS。

现状（per Doc 4 §6.3 + 架构 v1.1.3-pre-stub）：
- web/src/screens/shared/TorModal.tsx 8 KB（commit a40d950 完成基础 SAT-1 +
  TMR + auto-MRC，Phase 3 完整化范围）
- 架构 v1.1.3 stub 132 [TBD-HAZID] 标注待 HAZID RUN-001 (8/19) 校准
- Doer-Checker M7 verdict 当前不在屏 ③ 显式显示
- 场景库仅 35，1100 cells coverage cube 待 D3.6 farn n-dim generator

目标（per Doc 4 §6.3 + 决策记录 §9.4）：
- D3.4 HMI 完整化：
  * Trajectory ghosting overlay（M5 BC-MPC 候选轨迹）
  * TorModal 完整 3-tier 升级（silent 0-20s → audio 20-45s → red+haptic 45-60s）
  * Doer-Checker verdict badge 在 ConningBar 与 ThreatRibbon 间
  * S-Mode IMO MSC.1/Circ.1609 完整对齐（IEC 62288 inspector 审）
  * 4 操作员状态联动（D2.1 后）
- D3.6 1100 cells coverage cube：
  * 11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds
  * dnv-opensource/farn v0.4.2 + ospx n-dim case folder
  * Monte Carlo LHS / Sobol 10000 sample（pass rate 95% CI + CPA min 分布）
  * 1000 场景 evidence pack 一键产出（Puppeteer batch GIF + Arrow KPI）
- D3.8 架构 v1.1.3 完整化：
  * HAZID RUN-001 (8/19 完成) 132 [TBD] 全部回填
  * 算法选型矩阵 + 仲裁优先级完整
  * ALARP demonstration 在制（推 v1.1.4）
- D3.9 Cybersec RFC-007：
  * DDS-Security x.509 cert
  * TLS/WSS 加密（前端 wss://）
  * IACS UR E26/E27 IT/OT 隔离

V&V 验收：
- 1100/1100 cells run + ≥ 95% 单 PASS（V&V Plan Phase 2 X2.4 扩展）
- Monte Carlo 95% CI 通过
- ToR auto-MRC 倒计时触发后 60s 内进入 MRC（Veitch 2024 TMR 基线）
- M7 verdict topic /l3/checker_veto 50ms 回灌 M5 实测
- IEC 62288 inspector 静态分析 0 violation
- DDS-Security profile 加载 + authentication active

输出 plan 含：
1. D3.4 HMI 5 子任务（trajectory ghost / TorModal 完整 / verdict badge /
   S-Mode / 4 operator state）
2. D3.6 1100 cells 立方体生成器 sub-task（farn + ospx 配置 + Sobol LHS）
3. D3.6 evidence pack 自动化 Puppeteer batch
4. D3.8 HAZID 回填 process（与 HAZID RUN-001 5/13 → 8/19 联动）
5. D3.9 RFC-007 Cybersec 实施
6. Phase 4 HIL 衔接预案















