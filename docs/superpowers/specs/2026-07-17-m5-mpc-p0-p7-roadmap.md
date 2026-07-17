# M5 MPC 避碰重构 P0–P7 执行路线图

> **产出**: 2026-07-17,把 2026-07-16 brainstorming 的 P0–P7 子项目分解草案(TXT,仅在会话内)落为权威文档,并同步 L3→L4 GNC 契约设计树(2026-07-17)的跨树修订。
> **工作树**: `.worktrees/m5-design-grounding`(分支 `codex/m5-design-grounding`)
> **模式**: 纯文档同步,无代码/接口改动

---

## 1. 权威性与文档分工

### 1.1 本路线图管什么 / 不管什么

| 文档 | 管什么(权威) | 不管什么 |
|---|---|---|
| **本路线图(P0–P7)** | **做什么 / 顺序 / 依赖 / 各 phase scope / GNC 决策的 phase 归属** | 裁决的技术依据(见下) |
| `specs/2026-07-16-m5-mpc-colav-solution-pack.md` | M5 MPC 避碰核心的**裁决依据**(11 DP + 11 VR + 14 TS + ALT + 风险) | 执行顺序 |
| `design-logs/2026-07-16-m5-mpc-colav-design-log.md` | M5 MPC 决策树全流程(Step1–6 + 跨树反馈修订) | phase 归属(本路线图补) |
| `specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md` | L3→L4 控制器无关契约(11 DP + 13 TS + 13 改动点) | M5 内部 phase(本路线图管) |

**冲突仲裁原则**: 本路线图的 phase scope 与 colav-design-log 的"跨树反馈修订"(VR-06b/VR-07b)一致。若 colav-design-log 的原 VR-06/VR-07(已被修订)与本路线图冲突,**以 VR-06b/VR-07b + 本路线图为准**。

### 1.2 来源

- **P0–P7 子项目分解草案**: 2026-07-16 brainstorming 会话(`#A4000设计-M5设计-MPC Step 03-06`),TXT 草案,此前从未落为文档。本路线图首次落地。
- **GNC 跨树同步**: 2026-07-17 L3→L4 契约设计树(`#A4000设计-M5 MPC对接GNC设计`),裁决 VR-06b/VR-07b/VR-02/VR-01/VR-05 影响 M5 侧 6 项。

---

## 2. ⚠ 两套 phase 编号须区分(防混淆)

本项目存在**两套独立的 phase 编号**,文档中须显式标注避免误读:

| 编号体系 | 来源 | 含义 | 本文用 |
|---|---|---|---|
| **M5 MPC 核心 P0–P7** | 本路线图(2026-07-16 草案) | M5 MPC 避碰重构的 8 个执行子项目(本路线图主题) | **`P0`/`P1`/.../`P7`** |
| **GNC 迁移路线 P1–P4** | GNC solution-pack VR-11 | L3→L4 控制器无关契约的**分阶段迁移路线**(P1 锁 PID-L4 → P2 契约收敛 → P3 切 MPC-L4 → P4 控制器无关验证) | **`GNC-P1`/`GNC-P2`/`GNC-P3`/`GNC-P4`** |

两套互不重叠: 本路线图 P0–P7 是 M5 内部重构; GNC-P1–P4 是 L3↔L4 接口的迁移路线。GNC 对接的 M5 侧改动(见 §5)归入本路线图的对应 phase + 一个"契约兑现项"集合。

---

## 3. 总览

### 3.1 依赖链

```
P0 (config fix) ──► P1 (acados 使能器)
                        │
          ┌─────────────┼─────────────┐
          ▼             ▼             ▼
         P2            P3            P4
    (Nomoto+状态+   (per-target    (Eriksen
     终端)            ξ slack)       时域)
          │             │             │
          └─────────────┴─────► P5 (M6 几何 + 反 chattering)
                                      │
                                      ▼
                                    P6 (BC-MPC 激活 + 四状态机)

          P7 (A+ 不确定性 + 意图) ◄── 后置/并行(依赖 P4 长时域)
```

