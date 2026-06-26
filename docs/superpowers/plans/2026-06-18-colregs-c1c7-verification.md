# COLREGs C1/C7 合规修复验证 Plan

> **For execution**: REQUIRED SUB-SKILL: superpowers:executing-plans or subagent-driven-development. Steps use `- [ ]` checkbox syntax.

**Goal**: 完成 C1（Rule 15 crossing）+ C7（Rule 13 overtaking）热修复的 COLREGs 合规核对、citation 修正、运行时验证、整合卫生闭合。

**Spec**: `docs/superpowers/specs/2026-06-18-colregs-c1c7-compliance.md`
**INDEX**: `docs/superpowers/indexes/2026-06-18-colregs-avoidance-c1c7-verification.md`

**NOT in scope**: 全 FSM 重写（spec/plan 2026-06-17）本日不执行。两线不并行。

---

## File Structure

**Modify (worktree `colregs-behavior-fix`)**:
- `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp` — C1 citation 注释修正 + C7 文档补强

**Modify (主 checkout)**:
- `docs/superpowers/specs/2026-06-17-colregs-avoidance-fsm-design.md` — §3.3.2 citation errata note
- (可选) `docs/Design/Review/2026-06-17/COLREGs_Avoidance_Decision_Logic_Report.md` — citation 引用核对

**No code geometry change**: C1 112.5° / C7 aspect<90° 几何值全部保留。

---

## Phase 0: Pre-flight 状态确认

### Task 0.1: 确认 worktree + 主 checkout 当前状态

- [ ] **Step 1: 确认 worktree `colregs-behavior-fix` HEAD**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
git log --oneline -3
# 期望: ea6b06e6 (fix m6 strict COLREG past-and-clear) 在顶
git status
# 期望: clean
```

- [ ] **Step 2: 确认主 checkout dirty 状态**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git status
# 期望: 9 文件 modified + 2 未追踪 scenario dir
git diff --stat HEAD
```

- [ ] **Step 3: 确认主 stack 健康（只读，不动）**

```bash
docker compose -p mass-l3-sil ps
# 期望: 7 容器 Up（不 stop/down/rm）
```

⚠️ **硬约束**: 主 stack 任何阶段都不碰。验证只在 behavior-fix 独立 stack。

---

## Phase 1: COLREGs 合规文档修正（worktree）

### Task 1.1: C1 citation 修正（release_policy.hpp）

**File**: worktree `colregs-behavior-fix/src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp`

- [ ] **Step 1: Read 当前注释（行 11-17）**

```bash
sed -n '9,25p' src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp
```

确认当前错误：行 11-13 写 "Rule 3(g) abaft the beam definition"。

- [ ] **Step 2: Edit 注释（仅注释，不动常量值 112.5）**

old_string (行 10-17):
```cpp
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
// COLREG Rule 8(d) / 15 past-and-clear for crossing give-way: the target must
// be abaft the beam (relative bearing >= 112.5 deg, the COLREG "abaft the
// beam" definition from Rule 3(g)) along the reference avoidance heading
// before the encounter is resolved. A 40 deg bow-clear threshold releases
// while the target is still on the bow and the own-ship is still altering --
// the "early return to route" the phase gate flags as a Rule 8(d) violation.
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 112.5;
```

new_string:
```cpp
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
// COLREG Rule 8(d) / 15 past-and-clear for crossing give-way: the target must
// be abaft the beam (relative bearing >= 112.5 deg along the reference
// avoidance heading) before the encounter is resolved. 112.5 deg derives from
// COLREG Rule 13(b) ("more than 22.5 degrees abaft her beam", 90 deg beam +
// 22.5 deg = 112.5 deg from bow) and Rule 21(c) (sternlight 135 deg arc =
// 67.5 deg from right aft each side). NOT Rule 3(g) -- Rule 3(g) defines
// "vessel restricted in her ability to manoeuvre" and is unrelated to the
// abaft-beam sector. A 40 deg bow-clear threshold releases while the target
// is still on the bow and the own-ship is still altering -- the "early return
// to route" the phase gate flags as a Rule 8(d) violation.
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 112.5;
```

