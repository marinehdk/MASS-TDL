# COLREGs C1 Crossing Beam Fix — Design Spec

**日期**：2026-06-18
**状态**：Draft（待用户审批）
**范围**：修正 C1 phase-gate 的 abaft-beam 判据在横越场景的几何不可达问题
**分支/worktree**：`.worktrees/colregs-behavior-fix`（HEAD `8e9faaf8`，clean）
**前置**：
- INDEX `docs/superpowers/indexes/2026-06-18-colregs-avoidance-c1c7-verification.md`
- Spec `docs/superpowers/specs/2026-06-18-colregs-c1c7-compliance.md`（C1/C7 合规核对，本 spec 是其修正）
- Spec `docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md`（phase-gate 原始设计）
- 规则权威：`COLREG-Consolidated-2018.pdf`

---

## 1. 问题陈述

### 1.1 已证明的几何不可达（核心 bug）

C1 phase-gate 要求横越让路场景 release 时 target 相对 release-reference-heading 的 bearing > 112.5° abaft beam（Rule 13(b) 追越扇区边界）。几何仿真证明：

| 场景 | 目标 | CPA 后 rel_bearing 渐近值 | 112.5° 可达？ |
|---|---|---|---|
| `colreg-rule15-cs-edge`（cog=215, 29kn，SW 通过）| -136° | ✅ t=200s 达 113.5° |
| `colreg-rule15-cs`（cog=290, 10.6kn，NW 通过）| **-87°** | ❌ **几何不可达** |

**根因**：本船右转避让后东移，目标 NW 方向离去，目标方位渐近 -87°（恰在 port beam 前），**永不进入 abaft sector**。112.5° 是 Rule 13(b) 追越扇区定义，对横越是过严约束。

内部 `COLREGs_8Probe_Complete_Design_Report.md §4.2` 自己写 `abaft_threshold = 112.5 if is_overtaking else 90.0`——**crossing 应用 90°，但 C1 gate 与 M6 release 都用了 112.5°，spec 内部自相矛盾**。

### 1.2 三次失败历史（why not just re-raise to 112.5）

| commit | 改动 | 结果 |
|---|---|---|
| `ea6b06e6` | bow-clear 40°→112.5° | rule15-cs release@1830s（>total_time 1200），route_return=False，**回归** |
| `e4e2cc37` | 回退到 c849f06c（40°）| rule15-cs route_return=True，但 C1 RED at rel_brg=36°（40° 太松）|
| 现状（`8e9faaf8`）| 40° bow-clear | C1 RED 持续，**未解决** |

**结论**：纯 bearing 阈值在 [40°, 112.5°] 之间无解——40° 太松（早回航线），112.5° 对慢速横越不可达。需要**按规则类型分层** + **多条件兜底**。

---

## 2. COLREGs 合规依据（A 级 + NLM 🟢 high）

### 2.1 Rule 8(d) "finally past and clear" 正确含义

> "The effectiveness of the action shall be carefully checked until the other vessel is **finally past and clear**."

NLM maritime_regulations（🟢 high, 124 sources）：
- "finally past and clear" 是**定性** "ordinary practice of seamen"，无单一数学阈值
- 工程代理 = **目标无残余碰撞风险**：`tcpa<0`（已过 CPA）+ `range≥cpa_safe 且 opening` + `forward-CPA≥cpa_safe`

### 2.2 Sector 阈值分层（修正 spec 内部矛盾）

| 规则 | 扇区 | abaft-beam 阈值 | 来源 |
|---|---|---|---|
| Rule 13 Overtaking | [112.5°, 247.5°] | **112.5°** | Rule 13(b) "more than 22.5° abaft her beam" + Rule 21(c) sternlight 135° arc |
| Rule 14 Head-on / Rule 15 Crossing | 非追越 | **90°**（正横）| 内部报告 §4.2；beam 即 90°，追越才需 +22.5° |

**推导**：112.5° = 90°(beam) + 22.5°(Rule 13(b) overtaking offset)。追越需更严（近平行航向，target 始终近 beam），横越/对头 beam（90°）即"已过正横"。

