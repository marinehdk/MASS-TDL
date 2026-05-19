开始全面盘点。先查 commit 历史 + workstream-by-workstream 验收检查。

46 commits since 套件提交。逐 workstream 验证。

完成审计。综合报告：

------

# 开发完成度审计（Workstream A→G）

## 总体进展

- **46 commits 自 suite v1.0 (d6f7fd7)** — 全部已推 origin/main，本地仅 HEAD 95a7392 比 origin 多 1 commit
- **426 pytest tests** 落地 · 32/32 scenario schema 校验通过 · CI gates 已加 `sil-smoke` + `sil-baseline` 两阶段

------

## ✅ 完成项（按 Workstream）

### Workstream A — 9 LifecycleNode + 4-DOF MMG · ✅ 完整

| 节点                                                   | 行数（原 23）     | 验证                                                         |
| ------------------------------------------------------ | ----------------- | ------------------------------------------------------------ |
| ship_dynamics + mmg_model + mmg_coefficients           | 319 + 7.2K + 4.5K | Yasukawa 2015 4-DOF (X/Y/N/K equations) + RK4 + 状态 [x,y,ψ,φ,u,v,r,p] + 引用 [R7] |
| tracker_mock                                           | 355               | LifecycleNode                                                |
| sensor_mock                                            | 210               | LifecycleNode                                                |
| target_vessel                                          | 194               | LifecycleNode                                                |
| scoring                                                | 181               | LifecycleNode + HagenScorer                                  |
| env_disturbance / fault_injection / scenario_authoring | 126 / 118 / 124   | LifecycleNode                                                |
| scenario_lifecycle_mgr                                 | 293               | 5-state FSM                                                  |

### Workstream B — humble + FMI · ✅ 完整

- `sil_orchestrator.Dockerfile`：humble ✅
- `sil_nodes.Dockerfile`：humble ✅
- `docker-compose.yml foxglove-bridge`：`mass-l3/ci:humble-ubuntu22.04` ✅
- `fmi_bridge/` 19 文件：libcosim_wrapper.cpp + dds_fmu_bridge.cpp + pyfmi_adapter.py + fcb_mmg_fmu.py + build_fmu.py + libcosim FetchContent + 4 tests（FMU model description / turning circle / PyFMI / dds-fmu latency）

### Workstream C — maritime-schema 迁移 · ✅ 完整

- 34 → 32 yaml（删 head_on.yaml + UUID 用户场景，归档到 scenarios/_archived/）
- `scenarios/schema/fcb_traffic_situation.schema.json` ✅
- `tools/migrate_scenario_yaml.py` (23.3K) + `tools/validate_scenarios.py` (5.6K)
- `scenarios/manifest/imazu22_sha256_manifest.yaml` Imazu-22 hash freeze ✅
- 33 个 yaml 全部 maritime-schema 3.0 PASS：`ALL 32 SCENARIOS PASSED`（去 schema.yaml 32 个 valid）
- COLREGs 11 个已迁移到 maritime-schema TrafficSituation 格式

### Workstream D — 6-Gate Sequencer · ✅ 完整

- `gate_runner.py` 19.1K + `selfcheck_routes.py` 78 行
- GATE 1-6 全部 commit：系统就绪 / 模块健康 + M7 隔离 / 场景完整性 / ODD-场景对齐 / 时基-证据链 / Doer-Checker 隔离
- `Preflight.tsx` 重写 237 行 + `GateCard.tsx` + `GoNoGoPanel.tsx`
- e2e Playwright tests
- SKIP 默认隐藏 + ASDR 记录通道

### Workstream E — telemetry 退役 + MultiThread + Marzip · ✅ 完整

- `telemetry_bridge.py` **已删除** ✅
- `main.py`：MultiThreadedExecutor(4) + ReentrantCallbackGroup ✅
- `useFoxgloveLive.ts`：`@foxglove/ws-protocol` + `@tier4/roslibjs-foxglove` 标准协议 ✅
- **Marzip 7 件容器**（`marzip_builder.py` 297 行 + `marzip_schemas.py`）：scenario.yaml + sha256 + manifest.yaml + scoring.arrow + results.bag.mcap + sha256 + asdr_events.jsonl + verdict.json 全实施 ✅
- `mcap_to_polars()` 后处理 → Arrow IPC ✅

