# 方案包: M5 MPC 避碰方案设计审查(Mid-MPC + BC-MPC)

> **产出**: design-grounding Step6,2026-07-16
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **范围**: M5 MPC 避碰核心(Mid-MPC NLP + BC-MPC 分支)。TailBuilder/AvoidancePlan 组装/committed-route 状态机/Normal-DEGRADED 振荡等 M5 其他子模块开新决策树另行处理。
> **模式**: 重构(M5 已有完整实现,避碰航线输出反复出问题)
> **权威性声明**: 本方案包基于 **Step2 逐决策点用户确认**(2026-07-16)的裁决 + Step3 调研(R22-R24)+ Step4/5 综合。此前 Step2 批量草稿已被修订记录作废;本版为权威。

---

## 方案包契约(brainstorming 权限边界)

- ✓ **可做**: 工程细节设计(架构/组件/数据流/错误处理/测试),已裁决方案(VR-01~09 + VR-01a/b)内优化拔高
- ✗ **不可做**: 推翻已裁决核心方案,除非发现**新矛盾证据**(回炉 design-grounding)
- ✗ **不可做**: 重提已弃用方案(ALT-01~12)
- ✗ **不可做**: 擅自修改技术规约(TS-01~14,单位/坐标系/符号),需改则回 design-grounding 重新裁决
- ⚠ **须推动**: TBD-5(Nomoto 参数辨识/字段语义澄清)须在实现早期澄清;C5/C9/C12 数据源(TBD-1/2/3)补齐前不得作硬约束引入

---

## 组件 1: 术语表

| 术语 | 定义 | 本方案含义 | 关联DP |
|---|---|---|---|
| Mid-MPC | 中层 NLP MPC,Eriksen ample-time 规划 | acados 求解 [ψ;r;u;ξ_{M·N}] → AvoidancePlan;horizon360s/dt10s/replan60s | DP-01,05,06 |
| BC-MPC | 短期分支枚举,replan5s | 3 档候选,近距跟踪+接管兜底;不做验证(归 M7) | DP-01,01a,08 |
| per-target per-step slack ξ | 每目标每步独立松弛 ξ_{m,n}≥0 | d²-cpa_hard²+ξ_{m,n}≥0;**混合 L1/L2 惩罚 ρ·ξ+½w·ξ²** | DP-03 |
| masking/free-riding | 单 slack 共享失效 | ξ 要消除的 | DP-03 |
| exact-penalty 精确性 | Kerrigan ρ>‖λ*‖∞,仅 L1 成立 | slack 线性项保 feasible 时 ξ=0 | DP-03 |
| ample time | Rule8 尽早明显 | horizon >> 转向/变速时间;360s | DP-06 |
| SB-MPC | 离散行为集枚举 | 弃用(ALT-12),标待选演进 | DP-05 |
| receding horizon | 执行首 5-10s 重算 | A+ 增强项 | DP-05 |
| warm-start shift-init | 上周期解 shift 作初值 | **首要反 chattering 机制**(保持同伦类) | DP-04,05 |
| 转移代价(混合范数) | 跨周期连续性 | tran_χ(L2 航向控制)+tran_U(L1 速度);w_trans 0.2-5 vs 碰撞40 | DP-04 |
| Nomoto 模型 | Tṙ+r=Kδ 1-DOF 偏航 | NLP/BC 预测;存 T',K' 缩放 T=(L/U)T',K=(U/L)K' | DP-02 |
| OU+intent_confidence | 长时域有界化不确定性+意图标量 | DP-09 A+:缩放 CPA 代价 | DP-09 |
| 四状态交接机 | MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE | 连续级联+交还hysteresis连续2周期+FINAL_DEGRADE报M7 | DP-01b,08 |
| 人工参考轨迹 | 避让期用避让参考做 J_dist 基准 | 防 optimizer 为最小化 XTE 过早归航 | DP-07 |

(完整术语表含边界/不是什么,见决策树日志 Step6)

---

## 组件 2: 技术规约表(六类)

