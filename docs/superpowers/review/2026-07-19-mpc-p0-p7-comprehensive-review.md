# M5 MPC P0–P7 + colregs graft 全面评审报告

> 评审日期：2026-07-20（Asia/Shanghai）  
> 评审工作树：`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`  
> 分支：`codex/m5-design-grounding`  
> 评审起点 HEAD：`c46e01045e1ad443cdadb835e62d822fc6b738a7`  
> 评审方式：生产代码只读；唯一写入为本报告及任务证据文件  
> 置信度：🟢=代码、Git 或本次运行直接证据；🟡=证据支持的根因推断；⚫=证据不足，保持 OPEN

## 1. 执行摘要（总体结论：FAIL）

**结论：FAIL。当前分支不能宣称 P0–P7 全量闭环，也不能作为 M5/COLREGs promotion 候选。**

三个核心结论：

1. **graft 没有覆盖 M5 核心源码，但工具链不兼容。** `2884e9dbb` 对 `src/l3_tdl_kernel/m5_tactical_planner/` 的 diff 为空，作者“0 M5 src/include matches”声明成立。可是 graft 改坏已有 `run_colregs_module_oracle.py` 调用契约；fast evaluator 又被 trace 中 NUL 行击穿，并出现 active run 与 scoring artifact run 不一致却报告 G-ART OK。故“核心未被覆盖”成立，“工具链无破坏”不成立。
2. **P7 codegen 维度当前一致；acados 仍未达到功能/实时闭环。** 当前 `kGTargetStride=8`、`np_global=154`、`np_per_stage=56`，codegen 与 production MX 图一致。旧容器确实复现过 `146 parameters` 对 `210 parameters` 的硬维度错误；当前源码重建后该错误消失。但手工 2500 m target 返回 wrapper `status=3` / raw acados `status=4`，全量 `test_mid_mpc_acados_solver` 600 s 超时。已有名为 `AmpleTime_FarTargetMustConverge` 的用例实际是**无目标**直线场景，不能证明 ample-time target 收敛。
3. **SIL HO/CS 未闭环。** 本次隔离容器的 HO、CS fast probe 都输出 RED；更早的 G-ART/trace consistency 已先失败，所以按 layered verdict 纪律，只能首先归因为 **ARTIFACT_INCONSISTENCY / 工具链证据失真**，不能把该 RED 直接归为 M5 行为失败。有效 CPA、SQP、收敛 KPI 无法从本次 probe 证据中认证。

此外，当前未提交 gate-v3 改动存在阻塞性 horizon gap 计算错误：扫描到首个 `min_cpa <= cpa_hard` 即 `break`，可能把深层相交误判成浅层首入 gap。该逻辑不能提交。

### 评审范围头

| 项 | 结论 |
|---|---|
| 受影响模块 | M5 主体；M2/M4/M6 输入契约；L4/M7 接管；M8/ASDR 证据链 |
| 受影响生产文件 | 本报告 §5 所列 7 个 M5 生产文件；本评审未修改它们 |
| ROS2 topics / messages / IDL | 未发现 dirty diff 修改 IDL；使用既有 WorldState、M6 决策、avoidance plan、M7/M8/ASDR 路径 |
| ODD 影响 | gate-v3 使用 ROT/preflight 阈值，但 authority/freshness 尚未统一 |
| COLREGs 影响 | dispatch、CPA gate、M6 决策可用性、HO/CS probe 证据均受影响 |
| M5/M7 边界 | FINAL_DEGRADE 报 M7 已存在；dirty reset/audit 仍不完整 |
| 必需测试 | M5 全量、acados formulation/solver/parity、BC、committed-route、OU |
| 必需 SIL | rule14-HO、rule15-CS；先通过 scenario-truth 与 G-ART，再判断 M5 |
| 证据产物 | `runs/m5_review_*_20260720.*`、`runs/trace_eval/20260720_m5_review_*` |

### Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| P0–P7 决策完整符合 | **FAIL** | VR-04/05/06b/09、TBD5/6/7 存在偏离或未闭环 |
| acados codegen 维度 | **PASS** | 8/154/56 与 generated NP=210 一致 |
| acados target 收敛与实时性 | **FAIL** | 2500 m target raw status=4；solver test 600 s timeout |
| graft 未覆盖 M5 核心 | **PASS** | M5 path diff 为空 |
| graft 工具兼容性 | **FAIL** | M7 oracle API TypeError；fast/G-ART provenance 缺陷 |
| M5 unit 回归 | **FAIL** | CTest 34/35；acados solver timeout |
| HO/CS SIL 闭环 | **FAIL / OPEN-SUT** | probe RED，但先发生 artifact consistency failure |
| 文档/认证可追溯 | **FAIL** | VR 重编号、hash/测试计数/closure 声明不可靠，P7 ASDR 缺失 |

## 2. 决策符合性（11 VR 逐项表）

权威基准：`2026-07-16-m5-mpc-colav-solution-pack.md`、设计日志、P0–P7 roadmap 与各 phase spec。实现报告自己的 VR-01…VR-11 重编号不作为权威编号。