### Workstream F — Mock 退役 + scoring 真实化 · ⚠️ 部分（5/6）

- `tools/demo/{demo_server.py, demo_ws_server.py, trajectory.py}` ✅ 删除
- `l3_external_mock_publisher` ✅ 删除
- `scoring_node` + HagenScorer (6 维) + ArrowWriter + KpiDeriver ✅
- 8 KPI cards 全字段接入（min_cpa_nm/tcpa_min_s/max_rudder_deg/grounding_risk_score/...）✅
- `simulation_settings.backend: demo|ros2` feature flag ✅

### Workstream G — D1.3.1 鉴定报告 · ✅ 完整

- 7 deliverable docs + README + annex/ccs-communication-schedule（01-overview 14K / 02-MMG-fidelity 14.5K / 03-determinism 12.9K / 04-sensor-confidence 11.6K / 05-orchestration-trace 11.7K / 06-evidence-matrix 10.5K / 07-ccs-mapping 11.8K）
- `tools/sil/d1_3_1_determinism_replay.py` + `d1_3_1_mmg_fidelity.py` + test fixture
- `test_m7_fmi_isolation.py`

------

## ❌ 遗漏 / 不完整项（须修补）

### 🔴 **OMIT-1（高优先）** GAP-001 仅部分关闭 — CI 仍 jazzy

`.gitlab-ci.yml` 现状：

```
line 41:  image: mass-l3/ci:jazzy-ubuntu22.04    # default ❌
line 230: source /opt/ros/jazzy/setup.bash       # ❌
line 435,554,576,609: source jazzy               # ❌（5 处）
line 632,635,653,656: humble ✅（仅 sil-smoke + sil-baseline 2 job）
```

docker-compose + Dockerfile 都改了 humble，但 **CI 主 pipeline 仍跑 jazzy 镜像**。 **风险**：CI 通过不代表生产能跑（容器镜像与 CI 镜像不一致）；DEMO-1 实际部署时可能出现 ROS2 包二进制不兼容。 **修复**：把 `.gitlab-ci.yml` default image + 5 处 `source jazzy` 全切 `mass-l3/ci:humble-ubuntu22.04` + `source /opt/ros/humble/setup.bash`。

### 🔴 **OMIT-2（高优先）** sil_mock_publisher 未退役

```
src/sim_workbench/sil_mock_publisher/{launch, sil_mock_publisher}/  ❌ 仍存在
```

Workstream F 范围内承诺退役（GAP-002）。`l3_external_mock_publisher` 已删，但 `sil_mock_publisher` 漏。 **修复**：确认无下游 import 后 `git rm -r src/sim_workbench/sil_mock_publisher`，更新 `docker/sil_nodes.Dockerfile` `--packages-select` 列表。

### 🟡 **OMIT-3（中优先）** 4 屏文件命名 GAP-014/029 未执行

```
web/src/screens/:
  BridgeHMI.tsx       ❌ 应为 SimulationMonitor.tsx
  Preflight.tsx        ❌ 应为 SimulationCheck.tsx
  RunReport.tsx        ❌ 应为 SimulationEvaluator.tsx
  ScenarioBuilder.tsx  ❌ 应为 SimulationScenario.tsx
```

App.tsx 路由 `#/builder` `#/preflight/:id` `#/bridge/:id` `#/report/:id` 也未改为 `#/scenario` `#/check/:id` `#/monitor/:id` `#/evaluator/:id`。 **修复**：一次性 git mv + import 重命名 + 路由更新（~2 小时工时）。

### 🟡 **OMIT-4（中优先）** Imazu-22 PASS 率 12/22 = 54.5%

`test-results/imazu22_results.json`：

