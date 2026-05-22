# BNWAS 等效机制设计草案 · D2.6

| 版本 | 2026-06-23 v0.1 |
|---|---|
| 范围 | **设计草案（Design Draft）；实现推 D2.8 v1.1.3 stub（4 缺失模块之一）** |
| 完整合规论证 | SAT1_EQUIV 轨道推 D3.9 RFC-007 cyber |
| Finding 关闭 | D P0-D-05 / D P1-D-04（本草案 commit 后关闭）|

## 1. 监管背景

**监管空白 🟡：** IMO MSC.282(86) 仅适用于有人值守桥楼。MASS 自主模式下 BNWAS 无直接对应规则。

- IMO RSE (MSC.1/Circ.1606, 2019) 已确认此监管空白 [R-IMO-RSE]
- SOLAS 和 STCW 修订预计 2026-2028 完成
- CCS 要求：远控站须接收船舶机械告警，告警沉默机制须独立 [R-CCS-REMOTE]
- **本草案选择保守合规路径：** DUAL 模式（双轨同时启用），允许 CCS 选择任一轨道作为合规依据

## 2. 设计方案 — 双轨 A+B 可切换

### 轨道 A：HEARTBEAT（心跳信号）

- 对应传统 BNWAS A 级（MSC.282(86)）
- ROC/船长须每 `interval_s`（默认 180s）确认一次
- 触发方式：点击 Web HMI ToR 面板上的"在位确认"按钮
- 告警升级：AUDIO_LOCAL → AUDIO_ROC（+grace_period_s）→ MRC_PREPARE（+30s）

### 轨道 B：SAT1_EQUIV（SAT-1 等效交互）

- 以 SAT-1 面板有效交互作为"有意味的人为干预"
- 理论依据：§12.4.1 已有"已知悉 SAT-1"交互验证机制
- 重置触发：`tor_acknowledgment_clicked` / `sat1_panel_interaction` / `mode_switch_confirmed`
- `timeout_s = 300`（5 分钟，比 heartbeat 宽松）

### DUAL 模式（默认推荐）

- 双轨同时运行，两份独立记录写入 ASDR
- CCS 审计时以 HEARTBEAT 轨道为主合规依据（论证工作量低）
- SAT1_EQUIV 记录作辅助证据

## 3. 告警升级序列

| 轨道 | 超时点 | 动作 |
|---|---|---|
| HEARTBEAT | interval_s 到期 | AUDIO_LOCAL |
| HEARTBEAT | interval_s + grace_period_s | AUDIO_ROC |
| HEARTBEAT | interval_s + grace_period_s + 30s | MRC_PREPARE |
| SAT1_EQUIV | timeout_s 到期 | AUDIO_ROC |
| SAT1_EQUIV | timeout_s + 60s | MRC_PREPARE |

## 4. ASDR 记录要求

```yaml
asdr_record:
  heartbeat_events: true
  # 格式：{timestamp_utc, bnwas_mode, trigger: "heartbeat_confirmed|timeout",
  #         operator_id: "ROC-01|BRIDGE", response_time_s: 12.3}
  sat1_interaction_events: true
  # 格式：{timestamp_utc, interaction_type: "tor_ack|panel_click|mode_switch",
  #         operator_id: "ROC-01|BRIDGE", sat1_state_snapshot: {...}}
```

## 5. CCS 合规论证路径

| 模式 | 合规路径 | 论证工作量 |
|---|---|---|
| HEARTBEAT | 直接映射 MSC.282(86) + CCS 远控站告警规范 [R-CCS-REMOTE] | 低 |
| SAT1_EQUIV | 须论证"SAT-1 交互 = 有意味的人为干预"（IMO MASS Code §3.x）| 高（推 D3.9）|
| DUAL | CCS 可选 HEARTBEAT 轨道；SAT1_EQUIV 作辅助 | 中 |

**本草案推荐 DUAL 模式。** SAT1_EQUIV 完整论证在 D3.9 RFC-007 cyber 处理。

## 6. ROS2 接口草案（D2.8 实现输入）

```yaml
# topic: /l3/bnwas/state @1Hz
bnwas_state:
  mode: DUAL
  heartbeat_timer_s: 180      # 当前剩余 heartbeat 计时（倒计时）
  sat1_timer_s: 300           # 当前剩余 SAT-1 等效计时（倒计时）
  alarm_level: NORMAL         # NORMAL | LOCAL_AUDIO | ROC_AUDIO | MRC_PREPARE
  last_confirmation:
    timestamp_utc: ~
    trigger: ~                # heartbeat_confirmed | sat1_interaction
    operator_id: ~

# service: /l3/bnwas/heartbeat_confirm (std_srvs/Trigger)
# 由 Web HMI ToR 面板"在位确认"按钮触发
```

## 7. D2.8 stub 输入

本草案作为 D2.8 v1.1.3 stub §15（BNWAS 等效机制）的设计基础：
- §15 须引用本草案 §2–§5（设计方案 + 合规论证路径）
- §15 须包含 §6 的 ROS2 接口草案作为"接口占位符"
- **实现状态在 D2.8 stub 中标为 NotImplemented**；Phase 4 才完整实现

## 8. 参考文献

- [R-IMO-RSE] IMO MSC.1/Circ.1606 (2019) → MSC 102/5/1 (2020) BNWAS gap
- [R-CCS-REMOTE] CCS《智能船舶规范》远控站告警要求
- IMO MSC.282(86) — 桥楼 BNWAS 规则（参照基线）
- [NLM-BNWAS] High — IMO RSE 2021 + CCS alarm rules