**建议执行顺序**: P0 → P1 → P2 → P3 → P4 → P5 → P6 → P7
**可并行**: P2/P3/P4 在 P1 通过后可并行;P5 相对独立(约束编译层)。

### 3.2 状态总表

| Phase | Scope(一句话) | 依赖 | 风险 | 状态 | spec / plan 指针 |
|---|---|---|---|---|---|
| **P0** | manifest 几何修正 + Nomoto 字段语义澄清 | 无 | 低 | ✅ 完成(commits a30938e4d/26023d53f/d315bb3ff) | `specs/2026-07-16-m5-p0-manifest-nomoto-fix-design.md` + `plans/2026-07-16-m5-p0-manifest-nomoto-fix.md` |
| **P1a** | acados 可行性 spike(工具链验证) | P0 | 中 | ✅ 完成(HEAD a2db064b1,5/5 task 6/6 gate green) | `specs/2026-07-16-m5-p1a-acados-feasibility-spike-design.md` + `plans/2026-07-16-m5-p1a-acados-feasibility-spike.md` |
| **P1b-0** | acados staging spike(4 结构点) | P1a | 中 | ✅ 完成(commit 02ce2bec0,7/7 gate green) | `specs/2026-07-16-m5-p1b0-acados-staging-spike-design.md` + `plans/2026-07-16-m5-p1b0-acados-staging-spike.md` |
| **P1b-1a** | staging 验证 2 新 physics 点 + 6 点合并 | P1b-0 | 中-高 | ⏳ spec/plan ready(commits 4aff16587/f0f72bcaf) | `specs/2026-07-16-m5-p1b1-acados-full-migration-design.md` + `plans/2026-07-16-m5-p1b1-acados-full-migration.md` |
| **P1b-1b** | 生产 MidMpcAcadosSolver(M5_USE_ACADOS flag) | P1b-1a | 高 | ⏳ 实施中(2026-07-17) | `plans/2026-07-17-m5-p1b1b-acados-production-backend.md` |
| **P1b-1c** | Rule14 HO benchmark(行为等价) | P1b-1b | 高 | ⏳ 待 | (P1b1 spec §P1b-1c) |
| **P1b-2** | 增强(1200s horizon P4 / COLREGs 几何 P5 / zone) | P1b-1 | 中-高 | ⏳ 待(部分内容已前移,见 §5.4) | (P1b1 spec §明确排除) |
| **P2** | Nomoto 接入 NLP + x=[ψ,r,u] + 终端路线(**VR-07b 修订**) | P0 | 中-高 | ⏳ 待 | **无 spec(待开)** |
| **P3** | per-target ξ 行为验证 + ρ 校准 + 测试缺口(formulation 不改,ξ+L1/L2 已 P1b-1b 落地) | P1 | 低-中 | ⏳ spec/plan ready(2026-07-18) | `specs/2026-07-18-m5-p3-slack-validation-design.md` + `plans/2026-07-18-m5-p3-slack-validation.md` |
| **P4** | horizon 1200s + 废终端 C10/C11 + TailBuilder 拼接淘汰 + timer 60s + 承诺前缀 180s + 切 acados 默认 ON(含 carryover I-1~4) | P1+P2 | 高 | ⏳ spec/plan ready(2026-07-18) | `specs/2026-07-18-m5-p4-horizon-terminal-tailbuilder-design.md` + `plans/2026-07-18-m5-p4-horizon-terminal-tailbuilder.md` |
| **P5** | M6 几何约束 + 反 chattering 三层组合(**+Huber 联动**) | (相对独立) | 高 | ⏳ 待 | **无 spec(待开)** |
| **P6** | BC-MPC 激活 + 四状态交接机 + 回退链 | P2/P3/P4 | 中 | ⏳ 待(P6-a/P6-b 子项) | **无 spec(待开)** |
| **P7** | A+ 不确定性(OU) + 意图建模 | P4 | 中 | ⏳ 后置/并行 | **无 spec(待开)** |

---

## 4. 各 phase 详细 scope

### P0. manifest 几何修正 + Nomoto 字段语义澄清 [前置小修,低风险]