| 决策 | 落地代码位置 | 判定 | 偏离、裁决与决策意图影响 |
|---|---|---|---|
| VR-01：Mid/BC 双层连续级联 | `launch/m5_mid_mpc.launch.py:10-29`；`src/bc_mpc/bc_mpc_node.cpp:78-112` | **符合** 🟢 | launch 同时启动 Mid/BC；未见场景 ID 或船型分支。 |
| VR-01b：四状态机、11 个枚举状态 | `committed_route.hpp:11-22`；`committed_route.cpp:444-526`；`mid_mpc_node.cpp:2092-2103` | **符合** 🟢 | 11 状态存在；FINAL_DEGRADE 通知 M7。 |
| VR-02：Nomoto Path-B 5D | `mid_mpc_acados_formulation.cpp:230-274`；`gen_mid_mpc_acados.py:300-306` | **符合后续 Path-B 裁决；偏离最初 Nomoto 理论** 🟢 | 当前是 5D `x,y,psi,u,r` + `a,rdot` 双积分模型。该 Path-B 已被后续用户裁决接受；海试 Nomoto 仍属 TBD-5。 |
| VR-03：每 target/step slack，mixed L1/L2 | `mid_mpc_acados_formulation.hpp:58-62,151-155`；`gen_mid_mpc_acados.py:155-171,508-514` | **结构符合，行为未闭环** 🟢 | `NSH=16`、`zl/Zl` 已生成。target 解算中 slack 常停在 `1e-19` 且 solver 非收敛；TBD-6 标定/正则效果未验证。 |
| VR-04：Eriksen 混合、shift-init、L2+L1 转移代价 | `mid_mpc_acados_formulation.cpp:361-467`；`mid_mpc_acados_solver.cpp:780-810,887-917,1174-1191` | **偏离** 🟢 | COLREG/route cost 已落地；warm-start 没按“新 k ← 旧 k+1”执行，且 60 s replan / 15 s stage 理应考虑 4-stage shift。无用户裁决允许同索引复用。破坏 anti-chatter 与收敛意图。 |
| VR-05：acados RTI/HPIPM | `gen_mid_mpc_acados.py:450-463`；`mid_mpc_acados_solver.cpp:1098-1106` | **偏离** 🟢 | 当前是 `SQP + FULL_CONDENSING_HPIPM`，不是 RTI。未找到正式 VR 修订。HPIPM 部分符合；实时意图未实现。 |
| VR-06b：1200 s / 15 s / 60 s / 180 s committed prefix | `config/m5_params.yaml:3-6`；`mid_mpc_node.cpp:477-483,786-805`；`committed_candidate_geometry.hpp:48-56,205-221` | **部分符合** 🟢 | N=80、dt=15、horizon=1200、replan=60 已落地。180 s 只影响 K；实际 frozen prefix 仍受固定 100 m 几何限制，5 m/s 下远小于 180 s 所需约 900 m。无正式裁决。 |
| VR-07b：相对跟踪 `t_b` + Huber；废 C10/C11 | `relative_track.cpp:27-51`；`mid_mpc_acados_formulation.cpp:320-353,454-467` | **符合** 🟢 | segment projection 与 Huber route cost 存在；terminal hard constraints 未重新加入。 |
| VR-08：45 s / 15° / 20% gate；废 keep-last；FINAL_DEGRADE→M7 | `committed_route.hpp:93-97`；`committed_route.cpp:275-307`；`mid_mpc_node.cpp:1489-1542,2092-2103` | **符合 P6 后续实现** 🟢 | stale/heading/speed gate 与 M7 上报存在；未见偷偷放宽阈值。 |
| VR-09：OU + UT + intent + BC accel | `types.hpp:65-98`；`mid_mpc_node.cpp:523-542`；`ou_uncertainty.hpp:32-47`；`mid_mpc_acados_formulation.cpp:361-428`；`bc_mpc_solver.cpp:26-38` | **部分符合且有方法偏差** 🟢 | 字段、OU、intent、BC acceleration 存在。但实现权重是中心 `0.001`、四边 `0.24975`、偏移 `±sigma` 的五点对称求积，不是 spec 声称的标准 UT α/β/κ；OU 极限分支还错误返回 `sigma0*sqrt(2)`。无正式裁决。 |
| TBD-5：Nomoto 海试辨识 | spec/roadmap TBD | **未落地 / OPEN** ⚫ | 需要船模/海试辨识数据与验收阈值。当前 Path-B 不关闭该 TBD。 |
| TBD-6：L1/L2 slack 标定 | solver/codegen penalties | **未落地 / OPEN** 🟢 | 结构存在；没有可信的可行/不可行边界标定与 active slack 证据。 |
| TBD-7：三层 anti-chattering | warm-start、transition、symbol-flip | **部分落地** 🟢 | transition cost 存在；shift-init 偏离；第三层 symbol-flip 明确延期。不能宣称关闭。 |

### 关键符合性问题

- **Warm-start**：`mid_mpc_acados_solver.cpp:780-810` 把旧 `trajectory[0]` 用于新 stage，而 VR-04/P5 要求 shift。根因：缓存复用与 replan/stage 时间关系未建模。建议：定义明确 shift_steps=`replan_period/dt`，边界不足时 terminal hold；添加逐 stage 数值断言。
- **Committed prefix**：`mid_mpc_node.cpp:786-805` 的时间 K 与 100 m 几何冻结并存，后者先截断。根因：time contract 与 geometry helper 双重 authority。建议：唯一 committed-prefix authority 使用时间/沿轨距离一致换算；测试 3/5/10 m/s。
- **所谓 UT**：`mid_mpc_acados_formulation.cpp:361-428` 与 P7 方法名不一致。建议二选一：实现标准 UT 并记录 α/β/κ；或正式修订 spec，命名“五点对称 quadrature”，停止援引标准 UT 性质。

## 3. acados 解算正确性（根因 + 修复建议）

### 3.1 维度审计

