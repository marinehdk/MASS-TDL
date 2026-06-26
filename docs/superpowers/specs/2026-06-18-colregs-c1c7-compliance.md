# COLREGs C1/C7 合规核对与验证 Spec

**日期**：2026-06-18
**状态**：Draft（待 Plan 审批）
**范围**：C1（Rule 15 crossing release）+ C7（Rule 13 overtaking release）热修复的 COLREGs 合规性核对、citation 修正、运行时验证条件
**INDEX**：`docs/superpowers/indexes/2026-06-18-colregs-avoidance-c1c7-verification.md`

**前置**：
- 已 commit 代码：worktree `colregs-behavior-fix` HEAD `ea6b06e6`
- 规则权威：`COLREG-Consolidated-2018.pdf`（附件）
- NLM 验证：maritime_regulations domain（124 sources, 🟢 high）

---

## 1. 背景与问题陈述

### 1.1 C1/C7 热修复已落地（代码层）

worktree `colregs-behavior-fix` commit `ea6b06e6` 已完成：

**C1（Rule 15 crossing release）**：
- `colregs_release_policy.hpp:17` 常量 `kGiveWayProjectionReleaseReferenceBowClearDeg` 从 40° 改 112.5°
- 作用域限定：reference bow-clear 仅作用于 `REFERENCE_CLEAR` gate，不 block Rule 14 head-on 的 `CURRENT_ABAFT` gate
- 解决问题：phase gate 诊断 rule15-cs 在 `rel_brg=40°`（目标仍在 bow）时过早 release（Rule 8(d) "finally past and clear" 违规）

**C7（Rule 13 overtaking release）**：
- `colregs_release_policy.hpp:164` 新增 `give_way_overtake_release_safe()`
- 判据：`aspect < 90°`（own-ship 在 target 前方半球）+ `cpa_projection_past_and_safe` + `range >= cpa_safe` + `!range_closing`
- 从 Rule 15 的 reference bow-clear 路径分离（近平行追越航向，target 始终在 bow，relative-bearing gate 永不开）
- 解决问题：rule13-ot release 判据几何错误

### 1.2 三个 COLREGs 合规问题（调研已核对）

**🔴 问题 A — C1 citation 错误**：
`release_policy.hpp:11-13` 注释 + commit message 写 "112.5° abaft beam (COLREG Rule 3(g))"。

**NLM 核对**（maritime_regulations, 🟢 high）：
> Rule 3(g) defines "vessel restricted in her ability to manoeuvre" ... has nothing to do with defining "abaft the beam".

**正确来源**：Rule 13(b) "more than 22.5 degrees abaft her beam" + Rule 21(c) sternlight 135° arc（67.5° from right aft each side）→ 112.5° from bow。

**几何值 112.5° 正确**，仅 citation 错。

**🟡 问题 B — C7 aspect<90° 判据合规性**：
`give_way_overtake_release_safe()` 用 `aspect < 90°`（own-ship 在 target 前方半球）作 overtake release 主几何判据。

**NLM 核对**（maritime_regulations, 🟢 high）：
- "forward hemisphere alone is insufficient"（Steamship Mutual: "always safer to cross astern"）
- COLREGs Rule 13(d) "finally past and clear" 是定性 "ordinary practice of seamen"，无单一数学阈值
- 但配合 `cpa_projection_past_and_safe` + `range >= cpa_safe` + `!range_closing` 三条件，符合 MASS 算法工程近似

**几何判据可接受**，但需文档说明依据 + 局限。

**🔴 问题 C — 主 checkout 脏改动重复**：
主 checkout `codex/colregs-phase-gate-diag` 9 文件 dirty = worktree `ea6b06e6` 已 commit 内容副本。两处同源副本，整合卫生风险。

---

## 2. COLREGs 合规依据（A 级来源，NLM 验证）

### 2.1 Sector 划分（Rule 13(b) + Rule 21(c)）

| Sector | 相对方位角（from target bow）| 规则 | 来源 |
|---|---|---|---|
| HEAD_ON | [337.5°, 360°) ∪ [0°, 22.5°] | Rule 14 | 遮光弧对称 |
| CROSSING_GIVE_WAY | (22.5°, 112.5°) | Rule 15 | 非对头非追越 |
| **OVERTAKING** | **[112.5°, 247.5°]** | **Rule 13** | **Rule 13(b) 22.5° abaft beam + Rule 21(c) sternlight 135° arc** |
| CROSSING_STAND_ON | (247.5°, 337.5°) | Rule 15 | 非对头非追越 |

