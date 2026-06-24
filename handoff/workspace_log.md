# MASS-L3 TDL — Workspace Handoff Log

---

## [2026-06-24] Agent: ZCode — COLREGs 测试体系 v1 阶段①（渲染/接线 gap 补齐）

- **Git Commits**: `7ddddc41` / `ccea5ccc` / `56472c29`（branch: `codex/colregs-generalization-debug`, worktree: `.worktrees/colregs-generalization-debug`）
- **任务目标**: 补齐实现 session 留下的三个渲染/接线 gap，让新算出的 timing_consistency / G-ART / phase 证据接入输出流水线（dashboard + TraceEvaluationReport + evidence folder），为阶段②③④提供可观测的分层 verdict。

### 核心改动

| Gap | Commit | 内容 |
|-----|--------|------|
| 1 timing_consistency 接线 | `7ddddc41` | 新增 `_m6_conflict_cleared_time`（从 `/l3/m6/colregs_constraint` trace 提 conflict true→false 转换点）；`compute_phase_semantics` 填 `timing_consistency`（premature flag + recovery_t + conflict_cleared_t + gap_s），随 `phase_sem` 进 batch_summary |
| 2 G-ART runner 接入 | `ccea5ccc` | `_compute_artifact_consistency` 构 verdict/timeline 调 `check_consistency`；`run_scenario` 填 `result.artifact_consistency`；main loop 写 `<scenario>.artifact_consistency.json` + batch print G-ART 行 |
| 3 dashboard+report 渲染 | `56472c29` | `TraceEvaluationVerdict` 加 `artifact_pass`；layers 加 `L8_artifact_consistency`（+failure_root_cause）；report 加 `phase_evidence`；dashboard GATE 卡加 Artifact tile、layers 渲染 L1-L8、Trace Signals 加 G-ART timing 行 + C1/C4/C5 avoid-window 分数 |

### 当前状态: **GREEN — 阶段①完成判据全通过**

容器验证证据（run-19ef7f8c362）：
- `timing_consistency.premature_recovery_before_rule_release=true`, gap=**36.0s**（M4@857.5s release vs M6@893.5s conflict clear）
- `artifact_consistency.g_art_ok=false`, `failure_root_cause=PREMATURE_RECOVERY_BEFORE_RULE_RELEASE`
- TraceEvaluationReport `L8=FAIL`, `verdict.artifact_pass=false`, `phase_evidence` 完整透出
- dashboard PNG（162KB）渲染：`G-ART timing=PREMATURE M4@858s/M6@894s gap=36.0s` + Artifact tile=FAIL + L8=FAIL
- evidence folder 含 `colreg-rule14-ho.artifact_consistency.json`（242B）

全量回归 **126 passed**，1 pre-existing fail（`test_clean_probe_yaml_declares_expected_probe_horizons`，rule14-ho total_time=3000 vs 测试期望 1200，YAML 陈旧，与本工作无关）。

### Handoff Notes

**环境关键事实**：
- colregs-generalization-debug worktree 的 stack 用 `docker-compose.behavior-fix-isolation.yml`：orchestrator 端口 **18001**（非默认 18000），DDS_DOMAIN=43
- runner 默认 `BASE=https://127.0.0.1:18000/api/v1`，必须 `source scripts/local-behavior-fix-env.sh` 或显式 `SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1`
- `--restart-container` 用 `colregs-generalization-debug-sil-nodes-1`（非 `mass-l3-sil-sil-nodes-1`）
- 容器 label project=`colregs-generalization-debug`，working_dir 指向本 worktree

**重要观察**：rule14-ho 真实 integration defect 确认存在——M4 在 sim_t=857.5s 回 TRANSIT，但 M6 conflict_detected 直到 893.5s 才清（gap 36.0s，比设计文档预估的 24.9s 更大）。G-ART 现在精确捕获这个，不再埋进 phase_semantics_ok。

**下一步：阶段②单模块功能测试**。入口 `scripts/run_colregs_clean_8probe.py --scenario <id> --restart-between-runs --restart-container colregs-generalization-debug-sil-nodes-1`，从 trace 提各模块输出喂 `colregs_module_oracle.evaluate_m6_oracle` / `evaluate_m4_oracle` 等。重点验证 M6（rule/role/direction/latch）、M4（状态机+时序前置）。

---

## [2026-06-22 11:17] Agent: Antigravity (IDE)