| 项 | Production MX | Codegen SX / generated | 判定 |
|---|---|---|---|
| target stride | `mid_mpc_acados_formulation.hpp:87-93`：8 | `gen_mid_mpc_acados.py:185-207,352-378`：8 | 一致 🟢 |
| `np_global` | `26 + 16×8 = 154` | `NP_GLOBAL=154` | 一致 🟢 |
| `np_per_stage` | `mid_mpc_acados_formulation.hpp:95-125`：56，含 `sigma_pos` slot 37 | `NP_STAGE=56` | 一致 🟢 |
| acados 总 NP | 154+56=210 | generated header `NP=210`；generated C `casadi_np=210` | 一致 🟢 |
| hard constraints/slacks | `kAcadosNsh=16` | generated `NSH=16, NS=16, NH=20` | 一致 🟢 |

**结论：用户怀疑的“当前 codegen 仍 stride=5”不成立。G7 静态维度 parity 已关闭。** 该同步由 `d6c11b9ad` 引入。

但旧/陈旧 install 的硬错误确实复现：

```text
acados_update_params: trying to set 146 parameters for external functions.
External function has 210 parameters. Exiting.
```

根因：容器 image install 仍是旧 production library（146），host generated solver 已是 P7（210）。当前源码在隔离容器重建后，该错误消失。`docker/sil_entrypoint.sh` 的评审期间并发 dirty 修改试图补 generated solver library path，但它不替代 production library 与 generated artifact 的构建一致性验证。

### 3.2 本次运行结果

| 验证 | 结果 | 解释 |
|---|---|---|
| `test_mid_mpc_acados_formulation` | PASS，16 cases | MX/SX formulation 结构通过 |
| `test_mid_mpc_acados_parity` | PASS，3/3，156.18 s | 选定数值 parity 通过；耗时高 |
| `test_mid_mpc_acados_solver` | **TIMEOUT 600.03 s** | 全量 14 cases 未完成；不是“通过” |
| `AcadosSolverTest.AmpleTime_FarTargetMustConverge` 单跑 | status=0，SQP=50，约 51 s | 用例实际调用 `straight_line()`，**无 target**；名称/注释误导 |
| 手工 target=2500 m | **wrapper status=3，raw acados=4，SQP=1，cost=17346.9** | 参数更新成功并进入 QP；不是 NP mismatch；target ample-time 不收敛 |

手工场景证据：`runs/m5_review_acados_ample2500_20260720.log`。

另有 target 单测返回 raw status=2 / SQP=400 仍被测试判 PASS，例如 `PerTargetBreakdown_OneTargetSlackPositive`。这些用例只记录 diagnostic 或宽松断言，不能用来证明 solver 正常收敛。

### 3.3 根因判定

1. **旧 status=1/sqp=0 的历史根因之一：build/install 版本错配。** 146/210 输出是直接证据。当前重建后该精确错误已消失。🟢
2. **当前 2500 m target 非收敛不是维度错配。** `update_params` 已成功，QP 返回 raw 4。根因落在 OCP 数值条件、initialization、约束/代价尺度或 QP 可行性。哪个子项为首因仍 **OPEN**；缺少 QP residual、condition estimate、stage constraint residual 与 seed dump。🟡
3. **生产“正常/实时”未成立。** 单个无 target 冷启动用例约 51 s，solver target 600 s 超时。现有 `Realtime_UnderBudget` 只检查 solver 内记录的 solve duration，未覆盖 constructor/codegen warm-up 与 end-to-end replan deadline。🟢

### 3.4 修复建议

1. 阻塞：新增真实 2500 m target acceptance test，要求 raw acados status=0、有限 residual、非空 avoidance trajectory；禁止用无 target `straight_line()` 代替。
2. 阻塞：保存失败 stage 的 `qp_status`、stationarity/equality/inequality/complementarity residual、active constraint/slack、initial guess hash；用同一 input 对比 MX/SX packed arrays。
3. 阻塞：容器启动时校验 production `np_global/np_stage/NP` 与 generated solver macros；不一致 fail-fast，不进入节点循环。
4. 重要：把 solver test 拆成冷启动与 warm solve；分别设置明确 budget。当前 600 s timeout 只掩盖慢测。
5. 重要：修正 stale 维度注释：`mid_mpc_acados_formulation.hpp:82-85`、`mid_mpc_acados_formulation.cpp:563-588`、`mid_mpc_acados_solver.cpp:57-60,931-933`。

## 4. colregs-nlp-cpa-fix graft 完整性（逐文件结论）

### 4.1 Git 边界验证

执行：

```bash
git show --stat --oneline --summary 2884e9dbb
git show 2884e9dbb -- src/l3_tdl_kernel/m5_tactical_planner/
```

结果：15 files changed，9160 insertions，122 deletions；第二条命令无输出。分类为 M5 核心 0、`tools/sil/` 6、scripts/tests 9。**未发现 graft 覆盖 M5 src/include/test。**

### 4.2 15 个文件逐项

| 文件 | 区域 | 结论 |
|---|---|---|
| `scripts/run_6_scenarios.py` | runner | 修改；允许 graft，但引入 NUL-line JSON 解析脆弱性与 G-ART provenance 缺陷 |
| `scripts/run_colregs_clean_8probe.py` | wrapper | 修改；参数转发，未触碰 M5 core |
| `tests/scripts/test_run_6_scenarios_gate.py` | tests | 修改；runner gate coverage |
| `tests/tools/test_colregs_artifact_consistency.py` | tests | 新增；未覆盖 active/scoring run mismatch |
| `tests/tools/test_colregs_fast_boundary.py` | tests | 新增 |
| `tests/tools/test_colregs_fast_evaluator.py` | tests | 新增；未覆盖 NUL/非 JSON 行 |
| `tests/tools/test_colregs_module_oracle.py` | tests | 修改；未覆盖 legacy CLI runner 调用 |
| `tests/tools/test_colregs_oracle_adapter.py` | tests | 修改 |
| `tests/tools/test_trace_time.py` | tests | 新增 |
| `tools/sil/colregs_artifact_consistency.py` | tools | 修改；功能扩展，但 runner 当前自比较可漏跨 run provenance |
| `tools/sil/colregs_fast_boundary.py` | tools | 新增 |
| `tools/sil/colregs_fast_evaluator.py` | tools | 新增；raw JSONL 容错不足 |
| `tools/sil/colregs_module_oracle.py` | tools | 修改；保留 `evaluate_m1…m7` 等函数名，但 **M7 API 非向后兼容** |
| `tools/sil/colregs_oracle_adapter.py` | tools | 修改；功能扩展 |
| `tools/sil/trace_time.py` | tools | 新增 |