- [ ] **Step 3: Verify 仅注释改，常量值不变**

```bash
git diff src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp
# 期望: 只有注释行变化，constexpr double ... = 112.5; 不变
```

### Task 1.2: C7 文档补强（give_way_overtake_release_safe 注释）

**File**: 同上，行 156-187 区域

- [ ] **Step 1: Read 当前注释**

```bash
sed -n '156,190p' src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp
```

- [ ] **Step 2: Edit 注释补强（[ref-engineering-approximation] + NLM 引用）**

在 `give_way_overtake_release_safe` 函数注释（行 156-163）追加 NLM 验证说明。新注释（替换行 156-163 的现有注释块）:

```cpp
// COLREG Rule 13(d): an overtaking give-way vessel is finally past and clear
// only once it has crossed into the target's forward hemisphere (aspect ahead
// of the beam) at a safe range with a past/safe CPA projection.
//
// COLREGs compliance note [ref-engineering-approximation]:
// Rule 13(d) "finally past and clear" is a qualitative "ordinary practice of
// seamen" standard (NLM maritime_regulations notebook, high confidence, 124
// sources). Forward hemisphere alone is insufficient (Steamship Mutual LP:
// "always safer to cross astern"; Rule 13(a) advises avoid crossing ahead).
// This function implements a multi-factor engineering approximation:
//   aspect < 90 deg (forward hemisphere) AND
//   cpa_projection_past_and_safe (CPA past, at safe distance) AND
//   range >= cpa_safe (range hard floor) AND
//   !range_closing (no remaining closing risk)
// Together these approximate "safely ahead with no remaining risk of
// collision" per Rule 13(d) + Rule 8(d). COLREGs intentionally has no single
// numerical threshold for "past and clear".
//
// The crossing bow-clear gate (kGiveWayProjectionReleaseReferenceBowClearDeg)
// is geometrically meaningless for near-parallel overtaking (target stays on
// the own-ship's bow throughout), so overtake must use aspect, not relative
// bearing.
```

- [ ] **Step 3: Verify 几何不动**

```bash
git diff src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp
# 期望: 仅注释块变化，函数签名 + 几何逻辑（aspect<90 等）不变
```

### Task 1.3: 单测回归（确认注释改不破编译）

- [ ] **Step 1: 容器内 colcon build + ctest**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
docker compose -p colregs-behavior-fix run --rm sil-nodes bash -c \
  "cd /opt/ws && colcon build --packages-select m6_colregs_reasoner 2>&1 | tail -20"
# 期望: BUILD SUCCEEDED（注释改不影响编译）
```

- [ ] **Step 2: run release_policy tests**

```bash
docker compose -p colregs-behavior-fix run --rm sil-nodes bash -c \
  "cd /opt/ws && colcon test --packages-select m6_colregs_reasoner --ctest-args -R ReleasePolicy 2>&1 | tail -30"
# 期望: 18 tests PASS（5 overtaking + crossing block/allow + head-on regression）
```

⚠️ 若 stack 未起（Phase 3 才起），跳到 Phase 3 后回来跑。

### Task 1.4: Commit citation 修正

- [ ] **Step 1: Commit（doc-only，不混功能）**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
git add src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp
git commit -m "docs(m6): fix COLREG citation Rule 3(g) -> Rule 13(b)+21(c)

The kGiveWayProjectionReleaseReferenceBowClearDeg comment and the ea6b06e6
commit message cited 'Rule 3(g)' as the source of the 112.5 deg abaft-beam
definition. NLM maritime_regulations verification (high confidence) confirms
Rule 3(g) actually defines 'vessel restricted in her ability to manoeuvre',
unrelated to the abaft-beam sector.

Correct source: Rule 13(b) 'more than 22.5 degrees abaft her beam' (90 deg
beam + 22.5 deg = 112.5 deg from bow) + Rule 21(c) sternlight 135 deg arc
(67.5 deg from right aft each side).

Geometry value 112.5 deg is unchanged -- only the citation.

Also adds [ref-engineering-approximation] note to give_way_overtake_release_safe
documenting that Rule 13(d) 'finally past and clear' is a qualitative standard
with no single numerical threshold; the aspect<90 + CPA/range/closing multi-
factor test is a MASS engineering approximation per Steamship Mutual / NLM.

Refs: NLM maritime_regulations (124 sources, high); COLREG-Consolidated-2018
Rule 13(b), 13(d), 21(c); COLREGs_8Probe_Complete_Design_Report.md §4.2."
```