- **Git Commit**: `82711a84` (branch: `codex/colregs-behavior-fix`, worktree: `.worktrees/colregs-behavior-fix`)
- **任务目标 (Goal)**: 修复 COLREGs 探针 8 场景中剩余的失败场景（rule13-ot, rule15-cs, rule15-cs-2, rule15-cs-intelligent）

---

### 核心改动 (Actions)

#### 1. M6 振荡 Bug 修复 ✅
**文件**: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`

**问题根因**: 当 `projection_resolved` 触发时，虽然清除了 `rule_latches_`、`give_way_latches_`、`standon_latches_`、`encounter_reference_heading_`，但 **没有** 清除 `encounter_fsms_`（EncounterStateMachine 对象）。导致 FSM 在下一轮推理时被重新评估，立刻再次触发 conflict_detected=true，形成无限振荡（AVOIDANCE → release → AVOIDANCE → …）。

**修复**: 在 `projection_resolved`/`finally_resolved` 触发块（~line 705）增加：
```cpp
encounter_fsms_.erase(rule13_key);
encounter_fsms_.erase(rule14_key);
encounter_fsms_.erase(rule15_key);
```

#### 2. YAML 坐标恢复 ✅
本次 session 曾尝试将 rule15-cs 目标初始位置推远（提升初始 TCPA > 720s），但导致场景完全失败（targets=0）。原因调查：
- 目标船由 `target_vessel_node` 仿真，通过 `default_targets_json` 参数配置，走 `/sil/target_vessel_state → sil_topic_bridge → /fusion/tracked_targets → M2` 链路
- M2 无距离过滤（dynamic_horizon_nm=5nm 只用于 ENC 查询，不过滤 target）
- 失败根因未完全确定（配置 without start 期间 targets=0 属正常）
- **决策**: 恢复原始坐标，验证 M6 fix 是否已足够修复场景

恢复的文件：
- `scenarios/COLREGs测试/colreg-rule15-cs.yaml` → target lat=63.461426, lon=10.437108, total_time=2000s
- `scenarios/COLREGs测试/colreg-rule15-cs-2.yaml` → target lat=63.455, lon=10.438105, total_time=1800s
- `scenarios/COLREGs测试/colreg-rule15-cs-intelligent.yaml` → target lat=63.461426, lon=10.437108, total_time=2000s
- `scenarios/COLREGs测试/colreg-rule13-ot.yaml` → target lat=63.454979, lon=10.381756, total_time=3600s
- `scenarios/COLREGs测试/colreg-rule15-ot-boundary.yaml` → 坐标也恢复

---

### 当前状态 (Status): **YELLOW — M6 fix 已提交，但场景验证未完成**

#### 12 场景全景（截至本次 session）

| 场景 | 状态 | 说明 |
|------|------|------|
| `colreg-rule14-ho` | ✅ PASS | 已通过（前序 sessions） |
| `colreg-rule14-ho-port` | ✅ PASS | 已通过 |
| `colreg-rule14-ho-intelligent` | ✅ PASS | 已通过 |
| `colreg-rule17-cr-so` | ✅ PASS | 已通过 |
| `colreg-rule17-cr-so-target-giveway` | ✅ PASS | 已通过 |
| `colreg-rule13-ot` | ❓ UNKNOWN | M6 fix 后未完整验证 |
| `colreg-rule13-ot-target-giveway` | ❓ UNKNOWN | 未运行 |
| `colreg-rule15-cs` | 🔴 FAILING | 最新结果 bp_transitions=[(26.9,0)]，无避碰（但那次跑的是坐标错误版本）。M6 fix 后原始坐标版本**尚未验证** |
| `colreg-rule15-cs-2` | 🔴 FAILING | 同上，未验证 |
| `colreg-rule15-cs-intelligent` | 🔴 FAILING | 同上，未验证 |
| `colreg-rule15-cs-edge` | ❓ UNKNOWN | 未运行 |
| `colreg-rule15-ot-boundary` | ❓ UNKNOWN | 未运行 |

---

### 接力指示 (Hand-off Context)

#### 环境
- **Worktree**: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix`
- **Branch**: `codex/colregs-behavior-fix`
- **Docker project**: `COMPOSE_PROJECT_NAME=colregs-behavior-fix`
- **容器**: `colregs-behavior-fix-sil-nodes-1`, `colregs-behavior-fix-sil-orchestrator-1`
- **Orchestrator**: `https://127.0.0.1:18000`
- **ROS_DOMAIN_ID**: 42

#### 立即要做的事