**推导**：Rule 21(c) sternlight 135° arc = 67.5° × 2（from right aft）。180° - 67.5° = 112.5°，180° + 67.5° = 247.5°。

### 2.2 Past-and-clear 判据（Rule 13(d) + Rule 8(d)）

| 规则 | 条款 | 要求 |
|---|---|---|
| Rule 13(d) | overtake duty 持续 | "...until she is **finally past and clear**" |
| Rule 8(d) | 行动有效性核查 | "...carefully checked until the other vessel is **finally past and clear**" |

**NLM 结论**（🟢 high）：
- "finally past and clear" 无单一数学阈值，是定性 "ordinary practice of seamen"
- forward hemisphere **alone 不足**，需配合 safe distance + no risk of collision
- 多因素工程近似（aspect + CPA + range + closing）合规，但需文档标注

### 2.3 内部设计交叉验证

内部 `COLREGs_8Probe_Complete_Design_Report.md`（GLM-5.1/多人评审, 🟢 High）§4.2 写：

```python
abaft_threshold = 112.5 if is_overtaking_situation else 90.0
# Rule 13 追越更严（112.5° abaft beam）
# Rule 14/15 常规 abaft（90°）
```

**与 C1/C7 代码一致**：C1 用 112.5°（crossing，实际应 90°，见下），C7 用 aspect<90°。

⚠️ **潜在不一致**：内部报告说 crossing 用 90°，但 C1 代码用 112.5°。**需核对**：C1 的 112.5° 是给 `REFERENCE_CLEAR` gate（reference heading 沿避让航向的 bow-clear），不是 target 的 abaft beam。两者坐标系不同，**不冲突**，但 Spec 须澄清。

### 2.4 Rule 8(b)/16 ample time + substantial alteration（背景，非本日改动）

| 参数 | 值 | A 级判例 | NLM 状态 |
|---|---|---|---|
| min_alteration_deg | ≥30° | *The Roseline* 2 Lloyd's Rep. 411; *The Oden* 1 Lloyd's Rep. 280; *The Hakki Deval* EWHC 2809 | 🟢 high |
| C-12 ample time | 12 min before collision | *The Samco Europe* 2 Lloyd's Rep. 579 (8.1 nm); *The Rickmers Genoa* EWHC 1949; *The Topaz* 2 Lloyd's Rep. 18 (C-10~C-8 stand-on, C-5 mandatory) | 🟢 high |

本日**不改**这些阈值（属全 FSM spec 范围）。

---

## 3. 合规三红线处理决策

### 3.1 红线 A — C1 citation 修正（必做）

**改动**（仅文档，不动几何）：
- `colregs_release_policy.hpp:11-13` 注释：`Rule 3(g)` → `Rule 13(b) + Rule 21(c)`
- commit `ea6b06e6` message 已 immutable（不改写历史），在**新 commit** 补正
- Spec §3.3.2（2026-06-17 全 FSM spec）引用错误处加 errata note

**新注释内容**：
```cpp
// COLREG Rule 8(d) / 15 past-and-clear for crossing give-way: the target must
// be abaft the beam (relative bearing >= 112.5 deg along the reference avoidance
// heading) before the encounter is resolved. 112.5 deg derives from Rule 13(b)
// ("more than 22.5 degrees abaft her beam", 90 deg beam + 22.5 deg = 112.5 deg
// from bow) and Rule 21(c) (sternlight 135 deg arc = 67.5 deg from right aft
// each side). NOT Rule 3(g), which defines "vessel restricted in her ability
// to manoeuvre". A 40 deg bow-clear threshold releases while the target is
// still on the bow and the own-ship is still altering -- the "early return to
// route" the phase gate flags as a Rule 8(d) violation.
```

### 3.2 红线 B — C7 文档补强（必做，几何可选加 backstop）

**改动**（文档 + 可选几何）：

