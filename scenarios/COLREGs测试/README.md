# COLREGs 测试场景套件

机读场景，schema `scenarios/fcb_traffic_situation.schema.json`（v3.0）。加载器
`tools/sil/scenario_spec.py`；编排器 `src/sil_orchestrator`（`ScenarioStore` 递归扫描，
本目录为 baseline 只读分类）。

## 场景清单

| 文件 | COLREGs | OS 角色 | 期望动作 | 备注 |
|---|---|---|---|---|
| `colreg-rule14-ho*.yaml` ×3 | R14 | give-way | 右转 | 对遇（含 8° 偏右） |
| `colreg-rule14-ho-port.yaml` | R14 | give-way | **右转** | 对遇，目标偏**左** 5°（仍在 ±22.5° 对遇扇区）；测"对遇绝不左转" |
| `colreg-rule15-cs*.yaml` ×4 | R15/R16 | give-way | 右转 | 右舷交叉让路 |
| `colreg-rule13-ot*.yaml` ×3 | R13 | give-way | 右转 | 正后/右后追越 |
| `colreg-rule17-cr-so.yaml` | R17/R15 | **stand-on** | 保向保速→末段独立行动 | 目标自左舷交叉、应让而不让（直线 replay）→ 触发 R17(b) |
| `colreg-rule17-cr-so-2.yaml` | R17/R15 | **stand-on** | 同上（更短 TCPA） | 目标更快、更早进入 stage-3 |
| `colreg-rule15-ms.yaml` | R15/R16 | give-way | 大幅右转/减速 | **多船**：双右舷交叉夹击，需统一解清两船 |
| `colreg-rule13-15-ms.yaml` | R13/R15 | give-way | 右转/减速 | **多船**：追越 TS1 同时右舷驶入 TS2，优先级 R15>R13 |
| `colreg-ms-headon-cross.yaml` | R14/R15 | give-way | 右转 | **多船**：对遇 TS1 + 右舷交叉 TS2 仲裁（同向右转解） |

新增 Tier-1/Tier-2 场景由 `tools/sil/gen_colreg_tier12.py` 生成（求解目标航速使
直线 DCPA≈0，构造真实碰撞风险），由 `tools/sil/verify_colreg_tier12.py` 本地校验
（schema 合法 + 加载器可解析 + M2 `encounter_classifier` 分类符合意图 + DCPA<500m）。

## 评估指标（已实现，复用现有打分层）

`src/sim_workbench/sil_nodes/scoring/`：
- `rule_compliance_evaluator.py` — R13/14/15/16/17 → full/partial/violated（查
  `rudder_side`/`heading_change_deg`/`role`/`timing_stage`）。R14 左舵=violated；
  R17 直航船早期大转向=violated。
- `kpi_deriver.py` — `min_cpa_nm` / `max_rudder_deg` / `avg_rot_dpm`（操纵平滑度）/
  `grounding_risk_score`。

## Tier-3 极限场景（暂缓，需 harness 改动，非本轮）

当前 harness 限制导致以下 corner case 无法仅靠写 YAML 表达：

1. **不合作机动目标**（如对遇中目标违规左转切入）：`target_vessel_node` 目标运动
   仅支持直线 `replay` 与随机游走 `ncdm`，**无脚本化机动**。需新增"脚本化机动"目标模式
   或接入 `trajectory_file` 重放。
2. **受限地缘/几何围栏交叉**（右舷紧邻浅滩 Geofence）：场景 schema **无任意多边形
   geofence/静态碍航物字段**，静态危险仅来自 ENC 海图（`enc_path`）。需补 ENC 浅滩数据
   或在 schema 增 geofence 字段。

> 副作用利用：因目标恒为直线，"应让而不让"的直航测试（R17 stage-3）反而无需改 harness
> 即可成立 —— 已在上面两个 `rule17-cr-so` 场景中使用。
