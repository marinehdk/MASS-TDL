# Session Handoff — 2026-07-20 M5 acatos CPA hard floor true-hard 化

## Task
修复 M5 Mid-MPC acatos 后端首次 QP NAN_SOL(raw 4)根因,按 7 层架构(L0-L5+LX)逐层修复。本会话完成 L0 + L1 Step2 + Step5 + L1a-spec-freeze 批次 1。

## Branch / HEAD
codex/m5-design-grounding @ ccd9acd1f(worktree .worktrees/m5-design-grounding)
- fe251260b L1a-spec-freeze 批次 1 源码
- ccd9acd1f 设计 docs + 架构文档

## Investigation Chain / Root Cause
1. acatos CPA row 全部进 idxsh(gen:510 `idxsh=arange(NT)+2`),BL-A 数学事实证实 row 进 idxsh 后残差减 slack(bgh.c:1411-1417)→ 结构上永远 soft。CPA "hard floor" 实际是 soft
2. 3 个 reference-feasible Rule14 case 在 soft floor 下 solver 解出违反 ample-time 的轨迹后被 KKT 拒绝 → raw 4
3. 7 层架构(L0-L5+LX)逐层修复,层间 GATE 管控
4. L0 输入守卫完成(6 guard + InputDegradation bitmask)
5. L1 Step2 评审暴露 9 盲区 + Step5 DESIGN-IT-TWICE 抉择方案 B(nh=20+nsh=0+J_colreg)
6. L1a-spec-freeze 批次 1(CPA hard floor true-hard 化)源码完成

## Key Decisions(下次会话必读)
- VR-01 final:方案 B(nh=20+nsh=0+J_colreg cost barrier 表达 soft);方案 A(nh=36 双 row)弃用(无生产先例 + double-expression 权重协调 known-hard)
- VR-07:L1a = 规格冻结 + 不依赖 k_head 子项;k_head 公式挪 L1b
- VR-08:parity 框架修正,IPOPT 不是 true hard oracle(σ 全局标量),两路径共同对齐到语义正确 OCP 规范
- VR-09:NLM retrieval 不可信,改用 IMO/MAIB 一手源

## Pitfalls
- **worktree 纪律**:启动 subagent 前必须 `git status` 确认 clean。本会话因脏 worktree 导致批次 1 与遗留 gate-2 v3.1 混合,已剥离
- **构建环境分离**:host 无 acatos/casadi Python 模块,只在 sil_nodes 构建容器里有。codegen+colcon 必须进容器
- **codegen 在 colcon build 自动触发**(CMakeLists.txt:264)
- **stale c_generated_code 是 git-ignored**,每次 colcon build 重生成
- **stale header(NSH=16/NP=210)与本批次源码(NSH=0/NP=211)不兼容**,不跑 codegen acatos-ON build 会 NP mismatch crash

## Next Steps
1. 容器内 codegen re-run + colcon test + 三 case SIL 回归(SC-01/02/03)
2. T4 friend-test 补(FB-2 d_min/violation_m unit test)
3. T-B1/T-B3 ample-time 实测(SC-08 距离扫点 + Rule 8/16)
4. L1a-spec-freeze 批次 2(box live + terminal contract + grid map + ROT 来源)
5. L1b(k_head + CPA suffix-hard + prefix witness + VR-03 b' + Q4 σ)
6. C2/C3/C4(L3 数值 / L4 fail-closed / L5 BC→L4+MRM)