### 2.3 与既有 spec 的关系

`2026-06-17-colregs-phase-semantics-gate.md` §C1 写 "bearing > 112.5°" 未区分规则类型。本 spec 是其 **errata**：crossing/headon 用 90°，overtaking 用 112.5°（C7 管）。原始 spec 的 Rule 8(d) 条款依据不变，仅阈值分层。

---

## 3. 设计方案（用户已选：M6 + evaluator 双改）

### 3.1 三处改动点（按规则类型分层 + 多条件兜底）

**改动 A — C1 evaluator（`scripts/run_6_scenarios.py`）**：
当前 C1（line 798-811）对所有 give-way 非 rule13 用 112.5°。改为：
- **主几何门**：`rel_brg_abs > 90.0`（crossing/headon 过正横）
- **兜底多条件**（满足任一即 C1 过）：
  - `tcpa < 0`（已过 CPA，Rule 8(d) "past" 的直接信号）AND `range ≥ cpa_safe` AND `range opening`
  - 或 `rel_brg_abs > 90°` AND range opening AND forward-CPA ≥ cpa_safe
- **rule13（overtaking）**：保持 112.5°（由 C7 管，C1 在 rule13 不触发，line 798-799 已排除）

**改动 B — M6 release_policy.hpp bow-clear 阈值**：
`kGiveWayProjectionReleaseReferenceBowClearDeg` 当前 40.0。该常量仅用于 `give_way_projection_release_safe` 的 `REFERENCE_CLEAR` gate（横越/对头 projection-release 路径，line 88-109）。
- 改为 **90.0**（crossing/headon 过正横才允许 projection release）
- `CURRENT_ABAFT` gate（Rule 14 headon，line 107-108）保持 `kGiveWayProjectionReleaseCurrentAbaftDeg=150.0` 不变

**改动 C — M6 reasoner_node.cpp `past_and_clear_from_heading`**：
当前 line 176-178 硬编码 112.5°，用于 `finally_resolved`（line 616-618）+ 4 个 latch 调用。改为按规则类型分层：
- 新增重载：`past_and_clear_from_heading(bearing, ref_heading, abaft_threshold_deg)`，默认 112.5°（保持追越兼容）
- Overtaking（Rule 13）用 112.5°，Crossing/Headon 用 90.0°
- **判断规则类型**：用 **rule13 latch 的 onset snapshot**（`rule_latches_[(mmsi<<8)|13]`，在 rule13 evaluate cycle 时按 Rule 13(d) 冻结 `onset_encounter_`）。在 `finally_resolved`（line 616-618）计算点：
  ```cpp
  const auto rl13 = rule_latches_.find((static_cast<uint64_t>(mmsi) << 8) | 13ULL);
  const bool is_overtaking =
      rl13 != rule_latches_.end() && rl13->second.has_onset() &&
      rl13->second.onset_encounter() == EncounterType::OVERTAKING;
  const double abaft_threshold = is_overtaking ? 112.5 : 90.0;
  ```
  需给 `RuleLatch` 加 `onset_encounter()` public getter（line 87 区已有 `onset_role()`，对称加一个）。
  - `finally_resolved` 用此 threshold 算 `past_and_clear`（line 616-618）。
  - duty latch / standon latch 调用（line 713/795/855）的 `past_and_clear` 用同一 threshold（提前在 per-target 块算一次，复用）。
  - fallback：rule13 latch 未 onset（首次 cycle 或非追越）→ is_overtaking=false → 90°（保守）。
  - 这避免坐标循环依赖（aspect sector 与 rel_brg past-clear 同坐标系会自指），且符合 Rule 13(d) "classification fixed at onset" 原则。

### 3.2 为什么用 90° 而非纯几何无关（用户已选）

纯几何无关（选项2）放弃 bearing 门会**弱化对"机械右转即回航线"bug 的检测力**——该 bug 的强信号正是"目标还在 bow 就回航线"。90° beam 保留这信号，同时修慢速不可达。叠加 `tcpa<0` 兜底确保"过正横"伴随"已过 CPA"，防止单纯 90° 仍可能早释放（如目标高速过 beam 但 CPA 还在前）。