| 类别 | 规约(权威) | 来源 | 与现状差异 |
|---|---|---|---|
| 坐标系 | WGS84/NED(psi=0北顺时针);body(x艏前/y左舷) | TS-01~03 [R6] | 一致 |
| 单位 | 内部 rad/m/s/m;消息 deg;YAML kn | TS-04 [R6] | 一致 |
| 符号 | ψ右舷正;ROT右转正;l右舷正 | TS-03 [R6] | 一致 |
| **决策变量** | **x=[ψ;r;u;ξ_{M·N}]** 含 ROT + per-target per-step slack;控制 u=[δ,n] | TS-06 [VR-03/05/07] | **重大重构:[psi(2N)+σ(1)]→[ψ,r,u+ξ(M·N)]** |
| **时域** | **Mid: horizon=360s,dt=10s(Np36),replan=60s;BC: 短horizon,replan=5s** | TS-05/11 [VR-06] | **90s/dt5s/replan1s→360s/dt10s/replan60s(RFC-001推翻)** |
| **预测模型** | **Nomoto-扩展(Mid+BC同一套),存 T',K' 运行时缩放**;VDM 4-DOF MMG 删除;manifest 几何修正(28→45m) | TS-12 [VR-02][R22] | **恒速→Nomoto;TBD-5 参数辨识/字段语义** |
| **约束层级** | 物理硬;CPA ξ 混合 L1/L2 软;Rule13/14/15 M6几何hard;Rule8/17 soft;反chatter:warm-start首要+混合范数转移 | TS-13 [VR-03/04] | **硬编码偏移→M6几何;纯L2→混合L1/L2;转移代价分层** |
| **slack 惩罚** | **混合 L1/L2: ρ·ξ+½w·ξ²,ρ>‖λ*‖∞**(acados zl/Zl 原生);Eriksen 同伦 K_ξ=[0.1,1,10,100,∞] | TS-07 [R23][TBD-6] | **纯 L2(w=1e8)→混合 L1/L2** |
| **回退交接** | **四状态机**+BC连续级联+stale45s/15°/20%门控+交还hysteresis连续2周期+废空plan+geo=BC后最终层+报M7 | TS-14 [VR-01b/08] | **失败计数→四状态机+连续级联** |
| **求解器** | **NLP 建模维持,求解器 IPOPT→acados**(RTI+HPIPM,μs-ms) | TS-06 [VR-05][R18] | **IPOPT→acados;TBD-4 实测** |
| 不确定性 | Mid 用 A+(OU+intent_confidence);BC Nominal;SB-MPC+GPU 完整 C 标待选 | TS-13 [VR-09][R21] | Nominal→A+ |
| 终端约束 | Eriksen 路线(无终端集+stage cost+转移代价+长horizon+per-step可行性)+人工参考;T1 降辅助 | TS-06 [VR-07][R20] | 维持T1→Eriksen路线+人工参考 |
| 接口 | psi_cmd/u_cmd→L4;AvoidancePlan→L4/M7/M8;ReactiveOverrideCmd(BC接管)→L4;waypoint CMM 字段待补 | TS-09 [R6] | Override 路径+CMM 字段待补 |

(完整 14 条 TS-01~14 含来源/单位/关联DP,见决策树日志注册表 0.8)

---

## 组件 3: 决策卡片集(Step2 逐点裁决 + Step5 TBD 精化)

### 全部裁决(11 决策点 + 2 子决策点,Step2 用户逐点确认)

| DP | 裁决(VR) | 一句话理由 | 证据链 |
|---|---|---|---|
| DP-01 | **双层连续级联 + 激活 BC-MPC** | [R5]Eriksen+[R16]单层证伪 | [R5][R9][R16] |
| DP-01a | **Eriksen 标准职责**(Mid规划+BC执行兜底;验证归M7;同步=并行非交叉检验) | [R5]+用户;避免 doer-checker 越界 | [R5] |
| DP-01b | **四状态交接机**+交还hysteresis连续2周期+FINAL_DEGRADE报M7 | [R5]连续级联+neutral+hysteresis | [R5] |
| DP-02 | **Nomoto-扩展(Mid+BC同一套)**;manifest 几何修正;VDM 删除;TBD-5 参数辨识 | [R1]恒速失真;3-DOF 缺数据阻塞 | [R1][R6][R22] |
| DP-03 | **per-target per-step slack ξ∈R^{M·N}**;废单标量;**TBD-6 混合 L1/L2 惩罚** | [R2]masking+用户多船核心;[R23]纯L2不精确 | [R2][R8][R17][R23] |
| DP-04 | **Eriksen 混合**(M6几何hard Rule13/14/15+Rule8/17soft+转移代价);移除硬编码;**TBD-7 warm-start首要+混合范数** | [R3]硬编码unsound+[R16]失效;[R24]warm-start首要 | [R3][R7][R16][R24] |
| DP-05 | **NLP 建模维持,IPOPT→acados**;SB-MPC 标待选演进 | [R18]acados O(n) vs IPOPT O(n³);用户选项B;TBD-4 实测 | [R15][R18] |
| DP-06 | **Eriksen 实测:Mid 360s/dt10s/replan60s+BC 5s**;RFC-001 推翻 | [R19]概念澄清+业界实测 | [R4][R10][R19] |
| DP-07 | **x=[ψ,r,u]含ROT**+Eriksen终端路线+人工参考轨迹;T1 降辅助 | [R20]Nomoto推荐含r;Eriksen无终端集 | [R14][R20] |
| DP-08 | **BC连续级联+四状态机+stale门控+废空plan**;geo降BC后最终层 | [R5][R16][R17c]命中冻结/振荡 | [R5][R9][R16][R17c] |
| DP-09 | **Mid 用 A+(OU+intent_confidence);BC Nominal**;SB-MPC+GPU C 标待选 | [R21]选项C NLP内不实用;A+可行 | [R4][R21] |