**必做**：`give_way_overtake_release_safe()` 注释补强：
```cpp
// COLREG Rule 13(d): an overtaking give-way vessel is finally past and clear
// only once it has crossed into the target's forward hemisphere (aspect ahead
// of the beam) at a safe range with a past/safe CPA projection.
//
// COLREGs compliance note [ref-engineering-approximation]:
// Rule 13(d) "finally past and clear" is a qualitative "ordinary practice of
// seamen" standard (NLM maritime_regulations, high confidence). Forward
// hemisphere alone is insufficient (Steamship Mutual: "always safer to cross
// astern"). This function implements a multi-factor engineering approximation:
//   aspect < 90 deg (forward hemisphere) AND
//   cpa_projection_past_and_safe (CPA past, safe distance) AND
//   range >= cpa_safe (range hard floor) AND
//   !range_closing (no remaining closing risk)
// Together these approximate "safely ahead with no remaining risk of collision".
// The crossing bow-clear gate (kGiveWayProjectionReleaseReferenceBowClearDeg)
// is geometrically meaningless for near-parallel overtaking (target stays on
// bow throughout), so overtake must use aspect, not relative bearing.
```

**可选**（用户 sign-off）：加 `abs(rel_bearing) >= 112.5°` backstop 作为额外几何硬门，使 C7 与 C1 同标准。**默认不加**（多条件已合规，加可能过严致 rule13-ot 永不 release）。开放问题 INDEX §8 已列。

### 3.3 红线 C — 主 checkout 脏改动处理（必做）

**Plan Phase 2 决策**，两个选项：

**选项 1（推荐，默认）**：discard 主 checkout dirty
- worktree `ea6b06e6` 是 C1/C7 source of truth，主 checkout dirty 是同内容副本
- discard 后主 checkout 回到 `68b38095`（phase-gate 集成干净状态）
- C1/C7 通过 worktree 整合路径引入（Plan Phase 7）

**选项 2**：主 checkout 也 commit C1/C7
- 在主 checkout 单独 commit，与 worktree 形成"双 commit"
- 整合时需 rebase 或选一弃一，更复杂

**推荐选项 1**：worktree 是 codex 并行开发约定（AGENTS.md §parallel Codex），主 checkout 是整合面不应自留 feature commit。

---

## 4. 运行时验证条件

### 4.1 C1 验证（rule15-cs，Rule 15 crossing）

**场景**：`colreg-rule15-cs`（目标从右舷横越，本船 give-way）

**期望几何行为**：
1. onset：TCPA≤T_plan 时 ACTIVE，onset snapshot 冻结
2. 避让：本船右转 starboard
3. **release（C1 修复点）**：目标必须沿 reference avoidance heading 的 rel_bearing ≥ 112.5°（abaft beam）才 release
4. **修复前 bug**：rel_brg=40° 就 release（过早回航线，Rule 8(d) 违规）
5. **修复后期望**：release 推迟到 rel_brg≥112.5°

**验证命令**：
```bash
cd /Users/marine/Code/MASS-L3-Tactical\ Layer/.worktrees/colregs-behavior-fix
source scripts/local-a4000-env.sh
python3 scripts/run_6_scenarios.py --scenario colreg-rule15-cs --restart-settle 40
```

**PASS 标准**：
- C1 phase-gate：past-clear 从 False（rel_brg=40°）转为 True（rel_brg≥112.5°）
- 几何 KPI：CPA / route_return / stability 全 PASS（不能为过 gate 牺牲几何安全）

### 4.2 C7 验证（rule13-ot，Rule 13 overtaking）

**场景**：`colreg-rule13-ot`（本船追越慢速目标）

**期望几何行为**：
1. onset：Rule 13 几何成立（bearing∈[112.5°,247.5°], aspect≤30°, closing）时 ACTIVE
2. 避让：本船右转让清
3. **release（C7 修复点）**：own-ship aspect<90°（前方半球）+ CPA projection past/safe + range≥cpa_safe + !closing 才 release
4. **修复前 bug**：用 crossing reference_projection 路径，target 始终在 bow，gate 永不开
5. **修复后期望**：aspect 进入前方半球后 release

**验证命令**：
```bash
python3 scripts/run_6_scenarios.py --scenario colreg-rule13-ot --restart-settle 40
# timeout 需 >130s（追越场景长），wrapper 已内置 200s
```

**PASS 标准**：
- C7 phase-gate：overtake-past 从 False 转为 True
- 几何 KPI 全 PASS

### 4.3 8-probe 严格 restart 回归（整体行为）

**Skill**：`colregs-clean-8probe`