### 3.3 不改的部分

- C7（Rule 13 overtaking）：保持 112.5°（追越扇区定义，C7 line 918-931 已用 along-axis，不依赖 bearing）。当前 `rule13-ot` C7=False 是 baseline 状态（`overtake_required: False`，场景不要求完全通过），**不在本 fix 范围**。
- C2/C3/C4/C5/C6/C8：不动。
- `kGiveWayProjectionReleaseCurrentAbaftDeg=150.0`（headon current-abaft gate）：不动。
- SIL tracker 修复（`5ec267e8`）：已 commit 且保留，不动。

---

## 4. 验证条件（完成定义）

### 4.1 TDD（M6 C++，容器内 colcon build）

新增/修改测试 `test/test_colregs_release_policy.cpp` + `test_rule_latch.cpp`：
- RED：crossing target rel_brg=80°（<90°）→ projection release 应 False
- GREEN：crossing target rel_brg=95°（>90°）+ tcpa<0 + range≥cpa_safe → release True
- RED：crossing target rel_brg=95° 但 tcpa>0（CPA 还在前）→ release False（tcpa<0 兜底）
- Overtaking 保持 112.5°（回归测试，防退化）

### 4.2 C1 evaluator 单元测试（`tests/scripts/test_run_6_scenarios_gate.py`）

- 慢速横越轨迹（rel_brg 渐近 -87°，tcpa 过零）→ C1 past_clear True（90° + tcpa<0）
- 早回航线轨迹（release@rel_brg=36°, tcpa>0）→ C1 RED（40° 太松被捕获）

### 4.3 运行时验证（behavior-fix stack，`ROS_DOMAIN_ID=43`）

| 场景 | 验证 | PASS 标准 |
|---|---|---|
| `colreg-rule15-cs`（慢速 10.6kn）| C1 gate + 几何 KPI | C1 past_clear True（90° beam + tcpa<0）+ CPA/route_return/stability PASS |
| `colreg-rule15-cs-edge`（快速 29kn）| 回归 | C1 past_clear True + KPI PASS（不能退化）|
| `colreg-rule13-ot`| 回归 | C7 baseline 状态不变 + KPI PASS |

### 4.4 8-probe 严格 restart 回归（colregs-clean-8probe skill）

```bash
python3 scripts/run_colregs_clean_8probe.py \
  --restart-between-runs \
  --summary-out runs/batch_colregs_clean_post_c1beamfix.json
```
**PASS 标准**：8/8 scenario `overall_pass=true`。特别关注 rule15-cs（修后应过）、rule15-cs-edge/rule14-ho（不退化）。

### 4.5 spec errata（文档卫生）

`docs/superpowers/specs/2026-06-17-colregs-phase-semantics-gate.md` §C1 加 errata note：112.5°→crossing 90°/overtaking 112.5° 分层，指向本 spec。

---

## 5. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 90° 对某些横越仍过早（目标高速过 beam 但危险未消）| tcpa<0 兜底硬门（已过 CPA 才算 past）|
| M6 C++ rebuild 失败 | 容器内 `colcon build --packages-select m6_colregs_reasoner` + 重启 sil-nodes |
| rule14-ho（headon）受 90° 影响退化 | headon 用 `CURRENT_ABAFT=150°` gate（不改），`REFERENCE_CLEAR=90°` 是 backup 路径，主路径不变 |
| rule15-cs release 时机仍偏（90°+tcpa<0 后 route_return 时间不够）| 先验证，若 route_return fail 则调 `tcpa<0` 为 `tcpa< -X`（过 CPA 后 X 秒）|
| 整合时与主 checkout phase-gate-diag 冲突 | 本 fix 在 worktree 独立分支，主 checkout 脏改动 Plan Phase 2 discard（INDEX §8）|

---

## 6. 开放问题

无（用户已选 M6+evaluator 双改 + 90° beam + 多条件）。实施中若发现 90°+tcpa<0 仍过早/过晚，回到此 spec 调参。

---

**Spec 结束。待用户审批后写 Plan。**