- **PASS 12 / FAIL 10**
- FAIL 列表：imazu-01-ho / 02-cr-gw / 04-cr-so / 05/06/08/09/11/14/18-ms
- 失败根因：`E1.1_geometric_compliance: false` —— 例 imazu-01：`dcpa_no_action_m=5855m` 远大于 `max_dcpa_no_action_m=926m`，意味场景几何"无碰撞风险"，无法触发避碰
- V&V Plan X1.5 要求 **22/22 PASS + CPA ≥ 200m ratio ≥ 95% + COLREGs class ≥ 95%**
- DEMO-1（6/15）允许（visual demo only），但 **DEMO-2（7/31）必须闭口**

**修复**：场景几何重新标定（迁移工具可能丢失了 cog/sog 的几何匹配性）— 重审 10 个 FAIL 场景的 ownShip vs targetShips 相对位姿；或检查 `tools/run_imazu22.py` 的 E1.1 判定逻辑（是否过严）。

### 🟡 **OMIT-5（中优先）** monaco-editor 未集成（GAP-022 NICE）

`web/package.json` 仅含 `ajv: ^8.20.0`，缺 monaco-editor。Builder 客户端 YAML 实时 schema 报错未实现 — 用户改坏 YAML 须等服务端 422。 **修复**：`npm i @monaco-editor/react monaco-editor` + 接入 ScenarioBuilder.tsx，配 JSON Schema 验证。

### 🟢 **OMIT-6（低优先）** GAP-032 DNV 完整规范未访问

7 篇 D1.3.1 文档基于 DNV-RP-0513 / CG-0264 公开摘要 + V&V Plan v0.1 引用，**未见付费规范完整 PDF 落地证据**。`annex/` 下未发现完整 PDF 副本或对应条款引用清单。 **修复**：D1.3.1 鉴定报告正式提交 CCS / DNV surveyor 前从 DNV 官方渠道购买 PDF；annex/ 补 license 合规副本或条款映射表（保留可追溯）。

### 🟢 **OMIT-7（低优先）** 套件文档 vs 实现轻微漂移

- 套件 Doc 4 §3.1.1 `head_on.yaml`（已删除）+ §3.1.4 `5c93bf30f54c.yaml`（已归档到 `scenarios/_archived/`）仍被引用
- 场景库现 32 个（原 35 - 3 删除），套件文档需在下次 v1.1 修订时更新数字 **修复**：套件 v1.1 修订时统一回填（DEMO-1 通过后批量做）。

### 🟢 **OMIT-8（低优先）** pytest 全量回归无近期证据

`test-results/pytest-report.json` 当前 `total=0`（污染或未跑）。426 测试套件实际 PASS/FAIL 比例无法立即判断。 **修复**：`pytest --json-report` 全量跑一次刷新 baseline。

------

## DEMO 时间线评估

| Milestone               | 日期       | 距今   | 主要风险                                                     |
| ----------------------- | ---------- | ------ | ------------------------------------------------------------ |
| DEMO-1 Skeleton Live    | 2026-06-15 | 31 天  | 低 — 4 屏 visual demo 已通过，6-gate sequencer 就位          |
| DEMO-2 Decision-Capable | 2026-07-31 | 77 天  | **中** — Imazu-22 PASS 率须从 54.5% 提到 ≥95%；L3 kernel 真实链路接通待验 |
| DEMO-3 Full-Stack       | 2026-08-31 | 108 天 | Workstream H 尚未启动                                        |

------

## 建议立即修补顺序（高 → 低）

1. **OMIT-1**：`.gitlab-ci.yml` default image + 5 处 source 切 humble（~1 小时）
2. **OMIT-2**：`sil_mock_publisher` 退役 + Dockerfile 更新（~2 小时）
3. **OMIT-4**：Imazu-22 10 个 FAIL 场景几何标定 / 判定逻辑审查（~1 周，DEMO-2 前关闭即可）
4. **OMIT-3**：4 屏文件重命名 GAP-014/029（~2 小时，DEMO-1 前一次性）
5. **OMIT-5/6/7/8**：可推到 DEMO-1 通过后或 v1.1 修订批量处理

------

需要我直接修补 OMIT-1（CI humble）+ OMIT-2（sil_mock_publisher 退役）这两个高优先项吗？或先派 subagent 调查 OMIT-4（Imazu-22 几何标定根因）？