- **DP/TBD**: DP-02 前置 · TBD-5 部分
- **scope**:
  - 28m→45m / 95t→145t 等几何修正(FCB 实际 LOA/LBP/beam/draft/排水量)
  - `nomoto_K_inv_s` 字段语义澄清 → 重命名 `nomoto_K_s`(存 K 本身,非 1/K);T_s 15→6.0
  - 6 文件成对更新(yaml key + loader key + hpp 字段 + nomoto_fallback 成员 + fixture + header 默认值)
  - **VDM 4-DOF MMG 删除推到 P2**(有结构依赖 TrajectoryPropagator::propagate_own,P0 不碰)
- **验证**: 运行时 behavior-preserving(K 值不参与运算,经消费者链探索验证)
- **状态**: ✅ 完成

### P1. acados 求解器迁移 [使能器,高风险,须先做]

- **DP/TBD**: DP-05 · VR-05 · TBD-4
- **scope**:
  - acados 安装(CMake 全栈)+ OCP interface 重表述
  - code-gen + RTI + HPIPM 后端(μs-ms,结构利用 O(n))
  - Rule14 HO benchmark 对比 IPOPT(TBD-4 实测门)
  - IPOPT 保留(additive,不删),acados overlay
  - 子分解: P1a(spike)→ P1b-0(staging 4 点)→ P1b-1a(staging 2 physics 点)→ P1b-1b(生产 backend)→ P1b-1c(benchmark)→ P1b-2(增强)
- **使能**: P2/P3/P4(360s→1200s / ξ(M·N) / x=[ψ,r,u] 实时性全靠 acados)
- **头号回炉触发**: 若 acados 在 1200s horizon 下实测不达标(VR-06b 强化此必要性),回炉 DP-05 重评 SB-MPC+GPU
- **状态**: P1a/P1b-0 ✅;P1b-1 ⏳

### P2. Nomoto-扩展预测模型 + x=[ψ,r,u] 状态重构 + 终端路线 [核心,中-高风险]

> **⚠ VR-07b 修订(2026-07-17,GNC 跨树)**: 原 TXT 草案本 phase 含"人工参考轨迹(防过早归航)" + "Eriksen 终端路线 + 人工参考轨迹"。**已废弃人工参考轨迹**,改相对跟踪 t_b + Huber;**废除终端 C10/C11**。详见 §5.2。

- **DP/TBD**: DP-02 + DP-07 · VR-02/07(**被 VR-07b 修订**)· TBD-5
- **scope(修订后)**:
  - Nomoto Tṙ+r=Kδ 接入 NLP 预测(替代恒速)
  - 状态升级 x=[ψ,r,u] 含 ROT(弃差分,原生约束角加速度);注:生产 P1b-1b 已扩 5 维 `x=[px,py,ψ,r,u_surge]`
  - 控制量 u=[δ,n](舵角+转速)
  - T,K 初始值(缩律估算 T≈2-10s/K≈0.1-0.6/s)+ T',K' 运行时缩放
  - **Eriksen 终端路线:无终端集 + stage cost + 相对跟踪 t_b + Huber 损失 + 长 horizon(P4)**(VR-07b)
  - **废除终端 C10(同侧)/C11(横向)**(VR-07b,长 horizon 保证收敛)
  - T1 softplus+硬行 降为辅助
  - **VDM 4-DOF MMG 删除**(从 P0 推过来)
- **依赖**: P0
- **风险**: 中-高(formulation 层重大重构)

### P3. per-target ξ 行为验证 + ρ 校准 + 测试缺口填补 [核心,低-中风险]

> **⚠ 2026-07-18 scope 收敛**: 原 P3 scope 写"per-target per-step ξ + 混合 L1/L2 slack"。**核心实现已由 P1b-1b 落地**(kAcadosNsh=16 per-target × N stages = ξ∈R^{16·N};gen Zl=1e2 quad + zl=1e3 linear L1)。P3 收敛为**验证 + 校准 + 测试缺口填补**(formulation 不改):① ρ exact-penalty SIL 实测(zl=1e3 是否满足 Kerrigan ρ>‖λ*‖∞);② ξ 独立性(masking 消除)+ 精确性(feasible ξ≈0)单测;③ ξ 可观测性(per-target breakdown publish,认证可见)。spec/plan: `specs/2026-07-18-m5-p3-slack-validation-design.md` + `plans/2026-07-18-m5-p3-slack-validation.md`。