graft 自身 Python tests：

```text
294 passed in 1.85s
```

命令覆盖上述 7 个 graft test 文件。此结果证明局部 tests 绿，不证明原 runner compatibility。

### 4.3 兼容性破坏

**[Important] legacy M7 oracle 调用被破坏。**

- 位置：`tools/sil/colregs_module_oracle.py:605`、`scripts/run_colregs_module_oracle.py:123-130`
- 直接结果：`TypeError: evaluate_m7_oracle() got an unexpected keyword argument 'unsafe_trajectory_vetoed'`
- 根因：函数从多个 keyword 参数改为 `m7_output: dict`，调用方未同步，测试也没覆盖真实 CLI。
- 修复：提供 backward-compatible wrapper，或原子更新全部调用者并添加 CLI integration test。
- 决策关联：graft “superset、不破坏原功能”声明；COLREGs layered oracle acceptance。

**[Critical] fast/G-ART runner 可产生错误的一致性绿灯。**

- 位置：`scripts/run_6_scenarios.py:2204-2221,2235-2255`
- 直接结果：HO trace line 2307、CS line 2409 为 NUL bytes；`json.loads()` 抛 `JSONDecodeError`，`fast_verdict_error` 被记录但 runner 继续。与此同时：
  - HO active `run-19f7d31b7e8`，scoring `runs/run-19f7d31b7b8/scoring.arrow`
  - CS active `run-19f7d33df1c`，scoring `runs/run-19f7d33dee7/scoring.arrow`
  - settle180 HO active `run-19f7d395cc6`，scoring `runs/run-19f7d395c85/scoring.arrow`
  runner 仍打印 G-ART OK。
- 根因：fast parser 假定每个非空行都是 JSON；G-ART verdict/timeline provenance 来自同一 runner result，未独立锁定 active run/trace/scoring lineage。
- 修复：NUL/坏行必须 fail-closed；G-ART 独立读取 artifact metadata，要求 run ID、scenario、trace/scoring/report lineage 全一致，否则 verdict=RED 且停止模块归因。
- 决策关联：COLREGs probe G-ART artifact consistency；不得用损坏证据判断 M5。

## 5. 未 commit 改动评审（起始 11 modified + 4 untracked）

评审开始时严格记录的 dirty scope 如下。评审期间另有并发变更，列在表后，不混入起始快照。

| 文件 | 合法流归类 | 结论 | 建议 |
|---|---|---|---|
| `m5_tactical_planner/CMakeLists.txt` | HO/acados diagnostic | 添加/调整测试或 profiling wiring；非 P7 核心 closure | **分离 diagnostic commit**；确认不改变 release flags |
| `common/types.hpp` | gate-v3 / M6 classification | 扩充 TargetState/Classification 数据 | **暂不提交**；先闭合 freshness/source/run contract |
| `gnc_avoidance_preflight.hpp` | gate-v3 | gate/preflight 条件 | **暂不提交**；ROT/ODD authority 必须唯一 |
| `mid_mpc_solver.hpp` | gate-v3 | 含阻塞性 horizon gap bug、NaN fail-open、align_sin worst-case 方向错误 | **revert 或修复后独立提交** |
| `mid_mpc_acados_solver.cpp` | P7/acados diagnostic | 主要为 instrumentation/profiling | **独立 diagnostic commit**；不要与行为 gate 混合 |
| `mid_mpc_node.cpp` | gate-v3/HO chain | dispatch/reset/ASDR 变更 | **暂不提交**；M6 freshness、counter reset、ASDR 完整性未闭环 |
| `mid_mpc_solver.cpp` | gate-v3/HO chain | gate reject fallback/audit | **暂不提交**；IPOPT fallback backend/counter audit 漏报 |
| `test_avoidance_waypoint_gen.cpp` | gate-v3 tests | waypoint/gate coverage | **随修复提交**；不能单独证明 horizon safety |
| `test_mid_mpc_solver.cpp` | gate-v3 tests | 有 tautology、enum-only、非行为测试 | **重写后提交**；加入 deep-crossing、NaN fail-closed、ASDR payload |
| `MASS_ADAS_L3_TDL_架构设计报告.md` | 文档同步 | 与旧 N/horizon/IPOPT/latency 声明混杂 | **单独文档评审/提交**；不要与 production patch 同 commit |
| `handoff/workspace_log.md` | handoff | 过程记录 | 核对结论后单独提交；不能把 G7/G8 写成全绿 |
| `docs/superpowers/research/` | P7 research | 研究证据 | 单独 evidence commit；验证来源可访问性与引用边界 |
| `2026-07-19-ho-red-execution-chain-diagnosis.md` | HO RED 诊断 | 合法诊断流 | 可单独提交；把未证实首因标 OPEN，避免“不是 P7”过度结论 |
| `2026-07-19-m5-acados-dispatch-gate-v3-event-based-design.md` | gate-v3 redesign | 合法设计流，但自相矛盾：未实现/已实现/待裁决并存 | **先裁决，后提交**；不能作为已批准 authority |
| `2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md` | HO/acados probe | 合法诊断流 | 可单独提交；“innate/Jacobian”需 condition/residual 证据，否则标 OPEN |

