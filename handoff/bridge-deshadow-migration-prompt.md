# Bridge 去创可贴 / 影子战术层归并提示词（新对话用）

> 复制下面整段到**新对话**。用途:把 SIL bridge(`docker/sil_topic_bridge.py`)里
> 篡夺的 M4 决策 / M5+L4 制导 / M1 约束逻辑**归并回正确模块**,让实现回到 L3→L4→L5
> 设计(RFC-001 方案 B)。直接触发因:COLREGs 探针 `ot` 因 bridge 自有 release 与 M6
> 权威 conflict 互搏而 RED。基线 commit:`fbe100c4`(main / origin main / gitlab l3-tdl 三端同步)。

---

```
任务:把 SIL bridge(docker/sil_topic_bridge.py,1397 行)退化为「薄 ROS↔sim 翻译层」,
将它现在篡夺的战术决策 / 制导 / 控制 / 约束逻辑归并回设计规定的模块(M4 / M5 / L4-stub /
M1)。这是架构对齐任务(消除设计偏离),不是单点 bugfix。直接触发因 = COLREGs `ot` 探针
behavior_toggles=126(M6 conflict 已稳定 toggles=2,翻动来自 bridge 自有 release)。

## 运行环境(独立 worktree,scp 部署)
- 新建 worktree off main(superpowers:using-git-worktrees):分支 feat/bridge-deshadow。
  main=fbe100c4 已含 M6 onset-latch 泛化修复 + 8 COLREGs 探针 + Phase B scorer,直接可用。
- A4000 走 scp 部署不走 git(CLAUDE.md §13,禁 git pull/reset)。bridge 是 Python →
  scp 改后免 colcon build,直接 docker compose ... restart sil-nodes。M4/M5 若改(C++)需
  容器内 colcon build --packages-select <pkg> + restart。
- A4000:ssh a4000→192.168.121.50;仓库 ~/Code/mass-l3;orchestrator https://127.0.0.1:18000。
  ⚠️ 单驱动纪律:测试期只用 CLI、别开前端;卡死复位 docker compose ... restart sil-nodes 等 30s。

## 设计(权威,改前必读,勿凭记忆)
- 架构报告 docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md §893-906 + §15.1
  接口契约表 + RFC-001(docs/.../Cross-Team Alignment/RFC-decisions.md):
  - **M5 Mid-MPC → L4**:`AvoidancePlan` = WP[] + speed_adjustments(1-2 Hz)。L4 在避让模式
    用 AvoidancePlan **覆盖 L2 PlannedRoute**,以**自身 LOS+WOP**处理 → L5。
  - **M5 BC-MPC → L4**:`ReactiveOverrideCmd` =(ψ_cmd,u_cmd,ROT_cmd),事件/≤10 Hz 紧急。
  - **L4 始终是 (ψ,u,ROT) 的最终生成者**;M5 只给「任务级路径变更」(WP)+「紧急覆盖」。
  - 弃用方案 A 理由(§906):让 M5 自出 (ψ,u,ROT) 会重复 L4 的 LOS+WOP、且 M5 须自处理漂流
    补偿/look-ahead 等本应 L4 的职责。→ **bridge 当前正是在犯方案 A 的错,且更糟(连决策也夺)**。
- 顶层 ADR(CLAUDE.md §4,不可让步):
  - **ADR-1 ODD/M6 = 唯一权威**:行为/避让切换唯一来源是 M1 ODD + M6 COLREGs;算法/桥**禁止**
    各自维护「是否避让」判断。← bridge 违反此条(自有 release 与 M6 conflict 互搏)。
  - **ADR-4 Backseat Driver**:决策核心零船型常量,严禁 if vessel==FCB 进 A 层。
- SIL 现实:**本仓库 = L3,没有真 L4**。bridge 在「stub L4」——但 stub 得既越权(夺 M4 决策、
  M1 约束)又零散(创可贴)。目标是让 stub 干净、忠于设计、各归其位。

## 现状诊断:bridge = 影子决策+制导+控制引擎(篡夺清单,行号 @ fbe100c4)
读 docker/sil_topic_bridge.py 核对(codegraph 对 Python 弱,直接读 + grep):
1. **避让生命周期决策(arm/release)—— 应属 M4(消费 M6 conflict)**:
   - ARM:`_on_avoidance_plan`(L1003)在 M5 valid plan 到达时 self._avoidance_active=True。
   - RELEASE 有 **4 条并行路径**(全是越权决策):
     · `_check_geometry_release`(L848):TCPA<0 && DCPA≥CPA_SAFE → release。**← ot 元凶**:
       本船避让把目标推过 CPA → 每 ~6s 触发 → disarm → M5 下个 valid plan 立即 re-arm →
       avoidance_active 翻 126×(M6 conflict 全程稳定 true)。
     · `_on_threat_state`(L881):M2 cpa_status==cleared && astern → release。
     · `_on_mission_goal`(L893):M3 task_valid && behavior==TRANSIT → release。
     · `_on_avoidance_plan`(L1026):M5 plan 失效 → disarm。
   - `_AVOID_TRANSIT_RELEASE_S` teardown + `_trigger_latch_release`(L939)+ latch offset 衰减。
2. **避让航向反推 + 战术 clamp —— 应属 M5(plan)+ L4(LOS)+ M1(能力约束)**:
   - 从 **M4 behavior 窗口** heading_min/max 反推 target_heading(L1066-1084),而非消费 M5
     AvoidancePlan 的 waypoints。← 绕过了 M5 的 plan,所以才需要自己 clamp + 释放。
   - `MAX_AVOID_DEV_DEG = 60.0` 硬编码过转 clamp(L1087)。设计里 ±60° 是 MRM-03 文档化约束
     (架构报告 §1081)+ ROT_max 来自 M1 CapabilityManifest(§1456/§2426),不该硬编码进桥。
3. **(ψ,u,ROT)生成 / LOS —— 应属 L4 Guidance(LOS+WOP)**:
   - `HeadingController`(L146 Kp+rate-limit)、速度控制器(L164)、`_compute_avoidance_autopilot`
     (L1205 heading→rudder)、waypoint→rudder(turn_radius+SHIP_LENGTH_M L100)。
4. **路径回归 / XTE —— 应属 M4 TRANSIT + L4 LOS**:
   - `_route_wps` 缓存 + 几何 cross-track(L331/L522/L1272-1293)、`_target_heading_deg` 航迹自驾。

## ⚠️ 关键陷阱(勿重蹈覆辙)
- bridge 的这些 teardown/clamp/geometry-release **不是凭空创可贴 —— 是为修真 bug 加的**:
  记忆 [[l3-circling-root-cause-m5-valid-forever]] + [[l3-route-return-plumbing-4-breaks]]:
  曾有人(Antigravity)一把删掉 bridge 的 anti-circling teardown(−257 行)→ 立刻复发
  circling(loops=1.51)+ U-turn(−180°)。**所以不是「删 bridge 逻辑」,是「把等价行为正确
  实现到目标模块」再撤桥内副本**。每撤一块都要先在目标模块落地 + A4000 验证不复发 circling。
- bridge 极脆且与 route-return 强耦合。**增量迁移,每步保持 SIL GREEN**,别大爆改。

## 目标态(交付定义)
- **决策**:避让 arm/release 生命周期 = M4 行为仲裁器拥有,**唯一**由 M6 conflict_detected +
  ODD 驱动(ADR-1)。删除 bridge 的 4 条 release 路径与 _avoidance_active 自有状态机;M4
  behavior_plan 须真正承载 avoidance 生命周期(现 trace 里的 avoidance_active 是桥自己塞的)。
- **制导**:M5 AvoidancePlan(WP,已存在)→ 干净的 **L4-Guidance stub 模块**(LOS+WOP →
  ψ/u/ROT),**独立于桥**(放 src/sim_workbench/ 下的新节点,忠于设计,不是桥内方法)。
- **约束**:过转 clamp / ROT_max 来自 M5 plan 约束或 M1 CapabilityManifest,非硬编码。
- **桥**:退化为薄翻译——消费 L4 的 (ψ,u,ROT)(或 actuator_cmd)→ /sil/actuator_cmd 写入 sim;
  trace topics;QoS/时钟。零战术决策、零航向反推、零 release 判断。

## 待定的设计问题(brainstorming 阶段先定,带来源)
1. L4-Guidance stub 落在哪?(本仓库 = L3,L4 是别层;SIL 需要一个忠于设计的 L4 stub 节点——
   新建 src/sim_workbench/ 下节点 vs 复用既有?查 docs/Init From Zulip/ 的 L4 接口参考[只读]。)
2. M4 是否已发避让生命周期信号,还是需扩展?(behavior=COLREG_AVOID 已有;avoidance_active 的
   权威化需 M4 真正基于 M6 conflict 维护,而非桥。)
3. M5 的 AvoidancePlan(WP)对 L4 LOS+WOP 是否够?紧急路径要不要补 ReactiveOverrideCmd(ψ,u,ROT)?
4. release 几何条件(TCPA<0 && DCPA≥CPA_SAFE)归 M6(past-and-clear 已是 M6 latch 释放语义,
   见 [[l3-m6-onset-latch-no-generalize]])还是 M4?—— 与 M6 的 give-way DUTY latch 释放对齐,避免
   两套 past-clear 判据再打架。

## 流程(强制)
- superpowers:brainstorming(架构任务,先定 L4-stub 归属 + M4/M6 职责边界,每方案带设计来源)→
  /nlm-ask --notebook maritime_regulations(COLREGs 释放语义)+ 读架构报告/RFC → writing-plans →
  executing-plans。证据链 + 置信度纪律(全局 CLAUDE.md)。
- cert 相关(IEC 61508 SIL2:M4 仲裁 / MRC 路径;ADR-1/4)。改 M4/M6 规则语义前 /nlm-ask 核对。
- 代码定位:Python 桥直接读+grep;M4/M5 用 codegraph_explore。改场景=改 tools/sil/gen_colreg_tier12.py
  regen,别手改 YAML。

## 验证(权威判据)
- **批量 = restart-between-runs**(关键):plain scripts/run_6_scenarios.py 读 0/8 = 纯跨场景泄漏
  (trace 切片/暖态污染);真实数须每场景前 docker restart sil-nodes + 24s settle(见
  [[l3-m6-onset-latch-no-generalize]] 的 run_8_clean.py 模式)。当前清洁批 = **6/8**。
- **目标**:ot 达 behavior_toggles≤2 →(ot 转绿)≥7/8;**6 个已绿探针**
  (ho/ho-port/cs/cs-2/cs-edge/cr-so)**绝不可回归**(conf_tog=2/role=0/beh=2)。
  ot-boundary 的 CPA 46m<500 是 M4/M5 转向幅度另案,可同批做或先放。
- **route-return 必须保持 GREEN**:avoid→hold→return 弧(offset→0 回航迹线、loops=0、无 U-turn),
  这是 anti-circling teardown 迁移后最易回归的点 —— 专门验。
- Phase B scorer 回归锁:pytest tests/sim_workbench/scoring/test_stability_scorer.py(9 例不破)。
- 每撤一块桥内逻辑 → A4000 scp+restart → 清洁批 + route-return 弧实测,绿了再撤下一块。

## 产出
1. 影子层逐块迁移记录(每块:桥内行号 → 目标模块 → 等价行为证据 → A4000 验证不复发)。
2. 桥最终行数大幅下降,只剩翻译/trace/QoS;_check_geometry_release 等 release 路径删除,
   avoidance_active 由 M4 权威产出。
3. 清洁批 ≥7/8(ot 转绿)+ 6 探针零回归 + route-return GREEN + pytest 9/9。
4. 更新 DEBUG_STATE.md、记忆 [[l3-m6-onset-latch-no-generalize]](ot 项标 RESOLVED)+ 新记忆
   记录 L4-stub 落点 + 桥薄化、workspace_log。feat 分支提交 → 切 main ff-merge → 三端同步。
```

参考:ot 证据 + bridge 行号见记忆 l3-m6-onset-latch-no-generalize;bridge 脆弱/circling 史见
l3-route-return-plumbing-4-breaks + l3-circling-root-cause-m5-valid-forever;设计 = 架构报告 §893-906
+ §15.1 + RFC-001。clean batch 模式见上轮 /tmp/run_8_clean.py(A4000 ~/Code/mass-l3/run_8_clean.py)。
```