- **DP/TBD**: DP-03 + TBD-6 · VR-03 + VR-TBD6
- **scope(收敛后)**:
  - **不改 formulation**(ξ∈R^{M·N} + 混合 L1/L2 已 P1b-1b 落地)
  - ρ(zl=1e3)exact-penalty SIL 实测(imazu-*-ms 多船场景)+ 条件性校准(固定/调大/同伦,据 SIL 结果)
  - ξ 独立性单测(多船一目标松弛不拖累其他,masking 消除 SC-02)
  - ξ 精确性单测(feasible ξ≈0 / infeasible ξ>0)
  - ξ 可观测性(per-target ξ breakdown publish,M8/ASDR/CCS 认证可见)
- **依赖**: P1(acados 原生支持混合 L1/L2,已落地)
- **状态**: spec/plan ready(2026-07-18);P1b-1a T7 staging 已证 per-target ξ 可扩
- **风险**: 低-中(只测试 + 可观测性 + 可能调 zl 一个数值;SIL 实测驱动)

### P4. Eriksen 分层时域 + RFC-001 推翻 [参数,高风险]

> **⚠ VR-06b 修订(2026-07-17,GNC 跨树)**: 原 TXT 草案本 phase 写 "horizon=360s/dt=10s(Np36)"。**已延长到 1200s**。详见 §5.3。

- **DP/TBD**: DP-06 · VR-06(**被 VR-06b 修订**)
- **scope(修订后)**:
  - **Mid: horizon=1200s**(VR-06b);dt 可调 10/15/20s(Np=120/80/60 benchmark 定);replan=60s
  - BC: 短 horizon / replan=5s
  - m5_params.yaml / solve_timer / resolve_horizon_config / kNDefault 全部更新
  - RFC-001(90s 锁定)正式推翻记录(2026-07-16 Step2 已授权)
  - **dt 三档 benchmark**:10s/Np120 vs 15s/Np80 vs 20s/Np60,P4 实施时测实时性取达标最大分辨率
- **依赖**: P1(acados 实时性,1200s 是 IPOPT O(n³) 不可承受)+ P2(Nomoto 速度缩放)
- **风险**: 高(1200s 实时性是头号回炉触发)
- **理由**(VR-06b): 用户 SIL 仿真观测完整避碰生命周期(避让→保持→返航)最长 900s;360s<900s 致 myopia(premature return/chattering/稳定性丢失);Johansen SB-MPC 用 600-1200s 实证;45m FCB 18kn 巡航下 20 分钟覆盖一般避碰+返航

### P5. M6 几何约束 + 反 chattering 三层组合 [约束层,高风险]

> **⚠ GNC 跨树联动(2026-07-17)**: 位置代价由纯二次改 Huber(与 P2 VR-07b 联动)。详见 §5.4。

- **DP/TBD**: DP-04 + TBD-7 · VR-04 + VR-TBD7
- **scope**:
  - 移除硬编码 Rule14/15 偏移
  - M6 几何 hard Rule13/14/15(preferred_direction / min_alteration)
  - Rule8/17 soft 代价
  - **反 chatter 三层组合**:
    - warm-start shift-init(首要,保持同伦类)
    - 转移代价混合范数(L2 航向控制 + L1 速度)
    - 符号翻转检测(Tengesdal K_sgn·exp)
  - **位置代价改 Huber 损失**(VR-07b 联动,近原点二次/远处线性)
  - 配合 M6 RuleLatch + FSM hysteresis + neutral safe state
  - C5/C9/C12 数据源 TBD(补齐前不作硬约束)
- **依赖**: 相对独立(约束编译层);Huber 改动依赖 P2
- **风险**: 高(几何推导)

### P6. BC-MPC 激活 + 四状态交接机 + 回退链 [集成,中风险]