**命令**：
```bash
python3 scripts/run_colregs_clean_8probe.py \
  --restart-between-runs \
  --summary-out runs/batch_colregs_clean_post_c1c7.json \
  --trace-report-dir runs/trace_eval/local_clean8_post_c1c7_$(date +%Y%m%d_%H%M%S)
```

**PASS 标准**：8/8 scenario `overall_pass=true`（CPA + stability + route_return + corridor + risk_gate + seamanship_gate 全绿）

**已知预期**：
- C1/C7 改 release 时机，可能影响 rule15-cs / rule13-ot 的 route_return 时序
- 若几何 KPI 退化，**回退** C1/C7 重新调参（见 §5 风险）

---

## 5. 风险与缓解

| 风险 | 缓解 |
|---|---|
| C1 112.5° 过严致 rule15-cs 永不 release（卡 ACTIVE）| projection_release backup 仍存在（cpa_projection_past_and_safe），兜底释放 |
| C7 aspect<90° 过松致 rule13-ot 早 release | 多条件（CPA/range/closing）兜底；可选加 112.5° backstop（§3.2 开放）|
| 8-probe 因 release 时机变更退化 | 回退 C1/C7 重新调参；或接受部分退化（标注，不 block）|
| behavior-fix stack 与主 stack 冲突 | Plan Phase 3 独立 network + domain 43 偏移 |
| C1 citation 修正引入新 commit 污染 worktree 历史 | 单独 doc-only commit，不混功能改动 |

---

## 6. 验证条件总表（Spec 完成定义）

| 验证项 | 命令/产物 | PASS 标准 |
|---|---|---|
| C1 citation 修正 | `grep -n "Rule 3(g)" release_policy.hpp` | 无残留（全改 Rule 13(b)+21(c)）|
| C7 文档补强 | `give_way_overtake_release_safe` 注释含 [ref-engineering-approximation] + NLM 引用 | 注释完整 |
| 脏改动清理 | `git status` 主 checkout | 无 dirty（discard 后）或单独 commit |
| C1 运行时 | rule15-cs gate + 几何 KPI | C1 past-clear rel_brg≥112.5° + KPI PASS |
| C7 运行时 | rule13-ot gate + 几何 KPI | C7 overtake-past True + KPI PASS |
| 8-probe 回归 | colregs-clean-8probe skill | 8/8 overall_pass=true |
| 整合路径 | worktree ea6b06e6 → 主 checkout 路径明确 | 文档记录，是否本日合入延后 |

---

## 7. 参考来源汇总

**COLREGs 原文（附件 PDF）**：
- Rule 3(g)：vessel restricted in ability to manoeuvre（与 abaft beam 无关）
- Rule 8(b)/8(d)：substantial alteration / finally past and clear
- Rule 13(b)：22.5° abaft her beam（overtaking sector 起点）
- Rule 13(d)：finally past and clear（overtake duty 持续）
- Rule 21(c)：sternlight 135° arc（67.5° from right aft each side）

**NLM maritime_regulations（🟢 high, 124 sources）**：
- Sector [112.5°, 247.5°] = Rule 13(b) + Rule 21(c) 双重确认
- "finally past and clear" 定性标准，forward hemisphere alone 不足，多因素工程近似合规
- Rule 8 ≥30°：*The Roseline* / *The Oden* / *The Hakki Deval*
- C-12 12min：*The Samco Europe* (8.1nm) / *The Rickmers Genoa* / *The Topaz* (C-10~C-8 stand-on)

**项目内部**：
- `COLREGs_8Probe_Complete_Design_Report.md` §4.2 abaft_threshold 表（🟢 High）
- `encounter_classifier.py` sector 定义（[112.5°, 247.5°] overtaking）
- worktree `ea6b06e6` C1/C7 源码

**外部（WebSearch 辅助）**：
- Steamship Mutual *Overtaking* (Rule 13): "always safer to cross astern"
- RWU Law Review: *Overtaking or Crossing: Judicial Interpretation*

---

## 8. 开放问题（需 Plan 前确认）

1. **C7 backstop**：是否加 `abs(rel_bearing) >= 112.5°` 硬门？（默认不加）
2. **脏改动 discard vs commit**：默认 discard，可改 commit
3. **8-probe 跑 local 还是 A4000**：默认 local OrbStack，A4000 延后

---

**Spec 结束。下一步：Plan 实施步骤。**