---

## Phase 2: 主 checkout 脏改动清理

### Task 2.1: 确认脏改动 = worktree ea6b06e6 内容

- [ ] **Step 1: 比对主 checkout dirty vs worktree commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
# 三个 src 文件
for f in src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp \
         src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp \
         src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp; do
  echo "=== $f ==="
  diff <(git diff HEAD -- "$f") \
       <(cd .worktrees/colregs-behavior-fix && git show ea6b06e6 -- "$f" | tail -n +6) \
    | head -20
done
```

期望：diff 只剩 commit message artifact（无实质内容差异）。

- [ ] **Step 2: 确认 gate_*.json 与 scenario dir 来源**

```bash
git diff --stat HEAD -- scenarios/colreg-rule14-ho/.preflight/
# 6 个 gate_*.json modified
ls scenarios/colreg-rule14-ho-port/ scenarios/colreg-rule15-cs-edge/ 2>&1
# 2 未追踪 scenario dir
```

记录这些是 phase-gate 诊断产物还是 worktree 副本，决定一起 discard 还是保留。

### Task 2.2: 决策与执行（默认 discard）

**决策树**：
- 若 §2.1 确认 src 3 文件 = worktree ea6b06e6 内容 → discard src
- gate_*.json + scenario dir：
  - 若主 checkout 独有诊断产物（worktree 没有）→ **保留**，单独 commit 或留 working tree
  - 若也是 worktree 副本 → discard

- [ ] **Step 1: 检查 worktree 是否有 gate_*.json / scenario dir**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
ls scenarios/colreg-rule14-ho/.preflight/ 2>&1
ls scenarios/colreg-rule14-ho-port/ scenarios/colreg-rule15-cs-edge/ 2>&1
git log --oneline --all -- scenarios/colreg-rule14-ho/.preflight/gate_1.json | head -3
```

- [ ] **Step 2A（若选 discard src + 保留 gate）**:

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git checkout HEAD -- \
  src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp \
  src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp \
  src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp
git status
# 期望: src 3 文件 clean，gate_*.json + scenario dir 保留
```

- [ ] **Step 2B（若全 discard）**:

```bash
git checkout HEAD -- src/l3_tdl_kernel/m6_colregs_reasoner/
# scenario dir 用 git clean -nd 先预览
git clean -nd scenarios/colreg-rule14-ho-port/ scenarios/colreg-rule15-cs-edge/
# 确认后 git clean -fd（若用户同意）
```

- [ ] **Step 3: 确认主 checkout 回到干净状态（或仅留意图保留的产物）**

```bash
git status
git log --oneline -1
# HEAD 仍 68b38095
```

⚠️ **若选 commit 路径（用户改默认）**: 在主 checkout `git add` + `git commit -m "fix(m6): C1/C7 strict release (mirror worktree ea6b06e6)"`，并在 Plan Phase 7 整合时跳过 worktree merge。

---

## Phase 3: behavior-fix stack 环境偏移（避开主 stack）

### Task 3.1: 设计独立 stack 配置

**目标**：behavior-fix stack 不与主 stack `mass-l3-sil`（host network + domain 0 + ports 18000/18765）冲突。

**偏移方案**：
- docker network: 独立 bridge（非 host）
- ports: orchestrator 18001, foxglove 18766
- ROS_DOMAIN_ID: 43（主 stack 0）
- COMPOSE_PROJECT_NAME: `colregs-behavior-fix`（已设）

- [ ] **Step 1: Read 当前 compose override 结构**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
ls docker-compose*.yml
grep -l "host\|network_mode\|ROS_DOMAIN_ID\|18000\|18765" docker-compose*.yml
```