**Step 1**: 重启 sil-nodes 并等待 180s
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/colregs-behavior-fix
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose restart sil-nodes
sleep 180
```

**Step 2**: 先跑 rule15-cs 单场景验证 M6 fix 是否生效
```bash
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule15-cs 2>&1 | tee /tmp/cs_verify.log
```

**期望结果**: `bp_transitions` 应该出现 behavior=2（AVOIDANCE），`cpa_ok=True`，`overall_pass=True`

**如果 rule15-cs 还是 bp_transitions=[(26.9, 0)]（无避碰）**:
- 查看 M6 日志: `docker logs colregs-behavior-fix-sil-nodes-1 2>&1 | grep -i "conflict\|encounter\|FSM\|ACTIVE\|PREPLAN\|release"`
- 问题可能在 ESM PREPLAN→ACTIVE 的 t_plan_s 门限（720s）。rule15-cs 的 TCPA=553s < 720s，理论上应立刻进 ACTIVE。
- 若 M6 仍不报 conflict，检查 M2 是否实际把 target 发到了 `/fusion/tracked_targets`（`docker logs | grep "on_tracked_targets"`）。

**Step 3**: 完整 4 场景批量跑（cs + cs-2 + cs-intelligent + rule13-ot）
```bash
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule15-cs \
  --scenario colreg-rule15-cs-2 \
  --scenario colreg-rule15-cs-intelligent \
  --scenario colreg-rule13-ot \
  --restart-between-runs \
  --restart-container colregs-behavior-fix-sil-nodes-1 \
  --restart-settle 180 \
  2>&1 | tee /tmp/batch4_run.log
```

#### 关键代码定位

| 组件 | 文件 | 关键点 |
|------|------|--------|
| M6 振荡修复 | `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` | Lines 720-722: `encounter_fsms_.erase(...)` |
| ESM 状态机 | 同文件 Lines ~740-790 | `PREPLAN→ACTIVE` 在 `TCPA <= t_plan_s(720s)` |
| 释放门限 | `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp` Lines 80-117 | `projection_resolved`, `give_way_reference_heading_release_safe` |
| target_vessel_node | `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py` Lines 251-337 | `on_configure` 读 `default_targets_json`; `on_activate` 创建发布者和 10Hz timer |
| sil_topic_bridge | `docker/sil_topic_bridge.py` Lines 866-896 | `/sil/target_vessel_state → /fusion/tracked_targets` |
| M2 参数 | `src/l3_tdl_kernel/m2_world_model/config/m2_params.yaml` | `dynamic_horizon_nm: 5.0`, `cpa_safe_m: [1852.0, 555.6, 277.8, 2778.0]` |

#### Geometry Reference (rule15-cs)
- Own ship: 63.44N, 10.38E, COG=0°, SOG=12kn
- Target: 63.461426N, 10.437108E, COG=290°, SOG=10.61kn
- Initial range ≈ 3700m, bearing ≈ 50° (starboard bow) ✓
- Initial TCPA ≈ 553s → 直接进 ESM ACTIVE（< t_plan_s=720s）
- CPA ≈ 1617m (without maneuver)
- give_way_vessel = own ship (target on starboard bow, Rule 15/16)

#### 已探明的架构事实
1. `target_vessel_node.on_configure()` 读 `default_targets_json` (JSON string) → 创建 `TargetVessel` 对象（replay mode = dead-reckoning 直线运动）
2. `target_vessel_node.on_activate()` 创建 10Hz sim-time timer，步进并发布到 `/sil/target_vessel_state`
3. `sil_topic_bridge` 订阅 `/sil/target_vessel_state` 并桥接到 `/fusion/tracked_targets`（M2 的目标输入）
4. M2 无距离门限过滤（track_buffer 接受所有 TrackedTargetArray）
5. 目标在 configure-only 状态下不发布（需 ACTIVE 后才发布）
6. `ais_replay_node` 和 `target_vessel_node` 是两条完全独立的路径；COLREGs 场景实际使用 `target_vessel_node`

---

## [2026-06-19～21] 前序 Sessions Summary

### 前序已完成
- rule14-ho/ho-port/ho-intelligent, rule17-cr-so/target-giveway: 全部 PASS
- M4 route_return 逻辑修复（Fix-A1/A2/A3、Fix-B、Fix-C 等多轮修复）
- sil_topic_bridge latch-release 逻辑修复

### 前序 M6 振荡根因发现
- `encounter_fsms_` 未随 latches 一起清除 → 本次已修复并提交
