# HIL Equivalence Declaration — D3.7

| 属性 | 值 |
|---|---|
| 文件版本 | v0.1-draft |
| 日期 | 2026-07-13 |
| 用途 | HIL 采购需求单附件（D4.1/D4.2 预条件）|
| 状态 | 初稿（8h SIL 测试数据 8/17 后回填 §3 实测列）|

---

## 1. 等价声明范围

本文档声明 D3.7 SIL 环境所验证的 6 类功能点，在 Phase 4 HIL 环境中的等价验证策略。

| # | SIL 验证功能点 | HIL 等价验证策略 |
|---|---|---|
| F1 | M7 PATH-S 独立性（`check-m7-path-s.sh` 0 violation）| HIL 中重复运行 PATH-S CI；M7 二进制相同，独立性不随硬件变化 |
| F2 | MRC 触发链（M7 → MRC → 安全等待 ≤60s）| HIL 注入相同场景 YAML，验证实际推进器停机时序；TMR ≥60s 在真实硬件中更保守 |
| F3 | Y-axis Reflex Arc < 500ms（`/reflex/override_cmd` 到达 L5 stub）| HIL 中 L5 stub 替换为真实推力分配接口；验证端到端 < 500ms |
| F4 | §15.2 接口矩阵延迟（24 行 p99 实测值）| HIL 用相同 `latency_monitor.py` 运行；预期 p99 下降（无 Docker 网桥开销）|
| F5 | ASDR HMAC-SHA256 完整性（全记录 PASS）| ASDR 写端逻辑不变；`SIL_INTEGRITY_KEY` 替换为 HIL 密钥；期望同等 PASS 率 |
| F6 | `/m7/sil_observability` 观测通道有效性（全程无 >5s 中断）| HIL 中 M7 相同 timer callback；仅时钟源从软件仿真时钟改为硬件 PTP |

---

## 2. 时序等价边界

| 时序因素 | SIL 环境 | HIL 环境 | 差异分析 |
|---|---|---|---|
| 时钟同步精度 | OrbStack 宿主机 chrony/PTP ≤1ms | 硬件 PTP IEEE 1588v2 ≤100μs | HIL 更精确；SIL 测量结果为保守上界 |
| DDS 传输延迟 | Docker bridge ~0.1–2ms | 物理以太网 ~0.05–0.5ms | HIL 延迟更低；SIL p99 阈值对 HIL 安全 |
| 进程调度 jitter | Linux CFS，无 PREEMPT_RT | 视 HIL 平台；建议 PREEMPT_RT | 非实时内核 jitter ≤5ms；PREEMPT_RT ≤500μs |
| M5 IPOPT 求解时间 | 软件仿真无推进器反馈 | 真实推进器反馈影响迭代收敛 | [TBD-D4.1: 实测 M5 在 HIL 中求解时间分布] |

---

## 3. 不等价项声明（SIL 无法等价 HIL 的项目）

以下项目需在 D4.1/D4.2 HIL 阶段补足，不在 D3.7 SIL 范围内：

1. **真实推进器响应延迟**：SIL 中 L5 是 stub，推进器响应设为零延迟。HIL 中推进器响应延迟需实测（典型 50–200ms），影响 MRC 时序计算。
2. **传感器 noise 真实分布**：SIL 使用参数化 noise 模型（MMG 4-DOF）；HIL 使用真实 GNSS/Radar，noise 分布非高斯尾部行为需单独验证。
3. **电磁兼容性（EMC）**：SIL 无 EMC 效应；HIL 需按 IEC 60945 验证桥楼电磁环境对 ROS2 DDS 通信的影响。
4. **硬件看门狗触发**：SIL 中进程崩溃检测用 `pgrep`；HIL 中需验证硬件 WDT 在 OOM/SIGABRT 时的行为。

---

## 4. HIL 硬件性能最低要求（引用 §15.2 阈值）

以下为 HIL 硬件采购必须满足的网络/计算性能最低要求，直接引用 D3.7 spec §6.2 实测阈值：

| 接口 | 阈值 | 硬件要求 |
|---|---|---|
| DDS 消息传输（高频，M2→M4 等）| p99 ≤ 50ms | 交换机端到端延迟 ≤10ms（百兆以太网足够）|
| M5 AvoidancePlan（M5→L4）| p99 ≤ 1000ms | CPU 单核浮点性能 ≥ 2 GHz（i7/Xeon 级别）|
| Y-axis Reflex Arc | p99 ≤ 200ms | 实时调度能力（建议 PREEMPT_RT Linux）|
| M7 VETO → M1 | p99 ≤ 50ms | DDS QoS RELIABLE 配置，无中间代理 |
| M8 HMI 透明性 | p99 ≤ 20ms | Shore Link 网络带宽 ≥ 1Mbps（VPN/4G）|

---

## 5. 实测数据回填（8/17 后）

> 本节在 8h SIL 测试完成后（2026-08-17 以后）回填实测 p99 值，与 §15.2 阈值对比。

| 接口矩阵行 | 阈值 ms | SIL 实测 p99 ms | 余量 % |
|---|---|---|---|
| 1-3 /l3/m1/odd_state | 100 | [TBD-8h] | [TBD] |
| 4-7 /l3/m2/world_model | 50 | [TBD-8h] | [TBD] |
| 11 /l3/m5/avoidance_plan | 1000 | [TBD-8h] | [TBD] |
| 16 /m7/safety_verdict (VETO) | 50 | [TBD-8h] | [TBD] |
| 17 /m7/sil_observability | 50 | [TBD-8h] | [TBD] |
| 22 /reflex/override_cmd | 200 | [TBD-8h] | [TBD] |

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1-draft | 2026-07-13 | 初稿：HIL 采购需求单附件；§3 实测列待 8h 测试后回填 |