- [ ] **Step 2: 创建 worktree-local override（或用 env 注入）**

最简方案：用 env 变量注入偏移，不改 compose 文件（避免污染共享 compose）。

```bash
# worktree-local env（写 scripts/local-behavior-fix-env.sh，worktree 独有）
cat > scripts/local-behavior-fix-env.sh <<'EOF'
#!/bin/bash
# behavior-fix stack isolation from main mass-l3-sil stack
export COMPOSE_PROJECT_NAME=colregs-behavior-fix
export ROS_DOMAIN_ID=43
export ORCH_PORT=18001
export FOX_PORT=18766
# 独立 network（避免 host network 冲突主 stack）
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml:docker-compose.behavior-fix-isolation.yml
export SIL_NODES_CPUS=3.5  # 本机 4 CPU，留 0.5 给主 stack
EOF
chmod +x scripts/local-behavior-fix-env.sh
```

- [ ] **Step 3: 创建 isolation override compose**

```bash
cat > docker-compose.behavior-fix-isolation.yml <<'EOF'
# behavior-fix stack isolation: 独立 network + 端口偏移 + domain 偏移
# 不改 base compose，仅 override
services:
  orchestrator:
    ports:
      - "18001:18000"
    environment:
      - ROS_DOMAIN_ID=43
    networks:
      - behavior-fix-net
  foxglove:
    ports:
      - "18766:18765"
    environment:
      - ROS_DOMAIN_ID=43
    networks:
      - behavior-fix-net
  sil-nodes:
    environment:
      - ROS_DOMAIN_ID=43
      - SIL_NODES_CPUS=3.5
    networks:
      - behavior-fix-net

networks:
  behavior-fix-net:
    driver: bridge
EOF
```

⚠️ **需先确认 base compose 用 host network 还是 bridge**。若 base 已是 bridge + ports 映射，override 仅改端口 + domain 即可。若 base 是 `network_mode: host`，需 override 改回 bridge（更复杂）。

### Task 3.2: 起 behavior-fix stack

- [ ] **Step 1: 确认 certs 已复制**

```bash
ls certs/sil.crt certs/sil.key 2>&1
# 若缺失：从主 checkout 复制
cp "/Users/marine/Code/MASS-L3-Tactical Layer/certs/sil.crt" certs/
cp "/Users/marine/Code/MASS-L3-Tactical Layer/certs/sil.key" certs/
```

- [ ] **Step 2: 起 stack**

```bash
source scripts/local-behavior-fix-env.sh
docker compose up -d
docker compose ps
# 期望: orchestrator/foxglove/sil-nodes Up
```

- [ ] **Step 3: 确认主 stack 不受影响**

```bash
docker compose -p mass-l3-sil ps
# 期望: 主 stack 仍 7 容器 Up（不动）
```

- [ ] **Step 4: 确认 DDS domain 隔离**

```bash
docker compose -p colregs-behavior-fix exec sil-nodes bash -c \
  "echo ROS_DOMAIN_ID=\$ROS_DOMAIN_ID && ros2 topic list | head"
# 期望: ROS_DOMAIN_ID=43, topic list 非空（含 /l3/* 话题）
docker compose -p mass-l3-sil exec sil-nodes bash -c \
  "echo ROS_DOMAIN_ID=\$ROS_DOMAIN_ID"
# 期望: 主 stack 仍 domain 0
```

- [ ] **Step 5: 若 stack 起不来（host network 冲突等）**

回退方案：用 docker network inspect 确认冲突，或临时 stop behavior-fix 让主 stack 独占 host network 调试，但**主 stack 不 stop**。