### dirty diff 阻塞发现

1. **[Critical] horizon deep-crossing 可误判安全**  
   位置：`mid_mpc_solver.hpp:215-228`。循环在首次 `min_cpa <= cpa_hard` 后 `break`，得到首入浅 gap，而非全 horizon 最小 signed gap。根因：event-entry 逻辑误作 horizon extremum。修复：完整扫描所有 stage；若要 early reject，只能在已满足“不可能恢复”的数学条件下执行。对应 P5、VR-05/06b、gate-v3 C3/C4。
2. **[Important] NaN target fail-open**  
   位置：`mid_mpc_solver.hpp:323-371`；`test_mid_mpc_solver.cpp:752-769`。gate 跳过非有限 target，随后仍把原 input 送 solver。修复：输入 sanitation 发生在唯一入口；安全相关 NaN fail-closed，输出明确 invalid-input reason。对应 M2→M5 contract、SOTIF。
3. **[Important] fallback audit 错报**  
   位置：`mid_mpc_solver.cpp:169-184`。gate reject 后走 IPOPT，却未更新 `last_nlp_backend_` 与 fallback counter。修复：backend/result/counter 原子更新；ASDR 测试验证。对应 VR-05、M8 auditability。
4. **[Important] counters 跨 scenario 污染**  
   位置：`mid_mpc_solver.hpp:120-135`、`mid_mpc_node.cpp:2052-2090`。reset 未清全部 gate/fallback counters。修复：定义 lifecycle reset contract，scenario/run 变化时清理。对应 G-ART provenance、M8 ASDR。
5. **[Important] M6 可用性仅检查指针**  
   位置：`mid_mpc_node.cpp:580-608,2077-2090`。无 stamp/source/run freshness。修复：沿用 fail-closed provenance contract，过期/跨 run M6 决策不得驱动 dispatch。对应 VR-04、M6→M5 contract。
6. **[Important] ROT authority 不一致**  
   位置：`mid_mpc_node.cpp:757-771,1838-1840,2058-2074`、`mid_mpc_solver.cpp:231-275`、`gnc_avoidance_preflight.hpp:28-35`。存在 hard-coded 4.7 与 live/default 1.2/2.0 多源。修复：只接收 M1 ODD authority；preflight、solver、tests 使用同一值。对应 architecture ODD invariant。
7. **[Important] tests 非行为断言**  
   位置：`test_mid_mpc_solver.cpp:805-871,1031-1060`。包含布尔 tautology、只检查 const input 未变、只检查 enum 数字。修复：检查 solver selection、reason、ASDR payload、horizon extrema、BC takeover。对应 gate-v3 verification。

### 评审期间 scope drift

评审开始后出现非本评审产生的 dirty/untracked：`docker/sil_entrypoint.sh`、`docs/superpowers/review-prompts/`、`docs/superpowers/specs/2026-07-20-m5-v31-upstream-fix-plan.md`。本报告保留这些文件，不修改、不纳入“起始 11+4”判定。合并前必须由所有者说明来源并重新冻结快照。

## 6. gate-2 dispatch 重设计评审

被评审 commit：`531aa65b7773372878986e012cca015493ecee1c`，修改 M5 solver header/cpp/test，另加 safety memo/diff。

### 结论

**FAIL：该 redesign 与 P5/P6 authority 不一致；“align BC-MPC boundary”声明不成立。**

1. P5 spec 的 ample-time 用户裁决是**当前距离约 2000 m**边界，不是 TCPA 2000 s，也不是 `min_cpa < cpa_safe`。
2. commit 把 dispatch 改为 M2 linear CPA gate；gate hit 后走 IPOPT fallback，**并不触发 BC-MPC takeover**。因此不能说与 P6 BC boundary 对齐。
3. P6 BC 激活语义是 Mid failure/handover lifecycle；单个 CPA gate 是 solver backend selection，不等于四状态机 takeover。
4. 未发现 vessel-specific branch 或 scenario-id conditional。该纪律项 PASS。
5. 当前 dirty gate-v3 试图替代 531aa65b7，但自身有 §5 的 Critical horizon bug，且设计文档未获正式裁决。

### 修复建议

- 先固定三个不同概念：ample-time qualification、Mid backend dispatch、Mid→BC takeover。分别定义 authority/input/transition/ASDR，不共享一个含糊 gate。
- 若推翻 P5“当前距离约 2000 m”裁决，必须新建正式 ADR/VR amendment，说明 CPA gap 优于 range 的证据与反例。
- 给四状态机添加 event-sequence test：Mid eligible→acados fail→IPOPT/BC policy→committed candidate→L4/M7；不能只测 gate boolean。

## 7. 测试结果（全量 + 分类）

### 7.1 构建

隔离容器、当前 dirty source、acados/CasADi enabled：

```bash
colcon build --packages-select m5_tactical_planner --symlink-install \
  --executor sequential --cmake-args \
  -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON -DM5_USE_CASADI=ON
```

结果：PASS。证据：`runs/m5_review_colcon_build_isolated_20260720.log`。

### 7.2 全量 M5

```bash
colcon test --packages-select m5_tactical_planner \
  --event-handlers console_direct+ --return-code-on-test-failure
colcon test-result
```

