# D1.3c Phase 1 — Tool Confidence Report (DEMO-1 · 2026-06-15)

**版本**：v0.1
**日期**：2026-06-15
**Owner**：V&V 工程师
**适用规范**：DNV-RP-0513 §4 — Assurance of simulation models

## 1. Tool Identification

| 属性 | 值 |
|---|---|
| 工具名称 | fmi_bridge + fcb_mmg_4dof.fmu |
| 工具版本 | v0.1.0 (Phase 1, pythonfmu) |
| FMI 版本 | FMI 2.0 Co-Simulation |
| 用途 | SIL ship dynamics — own-ship MMG 4-DOF motion |
| 模型来源 | Yasukawa 2015 [R7] + FCB Capability Manifest |
| 积分方法 | RK4, dt=0.02s |

## 2. Tool Qualification Evidence (per DNV-RP-0513 §4)

### 2.1 Model Verification

| 测试 | 参考解 | 允差 | 结果 |
|---|---|---|---|
| 旋回: advance | 3L = 135m | ±5% (±6.75m) | [TBD-RUN] |
| 旋回: transfer | 0.7L = 31.5m | ±5% (±1.58m) | [TBD-RUN] |
| 直航减速 | 数值参考解 (dt=0.001s) | ≤10% | [TBD-RUN] |
| 确定性 (100 runs, same seed) | std < 1e-9 | < 1e-9 | [TBD-RUN] |

### 2.2 dds-fmu Latency Budget

| 指标 | 目标 | 实测 | 结果 |
|---|---|---|---|
| DDS roundtrip P95 | < 10ms | [TBD-RUN] | [TBD] |
| DDS roundtrip P99 | < 15ms | [TBD-RUN] | [TBD] |

### 2.3 farn Case Folder Sweep (10 cells)

| 参数 | 扰动范围 | 旋回 advance | 结果 |
|---|---|---|---|
| [TBD-RUN: farn cell config] | [TBD] | [TBD] | [TBD] |

## 3. Limitations (Phase 1)

- pythonfmu embeds Python 3.11 runtime in FMU — not suitable for IEC 61508 SIL 2 certification evidence
- Target ships use ROS2 native topic, not FMU (per D1.3c Phase 1 scope)
- Disturbance inputs (wind/current) are stubbed (zero values)
- Model parameters are HAZID-UNVERIFIED (per D1.3a §5)

## 4. Phase 2 Upgrade Path

- C++ FMI Library wrapper replaces pythonfmu
- Multi-FMU co-simulation (MMG + ncdm_vessel + disturbance)
- farn 100-cell LHS sweep