记录失败原因到 diary，转 Plan Phase 3 Task 3.3。

### Task 3.3: scenario configure 验证（解开 handoff 提到的 configure 未通）

handoff 记录：behavior-fix stack Stage2 26s 收到 1 帧 own_ship 但后续 echo 不到 / runtime summary 0 节点 / 场景 configure 未验证通。

- [ ] **Step 1: 跑 rule14-ho（baseline，验证 stack 通）**

```bash
source scripts/local-behavior-fix-env.sh
curl -sk -X POST "https://127.0.0.1:18001/api/v1/integration/profile" \
  -H 'Content-Type: application/json' -d '{"name":"internal"}'
python3 scripts/run_6_scenarios.py --scenario colreg-rule14-ho --restart-settle 40 2>&1 | tail -30
# 期望: rule14-ho PASS（baseline 不该被 C1/C7 改动影响）
```

- [ ] **Step 2: 若 configure 未通，排查**

按 AGENTS.md §A4000 sync "If scenario configuration wedges around env_disturbance, suspect concurrent configure drivers"：
- 确认仅 CLI 一个 driver（不开 frontend）
- `docker compose -p colregs-behavior-fix restart sil-nodes` 后重试

---

## Phase 4: C1 运行时验证（rule15-cs）

### Task 4.1: 跑 rule15-cs + 验证 C1 gate

- [ ] **Step 1: 跑场景**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
source scripts/local-behavior-fix-env.sh
python3 scripts/run_6_scenarios.py --scenario colreg-rule15-cs --restart-settle 40 \
  2>&1 | tee runs/rule15_cs_post_c1c7_$(date +%Y%m%d_%H%M%S).log