### Step5 TBD 精化裁决(低风险采纳,非方案推翻)

| TBD | 裁决 | 理由 |
|---|---|---|
| TBD-6(DP-03 slack 形式) | **纯 L2 → 混合 L1/L2**(ρ·ξ+½w·ξ²,acados zl/Zl 原生) | [R23]精确性条件仅 L1 成立 |
| TBD-7(DP-04 反 chattering) | **纯 L2-on-ψ → warm-start shift-init(首要)+混合范数转移+可选符号翻转检测** | [R24]warm-start 是首要;纯L2孤立最弱 |
| TBD-5(DP-02 Nomoto 参数) | 标 HAZID/试航待办;① manifest 字段语义澄清 ② 存 T',K' 缩放 ③ 初始数量级估算+HAZID校准 | [R22]参数缺口,非方案选择 |

### 头号裁决回炉触发条件(保留)
- **DP-05 NLP→acados 若实测不达标**(360s/36步 RTI 实时性 SIL 证伪)→ 回炉 DP-05 重评 SB-MPC+GPU(原 Step5 A/B 对比备查)
- **DP-09 A+ 若 SIL 多船极端场景不足**(意图感知/不确定性有界不够)→ 回炉 DP-05 转 SB-MPC+GPU

---

## 组件 4: 证据矩阵(完整溯源 R1-R24)

| ID | 来源 | 检索/权威/适用 | 关键结论 | 归属 |
|---|---|---|---|---|
| [R1] | NLM colav_algorithms | 高/高/高 | 恒速90s失真;3-DOF有效中间档;MMG太慢 | DP-02 |
| [R2] | NLM colav_algorithms | 高/高/高 | 单slack masking;Eriksen per-target ξ∈R^{MNp} | DP-03 |
| [R3] | NLM colav_algorithms | 高/高/高 | 硬编码unsound;Rule13/14/15可硬,8/17需软 | DP-04 |
| [R4] | NLM colav_algorithms | 高/高/高 | 90s偏短;360-600s+SB-MPC+OU | DP-06,09 |
| [R5] | NLM colav_algorithms | 高/高/高 | Eriksen三层混合;连续级联;冻结/shattering失效 | DP-01,05,08 |
| [R6-R15] | PROJECT_FACT | —/本项目/— | 现状代码(NLP/约束/求解器/时域/回退链) | 各DP |
| [R16] | PROJECT_FACT(SIL 2026-06-22) | 高/本项目/高 | 5/12PASS;rule15-ot-boundary SOLVER=1/FALLBACK=2121 | DP-04,08 |
| [R17a/b/c] | DOCUMENTED_INTENT(v2.3)+PROJECT_FACT | —/本项目/— | per-target slack设计;实现单标量;纯硬坍塌 | DP-03,08 |
| [R18] | NLM colav_algorithms | 高/高/高 | slack M·N致IPOPT O(n³);acados+HPIPM最可行 | DP-05 |
| [R19] | NLM colav_algorithms | 高/高/高 | ample-time无刚性公式;Eriksen Mid360s/replan60s+BC5s | DP-06 |
| [R20] | NLM colav_algorithms | 高/高/高 | 终端三方法;Eriksen无终端集;Nomoto推荐x=[ψ,r,u] | DP-07 |
| [R21] | NLM colav_algorithms | 高/高/高 | 选项C NLP内不实用;A+(OU+intent)可行替代 | DP-09 |
| [R22] | NLM ship_maneuvering+colav+Web | 中/高/低-中 | **BL-11 Nomoto辨识**:T,K物理含义;zigzag辨识;本船型数值缺口;字段语义歧义 | DP-02 |
| [R23] | NLM colav+safety+Web | 高/高/中 | **BL-12 w_slack**:Kerrigan精确性ρ>‖λ*‖∞仅L1;纯L2不精确;acados混合L1/L2原生 | DP-03 |
| [R24] | NLM colav+Web | 高/高/中 | **BL-13 转移代价**:Eriksen混合范数;warm-start首要;符号翻转检测最针对flip | DP-04 |