- **DP/TBD**: DP-01 + DP-01a/b + DP-08 · VR-01/01a/01b/08
- **scope**:
  - BC-MPC 激活(清 launch/namespace/bridge 集成债)
  - Eriksen 标准职责(执行+兜底,验证归 M7)
  - 四状态机(MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE)
  - stale 45s/15°/20% 门控 + 交还 hysteresis 连续 2 周期
  - 废 keep-last 空 plan;geo 降 BC 后最终层
  - FINAL_DEGRADE 报 M7(safety_concern_event)
- **子项**:
  - **P6-a**: Launch Activation + Rebaseline(加 `bc_mpc_node` 到 `m5_mid_mpc.launch.py`,end-to-end 验证)
  - **P6-b**: 四状态机(当前是单 boolean 阈值 `consecutive ≥ 3`,无 hysteresis/M7 态,需从零建)
- **依赖**: P2/P3/P4(BC 跟踪 Mid 输出)
- **风险**: 中
- **关联 GNC**: ReactiveOverrideCmd(BC 接管时)→L4 接入路径在 GNC 设计树 DP-10/VR-08 已定义契约,L4 侧接线归 GNC-P1/P2(本路线图不管 L4 侧)

### P7(后续/并行). A+ 不确定性 + 意图建模 [增强,中风险]

- **DP/TBD**: DP-09 · VR-09
- **scope**:
  - OU 过程有界化横向不确定性
  - intent_confidence 标量缩放 CPA 代价
  - BC Nominal(短时域)
- **依赖**: P4(长时域才需 OU)
- **风险**: 中
- **回炉触发**: 若 A+ 在 SIL 多船极端场景验证中不足(意图感知/不确定性有界不够)→ 回炉 P1/DP-05 重评 SB-MPC+GPU 完整 C

---

## 5. GNC 对接跨树同步(2026-07-17)

> **来源**: `design-logs/2026-07-17-l3-l4-gnc-contract-design-log.md`(L3→L4 GNC 契约设计,Step2 模块① + Step4 Spec 同步指引)
> **触发**: L3→L4 契约设计 grilling DP-02 时,用户质疑"360s 时域下 TailBuilder 几何续貂是否冗余",触发 Eriksen 原文 + NLM 三方高置信度查证,裁决 VR-06b/VR-07b 等。
> **处理纪律**: append-only。colav-design-log 的原 VR-06/VR-07 不抹除,新增 VR-06b/VR-07b 修订行。本路线图按修订后的 phase scope 写。

### 5.1 GNC 13 改动点 → M5 phase 归属映射

GNC design-log Step4 列出 13 个 Spec 同步改动点(M5 侧 6 + 接口侧 4 + L4 侧 3)。**M5 侧 6 项** 归属本路线图如下:

| GNC 改动点 | VR | 内容 | 归属本路线图 phase | 原状态 |
|---|---|---|---|---|
| 1 | VR-02/VR-06b | NLP horizon 360s→**1200s** | **P4** | ❌ P4 仍写 360s → 本路线图已修订(§4 P4) |
| 2 | VR-03/VR-07b | 废弃人工参考→**相对跟踪 t_b + Huber 损失** | **P2**(终端)+ **P5**(位置代价) | ❌ P2 仍写"人工参考" → 已修订(§4 P2) |
| 3 | VR-03/VR-07b | **废除终端 C10/C11** | **P2** | ❌ P2 未提废除 → 已修订(§4 P2) |
| 4 | VR-02 | **淘汰 TailBuilder** | **P2 输出流程** | ❌ 未含 → 见 §5.2 |
| 5 | VR-05 | preflight 4 调用点改读 `effective_gnc_odd_()` 删硬编码 | **契约兑现项(新)** | ❌ 未归 phase → 见 §5.5 |
| 6 | VR-01 | 新 trajectory 输出(NLP 解直接 TimedTrajectory) | **契约兑现项(新)** | ❌ 未归 phase → 见 §5.5 |

