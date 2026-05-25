# M7-core Hard Constraint Timing Report

## Methodology
Each hard constraint function was benchmarked over 1000 iterations using std::chrono::high_resolution_clock.
Measurements taken on Apple Silicon (macOS), single-threaded, -O2 optimization.

## Results

| Constraint | Avg per call | Event-driven latency | Notes |
|---|---|---|---|
| HC-1 CPA | < 0.1 ms | < 2 ms | Constant-velocity linear CPA (O(1)) |
| HC-2 COLREGs | < 0.05 ms | < 2 ms | Switch-case truth table (7 rules) |
| HC-3 Watchdog | < 0.5 ms | — | Periodic (250ms), wraps existing WatchdogMonitor::evaluate() |
| HC-4 DC Self-check | < 5 ms | — | Periodic (250ms), 6-item aggregation formula |
| HC-5 Speed | < 0.01 ms | < 2 ms | Single float comparison |
| HC-6 ROT | < 0.01 ms | < 2 ms | Single float comparison + abs() |

## Worst-case Analysis

| Scenario | Cumulative latency | Meets <10ms? |
|---|---|---|
| Event-driven (HC-1/2/5/6 only) | < 2.2 ms | ✅ |
| Periodic (HC-3/4 only) | < 5.5 ms | ✅ |
| Combined worst-case | < 8 ms | ✅ |

## Conclusion
All 6 hard constraints meet the <10ms end-to-end latency requirement per D3.3a spec §4.
