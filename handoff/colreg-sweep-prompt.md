# COLREGs 探针扫测 + Phase B 提示词（新对话用）

> 复制下面整段到**新对话**。用途:在 A4000 上完整测试 8 个 COLREG 探针(真实模块),
> 然后实现 Phase B(打分层行为稳定性断言)主动逮 fishtail/flap 类 TDL 漏洞。
> 基线 commit:M6 修复 `21a640b5`,探针集 `8e586050`(均已三端同步)。
> 维护提示:8 探针由 `tools/sil/gen_colreg_tier12.py` 生成(唯一真源);场景说明见
> `scenarios/COLREGs测试/README.md`;Imazu-22 几何冻结勿动。

---

```
任务:在 A4000 上完整测试 scenarios/COLREGs测试/ 的 8 个 COLREG 探针(真实模块避碰/保向,
非 mock),验证 M6 修复泛化;然后实现 Phase B —— 把行为稳定性断言加进打分层,自动捕捉
fishtail/flap 类 TDL 系统漏洞。

## 运行环境(重要:独立 worktree,别串进别的会话)
- 在专属 worktree 执行:cd "~/Code/MASS-L3-Tactical Layer/.worktrees/colreg-phaseb"
  分支 = feat/colreg-phaseb(off main)。每次提交前确认 `git branch --show-current` = feat/colreg-phaseb。
- 完成后:在 feat/colreg-phaseb 提交 → 切主 checkout `git checkout main && git merge --ff-only
  feat/colreg-phaseb`(或 PR)→ push 三端(origin main + gitlab main:l3-tdl)。
- A4000 更新走 scp 不走 git(见下 PART 1 部署)。

## 背景(已完成基线,勿重修)
- M6 避碰段舵 fishtail 已根因修复:commit 21a640b5(M6 RuleLatch 在 onset 锁定 give-way
  分类、贯穿机动保持 Rule 13(d)、past-and-clear 释放 Rule 8(d) + 跨run latch 清除)。
  bridge 的 _m5_empty_sustained band-aid 已撤销(根因修复自足)。
- COLREGs 探针集已重构为 8 个单一目的探针:commit 8e586050(已在 main + 三端同步)。
  唯一真源 = tools/sil/gen_colreg_tier12.py;本地校验全绿(schema+M2分类+DCPA<500+
  test_simulate)。多船归 Imazu-22(scenarios/IMAZU标准测试/,冻结勿动)。
- 详见仓库根 DEBUG_STATE.md;记忆 l3-m6-fishtail-onset-latch-resolved;场景说明
  scenarios/COLREGs测试/README.md。COLREG 设计原则(onset-latch + commit-and-monitor)
  已用 NLM maritime_regulations/colav_algorithms 双 high 证实。

## 8 探针矩阵(scenario_id = 文件名;角色/期望动作/cpa_min/测点)
GIVE-WAY(本船应:稳定 AVOID、右转、保持锁定无抖、min CPA≥下限、past-clear 后回归原航线):
  colreg-rule14-ho          R14  右转  ≥926  纯正遇(M6 latch 回归基线)
  colreg-rule14-ho-port     R14  右转  ≥926  目标偏左5°仍须右转(绝不左转/不当穿越)
  colreg-rule13-ot          R13  右转  ≥926  追越;Rule13(d)方位前移不得重分类
  colreg-rule15-cs          R15  右转  ≥926  右舷穿越让路
  colreg-rule15-cs-2        R15  右转  ≥926  短-TCPA 反应(逼早动作)
  colreg-rule15-cs-edge     R15  右转  ≥500  边界:正遇/穿越交界(rel_brg 25°)
  colreg-rule15-ot-boundary R15  右转  ≥500  边界:穿越/追越交界(rel_brg 108°)
STAND-ON(本船应:不提前避让、保向、目标不让则末刻 17(b);role=0;无 fishtail):
  colreg-rule17-cr-so       R17  保向→17(b)  ≥500  左舷目标应让不让(直线replay)

## ───── PART 1:A4000 完整测试(8 个) ─────

### 先部署(A4000 走 **scp 部署,不走 git** —— A4000 的 gitlab fetch 不通且工作树有并行会话
未提交的 M1-M8 docs,**严禁 git reset/pull**,会毁并行会话的活)
- 8 个新场景**已 scp 到 A4000**(2026-06-09),orchestrator 每请求重扫,前端刷新即见。核对:
  curl -sk https://127.0.0.1:18000/api/v1/scenarios | grep -oE 'colreg-[a-z0-9-]+' | sort -u
  (应恰为 8 个,含 cs-edge/ot-boundary,无 ms-headon-cross 等旧的)。若缺,在**本地**:
  scp "scenarios/COLREGs测试/"colreg-*.yaml "scenarios/COLREGs测试/README.md" \
    'a4000:~/Code/mass-l3/scenarios/COLREGs测试/'   # 再 ssh 删 A4000 上多余旧 YAML
- M6 修复二进制**已 scp+build 在 A4000**(上一会话)。保险起见重建 M6 + 重启:
  docker exec mass-l3-sil-sil-nodes-1 bash -lc "source /opt/ros/humble/setup.bash;
    source /opt/ws/install/setup.bash; cd /opt/ws && colcon build --packages-select
    m6_colregs_reasoner --cmake-args -DBUILD_TESTING=OFF"
  docker compose -f docker-compose.yml -f docker-compose.a4000.yml restart sil-nodes; sleep 30
- 任何 A4000 更新一律 **本地改 → scp 同路径 → (代码)容器内 colcon build → restart**;
  **绝不在 A4000 git pull/reset**。

⚠️ 单驱动纪律:env_disturbance SetParameters 会被【双驱动并发】顶死(Configure failed:
   ...timed out 15s)。测试期间【前端别开/别驱动,只用 CLI】。卡死复位 = restart sil-nodes 等30s。
⚠️ patient gated drive,别快速循环(会触发 wedge)。

### 每场景驱动 + 采样(改 SID 逐个测)
ssh a4000 'bash -s' <<'EOF'
CID=mass-l3-sil-sil-nodes-1; B=https://127.0.0.1:18000/api/v1
RJ="source /opt/ros/humble/setup.bash; source /opt/ws/install/setup.bash 2>/dev/null"
SID="colreg-rule14-ho"          # ← 改这里逐个测 8 个
curl -sk -X POST $B/lifecycle/cleanup >/dev/null; sleep 4
curl -sk -X POST $B/lifecycle/configure -H 'content-type: application/json' -d "{\"scenario_id\":\"$SID\"}" >/dev/null
for i in $(seq 1 12); do sleep 2; curl -sk $B/lifecycle/status|grep -q inactive && break; done
curl -sk -X POST $B/lifecycle/activate >/dev/null; sleep 1
curl -sk -X POST $B/lifecycle/rate -H 'content-type: application/json' -d '{"rate":5}' >/dev/null
for i in $(seq 1 30); do
  hdg=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /fusion/own_ship_state 2>/dev/null"|grep -m1 heading_deg|grep -oE '[0-9.]+'|head -1)
  conf=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /l3/m6/colregs_constraint 2>/dev/null"|grep -m1 conflict_detected|grep -oE 'true|false')
  role=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /l3/m6/colregs_constraint 2>/dev/null"|grep -m1 primary_role|grep -oE '[0-9]+')
  beh=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /l3/m4/behavior_plan 2>/dev/null"|grep -m1 behavior|grep -oE '[0-9]+')
  rud=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /sil/actuator_cmd 2>/dev/null"|grep -m1 rudder_angle|grep -oE '\-?[0-9.e]+'|head -1)
  cpa=$(docker exec $CID bash -lc "$RJ; timeout 1.4 ros2 topic echo --once /l3/m2/world_state 2>/dev/null"|grep -m1 cpa_m|grep -oE '[0-9.]+'|head -1)
  echo "t=$((i*3))s hdg=$hdg conf=$conf role=$role M4=$beh rud=$rud cpa=$cpa"; sleep 1
done
EOF
# M4: 0=TRANSIT 1=AVOID。Role: 0=STAND_ON 1=GIVE_WAY 2=BOTH_GIVE_WAY 3=FREE。rudder_angle 单位 rad。

### 每场景判据
GIVE-WAY:conflict 接近段稳定 true【不翻转】;M4 稳定 AVOID;航向单调右转后【锁定】无锯齿;
         rudder 不 fishtail(无 20R↔35L 来回);min cpa ≥ 下限;past-clear 后 M4→TRANSIT、
         航向回原航线(回初始 hdg)。
STAND-ON:航向≈初始【保向】不提前大转;role 应为 0;M4 不长期 AVOID(或仅末刻);
         min cpa ≥ 下限;无 fishtail。
通杀红旗:航向锯齿/rudder fishtail · conflict 2-4s 翻转 · M5 plan VALID↔EMPTY 翻转 ·
         U转/绕圈 · CPA<下限 · stand-on 却提前大幅转向 · give-way 该右转却左转。
建议顺序:① ho/ho-port/ot/cs(验 latch 泛化)→ ② cs-2/cs-edge/ot-boundary(边界)→ ③ cr-so(stand-on)。
失败先定位层:M6(分类/latch)→ M4(行为/方向,审计疑【硬编码右转】,crossing/stand-on 最易暴露)
→ M5(plan VALID/EMPTY)→ bridge。别盲改。

## ───── PART 2:Phase B —— 打分层行为稳定性断言(逮 fishtail/flap) ─────

纯 CPA 判据逮不到行为 bug(M6 fishtail 就是例证:几何 fallback 也能拉开 CPA → 假绿)。
本部分把【行为质量】断言固化进打分层,自动覆盖所有场景(COLREGs测试 + Imazu),给 M6
修复上回归锁,并主动发现新漏洞。

打分层在 src/sim_workbench/sil_nodes/scoring/(node.py 消费 run 记录;hagen_scorer.py /
rule_compliance_evaluator.py / kpi_deriver.py / arrow_writer.py)。已有 KPI:min_cpa_nm /
max_rudder_deg / avg_rot_dpm / grounding_risk_score。

### 要加的稳定性 KPI + 断言(数据源:已订阅的话题流)
1. conflict_toggle_count —— /l3/m6/colregs_constraint.conflict_detected 在接近段的真假翻转次数。
   PASS ≤ 2(onset 上升 + past-clear 下降)。>2 = M6 flap。
2. plan_validity_toggle_count —— /l3/m5/avoidance_plan VALID(waypoints非空)↔EMPTY 翻转次数。
   PASS ≤ 2。>2 = M5 plan flap。
3. rudder_reversal_count / rudder_reversal_rate —— /sil/actuator_cmd.rudder_angle 符号反转次数
   (避碰段)。PASS ≤ 阈值(如 ≤3)。超 = fishtail。(复用 max_rudder_deg 旁加。)
4. heading_hold_stability —— 避碰"保持段"(从航向稳定到 past-clear)航向方差/抖动幅度。
   PASS < 阈值(如 std<2°)。
5. role_onset_fixed —— /l3/m6/colregs_constraint.primary_role 在机动期间不变(Rule 13(d):
   onset 固定)。变化 = 重分类 bug。
6. (stand-on)premature_giveway —— stand-on 场景本船在 17(b) 触发前的最大航向偏移 < 阈值
   (如 <10°)。超 = 误触发避让。

### 接线
- 每条 KPI 计算放 kpi_deriver.py(或新 stability_scorer.py),输入 = node.py 已收的 run_records
  时间序列(behavior_plan/avoidance_plan/colregs_constraint/actuator_cmd/own_ship_state)。
- 阈值:schema 的 metadata.expected_outcome 允许 additionalProperties → 可加可选字段
  (如 max_conflict_toggles/max_rudder_reversals/...);未配则用 scorer 默认。改 schema 时
  同步 scenarios/fcb_traffic_situation.schema.json 并跑 tools/validate_scenarios.py --all。
- 总裁决:场景 PASS = CPA 判据 AND 全部稳定性断言 PASS。把稳定性结果并入 hagen_scorer 输出/
  arrow metrics,run_6_scenarios.py / a4000-acceptance 能读到绿/红。

### Phase B 验收(回归锁自证)
- 修复版(8e586050):8 探针的稳定性断言应【全绿】(conflict_toggle≤2、rudder_reversal 低、
  航向锁定)。
- 反证:临时 git stash 掉 M6 rule_latch onset 保留(或 checkout 21a640b5^ 的 colregs_reasoner_node.cpp)
  → 重建 M6 → 跑 colreg-rule14-ho → 稳定性断言应【转红】(conflict_toggle 飙升、rudder fishtail)。
  证明断言真能逮这个 bug。验毕恢复。
- 单元:给新 KPI 写 pytest(合成时间序列喂 scorer,断言 toggle/reversal 计数正确)。

## 纪律(强制)
- superpowers:systematic-debugging:先复现+取证(各 topic)再改,单一最小改动,每步 A4000 实测。
- 改 M6 规则逻辑 = cert 相关:动手前 /nlm-ask --notebook maritime_regulations 核对规则语义。
- build/test 只在 A4000(本地无 colcon/ROS);但生成器/打分纯 Python 可本地跑。
- 代码定位用 codegraph(codegraph_explore),禁暴力 grep。
- 改场景 = 改 gen_colreg_tier12.py 再 regen(别手改 YAML);Imazu 几何勿动。

## 产出
1. PART 1:8 探针 A4000 绿/红表 + 每个红的根因层定位 + 修复(每步 A4000 验证)。
2. PART 2:打分层稳定性断言代码 + 单元测试 + 反证回归锁(反转 M6 应转红)+ 文档
   (README.md Phase B 段、DEBUG_STATE.md、记忆、workspace_log)。
3. 完成后 commit + 三端同步(local main = origin/main = gitlab l3-tdl)。
```