结果：

- CTest target：**34/35 PASS，1 TIMEOUT**。
- 失败 target：`test_mid_mpc_acados_solver`，600.03 s。
- 总 wall time：772.10 s。
- `colcon test-result` 当前 CTest XML：35 tests，0 errors，1 failure。
- 聚合目录还包含旧 XML，打印 `868 tests, 1 error, 1 failure, 392 skipped`；该数不是本轮唯一 case count，不用于通过声明。

证据：`runs/m5_review_colcon_test_full_20260720.log`、`runs/m5_review_colcon_test_result_20260720.log`。

### 7.3 分类结果

| 分类 | 结果 | 备注 |
|---|---|---|
| acados formulation | PASS | 16 cases |
| acados parity | PASS | 3/3；156.18 s |
| acados solver | **TIMEOUT** | 600 s；若干 target case 非收敛却按 diagnostic PASS |
| BC-MPC solver | PASS | targeted run |
| BC-MPC node handover | PASS | targeted run |
| committed-route FSM | PASS | 11-state suite target PASS |
| P7 OU uncertainty | PASS | 11 cases |
| graft Python tests | PASS | 294 cases；但缺真实 CLI compatibility test |

BC/committed/OU targeted CTest：4/4，0.35 s，证据 `runs/m5_review_bc_committed_ou_20260720.log`。

### 7.4 失败归因

- 全量唯一 target failure 属于 **acados solver 时限/收敛问题**，不是 graft 直接覆盖 M5 代码造成。
- graft 的 API/provenance 缺陷由其 Python unit suite漏测，属于**工具链回归**。
- dirty gate-v3 Critical bug目前未被 test 捕获，属于**HO RED 诊断流引入的未提交风险**。

## 8. SIL 解算闭环结果

### 8.1 环境隔离

未触碰占用标准 18000/domain42 的其他 stack。建立任务专属 `codex-m5-review`，orchestrator 18001、ROS domain43；从当前 worktree 重建 M5。所有 localhost 请求使用 `NO_PROXY/no_proxy=127.0.0.1,localhost`。

### 8.2 probe 结果

| 场景 | Runner verdict | Layered first failure | 可认证 M5 KPI？ |
|---|---|---|---|
| rule14-HO fast，24 s settle | RED | `SCENARIO_TRUTH_NOT_LOCKED` / SCENARIO；独立 G-ART RED | 否 |
| rule15-CS fast，24 s settle | RED | 同上；另有 M2 `MEASUREMENT_INCONSISTENT` | 否 |
| rule14-HO fast，180 s settle | RED | active/scoring run mismatch；trace NUL line | 否 |

证据：

- `runs/m5_review_rule14_ho_fast_20260720.json`
- `runs/m5_review_rule15_cs_fast_20260720.json`
- `runs/m5_review_rule14_ho_fast_settle180_20260720.json`
- `runs/trace_eval/20260720_m5_review_rule14_ho_fast/`
- `runs/trace_eval/20260720_m5_review_rule15_cs_fast/`
- `runs/trace_eval/20260720_m5_review_rule14_ho_fast_settle180/`

raw trace 过滤单个 NUL line 后的 manual fast evaluation：

- HO：scenario truth false、transit false、avoidance false、M2 true、M6 false、M4 true、M5 false（NO_FEASIBLE_PLAN）、L4 true、M7 true、M1 false；M8/G-ART false。
- CS：同类失败，另有 M2 measurement inconsistency。
- M8/G-ART reason：`trace_cpa_unavailable`、`trace_recovery_unavailable`、`recovery_time_mismatch`。

首次 poll 已报告 sim time 5794/7451/11583，明显超过 override 900，wall time 却仅约 0.2–0.3 s；这进一步证明 scenario reset/run ownership 未锁定。

### 8.3 分层判定

按 colregs-probe 的 G-ART first 纪律：

1. 当前归因：**ARTIFACT_INCONSISTENCY / SCENARIO lifecycle failure**。
2. 不能从该证据宣称“M5 导致 HO/CS RED”，也不能宣称“M5 正常”。
3. CPA、SQP iter、收敛、avoidance_plan KPI 均为 **OPEN**，因为 active run 与 artifact lineage 不可信。
4. 容器重建后另见一次 `[M5][MidMPC][acados] status=1 ... sqp_iter=0 cost=0` 与 `c5_aligned` gate reject；它证明生产路径仍有失败，但不属于本次有效 HO/CS probe lineage，不能强行拼接成场景结论。

### 8.4 闭环条件

重新验收前必须：

1. 修复 module oracle CLI contract 与 NUL fail-closed。
2. 每次 probe 确认唯一 configure driver；reset 后 sim time 从受控起点开始。
3. active run ID、trace、scoring.arrow、report、scenario manifest 全部同 lineage。
4. G-ART GREEN 后再读 M2→M6→M4→M5→L4→M7。
5. 对 M5 记录 raw acados status、SQP、residual、plan publication、L4 consumption；HO/CS 各至少一次可重现 run。

## 9. 文档与认证可追溯性

### 9.1 实现报告与实际不一致

文件：`docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md`。