**接口侧 4 项 + L4 侧 3 项** 不属本路线图(M5 内部),归 GNC-P1/GNC-P2/GNC-P3 迁移路线,见 GNC solution-pack 组件 5 流程分解树⑥。

### 5.2 P2 修订详情(终端 + 输出流程)

**原 TXT 草案 P2** 写: "Eriksen 终端路线 + 人工参考轨迹(防过早归航);T1 softplus+硬行 降为辅助"。

**VR-07b 修订后**:
- ✗ **废弃"人工参考轨迹"**: 因果倒置误读。Eriksen reference 始终是 nominal,用相对跟踪 t_b(每周期投影回 nominal route 找最近点),从不切换成"避让参考"。人工参考是"防过早归航"误读产物。
- ✓ **位置代价纯二次 → Huber 损失**(Eq20-21,近原点二次/远处线性,防被障碍推开时指数回拉)
- ✗ **废除终端 C10(同侧)/C11(横向)**: VR-06b 长 horizon 1200s 下靠 horizon 保证收敛,不需终端集
- ✓ **改相对轨迹跟踪 t_b**(每周期投影回 nominal route 找最近点)—— 新增实现项
- ✓ T1 softplus+硬行 降为辅助(保留)

**P2 输出流程修订(VR-02 淘汰 TailBuilder)**:
- 原 M5 输出流程: NLP 解[ψ,r,u,x,y] → **TailBuilder 尾段拼接**(几何 hold+rejoin) → preflight → publish
- **修订后**: NLP 解[ψ,r,u,x,y](1200s horizon,相对跟踪 t_b+Huber,无终端集)→ 直接 trajectory(单一真相,含避让+保持+返航完整生命周期)→ preflight → publish。**无 TailBuilder,无尾段拼接,无人工参考**。
- 老 TailBuilder 是 VR-07"人工参考轨迹防过早归航"误读的补丁,根因消除后补丁亦消除。

### 5.3 P4 修订详情(时域)

**原 TXT 草案 P4** 写: "Mid: horizon=360s/dt=10s(Np36)/replan=60s"。

**VR-06b 修订后**:
- **Mid: horizon=1200s**(VR-06b);dt 可调 10/15/20s(Np=120/80/60 benchmark 定);replan=60s 不变;BC 5s 不变
- **+ 承诺前缀 180s 语义**(GNC VR-06): M5 replan 60s ↔ L4 跟踪衔接。承诺前缀=180s(L4 必跟踪、M5 保证不推翻),预测尾段=1020s(1200−180,参考用)。material-change(role/direction/risk/GNC reject/偏航/M7 VETO/紧急升级)才更新 plan version。
- **dt 三档 benchmark**: 1200s 下 dt=10s(Np120)/15s(Np80)/20s(Np60),P4 实施时测实时性,取达标最大分辨率。COLREGs ample-time 分钟级,dt=15-20s 足够。
- **acados 价值兑现**: 1200s horizon 是 IPOPT O(n³) 不可承受的,正是 acados RTI O(n) 的用武之地。VR-06b 强化 P1(acados 迁移)的必要性。

> **注**: P1b-1b 生产 backend 当前 kNDefault=18(对应 90s/dt=5)是 P1b 范围正确值(等价迁移),**不在 P1b 改 horizon**。horizon 延长到 1200s 是 P4 范围,待 P1b 全量迁移通过后做。

### 5.4 P5 修订详情(约束 + 位置代价)

- 反 chattering 三层组合(warm-start shift-init 首要 + 转移代价混合范数 + 符号翻转检测)**保持不变**
- **位置代价由纯二次改 Huber 损失**(VR-07b 联动,与 P2 终端修订同源)
- M6 几何 hard Rule13/14/15 + Rule8/17 soft 保持

### 5.5 契约兑现项(新增,跨 GNC-P1/GNC-P2)

GNC VR-01(TimedTrajectory 输出)与 VR-05(preflight 删硬编码)是 L3→L4 控制器无关契约的 M5 侧兑现,不属 M5 MPC 核心 P0–P7 任何一个,单列为"契约兑现项":