(完整 R1-R24 含参考文献,见决策树日志 `## 参考文献`)

---

## 组件 5: 技术分解完整树(TD-01 及 11 子模块裁决)

```
TD-01 M5 MPC 避碰核心
├ DP-01 架构        ✓裁决 VR-01 双层连续级联+激活BC
│  ├ DP-01a 职责    ✓裁决 VR-01a Eriksen标准(执行+兜底,验证归M7)
│  └ DP-01b 交接    ✓裁决 VR-01b 四状态机+hysteresis+报M7
├ DP-02 预测模型    ✓裁决 VR-02 Nomoto-扩展(Mid+BC同一套)+TBD-5参数辨识
├ DP-03 约束slack   ✓裁决 VR-03 per-target per-step ξ(M·N)+TBD-6混合L1/L2
├ DP-04 COLREGs     ✓裁决 VR-04 Eriksen混合+TBD-7 warm-start首要+混合范数
├ DP-05 求解器      ✓裁决 VR-05 NLP维持,IPOPT→acados+TBD-4实测(头号裁决)
├ DP-06 时域        ✓裁决 VR-06 Eriksen实测Mid360s/dt10s/replan60s+BC5s(RFC-001推翻)
├ DP-07 终端        ✓裁决 VR-07 x=[ψ,r,u]含ROT+Eriksen终端路线+人工参考
├ DP-08 回退        ✓裁决 VR-08 BC连续级联+四状态机+stale门控+废空plan
└ DP-09 不确定性    ✓裁决 VR-09 Mid用A+(OU+intent)/BC Nominal
DECOMPOSITION 闭环: ✓ 全部11决策点已裁决(Step2逐点确认),无INCOMPLETE
TBD-4/5/6/7: 实现阶段验证/内部形式精化,非结构盲区,不阻塞
```

---

## 组件 6: 弃用方案及理由(ALT-01~12)

| ALT | 方案 | 弃用理由 | 证据 |
|---|---|---|---|
| ALT-01 | 单层NLP+纯几何回退 | 几何fallback粗糙 | [R16]SOLVER=1 |
| ALT-02 | 保持恒速直线 | 大转向+90s失真 | [R1][BL-02] |
| ALT-03 | 全MMG预测 | 太慢不适实时NLP | [R1] |
| ALT-04 | Nomoto r₀=0 | 退化为恒向无物理 | [R12] |
| ALT-05 | 单标量σ slack | masking+多船核心 | [R2] |
| ALT-06 | 纯硬约束无slack | INFEAS 371× | [R17c] |
| ALT-07 | 硬编码5°/10°Rule | fundamentally unsound | [R3][R16] |
| ALT-08 | 全软代价COLREGs | 可被压过早归航 | [R3] |
| ALT-09 | 90s单层时域 | 偏短+两头不靠 | [R4][BL-02] |
| ALT-10 | 失败计数为主交接 | 冻结风险 | [R5][R16] |
| ALT-11 | keep-last空plan | GNC丢弃 | [R17c] |
| ALT-12 | SB-MPC整体转型 | 多船组合爆炸+弃投资+Eriksen中层即NLP | [R2][R3][R4][R5]+用户Step2选项B |

(原 Step5 NLP(A)vs SB-MPC(B)完整对比备查,见决策树日志 Step5"原 Step5 备查")

---

## 组件 7: 需求场景 + 验收边界

