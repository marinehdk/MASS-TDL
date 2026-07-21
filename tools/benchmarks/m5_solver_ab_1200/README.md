# M5 IPOPT/acados 1200 秒公平基准

用途：比较当前 M5 IPOPT 与 acados 生产后端在同一 Rule 14、N=80、dt=15 s 输入下的启动、cold/warm 求解、轨迹安全性与 dispatch 一致性。

该基准不修改生产源码。脚本把当前工作树只读挂载进临时容器，在 `/tmp` 构建两个后端；证据写入工作树 `runs/`。

## 场景

- 5000 m 近对头，横向偏置 -60 m，避免完全对称数值奇点。
- 自船与目标各 3 m/s，TCPA 833.273 s。
- Rule 14、BOTH_GIVE_WAY、STARBOARD、最小实质转向 15°。
- CPA hard/safe 1852 m。
- ROT 4.7°/s，减速度 0.08 m/s²。
- Horizon 1200 s，N=80，dt=15 s。

## 运行

```bash
cd /home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding
tools/benchmarks/m5_solver_ab_1200/build_and_run.sh
```

可选环境变量：

```bash
M5_BENCH_IMAGE=codex-m5-review-sil-nodes:latest
BENCH_REPEATS=5
BENCH_WARM_RUNS=10
BENCH_CPU_CORE=15
```

可给脚本传输出目录：

```bash
tools/benchmarks/m5_solver_ab_1200/build_and_run.sh \
  runs/m5_solver_ab_$(date +%Y%m%d_%H%M%S)
```

## 输出

- 两个后端的构建日志。
- 每个独立进程的原始 solver log、JSON、外部墙钟。
- `summary.json`、`runs_summary.csv`。
- timing、warm latency、trajectory、acceptance matrix 的 PNG/SVG。

## 解释边界

- IPOPT 与 acados 当前使用不同内部 formulation/physics。本测试比较现有生产后端，不是纯求解算法微基准。
- acados 的短耗时只有在 `Converged` 时才算性能结果；QP/NumericalFailure 的快速返回不得解释为速度优势。
- solver status 不能替代独立 CPA、ROT、减速度及返航检查。
- direct-solver A/B 与生产 dispatch 同时记录；dispatch 放行不等于求解成功。

2026-07-20 基线报告：
`docs/superpowers/reviews/2026-07-20-m5-solver-ab-1200/REPORT.md`。