```

- [ ] **Step 2: 检查 C1 phase-gate 输出**

从 log / trace 抓 C1 past-clear 判据：
```bash
# 期望: C1 past-clear 从 False (rel_brg=40°) 转为 True (rel_brg>=112.5°)
grep -E "C1|past.clear|release|rel_brg" runs/rule15_cs_post_c1c7_*.log | tail -20
```

- [ ] **Step 3: 检查几何 KPI**

```bash
# 从 trace report 抓 CPA / route_return / stability
ls runs/trace_eval/*rule15* 2>&1
python3 -c "
import json, glob
f = sorted(glob.glob('runs/trace_eval/*rule15*/**/*.json', recursive=True))[-1]
d = json.load(open(f))
print('CPA:', d.get('cpa_ok'))
print('stability:', d.get('stability_pass'))
print('route_return:', d.get('returned_to_route'))
print('overall:', d.get('overall_pass'))
"
```

- [ ] **Step 4: PASS 判定**

| 项 | 期望 |
|---|---|
| C1 past-clear | rel_brg>=112.5° 时 True（非 40° 早 release）|
| CPA | PASS（未为 gate 牺牲安全距离）|
| route_return | PASS（release 后正确回航线）|
| stability | PASS（无抖动）|
| overall | PASS |

- [ ] **Step 5: 若 FAIL**

按 systematic-debugging：
- 若 C1 卡 ACTIVE（112.5° 过严）→ 检查 projection_release backup 是否触发
- 若几何 KPI 退化 → 回退 C1 112.5°→中间值（如 90°）重测，记 diary
- 不强行改 gate 阈值放过

---

## Phase 5: C7 运行时验证（rule13-ot）

### Task 5.1: 跑 rule13-ot + 验证 C7 gate

- [ ] **Step 1: 跑场景（timeout >130s，wrapper 内置 200s）**

```bash
python3 scripts/run_6_scenarios.py --scenario colreg-rule13-ot --restart-settle 40 \
  2>&1 | tee runs/rule13_ot_post_c1c7_$(date +%Y%m%d_%H%M%S).log
```

- [ ] **Step 2: 检查 C7 phase-gate**

```bash
grep -E "C7|overtake.past|aspect|release" runs/rule13_ot_post_c1c7_*.log | tail -20
# 期望: C7 overtake-past 从 False 转为 True（aspect<90° + CPA/range/closing 条件）
```

- [ ] **Step 3: 几何 KPI（同 Phase 4 Step 3）**

- [ ] **Step 4: PASS 判定**

| 项 | 期望 |
|---|---|
| C7 overtake-past | aspect<90° 时 True |
| CPA/route_return/stability | PASS |
| overall | PASS |

- [ ] **Step 5: 若 FAIL**

- 若 C7 永不 release（aspect 判据有问题）→ 检查 aspect 计算坐标（colregs_reasoner_node.cpp aspect_deg 来源）
- 若早 release（几何退化）→ 考虑加 112.5° backstop（Spec §3.2 开放项）

---

## Phase 6: 8-probe 严格 restart 回归

### Task 6.1: 跑完整 8-probe（colregs-clean-8probe skill）

- [ ] **Step 1: Invoke skill 确认流程**

按 skill `colregs-clean-8probe`：
- restart-between-runs（每场景重启 sil-nodes）
- 单 CLI driver（不开 frontend）

- [ ] **Step 2: 跑 8-probe**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"
source scripts/local-behavior-fix-env.sh
python3 scripts/run_colregs_clean_8probe.py \
  --restart-between-runs \
  --summary-out runs/batch_colregs_clean_post_c1c7.json \
  --trace-report-dir runs/trace_eval/local_clean8_post_c1c7_$(date +%Y%m%d_%H%M%S) \
  2>&1 | tee runs/clean8_post_c1c7_$(date +%Y%m%d_%H%M%S).log
# 注意: exec 后台 timeout 调 1800s，不加 nohup&
```

预期场景顺序：
```
colreg-rule14-ho
colreg-rule14-ho-port
colreg-rule13-ot
colreg-rule15-cs
colreg-rule15-cs-2
colreg-rule15-cs-edge
colreg-rule15-ot-boundary
colreg-rule17-cr-so
```

- [ ] **Step 3: 抓 summary**

```bash
python3 -c "
import json
d = json.load(open('runs/batch_colregs_clean_post_c1c7.json'))
for s in d.get('scenarios', []):
    print(f\"{s['scenario']}: overall_pass={s.get('overall_pass')}\")
print('Total:', sum(1 for s in d['scenarios'] if s.get('overall_pass')), '/', len(d['scenarios']))
"
```

- [ ] **Step 4: PASS 判定**

期望 8/8 overall_pass=true。

- [ ] **Step 5: 若有红场景**

- 若是 rule15-cs / rule13-ot（C1/C7 直接受影响）→ 回 Phase 4/5 调参
- 若是其他场景退化（C1/C7 副作用）→ 评估是否接受（标注）或回退
- 每个红场景记 diary + trace 分析

---

## Phase 7: 整合路径决策 + 收尾

### Task 7.1: 决策 worktree → 主 checkout 整合时机

- [ ] **Step 1: 评估整合条件**

| 条件 | 状态 |
|---|---|
| C1 citation 修正 commit | worktree 已有 |
| C7 文档补强 commit | worktree 已有 |
| C1 运行时 PASS | Phase 4 |
| C7 运行时 PASS | Phase 5 |
| 8-probe 回归 PASS | Phase 6 |

- [ ] **Step 2: 整合选项**

**选项 A（本日整合）**：worktree `ea6b06e6` + citation commit cherry-pick / merge 到主 checkout `codex/colregs-phase-gate-diag`
**选项 B（延后整合）**：worktree 保留，主 checkout 不动，另立整合任务（含 A4000 sync + A4000 gate）

**默认选项 B**（本日已重，整合 + A4000 另立）。

### Task 7.2: 更新 2026-06-17 spec citation errata

- [ ] **Step 1: Edit spec §3.3.2 加 errata**

主 checkout `docs/superpowers/specs/2026-06-17-colregs-avoidance-fsm-design.md` §3.3.2 "112.5°" 引用处加：

```markdown
> **Errata 2026-06-18**: 此处原引用 "Rule 3(g) abaft beam" 有误。Rule 3(g)
> 实际定义 "vessel restricted in her ability to manoeuvre"。112.5° 正确来源
> 是 Rule 13(b) "22.5° abaft her beam" + Rule 21(c) sternlight 135° arc。
> 详见 `docs/superpowers/specs/2026-06-18-colregs-c1c7-compliance.md` §3.1。
```

- [ ] **Step 2: Commit（主 checkout docs-only）**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git add docs/superpowers/specs/2026-06-17-colregs-avoidance-fsm-design.md
git commit -m "docs(spec): errata Rule 3(g) -> Rule 13(b)+21(c) for abaft-beam 112.5 deg

NLM maritime_regulations verification (high) confirmed Rule 3(g) defines
'vessel restricted in her ability to manoeuvre', unrelated to abaft beam.
Correct source: Rule 13(b) + Rule 21(c). See 2026-06-18-c1c7-compliance spec."
```

### Task 7.3: diary_write + handoff

- [ ] **Step 1: mempalace diary_write（AAAK 格式）**

```python
# via mempalace_diary_write MCP
# wing: mass_l3_tactical_layer, topic: colregs-c1c7-verification-2026-06-18
```

内容骨架（AAAK）：
- **A**ction: C1/C7 合规核对 + citation 修正 + 运行时验证
- **A**rtifacts: INDEX/spec/plan 三件套 + citation commit + 8-probe 证据
- **A**ssessment: C1/C7 几何合规（NLM high），citation 错已修，运行时 [PASS/FAIL]
- **K**ey decisions: 本日只做热修不推进全 FSM；主 stack 不碰；behavior-fix stack domain 43 偏移

- [ ] **Step 2: handoff/workspace_log.md 追加**

```markdown
## [2026-06-18] ZCode / (worktree colregs-behavior-fix HEAD) / COLREGs C1/C7 合规验证
### Task Goal
C1(Rule15)+C7(Rule13) 热修复 COLREGs 合规核对 + citation 修正 + 运行时验证
### Core Changes
- citation 修正: Rule 3(g) -> Rule 13(b)+21(c) (worktree commit)
- C7 文档补强 [ref-engineering-approximation]
- 主 checkout dirty 清理 (discard/commit)
- behavior-fix stack domain 43 偏移隔离
### Current Status
[填 Phase 4/5/6 结果]
### Handoff Notes
- 全 FSM 重写 (2026-06-17 spec/plan) 未启动, 待热修验证绿后决策
- worktree ea6b06e6 + citation commit 待整合 (延后, 含 A4000 sync)
- 主 stack mass-l3-sil 全程未碰
```

- [ ] **Step 3: 收尾 stack（可选）**

```bash
# 若不再用 behavior-fix stack
docker compose -p colregs-behavior-fix down
# 主 stack 保持 Up
```

---

## Self-Review Notes

**Spec coverage**: 三红线全 mapped（A→Phase 1, B→Phase 1, C→Phase 2）。验证条件全 mapped（C1→Phase 4, C7→Phase 5, 8-probe→Phase 6）。

**几何不动保证**: Phase 1 仅改注释，Task 1.1 Step 3 + Task 1.2 Step 3 显式 verify 常量值 + 函数逻辑不变。

**主 stack 保护**: 全 Plan 唯一涉及主 stack 的命令是 `docker compose -p mass-l3-sil ps`（只读），Phase 3 Task 3.2 Step 3/4 仅确认不受影响。

**回退路径**:
- C1 过严 → projection_release backup 兜底，或回退 112.5°→90° 中间值
- C7 过松 → 加 112.5° backstop（开放项）
- 8-probe 退化 → 评估接受或回退 C1/C7

**未覆盖（明确 out of scope）**:
- 全 FSM 重写（2026-06-17 spec/plan）
- A4000 sync + A4000 gate（另立任务）
- D-1~D-8 完整闭合（全 FSM 范围）
- 主 stack 任何改动