| 契约兑现项 | VR | 内容 | 依赖 | 时机 |
|---|---|---|---|---|
| **TimedTrajectory 输出** | VR-01 | 新建 `l3_msgs/TimedTrajectory` 消息(含 safety_intent/execution_policy/segment_source/plan_id+version);M5 NLP 解直接产 trajectory(含 ψ/u/x/y/yaw_rate/curvature/acceleration per step)替代 avoidance_plan 航点为主 | P2(NLP 解 trajectory)+ P4(horizon 1200s 覆盖完整生命周期) | GNC-P1(PID-L4 adapter 就绪时) |
| **preflight 删硬编码** | VR-05 | preflight 4 调用点(`mid_mpc_node.cpp:1942/2052`、`mid_mpc_waypoint_generator.cpp:144`、`gnc_avoidance_preflight.hpp:27-29`)改传 `effective_gnc_odd_()` 删硬编码默认(0.5/3.5/45/0.20) | 无强依赖(机制已就绪:GncExecutionOdd QoS 已 latched,M5 侧订阅已匹配 TRANSIENT_LOCAL+RELIABLE) | GNC-P1 |
| **TailBuilder 淘汰** | VR-02 | 删除 TailBuilder 尾段拼接(见 §5.2) | P2(相对跟踪 t_b+Huber 使 NLP 内部端到端)+ P4(horizon 1200s 覆盖返航) | 与 P2 同期 |

**控制器无关兑现路径**(GNC VR-11): M5 在 GNC-P1 一次改到位(trajectory 单一输出 + preflight 读 capability + 控制器无关),GNC-P2/GNC-P3 零 M5 改动 = DP-00 兑现。

---

## 6. 开放项与回炉触发

### 6.1 开放项(无阻塞,实施阶段验证)

| 开放项 | 归属 | 说明 |
|---|---|---|
| dt 三档 benchmark(10/15/20s) | P4 | 1200s horizon 下取达标最大分辨率;COLREGs ample-time 分钟级,dt=15-20s 足够 |
| SIL 校准承诺前缀 180s | GNC VR-06 | 设计值 180s(NLM 推荐 120-180s 取上端),延后一周用户跑 900s 场景验证双 L4 跟踪稳定 |
| 相对跟踪 t_b 投影算法 | P2 | 本船→nominal route 最近点投影,新增实现项 |
| acados 1200s 实时性 | P1+P4 | **头号回炉触发**:若 acados RTI 在 1200s/Np60-120 下实测不达标,回炉 DP-05 重评 SB-MPC+GPU |
| bridge 跨 domain TRANSIENT_LOCAL 端到端 | GNC-P1 | M5 侧订阅已匹配 TRANSIENT_LOCAL+RELIABLE,bridge 侧待确认 |

### 6.2 回炉触发条件(保留)

- **DP-05/P1 acados 若实测不达标**(1200s horizon ample-time SIL 证伪)→ 回炉 DP-05 重评 SB-MPC+GPU(原 Step5 A/B 对比备查,colav-design-log Step5)
- **P7/DP-09 A+ 若 SIL 多船极端场景不足**(意图感知/不确定性有界不够)→ 回炉 DP-05 转 SB-MPC+GPU 完整 C
- **GNC 承诺前缀 180s SIL 校准不通过** → 回炉 GNC DP-03(VR-06),重评承诺前缀长度

---

## 7. 文档交叉引用

- **M5 MPC 裁决依据**: `specs/2026-07-16-m5-mpc-colav-solution-pack.md`(11 DP + 11 VR + 14 TS + ALT-01..12 + 风险)
- **M5 MPC 决策树全流程**: `design-logs/2026-07-16-m5-mpc-colav-design-log.md`(Step1–6 + 跨树反馈修订 VR-06b/VR-07b)
- **L3→L4 契约(13 改动点权威)**: `specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md` + `design-logs/2026-07-17-l3-l4-gnc-contract-design-log.md`
- **M5 架构级职责(子模块边界,TailBuilder/preflight/BC-MPC 定位)**: `design-logs/2026-07-16-m5-architecture-design-log.md`
- **已产出 spec/plan**: 见 §3.2 状态总表指针列