1. **VR 编号失真**：`line 14,43-57` 自称 design-log VR-01…VR-11 全落地，却把权威 VR-01…VR-09/修订项重新编号。无法逐项追溯 solution-pack 的 11 项 VR。
2. **closure 声明自相矛盾**：`line 8,14-22` 写“P0–P7 全量闭环、11/11、88+全过”；`line 284-327` 又明确 G7 部分、G8 HO RED；`line 381-393` 仍有 open items。
3. **commit attribution 不准确**：`line 6` 把 P7 HEAD 写为 `d02a2a087`，该 commit 实际是 P7 docs/spec；production P7 是 `d6c11b9ad`。表中 `4aff16587` 实际也是 P1b spec commit，不是对应 production migration 证明。
4. **测试数字冲突**：开头 88+，`line 262` 为 77；未解释 suite overlap。当前全量又是 34/35 target，不能继续引用历史“全部通过”。
5. **预测当证据**：`line 211-219` 明写“预计”，但被总体 closure 包含；`line 242` 声称所有 target converged，与本次 2500 m raw status=4 冲突。
6. **架构报告陈旧**：`MASS_ADAS_L3_TDL_架构设计报告.md:455-470,850-972,2823-2829` 仍含 N=18/90 s、IPOPT、<500 ms、BC 30–90 s 等旧陈述，和 80/1200 s/acados/当前 runtime 不一致。

### 9.2 P7 ASDR 缺口

`mid_mpc_node.cpp:1284-1314` 的当前 ASDR 未完整输出：

- `intent_confidence` 及其 source/stamp/run provenance；
- 每 target/stage `sigma_pos` 或可重算的 OU 参数；
- UT/五点求积 cost contribution；
- robust cost 与 nominal cost 差值；
- solver packed dimension/version hash。

因此无法从 ASDR 证明 P7 是否真正进入 production OCP，也无法审计 uncertainty 是否改变了行为。对应 CCS auditability、ISO 21448 degraded perception 与 VR-09。

### 9.3 方法与认证边界

实现报告 `line 24,37,140-170` 明确写出“P7 是 [RMD] Ch3 工程扩展，非 Eriksen 方法”，**边界表述本身清楚**。但以下仍 OPEN：

- 当前五点求积是否可称为 UT；
- [RMD] 是否直接支持报告中的 OU/UT 具体公式与权重；
- `intent_confidence` 的安全含义、数据质量与 fail-safe policy；
- 这些工程扩展如何映射到 requirement→test→evidence，而非只列参考文献。

建议：保留“非 Eriksen”边界；将每个 P7 claim 分成 source claim、engineering choice、assumption、verification evidence 四列。没有原文/实验支撑的项标 `[TBD-reason]`。

## 10. Critical / Important / Minor 发现清单（按严重度）

### Critical

| ID | 位置 | 根因 | 修复建议 | 决策记录 |
|---|---|---|---|---|
| C-01 | `mid_mpc_solver.hpp:215-228` | 首次进入 hard CPA 区即停止扫描，把 entry gap 当全 horizon minimum | 完整扫描；加 shallow-entry/deep-crossing 反例与 property test | P5、VR-05/06b、gate-v3 C3/C4 |
| C-02 | `scripts/run_6_scenarios.py:2204-2255` | NUL line parser 非 fail-closed；G-ART 与 runner 同源自证，未锁 active/scoring lineage | 坏行即 RED；独立 artifact metadata 校验；run/scenario/hash 全一致 | G-ART、COLREGs probe acceptance |
| C-03 | `test_mid_mpc_acados_solver.cpp:859-890`；production solver | “FarTarget”实为无 target；真实 2500 m target raw status=4；全 suite timeout | 新增真实 target convergence acceptance；输出 residual/seed/constraints；关闭 600 s timeout | VR-03/05/06b、G7/G8 |
| C-04 | implementation report `:8-22,43-57,284-327,381-393` | 权威 VR 被重编号；FAIL/open 与“11/11全闭环”并存 | 报告按权威 VR 重写；当前状态降为 FAIL/open；commit/test evidence 一项一证 | 全部 P0–P7 VR、认证 traceability |

### Important

| ID | 位置 | 根因 | 修复建议 | 决策记录 |
|---|---|---|---|---|
| I-01 | `tools/sil/colregs_module_oracle.py:605`；runner `:123-130` | M7 oracle signature 非兼容 | wrapper 或原子升级调用方；CLI integration test | graft superset 声明 |
| I-02 | `mid_mpc_acados_solver.cpp:780-810,887-917,1174-1191` | warm-start 未 shift，未考虑 60/15=4 stage | 定义 shift_steps；terminal fill；数值测试 | VR-04、TBD-7 |
| I-03 | `mid_mpc_node.cpp:786-805`；candidate geometry `:48-56,205-221` | 180 s time prefix 被固定 100 m 截断 | 单一时间/沿轨 authority；多速率测试 | VR-06b |
| I-04 | `gen_mid_mpc_acados.py:450-463` | 使用 full SQP 而非 RTI，无 amendment | 实现 RTI 或正式修订 VR；重定实时 budget | VR-05 |
| I-05 | `mid_mpc_acados_formulation.cpp:361-428` | 自定义五点权重被称标准 UT | 正式 UT 或改名/修订理论 claim | VR-09/P7 |
| I-06 | `ou_uncertainty.hpp:32-47` | 极限分支返回 `sigma0*sqrt(2)`，与公式渐近值 `sigma0` 冲突 | 修公式并加 extreme-horizon test | VR-09/P7 |
| I-07 | `mid_mpc_node.cpp:1284-1314` | P7 uncertainty/intention/robust cost 未进入 ASDR | 输出 provenance、sigma、cost breakdown、dimension hash | VR-09、CCS/SOTIF |
| I-08 | `mid_mpc_solver.hpp:323-371` | NaN target gate 跳过后仍下发 solver | 入口 sanitize/fail-closed | M2→M5、SOTIF |
| I-09 | `mid_mpc_solver.cpp:169-184` | IPOPT fallback audit/backend counter 漏更新 | 原子更新 backend/reason/counter | VR-05、M8 |
| I-10 | solver/node reset `:120-135,2052-2090` | counters 未按 run/scenario reset | lifecycle reset contract | G-ART、M8 |
| I-11 | `mid_mpc_node.cpp:580-608` | M6 availability 无 freshness/source/run | fail-closed provenance gate | VR-04、M6→M5 |
| I-12 | preflight/node/solver ROT paths | ODD/ROT 多 authority 与 hard-coded 4.7 | 仅 M1 ODD authority | architecture ODD invariant |
| I-13 | `bc_mpc_node.cpp:18,34-37,66-112` | VR 所述 5 s BC replan 未实现；WorldState 到达即 solve | timer/trigger contract或正式修订 | VR-01/06b/P6 |
| I-14 | `test_mid_mpc_solver.cpp:805-871,1031-1060` | tautology/enum-only tests | 行为、reason、ASDR、state transition assertions | gate-v3 verification |
| I-15 | implementation/architecture docs | hash、test count、参数/后端陈旧 | 自动生成 evidence matrix；按当前 code/test 更新 | 全部 VR、cert traceability |