| SC | 场景 | 约束 | 验收(方案须通过) | 驱动DP |
|---|---|---|---|---|
| SC-01 | COLREGs避让(右舵≥30°+变速) | Rule8 ample time | SIL rule14-ho GREEN;CPA≥floor;无chattering;Nomoto预测轨迹准确 | DP-02,06 |
| SC-02 | 多船同时避碰 | ≥2目标CPA同激活 | ξ_{m,n}独立波单测;多船SIL CPA floor不崩塌;slack feasible时ξ=0 | DP-03 |
| SC-03 | 斜遇/非常规几何 | 硬编码失效区 | SIL rule15-cs/edge GREEN;no-cross-ahead通过 | DP-04 |
| SC-04 | NLP连续失败/目标突变 | 冻结/振荡区 | rule15-ot-boundary无SOLVER=1/FALLBACK=2121;stale≤45s出锁;四状态机正确转换 | DP-01b,08 |
| SC-05 | 远距ample-time预警 | 长前瞻需求 | ample-time门GREEN(360s分层);长时域CPA不爆炸(OU) | DP-06,09 |
| SC-06 | acados实时性 | 360s/36步 RTI | TBD-4 benchmark:acados RTI ≤求解预算;Rule14 HO对比IPOPT | DP-05 |

---

## 组件 8: 已知冲突与未闭环盲区

**已知冲突(Step5 已裁决)**:
- 冲突 1(头号,已裁决): DP-05 NLP vs SB-MPC → Step2 选项B(NLP→acados)。**回炉触发**: 若 acados 落地后 360s horizon ample-time SIL 证伪,带新矛盾证据回炉 DP-05。
- 冲突 2(已闭环): DP-06 RFC-001 90s 锁定 → Step2 用户授权推翻,Eriksen 360s 实测参数。
- 冲突 3(Step3 TBD-6,已裁决): DP-03 纯 L2 slack → Kerrigan 精确性不满足 → Step5 升级混合 L1/L2。
- 冲突 4(Step3 TBD-7,已裁决): DP-04 纯 L2-on-ψ 转移 → 弱于 Eriksen 混合范数 → Step5 升级 warm-start 首要+混合范数。

**未闭环盲区(无)**:
- BL-01~05: NLM 闭环([R1-R5])
- BL-06/07/09: 用户闭环(Step2)
- BL-08/10: SIL 闭环([R16])
- BL-11/12/13: Step3 闭环([R22-R24])

**残余 TBD(实现阶段,非盲区)**:
- **TBD-1**: C5 CPA cpa_hard 应来自 M1 ODD(非硬编 1852);M2 须实现 CPA 推算(当前置零)
- **TBD-2**: C9 ample time 不引入无源硬约束,仅转移代价软实现
- **TBD-3**: C12 Zone 死代码,待 ENC 接入后启用
- **TBD-4**: acados 安装+重表述+code-gen+Rule14 HO benchmark(若不达标回炉 DP-05)
- **TBD-5**: Nomoto T,K 辨识(HAZID/试航);manifest 字段 nomoto_K_inv_s 语义澄清;存 T',K' 缩放
- **TBD-6**: slack 混合 L1/L2 实现(acados zl/Zl);ρ 取值(Eriksen 同伦或 ρ>max|λ*| 下界)
- **TBD-7**: warm-start shift-init 实现;转移代价混合范数(航向 L2+速度 L1);可选符号翻转检测

**残余风险(显式记录)**:
- Nomoto 参数无试航数据,缩律估算 2x 误差(需 HAZID 校准或补 zigzag)
- BC-MPC 激活集成债(M5-progress §5:launch/namespace/bridge)
- C5/C9/C12 数据源补齐前不作硬约束(避免不收敛)
- A+ 若 SIL 多船极端场景不足 → 回炉 DP-05

---

## 移交 brainstorming

调用 brainstorming 时声明:

> "本方案的核心技术决策已通过 design-grounding 裁决(Step2 逐点用户确认 + Step3 R22-24 调研 + Step4/5 综合)。brainstorming 负责工程细节设计,不得推翻已裁决方案(VR-01~09+VR-01a/b)/重提弃用方案(ALT-01~12)/修改技术规约(TS-01~14),除非发现新矛盾证据则回炉 design-grounding。须推动 TBD-1~7 在实现早期澄清。"

brainstorming 衔接重点:
1. **先读本方案包 + 决策树日志**,不问已裁决的技术决策问题
2. 工程细节(per-target per-step ξ + 混合 L1/L2 落地、Nomoto 接入+字段语义、M6 几何约束推导、BC-MPC 激活集成、四状态机、Eriksen 360s 分层时域接线、acados 重表述、warm-start+混合范数反 chattering、人工参考轨迹)
3. **须推动**: TBD-5(Nomoto 参数/字段语义)、C5/C9/C12 数据源、TBD-4 acados 实测、TBD-6/7 精化实现
4. 回炉触发条件见组件 3/8
