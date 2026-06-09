# M6 onset-latch 泛化修复提示词（新对话用）

> 复制下面整段到**新对话**。用途:修 M6 COLREGs onset-latch 的泛化缺陷 —— 它只稳住了
> 标准正遇,其余几何(偏舷正遇/追越/穿越/直航)的 conflict_detected 翻转抖动。
> 用现成的 8 探针 + Phase B 打分器当"捕虫器"(before/after 计数 + 逐 trace 时间线定位)。
> 基线 commit:`bce31a92`(main / origin main / gitlab l3-tdl 三端同步,含 Phase B)。

---

```
任务:修 M6 COLREGs 推理器的 onset-latch 泛化缺陷。Phase B(bce31a92)已证实 M6 修复
21a640b5 只稳住标准正遇(rule14-ho)和正遇/穿越边界(cs-edge);其余 5 个 give-way/stand-on
探针 conflict_detected 高频翻转(14-68 次/run)→ M4(AVOID↔TRANSIT)/M5(VALID↔EMPTY)/
舵(fishtail)跟着抖。用现成 8 探针 + Phase B 打分器捕捉并验证修复。

## 运行环境(独立 worktree,别串会话)
- 新建 worktree off main(用 superpowers:using-git-worktrees):分支 feat/m6-colreg-generalize。
  main=bce31a92 已含 Phase B 打分器 + 8 探针 + bridge M6 trace,直接可用。
- A4000 走 **scp 部署,不走 git**(CLAUDE.md §13:A4000 工作树有并行会话未提交 docs,
  **严禁 git pull/reset**)。M6 是 C++:scp 改后容器内 `colcon build --packages-select
  m6_colregs_reasoner --cmake-args -DBUILD_TESTING=OFF` + `docker compose ... restart sil-nodes`。
- A4000:`ssh a4000`→192.168.121.50;仓库 ~/Code/mass-l3;orchestrator https://127.0.0.1:18000。
  当前 A4000 已在 fixed-M6 GREEN 基线、bridge 已 trace M6。⚠️ 单驱动纪律(env_disturbance
  wedge):测试期只用 CLI、别开前端;卡死复位 `docker compose ... restart sil-nodes` 等 30s。

## 背景:bug 现象与已定位的层(勿重新发现,直接深挖)
- **症状**(A4000,真实模块,rate 10):/l3/m6/colregs_constraint.conflict_detected 每 ~7-15s
  翻 true↔false。各探针 conflict_toggles:ho-port 46 · ot 68 · cs 22 · cs-2 14 · cr-so 48
  (干净的 ho/cs-edge 只有 2)。role 振荡 GIVE_WAY(1)→FREE(3)→**STAND_ON(0)/INDEPENDENT_ACTION**
  (正遇绝不该变直航)。rule14-ho 在 t≈69 才 onset 且贯穿保持到 past-clear(t=326);
  rule14-ho-port(仅偏 5°)t≈1 就 onset 然后开始抖。
- **层 = M6**(trace 证实 M4/M5 忠实跟随 M6 的 conflict 抖动,非 M4/M5 自身问题)。
- **机制**:RuleLatch(`src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/
  rule_latch.hpp`,仅对 Rule 14/15,见 colregs_reasoner_node.cpp 的 latch 块 ~L532-600)
  onset 门槛 = `rule_active && cpa_m<cpa_safe && range_closing`,释放 = `opening &&
  past_and_clear(|rel_brg|>112.5°)`。偏轴几何下,本船右转把目标转出规则锥,eval.is_active
  闪烁;latch 该顶住却没顶住 → conflict 抖。
- **另案**:rule15-ot-boundary 行为干净但 min-CPA 161m<500m 下限(穿越/追越边界避让幅度不足)
  —— 大概率 M4/M5 转向幅度,不是 latch。分开处理或先放。

## 现成捕虫器(直接用,别造新的)
- **批量绿红表**(before/after 总指标):A4000 host `cd ~/Code/mass-l3 && MPLBACKEND=Agg
  python3 scripts/run_6_scenarios.py` → runs/batch_colregs_results.json(每场景 stability_kpis/
  checks/overall_pass)。当前 2/8 PASS。修复目标:give-way 探针 conflict_toggles≤2、stability_pass。
- **逐 trace 时间线定位 flap 起点**(单场景驱动后跑;先驱一个最小复现 ho-port):
  ```python
  # /tmp/tl.py —— M6/M4/M5 压缩转移时间线
  import json
  recs=[json.loads(l) for l in open("runs/trace_current.jsonl") if l.strip()]
  start=0
  for i in range(1,len(recs)):
      if recs[i].get("sim_t",0)+1.0<recs[i-1].get("sim_t",0): start=i
  run=recs[start:]
  def key(r):
      t=r.get("topic")
      if t=="/l3/m6/colregs_constraint": return ("M6",r.get("conflict_detected"),r.get("primary_role"),r.get("phase"))
      if t=="/l3/m4/behavior_plan": return ("M4",r.get("behavior"),r.get("avoidance_active"))
      if t=="/l3/m5/avoidance_plan": return ("M5",r.get("solver_status"))
  prev={}; 
  for r in run:
      k=key(r)
      if k and prev.get(k[0])!=k: print(round(r.get("sim_t",0),1),k); prev[k[0]]=k
  ```
- **Phase B 打分器**(纯 Python,可逐 trace 调):src/sim_workbench/sil_nodes/scoring/scoring/
  stability_scorer.py;`analyze_stability(run_records, role="give_way", init_heading_deg=0.0)`。
  单测 `pytest tests/sim_workbench/scoring/test_stability_scorer.py`(9 例,本地跑)。

## ── PART 1:复现 + 仪表化(systematic-debugging Phase 1) ──
1. 跑 run_6 确认 2/8 + 记下各 give-way 探针 conflict_toggles。
2. 单驱 ho-port(最小复现:仅比通过的 ho 偏 5°),跑 /tmp/tl.py 看 conflict 在哪个 sim_t 掉、
   掉时 role/phase 是什么、M4/M5 怎么跟。（已知:t≈31 conflict 掉到 false/role3,然后反复。）
3. **加 M6 内部仪表**(关键,不靠猜):在 RuleLatch::update 与 node 的 latch 块(L532-600)
   打日志,每 cycle 输出:rule_id、eval.is_active(raw)、cpa_m、range_closing、past_and_clear、
   latched_、has_onset_、onset_role_、最终 conflict_detected。scp+build+restart 后单驱 ho-port,
   `docker logs mass-l3-sil-sil-nodes-1` 看为何:
   - ho-port t≈1 就 onset 而 ho 要 t≈69?(CPA/risk/锥分类偏轴差异)
   - latch 为何没在 is_active 闪烁时顶住?(onset 门槛 range_closing 首 cycle 不可得?
     还是 latch 压根没 engage,conflict 由 raw 规则携带?)
   - role 为何窜到 STAND_ON(0)?(Rule 14/15 inactive 后哪条规则补位)

## ── PART 2:根因假设 + 最小验证(systematic-debugging Phase 3) ──
形成单一假设再改(别多处同时改)。候选(按 trace 证据排序,逐个证伪):
- H1 onset 门槛 `range_closing` 首 cycle 无 prev_target_range → ho-port 早 onset 走 raw 规则、
  latch 未 engage;raw 一出锥就 inactive 且 latch 没接管 → conflict 掉。
- H2 Rule 14 头对头锥 ±6°,ho-port 偏 5° 贴边,本船一右转目标立刻出锥 → is_active 闪;
  latch engage 条件太严(需同时 cpa<safe & closing)在贴边/偏轴下时真时假。
- H3 latch 是 per-(mmsi,rule_id);偏轴时 Rule 14 与 Rule 15 交替 active → 两个 latch 互相
  让位,谁都没稳定 hold。
- H4 role→STAND_ON:Rule 14/15 inactive 时 Rule 17 补位(node L582-595 的 risk gate 放行)。
**改 M6 规则语义前** `/nlm-ask --notebook maritime_regulations` 核对(Rule 13(d) onset 锁、
Rule 14 贯穿、Rule 7 risk gate)。改最小一处,A4000 build+单驱 ho-port,/tmp/tl.py 看 conflict
是否变 2 次。不行→换假设,别叠改。

## ── PART 3:逐几何验证 + 回归 ──
- 单几何过了再 run_6 全跑。逐个确认 give-way 探针 conflict_toggles≤2、stability_pass。
  顺序:ho-port(贴边正遇)→ ot(追越)→ cs/cs-2(穿越)→ cr-so(直航,role 应稳定 0、
  premature_giveway<10°)。别为过一个几何而退化 ho/cs-edge(已绿)。
- **回归锁已就位**:Phase B 任何 flap→RED,改完 run_6 应 ≥7/8 PASS(ot-boundary 若仍 CPA 不足
  另算)。改完跑 `pytest tests/sim_workbench/scoring/test_stability_scorer.py`(9 例不破)。

## 纪律(强制)
- superpowers:systematic-debugging:先复现+M6 内部取证再改,单一最小改动,每步 A4000 实测。
- 改 M6 规则逻辑 = cert 相关:动手前 /nlm-ask --notebook maritime_regulations 核对规则语义。
- C++ 改只在 A4000 build(本地无 colcon/ROS);Phase B 打分器/生成器纯 Python 本地跑。
- 代码定位用 codegraph(codegraph_explore),禁暴力 grep。
- 改场景=改 tools/sil/gen_colreg_tier12.py 再 regen(别手改 YAML);Imazu 几何勿动。

## 产出
1. M6 内部仪表证据 + 单一根因(指向 rule_latch.hpp / colregs_reasoner_node.cpp 具体行)。
2. 最小修复 + 逐几何 A4000 验证(/tmp/tl.py conflict_toggles before/after 表)。
3. run_6 ≥7/8 PASS + pytest 9/9;更新 DEBUG_STATE.md、记忆 [[l3-m6-onset-latch-no-generalize]]、
   workspace_log。
4. 完成后 feat 分支提交 → 切 main ff-merge → 三端同步(local=origin/main=gitlab l3-tdl)。
   ⚠️ main 可能领先 1 个 docs commit:ff 前先 `git rebase --autostash main`。
```

参考:绿红表细节 runs/batch_colregs_results.json;根因记忆 l3-m6-onset-latch-no-generalize;
Phase B 实现记忆 l3-colreg-probe-set-and-phaseb;原始修复 l3-m6-fishtail-onset-latch-resolved。