### Minor

| ID | 位置 | 根因 | 修复建议 | 决策记录 |
|---|---|---|---|---|
| M-01 | formulation header/cpp、solver cpp 的 106/146 注释 | P7 stride 更新后注释未同步 | 更新为 154/210；加入 static/generated check | VR-09/G7 |
| M-02 | `committed_route.cpp:437`、`mid_mpc_node.cpp:1476-1487` 等 | 生命周期/keep-last 注释陈旧 | 文档性修正，单独 commit | VR-08 |
| M-03 | untracked design/probe docs | 文件名 `acatos`/状态语义混用，因果语言过强 | 统一 acados spelling/status mapping；OPEN 先因 | G7/G8 |

## 11. 修复优先级建议（阻塞性 → 重要 → 改进）

### P0：阻塞性

1. **冻结并清理 dirty scope**：禁止提交当前 gate-v3 behavior diff；先修 C-01，补 deep-crossing/NaN fail-closed tests。
2. **修工具证据链**：I-01 + C-02。module oracle CLI 可运行、trace坏行 fail-closed、active/scoring/report lineage 唯一后，才重跑 HO/CS。
3. **建立真实 acados target acceptance**：2500 m target必须 raw status=0；收集 residual、stage constraints、seed、packed hash，定位 raw status=4 首因。
4. **修订实现报告状态**：撤销“11/11全闭环/全部测试通过”结论，明确 G7静态 parity PASS、target convergence FAIL、G8 OPEN/RED。

### P1：重要

1. 关闭 warm-start shift、180 s committed prefix、BC replan contract。
2. 统一 M1 ODD/ROT authority；补 M6 stamp/source/run freshness；完整 reset/audit counters。
3. 决定 RTI vs full SQP、标准 UT vs五点 quadrature；任何偏离先正式 amendment，再改实现/报告。
4. P7 ASDR 输出 intent/OU/sigma/robust cost/provenance。

### P2：回归与 SIL

1. 全量 M5 35/35 target PASS；`test_mid_mpc_acados_solver` 在明确 budget 内完成。
2. HO/CS 各跑至少一次：G-ART GREEN→scenario truth GREEN→模块 oracles→trajectory KPI。
3. 保存 run-name 自有 trace、scoring、report、trajectory；记录 CPA、raw acados status、SQP、residual、published plan、L4 consumption、M7 verdict。

### P3：改进

1. 清理 stale 注释、术语、`acatos` 拼写。
2. implementation report/architecture report从可执行 evidence matrix 生成参数、hash、测试数，减少手写漂移。
3. 将 diagnostic/profiling、production behavior、tests、docs 分语义 commit。

---

## 附录 A：本次关键命令与证据

```bash
# graft
git show --stat --oneline --summary 2884e9dbb
git show 2884e9dbb -- src/l3_tdl_kernel/m5_tactical_planner/
python3 -m pytest -q \
  tests/scripts/test_run_6_scenarios_gate.py \
  tests/tools/test_colregs_artifact_consistency.py \
  tests/tools/test_colregs_fast_boundary.py \
  tests/tools/test_colregs_fast_evaluator.py \
  tests/tools/test_colregs_module_oracle.py \
  tests/tools/test_colregs_oracle_adapter.py \
  tests/tools/test_trace_time.py

# broken compatibility reproduction
python3 scripts/run_colregs_module_oracle.py \
  --trace runs/gate_v3_1_sil/ho_v31/colreg-rule14-ho.trace_current.jsonl \
  --scenario colreg-rule14-ho --out /tmp/m5_review_module_oracle.json

# current-source container build/test
colcon build --packages-select m5_tactical_planner --symlink-install \
  --executor sequential --cmake-args \
  -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON -DM5_USE_CASADI=ON
colcon test --packages-select m5_tactical_planner \
  --event-handlers console_direct+ --return-code-on-test-failure
colcon test-result

# isolated typical probes
NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost \
SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho --m5-short-avoidance-gate --fast

NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost \
SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule15-cs --m5-short-avoidance-gate --fast
```

## 附录 B：评审最终状态

- Reviewer workspace writes：本报告及 `runs/m5_review_*` 评审证据；生产代码 none。
- Promotion：**禁止**。
- 仍需用户裁决：RTI vs full SQP；标准 UT vs五点 quadrature；P5 range boundary是否被 CPA-gap 正式替代；gate-v3 是否获准进入修复流。
- OPEN：2500 m raw acados=4 的精确数值首因；有效 HO/CS SUT verdict；TBD-5/6/7 closure。
